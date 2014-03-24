/*
* Copyright 2013, 2014 Range Networks, Inc.
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
// Written by Pat Thompson.

#ifndef SIPDIALOG_H
#define SIPDIALOG_H
#include <ControlTransfer.h>
#include "SIPExport.h"
#include "SIPMessage.h"
#include "SIPBase.h"
#include "SIPTransaction.h"


namespace SIP {


// This is the start of a RFC-3261 Transaction Layer state machine per call_id to filter the messages before delivery to L3.
DEFINE_MEMORY_LEAK_DETECTOR_CLASS(SipDialog,MemCheckSipDialog)
// The antecedent classes are virtual because they all are descendents of a single SipBase.
class SipDialog : public MemCheckSipDialog, public virtual SipBase,
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
	void MODSendBYE();
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

	void dialogCancel(CancelCause cause = CancelCauseUnknown);
	//void dialogMOCSendInvite(const char *bcddigits,Control::CodecSet codecs);
	void dialogWriteDownlink(SipMessage *msg);
	void handleInviteResponse(int status, bool sendAck);
	bool dialogPeriodicService();
	string dialogText(bool verbose=true) const;
	Control::TranEntry *createMTTransaction(SipMessage *invite);
	Control::TranEntry *findTranEntry();
	void handleUssdBye(SipMessage *msg);
};
std::ostream& operator<<(std::ostream& os, const DialogState);
std::ostream& operator<<(std::ostream& os, const SipDialog&);
std::ostream& operator<<(std::ostream& os, const SipDialog*);

extern SipDialog *gRegisterDialog;

};	// namespace
#endif
