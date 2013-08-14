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

/**@file Common-use GSM declarations, most from the GSM 04.xx and 05.xx series. */


#ifndef GPRSL2MAC_H
#define GPRSL2MAC_H

#include "MemoryLeak.h"
#include "GPRSRLC.h"
#include "GSML1FEC.h"
#include "GSMTDMA.h"
#include "GSMTransfer.h"	// For GSM::L2Frame
#include "Interthread.h"
//#include "GSMCommon.h"	// For ChannelType
#include "GSML3RRElements.h"	// For RequestReference
#include "TBF.h"
#include "RList.h"
#include "Utils.h"
#include <list>
namespace GPRS {
extern void mac_debug();

// (pat) About BSN (radio block sequence numbers):
// We need a period for our internal BSNs, and it is somewhat arbitrary.
// For timing, we need a cyclic period of at least 8 52-multiframes.  (GSM03.64 Figure 19)
// There are specified timeouts of up to 5 seconds over which it would be convenient
// if radio block numbers were non-repeating, which would be about 1000 frames, 250 radio blocks.
// But there is no reason not to just use the hyperframe, so we will.
// A hyperframe is 2048 * 26 * 51 frames; (2048*26*51 * 12/52) = 626688 Radio Blocks.
// A 52 multiframe takes 240ms, so each radio block averages 20ms.
// btw, if the block sequence numbers went on forever, stored in an int, it would last 497 days.
// There are 52 frames for 12 blocks.

// In GSM the downlink block numbers trail the uplink block numbers slightly.
// I observed the incoming blocks are this far behind gBSNNext.
// If you are expecting an answer at time N,
// look for it when gBSNext == N+BSNLagTime.
const int BSNLagTime = 4;

extern bool gFixTFIBug;
extern bool gFixSyncUseClock; // For debugging: Use wall time for service loop synchronization
							// instead of GSM frames.  TODO: Get rid of this.
extern int gFixIdleFrame;	// Works around this bug - see code.
extern int gFixDRX;		// Works around DRX mode bug.  The value is the number of assignments
					// that are unanswered before we assume the MS is in DRX mode.

// Set to 1 to request a poll in the ImmediateAssignment on CCCH.
// We still have to wait for the poll time anyway, so this is here
// only for debugging.
extern bool gFixIAUsePoll;
extern bool gFixConvertForeignTLLI;


typedef RList<PDCHL1FEC*> PDCHL1FECList_t;
typedef RList<MSInfo*> MSInfoList_t;

struct Stats_t {
	Statistic<double> macServiceLoopTime;
	UInt_z countPDCH;
	UInt_z countMSInfo;
	UInt_z countTBF;
	UInt_z countRach;
};
extern struct Stats_t Stats;



// For now, there is only one pool of TFIs shared by all channels.
// It should be per-ARFCN, but I expect there to only be one.
// Sharing TFIs between channels on the ARFCN makes multislot tfi allocation easy.
// There are 32 uplink and 32 downlink TFIs, which should be enough for some time to come.
// If this gets congested, can be split up into one pool of TFIs per uplink/downlink channel,
// but then you have to be careful when allocating multislot tfis that the
// tfi is unique across all channels.
// TODO: When we allocate multislot tfis, the tfi must be unique in all slots.
// The easiest way to do that is to have a single tfilist for the entire system.
class TBF;
struct TFIList {
	TBF *mTFIs[2][32];	// One list for uplink, one list for downlink.

	// 12-22-2011: It looked like the Blackberry abandoned an uplink TBF when
	// a downlink TBF was established using the same TFI.   This seems like a horrible
	// bug in the MS, but to work around it, I added the gFixTFIBug which uses
	// a single TFI space for both uplink and downlink.  This eliminated
	// a bunch of msStop calls with cause T3101, so I think this is a genuine
	// problem with MSs and we need this fix in permanently.
	// Update: The TFI is reserved during the time after a downlink ends, so the MS
	// may have been justifiably upset about seeing it reissued for an uplink too soon.
	RLCDir::type fixdir(RLCDir::type dir) {
		return gFixTFIBug ? RLCDir::Up : dir;
	}

	TFIList();

	TBF *getTFITBF(RLCDirType dir, int tfi) { return mTFIs[fixdir(dir)][tfi]; }
	void setTFITBF(int tfi,RLCDirType dir,TBF *tbf) { mTFIs[fixdir(dir)][tfi] = tbf; }
	int findFreeTFI(RLCDirType dir);
	void tfiDump(std::ostream&os);
};
extern struct TFIList *gTFIs;
#if MAC_IMPLEMENTATION
TFIList::TFIList() { for (int i=0;i<32;i++) { mTFIs[0][i] = mTFIs[1][i] = NULL; } }
int TFIList::findFreeTFI(RLCDirType dir)
{
	static int lasttfi = 0;
	dir = fixdir(dir);
	// TODO: After the TBF the tfi may only be reused by the same MS for some
	// period of time.  See "TBF release" section.
	// Temporary work around is to round-robin the tfis.
	for (int i = 0; i < 32; i++) {
		lasttfi++;
		if (lasttfi == 32) { lasttfi = 0; }
		if (!mTFIs[dir][lasttfi]) { return lasttfi; }
	}
#if 0
	// DEBUG: start at 1 instead of 0
	for (int tfi = 1; tfi < 32; tfi++) {
		// DEBUG: try incrementing tfi to avoid duplication errors:
		if (tfi == lasttfi) { continue; }
		if (!mTFIs[dir][tfi]) {
			lasttfi = tfi;
			return tfi;
		}
	}
#endif
	return -1;
}

void TFIList::tfiDump(std::ostream&os)
{
	int dir, tfi;
	for (dir = 0; dir <= (gFixTFIBug ? 0 : 1); dir++) {
		os << "TFI=(";
		if (!gFixTFIBug) {
			os << RLCDir::name(dir) << ":";
		}
		for (tfi = 0; tfi < 32; tfi++) {
			TBF *tbf = mTFIs[dir][tfi];
			if (tbf) { os << " " << tfi << "=>" << tbf; }
		}
	}
	os << ")\n";
}
#endif

// The USF associates radio blocks with an MS.
// It is placed in the downlink block header to indicate that the next uplink block
// is allocated to the MS assigned that USF.
// There may be multiple simultaneous uplink TBFs from the same MS; all will use the same USF.
// The MS does that if a higher priority or throughput TBF comes in while one is in progress.
// The USF is only 3 bits, and 0x7 is reserved (to indicate PRACH) on PCCCH channels.
// We are not using PCCCH, but I am going to avoid 0x7 anyway.
// Additionally, for RACH initiated single block uplink assignments, we need a USF
// that is not in use by any MS, for which we will reserve USF=0.
// so really only 6 USFs (1-6) are available per uplink channel, which should be plenty.
// Note that there are alot more TFIs than USFs, because TFIs are per-TBF,
// while USFs are per-MS.
// The USFList information applies to an uplink channel, but it is used primarily by
// the downlink channel to set the USF in the MAC header of each downlink block.
// Note that there is no pointer to the TBFs (could be multiple ones) from this USF struct,
// because arriving uplink blocks are associated with their TBFs using the TFI, not the USF.
// The USFs are numbered 0..7.
const int USFMIN = 1;
const int USFMAX = 6;
const int USFTotal = 8;
#define USF_VALID(usf) ((usf) >= USFMIN && (usf) <= USFMAX)
class USFList
{
	//PDCHL1FEC *mlchan;			// The channel these USFs are being used on.
	// We use the usf as the index, so mlUSFs[0] is unused.
	struct UsfInfo {
		MSInfo *muMS; 				// The MS assigned this USF on this channel.
		GprsTimer muDeadTime;		// When a TBF dies reserve the USF for this ms until this expires.
	};
	UsfInfo mlUSFs[USFTotal]; 		// Some slots in this array are unused.

	//int mRandomUSF;			// Used to pick one of the USFs.

	// When the channel is running, we save the usf that is sent on each downlink block,
	// so that we can correlate the uplink responses independently of their content.
	// We save them in an array indexed by bsn, length only has to cover the difference
	// between uplink and downlink BSNs plus some slop, so 32 is way overkill.
	unsigned char sRememberUsf[32];	// The usf transmitted in the downlink block.
	unsigned sRememberUsfBsn[32];

	public:
	USFList();

	// Return the usf that was specified on the downlink burst, given the bsn from the uplink burst.
	int getUsf(unsigned upbsn);

	// Remember the usf transmitted on specified downlink burst.  OK to pass 0 for usf.
	void setUsf(unsigned downusf, unsigned downbsn);		// Save usf for current downlink burst.

	// Which MS is using this USF?
	MSInfo *getUSFMS(int usf);

	int allocateUSF(MSInfo *ms);
	int freeUSF(MSInfo *ms,bool wReserve);
	//int getRandomUSF();
	void usfDump(std::ostream&os);
};

struct RachInfo
{
	unsigned mRA;
	const GSM::Time mWhen;
	RadData mRadData;

	// Gotta love this language.
	RachInfo(unsigned wRA, const GSM::Time &wWhen, RadData wRD)
		: mRA(wRA), mWhen(wWhen), mRadData(wRD)
		{ RN_MEMCHKNEW(RachInfo) }
	~RachInfo() { RN_MEMCHKDEL(RachInfo) }
	void serviceRach();
};


// There is only one of these.
// It holds the lists used to find all the other stuff.
class L2MAC
{
	Thread macThread;
	// The entire MAC runs in a single thread.
	// This Mutex is used at startup to make sure we only start one.
	// Also used to lock the serviceloop so the CLI does not modify MS or TBF lists simultaneously.
	public:
	mutable Mutex macLock;

	// The RACH bursts come in unsychronized to the rest of the GPRS code.
	// The primary purpose of this queue is just to allow the MAC service loop
	// to handle the RACH in its single thread by saving the RACH until the MAC service
	// loop gets around to it.  If GPRS is running, we dont really expect multiple
	// simultaneous RACHs to queue up here because we service the RACH queue on each loop.
	// However, if a RACH comes in while GPRS service is stopped and all channels
	// are in use for RR connections, the as-yet-unserviced RACHs may queue up here.
	// When GPRS service resumes, we should disard RACHs that are too old.
	// Note that there could be multiple RACH for the same MS, a case we cannot detect.
	InterthreadQueue<RachInfo> macRachQ;

	// We are doing a linear search through these lists, but there should only be a few of them.
	PDCHL1FECList_t macPDCHs;	// channels assigned to GPRS.
	PDCHL1FECList_t macPacchs;	// The subset of macPDCHs that we assign as PACCH.
	//Mutex macTbfListLock;
	TBFList_t macTBFs;	// active TBFs.
	MSInfoList_t macMSs;	// The MS we know about.

	// For debugging, we keep expired TBF and MS around for post-mortem examination:
	TBFList_t macExpiredTBFs;
	MSInfoList_t macExpiredMSs;

#define RN_MAC_FOR_ALL_PDCH(ch) RN_FOR_ALL(PDCHL1FECList_t,gL2MAC.macPDCHs,ch)
#define RN_MAC_FOR_ALL_PACCH(ch) RN_FOR_ALL(PDCHL1FECList_t,gL2MAC.macPacchs,ch)
#define RN_MAC_FOR_ALL_MS(ms) for (RListIterator<MSInfo*> itr(gL2MAC.macMSs); itr.next(ms); )
#define RN_MAC_FOR_ALL_TBF(tbf) for (RListIterator<TBF*> itr(gL2MAC.macTBFs); itr.next(tbf); ) 

	L2MAC()
	{
		gTFIs = new TFIList();
	}
	~L2MAC() { delete gTFIs; }

	public:
	unsigned macN3101Max;
	unsigned macN3103Max;
	unsigned macN3105Max;
	unsigned macT3169Value;
	unsigned macT3191Value;
	unsigned macT3193Value;
	unsigned macT3168Value;
	unsigned macT3195Value;
	unsigned macMSIdleMax;
	unsigned macChIdleMax;	
	unsigned macChCongestionMax;
	unsigned macDownlinkPersist;
	unsigned macDownlinkKeepAlive;
	unsigned macUplinkPersist;
	unsigned macUplinkKeepAlive;
	float macChCongestionThreshold;
	Float_z macDownlinkUtilization;

	Bool_z macRunning;		// The macServiceLoop is running.
	time_t macStartTime;
	Bool_z macStopFlag;		// Set this to terminate the service thread.
	Bool_z macSingleStepMode;	// For debugging.

	MSInfo *macFindMSByTlli(uint32_t tlli, int create = 0);
	void macAddMS(MSInfo *ms);
	void macForgetMS(MSInfo *ms,bool forever);

	// When deleting tbfs, macForgetTBF could be called on a tbf already removed
	// from the list, which is ok.
	void macAddTBF(TBF *tbf);
	void macForgetTBF(TBF *tbf,bool forever);

	void macServiceLoop();
	PDCHL1FEC *macPickChannel();	// pick the least busy channel;
	PDCHL1FEC *macFindChannel(unsigned arfcn, unsigned tn);	// find specified channel, or null
	unsigned macFindChannels(unsigned arfcn);
	bool macAddChannel();		// Add a GSM RR channel to GPRS use.
	bool macFreeChannel();		// Restore a GPRS channel back to GSM RR use.
	void macForgetCh(PDCHL1FEC*ch);
	void macConfigInit();
	bool macStart();	// Fire it up.
	void macStop(bool channelstoo);		// Try to kill it.
	int macActiveChannels();		// Is GPRS running, ie, are there channels allocated?
	int macActiveChannelsC(unsigned cn);		// Number of channels on specified 0-based ARFCN
	float macComputeUtilization();
	void macCheckChannels();
	void macServiceRachQ();
};
extern L2MAC gL2MAC;

//const int TFIInvalid = -1;
//const int TFIUnknown = -2;

// The master clock is not exactly synced up with the radio clock.
// It is corrected at intervals.  This means there is a variable delay
// between the time we send a message to the MS and when we can expect an answer.
// It does not affect RRBP reservations, which are relative, but it affects
// L3 messages sent on CCCH, which must specify an exact time for the response.
// I'm not sure what to do about this.
// I am just adding some ExtraDelay, and if reservations are unanswered,
// increasing this value until they start getting answered.
// We could probably set this back to 0 when we observe the time() run backwards,
// which means the clock is synced back up.
extern int ExtraClockDelay;	// in blocks.

// This specifies a Radio Block reservation in an uplink channel.
// Reservations are used for:
//	o response to CCCH Immediate Assignment One BLock initiated by MS on RACH.
//		In this case we dont even know what MS the message was coming from, so if
//		it does not arrive, nothing we can do but wait for the MS to try again later.
//  o For an Uplink TBF: The RLCUpEngine sends AckNack every N blocks.
//		We could require a response, but I dont think we will unless it gets stalled.
//	o For a stalled Uplink TBF: The RLCUpEngine sends an AckNack with an RRBP
//		reservation.  The MS may respond with any type of message.
//		If that response does not arrive, the RLCUpEngine network immediately sends
//		another AckNack.  Serviced when Uplink serviced.
//	o For a completed Uplink TBF: network sends a final acknack with RRBP reservation,
//		which must be repeated until received.
//		Could be handled by the engine or MsgTransaction.
//	o For a Downlink TBF: send an RRBP reservation every N blocks, which we expect
//		the MS to use to send us an AckNack, or some other message.  If we dont get
//		a response, send another RRBP reservation immediately.
//	o For a stalled Downlink TBF: resend the oldest block with another RRBP reservation.
//	Note: a completed Downlink TBF can be destroyed immediately, since we received
//	the final ack nack.
// The following are handled by the MsgTransaction class:
//  o response to Packet Polling Request message.
//		If the message does not arrive, we may want to try again.
//	o RRBP responses in downlink TBF data blocks or control blocks.
//		If these dont arrive from the MS, it doesnt matter.
//		The response type is actually unknown: it could be a Packet Downlink Ack/Nack,
//		or anything else the MS wants to send.  The uplink message will have all the required
//		info so we dont have to save anything in the RLCBlockReservation;
//	o Packet Control Acknowledgement responses.  Could come from:
//		- Packet uplink/downlink assignment message, which may require network to resend.
//		- Packet TBF release message, which requires network to resend.

//  o WRONG: For an Uplink TBF: The RLCUpEngine sends AckNack after N blocks and provides an RRBP
//		uplink response.  The MS may respond with any type of message.
//		If that response does not arrive, the RLCUpEngine network immediately sends
//		another AckNack.
struct RLCBlockReservation : public Text2Str {
	enum type {
		None,
		ForRACH,
		ForPoll, // For the poll response to a downlink immediate assignment when
				// the MS is in packet idle mode.  see sendAssignment()
		ForRRBP	// This has many subtypes of type MsgTransactionType
	};
	type mrType;		// Primary type
	MsgTransactionType mrSubType;	// Subtype, only valid if mrType is ForRRBP
	RLCBSN_t mrBSN;		// The block number that has been reserved.
	TBF *mrTBF;			// tbf if applicable.  (Not known for MS initiated RACH.)
	// TODO: Is it stupid to save mrRadData?  We will get new data when the MS responds.
	RadData mrRadData;		// Saved from a RACH to be put in MS when it responds.
	static const char *name(RLCBlockReservation::type type);
	void text(std::ostream&) const;
};
std::ostream& operator<<(std::ostream& os, RLCBlockReservation::type &type);
std::ostream& operator<<(std::ostream& os, RLCBlockReservation &res);

#if MAC_IMPLEMENTATION
const char *RLCBlockReservation::name(RLCBlockReservation::type mode)
{
	switch (mode) {
		CASENAME(None)
		CASENAME(ForRACH)
		CASENAME(ForPoll)
		CASENAME(ForRRBP)
		default: return "unrecognized";	// Not reached, but makes gcc happy.
	}
}
std::ostream& operator<<(std::ostream& os, RLCBlockReservation::type &type)
{
	os << RLCBlockReservation::name(type);
	return os;
}
void RLCBlockReservation::text(std::ostream &os) const
{
	os << "res=(";
	os << LOGVAR2("bsn",mrBSN) << " " << name(mrType);
	if (mrTBF) { os << " " << mrTBF; }
	os <<")";
}
std::ostream& operator<<(std::ostream& os, RLCBlockReservation &res)
{
	res.text(os);
	return os;
}
#endif

// The uplink reservation system.
// It is used by both uplink and downlink parts.
// I put it in its own class to avoid clutter elsewhere.
// Note that reservations are kept around after the current time passes,
// so that uplink acknowledgements can be paired with the message to which they belong.
//
// In order to efficiently utilize the uplink resource, we need to make reservations
// of future uplink RLCBlocks for various purposes.
// There is a problem in that the RRBP field is limited to identifying
// blocks 3-6 blocks in the future.  The obvious solution would be to reserve
// them first, but that will not work because the control messages occur asynchronously
// with respect to the data streams and (should) have higher priority.
// My K.I.S.S. system to efficiently use these resources is to reserve even numbered
// uplink blocks for RRBP responses, which are typically Ack/Nack blocks,
// but could actually be any type of block, and to reserve odd numbered uplink blocks
// for all other control blocks, which include Single-Block accesses initiated
// by RACH (in which case, there is no TFI or TLLI) and all other control block
// responses to network initiated messages.
// A minimum sized downlink response is 2 blocks long, so this method tends to fully
// utilize the channel and still limit latency (as opposed to alternative schemes
// that would assign fixed slots for various messages, or lump all the blocks together
// which would mean that RRBP responses would sometimes not have a reservation available.)
// Note that a single-block downlink resend as a result of an Ack/Nack message with
// a single Nack may be only one block long, so it is still possible to run
// out of odd numbered uplink blocks for RRBP responses.
// When downlink blocks are finally queued for transmission, any unreserved uplink
// blocks are utilized for current uplink data transfers using dynamic allocation using USF.
// Using this method, the reservations are also monotonically increasing in each
// domain (RRBP and non-RRBP) which makes it easy.
class L1UplinkReservation
{
	private:
	// TODO: mLock no longer needed because RACH processing synchronous now.
	Mutex mLock;	// The reservation controller can be called from GSM threads, so protect it.

	public:
	// There should only be a few reservations at a time, probably limited to
	// around one per actively attached MS.
	// Update 8-8-2012: well, the MS can be in two sub-modes of transmit mode simultaneously,
	// specifically, persistent keep-alive and reassignment, so I am upgrading this
	// with the sub-type.
	// There are timeouts of up to 5 seconds (250 blocks), so we should keep history that long.
	// The maximum reservation in advance is probably from AGCH, which can stack up
	// waiting for CCCH downlink spots up to a maximum (defined by a config option)
	// in AccessGrantResponder(), but currently 1.5 seconds.
	// There is no penalty for making this array larger, so just go ahead and overkill it.
	static const int mReservationSize = (2*500);
	RLCBlockReservation mReservations[mReservationSize];

	L1UplinkReservation();

	private:
	RLCBSN_t makeReservationInt(RLCBlockReservation::type restype, RLCBSN_t afterBSN,
		TBF *tbf, RadData *rd, int *prrbp, MsgTransactionType mttype);

	public:
	RLCBSN_t makeCCCHReservation(GSM::CCCHLogicalChannel *AGCH,
		RLCBlockReservation::type type, TBF *tbf, RadData *rd, bool forDRX, MsgTransactionType mttype);
	RLCBSN_t makeRRBPReservation(TBF *tbf, int *prrbp, MsgTransactionType ttype);

	// Get the reservation for the specified block timeslot.
	// Return true if found, and return TFI in *TFI.
	// bsn can be in the past or future.
	RLCBlockReservation *getReservation(RLCBSN_t bsn);
	
	void clearReservation(RLCBSN_t bsn, TBF *tbf);
	RLCBlockReservation::type recvReservation(RLCBSN_t bsn, TBF**restbf, RadData *prd,PDCHL1FEC *ch);
	void dumpReservations(std::ostream&os);
};

extern bool setMACFields(MACDownlinkHeader *block, PDCHL1FEC *pdch, TBF *tbf, int makeres,MsgTransactionType mttype,unsigned *pcounter);
extern int configGetNumQ(const char *name, int defaultvalue);
extern int configGprsMultislotMaxUplink();
extern int configGprsMultislotMaxDownlink();

// The USF is assigned in each downlink block to indicate if the uplink
// block in the same frame position is available for uplink data,
// or reserved for some other purpose.
// There are only 7 USFs available, so we have to share.
// TODO: MOVE TO UPLINK RESERVATION:
//class L1USFTable
//{
//	TBF *mUSFTable[8];
//	public:
//	void setUSF(RLCBSN_t bsn, TBF* tbf) { mUSFTable[bsn % mUSFTableSize] = tbf; }
//	TBF *getTBFByUSF(RLCBSN_t bsn) { return mUSFTable[bsn % mUSFTableSize]; }
//
//	L1USFTable() { for (int i = mUSFTableSize-1; i>= 0; i--) { mUSFTable[i] = 0; } }
//};

};	// namespace GPRS

#endif
