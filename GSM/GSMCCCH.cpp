/* 
* Copyright 2014 Range Networks, Inc.
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

#define LOG_GROUP LogGroup::GSM		// Can set Log.Level.GSM for debugging

#include "GSMLogicalChannel.h"
#include "GSMCCCH.h"
#include "GSMRadioResource.h"
#include <ControlTransfer.h>
#include <L3TranEntry.h>	// For NewTransactionTable.
#include <MAC.h>
#include <math.h>

using namespace Control;
using namespace GPRS;

namespace GSM {


static int sMaxAge;			///< Maximum allowed age of RACH in frames, a constant computed from config options.


CCCHLogicalChannel::CCCHLogicalChannel(unsigned wCcchGroup, const TDMAMapping& wMapping)
	:mCcchGroup(wCcchGroup), mRunning(false)
{
	unsigned TN = wCcchGroup * 2; // *2 to convert from CCCH_GROUP 
	this->mL1 = new CCCHL1FEC(wMapping,TN);
	this->connect(this->mL1);
}


static void *CCCHLogicalChannelServiceLoopAdapter(CCCHLogicalChannel* chan)
{
	chan->ccchServiceLoop();
	return NULL;
}

void CCCHLogicalChannel::ccchOpen()
{
	L2LogicalChannelBase::startl1();
	if (!mRunning) {
		mRunning=true;
		mServiceThread.start((void*(*)(void*))CCCHLogicalChannelServiceLoopAdapter,this);
	}
}

// This is a specialized thread-safe queue to be used only for the paging and related queues.
// The writer thread(s) can only write to the end of the queue.
// The single reader thread can do a protected iteration using a single captive iterator in the class.
// It does not support blocking wait so it is much simpler than InterthreadQueue.
template <class ETP>	// Entry Type, which is a Pointer.
class IterableQueue {
	typedef typename std::list<ETP>::iterator itr_t;
	Mutex miqLock;
	std::list<ETP> miqList;
	typename std::list<ETP>::iterator miqIterator;
	ETP iqResult() { return miqIterator == miqList.end() ? NULL : *miqIterator; }

	public:
	int *miqLoadPointer;  // If the descendent class sets this, it will be maintained with the number of elements in the queue.
	IterableQueue() : miqLoadPointer(NULL) {}

	void push_back(ETP elt) {
		ScopedLock lock(miqLock);
		miqList.push_back(elt);
		if (miqLoadPointer) (*miqLoadPointer)++;
	}
	//ETP pop_frontr() {
	//	ScopedLock lock(miqLock);
	//	if (miqList.empty()) { return 0; }	// Hopefully 0 has some distinct meaning for type ET.
	//	ETP result = miqList.front();
	//	miqList.pop_front();
	//	if (miqLoadPointer) (*miqLoadPointer)++;
	//	return result;
	//}

	ETP itBegin() {
		ScopedLock lock(miqLock);
		miqIterator = miqList.begin();
		return iqResult();
	}
	// It is the caller's responsibility to delete the element, or enqueue it elsewhere.
	ETP itRemove(ETP thisone) {	// The argument is redundant but helps check compliance with list semantics.
		LOG(DEBUG);
		ScopedLock lock(miqLock);
		assert(*miqIterator == thisone);
		miqIterator = miqList.erase(miqIterator);
		if (miqLoadPointer) (*miqLoadPointer)--;
		return iqResult();
	}
	ETP itSkip(ETP thisone) {	// The argument is redundant but helps check compliance with list semantics.
		ScopedLock lock(miqLock);
		assert(*miqIterator == thisone);
		miqIterator++;
		return iqResult();
	}

	int getSize() {
		ScopedLock lock(miqLock);
		return miqList.size();
	}
};

class PagingQ {
	public:
	InterthreadQueue<NewPagingEntry> mPageQ;
	void addPage(NewPagingEntry *npe) { mPageQ.write(npe); }
	unsigned getPagingLoad() { return mPageQ.size(); }
} gPagingQ;

// Global linkage:
int getPCHLoad() {
	return gPagingQ.getPagingLoad();
}


// Welcome to wonderful C++.
struct RachCompareAdapter {
	/** Compare the objects pointed to, not the pointers themselves. */
	// (pat) This is used when a RachInfo is placed in a priority_queue.
	// Return true if rach1 should appear before rach2 in the priority_queue,
	// meaning that rach2 will be serviced before rach1, since the stupid C++ priority_queue pops from the END of the queue.
	bool operator()(const RachInfo *rach1, const RachInfo *rach2) {
		assert(!rach1->mChan == !rach2->mChan);	// In any given queue, all raches have mChan, or none.
		if (rach1->mChan) {
			return rach1->mReadyTime > rach2->mReadyTime;
		} else {
			return rach1->mWhen > rach2->mWhen;
		}
	}
};


// This queue holds RACHes of all types waiting to be serviced.
InterthreadQueue<RachInfo> gRachq;


// The result is 3-state (LCH, !LCH and deleteMe==true, !LCH && deleteMe==false.)
// If it is a TCH or SDCCH return an allocated channel, or NULL if not possible,
// in which case the handset may ultimately be sent an immediate assignment reject.
// On return, if deleteMe, the caller will delete the rach.
static L2LogicalChannel *preallocateChForRach(RachInfo *rach, bool *deleteMe)
{
	*deleteMe = false;

	Time now = gBTS.time();
	int age = now - rach->mWhen;	// The result is number of frames and could be negative.
	if (age>sMaxAge) {
		LOG(WARNING) << "ignoring RACH burst with age " << age;
		*deleteMe = true;
		return NULL;
	}

	L2LogicalChannel *LCH = NULL;
	ChannelType chtype = decodeChannelNeeded(rach->mRA);
	if (chtype == PSingleBlock1PhaseType || chtype == PSingleBlock2PhaseType) {
		return NULL;	// this routine does nothing for this.
	} else if (chtype == TCHFType) {
		LCH = gBTS.getTCH();
	} else if (chtype == SDCCHType) {
		LCH = gBTS.getSDCCH();
	} else {
		LOG(NOTICE) << "RACH burst for unsupported service RA=" << rach->mRA;
		// (pat) 4-2014: Stop allocating channels for this.  We get a lot of false RACHes so lets ignore those with unrecognized RA.
		// LCH = gBTS.getSDCCH();
		*deleteMe = true;
		return NULL;
	}

	if (!LCH) {
		LOG(DEBUG) << "No channels availablable, RACH discarded";
		*deleteMe = true;
		return NULL;
	}

	int initialTA = rach->initialTA();
	assert(initialTA >= 0 && initialTA <= 62);	// enforced by AccessGrantResponder.
	LCH->l1InitPhy(rach->RSSI(),initialTA,gBTS.clock().systime(rach->mWhen.FN()));
	LCH->lcstart();
	rach->mChan = LCH;
	Time sacchStart = LCH->getSACCH()->getNextWriteTime();
	rach->mReadyTime = sacchStart;
	// There is a race whether the thread that runs SACCH can run before sacchStart time, and if not
	// the SACCH will not be transmitted at this time.
	// Therefore, if sacchStart time is close, add a whole sacch frame, 104 frames == 480ms.
	now = gBTS.time();	// Must update this because getTCH blocked.
	int diff = rach->mReadyTime - now;	// Returns number of 4.8ms frames.  // positive diff is in the future.
	if (diff <  26) {	// very conservative.
		// (pat) We are making SURE the SACCH has transmitted a frame before we tell the handset to use the channel.
		// (pat) Not sure this is necessary.
		rach->mReadyTime += 104;
	}
	if (rach->mReadyTime < now + 26) {	// negative diff means sacchStart is still in the past.
		// Should never happen.
		rach->mReadyTime = now + 104;
	}
	LOG(DEBUG) "RACH "<<LOGVAR(sacchStart)<<LOGVAR(now)<<LOGVAR(diff)<<LOGVAR2("readyTime",rach->mReadyTime);
	return LCH;
}

void enqueueRach(RachInfo *rip)
{
	// TODO: Phones next to the base station will include the operator's.
	// We should service them first, even if they are just LUR.

	bool deleteMe = false;
	preallocateChForRach(rip,&deleteMe);
	if (deleteMe) {
		delete rip;
		return;
	}

	gRachq.write(rip);
}

// This queue holds ImmediateAssignments sent from GPRS waiting be serviced.
// I believe that now GPRS will send assignments on PACCH if it can, so all elements here
// are sent only after the handset has stopped monitoring PACCH and gone back to monitoring CCCH.
// DRX mode appears to be on a separate timer from PACCH monitoring, so the elements in this queue
// may be in DRX mode or not.
// If the MS is in not in DRX mode we can send on any CCCH-AGCH.
// After the DRX time period expires these messages can only be sent on a paging channel.
// They are in NewPagingEntry format so they can be moved to the paging queue trivially.
IterableQueue<NewPagingEntry*> gGprsCcchMessageQ;

// Global linkage:
int getAGCHLoad()
{
	int result = 0;
		result += gRachq.size();
	return result + gGprsCcchMessageQ.getSize();
}

// This is called from the GPRS directory.
void pagerAddCcchMessageForGprs(NewPagingEntry *npe)
{
	LOG(DEBUG) <<LOGVAR(npe);
	gGprsCcchMessageQ.push_back(npe);
}


bool CCCHLogicalChannel::sendGprsCcchMessage(NewPagingEntry *gprsMsg, GSM::Time &frameTime)
{
	if (! gprsPageCcchSetTime(gprsMsg->mGprsClient,gprsMsg->mImmAssign,frameTime.FN())) { return false; }
	L2LogicalChannelBase::l2sendm(*(gprsMsg->mImmAssign),L3_UNIT_DATA);
	return true;
}


int getAGCHPending() { return 0; }


// Return true if the CCCH frame was used.
bool CCCHLogicalChannel::processRaches()
{
	while (RachInfo *rach = gRachq.readNoBlock())
	{
		Time now = gBTS.time();
		int age = now - rach->mWhen;	// The result is number of frames and could be negative.
		if (age>sMaxAge) {
			LOG(WARNING) << "ignoring RACH burst with age " << age;
			if (rach->mChan) {
				LOG(INFO) << "BTS congestion: unable to process RACH for pre-allocated channel within expiration time";
				rach->mChan->l2sendp(L3_HARDRELEASE_REQUEST);	// (pat) added 9-6-2013
			}
			delete rach;
			continue;	// Did not use the CCCH frame yet.
		}


		ChannelType chtype = decodeChannelNeeded(rach->mRA);
		LOG(DEBUG) <<LOGVAR(*rach) <<LOGVAR(chtype) <<LOGVAR(mCcchGroup);

		if (chtype == PSingleBlock1PhaseType || chtype == PSingleBlock2PhaseType) {
			assert(rach->mChan == NULL);
			if (0 == gConfig.getNum("GPRS.Enable")) {
				// GPRS service request when the beacon advertises no beacon support.
				// This was a spurious RACH message or a stupid handset.  Ignore it.
				assert(rach->mChan == NULL);
				delete rach;
				continue;
			}
			// Regardless of the type of GPRS request, we will send a single-block uplink,
			// which the MS will (most likely) use to send us a PacketResourceRequest.
			// First request a single-block reservation from GPRS.  If GPRS resources are busy,
			// this will return NULL and we will send reject the RACH.
			L3ImmediateAssignment *iap = GPRS::makeSingleBlockImmediateAssign(rach, mCcchNextWriteTime.FN() + 4);
			if (iap) {
				L2LogicalChannelBase::l2sendm(*iap,L3_UNIT_DATA);
				delete iap;
				delete rach;
			} else {
			}
			return true;	// We processed this rach and used the CCCH, one way or another.
		}

//		L2LogicalChannel *LCH;
//		if (chtype == TCHFType) {
//			// FIXME: This blocks at L2LAPDm::sendIdle!
//			LCH = gBTS.getTCH();
//		} else if (chtype == SDCCHType) {
//			// We may reserve some SDCCH for CC and SMS.
//			if (requestingLUR(rach->mRA)) {
//				int SDCCHAvailable = gBTS.SDCCHAvailable();
//				int SDCCHReserve = gConfig.getNum("GSM.Channels.SDCCHReserve");
//				if (requestingLUR(rach->mRA) && SDCCHAvailable <= SDCCHReserve) {
//					// (pat 2-2014) Changed this message and downgraded from CRIT.
//					LOG(CRIT) << "LUR [Location Update Request] congestion, insufficient "<<LOGVAR(SDCCHAvailable) << " <= " <<LOGVAR(SDCCHReserve);
//					goto failure;
//				}
//			}
//
//			LCH = gBTS.getSDCCH();
//		} else {
//			LOG(NOTICE) << "RACH burst for unsupported service RA=" << rach->mRA;
//			LCH = gBTS.getSDCCH();	// Try anyway.
//		}
//
//
//		// Nothing available?
//		if (!LCH) {
//			failure:
#if 0
//			return false;
#endif
//		}

		if (! rach->mChan) {
			delete rach;
			continue;
		}

		// Success.  Send an ImmediateAssignment.
		int initialTA = rach->initialTA();
		assert(initialTA >= 0 && initialTA <= 62);	// enforced by AccessGrantResponder.
		//LCH->l1InitPhy(rach->RSSI(),initialTA,gBTS.clock().systime(rach->mWhen.FN()));
		gReports.incr("OpenBTS.GSM.RR.RACH.TA.Accepted",(int)(initialTA));
		L2LogicalChannel *LCH = rach->mChan;

		// TODO: Update T3101.

		L3ImmediateAssignment assign(
			L3RequestReference(rach->mRA,rach->mWhen),
			LCH->channelDescription(),
			L3TimingAdvance(initialTA)
		);
		//assign.setStartFrame(rach->mReadyTime.FN() + 104);

		if (0) {	// This was for debugging.  Adding this delay made the layer1 connection reliable.
			// Delay the channel assignment until the SACCH is known to be transmitting...
			Time sacchStart = LCH->getSACCH()->getNextWriteTime();
			now = gBTS.time();	// Must update this because getTCH blocked.
			int msecsDelay = ((sacchStart - now.FN()).FN() * gFrameMicroseconds) / 1000;
			// Add an extra gratuitous 250ms delay for testing.
			// msecsDelay += 250;
			if (msecsDelay < 0) { msecsDelay = 0; }
			if (msecsDelay) {
				LCH->addT3101(msecsDelay);
				//assign.setStartFrame(now.FN() + (1000 * msecsDelay) / gFrameMicroseconds);
				usleep(msecsDelay * 1000);
			}
			LOG(INFO) << "sending L3ImmediateAssignment " << LCH->descriptiveString() <<LOGVAR(msecsDelay) <<" " <<assign;
		}

		L2LogicalChannelBase::l2sendm(assign,L3_UNIT_DATA);
		delete rach;
		return true;	// We used this CCCH.
	}
	return false;	// CCCH frame not used.
}


// Simplified version for public release
bool CCCHLogicalChannel::processPages()
{
	while (NewPagingEntry *npe1 = gPagingQ.mPageQ.readNoBlock()) {
		LOG(DEBUG)<<LOGVAR(npe1);
		if (npe1->mGprsClient) {	// Is it a GPRS page?
			// Add 51 to the frame time because the message because the MS may be on the other 51-multiframe.
			Time future(mCcchNextWriteTime + 52);
			if (! sendGprsCcchMessage(npe1,future)) {
				delete npe1;	// In the incredibly unlikely event that the above failed, just give up.
				continue;
			}
		} else {
			const L3MobileIdentity& id1 = npe1->getMobileId();
			ChannelType type1 = npe1->getGsmChanType();
			L3PagingRequestType1 page1(id1,type1);
			L2LogicalChannelBase::l2sendm(page1,L3_UNIT_DATA);
		}
		if (++npe1->mSendCount < 2) {	// Send each page twice.
			gPagingQ.mPageQ.write_front(npe1);	// Put it back for resend in the next multiframe.
		} else {
			delete npe1;
		}
		return true;
	}
	return false;	// CCCH unused.
}



// Return true if the current CCCH was used.
bool CCCHLogicalChannel::ccchServiceQueue()
{
	// Update the time when the next CCCH frame will be sent.
	// All the l2sendm/l2sendp calls below block using waitToSend()
	// on the mPrevWriteTime set by the getNextWriteTime call.
	mCcchNextWriteTime = getNextWriteTime();
	if (gBTS.btsHold()) { return false; }

	// Is this CCCH used for pages?  We determine this by looking at the frame number within the 51-multiframe.
	int paging_block_index = mRevPCH[mCcchNextWriteTime.FN() % 51];


	if (paging_block_index >= 0 && processPages()) { return true; }

	// We did not use this CCCH for a page, so lets look for something else to send.
	if (processRaches()) {
		return true;	// We used this CCCH.
	}

	// GPRS messages may be in DRX mode or not.
	// If they are in DRX mode, move them to a paging queue and set foundSomeNewPages.
	bool foundSomeNewPages = false;

	for (NewPagingEntry *gprsIt = gGprsCcchMessageQ.itBegin(); gprsIt; ) {
		NewPagingEntry *gprsMsg = gprsIt;	// This is redundant; the items are not currently modified by iteration.
		LOG(DEBUG) << "processing"<<LOGVAR(gprsMsg);
		GSM::Time drxBeginTime(gprsMsg->mDrxBegin);
		if (mCcchNextWriteTime >= drxBeginTime) {
			// This MS is now in DRX mode.  Move the message to the paging queue.
			gprsIt = gGprsCcchMessageQ.itRemove(gprsIt);
			gPagingQ.addPage(gprsMsg);
			foundSomeNewPages = true;
			continue;
		}
		// Woo hoo!  Send out the message.
		if (sendGprsCcchMessage(gprsMsg,mCcchNextWriteTime)) {
			gprsIt = gGprsCcchMessageQ.itRemove(gprsIt);
			delete gprsMsg;
			return true;	// We used this CCCH.
		} else {
			// Failed.  That means we were unable to make a reservation for some reason.
			// While unlikely, it is possible that a reservation on one channel failed while reservations on other
			// channels would succeed, so dont block the entire queue due to this one failure, keep going.
			gprsIt = gGprsCcchMessageQ.itSkip(gprsIt);
		}
	}

	if (foundSomeNewPages);	// shut up gcc

	return false;	// We have not used this CCCH.
}

// pats TODO: Send the transceiver an idle frame rather than doing it here.
// There is one of these for each timeslot, ie, for ccch_group.
void CCCHLogicalChannel::ccchServiceLoop()
{
	// build the idle frame
	static const L3PagingRequestType1 filler;
	static const L3Frame idleFrame(filler,L3_UNIT_DATA);

	bool isCCCHCombined = gControlChannelDescription->isCCCHCombined();
	// TODO: Send idle frame to transceiver.

	// Calculate maximum number of frames of delay.
	// See GSM 04.08 3.3.1.1.2 for the logic here.
	static const unsigned txInteger = gConfig.getNum("GSM.RACH.TxInteger");
	assert(txInteger <= 15);
	// RACH slot definition: we see in GSM 5.02 clause 7 table 3 of 9 that a RACH can appear on timeslots 0,2,4, or 6, and in table 5 of 9 we
	// see that for a non-combined CCCH-CONF any of the 51 frames can be used, and for combined CCCH-CONF the available (51*4=204 slots/51-multiframe)
	// frames are: B4, B5, B14, B15 ... B36, B45, B46 (27*4=108 slots/51-multiframe.)
	// GSM 4.08 11.1.1, And I quote: "The minimum value of this timer is equal to the time taken by T+2S slots of the mobile station's
	// RACH. S and T are defined in sub-clause 3.3.1.2. The maximum value of this timer is 5 seconds."
	static const int stval = GSM::RACHSpreadSlots[txInteger] + 2*(isCCCHCombined ? GSM::RACHWaitSParamCombined[txInteger] : GSM::RACHWaitSParam[txInteger]);
	// Subtract some frames from the maxAge to make sure we still have time left to send it; the amount should be 4 frames
	// plus some time for the MS to receive and decode it plus the slack induced by the OpenBTS tranceiver interface, which we do not know.
	sMaxAge = min(stval, (int)(5 * 51 * 4.2)) - 6;

	for (int i = 0; i < 51; i++) { mRevPCH[i] = -1; }	// -1 means not used for paging.

		mRevPCH[16] = 0;

	int cnt = 0;
	while (! gBTS.btsShutdown()) {

		if (! ccchServiceQueue()) {
			// Nothing to send on CCCH frame, so just send an idle frame.
			//LOG(DEBUG)<<"sending page as idleframe";
			l2sendf(idleFrame);
		}

	}
	if (cnt);	// shut up gcc.
}

static unsigned newPageAll()
{
	LOG(DEBUG);
	Control::NewPagingList_t pages;
	Control::MMGetPages(pages,false);

	LOG(INFO) << "paging " << pages.size() << " mobile(s)";

	// Move the pages from the single incoming queue to the individual PCH paging queues.
	for (Control::NewPagingList_t::iterator lp = pages.begin(); lp != pages.end(); lp++) {
		gPagingQ.addPage(new NewPagingEntry(*lp));
	}
	return pages.size();
}


size_t NewPager::pagingEntryListSize()
{
	Control::NewPagingList_t pages;
	MMGetPages(pages,false);
	return pages.size();
}

void NewPager::serviceLoop()
{
	Timeval nextTime;
	while (! gBTS.btsShutdown()) {
		// Wait for pending activity to clear PCH.
		if (unsigned load = gPagingQ.getPagingLoad()) {
			LOG(DEBUG) << "Pager waiting with load " << load;
			sleepFrames(51); // There could be multiple paging channels, in which case this is longer than necessary, but no matter.
			continue;
		}
		// nextTime controls how quickly we resend pages.
		if (! nextTime.passed()) {
			sleepFrames(51);
			continue;
		}
		newPageAll();
		nextTime.future(5000); // Wait 5 seconds between paging sets.
	}
}


void* NewPager::PagerServiceLoopAdapter(NewPager *pager)
{
	pager->serviceLoop();
	return NULL;
}

void NewPager::start()
{
	if (mRunning) return;
	mRunning=true;
	mPagingThread.start((void* (*)(void*))PagerServiceLoopAdapter, (void*)this);
}

NewPager gPager;

void PagerStart()
{
	gPager.start();
}

};	// namespace GSM
