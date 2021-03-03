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

#define LOG_GROUP LogGroup::Control
#include <GSML3CCElements.h>
#include "ControlCommon.h"
#include "L3CallControl.h"
#include "L3StateMachine.h"
#include "L3TranEntry.h"
#include "L3MMLayer.h"
#include "L3SupServ.h"
#include <SIPDialog.h>
#include <Peering.h>
#include <GSMCommon.h>
#include <GSML3Message.h>
#include <GSMLogicalChannel.h>
#include <GSML3SSMessages.h>
#include <CLI.h>


namespace Control {

using namespace GSM;
using namespace SIP;


// The base class for CC [Call Control]
// SS messages may be sent to CC transactions.
class CCBase : public SSDBase {
	protected:
	MachineStatus handleIncallCMServiceRequest(const GSM::L3Message *l3msg);
	//MachineStatus stateRecvHold(const GSM::L3Hold*);
	MachineStatus defaultMessages(int state, const GSM::L3Message*);
	bool isVeryEarly();
	CCBase(TranEntry *wTran) : SSDBase(wTran) {}
	MachineStatus closeCall(TermCause cause);
	MachineStatus sendReleaseComplete(TermCause cause, bool sendCause);
	MachineStatus sendRelease(TermCause cause, bool sendCause);
	void handleTerminationRequest();
};

class MOCMachine : public CCBase {
	enum State {
		stateStartUnused,		// Reserve 0 superstitiously.
		stateCCIdentResult,
		stateAssignTCHFSuccess,
	};
	bool mIdentifyResult;
	MachineStatus sendCMServiceReject(MMRejectCause rejectCause,bool fatal);
	MachineStatus handleSetupMsg(const GSM::L3Setup *setup);
	MachineStatus serviceAccept();

	// The MOC is created by an initial CMServiceRequest:
	// |-----------CMServiceRequest------------>|                              | old: CMServiceResponder calls MOCProcedure
	// |<-------Authentication Procedure------->|                              | resolveIMSI
	// |<-----------CMServiceAccept ------------|                              | MOCProcedure
	// |-------------L3Setup(SDCH)------------->|                              | MOCProcedure
	// |<----------CC-CALL PROCEEDING-----------|------------INVITE----------->| MOCProcedure,MOCSendINVITE

	public:
	MachineStatus machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg);
	MOCMachine(TranEntry *wTran) : CCBase(wTran) {}
	const char *debugName() const { return "MOCMachine"; }
};	// mMOCProcedure;

class AssignTCHMachine : public CCBase {
	enum State {
		stateStart,
		stateAssignTimeout
	};
	// We have to suspend processing of SIP messages while we are busy changing channels.
	// We just let these messages go by, then after we get the new TCH (if we do),
	// we will send any l3 messages required by the then-current sip state.
	void sendReassignment();
	// The reassignment timer is how long we try to send reassignments; it should not abort the transaction
	// immediately when it expires or it might abort a successful reassignment, so this timer must not be in the L3TimerId list.
	// 
	Timeval TChReassignment;
	protected:

	// |<----------ChannelModeModify------------|     SIPState=Starting        | MOCProcedure if veryEarly
	// |----------ChannelModeModifyAck--------->|                              | MOCProcedure if veryEarly
	// |            call assignTCHF             |                              | (used only for EA; for VEA we assigned TCH in AccessGrantResponder)
	// |<---------L3AssignmentCommand-----------|                              | assignTCHF, repeated until answered
	// |--------AssignmentComplete(FACCH)------>|                              | DCCHDispatchRR,AssignmentCompleteHandler, calls MOCController or MTCController

	public:
	MachineStatus machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg);
	AssignTCHMachine(TranEntry *wTran) : CCBase(wTran) {}
	const char *debugName() const { return "AssignTCHMachine"; }
};	// mAssignTCHFProcedure;


class MTCMachine : public CCBase {
	enum State {
		stateStart,
		statePostChannelChange,
	};
	public:
	MachineStatus machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg);
	MTCMachine(TranEntry *wTran) : CCBase(wTran) {}
	const char *debugName() const { return "MTCMachine"; }
};


class InboundHandoverMachine : public CCBase {
	bool mReceivedHandoverComplete;
	enum State {
		stateStart,
	};
	public:
	MachineStatus machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg);
	InboundHandoverMachine(TranEntry *wTran) : CCBase(wTran), mReceivedHandoverComplete(false) {}
	const char *debugName() const { return "InboundHandoverMachine"; }
};

class InCallMachine : public CCBase {
	enum State {
		stateStart
	};
	bool mDtmfSuccess;
	char mDtmfKey;	// Encoded as used inside L3KeypadFacility
	void acknowledgeDtmf();
	public:
	MachineStatus machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg);
	//MachineStatus machineRunSipMsg(const SIP::DialogMessage *sipmsg);
	InCallMachine(TranEntry *wTran) : CCBase(wTran) {}
	const char *debugName() const { return "InCallMachine"; }
};

// MOCMachine: Mobile Originated Call State Machine
// GSM 4.08 5.2.1 Mobile originating call establishment.
// On entry phone has an RR connection but is trying to get the CM connection established.
// We run MM procedures (identify, authenticate) to associate the RR LogicalChannel with a MMUser.
// Process up through receiving the L3Setup message.
// This message indicates establishment of both an MM Layer (ie, LocationUpdating has been performed 4.08 4.5.1) and a CM Layer connection.
void startMOC(const GSM::L3MMMessage *l3msg, MMContext *dcch, CMServiceTypeCode serviceType)
{
	LOG(DEBUG) <<dcch;
	TranEntry *tran = TranEntry::newMOC(dcch,serviceType);

	MOCMachine *mocp = new MOCMachine(tran);
	// The message is CMServiceRequest.
	tran->lockAndStart(mocp,(GSM::L3Message*)l3msg);
}

#if UNUSED
//MachineStatus ProcedureDetach::machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg) 
//{
//	PROCLOG2(DEBUG,state)<<LOGVAR(l3msg)<<LOGVAR(sipmsg)<<LOGVAR2("imsi",tran()->subscriber());
//	getDialog()->dialogCancel();  // reudundant, chanLost would do it.  Does nothing if dialog not yet started.
//	setGSMState(CCState::NullState);	// redundant, we are deleting this transaction.
//	channel()->l3sendm(L3ChannelRelease());
//	channel()->chanRelease(HARDRELEASE);
//	//channel()->l3sendp(HARDRELEASE);
//	//channel()->chanLost();
//	return MachineStatusOK;
//}
#endif


// Identical to teCloseCallNow.
MachineStatus CCBase::sendReleaseComplete(TermCause cause, bool sendCause)
{
	LOG(INFO) << "SIP term info sendReleaseComplete"<<LOGVAR(cause); // SVGDBG&pat
	tran()->teCloseCallNow(cause,sendCause);
	return MachineStatus::QuitTran(cause);
}

MachineStatus CCBase::sendRelease(TermCause cause, bool sendCause)
{
	LOG(INFO) << "SIP term info sendRelease cause: " << cause; // SVGDBG
	tran()->teCloseDialog(cause);		// redundant, would happen soon anyway.
	if (isL3TIValid()) {
		unsigned l3ti = getL3TI();
		if (tran()->clearingGSM()) {
			// Oops!  Something went wrong.  Clear immediately.
			LOG(INFO) << "SIP term info call teCloseCallNow  cause: " << cause;
			tran()->teCloseCallNow(cause,sendCause);
			return MachineStatus::QuitTran(cause);
		} else {
			// This tells the phone that the network intends to release the TI.
			// The handset is supposed to respond with ReleaseComplete.
			if (sendCause) {
				// If BTS initiates release, we must include the cause element.
				channel()->l3sendm(GSM::L3Release(l3ti,cause.tcGetCCCause()));
			} else {
				// Handset sent disconnect; our reply L3Release does not include a Cause Element.  GSM 4.08 9.3.18.1.1
				channel()->l3sendm(GSM::L3Release(l3ti));
			}
			setGSMState(CCState::ReleaseRequest);
			timerStart(T308,T308ms,TimerAbortTran);
			LOG(DEBUG) << gMMLayer.printMMInfo();
			return MachineStatusOK;	// We are waiting for a ReleaseComplete
		}
	} else {
		// The transaction is already dead.  Kill the state machine and the next layer will send the RR Release.
		LOG(DEBUG) << gMMLayer.printMMInfo();
		return MachineStatus::QuitTran(cause);
	}
}

// Perform a network initiated clearing 24.008 5.4.  If things look ok send a disconnect and continue waiting for a Release message,
// or if things have gone wrong, send a ReleaseRequest and kill the transaction.  We used to do that all the time
// but some handsets (BLU phone) report "Network Failure" if you dont go through the disconnect procedure.
// We dont send the RR releaes at this level - the MM layer does that after this transaction dies.
MachineStatus CCBase::closeCall(TermCause cause)
{
	LOG(INFO) << "SIP term info closeCall"<<LOGVAR(cause); // SVGDBG&pat
	WATCHINFO("closeCall"<<LOGVAR(cause) <<" "<<channel()->descriptiveString());
	tran()->teCloseDialog(cause);	// Make sure; this is redundant because the call will be repeated when the transaction is killed,
	// We could assert this if we dont call this until after an L3Setup.
	if (isL3TIValid()) {
		unsigned l3ti = getL3TI();
		// We dont have to send a disconnect at all, but if you dont, the phone may report that the call "failed".
		CallState ccstate = tran()->getGSMState();
		if (ccstate == CCState::Active) {
			if (1) {
				channel()->l3sendm(GSM::L3Disconnect(l3ti,cause.tcGetCCCause()));
				setGSMState(CCState::DisconnectIndication);
			} else {
				// (pat 10-24-2013) As an option per 24.008 5.4.2: we could send a Release message and start T308
				channel()->l3sendm(GSM::L3Release(l3ti,cause.tcGetCCCause()));
				setGSMState(CCState::ReleaseRequest);
			}
			timerStart(T308,T308ms,TimerAbortTran);
			return MachineStatusOK;	// Wait for ReleaseComplete.
		} else if (ccstate != CCState::NullState && ccstate != CCState::ReleaseRequest) {
			channel()->l3sendm(GSM::L3ReleaseComplete(l3ti,cause.tcGetCCCause()));	// This is a CC message that releases this Transaction.
		}
	} else {
		// If no TI we cant send any CC release messages, just kill the transaction and if nothing is happening
		// the MM layer will send an RR release on the channel.
	}
	WATCH("CLOSE CALL:"<<cause <<gMMLayer.printMMInfo());

	setGSMState(CCState::NullState);	// redundant, we are deleting this transaction.
	LOG(DEBUG) << gMMLayer.printMMInfo();
	if (IS_LOG_LEVEL(DEBUG)) { CommandLine::printChansV4(cout,false); }
	LOG(DEBUG) <<"finish";
	// The caller is a state machine.  We cannot remove the transaction yet because the state machine is still using it.
	// The state machine caller should return MachineStatusQuitTran which causes handleMachineStatus()
	// to call teRemove to finish transaction destruction.
	return MachineStatus::QuitTran(cause);
}

// This is called outside the normal procedure handling, so we dont return a MachineStatus.
// On return the caller will release the RR channel preemptively.
// TODO: Get rid of this.  The terminator should Send a message to the MMLayer indicating type of termination (operator intervention
// or emergency call) which should be copied out to all the transactions in the MMContext.
void CCBase::handleTerminationRequest()
{
	LOG(INFO) "SIP term info handleTerminationRequest call closeCallNow Preemption";
	// TODO: It may be pre-emption by emergency call.
	//tran()->teCloseCallNow(TermCause::Local(TermCodeOperatorIntervention));
	//tran()->teCloseCallNow(TermCause::Local((L3Cause::AnyCause)L3Cause::Operator_Intervention));
	tran()->teCloseCallNow(TermCause::Local(L3Cause::Operator_Intervention),true);
}


// See: callManagementDispatchGSM
// TODO (pat) 2-2014: The MS can send DTMF messages before the call is connected; currently we just ignore them, which works,
// but we should probably at least send a reject.
MachineStatus CCBase::defaultMessages(int state, const GSM::L3Message *l3msg)
{
	if (!l3msg) { return unexpectedState(state,l3msg); }	// Maybe unhandled dialog message.
	switch (state) { // L3CASE_RAW(l3msg->PD(),l3msg->MTI()) {
		case L3CASE_CC(Hold): {
			const L3Hold *hold = dynamic_cast<typeof(hold)>(l3msg);
			PROCLOG(NOTICE) << "rejecting hold request from " << tran()->subscriber();
			channel()->l3sendm(GSM::L3HoldReject(getL3TI(),L3Cause::Service_Or_Option_Not_Available));
			return MachineStatusOK;	// ignore bad message otherwise.
		}
		case L3CASE_MM(CMServiceAbort): {
			const L3CMServiceAbort *cmabort = dynamic_cast<typeof(cmabort)>(l3msg);
			// 4.08 5.2.1 and 4.5.1.7: If the MS wants to cancel before we get farther it should send a CMServiceAbort.
			PROCLOG(INFO) << "received CMServiceAbort, closing channel and clearing";
			timerStopAll();
			// 603 is only supposed to be used if we know there is no second choice like voice mail.
			return closeCall(TermCause::Local(L3Cause::Call_Rejected));	// normal event.
		}
		case L3CASE_CC(Disconnect): { // MOD
			// 4.08 5.4.3 says we must be prepared to receive a DISCONNECT any time.
			timerStopAll();
			const L3Disconnect *dmsg = dynamic_cast<typeof(dmsg)>(l3msg);
			return sendRelease(TermCause::Local(dmsg->cause().cause()),false);	// (pat) Preserve the cause the handset sent us.
			//return sendRelease(TermCause::Local(L3Cause::Normal_Call_Clearing));  //svg change from CallRejected to NormalCallClearing 05/29/14
		}
		case L3CASE_CC(Release): {
			// 24.008 5.4.3.3: In any state except ReleaseRequest send a ReleaseComplete, then kill the transaction,
			timerStopAll();
			const L3Release *dmsg = dynamic_cast<typeof(dmsg)>(l3msg);
			if (dmsg->mFacility.mExtant) WATCH(dmsg);	// USSD DEBUG!
			// (pat) The cause is optional; only included if the Release message is used to initiate call clearing.
			L3Cause::CCCause cccause;
			if (dmsg->haveCause()) {
				cccause = dmsg->cause().cause();
			} else {
				cccause = L3Cause::Normal_Call_Clearing;
			}
			return sendReleaseComplete(TermCause::Local(cccause),false);
		}
		case L3CASE_CC(ReleaseComplete): {
			// 24.008 5.4.3.3: Just kill the transaction immediately..
			const L3ReleaseComplete *dmsg = dynamic_cast<typeof(dmsg)>(l3msg);
			if (dmsg->mFacility.mExtant) WATCH(dmsg);	// USSD DEBUG!
			timerStopAll();
			//changed 10-24-13: return closeCall(L3Cause::Normal_Call_Clearing);	// normal event.
			// tran()->teCloseDialog(TermCause::Local(TermCodeNormalDisconnect)); // Redundant, and we dont know what initiated it so this error is not correct
			setGSMState(CCState::NullState);	// redundant, we are deleting this transaction.
			// (pat) The ReleaseComplete message may be sent by handset in response to our request for Release,
			// in which case we dont want to change the termination cause from what it was previously,
			// or it could be the handset informing us for the first time that it wants to delete this transaction.
			TermCause cause = tran()->mFinalDisposition;
			if (cause.tcIsEmpty()) { cause = TermCause::Local(L3Cause::Normal_Call_Clearing); }
			return MachineStatus::QuitTran(cause);
		}
		case L3CASE_MM(IMSIDetachIndication): {
			const GSM::L3IMSIDetachIndication* detach = dynamic_cast<typeof(detach)>(l3msg);
			timerStopAll();
			// The IMSI detach procedure will release the LCH.
			PROCLOG(INFO)  << "GSM IMSI Detach " << *tran();
			LOG(INFO) << "SIP term info IMSIDetachIndication text: " << l3msg->text();
			// Must unregister.  FIXME: We're going to do that first because the stupid layer2 may hang in l3sendm.
			L3MobileIdentity mobid = detach->mobileID();
			imsiDetach(mobid,channel());

			channel()->l3sendm(L3ChannelRelease());
			// Many handsets never complete the transaction.
			// So force a shutdown of the channel.
			// (pat 5-2014) Changed from HARDRELEASE to RELEASE - we need to let the LAPDm shut down normally.
			channel()->chanRelease(L3_RELEASE_REQUEST,TermCause::Local(L3Cause::IMSI_Detached));
			return MachineStatus::QuitChannel(TermCause::Local(L3Cause::IMSI_Detached));
		}
		case L3CASE_RR(ApplicationInformation): {
			const GSM::L3ApplicationInformation *aimsg = dynamic_cast<typeof(aimsg)>(l3msg);
			// handle RRLP answer.
			// TODO
			return MachineStatusOK;
		}
		case L3CASE_SS(Register):
		case L3CASE_SS(ReleaseComplete):
		case L3CASE_SS(Facility): {
			return handleSSMessage(l3msg);
		}
		default:
			return unexpectedState(state,l3msg);
	}
}

MachineStatus CCBase::handleIncallCMServiceRequest(const GSM::L3Message *l3msg)
{
	const GSM::L3CMServiceRequest *cmsrq = dynamic_cast<typeof(cmsrq)>(l3msg);
	assert(cmsrq);
	// SMS submission?  The rest will happen on the SACCH.
	if (cmsrq->serviceType().type() == GSM::L3CMServiceType::ShortMessage) {
		PROCLOG(INFO)  << "in call SMS submission on " << *channel();
		//FIXME:
		//InCallMOSMSStarter(transaction);
		//LCH->l3sendm(GSM::L3CMServiceAccept());
		return MachineStatusOK;
	}
	// For now, we are rejecting anything else.
	PROCLOG(NOTICE) << "cannot accept additional CM Service Request from " << tran()->subscriber();
	// Can never be too verbose.
	// (pat) There is no termcause here because there is nothing to terminate.
	channel()->l3sendm(GSM::L3CMServiceReject(L3RejectCause::Service_Option_Not_Supported));
	return MachineStatusOK;
}

// The reject cause is 4.08 10.5.3.6.  It has values similar to L3Cause 10.5.4.11
MachineStatus MOCMachine::sendCMServiceReject(MMRejectCause rejectCause, bool fatal)
{
	channel()->l3sendm(L3CMServiceReject(rejectCause));
	LOG(INFO) << "SIP term info closeChannel called in sendCMServiceReject";
	if (fatal) {
		// Authorization failure.  It is an MM level failure, but a "normal event" at the RR level.
		return closeChannel(L3RRCause::Normal_Event,L3_RELEASE_REQUEST,TermCause::Local(rejectCause));
	} else {
		// This would happen if the user is not authorized for the particular service requested.
		// This case does not currently occur.
		tran()->teCloseDialog(TermCause::Local(rejectCause));
		return MachineStatus::QuitTran(TermCause::Local(rejectCause));
	}
}

bool CCBase::isVeryEarly() { return (channel()->chtype()==GSM::FACCHType); }


// GSM 04.08 5.2.1.2
// This is where we set the TI [Transaction Identifier] in the TranEntry to what the MS sent us in the L3Setup message.
// We also start the SIP dialog now.
MachineStatus MOCMachine::handleSetupMsg(const L3Setup *setup)
{
	// pat fixed.  See comments at MOCInitiated. setGSMState(CCState::MOCInitiated);

	PROCLOG(INFO) << *setup;
	gReports.incr("OpenBTS.GSM.CC.MOC.Setup");
	if (setup->mFacility.mExtant) WATCH(setup);	// USSD DEBUG!

	// See GSM 04.07 11.2.3.1.3.
	// Set the high bit, since this TI came from the MS.
	// Set l3ti before calling any aborts so we will handle the response to the MS properly.
	// (pat) The MS will continue to use the original TI (without the high bit set) when it communicates with us,
	// and We need to set the high bit only when we send an L3TI to the MS.
	tran()->setL3TI(setup->TI() | 0x08);
	tran()->setCodecs(setup->getCodecSet());
	string calledNumber;
	{
		if (!setup->haveCalledPartyBCDNumber()) {
			// FIXME -- This is quick-and-dirty, not following GSM 04.08 5.
			// (pat) I disagree: this exactly follows GSM 4.08 5.4.2
			PROCLOG(WARNING) << "MOC setup with no number";
			// It is MOC, so we should not be sending an error to any dialogs, but we will fill in a SIP error anyway.
			return closeCall(TermCause::Local(L3Cause::Missing_Called_Party_Number));
		}
		const L3CalledPartyBCDNumber& calledPartyIE = setup->calledPartyBCDNumber();
		tran()->setCalled(calledPartyIE);
		calledNumber = calledPartyIE.digits();
	}



	// Start a new SIP Dialog, which sends an INVITE.
	PROCLOG(DEBUG) << "SIP start engine";
	//getDialog()->dialogOpen(tran()->subscriberIMSI());
	//const char * imsi = tran()->subscriberIMSI();		// someday these will be a strings already
	// The sipDialogMOC creates the SIP Dialog and sends the INVITE.
	// The setDialog associates the new dialog with this transaction.
	SipDialog *dialog = SipDialog::newSipDialogMOC(tran()->tranID(),tran()->subscriber(),calledNumber,tran()->getCodecs(), channel());
	if (dialog == NULL) {
		// We failed to create the SIP session for some reason.  I dont think this can happen, but dont crash here.
		LOG(ERR) << "Failed to create SIP Dialog, dropping connection";
		LOG(INFO) << "SIP term info closeChannel called in handlesetupMessage";
		return closeChannel(L3RRCause::Unspecified,L3_RELEASE_REQUEST,TermCause::Local(L3Cause::Sip_Internal_Error));
	}
	//setDialog(dialog);	Moved into newSipDialogMOC to eliminate a race.


	// Once we can start SIP call setup, send Call Proceeding.
	// (pat) 4.08 5.2.1.2 says we are supposed to verify the number before sending call proceeding.
	// (pat) TODO: I dont think this is right - supposed to wait for SIP proceeding before sending this.
	PROCLOG(INFO) << "Sending Call Proceeding, transaction:" <<tran();
	channel()->l3sendm(GSM::L3CallProceeding(getL3TI()));
	setGSMState(CCState::MOCProceeding); 
	return MachineStatusOK;
}


// FIXME -- At this point, verify that the subscriber has access to this service.
// If the subscriber isn't authorized, send a CM Service Reject with
// cause code, 0x41, "requested service option not subscribed",
// followed by a Channel Release with cause code 0x6f, "unspecified".
// Otherwise, proceed to the next section of code.
// For now, we are assuming that the phone won't make a call if it didn't
// get registered.
MachineStatus MOCMachine::serviceAccept()
{
	GPRS::GPRSNotifyGsmActivity(tran()->subscriber().mImsi.c_str());

	// Allocate a TCH for the call, if we don't have it already.
	// TODO: This should be a function in MMContext.
	if (!isVeryEarly()) {
		if (! channel()->reassignAllocNextTCH()) {
			TermCause cause = TermCause::Local(L3Cause::No_Channel_Available);
			channel()->l3sendm(GSM::L3CMServiceReject(L3RejectCause::Congestion));
			tran()->teCloseDialog(cause);	// TODO: This will become redundant with closeChannel and should be removed later.
			// (pat) TODO: Now what? We are supposed to go back to using SDCCH in case of an ongoing SMS,
			// so lets just close the Transaction.
			LOG(INFO) << "SIP term info closeChannel called in serviceAccept";
			return closeChannel(L3RRCause::Normal_Event,L3_RELEASE_REQUEST,cause);
		}
	}

	// Let the phone know we're going ahead with the transaction.
	PROCLOG(INFO) << "sending CMServiceAccept";
	channel()->l3sendm(GSM::L3CMServiceAccept());

	// We are now waiting for a L3Setup message.
	// We could attach the MMContext to the MMUser at any time but it might start receiving calls or SMS immediately,
	// so we are going to wait until this call kicks off, which may be safer.

	return MachineStatusOK;
}


// This is used both for MOC and emergency calls, which are differentiated by the service type in the CMServiceRequest message.
// (pat) The Blackberry will attempt an MOC even if periodic LUR returned unauthorized!
MachineStatus MOCMachine::machineRunState(int state, const GSM::L3Message *l3msg, const SIP::DialogMessage *sipmsg)
{
	PROCLOG2(DEBUG,state)<<LOGVAR(l3msg)<<LOGVAR(sipmsg)<<LOGVAR2("msid",tran()->subscriber());
	switch (state) {
		// This is the start state:
		case L3CASE_MM(CMServiceRequest): {
			timerStart(T303,T303ms,TimerAbortTran);	// MS side: start CMServiceRequest sent; stop CallProceeding received.
			// This is both the start state and a request to start a new MO SMS when one is already in progress, as per GSM 4.11 5.4
			setGSMState(CCState::MOCInitiated);
			const L3CMServiceRequest *req = dynamic_cast<typeof(req)>(l3msg);
			const GSM::L3MobileIdentity &mobileID = req->mobileID();	// Reference ok - the SM is going to copy it.

			return machPush(new L3IdentifyMachine(tran(),mobileID, &mIdentifyResult), stateCCIdentResult);
		}

		case stateCCIdentResult: {
			// TODO: We may want an option to return an immediate CM service reject if this BTS is not configured
			// to handle calls, for example, if it is an SMS-only server or such like.
			// The L3IdentifyMachine checks for emergency calls, but we will check again here to be sure.
			if (mIdentifyResult) {
				return serviceAccept();
			} else {
				// If handset is not in TMSI table We do not return any programmable failure codes here,
				// we must return cause CM Service Reject Cause 4,
				// which will cause the MS to do a new Location Update, and the Location Update code
				// will either pass it or determine an appropriate reject code.
				return sendCMServiceReject(L3RejectCause::IMSI_Unknown_In_VLR,true);
			}
		}

#if 0	// (pat) 9-15-2013: replaced with code to call the common L3IdentifyMachine.
		case L3CASE_MM(CMServiceRequest): {
			const L3CMServiceRequest *req = dynamic_cast<typeof(req)>(l3msg);
			// We dont want to leave our GSMState indicator in NullState once we start
			// doing things here, so we want to change the state to something.
			// On receipt of CMServiceRequest we are doing MM procedures so you would think there is
			// no CC state yet, but apparently that is not the case; see comments at CCState::MOCInitiated,
			// indicating that this state is correct.
			setGSMState(CCState::MOCInitiated);
			// There is no specific timer in the documentation on the network side for this case.
			// T303 is defined on the MS side, and we use that.  It is a generic 30 second timer.
			timerStart(T303,T303ms,TimerAbortTran);	// MS side: start CMServiceRequest sent; stop CallProceeding received.

			// If we got a TMSI, find the IMSI.
			// Note that this is a copy, not a reference.
			GSM::L3MobileIdentity mobileID = req->mobileID();

			// ORIGINAL CODE: resolveIMSI(mobileID,LCH);

			// I think other messages are errors during this part of the state diagram, but the old
			// code ignored them (rather, it set the state improperly which error was later corrected)
			// while waiting for the RR AssignmentComplete message so I will too.

			// Pat says: Take care that RRLP does not use up the 30 second T303 timer running in the MS now.
			if (gConfig.getBool("Control.Call.QueryRRLP.Early")) {
				// TODO...
			}

			// Have an imsi already?
			if (mobileID.type()==IMSIType) {
				string imsi(mobileID.digits());
				tran()->setSubscriberImsi(string(mobileID.digits()),true);
				if (!gTMSITable.tmsiTabCheckAuthorization(imsi)) {
					return sendCMServiceReject(L3RejectCause::Requested_Service_Option_Not_Subscribed,true);
				}
				return serviceAccept();
			}

			// If we got a TMSI, find the IMSI.
			if (mobileID.type()==TMSIType) {
				unsigned authorized;
				string imsi = gTMSITable.tmsiTabGetIMSI(mobileID.TMSI(),&authorized);
				if (imsi.size()) {
					// TODO: We need to authenticate this.
					// But for now, just accept it.
					tran()->setSubscriberImsi(imsi,true);
					if (!authorized) {
						return sendCMServiceReject(L3RejectCause::Requested_Service_Option_Not_Subscribed,true);
					}
					return serviceAccept();
				}
			}


			// Still no IMSI?  Ask for one.
			// TODO: We should ask the SIP Registrar.
			// (pat) This is not possible if the MS is compliant (unless the TMSI table has been lost) -
			// the MS should have done a LocationUpdate first, which provides us with the IMSI.
			// Or maybe the tmsi table was deleted.
			PROCLOG(NOTICE) << "MOC with no IMSI or valid TMSI.  Reqesting IMSI.";
			timerStart(T3270,T3270ms,TimerAbortChan); // start IdentityRequest sent; stop IdentityResponse received.
			channel()->l3sendm(L3IdentityRequest(IMSIType));
			return MachineStatusOK;
		}

		// TODO: This should be moved to an MM Identify procedure run before starting the MOC.
		case L3CASE_MM(IdentityResponse): {
			timerStop(T3270);
			const L3IdentityResponse *resp = dynamic_cast<typeof(resp)>(l3msg);
			L3MobileIdentity mobileID = resp->mobileID();
			if (mobileID.type()==IMSIType) {
				string imsi(mobileID.digits());
				tran()->setSubscriberImsi(imsi,true);
				if (!gTMSITable.tmsiTabCheckAuthorization(imsi)) {
					return sendCMServiceReject(L3RejectCause::Requested_Service_Option_Not_Subscribed,true);
				}
				return serviceAccept();
			} else {
				// FIXME -- This is quick-and-dirty, not following GSM 04.08 5.
				PROCLOG(WARNING) << "MOC setup with no IMSI";	// (pat) It is used for MO-SMS, not MOC.
				// Reject cause in 10.5.3.6.
				// Cause 0x62 means "message type not not compatible with protocol state".
				return sendCMServiceReject(L3RejectCause::Message_Type_Not_Compatible_With_Protocol_State,false);
			}
			return something
		}
#endif


		case L3CASE_CC(Setup): {
			timerStop(T303);
			if (getGSMState() == CCState::MOCProceeding) {
				LOG(DEBUG) << "ignoring duplicate L3EmergencySetup";
				return MachineStatusOK;
			}
			const L3Setup *msg = dynamic_cast<typeof(msg)>(l3msg);
			
			MachineStatus stat = handleSetupMsg(msg);
			if (stat != MachineStatusOK) { return stat; }
			return machPush(new AssignTCHMachine(tran()), stateAssignTCHFSuccess);
		}

		case stateAssignTCHFSuccess: {
			// We have just received our shiny new TCH.  See if the SIP state changed while we were waiting;
			// if so, the sip messages themselves were discarded, but invite response, if any, was saved.
			// Take care: we are invoking a dialog state without passing the DialogMessage, which is gone.
			return machineRunState(L3CASE_DIALOG_STATE(getDialog()->getDialogState()),NULL,NULL);
		}

		case L3CASE_SIP(dialogStarted): {
			return MachineStatusOK;		// It just means the dialog has not received an answer to the initial INVITE yet.
		}
		case L3CASE_SIP(dialogProceeding): {
			// pat 2-2014: Tried out the L3Progress message to fix the ZTE lack of ring-back.
			// I notice we are sending an invalid Progress value so it was worth a try, but did not help.
			channel()->l3sendm(L3Progress(getL3TI()));
			if (getGSMState() != CCState::MOCProceeding) { // No CCState change on receiving this message.
				PROCLOG(ERR) << "MOC received SIP Progress message in unexpected GSM state:"<< getGSMState();
			}
			return MachineStatusOK;
		}
		case L3CASE_SIP(dialogRinging): {
#define ATTEMPT_TO_FIX_ZTE_PHONE 1
#if ATTEMPT_TO_FIX_ZTE_PHONE
			// pat 2-2014: The ZTE phone does not play in audio ringing during the Alerting.
			// Looks like a bug in the phone.  To try work around it add a Progress Indicator IE.
			// If you set in-band audio it will play whatever you send it, but it will just not generate its own ring tone in any case.
			//L3ProgressIndicator progressIE(L3ProgressIndicator::ReturnedToISDN);  This one tells it to not use in-band audio, but did not help.
			//L3ProgressIndicator progressIE(L3ProgressIndicator::InBandAvailable);
			// To make the ZTE work I tried: Progress=Unspecified, NotISDN and Queuing.
			L3ProgressIndicator progressIE(L3ProgressIndicator::Queuing,L3ProgressIndicator::User);
			channel()->l3sendm(L3Alerting(getL3TI(),progressIE));
#else
			channel()->l3sendm(L3Alerting(getL3TI()));
#endif
			setGSMState(CCState::MOCDelivered);
			return MachineStatusOK;
		}
		case L3CASE_SIP(dialogActive): {
			// Success!  The call is connected.
			tran()->mConnectTime = time(NULL);

			if (gConfig.getBool("GSM.Cipher.Encrypt")) {
				int encryptionAlgorithm = gTMSITable.tmsiTabGetPreferredA5Algorithm(tran()->subscriberIMSI().c_str());
				if (!encryptionAlgorithm) {
					LOG(DEBUG) << "A5/3 and A5/1 not supported: NOT sending Ciphering Mode Command on " << *channel() << " for " << tran()->subscriberIMSI();
				} else if (channel()->getL2Channel()->decryptUplink_maybe(tran()->subscriberIMSI(), encryptionAlgorithm)) {
					LOG(DEBUG) << "sending Ciphering Mode Command on " << *channel() << " for IMSI" << tran()->subscriberIMSI();
					channel()->l3sendm(GSM::L3CipheringModeCommand(
						GSM::L3CipheringModeSetting(true, encryptionAlgorithm),
						GSM::L3CipheringModeResponse(false)));
				} else {
					LOG(DEBUG) << "no ki: NOT sending Ciphering Mode Command on " << *channel() << " for IMSI" << tran()->subscriberIMSI();
				}
			}

			channel()->l3sendm(L3Connect(getL3TI()));
			setGSMState(CCState::ConnectIndication);
			getDialog()->MOCInitRTP();
			getDialog()->MOCSendACK();
			return MachineStatusOK;	// We are waiting for the ConnectAcknowledge.
		}

		case L3CASE_CC(ConnectAcknowledge): {
			if (getDialog()->isActive()) { 
				// We're rolling.  Fire up the in-call procedure.
				setGSMState(CCState::Active);
				return callMachStart(new InCallMachine(tran()));
			} else if (getDialog()->isFinished()) {
				// The SIP side hung up on us!
				TermCause cause = dialog2TermCause(getDialog());
				return closeCall(cause);
			} else {
				// Not possible.
				PROCLOG(ERR) << "Connect Acknowledge received in incorrect SIP Dialog state:"<< getDialog()->getDialogState();
			}
			return callMachStart(new InCallMachine(tran()));
		}

		case L3CASE_RR(AssignmentComplete): {	// Ignore duplicate message subsequent to AssignTCHF.
			PROCLOG(INFO) << "Ignoring duplicate GSM AssignmentComplete " << *tran();
			return MachineStatusOK;
		}
		case L3CASE_RR(ChannelModeModifyAcknowledge): {	// Ignore duplicate message subsequent to AssignTCHF.
			PROCLOG(INFO) << "Ignoring duplicate GSM ChannelModeModifyAcknowledge " << *tran();
			return MachineStatusOK;
		}

		case L3CASE_SIP(dialogBye): {
			// The other user hung up before we could finish.
			return closeCall(dialog2ByeCause(getDialog()));
		}
		case L3CASE_SIP(dialogFail): {
			// 0x11: "User Busy";  0x7f "Interworking unspecified"
			// (pat) Since this is MOC, the SIP code supplied in the cause should not be used,
			// but we will be ultra cautious and preserve it.
			TermCause cause = dialog2TermCause(getDialog());
			LOG(INFO) << "SIP dialogFail"<<LOGVAR(cause);
			return closeCall(cause);
			break;
		}

#if TODO // TODO: What to do about this?
	MachineStatus MOCMachine::stateExpiredT303()
	{
		PROCLOG(INFO) << "T303 expired, closing channel and clearing";
		return closeChannel(?); // no sip yet, just exit
	}
#endif
	default:
		return defaultMessages(state,l3msg);
	}
}


//=== AssignTCHMachine ===
// Replaces assignTCHF()


// TODO: This should move to MMContext.
void AssignTCHMachine::sendReassignment()
{
	static const GSM::L3ChannelMode speechMode(GSM::L3ChannelMode::SpeechV1);

	if (isVeryEarly()) { return; }
	GSM::TCHFACCHLogicalChannel *tch = dynamic_cast<typeof(tch)>(channel()->mNextChan);
	// FIXME - We should probably be setting the initial power here.
	channel()->l3sendm(GSM::L3AssignmentCommand(tch->channelDescription(),speechMode));
}

MachineStatus AssignTCHMachine::machineRunState(int state, const GSM::L3Message *l3msg, const SIP::DialogMessage *sipmsg)
{
	PROCLOG2(DEBUG,state)<<LOGVAR(l3msg)<<LOGVAR(sipmsg)<<LOGVAR2("imsi",tran()->subscriber());
	static const GSM::L3ChannelMode speechMode(GSM::L3ChannelMode::SpeechV1);

	//beginning:
	switch (state) {

		case stateStart: {
			// If this timer goes off the assignment was (possibly) successful, at least it was not unsuccessful.
			//onTimeout1(GSM::T3101ms-1000,stateAssignTimeout);

			// We postpone processing any dialog messages until after this procedure.
			//tran()->mSipDialogMessagesBlocked = true;

			// (pat) The original assignTCHF code sent the L3Assignment multiple times.
			// That shouldnt be necessary because it is using LAPDm, but I am going to duplicate the behavior.

			if (isVeryEarly()) {
				// For very early assignment, we gave the MS a TCH in the initial ImmediateAssignment but we need a mode change.
				static const GSM::L3ChannelMode modemsg(GSM::L3ChannelMode::SpeechV1);
				GSM::TCHFACCHLogicalChannel *tch = dynamic_cast<typeof(tch)>(channel());
				channel()->l3sendm(L3ChannelModeModify(tch->channelDescription(),modemsg));
			} else {
				// For early (not "very early") assignment, we gave the MS an SDDCH in the initial ImmediateAssignment.
				// Send the TCH assignment now.
				// (pat) We do not support "late assignment" as defined in 4.08 7.3.2.
				channel()->reassignStart();
				// And I quote 4.08 11.1.2: "This timer [T3101] is started when a channel is allocated with an IMMEDIATE ASSIGNMENT message.
				// It is stopped when the MS has correctly seized the channels."
				// If we receive a reassignment failure we will resend the assignment, which often works the second time.
				// The TChReassignment timer controls how long we will keep re-trying.
				// We used to use 3 (T3101-1) seconds, but I dont think this time is related to T3101 and 
				// I dont know what the ultimate limit is, maybe nothing.  I am going to up it just use T3101.
				TChReassignment.future(T3101ms);
				timerStart(T3101,T3101ms,stateAssignTimeout);	// This timer will truly abort.
				sendReassignment();		// Sets T3101 as a side effect, way down in L1Decoder.
			}
			return MachineStatusOK;
		}

	case L3CASE_RR(ChannelModeModifyAcknowledge): {
		const GSM::L3ChannelModeModifyAcknowledge*ack = dynamic_cast<typeof(ack)>(l3msg);
		bool modeOK = (ack->mode()==speechMode);

		// if (!modeOK) return abortAndRemoveCall(transaction,LCH,GSM::L3Cause(0x06));
		// (pat) TODO: Why is this todo here?  network send 'ChannelUnacceptable'?
		// Since we already started sip, if the channel is unacceptable the only recovery to close the call.
		//tran()->mSipDialogMessagesBlocked = false;
		if (!modeOK) return closeCall(TermCause::Local(L3Cause::Channel_Unacceptable));
		return MachineStatusPopMachine;
	}

	// We retry this loop in case there are stray messages in the channel.
	// On some phones, we see repeated Call Confirmed messages on MTC.

	//case stateAssignRetry:			// We are sending the TCH assignment on the old SDCCH.
	//	DCCH->l3sendm(GSM::L3AssignmentCommand(TCH->channelDescription(),GSM::L3ChannelMode(GSM::L3ChannelMode::SpeechV1)));
	//	return MachineStatusOK;

	// This arrives on the new FACCH, however, the channel() comes from the Context which is still mapped to the old channel,
	// but reassignmentComplete knows this.
	case L3CASE_RR(AssignmentComplete): {
		timerStop(T3101);
		channel()->reassignComplete();
		PROCLOG(INFO) << "successful assignment";
		PROCLOG(DEBUG) << gMMLayer.printMMInfo();
		if (IS_WATCH_LEVEL(DEBUG)) {
			cout << "AssignmentComplete:\n";
			CommandLine::printChansV4(cout,false);
		}
		//tran()->mSipDialogMessagesBlocked = false;	// Next process will handle the postponed dialog messages.
		return MachineStatusPopMachine;
	}

	case L3CASE_RR(AssignmentFailure): {
		// We tried to reassign the MS from SDCCH to TCH and failed.
		// This arrives on the old SDCCH after "The mobile station has failed to seize the new channel."
		// The old code continually retried in this case.  So we will too, because
		// conceivably this could be working around some bug in OpenBTS.
		// Old code retried until T3101ms-1000, which just cant be right.
		if (! TChReassignment.passed()) {
			sendReassignment();
			return MachineStatusOK;
		} else {
			// (pat) redundant: chanFreeContext(TermCause::Local(L3Cause::Channel_Assignment_Failure));
			goto caseAssignTimeout;
		}
	}

	case stateAssignTimeout: {
		// This is the case where we received neither AssignmentComplete nor AssignmentFailure - it is loss of radio contact.
		LOG(INFO) << "SIP term info stateAssignTimeout NoUserResponding";
		caseAssignTimeout:
		channel()->reassignFailure();
		// TODO: This is not optimal - we should drop back to the MMLayer to see if it wants to do something else.
		// Determine and pass cause	SVGDBG
		LOG(INFO) << "SIP term info dialogCancel called in AssignTCHMachine::machineRunState";
		TermCause cause = TermCause::Local(L3Cause::Channel_Assignment_Failure);
		if (getDialog()) { getDialog()->dialogCancel(cause); }	// Should never be NULL, but dont crash.
		// We dont call closeCall because we already sent the specific RR message required for this situation.
		LOG(INFO) << "SIP term info closeChannel called in AssignTCHMachine::machineRunState 1";
		return closeChannel(L3RRCause::No_Activity_On_The_Radio,L3_RELEASE_REQUEST,cause);
	}

	// This would be a new CMServiceRequest, eg, for SMS message.
	// TODO: Can the MS send this so early in the MOC process?
	case L3CASE_MM(CMServiceRequest): {
		handleIncallCMServiceRequest(l3msg);
		// Resend the channel assignment.
		sendReassignment();	// duplicates old code, but is this really necessary?
	}

	case L3CASE_CC(Setup):
		LOG(DEBUG) << "ignoring duplicate L3Setup";
		return MachineStatusOK;

	default:
		if (sipmsg) {
			LOG(DEBUG) << "Dialog message received in AssignTCHF procedure.";
			// We just ignore sip messages.  The caller will handle the final SIP state when we return.
			return MachineStatusOK;
		}
		return defaultMessages(state,l3msg);
	}
}


// Timer values in 24.008 table 11.4
MachineStatus MTCMachine::machineRunState(int state, const GSM::L3Message* l3msg, const SIP::DialogMessage *sipmsg)
{
	PROCLOG2(DEBUG,state)<<LOGVAR(state)<<LOGVAR(l3msg)<<LOGVAR(sipmsg)<<LOGVAR2("imsi",tran()->subscriber());
	switch(state) {
		case stateStart: {
			if (getDialog()->isFinished()) {
				// SIP side closed already.
				//formerly: return closeCall(L3Cause::Interworking_Unspecified);
				return closeCall(dialog2TermCause(getDialog()));
			}

			// Allocate channel now, to be sure there is one.
			// Formerly all we had to do was check the VEA flag, since that controlled the channel type,
			// but it is better to test for TCHF directly - this works for testcall where the channel type was
			// specified by the user, and also handles the rare case where the VEA option changed on us.
			//if (!isVeryEarly())
			if (! channel()->isTCHF()) {
				if (! channel()->reassignAllocNextTCH()) {
					channel()->l3sendm(GSM::L3CMServiceReject(L3RejectCause::Congestion));
					TermCause cause = TermCause::Local(L3Cause::No_Channel_Available);
					tran()->teCloseDialog(cause);
					// (pat) TODO: We are supposed to go back to using SDCCH in case of an ongoing SMS.
					LOG(INFO) << "SIP term info closeChannel called in AssignTCHMachine::machineRunState 2";
					return closeChannel(L3RRCause::Normal_Event,L3_RELEASE_REQUEST,cause);
				}
			}

			// Allocate Transaction Identifier
			unsigned l3ti = channel()->chanGetContext(true)->mmGetNextTI();
			tran()->setL3TI(l3ti);

			// Send Setup message to MS.
			L3Setup setupmsg(l3ti,tran()->calling());
			// pat 2-2014: Attempt to make the buggy ZTE phone by sending an explicit L3Signal IE in the L3Setup message.
			// Update: did not help.
			//L3Signal tone(L3Signal::SignalBusyToneOn);	// Tryed both Ringing and Busy tone, no joy.
			//setupmsg.setSignal(tone);
			PROCLOG(INFO) << "sending L3Setup to call " << LOGVAR2("calling",tran()->calling()) << tran() <<LOGVAR(setupmsg);
			channel()->l3sendm(setupmsg);
			setGSMState(CCState::CallPresent);
			timerStart(T303,T303ms,TimerAbortTran);	// Time state "Call Present"; start CMServiceRequest recv; stop CallProceeding recv.

			// And send trying message to SIP
			if (getDialog()) { getDialog()->MTCSendTrying(); }

			return MachineStatusOK; // Wait for L3CallConfirmed message.
		}

		case L3CASE_CC(CallConfirmed): {
			timerStop(T303);
			// Some handsets send a CallConfirmed both before and after the channel change.
			if (getGSMState() == CCState::MTCConfirmed) {
				LOG(DEBUG) << "ignoring duplicate L3CallConfirmed";
				return MachineStatusOK;
			}
			setGSMState(CCState::MTCConfirmed);
			timerStart(T310,T310ms,TimerAbortTran);	// Time state "Call Confirmed"; start CallConfirmed recv; stop Alert,Connect,Disconnect recv.
			// Change channels.
			return machPush(new AssignTCHMachine(tran()), statePostChannelChange);
		}

		case statePostChannelChange: {
			// We just wait for something else to happen.
			if (IS_LOG_LEVEL(DEBUG)) { CommandLine::printChansV4(cout,false); }
			switch (getDialog()->getDialogState()) {
				case DialogState::dialogUndefined:
				case DialogState::dialogStarted:
				case DialogState::dialogProceeding:
				case DialogState::dialogRinging:
				case DialogState::dialogDtmf:
					// We dont care about these.
					return MachineStatusOK;	// Waiting for L3Alerting or L3Connect.
				case DialogState::dialogActive:
				case DialogState::dialogBye:
				case DialogState::dialogFail:
					return machineRunState(L3CASE_DIALOG_STATE(getDialog()->getDialogState()),NULL,NULL);
			}
		}


		// TODO: Should we resend the Ringing message on some timer?
		case L3CASE_CC(Alerting): {
			// We send a Ringing indication to SIP every time we receive an L3Alerting message.
			const GSM::L3Alerting*msg = dynamic_cast<typeof(msg)>(l3msg);
			if (msg->mFacility.mExtant) WATCH(msg);	// USSD DEBUG!
			timerStart(T301,T301ms,TimerAbortTran);		// Time state "Call Received"; start Alert recv; stop Connect recv.
			setGSMState(CCState::CallReceived);
			if (getDialog()) { getDialog()->MTCSendRinging(); }
			return MachineStatusOK;		// Waiting for L3Connect.
		}

		case L3CASE_CC(Connect): {
			timerStop(T301);
			timerStop(T303);
			timerStop(T310);
			timerStopAll();		// a little redundancy here.
			if (getGSMState() == CCState::ConnectIndication) {
				// I think the code below would work ok, but this is neater.
				LOG(DEBUG) << "ignoring duplicate L3Connect";
				return MachineStatusOK;
			}
			//timerStop(TRing);
			// We used to set GSMstate Active when we received the Connect,
			// but now we wait until we send the ConnectAcknowledge, which is after we receive confirmation
			// from the SIP side.  This is necessary because it is not until then that rtp is inited,
			// and we use the CCState flag to indicate when the RTP traffic can start.
			// Setting state Active later is probably more technically correct too.
			//old: setGSMState(CCState::Active);
			setGSMState(CCState::ConnectIndication);	// Note: This may technically be an MOC only defined state.
			if (getDialog()) { getDialog()->MTCSendOK(tran()->chooseCodec(),channel()); }
			return MachineStatusOK;		// Wait for SIP OK-ACK
		}

		case L3CASE_SIP(dialogActive): {		// SIP Dialog received SIP ACK to 200 OK.
			// Success!  The call is connected.
			tran()->mConnectTime = time(NULL);

			// (pat) To doug: The place to move cipher starting is probably InCallMachine::machineRunState case stateStart.
			if (gConfig.getBool("GSM.Cipher.Encrypt")) {
				int encryptionAlgorithm = gTMSITable.tmsiTabGetPreferredA5Algorithm(tran()->subscriberIMSI().c_str());
				if (!encryptionAlgorithm) {
					LOG(DEBUG) << "A5/3 and A5/1 not supported: NOT sending Ciphering Mode Command on " << *channel() << " for IMSI" << tran()->subscriberIMSI();
				} else if (channel()->getL2Channel()->decryptUplink_maybe(tran()->subscriberIMSI(), encryptionAlgorithm)) {
					LOG(DEBUG) << "sending Ciphering Mode Command on " << *channel() << " for IMSI" << tran()->subscriberIMSI();
					channel()->l3sendm(GSM::L3CipheringModeCommand(
						GSM::L3CipheringModeSetting(true, encryptionAlgorithm),
						GSM::L3CipheringModeResponse(false)));
				} else {
					LOG(DEBUG) << "no ki: NOT sending Ciphering Mode Command on " << *channel() << " for IMSI" << tran()->subscriberIMSI();
				}
			}

			setGSMState(CCState::Active);
			getDialog()->MTCInitRTP();
			channel()->l3sendm(GSM::L3ConnectAcknowledge(tran()->getL3TI()));
			return callMachStart(new InCallMachine(tran()));
		}

		case L3CASE_SIP(dialogStarted):
		case L3CASE_SIP(dialogProceeding):
		case L3CASE_SIP(dialogRinging): {
			PROCLOG(ERR) << "MTC received unexpected SIP Dialog message: << sipmsg;SIP Progress message";
			return MachineStatusOK;
		}

		// SIP Dialog failure cases.
		case L3CASE_SIP(dialogBye): {
			// The other user hung up before we could finish.
			return closeCall(dialog2ByeCause(getDialog()));
		}
		case L3CASE_SIP(dialogFail): {
			// It cannot be busy because it is a MTC.
			// This most likely a CANCEL, ie, it is a Mobile Terminated Disconnect before the SIP dialog ACK.
			TermCause cause = dialog2TermCause(getDialog());
			LOG(INFO) << "SIP dialogFail"<<LOGVAR(cause);
			return closeCall(cause);	// formerly: (L3Cause::Interworking_Unspecified,500,"Dialog failure"));
		}

		default:
			return defaultMessages(state,l3msg);
	}
}


MachineStatus InboundHandoverMachine::machineRunState(int state, const GSM::L3Message *l3msg, const SIP::DialogMessage *sipmsg)
{
	PROCLOG2(DEBUG,state)<<LOGVAR(l3msg)<<LOGVAR(sipmsg)<<LOGVAR2("imsi",tran()->subscriber());

	switch (state) {
		case stateStart:
			// The MS has already established the channel.  The HandoverComplete message should arrive
			// immediately unless the signal goes bad.
			// We need a timer to make sure we eventually receive HandoverComplete.
			// There is no specific timer for this.
			// Instead of using the generic channel-loss timeout, use a shorter timer here.
			timerStart(THandoverComplete,5000,TimerAbortChan);
			return MachineStatusOK;

		case L3CASE_RR(HandoverComplete): {
			timerStop(THandoverComplete);
			mReceivedHandoverComplete = true;
			// MS has successfully arrived on BS2.  Open the SIPDialog and attempt to transfer the SIP session.

			// Send re-INVITE to the remote party.
			HandoverEntry *hop = tran()->getHandoverEntry(true);
			SIP::SipDialog *dialog = SIP::SipDialog::newSipDialogHandover(tran(),hop->mSipReferStr);
			if (dialog == NULL) {
				// We cannot abort the handover - it is too late.  All we can do is drop the call.
				LOG(ERR) << "handover failure due to failure to create dialog for " << tran();	// Will probably never happen.
				TermCause cause = TermCause::Local(L3Cause::Invalid_Handover_Message);
				closeCall(cause);
				LOG(INFO) << "SIP term info closeChannel called in InboundHandoverMachine::machineRunState 1";
				return closeChannel(L3RRCause::Normal_Event,L3_RELEASE_REQUEST,cause);
			}
			setDialog(dialog);
			setGSMState(CCState::HandoverProgress);
			timerStart(TSipHandover,4000,TimerAbortChan);
			return MachineStatusOK;	// Wait for SIP response from peer.
		}

		case L3CASE_SIP(dialogFail):
			// TODO: We should send a CC message to the phone based on the SIP fail code.
			LOG(INFO) << "SIP term info closeChannel called in InboundHandoverMachine::machineRunState 2";
			return closeCall(dialog2TermCause(getDialog()));
			//return closeChannel(L3RRCause::Normal_Event,L3_RELEASE_REQUEST);

		case L3CASE_SIP(dialogBye):
			// SIP end hung up.  Just hang up the MS.
			LOG(INFO) << "SIP term info closeChannel called in InboundHandoverMachine::machineRunState 3";
			return closeCall(dialog2ByeCause(getDialog()));
			//return closeChannel(L3RRCause::Normal_Event,L3_RELEASE_REQUEST);

		case L3CASE_SIP(dialogActive): {
			// Success!  SIP side is active.
			tran()->mConnectTime = time(NULL);
			timerStop(TSipHandover);
			getDialog()->MOCSendACK();

			// Send completion to peer BTS.  TODO: This should be in a separate thread.
			gPeerInterface.sendHandoverComplete(tran()->getHandoverEntry(true));

			// Convert to a normal call.  The Active status will (hopefully) cause RTP data to start
			// being transferred by the service loop as soon as we return...
			setGSMState(CCState::Active);
			getDialog()->MOCInitRTP();

			// We can connect to the MMUser now.
			// TODO: I moved this to InCallMachine but I dont want to test handover right now so leave this here too;
			// doesnt hurt to call mmAttachByImsi twice.
			string imsi = tran()->subscriberIMSI();
			if (imsi.empty()) {
				LOG(ALERT) "handover with empty imsi?";	// Should be an assert.
			}
			gMMLayer.mmAttachByImsi(channel(),imsi);

			// Update subscriber registry to reflect new registration.
			/*** Pat thinks these are not used.
			if (transaction->SRIMSI().length() && transaction->SRCALLID().length()) {
				gSubscriberRegistry.addUser(transaction->SRIMSI().c_str(), transaction->SRCALLID().c_str());
			}
			***/
			LOG(INFO) << "succesful inbound handover " << tran();
			return callMachStart(new InCallMachine(tran()));
		}

		default:
			// If we get any other message before receiving the HandoverComplete, it is unrecoverable.
			if (!mReceivedHandoverComplete) {
				machineErrorMessage(LOG_NOTICE,state,l3msg,sipmsg,"waiting for Handover Complete");
				TermCause cause = TermCause::Local(L3Cause::Invalid_Handover_Message);
				return closeChannel(L3RRCause::Message_Type_Not_Compapatible_With_Protocol_State,L3_RELEASE_REQUEST,cause);
			} else {
				// This state machine may need to be modified to handle this message, whatever it is:
				machineErrorMessage(LOG_NOTICE,state,l3msg,sipmsg,"waiting for SIP Handover Complete");
				return MachineStatusOK; // Just keep going...
			}
	}
}

void startInboundHandoverMachine(TranEntry *tran)
{
	InboundHandoverMachine *ihm = new InboundHandoverMachine(tran);
	tran->lockAndStart(ihm);
}


void InCallMachine::acknowledgeDtmf()
{
	if (mDtmfSuccess) {
		 L3KeypadFacility thekey(mDtmfKey);
		 channel()->l3sendm(GSM::L3StartDTMFAcknowledge(tran()->getL3TI(),thekey));
	} else {
		LOG (CRIT) << "DTMF sending attempt failed; is any DTMF method defined?";
		channel()->l3sendm(GSM::L3StartDTMFReject(tran()->getL3TI(),L3Cause::Service_Or_Option_Not_Available));
	}
}

static bool supportRFC2833() {
	return gConfig.getBool("SIP.DTMF.RFC2833");
}
static bool supportRFC2976() {
	// Unfortunately the config option was originall misnamed, so test for both.
	return gConfig.getBool("SIP.DTMF.RFC2976") || (gConfig.defines("SIP.DTMF.RFC2967") && gConfig.getBool("SIP.DTMF.RFC2967"));
}

MachineStatus InCallMachine::machineRunState(int state, const GSM::L3Message *l3msg, const SIP::DialogMessage *sipmsg)
{
	PROCLOG2(DEBUG,state)<<LOGVAR(l3msg)<<LOGVAR(sipmsg)<<LOGVAR2("imsi",tran()->subscriber());

	switch (state) {
		case stateStart:
			// In the MOC case, this attachByImsi lets the MS start receiving MTSMS now.
			// In the MTC case, this attachByImsi was done previously.
			gMMLayer.mmAttachByImsi(channel(),tran()->subscriberIMSI());
			//channel()->setVoiceTran(tran());
			PROCLOG(DEBUG) << "setting voice tran="<<tran();
			return MachineStatusOK;

		case L3CASE_RR(ChannelModeModifyAcknowledge):
			PROCLOG(INFO) << "Ignoring duplicate GSM ChannelModeModifyAcknowledge " << *tran();
			return MachineStatusOK;

		case L3CASE_RR(AssignmentComplete):
			PROCLOG(INFO) << "Ignoring duplicate GSM AssignmentComplete " << *tran();
			return MachineStatusOK;

		case L3CASE_CC(ConnectAcknowledge):
			PROCLOG(INFO) << "Ignoring duplicate GSM Connect Acknowledge " << *tran();
			return MachineStatusOK;

		case L3CASE_CC(Connect):
			PROCLOG(INFO) << "Ignoring duplicate GSM Connect " << *tran();
			return MachineStatusOK;

		case L3CASE_CC(CallConfirmed):
			PROCLOG(INFO) << "Ignoring duplicate GSM Call Confirmed " << *tran();
			return MachineStatusOK;

		case L3CASE_CC(Alerting):
			PROCLOG(INFO) << "Ignoring duplicate GSM Alerting" << *tran();
			return MachineStatusOK;

		// Start DTMF
		// (pat) What should we do if the MS sends additional DTMF commands before the SIP side returns a response?
		// Is it guaranteed not to send another StartDTMF until we send the DTMFAcknowledge?
		case L3CASE_CC(StartDTMF): {
			const GSM::L3StartDTMF* startDtmfMsg = dynamic_cast<typeof(startDtmfMsg)>(l3msg);
			// Transalate to RFC-2976 or RFC-2833.
			mDtmfSuccess = false;
			mDtmfKey = startDtmfMsg->key().IA5();
			PROCLOG(INFO) << "DMTF key=" << mDtmfKey <<  ' ' << *tran();
			if (supportRFC2833()) {
				// TODO: Who do we need to lock here?
				bool s = getDialog()->startDTMF(mDtmfKey);
				if (!s) PROCLOG(ERR) << "DTMF RFC-28333 failed.";
				mDtmfSuccess |= s;
			}
			if (supportRFC2976()) {
				unsigned bcd = GSM::encodeBCDChar(mDtmfKey);
				getDialog()->sendInfoDtmf(bcd);
				// In this case we need to return and wait for the reply to the INFO message;
				// when it arrives we will go to the dialogDtmf state.
			} else {
				// If RFC2697 used we will send acknowledgement to MS when SIP OK arrives, otherwise send it now.
				acknowledgeDtmf();
			}
			return MachineStatusOK;
		}
		case L3CASE_SIP(dialogDtmf): {		// This is returned if RFC2697 is used, ie, SIP instead of RTP.
			if (sipmsg->sipStatusCode() == 200) {
				mDtmfSuccess = true;
			} else {
				PROCLOG(ERR) << "DTMF RFC-2967 failed with code="<<sipmsg->sipStatusCode();
			}
			acknowledgeDtmf();
			return MachineStatusOK;
		}

		// Stop DTMF RFC-2967 or RFC-2833
		case L3CASE_CC(StopDTMF): {
			if (supportRFC2833()) {
				getDialog()->stopDTMF();
			}
			// For RFC2976 there is nothing more to do - we sent one SIP INFO message and that is it.
			channel()->l3sendm(GSM::L3StopDTMFAcknowledge(tran()->getL3TI()));
			return MachineStatusOK;
		}

		case L3CASE_SIP(dialogProceeding): {
			PROCLOG(INFO) << "Ignoring duplicate SIP Proceeding " << *tran();
			return MachineStatusOK;
		}
		case L3CASE_SIP(dialogRinging): {
			PROCLOG(INFO) << "Ignoring duplicate SIP Ringing " << *tran();
			return MachineStatusOK;
		}
		case L3CASE_SIP(dialogActive): {
			PROCLOG(ERR) << "Ignoring duplicate SIP Active " << *tran();
			return MachineStatusOK;
		}
		case L3CASE_SIP(dialogBye): {
			return closeCall(dialog2ByeCause(getDialog()));
		}
		case L3CASE_SIP(dialogFail): {
			// This is MTD - Mobile Terminated Disconnect.  SIP sends a CANCEL which translates to this Fail.
			// It cant be busy at this point because we already connected.
			//devassert(! sipmsg->isBusy());
			TermCause cause = dialog2TermCause(getDialog());
			LOG(INFO) << "SIP dialogFail"<<LOGVAR(cause);
			return closeCall(cause);
		}
		case L3CASE_SIP(dialogStarted):
			devassert(0);
			return MachineStatus::QuitTran(TermCause::Local(L3Cause::Sip_Internal_Error));		// Shouldnt happen, but dont crash.

		default:
			// Note: CMServiceRequest is handled at a higher layer, see handleCommonMessages.
			return defaultMessages(state,l3msg);

	}
	return MachineStatusOK;
}

void initMTC(TranEntry *tran)
{
	tran->teSetProcedure(new MTCMachine(tran));
}


};	// namespace
