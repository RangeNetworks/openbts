/**@file
	@brief Common elements for L3 messages, GSM 04.08 10.5.1.
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




#include "GSML3CommonElements.h"
#include <Logger.h>


using namespace std;
using namespace GSM;


L3LocationAreaIdentity::L3LocationAreaIdentity(const char*wMCC, const char* wMNC, unsigned wLAC)
	:L3ProtocolElement(),
	mLAC(wLAC)
{
	// country code
	assert(strlen(wMCC)==3);
	mMCC[0] = wMCC[0]-'0';
	mMCC[1] = wMCC[1]-'0';
	mMCC[2] = wMCC[2]-'0';
	// network code
	assert(strlen(wMNC)<=3);
	assert(strlen(wMNC)>=2);
	mMNC[0] = wMNC[0]-'0';
	mMNC[1] = wMNC[1]-'0';
	if (wMNC[2]!='\0') mMNC[2]=wMNC[2]-'0';
	else mMNC[2]=0x0f;
}


void L3LocationAreaIdentity::writeV(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,mMCC[1],4);
	dest.writeField(wp,mMCC[0],4);
	dest.writeField(wp,mMNC[2],4);
	dest.writeField(wp,mMCC[2],4);
	dest.writeField(wp,mMNC[1],4);
	dest.writeField(wp,mMNC[0],4);
	dest.writeField(wp,mLAC,16);
}

void L3LocationAreaIdentity::parseV( const L3Frame& src, size_t &rp )
{
	mMCC[1] =  src.readField(rp, 4); 
	mMCC[0] =  src.readField(rp, 4);
	mMNC[2] =  src.readField(rp, 4);
	mMCC[2] =  src.readField(rp, 4); 
	mMNC[1] =  src.readField(rp, 4);
	mMNC[0] =  src.readField(rp, 4);
	mLAC = src.readField(rp, 16);
}


bool L3LocationAreaIdentity::operator==(const L3LocationAreaIdentity& other) const
{
	// MCC
	if (mMCC[0]!=other.mMCC[0]) return false;
	if (mMCC[1]!=other.mMCC[1]) return false;
	if (mMCC[2]!=other.mMCC[2]) return false;
	// MNC
	if (mMNC[0]!=other.mMNC[0]) return false;
	if (mMNC[1]!=other.mMNC[1]) return false;
	if (mMNC[2]!=other.mMNC[2]) return false;
	// LAC
	if (mLAC!=other.mLAC) return false;
	// So everything matched.
	return true;
}


void L3LocationAreaIdentity::text(ostream& os) const
{
	os << "MCC=" << mMCC[0] << mMCC[1] << mMCC[2];
	os << " MNC=" << mMNC[0] << mMNC[1];
	if (mMNC[2]<15) os << mMNC[2];
	os << " LAC=0x" << hex << mLAC << dec;
}

int L3LocationAreaIdentity::MNC() const
{
	int val = mMNC[0]*10 + mMNC[1];
	if (mMNC[2]<15) val = val*10 + mMNC[2];
	return val;
}





void L3CellIdentity::writeV(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,mID,16);
}


void L3CellIdentity::text(ostream& os) const
{
	os << mID;
}



bool L3MobileIdentity::operator==(const L3MobileIdentity& other) const
{
	if (other.mType!=mType) return false;
	if (mType==TMSIType) return (mTMSI==other.mTMSI);
	return (strcmp(mDigits,other.mDigits)==0);
}

bool L3MobileIdentity::operator<(const L3MobileIdentity& other) const
{
	if (other.mType != mType) return mType > other.mType;
	if (mType == TMSIType) return mTMSI > other.mTMSI;
	return strcmp(mDigits,other.mDigits)>0;
}


size_t L3MobileIdentity::lengthV() const
{
	if (mType==NoIDType) return 1;
	if (mType==TMSIType) return 5;
	return 1 + strlen(mDigits)/2;
}


void L3MobileIdentity::writeV(L3Frame& dest, size_t &wp) const
{
	// See GSM 04.08 10.5.1.4.

	if (mType==NoIDType) {
		dest.writeField(wp,0x0f0,8);
		return;
	}

	if (mType==TMSIType) {
		dest.writeField(wp,0x0f4,8);
		dest.writeField(wp,mTMSI,32);
		return;
	}

	int numDigits = strlen(mDigits);
	assert(numDigits<=15);

	// The first byte.
	dest.writeField(wp,mDigits[0]-'0',4);
	dest.writeField(wp,(numDigits%2),1);
	dest.writeField(wp,mType,3);

	// The other bytes are more regular.
	int i=1;
	while (i<numDigits) {
		if ((i+1)<numDigits) dest.writeField(wp,mDigits[i+1]-'0',4);
		else dest.writeField(wp,0x0f,4);
		dest.writeField(wp,mDigits[i]-'0',4);
		i+=2;
	}
}


void L3MobileIdentity::parseV( const L3Frame& src, size_t &rp, size_t expectedLength)
{
	// See GSM 04.08 10.5.1.4.

	size_t endCount = rp + expectedLength*8;

	// Read first digit, a special case.
	int numDigits = 0;
	mDigits[numDigits++] = src.readField(rp,4)+'0';
    
	// Get odd-count flag and identity type. 
	bool oddCount = (bool) src.readField(rp,1);
	mType = (MobileIDType) src.readField(rp,3);

	switch (mType) {
		case TMSIType:
			mDigits[0]='\0';
			// GSM 03.03 2.4 tells us the TMSI is always 32 bits
			mTMSI = src.readField(rp,32);
			break;
		case IMSIType:
		case IMEISVType:
		case IMEIType:
			while (rp<endCount) {
				unsigned tmp = src.readField(rp,4);
				mDigits[numDigits++] = src.readField(rp,4)+'0';
				mDigits[numDigits++] = tmp + '0';
				if (numDigits>15) L3_READ_ERROR;
			}
			if (!oddCount) numDigits--;
			mDigits[numDigits]='\0';
			break;
		default:
			LOG(NOTICE) << "non-standard identity type " << (int)mType;
			mDigits[0]='\0';
			mType = NoIDType;
	}
}

void L3MobileIdentity::text(ostream& os) const
{
	os << mType << "=";
	if (mType==TMSIType) {
		os << hex << "0x" << mTMSI << dec;
		return;
	}
	os << mDigits;
}



void L3MobileStationClassmark1::parseV(const L3Frame& src, size_t& rp)
{
	rp++;	// spare
	mRevisionLevel = src.readField(rp,2);
	mES_IND = src.readField(rp,1);
	mA5_1 = src.readField(rp,1);
	mRFPowerCapability = src.readField(rp,3);
}

void L3MobileStationClassmark1::text(ostream& os) const
{
	os << "revision=" << mRevisionLevel;
	os << " ES-IND=" << mES_IND;
	os << " A5/1=" << mA5_1;
	os << " powerCap=" << mRFPowerCapability;
}



void L3MobileStationClassmark2::parseV(const L3Frame& src, size_t& rp)
{
	rp++;	// spare
	mRevisionLevel = src.readField(rp,2);
	mES_IND = src.readField(rp,1);
	mA5_1 = src.readField(rp,1);
	mRFPowerCapability = src.readField(rp,3);
	rp++;	// spare
	mPSCapability = src.readField(rp,1);
	mSSScreenIndicator = src.readField(rp,2);
	mSMCapability = src.readField(rp,1);
	mVBS = src.readField(rp,1);
	mVGCS = src.readField(rp,1);
	mFC = src.readField(rp,1);
	mCM3 = src.readField(rp,1);
	rp++;	// spare
	mLCSVACapability = src.readField(rp,1);
	rp++;	// spare
	mSoLSA = src.readField(rp,1);
	mCMSF = src.readField(rp,1);
	mA5_3 = src.readField(rp,1);
	mA5_2 = src.readField(rp,1);
}

void L3MobileStationClassmark2::parseV(const L3Frame& src, size_t& rp, size_t len)
{
	// This is sometimes send as LV, with extra bytes.
	// FIXME -- Learn to decode those extra bytes.
	size_t frp = rp;
	parseV(src,frp);
	rp += len*8;
}

void L3MobileStationClassmark2::text(ostream& os) const
{
	os << "revision=" << mRevisionLevel;
	os << " ES-IND=" << mES_IND;
	os << " A5/1=" << mA5_1;
	os << " A5/2=" << mA5_2;
	os << " A5/3=" << mA5_3;
	os << " powerCap=" << mRFPowerCapability;
	os << " PS=" << mPSCapability;
	os << " SSScrenInd=" << mSSScreenIndicator;
	os << " SM=" << mSMCapability;
	os << " VBS=" << mVBS;
	os << " VGCS=" << mVGCS;
	os << " FC=" << mFC;
	os << " CM3=" << mCM3;
	os << " LCSVA=" << mLCSVACapability;
	os << " SoLSA=" << mSoLSA;
	os << " CMSF=" << mCMSF;
}



void L3MobileStationClassmark3::parseV(const L3Frame& src, size_t& rp)
{
	// FIXME -- This is incomplete.
	rp+=1;	// skip spare bits
	unsigned mMultiband = src.readField(rp,3);
	switch (mMultiband) {
		case 0:
		case 5: case 6:
		case 1: case 2: case 4:
			mA5_7 = src.readField(rp,1);
			mA5_6 = src.readField(rp,1);
			mA5_5 = src.readField(rp,1);
			mA5_4 = src.readField(rp,1);
			break;
		default:
			break;
	}
}

void L3MobileStationClassmark3::parseV(const L3Frame& src, size_t& rp, size_t len)
{
	// We do this because the parser is incomplete.
	size_t frp = rp;
	parseV(src,frp);
	rp += len*8;
}

void L3MobileStationClassmark3::text(ostream& os) const
{
	os << "multiband=" << mMultiband;
	os << " A5/4=" << mA5_4;
	os << " A5/5=" << mA5_4;
	os << " A5/6=" << mA5_4;
	os << " A5/7=" << mA5_4;
}




void L3CipheringKeySequenceNumber::writeV(L3Frame &dest, size_t& wp) const
{
	dest.writeField(wp,mCIValue,4);
}

void L3CipheringKeySequenceNumber::text(ostream& os) const
{
	os << mCIValue;
}



// vim: ts=4 sw=4
