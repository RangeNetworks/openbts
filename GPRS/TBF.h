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

#ifndef TBF_H
#define TBF_H

//#include <Interthread.h>
//#include <list>

#include "GPRSInternal.h"
#include "GPRSRLC.h"
#include "RLCHdr.h"
//#include "RList.h"
//#include "BSSG.h"
#include "Utils.h"
#include "MSInfo.h"

namespace GPRS {
class MSInfo;
struct RLCMsgChannelRequestDescriptionIE;

// TBF - Temporary Block Flow.
// This class is responsible for doing the work of moving data to/from the MS,
// and all the directly involved messages and acknowledgements.
// The TBF class is the main class, but always includes a MsgTransaction class,
// and is itself encapsulated in either an RLCEngineUp or RLCEngineDown class.
// These did not need to be separate classes, it was just a convenient way to
// encapsulate the different functionalities clearly.

// A major design decision was how to share the channel resource.
// We share the channels because an MS with a bad connection may require
// multiple block resends, and may have up to 5 second timeouts mulitple times.
// We allow only one downlink and one uplink TBF per MS.
// But all TBFs of all connected MS, both uplink and downlink, run simultaneously
// and are serviced in round-robin fashion, so short TBFs dont get hung behind hogs,
// and the system should degrade gracefully under load.
//
// Rather than using queues for messages and data blocks, these classes generate
// all the messages and data blocks on demand.  At each RLC Block time, and for
// each GPRS channel, we look for a message or data block to send on that channel
// by calling a TBF service routine the downlink PDCH service routine.
// If the TBF has something to send on the channel at that time, it does so,
// and notifies the calling service routine that it should stop looking for a block to send.
//
// Why do we use on-demand instead of queues?  Several reasons:
// o The data to be sent may be influenced by uplink blocks up until the time it is sent.
//   For example, we may need to resend a previous block, based on an intervening
//   acknack message from the MS.
// o If there is a block to be sent that needs an RRBP reservation and can't get it (because
//   the RRBP has an extremely limited reservation range and all may be in use), it surrenders
//   its service time to some TBF that can use the downlink without a reservation.
// o Assignment messages dont even know if they are going out on the PDCH or on the
//   CCCH queue until their transmit time arrives, at which time they consult the T3192/T3193
//   timers to find out.
// By generating all data and messages blocks on demand, we also ensure maximum utilization
// of the downlink resource regardless of the load.
// Note that TBFs may be traveling on multiple channels for multislot MS.

// The TBF defines a little state machine, which marks the progress of the TBF.
// This has nothing to do with the RACH responder, which happens before a TBF ever gets started.
// The RACH repsponder grants only single-block uplink reservations, which we promptly
// forget about (except reserving that timeslot), and service only if a message
// actually arrives in the granted uplink block, at which time a TBF is created.
// And if we dont get the message, nobody ever knows;
// it is the responsibility of the MS to run a timer and try RACHing again.
//
// Uplink for MS in PacketIdle:
// 		o MS sends RACH
//		o BTS sends ImmediateAssignment of single block on CCCH.
//			no timers started; if MS does not receive it, oh well...
//		o MS sends PacketResourceRequest on PDCH, then listens to PDCH for period T3168.
//			note that MS ignores downlink assignment during this period.
// 		label1:
//		o BTS allocates uplink TBF in state DataReadyToConnect, waits for internal resources.
//		label2:
//		o BTS sends PacketUplinkAssignment with poll for ControlAcknowledgment,
//		TBF moves to state DataWaiting1.  Note there is no timer - the ControlAcknowledgement
//		is scheduled for a particular RLCBSN.
//		TODO: If T3168 expired before reaching this state, dont bother.
//		o MS sends ControlAcknowledgement, TBF moves to DataTransmit state.
//		or: MS does not send response - if T3168 or retries not expired goto label2
//		o MS starts sending TBF data.
// Uplink for MS in PacketTransfer mode.
//		o MS sends PacketResourceRequest on PDCH. It can use an RRBP poll for this purpose.
//		I still havent figured out if we can let the MS do this during T3192 wait.
//		Unclear if MS starts T3168, but we certainly dont.
//		BTS allows multiple simultaneous uplinks, so just goto label1;
//		Not sure about special case where we are still waiting for response
//		from first PacketUplinkAssignment.
// Downlink when MS in PacketIdle:
//		This can only happen if we have heard from the MS recently so we
//		already have a TLLI to identify it.  It occurs when the SGSN sends a downlink TBF
//		but the MS has timed out and is back in PacketIdle mode, listening to CCCH.
//		This is distinct from a Paging Request which is initiated by the SGSN
//		when it is not sure if the MS is listening to this BTS or not.
//		label3:
//		o BTS sends ImmediateAssignment downlink message with poll.
//			we dont need to start timer T3141 because we poll.
//		o MS sends ControlAcknowledgement, TBF moves to DataTransmit state.
//		or: MS does not send response - if retries not expired goto label3
//		o BTS starts sending TBF data; TBF in DataTransmit state.
// Downlink for MS in PackeTransfer mode or T3192 wait period.
//		Can happen if MS is doing an uplink TBF or if MS camped on PDCH during
//		T3192 period after downlink completes.
//		label4:
//		o BTS sends PacketDownlinkAssignment message with RRBP poll.
//		o MS sends ControlAcknowledgement or other message in response to poll,
//		TBF moves to DataTransmit state.
//		or: MS does not send response - if retries not expired goto label4
//		
class TBFState
{
	public:
	enum type {
		Unused, // 0 Reserved.

		DataReadyToConnect,
			// Waiting to call tbf->mtAttach...() successfully.
			// It is waiting on resources like TFI or USF.
			// We (currently) only allow one downlink TBF per MS, although the MS can send multiple
			// simultaneous uplink TBFs, and nothing we can do about that.
			// When it connects (gets the resources reserved),
			// it calls MsgTransaction->sendMsg, which enqueues the message
			// (for either PACCH or CCCH), increments mSendTrys.
			// TBF state changes to DataWaiting1.

			// If the MS is in packet-idle mode, need to send the message on CCCH,
			// otherwise on PACCH.  See MSMode.

		DataWaiting1,
			// Waiting on MsgTransaction for MS to respond to uplink/downlink message,
			// Reply is in the form of RRBP granted PacketControlAcknowledgement.
			// If it is a multislot assignment that went on CCCH, we need yet
			// another MsgTransaction to do the timeslot reconfigure, so go to DataWaiting2,
			// otherwise go directly to DataTransmit.
			// (Because the CCCH Immediate Assignment supports only single-slot.)
			// Otherwise go directly to DataWaiting2.

		DataWaiting2,
			// Send the Packet Timing Advance required for downlink immediate assignment on CCCH.
			// Multislot TODO
			// Send the Multislot assignment.
			// Reply is in the form of RRBP granted PacketControlAcknowledgement.
			// On reply, go to state DataTransmit.
			// if gBSNNext <= mtExpectedAckBSN:
			//		We are waiting for the MS to send the response.  Do nothing.
			// else:
			//		if the MS did not set mAckYet (by sending us a message,
			//			probably Packet Control Acknowledgment):
			//			if too many mSendTrys, give up, goto stateDead.
			//			else resend the message and stay in this state.
			//		else the MS did set mAck:
			//			If the message is:
			//			Packet TBF release, kill this TBF.
			//			FinalAckNack: All uplink data successful, kill this TBF.
			//			Otherwise it is data up/down: activate the TBF, goto stateActive

		DataReassign,
			// Not currently used.

		DataTransmit,
			// MS and BTS are in PacketTransfer Mode
			// A data transfer TBF is trying to do its thing using the RLCEngine.
			// If it is a packet uplink assignment, set some timer, and we start setting USF
			// in downlink blocks to allow the MS to send us uplink blocks.
			// If it is a packet downlink assignment, start some timer and the serviceloop
			// will start sending downlink blocks when there is nothing else to do.
			// If it is packet TBF release, destroy this TBF.
			// Periodically the RLCEngine will send AckNack blocks in unacknowledged mode.
			// (Only the final AckNack message is sent in acknowledged mode.)

		DataFinal,
			// Only used for uplink TBFs now.
			// We have received all the uplink data, but we are waiting
			// for the MS to respond to the uplinkacknack message.
			// It wont stop sending data until it gets it, so we make sure.

		TbfRelease,
			// TBF is undergoing PacketTBFRelease procedure as the result of an abnormal termination.
			// We send the PacketTBFRelease message and then wait in this state until we get the acknowledgement.
			// When the get the response we will retry the TBF.

		Finished,
			// TBF is completely finished, but we keep it around until
			// all its reservations expire before detaching.

		Dead,
			// Similar to Finished, but this TBF is dead because
			// we lost contact with the MS or some timer expired.
			// It is *not* detached yet, ie, it hangs on to its resources.
			// We cannot reuse the resources for 5 (config parameter) seconds.
			// We could also act preemptively to send TBF destruction messages, which if answered
			// would allow us to get rid of the TBF, not that we care that much.
		Deleting
			// This state is entered via mtDetach().
			// This resources for this TBF have been (or are in the midst of being) released.
			// We use this ephemeral state to make deletion simpler.
		};
	static const char *name(int value);
};
std::ostream& operator<<(std::ostream& os, const TBFState::type &type);

#if TBF_IMPLEMENTATION
const char *TBFState::name(int value)
{
	switch ((type)value) {
	case Unused: return "TBFState::Unused";
	case DataReadyToConnect: return "TBFState::DataReadyToConnect";
	case DataWaiting1: return "TBFState::DataWaiting1";
	case DataWaiting2: return "TBFState::DataWaiting2";
	case DataReassign: return "TBFState::DataReassign";
	case DataTransmit: return "TBFState::DataTransmit";
	//case DataStalled: return "TBFState:DataStalled";
	case TbfRelease: return "TBFState::TbfRelease";
	case Finished: return "TBFState::Finished";
	case DataFinal: return "TBFState::DataFinal";
	case Dead: return "TBFState::Dead";
	case Deleting: return "TBFState::Deleting";
	}
	return "TBFState undefined!";	// Makes gcc happy
}
std::ostream& operator<<(std::ostream& os, const TBFState::type &type)
{
	os << TBFState::name(type);
	return os;
}
#endif

// These are the message transaction types.
// Each TBFState only uses one type of message transaction, so we could use
// the TBFState as the message transaction type, but the code is clearly
// if the message types are seprate from the TBF states.
// When we change state there may be outstanding messages that belong to the previous state,
// especially on error conditions.
// Most of these states are used only by uplink or downlink tbfs but not both;
// the inapplicable states are never used.
enum MsgTransactionType {
	MsgTransNone,		// Means nothing pending.
	MsgTransAssign1,	// For ack to first assignment msg (on ccch or pacch.)
	MsgTransAssign2,	// For ack to optional second assignment msg (always on pacch.)
	MsgTransReassign,	// For ack to reassignment if required.
	MsgTransDataFinal,	// For ack to final transmitted block. Used for both up and downlink.
						// In downlink, the block with the FBI indicator. N3103 in uplink, N3105 in downlink
	MsgTransTransmit,	// For acknack message during DataTransmit mode. Used for both uplink and downlink
						// In downlink N3105, in uplink for the non-final-ack.
	// There are two different transaction states for Assign messages because we may send two:
	// if the first is on CCCH and we want multislot mode, we have to send a second
	// assignment on pacch.
	MsgTransTA,			// For ack to Timing Advance msg.
	MsgTransTbfRelease,	// For ack to TbfRelease msg.
	MsgTransMax			// Not a transaction - indicates number of Transaction Types.
};

// This facility is used only for messages belonging to a TBF, which includes RRBP reservations
// and the Poll reservation for an assignment message sent on AGCH.
// It is not used for RACH polls, since we dont know who they belong to, and would be
// meaningless anyway because they do not correspond to a TBF somewhere changing states.
// We only track one outstanding reservation at a time for each TBF.
// Generally we send a message and then wait for a PacketControlAcknowledgement.
class MsgTransaction
{	private:
	BitSet mtMsgAckBits; 		// Type of acks received.
	BitSet mtMsgExpectedBits;	// The types messages we are waiting for.

	public:
	RLCBSN_t mtExpectedAckBSN[MsgTransMax];	// When we expect to get the acknowledgement from the MS.
	// This is supposed to be in the MS, but I moved it to the TBF because
	// some TBFs become non-responsive individually while others are still moving,
	// so we dont want to kill off the MS if a single TBF dies.
	UInt_z mtN3105;		// Counts RRBP data reservations that MS ignores.
	UInt_z mtN3103;		// Counts final downlinkacknacks RRBP that MS ignores.
	UInt_z mtAssignCounter;	// Count Assignment Messages.
	UInt_z mtReassignCounter;	// Count reassigment messages.
	UInt_z mtCcchAssignCounter;	// Number of assignments sent on CCCH.
	UInt_z mtTbfReleaseCounter;

	// Wait for the next message.
	void mtMsgSetWait(MsgTransactionType mttype) {
		devassert(mtExpectedAckBSN[mttype].valid());
		mtMsgAckBits.clearBit(mttype);
		mtMsgExpectedBits.setBit(mttype);
		GPRSLOG(4) << "mtMsgSetWait"<<LOGVAR(mttype)<<LOGVAR(mtMsgExpectedBits);
	}
	void text(std::ostream &os) const;

	// Set the BSN when the TBF is expecting a message, but note that the
	// MS may send uplink data blocks before this time.
	void mtSetAckExpected(RLCBSN_t when, MsgTransactionType mttype) {
		GPRSLOG(4) << "mtSetAckExpected"<<LOGVAR(when)<<LOGVAR(mttype);
		mtExpectedAckBSN[mttype] = when;
		mtMsgSetWait(mttype);
	}

	// Called to indicate that a message for this TBF arrived.
	void mtRecvAck(MsgTransactionType mttype) {
		GPRSLOG(4) << "mtRecvAck"<<LOGVAR(mttype)<<LOGVAR(mtMsgExpectedBits);
		mtMsgAckBits.setBit(mttype);
		mtMsgExpectedBits.clearBit(mttype);
		mtN3105 = 0;	// Not all of these are for mtN3105, but doesnt hurt to always reset it.
	}

	bool mtGotAck(MsgTransactionType mttype, bool clear) {
		bool result = mtMsgAckBits.isSet(mttype);
		GPRSLOG(4) << "mtGotAck "<<(result?"yes":"no")<<LOGVAR(mttype)<<LOGVAR(mtMsgExpectedBits);
		if (result && clear) {
			mtMsgExpectedBits.clearBit(mttype);
			mtExpectedAckBSN[mttype] = -1;
		}
		return result;
	}

	// Is any reservation currently outstanding?
	bool mtMsgPending();
	bool mtMsgPending(MsgTransactionType mttype);	// Is this msg still outstanding?

	//MsgTransaction() { }
};

// We will allocate a TBF both for data uplink/downlink transfers, and for single
// messages to the MS which require an ACK, even though strictly speaking that is not a TBF.
// A TBF is not used for a single block packet [uplink] access.
class TBF :
	public MsgTransaction		// Sends messages reliably.
{
	private:
	TBFState::type mtState;		// State of this TBF.  Dont set this directly, call setState().

	public:
	unsigned mtDebugId;
	MSInfo *mtMS;
	RLCDirType mtDir;
	Int_i<-1> mtTFI;				// Assigned TFI, or -1.

	// There is some question about whether we need ACKs (PacketControlAcknowledgement) at all.
	// For packet downlink, without the ACK we would not notice until the MS
	// did not respond to RRBPs long enough that we figured it out.
	// For packet uplink, without the ACK we could look at responses from this MS,
	// and if they are bad for awhile, restart this, but we cant really be sure
	// that the old TFI is released first.
	// So the ACKs make the state machine much safer in both cases.
	

	Bool_z mtUnAckMode;			// a.k.a. RLCMode.  If 1, send in unacknowledged mode.
								// Note: as of 6-2012 unacknowledged mode is not implemented.
	Bool_z mtAttached;			// Flag for mtAttach()/mtDetach() status.
	Bool_z mtAssignmentOnCCCH;	// Set if assignment was sent on CCCH.
	Bool_z mtPerformReassign;	// Reissue an uplink TBF assignment to 'change priority' geesh.
	//Bool_z mtIsRetry;			// Is this our second attempt to send this TBF?
	Bool_z mtTASent;			// For debugging, true if we sent an extra Timing Adv message.
	GprsTimer mtDeadTime;		// When a dead TBF can finally release resources.
	MSStopCause::type mtCause;	// Why the TBF died.
	//Float_z mtLowRSSI;			// Save the lowest RSSI seen for reporting purposes.
	uint32_t mtTlli;			// The tlli of an uplink TBF.  It is != mtMS->msTlli only
								// in the special case of a second AttachRequest occuring
								// after the TLLI reassignment procedure.

	// Persistence timers, used for both uplink and downlink.
	//GprsTimer mtKeepAliveTimer;	// Time to next keep alive.
	GprsTimer mtPersistTimer;	// How long TBF persists while idle.

	std::string mtDescription;	// For error reporting, what was in this TBF?

	Timeval mtStartTime;		// For reporting.  default init is to current time.


	// Statistics for flow control, needed only in downlink direction.
	//TODO: RLCBSN_t mtStartTime;	// When we started sending it.

	TBF(MSInfo *wms, RLCDirType wdir);
	// The virtual keyword tells C++ to call the derived destructors too.
	// Otherwise it may not.  It is foo bar.
	virtual ~TBF();

	// Can this TBF use the specified downlink?
	// It depends on the MS allocated channels.
	bool canUseDownlink(PDCHL1Downlink*down) { return mtMS->canUseDownlink(down); }

	// Can this TBF use the specified uplink?
	bool canUseUplink(PDCHL1Uplink*up) { return mtMS->canUseUplink(up); }

	void mtSetState(TBFState::type wstate);
	TBFState::type mtGetState() { return mtState; }

	// These are the states that count toward an MS RROperatingMode being
	// in PacketTransfer mode instead of PacketIdle mode.
	// We leave DataWaiting1 out.
	// First of all, we call this when we are doing a sendAssignment
	// and the tbf on whose behalf we are inquiring is in DataWaiting1,
	// so we get stuck here.
	// Second of all, we dont really know if the MS is listening
	// or not in DataWaiting1 mode because we have not heard back from
	// it after the sendAssignment.  Maybe we need yet another mode.
	bool isTransmitting() {
		switch (mtState) {
			case TBFState::DataWaiting2:
			case TBFState::DataTransmit:
			case TBFState::DataReassign:
			//case TBFState::DataStalled:
			case TBFState::DataFinal:
				return true;
			default:
				return false;
		}
	}
	// Used to determine if there is already a TBF running so we should not start another.
	// It is very important to include DataReadyToConnect because those indicate
	// that an assignment is already in progress, and we dont want to start another.
	bool isActive() {	// The TBF is trying to do something.
		switch (mtState) {
			case TBFState::DataReadyToConnect:
			case TBFState::DataWaiting1:
			case TBFState::DataWaiting2:
			case TBFState::DataTransmit:
			case TBFState::DataReassign:
			//case TBFState::DataStalled:
			case TBFState::DataFinal:
			case TBFState::TbfRelease:	// Counts until it is acknowledged as released.
				return true;
			case TBFState::Dead:
			case TBFState::Finished:	// TBF may still wait for res, but we dont count it.
			case TBFState::Deleting:
			case TBFState::Unused:
				return false;
		}
		return false;	// Unreached, but makes gcc happy.
	}
	bool mtServiceDownlink(PDCHL1Downlink *down);
	bool mtSendTbfRelease(PDCHL1Downlink *down);
	void mtServiceUnattached();
	void mtCancel(MSStopCause::type cause, TbfCancelMode release);
	void mtCancel(MsgTransactionType cause, TbfCancelMode release) {
		// Canceled due to expiry of MsgTransactionType timer.
		mtCancel((MSStopCause::type) cause, release);
	}
	void mtRetry();
	void mtFinishSuccess();
	std::string tbfDump(bool verbose) const;

	// For downlink we specify the channelcoding in the qbits of every block,
	// so we can change channelcoding dynamically between CS-1 and CS-4.
	// For uplink, the BTS specifies the encoding the MS will use in both
	// the uplink assignment and in every uplinkacknack message.
	// The ChannelCodingMax is used for retries to throttle back to a more secure codec.
	ChannelCodingType mtChannelCodingMax;	// The max channel coding (0-3) allowed for this TBF.
	ChannelCodingType mtCCMin, mtCCMax;		// Saved for reporting purposes.
	ChannelCodingType mtChannelCoding() const; 	// Return 0 - 3 for CS-1 or CS-4 for data transfer.

	// Note that this TBF is a base class of either an RLCEngineUp or RLCEngineDown,
	// depending on the TBF direction.
	// These functions are defined in RLCEngineUp and RLCEngineDown:
	virtual bool engineService(PDCHL1Downlink *down) = 0;
	virtual unsigned engineDownPDUSize() const {return 0;}
	virtual void engineRecvDataBlock(RLCUplinkDataBlock* block, int tn) {}
	virtual void engineRecvAckNack(const RLCMsgPacketDownlinkAckNack *msg) {}
	virtual float engineDesiredUtilization() = 0;
	virtual void engineGetStats(unsigned *pSlotsTotal, unsigned *pSlotsUsed, unsigned *pGrants) const = 0;
	virtual int engineGetBytesPending() = 0;
	virtual void engineDump(std::ostream &os) const = 0;
	virtual bool stalled() const = 0;
	//RLCDownEngine const* getDownEngine() const;	// Cant use this to change anything in RLCDownEngine
	RLCDownEngine * getDownEngine();
	static TBF *newUpTBF(MSInfo *ms,RLCMsgChannelRequestDescriptionIE &mCRD, uint32_t tlli, bool onRach);

	// In a multislot configuration one of the slots is the primary slots and is
	// used exclusively for all messages, and all other channels are used only for data.
	// The primary slot must have both uplink and downlink timeslots assigned,
	// and is currently identical to msPacch.  It is also the first channel in the
	// downlink list.
	bool isPrimary(PDCHL1Downlink *down);
	bool wantsMultislot();	// Does this tbf want to be multislot?

	// Attach to a channel.  Return true if we succeeded.
	bool mtAttach();
	bool mtNonResponsive();		// Is this TBF non responsive?
	// Detach from the channel.  Release our resources.
	void mtDetach();	// Internal use only - call mtCancel or mtFinishSuccess
	void mtDeReattach();	// Internal use only - call mtCancel or mtFinishSuccess
	// If forever, do not move to expired list, just kill it.
	void mtDelete(bool forever=0);	// Internal use only - call mtCancel or mtFinishSuccess
	//void setRadData(RadData &wRD);
	//void talkedUp() { mtMS->talkedUp(); }
	void talkedDown() { mtMS->talkedDown(); }
	uint32_t mtGetTlli();
	const char *tbfid(bool verbose);

	private:
	bool mtAllocateTFI();
	bool mtAllocateUSF();
	void mtFreeTFI();
};
extern unsigned gTBFDebugId;

std::ostream& operator<<(std::ostream& os, const TBF*tbf);
#if TBF_IMPLEMENTATION
std::ostream& operator<<(std::ostream& os, const TBF*tbf)
{
	if (tbf) {
		os << " TBF#" << tbf->mtDebugId <<" ";
	} else {
		os << " TBF(null ptr) ";
	}
	return os;
}
#endif

extern bool sendAssignment(PDCHL1FEC *pacch,TBF *tbf, std::ostream *os);

}	// namespace GPRS
#endif
