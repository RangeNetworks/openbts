/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/


#include "GSMConfig.h"
#include "GSMTransfer.h"
#include "GSMLogicalChannel.h"
#include "GPRSExport.h"
#include <ControlCommon.h>
#include <Logger.h>
#include <NeighborTable.h>
#include <Reporting.h>
#include <Globals.h>



using namespace std;
using namespace GSM;



GSMConfig::GSMConfig()
	:mCBCH(NULL),
	mSI5Frame(UNIT_DATA),mSI6Frame(UNIT_DATA),
	mSI1(NULL),mSI2(NULL),mSI3(NULL),mSI4(NULL),
	mSI5(NULL),mSI6(NULL),
	mStartTime(::time(NULL)),
	mChangemark(0)
{
}

void GSMConfig::init() 
{
	mBand = (GSMBand)gConfig.getNum("GSM.Radio.Band");
	mT3122 = gConfig.getNum("GSM.Timer.T3122Min");
 	regenerateBeacon();
}

void GSMConfig::start()
{
	mPowerManager.start();
	// Do not call this until the paging channels are installed.
	mPager.start();
	// If requested, start gprs to allocate channels at startup.
	// Otherwise, channels are allocated on demand, if possible.
	if (GPRS::configGprsChannelsMin() > 0) {
		// Start gprs.
		GPRS::gprsStart();
	}
	// Do not call this until AGCHs are installed.
	mAccessGrantThread.start(Control::AccessGrantServiceLoop,NULL);
}




void GSMConfig::regenerateBeacon()
{
	// FIXME -- Need to implement BCCH_CHANGE_MARK

	gReports.incr("OpenBTS.GSM.RR.BeaconRegenerated");
	mChangemark++;

	// Update everything from the configuration.
	LOG(NOTICE) << "regenerating system information messages, changemark " << mChangemark;

	// BSIC components
	mNCC = gConfig.getNum("GSM.Identity.BSIC.NCC");
	LOG_ASSERT(mNCC<8);
	mBCC = gConfig.getNum("GSM.Identity.BSIC.BCC");
	LOG_ASSERT(mBCC<8);

	// MCC/MNC/LAC
	mLAI = L3LocationAreaIdentity();

	std::vector<unsigned> neighbors = gNeighborTable.ARFCNList();
	// if the neighbor list is emtpy, put ourselves on it
	if (neighbors.size()==0) neighbors.push_back(gConfig.getNum("GSM.Radio.C0"));

	// Now regenerate all of the system information messages.

	// SI1
	L3SystemInformationType1 *SI1 = new L3SystemInformationType1;
	if (mSI1) delete mSI1;
	mSI1 = SI1;
	LOG(INFO) << *SI1;
	L3Frame SI1L3(UNIT_DATA);
	SI1->write(SI1L3);
	L2Header SI1Header(L2Length(SI1L3.L2Length()));
	mSI1Frame = L2Frame(SI1Header,SI1L3);
	LOG(DEBUG) << "mSI1Frame " << mSI1Frame;

	// SI2
	L3SystemInformationType2 *SI2 = new L3SystemInformationType2(neighbors);
	if (mSI2) delete mSI2;
	mSI2 = SI2;
	LOG(INFO) << *SI2;
	L3Frame SI2L3(UNIT_DATA);
	SI2->write(SI2L3);
	L2Header SI2Header(L2Length(SI2L3.L2Length()));
	mSI2Frame = L2Frame(SI2Header,SI2L3);
	LOG(DEBUG) << "mSI2Frame " << mSI2Frame;

	// SI3
	L3SystemInformationType3 *SI3 = new L3SystemInformationType3;
	if (mSI3) delete mSI3;
	mSI3 = SI3;
	LOG(INFO) << *SI3;
	L3Frame SI3L3(UNIT_DATA);
	SI3->write(SI3L3);
	L2Header SI3Header(L2Length(SI3L3.L2Length()));
	mSI3Frame = L2Frame(SI3Header,SI3L3,true);
	LOG(DEBUG) << "mSI3Frame " << mSI3Frame;

	// SI4
	L3SystemInformationType4 *SI4 = new L3SystemInformationType4;
	if (mSI4) delete mSI4;
	mSI4 = SI4;
	LOG(INFO) << *SI4;
	LOG(INFO) << SI4;
	L3Frame SI4L3(UNIT_DATA);
	SI4->write(SI4L3);
	//printf("SI4 bodylength=%d l2len=%d\n",SI4.l2BodyLength(),SI4L3.L2Length());
	//printf("SI4L3.size=%d\n",SI4L3.size());
	L2Header SI4Header(L2Length(SI4L3.L2Length()));
	mSI4Frame = L2Frame(SI4Header,SI4L3,true);
	LOG(DEBUG) << "mSI4Frame " << mSI4Frame;

#if GPRS_PAT | GPRS_TEST
	// SI13. pat added 8-2011 to advertise GPRS support.
	L3SystemInformationType13 *SI13 = new L3SystemInformationType13;
	LOG(INFO) << *SI13;
	L3Frame SI13L3(UNIT_DATA);
	//printf("start=%d\n",SI13L3.size());
	SI13->write(SI13L3);
	//printf("end=%d\n",SI13L3.size());
	//printf("SI13 bodylength=%d l2len=%d\n",SI13.l2BodyLength(),SI13L3.L2Length());
	//printf("SI13L3.size=%d\n",SI13L3.size());
	L2Header SI13Header(L2Length(SI13L3.L2Length()));
	mSI13Frame = L2Frame(SI13Header,SI13L3,true);
	LOG(DEBUG) << "mSI13Frame " << mSI13Frame;
#endif

	// SI5
	regenerateSI5();

	// SI6
	L3SystemInformationType6 *SI6 = new L3SystemInformationType6;
	if (mSI6) delete mSI6;
	mSI6 = SI6;
	LOG(INFO) << *SI6;
	SI6->write(mSI6Frame);
	LOG(DEBUG) "mSI6Frame " << mSI6Frame;

}


void GSMConfig::regenerateSI5()
{
	std::vector<unsigned> neighbors = gNeighborTable.ARFCNList();
	// if the neighbor list is emtpy, put ourselves on it
	if (neighbors.size()==0) neighbors.push_back(gConfig.getNum("GSM.Radio.C0"));
	L3SystemInformationType5 *SI5 = new L3SystemInformationType5(neighbors);
	if (mSI5) delete mSI5;
	mSI5 = SI5;
	LOG(INFO) << *SI5;
	SI5->write(mSI5Frame);
	LOG(DEBUG) << "mSI5Frame " << mSI5Frame;
}

CCCHLogicalChannel* GSMConfig::minimumLoad(CCCHList &chanList)
{
	if (chanList.size()==0) return NULL;
	CCCHList::iterator chan = chanList.begin();
	CCCHLogicalChannel *retVal = *chan;
	unsigned minLoad = (*chan)->load();
	++chan;
	while (chan!=chanList.end()) {
		unsigned thisLoad = (*chan)->load();
		if (thisLoad<minLoad) {
			minLoad = thisLoad;
			retVal = *chan;
		}
		++chan;
	}
	return retVal;
}






template <class ChanType> ChanType* getChan(vector<ChanType*>& chanList, bool forGprs)
{
	const unsigned sz = chanList.size();
	LOG(DEBUG) << "sz=" << sz;
	if (sz==0) return NULL;
	// (pat) Dont randomize for GPRS!  GPRS requires that channels are returned
	// in order for the initial channels allocated on C0.
	// We shouldnt randomize at all because we want RR TCH to come from the
	// front of the list and GPRS from the back.
	unsigned pos = 0;
	const char *configRandomize = "GSM.Channels.Randomize";
	if (gConfig.defines(configRandomize)) {
		if (forGprs) {
			// If the parameter is 'required', the gConfig.remove fails, but dont print a zillion messages.
			static bool once = true;
			if (once) {
				LOG(ALERT) << "Config parameter '" << configRandomize << "' is incompatible with GPRS, removed";
				once = false;
			}
			gConfig.remove(configRandomize);
		} else {
			pos = random() % sz;
		}
	}
	for (unsigned i=0; i<sz; i++, pos = (pos+1)%sz) {
		LOG(DEBUG) << "pos=" << pos << " " << i << "/" << sz;
		ChanType *chan = chanList[pos];
		if (! chan->inUseByGPRS() && chan->recyclable()) return chan;
	}
	return NULL;
}

// Are the two channels adjacent?
template <class ChanType>
bool testAdjacent(ChanType *ch1, ChanType *ch2)
{
	return (ch1->CN() == ch2->CN() && ch1->TN() == ch2->TN()-1);
}

// Return the goodness of this possible match of gprs channels.
// Higher numbers are gooder.
template <class ChanType>
int testGoodness(vector<ChanType*>& chanList, int lo, int hi)
{
	int goodness = 0;
	if (lo > 0) {
		ChanType *ch1 = chanList[lo-1];	// ch1 is below to ch lo.
		if (testAdjacent(ch1,chanList[lo])) {
			// The best match is adjacent to other gprs channels.
			if (ch1->inUseByGPRS()) { goodness += 2; }
			// The next best is an empty adjacent channel.
			else if (ch1->recyclable()) { goodness += 1; }
		}
	}
	if (hi < (int)chanList.size()-1) {
		ChanType *ch2 = chanList[hi+1];	// ch2 is above ch hi
		if (testAdjacent(ch2,chanList[hi])) {
			if (ch2->inUseByGPRS()) { goodness += 2; }
			else if (ch2->recyclable()) { goodness += 1; }
		}
	}
	return goodness;
}

// (pat) 6-20-2012: To increase the likelihood that GPRS channels will be adjacent,
// GSM RR channels will be allocated from the front of the channel list
// and GPRS from the end.
// This function allocates a group of channels for gprs.
// Look for the largest group of adjacent channels <= groupSize.
// Give preference to channels that are adjacent to channels already
// allocated for gprs, or to empty channels.
// Give second preference to groups near the end of the channel list.
// Return the allocated channels in the array pointed to by results and
// return number of channels found.
template <class ChanType>
static unsigned getChanGroup(vector<ChanType*>& chanList, ChanType **results)
{
	const unsigned sz = chanList.size();
	if (sz==0) return 0;

	const bool backwards = true;	// Currently we always search backwards.
	// To search forwards, dont forget to invert besti,bestn below
	ChanType *prevFreeCh = NULL;	// unneeded initialization
	int curN = 0;					// current number of adjacent free channels.
	int bestI=0, bestN=0;			// best match
	int bestGoodness = 0;			// goodness of best match
	for (unsigned i=0; i<sz; i++) {
		ChanType *chan = chanList[backwards ? sz-i-1 : i];
		if (chan->inUseByGPRS()) { continue; }
		if (! chan->recyclable()) { continue; }
		if (bestN == 0) {
			bestI = i;
			curN = bestN = 1;
			bestGoodness = testGoodness(chanList,bestI,bestN);
		} else {
			if (testAdjacent<ChanType>(chan,prevFreeCh)) {
				curN++; 	// chan is adjacent to prevCh.
				int curGoodness = testGoodness(chanList,i,curN);
				if (curN > bestN || (curN == bestN && curGoodness > bestGoodness)) {
					// Best so far, so remember it.
					bestN = curN;
					bestI = i;
					bestGoodness = curGoodness;
					// optional early termination test
					//if (bestN >= groupSize && bestIsAdjacent) { goto finished; }
				}
			} else {
				curN = 0;
			}
		}
		prevFreeCh = chan;
	}
	//finished:
	for (int j = 0; j < bestN; j++) {
		results[j] = chanList[bestI+j];
	}
	return bestN;
}

// Allocate a group of channels for gprs.
// See comments at getChanGroup.
int GSMConfig::getTCHGroup(int groupSize,TCHFACCHLogicalChannel **results)
{
	ScopedLock lock(mLock);
	int nfound = getChanGroup<TCHFACCHLogicalChannel>(mTCHPool,results);
	for (int i = 0; i < nfound; i++) {
		results[i]->debugGetL1()->setGPRS(true,NULL);
	}
	return nfound;
}





SDCCHLogicalChannel *GSMConfig::getSDCCH()
{
	LOG(DEBUG);
	ScopedLock lock(mLock);
	LOG(DEBUG);
	SDCCHLogicalChannel *chan = getChan<SDCCHLogicalChannel>(mSDCCHPool,0);
	LOG(DEBUG);
	if (chan) chan->open();
	LOG(DEBUG);
	return chan;
}


// (pat) By a very tortuous path, chan->open() calls L1Encoder::open() and L1Decoder::open(),
// which sets mActive in both and resets the timers.
TCHFACCHLogicalChannel *GSMConfig::getTCH(
	bool forGPRS,	// If true, allocate the channel to gprs, else to RR use.
	bool onlyCN0)	// If true, allocate only channels on the lowest ARFCN.
{
	LOG(DEBUG);
	ScopedLock lock(mLock);
	//if (GPRS::GPRSDebug) {
	//	const unsigned sz = mTCHPool.size();
	//	char buf[300];  int n = 0;
	//	for (unsigned i=0; i<sz; i++) {
	//		TCHFACCHLogicalChannel *chan = mTCHPool[i];
	//		n += sprintf(&buf[n],"ch=%d:%d,g=%d,r=%d ",chan->CN(),chan->TN(),
	//				chan->inUseByGPRS(),chan->recyclable());
	//	}
	//	LOG(WARNING)<<"getTCH list:"<<buf;
	//}
	TCHFACCHLogicalChannel *chan = getChan<TCHFACCHLogicalChannel>(mTCHPool,forGPRS);
	// (pat) We have to open it or set gprs mode before returning to avoid a race.
	if (chan) {
		// The channels are searched in order from low to high, so if the first channel
		// found is not on CN0, we have failed.
		//LOG(DEBUG)<<"getTCH returns"<<LOGVAR2("chan->CN",chan->CN());
		if (onlyCN0 && chan->CN()) { return NULL; }
		if (forGPRS) {
			// (pat) Reserves channel for GPRS, but does not start delivering bursts yet.
			chan->debugGetL1()->setGPRS(true,NULL);
			return chan;
		}
		chan->open();	// (pat) LogicalChannel::open();  Opens mSACCH also.
		gReports.incr("OpenBTS.GSM.RR.ChannelAssignment");
	} else {
		//LOG(DEBUG)<<"getTCH returns NULL";
	}
	LOG(DEBUG);
	return chan;
}



template <class ChanType> size_t chanAvailable(const vector<ChanType*>& chanList)
{
	size_t count = 0;
	for (unsigned i=0; i<chanList.size(); i++) {
		if (chanList[i]->inUseByGPRS()) { continue; }
		if (chanList[i]->recyclable()) count++;
	}
	return count;
}



size_t GSMConfig::SDCCHAvailable() const
{
	ScopedLock lock(mLock);
	return chanAvailable<SDCCHLogicalChannel>(mSDCCHPool);
}

size_t GSMConfig::TCHAvailable() const
{
	ScopedLock lock(mLock);
	return chanAvailable<TCHFACCHLogicalChannel>(mTCHPool);
}


size_t GSMConfig::totalLoad(const CCCHList& chanList) const
{
	size_t total = 0;
	for (unsigned i=0; i<chanList.size(); i++) {
		total += chanList[i]->load();
	}
	return total;
}



unsigned countActive(const SDCCHList& chanList)
{
	unsigned active = 0;
	const unsigned sz = chanList.size();
	for (unsigned i=0; i<sz; i++) {
		if (!chanList[i]->recyclable()) active++;
	}
	return active;
}


unsigned countActive(const TCHList& chanList)
{
	unsigned active = 0;
	const unsigned sz = chanList.size();
	// Start the search from a random point in the list.
	for (unsigned i=0; i<sz; i++) {
		if (chanList[i]->inUseByGPRS()) continue;
		if (!chanList[i]->recyclable()) active++;
	}
	return active;
}

unsigned countAvailable(const TCHList& chanList)
{
	unsigned available = 0;
	const unsigned sz = chanList.size();
	// Start the search from a random point in the list.
	for (unsigned i=0; i<sz; i++) {
		if (chanList[i]->inUseByGPRS()) continue;
		available++;
	}
	return available;
}


unsigned GSMConfig::SDCCHActive() const
{
	return countActive(mSDCCHPool);
}

unsigned GSMConfig::TCHActive() const
{
	return countActive(mTCHPool);
}

unsigned GSMConfig::TCHTotal() const
{
	return countAvailable(mTCHPool);
}




unsigned GSMConfig::T3122() const
{
	ScopedLock lock(mLock);
	return mT3122;
}

unsigned GSMConfig::growT3122()
{
	unsigned max = gConfig.getNum("GSM.Timer.T3122Max");
	ScopedLock lock(mLock);
	unsigned retVal = mT3122;
	mT3122 += (random() % mT3122) / 2;
	if (mT3122>(int)max) mT3122=max;
	return retVal;
}


unsigned GSMConfig::shrinkT3122()
{
	unsigned min = gConfig.getNum("GSM.Timer.T3122Min");
	ScopedLock lock(mLock);
	unsigned retVal = mT3122;
	mT3122 -= (random() % mT3122) / 2;
	if (mT3122<(int)min) mT3122=min;
	return retVal;
}



void GSMConfig::createCombination0(TransceiverManager& TRX, unsigned TN)
{
	// This channel is a dummy burst generator.
	// This should not be applied to C0T0.
	LOG_ASSERT(TN!=0);
	LOG(NOTICE) << "Configuring dummy filling on C0T " << TN;
	ARFCNManager *radio = TRX.ARFCN(0);
	radio->setSlot(TN,0);	// (pat) 0 => Transciever.h enum ChannelCombination = FILL
}


void GSMConfig::createCombinationI(TransceiverManager& TRX, unsigned CN, unsigned TN)
{
	LOG_ASSERT((CN!=0)||(TN!=0));
	LOG(NOTICE) << "Configuring combination I on C" << CN << "T" << TN;
	ARFCNManager *radio = TRX.ARFCN(CN);
	radio->setSlot(TN,1);	// (pat) 1 => Transciever.h enum ChannelCombination = I
	TCHFACCHLogicalChannel* chan = new TCHFACCHLogicalChannel(CN,TN,gTCHF_T[TN]);
	chan->downstream(radio);
	Thread* thread = new Thread;
	thread->start((void*(*)(void*))Control::DCCHDispatcher,chan);
	chan->open();
	gBTS.addTCH(chan);
}


void GSMConfig::createCombinationVII(TransceiverManager& TRX, unsigned CN, unsigned TN)
{
	LOG_ASSERT((CN!=0)||(TN!=0));
	LOG(NOTICE) << "Configuring combination VII on C" << CN << "T" << TN;
	ARFCNManager *radio = TRX.ARFCN(CN);
	radio->setSlot(TN,7);	// (pat) 7 => Transciever.h enum ChannelCombination = VII
	for (int i=0; i<8; i++) {
		SDCCHLogicalChannel* chan = new SDCCHLogicalChannel(CN,TN,gSDCCH8[i]);
		chan->downstream(radio);
		Thread* thread = new Thread;
		thread->start((void*(*)(void*))Control::DCCHDispatcher,chan);
		chan->open();
		gBTS.addSDCCH(chan);
	}
}


void GSMConfig::hold(bool val)
{
	ScopedLock lock(mLock);
	mHold = val;
}

bool GSMConfig::hold() const
{
	ScopedLock lock(mLock);
	return mHold;
}



#if ENABLE_PAGING_CHANNELS

// 5-27-2012 pat added:
// Routines for CCCH messages to add real paging channels.
// Added in the simplest possible way to avoid destabilizing anything.
// GPRS still needs a pretty major rewrite of the underlying CCCHLogicalChannel class
// to reduce the latency, but paging queues at least relieve the congestion on CCCH.
// In DRX [Discontinuous Reception] mode the MS listens only to a subset of CCCH based on its IMSI.
// This is a GPRS thing but dependent on the configuration of CCCH in our system.
// See: GSM 05.02 6.5.2: Determination of CCCH_GROUP and PAGING_GROUP for MS in idle mode.
void GSMConfig::crackPagingFromImsi(	
	unsigned imsiMod1000	// The phones imsi mod 1000, so just atoi the last 3 digits.
	unsigned &paging_block_index,	// Returns which of the paging ccchs to use.
	unsigned &multiframe_index	// Returns which 51-multiframe to use.
	)
{
	L3ControlChannelDescription mCC;

	// BS_CCCH_SDCCH_COMB is defined in GSM 05.02 3.3.2.3;
	int bs_cc_chans;			// The number of ccch timeslots per 51-multiframe.
	bool bs_ccch_sdcch_comb;	// temp var indicates if sdcch is on same TS as ccch.
	switch (mCC.mCCCH_CONF) {
	case 0: bs_cc_chans=1; bs_ccch_sdcch_comb=false; break;
	case 1: bs_cc_chans=1; bs_ccch_sdcch_comb=true; break;
	case 2: bs_cc_chans=2; bs_ccch_sdcch_comb=false; break;
	case 4: bs_cc_chans=3; bs_ccch_sdcch_comb=false; break;
	case 6: bs_cc_chans=4; bs_ccch_sdcch_comb=false; break;
	default:
		LOG(ERR) << "Invalid GSM.CCCH.CCCH-CONF value:"<<mCC.mCCCH_CONF <<" GPRS will fail until fixed";
		return NULL;	// There will be no reliable GPRS service until you fix this.
	}

	// BS_PA_MFRMS is the number of 51-multiframes used for paging.
	unsigned bs_pa_mfrms = mCC.getBS_PA_MFRMS();

	// Here are some example numbers:
	// We currently use CCCH_CONF=1 so cc_chans=1, so agch_avail=3.
	// Since BS_CC_CHANS=1, then CCCH_GROUP is always 0.
	// For BS_PA_MFRMS=2, BS_AG_BLKS_RES=2:
	//	N=2; tmp = imsi % 2; CCCH_GROUP = 0; PAGING_GROUP = imsi % 2;
	// For BS_PA_MFRMS=2, BS_AG_BLKS_RES=1:
	//	N=4; tmp = imsi % 4; PAGING_GROUP = imsi % 4;
	// For BS_PA_MFRMS=2, BS_AG_BLKS_RES=0:
	//	N=6; tmp = imsi % 6; PAGING_GROUP = imsi % 6;
	// For BS_PA_MFRMS=3, BS_AG_BLKS_RES=0:
	//	N=9; tmp = imsi % 9; PAGING_GROUP = imsi % 9;
	// Paging block index = PAGING_GROUP % BS_PA_MFRMS
	// Multiframe index = PAGING_GROUP / pch_avail;
	// correct multiframe when: multiframe_index == (FN div 51) % BA_PA_MFRMS

	// From GSM 05.02 Clause 7 table 5 (located after sec 6.5)
	unsigned agch_avail = bs_ccch_sdcch_comb ? 3 : 8;
	// If you hit this assertion, go fix L3ControlChannelDescription
	// to make sure you leave some paging channels available.
	assert(agch_avail > mPCC.mBS_AG_BLKS_RES);

	// GSM 05.02 6.5.2: N is number of paging blocks "available" on one CCCH.
	// The "available" is in quotes and not specifically defined, but I believe
	// they mean after subtracting out BS_AG_BLKS_RES, as per 6.5.1 paragraph v).
	unsigned pch_avail = agch_avail - mPCC.mBS_AG_BLKS_RES;
	unsigned Ntotal = pch_avail * bs_pa_mfrms;
	unsigned tmp = (imsiMod1000 % (bs_cc_chans * Ntotal)) % Ntotal;
	unsigned paging_group = tmp % Ntotal;
	paging_block_index = paging_group / (Ntotal / bs_pa_mfrms);
	// And I quote: The required 51-multiframe occurs when:
	// PAGING_GROUP div (N div BS_PA_MFRMS) = (FN div 51) mod (BS_PA_MFRMS)
	multiframe_index = paging_group / (Ntotal % bs_pa_mfrms); 
}

void GSMConfig::sendPCH(const L3RRMessage& msg,unsigned imsiMod1000)
{
	unsigned paging_block_index;	// which of the paging ccchs to use.
	unsigned multiframe_index;	// which 51-multiframe to use.
	crackPagingFromImsi(imsiMod1000,paging_block_index, multiframe_index);
	assert(multiframe_index < sMax_BS_PA_MFRMS);
	CCCHLogicalChannel* ch = getPCH(paging_block_index);
	ch->mPagingQ[multiframe_index].write(new L3Frame((const L3Message&)msg,UNIT_DATA));
}

Time GSMConfig::getPchSendTime(imsiMod1000)
{
	unsigned paging_block_index;	// which of the paging ccchs to use.
	unsigned multiframe_index;	// which 51-multiframe to use.
	crackPagingFromImsi(imsiMod1000,paging_block_index, multiframe_index);
	assert(multiframe_index < sMax_BS_PA_MFRMS);
	CCCHLogicalChannel* ch = getPCH(paging_block_index);
	return ch->getNextPchSendTime(multiframe_index);
}
#endif


// vim: ts=4 sw=4
