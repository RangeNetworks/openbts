/**@file
	@brief Elements for Mobility Management messages, GSM 04.08 9.2.
*/
/*
* Copyright 2008, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
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




#include <time.h>
#include "GSML3MMElements.h"
#include <Logger.h>


using namespace std;
using namespace GSM;


void L3CMServiceType::parseV(const L3Frame& src, size_t &rp)
{
	mType = (TypeCode)src.readField(rp,4);
}


ostream& GSM::operator<<(ostream& os, L3CMServiceType::TypeCode code)
{
	switch (code) {
		case L3CMServiceType::MobileOriginatedCall: os << "MOC"; break;
		case L3CMServiceType::EmergencyCall: os << "Emergency"; break;
		case L3CMServiceType::ShortMessage: os << "SMS"; break;
		case L3CMServiceType::SupplementaryService: os << "SS"; break;
		case L3CMServiceType::VoiceCallGroup: os << "VGCS"; break;
		case L3CMServiceType::VoiceBroadcast: os << "VBS"; break;
		case L3CMServiceType::LocationService: os << "LCS"; break;
		case L3CMServiceType::MobileTerminatedCall: os << "MTC"; break;
		case L3CMServiceType::MobileTerminatedShortMessage: os << "MTSMS"; break;
		case L3CMServiceType::TestCall: os << "Test"; break;
		default: os << "?" << (int)code << "?";
	}
	return os;
}

void L3CMServiceType::text(ostream& os) const
{
	os << mType;
}

void L3RejectCause::writeV( L3Frame& dest, size_t &wp ) const
{
	dest.writeField(wp, mRejectCause, 8);
}


void L3RejectCause::text(ostream& os) const
{	
	os <<"0x"<< hex << mRejectCause << dec;	
}





void L3NetworkName::writeV(L3Frame& dest, size_t &wp) const
{
	unsigned sz = strlen(mName);
	// header byte
	if (mAlphabet == ALPHABET_UCS2) {
		// Ext: 1b, coding scheme: 001b (UCS2), CI, trailing spare bits: 000b (0)
		dest.writeField(wp,1,1);	// ext bit
		dest.writeField(wp,1,3);	// coding scheme USC2
		dest.writeField(wp,mCI,1);	// show country name?
		dest.writeField(wp,0,3);	// spare bits in last octet
		// the characters
		for (unsigned i=0; i<sz; i++) {
			dest.writeField(wp,mName[i],16);
		}
	} else {
		// Ext: 1b, coding scheme: 000b (GSM 03.38 coding scheme),
		int nameBits = sz*7;
		int nameBytes = nameBits/8;
		if (nameBits%8) nameBytes++;
		int msgBits = nameBytes*8;
		int spareBits = msgBits-nameBits;
		dest.writeField(wp,1,1);				// ext bit
		dest.writeField(wp,0,3);				// coding scheme USC2
		dest.writeField(wp,mCI,1);				// show country name?
		dest.writeField(wp,spareBits,3);		// spare bits in last octet
		// Temporary vector "chars" so we can do LSB8MSB() after encoding.
		BitVector chars(dest.segment(wp,msgBits));
		size_t twp = 0;
		// the characters: 7 bit, GSM 03.38 6.1.2.2, 6.2.1
		for (unsigned i=0; i<sz; i++) {
			chars.writeFieldReversed(twp,encodeGSMChar(mName[i]),7);
		}
		chars.writeField(twp,0,spareBits);
		chars.LSB8MSB();
		wp += twp;
	}
}


void L3NetworkName::text(std::ostream& os) const
{
	os << mName;
}


void L3TimeZoneAndTime::writeV(L3Frame& dest, size_t& wp) const
{
	// See GSM 03.40 9.2.3.11.

	// Convert from seconds since Jan 1, 1970 to calendar time.
	struct tm fields;
	const time_t seconds = mTime.sec();
	if (mType == LOCAL_TIME) {
		localtime_r(&seconds,&fields);
	} else {
		gmtime_r(&seconds,&fields);
	}
	// Write the fields in BCD format.
	// year
	unsigned year = fields.tm_year % 100;
	dest.writeField(wp, year % 10, 4);
	dest.writeField(wp, year / 10, 4);
	// month
	unsigned month = fields.tm_mon + 1;
	dest.writeField(wp, month % 10, 4);
	dest.writeField(wp, month / 10, 4);
	// day
	dest.writeField(wp, fields.tm_mday % 10, 4);
	dest.writeField(wp, fields.tm_mday / 10, 4);
	// hour
	dest.writeField(wp, fields.tm_hour % 10, 4);
	dest.writeField(wp, fields.tm_hour / 10, 4);
	// minute
	dest.writeField(wp, fields.tm_min % 10, 4);
	dest.writeField(wp, fields.tm_min / 10, 4);
	// second
	dest.writeField(wp, fields.tm_sec % 10, 4);
	dest.writeField(wp, fields.tm_sec / 10, 4);

	// time zone, in 1/4 steps with a sign bit
	int zone;
	if (mType == LOCAL_TIME) {
		zone = fields.tm_gmtoff;
	} else {
		// At least under Linux gmtime_r() does not return timezone
		// information for some reason and we have to use localtime_r()
		// to reptrieve this information.
		struct tm fields_local;
		localtime_r(&seconds,&fields_local);
		zone = fields_local.tm_gmtoff;
	}
	zone = zone / (15*60);
	unsigned zoneSign = (zone < 0);
	zone = abs(zone);
	dest.writeField(wp, zone % 10, 4);
	dest.writeField(wp, zoneSign, 1);
	dest.writeField(wp, zone / 10, 3);

	LOG(DEBUG) << "year=" << year << " month=" << month << " day=" << fields.tm_mday
	           << " hour=" << fields.tm_hour << " min=" << fields.tm_min << " sec=" << fields.tm_sec
	           << " zone=" << (zoneSign?"-":"+") << zone;
}
	
	
void L3TimeZoneAndTime::parseV(const L3Frame& src, size_t& rp)
{
	// See GSM 03.40 9.2.3.11.

	// Read it all into a localtime struct tm,
	// then covert to Unix seconds.
	struct tm fields;
	// year
	fields.tm_year = 2000 + src.readField(rp,4) + src.readField(rp,4)*10;
	// month
	fields.tm_mon = 1 + src.readField(rp,4) + src.readField(rp,4)*10;
	// day
	fields.tm_mday = src.readField(rp,4) + src.readField(rp,4)*10;
	// hour
	fields.tm_hour = src.readField(rp,4) + src.readField(rp,4)*10;
	// minute
	fields.tm_min = src.readField(rp,4) + src.readField(rp,4)*10;
	// second
	fields.tm_sec = src.readField(rp,4) + src.readField(rp,4)*10;
	// zone
	unsigned zone = src.readField(rp,4);
	unsigned zoneSign = src.readField(rp,1);
	zone += 10*src.readField(rp,4);
	if (zoneSign) zone = -zone;
	fields.tm_gmtoff = zone * 15 * 60;
	// convert
	mTime = Timeval(timegm(&fields),0);
}

void L3TimeZoneAndTime::text(ostream& os) const
{
	char timeStr[26];
	const time_t seconds = mTime.sec();
	ctime_r(&seconds,timeStr);
	timeStr[24]='\0';
	os << timeStr << " (local)";
}



void L3RAND::writeV(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,mRUpper,64);
	dest.writeField(wp,mRLower,64);
}

void L3RAND::text(ostream& os) const
{
	os << hex << "0x" << mRUpper << mRLower << dec;
}

void L3SRES::parseV(const L3Frame& src, size_t &rp)
{
	mValue = src.readField(rp,32);
}

void L3SRES::text(ostream& os) const
{
	os << hex << "0x" << mValue << dec;
}


// vim: ts=4 sw=4
