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

// Written by Pat Thompson

#define LOG_GROUP LogGroup::Control		// Can set Log.Level.Control for debugging
#include "L3Utils.h"
#include "L3StateMachine.h"
#define CASENAME(x) case x: return #x;

namespace Control {

	const char *TimerId2Str(L3TimerId tid)
	{
		switch (tid) {
			CASENAME(T301)
			CASENAME(T302)
			CASENAME(T303)
			CASENAME(T304)
			CASENAME(T305)
			CASENAME(T308)
			CASENAME(T310)
			CASENAME(T313)
			CASENAME(T3101)
			CASENAME(T3113)
			CASENAME(T3260)
			CASENAME(T3270)
			CASENAME(TR1M)
			CASENAME(TR2M)
			CASENAME(THandoverComplete)
			CASENAME(TSipHandover)
			CASENAME(TMisc1)
			CASENAME(TCancel)
			CASENAME(TMMCancel)
			case cNumTimers: break;
			// The last entry is not a timer - it is the number of max number of timers defined.
		};
		return "invalid";
	}
	const char * L3Timer::tName() const { return TimerId2Str(mTimerId); }

	// Start a timer that will abort the procedure.
	//void L3TimerList::timerStartAbortTran(L3TimerId tid, long wEndtime) {
		//mtlTimers[tid].tStart(tid,wEndtime,L3Timer::AbortTran);
	//}
	void L3TimerList::timerStart(L3TimerId tid, unsigned wEndtime, int nextState) {
		mtlTimers[tid].tStart(tid,wEndtime,nextState);
	}
	void L3TimerList::timerStop(L3TimerId tid) {
		mtlTimers[tid].tStop();
	}
	void L3TimerList::timerStopAll() {
		for (int tid = 0; tid < cNumTimers; tid++) {
			mtlTimers[tid].tStop();
		}
	}
	bool L3TimerList::timerExpired(L3TimerId tid) {
		return mtlTimers[tid].tIsExpired();
	}

	// Trigger any expired timers.  Return true if any have gone off.
	bool L3TimerList::checkTimers() {
		for (int tid = 0; tid < cNumTimers; tid++) {
			if (mtlTimers[tid].tIsExpired()) {
				mtlTimers[tid].tStop();
				lockAndInvokeTimeout(&mtlTimers[tid]);
				// Since the timer can result in the transaction being killed, only trigger one timer
				// then we return to let the caller invoke us again if the transaction is still active.
				return true;
			}
		}
		return false;
	}

	void L3TimerList::text(std::ostream &os) const {
		for (int tid = 0; tid < cNumTimers; tid++) {
			const L3Timer *timer = &mtlTimers[tid];
			if (timer->tIsExpired()) { os <<LOGVAR2(timer->tName(),"expired"); }
			if (timer->tRemaining()) { os <<LOGVAR2(timer->tName(),timer->tRemaining()); }
		}
	}

	// Return the remaining time of any timers, or nextTimeout if none active.
	// The idiom is to pass it -1, and it returns it if no timers running.
	// This was meant for the single-thread version of the State Machines and is not currently used.
	int L3TimerList::remainingTime() {
		int nextTimeout = -1;
		for (L3TimerId tid = (L3TimerId)0; tid < cNumTimers; tid = (L3TimerId)(tid + 1)) {
			if (mtlTimers[tid].tIsActive()) {
				int remaining = mtlTimers[tid].tRemaining();
				if (nextTimeout == -1) {
					nextTimeout = remaining;
				} else {
					nextTimeout = min(nextTimeout,remaining);
				}
			}
		}
		return nextTimeout;
	}
};
