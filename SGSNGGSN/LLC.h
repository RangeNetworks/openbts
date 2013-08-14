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

/**@file LLC objects, from GSM 04.64. */
//#include "GPRSInternal.h"	// for LLCWARN

#ifndef LLC_H
#define LLC_H

#include <ByteVector.h>
#include "SgsnBase.h"
#include "GPRSL3Messages.h"
#include <MemoryLeak.h>
//#include "TBF.h"

namespace GPRS { class MSInfo; }

namespace SGSN {
struct LlcEntity;
class SgsnInfo;
struct Sgsn;
class PdpContext;
struct Sndcp;

// GSM04.64 6.2.3 table 2.
struct LlcSapi {
	enum type {
		GPRSMM = 1,		// receives all control messages, both GMM an SM protocols.
		TOM2 = 2,
		UserData3 = 3,
		UserData5 = 5,
		SMS = 7,
		TOM8 = 8,
		UserData9 = 9,
		UserData11 = 11,
	};
	static const char *name(type sapi);
};

struct LLCFormat {
	enum type {
		Invalid, I, S, U, UI, ISack, SSack
	};
	static const char *name(type format);
};

// (pat) This is a generic parity generator for up to 32 bit parity.

// Pats Notes: The generator-based algorithm in BitVector.h did not work
// for the LLC FCS when I tried to pre-invert the remainder.
// Here is a new one based on table lookups, which is better for ByteVectors.
// An N-bit CRC is always N+1 bits long where the first and last bits are 1.
// The CRC is the remainder of division of the input by the generator.
// The complicated description of the LLC FCS is describing a normal 24-bit CRC
// but they are setting the remainder to all ones beforehand (to catch the input
// error of leading 0s) and inverting it after (to catch the input error of trailing 0s.)
// Algorithm here is based on LSB-first algorithm from wikipedia "Computation of CRC"
// So we have to pre-reverse the bits of the CRC generator.
// The top bit of the CRC sets the shift-register output to 0 for the division,
// so the division result comes out 0, but those bits are not relevant to the CRC,
// which is the remainder, because they are all shifted away.
// So we chop the top bit off.
class Parity32
{
	uint32_t mTab[256];	// Precomputed crc remainders.
	uint32_t mInvertedGenerator;
	uint32_t mInitialRemainder;
	uint32_t mMask;
	public:
	Parity32(uint32_t generator, unsigned width, bool invertFirst);
	uint32_t computeCrc(unsigned char *str, int len);
	uint32_t computeCrc(ByteVector &bv);
};


// 04.64 5.5 FCS [Frame Check Sequence] field, aka parity.
// The CRC shall be the ones complement of the sum (modulo 2) of:
// 		the remainder of xk (x23 + x22 + x21 +... + x2 + x + 1) divided (modulo 2) by
//		the generator polynomial, where k is the number of bits of the dividend;
// 	plus the remainder of the division (modulo 2) by the generator polynomial
// 		of the product of x24 by the dividend.
// The CRC-24 generator polynomial is:
// G(x) = x24 + x23 + x21 + x20 + x19 + x17 + x16 + x15 + x13 + x8 + x7 + x5 + x4 + x2 + 1
class LlcParity : public Parity32
{
	static const uint32_t sFCSGenerator =
		(1<<24) + (1<<23) + (1<<21) + (1<<20) + (1<<19) + (1<<17) + (1<<16) +
		(1<<15) + (1<<13) + (1<<8) + (1<<7) + (1<<5) + (1<<4) + (1<<2) + 1;
	public:
	LlcParity() : Parity32(sFCSGenerator,24,true) {};
	void appendFCS(ByteVector &bv);
	bool checkFCS(ByteVector &bv);	// true if parity ok.
};
extern LlcParity gLlcParity;

struct LlcDefs {
	// sec 6.4 LLC commands for U-format frames, passed in LlcFrame::mM above.
	enum U_M_Commands {	// Some are commands and some are responses.
		UCMD_NULL = 0,		// null command
		UCMD_DM = 1, 	// DM response: Disconnected Mode Response.
						// "An LLE shall transmit a DM response to any valid command
						// received that it cannot action."
						// However, I think the multitech modem sends this as a command.
		UCMD_DISC = 4, 	// DISC command: Disconnect - terminate ABM mode
		UCMD_UA = 6, 	// UA response: acknowledge SABM or DISC command
		UCMD_SABM = 7,	// SABM command: Set Async Balanced (aka acknowledged) Mode
		UCMD_FRMR = 8,	// FRMR response: Frame Reject response - includes a bunch of info; see spec.
		UCMD_XID = 0xb // XID command or response: Exchange Identification - used to set parameters.
	};
	// sec 6.4 LLC commands for S-format frames, passed in LlcFrame::mS above.
	enum U_S_Commands { 
		SCMD_RR = 0,
		SCMD_ACK = 1,
		SCMD_RNR = 2,
		SCMD_SACK = 3
	};
};

struct LlcMsg {
	virtual void llcProcess(LlcEntity *lle) = 0;
	virtual const char *typeName() { return "generic"; }
};

// Note: The frame formats are in 04.64 6.3.
struct LlcFrame : public LlcDefs, public ByteVector
{
	static const unsigned addrOffset = 0;
	static const unsigned controlOffset = 1;
	static const unsigned UIHeaderLength = 3;	// 1 byte for addr, 2 for header.

	// Address fields:
	unsigned getSapi() { return getByte(addrOffset) & 0xf; }
	bool getLlcPD() { return getBitR1(addrOffset,8); }	// 1 means LLC frame.  Good grief.
	bool getCR() { return getBitR1(addrOffset,7); }
	LLCFormat::type getFormat();	// get the format from the ByteVector.

	LlcFrame(const ByteVector &vec) : ByteVector(vec) { }
	LlcFrame(unsigned size) : ByteVector(size) { }

	//LlcFrame(const LlcFrame &other) { *this = other; }

	// Return nth control byte.  0 is the first byte after the address byte,
	// ie, LLC header byte 1.
	unsigned getControl(unsigned nth) {
		unsigned w = nth + controlOffset;
		return size() <= w ? 0 :  getByte(w);
	}

	// Write the LLC header.
	void writeAddrHeader(unsigned sapi, bool isCmd) {
		setField(0,0,1);	// PD bit always 0.
		setField(1,isCmd,1);	// Command/Response set to 1 for a downlink command.
		setField(2,0,2);	// 2 unused bits.
		setField(4,sapi,4);
	}
	void appendAddrHeader(unsigned sapi, bool isCmd) {
		writeAddrHeader(sapi, isCmd);
		setAppendP(1);
	}

	LlcMsg *switchFrame();
	void llcProcess1(LlcEntity *lle);
};

struct LlcDlFrame : public LlcFrame
{
	// Allocate room for all the downlink headers that will be needed:
	// The size is the needed payload size.
	// Add room for all the downstream headers and trailers:
	// sndcp header up to 4 bytes, but it allocates its own, so all we need are:
	// llc header 3 bytes
	// fcs trailer 3 bytes
	// We will overkill it a bit, and and let the downstream prepend their headers.
	LlcDlFrame(unsigned size) : LlcFrame(size+12) {
		trimLeft(8);	// Room for headers.
		setAppendP(0);
	}
};

// 05.64 6.3
struct LlcFrameI : public LlcFrame, public LlcMsg
{
	LlcFrameI(ByteVector &f) : LlcFrame(f) {}

	bool getA() { return getBit2(controlOffset,1); }
	unsigned getNS() { return getField2(controlOffset,3,9); }
	unsigned getNR() { return getField2(controlOffset+1,5,9); }
	unsigned getS() { return getField2(controlOffset+2,6,2); }	// S1 and S2 bits.

	void llcProcess(LlcEntity *lle) {	// We dont handle them.
		LLCWARN("LLC unexpected I frame ignored"<<LOGVAR2("S",getS()));
	}
};

// 05.64 6.3
struct LlcFrameS : public LlcFrame, public LlcMsg
{
	LlcFrameS(ByteVector &f) : LlcFrame(f) {}

	bool getA() { return getBit2(controlOffset,2); }
	unsigned getNR() { return getField2(controlOffset,5,9); }
	unsigned getS() { return getField2(controlOffset+1,6,2); }	// S1 and S2 bits.

	void llcProcess(LlcEntity *lle) {	// We dont handle them.
		LLCWARN("LLC unexpected S frame ignored"<<LOGVAR2("S",getS()));
	}
};

// 05.64 6.3
struct LlcFrameUI : public LlcFrame, public LlcMsg
{
	const char *typeName() { return "FrameUI"; }
	LlcFrameUI(ByteVector &f) : LlcFrame(f) {}
	unsigned getNU() { return getField2(controlOffset,5,9); }	// frame number
	bool getE() { return getField2(controlOffset+1,6,1); }	// encryption bit
	bool getPM() { return getField2(controlOffset+1,7,1); }	// protected mode (crc data too?)

	void llcProcess(LlcEntity *lle);
	void writeUIHeader(unsigned wNU /*, bool pf*/);
};

// 05.64 6.3
// These are commands using the S bits, mostly for ABM aka acknowledged mode.
struct LlcFrameU : public LlcFrame, public LlcMsg
{
	LlcFrameU(ByteVector &f) : LlcFrame(f) {}
	LlcFrameU(unsigned size) : LlcFrame(size) {}
	bool getUPF() { return getBit2(controlOffset,3); }
	unsigned getUM() { return getField2(controlOffset,4,4); }

	void appendUHeader(unsigned ucmd, bool pf) {
		// Write the header.
		appendField(0x7,3);	// Identifies U-format frame.
		appendField(pf,1);		// P/F should nearly always be 1.
		appendField(ucmd,4);	// The U-format command.
	}
	void llcProcess(LlcEntity *lle);
};

struct LlcFrameSack : public LlcFrame , public LlcMsg
{
	LlcFrameSack(ByteVector &f) : LlcFrame(f) {}
	void llcProcess(LlcEntity *lle) {
		LOG(ERR) << "LLC SSACK frame ignored";
		LLCWARN("LLC SSACK frame ignored");
	}
};

// XID params are in 6.4.1.6 table 6
// The "Layer-3 Parameters" are the SNDCP XID params used for compression, and vary by SAPI.
// The LLC default values are in 8.9.9, and vary by SAPI:
// Defaults for user SAPIS: N201-I=1503, N201-U=500.
struct LlcFrameXid : public LlcFrameU
{
	enum XID_Type {
		Version = 0,	// value 0-15
		IOV_UI = 1,	// Ciphering input offset value for UI frames, for all SAPIs.
		IOV_I = 2,	// Ciphering input offset value for I frames, for SAPI under negotiation
		T200 = 3,
		N200 = 4,
		N201_U = 5,	// U frame info length 140-1520
		N201_I = 6,	// I frame info length 140-1520
		mD = 7,	// I frame buffer in downlink direction, 2 bytes, 0, 9 -24320
		mU = 8,	// I frame buffer in uplink direction
		kD = 9,	// window size, downlink
		kU = 10,	// window size uplink
		layer3 = 11,	// These are SNDCP XID commands, see 3GPP 04.65.
						// They must go to one of the data SAPIs.
		reset = 12		// Does a reset.
	};
	LlcFrameXid(unsigned size) : LlcFrameU(size) { }
	//void writeXID(ByteVector bv, bool pf) {
		//writeUHeader(bv,UCMD_XID,1);	// poll/final bit is always 1 for XID frame.
	//}
	void appendXidItem(unsigned xidtype, unsigned len, unsigned value)
	{
		if (len <= 3) {
			appendField(0,1);	// XL - item length < 4.
			appendField(xidtype,5);
			appendField(len,2);
			appendField(value,8*len);
		} else {
			appendField(1,1);	// XL - item length >= 4.
			appendField(xidtype,5);
			appendField(len,8);
			appendField(0,2);			// 2 unused bits.
			appendField(value,8*len);
		}
	}
};

// 3GPP 04.64 Logical Link Entity part of LLC.
// There is one of these for each data LLC SAPI for each MS.
// The LLC SAPIs are supposed to correspond to QoS [Quality of Service] classes;
// the final NSAPI [Network SAPIs] that are connected to PDPContexts
// are the on the high side of the SNDCP entity for user-data LLC SAPIs only.
// There can be a many-to-one mapping of SNDCP entity to LLC SAPI.
// The pdu number in the header is used to discard duplicates (04.64 8.4.2),
// with a memory of 32 frames, which is redundant for data SAPIs because
// SNDCP also discards PDUs, as well as assembling and reordering.
// LLC has three states which affect LLC Entity:
//		o TLLI Unassigned state.  Can only use UI and XID frames for SAPI = 1.
//		o TLLI assigned (by the SGSN, from higher layers) - affects all entities.
//		o ABM state - acknowledged data state - per entity.
//			Unacknowledged frames may also be sent in ABM mode.
// The SAP [Service Access Points] are defined in LlcSapi above.
struct LlcEntity
{
	// 6.3.5.5 Unacknowledged mode state variables:
	static const unsigned mSNS = 512;	// modulo arithment
	unsigned mVU;	// Unconfirmed send state variable
	unsigned mVUR;	// Unconfirmed receive state variable.
	unsigned mN201U;	// Max number of bytes in UI data field.
						// This is used by the SNDCP to split the data.
	//GPRS::MSInfo *mMS;	// The MS who ultimately owns us.
	//LlcEntity(GPRS::MSInfo *ms) : mMS(ms) { reset(); }
	//GPRS::MSInfo *getMS() { return mMS; }
	SgsnInfo *mSI;	// The SgsnInfo in which we reside.
	LlcEntity(SgsnInfo *wSI) : mSI(wSI) {}

	//SgsnInfo *getSgsnInfo();

	void reset() {
		mVU = mVUR = 0;
		// 8.9.8: LLC layer parameter default values.
		// It varies by SAPI, but for user data default is 500, max 1520.
		// For other sapis length wont be exceeded anyway so dont worry about them.
		mN201U = 500;
	}

	virtual void lleUplinkData(ByteVector &payload) = 0;
	virtual unsigned getLlcSapi() = 0;
	//Sndcp *getSndcp(unsigned nsapi);
	//void setSndcp(unsigned nsapi,Sndcp*);
	void lleWriteLowSide(LlcFrame &frame);
	void lleWriteHighSide(LlcDlFrame &frame, bool isCmd, const char *descr);
	void lleWriteHighSide(L3GprsDlMsg &msg);
	void lleWriteRaw(ByteVector &frame, const char *descr);
};
#if LLC_IMPLEMENTATION
	//SgsnInfo *LlcEntity::getSgsnInfo() { return mSI; }
#endif

// Attached to one of the user-data SAPIs for an MS
struct LlcEntityUserData : public LlcEntity
{
	unsigned mLlcSapi;	// The LLC sapi of this entity.
	unsigned mN201U;
	LlcEntityUserData(unsigned wLlcSapi, SgsnInfo *si) :
		LlcEntity(si),
		mLlcSapi(wLlcSapi)
	{
		mN201U = 500;	// Default max size for pdus.
	}

	unsigned getLlcSapi() { return mLlcSapi; }
	unsigned getMaxPduSize() { return mN201U; }
	Sndcp *getSndcp(unsigned nsapi);
	void setSndcp(unsigned nsapi, Sndcp*ptr);
	void lleUplinkData(ByteVector &payload);
};
#if LLC_IMPLEMENTATION
#endif

// Attached to the LLC GPRSMM sapi for an MS.
struct LlcEntityGmm : public LlcEntity
{
	LlcEntityGmm(SgsnInfo *si) : LlcEntity(si) {}
	// The payload is in L3 message.
	void lleUplinkData(ByteVector &payload); // calls: Sgsn::handleL3Msg(this,&payload);
	unsigned getLlcSapi() { return 1; }
};

// 3GPP 04.64: SNDCP, with yet another stupid header.
// It is a miracle any data gets through at all.
// The NSAPI are on the high (network) side, and the SAPI are the low side at LLC.
// There can be multiple NSSAPI, each with a PDP context, attached to each SAPI.
// The L3 Activate PDP context message specifies both NSAPI and LLC SAPI.
struct SndcpFrame : public ByteVector
{

	SndcpFrame(ByteVector &bv) : ByteVector(bv) { }

	bool getF() { return getBit(1); }	// First segment indicator.
	bool getT() { return getBit(2); }	// 0 - DATA(acked) 1 - UNITDATA
	bool getM() { return getBit(3); }	// More bit: 1 => more segments.
	unsigned getNSapi() { return getField(4,4); }	// NSAPI on sndcp network high side.
	// Dcomp and Pcomp are only extent if F bit is set.
	unsigned getDcomp() { return getField2(1,0,4); } // data compression
	unsigned getPcomp() { return getField2(1,4,4); } // protocol compression
	ByteVector getPayload() { return tail((getT() ? 3 : 2)+(getF()?1:0)); }

	// For UNITDATA (unacknowledged mode) - T bit == 1
	unsigned getSegmentNumber() { return getField2(getF()?2:1,0,4); }
	unsigned getPduNumber() {
		if (getT()) {	// unacknowledged mode.
			return getField2(getF()?2:1,4,12);
		} else {		// acknowledged mode.
			return getField2(getF()?2:1,0,8);
		}
	}

	// For DATA (acknowled mode) - T bit == 0
	//unsigned getAMPduNumber() { return getField2(2,0,8); }
};

class Sndcp
{
	enum {	// Address field bits
		F_BIT = 0x40,
		T_BIT = 0x20,
		M_BIT = 0x10
	};
	// Our identifiers:
	static const unsigned sUmSNS = 4096;	// Module for Unacknowledged Mode, sequence number space.
	static const unsigned sAmSNS = 256;	// Module for Acknowledged Mode, sequence number space.
	unsigned mSNS;		// Current modulo
	unsigned mNSapi;	// 5..16
	unsigned mLlcSapi;	// One of the LLC UserData sapis.
	UInt_z mRecvNPdu;
	UInt_z mSendNPdu;
	LlcEntityUserData *mlle;

	public:
	//PdpContext *mPdp;
	//void setPdp(PdpContext *pdp) { mPdp = pdp; }
	//PdpContext *getPdp() { return mPdp; }
	SgsnInfo *getSgsnInfo();

	// I dont think it is possible in our case for an sndcp to exist in pdp-inactive state.
	//bool isPdpInactive();

	// Data reassembly Queues:
	// We are supposed to reorder pdus.
	// We dont care about the order, since they are going to the internet, but the packets may
	// arrive in multiple segments that have become unordered, possibly due to multi-slot transmission,
	// so we need to handle it.  We will only reorder the last sMemory pdus.
	static const unsigned sMemory = 32;
	struct OneSdu {
		UInt_z mSegCount;	// Number of segs, derived from 'm' bit.
		ByteVector segs[16];	// The segments.
	};
	OneSdu mSegs[sMemory];		// This stuff is all deleted automatically.

	Sndcp(unsigned wNSapi, unsigned wLlcSapi, LlcEntityUserData *mlle);
	~Sndcp();

	private:
	// If we have all the segments for pdu num, send it off.
	// If force, delete it even if incomplete.
	void flush(unsigned num, bool force);
	int diffSNS(int v1, int v2);
	// SDU segmented to this size.  May be negotiated using XID command, which we dont implement.
	unsigned getMaxPduSize();
	void sndcpWriteSegment(ByteVector &pduSeg, unsigned segnum, unsigned flags);

	public:
	// downlink data from internet comes in here.
	// It needs to be segmented and sent to LLC.
	void sndcpWriteHighSide(ByteVector &sdu);
	// uplink data from MS comes in here.
	void sndcpWriteLowSide(SndcpFrame &frame);
};
#if LLC_IMPLEMENTATION
Sndcp::Sndcp(unsigned wNSapi, unsigned wLlcSapi, LlcEntityUserData *wlle) :
	mSNS(sUmSNS),
	mNSapi(wNSapi),
	mLlcSapi(wLlcSapi),
	mlle(wlle)
	//,mPdp(0)
{
	mlle->setSndcp(mNSapi,this);
}

Sndcp::~Sndcp()
{
	// Test mlle and mPdp and setting to 0 after are cautious overkill.
	// The setSndcp() is also redundant, since our caller does it too.
	if (mlle) {mlle->setSndcp(mNSapi,0); mlle = 0;}
	//if (mPdp) {delete mPdp; mPdp = 0;}
}
#endif

// A connection is identified by DLCI [Data Link Connection Identifier] which is a TLLI+SAP.
// However, our LlcEngine corresponds directly with an SgsnInfo which corresponds
// directly with the MSInfo, so we dont use this.
//struct LlcDlci {
//	MSInfo *ms;
//	unsigned char mSapi;
//};

// An LLC engine for use with a single MS.
struct LlcEngine {
	// A set of LLC LLE [logical link entities] for use in an MS.
	// These use predefined SAPIs, and here they are.
	// The different user-data ones are supposed to correspond to different priorities,
	// but we ignore the distinction.
	LlcEntityGmm mLleGmm;
	LlcEntityUserData mLleUserData3;
	LlcEntityUserData mLleUserData5;
	LlcEntityUserData mLleUserData9;
	LlcEntityUserData mLleUserData11;

	// An sndcp entity for each NSAPI that is in use, ie, for each allocated pdpcontext.
	// Allocated/deallocated on demand and at the same time as the PdpContext they connect with.
	// The LlcEntityUserData are connected to these in a one-to-many mapping,
	// ie, each LlcEntity may be tied to one or more Sndcp.
#if 0==SNDCP_IN_PDP
	Sndcp *mSndcp[16];	// 0-4 are reserved (same as UMTS), but we just allocate the whole array
						// and index it directly with nsapi [Network SAPI]
#endif

	LlcEngine(SgsnInfo *si) :
		mLleGmm(si),
		mLleUserData3(3,si),
		mLleUserData5(5,si),
		mLleUserData9(9,si),
		mLleUserData11(11,si)
	{
		RN_MEMCHKNEW(LlcEngine)
#if 0==SNDCP_IN_PDP
		memset(mSndcp,0,sizeof(mSndcp));
#endif
	}

	~LlcEngine() { RN_MEMCHKDEL(LlcEngine) }

	static bool isValidDataSapi(unsigned sapi) {
		switch (sapi) {
		case LlcSapi::UserData3:
		case LlcSapi::UserData5:
		case LlcSapi::UserData9:
		case LlcSapi::UserData11:
			return true;
		default:
			return false;
		}
	}

	LlcEntity *getLlcEntity(unsigned sapi) {
		switch (sapi) {
		case LlcSapi::GPRSMM: return &mLleGmm;
		case LlcSapi::UserData3: return &mLleUserData3;
		case LlcSapi::UserData5: return &mLleUserData5;
		case LlcSapi::UserData9: return &mLleUserData9;
		case LlcSapi::UserData11: return &mLleUserData11;
		default: return 0;
		}
	}

	// The LLC Entities.  The GMM ones process L3 messages and the UserData ones are for...guess what?
	// The LlcEntityUserData communicates to one or more Sndcp on its high side.
	LlcEntityGmm *getLlcGmm();
	LlcEntityUserData *getLlcEntityUserData(unsigned llcSapi);

	// The bv is an LLC message.
	// It may cause the LLC to send some download messages,
	// or it may be an information frame on a UserData sapi
	// whose payload goes to GGSN via an Sndcp entity.
	void llcWriteLowSide(ByteVector &bv,SgsnInfo *si);

	void llcWriteHighSide(ByteVector &sdu,int nsapi);

	void allocSndcp(SgsnInfo *si, unsigned nsapi, unsigned llcsapi);
	void freeSndcp(unsigned nsapi);
};


// GSM04.64 6.2
// (pat) The RLC/MAC layer knows nothing about the contents of an LlcFrame; it just passes data
// back and forth between the MS and SGSN.
// This is used to dump out the complete header.
struct LlcFrameDump : public LlcFrame
{
	LLCFormat::type mFormat;

	// I, I+S format is for acknowledged mode.
	// UI format is for unacknowledged mode.
	// U format is for LLC control only, includes no data.
	// Control fields.  Which are valid depends on format:
	bool mE;		// UI format only
	bool mPM;		// UI format only
	bool mA;		// I or S format
	bool mPF;		// U format only
	unsigned mM;	// U format only
	unsigned mS;	// I or S formats.
	unsigned mK;	// I Sack format bitmap length.
	unsigned mNS, mNR, mNU;

	// Return -1 if we dont know how long, which is S SACK format.
	int headerLength();

	//LlcFrameDump(const ByteVector &vec) : LlcFrame(vec)
	LlcFrameDump(const ByteVector &vec) : LlcFrame(vec)
	{
		mFormat = getFormat();
		llcParseDump();
	}
	void llcParseDump();

	void textHeader(std::ostream &os);
	void textContent(std::ostream &os, bool verbose);
	void text(std::ostream &os);
};

};  // namespace GPRS

#endif

