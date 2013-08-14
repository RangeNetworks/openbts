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

#ifndef BSSGMESSAGES_H
#define BSSGMESSAGES_H


// This file includes both the NS and BSSG layer messages.

// For a downlink PDU, we know the size when we receive it.
// For an uplink data PDU, which comes from RLCEngine, we will allocate the
// maximum size right off the bat, and let the RLCEngine write the data
// directly into it.  There is no real reason to downsize it; these dont last long.

namespace GPRS { extern unsigned GPRSDebug; };
#include "GPRSExport.h"

#include "Defines.h"
#include "ScalarTypes.h"
#include "ByteVector.h"
#include "Utils.h"
//#ifndef OFFSETOF	// This is a standard definition in many header files.
//#define OFFSETOF(type, field) ((int) ((char *) &((type *) 0)->field))
//#endif

namespace BSSG {

// BVCI defined GSM 08.18 5.4.1
// See table 5.4 for which BVCI to use with each message.
// The SIGNALLING and PTP numbers are reserved.
// See LookupBVCI(BSPDUType::type bstype);
class BVCI {
	public:
	enum type {
		SIGNALLING = 0,	// For BSSG BVC messages other than data.
		PTM = 1,	// Point to multipoint
		PTP = 2		// Any other value is a base station designator for Point-To-Point data.
					// We only use one value, gBSSG.mbsBVCI, which must be >- PTP.
	};
};

/**********************************************************
  BSSGSP Messages we need to support eventually:
    DL-UNITDATA
      Includes PDU type (DL-UNITDATA), TLLI, QoS Profile, PDU Lifetime, PDU.
      optional: IMSI, oldTLLI, PFI (Packet Flow Identifier), etc.
    UL-UNITDATA
      Includes PDU type (UL-UNITDATA), TLLI, BVCI, Cell Identifier, PDU.
    GMM-PAGING-PS/GMM-PAGING-CS (for packet or voice)
      Includes PDU type (PAGING-PS), QoS Profile, P-TMSI <or> IMSI.
      Note: If TLLI is specified and already exists within a Radio Context in BSS
      [because MS has communicated previously] it is used.

      BVCI <or> Location Area <or> Routing Area <or> BSSArea Indication
      Optional: P-TMSI, BVCI, Location area, Routing area.
    GMM-RA-CAPABILITY, GMM-RA-CAPABILITY-UPDATE
      Astonishingly, the BSS asks the SGSN for this info.
	  It is because the MS may be moving from BTS to BTS, so the SGSN
	  is a slightly more permanent repository, and makes handover easier between BTSs.
	  But note that the MS can also travel from SGSN to SGSN, so I think the location
	  of the MS info is arbitrary.
	  We are talking about information that is only kept around a short while anyway.
    GMM-RADIO-STATUS
    GMM-SUSPEND
    GMM-RESUME

 Note that there are alot of messages to control the data-rate on the BSSG connection.
 None of them are implemented in the SGSN, so we dont support them either.
**********************************************************/

class BSPDUType {
	public:
	// GSM08.18 sec11.3.26 table11.27: PDU Types
	// GSM08.18 table 5.4 defines the BVCI to be used with each message.
	// BVCIs are: PTP, PTM, SIG
	enum type {
		// PDUs between RL and BSSGP SAPs:
		DL_UNITDATA = 0,	// PTP network->MS
		UL_UNITDATA = 1,	// PTP MS->network
		// PDUs between GMM SAPs.
		RA_CAPABILITY = 2,	// PTP network->BSS
		PTM_UNITDATA = 3,	// PTM not currently used
		// PDUs between GMM SAPs:
		PAGING_PS = 6,	// PTP or SIG network->BSS request to page MS for packet connection.
		PAGING_CS = 7,	// PTP or SIG network->BSS request to page MS for RR connection.
		RA_CAPABILITY_UPDATE = 8,	// PTP BSS->network request for MS Radio Access Capabilities.
		RA_CAPABILITY_UPDATE_ACK = 9,// PTP network->BSS Radio Access Capability and IMSI.
		RADIO_STATUS = 0x0a,	// PTP BSS->SGSN notification of error

		SUSPEND = 0x0b,			// SIG MS->network request to suspend GPRS service.
		SUSPEND_ACK = 0x0c,		// SIG network->MS ACK
		SUSPEND_NACK = 0x0d,	// SIG network->MS NACK
		RESUME = 0xe,			// SIG MS->network request to resume GPRS service.
		RESUME_ACK = 0xf,		// SIG network->MS ACK
		RESUME_NACK = 0x10,		// SIG network->MS NACK
		// PDUs between NM SAPs:
		// We will not use the flow control stuff, block, unblock, etc.
		BVC_BLOCK = 0x20,		// SIG
		BVC_BLOCK_ACK = 0x21,	// SIG
		BVC_RESET = 0x22,		// SIG network->BSS request reset everything.
		BVC_RESET_ACK = 0x23,	// SIG BSS->network and network->BSS?
		BVC_UNBLOCK = 0x24,		// SIG
		BVC_UNBLOCK_ACK = 0x25,		// SIG
		FLOW_CONTROL_BVC = 0x26,	// PTP BSS->network inform maximum throughput on Gb I/F
		FLOW_CONTROL_BVC_ACK = 0x27, // PTP network->BSS
		FLOW_CONTROL_MS = 0x28,		// PTP BSS->network inform maximum throughput for MS.
		FLOW_CONTROL_MS_ACK = 0x29,	// PTP network->BSS
		FLUSH_LL = 0x2a,		// SIG network->BSS forget this MS (it moved to another cell.)
		FLUSH_LL_ACK = 0x2b,	// SIG BSS->network
		LLC_DISCARDED = 0x2c,	// SIG BSS->network notification of lost PDUs (probably expired)
		// We ignore all these:
		SGSN_INVOKE_TRACE = 0x40,	// network->BSS request trace an MS
		STATUS = 0x41,			// SIG BSS->network or network->BSS report error condition.
		DOWNLOAD_BSS_PFC = 0x50,	// PTP
		CREATE_BSS_PFC = 0x51,	// PTP
		CREATE_BSS_PFC_ACK = 0x52,	// PTP
		CREATE_BSS_PFC_NACK = 0x53,	// PTP
		MODIFY_BSS_PFC = 0x54,	// PTP
		MODIFY_BSS_PFC_ACK = 0x55,	// PTP
		DELETE_BSS_PFC = 0x56,	// PTP
		DELETE_BSS_PFC_ACK = 0x57 	// PTP
	};
	static const char *name(int val);
	static const unsigned LookupBVCI(BSPDUType::type bstype);
};
std::ostream& operator<<(std::ostream& os, const BSPDUType::type val);

class NsIEIType {
	// GSM08.18 sec10.3 NS protocol IEI Types.
	// The NS protocol doesnt do much.   It specifies a procedure
	// to make sure the link is alive, to reset it after failure,
	// and to turn the entire link on and off (block/unblock.)
	public: enum type {
		IEINsCause,
		IEINsVCI,
		IEINsPDU,
		IEINsBVCI,
		IEINsNSEI,
	};
};

class NsCause {
	public: enum type {
		TransitNetworkFailure,
		OAndMIntervention,
		EquipmentFailure,
		NSVCBlocked,
		NSVCUnknown,
		BVCIUnknown,
		SemanticallyIncorrectPDU = 8,
		PduNotCompatible = 10,
		ProtocolError = 11,
		InvalidEssentialIE = 12,
		MissingEssentialIE = 13
	};
};

class IEIType {
	public:
	// GSM08.18 sec11.3 table11.1: IEI Types
	enum type {
		AlignmentOctets = 0x00,
		BmaxDefaultMS = 0x01,
		BSSAreaIndication = 0x02,
		BucketLeakRate = 0x03,
		BVCI = 0x04,
		BVCBucketSize = 0x05,
		BVCMeasurement = 0x06,
		Cause = 0x07,
		CellIdentifier = 0x08,
		ChannelNeeded = 0x09,
		DRXParameters = 0x0a,
		eMLPPPriority = 0x0b,
		FlushAction = 0x0c,
		IMSI = 0x0d,
		LLCPDU = 0x0e,
		LLCFramesDiscarded = 0x0f,
		LocationArea = 0x10,
		MobileId = 0x11,
		MSBucketSize = 0x12,
		MSRadioAccessCapability = 0x13,
		OMCId = 0x14,
		PDUInError = 0x15,
		PDULifetime = 0x16,
		Priority = 0x17,
		QoSProfile = 0x18,
		RadioCause = 0x19,
		RACapUPDCause = 0x1a,
		RouteingArea = 0x1b,
		RDefaultMS = 0x1c,
		SuspendReferenceNumber = 0x1d,
		Tag = 0x1e,
		TLLI = 0x1f,
		TMSI = 0x20,
		TraceReference = 0x21,
		TraceType = 0x22,
		TransactionId = 0x23,
		TriggerId = 0x24,
		NumberOfOctetsAffected = 0x25,
		LSAIdentifierList = 0x26,
		LSAInformation = 0x27,
		PacketFlowIdentifier = 0x28,
		PacketFlowTimer = 0x29,
		AggregateBSSQoSProfile = 0x3a, // (ABQP) 
		FeatureBitmap = 0x3b,
		BucketFullRatio = 0x3c,
		ServiceUTRANCCO = 0x3d			// (Cell Change Order) 
	};
	static const char *name(int val);

};
std::ostream& operator<<(std::ostream& os, const IEIType::type val);

// Notes:
// BVC = BSSG Virtual Connection
// NS SDU = the BSSG data packet transmitted over NS.

// GSM08.16 sec 10.3.7: Network Service PDU Type
class NSPDUType {
	public:
	enum type {
		NS_UNITDATA = 0,
		NS_RESET = 2,
		NS_RESET_ACK = 3,
		NS_BLOCK = 4,
		NS_BLOCK_ACK = 5,
		NS_UNBLOCK = 6,
		NS_UNBLOCK_ACK = 7,
		NS_STATUS = 8,
		NS_ALIVE = 10,
		NS_ALIVE_ACK = 11
	};
	static const char *name(int val);
};
std::ostream& operator<<(std::ostream& os, const NSPDUType::type val);

// GSM08.16 sec 9.2.10
struct RN_PACKED NSUnitDataHeader {
	ByteType mNSPDUType;
	ByteType mUnused;
	ByteType mBVCI[2];
};

class NSMsg : public ByteVector, public Utils::Text2Str
{
	public:
	// This is the NS header length only for NS_UNIT_DATA messages,
	// which are the only ones we care about because they are the
	// only ones that have BSSG and potentially user data in them.
	static const unsigned UnitDataHeaderLen = sizeof(struct NSUnitDataHeader);

	// wlen should include the NS header + BSSG header + data
	NSMsg(ByteType *wdata, int wlen) // Make one from downlink data.
		: ByteVector(wdata,wlen)
	{ }

	// wlen should include the NS header + BSSG header + data
	NSMsg(int wlen) 		// Make one for uplink data.
		: ByteVector(wlen + NSMsg::UnitDataHeaderLen)	// But we will add it in anyway
	{
		// Zero out the NS header; the type will be filled in later.
		fill(0,0,NSMsg::UnitDataHeaderLen);
	}

	// Make a new message from some other, taking over the ByteVector.
	// Used to change the type of a BSSG message.
	NSMsg(NSMsg *src) : ByteVector(*src) {
		//assert(src->isOwner());
		//move(*src); 	// Grab the memory from src.
		//assert(!src->isOwner());	no longer true with refcnts.
	}

// Passify the brain-dead compiler:
#define NSMsgConstructors(type1,type2) \
	type1(ByteType *data, int len) : type2(data,len) {} \
	type1(int wlen) : type2(wlen) {} \
	type1(NSMsg *src) : type2(src) {}

	// Fields in the 4 byte NS Header:
	void setNSPDUType(NSPDUType::type nstype) { setByte(0,nstype); }
	NSPDUType::type getNSPDUType() const { return (NSPDUType::type) getByte(0); }

	void textNSHeader(std::ostream&os) const;
	virtual void text(std::ostream&os) const;
	std::string str() const { return this->Text2Str::str(); } // Disambigute
};

class BSSGMsg : public NSMsg {
	public:
	NSMsgConstructors(BSSGMsg,NSMsg)
	// Common fields in the BSSG Header:
	void setPDUType(BSPDUType::type type) { setByte(NSMsg::UnitDataHeaderLen,(ByteType)type); }
	BSPDUType::type getPDUType() const { return (BSPDUType::type) getByte(NSMsg::UnitDataHeaderLen); }
	virtual void text(std::ostream &os) const;
	virtual std::string briefDescription() const;
};

class BSSGUplinkMsgElt {
	public:
	//TODO: virtual void text(std::ostream&) const;
	virtual void parseElement(const char *src, size_t &rp);
};
class BSSGDownlinkMsgElt {
	public:
	//TODO: virtual void text(std::ostream&) const;
};

// Note that the ByteVector in NSMsg is allocated, and all the other ones in downlink messages
// are segments referring to this one.
//class BSSGDownlinkMsg : public NSMsg, public BSSGMsg
class BSSGDownlinkMsg : public BSSGMsg
{
	public:
	NSMsgConstructors(BSSGDownlinkMsg,BSSGMsg)

	virtual void text(std::ostream &os) const { BSSGMsg::text(os); }
};

class BSSGUplinkMsg : public BSSGMsg
{
	public:
	NSMsgConstructors(BSSGUplinkMsg,BSSGMsg)

	virtual void text(std::ostream &os) const { BSSGMsg::text(os); }
};


// This is the QoS Profile in the header, when not including the 2 byte IEI prefix.
// GSM08.18 sec 11.3.28
struct QoSProfile {
	// Coded as value part of Bucket Leak Rate 'R' from sec 11.3.4
	// And I quote:
	// The R field is the binary encoding of the rate information expressed in 100 bits/sec
	// increments starting from 0 x 100 bits/sec until 65535 * 100 bits/sec (6Mbps)
	// Note a) Bit Rate 0 means "Best Effort".
	unsigned mPeakBitRate:16;

	// These are the bits of byte 3.
	unsigned mSpare:2;
	unsigned mCR:1;
	unsigned mT:1;
	unsigned mA:1;
	unsigned mPrecedence:3;

	ByteType getB3() { return (mCR<<5)|(mT<<4)|(mA<<3)|mPrecedence; }
	void setB3(ByteType bits) {
		mPrecedence = bits & 0x7;
		mA = (bits>>3)&1;
		mT = (bits>>4)&1;
		mCR = (bits>>5)&1;
	}

	QoSProfile() {	// Create a default QoSProfile
		mPeakBitRate = 0;
		setB3(0);
	}

	// Get from the ByteVector, which is in network order:
	void qosRead(ByteVector &src, size_t &wp) {
		mPeakBitRate = src.getUInt16(wp); wp+=2;
		ByteType byte3 = src.getByte(wp++);
		setB3(byte3);
	}

	// Write to ByteVector in network order.
	void qosAppend(ByteVector *dest) {
		dest->appendUInt16(mPeakBitRate);
		dest->appendByte(getB3());
	}
};


// GSM 08.18 sec 10.2.1  From SGSN to BSS.
class BSSGMsgDLUnitData : public BSSGDownlinkMsg
{
	public:
	// Mandatory elements:
	UInt32_z mbdTLLI;
	UInt16_z mbdPDULifetime;
	QoSProfile mbdQoS;		// 3 bytes
	Bool_z mbdHaveOldTLLI;
	UInt32_z mbdOldTLLI;
	// Optional elements:
	//RLCMsgEltMSRACapabilityValuePart mbdRACap;
	ByteVector mbdRACap;
	ByteVector mbdIMSI;
	ByteVector mbdPDU;

	BSSGMsgDLUnitData(BSSGDownlinkMsg*src) : BSSGDownlinkMsg(src) {
		size_t wp = NSMsg::UnitDataHeaderLen;
		parseDLUnitDataBody(*this,wp);
	}

	// Parse body, excluding the NS header.
	void parseDLUnitDataBody(ByteVector &src, size_t &wp);
	void text(std::ostream &os) const;
	std::string briefDescription() const;
};

BSSGDownlinkMsg* BSSGDownlinkMessageParse(ByteVector&src);

// Routing Area Identification: GSM04.08 sec 10.5.5.15.
// 6 bytes.  This is just a magic cookie as far as we are concerned.
struct RN_PACKED RoutingAreaID {
	// First three bytes identify MCC Mobile Country Code and MNC Mobile Network Code.
	// Weird encoding; see spec.
	ByteType mMCCDigits12;	// MCC Digit2, MCC Digit1.
	ByteType mHighDigits;	// MNC Digit3, MCC Digit3.
	ByteType mMNCDigits12;	// MNC Digit2, MNC Digit1.
	unsigned mLAC:16;	// Location Area Code.
	unsigned mRAC:8;	// Routing Area Code.
	RoutingAreaID(unsigned MCC, unsigned MNC, unsigned LAC, unsigned RAC) {
		// TODO, but maybe no one cares.
	}
};


// Cell Identifier: GSM 08.18 sec 11.2.9
struct RN_PACKED CellIdentifier {
	// Routing Area Identification: GSM 04.08 sec 10.5.5.15.
	uint64_t RoutingAreaIdentification:48;
	// Cell Identity: GSM04.08 sec 10.5.1.1
	// It is just a two byte int whose value determined by the administrator.
	unsigned CellIdentity:16;
};

struct RN_PACKED CellIdentifierIEI {
	ByteType iei;
	ByteType length;
	ByteType RoutingAreaID[6];
	ByteType CellIdentity[2];
};

// This is the specific UlUnitData format that we use.
struct RN_PACKED BSSGMsgULUnitDataHeader {
	NSUnitDataHeader mbuNS;
	ByteType mbuPDUType;
	ByteType mbuTLLI[4];
	ByteType mbuQoS[3];
	ByteType mbuCellIdIEI[10];
	ByteType mbuAlignmentIEI[2];	// spacer required to make size div by 4.
	ByteType mLLCIEIType;
	ByteType mLLCPDULength[2];
	// PDU data starts after this..
};

//class BSSGMsgBVCReset {
//	NSUnitDataHeader mbuNS;
//};

// GSM 08.18 sec 10.2.2  Packet Data from BSS to SGSN.
// It could be user data or a GMM or other message from a higher
// layer in the MS to the SGSN.
// There are several optional IEIs we do not include.
// Alignment octets are necessary so the start of the PDU IEI
// starts on a 32bit boundary, which is totally dopey because the PDU
// itself is unaligned inside the PDU IEI.  Whatever.
// This structure is write-only; the internal fields are in network order and
// we dont need to read them, so we dont.
// The RLCEngine is the primary producer of these things, and always allocates
// a maximum size (1502 bytes plus headers) so it can just append the data in.
// The lifetime of these things is quite short; they live in the BSSG outgoing
// queue until the service thread sends them to the SGSN.
class BSSGMsgULUnitData : public BSSGUplinkMsg
{
	public:
	static const unsigned HeaderLength = sizeof(BSSGMsgULUnitDataHeader);
	int mTBFId;	// For debugging, the associated tbf that processed us.

	BSSGMsgULUnitData(unsigned wLen, uint32_t wTLLI);

	// Return the pointer to the header within the ByteVector (its just 0)
	// what a horrible language.
	BSSGMsgULUnitDataHeader *getHeader() { return (BSSGMsgULUnitDataHeader*) begin(); }
	BSSGMsgULUnitDataHeader *getHeader() const { return const_cast<BSSGMsgULUnitData*>(this)->getHeader(); }

	//unused: void setBVCI(int wBVCI) { setUInt16(2,wBVCI); }
	unsigned getBVCI() const { return getUInt16(2); }
	//unused: void setTLLI(int wTLLI) { sethtonl(getHeader()->mbuTLLI,wTLLI); }
	int getTLLI() const { return getntohl(getHeader()->mbuTLLI); }

	ByteVector getPayload() {
		// Now the return value also owns memory.
		//return tail(HeaderLength);
		ByteVector result = *this;	// Increments refcnt.
		result.trimLeft(HeaderLength);
		return result;
	}

	// We set the length after assembling the complete pdu.
	unsigned mLengthPosition;	// Where the PDU Length goes in the ByteVector.
	void setLength();

	void text(std::ostream&os) const;
};

// Make a short BSSG Protocol signaling message.
BSSGUplinkMsg *BVCFactory(BSPDUType::type bssgtype, int arg1=0);
// Make a short NS Protocol message.
NSMsg *NsFactory(NSPDUType::type nstype, int cause=0);

//class BSSGMsgULUnitData : public BSSGUplinkMsg {
//	unsigned mbPDUType:8;
//	unsigned mTLLI:32;
//	QoSProfile mQoS;
//	CellIdentifierIEI mCellIdentifier;	// 10 bytes
//};

};
#endif
