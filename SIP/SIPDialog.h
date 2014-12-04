/*
* Copyright 2013, 2014 Range Networks, Inc.
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
// Written by Pat Thompson.

#ifndef SIPDIALOG_H
#define SIPDIALOG_H
#include <GSML3CCElements.h>
#include <ControlTransfer.h>
#include <L3TermCause.h>
#include "SIPExport.h"
#include "SIPMessage.h"
#include "SIPBase.h"
#include "SIPRtp.h"
#include "SIPTransaction.h"


namespace SIP {

class SipDialogBase: public virtual SipBase, public SipRtp
{
	public:
	void sipWrite(SipMessage *sipmsg);
	SipDialog *dgGetDialog();
	SipState MODSendCANCEL(Control::TermCause l3Cause);
	void initRTP();
	void MOCInitRTP();
	void MTCInitRTP();
	string sdbText() const;
	void sdbText(std::ostringstream&os, bool verbose=false) const;
	string makeSDPOffer();
	string makeSDPAnswer();
	int vGetRtpPort() const { return this->SipRtp::mRTPPort; }
	Control::CodecSet vGetCodecs() const { return this->SipRtp::mCodec; }
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

class SipInviteServerTransactionLayerBase: public SipTimers, public virtual SipDialogBase
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
	void MTCEarlyError(Control::TermCause cause);	// The message must be 300-699.

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

	bool moPeriodicService(); // Return TRUE to remove the dialog.
	string motlText() const;	// MO Transaction Layer Text
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

// The antecedent classes are virtual because they all are descendents of a single SipBase.
DEFINE_MEMORY_LEAK_DETECTOR_CLASS(SipDialog,MemCheckSipDialog)
class SipDialog : public MemCheckSipDialog, public virtual SipDialogBase,
	public virtual SipMOInviteClientTransactionLayer, public virtual SipMTInviteServerTransactionLayer
{

	private:

	// We only send one state change message to L3 for each change of DialogState.  This is the previous one we sent.
	// There is no current dialog state - it is derived from SIPBase::mState
	DialogState::msgState mPrevDialogState;

	bool permittedTransition(DialogState::msgState oldState, DialogState::msgState newState);
	// Change the sip state and possibly push a message to L3.
	public: void dialogPushState(SipState newState, int code, char timer = 0);
	// Possibly send a message to L3 if the SIP state has changed.
	private: void dialogChangeState(SipMessage *dmsg);
	void registerHandleSipCode(SipMessage *msg);

	public:
	Bool_z mReceived180;	// The 1xx response, with the caveat that 180 (ringing) is saved over others.

	// To work around the buggy smqueue we need to resend the SMS message so we need to save it.
	// There is another copy saved down in the transaction layer, but it is cleaner to just save it up here
	// than try to dig it out of the lower layer resend machinery.
	string smsBody, smsContentType;		// Temporary, until smqueue is fixed.

	void dgReset();
	DialogState::msgState getDialogState() const;
	bool isActive() const { return getDialogState() == DialogState::dialogActive; }
	bool isFinished() const { DialogState::msgState st = getDialogState(); return st == DialogState::dialogFail || st == DialogState::dialogBye; }
	bool dgIsDeletable() const;
	const char *dialogStateString() const { return DialogState::msgStateString(getDialogState()); }
	//void dialogOpen(const char *userid);	// The userid is the IMSI.
	//void dialogClose();
	void MODSendBYE(Control::TermCause l3Cause);
	void sendInfoDtmf(unsigned bcdkey);

	// Send an error code that terminates the dialog.
	void sendError(int code, const char *errorString) {LOG(ALERT) << "unimplemented"<<code<<errorString;}	// TODO;

	// Dont call this directly.  Use one of the static newSipDialog.... methods.
	SipDialog(DialogType wDialogType,
		string wProxy,			// The proxy IP address or DNS name.
		const char * wProxyProvenance):	// A helpful message in case the proxy address cannot be resolved.
		mPrevDialogState(DialogState::dialogUndefined)
		{
			SipBaseInit(wDialogType, wProxy, wProxyProvenance);
		}

	// This is the new way:
	static SipDialog *newSipDialogMT(DialogType dtype,
			SipMessage *request);		// INVITE or MESSAGE.

	static SipDialog *newSipDialogMOC(TranEntryId tranid, const FullMobileId &msid,
		const string&wCalledDigits, Control::CodecSet wCodecs,
		L3LogicalChannel *chan);
	static SipDialog *newSipDialogMOUssd(TranEntryId tranid, const FullMobileId &msid, const string&wUssd, L3LogicalChannel *chan);
	static SipDialog *newSipDialogMOSMS(TranEntryId tranid, const FullMobileId &msid, const string &calledDigits, const string &body, const string &contentType);
	static SipDialog *newSipDialogRegister1();
	static SipDialog *newSipDialogHandover(TranEntry *tran, string sipReferStr);

	virtual ~SipDialog();
	void sipStartTimers() {	// We dont do it this way any more.
	}
	void sipStopTimers() {	// We dont do it this way any more.
	}
	bool dgWriteHighSide(SipMessage *msg) {
		switch (mDialogType) {
			case SIPDTMTC: case SIPDTMTSMS:
				MTWriteHighSide(msg); return true;
			case SIPDTMOC: case SIPDTMOSMS: case SIPDTMOUssd:
				MOWriteHighSide(msg); return true;
			default: return false;	// This indicates a serious error on the part of the SIP peer.
		}
	}

	void dialogCancel(TermCause cause/*, GSM::CCCause l3Cause*/);
	//void dialogMOCSendInvite(const char *bcddigits,Control::CodecSet codecs);
	void dialogWriteDownlink(SipMessage *msg);
	void handleInviteResponse(int status, bool sendAck);
	bool dialogPeriodicService();
	string dialogText(bool verbose=true) const;
	Control::TranEntry *createMTTransaction(SipMessage *invite);
	Control::TranEntry *findTranEntry();
	void handleUssdBye(SipMessage *msg);
	/**@name Messages for SIP registration. */
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
	string vsdbText() const { return sdbText(); }
	SipState vgetSipState() const { return getSipState(); }
};

std::ostream& operator<<(std::ostream& os, const DialogState);
std::ostream& operator<<(std::ostream& os, const SipDialog&);
std::ostream& operator<<(std::ostream& os, const SipDialogRef&);
std::ostream& operator<<(std::ostream& os, const SipDialog*);

extern SipDialog *gRegisterDialog;

};	// namespace
#endif
