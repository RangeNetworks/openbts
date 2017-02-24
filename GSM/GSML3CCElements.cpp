/**@file
	@brief Call Control messages, GSM 04.08 9.3
*/
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

#define LOG_GROUP LogGroup::GSM		// Can set Log.Level.GSM for debugging


#include "GSML3CCElements.h"
#include <ControlTransfer.h>
#include <Logger.h>
#define CASENAME(x) case x: return #x;

using namespace std;
using namespace Control;
namespace GSM {


void L3BearerCapability::writeV( L3Frame &dest, size_t &wp ) const
{
	// See GSM 10.5.4.5.
	// This is a hell of a complex element, inherited from ISDN.
	// But we're going to ignore a lot of it.

	// "octet 3"
	dest.writeField(wp, mOctet3, 8);

	// zero or more "octet 3a"
	for (unsigned i = 0; i < mNumOctet3a; i++) {
		dest.writeField(wp,mOctet3a[i],8);
	}
}


void L3BearerCapability::parseV( const L3Frame& src, size_t &rp, size_t expectedLength )
{
	mPresent = true;
	// See GSM 10.5.4.5.
	// This is a hell of a complex element, inherited from ISDN.
	// (pat) If the bearer capability is not for speech, we dont save it.
	// Bits 1-3 of octet3 == 0 are the indicator, but the caller already lopped off the IEI and length
	// so it is in the first byte.
	if (expectedLength == 0) {return;}	// Bad IE.  Toss it.
	size_t end = rp + 8*expectedLength;
	unsigned octet3 = src.readField(rp,8);
	LOG(DEBUG) "BearerCapability"<<LOGVAR(expectedLength);
	if ((octet3 & 7) == 0) {
		// This IE is is for speech, we will save it.
		mOctet3 = octet3;
		// Get the multiple octet3a.  There may be none.
		for (mNumOctet3a = 0; rp < end && mNumOctet3a < sizeof(mOctet3a); mNumOctet3a++) {
			mOctet3a[mNumOctet3a] = src.readField(rp,8);
			LOG(DEBUG) "Add BearerCapability"<<LOGVAR(mNumOctet3a)<<LOGVAR(mOctet3a[mNumOctet3a]);
		}
	}

	// Just move the read index and return.
	rp = end;
}

// Convert the L3BearerCapability Speech Version IE into a CodecType.
// Using 24.008 5.4.5 table 10.5.103 interpolated via meanings in 48.008 3.2.2.11 into 26.103 6.2.  Gotta love this stuff.
CodecType L3BearerCapability::getCodecType(unsigned n) const
{
	// There is no rhyme or reason to the values.
	switch (mOctet3a[n] & 0xf) {
		case 0: return GSM_FR;
		case 2: return GSM_EFR;
		case 4: return AMR_FR;
		case 6: return OFR_AMR_WB;
		case 8: return AMR_FR_WB;
		case 1: return GSM_HR;
		case 5: return AMR_HR;
		case 7: return OHR_AMR_WB;
		case 11: return OHR_AMR;
		case 15: return CodecTypeUndefined;	// "No speech version supported for GERAN."
		default: return CodecTypeUndefined;	// undefined
	}
}

CodecType L3BearerCapability::getCodecSet() const
{
	unsigned result = 0;
	for (unsigned i = 0; i < mNumOctet3a; i++) {
		result |= getCodecType(i);
	}
	// If no Octet3a is given, the default is GSM_FR as you would expect.
	return result ? (CodecType) result : GSM_FR;
}

void L3BearerCapability::text(ostream& os) const
{
	os << LOGHEX2("octet3",mOctet3);
	for (unsigned i = 0; i < mNumOctet3a; i++) {
		os<<LOGHEX2("octet3a",mOctet3a[i]) << "(" <<CodecType2Name(getCodecType(i)) << ")";
	}
}

void L3SupportedCodecList::writeV( L3Frame& dest, size_t &wp ) const
{
	if (mGsmPresent) {
		dest.writeField(wp,SysIdGSM,8);
		dest.writeField(wp,2,8);	// bitmap length is 2 bytes.
		dest.writeField(wp,mGsmCodecs.mCodecs,16);	// bitmap length is 2 bytes.
	}
	if (mUmtsPresent) {
		dest.writeField(wp,SysIdUMTS,8);
		dest.writeField(wp,2,8);	// bitmap length is 2 bytes.
		dest.writeField(wp,mUmtsCodecs.mCodecs,16);	// bitmap length is 2 bytes.
	}
}

// (pat) 3GPP 26.103 6.2 defines the Codec Bitmap for Call Control.
// Looks like this:
// Bit: 8        7         6        5      4      3       2      1 
//      TDMA_EFR UMTS_AMR2 UMTS_AMR H_RAMR FR_AMR GSM_EFR GSM_HR GSM_FR
// Optional second byte:
// Bit: 16        15         14        13        12      11         10       9
//      -         -          OHR_AMRWB OFR_AMRWB OHR_AMR UMTS_AMRWB FR_AMRWB PDC_EFR
void L3SupportedCodecList::parseV( const L3Frame& src, size_t &rp, size_t expectedLength )
{
	mPresent = true;
	while (expectedLength >= 3) {
		int sysid = src.readField(rp,8);
		int bitmapLength = src.readField(rp,8);
		// (pat) If there are two bytes, the second is the high bits, so we cant just
		// read them as a single field, we have to byte swap them.
		int fixedLength = min((int)expectedLength-2,bitmapLength);	// Correct for error: bitmaplength longer than IE length.
		unsigned codeclist = 0;
		if (fixedLength >= 1) codeclist = src.readField(rp,8);
		if (fixedLength >= 2) codeclist |= (src.readField(rp,8) << 8);
		switch (sysid) {
		case SysIdGSM:
			mGsmPresent = true;
			mGsmCodecs.mCodecs = (CodecType)codeclist;
			break;
		case SysIdUMTS:
			mUmtsPresent = true;
			mUmtsCodecs.mCodecs = (CodecType)codeclist;
			break;
		default:	// toss it.
			break;
		}
		expectedLength -= 2 + bitmapLength;
	}
}

// Return the CodecSet for the radio access technology that is currently in use, ie,
// if OpenBTS product return gsm, if OpenNodeB product return umts.
CodecSet L3SupportedCodecList::getCodecSet() const
{
#if RN_UMTS
	return mUmtsPresent ? mUmtsCodecs : CodecSet();
#else
	return mGsmPresent ? mGsmCodecs : CodecSet();
#endif
}

void L3SupportedCodecList::text(std::ostream& os) const
{
	os << "SupportedCodecList=(";
	if (mGsmPresent) { os << "gsm="; mGsmCodecs.text(os); }
	if (mUmtsPresent) { if (mGsmPresent) {os<<",";} os << "umts="; mUmtsCodecs.text(os); }
	os << ")";
}

Control::CodecSet L3CCCapabilities::getCodecSet() const
{
	// (pat) I'm going to return an OR of all the codecs we find anywhere.
	Control::CodecSet result;
	if (mBearerCapability.mPresent) { result.orSet(mBearerCapability.getCodecSet()); }
	// This is supposedly only for UMTS but phones (Samsung Galaxy) may return it for GSM:
	if (mSupportedCodecs.mPresent) { result.orSet(mSupportedCodecs.getCodecSet()); }
	// If the phone doesnt report any capabilities, fall back to GSM_FR and hope.
	if (result.isEmpty()) { result.orType(GSM_FR); }
	return result;
}

void L3BCDDigits::parse(const L3Frame& src, size_t &rp, size_t numOctets, bool international)
{
	unsigned i=0;
	size_t readOctets = 0;
	LOG(DEBUG) << "parse international " << international;
	if (international) mDigits[i++] = '+';
	while (readOctets < numOctets) {
		unsigned d2 = src.readField(rp,4);
		unsigned d1 = src.readField(rp,4);
		readOctets++;
		mDigits[i++] = d1 == 10 ? '*' : d1 == 11 ? '#' : d1+'0';
		if (d2!=0x0f) mDigits[i++] = d2 == 10 ? '*' : d2 == 11 ? '#' : d2+'0';
		if (i>maxDigits) L3_READ_ERROR;
	}
	mDigits[i++]='\0';
}


static int encode(char c, bool *invalid)
{
	//return c == '*' ? 10 : c == '#' ? 11 : c-'0';
	if (c == '*') return 10;
	if (c == '#') return 11;
	if (isdigit(c)) return c - '0';
	*invalid = true;
	return 0;	// Not sure what to do.
}

/*
 * If digit string starts with a plus strip off the plus. I suspect that this get encoded as an international type somewhere else
 * The write function send digits/information and the parse function decodes and store digits/incomming information. SVG
 */
void L3BCDDigits::write(L3Frame& dest, size_t &wp) const
{
	bool invalid = false;
	unsigned index = 0;
	unsigned numDigits = strlen(mDigits);
	if (index < numDigits && mDigits[index] == '+') {
		LOG(DEBUG) << "write got +";
		index++;
	}
	while (index < numDigits) {
		if ((index+1) < numDigits) dest.writeField(wp,encode(mDigits[index+1],&invalid),4);
		else dest.writeField(wp,0x0f,4);
		dest.writeField(wp,encode(mDigits[index],&invalid),4);
		index += 2;
	}
	if (invalid) { LOG(ERR) << "Invalid BCD string: '" <<mDigits<< "'"; }
}


size_t L3BCDDigits::lengthV() const 
{
	unsigned sz = strlen(mDigits);
	if (*mDigits == '+') sz--;
	return (sz/2) + (sz%2);
}



ostream& operator<<(ostream& os, const L3BCDDigits& digits)
{
	os << digits.digits();
	return os;
}



void L3CalledPartyBCDNumber::writeV( L3Frame &dest, size_t &wp ) const
{
	dest.writeField(wp, 0x01, 1);	// dest.writeField(wp, *digits() == '+' ? InternationalNumber : mType, 3); // Don't think this makes sense
	//LOG(DEBUG) << "writeV mType " << mType << " first digit " << *digits();
	dest.writeField(wp, mType, 3);
	dest.writeField(wp, mPlan, 4);
	mDigits.write(dest,wp);
}

void L3CalledPartyBCDNumber::parseV( const L3Frame &src, size_t &rp, size_t expectedLength ) 
{
	LOG(DEBUG) << "L3CalledPartyBCDNumber::parseV rp="<<rp<<" expLen="<<expectedLength;
	// ext bit must be 1
	if (src.readField(rp, 1) != 1) L3_READ_ERROR;	
	mType = (TypeOfNumber)src.readField(rp, 3);
	//LOG(DEBUG) << "parseV mType " << mType;
	mPlan = (NumberingPlan)src.readField(rp, 4);
	mDigits.parse(src,rp,expectedLength-1, mType == InternationalNumber);
}


size_t L3CalledPartyBCDNumber::lengthV() const
{
	if (mDigits.lengthV()==0) return 0;
	return 1 + mDigits.lengthV();
}

void L3CalledPartyBCDNumber::text(ostream& os) const
{
	os << "type=" << mType;
	os << " plan=" << mPlan;
	os << " digits=" << mDigits;
}



void L3CallingPartyBCDNumber::writeV( L3Frame &dest, size_t &wp ) const
{
	// If Octet3a is extended, then write 0 else 1.
	dest.writeField(wp, (!mHaveOctet3a & 0x01), 1);
	LOG(DEBUG) << "writeV mType " << mType << " first digit " << *digits();
	dest.writeField(wp, mType, 3);
	dest.writeField(wp, mPlan, 4);

	if(mHaveOctet3a){
		dest.writeField(wp, 0x01, 1);
		dest.writeField(wp, mPresentationIndicator, 2); 	
		dest.writeField(wp, 0, 3);
		dest.writeField(wp, mScreeningIndicator, 2);
	}

	mDigits.write(dest,wp);
}





// (pat) 24.008 10.5.4.9 quote: "This IE is not used in the MS to network direction."
// Which is a good thing because I do not think this parseV is correct.
void L3CallingPartyBCDNumber::parseV( const L3Frame &src, size_t &rp, size_t expectedLength) 
{
	size_t remainingLength = expectedLength;
	// Read out first bit = 1.
	mHaveOctet3a = !src.readField(rp, 1);	// Bit is reversed 0 means you have an octet
	mType = (TypeOfNumber)src.readField(rp, 3);
	//LOG(DEBUG) << "parseV mType " << mType;
	mPlan = (NumberingPlan)src.readField(rp, 4);
	remainingLength -= 1;

	if (mHaveOctet3a) {
		if (src.readField(rp,1)!=1) L3_READ_ERROR;
		mPresentationIndicator = src.readField(rp, 3);
		src.readField(rp,3);
		mScreeningIndicator = src.readField(rp, 4);
		remainingLength -= 1;
	}

	mDigits.parse(src,rp,remainingLength, mType == InternationalNumber);
}


size_t L3CallingPartyBCDNumber::lengthV() const
{
	return 1 + mHaveOctet3a + mDigits.lengthV();
}



void L3CallingPartyBCDNumber::text(ostream& os) const
{
	os << "type=" << mType;
	os << " plan=" << mPlan;
	if (mHaveOctet3a) {
		os << " presentation=" << mPresentationIndicator;
		os << " screening=" << mScreeningIndicator;
	}
	os << " digits=" << mDigits;
}


void L3CauseElement::parseV(const L3Frame& src, size_t &rp , size_t expectedLength)
{
	size_t pos = rp;
	rp += 8*expectedLength;

	// Octet 3
	// We only supprt the GSM coding standard.
	if (src.readField(pos,4)!=0x0e) L3_READ_ERROR;
	mLocation = (Location)src.readField(pos,4);
	
	// Octet 4
	if (src.readField(pos,1)!=1)  L3_READ_ERROR;
	mCause = (Cause) src.readField(pos,7);	

	// Skip the diagnostics.
} 


void L3CauseElement::writeV(L3Frame& dest, size_t &wp) const
{
	// Write Octet3.
	dest.writeField(wp,0x0e,4);
	dest.writeField(wp,mLocation,4);

	// Write Octet 4.
	dest.writeField(wp,0x01,1);
	dest.writeField(wp,mCause,7);
}


void L3CauseElement::text(ostream& os) const
{
	os << "location=" << mLocation;
	os << " cause=0x" << hex << mCause << dec <<" "<<L3Cause::CCCause2Str(mCause);
}





void L3CallState::parseV( const L3Frame& src, size_t &rp)
{
	rp +=2;	
	mCallState = src.readField(rp, 6);
}

void L3CallState::writeV( L3Frame& dest, size_t &wp ) const
{
	dest.writeField(wp,3,2);
	dest.writeField(wp, mCallState, 6);
}

void L3CallState::text(ostream& os) const
{
	os << mCallState;
}

void L3ProgressIndicator::writeV(L3Frame& dest, size_t &wp) const
{
	// octet 3
	// ext bit, GSM coding standard, spare bit
	dest.writeField(wp,0x0e,4);
	dest.writeField(wp,mLocation,4);

	// octet 4
	dest.writeField(wp,1,1);
	dest.writeField(wp,mProgress,7);
}



void L3ProgressIndicator::text(ostream& os) const
{
	os << "location=" << mLocation;
	os << " progress=0x" << hex << mProgress << dec;
}




void L3KeypadFacility::parseV(const L3Frame& src, size_t &rp)
{
	mIA5 = src.readField(rp,8);
}


void L3KeypadFacility::writeV(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,mIA5,8);
}


void L3KeypadFacility::text(ostream& os) const
{
	os << hex << "0x" << mIA5 << dec;
}

// We shouldnt ever read in a Signal IE, but here it is anyway.
void L3Signal::parseV(const L3Frame& src, size_t &rp)
{
	mSignalValue = src.readField(rp,8);
}

void L3Signal::writeV(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,mSignalValue,8);
}

void L3Signal::text(ostream& os) const
{
	os << format("Signal(0x%x)",mSignalValue);
}

};

// vim: ts=4 sw=4


