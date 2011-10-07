/*
* Copyright 2008, 2011 Free Software Foundation, Inc.
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


#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>
#include <iostream>
#include <assert.h>

class Mutex;


/**@name Multithreaded access for standard streams. */
//@{

/**@name Functions for gStreamLock. */
//@{
extern Mutex gStreamLock;	///< global lock for cout and cerr
void lockCerr();		///< call prior to writing cerr
void unlockCerr();		///< call after writing cerr
void lockCout();		///< call prior to writing cout
void unlockCout();		///< call after writing cout
//@}

/**@name Macros for standard messages. */
//@{
#define COUT(text) { lockCout(); std::cout << text; unlockCout(); }
#define CERR(text) { lockCerr(); std::cerr << __FILE__ << ":" << __LINE__ << ": " << text; unlockCerr(); }
#ifdef NDEBUG
#define DCOUT(text) {}
#define OBJDCOUT(text) {}
#else
#define DCOUT(text) { COUT(__FILE__ << ":" << __LINE__ << " " << text); }
#define OBJDCOUT(text) { DCOUT(this << " " << text); } 
#endif
//@}
//@}



/**@defgroup C++ wrappers for pthread mechanisms. */
//@{

/** A class for recursive mutexes based on pthread_mutex. */
class Mutex {

	private:

	pthread_mutex_t mMutex;
	pthread_mutexattr_t mAttribs;

	public:

	Mutex();

	~Mutex();

	void lock() { pthread_mutex_lock(&mMutex); }

	void unlock() { pthread_mutex_unlock(&mMutex); }

	friend class Signal;

};


class ScopedLock {

	private:
	Mutex& mMutex;

	public:
	ScopedLock(Mutex& wMutex) :mMutex(wMutex) { mMutex.lock(); }
	~ScopedLock() { mMutex.unlock(); }

};




/** A C++ interthread signal based on pthread condition variables. */
class Signal {

	private:

	mutable pthread_cond_t mSignal;

	public:

	Signal() { int s = pthread_cond_init(&mSignal,NULL); assert(!s); }

	~Signal() { pthread_cond_destroy(&mSignal); }

	/**
		Block for the signal up to the cancellation timeout.
		Under Linux, spurious returns are possible.
	*/
	void wait(Mutex& wMutex, unsigned timeout) const;

	/**
		Block for the signal.
		Under Linux, spurious returns are possible.
	*/
	void wait(Mutex& wMutex) const
		{ pthread_cond_wait(&mSignal,&wMutex.mMutex); }

	void signal() { pthread_cond_signal(&mSignal); }

	void broadcast() { pthread_cond_broadcast(&mSignal); }

};



#define START_THREAD(thread,function,argument) \
	thread.start((void *(*)(void*))function, (void*)argument);

/** A C++ wrapper for pthread threads.  */
class Thread {

	private:

	pthread_t mThread;
	pthread_attr_t mAttrib;
	// FIXME -- Can this be reduced now?
	size_t mStackSize;
	

	public:

	/** Create a thread in a non-running state. */
	Thread(size_t wStackSize = (65536*4)):mThread((pthread_t)0) { mStackSize=wStackSize;}

	/**
		Destroy the Thread.
		It should be stopped and joined.
	*/
	~Thread() { pthread_attr_destroy(&mAttrib); }


	/** Start the thread on a task. */
	void start(void *(*task)(void*), void *arg);

	/** Join a thread that will stop on its own. */
	void join() { int s = pthread_join(mThread,NULL); assert(!s); }

};




#endif
// vim: ts=4 sw=4
