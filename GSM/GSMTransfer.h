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



#ifndef GSMTRANSFER_H
#define GSMTRANSFER_H

#include "Interthread.h"
#include "BitVector.h"
#include "GSMCommon.h"


/* Data transfer objects for the GSM core. */

namespace GSM {


// Forward references.
class TxBurst;
class RxBurst;
class L3Message;

/**@name Positions of stealing bits within a normal burst, GSM 05.03 3.1.4. */
//@{
static const unsigned gHlIndex = 60;		///< index of first stealing bit, GSM 05.03 3.1.4
static const unsigned gHuIndex = 87;		///< index of second stealing bit, GSM 05.03 3.1.4
//@}

static const unsigned gSlotLen = 148;	///< number of symbols per slot, not counting guard periods




/**
	Interlayer primitives, GSM 04.04 4, GSM 04.06 4, GSM 04.07 10.
	PH, DL, MDL, etc. is implied by context.
		- L1-L2: PH
		- L2-L3: DL, MDL
	We don't provide the full req-conf-ind-ack handshake because we
	don't always need it in such a tighly integrated system, so
	our primitive set is simple.
*/
enum Primitive {
	ESTABLISH,		///< channel establihsment
	RELEASE,		///< normal channel release
	DATA,			///< multiframe data transfer
	UNIT_DATA,		///< datagram-type data transfer
	ERROR,			///< channel error
	HARDRELEASE		///< forced release after an assignment
};


std::ostream& operator<<(std::ostream& os, Primitive);



/**
	Class to represent one timeslot of channel bits with hard encoding.
*/
class TxBurst : public BitVector {

	private:

	Time mTime;			///< GSM frame number

	public:

	/** Create an empty TxBurst. */
	TxBurst(const Time& wTime = Time(0))
		:BitVector(gSlotLen),mTime(wTime)
	{
		// Zero out the tail bits now.
		mStart[0]=0; mStart[1]=0; mStart[2]=0;
		mStart[145]=0; mStart[146]=0; mStart[147]=0;
	}

	/** Create a TxBurst by copying from an existing BitVector. */
	TxBurst(const BitVector& wSig, const Time& wTime = Time(0))
		:BitVector(wSig),mTime(wTime)
	{ assert(wSig.size()==gSlotLen); }

	/** Create a TxBurst from an RxBurst (for testing). */
	TxBurst(const RxBurst& rx);

	/**@name Basic accessors. */
	//@{
	Time time() const { return mTime; }
	void time(const Time& wTime) { mTime = wTime; }
	//@}

	bool operator>(const TxBurst& other) const
		{ return mTime > other.mTime; }

	/** Set upper stealing bit. */
	void Hu(bool HuVal) { mData[gHuIndex] = HuVal; }

	/** Set lower stealing bit. */
	void Hl(bool HlVal) { mData[gHlIndex] = HlVal; }

	friend std::ostream& operator<<(std::ostream& os, const TxBurst& ts);
	
};





std::ostream& operator<<(std::ostream& os, const TxBurst& ts);

typedef InterthreadQueue<TxBurst> TxBurstFIFO;

/** The InterthreadPriorityQueue accepts Timeslots and sorts them by Timestamp. */
class TxBurstQueue : public InterthreadPriorityQueue<TxBurst> {

	public:

	/** Get the framenumber of the next outgoing burst.  Blocks if queue is empty. */
	Time nextTime() const;

};





/**
	Class to represent one timeslot of channel bits with soft encoding.
*/
class RxBurst : public SoftVector {

	private:

	Time mTime;				///< timeslot and frame on which this was received
	float mTimingError;		///< Timing error in symbol steps, <0 means early.
	float mRSSI;			///< RSSI estimate associated with the slot, dB wrt full scale.


	public:

	/** Initialize an RxBurst from a hard Timeslot.  Note the funny cast. */
	RxBurst(const TxBurst& source, float wTimingError=0, int wRSSI=0);

	/** Wrap an RxBurst around an existing float array. */
	RxBurst(float* wData, const Time &wTime, float wTimingError, int wRSSI)
		:SoftVector(wData,gSlotLen),mTime(wTime),
		mTimingError(wTimingError),mRSSI(wRSSI)
	{ }


	Time time() const { return mTime; }

	void time(const Time& wTime) { mTime = wTime; }
	
	float RSSI() const { return mRSSI; }

	float timingError() const { return mTimingError; }

	/** Return a SoftVector alias to the first data field. */
	const SoftVector data1() const { return segment(3, 57); }

	/** Return a SoftVector alias to the second data field. */
	const SoftVector data2() const { return segment(88, 57); }

	/** Return upper stealing bit. */
	bool Hu() const { return bit(gHuIndex); }

	/** Return lower stealing bit. */
	bool Hl() const { return bit(gHlIndex); }

	/** Mark even bits as unkmnown. */
	void clearEven();

	/** Mark odd bits as unknown. */
	void clearOdd();

	friend std::ostream& operator<<(std::ostream& os, const RxBurst& ts);
};

std::ostream& operator<<(std::ostream& os, const RxBurst& ts);



typedef InterthreadQueue<RxBurst> RxBurstFIFO;



class L2Frame;
class L3Frame;





/** L2 Address as per GSM 04.06 3.2 */
class L2Address {

	private:

	unsigned mSAPI;		///< service access point indicator
	unsigned mCR;		///< command/response flag
	unsigned mLPD;		///< link protocol discriminator

	public:

	L2Address(unsigned wCR=0, unsigned wSAPI=0, unsigned wLPD=0)
		:mSAPI(wSAPI),mCR(wCR),mLPD(wLPD)
	{
		assert(wSAPI<4);
	}

	/**@name Obvious accessors. */
	//@{
	unsigned SAPI() const { return mSAPI; }
	unsigned CR() const { return mCR; }
	unsigned LPD() const { return mLPD; }
	//@}

	/** Write attributes to an L2 frame. */
	void write(L2Frame& target, size_t& writeIndex) const;

};


std::ostream& operator<<(std::ostream& os, const L2Address& address);





/** The L2 header control field, as per GSM 04.06 3.4 */
class L2Control {

	public:

	/** Control field format types, GSM 04.06 3.4. */
	enum ControlFormat { IFormat, SFormat, UFormat };

	/** LAPDm frame types, GSM 04.06 3.8.1. */
	enum FrameType {
		UIFrame,
		SABMFrame,
		UAFrame,
		DMFrame,
		DISCFrame,
		RRFrame,
		RNRFrame,
		REJFrame,
		IFrame,
		BogusFrame		///< a return code used when parsing fails
	};


	private:

	ControlFormat mFormat;		///< control field format
	unsigned mNR;				///< receive sequence number
	unsigned mNS;				///< transmit sequence number
	unsigned mPF;				///< poll/final bit
	unsigned mSBits;			///< supervisory bits
	unsigned mUBits;			///< unnumbered function bits


	public:

	/** Initialize a U or S frame. */
	L2Control(ControlFormat wFormat=UFormat, unsigned wPF=0, unsigned bits=0)
		:mFormat(wFormat),mPF(wPF),mSBits(bits),mUBits(bits)
	{
		assert(mFormat!=IFormat);
		assert(mPF<2);
		if (mFormat==UFormat) assert(mUBits<0x20);
		if (mFormat==SFormat) assert(mSBits<0x04);
	}

	/** Initialize an I frame. */
	L2Control(unsigned wNR, unsigned wNS, unsigned wPF)
		:mFormat(IFormat),mNR(wNR),mNS(wNS),mPF(wPF)
	{
		assert(mNR<8);
		assert(mNS<8);
		assert(mPF<2);
	}


	/**@name Obvious accessors. */
	//@{
	ControlFormat format() const { return mFormat; }
	unsigned NR() const { assert(mFormat!=UFormat); return mNR; }
	void NR(unsigned wNR) { assert(mFormat!=UFormat); mNR=wNR; }
	unsigned NS() const { assert(mFormat==IFormat); return mNS; }
	void NS(unsigned wNS) { assert(mFormat==IFormat); mNS=wNS; }
	unsigned PF() const { assert(mFormat!=IFormat); return mPF; }
	unsigned P() const { assert(mFormat==IFormat); return mPF; }
	unsigned SBits() const { assert(mFormat==SFormat); return mSBits; }
	unsigned UBits() const { assert(mFormat==UFormat); return mUBits; }
	//@}


	void write(L2Frame& target, size_t& writeIndex) const;

	/** decode frame type */
	FrameType decodeFrameType() const;
};





std::ostream& operator<<(std::ostream& os, L2Control::ControlFormat fmt);
std::ostream& operator<<(std::ostream& os, L2Control::FrameType cmd);
std::ostream& operator<<(std::ostream& os, const L2Control& control);


/** L2 frame length field, GSM 04.06 3.6 */
class L2Length {

	private:

	unsigned mL;			///< payload length in the frame
	unsigned mM;			///< more data flag ("1" indicates segmentation)


	public: 

	L2Length(unsigned wL=0, bool wM=0)
		:mL(wL),mM(wM)
	{ }

	/**@name Obvious accessors. */
	//@{
	unsigned L() const { return mL; }
	void L(unsigned wL) { mL=wL; }

	unsigned M() const { return mM; }
	void M(unsigned wM) { mM=wM; }
	//@}

	void write(L2Frame& target, size_t &writeIndex) const;

};


std::ostream& operator<<(std::ostream&, const L2Length&);





/** The total L2 header, as per GSM 04.06 3 */
class L2Header {

	public:

	/** LAPDm frame format types, GSM 04.06 2.1 */
	enum FrameFormat {
		FmtA,			///< full header (just use B instead)
		FmtB,			///< full header
		FmtBbis,		///< no header (actually, a pseudolength header)
		FmtBter,		///< "short header" (which we don't use)
		FmtB4,			///< addesss and control only, implied length
		FmtC,			///< RACH (which we don't use)
	};



	private:

	FrameFormat mFormat;					///< format to use in the L2 frame
	L2Address mAddress;						///< GSM 04.06 2.3
	L2Control mControl;						///< GSM 04.06 2.4
	L2Length mLength;						///< GSM 04.06 2.5

	public:


	/** Parse the header from an L2Frame, assuming DCCH uplink. */
	L2Header(FrameFormat wFormat, const L2Frame& source);

	/** Format A or B. */
	L2Header(const L2Address& wAddress, const L2Control& wControl, const L2Length& wLength,
			 FrameFormat wFormat=FmtB)
		:mFormat(wFormat),
		mAddress(wAddress), mControl(wControl), mLength(wLength)
	{ }

	/** Format B4. */
	L2Header(const L2Address& wAddress, const L2Control& wControl)
		:mFormat(FmtB4),
		mAddress(wAddress), mControl(wControl)
	{ }

	/** Pseudolength case, used on non-dedicated control channels. */
	L2Header(const L2Length& wLength)
		:mFormat(FmtBbis),
		mLength(wLength)
	{ }

	/**
		Write the header into an L2Frame at a given offset.
		@param frame The frame to write to.
		@return number of bits written.
	*/
	size_t write(L2Frame& target) const;

	/** Determine the header's LAPDm operation. */
	L2Control::FrameType decodeFrameType() const { return mControl.decodeFrameType(); }

	/**@name Obvious accessors. */
	//@{
	FrameFormat format() const { return mFormat; }
	void format(FrameFormat wFormat) { mFormat=wFormat; }

	const L2Address& address() const { return mAddress; }
	L2Address& address() { return mAddress; }
	void address(const L2Address& wAddress) { mAddress=wAddress; }

	const L2Control& control() const { return mControl; }
	L2Control& control() { return mControl; }
	void control(const L2Control& wControl) { mControl=wControl; }

	const L2Length& length() const { return mLength; }
	L2Length& length() { return mLength; }
	void length(const L2Length& wLength) { mLength=wLength; }
	//@}

	/** Return the number of bits needed to encode the header. */
	size_t bitsNeeded() const;
};


std::ostream& operator<<(std::ostream& os, const L2Header& header);
std::ostream& operator<<(std::ostream& os, const L2Header::FrameFormat val);

/** N201, the maximum payload size of an L2 frame in bytes, GSM 04.06 5.8.3. */
unsigned N201(ChannelType, L2Header::FrameFormat);





/**
	The bits of an L2Frame
	Bit ordering is MSB-first in each octet.
*/
class L2Frame : public BitVector {

	private:

	GSM::Primitive mPrimitive;

	public:

	/** Fill the frame with the GSM idle pattern, GSM 04.06 2.2. */
	void idleFill();

	/** Build an empty frame with a given primitive. */
	L2Frame(GSM::Primitive wPrimitive=UNIT_DATA)
		:BitVector(23*8),
		mPrimitive(wPrimitive)
	{ idleFill(); }

	/** Make a new L2 frame by copying an existing one. */
	L2Frame(const L2Frame& other)
		:BitVector((const BitVector&)other),
		mPrimitive(other.mPrimitive)
	{ }

	/**
		Make an L2Frame from a block of bits.
		BitVector must fit in the L2Frame.
	*/
	L2Frame(const BitVector&, GSM::Primitive);

	/**
		Make an L2Frame from a payload using a given header.
		The L3Frame must fit in the L2Frame.
		The primitive is DATA.
	*/
	L2Frame(const L2Header&, const BitVector&);

	/**
		Make an L2Frame from a header with no payload.
		The primitive is DATA.
	*/
	L2Frame(const L2Header&);

	/** Get the LPD from the L2 header.  Assumes address byte is first. */
	unsigned LPD() const;

	/**
		Look into the LAPDm header and get the SAPI, see GSM 04.06 2 and 3.2.
		This method assumes frame format A or B, GSM 04.06 2.1.
	*/
	unsigned SAPI() const;


	/**@name Decoding methods that assume A/B header format. */
	//@{

	/** Look into the LAPDm header and get the control format.  */
	L2Control::ControlFormat controlFormat() const;
	
	/** Look into the LAPDm header and decode the U-frame type. */
	L2Control::FrameType UFrameType() const;

	/** Look into the LAPDm header and decode the S-frame type. */
	L2Control::FrameType SFrameType() const;

	/** Look into the LAPDm header and get the P/F bit. */
	bool PF() const { return mStart[8+3] & 0x01; }
	
	/** Set/clear the PF bit. */
	void PF(bool wPF) { mStart[8+3]=wPF; }

	/** Look into the header and get the length of the payload. */
	unsigned L() const { return peekField(8*2,6); }

	/** Get the "more data" bit (M). */
	bool M() const { return mStart[8*2+6] & 0x01; }

	/** Return the L3 payload part.  Assumes A or B header format. */
	BitVector L3Part() const { return segment(8*3,8*L()); }

	/** Return NR sequence number, GSM 04.06 3.5.2.4.  Assumes A or B header. */
	unsigned NR() const { return peekField(8*1+0,3); }

	/** Return NS sequence number, GSM 04.06 3.5.2.5.  Assumes A or B header. */
	unsigned NS() const { return peekField(8*1+4,3); }

	/** Return the CR bit, GSM 04.06 3.3.2.  Assumes A or B header. */
	bool CR() const { return mStart[6] & 0x01; }

	/** Return truw if this a DCCH idle frame. */
	bool DCCHIdle() const
	{
		return peekField(0,32)==0x0103012B;
	}

	//@}

	Primitive primitive() const { return mPrimitive; }

	/** This is used only for testing. */
	void primitive(Primitive wPrimitive) { mPrimitive=wPrimitive; }

};

std::ostream& operator<<(std::ostream& os, const L2Frame& msg);


typedef InterthreadQueueWithWait<L2Frame> L2FrameFIFO;



/**
	Representation of a GSM L3 message in a bit vector.
	Bit ordering is MSB-first in each octet.
	NOTE: This is for the GSM message bits, not the message content.  See L3Message.
*/
class L3Frame : public BitVector {

	private:

	Primitive mPrimitive;
	size_t mL2Length;		///< length, or L2 pseudo-length, as appropriate

	public:

	/** Empty frame with a primitive. */
	L3Frame(Primitive wPrimitive=DATA, size_t len=0)
		:BitVector(len),mPrimitive(wPrimitive),mL2Length(len)
	{ }

	/** Put raw bits into the frame. */
	L3Frame(const BitVector& source, Primitive wPrimitive=DATA)
		:BitVector(source),mPrimitive(wPrimitive),mL2Length(source.size()/8)
	{ if (source.size()%8) mL2Length++; }

	/** Concatenate 2 L3Frames */
	L3Frame(const L3Frame& f1, const L3Frame& f2)
		:BitVector(f1,f2),mPrimitive(DATA),
		mL2Length(f1.mL2Length + f2.mL2Length)
	{}

	/** Build from an L2Frame. */
	L3Frame(const L2Frame& source)
		:BitVector(source.L3Part()),mPrimitive(DATA),
		mL2Length(source.L())
	{ }

	/** Serialize a message into the frame. */
	L3Frame(const L3Message& msg, Primitive wPrimitive=DATA);

	/** Get a frame from a hex string. */
	L3Frame(const char*);

	/** Get a frame from raw binary. */
	L3Frame(const char*, size_t len);

	/** Protocol Discriminator, GSM 04.08 10.2. */
	L3PD PD() const { return (L3PD)peekField(4,4); }

	/** Message Type Indicator, GSM 04.08 10.4.  */
	unsigned MTI() const { return peekField(8,8); }

	/** TI value, GSM 04.07 11.2.3.1.3.  */
	unsigned TI() const { return peekField(0,4); }

	/** Return the associated primitive. */
	GSM::Primitive primitive() const { return mPrimitive; }

	/** Return frame length in BYTES. */
	size_t length() const { return size()/8; }

	/** Length, or L2 pseudolength, as appropriate */
	size_t L2Length() const { return mL2Length; }

	void L2Length(size_t wL2Length) { mL2Length=wL2Length; }

	// Methods for writing H/L bits into rest octets.
	void writeH(size_t& wp);
	void writeL(size_t& wp);
};



std::ostream& operator<<(std::ostream& os, const L3Frame&);

typedef InterthreadQueue<L3Frame> L3FrameFIFO;



/** A vocoder frame for use in GSM/SIP contexts. */
class VocoderFrame : public BitVector {

	public:

	VocoderFrame()
		:BitVector(264)
	{ fillField(0,0x0d,4); }

	/** Construct by unpacking a char[33]. */
	VocoderFrame(const unsigned char *src)
		:BitVector(264)
	{ unpack(src); }

	BitVector payload() { return tail(4); }
	const BitVector payload() const { return tail(4); }

};


typedef InterthreadQueue<VocoderFrame> VocoderFrameFIFO;

};	// namespace GSM



#endif


// vim: ts=4 sw=4
