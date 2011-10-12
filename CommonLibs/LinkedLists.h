/*
* Copyright 2008 Free Software Foundation, Inc.
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

	private:

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

	/** Put an item into the FIFO. */
	void put(void* val);

	/**
		Take an item from the FIFO.
		Returns NULL for empty list.
	*/
	void* get();


	private:

	/** Allocate a new node to extend the FIFO. */
	ListNode *allocate();

	/** Release a node to the free pool after removal from the FIFO. */
	void release(ListNode* wNode);

};





#endif
// vim: ts=4 sw=4
