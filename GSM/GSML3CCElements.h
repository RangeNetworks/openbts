/**@file Elements for Call Control, GSM 04.08 10.5.4.  */
/*
* Copyright 2008, 2009, 2014 Free Software Foundation, Inc.
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




#ifndef GSML3CCELEMENTS_H
#define GSML3CCELEMENTS_H

#include "GSML3Message.h"
#include <iostream>
#include <CodecSet.h>
#include <Logger.h>

namespace GSM {

/** Bearer Capability, GSM 04.08 10.5.4.5 */
class L3BearerCapability : public L3ProtocolElement {

	// Obsolete comment:
	// The spec for this is really intimidating.
	// But we're just going to assume circuit-switched speech
	// with a full rate codec, since every phone supports that.
	// So we can just ignore this hideously complex element.

	// (pat) There may be multiple BearerCapability IEs for speech and non-speech.
	// We save only the speech one; the speech version of this IE includes only Octet3
	// and zero or more octet3a, one for each codec supported.
	uint8_t mOctet3, mOctet3a[10];
	unsigned mNumOctet3a;	// Number of elements in mOctet3a.

public:
	Bool_z mPresent;

	L3BearerCapability() {
		mOctet3 = 0x0f; // We hard code this octet for circuit switched speech.
		mNumOctet3a = 1;
		mOctet3a[0] = 0x80; // We hard code for full rate speech v1, the GSM 06.10 codec.
	}
	
	size_t lengthV() const { return 2; }
	void writeV( L3Frame& dest, size_t &wp ) const;
	void parseV( const L3Frame& src, size_t &rp, size_t expectedLength );	
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void text(std::ostream&) const;

	// accessors
	// Note: As defined in 26.103 and 48.008
	// Meaning of these bits is hard to find: It is in 48.008 3.2.2.11:
	// GSM speech full rate version 1: GSM FR
	// GSM speech full rate version 2: GSM EFR
	// GSM speech full rate version 3: FR AMR
	// GSM speech full rate version 4: OFR AMR-WB
	// GSM speech full rate version 5: FR AMR-WB
	// GSM speech half rate version 1: GSM HR
	// GSM speech half rate version 2: not defined
	// GSM speech half rate version 3: HR AMR
	// GSM speech half rate version 4: OHR AMR-WB
	// GSM speech half rate version 6: OHR AMR
	//unsigned getSpeechVersion() { return mOctet3 & 0xf; }
	// Return the CodecType for the speech version in octet n;
	Control::CodecType getCodecType(unsigned n) const;
	Control::CodecType getCodecSet() const;
	unsigned getHalfRateSupport() { return mOctet3 & 0x40; } // Bit 7 is true if half-rate supported.
};

// (pat) Added 10-22-2012.
// 3GPP 24.008 10.5.4.32 and 3GPP 26.103
// I added this IE before I read the fine print.  This is only used for UMTS, and the BearerCapability
// is used for GSM radio networks, so we dont really need this, since any UMTS phone supports AMR_FR,
// which is all we care.  But here it is.
class L3SupportedCodecList : public L3ProtocolElement
{
	Control::CodecSet mGsmCodecs, mUmtsCodecs;
	enum {	// SysID defined in 26.103 6.1
		SysIdGSM = 0,
		SysIdUMTS = 4
	};
	public:
	Bool_z mPresent;					// Was the IE present?
	Bool_z mGsmPresent, mUmtsPresent;	// Were these sub-parts of the IE present?
	L3SupportedCodecList() {}
	Control::CodecSet getCodecSet() const;	// Return codec set for gsm in OpenBTS or umts in OpenNodeB
	// Each sub-section is 4 bytes.
	size_t lengthV() const { return (mGsmPresent?4:0) + (mUmtsPresent?4:0); }	// length excluding IEI and initial length byte.
	void writeV( L3Frame& dest, size_t &wp ) const;
	void parseV( const L3Frame& src, size_t &rp, size_t expectedLength );	
	void parseV(const L3Frame&, size_t&) { assert(0); } // This IE must always include an initial length byte.
	void text(std::ostream&) const;
};



class L3CCCapabilities
{
	public:
	/// Bearer Capability IE
	// (pat) BearerCapability is sent by GSM phone
	//Bool_z mHaveBearerCapability;
	L3BearerCapability mBearerCapability;

	// (pat) SupportedCodecList is sent by UMTS phone
	//Bool_z mHaveSupportedCodecs;
	L3SupportedCodecList mSupportedCodecs;	// (pat) added 10-22-2012

	// Return the CodecSet for the radio access capability we are in.
	Control::CodecSet getCodecSet() const;
};

/** A general class for BCD numbers as they normally appear in L3. */
class L3BCDDigits {

	private:

	static const unsigned maxDigits = 20;
	char mDigits[maxDigits+1];					///< ITU-T E.164 limits address to 15 digits

	public:

	L3BCDDigits() { mDigits[0]='\0'; }

	// (pat) The -1 below and +1 above are mutually redundant.
	L3BCDDigits(const char* wDigits) { strncpy(mDigits,wDigits,sizeof(mDigits)-1); mDigits[sizeof(mDigits)-1]='\0'; }

	L3BCDDigits(const L3BCDDigits &other) {
		memcpy(mDigits,other.mDigits,sizeof(mDigits));
	}

	void parse(const L3Frame& src, size_t &rp, size_t numOctets, bool international = false);
	void write(L3Frame& dest, size_t &wp) const;

	/** Return number of octets needed to encode the digits. */
	size_t lengthV() const;

	unsigned size() const { return strlen(mDigits); }
	const char* digits() const { return mDigits; }
};


std::ostream& operator<<(std::ostream&, const L3BCDDigits&);







/** Calling Party BCD Number, GSM 04.08 10.5.4.9 */
// (pat) 24.018 10.5.4.9 quote: "This IE is not used in the MS to network direction."
class L3CallingPartyBCDNumber : public L3ProtocolElement {

private:

	TypeOfNumber mType;
	NumberingPlan mPlan;

	L3BCDDigits mDigits;

	/**@name Octet 3a */
	//@{
	Bool_z mHaveOctet3a;
	int mPresentationIndicator;	// uninited, but not used unless mHaveOctet3a
	int mScreeningIndicator;	// uninited, but not used unless mHaveOctet3a
	//@}


public:

	L3CallingPartyBCDNumber()
		:mType(UnknownTypeOfNumber), mPlan(UnknownPlan),
		mHaveOctet3a(false)
	{ }

	L3CallingPartyBCDNumber( const char * wDigits )
		:mPlan(E164Plan), mDigits(wDigits),
		mHaveOctet3a(false)
	{
		mType = (wDigits[0] == '+') ?  GSM::InternationalNumber : GSM::NationalNumber;
		//LOG(DEBUG) << "L3CallingPartyBCDNumber ctor type=" << mType << " Digits " << wDigits;		(pat) what was "ctor"?
		LOG(DEBUG) << "L3CallingPartyBCDNumber create type=" << mType << " Digits " << wDigits;
	}

	L3CallingPartyBCDNumber(const L3CallingPartyBCDNumber &other)
		:mType(other.mType),mPlan(other.mPlan),mDigits(other.mDigits),
		mHaveOctet3a(other.mHaveOctet3a),
		mPresentationIndicator(other.mPresentationIndicator),
		mScreeningIndicator(other.mScreeningIndicator)
	{}


	NumberingPlan plan() const { return mPlan; }
	TypeOfNumber type() const { return mType; }
	const char* digits() const { return mDigits.digits(); }

	size_t lengthV() const;
	void writeV( L3Frame& dest, size_t &wp  ) const;
	void parseV( const L3Frame& src, size_t &rp, size_t expectedLength);	
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void text(std::ostream&) const;
};


/** Called Party BCD Number, GSM 04.08 10.5.4.7 */
class L3CalledPartyBCDNumber : public L3ProtocolElement {


private:

	TypeOfNumber mType;
	NumberingPlan mPlan;
	L3BCDDigits mDigits;

public:

	L3CalledPartyBCDNumber()
		:mType(UnknownTypeOfNumber),
		mPlan(UnknownPlan)
	{ }

	L3CalledPartyBCDNumber(const char * wDigits)
		:mPlan(E164Plan), mDigits(wDigits)
	{
		mType = (wDigits[0] == '+') ?  GSM::InternationalNumber : GSM::NationalNumber;
		LOG(DEBUG) << "L3CallingPartyBCDNumber ctor type=" << mType << " Digits " << wDigits;
	}

	// (pat) This auto-conversion from CallingParty to CalledParty was used in the SMS code,
	// however, it was creating a disaster during unintended auto-conversions of L3Messages,
	// which are unintentionally sprinkled throughout the code base due to incomplete constructors.
	// The fix would be to add 'explicit' keywords everywhere.
	explicit L3CalledPartyBCDNumber(const L3CallingPartyBCDNumber& other)
		:mType(other.type()),mPlan(other.plan()),mDigits(other.digits())
	{ }

	// (pat) We must have this constructor as a choice also.
	L3CalledPartyBCDNumber(const L3CalledPartyBCDNumber& other)
		:mType(other.mType),mPlan(other.mPlan),mDigits(other.mDigits)
	{ }


	NumberingPlan plan() const { return mPlan; }
	TypeOfNumber type() const { return mType; }
	const char* digits() const { return mDigits.digits(); }

	size_t lengthV() const ; 
	void writeV( L3Frame& dest, size_t &wp  ) const;
	void parseV( const L3Frame& src, size_t &rp, size_t expectedLength );	
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void text(std::ostream&) const;
};








/**
	Cause, GSM 04.08 10.5.4.11
	Very poorly names, it is the Call Control cause.
	Read the spec closely: we only have to support coding standard 3 (GSM),
	and that format doesn't carry the "recommendation" field.
*/

class L3CauseElement : public L3ProtocolElement {
public:
	typedef L3Cause::Location Location;
	typedef L3Cause::CCCause Cause;

private:

	// FIXME -- This should include any supplied diagnostics.
	// See ticket GSM 04.08 10.5.4.11 and ticket #1139.

	Location mLocation;
	Cause mCause;		// 7 bits of cause, consisting of 3 bit "class" and 4 bit "value".
	
public:

	L3CauseElement(Cause wCause=L3Cause::Normal_Call_Clearing, Location wLocation=L3Cause::Private_Serving_Local)
		:L3ProtocolElement(),
		mLocation(wLocation),mCause(wCause)
	{ }

	Location location() const { return mLocation; }
	Cause cause() const { return mCause; }

	// We don't support diagnostics, so length=2.
	size_t lengthV() const { return  2; }

	void writeV( L3Frame& dest, size_t &wp) const;
	void parseV( const L3Frame& src, size_t &rp , size_t expectedLength );
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void text(std::ostream&) const;
};


/** Call State, GSM 04.08 10.5.4.6. */
class L3CallState : public L3ProtocolElement {

private:

	unsigned mCallState;

public:

	/** The default call state is the "Null" state. */
	L3CallState( unsigned wCallState=0 )
		:L3ProtocolElement(),
		mCallState(wCallState)
	{ }

	unsigned callState() const { return mCallState; }

	size_t lengthV()const { return 1;}
	void writeV( L3Frame& dest, size_t &wp) const;
	void parseV( const L3Frame& src, size_t &rp );
	void parseV(const L3Frame&, size_t&, size_t) { assert(0); }
	void text(std::ostream&) const;
	
};

/** GSM 04.08 10.5.4.21 */
class L3ProgressIndicator : public L3ProtocolElement {

	public:

	enum Location {
		User=0,
		PrivateServingLocal=1,
		PublicServingLocal=2,
		PublicServingRemote=4,
		PrivateServingRemote=5,
		BeyondInternetworking=10
	};

	// pat 2-2014: GSM 4.08 5.5.1: The ProgressIndicator is used to start in-band tones and announcements
	// if the value is 1-3 or 6-20.
	enum Progress {
		Unspecified=0,
		NotISDN=1,
		DestinationNotISDN=2,
		OriginationNotISDN=3,
		ReturnedToISDN=4,
		InBandAvailable=8,
		EndToEndISDN=0x20,
		Queuing=0x40
	};

	private:

	Location mLocation;
	Progress mProgress;

	public:

	/** Default values are unspecified progress in the BTS. */
	L3ProgressIndicator(Progress wProgress=Unspecified, Location wLocation=PrivateServingLocal)
		:L3ProtocolElement(),
		mLocation(wLocation),mProgress(wProgress)
	{}

	Location location() const { return mLocation; }
	Progress progress() const { return mProgress; }

	size_t lengthV() const { return 2; }
   	void writeV(L3Frame& dest, size_t &wp ) const;
	void parseV(const L3Frame&, size_t&, size_t) { assert(0); }
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void text(std::ostream&) const;
};


/** GSM 04.08 10.5.4.17 */
class L3KeypadFacility : public L3ProtocolElement {

	private:

	char mIA5;

	public:

	L3KeypadFacility(char wIA5=0)
		:mIA5(wIA5)
	{}

	char IA5() const { return mIA5; }

	size_t lengthV() const { return 1; }
   	void writeV(L3Frame&, size_t&) const;
	void parseV(const L3Frame&, size_t&, size_t) { assert(0); }
	void parseV(const L3Frame& src, size_t& rp);
	void text(std::ostream&) const;
};


/** GSM 04.08 10.5.4.23 */
// (pat) 2-2014: Added to try to work around bug that ZTE phone does not play ring-back tone during Alerting.
class L3Signal : public L3ProtocolElement {
	char mSignalValue;
	public:
	enum SignalValues {
		SignalDialToneOn = 0,
		SignalRingBackToneOn = 1,
		SignalInterceptToneOn = 2,
		SignalNetworkCongestionToneOn = 3,
		SignalBusyToneOn = 4,
		SignalConfirmToneOn = 5,
		SignalAnswerToneOn = 6,
		SignalCallWaitingToneOn = 7,
		SignalTonesOff = 0x3f,
		SignalAlertingOff = 0x4f
	};
	L3Signal(SignalValues tone = SignalRingBackToneOn) : mSignalValue(tone) {}

	size_t lengthV() const { return 1; }
   	void writeV(L3Frame&, size_t&) const;
	void parseV(const L3Frame&, size_t&, size_t) { assert(0); }
	void parseV(const L3Frame& src, size_t& rp);
	void text(std::ostream&) const;
};

} // GSM

#endif
// vim: ts=4 sw=4
