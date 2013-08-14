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

#include <list>
#include <SIPInterface.h>
#include <SIPUtility.h>
#include <SIPMessage.h>
#include <SIPEngine.h>
#include <SubscriberRegistry.h>
//#include "RList.h"
#include "LLC.h"
//#include "MSInfo.h"
#include "GPRSL3Messages.h"
#include "Ggsn.h"
#include "Sgsn.h"
#include "Utils.h"
#include "Globals.h"
//#include "MAC.h"
#include "miniggsn.h"
using namespace Utils;
#define CASENAME(x) case x: return #x;
#define SRB3 3

using namespace SIP;

namespace SGSN {
typedef std::list<SgsnInfo*> SgsnInfoList_t;
static SgsnInfoList_t sSgsnInfoList;
typedef std::list<GmmInfo*> GmmInfoList_t;
static GmmInfoList_t sGmmInfoList;
static Mutex sSgsnListMutex;	// One lock sufficient for all lists maintained by SGSN.
static void dumpGmmInfo();
#if RN_UMTS
static void sendAuthenticationRequest(SgsnInfo *si, string IMSI);
#endif

//static void killOtherTlli(SgsnInfo *si,uint32_t newTlli);
static SgsnInfo *sgsnGetSgsnInfoByHandle(uint32_t mshandle, bool create);
static int getNMO();

bool sgsnDebug()
{
	return gConfig.getBool("SGSN.Debug") || gConfig.getBool("GPRS.Debug");
}

bool enableMultislot()
{
	return gConfig.getNum("GPRS.Multislot.Max.Downlink") > 1 ||
		gConfig.getNum("GPRS.Multislot.Max.Uplink") > 1;
}

const char *GmmCause::name(unsigned mt, bool ornull)
{
	switch (mt) {
		CASENAME(IMSI_unknown_in_HLR)
		CASENAME(Illegal_MS)
		CASENAME(IMEI_not_accepted)
		CASENAME(Illegal_ME)
		CASENAME(GPRS_services_not_allowed)
		CASENAME(GPRS_services_and_non_GPRS_services_not_allowed)
		CASENAME(MS_identity_cannot_be_derived_by_the_network)
		CASENAME(Implicitly_detached)
		CASENAME(PLMN_not_allowed)
		CASENAME(Location_Area_not_allowed)
		CASENAME(Roaming_not_allowed_in_this_location_area)
		CASENAME(GPRS_services_not_allowed_in_this_PLMN)
		CASENAME(No_Suitable_Cells_In_Location_Area)
		CASENAME(MSC_temporarily_not_reachable)
		CASENAME(Network_failure)
		CASENAME(MAC_failure)
		CASENAME(Synch_failure)
		CASENAME(Congestion)
		CASENAME(GSM_authentication_unacceptable)
		CASENAME(Not_authorized_for_this_CSG)
		CASENAME(No_PDP_context_activated)
		// 0x30 to 0x3f - retry upon entry into a new cell?
		CASENAME(Semantically_incorrect_message)
		CASENAME(Invalid_mandatory_information)
		CASENAME(Message_type_nonexistent_or_not_implemented)
		CASENAME(Message_type_not_compatible_with_the_protocol_state)
		CASENAME(Information_element_nonexistent_or_not_implemented)
		CASENAME(Conditional_IE_error)
		CASENAME(Message_not_compatible_with_the_protocol_state)
		CASENAME(Protocol_error_unspecified)
		default:
			return ornull ? 0 : "unrecognized GmmCause type";
	}
}

SgsnInfo::SgsnInfo(uint32_t wMsHandle) :
	//mState(GmmState::GmmNotOurTlli),
	mGmmp(0),
	mLlcEngine(0),
	mMsHandle(wMsHandle),
	mT3310FinishAttach(15000),	// 15 seconds
	mT3370ImsiRequest(6000)		// 6 seconds
	// mSuspended(0),
{
	//memset(mOldMcc,0,sizeof(mOldMcc));
	//memset(mOldMnc,0,sizeof(mOldMnc));
	time(&mLastUseTime);
#if RN_UMTS == 0
	mLlcEngine = new LlcEngine(this);
#endif
	sSgsnInfoList.push_back(this);
}

SgsnInfo::~SgsnInfo()
{
	if (mLlcEngine) {delete mLlcEngine;}
}

void SgsnInfo::sirm()
{
	std::ostringstream ss;
	sgsnInfoDump(this,ss);
	SGSNLOG("Removing SgsnInfo:"<<ss);
	sSgsnInfoList.remove(this);
	delete this;
}

// This is for use by the Command Line Interface
// Return true on success.
bool cliSgsnInfoDelete(SgsnInfo *si)
{
	ScopedLock lock(sSgsnListMutex);
	GmmInfo *gmm = si->getGmm();
	if (gmm && gmm->getSI() == si) {
		// You cannot delete this si by itself.  Must delete the GmmInfo instead.
		return false;
	}
	si->sirm();
	return true;
}

// This is the generalized printer to identify an SgsnInfo.
// The alternate sgsnInfoDump is used only for gmmDump and prints
// only that info that is not duplicated in the Gmm.
std::ostream& operator<<(std::ostream& os, const SgsnInfo*si)
{
	MSUEAdapter *ms = si->getMS();
	if (ms) {
		os << ms->msid();
	} else {
#if RN_UMTS
		os << LOGHEX2("URNTI", si->mMsHandle);
#else
		os << LOGHEX2("TLLI", si->mMsHandle);
#endif
	}
	if (si->getGmm()) { os << LOGVAR2("imsi",si->getGmm()->mImsi.hexstr()); }
	return os;
}

// Reset this connection, for example, because it is doing a GmmDetach or a new GmmAttach.
void SgsnInfo::sgsnReset()
{
	freePdpAll(true);
	if (mLlcEngine) { mLlcEngine->getLlcGmm()->reset(); }
}

// The operator is allowed to choose the P-TMSI allocation strategy, subject to the constraints
// that they should not collide in the same routing area, must not be all 1s, and we dont allow all 0s either.
// Note that the MS sends RA [Routing Area] along with TMSI.
// The MS creates a local TLLI from the P-TMSI by setting the top two bits,
// so the P-TMSI is really limited to 30 bits.
// For UMTS, the URNTI consists of a 20-bit (really 16-bit, because it must fit in that) UE id
// plus a 12 bit SRNC id.
static uint32_t gPTmsiNext = 0;
static uint32_t allocatePTmsi()
{
	if (gPTmsiNext == 0) {
		// Add in the time to the starting TMSI so if the BTS is restarted there is a better chance
		// of not using the same tmsis over again.
		time_t now;
		time(&now);
		gPTmsiNext = ((now&0xff)<<12) + 1;
	}
	if (gPTmsiNext == 0 || gPTmsiNext >= (1<<30)) { gPTmsiNext = 1; }
	return gPTmsiNext++;
	//return Tlli::makeLocalTlli(gPTmsiNext++);
}

MSUEAdapter *SgsnInfo::getMS() const
{
	// The MSInfo struct disappears after a period of time, so look it up.
	//return GPRS::gL2MAC.macFindMSByTLLI(mMsHandle,0);
	return SgsnAdapter::findMs(mMsHandle);
}

GmmInfo::GmmInfo(ByteVector &imsi):
	mImsi(imsi), mState(GmmState::GmmDeregistered), msi(0)
{
	memset(mPdps,0,sizeof(mPdps));
	mPTmsi = allocatePTmsi();
	mGprsMultislotClass = -1;		// -1 means invalid.
	mAttachTime = 0;
	// Must set activityTime to prevent immediate removal from list by another phone simultaneously connection.
	setActivity();
	ScopedLock lock(sSgsnListMutex);
	sGmmInfoList.push_back(this);
}

GmmInfo::~GmmInfo()
{
	freePdpAll(true);
}

// Assumes sSgsnListMutex is locked on entry.
static void GmmRemove(GmmInfo *gmm)
{
	std::ostringstream ss;
	gmmInfoDump(gmm,ss,0);
	SGSNLOG("Removing gmm:"<<ss);
	SgsnInfo *si;
	RN_FOR_ALL(SgsnInfoList_t,sSgsnInfoList,si) {
		// The second test here should be redundant.
		if (si->getGmm() == gmm || gmm->getSI() == si) {
			si->sirm();	// yes this is suboptimal, but list is short
		}
	}
#if 0
	for (SgsnInfoList_t::iterator itr = sSgsnInfoList.begin(); itr != sSgsnInfoList.end(); ) {
		SgsnInfo *si = *itr;
		if (si->getGmm() == gmm) {
			itr = sSgsnInfoList.erase(itr);
			delete si;
		} else {
			itr++;
		}
	}
#endif
	sGmmInfoList.remove(gmm);
	delete gmm;
}


// This is for use by the Command Line Interface
void cliGmmDelete(GmmInfo *gmm)
{
	ScopedLock lock(sSgsnListMutex);
	GmmRemove(gmm);
}

PdpContext *GmmInfo::getPdp(unsigned nsapi)
{
	//return mSndcp[nsapi] ? mSndcp[nsapi]->mPdp : 0;
	assert(nsapi < sNumPdps);
	setActivity();
	return mPdps[nsapi];
}

// True if the pdpcontext is not in state PDP-INACTIVE
bool GmmInfo::isNSapiActive(unsigned nsapi)
{
	assert(nsapi < sNumPdps);
	return !(mPdps[nsapi] == 0 || mPdps[nsapi]->isPdpInactive());
}

// This status is sent back to the MS in messages to indicate what the Network thinks
// what PDPContexts are currently in use.
PdpContextStatus GmmInfo::getPdpContextStatus()
{
	PdpContextStatus result;
	for (int i = 0; i <= 7; i++) {
		if (isNSapiActive(i)) { result.mStatus[0] |= (1<<i); }
		if (isNSapiActive(i+8)) { result.mStatus[1] |= (1<<i); }
	}
	return result;
}

void GmmInfo::connectPdp(PdpContext *pdp, mg_con_t *mgp)
{
	// Order may be important here.
	// We dont want to hook the mgp up until the stack is all connected and prepared
	// to receive packets, because they could come blasting in any time,
	// even before any outgoing packets are sent.
	assert(pdp->mNSapi >= 0 && pdp->mNSapi < (int)sNumPdps);
	mPdps[pdp->mNSapi] = pdp;
	// getSI() should never NULL.  The mLlcEngine is null in umts.
	SgsnInfo *si = getSI();
	assert(si);
	if (si->mLlcEngine) { si->mLlcEngine->allocSndcp(si,pdp->mNSapi,pdp->mLlcSapi); }
	mg_con_open(mgp,pdp);
}

// Return TRUE if the pdp was allocated.
bool GmmInfo::freePdp(unsigned nsapi)
{
	assert(nsapi < sNumPdps);
	PdpContext *pdp = mPdps[nsapi];
	mPdps[nsapi] = 0;
	if (pdp) delete pdp;	// This disconnects the mgp also.
	// getSI() should never be NULL.  The mLlcEngine is null in umts.
#if SNDCP_IN_PDP
	// sndcp is in the PdpContext and deleted automatically.
	// Do we want to reset the LLC Sapi?  Doubt it because it is shared.
#else
	LlcEngine *llc = getSI() ? getSI()->mLlcEngine : NULL;
	if (llc) { llc->freeSndcp(nsapi); }
#endif
	return !!pdp;
}

void SgsnInfo::deactivateRabs(unsigned nsapiMask)
{
#if RN_UMTS
	MSUEAdapter *ms = getMS();
	if (ms) {
		ms->msDeactivateRabs(nsapiMask);
	} else {
		SGSNERROR("ggsn: DeactivatePdpContextRequest: MS not found "<<this);
	}
#endif
}

// Return a mask of RABs that were freed.
unsigned GmmInfo::freePdpAll(bool freeRabsToo)
{
	unsigned rabMask = 0;
	for (unsigned nsapi = 0; nsapi < sNumPdps; nsapi++) {
		if (freePdp(nsapi)) { rabMask |= 1<<nsapi; }
	}
	if (freeRabsToo && rabMask) {
		// It would be a serious internal error for getSI() to fail, but check anyway.
		if (getSI()) { getSI()->deactivateRabs(rabMask); }
	}
	if (rabMask) addShellRequest("PdpDeactivateAll",this);
	return rabMask;
}

void SgsnInfo::sgsnSend2PdpLowSide(int nsapi, ByteVector &packet)
{
	PdpContext *pdp = getPdp(nsapi);
	assert(pdp);
	pdp->pdpWriteLowSide(packet);
}

// The rbid is not used by GPRS, and is just 0.
void SgsnInfo::sgsnSend2MsHighSide(ByteVector &pdu,const char *descr, int rbid)
{
		MSUEAdapter *ms = getMS();
#if RN_UMTS
		// TODO: It would be safer not to call getMS, but just send the dlpdu through
		// an InterthreadQueue and let the UMTS or GPRS L2 handle that part in its own thread.
		// In that case we have to add oldTlli to the message also.
		if (!ms) {
			SGSNWARN("no corresponding MS for URNTI " << mMsHandle);
			return;
		}
		// For UMTS we pass the rbid which is an intrinsic part of this channel.
		// TODO: Update UMTS to use DownlinkPdu too.
		ms->msWriteHighSide(pdu,rbid,descr);
#else
		GmmInfo *gmm = getGmm();
		uint32_t tlli, aliasTlli = 0;
		if (gmm && gmm->isRegistered()) {
			tlli = gmm->getTlli();	// The TLLI based on the assigned P-TMSI.
		} else {
			// We send the message using the TLLI of the SgsnInfo,
			// which is the one the MS used to talk to us.
			tlli = mMsHandle;
			// If we know the P-TMSI that will be used for the local TLLI
			// for this MS after the attach procedure, notify L2.
			if (gmm) { aliasTlli = gmm->getTlli(); }
			if (aliasTlli == tlli) { aliasTlli = 0; }	// Be tidy; but dont think this can happen.
		}
		if (!ms) {
			LOG(WARNING) << "no corresponding MS for TLLI " << mMsHandle;
			return;
		}
		GprsSgsnDownlinkPdu *dlpdu = new GprsSgsnDownlinkPdu(pdu,tlli,aliasTlli,descr);
		//ms->msWriteHighSide(dlpdu);
		// This is thread safe:
		// Go ahead and enqueue it even if there is no MS
		SgsnAdapter::saWriteHighSide(dlpdu);
#endif
}

void SgsnInfo::sgsnWriteHighSideMsg(L3GprsDlMsg &msg)
{
#if RN_UMTS
		// bypass llc
		ByteVector bv(1000);
		bv.setAppendP(0,0);
		msg.gWrite(bv);
		SGSNLOG("Sending "<<msg.str() <<this);
		sgsnSend2MsHighSide(bv,msg.mtname(),SRB3);	// TODO: Is SRB3 correct?
#else
		LlcDlFrame lframe(1000);
		lframe.setAppendP(0,0);
		msg.gWrite(lframe);
		SGSNLOG("Sending "<<msg.str() <<this<<" frame(first20)="<<lframe.head(MIN(20,lframe.size())));
		mLlcEngine->getLlcGmm()->lleWriteHighSide(lframe,msg.isSenseCmd(),msg.mtname());
#endif
}

// Incoming packets on a PdpContext come here.
void SgsnInfo::sgsnWriteHighSide(ByteVector &sdu,int nsapi)
{
#if RN_UMTS
		// The PDCP is a complete no-op.
		sgsnSend2MsHighSide(sdu,"userdata",nsapi);
#else
		mLlcEngine->llcWriteHighSide(sdu,nsapi);
#endif
}

// TLLI 03.03 2.6, Specified in binary:
// starts with 11 - local tlli
// starts with 10 - foreign tlli
// starts with 01111 - random tlli
// starts with 01110 - auxiliary tlli.
// TLLI may not be all 1s, and if it starts with one of the above, cant be all 0s either.
//struct Tlli {
//	enum Type { Unused, LocalTlli, ForeignTlli, RandomTlli, AuxTlli, UnknownTlli };
//	static Type tlli2Type(uint32_t tlli) {
//		unsigned toptwo = tlli >> (32-2);	// It is unsigned, dont have to mask.
//		if (toptwo == 0x3) return LocalTlli;
//		if (toptwo == 0x2) return ForeignTlli;
//		unsigned topfive = tlli >> (32-5);	// It is unsigned, dont have to mask.
//		if (topfive == 0x0f) return RandomTlli;
//		if (topfive == 0x0e) return AuxTlli;
//		return UnknownTlli;
//	}
//	//static uint32_t tlli2ptmsi(uint32_t tlli) { return tlli & ~sLocalTlliMask; }
//	// Make a local TLLI
//	//static uint32_t makeLocalTlli(uint32_t tmsi) { return tmsi | sLocalTlliMask; }
//};

// Return Network Mode of Operation 1,2,3
static int getNMO()
{
	return gConfig.getNum("GPRS.NMO");
}

void sendAttachAccept(SgsnInfo *si)
{
	si->mT3310FinishAttach.reset();
	GmmInfo *gmm = si->getGmm();
	assert(gmm);
	//L3GmmMsgAttachAccept aa(si->attachResult(),gmm->getPTmsi(),si->mAttachMobileId);
	uint32_t ptmsi = gmm->getPTmsi();
	L3GmmMsgAttachAccept aa(si->attachResult(),ptmsi);
	// We are finished with the attach procedure now.
	// Note that we are using the si (and TLLI) that the message was sent on.
	// If the BTS and the MS disagreed on the attach state at the start of this procedure,
	// we reset the MS registration to match what the MS thinks to make sure we will
	// use the old TLLI in the si, not the new one based on the PTMSI.
	si->sgsnWriteHighSideMsg(aa);
}

static void handleAttachStep(SgsnInfo *si)
{
	GmmInfo *gmm = si->getGmm();
	if (!gmm) {	// This cannot happen.
		SGSNERROR("No imsi found for MS during Attach procedure"<<si);
		return;
	}
#if RN_UMTS
		// Must do the Security Proecedure first, message flow like this:
		//      L3 AttachRequest
		// MS ---------------------------------> Network
		//      RRC SecurityModeCommand
		// MS <--------------------------------- Network
		//      RRC SecurityModeComplete
		// MS ---------------------------------> Network
		//     L3 AttachAccept
		// MS <--------------------------------- Network
		// (pat) Update: Havind added the authentication for NMO I in here,
		// so the above procedure is now moved to 

		// If we are in NMO 2, authentication was allegedly already done by
		// the Mobility Management protocol layer, in which case there is
		// a Kc sitting in the TMSI table.
		// We need to pass it a nul-terminated IMSI string.
		string IMSI = gmm->mImsi.hexstr();
		//int len = gmm->mImsi.size();
		//char imsi[len+2];
		//memcpy(imsi,gmm->mImsi.hexstr().c_str(),len);
		//imsi[len] = 0;
		LOG(INFO) << "Looking up Kc for imsi " << IMSI;
		string Kcs = gTMSITable.getKc(IMSI.c_str());
		if (Kcs.length() <= 1) {
			SGSNERROR("No Kc found for MS in TMSI table during Attach procedure"<<si);
			// need to do authentication, send authentication request
                        //sendAuthenticationRequest(si);
		}
		sendAuthenticationRequest(si,IMSI);
#else
		// We must use the TLLI that the MS used, not the PTMSI.
		// To do that, reset the registered status.
		gmm->setGmmState(GmmState::GmmDeregistered);
		sendAttachAccept(si);
#endif
}

#if RN_UMTS
// Called from UMTS when it receives the SecurityModeComplete or SecurityModeFailure msg.
void MSUEAdapter::sgsnHandleSecurityModeComplete(bool success)
{
	SgsnInfo *si = sgsnGetSgsnInfo();
	// The si would only be null if the UE sent us a spurious SecurityModeComplete command.
	if (si == NULL) {
		SGSNERROR("Received spurious SecurityMode completion command for UE:"<<msid());
		return;
	}
	if (! si->mT3310FinishAttach.active()) {
		SGSNERROR("Received security response after T3310 expiration for UE:"<<si);
		return;
	}
	if (success) {
		sendAttachAccept(si);	// happiness
	} else {
		SGSNERROR("Integrity Protection failed for UE:"<<si);
		// Oops!  We could send an attach reject, but why bother?
		// The UE already knows it failed, no recovery is possible,
		// and it will timeout shortly anyway.
	}
}
#endif

#if RN_UMTS
static void sendAuthenticationRequest(SgsnInfo *si, string IMSI)
{
        SIPEngine engine(gConfig.getStr("SIP.Proxy.Registration").c_str(),IMSI.c_str());
	string RAND;
        //bool success =
		engine.Register(SIPEngine::SIPRegister, &RAND);
	// Stick new UE into TMSI table if its not already there
	if (!gTMSITable.TMSI(IMSI.c_str())) gTMSITable.assign(IMSI.c_str());

        ByteVector rand(RAND.size()/2);    // Leave it random.
	for (unsigned i = 0; i < RAND.size(); i++) {
		char ch = (RAND.c_str())[i];
		ch = (ch > '9') ? ((ch & 0x0f) + 9) : (ch & 0x0f);
		rand.setField(i*4,ch,4);
	}
        L3GmmMsgAuthentication amsg(rand);
        si->sgsnWriteHighSideMsg(amsg);
	si->mRAND = rand;
}
#endif

static void handleAuthenticationResponse(SgsnInfo *si, L3GmmMsgAuthenticationResponse &armsg) 
{
	if (Sgsn::isUmts()) {
                GmmInfo *gmm = si->getGmm();
                if (!gmm) {
                        SGSNERROR("No imsi found for MS during Attach procedure"<<si);
                        return;
                }

                string IMSI = gmm->mImsi.hexstr();
		string RAND = si->mRAND.hexstr();
                // verify SRES 
		bool success = false;
                try {
                        SIPEngine engine(gConfig.getStr("SIP.Proxy.Registration").c_str(),IMSI.c_str());
                        SGSNLOG("waiting for registration on IMSI: " << IMSI);
                        string SRESstr = armsg.mSRES.hexstr();
                        success = engine.Register(SIPEngine::SIPRegister, &RAND, IMSI.c_str(), SRESstr.c_str());
                }
                catch(SIPTimeout) {
                        SGSNLOG("SIP authentication timed out.  Is the proxy running at " << gConfig.getStr("SIP.Proxy.Registration"));
                        // TODO: Reject 
                        return;
                }

		if (!success) return;

                LOG(INFO) << "Looking up Kc for imsi " << IMSI;
                string Kcs = gTMSITable.getKc(IMSI.c_str());
                if (Kcs.length() <= 1) {
                        SGSNERROR("No Kc found for MS in TMSI table during Attach procedure"<<si);
                        // need to do authentication, send authentication request
                        //sendAuthenticationRequest(si);
                }

#if RN_UMTS
                SgsnAdapter::startIntegrityProtection(si->mMsHandle,Kcs);
#endif
	}
}

static void handleIdentityResponse(SgsnInfo *si, L3GmmMsgIdentityResponse &irmsg)
{
	if (! si->mT3310FinishAttach.active()) {
		// Well that is interesting.  We got a spurious identity response.
		SGSNERROR("unexpected message:"<<irmsg.str());
		return;
	} else {
		// The MS sent an attach request.  Try to send the response using the new IMSI.
		if (! irmsg.mMobileId.isImsi()) {
			SGSNERROR("Identity Response message does not include imsi:"<<irmsg.str());
			return;
		}
		ByteVector passbyreftmp = irmsg.mMobileId.getImsi();		// c++ foo bar
		findGmmByImsi(passbyreftmp,si);	// Always succeeds - creates if necessary, sets si->mGmmp.

		// Use the imsi as the mobileId in the AttachAccept.
		//si->mAttachMobileId = irmsg.mMobileId;
		handleAttachStep(si);
		//si->mT3310FinishAttach.reset();
		//GmmInfo *gmm = findGmmByImsi(passbyreftmp,si);	// Always succeeds - creates if necessary.
		// TODO: Why do we send the mobileid?  It seems to Work this way, just wondering, because
		// the message is delivered to the MS based on the L2 connection as defined by si.
		//L3GmmMsgAttachAccept aa(si->attachResult(),gmm->getPTmsi(),irmsg.mMobileId);
		//si->sgsnWriteHighSideMsg(aa);
	}
}

void AttachInfo::stashMsgInfo(GMMAttach &msgIEs,
	bool isAttach)	// true: attach request; false: RAUpdate
{
	// Save the MCC and MNC from which the MS drifted in on for reporting.
	// We only save them the first time we see them, because I am afraid
	// after that they will revert to our own MCC and MNC.
	if (! mPrevRaId.valid()) { mPrevRaId = msgIEs.mOldRaId; }

	//if (mOldMcc[0] == 0 && mOldMcc[1] == 0) {
	//	for (int i = 0; i < 3; i++) { mOldMcc[i] = DEHEXIFY(msgIEs.mOldRaId.mMCC[i]); }
	//}
	//if (mOldMnc[0] == 0 && mOldMnc[1] == 0) {
	//	for (int i = 0; i < 3; i++) { mOldMnc[i] = DEHEXIFY(msgIEs.mOldRaId.mMNC[i]); }
	//}

	// If a PTMSI was specified in the AttachRequest we need to remember it.
	if (isAttach && msgIEs.mMobileId.isTmsi()) {
		mAttachReqPTmsi = msgIEs.mMobileId.getTmsi();
	}

	if (msgIEs.mMsRadioAccessCapability.size()) {
		mMsRadioAccessCap = msgIEs.mMsRadioAccessCapability;
	}
	//mAttachMobileId = msgIEs.mMobileId;
}

void AttachInfo::copyFrom(AttachInfo &other)
{
	if (! mPrevRaId.valid()) { mPrevRaId = other.mPrevRaId; }
	if (! mAttachReqPTmsi) { mAttachReqPTmsi = other.mAttachReqPTmsi; }
	if (! mAttachReqType) { mAttachReqType = other.mAttachReqType; }
	if (other.mMsRadioAccessCap.size()) {
		mMsRadioAccessCap = other.mMsRadioAccessCap;
	}
}

void sendImplicitlyDetached(SgsnInfo *si)
{
	L3GmmMsgGmmStatus statusMsg(GmmCause::Implicitly_detached);
	si->sgsnWriteHighSideMsg(statusMsg);
	// The above didn't do it, so try sending one of these too:
	// Detach type 1 means re-attach required.
	//L3GmmMsgDetachRequest dtr(1,GmmCause::Implicitly_detached);
	// 7-2012: Tried taking out the cause to stop the Multitech modem
	// sending 'invalid mandatory information'.
	// The only reason obvious to send that is in 24.008 8.5 is an unexpected IE,
	// so maybe it is the cause.  But it did not help.
	L3GmmMsgDetachRequest dtr(1,0);
	si->sgsnWriteHighSideMsg(dtr);
}

// The ms may send a P-TMSI or IMSI in the mobile id.
static void handleAttachRequest(SgsnInfo *si, L3GmmMsgAttachRequest &armsg)
{
	switch ((AttachType) (unsigned) armsg.mAttachType) {
	case AttachTypeGprsWhileImsiAttached:
		SGSNLOG("NOTICE attach type "<<(int)armsg.mAttachType <<si);
		// Fall through
	case AttachTypeGprs:
		si->mtAttachInfo.mAttachReqType = AttachTypeGprs;
		break;
	case AttachTypeCombined:
		if (getNMO() != 1) {
			// The MS should not have done this.
			LOG(ERR)<<"Combined Attach attempt incompatible with NMO 1 "<<si;
		} else {
			SGSNLOG("NOTICE attach type "<<(int)armsg.mAttachType <<si);
		}
		si->mtAttachInfo.mAttachReqType = AttachTypeCombined;
		break;
	}
	//uint32_t newptmsi;

	// Save info from the message:
	si->mtAttachInfo.stashMsgInfo(armsg,true);

	// Re-init the state machine.
	// If the MS does a re-attach, we may have an existing SgsnInfo from earlier, so we must reset it now:
	// si->sgsnReset(); // 6-3-2012: changed to just freePdpAll.
	si->freePdpAll(true);

	GmmInfo *gmm = si->getGmm();
	// 7-1-2012: Working on multitech modem failure to reattach bug.
	// I tried taking this out to send an extra identity request,
	// but then the modem did not respond to that identity request,
	// just like before it did not respond to the second attach request.
	// Even after deleting all but that single SgsnInfo, and modifying the msid
	// to print both tllis, and looking at pat.log the message is definitely
	// sent on the correct TLLi.
	// But if you tell the modem to detach and then try attach again,
	// then the modem uses a new TLLI and sends an IMSI, so it thinks
	// it was attached, but it is sending an attach request anyway, with a PTMSI.
	// But the first attach used a PTMSI, and it succeeded.
	// Things to try:  send protocol incompabible blah blah.
	// Try converting to local tlli (0xc...); I tried that before but maybe
	// the tlli change procedure was wrong back then.
	// Send a detach, although I think the modem ignores this.
	if (gmm) {
		// We already have an IMSI for this MS, where the MS was identified by some TLLI
		// associated with this SgsnInfo, which means we already did
		// the IdentityResponse challenge.  Just use it.
	} else {
		// We need an imsi.  If it is not in the message, we will need to ask for it.
		ByteVector imsi;
		// There is a slight problem that we only have 6 seconds to register the MS,
		// which may not be enough time to do the IdentityResponse Challenge.
		// Therefore we save the IMSI associated with the TLLI that we got from the Identity response
		// challenge in the SgsnInfo, and when the MS tries again with the same TLLI,
		// we can skip the IdentityRequest phase.
		//if (si->mImsi.size()) {
		//	// Already did the identity challange; use the previously queried imsi from this ms.
		//	imsi = si->mImsi;
		//} else {
			// If the MS did not send us an IMSI already, ask for one.
			if (armsg.mMobileId.isImsi()) {
				// The MS included the IMSI in the attach request
				imsi = armsg.mMobileId.getImsi();
				findGmmByImsi(imsi,si);	// Create the gmm and associate with si.
			} else {
				// 3GPP 24.008 11.2.2 When T3370 expires we can send another Identity Request.
				// However we are also going to use it inverted, and send Identity Requests
				// no closer together than T3370.
				// If this expires, the MS will try again.
				if (! si->mT3370ImsiRequest.active() || si->mT3370ImsiRequest.expired()) {
					// Send off a request for the imsi.
					L3GmmMsgIdentityRequest irmsg;
					si->mT3370ImsiRequest.set();
					// We only use the timer in this case, so we only set it in this case, instead
					// of at the top of this function.
					si->mT3310FinishAttach.set();
					si->sgsnWriteHighSideMsg(irmsg);
				}
				return;
			}
		//}
		//SgsnInfo *si2 = Sgsn::findAssignedSgsnInfoByImsi(imsi);
		//newptmsi = si2->mMsHandle;
	}
#if 0
	// We dont care if the MS already had a P-TMSI.
	// If it is doing an attach, go ahead and assign a new one.
	if (!si->mAllocatedTmsiTlli) {
		si->mAllocatedTmsiTlli = Sgsn::allocateTlli();
	}
	// We cant set the tlli in the MS until it has received the new tlli,
	// because we have to use the previous tlli to talk to it.
#endif
	// This was for testing:
	//L3GmmMsgIdentityRequest irmsg;
	//si->sgsnWriteHighSideMsg(irmsg);

	// We are assigning this ptmsi to the MS.
	handleAttachStep(si);
	//si->mT3310FinishAttach.reset();
	//L3GmmMsgAttachAccept aa(si->attachResult(),gmm->getPTmsi(),armsg.mMobileId);
	//si->sgsnWriteHighSideMsg(aa);
}


static void handleAttachComplete(SgsnInfo *si, L3GmmMsgAttachComplete &acmsg)
{
	// The ms is acknowledging receipt of the new tlli.
	GmmInfo *gmm = si->getGmm();
	if (! gmm) {
		// The attach complete does not match this ms state.
		// Happens, for example, when you first turn on the bts and the ms
		// is still trying to complete a previous attach.  Ignore it.
		// The MS will timeout and try to attach again.
		SGSNLOG("Ignoring spurious Attach Complete" << si);
		// Dont send a reject because we did not reject anything.
		return;
	}
	//SGSNLOG("attach complete gmm="<<((uint32_t)gmm));
	gmm->setGmmState(GmmState::GmmRegisteredNormal);
	gmm->setAttachTime();
#if RN_UMTS
#else
	// Start using the tlli associated with this imsi/ptmsi when we talk to the ms.
	si->changeTlli(true);
#endif
	addShellRequest("GprsAttach",gmm);

#if 0 // nope, we are going to pass the TLLI down with each message and let GPRS deal with it.
	//if (! Sgsn::isUmts()) {
	//	// Update the TLLI in all the known MS structures.
	//	// Only the SGSN knows that the MSInfo with these various TLLIs
	//	// are in fact the same MS.  But GPRS needs to know because
	//	// the MS will continue to use the old TLLIs, and it will botch
	//	// up if, for example, it is in the middle of a procedure on one TLLI
	//	// and the MS is using another TLLI, which is easy to happen given the
	//	// extremely long lag times in message flight.
	//	// The BSSG spec assumes there only two TLLIs, but I have seen
	//	// the Blackberry use three simultaneously.
	//	SgsnInfo *sip;
	//	uint32_t newTlli = gmm->getTlli();
	//	RN_FOR_ALL(SgsnInfoList_t,sSgsnInfoList,sip) {
	//		if (sip->getGmm == gmm) {
	//			UEAdapter *ms = sip->getMS();
	//			// or should we set the ptmsi??
	//			if (ms) ms->changeTlli(newTlli);
	//		}
	//	}
	//}
#endif
}

static void handleDetachRequest(SgsnInfo *si)
{
	L3GmmMsgDetachAccept detachAccept(0);
	GmmInfo *gmm = si->getGmm();
	if (!gmm) {
		// Hmm, but fall through, because it is certainly detached.
	} else {
		gmm->setGmmState(GmmState::GmmDeregistered);
	}
	si->sgsnWriteHighSideMsg(detachAccept);
	si->sgsnReset();
	if (gmm) addShellRequest("GprsDetach",gmm);
}

static void sendRAUpdateReject(SgsnInfo *si,unsigned cause)
{
	L3GmmMsgRAUpdateReject raur(cause);
	si->sgsnWriteHighSideMsg(raur);
}

// TODO:  Need to follow 4.7.13 of 24.008
static void handleServiceRequest(SgsnInfo *si, L3GmmMsgServiceRequest &srmsg)
{
        GmmInfo *gmm = si->getGmm();
	// TODO:  Should we check the PTmsi and the PDP context status??? 
        if (!gmm) {
	        L3GmmMsgServiceReject sr(GmmCause::Implicitly_detached);
        	si->sgsnWriteHighSideMsg(sr);
                return;
        } else {
                gmm->setActivity();
                L3GmmMsgServiceAccept sa(si->getPdpContextStatus());
                si->sgsnWriteHighSideMsg(sa);
        }
} 

// 24.008 4.7.5, and I quote:
// "The routing area updating procedure is always initiated by the MS.
// 	It is only invoked in state GMM-REGISTERED."
// The MS may send an mMobileId containing a P-TMSI, and it sends TmsiStatus
// telling if it has a valid TMSI.
static void handleRAUpdateRequest(SgsnInfo *si, L3GmmMsgRAUpdateRequest &raumsg)
{
	bool sendTmsi = 0;
	RAUpdateType updatetype = (RAUpdateType) (unsigned)raumsg.mUpdateType;
	switch (updatetype) {
	case RAUpdated:
	case PeriodicUpdating:
		updatetype = RAUpdated;
		if (getNMO() == 1) {
			updatetype = CombinedRALAUpdated;
		}
		break;
	case CombinedRALAUpdated:
	case CombinedRALAWithImsiAttach:	// As of 4-29-2012, we dont even save the imsi.
		if (getNMO() != 1) {
			// TODO: Should we send a reject, or an accept with a different updatetype?
			// I think the type should have matched the NMO broadcast in the beacon,
			// so we should reject.
			// Warning: This reject is saved in the MS semi-permanently,
			// and it will not try again.
			// DEBUG: Try just accepting the LAUpdate unconditionally...
			//sendRAUpdateReject(si,GmmCause::Location_Area_not_allowed);
			//return;
			LOG(ERR)<<"Routing Area combined Location Area Update request incompatible with NMO 1 "<<si;
		}
		updatetype = CombinedRALAUpdated;
		if (! raumsg.mTmsiStatus) {
			// We must assign a tmsi, so make one up.
			// Just use the tlli, but lop off some bits so we can tell what it is.
			sendTmsi = true;
		}
		break;
	}
	si->mtAttachInfo.stashMsgInfo(raumsg,false);

	GmmInfo *gmm = si->getGmm();
	if (! gmm) {
		// The MS has not registered with us yet, so reject the RAUpdate.
		// Doesnt seem like this should be always needed, because we want to accept anyone.
		// But this seems to work, and it didnt work when I didnt do this.
		// This makes the MS come back with an AttachRequest message.
		// I have seen the Blackberry trying RAUpdate with the same foreign TLLI about 10 times,
		// and getting rejects before coming back with the AttachRequest, but it eventually did.
		// TODO: Maybe use a different cause for foreign TLLI.
		// And I quote, from 24.008 Annex G.6:
		// 	"Cause value = 9 MS identity cannot be derived by the network
		// 	"This cause is sent to the MS when the network cannot derive the MS's identity from
		//	"the P-TMSI in case of inter-SGSN routing area update.
		//	"Cause value = 10 Implicitly detached
		//	"This cause is sent to the MS either if the network has implicitly detached the MS,
		//	"e.g. some while after the Mobile reachable timer has expired,
		//	"or if the GMM context data related to the subscription dose not exist in the
		//	"SGSN e.g. because of a SGSN restart.
		// Also, see 4.7.1.5.4 which gives the specific behavior the MS to each of these causes.
		// Specifically, cause 10 is the correct one to make the MS reregister and get new contexts.

		// I did not try just assigning a new TMSI in the RAUpdateAccept message.
		// Also have not tried just echoing back the TLLI that the MS used in L2 as
		// the allocated-TMSI in this response, which might work,
		// but is not procedurally correct if the TMSI came from a different routing area.

		if (raumsg.mPdpContextStatus.anyDefined()) {
			// If the MS thinks it has PDP contexts, we need to explicitly release them.
			// TODO: We do that in the raaccept message...
			// Let MS establish a session, then turn BTS off and on; the MS continues to
			// use the old IP addresses with its new pdp contexts.
			// Not exactly sure why, maybe my bug somewhere.
			// 4-30: I tried putting this back in but it did not work -
			// The blackberry continued to send RAUpdateRequest that indicated it
			// did not tear them down.
			// 24.008 4.7.5.1.3 Indicates all you have to do is send an RaUpdateAccept
			// with the pdpstatus zeroed out.
			//sendSmStatus(si,SmCause::Unknown_PDP_address_or_PDP_type);
			//sendPdpDeactivateAll(si, SmCause::Unknown_PDP_address_or_PDP_type);
		}
		// 4.7.5.1.4 says that 'cause 9' shall make the MS delete its P-TMSI,
		// enter state GMM-DEREGISTERED, and subsequently automatically initiate GPRS attach.
		// However, it didnt work for me on the blackberry after a test that ended in TBF failure.
		// Had to turn off the MS and turn it back on, then ok.
		// Maybe it had entered state LIMITED.SERVICE,
		// which prevents attaches, or maybe NO.CELL.AVAILABLE.
		// It was trying to register with the mobile-id set to no value.
		// Cause 10 looks like it might be better: MS releases PDP contexts,
		// enters GMM-DEREGISTERED.NORMAL, and forces a new attach.
		sendRAUpdateReject(si,GmmCause::Implicitly_detached);
		return;
	} else {
		gmm->setActivity();
		/** wrong
		if (si->getState() != GmmState::GmmDeregistered) {
			//sendRAUpdateReject(si,GmmCause::MessageNotCompatibleWithProtocolState);
			// This is the cause that the opensgsn sends:
			sendRAUpdateReject(si,GmmCause::MS_identity_cannot_be_derived_by_the_network);
			return;
		}
		***/

		// The RAUpdate result is yes only if the MS has attached to us.
		// Note that this message gets back to the originating MS guaranteed at layer2,
		// regardless of whatever tmsi/mobile-id we put in this message.
		// TODO: Do we need to set the allocated P-TMSI or not?  Not sure.
		// The blackberry did not work without it, but it may have been sql open-registration was wrong.
		// DONT DO THIS:
		//if (gConfig.defines("SGSN.RAUpdateIncludeTmsi") && gConfig.getNum("SGSN.RAUpdateIncludeTmsi")) {
		//	tmsi = si->mTlli;
		//}

		//if (updatetype == CombinedRALAUpdated) {
			// DEBUG: try this
			// Send an authentication request to make the MS happy about this.
			//sendAuthenticationRequest(si);
		//}

		// We are not integrated with the OpenBTS stack yet,
		// so if we need a tmsi just make one up.
		uint32_t ptmsi = gmm->getPTmsi();
		L3GmmMsgRAUpdateAccept raa(updatetype, si->getPdpContextStatus(),ptmsi,
			sendTmsi ? ptmsi : 0);
		si->sgsnWriteHighSideMsg(raa);
	}
}

static void handleRAUpdateComplete(SgsnInfo *si, L3GmmMsgRAUpdateComplete &racmsg)
{
	// Do not need to do anything.
}

// This message may arrive on a DCCH channel via the GSM RR stack, rather than a GPRS message,
// and as such, could be running in a separate thread.
// We queue the message for processing.
// The suspension may be user initiated or by the MS doing some RR messages,
// most often, Location Area Update.  The spec says we are supposed to freeze
// the LLC state and continue after resume.  But in the permanent case any incoming packets
// will be hopelessly stale after resumption, so we just toss them.  Note that web sites chatter
// incessantly with keepalives even when they look quiescent to the user, and we dont want
// all that crap to back up in the downlink queue.
// In the temporary case, which is only a second or two, we will attempt to preserve the packets
// to prevent a temporary loss of service.  I have observed that the MS first stops responding
// to the BSS for about a second before sending the RACH to initiate the RR procedure,
// so there is no warning at all.  However, we MUST cancel the TBFs.  If we dont, and after
// finishing the RR procedure the MS gets back to GPRS before the previous TBFs timeout,
// it assumes they are new TBFs, which creates havoc, because the acknacks do not correspond
// to the previous TBF.  This generates the "STUCK" condition, up to a 10 second loss of service,
// and I even saw the Blackberry detach and reattach to recover.
// In either case the MS signals resumption by sending us anything on the uplink.
// WARNING: This runs in a different thread.
bool Sgsn::handleGprsSuspensionRequest(uint32_t wTlli,
	const ByteVector &wraid)	// The Routing Area id.
{
	SGSNLOG("Received GPRS SuspensionRequest for"<<LOGHEX2("tlli",wTlli));
	return false;	// Not handled yet.
	// TODO:
	// if sgsn not enabled, return false.
	// save the channel?
	// Send the resumption ie in the RR channel release afterward.
}

// WARNING: This runs in a different thread.
void Sgsn::notifyGsmActivity(const char *imsi)
{
}

static void handleL3GmmMsg(SgsnInfo *si,ByteVector &frame1)
{
	L3GmmFrame frame(frame1);
	// Standard L3 header is 2 bytes:
	unsigned mt = frame.getMsgType();	// message type
	//SGSNLOG("CRACKING GMM MSG TYPE "<<mt);
	MSUEAdapter *ms = si->getMS();
	if (ms == NULL) {
		// This is a serious internal error.
		SGSNERROR("L3 message "<<L3GmmMsg::name(mt)
			<<" for non-existent MS Info struct" <<LOGHEX2("tlli",si->mMsHandle));
		return;
	}
	switch (mt) {
	case L3GmmMsg::AttachRequest: {
		L3GmmMsgAttachRequest armsg;
		armsg.gmmParse(frame);
		SGSNLOG("Received "<<armsg.str()<<si);
		handleAttachRequest(si,armsg);
		dumpGmmInfo();
		break;
	}
	case L3GmmMsg::AttachComplete: {
		L3GmmMsgAttachComplete acmsg;
		//acmsg.gmmParse(frame);	// not needed, nothing in it.
		SGSNLOG("Received "<<acmsg.str()<<si);
		handleAttachComplete(si,acmsg);
		dumpGmmInfo();
		break;
	}
	case L3GmmMsg::IdentityResponse: {
		L3GmmMsgIdentityResponse irmsg;
		irmsg.gmmParse(frame);
		SGSNLOG("Received "<<irmsg.str()<<si);
		handleIdentityResponse(si,irmsg);
		break;
	}
	case L3GmmMsg::DetachRequest: {
		SGSNLOG("Received DetachRequest");
		handleDetachRequest(si);
		break;
	}
	case L3GmmMsg::DetachAccept:
		SGSNLOG("Received DetachAccept");
		//TODO...
		break;
	case L3GmmMsg::RoutingAreaUpdateRequest: {
		L3GmmMsgRAUpdateRequest raumsg;
		raumsg.gmmParse(frame);
		SGSNLOG("Received "<<raumsg.str()<<si);
		handleRAUpdateRequest(si,raumsg);
		break;
	}
	case L3GmmMsg::RoutingAreaUpdateComplete: {
		L3GmmMsgRAUpdateComplete racmsg;
		//racmsg.gmmParse(frame);  not needed
		SGSNLOG("Received RAUpdateComplete "<<si);
		handleRAUpdateComplete(si,racmsg);
		break;
	}
	case L3GmmMsg::GMMStatus: {
		L3GmmMsgGmmStatus stmsg;
		stmsg.gmmParse(frame);
		SGSNLOG("Received GMMStatus: "<<stmsg.mCause<<"=" <<GmmCause::name(stmsg.mCause)<<si);
		break;
	}
	case L3GmmMsg::AuthenticationAndCipheringResp: {
		L3GmmMsgAuthenticationResponse armsg;
		armsg.gmmParse(frame);
		SGSNLOG("Received AuthenticationAndCipheringResp message "<<si);
		handleAuthenticationResponse(si,armsg);
		break;
	}
	case L3GmmMsg::ServiceRequest: {
		L3GmmMsgServiceRequest srmsg;
		srmsg.gmmParse(frame);
		SGSNLOG("Received ServiceRequest message" << si);
		handleServiceRequest(si,srmsg);
		break;
	}

		// Downlink direction messages:
		//RoutingAreaUpdateAccept = 0x09,
		//AttachAccept = 0x02,
		//AttachReject = 0x04,
		//RoutingAreaUpdateReject = 0x0b,

		// Other: TODO?
		//ServiceAccept = 0x0d,
		//ServiceReject = 0x0e,
		//PTMSIReallocationCommand = 0x10,
		//PTMSIReallocationComplete = 0x11,
		//AuthenticationAndCipheringRej = 0x14,
		//AuthenticationAndCipheringFailure = 0x1c,
		//GMMInformation = 0x21,
	default:
		//SGSNWARN("Ignoring GPRS GMM message type "<<mt <<L3GmmMsg::name(mt));
		return;
	}
}



// This is the old UMTS-centric entry point
//void Sgsn::sgsnWriteLowSide(ByteVector &payload,SgsnInfo *si, unsigned rbid)
//{
//	// No Pdcp, so just send it off.
//	si->sgsnSend2PdpLowSide(rbid, payload);
//}

// The handle is the URNTI and the rbid specfies the rab.
// In gprs, the handle is the TLLI and all the rab info is encoded into the
// payload with LLC headers so rbid is not used, which was a pretty dopey design.
void MSUEAdapter::sgsnWriteLowSide(ByteVector &payload, uint32_t handle, unsigned rbid)
{
	SgsnInfo *si = sgsnGetSgsnInfoByHandle(handle,true);	// Create if necessary.
#if RN_UMTS
	// No Pdcp, so just send it off.
	si->sgsnSend2PdpLowSide(rbid, payload);
#else
	si->mLlcEngine->llcWriteLowSide(payload,si);
#endif
}

#if RN_UMTS
void MSUEAdapter::sgsnHandleL3Msg(uint32_t handle, ByteVector &msgFrame)
{
	SgsnInfo *si = sgsnGetSgsnInfoByHandle(handle,true);	// Create if necessary.
	handleL3Msg(si,msgFrame);
}
#endif

void handleL3Msg(SgsnInfo *si, ByteVector &bv)
{
	unsigned pd = 0;
	try {
		L3GprsFrame frame(bv);
		if (frame.size() == 0) { // David saw this happen.
			//SGSNWARN("completely empty L3 uplink message "<<si);
			return;
		}
		pd = frame.getNibble(0,0);	// protocol descriminator
		switch ((GSM::L3PD) pd) {
		case GSM::L3GPRSMobilityManagementPD: {	// Couldnt we shorten this?
			handleL3GmmMsg(si,frame);
			break;
		}
		case GSM::L3GPRSSessionManagementPD: {	// Couldnt we shorten this?
			Ggsn::handleL3SmMsg(si,frame);
			break;
		}
		// TODO: Send GSM messages somewhere
		default:
			SGSNERROR("unsupported L3 Message PD:"<<pd);
		}
	} catch(SgsnError) {
		return;	// Handled already
	} catch(ByteVectorError) {	// oops!
		SGSNERROR("internal error assembling SGSN message, pd="<<pd);	// not much to go on.
	}
}

// Forces the SgsnInfo to exist.
// For GPRS the handle is a TLLI.
// From GSM03.03 sec 2.6 Structure of TLLI; and reproduced at class MSInfo comments.
// The top bits of the TLLI encode where it came from.
// A local TLLI has top 2 bits 11, and low 30 bits are the P-TMSI.
// For UMTS, the handle is the invariant URNTI.
SgsnInfo *findSgsnInfoByHandle(uint32_t handle, bool create)
{
	// Update: the lock is needed because the suspension request is sent by the GSM RR stack
	// running in a separate thread.
	ScopedLock lock(sSgsnListMutex); // I dont think this is necessary, but be safe.

	SgsnInfo *si, *result = NULL;
	// We can delete unused SgsnInfo as soon as the attach procedure is over,
	// which is 15s, but let them hang around a bit longer so the user can see them.
	int idletime = gConfig.getNum("SGSN.Timer.MS.Idle");
	time_t now; time(&now);
	RN_FOR_ALL(SgsnInfoList_t,sSgsnInfoList,si) {
		if (si->mMsHandle == handle) {result=si; continue;}
#if RN_UMTS
#else
#if NEW_TLLI_ASSIGN_PROCEDURE
		if (si->mAltTlli == handle) {result=si;continue;}
#endif
#endif
		// Kill off old ones, except ones that are the primary one for a gmm.
		GmmInfo *gmm = si->getGmm();
		if (gmm==NULL || gmm->getSI() != si) {
			if (now - si->mLastUseTime > idletime) { si->sirm(); }
		}
	}
	if (result) {
		time(&result->mLastUseTime);
		return result;
	}
	if (!create) { return NULL; }

	// Make a new one.
	SgsnInfo *sinew = new SgsnInfo(handle);
	return sinew;
}

// Now we create the SgsnInfo for the assigned ptmsi as soon as the ptmsi is created,
// even if the MS has not used it yet.
//GmmInfo *SgsnInfo::findGmm()
//{
//	if (mGmmp) { return mGmmp; }	// Hooked up previously.
//	return NULL;
// Old comment:
// For GPRS, the MS contacts with some random tlli, then we create a GmmInfo and a PTMSI,
// and send the PTMSI to the MS, but the GmmInfo is not yet hooked to any SgsnInfos.
// The MS will then call us again using a TLLI derived from the PTMSI,
// and we hook up that SgsnInfo to the GmmInfo right here.
//	if (! Sgsn::isUmts()) {
//		uint32_t tlli = mMsHandle;
//		// Only a local TLLI can be converted to a P-TMSI to look up the Gmm context.
//		if (Tlli::tlli2Type(tlli) == Tlli::LocalTlli) {
//			uint32_t ptmsi = Tlli::tlli2ptmsi(tlli);
//			GmmInfo *gmm;
//			RN_FOR_ALL(GmmInfoList_t,sGmmInfoList,gmm) {
//				if (gmm->mPTmsi == ptmsi) {
//					SGSNLOG("Hooking up"<<LOGHEX2("tlli",tlli)<<" to"<<LOGHEX2("ptmsi",ptmsi));
//					this->setGmm(gmm);
//					gmm->msi = this;
//					return gmm;
//				}
//			}
//		}
//	} else {
//		// In UMTS the Gmm context is indexed by URNTI.
//		// If this doesnt work right, we will need to look up the Gmm context
//		// from the ptmsi in the L3 messages.
//	}
//	return NULL;
//}

// Works, but not currently used:
void MSUEAdapter::sgsnFreePdpAll(uint32_t mshandle)
{
	SgsnInfo *si = sgsnGetSgsnInfoByHandle(mshandle,false);
	if (si) si->freePdpAll(true);
}

// Forces it to exist if it did not already.
static SgsnInfo *sgsnGetSgsnInfoByHandle(uint32_t mshandle, bool create)
{
	// We cant cache this thing for GPRS because it changes
	// during the TLLI assignment procedure.
	// We could cache it for UMTS, but that assumes the lifetime of the SgsnInfo
	// is greater than the UE, both of which are controlled by user parameters,
	// so to be safe, we are just going to look it up every time.
	// TODO: go back to caching it in UMTS only.
	//if (! mSgsnInfo) {
		//uint32_t mshandle = msGetHandle();
		//mSgsnInfo = findSgsnInfoByHandle(mshandle,create);
	//}
	//return mSgsnInfo;
	return findSgsnInfoByHandle(mshandle,create);
}

#if RN_UMTS
SgsnInfo *MSUEAdapter::sgsnGetSgsnInfo()
{
	uint32_t mshandle = msGetHandle();
	return findSgsnInfoByHandle(mshandle,false);
}
#else
void MSUEAdapter::sgsnSendKeepAlive()
{
	// TODO
}
#endif

#if RN_UMTS
	// not applicable
#else
static void parseCaps(GmmInfo *gmm)
{
	if (/*gmm->mGprsMultislotClass == -1 &&*/ gmm->mgAttachInfo.mMsRadioAccessCap.size()) {
		MsRaCapability caps(gmm->mgAttachInfo.mMsRadioAccessCap);
		gmm->mGprsMultislotClass = caps.mCList[0].getCap(AccessCapabilities::GPRSMultislotClass);
		gmm->mGprsGeranFeaturePackI = caps.mCList[0].getCap(AccessCapabilities::GERANFeaturePackage1);
	}
}


int MSUEAdapter::sgsnGetMultislotClass(uint32_t mshandle)
{
	SgsnInfo *si = sgsnGetSgsnInfoByHandle(mshandle,false);
	if (!si) { return -1; }
	GmmInfo *gmm = si->getGmm();	// Must be non-null or we would not be here.
	if (!gmm) { return -1; }		// But dont crash if I'm mistaken.
	parseCaps(gmm);
	return gmm->mGprsMultislotClass;
}

bool MSUEAdapter::sgsnGetGeranFeaturePackI(uint32_t mshandle)
{
	SgsnInfo *si = sgsnGetSgsnInfoByHandle(mshandle,false);
	if (!si) { return -1; }
	GmmInfo *gmm = si->getGmm();	// Must be non-null or we would not be here.
	if (!gmm) { return -1; }		// But dont crash if I'm mistaken.
	parseCaps(gmm);
	return gmm->mGprsGeranFeaturePackI;
}
#endif

GmmState::state MSUEAdapter::sgsnGetRegistrationState(uint32_t mshandle)
{
	SgsnInfo *si = sgsnGetSgsnInfoByHandle(mshandle,false);
	if (!si) { return GmmState::GmmDeregistered; }
	GmmInfo *gmm = si->getGmm();	// Must be non-null or we would not be here.
	if (!gmm) { return GmmState::GmmDeregistered; }
	return gmm->getGmmState();
}


#if RN_UMTS
void MSUEAdapter::sgsnHandleRabSetupResponse(unsigned rabId, bool success)
{
	SgsnInfo *si = sgsnGetSgsnInfo();
	if (si == NULL) {
		// Dont think this can happen, but be safe.
		SGSNERROR("Received spurious RabSetupResponse for UE:"<<msid());
		return;
	}
	if (success) {
		PdpContext *pdp = si->getPdp(rabId);
		if (pdp==NULL) return; // FIXME: Not sure what to do here
		if (pdp->mUmtsStatePending) {
			pdp->update(pdp->mPendingPdpr);
			pdp->mUmtsStatePending = false;
		}
		sendPdpContextAccept(si,pdp);
	} else {
		// We do NOT want to send a RAB teardown message - we got here because
		// the RAB setup did not work in the first place.  Just free it.
		si->freePdp(rabId);
	}
}
#endif

const char *GmmState::GmmState2Name(GmmState::state state)
{
	switch (state) {
	CASENAME(GmmDeregistered)
	CASENAME(GmmRegistrationPending)
	CASENAME(GmmRegisteredNormal)
	CASENAME(GmmRegisteredSuspsended)
	}
	return "";
}

// The alternate sgsnInfoPrint is used only for gmmDump and prints
void sgsnInfoDump(SgsnInfo *si,std::ostream&os)
{
	//if (si == gmm->getSI()) {continue;}		// Already printed the main one.
	uint32_t handle = si->mMsHandle;
	os << "SgsnInfo"<<LOGHEX(handle)
		<<" T3370:active="<<si->mT3370ImsiRequest.active()
		<<" remaining=" << si->mT3370ImsiRequest.remaining();
		MSUEAdapter *ms = si->getMS();
		if (ms) { os << ms->msid(); }
		else { os << " MS=not_active"; }
		AttachInfo *ati = &si->mtAttachInfo;
		if (ati->mPrevRaId.valid()) { os << " prev:"; ati->mPrevRaId.text(os); }
	if (!si->getGmm()) { os << " no gmm"; }
	os << endl;
}

void gmmInfoDump(GmmInfo *gmm,std::ostream&os,int options)
{
	os << " GMM Context:";
	os << LOGVAR2("imsi",gmm->mImsi.hexstr());
	os << LOGHEX2("ptmsi",gmm->mPTmsi);
	os << LOGHEX2("tlli",gmm->getTlli());
	os << LOGVAR2("state",GmmState::GmmState2Name(gmm->getGmmState()));
	time_t now; time(&now);
	os << LOGVAR2("age",(gmm->mAttachTime ? now - gmm->mAttachTime : 0));
	os << LOGVAR2("idle",now - gmm->mActivityTime);
	SgsnInfo *si = gmm->getSI();
	if (!(options & printNoMsId)) {
		if (si) {	// Can this be null?  No, but dont crash.
			// The mPrevRaId is generally invalid in the SgsnInfo for the GMM,
			// because it is the one we assigned, and the routing info is in the SgsnInfo
			// the MS initially called in on.
			//os << LOGVAR2("oldMCC",si->mOldMcc);
			//os << LOGVAR2("oldMNC",si->mOldMnc);
			// The GPRS ms struct will disappear shortly after the MS stops communicating with us.
			MSUEAdapter *ms = si->getMS();
			if (ms) { os << ms->msid(); }
			else { os << " MS=not_active"; }
		}
	}

	os << " IPs=";
	int pdpcnt = 0;
	for (unsigned nsapi = 0; nsapi < GmmInfo::sNumPdps; nsapi++) {
		if (gmm->isNSapiActive(nsapi)) {
			// FIXME: Darn it, we need to lock the pdp contexts for this too.
			// Go ahead and do it anyway, because collision is very low probability.
			PdpContext *pdp = gmm->getPdp(nsapi);
			mg_con_t *mgp;	// Temp variable reduces probability of race; the mgp itself is immortal.
			if (pdp && (mgp=pdp->mgp)) {
				char buf[30];
				if (pdpcnt) {os <<",";}
				os << ip_ntoa(mgp->mg_ip,buf);
			}
			pdpcnt++;
		}
	}
	if (pdpcnt == 0) { os <<"none"; }
	os << endl;

	if (options & printDebug) {
		// Print out all the SgsnInfos associated with this GmmInfo.
		RN_FOR_ALL(SgsnInfoList_t,sSgsnInfoList,si) {
			if (si->getGmm() != gmm) {continue;}
			os <<"\t";	// this sgsn is associated with the GmmInfo just above it.
			sgsnInfoDump(si,os);
		}
	}

	// Now the caps:
	if ((options & printCaps) && gmm->mgAttachInfo.mMsRadioAccessCap.size()) {
		MsRaCapability caps(gmm->mgAttachInfo.mMsRadioAccessCap);
		caps.text2(os,true);
	}
	//os << endl;	// This is extra.  There is one at the end of the caps.
}

void gmmDump(std::ostream &os)
{
	// The sSgsnListMutex exists for this moment: to allow this cli list routine
	// to run from a foreign thread.
	ScopedLock lock(sSgsnListMutex);
	int debug = sgsnDebug();
	GmmInfo *gmm;
	RN_FOR_ALL(GmmInfoList_t,sGmmInfoList,gmm) {
		gmmInfoDump(gmm,os,debug ? printDebug : 0);
		os << endl;	// This is extra.  There is one at the end of the caps.
	}
	// Finally, print out SgsnInfo that are not yet associated with a GmmInfo.
	if (debug) {
		SgsnInfo *si;
		RN_FOR_ALL(SgsnInfoList_t,sSgsnInfoList,si) {
			if (! si->getGmm()) { sgsnInfoDump(si,os); }
		}
	}
}

void dumpGmmInfo()
{
	if (sgsnDebug()) {
		std::ostringstream ss;
		gmmDump(ss);
		SGSNLOG(ss.str());
	}
}

void SgsnInfo::setGmm(GmmInfo *gmm)
{
	// Copy pertinent info from the Routing Update or Attach Request message into the GmmInfo.
	gmm->mgAttachInfo.copyFrom(mtAttachInfo);
	this->mGmmp = gmm;
}

#if NEW_TLLI_ASSIGN_PROCEDURE
static void killOtherTlli(SgsnInfo *si,uint32_t newTlli)
{
	SgsnInfo *othersi = findSgsnInfoByHandle(newTlli,false);
	if (othersi && othersi != si) {
		if (othersi->getGmm()) {
			// This 'impossible' situation can happen if an MS that
			// is already attached tries to attach again.
			// Easy to reproduce by pulling the power on an MS.  (Turning off may not be enough.)
			// For example:
			// MS -> attachrequest TLLI=80000001; creates new SgsnInfo 80000001
			// MS <- attachaccept
			// MS -> attachcomplete TLLI=c0000001; SgsnInfo 80000001 changed to c0000001
			// MS gets confused (turn off or pull battery)
			// MS -> attachrequest TLLI=80000001; creates new SgsnInfo 80000001
			// MS <- attachaccept TLLI=80000001;  <--- PROBLEM 1!  See below.
			// MS -> attachcomplete TLLI=c0000001; <--- PROBLEM 2: Both SgsnInfo exist.
			// PROBLEM 1:  We have already issued the TLLI change command so L2
			// is now using the new TLLI.  We will use the old TLLI (80000001)
			// in the attachaccept, which will cause a 'change tlli' procedure in L2
			// to switch back to TLLI 80000001 temporarily.
			// PROBLEM 2: Solved by deleting the original registered SgsnInfo (c0000001 above)
			// and then caller will change the TLLI of the unregistred one (80000001 above.)
			SGSNWARN("Probable repeat attach request: TLLI change procedure"<<LOGVAR(newTlli)
				<<" for SgsnInfo:"<<si
				<<" found existing registered SgsnInfo:"<<othersi);
			// I dont think any recovery is possible; sgsn is screwed up.
		} else {
			// We dont know or care where this old SgsnInfo came from.
			// Destroy it with prejudice and use si, which is the
			// SgsnInfo the MS is using to talk with us right now.
			SGSNWARN("TLLI change procedure"<<LOGVAR(newTlli)
				<<" for SgsnInfo:"<<si
				<<" overwriting existing unregistered SgsnInfo:"<<othersi);
			othersi->sirm();
		}
	}
}
#endif

// Switch to the new tlli.  If now, do it now, otherwise, just allocate the new SgsnInfo,
#if NEW_TLLI_ASSIGN_PROCEDURE
// just set altTlli to the future TLLI we will be using after the attach procedure,
#endif
// which we do ahead of time so we can inform GPRS L2 that the new and old TLLIs are the same MS.
// Return the new si.  In the new tlli procedure, it is the same as the old.
// ------------------
// Note the following sequence, easy to reproduce by pulling the power on an MS:
// MS -> attachrequest TLLI=80000001; creates new SgsnInfo 80000001
// MS <- attachaccept
// MS -> attachcomplete TLLI=c0000001; SgsnInfo 80000001 changed to c0000001
// MS gets confused (turn off or pull battery)
// MS -> attachrequest TLLI=80000001; creates new SgsnInfo 80000001
// MS <- attachaccept TLLI=80000001;  <--- PROBLEM 1!  See below.
// MS -> attachcomplete TLLI=c0000001; <--- PROBLEM 2: Both SgsnInfo exist.
// PROBLEM 1:  We have already issued the TLLI change command so L2
// is now using the new TLLI.  Easy fix is we use the old TLLI (80000001)
// in the attachaccept, which will cause a 'change tlli' procedure in L2
// to switch back to TLLI 80000001 temporarily until we receive attachcomplete.
// PROBLEM 2: The SgsnInfo for the new tlli will already exist.
// ???? Solved by deleting the original registered SgsnInfo (c0000001 above)
// ???? and then caller will change the TLLI of the unregistred one (80000001 above.)
SgsnInfo * SgsnInfo::changeTlli(bool now)
{
	GmmInfo *gmm = getGmm();
	uint32_t newTlli = gmm->getTlli();
#if NEW_TLLI_ASSIGN_PROCEDURE
	SgsnInfo *si = this;
	if (si->mMsHandle != newTlli) {
		killOtherTlli(si,newTlli);
		if (now) {
			si->mAltTlli = si->mMsHandle;
			si->mMsHandle = newTlli;
		} else {
			si->mAltTlli = newTlli;
		}
	}
	return si;
#else
	SgsnInfo *othersi = findSgsnInfoByHandle(newTlli,true);
	// We will use the new tlli for downlink l3 messages, eg, pdp context messages,
	// unless they use some other SI specifically, like AttachAccept
	// must be sent on the SI tha the AttachRequest arrived on.
	othersi->setGmm(gmm);
	if (now) { gmm->msi = othersi; }
	return othersi;
#endif
}

// If si, forces the GmmInfo for this imsi to exist and associates it with that si.
// If si == NULL, return NULL if gmm not found - used for CLI.
GmmInfo *findGmmByImsi(ByteVector &imsi, SgsnInfo *si)
{
	ScopedLock lock(sSgsnListMutex);
	GmmInfo *gmm, *result = NULL;
	// 24.008 11.2.2: Implicit Detach timer default is 4 min greater
	// than T3323, which can be provided in AttachAccept, otherwise
	// defaults to T3312, which defaults to 54 minutes.
	int attachlimit = gConfig.getNum("SGSN.Timer.ImplicitDetach");	// expiration time in seconds.
	time_t now; time(&now);
	RN_FOR_ALL(GmmInfoList_t,sGmmInfoList,gmm) {
		if (now - gmm->mActivityTime > attachlimit) {
			GmmRemove(gmm);
			continue;
		}
		if (gmm->mImsi == imsi) { result = gmm; }
	}
	if (result) {
		if (si) si->setGmm(result);
		return result;
	}
	if (!si) { return NULL; }

	// Not found.  Make a new one in state Registration Pending.
	gmm = new GmmInfo(imsi);
	gmm->setGmmState(GmmState::GmmRegistrationPending);
	si->setGmm(gmm);
	gmm->msi = si;
#if RN_UMTS
		// For UMTS, the si is indexed by URNTI, which is invariant, so hook up and we are finished.
#else
		// We hook up the GMM context to the SgsnInfo corresponding to the assigned P-TMSI,
		// even if that SgsnInfo does not exist yet,
		// rather than the SgsnInfo corresponding to the current TLLI, which could be anything.
		// The MS will use the SgsnInfo for the P-TMSI to talk to us after a successful attach.
		si->changeTlli(false);
//#if NEW_TLLI_ASSIGN_PROCEDURE
//		// 3GPP 04.64 7.2.1.1 and 8.3: Perform the TLLI reassignment procedure.
//		// Change the TLLI in the SgsnInfo the MS is currently using.
//		// The important point is that the LLC state does not change.
//		uint32_t newTlli = gmm->getTlli();
//		killOtherTlli(si,newTlli);
//		// Do the TLLI reassignment.
//		if (si->mMsHandle != newTlli) { // Any other case is extremely unlikely.
//			// We must continue using the existing MsHandle until we
//			// receive the AttachComplete message from the MS, but mark
//			// that the new tlli is an alias for this TLLI.
//			si->mAltTlli = newTlli;
//		}
//#else
//		SgsnInfo *newsi = findSgsnInfoByTlli(gmm->getTlli(),true);
//		newsi->setGmm(gmm);
//		// NO, not until attachComplete: gmm->msi = newsi;	// Use this one instead.
//#endif
#endif
	return gmm;
}

void RabStatus::text(std::ostream &os) const
{
	os <<"RabStatus(mStatus=";
	switch (mStatus) {
	case RabIdle: os << "idle"; break;
	case RabFailure: os << "failure"; break;
	case RabPending: os << "pending"; break;
	case RabAllocated: os << "allocated"; break;
	case RabDeactPending: os << "deactPending"; break;
	}
	os<<LOGVAR2("mFailCode",SmCause::name(mFailCode));
	os<<LOGVAR(mRateDownlink)<<LOGVAR(mRateUplink);
	os<<")";
}

void MSUEAdapter::sgsnPrint(uint32_t mshandle, int options, std::ostream &os)
{
	ScopedLock lock(sSgsnListMutex);		// Probably not needed.
	SgsnInfo *si = sgsnGetSgsnInfoByHandle(mshandle,false);
	if (!si) { os << " GMM state unknown\n"; return; }
	GmmInfo *gmm = si->getGmm();	// Must be non-null or we would not be here.
	if (!gmm) { os << " GMM state unknown\n"; return; }
	gmmInfoDump(gmm,os,options);
}

}; // namespace
