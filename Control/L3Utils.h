/**@file Declarations for Circuit Switched State Machine and related classes. */
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

#ifndef _L3UTILS_H_
#define _L3UTILS_H_
#include <GSMCommon.h>		// Where Z100Timer lives.

namespace Control {
enum L3TimerId {
	T301,		///< recv ALERT --> recv CONN
	T302,		///< send SETUP ACK --> any progress
	T303,		///< send SETUP --> recv CALL CONF or REL COMP  On network side, MTC only.
	T304,	// TODO: (pat)cant find it		///< recv SETUP ACK --> any progress
	T305,		///< send DISC --> recv REL or DISC
	T308,		///< send REL --> rev REL or REL COMP
	T310,		///< recv CALL CONF --> recv ALERT, CONN, or DISC
	T313,		///< send CONNECT --> recv CONNECT ACK
	TCancel,	// Generic cancellation timer.

	// Mobility Management Timers.  TODO: These should be in the MMContext.
	T3101,	// Reassignment failure.
	T3113, // for paging.
			// Note that there is a 10s timeout in the MS between any two MM commands.
	T3260,	// AuthenticationRequest -> AuthenticationResponse. 12sec.
	T3270,	// IdentityRequest -> IdentityResponse. 12sec.
	// THandoverComplete is not a GSM timer.  We use it to make sure we get a HandoverComplete message.
	THandoverComplete,
	// TSipHandover is not a GSM timer - it is how long we wait on the SIP side for the peer response during handover
	// before dropping the GSM side connection.  It should be nearly instantaneous but we must avoid hanging in this state.
	TSipHandover,

	TMisc1,	// Generic timer for use by whoever needs one.
	TMMCancel,	// Generic cancellation timer.
	TR1M,		// MO-SMS timer.
	TR2M,		// MT-SMS timer.
	cNumTimers // The last entry is not a timer - it is the number of max number of timers defined.
	};

typedef enum L3TimerId MMTimerId;
typedef enum L3TimerId TranTimerId;

// The timer table used for L3Rewrite.
#if 0
class L3TimerTable {
	const char *mNames[cNumTimers];
	ml3Timers[cNumTimers];
	//L3Timer mT301, mT302, mT303, mT304, mT305, mT308, mT310, mT313, mT3113, mTRIM;

	initValues {
		ml3Timers[T301].load(T301ms);
		ml3Timers[T302].load(T302ms);
		ml3Timers[T303].load(T303ms);
		ml3Timers[T304].load(T304ms);
		ml3Timers[T305].load(T305ms);
		ml3Timers[T308].load(T308ms);
		ml3Timers[T310].load(T310ms);
		ml3Timers[T313].load(T313ms);
		ml3Timers[T3113].load(T3113ms);
	};
	static initNames() {
		for (TimerTag t = T301; t < cNumTimers; t++) {
			switch (t) {	// Using a switch forces this map to be kept up-to-date in this idiotic language.
			case T301: mNames[T301] = "T301"; break;
			case T302: mNames[T302] = "T302"; break;
			case T303: mNames[T303] = "T303"; break;
			case T304: mNames[T304] = "T304"; break;
			case T305: mNames[T305] = "T305"; break;
			case T308: mNames[T308] = "T308"; break;
			case T313: mNames[T313] = "T313"; break;
			case T3113: mNames[T3113] = "T3113"; break;
			case TRIM: mNames[TRIM] = "TRIM"; break;
			case cNumTimers: break;
			}
		}
	}
	public:
	L3TimerTable() { initValues(); }
};
#endif

const int TimerAbortTran = -1;	// Abort the transaction;  If there are other transactions pending, they may be able to run.
const int TimerAbortChan = -2;	// Abort the entire MMChannel.

class L3Timer : GSM::Z100Timer
{
	L3TimerId mTimerId;
	// Payload.  Used as Procedure state to be invoked on timeout.  Negative value is one of these:
	int mNextState;

	public:
	void tStart(L3TimerId wTimerId, long wEndtime, int wNextState) { Z100Timer::set(wEndtime); mTimerId = wTimerId; mNextState = wNextState; }
	void tStop() { if (active()) Z100Timer::reset(); }
	bool tIsActive() const { return Z100Timer::active(); }
	bool tIsExpired() const { return Z100Timer::active() ? Z100Timer::expired() : false; }
	long tRemaining() const { return Z100Timer::remaining(); }
	int tNextState() const { return mNextState; }
	const char *tName() const;
};


class L3TimerList {
	L3Timer mtlTimers[cNumTimers];
	protected:
	virtual bool lockAndInvokeTimeout(L3Timer *timer) = 0;

	public:
	//void timerStartAbort(L3TimerId id, long wEndtime);
	void timerStart(L3TimerId id, unsigned wEndtime, int wNextState);
	void timerStop(L3TimerId id);
	void timerStopAll();
	bool timerExpired(L3TimerId id);
	// Trigger any expired timers.
	bool checkTimers();
	// Return the remaining time of any timers, or -1 if none active.
	int remainingTime();
	void text(std::ostream&os) const;
};	// class L3TimerList

};	// namespace
#endif
