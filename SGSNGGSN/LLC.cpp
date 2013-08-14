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

#define LLC_IMPLEMENTATION 1
#include "GPRSL3Messages.h"
#include "Sgsn.h"
#include "Ggsn.h"
#include "LLC.h"
#define CASENAME(x) case x: return #x;

namespace SGSN {

const char *LlcSapi::name(type sapi)
{
	switch (sapi) {
		CASENAME(GPRSMM)
		CASENAME(TOM2)
		CASENAME(UserData3)
		CASENAME(UserData5)
		CASENAME(SMS)
		CASENAME(TOM8)
		CASENAME(UserData9)
		CASENAME(UserData11)
		default: return "unrecognized LlcSapi";
	}
}

const char *LLCFormat::name(type format)
{
	switch (format) {
		CASENAME(Invalid)
		CASENAME(I)
		CASENAME(S)
		CASENAME(UI)
		CASENAME(U)
		CASENAME(ISack)
		CASENAME(SSack)
		default: return "unrecognized LLCFormat";
	}
}

LLCFormat::type LlcFrame::getFormat()
{
	if (size() < 2) { return LLCFormat::Invalid; }
	unsigned tag = getControl(0);
	if ((tag & 0x80) == 0) {
		return (getControl(2) & 3) == 3 ? LLCFormat::ISack : LLCFormat::I;
	} else if ((tag & 0x40) == 0) {
		return (getControl(1) & 3) == 3 ? LLCFormat::SSack : LLCFormat::S;
	} else if ((tag & 0x20) == 0) {
		return LLCFormat::UI;
	} else {
		return LLCFormat::U;
	}
}

// C++ note: You cannot just return an LlcMsg here, or the result is back-converted to LlcMsg.
// You must return a pointer, even though they are barely supported by C++.  Gotta love it.
LlcMsg *LlcFrame::switchFrame()
{
	switch (getFormat()) {
		case LLCFormat::U:
			return new LlcFrameU(*this);
		case LLCFormat::UI: {
			return new LlcFrameUI(*this);
			//LlcFrameUI result = LlcFrameUI(*this);
			//LLCDEBUG << "switchFrame result="<<result.typeName()<<"\n";
			//return result;
			}
		case LLCFormat::ISack:
			LLCDEBUG("LLC ISack frame");
			return new LlcFrameSack(*this);
		case LLCFormat::I:
			return new LlcFrameI(*this);
		case LLCFormat::SSack:
			LLCDEBUG("LLC SSack frame");
			return new LlcFrameSack(*this);
		case LLCFormat::S:
			return new LlcFrameS(*this);
		default: assert(0);
	}
}

void LlcFrame::llcProcess1(LlcEntity *lle)
{
	switch (getFormat()) {
		case LLCFormat::U:
			LlcFrameU(*this).llcProcess(lle);
			break;
		case LLCFormat::UI: {
			LlcFrameUI(*this).llcProcess(lle);
			break;
			}
		case LLCFormat::ISack:
			LLCDEBUG("LLC ISack frame");
			LlcFrameSack(*this).llcProcess(lle);
			break;
		case LLCFormat::I:
			LlcFrameI(*this).llcProcess(lle);
			break;
		case LLCFormat::SSack:
			LLCDEBUG("LLC SSack frame");
			LlcFrameSack(*this).llcProcess(lle);
			break;
		case LLCFormat::S:
			LlcFrameS(*this).llcProcess(lle);
			break;
		default: assert(0);
	}
}

// Dont bother with the stupid thing, just return it and hope it didnt screw us up.
// The multitech modem sends: 03FB/16.01.F4.2C/634CCA header/xids/checksum
// The xids are:
// 		XID xidtype=5 xidlen=2 value=500
// 		XID xidtype=11 xidlen=0 value=0
// I replied with: 43FB/16.01.F4.2C and with: 43FB and with 03FB
// but none worked for the multitech modem.
static void handleXid(LlcEntity *lle, ByteVector &xids)
{
  try {
	int totlen = xids.size();	// 3 to remove the FCS checksum.
	// Create an outbound xid command
	LlcFrameXid uframe(totlen+5);	// Add room for 2 byte header, 3 byte FCS checksum.
	uframe.setAppendP(0);
	// 6.2.2: This is a downlink response, so the C/R bit is 0.
	uframe.appendAddrHeader(lle->getLlcSapi(),false);
	uframe.appendUHeader(LlcDefs::UCMD_XID,true);	// Leaves append pointer at data.
	// And I quote: "As an optimisation, parameters confirming the requested
	// value smay be omitted from the XID response."
	// So you can just return the xid header.
	// However, the multitech modem did not accept that, but does succeed
	// if you send the Full XID response string.
	//if (gConfig.getNum("GPRS.XID.Full",1)) // There is no reason for this to be an option
	if (1) {
		for (int n = 0; n < totlen;) {
			bool xl = xids.getBit2(n,0);
			int xidtype = xids.getField2(n,1,5);
			int xidlen = xl ? xids.getField2(n,6,8) : xids.getField2(n,6,2);
			n += (xl ? 2 : 1);
			unsigned value = 0;
			if (xidlen <= 4) {
				value = xids.getField2(n,0,8*xidlen);
				uframe.appendXidItem(xidtype, xidlen,value);
				LLCWARN("LLC XID"<<LOGVAR(xidtype)<<LOGVAR(xidlen)<<LOGVAR(value));
			} else {
				// The only xid item with length > 4 is the L3 params, just hope we dont get those.
				LLCWARN("LLC ignoring over-length XID parameter:"
					<<LOGVAR(xidtype)<<LOGVAR(xidlen)
					<<LOGVAR2("bytes",xids.segment(n,xidlen).hexstr()));
			}
			n += xidlen;
		}
	}
	// Send it.
	LLCWARN("LLC Sending XID command:"<<uframe.hexstr());
	lle->lleWriteRaw(uframe,"xid cmd");
  } catch (ByteVectorError) {
	LLCWARN("over-run error parsing LLC XID command");
  }
}

void LlcFrameU::llcProcess(LlcEntity *lle)
{
	// Note: The U frame exists to send a command via the S and M fields,
	// which are defined in 04.64 6.4; See enum U_M_Commands 
	// Sec 8.5.4 says we should discard unrecognized if we are in TLL Assigned/ADM state.
	// Sec 8.8.4 has a table that says the same.
	// Sec 8.2 describes the P/F bit, says we should return a U command,
	// so I tried returning DM, but it did not make the multitech modem work.
	int cmd = getUM();
	if (cmd == UCMD_XID) {
		ByteVector xids = ByteVector(*this);
		xids.trimLeft(2);	// Chop off the U frame header.
		LLCWARN("LLC XID frame received"<<LOGVAR2("size",xids.size())<<LOGVAR2("llcsapi",lle->getLlcSapi()));
		handleXid(lle,xids);
	} else {
		const char *cmdname = "?";
		switch (cmd) {
		case UCMD_SABM: cmdname = "SABM"; break;
		case UCMD_XID: cmdname = "XID"; break;
		case UCMD_DM: cmdname = "DM"; break;
		case UCMD_DISC: cmdname = "DISC"; break;
		case UCMD_UA: cmdname = "UA"; break;
		case UCMD_FRMR: cmdname = "FRMR"; break;
		case UCMD_NULL: cmdname = "null"; break;
		}
		LLCWARN("LLC U frame ignored"<<LOGVAR(cmdname)<<LOGVAR2("PF",getUPF())<<LOGVAR2("M",getUM())<<LOGVAR2("llcsapi",lle->getLlcSapi()));
	}
}


// The checksum has already been chopped off.
void LlcFrameUI::llcProcess(LlcEntity *lle)
{
	// This is a data frame.
	LLCDEBUG("UI::llcProcess");
	ByteVector payload(tail(UIHeaderLength));
	lle->lleUplinkData(payload);
}

void LlcFrameUI::writeUIHeader(unsigned wNU /*, bool pf*/)
{
	bool wE = 0;	// no encryption.
	bool wPM = 1;	// Checksum FCS is over everything.
	setField2(controlOffset,0,0x18,5);	// UI format tag and unused bits.
	setField2(controlOffset,5,wNU,9);	// frame number.
	setField2(controlOffset+1,6,wE,1);
	setField2(controlOffset+1,7,wPM,1);
}

void LlcEngine::allocSndcp(SgsnInfo *si, unsigned nsapi, unsigned llcsapi)
{
	//LlcEntityUserData *userdatalle = si->mLlcEngine->getLlcEntityUserData(llcsapi);
	LlcEntityUserData *userdatalle = getLlcEntityUserData(llcsapi);
	new Sndcp(nsapi,llcsapi,userdatalle);
}

#if 0==SNDCP_IN_PDP
void LlcEngine::freeSndcp(unsigned nsapi)
{
	Sndcp *sndcp = mSndcp[nsapi];
	mSndcp[nsapi] = 0;
	if (sndcp) delete sndcp;

	// TODO: This is wrong - LlcEntity is by llc sapi, not nsapi.
	// So I am just commenting it out.
	// Must reset the LLC state machine also.
	//LlcEntity *lle = getLlcEntity(nsapi);
	//if (lle) {lle->reset();}	// Better not be 'if'
}
#endif

void LlcEngine::llcWriteHighSide(ByteVector &sdu,int nsapi)
{
#if SNDCP_IN_PDP
	PdpContext *pdp = mLleGmm.mSI->getPdp(nsapi);
	if (!pdp) {
		LLCWARN("llcWriteHighSide to unconfigured nsapi:"<<nsapi);	// cant happen?
		return;
	}
	Sndcp *sndcp = pdp->mSndcp1;
#else
	Sndcp *sndcp = mSndcp[nsapi];
#endif
	if (sndcp) {
		sndcp->sndcpWriteHighSide(sdu);
	} else {
		assert(0); // not possible because Sndcp and PdpContext allocated/deallocated together.
	}
}

void LlcEngine::llcWriteLowSide(ByteVector &bv,SgsnInfo *si)
{
	if (bv.size() < 2) { return; }
	LlcFrame lframe(bv);
	int llcsapi = lframe.getSapi();
	LLCDEBUG("llcWriteLowSide sapi="<<llcsapi);
	LlcEntity *lle = getLlcEntity(llcsapi);
	if (lle == 0) {
		LLCWARN("LLC received PDU with unexpected SAPI="<<llcsapi);
		// This is an "invalid frame" and shall be ignored without indication.
		return;
	}
	// Chop off the parity.
	// TODO: Check it.
	lframe.trimRight(3);
	lle->lleWriteLowSide(lframe);
}

//LlcEntity * SgsnInfo::getLlcEntity(unsigned llcSapi)
//{
//	return mllcEngine->getLlcEntity(llcSapi);
//}

LlcEntityUserData * LlcEngine::getLlcEntityUserData(unsigned llcSapi)
{
	return dynamic_cast<LlcEntityUserData*>(getLlcEntity(llcSapi));
}

LlcEntityGmm *LlcEngine::getLlcGmm()
{
	return dynamic_cast<LlcEntityGmm*>(getLlcEntity(LlcSapi::GPRSMM));
}

void LlcEntity::lleWriteLowSide(LlcFrame &frame)
{
	mVUR++;
	frame.llcProcess1(this);
}


void LlcEntity::lleWriteRaw(ByteVector &frame, const char *descr)
{
	gLlcParity.appendFCS(frame);
	mSI->sgsnSend2MsHighSide(frame,descr,0);
	//GPRS::DownlinkQPdu *dlpdu = new GPRS::DownlinkQPdu();
	//dlpdu->mDlData = uiframe;
	//LLCDEBUG("llewriteHighSide:"<<(ByteVector)frame);
	//dlpdu->mDescr = std::string(descr);
	//mSI->getMS()->msDownlinkQueue.write(dlpdu);
}

// Write a UI frame for unacknowledged information.
void LlcEntity::lleWriteHighSide(LlcDlFrame &frame, bool isCmd, const char *descr)
{
	// Prepend the LLC header; the bv already has room allocated.
	frame.growLeft(LlcFrame::UIHeaderLength);
	frame.writeAddrHeader(getLlcSapi(),isCmd);
	//LlcFrameUI uiframe(frame.begin());
	LlcFrameUI uiframe(frame);
	uiframe.writeUIHeader(mVU++);
	lleWriteRaw(frame,descr);

}

void LlcEntityGmm::lleUplinkData(ByteVector &payload)
{
	LLCDEBUG("LlcEntityGmm lleUplinkData");
	// This is an l3 message.
	handleL3Msg(mSI,payload);
}

// Warning: The MS can send uplink data before attaching or creating pdpcontext,
// for example, if the bts is rebooted.
void LlcEntityUserData::lleUplinkData(ByteVector &payload)
{
	// okey dokey, this goes to the sndcp.
	SndcpFrame sframe(payload);
	unsigned nsapi = sframe.getNSapi();
	// The NSAPI is pre-configured by an L3 PDP Context Activation message.
	// If it does not exist, the MS and BTS are out of sync, possible after a crash,
	// or invalid RA-Update.
	Sndcp *sndcp = getSndcp(nsapi);
	if (sndcp == 0) {
		if (! mSI->isRegistered()) {
			// The MS has not done an Attach.
			// This happens if the BTS comes on and the MS was previously talking to us.
			// 24.008 Annex G:  We can send "ImplicitlyDeattached" and I quote:
			// "This cause is sent ..., or if the GMM context data related
			// to the subscription dose (sic) not exist in the SGSN e.g.
			// because of a SGSN restart."
			// Update: This does not appear to do the job on the Blackberry.
			LLCINFO("received packet to detached MS on nsapi="<<nsapi<<" Sending Implicitly_Detached message "<<mSI);
			sendImplicitlyDetached(mSI);
		} else {
			// This is a serious problem.
			// We cant send a PdpDeactivateRequest message because the stupid thing
			// is by TI [Transaction Identifier] instead of NSAPI, and we dont have one.
			// Not sure what to do.
			//Tried this anyway, did nothing:
			//		sendPdpDeactivate(getSgsnInfo(),nsapi,SmCause::Unknown_PDP_context);
			LLCWARN("received packet to unconfigured nsapi="<<nsapi<<" "<<mSI);
		}

		return;	// Thats the end of that.
	}
	sndcp->sndcpWriteLowSide(sframe);
}

//Sndcp *LlcEntityUserData::getSndcp(unsigned nsapi) { return getSgsnInfo()->mSndcp[nsapi]; }
//void LlcEntityUserData::setSndcp(unsigned nsapi, Sndcp*ptr) { getSgsnInfo()->mSndcp[nsapi] = ptr; }
#if SNDCP_IN_PDP
Sndcp *LlcEntityUserData::getSndcp(unsigned nsapi)
{
	// The pdp will be NULL if the MS sends uplink data before allocating a PdpContext.
	PdpContext *pdp = mSI->getPdp(nsapi);
	return pdp ? pdp->mSndcp1 : NULL;
}
void LlcEntityUserData::setSndcp(unsigned nsapi, Sndcp*ptr) { mSI->getPdp(nsapi)->mSndcp1 = ptr; }
#else
Sndcp *LlcEntityUserData::getSndcp(unsigned nsapi) { return mSI->mLlcEngine->mSndcp[nsapi]; }
void LlcEntityUserData::setSndcp(unsigned nsapi, Sndcp*ptr) { mSI->mLlcEngine->mSndcp[nsapi] = ptr; }
#endif

// We dont know the length of S SACK format, so return the minimum length.
int LlcFrameDump::headerLength()
{
	switch (mFormat) {
		case LLCFormat::I: return 4;
		case LLCFormat::S: return 3;
		case LLCFormat::UI: return 3;
		case LLCFormat::U: return 2;
		case LLCFormat::ISack: return 5 + 1 + (mK+1+7)/8;
		case LLCFormat::SSack: return 3;		// Minimum length is probably 4, not 3.
		case LLCFormat::Invalid: return -1;
		default: assert(0);
	}
}

void LlcFrameDump::llcParseDump()
{

	mK = 0;  // headerLength uses mK, so set to 0 for first test here.

	switch (mFormat) {
		case LLCFormat::U:
			mPF = getBitR1(controlOffset,5);
			mM = getByte(controlOffset) & 0xf;
			break;
		case LLCFormat::UI:
			mNU = getFieldR1(controlOffset,3,9);
			mE = getBitR1(controlOffset+1,2);
			mPM = getBitR1(controlOffset+1,1);
			break;
		case LLCFormat::ISack:
			mK = getByte(controlOffset+3) & 0x1f;
			// Check again, now that we know the real mK
			if ((int)size() < headerLength()) { mFormat = LLCFormat::Invalid; return; }
			// Fall through
		case LLCFormat::I:
			mA = getBitR1(controlOffset,7);
			mNS = getFieldR1(controlOffset,5,9);
			mNR = getFieldR1(controlOffset+1,3,9);
			mS = getByte(controlOffset+2) & 0x3;
			break;
		case LLCFormat::SSack:
		case LLCFormat::S:
			mA = getBitR1(controlOffset,6);
			mNR = getFieldR1(controlOffset,3,9);
			break;
		default: assert(0);
	}
}

void LlcFrameDump::textHeader(std::ostream &os)
{
	os << "format=" <<LLCFormat::name(mFormat)
		<<LOGVAR2("SAPI",getSapi()) <<LOGVAR2("LlcPD",getLlcPD()) <<LOGVAR2("CR",getCR());
	switch (mFormat) {
		case LLCFormat::I: case LLCFormat::ISack:
			os <<LOGVAR(mNS) <<LOGVAR(mNR) << LOGVAR(mS);
			break;
		case LLCFormat::S: case LLCFormat::SSack:
			os <<LOGVAR(mNR) <<LOGVAR(mA) << LOGVAR(mS);
			break;
		case LLCFormat::UI:
			os <<LOGVAR(mNU) <<LOGVAR(mE) << LOGVAR(mPM);
			break;
		case LLCFormat::U:
			os <<LOGVAR(mPF) <<LOGVAR(mM);
			break;
		default: break;
	}
}

void LlcFrameDump::textContent(std::ostream &os,bool verbose)
{
	if (mFormat == LLCFormat::S || mFormat == LLCFormat::SSack) {
		return;	// There is no data field.
	}
	// What the data is depends on the SAPI; could be an L3Message or user data.
	int pos = headerLength();
	switch (getSapi()) {
		case LlcSapi::GPRSMM: {
			// The contents are an L3 Message, prefixed by an LLC header
			// and followed by the FCS checksum.
			//ByteVector l3msg = segment(pos,size()-pos-3);
			ByteVector payload = segment(pos,size()-pos-3);
			os << " L3="<<L3GprsMsgType2Name(payload);
			if (verbose) {
				L3GprsFrame frame(payload);
				frame.dump(os);
			}
			return;
		}
		case LlcSapi::UserData3:
		case LlcSapi::UserData5:
		case LlcSapi::UserData9:
		case LlcSapi::UserData11:
			// The contents are some user data.
			os << " user PDU size=" << ((int)size() - pos);
			break;
		//SMS = 7
		//TOM2 = 2,
		//TOM8 = 8,
		default:
			os << "unrecognized SAPI="<<getSapi();
			break;
	}
}
void LlcFrameDump::text(std::ostream &os)
{
	os << "LlcFrame:(";
	textHeader(os);
	textContent(os,true);
	os << ")";
}

//bool Sndcp::isPdpInactive() { return mPdp==0 || mPdp->isPdpInactive(); }
unsigned Sndcp::getMaxPduSize() { return mlle->getMaxPduSize(); }
SgsnInfo *Sndcp::getSgsnInfo() { return mlle->mSI; }

// If we have all the segments for pdu num, send it off.
// If force, delete it even if incomplete.
void Sndcp::flush(unsigned num, bool force)
{
	OneSdu *sp = &mSegs[num%sMemory];
	unsigned i;
	if (sp->mSegCount) {	// We have received the final segment.
		unsigned totsize = 0;
		// Do we have all the segments yet?
		for (i = 0; i < sp->mSegCount; i++) {
			unsigned size = sp->segs[i].size();
			if (size == 0) { break; }	// failure.
			totsize += size;
		}
		if (i == sp->mSegCount) {	// success.
			SNDCPDEBUG("flush"<<LOGVAR(num)<<LOGVAR(sp->mSegCount));
			ByteVector result(totsize);
			result.setAppendP(0);
			for (i = 0; i < sp->mSegCount; i++) {
				result.append(sp->segs[i]);
				sp->segs[i].clear();
			}
			sp->mSegCount = 0;
			//mPdp->pdpWriteLowSide(result);
			getSgsnInfo()->sgsnSend2PdpLowSide(mNSapi,result);
			//PdpContext *pdp = mlle->getSgsnInfo()->getPdp(mNSapi);
			//assert(pdp);
			//pdp->pdpWriteLowSide(result);
			return;
		}
		SNDCPDEBUG("flush still pending"<<LOGVAR(num)<<LOGVAR(sp->mSegCount));
	} else {
		// Anything there at all?  This is just for a message.
		for (i = 0; i < 16; i++) {
			if (sp->segs[i].size()) {
				SNDCPDEBUG("flush still pending"<<LOGVAR(num)<<LOGVAR2("segment",i));
				break;
			}
		}
	}

	if (force) {
		// Delete all segments.
		for (i = 0; i < 16; i++) {
			sp->segs[i].clear();
		}
		sp->mSegCount = 0;
	}
}

int Sndcp::diffSNS(int v1, int v2)
{
	int diff = v1 - v2;
	if (diff < (int)mSNS/2) diff += mSNS;
	if (diff > (int)mSNS/2) diff -= mSNS;
	return diff;
}

// uplink data from MS comes in here.
void Sndcp::sndcpWriteLowSide(SndcpFrame &frame)
{
	// Todo: segment it.
	unsigned segnum = frame.getSegmentNumber();
	unsigned pdunum = frame.getPduNumber();
	ByteVector payload(frame.getPayload());
	SNDCPDEBUG("uplink packet"<<LOGVAR(pdunum)<<LOGVAR(segnum)<<LOGVAR2("size",payload.size())
		<<" header="<<frame.head(MIN(20,frame.size())));

	int diff = diffSNS(pdunum,mRecvNPdu);
	if (diff >= 0) {	// Is pdunum greater than or eql mRecvNPdu?
		// If pdunum is totally off, dont move it?
		if (diff < 16)
		while (mRecvNPdu != pdunum) {	// Advance mRecvNPdu
			// flush does a % mSNS, and these are unsigned, so we can subtract without
			// fear of the negative number botching it up.
			flush((mRecvNPdu - sMemory)%mSNS,true);
			mRecvNPdu = (mRecvNPdu + 1) % mSNS;
		}
	} else if (-diff >= (int)sMemory) {
		// Too old to be in our window.
		LLCWARN("SNDCP packet too old, discarded (number="<<pdunum<<",current="<<mRecvNPdu<<")");
		return;	// discard incoming frame.
	}

	// Save the pdu.
	if (frame.getF()) {	// first segment; flag marks that PCOMP/DCOMP byte is present.
		if (segnum != 0) {
			LOG(ERR) <<"invalid Sndcp pdu with F and seg number != 0";
			segnum = 0;	// Lets pretend.
		}
	}
	mSegs[pdunum%sMemory].segs[segnum] = payload;
	if (!frame.getM()) {
		mSegs[pdunum%sMemory].mSegCount = segnum+1;
		flush(pdunum,false);
	}
}

// Send the pdu segment on its way.
// TODO: we are assuming unacknowledged mode.
void Sndcp::sndcpWriteSegment(ByteVector &pduSeg, unsigned segnum, unsigned flags)
{
	LlcDlFrame result(pduSeg.size()+4);	// May be overkill by one or more bytes.
	result.appendByte(flags);
	if (flags & F_BIT) {
		// 6.7.1.1: First segment has DCOMP and PCOMP parameters.
		// Amusingly, only make the pdu bigger.
		result.appendByte(0);	// No compression.
	}
	result.appendField(segnum,4);	// segment number.
	result.appendField(mSendNPdu % mSNS,12);	// pdu number.
	result.append(pduSeg);
	// TODO: Is this a command or a response?
	mlle->lleWriteHighSide(result,true,"user pdu");
}

// downlink data from internet comes in here.
// It needs to be segmented and sent to LLC Entity for yet another header.
void Sndcp::sndcpWriteHighSide(ByteVector &sdu)
{
	// Set the first byte flags.
	unsigned flags = mNSapi;
	flags |= T_BIT;	// UNITDATA PDU
	flags |= F_BIT;	// First segment.
	// Segment the pdu.
	unsigned segnum = 0;
	unsigned segsize = getMaxPduSize();
	segsize -= 12;	// be safe.  If you dont do this, the blackberry rejects the packets.
	for (; sdu.size() > segsize; segnum++) {
		flags |= M_BIT;	// Not last segment.
		ByteVector seg(sdu.segment(0,segsize));
		sndcpWriteSegment(seg,segnum,flags);
		sdu.trimLeft(segsize);
		flags &= ~F_BIT;	// Not first segment.
	}
	flags &= ~M_BIT;	// Now it is the last segment.
	sndcpWriteSegment(sdu,segnum,flags);
	mSendNPdu = (mSendNPdu+1) % mSNS;
}

// invert the low width bits of x.
static uint32_t revbits(uint32_t x, unsigned width)
{
	x &= ((uint32_t)1<<width)-1;
	uint32_t result = 0;
	for (unsigned i = 0; i < width; i++) {
		result = result << 1;
		result |= (x&1);
		x = x >> 1;
	}
	return result;
}

// Pre-compute the CRC divisors for each possible byte.
static void  genParityTab(uint32_t invGen, uint32_t *tab)
{
	for (int i = 0; i < 256; i++) {
		uint32_t crc = i;

		for (int b = 7; b >= 0; b--) {
			unsigned bit = crc & 1;
			crc >>= 1;
			if (bit) { crc ^= invGen; }
		}
		tab[i] = crc;
	}
}

Parity32::Parity32(uint32_t generator, unsigned width, bool invertFirst)
{
	mMask = (((uint32_t)1<<width) - 1);
	mInitialRemainder = invertFirst ? mMask : 0;
	// If it is a 32-bit generator, dont bother passing in bit 33,
	// which gets shifted off the top of the 32-bit generator argument.
	if (width == 32) {
		// untested:  The 33rd bit is off the top, so put it back.
		// Note that this would work for both cases, but clearer to separate it.
		mInvertedGenerator = (revbits(generator,32) >> 1) | (1<<31);
	} else {
		mInvertedGenerator = revbits(mMask & generator,24);
	}
	genParityTab(mInvertedGenerator,mTab);
}

uint32_t Parity32::computeCrc(unsigned char *str, int len)
{
	uint32_t crc=mInitialRemainder;
	unsigned char *bp = str, *ep = str + len;
	while (bp < ep) {
		crc = (crc >> 8) ^ mTab[(crc ^ *bp++) & 0xff];
	}
	return (~crc) & mMask;

	// As a comment, this is the identical algorithm to the above, without the table lookup:
	/***
	for (int l = 0; l < len; l++) {
		crc = crc ^ str[l];
		for (int b = 7; b >= 0; b--)
		{
			unsigned bit = crc & 1;
			crc >>= 1;
			if (bit) { crc ^= lsbgen; }
		}
	}
	***/
}

uint32_t Parity32::computeCrc(ByteVector &bv)
{
	return computeCrc(bv.begin(),bv.size());
}

extern "C" { int gprs_llc_fcs(uint8_t *data, unsigned int len); };

void LlcParity::appendFCS(ByteVector &bv)
{
	uint32_t fcs = computeCrc(bv);
	// append 24-bit fcs LSB first.
	bv.appendByte(fcs&0xff);
	bv.appendByte((fcs>>8)&0xff);
	bv.appendByte((fcs>>16)&0xff);

	// Double check:
#if 0
	uint32_t oldcrc = gprs_llc_fcs(bv.begin(),bv.size()-3);
	if (fcs != oldcrc) {
		printf("CRC ERROR: old=%d new=%d\n",oldcrc,fcs);
	} else {
		printf("CRC matches\n");
	}
#endif
}

// Check the FCS in the last 3 bytes of bytevector.
bool LlcParity::checkFCS(ByteVector &bv)
{
	unsigned len = bv.size();
	uint32_t fcs = (bv.getByte(len-1)<<16) | (bv.getByte(len-2)<<8) | bv.getByte(len-3);
	uint32_t computedFCS = computeCrc(bv.begin(),len-3);
	return fcs == computedFCS;
}

LlcParity gLlcParity;	// The one and only parity generator needed.

};	// namespace
