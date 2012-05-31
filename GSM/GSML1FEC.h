/*
* Copyright 2008-2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/



#ifndef GSML1FEC_H
#define GSML1FEC_H

#include "Threads.h"
#include <assert.h>
#include "BitVector.h"

#include "GSMCommon.h"
#include "GSMTransfer.h"
#include "GSMTDMA.h"

#include "GSM610Tables.h"

#include <Globals.h>


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





/**
	Abstract class for L1 encoders.
	In most subclasses, writeHighSide() drives the processing.
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
	unsigned mTotalBursts;			///< total bursts sent since last open()
	GSM::Time mPrevWriteTime;		///< timestamp of pervious generated burst
	GSM::Time mNextWriteTime;		///< timestamp of next generated burst
	volatile bool mRunning;			///< true while the service loop is running
	bool mActive;					///< true between open() and close()
	//@}

	ViterbiR2O4 mVCoder;	///< nearly all GSM channels use the same convolutional code

	char mDescriptiveString[100];

	public:

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
	writeLowSide() drives the processing.
*/
class L1Decoder {

	protected:

	SAPMux * mUpstream;

	/**@name Mutex-controlled state information. */
	//@{
	mutable Mutex mLock;				///< access control
	/**@name Timers from GSM 04.08 11.1.2 */
	//@{
	Z100Timer mT3101;					///< timer for new channels
	Z100Timer mT3109;					///< timer for existing channels
	Z100Timer mT3111;					///< timer for reuse of a closed channel
	//@}
	bool mActive;						///< true between open() and close()
	//@}

	/**@name Atomic volatiles, no mutex. */
	// Yes, I realize we're violating our own rules here. -- DAB
	//@{
	volatile bool mRunning;						///< true if all required service threads are started
	volatile float mFER;						///< current FER estimate
	static const int mFERMemory=20;				///< FER decay time, in frames
	//@}

	/**@name Parameters fixed by the constructor, not requiring mutex protection. */
	//@{
	unsigned mCN;					///< carrier index
	unsigned mTN;					///< timeslot number 
	const TDMAMapping& mMapping;	///< demux parameters
	L1FEC* mParent;			///< a containing L1 processor, if any
	//@}

	ViterbiR2O4 mVCoder;	///< nearly all GSM channels use the same convolutional code


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
			mActive(false),
			mRunning(false),
			mFER(0.0F),
			mCN(wCN),mTN(wTN),
			mMapping(wMapping),mParent(wParent)
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
	void upstream(SAPMux * wUpstream)
	{
		assert(mUpstream==NULL);	// Only call this once.
		mUpstream=wUpstream;
	}

	/** Total frame error rate since last open(). */
	float FER() const { return mFER; }

	/** Return the multiplexing parameters. */
	const TDMAMapping& mapping() const { return mMapping; }

	/** Accept an RxBurst and process it into the deinterleaver. */
	virtual void writeLowSide(const RxBurst&) = 0;

	/**@name Components of the channel description. */
	//@{
	unsigned TN() const { return mTN; }
	unsigned ARFCN() const;					///< this comes from mUpstream
	TypeAndOffset typeAndOffset() const;	///< this comes from mMapping
	//@}


	protected:

	virtual L1FEC* parent() { return mParent; }

	/** Return pointer to paired L1 encoder, if any. */
	virtual L1Encoder* sibling();

	/** Return pointer to paired L1 encoder, if any. */
	virtual const L1Encoder* sibling() const;

	/** Mark the decoder as started.  */
	virtual void start() { mRunning=true; }

	void countGoodFrame();

	void countBadFrame();
};





/**
	The L1FEC encapsulates an encoder and decoder.
*/
class L1FEC {

	protected:

	L1Encoder* mEncoder;
	L1Decoder* mDecoder;

	public:

	/**
		The L1FEC constructor is over-ridden for different channel types.
		But the default has no encoder or decoder.
	*/
	L1FEC():mEncoder(NULL),mDecoder(NULL) {}

	/** This is no-op because these channels should not be destroyed. */
	virtual ~L1FEC() {};

	/** Send in an RxBurst for decoding. */
	void writeLowSide(const RxBurst& burst)
		{ assert(mDecoder); mDecoder->writeLowSide(burst); }

	/** Send in an L2Frame for encoding and transmission. */
	void writeHighSide(const L2Frame& frame)
		{ assert(mEncoder); mEncoder->writeHighSide(frame); }

	/** Attach L1 to a downstream radio. */
	void downstream(ARFCNManager*);

	/** Attach L1 to an upstream SAPI mux and L2. */
	void upstream(SAPMux* mux)
		{ if (mDecoder) mDecoder->upstream(mux); }

	/**@name Ganged actions. */
	//@{
	void open();
	void close();
	//@}
	

	/**@name Pass-through actions. */
	//@{
	TypeAndOffset typeAndOffset() const
		{ assert(mEncoder); return mEncoder->typeAndOffset(); }

	unsigned TN() const
		{ assert(mEncoder); return mEncoder->TN(); }

	unsigned CN() const
		{ assert(mEncoder); return mEncoder->CN(); }

	unsigned TSC() const
		{ assert(mEncoder); return mEncoder->TSC(); }

	unsigned ARFCN() const
		{ assert(mEncoder); return mEncoder->ARFCN(); }

	float FER() const
		{ assert(mDecoder); return mDecoder->FER(); }

	bool recyclable() const
		{ assert(mDecoder); return mDecoder->recyclable(); }

	bool active() const;

	const TDMAMapping& txMapping() const
		{ assert(mEncoder); return mEncoder->mapping(); }

	const TDMAMapping& rcvMapping() const
		{ assert(mDecoder); return mDecoder->mapping(); }

	const char* descriptiveString() const
		{ assert(mEncoder); return mEncoder->descriptiveString(); }

	//@}


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

	void writeLowSide(const RxBurst&);
	void writeHighSide(const L2Frame&);

	void downstream(ARFCNManager *wDownstream) { mDownstream=wDownstream; }
	void upstream(SAPMux * wUpstream){ mUpstream=wUpstream;}
};


/** L1 decoder for Random Access (RACH). */
class RACHL1Decoder : public L1Decoder {

	private:

	/**@name FEC state. */
	//@{
	Parity mParity;					///< block coder
	BitVector mU;					///< u[], as per GSM 05.03 2.2
	BitVector mD;					///< d[], as per GSM 05.03 2.2
	//@}

	// The RACH channel uses an internal FIFO,
	// because the channel allocation process might block
	// and we don't want to block the radio receive thread.
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
	void writeLowSide(const RxBurst&);

	/** A loop to watch the FIFO. */
	void serviceLoop();

	/** A "C" calling interface for pthreads. */
	friend void *RACHL1DecoderServiceLoopAdapter(RACHL1Decoder*);
};

void *RACHL1DecoderServiceLoopAdapter(RACHL1Decoder*);



/** Abstract L1 decoder for most control channels -- GSM 05.03 4.1 */
class XCCHL1Decoder : public L1Decoder {

	protected:

	/**@name FEC state. */
	//@{
	Parity mBlockCoder;
	SoftVector mI[4];			///< i[][], as per GSM 05.03 2.2
	SoftVector mC;				///< c[], as per GSM 05.03 2.2
	BitVector mU;				///< u[], as per GSM 05.03 2.2
	BitVector mP;				///< p[], as per GSM 05.03 2.2
	BitVector mDP;				///< d[]:p[] (data & parity)
	BitVector mD;				///< d[], as per GSM 05.03 2.2
	//@}

	GSM::Time mReadTime;		///< timestamp of the first burst
	unsigned mRSSIHistory[4];

	public:

	XCCHL1Decoder(unsigned wCN, unsigned wTN, const TDMAMapping& wMapping,
		L1FEC *wParent);

	protected:

	/** Offset to the start of the L2 header. */
	virtual unsigned headerOffset() const { return 0; }

	/** The channel type. */
	virtual ChannelType channelType() const = 0;

	/** Accept a timeslot for processing and drive data up the chain. */
	virtual void writeLowSide(const RxBurst&);

	/**
	  Accept a new timeslot for processing and save it in i[].
	  This virtual method works for all block-interleaved channels (xCCHs).
	  A different method is needed for diagonally-interleaved channels (TCHs).
	  @return true if a new frame is ready for deinterleaving.
	*/
	virtual bool processBurst(const RxBurst&);
	
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
	unsigned mRSSICounter;
	volatile float mRSSI[4];			///< RSSI history , dB wrt full scale
	volatile float mTimingError[4];		///< Timing error histoty in symbol
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
		mRSSICounter(0)
	{
		for (int i=0; i<4; i++) mRSSI[i]=0.0F;
	}

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
	void setPhy(float wRSSI, float wTimingError);

	void setPhy(const SACCHL1Decoder& other);

	/** RSSI of most recent received burst, in dB wrt full scale. */
	float RSSI() const;

	/**
		Timing error of most recent received burst, symbol units.
		Positive is late; negative is early.
	*/
	float timingError() const;


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
class XCCHL1Encoder : public L1Encoder {

	protected:

	/**@name FEC signal processing state.  */
	//@{
	Parity mBlockCoder;			///< block coder for this channel
	BitVector mI[4];			///< i[][], as per GSM 05.03 2.2
	BitVector mC;				///< c[], as per GSM 05.03 2.2
	BitVector mU;				///< u[], as per GSM 05.03 2.2
	BitVector mD;				///< d[], as per GSM 05.03 2.2
	BitVector mP;				///< p[], as per GSM 05.03 2.2
	//@}

	public:

	XCCHL1Encoder(
		unsigned wCN,
		unsigned wTN,
		const TDMAMapping& wMapping,
		L1FEC* wParent);

	protected:

	/** Process pending incoming messages. */
	virtual void writeHighSide(const L2Frame&);

	/** Offset from the start of mU to the start of the L2 frame. */
	virtual unsigned headerOffset() const { return 0; }

	/** Send a single L2 frame.  */
	virtual void sendFrame(const L2Frame&);

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

};



/** L1 encoder used for full rate TCH and FACCH -- mostry from GSM 05.03 3.1 and 4.2 */
class TCHFACCHL1Encoder : public XCCHL1Encoder {

private:

	bool mPreviousFACCH;	///< A copy of the previous stealing flag state.
	size_t mOffset;			///< Current deinterleaving offset.

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

	/** Interleave c[] to i[].  GSM 05.03 4.1.4. */
	virtual void interleave(int blockOffset);

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

	SoftVector mI[8];	///< deinterleaving history, 8 blocks instead of 4
	BitVector mTCHU;					///< u[] (uncoded) in the spec
	BitVector mTCHD;					///< d[] (data) in the spec
	SoftVector mClass1_c;				///< the class 1 part of c[]
	BitVector mClass1A_d;				///< the class 1A part of d[]
	SoftVector mClass2_c;				///< the class 2 part of c[]

	VocoderFrame mVFrame;				///< unpacking buffer for vocoder frame
	unsigned char mPrevGoodFrame[33];	///< previous good frame.

	Parity mTCHParity;

	InterthreadQueue<unsigned char> mSpeechQ;					///< output queue for speech frames


	public:

	TCHFACCHL1Decoder(unsigned wCN, unsigned wTN, 
			   const TDMAMapping& wMapping,
			   L1FEC *wParent);

	ChannelType channelType() const { return FACCHType; }


	/** TCH/FACCH has a special-case writeLowSide. */
	void writeLowSide(const RxBurst& inBurst);

	/**
		Unlike other DCCHs, TCH/FACCH process burst calls
		deinterleave, decode, handleGoodFrame.
	*/
	bool processBurst( const RxBurst& );
	
	/** Deinterleave i[] to c[].  */
	void deinterleave(int blockOffset );

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
class GeneratorL1Encoder : public L1Encoder {

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
	int actualMSPower() const { return mSACCHDecoder->actualMSPower(); }
	int actualMSTiming() const { return mSACCHDecoder->actualMSTiming(); }
	void setPhy(const SACCHL1FEC&);
	virtual void setPhy(float RSSI, float timingError);
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
