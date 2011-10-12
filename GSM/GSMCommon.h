/**@file Common-use GSM declarations, most from the GSM 04.xx and 05.xx series. */
/*
* Copyright 2008-2011 Free Software Foundation, Inc.
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



#ifndef GSMCOMMON_H
#define GSMCOMMON_H

#include <stdlib.h>
#include <sys/time.h>
#include <ostream>
#include <vector>

#include <Threads.h>
#include <Timeval.h>
#include <BitVector.h>




namespace GSM {

/**@namespace GSM This namespace covers L1 FEC, L2 and L3 message translation. */


/* forward references */
class L1FEC;
class L2LAPDm;
class L3Processor;
class LogicalChannel;
class L2Header;

/** Call states based on GSM 04.08 5 and ITU-T Q.931 */
enum CallState {
	NullState,
	Paging,
	AnsweredPaging,
	MOCInitiated,
	MOCProceeding,
	MTCConfirmed,
	CallReceived,
	CallPresent,
	ConnectIndication,
	Active,
	DisconnectIndication,
	ReleaseRequest,
	SMSDelivering,
	SMSSubmitting,
};


/** Return a human-readable string for a GSM::CallState. */
const char* CallStateString(CallState state);

std::ostream& operator<<(std::ostream& os, CallState state);


/** A base class for GSM exceptions. */
class GSMError {};

/** Duration ofa GSM frame, in microseconds. */
const unsigned gFrameMicroseconds = 4615;


/** Sleep for a given number of GSM frame periods. */
inline void sleepFrames(unsigned frames)
	{ usleep(frames*gFrameMicroseconds); }

/** Sleep for 1 GSM frame period. */
inline void sleepFrame()
	{ usleep(gFrameMicroseconds); }



/** GSM Training sequences from GSM 05.02 5.2.3. */
extern const BitVector gTrainingSequence[];

/** C0T0 filler burst, GSM 05.02, 5.2.6 */
extern const BitVector gDummyBurst;

/** Random access burst synch. sequence */
extern const BitVector gRACHSynchSequence;

enum GSMAlphabet {
	ALPHABET_7BIT,
	ALPHABET_8BIT,
	ALPHABET_UCS2
};

/**@name Support for GSM 7-bit alphabet, GSM 03.38 6.2.1. */
//@{
/**
	Indexed by GSM 7-bit, returns ISO-8859-1.
	We do not support the extended table, so 0x1B is a space.
	FIXME -- ISO-8859-1 doesn't support Greek!
*/
static const unsigned char gGSMAlphabet[] = "@\243$\245\350\351\371\354\362\347\n\330\370\r\305\345D_FGLOPCSTZ \306\346\337\311 !\"#\244%&\'()*+,-./0123456789:;<=>?\241ABCDEFGHIJKLMNOPQRSTUVWXYZ\304\326\321\334\247\277abcdefghijklmnopqrstuvwxyz\344\366\361\374\341";
unsigned char encodeGSMChar(unsigned char ascii);
inline unsigned char decodeGSMChar(unsigned char sms) { return gGSMAlphabet[(unsigned)sms]; }
//@}


/**@name BCD-ASCII mapping, GMS 04.08 Table 10.5.118. */
//@{
/** Indexed by BCD, returns ASCII. */
static const char gBCDAlphabet[] = "0123456789.#abc";
char encodeBCDChar(char ascii);
inline char decodeBCDChar(char bcd) { return gBCDAlphabet[(unsigned)bcd]; }
//@}


/**@name Globally-fixed GSM timeout values (all in ms). */
//@{
/**@name GSM LAPDm timeouts, GSM 04.06 5.8, ITU-T Q.921 5.9 */
//@{
const unsigned T200ms = 900;		///< LAPDm ACK timeout, set for typical turnaround time
//@}
/**@name GSM timeouts for radio resource management, GSM 04.08 11.1. */
//@{
const unsigned T3101ms = 4000;		///< L1 timeout for SDCCH assignment
const unsigned T3107ms = 3000;		///< L1 timeout for TCH/FACCH assignment
const unsigned T3109ms = 30000;		///< L1 timeout for an existing channel
const unsigned T3111ms = 2*T200ms;	///< L1 timeout for reassignment of a channel
//@}
/**@name GSM timeouts for mobility management, GSM 04.08 11.2. */
//@{
const unsigned T3260ms = 12000;		///< ID request timeout
//@}
/**@name GSM timeouts for SMS. GSM 04.11 */
//@{
const unsigned TR1Mms = 30000;		///< RP-ACK timeout
//@}
//@}




/** GSM 04.08 Table 10.5.118 and GSM 03.40 9.1.2.5 */
enum TypeOfNumber {
	UnknownTypeOfNumber = 0,
	InternationalNumber = 1,
	NationalNumber = 2,
	NetworkSpecificNumber = 3,
	ShortCodeNumber = 4,
	AlphanumericNumber = 5,
	AbbreviatedNumber = 6
};

std::ostream& operator<<(std::ostream&, TypeOfNumber);


/** GSM 04.08 Table 10.5.118 and GSM 03.40 9.1.2.5 */
enum NumberingPlan {
	UnknownPlan = 0,
	E164Plan = 1,
	X121Plan = 3,
	F69Plan = 4,
	NationalPlan = 8,
	PrivatePlan = 9,
	ERMESPlan = 10
};

std::ostream& operator<<(std::ostream&, NumberingPlan);



/** Codes for GSM band types, GSM 05.05 2.  */
enum GSMBand {
	GSM850=850,			///< US cellular
	EGSM900=900,		///< extended GSM
	DCS1800=1800,		///< worldwide DCS band
	PCS1900=1900		///< US PCS band
};



/**@name Actual radio carrier frequencies, in kHz, GSM 05.05 2 */
//@{
unsigned uplinkFreqKHz(GSMBand wBand, unsigned wARFCN);
unsigned uplinkOffsetKHz(GSMBand);
unsigned downlinkFreqKHz(GSMBand wBand, unsigned wARFCN);
//@}


/**@name GSM Logical channel (LCH) types. */
//@{
/** Codes for logical channel types. */
enum ChannelType {
	///@name Non-dedicated control channels.
	//@{
	SCHType,		///< sync
	FCCHType,		///< frequency correction
	BCCHType,		///< broadcast control
	CCCHType,		///< common control, a combination of several sub-types
	RACHType,		///< random access
	SACCHType,		///< slow associated control (acutally dedicated, but...)
	//@}
	///@name Dedicated control channels (DCCHs).
	//@{
	SDCCHType,		///< standalone dedicated control
	FACCHType,		///< fast associated control
	//@}
	///@name Traffic channels
	//@{
	TCHFType,		///< full-rate traffic
	TCHHType,		///< half-rate traffic
	AnyTCHType,		///< any TCH type
	//@}
	///@name Special internal channel types.
	//@{
	LoopbackFullType,		///< loopback testing
	LoopbackHalfType,		///< loopback testing
	AnyDCCHType,			///< any dedicated control channel
	UndefinedCHType,		///< undefined
	//@}
};


/** Print channel type name to a stream. */
std::ostream& operator<<(std::ostream& os, ChannelType val);


//@}



/** Mobile identity types, GSM 04.08 10.5.1.4 */
enum MobileIDType {
	NoIDType = 0,
	IMSIType = 1,
	IMEIType = 2,
	IMEISVType = 3,
	TMSIType = 4
};

std::ostream& operator<<(std::ostream& os, MobileIDType);


/** Type and TDMA offset of a logical channel, from GSM 04.08 10.5.2.5 */
enum TypeAndOffset {
	TDMA_MISC=0,
	TCHF_0=1,
	TCHH_0=2, TCHH_1=3,
	SDCCH_4_0=4, SDCCH_4_1=5, SDCCH_4_2=6, SDCCH_4_3=7,
	SDCCH_8_0=8, SDCCH_8_1=9, SDCCH_8_2=10, SDCCH_8_3=11,
	SDCCH_8_4=12, SDCCH_8_5=13, SDCCH_8_6=14, SDCCH_8_7=15,
	/// Some extra ones for our internal use.
	TDMA_BEACON_BCCH=253,
	TDMA_BEACON_CCCH=252,
	TDMA_BEACON=255
};

std::ostream& operator<<(std::ostream& os, TypeAndOffset);








/**
 L3 Protocol Discriminator, GSM 04.08 10.2, GSM 04.07 11.2.3.1.1.
*/
enum L3PD {
	L3GroupCallControlPD=0x00,
	L3BroadcastCallControlPD=0x01,
	L3PDSS1PD=0x02,
	L3CallControlPD=0x03,
	L3PDSS2PD=0x04,
	L3MobilityManagementPD=0x05,
	L3RadioResourcePD=0x06,
	L3MobilityManagementGPRSPD=0x08,
	L3SMSPD=0x09,
	L3GPRSSessionManagementPD=0x0a,
	L3NonCallSSPD=0x0b,
	L3LocationPD=0x0c,
	L3ExtendedPD=0x0e,
	L3TestProcedurePD=0x0f,
	L3UndefinedPD=-1
};



std::ostream& operator<<(std::ostream& os, L3PD val);




/**@name Tables related to Tx-integer; GSM 04.08 3.3.1.1.2 and 10.5.2.29. */
//@{
/** "T" parameter, from GSM 04.08 10.5.2.29.  Index is TxInteger. */
extern const unsigned RACHSpreadSlots[];
/** "S" parameter, from GSM 04.08 3.3.1.1.2.  Index is TxInteger. */
extern const unsigned RACHWaitSParam[];
//@}




/**@name Modulus operations for frame numbers. */
//@{
/** The GSM hyperframe is largest time period in the GSM system, GSM 05.02 4.3.3. */
const uint32_t gHyperframe = 2048UL * 26UL * 51UL;

/** Get a clock difference, within the modulus, v1-v2. */
int32_t FNDelta(int32_t v1, int32_t v2);

/**
	Compare two frame clock values.
	@return 1 if v1>v2, -1 if v1<v2, 0 if v1==v2
*/
int FNCompare(int32_t v1, int32_t v2);


//@}




/**
	GSM frame clock value. GSM 05.02 4.3
	No internal thread sync.
*/
class Time {

	private:

	int mFN;				///< frame number in the hyperframe
	int mTN;			///< timeslot number

	public:

	Time(int wFN=0, int wTN=0)
		:mFN(wFN),mTN(wTN)
	{ }


	/** Move the time forward to a given position in a given modulus. */
	void rollForward(unsigned wFN, unsigned modulus)
	{
		assert(modulus<gHyperframe);
		while ((mFN % modulus) != wFN) mFN=(mFN+1)%gHyperframe;
	 }

	/**@name Accessors. */
	//@{
	int FN() const { return mFN; }
	void FN(unsigned wFN) { mFN = wFN; }
	unsigned TN() const { return mTN; }
	void TN(unsigned wTN) { mTN=wTN; }
	//@}

	/**@name Arithmetic. */
	//@{

	Time& operator++()
	{
		mFN = (mFN+1) % gHyperframe;
		return *this;
	}

    Time& decTN(unsigned step=1)
    {
		assert(step<=8);
		mTN -= step;
		if (mTN<0) {
			mTN+=8;
			mFN-=1;
			if (mFN<0) mFN+=gHyperframe;
		}
        return *this;
    }

	Time& incTN(unsigned step=1)
	{
		assert(step<=8);
		mTN += step;
		if (mTN>7) {
			mTN-=8;
			mFN = (mFN+1) % gHyperframe;
		}
		return *this;
	}

	Time& operator+=(int step)
	{
		// Remember the step might be negative.
		mFN += step;
		if (mFN<0) mFN+=gHyperframe;
		mFN = mFN % gHyperframe;
		return *this;
	}

	Time operator-(int step) const
		{ return operator+(-step); }

	Time operator+(int step) const
	{
		Time newVal = *this;
		newVal += step;
		return newVal;
	}

	Time operator+(const Time& other) const
    {
        unsigned newTN = (mTN + other.mTN) % 8;
		uint64_t newFN = (mFN+other.mFN + (mTN + other.mTN)/8) % gHyperframe;
        return Time(newFN,newTN);
    } 

	int operator-(const Time& other) const
	{
		return FNDelta(mFN,other.mFN);
	}

	//@}


	/**@name Comparisons. */
	//@{

	bool operator<(const Time& other) const
	{
		if (mFN==other.mFN) return (mTN<other.mTN);
		return FNCompare(mFN,other.mFN)<0;
	}

	bool operator>(const Time& other) const
	{
		if (mFN==other.mFN) return (mTN>other.mTN);
		return FNCompare(mFN,other.mFN)>0;
	}

	bool operator<=(const Time& other) const
	{
		if (mFN==other.mFN) return (mTN<=other.mTN);
		return FNCompare(mFN,other.mFN)<=0;
	}

	bool operator>=(const Time& other) const
	{
		if (mFN==other.mFN) return (mTN>=other.mTN);
		return FNCompare(mFN,other.mFN)>=0;
	}

	bool operator==(const Time& other) const
	{
		return (mFN == other.mFN) && (mTN==other.mTN);
	}

	//@}



	/**@name Standard derivations. */
	//@{

	/** GSM 05.02 3.3.2.2.1 */
	unsigned SFN() const { return mFN / (26*51); }

	/** GSM 05.02 3.3.2.2.1 */
	unsigned T1() const { return SFN() % 2048; }

	/** GSM 05.02 3.3.2.2.1 */
	unsigned T2() const { return mFN % 26; }

	/** GSM 05.02 3.3.2.2.1 */
	unsigned T3() const { return mFN % 51; }

	/** GSM 05.02 3.3.2.2.1. */
	unsigned T3p() const { return (T3()-1)/10; }

	/** GSM 05.02 6.3.1.3. */
	unsigned TC() const { return (FN()/51) % 8; }

	/** GSM 04.08 10.5.2.30. */
	unsigned T1p() const { return SFN() % 32; }

	/** GSM 05.02 6.2.3 */
	unsigned T1R() const { return T1() % 64; }

	//@}
};


std::ostream& operator<<(std::ostream& os, const Time& ts);






/**
	A class for calculating the current GSM frame number.
	Has built-in concurrency protections.
*/
class Clock {

	private:

	mutable Mutex mLock;
	int32_t mBaseFN;
	Timeval mBaseTime;

	public:

	Clock(const Time& when = Time(0))
		:mBaseFN(when.FN())
	{}

	/** Set the clock to a value. */
	void set(const Time&);

	/** Read the clock. */
	int32_t FN() const;

	/** Read the clock. */
	Time get() const { return Time(FN()); }

	/** Block until the clock passes a given time. */
	void wait(const Time&) const;
};








/**
	CCITT Z.100 activity timer, as described in GSM 04.06 5.1.
	All times are in milliseconds.
*/
class Z100Timer {

	private:

	Timeval mEndTime;		///< the time at which this timer will expire
	long mLimitTime;		///< timeout in milliseconds
	bool mActive;			///< true if timer is active

	public:

	/** Create a timer with a given timeout in milliseconds. */
	Z100Timer(long wLimitTime)
		:mLimitTime(wLimitTime),
		mActive(false)
	{}

	/** Blank constructor; if you use this object, it will assert. */
	Z100Timer():mLimitTime(0),mActive(false) {}

	/** True if the timer is active and expired. */
	bool expired() const;

	/** Force the timer into an expired state. */
	void expire();

	/** Start or restart the timer. */
	void set();

	/** Start or restart the timer, possibly specifying a new limit. */
	void set(long wLimitTime);

	/** Stop the timer. */
	void reset() { assert(mLimitTime!=0); mActive = false; }

	/** Returns true if the timer is active. */
	bool active() const { return mActive; }

	/**
		Remaining time until expiration, in milliseconds.
		Returns zero if the timer has expired.
	*/
	long remaining() const;

	/**
		Block until the timer expires.
		Returns immediately if the timer is not running.
	*/
	void wait() const;
};





}; 	// namespace GSM


#endif

// vim: ts=4 sw=4
