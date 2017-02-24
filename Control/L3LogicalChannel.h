/*
* Copyright 2013, 2014 Range Networks, Inc.
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
#ifndef _L3LOGICALCHANNEL_H_
#define _L3LOGICALCHANNEL_H_ 1
#include "ControlTransfer.h"
#include "L3TermCause.h"
//#include <GSML3RRElements.h>
#include <L3Enums.h>
#include <GSMTransfer.h>

namespace Control {
using namespace GSM;
class TranEntry;
class MMContext;

class L3LogicalChannel {
	friend class AssignTCHMachine;
	friend void L3DCCHLoop(L3LogicalChannel*dcch, L3Frame*frame);

	// Normally only one thread accesses each LogicalChannel, however, during reassignment the LogicalChannel
	// is accessed from the thread of the previous channel.  I dont think conflicts are possible, but to be
	// safe we will use a Mutex to protect those methods.
	// Update: There are so many penetrations through L3LogicalChannel into MMContext, which must be locked
	// with gMMLock, that I gave up on having a separate lock here and just use gMMLock.
	// mutable Mutex mChanLock;
#if UNUSED
	//L3FrameFIFO ml3DownlinkQ;

	// New way.  This is only used for RR and CC messages which are always on SAPI 0.
	//void l3sendm(const L3Message& msg);
	// (pat) Stick a primitive in the uplink queue.
	//void l2uplinkEneuquep(const GSM::Primitive& prim, unsigned SAPI=0)
	//	{ assert(mL2[SAPI]); mL2[SAPI]->l2WriteHighSide(GSM::L3Frame(prim)); }

	// If there is an ongoing voice call, we need the SIP pointer.
	// For convenience I am using the TransactionEntry to find the voice data,
	// but do NOT use this TransactionEntry for anything else; there may be multiple TransactionEntries associated
	// with each logical channel.  For example, L3 messages should be sent to the
	// L3 state machines in case the TI [Transaction Identifier] in the message does not match this TransactionEntry.
	//TranEntry *mVoiceTrans;
#endif
	// DCCH Channels go through two regimes in both GSM and UMTS.
	// When the channel is first granted to an MS the MS is usually unidentified,
	// meaning it sent us a TMSI but we either do not know or do not trust the TMSI->IMSI mapping.
	// This is Regime 1, there is only an MMContext, and only MO operations are permitted.
	// After the MS is identified, we enter the Regime 2 where the channel is also associated with an MMUser,
	// and can process (possibly multiple simultaneous) MT transactions.
	// We may never enter Regime 2, meaning we may never associate an IMSI-identified MMUser with this channel,
	// for example, for SOS calls.

	// The MMContext holds the active Transactions on a channel.
	// It can be moved to a different channel by RR Procedures.
	// (In contrast, an MMUser is associated with an IMSI.)
	MMContext *mChContext;	// When set we increment the use count in the MMContext.
	protected:
	L3LogicalChannel *mNextChan;	// Used in GSM during channel reassignment.
	//L3LogicalChannel *mPrevChan;

	public:
	bool chanRunning();
	//InterthreadQueue<L3Frame> ml3UplinkQ;	// uplink SACCH message are enqueued here.

	private:
	// This can be thought of as the RR state, as known from an L3 perspective.
	// It informs the service loop that we want to release the channel, since we dont want
	// to send things on the channel from some other channel's thread.
	// It is GSM-specific, not used in UMTS.
	enum ChannelState {
		chIdle,				// Not assigned to any MS.
		chEstablished,		// Assigned to a MS
		chRequestRelease,	// controlling thread requests RELEASE.
		chRequestHardRelease,	// controlling thread requests HARDRELEASE.
		chReassignTarget,		// State of the channel we are assigning to, until it is established.
		//chReleased,			// Channel released by Layer3 by sending RELEASE or HARDRELEASE to layer 2.
		// These two states could be combined since they are functionally the same, but nice for debugging to tell what happened.
		// No longer used:
		//chReassignPending,		// This is an SDCCH with pending reassignment to TCH.
		//chReassignComplete, // This is an SDCCH after successful reassignment to TCH, ie, we are going to release it momentarily.
		//chReassignFailure	// This channel is the reassignment target; needs to be released after a reassignment failure.
	} volatile mChState;
	static const char *ChannelState2Text(ChannelState chstate);
	void chanSetState(ChannelState wChState) { mChState = wChState; }

	public:
	// Pass-throughs from Layer2.  These will be different for GSM or UMTS.
	virtual GSM::L3Frame * l2recv(unsigned timeout_ms = 15000) = 0;
	// In GSM the l2send methods may block in LAPDm.
	virtual void l2sendf(const GSM::L3Frame& frame) = 0;
	virtual void l2sendm(const GSM::L3Message& msg, GSM::Primitive prim=GSM::L3_DATA, SAPI_t SAPI=SAPI0) = 0;
	virtual void l2sendp(const GSM::Primitive& prim, SAPI_t SAPI=SAPI0) = 0;
	void l3sendm(const GSM::L3Message& msg, const GSM::Primitive& prim=GSM::L3_DATA, SAPI_t SAPI=SAPI0);
	void l3sendf(const GSM::L3Frame& frame);
	void l3sendp(const GSM::Primitive& prim, SAPI_t SAPI=SAPI0);
	//unused virtual unsigned N200() const = 0;
	virtual bool multiframeMode(SAPI_t SAPI) const = 0;	// Used by SMS code.
	virtual const char * descriptiveString() const;
	virtual bool radioFailure() const = 0;
	virtual GSM::ChannelType chtype() const =0;
	bool isSDCCH() const;
	bool isTCHF() const;
	bool isReleased() const { return mChState == chRequestRelease || mChState == chRequestHardRelease; }


	// Return the L2 info given the L3LogicalChannel.
	GSM::L2LogicalChannel *getL2Channel();	// This method will not work under UMTS.
	const GSM::L2LogicalChannel *getL2Channel() const;	// Stupid language.
	L3LogicalChannel* getSACCHL3();

	MMContext *chanGetContext(bool create);
	void chanSetHandoverPenalty(NeighborPenalty &penalty);
	std::string chanGetImsi(bool verbose) const;	// If the IMSI is known, return it, else string("") or if verbose, something to display in error messages and CLI.
	time_t chanGetDuration() const;
	//void chanSetContext(MMContext* wTranSet);
	void chanFreeContext(TermCause cause);
	void reassignComplete();
	void reassignFailure();
	void reassignStart();
	bool reassignAllocNextTCH();

	//MMUser *getMMC() { return mMMC; }
	//void setMMC(MMUser *mmc) { mMMC = mmc; }
	//void chanLost();
	// Send a channel release message, then release it.
	void chanClose(GSM::RRCause cause,		// cause sent to the handset.
		GSM::Primitive prim,	// prim is RELEASE or HARDRELEASE
		TermCause upstreamCause);	// All active transactions closed with this - sent upstram via SIP.
	// Request an immediate channel release.
	// Dont use HARDRELEASE if you can avoid it - only used when the channel is already completely cleared.
	void chanRelease(Primitive prim,TermCause cause);

	RefCntPointer<TranEntry> chanGetVoiceTran();
	//void chanSetVoiceTran(TranEntry *trans);
	//void chanEnqueueFrame(L3Frame *frame);

	/** Block until a HANDOVER_ACCESS or ESTABLISH arrives. */
	GSM::L3Frame* waitForEstablishOrHandover();

	void L3LogicalChannelReset();
	void L3LogicalChannelInit();
	L3LogicalChannel() { L3LogicalChannelInit(); }
	// In GSM the L3LogicalChannel is attached to the various DCCH at startup and is immortal.
	// In UMTS there is one LogicalChannel per UE, destroyed when we lose contact with the UE.
	void getTranIds(TranEntryList &tids) const;
	~L3LogicalChannel();
	std::ostream& chanText(std::ostream&os) const;
	std::ostream& chanContextText(std::ostream&os) const;
}; 

extern void printChansInfo(std::ostream&os);


std::ostream& operator<<(std::ostream&, const L3LogicalChannel&);
std::ostream& operator<<(std::ostream&os, const L3LogicalChannel*ch);
};
#endif
