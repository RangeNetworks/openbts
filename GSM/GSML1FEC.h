/*
* Copyright 2008-2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2014 Range Networks, Inc.
*

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*/



#ifndef GSML1FEC_H
#define GSML1FEC_H
#include "Defines.h"

#include "Threads.h"
#include <assert.h>
#include "BitVector.h"
#include "ViterbiR204.h"
#include "AmrCoder.h"

#include "GSMCommon.h"
#include "GSMTransfer.h"
#include "GSMTDMA.h"

#include <a53.h>
#include "A51.h"

#include "GSM610Tables.h"
#include "GSM503Tables.h"

#include <OpenBTSConfig.h>

#include "../GPRS/GPRSExport.h"


class ARFCNManager;

namespace GSM {

ViterbiBase *newViterbi(AMRMode mode);

/* forward refs */
class GSMConfig;

//class SAPMux;

class L1FEC;
class L1Encoder;
class L1Decoder;
class GeneratorL1Encoder;
class SACCHL1Encoder;
class SACCHL1Decoder;
class SACCHL1FEC;
class TrafficTranscoder;
class L2LogicalChannelBase;

/*
	Naming convention for bit vectors follows GSM 05.03 Section 2.2.
	d[k]		data
	u[k]		data bits after first encoding step
	c[k]		data bits after second encoding step
	i[B][k]		interleaved data bits
	e[B][k]		bits in a burst
*/


enum EncryptionType {
	ENCRYPT_NO,
	ENCRYPT_MAYBE,
	ENCRYPT_YES
};




/**
	Abstract class for L1 encoders.
	In most subclasses, writeHighSide() drives the processing.
	(pat) base class for: XCCHL1Encoder, GeneratorL1Encoder
*/
class L1Encoder {

	protected:
	friend class L1FEC;

	ARFCNManager *mDownstream;
	TxBurst mBurst;					///< a preformatted burst template
	TxBurst mFillerBurst;			///< the filler burst for this channel
	// We cannot use a Time object for this because the channel could be idle longer than a hyperframe.
	Timeval mFillerSendTime;		// When the filler burst was last sent.

	/**@name Config items that don't change. */
	//@{
	const TDMAMapping& mMapping;	///< multiplexing description
	unsigned mCN;					///< carrier index
	unsigned mTN;					///< timeslot number to use
	unsigned mTSC;					///< training sequence for this channel
	L1FEC *mParent;					///< a containing L1FEC, if any
	//@}

	/**@name Multithread access control and data shared across threads. */
	// (pat) It is not being used effectively.
	//@{
	mutable Mutex mEncLock;
	//@}

	/**@ Internal state. */
	//@{
	// (pat) The way this works is rollForward() sets mNextWriteTime to the next
	// frame time specified in mMapping.  Each logical channel combination has a
	// custom serviceloop function running in a separate thread to multiplex the downstream data,
	// and send an appropriate frame to ARFCNManager::writeHighSideTx.
	// This is totally unlike decoders, for which AFCNManager:receiveBurst uses
	// the encoder mapping (which it has cached) to send incoming bursts directly
	// to the mapped L1Decoder::writeLowSideRx() for each frame.
	unsigned mTotalFrames;			///< total frames sent since last open()
	GSM::Time mPrevWriteTime;		///< timestamp of previous generated burst
	GSM::Time mNextWriteTime;		///< timestamp of next generated burst

	// (pat 10-2014) Another thread calls getNextWriteTime so we must protect places that write to mNextWriteTime.
	mutable Mutex mWriteTimeLock;

	volatile bool mRunning;			///< true while the service loop is running
	Bool_z mEncActive;				///< true between open() and close()
	Bool_z mEncEverActive;			// true if the encoder has ever been active.
	//@}

	// (pat) Moved to classes that need the convolutional coder.
	//ViterbiR2O4 mVCoder;	///< nearly all GSM channels use the same convolutional code

	std::string mDescriptiveString;

	public:

	EncryptionType mEncrypted;
	int mEncryptionAlgorithm;	// Algorithm number, ie 1 means A5_1, 2 means A5_2, etc.

	/**
		The basic encoder constructor.
		@param wCN carrier index.
		@param wTN TDMA timeslot number.
		@param wMapping TDMA mapping onto the timeslot -- MUST PERSIST.
		@param wParent The containing L1FEC, for sibling access -- may be NULL.
	*/
	L1Encoder(unsigned wCN, unsigned wTN, const TDMAMapping& wMapping, L1FEC *wParent);

	virtual ~L1Encoder() {}

	/** Set the transceiver pointer.  */
	virtual void downstream(ARFCNManager *wDownstream)
	{
		assert(mDownstream==NULL);	// Don't call this twice.
		mDownstream=wDownstream;
	}

	ARFCNManager *getRadio() { return mDownstream; }
	// Used by XCCHEncoder
	void transmit(BitVector2 *mI, BitVector2 *mE, const int *qbits);

	/**@name Accessors. */
	//@{
	const TDMAMapping& mapping() const { return mMapping; }
	/**@name Components of the channel description. */
	//@{
	unsigned CN() const { return mCN; }
	unsigned TN() const { return mTN; }
	unsigned TSC() const { return mTSC; }
	unsigned ARFCN() const;
	TypeAndOffset typeAndOffset() const;	///< this comes from mMapping
	//@}
	//@}

	/** Close the channel after blocking for flush.  */
	virtual void close();

	/** Open the channel for a new transaction.  */
	virtual void encInit();
	void encStart();
	virtual void serviceStart() {}

	public:

	/** Set mDownstream handover correlator mode. */
	void handoverPending(bool flag);

	/**
		Returns true if the channel is in use by a transaction.
		For broadcast and unicast channels this is always true.
		For dedicated channels, this is taken from the sibling deocder.
	*/
	virtual bool encActive() const;

	/**
	  Process pending L2 frames and/or generate filler and enqueue the resulting timeslots.
	  This method may block briefly, up to about 1/2 second.
	  This method is meaningless for some suclasses.
	*/
	virtual void writeHighSide(const L2Frame&) { assert(0); }

	virtual const char* descriptiveString() const { return mDescriptiveString.c_str(); }
	//virtual string debugId() const { static string id; return id.size() ? id : (id=format("%s %s ",typeid(this).name(),descriptiveString())); }

	const L1FEC* parent() const { return mParent; }
	L1FEC* parent() { return mParent; }

	GSM::Time getNextWriteTime();

	protected:

	/** Roll write times forward to the next positions. */
	void rollForward();

	/** Return pointer to paired L1 decoder, if any. */
	virtual L1Decoder* sibling();

	/** Return pointer to paired L1 decoder, if any. */
	virtual const L1Decoder* sibling() const;

	/** Make sure we're consistent with the current clock.  */
	void resync(bool force=false);

	/** Block until the BTS clock catches up to mPrevWriteTime.  */
	void waitToSend() const;

	/**
		Send the dummy filling pattern, if any.
		(pat) This is not the L2 idle fill pattern!
		The default is a dummy burst.
	*/
	void sendDummyFill();
	public:
	bool l1IsIdle() const;
};
extern ostream& operator<<(std::ostream& os, const L1Encoder *);


struct HandoverRecord {
	bool mValid;
	float mhrRSSI;
	float mhrTimingError;
	double mhrTimestamp;
	HandoverRecord() : mValid(false) {}
	HandoverRecord(float rssi,float te, double timestamp) : mValid(true), mhrRSSI(rssi), mhrTimingError(te), mhrTimestamp(timestamp) {}
};

struct DecoderStats {
	float mAveFER;
	float mAveBER;
	float mAveSNR;
	float mLastSNR;	// Most recent SNR
	float mLastBER;	// Most recent BER
	unsigned mSNRCount;	// Number of SNR samples taken.
	int mStatTotalFrames;
	int mStatStolenFrames;
	int mStatBadFrames;
	void decoderStatsInit();
	void countSNR(const RxBurst &burst);
};


/**
	An abstract class for L1 decoders.
	writeLowSideRx() drives the processing.
	// (pat) base class for: RACHL1Decoder, XCCHL1Decoder
	// It would be more elegant to split this into two classes: a base class
	// for both GPRS and RR, and the rest of this class that is RR specific.
	// Update: The thing to do here is to take RACCH out because it shares no code with this class.
*/
class L1Decoder {

	protected:
	friend class L1FEC;

	// (pat) Not used for GPRS.  Or for RACCH.  It should be in XCCHL1Decoder.
	L2LogicalChannelBase * mUpstream;	// Points to either L2LogicalChannel or SACCHLogicalChannel.

	/**@name Mutex-controlled state information. */
	//@{
	mutable Mutex mDecLock;				///< access control
	/**@name Timers from GSM 04.08 11.1.2 */
	// pat thinks these timers should be in XCCHL1Decoder or L2LogicalChannel.
	// (pat) GSM 4.08 3.4.13.1.1 is confusing, lets define some terms:
	// "main signalling disconnected" means at layer 2, which can be either by the normal release procedure
	// that sends a LAPDm DISC and waits for a response, or by "local end release" which means drop the channel immediately.
	// T3109 is either: 3.4.13.1.1 the time between deactivation of the SACCH and when the channel is considered recyclable, in which
	//   case we are relying on the MS to release the channel after RADIO-LINK-TIMEOUT missing SACCH messages,
	// or 3.4.13.2.2: the time after detecting a radio failure (by SACCH loss or bad RXLEV as per 5.08 5.2) and when
	// the channel is considered recylcable.
	// T3111 is shorter than T3109, used when a normal RELEASE is successfully acknoledged by the handset,
	// so we dont have to wait the entire T3109 time.
	//
	//@{
	//Z100Timer mT3101;					///< timer for new channels
	//Z100Timer mT3109;					///< timer for loss of uplink signal.  Need to check both host and SACCH timers.
	//Z100Timer mT3111;					///< timer for reuse of a normally closed channel
	Z100Timer mT3103;					///< timer for handover
	//@}
	bool mDecActive;					///< true between open() and close()
	//@}

	/**@name Atomic volatiles, no mutex. */
	// Yes, I realize we're violating our own rules here. -- DAB
	//@{
	//volatile float mFER;						///< current FER estimate
	//static const int mFERMemory=208;			///< FER decay time, in frames
				// (pat) Uh, no.  It is in frames for control frames, but in frames/4 for TCH frames.
				// (pat) Upped it from 20 frames to 208 frames, ie, about a second.
	static const int mSNRMemory = 208;
	volatile bool mHandoverPending;				///< if true, we are decoding handover bursts
	volatile unsigned mHandoverRef;
	HandoverRecord mHandoverRecord;
	//@}

	/**@name Parameters fixed by the constructor, not requiring mutex protection. */
	//@{
	unsigned mCN;					///< carrier index
	unsigned mTN;					///< timeslot number 
	const TDMAMapping& mMapping;	///< demux parameters
	L1FEC* mParent;			///< a containing L1 processor, if any
	/** The channel type. */
	virtual ChannelType channelType() const = 0;
	//@}

	// (pat) Moved to classes that use the convolutional coder.
	//ViterbiR2O4 mVCoder;	///< nearly all GSM channels use the same convolutional code

	EncryptionType mEncrypted;
	int mEncryptionAlgorithm;	// Algorithm number, ie 1 means A5_1, 2 means A5_2, etc.
	unsigned char mKc[8];
	int mFN[8];

	DecoderStats mDecoderStats;

	public:

	/**
		Constructor for an L1Decoder.
		@param wTN The timeslot to decode on.
		@param wMapping Demux parameters, MUST BE PERSISTENT.
		@param wParent The containing L1FEC, for sibling access.
	*/
	L1Decoder(unsigned wCN, unsigned wTN, const TDMAMapping& wMapping, L1FEC* wParent)
			:mUpstream(NULL),
			//mT3101(T3101ms),
			//mT3109(gConfig.GSM.Timer.T3109),
			//mT3111(T3111ms),
			mT3103(gConfig.GSM.Timer.T3103),
			mDecActive(false),
			//mRunning(false),
			//mFER(0.0F),
			mCN(wCN),mTN(wTN),
			mMapping(wMapping),mParent(wParent),
			mEncrypted(ENCRYPT_NO),
			mEncryptionAlgorithm(0)
	{
		// Start T3101 so that the channel will
		// become recyclable soon.
		//mT3101.set();
	}


	virtual ~L1Decoder() { }


	/**
		Clear the decoder for a new transaction.
		Start T3101, stop the others.
	*/
	virtual void decInit();
	void decStart();

	/**
		Call this at the end of a tranaction.
		Stop timers.  If releaseType is not hardRelase, start T3111.
	*/
	virtual void close();	// (pat) why virtual?

	public:
	/**
		Returns true if the channel is in use for a transaction.
		Returns true if T3111 is not active.
	*/
	bool decActive() const;

	/** Connect the upstream SAPMux and L2.  */
	virtual void upstream(L2LogicalChannelBase * wUpstream)
	{
		assert(mUpstream==NULL);	// Only call this once.
		mUpstream=wUpstream;
	}

	/** Total frame error rate since last open(). */
	float FER() const { return mDecoderStats.mAveFER; }			// (pat) old routine, switched to new variable.
	DecoderStats getDecoderStats() const { return mDecoderStats; }	// (pat) new routine, more stats

	/** Return the multiplexing parameters. */
	const TDMAMapping& mapping() const { return mMapping; }

	/** Accept an RxBurst and process it into the deinterleaver. */
	virtual void writeLowSideRx(const RxBurst&) = 0;

	/**@name Components of the channel description. */
	//@{
	unsigned TN() const { return mTN; }
	unsigned ARFCN() const;                 ///< this comes from mUpstream
	TypeAndOffset typeAndOffset() const;    ///< this comes from mMapping
	//@}

	/** Control the processing of handover access bursts. */
	// flag is true if handover is pending, false otherwise.
	// OK to pass reference to HandoverRecord since this struct is immortal.
	HandoverRecord &handoverPending(bool flag, unsigned handoverRef)
	{
		LOG(INFO) << LOGVAR(flag);
		if (flag) { mT3103.set(gConfig.GSM.Timer.T3103); } else { mT3103.reset(); }
		mHandoverPending=flag;
		mHandoverRef=handoverRef;
		return mHandoverRecord;
	}

	public:
	const L1FEC* parent() const { return mParent; }
	L1FEC* parent() { return mParent; }

	/** How much time left in T3101? */
	//long debug3101remaining() { return mT3101.remaining(); }

	protected:

	/** Return pointer to paired L1 encoder, if any. */
	virtual L1Encoder* sibling();

	/** Return pointer to paired L1 encoder, if any. */
	virtual const L1Encoder* sibling() const;

	public:
	// (pat 5-2014) Count bad frames in the BTS the same way as documented for RADIO-LINK-TIMEOUT in the MS, namely,
	// increment by one for each bad frame, decrement by two for each good frame.  This allows detecting marginal
	// conditions, whereas the old way of just using a timer that was reset only required one good frame every timer period,
	// which really only detected total channel loss, not a marginal channel.  This var is only used on SACCH,
	// so we dont bother modifying it in countStolenFrame, since stolen frames are only on TCH/FACCH.
	// Note: Spec says frames are always sent on SACCH in both directions as long as channel is up,
	// which is not necessarily required on the associated SDCCH or TCH/FACCH.
	// Note: Unlike the counts in DecoderStats we are counting message periods, which are 4-frame groups, not individual frames.
	int mBadFrameTracker;
	void countGoodFrame(unsigned nframes=1);
	virtual void countBadFrame(unsigned nframes = 1);	// over-ridden by SACCHL1Decoder
	void countStolenFrame(unsigned nframes = 1);
	void countBER(unsigned bec, unsigned frameSize);
	// TODO: The RACH does not have an encoder sibling; we should put a descriptive string for it too.
	virtual const char* descriptiveString() const { return sibling() ? sibling()->descriptiveString() : "RACH?"; }
	//virtual string debugId() const { static string id; return id.size() ? id : (id=format("%s %s ",typeid(this).name(),descriptiveString())); }
	std::string displayTimers() const;

	bool decrypt_maybe(string wIMSI, int wA5Alg);
	unsigned char *kc() { return mKc; }
};
extern ostream& operator<<(std::ostream& os, const L1Decoder *decp);





/**
	The L1FEC encapsulates an encoder and decoder.
	Notes by pat 8/2011:
	A complete L2 <-> L1 handler includes a set of instances of classes L1FEC, L1Encoder, L1Decoder.
	These are always wrapped by an instance of LogicalChannel, which defines the
	complete L3 <-> L1 handler.  The L1<->L2 handling is quite different for different
	logical channels, so all these classes are always over-ridden by more specific ones
	for each logical channel.  The descendents of L1Encoder/L2Decoder classes
	are not just encoders/decoders; together with the associated LogicalChannel class
	they incorporate the complete upstream and downstream channel handler.

	Initialization:
	All these instances are immortal (unlike GPRS PDCHL1FEC, which is allocated/deallocated
	on demand.)  The mEncoder and mDecoder below are set once
	and never changed, to define the related set of L1FEC+L1Encoder+L2Decoder.
	At startup, GSMConfig uses info from the tables in GPRSTDMA
	to create a complete set of instances of all these classes for each logical channel,
	in each physical channel to which they apply.  (The C0T0 beach gets a different
	set of classes than TCH Traffic channels, but every LogicalChannel descendent has
	its own distinct set of L1FEC+L1Encoder+L1Decoder descendents.)
	Note that there is an L1FEC+L1Encoder+L1Decoder per logical channel, not per
	physical channel; they all share the physical channel resource, as described below.
	The downstream end is connected to ARFCNManager in TRXManager.cpp.
	The upstream end goes various places, connected at runtime through SAPMux,
	or for some classes (example: RACH), directly to low-level managers.

	See also documentation in LogicalChannel::send().
	L2 -> L1 data flow is as follows:
		L2 calls SAPMux::writeHighSide(L2Frame),
		which calls L1FEC::writeHighSide(L2Frame),
		<or> L2 calls L1FEC::writeHighSide(L2Frame) directly,
		which then calls (L1Encoder)mEncoder->writeHighSide(L2Frame)
		This is overridden to provide the logical channel specific handling,
		which is performed by descendents of L1Encoder.  The frames may be processed
		at that point (for example, cause RR setup/teardown based on the frame primitive)
		or be passed downstream, in which case they usually go through sendFrame() below,
		which is over-ridden to provide the logical-channel specific encoding.
		Eventually, downstream frames go to L1Encoder::writeHighSideTx, which
		delivers them to the ARFCNManager.
		They may be delivered directly or spend time in an InterThreadQueue,
		which is processed by a serviceLoop, (which may reside either in the L1Encoder
		or LogicalChannel descendent) to synchronize them to the BTS frame clock
		(by using rollForward() to set mPrevTime, mNextTime, and then waitToSend() to block.)

	L1 -> L2 data flow is as follows:
		In TRXManager, the mDemuxTable, which was initialized from the GSMTDMA frame data,
		is consulted to pass the radio burst to the appropriate logical channel, using
		L1FEC::writeLowSideRx(RxBurst) in the appropriate L1FEC descendent.
		From there, anything can happen.  Four bursts need to be assembled and decoded.
		For TCH, FACH and SACH, this happens in (L1Decoder descendent)::processBurst(),
		which then calls countGoodFrame()+handleGoodFrame() or countBadFrame() if the
		parity was wrong.  handleGoodFrame() does the L1 housekeeping (start/stop timers,
		remember power/timing parameters) then passes the frame up using SAPMux->writeLowSide(),
		which calls some descendent L2DL.
		For RACH, writeLowSideRx decodes the burst and sends a message directly
		to gBTS.channelRequest(), which enqueues them for eventual processing
		by AccessGrantResponder().

	Routines:
	The start() routine is usually called once to create a thread to start a serviceloop thread.
	Radio bursts are then delivered to the class endpoints forever.
	The channels are turned on/off by calling open()/close(), which sets the active flag
	to determine whether they will process those bursts or drop them.

	GPRS Support:
	The "L2Frame" used ubiquitously in this code is a GSM-specific L2 frame.
	Now we want to add GPRS support with a new frame structure.

	I split the XCCHL1encoder/XCCHL1decoder classes into separate parts for handling
	the logical channel flow, which remained in the original classes, and the
	actual data encoding/decoding, which moved to SharedL1Encoder/SharedL2Encoder.
	The new SharedL1Encoder/SharedL2Encoder are shared with GPRS.

	Almost all the other functions in the L1Encoder/L2Decoder are different for GPRS
	because the channel is shared by multiple MS.  So GPRS has its own
	set of classes: PDCHL1FEC, PDCHL1Uplink, PDCHL1Downlink.

	Notice that the frame numbers used by GPRS for Radio Blocks are identical to the
	data frame numbers for GSM RR TCH channels.  Similarly, the GPRS timing advance channels
	use the same frame numbers as GPRS RR SACCH (although we dont use those yet.)
	We will allocate the GPRS channels dynamically from the TCH pool using
	getTCH to allocate an existing TCH LogicalChannel class, which wont otherwise
	be used for GPRS, except to return to the pool when GPRS signs off the channel.
	The L1Decoder/L1Encoder classes will now be three state: inactive, active for RR,
	active for GPRS.  Uplink data will be diverted to GPRS code at the earliest point
	possible, which is in XCCHL1Decoder::writeLowSideRx().

	Another option was to completely bypass this code, modifing TRXManager,
	either by changing the mDemuxTable to send radio bursts directly to the GPRS code, or
	adding a new hook to simply send the entire timeslot to GPRS.
	We might still want to go back and do that at some point, possibly when
	we implement continuous timing advance.
*/
class L1FEC {
	protected:

	L1Encoder* mEncoder;
	L1Decoder* mDecoder;

	public:
	// The mGprsReserved variable prevents the GSM subsystem from using the channel.
	// When the GPRS PDCHL1FEC is ready to receive bursts, it sets mGPRSFEC.
	bool mGprsReserved;				// If set, channel reserved for GPRS.
	GPRS::PDCHL1FEC *mGPRSFEC;	// If set, bursts are delivered to GPRS.
					// Currently, this could go in TCHFACCHL1Decoder instead.


	/**
		The L1FEC constructor is over-ridden for different channel types.
		But the default has no encoder or decoder.
	*/
	L1FEC():
	mEncoder(NULL),mDecoder(NULL)
	, mGprsReserved(0)
	, mGPRSFEC(0) 
	{}

	/** This is no-op because these channels should not be destroyed.
		(pat) We may allocate/deallocate GPRS channels on demand,
		stealing GSM channels, so above statement may become untrue.
	*/
	virtual ~L1FEC() {};

	/** Send in an RxBurst for decoding. */
	// (pat) I dont think this is ever called.  Gotta love C++
	void writeLowSideRx(const RxBurst& burst)
		{ assert(mDecoder); mDecoder->writeLowSideRx(burst); }

	/** Send in an L2Frame for encoding and transmission. */
	// (pat) not used for GPRS.
	virtual void writeHighSide(const L2Frame& frame)
	{
		assert(mEncoder); mEncoder->writeHighSide(frame);
	}

	/** Attach L1 to a downstream radio. */
	void downstream(ARFCNManager*);

	/** Attach L1 to an upstream SAPI mux and L2. */
	// (pat) not used for GPRS.
	virtual void upstream(L2LogicalChannelBase* mux)
		{ if (mDecoder) mDecoder->upstream(mux); }

	/** set encoder and decoder handover pending mode. */
	HandoverRecord& handoverPending(bool flag, unsigned handoverRef);

	void l1init();	// Called from lcopen when channel is created or from getTCH/getSDCCH.
	void l1start();	// Called from lcopen when channel is created or from getTCH/getSDCCH.
	void l1open() { l1init(); l1start(); }

	void l1close();	// Called from lcopen when channel is created or from getTCH/getSDCCH.
	

	/**@name Pass-through actions that concern the physical channel. */
	//@{
	TypeAndOffset typeAndOffset() const
		{ assert(mEncoder); return mEncoder->typeAndOffset(); }

	unsigned TN() const		// Timeslot number to use.
		{ assert(mEncoder); return mEncoder->TN(); }

	unsigned CN() const		// Carrier index.
		{ assert(mEncoder); return mEncoder->CN(); }

	unsigned TSC() const	// Trainging sequence for this channel.
		{ assert(mEncoder); return mEncoder->TSC(); }

	unsigned ARFCN() const	// Absolute Radio Frequence Channel Number.
		{ assert(mEncoder); return mEncoder->ARFCN(); }

	float FER() const		// Frame Error Rate
		{ assert(mDecoder); return mDecoder->FER(); }

	// Is the channel active?  This tests both the mActive flag, which follows the L2 LAPDm state,
	// as known by tracking ESTABLISH/RELEASE/ERROR primitives
	// and recyclable, which tracks RR failure.
	// The testonly flag is used from recyclable to prevent an infinite loop.
	bool l1active() const;	// Channel in use?  See L1Encoder
	//L2LogicalChannel *ownerChannel() const;

	// (pat) This lovely function is unsed.
	// TRXManager.cpp:installDecoder uses L1Decoder::mapping() directly.
	const TDMAMapping& txMapping() const
		{ assert(mEncoder); return mEncoder->mapping(); }

	// (pat) This function is unsed.
	const TDMAMapping& rcvMapping() const
		{ assert(mDecoder); return mDecoder->mapping(); }

	virtual const char* descriptiveString() const
		{ assert(mEncoder); return mEncoder->descriptiveString(); }
	//virtual string debugId() const { static string id; return id.size() ? id : (id=format("%s %s ",typeid(this).name(),descriptiveString())); }

	//@}

	//void setDecoder(L1Decoder*me) { mDecoder = me; }
	//void setEncoder(L1Encoder*me) { mEncoder = me; }
	ARFCNManager *getRadio() { return mEncoder->getRadio(); }
	bool inUseByGPRS() const { return mGprsReserved; }
	void setGPRS(bool reserved, GPRS::PDCHL1FEC *pch) { mGprsReserved = reserved; mGPRSFEC = pch; }
	std::string displayTimers() const { return mDecoder ? mDecoder->displayTimers() : ""; }

	L1Decoder* decoder() { return mDecoder; }
	L1Encoder* encoder() { return mEncoder; }
}; 
extern ostream& operator<<(std::ostream& os, const L1FEC *);


/**
	The TestL1FEC does loopbacks at each end.
*/
class TestL1FEC : public L1FEC {

	private:

	L2LogicalChannelBase * mUpstream;
	ARFCNManager* mDownstream;

	public:

	void writeLowSideRx(const RxBurst&);
	void writeHighSide(const L2Frame&);

	void downstream(ARFCNManager *wDownstream) { mDownstream=wDownstream; }
	void upstream(L2LogicalChannelBase * wUpstream){ mUpstream=wUpstream;}
};


/** L1 decoder for Random Access (RACH). */
class RACHL1Decoder : public L1Decoder
{

	private:

	/**@name FEC state. */
	//@{
	ViterbiR2O4 mVCoder;	///< nearly all GSM channels use the same convolutional code
	Parity mParity;					///< block coder
	BitVector2 mU;					///< u[], as per GSM 05.03 2.2
	BitVector2 mD;					///< d[], as per GSM 05.03 2.2
	//@}
	ChannelType channelType() const { return RACHType; }

	public:

	RACHL1Decoder(const TDMAMapping &wMapping, L1FEC *wParent, unsigned wTN)
		:L1Decoder(0,wTN,wMapping,wParent),
		mParity(0x06f,6,8),mU(18),mD(mU.head(8))
	{ }

	/** Decode the burst and call the channel allocator. */
	void writeLowSideRx(const RxBurst&);
};





// This is just an encoder, nothing else, shared by RR and GPRS.
// This is the encoder specified in GSM05.03 sec 4.1, used for SACCH and GPRS CS-1.
// Why isnt this derived directly from L1Encoder, you ask?
// First it was because GPRS has multiple encoders for different encoding schemes
// and they all use a single L1Encoder attached to the radio.
// Second, because the GSM L1Encoder is not just an encoder, it is the complete stack
// down to the radio, whereas this class is just an encoder only.
// First case above is now inapplicable because the additional GPRS encoders are now
// derived from this one.
class SharedL1Encoder
{
	protected:
#ifdef TESTTCHL1FEC
	public:
#endif
	ViterbiR2O4 mVCoder;	///< nearly all GSM channels use the same convolutional code
    Parity mBlockCoder;
    BitVector2 mC;               ///< c[], as per GSM 05.03 2.2 Data after second encoding step.
    BitVector2 mU;               ///< u[], as per GSM 05.03 2.2 Data after first encoding step.
    //BitVector2 mDP;              ///< d[]:p[] (data & parity)
	public:
    BitVector2 mD;               ///< d[], as per GSM 05.03 2.2		Incoming Data.
	private:
    BitVector2 mP;               ///< p[], as per GSM 05.03 2.2  (pat) no such thing
	public:
    BitVector2 mI[4];           ///< i[][], as per GSM 05.03 2.2	Outgoing Data.
    BitVector2 mE[4];

	/** GSM 05.03 4.2 encoder
	  Encode u[] to c[].
	  Includes LSB-MSB reversal within each octet.
	*/
	void encode41();

	/**
	  Interleave c[] to i[].
	  GSM 05.03 4.1.4.
	  It is not virtual.
	*/
	void interleave41();

	public:

	SharedL1Encoder();

	//void encodeFrame41(const L2Frame &frame, int offset);
	void encodeFrame41(const BitVector2 &frame, int offset, bool copy=true);
	void initInterleave(int);
	virtual const char* descriptiveString() const = 0;
	//string debugId() const { static string id; return id.size() ? id : (id=format("SharedL1Encoder %s ",descriptiveString())); }
};

// Shared by RR and GPRS
class SharedL1Decoder
{
    protected:

    /**@name FEC state. */
    //@{
	ViterbiR2O4 mVCoder;	///< nearly all GSM channels use the same convolutional code
    Parity mBlockCoder;
	public:
    SoftVector mC;              ///< c[], as per GSM 05.03 2.2
    BitVector2 mU;               ///< u[], as per GSM 05.03 2.2
    BitVector2 mP;               ///< p[], as per GSM 05.03 2.2
    BitVector2 mDP;              ///< d[]:p[] (data & parity)
	public:
    BitVector2 mD;               ///< d[], as per GSM 05.03 2.2
    SoftVector mE[4];
    SoftVector mI[4];           ///< i[][], as per GSM 05.03 2.2
	/**@name Handover Access Burst FEC state. */
	//@{
	Parity mHParity;			///< block coder for handover access bursts
	BitVector2 mHU;				///< u[] for handover access, as per GSM 05.03 4.6
	BitVector2 mHD;				///< d[] for handover access, as per GSM 05.03 4.6
	//@}
    //@}

	GSM::Time mReadTime;        ///< timestamp of the first burst

	public:

    SharedL1Decoder();
	virtual const char* descriptiveString() const = 0;
	//string debugId() const { static string id; return id.size() ? id : (id=format("SharedL1Decoder %s ",descriptiveString())); }

    void deinterleave();
    bool decode();
	SoftVector *result() { return mI; }
};


/** Abstract L1 decoder for most control channels -- GSM 05.03 4.1 */
// (pat) That would be SDCCH, SACCH, and FACCH.  TCH goes to TCHFRL1Decoder instead.
class XCCHL1Decoder :
	virtual public SharedL1Decoder,
	public L1Decoder
{

	protected:

	public:

	XCCHL1Decoder(unsigned wCN, unsigned wTN, const TDMAMapping& wMapping,
		L1FEC *wParent);

	void saveMi();
	void restoreMi();
	void decrypt();

	protected:

	/** Offset to the start of the L2 header. */
	virtual unsigned headerOffset() const { return 0; }

	/** Accept a timeslot for processing and drive data up the chain. */
	virtual void writeLowSideRx(const RxBurst&);

	/**
	  Accept a new timeslot for processing and save it in i[].
	  This virtual method works for all block-interleaved channels (xCCHs).
	  A different method is needed for diagonally-interleaved channels (TCHs).
	  @return true if a new frame is ready for deinterleaving.
	*/
	virtual bool processBurst(const RxBurst&);
	
	/** Finish off a properly-received L2Frame in mU and send it up to L2. */
	virtual void handleGoodFrame();
	const char* descriptiveString() const { return L1Decoder::descriptiveString(); }
	//string debugId() const { return L1Decoder::debugId(); }
};



/** L1 decoder for the SDCCH.  */
class SDCCHL1Decoder : public XCCHL1Decoder {

	public:

	SDCCHL1Decoder(
		unsigned wCN,
		unsigned wTN,
		const TDMAMapping& wMapping,
		L1FEC *wParent)
		:XCCHL1Decoder(wCN,wTN,wMapping,wParent)
	{ }

	ChannelType channelType() const { return SDCCHType; }
	const char* descriptiveString() const { return L1Decoder::descriptiveString(); }

};

// This is just the physical info sent on SACCH.  The FER() is in the base class used by all channel types,
// and the measurement reports are up in Layer 2.
class MSPhysReportInfo {
	public:	// dont know why we bother
	unsigned mReportCount;			// Number of measurement reports received so far (not including the initial RACH); used for averaging RSSI and others.
	volatile float mRSSI;			///< most recent RSSI, dB wrt full scale (Received Signal Strength derived from our measurement of the received burst).
	volatile float mTimingError;		///< Timing error history in symbols (derived from our measurement of the received burst).
	volatile double mTimestamp;		///< system time of most recent received burst, including the initial RACH burst.
	volatile int mActualMSPower;		///< actual MS tx power in dBm (that the MS reported to us)
	volatile int mActualMSTiming;		///< actual MS tx timing advance in symbols (that the MS reported to us)
	void sacchInit1();
	void sacchInit2(float wRSSI, float wTimingError, double wTimestamp);
	public:
	// constructor is irrelevant, should call sacchInit via open() before each use, but be tidy.
	MSPhysReportInfo() { sacchInit1(); }	// Unused init

	bool isValid() const { return mReportCount > 0; }
	int actualMSPower() const { return mActualMSPower; }
	int actualMSTiming() const { return mActualMSTiming; }
	
	/** Set physical parameters for initialization. */
	void initPhy(float wRSSI, float wTimingError, double wTimestamp);

	void setPhy(const SACCHL1Decoder& other);

	/** RSSI of most recent received burst, in dB wrt full scale. */
	float getRSSI() const { return mRSSI; }
	float getRSSP() const {
		return mRSSI + gConfig.GSM.MS.Power.Max - mActualMSPower;
	}
	
	/** Artificially push down RSSI to induce the handset to push more power. */
	void RSSIBumpDown(float dB) { mRSSI -= dB; }

	/** Timestamp of most recent received burst. */
	// (pat) Since BTS started, in seconds, with uSec resolution.
	// Includes the initial rach burst, which is significant because that one provided only TA, did not have any useful power data.
	double timestamp() const { return mTimestamp; }

	/**
		Timing error of most recent received burst, symbol units.
		Positive is late; negative is early.
	*/
	float timingError() const { return mTimingError; }
	void processPhysInfo(const RxBurst &inBurst);
	MSPhysReportInfo * getPhysInfo() { return this; }
	virtual const char* descriptiveString() const = 0;
	//string debugId() const { static string id; return id.size() ? id : (id=format("MSPhysReportInfo %s ",descriptiveString())); }
};


/**
	L1 decoder for the SACCH.
	Like any other control channel, but with hooks for power/timing control.
*/
class SACCHL1Decoder : public XCCHL1Decoder, public MSPhysReportInfo {

	private:

	SACCHL1FEC *mSACCHParent;


	public:

	SACCHL1Decoder(
		unsigned wCN,
		unsigned wTN,
		const TDMAMapping& wMapping,
		SACCHL1FEC *wParent)
		:XCCHL1Decoder(wCN,wTN,wMapping,(L1FEC*)wParent),
		mSACCHParent(wParent)
	{
		// (pat) DONT init any dynamic data here.  It is pointless - this object is immortal.
		// TODO: on first measurement, make them all the same.
		// David says: RACH has an option to ramp up power - should remove that option.
		//sacchInit();
	}

	ChannelType channelType() const { return SACCHType; }


	/** Override decInit() to set physical parameters with reasonable defaults. */
	void decInit();

	/**
		Override processBurst to catch the physical parameters.
	*/
	bool processBurst(const RxBurst&);
	void countBadFrame(unsigned nframes = 1);	// over-ridden by SACCHL1Decoder

	// (pat) TODO: We should just keep SNR for the SACCH, it is overkill computing it for the main channel.
	float getAveSNR() { return getDecoderStats().mAveSNR; }

	protected:

	//unused: SACCHL1FEC *SACCHParent() { return mSACCHParent; }

	SACCHL1Encoder* SACCHSibling();

	/**
		This is a wrapper on handleGoodFrame that processes the physical header.
	*/
	void handleGoodFrame();

	unsigned headerOffset() const { return 16; }
	const char* descriptiveString() const { return L1Decoder::descriptiveString(); }
	//string debugId() const { return L1Decoder::debugId(); }
};




/** L1 encoder used for many control channels -- mostly from GSM 05.03 4.1 */
class XCCHL1Encoder :
	virtual public SharedL1Encoder,
	public L1Encoder
{
	public:

	XCCHL1Encoder(
		unsigned wCN,
		unsigned wTN,
		const TDMAMapping& wMapping,
		L1FEC* wParent);

	protected:

	/** Process pending incoming messages. */
	// (pat) Messages may be control primitives.  If it is data, it is passed to sendFrame()
	virtual void writeHighSide(const L2Frame&);

	/** Offset from the start of mU to the start of the L2 frame. */
	virtual unsigned headerOffset() const { return 0; }

	/** Send a single L2 frame.  */
	virtual void sendFrame(const L2Frame&);
	const char* descriptiveString() const { return L1Encoder::descriptiveString(); }
	//string debugId() const { return L1Encoder::debugId(); }
	// Moved to SharedL1Encoder
	//virtual void transmit(BitVector2 *mI);
};




// downstream flow:
//	RTP => transaction->rxFrame(char[] rxFrame)
//		updateCallTraffic(): TCHFACCHLogicalChannel *TCH->sendTCH(rxFrame);
//		sendTCH packs as VocoderFrame and puts on SpeechQ.
//		encodeTCH gets the payload from the VocoderFrame, result left in mC.
//		then TCHFACCHL1Encoder::dispatch() sends mC result to radio.
// Alternative:
//		Allocate as bytevector from rtp on, and decode to VocoderFrame in sendTCH.
//		So speechQ type is ByteVector.
//
// upstream flow:
//		speechQ

class TCHFRL1Encoder : virtual public SharedL1Encoder
{
	// Shared AMR and GSM_FR variables:
	ViterbiBase *mViterbi;		// Points to current encoder in use.
	AMRMode mAMRMode;
	Parity mTCHParity;
	BitVector2 mPrevGoodFrame;	///< current and previous good frame

	BitVector2 mTCHU;				///< u[], but for traffic
	BitVector2 mTCHD;				///< d[], but for traffic
	BitVector2 mTCHRaw;				///< Raw data before mapping.

	BitVector2 mClass1A_d;			///< the class 1A part of taffic d[]
	BitVector2 mClass1_c;			///< the class 1 part of taffic c[]
	BitVector2 mClass2_d;			///< the class 2 part of d[]

	// AMR variables:
	const unsigned *mAMRBitOrder;
	unsigned mClass1ALth;
	unsigned mClass1BLth;
	BitVector2 mTCHUC;
	const unsigned *mPuncture;
	unsigned mPunctureLth;

	void encodeTCH_AFS(const SIP::AudioFrame* vFrame);
	void encodeTCH_GSM(const SIP::AudioFrame* vFrame);
	void setViterbi(AMRMode wMode) {
		if (mViterbi) { delete mViterbi; }
		mViterbi = newViterbi(wMode);
	}
	public:
	unsigned getTCHPayloadSize() { return GSM::gAMRKd[mAMRMode]; }	// decoded payload size.
	void setAmrMode(AMRMode wMode);
	/** Encode a full speed AMR vocoder frame into c[]. */
	void encodeTCH(const SIP::AudioFrame* aFrame);	// Not that the const does any good.

	// (pat) Irritating and pointless but harmless double-initialization of Parity and BitVector2s.  Stupid language.
	// (pat) Assume TCH_FS until someone changes the mode to something else.
	TCHFRL1Encoder() : mViterbi(0), mTCHParity(0,0,0) { setAmrMode(TCH_FS); }
	//string debugId() const { static string id; return id.size() ? id : (id=format("TCHFRL1Encoder %s ",descriptiveString())); }
};


/** L1 encoder used for full rate TCH and FACCH -- mostry from GSM 05.03 3.1 and 4.2 */
class TCHFACCHL1Encoder :
	public XCCHL1Encoder,
	public TCHFRL1Encoder
{
	bool mPreviousFACCH;	///< A copy of the previous stealing flag state.
	size_t mOffset;			///< Current deinterleaving offset.

	BitVector2 mE[8];
	// (pat) Yes, the mI here duplicates but overrides the same
	// vector down in XCCHL1Encoder.
	BitVector2 mI[8];			///< deinterleaving history, 8 blocks instead of 4

	BitVector2 mFillerC;				///< copy of previous c[] for filling dead time

	AudioFrameFIFO mSpeechQ;		///< input queue for speech frames

	L2FrameFIFO mL2Q;				///< input queue for L2 FACCH frames

	Thread mEncoderThread;
	friend void TCHFACCHL1EncoderRoutine( TCHFACCHL1Encoder * encoder );	

public:

	TCHFACCHL1Encoder(unsigned wCN, unsigned wTN, 
			  const TDMAMapping& wMapping,
			  L1FEC* wParent);

	/** Enqueue a traffic frame for transmission by the FEC to be sent to the radio. */
	void sendTCH(SIP::AudioFrame *frame) { mSpeechQ.write(frame); }

	/** Extend open() to set up semaphores. */
	void encInit();

protected:

	// GSM 05.03, 3.1.3
	/** Interleave c[] to i[].  GSM 05.03 4.1.4. */
	void interleave31(int blockOffset);

	/** Encode a FACCH and enqueue it for transmission. */
	void sendFrame(const L2Frame&);

	/**
		dispatch called in a while loop.
		process reading transcoder and fifo to 
		interleave and send.
	*/
	void dispatch();

	/** Will start the dispatch thread. */
	void serviceStart();

	//string debugId() const { static string id; return id.size() ? id : (id=format("TCHFACCHL1Encoder %s ",descriptiveString())); }
};


/** The C adapter for pthreads. */
void TCHFACCHL1EncoderRoutine( TCHFACCHL1Encoder * encoder );

// TCH full rate decoder.
class TCHFRL1Decoder : virtual public SharedL1Decoder
{
	// Shared AMR and GSM_FR variables:
	AMRMode mAMRMode;
	Parity mTCHParity;
	BitVector2 mPrevGoodFrame;	///< current and previous good frame
	unsigned mNumBadFrames;		// Number of bad frames in a row.

	BitVector2 mTCHU;					///< u[] (uncoded) in the spec
	BitVector2 mTCHD;					///< d[] (data) in the spec
	//SoftVector mClass1_c;				///< the class 1 part of c[]
	BitVector2 mClass1A_d;				///< the class 1A part of d[]
	//SoftVector mClass2_c;				///< the class 2 part of c[]

	// AMR variables:
	const unsigned *mAMRBitOrder;
	unsigned mClass1ALth;
	unsigned mClass1BLth;
	SoftVector mTCHUC;
	const unsigned *mPuncture;
	unsigned mPunctureLth;
	ViterbiBase *mViterbi;		// Points to current decoder in use.
	SoftVector mE[8];	///< deinterleaving history, 8 blocks instead of 4
	SoftVector mI[8];	///< deinterleaving history, 8 blocks instead of 4

	bool decodeTCH_GSM(bool stolen, const SoftVector *wC);
	bool decodeTCH_AFS(bool stolen, const SoftVector *wC);
	protected:
	virtual void addToSpeechQ(SIP::AudioFrame *newFrame) = 0;	// Where the upstream result from decodeTCH goes.
	void setViterbi(AMRMode wMode) {
		if (mViterbi) { delete mViterbi; }
		mViterbi = newViterbi(wMode);
	}
	public:
	/**
		Decode a traffic frame from TCHI[] and enqueue it.
		Return true if there's a good frame.
	*/
	bool decodeTCH(bool stolen, const SoftVector *wC);	// result goes to mSpeechQ
	void setAmrMode(AMRMode wMode);

	// (pat) Irritating and pointless but harmless double-initialization of Parity and BitVector2s.  Stupid language.
	// (pat) Assume TCH_FS until someone changes the mode to something else.
	TCHFRL1Decoder() : mTCHParity(0,0,0), mViterbi(0) { setAmrMode(TCH_FS); }
	//string debugId() const { static string id; return id.size() ? id : (id=format("TCHFRL1Decoder %s ",descriptiveString())); }
};

/** L1 decoder used for full rate TCH and FACCH -- mostly from GSM 05.03 3.1 and 4.2 */
// (pat) This class is basically a switch to steer incoming uplink radio frames in one of three categories
// (FAACH, TCH, handover burst) into either the control or data upstream paths.
// If we are not waiting for handover, the 'stolen' flag inside the radio frame
// indicates whether the frame is FACCH (fast associated control channel message) or TCH (voice traffic) frame.
// Messages (both FACCH and handover) go to XCCHL1Decoder and TCH go to TCHFRL1Decoder.
// fyi: Oh, the TCHFACCHL1Decoder is connected to the XCCHL1Decoder, and the XCCHL1Decoder is connected to the...L1Decoder,
// and the L1Decoder is connected to the...SAPMux, and the SAPMux is connected to the...LogicalChannel,
// and the LogicalChannel is connected to the...L2DL, and the L2DL is connected to the...FACCHL2,
// and the FACCHL2 is connected to the...L2LapDm, and the L2LapDm is connected to the...L3FrameFIFO,
// and the L3FrameFIFO is connected to...LogicalChannel::recv, and thence to whatever procedure in the L3 layers that wants
// to receive these messages, eg MOCController, MTCController, although the L3rewrite will substitute all
// those with the L3 state machine.
class TCHFACCHL1Decoder :
	public XCCHL1Decoder,
	public TCHFRL1Decoder
{
	protected:
	SoftVector mE[8];	///< deinterleaving history, 8 blocks instead of 4
	SoftVector mI[8];	///< deinterleaving history, 8 blocks instead of 4
	AudioFrameFIFO mSpeechQ;					///< output queue for speech frames
	unsigned stealBitsU[8], stealBitsL[8];	// (pat 1-16-2014) These are single bits; the upper and lower stealing bits found in incoming bursts.

	public:
	TCHFACCHL1Decoder(unsigned wCN, unsigned wTN, 
			   const TDMAMapping& wMapping,
			   L1FEC *wParent);

	ChannelType channelType() const { return FACCHType; }

	/** TCH/FACCH has a special-case writeLowSide. */
	void writeLowSideRx(const RxBurst& inBurst);

	/**
		Unlike other DCCHs, TCH/FACCH process burst calls
		deinterleave, decode, handleGoodFrame.
	*/
	bool processBurst( const RxBurst& );

	void saveMi();
	void restoreMi();
	void decrypt(int B);

	/** Deinterleave i[] to c[].  */
	// (pat) This overrides the deinterleave in SharedL1Decoder.
	// Note also that we use SharedL1Decoder::decode() for control frames, but TCHFRL1Decoder::decodeTCH() for TCH frames.
	void deinterleaveTCH(int blockOffset );

	void deinterleave() { assert(0); }		// We must not use the inherited SharedL1Decoder::deinterleave().

	/**
		Receive a traffic frame.
		Non-blocking.  Returns NULL if queue is dry.
		Caller is responsible for deleting the returned array.
	*/
	SIP::AudioFrame *recvTCH() { return mSpeechQ.read(0); }
	void addToSpeechQ(SIP::AudioFrame *newFrame);	// write the audio frame into the mSpeechQ

	/** Return count of internally-queued traffic frames. */
	unsigned queueSize() const { return mSpeechQ.size(); }
	const char* descriptiveString() const { return L1Decoder::descriptiveString(); }
	//string debugId() const { static string id; return id.size() ? id : (id=format("TCHFACCHL1Decoder %s ",descriptiveString())); }

	// (pat) 3-29: Moved this higher in the hierarchy so it can be shared with SDCCH.
	//bool uplinkLost() const;
};



/**
	This is base class for output-only encoders.
	These all have very thin L2/L3 and are driven by a clock instead of a FIFO.
*/
class GeneratorL1Encoder :
	public L1Encoder
{

	private:

	Thread mSendThread;

	public:

	GeneratorL1Encoder(	
		unsigned wCN,
		unsigned wTN,
		const TDMAMapping& wMapping,
		L1FEC* wParent)
		:L1Encoder(wCN,wTN,wMapping,wParent)
	{ }

	void serviceStart();

	protected: 

	/** The generate method actually produces output bursts. */
	virtual void generate() =0;

	/** The core service loop calls generate repeatedly. */
	void serviceLoop();

	/** Provide a C interface for pthreads. */
	friend void *GeneratorL1EncoderServiceLoopAdapter(GeneratorL1Encoder*);

};


void *GeneratorL1EncoderServiceLoopAdapter(GeneratorL1Encoder*);


/**
	The L1 encoder for the sync channel (SCH).
	The SCH sends out an encoding of the current BTS clock.
	GSM 05.03 4.7.
*/
class SCHL1Encoder : public GeneratorL1Encoder {

	private:
	ViterbiR2O4 mVCoder;	///< nearly all GSM channels use the same convolutional code
	Parity mBlockCoder;			///< block parity coder
	BitVector2 mU;				///< u[], as per GSM 05.03 2.2
	BitVector2 mE;				///< e[], as per GSM 05.03 2.2
	BitVector2 mD;				///< d[], as per GSM 05.03 2.2 
	BitVector2 mP;				///< p[], as per GSM 05.03 2.2
	BitVector2 mE1;				///< first half of e[]
	BitVector2 mE2;				///< second half of e[]

	public:

	SCHL1Encoder(L1FEC* wParent,unsigned wTN);

	const char* descriptiveString() const { return "SCH"; }

	protected:

	void generate();

};




/**
	The L1 encoder for the frequency correction channel (FCCH).
	The FCCH just sends bursts of zeroes at set points in the TDMA pattern.
	See GSM 05.02 5.2.4.
*/
class FCCHL1Encoder : public GeneratorL1Encoder {

	public:

	FCCHL1Encoder(L1FEC *wParent, unsigned wTN);
	const char* descriptiveString() const { return "FCCH"; }
	//string debugId() const { static string id; return id.size() ? id : (id=format("FCCHL1Encoder %s ",descriptiveString())); }

	protected:

	void generate();
};




/**
	L1 encoder for repeating non-dedicated control channels (BCCH).
	This have generator-like drive loops, but xCCH-like FEC.
*/
class NDCCHL1Encoder : public XCCHL1Encoder {

	protected:

	Thread mSendThread;

	public:


	NDCCHL1Encoder(
		unsigned wCN,
		unsigned wTN,
		const TDMAMapping& wMapping,
		L1FEC *wParent)
		:XCCHL1Encoder(wCN, wTN, wMapping, wParent)
	{ }

	void serviceStart();

	protected:

	virtual void generate() =0;

	/** The core service loop. */
	void serviceLoop();

	friend void *NDCCHL1EncoderServiceLoopAdapter(NDCCHL1Encoder*);
	//string debugId() const { static string id; return id.size() ? id : (id=format("NDCCHL1Encoder %s ",descriptiveString())); }
};

void *NDCCHL1EncoderServiceLoopAdapter(NDCCHL1Encoder*);



/**
	L1 encoder for the BCCH has generator filling behavior but xCCH-like FEC.
*/
class BCCHL1Encoder : public NDCCHL1Encoder {

	public:

	BCCHL1Encoder(L1FEC *wParent, unsigned wTN)
		:NDCCHL1Encoder(0,wTN,gBCCHMapping,wParent)
	{}
	const char* descriptiveString() const { return "BCCH"; }
	//string debugId() const { static string id; return id.size() ? id : (id=format("BCCHL1Encoder %s ",descriptiveString())); }

	private:

	void generate();
};


/**
	L1 encoder for the SACCH.
	Like any other control channel, but with hooks for power/timing control.
	The SI5 and SI6 generation will be handled in higher layers.
*/
class SACCHL1Encoder : public XCCHL1Encoder {

	private:

	SACCHL1FEC *mSACCHParent;
	string mSacchDescriptiveString;	// In L1Encoder

	/**@name Physical header, GSM 04.04 6, 7.1, 7.2 */
	//@{
	volatile float mOrderedMSPower;			///< ordered MS tx power level, dBm
	volatile float mOrderedMSTiming;		///< ordered MS timing advance in symbols
	//@}

	public:

	SACCHL1Encoder(unsigned wCN, unsigned wTN, const TDMAMapping& wMapping, SACCHL1FEC *wParent);

	// (pat) commented out: never used.
	//void orderedMSPower(int power) { mOrderedMSPower = power; }
	//void orderedMSTiming(int timing) { mOrderedMSTiming = timing; }
	void setMSPower(float orderedMSPower);
	void setMSTiming(float orderedTiming);

	void setPhy(const SACCHL1Encoder&);
	void initPhy(float RSSI, float timingError);
	virtual const char* descriptiveString() const;

	/** Override open() to initialize power and timing. */
	void encInit();

	protected:
	// (pat 5-2014) bool encActive() const { return true; }	// (pat) WRONG!  The SACCH is deactivated before releasing the channel.

	//unused: SACCHL1FEC *SACCHParent() { return mSACCHParent; }

	SACCHL1Decoder *SACCHSibling();

	unsigned headerOffset() const { return 16; }

	/** A warpper to send an L2 frame with a physical header.  (pat) really really fast. */
	virtual void sendFrame(const L2Frame&);

	//string debugId() const { static string id; return id.size() ? id : (id=format("SACCHL1Encoder %s ",descriptiveString())); }

};



typedef XCCHL1Encoder SDCCHL1Encoder;


/** The Common Control Channel (CCCH).  Carries the AGCH, NCH, PCH. */
class CCCHL1Encoder : public XCCHL1Encoder {

	public:

	CCCHL1Encoder(const TDMAMapping& wMapping,
			L1FEC* wParent, unsigned wTN)
		:XCCHL1Encoder(0,wTN,wMapping,wParent)
	{}
	//string debugId() const { static string id; return id.size() ? id : (id=format("CCCHL1Encoder %s ",descriptiveString())); }

};


/** Cell Broadcast Channel (CBCH). */
class CBCHL1Encoder : public XCCHL1Encoder {

	public:

	CBCHL1Encoder(int wCN, int wTN, const TDMAMapping& wMapping, L1FEC* wParent)
		:XCCHL1Encoder(wCN,wTN,wMapping,wParent)
	{}

	/** Override sendFrame to meet sync requirements of GSM 05.02 6.5.4. */
	virtual void sendFrame(const L2Frame&);
	//string debugId() const { static string id; return id.size() ? id : (id=format("CBCHL1Encoder %s ",descriptiveString())); }

};






class SDCCHL1FEC : public L1FEC {

	public:

	SDCCHL1FEC(
		unsigned wCN,
		unsigned wTN,
		const MappingPair& wMapping)
		:L1FEC()
	{
		mEncoder = new SDCCHL1Encoder(wCN,wTN,wMapping.downlink(),this);
		mDecoder = new SDCCHL1Decoder(wCN,wTN,wMapping.uplink(),this);
	}
	//string debugId() const { static string id; return id.size() ? id : (id=format("SDCCHL1FEC %s ",descriptiveString())); }
};






class CBCHL1FEC : public L1FEC {

	public:

	CBCHL1FEC(int wCN, int wTN, const MappingPair& wMapping)
		:L1FEC()
	{
		mEncoder = new CBCHL1Encoder(wCN,wTN,wMapping.downlink(),this);
	}
	//string debugId() const { static string id; return id.size() ? id : (id=format("CBCHL1FEC %s ",descriptiveString())); }
};









class TCHFACCHL1FEC : public L1FEC {

protected:

	TCHFACCHL1Decoder * mTCHDecoder;
	TCHFACCHL1Encoder * mTCHEncoder;

	
public:


	TCHFACCHL1FEC(
		unsigned wCN,
		unsigned wTN,
		const MappingPair& wMapping)
		:L1FEC()
	{
		mTCHEncoder = new TCHFACCHL1Encoder(wCN, wTN, wMapping.downlink(), this );
		mEncoder = mTCHEncoder;
		mTCHDecoder = new TCHFACCHL1Decoder(wCN, wTN, wMapping.uplink(), this );	
		mDecoder = mTCHDecoder;
	}

	/** Send a traffic frame. */
	void sendTCH(SIP::AudioFrame * frame)
		{ assert(mTCHEncoder); mTCHEncoder->sendTCH(frame); }

	/**
		Receive a traffic frame.
		Returns a pointer that must be deleted by calls.
		Non-blocking.
		Returns NULL is no data available.
	*/
	//unsigned char* recvTCH()
	SIP::AudioFrame* recvTCH()
		{ assert(mTCHDecoder); return mTCHDecoder->recvTCH(); }

	unsigned queueSize() const
		{ assert(mTCHDecoder); return mTCHDecoder->queueSize(); }

	//string debugId() const { static string id; return id.size() ? id : (id=format("TCHFACCHL1FEC %s ",descriptiveString())); }
};



class SACCHL1FEC : public L1FEC {

	private:

	SACCHL1Decoder *mSACCHDecoder;
	SACCHL1Encoder *mSACCHEncoder;

	public:

	SACCHL1FEC(
		unsigned wCN,
		unsigned wTN,
		const MappingPair& wMapping)
		:L1FEC()
	{
		mSACCHEncoder = new SACCHL1Encoder(wCN,wTN,wMapping.downlink(),this);
		mEncoder = mSACCHEncoder;
		mSACCHDecoder = new SACCHL1Decoder(wCN,wTN,wMapping.uplink(),this);
		mDecoder = mSACCHDecoder;
	}

	SACCHL1Decoder *decoder() { return mSACCHDecoder; }
	SACCHL1Encoder *encoder() { return mSACCHEncoder; }
	//void sacchl1open1();
	//void sacchl1open2(float wRSSI, float wTimingError, double wTimestamp);

	virtual const char* descriptiveString() const
		{ return mSACCHEncoder->descriptiveString(); }

	/**@name Physical parameter access. */
	//@{
	MSPhysReportInfo *getPhysInfo() { return mSACCHDecoder->getPhysInfo(); }
	void RSSIBumpDown(int dB) { mSACCHDecoder->RSSIBumpDown(dB); }
	void setPhy(const SACCHL1FEC&);
	/*virtual*/ void l1InitPhy(float RSSI, float timingError, double wTimestamp);
	//@}
};








class LoopbackL1FEC : public L1FEC {

	public:

	LoopbackL1FEC(unsigned wCN, unsigned wTN)
		:L1FEC()
	{
		mEncoder = new XCCHL1Encoder(wCN,wTN,gLoopbackTestFullMapping,this);
		mDecoder = new SDCCHL1Decoder(wCN,wTN,gLoopbackTestFullMapping,this);
	}
};



/** The common control channel (CCCH). */
class CCCHL1FEC : public L1FEC {

	public:

	CCCHL1FEC(const TDMAMapping& wMapping, unsigned wTN)
		:L1FEC()
	{
		mEncoder = new CCCHL1Encoder(wMapping,this,wTN);
	}
};




/**
	A subclass for channels that have L2 and L3 so thin
	that they are handled as special cases.
	These are all broadcast and unicast channels.
*/
class NDCCHL1FEC : public L1FEC {

	public:

	NDCCHL1FEC():L1FEC() {}

	void upstream(L2LogicalChannelBase*){ assert(0);}
};


class FCCHL1FEC : public NDCCHL1FEC {

	public:

	FCCHL1FEC(unsigned wTN=0):NDCCHL1FEC()
	{
		mEncoder = new FCCHL1Encoder(this,wTN);
	}

};


class RACHL1FEC : public NDCCHL1FEC {

	public:

	RACHL1FEC(const TDMAMapping& wMapping, unsigned wTN)
		:NDCCHL1FEC()
	{
		mDecoder = new RACHL1Decoder(wMapping,this,wTN);
	}
};


class SCHL1FEC : public NDCCHL1FEC {

	public:

	SCHL1FEC(unsigned wTN=0):NDCCHL1FEC()
	{
		mEncoder = new SCHL1Encoder(this,wTN);
	}
};


class BCCHL1FEC : public NDCCHL1FEC {

	public:

	BCCHL1FEC(unsigned wTN=0):NDCCHL1FEC()
	{
		mEncoder = new BCCHL1Encoder(this,wTN);
	}
};





}; 	// namespace GSM





#endif

// vim: ts=4 sw=4
