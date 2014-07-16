/*
* Copyright 2008, 2014 Free Software Foundation, Inc.
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/




#ifndef SIP_UTILITY_H
#define SIP_UTILITY_H
#include "netdb.h"
#include <ScalarTypes.h>
#include <Timeval.h>
#include <assert.h>
#include <string>
#include <map>
#include <list>
#include <Threads.h>
#include <Interthread.h>
#include <Utils.h>



template <class ValueType>
class ThreadSafeList {
	typedef std::list<ValueType> BaseType;
	BaseType mList;
	Mutex mMapLock;
	public:
	Mutex &getLock() { return mMapLock; }

	public:
#if USE_SCOPED_ITERATORS
	typedef ScopedIteratorTemplate<BaseType,ThreadSafeList,ValueType> ScopedIterator;
#endif
	// The caller is supposed to lock the mutex via getLock before the start of any use of begin or end.
	typename BaseType::iterator begin() { devassert(mMapLock.lockcnt()); return mList.begin(); }
	typename BaseType::iterator end() { devassert(mMapLock.lockcnt()); return mList.end(); }
	typedef typename BaseType::iterator iterator;

	int size() {
		ScopedLock(this->mMapLock);
		return mList.size();
	}
	void push_back(ValueType val) {
		ScopedLock(this->mMapLock);
		mList.push_back(val);
	}
	void push_front(ValueType val) {
		ScopedLock(this->mMapLock);
		mList.push_front(val);
	}
	ValueType pop_frontr(ValueType val) { // Like pop_front, but return the value.
		ScopedLock(this->mMapLock);
		ValueType result = mList.front();
		mList.pop_front(val);
		return result;
	}
	ValueType pop_backr(ValueType val) {	// Like pop_back, but return the value.
		ScopedLock(this->mMapLock);
		ValueType result = mList.back();
		mList.pop_back(val);
		return result;
	}
	typename BaseType::iterator erase(typename BaseType::iterator position) {
		ScopedLock(this->mMapLock);
		return this->mList.erase(position);
	}
};



namespace SIP {
using namespace std;

// These could be global.
extern string localIP(); // Replaces mSIPIP.
extern string localIPAndPort(); // Replaces mSIPIP and mSIPPort.

struct IPAddressSpec {
	enum ConnectionType { None, TCP, UDP };
	ConnectionType mipType;
	string mipName;		// Original address, which may be an IP address in dotted notation or possibly a domain name, and optional port.
	string mipIP;		// The IP address of mipName as a string.  If mipName is already an IP address, it is the same.
	unsigned mipPort;
	Bool_z mipValid;		// Is the address valid, ie, was resolution successful?
	struct ::sockaddr_in mipSockAddr;	///< the ready-to-use UDP address
	bool ipSet(string addrSpec, const char *provenance);
	string ipToText() const;
	string ipAddrStr() const { return format("%s:%d",mipIP,mipPort); }
	string ipTransportName() const { return "UDP"; }
	bool ipIsReliableTransport() const { return false; }	// Right now we only support UDP
	IPAddressSpec() : mipType(None), mipPort(0) {}
};

// Ticket #1158
class ResponseTable {

	typedef std::map<unsigned, string> ResponseMap;
	ResponseMap mMap;
	void addResponse(unsigned,const char *);

	public:

	//void operator[](unsigned code, const char* name) { mMap[code]=name; }
	string operator[](unsigned code) const;

	/** Get the string. Log ERR and return "undefined" if the code is not in the table. */
	//string response(unsigned code) const;
	static string get(unsigned code);
	ResponseTable();
};
extern ResponseTable gResponseTable;

// Update: 3GPP 24.229 table 7.8 provides new values for SIP Timers recommended for UE transactions,
// but I think they only apply to SIP transactions bound for a UE using IMS.
struct SipTimers
{
	static const int T1 = 500;		// 500 ms.  17.1.2.2
	static const int T2 = 4*1000;	// 4 seconds  17.1.2.2
	static const int T4 = 5*1000;	// 5 seconds 17.1.2.2
};

class SipTimer
{
	bool mActive;			///< true if timer is active
	Timeval mEndTime;		///< the time at which this timer will expire
	unsigned long mLimitTime;		///< timeout in milliseconds
	//int mNextState;		// Payload.  Used as Procedure state to be invoked on timeout.  -1 means abort the procedure.

	public:
	SipTimer() : mActive(false), mLimitTime(0) {}
	bool isActive() { return mActive; }

	bool expired() const {
		if (!mActive) { return false; } // A non-active timer does not expire.
		return mEndTime.passed();
	}

	void setTime(long wTime) { mLimitTime = wTime; }

	void set() {
		assert(mLimitTime!=0);
		mEndTime = Timeval(mLimitTime);
		mActive=true;
	} 
	void set(long wLimitTime) {
		mLimitTime = wLimitTime;
		set();
	} 
	// Set the timer but only if it is not already running.
	void setOnce(long wLimitTime) {
		if (!mActive) set(wLimitTime);
	}
	// Double the timer value but no more than maxTime.  Lots of UDP connection timers in SIP use this.
	void setDouble(unsigned maxTime=10000) {
		assert(mLimitTime!=0);
		unsigned long newTime = mLimitTime * 2;
		if (newTime > maxTime) { newTime = maxTime; }
		set(mLimitTime * 2);
	}
	void stop() { mActive = false; }


	long remaining() const {
		if (!mActive) return 0;
		long rem = mEndTime.remaining();
		if (rem<0) rem=0;
		return rem;
	}

	string text() const {
		return mActive ?
			format("(active=%d remaining=%ld limit=%ld",mActive,mEndTime.remaining(),mLimitTime) :
			string("(active=0)");
	}

};
std::ostream& operator<<(std::ostream& os, const SipTimer&);


extern string make_tag();
extern string make_branch(const char *name=NULL);
extern string globallyUniqueId(const char *start);
extern string dequote(const string);

extern string makeMD5(string input);
extern string makeResponse(string username, string realm, string password, string method, string uri, string nonce);
extern const char* sipSkipPrefix1(const char* in);
extern string sipSkipPrefix(string in);


};
#endif
// vim: ts=4 sw=4
