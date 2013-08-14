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

#ifndef _GPRSL3MESSAGES_H_
#define _GPRSL3MESSAGES_H_

#include <stdio.h>
//#include "GPRSInternal.h"
#include "Configuration.h"
#include "GSMCommon.h"
#include "GSML3Message.h"
#include "ByteVector.h"
#include "Utils.h"
#include "ScalarTypes.h"
#include "SgsnBase.h"
//#include "GPRSL3New.h"
extern ConfigurationTable gConfig;
using namespace std;
#define DEHEXIFY(c) ((c)>=10 ? (c)-10+'a' : (c)+'0')

namespace SGSN {
struct LlcEntity;

// 10.5.5.18 Update Type uses all four values.
// 10.5.5.17 Update Result uses the first two values.
// RA is for GPRS, and LA is for GSM CS [circuit switched].
enum RAUpdateType {
	RAUpdated = 0, CombinedRALAUpdated = 1, CombinedRALAWithImsiAttach = 2, PeriodicUpdating = 3
};


#if 0
// An L3Message as known by the GPRS code.
// The L3Message is not really applicable here, because we do not
// really know enough about this message to set the bodylength etc. yet.
//struct GPRSL3Message : ByteVector {
//	unsigned mSkip;
//	unsignd mPD;
//	unsigned mMITI;
//
//	// Parse out the common L3Message header
//	GPRSL3Message(ByteVector &vec) : ByteVector(vec.begin(),vec,size())
//	{
//		mSkip = getByte(0) >> 4;
//		mPD = getByte(0) & 0xf;
//		mMITI = getByte(1);
//	}
//};
#endif

// The L3Message is built on L3Frame which assumes that messages are in BitVectors,
// but for GPRS messages come from RLC (and LLC and LLE) and they are ByteVectors.
class L3GprsMsg : public Text2Str
{	public:
	virtual void text(std::ostream &os) const = 0;
	virtual int MTI() const =0;
	virtual const char *mtname() const =0;
};

// All Gmm and Sm messages need a sense that is either reply or command.
// For SM messages, the sense is encoded in the transaction-identifier field.
// For both types, the sense is also encoded in one of the LLC headers.
// Cant be be too too redundant redundant.
class L3GprsDlMsg : public virtual L3GprsMsg
{	public:
	enum MsgSense { senseInvalid, senseReply, senseCmd } mSense;
	L3GprsDlMsg(MsgSense wSense) : mSense(wSense) {}
	// This dummy constructor is used for uplink messages, when message can be both up and downlink:
	L3GprsDlMsg() : mSense(senseInvalid) {}
	// void setSense(MsgSense wSense) { mSense = wSense; }	//unused.
	bool isSenseCmd() { 
		assert(mSense != senseInvalid);
		return mSense == senseCmd;
	}
	virtual void gWrite(ByteVector &msg) = 0;
};


// An L3Frame for Gprs.
class L3GprsFrame : public ByteVector
{
	public:
	void dump(std::ostream &os);
	L3GprsFrame(ByteVector &vec) : ByteVector(vec) {}

	GSM::L3PD getPD() { return (GSM::L3PD)getNibble(0,0); }		// protocol descriminator
	//unsigned getSkip() { return getNibble(0,1); }	// skip indicator
	virtual unsigned getMsgType() { assert(0); }
	// TODO: Handle extended transaction id 24.007
	virtual unsigned getBodyOffset() { assert(0); }

	unsigned mIEI;	// cache for message parsers.

	unsigned readIEI(size_t &rp) {
		mIEI = readByte(rp);
		return mIEI;
	}

	ByteVector readLVasBV(size_t &rp) {
		int len = readByte(rp);
		ByteVector result; result.clone(segment(rp,len));
		rp += len;
		return result;
	}
	// Skip over an IE we dont care about:
	void skipLV(size_t &rp) {
		int len = readByte(rp);
		rp += len;
	}

	// The minlen and maxlen are of the IE itself, excluding to Type (if any) and Length byte.
	void skipLV(size_t &rp, int minlen, int maxlen, const char *text) {
		int len = readByte(rp);
		if (len < minlen || len > maxlen) {
			LLCWARN("unexpected message length for iei:"<<mIEI<<":"<<text);
		}
		rp += len;
	}
};

class L3GmmFrame : public L3GprsFrame
{	public:
	unsigned getSkip() { return getNibble(0,1); }	// skip indicator
	unsigned getMsgType() { return getByte(1); }
	// TODO: Handle extended transaction id 24.007
	unsigned getBodyOffset() { return 2; }	// Where message specific body begins.
	L3GmmFrame(ByteVector &vec) : L3GprsFrame(vec) {}
};

class L3SmFrame: public L3GprsFrame
{	public:
	// 24.007 11.2.3.1.3: Transaction identifier - it may have an extension byte,
	// which will also move the message type and the body location in the message.
	unsigned getTIflag() { return getField(0,1); }	// First bit in message.
	bool isTiExt() { return getField(1,3) == 7; }
	unsigned getTI() {
		unsigned ti = getField(1,3);
		if (ti == 7) {	// special value indicates extension.
			ti = getField2(1,1,7);
		}
		return ti;
	}
	// 24.008 10.4: Top 2 bits do something else.
	unsigned getMsgType() {
		return getByte(isTiExt() ? 2 : 1);
	}
	unsigned getBodyOffset() { // Where message specific body begins.
		return isTiExt() ? 3 : 2;
	}
	L3SmFrame(ByteVector &vec) : L3GprsFrame(vec) {}
};

// This is defined as an L3Message, however, the functions in L3Message
// are generally inapplicable because the transport for these messages
// uses an LLC layer wrapper inside an RLCControlMessage wrapper.
// The BTS does not worry about these, however, these are here to allow
// debug inspection of LLC packets passing through the BTS.
struct L3GmmMsg : public virtual L3GprsMsg //, public Text2Str
{
	// Protocol Discriminator GSM 04.07 11.2.3.1.1
	static const unsigned mPD = GSM::L3GPRSMobilityManagementPD; // 8
	// This should be declared static, but it is not static in L3Message.
	GSM::L3PD PD() const { return GSM::L3GPRSMobilityManagementPD; }

	// GSM 04.08, 24.008 table 10.4: Message Types for GPRS Mobility Management.
	// Note that the IdentityRequest/IdentityResponse are DIFFERENT NUMBERS
	// than the identically named messages in Table 10.2: Message Types
	// for Mobility Management.
	// Note that mobility management is handled inside the SGSN and the MS,
	// and these messages are usually transferred opaquely through the BTS,
	// so the BTS does not have to worry about this.
	enum MessageType {
		AttachRequest = 0x01,
		AttachAccept = 0x02,
		AttachComplete = 0x03,
		AttachReject = 0x04,
		DetachRequest = 0x05,
		DetachAccept = 0x06,
		RoutingAreaUpdateRequest = 0x08,
		RoutingAreaUpdateAccept = 0x09,
		RoutingAreaUpdateComplete = 0x0a,
		RoutingAreaUpdateReject = 0x0b,
		ServiceRequest = 0x0c,
		ServiceAccept = 0x0d,
		ServiceReject = 0x0e,
		PTMSIReallocationCommand = 0x10,
		PTMSIReallocationComplete = 0x11,
		AuthenticationAndCipheringReq = 0x12,
		AuthenticationAndCipheringResp = 0x13,
		AuthenticationAndCipheringRej = 0x14,
		AuthenticationAndCipheringFailure = 0x1c,
		IdentityRequest = 0x15,
		IdentityResponse = 0x16,
		GMMStatus = 0x20,
		GMMInformation = 0x21,
	};
	const char *mtname() const { return name(MTI()); }
	static const char *name(unsigned mt, bool ornull=0);
	static void dump(L3GmmFrame &frame, std::ostream &os);
	virtual void textBody(std::ostream&os) const = 0;
	void text(std::ostream&os) const;
};
std::ostream& operator<<(std::ostream& os, L3GmmMsg::MessageType mt);

class L3GmmDlMsg : public virtual L3GmmMsg, public L3GprsDlMsg
{	protected:
	virtual void gmmWriteBody(ByteVector &msg) = 0;
	public:
	L3GmmDlMsg(MsgSense wSense) : L3GprsDlMsg(wSense) {}	// For a downlink message
	L3GmmDlMsg() {}			// For uplink messages
	void gWrite(ByteVector &msg);
};

class L3GmmUlMsg : public virtual L3GmmMsg
{	protected:
	virtual void gmmParseBody(L3GmmFrame &src, size_t &rp) = 0;
	public:
	void gmmParse(L3GmmFrame &frame);
};

///@name GSM04.08 table 10.4a: Message types for GPRS session management.
// GSM 24.008 10.4 has a more complete list.
// The BTS does not worry about these, however, these are here to allow
// debug inspection of LLC packets passing through the BTS.
class L3SmMsg : public virtual L3GprsMsg //, public Text2Str
{	public:
	// Init transaction id to invalid value.  It must be specified for every downlink SM message.
	int mTransactionId;
	L3SmMsg() : mTransactionId(-1) {}

	static const unsigned mPD = GSM::L3GPRSSessionManagementPD; // 10
	GSM::L3PD PD() const { return GSM::L3GPRSSessionManagementPD; }

	enum MessageType {
		ActivatePDPContextRequest = 0x41,
		ActivatePDPContextAccept = 0x42,
		ActivatePDPContextReject = 0x43,
		RequestPDPContextActivation = 0x44,
		RequestPDPContextActivationReject = 0x45,
		DeactivatePDPContextRequest = 0x46,		// not a very symmetric naming system here.
		DeactivatePDPContextAccept = 0x47,
		ModifyPDPContextRequest = 0x48,	// network to MS direction.
		ModifyPDPContextAccept = 0x49,	// MS to netowrk direction.
		ModifyPDPContextRequestMS = 0x4a,	// MS to network direction.
		ModifyPDPContextAcceptMS = 0x4b,	// netowrk to MS direction.
		ModifyPDPContextReject = 0x4c,
		ActivateSecondaryPDPContextRequest = 0x4d,
		ActivateSecondaryPDPContextAccept = 0x4e,
		ActivateSecondaryPDPContextReject = 0x4f,

		// GSM 24.008 says: 0x50 - 0x54: "Reserved: was allocated in
		// earlier phases of the protocol"
		// But here they are anyway:
		ActivateAAPDPContextRequest = 0x50,		// Anonymous Access PDP.
		ActivateAAPDPContextAccept = 0x51,
		ActivateAAPDPContextReject = 0x52,
		DeactivateAAPDPContextRequest = 0x53,
		DeactivateAAPDPContextAccept = 0x54,

		SMStatus = 0x55,
		// From GSM 24.008:
		ActivateMBMSContextRequest = 0x56,
		ActivateMBMSContextAccept = 0x57,
		ActivateMBMSContextReject = 0x58,
		RequestMBMSContextActivation = 0x59,
		RequestMBMSContextActivationReject = 0x5a,

		RequestSecondaryPDPContextActivation = 0x5b,
		RequestSecondaryPDPContextActivationReject = 0x5c,
		Notification = 0x5d,
	};
	const char *mtname() const { return name(MTI()); }
	static const char *name(unsigned mt, bool ornull=0);	// convert mt to a string name of the message.
	static void dump(L3SmFrame &frame, std::ostream &os);	// just dump the header.  old routine.
	virtual void textBody(std::ostream&os) const = 0;
	void text(std::ostream&os) const;
};
std::ostream& operator<<(std::ostream& os, L3SmMsg::MessageType mt);

class L3SmDlMsg : public virtual L3SmMsg, public L3GprsDlMsg
{	protected:
	// All SM downlink messages must have a ti [Transaction Identifier.]
	// The assertion in smWrite guarantees that the correct constructor was called.
	L3SmDlMsg(unsigned ti,MsgSense wSense) : L3GprsDlMsg(wSense) { mTransactionId = ti; }
	// This dummy constructor is used for uplink messages, when message can be both up and downlink:
	L3SmDlMsg() {}
	void appendTiPd(ByteVector &msg);	// Append Transaction Id and PD to msg.
	virtual void smWriteBody(ByteVector &msg) = 0;
	public:
	void gWrite(ByteVector &msg);
};

class L3SmUlMsg : public virtual L3SmMsg
{	protected:
	virtual void smParseBody(L3SmFrame &src, size_t &rp) = 0;
	void smParse(L3SmFrame &frame);
};

const char *L3GprsMsgType2Name(unsigned pd, unsigned mt);
const char *L3GprsMsgType2Name(ByteVector &vec);

// 3GPP 24.008 10.5.7.3 Gprs Timer IE
// The timers themselves are in 24.008 11.2.2
struct GprsTimerIE
{
	unsigned mUnits;	// => 0 (2-sec interval) or 1 (minutes) or 2 (deci-hours) or 7 deactivated
	unsigned mValue;	// 5 bit range.
	GprsTimerIE() : mUnits(7), mValue(0) {}	// Deactivate.
	void setSeconds(unsigned numSeconds) {
		// 6-2013: 0 value now disables.
		if (numSeconds == 0) { mUnits = 7; mValue = 0; return; }
		if (numSeconds > 62) { setMinutes((numSeconds+59)/60); return; }
		mUnits = 0;				// 2-second increments.
		mValue = numSeconds/2;	//
	}
	void setMinutes(unsigned numMinutes) {
		if (numMinutes >= 200) {
			// Deactivate
			mUnits = 7; mValue = 0;
		} else if (numMinutes > 186) {
			// Use maximum value.
			mUnits = 2; mValue = 31;
		} else if (numMinutes > 31) {
			mUnits = 2;	// deci-hours
			mValue = (numMinutes+5)/6;
		} else {
			mUnits = 1;	// minutes
			mValue = numMinutes;
		}
	}
	unsigned getSeconds() const {
		switch (mUnits) {
		case 0: return mValue*2;
		case 1: return mValue*60;
		case 2: return mValue*600;
		case 7: return 0;			// special value means disabled.
		default: return 0xffffffff;	// error.
		}
	}
	unsigned getIEValue() const { return (mUnits << 5) | (mValue & 0x1f); }
	void appendElement(ByteVector &msg) { msg.appendByte(getIEValue()); }
};

// 24.008 10.5.5.15
// See also GSM::L3LocationAreaIdentity
struct GMMRoutingAreaIdIE : Text2Str
{
	unsigned char mMCC[3], mMNC[3];
	uint16_t mLAC;
	uint8_t mRAC;
	void parseElement(ByteVector &pp, size_t &rp);
	void raLoad();
	void appendElement(ByteVector &msg);
	void text(std::ostream&os) const;
	GMMRoutingAreaIdIE();			// constructor zeros mMCC and mMNC
	bool valid() { return mMCC[0] || mMCC[1] || mMCC[2]; }	// Has IE been set?
};

// 24.008 10.5.1.4 Mobile Identity
// We dont really care what the MS/UE tells us except for TMSI,
// so treat TMSI specially, and otherwise just save the whole thing.
struct GmmMobileIdentityIE : Text2Str
{
	Field_z<3> mTypeOfId;	// From the first byte.
	// Either mTmsi or mIdData+mLen is valid.
	uint32_t mTmsi;			// tmsi or ptmsi
	unsigned char mIdData[8];	// Original data from the IEI.
	unsigned mLen;		// Length of mIdData = length of IE.
	bool mPresent;
	GmmMobileIdentityIE() : mPresent(false) {}

	bool isImsi() const { return mTypeOfId == 1; }
	bool isTmsi() const { return mTypeOfId == 4; }	// TMSI or P-TMSI

	// Parse out an IMSI from the data, and return it in the ByteVector, which must have room to hold it,
	// and return true if it was found, false otherwise.
	ByteVector getImsi() const;
	uint32_t getTmsi() const { assert(isTmsi()); return mTmsi; }
	void decodeIM(ByteVector &result) const;
	void parseLV(ByteVector &pp, size_t &rp);
	const string getAsBcd() const;	// its not bcd but readable
	void text(std::ostream&os) const;
	// write the length and value, but not the IEI type.
	void appendLV(ByteVector &msg);

	void setTmsi(uint32_t tmsi) {
		mPresent = true;
		mTypeOfId = 4;
		mTmsi = tmsi;	// TMSI or P-TMSI
	}
};

// We keep only the ByteVector of the QoS IE itself,
// but the description in 24.008 of the IE numbers octets starting at 3,
// and bits 8..1, so we will do the same:
#define DEFINE_QOS_FIELD(name,octet,bitr,length) \
	unsigned get##name() { return getField2(octet-3,8-bitr,length); } \
	void set##name(unsigned val) { setField2(octet-3,8-bitr,val,length); }

// 3GPP 24.008 10.5.6.5 Quality of Service
// We leave it as a ByteVector, read the values out of it.
struct SmQoS : public ByteVector {
	// We dont fill in every bit, and the unused ones are defined as zero,
	// so just zero the whole thing before starting.
	SmQoS(ByteVector &bv) : ByteVector(bv) {}
	SmQoS(unsigned size) : ByteVector(size) {fill(0);}
	// Octet 3 (meaning first byte in the QoS ByteVector.)
	DEFINE_QOS_FIELD(DelayClass,3,6,3)
	DEFINE_QOS_FIELD(ReliabilityClass,3,3,3)
	// Octet 4:
	DEFINE_QOS_FIELD(PeakThroughputCode,4,8,4)
	DEFINE_QOS_FIELD(PrecedenceClass,4,3,3)
	// Octet 5:
	DEFINE_QOS_FIELD(MeanThroughputCode,5,5,5)
	// Octet 6:
	DEFINE_QOS_FIELD(TrafficClass,6,8,3)
	DEFINE_QOS_FIELD(DeliveryOrder,6,5,2)
	DEFINE_QOS_FIELD(DeliveryOfErrSdu,6,3,3)
	// Octet 7:
	DEFINE_QOS_FIELD(MaxSduSize,7,8,8)
	// Octet 8,9: (actual 0-based bytes number 5 and 6)
	DEFINE_QOS_FIELD(MaxBitRateUplinkCode,8,8,8)
	DEFINE_QOS_FIELD(MaxBitRateDownlinkCode,9,8,8)
	// Octet 10:
	DEFINE_QOS_FIELD(ResidualBER,10,8,4)
	DEFINE_QOS_FIELD(SduErrorRatio,10,4,4)
	// Octet 11:
	DEFINE_QOS_FIELD(TransferDelay,11,8,6)
	DEFINE_QOS_FIELD(TrafficHandlingPriority,11,2,2)
	// Octet 12,13:
	// "The Guaranteed Bit Rate is ignored if the Traffic Class is
	// Interactive or Background class or the max bit rate for downlink is 0 kBps
	// 0xff implies 0kBps, ie, unspecified.
	DEFINE_QOS_FIELD(GuaranteedBitRateUplinkCode,12,8,8)
	DEFINE_QOS_FIELD(GuaranteedBitRateDownlinkCode,13,8,8)
	// Octet 14:
	DEFINE_QOS_FIELD(SignalingIndication,14,5,1)
	DEFINE_QOS_FIELD(SourceStatisticsDescriptor,14,4,4)
	// Optional Octets 15-18 are for extented Max and Guaranteed bit-rates.

	// Encoding methods using raw methods above:

	// Maximum and Guaranteed bit rate for uplink and downlink have
	// normal and extended versions, where the extended versions
	// add yet another extra octet at the end of the QoS IE.
	// Return -1 if not specified in the IE, because it was too short,
	// in which case the caller must fall back on PeakThroughput.
	void setMaxBitRate(unsigned val, bool uplink);
	int getMaxBitRate(bool uplink);

	unsigned getPeakThroughput();
	void setPeakThroughput(unsigned bytePSec);
	// We probably dont ever need mean throughput, because you can set it to 'best effort',
	// implying that only peak throughput is significant.
	unsigned getMeanThroughput();
	void setMeanThroughput(unsigned bytePHour);
	void defaultPS(unsigned rateDownlink, unsigned rateUplink);
};

// 24.008 10.5.5.12a MS Radio Access Capability
// 5-15-2015, David says Range supports:
// 850: GSM 850, 900: GSM E, 1800: GSM 1800, 1900: GSM 1900
enum AccessTechnologyType {
	GSM_P = 0,
	GSM_E = 1,
	GSM_R = 2,
	GSM_1800 = 3,
	GSM_1900 = 4,
	GSM_450 = 5,
	GSM_480 = 6,
	GSM_850 = 7,
	GSM_750 = 8,
	GSM_T380 = 9,
	GSM_T410 = 10,
	GSM_UNUSED = 11,
	GSM_710 = 12,
	GSM_T810 = 13
};
const char *AccessTechnologyType2Name(AccessTechnologyType type);

// 24.008 10.5.5.12a MS Radio Access Capability
// 45.002 appendix B explains GPRS Multislot Class.
// There is so much junk in here I am taking a new tack.
// Keep the capabilities in a single array indexed by these names.
// A value of -1 indicates not present.
struct AccessCapabilities {
	Bool_z mValid;
	AccessTechnologyType mTechType;
	Bool_z mSameAsPrevious;
	enum CapType {
		//AccessTechnologyType,
		RFPowerCapability,
		A5Bits,
		ESInd,
		PS,
		VGCS,
		VBS,
		// multislot capabilities:
		// Warning: parsing code assumes multislot caps are in the order below.
		HSCSDMultislotClass,
		GPRSMultislotClass,
		GPRSExtendedDynamicAllocationCapability,
		SMS_VALUE,
		SM_VALUE,
		ECSDMultislotClass,		// multislot additions in release 99
		EGPRSMultislotClass,
		EGPRSExtendedDynamicAllocationCapability,
		DTMGPRSMultiSlotClass,
		SingleSlotDTM,
		DTMEGPRSMultiSlotClass,
		// Additions in release 99:
		EightPSKPowerCapability,
		COMPACTInterferenceMeasurementCapability,
		RevisionLevelIndicator,
		UMTSFDDRadioAccessTechnologyCapability,
		UMTS384McpsTDDRadioAccessTechnologyCapability,
		CDMA2000RadioAccessTechnologyCapability,
		// Additions in release 4:
		UMTS128McpsTDDRadioAccessTechnologyCapability,
		GERANFeaturePackage1,		// Finally, something we care about.
		ExtendedDTMGPRSMultiSlotClass,
		ExtendedDTMEGPRSMultiSlotClass,
		ModulationBasedMultislotClassSupport,
		// Addigions in release 5:
		HighMultislotCapability,
		//GMSKPowerClass,
		//8PSKPowerClass,
		CapsMax	// Here to indicate the length of the required list.
	};
	// These are the capabilities that are actually of some interest to us,
	// and will be printed in the message:
	static CapType mPrintList[];
	const char *CapName(CapType type) const;
	short mCaps[CapsMax];
	AccessCapabilities() { for (int i = 0; i < CapsMax; i++) { mCaps[i] = -1; } }
	int getCap(CapType captype) {	// Return cap value or -1.
		if (!mValid) return -1;
		assert(captype < CapsMax);
		return mCaps[captype];
	}
	void parseAccessCapabilities(ByteVector &bv,size_t &rp,AccessCapabilities *prev,size_t end);
	void text2(std::ostream &os,bool verbose) const;
	void text(std::ostream &os) const;
};

// 24.008 10.5.5.12a MS Radio Access Capability
// It is a list of structures, each of which is either an AccessCapabilitiesStruct
// or an AdditionalAccessTechnologiesStruct
struct MsRaCapability : public ByteVector {
	static const int sMsRaCapMaxTypes = 4;	// Keep the first four.
	AccessCapabilities mCList[sMsRaCapMaxTypes];	// Keep first four.
	void parseMsRaCapability();
	void text2(std::ostream &os,bool verbose) const;
	void text(std::ostream &os) const;
	MsRaCapability(const ByteVector &bv) : ByteVector(bv) { if (bv.size()) parseMsRaCapability(); }
};

struct PdpContextStatus : public Text2Str
{
	unsigned char mStatus[2];
	PdpContextStatus() { mStatus[0] = mStatus[1] = 0; }
	void text(std::ostream &os) const {
		os <<"PdpContextStatus="<<hex<<(int)mStatus[0]<<","<<(int)mStatus[1]<<dec;
	}
	bool anyDefined() { return !!(mStatus[0] | mStatus[1]); }
};


// The only differences between RoutingAreaUpdateRequest and AttachRequest are:
// The first nibble: for AttachRequest it is Attach Type 10.5.5.2
//		RAUpdate it is update type 10.5.5.18
// Mobile Identity: AttachRequest it is P-TMSI or IMSI; RAUpdate it is PTmsi.
// DRXparameter : AttachRequest: required; RAupdate: optional.
// PDP Context Status: RAUpdate: optional, AttachRequest: not present
struct GMMAttach
{
	Field_z<4> mCypheringKeySequenceNumber;	// Only bottom 3 bits used.
				// value 7 from MS means no key available, which it should be
				// if we have not sent an Authentication Request.
	Field_z<16> mDrxParameter;
	GmmMobileIdentityIE mMobileId;	// AttachRequest: PtmsiOrImsi; RAUpdate: mPTmsi;
	Bool_z mTmsiStatus;				// 10.5.5.4: does ms have a valid TMSI?
	ByteVector mMsRadioAccessCapability;// Required element in both AttachRequest and RAUpdate.
	GMMRoutingAreaIdIE mOldRaId;	// Required element in both AttachRequest and RAUpdate.
	Field_z<24> mOldPtmsiSignature;
	// For an incoming RAUpdate or AttachRequest, this is the mRequestedReadyTimerValue.
	Field_z<8> mRequestedReadyTimerValue;	// Unit is encoded per 10.5.7.3

	ByteVector mMsNetworkCapability;

	// For RA update the PDP context status indicates which PDP contexts
	// are still active in the GGSN, because you can switch SGSNs
	// while still having active PDP contexts.
	PdpContextStatus mPdpContextStatus;
	GmmMobileIdentityIE mAdditionalMobileId;

	void gmParseIEs(L3GmmFrame &src, size_t &rp, const char *culprit);
	void text(std::ostream &os) const {
		os 	<<LOGVAR2("mobileId",mMobileId.str())
			<<LOGVAR2("addtionalMobileId",mAdditionalMobileId.str())
			<<LOGVAR2("drx",mDrxParameter)
			<<LOGVAR2("tmsiStatus",mTmsiStatus)
			<<LOGVAR2("pdpContextStatus",mPdpContextStatus.str())
			<<LOGVAR(mCypheringKeySequenceNumber)
			//<<LOGVAR(mMsRadioAccessCapability)
			<<LOGVAR2("oldRaId",mOldRaId.str())
			<<LOGVAR(mOldPtmsiSignature)
			<<LOGVAR(mRequestedReadyTimerValue)
			<<LOGVAR(mMsNetworkCapability)
			;
		MsRaCapability caps(mMsRadioAccessCapability);
		os <<"\n";
		caps.text(os);
	}
};

// 24.008 9.4.20 Service Request
struct L3GmmMsgServiceRequest : L3GmmUlMsg
{
        Field<4> mCypheringKeySequenceNumber;   // Only bottom 3 bits used.
        GmmMobileIdentityIE mMobileId;  // PTmsi;
	Field<4> mServiceType; // Only bottom 3 bits are used

        // For RA update the PDP context status indicates which PDP contexts
        // are still active in the GGSN, because you can switch SGSNs
        // while still having active PDP contexts.
        PdpContextStatus mPdpContextStatus;
	// MBMS context status -- not implemented yet
	// Uplink data status -- not implemented yet;

        int MTI() const {return ServiceRequest;}
        void gmmParseBody(L3GmmFrame &src, size_t &rp);
        void textBody(std::ostream &os) const {
                os <<LOGVAR(mCypheringKeySequenceNumber) << LOGVAR(mServiceType)
			<<LOGVAR2("PdpContextStatus",mPdpContextStatus.str());        
                mMobileId.text(os);
        }
};

// 24.008 9.4.21 Service Accept
struct L3GmmMsgServiceAccept : L3GmmDlMsg
{
        // For RA update the PDP context status indicates which PDP contexts
        // are still active in the GGSN, because you can switch SGSNs
        // while still having active PDP contexts.
        PdpContextStatus mPdpContextStatus;
        // MBMS context status -- not implemented yet

        int MTI() const {return ServiceAccept;}
        L3GmmMsgServiceAccept(PdpContextStatus wStatus):
                mPdpContextStatus(wStatus)
        {
        }
        void gmmWriteBody(ByteVector &msg);
        void textBody(std::ostream &os) const {
                os << LOGVAR2("PdpContextStatus",mPdpContextStatus.str());
        }
};

// 24.008 9.4.22 Service Reject
struct L3GmmMsgServiceReject : L3GmmDlMsg
{
        uint8_t mGmmCause;      // GMM cause 10.5.5.14
        int MTI() const {return ServiceReject;}
        L3GmmMsgServiceReject(uint8_t wGmmCause):
                mGmmCause(wGmmCause)
        {
        }
        void gmmWriteBody(ByteVector &msg);
        void textBody(std::ostream &os) const {
                os << LOGVAR2("GmmCause",mGmmCause);
        }

};

struct L3GmmMsgRAUpdateRequest : L3GmmUlMsg, GMMAttach
{
	Field<3> mUpdateType;	// 10.5.5.18:
		// 0 => RA updating, 1 => Combined RA/LA updating
		// 2 => Combined RA/LA updating with IMSI attach
		// 3 => Periodic updating.
	bool mFollowOnRequestPending;

	int MTI() const {return RoutingAreaUpdateRequest;}
	void gmmParseBody(L3GmmFrame &src, size_t &rp);
	void textBody(std::ostream &os) const {
		//os <<"RAUpdateRequest" <<LOGVAR(mUpdateType);
		os <<LOGVAR(mUpdateType) << LOGVAR(mFollowOnRequestPending);
		GMMAttach::text(os);
	}
};

struct L3GmmMsgRAUpdateComplete : L3GmmUlMsg {
	int MTI() const {return RoutingAreaUpdateComplete;}
	void gmmParseBody(L3GmmFrame &/*src*/, size_t &/*rp*/) {
		// There is nothing interesting inside.
		// The fact that it arrived is the message.
	}
	//void text(std::ostream &os) const { os <<"RaUpdateComplete"; }
	void textBody(std::ostream &os) const;
};

// 24.008 9.4.2 Attach Accept
struct L3GmmMsgAttachAccept : public L3GmmDlMsg
{
	Field<4> mAttachResult;	// 1=> GPRS only attach, 3=>combined GPRS/IMSI attach.
	Bool_z mForceToStandby;
	GprsTimerIE mPeriodicRAUpdateTimer;
	GprsTimerIE mReadyTimer;
	uint32_t mPTmsi;
	// Per 24.008 9.4.2: Attach Accept Description,
	// it sounds like the second mobile identity is included only to set
	// a TMSI in case of a combined attach, so we should not include
	// this IE unless using NMO 1 and we support the combined attach.
	// 6-7-2012: Previously I had been returning the IMSI in this spot,
	// and that worked ok, but I am removing it as incorrect.
	GmmMobileIdentityIE mMobileId;

	int MTI() const {return AttachAccept;}

	// Constructor prior to 6-7-2012:
	L3GmmMsgAttachAccept(unsigned wAttachResult, uint32_t wPTmsi,
			GmmMobileIdentityIE /*wMobileId*/) :
		L3GmmDlMsg(senseReply),
		mAttachResult(wAttachResult),
		mPTmsi(wPTmsi)
		//mMobileId(wMobileId)
	{
	}

	// New constructor, no mobile id.
	L3GmmMsgAttachAccept(unsigned wAttachResult, uint32_t wPTmsi):
		L3GmmDlMsg(senseReply),
		mAttachResult(wAttachResult),
		mPTmsi(wPTmsi)
	{
	}

	void gmmWriteBody(ByteVector &msg);
	void textBody(std::ostream &os) const {
		//os <<"AttachAccept ";
		unsigned RAUpdateIE = mPeriodicRAUpdateTimer.getIEValue();
		os <<LOGVAR(mAttachResult) <<LOGHEX(mPTmsi)
			<<LOGVAR(mForceToStandby)
			<<LOGVAR2("RAUpdateTimer",mPeriodicRAUpdateTimer.getSeconds()) <<LOGHEX(RAUpdateIE)
			<<LOGVAR2("mobileId",mMobileId.str());
		GMMRoutingAreaIdIE mRaId;
		mRaId.raLoad();
		mRaId.text(os);
	}
};

// 24.008 9.4.15 Routing Area Update Accept
struct L3GmmMsgRAUpdateAccept : L3GmmDlMsg
{
	unsigned mUpdateResult;	// 10.5.5.17
	Bool_z mForceToStandby;	// 0 means no, 1 means force to standby.
	GprsTimerIE mPeriodicRAUpdateTimer;
	// "This IE may be included to assign or unassign a TMSI to a MS
	// in case of a combined routing area updating procedure."
	// Note: we do not need the MS id in this message because the L2 Layer takes care
	// of delivering it back to the correct MS.
	//GmmMobileIdentityIE mMobileId;
	PdpContextStatus mPdpContextStatusCurrent;
	uint32_t mAllocatedPTmsi;	// Allocated p-tmsi, or 0
	uint32_t mTmsi;				// tmsi or 0, for combined raupdate, sent in the mobile-id ie.

	L3GmmMsgRAUpdateAccept(RAUpdateType updatetype, PdpContextStatus wPdpContextStatus, uint32_t ptmsi,uint32_t tmsi) :
		L3GmmDlMsg(senseReply),
		mUpdateResult((unsigned)updatetype),
		mPdpContextStatusCurrent(wPdpContextStatus),
		mAllocatedPTmsi(ptmsi),
		mTmsi(tmsi)
		//mMobileId(mid),
	{
		mPeriodicRAUpdateTimer.setSeconds(gConfig.getNum("SGSN.Timer.RAUpdate"));
	}

	int MTI() const {return RoutingAreaUpdateAccept;}
	void gmmWriteBody(ByteVector &msg);
	void textBody(std::ostream &os) const {
		//os <<"RaUpdateAccept"
		unsigned RAUpdateIE = mPeriodicRAUpdateTimer.getIEValue();
		os<<LOGVAR(mUpdateResult) <<LOGVAR(mForceToStandby)
			// We did not bother to print out the GMMRoutingAreaIdIE
			<<LOGHEX2("ptmsi",mAllocatedPTmsi) <<LOGHEX2("MSIdentity(mTmsi)",mTmsi)
			<<LOGVAR2("RAUpdateTimer",mPeriodicRAUpdateTimer.getSeconds()) <<LOGHEX(RAUpdateIE)
			<<LOGVAR2("PdpContextStatusCurrent",mPdpContextStatusCurrent.str());
	}
};


// 24.008 9.4.17 Routing Area Update Reject
// Note there is no MS id here - the L2 Layer takes care of delivering it back to the correct MS.
struct L3GmmMsgRAUpdateReject : L3GmmDlMsg
{
	uint8_t mGmmCause;	// GMM cause 10.5.5.14
	L3GmmMsgRAUpdateReject(unsigned cause) :
		L3GmmDlMsg(senseReply),
		mGmmCause(cause)
		{}
	int MTI() const {return RoutingAreaUpdateReject;}
	void gmmWriteBody(ByteVector &msg);
	void textBody(std::ostream &os) const;
};


struct L3GmmMsgAttachRequest : L3GmmUlMsg, GMMAttach
{
	Field<4> mAttachType;	// 10.5.5.2

	int MTI() const {return AttachRequest;}
	void gmmParseBody(L3GmmFrame &src, size_t &rp);

	void textBody(std::ostream &os) const {
		os 	<<LOGVAR(mAttachType);
			//<<LOGVAR2("mobileId",mMobileId.str())
			//<<LOGVAR2("drx",mDrxParameter)
			//<<LOGVAR2("tmsiStatus",mTmsiStatus);
		GMMAttach::text(os);
	}
};

struct L3GmmMsgAttachComplete : L3GmmUlMsg
{
	int MTI() const {return AttachComplete;}
	void gmmParseBody(L3GmmFrame &/*src*/, size_t &/*rp*/) {
		// There is nothing interesting inside the message.
		// The fact that it arrived is the message.
	}
	void textBody(std::ostream &/*os*/) const {/*nothing*/}
};

// 3GPP 24.008 9.4.5.1 Detach Request Network Originated
// and 9.5.4.2 Detach Request Mobile Originated.
struct L3GmmMsgDetachRequest : L3GmmDlMsg, L3GmmUlMsg
{
	int MTI() const {return DetachRequest;}

	Field_z<4> mDetachType, mForceToStandby;
	// The GmmCause and ForceToStandby are only present in the downlink direction.
	uint16_t mGmmCause;
	Bool_z mGmmCausePresent;

	// The tmsi is only present in the uplink direction.
	GmmMobileIdentityIE mMobileId;	// This is supposed to be a PTMSI only, so why encoded as a MobileIdentity IE?
	Bool_z mMobileIdPresent;

	// In the uplink direction there is also a P-TMSI signature that we dont use, so we dont parse.
	// In fact, the MS is not supposed to include it if we didnt sent it one in an earlier message.

	// This message is bidirectional.  This constructor is for reading one in:
	L3GmmMsgDetachRequest() {}
	void gmmParseBody(L3GmmFrame &src, size_t &rp);

	// This message is bidirectional.  This constructor is for making one to send downstream:
	L3GmmMsgDetachRequest(unsigned type, unsigned cause) :
		L3GmmDlMsg(senseCmd),
		mDetachType(type),
		mForceToStandby(0),
		mGmmCause(cause),
		mGmmCausePresent(cause != 0)
	{}
	void gmmWriteBody(ByteVector &msg);

	void textBody(std::ostream &os) const;
};

// 3GPP 24.008 9.4.6 Detach Accept
struct L3GmmMsgDetachAccept : L3GmmUlMsg, L3GmmDlMsg
{
	int MTI() const {return DetachAccept;}
	Field_z<4> mForceToStandby;

	// This message is bidirectional.  This constructor is for reading one in:
	L3GmmMsgDetachAccept() {}
	void gmmParseBody(L3GmmFrame &/*src*/, size_t &/*rp*/) {
		// Nothing at all.  The presence of this message is the indication.
	}

	// This message is bidirectional.  This constructor is for making one to send downstream.
	// Good old C++ requires us to make the constructor arguments unique so we will pass in the useless ForceToStandby.
	L3GmmMsgDetachAccept(unsigned wForceToStandby) :
		L3GmmDlMsg(senseReply),
		mForceToStandby(wForceToStandby)
		{}
	void gmmWriteBody(ByteVector &msg);

	void textBody(std::ostream &os) const;
};

struct L3GmmMsgIdentityRequest : L3GmmDlMsg
{
	Field<4> mIdentityType, mForceToStandby;
	int MTI() const {return IdentityRequest;}
	L3GmmMsgIdentityRequest() :
		L3GmmDlMsg(senseCmd),
		mIdentityType(1),		// 1 means IMSI, the only kind we ever ask for.
		mForceToStandby(0)
		{}
	void gmmWriteBody(ByteVector &msg);
	void textBody(std::ostream &os) const;
};

// 24.008 9.4.9 Authenticaion and ciphering request.
struct L3GmmMsgAuthentication : L3GmmDlMsg
{
	int MTI() const {return AuthenticationAndCipheringReq;}
	// We wont use any of the IEs:
	// Ciphering algorithm - always 0
	// IMEISV request - request IMEI in response, nope.
	// Force to standyby - nope
	// A&C reference number - just used to match up Authentication Response to this message.
	ByteVector mRand; // 128 bit random number.
	// GPRS ciphering key sequence - nope
	// AUTN - if specified, it is a UMTS type challenge. nope.
	void gmmWriteBody(ByteVector &msg);
	L3GmmMsgAuthentication(ByteVector &rand) : L3GmmDlMsg(senseCmd), mRand(rand)
	{
		assert(rand.size() == 16);
	}
	void textBody(std::ostream &os) const {
		os <<LOGVAR(mRand);
	}
};

// 24.008 9.4.9 Authenticaion and ciphering request.
struct L3GmmMsgAuthenticationResponse : L3GmmUlMsg
{
        int MTI() const {return AuthenticationAndCipheringResp;}
        ByteVector mSRES; // 32 bit authentication result.
        void gmmParseBody(L3GmmFrame &src, size_t &rp);
        void textBody(std::ostream &os) const {
                os <<LOGVAR(mSRES);
        }
};



struct L3GmmMsgIdentityResponse : L3GmmUlMsg
{
	int MTI() const {return IdentityResponse;}
	GmmMobileIdentityIE mMobileId;
	void gmmParseBody(L3GmmFrame &src, size_t &rp);
	void textBody(std::ostream &os) const;
};

struct L3SmMsgActivatePdpContextRequest : L3SmUlMsg
{
	unsigned mNSapi;
	unsigned mLlcSapi;
	unsigned mRequestType;
	ByteVector mQoS;
	ByteVector mPdpAddress;
	ByteVector mApName;
	ByteVector mPco;

	L3SmMsgActivatePdpContextRequest() {};
	L3SmMsgActivatePdpContextRequest(L3SmFrame &src) { smParse(src); }
	int MTI() const {return ActivatePDPContextRequest;}
	void smParseBody(L3SmFrame &src, size_t &rp);
	void textBody(std::ostream &os) const {
		os<<LOGVAR(mNSapi)<<LOGVAR(mLlcSapi) <<LOGVAR(mRequestType)
			<<LOGVAR(mPdpAddress)<<LOGVAR(mQoS)<<LOGVAR(mApName) <<LOGVAR(mPco);
	}
};

struct L3SmMsgActivatePdpContextAccept : L3SmDlMsg
{
	unsigned mLlcSapi;
	ByteVector mQoS;
	unsigned mRadioPriority;
	ByteVector mPdpAddress;	// Would be optional if we supported static ips, but we dont.
	ByteVector mPco;		// If  you dont pass these down, get: SM Status: Invalid Mandatory information.

	L3SmMsgActivatePdpContextAccept(unsigned wti) : L3SmDlMsg(wti,senseReply) {}	// Other fields filled in by caller.
	int MTI() const {return ActivatePDPContextAccept;}
	void smWriteBody(ByteVector &msg);
	void textBody(std::ostream &os) const {
		os<<LOGVAR(mLlcSapi)<<LOGVAR(mPdpAddress)<<LOGVAR(mQoS)<<LOGVAR(mRadioPriority)<<LOGVAR(mPco);
	}
};

struct L3SmMsgActivatePdpContextReject : L3SmDlMsg
{
	unsigned mCause;	// type SmCause::Cause
	L3SmMsgActivatePdpContextReject(unsigned wti, unsigned wcause) : L3SmDlMsg(wti,senseReply), mCause(wcause) {}
	int MTI() const {return ActivatePDPContextReject;}
	void smWriteBody(ByteVector &msg);
	void textBody(std::ostream &os) const;
};

// TODO: How do you deactivate the contexts?  The PDP context is identified by its TI,
// but if we just get a packet for an unconfigured NSAPI, then we dont have a TI...
// I added sendPdpDeactivateAll to just deactivate them all.
struct L3SmMsgDeactivatePdpContextRequest : L3SmUlMsg, L3SmDlMsg
{
	unsigned mCause;	// This is an SmCause
	Bool_z mTearDownIndicator;
	ByteVector mPco;	// Option PCO.  Added this so I can see what is in there.

	// This message is bidirectional.  This constructor is for reading one in:
	L3SmMsgDeactivatePdpContextRequest(L3SmFrame &src) { smParse(src); }
	void smParseBody(L3SmFrame &src,size_t &rp);

	// This message is bidirectional.  This constructor is for making one to send downstream:
	L3SmMsgDeactivatePdpContextRequest(unsigned ti, SmCause::Cause cause, bool wTearDownIndicator) :
		L3SmDlMsg(ti,senseCmd), mCause(cause), mTearDownIndicator(wTearDownIndicator) {}
	int MTI() const {return DeactivatePDPContextRequest;}
	void smWriteBody(ByteVector &msg);
	void textBody(std::ostream &os) const;
};

// 3GPP 24.008 9.5.15
struct L3SmMsgDeactivatePdpContextAccept : L3SmDlMsg, L3SmUlMsg
{
	// Message has optional PCO and MBMS IEs that we ignore.
	// This message is bidirectional.  This constructor is for reading one in:
	L3SmMsgDeactivatePdpContextAccept(L3SmFrame &src) { smParse(src); }
	void smParseBody(L3SmFrame &/*src*/,size_t &/*rp*/) {/*nothing*/}

	// This message is bidirectional.  This constructor is for making one to send downstream:
	L3SmMsgDeactivatePdpContextAccept(unsigned ti) : L3SmDlMsg(ti,senseReply) {} 
	int MTI() const {return DeactivatePDPContextAccept;}
	void smWriteBody(ByteVector &/*msg*/) {/*nothing*/}
	void textBody(std::ostream &os) const;
};


struct L3SmMsgSmStatus : L3SmUlMsg, L3SmDlMsg
{
	unsigned mCause;
	int MTI() const {return SMStatus;}
	// This message is bidirectional.  This constructor is for reading one in:
	L3SmMsgSmStatus(L3SmFrame &src) { smParse(src); }
	void smParseBody(L3SmFrame &src,size_t &rp) {
		mCause = src.getByte(rp);
	}

	// This message is bidirectional.  This constructor is for making one to send downstream:
	L3SmMsgSmStatus(unsigned tid, SmCause::Cause cause) : L3SmDlMsg(tid,senseReply), mCause(cause) {}
	void smWriteBody(ByteVector &msg);

	void textBody(std::ostream &os) const;
};


// This message is birectional.
struct L3GmmMsgGmmStatus : L3GmmUlMsg, L3GmmDlMsg
{
	unsigned mCause;
	int MTI() const {return GMMStatus;}
	void gmmParseBody(L3GmmFrame &src, size_t &rp) {
		mCause = src.getByte(rp);
	}
	void gmmWriteBody(ByteVector &msg) {
		msg.appendByte(mCause);
	}
	void textBody(std::ostream &os) const;
	// This message is bidirectional.  This constructor is for reading one in:
	L3GmmMsgGmmStatus() {}
	// This message is bidirectional.  This constructor is for making one to send downstream.
	L3GmmMsgGmmStatus(unsigned wCause): L3GmmDlMsg(senseCmd), mCause(wCause) {}
};

};	// namespace GPRS
#endif
