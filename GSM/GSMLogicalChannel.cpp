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

#include <TransactionTable.h>
#include <SMSControl.h>
#include <ControlCommon.h>
#include "GPRSExport.h"

#include <Logger.h>

using namespace std;
using namespace GSM;



void LogicalChannel::open()
{
	LOG(INFO);
	LOG(DEBUG);
	if (mSACCH) mSACCH->open();
	LOG(DEBUG);
	if (mL1) mL1->open();		// (pat) L1FEC::open()
	LOG(DEBUG);
	for (int s=0; s<4; s++) {
		if (mL2[s]) mL2[s]->open();
		LOG(DEBUG) << "SAPI=" << s << " open complete";
	}
	// Empty any stray transactions in the FIFO from the SIP layer.
	while (true) {
		Control::TransactionEntry *trans = mTransactionFIFO.readNoBlock();
		if (!trans) break;
		LOG(WARNING) << "flushing stray transaction " << *trans;
		// FIXME -- Shouldn't we be deleting these?
	}
	LOG(DEBUG);
}


// (pat) This is connecting layer2, not layer1.
void LogicalChannel::connect()
{
	mMux.downstream(mL1);
	if (mL1) mL1->upstream(&mMux);
	for (int s=0; s<4; s++) {
		mMux.upstream(mL2[s],s);
		if (mL2[s]) mL2[s]->downstream(&mMux);
	}
}


// (pat) This is only called during initialization, using the createCombination*() functions.
// The L1FEC->downstream hooks the radio to this logical channel, permanently.
void LogicalChannel::downstream(ARFCNManager* radio)
{
	assert(mL1);	// This is L1FEC
	mL1->downstream(radio);
	if (mSACCH) mSACCH->downstream(radio);
}



// Serialize and send an L3Message with a given primitive.
void LogicalChannel::send(const L3Message& msg,
		const GSM::Primitive& prim,
		unsigned SAPI)
{
	LOG(INFO) << "L3 SAP" << SAPI << " sending " << msg;
	send(L3Frame(msg,prim), SAPI);
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
	LogicalChannel::open();
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
	LogicalChannel::send(idleFrame);
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
			// at LogicalChannel::send), which blocks until L1Encoder::mPrevWriteTime.
			// Note: The q size is 0 while we are blocked here, so if we are trying
			// to determine the next write time by adding the qsize, we are way off.
			// Thats why there is an mWaitingToSend flag.
			mWaitingToSend = true;	// Waiting to send this block at mNextWriteTime.
			LogicalChannel::send(*frame);
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
			LogicalChannel::send(idleFrame);
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



L3ChannelDescription LogicalChannel::channelDescription() const
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
	L2LAPDm *SAP0L2 = new SDCCHL2(1,0);
	L2LAPDm *SAP3L2 = new SDCCHL2(1,3);
	LOG(DEBUG) << "LAPDm pairs SAP0=" << SAP0L2 << " SAP3=" << SAP3L2;
	SAP3L2->master(SAP0L2);
	mL2[0] = SAP0L2;
	mL2[3] = SAP3L2;
	mSACCH = new SACCHLogicalChannel(wCN,wTN,wMapping.SACCH(),this);
	connect();
}





SACCHLogicalChannel::SACCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const MappingPair& wMapping,
		const LogicalChannel *wHost)
		: mRunning(false),
		mHost(wHost)
{
	mSACCHL1 = new SACCHL1FEC(wCN,wTN,wMapping);
	mL1 = mSACCHL1;
	// SAP0 is RR, SAP3 is SMS
	// SAP1 and SAP2 are not used.
	mL2[0] = new SACCHL2(1,0);
	mL2[3] = new SACCHL2(1,3);
	connect();
	assert(mSACCH==NULL);
}


void SACCHLogicalChannel::open()
{
	LogicalChannel::open();
	if (!mRunning) {
		mRunning=true;
		mServiceThread.start((void*(*)(void*))SACCHLogicalChannelServiceLoopAdapter,this);
	}
}



L3Message* processSACCHMessage(L3Frame *l3frame)
{
	if (!l3frame) return NULL;
	LOG(DEBUG) << *l3frame;
	Primitive prim = l3frame->primitive();
	if ((prim!=DATA) && (prim!=UNIT_DATA)) {
		LOG(INFO) << "non-data primitive " << prim;
		return NULL;
	}
	// FIXME -- Why, again, do we need to do this?
//	L3Frame realFrame = l3frame->segment(24, l3frame->size()-24);
	L3Message* message = parseL3(*l3frame);
	if (!message) {
		LOG(WARNING) << "SACCH recevied unparsable L3 frame " << *l3frame;
	}
	return message;
}



void SACCHLogicalChannel::serviceLoop()
{

	// run the loop
	unsigned count = 0;
	while (true) {

		// Throttle back if not active.
		if (!active()) {
			//OBJLOG(DEBUG) << "SACCH sleeping";
			sleepFrames(51);
			continue;
		}

		// TODO SMS -- Check to see if the tx queues are empty.  If so, send SI5/6,
		// otherwise sleep and continue;

		// Send alternating SI5/SI6.
		// These L3Frames were created with the UNIT_DATA primivitive.
		OBJLOG(DEBUG) << "sending SI5/6 on SACCH";
		if (count%2) {
			gBTS.regenerateSI5();
			LogicalChannel::send(gBTS.SI5Frame());
		}
		else LogicalChannel::send(gBTS.SI6Frame());
		count++;

		// Receive inbound messages.
		// This read loop flushes stray reports quickly.
		while (true) {

			OBJLOG(DEBUG) << "polling SACCH for inbound messages";
			bool nothing = true;

			// Process SAP0 -- RR Measurement reports
			L3Frame *rrFrame = LogicalChannel::recv(0,0);
			if (rrFrame) nothing=false;
			L3Message* rrMessage = processSACCHMessage(rrFrame);
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
					Control::HandoverDetermination(mMeasurementResults,this);
				} else {
					OBJLOG(NOTICE) << "SACCH SAP0 sent unaticipated message " << rrMessage;
				}
				delete rrMessage;
			}

			// Process SAP3 -- SMS
			L3Frame *smsFrame = LogicalChannel::recv(0,3);
			if (smsFrame) nothing=false;
			L3Message* smsMessage = processSACCHMessage(smsFrame);
			delete smsFrame;
			if (smsMessage) {
				const SMS::CPData* cpData = dynamic_cast<const SMS::CPData*>(smsMessage);
				if (cpData) {
					OBJLOG(INFO) << "SMS CPDU " << *cpData;
					Control::TransactionEntry *transaction = gTransactionTable.find(this);
					try {
						if (transaction) {
							Control::InCallMOSMSController(cpData,transaction,this);
						} else {
							OBJLOG(WARNING) << "in-call MOSMS CP-DATA with no corresponding transaction";
						}
					} catch (Control::ControlLayerException e) {
						//LogicalChannel::send(RELEASE,3);
						gTransactionTable.remove(e.transactionID());
					}
				} else {
					OBJLOG(NOTICE) << "SACCH SAP3 sent unaticipated message " << rrMessage;
				}
				delete smsMessage;
			}

			// Anything from the SIP side?
			// MTSMS (delivery from SIP to the MS)
			Control::TransactionEntry *sipTransaction = mTransactionFIFO.readNoBlock();
			if (sipTransaction) {
				OBJLOG(INFO) << "SIP-side transaction: " << sipTransaction;
				assert(sipTransaction->service() == L3CMServiceType::MobileTerminatedShortMessage);
				try {
					Control::MTSMSController(sipTransaction,this);
				} catch (Control::ControlLayerException e) {
					//LogicalChannel::send(RELEASE,3);
					gTransactionTable.remove(e.transactionID());
				}
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
void LogicalChannel::setPhy(float wRSSI, float wTimingError, double wTimestamp)
	{ assert(mSACCH); mSACCH->setPhy(wRSSI,wTimingError,wTimestamp); }
void LogicalChannel::setPhy(const LogicalChannel& other)
	{ assert(mSACCH); mSACCH->setPhy(*other.SACCH()); }
float LogicalChannel::RSSI() const
	{ assert(mSACCH); return mSACCH->RSSI(); }
float LogicalChannel::timingError() const
	{ assert(mSACCH); return mSACCH->timingError(); }
double LogicalChannel::timestamp() const
	{ assert(mSACCH); return mSACCH->timestamp(); }
int LogicalChannel::actualMSPower() const
	{ assert(mSACCH); return mSACCH->actualMSPower(); }
int LogicalChannel::actualMSTiming() const
	{ assert(mSACCH); return mSACCH->actualMSTiming(); }
const L3MeasurementResults& LogicalChannel::measurementResults() const
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
	mL2[0] = new FACCHL2(1,0);
	mL2[3] = new FACCHL2(1,3);
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


void CBCHLogicalChannel::send(const L3SMSCBMessage& msg)
{
	L3Frame frame(UNIT_DATA,88*8);
	msg.write(frame);
	LogicalChannel::send(frame);
}




bool LogicalChannel::waitForPrimitive(Primitive primitive, unsigned timeout_ms)
{
	bool waiting = true;
	while (waiting) {
		L3Frame *req = recv(timeout_ms);
		if (req==NULL) {
			LOG(NOTICE) << "timeout at uptime " << gBTS.uptime() << " frame " << gBTS.time();
			return false;
		}
		waiting = (req->primitive()!=primitive);
		delete req;
	}
	return true;
}


void LogicalChannel::waitForPrimitive(Primitive primitive)
{
	bool waiting = true;
	while (waiting) {
		L3Frame *req = recv();
		if (req==NULL) continue;
		waiting = (req->primitive()!=primitive);
		delete req;
	}
}

L3Frame* LogicalChannel::waitForEstablishOrHandover()
{
	while (true) {
		L3Frame *req = recv();
		if (req==NULL) continue;
		if (req->primitive()==ESTABLISH) return req;
		if (req->primitive()==HANDOVER_ACCESS) return req;
		LOG(INFO) << "LogicalChannel: Ignored primitive:"<<req->primitive();
		delete req;
	}
	return NULL;	// to keep the compiler happy
}



ostream& GSM::operator<<(ostream& os, const LogicalChannel& chan)
{
	os << chan.descriptiveString();
	return os;
}


void LogicalChannel::addTransaction(Control::TransactionEntry *transaction)
{
	assert(transaction->channel()==this);
	mTransactionFIFO.write(transaction);
}

// vim: ts=4 sw=4

