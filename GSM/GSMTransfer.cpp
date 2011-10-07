/*
* Copyright 2008 Free Software Foundation, Inc.
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




#include <iostream>

#include "GSMTransfer.h"
#include "GSML3Message.h"


using namespace std;
using namespace GSM;


ostream& GSM::operator<<(ostream& os, const L2Frame& frame)
{
	os << "primitive=" << frame.primitive();
	os << " raw=(";
	frame.hex(os);
	os << ")";
	return os;
}


ostream& GSM::operator<<(ostream& os, const L3Frame& frame)
{
	os << "primitive=" << frame.primitive();
	os << " raw=(";
	frame.hex(os);
	os << ")";
	return os;
}


ostream& GSM::operator<<(ostream& os, const L2Header::FrameFormat val)
{
	switch (val) {
		case L2Header::FmtA: os << "A"; break;
		case L2Header::FmtB: os << "B"; break;
		case L2Header::FmtBbis: os << "Bbis"; break;
		case L2Header::FmtBter: os << "Bter"; break;
		case L2Header::FmtB4: os << "B4"; break;
		case L2Header::FmtC: os << "C"; break;
		default: os << "?" << (int)val << "?";
	}
	return os;
}



size_t N201(ChannelType wChanType, L2Header::FrameFormat wFormat)
{
	// Number of payload bytes in the L2Frame.
	// GSM 04.06 5.8.3
	switch (wFormat) {
		case L2Header::FmtA:
		case L2Header::FmtB:
			switch (wChanType) {
				case SACCHType: return 18;
				case FACCHType:
				case SDCCHType: return 20;
				default: abort();
			}
		case L2Header::FmtBbis:
			// We count L2 pseudolength as part of the header.
			switch (wChanType) {
				case BCCHType:
				case CCCHType: return 22;
				default: abort();
			}
		case L2Header::FmtBter:
			switch (wChanType) {
				case SACCHType: return 21;
				case FACCHType:
				case SDCCHType: return 23;
				default: abort();
			}
		case L2Header::FmtB4:
			switch (wChanType) {
				case SACCHType: return 19;
				default: abort();
			}
		default:
			abort();
	}
}







size_t L2Header::bitsNeeded() const
{
	// number of BITS to encode the header
	// GSM 04.06 2.1
	switch (mFormat) {
		case FmtA:
		case FmtB: return 3*8;
		case FmtBbis: return 8;
		case FmtBter: return 0;
		case FmtB4: return 2*8;
		case FmtC: return 0;
		default: abort();
	}
}



size_t L2Header::write(L2Frame& frame) const
{
	size_t wp=0;
	switch (mFormat) {
		case FmtA:
		case FmtB:
			mAddress.write(frame,wp);
			mControl.write(frame,wp);
			mLength.write(frame,wp);
			break;
		case FmtBbis:
			mLength.write(frame,wp);
			break;
		case FmtBter:
			break;
		case FmtB4:
			mAddress.write(frame,wp);
			mControl.write(frame,wp);
			break;
		case FmtC:
			break;
		default:
			assert(0);
	}
	return wp;
}






void L2Control::write(L2Frame& frame, size_t& wp) const
{
	// GSM 04.06 Table 3

	switch (mFormat) {
		case IFormat: {
			frame.writeField(wp,mNR,3);
			frame.writeField(wp,mPF,1);
			frame.writeField(wp,mNS,3);
			frame.writeField(wp,0,1);
			break;
		}
		case SFormat:{
			frame.writeField(wp,mNR,3);
			frame.writeField(wp,mPF,1);
			frame.writeField(wp,mSBits,2);
			frame.writeField(wp,1,2);
			break;
		}
		case UFormat:{
			const unsigned U1 = mUBits >> 2;
			const unsigned U2 = mUBits & 0x03;
			frame.writeField(wp,U1,3);
			frame.writeField(wp,mPF,1);
			frame.writeField(wp,U2,2);
			frame.writeField(wp,3,2);
			break;
		}
		default:
			abort();
	}
}

void L2Length::write(L2Frame& frame, size_t& wp) const
{
	// GSM 04.06 3.6
	assert(mL<64);
	frame.writeField(wp,mL,6);
	frame.writeField(wp,mM,1);
	frame.writeField(wp,1,1);
}




void L2Frame::idleFill()
{
	// GSM 04.06 2.2
	static const int pattern[8] = {0,0,1,0,1,0,1,1};
	for (size_t i=0; i<size(); i++) {
		mStart[i] = pattern[i%8];
	}
}


L2Frame::L2Frame(const BitVector& bits, Primitive prim)
	:BitVector(23*8),mPrimitive(prim)
{
	idleFill();
	assert(bits.size()<=this->size());
	bits.copyTo(*this);
}


L2Frame::L2Frame(const L2Header& header, const BitVector& l3)
	:BitVector(23*8),mPrimitive(DATA)
{
	idleFill();
	assert((header.bitsNeeded()+l3.size())<=this->size());
	size_t wp = header.write(*this);
	l3.copyToSegment(*this,wp);
}


L2Frame::L2Frame(const L2Header& header)
	:BitVector(23*8),mPrimitive(DATA)
{
	idleFill();
	header.write(*this);
}



unsigned L2Frame::SAPI() const
{
	// Get the service access point indicator,
	// assuming frame format A or B.
	// See GSM 04.06 2.1, 2.3, 3.2.
	// This assumes MSB-first field packing.
	return peekField(3,3);
}


unsigned L2Frame::LPD() const
{
	// This assumes MSB-first field packing.
	// See GSM 04.06 3.2, GSM  04.12 3.3.1.
	return peekField(1,2);
}


L2Control::ControlFormat L2Frame::controlFormat() const
{
	// Determine the LAPDm frame type.
	// See GSM 04.06 Table 3.
	// The control field is in the second octet.
	if (bit(8+7)==0) return L2Control::IFormat;
	if (bit(8+6)==0) return L2Control::SFormat;
	return L2Control::UFormat;
}

L2Control::FrameType L2Frame::UFrameType() const
{
	// Determine U-frame command type.
	// GSM 04.06 Table 4.
	// TODO -- This would be more efficient as an array.
	char upper = peekField(8+0,3);
	char lower = peekField(8+4,2);
	char uBits = upper<<2 | lower;
	switch (uBits) {
		case 0x07: return L2Control::SABMFrame;
		case 0x03: return L2Control::DMFrame;
		case 0x00: return L2Control::UIFrame;
		case 0x08: return L2Control::DISCFrame;
		case 0x0c: return L2Control::UAFrame;
		default: return L2Control::BogusFrame;
	}
}


L2Control::FrameType L2Frame::SFrameType() const
{
	// Determine S-frame command type.
	// GSM 04.06 Table 4.
	char sBits = peekField(8+4,2);
	static const L2Control::FrameType type[] = {
		L2Control::RRFrame, L2Control::RNRFrame,
		L2Control::REJFrame, L2Control::BogusFrame };
	return type[(int)sBits];
}






ostream& GSM::operator<<(ostream& os, const TxBurst& ts)
{
	os << "time=" << ts.time() << " data=(" << (const BitVector&)ts << ")" ;
	return os;
}

ostream& GSM::operator<<(ostream& os, const RxBurst& ts)
{
	os << "time=" << ts.time();
	os << " RSSI=" << ts.RSSI() << " timing=" << ts.timingError();
	os << " data=(" << (const SoftVector&)ts << ")" ;
	return os;
}




// We put this in the .cpp file to avoid a circular dependency.
TxBurst::TxBurst(const RxBurst& rx)
	:BitVector((const BitVector&)rx),mTime(rx.time())
{}

// We put this in the .cpp file to avoid a circular dependency.
RxBurst::RxBurst(const TxBurst& source, float wTimingError, int wRSSI)
	:SoftVector((const BitVector&) source),mTime(source.time()),
	mTimingError(wTimingError),mRSSI(wRSSI)
{ }



// Methods for the L2 address field.

void L2Address::write(L2Frame& frame, size_t& wp) const
{
	// GSM 04.06 3.2, 3.3
	frame.writeField(wp,0,1);		// spare bit
	frame.writeField(wp,mLPD,2);
	frame.writeField(wp,mSAPI,3);
	frame.writeField(wp,mCR,1);
	frame.writeField(wp,1,1);		// no extension
}





ostream& GSM::operator<<(ostream& os, const L2Address& address)
{
	os <<  "SAPI=" << address.SAPI() <<  " CR=" << address.CR();
	return os;
}


ostream& GSM::operator<<(ostream& os,L2Control::ControlFormat format)
{
	switch (format) {
		case L2Control::IFormat: os << "I"; break;
		case L2Control::SFormat: os << "S"; break;
		case L2Control::UFormat: os << "U"; break;
		default: os << "?" << (int)format << "?"; break;
	} 
	return os;
}


ostream& GSM::operator<<(ostream& os, const L2Control& control)
{
	os << "fmt=" << control.format() << " ";
	switch (control.format()) {
		case L2Control::IFormat:
			os << "NR=" << control.NR() << " PF=" << control.PF() << " NS=" << control.NS();
			break;
		case L2Control::SFormat:
			os << "NR=" << control.NR() << " PF=" << control.PF();
			os << hex << " Sbits=0x" << control.SBits() << dec;
			break;
		case L2Control::UFormat:
			os << "PF=" << control.PF();
			os << hex << " Ubits=0x" << control.UBits() << dec;
			break;
		default:
			os << "??";
	}
	return os;
}


ostream& GSM::operator<<(ostream& os, const L2Length& length)
{
	os << "L=" << length.L() << " M=" << length.M();
	return os;
}


ostream& GSM::operator<<(ostream& os, const L2Header& header)
{
	os << "fmt=" << header.format();
	os << " addr=(" << header.address() << ")";
	os << " cntrl=(" << header.control() << ")";
	os << " len=(" << header.length() << ")";
	return os;
}




ostream& GSM::operator<<(ostream& os, L2Control::FrameType cmd)
{
	switch (cmd) {
		case L2Control::UIFrame: os << "UI"; break;
		case L2Control::SABMFrame: os << "SABM"; break;
		case L2Control::UAFrame: os << "UA"; break;
		case L2Control::DMFrame: os << "DM"; break;
		case L2Control::DISCFrame: os << "DISC"; break;
		case L2Control::RRFrame: os << "RR"; break;
		case L2Control::RNRFrame: os << "RNR"; break;
		case L2Control::REJFrame: os << "REJ"; break;
		case L2Control::IFrame: os << "I"; break;
		default: os << "?" << (int)cmd << "?";
	}
	return os;
}


ostream& GSM::operator<<(ostream& os, Primitive prim)
{
	switch (prim) {
		case ESTABLISH: os << "ESTABLISH"; break;
		case RELEASE: os << "RELEASE"; break;
		case DATA: os << "DATA"; break;
		case UNIT_DATA: os << "UNIT_DATA"; break;
		case ERROR: os << "ERROR"; break;
		case HARDRELEASE: os << "HARDRELEASE"; break;
		default: os << "?" << (int)prim << "?";
	}
	return os;
}




L3Frame::L3Frame(const L3Message& msg, Primitive wPrimitive)
	:BitVector(msg.bitsNeeded()),mPrimitive(wPrimitive),
	mL2Length(msg.L2Length())
{
	msg.write(*this);
}



L3Frame::L3Frame(const char* hexString)
	:mPrimitive(DATA)
{
	size_t len = strlen(hexString);
	mL2Length = len/2;
	resize(len*4);
	size_t wp=0;
	for (size_t i=0; i<len; i++) {
		char c = hexString[i];
		int v = c - '0';
		if (v>9) v = c - 'a' + 10;
		writeField(wp,v,4);
	}
}


L3Frame::L3Frame(const char* binary, size_t len)
	:mPrimitive(DATA)
{
	mL2Length = len;
	resize(len*8);
	size_t wp=0;
	for (size_t i=0; i<len; i++) {
		writeField(wp,binary[i],8);
	}
}



static const unsigned fillPattern[8] = {0,0,1,0,1,0,1,1};

void L3Frame::writeH(size_t &wp)
{
	unsigned fillBit = fillPattern[wp%8];
	writeField(wp,!fillBit,1);
}


void L3Frame::writeL(size_t &wp)
{
	unsigned fillBit = fillPattern[wp%8];
	writeField(wp,fillBit,1);
}



// vim: ts=4 sw=4
