/*
* Copyright 2011 Range Networks, Inc.
* All Rights Reserved.
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

#define TBF_IMPLEMENTATION 1
#include "MSInfo.h"
#include "TBF.h"
#include "FEC.h"
#include "RLCMessages.h"
#include "BSSG.h"
#include "RLCEngine.h"

namespace GPRS {

typedef SGSN::GprsSgsnDownlinkPdu DownlinkQPdu;

static bool SendExtraTA = 0;// DEBUG: Send an extra TA message
							// after an Immediate Assignment just to prove it is working.
							// 4-24-2012, turned it back off, does not help CS-4 bug.

static bool T3168Behavior = 1;	// Dont send downlink assignments while t3168 running.

static int configTbfRetry() {
	return gConfig.getNum("GPRS.TBF.Retry");
}

bool MsgTransaction::mtMsgPending(MsgTransactionType mttype)
{
	// The multiple tests here are overkill.
	return mtMsgExpectedBits.isSet(mttype) &&
		mtExpectedAckBSN[mttype].valid() &&
		mtExpectedAckBSN[mttype] + BSNLagTime >= gBSNNext;
}

// Is any message pending?
bool MsgTransaction::mtMsgPending()
{
	// We are not 'waiting' until the expectedAckBSN is valid, because:
	// 1. the first time through the TBF state machine's logic,
	// it tests this function before the msgAckTime has been set.
	// 2. In RLCEngine.cpp, we wait on an optional RRBP reservation,
	// so we dont want to wait forever if the first one does not get
	// its reservation and expectedAckBSN is still invalid.

	//GPRSLOG(1) << "mtMsgPending:" <<LOGVAR(mtExpectedAckBSN)
		//<<LOGVAR(gBSNNext) <<" cmp=" << (mtExpectedAckBSN >= gBSNNext);
	// Wait lag-time extra blocks beyond when we expect
	// it to make sure we dont launch another sendassignment
	// at the same time it is being received.
	// Note that the operators here are in class RLCBSN_t and do modulo logic.
	if (mtMsgExpectedBits) {
		for (int i = 0; i < MsgTransMax; i++) {
			if (mtExpectedAckBSN[i].valid() && mtExpectedAckBSN[i] + BSNLagTime >= gBSNNext) return true;
		}
	}
	// We could probably clear mtMsgExpectedBits at this point...
	return false;
}

TBF *TBF::newUpTBF(MSInfo *ms,RLCMsgChannelRequestDescriptionIE &mCRD,
	uint32_t tlli,
	bool onRach)	// If true, request on RACH, else request on uplink ack-nack.
{
	// Changed 5-20-2012.  There is only ONE uplink tbf,
	// and assigning a new one just changes the priority of the existing.
	// Note that there is also a GPRS optional feature to allow multiple simultaneous
	// separate TBFs with separate RLCs, but we dont support it.
	TBF *activeTbf = 0;
	ms->msN3101 = 0;	// I'm going to reset this no matter what happens below.
	if (ms->msCountActiveTBF(RLCDir::Up, &activeTbf) >= 1) {
		//GLOG(INFO) << "MS denied additional uplink TBF because GPRS.TBF.Up.Max limit exceeded";
		if (activeTbf->mtGetState() == TBFState::DataFinal) {
			// In this state this uplink TBF has already completed and we have sent the
			// uplinkAckNack message.  We now set the TBF_EST field in that message
			// so the MS may respond to the RRBP reservation with a PacketResourceRequest.
			// Doesn't really matter whether that is the case or not - in this state,
			// go ahead and start a new uplink TBF.
		} else if (activeTbf->isTransmitting()) {
			// When the MS requests a second TBF when one is already running,
			// it means it has a new PDU with a different priority or QoS (throughput.)
			// See 44.060 8.1.1.1.2 and 9.5.
			// We are required to reissue the TBF assignment within timer T3168.
			// Originally I just sent a new PacketUplinkAssignment, but that does not always,
			// we have to end the TBF and restart it.
			// Maybe if the new priority/QoS is lower, then just reissuing a new
			// PacketUplinkAssignment on the current TBF would work, but I'm just
			// going to reissue the TBF.
			// This code waits until the current PDU finishes and then terminates the transfer,
			// which implies that we expect the MS to comply by setting the uplink MAC header Countdown Value to 0
			// within the T3168 grace period.
			// FIXME: Temporarily provide a way to disable this in case it has a bug:
			if (gConfig.getBool("GPRS.Reassign.Enable")) {
				activeTbf->mtPerformReassign = true;
			}
			return NULL;
		} else {
			// The activeTbf is in the midst of being issued a new assignment.
			if (tlli != activeTbf->mtTlli) {
				// The new uplink tbf has a different tlli.
				// This case happens only for the AttachComplete message when it is
				// sent by the MS in an uplink tbf requested in the downlinkacknack
				// for the AttachAccept message.  So check for this special case:
				if (tlli != ms->msTlli && tlli != ms->msOldTlli) {
					// Should not happen because the internal SGSN provides
					// the newTlli as an alias with the AttachAccept message.
					GLOG(ERR) << "uplink TBF request"<<LOGVAR(tlli)<<"does not match MS:"<<ms;
					// But go ahead and do it anyway.
				}
				GPRSLOG(1) << "Changing uplink TBF tlli to"<<LOGVAR(tlli)<<ms;
				activeTbf->mtTlli = tlli;
			} else {
				// Just ignore this duplicate uplink request.
				// TODO: We can infer the MS mode if this arrives on RACH.
				GPRSLOG(1) << "MS denied second uplink TBF" << activeTbf;
			}
			return NULL;
		}
	}

	TBF *tbf = (TBF*) new RLCUpEngine(ms,mCRD.mRLCOctetCount);
	tbf->mtTlli = tlli;
	tbf->mtUnAckMode = mCRD.mRLCMode;
	if (mCRD.mLLCPDUType == 0) { // 0 == LLC PDU is SACK or ACK, 1 == its not
		GLOG(ERR) << "Uplink PDU LLC SACK request"<<ms;
	}
	tbf->mtSetState(TBFState::DataReadyToConnect);
	return tbf;
}

ChannelCodingType TBF::mtChannelCoding() const
{	// Return 0 - 3 for CS-1 or CS-4 for data transfer.
	assert(mtChannelCodingMax >= ChannelCodingCS1 && mtChannelCodingMax <= ChannelCodingCS4);
	ChannelCodingType result;
	if (mtChannelCodingMax == ChannelCodingCS1) {
		result = ChannelCodingCS1;	// Locked to lowest speed.  No need to query the MS RSSI.
	} else {
		ChannelCodingType dynamicCS = mtMS->msGetChannelCoding(mtDir);
		result = min(mtChannelCodingMax,dynamicCS);
	}
	mtMS->msChannelCoding.addPoint((int)result);
	return result;
}


TBF::TBF(MSInfo *wms, RLCDirType wdir)
	:  mtState(TBFState::Unused), mtDebugId(++Stats.countTBF), mtMS(wms), mtDir(wdir), mtTFI(-1)
{
	gReports.incr("GPRS.TBF");
	RN_MEMCHKNEW(TBF)
	mtChannelCodingMax = ChannelCodingMax;	// This may be changed by caller.
	mtCCMin = mtCCMax = (ChannelCodingType)-1;
	gL2MAC.macAddTBF(this);
	mtMS->msAddTBF(this);
	// Reset these counters to avoid killing the TBF before it has a chance to do anything:
	mtMS->msTalkUpTime.setNow();
	mtMS->msTalkDownTime.setNow();
	mtMS->msCountTbfs++;
	// Reset these counts just to validate them.
	//mtPersistLastUse.setNow();
	//mtPersistKeepAlive.setNow();
}

TBF::~TBF() { RN_MEMCHKDEL(TBF) }		// housekeeping handled by mtDelete()

const char *TBF::tbfid(bool verbose)
{
	static char buf[30];
	int n1 = sprintf(buf, "%c%d", (mtDir==RLCDir::Up ? 'U' : 'D'), mtDebugId);
	if (verbose) {
		// Just add the seconds part of the time string, which looks like "HH:MM:SS.T".
		const char *ts = strrchr(timestr().c_str(),':');
		if (ts) { n1 += sprintf(buf+n1," %s",ts+1); }
		// Add the TLLI.
		//sprintf(buf+n1," MS#%d,%x",mtMS->msDebugId,mtMS->msTlli);
		sprintf(buf+n1," MS#%d",mtMS->msDebugId);
	}
	return (const char *)buf;
}

//void TBF::setRadData(RadData &wRD)
//{
//	mtMS->msSetRadData(wRD);
//	if (wRD.mRSSI < mtLowRSSI) { mtLowRSSI = wRD.mRSSI; }
//}

void TBF::mtDelete(bool forever)
{
	devassert(! mtAttached);
	//mtDetach();				// Did this already, but be safe and call again.
	mtMS->msForgetTBF(this);
	gL2MAC.macForgetTBF(this,forever);		// TBF destruction happens in here.
}

void MsgTransaction::text(std::ostream &os) const
{
	os << LOGHEX(mtMsgExpectedBits);
	os << LOGHEX(mtMsgAckBits);
	for (int i = 0; i < MsgTransMax; i++) {
		if (mtExpectedAckBSN[i].valid() && mtExpectedAckBSN[i] + BSNLagTime >= gBSNNext) {
			os << " mtExpectedAckBSN["<<i<<"]="<<mtExpectedAckBSN[i];
		}
	}
}

std::string TBF::tbfDump(bool verbose) const
{
	std::ostringstream os;
	//int tn = -1, usf = 0;
	//PDCHL1FEC *pacch = mtMS->msPacch;
	//if (mtDir == RLCDir::Up && pacch) {
	//	tn = pacch->TN();
	//	usf = (tn >= 0) ? (int)mtMS->msUSFs[tn] : -1;
	//}
	// os << LOGVAR(pacch) << LOGVAR(usf);
	os << this		// Dumps the operator<< value, which is sprintf(TBF#%d,mtDebugId)
		<< LOGVAR(mtMS)
		//<< " mtDir=" << RLCDir::name(mtDir)
		<< LOGVAR(mtDir)
		<< "\n";
	
	os << "\t"; mtMS->msDumpChannels(os); os << "\n";
	if (0) {
		PDCHL1Uplink *up; PDCHL1Downlink *down;
		os << "\t";
		RN_FOR_ALL(PDCHL1DownlinkList_t,mtMS->msPCHDowns,down) {
			os << format(" down%d:%d",down->ARFCN(),down->TN());
		}
		RN_FOR_ALL(PDCHL1UplinkList_t,mtMS->msPCHUps,up) {
			int tn = up->TN();
			os << format(" up%d:%d usf=%d",up->ARFCN(),tn,(int)mtMS->msUSFs[tn]);
		}
	}

	os << "\t" 
		<< LOGVAR2("mtState=",TBFState::name(mtState))
		<< LOGVAR(mtAttached)
		<< LOGVAR(mtTFI)
		<< LOGHEX(mtTlli);
		if (mtDir == RLCDir::Down) { os <<" size="<<engineDownPDUSize(); }
	if (!verbose) return os.str();
	os << "\n\t";
		MsgTransaction::text(os);
		os << "\n";
	os << "\t";
		//os << LOGVARRANGE("ChannelCoding",mtChannelCoding(),mtCCMin,mtCCMax); //moved to SignalQuality.
		os << LOGVAR(mtUnAckMode);
		os <<LOGVAR2("OnCCCH",mtAssignmentOnCCCH);
		if (mtAssignCounter) os << LOGVAR(mtAssignCounter);
		if (mtReassignCounter) os << LOGVAR(mtReassignCounter);
		if (mtPerformReassign) os << LOGVAR(mtPerformReassign);
		if (mtMS->msN3101) os << LOGVAR2("N3101",mtMS->msN3101);
		if (mtN3103) os << LOGVAR2("N3103",mtN3103);
		if (mtN3105) os << LOGVAR2("N3105",mtN3105);
		if (mtDeadTime.valid()) os << LOGVAR2("deadTime",- mtDeadTime.elapsed());
		if (mtMS->msT3193.active()) { os<<LOGVAR2("msT3193",mtMS->msT3193.remaining()); }
		if (mtMS->msT3168.active()) { os<<LOGVAR2("msT3168",mtMS->msT3168.remaining()); }
		if (mtDescription.size()) os <<" descr="<<mtDescription;
		os <<"\n";
	mtMS->msDumpCommon(os);
	mtMS->dumpSignalQuality(os);

	os << "\t"; engineDump(os);
	unsigned total, unique, grants;
	engineGetStats(&total,&unique,&grants);
	// Note that this statistic is off by the small number of blocks
	// that have been sent but not yet acknowledged.
	os << "\n\t blocks:" << LOGVAR(total) << LOGVAR(unique) << LOGVAR(grants);

	//float efficiency = 1.0 * slotsused / slotstotal;
	// os << LOGVAR(efficiency);
	//os << "\n";
	return os.str();
}

static void tbfDumpAll()
{
	if (GPRSDebug & 2) {
		GPRSLOG(2) <<"TBF DUMP:\n";
		TBF *tbf;
		RN_MAC_FOR_ALL_TBF(tbf) { GPRSLOG(2) << tbf->tbfDump(true) << "\n"; }
	}
}

// Can never have too many const in a language, thats what I always say.
RLCDownEngine *TBF::getDownEngine()
{
	return (mtDir == RLCDir::Down) ? dynamic_cast<RLCDownEngine *>(this) : NULL;
}
//RLCDownEngine const *TBF::getDownEngine() const
//{
//	return (mtDir == RLCDir::Down) ? dynamic_cast<RLCDownEngine const*>(this) : NULL;
//}


// Release resources for this TBF: TFI, and if we were the last user of this USF,
// release that as well.
// Note this code is overkill at the moment, because there is only
// one tfi list shared among all channels.
// This does not release the channel assignment.
void TBF::mtDetach()
{
	if (mtAttached) {
		mtAttached = false; // Must do this before calling msCleanUSFs()
		// Set the state to the transitory Deleting state, so that the callers
		// who wade through the TBF lists will ignore this one now.
		mtSetState(TBFState::Deleting);
		mtFreeTFI();
		if (mtDir == RLCDir::Up) { mtMS->msCleanUSFs(); }
	}
}

// TODO: The TBF needs to detach and then re-attach.
void TBF::mtDeReattach()
{
	if (mtAttached) {
		mtAttached = false; // Must do this before calling msCleanUSFs()
		mtSetState(TBFState::DataReadyToConnect);
		mtFreeTFI();
		if (mtDir == RLCDir::Up) { mtMS->msCleanUSFs(); }
	}
}

uint32_t TBF::mtGetTlli()
{
	uint32_t tlli = mtTlli;
	if (gFixConvertForeignTLLI) {
		if ((tlli & 0xc0000000) == 0x80000000) {	// Is it a foreign tlli?
			tlli |= TLLI_LOCAL_BIT;
			GPRSLOG(1) << "*** Converting foreign tlli to local:"<<LOGHEX(tlli);
		}
	}
	return tlli;
}

// 7-25: Neither the Blackberry nor Multitech like this message.
// Maybe  because I include the downlink assignment when there is no downlink TBF?
static bool sendTimeslotReconfigure(
	PDCHL1FEC *pacch,	// The PACCH channel.
	TBF *tbf,	// This is an uplink tbf.
	std::ostream *os)	// for testing - if set, print out the message instead of sending it.
{
	RLCMsgPacketTimeslotReconfigure *msg = new RLCMsgPacketTimeslotReconfigure(tbf);

	//msg->setTimingAdvance(ms->msGetTA());

	// This will set mtExpectedAckBSN if the message is sent.
	// If the MS gets this message, it will send Packet Control Acknowledgment
	// in the block specified by RRBP.
	if (os) {
		msg->text(*os);
		delete msg;
		return true;
	} else {
		GPRSLOG(1) << "GPRS sendReassignment "<<tbf<<" sending:" << msg;
		tbfDumpAll();
		// This will increment the counter if the message is really sent.
		return pacch->downlink()->
			send1MsgFrame(tbf,msg,2,MsgTransReassign,&tbf->mtReassignCounter);
	}
}

static bool sendAssignmentPacch(
	PDCHL1FEC *pacch,	// The PACCH channel.
	TBF *tbf,
	bool isNewAssignment,	// If false, it is a reassignment.
	MsgTransactionType msgstate,
	std::ostream *os)	// for testing - if set, print out the message instead of sending it.
{
	MSInfo *ms = tbf->mtMS;
	// Send assignment message on PACCH.
	// A reassignment message is identical to an assignment message except
	// for the ControlAck bit, however, now we use timeslot reconfigure
	// for reassignments.
	RLCDownlinkMessage *msg;
	if (tbf->mtDir == RLCDir::Up) {
		RLCMsgPacketUplinkAssignment *ulmsg = new RLCMsgPacketUplinkAssignment(tbf);

		RLCMsgPacketUplinkAssignmentDynamicAllocationElt *dynelt;
		dynelt = ulmsg->setDynamicAllocation();
		dynelt->setUplinkTFI(tbf->mtTFI);
		dynelt->setFrom(tbf,MultislotSymmetric);
		msg = ulmsg;
	} else {
		RLCMsgPacketDownlinkAssignment *dlmsg =
			new RLCMsgPacketDownlinkAssignment(tbf,isNewAssignment);
		msg = dlmsg;
	}

	// todo: stop timers for ms?
	// todo: start timers?

	msg->setTimingAdvance(ms->msGetTA());
	msg->setTLLI(tbf->mtGetTlli());

	// This will set mtExpectedAckBSN if the message is sent.
	// If the MS gets this message, it will send Packet Control Acknowledgment
	// in the block specified by RRBP.
	if (os) {
		msg->text(*os);
		delete msg;
		return true;
	} else {
		unsigned *pcounter = NULL;
		switch (msgstate) {
		case MsgTransAssign1:
		case MsgTransAssign2:
			GPRSLOG(1) << "GPRS sendAssignment "<<tbf<<" sending:" << msg;
			pcounter = &tbf->mtAssignCounter;
			break;
		case MsgTransReassign:
			GPRSLOG(1) << "GPRS sendReassignment "<<tbf<<" sending:" << msg;
			pcounter = &tbf->mtReassignCounter;
			break;
		default: devassert(0);
		}
		tbfDumpAll();
		// This will increment the counter if the message is really sent.
		devassert(pcounter);
		return pacch->downlink()->send1MsgFrame(tbf,msg,2,msgstate,pcounter);
	}
}

void sendAssignmentCcch(
	PDCHL1FEC *pacch,	// The PACCH channel.
	TBF *tbf,
	std::ostream *os)	// for testing - if set, print out the message instead of sending it.
{
	static GSM::CCCHLogicalChannel *ourAGCH = 0;
	MSInfo *ms = tbf->mtMS;
		// Send a message on CCCH.
		// TODO FIXME:
		// GSM 44.60 5.5.1.5 Describes the conditions under which DRX should be applied.
		// The MS sends the DRX info to the SGSN in the Attach message.
		// None of that implemented yet.
		bool drxmode = false;

	// BUG: The returned AGCH time is not monotonically increasing.  The bug traces all
	// the way back down that the getAGCH call is not, in fact, returning the next one.
	// I am guessing that the choice of CCCH has a bug that does not really
	// return the next CCCH slot because of the way the idle frames are handled.
	// Anyway, this screws up sendAssignment; see todo.txt.
	// To try to fix this, we will use just one AGCH, ever, to try to get our
	// messages delivered in order.

		// TODO: We need to use ourAGCH for unattached MS,
		// but we could select a different AGCH for every attached MS.
		if (ourAGCH == 0) { ourAGCH = gBTS.getAGCH(); }

		// Make a reservation for the poll response.
		RLCBSN_t resBSN = 0;
		// TODO: Fix this.  The DRX mode is sent to us by the MS I think in the
		// initial attach message which went to the SGSN.
		if (gFixDRX && tbf->mtCcchAssignCounter >= (unsigned)gFixDRX) {
			// We may have lost contact with the MS because it is in DRX mode.
			// Send the message on all 3 AGCH channels twice - we need
			// to saturate both 51 multi-frames because there
			// are 6 paging channels.  And we just hope no other
			// AGCH messages are in the way.
			// We need to increase the reservation to make sure it is beyond
			// the CCCH in the second 51 multiframe.
			drxmode = true;
		}
		GSM::CCCHLogicalChannel *currentAGCH = drxmode ? gBTS.getPCH(0) : ourAGCH;

		resBSN = pacch->makeCCCHReservation(currentAGCH,RLCBlockReservation::ForPoll,tbf,NULL,drxmode,MsgTransAssign1);
		if (! resBSN.valid()) {
			// We will try again later.
			return /*false*/;	// We did not use the packet channel downlink.
		}
		// TODO: mtSetAckExpected function should be moved into the makeReservation code.
		// We have to wait for the time whether we sent the poll or not.
		tbf->mtSetAckExpected(resBSN,MsgTransAssign1);

		// The RequestReference is not used for this type of downlink assignment,
		// which contains TLLI instead; we have to set RequestReference to a number that
		// cannot possibly be confused with any valid value, ie, somewhere in the future,
		// at the time this is sent.
		RLCBSN_t impossibleBSN = resBSN + 1000;
		GSM::Time impossible(BSN2FrameNumber(impossibleBSN),0);	// TN not used.
		// The downlink bit documentation is goofed up in GSM 4.18: They clarified it
		// in GSM 44.018 sec 10.5.2.25b: This bit is 1 only for downlink TBFs.
		L3ImmediateAssignment amsg(
			L3RequestReference(0,impossible),
			pacch->packetChannelDescription(),
			GSM::L3TimingAdvance(ms->msGetTA()),
			true,tbf->mtDir == RLCDir::Down);	// tbf, downlink

		L3IAPacketAssignment *pa = amsg.packetAssign();
		// DEBUG: I tried taking out power:
		// 12-16: Put power back in:
		pa->setPacketPowerOptions(GetPowerAlpha(),GetPowerGamma());
		if (gFixIAUsePoll) {
			pa->setPacketPollTime(resBSN.FN());
		}
		if (tbf->mtDir == RLCDir::Up) {
			// This assignment type is only for 1-phase access, which we do not support.
			// An uplink immediate assignment can only be in response to a RACH
			// and is identified by the request reference.  Instead we send a
			// one-block uplink assignment to get a packet resource request,
			// then do the uplink TBF on PACCH based on that.
			devassert(0);
			// There is only one USF (ie, no multi-slot).
			//int usf = ms->msUSFs[pacch->uplink()->TN()];
			int usf = ms->msUSFs[pacch->TN()];
			devassert(USF_VALID(usf));
			pa->setPacketUplinkAssignDynamic(tbf->mtTFI, tbf->mtChannelCoding(), usf);
		} else {
			pa->setPacketDownlinkAssign(tbf->mtGetTlli(), tbf->mtTFI,
				tbf->mtChannelCoding(), tbf->mtUnAckMode, 1);
		}

		if (os) {
			amsg.text(*os);
		} else {
			GPRSLOG(1) << "GPRS sendAssignment "<<tbf<<" sending L3ImmediateAssignment:"
				<<LOGVAR2("agchload",currentAGCH->load()) << amsg;
			LOGWATCHF("%s CCCH load=%d\n", tbf->tbfid(1), currentAGCH->load());
			tbfDumpAll();
			tbf->mtAssignCounter++;

			// FIXME TODO:
			if (drxmode) {
				// Send the message on a paging channel.
				// This code assumes the paging channel setup described
				// in the class L3ControlChannelDescription.
				// This code should be replaced with real paging channels.
				// Our BS_PA_MFRMS is 2, so send the paging message twice to make
				// sure it is sent in all (2) available paging 51-multiframes.
				currentAGCH->send(amsg);
				currentAGCH->send(amsg);
				// DEBUG: try just plastering these messages out there.
				//for (int ii = 0; ii < 6; ii++) {
				//	GSM::CCCHLogicalChannel *anyagch = gBTS.getAGCH();
				//	anyagch->send(amsg);
				//}
			} else {
				currentAGCH->send(amsg);	// send() takes care of converting it to a BitVector.
			}
			tbf->mtCcchAssignCounter++;
		}
}

// Return true if we send a block on the downlink.
// NOTE: The L3ImmediateAssignment message can not assign multislot assignments.
// NOTE: If MS T3204 (GSM04.08) 1 second, started when MS sends RACH, expires before
// receiving ImmediateAssignment, MS aborts packet access procedure.
bool sendAssignment(
	PDCHL1FEC *pacch,	// The PACCH channel.
	TBF *tbf,
	std::ostream *os)	// for testing - if set, print out the message instead of sending it.
{
	MSInfo *ms = tbf->mtMS;

	// This did not help the cause=3105 errors.  My idea was that the packet downlink assignment
	// is sent in the block immediately following the previous one, and I thought maybe there
	// Should we send the message on CCCH or PACCH?
	// It depends on which channel the MS is listening to now.
	// In short: if there are any active TBFs, or T3168 is running, or T3193 is running,
	// the MS is on PACCH.
	// The spec is all muddled about this; there is no clear state machine
	// showing what the MS is doing, rather, there are separate uplink and downlink
	// timers that get started in various modes, and it is not really even obvious
	// whether those timers should be started depending on other modes.
	bool onccch = (ms->getRROperatingMode() == RROperatingMode::PacketIdle);

	// We have maintained the T3193 and T3168 timers exclusively for this moment!
	// T3168 is for uplink requests initiated by the MS.
	// T3192/3193 are for a new network initiated downlink after completion of previous downlink.
	if (ms->msT3193.expired()) { ms->msT3193.reset(); }
	if (ms->msT3168.expired()) {
		ms->msT3168.reset();
		if (tbf->mtDir == RLCDir::Up) {
			tbf->mtCancel(MSStopCause::T3168,TbfNoRetry);
			return false;
		}
	}
	if (ms->msT3193.active()) { onccch = false; }
	if (ms->msT3168.active()) {
		if (T3168Behavior) {
			// If T3168 is running, the MS supposedly ignores downlink assignments.
			// GSM04.60 7.1.3.1, and I quote:
			// "At sending of the PACKET RESOURCE REQUEST message,
			// the mobile station shall start timer T3168. Further more,
			// the mobile station shall not respond to PACKET DOWNLINK ASSIGNMENT messages −
			// but may acknowledge such messages if they contain a valid RRBP field −
			// while timer T3168 is running."  GSM44.60 says something different.
			if (tbf->mtDir == RLCDir::Down) { return false; } // wait until later.
		}
		onccch = false;
	}

	// This is a total hack.  After several attempts, just ignore
	// whether we think it should be on ccch or not, and alternate back and forth.
	// This only works for downlink assignments, which include TLLI.
	// The uplink immediate assignment on CCCH is only for one-phase access
	// after a RACH, and we dont use that.
	tbf->mtAssignmentOnCCCH = false;	// For uplink TBF, always false.
	if (tbf->mtDir == RLCDir::Down) {
		if (tbf->mtAssignCounter < 4) {
			tbf->mtAssignmentOnCCCH = onccch;
		} else {
			tbf->mtAssignmentOnCCCH = ! tbf->mtAssignmentOnCCCH;
		}
	}

	if (GPRSDebug & 1) {
		GPRSLOG(1) <<ms <<" OnCCCH="<<tbf->mtAssignmentOnCCCH
			<<" RRMode="<<ms->getRROperatingMode()
			<<" T3193="<<(ms->msT3193.active()?ms->msT3193.remaining():0)
			<<" T3168="<<(ms->msT3168.active()?ms->msT3168.remaining():0);
	}

	if (tbf->mtAssignmentOnCCCH) {
		sendAssignmentCcch(pacch,tbf,os);
		return false;	// We did not use the packet channel downlink.
	} else {
		// Send assignment message on PACCH.
		return sendAssignmentPacch(pacch,tbf,true,MsgTransAssign1,os);
	}
}

static bool sendTA(PDCHL1Downlink *down,TBF *tbf)
{
	RLCMsgPacketPowerControlTimingAdvance *tamsg = new RLCMsgPacketPowerControlTimingAdvance(tbf);
	// DEBUG: If you change the TFI or downlink flag, this
	// is unanswered, as expected:
	//tamsg->setGlobalTFI(1,tbf->mtTFI);
	GPRSLOG(1) << "GPRS sendTA "<<tbf<<" sending:" << tamsg;
	RLCDownlinkMessage *msg = tamsg;
	// Lets ask for a response and see what happens:
	return down->send1MsgFrame(tbf,msg,2,MsgTransTA,NULL);
}

bool MSInfo::msCanUseDownlinkTn(unsigned tn)
{
	PDCHL1Downlink *down;
	RN_FOR_ALL(PDCHL1DownlinkList_t,msPCHDowns,down) {
		if (down->TN() == tn) {return true;}
	}
	return false;
}

bool MSInfo::msCanUseUplinkTn(unsigned tn)
{
	PDCHL1Uplink *up;
	RN_FOR_ALL(PDCHL1UplinkList_t,msPCHUps,up) {
		if (up->TN() == tn) {return true;}
	}
	return false;
}

bool TBF::mtAllocateUSF()
{
	// TODO WAY LATER: Is there a deadlock condition possible if multiple multislot MS
	// are in contention over USFs in multiple channels?
	// I dont think so because we run in one thread and so they cannot become codependent.
	PDCHL1Uplink *up;
	RN_FOR_ALL(PDCHL1UplinkList_t,mtMS->msPCHUps,up) {
		// Is this channel bidirectional?  Otherwise the MS cannot use the USF.
		if (!mtMS->msCanUseDownlinkTn(up->TN())) {continue;}
		int usf = up->mchParent->allocateUSF(mtMS);
		// This only allocates a new USF if one is not already allocated for this MS.
		if (usf < 0) {
			// Failure.  But we will keep the TFIs and USFs we have so far and try again later.
			GLOG(INFO) << "USF congestion on uplink";
			return false;
		}
		mtMS->msUSFs[up->TN()] = usf;	// It might already be assigned, or maybe new.
	}
	return true;
}


// Set the TFI in the channels in use by the MS that will be used for this TBF transaction.
// The newvalue must be == tbf to set it, or == NULL to reset it.
static
void propagateTFI(TBF*tbf,int tfi,TBF*newvalue)
{
	if (tbf->mtDir == RLCDir::Up) {
		PDCHL1Uplink *up;
		RN_FOR_ALL(PDCHL1UplinkList_t,tbf->mtMS->msPCHUps,up) {
			up->setTFITBF(tfi,RLCDir::Up,newvalue);
		}
	} else {
		PDCHL1Downlink *down;
		RN_FOR_ALL(PDCHL1DownlinkList_t,tbf->mtMS->msPCHDowns,down) {
			down->setTFITBF(tfi,RLCDir::Down,newvalue);
		}
	}
}

void TBF::mtFreeTFI()
{
	if (mtTFI >= 0) {
		propagateTFI(this,mtTFI,NULL);	// Reset tfi in all channels
		mtTFI = -1;
	}
}

bool TBF::mtAllocateTFI()
{
	if (mtTFI < 0) {
		devassert(mtMS->msPCHDowns.size() && mtMS->msPCHUps.size());
		PDCHL1FEC* pdch = mtMS->msPCHDowns.front()->parent();
		int tfi = pdch->mchTFIs->findFreeTFI(mtDir);
		if (tfi < 0) {
			GLOG(INFO) << "TFI congestion on "<<mtDir;
			return false;
		}
		mtTFI = tfi;
	}
	propagateTFI(this,mtTFI,this);	// Set TFI in all channels in use by MS.
	return true;
}

bool TBF::mtAttach()
{
	MSInfo *ms = mtMS;

	// msAssignChannels only does something if this is the first transaction.
	if (! ms->msAssignChannels()) return false;
	if (! mtAllocateTFI()) return false;
	if (mtDir == RLCDir::Up && ! mtAllocateUSF()) return false;

	mtAttached = true;
	mtSetState(TBFState::DataWaiting1);
	return true;
}

void TBF::mtSetState(TBFState::type wstate)
{
	if (mtState != wstate) {
		//mtMsgReset(); // May be starting a new transaction message state.
		mtState = wstate;
		tbfDumpAll();

		if (mtState == TBFState::Dead) {
			// TODO: We may be able to reduce the dead time by the length of time
			// the MS has not responded and we have not sent it any messages.
			// This is how long the TBF will be dead, ie, reserving its resources.
			int timerval = mtDir == RLCDir::Up ? gL2MAC.macT3169Value : gL2MAC.macT3195Value;
			mtDeadTime.setFuture(timerval);
		}

		if (mtState == TBFState::DataTransmit) {
			LOGWATCHF("%s Start%s bytes=%d down/up=%d/%d\n", tbfid(1),
				mtAssignmentOnCCCH ? " CCCH" : "",
				engineGetBytesPending(), mtMS->msPCHDowns.size(),mtMS->msPCHUps.size());
			// FIXME:
			// There is some housekeeping to do.
			// If a previous uplink TBF died for this MS, and then the MS was issued
			// a new uplink channel assignment, it may still have the the old USFs reserved
			// for the old channel assignment.
			// We should free those now, but it is just an efficiency issue.
		} else if (mtState == TBFState::Finished) {
			LOGWATCHF("%s Fin\n", tbfid(1));
		} else if (mtState == TBFState::Dead) {
			LOGWATCHF("%s Dead\n", tbfid(1));
		}
	}
}

// Stop all the active TBFs associated with this MS in the specified dir.
// They enter the dead state, which means their resources cannot be reused
// until the timeout expires.
void MSInfo::msStop(RLCDir::type dir, MSStopCause::type cause, TbfCancelMode cmode,
	unsigned howlong)  // howlong in msecs to disable MS - unused now.
{
	TBF *tbf;
	RN_MS_FOR_ALL_TBF(this,tbf) {
		if (dir != RLCDir::Either && tbf->mtDir != dir) continue;
		// 6-26-2012: Changing this test from isTransmitting to isActive.
		//if (tbf->isTransmitting())
		if (tbf->isActive()) {
			tbf->mtCancel(cause,cmode);
		}
	}
#if 0
	//if (!msTxxxx.active()) {
	//	GPRSLOG(1) << this <<" STOPPED" <<LOGHEX(msTLLI) <<" cause="<<(int)cause;
	//	msTxxxx.set(howlong);

	//	// DEBUG: Dont do it;
	//	return;

	//	msStopCause = cause;
	//	// Suspend all the tbfs.
	//	TBF *tbf;
	//	RN_FOR_ALL(TBFList_t,msTBFs,tbf) {
	//		if (tbf->isActive()) { tbf->mtSetState(TBFState::Dead); }
	//	}

	//	GLOG(INFO) << "MS with " <<LOGHEX(msTLLI) << " temporarily stopped, cause: " << (int)cause;
	//}
#endif
}

#if 0
void MSInfo::msRestart()
{
	TBF *tbf;
	RN_MS_FOR_ALL_TBF(this,tbf) {
		// We only delete dead tbfs, ie, the ones that we stopped previously.
		// That is so if someone changes this code in the future to allow
		// new TBFs to start during the 5 second delay in resource release,
		// we wont delete those.
		if (tbf->mtGetState() == TBFState::Dead) { tbf->mtCancel(); }
	}
	msTxxxx.reset();
	msT3191.reset();
	msT3193.reset();	// Should have expired already, but be sure.
	//msMode = RROperatingMode::PacketIdle;
}
#endif

static RLCDownEngine *createDownlinkTbf(MSInfo *ms, DownlinkQPdu *dlmsg, bool isRetry, ChannelCodingType codingMax)
{
	ms->msStalled = 0;
	GPRSLOG(2) << "<---- downlink PDU";
	RLCDownEngine *engine = new RLCDownEngine(ms);
	// Wrong! Removed 4-24 : ms->msT3193.reset();
	// At this point the RLCEngine takes charge of the dlmsg memory.
	TBF *tbf = engine->getTBF();
	tbf->mtTlli = dlmsg->mTlli;	// Dont think mtTlli is used in a downlink TBF.
	tbf->mtChannelCodingMax = codingMax;
	//tbf->mtIsRetry = isRetry;
	engine->engineWriteHighSide(dlmsg);
	engine->mtSetState(TBFState::DataReadyToConnect);
	return engine;
}

// Service this MS, called from the service loop every RLCBSN time.
// Counters and Timers defined in GSM04.60 sec 13.
void MSInfo::msService()
{
	// After the last downlink TBF, the MS waits until this timer expires
	// before going to PacketIdle mode.
	if (msT3193.expired()) {
		msT3193.reset();
		// If there are no uplink TBFs going, the MS has dropped to PacketIdle mode.
		//if (! msCountActiveTBF(RLCDir::Up)) {
		//	msMode = RROperatingMode::PacketIdle;
		//}
	}

	if (msIsSuspended()) {return;}

	if (msTBFs.size()) {
		msIdleCounter = 0;
	} else {
		if (++msIdleCounter > gL2MAC.macMSIdleMax) {
			msDelete();
			return;
		}
	}

	// If MS running, check for counter expiration and stop if necessary.
	// =========== From GSM04.60 sec 13: ==========
	// N3101:
	// When the network after setting USF, receives a valid data block from the mobile station, it will
	// reset counter N3101. The network will increment counter N3101 for each USF for which no data
	// is received. N3101max shall be greater than 8.
	// N3103:
	// N3103 is reset when transmitting the final PACKET UPLINK ACK/NACK message within a TBF
	// (final ack indicator set to 1). If the network does not receive the PACKET CONTROL
	// ACKNOWLEDGEMENT message in the scheduled block, it shall increment counter N3103 and
	// retransmit the PACKET UPLINK ACK/NACK message. If counter N3103 exceeds its limit, the
	// network shall start timer T3169.
	// N3105:
	// When the network after sending a RRBP field in the downlink RLC data block , receives a valid
	// RLC/MAC control message from the mobile station, it will reset counter N3105. The network will
	// increment counter N3105 for each allocated data block for which no RLC/MAC control message
	// is received. The value of N3105max is network dependent.
	// ===========================================

	// (pat) Having multiple timers here is overkill in the spec;
	// they all do the same thing and will probably have the same value:
	// they wait to make sure the MS is in PacketIdle mode before releasing resources.

	// When N3101 or N3103 counter expires, timeout using T3169.
	// 7-5: I want to count RLC block periods, not USFs, so multiply by the number
	// of uplink channels in use.
	if (msN3101 * min(1,(int)msPCHUps.size()) > gL2MAC.macN3101Max) {
		msStop(RLCDir::Up,MSStopCause::N3101,TbfRetryAfterWait,gL2MAC.macT3169Value);
	}
	// TODO:
	//if (msN3103 > gL2MAC.macN3103Max) {
	//	msStop(MSStopCause::N3103,gL2MAC.macT3169Value);
	//}

	// 12-22: I am taking this timer out for now, because it needs to be in the TBF,
	// not the MS. We will detect timeout here using RRBPs.  If you want to put it back in,
	// move it to class TBF.
	//if (msT3191.expired()) {
		// Spec says when T3191 expires (5 secs) can release resources,
		// but we will wait another second.
	//	msStop(MSStopCause::T3191,1000);
	//}

	// If MS is stopped, check for timer expiry and restart if necessary.
	//if (msTxxxx.expired()) { msRestart(); }

	// If there is a downlink message and this MS does not have any downlink TBFs running,
	// create a new TBF.
	while (msDownlinkQueue.size()) {
		// We will not start a new downlink TBF as long as there is any kind
		// of downlink TBF.
		// Formerly, (if 0==StallOnlyForActiveTBF) we also stalled for dead TBFS
		// (indicating the MS is probably unreachable) but that tended to kill off
		// active TBFs when any one died for mysterious reasons, so I turned it off.
		// 6-24-2012 UPDATE: I am going to reset StallOnlyForActive because we dont
		// have bugs and we now use dead tbfs to legitimately block downlinks until expiry.
		bool stallOnlyForActiveTBF = configGetNumQ("GPRS.TBF.StallOnlyForActive",0);
		TBF *blockingtbf;
		if (! msCountTBF2(RLCDir::Down,stallOnlyForActiveTBF?TbfMActive:TbfMAny,&blockingtbf)) {
			DownlinkQPdu *dlmsg = msDownlinkQueue.read();
			// Because the message is queued for this MS, it means the tlli
			// is equal to either msTlli or msOldTlli.  The SGSN tells us which
			// one to use.  Make sure it is the current one.
			// The tlli is changed on the next message after an attach procedure.
			msChangeTlli(dlmsg->mTlli);
			createDownlinkTbf(this,dlmsg,false,ChannelCodingMax);
		} else {
			// This code just prints a nice message:
			devassert(blockingtbf);
			// stalltype is 1 for stalled by active, 2 for stalled by dead tbf.
			unsigned stalltype = blockingtbf->isActive() ? 1 : 2;
			if (stalltype != msStalled) {
				GPRSLOG(2) << this <<" msDownlinkQueue stalled by "
					<<(stalltype==1 ? "active:" : "dead:") << blockingtbf;
				msStalled = stalltype;
			}
		}
		break;
	}

	// If the MS has a delayed request an uplink TBF, start it up.
	// TODO: If the Q is small, try flow control?

	// FIXME: no longer necessary?
	//if (msUplinkRequest) {
	//	TBF *tbf = TBF::newUpTBF(ms,msrmsg->mCRD);
	//if (ms->msCountActiveTBF(RLCDir::Up, &activeTbf) >= 1)
	//}

	// Over-riding TBF killer.
	// In the spec there is no such timer; instead there are timers for individual
	// states from the spec, but that does not include all the individual substates
	// we may go through, like reassignment.  If any of those has a bug the TBF
	// state machine may hang forever.  This would usually prevent that.
	// I am defaulting this timer to 6 secs which is longer than any other.
	if (msTBFs.size()) {
		// TODO: Should be TBF.NonResponsivve.
		int timerVal = gConfig.getNum("GPRS.Timers.MS.NonResponsive");	// value of 0 disables.
		if (timerVal > 0 && msTalkUpTime.elapsed() > timerVal) {
			msStop(RLCDir::Either,MSStopCause::NonResponsive,TbfNoRetry,gL2MAC.macT3169Value);
		}
	}

	if (((int)gBSNNext % 24) == 0) msTrafficMetric = msTrafficMetric / 2;
}

// The TBF ends either in mtFinishSuccess or mtCancel.
void TBF::mtFinishSuccess()
{
	mtMS->msT3191.reset();
	//GPRSLOG(1) << "@@@ok" << this <<" dir="<<mtDir <<" descr="<<mtDescription <<" OnCCCH="<<mtAssignmentOnCCCH;
	GPRSLOG(1) << "@@@ok" << this->tbfDump(false)<<timestr();
	mtSetState(TBFState::Finished);

	Timeval now;
	mtMS->msAddConnectTime(now.delta(mtStartTime));	// This includes the time it took the TBF to get its assignment through.

	// When a downlink TBF stops, set T3193, which measures how long the MS camps on the line.
	// Note that this timer runs even if there are intervening uplink TBFs.
	if (mtDir == RLCDir::Down) {
		mtMS->msT3193.set();
	} else {
		// If the last uplink TBF ends and the T3193 is not running,
		// MS immediately enters PacketIdle mode.
		//int anyactive = msCountActiveTBF(RLCDir::Either);
		//if (!anyactive) {
		//	mtMS->msMode = RROperatingMode::PacketIdle;
		//}
	}
}

// If the TBF never even started, the previous dlmsg will not have been pulled
// off of the queue yet.
void TBF::mtRetry()
{
	int retrycoding;
	bool retry = (mtDir == RLCDir::Down) && (retrycoding = configTbfRetry());
	if (mtMS->msDeprecated) {
		// No retry for MSInfo that has been replaced by some other TLLI.
		// We check again because deprecated may have changed between the time this TBF
		// was first attempted and when we get here.
		retry = false;
	}
	if (retry) {
		// Retry the last packet with a possibly slower codec:
		ChannelCodingType chCoding = (ChannelCodingType) RN_BOUND((retrycoding-1),ChannelCodingCS1,ChannelCodingCS4);
		RLCDownEngine *oldengine = getDownEngine();
		// TODO: We would like to retry all the PDUs that were not acknowledged,
		// but right now we only keep the last.
		// The mDownlinkPdu might be NULL if the engine never started, ie, MS didnt get the assignment.
		DownlinkQPdu *dlmsg = NULL;
		if (oldengine->mDownlinkPdu) {
			//dlmsg = new DownlinkQPdu(*oldengine->mDownlinkPdu);
			// retrychannel=1 implies ChannelCodingCS1
			dlmsg = oldengine->mDownlinkPdu;
			oldengine->mDownlinkPdu = NULL;
		} else {
			dlmsg = mtMS->msDownlinkQueue.readNoBlock();
		}
		if (dlmsg) { // Not possible to be NULL, but be safe.
			if (dlmsg->mDlTime.elapsed() < gConfig.getNum("GPRS.TBF.Expire")) {
				createDownlinkTbf(mtMS, dlmsg, true, chCoding);
			} else {
				// Too old.  Give up.
				delete dlmsg;
			}
		}
		// The old tbf and engine will be deleted momentarily.
	}
	mtSetState(TBFState::Dead);
}

// Kill the TBF with prejudice, either because it timed out,
// or for reasons beyond its purview, like we are shutting down GPRS.
// Same actions in either case.
void TBF::mtCancel(MSStopCause::type cause,
	TbfCancelMode release)	// When to release and whether to retry.
{
	// Clear out any reservation, in case the reservation does try to notify
	// this tbf, which will no longer exist.  This is probably overkill,
	// because either the reservations were answered and cleaned up and they
	// weren't, in which case this turned into a dead tbf,
	// and we keep dead tbfs around for 5 seconds, and their reservations
	// should be long passed over.
	// NO DONT DO THIS!!!  The thing may actually respond at that time.
	// PDCHL1Downlink *down;
	//if (mtExpectedAckBSN.valid()) {
	//	RN_FOR_ALL(PDCHL1DownlinkList_t,mtMS->msPCHDowns,down) {
	//		down->parent()->clearReservation(mtExpectedAckBSN,this);
	//	}
	//}

	// Keep separate statistics for TBF that never got a connection.
	switch (mtState) {
	default:
		mtMS->msCountTbfNoConnect++;
		break;
	case TBFState::DataTransmit:
	case TBFState::DataFinal:
	case TBFState::Finished:
		{	Timeval now;
			mtMS->msAddConnectTime(now.delta(mtStartTime));	// This includes the time it took the TBF to get its assignment through.
			mtMS->msCountTbfFail++;
			break;
		}
	}

	if (mtDir == RLCDir::Up) {
		mtMS->msFailUSFs();	// Cant use these USFs for a different MS for 5 seconds.
	}
	if (mtMS->msDeprecated) { release = TbfNoRetry; }

	std::string ss = tbfDump(true);
	bool retry = false, needRelease = false;
	const char *what = "";
	switch (release) {
		case TbfRetryInapplicable:
		case TbfNoRetry:
			what = "@@@failed tbf";
			break;
		case TbfRetryAfterRelease:
			needRelease = (mtDir == RLCDir::Down) && isTransmitting();
			what = "@@@release tbf";
			goto retryafterwait;
		case TbfRetryAfterWait:
			what = "@@@failed tbf";
			retryafterwait:
			retry = (mtDir == RLCDir::Down) && ! mtMS->msDeprecated && configTbfRetry();
			break;
	}
	GLOG(NOTICE) << timestr() << what <<LOGVAR2("cause",cause) << ss;
	if (GPRSDebug) {
		std::cout << timestr() <<what <<LOGVAR2("cause",cause) << ss<<"\n";
	}
	mtMS->msT3191.reset();

	// If it is a shutdown cause or an error during starting up the TBF, we kill it immediately.
	// If the TBF was in a transmit mode, we need to send a PacketTBFRelease message.
	// I think we are supposed to be able to set mControlAck in the PacketDownlinkAssignment
	// but the Blackberry does not implement that properly, probably because the documentation is unclear.
	// Update: other phones do implement it properly, ie, if the controlack bit is set,
	// the assignment creates a new TBF.
	// We only send the TBFRelease message in transmit mode, even though in DataWaiting modes the TBF is
	// already started, because the reason we are sending it at all is so that a new TBF does
	// not try to use RLC acknowledged mode to retrieve blocks from the previous (released) tbf.
	// So it only matters if we have started transmitting blocks.
	// Update: I am just going to send this in all transmitting modes, because we should
	// do it for DataReassign or DataFinal also, and we rarely even use the DataWaiting2 mode.

	// If we were in state TbfRelease then we set kill time in the previous call
	// to mtCancel and we dont need to add any additional time to mtKillTime.
	// That is the only way mtDeadTime can be valid on entry to this function.
	//if (mtGetState() != TBFState::TbfRelease) {
		//mtKillTime = gBSNNext.addTime(gL2MAC.macT3169Value);
	//}

	mtCause = cause;
	mtSetState(needRelease ? TBFState::TbfRelease : TBFState::Dead);

	if (retry && release == TbfRetryAfterWait) {
		mtRetry();
	}
}

// Handle the few cases for a TBF that does not have channels
// assigned to its MS yet.
void TBF::mtServiceUnattached()
{
	// Dead tbfs are attached, so dont test this flag.
	//if (mtAttached) return;
	switch (mtState) {
		case TBFState::Unused:
			// This state may occur legally during testing.
			GLOG(ERR) << "GPRS found TBF with uninitialized state\n";
			// Fix it so we wont see this message again.
			mtCancel(MSStopCause::CauseUnknown, TbfNoRetry);
			//mtState = TBFState::Dead;
			return;
		case TBFState::DataReadyToConnect:
			if (mtAttach()) {
				mtSetState(TBFState::DataWaiting1);
			}
			return;
		case TBFState::Deleting:
			casedeleting:
			if (mtMsgPending()) {
				// Wait until the response must have been received,
				// to make sure it gets delivered to the right TBF.
				// This is overkill - whoever put this in Deleting state already did this.
				return;
			}
			mtDelete();	// Cleans up and deletes.
			return;
		case TBFState::Dead:
			// A dead TBF is normally still "attached", ie, hanging onto its resources
			// to prevent someone else from using them, until its killtime expires.
			// But the MS may lose its channel assignment (eg, due to RACH)
			// so this case may not be handled by the attached tbf code.
			devassert(mtDeadTime.valid());
			if (mtDeadTime.expired()) { mtDetach(); goto casedeleting; }
			return;
		default:return;
	}
}

// Generic check for non-reponsive ms before sending another message.
// If we have sent the same message more than a specified number of times,
// consider the TBF dead.
// There is no specific mention of some of these timeout conditions in the spec,
// so just use reasonable values.
// Previously I stopped the whole MS for these cases, but sometimes the MS
// simply refuses to respond to one particular TBF, so instead, just kill the
// specific TBF that is non-responsive.
//bool TBF::mtNonResponsive()
//{
//	if (mtSendTries > gConfig.getNum("GPRS.Counters.Misc",10)) {
//		mtCancel(MSStopCause::Misc);
//		return true;
//	}
//	if (mtN3105 > gL2MAC.macN3105Max) {
//		mtCancel(MSStopCause::N3105);
//		return true;
//	}
//	return false;
//}

bool TBF::isPrimary(PDCHL1Downlink *down)
{
	return (down->parent() == mtMS->msPacch);
}

bool TBF::wantsMultislot()
{
	if (mtDir == RLCDir::Down) {
		return mtMS->msPCHDowns.size() >= 2;
	} else {
		return mtMS->msPCHUps.size() >= 2;
	}
}

// If we get a response to TbfRelease, try to restart the TBF.
// Return true if we sent something on the downlink.
// We depend on setState resetting the msgAck flag.
bool TBF::mtSendTbfRelease(PDCHL1Downlink *down)
{
	if (mtMsgPending()) { return false; }	// Wait for message in progress.
	if (mtGotAck(MsgTransTbfRelease,true)) {
		mtDetach();
		//mtSetState(TBFState::Dead);	// redundant, done inside mtRetry()
		mtRetry();
		return false;
	}
	if ((int)mtTbfReleaseCounter > gConfig.getNum("GPRS.Counters.TbfRelease")) {
		mtCancel(MSStopCause::ReleaseCounter,TbfRetryAfterWait);
		return false;
	}
	RLCMsgPacketTBFRelease *rmsg = new RLCMsgPacketTBFRelease(this);
	return down->send1MsgFrame(this,rmsg,2,MsgTransTbfRelease,&mtTbfReleaseCounter);
}

// See if the TBF can send anything on this downlink, and return true if it sent a block.
bool TBF::mtServiceDownlink(PDCHL1Downlink *down)
{
	// Only send messages on PACCH.
	while (1) {
		mac_debug();
		switch (mtState) {
			case TBFState::Unused:
				// This state may occur legally during testing.
				GLOG(ERR) << "GPRS found TBF with uninialized state\n";
				mtCancel(MSStopCause::CauseUnknown, TbfNoRetry);
				//mtState = TBFState::Dead;	// Fix it so we wont see this message again.
				return false;
			case TBFState::DataReadyToConnect:
				if (mtAttach()) {
					mtSetState(TBFState::DataWaiting1);
					continue;
				}
				return false;
			case TBFState::DataWaiting1:	// Waiting for ACK to assignment
				if (! isPrimary(down)) { return false; }
				// A non-responsive MS is detected by too many mtAssignCounter.
				if (mtGotAck(MsgTransAssign1,true)) {
					if (mtDir == RLCDir::Up) {
						// If the MS Rached us then this timer is running;
						// must reset it when the MS receives the assignment.
						mtMS->msT3168.reset();
					}
					mtSetState(TBFState::DataWaiting2);
					continue;
				} else if (! mtMsgPending()) {
					// DEBUG: Try sending extra TA messages.
					//if (mtAssignCounter > 6 && !mtTASent && sendTA(down,this)) { mtTASent=1; return true; }

					if ((int)mtAssignCounter > gConfig.getNum("GPRS.Counters.Assign")) {
						mtCancel(MSStopCause::AssignCounter,TbfNoRetry);
						return false;
					}
					if (mtAssignmentOnCCCH && ! gFixIAUsePoll) {
						// We will never get a response since we didnt poll.
						// The second time through this loop, // just go to the next state.
						// The ExpectedAckBSN is valid even though we are not polling because we are
						// using it basically as a timer to wait until the message is sent on AGCH.
						//if (mtExpectedAckBSN.valid())
						if (mtMsgPending()) {
							mtSetState(TBFState::DataWaiting2);
							continue;
						}
					}
					bool result = sendAssignment(down->parent(),this,NULL);
					// else we wait in this state for the poll result
					return result;
				}
				return false;

			case TBFState::DataWaiting2:
				if (! isPrimary(down)) { return false; }
				if (mtAssignmentOnCCCH) {
					if (mtDir == RLCDir::Down) {
						// See GSM4.60 7.2.2.1
						// We do not have to send a timing advance message
						// because we included a starting time in the
						// assignment message, but it is useful for debugging
						// to see if the MS responds.
						if (SendExtraTA && ! sendTA(down,this)) { return false; }
						mtSetState(TBFState::DataTransmit);
						return true;	// We sent a message.
					}
				}
				mtSetState(TBFState::DataTransmit);
				continue;

			case TBFState::DataReassign:
				devassert(0);
#if 0
				// Wait for existing messages to clear.
				if (mtMsgPending()) {return false;}
				// Did we get the ack to the reassignment?
				if (mtGotAck(MsgTransReassign,true)) {
					mtSetState(TBFState::DataTransmit);
					continue;
				}
				if ((int)mtReassignCounter > gConfig.getNum("GPRS.Counters.Reassign")) {
					mtCancel(MSStopCause::ReassignCounter,TbfRetryAfterWait);
					return false;
				}
				// Send the reassignment.
				// This is currently used only in the case where an uplink TBF
				// wants to change its priority.
				devassert(tbf->mtDir == RLCDir::Up);
				if (sendTimeslotReconfigure(down->parent(),this,NULL)) { return true; }
				// TODO: While we are waiting for the above, we could be sending
				// blocks or setting USFs on the old channels, but for now
				// we will not send any more blocks until the reassign occurs.
				return false;	// We did not use the downlink.
#endif
			case TBFState::DataTransmit:
				if (mtAssignmentOnCCCH) {
					if (mtGotAck(MsgTransAssign2,true)) {
						// Woo hoo!  We are multislot now.
						mtAssignmentOnCCCH = false;
						// Turn off the reassign too, since we just did an additional reassign.
						mtPerformReassign = false;
						// And fall through to service the TBF on this channel.
					} else {
						// If the assignment was sent on CCCH we can not set up multislot,
						// so we will use PACCH exclusively for the nonce.
						if (! isPrimary(down)) { return false; }

						if (wantsMultislot()) {
							// We would like to be multislot, but we are not yet.
							// A multislot assignment can not be included in
							// the L3ImmediateAssignment message on CCCH.
							// So to do multislot we will need to send two messages
							// the initial assignment and then another
							// on PACCH to get into multislot mode.
							if (! mtMsgPending()) {
								// Send a second assignment to set up multislot.
								if (sendAssignmentPacch(down->parent(),this,false,MsgTransAssign2,NULL)) { return true; }
							}
						}
					}
				}

#if OLD_REASSIGN
				if (mtPerformReassign) {
					// We are sending a reassign because the MS requested a new priority for this TBF.
					if (mtGotAck(MsgTransReassign,true)) {
						mtPerformReassign = false;
						// And fall through to service the TBF on this channel.
					} else {
						if (! mtMsgPending()) {
							if ((int)mtReassignCounter > gConfig.getNum("GPRS.Counters.Reassign")) {
								// The iphone is not answering these.  It may be because we are only allowed
								// to have 3 RRBPs out at a time, but whatever, dont kill the TBF for this,
								// just stop sending the messages.  If the MS wants 
								//mtCancel(MSStopCause::ReassignCounter,TbfRetryAfterWait);
								// return false;
								mtPerformReassign = false;
							} else {
								if (sendAssignmentPacch(down->parent(),this,false,MsgTransReassign,NULL)) { return true; }
							}
						}
						// Otherwise fall through to utilize the channel normally.
					}
				}
#endif

				// Nonresponsive uplink is detected by N3101 (too many unanswered USF)
				// in the msService routine, or N3103 in DataFinal mode.
				// Nonresponsive downlink is detected by N3105 (no answer to RRBP data block),
				// or when finished by T3191 expiry with no downlinkacknack received from MS.
				// When this overflows the final reservation is still outstanding.
				// TODO: fix this minor problem.
				if (mtN3105 > gL2MAC.macN3105Max) {
					if (mtAssignmentOnCCCH && !gFixIAUsePoll) {
						// This error indicates the assignment failed.
						// Go back and try it again.
						mtSetState(TBFState::DataWaiting1);
						continue;
					}
					mtCancel(MSStopCause::N3105,TbfRetryAfterRelease);
					return false;
				}
				return engineService(down);

			case TBFState::DataFinal:
				if (mtAssignmentOnCCCH && ! isPrimary(down)) { return false; }
				// Nonresponsive uplink detected by N3103 (no answer to RRBP in final acknack).
				// Not applicable to downlink.
				if (mtN3103 > gL2MAC.macN3103Max) {
					mtCancel(MSStopCause::N3103,TbfRetryInapplicable);
					return false;
				}
				// For a downlink, we have to wait for the acknack from the MS.
				// For an uplink, we have to wait for the RRBP response
				// to the acknack that we sent the MS.
				// The engineService routine takes care of this.
				return engineService(down);

			//case TBFState::TbfRelease1:
			//	// Wait for any existing reservations to clear first.
			//	if (gBSNNext >= mtKillTime) { mtSetState(TBFState::Dead); continue; }
			//	if (! mtMsgPending()) { mtSetState(TBFState::TbfRelease2); continue; }
			//	return false;

			// Currently TbfRelease is used only when killing a TBF.
			// mtSendTbfRelease will retry the TBF if possible.
			case TBFState::TbfRelease:
				if (! isPrimary(down)) { return false; }
				// This extra check for killtime is no longer needed because
				// we check in the msService loop.
				//if (gBSNNext >= mtKillTime) { mtSetState(TBFState::Dead); continue; }
				// Send the TBF Release message.
				return mtSendTbfRelease(down);

			case TBFState::Finished:
				// Hang around in this state until we are sure
				// the MS has stopped talking to us.
				if (! mtMsgPending()) { mtDetach(); }
				return false;

			case TBFState::Dead:
				// A dead TBF is still attached, ie, holding onto USF and TFI
				// resources, but the MS may or may not have channel assignments,
				// so this case must appear both here and in mtServiceUnattached.
				devassert(mtDeadTime.valid());
				if (mtDeadTime.expired()) { mtDetach(); }
				return false;

			case TBFState::Deleting:
				return false;
		}
	}
}

};	// namespace
