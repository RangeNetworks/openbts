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

#ifndef SGSNEXPORT_H
#define SGSNEXPORT_H
#include <string>
#include <Globals.h>	// For RN_UMTS
#include "ByteVector.h"
#include "SgsnBase.h"	// For SmCause
#include "LinkedLists.h"
#include "MemoryLeak.h"

namespace SGSN {
class GmmInfo;
class SgsnInfo;

// Printing option flags.
enum PrintOptions {
	printVerbose = 1,
	printCaps = 2,
	printDebug = 4,
	printNoMsId = 8
};

// 24.008 4.1.3 GMM state machine picture and description.
class GmmState
{ 	public:
	enum state {
		//GmmNotOurTlli,		// This tlli was not assigned by us.
		GmmDeregistered,	// Tlli was assigned by us, but not registered yet.
		GmmRegistrationPending,
		GmmRegisteredNormal,		// aka "GPRS Attached"
		GmmRegisteredSuspsended
	};
	static const char *GmmState2Name(state);
};

// A class for downlink messages from the SGSN to GPRS/UMTS.
struct SgsnDownlinkMsg
{
	//enum { } msgType;
	ByteVector mDlData;	// The PDU itself.
	std::string mDescr;	// Description of this pdu.
	SgsnDownlinkMsg(ByteVector a, std::string wDescr) : mDlData(a), mDescr(wDescr) { RN_MEMCHKNEW(SgsnDownlinkMsg) }
	// The virtual keyword on a destructor indicates that both the base destructor (this one)
	virtual ~SgsnDownlinkMsg() { RN_MEMCHKDEL(SgsnDownlinkMsg) }
	//SgsnDownlinkPdu(SgsnDownlinkPdu&other) { *this = other; }
};

struct GprsSgsnDownlinkPdu : public SingleLinkListNode, public SgsnDownlinkMsg
{
	uint32_t mTlli;		// For gprs: the TLLI of the MS to receive the message.
						// (In gprs NSAPI is encoded in the LLC message in the data.)
	uint32_t mAliasTlli;// Another TLLI that the SGSN knows refers to the same MS as the above.
	Timeval mDlTime;
	bool isKeepAlive() { return false; }	// Is this is a dummy message?
	unsigned size() { return mDlData.size(); }	// Decl must exactly match SingleLinkListNode
	GprsSgsnDownlinkPdu(ByteVector a, uint32_t wTlli, uint32_t wAliasTlli, std::string descr) :
		SgsnDownlinkMsg(a,descr), mTlli(wTlli), mAliasTlli(wAliasTlli)
		{}
};

struct UmtsSgsnDownlinkPdu : public SgsnDownlinkMsg
{
	int mRbid;			// For UMTS, the rbid (if 3..4) or NSAPI if (5..15).
	UmtsSgsnDownlinkPdu(ByteVector a, int wRbid, std::string descr) :
		SgsnDownlinkMsg(a,descr), mRbid(wRbid)
		{}
};

// This class is inherited by the MSInfo struct in GPRS and the UEInfo struct in UMTS
// to provide the necessary Sgsn linkage.
// The SgsnInfo class has the L3 information for the MS/UE and corresponds
// one-to-one with MSInfo or UEInfo classes (and their corresponding inherited MSUEAdapter)
// but the creation and destruction of SgsnInfo and UE or MSInfo are decoupled:
// the SgsnInfo is manufactured upon need (GPRS Attach Request) from the MSInfo or UEInfo,
// and either struct can be deleted without informing the other.
// They are identified by TLLI (in GPRS) or U-RNTI (in UMTS) which is the same in both.
// The MSInfo is very ephemeral, and is normally destroyed shortly after the
// MS ceases communication (sending TBFs); it could be as soon as the MS has certainly
// dropped back to GPRS Packet-Idle mode, because the MS will call back in if necessary
// to create a new MSInfo structure, although we keep it around a little longer than that.
// However, if the MS calls back in it will do so with the same TLLI,
// which will correspond to the SgsnInfo it used previously.
// According to the spec, the GPRS registration information, which is saved in GmmInfo
// should last about an hour.
class MSUEAdapter {
	// Methods that begin with 'sgsn' are implemented by the SGSN side.
	// Methods that begin with 'ms' are implemented by the UMTS RRC or GPRS L2 side.

	public:
	// The rbid is used only by UMTS.
	void sgsnWriteLowSide(ByteVector &payload,uint32_t handle, unsigned rbid=0);
	GmmState::state sgsnGetRegistrationState(uint32_t mshandle);
	void sgsnPrint(uint32_t mshandle, int options, std::ostream &os);
	void sgsnFreePdpAll(uint32_t mshandle);

	//MSUEAdapter() {}
	//virtual ~MSUEAdapter() {}
	virtual std::string msid() const = 0;	// A human readable name for the MS or UE.

#if RN_UMTS
	// This is the old interface to GPRS/UMTS; see saWriteHighSide()
	// This sends a pdu from the sgsn (or anywhere) to the ms.
	// For UMTS the rbid is the rbid; For GPRS the rbid is the TLLI.
	// This is the previous interface:
	virtual void msWriteHighSide(ByteVector &dlpdu, uint32_t rbidOrTlli, const char *descr) = 0;
	// This is called when the RRC SecurityModeComplete or SecurityModeFailure is received.
	void sgsnHandleSecurityModeComplete(bool success);
	// When allocating Pdp Contexts, the Ggsn allocates RABs in the middle
	// of the procedure, and waits for RRC to respond with this function:
	void sgsnHandleRabSetupResponse(unsigned RabId,bool success);
	// Deactivate all the rabs specified by rabMask.
	virtual void msDeactivateRabs(unsigned rabMask) = 0;
	// This is only externally visible for UMTS because in GPRS we have
	// to send messages through LLC first, so this call is made by LLC to the SGSN.
	void sgsnHandleL3Msg(uint32_t handle, ByteVector &msgFrame);
	// For UMTS only, get the SgsnInfo that goes with this UE.
	// Only used when called indirectly from RRC so we know the UEInfo still exists.
    SGSN::SgsnInfo *sgsnGetSgsnInfo();  // Return SgsnInfo for this UE or NULL.
#else
	// This sends a pdu from the MS/UE to the sgsn.
	void sgsnSendKeepAlive();
	int sgsnGetMultislotClass(uint32_t mshandle);
	bool sgsnGetGeranFeaturePackI(uint32_t mshandle);
#endif
	private:
	virtual uint32_t msGetHandle() = 0;	// return URNTI or TLLI
	friend std::ostream& operator<<(std::ostream&os,const SgsnInfo*);
};

// This is our information about the allocated channel passed from GPRS or UMTS back to GGSN.
// In a normal system, the whole QoS would be passed back and forth, but we
// are letting the GGSN module handle the QoS.
struct RabStatus {
	enum Status { RabIdle, RabFailure, RabPending, RabAllocated, RabDeactPending } mStatus;
	//Timeval mDeactivateTime;	// If RabFreePending, when to kill it.
	SmCauseType mFailCode;
	unsigned mRateDownlink;	// peak KByte/sec downlink of allocated channel
	unsigned mRateUplink;	// peak KByte/sec uplink of allocated channel
	RabStatus(): mStatus(RabIdle), mFailCode((SmCauseType)0) {}
	RabStatus(SmCauseType wFailCode): mStatus(RabFailure), mFailCode(wFailCode) {}
	RabStatus(Status wStatus,SmCauseType wFailCode): mStatus(wStatus), mFailCode(wFailCode) {}
	//void scheduleDeactivation() {
	//	mStatus = RabDeactPending;
	//	mDeactivationTime.future(gConfig.getNum("UMTS.Rab.DeactivationDelay",5000));
	//}
	void text(std::ostream &os) const;
};

// This class is just a container to hold the callbacks from the SGSN to the external world.
// No objects of this class are created.
struct SgsnAdapter {
	// Find the MS by TLLI or UE by U-RNTI.
	// Return NULL if it no longer exists.
	static MSUEAdapter *findMs(uint32_t);
	static bool isUmts();
	// Send a PDU to GPRS/UMTS [not implemented for UMTS yet.]
	static void saWriteHighSide(GprsSgsnDownlinkPdu *dlpdu);
#if RN_UMTS
	// Return 0 on success or SmCause value on error.
	static RabStatus allocateRabForPdp(uint32_t urnti, int rbid, ByteVector &qos);
	static void startIntegrityProtection(uint32_t urnti, std::string Kcs);
#endif
};

// This is the external interface to the SGSN.
struct Sgsn {
	public:
	static bool isUmts() { return SgsnAdapter::isUmts(); }

	static bool handleGprsSuspensionRequest(uint32_t wTlli, const ByteVector &wRaId);
	static void notifyGsmActivity(const char *imsi);
//#if RN_UMTS
	// FIXME: make this work like gprs
	//static void sgsnWriteLowSide(ByteVector &payload,SgsnInfo *si, unsigned rbid);	// UMTS only
//#endif
};

};	// namespace SGSN
#endif
