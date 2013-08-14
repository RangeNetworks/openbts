/**@file Logical Channel.  */

/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
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




#ifndef LOGICALCHANNEL_H
#define LOGICALCHANNEL_H

#include <sys/types.h>
#include <pthread.h>

#include <iostream>

#include "GSML1FEC.h"
#include "GSMSAPMux.h"
#include "GSML2LAPDm.h"
#include "GSML3RRElements.h"
#include "GSMTDMA.h"
#include <TransactionTable.h>

#include <Logger.h>

class ARFCNManager;
class UDPSocket;


//namespace Control {
//class TransactionEntry;
//};


namespace GSM {

typedef InterthreadQueue<Control::TransactionEntry> TransactionFIFO;

class SACCHLogicalChannel;
class L3Message;
class L3RRMessage;
class L3SMSCBMessage;


/**
	A complete logical channel.
	Includes processors for L1, L2, L3, as needed.
	The layered structure of GSM is defined in GSM 04.01 7, as well as many other places.
	The concept of the logical channel and the channel types are defined in GSM 04.03.
	This is virtual class; specific channel types are subclasses.
*/
// (pat) It would be nice to break this into two classes: one that has the base functionality
// that GPRS will not use, and one with all the RR specific channel stuff.
class LogicalChannel {
	
protected:	

	/**@name Contained layer processors. */
	//@{
	L1FEC *mL1;			///< L1 forward error correction
	SAPMux mMux;		///< service access point multiplex
	L2DL *mL2[4];		///< data link layer state machines, one per SAP
	//@}

	SACCHLogicalChannel *mSACCH;	///< The associated SACCH, if any.

	/**
		A FIFO of inbound transactions intiated in the SIP layers on an already-active channel.
		Unlike most interthread FIFOs, do *NOT* delete the pointers that come out of it.
	*/
	TransactionFIFO mTransactionFIFO;

public:

	/**
		Blank initializer just nulls the pointers.
		Specific sub-class initializers allocate new components as needed.
	*/
	LogicalChannel()
		:mL1(NULL),mSACCH(NULL)
	{
		for (int i=0; i<4; i++) mL2[i]=NULL;
	}


	
	/** The destructor doesn't do anything since logical channels should not be destroyed. */
	virtual ~LogicalChannel() {};
	

	/**@name Accessors. */
	//@{
	SACCHLogicalChannel* SACCH() { return mSACCH; }
	const SACCHLogicalChannel* SACCH() const { return mSACCH; }
	L3ChannelDescription channelDescription() const;
	//@}


	/**@name Pass-throughs. */
	//@{

	// Pat 5-27-2012: Let the LogicalChannel know the next scheduled write time.
	GSM::Time getNextWriteTime() { return mL1->encoder()->getNextWriteTime(); }

	/** Set L1 physical parameters from a RACH or pre-exsting channel. */
	virtual void setPhy(float wRSSI, float wTimingError, double wTimestamp);

	/* Set L1 physical parameters from an existing logical channel. */
	virtual void setPhy(const LogicalChannel&);

	virtual const L3MeasurementResults& measurementResults() const;

	/**@name L3 interfaces */
	//@{

	/**
		Read an L3Frame from SAP0 uplink, blocking, with timeout.
		The caller is responsible for deleting the returned pointer.
		The default 15 second timeout works for most L3 operations.
		@param timeout_ms A read timeout in milliseconds.
		@param SAPI The service access point indicator from which to read.
		@return A pointer to an L3Frame, to be deleted by the caller, or NULL on timeout.
	*/
	virtual L3Frame * recv(unsigned timeout_ms = 15000, unsigned SAPI=0)
		{ assert(mL2[SAPI]); return mL2[SAPI]->readHighSide(timeout_ms); }

	/**
		Send an L3Frame on downlink.
		This method will block until the message is transferred to the transceiver.
		@param frame The L3Frame to be sent.
		@param SAPI The service access point indicator.
	*/
	virtual void send(const L3Frame& frame, unsigned SAPI=0)
	{
		// (pat) Note that writeHighSide is overloaded per class hierarchy, and is also used
		// for entirely unrelated classes, which are distinguishable (by humans,
		// not by the compiler, which considers them unrelated functions)
		// by arguments of L3Frame or L2Frame.
		//
		// For traffic channels:
		// This function calls virtual L2DL::writeHighSide(L3Frame) which I think maps
		// to L2LAPDm::writeHighSide() which interprets the primitive, and then
		// sends traffic data through sendUFrameUI(L3Frame) which creates an L2Frame
		// and sends it through several irrelevant functions to L2LAPDm::writeL1
		// which calls (SAPMux)mDownstream->SAPMux::writeHighSide(L2Frame),
		// which does nothing but call mL1->writeHighSide(L2Frame), which is a pass-through
		// except that the SapMux uses mDownStream which is copied from mL1, so there is a
		// chance to redirect it.  But wouldn't that be an error?
		// Anyway, L1Encoder::writeHighSide is usually overridden.
		// For TCH, it goes to XCCHL1Encoder::writeHighSide() which processes
		// the L2Frame primitive, then sends traffic data to TCHFACCHL1Encoder::sendFrame(),
		// which just enqueues the frame - it does not block.
		// A thread runs GSM::TCHFACCHL1EncoderRoutine() which
		// calls TCHFACCHL1Encoder::dispatch() which is synchronized with the gBTS clock,
		// unsynchronized with the queue, because it must send data no matter what.
		// Eventually it encodes the data and
		// calls (ARFCNManager*)mDownStream->writeHighSideTx(), which writes to the socket.
		//
		// For CCCH channels:
		// CCCHLogicalChannel::send(L3RRMessage) wraps the message in an L3Frame
		// and enqueues the message on CCCHLogicalChannel::mQ.
		// CCCHLogicalChannel::serviceLoop() pulls it out and sends it to
		// LogicalChannel::send(L3Frame) [this function], which is virtual, but I dont think it
		// is over-ridden, so message goes to L2DL::writeHighSide(L3Frame) which
		// is over-ridden to CCCHL2::writeHighSide(L3Frame) which creates an L2Frame
		// and calls (SAPMux)mDownstream->writeHighSide(L2Frame), which just
		// calls (L1FEC)mDownStream->writeHighSide(L2Frame), which
		// (because CCCHL1FEC is nearly empty) just
		// calls (L1Encoder)mEncoder->writeHighSide(L2Frame), which maps
		// to CCCHL1Encoder which maps to XCCHL1Encoder::writeHighSide(L2Frame),
		// which processes the L2Frame primitive, and sends traffic data to
		// XCCHL1Encoder::sendFrame(L2Frame), which encodes the frame and then calls
		// XCCHL1Encoder::transmit(implicit mI arg with encoded burst) that
		// finally blocks until L1Encoder::mPrevWriteTime occurs, then sets the
		// burst time to L1Encoder::mNextWriteTime and
		// calls (ARFCNManager*)mDownStream->writeHighSideTx() which writes to the socket.
		assert(mL2[SAPI]);
		LOG(DEBUG) << "SAP"<< SAPI << " " << frame;
		mL2[SAPI]->writeHighSide(frame);
	}

	/**
		Send "naked" primitive down the channel.
		@param prim The primitive to send.
		@pram SAPI The service access point on which to send.
	*/
	virtual void send(const GSM::Primitive& prim, unsigned SAPI=0)
		{ assert(mL2[SAPI]); mL2[SAPI]->writeHighSide(L3Frame(prim)); }

	/**
		Initiate a transaction from the SIP side on an already-active channel.
	(*/
	virtual void addTransaction(Control::TransactionEntry* transaction);

	/**
		Serialize and send an L3Message with a given primitive.
		@param msg The L3 message.
		@param prim The primitive to use.
	*/
	virtual void send(const L3Message& msg,
			const GSM::Primitive& prim=DATA,
			unsigned SAPI=0);

	/**
		Block on a channel until a given primitive arrives.
		Any payload is discarded.  Block indefinitely, no timeout.
		@param primitive The primitive to wait for.
	*/
	void waitForPrimitive(GSM::Primitive primitive);

	/** Block until a HANDOVER_ACCESS or ESTABLISH arrives. */
	L3Frame* waitForEstablishOrHandover();

	/**
		Block on a channel until a given primitive arrives.
		Any payload is discarded.  Block indefinitely, no timeout.
		@param primitive The primitive to wait for.
		@param timeout_ms The timeout in milliseconds.
		@return True on success, false on timeout.
	*/
	bool waitForPrimitive(GSM::Primitive primitive, unsigned timeout_ms);



	//@} // L3

	/**@name L1 interfaces */
	//@{

	/** Write a received radio burst into the "low" side of the channel. */
	virtual void writeLowSide(const RxBurst& burst) { assert(mL1); mL1->writeLowSideRx(burst); }

	/** Return true if the channel is safely abandoned (closed or orphaned). */
	virtual bool recyclable() const { assert(mL1); return mL1->recyclable(); }

	/** Return true if the channel is active. */
	virtual bool active() const { assert(mL1); return mL1->active(); }

	/** The TDMA parameters for the transmit side. */
	// (pat) This lovely function is unused.  Use L1Encoder::mapping()
	const TDMAMapping& txMapping() const { assert(mL1); return mL1->txMapping(); }

	/** The TDMAParameters for the receive side. */
	// (pat) This lovely function is unused.  Use L1Decoder::mapping()
	const TDMAMapping& rcvMapping() const { assert(mL1); return mL1->rcvMapping(); }

	/** GSM 04.08 10.5.2.5 type and offset code. */
	TypeAndOffset typeAndOffset() const { assert(mL1); return mL1->typeAndOffset(); }

	/** ARFCN */ /* TODO: Use this, or when obtaining the physical info use ARFCN from a diff location? */
	unsigned ARFCN() const { assert(mL1); return mL1->ARFCN(); }

	/**@name Channel stats from the physical layer */
	//@{
	/** Carrier index. */
	unsigned CN() const { assert(mL1); return mL1->CN(); }
	/** Slot number. */
	unsigned TN() const { assert(mL1); return mL1->TN(); }
	/** Receive FER. */
	float FER() const { assert(mL1); return mL1->FER(); }
	/** RSSI wrt full scale. */
	virtual float RSSI() const;
	/** Uplink timing error. */
	virtual float timingError() const;
	/** System timestamp of RSSI and TA */
	virtual double timestamp() const;
	/** Actual MS uplink power. */
	virtual int actualMSPower() const;
	/** Actual MS uplink timing advance. */
	virtual int actualMSTiming() const;
	/** Control whether to accept a handover. */
	void handoverPending(bool flag) { assert(mL1); mL1->handoverPending(flag); }
	//@}

	//@} // L1

	/**@name L2 passthroughs */
	//@{
	unsigned N200() const { assert(mL2[0]); return mL2[0]->N200(); }
	unsigned T200() const { assert(mL2[0]); return mL2[0]->T200(); }
	bool multiframeMode(unsigned SAPI) const
		{ assert(mL2[SAPI]); return mL2[SAPI]->multiframeMode(); }
	//@}

	//@} // passthrough


	/** Connect an ARFCN manager to link L1FEC to the radio. */
	void downstream(ARFCNManager* radio);

	/** Return the channel type. */
	virtual ChannelType type() const =0;

	/**
		Make the channel ready for a new transaction.
		The channel is closed with primitives from L3.
	*/
	virtual void open();

	/**@ Debuging functions: will give access to all intermediate layers. */
	//@{
	L2DL * debugGetL2(unsigned sapi){ return mL2[sapi]; }
	L1FEC * debugGetL1(){ return mL1; }
	//@}

	const char* descriptiveString() const { return mL1->descriptiveString(); }

	protected:

	/**
		Make the normal inter-layer connections.
		Should be called from inside the constructor after
		the channel components are created.
	*/
	virtual void connect();

	public:
	bool inUseByGPRS() { return mL1->inUseByGPRS(); }

	bool decryptUplink_maybe(string wIMSI, int wA5Alg) { return mL1->decoder()->decrypt_maybe(wIMSI, wA5Alg); }
};


std::ostream& operator<<(std::ostream&, const LogicalChannel&);


/**
	Standalone dedicated control channel.
	GSM 04.06 4.1.3: "A dedicated control channel (DCCH) is a point-to-point
	bi-directional or uni-directional control channel. ... A SDCCH (Stand-alone
	DCCH) is a bi-directional DCCH whose allocation is not linked to the
	allocation of a TCH.  The bit rate of a SDCCH is 598/765 kbit/s. 
"
*/
class SDCCHLogicalChannel : public LogicalChannel {

	public:
	
	SDCCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const CompleteMapping& wMapping);

	ChannelType type() const { return SDCCHType; }
};





/**
	Logical channel for NDCCHs that use Bbis format and a pseudolength.
	This is a virtual base class this is extended for CCCH & BCCH.
	See GSM 04.06 4.1.1, 4.1.3.
*/
class NDCCHLogicalChannel : public LogicalChannel {

	public:

	/** This channel only sends RR protocol messages. */
	virtual void send(const L3RRMessage& msg)
		{ LogicalChannel::send((const L3Message&)msg,UNIT_DATA); }

	/** This channel only sends RR protocol messages. */
	void send(const L3Message&) { assert(0); }

};






/**
	Slow associated control channel.

	GSM 04.06 4.1.3: "A SACCH (Slow Associated DCCH) is either a bi-directional or
	uni-directional DCCH of rate 115/300 or a bi- directional DCCH of rate
	299/765 kbit/s. An independent SACCH is always allocated together with a TCH
	or a SDCCH. The co-allocated TCH and SACCH shall be either both bi-directional
	or both uni-directional."

	We're going to cut a corner for the moment and give the SAACH a "thin" L2 that
	supports only the UNIT_DATA_* primitives (ie, no multiframe mode).  This is OK
	until we need to transfer SMS for an in-progress call.

	The main role of the SACCH, for now, will be to send SI5 and SI6 messages and
	to accept uplink mesaurement reports.
*/
class SACCHLogicalChannel : public LogicalChannel {

	protected:

	SACCHL1FEC *mSACCHL1;
	Thread mServiceThread;	///< a thread for the service loop
	bool mRunning;			///< true is the service loop is started

	/** MeasurementResults from the MS. They are caught in serviceLoop, accessed
	 for recording along with GPS and other data in MobilityManagement.cpp */
	L3MeasurementResults mMeasurementResults;
	const LogicalChannel *mHost;

	public:

	SACCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const MappingPair& wMapping,
		const LogicalChannel* wHost);

	ChannelType type() const { return SACCHType; }

	void open();

	friend void *SACCHLogicalChannelServiceLoopAdapter(SACCHLogicalChannel*);

	/**@name Pass-through accoessors to L1. */
	//@{
	float RSSI() const { return mSACCHL1->RSSI(); }
	float timingError() const { return mSACCHL1->timingError(); }
	double timestamp() const { return mSACCHL1->timestamp(); }
	int actualMSPower() const { return mSACCHL1->actualMSPower(); }
	int actualMSTiming() const { return mSACCHL1->actualMSTiming(); }
	void setPhy(float RSSI, float timingError, double wTimestamp)
		{ mSACCHL1->setPhy(RSSI,timingError,wTimestamp); }
	void setPhy(const SACCHLogicalChannel& other) { mSACCHL1->setPhy(*other.mSACCHL1); }
	void RSSIBumpDown(int dB) { assert(mL1); mSACCHL1->RSSIBumpDown(dB); }

	//@}

	/**@name Channel and neighbour cells stats as reported from MS */
	//@{
	const L3MeasurementResults& measurementResults() const { return mMeasurementResults; }
	//@}

	/** Get active state from the host DCCH. */
	bool active() const { assert(mHost); return mHost->active(); }

	/** Get recyclable state from the host DCCH. */
	bool recyclable() const { assert(mHost); return mHost->recyclable(); }

	protected:

	/** Read and process a measurement report, called from the service loop. */
	void getReport();

	/** This is a loop in its own thread that sends SI5 and SI6. */
	void serviceLoop();

};

/** A C interface for the SACCHLogicalChannel embedded loop. */
void *SACCHLogicalChannelServiceLoopAdapter(SACCHLogicalChannel*);


/**
	Common control channel.
	The "uplink" component of the CCCH is the RACH.
	See GSM 04.03 4.1.2: "A common control channel is a point-to-multipoint
	bi-directional control channel. Common control channels are physically
	sub-divided into the common control channel (CCCH), the packet common control
	channel (PCCCH), and the Compact packet common control channel (CPCCCH)."
	(pat) To implement DRX and paging I added the CCCHCombinedChannel to which CCCH messages
	should now be sent, and this class is now just a private attachment point whose primary
	purpose is to house the serviceloop for a single CCCH.
*/
class CCCHLogicalChannel : public NDCCHLogicalChannel {

	protected:
	friend class GSMConfig;

	/*
		Because the CCCH is written by multiple threads,
		we funnel all of the outgoing messages into a FIFO
		and empty that FIFO with a service loop.
	*/

	Thread mServiceThread;	///< a thread for the service loop
	L3FrameFIFO mQ;			///< because the CCCH is written by multiple threads
#if ENABLE_PAGING_CHANNELS
	L3FrameFIFO mPagingQ[sMax_BS_PA_MFRMS];	///< A queue for each paging channel on this timeslot.
#endif
	bool mRunning;			///< a flag to indication that the service loop is running
	bool mWaitingToSend;	// If this is set, there is another CCCH message
							// waiting in the encoder serviceloop.
							// This variable is not mutex locked and could
							// be incorrect, but it is not critical.

	public:

	CCCHLogicalChannel(const TDMAMapping& wMapping);

	void open();

	void send(const L3RRMessage& msg)
		{
			// DEBUG:
			//LOG(WARNING) << "CCCHLogicalChannel2::write q";
			mQ.write(new L3Frame((const L3Message&)msg,UNIT_DATA));
		}

	void send(const L3Message&) { assert(0); }

	/** This is a loop in its own thread that empties mQ. */
	void serviceLoop();

	/** Return the number of messages waiting for transmission. */
	unsigned load() const { return mQ.size(); }

	// (pat) GPRS needs to know exactly when the CCCH message will be sent downstream,
	// because it needs to allocate an upstream radio block after that time,
	// and preferably as quickly as possible after that time.
	// For now, I'm going to punt on this and return the worst case.
	// TODO: This is the wrong way to do this.
	// First, this calculation should not be here; it will be hard for anyone maintaining
	// the code and making changes that would affect this calculation to find it here.
	// Second, it depends on what kind of C0T0 beacon we have.
	// We should wait until it is time to send the message, then create it.
	// To do this, either the CCCHLogicalChannel::serviceLoop should be rewritten,
	// or we should hook XCCHL1Encoder::sendFrame(L2Frame) to modify the message
	// if it is a packet message.  Or more drastically, make the CCCHLogicalChannel::mQ
	// queue hold internal messages not L3Frames, for example, for RACH a struct
	// with the arrival time, RACH message, signal strength and timing advance,
	// and delay generating the RRMessage until it is ready to send.
	// 
	// But for now, just punt and send a frame time far enough in the future that it
	// is guaranteed to work:
	// Note: Time wraps at gHyperFrame.
	Time getNextMsgSendTime();

	ChannelType type() const { return CCCHType; }

	friend void *CCCHLogicalChannelServiceLoopAdapter(CCCHLogicalChannel*);

};

/** A C interface for the CCCHLogicalChannel embedded loop. */
void *CCCHLogicalChannelServiceLoopAdapter(CCCHLogicalChannel*);



class TCHFACCHLogicalChannel : public LogicalChannel {

	protected:

	TCHFACCHL1FEC * mTCHL1;

	/**@name Sockets for RTP traffic, must be non-blocking. */
	//@{
	UDPSocket * mRTPSocket;		///< RTP traffic
	UDPSocket * mRTCPSocket;	///< RTP control
	//@}

	public:

	TCHFACCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const CompleteMapping& wMapping);

	UDPSocket * RTPSocket() { return mRTPSocket; }
	UDPSocket * RTCPSocket() { return mRTCPSocket; }

	ChannelType type() const { return FACCHType; }

	void sendTCH(const unsigned char* frame)
		{ assert(mTCHL1); mTCHL1->sendTCH(frame); }

	unsigned char* recvTCH()
		{ assert(mTCHL1); return mTCHL1->recvTCH(); }

	unsigned queueSize() const
		{ assert(mTCHL1); return mTCHL1->queueSize(); }

	bool radioFailure() const
		{ assert(mTCHL1); return mTCHL1->radioFailure(); }
};



/**
	Cell broadcast control channel (CBCH).
	See GSM 04.12 3.3.1.
*/
class CBCHLogicalChannel : public NDCCHLogicalChannel {

	protected:

	/*
		The CBCH should be written be a single thread.
		The input interface is *not* multi-thread safe.
	*/

	public:

	CBCHLogicalChannel(const CompleteMapping& wMapping);

	void send(const L3SMSCBMessage& msg);

	void send(const L3Message&) { assert(0); }

	ChannelType type() const { return CBCHType; }


};




/**@name Test channels, not actually used in GSM. */
//@{

/**
	A logical channel that loops L3Frames from input to output.
	Use a pair of these for control layer testing.
*/
class L3LoopbackLogicalChannel : public LogicalChannel {

	private:

	L3FrameFIFO mL3Q[4];		///< a queue used for the loopback

	public:

	L3LoopbackLogicalChannel();

	/** Fake the SDCCH channel type because that makes sense for most tests. */
	ChannelType type() const { return SDCCHType; }

	/** L3 Loopback */
	void send(const L3Frame& frame, unsigned SAPI=0)
		{ mL3Q[SAPI].write(new L3Frame(frame)); }

	/** L3 Loopback */
	void send(const GSM::Primitive prim, unsigned SAPI=0)
		{ mL3Q[SAPI].write(new L3Frame(prim)); }

	/** L3 Loopback */
	L3Frame* recv(unsigned timeout_ms = 15000, unsigned SAPI=0)
		{ return mL3Q[SAPI].read(timeout_ms); }

};



class SDCCHLogicalChannel_LB : public SDCCHLogicalChannel 
{
	public : 
      SDCCHLogicalChannel_LB(
		unsigned wCN,
		unsigned wTN,
		const CompleteMapping& wMapping);
};


class TCHFACCHLogicalChannel_UPLINK : public TCHFACCHLogicalChannel 
{
public:
	/** Custom constructor, L2 is Uplink instead of downlink. */
	TCHFACCHLogicalChannel_UPLINK(
		unsigned wCN,
		unsigned wTN, 
		const CompleteMapping& wMapping);

};   

//@}

};		// GSM

#endif


// vim: ts=4 sw=4
