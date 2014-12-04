/**@file GSM Radio Resource procedures, GSM 04.18 and GSM 04.08. */

/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012, 2013, 2014 Range Networks, Inc.
*

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <list>

#define LOG_GROUP LogGroup::Control
#include <Defines.h>
#include "ControlCommon.h"
#include "L3Handover.h"
#include "L3CallControl.h"
#include "L3MMLayer.h"

#include <GSMLogicalChannel.h>
#include <GSMConfig.h>
#include "../GPRS/GPRSExport.h"
#include <GSML3RRElements.h>
#include <L3Enums.h>

#include <NeighborTable.h>
#include <Peering.h>
#include <SIPDialog.h>

#include <Reporting.h>
#include <Logger.h>
#undef WARNING




using namespace std;
using namespace GSM;
namespace Control {

static void abortInboundHandover(RefCntPointer<TranEntry> transaction, RRCause cause, L3LogicalChannel *LCH=NULL)
{
	LOG(DEBUG) << "aborting inbound handover " << *transaction;
	unsigned holdoff = gConfig.getNum("GSM.Handover.FailureHoldoff");
	gPeerInterface.sendHandoverFailure(transaction->getHandoverEntry(true),cause,holdoff);
}



// How did we get here you ask?  Peering received a handover request on BTS2 (us), allocated a channel and set the handoverPending flag,
// created a transaction with the specified IMSI, returned an L3 handover command which BTS1 sent to the MS, which then
// sent a handover access to BTS2, and here we are!
void ProcessHandoverAccess(L3LogicalChannel *chan)
{
	using namespace SIP;
	// In this function, we are "BS2" in the ladder diagram.
	// This is called from the DCCH dispatcher when it gets a HANDOVER_ACCESS primtive.
	// The information it needs was saved in the transaction table by SaveHandoverAccess.
	LOG(DEBUG) << *chan;

	RefCntPointer<TranEntry> tran = chan->chanGetVoiceTran();
	if (tran == NULL) {
		LOG(WARNING) << "handover access with no inbound transaction on " << chan;
		chan->chanRelease(L3_HARDRELEASE_REQUEST,TermCause::Local(L3Cause::Handover_Error));
		return;
	}
	LOG(DEBUG) << *tran;

	if (!tran->getHandoverEntry(false)) {
		LOG(WARNING) << "handover access with no inbound handover on " << *chan;
		chan->chanRelease(L3_HARDRELEASE_REQUEST,TermCause::Local(L3Cause::Handover_Error));
		return;
	}

	// clear handover in transceiver and get the RSSI and TE.
	// This instructs L2 to stop looking for and stop sending HANDOVER_ACCESS.
	// However, we cant just flush them out of the queue here because that is running in another
	// thread and it may keep pushing HANDOVER_ACCESS at, so we keep flushing them (below) 
	// However, we should NEVER see HANDOVER_ACCESS after the ESTABLISH, yet I did.
	GSM::HandoverRecord hr = chan->getL2Channel()->handoverPending(false,0);

	// TODO: Move this into L1?
	if (hr.mhrTimingError > gConfig.getNum("GSM.MS.TA.Max")) {
		// Handover failure.
		LOG(NOTICE) << "handover failure on due to TA=" << hr.mhrTimingError << " for " << *tran;
		// RR cause 8: Handover impossible, timing advance out of range
		abortInboundHandover(tran,L3RRCause::Handover_Impossible,dynamic_cast<L2LogicalChannel*>(tran->channel()));
		chan->chanRelease(L3_HARDRELEASE_REQUEST,TermCause::Local(L3Cause::Distance));	// TODO: Is this right?  Will the channel be immediately re-available?
		return;
	}

	chan->getL2Channel()->l1InitPhy(hr.mhrRSSI,hr.mhrTimingError,hr.mhrTimestamp);
	
	// Respond to handset with physical information until we get Handover Complete.
	int TA = (int)(hr.mhrTimingError + 0.5F);
	if (TA<0) TA=0;
	if (TA>62) TA=62;

	// We want to do this loop carefully so we exit as soon as we get a frame that is not HANDOVER_ACCESS.
	Z100Timer T3105(gConfig.GSM.Timer.T3105);	// It defaults to only 50ms.

	// 4.08 11.1.3 "Ny1: The maximum number of repetitions for the PHYSICAL INFORMATION message during a handover."
	for (unsigned sendCount = gConfig.getNum("GSM.Handover.Ny1"); sendCount > 0; sendCount--) {
		T3105.set();
		// (pat) It is UNIT_DATA because the channel is not established yet.
		// (pat) WARNING: This l3sendm call is not blocking because it is sent on FACCH which has a queue.
		// Rather than modifying the whole LogicalChannel stack to have a blocking mode,
		// we are just going to wait afterwards.  The message should take about 20ms to transmit,
		// and GSM uses roughly 4 out of every 5 frames, so 20-25ms would transmit the message continuously.
		chan->l3sendm(L3PhysicalInformation(L3TimingAdvance(TA)),GSM::L3_UNIT_DATA);

		// (pat) Throw away all the HANDOVER_ACCESS that arrive while we were waiting.
		// They are not messages that take 4 bursts; they can arrive on every burst, so there
		// can be a bunch of them queued up (I would expect 5) for each message we send.
		while (L3Frame *frame = chan->l2recv(T3105.remaining())) {
			switch (frame->primitive()) {
				case HANDOVER_ACCESS:
					// See comments above.  L2 is no longer generating these, but we need
					// to flush any extras from before we started, and there also might be have been
					// some in progress when we turned them off, so just keep flushing.
					LOG(INFO) << "flushing HANDOVER_ACCESS while waiting for Handover Complete";
					delete frame;
					continue;
				case L3_ESTABLISH_INDICATION:
					delete frame;
					// Channel is established, so the MS is there.  Finish up with a state machine.
					startInboundHandoverMachine(tran.self());
					return;
				default:
					// Something else?
					LOG(NOTICE) << "unexpected primitive waiting for Handover Complete on "
						<< *chan << ": " << *frame << " for " << *tran;
					delete frame;
					abortInboundHandover(tran,L3RRCause::Message_Type_Not_Compapatible_With_Protocol_State,chan);
					chan->chanRelease(L3_HARDRELEASE_REQUEST,TermCause::Local(L3Cause::Handover_Error));	// TODO: Is this right?  Will the channel be immediately re-available?
					return;
			}
		}
	}

	// Failure.
	LOG(NOTICE) << "timed out waiting for Handover Complete on " << *chan << " for " << *tran;
	// RR cause 4: Abnormal release, no activity on the radio path
	abortInboundHandover(tran,L3RRCause::No_Activity_On_The_Radio,chan);
	chan->chanRelease(L3_HARDRELEASE_REQUEST,TermCause::Local(L3Cause::Radio_Interface_Failure));	// TODO: Is this right?  Will the channel be immediately re-available?
	return;
}

static BestNeighbor NoHandover(BestNeighbor np)
{
	np.mValid = false;
	return np;
}

static BestNeighbor YesHandover(BestNeighbor &np, L3Cause::BSSCause why)
{
	assert(np.mValid);
	np.mHandoverCause = string(L3Cause::BSSCause2Str(why));
	return np;
}


static BestNeighbor HandoverDecision(const L3MeasurementResults* measurements, SACCHLogicalChannel* sacch)
{
	ChannelHistory *chp = sacch->getChannelHistory();
	int myRXLEV_DL = chp->getAvgRxlev();
	NeighborPenalty penalty;
	if (MMContext *mmc = sacch->hostChan()->chanGetContext(false)) {
		penalty = mmc->mmcHandoverPenalty;
	}
	LOG(DEBUG) << LOGVAR(penalty);

	// This uses RLXLEV_DL.History to comute the neighbor RXLEV
	BestNeighbor bestn = chp->neighborFindBest(penalty);
	if (! bestn.mValid) { return NoHandover(bestn); }

	int margin = gConfig.GSM.Handover.Margin;

	Peering::NeighborEntry nentry;
	if (!gNeighborTable.ntFindByArfcn(bestn.mARFCN, bestn.mBSIC, &nentry)) {
		LOG(ERR) << "Could not find best neighbor entry from :"<<LOGVAR2("ARFCN",bestn.mARFCN)<<LOGVAR2("BSIC",bestn.mBSIC);
		return NoHandover(bestn);
	}

	if (bestn.mRxlev - myRXLEV_DL >= margin) { return YesHandover(bestn,L3Cause::Downlink_Strength); }

	return NoHandover(bestn);
}

// Warning: This runs in a separate thread.
void HandoverDetermination(const L3MeasurementResults* measurements, SACCHLogicalChannel* sacch)
{
	//LOG(DEBUG) <<measurements->text();
	// This is called from the SACCH service loop.
	if (! sacch->neighborAddMeasurements(sacch,measurements)) return;

	// (pat) TODO: If you add your own IP address to the sql neighbor list, the MS will return info on yourself,
	// which will attempt a handover to yourself unless you throw those measurement reports away here.
	// We should detect this and throw them out.
	// Currently processNeighborParams() detects this condition when it gets a Peer report (but not at startup!)
	// but we dont save the BSIC in memory so we dont have that information here where we need it.

	BestNeighbor bestn = HandoverDecision(measurements, sacch);
	LOG(DEBUG) << bestn;
	if (! bestn.mValid) {
		// No handover for now.
		return;
	}

	string whatswrong;	// pass by reference to getAddress()
	string peerstr = gNeighborTable.getAddress(bestn.mARFCN, bestn.mBSIC,whatswrong);
	if (peerstr.empty()) {
		LOG(INFO) << "measurement for unknown neighbor"<<LOGVAR2("ARFCN",bestn.mARFCN)<<LOGVAR2("BSIC",bestn.mBSIC) <<" "<<whatswrong;
		return;
	}
	if (gNeighborTable.holdingOff(peerstr.c_str())) {
		LOG(NOTICE) << "skipping "<<bestn.mHandoverCause<< " handover to " << peerstr << " due to holdoff";
		return;
	}

	// Find the transaction record.

	const L3LogicalChannel *mainChanConst = dynamic_cast<typeof(mainChanConst)>(sacch->hostChan());
	L3LogicalChannel *mainChan = const_cast<typeof(mainChan)>(mainChanConst);	// idiotic language
	// The RefCntPointer prevents the tran from being deleted while we are working here, as unlikely as that would be.
	const RefCntPointer<TranEntry> tran = mainChan->chanGetVoiceTran();
	if (tran == NULL) {
		LOG(ERR) << "active SACCH with no transaction record: " << *sacch;
		return;
	}
	if (tran->getGSMState() != CCState::Active) {
		LOG(DEBUG) << "skipping handover for transaction " << tran->tranID()
			<< " due to state " << tran->getGSMState();
		return;
	}
	// Don't hand over an emergency call based on an IMEI.  It WILL fail.
	if (tran->servicetype() == GSM::L3CMServiceType::EmergencyCall &&
		//Unconst(tran)->subscriber().mImsi.length() == 0)
		tran->subscriber().mImsi.length() == 0) {
		LOG(ALERT) << "cannot handover emergency call with non-IMSI subscriber ID: " << *tran;
		return;
	}

	// (pat) Dont handover a brand new transaction.  This also prevents an MS from bouncing
	// back and forth between two BTS.  We dont need a separate timer for this handover holdoff,
	// we can just use the age of the Transaction.
	// I dont see any such timer in the spec; I am reusing T3101ms, which is not correct but vaguely related.
	// Update - this is now unnecessary because the averaging method of myRxLevel prevents a handover for the first 5-10 secs.
	unsigned age = tran->stateAge();	// in msecs.
	unsigned holdoff = 1000 * gConfig.getNum("GSM.Timer.Handover.Holdoff"); // default 10 seconds.
	if (age < holdoff) {
		WATCH("skipping handover for transaction " << tran->tranID() << " due to young"<<LOGVAR(age));
		LOG(DEBUG) << "skipping handover for transaction " << tran->tranID() << " because age "<<age<<"<"<<holdoff;
		return;
	}
	LOG(INFO) << "preparing "<<bestn.mHandoverCause<<" handover of " << tran->tranID()
		<< " to " << peerstr << " with downlink RXLEV=" << bestn.mRxlev << " dbm";

	// The handover reference will be generated by the other BTS.
	// We don't set the handover reference or state until we get RSP HANDOVER.

	// TODO: Check for handover request to our own BTS and avoid it.  Dont forget to check the port too. 
#if 0  // This did not work for some reason.
	struct sockaddr_in peerAddr;
	if (resolveAddress(&peerAddr,peerstr.c_str())) {
		LOG(ALERT) "handover"<<LOGHEX(peerAddr.sin_addr.s_addr)<<LOGHEX(inet_addr("127.0.0.1"))<<LOGVAR(peerAddr.sin_port)<<LOGVAR(gConfig.getNum("Peering.Port"));
		if (peerAddr.sin_addr.s_addr == inet_addr("127.0.0.1") &&
			peerAddr.sin_port == gConfig.getNum("Peering.Port")) {
			LOG(ERR) << "Attempted handover to self ignored.";
			return;
		}
	}
#endif

	// Form and send the message.
	// This message is re-sent every 0.5s (the periodicity of measurement reports) until the peer answers.
	// pats TODO: we could surely do this a better way.
	gPeerInterface.sendHandoverRequest(peerstr,tran,bestn.mHandoverCause);
}

// (pat) TODO: This should be merged into the InCallProcedure state machine, but lets just get it working first.
// We are BS1 and received a RSP HANDOVER from the peer BS2, so we will now send the handoverCommand to the MS.
bool outboundHandoverTransfer(TranEntry* transaction, L3LogicalChannel *TCH)
{
	LOG(DEBUG) <<LOGVAR(TCH) <<LOGVAR(transaction);
	// By returning true, this function indicates to its caller that the call is cleared
	// and no longer needs a channel on this BTS.

	// In this method, we are "BS1" in the ladder diagram.
	// BS2 has alrady accepted the handover request.

	// Send the handover command.
	// We leave TA out for a non-synchronized handover, which causes the MS to use the
	// default TA value defined in GSM 5.10 6.6, which is 0.
	// We use a 0 value in OutboundPowerCmd which is max power.
	HandoverEntry *hop = transaction->getHandoverEntry(true);
	L3Frame HandoverCommand(SAPI0, hop->mHexEncodedL3HandoverCommand.c_str());
	LOG(INFO) <<TCH<<" sending handover command";
	TCH->l3sendf(HandoverCommand);

	// Start a timer for T3103, the handover failure timer.
	// This T3103 timer is for the outbound leg of the handover on BS1.
	// There is another T3103 timer in GSML1FEC for the inbound handover on BS2.
	GSM::Z100Timer outboundT3103(gConfig.getNum("GSM.Timer.T3103") + 1000);
	outboundT3103.set();

	// The next step for the MS is to send Handover Access to BS2.
	// The next step for us is to wait for the Handover Complete message
	// and see that the phone doesn't come back to us.
	// BS2 is doing most of the work now.
	// We will get a handover complete once it's over, but we don't really need it.

	// Q: What about transferring audio packets?
	// A: There should not be any after we send the Handover Command.
	// A2: (pat 7-25-2013) Wrong, the MS may take up to a second to get around to handover, so we should keep sending
	// audio packets as long as we can.

	// Get the response.
	// This is supposed to time out on successful handover, similar to the early assignment channel transfer.
	GSM::L3Frame *result = TCH->l2recv(outboundT3103.remaining());
	if (result) {
		// If we got here, the handover failed and we just keep running the call.
		if (IS_LOG_LEVEL(NOTICE)) {
			L3Message *msg = parseL3(*result);
			// It is ok to pass a NULL L3Message pointer here.
			LOG(NOTICE) << "failed handover, "<<TCH <<" received " << *result << msg;
			if (msg) { delete msg; }
		}
		delete result;
		// Restore the call state.
		transaction->setGSMState(CCState::Active);
		return false;
	}

	// If the phone doesn't come back, either the handover succeeded or
	// the phone dropped the connection.  Either way, we are clearing the call.

	// Invalidate local cache entry for this IMSI in the subscriber registry.
	// (pat) TODO: I dont understand how this works - it looks like it is over-writing what BS2 added.
	string imsi = string("IMSI").append(transaction->subscriber().mImsi);
	// (mike) TODO: disabled as there no longer local vs upstream caches
	//gSubscriberRegistry.removeUser(imsi.c_str());

	TermCause cause = TermCause::Local(L3Cause::Handover_Outbound);	// There is no SIP code.  The dialog was moved to another BTS via a SIP REFER.
	transaction->teCancel(cause);	// We need to do this immediately in case a reverse handover comes back soon.  This destroys the dialog too.

	LOG(INFO) <<"timeout following outbound handover; exiting normally";
	return true;
}

};	// namespace Control
