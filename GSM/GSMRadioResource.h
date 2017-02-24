/* 
* Copyright 2014, Range Networks, Inc.
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
#ifndef GSMRadioResource_H
#define GSMRadioResource_H 1

#include "Defines.h"
#include "GSMCommon.h"
#include "MemoryLeak.h"

namespace GSM {
class L2LogicalChannel;


struct RadData {
	bool mValid;
	float mRSSI;
	float mTimingError;
	RadData() { mValid = false; }
	RadData(float wRSSI, float wTimingError) : mValid(true),mRSSI(wRSSI),mTimingError(wTimingError) {}
};

struct RachInfo {
	unsigned mRA;
	const GSM::Time mWhen;
	RadData mRadData;
	unsigned mTN;

	// We now pre-allocate channels for RACHes for TCH/SDCCH services, then wait until the mReadyTime
	// has passed before servicing them by sending an ImmediateAssignment.
	L2LogicalChannel *mChan;
	GSM::Time mReadyTime;	// When the SACCH is known to have started transmiting correctly.

	int initialTA() const { return mRadData.mTimingError; }
	float RSSI() const { return mRadData.mRSSI; }

	// Gotta love this language.
	RachInfo(unsigned wRA, const GSM::Time &wWhen, RadData wRD, unsigned wTN=0)
		: mRA(wRA), mWhen(wWhen), mRadData(wRD), mTN(wTN), mChan(NULL)
		{ RN_MEMCHKNEW(RachInfo) }
	~RachInfo() { RN_MEMCHKDEL(RachInfo) }

#if 0
	// (pat) This is used when a RachInfo is placed in a priority_queue.
	// Return true if rach1 should appear before rach2 in the priority_queue,
	// meaning that rach1 will be serviced before rach2.
	bool operator>(const RachInfo *rach1, const RachInfo *rach2) {
		if (rach1->mChan) {
			assert(rach2->mChan);
			return rach1->mReadyTime < rach2->mreadyTime;
		}
		return rach1->mWhen < rach2-mWhen;
	}
#endif
};
std::ostream& operator<<(std::ostream& os, const RachInfo &rach);
std::ostream& operator<<(std::ostream& os, const RachInfo *rach);

#if unused
/** This record carries all of the parameters associated with a RACH burst. */
class ChannelRequestRecord {

	private:

	unsigned mRA;		///< request reference
	GSM::Time mFrame;	///< receive timestamp
	float mRSSI;		///< dB wrt full scale
	float mTimingError;	///< correlator timing error in symbol periods

	public:

	ChannelRequestRecord(
		unsigned wRA, const GSM::Time& wFrame,
		float wRSSI, float wTimingError)
		:mRA(wRA), mFrame(wFrame),
		mRSSI(wRSSI), mTimingError(wTimingError)
	{ }

	unsigned RA() const { return mRA; }
	const GSM::Time& frame() const { return mFrame; }
	float RSSI() const { return mRSSI; }
	float timingError() const { return mTimingError; }

};
#endif

// Enum used to create fake RACHes for testing purposes.
enum FakeRachType {
		FakeRachTCH,
		FakeRachSDCCH,
		FakeRachLUR,
		FakeRachGPRS,
		FakeRachAnswerToPaging
};
FakeRachType fakeRachTypeTranslate(const char *name);
extern void createFakeRach(FakeRachType rachtype);

extern void AccessGrantResponder(unsigned RA, const GSM::Time& when, float RSSI, float timingError, int TN);
extern ChannelType decodeChannelNeeded(unsigned RA);
extern bool requestingLUR(unsigned RA);

};
#endif
