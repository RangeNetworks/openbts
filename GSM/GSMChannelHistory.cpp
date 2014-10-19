/*
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/
// Written 3-2014 by Pat Thompson

#define LOG_GROUP LogGroup::GSM		// Can set Log.Level.GSM for debugging

#include <math.h>

#include "GSMLogicalChannel.h"
#include "NeighborTable.h"
#include "GSMChannelHistory.h"
#include "GSML1FEC.h"

namespace GSM {

// Return the averaged RXLEV from the serving BTS as reported by the handset.
int ChannelHistory::getAvgRxlev()
{
	ScopedLock lock(mSCellLock);
	FrameNum now = gBTS.time().FN();
	return round(ComputeTrend(mSCellData,now,&SCellPoint::getFrameNum,&SCellPoint::getRxlev,gConfig.GSM.Handover.RXLEV_DL.History));
}


bool NeighborHistory::nhGetAvgRxlev(FrameNum fn, float &avg, int algorithm)
{
	ScopedLock lock(nhLock);
	if (nhList.size() == 0) { return false; }
	avg = ComputeTrend(nhList,fn,&NCellPoint::getFrameNum,&NCellPoint::getRxlev,gConfig.GSM.Handover.RXLEV_DL.History,algorithm);
	return true;
}

NeighborHistory& ChannelHistory::getNeighborData(unsigned arfcn, unsigned BSIC)
{
	unsigned key = makeKey(arfcn,BSIC);
	NeighborMap::iterator it;
	if ((it = mNeighborData.find(key)) == mNeighborData.end()) {
		// Create a new entry.  You would think this could be easier, but not if you want to avoid the extra unnecessary construction.
		// C++11 adds emplace but we cant use that yet.
		//std::pair<unsigned,NeighborHistory> mapentry(key,NeighborHistory(arfcn,BSIC));
		//std::pair<NeighborMap::iterator,bool> blah = mNeighborData.insert(mapentry);
		//return blah.first->second;
		//std::pair<NeighborMap::iterator,bool> blah = mNeighborData.insert( NeighborMap::value_type(key,newone) );
		NeighborHistory newone((unsigned)arfcn,BSIC);
		it = mNeighborData.insert( NeighborMap::value_type(key,newone) ).first;
	}
	return it->second;
}

// Clear all neighbor data.
void ChannelHistory::neighborClearMeasurements()
{
	LOG(DEBUG);
	mNeighborData.clear();
}

void NeighborHistory::nhAddPoint(NCellPoint &npt, FrameNum now)
{
	ScopedLock lock(nhLock);
	nhList.push_front(npt);		// This makes a copy of npt.
	int maxlen = gConfig.GSM.Handover.History.Max;
	while ((int)nhList.size() > maxlen) { nhList.pop_back(); }
	// Throw away points that are too old.
	int maxage = (1+maxlen) * 2*52;		// Each report requires 2 * 52-multiframes, 480ms.
	while (nhList.size()) {
		NCellPoint &bk = nhList.back();
		if (FNDelta(now,bk.ncpFrame) > maxage) { nhList.pop_back(); continue; }
		break;
	}
}

void ChannelHistory::chAddPoint(SCellPoint &spt, FrameNum now)
{
	LOG(DEBUG) << LOGVAR(spt.scpFrame) << LOGVAR(now);
	ScopedLock lock(mSCellLock);
	mSCellData.push_front(spt);
	int maxlen = gConfig.GSM.Handover.History.Max;
	while ((int)mSCellData.size() > maxlen) { mSCellData.pop_back(); }
	// Throw away points that are too old.
	int maxage = (1+maxlen) * 2*52;		// Each report requires 2 * 52-multiframes, 480ms.
	while (mSCellData.size()) {
		SCellPoint &bk = mSCellData.back();
		if (FNDelta(now,bk.scpFrame) > maxage) { mSCellData.pop_back(); continue; }
		break;
	}
}

// Find the neighbor with the highest RXLEV.
// If none, the mValid in the result will be false.
Control::BestNeighbor ChannelHistory::neighborFindBest(Control::NeighborPenalty penalty)
{
	Control::BestNeighbor best;
	FrameNum sampleFN = gBTS.time().FN();
	LOG(DEBUG) <<LOGVAR(penalty);
	for (NeighborMap::iterator it = mNeighborData.begin(); it != mNeighborData.end(); it++) {
		NeighborHistory &nh = it->second;
		LOG(DEBUG) <<LOGVAR(nh.nhBSIC) <<LOGVAR(nh.nhARFCN) <<LOGVAR(nh.nhConsecutiveCount) <<LOGVAR(nh.nhTimestamp);
		if (nh.nhConsecutiveCount < gConfig.GSM.Handover.RXLEV_DL.History) {
			LOG(DEBUG) << "skipping," <<LOGVAR(nh.nhConsecutiveCount);
			continue;
		}
		float thisRxlev;
		if (! nh.nhGetAvgRxlev(sampleFN,thisRxlev)) {	// If no points, ignore it.
			LOG(DEBUG) <<"skipping, no RXLEV data?";
			continue;
		}
		if (penalty.match(best.mARFCN,best.mBSIC) && ! penalty.mPenaltyTime.passed()) {
			LOG(DEBUG) <<"skipping, "<<LOGVAR(penalty);
			continue;
		}
		if (! best.mValid || thisRxlev > best.mRxlev) {
			// Check for congestion in the neighbor.
			if (gNeighborTable.neighborCongestion(nh.nhARFCN,nh.nhBSIC)) {
				LOG(DEBUG) << "skipping, neighborCongestion";
				continue;
			}
			best.mValid = true;
			best.mRxlev = thisRxlev;
			unsigned bestkey = it->first;
			crackKey(bestkey,&best.mARFCN,&best.mBSIC);
			LOG(DEBUG) <<"found:"<<LOGVAR(bestkey) <<LOGVAR(best.mARFCN) <<LOGVAR(best.mBSIC) <<LOGVAR(thisRxlev) <<LOGVAR(best.mRxlev);
		}
	}
	return best;
}


// The MS reports the 6 best cells, but that could vary from report to report.
// So we dont delete a neighbor just because it does not appear in a single report.
void ChannelHistory::neighborAddMeasurement(FrameNum when, unsigned arfcn, unsigned BSIC, int rxlevdb)
{
	NeighborHistory &nh = this->getNeighborData(arfcn,BSIC);	// creates an entry if necessary.

	{	NCellPoint npt;
		npt.ncpFrame = when;
		npt.ncpRxlev = rxlevdb;
		nh.nhAddPoint(npt,when);
		LOG(DEBUG) <<LOGVAR(when) <<LOGVAR(arfcn) <<LOGVAR(BSIC) <<LOGVAR(rxlevdb) <<LOGVAR(npt.ncpRxlev);
	}

	bool isConsecutive = nh.nhTimestamp == this->mReportTimestamp-1;
	LOG(DEBUG) <<LOGVAR(arfcn) <<LOGVAR(BSIC) <<LOGVAR(nh.nhTimestamp) <<LOGVAR(this->mReportTimestamp) <<LOGVAR(isConsecutive);
	if (isConsecutive) {
		nh.nhConsecutiveCount++;
	} else {
		nh.nhConsecutiveCount = 0;
	}
	nh.nhTimestamp = this->mReportTimestamp;

}

bool ChannelHistory::neighborAddMeasurements(SACCHLogicalChannel* SACCH,const L3MeasurementResults* measurements)
{

	Time sampleTime = gBTS.time();	// This thread could be running behind the clock, but it is close enough for government work.

	this->mReportTimestamp++;

	{	SCellPoint spt;
		spt.scpFrame = sampleTime.FN();
		// These are reported by the handset.
		spt.scpValid = measurements->isServingCellValid();
		if (spt.scpValid) {
			spt.scpRxlev = measurements->RXLEV_FULL_SERVING_CELL_dBm();
			spt.scpRxqual  = measurements->RXQUAL_FULL_SERVING_CELL();
		} else {
			spt.scpRxlev = -1000;	// Impossible value.
			spt.scpRxqual  = 8;		// Impossible value.
		}
	}

	// Save the RXLEV for the neighbors.
	if (7 == measurements->NO_NCELL()) {
		// This special value means neighbor cell information is not avaliable.  Gotta love that.
		return false;
	}
	for (unsigned int i=0; i<measurements->NO_NCELL(); i++) {
		int thisRxLevel = measurements->RXLEV_NCELL_dBm(i);
		int thisFreq = measurements->BCCH_FREQ_NCELL(i);
		if (thisFreq == 31) {
			// (pat) This is reserved for 3G in some weird way.
			// We support only 31 instead of 32 neighbors to avoid any confusion here.
			continue;
		}
		int thisBSCI = measurements->BSIC_NCELL(i);
		int arfcn = gNeighborTable.getARFCN(thisFreq);
		if (arfcn < 0) {
			LOG(INFO) << "Measurement report with invalid freq index:" << thisFreq << " arfcn:" << arfcn;  // SVGDBG seeing this error  (pat) Maybe fixed 10-17-2014 by ticket #1915
			continue;
		}
		this->neighborAddMeasurement(sampleTime.FN(),(unsigned)arfcn,thisBSCI,thisRxLevel);
	}
	return measurements->isServingCellValid() && measurements->NO_NCELL() > 0;
}


};
