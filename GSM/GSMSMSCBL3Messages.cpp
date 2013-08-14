/*
* Copyright 2010 Kestrel Signal Processing, Inc.
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



#include "GSMSMSCBL3Messages.h"
#include <iomanip>

using namespace GSM;
using namespace std;


void L3SMSCBSerialNumber::writeV(L3Frame& l3, size_t& wp) const
{
	l3.writeField(wp,mGS,2);
	l3.writeField(wp,mMessageCode,10);
	l3.writeField(wp,mUpdateNumber,4);
}

void L3SMSCBSerialNumber::text(ostream& os) const
{
	os << "GS=" << mGS;
	os << " MessageCode=" << mMessageCode;
	os << " UpdateNumber=" << mUpdateNumber;
}


void L3SMSCBMessageIdentifier::writeV(L3Frame& l3, size_t& wp) const
{
	l3.writeField(wp,mValue,16);
}

void L3SMSCBMessageIdentifier::text(ostream& os) const
{
	os << hex << "0x" << mValue << dec;
}


void L3SMSCBDataCodingScheme::writeV(L3Frame& l3, size_t& wp) const
{
	l3.writeField(wp,mValue,8);
}

void L3SMSCBDataCodingScheme::text(ostream& os) const
{
	os << hex << "0x" << mValue << dec;
}


void L3SMSCBPageParameter::writeV(L3Frame& l3, size_t& wp) const
{
	l3.writeField(wp,mNumber,4);
	l3.writeField(wp,mTotal,4);
}

void L3SMSCBPageParameter::text(ostream& os) const
{
	os << mNumber << "/" << mTotal;
}

void L3SMSCBContent::writeV(L3Frame& l3, size_t& wp) const
{
	for (unsigned i=0; i<82; i++) l3.writeField(wp,mData[i],8);
}

void L3SMSCBContent::text(ostream& os) const
{
	os << hex;
	for (unsigned i=0; i<82; i++) os << setw(2) << (int)mData[i];
	os << dec;
}



ostream& GSM::operator<<(ostream& os, const L3SMSCBMessage& msg)
{
	msg.text(os);
	return os;
}


void L3SMSCBMessage::write(L3Frame& frame) const
{
	size_t wp=0;
	mSerialNumber.writeV(frame,wp);
	mMessageIdentifier.writeV(frame,wp);
	mDataCodingScheme.writeV(frame,wp);
	mPageParameter.writeV(frame,wp);
	mContent.writeV(frame,wp);
}

void L3SMSCBMessage::text(ostream& os) const
{
	os << "serialNumber=(" << mSerialNumber << ")";
	os << " messageID=" << mMessageIdentifier;
	os << " DCS=" << mDataCodingScheme;
	os << " page=" << mPageParameter;
	os << " content=(" << mContent << ")";
}


// vim: ts=4 sw=4
