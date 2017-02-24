/**@file Declarations for Circuit Switched State Machine and related classes. */
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

// TODO: To avoid bugs where the state machines get stuck,
// send a HARDRELEASE from L3 when mT3109 expires, which is the uplink activity counter in XCCHL1Decoder,
// which is used for TCHFACCHL1Decoder, SDDCHL1Decoder and SACCHL1Decoder.
// Technique is similar to: if (mUpstream!=NULL) mUpstream->writeLowSide(L2Frame(ESTABLISH));
// Currently, the TCH handler in Control dir polls radioFailure() which eventually checks mT3109.
// For SDCCH, the ch just gets reused after mT3109 expiry, and L3 is not notified - we just hope it is no longer in use after 30s.


#ifndef CSL3STATEMACHINE_H
#define CSL3STATEMACHINE_H

#include <map>

#include <Logger.h>
#include <Interthread.h>
#include <Timeval.h>


#include <GSMTransfer.h>
#include "ControlCommon.h"
#include "L3Utils.h"
#include "L3TermCause.h"
//#include <GSML3CommonElements.h>
//#include <GSML3MMElements.h>
//#include <GSML3CCElements.h>
//#include <GSML3Message.h>		// Doesnt this poor L3Message get lonely?  When apparently there are multiple L3MMMessages and L3CCMessages?
#include <GSML3MMMessages.h>
#include <GSML3CCMessages.h>
#include <GSML3RRMessages.h>
//#include <SIPDialog.h>
namespace SIP { class SipDialog; };

// These are only for use inside state machines:
#define PROCLOG(level) LOG(level)<<machText()
#define PROCLOG2(level,state) LOG(level) <<LOGHEX(state)<<machText()<<" "

namespace Control {
using namespace GSM;
class TranEntry;
class MMContext;
extern void L3DCCHLoop(L3LogicalChannel*dcch, L3Frame *frame);

#if UNUSED_BUT_SAVE_FOR_UMTS
typedef InterthreadQueue<GenericL3Msg> CSL3StateMachineFifo;
#endif

// This is a return state from a state machine.

struct MachineStatus {
	// There are only 4 things a state machine procedure can do on return.
	// Any error returns have to map to one of these four.
	enum MachineStatusCode {
		MachineCodeOK,			// continue the procedure, meaning return to L3 message handler and wait for the next message.
		MachineCodePopMachine,	// return to previous procedure on stack
		MachineCodeQuitTran,	// Pop all machines from stack and remove the transaction.  This is the normal exit from a completed procedure.
		MachineCodeQuitChannel,	// Drop the channel, which kills all transactions on this channel.
		MachineCodeUnexpectedState,		// Unexpected message or state was not handled by the current state machine.
	};
	MachineStatusCode msCode;
	TermCause msCause;	// If it is QuitTran or QuitChannel.
	bool operator==(MachineStatus &other) { return msCode == other.msCode; }
	bool operator!=(MachineStatus &other) { return msCode != other.msCode; }
	MachineStatus(MachineStatusCode code) { msCode = code; }
	static MachineStatus QuitTran(TermCause wCause) {
		MachineStatus result(MachineCodeQuitTran);
		result.msCause = wCause;
		return result;
	}
	static MachineStatus QuitChannel(TermCause wCause) {
		MachineStatus result(MachineCodeQuitChannel);
		result.msCause = wCause;
		return result;
	}

};
std::ostream& operator<<(std::ostream& os, MachineStatus::MachineStatusCode state);

// These ones have no arguments so they might as well be constants.
extern MachineStatus MachineStatusOK, MachineStatusPopMachine, MachineStatusAuthorizationFail;
extern MachineStatus MachineStatusAuthorizationFail, MachineStatusUnexpectedState;
//extern MachineStatus MachineStatusQuitChannel;
//extern MachineStatus MachineStatusQuitTran;


struct MachineStatusQuitTran : MachineStatus {
	//MachineStatusQuitTran(GSM::CCCause wcause) : MachineStatus(MachineCodeQuitTran) { mCCCause = wCCcause; }
};



// A base class for the individual Procedure state machines.
// Formerly this contained state so had to be unique for each state machine.
DEFINE_MEMORY_LEAK_DETECTOR_CLASS(MachineBase,MemCheckMachineBase)
class MachineBase : public MemCheckMachineBase
{
	friend class TranEntry;
	friend class ProcCommon;
	protected:
	int mPopState; // If we push into a procedure, save the return location here.
	public:
	// Args are the L3Procedure we are calling and the state in the current procedure to which we will return when nextProcedure is popped.
	MachineStatus machPush( MachineBase*wCalledProcedure, int wNextState);

	// The one and only TranEntry that is running this procedure.
	// The CallHold and CallWaiting will have multiple TransactionEntries and multiple procedures.
	private: TranEntry *mTran;

	// No TranEntry yet?  Then no L3Procedure can be started.
	protected:
	MachineBase(TranEntry *wTran): mTran(wTran) {
		mPopState = 0;		// unnecessary but neat.
	}
	void setTran(TranEntry *wTran) { mTran = wTran; }
	TranEntry *tran() const { return mTran; }
	MMContext *getContext() const;
	MachineBase *currentProcedure() { return this; }

	MachineStatus callMachStart(MachineBase* wProc, unsigned startState=0);

	void timerStart(L3TimerId tid, unsigned val, int nextState);	// Executes nextState on expiry.
	void timerStop(L3TimerId tid);
	void timerStopAll();
	bool timerExpired(L3TimerId tid);		// Is it expired?

	public:
	virtual ~MachineBase() {}	// Must be virtual to allow derived L3Procedures to delete themselves properly.
	void machText(std::ostream&) const;
	string machText() const;

	// accessors
	L3LogicalChannel *channel() const; 	// may be SDCCH or FACCH.
	CallState getGSMState() const;
	void setGSMState(CallState);
	SIP::SipDialog *getDialog() const;
	void setDialog(SIP::SipDialog*dialog);
	unsigned getL3TI() const;
	bool isL3TIValid() const;
	virtual const char *debugName() const = 0;
	MachineStatus unexpectedState(int state, const L3Message*l3msg);
	MachineStatus closeChannel(RRCause rrcause,Primitive prim,TermCause cause);
	void machineErrorMessage(int level, int state, const L3Message *l3msg, const SIP::DialogMessage *sipmsg, const char *format);

	virtual void handleTerminationRequest() {}	// Procedure can over-ride this to do nicer cleanup.

	// Methods for Procedure States.
	// The primary entry point for the state machine at the specified state.
	// State 0 is always reserved as the initial start state.
	// If this invocation is due to a message arrival, it is included as an argument.
	// The other possibilities are: timeout, popProcedure, or initial invocation in state 0.

	// The state machine must implement one of the following methods, depending on how much control it wants over its input.
	virtual MachineStatus machineRunState(int state, const GSM::L3Message *l3msg=NULL, const SIP::DialogMessage *sipmsg=NULL);
	virtual MachineStatus machineRunState1(int state, const GSM::L3Frame *frame=NULL, const GSM::L3Message *l3msg=NULL, const SIP::DialogMessage *sipmsg=NULL);

	//virtual MachineStatus procRunSipMsg(const SIP::DialogMessage *sipmsg) = 0;

	MachineStatus dispatchSipDialogMsg(const SIP::DialogMessage *msg);
	MachineStatus dispatchL3Msg(const GSM::L3Message *msg);
	MachineStatus dispatchTimeout(L3Timer *timer);
	MachineStatus dispatchFrame(const L3Frame *frame, const L3Message *l3msg);
};


#if UNUSED_BUT_SAVE_FOR_UMTS
// TODO: This class could go away.  I am keeping it around to see if it is useful for UMTS.
// This is the outer layer state machine to process CS L3 Messages.
// CS [Circuit Switched] L3 messages are specified in 3GPP 4.08, as opposed to PS [Packet Switched] L3 messages handled by the SGSN.
// This is part of the L3 rewrite.
class CSL3StateMachine
{
	CSL3StateMachineFifo mCSL3Fifo;
	Thread* mCSL3Thread;

	public:
	CSL3StateMachine();
	//void csl3ServiceLoop();
	//void csl3Start();
	// Put a message on the queue of messages to process.
	//bool csl3Write(GenericL3Msg *msg);		// return TRUE if the message was handled.
};
extern CSL3StateMachine gCSL3StateMachine;
#endif

};	// namespace Control

#endif

