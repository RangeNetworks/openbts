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
#ifndef _GSMNEIGHBORS_H_
#define _GSMNEIGHBORS_H_ 1

#include <OpenBTSConfig.h>
#include "GSMCommon.h"
#include "GSMConfig.h"
#include <ControlTransfer.h>

namespace GSM {
class SACCHLogicalChannel;

// GSM FN values must use modulo logic.
// Using this type is just a comment that the value is a frame number, use is not enforced.
typedef int FrameNum;


template<typename PointListType, typename PointType,typename YValueType>
	float inline ComputeTrend(
		PointListType &data, 		// currently it is always: std::deque<PointType> &data,
		FrameNum x0,				// this doesnt need to be an argument; it is always the current frame number. 
		FrameNum (PointType::*xmethod)(),				// method pointer to retrieve X values from data.
		YValueType (PointType::*ymethod)(bool &valid),	// method pointer to retrieve Y values from data.
		int maxpoints,
		int algorithm=0				// Which algorithm to use.
	)
{
	// Init accumulator variables.
	float sumy = 0;
	int npoints = 0;
	// Process the points.  Run backwards so exponential decay weights the first point most heavily.
	// Running backwards we have to skip points until we get to maxpoints.
	int skip = data.size() - maxpoints;	// may already be negative, which means skip no points.
	for (typename PointListType::reverse_iterator it = data.rbegin(); it != data.rend(); it++) {
		if (skip-- > 0) continue;
		PointType &pt = *it;
		bool valid = true;	// useless init to quiet g++.
		YValueType y = (pt.*ymethod)(valid);
		if (!valid) { continue; }
		sumy += y;
		npoints++;
	}

	return (float) sumy / npoints;
}


// Neighbor Cell Data Point
struct NCellPoint {

	// The measurement report scales rxlev; this is post scaled, ie, actual db.
	int ncpRxlev;			// Value is negative in dB.
	FrameNum ncpFrame;		// When the report was received in GSM frame numbers.
	public:
	NCellPoint() {}
	//NCellPoint(int wRxlev, Time wTime) : ncpRxlev(wRxlev), ncpFrame(wTime.FN()) {}
	int getRxlev(bool &valid) { valid=true; return ncpRxlev; }
	FrameNum getFrameNum() { return ncpFrame; }
};
typedef std::deque<NCellPoint> NCellPointList;

class ChannelHistory;
class NeighborHistory {
	// The most recent point is on the front of the vector.
	NCellPointList nhList;
	Mutex nhLock;

	friend class ChannelHistory;
	// These are the identifying information for this neighbor, used for the key in the map.
	// It does not absolutely need to be in this struct, it could be passed around everywhere, but this is simpler.
	unsigned nhARFCN;
	unsigned nhBSIC;

	public:
	// No special destructor is needed.
	NeighborHistory(unsigned wARFCN,unsigned wBSIC) : nhARFCN(wARFCN), nhBSIC(wBSIC) {}

	// timestamp of most recent report;
	Int_z nhTimestamp;
	// Number of consecutive reports.
	Int_z nhConsecutiveCount;


	void nhAddPoint(NCellPoint &npt, FrameNum now);

	void nhClear();

	// Return true if there was data available and compute and return the averaged RXLEV, where 'averaged' is computed by one of several algorithms.
	bool nhGetAvgRxlev(FrameNum fn, float &avg, int algorithm=0);

	// Return the latest point, which is the front.
	bool nhGetLatest(NCellPoint &latest);	// not used.
	void nhText(string &result,bool full);
};

// Serving Cell Data Point
struct SCellPoint {
	// Max age: 32 reports, each SACCH frame requiring 2 * 52-multiframes each, approximately 16 seconds, plus slop.
	//const int cMaxAgeFrames = 32 * 2 * 52 + (2*52-1);

	// The measurement report scales rxlev; this is post scaled, ie, actual db.
	FrameNum scpFrame;		// When the report was received in GSM frame numbers.

	bool scpValid;			// Measurements from MS were valid in this report.
	int scpRxlev;			// serving cell RXLEV reported by MS.  Value is negative in dB.
	int scpRxqual;			// value is 0-7.  GSM5.08 8.2.24:  0 is good, 7 is BER > 12.8%
	public:
	FrameNum getFrameNum() { return scpFrame; }
	int getRxlev(bool &valid) { valid=scpValid; return scpRxlev; }

};
typedef std::deque<SCellPoint> SCellPointList;

// GSM 5.08 A3.1 specifies BSS processing of measurement reports and recommended. operator control parameters.
// We are required to save 32 samples.

// (pat) We average the measurement reports from the best neighbors for handover purposes, so we dont
// cause a handover from one spuriously low measurement report.
// Note that there could be neighbors varying slightly but all much better than the current cell,
// so we save all the neighbor data, not just the best one.
// We dont have to worry about this growing without bounds because there will only be a few neighbors.
// (pat) At my house, using the Blackberry, I see a regular 9.5 second heart-beat, where the measurements drop about 8db.
// The serving cell RSSI drops first, then in the next measurement report the serving RSSI is back to normal
// and the neighbor RSSI drops.  If it were just 2db more, it would be causing a spurious handover back and
// forth every 9.5 seconds.  This cache alleviates that problem.
class ChannelHistory {
	// The unsigned key is the frequency index combined with the BSIC.
	// The Peering code creates the frequency index; it is sent in one of several sysinfo messages, for us probably type2.
	typedef std::map<unsigned,NeighborHistory> NeighborMap;
	NeighborMap mNeighborData;

	// The most recent point is on the front of the vector.
	SCellPointList mSCellData;
	mutable Mutex mSCellLock;

	void chAddPoint(SCellPoint &spt,FrameNum now);

	//int cNumReports;	// Neighbor must appear in 2 of last cNumReports measurement reports.
	Int_z mReportTimestamp;	// Incremented each time a report arrives.
	public:
	unsigned makeKey(unsigned arfcn, unsigned BSIC) { return (arfcn<<6) + BSIC; }
	void crackKey(unsigned key, unsigned *arfcn, unsigned *BSIC) { *BSIC = key & 0x3f; *arfcn = key>>6; }

	NeighborHistory &getNeighborData(unsigned arfcn, unsigned BSIC);

	// Argument is current RSSI, and return is the averaged RSSI to use for handover determination purposes.
	void neighborAddMeasurement(FrameNum when, unsigned freq, unsigned BSIC, int RSSI);
	bool neighborAddMeasurements(SACCHLogicalChannel* SACCH,const L3MeasurementResults* measurements);
	void neighborClearMeasurements();	// Call to clear everything.
	Control::BestNeighbor neighborFindBest(Control::NeighborPenalty penalty);

	// Routines to return the accumulated data.
	int getAvgRxlev();	// Doesnt hurt to round the return to an int.
};

};
#endif
