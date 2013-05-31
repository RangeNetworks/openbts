/*
* Copyright 2008 Free Software Foundation, Inc.
*
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




#include "LinkedLists.h"


void PointerFIFO::push_front(void* val)	// by pat
{
	// Pat added this routine for completeness, but never used or tested.
	// The first person to use this routine should remove this assert.
	ListNode *node = allocate();
	node->data(val);
	node->next(mHead);
	mHead = node;
	if (!mTail) mTail=node;
	mSize++;
}

void PointerFIFO::put(void* val)
{
	ListNode *node = allocate();
	node->data(val);
	node->next(NULL);
	if (mTail!=NULL) mTail->next(node);
	mTail=node;
	if (mHead==NULL) mHead=node;
	mSize++;
}

/** Take an item from the FIFO. */
void* PointerFIFO::get()
{
	// empty list?
	if (mHead==NULL) return NULL;
	// normal case
	ListNode* next = mHead->next();
	void* retVal = mHead->data();
	release(mHead);
	mHead = next;
	if (next==NULL) mTail=NULL;
	mSize--;
	return retVal;
}


ListNode *PointerFIFO::allocate()
{
	if (mFreeList==NULL) return new ListNode;
	ListNode* retVal = mFreeList;
	mFreeList = mFreeList->next();
	return retVal;
}

void PointerFIFO::release(ListNode* wNode)
{
	wNode->next(mFreeList);
	mFreeList = wNode;
}
