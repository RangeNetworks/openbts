/*
* Copyright 2008 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
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



#include <Sockets.h>
#include <ByteVector.h>
#include <CodecSet.h>
#include <Timeval.h>
#include <ControlTransfer.h>
#include "SIPUtility.h"
#include "SIPMessage.h"

extern int gCountSipDialogs;

namespace Control { class L3LogicalChannel; }

namespace SIP {
class SipDialog;
typedef RefCntPointer<SipDialog> SipDialogRef;
using namespace Control;
using namespace std;

extern bool gPeerTestedForSmqueue;

// These could be global.
extern string localIP(); // Replaces mSIPIP.
extern string localIPAndPort(); // Replaces mSIPIP and mSIPPort.

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

enum DialogType {
	SIPDTUndefined,
	SIPDTRegister,
	SIPDTUnregister,
	SIPDTMOC, // Mobile Originate Call
	SIPDTMTC, // Mobile Terminate Call
	SIPDTMOSMS,
	SIPDTMTSMS,
	SIPDTMOUssd
};

class DialogMessage;
class SipTransaction;

// This is a simple attachment point for a TranEntry.
class SipEngine {

	public:
	Control::TranEntryId mTranId;		// So we can find the TransactionEntry that owns this SIPEngine.

	void dialogQueueMessage(DialogMessage *dmsg);

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
	virtual DialogType vgetDialogType() const = 0;

	public:
	/** Return the current SIP call state. */
	// (pat) This is the dialog state, if we are a dialog.
	SipState getSipState() const { return mState; }
	// (pat) Set the dialog state.  The purpose of this protected class is to enforce that the dialog type is set
	// only with this method by all internal and external users.
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
		return false;	/*NOTREACHED*/
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
	// (pat 5-2104) The proxy address is set from config options depending on the type of message.
	// We currently dont support multiple upside peers.  We dont return the message using the via for example.
	IPAddressSpec mProxy;	// The remote URL.
	public:
	unsigned mLocalCSeq;	// The local CSeq for in-dialog requests.  We dont bother tracking the remote CSeq.
							// 14.1: The Cseq for re-INVITE follows same rules for in-dialog requests, namely,
							// CSeq num must be incremented by one.
	string dsPAssociatedUri;
	string dsPAssertedIdentity;

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
	void updateProxy(const char *sqlOption);
};


// SipBase is intended to have no dependencies on the rest of OpenBTS so that it can be used as the base class for
// the message parser classes, allowing them to be used stand-alone.
// SipDialog is the class with many dependencies elsewhere, including, for example, gSipInterface and SipRtp.
// SipBase includes much of the state for a SIP dialog, but SipBase can also be used to parse non-dialog SIP messages,
// in which case all the dialog state stuff is just ignored.
DEFINE_MEMORY_LEAK_DETECTOR_CLASS(SipBase,MemCheckSipBase)
class SipBase : public MemCheckSipBase,
	public RefCntBase, public SipEngine, public DialogStateVars, public SipBaseProtected
{
	friend class SipInterface;
protected:
	mutable Mutex mDialogLock;
	/**@name General SIP tags and ids. */

	SipMessage* mInvite;	///< the INVITE or MESSAGE message for this transaction
public:
	SipMessage *getInvite() const { return mInvite; }

	/**@name SIP UDP parameters */
	//SipState getSipState() const { return SipBaseProtected::getSipState(); }	// stupid language.

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
	int getLastResponseCode(string &reason) {	// Return is the reason phrase from an error response.
		if (mLastResponse) { reason = mLastResponse->smGetReason(); return mLastResponse->smGetCode(); }
		else return 0;
	}
	string getLastResponseReasonHeader() {	// Return is the reason phrase from an error response.
		return mLastResponse ? mLastResponse->msmReasonHeader : string("");
	}
	//string getLastResponseReason() { return mLastResponse ? mLastResponse->smGetReason() : string(""); }
	//@}

	// These go in the Transaction:
	std::string mInviteViaBranch;	// (pat) This is part of the individual Transaction.

	// Make an initial request: INVITE MESSAGE REGISTER
	// Make a standard initial request with info from the DialogState.
	SipMessage *makeRequest(string method,string requri, string whoami, SipPreposition* toContact, SipPreposition* fromContact, string branch);
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
	DialogType vgetDialogType() const { return mDialogType; }
	bool dgIsServer() const { return mDialogType == SIPDTMTC || mDialogType == SIPDTMTSMS; }

	/** Save a copy of an INVITE or MESSAGE message in the engine. */
	void saveInviteOrMessage(const SipMessage *INVITE, bool mine);

	/** Save a copy of a response message to an MO request in the engine. */
	void saveMOResponse(SipMessage *respsonse);

	/** Determine if this invite matches the saved one */
	bool sameInviteOrMessage(SipMessage * msg);

	bool sameDialog(SipBase *other);
	bool matchMessage(SipMessage *msg);
	SipDialog *dgGetDialog();

	public:
	void sbText(std::ostringstream&os) const;
	string sbText() const;
	virtual int vGetRtpPort() const { return 0; }
	Control::CodecSet vGetCodecs() const { return CodecSet(); }

	string dsHandoverMessage(string peer) const;

	// Add this to code where reason needs to be set
	// Example: SipBase::addCallTerminationReasonDlg(CallTerminationCause(CallTerminationCause::eTermSIP/eQ850, 100, "This is an error"));
	void addCallTerminationReasonDlg(CallTerminationCause::termGroup group, int cause, string desc) {
		LOG(INFO) << "SIP term info addCallTerminationReasonDlg cause: " << cause;
		SIPDlgCallTermList.add(group, cause, desc);
	}

	SipTermList& getTermList() { return SIPDlgCallTermList; }
private:
	SipTermList SIPDlgCallTermList;  // List of call termination reasons
};

struct SipCallbacks {
	// ttAddMessage
	typedef void (*ttAddMessage_functype)(TranEntryId tranid,DialogMessage *dmsg);
	static ttAddMessage_functype ttAddMessage_callback;
	static void ttAddMessage(TranEntryId tranid,SIP::DialogMessage *dmsg);
	static void setcallback_ttAddMessage(ttAddMessage_functype callback) {
		ttAddMessage_callback = callback;
	}

	// writePrivateHeaders
	typedef void (*writePrivateHeaders_functype)(SipMessage *msg, const L3LogicalChannel *l3chan);
	static writePrivateHeaders_functype writePrivateHeaders_callback;
	static void writePrivateHeaders(SipMessage *msg, const L3LogicalChannel *l3chan);
	static void setcallback_writePrivateHeaders(writePrivateHeaders_functype callback) {
		writePrivateHeaders_callback = callback;
	}
};

extern string OpenBTSUserAgent();

};
#endif
