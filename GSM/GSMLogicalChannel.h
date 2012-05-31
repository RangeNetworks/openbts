/**@file Logical Channel.  */

/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
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


/**
	A complete logical channel.
	Includes processors for L1, L2, L3, as needed.
	The layered structure of GSM is defined in GSM 04.01 7, as well as many other places.
	The concept of the logical channel and the channel types are defined in GSM 04.03.
	This is virtual class; specific channel types are subclasses.
*/
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

	/** Set L1 physical parameters from a RACH or pre-exsting channel. */
	virtual void setPhy(float wRSSI, float wTimingError);

	/* Set L1 physical parameters from an existing logical channel. */
	virtual void setPhy(const LogicalChannel&);

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
	virtual void writeLowSide(const RxBurst& burst) { assert(mL1); mL1->writeLowSide(burst); }

	/** Return true if the channel is safely abandoned (closed or orphaned). */
	bool recyclable() const { assert(mL1); return mL1->recyclable(); }

	/** Return true if the channel is active. */
	bool active() const { assert(mL1); return mL1->active(); }

	/** The TDMA parameters for the transmit side. */
	const TDMAMapping& txMapping() const { assert(mL1); return mL1->txMapping(); }

	/** The TDMAParameters for the receive side. */
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
	/** Actual MS uplink power. */
	virtual int actualMSPower() const;
	/** Actual MS uplink timing advance. */
	virtual int actualMSTiming() const;
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

	public:

	SACCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const MappingPair& wMapping);

	ChannelType type() const { return SACCHType; }

	void open();

	friend void *SACCHLogicalChannelServiceLoopAdapter(SACCHLogicalChannel*);

	/**@name Pass-through accoessors to L1. */
	//@{
	float RSSI() const { return mSACCHL1->RSSI(); }
	float timingError() const { return mSACCHL1->timingError(); }
	int actualMSPower() const { return mSACCHL1->actualMSPower(); }
	int actualMSTiming() const { return mSACCHL1->actualMSTiming(); }
	void setPhy(float RSSI, float timingError) { mSACCHL1->setPhy(RSSI,timingError); }
	void setPhy(const SACCHLogicalChannel& other) { mSACCHL1->setPhy(*other.mSACCHL1); }
	//@}

	/**@name Channel and neighbour cells stats as reported from MS */
	//@{
	const L3MeasurementResults& measurementResults() const { return mMeasurementResults; }
	//@}

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
*/
class CCCHLogicalChannel : public NDCCHLogicalChannel {

	protected:

	/*
		Because the CCCH is written by multiple threads,
		we funnel all of the outgoing messages into a FIFO
		and empty that FIFO with a service loop.
	*/

	Thread mServiceThread;	///< a thread for the service loop
	L3FrameFIFO mQ;			///< because the CCCH is written by multiple threads
	bool mRunning;			///< a flag to indication that the service loop is running

	public:

	CCCHLogicalChannel(const TDMAMapping& wMapping);

	void open();

	void send(const L3RRMessage& msg)
		{ mQ.write(new L3Frame((const L3Message&)msg,UNIT_DATA)); }

	void send(const L3Message&) { assert(0); }

	/** This is a loop in its own thread that empties mQ. */
	void serviceLoop();

	/** Return the number of messages waiting for transmission. */
	unsigned load() const { return mQ.size(); }

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
