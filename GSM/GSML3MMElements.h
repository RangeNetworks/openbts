/**@file @brief Elements for Mobility Management messages, GSM 04.08 9.2. */

/*
* Copyright 2008-2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
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



#ifndef GSML3MMELEMENTS_H
#define GSML3MMELEMENTS_H

#include "L3Enums.h"
#include "GSML3Message.h"
#include <OpenBTSConfig.h>

namespace GSM {

/** CM Service Type, GSM 04.08 10.5.3.3 */
// (pat) This is used to identify the type of TransactionEntry, and so has been extended by Range with non-standard codes
// to identify all possible types of TransactionEntry.
class L3CMServiceType : public L3ProtocolElement {

	public:
	enum TypeCode {
		UndefinedType=0,
		MobileOriginatedCall=1,
		EmergencyCall=2,
		ShortMessage=4,							///< specifically, MO-SMS
		SupplementaryService=8,
		VoiceCallGroup=9,
		VoiceBroadcast=10,
		LocationService=11,		// (pat) See GSM 04.71.  Has nothing to do with MM Location Update.

		MobileTerminatedCall=100,				///< non-standard code
		MobileTerminatedShortMessage=101,		///< non-standard code
		HandoverCall=103,		///< non-standard code
		LocationUpdateRequest=105, ///< non-standard code
	};
		
	private:

	TypeCode mType;

	public:

	L3CMServiceType(TypeCode wType=UndefinedType)
		:L3ProtocolElement(),mType(wType)
	{}

	TypeCode type() const { return mType; }

	// Is it any call-control service type?
	bool isCC() const { return mType == MobileOriginatedCall || mType == EmergencyCall || mType == MobileTerminatedCall; }

	// Is it any call-control service type?
	bool isSMS() const { return mType == ShortMessage || mType == MobileTerminatedShortMessage; }

	// Is it any type of MM specific service type?
	bool isMM() const { return mType == LocationUpdateRequest; }

	bool operator==(const L3CMServiceType& other) const
		{ return mType == other.mType; }

	bool operator!=(const L3CMServiceType& other) const
		{ return mType != other.mType; }
	
	size_t lengthV() const { return 0; }	
	void writeV(L3Frame&, size_t&) const { devassert(0); }
	void parseV(const L3Frame &src, size_t &rp);
	void parseV(const L3Frame&, size_t&, size_t) { devassert(0); }
	void text(std::ostream&) const;

};

typedef L3CMServiceType::TypeCode CMServiceTypeCode;


std::ostream& operator<<(std::ostream& os, CMServiceTypeCode code);


/** RejectCause, GSM 04.08 10.5.3.6 */
// Better: 24.008 10.5.3.6
// This is the Mobility Management reject cause.
// For RR causes see L3RRCause, and for CC Causes see L3Cause.
class L3RejectCauseIE : public L3ProtocolElement {
private:
	MMRejectCause mRejectCause;

public:
	
	L3RejectCauseIE( const MMRejectCause wRejectCause=L3RejectCause::Zero )
		:L3ProtocolElement(),mRejectCause(wRejectCause)
	{}

	size_t lengthV() const { return 1; }	
	void writeV( L3Frame& dest, size_t &wp ) const;
	void parseV(const L3Frame&, size_t&);
	void parseV(const L3Frame&, size_t& , size_t) { devassert(0); }
	void text(std::ostream&) const;
};




/**
	Network Name, GSM 04.08 10.5.3.5a
	This class supports UCS2 and 7-bit (default) encodings.
*/
class L3NetworkName : public L3ProtocolElement {


private:

	static const unsigned maxLen=93;
	GSMAlphabet mAlphabet;		///< Alphabet to use for encoding
	char mName[maxLen+1];		///< network name as a C string
	int mCI;		///< CI (Country Initials) bit value

public:

	/** Set the network name, taking the default from gConfig. */
	L3NetworkName(const char* wName,
	              GSMAlphabet alphabet=ALPHABET_7BIT,
	              int wCI=gConfig.getBool("GSM.ShowCountry"))
		:L3ProtocolElement(), mAlphabet(alphabet), mCI(wCI)
	{ strncpy(mName,wName,maxLen); mName[maxLen] = '\0'; }

	size_t lengthV() const
	{
		if (mAlphabet == ALPHABET_UCS2)
			return 1+strlen(mName)*2;
		else
			return 1+(strlen(mName)*7+7)/8;
	}
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&) { devassert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { devassert(0); }
	void text(std::ostream&) const;
};

/**
	Time & Time Zone, GSM 04.08 10.5.3.9, GSM 03.40 9.2.3.11.
	This class is also used in SMS.
*/
class L3TimeZoneAndTime : public L3ProtocolElement {
public:
	enum TimeType {
		LOCAL_TIME, ///< Used in SMS. Time is sent as local time. In this case
		            ///< timezone seems to be ignored by handsets (tested with
		            ///< Nokia DCT3, Siemens and Windows Mobile 6), but we still
		            ///< send it.
		UTC_TIME    ///< Used in MM Info message. Time is sent as UTC time. In
		            ///< this case phones seem to regard timezone information.
	};

protected:

	Timeval mTime;
	TimeType mType;

public:

	/** Defaults from the current time. */
	L3TimeZoneAndTime(const Timeval& wTime = Timeval(), TimeType type = LOCAL_TIME)
		:L3ProtocolElement(),
		mTime(wTime),
		mType(type)
	{}

	const Timeval& time() const { return mTime; }
	void time(const Timeval& wTime) { mTime=wTime; }

	TimeType type() const { return mType; }
	void type(TimeType type) { mType=type; }

	size_t lengthV() const { return 7; }
	void writeV(L3Frame&, size_t&) const;
	void parseV(const L3Frame& src, size_t &rp);
	void parseV(const L3Frame&, size_t& , size_t) { devassert(0); }
	void text(std::ostream&) const;
};


/** GSM 04.08 10.5.3.1 */
class L3RAND : public L3ProtocolElement {

	private:

	uint64_t mRUpper;		///< upper 64 bits
	uint64_t mRLower;		///< lower 64 bits

	public:

	L3RAND(uint64_t wRUpper, uint64_t wRLower):
		mRUpper(wRUpper),mRLower(wRLower)
	{ }

	size_t lengthV() const { return 16; }
	void writeV(L3Frame&, size_t&) const;
	void parseV(const L3Frame&, size_t&) { devassert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { devassert(0); }
	void text(std::ostream&) const;
};


/** GSM 04.08 10.5.3.2 */
class L3SRES : public L3ProtocolElement {

	private:

	uint32_t mValue;

	public:

	L3SRES(uint32_t wValue):
		mValue(wValue)
	{ }

	L3SRES():mValue(0) {}

	uint32_t value() const { return mValue; }

	size_t lengthV() const { return 4; }
	void writeV(L3Frame&, size_t&) const { devassert(0); }
	void parseV(const L3Frame&, size_t&);
	void parseV(const L3Frame&, size_t& , size_t) { devassert(0); }
	void text(std::ostream&) const;
};



} // namespace GSM

#endif

// vim: ts=4 sw=4
