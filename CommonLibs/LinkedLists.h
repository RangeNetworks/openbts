/*
* Copyright 2008 Free Software Foundation, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
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



#ifndef LINKEDLISTS_H
#define LINKEDLISTS_H

#include <stdlib.h>
#include <assert.h>



/** This node class is used to build singly-linked lists. */
class ListNode {

	private:

	ListNode* mNext;
	void* mData;

	public:

	ListNode* next() { return mNext; }
	void next(ListNode* wNext) { mNext=wNext; }

	void* data() { return mData; }
	void data(void* wData) { mData=wData; }
};




/** A fast FIFO for pointer-based storage. */
class PointerFIFO {

	protected:

	ListNode* mHead;		///< points to next item out
	ListNode* mTail;		///< points to last item in
	ListNode* mFreeList;	///< pool of previously-allocated nodes
	unsigned mSize;			///< number of items in the FIFO

	public:

	PointerFIFO()
		:mHead(NULL),mTail(NULL),mFreeList(NULL),
		mSize(0)
	{}

	unsigned size() const { return mSize; }
	unsigned totalSize() const { return 0; }	// Not used in this version.

	/** Put an item into the FIFO at the back of the queue. */
	void put(void* val);
	/** Push an item on the front of the FIFO. */
	void push_front(void*val);			// pat added.

	/**
		Take an item from the FIFO.
		Returns NULL for empty list.
	*/
	void* get();

	/** Peek at front item without removal. */
	void *front() { return mHead ? mHead->data() : 0; }	// pat added


	private:

	/** Allocate a new node to extend the FIFO. */
	ListNode *allocate();

	/** Release a node to the free pool after removal from the FIFO. */
	void release(ListNode* wNode);

};

// This is the default type for SingleLinkList Node element;
// You can derive your class directly from this, but then you must add type casts
// all over the place.
class SingleLinkListNode
{	public:
	SingleLinkListNode *mNext;
	SingleLinkListNode *next() {return mNext;}
	void setNext(SingleLinkListNode *item) {mNext=item;}
	SingleLinkListNode() : mNext(0) {}
	virtual unsigned size() { return 0; }
};

// A single-linked lists of elements with internal pointers.
// The methods must match those from SingleLinkListNode.
// This class also assumes the Node has a size() method, and accumulates
// the total size of elements in the list in totalSize().
template<class Node=SingleLinkListNode>
class SingleLinkList
{
	Node *mHead, *mTail;
	unsigned mSize;			// Number of elements in list.
	unsigned mTotalSize;	// Total of size() method of elements in list.

	public:
	SingleLinkList() : mHead(0), mTail(0), mSize(0), mTotalSize(0) {}
	unsigned size() const { return mSize; }
	unsigned totalSize() const { return mTotalSize; }

	Node *pop_back() { assert(0); } // Not efficient with this type of list.

	Node *pop_front()
	{
		if (!mHead) return NULL;
		Node *result = mHead;
		mHead = mHead->next();
		if (mTail == result) { mTail = NULL; assert(mHead == NULL); }
		result->setNext(NULL);	// be neat
		mSize--;
		mTotalSize -= result->size();
		return result;
	}

	void push_front(Node *item)
	{
		item->setNext(mHead);
		mHead = item;
		if (!mTail) { mTail = item; }
		mSize++;
		mTotalSize += item->size();
	}

	void push_back(Node *item)
	{
		item->setNext(NULL);
		if (mTail) { mTail->setNext(item); }
		mTail = item;
		if (!mHead) mHead = item;
		mSize++;
		mTotalSize += item->size();
	}
	Node *front() { return mHead; }
	Node *back() { return mTail; }

	// Interface to InterthreadQueue so it can used SingleLinkList as the Fifo.
	void put(void *val) { push_back((Node*)val); }
	void *get() { return pop_front(); }
};





#endif
// vim: ts=4 sw=4
