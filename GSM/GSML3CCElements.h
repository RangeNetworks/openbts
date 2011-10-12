/**@file Elements for Call Control, GSM 04.08 10.5.4.  */
/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
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




#ifndef GSML3CCELEMENTS_H
#define GSML3CCELEMENTS_H

#include "GSML3Message.h"
#include <iostream>


namespace GSM {

/** Bearer Capability, GSM 04.08 10.5.4.5 */
class L3BearerCapability : public L3ProtocolElement {

	// The spec for this is really intimidating.
	// But we're just going to assume circuit-switched speech
	// with a full rate codec, since every phone supports that.
	// So we can just ignore this hideously complex element.

public:

	L3BearerCapability() : L3ProtocolElement() {}
	
	size_t lengthV() const { return 2; }
	void writeV( L3Frame& dest, size_t &wp ) const;
	void parseV( const L3Frame& src, size_t &rp, size_t expectedLength );	
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void text(std::ostream&) const;

};



/** A general class for BCD numbers as they normally appear in L3. */
class L3BCDDigits {

	private:

	static const unsigned maxDigits = 20;
	char mDigits[maxDigits+1];					///< ITU-T E.164 limits address to 15 digits

	public:

	L3BCDDigits() { mDigits[0]='\0'; }

	L3BCDDigits(const char* wDigits) { strncpy(mDigits,wDigits,sizeof(mDigits)-1); mDigits[sizeof(mDigits)-1]='\0'; }

	void parse(const L3Frame& src, size_t &rp, size_t numOctets);
	void write(L3Frame& dest, size_t &wp) const;

	/** Return number of octets needed to encode the digits. */
	size_t lengthV() const;

	unsigned size() const { return strlen(mDigits); }
	const char* digits() const { return mDigits; }
};


std::ostream& operator<<(std::ostream&, const L3BCDDigits&);







/** Calling Party BCD Number, GSM 04.08 10.5.4.9 */
class L3CallingPartyBCDNumber : public L3ProtocolElement {

private:

	TypeOfNumber mType;
	NumberingPlan mPlan;

	L3BCDDigits mDigits;

	/**@name Octet 3a */
	//@{
	bool mHaveOctet3a;
	int mPresentationIndicator;
	int mScreeningIndicator;
	//@}


public:

	L3CallingPartyBCDNumber()
		:mType(UnknownTypeOfNumber),mPlan(UnknownPlan),
		mHaveOctet3a(false)
	{ }

	L3CallingPartyBCDNumber( const char * wDigits )
		:mType(NationalNumber),mPlan(E164Plan),mDigits(wDigits),
		mHaveOctet3a(false)
	{ }


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
		:mType(NationalNumber),mPlan(E164Plan),mDigits(wDigits)
	{ }

	L3CalledPartyBCDNumber(const L3CallingPartyBCDNumber& other)
		:mType(other.type()),mPlan(other.plan()),mDigits(other.digits())
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
	Read the spec closely: we only have to support coding standard 3 (GSM),
	and that format doesn't carry the "recommendation" field.
*/

class L3Cause : public L3ProtocolElement {

public:

	enum Location {
		User=0,
		PrivateServingLocal=1,
		PublicServingLocal=2,
		Transit=3,
		PublicServingRemote=4,
		PrivateServingRemote=5,
		International=7,
		BeyondInternetworking=10
	};

private:

	Location mLocation;
	unsigned mCause;
	
public:

	L3Cause(unsigned wCause=0, Location wLocation=PrivateServingLocal)
		:L3ProtocolElement(),
		mLocation(wLocation),mCause(wCause)
	{ }

	Location location() const { return mLocation; }
	unsigned cause() const { return mCause; }

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


} // GSM

#endif
// vim: ts=4 sw=4
