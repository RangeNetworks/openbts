/*
* Copyright 2011 Range Networks, Inc.
* All Rights Reserved.
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

#ifndef RLIST_H
#define RLIST_H
#include <list>
#include "GPRSInternal.h"	// For devassert

// A list with random access too.
template<class T>
class RList : public std::list<T> {
	typedef typename std::list<T>::iterator itr_t;
	typedef typename std::list<T> type_t;
	public:
	T operator[](unsigned ind) {
		unsigned i = 0;
		for (itr_t itr = type_t::begin(); itr != type_t::end(); itr++) {
			if (i++ == ind) return *itr;
		}
		return 0;	// Since we use 0 to mean unknown, this class can not be used
					// if this is a valid value of T.  Could throw an error instead,
					// and complicate things for the caller.
	}

	bool find(T element) {
		for (itr_t itr = type_t::begin(); itr != type_t::end(); itr++) {
			if (*itr == element) {
				return true;
			}
		}
		return false;
	}

	//bool find_safely(T element) { return find(element); }
	//void push_back_safely(T&val) { assert(! find(val)); std::list<T>::push_back(val); }
	//void remove_safely(T&val) { std::list<T>::remove(val); }
};

// Like RList but add an internal Mutex so it can auto-lock for multi-threads using RlistIteratorThreadSafe.
template<class T>
class RListThreadSafe : RList<T> {
	Mutex mListLock;
	bool find_safely(T element) {
		ScopedLock lock(mListLock);
		return find(element);
	}

	void push_back_safely(T&val) {
		ScopedLock lock(mListLock);
		assert(! find(val));
		std::list<T>::push_back(val);
	}
	void remove_safely(T&val) {
		ScopedLock lock(mListLock);
		std::list<T>::remove(val);
	}
};

// Like ScopedLock but creates a scoped iterator especially useful in a for statement.
// Use like this: given a list and a mutex to protect access to the list:
// class T; list<T> mylist; Mutex mymutex; for (ScopedIterator<T,list<T> >(mylist,mymutex); next(var);)
template<class T, class ListType = RList<T> >
class ScopedIterator  {
	ListType &mPList;
	Mutex& mPMutex;
	public:
	typename ListType::iterator mNextItr, mEndp;
	void siInit() {
		mPMutex.lock();
		mNextItr = mPList.begin();
		mEndp = mPList.end();
	}
	ScopedIterator(ListType &wPList, Mutex &wPMutex) : mPList(wPList), mPMutex(wPMutex) { siInit(); }
	// Yes you can use this in a const method function and yes we are changing the Mutex and yes it is ok so use a const_cast.
	// ScopedIterator(ListType const &wPList) : mPList(const_cast<ListType&>(wPList)), mPMutex(mPList.mListLock) { siInit(); }
	~ScopedIterator() { mPMutex.unlock(); }

	// This is meant to be used as the test statement in a for or while loop.
	// We always point the iterator at the next element, so that 
	// deletion of the current element by the caller is permitted.
	// We also store the end element when the iteration starts, so new
	// elements pushed onto the back of the list during the iteration are ignored.
	bool next(T &var) {
		if (mNextItr == mEndp) return false;
		var = *mNextItr++;
		return true;
	}
};

// An iterator to be used in for loops.
template<class T>
class RListIterator
{
	typedef RList<T> ListType;
	ListType &mPList;
	public:
	typename ListType::iterator mItr, mNextItr, mEndp;
	bool mFinished;
	void siInit() {
		mNextItr = mPList.begin();
		mItr = mEndp = mPList.end();
		mFinished = (mNextItr == mEndp);
	}
	RListIterator(ListType &wPList) : mPList(wPList) { siInit(); }
	RListIterator(ListType const &wPList) : mPList(const_cast<ListType&>(wPList)) { siInit(); }
	bool next(T &var) {
		if (mFinished) { return false; }
		mItr = mNextItr;
		var = *mNextItr++;
		mFinished = (mNextItr == mEndp);	// We check now in case caller deletes end().
		return true;
	}
	// Erase the current element.
	void erase() {
		devassert(mItr != mEndp);
		mPList.erase(mItr);
		mItr = mEndp;		// To indicate we have already erased it.
	}
	bool next(T &var, typename ListType::iterator &itr) {	// Return a regular old iterator to the user.
		bool result = next(var);
		itr = mItr;
		return result;
	}
};

// A ScopedIterator wrapper specifically for RList, to be used in for loops.
// The RList has the mutex built-in.
template<class T>
class RListIteratorThreadSafe : public ScopedIterator<T>
{	public:
	typedef RList<T> ListType;
	RListIteratorThreadSafe(ListType &wPList) : ScopedIterator<T>(wPList,wPList.mListLock) {}
	// Yes you can use this in a const method and yes the Mutex is non-const but yes it is ok so use a const_cast.
	RListIteratorThreadSafe(ListType const &wPList) : ScopedIterator<T>(const_cast<ListType&>(wPList),const_cast<ListType&>(wPList).mListLock) {}
};


// Assumes the list is an RList which has an internal mutex.
#define RN_RLIST_FOR_ALL_THREAD_SAFE(type,list,var) \
	for (RListIteratorThreadSafe<type> itr(list); itr.next(var); ) 

/*
// This macro requires the caller to advance itr if the var is deleted.
//#define RN_FOR_ALL_WITH_ITR(type,list,var,itr) \
//	for (type::iterator itr = list.begin(); \
//		itr == list.end() ? 0 : ((var=*itr),1); \
//		itr++)
*/

// This macro allows deletion of the current var from the list being iterated,
// because itr is advanced to the next position at the beginning of the loop,
// and list iterators are defined as keeping their position even if elements are deleted.
#define RN_FOR_ALL(type,list,var) \
	for (type::iterator itr = (list).begin(); \
		itr == (list).end() ? 0 : ((var=*itr++),1);)


// Geez, the language sure botched this...
#define RN_FOR_ALL_CONST(type,list,var) \
	for (type::const_iterator itr = (list).begin(); \
		itr == (list).end() ? 0 : ((var=*itr++),1);)

/* not used
#define RN_FOR_ALL_REVERSED(type,list,var) \
	for (type::reverse_iterator var##_itr = (list).rbegin(); \
		var##_itr == (list).rend() ? 0 : ((var=*var##_itr),1); \
		var##_itr++)
*/


#if 0
// For your edification, these are the old functions.

// Does the list contain the element?  Return TRUE if so.
template<class T>
bool findlistrev(std::list<T> list, T element, typename std::list<T>::reverse_iterator *result = 0) {
	for (typename std::list<T>::reverse_iterator itr = list.rbegin(); itr != list.rend(); itr++) {
		if (*itr == element) {
			if (result) *result = itr;
			return true;
		}
	}
	return false;
	// This did not work?
	// return std::find(msPCHDowns.begin(),msPCHDowns.end(),(const PDCHL1Downlink*)down) != msPCHDowns.end();
}

template<class T>
bool findlist(std::list<T> list, T element, typename std::list<T>::iterator *result = 0) {
	for (typename std::list<T>::iterator itr = list.begin(); itr != list.end(); itr++) {
		if (*itr == element) {
			if (result) *result = itr;
			return true;
		}
	}
	return false;
	// This did not work?
	// return std::find(msPCHDowns.begin(),msPCHDowns.end(),(const PDCHL1Downlink*)down) != msPCHDowns.end();
}
#endif
#endif
