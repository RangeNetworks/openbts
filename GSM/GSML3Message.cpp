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



#include "GSML3Message.h"
#include "GSML3RRMessages.h"
#include "GSML3MMMessages.h"
#include "GSML3CCMessages.h"
#include <Logger.h>


//#include <SMSTransfer.h>
#include <SMSMessages.h>
//using namespace SMS;


using namespace std;
using namespace GSM;



// FIXME -- We actually should not be using this anymore.
void L3Message::parse(const L3Frame& source)
{
	size_t rp = 16;
	parseBody(source,rp);
}


void L3Message::write(L3Frame& dest) const
{
	size_t l3len = bitsNeeded();
	if (dest.size()!=l3len) dest.resize(l3len);
	size_t wp = 0;
	// write the standard L3 header
	dest.writeField(wp,0,4);
	dest.writeField(wp,PD(),4);
	dest.writeField(wp,MTI(),8);
	// write the body
	writeBody(dest,wp);
	// set the L2 length or pseudolength
	dest.L2Length(L2Length());
}


L3Frame* L3Message::frame( Primitive prim ) const
{
	L3Frame *newFrame = new L3Frame(prim, bitsNeeded());
	write(*newFrame);
	return newFrame;
}



void L3Message::text(ostream& os) const
{
	os << "PD=" << PD();
	os << " MTI=" << MTI();
}






size_t GSM::skipLV(const L3Frame& source, size_t& rp)
{
	if (rp==source.size()) return 0;
	size_t base = rp;
	size_t length = 8 * source.readField(rp,8);
	rp += length;
	return rp-base;
}


size_t GSM::skipTLV(unsigned IEI, const L3Frame& source, size_t& rp)
{
	if (rp==source.size()) return 0;
	size_t base = rp;
	unsigned thisIEI = source.peekField(rp,8);
	if (thisIEI != IEI) return 0;
	rp += 8;
	size_t length = 8 * source.readField(rp,8);
	rp += length;
	return rp-base;
}


size_t GSM::skipTV(unsigned IEI, size_t numBits, const L3Frame& source, size_t& rp)
{
	if (rp==source.size()) return 0;
	size_t base = rp;
	size_t IEISize;
	if (numBits>4) IEISize=8;
	else IEISize=4;
	unsigned thisIEI = source.peekField(rp,IEISize);
	if (thisIEI != IEI) return 0;
	rp += IEISize;
	rp += numBits;
	return rp-base;
}



ostream& GSM::operator<<(ostream& os, const L3Message& msg)
{
	msg.text(os);
	return os;
}








GSM::L3Message* GSM::parseL3(const GSM::L3Frame& source)
{
	if (source.size()==0) return NULL;

	LOG(DEBUG) << "GSM::parseL3 "<< source;
	L3PD PD = source.PD();
	
	L3Message *retVal = NULL;
	try {
		switch (PD) {
			case L3RadioResourcePD: retVal=parseL3RR(source); break;
			case L3MobilityManagementPD: retVal=parseL3MM(source); break;
			case L3CallControlPD: retVal=parseL3CC(source); break;
			case L3SMSPD: retVal=SMS::parseSMS(source); break;
			default:
				LOG(NOTICE) << "L3 parsing failed for unsupported protocol " << PD;
				return NULL;
		}
	}
	catch (L3ReadError) {
		LOG(NOTICE) << "L3 parsing failed for " << source;
		return NULL;
	}

	if (retVal) LOG(INFO) << "L3 recv " << *retVal;
	return retVal;
}





void L3ProtocolElement::parseLV(const L3Frame& source, size_t &rp)
{
	size_t expectedLength = source.readField(rp,8);
	if (expectedLength==0) return;
	size_t rpEnd = rp + 8*expectedLength;
	parseV(source, rp, expectedLength);
	if (rpEnd != rp) {
		LOG(NOTICE) << "LV element does not match expected length";
		L3_READ_ERROR;
	}
}


bool L3ProtocolElement::parseTV(unsigned IEI, const L3Frame& source, size_t &rp)
{
	if (rp==source.size()) return false;
	if (lengthV()==0) {
		unsigned thisIEI = source.peekField(rp,4);
		if (thisIEI!=IEI) return false;
		rp += 4;
		parseV(source,rp);
		return true;
	}

	unsigned thisIEI = source.peekField(rp,8);
	if (thisIEI!=IEI) return false;
	rp += 8;
	parseV(source,rp);
	return true;
}



bool L3ProtocolElement::parseTLV(unsigned IEI, const L3Frame& source, size_t &rp)
{
	if (rp==source.size()) return false;
	unsigned thisIEI = source.peekField(rp,8);
	if (thisIEI!=IEI) return false;
	rp += 8;
	parseLV(source,rp);
	return true;
}




void L3ProtocolElement::writeLV(L3Frame& dest, size_t &wp) const
{
	unsigned len = lengthV();
	dest.writeField(wp, len, 8);
	if (len) writeV(dest, wp);
}

void L3ProtocolElement::writeTLV(unsigned IEI, L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,IEI,8);
	writeLV(dest,wp);
}

void L3ProtocolElement::writeTV(unsigned IEI, L3Frame& dest, size_t &wp) const
{
	if (lengthV()==0) {
		dest.writeField(wp,IEI,4);
		writeV(dest,wp);
		return;
	}
	dest.writeField(wp,IEI,8);
	writeV(dest,wp);
}


void L3ProtocolElement::skipExtendedOctets( const L3Frame& source, size_t &rp )
{
	if (rp==source.size()) return;
	int endbit = 0;
	while(!endbit){
		endbit = source.readField(rp, 1);
		rp += 7;
	}
}



ostream& GSM::operator<<(ostream& os, const L3ProtocolElement& elem)
{
	elem.text(os);
	return os;
}




// vim: ts=4 sw=4
