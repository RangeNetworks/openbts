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

#ifndef GPRSRLC_H
#define GPRSRLC_H
#include <iostream>
#include <stdint.h>
//#include <stdio.h>
#include <Timeval.h>

namespace GPRS {
class RLCBSN_t;

// There are roughly 48 RLC blocks/second.
const unsigned RLCBlocksPerSecond = 48;
// TODO: Get this exact.
const double RLCBlockTime = 1.0 / RLCBlocksPerSecond;		// In seconds
const unsigned RLCBlockTimeMsecs = 1000.0 / RLCBlocksPerSecond;		// In mseconds

extern RLCBSN_t FrameNumber2BSN(int fn);		// convert frame -> BSN
extern int BSN2FrameNumber(RLCBSN_t bsn);	// convert BSN -> frame

extern RLCBSN_t gBSNNext;		// The next Block Sequence Number that will be send on the downlink.

class RLCDir
{
	public:
	// Order matters: The first bit of a GlobalTFI is 0 for up, 1 for down, matching this.
	// Either is used as a 'dont-care' dir in function parameters.
	enum type { Up, Down, Either };
	static const char *name(int val)
	{
		switch ((type)val) {
			case Up: return "RLCDir::Up";
			case Down: return "RLCDir::Down";
			case Either: return "RLCDir::Either";
		}
		return "unknown";	// Makes gcc happy.
	}
};
#define RLCDirType RLCDir::type
std::ostream& operator<<(std::ostream& os, const RLCDir::type &mode);


extern unsigned RLCBlockSize[4];
extern const unsigned RLCBlockSizeBytesMax;
extern int deltaBSN(int bsn1,int bsn2);

class RLCBSN_t { // Type of radio block sequence numbers.  -1 means invalid.
	int32_t mValue;
	public:
	// Number of Radio Blocks in a hyperframe: there are 12 blocks every 52 frames.
	// Hyperframe = 2048UL * 26UL * 51UL;
	static const unsigned BSNPeriodicity = 2048UL * 26UL * 51UL * 12UL / 52UL;

	// Note: C++ default operator=() is ok.
	RLCBSN_t() { mValue = -1; }
	RLCBSN_t(int wValue) : mValue(wValue) {}
	operator int() const { return mValue; }

	void normalize() {
		mValue = mValue % BSNPeriodicity;
		if (mValue<0) { mValue += BSNPeriodicity; }
	}

	// Return v1 - v2, accounting for wraparound, assuming the values are
	// less than half a hyperframe apart.
	int BSNdelta(RLCBSN_t v2);
	int BSNcompare(RLCBSN_t v2);
	bool operator<(RLCBSN_t v2) { return BSNcompare(v2) < 0; }
	bool operator<=(RLCBSN_t v2) { return BSNcompare(v2) <= 0; }
	bool operator>(RLCBSN_t v2) { return BSNcompare(v2) > 0; }
	bool operator>=(RLCBSN_t v2) { return BSNcompare(v2) >= 0; }
	RLCBSN_t operator+(RLCBSN_t v2) {
		RLCBSN_t result(this->mValue + v2.mValue); result.normalize(); return result;
	}
	RLCBSN_t operator-(RLCBSN_t v2) {
		RLCBSN_t result(this->mValue - v2.mValue); result.normalize(); return result;
	}
	RLCBSN_t operator+(int32_t v2) {
		RLCBSN_t result(this->mValue + v2); result.normalize(); return result;
	}
	RLCBSN_t operator-(int32_t v2) {
		RLCBSN_t result(this->mValue - v2); result.normalize(); return result;
	}
	RLCBSN_t& operator++() { mValue++; normalize(); return *this; }	// prefix
	void operator++(int) { mValue++; normalize(); }	// postfix
	bool valid() const { return mValue >= 0; }

	//static const int invalid = -1;	// An invalid value for an RLCBSN_t.

	// Return the bsn that is msecs in the future from a base bsn.
	// Used to conveniently specify timeouts in terms of something we track, namely, gBSNNext.
	RLCBSN_t addTime(int msecs)
	{
		// 20msecs is one BSN.
		int future = (msecs + RLCBlockTimeMsecs/2) / RLCBlockTimeMsecs;
		return *this + future;	// The operator+ normalizes it.
	}

	// Convert to GSM Frame Number.
	unsigned FN() { return (unsigned) BSN2FrameNumber(mValue); }
};

// A timer based on BSNs.
// Must not extend greater than BSNPeriodicity/ into the future.
class GprsTimer {
	Timeval mWhen;
	bool mValid;
	public:
#if 1	// TODO: Switch this to use the BSN based timers below, but needs testing.
	GprsTimer() : mValid(false) {}
	bool valid() const { return mValid; }
	void setInvalid() { mValid = false; }
	// Two functions for a countdown timer:
	void setFuture(int msecs) { mValid = true; mWhen.future(msecs); }
	bool expired() const { return mValid && mWhen.passed(); }
	// Two functions for a countup timer:
	void setNow() { mValid = true; mWhen.now(); }
	int elapsed() const { return mValid ? mWhen.elapsed() : 0; }
#else
	RLCBSN_t mWhen;	// Inits to not valid.
	public:
	bool valid() { return mWhen.valid(); }
	void setInvalid() { mValid = false; }
	// setFuture and expired function as a countdown timer.
	void setFuture(int msecs) {
		mWhen = gBSNNext.addTime(msecs);
		GPRSLOG(1) << format("*** setFuture %d bsnnext=%d when=%d\n",msecs,(int)gBSNNext,(int)mWhen);
	}
	bool expired() {
		GPRSLOG(1) << format("*** expired valid=%d bsnnext=%d when=%d togo=%d\n",
			valid(),(int)gBSNNext,(int)mWhen,(mWhen-gBSNNext)*RLCBlockTimeMsecs );
		return valid() && gBSNNext > mWhen;
	}
	// setNow and elapsed function as a countup timer.
	void setNow() { mWhen = gBSNNext; }
	// Elapsed time from a now() in msecs.
	int elapsed() {
		return valid() ? (int)(gBSNNext - mWhen) * RLCBlockTimeMsecs : 0;
	}
#endif
	long remaining() const { return -elapsed(); }
};

};

#endif
