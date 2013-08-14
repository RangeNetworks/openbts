/*
* Copyright 2008-2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
*

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribuion.
*/



#ifndef GSML1FEC_H
#define GSML1FEC_H
#include "Defines.h"

#include "Threads.h"
#include <assert.h>
#include "BitVector.h"

#include "GSMCommon.h"
#include "GSMTransfer.h"
#include "GSMTDMA.h"

#include "a53.h"
#include "A51.h"

#include "GSM610Tables.h"

#include <Globals.h>

#include "../GPRS/GPRSExport.h"


class ARFCNManager;

namespace GSM {


/* forward refs */
class GSMConfig;

class SAPMux;

class L1FEC;
class L1Encoder;
class L1Decoder;
class GeneratorL1Encoder;
class SACCHL1Encoder;
class SACCHL1Decoder;
class SACCHL1FEC;
class TrafficTranscoder;

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

	ARFCNManager *mDownstream;
	TxBurst mBurst;					///< a preformatted burst template
	TxBurst mFillerBurst;			///< the filler burst for this channel

	/**@name Config items that don't change. */
	//@{
	const TDMAMapping& mMapping;	///< multiplexing description
	unsigned mCN;					///< carrier index
	unsigned mTN;					///< timeslot number to use
	unsigned mTSC;					///< training sequence for this channel
	L1FEC *mParent;					///< a containing L1FEC, if any
	//@}

	/**@name Multithread access control and data shared across threads. */
	//@{
	mutable Mutex mLock;
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
	unsigned mTotalBursts;			///< total bursts sent since last open()
	GSM::Time mPrevWriteTime;		///< timestamp of pervious generated burst
	GSM::Time mNextWriteTime;		///< timestamp of next generated burst

	volatile bool mRunning;			///< true while the service loop is running
	bool mActive;					///< true between open() and close()
	//@}

	// (pat) Moved to classes that need the convolutional coder.
	//ViterbiR2O4 mVCoder;	///< nearly all GSM channels use the same convolutional code

	char mDescriptiveString[100];

	public:

	EncryptionType mEncrypted;
	int mEncryptionAlgorithm;

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
	void transmit(BitVector *mI, BitVector *mE, const int *qbits);

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
	virtual void open();

	/** Set mDownstream handover correlator mode. */
	void handoverPending(bool flag);

	/**
		Returns true if the channel is in use by a transaction.
		For broadcast and unicast channels this is always true.
		For dedicated channels, this is taken from the sibling deocder.
	*/
	virtual bool active() const;

	/**
	  Process pending L2 frames and/or generate filler and enqueue the resulting timeslots.
	  This method may block briefly, up to about 1/2 second.
	  This method is meaningless for some suclasses.
	*/
	virtual void writeHighSide(const L2Frame&) { assert(0); }

	/** Start the service loop thread, if there is one.  */
	virtual void start() { mRunning=true; }

	const char* descriptiveString() const { return mDescriptiveString; }

	L1FEC* parent() { return mParent; }

	GSM::Time getNextWriteTime() { resync(); return mNextWriteTime; }

	protected:

	/** Roll write times forward to the next positions. */
	void rollForward();

	/** Return pointer to paired L1 decoder, if any. */
	virtual L1Decoder* sibling();

	/** Return pointer to paired L1 decoder, if any. */
	virtual const L1Decoder* sibling() const;

	/** Make sure we're consistent with the current clock.  */
	void resync();

	/** Block until the BTS clock catches up to mPrevWriteTime.  */
	void waitToSend() const;

	/**
		Send the idle filling pattern, if any.
		The default is a dummy burst.
	*/
	virtual void sendIdleFill();

};


/**
	An abstract class for L1 decoders.
	writeLowSideRx() drives the processing.
	// (pat) base class for: RACHL1Decoder, XCCHL1Decoder
	// It would be more elegant to split this into two classes: a base class
	// for both GPRS and RR, and the rest of this class that is RR specific.
*/
class L1Decoder {

	protected:

	// (pat) Not used for GPRS
	SAPMux * mUpstream;

	/**@name Mutex-controlled state information. */
	//@{
	mutable Mutex mLock;				///< access control
	/**@name Timers from GSM 04.08 11.1.2 */
	//@{
	Z100Timer mT3101;					///< timer for new channels
	Z100Timer mT3109;					///< timer for existing channels
	Z100Timer mT3111;					///< timer for reuse of a closed channel
	Z100Timer mT3103;					///< timer for handover
	//@}
	bool mActive;						///< true between open() and close()
	//@}

	/**@name Atomic volatiles, no mutex. */
	// Yes, I realize we're violating our own rules here. -- DAB
	//@{
	volatile bool mRunning;						///< true if all required service threads are started
	volatile float mFER;						///< current FER estimate
	static const int mFERMemory=20;				///< FER decay time, in frames
	volatile bool mHandoverPending;				///< if true, we are decoding handover bursts
	//@}

	/**@name Parameters fixed by the constructor, not requiring mutex protection. */
	//@{
	unsigned mCN;					///< carrier index
	unsigned mTN;					///< timeslot number 
	const TDMAMapping& mMapping;	///< demux parameters
	L1FEC* mParent;			///< a containing L1 processor, if any
	//@}

	// (pat) Moved to classes that use the convolutional coder.
	//ViterbiR2O4 mVCoder;	///< nearly all GSM channels use the same convolutional code

	EncryptionType mEncrypted;
	int mEncryptionAlgorithm;
	unsigned char mKc[8];
	int mFN[8];


	public:

	/**
		Constructor for an L1Decoder.
		@param wTN The timeslot to decode on.
		@param wMapping Demux parameters, MUST BE PERSISTENT.
		@param wParent The containing L1FEC, for sibling access.
	*/
	L1Decoder(unsigned wCN, unsigned wTN, const TDMAMapping& wMapping, L1FEC* wParent)
			:mUpstream(NULL),
			mT3101(T3101ms),mT3109(T3109ms),mT3111(T3111ms),
			mT3103(gConfig.getNum("GSM.Timer.T3103")),
			mActive(false),
			mRunning(false),
			mFER(0.0F),
			mCN(wCN),mTN(wTN),
			mMapping(wMapping),mParent(wParent),
			mEncrypted(ENCRYPT_NO),
			mEncryptionAlgorithm(0)
	{
		// Start T3101 so that the channel will
		// become recyclable soon.
		mT3101.set();
	}


	virtual ~L1Decoder() { }


	/**
		Clear the decoder for a new transaction.
		Start T3101, stop the others.
	*/
	virtual void open();

	/**
		Call this at the end of a tranaction.
		Stop timers.  If !hardRelase, start T3111.
	*/
	virtual void close(bool hardRelease=false);

	/**
		Returns true if the channel is in use for a transaction.
		Returns true if T3111 is not active.
	*/
	bool active() const;

	/** Return true if any timer is expired. */
	bool recyclable() const;

	/** Connect the upstream SAPMux and L2.  */
	virtual void upstream(SAPMux * wUpstream)
	{
		assert(mUpstream==NULL);	// Only call this once.
		mUpstream=wUpstream;
	}

	/** Total frame error rate since last open(). */
	float FER() const { return mFER; }

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

	/** Control the processing of handover access busts. */
	void handoverPending(bool flag)
	{
		if (flag) mT3103.set();
		mHandoverPending=flag;
	}

	public:
	L1FEC* parent() { return mParent; }	// pat thinks it is not used virtual.

	/** How much time left in T3101? */
	long debug3101remaining() { return mT3101.remaining(); }

	protected:

	/** Return pointer to paired L1 encoder, if any. */
	virtual L1Encoder* sibling();

	/** Return pointer to paired L1 encoder, if any. */
	virtual const L1Encoder* sibling() const;

	/** Mark the decoder as started.  */
	virtual void start() { mRunning=true; }

	public:
	void countGoodFrame();
	void countBadFrame();

	bool decrypt_maybe(string wIMSI, int wA5Alg);
	unsigned char *kc() { return mKc; }
};





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
	L1FEC():mEncoder(NULL),mDecoder(NULL)
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
	virtual void upstream(SAPMux* mux)
		{ if (mDecoder) mDecoder->upstream(mux); }

	/** set encoder and decoder handover pending mode. */
	void handoverPending(bool flag);

	/**@name Ganged actions. */
	//@{
	void open();
	void close();
	//@}
	

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

	bool recyclable() const	// Can we reuse this channel yet?
		{ assert(mDecoder); return mDecoder->recyclable(); }

	bool active() const;	// Channel in use?  See L1Encoder

	// (pat) This lovely function is unsed.
	// TRXManager.cpp:installDecoder uses L1Decoder::mapping() directly.
	const TDMAMapping& txMapping() const
		{ assert(mEncoder); return mEncoder->mapping(); }

	// (pat) This function is unsed.
	const TDMAMapping& rcvMapping() const
		{ assert(mDecoder); return mDecoder->mapping(); }

	const char* descriptiveString() const
		{ assert(mEncoder); return mEncoder->descriptiveString(); }

	//@}

	//void setDecoder(L1Decoder*me) { mDecoder = me; }
	//void setEncoder(L1Encoder*me) { mEncoder = me; }
	ARFCNManager *getRadio() { return mEncoder->getRadio(); }
	bool inUseByGPRS() { return mGprsReserved; }
	void setGPRS(bool reserved, GPRS::PDCHL1FEC *pch) { mGprsReserved = reserved; mGPRSFEC = pch; }

	L1Decoder* decoder() { return mDecoder; }
	L1Encoder* encoder() { return mEncoder; }
}; 


/**
	The TestL1FEC does loopbacks at each end.
*/
class TestL1FEC : public L1FEC {

	private:

	SAPMux * mUpstream;
	ARFCNManager* mDownstream;

	public:

	void writeLowSideRx(const RxBurst&);
	void writeHighSide(const L2Frame&);

	void downstream(ARFCNManager *wDownstream) { mDownstream=wDownstream; }
	void upstream(SAPMux * wUpstream){ mUpstream=wUpstream;}
};


/** L1 decoder for Random Access (RACH). */
class RACHL1Decoder : public L1Decoder
{

	private:

	/**@name FEC state. */
	//@{
	ViterbiR2O4 mVCoder;	///< nearly all GSM channels use the same convolutional code
	Parity mParity;					///< block coder
	BitVector mU;					///< u[], as per GSM 05.03 2.2
	BitVector mD;					///< d[], as per GSM 05.03 2.2
	//@}

	// The RACH channel uses an internal FIFO,
	// because the channel allocation process might block
	// and we don't want to block the radio receive thread.
	// (pat) I dont think this is used.  I think TRXManager calls writeLowSideRx directly.
	// The serviceLoop is still started, and watches mQ forever, hopefully
	// waiting for a burst that never comes.
	RxBurstFIFO	mQ;					///< a FIFO to decouple the rx thread

	Thread mServiceThread;			///< a thread to process the FIFO


	public:

	RACHL1Decoder(const TDMAMapping &wMapping,
		L1FEC *wParent)
		:L1Decoder(0,0,wMapping,wParent),
		mParity(0x06f,6,8),mU(18),mD(mU.head(8))
	{ }

	/** Start the service thread. */
	void start();

	/** Decode the burst and call the channel allocator. */
	void writeLowSideRx(const RxBurst&);

	/** A loop to watch the FIFO. */
	void serviceLoop();

	/** A "C" calling interface for pthreads. */
	friend void *RACHL1DecoderServiceLoopAdapter(RACHL1Decoder*);
};

void *RACHL1DecoderServiceLoopAdapter(RACHL1Decoder*);




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
	ViterbiR2O4 mVCoder;	///< nearly all GSM channels use the same convolutional code
    Parity mBlockCoder;
    BitVector mC;               ///< c[], as per GSM 05.03 2.2
    BitVector mU;               ///< u[], as per GSM 05.03 2.2
    //BitVector mDP;              ///< d[]:p[] (data & parity)
    BitVector mP;               ///< p[], as per GSM 05.03 2.2
	public:
    BitVector mD;               ///< d[], as per GSM 05.03 2.2		Incoming Data.
    BitVector mI[4];           ///< i[][], as per GSM 05.03 2.2	Outgoing Data.
    BitVector mE[4];

	/**
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
	void encodeFrame41(const BitVector &frame, int offset, bool copy=true);
	void initInterleave(int);
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
    BitVector mU;               ///< u[], as per GSM 05.03 2.2
    BitVector mP;               ///< p[], as per GSM 05.03 2.2
    BitVector mDP;              ///< d[]:p[] (data & parity)
	public:
    BitVector mD;               ///< d[], as per GSM 05.03 2.2
    SoftVector mE[4];
    SoftVector mI[4];           ///< i[][], as per GSM 05.03 2.2
	/**@name Handover Access Burst FEC state. */
	//@{
	Parity mHParity;			///< block coder for handover access bursts
	BitVector mHU;				///< u[] for handover access, as per GSM 05.03 4.6
	BitVector mHD;				///< d[] for handover access, as per GSM 05.03 4.6
	//@}
    //@}

	GSM::Time mReadTime;        ///< timestamp of the first burst

	public:

    SharedL1Decoder();

    void deinterleave();
    bool decode();
	SoftVector *result() { return mI; }
};


/** Abstract L1 decoder for most control channels -- GSM 05.03 4.1 */
class XCCHL1Decoder :
	public SharedL1Decoder,
	public L1Decoder
{

	protected:

	// Moved to SharedL1Decoder
#if 0
	/**@name FEC state. */
	//@{
	/**@name Normal Burst FEC state. */
	//@{
	Parity mBlockCoder;			///< block coder for normal bursts
	SoftVector mI[4];			///< i[][], as per GSM 05.03 2.2
	SoftVector mC;				///< c[], as per GSM 05.03 2.2
	BitVector mU;				///< u[], as per GSM 05.03 2.2
	BitVector mP;				///< p[], as per GSM 05.03 2.2
	BitVector mDP;				///< d[]:p[] (data & parity)
	BitVector mD;				///< d[], as per GSM 05.03 2.2
	//@}
	/**@name Handover Access Burst FEC state. */
	//@{
	Parity mHParity;			///< block coder for handover access bursts
	BitVector mHU;				///< u[] for handover access, as per GSM 05.03 4.6
	BitVector mHD;				///< d[] for handover access, as per GSM 05.03 4.6
	//@}
	//@}
#endif

	public:

	XCCHL1Decoder(unsigned wCN, unsigned wTN, const TDMAMapping& wMapping,
		L1FEC *wParent);

	void saveMi();
	void restoreMi();
	void decrypt();

	protected:

	/** Offset to the start of the L2 header. */
	virtual unsigned headerOffset() const { return 0; }

	/** The channel type. */
	virtual ChannelType channelType() const = 0;

	/** Accept a timeslot for processing and drive data up the chain. */
	virtual void writeLowSideRx(const RxBurst&);

	/**
	  Accept a new timeslot for processing and save it in i[].
	  This virtual method works for all block-interleaved channels (xCCHs).
	  A different method is needed for diagonally-interleaved channels (TCHs).
	  @return true if a new frame is ready for deinterleaving.
	*/
	virtual bool processBurst(const RxBurst&);
	
	// Moved to SharedL1Encoder.
#if 0
	/**
	  Deinterleave the i[] to c[].
	  This virtual method works for all block-interleaved channels (xCCHs).
	  A different method is needed for diagonally-interleaved channels (TCHs).
	*/
	virtual void deinterleave();

	/**
	  Decode the frame and send it upstream.
	  Includes LSB-MSB reversal within each octet.
	  @return True if frame passed parity check.
	 */
	bool decode();
#endif
	
	/** Finish off a properly-received L2Frame in mU and send it up to L2. */
	virtual void handleGoodFrame();
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

};


/**
	L1 decoder for the SACCH.
	Like any other control channel, but with hooks for power/timing control.
*/
class SACCHL1Decoder : public XCCHL1Decoder {

	private:

	SACCHL1FEC *mSACCHParent;
	volatile float mRSSI;			///< most recent RSSI, dB wrt full scale
	volatile float mTimingError;		///< Timing error history in symbols
	volatile double mTimestamp;		///< system time of most recent received burst
	volatile int mActualMSPower;		///< actual MS tx power in dBm
	volatile int mActualMSTiming;		///< actual MS tx timing advance in symbols

	public:

	SACCHL1Decoder(
		unsigned wCN,
		unsigned wTN,
		const TDMAMapping& wMapping,
		SACCHL1FEC *wParent)
		:XCCHL1Decoder(wCN,wTN,wMapping,(L1FEC*)wParent),
		mSACCHParent(wParent),
		mRSSI(0.0F),
		mTimingError(0.0F),
		mTimestamp(0.0),
		mActualMSPower(0),
		mActualMSTiming(0)
	{ }

	ChannelType channelType() const { return SACCHType; }

	int actualMSPower() const { return mActualMSPower; }
	int actualMSTiming() const { return mActualMSTiming; }

	/** Override open() to set physical parameters with reasonable defaults. */
	void open();

	/**
		Override processBurst to catch the physical parameters.
	*/
	bool processBurst(const RxBurst&);
	
	/** Set pyshical parameters for initialization. */
	void setPhy(float wRSSI, float wTimingError, double wTimestamp);

	void setPhy(const SACCHL1Decoder& other);

	/** RSSI of most recent received burst, in dB wrt full scale. */
	float RSSI() const { return mRSSI; }
	
	/** Artificially push down RSSI to induce the handset to push more power. */
	void RSSIBumpDown(float dB) { mRSSI -= dB; }

	/**
		Timing error of most recent received burst, symbol units.
		Positive is late; negative is early.
	*/
	float timingError() const { return mTimingError; }

	/** Timestamp of most recent received burst. */
	double timestamp() const { return mTimestamp; }


	protected:

	SACCHL1FEC *SACCHParent() { return mSACCHParent; }

	SACCHL1Encoder* SACCHSibling();

	/**
		This is a wrapper on handleGoodFrame that processes the physical header.
	*/
	void handleGoodFrame();

	unsigned headerOffset() const { return 16; }

};




/** L1 encoder used for many control channels -- mostly from GSM 05.03 4.1 */
class XCCHL1Encoder :
	public SharedL1Encoder,
	public L1Encoder
{

	protected:

	// Moved to SharedL1Encoder
#if 0
	/**@name FEC signal processing state.  */
	//@{
	Parity mBlockCoder;			///< block coder for this channel
	BitVector mI[4];			///< i[][], as per GSM 05.03 2.2
	BitVector mC;				///< c[], as per GSM 05.03 2.2
	BitVector mU;				///< u[], as per GSM 05.03 2.2
	BitVector mD;				///< d[], as per GSM 05.03 2.2
	BitVector mP;				///< p[], as per GSM 05.03 2.2
	//@}
#endif

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
	// Moved to SharedL1Encoder
	//virtual void transmit(BitVector *mI);
#if 0
	/**
	  Encode u[] to c[].
	  Includes LSB-MSB reversal within each octet.
	*/
	void encode();

	/**
	  Interleave c[] to i[].
	  GSM 05.03 4.1.4.
	*/
	virtual void interleave();

	/**
	  Format i[] into timeslots and send them down for transmission.
	  Set stealing flags assuming a control channel.
	  Also updates mWriteTime.
	  GSM 05.03 4.1.5, 05.02 5.2.3.
	*/
	virtual void transmit();
#endif

};



/** L1 encoder used for full rate TCH and FACCH -- mostry from GSM 05.03 3.1 and 4.2 */
class TCHFACCHL1Encoder : public XCCHL1Encoder {

private:

	bool mPreviousFACCH;	///< A copy of the previous stealing flag state.
	size_t mOffset;			///< Current deinterleaving offset.

	BitVector mE[8];
	// (pat) Yes, the mI here duplicates but overrides the same
	// vector down in XCCHL1Encoder.
	BitVector mI[8];			///< deinterleaving history, 8 blocks instead of 4
	BitVector mTCHU;				///< u[], but for traffic
	BitVector mTCHD;				///< d[], but for traffic
	BitVector mClass1_c;			///< the class 1 part of taffic c[]
	BitVector mClass1A_d;			///< the class 1A part of taffic d[]
	BitVector mClass2_d;			///< the class 2 part of d[]

	BitVector mFillerC;				///< copy of previous c[] for filling dead time

	Parity mTCHParity;

	VocoderFrameFIFO mSpeechQ;		///< input queue for speech frames

	L2FrameFIFO mL2Q;				///< input queue for L2 FACCH frames

	Thread mEncoderThread;
	friend void TCHFACCHL1EncoderRoutine( TCHFACCHL1Encoder * encoder );	

public:

	TCHFACCHL1Encoder(unsigned wCN, unsigned wTN, 
			  const TDMAMapping& wMapping,
			  L1FEC* wParent);

	/** Enqueue a traffic frame for transmission. */
	void sendTCH(const unsigned char *frame)
		{ mSpeechQ.write(new VocoderFrame(frame)); }

	/** Extend open() to set up semaphores. */
	void open();

protected:

	// GSM 05.03, 3.1.3
	void interleave31(int blockOffset);
#if 0
	/** Interleave c[] to i[].  GSM 05.03 4.1.4. */
	virtual void interleave31(int blockOffset);
#endif

	/** Encode a FACCH and enqueue it for transmission. */
	void sendFrame(const L2Frame&);

	/**
		dispatch called in a while loop.
		process reading transcoder and fifo to 
		interleave and send.
	*/
	void dispatch();

	/** Will start the dispatch thread. */
	void start();

	/** Encode a vocoder frame into c[]. */
	void encodeTCH(const VocoderFrame& vFrame);

};


/** The C adapter for pthreads. */
void TCHFACCHL1EncoderRoutine( TCHFACCHL1Encoder * encoder );

/** L1 decoder used for full rate TCH and FACCH -- mostly from GSM 05.03 3.1 and 4.2 */
class TCHFACCHL1Decoder : public XCCHL1Decoder {

	protected:

	SoftVector mE[8];	///< deinterleaving history, 8 blocks instead of 4
	SoftVector mI[8];	///< deinterleaving history, 8 blocks instead of 4
	BitVector mTCHU;					///< u[] (uncoded) in the spec
	BitVector mTCHD;					///< d[] (data) in the spec
	SoftVector mClass1_c;				///< the class 1 part of c[]
	BitVector mClass1A_d;				///< the class 1A part of d[]
	SoftVector mClass2_c;				///< the class 2 part of c[]

	VocoderFrame mVFrame;		///< unpacking buffer for current vocoder frame
	VocoderFrame mPrevGoodFrame;	///< previous good frame

	Parity mTCHParity;

	InterthreadQueue<unsigned char> mSpeechQ;					///< output queue for speech frames


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
	void deinterleave(int blockOffset );

	// (pat) Routine does not exist.
	void replaceFACCH( int blockOffset );

	/**
		Decode a traffic frame from TCHI[] and enqueue it.
		Return true if there's a good frame.
	*/
	bool decodeTCH(bool stolen);

	/**
		Receive a traffic frame.
		Non-blocking.  Returns NULL if queue is dry.
		Caller is responsible for deleting the returned array.
	*/
	unsigned char *recvTCH() { return mSpeechQ.read(0); }

	/** Return count of internally-queued traffic frames. */
	unsigned queueSize() const { return mSpeechQ.size(); }

	/** Return true if the uplink is dead. */
	bool uplinkLost() const;
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

	void start();

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
	BitVector mU;				///< u[], as per GSM 05.03 2.2
	BitVector mE;				///< e[], as per GSM 05.03 2.2
	BitVector mD;				///< d[], as per GSM 05.03 2.2 
	BitVector mP;				///< p[], as per GSM 05.03 2.2
	BitVector mE1;				///< first half of e[]
	BitVector mE2;				///< second half of e[]

	public:

	SCHL1Encoder(L1FEC* wParent);

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

	FCCHL1Encoder(L1FEC *wParent);

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

	void start();

	protected:

	virtual void generate() =0;

	/** The core service loop. */
	void serviceLoop();

	friend void *NDCCHL1EncoderServiceLoopAdapter(NDCCHL1Encoder*);
};

void *NDCCHL1EncoderServiceLoopAdapter(NDCCHL1Encoder*);



/**
	L1 encoder for the BCCH has generator filling behavior but xCCH-like FEC.
*/
class BCCHL1Encoder : public NDCCHL1Encoder {

	public:

	BCCHL1Encoder(L1FEC *wParent)
		:NDCCHL1Encoder(0,0,gBCCHMapping,wParent)
	{}

	private:

	void generate();
};


/**
	L1 decoder for the SACCH.
	Like any other control channel, but with hooks for power/timing control.
	The SI5 and SI5 generation will be handled in higher layers.
*/
class SACCHL1Encoder : public XCCHL1Encoder {

	private:

	SACCHL1FEC *mSACCHParent;

	/**@name Physical header, GSM 04.04 6, 7.1, 7.2 */
	//@{
	volatile float mOrderedMSPower;			///< ordered MS tx power level, dBm
	volatile float mOrderedMSTiming;		///< ordered MS timing advance in symbols
	//@}

	public:

	SACCHL1Encoder(unsigned wCN, unsigned wTN, const TDMAMapping& wMapping, SACCHL1FEC *wParent);

	void orderedMSPower(int power) { mOrderedMSPower = power; }
	void orderedMSTiming(int timing) { mOrderedMSTiming = timing; }

	void setPhy(const SACCHL1Encoder&);
	void setPhy(float RSSI, float timingError);

	/** Override open() to initialize power and timing. */
	void open();

	//bool active() const { return true; }

	protected:

	SACCHL1FEC *SACCHParent() { return mSACCHParent; }

	SACCHL1Decoder *SACCHSibling();

	unsigned headerOffset() const { return 16; }

	/** A warpper to send an L2 frame with a physical header.  */
	virtual void sendFrame(const L2Frame&);

};



typedef XCCHL1Encoder SDCCHL1Encoder;


/** The Common Control Channel (CCCH).  Carries the AGCH, NCH, PCH. */
class CCCHL1Encoder : public XCCHL1Encoder {

	public:

	CCCHL1Encoder(const TDMAMapping& wMapping,
			L1FEC* wParent)
		:XCCHL1Encoder(0,0,wMapping,wParent)
	{}

};


/** Cell Broadcast Channel (CBCH). */
class CBCHL1Encoder : public XCCHL1Encoder {

	public:

	CBCHL1Encoder(const TDMAMapping& wMapping,
			L1FEC* wParent)
		:XCCHL1Encoder(0,0,wMapping,wParent)
	{}

	/** Override sendFrame to meet sync requirements of GSM 05.02 6.5.4. */
	virtual void sendFrame(const L2Frame&);

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
};






class CBCHL1FEC : public L1FEC {

	public:

	CBCHL1FEC(const MappingPair& wMapping)
		:L1FEC()
	{
		mEncoder = new CBCHL1Encoder(wMapping.downlink(),this);
	}
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
	void sendTCH(const unsigned char * frame)
		{ assert(mTCHEncoder); mTCHEncoder->sendTCH(frame); }

	/**
		Receive a traffic frame.
		Returns a pointer that must be deleted by calls.
		Non-blocking.
		Returns NULL is no data available.
	*/
	unsigned char* recvTCH()
		{ assert(mTCHDecoder); return mTCHDecoder->recvTCH(); }

	unsigned queueSize() const
		{ assert(mTCHDecoder); return mTCHDecoder->queueSize(); }

	bool radioFailure() const
		{ assert(mTCHDecoder); return mTCHDecoder->uplinkLost(); }
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

	/**@name Physical parameter access. */
	//@{
	float RSSI() const { return mSACCHDecoder->RSSI(); }
	float timingError() const { return mSACCHDecoder->timingError(); }
	double timestamp() const { return mSACCHDecoder->timestamp(); }
	int actualMSPower() const { return mSACCHDecoder->actualMSPower(); }
	int actualMSTiming() const { return mSACCHDecoder->actualMSTiming(); }
	void setPhy(const SACCHL1FEC&);
	virtual void setPhy(float RSSI, float timingError, double wTimestamp);
	void RSSIBumpDown(int dB) { mSACCHDecoder->RSSIBumpDown(dB); }
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

	CCCHL1FEC(const TDMAMapping& wMapping)
		:L1FEC()
	{
		mEncoder = new CCCHL1Encoder(wMapping,this);
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

	void upstream(SAPMux*){ assert(0);}
};


class FCCHL1FEC : public NDCCHL1FEC {

	public:

	FCCHL1FEC():NDCCHL1FEC()
	{
		mEncoder = new FCCHL1Encoder(this);
	}

};


class RACHL1FEC : public NDCCHL1FEC {

	public:

	RACHL1FEC(const TDMAMapping& wMapping)
		:NDCCHL1FEC()
	{
		mDecoder = new RACHL1Decoder(wMapping,this);
	}
};


class SCHL1FEC : public NDCCHL1FEC {

	public:

	SCHL1FEC():NDCCHL1FEC()
	{
		mEncoder = new SCHL1Encoder(this);
	}
};


class BCCHL1FEC : public NDCCHL1FEC {

	public:

	BCCHL1FEC():NDCCHL1FEC()
	{
		mEncoder = new BCCHL1Encoder(this);
	}
};





}; 	// namespace GSM





#endif

// vim: ts=4 sw=4
