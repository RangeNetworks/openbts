/*
* Copyright 2008 Free Software Foundation, Inc.
* Copyright 2011 Range Networks, Inc.
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


#include "GSMCommon.h"

using namespace GSM;
using namespace std;


const char* GSM::CallStateString(GSM::CallState state)
{
	switch (state) {
		case NullState: return "null";;
		case Paging: return "paging";
		case AnsweredPaging: return "answered-paging";
		case MOCInitiated: return "MOC-initiated";
		case MOCProceeding: return "MOC-proceeding";
		case MTCConfirmed: return "MTC-confirmed";
		case CallReceived: return "call-received";
		case CallPresent: return "call-present";
		case ConnectIndication: return "connect-indication";
		case Active: return "active";
		case DisconnectIndication: return "disconnect-indication";
		case ReleaseRequest: return "release-request";
		case SMSDelivering: return "SMS-delivery";
		case SMSSubmitting: return "SMS-submission";
		default: return NULL;
	}
}

ostream& GSM::operator<<(ostream& os, GSM::CallState state)
{
	const char* str = GSM::CallStateString(state);
	if (str) os << str;
	else os << "?" << state << "?";
	return os;
}


ostream& GSM::operator<<(ostream& os, L3PD val)
{
	switch (val) {
		case L3CallControlPD: os << "Call Control"; break;
		case L3MobilityManagementPD: os << "Mobility Management"; break;
		case L3RadioResourcePD: os << "Radio Resource"; break;
		default: os << hex << "0x" << (int)val << dec;
	}
	return os;
}


const BitVector GSM::gTrainingSequence[] = {
    BitVector("00100101110000100010010111"),
    BitVector("00101101110111100010110111"),
    BitVector("01000011101110100100001110"),
    BitVector("01000111101101000100011110"),
    BitVector("00011010111001000001101011"),
    BitVector("01001110101100000100111010"),
    BitVector("10100111110110001010011111"),
    BitVector("11101111000100101110111100"),
};

const BitVector GSM::gDummyBurst("0001111101101110110000010100100111000001001000100000001111100011100010111000101110001010111010010100011001100111001111010011111000100101111101010000");

const BitVector GSM::gRACHSynchSequence("01001011011111111001100110101010001111000");



unsigned char GSM::encodeGSMChar(unsigned char ascii)
{
	// Given an ASCII char, return the corresponding GSM char.
	// Do it with a lookup table, generated on the first call.
	// You might be tempted to replace this init with some more clever NULL-pointer trick.
	// -- Don't.  This is thread-safe.
	static char reverseTable[256]={'?'};
	static volatile bool init = false;
	if (!init) {
		for (size_t i=0; i<sizeof(gGSMAlphabet); i++) {
			reverseTable[(unsigned)gGSMAlphabet[i]]=i;
		}
		// Set the flag last to be thread-safe.
		init=true;
	}
	return reverseTable[(unsigned)ascii];
}


char GSM::encodeBCDChar(char ascii)
{
	// Given an ASCII char, return the corresponding BCD.
	if ((ascii>='0') && (ascii<='9')) return ascii-'0';
	switch (ascii) {
		case '.': return 11;
		case '*': return 11;
		case '#': return 12;
		case 'a': return 13;
		case 'b': return 14;
		case 'c': return 15;
		default: return 15;
	}
}




unsigned GSM::uplinkFreqKHz(GSMBand band, unsigned ARFCN)
{
	switch (band) {
		case GSM850:
			assert((ARFCN<252)&&(ARFCN>129));
			return 824200+200*(ARFCN-128);
		case EGSM900:
			if (ARFCN<=124) return 890000+200*ARFCN;
			assert((ARFCN>974)&&(ARFCN<1024));
			return 890000+200*(ARFCN-1024);
		case DCS1800:
			assert((ARFCN>511)&&(ARFCN<886));
			return 1710200+200*(ARFCN-512);
		case PCS1900:
			assert((ARFCN>511)&&(ARFCN<811));
			return 1850200+200*(ARFCN-512);
		default:
			assert(0);
	}
}


unsigned GSM::uplinkOffsetKHz(GSMBand band)
{
	switch (band) {
		case GSM850: return 45000;
		case EGSM900: return 45000;
		case DCS1800: return 95000;
		case PCS1900: return 80000;
		default: assert(0);
	}
}


unsigned GSM::downlinkFreqKHz(GSMBand band, unsigned ARFCN)
{
	return uplinkFreqKHz(band,ARFCN) + uplinkOffsetKHz(band);
}




// See GSM 04.08 Table 10.5.68.
const unsigned GSM::RACHSpreadSlots[16] =
{
	3,4,5,6,
	7,8,9,10,
	11,12,14,16,
	20,25,32,50
};

// See GSM 04.08 Table 3.1
const unsigned GSM::RACHWaitSParam[16] =
{
	55,76,109,163,217,
	55,76,109,163,217,
	55,76,109,163,217,
	55
};




int32_t GSM::FNDelta(int32_t v1, int32_t v2)
{
	static const int32_t halfModulus = gHyperframe/2;
	int32_t delta = v1-v2;
	if (delta>=halfModulus) delta -= gHyperframe;
	else if (delta<-halfModulus) delta += gHyperframe;
	return (int32_t) delta;
}

int GSM::FNCompare(int32_t v1, int32_t v2)
{
	int32_t delta = FNDelta(v1,v2);
	if (delta>0) return 1;
	if (delta<0) return -1;
	return 0;
}




ostream& GSM::operator<<(ostream& os, const Time& t)
{
	os << t.TN() << ":" << t.FN();
	return os;
}




void Clock::set(const Time& when)
{
	mLock.lock();
	mBaseTime = Timeval(0);
	mBaseFN = when.FN();
	mLock.unlock();
}


int32_t Clock::FN() const
{
	mLock.lock();
	Timeval now;
	int32_t deltaSec = now.sec() - mBaseTime.sec();
	int32_t deltaUSec = now.usec() - mBaseTime.usec();
	int64_t elapsedUSec = 1000000LL*deltaSec + deltaUSec;
	int64_t elapsedFrames = elapsedUSec / gFrameMicroseconds;
	int32_t currentFN = (mBaseFN + elapsedFrames) % gHyperframe;
	mLock.unlock();
	return currentFN;
}


void Clock::wait(const Time& when) const
{
	int32_t now = FN();
	int32_t target = when.FN();
	int32_t delta = FNDelta(target,now);
	if (delta<1) return;
	static const int32_t maxSleep = 51*26;
	if (delta>maxSleep) delta=maxSleep;
	sleepFrames(delta);
}







ostream& GSM::operator<<(ostream& os, TypeOfNumber type)
{
	switch (type) {
		case UnknownTypeOfNumber: os << "unknown"; break;
		case InternationalNumber: os << "international"; break;
		case NationalNumber: os << "national"; break;
		case NetworkSpecificNumber: os << "network-specific"; break;
		case ShortCodeNumber: os << "short code"; break;
		default: os << "?" << (int)type << "?";
	}
	return os;
}


ostream& GSM::operator<<(ostream& os, NumberingPlan plan)
{
	switch (plan) {
		case UnknownPlan: os << "unknown"; break;
		case E164Plan: os << "E.164/ISDN"; break;
		case X121Plan: os << "X.121/data"; break;
		case F69Plan: os << "F.69/Telex"; break;
		case NationalPlan: os << "national"; break;
		case PrivatePlan: os << "private"; break;
		default: os << "?" << (int)plan << "?";
	}
	return os;
}

ostream& GSM::operator<<(ostream& os, MobileIDType wID)
{
	switch (wID) {
		case NoIDType: os << "None"; break;
		case IMSIType: os << "IMSI"; break;
		case IMEIType: os << "IMEI"; break;
		case TMSIType: os << "TMSI"; break;
		case IMEISVType: os << "IMEISV"; break;
		default: os << "?" << (int)wID << "?";
	}
	return os;
}


ostream& GSM::operator<<(ostream& os, TypeAndOffset tao)
{
	switch (tao) {
		case TDMA_MISC: os << "(misc)"; break;
		case TCHF_0: os << "TCH/F"; break;
		case TCHH_0: os << "TCH/H-0"; break;
		case TCHH_1: os << "TCH/H-1"; break;
		case SDCCH_4_0: os << "SDCCH/4-0"; break;
		case SDCCH_4_1: os << "SDCCH/4-1"; break;
		case SDCCH_4_2: os << "SDCCH/4-2"; break;
		case SDCCH_4_3: os << "SDCCH/4-3"; break;
		case SDCCH_8_0: os << "SDCCH/8-0"; break;
		case SDCCH_8_1: os << "SDCCH/8-1"; break;
		case SDCCH_8_2: os << "SDCCH/8-2"; break;
		case SDCCH_8_3: os << "SDCCH/8-3"; break;
		case SDCCH_8_4: os << "SDCCH/8-4"; break;
		case SDCCH_8_5: os << "SDCCH/8-5"; break;
		case SDCCH_8_6: os << "SDCCH/8-6"; break;
		case SDCCH_8_7: os << "SDCCH/8-7"; break;
		case TDMA_BEACON: os << "(beacon)"; break;
		default: os << "?" << (int)tao << "?";
	}
	return os;
}

ostream& GSM::operator<<(ostream& os, ChannelType val)
{
	switch (val) {
		case UndefinedCHType: os << "undefined"; return os;
		case SCHType: os << "SCH"; break;
		case FCCHType: os << "FCCH"; break;
		case BCCHType: os << "BCCH"; break;
		case RACHType: os << "RACH"; break;
		case SDCCHType: os << "SDCCH"; break;
		case FACCHType: os << "FACCH"; break;
		case CCCHType: os << "CCCH"; break;
		case SACCHType: os << "SACCH"; break;
		case TCHFType: os << "TCH/F"; break;
		case TCHHType: os << "TCH/H"; break;
		case AnyTCHType: os << "any TCH"; break;
		case LoopbackFullType: os << "Loopback Full"; break;
		case LoopbackHalfType: os << "Loopback Half"; break;
		case AnyDCCHType: os << "any DCCH"; break;
		default: os << "?" << (int)val << "?";
	}
	return os;
}




bool Z100Timer::expired() const
{
	assert(mLimitTime!=0);
	// A non-active timer does not expire.
	if (!mActive) return false;
	return mEndTime.passed();
}

void Z100Timer::set()
{
	assert(mLimitTime!=0);
	mEndTime = Timeval(mLimitTime);
	mActive=true;
} 

void Z100Timer::expire()
{
	mEndTime = Timeval(0);
	mActive=true;
} 


void Z100Timer::set(long wLimitTime)
{
	mLimitTime = wLimitTime;
	set();
} 


long Z100Timer::remaining() const
{
	if (!mActive) return 0;
	long rem = mEndTime.remaining();
	if (rem<0) rem=0;
	return rem;
}

void Z100Timer::wait() const
{
	while (!expired()) msleep(remaining());
}

// vim: ts=4 sw=4
