/**@file GSM Radio Resource procedures, GSM 04.18 and GSM 04.08. */

/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012 Range Networks, Inc.
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
#include "TransactionTable.h"
#include "RadioResource.h"
#include "SMSControl.h"
#include "CallControl.h"

#include <GSMLogicalChannel.h>
#include <GSMConfig.h>
#include "../GPRS/GPRSExport.h"

#include <NeighborTable.h>
#include <Peering.h>
#include <SIPEngine.h>


#include <Reporting.h>
#include <Logger.h>
#undef WARNING




using namespace std;
using namespace GSM;
using namespace Control;


extern unsigned allocateRTPPorts();



/**
	Determine the channel type needed.
	This is based on GSM 04.08 9.1.8, Table 9.3 and 9.3a.
	The following is assumed about the global BTS capabilities:
	- We do not support call reestablishment.
	- We do not support GPRS.
	@param RA The request reference from the channel request message.
	@return channel type code, undefined if not a supported service
*/
static
ChannelType decodeChannelNeeded(unsigned RA)
{
	// This code is based on GSM 04.08 Table 9.9.

	unsigned RA4 = RA>>4;
	unsigned RA5 = RA>>5;


	// Answer to paging, Table 9.9a.
	// We don't support TCH/H, so it's wither SDCCH or TCH/F.
	// The spec allows for "SDCCH-only" MS.  We won't support that here.
	// FIXME -- So we probably should not use "any channel" in the paging indications.
	if (RA5 == 0x04) return TCHFType;		// any channel or any TCH.
	if (RA4 == 0x01) return SDCCHType;		// SDCCH
	if (RA4 == 0x02) return TCHFType;		// TCH/F
	if (RA4 == 0x03) return TCHFType;		// TCH/F
	if ((RA&0xf8) == 0x78 && RA != 0x7f) return PSingleBlock1PhaseType;
	if ((RA&0xf8) == 0x70) return PSingleBlock2PhaseType;

	int NECI = gConfig.getNum("GSM.CellSelection.NECI");
	if (NECI==0) {
		if (RA5 == 0x07) return SDCCHType;		// MOC or SDCCH procedures
		if (RA5 == 0x00) return SDCCHType;		// location updating
	} else {
		if (gConfig.getBool("Control.VEA")) {
			// Very Early Assignment
			if (RA5 == 0x07) return TCHFType;		// MOC for TCH/F
			if (RA4 == 0x04) return TCHFType;		// MOC, TCH/H sufficient
		} else {
			// Early Assignment
			if (RA5 == 0x07) return SDCCHType;		// MOC for TCH/F
			if (RA4 == 0x04) return SDCCHType;		// MOC, TCH/H sufficient
		}
		if (RA4 == 0x00) return SDCCHType;		// location updating
		if (RA4 == 0x01) return SDCCHType;		// other procedures on SDCCH
	}

	// Anything else falls through to here.
	// We are still ignoring data calls, LMU.
	return UndefinedCHType;
}


/** Return true if RA indicates LUR. */
static
bool requestingLUR(unsigned RA)
{
	int NECI = gConfig.getNum("GSM.CellSelection.NECI");
	if (NECI==0) return ((RA>>5) == 0x00);
	 else return ((RA>>4) == 0x00);
}


/** Decode RACH bits and send an immediate assignment; may block waiting for a channel for an SOS call. */
static
void AccessGrantResponder(
		unsigned RA, const GSM::Time& when,
		float RSSI, float timingError)
{
	// RR Establishment.
	// Immediate Assignment procedure, "Answer from the Network"
	// GSM 04.08 3.3.1.1.3.
	// Given a request reference, try to allocate a channel
	// and send the assignment to the handset on the CCCH.
	// This GSM's version of medium access control.
	// Papa Legba, open that door...

	gReports.incr("OpenBTS.GSM.RR.RACH.TA.All",(int)(timingError));
	gReports.incr("OpenBTS.GSM.RR.RACH.RA.All",RA);

	// Are we holding off new allocations?
	if (gBTS.hold()) {
		LOG(NOTICE) << "ignoring RACH due to BTS hold-off";
		return;
	}

	// Check "when" against current clock to see if we're too late.
	// Calculate maximum number of frames of delay.
	// See GSM 04.08 3.3.1.1.2 for the logic here.
	static const unsigned txInteger = gConfig.getNum("GSM.RACH.TxInteger");
	static const int maxAge = GSM::RACHSpreadSlots[txInteger] + GSM::RACHWaitSParam[txInteger];
	// Check burst age.
	int age = gBTS.time() - when;
	ChannelType chtype = decodeChannelNeeded(RA);
	int lur = requestingLUR(RA);
	int gprs = (chtype == PSingleBlock1PhaseType) || (chtype == PSingleBlock2PhaseType); 

	// This is for debugging.
	if (GPRS::GPRSDebug && gprs) {
		Time now = gBTS.time();
		LOG(NOTICE) << "RACH" <<LOGVAR(now) <<LOGVAR(chtype) <<LOGVAR(lur) <<LOGVAR(gprs)
		<<LOGVAR(when)<<LOGVAR(age)<<LOGVAR2("TE",timingError)<<LOGVAR(RSSI)<<LOGHEX(RA);
	}
		LOG(INFO) << "**Incoming Burst**"<<LOGVAR(lur)<<LOGVAR(gprs)
		<<LOGVAR(when)<<LOGVAR(age)<<LOGVAR2("TE",timingError)<<LOGVAR(RSSI)<<LOGHEX(RA);

	//LOG(INFO) << "Incoming Burst: RA=0x" << hex << RA << dec
	//	<<LOGVAR(when) <<LOGVAR(age)
	//	<< " delay=" << timingError <<LOGVAR(chtype);
	if (age>maxAge) {
		LOG(WARNING) << "ignoring RACH bust with age " << age;
		// FIXME -- What was supposed to be happening here?
		gBTS.growT3122()/1000;		// Hmmm...
		return;
	}

	// Screen for delay.
	if (timingError>gConfig.getNum("GSM.MS.TA.Max")) {
		LOG(NOTICE) << "ignoring RACH burst with delay="<<timingError<<LOGVAR(chtype);
		return;
	}
	if (chtype == PSingleBlock1PhaseType || chtype == PSingleBlock2PhaseType) {
		// This is a request for a GPRS TBF.  It will get queued in the GPRS code
		// and handled when the GPRS MAC service loop gets around to it.
		// If GPRS is not enabled or is busy, it may just get dropped.
		GPRS::GPRSProcessRACH(RA,when,RSSI,timingError);
		return;
	}

	// Get an AGCH to send on.
	CCCHLogicalChannel *AGCH = gBTS.getAGCH();
	// Someone had better have created a least one AGCH.
	assert(AGCH);
	// Check AGCH load now.
	// (pat) The default value is 5, so about 1.25 second for a system
	// with a C0T0 beacon with only one CCCH.
	if ((int)AGCH->load()>gConfig.getNum("GSM.CCCH.AGCH.QMax")) {
		LOG(CRIT) << "AGCH congestion";
		return;
	}

	// Check for location update.
	// This gives LUR a lower priority than other services.
	// (pat): LUR = Location Update Request Message
	if (requestingLUR(RA)) {
		// Don't answer this LUR if it will not leave enough channels open for other operations.
		if ((int)gBTS.SDCCHAvailable()<=gConfig.getNum("GSM.Channels.SDCCHReserve")) {
			unsigned waitTime = gBTS.growT3122()/1000;
			LOG(CRIT) << "LUR congestion, RA=" << RA << " T3122=" << waitTime;
			const L3ImmediateAssignmentReject reject(L3RequestReference(RA,when),waitTime);
			LOG(DEBUG) << "LUR rejection, sending " << reject;
			AGCH->send(reject);
			return;
		}
	}

	// Allocate the channel according to the needed type indicated by RA.
	// The returned channel is already open and ready for the transaction.
	LogicalChannel *LCH = NULL;
	switch (chtype) {
		case TCHFType: LCH = gBTS.getTCH(); break;
		case SDCCHType: LCH = gBTS.getSDCCH(); break;
#if 0
        // GSM04.08 sec 3.5.2.1.2
        case PSingleBlock1PhaseType:
        case PSingleBlock2PhaseType:
            {
                L3RRMessage *msg = GPRS::GPRSProcessRACH(chtype,
                        L3RequestReference(RA,when),
                        RSSI,timingError,AGCH);
                if (msg) {
                    AGCH->send(*msg);
                    delete msg;
                }
                return;
            }
#endif
		// If we don't support the service, assign to an SDCCH and we can reject it in L3.
		case UndefinedCHType:
			LOG(NOTICE) << "RACH burst for unsupported service RA=" << RA;
			LCH = gBTS.getSDCCH();
			break;
		// We should never be here.
		default: assert(0);
	}

	// Nothing available?
	if (!LCH) {
		// Rejection, GSM 04.08 3.3.1.1.3.2.
		unsigned waitTime = gBTS.growT3122()/1000;
		// TODO: If all channels are statically allocated for gprs, dont throw an alert.
		LOG(CRIT) << "congestion, RA=" << RA << " T3122=" << waitTime;
		const L3ImmediateAssignmentReject reject(L3RequestReference(RA,when),waitTime);
		LOG(DEBUG) << "rejection, sending " << reject;
		AGCH->send(reject);
		return;
	}

	// (pat) gprs todo: Notify GPRS that the MS is getting a voice channel.
	// It may imply abandonment of packet contexts, if the phone does not
	// support DTM (Dual Transfer Mode.)  There may be other housekeeping
	// for DTM phones; haven't looked into it.

	// Set the channel physical parameters from the RACH burst.
	LCH->setPhy(RSSI,timingError,gBTS.clock().systime(when.FN()));
	gReports.incr("OpenBTS.GSM.RR.RACH.TA.Accepted",(int)(timingError));

	// Assignment, GSM 04.08 3.3.1.1.3.1.
	// Create the ImmediateAssignment message.
	// Woot!! We got a channel! Thanks to Legba!
	int initialTA = (int)(timingError + 0.5F);
	if (initialTA<0) initialTA=0;
	if (initialTA>62) initialTA=62;
	const L3ImmediateAssignment assign(
		L3RequestReference(RA,when),
		LCH->channelDescription(),
		L3TimingAdvance(initialTA)
	);
	LOG(INFO) << "sending L3ImmediateAssignment " << assign;
	// (pat) This call appears to block.
	// (david) Not anymore. It got fixed in the trunk while you were working on GPRS.
	// (doug) Adding in a delay to make sure SI5/6 get out before IA.
	sleepFrames(20);
	AGCH->send(assign);

	// On successful allocation, shrink T3122.
	gBTS.shrinkT3122();
}



void* Control::AccessGrantServiceLoop(void*)
{
	while (true) {
		ChannelRequestRecord *req = gBTS.nextChannelRequest();
		if (!req) continue;
		AccessGrantResponder(
			req->RA(), req->frame(),
			req->RSSI(), req->timingError()
		);
		delete req;
	}
	return NULL;
}

void abortInboundHandover(TransactionEntry* transaction, unsigned cause, GSM::LogicalChannel *LCH=NULL)
{
	LOG(DEBUG) << "aborting inbound handover " << *transaction;
	char ind[100];
	unsigned holdoff = gConfig.getNum("GSM.Handover.FailureHoldoff");
	sprintf(ind,"IND HANDOVER_FAILURE %u %u %u", transaction->ID(),cause,holdoff);
	gPeerInterface.sendUntilAck(transaction,ind);

	if (LCH) LCH->send(HARDRELEASE);

	gTransactionTable.remove(transaction);
}



bool Control::SaveHandoverAccess(unsigned handoverReference, float RSSI, float timingError, const GSM::Time& timestamp)
{
	// In this function, we are "BS2" in the ladder diagram.
	// This is called from L1 when a handover burst arrives.

	// We will need to use the transaction record to carry the parameters.
	// We put this here to avoid dealing with the transaction table in L1.
	TransactionEntry *transaction = gTransactionTable.inboundHandover(handoverReference);
	if (!transaction) {
		LOG(ERR) << "no inbound handover with reference " << handoverReference;
		return false;
	}

	if (timingError > gConfig.getNum("GSM.MS.TA.Max")) {
		// Handover failure.
		LOG(NOTICE) << "handover failure on due to TA=" << timingError << " for " << *transaction;
		// RR cause 8: Handover impossible, timing advance out of range
		abortInboundHandover(transaction,8);
		return false;
	}

	LOG(INFO) << "saving handover access for " << *transaction;
	transaction->setInboundHandover(RSSI,timingError,gBTS.clock().systime(timestamp));
	return true;
}




void Control::ProcessHandoverAccess(GSM::TCHFACCHLogicalChannel *TCH)
{
	// In this function, we are "BS2" in the ladder diagram.
	// This is called from the DCCH dispatcher when it gets a HANDOVER_ACCESS primtive.
	// The information it needs was saved in the transaction table by Control::SaveHandoverAccess.


	assert(TCH);
	LOG(DEBUG) << *TCH;

	TransactionEntry *transaction = gTransactionTable.inboundHandover(TCH);
	if (!transaction) {
		LOG(WARNING) << "handover access with no inbound transaction on " << *TCH;
		TCH->send(HARDRELEASE);
		return;
	}

	// clear handover in transceiver
	LOG(DEBUG) << *transaction;
	transaction->channel()->handoverPending(false);
	
	// Respond to handset with physical information until we get Handover Complete.
	int TA = (int)(transaction->inboundTimingError() + 0.5F);
	if (TA<0) TA=0;
	if (TA>62) TA=62;
	unsigned repeatTimeout = gConfig.getNum("GSM.Timer.T3105");
	unsigned sendCount = gConfig.getNum("GSM.Ny1");
	L3Frame* frame = NULL;
	while (!frame && sendCount) {
		TCH->send(L3PhysicalInformation(L3TimingAdvance(TA)),GSM::UNIT_DATA);
		sendCount--;
		frame = TCH->recv(repeatTimeout);
		if (frame && frame->primitive() == HANDOVER_ACCESS) {
			LOG(NOTICE) << "flushing HANDOVER_ACCESS while waiting for Handover Complete";
			delete frame;
			frame = NULL;
		}
	}

	// Timed out?
	if (!frame) {
		LOG(NOTICE) << "timed out waiting for Handover Complete on " << *TCH << " for " << *transaction;
		// RR cause 4: Abnormal release, no activity on the radio path
		abortInboundHandover(transaction,4,TCH);
		return;
	}

	// Screwed up channel?
	if (frame->primitive()!=ESTABLISH) {
		LOG(NOTICE) << "unexpected primitive waiting for Handover Complete on "
			<< *TCH << ": " << *frame << " for " << *transaction;
		delete frame;
		// RR cause 0x62: Message not compatible with protocol state
		abortInboundHandover(transaction,0x62,TCH);
		return;
	}

	// Get the next frame, should be HandoverComplete.
	delete frame;
	frame = TCH->recv();
	L3Message* msg = parseL3(*frame);
	if (!msg) {
		LOG(NOTICE) << "unparsable message waiting for Handover Complete on "
			<< *TCH << ": " << *frame << " for " << *transaction;
		delete frame;
		// RR cause 0x62: Message not compatible with protocol state
		TCH->send(L3ChannelRelease(0x62));
		abortInboundHandover(transaction,0x62,TCH);
		return;
	}
	delete frame;

	L3HandoverComplete* complete = dynamic_cast<L3HandoverComplete*>(msg);
	if (!complete) {
		LOG(NOTICE) << "expecting for Handover Complete on "
			<< *TCH << "but got: " << *msg << " for " << *transaction;
		delete frame;
		// RR cause 0x62: Message not compatible with protocol state
		TCH->send(L3ChannelRelease(0x62));
		abortInboundHandover(transaction,0x62,TCH);
	}
	delete msg;

	// Send re-INVITE to the remote party.
	unsigned RTPPort = allocateRTPPorts();
	SIP::SIPState st = transaction->inboundHandoverSendINVITE(RTPPort);
	if (st == SIP::Fail) {
		abortInboundHandover(transaction,4,TCH);
		return;
	}

	transaction->GSMState(GSM::HandoverProgress);

	while (1) {
		// FIXME - the sip engine should be doing this
		// FIXME - and checking for timeout
		// FIXME - and checking for proceeding (stop sending the resends)
		st = transaction->inboundHandoverCheckForOK();
		if (st == SIP::Active) break;
		if (st == SIP::Fail) {
			LOG(NOTICE) << "received Fail while waiting for OK";
			abortInboundHandover(transaction,4,TCH);
			return;
		}
	}
	st = transaction->inboundHandoverSendACK();
	LOG(DEBUG) << "status of inboundHandoverSendACK: " << st << " for " << *transaction;

	// Send completion to peer BTS.
	char ind[100];
	sprintf(ind,"IND HANDOVER_COMPLETE %u", transaction->ID());
	gPeerInterface.sendUntilAck(transaction,ind);

	// Update subscriber registry to reflect new registration.
	if (transaction->SRIMSI().length() && transaction->SRCALLID().length()) {
		gSubscriberRegistry.addUser(transaction->SRIMSI().c_str(), transaction->SRCALLID().c_str());
	}

	// The call is running.
	LOG(INFO) << "succesful inbound handover " << *transaction;
	transaction->GSMState(GSM::Active);
	callManagementLoop(transaction,TCH);
}


void Control::HandoverDetermination(const L3MeasurementResults& measurements, SACCHLogicalChannel* SACCH)
{
	// This is called from the SACCH service loop.

	// Valid measurements?
	if (measurements.MEAS_VALID()) return;

	// Got neighbors?
	unsigned N = measurements.NO_NCELL();
	if (N==0) return;

	if (N == 7) {
		LOG(DEBUG) << "neighbor cell information not available";
		return;
	}

	// Is our current signal OK?
	int myRxLevel = measurements.RXLEV_SUB_SERVING_CELL_dBm();
	int localRSSIMin = gConfig.getNum("GSM.Handover.LocalRSSIMin");
	LOG(DEBUG) << "myRxLevel=" << myRxLevel << " dBm localRSSIMin=" << localRSSIMin << " dBm";
	if (myRxLevel > localRSSIMin) return;
	

	// Look at neighbor cell rx levels
	int best = 0;
	int bestRxLevel = measurements.RXLEV_NCELL_dBm(best);
	for (unsigned int i=1; i<N; i++) {
		int thisRxLevel = measurements.RXLEV_NCELL_dBm(i);
		if (thisRxLevel>bestRxLevel) {
			bestRxLevel = thisRxLevel;
			best = i;
		}
	}

	// Does the best exceed the current by more than the threshold?
	int threshold = gConfig.getNum("GSM.Handover.ThresholdDelta");
	LOG(DEBUG) << "myRxLevel=" << myRxLevel << " dBm, best neighbor=" <<
		bestRxLevel << " dBm, threshold=" << threshold << " dB";
	if (bestRxLevel < (myRxLevel + threshold)) return;


	// OK.  So we will initiate a handover.
	LOG(DEBUG) << measurements;
	int BCCH_FREQ_NCELL = measurements.BCCH_FREQ_NCELL(best);
	int BSIC = measurements.BSIC_NCELL(best);
	char* peer = gNeighborTable.getAddress(BCCH_FREQ_NCELL,BSIC);
	if (!peer) {
		LOG(CRIT) << "measurement for unknown neighbor BCCH_FREQ_NCELL " << BCCH_FREQ_NCELL << " BSIC " << BSIC;
		return;
	}
	if (gNeighborTable.holdingOff(peer)) {
		LOG(NOTICE) << "skipping handover to " << peer << " due to holdoff";
		return;
	}

	// Find the transaction record.
	TransactionEntry* transaction = gTransactionTable.findBySACCH(SACCH);
	if (!transaction) {
		LOG(ERR) << "active SACCH with no transaction record: " << *SACCH;
		return;
	}
	if (transaction->GSMState() != GSM::Active) {
		LOG(DEBUG) << "skipping handover for transaction " << transaction->ID()
			<< " due to state " << transaction->GSMState();
		return;
	}
	LOG(INFO) << "preparing handover of " << transaction->ID()
		<< " to " << peer << " with downlink RSSI " << bestRxLevel << " dbm";

	// The handover reference will be generated by the other BTS.
	// We don't set the handover reference or state until we get RSP HANDOVER.

	// Form and send the message.
	string msg = string("REQ HANDOVER ") + transaction->handoverString();
	struct sockaddr_in peerAddr;
	if (!resolveAddress(&peerAddr,peer)) {
		LOG(ALERT) << "cannot resolve peer address " << peer;
		return;
	}
	gPeerInterface.sendMessage(&peerAddr,msg.c_str());
}




void Control::PagingResponseHandler(const L3PagingResponse* resp, LogicalChannel* DCCH)
{
	assert(resp);
	assert(DCCH);
	LOG(INFO) << *resp;

	// If we got a TMSI, find the IMSI.
	L3MobileIdentity mobileID = resp->mobileID();
	if (mobileID.type()==TMSIType) {
		char *IMSI = gTMSITable.IMSI(mobileID.TMSI());
		if (IMSI) {
			mobileID = L3MobileIdentity(IMSI);
			// (pat) Whenever the MS RACHes, we need to alert the SGSN.
			// Not sure this is necessary in this particular case, but be safe.
			GPRS::GPRSNotifyGsmActivity(IMSI);
			free(IMSI);
		} else {
			// Don't try too hard to resolve.
			// The handset is supposed to respond with the same ID type as in the request.
			// This could be the sign of some kind of DOS attack.
			LOG(CRIT) << "Paging Reponse with non-valid TMSI";
			// Cause 0x60 "Invalid mandatory information"
			DCCH->send(L3ChannelRelease(0x60));
			return;
		}
	}
	else if(mobileID.type()==IMSIType){
		//Cause the tmsi table to be touched
		gTMSITable.TMSI(resp->mobileID().digits());
	}

	// Delete the Mobile ID from the paging list to free up CCCH bandwidth.
	// ... if it was not deleted by a timer already ...
	gBTS.pager().removeID(mobileID);

	// Find the transction table entry that was created when the phone was paged.
	// We have to look up by mobile ID since the paging entry may have been
	// erased before this handler was called.  That's too bad.
	// HACK -- We also flush stray transactions until we find what we 
	// are looking for.
	TransactionEntry* transaction = gTransactionTable.answeredPaging(mobileID);
	if (!transaction) {
		LOG(WARNING) << "Paging Reponse with no transaction record for " << mobileID;
		// Cause 0x41 means "call already cleared".
		DCCH->send(L3ChannelRelease(0x41));
		return;
	}
	LOG(INFO) << "paging reponse for transaction " << *transaction;
	// Set the transaction channel.
	transaction->channel(DCCH);
	// We are looking for a mobile-terminated transaction.
	// The transaction controller will take it from here.
	switch (transaction->service().type()) {
		case L3CMServiceType::MobileTerminatedCall:
			MTCStarter(transaction, DCCH);
			return;
		case L3CMServiceType::MobileTerminatedShortMessage:
			MTSMSController(transaction, DCCH);
			return;
		case L3CMServiceType::TestCall:
			TestCall(transaction, DCCH);
			return;
		default:
			// Flush stray MOC entries.
			// There should not be any, but...
			LOG(ERR) << "non-valid paging-state transaction: " << *transaction;
			gTransactionTable.remove(transaction);
			// FIXME -- Send a channel release on the DCCH.
	}
}



void Control::AssignmentCompleteHandler(const L3AssignmentComplete *confirm, TCHFACCHLogicalChannel *TCH)
{
	// The assignment complete handler is used to
	// tie together split transactions across a TCH assignment
	// in non-VEA call setup.

	assert(TCH);
	assert(confirm);
	LOG(DEBUG) << *confirm;

	// Check the transaction table to know what to do next.
	TransactionEntry* transaction = gTransactionTable.find(TCH);
	if (!transaction) {
		LOG(WARNING) << "No transaction matching channel " << *TCH << " (" << TCH << ").";
		throw UnexpectedMessage();
	}
	LOG(INFO) << "service="<<transaction->service().type();

	// These "controller" functions don't return until the call is cleared.
	switch (transaction->service().type()) {
		case L3CMServiceType::MobileOriginatedCall:
			MOCController(transaction,TCH);
			break;
		case L3CMServiceType::MobileTerminatedCall:
			MTCController(transaction,TCH);
			break;
		default:
			LOG(WARNING) << "unsupported service " << transaction->service();
			throw UnsupportedMessage(transaction->ID());
	}
	// If we got here, the call is cleared.
}








void Pager::addID(const L3MobileIdentity& newID, ChannelType chanType,
		TransactionEntry& transaction, unsigned wLife)
{
	transaction.GSMState(GSM::Paging);
	transaction.setTimer("3113",wLife);
	// Add a mobile ID to the paging list for a given lifetime.
	ScopedLock lock(mLock);
	// If this ID is already in the list, just reset its timer.
	// Uhg, another linear time search.
	// This would be faster if the paging list were ordered by ID.
	// But the list should usually be short, so it may not be worth the effort.
	for (PagingEntryList::iterator lp = mPageIDs.begin(); lp != mPageIDs.end(); ++lp) {
		if (lp->ID()==newID) {
			LOG(DEBUG) << newID << " already in table";
			lp->renew(wLife);
			mPageSignal.signal();
			return;
		}
	}
	// If this ID is new, put it in the list.
	mPageIDs.push_back(PagingEntry(newID,chanType,transaction.ID(),wLife));
	LOG(INFO) << newID << " added to table";
	mPageSignal.signal();
}


unsigned Pager::removeID(const L3MobileIdentity& delID)
{
	// Return the associated transaction ID, or 0 if none found.
	LOG(INFO) << delID;
	ScopedLock lock(mLock);
	for (PagingEntryList::iterator lp = mPageIDs.begin(); lp != mPageIDs.end(); ++lp) {
		if (lp->ID()==delID) {
			unsigned retVal = lp->transactionID();
			mPageIDs.erase(lp);
			return retVal;
		}
	}
	return 0;
}



unsigned Pager::pageAll()
{
	// Traverse the full list and page all IDs.
	// Remove expired IDs.
	// Return the number of IDs paged.
	// This is a linear time operation.

	ScopedLock lock(mLock);

	// Clear expired entries.
	PagingEntryList::iterator lp = mPageIDs.begin();
	while (lp != mPageIDs.end()) {
		bool expired = lp->expired();
		bool defunct = gTransactionTable.find(lp->transactionID()) == NULL;
		if (!expired && !defunct) ++lp;
		else {
			LOG(INFO) << "erasing " << lp->ID();
			// Non-responsive, dead transaction?
			gTransactionTable.removePaging(lp->transactionID());
			// remove from the list
			lp=mPageIDs.erase(lp);
		}
	}

	LOG(INFO) << "paging " << mPageIDs.size() << " mobile(s)";

	// Page remaining entries, two at a time if possible.
	// These PCH send operations are non-blocking.
	lp = mPageIDs.begin();
	while (lp != mPageIDs.end()) {
		// FIXME -- This completely ignores the paging groups.
		// HACK -- So we send every page twice.
		// That will probably mean a different Pager for each subchannel.
		// See GSM 04.08 10.5.2.11 and GSM 05.02 6.5.2.
		const L3MobileIdentity& id1 = lp->ID();
		ChannelType type1 = lp->type();
		++lp;
		if (lp==mPageIDs.end()) {
			// Just one ID left?
			LOG(DEBUG) << "paging " << id1;
			gBTS.getPCH(0)->send(L3PagingRequestType1(id1,type1));
			gBTS.getPCH(0)->send(L3PagingRequestType1(id1,type1));
			break;
		}
		// Page by pairs when possible.
		const L3MobileIdentity& id2 = lp->ID();
		ChannelType type2 = lp->type();
		++lp;
		LOG(DEBUG) << "paging " << id1 << " and " << id2;
		gBTS.getPCH(0)->send(L3PagingRequestType1(id1,type1,id2,type2));
		gBTS.getPCH(0)->send(L3PagingRequestType1(id1,type1,id2,type2));
	}

	return mPageIDs.size();
}

size_t Pager::pagingEntryListSize()
{
	ScopedLock lock(mLock);
	return mPageIDs.size();
}

void Pager::start()
{
	if (mRunning) return;
	mRunning=true;
	mPagingThread.start((void* (*)(void*))PagerServiceLoopAdapter, (void*)this);
}



void* Control::PagerServiceLoopAdapter(Pager *pager)
{
	pager->serviceLoop();
	return NULL;
}

void Pager::serviceLoop()
{
	while (mRunning) {

		LOG(DEBUG) << "Pager blocking for signal";
		mLock.lock();
		while (mPageIDs.size()==0) mPageSignal.wait(mLock);
		mLock.unlock();

		// page everything
		pageAll();

		// Wait for pending activity to clear the channel.
		// This wait is what causes PCH to have lower priority than AGCH.
		unsigned load = gBTS.getPCH()->load();
		LOG(DEBUG) << "Pager waiting for " << load << " multiframes";
		if (load) sleepFrames(51*load);
	}
}



void Pager::dump(ostream& os) const
{
	ScopedLock lock(mLock);
	PagingEntryList::const_iterator lp = mPageIDs.begin();
	while (lp != mPageIDs.end()) {
		os << lp->ID() << " " << lp->type() << " " << lp->expired() << endl;
		++lp;
	}
}



// vim: ts=4 sw=4
