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
#ifndef MSINFO_H
#define MSINFO_H

#include <Interthread.h>
//#include <list>

#include "GPRSInternal.h"
#include "GPRSRLC.h"
//#include "RLCHdr.h"
#include "RList.h"
//#include "BSSG.h"
#include "Utils.h"
#include "SgsnExport.h"

#define CASENAME(x) case x: return #x;

namespace GPRS {
struct RadData;

typedef RList<PDCHL1Downlink*> PDCHL1DownlinkList_t;
typedef RList<PDCHL1Uplink*> PDCHL1UplinkList_t;
typedef RList<TBF*> TBFList_t;

// A way to describe a collection of tbf states.
// See TBF.h isActive() and isTransmitting().
enum TbfMacroState {
	TbfMAny,		// any state
	TbfMActive,		// any active state
	TbfMTransmitting	// any transmitting state.
};

// This should be in TBF.h but classes TBF and MSInfo are circularly referential.
enum TbfCancelMode {
	TbfRetryInapplicable,	// Tbf retry is inapplicable to an uplink tbf.
	TbfNoRetry,				// Kill the tbf forever
	TbfRetryAfterRelease,	// Retry tbf after sending a TbfRelease message.
							// If the tbf release message fails, fall back to RetryAfterTimeout.
	TbfRetryAfterWait		// Retry tbf after timeout.
	};

enum MultislotSymmetry {
	MultislotSymmetric,	// Use only symmetric multislot assignment, eg 2-down/2-up.
	MultislotFull		// Use full multislot assignment, which may be assymetric.
};

// GSM03.64 sec 6.2
// Note: The RROperatingMode is a state of the MS and known only at the layer2 MAC level.
// The GSM spec says essentially that the RROperatingMode is simply whether
// the MS thinks there is an active TBF running, which is not particularly useful to us.
// What we need to track is whether the MS is listening to
// CCCH (PacketIdle mode) or PDCH (PacketTransfer mode.)  Generally when all TBFs are
// finished the MS goes back to PacketIdle mode, but after a downlink transaction
// we keep it camped on PDCH for a time determined by the T3192 timer, whose value
// we broadcast in the System Information messages.
//
// Note: For T3192 timeout determination, dont forget to add the time delay
// of the outgoing message queue, but that should be short.
// But messages enqueued on CCCH may have long delays.
// 
// This mode has almost nothing to do with Mobility Management Status, which is up in Layer3.
// When the docs talk about "GPRS attached" they usually mean Mobility Mangement State, not this.
// An MS in GMM state "Ready" (meaning the MS and SGSN have negotiated an SGSN supplied
// TLLI for the MS instead of using the random TLLI the MS uses for its first uplink message)
// will usually be in PacketIdle mode unless there is an ongoing TBF transaction.
// For a non dual-transfer-mode MS, the MS must relinquish (or suspend) its GMM "Ready" state
// to make a voice call, in which case it would no longer be in PacketIdle mode,
// but that has almost no meaning at the MAC level.  If the voice call ends and the MS wants
// to use GPRS again, it will send us another RACH and we dont need to know that
// a voice call was made in the meanwhile.
class RROperatingMode {
	public:
	enum type {
		PacketIdle,
		PacketTransfer,
		//DualTransfer,
		// This mode was unnecessary:
		//Camped		// This is our own mode, not an official RROperatingMode.
				// It marks the time MS is camped on Packet Channel between PacketTransfer
				// and PacketIdle, when the T3192 timer is running.
				// If a new transfer does not commence before T3192 expires,
				// MS is in PacketIdle mode.
	};
	static const char *name(RROperatingMode::type mode);
};

std::ostream& operator<<(std::ostream& os, const RROperatingMode::type &mode);

// When we lose contact with the MS or something bad happens, we stop talking to it.
// This says why.  Reserve the first 100 numbers so the MsgTransactionType can be used
// as a stop cause when the corresponding MsgTransactionType counter expires.
class MSStopCause {
	public:
	enum type {
		AssignCounter = 1,
		// These were the values in release 3.0:
		//ShutDown = 2,
		//Stuck = 3,
		//Reassign = 4,	// Reassignment failed.
		ShutDown = 102,
		Stuck = 103,
		//Reassign = 104,	// Reassignment failed.
		Rach = 105,		// Running TBF killed by a RACH.
		ReleaseCounter = 106,
		ReassignCounter = 107,
		NonResponsive = 108,	// MS does not talk to us any more.
		Goof = 109,
		N3101 = 3101,
		N3103 = 3103,
		N3105 = 3105,
		T3191 = 3191,
		T3168 = 3168,
		CauseUnknown = 9999 	// Used for unrecoverable internal inconsistency.
	};
	//int mValue;
	//MSStopCause(/*enum MsgTransactionType*/ int wMsgTransType) : mValue(wMsgTransType) {}
};


struct SignalQuality {
	// TODO: Get the Channel Quality Report from packet downlink ack/nack GSM04.60 11.2.6
	Statistic<float> msTimingError;
	Statistic<int> msRSSI;		// Dont bother saving RSSI as a float
	Statistic<int> msChannelCoding;
	Statistic<int> msCValue;
	Statistic<int> msILevel;
	Statistic<int> msRXQual;
	Statistic<int> msSigVar;
	void setRadData(RadData &rd);
	void setRadData(float wRSSI,float wTimingError);
	void dumpSignalQuality(std::ostream &os) const;
};

struct StatTotalHits {
	// Use ints instead of unsigned in case some statistic is buggy and runs backwards.
	Int_z mTotal;
	Int_z mGood;
	void clear() { mGood = mTotal = 0; }
};

// keep the history of some success rate over the last few seconds for reporting purposes.
class StatHits {
	// Keep totals for each of the last NumHist 48-block-multiframes, which is approx one second.
	public: static const int cNumHist = 20;
	private:
	StatTotalHits mTotal; 				// Totals for all time.
	StatTotalHits mHistory[cNumHist];	// Recent history.
	UInt_z mWhen[cNumHist];	// When the historical data in this history bucket was collected.
	// Return index in arrays above, which is the current 48-block-multiframe
	unsigned histind();

	public:
	void addTotal() {
		mTotal.mTotal++;
		mHistory[histind()].mTotal++;
	}
	void addGood() {	// increment only good count, not total.
		mTotal.mGood++;
		mHistory[histind()].mGood++;
	}
	void addMiss() { addTotal(); }
	void addHit() {		// increment good and total.
		unsigned i = histind();
		mTotal.mTotal++; mTotal.mGood++;
		mHistory[i].mTotal++; mHistory[i].mGood++;
	}

	void getStats(float *pER, int *pTotal, float *pWorstER, int *pWorstTotal);
	void textRecent(std::ostream &os); 	// Print the average for the last N seconds and worst second.
	void textTotal(std::ostream&os);	// Print totals.
};

// More MS statistics.  Separate from MSInfo just because MSInfo is so large.
struct MSStat {
	// This is a measure of the instantaneous traffic, used to pick the least busy channel.
	// It is incremented every time a block is sent/received, and decayed on a regular time schedule.
	UInt_z msTrafficMetric;

	StatHits msCountCcchReservations;
	StatHits msCountRbbpReservations;

	StatHits msCountBlocks;	// Counts both uplink and downlink.

	UInt_z msConnectTime;
	void msAddConnectTime(unsigned msecs) { msConnectTime += msecs; }
	UInt_z msCountTbfs, msCountTbfFail, msCountTbfNoConnect;
	UInt_z msBytesUp, msBytesDown;


	//UInt_z msCountCcchReservations;
	//UInt_z msCountCcchReservationReplies;
	//UInt_z msCountRbbpReservations;
	//UInt_z msCountRbbpReservationReplies;
	//void service() {
	//	// There are approx 48 blocks per second.
	//	if (gBSNNext % 48 != 0) { return; }
	//	unsigned ind = (gBSNNext / 48) % numHist;
	//	msHistoryCcchReservations.sethits(int,msCountCcchReservations,msCountCcchReplies);
	//	msHistoryRbbpReservations.sethits(int,msCountRbbpReservations,msCountRbbpReplies);
	//	fivesecavg.countCcchReservations = msCountCcchReservations.
	//}

	// We use talkUp/talkDown to determine when the MS is non-responsive:
	GprsTimer msTalkUpTime;		// When the MS last talked to us.
	GprsTimer msTalkDownTime;	// When we last talked to the MS.

	// Called at every uplink/downlink communication from MS.
	// These are not strictly statistics because they are also used to kill a non-responsive MS.
	// These timers differ from the persistent mode timers in that those
	// count only data, and these count anything.
	void talkedUp(bool doubleCount=false) { msTalkUpTime.setNow(); if (!doubleCount) {msTrafficMetric++;} }
	void talkedDown() { msTalkDownTime.setNow(); msTrafficMetric++; }

	// Dump all except traffic metric.
	void msStatDump(const char *indent,std::ostream &os);
};

// MS Info a.k.a. Radio Context.  There is one of these for each TLLI (not per-MS, per-TLLI)
// GSM04.08 4.7.1.4 talks about GPRS attach, P-TMSI, and TLLI.
// An MS, for our purposes in L2, is defined by its TLLI.  No TLLI, no MSInfo struct.
// The TLLI identifies the MS in all transactions except the initial RACH.
// The RACH creates an anonymous packet uplink assignment for the MS, still identified
// only by the RACH time, to transmit one block, which will be a Packet Resource Request
// containing the all important TLLI.  However, knowing the TLLI does not identify
// a unique MS; the MS may (and usually does) use several of them.
// Note that the MS can pick a "random" TLLI for itself when it does its first
// GPRS-attach, but the SGSN issues a new "local" TLLI based on P-TMSI on AttachAccept.
// The TLLI is specific to the sgsn, so if you switched sgsns, you would have a new TLLI,
// however, the MS remembers its old TLLIs and will try calling in with them,
// converted to foreign TLLIs.
// 
// The spec is entirely botched up about whether the critical information needed
// to communicate with the MS is in L3 (the SGSN) or L2 (the BTS.)
// The SGSN stores the MS capabilities (RACap and DRX mode), which is ok but unnecessary,
// since the MS retransmits them in every RAUpdate.
// (The SGSN copies would be forwarded to the new cell during a cell change though.)
// The problem is that we need to remember the radio parameters for the MS
// (RSSI and TimingError), and a whole bunch of ad-hoc timers running in the MS
// here in L2, where we identify MS using TLLI,
// but the mapping of TLLI to MS (as known by IMSI) is in L3.  Whoops!
// Also note that during a TLLI reassignment procedure using BSSG, the SGSN commands
// the BTS to switch TLLIs *after* it has received the Attach Complete Message,
// which has already arrived [possibly but not always] using the new TLLI,
// so here at L2 the MSInfo for the new TLLI already exists.
// We are supposed to combine the two RadioContexts (MSInfos) when we get
// the TLLI reassignment, and then recognize either TLLI.
// The RSSI and TimingError are updated separately per-TLLI (ie, per-MSInfo struct)
// because the MS initiates each conversation.
//
// The entire communication system between the MS and the SGSN can best
// be described in terms of two different state-universes, corresponding
// to the GPRS-Registration state: Registered (GPRS-Attached) or not.
// In the pre-gprs-attach state the MS may call in with several TLLIs,
// and we dont know how to correlate them to an actual MS.
// In this case it is quite easy to lose communication with the MS, because
// when an incoming RACH+PacketResourceRequest is answered, a new PACCH
// may be assigned at random, and it may conflict with a previous assignment
// that is on-going or in-flight on AGCH.
// Also in this state I think there is simply no way to know for sure the state of the
// per-MS timers, which are needed to know how to send the Immediate Assignment.
// TODO: Maybe we should use a single PACCH timeslot for all unregistered TLLIs,
// which implies communicationg the registration state from the SGSN to L2.

// In the Registered-state we know that the new and old TLLI are the same physical MS,
// and communication is more secure.  We can assign a new PACCH for the MS.
// By Registered we mean that both the SGSN and the MS agree on the
// Registration state and the P-TMSI, which agreement is handshaked in both
// directions (3 messages) and consumated by the AttachComplete message sent by MS to SGSN.

#define TLLI_LOCAL_BIT 0x40000000
#define TLLI_MASK_LOCAL(tlli) ((tlli) & ~ TLLI_LOCAL_BIT)
static __inline__ uint32_t tlliEq(uint32_t tlli1, uint32_t tlli2) {
	// Temporarily provide a way to disable this in case it does not work:
	if (gConfig.getBool("GPRS.LocalTLLI.Enable")) {
		return TLLI_MASK_LOCAL(tlli1) == TLLI_MASK_LOCAL(tlli2);
	} else {
		return tlli1 == tlli2;
	}
}


// vvv OLD COMMENT:
// Formerly I changed the TLLI in the MSInfo struct to an oldTLLI on the command of the SGSN,
// but that is incorrect - if the MS later sends another RACH using the oldTLLI,
// we need to respond with that oldTLLI, not the newTLLI, although possibly we
// should just ignore it in that case.
// The RACH creates an anonymous packet uplink assignment for the MS, still identified
// only by the RACH time, to transmit one block, which will be a Packet Resource Request
// with a TLLI.  If that TLLI maps to an old TLLI, it is because we have already
// succeeded with the Attach Complete, and therefore it is safe to use the new TLLI.
// Therefore, an MSInfo structure has one and only one TLLI, and the sole purpose of
// TLLI is as a layer-2 transport identifier for the MS.
// ^^^ OLD COMMENT

// The MSInfo struct needs to hang around as long as the MS is in packet-transfer mode,
// which means as long as it has TBFs, or the MS is in the T3192 period when it is camped
// on the PACCH channel instead of the CCCH channel.
// If we lose a connection with an MS, we keep the dead TBF around too until we
// are sure it is no longer in use (config option, default 5 seconds),
// so we dont need the MSInfo to survive after that.  Doesn't hurt to keep it around, either.
// An unused MSInfo eventually decays and is destroyed; this delay must be longer
// than the expected use of the MSInfo by the SGSN - at least several seconds.
// The SGSN is what remembers GPRS-attached MS, and will send us both a TLLI and the
// MS capabilities (ie, multislot) in any transactions so that we can recreate the MSInfo at need.
class MSInfo : public SGSN::MSUEAdapter, public SignalQuality, public MSStat
{
	public:
	unsigned msDebugId;

	// Use of TLLI is in GSM04.08 4.7.1.5: P-TMSI handling.
	// From GSM03.03 sec 2.6: Structure of TLLI:
	// local TLLI is built by MS that has a valid P-TMSI:
	//		top 2 bits 11, lower 30 bits are low 30 bits of P-TMSI.
	// foreign TLLI is built by an MS that has a valid P-TMSI from elswhere:
	//		top 2 bits 10, lower 30 bits from P-TMSI;
	// random TLLI is built by an MS with no P-TMSI:
	//		top top 5 bits 01111, lower 27 bits random.
	// auxiliary TLLI is built by SGSN:
	//		top 5 bits 01110, lower 27 bits at SGSN discretion.
	// GSM03.03 sec 2.4: Structure of TMSI 
	// 	The 32-bit TMSI has only local significance (within VLR or SGSN) and
	// 	is created at manufacturer discretion, however, for SGSN top
	//	2 bits must be 11, and it may not be all 1s, which value is reserved
	//	to mean invalid.  We also reserve value 0 to mean unset.
	//	TMSI is stored in hex notation in 4 octets, and always ciphered.
	// GPRS-L2 doesn't care about any of the above, the SGSN knows those things.
	// In GPRS-L2 we just use whatever TLLI we are told.

	// An MSInfo structure is created as a result of an MS communication with a TLLI.
	// No TLLI, no MSInfo structure.  These things can time out and die whenever
	// they want - their lifetime only needs to be as long as the RSSI and TimingError is valid.
	// They will be recreated if the MS RAChes us again.
	// In the spec they can also be created by a page from the SGSN, but not implemented here.
	// The MSInfo struct corresponds to an SgsnInfo in the SGSN, but because
	// either can be deleted any time we dont keep pointers between them, we always
	// look them up by TLLI for communication to/from the SGSN.
	//
	// This is as per the spec:
	// The msTlli is the TLLI we (the L2 layer) always use to communicate with the MS.
	// It is initialzied from the TLLI the MS used to communicate with us.
	// It is only changed if we get a 'change tlli' command from the SGSN,
	// which happens only after a successfully completed attach procedure,
	// (which is a fully acknowledged procedure with 3 way handshake)
	// at which time msTlli becomes msOldTlli, and we must subsequently recognize
	// either msTlli (the new sgsn-assigned one) or msOldTlli (the original one)
	// for uplink communication, but use only msTlli for downlink communication.
	// 
	// This is not as per the spec:
	// After the attach is successful, we use only the assigned TLLI,
	// only one MSInfo structure, and everything is copascetic.
	// However, prior to a successful attach, I have seen the MS just use
	// several different TLLIs in uplink messages one right after the other,
	// maybe trying to find one that already works?
	// So many of these MSInfo refer to the same phone.
	// The spec deals with this by letting these things time out and die,
	// however, this results in conflicting in-flight assignments on AGCH
	// for different TLLIs that, in fact, refer to the same phone.
	// The spec does not provide any way for the L2 layer (us) to find that out.
	// So I introduced the AltTlli, which points to another MSInfo struct that
	// refers the same phone.  It MUST NOT be used for communication with the MS,
	// however, it can be used to avoid launching conflicting assignments.
	// The Sgsn sends us AltTlli as the AliasTlli in the DownlinkQPdu.
	UInt32_z msTlli;		// Identifies the MS, and is the TLLI used for downlink communication.
	UInt32_z msOldTlli;		// Also identifies the MS, and must be recognized in uplink communication.
	UInt32_z msAltTlli;		// Used to 'point' to another MSInfo struct that refers to the same MS,
							// as reported to us by the SGSN, which knows these things.
							// We save the TLLI instead of using a pointer because the other MSInfo
							// could disappear at any time.
	Bool_z msDeprecated;	// This MS has been replaced by some other, which is another way
							// of saying that the active MS's oldTlli points to this one.


	// If the MS is in packet-idle state, there should be no TBFs.
	TBFList_t msTBFs;	// TBFs for this MS, both uplink and downlink.
#define RN_MS_FOR_ALL_TBF(ms,tbf) for (RListIterator<TBF*> itr(ms->msTBFs); itr.next(tbf); ) 
	Int_z msUSFs[8];		// USF in each timeslot.
	// These are used by the RLCEngine to know when the MS has been granted a USF, ie, a chance to respond.
	Int_z msNumDataUSFGrants;	// Total number of USF grants; reset when last TBF detached.
	Int_z msAckNackUSFGrant;	// The msNumDataUSFGrants value of the last acknack.

	// Note: The uplink/downlink channels must all be in the same ARFCN that the MS is
	// camped on, and for multislot, follow strict timeslot adjacency rules.
	// The spec says that it is possible for different simultaneous TBFs for the same MS
	// to use different channel assignments, for exmaple, if the MS is sending a low-data-rate
	// TBF1 on a single channel and then wants to send a high-data-rate TBF2, it will interrupt
	// TBF2, may request a multislot allocation, and do TBF2 first.
	// Or another example, if TBF2 comes in and the TBF1 channel is on is congested, 
	// the MAC can pick a different channel for TBF2.
	// However, we are not going to support that.  The channels will be assigned to the MS
	// permanently, and all TBFs will use the same ones, which is why these lists are
	// here instead of in the TBF, where they really belong.
	// We may, however, someday change the channel assignments dynamically based on the
	// relative utilization of up and down links, for example, change from 4 down 1 up
	// to 4 up 1 down, etc., but if we do that we will have to wait until there are
	// no active TBFs, or reconfigure the existing TBFs.  Having all the channels
	// here in the MSInfo instead of scattered in different TBFs will make
	// such reconfiguration easier.
	PDCHL1UplinkList_t msPCHUps;	// uplink channels assigned to the MS; usually just one.
	PDCHL1DownlinkList_t msPCHDowns;	// downlink channels assigned to the MS; usually just one.
	bool msCanUseUplinkTn(unsigned tn);
	bool msCanUseDownlinkTn(unsigned tn);

	// This is the channel the MS is listening to for messages.
	// It is set before the msPCH assignments above.
	// How did the MS come to be listening to this channel, you wonder?
	// When a RACH comes in to the BTS, we do not know what MS it belongs to, so we pick
	// the least busy GPRS channel and tell the MS to send its request on that channel.
	// From then on the MS listens to that channel until we tell it differently
	// in a channel assignment, which should be the next thing we send to it.

	// TODO: Is the below correct?  Doesnt the MS always need to monitor PAACH which must
	// be one of its assigned channels?
	// It is possible for the msPCH assignments to not include the msPacch in several cases:
	// 1. If we deliberately give the MS a different channel assignment
	// for an uplink/downlink transfer, maybe because GPRS is underutilized and
	// we decided to close the channel.  (Channel closing not implemented in first draft.)
	// 2. Maybe we decide on a different set of channels to satisfy this particular
	// MSs multislot requirements.
	// 3. If a previously attached MS starts a new RACH request, which will assign
	// a new PCH at random (because we dont know what MS it is yet) and for some
	// reason we havent cleared the msPCH channels, maybe because our timeouts are out of phase.
	// In this weird case a BSSG downlink command might try to talk to the MS on
	// the old channels, which might even possibly work if the new assignment and the
	// old happen to be the same.  The special cases are complicated.
	// Note that if we used one-phase uplink access, this case 2 would extend in time out
	// into the uplink transfer, but we wont do that.
	PDCHL1FEC *msPacch;

	//RROperatingMode::type msMode; // Our belief about the state of MS: packet-idle, packet-transfer.


	UInt_z msIdleCounter;		// Counts how long MS is without TBFs; eventually we delete it.
	UInt_z msStalled;			// If MS is blocked, this is why, for error reporting.

	//Bool_z msUplinkRequest;		// Request from phone to establish uplink was delayed due to existing uplink tbf.
	//RLCMsgChannelRequestDescriptionIE msUplinkRequestCRD;  // The CRD for delayed request above.


	// GSM04.18 11.1.2:
	// T3141 - Started at Immediate Assignment, stopped when MS starts TBF.
	// We dont need it because we poll instead.

	// Note: Using Z100Timer is overkill for our single-threaded application;
	// could just use RLCBlockTime counters instead.
	// Counters and Timers defined in GSM04.60 sec 13.
	// We dont calculate N3105 and N3101 exactly the way the spec says,
	// but doesnt matter if they are off by 1 or 2.
	// NOTE: The blackberry sometimes waits 3 block periods before it starts
	// answering USFs, so N3101 better be bigger than that.
	UInt_z msN3101;			// Number of unacknowledged USF grants (off by one.)

	// GSM04.60 sec 13:
	// Note: The MS may take advantage of this time period by keeping the TBF open
	// after a PDU finishes, and not sending anything for a long time, then
	// sending sending additional PDUs in the same TBF later, but before the timer expires.
	GSM::Z100Timer msT3191;		// Waiting for acknowledgement of final TBF data block.

	// GSM04.60 sec 13:
	GSM::Z100Timer msT3193;		// After downlink TBF finished, MS camps on PDCH this long.
								// MS runs same timer but called T3192.

	// GSM04.60 sec 13:
	GSM::Z100Timer msT3168;		// MS camped on PDCH waiting for uplink assignment.
								// This timer is defined to be in the MS, not the BTS,
								// and we do not really need to track it as long as we
								// are sure we send the downlink assignment message
								// before this timer expires.  The timer value is in
								// the sql and broadcast in the beacon.
								// However, I am using the timer as a way of tracking
								// whether the assignment is for a RACH, rather
								// than setting some other variable.

	// GSM04.60 sec 13:
	//GSM::Z100Timer msT3169;		// Final timeout for dead tbf.
	//GSM::Z100Timer msT3195;		// Final timeout for dead tbf.
	//GSM::Z100Timer msTxxxx;		// Combined T3169, T3191, T3195 - timeout for
				// resource release after abnormal condition, during which time USF and TFI may not be reused.

	// When this MS was last granted a USF.
	// We use this when multiple MS are in contention for the uplink to make it fair.
	RLCBSN_t msLastUsfGrant;

	// Called when a USF is granted for this MS.
	// If penalize, if the MS does not answer we kill of the tbf.
	void msCountUSFGrant(bool penalize);

	// Incoming downlink data queue.
	// This queue is not between separate threads for BSSG,
	// and it is no longer for the internal sgsn either.
	//InterthreadQueue<BSSG::BSSGMsgDLUnitData> msDownlinkQueue;
	InterthreadQueue2<SGSN::GprsSgsnDownlinkPdu,SingleLinkList<> > msDownlinkQueue;
	Statistic<unsigned> msDownlinkQStat;
	Statistic<double> msDownlinkQDelay;
	Timeval msDownlinkQOldest;			// The timeval from the last guy in the queue.

	// Can this TBF use the specified uplink?
	bool canUseUplink(PDCHL1Uplink*up) {
		return msPCHUps.find(up);
	}
	// Can this TBF use the specified downlink?
	bool canUseDownlink(PDCHL1Downlink*down) {
		return msPCHDowns.find(down);
	}
	// Return the downlink channels as a bitmask for PacketDownlinkAssignment msg.
	unsigned char msGetDownlinkTimeslots(MultislotSymmetry sym);

	//PDCHL1Downlink *msPrimaryDownlink() { return msPCHDowns.front(); }
	bool msAssignChannels(); 	// Get channel(s) for this MS.
	private:
		bool msAddCh(unsigned chmask, const char *tnlist);
		bool msTrySlots(unsigned chmask,int down,int up);
		bool msAssignChannels2(int maxdown, int maxup, int sum);
	public:
	void msDeassignChannels();	// Release all channels for this MS.
	void msReassignChannels();	// Not implemented specially yet.

	//int msLastUplinkMsgBSN;	// When did we last hear from this MS?

	MSInfo(uint32_t tlli);
	// Use msDelete instead of calling ~MSinfo() directly.
	void msDelete(bool forever=0);	// If forever, do not move to expired list, just kill it.

	void msAddTBF(TBF *tbf) {
		devassert(! msTBFs.find(tbf));
		msTBFs.push_back(tbf);
	}
	void msForgetTBF(TBF *tbf) {
		devassert(msTBFs.find(tbf));
		msTBFs.remove(tbf);
	}


	// Called when a TBF goes dead.  If it was the last active uplink TBF, surrender our USFs.
	void msCleanUSFs();
	void msFailUSFs();

	unsigned msGetDownlinkQueuedBytes();
	//TBF * msGetDownlinkActiveTBF();
	private:
	int msCountTBF1(RLCDirType dir, enum TbfMacroState tbfstate, TBF**ptbf=0) const;
	int msCountTBF2(RLCDirType dir, enum TbfMacroState tbfstate, TBF**ptbf=0);
	public:
	int msCountActiveTBF(RLCDirType dir, TBF**ptbf=0);
	int msCountTransmittingTBF(RLCDirType dir, TBF**ptbf=0);
	void msService();
	void msStop(RLCDir::type dir, MSStopCause::type cause, TbfCancelMode cmode, int unsigned howlong);
	MSStopCause::type msStopCause;
	//void msRestart();
	ChannelCodingType msGetChannelCoding(RLCDirType wdir) const;
	int msGetTA() const { return GetTimingAdvance(msTimingError.getCurrent()); }
	// All MS use the same power params at the moment.
	int msGetAlpha() const { return GetPowerAlpha(); }
	int msGetGamma() const { return GetPowerGamma(); }
	void msDump(std::ostream&os, SGSN::PrintOptions options);
	void msDumpCommon(std::ostream&os) const;
	void msDumpChannels(std::ostream&os) const;
	RROperatingMode::type getRROperatingMode();
	string id() const;
	void msAliasTlli(uint32_t newTlli);
	void msChangeTlli(uint32_t newTlli);

	//void msSetUplinkRequest(RLCMsgChannelRequestDescriptionIE &wCRD) {
	//	msUplinkRequest = true;
	//	msUplinkRequestCRD = wCRD;
	//}

	// These are the functions required by the MSUEAdapter:
	uint32_t msGetHandle() { return msTlli; }
	string msid() const { return id(); }
	//void msWriteHighSide(ByteVector &dlpdu, uint32_t tlli, const char *descr) {
		//msDownlinkQueue.write(new GprsSgsnDownlinkPdu(dlpdu,tlli,descr));
	//}
	void msDeactivateRabs(unsigned rabMask) {}	// no-op in GPRS.

	bool msIsSuspended();				// Is the MS in suspended mode?
	bool msIsRegistered();				// Is the MS GPRS registered?
	bool isExtendedDynamic() { return msPCHUps.size() > msPCHDowns.size(); }
	bool msCanUseExtendedUplink();
};
extern unsigned gMSDebugId;

std::ostream& operator<<(std::ostream& os, const MSInfo*ms);

MSInfo *bssgMSChangeTLLI(unsigned oldTLLI,unsigned newTLLI);

};	// namespace
#endif
