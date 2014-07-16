/* Copyright 2013, 2014 Range Networks, Inc.
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

#ifndef _SIPEXPORTH_
#define _SIPEXPORTH_ 1

//#include <SIPBase.h>
#include <ControlTransfer.h>

namespace SIP {
extern void startRegister(TranEntryId tid, const Control::FullMobileId &msic, const string rand, const string sres, L3LogicalChannel *chan);
extern void startUnregister(const FullMobileId &msid, L3LogicalChannel *chan);
class SipDialog;
extern SipDialog *getRegistrar();

// These are the SipDialog states that are sent as messages to the L3 state machines.
// They are a subset of SIPState, because SIPState tracks the exact state of
// the various acknowledgements, while this only cares about the overall state of the dialog.
// For example, if the dialog is being cleared or canceled, L3 no longer cares about who initiated the clearing.
// Note: ORDER IS IMPORTANT.  In general you can transition only forward through these states
// so instead of having a big allowed state transition table, we only allow forward progress,
// with any exceptions handled specially.
struct DialogState {
	enum msgState {
	dialogUndefined,		// The initial state until something happens.  For MO we stay here until answer to INVITE
	dialogStarted,			// initial INVITE sent.
	dialogProceeding,
	dialogRinging,
	dialogActive,
	dialogBye,
	dialogFail,			// busy, cancel or fail for any reason.

	// Other messages not related to the current dialog state.
	dialogDtmf,
	};
	static const char *msgStateString(DialogState::msgState dstate);
};

struct SipCode {
	int mCode;
	string mReason;
	SipCode() : mCode(0), mReason("") {}
	SipCode(int wCode, string wReason) : mCode(wCode), mReason(wReason) {}
};

class DialogMessage {
	//virtual void _define_vtable();
	public:
	virtual ~DialogMessage() {}
	Control::TranEntryId mTranId;		// The associated TransactionEntry or 0 for the old MobilityManagement SipBase which has none.
										// By using the TransactionId instead of a pointer, we dont crash if the TransactionEntry disappears
										// while this message is in flight.
										// Update: now the message is queued into the TranEntry, so this probably is not used.
	DialogState::msgState mMsgState;
	//SipMethod::MethodType mMethod;	// If not a method then SipMethod:Undefined.
	unsigned mSipStatusCode;			// eg 200 for OK.
	DialogMessage(Control::TranEntryId wTranId,DialogState::msgState nextState, unsigned code) :
		mTranId(wTranId), mMsgState(nextState), mSipStatusCode(code) {}
	DialogState::msgState dialogState() const { return mMsgState; }
	unsigned sipStatusCode() const { return mSipStatusCode; }

	// Works, but not used:
	// bool isBusy() const { return mSipStatusCode == 486 || mSipStatusCode == 600 || mSipStatusCode == 603; }

	// What a brain dead language.
#define DIALOG_MESSAGE_CONSTRUCTOR(subclass) \
	subclass(Control::TranEntryId wTranId,DialogState::msgState nextState, unsigned code) : DialogMessage(wTranId,nextState,code) {}
};

struct DialogUssdMessage : public DialogMessage {
	string dmMsgPayload;			// For USSD message.
	DIALOG_MESSAGE_CONSTRUCTOR(DialogUssdMessage)
};

struct DialogChallengeMessage : public DialogMessage {
	string dmRand;				// for 401 message.
	Int_z dmRejectCause;
	DIALOG_MESSAGE_CONSTRUCTOR(DialogChallengeMessage)
};

struct DialogAuthMessage : public DialogMessage {
	string dmKc;
	string dmPAssociatedUri;
	string dmPAssertedIdentity;
	DIALOG_MESSAGE_CONSTRUCTOR(DialogAuthMessage)
};

std::ostream& operator<<(std::ostream& os, const DialogMessage&);
std::ostream& operator<<(std::ostream& os, const DialogMessage*);
};
#endif
