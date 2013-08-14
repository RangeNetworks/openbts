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

#include <ostream>
#include "GPRSL3Messages.h"
//#include "TBF.h"
#include "Sgsn.h"
#include "Ggsn.h"
#include "Utils.h"

namespace SGSN {
class SgsnError;

static bool sEnableMsRaCap = true;	// MsRaCap had alot of bugs, so provide a way to disable it.

#define CASENAME(x) case x: return #x;

const char *L3GmmMsg::name(unsigned mt, bool ornull)
{
	switch ((MessageType)mt) {
		CASENAME(AttachRequest)
		CASENAME(AttachAccept)
		CASENAME(AttachComplete)
		CASENAME(AttachReject)
		CASENAME(DetachRequest)
		CASENAME(DetachAccept)
		CASENAME(RoutingAreaUpdateRequest)
		CASENAME(RoutingAreaUpdateAccept)
		CASENAME(RoutingAreaUpdateComplete)
		CASENAME(RoutingAreaUpdateReject)
		CASENAME(ServiceRequest)
		CASENAME(ServiceAccept)
		CASENAME(ServiceReject)
		CASENAME(PTMSIReallocationCommand)
		CASENAME(PTMSIReallocationComplete)
		CASENAME(AuthenticationAndCipheringReq)
		CASENAME(AuthenticationAndCipheringResp)
		CASENAME(AuthenticationAndCipheringRej)
		CASENAME(AuthenticationAndCipheringFailure)
		CASENAME(IdentityRequest)
		CASENAME(IdentityResponse)
		CASENAME(GMMStatus)
		CASENAME(GMMInformation)
		default:
			return ornull ? 0 : "unrecognized L3GmmMsg type";
	}
}

void L3GmmMsg::text(std::ostream&os) const
{
	os <<name(MTI());
	textBody(os);
}

void L3GmmDlMsg::gWrite(ByteVector &msg)
{
	// Note: nibbles are reversed.
	msg.appendField(0,4);	// Skip indicator.
	msg.appendField(GSM::L3GPRSMobilityManagementPD,4);	// protocol discriminator.
	msg.appendByte(MTI());
	gmmWriteBody(msg);
}

void L3GmmUlMsg::gmmParse(L3GmmFrame &frame)
{
	try {
		size_t rp = frame.getBodyOffset();
		gmmParseBody(frame,rp);
	} catch (ByteVectorError) {
		SGSNERROR("Premature end of GPRS GMM message type "<<MTI()<<" "<<mtname());
		throw(SgsnError());
	}
}


void L3SmMsg::text(std::ostream&os) const {
	os <<name(MTI())<<LOGVAR2("TransactionId",mTransactionId);
	textBody(os);
}

void L3SmDlMsg::gWrite(ByteVector &msg) {
	assert(mTransactionId != -1);	// Must be specified in initializer.
	appendTiPd(msg);	// Append Transaction Id and PD to msg.
	msg.appendByte(MTI());
	smWriteBody(msg);
}

void L3SmUlMsg::smParse(L3SmFrame &frame) {
	try {
		mTransactionId = frame.getTI();
		size_t rp = frame.getBodyOffset();
		smParseBody(frame,rp);
	} catch (ByteVectorError) {
		SGSNERROR("Premature end of GPRS SM message type "<<MTI()<<" "<<mtname());
		throw(SgsnError());
	}
}

#define NAMEQ(name) " "<<name<<"="

// obsolete:
class GPRSL3Dumper {
	public:
	ByteVector vec;
	std::ostream &os;
	size_t wp;
	GPRSL3Dumper(ByteVector &wVec, std::ostream &wos) : vec(wVec), os(wos), wp(0) {}
	void dumpval(int len) {
		os << hex;
		for (int i = 0; i < len; i++) {
			os <<((unsigned) vec.getByte(wp++));
		}
		os << dec;
		wp += len;
	}
	void dumpV(const char *name, int len) {
		os << NAMEQ(name) << "(";
			dumpval(len);
		os << ")";
		wp += len;
	}
	void dumpLV(const char *name) {
		int len = vec.readByte(wp);
		os << NAMEQ(name) <<"(";
			os << NAMEQ("l")<<len <<NAMEQ("v"); dumpval(len);
		os << ")";
		wp += len;
	}
	// The specs give the low nibble first, then the high nibble.
	void dumpNibble(const char *name,int hi) {
		if (name) {
			os << NAMEQ(name)<< ((int)vec.getNibble(wp,hi));
		}
		if (hi) { wp++; }
	}
	void dumpOT(int iei, const char *name) {
		// Just the IEI existence is all there is.
		if (vec.getByte(wp) == iei) { wp++; os<<name<<"=true ";}
	}
	void dumpOTV(int iei, const char *name, int len) {
		if (vec.getByte(wp) == iei) { wp++; dumpV(name,len); }
	}
	void dumpOTLV(int iei, const char *name) {
		if (vec.getByte(wp) == iei) { wp++; dumpLV(name); }
	}
	void finish() {}
};


void L3GmmMsg::dump(L3GmmFrame &frame,std::ostream &os)
{
	if (frame.size() < 2) { os <<"L3 Message size too small ="<<frame.size(); return; }
  try {
	// Standard L3 header:
	unsigned mt = frame.getMsgType();
	os <<" type="<<name(mt);
	os <<LOGVAR2("pd",frame.getPD()) << LOGVAR2("skip",frame.getSkip());
		//<<" skip="<<((int)getNibble(0,1));
	//os <<" pd="<<((int)getNibble(0,0))
		//<<" skip="<<((int)getNibble(0,1));

	GPRSL3Dumper p(frame,os);
	p.wp += 2;
	switch (mt) {
	case AttachRequest:
		p.dumpLV("MS Network Capability");
		p.dumpNibble("AttachType",0);
		p.dumpNibble("CipheringKeySeq",1);
		p.dumpV("DRXParameter",2);
		p.dumpLV("P-TMSIorIMSI");
		p.dumpV("Old RAId",6);
		p.dumpLV("MS RadioAccessCap");
		p.dumpOTV(0x19,"Old P-TMSI signature",4);
		p.dumpOTV(0x17,"Requested READY timer value",2);
		p.dumpOTV(0x9,"TMSI status",1);
		p.dumpOTLV(0x33,"PS LCS Capability");
		p.dumpOTLV(0x11,"MS classmark2");
		p.dumpOTLV(0x20,"MS classmark3");
		p.dumpOTLV(0x40,"Supported Codecs");
		p.dumpOTLV(0x58,"UE Network Capability");
		p.dumpOTLV(0x1a,"Additional Mobile Id");
		p.dumpOTLV(0x1b,"Additional Old RAId");
		p.dumpOTLV(0x5d,"Voice domain preference");
		p.finish();
		break;
	case AttachAccept:
		p.dumpNibble("Attach Result",0);
		p.dumpNibble("Force to Standby",1);
		p.dumpV("Periodic RA Update Timer",1);
		p.dumpNibble("Radio Priority for SMS",0);
		p.dumpNibble("Radio Priority for TOM8",1);
		p.dumpV("RA Id",6);
		p.dumpOTV(0x19,"P-TMSI signature",4);
		p.dumpOTV(0x17,"Negotiated READY timer value",2);
		p.dumpOTLV(0x18,"Allocated P-TMSI");
		p.dumpOTLV(0x23,"MS Id");
		p.dumpOTV(0x25,"GMM cause",2);
		p.dumpOTLV(0x2A,"T3302 value");
		p.dumpOT(0x8C,"Cell Notification");
		p.dumpOTLV(0x4A,"Equiv PLMNs");
		// This might Bx where x is a dont care.
		p.dumpOTV(0xB,"Network feature support",1);
		p.dumpOTLV(0x34,"Emergency number list");
		p.dumpOTV(0xA,"Requested MS Info",1);
		p.dumpOTLV(0x37,"T3319 value");
		p.dumpOTLV(0x38,"T3323 value");
		p.finish();
		break;
	case AttachComplete:
		// nothing interesting
		break;
	case AttachReject:
		p.dumpV("GMM cause",1);
		p.dumpOTLV(0x2a,"T3302 value");
		p.finish();
	case PTMSIReallocationCommand:
		p.dumpLV("Allocated P-TMSI");
		p.dumpV("RA Id",6);
		p.dumpNibble("Force to standby",0);
		p.dumpNibble("",1);
		p.dumpOTV(0x19,"P-TMSI signature",4);
		p.finish();
		break;
	case IdentityResponse:
		p.dumpLV("MS Id");
		p.finish();
		break;
	case RoutingAreaUpdateRequest:
		// todo
		break;
	case RoutingAreaUpdateAccept:
		// todo
		break;
	}
  } catch (ByteVectorError) {
	os <<"ERROR: premature end of message";
  }
}

void L3SmMsg::dump(L3SmFrame &frame, std::ostream &os)
{
	// Standard L3 header:
	unsigned mt = frame.getMsgType();
	os <<LOGVAR2("type",name(mt));
	os <<LOGVAR2("pd",frame.getPD()) << LOGVAR2("TI",frame.getTI());

	// not implemented, and never will be because we did a full sgsn instead.
}

std::ostream& operator<<(std::ostream& os, L3GmmMsg::MessageType mt)
{
	os << L3GmmMsg::name(mt);
	return os;
}

const char *L3SmMsg::name(unsigned mt, bool ornull)
{
	switch ((MessageType)mt) {
		CASENAME(ActivatePDPContextRequest)
		CASENAME(ActivatePDPContextAccept)
		CASENAME(ActivatePDPContextReject)
		CASENAME(RequestPDPContextActivation)
		CASENAME(RequestPDPContextActivationReject)
		CASENAME(DeactivatePDPContextRequest)
		CASENAME(DeactivatePDPContextAccept)
		CASENAME(ModifyPDPContextRequest)
		CASENAME(ModifyPDPContextAccept)
		CASENAME(ModifyPDPContextRequestMS)
		CASENAME(ModifyPDPContextAcceptMS)
		CASENAME(ModifyPDPContextReject)
		CASENAME(ActivateSecondaryPDPContextRequest)
		CASENAME(ActivateSecondaryPDPContextAccept)
		CASENAME(ActivateSecondaryPDPContextReject)

		// These are old names, no longer sanctioned.
		CASENAME(ActivateAAPDPContextRequest)
		CASENAME(ActivateAAPDPContextAccept)
		CASENAME(ActivateAAPDPContextReject)
		CASENAME(DeactivateAAPDPContextRequest)
		CASENAME(DeactivateAAPDPContextAccept)

		CASENAME(SMStatus)

		CASENAME(ActivateMBMSContextRequest)
		CASENAME(ActivateMBMSContextAccept)
		CASENAME(ActivateMBMSContextReject)
		CASENAME(RequestMBMSContextActivation)
		CASENAME(RequestMBMSContextActivationReject)

		CASENAME(RequestSecondaryPDPContextActivation)
		CASENAME(RequestSecondaryPDPContextActivationReject)
		CASENAME(Notification)
		default:
			return ornull ? 0 : "urecgnized L3SmMsg type";
	}
}

std::ostream& operator<<(std::ostream& os, L3SmMsg::MessageType mt)
{
	os << L3SmMsg::name(mt);
	return os;
}

const char *L3GprsMsgType2Name(unsigned pd, unsigned mt)
{
	static char buf[40];	// Beware: will fail if multithreaded; GPRS is single threaded.
	switch ((GSM::L3PD) pd) {
	case GSM::L3GPRSMobilityManagementPD:	// Couldnt we shorten this?
		return L3GmmMsg::name(mt);
	case GSM::L3GPRSSessionManagementPD:	// Couldnt we shorten this?
		return L3SmMsg::name(mt);
	default:
		sprintf(buf,"unsupported PD: %d\n",pd);
		return buf;
	}
}

const char *L3GprsMsgType2Name(ByteVector &vec)
{
	unsigned pd = vec.getNibble(0,0);
	unsigned mt = vec.getByte(1);
	return L3GprsMsgType2Name(pd,mt);
}

void L3GprsFrame::dump(std::ostream &os)
{
	//L3GprsFrame frame(vec);
	os << "(";
	GSM::L3PD pd = getPD();
	switch ((GSM::L3PD) pd) {
	case GSM::L3GPRSMobilityManagementPD: {	// Couldnt we shorten this?
		L3GmmFrame gmframe(*this);
		L3GmmMsg::dump(gmframe,os);
		break;
	}
	case GSM::L3GPRSSessionManagementPD: {	// Couldnt we shorten this?
		L3SmFrame smframe(*this);
		L3SmMsg::dump(smframe,os);
		break;
	}
	default:
		os << "unsupported PD:"<<pd;
	}
	os << ")";
}

void GMMAttach::gmParseIEs(L3GmmFrame &src, size_t &rp, const char *culprit)
{
	// All subsequent IEs are optional.
	while (rp < src.size()) {
		unsigned iei = src.readIEI(rp);
		//SGSNLOG(format("debug: gmParseIes %s iei=0x%x rp=%d size=%d\n",culprit,iei,rp,src.size()));
		if ((iei & 0xf0) == 0x90) {
			// 10.5.5.4 TMSI status: high nibble is 9, low bit is TMSI status.
			mTmsiStatus = iei & 1;
			continue;
		}
		switch (iei) {
		case 0x19:	// TV Old P-TMSI signature.
			// Dont have a 3 byte 'read' function so use getField then advance rp by 3.
			mOldPtmsiSignature = src.getField(rp,24);
			rp += 3;
			continue;
		case 0x17: // TV
			mRequestedReadyTimerValue = src.readByte(rp);
			continue;
		case 0x27:	// TV drx parameter
			mDrxParameter = src.readUInt16(rp);
			continue;
		}

		// All the rest are TLV
		// The specified length is of the ie itself, excluding the iei type and length byte.
		// Get the length, but dont move rp - let the IEs do that, because
		// some of them need the length byte.
		int len = src.getByte(rp);
		size_t nextrp = rp + len + 1;
		if (nextrp > src.size()) {	// last one will have nextrp == src.size()
			SGSNERROR("invalid message size in "<<culprit <<" bytes="<<src.hexstr());
			return;
		}
		switch (iei) {
		default:
			rp = nextrp;
			break;
		case 0x18:	// P-TMSI, hard coded in Attach Request, optional in RAUpdate
			mMobileId.parseLV(src,rp);
			break;
		case 0x31:	// This has a hard-code position in Attach Request,
					// but is an optional IE in RA Update Request.
			mMsNetworkCapability = src.readLVasBV(rp);
			break;
		case 0x32:	// PDP Context status.
			rp++;	// skip length
			mPdpContextStatus.mStatus[0] = src.readByte(rp);
			mPdpContextStatus.mStatus[1] = src.readByte(rp);
			break;
		case 0x33:	// Location services; just dump it.
			src.skipLV(rp,1,1,"PS LCS Capability");
			break;
		case 0x35:	// MBMS context status in RAUpdateRequest
			src.skipLV(rp,3,3,"MBMS context status");
		case 0x11:
			src.skipLV(rp,3,3,"Mobile station classmark 2");
			break;
		case 0x20:
			src.skipLV(rp,0,32,"Mobile station classmark 3");
			break;
		case 0x40:
			src.skipLV(rp,3,127,"Supported codecs");
			break;
		case 0x58:
			src.skipLV(rp,2,13,"UE nework capability");
			break;
		case 0x1A:
			mAdditionalMobileId.parseLV(src,rp);
			break;
		case 0x1B:
			src.skipLV(rp,6,6,"Additional old routing area id");
			break;
		case 0x5D:
			src.skipLV(rp,1,1,"Voice domain preference");
			break;
		}
		assert(rp == nextrp);
	}
}

// Decode an IMSI, IMEI or IMEISV type, which are all encoded the same.
// The result ByteVector size is a multiple of 4 bits, so use sizeBits() not size().
void GmmMobileIdentityIE::decodeIM(ByteVector &result) const
{
	result.setAppendP(0);
	bool isodd = (mIdData[0] & 0x8);
	result.appendField(mIdData[0]>>4,4);	// First nibble is in the high nibble of first byte.
	for (unsigned i = 1; i < mLen; i++) {
		// The dang nibbles are backwards.  What nimrods.
		result.appendField(mIdData[i]&0xf,4);
		// Last byte may only have one nibble:
		// Note that the even/odd indication includes the single nibble
		// in the first byte, so we counter-intuitively check isodd here
		// instead of iseven.
		if (i < mLen-1 || isodd) {
			result.appendField((mIdData[i]>>4)&0xf,4);
		}
	}
}

// 24.008 10.5.1.4 Mobile Identity
//void GmmMobileIdentityIE::setIMSI(ByteVector &imsi)
//{
//	int len = imsi.size();
//	if (len == 0) {
//		mPresent = false;
//		return;
//	}
//	mPresent = true;
//	mTypeOfId = 1;	// IMSI
//}

ByteVector GmmMobileIdentityIE::getImsi() const
{
	assert(isImsi());	// Caller was supposed to check this.
	ByteVector imsi(8);
	decodeIM(imsi);
	return imsi;
}

const string GmmMobileIdentityIE::getAsBcd() const
{
	ByteVector tmp(12);	// 12 is overkill.
	decodeIM(tmp);
	return tmp.hexstr();

	//unsigned i;
	//for (i=0; i<mLen && i<8;i++) { sprintf(&buf[2*i],"%02x",mIdData[i]); }
	//buf[i*2] = 0;
	//return buf;
}

void GmmMobileIdentityIE::parseLV(ByteVector &pp, size_t &rp)
{
	mPresent = true;
	mLen = pp.readByte(rp);	// length of value part, should be 5.
	if (mLen > 8) {
		LLCWARN( "unexpected Mobile Identity length:"<<mLen);
	}
	unsigned typeIdByte = pp.getByte(rp);
	mTypeOfId = typeIdByte & 7;

	if (isTmsi()) {	// It is a tmsi/ptmsi
		if (mLen != 5) {	// one byte for type of identity and flags, 4 for tmsi.
			LLCWARN("unexpected Mobile Identity TMSI length:"<<mLen);
		}
		mTmsi = pp.getUInt32(rp+1);
	} else {
		// We dont care what it is, so just copy the entire thing.
		if (mLen > 8) {
			LLCWARN("invalid Mobile Identity TMSI length (must be <=8):"<<mLen);
			mLen = 8;
		}
		//LLCDEBUG << "mobileid:"<<*(pp.begin()+rp)<<*(pp.begin()+rp+1)<<*(pp.begin()+rp+2)<<"\n";
		memcpy(mIdData,pp.begin()+rp,mLen);
		//LLCDEBUG << "mIdData:"<<(int)mIdData[0]<<(int)mIdData[1]<<(int)mIdData[2]<<(int)mIdData[3]<<"\n";
	}
#if 0
	int datalen = rp-1;
	if (datalen > 8) datalen = 8;
	unsigned char *datap = pp.begin() + rp + 1;

	mTypeOfId = typeIdByte & 7;
	int isOdd = typeIdByte & 8;
	switch (mTypeOfId) {
	case 0:	// No identity
		break;
	case 4:	// TMSI or P-TMSI
		if (len != 4) {
			LLCWARN("unexpected Mobile Identity TMSI length:"<<len);
		}
		mTmsi = pp.getUInt32(rp+1);
		break;
	case 1:	// IMSI
	case 2: // IMEI
	case 3: // IMEISV
		mIdData[0] = typeIdByte>>4;
		memcpy(&mIdData[1],datap,datalen);
		mNumDigits = (len-1)*2 - (isOdd?1:0);
		break;
	case 5: // TMGI and MBMS session id.
		memcpy(&mIdData[0],datap,datalen);
		break;
	default:
		LLCWARN("unexpecited Mobile Identity type:" << mTypeOfId);
		break;
	}
	// Assume it is p-tmsi and get it.
#endif
	rp += mLen;
}

void GmmMobileIdentityIE::text(std::ostream&os) const
{
	if (!mPresent) { os << "not present"; return; }
	switch (mTypeOfId) {
	case 1: os << LOGVAR2("IMSI",getAsBcd()); break;
	case 2: os << LOGVAR2("IMEI",getAsBcd()); break;
	case 3: os << LOGVAR2("IMEISV",getAsBcd()); break;
	case 4: os << LOGHEX2("(P)TMSI",mTmsi); break;
	case 5: os << LOGVAR2("TMGI",ByteVectorTemp((char*)mIdData,mLen).hexstr()); break;
	case 6: os << LOGVAR2("type6",ByteVectorTemp((char*)mIdData,mLen).hexstr()); break;
	case 7: os << LOGVAR2("type7",ByteVectorTemp((char*)mIdData,mLen).hexstr()); break;
	}
}

// write the length and value, but not the IEI type.
void GmmMobileIdentityIE::appendLV(ByteVector &msg)
{
	if (mTypeOfId == 4) {
		msg.appendByte(5);	// Length of IE for TMSI/PTMSI
		msg.appendByte(0xf4);
		msg.appendUInt32(mTmsi);
	} else {
		msg.appendByte(mLen);
		msg.append(mIdData,mLen);
	}
}

// 3GPP 24.008 10.5.6.5 in octets/sec
static const unsigned sPeakThroughputTableSize = 10;
static unsigned sPeakThroughputTable[sPeakThroughputTableSize] = {
	/* 0 */ 0,	// 0 means 'subscribed peak throughput'
	/* 1 */ 1000,
	/* 2 */ 2000,
	/* 3 */ 4000,
	/* 4 */ 8000,
	/* 5 */ 16000,
	/* 6 */ 32000,
	/* 7 */ 64000,
	/* 8 */ 128000,
	/* 9 */ 256000,
};

void SmQoS::setPeakThroughput(unsigned bytepSec)	// In Bytes/sec
{
	for (unsigned code = 1; code < sPeakThroughputTableSize; code++) {
		if (bytepSec <= sPeakThroughputTable[code]) { setPeakThroughputCode(code); }
	}
	setPeakThroughputCode(sPeakThroughputTableSize-1);
}


// Result is Bytes/sec
unsigned SmQoS::getPeakThroughput()
{
	unsigned code = getPeakThroughputCode();
	return code <= 9 ? sPeakThroughputTable[code] : 0;
}

// 3GPP 24.008 10.5.6.5  in octets/hour
static const unsigned sMeanThroughputTableSize = 19;
static unsigned sMeanThroughputTable[sMeanThroughputTableSize] = {
	/* 0 */ 0,
	/* 1 */ 100,
	/* 2 */ 200,
	/* 3 */ 500,
	/* 4 */ 1*1000,
	/* 5 */ 2*1000,
	/* 6 */ 5*1000,
	/* 7 */ 10*1000,
	/* 8 */ 20*1000,
	/* 9 */ 50*1000,
	/* 10 */ 100*1000,
	/* 11 */ 200*1000,
	/* 12 */ 500*1000,
	/* 13 */ 1*1000000,
	/* 14 */ 2*1000000,
	/* 15 */ 5*1000000,
	/* 16 */ 10*1000000,
	/* 17 */ 20*1000000,
	/* 18 */ 40*1000000
};

// Result is in Bytes/hour
// 0 means best effort.
unsigned SmQoS::getMeanThroughput()
{
	unsigned code = getMeanThroughputCode();
	return code <= 18 ? sMeanThroughputTable[code] : 0;
}

// We probably will not use this, just set 'best effort' using setMeanThroughputCode()
void SmQoS::setMeanThroughput(unsigned bytepHour)	// KBytes/sec
{
	for (unsigned code = 1; code < sMeanThroughputTableSize; code++) {
		if (bytepHour <= sMeanThroughputTable[code]) { setMeanThroughputCode(code); }
	}
	setMeanThroughputCode(sMeanThroughputTableSize-1);
}

// In kilobits/s.  24.008 table 10.5.165
void SmQoS::setMaxBitRate(unsigned kbitps, bool uplink)
{
	unsigned code;
	if (kbitps == 0) { code = 0xff; }
	else if (kbitps <= 64) { code = kbitps; }
	else if (kbitps <= 568) { code = 0x40 + ((kbitps-64)/8); }
	else if (kbitps <= 8640) { code = 0x80 + ((kbitps-576)/64); }
	else {
		// There is a way to encode this using extended bytes, but we wont need it.
		// Just use the max value:
		code = 0xfe;
	}
	if (uplink) {
		setMaxBitRateUplinkCode(code);
	} else {
		setMaxBitRateDownlinkCode(code);
	}
}

// Return -1 if not defined.
int SmQoS::getMaxBitRate(bool uplink)
{
	// The IE is variable sized and may not include these fields.
	if (size() <= 6) {return -1;}
	unsigned code = uplink ? getMaxBitRateUplinkCode() : getMaxBitRateDownlinkCode();
	if (code == 0xff) { return 0; }
	switch (code & 0xc0) {
	case 0: return code;
	case 0x40: return 64 + 8*(code & 0x3f);
	default: return 576 + 64*(code & 0x3f);
	}
}


// 3GPP 24.008 10.5.6.5
// Set defaults for PS [Packet Switched] services.
// For streaming video, set udp to true.
// Specify rates in KBytes/sec, using K=1000 not 1024.
void SmQoS::defaultPS(unsigned rateDownlink, unsigned rateUplink)
{
	setDelayClass(4);	// best effort
	// Reliability Class:
	// 3 => unacknowledged GTP and LLC, acknowledged RLC, protected data.
	// 4 =>  unacknowledged GTP and LLC, RLC, protected data.
	// 5 =>  unacknowledged GTP and LLC, RLC, unprotected data.
	setReliabilityClass(3);
	//mPeakThroughput = 6);	// 32k/s 1 is lowest peak throughput
	unsigned peakThroughput = 1000* max(rateUplink,rateDownlink);
	setPeakThroughput(peakThroughput);
	setPrecedenceClass(2);	// normal
	setMeanThroughput(0x1f);	// best effort
	// Traffic class: 3 => Interactive, 4=background, 2=streaming, 1=conversational
	setTrafficClass(3);
	setDeliveryOrder(2);		// unordered
	setDeliveryOfErrSdu(2);	// yes, or could use 1 = nodetect
	setMaxSduSize(0x99);			// 1520 bytes
	setMaxBitRate(8*rateDownlink,0);
	setMaxBitRate(8*rateUplink,1);
	//setMaxBitRateForUplinkCode(0x3f);	// 63kbps; anything from 1 to 0x3f is value * 1k
	//setMaxBitRateForDownlinkCode(0x3f);	// 63kbps
	setResidualBER(1);			// 5e-2
	setSduErrorRatio(1);			// 1e-2
	setTransferDelay(0x10);		// 200ms
	setTrafficHandlingPriority(3);	// value 1-3.
	setGuaranteedBitRateUplinkCode(0xff);	// 0k, ignored for traffic class interactive.
	setGuaranteedBitRateDownlinkCode(0xff);	// 0k, ignored for traffic class interactive.
	setSignalingIndication(0);		// not optimized
	// Source statistics: 0=unknown, 1 =speech, but:
	// "The Source Statistics Descriptor value is ignored if the Traffic Class
	//  is Interactive class or Background Class"
	setSourceStatisticsDescriptor(0);
}

const char *AccessTechnologyType2Name(AccessTechnologyType type)
{
	switch (type) {
		CASENAME(GSM_P)
		CASENAME(GSM_E)
		CASENAME(GSM_R)
		CASENAME(GSM_1800)
		CASENAME(GSM_1900)
		CASENAME(GSM_450)
		CASENAME(GSM_480)
		CASENAME(GSM_850)
		CASENAME(GSM_750)
		CASENAME(GSM_T380)
		CASENAME(GSM_T410)
		CASENAME(GSM_UNUSED)
		CASENAME(GSM_710)
		CASENAME(GSM_T810)
		default: return "unknown AccessTechnologyType";
	}
}

const char *AccessCapabilities::CapName(CapType type) const
{
	switch (type) {
		//CASENAME(eAccessTechnologyType)
		CASENAME(RFPowerCapability)
		CASENAME(A5Bits)
		CASENAME(ESInd)
		CASENAME(PS)
		CASENAME(VGCS)
		CASENAME(VBS)
		// multislot capabilities:
		CASENAME(HSCSDMultislotClass)
		CASENAME(GPRSMultislotClass)
		CASENAME(GPRSExtendedDynamicAllocationCapability)
		CASENAME(SMS_VALUE)
		CASENAME(SM_VALUE)
		CASENAME(ECSDMultislotClass)
		CASENAME(EGPRSMultislotClass)
		CASENAME(EGPRSExtendedDynamicAllocationCapability)
		CASENAME(DTMGPRSMultiSlotClass)
		CASENAME(SingleSlotDTM)
		CASENAME(DTMEGPRSMultiSlotClass)
		// Additions in release 99:
		CASENAME(EightPSKPowerCapability)
		CASENAME(COMPACTInterferenceMeasurementCapability)
		CASENAME(RevisionLevelIndicator)
		CASENAME(UMTSFDDRadioAccessTechnologyCapability)
		CASENAME(UMTS384McpsTDDRadioAccessTechnologyCapability)
		CASENAME(CDMA2000RadioAccessTechnologyCapability)
		// Additions in release 4:
		CASENAME(UMTS128McpsTDDRadioAccessTechnologyCapability)
		CASENAME(GERANFeaturePackage1)
		CASENAME(ExtendedDTMGPRSMultiSlotClass)
		CASENAME(ExtendedDTMEGPRSMultiSlotClass)
		CASENAME(ModulationBasedMultislotClassSupport)
		// Additions in release 5:
		CASENAME(HighMultislotCapability)
		//eGMSKPowerClass
		//EightPSKPowerClass
		default: return "unknown CapName";
	}
}

void AccessCapabilities::parseAccessCapabilities(
	ByteVector &bv,		// bytevector we are parsing
	size_t &rp,			// location in bytevector
	AccessCapabilities *prev,	// Previous capabilities or null.
	size_t end)			// end of capabilities list
{
	mCaps[RFPowerCapability] = bv.readField(rp,3);
	if (bv.readField(rp,1)) { mCaps[A5Bits] = bv.readField(rp,7); }
	else if (prev) { mCaps[A5Bits] = prev->mCaps[A5Bits]; }
	mCaps[ESInd] = bv.readField(rp,1);
	mCaps[PS] = bv.readField(rp,1);
	mCaps[VGCS] = bv.readField(rp,1);
	mCaps[VBS] = bv.readField(rp,1);
	if (bv.readField(rp,1)) {	// multislot capability struct present
		if (bv.readField(rp,1)) {
			mCaps[HSCSDMultislotClass] = bv.readField(rp,5);
		}
		if (bv.readField(rp,1)) {
			mCaps[GPRSMultislotClass] = bv.readField(rp,5);
			mCaps[GPRSExtendedDynamicAllocationCapability] = bv.readField(rp,1);
		}
		if (bv.readField(rp,1)) {
			mCaps[SMS_VALUE] = bv.readField(rp,4);
			mCaps[SM_VALUE] = bv.readField(rp,4);
		}
		// Additions in release 99:  Just scan past some of these, dont bother saving.
		if (rp >= end) return;
		if (bv.readField(rp,1)) {	// ECSD multslot class
			bv.readField(rp,5);	// Toss it.
		}
		if (rp >= end) return;
		if (bv.readField(rp,1)) {	// EGPRS multslot class
			mCaps[EGPRSMultislotClass] = bv.readField(rp,5);
			mCaps[EGPRSExtendedDynamicAllocationCapability] = bv.readField(rp,1);
		}
		if (rp >= end) return;
		if (bv.readField(rp,1)) {	// DTM GPRS Multi Slot Class present
			mCaps[DTMGPRSMultiSlotClass] = bv.readField(rp,2);
			mCaps[SingleSlotDTM] = bv.readField(rp,1);
			if (bv.readField(rp,1)) {	// DTM EGPRS Multi Slot Class present
				mCaps[DTMEGPRSMultiSlotClass] = bv.readField(rp,2);
			}
		}
	} else if (prev) {	// No multislot struct means same as previous.
		for (int i = HSCSDMultislotClass; i <= DTMEGPRSMultiSlotClass; i++) {
			mCaps[i] = prev->mCaps[i];
		}
	}

	// Additions in release 99
	if (rp >= end) return;
	if (bv.readField(rp,1)) { mCaps[EightPSKPowerCapability] = bv.readField(rp,2); }
	if (rp >= end) return;
	mCaps[COMPACTInterferenceMeasurementCapability] = bv.readField(rp,1);
	mCaps[RevisionLevelIndicator] = bv.readField(rp,1);
	mCaps[UMTSFDDRadioAccessTechnologyCapability] = bv.readField(rp,1);
	mCaps[UMTS384McpsTDDRadioAccessTechnologyCapability] = bv.readField(rp,1);
	mCaps[CDMA2000RadioAccessTechnologyCapability] = bv.readField(rp,1);
	// Additions in release 4:
	if (rp >= end) return;
	mCaps[UMTS128McpsTDDRadioAccessTechnologyCapability] = bv.readField(rp,1);
	if (rp >= end) return;
	mCaps[GERANFeaturePackage1] = bv.readField(rp,1);
	if (rp >= end) return;
	if (bv.readField(rp,1)) {
		mCaps[ExtendedDTMGPRSMultiSlotClass] = bv.readField(rp,2);
		mCaps[ExtendedDTMEGPRSMultiSlotClass] = bv.readField(rp,2);
	}
	if (rp >= end) return;
	mCaps[ModulationBasedMultislotClassSupport] = bv.readField(rp,1);
	// Additions in release 5:
	if (rp >= end) return;
	if (bv.readField(rp,1)) {
		mCaps[HighMultislotCapability] = bv.readField(rp,2);
	}
	// Rest ignored.
}

AccessCapabilities::CapType AccessCapabilities::mPrintList[] = {
		GPRSMultislotClass,
		GPRSExtendedDynamicAllocationCapability,
		GERANFeaturePackage1
		};

void AccessCapabilities::text2(std::ostream &os,bool verbose) const
{
	if (!sEnableMsRaCap ) {return;}
	if (!verbose) {	// TODO: how to switch to get the full list?
		// Short list:
		for (unsigned j = 0; j < sizeof(mPrintList)/sizeof(CapType); j++) {
			unsigned cap = mPrintList[j];
			if (mCaps[cap] != -1) { os <<LOGVAR2(CapName((CapType)cap),mCaps[cap]); }
		}
	} else {
		// Full list:
		for (int i = 0; i < CapsMax; i++) {
			if (mCaps[i] != -1) { os <<LOGVAR2(CapName((CapType)i),mCaps[i]); }
		}
	}
}

void AccessCapabilities::text(std::ostream &os) const { text2(os,0); }

void MsRaCapability::parseMsRaCapability()
{
	if (!sEnableMsRaCap ) {return;}
	size_t rp = 0;
	for (int numTechs = 0; numTechs < sMsRaCapMaxTypes; numTechs++) {
		try {
			mCList[numTechs].mTechType = (AccessTechnologyType) readField(rp,4);
			unsigned len = readField(rp,7);
			mCList[numTechs].mValid = true;
			if (mCList[numTechs].mTechType == 0xf) {
				// Special case means same as previous.
				mCList[numTechs].mSameAsPrevious = true;
				readField(rp,1);	// Extraneous bit, not included in the length?
				size_t trp = rp;
				mCList[numTechs].mTechType = (AccessTechnologyType) readField(trp,4);
				if (numTechs > 0) {	// Otherwise it is an error on the part of the MS.
					*mCList[numTechs].mCaps = *mCList[numTechs-1].mCaps;
				}
				// There are two more fields: GMSK Power Class and 8PSK Power Class
				// which are included in the length and so skipped over without any more code here.
			} else {
				size_t trp = rp;
				AccessCapabilities *prev = numTechs ? &mCList[numTechs-1] : 0;
				mCList[numTechs].parseAccessCapabilities(*this,trp,prev,rp+len);
			}
			// Advance rp.
			rp += len;
			if (rp + 15 >= sizeBits()) { break; }	// Not enough room left for anything useful.
			if (readField(rp,1) == 0) { break; }	// End of list marker.
		} catch(ByteVectorError) {	// oops!
			break;	// End of that.
		}
	}
}

void MsRaCapability::text2(std::ostream &os, bool verbose) const
{		
	if (!sEnableMsRaCap ) {return;}
	for (int numTechs = 0; numTechs < sMsRaCapMaxTypes; numTechs++) {
		if (! mCList[numTechs].mValid) {continue;}
		// Dont bother to print the types that Range does not support.
		AccessTechnologyType atype = mCList[numTechs].mTechType;
		switch (atype) {
		case GSM_E: case GSM_850: case GSM_1800: case GSM_1900:
			os << (verbose ? "\t" : " ");
			os <<" MsRaCapability[" << AccessTechnologyType2Name(atype) << "]=(";
			if (mCList[numTechs].mSameAsPrevious) {
				os <<"same";
			} else {
				mCList[numTechs].text2(os,verbose);
			}
			os <<")\n";
		default: continue;
		}
	}
}

void MsRaCapability::text(std::ostream &os) const { text2(os,0); }

void L3GmmMsgServiceRequest::gmmParseBody(L3GmmFrame &src, size_t &rp)
{
	mServiceType = src.getNibble(rp,0);
	mCypheringKeySequenceNumber = src.getNibble(rp,1);
	rp++;
        mMobileId.parseLV(src,rp);
        if (rp < src.size()) {
                unsigned iei = src.readIEI(rp);
		if (iei == 0x32) {
                        rp++;   // skip length
                        mPdpContextStatus.mStatus[0] = src.readByte(rp);
                        mPdpContextStatus.mStatus[1] = src.readByte(rp);
		}
	}
}

void L3GmmMsgServiceAccept::gmmWriteBody(ByteVector &msg)
{
	msg.appendByte(0x32);
	msg.appendByte(0x02);
	msg.appendByte(mPdpContextStatus.mStatus[0]);
	msg.appendByte(mPdpContextStatus.mStatus[1]);
}

void L3GmmMsgServiceReject::gmmWriteBody(ByteVector &msg)
{
        msg.appendByte(mGmmCause);
}

void L3GmmMsgRAUpdateRequest::gmmParseBody(L3GmmFrame &src, size_t &rp)
{
	unsigned update_type = src.getNibble(rp,0);
	mUpdateType = update_type & 7;
	mFollowOnRequestPending = !!(update_type&8);
	mCypheringKeySequenceNumber = src.getNibble(rp,1);
	rp++;
	mOldRaId.parseElement(src,rp);
	mMsRadioAccessCapability = src.readLVasBV(rp);
	gmParseIEs(src,rp,"RAUpdateRequest");
}

void L3GmmMsgRAUpdateAccept::gmmWriteBody(ByteVector &msg)
{
	//msg.appendByte(RoutingAreaUpdateAccept);
	msg.appendField(mUpdateResult,4);	// nibbles reversed.
	msg.appendField(mForceToStandby,4);
	//msg.appendByte(mPeriodicRAUpdateTimer.getIEValue());
	mPeriodicRAUpdateTimer.appendElement(msg);
	GMMRoutingAreaIdIE mRaId;
	mRaId.raLoad();
	mRaId.appendElement(msg);
	// End of mandatory IEs.

	// Add the allocated P-TMSI:
	if (mAllocatedPTmsi) {
		msg.appendByte(0x18);	// Allocate P-TMSI IEI
		GmmMobileIdentityIE midtmp;
		midtmp.setTmsi(mAllocatedPTmsi);
		midtmp.appendLV(msg);
	}

	// Add the mobile identity that it sent to us:
	//msg.appendByte(0x23);	// MS identity IEI.
	//mMobileId.appendLV(msg);

	if (mTmsi) {
		msg.appendByte(0x23);	// MS identity IEI.
		GmmMobileIdentityIE idtmp;
		idtmp.setTmsi(mTmsi);
		idtmp.appendLV(msg);
	}

	// 10.5.7.1 PDP Context Status
	// And I quote: "This IE shall be included by the Network".  Hmm.
	// If you set this to zeros the MS relinquishes its PDP contexts.
	{
		msg.appendByte(0x32);
		msg.appendByte(2);	// length is 2 bytes
		msg.appendByte(mPdpContextStatusCurrent.mStatus[0]);
		msg.appendByte(mPdpContextStatusCurrent.mStatus[1]);
	}
}

void L3GmmMsgRAUpdateReject::gmmWriteBody(ByteVector &msg)
{
	//msg.appendByte(RoutingAreaUpdateReject);
	msg.appendByte(mGmmCause);
	msg.appendByte(0);		// spare half octet and force-to-standby
}

void L3GmmMsgAttachAccept::gmmWriteBody(ByteVector &msg)
{
	mPeriodicRAUpdateTimer.setSeconds(gConfig.getNum("SGSN.Timer.RAUpdate"));
	mReadyTimer.setSeconds(gConfig.getNum("SGSN.Timer.Ready"));

	//msg.appendByte(AttachAccept);		// message type
	msg.appendField(mForceToStandby,4);	// high nibble first.
	msg.appendField(mAttachResult,4);
	//msg.appendByte(mPeriodicRAUpdateTimer.getIEValue());
	mPeriodicRAUpdateTimer.appendElement(msg);
	// Next byte is SMS and TOM8 message priority.
	// I am hard coding them to the lowest value, which is 4.
	msg.appendByte(0x44);
	GMMRoutingAreaIdIE mRaId;
	mRaId.raLoad();
	mRaId.appendElement(msg);
	// End of mandatory elements.

	// Add the allocated P-TMSI:
	if (mPTmsi) {
		// TLV Allocated P-TMSI, but only if we send it.
		GmmMobileIdentityIE idtmp;
		idtmp.setTmsi(mPTmsi);
		msg.appendByte(0x18);	// Allocated P-TMSI IEI
		idtmp.appendLV(msg);
	}

	// 6-7-2012: Removed this.  Per 24.008 9.4.2: Attach Accept Description,
	// I now believe the second mobile identity is included only to set
	// TMSI in case of a combined attach, so we should not include
	// this IE unless using NMO 1 and we support the combined attach.
	//if (mMobileId.mPresent) {
	// msg.appendByte(0x23);	// MS identity IEI.
	// mMobileId.appendLV(msg);
	//}
	// Note: you can also set timer T3302, T3319, T3323 values.
	// These are all MM timers in Table 11.3a page 545, and not too interesting.
}

void L3GmmMsgAttachRequest::gmmParseBody(L3GmmFrame &src, size_t &rp)
{
	mMsNetworkCapability = src.readLVasBV(rp);
	mAttachType = src.getNibble(rp,0);
	mCypheringKeySequenceNumber = src.getNibble(rp,1);
	rp++;	// skip to end of nibbles we just read, above.
	mDrxParameter = src.readUInt16(rp);
	mMobileId.parseLV(src,rp);
	mOldRaId.parseElement(src,rp);
	mMsRadioAccessCapability = src.readLVasBV(rp);
	gmParseIEs(src,rp,"GmmAttachRequest");
}

// 9.5.4.2 Mobile Originated Detach Request.
void L3GmmMsgDetachRequest::gmmParseBody(L3GmmFrame &src, size_t &rp)
{
	mDetachType = 0xf & src.readByte(rp);		// Low nibble is detach type, high nibble is unused.
	mMobileId.parseLV(src,rp);
	while (rp < src.size()) {
		unsigned iei = src.readIEI(rp);
		switch (iei) {
		case 0x18:	// P-TMSI
			mMobileId.parseLV(src,rp);
			mMobileIdPresent = true;
			break;
		case 0x19: // P-TMSI signature.  Skip it.
			src.skipLV(rp,3,3,"P-TMSI signature");
			break;
		default: // Unknown IEI.
			src.skipLV(rp);
			break;
		}
	}
}

// 9.5.4.1 Network Originated Detach Request.
void L3GmmMsgDetachRequest:: gmmWriteBody(ByteVector &msg)
{
	msg.appendByte((mForceToStandby<<4) | mDetachType);
	if (mGmmCausePresent) {
		msg.appendByte(0x25);
		msg.appendUInt16(mGmmCause);
	}
}

void L3GmmMsgDetachRequest::textBody(std::ostream &os) const
{
	// The contents are different in uplink and downlink directions.
	// We just print anything that has a 'present' flag set.
	os<<LOGVAR(mDetachType)<<LOGVAR(mForceToStandby);
	if (mGmmCausePresent) { os<<LOGVAR(mGmmCause)<<"="<<GmmCause::name(mGmmCause); }
	if (mMobileIdPresent) { os<<LOGVAR2("mobileId",mMobileId.str()); }
}

void L3GmmMsgDetachAccept::gmmWriteBody(ByteVector &msg)
{
	msg.appendByte(mForceToStandby&0xf);	// low nibble is ForceToStandby, high nibble is unused.
}
void L3GmmMsgDetachAccept::textBody(std::ostream &os) const
{
	// The ForceToStandby is only in the dowlink direction, but oh well...
	os << LOGVAR(mForceToStandby);
}

void L3GmmMsgIdentityRequest::gmmWriteBody(ByteVector &msg)
{
	msg.appendByte(((int)mForceToStandby<<4) | (int)mIdentityType);
}

void L3GmmMsgIdentityRequest::textBody(std::ostream &os) const
{
	os<<LOGVAR(mIdentityType) <<LOGVAR(mForceToStandby);
}

void L3GmmMsgIdentityResponse::gmmParseBody(L3GmmFrame &src, size_t &rp)
{
	mMobileId.parseLV(src,rp);
}

void L3GmmMsgIdentityResponse::textBody(std::ostream &os) const
{
	os<<LOGVAR2("mobileId",mMobileId.str());
}

void L3GmmMsgAuthentication::gmmWriteBody(ByteVector &msg)
{
	// Ciphering algorithm nibble - all zero = no ciphering
	// IMEISV request nibble - all zero = not requested.
	msg.appendByte(0);
	// Force to standby nibble - zero = no.
	// A&C reference number - zero is a find reference number.
	msg.appendByte(0);
	// The optional rand IE, because we are trying to authtenticate.
	msg.appendByte(0x21);
	// 128 bit rand.
	msg.append(mRand);
	// CKSN must be included
	msg.appendByte(0x80 | 0x00); //IE;
}

void L3GmmMsgAuthenticationResponse::gmmParseBody(L3GmmFrame &src, size_t &rp)
{
	// 9.4.10 of 24.008
        unsigned char ACrefnum = src.readByte(rp); // ignore for now
        ByteVector SRES(4);

        // optional ieis:
        while (rp < src.size()) {
                unsigned iei = src.readIEI(rp);
                switch (iei) {
                case 0x22: // SRES/RES 4 bytes
			for (int i = 0; i < 4; i++) 
				SRES.setByte(i,src.readByte(rp));
			mSRES = SRES;	
                        break;
                case 0x23: // IMEISV  TLV of 11 bytes
			// ignore for now;
		case 0x29: // Authentication Response parameter extension (AUTN) TLV 3-14
			// ignore for now;
			{
				unsigned int len = src.readByte(rp);
				for (unsigned int i = 0; i < len; i++) src.readByte(rp);
			}
			break;
                default:
                        break;
                }
        }
}

// 24.008 9.5.1
void L3SmMsgActivatePdpContextRequest::smParseBody(L3SmFrame &src, size_t &rp)
{
	mRequestType = 0;
	//size_t rp = parseSmHeader(src);
	// PD, transaction id, message type already parsed.
	mNSapi = src.readByte(rp);
	mLlcSapi = src.readByte(rp);
	// In 4.008 QoS [Quality of Service] is 3 bytes long.
	// In 24.008 QoS is specified as 13-17 bytes long in the message description
	// but if you look in the 10.5.6.5. IE description it says 3 bytes is ok.
	// The blackberry sends 3 bytes in 2G gprs mode.
	mQoS = src.readLVasBV(rp);	// 10.5.6.5

	// We're going to ignore almost everything else.
	mPdpAddress = src.readLVasBV(rp);	// 10.5.6.4

	// optional ieis:
	while (rp < src.size()) {
		unsigned iei = src.readIEI(rp);
		if ((iei & 0xf0) == 0xa0) {
			mRequestType = iei & 0xf;	// 10.5.6.17 Request type
			// 1 is initial request, 2 is handover, 4 is emergency.
			continue;
		}
		int len = src.getByte(rp);
		size_t nextrp = rp + len + 1;
		if (nextrp > src.size()) {	// last one will have nextrp == src.size()
			SGSNERROR("invalid message size in ActivatePdpContextRequest");
			return;
		}
		switch (iei) {
		case 0x28:
			mApName = src.readLVasBV(rp);	// 10.5.6.1
			continue;
		case 0x27:
			mPco = src.readLVasBV(rp);	// 10.5.6.3
			continue;
		default:
			rp = nextrp;
			continue;
		}
	}
}

// 3GPP 24.007 11.2.3.1.3 Transaction Identifier.
// First bit is the TIflag that must be set for a message reply.
// And I quote: "A message has a TI flag set to "0" when it belongs to transaction initiated by its sender,
// and to "1" otherwise."
// Transaction id values > 7 requre an extension byte.
void L3SmDlMsg::appendTiPd(ByteVector &msg)
{
	unsigned tiFlag = isSenseCmd() ? 0 : 0x8;
	// Note: nibbles are reversed, as always for L3 messages.
	if (mTransactionId < 7) {
		msg.appendField(tiFlag|mTransactionId,4);	// Transaction id.  The 0x8 indicates this is a command.
		msg.appendField(GSM::L3GPRSSessionManagementPD,4);	// protocol discriminator.
	} else {
		msg.appendField(tiFlag|0x7,4);	// Magic value indicates additional transaction id field present.
		msg.appendField(GSM::L3GPRSSessionManagementPD,4);	// protocol discriminator.
		msg.appendByte(0x80|mTransactionId);	// The 0x80 is a required but meaningless extension indicator.
	}
}


void L3SmMsgActivatePdpContextAccept::smWriteBody(ByteVector &msg)
{
	//msg.appendByte(L3SmMsg::ActivatePDPContextAccept);
	msg.appendByte(mLlcSapi);
	// LV: QoS.
	msg.appendByte(mQoS.size());
	msg.append(mQoS);
	msg.appendField(0,4);	// "spare half octet"
	msg.appendField(mRadioPriority,4);
	// TLV:  Pdp Address
	msg.appendByte(0x2b);	// IEI
	msg.appendByte(mPdpAddress.size());
	msg.append(mPdpAddress);

	// TLV: Protocol Configuration Options.
	msg.appendByte(0x27);	// IEI
	msg.appendByte(mPco.size());
	msg.append(mPco);
}

// 24.008 9.5.14
void L3SmMsgDeactivatePdpContextRequest::smParseBody(L3SmFrame &src, size_t &rp)
{
	//size_t rp = parseSmHeader(src);
	if (rp >= src.size()) {
		SGSNERROR("DeactivatePdpContextRequest too short according to spec");
		mCause = 0;
		return;	// But we dont care.
	}
	mCause = src.readByte(rp);
	// optional ieis:
	while (rp < src.size()) {
		unsigned iei = src.readIEI(rp);
		if ((iei & 0xf0) == 0x90) {
			mTearDownIndicator = iei & 0x1;	// 10.5.6.10
			continue;
		}
		if (rp >= src.size()) {break;}	// This is an error, but we dont care.
		int len = src.getByte(rp);
		if (iei == 0x27) {
			mPco = src.readLVasBV(rp);	// 10.5.6.3
			continue;
		}
		// Ignoring; optional MBMS protocol configuration options
		// Ignoring: any IEs not in our spec.
		rp = rp + len + 1;
	}
}

void L3SmMsgDeactivatePdpContextRequest::smWriteBody(ByteVector &msg)
{
	//msg.appendByte(L3SmMsg::DeactivatePDPContextRequest);
	msg.appendByte(mCause);
	if (mTearDownIndicator) {
		msg.appendByte(0x91);
	}
	// Ignoring:
	// optional protcol configuration options
	// optional MBMS protocol configuration options
}
void L3SmMsgDeactivatePdpContextRequest::textBody(std::ostream &os) const
{
	os<<LOGVAR(mTearDownIndicator) <<LOGVAR(mCause)<<"="<<SmCause::name(mCause)
		<<" PCO="<<mPco.str();
}

void L3SmMsgSmStatus::smWriteBody(ByteVector &msg)
{
	//msg.appendByte(L3SmMsg::SMStatus);
	msg.appendByte(mCause);
}

void L3GmmMsgRAUpdateReject::textBody(std::ostream &os) const
{
	os <<LOGVAR2("GmmCause",(int)mGmmCause)<<"="<<GmmCause::name(mGmmCause);
}

void L3SmMsgActivatePdpContextReject::smWriteBody(ByteVector &msg)
{
	//msg.appendByte(L3SmMsg::ActivatePDPContextReject);
	msg.appendByte((unsigned)mCause);
}

void L3SmMsgActivatePdpContextReject::textBody(std::ostream &os) const
{
	os <<LOGVAR(mCause)<<"="<<GmmCause::name(mCause);
}

void L3GmmMsgGmmStatus::textBody(std::ostream &os) const
{
	os <<LOGVAR(mCause)<<"="<<GmmCause::name(mCause);
}


void L3SmMsgSmStatus::textBody(std::ostream &os) const
{
	os <<LOGVAR(mCause)<<"="<<SmCause::name(mCause);
}

// When virtual functions are used, C++ requires at least one function to be declared
// outside the class definition, even if empty.
void L3SmMsgDeactivatePdpContextAccept::textBody(std::ostream &os) const { /*nothing*/ }

void L3GmmMsgRAUpdateComplete::textBody(std::ostream &os) const {/*nothing*/}

void GMMRoutingAreaIdIE::parseElement(ByteVector &pp, size_t &rp)
{
	mMCC[1] = pp.getNibble(rp,1);
	mMCC[0] = pp.getNibble(rp,0);
	mMNC[2] = pp.getNibble(rp+1,1);
	mMCC[2] = pp.getNibble(rp+1,0);
	mMNC[1] = pp.getNibble(rp+2,1);
	mMNC[0] = pp.getNibble(rp+2,0);
	mLAC = pp.getUInt16(rp+3);
	mRAC = pp.getByte(rp+5);
	rp += 6;
}

void GMMRoutingAreaIdIE::appendElement(ByteVector &msg)
{
	msg.appendByte((mMCC[1]<<4) | mMCC[0]);
	msg.appendByte((mMNC[2]<<4) | mMCC[2]);
	msg.appendByte((mMNC[1]<<4) | mMNC[0]);
	msg.appendUInt16(mLAC);
	msg.appendByte(mRAC);
}

GMMRoutingAreaIdIE::GMMRoutingAreaIdIE()
{
	mMCC[0] = mMCC[1] = mMCC[2] = 0;
	mMNC[0] = mMNC[1] = mMNC[2] = 0;
}

void GMMRoutingAreaIdIE::text(std::ostream&os) const
{
	os <<format("MCC=%c%c%c MNC=%c%c%c",
		DEHEXIFY(mMCC[0]), DEHEXIFY(mMCC[1]), DEHEXIFY(mMCC[2]),
		DEHEXIFY(mMNC[0]), DEHEXIFY(mMNC[1]), DEHEXIFY(mMNC[2]));
	os <<LOGVAR2("LAC",mLAC+0)<<LOGVAR2("RAC",mRAC+0);
}

void GMMRoutingAreaIdIE::raLoad()
{
	const char* wMCC, *wMNC;
	if (Sgsn::isUmts()) {
		wMCC = gConfig.getStr("UMTS.Identity.MCC").c_str();
		wMNC = gConfig.getStr("UMTS.Identity.MNC").c_str();
		mLAC = gConfig.getNum("UMTS.Identity.LAC");
	} else {
		wMCC = gConfig.getStr("GSM.Identity.MCC").c_str();
		wMNC = gConfig.getStr("GSM.Identity.MNC").c_str();
		mLAC = gConfig.getNum("GSM.Identity.LAC");
	}
	mRAC = gConfig.getNum("GPRS.RAC");

	mMCC[0] = wMCC[0]-'0';
	mMCC[1] = wMCC[1]-'0';
	mMCC[2] = wMCC[2]-'0';
	mMNC[0] = wMNC[0]-'0'; 
	mMNC[1] = wMNC[1]-'0';
	// 24.008 10.5.5.15 says if only two digits, MNC[2] is 0xf.
	mMNC[2] = wMNC[2] ? (wMNC[2]-'0') : 0xf;
}

};	// namespace
