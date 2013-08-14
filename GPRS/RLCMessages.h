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

/**@file GPRS L2 RLC Messages, from GSM 04.60 Section 11 */

#ifndef GPRSL2RLCMESSAGES_H
#define GPRSL2RLCMESSAGES_H


#include <iostream>
#include <stdint.h>
#include "Defines.h"
#include "BitVector.h"
#include "Logger.h"
#include "GSMCommon.h"
//#include "GSMTransfer.h"
//#include "GSMTDMA.h"
//#include <Globals.h>
#include "MsgBase.h"
#include "RLCHdr.h"
#include "TBF.h"
#include "MemoryLeak.h"


namespace GPRS {
class TBF;


/** GSM 04.60 11.2 */
class RLCMessage : public Text2Str {
	public:
	virtual void text(std::ostream&) const = 0;
	RLCMessage() { RN_MEMCHKNEW(RLCMessage) }
	// The virtual keyword on a destructor indicates that both the base destructor (this one)
	// and the derived class destructor are both called.  Otherwise they aren't.  It is foo bar.
	virtual ~RLCMessage() { RN_MEMCHKDEL(RLCMessage) }
};
std::ostream& operator<<(std::ostream& os, const RLCMessage *msg);
#if RLCMESSAGES_IMPLEMENTATION
std::ostream& operator<<(std::ostream& os, const RLCMessage *msg)
{
	msg->text(os);
	return os;
}
#endif

class RLCMsgUplinkIE
{
	public:
	virtual void parseElement(const BitVector &src, size_t &rp) = 0;
	virtual void writeBody(MsgCommon&dst) const = 0;
	// The indivual IE can overwrite textElement is MsgCommon does not work very well for it.
	virtual void textElement(std::ostream& os) const {
		MsgCommonText dst(os);
		writeBody(dst);
	}
};

class RLCMsgDownlinkIE
{
	public:
	virtual void writeBody(MsgCommon&dst) const = 0;

	void writeOptional01(MsgCommon &dst, bool control) const {
		if (control) {
			dst.write1();
			writeBody(dst);
		} else {
			dst.write0();
		}
	}
};

// The RLCDownlink and RLCUplink Messagesc are RLC Control messages -
// they are not L3 Messages as defined in GSM 04.18.
// These messages are used at the RLC/MAC layer to control the PDCH channels
// assigned for packet (GPRS) use.  Their primary use is to transfer
// L3 Messages and user data between the SGSN and the MS.
// Mobility Management routines reside entirely inside the SGSN.
class RLCDownlinkMessage :
	public MACDownlinkHeader,
	public RLCMessage
{
	public:

	/** Message Type GSM 04.06 11.2.0.2 */
	// The ones we will implement marked with <<<
	// Message marked PCCCH only cannot be implemented now.
	// From sec 11.1.1.1: if the high bit of a downlink control message type is 1,
	// then it is a distribution message to all MS, otherwise it is a non-distribution message.
	// Note that the downlink message content is byte aligned (6 bit MessageType + 2 bit PageMode)
	// Also note that the uplink message content is not byte aligned.
	// From sec 11.1.1.2: Non-distribution messages may have distribution contents,
	// followed by an Address Information (to identify a single MS) followed by
	// non-distribution contents. 
	// NOTE: The unused bits at the end of the control message must be packed out
	// as per GSM04.60sec11: one 0 bit, followed by 0 bits to get to a byte boundary,
	// followed by byte 0x2b forever.
	enum MessageType {
		/*100001 */  PacketAccessReject  = 0x21,		// PACCH or PCCCH <<<<
		/*000001 */  PacketCellChangeOrder = 0x1,	// PCCCH or PACCH
		/*000010 */  PacketDownlinkAssignment = 0x2,	// PCCCH or PACCH <<<< acknowledged
		/*000011 */  PacketMeasurementOrder  = 0x3,	// PCCCH or PACCH

		// PagingRequest is only sent on PACCH to initiate an RR connection, so skip for now:
		// Note: You can send still GSM paging messages on CCCH when MS is in packet-idle mode.
		/*100010 */  PacketPagingRequest  = 0x22,		// PCCCH or PACCH
		/*100011 */  PacketPDCHRelease = 0x23,			// PACCH
		/*000100 */  PacketPollingRequest = 0x4,		// PACCH or PCCH <<<< not used implicitly acknowldged
		/*000101 */  PacketPowerControlTimingAdvance = 0x5,	// PACCH
		/*100100 */  PacketPRACHParameters = 0x24,		// PCCCH only
		/*000110 */  PacketQueueingNotification = 0x6,	// PCCCH only
		/*000111 */  PacketTimeslotReconfigure = 0x7,	// PACCH
		/*001000 */  PacketTBFRelease = 0x8,			// PACCH <<< acknowledged
		/*001001 */  PacketUplinkAckNack = 0x9,			// PACCH <<<
		// The acknowledgement to Packet Uplink Assignment is not strictly needed
		// because we will know as soon as the MS starts sending blocks.
		/*001010 */  PacketUplinkAssignment = 0xa,		// PCCCH or PACCH <<< acknowledged
		/*100101 */  PacketDownlinkDummyControlBlock = 0x25,	// PCCCH or PACCH
		/*110001 */  PSI1 = 0x31,
		/*110010 */  PSI2 = 0x32,
		/*110011 */  PSI3 = 0x33,
		/*110100 */  PSI3bis = 0x34,
		/*110101 */  PSI4 = 0x35,
		/*110110 */  PSI5 = 0x36,
		/*110000 */  PSI6 = 0x30,
		/*111000 */  PSI7 = 0x38,
		/*111001 */  PSI8 = 0x39,
		/*110111 */  PSI13 = 0x37,
		/*111010 */  PSI14 = 0x3a,
		/*111100 */  PSI3ter = 0x3c,
		/*111101 */  PSI3quater = 0x3d,
		/*111110 */  PSI15 = 0x3e
	};
	static const char *name(MessageType mtype);

	// There are optional additional RLC Control Block header fields here, but
	// we will not use them, so they are not even mentioned here.

	MessageType mMessageType;	// 6 bits
	unsigned mPageMode;			// 2 bits

	TBF *mTBF;					// If this message is associated with a tbf, here it is.
	
	RLCDownlinkMessage(MessageType mtype, bool wRequiresAck, TBF *wtbf) :
		mMessageType(mtype),
		mPageMode(0),
		mTBF(wtbf)
	{
		MACDownlinkHeader::init(MACPayloadType::RLCControl);
	}


	// Not all the messages have setTLLI().
	virtual void setTLLI(int) {};
	// Not all the messages have setTimingAdvance().
	virtual void setTimingAdvance(int) {};

	// writeBody defined in each message class to write the message body starting after PageMode.
	virtual void writeBody(MsgCommon&dst) const = 0;
	void writeHeader(MsgCommon &dst) const;

	/** Serialize this message into a BitVector.  The caller is responsible for deleting the memory.
	 */
	void write(BitVector&vec) const;
	void text(std::ostream &os) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCDownlinkMessage::writeHeader(MsgCommon& dst) const {
		MACDownlinkHeader::writeMACHeader(dst);
		dst.WRITE_FIELD(mMessageType,6);
		dst.WRITE_FIELD(mPageMode,2);
	}
	void RLCDownlinkMessage::text(std::ostream &os) const {
		MsgCommonText dst(os);
		os << name(mMessageType) << ":";
		writeHeader(dst);
		writeBody(dst);
	}
	void RLCDownlinkMessage::write(BitVector&vec) const {
		MsgCommonWrite dst(vec);
		writeHeader(dst);
		writeBody(dst);
		// GSM04.60 sec 11: 
		// The padding bits may be the 'null' string. Otherwise, the padding
		// bits starts with bit '0', followed by 'spare padding'.
		if (dst.wp & 0x7) { dst.write0(); }	// one optional 0 bit
		while (dst.wp & 0x7) { dst.writeL(); }	// pad to a byte boundary.
		while (dst.wp < RLCBlockSizeInBits[0]) { dst.writeField(0x2b,8); }
	}
#endif



/** GSM 04.60 11.2 */
class RLCUplinkMessage : public RLCMessage
{
	public:
	// GSM04.60 sec 11.2
	enum MessageType {
		/* 000000 */ PacketCellChangeFailure = 0x0,
		/* 000001 */ PacketControlAcknowledgement = 0x1,
		/* 000010 */ PacketDownlinkAckNack = 0x2,
		/* 000011 */ PacketUplinkDummyControlBlock = 0x3,
		/* 000100 */ PacketMeasurementReport = 0x4,
		/* 001010 */ PacketEnhancedMeasurementReport = 0xa,
		/* 000101 */ PacketResourceRequest = 0x5,
		/* 000110 */ PacketMobileTBFStatus = 0x6,
		/* 000111 */ PacketPSIStatus = 0x7,
		/* 001000 */ EGPRSPacketDownlinkAckNack = 0x8,
		/* 001001 */ PacketPause = 0x9,
		/* 001011 */ AdditionalMSRadioAccessCapabilities = 0xb
	};
	static const char *name(MessageType type);
	MACUplinkHeader mmac;				// 8 bits
	MessageType mMessageType;	// 6 bits


	// Parse/text just the body.  These must be defined in each descendent class
	//virtual void textBody(std::ostream&os) const = 0;

	virtual int getTFI() { return -1; }
	virtual MSInfo *getMS(PDCHL1FEC *chan, bool create) { assert(0); }
	virtual MSInfo *getTBF(PDCHL1FEC *chan) { assert(0); }

	virtual void parseBody(const BitVector &src, size_t &rp) = 0;
	virtual void writeBody(MsgCommon&dst) const = 0;
	void parse(const RLCRawBlock *src);
	void writeHeader(MsgCommon &dst) const;

	/** Serialize this message into a BitVector.  The caller is responsible for deleting the memory.
	 */
	void write(BitVector&vec) const;
	void text(std::ostream &os) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCUplinkMessage::parse(const RLCRawBlock*src) {
		// The MACUplinkHeader is explict (mmac) instead of a base class
		// to make this line of code more obvious:
		mmac = src->mmac;		// Parsed previously.
		size_t rp = mmac.lengthBits();
		mMessageType = (MessageType) src->mData.readField(rp,6);
		parseBody(src->mData,rp);
	}

	void RLCUplinkMessage::writeHeader(MsgCommon&dst) const {
		mmac.writeMACHeader(dst);
		dst.WRITE_FIELD(mMessageType,6);
	}

	void RLCUplinkMessage::text(std::ostream &os) const {
		MsgCommonText dst(os);
		os << name(mMessageType) << ":";
		mmac.text(os);		// or: mmac.writeMACHeader(dst);
		dst.WRITE_FIELD(mMessageType,6);
		writeBody(dst);
	}
	void RLCUplinkMessage::write(BitVector&vec) const {
		MsgCommonWrite dst(vec);
		writeHeader(dst);
		writeBody(dst);
		// GSM04.60 sec 11: For RLC messages: one 0 followed by byte-aligned 0x2b
		if (dst.wp & 0x7) { dst.write0(); }	// one optional 0 bit
		while (dst.wp & 0x7) { dst.writeL(); }	// pad to a byte boundary.
		while (dst.wp < RLCBlockSizeInBits[0]) { dst.writeField(0x2b,8); }
	}
#endif

// (pat) This is for downlink power parameters, as per GSM04.60 sec 11.2.29.
// We will probably never use this.
//struct L3IADownlinkPowerOptionIE {
//  int mPowerOption;   // 1 if P0, PWR_CTRL_MODE, PR_MODE should be included.
//  unsigned mP0:4;
//  unsigned mBTSPwrCtrlMode:1;
//  unsigned mPRMode:1;
//  void writePower(L3Frame &dst, int whichDesignationMethod) {
//      if (mPowerOption) {
//          whichDesignationMethod ? dst.writeH() : dst.writeField(1,1);
//          dst.writeField(P0,4);
//          dst.writeField(mBTSPwrCtrlMode,1);
//          dst.writeField(mPRMode,1);
//      } else {
//          whichDesignationMethod ? dst.writeL() : dst.writeField(0,1);
//      }
//  }
//  L3AssignmentPowerOptionIE::L3AssignmentPowerOptionIE() { memset(this,0,sizeof(*this)); }
//};


// GSM04.60 12.3
// Note this IE is both uplink and downlink.
struct RLCMsgPacketAckNackDescriptionIE : public RLCMsgUplinkIE, public RLCMsgDownlinkIE
{
	Field<1> mFinalAckIndication;
	Field<7> mSSN;	// "Starting Sequence Number" except it is not.
					// It is actually the ending sequence number.
	static const int mbitmapsize = 64;
	bool mBitMap[mbitmapsize];

	void parseElement(const BitVector &src, size_t &rp);
	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketAckNackDescriptionIE::parseElement(const BitVector &src, size_t &rp)
	{
		mFinalAckIndication = src.readField(rp,1);
		mSSN = src.readField(rp,7);
		for (int i = 0; i < mbitmapsize; i++) {
			mBitMap[i] = src.readField(rp,1);
		}
	}
	void RLCMsgPacketAckNackDescriptionIE::writeBody(MsgCommon&dst) const
	{
		dst.WRITE_ITEM(mFinalAckIndication);
		dst.WRITE_ITEM(mSSN);
		dst.writeBitMap((bool*)mBitMap,mbitmapsize,"Bitmap");
	}
#endif

// GSM04.60 12.7
struct RLCMsgChannelRequestDescriptionIE : public RLCMsgUplinkIE
{
	Field<4> mPeakThroughputClass; // See 3GPP 24.008
	Field<2> mRadioPriority;	// See 11.2.5 0 =highest, 3=lowest
	Field<1> mRLCMode;	// 0 == acknowledged, 1 == unacknowleged mode.
	Field<1> mLLCPDUType;	// 0 == LLC PDU is SACK or ACK[acknowledged mode], 1 == its not
	Field<16> mRLCOctetCount;	// Number of octets MS wants to transfer, or 0 if unspecified.

	void parseElement(const BitVector &src, size_t &rp);
	//void textElement(std::ostream&) const;
	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgChannelRequestDescriptionIE::parseElement(const BitVector &src, size_t &rp)
	{
		mPeakThroughputClass = src.readField(rp,4);
		mRadioPriority = src.readField(rp,2);
		mRLCMode = src.readField(rp,1);
		mLLCPDUType = src.readField(rp,1);
		mRLCOctetCount = src.readField(rp,16);
	}
	void RLCMsgChannelRequestDescriptionIE::writeBody(MsgCommon&dst) const {
		dst.WRITE_ITEM(mPeakThroughputClass);
		dst.WRITE_ITEM(mRadioPriority);
		dst.WRITE_ITEM(mRLCMode);
		dst.WRITE_ITEM(mLLCPDUType);
		dst.WRITE_ITEM(mRLCOctetCount);
	}

	/*
	void RLCMsgChannelRequestDescriptionIE::textElement(std::ostream&os) const
	{
		os << " ChannelRequest=(";
		RN_WRITE_TEXT(mPeakThroughputClass);
		RN_WRITE_TEXT(mRadioPriority);
		RN_WRITE_TEXT(mRLCMode);
		RN_WRITE_TEXT(mLLCPDUType);
		RN_WRITE_TEXT(mRLCOctetCount);
		os << ")";
	}
	*/
#endif

struct MSRACap_s : public RLCMsgUplinkIE
{
	Field<4> mAccessTechnologyType;	// The caller parses and sets this.
	bool mA5BitsPresent;
		Field<3> mRFPowerCapability;
	Field<7> mA5Bits;
	Field<1> mESInd;
	Field<1> mPS;
	Field<1> mVGCS;
	Field<1> mVBS;
	bool mMultiSlotCapPresent;
		bool mHSCDMultiSlotClassPresent;
			Field<5> mHSCDMultiSlotClass;
		bool mGPRSMultiSlotClassPresent;
			Field<5> mGPRSMultiSlotClass;
			Field<1> mGPRSExtendedDynamicAllocationCapability;
		bool mSMSPresent;
			Field<4> mSMS_VALUE; Field<4> mSM_VALUE;
	// There is much more, but we dont keep it.

	void parseElement(const BitVector &src, size_t &rp);
	void writeBody(MsgCommon&dst) const;
	//void textElement(std::ostream&) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void MSRACap_s::parseElement(const BitVector &src, size_t &rp)
	{
		mRFPowerCapability = src.readField(rp,3);
		if ((mA5BitsPresent = src.readField(rp,1))) {
			mA5Bits = src.readField(rp,7);
		}
		mESInd = src.readField(rp,1);
		mPS = src.readField(rp,1);
		mVGCS = src.readField(rp,1);
		mVBS = src.readField(rp,1);
		if ((mMultiSlotCapPresent = src.readField(rp,1))) {
			// Multislot Capability Struct:
			if ((mHSCDMultiSlotClassPresent = src.readField(rp,1))) {
				mHSCDMultiSlotClass = src.readField(rp,5);
			}
			if ((mGPRSMultiSlotClassPresent = src.readField(rp,1))) {
				mGPRSMultiSlotClass = src.readField(rp,5);
				mGPRSExtendedDynamicAllocationCapability = src.readField(rp,1);
			}
			if ((mSMSPresent = src.readField(rp,1))) {
				mSMS_VALUE = src.readField(rp,4);
				mSM_VALUE = src.readField(rp,4);
			}
		} else {
			mGPRSMultiSlotClassPresent = 0;
		}
	}
	void MSRACap_s::writeBody(MsgCommon&dst) const {
		dst.WRITE_ITEM(mAccessTechnologyType);
		dst.WRITE_ITEM(mRFPowerCapability);
		if (dst.write01(mA5BitsPresent)) { dst.WRITE_ITEM(mA5Bits); }
		dst.WRITE_ITEM(mESInd);
		dst.WRITE_ITEM(mVGCS);
		dst.WRITE_ITEM(mVBS);
		if (mMultiSlotCapPresent) {
			if (dst.write01(mHSCDMultiSlotClassPresent)) dst.WRITE_ITEM(mHSCDMultiSlotClass); 
			if (dst.write01(mGPRSMultiSlotClassPresent)) {
				dst.WRITE_ITEM(mGPRSMultiSlotClass);
				dst.WRITE_ITEM(mGPRSExtendedDynamicAllocationCapability);
			}
			if (dst.write01(mSMSPresent)) {
				dst.WRITE_ITEM(mSMS_VALUE);
				dst.WRITE_ITEM(mSM_VALUE);
			}
		}
		// More stuff ignored.
	}

	/*
	void MSRACap_s::textElement(std::ostream&os) const {
		RN_WRITE_TEXT(mAccessTechnologyType);
		RN_WRITE_TEXT(mRFPowerCapability);
		RN_WRITE_OPT_TEXT(mA5Bits,mA5BitsPresent);
		RN_WRITE_TEXT(mESInd);
		RN_WRITE_TEXT(mPS);
		RN_WRITE_TEXT(mVGCS);
		RN_WRITE_TEXT(mVBS);
		RN_WRITE_OPT_TEXT(mGPRSMultiSlotClass,mGPRSMultiSlotClassPresent);
	}
	*/
#endif

// GSM24.008sec10.5.5.12a
struct RLCMsgMSRACapabilityValuePartIE : public RLCMsgUplinkIE
{
	// There can be many of these.
	// The first one is all we are interested in.
	// We will map all the other caps into the second struct, which we wont even use.
	MSRACap_s MSRACap[2];
	int mNumMSRACap;

	void parseElement(const BitVector &src, size_t &rp);
	void writeBody(MsgCommon&dst) const;
	//void textElement(std::ostream&) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgMSRACapabilityValuePartIE::parseElement(const BitVector &src, size_t &rp)
	{
		mNumMSRACap = 0;
		do {
			int AccessTechnologyType = src.readField(rp,4);
			unsigned len = src.readField(rp,7);	// Length in bits of the rest of this access cap.
			// Kyle at L3 reported that this was crashing in the src.readField below,
			// which means this struct is invalid or not being parsed properly.
			// Lets check, although it actually needs to be much bigger than this
			// because this is embedded inside a large message.
			if (len + 1 >= src.size()) {
				LOG(INFO) << "Invalid RA Capability Value Part struct"<<LOGVAR(len)<<LOGVAR(mNumMSRACap);
			}
			size_t startrp = rp;
			if (AccessTechnologyType != 0xf) {
				if (mNumMSRACap < 2) {	// Save the first two.
					MSRACap[mNumMSRACap].mAccessTechnologyType = AccessTechnologyType;
					MSRACap[mNumMSRACap].parseElement(src,rp);
					mNumMSRACap++;
				}
			} else {
				// Additional access technologies.  Just throw it away.
			}
			rp = startrp + len;
		} while (src.readField(rp,1));
	}
	void RLCMsgMSRACapabilityValuePartIE::writeBody(MsgCommon&dst) const {
		std::ostream *os;
		if ((os = dst.getStream())) {
			for (int i=0; i < mNumMSRACap; i++) {
				*os << " RACapability[" << i <<"]=(";
					MSRACap[i].writeBody(dst);
				*os << ")";
			}
		} else {
			// Not implemented.  It is an uplink IE so we dont need it.
		}
	}
	/*
	void RLCMsgMSRACapabilityValuePartIE::textElement(std::ostream&os) const
	{
		for (int i=0; i < mNumMSRACap; i++) {
			os << " RACapability[" << i <<"]=(";  MSRACap[i].textElement(os); os << ")";
		}
	}
	*/
#endif

// GSM 04.60 12.10
struct RLCMsgGlobalTFIIE : public RLCMsgDownlinkIE, public RLCMsgUplinkIE
{
	Field_z<1> mIsDownlinkTFI;	// 1 for downlink TFI, 0 for uplink TFI
	Field_z<5> mGTFI;	// This is NOT a TFI assignment; it is used
							// to identify the MS which receives this message.

	void setTFI(bool wIsDownlinkTFI,int wTFI) {
		// TBF uninitialized TFI is -1; it is an error if that value finds its way here.
		assert(wTFI >= 0 && wTFI < 32);
		mIsDownlinkTFI = wIsDownlinkTFI; mGTFI = wTFI;
	}
	void parseElement(const BitVector &src, size_t &rp);
	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgGlobalTFIIE::parseElement(const BitVector &src, size_t &rp) {
		mIsDownlinkTFI = src.readField(rp,1);
		mGTFI = src.readField(rp,5);
	}
	void RLCMsgGlobalTFIIE::writeBody(MsgCommon &dst) const {
		dst.WRITE_ITEM(mIsDownlinkTFI);
		dst.WRITE_ITEM(mGTFI);
	}
#endif

// 3GPP 44-060 11.2.6
// See 45.008 for definitions of these things.
struct ChannelQualityReportIE : public RLCMsgUplinkIE
{

	// 45.008 10.2.3: "C Value is the enormalized received signal level at the MS
	//		as defined in 10.2.3.1".
	// During Packet Transfer mode (which we are for a DownlinkAckNack)
	// C Value and SIGN_VAR (variance of C Value) are measured from BCCH and
	// subsequently used to determine MS output power as per 10.2.1.
	Field<6> mCValue;
	// RXQUAL is derived from decoder BEP [Bit Error Probability]
	Field<3> mRXQual;	// From decoder, low is better, meaningless for CS-4 encoding.
	// SIGN_VAR from 0 to 64 encoded as: 0 to 15.75dB in 0.25dB steps.
	Field<6> mSignVar;
	// 45.008 10.3: I_LEVEL is measured interference level on each channel.
	Bool_z mHaveILevel[8];
	Field<4> mILevel[8];	// For each timeslot.
	void parseElement(const BitVector &src, size_t &rp);
	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void ChannelQualityReportIE::parseElement(const BitVector &src, size_t &rp) {
		mCValue = src.readField(rp,6);
		mRXQual = src.readField(rp,3);
		mSignVar = src.readField(rp,6);
		for (int i = 0; i <= 7; i++) {
			if ((mHaveILevel[i] = src.readField(rp,1))) {
				mILevel[i] = src.readField(rp,4);
			}
		}
	}
	void ChannelQualityReportIE::writeBody(MsgCommon&dst) const {
		dst.WRITE_ITEM(mCValue);
		dst.WRITE_ITEM(mRXQual);
		dst.WRITE_ITEM(mSignVar);
		for (int i = 0; i <= 7; i++) {
			if (mHaveILevel[i]) { dst.WRITE_ITEM(mILevel[i]); }
		}
	}
#endif

/** GSM04.60 11.2.6 From MS to network. */
struct RLCMsgPacketDownlinkAckNack : public RLCUplinkMessage
{
	Field<5> mTFI;
	RLCMsgPacketAckNackDescriptionIE mAND;
	bool mHaveChannelRequest;
	RLCMsgChannelRequestDescriptionIE mCRD;
	ChannelQualityReportIE mCQR;

	// We can ignore the rest of the message.
	// struct ChannelQualityReport CQR;
	// unsigned PFI:7 // (we wont use it)

	RLCMsgPacketDownlinkAckNack(const RLCRawBlock *src) { parse(src); }

	int getTFI() { return mTFI; }

	void parseBody(const BitVector &src, size_t &rp);
	void writeBody(MsgCommon&dst) const;
	//void textBody(std::ostream&) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketDownlinkAckNack::parseBody(const BitVector &src, size_t &rp)
	{
		mTFI = src.readField(rp,5);
		mAND.parseElement(src,rp);
		mHaveChannelRequest = src.readField(rp,1);
		if (mHaveChannelRequest) { mCRD.parseElement(src,rp); }
		mCQR.parseElement(src,rp);
	}
	void RLCMsgPacketDownlinkAckNack::writeBody(MsgCommon&dst) const {
		dst.WRITE_ITEM(mTFI);
		mAND.writeBody(dst);
		if (dst.write01(mHaveChannelRequest)) { mCRD.writeBody(dst); }
		mCQR.writeBody(dst);
	}

	/*
	void RLCMsgPacketDownlinkAckNack::textBody(std::ostream&os) const
	{
		RN_WRITE_TEXT(mTFI);
		mAND.textElement(os);
		if (mHaveChannelRequest) { mCRD.textElement(os); }
	}
	*/
#endif

//static uint64_t readOptField(BitVector&src,size_t&rp,int bits,bool&option) {
//	if ((option = src.readField(rp,1))) {
//		return src.readField(rp,bits);
//	}
//}


/** GSM04.60 11.2.6 From MS to network. */
struct RLCMsgPacketControlAcknowledgement : public RLCUplinkMessage
{
	Field<32> mTLLI;		// Yes, this really is not byte aligned in the message.
	Field<2> mCtrlAck;	// Which segments of the RLC/MAC control message were received?

	RLCMsgPacketControlAcknowledgement(const RLCRawBlock *src) { parse(src); }
	void parseBody(const BitVector &src, size_t &rp);
	void writeBody(MsgCommon&dst) const;
	//void textBody(std::ostream&) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketControlAcknowledgement::parseBody(const BitVector&src, size_t&rp)
	{
		mTLLI = src.readField(rp,32);
		mCtrlAck = src.readField(rp,2);
	}
	void RLCMsgPacketControlAcknowledgement::writeBody(MsgCommon&dst) const {
		dst.writeField(mTLLI,32,"TLLI",tohex);
		dst.WRITE_ITEM(mCtrlAck);
	}

	/*
	void RLCMsgPacketControlAcknowledgement::textBody(std::ostream&os) const
	{
		RN_WRITE_TEXT(mTLLI);
		RN_WRITE_TEXT(mCtrlAck);
	}
	*/
#endif

/** GSM04.60 11.2.16 From MS to network. */
struct RLCMsgPacketResourceRequest : public RLCUplinkMessage
{
	bool mAccessTypePresent; // 0=two phase, 1=page response, 2=cell update, 3=mm procedure
	Field<2> mAccessType;
	RLCMsgGlobalTFIIE mGTFI;
	bool mTLLIPresent;	// 1 for TLLI present, 0 for TFI present.
	Field<32> mTLLI;
	bool mMSRadioAccessCapability2Present;
	RLCMsgMSRACapabilityValuePartIE mMSRACap;
	RLCMsgChannelRequestDescriptionIE mCRD;
	bool mChangeMarkPresent;
	Field<2> mChangeMark;
	Field<5> mCValue;
	bool mSignVarPresent;
	Field<6> mSignVar;	// Not present for two phase access.
	bool mILevelPresent[8];
	Field<4> mILevelTN[8];
	bool mExtensionsPresent;

	RLCMsgPacketResourceRequest(const RLCRawBlock *src) { parse(src); }

	// Return the MSInfo identified by the contents of this message.
	MSInfo *getMS(PDCHL1FEC *chan, bool create);

	void parseBody(const BitVector &src, size_t &rp);
	void writeBody(MsgCommon&dst) const;
	//void textBody(std::ostream&) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketResourceRequest::parseBody(const BitVector &src, size_t &rp)
	{
		if ((mAccessTypePresent = src.readField(rp,1))) { mAccessType = src.readField(rp,2); }
		if ((mTLLIPresent = src.readField(rp,1))) {
			mTLLI = src.readField(rp,32);
		} else {
			mGTFI.parseElement(src,rp);
		}

		if ((mMSRadioAccessCapability2Present = src.readField(rp,1))) {
			mMSRACap.parseElement(src,rp);
		}

		mCRD.parseElement(src,rp);	// We will use this.
		if ((mChangeMarkPresent = src.readField(rp,1))) { mChangeMark = src.readField(rp,2); }
		mCValue = src.readField(rp,6);
		if ((mSignVarPresent = src.readField(rp,1))) { mSignVar = src.readField(rp,2); }
		for (int i = 0; i < 8; i++) {
			mILevelTN[i] = (mILevelPresent[i] = src.readField(rp,1)) ? src.readField(rp,4) : -1;
		}
		mExtensionsPresent = rp < src.size() && src.readField(rp,1);
		// But ignore the exensions.
	}
	void RLCMsgPacketResourceRequest::writeBody(MsgCommon&dst) const {
		if (dst.write01(mAccessTypePresent)) { dst.WRITE_ITEM(mAccessType); }
		if (dst.write01(mTLLIPresent)) {
			dst.writeField(mTLLI,32,"TLLI",tohex);
		} else {
			mGTFI.writeBody(dst);
		}
		if (dst.write01(mMSRadioAccessCapability2Present)) {
			mMSRACap.writeBody(dst);
		}
		mCRD.writeBody(dst);
		if (dst.write01(mChangeMarkPresent)) { dst.WRITE_ITEM(mChangeMark); }
		dst.WRITE_ITEM(mCValue);
		if (dst.write01(mSignVarPresent)) { dst.WRITE_ITEM(mSignVar); }
		for (int i = 0; i < 8; i++) {
			if (dst.write01(mILevelPresent[i])) dst.WRITE_ITEM(mILevelTN[i]);
		}
		dst.writeField(mExtensionsPresent,1,"mExtensionsPresent");
	}

	/*
	void RLCMsgPacketResourceRequest::textBody(std::ostream&os) const {
		RN_WRITE_OPT_TEXT(mAccessType,mAccessTypePresent);
		RN_WRITE_OPT_TEXT(mTLLI,mTLLIPresent);
		RN_WRITE_OPT_TEXT(mIsDownlinkTFI,!mTLLIPresent);
		RN_WRITE_OPT_TEXT(mTFI,!mTLLIPresent);
		if (mMSRadioAccessCapability2Present) { mMSRACap.textElement(os); } 
		mCRD.textElement(os);
		RN_WRITE_OPT_TEXT(mChangeMark,mChangeMarkPresent);
		RN_WRITE_TEXT(mCValue);
		RN_WRITE_OPT_TEXT(mSignVar,mSignVarPresent);
		for (int i = 0; i < 8; i++) {
			RN_WRITE_OPT_TEXT(mILevelTN[i],mILevelTN[i] >= 0);
		}
	}
	*/
#endif

/** GSM04.60 11.2.8b From MS to network. */
struct RLCMsgPacketUplinkDummyControlBlock : public RLCUplinkMessage
{
	Field<32> mTLLI;

	RLCMsgPacketUplinkDummyControlBlock(const RLCRawBlock*src) { parse(src); }

	void parseBody(const BitVector &src, size_t &rp);
	void textBody(std::ostream&) const;
	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketUplinkDummyControlBlock::parseBody(const BitVector &src, size_t &rp)
	{
		mTLLI = src.readField(rp,32);
	}
	void RLCMsgPacketUplinkDummyControlBlock::writeBody(MsgCommon&dst) const {
		dst.writeField(mTLLI,32,"TLLI",tohex);
	}

	/*
	void RLCMsgPacketUplinkDummyControlBlock::textBody(std::ostream&os) const
	{
		RN_WRITE_TEXT(mTLLI);
	};
	*/
#endif


// GSM04.60 12.12
struct RLCMsgPacketTimingAdvanceIE : public RLCMsgDownlinkIE
{
	// Timing Advance defined in GSM05.10
	Field_z<1> mHaveTimingAdvanceValue;
	Field_z<6> mTimingAdvanceValue;		// New timing advance if present.
	// We dont use the following:
	// Timeslot number defined in GSM05.10
	Field_z<1> mHaveContinuousTiming;	// Controls next two fields:
	Field_z<4> mTimingAdvanceIndex;		// Used for continuous timing advance.
	Field_z<3> mTimingAdvanceTimeslotNumber;	// Timeslot that gets the continuous timing advance.

	void setTimingAdvance(int ta) { mHaveTimingAdvanceValue=1; mTimingAdvanceValue=ta; }

	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketTimingAdvanceIE::writeBody(MsgCommon &dst) const {
		if (dst.write01(mHaveTimingAdvanceValue)) {
			dst.WRITE_ITEM(mTimingAdvanceValue);
		}
		if (dst.write01(mHaveContinuousTiming)) {
			dst.WRITE_ITEM(mTimingAdvanceIndex);
			dst.WRITE_ITEM(mTimingAdvanceTimeslotNumber);
		}
	}
#endif

// GSM04.60 12.12a - same as TimingAdvanceIE but allows separate
// values for uplink and downlink.
struct RLCMsgPacketGlobalTimingAdvanceIE : public RLCMsgDownlinkIE
{
	// Timing Advance defined in GSM05.10
	Bool_z mHaveTimingAdvanceValue;
	Field_z<6> mTimingAdvanceValue;		// New timing advance if present.
	// Timeslot number defined in GSM05.10
	// We dont use the following:
	Bool_z mHaveUplinkContinuousTiming;	// Controls next two fields:
	Field_z<4> mUplinkTimingAdvanceIndex;		// Used for continuous timing advance.
	Field_z<3> mUplinkTimingAdvanceTimeslotNumber;	// Timeslot that gets the continuous timing advance.
	Bool_z mHaveDownlinkContinuousTiming;	// Controls next two fields:
	Field_z<4> mDownlinkTimingAdvanceIndex;		// Used for continuous timing advance.
	Field_z<3> mDownlinkTimingAdvanceTimeslotNumber;	// Timeslot that gets the continuous timing advance.

	void setTimingAdvance(int ta) { mHaveTimingAdvanceValue=1; mTimingAdvanceValue=ta; }

	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketGlobalTimingAdvanceIE::writeBody(MsgCommon &dst) const {
		if (dst.write01(mHaveTimingAdvanceValue)) {
			dst.WRITE_ITEM(mTimingAdvanceValue);
		}
		if (dst.write01(mHaveUplinkContinuousTiming)) {
			dst.WRITE_ITEM(mUplinkTimingAdvanceIndex);
			dst.WRITE_ITEM(mUplinkTimingAdvanceTimeslotNumber);
		}
		if (dst.write01(mHaveDownlinkContinuousTiming)) {
			dst.WRITE_ITEM(mDownlinkTimingAdvanceIndex);
			dst.WRITE_ITEM(mDownlinkTimingAdvanceTimeslotNumber);
		}
	}
#endif

// GSM04.60 12.13
struct RLCMsgPowerControlParametersIE : public RLCMsgDownlinkIE
{
	Field_z<4> mAlpha;		// 4 bits.
	Bool_z mHaveTN[8];
	Field_z<5> mGammaTN[8];		// 5 bits each
	void setAlpha(int alpha) { mAlpha = alpha; }
	void setGamma(int timeslot, int gamma) { mGammaTN[timeslot] = gamma; mHaveTN[timeslot] = true; }
	void setFrom(TBF *tbf);

	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPowerControlParametersIE::writeBody(MsgCommon &dst) const {
		dst.WRITE_ITEM(mAlpha);
		for (int i=0; i<8; i++) {
			if (dst.write01(mHaveTN[i])) dst.WRITE_ITEM(mGammaTN[i]);
		}
	}
#endif

// This is not an official Informaion Element, but is commonly used in several messages.
// Used to identify to the MS targetted for a downlink message.
// The MS may be identified by TLLI, uplink TFI, or downlink TFI.
// In some messages, can also use TQI  or Packet Request Reference,
// but we dont use those.
// This is not an official "Information Element", but it is used the
// same way in many downlink messages, since they all have to target an MS.
struct RLCMsgGlobalTFIorTLLIElt : public RLCMsgDownlinkIE
{
	Bool_z mHaveGlobalTFI;
	// Global TFI includes the following two fields:
	RLCMsgGlobalTFIIE mGlobalTFI; // This is NOT a TFI assignment; it is used
							// to identify the MS which receives this message.
	Bool_z mHaveTLLI;
	Field_z<32> mTLLI;

	void setTLLI(int tlli) {
		mHaveGlobalTFI=0; mHaveTLLI=1; mTLLI=tlli;
	}
	void setGlobalTFI(bool wIsDownlinkTFI,int wTFI) {
		mHaveGlobalTFI = 1; mGlobalTFI.setTFI(wIsDownlinkTFI,wTFI);
	}

	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgGlobalTFIorTLLIElt::writeBody(MsgCommon &dst) const {
		if (mHaveGlobalTFI) {
			dst.write0();
			mGlobalTFI.writeBody(dst);
		} else if (mHaveTLLI) {
			dst.write1();
			dst.write0();
			dst.writeField(mTLLI,32,"TLLI",tohex);
		} else {
			// Could be TQI  or Packet Request Reference
			assert(0);
		}
	}
#endif

// GSM04.60 12.21
class RLCMsgStartingFrameNumberIE : public RLCMsgDownlinkIE
{
	public:
	Field_z<1> mAbsoluteOrRelative;	// 0 => absolute, 1 => relative
	Field_z<16> mAbsoluteFrameNumber;
	Field_z<13> mRelativeFrameNumber;

	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgStartingFrameNumberIE::writeBody(MsgCommon &dst) const {
		//dst.writeField(mAbsoluteOrRelative,1);
		dst.WRITE_ITEM(mAbsoluteOrRelative);
		if (mAbsoluteOrRelative) {
			dst.WRITE_ITEM(mRelativeFrameNumber);
		} else {
			dst.WRITE_ITEM(mAbsoluteFrameNumber);
		}
	}
#endif

// GSM04.60 11.2.29
// Also used in Timeslot Reconfigure message 44.060 11.2.31
class RLCMsgPacketUplinkAssignmentDynamicAllocationElt : public RLCMsgDownlinkIE
{
	public:
	Field_z<1> mExtendedDynamicAllocation;	// Needed if number up > number down
	// P0, PR_MODE unused.
	Field_z<1> mUSFGranularity;				// Use 0 for USF specified per-block.

	// In the TimeslotReconfigure message, this is never present, always one-bit 0:
	Bool_z mUplinkTFIAssignmentPresent;
	Field_z<5> mUplinkTFIAssignment;

	//Bool_z mRLCDataBlocksGrantedPresent;
	//Field_z<8> mRLCDataBlocksGranted;

	Field_z<1> mTBFStartingTimePresent;
	RLCMsgStartingFrameNumberIE mTBFStartingTime;

	// There are two types of Timeslot allocation, with and without power control params.
	Bool_z mHavePower;	// Set if gamma supplied for any timeslot.
	Field_z<4> mAlpha;		// 4 bits.
	Bool_z mHaveTN[8];
	Field_z<3> mUSFTN[8];		// 3 bits each.
	Field_z<5> mGammaTN[8];		// 5 bits each

	void setUplinkTFI(int tfi) { mUplinkTFIAssignmentPresent=1; mUplinkTFIAssignment=tfi; }
	void setUSF(int timeslot, int usf) { mUSFTN[timeslot] = usf; mHaveTN[timeslot] = true; }
	void setGamma(int timeslot, int gamma) { mGammaTN[timeslot] = gamma; mHavePower = true; }
	void setAlpha(int alpha) { mAlpha = alpha; mHavePower = true; }
	void setFrom(TBF *tbf,MultislotSymmetry);

	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketUplinkAssignmentDynamicAllocationElt::writeBody(MsgCommon &dst) const {
		dst.WRITE_ITEM(mExtendedDynamicAllocation);
		dst.write0();	// No P0, PR_MODE.
		dst.WRITE_ITEM(mUSFGranularity);
		//dst.WRITE_OPT_FIELD01(mUplinkTFIAssignment,5,mUplinkTFIAssignmentPresent);
		dst.WRITE_OPT_ITEM01(mUplinkTFIAssignment,mUplinkTFIAssignmentPresent);
		// 44.060 removed the RLC Data Blocks Granted field, just inserts a 0 bit here.
		// dst.WRITE_OPT_ITEM01(mRLCDataBlocksGranted,mRLCDataBlocksGrantedPresent);
		dst.write0();	// RLC Data Blocks Granted removed from 44.060 11.2.29
		//dst.writeField(mTBFStartingTimePresent,1);
		dst.WRITE_ITEM(mTBFStartingTimePresent);
		if (mTBFStartingTimePresent) {
			mTBFStartingTime.writeBody(dst);
		}

		char name[14];	// Make the output pretty.
		strcpy(name,"USF_TN?");
		char *end = name+strlen(name)-1;
		if (dst.write01(mHavePower)) {
			dst.WRITE_ITEM(mAlpha);
			for (int i=0; i<8; i++) {
				if (dst.write01(mHaveTN[i])) {
					*end = '0' + i;
					dst.writeField(mUSFTN[i],3,name);
					dst.writeField(mGammaTN[i],5,"Gamma");
				}
			}
		} else {
			for (int i=0; i<8; i++) {
				if (dst.write01(mHaveTN[i])) {
					*end = '0' + i;
					dst.writeField(mUSFTN[i],3,name);
				}
			}
		}
	}
#endif


/** GSM04.60 11.2.29 From network to MS */
class RLCMsgPacketUplinkAssignment : public RLCDownlinkMessage
{
	public:
	//Bool_z mPersistenceLevelPresent;
	//Field_z<4> mPersistenceLevel[4];		// This is for PRACH control, so we wont use it.

	// mTQI		// We wont use it.
	// mPacketRequestReference // We wont use it.
	RLCMsgGlobalTFIorTLLIElt mMSID;
	void setTLLI(int tlli) { mMSID.setTLLI(tlli); }

	// Currently we only support CS-1 for uplink.
	ChannelCodingType mChannelCodingCommand;	// MS to use which: CS-1, etc.

	// For one phase access, the MS must send the TLLI in an RLC Data Block early on.
	// TLLIBlockChannelCoding controls the coding of that block.  We are not using it.
	Field_z<1>  mTLLIBlockChannelCoding;	// 0 MS to use CS-1 for TLLI block, 1 to use mChannelCoding.
	RLCMsgPacketTimingAdvanceIE mPacketTimingAdvance;
	void setTimingAdvance(int ta) { mPacketTimingAdvance.setTimingAdvance(ta); }

	// FreqParams specifies an ARFCN, TSC (Training Sequence Code) frequency hopping, etc.
	// FrequenceParameters;	// We dont use it.

	// Finally, the allocation itself:  We only support dynamic on uplink for now.
	Field_z<2> mAllocationType;	// 1 => dynamic, 2 => single block, 3 => fixed

	RLCMsgPacketUplinkAssignmentDynamicAllocationElt mDynamicAllocation;
	RLCMsgPacketUplinkAssignmentDynamicAllocationElt *setDynamicAllocation() {
		mAllocationType = 1;
		return &mDynamicAllocation;
	}
	
	RLCMsgPacketUplinkAssignment(TBF *tbf)
		: RLCDownlinkMessage(RLCDownlinkMessage::PacketUplinkAssignment,true,tbf),
		mChannelCodingCommand(tbf->mtChannelCoding())
		//mChannelCodingCommand(ChannelCodingCS1)
	{ 
		// Blackberry was using CS-1 when commanded to use CS-4, and it shouldnt matter
		// what we put here, so try 1.  Didnt help, back to 0
		mTLLIBlockChannelCoding = 0;
	}

	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketUplinkAssignment::writeBody(MsgCommon &dst) const {
		dst.write0();	// No persistence Level field.
		// MSID Could also be TQI or Packet Request Reference in this message; we dont support them.
		mMSID.writeBody(dst);
		dst.write0();	// Message escape, means more stuff follows.
		dst.WRITE_FIELD(mChannelCodingCommand,2);
		dst.WRITE_ITEM(mTLLIBlockChannelCoding);
		mPacketTimingAdvance.writeBody(dst);
		dst.write0();	// No frequency parameters.
		dst.WRITE_ITEM(mAllocationType);
		switch (mAllocationType) {
			case 0:	// reserved.
				assert(0);
			case 1: // dynamic allocation
				mDynamicAllocation.writeBody(dst);
				break;
			case 2: // single block allocation
				assert(0);
			case 3: // fixed allocation
				assert(0);
		}
		dst.write0();	// One more 0 required to mark no Packet Extended Timing Advance.
	}
#endif

/** GSM 04.60 11.2.7 From network to MS */
// Unlike an uplink assignment there is no ChannelCoding in the downlink assignment
// because it is passed in the qbits in each radio block.
// See: PDCHL1Downlink::transmit(BitVector *mI, const int *qbits)
class RLCMsgPacketDownlinkAssignment : public RLCDownlinkMessage
{
	public:

	//Bool_z mPersistenceLevelPresent;
	//Field_z<r> mPersistenceLevel[4];		// This is for PRACH control, so we wont use it.

	RLCMsgGlobalTFIorTLLIElt mMSID;
	void setTLLI(int tlli) { mMSID.setTLLI(tlli); }

	Field_z<2> mMACMode;	// 0 == dynamic, 2 = fixed.  Not sure why this is here.
	Field_z<1> mRLCMode;	// 0 == acknowledged, 1 == unacknowleged mode.
	Field_z<1> mControlAck;	// Set if establishing a new downlink TBF and T3192 is running.
							// We only use this message to change an existing downlink,
							// so always 0.  Not sure how it could ever be 1.
	Field_z<8> mTimeslotAllocation;
	RLCMsgPacketTimingAdvanceIE mPacketTimingAdvance;
	void setTimingAdvance(int ta) { mPacketTimingAdvance.setTimingAdvance(ta); }
	// No P0, BTS_PWR_CTRL_MODE, PR_MODE.
	// No Frequence Parameters
	Bool_z mHaveDownlinkTFIAssignment;
	Field_z<5> mDownlinkTFIAssignment;	// Assigned TFI for this downlink.
	Bool_z mHavePowerControlParameters;
	RLCMsgPowerControlParametersIE mPowerControlParameters;
	// No TBF Starting Time.
	// No Measurement Mapping.

	RLCMsgPacketDownlinkAssignment(TBF *tbf, bool isNewAssignment);
	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	RLCMsgPacketDownlinkAssignment::RLCMsgPacketDownlinkAssignment(TBF *tbf,bool isNewAssignment)
		: RLCDownlinkMessage(RLCDownlinkMessage::PacketDownlinkAssignment,true,tbf)
	{
		mTimeslotAllocation = tbf->mtMS->msGetDownlinkTimeslots(MultislotSymmetric);
		// We dont track T3192, we track T3193, which is (almost) the same thing.
		mControlAck = isNewAssignment ? tbf->mtMS->msT3193.active() : 0;
		// 5-13-2012 I tried setting this to true all the time, and it seemed to make no difference
		// on the Blackberry, in particular, did not get rid of the cause=3105 retries.
		// 5-23-2012: After a 3105 failure we retry with a new TBF.  That tbf was established
		// with controlack=false for some reason, and based on its first downlinkacknack, it clearly
		// took over from the previous TBF rather than establishing a new one.
		// So I am setting this to true permanently.
		// 6-18: The above is all fixed.  The Blackberry ignores ControlAck
		// but other phones do not, so it must be set properly.

		assert(tbf->mtTFI >= 0);
		mHaveDownlinkTFIAssignment = true;
		mDownlinkTFIAssignment = tbf->mtTFI;
		mHavePowerControlParameters = true;
		mPowerControlParameters.setFrom(tbf);
	}

	void RLCMsgPacketDownlinkAssignment::writeBody(MsgCommon &dst) const {
		dst.write0();	// No persistence level yet.
		mMSID.writeBody(dst); // MS identified by either uplink or downlink TFI.
		dst.write0();	// Message escape, means more stuff follows.
		// Next comes MAC_MODE, dynamic, fixed, etc.
		// This is mostly meaningless for a downlink, and I dont know why it is here.
		// Except if assigned fixed slots, the MS can do other stuff when its not busy
		// listening to us.
		dst.WRITE_ITEM(mMACMode);
		dst.WRITE_ITEM(mRLCMode);
		dst.WRITE_ITEM(mControlAck);
		dst.WRITE_ITEM(mTimeslotAllocation);
		mPacketTimingAdvance.writeBody(dst);
		dst.write0();	// No P0, PR_MODE.
		dst.write0();	// No Frequency Parameters.
		if (dst.write01(mHaveDownlinkTFIAssignment)) {
			dst.WRITE_ITEM(mDownlinkTFIAssignment);
		}
		mPowerControlParameters.writeOptional01(dst,mHavePowerControlParameters);
		dst.write0();	// No TBF Starting time.
		dst.write0();	// No Measurement mapping.
		dst.write0();	// End of message (no EGPRS, etc, stuff.)
	}
#endif

// 44.060 11.2.31
// This message does not include a TLLI so it can not be used to initiate
// an uplink or downlink assignment.  This message includes tfi assignment
// elements, but they are only to change the tfi of an existing TBF.
// The normal uplink and downlink assignment messages can only allocate PDCH
// symmetrically, ie, 2-up/2-down, and we must use this message to change
// to any other multislot mode.
class RLCMsgPacketTimeslotReconfigure : public RLCDownlinkMessage
{
	public:
	RLCMsgGlobalTFIIE mGlobalTFI; // This is NOT a TFI assignment; it is used
							// to identify the MS which receives this message.

	// This IE has timing advance for both uplink and downlink.
	// We dont need it so unimplemented, just use three '0' bits instead.
	// GLOBAL_PACKET_TIMING_ADVANCE

	// Elements for downlink modification:
	Field_z<1> mDownlinkRlcMode;	// 0 == acknowledged, 1 == unacknowleged mode.
									// Always acknowledged for us.
	Field_z<1> mControlAck;	// Set if T3192 is running in MS.
	// DOWNLINK_TFI_ASSIGNMENT unused
	// UPLINK_TFI_ASSIGNMENT unused
	Field_z<8> mDownlinkTimeslotAllocation;

	// FrequenceParameters;	// We dont use it.

	// Elements for an uplink modification:
	ChannelCodingType mChannelCodingCommand;
	RLCMsgPacketUplinkAssignmentDynamicAllocationElt mDynamicAllocation;
	RLCMsgPacketUplinkAssignmentDynamicAllocationElt *setDynamicAllocation() {
		//mAllocationType = 1;
		return &mDynamicAllocation;
	}
	
	RLCMsgPacketTimeslotReconfigure(TBF *tbf);

	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	RLCMsgPacketTimeslotReconfigure::RLCMsgPacketTimeslotReconfigure(TBF *tbf)
		: RLCDownlinkMessage(RLCDownlinkMessage::PacketTimeslotReconfigure,true,tbf)
	{
		mGlobalTFI.setTFI(tbf->mtDir == RLCDir::Down,tbf->mtTFI);
		mDownlinkTimeslotAllocation = tbf->mtMS->msGetDownlinkTimeslots(MultislotFull);
		mChannelCodingCommand = tbf->mtChannelCoding();
		// Must not call mDynamicAllocationsetUplinkTFI()
		mDynamicAllocation.setFrom(tbf,MultislotFull);
	}
	void RLCMsgPacketTimeslotReconfigure::writeBody(MsgCommon &dst) const
	{
		// PAGE_MODE is handled by the parent class
		dst.write0();	// unlabeled extra 0 bit.
		mGlobalTFI.writeBody(dst);
		dst.write0();	// message escape: not EGPRS
		dst.WRITE_FIELD(mChannelCodingCommand,2);
		dst.writeField(0,3);	// Skip Global Packet Timing Advance
		dst.WRITE_ITEM(mDownlinkRlcMode);
		dst.WRITE_ITEM(mControlAck);
		dst.write0();			// no downlink tfi assignment
		dst.write0();			// no uplink tfi assignment
		dst.WRITE_ITEM(mDownlinkTimeslotAllocation);
		dst.write0();			// no frequence parameters
		dst.write0();			// just an extra 0 bit.
		mDynamicAllocation.writeBody(dst);
		dst.write0();			// Marks end of message, no extensions.
	}
#endif

/** GSM 04.60 11.2.1 From network to MS */
class RLCMsgPacketAccessReject : public RLCDownlinkMessage
{
	public:
	Field_z<32> mTLLI;
	// GlobalTFI mGlobalTFI;	We dont use.
	Bool_z mHaveWaitIndication;
	Field_z<8> mWaitIndication;
	Field_z<1> mWaitIndicationSize;
	
	RLCMsgPacketAccessReject(TBF *tbf)
		: RLCDownlinkMessage(RLCDownlinkMessage::PacketAccessReject,false,NULL),
		mTLLI(tbf->mtMS->msTlli)
	{ }

	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketAccessReject::writeBody(MsgCommon &dst) const {
		dst.write1();	// Start of reject struct.
		dst.write0();	// Indicate we have a TLLI
		dst.writeField(mTLLI,32,"TLLI",tohex);
		if (dst.write01(mHaveWaitIndication)) {
			dst.WRITE_ITEM(mWaitIndication);
			dst.WRITE_ITEM(mWaitIndicationSize);
		}
		dst.write0();	// No more reject structs.
	}
#endif

/** GSM 04.60 11.2.26 From network to MS */
class RLCMsgPacketTBFRelease : public RLCDownlinkMessage
{
	public:
	RLCMsgGlobalTFIIE mGlobalTFI;
	Field_z<1> mUplinkRelease;
	Field_z<1> mDownlinkRelease;
	Field_z<4> mTBF_RELEASE_CAUSE;
	RLCMsgPacketTBFRelease(TBF *tbf);
	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	RLCMsgPacketTBFRelease::RLCMsgPacketTBFRelease(TBF *tbf)
		: RLCDownlinkMessage(RLCDownlinkMessage::PacketTBFRelease,true,tbf)
	{
		assert(tbf->mtTFI != -1);	// TBF has not been released yet.
		mGlobalTFI.mGTFI = tbf->mtTFI;
		if (tbf->mtDir == RLCDir::Down) {
			mGlobalTFI.mIsDownlinkTFI = true;
			mDownlinkRelease = true;
		} else {
			mGlobalTFI.mIsDownlinkTFI = false;
			mUplinkRelease = true;
		}
		mTBF_RELEASE_CAUSE = 2;	// Abnormal release, the only reason we will ever do this.
	}
	void RLCMsgPacketTBFRelease::writeBody(MsgCommon &dst) const {
		dst.write0();		// Just an extra 0 in the spec.
		mGlobalTFI.writeBody(dst);
		dst.WRITE_ITEM(mUplinkRelease);
		dst.WRITE_ITEM(mDownlinkRelease);
		dst.WRITE_ITEM(mTBF_RELEASE_CAUSE);
	}
#endif


/** GSM 04.60 11.2.28 From network to MS */
// Note that description in the GSM documentation is indented out of sync
// with the options; be careful reading it.
class RLCMsgPacketUplinkAckNack : public RLCDownlinkMessage
{
	private:
	Field_z<5> mUplinkTFI;
	ChannelCodingType mChannelCodingCommand;
	RLCMsgPacketAckNackDescriptionIE mAND;
	// 12.12b: Allows larger timing advance values. We wont use.
	Bool_z mPacketExtendedTimingAdvancePresent;
	Field_z<2> mPacketExtendedTimingAdvance;
	// 4.60 9.2.3.4 Describes TBF_EST use.
	// For dynamic uplink (which we use) the MS may return either a ControlAcknowledgement
	// or a PacketResourceRequest in the RRBP reservation to this message.
	Field<1> mTBFEst;		// If true, MS may request a new uplink TBF in the response Packet Control Acknowledgment.
							// It helps avoid raches in the case where there is an
							// uplink and no downlink.

	// The contention resolution TLLI is needed for one-phase uplink access, for which
	// the network does not know the TLLI when it does the uplink resource assignment,
	// and has to get it from the uplink data; this specifies that the necessary
	// TLLI is inside this block somewhere.
	// We arent using this.
	//bool mHaveContentionResolutionTLLI;
	//unsigned mContentionResolutionTLLI;

	// We dont need to supply timing advance or power control in this message,
	// because we supplied these in the assignment message.
	Bool_z mHavePacketTimingAdvance;
	RLCMsgPacketTimingAdvanceIE mPacketTimingAdvance;
	void setTimingAdvance(int ta) {
		mHavePacketTimingAdvance = true;
		mPacketTimingAdvance.setTimingAdvance(ta);
	}

	Bool_z mHavePowerControlParameters;
	RLCMsgPowerControlParametersIE mPowerControlParameters;

	public:
	RLCMsgPacketUplinkAckNack(TBF *wtbf, const RLCMsgPacketAckNackDescriptionIE& wAND);
	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	RLCMsgPacketUplinkAckNack::RLCMsgPacketUplinkAckNack(
		TBF *wtbf, const RLCMsgPacketAckNackDescriptionIE& wAND)
		: RLCDownlinkMessage(RLCDownlinkMessage::PacketUplinkAckNack,false,wtbf),
		mUplinkTFI(wtbf->mtTFI),
		mChannelCodingCommand(wtbf->mtChannelCoding()),
		mAND(wAND),
		// 6-26-2012: set TBF_EST true by default.
		mTBFEst(gConfig.getBool("GPRS.TBF.EST")) // Allow MS to request another uplink assignment instead of Packet Control Acknowledgment in RRBP response.
	{
		assert(wtbf->mtDir == RLCDir::Up);
		setTimingAdvance(wtbf->mtMS->msGetTA());
	}
	void RLCMsgPacketUplinkAckNack::writeBody(MsgCommon &dst) const {
		dst.write0();	// two extra 0 after PAGE_MODE.
		dst.write0();
		dst.WRITE_ITEM(mUplinkTFI);
		dst.write0();	// message escape - indicates normal (non-EGPRS) message.
		dst.WRITE_FIELD(mChannelCodingCommand,2);
		mAND.writeBody(dst);
		dst.write0();	// no CONTENTION_RESOLUTION_TLLI
		if (dst.write01(mHavePacketTimingAdvance)) {
			mPacketTimingAdvance.writeBody(dst);
		}
		if (dst.write01(mHavePowerControlParameters)) {
			mPowerControlParameters.writeBody(dst);
		}
		dst.write0();	// no Extension Bits.
		dst.write0();	// no Fixed Allocation Parameters.
		// Note that the indentation in the documentation here is misleading.
		dst.write1();	// Enable additions for R99
		if (dst.write01(mPacketExtendedTimingAdvance)) {
			dst.WRITE_ITEM(mPacketExtendedTimingAdvance);
		}
		dst.WRITE_ITEM(mTBFEst);
	}
#endif

// GSM04.60 11.2.8 From network to MS
struct RLCMsgPacketDownlinkDummyControlBlock : public RLCDownlinkMessage
{
	//Field_z<1> mPersistenceLevelPresent;
	//Field_z<4> mPersistenceLevel[4];		// This is for PRACH control, so we wont use it.

	RLCMsgPacketDownlinkDummyControlBlock()
		: RLCDownlinkMessage(RLCDownlinkMessage::PacketDownlinkDummyControlBlock,false,NULL)
	{ RN_MEMCHKNEW(RLCMsgPacketDownlinkDummyControlBlock) }

	~RLCMsgPacketDownlinkDummyControlBlock() { RN_MEMCHKDEL(RLCMsgPacketDownlinkDummyControlBlock) }

	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketDownlinkDummyControlBlock::writeBody(MsgCommon &dst) const {
		dst.write0();	// No Persistence Level
	}
#endif

///** A factory function for uplink messages */
//RLCUplinkMessage *RLCUplinkFactory(RLCUplinkMessage::MTI);

///** Generic RLC uplink parser */
extern RLCUplinkMessage* RLCUplinkMessageParse(RLCRawBlock *src);


/** GSM 04.60 11.2.13 From network to MS */
// Note that description in the GSM documentation is indented out of sync
// with the options; be careful reading it.
class RLCMsgPacketPowerControlTimingAdvance : public RLCDownlinkMessage
{
	// The MS may be identified by GlobalTFI, TQI, or PacketRequestReference,
	// but we only use GlobalTFI, so I left the other methods out:
	Bool_z mHaveGlobalTFI;	// Better be true.
	RLCMsgGlobalTFIIE mGlobalTFI;				// 12.10
	// Global Power Control Parameters not implemented:
	// Bool_z mHaveGlobalPowerControlParameters;
	// RLCMsgGlobalPowerControlParametersIE mPowerControlParameters;	// 12.9
	Bool_z mHavePowerControlParameters;
	RLCMsgPowerControlParametersIE mPowerControlParameters;	// 12.9
	Bool_z mHaveGlobalTimingAdvance;
	RLCMsgPacketGlobalTimingAdvanceIE mGlobalTimingAdvance; // 12.12a

	// 12.12b: Allows larger timing advance values.
	Bool_z mPacketExtendedTimingAdvancePresent;
	Field_z<2> mPacketExtendedTimingAdvance;
	public:
	RLCMsgPacketPowerControlTimingAdvance(TBF *wtbf)
		: RLCDownlinkMessage(RLCDownlinkMessage::PacketPowerControlTimingAdvance,false,wtbf)
	{
		assert(wtbf->mtTFI >= 0);
		setGlobalTFI(wtbf->mtDir == RLCDir::Down,wtbf->mtTFI);
		setTimingAdvance(wtbf->mtMS->msGetTA());
	}

	void setTimingAdvance(int ta) {
		mHaveGlobalTimingAdvance = true;
		mGlobalTimingAdvance.setTimingAdvance(ta);
	}
	void setGlobalTFI(bool wIsDownlinkTFI,int wTFI) {
		mHaveGlobalTFI = 1; mGlobalTFI.setTFI(wIsDownlinkTFI,wTFI);
	}
	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketPowerControlTimingAdvance::writeBody(MsgCommon &dst) const {
		// page_mode is written by RLCDownlinkMessage writeHeader()
		if (mHaveGlobalTFI) {
			dst.write0();
			mGlobalTFI.writeBody(dst);
		} else {
			assert(0);	// Other MS ID methods not supported.
		}
		dst.write0();	// message escape.
		dst.write0();	// No Global Power Parameters.
		if (mHaveGlobalTimingAdvance && mHavePowerControlParameters) {
			dst.write0();
			mGlobalTimingAdvance.writeBody(dst);
			mPowerControlParameters.writeBody(dst);
		} else {
			dst.write1();
			if (mHaveGlobalTimingAdvance) {
				dst.write0();
				mGlobalTimingAdvance.writeBody(dst);
			} else {
				dst.write1();
				mPowerControlParameters.writeBody(dst);
			}
		}
		// The following is confusing in the spec because
		// the indentation is misleading.
		// Look carefully and note that there is no ending
		// curly brace on the line  " null | 0 bit"
		if (dst.write01(mPacketExtendedTimingAdvancePresent)) {
			if (dst.write01(mPacketExtendedTimingAdvancePresent)) {
				dst.WRITE_ITEM(mPacketExtendedTimingAdvance);
			}
		}
	}
#endif

// GSM04.60 11.2.9b.
// When you set the NETWORK_CONTROL_ORDER to NC2 (network does cell reselection)
// then the MS sends measurement reports at the reporting periods defined below.
// These messages clog up our very limited ACCH.
// This message is one of the ways to change the reporting period.
// I'm not sure there is any other way for us.
class RLCMsgPacketMeasurementOrder : public RLCDownlinkMessage
{
	RLCMsgGlobalTFIorTLLIElt mMSID;
	void setTLLI(int tlli) { mMSID.setTLLI(tlli); }
	void setGlobalTFI(bool wIsDownlinkTFI,int wTFI) {
		mMSID.setGlobalTFI(wIsDownlinkTFI,wTFI);
	}

	Field_z<3> mPMOIndex;
	Field_z<3> mPMOCount;
	Bool_z mHaveNCMeasurementParameters;
	Field<2> mNetworkControlOrder;
	Field<3> mNCNonDrxPeriod;	// Non-drx period after MS sends a measurement.
	// Reporting period is documented in PSI5
	// It is a 3 bit code.  Larger values mean longer times.
	// Min is 0.48 sec and max is 61.44 sec.
	Field<3> mNCReportingPeriodI;	// GPRS default is 7 == 61.44 sec.
	Field<3> mNCReportingPeriodT;	// GPRS default is 3 == 3.84 sec.

	RLCMsgPacketMeasurementOrder(TBF*wtbf) :
		RLCDownlinkMessage(RLCDownlinkMessage::PacketMeasurementOrder,false,wtbf),
		mNetworkControlOrder(2),
		mNCNonDrxPeriod(0),
		mNCReportingPeriodI(7),	// Max these out
		mNCReportingPeriodT(7)
	{}
	void writeBody(MsgCommon&dst) const;
};
#if RLCMESSAGES_IMPLEMENTATION
	void RLCMsgPacketMeasurementOrder::writeBody(MsgCommon &dst) const {
		// page_mode is written by RLCDownlinkMessage writeHeader()
		mMSID.writeBody(dst); // MS identified by either uplink or downlink TFI.
		dst.WRITE_ITEM(mPMOIndex);
		dst.WRITE_ITEM(mPMOCount);
		if (dst.write01(mHaveNCMeasurementParameters)) {
			dst.WRITE_ITEM(mNetworkControlOrder);
			dst.write1();	// We always include this optional section.
			dst.WRITE_ITEM(mNCNonDrxPeriod);
			dst.WRITE_ITEM(mNCReportingPeriodI);
			dst.WRITE_ITEM(mNCReportingPeriodT);
			dst.write0(); // No NC_FREQUENCY_LIST.
		}
		dst.write0();	// No EXT Measurement Parameters.
		dst.write0();	// No optional stuff follows.
	}
#endif


}; // namespace GPRS
#endif
