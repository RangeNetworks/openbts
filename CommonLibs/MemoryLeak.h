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
#ifndef _MEMORYLEAK_
#define _MEMORYLEAK_ 1
#include <map>
#include "ScalarTypes.h"
#include "Logger.h"

namespace Utils {

struct MemStats {
	// Enumerates the classes that are checked.
	// Redundancies are ok, for example, we check BitVector and also
	// several descendants of BitVector.
	enum MemoryNames {
		mZeroIsUnused,
		mVector,
		mVectorData,
		mBitVector,
		mByteVector,
		mByteVectorData,
		mRLCRawBlock,
		mRLCUplinkDataBlock,
		mRLCMessage,
		mRLCMsgPacketDownlinkDummyControlBlock,	// Redundant with RLCMessage
		mTBF,
		mLlcEngine,
		mSgsnDownlinkMsg,
		mRachInfo,
		mPdpPdu,
		mFECDispatchInfo,
		mL3Frame,
		msignalVector,
		mSoftVector,
		mScramblingCode,
		mURlcDownSdu,
		mURlcPdu,
		// Must be last:
		mMax,
	};
	int mMemTotal[mMax];	// In elements, not bytes.
	int mMemNow[mMax];
	const char *mMemName[mMax];
	MemStats();
	void memChkNew(MemoryNames memIndex, const char *id);
	void memChkDel(MemoryNames memIndex, const char *id);
	void text(std::ostream &os);
	// We would prefer to use an unordered_map, but that requires special compile switches.
	// What a super great language.
	typedef std::map<std::string,Int_z> MemMapType;
	MemMapType mMemMap;
};
extern struct MemStats gMemStats;
extern int gMemLeakDebug;

// This is a memory leak detector.
// Use by putting RN_MEMCHKNEW and RN_MEMCHKDEL in class constructors/destructors,
// or use the DEFINE_MEMORY_LEAK_DETECTOR class and add the defined class
// as an ancestor to the class to be memory leak checked.

struct MemLabel {
	std::string mccKey;
	virtual ~MemLabel() {
		Int_z &tmp = Utils::gMemStats.mMemMap[mccKey]; tmp = tmp - 1;
	}
};

#if RN_DISABLE_MEMORY_LEAK_TEST
#define RN_MEMCHKNEW(type)
#define RN_MEMCHKDEL(type)
#define RN_MEMLOG(type,ptr)
#define DEFINE_MEMORY_LEAK_DETECTOR_CLASS(subClass,checkerClass) \
	struct checkerClass {};
#else

#define RN_MEMCHKNEW(type) { Utils::gMemStats.memChkNew(Utils::MemStats::m##type,#type); }
#define RN_MEMCHKDEL(type) { Utils::gMemStats.memChkDel(Utils::MemStats::m##type,#type); }

#define RN_MEMLOG(type,ptr) { \
	static std::string key = format("%s_%s:%d",#type,__FILE__,__LINE__); \
	(ptr)->/* MemCheck##type:: */ mccKey = key; \
	Utils::gMemStats.mMemMap[key]++; \
	}

// TODO: The above assumes that checkclass is MemCheck ## subClass
#define DEFINE_MEMORY_LEAK_DETECTOR_CLASS(subClass,checkerClass) \
	struct checkerClass : public virtual Utils::MemLabel { \
	    checkerClass() { RN_MEMCHKNEW(subClass); } \
		virtual ~checkerClass() { \
			RN_MEMCHKDEL(subClass); \
		} \
	};

#endif

}	// namespace Utils

#endif
