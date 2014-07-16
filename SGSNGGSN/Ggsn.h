/*
* Copyright 2011, 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#ifndef GGSN_H
#define GGSN_H
#include <Interthread.h>
#include <LinkedLists.h>
#include <ByteVector.h>
#include "SgsnBase.h"
#include "SgsnExport.h"
#include "GPRSL3Messages.h"
#include "GSMCommon.h"	// For Z100Timer - I really dont want to include the other stuff here.
#include "miniggsn.h"

namespace SGSN {
class LlcEntityGmm;
class L3GprsFrame;
void *miniGgsnReadServiceLoop(void *arg);
void *miniGgsnWriteServiceLoop(void *arg);
void sendPdpDeactivateAll(SgsnInfo *si, SmCause::Cause cause);
void sendSmStatus(SgsnInfo *si,SmCause::Cause cause);
void sendPdpContextAccept(SgsnInfo *si, PdpContext *pdp);

struct PdpPdu : SingleLinkListNode {
	//PdpPdu *mNext;
	ByteVector mpdu;
	//PdpContext *mpdp;
	mg_con_t *mgp;
	public:
	//PdpPdu *next() { return mNext; }
	//void setNext(PdpPdu*wNext) { mNext = wNext; }
	PdpPdu(ByteVector wpdu,mg_con_t *wmgp) : mpdu(wpdu), mgp(wmgp) { RN_MEMCHKNEW(PdpPdu) }
	~PdpPdu() { RN_MEMCHKDEL(PdpPdu) }
};


struct ShellRequest {
	std::string msrCommand;
	std::string msrArg1, msrArg2, msrArg3;
};
void addShellRequest(const char *wCmd,GmmInfo*gmm,PdpContext *pdp=0);
void addShellRequest(const char *wCmd,const char *arg);

class Ggsn {
	// Normally a PdpContext is associated with a BVCI to identify the BTS
	// and the tlli, which is included with each message to the GGSN and comes
	// from the L2 layer - every message includes tlli from the MSInfo.
	// For our case, we can associate it directly with the Sndcp instance.
	// The transaction identifier comes from the L3 message and is needed to establish
	// secondary pdp contexts, which we dont support yet.
	// It is conceivable that the PdpContext can be deleted while there
	bool mActive;
	Thread mGgsnRecvThread;
	Thread mGgsnSendThread;
	Thread mGgsnShellThread;
	Bool_z mShellThreadActive;
	public:
	static const unsigned mStopTimeout = 3000;	// How often the service loops check for active.
	InterthreadQueue2<PdpPdu,SingleLinkList<> > mTxQ;
	InterthreadQueue2<ShellRequest> mShellQ;

	public:
	static void handleL3SmMsg(SgsnInfo *si, L3GprsFrame &frame);

	// If it returns false, the service loop exits.
	bool active() { return true; }

	static void stop();
	// Return true on success
	static bool start();

};
extern Ggsn gGgsn;

// The PdpContext is a holder for an IP address and controlling information like QoS.
// The downstream is attached to an Sndcp entity, which is attached to an LlcEntity,
// which points to an MSInfo, which gets the TBF to send the data downlink.
// The upstream is an mg_con_t, which is a raw IP address.
// There can be multiple PDPContexts for a single IP address.
// That is the case if secondary PDP contexts are activated -
// the different ones have different TFTs [Traffic Flow Templates].
// Looks to me like the primary purpose is to route different IP protocols to different SAPs,
// or to have different priority packets for the same IP address,
// routed way up here at this level.
// Currently we dont deal with that.
//
// When the PDPContext is destroyed, we must keep the mg_con_t unused for a while because:
//	1.  There could be messages in the Ggsn TxQ that still refer to the PdpContext.
//	2.  We are required to by the spec, because there could be incoming packets on
//		the IP address for some time after it is released.
// Note: the L3 Create PDP Context message is sent to the MM SAPI of the llc,
// not the LLC sap to which this is attached.
struct Sndcp;
class PdpContext
{	public:
	// A real pdp context would live in a GGSN and include a TEID [tunnel endpoint]
	// consisting of the sgsn id and the TLLI, but we dont need it because we have a direct
	// pointer to the SgsnInfo entity.  This is a vast simplication, particularly because when
	// either a TLLI or PdpContext is modified in a traditional system, those changes have to go into
	// a nebulous pending state until they ripple downward, and an acknowledgement from the MS ripples
	// all the way back up.  This is particularly confusing for the TLLI, since that is what we use
	// to talk to the MS, so the L3 and L2 normally use different TLLIs during the TLLI assignment process.
	// For UMTS, we just send the message directly to the ms associated with the SgsnInfo
	// via the UEAdapter.
	// GPRS is a bit more complicated because we have to take a little side-trip through LLC:
	// Oh, the PdpContext is connected to the...SgsnInfo,
	// and the SgsnInfo is connected to the...LLCEngine
	// and the LLCEngine is connected to the...Sndcp
	// and the Sndcp is connected to the...LlcEntityUserData,
	// and the LlcEntityUserData is connected to the...LlcEntity
	// and the LlcEntity is connected to the...SgsnInfo [again]
	// and the SgsnInfo is connected to the...MSInfo
	// and the MSInfo is connected to the...TBF
	// and the TBF is connected to the RLCEngine...
	// Dem bones gonna walk around...
	GmmInfo *mpcGmm;
	int mNSapi;
	// LlcSapi is not used in UMTS, but we are supposed to put the same value from the incoming messages
	// we received in the response messages anyway.
	int mLlcSapi;
	mg_con_t *mgp;	// Contains the IP address.
	int mTransactionId;	// From the L3 message that created this PdpContext, needed during deactivation??
#if SNDCP_IN_PDP
	Sndcp *mSndcp1;		// Not used in UMTS.
#endif

	void pdpWriteLowSide(ByteVector &payload);
	void pdpWriteHighSide(unsigned char *packet, unsigned packetlen);

	// Once the connection is set up we dont care about this stuff any more,
	// but we have to cache it for UMTS because the PdpContextAccept message is not sent out instantly.
	// You cant save the pco - each one has uniquifying identifiers in it.
	ByteVector mPcoReq;	// Requested Protocol Config Options - from L3 uplink message.
	ByteVector mQoSReq;	// Requested QoS, which we are not currently using.
	//ByteVector mPdpAddr;
	//ByteVector mApName;

	RabStatus mRabStatus;	// After the link is allocated, we set this information about it.
							// This info goes out in the Pdp Context allocation message.


	// PDP STATES:
	// 3GPP 24.008 6.1.2.2 defines "Session Management States on the Network Side"
	// Those states are are applicable only to network-initiated PDP context state changes,
	// unused for MS-initiated PSP context activation/modification etc.
	// In GPRS, there are only two states - when the MS requests a PDP context, it becomes active.
	// There is no received indication of success, although we could get an indication
	// of whether the message was successfully delivered from L2.
	// For UMTS, after we receive a PDP activiate/modify/etc from the UE, we must first
	// do UMTS RadioBearer setup/modification/teardown before replying.  That puts the PDP Context
	// into a pending a state that is not related to "Session Management States on the Network Side".
	// This state is reset when we receive the radiobearer setup acknowledgment,
	// at which time we send the PDP ActivateRequestAccept.  These messages are sent
	// in acknowledged mode, but we are not currently using that acknowledgement.
	// If the UE asks for it again, we will send it again.
	bool mUmtsStatePending;
	L3SmMsgActivatePdpContextRequest mPendingPdpr; // the request upon which we are pending, since the pending has to be cleared by the Rab setup response handler

	// I dont think we have to remember that state here.
	// It is ok to resend the PDP Context Accept message each time we receive a
	// UMTS Radio Bearer Setup Complete message.

	// For the purposes of transmitting a state indication to the MS in any L3 message,
	// there are only two possibilities: PDP-INACTIVE or any other state.
	// For GPRS, if the PdpContext exists, it is active.
	// Note that gprs suspension occurs in the GMM layer (below us),
	// and is a substate of GMM-REGISTERED, so I dont think it affects us in the SM layer at all.
	bool isPdpInactive() {
#if RN_UMTS
			// For UMTS: we are supposed to wait until we receive the radioBearerSetupComplete
			return mUmtsStatePending;
#else
			// For GPRS: we exist, therefore we are active.
			return false;
#endif
	}
	//PdpContext(Sndcp *wSndcp, mg_con_t *wmgp, L3SmMsgActivatePdpContextRequest &pdpr);
	//PdpContext(int wNSapi, mg_con_t *wmgp, L3SmMsgActivatePdpContextRequest &pdpr);
	PdpContext(GmmInfo *wgmm, mg_con_t *wmgp, int nsapi, int llcsapi);
	~PdpContext();
	void update(L3SmMsgActivatePdpContextRequest &pdpr);
};
#if GGSN_IMPLEMENTATION
	// For GPRS the Sndcp deletes us when it is deleted; there is no other way.
	PdpContext::~PdpContext() {
		// Make sure nobody points at us, although a dangling Sndcp with no PdpContext would be an error.
		//if (mpdpDownstream && mpdDownstream->getPdp()) {
		//	mpdpDownstream->setPdp(0);
		//}
		//mpdpDownstream = 0;		// Just being tidy.
		if (mgp) { mg_con_close(mgp); mgp = 0; }
	}
	// void setPco();  The pco could conceivably vary by PdpContext type, but we arent worrying about it.

	// Update based on most recent request.
	// Critical for UMTS because the PdpContextAccept message is delayed
	// so we have to save this info from the request.
	void PdpContext::update(L3SmMsgActivatePdpContextRequest &pdpr)
	{
		mPcoReq = pdpr.mPco;
		mQoSReq = pdpr.mQoS;
		mTransactionId = pdpr.mTransactionId;
	}

	PdpContext::PdpContext(GmmInfo *wgmm, mg_con_t *wmgp, int nsapi, int llcsapi) :
		mpcGmm(wgmm),
		mNSapi(nsapi),
		mLlcSapi(llcsapi),
		//mNSapi(pdpr.mNSapi),
		//mLlcSapi(pdpr.mLlcSapi),
		mgp(wmgp),
		//mTransactionId(pdpr.mTransactionId),
		mSndcp1(NULL),
		//mPcoReq(pdpr.mPco),
		//mQoSReq(pdpr.mQoS),
		//mPdpAddr(pdpr.mPdpAddress),
		//mApName(pdpr.mApName),
		//mTransactionId(pdpr.mTransactionId)
		mUmtsStatePending(0)
	{
		//mT3385.configure(gConfig.getNum("UMTS.Timers.T3385",8));
		//mT3395.configure(gConfig.getNum("UMTS.Timers.T3395",8));
	}

	void PdpContext::pdpWriteLowSide(ByteVector &payload) {
		SNDCPDEBUG("pdpWriteLowSide"<<LOGVAR2("packetlen",payload.size()));
		PdpPdu *newpdu = new PdpPdu(payload,this->mgp);
		gGgsn.mTxQ.write(newpdu);
	}
	void PdpContext::pdpWriteHighSide(unsigned char *packet, unsigned packetlen) {
		SNDCPDEBUG("pdpWriteHighSide"<<LOGVAR2("packetlen",packetlen));
		ByteVectorTemp sdu(packet,packetlen);
		//mpdpDownstream->snWriteHighSide(sdu);
		mpcGmm->getSI()->sgsnWriteHighSide(sdu,mNSapi);
	}
#endif

}; // namespace
#endif
