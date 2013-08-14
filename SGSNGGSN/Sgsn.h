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

#ifndef _SGSN_H_
#define _SGSN_H_
#include "GPRSL3Messages.h"
#include "SgsnExport.h"
#include "GSMCommon.h"	// For Z100Timer
#ifndef MIN
#define MIN(a,b) ((a)<=(b)?(a):(b))
#endif
#ifndef RN_FOR_ALL
#define RN_FOR_ALL(type,list,var) \
	for (type::iterator itr = (list).begin(); \
		itr == (list).end() ? 0 : ((var=*itr++),1);)
#define RN_FOR_ALL_WITH_ITR(type,list,var,itr) \
	type::iterator itr; \
	for (type::iterator itr1 = (list).begin(),itr=itr1; \
		itr1 == (list).end() ? 0 : ((itr=itr1),(var=*itr1++),1);)
#endif

#define NEW_TLLI_ASSIGN_PROCEDURE 0		// NO, dont use this.

namespace SGSN {

extern bool enableMultislot();
extern void sendImplicitlyDetached(SgsnInfo *si);

// 10.5.5.2 AttachType can take all 3 values.
// 10.5.5.1 Attach Result can only be GPRS or Combined, not value 2.
enum AttachType {
	AttachTypeGprs = 1, AttachTypeGprsWhileImsiAttached = 2, AttachTypeCombined = 3
};
class SgsnInfo;

const static uint32_t sLocalTlliMask = 0xc0000000;

struct AttachInfo {
	AttachType mAttachReqType;
	uint32_t mAttachReqPTmsi;	// Saved if specified in the attach request.
	// MsRadioAcces caps saved from the AttachRequest to go in GmmInfo when it is created.
	ByteVector mMsRadioAccessCap;
	GMMRoutingAreaIdIE mPrevRaId;
	void stashMsgInfo(GMMAttach &msgIEs, bool isAttach);
	void copyFrom(AttachInfo &other);

	AttachInfo() :
		// wont be used until initialized, but init to invalid value for copyFrom.
		mAttachReqType((AttachType)0),
		mAttachReqPTmsi(0)
	{}
};

// This is the state for a single MS identified by IMSI.
// No IMSI, no GmmInfo; this struct is not allocated until the MS coughs up an IMSI.
// TODO: We may, however, eventually keep a list of known TMSI and/or IMSI in
// an sql data-base somewhere so the phone can subsequently attach without
// having to send us an IMSI, however, this will still be per-IMSI.
//
class GmmInfo
{
	public:
	ByteVector mImsi;	// The IMSI.
	uint32_t mPTmsi;	// The unique P-TMSI we create for this Gmm context.
	uint32_t getPTmsi() const { return mPTmsi; }
	uint32_t getTlli() const { return mPTmsi | sLocalTlliMask; }
	static const unsigned sNumPdps = 16;
	time_t mAttachTime;
	time_t mActivityTime;
	int mGprsMultislotClass;		// -1 means invalid.
	Bool_z mGprsGeranFeaturePackI;
	AttachInfo mgAttachInfo;		// Copied from SgsnInfo.
	void setActivity() { time(&mActivityTime); }
	void setAttachTime() { time(&mAttachTime); mActivityTime = mAttachTime; }

	private:
	// mState starts in GmmDeregistered.
	GmmState::state mState;

	// PDPContext are allocated on request, via ActivatePdpContext message.
	// In GPRS, each PdpContext here corresponds to a Sndcp in the mSndcp[].
	PdpContext *mPdps[sNumPdps];	// Only 5..15 are used, but we allocate the whole array and index into
							// it directly with the RB [Radio Bearer] index, aka NSAPI.

	public:

	// This points to the SgsnInfo we will use for downlink messages after registration.
	// It is not used for the responses during the attach process.
	// If msi does not already exist, it is allocated at the same time as GmmInfo,
	// however, in that case where it did not pre-exist, it means we have not yet
	// heard from the MS using the PTMSI we are going to assign to it, which means
	// that there may be no corresponding MSInfo structure yet for this SgsnInfo,
	// which case is handled by the TLLI change procecure in L2.
	SgsnInfo *msi;
	SgsnInfo *getSI() { return msi; }

	bool isRegistered() {
		return mState == GmmState::GmmRegisteredNormal ||
		       mState == GmmState::GmmRegisteredSuspsended;
	}

	void setGmmState(GmmState::state newstate) { mState = newstate; }
	GmmState::state getGmmState() { return mState; }

	bool isNSapiActive(unsigned nsapi); 	// True if the pdpcontext is not in state PDP-INACTIVE
	// 3GPP 24.008 10.5.7.1: PDP Context Status.
	// It is a bit-map of the allocated PdpContext for this ms.
	// The result bytes for this IE are not in network order, so return it as two bytes.
	PdpContextStatus getPdpContextStatus();
	void connectPdp(PdpContext *pdp, mg_con_t *mgp);
	bool freePdp(unsigned nsapi);	// Free both the PdpContext and the Sndcp.
	unsigned freePdpAll(bool freeRabsToo);
	PdpContext *getPdp(unsigned nsapi);

	GmmInfo(ByteVector &imsi);
	~GmmInfo();
};


// The data path through SGSN is different for GPRS and UMTS.
// For GPRS it includes LLC, LLE, and SNDCP.
// For UMTS it includes PDCP, which is a no-op, so it is nearly empty.
// Uplink Data Path:
//		GPRS and UMTS have significantly different uplink paradigms in that:
//		GPRS sends all packets to a single entry point in LLC, whence packets are
//			directed based on information in the packet.
//			But there is a wrinkle in that during the attach process there are multiple active TLLIs
//			(the old one the ms uses to contact us, and the new one we are assigning) and they
//			each have their own LLC state machine, but both must map to the same MS state info.
//			TODO: The above is incorrect - there should be only one set of LLC.
//		UMTS segregates data channels into RBs, so there is one entry point at the low end of the
//			sgsn for each tuple of UE-identifier + RB-id, where the UE-identifier uniquely
//			corresponds to an SgsnInfo, and RB-id is 3 is for L3 messages and 5..15 are for user data.
// 		The GPRS MS sends stuff to:
//			sgsnWriteLowSide(ByteVector &payload,SgsnInfo *si);
//		The UMTS UE sends stuff to:
//			sgsnWriteLowSide(ByteVector &payload,RB????);
// 		At the high end of the uplink path, the PdpContext sends packets to:
//			void SgsnInfo::sgsnSend2PdpLowSide(int nsapi, ByteVector &packet)
//			which calls:
//			PdpContext *pdp->pdpWriteLowSide();
// Downlink Data Path:
// 		The pdp context sends stuff to:
//			SgsnInfo *si->sgsnWriteHighSide(ByteVector &pdu,int mNSapi);
// 		At the low end of the downlink path, stuff goes to:
//			void sgsnSend2MsHighSide(ByteVector &pdu,const char *descr, int rbid);
//			which calls:
//			SgsnInfo *si->getMS()->msDownlinkQueue.write(GPRS::DownlinkQPdu *dlpdu);

// There is one SgsnInfo for each TLLI.
// This does not map one-to-one to MSInfo structs because MSInfo change TLLI when registered.
// During the attach process an MS may call in with several different TLLIs 
// and/or PTMSIs, which will create a bunch of MSInfo structs in GPRS and corresponding
// SgsnInfos here.  We dont know what MS these really are until we get an IMSI,
// or todo: see a known TMSI/PTSMI+RAI pair.
// Upon completion of a successful attach, sgsn assigns (or looks up) a new PTMSI
// for the MS, which it will use to talk back to us with yet another new TLLI based on
// the PTMSI that it sent us.
// The Session Management State is in the GmmInfo and is per-MS, not per-TLLI.
// There is a problem that during the attach procedure, uplink and downlink
// messages may be occuring simultaneously with different TLLIs,
// even though they are just one MS and we want to keep gprs straight about
// that so it does not try to start multiple TBFs for the same MS.
// This is how we resolve all that:
// The per-tlli info is in two structs: MSInfo in gprs and SgsnInfo here.
// The MSInfo has a tlli, an oldTlli, and an altTlli, see documentation at the class.
// In the sgsn, there is one SgsnInfo for each tlli.
// (It could have been organized like MSInfo, but is not just for historical reasons.)
// In the sgsn, before we have identified the MS we keep all the information
// in the SgsnInfo, which includes everything we need to remember from the
// RAUpdate or AttachRequest messages.
// After a successful AttachComplete, we will subsequently use only
// one TLLI, and therefore one SgsnInfo, for downlink messages,
// although we still accept uplink messages using the old TLLI,
// and the MS may continue to send messages using
// those old TLLIs well into the PdpContext activation procedures.
// So this process results in one SgsnInfo that is associated with the TLLI that
// finally successfully attached, one SgsnInfo for the TLLI the MS used to initiate
// the attach, and we also may end up with a bunch of ephemeral SgsnInfo
// associated with TLLIs that the MS tried but did not finish attaching.

// Note that the ephemeral SgsnInfo must send send an L3 attach message
// (to assign the local TLLI) using the immensely over-bloated LLC machinery
// to get the MS attached to a semi-permanent SgsnInfo.
// I was really tempted just to hard-code the single LLC message that is required,
// but I didn't; we allocate a whole new LLC and send the L3 message through it.
// I think after the attach process we could just get rid of the SgsnInfo, and let them
// be created anew if the MS wants to talk to us again.
// ==========
// The LLC state machine is DEFINED as being per-TLLI.
// From 3GPP 04.64 [LLC] 4.5.1: "A logical link connection is identified by a DLCI
// consisting of two identifiers: a SAPI and a TLLI."
// However, during the Attach process we change the TLLI using the procedures
// defined in 3GPP 04.64 8.3.
// The GmmInfo holds the Session Management info, and is associated with one
// and only one MS identified by IMSI.
// There are two major types of SgsnInfo:
class SgsnInfo
{
	friend class MSUEAdapter;

	// The LlcEngine is used only by GPRS.
	// UMTS uses PDCP, but it is a complete no-op, so is not represented here at all.
	// The LLC sits between GPRS and SGSN and could have been combined with GPRS instead
	// of being in the SGSN at all.   Here, it is encapsulated entirely within this object.
	// I left the LLC component in the SGSN for several reasons:
	// in case we implement handover, the LLC state must be passed too;
	// and just because that is the way things are partitioned in commercial systems.
	private:
	GmmInfo *mGmmp;
	public:
	GmmInfo *getGmm() const { return mGmmp; }
	void setGmm(GmmInfo *gmm);

	LlcEngine *mLlcEngine;
	time_t mLastUseTime;

	//GmmMobileIdentityIE mAttachMobileId;

	// For the local SGSN, this is the P-TMSI/TLLI that we [are attempting to]
	// assign to this MS.  In the old separated SGSN system, the SGSN would remember both old
	// and new [being assigned] TLLI, and would try to change the TLLI in the BTS simultaneously,
	// keeping everything in sync, but we dont do that any more.  We let the GPRS directory create
	// a separate MSInfo structure for each TLLI, each of which maps to an independent SgsnInfo struct
	// for that TLLI, because each needs a unique LLC state machine.
	//
	// In L3 messages, we send P-TMSI to ms and it is supposed to convert that to
	// a "TLLI" of some sort when talking back to us by twiddling the top bits
	// depending on the Routing Area.  In the same Routing Area, it sets the top 2 bits.
	// Which is silly for us, so we just set the top 2 bits to start with and use
	// the same 32-bit number for TLLI and P-TMSI.
	uint32_t mMsHandle;	// The single TLLI or URNTI associated with this SgsnInfo.
					// If it is an assigned SgsnInfo, this is equal to the P-TMSI we allocated for the MS.

	// I tried to implement the TLLI Assign Procedure by adding an AltTlli to this
	// SgsnInfo struct, but it does not work well, because after an attach, if
	// the MS tries to attach again (happens if it is powercycled or confused)
	// then we really need the two SgsnInfo separate again so we can communicate
	// with the MS distinctly with the old TLLI for the attach accept message.
	// Also it was easy to end up with two SgsnInfo with the same TLLI in that case,
	// so its easier to just keep them distinct.
	// So an SgsnInfo is synonymous with a single TLLI.
#if NEW_TLLI_ASSIGN_PROCEDURE
	uint32_t mAltTlli;	// For GPRS, this is the alternate TLLI before a TLLI assignment
						// procedure that occurs at AttachComplete.  This SgsnInfo must
						// respond to both new and alt tllis, but only the tlli
						// in mMsHandle is used for downlink communication.
#endif
	SgsnInfo *changeTlli(bool now);
	//uint32_t getTmsi() { return mMsHandle; }	// NOT RIGHT!
	//uint32_t getPTmsi() { return mMsHandle; }	// NOT RIGHT!

	ByteVector mRAND;
	// The information in the L3 AttachRequest is rightly part of the GmmInfo context,
	// but when we receive the message, that does not exist yet, so we save the
	// AttachRequest info in the SgsnInfo here.
	// This is the type of attach the MS requested.  It is saved from the AttachRequest so it can
	// be used to send the correct attach accept message after the MS identity request handshake(s).
	AttachInfo mtAttachInfo;

	AttachType attachResult() {
		if (mtAttachInfo.mAttachReqType == AttachTypeGprsWhileImsiAttached) {
			return AttachTypeCombined;
		}
		return mtAttachInfo.mAttachReqType;
	}
	void deactivateRabs(unsigned nsapiMask);


	// Pass throughs to GmmInfo.  All must be protected from mGmmp == NULL.
	bool isRegistered() const { return mGmmp && mGmmp->isRegistered(); }
	PdpContextStatus getPdpContextStatus() { return mGmmp ? mGmmp->getPdpContextStatus() : PdpContextStatus(); }
	bool freePdp(unsigned nsapi) { return mGmmp ? mGmmp->freePdp(nsapi) : false; }
	unsigned freePdpAll(bool freeRabsToo) { return mGmmp ? mGmmp->freePdpAll(freeRabsToo) : 0; }
	PdpContext *getPdp(unsigned nsapi) { return mGmmp ? mGmmp->getPdp(nsapi) : 0; }

	// After packet has been processed by LLC or PDCP, this sends it to the correct PdpContext.
	void sgsnSend2PdpLowSide(int nsapi, ByteVector &packet);
	void sgsnSend2MsHighSide(ByteVector &pdu,const char *descr, int rbid);

	void sgsnWriteHighSide(ByteVector &sdu,int nsapi);

	// 24.008 11.2.2.  T3310 is in the MS and is 15s.  We are using it here similarly to place a limit
	// on the Attach Request process, so when we receive an Identity Response we dont send
	// an attach long afterward.
	GSM::Z100Timer mT3310FinishAttach;
	// T3370 is the ImsiRequest repeat at 6s, but the total time is only 15s, so its hardly worth bothering,
	// so we dont; the MS will RACH again if necessary.  But here is the timer anyway.
	GSM::Z100Timer mT3370ImsiRequest;

	SgsnInfo(uint32_t wTlli);	// May be a URNTI instead of TLLI.
	~SgsnInfo();
	void sirm();	// Remove si from list and delete it.

	MSUEAdapter *getMS() const;

	void sgsnReset();

	// Downlink L3 Messages come here.
	void sgsnWriteHighSideMsg(L3GprsDlMsg &msg);

	//GMMAttach *atp;	// All the info from the attach or ra-update.
	//GPRS::MSInfo *mMs;		// The MSInfo associated with this SgsnInfo.
	friend std::ostream& operator<<(std::ostream& os, const SgsnInfo*si);
};
std::ostream& operator<<(std::ostream& os, const SgsnInfo*si);

void handleL3Msg(SgsnInfo *si, ByteVector &payload);
void gmmDump(std::ostream&os);
void sgsnInfoDump(SgsnInfo *si,std::ostream&os);
void gmmInfoDump(GmmInfo *si,std::ostream&os,int options);
SgsnInfo *findSgsnInfoByHandle(uint32_t handle,bool create);
GmmInfo *findGmmByImsi(ByteVector&imsi,SgsnInfo *si);
bool cliSgsnInfoDelete(SgsnInfo *si);
void cliGmmDelete(GmmInfo *gmm);


};	// namespace
#endif
