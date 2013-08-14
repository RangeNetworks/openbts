/**@file GSM/SIP Call Control -- GSM 04.08, ISDN ITU-T Q.931, SIP IETF RFC-3261, RTP IETF RFC-3550. */
/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012 Range Networks, Inc.
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


/*
	Abbreviations:
	MTC -- Mobile Terminated Connect (someone calling the mobile)
	MOC -- Mobile Originated Connect (mobile calling out)
	MTD -- Mobile Terminated Disconnect (other party hangs up)
	MOD -- Mobile Originated Disconnect (mobile hangs up)
*/


#include <Globals.h>

#include "ControlCommon.h"
#include "TransactionTable.h"
#include "MobilityManagement.h"
#include "SMSControl.h"
#include "CallControl.h"
#include "RRLPServer.h"

#include <GSMCommon.h>
#include <GSMLogicalChannel.h>
#include <GSML3RRMessages.h>
#include <GSML3MMMessages.h>
#include <GSML3CCMessages.h>
#include <GSMConfig.h>

#include <SIPInterface.h>
#include <SIPUtility.h>
#include <SIPMessage.h>
#include <SIPEngine.h>

#include <Logger.h>
#include <Reporting.h>
#undef WARNING

using namespace std;
using namespace Control;



// Forward refs.

bool callManagementDispatchGSM(TransactionEntry *transaction, GSM::LogicalChannel* LCH, const GSM::L3Message *message);



/**
	Return an even UDP port number for the RTP even/odd pair.
*/
unsigned allocateRTPPorts()
{
	const unsigned base = gConfig.getNum("RTP.Start");
	const unsigned range = gConfig.getNum("RTP.Range");
	const unsigned top = base+range;
	static Mutex lock;
	// Pick a random starting point.
	static unsigned port = base + 2*(random()%(range/2));
	unsigned retVal;
	lock.lock();
	//This is a little hacky as RTPAvail is O(n)
	do {
		retVal = port;
		port += 2;
		if (port>=top) port=base;
	} while (!gTransactionTable.RTPAvailable(retVal));
	lock.unlock();
	return retVal;
}






/**
	Force clearing on the GSM side.
	@param transaction The call transaction record.
	@param LCH The logical channel.
	@param cause The L3 abort cause.
*/
void forceGSMClearing(TransactionEntry *transaction, GSM::LogicalChannel *LCH, const GSM::L3Cause& cause)
{
	LOG(DEBUG);
	LOG(INFO) << "Q.931 state " << transaction->GSMState();
	// Already cleared?
	if (transaction->GSMState()==GSM::NullState) return;
	// Clearing not started?  Start it.
	if (!transaction->clearingGSM()) LCH->send(GSM::L3Disconnect(transaction->L3TI(),cause));
	// Force the rest.
	LOG(DEBUG);
	LCH->send(GSM::L3ReleaseComplete(transaction->L3TI()));
	LCH->send(GSM::L3ChannelRelease());
	LOG(DEBUG);
	transaction->resetTimers();
	transaction->GSMState(GSM::NullState);
	LOG(DEBUG);
	//LCH->send(GSM::RELEASE);
	//LOG(DEBUG);
}


/**
	Force clearing on the SIP side.
	@param transaction The call transaction record.
*/
void forceSIPClearing(TransactionEntry *transaction)
{
	if (transaction->deadOrRemoved()) {
		LOG(ERR) << "aborting transaction that is already removed or defunct";
		return;
	}

	LOG(DEBUG);
	SIP::SIPState state = transaction->SIPState();
	LOG(INFO) << "SIP state " << state;
	//why aren't we checking for failed here? -kurtis ; we are now. -david
	if (transaction->SIPFinished()) return;
	if (state==SIP::Active){
		//Changes state to clearing
 		transaction->MODSendBYE();
		//then cleared
		transaction->MODWaitForBYEOK();
	} else if (transaction->instigator()){ //hasn't started yet, need to cancel
 		//Changes state to canceling
		transaction->MODSendCANCEL();
		//then canceled
		transaction->MODWaitForCANCELOK();
	}
	else { //we received, respond and then don't send ok
		//changed state immediately to canceling
		transaction->MODSendERROR(NULL, 480, "Temporarily Unavailable", true);
		//then canceled
		transaction->MODWaitForERRORACK(true);
	}
}



/**
	Abort the call.  Does not remove the transaction from the table.
	@param transaction The call transaction record.
	@param LCH The logical channel.
	@param cause The L3 abort cause.
*/
void abortCall(TransactionEntry *transaction, GSM::LogicalChannel *LCH, const GSM::L3Cause& cause)
{
	LOG(DEBUG);
	if (!transaction->deadOrRemoved()) {
		LOG(INFO) << "cause: " << cause << ", transaction: " << *transaction;
		if (LCH) forceGSMClearing(transaction,LCH,cause);
		forceSIPClearing(transaction);
	}
	else {
		if (LCH) {
			LOG(ERR) << "aborting transaction that is already removed or defunct on " << *LCH;
			forceGSMClearing(transaction,LCH,cause);
		}
		else {
			LOG(ERR) << "aborting transaction that is already removed or defunct, no channel";
		}
		
	}
}


/**
	Abort the call and remove the transaction.
	@param transaction The call transaction record.
	@param LCH The logical channel.
	@param cause The L3 abort cause.
*/
void abortAndRemoveCall(TransactionEntry *transaction, GSM::LogicalChannel *LCH, const GSM::L3Cause& cause)
{
	if (transaction->deadOrRemoved()) {
		if (LCH) {
			LOG(ERR) << "aborting transaction that is already removed or defunct on " << *LCH;
		}
		else {
			LOG(ERR) << "aborting transaction that is already removed or defunct, no channel";
		}
		return;
	}
	LOG(NOTICE) << "cause: " << cause << ", transaction: " << *transaction;
	abortCall(transaction,LCH,cause);
	gTransactionTable.remove(transaction);
}




/**
	Allocate a TCH and clean up any failure.
	@param DCCH The DCCH that will be used to send the assignment.
	@return A pointer to the TCH or NULL on failure.
*/
GSM::TCHFACCHLogicalChannel *allocateTCH(GSM::LogicalChannel *DCCH)
{
	// Get TCH will open the channel.
	GSM::TCHFACCHLogicalChannel *TCH = gBTS.getTCH();
	if (!TCH) {
		LOG(WARNING) << "congestion, no TCH available for assignment";
		// Cause 0x16 is "congestion".
		DCCH->send(GSM::L3CMServiceReject(0x16));
		DCCH->send(GSM::L3ChannelRelease());
	}
	return TCH;
}





/**
	Assign a full rate traffic channel and clean up any failures.
	@param DCCH The DCCH on which to send the assignment.
	@param TCH The TCH to be assigned.
	@bool True on successful transfer.
*/
bool assignTCHF(TransactionEntry *transaction, GSM::LogicalChannel *DCCH, GSM::TCHFACCHLogicalChannel *TCH)
{
	TCH->open();
	TCH->setPhy(*DCCH);

	// We retry this loop in case there are stray messages in the channel.
	// On some phones, we see repeated Call Confirmed messages on MTC.

	GSM::Z100Timer retry(GSM::T3101ms-1000);
	retry.set();
	while (!retry.expired()) {

		// Send the assignment.
		transaction->channel(TCH);
		LOG(DEBUG) << "updated transaction " << *transaction;
		LOG(INFO) << "sending AssignmentCommand for " << *TCH << " on " << *DCCH;
		// FIXME - We should probably be setting the initial power here.
		DCCH->send(GSM::L3AssignmentCommand(TCH->channelDescription(),GSM::L3ChannelMode(GSM::L3ChannelMode::SpeechV1)));

		// This read is SUPPOSED to time out if the assignment was successful.
		// Pad the timeout just in case there's a large latency somewhere.
		GSM::L3Frame *result = DCCH->recv(GSM::T3107ms+2000);
		if (!result) {
			LOG(INFO) << "sucessful assignment; exiting normally";
			DCCH->send(GSM::HARDRELEASE);
			return true;
		}

		// If we got here, the assignment failed, or there was a message backlog in L3.
		GSM::L3Message *msg = parseL3(*result);
		if (!msg) { LOG(NOTICE) << "waiting for assignment complete, received unparsed L3 frame " << *result; }
		delete result;
		if (!msg) continue;
		LOG(NOTICE) << "waiting for assignment complete, received " << *msg;
		callManagementDispatchGSM(transaction,DCCH,msg);
	}

	// Turn off the TCH.
	TCH->send(GSM::RELEASE);

	// RR Cause 0x04 -- "abnormal release, no activity on the radio path"
	DCCH->send(GSM::L3ChannelRelease(0x04));

	// Dissociate channel from the transaction.
	// The tranaction no longer has a channel.
	transaction->channel(NULL);
	
	// Shut down the SIP side of the call.
	forceSIPClearing(transaction);

	// Indicate failure.
	return false;
}




/**
	Process a message received from the phone during a call.
	This function processes all deviations from the "call connected" state.
	For now, we handle call clearing and politely reject everything else.
	@param transaction The transaction record for this call.
	@param LCH The logical channel for the transaction.
	@param message A pointer to the receiver message.
	@return true If the call has been cleared and the channel released.
*/
bool callManagementDispatchGSM(TransactionEntry *transaction, GSM::LogicalChannel* LCH, const GSM::L3Message *message)
{
	if (message) { LOG(DEBUG) << "from " << transaction->subscriber() << " message " << *message; }

	// FIXME -- This dispatch section should be something more efficient with PD and MTI swtiches.

	// Actually check state before taking action.
	//if (transaction->SIPState()==SIP::Cleared) return true;
	//if (transaction->GSMState()==GSM::NullState) return true;

	// Call connection steps.

	// Connect Acknowledge
	if (dynamic_cast<const GSM::L3ConnectAcknowledge*>(message)) {
		LOG(INFO) << "GSM Connect Acknowledge " << *transaction;
		transaction->resetTimers();
		transaction->GSMState(GSM::Active);
		return false;
	}

	// Connect
	// GSM 04.08 5.2.2.5 and 5.2.2.6
	if (dynamic_cast<const GSM::L3Connect*>(message)) {
		LOG(INFO) << "GSM Connect " << *transaction;
		transaction->resetTimers();
		transaction->GSMState(GSM::Active);
		return false;
	}

	// Call Confirmed
	// GSM 04.08 5.2.2.3.2
	// "Call Confirmed" is the GSM MTC counterpart to "Call Proceeding"
	if (dynamic_cast<const GSM::L3CallConfirmed*>(message)) {
		LOG(INFO) << "GSM Call Confirmed " << *transaction;
		transaction->resetTimer("303");
		transaction->setTimer("301");
		transaction->GSMState(GSM::MTCConfirmed);
		return false;
	}

	// Alerting
	// GSM 04.08 5.2.2.3.2
	if (dynamic_cast<const GSM::L3Alerting*>(message)) {
		LOG(INFO) << "GSM Alerting " << *transaction;
		transaction->resetTimer("310");
		transaction->setTimer("301");
		transaction->GSMState(GSM::CallReceived);
		return false;
	}

	// Call clearing steps.
	// Good diagrams in GSM 04.08 7.3.4

	// FIXME -- We should be checking TI values against the transaction object.

	// Disconnect (1st step of MOD)
	// GSM 04.08 5.4.3.2
	if (const GSM::L3Disconnect* disc = dynamic_cast<const GSM::L3Disconnect*>(message)) {
		LOG(INFO) << "GSM Disconnect " << *transaction;
		gReports.incr("OpenBTS.GSM.CC.MOD.Disconnect");
		bool early = transaction->GSMState() != GSM::Active;
		bool normal = (disc->cause().cause() <= 0x10);
		if (!normal) {
			LOG(NOTICE) << "abnormal terminatation: " << *disc;
		}
		/* late RLLP request */
		if (normal && !early && gConfig.getBool("Control.Call.QueryRRLP.Late")) {
			// Query for RRLP
			if (!sendRRLP(transaction->subscriber(), LCH)) {
				LOG(INFO) << "RRLP request failed";
			}
		}
		transaction->resetTimers();
		LCH->send(GSM::L3Release(transaction->L3TI()));
		transaction->setTimer("308");
		transaction->GSMState(GSM::ReleaseRequest);
		//bug #172 fixed
		if (transaction->SIPState()==SIP::Active){
			transaction->MODSendBYE();
			transaction->MODWaitForBYEOK();
		}
		else { //this is the end if the call isn't setup yet in the SIP domain
			if (transaction->instigator()){ //if we instigated the call, send a cancel
				transaction->MODSendCANCEL();
				transaction->MODWaitForCANCELOK();
				//if we cancel the call, Switch might send 487 Request Terminated
				//listen for that
				transaction->MODWaitFor487();
				// TODO: Asterisk fires off two SIP packets, OK and 487. We may not receive them
				//       in that order. We will want to use the code below to eat both of the
				//       packets, but accept them in any order.
				/*vector<unsigned> valid(2);
				valid.push_back(200);
				valid.push_back(487);
				transaction->MODWaitForResponse(&valid);
				transaction->MODWaitForResponse(&valid);*/
			}
			else { //if we received it, send a 4** instead
				//transaction->MODSendERROR(NULL, 480, "Temporarily Unavailable", true);
				transaction->MODSendERROR(NULL, 486, "Busy Here", true);
				transaction->MODWaitForERRORACK(true);
			}
			//transaction->GSMState(GSM::NullState);
			//return true;
		}
		return false;
	}

	// Release (2nd step of MTD)
	if (const GSM::L3Release *rls = dynamic_cast<const GSM::L3Release*>(message)) {
		LOG(INFO) << "GSM Release " << *transaction;
		gReports.incr("OpenBTS.GSM.CC.MTD.Release");
		if (rls->haveCause() && (rls->cause().cause() > 0x10)) {
			LOG(NOTICE) << "abnormal terminatation: " << *rls;
		}
		/* late RLLP request */
		if (gConfig.getBool("Control.Call.QueryRRLP.Late")) {
			// Query for RRLP
			if (!sendRRLP(transaction->subscriber(), LCH)) {
				LOG(INFO) << "RRLP request failed";
			}
		}
		transaction->resetTimers();
		LCH->send(GSM::L3ReleaseComplete(transaction->L3TI()));
		LCH->send(GSM::L3ChannelRelease());
		transaction->GSMState(GSM::NullState);
		transaction->MTDSendBYEOK();
		return true;
	}

	// Release Complete (3nd step of MOD)
	// GSM 04.08 5.4.3.4
	if (dynamic_cast<const GSM::L3ReleaseComplete*>(message)) {
		LOG(INFO) << "GSM Release Complete " << *transaction;
		transaction->resetTimers();
		LCH->send(GSM::L3ChannelRelease());
		transaction->GSMState(GSM::NullState);
		return true;
	}

	// IMSI Detach -- the phone is shutting off.
	if (const GSM::L3IMSIDetachIndication* detach = dynamic_cast<const GSM::L3IMSIDetachIndication*>(message)) {
		// The IMSI detach procedure will release the LCH.
		LOG(INFO) << "GSM IMSI Detach " << *transaction;
		IMSIDetachController(detach,LCH);
		forceSIPClearing(transaction);
		return true;
	}

	// Start DTMF
	// Transalate to RFC-2967 or RFC-2833.
	if (const GSM::L3StartDTMF* startDTMF = dynamic_cast<const GSM::L3StartDTMF*>(message)) {
		char key = startDTMF->key().IA5();
		LOG(INFO) << "DMTF key=" << key <<  ' ' << *transaction;
		bool success = false;
		if (gConfig.defines("SIP.DTMF.RFC2833")) {
			bool s = transaction->startDTMF(key);
			if (!s) LOG(ERR) << "DTMF RFC-28333 failed.";
			success |= s;
		}
		if (gConfig.defines("SIP.DTMF.RFC2967")) {
			unsigned bcd = GSM::encodeBCDChar(key);
			bool s = transaction->sendINFOAndWaitForOK(bcd);
			if (!s) LOG(ERR) << "DTMF RFC-2967 failed.";
			success |= s;
		}
		if (success) {
			 LCH->send(GSM::L3StartDTMFAcknowledge(transaction->L3TI(),startDTMF->key()));
		} else {
			LOG (CRIT) << "DTMF sending attempt failed; is any DTMF method defined?";
			// Cause 0x3f means "service or option not available".
			LCH->send(GSM::L3StartDTMFReject(transaction->L3TI(),0x3f));
		}
		return false;
	}

	// Stop DTMF
	// RFC-2967 or RFC-2833
	if (dynamic_cast<const GSM::L3StopDTMF*>(message)) {
		transaction->stopDTMF();
		LCH->send(GSM::L3StopDTMFAcknowledge(transaction->L3TI()));
		return false;
	}

	// CM Service Request
	if (const GSM::L3CMServiceRequest *cmsrq = dynamic_cast<const GSM::L3CMServiceRequest*>(message)) {
		// SMS submission?  The rest will happen on the SACCH.
		if (cmsrq->serviceType().type() == GSM::L3CMServiceType::ShortMessage) {
			LOG (INFO) << "in call SMS submission on " << *LCH;
			InCallMOSMSStarter(transaction);
			LCH->send(GSM::L3CMServiceAccept());
			return false;
		}
		// For now, we are rejecting anything else.
		LOG(NOTICE) << "cannot accept additional CM Service Request from " << transaction->subscriber();
		// Cause 0x20 means "serivce not supported".
		LCH->send(GSM::L3CMServiceReject(0x20));
		return false;
	}

//#if 0

//This needs to work, but putting it in causes heap corruption.

	// Status
	// If we get this message, is is probably carrying an error code.
	if (const GSM::L3CCStatus* status = dynamic_cast<const GSM::L3CCStatus*>(message)) {
		LOG(NOTICE) << "unsolicited status message: " << *status;
		unsigned callState = status->callState().callState();
		// See GSM 04.08 Table 10.5.117.
		if (callState>10) {
			// Just cancel on the SIP side.
			// FIXME -- We should really try to translate the error cause.
			transaction->MODSendCANCEL();
			transaction->resetTimers();
			LCH->send(GSM::L3Release(transaction->L3TI()));
			transaction->setTimer("308");
			transaction->GSMState(GSM::ReleaseRequest);
			return true;
		}
	}
//#endif

	// We don't process Assignment Failurehere, but catch it to avoid misleading log message.
	if (dynamic_cast<const GSM::L3AssignmentFailure*>(message)) {
		return false;
	}

	if (dynamic_cast<const GSM::L3CipheringModeComplete*>(message)) {
		LOG(DEBUG) << "received Ciphering Mode Complete on " << *LCH << " for " << transaction->subscriber();
		// Although the spec (04.08 3.4.7) says you can start ciphering the downlink at this time,
		// it also says you can start when you successfully decrypt an uplink layer 2 frame,
		// which is what we do.
		return false;
	}

	// Stubs for unsupported features.
	// We need to answer the handset so it doesn't hang.

	// Hold
	if (dynamic_cast<const GSM::L3Hold*>(message)) {
		LOG(NOTICE) << "rejecting hold request from " << transaction->subscriber();
		// Default cause is 0x3f, option not available
		LCH->send(GSM::L3HoldReject(transaction->L3TI(),0x3f));
		return false;
	}

	if (message) {
		LOG(NOTICE) << "no support for message " << *message << " from " << transaction->subscriber();
	} else {
		LOG(NOTICE) << "no support for unrecognized message from " << transaction->subscriber();
	}


	// If we got here, we're ignoring the message.
	return false;
}






/**
	Update vocoder data transfers in both directions.
	@param transaction The transaction object for this call.
	@param TCH The traffic channel for this call.
	@return True if anything was transferred.
*/
bool updateCallTraffic(TransactionEntry *transaction, GSM::TCHFACCHLogicalChannel *TCH)
{
	bool activity = false;

	// Transfer in the downlink direction (RTP->GSM).
	// Blocking call.  On average returns 1 time per 20 ms.
	// Returns non-zero if anything really happened.
	// Make the rxFrame buffer big enough for G.711.
	unsigned char rxFrame[160];
	if (transaction->rxFrame(rxFrame)) {
		activity = true;
		TCH->sendTCH(rxFrame);
	}

	// Transfer in the uplink direction (GSM->RTP).
	// Flush FIFO to limit latency.
	unsigned maxQ = gConfig.getNum("GSM.MaxSpeechLatency");
	while (TCH->queueSize()>maxQ) delete[] TCH->recvTCH();
	if (unsigned char *txFrame = TCH->recvTCH()) {
		activity = true;
		// Send on RTP.
		transaction->txFrame(txFrame);
		delete[] txFrame;
	}

	// Return a flag so the caller will know if anything transferred.
	return activity;
}




/**
	Check GSM signalling.
	Can block for up to 52 GSM L1 frames (240 ms) because LCH::send is blocking.
	@param transaction The call's TransactionEntry.
	@param LCH The call's logical channel (TCH/FACCH or SDCCH).
	@return true If the call was cleared, but the transaction is still there.
*/
bool updateGSMSignalling(TransactionEntry *transaction, GSM::LogicalChannel *LCH, unsigned timeout=0)
{
	if (transaction->GSMState()==GSM::NullState) return true;

	// Any Q.931 timer expired?
	if (transaction->anyTimerExpired()) {
		// Cause 0x66, "recover on timer expiry"
		abortCall(transaction,LCH,GSM::L3Cause(0x66));
		return true;
	}

	// Look for a control message from MS side.
	if (GSM::L3Frame *l3 = LCH->recv(timeout)) {
		// Check for lower-layer error.
		if (l3->primitive() == GSM::ERROR) return true;
		// Parse and dispatch.
		GSM::L3Message *l3msg = parseL3(*l3);
		delete l3;
		bool cleared = false;
		if (l3msg) {
			LOG(DEBUG) << "received " << *l3msg;
			cleared = callManagementDispatchGSM(transaction, LCH, l3msg);
			delete l3msg;
		}
		return cleared;
	}

	// If we are here, we have timed out, but assume the call is still running.
	return false;
}



/**
	Check SIP signalling.
	@param transaction The call's TransactionEntry.
	@param LCH The call's GSM logical channel (TCH/FACCH or SDCCH).
	@param GSMCleared True if the call is already cleared in the GSM domain.
	@return true If the call is cleared in the SIP domain.
*/
bool updateSIPSignalling(TransactionEntry *transaction, GSM::LogicalChannel *LCH, bool GSMCleared)
{

	// The main purpose of this code is to initiate disconnects from the SIP side.

	if (transaction->SIPFinished()) return true;

	bool GSMClearedOrClearing = GSMCleared || transaction->clearingGSM();

	//only checking for Clearing because the call is active at this state. Should not cancel
	if (transaction->MTDCheckBYE() == SIP::MTDClearing) {
		LOG(DEBUG) << "got SIP BYE " << *transaction;
		if (!GSMClearedOrClearing) {
			// Initiate clearing in the GSM side.
			LCH->send(GSM::L3Disconnect(transaction->L3TI()));
			transaction->setTimer("305");
			transaction->GSMState(GSM::DisconnectIndication);
		} else {
			// GSM already cleared?
			// Ack the BYE and end the call.
			transaction->MTDSendBYEOK();
		}
	}

	return (transaction->SIPFinished());
}



/**
	Check SIP and GSM signalling.
	Can block for up to 52 GSM L1 frames (240 ms) because LCH::send is blocking.
	@param transaction The call's TransactionEntry.
	@param LCH The call's logical channel (TCH/FACCH or SDCCH).
	@return true If the call is cleared in both domains.
*/
bool updateSignalling(TransactionEntry *transaction, GSM::LogicalChannel *LCH, unsigned timeout=0)
{

	bool GSMCleared = (updateGSMSignalling(transaction,LCH,timeout));
	bool SIPFinished = updateSIPSignalling(transaction,LCH,GSMCleared);
	return GSMCleared && SIPFinished;
}




bool outboundHandoverTransfer(TransactionEntry* transaction, GSM::TCHFACCHLogicalChannel *TCH)
{
	// By returning true, this function indicates to its caller that the call is cleared
	// and no longer needs a channel on this BTS.

	// In this method, we are "BS1" in the ladder diagram.
	// BS2 has alrady accepted the handover request.

	// Send the handover command.
	TCH->send(GSM::L3HandoverCommand(
		transaction->outboundCell(),
		transaction->outboundChannel(),
		transaction->outboundReference(),
		transaction->outboundPowerCmd(),
		transaction->outboundSynch()
		));

	// Start a timer for T3103, the handover failure timer.
	GSM::Z100Timer T3103(gConfig.getNum("GSM.Timer.T3103"));
	T3103.set();

	// The next step for the MS is to send Handover Access to BS2.
	// The next step for us is to wait for the Handover Complete message
	// and see that the phone doesn't come back to us.
	// BS2 is doing most of the work now.
	// We will get a handover complete once it's over, but we don't really need it.

	// Q: What about transferring audio packets?
	// A: There should not be any after we send the Handover Command.

	// Get the response.
	// This is supposed to time out on successful handover, similar to the early assignment channel transfer..
	GSM::L3Frame *result = TCH->recv(T3103.remaining());
	if (result) {
		// If we got here, the handover failed and we just keep running the call.
		LOG(NOTICE) << "failed handover, received " << *result;
		delete result;
		// Restore the call state.
		transaction->GSMState(GSM::Active);
		return false;
	}

	// If the phone doesn't come back, either the handover succeeded or
	// the phone dropped the connection.  Either way, we are clearing the call.

	// Invalidate local cache entry for this IMSI in the subscriber registry.
	string imsi = string("IMSI").append(transaction->subscriber().digits());
	gSubscriberRegistry.removeUser(imsi.c_str());

	LOG(INFO) "timeout following outbound handover; exiting normally";
	TCH->send(GSM::HARDRELEASE);
	return true;
}


/**
	Poll for activity while in a call.
	Sleep if needed to prevent fast spinning.
	Will block for up to 250 ms.
	@param transaction The call's TransactionEntry.
	@param TCH The call's TCH+FACCH.
	@return true If the call was cleared.
*/
bool pollInCall(TransactionEntry *transaction, GSM::TCHFACCHLogicalChannel *TCH)
{

	// See if the radio link disappeared.
	if (TCH->radioFailure()) {
		LOG(NOTICE) << "radio link failure, dropped call";
		gReports.incr("OpenBTS.GSM.CC.DroppedCalls");
		forceSIPClearing(transaction);
		return true;
	}

	// Process pending SIP and GSM signalling.
	// If this returns true, it means the call is fully cleared.
	if (updateSignalling(transaction,TCH)) return true;

	// Check for outbound handover.
	if (transaction->GSMState() == GSM::HandoverOutbound)
		return outboundHandoverTransfer(transaction,TCH);

	// Did an outside process request a termination?
	if (transaction->terminationRequested()) {
		// Cause 25 is "pre-emptive clearing".
		abortCall(transaction,TCH,25);
		// Do the hard release to short-cut the timers.
		// If something else is requesting termination,
		// it's probably because we need the channel for
		// something else (like an emegency call) right away.
		//TCH->send(GSM::HARDRELEASE);
		return true;
	}

	// Transfer vocoder data.
	// If anything happened, then the call is still up.
	// This is a blocking call, blocking 20 ms on average.
	if (updateCallTraffic(transaction,TCH)) return false;

	// If nothing happened, sleep so we don't burn up the CPU cycles.
	msleep(50);
	return false;
}


/**
	Pause for a given time while managing the connection.
	Returns on timeout or call clearing.
	Used for debugging to simulate ringing at terminating end.
	@param transaction The transaction record for the call.
	@param TCH The TCH+FACCH sed for this call.
	@param waitTime_ms The maximum time to wait, in ms.
	@return true If the call is cleared during the wait.
*/
bool waitInCall(TransactionEntry *transaction, GSM::TCHFACCHLogicalChannel *TCH, unsigned waitTime_ms)
{
	Timeval targetTime(waitTime_ms);
	LOG(DEBUG);
	while (!targetTime.passed()) {
		if (pollInCall(transaction,TCH)) return true;
	}
	return false;
}



/**
	This is the standard call manangement loop, regardless of the origination type.
	This function returns when the call is cleared and the channel is released.
	@param transaction The transaction record for this call, will be cleared on exit.
	@param TCH The TCH+FACCH for the call.
*/
void Control::callManagementLoop(TransactionEntry *transaction, GSM::TCHFACCHLogicalChannel* TCH)
{
	LOG(INFO) << " call connected " << *transaction;
	if (gConfig.getBool("GSM.Cipher.Encrypt")) {
		int encryptionAlgorithm = gTMSITable.getPreferredA5Algorithm(transaction->subscriber().digits());
		if (!encryptionAlgorithm) {
			LOG(DEBUG) << "A5/3 and A5/1 not supported: NOT sending Ciphering Mode Command on " << *TCH << " for " << transaction->subscriber();
		} else if (TCH->decryptUplink_maybe(transaction->subscriber().digits(), encryptionAlgorithm)) {
			// send Ciphering Mode Command
			// start reception in new mode (GSM 04.08, 3.4.7)
			// The spec says to start decrypting uplink at this time, but that would cause us to
			// start decrypting before the Ciphering Mode Command is acknowledged, so we start
			// maybe decrypting - try decoding without decrypting, and when a frame comes along
			// that fails, we try decrypting, and if that passes than we start decrypting everything.
			LOG(DEBUG) << "sending Ciphering Mode Command on " << *TCH << " for " << transaction->subscriber();
			TCH->send(GSM::L3CipheringModeCommand(
				GSM::L3CipheringModeSetting(true, encryptionAlgorithm),
				GSM::L3CipheringModeResponse(false)));
		} else {
			LOG(DEBUG) << "no ki: NOT sending Ciphering Mode Command on " << *TCH << " for " << transaction->subscriber();
		}
	}
	gReports.incr("OpenBTS.GSM.CC.CallMinutes");
	// poll everything until the call is finished
	// A rough count of frames.
	size_t fCount = 0;
	while (!pollInCall(transaction,TCH)) {

		if (transaction->deadOrRemoved()) {
			LOG(ERR) << "attempting to use a defunct transaction";
			TCH->send(GSM::L3ChannelRelease());
			return;
		}

		fCount++;
		// On average, pollInCall blocks for 20 ms.
		// Every minute, reset the watchdog timer.
		if ((fCount%(60*50))==0) {
			LOG(DEBUG) << fCount << " cycles of call management loop; resetting watchdog";
			gResetWatchdog();
			gReports.incr("OpenBTS.GSM.CC.CallMinutes");
		}
	}
	gTransactionTable.remove(transaction);
}




/**
	This function starts MOC on the SDCCH to the point of TCH assignment. 
	@param req The CM Service Request that started all of this.
	@param LCH The logical used to initiate call setup.
*/
void Control::MOCStarter(const GSM::L3CMServiceRequest* req, GSM::LogicalChannel *LCH)
{
	assert(LCH);
	assert(req);
	LOG(INFO) << *req;

	// Determine if very early assignment already happened.
	bool veryEarly = (LCH->type()==GSM::FACCHType);

	// If we got a TMSI, find the IMSI.
	// Note that this is a copy, not a reference.
	GSM::L3MobileIdentity mobileID = req->mobileID();
	resolveIMSI(mobileID,LCH);


	// FIXME -- At this point, verify the that subscriber has access to this service.
	// If the subscriber isn't authorized, send a CM Service Reject with
	// cause code, 0x41, "requested service option not subscribed",
	// followed by a Channel Release with cause code 0x6f, "unspecified".
	// Otherwise, proceed to the next section of code.
	// For now, we are assuming that the phone won't make a call if it didn't
	// get registered.

	// Allocate a TCH for the call, if we don't have it already.
	GSM::TCHFACCHLogicalChannel *TCH = NULL;
	if (!veryEarly) {
		TCH = allocateTCH(dynamic_cast<GSM::LogicalChannel*>(LCH));
		// It's OK to just return on failure; allocateTCH cleaned up already,
		// and the SIP side and transaction record don't exist yet.
		if (TCH==NULL) return;
	}

	// Let the phone know we're going ahead with the transaction.
	LOG(INFO) << "sending CMServiceAccept";
	LCH->send(GSM::L3CMServiceAccept());

	// Get the Setup message.
	// GSM 04.08 5.2.1.2
	GSM::L3Message* msg_setup = getMessage(LCH);

	// Check for abort, if so close and cancel
	if (const GSM::L3CMServiceAbort *cmsab = dynamic_cast<const GSM::L3CMServiceAbort*>(msg_setup)) {
		LOG(INFO) << "received CMServiceAbort, closing channel and clearing";
		//SIP Engine not started, just close the channel and exit
		LCH->send(GSM::L3ChannelRelease());
		delete cmsab;
		return;
	}

	const GSM::L3Setup *setup = dynamic_cast<const GSM::L3Setup*>(msg_setup);
	if (!setup) {
		if (msg_setup) {
			LOG(WARNING) << "Unexpected message " << *msg_setup;
			delete msg_setup;
		}
		throw UnexpectedMessage();
	}
	gReports.incr("OpenBTS.GSM.CC.MOC.Setup");
	
	/* early RLLP request */
	/* this seems to need to be sent after initial call setup
	   -kurtis */
	if (gConfig.getBool("Control.Call.QueryRRLP.Early")) {
		// Query for RRLP
		if (!sendRRLP(mobileID, LCH)) {
			LOG(INFO) << "RRLP request failed";
		}
	}

	LOG(INFO) << *setup;
	// Pull out the L3 short transaction information now.
	// See GSM 04.07 11.2.3.1.3.
	// Set the high bit, since this TI came from the MS.
	unsigned L3TI = setup->TI() | 0x08;
	if (!setup->haveCalledPartyBCDNumber()) {
		// FIXME -- This is quick-and-dirty, not following GSM 04.08 5.
		LOG(WARNING) << "MOC setup with no number";
		// Cause 0x60 "Invalid mandatory information"
		LCH->send(GSM::L3ReleaseComplete(L3TI,0x60));
		LCH->send(GSM::L3ChannelRelease());
		// The SIP side and transaction record don't exist yet.
		// So we're done.
		delete msg_setup;
		return;
	}

	LOG(DEBUG) << "SIP start engine";
	// Get the users sip_uri by pulling out the IMSI.
	//const char *IMSI = mobileID.digits();
	// Pull out Number user is trying to call and use as the sip_uri.
	const char *bcdDigits = setup->calledPartyBCDNumber().digits();

	// Create a transaction table entry so the TCH controller knows what to do later.
	// The transaction on the TCH will be a continuation of this one.
	TransactionEntry *transaction = new TransactionEntry(
		gConfig.getStr("SIP.Proxy.Speech").c_str(),
		mobileID,
		LCH,
		req->serviceType(),
		L3TI,
		setup->calledPartyBCDNumber());
	LOG(DEBUG) << "transaction: " << *transaction;
	gTransactionTable.add(transaction);

	// At this point, we have enough information start the SIP call setup.
	// We also have a SIP side and a transaction that will need to be
	// cleaned up on abort or clearing.

	// Now start a call by contacting asterisk.
	// Engine methods will return their current state.	
	// The remote party will start ringing soon.
	LOG(DEBUG) << "starting SIP (INVITE) Calling "<<bcdDigits;
	unsigned basePort = allocateRTPPorts();
	transaction->MOCSendINVITE(bcdDigits,gConfig.getStr("SIP.Local.IP").c_str(),basePort,SIP::RTPGSM610);
	LOG(DEBUG) << "transaction: " << *transaction;

	// Once we can start SIP call setup, send Call Proceeding.
	LOG(INFO) << "Sending Call Proceeding";
	LCH->send(GSM::L3CallProceeding(L3TI));
	transaction->GSMState(GSM::MOCProceeding);
	// Finally done with the Setup message.
	delete msg_setup;

	// The transaction is moving on to the MOCController.
	// If we need a TCH assignment, we do it here.
	LOG(DEBUG) << "transaction: " << *transaction;
	if (veryEarly) {
		// For very early assignment, we need a mode change.
		static const GSM::L3ChannelMode mode(GSM::L3ChannelMode::SpeechV1);
		LCH->send(GSM::L3ChannelModeModify(LCH->channelDescription(),mode));
		GSM::L3Message *msg_ack = getMessage(LCH);
		const GSM::L3ChannelModeModifyAcknowledge *ack =
			dynamic_cast<GSM::L3ChannelModeModifyAcknowledge*>(msg_ack);
		if (!ack) {
			// FIXME -- We need this in a loop calling the GSM disptach function.
			if (msg_ack) {
				LOG(WARNING) << "Unexpected message " << *msg_ack;
				delete msg_ack;
			}
			throw UnexpectedMessage(transaction->ID());
		}
		// Cause 0x06 is "channel unacceptable"
		bool modeOK = (ack->mode()==mode);
		delete msg_ack;
		if (!modeOK) return abortAndRemoveCall(transaction,LCH,GSM::L3Cause(0x06));
		MOCController(transaction,dynamic_cast<GSM::TCHFACCHLogicalChannel*>(LCH));
	} else {
		// For late assignment, send the TCH assignment now.
		// This dispatcher on the next channel will continue the transaction.
		assignTCHF(transaction,LCH,TCH);
	}
}





/**
	Continue MOC process on the TCH.
	@param transaction The call state and SIP interface.
	@param TCH The traffic channel to be used.
*/
void Control::MOCController(TransactionEntry *transaction, GSM::TCHFACCHLogicalChannel* TCH)
{
	assert(transaction);
	assert(TCH);
	if (transaction->deadOrRemoved()) {
		LOG(ERR) << "dead or defunct transaciton on " << *TCH;
		TCH->send(GSM::L3ChannelRelease());
		return;
	}
	LOG(DEBUG) << "transaction: " << *transaction;
	unsigned L3TI = transaction->L3TI();
	assert(L3TI>7);


	// Look for RINGING or OK from the SIP side.
	// There's a T310 running on the phone now.
	// The phone will initiate clearing if it expires.
	// FIXME -- We should also have a SIP.Timer.B timeout on this end.
	SIP::SIPState prevState = transaction->SIPState();
	while (transaction->GSMState()!=GSM::CallReceived) {

		if (transaction->deadOrRemoved()) {
			LOG(ERR) << "attempting to use a defunct transaction";
			TCH->send(GSM::L3ChannelRelease());
			return;
		}

		if (updateGSMSignalling(transaction,TCH)) return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x15));
		if (transaction->clearingGSM()) return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x7F));

		LOG(INFO) << "wait for Ringing or OK";
		SIP::SIPState state = transaction->MOCCheckForOK();
		LOG(DEBUG) << "SIP state="<<state;
		switch (state) {
			case SIP::Busy:
				LOG(INFO) << "SIP:Busy, abort";
				transaction->MOCSendACK();
				forceGSMClearing(transaction,TCH,GSM::L3Cause(0x11));
				gTransactionTable.remove(transaction);
				return;
			case SIP::Fail:
				LOG(NOTICE) << "SIP:Fail, abort";
				return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x7F));
			case SIP::Ringing:
				LOG(INFO) << "SIP:Ringing, send Alerting and move on";
				TCH->send(GSM::L3Alerting(L3TI));
				transaction->GSMState(GSM::CallReceived);
				break;
			case SIP::Active:
				LOG(DEBUG) << "SIP:Active, move on";
				transaction->GSMState(GSM::CallReceived);
				break;
			case SIP::Proceeding:
				if (state != prevState) {
					LOG(DEBUG) << "SIP::Proceeding, state change, sending L3 progress";
					TCH->send(GSM::L3Progress(L3TI));
				}
				break;
			case SIP::Timeout:
				// This is CRIT instead of ALERT because it could also be due to packet loss.
				LOG(CRIT) << "MOC INVITE Timed out. Is SIP.Proxy.Speech (" << gConfig.getStr("SIP.Proxy.Speech") << ") configured correctly?";
				state = transaction->MOCResendINVITE();
				break;
			default:
				LOG(NOTICE) << "SIP unexpected state " << state;
				break;
		}
		prevState = state;
	}

	// There's a question here of what entity is generating the "patterns"
	// (ringing, busy signal, etc.) during call set-up.  For now, we're ignoring 
	// that question and hoping the phone will make its own ringing pattern.


	// Wait for the SIP session to start.
	// There's a timer on the phone that will initiate clearing if it expires.
	LOG(INFO) << "wait for SIP OKAY";
	SIP::SIPState state = transaction->SIPState();
	while (state!=SIP::Active) {

		if (transaction->deadOrRemoved()) {
			LOG(ERR) << "attempting to use a defunct transaction";
			TCH->send(GSM::L3ChannelRelease());
			return;
		}

		LOG(DEBUG) << "wait for SIP session start";
		state = transaction->MOCCheckForOK();
		LOG(DEBUG) << "SIP state "<< state;

		// check GSM state
		if (updateGSMSignalling(transaction,TCH)) return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x15));
		if (transaction->clearingGSM()) return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x7F));

		// parse out SIP state
		switch (state) {
			case SIP::Busy:
				// Should this be possible at this point?
				LOG(INFO) << "SIP:Busy, abort";
				return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x11));
			case SIP::Fail:
				LOG(INFO) << "SIP:Fail, abort";
				return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x7F));
			case SIP::Proceeding:
				LOG(DEBUG) << "SIP:Proceeding, NOT sending progress";
				//TCH->send(GSM::L3Progress(L3TI));
				break;
			// For these cases, do nothing.
			case SIP::Timeout:
				// FIXME We should abort if this happens too often.
				// For now, we are relying on the phone, which may have bugs of its own.
			case SIP::Active:
			default:
				break;
		}
	} 
	
	// Let the phone know the call is connected.
	LOG(INFO) << "sending Connect to handset";
	TCH->send(GSM::L3Connect(L3TI));
	transaction->setTimer("313");
	transaction->GSMState(GSM::ConnectIndication);

	// The call is open.
	transaction->MOCInitRTP();
	transaction->MOCSendACK();

	// FIXME -- We need to watch for a repeated OK in case the ACK got lost.

	// Get the Connect Acknowledge message.
	while (transaction->GSMState()!=GSM::Active) {

		if (transaction->deadOrRemoved()) {
			LOG(ERR) << "attempting to use a defunct transaction";
			TCH->send(GSM::L3ChannelRelease());
			return;
		}

		LOG(DEBUG) << "MOC Q.931 state=" << transaction->GSMState();
		if (updateGSMSignalling(transaction,TCH,T313ms)) return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x7F));
	}

	// At this point, everything is ready to run the call.
	callManagementLoop(transaction,TCH);

	// The radio link should have been cleared with the call.
	// So just return.
}




void Control::MTCStarter(TransactionEntry *transaction, GSM::LogicalChannel *LCH)
{
	assert(LCH);
	LOG(INFO) << "MTC on " << LCH->type() << " transaction: "<< *transaction;

	// Determine if very early assigment already happened.
	bool veryEarly = false;
	if (LCH->type()==GSM::FACCHType) veryEarly=true;

	/* early RLLP request */
	if (gConfig.getBool("Control.Call.QueryRRLP.Early")) {
		// Query for RRLP
		if (!sendRRLP(transaction->subscriber(), LCH)) {
			LOG(INFO) << "RRLP request failed";
		}
	}

	// Allocate a TCH for the call.
	GSM::TCHFACCHLogicalChannel *TCH = NULL;
	if (!veryEarly) {
		TCH = allocateTCH(dynamic_cast<GSM::LogicalChannel*>(LCH));
		// It's OK to just return on failure; allocateTCH cleaned up already.
		// The orphaned transaction will be cleared automatically later.
		if (TCH==NULL) return;
	}


	// Get transaction identifiers.
	// This transaction was created by the SIPInterface when it
	// processed the INVITE that started this call.
	unsigned L3TI = transaction->L3TI();
	assert(L3TI<7);

	// GSM 04.08 5.2.2.1
	LOG(INFO) << "sending GSM Setup to call " << transaction->calling();
	LCH->send(GSM::L3Setup(L3TI,GSM::L3CallingPartyBCDNumber(transaction->calling())));
	gReports.incr("OpenBTS.GSM.CC.MTC.Setup");
	transaction->setTimer("303");
	transaction->GSMState(GSM::CallPresent);

	// Wait for Call Confirmed message.
	LOG(DEBUG) << "wait for GSM Call Confirmed";
	while (transaction->GSMState()!=GSM::MTCConfirmed) {
		if (transaction->MTCSendTrying()==SIP::Fail) {
			LOG(NOTICE) << "call failed on SIP side";
			if (TCH) TCH->send(GSM::HARDRELEASE);
			// Cause 0x03 is "no route to destination"
			return abortAndRemoveCall(transaction,LCH,GSM::L3Cause(0x03));
		}
		// FIXME -- What's the proper timeout here?
		// It's the SIP TRYING timeout, whatever that is.
		if (updateGSMSignalling(transaction,LCH,1000)) {
			LOG(INFO) << "Release from GSM side";
			if (TCH) TCH->send(GSM::HARDRELEASE);
			LCH->send(GSM::RELEASE);
			return;
		}
		// Check for SIP cancel, too.
		if (transaction->MTCCheckForCancel()==SIP::MTDCanceling) {
			LOG(INFO) << "call cancelled on SIP side";
			if (TCH) TCH->send(GSM::HARDRELEASE);
			transaction->MTDSendCANCELOK();
			//should probably send a 487 here
			// Cause 0x15 is "rejected"
			return abortAndRemoveCall(transaction,LCH,GSM::L3Cause(0x15));
		}
		//lastly check if we're toast
		if(transaction->SIPState()==SIP::Fail) {
			LOG(DEBUG) << "Call failed";
			if (TCH) TCH->send(GSM::HARDRELEASE);
			return abortAndRemoveCall(transaction,LCH,GSM::L3Cause(0x7F));
		}
	}

	// The transaction is moving to the MTCController.
	// Once this update happens, don't change the transaction object again in this function.
	LOG(DEBUG) << "transaction: " << *transaction;
	if (veryEarly) {
		// For very early assignment, we need a mode change.
		static const GSM::L3ChannelMode mode(GSM::L3ChannelMode::SpeechV1);
		LCH->send(GSM::L3ChannelModeModify(LCH->channelDescription(),mode));
		// FIXME - We should call this in a loop in case there are stray messages in the channel.
		GSM::L3Message* msg_ack = getMessage(LCH);
		const GSM::L3ChannelModeModifyAcknowledge *ack =
			dynamic_cast<GSM::L3ChannelModeModifyAcknowledge*>(msg_ack);
		if (!ack) {
			if (msg_ack) {
				LOG(WARNING) << "Unexpected message " << *msg_ack;
				delete msg_ack;
			}
			throw UnexpectedMessage(transaction->ID());
		}
		// Cause 0x06 is "channel unacceptable"
		bool modeOK = (ack->mode()==mode);
		delete msg_ack;
		if (!modeOK) return abortAndRemoveCall(transaction,LCH,GSM::L3Cause(0x06));
		MTCController(transaction,dynamic_cast<GSM::TCHFACCHLogicalChannel*>(LCH));
	}
	else {
		// For late assignment, send the TCH assignment now.
		// The dispatcher on the next channel will continue the transaction.
		assert(TCH);
		assignTCHF(transaction,LCH,TCH);
	}
}


void Control::MTCController(TransactionEntry *transaction, GSM::TCHFACCHLogicalChannel* TCH)
{
	// Early Assignment Mobile Terminated Call. 
	// Transaction table in 04.08 7.3.3 figure 7.10a

	LOG(DEBUG) << "transaction: " << *transaction;
	unsigned L3TI = transaction->L3TI();
	assert(L3TI<7);
	assert(TCH);

	// Get the alerting message.
	LOG(INFO) << "waiting for GSM Alerting and Connect";
	while (transaction->GSMState()!=GSM::Active) {
		if (updateGSMSignalling(transaction,TCH,1000)) return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x15));
		if (transaction->GSMState()==GSM::Active) break;
		if (transaction->GSMState()==GSM::CallReceived) {
			LOG(DEBUG) << "sending SIP Ringing";
			transaction->MTCSendRinging();
		}
		//this should probably check if GSM has ended as well
		if (transaction->SIPFinished()){
			//Call was canceled during setup, just remove it
			LOG(INFO) << "Call canceled during setup, removing";
			return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x15));
		}
		// Check for SIP cancel, too.
		if (transaction->MTCCheckForCancel()==SIP::MTDCanceling) {
			LOG(INFO) << "MTCCheckForCancel return Canceling";
			transaction->MTDSendCANCELOK();
			//should probably send a 487 here
			// Cause 0x15 is "rejected"
			return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x15));
		}
		//check if we're toast
		if (transaction->SIPState()==SIP::Fail){
			LOG(DEBUG) << "Call failed";
			return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x7F));
		}
	}

	// FIXME -- We should also have a SIP.Timer.F timeout here.
	LOG(INFO) << "allocating port and sending SIP OKAY";
	unsigned RTPPorts = allocateRTPPorts();
	SIP::SIPState state = transaction->MTCSendOK(RTPPorts,SIP::RTPGSM610);
	while (state!=SIP::Active) {
		LOG(DEBUG) << "wait for SIP OKAY-ACK";
		if (updateGSMSignalling(transaction,TCH)) return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x15));
		state = transaction->MTCCheckForACK();
		LOG(DEBUG) << "SIP call state "<< state;
		switch (state) {
			case SIP::Active:
				break;
			case SIP::Fail:
				return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x7F));
			case SIP::Timeout:
				state = transaction->MTCSendOK(RTPPorts,SIP::RTPGSM610);
				break;
			case SIP::Connecting:
				break;
			case SIP::MTDCanceling:
				state = transaction->MTDSendCANCELOK();
				//should probably send a 487 here
				// Cause 0x15 is "rejected"
				return abortAndRemoveCall(transaction,TCH,GSM::L3Cause(0x15));
			default:
				LOG(NOTICE) << "SIP unexpected state " << state;
				break;
		}
	}
	transaction->MTCInitRTP();

	// Send Connect Ack to make it all official.
	LOG(DEBUG) << "MTC send GSM Connect Acknowledge";
	TCH->send(GSM::L3ConnectAcknowledge(L3TI));

	// At this point, everything is ready to run for the call.
	// The radio link should have been cleared with the call.
	callManagementLoop(transaction,TCH);
}


void Control::TestCall(TransactionEntry *transaction, GSM::LogicalChannel *LCH)
{
	assert(LCH);
	LOG(INFO) << LCH->type() << " transaction: "<< *transaction;
	assert(transaction->L3TI()<7);

	// Mark the call as active.
	transaction->GSMState(GSM::Active);

	LOG(INFO) << "starting test call";
	while (!transaction->terminationRequested()) { sleep(1); }
	LOG(INFO) << "ending test call";
	LCH->send(GSM::L3ChannelRelease());
	gTransactionTable.remove(transaction);
}





void Control::initiateMTTransaction(TransactionEntry *transaction, GSM::ChannelType chanType, unsigned pageTime)
{
	gTransactionTable.add(transaction);
	transaction->GSMState(GSM::Paging);
	gBTS.pager().addID(transaction->subscriber(),chanType,*transaction,pageTime);
}





// vim: ts=4 sw=4
