/*
* Copyright 2008 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/
#ifndef _SIPBase_H_
#define _SIPBase_H_ 1
#include <string>
#include <sys/time.h>
#include <sys/types.h>
#include <semaphore.h>


#include <ortp/ortp.h>
#undef WARNING		// The nimrods defined this to "warning"
#undef CR			// This too


#include <Sockets.h>
#include <Globals.h>
#include <GSMTransfer.h>
#include <ControlTransfer.h>
#include <Timeval.h>
#include "SIPUtility.h"
#include "SIPMessage.h"

extern int gCountRtpSessions;
extern int gCountRtpSockets;
extern int gCountSipDialogs;

namespace SIP {
class SipDialog;
using namespace Control;
using namespace std;

extern bool gPeerIsBuggySmqueue;
extern bool gPeerTestedForSmqueue;

extern void writePrivateHeaders(SipMessage *msg, const L3LogicalChannel *chan);

// These could be global.
inline string localIP() { // Replaces mSIPIP.
	return gConfig.getStr("SIP.Local.IP");
}
inline string localIPAndPort() { // Replaces mSIPIP and mSIPPort.
	return format("%s:%u", localIP(), (unsigned) gConfig.getNum("SIP.Local.Port"));
}

// These are the overall dialog states.
enum SipState  {
	SSNullState,
	SSTimeout,
	Starting,		// (pat) MOC or MOSMS or inboundHandover sent INVITE; MTC not used.
	Proceeding,		// (pat) MOC received Trying, Queued, BeingForwarded; MTC sent Trying
	Ringing,		// (pat) MOC received Ringing, notably not used for MTC sent Ringing, which is probably a bug of no import.
	MOCBusy,		// (pat) MOC received Busy; MTC not used.
	Connecting,		// (pat) MTC sent OK.
	Active,			// (pat) MOC received OK; MTC sent ACK
	MODClearing,	// (pat) MOD sent BYE
	MODCanceling,	// (pat) MOD sent a cancel, see forceSIPClearing.
	MODError,		// (pat) MOD sent an error response, see forceSIPClearing.
	MTDClearing,	// (pat) MTD received BYE.
	MTDCanceling,	// (pat) MTD received CANCEL
	Canceled,		// (pat) received OK to CANCEL.
	Cleared,		// (pat) MTD sent OK to BYE, or MTD internal error loss of FIFO, or MOSMS received OK, or MTSMS sent OK.

	//SipRegister,	// (pat) This SIPEngine is being used for registration, none of the other stuff applies.
	//SipUnregister,	// (pat) This SIPEngine is being used for registration, none of the other stuff applies.
	SSFail,

	MOSMSSubmit,		// (pat) SMS message submitted, "MESSAGE" method.  Set but never used.  Message success indicated by Cleared.
	// (pat) The handover states are invalid SIPEngine states, set during initial state for handover,
	// used only to check if SIPEngine is dead because this state was never changed to a valid SIP state.
	HandoverInbound,
	//HandoverInboundReferred,	// pat removed, totally unused.
	HandoverOutbound	// (pat) Another fine state that we dont ever use.
};
std::ostream& operator<<(std::ostream& os, SipState s);
extern const char* SipStateString(SipState s);

enum DialogType { SIPDTUndefined, SIPDTRegister, SIPDTUnregister, SIPDTMOC, SIPDTMTC, SIPDTMOSMS, SIPDTMTSMS, SIPDTMOUssd };

class DialogMessage;
class SipTransaction;

// This is a simple attachment point for a TranEntry.
//typedef InterthreadQueue<DialogMessage> DialogMessageFIFO;
//typedef list<SipTransaction*> SipTransactionList;
class SipEngine {

	public:
	Control::TranEntryId mTranId;		// So we can find the TransactionEntry that owns this SIPEngine.

	//DialogMessageFIFO mDownlinkFifo;	// Messages from the SIP layer to Control Layer3.  Layer3 will read them from here.
	//DialogMessage *dialogRead();
	void dialogQueueMessage(DialogMessage *dmsg);

	//SipTransactionList mTransactionList;
	void setTranId(Control::TranEntryId wTranId) { mTranId = wTranId; }

	// Dont delete these empty constructor/destructors. They are needed because InterthreadQueue calls
	// delete DialogMessage and it causes weird error messages without these.
	SipEngine();
	virtual ~SipEngine();
};

// This class is for variables that not even SipBase is allowed to change directly.
class SipBaseProtected
{
	virtual void _define_vtable();
	// You must not change mState without going through setSipState to make sure the state age is updated.
	SipState mState;			///< current SIP call state
	Timeval mStateAge;
	virtual DialogType getDialogType() const = 0;

	public:
	/** Return the current SIP call state. */
	SipState getSipState() const { return mState; }
	/** Set the state.  Must be called by for all internal and external users. */
	void setSipState(SipState wState) { mState=wState; mStateAge.now(); }

	/** Return TRUE if it looks like the SIP session is stuck indicating some internal error. */
	bool sipIsStuck() const;

	/** Return true if the call has finished, successfully or not. */
	bool sipIsFinished() const {
		switch (mState) {
			case Cleared:  case MODClearing:  case MTDClearing:
			case Canceled: case MODCanceling: case MTDCanceling:
			case SSFail: case MODError:
			case SSTimeout:
				return true;
			case SSNullState:
			case Starting:
			case Proceeding:
			case Ringing:
			case MOCBusy:
			case Connecting:
			case Active:
			case MOSMSSubmit:
			case HandoverInbound:
			case HandoverOutbound:
				return false;
		}
	}

	SipBaseProtected() : mState(SSNullState) { mStateAge.now(); }
};


// The 'Dialog State' is a defined term in RFC3261.  It is established by the INVITE and 2xx reply,
// and can only be modified by re-INVITE.
// Be careful with the name - we also use a DialogState to mean the integral dialog state, so we call this a context.
class DialogStateVars {
	protected:
	friend class SipTransaction;
	// RFC3261 uses the terms 'remote URI' and 'remote target URI' interchangably.
	// The To and From headers optionally include a Display Name as well as the URI.
	// We are going to save the whole contact here, including the display name,
	// even though the Display Names are technically not part of the DialogStateVars.
	SipPreposition mLocalHeader;	// Used in From:
	SipPreposition mRemoteHeader; // Used in To: and sometimes in request-URI.  Updated from Contact: header.
	string mRouteSet;	// Complicated.  See RFC-3261 12.2.1.1  Immutable once set.
	std::string mCallId;	// (pat) dialog-id for inbound sip messages but also used for some outbound messages.
	IPAddressSpec mProxy;	// The remote URL.  It starts out as the proxy address, but is set from the Contact for established dialogs.
	public:
	unsigned mLocalCSeq;	// The local CSeq for in-dialog requests.  We dont bother tracking the remote CSeq.
							// 14.1: The Cseq for re-INVITE follows same rules for in-dialog requests, namely,
							// CSeq num must be incremented by one.

	DialogStateVars();
	string dsLocalTag() { return mLocalHeader.mTag; }
	string dsRemoteTag() { return mRemoteHeader.mTag; }
	string sipLocalUsername() const { return mLocalHeader.mUri.uriUsername(); }	// This is the IMSI or IMEI.
	string sipRemoteUsername() const { return mRemoteHeader.mUri.uriUsername(); }
	string sipRemoteDisplayname() const { return mRemoteHeader.mDisplayName.empty() ? sipRemoteUsername()  : dequote(mRemoteHeader.mDisplayName); }

	// Various ways to set local and remote headers:
	//void dsSetLocalNameAndHost(string username, string host, unsigned port = 0) {
	//	mLocalHeader.prepSetUri(makeUri(username,host,port));
	//}
	// This sets the local URI without a tag.  It is used for non-dialogs
	private:
	void dsSetLocalUri(string uri) { mLocalHeader.prepSetUri(uri); }
	public:
	void dsSetRemoteUri(string uri) { mRemoteHeader.prepSetUri(uri); }
	//void dsSetRemoteNameAndHost(string username, string host, unsigned port = 0) {
	//	mRemoteHeader.prepSetUri(makeUri(username,host,port));
	//}
	void dsSetLocalHeaderMT(SipPreposition *toheader, bool addTag);
	void dsSetLocalMO(const FullMobileId &msid, bool addTag);
	// Set local header directly, used for handover:
	void dsSetLocalHeader(const SipPreposition *header) { LOG(DEBUG); mLocalHeader = *header; }
	void dsSetRemoteHeader(const SipPreposition *header) { LOG(DEBUG); mRemoteHeader = *header; }
	void dsSetCallId(const std::string wCallId) { mCallId = wCallId; }

	// RFC3261 12.2.1.1: "The URI in the To field of the request MUST be set to the remote URI
	// from the dialog state. The tag in the To header field of the request
	// MUST be set to the remote tag of the dialog ID. The From URI of the
	// request MUST be set to the local URI from the dialog state. The tag
	// in the From header field of the request MUST be set to the local tag
	// of the dialog ID. If the value of the remote or local tags is null,
	// the tag parameter MUST be omitted from the To or From header fields, respectively.

	// Accessors:
	// We set the remote-tag when we create the URI for a dialog.
	// We set the local-tag the first time we hear from the peer, which for an inbound INVITE is
	// in that invite, or for outbound INVITE is the first response.
	//string dsRequestToHeader() const { return mRemoteHeader.toFromValue(); }
	//string dsRequestFromHeader() const { return mLocalHeader.toFromValue(); }
	// This does alot of copying, but at least they are strings so it is just refcnt incrementing.
	const SipPreposition *dsRequestToHeader() const { return &mRemoteHeader; }
	const SipPreposition *dsRequestFromHeader() const { return &mLocalHeader; }
	//string dsReplyToHeader() const { return mLocalHeader.toFromValue(); }
	//string dsReplyFromHeader() const { return mRemoteHeader.toFromValue(); }
	const SipPreposition *dsReplyToHeader() const { return &mLocalHeader; }
	const SipPreposition *dsReplyFromHeader() const { return &mRemoteHeader; }
	//string dsRemoteURI() const { return mRemoteHeader.mUri.uriValue(); }
	string dsRemoteURI() const { return format("sip:%s@%s",sipRemoteUsername(),mProxy.mipName); }
	// TODO_NOW: THIS IS WRONG!  See SipMessageRequestWithinDialog
	//string dsInDialogRequestURI() const { return mRemoteHeader.mUri.uriAddress(); }
	string dsInDialogRequestURI() const { return dsRemoteURI(); }
	unsigned dsNextCSeq() { return ++mLocalCSeq; }
	string dsToString() const;
	const std::string& callId() const { return mCallId; } 
	const IPAddressSpec *dsPeer() const { return &mProxy; }
	string localIP() const { return SIP::localIP(); }
	string localIPAndPort() const { return SIP::localIPAndPort(); }
	void updateProxy(const char *sqlOption) {
		string proxy = gConfig.getStr(sqlOption);
		if (! proxy.empty() && ! mProxy.ipSet(proxy,sqlOption)) {
			LOG(ALERT) << "cannot resolve IP address for"<<LOGVAR(proxy)<<" specified by"<<LOGVAR(sqlOption);
		}
	}
};

class SipRtp {
	Mutex mRtpLock;
	public:
	/**@name RTP state and parameters. */
	//@{
	unsigned mRTPPort;
	//short mRTPRemPort;
	//string mRTPRemIP;
	Control::CodecSet mCodec;
	RtpSession * mSession;		///< RTP media session
	unsigned int mTxTime;		///< RTP transmission timestamp in 8 kHz samples
	unsigned int mRxTime;		///< RTP receive timestamp in 8 kHz samples
	uint64_t mRxRealTime;		// In msecs.
	uint64_t mTxRealTime;		// In msecs.
	//@}

	/**@name RFC-2833 DTMF state. */
	//@{
	// (pat) Dont change mDTMF to char.  The unbelievably stupid <<mDTMS will write the 0 directly on the string prematurely terminating it.
	unsigned mDTMF;					///< current DTMF digit, \0 if none
	unsigned mDTMFDuration;		///< duration of DTMF event so far
	unsigned mDTMFStartTime;	///< start time of the DTMF key event
	unsigned mDTMFEnding;		///< Counts number of rtp end events sent; we are supposed to send three.
	//@}

	/** Return RTP session */
	RtpSession * RTPSession() const { return mSession; }

	/** Return the RTP Port being used. */
	unsigned RTPPort() const { return mRTPPort; }

	bool txDtmf();

	/** Set up to start sending RFC2833 DTMF event frames in the RTP stream. */
	bool startDTMF(char key);

	/** Send a DTMF end frame and turn off the DTMF events. */
	void stopDTMF();

	/** Send a vocoder frame over RTP. */
	void txFrame(GSM::AudioFrame* frame, unsigned numFlushed);

	/**
		Receive a vocoder frame over RTP.
		@param The vocoder frame
		@return audio data or NULL on error or no data.
	*/
	GSM::AudioFrame * rxFrame();
	void initRTP1(const char *d_ip_addr, unsigned d_port, unsigned dialogId);

	virtual string sbText() const = 0;
	virtual SipState getSipState() const = 0;

	void rtpInit();
	SipRtp() { rtpInit(); }
	void rtpStop();
	// The virtual keyword is not currently needed since we dont use pointers to SipRtp as a base class.
	virtual ~SipRtp() { rtpStop(); }
	void rtpText(std::ostringstream&os) const;
};


// MO, uses SIP Client transaction:
//   us -> INVITE -> them
//   us <- 1xx whatever <- them
//   us <- 200 OK <- them
//   us -> ACK -> them
// We dont use server transactions for requests within an INVITE (RFC3261 section 17.2.2 Figure 8)
// - the only thing the server transaction has to do is resend the reply if a new request arrives,
// so that is just handled by the dialog.
// For the INVITE server transaction (figure 7), the transaction layer is required to resend the 2xx
// MT, uses SIP Server transaction:
//   us <- INVITE <- them	  duplicate INVITE handled by SIP2Interface, not SipServerTransaction
//   us -> 1xx whatever -> them
//   us -> 200 OK -> them
//   us <- ACK <- them
DEFINE_MEMORY_LEAK_DETECTOR_CLASS(SipBase,MemCheckSipBase)
class SipBase : public MemCheckSipBase,
	public RefCntBase, public SipEngine, public DialogStateVars, public SipRtp, public SipBaseProtected
{
	friend class SipInterface;
protected:
	mutable Mutex mDialogLock;
	/**@name General SIP tags and ids. */

	SipMessage* mInvite;	///< the INVITE or MESSAGE message for this transaction
public:
	SipMessage *getInvite() const { return mInvite; }

	/**@name SIP UDP parameters */
	//@{
	SipState getSipState() const { return SipBaseProtected::getSipState(); }	// stupid language.


	string localSipUri() {
		return format("sip:%s@%s", sipLocalUsername(), localIPAndPort());
	}
	// Normally the localUserName would just be sipLocalUserName from the SipDialog, but
	// REGISTER is special because the on-going dialog for all REGISTER messages does not contain
	// the user info for any particular REGISTER, so we need to pass the user name in for that.
	string preferredIdentity(string localUsername) {
		return format("<sip:%s@%s>",localUsername,localIP());
	}
	string localContact(string localUsername, int expires=3600) {
		return format("<sip:%s@%s>;expires=%u",localUsername,localIPAndPort(),expires);
	}
	string proxyIP() const { return mProxy.mipIP; }
	string transportName() { return string("UDP"); }	// TODO
	bool isReliableTransport() { return transportName() == "TCP"; }	// TODO
	//@}

	//bool mInstigator;		///< true if this side initiated the call
	DialogType mDialogType;
	unsigned mDialogId;		// A numeric id in case we need it.  It may be used for the RTP call back.

	/**@name Saved SIP messages. */
	//@{
	// The SDP we received from the peer, in the MTC invite or the MOC 200 OK.  We need it for the IP and port number.
	// Note that in the MTC case the peer info is the offer, while in the MOC case it is the final answer.
	//string mSdpPeerInfo;
	string mSdpOffer, mSdpAnswer;
	string getSdpRemote() const { return mDialogType == SIPDTMTC ? mSdpOffer : mSdpAnswer; }
	SipMessage *mLastResponse; // used only for the 200 OK to INVITE to retrive the sdp stuff.
	// Return the response code, only for MO invites.
	int getLastResponseCode() { return mLastResponse ? mLastResponse->smGetCode() : 0; }
	//@}

	// These go in the Transaction:
	std::string mInviteViaBranch;	// (pat) This is part of the individual Transaction.

	// Make an initial request: INVITE MESSAGE REGISTER
	SipMessage *makeRequest(string method,string requri, string whoami, SipPreposition* toContact, SipPreposition* fromContact, string branch);
	// Make a standard initial request with info from the DialogState.
	SipMessage *makeInitialRequest(string method);	// with default URIs
public:
	Bool_z mIsHandover;	// TRUE if this dialog was established by an inbound handover.

	// Warning: sipWrite is only for non-invite transactions.
	// Instead use moWriteLowSide or mtWriteLowSide for INVITE or MESSAGE transactions.
	// However, sipWrite is called from L3MobilityManagement.cpp for the register message in the pre-transaction code
	void sipWrite(SipMessage *sipmsg);

	/**
		Default constructor. Initialize the object.
		// (pat) New way is to call SipDialogMT or SipDialogMO
		@param proxy <host>:<port>
	*/
	void SipBaseInit();
	void SipBaseInit(DialogType wDialogType, string proxy, const char *proxyProvenance); // , TranEntryId wTranId)

	// This constructor is just to count them.  Initialization is via SipBaseInit().
	SipBase() { gCountSipDialogs++; }
	/** Destroy held message copies. */
	virtual ~SipBase();

	bool dgIsInvite() const;	// As opposed to a MESSAGE.

	// Return if we are the server, ie, return true if communication was started by peer, not us.
	DialogType getDialogType() const { return mDialogType; }
	bool dgIsServer() const { return mDialogType == SIPDTMTC || mDialogType == SIPDTMTSMS; }

	/**@name Messages for SIP registration. */
	//@{
	/**
		Send sip register and look at return msg.
		Can throw SIPTimeout().
		@return True on success.
	*/
	SipMessage *makeRegisterMsg(DialogType wMethod, const L3LogicalChannel* chan, string RAND, const FullMobileId &msid, const char *SRES = NULL);	

	/**
		Send sip unregister and look at return msg.
		Can throw SIPTimeout().
		@return True on success.
	*/
	//bool unregister() { return (Register(SIPDTUnregister)); };

	//@}

	// unused string makeSDP(string rtpSessionId, string rtpVersionId);
	string makeSDPOffer();
	string makeSDPAnswer();


	/**@name Messages for MOD procedure. */
	//@{
	SipState MODSendCANCEL();
	//@}

	/** Save a copy of an INVITE or MESSAGE message in the engine. */
	void saveInviteOrMessage(const SipMessage *INVITE, bool mine);

	/** Save a copy of a response message to an MO request in the engine. */
	void saveMOResponse(SipMessage *respsonse);

	/** Determine if this invite matches the saved one */
	bool sameInviteOrMessage(SipMessage * msg);

	bool sameDialog(SipDialog *other);
	bool matchMessage(SipMessage *msg);
	SipDialog *dgGetDialog();

	public:
	void sbText(std::ostringstream&os, bool verbose=false) const;
	string sbText() const;

	/**
		Initialize the RTP session.
		@param msg A SIP INVITE or 200 OK wth RTP parameters
	*/
	public:
	void initRTP();
	void MOCInitRTP();
	void MTCInitRTP();

	/**
		Generate a standard set of private headers on initiating messages.
	*/
	string dsHandoverMessage(string peer) const;
	//bool dsDecapsulate(string msgstr);
};


// Typical OpenBTS message stream for MOC to MTC on same BTS:
// MOC invite->
// MOC	<-100
// MTC <-invite
// MTC	100->
// MTC	180->
// MOC <-180
// MTC 200->
// MTC <-ACK
// MOC	<-200
// CC Connect

class SipInviteServerTransactionLayerBase: public SipTimers, public virtual SipBase
{
	public:
	virtual void dialogPushState(SipState newState, int code, char letter=0) = 0;
	// This is used by inbound BYE or CANCEL.  We dont care which because both kill off the dialog.
	SipTimer mTimerJ;
	void setTimerJ() { if (!dsPeer()->ipIsReliableTransport()) { mTimerJ.setOnce(64 * T1); } }
	void SipMTBye(SipMessage *sipmsg);
	void SipMTCancel(SipMessage *sipmsg);
};

// The SipStates used here and their correspondence to RFC3261 server transaction states are:
// Our SipState      | RFC3261 INVITE server       | RFC3261 non-INVITE server
// Starting          | N/A                         | Trying
// Proceeding        | Proceeding                  | Proceeding
// Connecting        | TU sent OK, TL goes to state Terminated, but we go to Completed | N/A
// Active            | Confirmed                   | Completed
// SSFail, various cancel/bye states
class SipMTInviteServerTransactionLayer : public virtual SipInviteServerTransactionLayerBase
{
	SipTimer mTimerG;	// Resend response for unreliable transport.

	// TimerHJ is how long we wait for a response from the peer before declaring failure.
	// For non-INVITE it is TimerJ, and for INVITE it is timerH.
	// For INVITE it is the wait for additional requests to be answered with the previous response.
	// In RFC3261 the 200 OK reply is passed to the TU which needs a similar delay; but we are going to use the same
	// state machine to send the 200 OK response, which makes it look more similar to the non-INVITE server transaction (figure 8.)
	// After an ACK is received, TimerI is how long we will soak up additional ACKs, but who cares?  We will soak
	// them up as long as the dialog is extant.
	SipTimer mTimerH;

	// There is no SIP specified timer for the 2xx response to an INVITE.
	// Eventually the MS will stop waiting and we will be canceled from that side.

	SipMessage mtLastResponse;

	void stopTimers() { mTimerG.stop(); mTimerH.stop(); /*mTimerI.stop();*/ }
	void setTimerG() { if (!dsPeer()->ipIsReliableTransport()) { mTimerG.setOnce(T1); } }
	void setTimerH() { if (!dsPeer()->ipIsReliableTransport()) { mTimerH.setOnce(64 * T1); } }

	protected:
	void mtWriteLowSide(SipMessage *sipmsg) {	// Outgoing message.
		mtLastResponse = *sipmsg;
		sipWrite(sipmsg);
	}

	public:

	void MTCSendTrying();
	void MTCSendRinging();
	void MTCSendOK(CodecSet wCodec, const L3LogicalChannel *chan);

	// Doesnt seem like messages need the private headers.
	void MTSMSReply(int code, const char *explanation); // , const L3LogicalChannel *chan)
	void MTSMSSendTrying();

	// This can only be used for early errors before we get the ACK.
	void MTCEarlyError(int code, const char*reason);	// The message must be 300-699.

	// This is called for the second and subsequent received INVITEs as well as the ACK.
	// We send the current response, whatever it is.
	void MTWriteHighSide(SipMessage *sipmsg);

	// Return TRUE to remove the dialog.
	bool mtPeriodicService();
	string mttlText() const;	// MT Transaction Layer Text
};


class SipMOInviteClientTransactionLayer : public virtual SipInviteServerTransactionLayerBase
{
	SipTimer mTimerAE;	// Time to resend initial INVITE for non-reliable transport.
	SipTimer mTimerBF;	// Timeout during INVITE phase.
	virtual void handleInviteResponse(int status, bool sendAck) = 0;
	void stopTimers() { mTimerAE.stop(); mTimerBF.stop(); mTimerK.stop(); mTimerD.stop(); }

	protected:
	// Timers K and D are for non-invite client transactions, MO BYE and MO CANCEL.
	SipTimer mTimerK;	// Timeout destroys dialog.
	SipTimer mTimerD;	// Timeout destroys dialog.
	void MOCSendINVITE(const L3LogicalChannel *chan = NULL);
	void MOUssdSendINVITE(string ussd, const L3LogicalChannel *chan = NULL);
	void handleSMSResponse(SipMessage *sipmsg);
	void MOWriteHighSide(SipMessage *sipmsg);	// Incoming message from outside, called from SIPInterface.
	void moWriteLowSide(SipMessage *sipmsg);	// Outgoing message from us, called only by SIPBase descendents.

	public:
	void MOCSendACK();
	void MOSMSSendMESSAGE(const string &messageText, const string &contentType);

	// This is a temporary routine to work around bugs in smqueue.
	// Resend the message with changes (made by the caller) to see if it works any better.
	bool MOSMSRetry();
	bool moPeriodicService(); // Return TRUE to remove the dialog.
	string motlText() const;	// MO Transaction Layer Text
};


};
#endif
