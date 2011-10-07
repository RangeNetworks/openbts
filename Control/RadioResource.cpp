/**@file GSM Radio Resource procedures, GSM 04.18 and GSM 04.08. */

/*
* Copyright 2008, 2009, 2010, 2011 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011 Range Networks, Inc.
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




#include <stdio.h>
#include <stdlib.h>
#include <list>

#include "ControlCommon.h"
#include "TransactionTable.h"
#include "RadioResource.h"
#include "SMSControl.h"
#include "CallControl.h"

#include <GSMLogicalChannel.h>
#include <GSMConfig.h>

#include <Logger.h>
#undef WARNING





using namespace std;
using namespace GSM;
using namespace Control;





/**
	Determine the channel type needed.
	This is based on GSM 04.08 9.1.8, Table 9.3 and 9.3a.
	The following is assumed about the global BTS capabilities:
	- We do not support call reestablishment.
	- We do not support GPRS.
	@param RA The request reference from the channel request message.
	@return channel type code, undefined if not a supported service
*/
ChannelType decodeChannelNeeded(unsigned RA)
{
	// This code is based on GSM 04.08 Table 9.9.

	unsigned RA4 = RA>>4;
	unsigned RA5 = RA>>5;

	// Answer to paging, Table 9.9a.
	// We don't support TCH/H, so it's wither SDCCH or TCH/F.
	// The spec allows for "SDCCH-only" MS.  We won't support that here.
	// FIXME -- So we probably should not use "any channel" in the paging indications.
	if (RA5 == 0x04) return TCHFType;		// any channel or any TCH.
	if (RA4 == 0x01) return SDCCHType;		// SDCCH
	if (RA4 == 0x02) return TCHFType;		// TCH/F
	if (RA4 == 0x03) return TCHFType;		// TCH/F

	int NECI = gConfig.getNum("GSM.CellSelection.NECI");
	if (NECI==0) {
		if (RA5 == 0x07) return SDCCHType;		// MOC or SDCCH procedures
		if (RA5 == 0x00) return SDCCHType;		// location updating
	} else {
		assert(NECI==1);
		if (gConfig.defines("Control.VEA")) {
			// Very Early Assignment
			if (RA5 == 0x07) return TCHFType;		// MOC for TCH/F
			if (RA4 == 0x04) return TCHFType;		// MOC, TCH/H sufficient
		} else {
			// Early Assignment
			if (RA5 == 0x07) return SDCCHType;		// MOC for TCH/F
			if (RA4 == 0x04) return SDCCHType;		// MOC, TCH/H sufficient
		}
		if (RA4 == 0x00) return SDCCHType;		// location updating
		if (RA4 == 0x01) return SDCCHType;		// other procedures on SDCCH
	}

	// Anything else falls through to here.
	// We are still ignoring data calls, GPRS, LMU.
	return UndefinedCHType;
}


/** Return true if RA indicates LUR. */
bool requestingLUR(unsigned RA)
{
	int NECI = gConfig.getNum("GSM.CellSelection.NECI");
	if (NECI==0) return ((RA>>5) == 0x00);
	 else return ((RA>>4) == 0x00);
}





/** Decode RACH bits and send an immediate assignment; may block waiting for a channel. */
void AccessGrantResponder(
		unsigned RA, const GSM::Time& when,
		float RSSI, float timingError)
{
	// RR Establishment.
	// Immediate Assignment procedure, "Answer from the Network"
	// GSM 04.08 3.3.1.1.3.
	// Given a request reference, try to allocate a channel
	// and send the assignment to the handset on the CCCH.
	// This GSM's version of medium access control.
	// Papa Legba, open that door...

	// Are we holding off new allocations?
	if (gBTS.hold()) {
		LOG(NOTICE) << "ignoring RACH due to BTS hold-off";
		return;
	}

	// Check "when" against current clock to see if we're too late.
	// Calculate maximum number of frames of delay.
	// See GSM 04.08 3.3.1.1.2 for the logic here.
	static const unsigned txInteger = gConfig.getNum("GSM.RACH.TxInteger");
	static const int maxAge = GSM::RACHSpreadSlots[txInteger] + GSM::RACHWaitSParam[txInteger];
	// Check burst age.
	int age = gBTS.time() - when;
	LOG(INFO) << "RA=0x" << hex << RA << dec
		<< " when=" << when << " age=" << age
		<< " delay=" << timingError << " RSSI=" << RSSI;
	if (age>maxAge) {
		LOG(WARNING) << "ignoring RACH bust with age " << age;
		gBTS.growT3122()/1000;
		return;
	}

	// Screen for delay.
	if (timingError>gConfig.getNum("GSM.MS.TA.Max")) {
		LOG(WARNING) << "ignoring RACH burst with delay " << timingError;
		return;
	}

	// Get an AGCH to send on.
	CCCHLogicalChannel *AGCH = gBTS.getAGCH();
	// Someone had better have created a least one AGCH.
	assert(AGCH);
	// Check AGCH load now.
	if (AGCH->load()>gConfig.getNum("GSM.CCCH.AGCH.QMax")) {
		LOG(WARNING) "AGCH congestion";
		return;
	}

	// Check for location update.
	// This gives LUR a lower priority than other services.
	if (requestingLUR(RA)) {
		if (gBTS.SDCCHAvailable()<=gConfig.getNum("GSM.CCCH.PCH.Reserve")) {
			unsigned waitTime = gBTS.growT3122()/1000;
			LOG(WARNING) << "LUR congestion, RA=" << RA << " T3122=" << waitTime;
			const L3ImmediateAssignmentReject reject(L3RequestReference(RA,when),waitTime);
			LOG(DEBUG) << "LUR rejection, sending " << reject;
			AGCH->send(reject);
			return;
		}
	}

	// Allocate the channel according to the needed type indicated by RA.
	// The returned channel is already open and ready for the transaction.
	LogicalChannel *LCH = NULL;
	switch (decodeChannelNeeded(RA)) {
		case TCHFType: LCH = gBTS.getTCH(); break;
		case SDCCHType: LCH = gBTS.getSDCCH(); break;
		// If we don't support the service, assign to an SDCCH and we can reject it in L3.
		case UndefinedCHType:
			LOG(NOTICE) << "RACH burst for unsupported service RA=" << RA;
			LCH = gBTS.getSDCCH();
			break;
		// We should never be here.
		default: assert(0);
	}

	// Nothing available?
	if (!LCH) {
		// Rejection, GSM 04.08 3.3.1.1.3.2.
		// But since we recognize SOS calls already,
		// we might as well save some AGCH bandwidth.
		unsigned waitTime = gBTS.growT3122()/1000;
		LOG(WARNING) << "congestion, RA=" << RA << " T3122=" << waitTime;
		const L3ImmediateAssignmentReject reject(L3RequestReference(RA,when),waitTime);
		LOG(DEBUG) << "rejection, sending " << reject;
		AGCH->send(reject);
		return;
	}

	// Set the channel physical parameters from the RACH burst.
	LCH->setPhy(RSSI,timingError);

	// Assignment, GSM 04.08 3.3.1.1.3.1.
	// Create the ImmediateAssignment message.
	// Woot!! We got a channel! Thanks to Legba!
	int initialTA = (int)(timingError + 0.5F);
	if (initialTA<0) initialTA=0;
	if (initialTA>62) initialTA=62;
	const L3ImmediateAssignment assign(
		L3RequestReference(RA,when),
		LCH->channelDescription(),
		L3TimingAdvance(initialTA)
	);
	LOG(INFO) << "sending " << assign;
	AGCH->send(assign);

	// On successful allocation, shrink T3122.
	gBTS.shrinkT3122();
}



void* Control::AccessGrantServiceLoop(void*)
{
	while (true) {
		ChannelRequestRecord *req = gBTS.nextChannelRequest();
		if (!req) continue;
		AccessGrantResponder(
			req->RA(), req->frame(),
			req->RSSI(), req->timingError()
		);
		delete req;
	}
	return NULL;
}





void Control::PagingResponseHandler(const L3PagingResponse* resp, LogicalChannel* DCCH)
{
	assert(resp);
	assert(DCCH);
	LOG(INFO) << *resp;

	// If we got a TMSI, find the IMSI.
	L3MobileIdentity mobileID = resp->mobileID();
	if (mobileID.type()==TMSIType) {
		char *IMSI = gTMSITable.IMSI(mobileID.TMSI());
		if (IMSI) {
			mobileID = L3MobileIdentity(IMSI);
			free(IMSI);
		} else {
			// Don't try too hard to resolve.
			// The handset is supposed to respond with the same ID type as in the request.
			// This could be the sign of some kind of DOS attack.
			LOG(CRIT) << "Paging Reponse with non-valid TMSI";
			// Cause 0x60 "Invalid mandatory information"
			DCCH->send(L3ChannelRelease(0x60));
			return;
		}
	}

	// Delete the Mobile ID from the paging list to free up CCCH bandwidth.
	// ... if it was not deleted by a timer already ...
	gBTS.pager().removeID(mobileID);

	// Find the transction table entry that was created when the phone was paged.
	// We have to look up by mobile ID since the paging entry may have been
	// erased before this handler was called.  That's too bad.
	// HACK -- We also flush stray transactions until we find what we 
	// are looking for.
	TransactionEntry* transaction = gTransactionTable.answeredPaging(mobileID);
	if (!transaction) {
		LOG(WARNING) << "Paging Reponse with no transaction record for " << mobileID;
		// Cause 0x41 means "call already cleared".
		DCCH->send(L3ChannelRelease(0x41));
		return;
	}
	// Set the transaction channel.
	transaction->channel(DCCH);
	// We are looking for a mobile-terminated transaction.
	// The transaction controller will take it from here.
	switch (transaction->service().type()) {
		case L3CMServiceType::MobileTerminatedCall:
			MTCStarter(transaction, DCCH);
			return;
		case L3CMServiceType::MobileTerminatedShortMessage:
			MTSMSController(transaction, DCCH);
			return;
		default:
			// Flush stray MOC entries.
			// There should not be any, but...
			LOG(ERR) << "non-valid paging-state transaction: " << *transaction;
			gTransactionTable.remove(transaction);
			// FIXME -- Send a channel release on the DCCH.
	}
}



void Control::AssignmentCompleteHandler(const L3AssignmentComplete *confirm, TCHFACCHLogicalChannel *TCH)
{
	// The assignment complete handler is used to
	// tie together split transactions across a TCH assignment
	// in non-VEA call setup.

	assert(TCH);
	assert(confirm);
	LOG(DEBUG) << *confirm;

	// Check the transaction table to know what to do next.
	TransactionEntry* transaction = gTransactionTable.find(TCH);
	if (!transaction) {
		LOG(WARNING) << "No transaction matching channel " << *TCH << " (" << TCH << ").";
		throw UnexpectedMessage();
	}
	LOG(INFO) << "service="<<transaction->service().type();

	// These "controller" functions don't return until the call is cleared.
	switch (transaction->service().type()) {
		case L3CMServiceType::MobileOriginatedCall:
			MOCController(transaction,TCH);
			break;
		case L3CMServiceType::MobileTerminatedCall:
			MTCController(transaction,TCH);
			break;
		default:
			LOG(WARNING) << "unsupported service " << transaction->service();
			throw UnsupportedMessage(transaction->ID());
	}
	// If we got here, the call is cleared.
}








void Pager::addID(const L3MobileIdentity& newID, ChannelType chanType,
		TransactionEntry& transaction, unsigned wLife)
{
	transaction.GSMState(GSM::Paging);
	transaction.setTimer("3113",wLife);
	// Add a mobile ID to the paging list for a given lifetime.
	ScopedLock lock(mLock);
	// If this ID is already in the list, just reset its timer.
	// Uhg, another linear time search.
	// This would be faster if the paging list were ordered by ID.
	// But the list should usually be short, so it may not be worth the effort.
	for (PagingEntryList::iterator lp = mPageIDs.begin(); lp != mPageIDs.end(); ++lp) {
		if (lp->ID()==newID) {
			LOG(DEBUG) << newID << " already in table";
			lp->renew(wLife);
			mPageSignal.signal();
			return;
		}
	}
	// If this ID is new, put it in the list.
	mPageIDs.push_back(PagingEntry(newID,chanType,transaction.ID(),wLife));
	LOG(INFO) << newID << " added to table";
	mPageSignal.signal();
}


unsigned Pager::removeID(const L3MobileIdentity& delID)
{
	// Return the associated transaction ID, or 0 if none found.
	LOG(INFO) << delID;
	ScopedLock lock(mLock);
	for (PagingEntryList::iterator lp = mPageIDs.begin(); lp != mPageIDs.end(); ++lp) {
		if (lp->ID()==delID) {
			unsigned retVal = lp->transactionID();
			mPageIDs.erase(lp);
			return retVal;
		}
	}
	return 0;
}



unsigned Pager::pageAll()
{
	// Traverse the full list and page all IDs.
	// Remove expired IDs.
	// Return the number of IDs paged.
	// This is a linear time operation.

	ScopedLock lock(mLock);

	// Clear expired entries.
	PagingEntryList::iterator lp = mPageIDs.begin();
	while (lp != mPageIDs.end()) {
		if (!lp->expired()) ++lp;
		else {
			LOG(INFO) << "erasing " << lp->ID();
			// Non-responsive, dead transaction?
			gTransactionTable.removePaging(lp->transactionID());
			// remove from the list
			lp=mPageIDs.erase(lp);
		}
	}

	LOG(INFO) << "paging " << mPageIDs.size() << " mobile(s)";

	// Page remaining entries, two at a time if possible.
	// These PCH send operations are non-blocking.
	lp = mPageIDs.begin();
	while (lp != mPageIDs.end()) {
		// FIXME -- This completely ignores the paging groups.
		// HACK -- So we send every page twice.
		// That will probably mean a different Pager for each subchannel.
		// See GSM 04.08 10.5.2.11 and GSM 05.02 6.5.2.
		const L3MobileIdentity& id1 = lp->ID();
		ChannelType type1 = lp->type();
		++lp;
		if (lp==mPageIDs.end()) {
			// Just one ID left?
			LOG(DEBUG) << "paging " << id1;
			gBTS.getPCH(0)->send(L3PagingRequestType1(id1,type1));
			gBTS.getPCH(0)->send(L3PagingRequestType1(id1,type1));
			break;
		}
		// Page by pairs when possible.
		const L3MobileIdentity& id2 = lp->ID();
		ChannelType type2 = lp->type();
		++lp;
		LOG(DEBUG) << "paging " << id1 << " and " << id2;
		gBTS.getPCH(0)->send(L3PagingRequestType1(id1,type1,id2,type2));
		gBTS.getPCH(0)->send(L3PagingRequestType1(id1,type1,id2,type2));
	}

	return mPageIDs.size();
}

size_t Pager::pagingEntryListSize()
{
	ScopedLock lock(mLock);
	return mPageIDs.size();
}

void Pager::start()
{
	if (mRunning) return;
	mRunning=true;
	mPagingThread.start((void* (*)(void*))PagerServiceLoopAdapter, (void*)this);
}



void* Control::PagerServiceLoopAdapter(Pager *pager)
{
	pager->serviceLoop();
	return NULL;
}

void Pager::serviceLoop()
{
	while (mRunning) {

		LOG(DEBUG) << "Pager blocking for signal";
		mLock.lock();
		while (mPageIDs.size()==0) mPageSignal.wait(mLock);
		mLock.unlock();

		// page everything
		pageAll();

		// Wait for pending activity to clear the channel.
		// This wait is what causes PCH to have lower priority than AGCH.
		unsigned load = gBTS.getPCH()->load();
		LOG(DEBUG) << "Pager waiting for " << load << " multiframes";
		if (load) sleepFrames(51*load);
	}
}



void Pager::dump(ostream& os) const
{
	ScopedLock lock(mLock);
	PagingEntryList::const_iterator lp = mPageIDs.begin();
	while (lp != mPageIDs.end()) {
		os << lp->ID() << " " << lp->type() << " " << lp->expired() << endl;
		++lp;
	}
}



// vim: ts=4 sw=4
