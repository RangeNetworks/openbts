/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
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

#include <stdint.h>
#include <stdio.h>
#include <cstdio>

#include "SMSMessages.h"
#include <Logger.h>

using namespace std;
using namespace GSM;
using namespace SMS;




ostream& SMS::operator<<(ostream& os, CPMessage::MessageType val)
{
	switch(val) {
		case CPMessage::DATA: os<<"CP-DATA"; break;
		case CPMessage::ACK: os<<"CP-ACK"; break;
		case CPMessage::ERROR: os<<"CP-ERROR"; break;
		default :
			os<<hex<<"0x"<<(int)val<<dec; break;
	}
	return os;
}


CPMessage * SMS::CPFactory(CPMessage::MessageType val)
{
	switch(val) {
		case CPMessage::DATA: return new CPData();
		case CPMessage::ACK: return new CPAck();
		case CPMessage::ERROR: return new CPError();
		default: {
			LOG(NOTICE) << "no factory support for MTI="<<val;
			return NULL;
		}
	}	
}



CPMessage * SMS::parseSMS( const GSM::L3Frame& frame )
{
	CPMessage::MessageType MTI = (CPMessage::MessageType)(frame.MTI());	
	LOG(DEBUG) << "MTI="<<MTI;
	
	CPMessage * retVal = CPFactory(MTI);
	if( retVal==NULL ) return NULL;
	retVal->TI(frame.TI());
	retVal->parse(frame);
	LOG(DEBUG) << *retVal;
	return retVal;
}


RPData *SMS::hex2rpdata(const char *hexstring)
{
	RPData *rp_data = NULL;

	BitVector RPDUbits(strlen(hexstring)*4);
	if (!RPDUbits.unhex(hexstring)) {
		return false;
	}
	LOG(DEBUG) << "SMS RPDU bits: " << RPDUbits;

	try {
		RLFrame RPDU(RPDUbits);
		LOG(DEBUG) << "SMS RPDU: " << RPDU;

		rp_data = new RPData();
		rp_data->parse(RPDU);
		LOG(DEBUG) << "SMS RP-DATA " << *rp_data;
	}
	catch (SMSReadError) {
		LOG(WARNING) << "SMS parsing failed (above L3)";
		// TODO:: send error back to the phone
		delete rp_data;
		rp_data = NULL;
	}
	catch (L3ReadError) {
		LOG(WARNING) << "SMS parsing failed (in L3)";
		// TODO:: send error back to the phone
		delete rp_data;
		rp_data = NULL;
	}

	return rp_data;
}

TLMessage *SMS::parseTPDU(const TLFrame& TPDU)
{
	LOG(DEBUG) << "SMS: parseTPDU MTI=" << TPDU.MTI();
	// Handle just the uplink cases.
	switch ((TLMessage::MessageType)TPDU.MTI()) {
		case TLMessage::DELIVER_REPORT:
		case TLMessage::STATUS_REPORT:
			// FIXME -- Not implemented yet.
			LOG(WARNING) << "Unsupported TPDU type: " << (TLMessage::MessageType)TPDU.MTI();
			return NULL;
		case TLMessage::SUBMIT: {
			TLSubmit *submit = new TLSubmit;
			submit->parse(TPDU);
			LOG(INFO) << "SMS SMS-SUBMIT " << *submit;
			return submit;
		}
		default:
			return NULL;
	}
}

void CPMessage::text(ostream& os) const 
{
	os << (CPMessage::MessageType)MTI();
	os <<" TI=" << mTI;
}



void CPMessage::write(L3Frame& dest) const
{
	// We override L3Message::write for the transaction identifier.
	dest.resize(bitsNeeded());
	size_t wp = 0;
	// Note that 1/2-octet fields are reversed relative to Table 7.1.
	dest.writeField(wp,mTI,4);
	dest.writeField(wp,PD(),4);
	dest.writeField(wp,MTI(),8);
	writeBody(dest, wp);
}


void CPData::parseBody( const L3Frame& src, size_t &rp )
{	
	mData.parseLV(src,rp);
}

void CPData::writeBody( L3Frame& dest, size_t &wp ) const
{
	mData.writeLV(dest,wp);
} 

void CPData::text(ostream& os) const
{
	CPMessage::text(os);
	os << " RPDU=(" << mData << ")";
}

void CPError::writeBody( L3Frame& dest, size_t &wp ) const
{
	mCause.writeV(dest,wp);
}


void CPUserData::parseV(const L3Frame& src, size_t &rp, size_t expectedLength)
{
	unsigned numBits = expectedLength*8;
	mRPDU.resize(numBits);
	src.segmentCopyTo(mRPDU,rp,numBits);
	rp += numBits;
}

void CPUserData::writeV(L3Frame& dest, size_t &wp) const
{
	unsigned numBits = mRPDU.size();
	mRPDU.copyToSegment(dest,wp,numBits);
	wp += numBits;
}




ostream& SMS::operator<<(ostream& os, RPMessage::MessageType val)
{
	switch(val) {
		case RPMessage::Data: os<<"RP-DATA"; break;
		case RPMessage::Ack: os<<"RP-ACK"; break;
		case RPMessage::Error: os<<"RP-ERROR"; break;
		case RPMessage::SMMA: os<<"RP-SMMA"; break;
		default :
			os<<hex<<"0x"<<(int)val<<dec; break;
	}
	return os;
}



ostream& SMS::operator<<(ostream& os, const RPMessage& msg)
{
	msg.text(os);
	return os;
}



void RPUserData::parseV(const L3Frame& src, size_t &rp, size_t expectedLength)
{
	LOG(DEBUG) << "src=" << src << " (length=" << src.length() << ") rp=" << rp << " expectedLength=" << expectedLength;
	unsigned numBits = expectedLength*8;
	if (rp+numBits > src.size()) {
		SMS_READ_ERROR;
	}
	mTPDU.resize(numBits);
	LOG(DEBUG) << "mTPDU length=" << mTPDU.length() << "data=" << mTPDU;
	src.segmentCopyTo(mTPDU,rp,numBits);
	rp += numBits;
}

void RPUserData::writeV(L3Frame& dest, size_t &wp) const
{
	unsigned numBits = mTPDU.size();
	mTPDU.copyToSegment(dest,wp,numBits);
	wp += numBits;
}




void RPMessage::parse(const RLFrame& frame)
{
	size_t rp = 8;
	// FIXME -- A consistency check of PD and MTI would be good.
	mReference = frame.readField(rp,8);
	parseBody(frame,rp);
}


void RPMessage::write(RLFrame& dest) const
{
	// All relay-layer messages (GSM 04.11 7.3) have the same 2-byte header.
	dest.resize(bitsNeeded());
	size_t wp=0;
	dest.writeField(wp,0,5);
	// Note that we add one for the n->ms direction.
	// See GSM 04.11 8.2.2 Table 8.3
	dest.writeField(wp,MTI()+1,3);
	dest.writeField(wp,mReference,8);
	// After the header, fill in the body.
	writeBody(dest,wp);
}


void RPMessage::text(ostream& os) const
{
	os << MTI() << " ref=" << mReference;
}


void RPData::parseBody(const RLFrame& src, size_t &rp)
{
	// GSM 04.11 7.3.1.2
	mOriginator.parseLV(src,rp);
	mDestination.parseLV(src,rp);
	mUserData.parseLV(src,rp);
}


void RPData::writeBody(RLFrame& dest, size_t& wp) const
{
	// GSM 04.11 7.3.1.1
	// This is the downlink form.
	mOriginator.writeLV(dest,wp);
	mDestination.writeLV(dest,wp);
	mUserData.writeLV(dest,wp);
}


void RPData::text(ostream& os) const
{
	RPMessage::text(os);
	os << " origSMSC=(" << mOriginator << ")";
	os << " destSMSC=(" << mDestination << ")";
	os << " TPDU=(" << TPDU() << ")";
}



void RPError::writeBody(RLFrame& dest, size_t &wp) const
{
	mCause.writeLV(dest,wp);
}

void RPError::parseBody(const RLFrame& dest, size_t &wp)
{
	mCause.parseLV(dest,wp);
}

void RPError::text(ostream& os) const
{
	RPMessage::text(os);
	os << " cause=(" << mCause << ")";
}







ostream& SMS::operator<<(ostream& os, TLMessage::MessageType val)
{
	switch(val) {
		case TLMessage::DELIVER: os<<"SMS-DELIVER/REPORT"; break;
		case TLMessage::STATUS_REPORT: os<<"SMS-STATUS-REPORT/COMMAND"; break;
		case TLMessage::SUBMIT: os<<"SMS-SUBMIT/REPORT"; break;
		default :
			os<<hex<<"0x"<<(int)val<<dec; break;
	}
	return os;
}


ostream& SMS::operator<<(ostream& os, const TLMessage& msg)
{
	msg.text(os);
	return os;
}


ostream& SMS::operator<<(ostream& os, const TLElement& elem)
{
	elem.text(os);
	return os;
}




/** Parse a TL address field, including length. */
void TLAddress::parse(const TLFrame& src, size_t& rp)
{
	// GSM 03.40.
	// This is different from the BCD formats in GSM 04.08,
	// even though it looks very similar.
	// The difference is in the encoding of the length field.

	size_t numDigits = src.readField(rp,8);
	size_t length = numDigits/2 + (numDigits % 2);
	if (src.readField(rp, 1) != 1) SMS_READ_ERROR;	
	mType = (TypeOfNumber)src.readField(rp, 3);
	mPlan = (NumberingPlan)src.readField(rp, 4);
	mDigits.parse(src,rp,length);
}


void TLAddress::text(ostream& os) const
{
	os << "type=" << mType;
	os << " plan=" << mPlan;
	os << " digits=" << mDigits;
}



void TLAddress::write(TLFrame& dest, size_t& wp) const
{
	dest.writeField(wp,mDigits.size(),8);
	dest.writeField(wp, 0x01, 1);
	dest.writeField(wp, mType, 3);
	dest.writeField(wp, mPlan, 4);
	mDigits.write(dest,wp);
}




size_t TLValidityPeriod::length() const
{
	// GSM 03.40 9.2.3.3
	switch (mVPF) {
		case 0:	return 0;		// not present
		case 1: return 1;		// relative format, 9.2.3.12.1
		case 2: return 7;		// enhanced format, 9.2.3.12.2
		case 3: return 7;		// absolute format, 9.2.3.12.3
		default: assert(0);		// someone forgot to initialize the VPF
	}
}


void TLValidityPeriod::parse(const TLFrame& src, size_t& rp)
{
	// FIXME -- Check remaining message length before reading!!
	LOG(DEBUG) << "SMS: TLValidityPeriod::parse VPF=" << mVPF;
	switch (mVPF) {
		case 2: {
			// Relative format.
			// GSM 03.40 9.2.3.12.1
			unsigned vp = src.readField(rp,8);
			unsigned minutes = 0;
			if (vp<144) minutes = (vp+1)*5;
			else if (vp<168) minutes = 12*60 + (vp-143)*30;
			else if (vp<197) minutes = 24*60*(vp-166);
			else minutes = 7*24*60*(vp-192);
			mExpiration = Timeval();
			mExpiration.addMinutes(minutes);
			return;
		}
		case 3: {
			// Absolute format, borrowed from GSM 04.08 MM
			// GSM 03.40 9.2.3.12.2
			L3TimeZoneAndTime decoder;
			decoder.parseV((TLFrame)(BitVector)src,rp);
			mExpiration = decoder.time();
			return;
		}
		case 1:
			// Enhanced format.
			// GSM 03.40 9.2.3.12.3
			LOG(NOTICE) << "SMS: ignoring grossly complex \"enhanced\" TP-VP and assuming 1 week.";
			rp += 7;
			// fall through...
		case 0:
			// No validity period field.
			LOG(DEBUG) << "SMS: no validity period, assuming 1 week";
			mExpiration = Timeval(7*24*60*60*1000);
			return;
		default: assert(0);		// someone forgot to initialize the VPF
	}
}


void TLValidityPeriod::write(TLFrame& dest, size_t& wp) const
{
	if (mVPF==0) return;
	// We only support VPF==1.
	assert(mVPF==1);
	int seconds = mExpiration.seconds() - time(NULL);
	int minutes = seconds/60;
	if (minutes<1) minutes=1;
	unsigned vp;
	if (minutes<=720) vp = (minutes-1)/5;
	else if (minutes<1440) vp = 143 + (minutes-720)/30;
	else if (minutes<43200) vp = 166 + minutes/(24*60);
	else vp = 192 + minutes/(7*24*60);
	if (vp>255) vp=255;
	dest.writeField(wp,vp,8);
}



void TLValidityPeriod::text(ostream& os) const
{
	char str[27];
	time_t seconds = mExpiration.sec();
	ctime_r(&seconds,str);
	str[24]='\0';
	os << "expiration=(" << str << ")";
}

void TLUserData::encode7bit(const char *text)
{
	size_t wp = 0;
	
	// 1. Prepare.
	// Default alphabet (7-bit)
	mDCS = 0;
	// With 7-bit encoding TP-User-Data-Length count septets, i.e. just number
	// of characters.
	mLength = strlen(text);
	int bytes = (mLength*7+7)/8;
	int filler_bits = bytes*8-mLength*7;
	mRawData.resize(bytes*8);

	// 2. Write TP-UD
	// This tail() works because UD is always the last field in the PDU.
	BitVector chars = mRawData.tail(wp);
	for (unsigned i=0; i<mLength; i++) {
		char gsm = encodeGSMChar(text[i]);
		mRawData.writeFieldReversed(wp,gsm,7);
	}
	mRawData.writeField(wp,0,filler_bits);
}

std::string TLUserData::decode() const
{
	std::string text;

	switch (mDCS) {
		case 0:
		case 244:
		case 245:
		case 246:
		case 247:
		{
			// GSM 7-bit encoding, GSM 03.38 6.
			// Check bounds.
			if (mLength*7 > (mRawData.size())) {
				LOG(NOTICE) << "badly formatted TL-UD";
				SMS_READ_ERROR;
			}

			size_t crp = 0;
			unsigned text_length = mLength;

			// Skip User-Data-Header. We don't decode it here.
			// User-Data-Header handling is described in GSM 03.40 9.2.3.24
			// and is pictured in GSM 03.40 Figure 9.2.3.24 (a)
			if (mUDHI) {
				// Length-of-User-Data-Header
				unsigned udhl = mRawData.peekFieldReversed(crp,8);
				// Calculate UDH length in septets, including fill bits.
				unsigned udh_septets = (udhl*8 + 8 + 6) / 7;
				// Adjust actual text position and length.
				crp += udh_septets * 7;
				text_length -= udh_septets;
				LOG(DEBUG) << "UDHL(octets)=" << udhl
				           << " UDHL(septets)=" << udh_septets
				           << " pointer(bits)=" << crp
				           << " text_length(septets)=" << text_length;
			}

			// Do decoding
			text.resize(text_length);
			for (unsigned i=0; i<text_length; i++) {
				char gsm = mRawData.readFieldReversed(crp,7);
				text[i] = decodeGSMChar(gsm);
			}
			break;
		}

		default:
			LOG(NOTICE) << "unsupported DCS 0x" << mDCS;
			SMS_READ_ERROR;
			break;
	}

	return text;
}

size_t TLUserData::length() const
{
	// The reported value includes the length byte itself.
	// The length() method only needs to work for formats supported 
	// by the write() method.
	assert(mDCS<0x100);	// Someone forgot to initialize the DCS.
	size_t sum = 1;		// Start by counting the TP-User-Data-Length byte.
#if 1
	sum += (mRawData.size()+7)/8;
#else
	// The DCS is defined in GSM 03.38 4.
	if (mDCS==0) {
		// Default 7-bit alphabet
		// Return the number of octets needed for encoding.
		unsigned bits = strlen(mData) * 7;
		unsigned octets = bits/8;
		if (bits%8) octets += 1;
		sum += octets;
	} else {
		LOG(WARNING) << "unsupported SMS DCS 0x" << hex << mDCS;
		// It's OK to abort here.  This method is only used for encoding.
		// So we should never end up here.
		assert(0);	// We don't support this DCS.
	}
#endif
	return sum;
}



void TLUserData::parse(const TLFrame& src, size_t& rp)
{
	// The DCS is defined in GSM 03.38 4.
	assert(mDCS<0x100);	// Someone forgot to initialize the DCS.
	// TP-User-Data-Length
	mLength = src.readField(rp,8);
#if 1
	// This tail() works because UD is always the last field in the PDU.
	mRawData.clone(src.tail(rp));
	// Should we do this here?
	mRawData.LSB8MSB();
#else
	assert(!mUDHI);		// We don't support user headers.
	switch (mDCS) {
		case 0:
		case 244:
		case 245:
		case 246:
		case 247:
		{
			// GSM 7-bit encoding, GSM 03.38 6.
			// Check bounds.
			if (numChar*7 > (src.size()-rp)) {
				LOG(NOTICE) << "badly formatted TL-UD";
				SMS_READ_ERROR;
			}
			BitVector chars(src.tail(rp));
			chars.LSB8MSB();
			size_t crp=0;
			for (unsigned i=0; i<numChar; i++) {
				char gsm = chars.readFieldReversed(crp,7);
				mData[i] = decodeGSMChar(gsm);
			}
			mData[numChar]='\0';
			if (crp%8) crp += 8 - crp%8;
			rp += crp;
			return;
		}
		default:
		{
			rp += numChar;
			sprintf(mData,"unsupported DCS 0x%x", mDCS);
			LOG(NOTICE) << mData;
			SMS_READ_ERROR;
		}
	}
#endif
}


void TLUserData::write(TLFrame& dest, size_t& wp) const
{
#if 1
	// First write TP-User-Data-Length
	dest.writeField(wp,mLength,8);

	// Then write TP-User-Data
	// This tail() works because UD is always the last field in the PDU.
	BitVector ud_dest = dest.tail(wp);
	mRawData.copyTo(ud_dest);
	ud_dest.LSB8MSB();
#else
	// Stuff we don't support...
	assert(!mUDHI);
	assert(mDCS==0);
	unsigned numChar = strlen(mData);
	dest.writeField(wp,numChar,8);
	// This tail() works because UD is always the last field in the PDU.
	BitVector chars = dest.tail(wp);
	chars.zero();
	for (unsigned i=0; i<numChar; i++) {
		char gsm = encodeGSMChar(mData[i]);
		dest.writeFieldReversed(wp,gsm,7);
	}
	chars.LSB8MSB();
#endif
}



void TLUserData::text(ostream& os) const
{
	os << "DCS=" << mDCS;
	os << " UDHI=" << mUDHI;
	os << " UDLength=" << mLength;
	os << " UD=("; mRawData.hex(os); os << ")";
}


void TLMessage::parse(const TLFrame& src)
{
	// FIXME -- Check MTI for consistency.
	size_t rp=8;
	return parseBody(src,rp);
}


void TLMessage::write(TLFrame& dest) const
{
	dest.resize(bitsNeeded());
	size_t wp=8;
	writeMTI(dest);
	writeBody(dest,wp);
}



size_t TLSubmit::l2BodyLength() const
{
	return 1 + mDA.length() + 1 + 1 + mVP.length() + mUD.length();
}


void TLSubmit::parseBody(const TLFrame& src, size_t& rp)
{
	bool udhi;

	parseRD(src);
	parseVPF(src);
	parseRP(src);
	udhi = parseUDHI(src);
	parseSRR(src);
	mMR = src.readField(rp,8);
	mDA.parse(src,rp);
	mPI = src.readField(rp,8);
	mDCS = src.readField(rp,8);
	mVP.VPF(mVPF);
	mVP.parse(src,rp);
	mUD.DCS(mDCS);
	mUD.UDHI(udhi);
	mUD.parse(src,rp);
}


void TLSubmit::text(ostream& os) const
{
	TLMessage::text(os);
	os << " RD=" << mRD;
	os << " VPF=" << mVPF;
	os << " RP=" << mRP;
	os << " UDHI=" << mUD.UDHI();
	os << " SRR=" << mSRR;
	os << " MR=" << mMR;
	os << " DA=(" << mDA << ")";
	os << " PI=" << mPI;
	os << " DCS=" << mDCS;
	os << " VP=(" << mVP << ")";
	os << " UD=\"" << mUD << "\"";
}


size_t TLDeliver::l2BodyLength() const
{
	LOG(DEBUG) << "TLDEliver::l2BodyLength OA " << mOA.length() << " SCTS " << mSCTS.length() << " UD " << mUD.length();
	return mOA.length() + 1 + 1 + mSCTS.length() + mUD.length();
}


void TLDeliver::writeBody(TLFrame& dest, size_t& wp) const
{
	writeMMS(dest);
	writeRP(dest);
	writeUDHI(dest, mUD.UDHI());
	writeSRI(dest);
	mOA.write(dest,wp);
	dest.writeField(wp,mPID,8);
	dest.writeField(wp,mUD.DCS(),8);
	mSCTS.write(dest,wp);
	writeUnused(dest);
	mUD.write(dest,wp);
}


void TLDeliver::text(ostream& os) const
{
	TLMessage::text(os);
	os << " OA=(" << mOA << ")";
	os << " SCTS=(" << mSCTS << ")";
	os << " UD=(" << mUD << ")";
}



// vim: ts=4 sw=4
