/** @file Messages for call independent Supplementary Service Control, GSM 04.80 2.2.  */

/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* Copyright 2011, 2014 Range Networks, Inc.

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



#include <iostream>
#include "GSML3SSMessages.h"
#include <L3SupServ.h>
#include <Logger.h>


using namespace std;
namespace GSM {

void L3SupServVersionIndicator::_define_vtable() {}	// Force the class vtable into this module.
void L3SupServFacilityIE::_define_vtable() {}	// Force the class vtable into this module.

ostream& operator<<(ostream& os, const L3SupServVersionIndicator& ie)
{
	ie.text(os);
	return os;
}

ostream& operator<<(ostream& os, const L3SupServFacilityIE& ie)
{
	ie.text(os);
	return os;
}

ostream& operator<<(ostream& os, const L3SupServMessage& msg)
{
	msg.text(os);
	return os;
}

ostream& operator<<(ostream& os, const L3SupServMessage* msg)
{
	if (msg == NULL) os << "(null SS message)";
	else msg->text(os);
	return os;
}

ostream& operator<<(ostream& os, L3SupServMessage::MessageType val)
{
	switch (val) {
		case L3SupServMessage::ReleaseComplete: 
			os << "ReleaseComplete"; break;
		case L3SupServMessage::Facility:
			os << "Facility"; break;
		case L3SupServMessage::Register:
			os << "Register"; break;
		default: os << hex << "0x" << (int)val << dec;
	}
	return os;
}

void L3OneByteProtocolElement::parseV(const L3Frame&src, size_t&rp, size_t expectedLength)
{
	if (expectedLength != 1) { LOG(ERR) << "Unexpected length="<<expectedLength <<" in one byte protocol element"; }	// Not much of a message.
	if (expectedLength) { parseV(src,rp); }
}

#if 0
// Warning: If the expectedLenght is 0 then parseLV does not call us, so mExtant would be false,
// but it does not matter anywhere because the Facility mComponentSize is inited to 0 and everything works out.
void L3SupServFacilityIE::parseV(const L3Frame&src, size_t&rp, size_t expectedLength)
{
	assert(expectedLength < 256);
	mExtant = true;
	mComponentSize = expectedLength;		// 0 is allowed.
	for (unsigned i = 0; i < expectedLength; i++) {
		mComponents[i] = src.readField(rp,8);
	}
}

void L3SupServFacilityIE::writeV(L3Frame&dest, size_t&wp) const
{
	for (unsigned i = 0; i < mComponentSize; i++) {
		dest.writeField(wp,mComponents[i],8);
	}
}
#endif

void L3SupServFacilityIE::text(ostream& os) const
{
	char rawdata[2*255+1+1];		// Add 1 extra for luck.
	unsigned len = lengthV();
	const unsigned char *components = peData();
	for (unsigned i = 0; i < len; i++) {
		sprintf(&rawdata[2*i],"%02x",components[i]);
	}
	string ussd = Control::ssMap2Ussd(components,len);
	os <<"SupServFacilityIE(size=" <<len <<" components:" <<LOGVAR(rawdata) <<LOGVAR(ussd)<<")";
}

L3SupServMessage * L3SupServFactory(L3SupServMessage::MessageType MTI)
{
	LOG(DEBUG) << "Factory MTI"<< (int)MTI;
	switch (MTI) {
		case L3SupServMessage::Facility: return new L3SupServFacilityMessage();
		case L3SupServMessage::Register: return new L3SupServRegisterMessage();
		case L3SupServMessage::ReleaseComplete: return new L3SupServReleaseCompleteMessage();
		default: {
			//LOG(NOTICE) << "no L3 NonCallSS factory support for message "<< MTI;
			return NULL;
		}
	}
}

L3SupServMessage * parseL3SupServ(const L3Frame& source)
{
	// mask out bit #7 (1011 1111) so use 0xbf
	L3SupServMessage::MessageType MTI = (L3SupServMessage::MessageType)(0xbf & source.MTI());
	LOG(DEBUG) << "MTI= "<< (int)MTI;
	L3SupServMessage *retVal = L3SupServFactory(MTI);
	if (retVal==NULL) return NULL;
	retVal->setTI(source.TI());
	retVal->parse(source);
	LOG(DEBUG) << "parse L3 SS Message" << *retVal;
	return retVal;
}

void L3SupServMessage::write(L3Frame& dest) const
{
	// We override L3Message::write for the transaction identifier.
	size_t l3len = bitsNeeded();
	if (dest.size()!=l3len) dest.resize(l3len);
	size_t wp = 0;
	dest.writeField(wp,mTI,4);
	dest.writeField(wp,PD(),4);
	dest.writeField(wp,MTI(),8);
	writeBody(dest,wp);
}

void L3SupServMessage::text(ostream& os) const
{
	os << " MTI = " <<(MessageType) MTI();
	os << " TI = " << mTI;
}

void L3SupServFacilityMessage::writeBody( L3Frame &dest, size_t &wp ) const
{
	mFacility.writeLV(dest,wp);
}

void L3SupServFacilityMessage::parseBody( const L3Frame &src, size_t &rp )
{
	mFacility.parseLV(src,rp);
}

void L3SupServFacilityMessage::text(ostream& os) const
{
	os <<"L3SSFacility(";
		L3SupServMessage::text(os);
		os << " facility=(" << mFacility << ")";	// Dump the facility IE from inside the layer3 facility message.
	os << ")";
}

void L3SupServRegisterMessage::writeBody( L3Frame &dest, size_t &wp ) const
{
	mFacility.writeTLV(0x1c,dest,wp);		// Facility IE is mandatory, but it is permitted to be empty.
	// The network to MS direction does not have a version indicator.
	devassert(haveVersionIndicator() == false);
	// However, we are going to write the message anyway in case of bugs
	// where messages are converted to L3Frame and back.
	if (haveVersionIndicator()) {
		mVersionIndicator.writeTLV(0x7f,dest,wp);
		//dest.writeField(wp,0x7F,8);		// The SS Version indicator IEI.
		//dest.writeField(wp,1,8);		// Extreme dopeyness - it is a one byte field with a length specified.
		//dest.writeField(wp,mVersionIndicator.mValue,8);
	}
}

void L3SupServRegisterMessage::parseBody( const L3Frame &src, size_t &rp )
{
	bool haveFacility = mFacility.parseTLV(0x1c,src,rp);
	if (! haveFacility) {
		LOG(ERR) << "Register message missing Facility IE";
		// The L3SupServFacilityIE is inited with a content size of 0, so we need no further action.
	}
	mVersionIndicator.parseTLV(0x7f,src,rp);
	//mHaveVersionIndicator = parseHasT(0x7f,src,rp);
	//if (mHaveVersionIndicator) {
	//	rp += 16;
	//	mVersionIndicator = source.readField(rp,8);
	//}
}

size_t L3SupServRegisterMessage::l2BodyLength() const 
{	
	size_t sum=0;
	sum += mFacility.lengthTLV();
	if (haveVersionIndicator()) sum += 3;
	return sum;
}

void L3SupServRegisterMessage::text(ostream& os) const
{
	os <<"L3SSRegister(";
		L3SupServMessage::text(os);
		os << " facility=(" << mFacility << ")";
		if (haveVersionIndicator()) os << " version=" << mVersionIndicator.mValue;
	os <<")";
}

void L3SupServReleaseCompleteMessage::writeBody( L3Frame &dest, size_t &wp ) const
{
	if (haveFacility()) mFacility.writeTLV(0x1c,dest,wp);
	if (mHaveCause) mCause.writeTLV(0x08,dest,wp);
}

void L3SupServReleaseCompleteMessage::parseBody( const L3Frame &src, size_t &rp )
{
	mHaveCause = mCause.parseTLV(0x08,src,rp);
	mFacility.parseTLV(0x1c,src,rp);
}

size_t L3SupServReleaseCompleteMessage::l2BodyLength() const 
{	
	size_t sum=0;
	if (mHaveCause) sum += mCause.lengthTLV();
	if (haveFacility()) sum += mFacility.lengthTLV();
	return sum;
}

void L3SupServReleaseCompleteMessage::text(ostream& os) const
{
	L3SupServMessage::text(os);
	if(haveFacility()) os << " facility=(" << mFacility << ")";
	if(mHaveCause) os << " cause = " << mCause;
}

};
