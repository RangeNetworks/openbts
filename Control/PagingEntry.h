/**@file Declarations for common-use control-layer functions. */
/*
* Copyright 2014 Range Networks, Inc.
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

#ifndef _PAGING_H_
#define _PAGING_H_

#include <GSMCommon.h>
#include "ControlTransfer.h"
namespace GSM { class L3Frame; class L2LogicalChannel; class L3ImmediateAssignment; class L3MobileIdentity; }

namespace Control {


/** An entry in the paging list. */
struct NewPagingEntry {
	GSM::ChannelType mInitialChanType;
	// Each page has to be sent twice to make sure the handset hears it.
	Int_z mSendCount;	// Number of times this page has been sent.

	// These fields are needed for all pages:
	std::string mImsi;		// Always provided.

	// For GSM pages we need the following fields:
	TMSI_t mTmsi;			// A place for the pager to cache the tmsi if it finds one.
	Bool_z mCheckedForTmsi;	// A flag used by the pager to see if the imsi has been checked for tmsi.

	// For GPRS pages we need the following fields:
	GPRS::TBF *mGprsClient;	// If non-NULL this is a GPRS client that needs service.
	GSM::L3ImmediateAssignment *mImmAssign;
	unsigned mDrxBegin;	// GSM Frame number when DRX mode begins.

	// Such a clever language.
	NewPagingEntry(GSM::ChannelType wChanType, std::string &wImsi) :
		/*mNpeType(wType),*/
		mInitialChanType(wChanType),
		mImsi(wImsi),
		mGprsClient(NULL), mImmAssign(NULL), mDrxBegin(0)
		{}
	NewPagingEntry(const NewPagingEntry &other) : /*mNpeType(other.mNpeType),*/
		mInitialChanType(other.mInitialChanType),
		mImsi(other.mImsi),
		mTmsi(other.mTmsi), mCheckedForTmsi(other.mCheckedForTmsi),
		mGprsClient(NULL), mImmAssign(NULL), mDrxBegin(0)
		{
			//mTmsi = other.mTmsi;
			//mCheckedForTmsi = other.mCheckedForTmsi
			assert(other.mGprsClient == NULL);
			assert(other.mImmAssign == NULL);	// Since it is deleted when NewPagingEntry is deleted, it would be double deleted.
		}
		// { *this = other; }
	~NewPagingEntry();

	// Return the GSM channel type, assuming it is not a GPRS paging type.
	GSM::ChannelType getGsmChanType() const;
	GSM::L3MobileIdentity getMobileId();
	unsigned getImsiMod1000() const;
	string text() const;
};
std::ostream& operator<<(std::ostream& os, const NewPagingEntry&npe);
std::ostream& operator<<(std::ostream& os, const NewPagingEntry*npe);

typedef std::vector<NewPagingEntry> NewPagingList_t;
void MMGetPages(NewPagingList_t &pages, bool wait);

};

#endif
