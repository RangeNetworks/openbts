/**@file TransactionTable and related classes. */

/*
* Copyright 2008, 2010, 2011 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Process, Inc.
* Copyright 2011 Raqnge Networks, Inc.
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


#include "TransactionTable.h"
#include "ControlCommon.h"

#include <GSMLogicalChannel.h>
#include <GSML3Message.h>
#include <GSML3CCMessages.h>
#include <GSML3RRMessages.h>
#include <GSML3MMMessages.h>
#include <GSMConfig.h>

#include <sqlite3.h>
#include <sqlite3util.h>

#include <SIPEngine.h>
#include <SIPInterface.h>

#include <CallControl.h>

#include <Logger.h>
#undef WARNING


using namespace std;
using namespace GSM;
using namespace Control;
using namespace SIP;






void TransactionEntry::initTimers()
{
	// Call this only once.
	// TODO -- It would be nice if these were all configurable.
	assert(mTimers.size()==0);
	mTimers["301"] = Z100Timer(T301ms);
	mTimers["302"] = Z100Timer(T302ms);
	mTimers["303"] = Z100Timer(T303ms);
	mTimers["304"] = Z100Timer(T304ms);
	mTimers["305"] = Z100Timer(T305ms);
	mTimers["308"] = Z100Timer(T308ms);
	mTimers["310"] = Z100Timer(T310ms);
	mTimers["313"] = Z100Timer(T313ms);
	mTimers["3113"] = Z100Timer(gConfig.getNum("GSM.Timer.T3113"));
	mTimers["TR1M"] = Z100Timer(TR1Mms);
}





// Form for MT transactions.
TransactionEntry::TransactionEntry(
	const char* proxy,
	const L3MobileIdentity& wSubscriber, 
	GSM::LogicalChannel* wChannel,
	const L3CMServiceType& wService,
	const L3CallingPartyBCDNumber& wCalling,
	GSM::CallState wState,
	const char *wMessage)
	:mID(gTransactionTable.newID()),
	mSubscriber(wSubscriber),mService(wService),
	mL3TI(gTMSITable.nextL3TI(wSubscriber.digits())),
	mCalling(wCalling),
	mSIP(proxy,mSubscriber.digits()),
	mGSMState(wState),
	mNumSQLTries(gConfig.getNum("Control.NumSQLTries")),
	mChannel(wChannel),
	mTerminationRequested(false)
{
	if (wMessage) mMessage.assign(wMessage); //strncpy(mMessage,wMessage,160);
	else mMessage.assign(""); //mMessage[0]='\0';
	initTimers();
}

// Form for MOC transactions.
TransactionEntry::TransactionEntry(
	const char* proxy,
	const L3MobileIdentity& wSubscriber,
	GSM::LogicalChannel* wChannel,
	const L3CMServiceType& wService,
	unsigned wL3TI,
	const L3CalledPartyBCDNumber& wCalled)
	:mID(gTransactionTable.newID()),
	mSubscriber(wSubscriber),mService(wService),
	mL3TI(wL3TI),
	mCalled(wCalled),
	mSIP(proxy,mSubscriber.digits()),
	mGSMState(GSM::MOCInitiated),
	mNumSQLTries(gConfig.getNum("Control.NumSQLTries")),
	mChannel(wChannel),
	mTerminationRequested(false)
{
	assert(mSubscriber.type()==GSM::IMSIType);
	mMessage.assign(""); //mMessage[0]='\0';
	initTimers();
}


// Form for SOS transactions.
TransactionEntry::TransactionEntry(
	const char* proxy,
	const L3MobileIdentity& wSubscriber,
	GSM::LogicalChannel* wChannel,
	const L3CMServiceType& wService,
	unsigned wL3TI)
	:mID(gTransactionTable.newID()),
	mSubscriber(wSubscriber),mService(wService),
	mL3TI(wL3TI),
	mSIP(proxy,mSubscriber.digits()),
	mGSMState(GSM::MOCInitiated),
	mNumSQLTries(2*gConfig.getNum("Control.NumSQLTries")),
	mChannel(wChannel),
	mTerminationRequested(false)
{
	mMessage.assign(""); //mMessage[0]='\0';
	initTimers();
}


// Form for MO-SMS transactions.
TransactionEntry::TransactionEntry(
	const char* proxy,
	const L3MobileIdentity& wSubscriber,
	GSM::LogicalChannel* wChannel,
	const L3CalledPartyBCDNumber& wCalled,
	const char* wMessage)
	:mID(gTransactionTable.newID()),
	mSubscriber(wSubscriber),
	mService(GSM::L3CMServiceType::ShortMessage),
	mL3TI(7),mCalled(wCalled),
	mSIP(proxy,mSubscriber.digits()),
	mGSMState(GSM::SMSSubmitting),
	mNumSQLTries(gConfig.getNum("Control.NumSQLTries")),
	mChannel(wChannel),
	mTerminationRequested(false)
{
	assert(mSubscriber.type()==GSM::IMSIType);
	if (wMessage!=NULL) mMessage.assign(wMessage); //strncpy(mMessage,wMessage,160);
	else mMessage.assign(""); //mMessage[0]='\0';
	initTimers();
}

// Form for MO-SMS transactions with parallel call.
TransactionEntry::TransactionEntry(
	const char* proxy,
	const L3MobileIdentity& wSubscriber,
	GSM::LogicalChannel* wChannel)
	:mID(gTransactionTable.newID()),
	mSubscriber(wSubscriber),
	mService(GSM::L3CMServiceType::ShortMessage),
	mL3TI(7),
	mSIP(proxy,mSubscriber.digits()),
	mGSMState(GSM::SMSSubmitting),
	mNumSQLTries(gConfig.getNum("Control.NumSQLTries")),
	mChannel(wChannel),
	mTerminationRequested(false)
{
	assert(mSubscriber.type()==GSM::IMSIType);
	mMessage[0]='\0';
	initTimers();
}



TransactionEntry::~TransactionEntry()
{
}


bool TransactionEntry::timerExpired(const char* name) const
{
	TimerTable::const_iterator itr = mTimers.find(name);
	assert(itr!=mTimers.end());
	ScopedLock lock(mLock);
	return (itr->second).expired();
}


bool TransactionEntry::anyTimerExpired() const
{
	ScopedLock lock(mLock);
	TimerTable::const_iterator itr = mTimers.begin();
	while (itr!=mTimers.end()) {
		if ((itr->second).expired()) {
			LOG(INFO) << itr->first << " expired in " << *this;
			return true;
		}
		++itr;
	}
	return false;
}


void TransactionEntry::resetTimers()
{
	ScopedLock lock(mLock);
	TimerTable::iterator itr = mTimers.begin();
	while (itr!=mTimers.end()) {
		(itr->second).reset();
		++itr;
	}
}



bool TransactionEntry::dead() const
{
	ScopedLock lock(mLock);

	// Null state?
	if (mGSMState==GSM::NullState && stateAge()>180*1000) return true;
	// Stuck in proceeding?
	if (mSIP.state()==Proceeding && stateAge()>180*1000) return true;
	
	// Paging timed out?
	if (mGSMState==GSM::Paging) {
		TimerTable::const_iterator itr = mTimers.find("3113");
		assert(itr!=mTimers.end());
		return (itr->second).expired();
	}

	return false;
}



ostream& Control::operator<<(ostream& os, const TransactionEntry& entry)
{
	entry.text(os);
	return os;
}



void TransactionEntry::text(ostream& os) const
{
	ScopedLock lock(mLock);
	os << mID;
	if (mChannel) os << " " << *mChannel;
	else os << " no chan";
	os << " " << mSubscriber;
	os << " L3TI=" << mL3TI;
	os << " SIP-call-id=" << mSIP.callID();
	os << " SIP-proxy=" << mSIP.proxyIP() << ":" << mSIP.proxyPort();
	os << " " << mService;
	if (mCalled.digits()[0]) os << " to=" << mCalled.digits();
	if (mCalling.digits()[0]) os << " from=" << mCalling.digits();
	os << " GSMState=" << mGSMState;
	os << " SIPState=" << mSIP.state();
	os << " (" << (stateAge()+500)/1000 << " sec)";
	if (mMessage[0]) os << " message=\"" << mMessage << "\"";
}

void TransactionEntry::message(const char *wMessage, size_t length)
{
	/*if (length>520) {
		LOG(NOTICE) << "truncating long message: " << wMessage;
		length=520;
	}*/
	ScopedLock lock(mLock);
	//memcpy(mMessage,wMessage,length);
	//mMessage[length]='\0';
	mMessage.assign(wMessage, length);
}

void TransactionEntry::messageType(const char *wContentType)
{
	ScopedLock lock(mLock);
	mContentType.assign(wContentType);
}





void TransactionEntry::channel(GSM::LogicalChannel* wChannel)
{
	ScopedLock lock(mLock);
	mChannel = wChannel;
}


void TransactionEntry::GSMState(GSM::CallState wState)
{
	ScopedLock lock(mLock);
	mGSMState = wState;
	mStateTimer.now();
}


void TransactionEntry::echoSIPState(SIP::SIPState state) const
{
	// Caller should hold mLock.
	if (mPrevSIPState==state) return;
	mPrevSIPState = state;
}




SIP::SIPState TransactionEntry::MOCSendINVITE(const char* calledUser, const char* calledDomain, short rtpPort, unsigned codec)
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MOCSendINVITE(calledUser,calledDomain,rtpPort,codec);
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MOCResendINVITE()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MOCResendINVITE();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MOCWaitForOK()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MOCWaitForOK();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MOCSendACK()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MOCSendACK();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::SOSSendINVITE(short rtpPort, unsigned codec)
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.SOSSendINVITE(rtpPort,codec);
	echoSIPState(state);
	return state;
}


SIP::SIPState TransactionEntry::MTCSendTrying()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MTCSendTrying();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MTCSendRinging()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MTCSendRinging();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MTCWaitForACK()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MTCWaitForACK();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MTCCheckForCancel()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MTCCheckForCancel();
	echoSIPState(state);
	return state;
}


SIP::SIPState TransactionEntry::MTCSendOK(short rtpPort, unsigned codec)
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MTCSendOK(rtpPort,codec);
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MODSendBYE()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MODSendBYE();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MODSendUNAVAIL()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MODSendUNAVAIL();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MODSendCANCEL()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MODSendCANCEL();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MODResendBYE()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MODResendBYE();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MODResendCANCEL()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MODResendCANCEL();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MODResendUNAVAIL()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MODResendUNAVAIL();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MODWaitForBYEOK()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MODWaitForBYEOK();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MODWaitForCANCELOK()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MODWaitForCANCELOK();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MODWaitForUNAVAILACK()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MODWaitForUNAVAILACK();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MODWaitFor487()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MODWaitFor487();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MTDCheckBYE()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MTDCheckBYE();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MTDSendBYEOK()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MTDSendBYEOK();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MTDSendCANCELOK()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MTDSendCANCELOK();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MOSMSSendMESSAGE(const char* calledUser, const char* calledDomain, const char* contentType)
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MOSMSSendMESSAGE(calledUser,calledDomain,mMessage.c_str(),contentType);
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MOSMSWaitForSubmit()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MOSMSWaitForSubmit();
	echoSIPState(state);
	return state;
}

SIP::SIPState TransactionEntry::MTSMSSendOK()
{
	ScopedLock lock(mLock);
	SIP::SIPState state = mSIP.MTSMSSendOK();
	echoSIPState(state);
	return state;
}

bool TransactionEntry::sendINFOAndWaitForOK(unsigned info)
{
	ScopedLock lock(mLock);
	return mSIP.sendINFOAndWaitForOK(info);
}

void TransactionEntry::SIPUser(const char* IMSI)
{
	ScopedLock lock(mLock);
	mSIP.user(IMSI);
}

void TransactionEntry::SIPUser(const char* callID, const char *IMSI , const char *origID, const char *origHost)
{
	ScopedLock lock(mLock);
	mSIP.user(callID,IMSI,origID,origHost);
}

void TransactionEntry::called(const L3CalledPartyBCDNumber& wCalled)
{
	ScopedLock lock(mLock);
	mCalled = wCalled;
}


void TransactionEntry::L3TI(unsigned wL3TI)
{
	ScopedLock lock(mLock);
	mL3TI = wL3TI;
}


bool TransactionEntry::terminationRequested()
{
	ScopedLock lock(mLock);
	bool retVal = mTerminationRequested;
	mTerminationRequested = false;
	return retVal;
}



void TransactionTable::init()
	// This assumes the main application uses sdevrandom.
{
	mIDCounter = random();
}



TransactionTable::~TransactionTable()
{
	// Don't bother disposing of the memory,
	// since this is only invoked when the application exits.
	if (mDB) sqlite3_close(mDB);
}




unsigned TransactionTable::newID()
{
	ScopedLock lock(mLock);
	return mIDCounter++;
}


void TransactionTable::add(TransactionEntry* value)
{
	LOG(INFO) << "new transaction " << *value;
	ScopedLock lock(mLock);
	mTable[value->ID()]=value;
}



TransactionEntry* TransactionTable::find(unsigned key)
{
	// Since this is a log-time operation, we don't screw that up by calling clearDeadEntries.

	// ID==0 is a non-valid special case.
	LOG(DEBUG) << "by key: " << key;
	assert(key);
	ScopedLock lock(mLock);
	TransactionMap::iterator itr = mTable.find(key);
	if (itr==mTable.end()) return NULL;
	if (itr->second->dead()) {
		innerRemove(itr);
		return NULL;
	}
	return (itr->second);
}


void TransactionTable::innerRemove(TransactionMap::iterator itr)
{
	LOG(DEBUG) << "removing transaction: " << *(itr->second);
	gSIPInterface.removeCall(itr->second->SIPCallID());
	delete itr->second;
	mTable.erase(itr);
}


bool TransactionTable::remove(unsigned key)
{
	// ID==0 is a non-valid special case, and it shouldn't be passed here.
	if (key==0) {
		LOG(ERR) << "called with key==0";
		return false;
	}

	ScopedLock lock(mLock);
	TransactionMap::iterator itr = mTable.find(key);
	if (itr==mTable.end()) return false;
	innerRemove(itr);
	return true;
}

bool TransactionTable::removePaging(unsigned key)
{
	// ID==0 is a non-valid special case and should not be passed here.
	assert(key);
	ScopedLock lock(mLock);
	TransactionMap::iterator itr = mTable.find(key);
	if (itr==mTable.end()) return false;
	if (itr->second->GSMState()!=GSM::Paging) return false;
	innerRemove(itr);
	return true;
}




void TransactionTable::clearDeadEntries()
{
	// Caller should hold mLock.
	TransactionMap::iterator itr = mTable.begin();
	while (itr!=mTable.end()) {
		if (!itr->second->dead()) ++itr;
		else {
			LOG(DEBUG) << "erasing " << itr->first;
			TransactionMap::iterator old = itr;
			itr++;
			innerRemove(old);
		}
	}
}




TransactionEntry* TransactionTable::find(const GSM::LogicalChannel *chan)
{
	LOG(DEBUG) << "by channel: " << *chan << " (" << chan << ")";

	// Yes, it's linear time.
	// Since clearDeadEntries is also linear, do that here, too.
	clearDeadEntries();

	// Brute force search.
	ScopedLock lock(mLock);
	for (TransactionMap::iterator itr = mTable.begin(); itr!=mTable.end(); ++itr) {
		const GSM::LogicalChannel* thisChan = itr->second->channel();
		//LOG(DEBUG) << "looking for " << *chan << " (" << chan << ")" << ", found " << *(thisChan) << " (" << thisChan << ")";
		if ((void*)thisChan == (void*)chan) return itr->second;
	}
	//LOG(DEBUG) << "no match for " << *chan << " (" << chan << ")";
	return NULL;
}


TransactionEntry* TransactionTable::find(const L3MobileIdentity& mobileID, GSM::CallState state)
{
	LOG(DEBUG) << "by ID and state: " << mobileID << " in " << state;

	// Yes, it's linear time.
	// Since clearDeadEntries is also linear, do that here, too.
	clearDeadEntries();

	// Brtue force search.
	ScopedLock lock(mLock);
	for (TransactionMap::iterator itr = mTable.begin(); itr!=mTable.end(); ++itr) {
		if (itr->second->GSMState() != state) continue;
		if (itr->second->subscriber() == mobileID) return itr->second;
	}
	return NULL;
}


TransactionEntry* TransactionTable::find(const L3MobileIdentity& mobileID, const char* callID)
{
	assert(callID);
	LOG(DEBUG) << "by ID and call-ID: " << mobileID << ", call " << callID;

	string callIDString = string(callID);
	// Yes, it's linear time.
	// Since clearDeadEntries is also linear, do that here, too.
	clearDeadEntries();

	// Brtue force search.
	ScopedLock lock(mLock);
	for (TransactionMap::iterator itr = mTable.begin(); itr!=mTable.end(); ++itr) {
		if (itr->second->mSIP.callID() != callIDString) continue;
		if (itr->second->subscriber() == mobileID) return itr->second;
	}
	return NULL;
}


TransactionEntry* TransactionTable::answeredPaging(const L3MobileIdentity& mobileID)
{
	// Yes, it's linear time.
	// Even in a 6-ARFCN system, it should rarely be more than a dozen entries.

	// Since clearDeadEntries is also linear, do that here, too.
	clearDeadEntries();

	// Brtue force search.
	ScopedLock lock(mLock);
	for (TransactionMap::iterator itr = mTable.begin(); itr!=mTable.end(); ++itr) {
		if (itr->second->GSMState() != GSM::Paging) continue;
		if (itr->second->subscriber() == mobileID) {
			// Stop T3113 and change the state.
			itr->second->GSMState(AnsweredPaging);
			itr->second->resetTimer("3113");
			return itr->second;
		}
	}
	return NULL;
}


GSM::LogicalChannel* TransactionTable::findChannel(const L3MobileIdentity& mobileID)
{
	// Yes, it's linear time.
	// Even in a 6-ARFCN system, it should rarely be more than a dozen entries.

	// Since clearDeadEntries is also linear, do that here, too.
	clearDeadEntries();

	// Brtue force search.
	ScopedLock lock(mLock);
	for (TransactionMap::iterator itr = mTable.begin(); itr!=mTable.end(); ++itr) {
		if (itr->second->subscriber() != mobileID) continue;
		GSM::LogicalChannel* chan = itr->second->channel();
		if (!chan) continue;
		if (chan->type() == FACCHType) return chan;
		if (chan->type() == SDCCHType) return chan;
	}
	return NULL;
}


unsigned TransactionTable::countChan(const GSM::LogicalChannel* chan)
{
	ScopedLock lock(mLock);
	clearDeadEntries();
	unsigned count = 0;
	for (TransactionMap::iterator itr = mTable.begin(); itr!=mTable.end(); ++itr) {
		if (itr->second->channel() == chan) count++;
	}
	return count;
}



size_t TransactionTable::dump(ostream& os) const
{
	ScopedLock lock(mLock);
	for (TransactionMap::const_iterator itr = mTable.begin(); itr!=mTable.end(); ++itr) {
		os << *(itr->second) << endl;
	}
	return mTable.size();
}


TransactionEntry* TransactionTable::findLongestCall()
{
	ScopedLock lock(mLock);
	clearDeadEntries();
	long longTime = 0;
	TransactionMap::iterator longCall = mTable.end();
	for (TransactionMap::iterator itr = mTable.begin(); itr!=mTable.end(); ++itr) {
		if (!(itr->second->channel())) continue;
		if (itr->second->GSMState() != GSM::Active) continue;
		long runTime = itr->second->stateAge();
		if (runTime > longTime) {
			runTime = longTime;
			longCall = itr;
		}
	}
	if (longCall == mTable.end()) return NULL;
	return longCall->second;
}



/* linear, we should move the actual search into this structure */
bool TransactionTable::RTPAvailable(short rtpPort)
{
	ScopedLock lock(mLock);
	clearDeadEntries();
	bool avail = true;
 	for (TransactionMap::iterator itr = mTable.begin(); itr!=mTable.end(); ++itr) {
		if (itr->second->mSIP.RTPPort() == rtpPort){
			avail = false;
			break;
		}
	}
	return avail;
}

// vim: ts=4 sw=4
