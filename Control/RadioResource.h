/**@file GSM Radio Resource procedures, GSM 04.18 and GSM 04.08. */
/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011 Range Networks, Inc.
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

#ifndef RADIORESOURCE_H
#define RADIORESOURCE_H

#include <list>
#include <Interthread.h>
#include "ControlCommon.h"
#include <GSML3CommonElements.h>


namespace GSM {
class Time;
class TCHFACCHLogicalChannel;
class SACCHLogicalChannel;
class L3PagingResponse;
class L3AssignmentComplete;
class L3HandoverComplete;
class L3HandoverAccess;
class L3MeasurementResults;
};

namespace Control {

class TransactionEntry;




/**
	Determine whether or not to initiate a handover.
	@param measurements The measurement results from the SACCH.
	@param SACCH The SACCH in question.
*/
void HandoverDetermination(const GSM::L3MeasurementResults &measurements, float myRxLev, GSM::SACCHLogicalChannel* SACCH);


/** Find and complete the in-process transaction associated with a paging repsonse. */
void PagingResponseHandler(const GSM::L3PagingResponse*, L3LogicalChannel*);

/** Find and compelte the in-process transaction associated with a completed assignment. */
void AssignmentCompleteHandler(const GSM::L3AssignmentComplete*, GSM::TCHFACCHLogicalChannel*);

/** Save handover parameters from L1 in the proper transaction record. */
bool SaveHandoverAccess(unsigned handoverReference, float RSSI, float timingError, const GSM::Time& timestamp);

/** Process the handover access; returns when the transaction is cleared. */
void ProcessHandoverAccess(L3LogicalChannel *chan);
bool outboundHandoverTransfer(TranEntry* transaction, L3LogicalChannel *TCH);

/**@ Access Grant mechanisms */
//@{


/** Decode RACH bits and send an immediate assignment; may block waiting for a channel. */
//void AccessGrantResponder(
//	unsigned requestReference, const GSM::Time& when,
//	float RSSI, float timingError);


/** This record carries all of the parameters associated with a RACH burst. */
class ChannelRequestRecord {

	private:

	unsigned mRA;		///< request reference
	GSM::Time mFrame;	///< receive timestamp
	float mRSSI;		///< dB wrt full scale
	float mTimingError;	///< correlator timing error in symbol periods

	public:

	ChannelRequestRecord(
		unsigned wRA, const GSM::Time& wFrame,
		float wRSSI, float wTimingError)
		:mRA(wRA), mFrame(wFrame),
		mRSSI(wRSSI), mTimingError(wTimingError)
	{ }

	unsigned RA() const { return mRA; }
	const GSM::Time& frame() const { return mFrame; }
	float RSSI() const { return mRSSI; }
	float timingError() const { return mTimingError; }

};


/** A thread to process contents of the channel request queue. */
void* AccessGrantServiceLoop(void*);


//@}

/**@ Paging mechanisms */
//@{

/** An entry in the paging list. */

struct NewPagingEntry {
	bool mWantCS;			// true for CS service requested, false for anything else, which is SMS.
	std::string mImsi;		// Always provided.
	TMSI_t mTmsi;			// Provided if known and we are sure it has been assigned to the MS.
	// Such a clever language.
	NewPagingEntry(bool wWantCS, std::string &wImsi, TMSI_t &wTmsi) : mWantCS(wWantCS), mImsi(wImsi), mTmsi(wTmsi) {}

	GSM::ChannelType getChanType();
	GSM::L3MobileIdentity getMobileId();
};
typedef std::vector<NewPagingEntry> NewPagingList_t;



/**
	The pager is a global object that generates paging messages on the CCCH.
	To page a mobile, add the mobile ID to the pager.
	The entry will be deleted automatically when it expires.
	All pager operations are linear time.
	Not much point in optimizing since the main operation is inherently linear.
*/
class Pager {

	private:

	Thread mPagingThread;					///< Thread for the paging loop.
	volatile bool mRunning;

	public:

	Pager()
		:mRunning(false)
	{}

	/** Set the output FIFO and start the paging loop. */
	void start();

	private:

	/** A loop that repeatedly calls pageAll. */
	void serviceLoop();

	/** C-style adapter. */
	friend void *PagerServiceLoopAdapter(Pager*);

public:

	/** return size of PagingEntryList */
	size_t pagingEntryListSize();
};


void *PagerServiceLoopAdapter(Pager*);



//@}	// paging mech

}


#endif
