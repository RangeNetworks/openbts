/**@file GSM Radio Resource procedures, GSM 04.18 and GSM 04.08. */

/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012, 2013 Range Networks, Inc.
*

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.
*/

#include <stdio.h>
#include <stdlib.h>
#include <list>

#include <Defines.h>
#include "ControlCommon.h"
#include "RadioResource.h"
#include "L3CallControl.h"
#include "L3MMLayer.h"

#include <GSMLogicalChannel.h>
#include <GSMConfig.h>
#include "../GPRS/GPRSExport.h"

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
	//gTransactionTable.remove(transaction);
}



#if UNUSED
bool SaveHandoverAccess(unsigned handoverReference, float RSSI, float timingError, const GSM::Time& timestamp)
{
	assert(! l3rewrite());	// Not used in l3rewrite.  See TCHFACCHL1Decoder::writeLowSideRx
	// In this function, we are "BS2" in the ladder diagram.
	// This is called from L1 when a handover burst arrives.

	// We will need to use the transaction record to carry the parameters.
	// We put this here to avoid dealing with the transaction table in L1.
	TransactionEntry *transaction = gTransactionTable.ttFindByInboundHandoverRef(handoverReference);
	if (!transaction) {
		LOG(ERR) << "no inbound handover with reference " << handoverReference;
		return false;
	}

	if (timingError > gConfig.getNum("GSM.MS.TA.Max")) {
		// Handover failure.
		LOG(NOTICE) << "handover failure on due to TA=" << timingError << " for " << *transaction;
		// RR cause 8: Handover impossible, timing advance out of range
		OldAbortInboundHandover(transaction,L3RRCause::HandoverImpossible,dynamic_cast<L2LogicalChannel*>(transaction->channel()));
		return false;
	}

	LOG(INFO) << "saving handover access for " << *transaction;
	transaction->setInboundHandover(RSSI,timingError,gBTS.clock().systime(timestamp));
	return true;
}
#endif



//void ProcessHandoverAccess(GSM::TCHFACCHLogicalChannel *TCH)
//{
//	// In this function, we are "BS2" in the ladder diagram.
//	// This is called from the DCCH dispatcher when it gets a HANDOVER_ACCESS primtive.
//	// The information it needs was saved in the transaction table by SaveHandoverAccess.
//
//
//	assert(TCH);
//	LOG(DEBUG) << *TCH;
//
//	TransactionEntry *transaction = gTransactionTable.ttFindByInboundHandoverChan(TCH);
//	if (!transaction) {
//		LOG(WARNING) << "handover access with no inbound transaction on " << *TCH;
//		TCH->l2sendp(HARDRELEASE);
//		return;
//	}
//
//	// clear handover in transceiver
//	LOG(DEBUG) << *transaction;
//	transaction->getL2Channel()->handoverPending(false);
//	
//	// Respond to handset with physical information until we get Handover Complete.
//	int TA = (int)(transaction->inboundTimingError() + 0.5F);
//	if (TA<0) TA=0;
//	if (TA>62) TA=62;
//	unsigned repeatTimeout = gConfig.getNum("GSM.Timer.T3105");
//	unsigned sendCount = gConfig.getNum("GSM.Ny1");
//	L3Frame* frame = NULL;
//	while (!frame && sendCount) {
//		TCH->l2sendm(L3PhysicalInformation(L3TimingAdvance(TA)),GSM::UNIT_DATA);
//		sendCount--;
//		frame = TCH->l2recv(repeatTimeout);
//		if (frame && frame->primitive() == HANDOVER_ACCESS) {
//			LOG(NOTICE) << "flushing HANDOVER_ACCESS while waiting for Handover Complete";
//			delete frame;
//			frame = NULL;
//		}
//	}
//
//	// Timed out?
//	if (!frame) {
//		LOG(NOTICE) << "timed out waiting for Handover Complete on " << *TCH << " for " << *transaction;
//		// RR cause 4: Abnormal release, no activity on the radio path
//		OldAbortInboundHandover(transaction,4,TCH);
//		return;
//	}
//
//	// Screwed up channel?
//	if (frame->primitive()!=ESTABLISH) {
//		LOG(NOTICE) << "unexpected primitive waiting for Handover Complete on "
//			<< *TCH << ": " << *frame << " for " << *transaction;
//		delete frame;
//		// RR cause 0x62: Message not compatible with protocol state
//		OldAbortInboundHandover(transaction,0x62,TCH);
//		return;
//	}
//
//	// Get the next frame, should be HandoverComplete.
//	delete frame;
//	frame = TCH->l2recv();
//	L3Message* msg = parseL3(*frame);
//	if (!msg) {
//		LOG(NOTICE) << "unparsable message waiting for Handover Complete on "
//			<< *TCH << ": " << *frame << " for " << *transaction;
//		delete frame;
//		// RR cause 0x62: Message not compatible with protocol state
//		TCH->l2sendm(L3ChannelRelease(L3RRCause::MessageTypeNotCompapatibleWithProtocolState));
//		OldAbortInboundHandover(transaction,0x62,TCH);
//		return;
//	}
//	delete frame;
//
//	L3HandoverComplete* complete = dynamic_cast<L3HandoverComplete*>(msg);
//	if (!complete) {
//		LOG(NOTICE) << "expecting for Handover Complete on "
//			<< *TCH << "but got: " << *msg << " for " << *transaction;
//		delete frame;
//		// RR cause 0x62: Message not compatible with protocol state
//		TCH->l2sendm(L3ChannelRelease(L3RRCause::MessageTypeNotCompapatibleWithProtocolState));
//		OldAbortInboundHandover(transaction,0x62,TCH);
//	}
//	delete msg;
//
//	// Send re-INVITE to the remote party.
//	unsigned RTPPort = allocateRTPPorts();
//	SIP::SIPState st = transaction->inboundHandoverSendINVITE(RTPPort);
//	if (st == SIP::Fail) {
//		OldAbortInboundHandover(transaction,4,TCH);
//		return;
//	}
//
//	transaction->GSMState(CCState::HandoverProgress);
//
//	while (1) {
//		// FIXME - the sip engine should be doing this
//		// FIXME - and checking for timeout
//		// FIXME - and checking for proceeding (stop sending the resends)
//		st = transaction->inboundHandoverCheckForOK();
//		if (st == SIP::Active) break;
//		if (st == SIP::Fail) {
//			LOG(NOTICE) << "received Fail while waiting for OK";
//			OldAbortInboundHandover(transaction,4,TCH);
//			return;
//		}
//	}
//	st = transaction->inboundHandoverSendACK();
//	LOG(DEBUG) << "status of inboundHandoverSendACK: " << st << " for " << *transaction;
//
//	// Send completion to peer BTS.
//	char ind[100];
//	sprintf(ind,"IND HANDOVER_COMPLETE %u", transaction->tranID());
//	gPeerInterface.sendUntilAck(transaction,ind);
//
//	// Update subscriber registry to reflect new registration.
//	if (transaction->SRIMSI().length() && transaction->SRCALLID().length()) {
//		gSubscriberRegistry.addUser(transaction->SRIMSI().c_str(), transaction->SRCALLID().c_str());
//	}
//
//	// The call is running.
//	LOG(INFO) << "succesful inbound handover " << *transaction;
//	transaction->GSMState(CCState::Active);
//	callManagementLoop(transaction,TCH);
//}

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
		chan->chanRelease(HARDRELEASE);
		return;
	}
	LOG(DEBUG) << *tran;

	if (!tran->getHandoverEntry(false)) {
		LOG(WARNING) << "handover access with no inbound handover on " << *chan;
		chan->chanRelease(HARDRELEASE);
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
		abortInboundHandover(tran,L3RRCause::HandoverImpossible,dynamic_cast<L2LogicalChannel*>(tran->channel()));
		chan->chanRelease(HARDRELEASE);	// TODO: Is this right?  Will the channel be immediately re-available?
		return;
	}

	chan->getL2Channel()->setPhy(hr.mhrRSSI,hr.mhrTimingError,hr.mhrTimestamp);
	
	// Respond to handset with physical information until we get Handover Complete.
	int TA = (int)(hr.mhrTimingError + 0.5F);
	if (TA<0) TA=0;
	if (TA>62) TA=62;

	// We want to do this loop carefully so we exit as soon as we get a frame that is not HANDOVER_ACCESS.
	Z100Timer T3105(gConfig.getNum("GSM.Timer.T3105"));	// It defaults to only 50ms.

	// 4.08 11.1.3 "Ny1: The maximum number of repetitions for the PHYSICAL INFORMATION message during a handover."
	for (unsigned sendCount = gConfig.getNum("GSM.Ny1"); sendCount > 0; sendCount--) {
		T3105.set();
		// (pat) It is UNIT_DATA because the channel is not established yet.
		// (pat) WARNING: This l3sendm call is not blocking because it is sent on FACCH which has a queue.
		// Rather than modifying the whole LogicalChannel stack to have a blocking mode,
		// we are just going to wait afterwards.  The message should take about 20ms to transmit,
		// and GSM uses roughly 4 out of every 5 frames, so 20-25ms would transmit the message continuously.
		chan->l3sendm(L3PhysicalInformation(L3TimingAdvance(TA)),GSM::UNIT_DATA);

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
				case ESTABLISH:
					delete frame;
					// Channel is established, so the MS is there.  Finish up with a state machine.
					startInboundHandoverMachine(tran.self());
					return;
				default:
					// Something else?
					LOG(NOTICE) << "unexpected primitive waiting for Handover Complete on "
						<< *chan << ": " << *frame << " for " << *tran;
					delete frame;
					abortInboundHandover(tran,L3RRCause::MessageTypeNotCompapatibleWithProtocolState,chan);
					chan->chanRelease(HARDRELEASE);	// TODO: Is this right?  Will the channel be immediately re-available?
					return;
			}
		}
	}

	// Failure.
	LOG(NOTICE) << "timed out waiting for Handover Complete on " << *chan << " for " << *tran;
	// RR cause 4: Abnormal release, no activity on the radio path
	abortInboundHandover(tran,L3RRCause::NoActivityOnTheRadio,chan);
	chan->chanRelease(HARDRELEASE);	// TODO: Is this right?  Will the channel be immediately re-available?
	return;


#if 0
	//	// Get the next frame, should be HandoverComplete.
	//	delete frame;
	//	frame = chan->l2recv();
	//	L3Message* msg = parseL3(*frame);
	//	if (!msg) {
	//		LOG(NOTICE) << "unparsable message waiting for Handover Complete on "
	//			<< *chan << ": " << *frame << " for " << *tran;
	//		delete frame;
	//		// The MS is listening to us now, so we have to send it something if we abort.
	//		// TODO: Should be a state machine from here on.
	//		// RR cause 0x62: Message not compatible with protocol state
	//		chan->chanClose(L3RRCause::MessageTypeNotCompapatibleWithProtocolState,HARDRELEASE);
	//		abortInboundHandover(tran,L3RRCause::MessageTypeNotCompapatibleWithProtocolState,chan);
	//		return;
	//	}
	//	delete frame;
	//
	//	L3HandoverComplete* complete = dynamic_cast<L3HandoverComplete*>(msg);
	//	if (!complete) {
	//		LOG(NOTICE) << "expecting for Handover Complete on "
	//			<< *chan << "but got: " << *msg << " for " << *tran;
	//		delete frame;
	//		// RR cause 0x62: Message not compatible with protocol state
	//		chan->chanClose(L3RRCause::MessageTypeNotCompapatibleWithProtocolState,HARDRELEASE);
	//		abortInboundHandover(tran,L3RRCause::MessageTypeNotCompapatibleWithProtocolState,chan);
	//	}
	//	delete msg;
	//
	//	// MS has successfully arrived on BS2.  Open the SIPDialog and attempt to transfer the SIP session.
	//
	//	// Send re-INVITE to the remote party.
	//	//unsigned RTPPort = allocateRTPPorts();
	//	SIP::SIPDialog *dialog = SIP::SIPDialog::newSIPDialogHandover(tran);
	//	if (dialog == NULL) {
	//		// TODO: Can we abort at this point?  It is too late.
	//		// But this only fails if the address is wrong.
	//		//abortInboundHandover(tran,L3RRCause::NoActivityOnTheRadio,chan);
	//		LOG(NOTICE) << "handover failure due to failure to create dialog for " << *tran;	// Will probably never happen.
	//		tran->teCloseCall(L3Cause::InterworkingUnspecified);
	//		chan->chanClose(L3RRCause::Unspecified,RELEASE);
	//		return;
	//	}
	//	tran->setDialog(dialog);
	//	tran->setGSMState(CCState::HandoverProgress);
	//
	//	while (DialogMessage*dmsg = dialog->dialogRead()) {
	//		switch (dmsg->dialogState()) {
	//			case SIPDialog::dialogActive:
	//				// We're good to go.
	//				tran->setGSMState(CCState::Active);
	//				break;
	//			case SIPDialog::dialogBye:
	//				// Other end hung up.  Just hang up.
	//				tran->teCloseCall(L3Cause::NormalCallClearing);
	//				chan->chanClose(L3RRCause::NormalEvent,RELEASE);
	//				return;
	//			default:
	//				LOG(ERR) << "unrecognized SIP Dialog state while waiting for handover re-invite OK"<<tran;
	//				goto oops;
	//			case SIPDialog::dialogFail:			// busy, cancel or fail for any reason.
	//				LOG(NOTICE) << "handover received SIP Dialog Fail while waiting for OK"<<tran;
	//				oops:
	//				//abortInboundHandover(tran,L3RRCause::Unspecified,chan);	// TODO: Can we still do this now?
	//				tran->teCloseCall(L3Cause::InterworkingUnspecified);
	//				chan->chanClose(L3RRCause::Unspecified,RELEASE);
	//				return;
	//		}
	//		delete dmsg;
	//		if (tran->getGSMState() == CCState::Active) { break; }
	//	}
	//	SIP::SIPState st = dialog->inboundHandoverSendACK();
	//	LOG(DEBUG) << "status of inboundHandoverSendACK: " << st << " for " << *tran;
	//
	//	// Send completion to peer BTS.
	//	char ind[100];
	//	sprintf(ind,"IND HANDOVER_COMPLETE %u", tran->tranID());
	//	gPeerInterface.sendUntilAck(tran->getHandoverEntry(true),ind);
	//
	//	// Update subscriber registry to reflect new registration.
	//	/*** Pat thinks these are not used.
	//	if (transaction->SRIMSI().length() && transaction->SRCALLID().length()) {
	//		gSubscriberRegistry.addUser(transaction->SRIMSI().c_str(), transaction->SRCALLID().c_str());
	//	}
	//	***/
	//
	//	// The call is running.
	//	LOG(INFO) << "succesful inbound handover " << *tran;
	//	//callManagementLoop(transaction,TCH);
#endif
}


// Warning: This runs in a separate thread.
void HandoverDetermination(const L3MeasurementResults& measurements, float myRxLevel, SACCHLogicalChannel* SACCH)
{
	// This is called from the SACCH service loop.

	// Valid measurements?
	if (measurements.MEAS_VALID()) return;

	// Got neighbors?
	// (pat) I am deliberately not aging the neighbor list if the measurement report is empty because
	// I am afraid it may be empty because the MS did not have time to make measurements during this time
	// period, rather than really indicating that there are no neighbors.
	unsigned N = measurements.NO_NCELL();
	if (N==0) { return; }

	if (N == 7) {
		LOG(DEBUG) << "neighbor cell information not available";
		return;
	}

	// (pat) TODO: If you add your own IP address to the sql neighbor list, the MS will return info on yourself,
	// which will attempt a handover to yourself unless you throw those measurement reports away here.
	// We should detect this and throw them out.
	// Currently processNeighborParams() detects this condition when it gets a Peer report (but not at startup!)
	// but we dont save the BSIC in memory so we dont have that information here where we need it.
	
	// Look at neighbor cell rx levels
	SACCH->neighborStartMeasurements();
	int best = 0;
	int bestRxLevel = -1000;
	for (unsigned int i=0; i<N; i++) {
		int thisRxLevel = measurements.RXLEV_NCELL_dBm(i);
		int thisFreq = measurements.BCCH_FREQ_NCELL(i);
		if (thisFreq == 31) {
			// (pat) This is reserved for 3G in some weird way.
			// We support only 31 instead of 32 neighbors to avoid any confusion here.
			continue;
		}
		int thisBSCI = measurements.BSIC_NCELL(i);
		// Average thisRxLevel over several neighbor reports.
		thisRxLevel = SACCH->neighborAddMeasurement(thisFreq,thisBSCI,thisRxLevel);
		if (thisRxLevel>bestRxLevel) {
			best = i;
			bestRxLevel = thisRxLevel;
		}
	}
	int bestBCCH_FREQ_NCELL = measurements.BCCH_FREQ_NCELL(best);	// (pat) This is an index into the neighborlist, not a frequency.
	int bestBSIC = measurements.BSIC_NCELL(best);

	// Is our current signal OK?
	//int myRxLevel = measurements.RXLEV_SUB_SERVING_CELL_dBm();
	int localRSSIMin = gConfig.getNum("GSM.Handover.LocalRSSIMin");
	int threshold = gConfig.getNum("GSM.Handover.ThresholdDelta");
	int gprsRSSI = gConfig.getNum("GPRS.ChannelCodingControl.RSSI");
	// LOG(DEBUG) << "myRxLevel=" << myRxLevel << " dBm localRSSIMin=" << localRSSIMin << " dBm";
	LOG(DEBUG) <<LOGVAR(myRxLevel)<<LOGVAR(localRSSIMin)<<LOGVAR(bestRxLevel) <<LOGVAR(threshold) <<" "<<SACCH->neighborText();
	// Does the best exceed the current by more than the threshold?  If not dont handover.
	if (bestRxLevel < (myRxLevel + threshold)) { return; }

	const char *what;
	if (myRxLevel > localRSSIMin) {
		// The current signal is ok; see if we want to do a discretionery handover.
		if (!(
			(gBTS.TCHTotal() == gBTS.TCHActive()) ||				// Is the current BTS full?
			(myRxLevel < gprsRSSI && bestRxLevel > gprsRSSI) ||		// Would a handover let GPRS use a better codec?
			(bestRxLevel > myRxLevel + 3 * threshold)				// Is the other BTS *much* better?
			)) { return; }											// If not, dont handover.
		what = "discretionary";
	} else {
		// Mandatory handover because the signal is poor and the neighbor BTS is threshold better.
		//LOG(DEBUG) << "myRxLevel=" << myRxLevel << " dBm, best neighbor=" << bestRxLevel << " dBm, threshold=" << threshold << " dB";
		what = "mandatory";
	}

	// OK.  So we will initiate a handover.  Woo hoo!
	LOG(DEBUG) <<what <<" handover " <<measurements.text();

	string peer = gNeighborTable.getAddress(bestBCCH_FREQ_NCELL,bestBSIC);
	if (peer.empty()) {
		LOG(INFO) << "measurement for unknown neighbor BCCH_FREQ_NCELL " << bestBCCH_FREQ_NCELL << " BSIC " << bestBSIC;
		return;
	}
	if (gNeighborTable.holdingOff(peer.c_str())) {
		LOG(NOTICE) << "skipping "<<what<< " handover to " << peer << " due to holdoff";
		return;
	}

	// Find the transaction record.

	const L3LogicalChannel *mainChanConst = dynamic_cast<typeof(mainChanConst)>(SACCH->hostChan());
	L3LogicalChannel *mainChan = const_cast<typeof(mainChan)>(mainChanConst);	// idiotic language
	// The RefCntPointer prevents the tran from being deleted while we are working here, as unlikely as that would be.
	const RefCntPointer<TranEntry> tran = mainChan->chanGetVoiceTran();
	if (tran == NULL) {
		LOG(ERR) << "active SACCH with no transaction record: " << *SACCH;
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
	LOG(INFO) << "preparing "<<what<<" handover of " << tran->tranID()
		<< " to " << peer << " with downlink RSSI " << bestRxLevel << " dbm";

	// The handover reference will be generated by the other BTS.
	// We don't set the handover reference or state until we get RSP HANDOVER.

	// TODO: Check for handover request to our own BTS and avoid it.  Dont forget to check the port too. 
#if 0  // This did not work for some reason.
	struct sockaddr_in peerAddr;
	if (resolveAddress(&peerAddr,peer.c_str())) {
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
	gPeerInterface.sendHandoverRequest(peer,tran);
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
	L3Frame HandoverCommand(hop->mHexEncodedL3HandoverCommand.c_str());
	LOG(INFO) <<TCH<<" sending handover command";
	TCH->l3sendf(HandoverCommand);
	//TCH->l3sendm(GSM::L3HandoverCommand(
	//	hep->mOutboundCell,
	//	hep->mOutboundChannel,
	//	hep->mOutboundReference,
	//	hep->mOutboundPowerCmd,
	//	hep->mOutboundSynch
	//	));

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
	// This is supposed to time out on successful handover, similar to the early assignment channel transfer..
	GSM::L3Frame *result = TCH->l2recv(outboundT3103.remaining());
	if (result) {
		// If we got here, the handover failed and we just keep running the call.
		L3Message *msg = parseL3(*result);
		LOG(NOTICE) << "failed handover, received " << *result << msg;
		if (msg) { delete msg; }
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

	transaction->teCancel();	// We need to do this immediately in case a reverse handover comes back soon.

	// We need to immediately destroy the dialog.

	LOG(INFO) "timeout following outbound handover; exiting normally";
	//TCH->l2sendp(GSM::HARDRELEASE);	now done by caller.
	return true;
}

};	// namespace Control
