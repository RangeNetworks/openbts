/*
* Copyright 2011 Range Networks, Inc.
* All Rights Reserved.
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

#ifndef RLCHDR_H
#define RLCHDR_H

#include <stdlib.h>
#include "GPRSRLC.h"
#include "ScalarTypes.h"
#include "Defines.h"
//#include <iostream>
#include <BitVector.h>
#include "ByteVector.h"
#include "MsgBase.h"
#include "GPRSInternal.h"
#include "MemoryLeak.h"
#define CASENAME(x) case x: return #x;


namespace GPRS {
extern unsigned RLCPayloadSizeInBytes[4];
extern unsigned RLCBlockSizeInBits[4];

class MACPayloadType // It is two bits.  GSM04.60sec10.4.7
{
	public:
	enum type {
		RLCData=0,
		RLCControl=1, 	// The RLC/MAC block does NOT include the optional Octets.
		RLCControlExt=2, 	// The RLC/MAC block DOES include the optional Octets.
									// Used only in downlink direction.
		// Value 3 is reserved.
	};
	static const char *name(int val) {
		switch ((type)val) {
			CASENAME(RLCData)
			CASENAME(RLCControl)
			CASENAME(RLCControlExt)
		}
		return "unrecognized MACPayloadType";
	}
};


// RLC/MAC control message may be sent in an RLC/MAC control block, which is always
// encoded with CS-1, so length is 176 bits (22 octets).
// Some MAC messages are also sent on PBCCH, PCCCH or PACCH.
// Note that no fields in any MAC or RLC header exceed one byte,
// so there are no network ordering problems, and we can simply use C structs
// to define the bit packing.

// (pat) Data blocks defined in GSM04.60 sec 10.3.

struct MACDownlinkHeader 	// 8 bits.  See GSM04.60sec10.3.1
{
	// From GSM 04.60 10.4:
	// RRBP - expected frame delay for ACK
	// SP - If 1 we expect an ack and RRBP means something.
	// USF - User state flag for shared channels.
	protected:
 	//MACPayloadType::type mPayloadType:2;
	Field_z<2> mPayloadType;
	Field_z<2> mRRBP; 	// RRBP: Relative Reserved Block Period.  See GSM04.60sec10.4.5
						// It specifies 3,4,5 or 6 block delay for ACK/NACK (to give the
						// MS time to decode the block.)
	public:
	Field_z<1> mSP; 		// Supplementary/Polling Bit - indicates whether RRBP is valid.
	Field_z<3> mUSF; 	// For uplink dynamic allocation method.
			// indicates owner of the next uplink radio block in the same timeslot.
			// Except on PCCCH, where a value of 0x111 indicates next uplink radio
			// block reserved for PRACH.
	unsigned lengthBits() { return 8; }	// Size of the MAC header in bits.

	void writeMACHeader(MsgCommon& dest) const;

	void init(MACPayloadType::type wPayloadType) { mPayloadType = wPayloadType; }
	void setRRBP(int rrbp) { mRRBP = rrbp; mSP = 1; }
	bool isControlMsg() { return mPayloadType != MACPayloadType::RLCData; }
	bool isMacUnused() { return mSP == 0 && mUSF == 0; }
};
#if RLCHDR_IMPLEMENTATION
	void MACDownlinkHeader::writeMACHeader(MsgCommon& dest) const {
		//dest.WRITE_ITEM(mPayloadType);
		dest.writeField(mPayloadType,2,"PayLoadType",MACPayloadType::name);
		dest.WRITE_ITEM(mRRBP);
		dest.WRITE_ITEM(mSP);
		dest.WRITE_ITEM(mUSF);
	}
#endif

struct MACUplinkHeader 	// 8 bits.  See GSM04.60sec10.3.2
{
	public:
	// Note: countdown value and SI are used only for uplink data blocks;
	// for uplink control blocks, CountdownValue and SI are unused, always 0.
	// Octet 1 (and only):
	MACPayloadType::type mPayloadType:2;
	Field<4> mCountDownValue; 	// GSM04.60 9.3.1.
									// 15 until close to the end, then countdown.  Last block 0.
									// Once MS starts countdown, it wont interrupt the
									// transfer for higher priority TBFs.
									// Update: MSs dont do the countdown, they send 15 until
									// the next-to-last block, then send 0.
	Field<1> mSI; // SI: Stall Indicator
	Field<1> mR; 	// Retry bit, sent by MS if it had to try more than once to get this through.

	unsigned lengthBits() { return 8; }	// Size of the MAC header in bits.
	int parseMAC(const BitVector&src);
	// Since this is an uplink header, we only need to write it for testing purposes, eg, text().
	void writeMACHeader(MsgCommon&dst) const;
	void text(std::ostream&os) const;
	bool isFinal() { return mCountDownValue == 0; }
};
#if RLCHDR_IMPLEMENTATION
	// It is one byte.
	int MACUplinkHeader::parseMAC(const BitVector&src) {
		size_t rp = 0;
		mPayloadType = (MACPayloadType::type) src.readField(rp,2);
		mCountDownValue = src.readField(rp,4);
		mSI = src.readField(rp,1);
		mR = src.readField(rp,1);
		return 8;
	}

	// Since this is an uplink header, we only need to write it for testing purposes, eg, text().
	void MACUplinkHeader::writeMACHeader(MsgCommon&dst) const {
		// This is special cased so we get the name of the payload type in the text.
		dst.writeField(mPayloadType,2,"PayLoadType",MACPayloadType::name);
		/***
		std::ostream *os = dst.getStream();	// returned os is non-null only if caller is text().
		if (os) {
			*os << "mPayloadType=(" << MACPayloadType::name(mPayloadType) << ")";
		} else {
			dst.writeField(mPayloadType,2);
		}
		***/
		dst.WRITE_ITEM(mCountDownValue);
		dst.WRITE_ITEM(mSI);
		dst.WRITE_ITEM(mR);
	}
	void MACUplinkHeader::text(std::ostream&os) const {
		MsgCommonText dst(os);
		writeMACHeader(dst);
		//os << "mPayloadType=(" << MACPayloadType::name(mPayloadType) << ")";
		//os << RN_WRITE_TEXT(mCountDownValue);
		//os << RN_WRITE_TEXT(mSI);
		//os << RN_WRITE_TEXT(mR);
	}
#endif

struct RadData {
	bool mValid;
	float mRSSI;
	float mTimingError;
	RadData() { mValid = false; }
	RadData(float wRSSI, float wTimingError) : mValid(true),mRSSI(wRSSI),mTimingError(wTimingError) {}
};


// An incoming block straight from the decoder.
struct RLCRawBlock {
	RLCBSN_t mBSN;	// The BSN corresponding to the GSM FN of the first received burst.
	RadData mRD;
	BitVector mData;
	MACUplinkHeader mmac;
	ChannelCodingType mUpCC;
	RLCRawBlock(int wfn, const BitVector &wData,float wRSSI, float wTimgingError,ChannelCodingType cc);
	~RLCRawBlock() { RN_MEMCHKDEL(RLCRawBlock) }
};
#if RLCHDR_IMPLEMENTATION
	RLCRawBlock::RLCRawBlock(int wbsn, const BitVector &wData,
		float wRSSI, float wTimingError, ChannelCodingType cc)
	{
		RN_MEMCHKNEW(RLCRawBlock)
		mData.clone(wData);	// Explicit clone.
		mBSN = wbsn;
		mmac.parseMAC(mData); 	// Pull the MAC header out of the BitVector.
		mUpCC = cc;
		assert(mData.isOwner());
		mRD = RadData(wRSSI,wTimingError);
	}
#endif

// There may be multiple RLC_sub_blocks for multiple PDUs in one RLC/MAC block.
struct RLCSubBlockHeader
{
	static int makeoctet(unsigned length, unsigned mbit, unsigned ebit) {
		return (length << 2) | (mbit << 1) | ebit;
	}
	//	unsigned mLengthIndicator:6;	// length of PDU with block, but see GSM04.60sec10.4.14
	// If the LLC PDU does not fit in a single RLC block, then there is no
	// length indicator byte for the full RLC blocks, and the rest of the RLC block is PDU data.
	// (Works because the E bit in the RLC header tells us if there is a length indicator.)
	// Otherwise (ie, for the final RLC segment of each PDU), there is a length indicator,
	// and there may be additional PDUs tacked on after the end of this PDU, specified by M bit.
	// The description of length indicator in 10.4.14 is confusing.
	// In English: you need the length-indicator byte if:
	// (a) The PDU does not fill the RLC block, indicated by the LI field, or
	// (b) This is the last segment of a PDU and there are more PDUs following
	//     in the same TBF, indicated by M=1.
	// The "singular" case mentioned in 10.4.14 occurs when you need the
	// length-indicator for (b) but not for (a).  In this singular case only,
	// you set the LI (length) field to 0, god only knows why, and presumably M=0,
	// in the next-to-last segment, then presumably you put the last byte of
	// the PDU into the next RLC block with a LI=1 and M=1.
	// If the PDU is incomplete (for uplink blocks only, which presumably means
	// the allocation ran out before the PDU data) then the final RLC block
	// would be full so you normally wouldn't need an LI Byte, but to mark this fact
	// you must reduce the last segment size by one to make room for an LI Byte with LI=0,
	// and presumably M=0, which is distinguishable from the "singular case" above
	// by the CountDown field reaching 0.
	//	unsigned mM:1;			// More bit; if set there is another sub-block after this one.
	//	unsigned mE:1;			// Extension bit, see GSM04.60 table 10.4.13.1.
	// With M bit, indicates if there is more PDU data in this RLC block.
	// M+E = 0+0: reserved;
	// M+E = 0+1: no more data after the current LLC PDU segment.
	// M+E = 1+0: new LLC PDU after current LLC PDU, with extension octet.
	// M+E = 1+1: new LLC PDU after current LLC PDU, fills rest of RLC block.
};


// We are not using this, because we are not doing contention resolution required
// for single phase uplink.
struct RLCSubBlockTLLI
{
	RLCSubBlockHeader hdr;	// 1 byte.
	unsigned char mTLLI[4];	// 4 bytes containing a TLLI.
	struct {
		unsigned mPFI:7;
		unsigned mE:1;
	} b5;
};


struct RLCDownlinkDataBlockHeader // GSM04.60sec10.2.1
 	: public MACDownlinkHeader		// 1 byte MAC header
{
	// From GSM 04.60 10.4:
	// Octet 1:
	// Use of Power Reduction field described in 5.08 10.2.2.  We dont need it.
	Field_z<2> mPR; 	// Power Reduction.  0 means no power reduction, 1,2 mean some, 3 means 'not usable.'
	Field_z<5> mTFI; 	// TFI - temp flow ID that this block is in
	Field_z<1> mFBI;	// Final Block Indicator - last block of this TBF

	// Octet 2:
	Field_z<7> mBSN; 		// Block Sequence Number, modulo 128
	// If E bit is one, followed directly by RLC data.
	// If E bit is zero, followed by zero or more RLC_Data_Sub_Block.
	Field_z<1> mE;			// End of extensions bit.

	// Note: unused RLC data field filled with 0x2b as per 04.60 10.4.16

	RLCDownlinkDataBlockHeader() {
		MACDownlinkHeader::init(MACPayloadType::RLCData);
	}
	void writeRLCHeader(MsgCommon& dest) const;
	void write(BitVector&dst) const;
	void text(std::ostream&os) const;

	//void setTFI(unsigned wTFI) { b1.mTFI = wTFI; }
	//void setFBI(bool wFBI) { b1.mFBI = wFBI; }
	//void setBSN(unsigned wBSN) { b2.mBSN = wBSN; }
	//void setE(bool wE) { b2.mE = wE; }

};
#if RLCHDR_IMPLEMENTATION
	void RLCDownlinkDataBlockHeader::writeRLCHeader(MsgCommon& dest) const {
		dest.WRITE_ITEM(mPR);
		dest.WRITE_ITEM(mTFI);
		dest.WRITE_ITEM(mFBI);
		dest.WRITE_ITEM(mBSN);
		dest.WRITE_ITEM(mE);
	}

	void RLCDownlinkDataBlockHeader::write(BitVector&dst) const {
		MsgCommonWrite mcw(dst);
		MACDownlinkHeader::writeMACHeader(mcw);
		RLCDownlinkDataBlockHeader::writeRLCHeader(mcw);
	}
	void RLCDownlinkDataBlockHeader::text(std::ostream&os) const {
		MsgCommonText dst(os);
		writeMACHeader(dst);
		writeRLCHeader(dst);
	}
#endif


// GSM04.60sec10.2.2
struct RLCUplinkDataBlockHeader
{
	// Octet 0 is the MAC header.
	MACUplinkHeader mmac;
	// Octet 1: RLC Header (starts at bit 8)
	Field<1> mSpare;
	Field<1> mPI;	// PFI Indicator bit; If set, optional PFI is present in data.
					// PFI identifies a Packet Flow Context defined GSM24.008,
					// and mentioned in GSP04.60 table 11.2.6.2
					// Range will not support these.
	Field<5> mTFI; 	// TFI - temp flow ID that this block is in
	Field<1> mTI;	// TLLI indicator.  If set, optional TLLI is present in dataa.
					// TLLI must be send by MS during a one phase access, because
					// the network does not know TLLI.  It is not needed for two phase access.
	// Octet 2:
	Field<7> mBSN;	// Block Sequence Number, modulo 128
	Field<1> mE;	// Extension bit: 0 indicates next word is length indicator,
					// 1 means whole block is data.

	public:

	size_t parseRLCHeader(const RLCRawBlock *src);
	// Since this is an uplink header, we only need to write it for testing purposes.
	void writeRLCHeader(MsgCommon&dst) const;
	void write(BitVector&dst) const;	// Only needed for testing.
	void text(std::ostream&os) const;
};
#if RLCHDR_IMPLEMENTATION
	size_t RLCUplinkDataBlockHeader::parseRLCHeader(const RLCRawBlock *src) {
		mmac = src->mmac;
		size_t rp = mmac.lengthBits();
		rp++;	// skip spare bit.
		mPI = src->mData.readField(rp,1);
		mTFI = src->mData.readField(rp,5);
		mTI = src->mData.readField(rp,1);
		mBSN = src->mData.readField(rp,7);
		mE = src->mData.readField(rp,1);
		return rp;
	}
	// Since this is an uplink header, we only need to write it for testing purposes.
	void RLCUplinkDataBlockHeader::writeRLCHeader(MsgCommon&dst) const {
		dst.WRITE_ITEM(mSpare);
		dst.WRITE_ITEM(mPI);
		dst.WRITE_ITEM(mTFI);
		dst.WRITE_ITEM(mTI);
		dst.WRITE_ITEM(mBSN);
		dst.WRITE_ITEM(mE);
	}
	void RLCUplinkDataBlockHeader::write(BitVector&dst) const {	// Only needed for testing.
		MsgCommonWrite mcw(dst);
		mmac.writeMACHeader(mcw);
		writeRLCHeader(mcw);
	}
	void RLCUplinkDataBlockHeader::text(std::ostream&os) const {
		MsgCommonText dst(os);
		mmac.writeMACHeader(dst);
		writeRLCHeader(dst);
	}
#endif

class RLCUplinkDataSegment : public BitVector {
	public:
	RLCUplinkDataSegment(const BitVector&wPayload) :
		BitVector(NULL,(char*)wPayload.begin(),(char*)wPayload.end()) {}

	// access to potentially multiple data fields
	// GSM04.60 sec 10.4.13 and 10.4.14
	size_t LIByteLI(size_t lp=0) const { return peekField(lp,6); }
	bool LIByteM(size_t lp=0) const { return peekField(lp+6,1); }
	bool LIByteE(size_t lp=0) const { return peekField(lp+7,1); }
};

class RLCUplinkDataBlock
	: public RLCUplinkDataBlockHeader
{
	BitVector mData;
	public:
	static const unsigned mHeaderSizeBits = 24;
	ChannelCodingType mUpCC;	// We dont use this - debugging info only.

	// Convert a BitVector into an RLC data block.
	// We simply take ownership of the BitVector memory.
	// The default destructor will destroy the BitVector when delete is called on us.
	RLCUplinkDataBlock(RLCRawBlock* wSrc);
	~RLCUplinkDataBlock() { RN_MEMCHKDEL(RLCUplinkDataBlock) }

	// Return subset of the BitVector that is payload.
	BitVector getPayload() { return mData.tail(mHeaderSizeBits); }
	void text(std::ostream&os);
};
#if RLCHDR_IMPLEMENTATION
	RLCUplinkDataBlock::RLCUplinkDataBlock(RLCRawBlock* wSrc)
	{
		RN_MEMCHKNEW(RLCUplinkDataBlock)
		mData = wSrc->mData;
		size_t tmp = parseRLCHeader(wSrc);
		mUpCC = wSrc->mUpCC;
		assert(tmp == mHeaderSizeBits);
	}
	void RLCUplinkDataBlock::text(std::ostream&os) {
		os << "RLCUplinkDataBlock=(";
		RLCUplinkDataBlockHeader::text(os);
		os << "\npayload:";
		// Write out the data as bytes.
		BitVector payload(getPayload());
		payload.hex(os);
		/***
		int i, size=payload.size(); char buf[10];
		for (i=0; i < size-8; i+=8) {
			sprintf(buf," %02x",(int)payload.peekField(i,8));
			os << buf;
		}
		***/
		os << ")";
	}
#endif

/** GSM 04.60 10.2.1 */
class RLCDownlinkDataBlock
	: public RLCDownlinkDataBlockHeader, public Text2Str
{
	public:
	// The mPayload does not own any allocated storage; it points into the mPDU of the TBF.
	// We are not currently putting multiple PDUs in a TBF, so this is ok.
	ByteVector mPayload;	// max size is 52, may be smaller.
	bool mIdle;				// If true, block contains only a keepalive.
	ChannelCodingType mChannelCoding;

	int getPayloadSize() const {	// In bytes.
		return RLCPayloadSizeInBytes[mChannelCoding];
	}

	int headerSizeBytes() { return 3; }

	RLCDownlinkDataBlock(ChannelCodingType wCC) : mIdle(0), mChannelCoding(wCC) {}

	// Convert the Downlink Data Block into a BitVector.
	// We do this right before sending it down to the encoder.
	BitVector getBitVector() const;
	void text(std::ostream&os, bool includePayload) const;
	void text(std::ostream&os) const { text(os,true); }	// Default value doesnt work. Gotta love that C++.
	
	//	/** Construct the block and write data into it. */
	//	DownlinkRLCDataBlock(
	//		unsigned RRBP, bool SP, unsigned USF,
	//		bool PR, unsigned TFI, bool FBI,
	//		unsigned BSN,
	//		const BitVector& data);

};
#if RLCHDR_IMPLEMENTATION
	BitVector RLCDownlinkDataBlock::getBitVector() const
	{
		// Add 3 bytes for mac and rlc headers.
		BitVector result(8 *(3+mPayload.size()));
		RLCDownlinkDataBlockHeader::write(result);
		BitVector resultpayload(result.tail(3*8));
		resultpayload.unpack(mPayload.begin()); // unpack mPayload into resultpayload
		return result;
	}
	void RLCDownlinkDataBlock::text(std::ostream&os, bool includePayload) const {
		os << "RLCDownlinkDataBlock=(";
		RLCDownlinkDataBlockHeader::text(os);
		os << LOGVAR2("CCoding",mChannelCoding) <<LOGVAR2("idle",mIdle);
		if (includePayload) {
			os << "\npayload:" << mPayload;
		}
		/***
		int i, size=mPayload.size(); char buf[10];
		for (i=0; i < size-8; i+=8) {
			sprintf(buf," %02x",(int)mPayload.peekField(i,8));
			os << buf;
		}
		***/
		os << ")";
	}
#endif

}; // namespace GPRS
#endif
