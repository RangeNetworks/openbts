/*
* Copyright 2008, 2009, 2010, 2014 Free Software Foundation, Inc.
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/

#define LOG_GROUP LogGroup::GSM		// Can set Log.Level.GSM for debugging


#include "GSMConfig.h"
#include "GSMTransfer.h"
#include "GSMLogicalChannel.h"
#include "GSMCCCH.h"
#include "GPRSExport.h"
#include <ControlCommon.h>
#include <Logger.h>
#include <NeighborTable.h>
#include <Reporting.h>
#include <Globals.h>



using namespace std;
using namespace GSM;
using Control::L3LogicalChannel;

extern void PagerStart();

int GSM::gNumC7s, GSM::gNumC1s;	// Number of C7 and C1 timelots created.
static bool testStart = false;	// Set this to try testing without the channels started.

bool GSM::isCBSEnabled() {
	return gConfig.getStr("Control.SMSCB.Table").length() != 0;
}


GSMConfig::GSMConfig()
	:mCBCH(NULL),
	mSI5Frame(SAPI0Sacch,L3_UNIT_DATA),mSI6Frame(SAPI0Sacch,L3_UNIT_DATA),
	mSI1(NULL),mSI2(NULL),mSI3(NULL),mSI4(NULL),
	mSI5(NULL),mSI6(NULL), mSI13(NULL),
	mStartTime(::time(NULL)),
	mChangemark(0)
{
}

void GSMConfig::gsmInit() 
{
	// (pat 3-2014) Because the changemark was always inited to 0, if you kill OpenBTS, change something that affects the beacon,
	// and restart, the handsets do not know the beacon has changed.  We need the changemark to be persistent across restarts.
	// As a sneaky dirty way to do this, I am putting it in the config database.
	const char *GsmChangeMark = "GSM.ChangeMark";
	if (gConfig.defines(GsmChangeMark)) {
		mChangemark = gConfig.getNum(GsmChangeMark) % 8;
	}

	long changed = 0;
	long band = gConfig.getNum("GSM.Radio.Band");
	long c0 = gConfig.getNum("GSM.Radio.C0");

	// We do not init this statically because it must be done after config options are inited.
	gControlChannelDescription = new L3ControlChannelDescription();
	gControlChannelDescription->validate();

	// adjust to an appropriate band if C0 is bogus
	if (c0 >= 128 && c0 <= 251 && band != 850) {
		changed = band;
		band = 850;
	} else if (c0 <= 124 && band != 900) {
		changed = band;
		band = 900;
	} else if (c0 >= 975 && c0 <= 1023 && band != 900) {
		changed = band;
		band = 900;
	} else if (c0 >= 512 && c0 <= 810 && band != 1800 && band != 1900) {
		changed = band;
		band = 1800;
	} else if (c0 >= 811 && c0 <= 885 && band != 1800) {
		changed = band;
		band = 1800;
	}

	if (changed) {
		if (gConfig.set("GSM.Radio.Band", band)) {
			LOG(NOTICE) << "crisis averted: automatically adjusted GSM.Radio.Band from " << changed << " to " << band << " so GSM.Radio.C0 is in range";
		} else {
			LOG(ERR) << "unable to automatically adjust GSM.Radio.Band, config write error";
		}
	}

	mBand = (GSMBand)gConfig.getNum("GSM.Radio.Band");
 	regenerateBeacon();
}

void GSMConfig::gsmStart()
{
	// If requested, start gprs to allocate channels at startup.
	// Otherwise, channels are allocated on demand, if possible.
	if (GPRS::GPRSConfig::IsEnabled()) {
		// Start gprs.
		GPRS::gprsStart();
	}
	gPowerManager.pmStart();
	// Do not call this until the paging channels are installed.
	PagerStart();

	Control::l3start();	// (pat) For the L3 rewrite: start the L3 state machine dispatcher.
}




void GSMConfig::regenerateBeacon()
{
	// (pat) regenerateBeacon is called whenever there is a change to the config database.
	// When we call gConfig.set() it would cause infinite recursion; this prevents that, albeit goofily.
	// This is not safe in case someone later installs throws that jump over this.
	static int noRecursionPlease = 0;
	if (noRecursionPlease) return;
	noRecursionPlease++;

	gReports.incr("OpenBTS.GSM.RR.BeaconRegenerated");
	mChangemark++;
	gConfig.set("GSM.ChangeMark",mChangemark);

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
	L3Frame SI1L3(L3_UNIT_DATA,0);
	SI1->write(SI1L3);
	L2Header SI1Header(L2Length(SI1L3.L2Length()));
	mSI1Frame = L2Frame(SI1Header,SI1L3);
	LOG(DEBUG) << "mSI1Frame " << mSI1Frame;

	// SI2
	L3SystemInformationType2 *SI2 = new L3SystemInformationType2(neighbors);
	if (mSI2) delete mSI2;
	mSI2 = SI2;
	LOG(INFO) << *SI2;
	L3Frame SI2L3(L3_UNIT_DATA,0);
	SI2->write(SI2L3);
	L2Header SI2Header(L2Length(SI2L3.L2Length()));
	mSI2Frame = L2Frame(SI2Header,SI2L3);
	LOG(DEBUG) << "mSI2Frame " << mSI2Frame;

	// SI3
	L3SystemInformationType3 *SI3 = new L3SystemInformationType3;
	if (mSI3) delete mSI3;
	mSI3 = SI3;
	LOG(INFO) << *SI3;
	L3Frame SI3L3(L3_UNIT_DATA,0);
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
	L3Frame SI4L3(L3_UNIT_DATA,0);
	SI4->write(SI4L3);
	//printf("SI4 bodylength=%d l2len=%d\n",SI4.l2BodyLength(),SI4L3.L2Length());
	//printf("SI4L3.size=%d\n",SI4L3.size());
	L2Header SI4Header(L2Length(SI4L3.L2Length()));
	mSI4Frame = L2Frame(SI4Header,SI4L3,true);
	LOG(DEBUG) << "mSI4Frame " << mSI4Frame;

#if GPRS_PAT | GPRS_TEST
	// SI13. pat added 8-2011 to advertise GPRS support.
	if (mSI13) { delete mSI13; mSI13 = NULL; }
	if (GPRS::GPRSConfig::IsEnabled()) {
		mSI13 = new L3SystemInformationType13;
		LOG(INFO) << *mSI13;
		L3Frame SI13L3(L3_UNIT_DATA,0);
		//printf("start=%d\n",SI13L3.size());
		mSI13->write(SI13L3);
		//printf("end=%d\n",SI13L3.size());
		//printf("SI13 bodylength=%d l2len=%d\n",SI13.l2BodyLength(),SI13L3.L2Length());
		//printf("SI13L3.size=%d\n",SI13L3.size());
		L2Header SI13Header(L2Length(SI13L3.L2Length()));
		mSI13Frame = L2Frame(SI13Header,SI13L3,true);
		LOG(DEBUG) << "mSI13Frame " << mSI13Frame;
	}
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

	noRecursionPlease--;
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

size_t GSMConfig::AGCHLoad() { return getAGCHLoad(); }
size_t GSMConfig::PCHLoad() { return getPCHLoad(); }


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

// (pat) Return the goodness of this possible match of gprs channels.
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
	int firstIx = sz-bestI-1; // only works if backwards==true
	for (int j = 0; j < bestN; j++) {
		results[j] = chanList[firstIx+j];
	}
	return bestN;
}

// Allocate a group of channels for gprs.
// See comments at getChanGroup.
int GSMConfig::getTCHGroup(int groupSize,TCHFACCHLogicalChannel **results)
{
	ScopedLock lock(mLock);
	int nfound = getChanGroup<TCHFACCHLogicalChannel>(mTCHPool,results);
	// limit to channels actually requested
	if (groupSize < nfound) {
		nfound = groupSize;
	}
	for (int i = 0; i < nfound; i++) {
		results[i]->lcGetL1()->setGPRS(true,NULL);
	}
	return nfound;
}





SDCCHLogicalChannel *GSMConfig::getSDCCH()
{
	ScopedLock lock(mLock);
	SDCCHLogicalChannel *chan = getChan<SDCCHLogicalChannel>(mSDCCHPool,0);
	if (chan) chan->lcinit();
	LOG(DEBUG) <<chan;
	return chan;
}


// 6-2014 pat: The channel is now returned with T3101 running but un-started, which means it is not yet transmitting.
// The caller is responsible for setting the Timing Advance and then starting it.
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
			chan->lcGetL1()->setGPRS(true,NULL);
			return chan;
		}
		gReports.incr("OpenBTS.GSM.RR.ChannelAssignment");
		chan->lcinit();
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
	thread->start((void*(*)(void*))Control::DCCHDispatcher,dynamic_cast<L3LogicalChannel*>(chan));
	chan->lcinit();
	if (CN == 0 && !testStart) chan->lcstart();	// Everything on C0 must broadcast continually.
	gBTS.addTCH(chan);
}

class Beacon {};


static Thread CBCHControlThread;
static bool isCBSRunning = false;
void GSMConfig::createCBCH(ARFCNManager *radio, TypeAndOffset type, int CN, int TN)
{
	LOG(INFO) << "creating CBCH for SMSCB";
	// CBCH is always SDCCH 2 (third one) but may be on any timeslot, depending on beacon type.
	CBCHLogicalChannel *CBCH = new CBCHLogicalChannel(CN,TN,type == SDCCH_4_2 ? gSDCCH4[2] : gSDCCH8[2]);
	CBCH->downstream(radio);
	CBCH->cbchOpen();
	gBTS.addCBCH(CBCH);
	if (!isCBSRunning) {	// As of 8-2014, this is only called once so this test is irrelevant.
		isCBSRunning = true;
		CBCHControlThread.start((void*(*)(void*))Control::SMSCBSender,NULL);
	}
	mCBCHDescription = L3ChannelDescription(type,TN,gConfig.getNum("GSM.Identity.BSIC.BCC"),gConfig.getNum("GSM.Radio.C0")+CN);
	regenerateBeacon();	// To update SI4 with the new CBCH timeslot.
}

// Combination-5 beacon has 3 x CCCH + 4 x SDCCH.
// There can be only one Combination 5 beacon, always on timeslot 0.
class BeaconC5 : public Beacon {
	SCHL1FEC SCH;
	FCCHL1FEC FCCH;
	BCCHL1FEC BCCH;
	RACHL1FEC *RACH;
	CCCHLogicalChannel *CCCH;
	SDCCHLogicalChannel *C0T0SDCCH[4];
	Thread C0T0SDCCHControlThread[4];

	public:
	BeaconC5(ARFCNManager *radio)
	{
		LOG(DEBUG);
		unsigned TN = 0;
		radio->setSlot(TN,5);
		SCH.downstream(radio);
		SCH.l1open();
		FCCH.downstream(radio);
		FCCH.l1open();
		BCCH.downstream(radio);
		BCCH.l1open();
		RACH = new RACHL1FEC(gRACHC5Mapping,0);
		RACH->downstream(radio);
		RACH->l1open();
		CCCH = new CCCHLogicalChannel(0, gCCCH_C5Mapping);	// Always ccch_grooup == 0
		CCCH->downstream(radio);
		CCCH->ccchOpen();
		// C-V C0T0 SDCCHs
		C0T0SDCCH[0] = new SDCCHLogicalChannel(0,0,gSDCCH_4_0);
		C0T0SDCCH[1] = new SDCCHLogicalChannel(0,0,gSDCCH_4_1);
		C0T0SDCCH[2] = new SDCCHLogicalChannel(0,0,gSDCCH_4_2);
		C0T0SDCCH[3] = new SDCCHLogicalChannel(0,0,gSDCCH_4_3);

		// GSM 5.02 6.4.1: Subchannel 2 used for CBCH if CBS enabled.
		// (pat) CBCH location is advertised in L3SystemInformationType4.
		bool SMSCB = isCBSEnabled();
		for (int i=0; i<4; i++) {
			if (SMSCB && (i==2)) continue;
			C0T0SDCCH[i]->downstream(radio);
			C0T0SDCCHControlThread[i].start((void*(*)(void*))Control::DCCHDispatcher,dynamic_cast<L3LogicalChannel*>(C0T0SDCCH[i]));
			C0T0SDCCH[i]->lcinit();
			C0T0SDCCH[i]->lcstart();	// Everything on channel 0 needs to broadcast constantly.
			gBTS.addSDCCH(C0T0SDCCH[i]);
		}
		// Install CBCH if used.
		if (SMSCB) {
			gBTS.createCBCH(radio,SDCCH_4_2,0,0);
		}
	}
};



void GSMConfig::createBeacon(ARFCNManager *radio)
{
	new BeaconC5(radio);
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
		thread->start((void*(*)(void*))Control::DCCHDispatcher,dynamic_cast<L3LogicalChannel*>(chan));
		chan->lcinit();
		if (CN == 0 && !testStart) chan->lcstart();	// Everything on C0 must broadcast continually.
		gBTS.addSDCCH(chan);
	}
}


void GSMConfig::setBtsHold(bool val)
{
	ScopedLock lock(mLock);
	mHold = val;
}

bool GSMConfig::btsHold() const
{
	ScopedLock lock(mLock);
	return mHold;
}


// (pat) Return a vector of the available channels.
// Use to avoid publishing these iterators to the universe.
void GSMConfig::getChanVector(L2ChanList &result)
{
	result.clear();
	result.reserve(64);		// Enough for a 2 ARFCN system.
	for (GSM::SDCCHList::const_iterator sChanItr = gBTS.SDCCHPool().begin(); sChanItr != gBTS.SDCCHPool().end(); ++sChanItr) {
		result.push_back(*sChanItr);
	}

	// TCHs
	for (GSM::TCHList::const_iterator tChanItr = gBTS.TCHPool().begin(); tChanItr != gBTS.TCHPool().end(); ++tChanItr) {
		result.push_back(*tChanItr);
	}
}


// vim: ts=4 sw=4
