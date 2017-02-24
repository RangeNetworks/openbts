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
// Written by Pat Thompson
#define LOG_GROUP LogGroup::Control		// Can set Log.Level.Control for debugging
#include "L3StateMachine.h"
#include "L3CallControl.h"
#include "L3TranEntry.h"
#include "L3MobilityManagement.h"
#include "L3MMLayer.h"
#include "L3Handover.h"
#include <GSMLogicalChannel.h>
#include <GSML3Message.h>
#include <GSML3CCMessages.h>
#include <GSML3RRMessages.h>
#include <GSML3MMMessages.h>
#include <SMSMessages.h>
#include <GSMConfig.h>
#include <RRLPServer.h>
#include <Globals.h>
#include <typeinfo>
#include <iostream>

using namespace std;
using namespace GSM;
namespace Control {

// See documentation as class MachineStatus.
MachineStatus MachineStatusOK = MachineStatus(MachineStatus::MachineCodeOK);
MachineStatus MachineStatusPopMachine = MachineStatus(MachineStatus::MachineCodePopMachine);
//MachineStatus MachineStatusQuitTran = MachineStatus(MachineStatus::MachineCodeQuitTran);
//MachineStatus MachineStatusQuitChannel = MachineStatus(MachineStatus::MachineCodeQuitChannel);
MachineStatus MachineStatusAuthorizationFail = MachineStatus(MachineStatus::MachineCodeQuitChannel);
MachineStatus MachineStatusUnexpectedState = MachineStatus(MachineStatus::MachineCodeUnexpectedState);

std::ostream& operator<<(std::ostream& os, MachineStatus::MachineStatusCode status)
{
#define CASE1(x) case MachineStatus::x: os<<#x; break;
	switch (status) {
		CASE1(MachineCodeOK)
		CASE1(MachineCodePopMachine)
		//CASE1(MachineCodeUnexpectedMessage)
		//CASE1(MachineCodeUnexpectedPrimitive)
		CASE1(MachineCodeUnexpectedState)
		//CASE1(MachineCodeAuthorizationFail)
		CASE1(MachineCodeQuitTran)
		CASE1(MachineCodeQuitChannel)
	}
	return os;
}


//void MachineBase::timerStartAbort(L3TimerId tid, unsigned val) { tran()->timerStartAbort(tid,val); }

void MachineBase::timerStart(L3TimerId tid, unsigned val, int nextState)
{
	tran()->timerStart(tid,val,nextState);
}

void MachineBase::timerStop(L3TimerId tid)
{
	tran()->timerStop(tid);
}

void MachineBase::timerStopAll()
{
	tran()->timerStopAll();
}

void MachineBase::machineErrorMessage(int level, int state, const L3Message *l3msg, const SIP::DialogMessage *sipmsg, const char *format)
{
	ostringstream os;
	os << pthread_self() << Utils::timestr() << " " <<debugName() <<":" <<format;

	// This kind sucks, digging into the Logger.  The logger could be better.
	if (l3msg) {
		Log(level).get() <<os <<" Unexpected L3 message:"<<l3msg;
	} else if (sipmsg) {
		Log(level).get() <<os <<" Unexpected SIP message:"<<sipmsg;
	} else {
		Log(level).get() <<os <<" Unexpected"<<LOGHEX(state);
	}
}

MachineStatus MachineBase::unexpectedState(int state, const L3Message*l3msg)
{
	if (l3msg) {
		LOG(INFO)<<"Unexpected message ignored:"<<l3msg->text();
	} else {
		LOG(INFO)<<"Unexpected state:"<<state;
	}
	// Just keep going.  This may be wrong.
	return MachineStatusUnexpectedState;
}

// compiler says this is ambiguous - why? does it think it convert from L3TimerId to const char *?
bool MachineBase::timerExpired(L3TimerId tid) { return tran()->L3TimerList::timerExpired(tid); }

MMContext *MachineBase::getContext() const { return mTran->teGetContext(); }

void MachineBase::machText(std::ostream&os) const
{
	os <<" Machine=(";
	os <<debugName()<<LOGVAR2("tid",tran()->tranID()) <<" ";
	if (channel()) { os <<channel()->descriptiveString(); } else { os <<"(no chan)"; }
	os <<LOGVAR2("CCState",tran()->getGSMState());
	os <<LOGVARM(mPopState);
	os <<")";
	//if (mSipHandlerState >= 0) os<<LOGVAR(mSipHandlerState);
	//for (unsigned i = 0; i < mNumTimers; i++) {
	//	if (mTimers[i].active()) {
	//		os<<" timer "<<i<<" rem="<<mTimers[i].remaining()<<" nextState="<<mTimeoutNextState[i];
	//	}
	//}
}

string MachineBase::machText() const
{
	ostringstream os; machText(os); return os.str();
}


MachineStatus MachineBase::machPush(
	MachineBase*wCalledProcedure,	// The L3Procedure we are calling.
	int wNextState)		// The state in the current procedure to which we will return when nextProcedure is popped.
{
	mPopState = wNextState;
	tran()->tePushProcedure(wCalledProcedure);
	// TODO: This may need a recursive call to handleMachineStatus
	return this->callMachStart(wCalledProcedure);
}

MachineStatus MachineBase::closeChannel(RRCause rrcause,Primitive prim,TermCause upstreamCause)
{
	LOG(INFO) << "SIP term info closeChannel L3RRCause: " << rrcause; // SVGDBG
	// We dont want to set to NullState because we want to differentiate the startup state from the closed state
	// so that if something new happens (like a SIP dialog message coming in) we wont advance, we'll stay dead.
	// TODO: Make sure the routines that handle incoming dialog messages check for channel already in a released state.
	// This is not quite right: The ReleaseRequest state is for sending a CC Release, not an RR ChannelRelease.
	setGSMState(CCState::ReleaseRequest);	// The chanClose below will send the request.
	// Many handsets never complete the transaction.
	// So force a shutdown of the channel.
	channel()->chanClose(rrcause,prim,upstreamCause);	// TODO: Remove, now redundant.
	return MachineStatus::QuitChannel(upstreamCause);
}

#if UNUSED
ControlLayerException MachineBase::procHandleL3Msg(GSM::L3Message *l3msg, L3LogicalChannel *lch)
{
	// Look up the message in the Procedure message table.
	int state = findMsgMap(l3msg->PD(),l3msg->MTI());
	if (state == -1) {
		// If state is -1, message is not mapped and is ignored by us.
		return L3OK();  // TODO: Return what indicator?
	}
	IF you use this again: procInvoke was replaced by calls to handleMachineStatus inside lockAnd... methods of TranEntry
	procInvoke(state,l3msg,NULL);
}
#endif

#if UNUSED
// The proc may be this, ie, tran->currentProcedure()
MachineStatus MachineBase::callProcState(MachineBase *wProc, unsigned state)
{
	tran()->teSetProcedure(wProc,false);
	return wProc->machineRunState(state);
}
#endif

// This switches to the specifed Procedure and starts it.  It is equivalent to a long goto.
// It is also legal for the procedure to already be the current procedure, in which case we just start it.
MachineStatus MachineBase::callMachStart(MachineBase *wProc, unsigned startState)
{
	tran()->teSetProcedure(wProc,false);
	LOG(DEBUG) << "start Procedure:"<<wProc->machText();
	// TODO: Call handleMachineStatus(MachineStatus status)
	return tran()->handleRecursion(wProc->machineRunState1(startState));	// Unless the StateMachine starts with a message, the start state must be state 0 in every Machine.
}

L3LogicalChannel* MachineBase::channel() const {
	return tran()->channel();
}


CallState MachineBase::getGSMState() const { return tran()->getGSMState(); }

void MachineBase::setGSMState(CallState state) {
	LOG(INFO) << "SIP term info setGSMState state: " << state; // SVGDBG
	tran()->setGSMState(state);
}

SIP::SipDialog * MachineBase::getDialog() const { return tran()->getDialog(); }
void MachineBase::setDialog(SIP::SipDialog*dialog) { return tran()->setDialog(dialog); }

unsigned MachineBase::getL3TI() const { return mTran->getL3TI(); }

bool MachineBase::isL3TIValid() const {
	return getL3TI() != TranEntry::cL3TIInvalid;
}


// TODO: Current this twiddles the RRLP status flags;
// the whole RRLP server needs to turn into some kind of state machine so we can tell what is going on,
// and whether a status message might be for RRLP or something else.
static void handleStatus(const L3Message *l3msg, MMContext *mmchan)
{
	assert(l3msg->MTI() ==  L3RRMessage::RRStatus);

	const L3RRStatus *statusMsg = dynamic_cast<typeof(statusMsg)>(l3msg);
	if (!statusMsg) {
		LOG(ERR) << "Could not cast Layer 3 RR Status message?";
		return;
	}
	int cause = statusMsg->cause().causeValue();

	switch (cause) {
		case 97:
			LOG(INFO) << "Received RR status message with cause: message not implemented";
			// (pat) TODO: Figure out if this is in response to RRLP or not...
			//Rejection code only useful if we're gathering IMEIs
			if (gConfig.getBool("Control.LUR.QueryIMEI")){
				// flag unsupported in SR so we don't waste time on it again
				string imsi = mmchan->mmGetImsi(false);		// with false flag, empty if no imsi
				if (imsi.size() >= 14 && isdigit(imsi[0])) {	// Cheap partial validation
					imsi = string("IMSI") + imsi;
					// TODO : disabled because of upcoming RRLP changes and need to rip out direct SR access
					//if (gSubscriberRegistry.imsiSet(imsi, "RRLPSupported", "0")) {
					//	LOG(INFO) << "SR update problem";
					//}
				}
			}
			return;
		case 98:
			LOG(INFO) << "Received RR status message with cause: message type not compatible with protocol state";
			return;
		default:
			LOG(INFO) << "Received RR Status with"<<LOGHEX(cause);
			return;
	}
}

// code copied from DCCHDispatchMessage() and descendents.
// Return TRUE if handled.
// 3GPP 4.08 4.5.1.1 says "Upon receiving a CM service request" the network may start any common MM or RR procedure including
// classmark interrogation, identification, authentication and cypering.
static bool handleCommonMessages(const L3Message *l3msg, MMContext *mmchan, bool *deletemsg)
{
	*deletemsg = true;
	LOG(INFO) << "USSD " << L3CASE_RAW(l3msg->PD(),l3msg->MTI());

	switch (L3CASE_RAW(l3msg->PD(),l3msg->MTI())) {
		case L3CASE_RR(L3RRMessage::PagingResponse):
			NewPagingResponseHandler(dynamic_cast<const L3PagingResponse*>(l3msg),mmchan);
			return true;
		case L3CASE_RR(L3RRMessage::ApplicationInformation):
			return true;
		case L3CASE_RR(L3RRMessage::RRStatus):
			handleStatus(l3msg,mmchan);
			return true;
		case L3CASE_MM(L3MMMessage::LocationUpdatingRequest):
			//LocationUpdatingController(dynamic_cast<const L3LocationUpdatingRequest*>(req),mmchan);
			//tran->lockAndStart(new L3ProcedureLocationUpdate(tran), (L3Message*)req);
			// TODO: Should we check that this an appropriate time to start it?
			LURInit(l3msg,mmchan);
			return true;
		case L3CASE_MM(L3MMMessage::IMSIDetachIndication): {
			// (pat) TODO, but it is not very important.
			L3MobileIdentity mobid = dynamic_cast<const L3IMSIDetachIndication*>(l3msg)->mobileID();
			imsiDetach(mobid,mmchan->tsChannel());
			mmchan->tsChannel()->chanRelease(L3_RELEASE_REQUEST,TermCause::Local(L3Cause::IMSI_Detached));
			return true;
			}
		case L3CASE_MM(L3MMMessage::CMServiceRequest):
			mmchan->mmcServiceRequests.write(l3msg);
			//NewCMServiceResponder(dynamic_cast<const L3CMServiceRequest*>(l3msg),dcch);
			*deletemsg = false;
			return true;
		default:
			break;
	}
	return false;
}

MachineStatus MachineBase::machineRunState(int /*state*/, const GSM::L3Message * /*l3msg*/, const SIP::DialogMessage * /*sipmsg*/)
{
	// The state machine must implement one of: machineRunState, machineRunL3Msg or machineRunFrame.
	assert(0);
}

#if UNUSED
MachineStatus MachineBase::machineRunL3Msg(int state, const GSM::L3Message *l3msg)
{
	bool deleteit;
	if (handleCommonMessages(l3msg, channel(),&deleteit)) {
		LOG(DEBUG) << "message handled by handleCommonMessages"<<LOGVAR(l3msg);
		if (deleteit) delete l3msg;
		return MachineStatusOK;
	}
	//int state = L3CASE_RAW(l3msg->PD(),l3msg->MTI());
	LOG(DEBUG) <<"calling machineRunState "<<LOGHEX(state)<<machText()<<LOGVAR(l3msg);
	return machineRunState(state,l3msg,NULL);
}
#endif

MachineStatus handlePrimitive(const L3Frame *frame, L3LogicalChannel *lch)
{
	switch (frame->primitive()) {
	case L3_ESTABLISH_CONFIRM:
	case L3_ESTABLISH_INDICATION:
		// Indicates SABM mode establishment.  The state machines that use machineRunState can ignore these.
		// The transaction is started by an L3 message like CMServiceRequest.
		return MachineStatusOK;
	default:
		// We took all the other primitives out of the message stream in csl3HandleFrame
		assert(0);
	//case GSM::HANDOVER_ACCESS:
	//	// TODO: test that this is on TCHFACH not SDCCH.
	//	assert(0);	// Someone higher handled this.
	//	//ProcessHandoverAccess(lch);
	//	return MachineStatusQuitChannel;
	//
	//case DATA:
	//case UNIT_DATA:
	//	assert(0);	// Caller checked this.
	//
	//case ERROR:			///< channel error
	//	// The LAPDM controller was aborted.
	//	lch->chanRelease(RELEASE); 		// Kill off all the transactions associated with this channel.
	//	return MachineStatusQuitChannel;
	//
	//case HARDRELEASE:
	//	if (frame->getSAPI() == 0) { lch->chanRelease(HARDRELEASE); } 		// Release the channel.
	//	return MachineStatusQuitChannel;
	//default:
	//	LOG(ERR) <<lch<<"unhandled primitive: " << frame->primitive();	// This is horrible.  But lets warn instead of crashing.
	//	// Fall through
	//case RELEASE:		///< normal channel release
	//	if (frame->getSAPI() == 0) { lch->chanRelease(RELEASE); }
	//	return MachineStatusQuitChannel;
	}
}

// The state machines may receive messages via machineRunState or machineRunState1.
// This is the extended version that lets the state machine handle the primitives, unparseable messages,
// and includes the frame, too, if it is available.
// Generally either frame or l3msg or sipmsg will be non-NULL.
// The l3msg will be provided instead of the frame if we were invoked by lockAndStart with just an l3msg,
// but the state machine should know that.
// l3msg may be NULL for primitives or unparseable messages.
MachineStatus MachineBase::machineRunState1(int state, const L3Frame *frame, const L3Message *l3msg, const SIP::DialogMessage *sipmsg)
{
	LOG(DEBUG)<<LOGHEX(state)<<LOGVAR(frame)<<LOGVAR(l3msg)<<LOGVAR(sipmsg);
	if (frame) {
		// Handle the primitives so the state machine does not have to.
		if (!frame->isData()) { return handlePrimitive(frame,channel()); }
	}
	if (l3msg || sipmsg || state < 256) {
		LOG(DEBUG) <<"calling machineRunState "<<LOGHEX(state)<<machText()<<LOGVAR(l3msg);
		return machineRunState(state,l3msg,sipmsg);
	}
	return MachineStatusOK;	// Ignore unparseable messages.
}

// l3msg may be NULL for primitives or unparseable messages.
MachineStatus MachineBase::dispatchFrame(const L3Frame *frame, const L3Message *l3msg)
{
	int state;
	if (frame) {
		if (frame->isData()) {
			// Note: the frame MTI must be ANDed with 0xbf.  See 4.08 10.4.  I moved that into class L3Frame.
			state = L3CASE_RAW(frame->PD(),frame->MTI());
		} else {
			state = L3CASE_PRIMITIVE(frame->primitive());
		}
	} else if (l3msg) {
		state = L3CASE_RAW(l3msg->PD(),l3msg->MTI());
	} else {
		LOG(ERR) << "called with no arguments?";
		return MachineStatusOK;
	}
	return machineRunState1(state,frame,l3msg);
#if 0
	if (frame->isData()) {
		if (L3Message *msg = parseL3(*frame)) {
			LOG(DEBUG) <<channel() <<" received L3 message "<<*msg;
			// Manufacture an integral state from the msg.
			// Note we overload pd==0 (Group Call Control) for naked states in the state machines, which is ok because
			// we dont support Group Call and even if we did, the Group Call MTIs (GSM 04.68 9.3) will probably not collide
			// because there are no small numberic MTIs.
			int state = L3CASE_RAW(frame->PD(),frame->MTI());
			LOG(DEBUG) <<"calling machineRunState1 "<<LOGHEX(state)<<machText()<<LOGVAR(frame);
			//MachineStatus result = machineRunL3Msg(state, msg);
			MachineStatus result = machineRunState1(state, frame, msg, NULL);
			delete msg;
			return result;
		} else {
			LOG(ERR) <<channel()<< " received unparseable Layer3 frame "<<*frame;
			//old: lch->chanGetContext(true)->mmDispatchError(PD,MTI,lch);
			return MachineStatusOK;	// Was not handled, but the caller cant do anything more with this frame.
		}
	} else {
		Primitive primitive = frame->primitive();
		int state = L3CASE_PRIMITIVE(primitive);
		LOG(DEBUG) <<"calling machineRunState1 "<<LOGHEX(state)<<machText()<<LOGVAR(primitive);
		return machineRunState1(state);
	}
#endif
}

// The procedure is pre-locked by the caller.
MachineStatus MachineBase::dispatchTimeout(L3Timer*timer)
{
	// A timer with no explicit next state specified just kills off the Transaction.
	int nextState = timer->tNextState();
	LOG(DEBUG) <<LOGVAR(nextState)<<machText() <<this;
	if (nextState >= 0) {
		return machineRunState1(nextState);
	} else if (nextState == TimerAbortTran) {
		// TODO: How do we kill it?
		// Answer: It depends on the procedure.  The caller should have done any specific
		// closing, for example CC message, before exiting the state machine.
		LOG(INFO) << "Timer "<<timer->tName()<<" timeout";
		// (pat) TODO: We should not use one error fits all here; the error should be set up when the timer was established.
		TermCause cause = TermCause::Local(L3Cause::No_User_Responding); // SVG 5/20/14 changed this from InterworkingUnspecified to NoUserResponding

		// If it is an SMS transaction, just drop it.
		// If it is a CC transaction, lets send a message to try to kill it, which may block for 30 seconds.
		switch (tran()->servicetype()) {
		case L3CMServiceType::MobileOriginatedCall:
		case L3CMServiceType::MobileTerminatedCall: {
			LOG(INFO) << "SIP term info dispatchTimeout call teCloseCallNow  servicetype: " << tran()->servicetype(); // SVGDBG
			tran()->teCloseCallNow(cause,true);
		}
		default:
			break;
		}

		// Code causes caller to kill the transaction, which will also cancel the dialog if any, which is
		// partly redundant with the teCloseCall above..
		return MachineStatus::QuitTran(cause);
	} else if (nextState == TimerAbortChan) {
		LOG(INFO) << "Timer "<<timer->tName()<<" timeout";
		// This indirectly causes immediate destruction of all transactions on this channel.
		LOG(INFO) << "SIP term info closeChannel called in dispatchTimeout";
		// TODO: Error should be set up when timer started.
		return closeChannel(L3RRCause::Timer_Expired,L3_RELEASE_REQUEST,TermCause::Local(L3Cause::No_User_Responding));
	} else {
		assert(0);
		return MachineStatus::QuitTran(TermCause::Local(L3Cause::L3_Internal_Error));
	}
}


MachineStatus MachineBase::dispatchSipDialogMsg(const SIP::DialogMessage *dialogmsg)
{
	return machineRunState1(L3CASE_DIALOG_STATE(dialogmsg->dialogState()),(L3Frame*)NULL,(L3Message*)NULL,dialogmsg);
}


MachineStatus MachineBase::dispatchL3Msg(const L3Message *l3msg)
{
	int state = L3CASE_RAW(l3msg->PD(),l3msg->MTI());
	LOG(DEBUG) <<"calling machineRunState1 "<<LOGHEX(state)<<machText()<<LOGVAR(l3msg);
	// We pass a NULL frame, but any state machine that gets messages via this function will know that.
	return machineRunState1(state,(L3Frame*)NULL,l3msg);
}


#if UNUSED
bool CSL3StateMachine::csl3Write(GenericL3Msg *msg)
{
	if (1) { //l3rewrite())
		LOG(DEBUG) << "Received "<<msg->typeName()<<" message for l3rewrite";
		mCSL3Fifo.write(msg);
		return true;			// Our code is going to deal with this message.
	} else {
		LOG(DEBUG) << "Received "<<msg->typeName()<<" message, handling with version 1 code";
		// Caller must deal with this message with pre-existing version 1 code.
		return false;
	}
}
#endif


#if UNUSED	// now it is
// TODO: This might as well be in MMContext.
static void csl3HandleLCHMsg(GSM::L3Message *l3msg, L3LogicalChannel *lch)
{
	// Do we have one or more transactions already running on this logical channel?
	// There can be multiple transactions on the same channel.
	// What is to prevent an MS from allocating multiple channels, eg, a TCH and SDCCH simultaneously?
	// I think previously nothing.   But we are not going to try to aggregate messages from multiple channels in the same MS together.
	// We assume that all the messages for a single Machine arrive on a single channel+SACCH pair.
	if (handleCommonMessages(l3msg, lch)) {
		LOG(DEBUG) << "message handled by handleCommonMessages"<<LOGVAR(l3msg);
		delete l3msg;
		return;
	}

	bool handled = lch->chanGetContext(true)->mmDispatchL3Msg(l3msg,lch);

	//TranEntry *tran = gNewTransactionTable.ttFindByL3Msg(l3msg,lch);
	//bool handled = false;
	//if (tran) {
	//	OBJLOG(DEBUG) <<"received l3msg for dcch:"<<*lch<<" l3msg="<<*l3msg<<LOGVAR(tran->text());
	//	// TODO: Do we ever need a way for the Procedure to tell us that a message was completely handled
	//	// and that we should not examine it further?
	//	if (tran->lockAndInvokeL3Msg(l3msg,lch)) handled++;
	//}

	if (!handled) {
		LOG(DEBUG) <<"unhandled l3msg" <<LOGVAR(l3msg) <<lch->chanGetContext(false);
	}
	delete l3msg;
}
#endif

// Return true if it was an l3 message or a primitive that we pass on to state machines, false otherwise.
// After calling us the caller should test chanRunning to see if the channel is still up.
static bool checkPrimitive(Primitive prim, L3LogicalChannel *lch, int sapi)
{
	LOG(DEBUG)<<lch<<LOGVAR(prim);
	// Process 'naked' primitives.
	switch (prim) {
	case L3_ESTABLISH_CONFIRM:
	case L3_ESTABLISH_INDICATION:
		// Indicates SABM mode establishment.
		// Most state machines can ignore these, but the MT-SMS controller has to wait for channel
		// establishment so we will pass it on.
		// One of these comes in when the channel is inited.
		// Pat took out this gReports temporarily because it is delaying channel establishment.
		// gReports.incr("OpenBTS.GSM.RR.ChannelSeized");
		return true;
	case HANDOVER_ACCESS:
		LOG(ALERT) << "Received HANDOVER_ACCESS on established channel";
		// TODO: test that this is on TCHFACH not SDCCH.
		// This does not return until the channel is ready to start running a state machine.
		//ProcessHandoverAccess(lch);
		return false;

	case L3_DATA:
	case L3_UNIT_DATA:
		return true;

	case MDL_ERROR_INDICATION:			///< channel error
		// The LAPDM controller was aborted.
		//gNewTransactionTable.ttLostChannel(lch);
		//lch->chanLost(); 		// Kill off all the transactions associated with this channel.
		LOG(ERR) << "Layer3 received ERROR from layer2 on channel "<<lch<<LOGVAR(sapi);

		// FIXME: This prim needs to be passed to the state machines to abort procedures.

		lch->chanRelease(L3_RELEASE_REQUEST,TermCause::Local(L3Cause::Layer2_Error)); 		// Kill off all the transactions associated with this channel.
		return false;

	//case HARDRELEASE:		///< forced release after an assignment
	//	if (sapi == 0) lch->chanRelease(L3_HARDRELEASE_REQUEST); 		// Release the channel.
	//	return false;
	case L3_RELEASE_INDICATION:		///< normal channel release
		//if (lch->mChState == L3LogicalChannel::chReassignPending || lch->mChState == L3LogicalChannel::chReassignComplete) {
		//	// This is what we wanted.
		//	// We will exit and the service loop will change the L3LogicahChannel mChState.
		//} else {
		//	// This is a bad thing when happening on an active channel.
		//	// Link either closed by peer or lost due to timeout in a lower layer.
		//	// The LAPDm is already released so we cannot send any more messages.
		//	// Just drop the channel.
		//	lch->chanLost();		// Kill off all the transactions associated with this channel.
		//}

		// FIXME: This prim needs to be passed to the state machines to abort procedures.

		if (sapi == 0) lch->chanRelease(L3_RELEASE_REQUEST,TermCause::Local(L3Cause::Normal_Call_Clearing));
		return false;
	default:
		LOG(ERR) <<lch<<"unhandled primitive: " << prim;		// oops!  But lets warn instead of crashing.
		// Something horrible happened.
		lch->chanRelease(L3_RELEASE_REQUEST,TermCause::Local(L3Cause::L3_Internal_Error)); 		// Kill off all the transactions associated with this channel.
		//lch->freeContext();
		return false;
	}
}

// Dont delete the frame; caller does that.
static void csl3HandleFrame(const GSM::L3Frame *frame, L3LogicalChannel *lch)
{
	L3Message *l3msg = NULL;
	// The primitives apply directly to the L3LogicalChannel and a specific SAPI, rather than to the MMContext.
	if (! checkPrimitive(frame->primitive(), lch, frame->getSAPI())) { return; }
	// Everything else runs on the MMContext.
	MMContext *mmchan = lch->chanGetContext(true);
	if (frame->isData()) {
		l3msg = parseL3(*frame);
		if (l3msg) {
			WATCHINFO(lch <<" received L3 message "<<*l3msg);
			//print msg to console
			LOG(INFO) << "USSD RX: " << *l3msg;
		} else {
			LOG(ERR) <<lch<< " received unparseable Layer3 frame "<<*frame;
			// We pass unparseable messages through to machineRunState1 to provide an error indication to
			// any state machine that uses that function, but the default machrineRunState1 eliminates them
			// so machineRunState never sees them.
		}
		bool deleteit;
		if (l3msg && handleCommonMessages(l3msg, mmchan, &deleteit)) {
			LOG(DEBUG) << "message handled by handleCommonMessages"<<LOGVAR(l3msg);
			if (deleteit) { delete l3msg; }
			return;
		}
	}
	mmchan->mmDispatchL3Frame(frame,l3msg);
	if (l3msg) delete l3msg;
}

#if UNUSED
// Handle timeouts.  Return the next timeout or -1 if no timeout is pending.
int CSL3StateMachine::csl3HandleTimers()
{
	ScopedLock lock(gNewTransactionTable.mLock,__FILE__,__LINE__);
	int nextTimeout = -1;
	for (NewTransactionMap::iterator itr = gNewTransactionTable.mTable.begin(); itr!=gNewTransactionTable.mTable.end(); ++itr) {
		TranEntry *tran = itr->second;
		if (tran->deadOrRemoved()) continue;
		tran->checkTimers();
	}
	for (NewTransactionMap::iterator itr = gNewTransactionTable.mTable.begin(); itr!=gNewTransactionTable.mTable.end(); ++itr) {
		TranEntry *tran = itr->second;
		int remaining = tran->remainingTime();
		if (remaining >= 0) {
			nextTimeout = (nextTimeout == -1) ? remaining : min(nextTimeout,remaining);
		}
		LOG(DEBUG)<<"transaction "<<tran->tranID()<<LOGVAR(nextTimeout);
	}
	return nextTimeout;
}
#endif

#if UNUSED
// Under GSM this could block as long as a downlink LAPDm message can block.
void CSL3StateMachine::csl3HandleSipMsg(SIP::DialogMessage *sipmsg)
{
	bool found = false;
	TranEntry *tran = gNewTransactionTable.ttFindById(sipmsg->mTranId);
	if (tran && ! tran->deadOrRemoved()) {
		found = true;
		LOG(DEBUG) << "sip message code="<<sipmsg->mSIPStatusCode <<" handled by trans "<<tran->tranID();
		// Call deletes it
		tran->lockAndInvokeSipMsg(sipmsg);
	}
#if 0
	ScopedLock lock(gNewTransactionTable.mLock,__FILE__,__LINE__);
	int code = sipmsg->sipStatusCode();
	LOG(DEBUG) << "Received Dialog message "<<LOGVAR(callid)<<LOGVAR(sipmsg);
	// TODO: We should have a back pointer from sip so we dont have to search for this.  It might go in the as yet non-existent mobility management layer.
	for (TransactionMap::iterator itr = gNewTransactionTable.mTable.begin(); itr!=gNewTransactionTable.mTable.end(); ++itr) {
		TranEntry *tran = itr->second;
		if (tran->deadOrRemoved()) continue;
		if (tran->SIPCallID() == callid) {
			found = true;
			LOG(DEBUG) << "sip message code="<<code <<" handled by trans "<<tran->tranID();
			tran->lockAndInvokeSipMsg(sipmsg);
		}
	}
#endif
	if (!found) { LOG(DEBUG) << "sip message code="<<sipmsg->mSIPStatusCode <<" no matching transaction found."; }
}
#endif

#if UNUSED
void CSL3StateMachine::csl3HandleMsg(GenericL3Msg *gmsg)
{
	switch (gmsg->ml3Type) {
	case GenericL3Msg::MsgTypeLCH:
		csl3HandleFrame(gmsg->ml3frame,gmsg->ml3ch);
		break;
	case GenericL3Msg::MsgTypeSIP:
		csl3HandleSipMsg(gmsg->mSipMsg /*, gmsg->mCallId*/);
		break;
	default: assert(0);
	}
	delete gmsg;		// Deletes the L3Frame in the GenericL3Msg as well.
}
#endif

/**
	Update vocoder data transfers in both directions.
	@param transaction The transaction object for this call.
	@param TCH The traffic channel for this call.
	@return bytes transferred.
*/
static unsigned newUpdateCallTraffic(TranEntry *transaction, GSM::TCHFACCHLogicalChannel *TCH)
{
	// We dont set the state Active until both SIP dialog and MS side have acked.
	LOG(DEBUG);
	if (transaction->getGSMState() != CCState::Active) { return false; }
	unsigned activity = 0;

	// Both the rx and tx directions block if rtp_session_set_blocking_mode() is true.

	// Transfer in the uplink direction (GSM->RTP).
	// Flush FIFO to limit latency.
	unsigned numFlushed = 0;
	{
		unsigned maxQ = gConfig.getNum("GSM.MaxSpeechLatency");
		static Timeval testTimeStart;
		while (TCH->queueSize()>maxQ) {
			if (numFlushed == 0) testTimeStart.now();
			numFlushed++;
			// (pat) LOG is too slow to call in here.  With 1000 backed up the delay is 1/2 sec.
			//LOG(DEBUG) <<TCH <<" ulFrame flushed"<<LOGVAR(TCH->queueSize());
			delete TCH->recvTCH();
		}
		if (numFlushed) { LOG(DEBUG) <<TCH <<" ulFrame flushed "<<numFlushed <<" in "<<testTimeStart.elapsed() << " msecs"; }
	}


	if (SIP::AudioFrame *ulFrame = TCH->recvTCH()) {
		activity += ulFrame->sizeBytes();
		// Send on RTP.
		LOG(DEBUG) <<TCH <<LOGVAR(*ulFrame);
		transaction->txFrame(ulFrame,numFlushed);
		delete ulFrame;
	}

	// Transfer in the downlink direction (RTP->GSM).
	// Blocking call.  On average returns 1 time per 20 ms.
	// Returns non-zero if anything really happened.
	// Make the rxFrame buffer big enough for G.711.
	if (SIP::AudioFrame *dlFrame = transaction->rxFrame()) {
		activity += dlFrame->sizeBytes();
		if (activity == 0) { activity++; }	// Make sure we signal activity.
		LOG(DEBUG) <<TCH <<LOGVAR(*dlFrame);
		TCH->sendTCH(dlFrame);
	}

	// Return a flag so the caller will know if anything transferred.
	LOG(DEBUG) <<LOGVAR(activity);
	return activity;
}

// Check for any message or timer activity on this channel.  Return true if something happened.
// The delay is how often we poll, used on SDCCH.
static bool checkemMessages(L3LogicalChannel *dcch, int delay)
{
	LOG(DEBUG) <<LOGVAR(dcch) <<LOGVAR(delay);
	// (pat) For l3Rewrite: there are two contending solutions for the uplink message path:
	// They can be sent in a common queue to the global L3 message handler (similar to UMTS)
	// or be handled by this dedicated channel thread called from DCCHDispatcher.
	// This is the code for the latter.
	// (pat) Can messages on FACCH be for transactions other than the current one?  Not sure, but
	// be safe and handle the message with the generic L3 handler instead of dispatching it directly to this transaction.

	MMContext *set = dcch->chanGetContext(true);

	if (set->mmcTerminationRequested) {
		set->mmcTerminationRequested = false; // Reset the flag superstitiously.
		dcch->chanClose(L3RRCause::Preemptive_Release,L3_RELEASE_REQUEST,TermCause::Local(L3Cause::Operator_Intervention));
		return true;
	}

	// All messages from all host chan saps and from sacch now come in l2recv now.
	if (GSM::L3Frame *l3frame = dcch->l2recv(delay)) {
		LOG(DEBUG) <<dcch<<" "<< *l3frame;
		csl3HandleFrame(l3frame, dcch);
		delete l3frame;
		return true;	// Go see if it terminated the TranEntry while we were potentially blocked.
	}

#if 0
	// // How about SAPI 3?
	// if (GSM::L3Frame *l3frame = dcch->l2recv(0,3)) {
	// 	LOG(DEBUG) <<dcch<< *l3frame;
	// 	csl3HandleFrame(l3frame, dcch);
	// 	delete l3frame;
	// 	return true;	// Go see if it terminated the TranEntry while we were potentially blocked.
	// }

	// // How about SACCH?  These messages are supposed to be prioritized, but we're not bothering.
	// // We need to pass the ESTABLISH primitive to higher layer, specifically, MTSMSMachine.
	// if (L3Frame *aframe = dcch->ml3UplinkQ.readNoBlock()) {
	// 	//if (IS_LOG_LEVEL(DEBUG)) {
	// 		//std::ostringstream os; os << *aframe;
	// 		//WATCHF("Frame on SACCH %s: %s\n",dcch->descriptiveString(),os.str().c_str());
	// 	//}
	// 	WATCH("Recv frame on SACCH "<<dcch->descriptiveString()<<" "<<*aframe);
	// 	csl3HandleFrame(aframe, dcch);
	// 	delete aframe;
	// 	return true;	// Go see if it terminated the TranEntry while we were potentially blocked.
	// }
#endif

	// Any Dialog messages from the SIP side?
	// Sadly we cannot process the sip messages in a separate global L3 thread because the TranEntry/procedure may be
	// locked for up to 30 seconds when sending to LAPDm, which would then stall that L3 thread.
	if (set->mmCheckSipMsgs()) {
		LOG(DEBUG) <<dcch<< " Sip message processed";
		return true;	// Must go see if transaction terminated as a result of dialog message.
	}

	// Check all timers in all transactions on this channel.
	if (set->mmCheckTimers()) {
		LOG(DEBUG) <<dcch<< " timer processed";
		return true;	// Must go see if transaction terminated as a result of timer.
	}

	// Note: This initiates the channel release if all transactions are finished.
	if (set->mmCheckNewActivity()) {
		LOG(DEBUG) <<dcch<< " new activity finished";
		return true;
	}
	LOG(DEBUG) <<dcch<<" returning, nothing to do";

	return false;	// No messages found.
}


// Handle TCH traffic during a call.  This is called from the dedicated thread created for a combination I full rate TCH in GSMConfig.
// It is called from DCCHDispatch.
// Prior to L3rewrite TCH traffic and messages were handled by callManagementLoop(), which called pollInCall(), updateGSMSignalling()
static void l3CallTrafficLoop(L3LogicalChannel *dcch)
{
	assert(dcch->chtype() == FACCHType);
	GSM::TCHFACCHLogicalChannel *tch = dynamic_cast<typeof(tch)>(dcch);
	// The gReports call invokes sqlite3 which takes tenths of seconds, which is too long
	// during a voice call, and especially a handover, so only call gReports if nothing else is pending.
	bool needReports = true;
	int nextDelay = 0;

	//LOG(INFO) << "call connected "<<*tran;
	size_t fCount = 0; // A rough count of frames.
	// chanRunning checks for loss in L3.  radioFailure checks for loss in L2.
	// Original code used throw...catch but during channel reassignment the channel state is terminated by changing
	// the state by a different thread, so we just the same method for all cases and terminate by changing the channel state.
	unsigned alternate = 0;
	while (dcch->chanRunning() && !gBTS.btsShutdown()) {
		if (tch->radioFailure()) {
			LOG(NOTICE) << "radio link failure, dropped call"<<LOGVAR(dcch);
			//gNewTransactionTable.ttLostChannel(dcch);
			// The radioFailure already waited for the timeout, so now we can immediately drop the channel.
			dcch->chanRelease(L3_HARDRELEASE_REQUEST,TermCause::Local(L3Cause::Radio_Interface_Failure)); 	// Kill off all the transactions associated with this channel.
			//tran->getDialog()->dialogCancel(TermCause::TermCodeUnknown, GSM::L3Cause::Unknown_L3_Cause); // was forceSIPClearing
			//tran->teRemove();
			return;
		}
		// The voiceTrans will not yet be set when this loop starts.
		// The MS establishes SABM over LAPDm which sends up an ESTABLISH primitive,
		// then we get the AssignmentComplete L3 message over this FACCH channel,
		// which calls the controling state machine to set voiceTrans.
		RefCntPointer<TranEntry> rtran = dcch->chanGetVoiceTran();
		TranEntry *tran = rtran.self();
		LOG(DEBUG) <<dcch <<" main loop"<<LOGVAR(nextDelay)<<" found tran:"<<(tran!=NULL);	// warning: tran may be NULL.
		if (tran != NULL) {
			if (tran->deadOrRemoved()) {
				// This is ok.  If there is something else ongoing (eg SMS) when the voice call ends
				// we should keep the channel open until that ends.
				LOG(NOTICE) << "attempting to use a defunct Transaction"<<LOGVAR(dcch)<<LOGVAR(*tran);
				// TODO: We should not be closing the channel here; we whould wait 
				dcch->chanClose(L3RRCause::Preemptive_Release,L3_RELEASE_REQUEST,TermCause::Local(L3Cause::No_Transaction_Expected));
				return;
			}
			// TODO: This needs to check all the transactions in the MMContext.
			// The termination request comes from the CLI or from RadioResource to make room for an SOS call.
			L3Cause::AnyCause termcause = tran->terminationRequested();
			if (termcause.value != 0) {
				LOG(DEBUG)<<dcch<<" terminationRequested";
				tran->terminateHook();		// Gives the L3Procedure state machine a chance to do something first.
				// GSM 4.08 3.4.13.4.1: Use RR Cause PreemptiveRelease if terminated for a higher priority, ie, emergency, call 
				dcch->chanClose(L3RRCause::Preemptive_Release,L3_RELEASE_REQUEST,TermCause::Local(termcause));
				return;		// We wont be back.
			}
			if (tran->getGSMState() == CCState::HandoverOutbound) {
				if (outboundHandoverTransfer(tran,dcch)) {
					LOG(DEBUG)<<dcch<<" outboundHandover";
					dcch->chanRelease(L3_HARDRELEASE_REQUEST,TermCause::Local(L3Cause::Handover_Outbound));
					return;	// We wont be back.
				}
			}

			// Any Q.931 timer expired?
			// TODO: Remove this, redundant with the below.
			if (tran->checkTimers()) {	// This calls a handler function and resets the timer.
				LOG(DEBUG) <<dcch <<" after checkTimers";
				continue;	// The transaction is probably defunct.
			}
		}

		// Look for messages or timers.
		// (pat 8-6-2013)  We have an architectural problem that we do not have a separate thread handling SAPI3
		// so the in-call SMS handler hangs for several seconds (in the best case.)  The rxFrame below also hangs.
		// In order not to wait too long to service of these two things, I am adding a super-hack to alternate servicing them.
		// I think we need to add another thread to run SAPI3.
		if (++alternate % 2) {
			if (checkemMessages(dcch,nextDelay)) { gResetWatchdog(); nextDelay = 0; continue; }
		}
		nextDelay = 0;
		LOG(DEBUG) <<dcch <<" after checkem Messages";

		// Finally, get down to business: transfer vocoder data.
		// This is a blocking call, blocking 20ms on average.
		if (tran != NULL) {
			static unsigned bytes = 0;
			if (unsigned theseBytes = newUpdateCallTraffic(tran,tch)) {
				bytes += theseBytes;
				// Print one of the log messages just once per second.
				//static time_t lasttime = 0;
				//time_t now = time(NULL);
				//if (now != lasttime) { LOG(DEBUG) "call traffic" <<LOGVAR(bytes) <<LOGVAR(dcch) <<LOGVAR(tran); }
				LOG(DEBUG) <<dcch <<" call traffic" <<LOGVAR(bytes) <<LOGVAR(tran);
				//lasttime = now;
				// If anything happened, then the call is still up.
				fCount++;
				// On average, each one takes 20ms, so 50 is a msec, and 60*50 is one second.
				if ((fCount%(60*50)) == 0) {
					LOG(DEBUG) <<dcch <<" reset watchdog";
					gResetWatchdog();
					LOG(DEBUG) <<dcch <<" after reset watchdog";
					needReports = true;
				}
				continue;
			}
		}

		// (pat) When we are using blocking mode in the RTP library, we never get here.
		if (needReports) {
			LOG(DEBUG) <<dcch << "calling gReports CC.CallMinutes";
			gReports.incr("OpenBTS.GSM.CC.CallMinutes");
			LOG(DEBUG) <<dcch << " after gReports CC.CallMinutes";
			needReports = false;
			continue;
		}
		LOG(DEBUG) <<dcch <<" end of loop";

		// If nothing happened, set nextDelay so so we dont burn up the CPU cycles.
		nextDelay = 20;		// Do not exceed the RTP frame size of 20ms.
	}
	LOG(DEBUG) << "final return";
}

// TODO: When a channel is first opened we should save the CMServiceRequest or LocationUpdateRequest or PagingResponse and initiate
// an identification and optional authorization procedure on the channel itself.  That is true for both GSM and UMTS.
// Once we have an IMSI, we can create TranEntrys for each procedure that is MO, and also move each
// each MTC or MT-SMS TranEntry onto this channel.
// If there was a race between MTC and MOC it needs to be resolved at the same time, although that may turn out to be a no-op, ie,
// resolved by whether the MS sent CMServiceRequest or PagingResponse first.  If the former, the MT calls need to turn into call-waiting things.
// Note MS could send multiple simultaneous MO CMServiceRequests - one for CS and one for SMS.
// FOR NOW:
// Just have a primary transaction on the channel, which is known from the GSMState aka CallState.
static void L3SDCCHLoop(L3LogicalChannel*dcch)
{
	assert(dcch->chtype() == SDCCHType);
	while (dcch->chanRunning() && !gBTS.btsShutdown()) {
		if (dcch->radioFailure()) {	// Checks expiry of T3109, set at 30s.
			LOG(NOTICE) << "radio link failure, dropped call";
			//gNewTransactionTable.ttLostChannel(dcch);
			// (pat) 5-2014: Changed to RELEASE from HARDRELEASE - even though we can no longer hear the handset,
			// it might still hear us so we have to deactivate SACCH and wait T3109.
			dcch->chanRelease(L3_RELEASE_REQUEST,TermCause::Local(L3Cause::Radio_Interface_Failure)); 	// Kill off all the transactions associated with this channel.
			return;
		}

		// Any L3 Messages from the MS side on this channel?
		// Wait 100ms.  I made this number up out of thin air.
		// Since an incoming L3 message will be returned instantly, this delay is the effective delay
		// in handling SIP messages or timers, and could be much longer since none of those are precision timers.
		if (checkemMessages(dcch,100)) { gResetWatchdog(); continue; }
	}
}

// dcch may be SDCCH or FACCH.
// This does not return until the channel is released.
void L3DCCHLoop(L3LogicalChannel*dcch, L3Frame *frame)
{
	LOG(INFO) <<"DCCH LOOP OPEN "<<dcch;
	try {
		// We must not reset the channel state when opened because during a channel reassignment the new channel
		// already has an attached MMContext.
		assert(frame);
		Primitive prim = frame->primitive();
		delete frame;

		dcch->chanSetState(L3LogicalChannel::chEstablished);
		switch (prim) {
			case L3_ESTABLISH_INDICATION:
				break;
			case HANDOVER_ACCESS:
				ProcessHandoverAccess(dcch);
				// If the handover fails, it sets the chState such that the loop below will return immediately,
				// so we can just break here.
				break;
			default:
				assert(0);	// Caller prevented anything else.
		}

		switch (dcch->chtype()) {
		case SDCCHType:
			L3SDCCHLoop(dcch);
			break;
		case FACCHType:
			l3CallTrafficLoop(dcch);
			break;
		default:
			assert(0);
		}
		devassert(dcch->mChState != L3LogicalChannel::chIdle); // This would be a bug.
	} catch (exception &e) {
		LOG(ERR) << "exception "<<e.what() << " " << typeid(&e).name();
	} catch (...) {
		LOG(ERR) << "unrecognized exception, channel reset";
	}
	WATCHINFO("DCCH LOOP EXIT " << dcch);

	// Always reset, even though the MMContext is shared between L3LogicalChannels during channel reassignment;
	// we now have a refcnt in the MMContext so this is bullet proof destruction.
	dcch->L3LogicalChannelReset();
	switch (dcch->mChState) {
		case L3LogicalChannel::chRequestRelease:
			// The RELEASE primitive will block up to 30 seconds, so we NEVER EVER send it from anywhere but right here.
			// To release the channel, set the channel state to chReleaseRequest and let it come here to release the channel.
			// FIXME: Actually, LAPDm blocks in this forever until it gets the next ESTABLISH, so this is where the serviceloop really waits.
			dcch->l3sendp(L3_RELEASE_REQUEST);	// WARNING!  This must be the only place in L3 that sends this primitive.
			break;
		case L3LogicalChannel::chRequestHardRelease:
			dcch->l3sendp(L3_HARDRELEASE_REQUEST);
			break;
		default: break;
	}
	LOG(DEBUG) <<"CLOSE "<<dcch << " dump all:" <<gMMLayer.printMMInfo();

	dcch->chanSetState(L3LogicalChannel::chIdle);
	LOG(DEBUG) << "DCCHLoop exiting "<<dcch;
}

#if UNUSED_BUT_SAVE_FOR_UMTS	// but may be used for UTMS
void CSL3StateMachine::csl3ServiceLoop()
{
	if (IS_LOG_LEVEL(DEBUG)) {
		ostringstream os;
		os <<"Transaction Table:";
		if (gNewTransactionTable.dump(os,true)) { LOG(DEBUG)<<os.str(); }
	}
	try {
		int timeout = -1;
		// Process all messages in the queue first, for no particular reason.
		if (mCSL3Fifo.size() == 0) {
			// Invoke Procedures to process timeouts and also determine the next timeout.
			timeout = csl3HandleTimers();
		}
		GenericL3Msg *msg;
		if (timeout >= 0) {
			msg = mCSL3Fifo.read(timeout);	// wait only until the next timer expires.
			LOG(DEBUG) "read(timeout="<<timeout<<") returned:"<<msg;
		} else {
			msg = mCSL3Fifo.read(); 		// wait forever.
			LOG(DEBUG) "read() returned:"<<msg;
		}
		gResetWatchdog();
		if (msg) {	// If timeout, there will be no msg.
			csl3HandleMsg(msg);
		}
	}
	// FIXME: copy catch code from DCCHDispatcher()
	catch (...) {
		LOG(ERR) << "unhandled exception in CSL3StateMachine";
	}
}


static void *csl3ThreadLoop(void *unusedArg)
{
	while (1) { gCSL3StateMachine.csl3ServiceLoop(); }
	return NULL;
}

void CSL3StateMachine::csl3Start()
{
	mCSL3Thread = new Thread;
	//thread->start((void*(*)(void*))Control::DCCHDispatcher,chan);
	mCSL3Thread->start(csl3ThreadLoop,NULL);
}

CSL3StateMachine gCSL3StateMachine;
CSL3StateMachine::CSL3StateMachine() : mCSL3Thread(NULL) {}
#endif

void l3start()
{
	// We are not doing it this way in GSM.  Each channel has its own service loop.
	// if (l3rewrite()) { gCSL3StateMachine.csl3Start(); }
}


};	// namespace
