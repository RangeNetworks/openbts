/*
* Copyright 2008, 2014 Free Software Foundation, Inc.
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




#include <iostream>

#include "GSMTransfer.h"
#include "GSML3Message.h"
//#include "Globals.h"


using namespace std;
using namespace GSM;


ostream& GSM::operator<<(ostream& os, const L2Frame& frame)
{
	os << LOGVAR2("primitive",frame.primitive()) <<LOGVAR2("SAPI",frame.SAPI());
	os << " raw=(";
	frame.hex(os);
	os << ")";
	return os;
}

ostream& GSM::operator<<(ostream& os, const L2Frame* frame)
{
	if (frame) {
		os << *frame;
	} else {
		os << "(null L2Frame)";
	}
	return os;
}


void L3Frame::text(std::ostream &os) const
{
	os << "L3Frame(" <<LOGVARM(mPrimitive)<<LOGVARM(mSapi);
	if (isData()) {
		string mtiname = mti2string(PD(),MTI());
		os <<LOGVAR2("PD",PD()) <<LOGHEX2("MTI",this->MTI()) <<"("<<mtiname<<")" <<LOGVAR2("TI",TI());
	}
	os << " raw=(";
	this->hex(os);
	os << "))";
}

ostream& GSM::operator<<(ostream& os, const L3Frame& frame)
{
	frame.text(os);
	return os;
}

ostream& GSM::operator<<(ostream& os, const L3Frame* frame)
{
	if (frame) {
		os << *frame;
	} else {
		os << "(null L3Frame)";
	}
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

void L2Frame::randomizeFiller(unsigned start)
{
	/* for debugging
	// no filler or first filler is 0x2b
	if (start-8 < size() && peekField(start-8,8) != 0x2b) {
		LOG(ALERT) << *this << " " << start;
		assert(0);
	}
	// reset of filler is 0x2b
	for (unsigned i = start; i < size(); i+=8) {
		if (peekField(i,8) != 0x2b) {
			LOG(ALERT) << *this << " " << start << " " << i;
			assert(0);
		}
	}
	*/
	for (unsigned i = start; i < size(); i++) {
		settfb(i, random() & 1);
	}
}

void L2Frame::randomizeFiller(const L2Header& header)
{
	switch (header.format()) {
		case L2Header::FmtA:
		case L2Header::FmtB:
			randomizeFiller((header.length().L() + 4) * 8);
			return;
		case L2Header::FmtBbis:
		case L2Header::FmtB4:
			randomizeFiller((header.length().L() + 2) * 8);
			return;
		default:
			return;
	}
}


L2Frame::L2Frame(const BitVector& bits)
	:BitVector(23*8), mPrimitive(L2_DATA)
{
	idleFill();
	assert(bits.size()<=this->size());
	bits.copyTo(*this);
}


L2Frame::L2Frame(const L2Header& header, const BitVector& l3, bool noran)
	:BitVector(23*8),mPrimitive(L2_DATA)
{
	idleFill();
	//printf("header.bitsNeeded=%d l3.size=%d this.size=%d\n",
	//header.bitsNeeded(),l3.size(),this->size());
	assert((header.bitsNeeded()+l3.size())<=this->size());
	size_t wp = header.write(*this);
	l3.copyToSegment(*this,wp);
	// FIXME - figure out why randomizeFiller doesn't like the "noran" headers
	if (gConfig.getBool("GSM.Cipher.ScrambleFiller") && !noran) randomizeFiller(header);
}


L2Frame::L2Frame(const L2Header& header)
	:BitVector(23*8),mPrimitive(L2_DATA)
{
	idleFill();
	header.write(*this);
	if (gConfig.getBool("GSM.Cipher.ScrambleFiller")) randomizeFiller(header);
}



unsigned L2Frame::SAPI() const
{
	// Get the service access point indicator,
	// assuming frame format A or B.
	// See GSM 04.06 2.1, 2.3, 3.2.
	// This assumes MSB-first field packing.
	// (pat) If the frame is a primitive, the size is 0 and sapi is not saved in the L2Frame.
	// The L3Frame will be rebound with the correct SAPI in L2LAPDm.
	return size()>8 ? peekField(3,3) : SAPIUndefined;
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
	// GSM 04.06 Table 4 (in section 3.8.1, obviously)
	// TODO -- This would be more efficient as an array.
	char upper = peekField(8+0,3);
	char lower = peekField(8+4,2);
	char uBits = upper<<2 | lower;
	switch (uBits) {
		case 0x07: return L2Control::SABMFrame;
		case 0x03: return L2Control::DMFrame;	// (disconnect mode)
		case 0x00: return L2Control::UIFrame;	// (unnumbered information)
		case 0x08: return L2Control::DISCFrame;	// (disconnect)
		case 0x0c: return L2Control::UAFrame;	// (unnumbered acknowledge)
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



const L2Frame& GSM::L2IdleFrame()
{
	static volatile bool init = false;
	static L2Frame idleFrame;
	if (!init) {
		init = true;
		// GSM 04.06 5.4.2.3.
		// As sent by the network.
		idleFrame.fillField(8*0,3,8);		// address
		idleFrame.fillField(8*1,3,8);		// control
		idleFrame.fillField(8*2,1,8);		// length
		if (gConfig.getBool("GSM.Cipher.ScrambleFiller")) idleFrame.randomizeFiller(8*4);
	}
	return idleFrame;
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


// (pat 1-2014) Compute the SNR of an RxBurst that is a GSM "normal burst", ie,
// one used for TCH/FACCH rather than for RACCH or other.
// For this case we ignore the tail bits and training bits and consider only the data bits.
float RxBurst::getNormalSNR() const
{
	SoftVector chunk1(this->segment(3,58));	// 57 data bits + stealing bit.
	float snr1 = chunk1.getSNR();
	SoftVector chunk2(this->segment(87,58));	// stealing bit + 57 data bits.
	float snr2 = chunk2.getSNR();
	assert(! chunk1.isOwner());	// Make sure the stupid SoftVector class really returned an alias.
	return (snr1 + snr2) / 2.0;
}



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


#define CASENAME_OS(x) case x: os << #x; break;
ostream& GSM::operator<<(ostream& os, Primitive prim)
{
	switch (prim) {
		CASENAME_OS(L2_DATA)
		CASENAME_OS(L3_DATA)
		CASENAME_OS(L3_DATA_CONFIRM)
		CASENAME_OS(L3_UNIT_DATA)
		CASENAME_OS(L3_ESTABLISH_REQUEST)
		CASENAME_OS(L3_ESTABLISH_INDICATION)
		CASENAME_OS(L3_ESTABLISH_CONFIRM)
		CASENAME_OS(L3_RELEASE_REQUEST)
		CASENAME_OS(L3_RELEASE_CONFIRM)
		CASENAME_OS(L3_HARDRELEASE_REQUEST)
		CASENAME_OS(MDL_ERROR_INDICATION)
		CASENAME_OS(L3_RELEASE_INDICATION)
		CASENAME_OS(PH_CONNECT)
		CASENAME_OS(HANDOVER_ACCESS)
		/**
		case ESTABLISH: os << "ESTABLISH"; break;
		case RELEASE: os << "RELEASE"; break;
		case DATA: os << "DATA"; break;
		case UNIT_DATA: os << "UNIT_DATA"; break;
		case ERROR: os << "ERROR"; break;
		case HARDRELEASE: os << "HARDRELEASE"; break;
		case HANDOVER_ACCESS: os << "HANDOVER_ACCESS"; break;
		**/
		//old:os << "?" << (int)prim << "?";
		default: os << "(unrecognized primitive:" << (int)prim << ")";
	}
	return os;
}

const char *GSM::SAPI2Text(SAPI_t sapi)
{
	switch (sapi) {
		case SAPI0: return "SAPI0";
		case SAPI3: return "SAPI3";
		case SAPI0Sacch: return "SAPI0-Sacch";
		case SAPI3Sacch: return "SAPI3-Sacch";
		case SAPIUndefined: return "SAPIUndefined";
		default: return "SAP-Invalid!";
	}
}
ostream& GSM::operator<<(ostream& os, SAPI_t sapi)
{
	os << SAPI2Text(sapi); return os;
}


void L3Frame::f3init()
{
	mTimestamp = timef();
}

L3Frame::L3Frame(const L3Message& msg, Primitive wPrimitive, SAPI_t wSapi)
	:BitVector(msg.bitsNeeded()),mPrimitive(wPrimitive),mSapi(wSapi),
	mL2Length(msg.l2Length())
{
	f3init();
	msg.write(*this);
}



L3Frame::L3Frame(SAPI_t sapi,const char* hexString)
	:mPrimitive(L3_DATA),mSapi(sapi)
{
	f3init();
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


// (pat) 9-8-2014 removed, unused.
//L3Frame::L3Frame(const char* binary, size_t len)
//	:mPrimitive(L3_DATA),mSapi(SAPIUndefined)
//{
//	f3init();
//	mL2Length = len;
//	resize(len*8);
//	size_t wp=0;
//	for (size_t i=0; i<len; i++) {
//		writeField(wp,binary[i],8);
//	}
//}

unsigned L3Frame::MTI() const
{
	if (!isData()) {
		// If someone calls MTI() on a primitive return a guaranteed invalid MTI instead of crashing:
		return (unsigned)-1;
	}
	int mti = peekField(8,8);
	switch (PD()) {
		case L3NonCallSSPD:
		case L3CallControlPD:
		case L3MobilityManagementPD:
			// (pat) 5-2013: For these protocols only, mask out the unused bits of the raw MTI from the L3 Frame.  See 3GPP 4.08 10.4
			return mti & 0xbf;
		default:
			return mti;
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

AudioFrameRtp::AudioFrameRtp(AMRMode wMode) : ByteVector((headerSizeBits(wMode)+GSM::gAMRKd[wMode]+7)/8), mMode(wMode)
{
	setAppendP(0);
	// Fill in the RTP and AMR headers.
	if (wMode == TCH_FS) {
		appendField(0xd,RtpHeaderSize());	// RTP type of GSM codec.
	} else {
		appendField(wMode,RtpHeaderSize());	// The CMR field is allegedly not used by anyone yet, but lets set it to the AMR mode we are using.
		appendField(0,1);		// The F bit must always be zero; we are directed to send one frame every 20ms.
		appendField(wMode,4);	// This is the important field that must be the AMR type index.
		appendField(1,1);		// All frames are "good" for now.
	}
}

void AudioFrameRtp::getPayload(BitVector *result) const
{
	// Cheating: set the BitVector directly.  TODO: move this into the BitVector class.
	char *rp = result->begin();
	for (int i = headerSizeBits(mMode), n = result->size();  n > 0; i++, n--) {
		*rp++ = getBit(i); 
	}
}



// vim: ts=4 sw=4
