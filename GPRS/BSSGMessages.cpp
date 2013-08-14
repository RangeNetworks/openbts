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

#include "BSSG.h"
#include "BSSGMessages.h"
#include "GPRSInternal.h"
#include "Globals.h"
#include "LLC.h"
#define CASENAME(x) case x: return #x;

namespace BSSG {

const char *BSPDUType::name(int val)
{
	switch ((type)val) {
		CASENAME(DL_UNITDATA)
		CASENAME(UL_UNITDATA)
		CASENAME(RA_CAPABILITY)
		CASENAME(PTM_UNITDATA)
		CASENAME(PAGING_PS)
		CASENAME(PAGING_CS)
		CASENAME(RA_CAPABILITY_UPDATE)
		CASENAME(RA_CAPABILITY_UPDATE_ACK)
		CASENAME(RADIO_STATUS)
		CASENAME(SUSPEND)
		CASENAME(SUSPEND_ACK)
		CASENAME(SUSPEND_NACK)
		CASENAME(RESUME)
		CASENAME(RESUME_ACK)
		CASENAME(RESUME_NACK)
		CASENAME(BVC_BLOCK)
		CASENAME(BVC_BLOCK_ACK)
		CASENAME(BVC_RESET)
		CASENAME(BVC_RESET_ACK)
		CASENAME(BVC_UNBLOCK)
		CASENAME(BVC_UNBLOCK_ACK)
		CASENAME(FLOW_CONTROL_BVC)
		CASENAME(FLOW_CONTROL_BVC_ACK)
		CASENAME(FLOW_CONTROL_MS)
		CASENAME(FLOW_CONTROL_MS_ACK)
		CASENAME(FLUSH_LL)
		CASENAME(FLUSH_LL_ACK)
		CASENAME(LLC_DISCARDED)
		CASENAME(SGSN_INVOKE_TRACE)
		CASENAME(STATUS)
		CASENAME(DOWNLOAD_BSS_PFC)
		CASENAME(CREATE_BSS_PFC)
		CASENAME(CREATE_BSS_PFC_ACK)
		CASENAME(CREATE_BSS_PFC_NACK)
		CASENAME(MODIFY_BSS_PFC)
		CASENAME(MODIFY_BSS_PFC_ACK)
		CASENAME(DELETE_BSS_PFC)
		CASENAME(DELETE_BSS_PFC_ACK)
	}
	return "unrecognized PDU";
}
std::ostream& operator<<(std::ostream& os, const BSPDUType::type val)
{
	os << "PDU_Type=" <<(int)val <<"=" <<BSPDUType::name(val);
	return os;
}

// GSM 08.18 table 5.4 says what BVCI to use for each message.
// This is kinda dopey, but we have to do it.
// We are ignoring PTM messages.
// There is a note the PAGING_PS and PAGING_CS may be BVCI::SIGNALLING
const unsigned BSPDUType::LookupBVCI(BSPDUType::type bstype)
{
	switch (bstype) {
	case BSPDUType::SUSPEND:
	case BSPDUType::SUSPEND_ACK:
	case BSPDUType::SUSPEND_NACK:
	case BSPDUType::RESUME:
	case BSPDUType::RESUME_ACK:
	case BSPDUType::RESUME_NACK:
	case BSPDUType::FLUSH_LL:
	case BSPDUType::FLUSH_LL_ACK:
	case BSPDUType::LLC_DISCARDED:
	case BSPDUType::BVC_BLOCK:
	case BSPDUType::BVC_BLOCK_ACK:
	case BSPDUType::BVC_UNBLOCK:
	case BSPDUType::BVC_UNBLOCK_ACK:
	case BSPDUType::BVC_RESET:
	case BSPDUType::BVC_RESET_ACK:
	case BSPDUType::SGSN_INVOKE_TRACE:
		return BVCI::SIGNALLING;
	default:
		return gBSSG.mbsBVCI;
	}
}

const char *IEIType::name(int val)
{
	switch ((type)val) {
		CASENAME(AlignmentOctets)
		CASENAME(BmaxDefaultMS)
		CASENAME(BSSAreaIndication)
		CASENAME(BucketLeakRate)
		CASENAME(BVCI)
		CASENAME(BVCBucketSize)
		CASENAME(BVCMeasurement)
		CASENAME(Cause)
		CASENAME(CellIdentifier)
		CASENAME(ChannelNeeded)
		CASENAME(DRXParameters)
		CASENAME(eMLPPPriority)
		CASENAME(FlushAction)
		CASENAME(IMSI)
		CASENAME(LLCPDU)
		CASENAME(LLCFramesDiscarded)
		CASENAME(LocationArea)
		CASENAME(MobileId)
		CASENAME(MSBucketSize)
		CASENAME(MSRadioAccessCapability)
		CASENAME(OMCId)
		CASENAME(PDUInError)
		CASENAME(PDULifetime)
		CASENAME(Priority)
		CASENAME(QoSProfile)
		CASENAME(RadioCause)
		CASENAME(RACapUPDCause)
		CASENAME(RouteingArea)
		CASENAME(RDefaultMS)
		CASENAME(SuspendReferenceNumber)
		CASENAME(Tag)
		CASENAME(TLLI)
		CASENAME(TMSI)
		CASENAME(TraceReference)
		CASENAME(TraceType)
		CASENAME(TransactionId)
		CASENAME(TriggerId)
		CASENAME(NumberOfOctetsAffected)
		CASENAME(LSAIdentifierList)
		CASENAME(LSAInformation)
		CASENAME(PacketFlowIdentifier)
		CASENAME(PacketFlowTimer)
		CASENAME(AggregateBSSQoSProfile)
		CASENAME(FeatureBitmap)
		CASENAME(BucketFullRatio)
		CASENAME(ServiceUTRANCCO)
	}
	return "unrecognized IEI";
}
std::ostream& operator<<(std::ostream& os, const IEIType::type val)
{
	os << "IEI_Type=" <<(int)val <<"=" <<IEIType::name(val);
	return os;
}

const char *NSPDUType::name(int val)
{
	switch ((type)val) {
		CASENAME(NS_UNITDATA)
		CASENAME(NS_RESET)
		CASENAME(NS_RESET_ACK)
		CASENAME(NS_BLOCK)
		CASENAME(NS_BLOCK_ACK)
		CASENAME(NS_UNBLOCK)
		CASENAME(NS_UNBLOCK_ACK)
		CASENAME(NS_STATUS)
		CASENAME(NS_ALIVE)
		CASENAME(NS_ALIVE_ACK)
	}
	return "unrecognized NSPDUType";
}
std::ostream& operator<<(std::ostream& os, const NSPDUType::type val)
{
	os << "NSPDU_Type=" <<(int)val <<"=" <<NSPDUType::name(val);
	return os;
}

#if 0
/** GSM 04.60 11.2 */
BSSGDownlinkMsg* BSSGDownlinkParse(ByteVector &src)
{
	BSSGDownlinkMsg *result = NULL;

	// NS PDUType is byte 0.
	NSPDUType::type nstype = (NSPDUType::type) src.getByte(0);
	if (nstype != NSPDUType::NS_UNITDATA) {
		LOG(INFO) << "Unrecognized NS PDU Type=" << nstype ;
		return NULL;
	}

	// BSSG PDUType is byte 5.
	BSPDUType::type msgType = (BSPDUType::type) src.getByte(5);

	switch (msgType) {
		case BSPDUType::DL_UNITDATA:
			result = new BSSGMsgDLUnitData(src);
			break;
		case BSPDUType::RA_CAPABILITY:
		case BSPDUType::PAGING_PS:
		case BSPDUType::PAGING_CS:
		case BSPDUType::RA_CAPABILITY_UPDATE_ACK:
		case BSPDUType::SUSPEND_ACK:
		case BSPDUType::SUSPEND_NACK:
		case BSPDUType::RESUME_ACK:
		case BSPDUType::RESUME_NACK:
		default:
			LOG(INFO) << "unimplemented BSSG downlink message, type=" << msgType;
			return NULL;
	}
	return result;
};
BSSGDownlinkMsg* BSSGDownlinkMessageParse(ByteVector&src)
{
	BSSGDownlinkMsg *result = NULL;
	unsigned pdutype = src.getByte(sizeof(NSMsg::UnitDataHeaderLen));
	switch (pdutype) {
		case BSPDUType::DL_UNITDATA:
			result = new BSSGMsgDLUnitData(src); 
			break;
		case BSPDUType::RA_CAPABILITY_UPDATE_ACK:
		case BSPDUType::RA_CAPABILITY:
		case BSPDUType::FLUSH_LL:
			// todo
			LOG(INFO) << "unsuppported BSSG downlink PDU type=" << pdutype;
			break;
		default:
			LOG(INFO) << "unsuppported BSSG downlink PDU type=" << pdutype;
			break;
	}
	return result;
}
#endif

void BSSGMsgDLUnitData::parseDLUnitDataBody(ByteVector &src, size_t &wp)
{
	wp++;	// Skip over the pdutype.
	mbdTLLI = src.getUInt32(wp); wp+=4;
	mbdQoS.qosRead(src,wp);
	// The rest of the fields use TLV format:
	unsigned len = src.size() - 1;	// wp cant actually get anywhere near size() because
				// the TLV headers take at least 2 bytes.
	while (wp < len) {
		unsigned iei = src.getByte(wp++);
		unsigned length = src.readLI(wp);
		unsigned nextwp = wp + length;
		switch (iei) {
			case IEIType::PDULifetime:
				mbdPDULifetime = src.getUInt16(wp);
				break;
			case IEIType::IMSI:
				mbdIMSI.set(src.segment(wp,length));
				break;
			case IEIType::MSRadioAccessCapability:
				mbdRACap.set(src.segment(wp,length));
				break;
			case IEIType::LLCPDU:
				// Finally, here is the data:
				mbdPDU.set(src.segment(wp,length));
				goto done;
			case IEIType::TLLI:		// old TLLI
				mbdHaveOldTLLI = true;
				mbdOldTLLI = src.getUInt32(wp);
				break;

			case IEIType::Priority:
			case IEIType::DRXParameters:
			case IEIType::PacketFlowIdentifier:
			case IEIType::LSAInformation:
			case IEIType::AlignmentOctets:
			default:
				// ignored.
				break;
		}
		wp = nextwp;
	}
	done:;
	//...
}

//std::ostream& operator<<(std::ostream& os, const BSSGMsgDLUnitData &val)
//{
	//val.text(os);
	//return os;
//}

static void NsAddCause(ByteVector *vec, unsigned cause)
{
	vec->appendByte(NsIEIType::IEINsCause);
	vec->appendLI(1);
	vec->appendByte(cause);
}

static void NsAddBVCI(ByteVector *vec, BVCI::type  bvci)
{
	vec->appendByte(NsIEIType::IEINsBVCI);
	vec->appendLI(2);
	vec->appendUInt16(bvci);
}

static void NsAddVCI(ByteVector *vec)
{
	vec->appendByte(NsIEIType::IEINsVCI);
	vec->appendLI(2);
	vec->appendUInt16(gBSSG.mbsNSVCI);
}

static void NsAddNSEI(ByteVector *vec)
{
	vec->appendByte(NsIEIType::IEINsNSEI);
	vec->appendLI(2);
	vec->appendUInt16(gBSSG.mbsNSEI);
}

NSMsg *NsFactory(NSPDUType::type nstype, int cause)
{
	NSMsg *vec = new NSMsg(80);	// Big enough for any message.

	vec->setAppendP(0);			// Setup vec for appending.
	vec->appendByte(nstype);	// First byte of message is NS PDUType.

	switch (nstype) {
	case NSPDUType::NS_RESET:
		NsAddCause(vec,cause);
		NsAddVCI(vec);
		NsAddNSEI(vec);
		break;
	case NSPDUType::NS_RESET_ACK:
		NsAddVCI(vec);
		NsAddNSEI(vec);
		break;
	case NSPDUType::NS_BLOCK:
		NsAddCause(vec,cause);
		NsAddVCI(vec);
		break;
	case NSPDUType::NS_BLOCK_ACK:
		NsAddVCI(vec);
		break;
	case NSPDUType::NS_UNBLOCK:
		break;	// 1 byte messages is finished.
	case NSPDUType::NS_UNBLOCK_ACK:
		break;	// 1 byte messages is finished.
	case NSPDUType::NS_STATUS:
		NsAddCause(vec,cause);
		switch (cause) {
			case NsCause::NSVCBlocked:
			case NsCause::NSVCUnknown:
				NsAddVCI(vec);
				break;
			case NsCause::SemanticallyIncorrectPDU:
			case NsCause::PduNotCompatible:
			case NsCause::ProtocolError:
			case NsCause::InvalidEssentialIE:
			case NsCause::MissingEssentialIE:
				// unimplemented.
				assert(0);	// In these cases need to append PDU.

			case NsCause::BVCIUnknown:
				// We dont really know what the cause was,
				// and this wont happen, so just make up a BVCI
				NsAddBVCI(vec,BVCI::PTP);
				break;
			case NsCause::TransitNetworkFailure:
			case NsCause::OAndMIntervention:
			case NsCause::EquipmentFailure:
				break;	// nothing more needed.
		}
		break;
	case NSPDUType::NS_ALIVE:
		break;	// 1 byte messages is finished.
	case NSPDUType::NS_ALIVE_ACK:
		break;	// 1 byte messages is finished.

	case NSPDUType::NS_UNITDATA:
		assert(0);		// Not handled by this routine.
	default: assert(0);
	}
	return vec;
}

static void BVCAddBVCI(ByteVector *vec, BSPDUType::type bstype)
{
	vec->appendByte(IEIType::BVCI);
	vec->appendLI(2);
	vec->appendUInt16(BSPDUType::LookupBVCI(bstype));
}

static void BVCAddCause(ByteVector *vec, int cause)
{
	vec->appendByte(IEIType::Cause);
	vec->appendLI(1);
	vec->appendByte(cause);
}

static void BVCAddTag(ByteVector *vec, int tag)
{
	vec->appendByte(IEIType::Tag);
	vec->appendLI(1);
	vec->appendByte(tag);
}

// GSM 08.18 10.4.12 and 11.3.9
static void BVCAddCellIdentifier(ByteVector *vec)
{
	vec->appendByte(IEIType::CellIdentifier);
	vec->appendLI(8);
	// Add Routing Area Identification IE from GSM 04.08 10.5.5.15, excluding IEI type and length bytes.
	// Another fine example of Object Oriented programming preventing sharing of code:
	// this information is wrapped up in the middle of an inapplicable class hierarchy.
	const char*mMCC = gConfig.getStr("GSM.Identity.MCC").c_str();
	const char*mMNC = gConfig.getStr("GSM.Identity.MNC").c_str();
	unsigned mLAC = gConfig.getNum("GSM.Identity.LAC");
	unsigned mRAC = gConfig.getNum("GPRS.RAC");
	vec->appendByte(((mMCC[1]-'0')<<4) | (mMCC[0]-'0'));	// MCC digit 2, MCC digit 1
	vec->appendByte(((mMNC[2]-'0')<<4) | (mMCC[2]-'0'));	// MNC digit 3, MCC digit 3
	vec->appendByte(((mMNC[1]-'0')<<4) | (mMNC[0]-'0')); // MNC digit 2, MNC digit 1
	vec->appendUInt16(mLAC);
	vec->appendByte(mRAC);
	// Add Routing Area Identification IE from GSM 04.08 10.5.5.15, excluding IEI type and length bytes.
	// Add Cell Identity IE GSM 04.08 10.5.1.1, excluding IEI type
	unsigned mCI = gConfig.getNum("GSM.Identity.CI");
	vec->appendUInt16(mCI);
}

// GSM 08.18 sec 10 describes the PDU messages that the SGSN can send to the BSS.
// Cause values: 08.18 sec 11.3.8
BSSGUplinkMsg *BVCFactory(BSPDUType::type bstype,
	int arg1)	// For reset, the bvci to reset; for others may be cause or tag .
{
	BSSGUplinkMsg *vec = new BSSGUplinkMsg(80);	// Big enough for any message.
	BVCI::type bvci;

	vec->setAppendP(0);			// Setup vec for appending.

	// Add the NS header.
	vec->appendByte(NSPDUType::NS_UNITDATA);
	vec->appendByte(0);	// unused byte.
	vec->appendUInt16(gBSSG.mbsNSEI);
	// Add the BSSG message type
	vec->appendByte((ByteType)bstype);

	switch (bstype) {
	case BSPDUType::BVC_RESET:
		// See GSM 08.18 sec 8.4: BVC-RESET procedure; and 10.4.12: BVC-RESET message.
		bvci = (BVCI::type) arg1;
		vec->appendByte(IEIType::BVCI);
		vec->appendLI(2);
		vec->appendUInt16(bvci);
		BVCAddCause(vec,0x8);	// Cause 8: O&M Intervention
		if (bvci != BVCI::SIGNALLING) {
			BVCAddCellIdentifier(vec);
		}
		// We dont use the feature bitmap.
		break;
	case BSPDUType::BVC_RESET_ACK:
		BVCAddBVCI(vec,bstype);
		// There could be a cell identifier
		// There coulde be a feature bitmap.
		break;
	case BSPDUType::BVC_BLOCK:
		BVCAddBVCI(vec,bstype);
		BVCAddCause(vec,arg1);
		break;
	case BSPDUType::BVC_BLOCK_ACK:	// fall through
	case BSPDUType::BVC_UNBLOCK:	// fall through
	case BSPDUType::BVC_UNBLOCK_ACK:
		BVCAddBVCI(vec,bstype);
		break;
	case BSPDUType::FLOW_CONTROL_BVC_ACK:
		BVCAddTag(vec,arg1);
		break;
	default: assert(0);
	}
	return vec;
}

// Length Indicator GSM 08.16 10.1.2
// And I quote:
//		"The BSS or SGSN shall not consider the presence of octet 2a in a received IE
// 		as an error when the IE is short enough for the length to be coded
// 		in octet 2 only."
// (pat) If the length is longer than 127, it is written simply
// as a 16 bit number in network order, which is high byte first,
// so the upper most bit is 0 if the value is <= 32767
static unsigned IEILength(unsigned int len)
{
	if (len < 127) return 0x80 + len;
	assert(0);
}


void NSMsg::textNSHeader(std::ostream&os) const
{
	int nstype = (int)getNSPDUType();
	if (nstype == NSPDUType::NS_UNITDATA) {
		os <<"NSPDUType="<<nstype <<"="<<NSPDUType::name(nstype)<<" BVCI="<<getUInt16(2);
	} else {
		os <<"NSPDUType="<<nstype <<"="<<NSPDUType::name(nstype);
		// The rest of the message is not interesting to us, so dont bother.
	}
}

void NSMsg::text(std::ostream&os) const
{
	os <<" NSMsg:"; textNSHeader(os);
}

void BSSGMsg::text(std::ostream&os) const
{
	textNSHeader(os);
	os << " BSPDUType="<<getPDUType() <<" size="<<size();
}

std::string BSSGMsg::briefDescription() const
{
	return BSPDUType::name(getPDUType());
}

BSSGMsgULUnitData::BSSGMsgULUnitData(unsigned wLen,	// Length of the PDU to be sent.
	uint32_t wTLLI)
	: BSSGUplinkMsg(wLen + HeaderLength)
{
	QoSProfile qos;	// Both we and the SGSN ignore the contents of this.
					// Note there is a different QoS format included in
					// PDP Context activation messages.

	setAppendP(0);			// Setup vec for appending.
	// Write the NS header.
	appendByte(NSPDUType::NS_UNITDATA);
	appendByte(0);	// unused byte.
	// The NS UNITDATA message puts the BVCI in the NS header where the NSEI normally goes.
	appendUInt16(gBSSG.mbsBVCI);
	// End of NS Header, start of BSSG message.
	appendByte(BSPDUType::UL_UNITDATA);
	appendUInt32(wTLLI);
	qos.qosAppend(this);
	BVCAddCellIdentifier(this);
	// We need a two byte alignment IEI to get to a 32 bit boundary.
	appendByte(IEIType::AlignmentOctets);
	appendByte(IEILength(0));

	// The PDU IEI itself comes next.
	appendByte(IEIType::LLCPDU);
	// We are allowed to use a 16 bit length IEI even for elements
	// less than 128 bytes long, so we will.
	mLengthPosition = size();		// Where the length goes.
	appendUInt16(0);	// A spot for the length.
	// Followed by the PDU data.
	assert(size() == HeaderLength);
}

// Call this when the PDU is finished to set the length in the BSSG header.
void BSSGMsgULUnitData::setLength()
{
	// The PDU begins immediately after the 2 byte length.
	assert(size() >= HeaderLength);
	unsigned pdulen = size() - HeaderLength;
	GPRSLOG(1) << "setLength:"<<LOGVAR(pdulen) << LOGVAR(mLengthPosition) << LOGVAR(size());
	setUInt16(mLengthPosition,pdulen);
}

void BSSGMsgULUnitData::text(std::ostream&os) const
{
	os <<"BSSGMsgULUnitData=("; 
	os <<"tbf=TBF#"<<mTBFId<<" ";
	BSSGUplinkMsg::text(os);
	unsigned TLLI = getTLLI();
	os << LOGHEX(TLLI);
	// The payload is 0 length only during debugging when we send in empty messages.
	ByteVector payload(tail(HeaderLength));
	if (payload.size()) {
		//GPRS::LLCFrame llcmsg(*const_cast<BSSGMsgULUnitData*>(this));
		os << " LLC UL payload="<<payload;
		SGSN::LlcFrameDump llcmsg(payload);
		os << " ";
		llcmsg.text(os);
	}
	//os << " payload=" <<payload;
	os <<")";
}

std::string BSSGMsgDLUnitData::briefDescription() const
{
	std::ostringstream ss;
	if (mbdPDU.size()) {
		SGSN::LlcFrameDump llcmsg(mbdPDU);
		llcmsg.textContent(ss,false);
	}
	return ss.str();
}

void BSSGMsgDLUnitData::text(std::ostream &os) const
{
	os << "BSSGMsgDLUnitData:";
	BSSGDownlinkMsg::text(os);
	os << " BSPDUType="<<getPDUType();
	os<<LOGHEX(mbdTLLI) <<LOGVAR(mbdPDULifetime);
	os <<" QoS skipped"; // Skip qos for now.
	if (mbdHaveOldTLLI) { os << LOGHEX(mbdOldTLLI); }
	os <<" RACap=(" <<mbdRACap <<")";
	os <<" IMSI=(" <<mbdIMSI <<")";
	//os <<" PDU=(" <<mbdPDU <<")";
	if (mbdPDU.size()) {
		os << " LLC DL payload="<<mbdPDU;
		SGSN::LlcFrameDump llcmsg(mbdPDU);
		os << " ";
		llcmsg.text(os);
	}
	os <<"\n";
}

};
