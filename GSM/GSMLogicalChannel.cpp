/**@file Logical Channel.  */

/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011 Range Networks, Inc.
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



#include "GSML3RRElements.h"
#include "GSML3Message.h"
#include "GSML3RRMessages.h"
#include "GSMSMSCBL3Messages.h"
#include "GSMLogicalChannel.h"
#include "GSMConfig.h"

#include <ControlTransfer.h>
#include "GPRSExport.h"

#include <Logger.h>

using namespace std;
using namespace GSM;


void L2LogicalChannel::open()
{
	LOG(INFO);
	LOG(DEBUG);
	if (mSACCH) mSACCH->open();
	LOG(DEBUG);
	if (mL1) mL1->open();		// (pat) L1FEC::open()
	LOG(DEBUG);
	for (int s=0; s<4; s++) {
		if (mL2[s]) mL2[s]->l2open(descriptiveString());
		LOG(DEBUG) << "SAPI=" << s << " open complete";
	}
}


// (pat) This is connecting layer2, not layer1.
void L2LogicalChannel::connect()
{
	mMux.downstream(mL1);
	if (mL1) mL1->upstream(&mMux);
	for (int s=0; s<4; s++) {
		mMux.upstream(mL2[s],s);
		if (mL2[s]) {
			mL2[s]->l2Downstream(&mMux);
			mL2[s]->l2Upstream(this);
		}
	}
}


// (pat) This is only called during initialization, using the createCombination*() functions.
// The L1FEC->downstream hooks the radio to this logical channel, permanently.
void L2LogicalChannel::downstream(ARFCNManager* radio)
{
	assert(mL1);	// This is L1FEC
	mL1->downstream(radio);
	if (mSACCH) mSACCH->downstream(radio);
}



// Serialize and send an L3Message with a given primitive.
// The msg is not deleted; its value is used before return.
void L2LogicalChannel::l2sendm(const L3Message& msg,
		const GSM::Primitive& prim,
		SAPI_t SAPI)
{
	OBJLOG(INFO) << "L3" <<LOGVAR(SAPI) << " sending " << msg;
	l2sendf(L3Frame(msg,prim,SAPI), SAPI);
}




CCCHLogicalChannel::CCCHLogicalChannel(const TDMAMapping& wMapping)
	:mRunning(false)
{
	mL1 = new CCCHL1FEC(wMapping);
	mL2[0] = new CCCHL2;
	connect();
}


void CCCHLogicalChannel::open()
{
	L2LogicalChannel::open();
	if (!mRunning) {
		mRunning=true;
		mServiceThread.start((void*(*)(void*))CCCHLogicalChannelServiceLoopAdapter,this);
	}
}


// (pat) BUG TODO: TO WHOM IT MAY CONCERN:
// I am not sure this routine works properly.  If there is no CCCH message (an L3Frame)
// in the queue immediately after the previous frame is sent, an idle frame is inserted.
// If a subsequent valid CCCH message (paging response or MS initiated RR call or packet
// uplink request) arrives it will be blocked until the idle frame is sent.
// Probably doesnt matter for RR establishment, but for packets, the extra 1/4 sec
// delay (length of a 51-multiframe) is going to hurt.
// Note that a GPRS Immediate Assignment message must know when this CCCH gets sent.
// Right now, it has to guess.
// pats TODO: Send the transceiver an idle frame rather than doing it here.
// This should be architecturally changed to a pull-system instead of push.
// Among other things, that would let us prioritize the responses
// (eg, emergency calls go first) and let the packet Immediate Assignment message be
// created right before being sent, when we are certain when the
// Immediate Assignment is being sent.
void CCCHLogicalChannel::serviceLoop()
{
	// build the idle frame
	static const L3PagingRequestType1 filler;
	static const L3Frame idleFrame(filler,UNIT_DATA);
#if ENABLE_PAGING_CHANNELS
	L3ControlChannelDescription mCC;
	unsigned bs_pa_mfrms = mCC.getBS_PA_MFRMS();
#endif
	// prime the first idle frame
	L2LogicalChannel::l2sendf(idleFrame);
	// run the loop
	while (true) {
		L3Frame* frame = NULL;
#if ENABLE_PAGING_CHANNELS
		// Check for paging message for this specific paging slot first,
		// and if none, send any message in the mQ.
		// The multiframe paging logic is from GSM 05.02 6.5.3.
		// See documentation at crackPagingFromImsi() which is used to
		// get the messages into the proper mPagingQ.
		GSM::Time next = getNextWriteTime();
		unsigned multiframe_index = (next.FN() / 51) % bs_pa_mfrms;
		frame = mPagingQ[multiframe_index].read();
#endif
		if (frame == NULL) {
			frame = mQ.read();	// (pat) This is a blocking read; mQ is an InterThreadQueue
		}
		if (frame) {
			// (pat) This tortuously calls XCCCHL1Encoder::transmit (see my documentation
			// at L2LogicalChannel::send), which blocks until L1Encoder::mPrevWriteTime.
			// Note: The q size is 0 while we are blocked here, so if we are trying
			// to determine the next write time by adding the qsize, we are way off.
			// Thats why there is an mWaitingToSend flag.
			mWaitingToSend = true;	// Waiting to send this block at mNextWriteTime.
			L2LogicalChannel::l2sendf(*frame);
			mWaitingToSend = false;
			OBJLOG(DEBUG) << "CCCHLogicalChannel::serviceLoop sending " << *frame;
			delete frame;
		}
		if (mQ.size()==0) {
			// (pat) The radio continues to send the last frame forever,
			// so we only send one idle frame here.
			// Unfortunately, this slows the response.
			// TODO: Send a static idle frame to the Transciever and rewrite this.
			mWaitingToSend = true;	// Waiting to send an idle frame at mNextWriteTime.
			L2LogicalChannel::l2sendf(idleFrame);
			mWaitingToSend = false;
			OBJLOG(DEBUG) << "CCCHLogicalChannel::serviceLoop sending idle frame";
		}
	}
}


void *GSM::CCCHLogicalChannelServiceLoopAdapter(CCCHLogicalChannel* chan)
{
	chan->serviceLoop();
	return NULL;
}

#if ENABLE_PAGING_CHANNELS
// (pat) This routine is going to be entirely replaced with one that works better for gprs.
// In the meantime, just return a number that is large enough to cover
// the worst case, which assumes that the messages in mQ also
// must go out on the paging timeslot.
Time GSM::CCCHLogicalChannel::getNextPchSendTime(unsigned multiframe_index)
{
	L3ControlChannelDescription mCC;
	// Paging is distributed over this many multi-frames.
	unsigned bs_pa_mfrms = mCC.getBS_PA_MFRMS();

	GSM::Time next = getNextWriteTime();
	unsigned next_multiframe_index = (next.FN() / 51) % bs_pa_mfrms;
	assert(bs_pa_mfrms > 1);
	assert(multiframe_index < bs_pa_mfrms);
	assert(next_multiframe_index < bs_pa_mfrms);
	int achload = mQ.size();
	if (mWaitingToSend) { achload++; }

	// Total wait time is time needed to empty queue, plus the time until the first
	// paging opportunity, plus 2 times the number of guys waiting in the paging queue,
	// but it is all nonsense because if a new agch comes in,
	// it will displace the paging message because the q is sent first.
	// This just needs to be totally redone, and the best way is not to figure out
	// when the message will be sent at all, but rather use a call-back to gprs
	// just before the message is finally sent.
	int multiframesToWait = 0;
	if (achload) {
		multiframesToWait = bs_pa_mfrms - 1;	// Assume worst case.
	} else {
		// If there is nothing else waiting, we can estimate better:
		while (next_multiframe_index != multiframe_index) {
			multiframe_index = (multiframe_index+1) % bs_pa_mfrms;
			multiframesToWait++;
		}
	}
	int total = achload + multiframesToWait + bs_pa_mfrms * mPagingQ[multiframe_index].size();

	int fnresult = (next.FN() + total * 51) % gHyperframe;
	GSM::Time result(fnresult);
	LOG(DEBUG) << "CCCHLogicalChannel::getNextSend="<< next.FN()
		<<" load="<<achload<<LOGVAR(mWaitingToSend) <<" now="<<gBTS.time().FN()<<LOGVAR(fnresult);
	return result;
}
#endif

Time GSM::CCCHLogicalChannel::getNextMsgSendTime() {
	// Get the current frame.
	// DAB GPRS - This should call L1->resync() first, otherwise, in an idle system,
	// DAB GPRS - you can get times well into the past..
	// (pat) Above is done in the underlying getNextWriteTime()
	// Pats note: This may return the current frame number if it is ready to send now.
	// 3-18-2012: FIXME: This result is not monotonically increasing!!
	// That is screwing up GPRS sendAssignment.
	GSM::Time next = getNextWriteTime();
	int achload = load();
	if (mWaitingToSend) { achload++; }
	//old: GSM::Time result = next + (achload+3) * 51;	// add one to be safe.

	// (pat) TODO: We are adding a whole 51-multframe for each additional
	// CCCH message, which may not be correct.
	// Note: We dont need to carefully make sure the frame
	// numbers are valid (eg, by rollForward), because this code is used by GPRS
	// which is going to convert it to an RLC block time anyway.
	int fnresult = (next.FN() + achload * 51) % gHyperframe;
	GSM::Time result(fnresult);
	LOG(DEBUG) << "CCCHLogicalChannel::getNextSend="<< next.FN()
		<<" load="<<achload<<LOGVAR(mWaitingToSend) <<" now="<<gBTS.time().FN()<<LOGVAR(fnresult);
	return result;
}



L3ChannelDescription L2LogicalChannel::channelDescription() const
{
	// In some debug cases, L1 may not exist, so we fake this information.
	if (mL1==NULL) return L3ChannelDescription(TDMA_MISC,0,0,0);

	// In normal cases, we get this information from L1.
	return L3ChannelDescription(
		mL1->typeAndOffset(),
		mL1->TN(),
		mL1->TSC(),
		mL1->ARFCN()
	);
}




SDCCHLogicalChannel::SDCCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const CompleteMapping& wMapping)
{
	mL1 = new SDCCHL1FEC(wCN,wTN,wMapping.LCH());
	// SAP0 is RR/MM/CC, SAP3 is SMS
	// SAP1 and SAP2 are not used.
	L2LAPDm *SAP0L2 = new SDCCHL2(1,SAPI0);		// derived from L2LAPDm
	L2LAPDm *SAP3L2 = new SDCCHL2(1,SAPI3);
	LOG(DEBUG) << "LAPDm pairs SAP0=" << SAP0L2 << " SAP3=" << SAP3L2;
	SAP3L2->master(SAP0L2);
	mL2[0] = SAP0L2;
	mL2[3] = SAP3L2;
	mSACCH = new SACCHLogicalChannel(wCN,wTN,wMapping.SACCH(),this);
	connect();
}



void NeighborCache::neighborClearMeasurements()
{
	LOG(DEBUG);
	mNeighborRSSI.clear();
	cNumReports = gConfig.getNum("GSM.Neighbors.Averaging");	// Neighbor must appear in 2 of last cNumReports measurement reports.
}

// I am a little worried that the MS will not report just the 6 best cells, but may report some cells in
// one report and some other cells in another report, so we dont delete a neighbor just because
// it does not appear in a single report.  We set mnCount to cNumReports and decrement it toward 0.
// The effect is that in order to be considered for handover, the neighbor must appear in at least
// 2 of the last cNumReports measurement reports, then we send a cumulative decaying average of the reports.
void NeighborCache::neighborStartMeasurements()
{
	LOG(DEBUG);
	// Called at start of measurement reports.  Decrement mnCount toward to zero.
	for (NeighborMap::iterator it = mNeighborRSSI.begin(); it != mNeighborRSSI.end(); it++) {
		if (it->second.mnCount) it->second.mnCount--;
	}
}

string NeighborCache::neighborText()
{
	string result; result.reserve(100);
	result.append("Neighbors(");
	for (NeighborMap::iterator it = mNeighborRSSI.begin(); it != mNeighborRSSI.end(); it++) {
		LOG(DEBUG);
		unsigned freqindex = it->first >> 6, bsic = it->first & 0x3f;
		char buf[82];
		snprintf(buf,80,"(freqIndex=%u BSIC=%u count=%u AvgRSSI=%d)",freqindex,bsic,it->second.mnCount,it->second.mnAvgRSSI);
		result.append(buf);
	}
	result.append(")");
	return result;
}

int NeighborCache::neighborAddMeasurement(unsigned freqindex, unsigned BSIC, int RSSI)
{
	unsigned key = (freqindex<<6) + BSIC;
	NeighborData &data = mNeighborRSSI[key];
	int result;
	int startCount = data.mnCount, startAvg = data.mnAvgRSSI;
	if (data.mnCount == 0) {
		// Handsets sometimes send a spuriously low measurement report,
		// so dont handover until we have seen at least two measurements from the same neighbor.
		// We prevent handover by sending an impossibly low RSSI.
		data.mnAvgRSSI = RSSI;
		data.mnCount = cNumReports;
		result = -200;	// Impossibly low value.
	} else {
		data.mnCount = cNumReports;
		result = data.mnAvgRSSI = RSSI/2 + data.mnAvgRSSI/2;
	}
	int endCount=data.mnCount;	// ffing << botches this.
	LOG(DEBUG) <<LOGVAR(result) <<LOGVAR(BSIC)<<LOGVAR(freqindex)<<LOGVAR(RSSI)<<LOGVAR(endCount) <<LOGVAR(startCount)<<LOGVAR(startAvg);
	return result;
}


SACCHLogicalChannel::SACCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const MappingPair& wMapping,
		/*const*/ L2LogicalChannel *wHost)
		: mRunning(false),
		mHost(wHost)
{
	mSACCHL1 = new SACCHL1FEC(wCN,wTN,wMapping);
	mL1 = mSACCHL1;
	// SAP0 is RR, SAP3 is SMS
	// SAP1 and SAP2 are not used.
	mL2[0] = new SACCHL2(1,SAPI0);	// derived from L2LAPDm
	mL2[3] = new SACCHL2(1,SAPI3);
	connect();
	assert(mSACCH==NULL);
}


void SACCHLogicalChannel::open()
{
	L2LogicalChannel::open();
	if (!mRunning) {
		mRunning=true;
		mServiceThread.start((void*(*)(void*))SACCHLogicalChannelServiceLoopAdapter,this);
#if USE_SEMAPHORE
		sem_init(&mOpenSignal,0,0);
#endif
	}
	neighborClearMeasurements();
	mAverageRXLEV_SUB_SERVICING_CELL = 0;
	// Just make sure any stray messages are flushed when we reactivate the channel.
	while (L3Message *straymsg = mTxQueue.readNoBlock()) { delete straymsg; }
#if USE_SEMAPHORE
	cout << descriptiveString() << " POST" <<endl;
	sem_post(&mOpenSignal);	// Note: you must open the L2LogicalChannel before starting the SACCH service loop.
#endif
}



static L3Message* parseSACCHMessage(const L3Frame *l3frame)
{
	if (!l3frame) return NULL;
	LOG(DEBUG) << *l3frame;
	Primitive prim = l3frame->primitive();
	if ((prim!=DATA) && (prim!=UNIT_DATA)) {
		LOG(INFO) << "non-data primitive " << prim;
		return NULL;
	}
	// FIXME -- Why, again, do we need to do this?  (pat) Apparently, we dont.
//	L3Frame realFrame = l3frame->segment(24, l3frame->size()-24);
	L3Message* message = parseL3(*l3frame);
	if (!message) {
		LOG(WARNING) << "SACCH recevied unparsable L3 frame " << *l3frame;
		WATCHF("SACCH received unparsable L3 frame PD=%d MTI=%d",l3frame->PD(),l3frame->MTI());
	}
	return message;
}



// (pat) This is started when SACCH is opened, and runs forever.
// The SACCHLogicalChannel are created by the SDCCHLogicalChannel and TCHFACCHLogicalChannel constructors.
void SACCHLogicalChannel::serviceLoop()
{

	// run the loop
	unsigned count = 0;
	while (true) {

		// Throttle back if not active.
		if (!active()) {
			//OBJLOG(DEBUG) << "SACCH sleeping";
			// pat 5-2013: Vastly reducing the delays here and in L2LAPDm to try to reduce
			// random failures of handover and channel reassignment from SDCCH to TCHF.
			// Update: The further this sleep is reduced, the more reliable handover becomes.
			// I left it at 4 for a while but handover still failed sometimes.
			//sleepFrames(51);
#define USE_SEMAPHORE 0	// This does not work well - there appear to be hundreds of interrupts per second.
#if USE_SEMAPHORE
			// (pat) Update: Getting rid of the sleep entirely.  We will use a semaphore instead.
			// Note that the semaphore call may return on signal, which is ok here.
			cout << descriptiveString() << " WAIT" <<endl;
			sem_wait(&mOpenSignal);
			cout << descriptiveString() << " AFTER" <<endl;
#else
			sleepFrames(2);
#endif
			// A clever way to avoid the sleep above would be to wait for ESTABLISH primitive.
			// (But which do you wait on - the tx or the rx queue?
			continue;	// paranoid, check again.
		}

		// Send any outbound messages.  If the tx queue is empty send alternating SI5/6.
		// (pat) FIXME: implement this!
		if (const L3Message *l3msg = mTxQueue.readNoBlock()) {
			SAPI_t sapi = SAPI0;		// Determine sapi from PD.  This is probably unnecessary, they are probably all SAPI=3
			switch (l3msg->PD()) {
			case L3RadioResourcePD: sapi = SAPI0; break;
			case L3SMSPD: sapi = SAPI3; break;
			default:
				OBJLOG(ERR)<<"In SACCHLogicalChannel, unexpected"<<LOGVAR(l3msg->PD());
				break;
			}
			L2LogicalChannel::l2sendm(*l3msg,GSM::DATA,sapi);
			delete l3msg;
		} else {
			// Send alternating SI5/SI6.
			// These L3Frames were created with the UNIT_DATA primivitive.
			OBJLOG(DEBUG) << "sending SI5/6 on SACCH";
			if (count%2) L2LogicalChannel::l2sendf(gBTS.SI5Frame());
			else L2LogicalChannel::l2sendf(gBTS.SI6Frame());
			count++;
		}

		// Receive inbound messages.
		// This read loop flushes stray reports quickly.
		while (true) {

			OBJLOG(DEBUG) << "polling SACCH for inbound messages";
			bool nothing = true;

			// Process SAP0 -- RR Measurement reports
			if (L3Frame *rrFrame = L2LogicalChannel::l2recv(0,0)) {
				nothing=false;
				bool isMeasurementReport = rrFrame->isData()
					&& rrFrame->PD() == L3RadioResourcePD && rrFrame->MTI() == L3RRMessage::MeasurementReport;
				if (isMeasurementReport) {
					// Neither of these 'ifs' should fail, but be safe.
					if (const L3Message* rrMessage = parseSACCHMessage(rrFrame)) {
						if (const L3MeasurementReport* measurement = dynamic_cast<typeof(measurement)>(rrMessage)) {
							OBJLOG(DEBUG) << "SACCH measurement report " << mMeasurementResults;
							mMeasurementResults = measurement->results();
							if (mMeasurementResults.MEAS_VALID() == 0) {
								addSelfRxLev(mMeasurementResults.RXLEV_SUB_SERVING_CELL_dBm());
							}
							// Add the measurement results to the table
							// Note that the typeAndOffset of a SACCH match the host channel.
							gPhysStatus.setPhysical(this, mMeasurementResults);
							// Check for handover requirement.
							// (pat) TODO: This may block while waiting for a reply from a Peer BTS.
							Control::HandoverDetermination(mMeasurementResults,mAverageRXLEV_SUB_SERVICING_CELL,this);
						}
						delete rrMessage;
					}
					delete rrFrame;
				} else {
					// Send it off to Layer 3.  Who knows what might show up here.
					hostChan()->chanEnqueueFrame(rrFrame);
				}
			}

#if 0
			L3Message* rrMessage = parseSACCHMessage(rrFrame);
			delete rrFrame;
			if (rrMessage) {
				L3MeasurementReport* measurement = dynamic_cast<L3MeasurementReport*>(rrMessage);
				if (measurement) {
					mMeasurementResults = measurement->results();
					OBJLOG(DEBUG) << "SACCH measurement report " << mMeasurementResults;
					// Add the measurement results to the table
					// Note that the typeAndOffset of a SACCH match the host channel.
					gPhysStatus.setPhysical(this, mMeasurementResults);
					// Check for handover requirement.
					// (pat) TODO: This may block while waiting for a reply from a Peer BTS.
					Control::HandoverDetermination(mMeasurementResults,this);
					delete rrMessage;
				} else {
					if (Control::l3rewrite()) {
						OBJLOG(DEBUG) << "chanEnqueuel3msg:"<<rrMessage;
						hostChan()->chanEnqueuel3msg(rrMessage);
					} else {
						OBJLOG(NOTICE) << "SACCH SAP0 sent unaticipated message " << rrMessage;
						delete rrMessage;
					}
				}
			}
#endif

			// Process SAP3 -- SMS
			L3Frame *smsFrame = L2LogicalChannel::l2recv(0,3);
			if (smsFrame) {
				nothing=false;

				OBJLOG(DEBUG) <<"received SMS frame:"<<smsFrame;

				// The SACCH messages are polled from by the single L3LogicalChannel thread that handles this MS.
				//if (smsFrame) { Control::gCSL3StateMachine.csl3Write(new Control::GenericL3Msg(smsFrame,this)); }
				//L3Message *smsMessage = parseSACCHMessage(smsFrame);
				//OBJLOG(DEBUG) <<"parsed SMS message:"<<smsMessage;
				//delete smsFrame;
				hostChan()->chanEnqueueFrame(smsFrame);
			}

			// Did we get anything from the phone?
			// If not, we may have lost contact.  Bump the RSSI to induce more power
			if (nothing) RSSIBumpDown(gConfig.getNum("Control.SACCHTimeout.BumpDown"));

			// Nothing happened?
			if (nothing) break;
		}
	}
}


void *GSM::SACCHLogicalChannelServiceLoopAdapter(SACCHLogicalChannel* chan)
{
	chan->serviceLoop();
	return NULL;
}


// These have to go into the .cpp file to prevent an illegal forward reference.
void L2LogicalChannel::setPhy(float wRSSI, float wTimingError, double wTimestamp)
	{ assert(mSACCH); mSACCH->setPhy(wRSSI,wTimingError,wTimestamp); }
void L2LogicalChannel::setPhy(const L2LogicalChannel& other)
	{ assert(mSACCH); mSACCH->setPhy(*other.SACCH()); }
MSPhysReportInfo * L2LogicalChannel::getPhysInfo() const {
	assert(mSACCH); return mSACCH->getPhysInfo();
}
const L3MeasurementResults& L2LogicalChannel::measurementResults() const
	{ assert(mSACCH); return mSACCH->measurementResults(); }



TCHFACCHLogicalChannel::TCHFACCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const CompleteMapping& wMapping)
{
	mTCHL1 = new TCHFACCHL1FEC(wCN,wTN,wMapping.LCH());
	mL1 = mTCHL1;
	// SAP0 is RR/MM/CC, SAP3 is SMS
	// SAP1 and SAP2 are not used.
	mL2[0] = new FACCHL2(1,SAPI0);
	mL2[3] = new FACCHL2(1,SAPI3);
	mSACCH = new SACCHLogicalChannel(wCN,wTN,wMapping.SACCH(),this);
	connect();
}




CBCHLogicalChannel::CBCHLogicalChannel(const CompleteMapping& wMapping)
{
	mL1 = new CBCHL1FEC(wMapping.LCH());
	mL2[0] = new CBCHL2;
	mSACCH = new SACCHLogicalChannel(0,0,wMapping.SACCH(),this);
	connect();
}


void CBCHLogicalChannel::l2sendm(const L3SMSCBMessage& msg)
{
	L3Frame frame(UNIT_DATA,88*8);
	msg.write(frame);
	L2LogicalChannel::l2sendf(frame);
}




#if UNUSED
bool L2LogicalChannel::waitForPrimitive(Primitive primitive, unsigned timeout_ms)
{
	bool waiting = true;
	while (waiting) {
		L3Frame *req = recv(timeout_ms);
		if (req==NULL) {
			OBJLOG(NOTICE) << "timeout at uptime " << gBTS.uptime() << " frame " << gBTS.time();
			return false;
		}
		waiting = (req->primitive()!=primitive);
		delete req;
	}
	return true;
}


void L2LogicalChannel::waitForPrimitive(Primitive primitive)
{
	bool waiting = true;
	while (waiting) {
		L3Frame *req = recv();
		if (req==NULL) continue;
		waiting = (req->primitive()!=primitive);
		delete req;
	}
}
#endif

// We only return state for SAPI0, although the state could be different in SAPI0 and SAPI3.
LAPDState L2LogicalChannel::getLapdmState() const
{
	// The check for NULL is redundant - these objects are allocated at startup and are immortal.
	if (mL2[0]) { return mL2[0]->getLapdmState(); }
	return LAPDStateUnused;
}


ostream& GSM::operator<<(ostream& os, const L2LogicalChannel& chan)
{
	os << chan.descriptiveString();
	return os;
}
std::ostream& GSM::operator<<(std::ostream&os, const L2LogicalChannel*ch)
{
	if (ch) { os <<*ch; } else { os << "(null L2Logicalchannel)"; }
	return os;
}



// vim: ts=4 sw=4

