/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


#include "GSMConfig.h"
#include "GSMTransfer.h"
#include "GSMLogicalChannel.h"
#include <ControlCommon.h>
#include <Logger.h>



using namespace std;
using namespace GSM;



GSMConfig::GSMConfig()
	:
	mSI5Frame(UNIT_DATA),mSI6Frame(UNIT_DATA),
	mStartTime(::time(NULL))
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
	// Do not call this until AGCHs are installed.
	mAccessGrantThread.start(Control::AccessGrantServiceLoop,NULL);
}




void GSMConfig::regenerateBeacon()
{
	// Update everything from the configuration.
	LOG(NOTICE) << "regenerating system information messages";

	// BSIC components
	mNCC = gConfig.getNum("GSM.Identity.BSIC.NCC");
	LOG_ASSERT(mNCC<8);
	mBCC = gConfig.getNum("GSM.Identity.BSIC.BCC");
	LOG_ASSERT(mBCC<8);

	// MCC/MNC/LAC
	mLAI = L3LocationAreaIdentity();

	// Now regenerate all of the system information messages.

	// SI1
	L3SystemInformationType1 SI1;
	LOG(INFO) << SI1;
	L3Frame SI1L3(UNIT_DATA);
	SI1.write(SI1L3);
	L2Header SI1Header(L2Length(SI1L3.L2Length()));
	mSI1Frame = L2Frame(SI1Header,SI1L3);
	LOG(DEBUG) << "mSI1Frame " << mSI1Frame;

	// SI2
	L3SystemInformationType2 SI2;
	LOG(INFO) << SI2;
	L3Frame SI2L3(UNIT_DATA);
	SI2.write(SI2L3);
	L2Header SI2Header(L2Length(SI2L3.L2Length()));
	mSI2Frame = L2Frame(SI2Header,SI2L3);
	LOG(DEBUG) << "mSI2Frame " << mSI2Frame;

	// SI3
	L3SystemInformationType3 SI3;
	LOG(INFO) << SI3;
	L3Frame SI3L3(UNIT_DATA);
	SI3.write(SI3L3);
	L2Header SI3Header(L2Length(SI3L3.L2Length()));
	mSI3Frame = L2Frame(SI3Header,SI3L3);
	LOG(DEBUG) << "mSI3Frame " << mSI3Frame;

	// SI4
	L3SystemInformationType4 SI4;
	LOG(INFO) << SI4;
	L3Frame SI4L3(UNIT_DATA);
	SI4.write(SI4L3);
	L2Header SI4Header(L2Length(SI4L3.L2Length()));
	mSI4Frame = L2Frame(SI4Header,SI4L3);
	LOG(DEBUG) << "mSI4Frame " << mSI4Frame;

	// SI5
	L3SystemInformationType5 SI5;
	LOG(INFO) << SI5;
	SI5.write(mSI5Frame);
	LOG(DEBUG) << "mSI5Frame " << mSI5Frame;

	// SI6
	L3SystemInformationType6 SI6;
	LOG(INFO) << SI6;
	SI6.write(mSI6Frame);
	LOG(DEBUG) "mSI6Frame " << mSI6Frame;

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






template <class ChanType> ChanType* getChan(vector<ChanType*>& chanList)
{
	const unsigned sz = chanList.size();
	if (sz==0) return NULL;
	// Start the search from a random point in the list.
	//unsigned pos = random() % sz;
	// HACK -- Try in-order allocation for debugging.
	for (unsigned i=0; i<sz; i++) {
		ChanType *chan = chanList[i];
		//ChanType *chan = chanList[pos];
		if (chan->recyclable()) return chan;
		//pos = (pos+1) % sz;
	}
	return NULL;
}





SDCCHLogicalChannel *GSMConfig::getSDCCH()
{
	ScopedLock lock(mLock);
	SDCCHLogicalChannel *chan = getChan<SDCCHLogicalChannel>(mSDCCHPool);
	if (chan) chan->open();
	return chan;
}


TCHFACCHLogicalChannel *GSMConfig::getTCH()
{
	ScopedLock lock(mLock);
	TCHFACCHLogicalChannel *chan = getChan<TCHFACCHLogicalChannel>(mTCHPool);
	if (chan) chan->open();
	return chan;
}



template <class ChanType> size_t chanAvailable(const vector<ChanType*>& chanList)
{
	size_t count = 0;
	for (unsigned i=0; i<chanList.size(); i++) {
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
	for (int i=0; i<chanList.size(); i++) {
		total += chanList[i]->load();
	}
	return total;
}



template <class ChanType> unsigned countActive(const vector<ChanType*>& chanList)
{
	unsigned active = 0;
	const unsigned sz = chanList.size();
	// Start the search from a random point in the list.
	for (unsigned i=0; i<sz; i++) {
		if (!chanList[i]->recyclable()) active++;
	}
	return active;
}


unsigned GSMConfig::SDCCHActive() const
{
	return countActive(mSDCCHPool);
}

unsigned GSMConfig::TCHActive() const
{
	return countActive(mTCHPool);
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
	if (mT3122>max) mT3122=max;
	return retVal;
}


unsigned GSMConfig::shrinkT3122()
{
	unsigned min = gConfig.getNum("GSM.Timer.T3122Min");
	ScopedLock lock(mLock);
	unsigned retVal = mT3122;
	mT3122 -= (random() % mT3122) / 2;
	if (mT3122<min) mT3122=min;
	return retVal;
}



void GSMConfig::createCombination0(TransceiverManager& TRX, unsigned TN)
{
	// This channel is a dummy burst generator.
	// This should not be applied to C0T0.
	LOG_ASSERT(TN!=0);
	LOG(NOTICE) << "Configuring dummy filling on C0T " << TN;
	ARFCNManager *radio = TRX.ARFCN(0);
	radio->setSlot(TN,0);
}


void GSMConfig::createCombinationI(TransceiverManager& TRX, unsigned CN, unsigned TN)
{
	LOG_ASSERT((CN!=0)||(TN!=0));
	LOG(NOTICE) << "Configuring combination I on C" << CN << "T" << TN;
	ARFCNManager *radio = TRX.ARFCN(CN);
	radio->setSlot(TN,1);
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
	radio->setSlot(TN,7);
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



// vim: ts=4 sw=4
