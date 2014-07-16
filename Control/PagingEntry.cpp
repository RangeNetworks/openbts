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

#define LOG_GROUP LogGroup::Control
#include "PagingEntry.h"
#include "L3MMLayer.h"
#include <GSML3RRMessages.h>

namespace Control {

NewPagingEntry::~NewPagingEntry()
{
	if (mImmAssign) { delete mImmAssign; }
}

GSM::ChannelType NewPagingEntry::getGsmChanType() const
{
	return mInitialChanType;
}

// This does a lot of mallocing.
L3MobileIdentity NewPagingEntry::getMobileId()
{
	if (! mCheckedForTmsi) {
		mCheckedForTmsi = true;
		if (! mTmsi.valid()) {
			uint32_t tmsi = gTMSITable.tmsiTabGetTMSI(mImsi,true);
			LOG(DEBUG)<<"tmsiTabGetTMSI imsi="<<mImsi<< "returns"<<LOGVAR(tmsi);
			if (tmsi) { mTmsi = tmsi; }
		}
	}

	if (mTmsi.valid()) {
		// page by tmsi
		return L3MobileIdentity(mTmsi.value());
	} else {
		// page by imsi
		return L3MobileIdentity(mImsi.c_str());
	}
}

unsigned NewPagingEntry::getImsiMod1000() const
{
	int len = mImsi.length();
	if (len < 3) return 0;	// This is bad.
	return atoi(&mImsi.c_str()[len-3]);
}

void MMGetPages(NewPagingList_t &pages, bool wait)
{
	assert(wait == false);
	gMMLayer.mmGetPages(pages);		// Blocks until there are some.

	for (NewPagingList_t::iterator it = pages.begin(); it != pages.end(); it++) {
		WATCH("page "<<*it);
	}
}

string NewPagingEntry::text() const
{
	ostringstream ss;
	//const char *requestType=mNpeType==npeCC ? "CC" : mNpeType==npeSMS ? "SMS" : mNpeType==npeGPRS ? "GPRS" : "undefined";
	//ss << "NewPagingEntry(" <<LOGVAR(requestType) <<LOGVAR(mImsi) <<LOGVAR(mTmsi) <<LOGHEX(mGprsClient) <<LOGVAR(mImmAssign) <<LOGVAR(mDrxBegin) <<")";
	ss << "NewPagingEntry(" <<LOGVAR(mInitialChanType) <<LOGVAR(mImsi) <<LOGVAR(mTmsi) <<LOGHEX(mGprsClient) <<LOGVAR(mImmAssign) <<LOGVAR(mDrxBegin) <<")";
	return ss.str();
}

std::ostream& operator<<(std::ostream& os, const NewPagingEntry&npe) { os <<npe.text(); return os; }
std::ostream& operator<<(std::ostream& os, const NewPagingEntry*npe) { os <<npe->text(); return os; }

};
