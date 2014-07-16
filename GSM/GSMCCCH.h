/* 
* Copyright 2014 Range Networks, Inc.
*

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

*/
// Written 3-2014 by Pat Thompson
#ifndef GSMCCCH_H
#define GSMCCCH_H 1


#include "GSMLogicalChannel.h"
#include "GSMCommon.h"
#include "GSMTDMA.h"
#include "PagingEntry.h"

namespace GSM {

class NewPager {
	Thread mPagingThread;					///< Thread for the paging loop.
	volatile bool mRunning;

	public:

	NewPager() :mRunning(false) {}

	/** Set the output FIFO and start the paging loop. */
	void start();

	/** A loop that repeatedly calls pageAll. */
	void serviceLoop();

	/** C-style adapter. */
	static void *PagerServiceLoopAdapter(NewPager*);

public:
	/** return size of PagingEntryList */
	size_t pagingEntryListSize();
};
extern NewPager gPager;
extern void PagerStart();

struct FrontOrBack {
	enum Dir { Front, Back };
};

class CCCHLogicalChannel : public NDCCHLogicalChannel
{
	private:
	friend class GSMConfig;

	unsigned mCcchGroup;	///< CCCH group: 0,1,2,3 corresponding to timeslots 0,2,4,6 on ARFCN 0.
	bool mRunning;			///< a flag to indication that the service loop is running
	Thread mServiceThread;	///< a thread for the service loop
	GSM::Time mCcchNextWriteTime;	///< Indicates frame currently being serviced.

	int mRevPCH[51]; // Reverse index of frame number to paging block number, B0 .. B8.

	public:

	CCCHLogicalChannel(unsigned wCcchGroup, const TDMAMapping& wMapping);

	void ccchOpen();
	void ccchServiceLoop();
	bool ccchServiceQueue();
	void sendReject(RachInfo *rach, int priority);
	void sendRawReject(RachInfo *rach, int delaysecs);	// Testing routine.
	bool processRaches();
	bool processPages(
		);
	bool sendGprsCcchMessage(Control::NewPagingEntry *gprsMsg, GSM::Time &frameTime);

	ChannelType chtype() const { return CCCHType; }
};

extern int gCcchTestIAReject;	// If non-zero, send an ImmediateAssignmentReject to every RACH with a delay time set from this var.

extern void pagerAddCcchMessageForGprs(Control::NewPagingEntry *npe);
extern int getAGCHLoad();
extern int getPCHLoad();
extern int getAGCHPending();
extern unsigned getT3122();
extern void enqueueRach(RachInfo *rip);

}; // namespace
#endif // GSMCCCH_H
