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

#include "GPRSRLC.h"
#include "GSMCommon.h"

namespace GPRS {

int deltaBSN(int bsn1,int bsn2)
{
	static const int halfModulus = RLCBSN_t::BSNPeriodicity/2;
	int delta = bsn1 - bsn2;
	if (delta>=halfModulus) delta -= RLCBSN_t::BSNPeriodicity;
	else if (delta<-halfModulus) delta += RLCBSN_t::BSNPeriodicity;
	return delta;
}


// Based on GSM::FNDelta
// We assume the values are within a half periodicity of each other.
int RLCBSN_t::BSNdelta(RLCBSN_t v2)
{
	RLCBSN_t v1 = *this;
	//int delta = v1.mValue - v2.mValue;
	//if (delta>=halfModulus) delta -= BSNPeriodicity;
	//else if (delta<-halfModulus) delta += BSNPeriodicity;
	//return RLCBSN_t(delta);
	return RLCBSN_t(deltaBSN(v1.mValue,v2.mValue));
}

// Return 1 if v1 > v2; return -1 if v1 < v2, using modulo BSNPeriodicity.
int RLCBSN_t::BSNcompare(RLCBSN_t v2)
{
	int delta = BSNdelta(v2);
	if (delta>0) return 1;
	if (delta<0) return -1;
	return 0;
}

// (pat) Return the block radio number for a frame number.
RLCBSN_t FrameNumber2BSN(int fn)
{
	// The RLC blocks use a 52-multiframe, but each 13-multiframe is identical:
	// the first 12 frames are 3 RLC blocks, and the last frame is for timing or idle.
	int mfn = (fn / 13);			// how many 13-multiframes
	int rem = (fn - (mfn*13));	// how many blocks within the last multiframe.
	RLCBSN_t result = mfn * 3 + ((rem==12) ? 2 : (rem/4));
	result.normalize();
	return result;
}


// Return the Block Sequence Number for a frame number.
// There are 12 radio blocks per 52 frames,
int BSN2FrameNumber(RLCBSN_t absn)	// absolute block sequence number.
{
	// One extra frame is inserted after every 3 radio blocks,
	// so 3 radio blocks take 13 frames.
	int bsn = absn;	// Convert to int so we do math on int, not RLCBSN_t
	int result = ((int)bsn / 3) * 13 + ((int)bsn % 3) * 4;
	assert(result >= 0 && (unsigned) result <= GSM::gHyperframe);
	return result;
}

std::ostream& operator<<(std::ostream& os, const RLCDir::type &dir)
{
	os << RLCDir::name(dir);
	return os;
}
};
