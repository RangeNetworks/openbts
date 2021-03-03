/*
* Copyright 2008 Free Software Foundation, Inc.
* Copyright 2011, 2014 Range Networks, Inc.
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


/*
Many elements follow Daniele Orlandi's <daniele@orlandi.com> vISDN/Q.921
implementation, although no code is copied directly.
*/

#ifndef L2LAPDM_H
#define L2LAPDM_H

#include "GSMCommon.h"
#include "GSMTransfer.h"
#include <ControlTransfer.h>


namespace GSM {

// Forward refs.
class L2SAPMux;
class L2LogicalChannelBase;

/**@name L2 Processing Errors */
//@{
/** L2 Read Error is thrown if there is an error in the data on the input side. */
// (pat) You could not use these unless you replace all the places where Mutex lock is called explicitly with ScopedLocks.
//unused: class L2ReadError : public GSMError { };
//unused: #define L2_READ_ERROR {throw L2ReadError();}

/** L2 Write Error is thrown if there is an error in the data on the output side. */
//unused: class l2dlWriteError : public GSMError { };
//unused: #define L2_WRITE_ERROR {throw l2dlWriteError();}
//@}

/**
	LAPD states, Q.921 4.3.
	We have few states than vISDN LAPD because LAPDm is simpler.
*/
enum LAPDState {
	LAPDStateUnused,
	LinkReleased,			// (pat) a.k.a. Idle state 4.06 5.4.5.
	AwaitingEstablish,		///< note that the BTS should never be in this state  (pat) Incorrect, state is used during link establishment.
	AwaitingRelease,
	LinkEstablished,
	ContentionResolution	///< GMS 04.06 5.4.1.4
};



/**
	Skeleton for data link layer (L2) entities.
	This is a base class from which the various channel classes are derived.
	Many derived classes are "thin" and do not implement full LAPDm.
	This is especially true of the downlink-only classes which do not have
	equivalents in Q.921 and HDLC.
	// (pat) The idea is that each radio channel has multiple uplink data paths (usually two: SAPI0 and SAPI3)
	// each with their own LAPDm state machine.  All are associated with the same L2LogicalChannel.
*/
class L2DL {

	protected:

	friend class L2SAPMux;
	L2SAPMux *mL2Downstream;		///< a pointer to the lower layer

	public:

	L2DL() :mL2Downstream(NULL) { }

	virtual ~L2DL() {}


	void l2Downstream(L2SAPMux *wDownstream) { mL2Downstream = wDownstream; }

	virtual void l2dlOpen(std::string wDescriptiveString) = 0;

	/** N201 value for a given frame format on this channel, GSM 04.06 5.8.3. */
	virtual unsigned N201(GSM::L2Control::ControlFormat) const = 0;

	/** N200 value for this channel, GSM 04.06 5.8.2. */
	// (pat) The values for N200 herein are for state timerRecovery; otherwise N200 is supposed to be 5.
	virtual unsigned N200() const = 0;

	/** T200 timeout for this channel, GSM 04.06 5.8.1. */
	virtual unsigned T200() const { return T200ms; }

	/** Check for establishment of multifame mode; only valid for LAPDm. */
	virtual bool multiframeMode() const { assert(0); }

	/**
		The L3->L2 interface.
		This is a blocking call and does not return until
		all of the corresponding radio bursts have been
		enqueued for transmission.
		That can take up to 1/2 second.
		(pat) Above comment is for channels without LAPDm;
		channels with LAPDm (the DCCH channels, ie, SACCH, FACCH, and SDCCH) can block
		until they receive an ACK from the MS.  If the MS has wandered out of range
		that will be until the N200*T200 LAPDm timeout, which is 30.6 secs for FACCH, 20.7 secs for SDCCH, 4.5s for SACCH.
	*/
	virtual void l2dlWriteHighSide(const GSM::L3Frame&) = 0;


	/** The L1->L2 interface */
	virtual void l2dlWriteLowSide(const GSM::L2Frame&) = 0;

	/** The L2->L3 interface. */
	//virtual L3Frame* l2ReadHighSide(unsigned timeout=3600000) = 0;

	// (pat) Never called on non-LAPDm channels, but let's return 0 rather than crashing.
	virtual LAPDState getLapdmState() const { return LAPDStateUnused; }
};



/**
	A "thin" L2 for CBCH.
	This is a downlink-only channel and does not use LAPDm.
	See GSM 04.12 3.3.1.
*/
class CBCHL2 : public L2DL {

	public:

	unsigned N201(GSM::L2Control::ControlFormat format) const { if (format) {} assert(0); }	// The 'if' shuts up gcc.

	unsigned N200() const { return 0; }

	void l2dlOpen(std::string) {}

	void l2dlWriteLowSide(const GSM::L2Frame&) { assert(0); }

	//L3Frame* l2ReadHighSide(unsigned timeout=3600000) { if (timeout) {} assert(0); return NULL; }	// The 'if' shuts up gcc.

	void l2dlWriteHighSide(const GSM::L3Frame&);

};






/**
	LAPDm == Link Access Procedure on the Dm channel.
	LAPDm transceiver, GSM 04.06, borrows from ITU-T Q.921 (LAPD) and ISO-13239 (HDLC).
	Dedicated control channels need full-blown LAPDm.

	LAPDm is best be thought of as lightweight HDLC.
    The main differences between LAPDm and HDLC are: 
		- LAPDm allows no more than one outstanding unacknowledged I-frame (k=1, GSM 04.06 5.8.4).
		- LAPDm does not support extended header formats (GSM 04.06 3).
		- LAPDm supports only the SABM, DISC, DM, UI and UA U-Frames (GSM 04.06 3.4, 3.8.1).
		- LAPDm supports the RR and REJ S-Frames (GSM 04.06 3.4, 3.8.1), but not RNR (GSM 04.06 3.8.7, see Note).
		- LAPDm has just one internal timer, T200.
		- LAPDm supports only one terminal endpoint, whose TEI is implied.
		- LAPDm should always be able to enter ABM when requested.
		- LAPDm can never be in a recevier-not-ready condition (GSM 04.06 3.8.7 , see Note).

	In a first release, we can simplify further by:
		- not supporting the Bter short header format
		- acking each received I-frame with an RR frame, even when outgoing I-frames are pending
		- using the Bbis format for L3 messages that use the L2 pseudolength element
		- just using independent L2s for each active SAP
		- just using independent L2s on each dedicated channel, which works with k=1
*/

class L2LAPDm : public L2DL {

	public:
	std::string myid;		// The descriptive string from the LogicalChannel, used only in user messages.


	private:

	Thread mUpstreamThread;		///< a thread for upstream traffic and T200 timeouts
	bool mRunning;				///< true once the service loop starts
	protected:
	//L3FrameFIFO mL3Out;			///< we connect L2->L3 through a FIFO
	private:
	L2FrameFIFO mL1In;			///< we connect L1->L2 through a FIFO

	unsigned mC;			///< the "C" bit for commands, 1 for BTS, 0 for MS
							// (pat) C is ALWAYS 1 and R 0 for us, so why is it an argument to the constructor?  For testing?
	unsigned mR;			///< this "R" bit for commands, 0 for BTS, 1 for MS

	SAPI_t mSAPI;			///< the service access point indicator for this L2

	L2LAPDm *mMaster;		///< This points to the SAP0 LAPDm on this channel.

	/**@name Mutex-protected state shared by uplink and downlink threads. */
	//@{
	mutable Mutex mLock;
	/**@name State variables from GSM 04.06 3.5.2 */
	//@{
	unsigned mVS;			///< GSM 3.5.2.2, Q.921 3.5.2.2, send counter, NS+1 of last sent I-frame
	unsigned mVA;			///< GSM 3.5.2.3, Q.921 3.5.2.3, ack counter, NR+1 of last acked I-frame
	unsigned mVR;			///< GSM 3.5.2.5, Q.921 3.5.2.5, recv counter, NR+1 of last recvd I-frame
	LAPDState mState;		///< current protocol state
	Signal mAckSignal;		///< signal used to wake a thread waiting for an ack
	//@}
	Bool_z mEstablishmentInProgress;	///< flag described in GSM 04.06 5.4.1.4
	/**@name Segmentation and retransmission. */
	//@{
	BitVector2 mRecvBuffer;	///< buffer to concatenate received I-frames, same role as sk_rcvbuf in vISDN
	L2Frame mSentFrame;		///< previous ack-able kept for retransmission, same role as sk_write_queue in vISDN
	bool mDiscardIQueue;		///< a flag used to abort I-frame sending
	unsigned mContentionCheck;	///< checksum used for contention resolution, GSM 04.06 5.4.1.4.
	unsigned mRC;				///< retransmission counter, GSM 04.06 5.4.1-5.4.4
	Z100Timer mT200;			///< retransmission timer, GSM 04.06 5.8.1
	size_t mMaxIPayloadBits;	///< N201*8 for the I-frame
	//@}
	//@}

	/** A handy idle frame. */
	// (pat) This is sent from TCHFACCHL1Encoder::dispatch()
	L2Frame mIdleFrame;

	/** A lock to control multi-threaded access to L1->L2. */
	Mutex mL1Lock;

	/** HACK -- A count of consecutive idle frames. Used to spot stuck channels. */
	unsigned mIdleCount;

	/** HACK -- Return maximum allowed idle count. */
	virtual unsigned maxIdle() const =0;

	public:

	/**
		Construct a LAPDm transceiver.
		@param wC "Command" bit, "1" for BTS, "0" for MS,
			GSM 04.06 3.3.2.
		@param wSAPI Service access point indicatior,
			GSM 040.6 3.3.3.
	*/
	L2LAPDm(unsigned wC=1, SAPI_t wSAPI=SAPI0);

	virtual ~L2LAPDm() {}


	/** Process an uplink L2 frame. */
	void l2dlWriteLowSide(const GSM::L2Frame&);

	/**
		Read the L3 output, with a timeout.
		Caller is responsible for deleting returned object.
	*/
	//L3Frame* l2ReadHighSide(unsigned timeout=3600000)
	//	{ LOG(DEBUG); return mL3Out.read(timeout); }

	/**
		Process a downlink L3 frame.
		This is a blocking call and does not return until
		all of the corresponding radio bursts have been
		enqueued for transmission.
		That can take up to 1/2 second.
	*/
	void l2dlWriteHighSide(const GSM::L3Frame&);


	/** Prepare the channel for a new transaction. */
	virtual void l2dlOpen(std::string wDescriptiveString);

	/** Set the "master" SAP, SAP0; should be called no more than once. */
	void master(L2LAPDm* wMaster)
		{ assert(!mMaster); mMaster=wMaster; }

	/** Return true if in multiframe mode. */
	bool multiframeMode() const
		{ ScopedLock lock(mLock); return mState==LinkEstablished; }


	protected:

	/** Block until we receive any pending ack. */
	void waitForAck();

	/** Send an L2Frame on the L2->L1 interface. */
	void writeL1(const L2Frame&);
	/** Send an L3Frame upstream on the L2->L3 interface. */
	virtual void writeL3(L3Frame *f);	// Over-ridden only by SACCHL2

	void writeL1Ack(const L2Frame&);			///< send an ack-able frame on L2->L1
	void writeL1NoAck(const L2Frame&);			///< send a non-acked frame on L2->L1

	/** Abort the link. */
	void linkError();

	/** Clear the state variables to released condition. */
	void releaseLink(bool notifyL3,Primitive releaseType /*=RELEASE*/);

	/** Clear the ABM-related state variables. */
	void clearCounters();

	/** Go to the "link released" state. */
	void releaseLink(Primitive releaseType);
	
	/** We go here when something goes really wrong. */
	void abnormalRelease(bool sendDM);
	void normalRelease();

	/** Abort link on unexpected message. */
	void unexpectedMessage(int whence);

	/** Process an ack.  Also forces state to LinkEstablished. */
	void processAck(unsigned NR);

	/** Retransmit last ackable frame. */
	void retransmissionProcedure();

	/** Clear any outgoing L3 frame. */
	//void discardIQueue() { mDiscardIQueue=true; }

	/**
		Accept and concatenate an I-frame data payload.
		GSM 04.06 5.5.2 (first 2 bullet points)
	*/
	void bufferIFrameData(const L2Frame&);

	/**@name Receive-handlers for the various frame types. */
	//@{
	void receiveFrame(const L2Frame&);			///< Top-level frame handler.
	/* 
		We will postpone support for suspension/resumption of multiframe mode (GSM 04.06 5.4.3).
		This will greatly simplify the L2 state machine.
	*/
	void receiveIFrame(const L2Frame&);			///< GSM 04.06 3.8.1, 5.5.2
	/**@name U-Frame handlers */
	//@{
	void receiveUFrame(const L2Frame&);			///< sub-dispatch for all U-Frames
	void receiveUFrameSABM(const L2Frame&);		///< GMS 04.06 3.8.2, 5.4.1
	void receiveUFrameDISC(const L2Frame&);		///< GSM 04.06 3.8.3, 5.4.4.2
	void receiveUFrameUI(const L2Frame&);		///< GSM 04.06 3.8.4, 5.2.1
	void receiveUFrameUA(const L2Frame&);		///< GSM 04.06 3.8.8
	void receiveUFrameDM(const L2Frame&);		///< GSM 04.06 3.8.9, 5.4.4.2
	//@}
	/**@name S-Frame handlers */
	//@{
	void receiveSFrame(const L2Frame&);			///< sub-dispatch for all S-Frames
	void receiveSFrameREJ(const L2Frame&);		///< GSM 04.06 3.8.6, 5.5.3
	void receiveSFrameRR(const L2Frame&);		///< GSM 04.06 3.8.5, 5.5.4
	//@}
	//@}

	/**@name Senders for various frame types. */
	//@{
	/*
		In vISDN, these are performed with functions
			output.c:lapd_send_uframe, datalink.c:lapd_send_sframe,
			output.c:lapd_prepare_uframe, output.c:lapd_prepare_iframe.
		We've broken these out into a specific function for each
		frame type.  For example, to send a DISC in vISDN, you call
		lapd_send_uframe with arguments that specify the DISC frame.
		In OpenBTS, you just call sendUFrameDISC.
	*/
	bool sendMultiframeData(const L3Frame&);	///< send an L3 frame in one or more I-frames
	void sendIFrame(const BitVector2&, bool);	///< GSM 04.06 3.8.1, 5.5.1, with payload and "M" flag
	void sendUFrameSABM();						///< GMS 04.06 3.8.2, 5.4.1
	void sendUFrameDISC();						///< GSM 04.06 3.8.3, 5.4.4.2
	void sendUFrameUI(const L3Frame&);			///< GSM 04.06 3.8.4, 5.2.1
	void sendUFrameUA(bool FBit);				///< GSM 04.06 3.8.8, 5.4.1, 5.4.4.2
	void sendUFrameUA(const L2Frame&);			///< GSM 04.06 3.8.8, 5.4.1.4
	void sendUFrameDM(bool FBit);				///< GMS 04.06 3.8.9, 5.4.4.2
	void sendSFrameRR(bool FBit);				///< GSM 04.06 3.8.5, 5.5.2
	void sendSFrameREJ(bool FBit);				///< GSM 04.06 3.8.6, 5.5.2
	//@}

	/**
		Handle expiration of T200.
		See GSM 04.06 5.8.1 for a definition of T200.
		See GSM 04.06 5.4.1.3, 5.4.4.3, 5.5.7, 5.7.2 for actions to take upon expiration of T200.
	*/
	void T200Expiration();

	/**
		Send the idle frame, GMS 04.06 5.4.2.3.
		This sends one idle frame to L1, but that sets up the modulator to start
		generating the idle pattern repeatedly.  See README.IdleFilling.
			- This should be called in SAP0 when a channel is first opened.
			- This should be called after sending any frame
				when no further outgoing frames are pending.
			- This should be called after receiving a REJ frame.
			- This need not be called when the channel is closed,
				as L1 will generate its own filler pattern that is more
				appropriate in this condition.
			- This does not need to be called for the SACCH or FACCH.
	*/
	virtual void sendIdle() { writeL1(mIdleFrame); }

	/**
		Increment or clear the idle count based on the current frame.
		@return true if we should abort
	*/
	bool stuckChannel(const L2Frame&);

	/**
		The upstream service loop handles incoming L2 frames from L1 and T200 timeouts.
	*/
	void lapServiceLoop();

	friend void *LAPDmServiceLoopAdapter(L2LAPDm*);

	public:
	LAPDState getLapdmState() const { return mState; }
	void text(std::ostream&os) const;
};


std::ostream& operator<<(std::ostream&, LAPDState);
std::ostream& operator<<(std::ostream&, L2LAPDm&);
std::ostream& operator<<(std::ostream&, L2LAPDm*);	// such a great language


/** C-style adapter for LAPDm serice loop. */
void *LAPDmServiceLoopAdapter(L2LAPDm*);



class SDCCHL2 : public L2LAPDm {

	protected:

	unsigned maxIdle() const { return 50; }

	/** GSM 04.06 5.8.3.  We support only A/B formats. */
	unsigned N201(GSM::L2Control::ControlFormat format) const
		{ assert(format==L2Control::IFormat); return 20; }

	/** GSM 04.06 5.8.2.1 */
	unsigned N200() const { return 23; }

	public:

	/**
		Construct the LAPDm part of the SDCCH.
		@param wC "Command" bit, "1" for BTS, "0" for MS.
		@param wSAPI Service access point indicatior.
	*/
	SDCCHL2(unsigned wC=1, SAPI_t wSAPI=SAPI0)
		:L2LAPDm(wC,wSAPI)
	{ }

};



/**
	Link Layer for Slow Associated Control Channel (SACCH).
	GSM 04.06 2 states that UI frames on the SACCH use format B4.
	However, in GSM 04.08 you see that all messages sent on the SACCH
	in the UI mode use an "L2 pseudolength" field.  So we can greatly
	simplify the SACCH by just using the B format and letting the
	length field of the B format stand in for the "L2 pseudolength"
	field, since their content is identical.  This will work well enough
	until we need to support L3 "rest octets" on the SACCH.
*/
class SACCHL2 : public L2LAPDm {

	protected:

	unsigned maxIdle() const { return 1000; }

	/** GSM 04.06 5.8.3.  We support only A/B formats. */
	unsigned N201(GSM::L2Control::ControlFormat format) const
		{ assert(format==L2Control::IFormat); return 18; }

	/** GSM 04.06 5.8.2.1 */
	unsigned N200() const { return 5; }

	/** T200 timeout for this channel, GSM 04.06 5.8.1. */
	unsigned T200() const { return 4*T200ms; }

	/** SACCH does not use idle frames. */
	void sendIdle() {};

	// same as base class: void writeL3(L3Frame *f);	// Over-ridden only by SACCHL2

	public:

	/**
		Construct the LAPDm part of the SACCH.
		@param wC "Command" bit, "1" for BTS, "0" for MS.
		@param wSAPI Service access point indicatior.
	*/
	SACCHL2(unsigned wC=1, SAPI_t wSAPI=SAPI0)
		:L2LAPDm(wC,wSAPI)
	{ }

};



/**
	Link Layer for Fast Associated Control Channel (FACCH).
*/
class FACCHL2 : public L2LAPDm {

	protected:

	unsigned maxIdle() const { return 500; }

	/** GSM 04.06 5.8.3.  We support only A/B formats. */
	unsigned N201(GSM::L2Control::ControlFormat format) const
		{ assert(format==L2Control::IFormat); return 20; }

	/** GSM 04.06 5.8.2.1 */
	unsigned N200() const { return 34; }

	/** FACCH does not need idle frames. */
	void sendIdle() {};

	public:

	/**
		Construct the LAPDm part of the FACCH.
		@param wC "Command" bit, "1" for BTS, "0" for MS.
		@param wSAPI Service access point indicatior.
	*/
	FACCHL2(unsigned wC=1, SAPI_t wSAPI=SAPI0)
		:L2LAPDm(wC,wSAPI)
	{ }

};





}; // namespace GSM

#endif



// vim: ts=4 sw=4
