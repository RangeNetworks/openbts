/**@file Logical Channel.  */

/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2014 Range Networks, Inc.
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
#include <semaphore.h>

#include <iostream>
#include <map>

#include "GSML1FEC.h"
#include "GSMSAPMux.h"
#include "GSML2LAPDm.h"
#include "GSML3RRElements.h"
#include "GSMTDMA.h"
#include <L3LogicalChannel.h>

#include <Logger.h>

class ARFCNManager;
class UDPSocket;


namespace GSM {

class SACCHLogicalChannel;
class L3Message;
class L3RRMessage;
class L3SMSCBMessage;
class L2LogicalChannel;


/**
	A complete logical channel.
	Includes processors for L1, L2, L3, as needed.
	The layered structure of GSM is defined in GSM 04.01 7, as well as many other places.
	The concept of the logical channel and the channel types are defined in GSM 04.03.
	This is virtual class; specific channel types are subclasses.
	(pat) This class is used for both DCCH and SACCH.  The DCCH can be TCH+FACCH or SDCCH.
	SACCH is a slave LogicalChannel always associated with a DCCH LogicalChannel.
	(pat) About Channel Establishment:  See more comments at L3LogicalChannel.
*/

// (pat) This class is a GSM-specific dedicated logical channel, meaning that at any given time the channel is connected to just one MS.
// GPRS and UMTS do not use dedicated logical channels, rather they use shared resources in layer 2, and in fact they dont
// even really have a logical channel type entity in layer 2.
// Therefore I split this class into L2 and L3 portions, where the L3LogicalChannel is common with UMTS and managed in the Control directory.
// This L2LogicalChannel is rarely referenced outside this directory; primarily just to send L3 level handover information to a Peer.
class L2LogicalChannel : public Control::L3LogicalChannel {
	
protected:	

	/**@name Contained layer processors. */
	//@{
	L1FEC *mL1;			///< L1 forward error correction
	SAPMux mMux;		///< service access point multiplex
	// (pat) mL2 is redundant with SAPMux mUpstream[].
	L2DL *mL2[4];		///< data link layer state machines, one per SAP
	//@}

	SACCHLogicalChannel *mSACCH;	///< The associated SACCH, if any.
									// (pat) The reverse pointer is SACCHLogicalChannel::mHost.
public:

	/**
		Blank initializer just nulls the pointers.
		Specific sub-class initializers allocate new components as needed.
	*/
	L2LogicalChannel()
		:mL1(NULL),mSACCH(NULL)
	{
		for (int i=0; i<4; i++) mL2[i]=NULL;
	}


	
	/** The destructor doesn't do anything since logical channels should not be destroyed. */
	virtual ~L2LogicalChannel() {};
	

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
	virtual void setPhy(const L2LogicalChannel&);

	virtual const L3MeasurementResults& measurementResults() const;

	/**@name L3 interfaces */
	//@{

	// (pat) This function is only applicable on channels that use LAPDm.
	// There is a fifo on LAPDm uplkink so this is only blocking if the fifo is empty.
	// For the GSM version of l3rewrite we are going to add a SIP notification to this queue,
	// so it should be moved from LAPDm to this class.
	/**
		Read an L3Frame from SAP0 uplink, blocking, with timeout.
		The caller is responsible for deleting the returned pointer.
		The default 15 second timeout works for most L3 operations.
		@param timeout_ms A read timeout in milliseconds.
		@param SAPI The service access point indicator from which to read.
		@return A pointer to an L3Frame, to be deleted by the caller, or NULL on timeout.
	*/
	virtual L3Frame * l2recv(unsigned timeout_ms = 15000, unsigned SAPI=0)
	{
		assert(mL2[SAPI]);
		L3Frame *result = mL2[SAPI]->l2ReadHighSide(timeout_ms);
		if (result) { LOG(DEBUG) <<descriptiveString()<<LOGVAR(SAPI) <<LOGVAR(timeout_ms) <<LOGVAR(result); }
		return result;
	}

	/**
		Send an L3Frame on downlink.
		This method will block until the message is transferred to the transceiver.
		@param frame The L3Frame to be sent.
		@param SAPI The service access point indicator.
	*/
	virtual void l2sendf(const L3Frame& frame, SAPI_t SAPI=SAPI0)
	{
		// (pat) Note that writeHighSide is overloaded per class hierarchy, and is also used
		// for entirely unrelated classes, which are distinguishable (by humans,
		// not by the compiler, which considers them unrelated functions)
		// by arguments of L3Frame or L2Frame.
		// Update: I have renamed some of of the L3->L2 methods to l2WriteHighSide.
		//
		// For DCCH channels (FACCH, SACCH, SDCCH):
		// This function calls virtual L2DL::l2WriteHighSide(L3Frame) which I think maps
		// to L2LAPDm::l2WriteHighSide() which interprets the primitive, and then
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
		LOG(INFO) <<channelDescription() <<LOGVAR(SAPI) <<LOGVAR(chtype()) <<" " <<frame;
		mL2[SAPI]->l2WriteHighSide(frame);
	}

	/**
		Send "naked" primitive down the channel.
		@param prim The primitive to send.
		@pram SAPI The service access point on which to send.
	*/
	// (pat) This is never over-ridden except for testing.
	virtual void l2sendp(const GSM::Primitive& prim, SAPI_t SAPI=SAPI0)
		{ assert(mL2[SAPI]); mL2[SAPI]->l2WriteHighSide(L3Frame(SAPI,prim)); }

	/**
		Serialize and send an L3Message with a given primitive.
		@param msg The L3 message.
		@param prim The primitive to use.
	*/
	// (pat) This is never over-ridden except for testing.
	virtual void l2sendm(const L3Message& msg,
			const GSM::Primitive& prim=DATA,
			SAPI_t SAPI=SAPI0);

	/**
		Block on a channel until a given primitive arrives.
		Any payload is discarded.  Block indefinitely, no timeout.
		@param primitive The primitive to wait for.
	*/
	// unused
	//void waitForPrimitive(GSM::Primitive primitive);

	/**
		Block on a channel until a given primitive arrives.
		Any payload is discarded.  Block indefinitely, no timeout.
		@param primitive The primitive to wait for.
		@param timeout_ms The timeout in milliseconds.
		@return True on success, false on timeout.
	*/
	// unused
	//bool waitForPrimitive(GSM::Primitive primitive, unsigned timeout_ms);



	//@} // L3

	/**@name L1 interfaces */
	//@{

	/** Write a received radio burst into the "low" side of the channel. */
	// (pat) What the heck?  This method makes no sense and is not used anywhere.
	// The operative virtual writeLowSide method is in class L2DL;
	//virtual void writeLowSide(const RxBurst& burst) { assert(mL1); mL1->writeLowSideRx(burst); }

	/** Return true if the channel is safely abandoned (closed or orphaned). */
	virtual bool recyclable() const { assert(mL1); return mL1->recyclable(); }

	/** Return true if the channel is active. */
	virtual bool active() const { assert(mL1); return mL1->active(); }

	// (pat 8-2013) Return the LAPDm state of the main SAPI0 for reporting in the CLI;
	// on channels without LAPDm it would return an empty string, except it will never be called for such cases.
	LAPDState getLapdmState() const;

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

	bool radioFailure() const { assert(mL1); return mL1->radioFailure(); }

	/**@name Channel stats from the physical layer */
	//@{
	/** Carrier index. */
	unsigned CN() const { assert(mL1); return mL1->CN(); }
	/** Slot number. */
	unsigned TN() const { assert(mL1); return mL1->TN(); }
	/** Receive FER. */
	float FER() const { assert(mL1); return mL1->FER(); }
	DecoderStats getDecoderStats() const { return mL1->decoder()->getDecoderStats(); }
	// Obtains SACCH reporting info.
	virtual MSPhysReportInfo *getPhysInfo() const;
	/** Control whether to accept a handover. */
	HandoverRecord& handoverPending(bool flag, unsigned handoverRef) { assert(mL1); return mL1->handoverPending(flag, handoverRef); }
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
	virtual ChannelType chtype() const =0;

	/**
		Make the channel ready for a new transaction.
		The channel is closed with primitives from L3.
		(pat) LogicalChannel::open() calls: L1FEC::open(), L1Encoder::open(), L1Encoder::open(), none of which do much but reset the L1 layer classes.
		If there is an associated SACCH, that is opened too.
		On channels with LAPDm, which are: TCHFACCH, SDCCH and SACCH:
		LogicalChannel::open() also calls L2LAPDm::l2open() on each SAP endpoint, which has a side effect of starting to send idle frames in downlink.
		After open, an ESTABLISH primitive may be sent on the channel to indicate when SABM mode is established.
		In downlink: only for MT-SMS, an ESTABLISH primitive is sent to establish LAPDm SABM mode, which is used only on SAP 3, which is used
			only for SMS messages in OpenBTS.
		In uplink: the MS always establishes SABM mode.  After the open(), when the first good frame arrives,
		an ESTABLISH primitive is sent upstream toward L3, which will notify the DCCHDispatcher to start looking for messages.
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
	bool inUseByGPRS() const { return mL1->inUseByGPRS(); }
	bool decryptUplink_maybe(string wIMSI, int wA5Alg) { return mL1->decoder()->decrypt_maybe(wIMSI, wA5Alg); }
};


std::ostream& operator<<(std::ostream&, const L2LogicalChannel&);
std::ostream& operator<<(std::ostream&os, const L2LogicalChannel*ch);


/**
	Standalone dedicated control channel.
	GSM 04.06 4.1.3: "A dedicated control channel (DCCH) is a point-to-point
	bi-directional or uni-directional control channel. ... A SDCCH (Stand-alone
	DCCH) is a bi-directional DCCH whose allocation is not linked to the
	allocation of a TCH.  The bit rate of a SDCCH is 598/765 kbit/s. 
"
*/
class SDCCHLogicalChannel : public L2LogicalChannel {

	public:
	
	SDCCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const CompleteMapping& wMapping);

	ChannelType chtype() const { return SDCCHType; }
};





/**
	Logical channel for NDCCHs that use Bbis format and a pseudolength.
	This is a virtual base class this is extended for CCCH & BCCH.
	See GSM 04.06 4.1.1, 4.1.3.
*/
class NDCCHLogicalChannel : public L2LogicalChannel {

	public:

	/** This channel only sends RR protocol messages. */
	virtual void l2sendm(const L3RRMessage& msg)
		{ L2LogicalChannel::l2sendm((const L3Message&)msg,UNIT_DATA); }

	/** This channel only sends RR protocol messages. */
	//void send(const L3Message&) { assert(0); }	// old method name.
	void l2sendm(const L3Message&) { assert(0); }

};




// (pat) We average the measurement reports from the best neighbors for handover purposes, so we dont
// cause a handover from one spuriously low measurement report.
// Note that there could be neighbors varying slightly but all much better than the current cell,
// so we save all the neighbor data, not just the best one.
// We dont have to worry about this growing without bounds because there will only be a few neighbors.
// (pat) At my house, using the Blackberry, I see a regular 9.5 second heart-beat, where the measurements drop about 8db.
// The serving cell RSSI drops first, then in the next measurement report the serving RSSI is back to normal
// and the neighbor RSSI drops.  If it were just 2db more, it would be causing a spurious handover back and
// forth every 9.5 seconds.  This cache alleviates that problem.
class NeighborCache {
	struct NeighborData {
		int16_t mnAvgRSSI;	// Must be signed.
		uint8_t mnCount;
		NeighborData() : mnCount(0) {}
	};
	typedef std::map<unsigned,NeighborData> NeighborMap;
	NeighborMap mNeighborRSSI;
	int cNumReports;	// Neighbor must appear in 2 of last cNumReports measurement reports.
	public:
	// Argument is current RSSI, and return is the averaged RSSI to use for handover determination purposes.
	int neighborAddMeasurement(unsigned freq, unsigned BSIC, int RSSI);
	void neighborStartMeasurements();	// Call this at the start of each measurement report.
	void neighborClearMeasurements();	// Call to clear everything.
	string neighborText();
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
class SACCHLogicalChannel : public L2LogicalChannel, public NeighborCache {

	protected:
	InterthreadQueue<L3Message> mTxQueue;	// FIXME: not currently used. Queue of outbound messages from Layer3 for this SACCH.  SAPI is determined from message PD.

	sem_t mOpenSignal;	// (pat 7-25-2013)

	SACCHL1FEC *mSACCHL1;
	Thread mServiceThread;	///< a thread for the service loop
	bool mRunning;			///< true is the service loop is started

	/** MeasurementResults from the MS. They are caught in serviceLoop, accessed
	 for recording along with GPS and other data in MobilityManagement.cpp */
	L3MeasurementResults mMeasurementResults;

	// (pat 7-21-2013) This self RXLEV returned from the measurement reports has short-term variations of up to 23db
	// on an iphone version 1, enough to trigger a spurious handover, so we are going to average this value.
	// The short-term variation can last up to 3 consecutive reports, so we want to average over a long enough period
	// to smooth that out.  Reports come every 1/2 second so we can make the averaging period pretty large.
	// Since this value is used only for handover, we dont have to worry about making the value correct during the first few,
	// in fact, we dont want a handover to happen too soon after the channel is opened anyway,
	// so we will just init it to 0 when the channel is opened and let it drift down.
	// (pat 1-2014) GSM 5.08 A3.1 says how we are supposed to average this; we are supposed to throw out
	// the best and worst measurements and average over a programmable period.
	// Note that this averaging puts a constraint on the maximum speed of the handset through the overlap area between cells
	// for a successful handover.  To improve handover for quickly moving handsets we should also watch delta(RXLEV)
	// and delta(TA) and if they together indicate quickly moving out of the cell, do the handover faster.
	static const int cAveragePeriodRXLEV_SUB_SERVING_CELL = 8; // How many we measurement reports we average over.
	float mAverageRXLEV_SUB_SERVICING_CELL;		// Must be signed!

	// Add a measurement result data point to the averaged RXLEV_SUB_SERVING_CELL value.
	void addSelfRxLev(int wDataPoint) {
		int minus1 = cAveragePeriodRXLEV_SUB_SERVING_CELL - 1;
		mAverageRXLEV_SUB_SERVICING_CELL = ((float) wDataPoint + minus1 * mAverageRXLEV_SUB_SERVICING_CELL)
			/ (float) cAveragePeriodRXLEV_SUB_SERVING_CELL;
	}

	/*const*/ L2LogicalChannel *mHost;
	void serviceSMS(L3Frame *smsFrame);	// Original pre-l3rewrite SMS message handler.

	public:

	SACCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const MappingPair& wMapping,
		/*const*/ L2LogicalChannel* wHost);

	ChannelType chtype() const { return SACCHType; }

	void open();

	friend void *SACCHLogicalChannelServiceLoopAdapter(SACCHLogicalChannel*);

	/**@name Pass-through accoessors to L1. */
	//@{
	// Obtains SACCH reporting info.
	MSPhysReportInfo *getPhysInfo() const { return mSACCHL1->getPhysInfo(); }
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
	L2LogicalChannel *hostChan() const { return mHost; }

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

	void l2sendm(const L3RRMessage& msg)
		{
			// DEBUG:
			//LOG(WARNING) << "CCCHLogicalChannel2::write q";
			mQ.write(new L3Frame((const L3Message&)msg,UNIT_DATA));
		}

	void l2sendm(const L3Message&) { assert(0); }

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

	ChannelType chtype() const { return CCCHType; }

	friend void *CCCHLogicalChannelServiceLoopAdapter(CCCHLogicalChannel*);

};

/** A C interface for the CCCHLogicalChannel embedded loop. */
void *CCCHLogicalChannelServiceLoopAdapter(CCCHLogicalChannel*);



class TCHFACCHLogicalChannel : public L2LogicalChannel {

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

	// unused:
	//UDPSocket * RTPSocket() { return mRTPSocket; }
	//UDPSocket * RTCPSocket() { return mRTCPSocket; }

	ChannelType chtype() const { return FACCHType; }

	void sendTCH(AudioFrame* frame)
		{ assert(mTCHL1); mTCHL1->sendTCH(frame); }

	AudioFrame* recvTCH()
		{ assert(mTCHL1); return mTCHL1->recvTCH(); }

	unsigned queueSize() const
		{ assert(mTCHL1); return mTCHL1->queueSize(); }

	// (pat) 3-28: Moved this higher in the hierarchy so we can use it on SDCCH as well.
	//bool radioFailure() const
	//	{ assert(mTCHL1); return mTCHL1->radioFailure(); }
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

	void l2sendm(const L3SMSCBMessage& msg);

	void l2sendm(const L3Message&) { assert(0); }

	ChannelType chtype() const { return CBCHType; }


};




/**@name Test channels, not actually used in GSM. */
//@{

/**
	A logical channel that loops L3Frames from input to output.
	Use a pair of these for control layer testing.
*/
class L3LoopbackLogicalChannel : public Control::L3LogicalChannel {

	private:

	L3FrameFIFO mL3Q[4];		///< a queue used for the loopback

	public:

	L3LoopbackLogicalChannel();

	/** Fake the SDCCH channel type because that makes sense for most tests. */
	ChannelType chtype() const { return SDCCHType; }

	/** L3 Loopback */
	// (pat) I dont think this class is used, but keep the old 'send' method names anyway in case
	// there is some test code somewhere that uses this class:
	//void send(const L3Frame& frame, unsigned SAPI=0)
		//{ l2sendf(frame,SAPI); }

	// (pat 7-25-2013) The 'new L3Frame' below was doing an auto-conversion through L3Message.
	void l2sendf(const L3Frame& frame, unsigned SAPI=0)
		{ mL3Q[SAPI].write(new L3Frame(frame)); }

	/** L3 Loopback */
	//void send(const GSM::Primitive prim, unsigned SAPI=0)
		//{ l2sendp(prim,SAPI); }

	void l2sendp(const GSM::Primitive prim, SAPI_t SAPI=SAPI0)
		{ mL3Q[SAPI].write(new L3Frame(SAPI,prim)); }

	/** L3 Loopback */
	L3Frame* l2recv(unsigned timeout_ms = 15000, unsigned SAPI=0)
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
