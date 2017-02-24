/**@file Logical Channel.  */

/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2014 Range Networks, Inc.
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




#ifndef LOGICALCHANNEL_H
#define LOGICALCHANNEL_H

#include <sys/types.h>
#include <pthread.h>
#include <semaphore.h>

#include <iostream>
#include <map>

#include "GSML1FEC.h"
//#include "GSMSAPMux.h"
#include "GSML2LAPDm.h"
#include "GSML3RRElements.h"
#include "GSMTDMA.h"
#include "GSMChannelHistory.h"
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
class L2DL;


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
class L2LogicalChannelBase
{
	virtual void _define_vtable();
	
protected:	
	L1FEC *mL1;			///< L1 forward error correction

	/**
		Blank initializer just nulls the pointers.
		Specific sub-class initializers allocate new components as needed.
	*/
	L2LogicalChannelBase() :mL1(NULL) { }
	void startl1();

public:

	/** The destructor doesn't do anything since logical channels should not be destroyed. */
	virtual ~L2LogicalChannelBase() {};

	/**@name Accessors. */
	//@{
	L3ChannelDescription channelDescription() const;
	//@}


	// Pat 5-27-2012: Let the LogicalChannel know the next scheduled write time.
	GSM::Time getNextWriteTime() { return mL1->encoder()->getNextWriteTime(); }

	/**@name L3 interfaces */
	//@{
	// Note: l2recv is defined in L2SAPMux.
	virtual void l2sendf(const L3Frame& frame) = 0;

	/**
		Send "naked" primitive down the channel.
		@param prim The primitive to send.
		@pram SAPI The service access point on which to send.
	*/
	void l2sendp(const GSM::Primitive& prim, SAPI_t SAPI=SAPI0)
		{ l2sendf(L3Frame(SAPI,prim)); }

	/**
		Serialize and send an L3Message with a given primitive.
		@param msg The L3 message.
		@param prim The primitive to use.
	*/
	void l2sendm(const L3Message& msg, GSM::Primitive prim=L3_DATA, SAPI_t SAPI=SAPI0);
	//@} // L3

	/**@name L1 interfaces */
	//@{

	public:

	// Over-ridden in L2LogicalChannel and SACCHLogicalChannel to handle uplink messages, setting the timers on the main channel.
	// The other channel types do not have an uplink so they over-ride this with an devassert(0).
	// Yes it might be better to have separate classes for these variations.
	virtual void writeLowSide(const L2Frame& frame) = 0;

	// The downstream connection.
	//virtual void writeHighSideL1(const L2Frame& frame) = 0;
	virtual void writeToL1(const L2Frame& frame) = 0;
	
	/** Connect an ARFCN manager to link L1FEC to the radio. */
	virtual void downstream(ARFCNManager* radio);	// Used by CCCHLogicalChannel

	/** The TDMA parameters for the transmit side. */
	// (pat) This lovely function is unused.  Use L1Encoder::mapping()
	const TDMAMapping& txMapping() const { devassert(mL1); return mL1->txMapping(); }

	/** The TDMAParameters for the receive side. */
	// (pat) This lovely function is unused.  Use L1Decoder::mapping()
	const TDMAMapping& rcvMapping() const { devassert(mL1); return mL1->rcvMapping(); }

	/** GSM 04.08 10.5.2.5 type and offset code. */
	TypeAndOffset typeAndOffset() const { devassert(mL1); return mL1->typeAndOffset(); }

	/** ARFCN */ /* TODO: Use this, or when obtaining the physical info use ARFCN from a diff location? */
	unsigned ARFCN() const { devassert(mL1); return mL1->ARFCN(); }
	bool l1active() const { devassert(mL1); return mL1->l1active(); }

	/**@name Channel stats from the physical layer */
	//@{
	/** Carrier index. */
	unsigned CN() const { devassert(mL1); return mL1->CN(); }
	/** Slot number. */
	unsigned TN() const { devassert(mL1); return mL1->TN(); }
	/** Receive FER. */
	float FER() const { devassert(mL1); return mL1->FER(); }
	DecoderStats getDecoderStats() const { return mL1->decoder()->getDecoderStats(); }
	/** Control whether to accept a handover. */
	HandoverRecord& handoverPending(bool flag, unsigned handoverRef) { devassert(mL1); return mL1->handoverPending(flag, handoverRef); }
	//@}

	//@} // L1


	/** Return the channel type. */
	virtual ChannelType chtype() const =0;

	/**
		Make the channel ready for a new transaction.
		The channel is closed with primitives from L3.
		(pat) LogicalChannel::lcopen() calls: L1FEC::open(), L1Encoder::open(), L1Encoder::open(), none of which do much but reset the L1 layer classes.
		If there is an associated SACCH, that is opened too.
		On channels with LAPDm, which are: TCHFACCH, SDCCH and SACCH:
		LogicalChannel::lcopen() also calls L2LAPDm::l2open() on each SAP endpoint, which has a side effect of starting to send idle frames in downlink.
		After open, an ESTABLISH primitive may be sent on the channel to indicate when SABM mode is established.
		In downlink: only for MT-SMS, an ESTABLISH primitive is sent to establish LAPDm SABM mode, which is used only on SAP 3, which is used
			only for SMS messages in OpenBTS.
		In uplink: the MS always establishes SABM mode.  After the lcopen(), when the first good frame arrives,
		an ESTABLISH primitive is sent upstream toward L3, which will notify the DCCHDispatcher to start looking for messages.

		(pat) 4-28-2014: Did a major overhaul of channel initialization.  Formerly lcopen was called directly from getTCH (except for GPRS channels),
		then some callers initialized SACCH, which may already be transmitting; old code also had a nasty side effect that getTCH blocked.
		The phy parameters in the SACCH must be inited before it starts transmitting or some handsets reject the channel.
		This code is also used during channel initialization when no phy parameters are known, eg in GSMConfig.
		So the former one step lcopen is replaced with three steps:
			1. lcinit does base initialization of the channel; 
			2. optionally call setPhy or initPhy on the underlying SACCH channels
			3. lcstart starts the channel actually transmitting.  Note that lcstart may block.
		Note getTCH is modified to call only lcinit, so callers that need the channel running must call lcstart.
		The lcopen() method still exists for those callers who really dont care about phy parameters, which should only be GSMConfig for
		the very initial channel creation when OpenBTS fires up, which should only be on ARFCN C0, which must be constantly transmitting
		on all timeslots.

		(pat) 5-2014: Communication on SACCH is constant in both directions.
		The loss of layer1 (RR) connection is detected by lack of communication on the SACCH, see GSM 5.08 5.3.
		GSM 4.08 3.4.13.2: RR connection loss is communicated directly from layer1 to layer3, which is why there is no "PHY-ERROR"
		primitive in LAPDm specs.  Layer3 is supposed to release the channel, which means stop transmitting on SACCH and then
		wait for T3109 to elapse before marking the channel available for reuse.  OpenBTS totally botched this previously.
		Note that it is common for the handset to be able to hear BTS even though the BTS cannot hear the handset.
		So note that when we detect loss of SACCH measurement reports we must first wait by SACCH loss for a network-determined
		period of time, but probably 30 secs like T3109, then layer3 is supposed to send a channel release, deactivate SACCH
		and wait another T3109 period before reusing the channel.
	*/

	/**@ Debuging functions: will give access to all intermediate layers. */
	//@{
	// unused: L2DL * debugGetL2(unsigned sapi){ return mL2[sapi]; }
	L1FEC * debugGetL1(){ return mL1; }
	L1FEC * lcGetL1() { return mL1; }
	//@}

	const char* descriptiveString() const { devassert(mL1); return mL1->descriptiveString(); }

	protected:

	/**
		Make the normal inter-layer connections.
		Should be called from inside the constructor after
		the channel components are created.
	*/
	void connect(L1FEC *wL1);

	public:
	bool inUseByGPRS() const { return mL1->inUseByGPRS(); }
	bool decryptUplink_maybe(string wIMSI, int wA5Alg) { return mL1->decoder()->decrypt_maybe(wIMSI, wA5Alg); }
};

// (pat) This is used for channels that use LAPDm, and for historical reasons, CCCH which doesnt.
class L2SAPMux : public L2LogicalChannelBase
{
	protected:
	// Input interface from Layer3.  The queue is common to both L2LogicalChannel and SACCHLogicalChannel, so it was
	// convenient to put it here, but all the methods accessing it are in the descendent classes.
	InterthreadPriorityQueue<L3Frame> mL3In;			///< we connect L3->L2 through a FIFO

	/**
		Send an L3Frame on downlink.
		Only call this from the service loop so that messages to LAPDm are serialized.
		This method will block until the message is transferred to the transceiver.
		@param frame The L3Frame to be sent.
		@param SAPI The service access point indicator.
	*/
	void sapWriteFromL3(const L3Frame& frame);
	void flushL3In();

	mutable Mutex mSapLock;
	L2DL * mL2[4];		///< one L2 for each SAP, GSM 04.05 5.3
						// (pat) Only SAP 0 and 3 are used: 0 for RR/MM/CC messages and 3 for SMS.
	public:
	L2SAPMux(){ 
		mL2[0] = NULL;
		mL2[1] = NULL;
		mL2[2] = NULL;
		mL2[3] = NULL;
	}
	void sapInit(L2DL *sap0, L2DL *sap3);

	virtual ~L2SAPMux() {}

	void sapWriteFromL1(const L2Frame& frame); 

	virtual void writeToL3(L3Frame*frame) = 0;

	void sapStart();
	// (pat 8-2013) Return the LAPDm state of the main SAPI0 for reporting in the CLI;
	// on channels without LAPDm it would return an empty string, except it will never be called for such cases.
	LAPDState getLapdmState(SAPI_t SAPI=SAPI0) const;
	bool multiframeMode(SAPI_t SAPI) const;
	void l2stop();
};

// This is a main LogicalChannel, meaning SDCCH or TCH/FACCH.
// It is always associated with a SACCHLogicalChannel.
class L2LogicalChannel: public L2SAPMux, public Control::L3LogicalChannel
{
	virtual void _define_vtable();

	// The messages from L2->L3 for both L2LogicalChannel and SACCHLogicalChannel go in this same queue.
	InterthreadPriorityQueue<L3Frame> mL3Out;			///< we connect L2->L3 through a FIFO

	Z100TimerThreadSafe mT3101;
	Z100TimerThreadSafe mT3109;
	Z100TimerThreadSafe mT3111;
	// When the channel becomes recyclable there may be a downlink transmission queued up.
	// SACCH transmissions are at 480ms intervals.
	// I want to make sure those clear, so we will wait an additional time before recycling the channel.
	// It also allows time for an ack as per spec.
	//Z100TimerThreadSafe mTRecycle;	// Additional delay before recycling channel.

	static void *MessageServiceLoop(L2LogicalChannel*);
	static void *ControlServiceLoop(L2LogicalChannel*);
	Thread mlcMessageServiceThread;	///< a thread for the service L3 queue loop
	Thread mlcControlServiceThread;	///< a thread for the service loop
	Bool_z mlcMessageLoopRunning;			///< true if the service loops are started
	Bool_z mlcControlLoopRunning;			///< true if the service loops are started

	protected:
	SACCHLogicalChannel *mSACCH;	///< The associated SACCH, if any.
									// (pat) The reverse pointer is SACCHLogicalChannel::mHost.
	void startNormalRelease();

	public:
	L2LogicalChannel() : mSACCH(NULL) {}
	SACCHLogicalChannel* getSACCH() { return mSACCH; }
	const SACCHLogicalChannel* getSACCH() const { return mSACCH; }
	void lcinit();
	void lcstart();
	void lcopen();

	L3Frame * l2recv(unsigned timeout_ms = 15000);
	void l2sendf(const L3Frame& frame);
	void l2sendp(const GSM::Primitive& prim, SAPI_t SAPI=SAPI0) { L2LogicalChannelBase::l2sendp(prim,SAPI); }
	void l2sendm(const L3Message& msg, GSM::Primitive prim=L3_DATA, SAPI_t SAPI=SAPI0) { L2LogicalChannelBase::l2sendm(msg,prim,SAPI); }
	bool multiframeMode(SAPI_t SAPI) const;

	void writeLowSide(const L2Frame& frame);

	// Returns true if link is lost due to RR failure or normal channel closure.
	bool radioFailure() const;

	/** Connect an ARFCN manager to link L1FEC to the radio. */
	void downstream(ARFCNManager* radio);

	/** Return true if the channel is safely abandoned (closed or orphaned). */
	bool recyclable();


	// Frame comes down from L2LAPDm writeL1.
	void writeToL1(const L2Frame& frame);

	// Frame comes from LapDm toward L3.
	void writeToL3(L3Frame*frame);

	const char* descriptiveString() const { devassert(mL1); return mL1->descriptiveString(); }
	std::string displayTimers() const;

	void serviceHost();
	void immediateRelease();

	/** Set L1 physical parameters from a RACH or pre-exsting channel. */
	// This just passes it off to the SACCH channel, which then just passes it off to SACCH L1.
	virtual void l1InitPhy(float wRSSI, float wTimingError, double wTimestamp);
	
	/* Set L1 physical parameters from an existing logical channel. */
	virtual void setPhy(const L2LogicalChannel&);
	virtual ChannelType chtype() const =0;	// Yet another redundant decl for wonderful C++.  


	/**@name L2 passthroughs */
	//@{
	virtual MSPhysReportInfo *getPhysInfo() const;
	virtual const L3MeasurementResults& measurementResults() const;
	ChannelHistory *getChannelHistory();
	//@}
	// FIXME: reset T3101 when the handover starts, and get rid of debug3101remaining
	void addT3101(int msecs) { mT3101.addTime(msecs); }
	long debug3101remaining() { return mT3101.remaining(); }

};


// (pat) The operator<< for L2LogicalChannel should not be necessary but g++ prints warnings in other directories if missing.  Compiler bug?
std::ostream& operator<<(std::ostream&os, const L2LogicalChannel&ch);
std::ostream& operator<<(std::ostream&os, const L2LogicalChannel*ch);
std::ostream& operator<<(std::ostream&os, const L2LogicalChannelBase&ch);
std::ostream& operator<<(std::ostream&os, const L2LogicalChannelBase*ch);


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
	This is a virtual base class this is extended for CCCH only.  (formerly used for BCCH)
	See GSM 04.06 4.1.1, 4.1.3.
	There is no SAPMux or LAPDm.
*/
class NDCCHLogicalChannel : public L2LogicalChannelBase {
	void writeToL1(const L2Frame& frame) { mL1->writeHighSide(frame); }

	public:
	// For CCCH channels:
	// This skips LAPDm entirely.
	// calls (L1FEC)mL1->writeHighSide(L2Frame), which
	// (because CCCHL1FEC is nearly empty) just
	// calls (L1Encoder)mEncoder->writeHighSide(L2Frame), which maps
	// to CCCHL1Encoder which maps to XCCHL1Encoder::writeHighSide(L2Frame),
	// which processes the L2Frame primitive, and sends traffic data to
	// XCCHL1Encoder::sendFrame(L2Frame), which encodes the frame and then calls
	// XCCHL1Encoder::transmit(implicit mI arg with encoded burst) that
	// finally blocks until L1Encoder::mPrevWriteTime occurs, then sets the
	// burst time to L1Encoder::mNextWriteTime and
	// calls (ARFCNManager*)mDownStream->writeHighSideTx() which writes to the socket.
	void l2sendf(const L3Frame& l3frame) {
		//OBJLOG(DEBUG) <<"NDCCH::writeHighSide " << l3frame;
		devassert(l3frame.primitive()==L3_UNIT_DATA);
		L2Header header(L2Length(l3frame.L2Length()));
		writeToL1(L2Frame(header,l3frame,true));
	}

	/** This channel only sends RR protocol messages. */
	void l2sendm(const L3RRMessage& msg)
		{ L2LogicalChannelBase::l2sendm((const L3Message&)msg,L3_UNIT_DATA); }

	/** This channel only sends RR protocol messages. */
	void l2sendm(const L3Message&) { devassert(0); }

	// No upstream messages ever.
	L3Frame * l2recv(unsigned /*timeout_ms = 15000*/, unsigned /*SAPI=0*/) { devassert(0); return NULL; }
	void writeLowSide(const L2Frame& /*frame*/) { devassert(0); }
	void writeToL3(L3Frame*) { devassert(0); }
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
class SACCHLogicalChannel : public L2SAPMux, public ChannelHistory
{
	friend class L2LogicalChannel;
	virtual void _define_vtable();
	//SAPMux mSapMux;		///< service access point multiplex

	InterthreadQueue<L3Message> mTxQueue;	// FIXME: not currently used. Queue of outbound messages from Layer3 for this SACCH.  SAPI is determined from message PD.

#if USE_SEMAPHORE
	sem_t mOpenSignal;	// (pat 7-25-2013)
#endif

	SACCHL1FEC *mSACCHL1;
	Thread mSacchServiceThread;	///< a thread for the service loop
	Bool_z mSacchRunning;			///< true if the service loop is started

	protected:

	/** MeasurementResults from the MS. They are caught in serviceLoop, accessed
	 for recording along with GPS and other data in MobilityManagement.cpp */
	// (pat) This is the most recent measurement; it is replaced every 480ms.
	L3MeasurementResults mMeasurementResults;

	// Return true if the frame was processed and discarded.
	bool processMeasurementReport(L3Frame *frame);

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
	//static const int cAveragePeriodRXLEV_SUB_SERVING_CELL = 8; // How many we measurement reports we average over.
	//float mAverageRXLEV_SUB_SERVICING_CELL;		// Must be signed!

	// Add a measurement result data point to the averaged RXLEV_SUB_SERVING_CELL value.
	//void addSelfRxLev(int wDataPoint) {
	//	int minus1 = cAveragePeriodRXLEV_SUB_SERVING_CELL - 1;
	//	mAverageRXLEV_SUB_SERVICING_CELL = ((float) wDataPoint + minus1 * mAverageRXLEV_SUB_SERVICING_CELL)
	//		/ (float) cAveragePeriodRXLEV_SUB_SERVING_CELL;
	//}

	L2LogicalChannel *mHost;

	public:

	SACCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const MappingPair& wMapping,
		/*const*/ L2LogicalChannel* wHost);

	ChannelType chtype() const { return SACCHType; }

	void sacchInit();

	// L2LAPDm low-side connections: via SAPMux interface:
	void writeLowSide(const L2Frame& frame) { sapWriteFromL1(frame); }
	// Frame comes down from L2LAPDm writeL1.
	void writeToL1(const L2Frame& frame);

	// L2LAPDm high-side connections
	// Frame comes from LapDm toward L3.
	void writeToL3(L3Frame*frame);


	// Layer3 interface:
	void l2sendf(const L3Frame& frame);

	/** A C interface for the SACCHLogicalChannel embedded loop. */
	static void *SACCHServiceLoop(SACCHLogicalChannel*);

	/**@name Pass-through accoessors to L1. */
	//@{
	// Obtains SACCH reporting info.
	MSPhysReportInfo *getPhysInfo() const { return mSACCHL1->getPhysInfo(); }
	ChannelHistory *getChannelHistory() { return static_cast<ChannelHistory*>(this); }
	DecoderStats getDecoderStats() const { return mSACCHL1->decoder()->getDecoderStats(); }
	void l1InitPhy(float RSSI, float timingError, double wTimestamp)
		{ mSACCHL1->l1InitPhy(RSSI,timingError,wTimestamp); }
	void setPhy(const SACCHLogicalChannel& other) { mSACCHL1->setPhy(*other.mSACCHL1); }
	void RSSIBumpDown(int dB) { devassert(mL1); mSACCHL1->RSSIBumpDown(dB); }

	//@}

	/**@name Channel and neighbour cells stats as reported from MS */
	//@{
	const L3MeasurementResults& measurementResults() const { return mMeasurementResults; }
	//@}

	/** Get recyclable state from the host DCCH. */
	//bool recyclable() { devassert(mHost); return mHost->recyclable(); }
	bool sacchRadioFailure() const;
	L2LogicalChannel *hostChan() const { return mHost; }

	private:
	/** This is a loop in its own thread that sends SI5 and SI6. */
	void serviceSACCH(unsigned &count);

};


class TCHFACCHLogicalChannel : public L2LogicalChannel {

	protected:

	TCHFACCHL1FEC * mTCHL1;

	/**@name Sockets for RTP traffic, must be non-blocking. */
	//@{
	//UDPSocket * mRTPSocket;		///< RTP traffic
	//UDPSocket * mRTCPSocket;	///< RTP control
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

	void sendTCH(SIP::AudioFrame* frame)
		{ devassert(mTCHL1); mTCHL1->sendTCH(frame); }

	SIP::AudioFrame* recvTCH()
		{ devassert(mTCHL1); return mTCHL1->recvTCH(); }

	unsigned queueSize() const
		{ devassert(mTCHL1); return mTCHL1->queueSize(); }

	// (pat) 3-28: Moved this higher in the hierarchy so we can use it on SDCCH as well.
	//bool radioFailure() const
	//	{ devassert(mTCHL1); return mTCHL1->radioFailure(); }
};


/**
	Cell broadcast control channel (CBCH).
	See GSM 04.12 3.3.1.
*/
// (pat) This uses one SDCCH slot, so it has a SACCH that doesnt do anything.
// It must be based on L2LogicalChannel because it has a SACCH, but all the relevant methods
// in L2LogicalChannel are overwritten so messages pass straight through to 
class CBCHLogicalChannel : public L2LogicalChannel {
	// The CBCH should be written be a single thread.
	// The input interface is *not* multi-thread safe.
	public:

	CBCHLogicalChannel(int wCN, int wTN, const CompleteMapping& wMapping);

	void l2sendm(const L3SMSCBMessage& msg);
	void l2sendm(const L3Message&) { devassert(0); }	// No other messages supported.

	void l2sendf(const L3Frame& frame);

	void cbchOpen();

	ChannelType chtype() const { return CBCHType; }

	// unneeded: void writeToL1(const L2Frame& frame) { mL1->writeHighSide(frame); }

	void writeLowSide(const L2Frame&) { assert(0); }	// There is no uplink.
	L3Frame * l2recv(unsigned, unsigned) { devassert(0); return NULL; }
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

	// (pat 7-25-2013) The 'new L3Frame' below was doing an auto-conversion through L3Message.
	void l2sendf(const L3Frame& frame)
		{
			mL3Q[frame.getSAPI()].write(new L3Frame(frame));
		}

	/** L3 Loopback */
	L3Frame* l2recv(unsigned timeout_ms = 15000, unsigned SAPI=0)
		{ return mL3Q[SAPI].read(timeout_ms); }

};



#if WHATISTHIS
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
#endif

//@}

};		// GSM

#endif


// vim: ts=4 sw=4
