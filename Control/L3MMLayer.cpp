/* 
* Copyright 2013, 2014 Range Networks, Inc.
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

// Written by Pat Thompson

#define LOG_GROUP LogGroup::Control		// Can set Log.Level.Control for debugging

#include "L3MMLayer.h"
#include "L3MobilityManagement.h"
#include <GSMConfig.h>
#include "L3CallControl.h"
#include <GSMLogicalChannel.h>		// Needed for L3LogicalChannel
#include <GSML3Message.h>		// Needed for L3LogicalChannel
#include <Logger.h>
#include <Interthread.h>
#include <Threads.h>
#include <GSMTransfer.h>
#include "ControlCommon.h"
#include "L3TranEntry.h"
#include "L3SMSControl.h"
#include "L3SupServ.h"
#include <SIPDialog.h>

namespace Control {
using namespace GSM;
using namespace SIP;

MMLayer gMMLayer;
Mutex gMMLock;		// This is a global lock for adding/removing MMContext and MMUser and hooking them together.
					// This global lock does not prevent internal modification of an MMContext/MMUser.
					// The global lock must not be held when executing any state machines.
					// See more comments at MMContext.

// What is to prevent an MS from allocating multiple channels, eg, a TCH and SDCCH simultaneously?  I think nothing.

// Procedure State Machines:
//	assignTCHF
//		on success (have TCH), if MTCq, start it, else (they hung up) if SMSq run switchToSDDCH, else run closeChannel.
//	switchToSDCCH
//		on success (have SDCCH), if MTCq, run assignTCHF, else if SMSq start it, else run closeChannel.
// Add new transaction:
// 		If existing MM procedure on TMSI or IMSI, block.
// If SMS and existing SMS, queue.

// On end of any transaction (MM or CS or SMS):
// On end of CS transaction:
//		If MTCq, run that, else if MTSMSq run switchToSDCCH, else run closeChannel.
// On end of SMS transaction:
//		If MTSMSq, run that.
// On end of MM transaction:
//		If MTCq, start assignTCH.
// On assignTCH success:
//		if MTCq, run it, else, goto ?


//void MMUser::mmuCleanupDialogs() { }

void MMContext::startSMSTran(TranEntry *tran)
{
	{
		ScopedLock lock(gMMLock,__FILE__,__LINE__);	// Make sure.
		LOG(INFO) << "new MTSMS"<<LOGVAR(tran)<<LOGVAR2("chan",this);
		// Tie the transaction to this channel.
		devassert(this->mmGetTran(MMContext::TE_MTSMS).isNULL());
		this->mmConnectTran(MMContext::TE_MTSMS,tran);

		initMTSMS(tran);
	}
	tran->lockAndStart();
}

void MMContext::startSSTran(TranEntry *tran)
{
	{
		ScopedLock lock(gMMLock,__FILE__,__LINE__);	// Make sure.
		LOG(INFO) << "new MTSS"<<LOGVAR(tran)<<LOGVAR2("chan",this);
		// Tie the transaction to this channel.
		devassert(this->mmGetTran(MMContext::TE_SS).isNULL());
		this->mmConnectTran(MMContext::TE_SS,tran);
		initMTSS(tran);
	}
}

// (pat) WARNING: If this routine returns true it has performed the gMMLock.unlock() corresponding to a lock() in the caller.
// Setting the lock in one function and releasing it in another sucks and should be fixed.
bool MMUser::mmuServiceMTQueues()	// arg redundant with mmuContext->channel.
{
	devassert(gMMLock.lockcnt());		// Caller locked it.
	//ScopedLock lock(mmuLock,__FILE__,__LINE__);
	// TODO: check for blocks on our IMSI or TMSI?
	//LOG(INFO) << "SS mmuServiceMTQueues()";

	// TODO: Move this to the logical channel main thread.
	// Service the MMC queues.
	if (mmuContext->mmGetTran(MMContext::TE_CS1).isNULL()) {
		if (mmuMTCq.size()) {
			TranEntry *tran = mmuMTCq.pop_frontr();
			LOG(INFO) << "new MTC"<<LOGVAR(tran)<<LOGVAR2("chan",mmuContext);

			// Tie the transaction to this channel.
			mmuContext->mmConnectTran(MMContext::TE_CS1,tran);

			// Did the SIP session give up while we were waiting?
			// That will be handled in the MTCMachine.
			switch (tran->servicetype()) {
			default:
				initMTC(tran);
				break;
			}
			gMMLock.unlock();
			tran->lockAndStart();
			return true;
		}
	}
	if (mmuContext->mmGetTran(MMContext::TE_MTSMS).isNULL()) {
		if (mmuMTSMSq.size()) {
			TranEntry *tran = mmuMTSMSq.pop_frontr();
			gMMLock.unlock();
			mmuContext->startSMSTran(tran);
			return true;
		}
	}
	if (mmuContext->mmGetTran(MMContext::TE_SS).isNULL()) {
		LOG(INFO) << "SS mmuContext->mmGetTran(MMContext::TE_SS).isNULL()";
		if (mmuMTSSq.size()) {
			TranEntry *tran = mmuMTSSq.pop_frontr();
			gMMLock.unlock();
			mmuContext->startSSTran(tran);
			return true;
		}
	}
	return false;
}

bool MMUser::mmuIsEmpty()
{
	ScopedLock lock(mmuLock,__FILE__,__LINE__);
	return mmuMTCq.size() + mmuMTSMSq.size() == 0;
}

bool MMContext::mmIsEmpty()
{
	//devassert(gMMLock.lockcnt());		// Caller locked it.
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	for (unsigned i = 0; i < TE_num; i++) {
		if (mmcTE[i] != NULL) { return false; }
	}
	return mmcMMU ? mmcMMU->mmuIsEmpty() : true;
}

// Return the Mobility Management state.  Defined in 24.008 section 4.
// Except the only thing we really care about is whether any MM procedure is currently running, which is boolean.
bool MMContext::mmInMobilityManagement()
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// (pat) I dont think this lock is necessary because we use RefCntPointer now.
	return ! mmGetTran(MMContext::TE_MM).isNULL();
}

// See if there are any new transactions to start.
// If all transactions are gone, initiate a channel release.
// Return true if anything happened.
bool MMContext::mmCheckNewActivity()
{
	// Dont lock gMMLock up here - we may send a message later and we cant do that holding the lock.

	// If there is a mobility management procedure in progress then we wont start anything else until it is finished.
	// We shouldnt need to lock gMMLock yet because the MMContext cannot be deleted while in the thread that called us,
	// and mmcServiceRequests is a thread safe queue.
	if (! mmInMobilityManagement()) {
		if (const L3Message *l3msg = mmcServiceRequests.readNoBlock()) {
			const L3CMServiceRequest *cmmsg = dynamic_cast<typeof(cmmsg)>(l3msg);
			NewCMServiceResponder(cmmsg,this);
			delete cmmsg;
			return true;
		}
		// We are refererencing the MMUser so we cannot let that change and the only
		// completely safe way to do that is to lock the entire MMLayer.
		// The unlock() corresponding to this lock() may be in mmuServiceMTQueues.
		gMMLock.lock(__FILE__,__LINE__); // Do not replace this one with a scoped lock!
		if (mmcMMU) {
			if (mmcMMU->mmuServiceMTQueues()) { return true; }	// If it returns true it unlocked the lock, gack.
		}
		gMMLock.unlock();
	}
	// If there are no transactions, kill the channel.
	// We wait 5 seconds to allow a transaction to start; otherwise there is a race because
	// the channel service thread that calls this method is started by an ESTABLISH sent by layer 1,
	// which is sent before the message that initiates the transaction is sent.
	// The 5 seconds is kind of made up.  It doesnt have to be very long because at channel initiation
	// the signal should be good.
	// TODO: A new SIP transaction could creep in here between the time
	// we check isEmpty and when the channel actually closes.
	// What to do about that?
	// When we detach the MMUser, if it has anything on it, just leave it there,
	// and paging will restart.
	if (mmIsEmpty() && mmcDuration() > 5) {
		LOG(DEBUG) <<"closing"<<this;
		mmcChan->chanClose(L3RRCause::Normal_Event,L3_RELEASE_REQUEST,TermCause::Local(L3Cause::No_Transaction_Expected));
		return true;	// This is new activity - the calling loop should skip back to the top
	}
	return false;
}

// What a stupid language.
MMUser::MMUser(string& wImsi)
{
	MMUserInit();
	mmuImsi = wImsi;
	LOG(DEBUG) << "MMUser ALLOC "<<(void*)this;
}

//MMUser::MMUser(string& wImsi, TMSI_t wTmsi)
//{
//	MMUserInit();
//	mmuImsi = wImsi;
//	mmuTmsi = wTmsi;
//	LOG(DEBUG) << "MMUser ALLOC "<<(void*)this;
//}


//GSM::CMServiceTypeCode MMUser::mmuGetInitialServiceType()
//{
//	devassert(gMMLock.lockcnt());		// Caller locked it.
//	if (mmuMTCq.size()) {
//		TranEntry *front = this->mmuMTCq.front();
//		return front->servicetype();
//	}
//	devassert(mmuMTSMSq.size());
//	// The purpose of this is to choose the channel type, so it doesnt really matter what the servicetype is as long as it is one that can use SDCCH.
//	return L3CMServiceType::ShortMessage;
//}

GSM::ChannelType MMUser::mmuGetInitialChanType() const
{
	devassert(gMMLock.lockcnt());		// Caller locked it.
	if (mmuMTCq.size()) {
		TranEntry *front = this->mmuMTCq.front();
		switch (front->servicetype()) {
		case L3CMServiceType::MobileOriginatedCall:
			devassert(0);
		case L3CMServiceType::MobileTerminatedCall:
		case L3CMServiceType::EmergencyCall:
			return gConfig.getBool("Control.VEA") ?  GSM::TCHFType : GSM::SDCCHType;
		default:	// There shouldnt be anything else in the MTCq.
			return GSM::SDCCHType;
		}
	}
	if (mmuMTSSq.size()){
		return GSM::SDCCHType;
	}
	devassert(mmuMTSMSq.size());
	return GSM::SDCCHType;
}

// Caller enters with the whole MMLayer locked so no one will try to add new contexts while we are doing this.
void MMUser::mmuFree(MMUserMap::iterator *piter, TermCause cause)	// Some callers deleted it from the MMUsers more efficiently than looking it up again.
{
	devassert(mmuContext == NULL);	// Caller already unlinked or verified that it was unattached.
	devassert(gMMLock.lockcnt());		// Caller locked it.

	// mmuCleanupDialogs();

	{
	ScopedLock lock(mmuLock,__FILE__,__LINE__);			// Now redundant.
	LOG(DEBUG) << "MMUser DELETE "<<(void*)this <<LOGVAR(!!piter);
	// At this point the only pointer to the transaction is in the InterthreadQueue.
	// Once the transaction moves to the MMContext it will be put in a RefCntPointer.
	// 10-10-2014: Formerly we needed to delete tran here, but now it is done inside teCancel.
	while (TranEntry *tran = mmuMTCq.pop_frontr()) { tran->teCancel(cause); }
	while (TranEntry *tran = mmuMTSMSq.pop_frontr()) { tran->teCancel(cause); }

	if (piter) {		// It is just an efficiency issue to use the iterator if we already have one.
		gMMLayer.MMUsers.erase(*piter);
	} else {
		LOG(DEBUG) << "MMUser erase begin " << this->mmuImsi;
		bool exists = gMMLayer.MMUsers.find(this->mmuImsi) != gMMLayer.MMUsers.end();
		LOG(DEBUG) << "MMUser erase "<<this->mmuImsi<<LOGVAR(exists);
		gMMLayer.MMUsers.erase(this->mmuImsi);
	}
	assert(gMMLayer.MMUsers.find(this->mmuImsi) == gMMLayer.MMUsers.end());
	}
	// The ScopedLock points into MMUser so we must release it before deleting this.
	delete this;
}

void MMContext::mmGetTranList(TranEntryVector &tranlist)
{
	tranlist.clear();	// Be sure.
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	for (unsigned i = TE_first; i < TE_num; i++) {
		RefCntPointer<TranEntry> tranp = mmGetTran(i);
		if (! tranp.isNULL()) { tranlist.push_back(tranp); }
	}
}


bool MMContext::mmCheckSipMsgs()
{
	// Update: We cannot hold the global lock while invoking state machines because they can block.
	// As an interim measure, just dont lock this and hope for the best.
	// (pat) Update 9-19-2014: Formerly, when this global lock was in place we got the "blocked more than one second at..."
	// messages during the stress test.  Ticket #1905 reports a crash that looks like using a transaction here while
	// it is being deleted.  Since I have separated layer2 from layer3 with InterthreadQueues since the last test,
	// I am re-enabling this global lock to attempt to fix the crash.
	// (pat) 10-10-2014 update: Holding the global lock here appears to cause system lockups as reported in St Pierre.  Not sure why.
	// So here is the revised plan: The gMMLock will be used only temporarily to protect access to the list, then we will
	// drop the global lock and attempt to invoke lockAndInvokeSipMsgs, which will be modified to check that the transaction
	// has not been deleted in the interim period - the mL3RewriteLock in the transaction prevents collisions at that transaction level.
	// ScopedLock lock(gMMLock,__FILE__,__LINE__);
	TranEntryVector tranlist;
	mmGetTranList(tranlist);
	bool result = false;
	for (TranEntryVector::iterator it = tranlist.begin(); it != tranlist.end(); it++) {
		TranEntry *tranp = (*it).self();
		result |= tranp->lockAndInvokeSipMsgs();
	}
	return result;
}

bool MMContext::mmCheckTimers()
{
	// Update: We cannot hold the global lock while invoking state machines because they can block.
	// As an interim measure, just dont lock this and hope for the best.
	//ScopedLock lock(gMMLock,__FILE__,__LINE__);	// I think this is unnecessary; the channel cannot change when this is called, but be safe.
	// Check MM timers.
	TranEntryVector tranlist;
	mmGetTranList(tranlist);

	// checkTimers locks the transaction if any timer needs servicing.
	bool result = false;
	for (TranEntryVector::iterator it = tranlist.begin(); it != tranlist.end(); it++) {
		TranEntry *tranp = (*it).self();
		result |= tranp->checkTimers();
	}
	return result;
}


// This is the first L3 message on the new channel.
// The Context for this channel is empty.
// All the TranEntrys are still in the Context on the old channel.
//void MMContext::reassignComplete()
//{
//
//	//case L3RRCASE(AssignmentComplete):
//	// TODO: what timer? timeout1Cancel();
//	//timerStop(TChReassignment);		// Handled by assignTCHFProcedure, which is notified after us.
//	LOG(INFO) << "successful assignment";
//
//	// The two channels are serviced by different threads.
//	// TODO: We need to lock the other channels thread.
//
//	// Move all the transactions to the new channel:
//	MMContext *prevSet = mPrevChan->getContext();
//	for (unsigned i = 0; i < TE_num; i++) {
//		devassert(mmcTE[i] == NULL);
//		mmcTE[i] = prevSet->mmcTE[i];
//		prevSet->mmcTE[i] = NULL;
//		if (mmcTE[i]) { mmcTE[i]->mContext = this; }
//	}
//#endif
//
//	// release the old channel.
//	// The old SDCCH channel will be released when the l2recv finishes and the channel notices that its state has changed.
//	// mPrevChan->l3sendp(GSM::HARDRELEASE);  Dont do this.  It can block.  Set the chReassignComplete flag and let that thread do it.
//	mPrevChan->chanSetState(L3LogicalChannel::chReassignComplete);
//
//	// We are going to delete this.  So get everything we want out of it first.
//
//	// Just move the MMContext prevChan to this one.
//	//mChan->freeContext();	// It is not being used, but we are running in it!
//	//mChan->mContext = mPrevChan->mTranSet;
//	//mPrevChan->mContext = NULL;
//
//	mPrevChan->chanMoveTo(this->mChan);		// Careful!  Deletes this as a side effect.
//
//	// Clear everything.  This is overkill because some of these are already 0.
//	prevSet->mNextChan = prevSet->mPrevChan = 0;
//	this->mNextChan = this->mPrevChan = 0;
//
//	//tran()->setChannel(tran()->mNextChannel);
//	//tran()->mNextChan = NULL;
//	//return callProcStart(new MOCConnect(tran()));  Now it could be for MTC too.
//}

// The significant bits of the L3TI.  The fourth bit is a direction indicator and we ignore it.
//static int l3TISigBits(int val) { return val & 7; }

#if UNUSED
// // Find the transaction that wants this frame/message.
// TranEntry *MMContext::findTran(L3PD pd, unsigned ti, int mti)
// {
// 	devassert(gMMLock.lockcnt());		// Caller locked it.
// 	TranEntry *tran = NULL;
// 	switch (pd) {
// 		case L3CallControlPD: {
// 			// Setup message is special because it is the message that establishes the TI correspondence.
// 			bool isSetup =  (mti == L3CCMessage::Setup);
// 			TranEntry *cs = mmcTE[TE_CS1];
// 			if (cs && (isSetup || l3TISigBits(cs->getL3TI()) == l3TISigBits(ti))) {
// 				return cs;
// 			}
// 			break;
// 		}
// 		case L3SMSPD: {
// 			for (int tx = TE_MOSMS1; tx <= TE_MTSMS; tx++) {
// 				TranEntry *sms = mmcTE[tx];
// 				if (sms && l3TISigBits(sms->getL3TI()) == l3TISigBits(ti)) {
// 					tran = sms;
// 					break;
// 				}
// 			}
// 			// For MO-SMS the TI in the transaction is not set until the first CP-DATA message arrives.
// 			// So if no transaction matched this specific TI, we send the message to the primary MO-SMS transaction and hope for the best.
// 			if (tran == NULL) { tran = mmcTE[TE_MOSMS1]; }
// 			break;
// 		}
// 		case L3RadioResourcePD:
// #if 0	// Now both channels share the MMContext so we just send it normally.
// 			if (l3msg->MTI() == L3RRMessage::AssignmentComplete) {
// 				// We have to notify the Procedure when complete, however when rmsimsieassignComplete returns
// 				// we have replaced the MMContext on this channel with the one from the old channel,
// 				// and 'this' has been deleted.  So we pass the tran that needs to be notified to reassignComplete
// 				// to actually do it, and we have to return from here without touching the data again.
// 				if (! postReassignment) {
// 					L3LogicalChannel *chan = this->tsChannel();
// 					chan->reassignComplete();
// 					// Be careful! 'this' is now invalid!  That is why we cached channel().
// 					// Redispatch the message on the now-reassigned channel.
// 					return chan->chanGetContext()->mmDispatchL3Msg(l3msg,true);
// 				}
// 				// else fall through
// 			}
// #endif
// 			// Fall through for all other RR messages.
// 		case L3MobilityManagementPD:
// 			// TODO: This is a hack.  We should split the Procedures into MM and CS parts, and
// 			// run the MM procedure first to identify the channel, then send a message to the CS procedure to start it.
// 			tran = mmcTE[TE_MM] ? mmcTE[TE_MM] : mmcTE[TE_CS1] ? mmcTE[TE_CS1] : mmcTE[TE_MOSMS1] ? mmcTE[TE_MOSMS1] : mmcTE[TE_MTSMS];
// 			break;
// 		default:
// 			LOG(ERR) << "unrecognized L3 frame:"<<LOGVAR(pd)<<LOGVAR(ti);
// 			return NULL;	// hopeless.
// 	}
// 	return NULL;
// }
#endif

// Either frame or l3msg may be NULL.
RefCntPointer<TranEntry> MMContext::findTran(const L3Frame *frame, const L3Message *l3msg) const
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__); //FIXMENOW
	GSM::L3PD pd; int ti;
	// Handle naked primitives.
	if (frame && !frame->isData()) {
		// Theoretically primitives should go to all transactions, but in reality the only state
		// machine that wants to receive primitives is MT-SMS, so we will just triage the primitives here
		// by returning that transaction, if any.
		return mmcTE[TE_MTSMS].self();	// We dont need to keep this locked because the tran is used only internally, so can use self and return TranEntry*
	}
	if (frame && frame->isData()) {
		pd = frame->PD();
		ti = frame->TI();		// Only call control, SMS and SS frames have a useful ti.
	} else if (l3msg) {
		pd = l3msg->PD();
		ti = l3msg->TI();		// Only call control, SMS and SS frames have a useful ti; returns nonsense for other PDs.
	} else {
		return (TranEntry*)NULL;	// Shouldnt happen, but be safe.
	}
	switch (pd) {
		case L3CallControlPD: {
			// Setup message is special because it is the message that establishes the TI correspondence.
			// Dont need to bother checking l3msg because we dont send naked setup messages.
			int mti = frame ? frame->MTI() : l3msg->MTI();
			bool isSetup = (mti == L3CCMessage::Setup);
			TranEntry *cs = mmcTE[TE_CS1].self();
			if (cs && (isSetup || cs->matchL3TI(ti,true))) {
				return cs;
			}
			break;
		}
		case L3SMSPD: {
			for (int tx = TE_MOSMS1; tx <= TE_MTSMS; tx++) {
				TranEntry *sms = mmcTE[tx].self();
				if (sms && sms->matchL3TI(ti,true)) {
					return sms;
				}
			}
			// For MO-SMS the TI in the transaction is not set until the first CP-DATA message arrives.
			// So if no transaction matched this specific TI, we send the message to the primary MO-SMS transaction and hope for the best.
			if (TranEntry *te = mmcTE[TE_MOSMS1].self()) { return te; }
			break;
		}
		case L3RadioResourcePD:
			// Fall through for all other RR messages.
		case L3MobilityManagementPD:
			// TODO: This is a hack.  We should split the Procedures into MM and CS parts, and
			// run the MM procedure first to identify the channel, then send a message to the CS procedure to start it.
			//return mmcTE[TE_MM] ? mmcTE[TE_MM] : mmcTE[TE_CS1] ? mmcTE[TE_CS1] : mmcTE[TE_MOSMS1] ? mmcTE[TE_MOSMS1] : mmcTE[TE_MTSMS];
			for (unsigned txi = TE_MM; txi < TE_num; txi++) {			
				if (TranEntry *te = mmcTE[txi].self()) { return te; }
			}
			break;
		case L3NonCallSSPD: {
				// The transaction identifier is used to identify whether the SS message applies to a specific
				// call or is outside any call, ie, was started by a CM service request.
				// I am not sure what to do about in-call SS: we could pass the USSD SIP INFO message in
				// the dialog of the call, but asterisk is going to just dump it.
				for (ActiveTranIndex ati = TE_CS1; ati <= TE_CSHold; ati = (ActiveTranIndex) (ati + 1)) {
					TranEntry *cs = mmcTE[ati].self();
					if (cs && cs->matchL3TI(ti,true)) {
						WATCHINFO("Found SS message matching CC transaction" <<LOGVAR2("message ti",ti)<<LOGVAR2("transaction ti",cs->getL3TI()));
						return cs;
					}
				}
				TranEntry *te = mmcTE[TE_SS].self();
				if (te) {
					// Dont even bother to check the tran id.
					// If it is a SSRegister message, it would be defining the tran id.
					WATCHINFO("Sending SS message to SS machine"<<LOGVAR2("message ti",ti)<<LOGVAR2("transaction ti",te->getL3TI()));
				} else {
					WATCH("Ignoring SS message with no transaction "<<l3msg);
				}
				return te;
			}
			break;
		default:
			LOG(ERR) << "unrecognized L3 frame:"<<LOGVAR(pd);
			return (TranEntry*)NULL;	// hopeless.
	}
	LOG(INFO) << "No transaction found to handle frame with"<<LOGVAR2("PD",pd)<<LOGVAR2("TI",ti)<<LOGVAR2("MTI",frame->MTI());
	return (TranEntry*)NULL;
}


#if UNUSED
// Find the transaction that wants to handle this message and invoke it.
// Return true if the message was handled.
//bool MMContext::mmDispatchL3Msg(const L3Message *l3msg, bool postReassignment)
bool MMContext::mmDispatchL3Msg(const L3Message *l3msg, L3LogicalChannel *chan)
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// I think this is unnecessary, but be safe.
	GSM::L3PD pd = l3msg->PD();
	LOG(DEBUG) << LOGVAR(pd) << this;
	int ti = 0, mti = 0;
	if (pd == L3CallControlPD || pd == L3SMSPD) {
		ti = l3msg->TI();
		mti = l3msg->MTI();
	}
	RefCntPointer<TranEntry> tran = findTran(pd,ti,mti);

	//TranEntry *tran = NULL;
	////GSM::L3CMServiceType service = tran->service();
	//switch (pd) {
	//	case L3CallControlPD: {
	//		//bool isSetup = (pd == L3CallControlPD) && l3msg->MTI() == L3CCMessage::Setup;
	//		//msgti = dynamic_cast<L3CCMessage*>(l3msg)->TI();
	//		// Setup message is special because it is the messages that establishes the TI correspondence.
	//		TranEntry *cs = mmcTE[TE_CS1];
	//		if (cs) LOG(DEBUG) << cs <<LOGVAR(l3msg->TI());
	//		if (cs && (l3msg->MTI() == L3CCMessage::Setup || l3TISigBits(cs->getL3TI()) == l3TISigBits(l3msg->TI()))) {
	//			tran = cs;
	//		}
	//		break;
	//	}
	//	case L3SMSPD: {
	//		//msgti = dynamic_cast<CPMessage*>(l3msg)->TI();
	//		for (int tx = TE_MOSMS1; tx <= TE_MTSMS; tx++) {
	//			TranEntry *sms = mmcTE[tx];
	//			if (sms) {
	//				LOG(DEBUG) <<LOGVAR(tx)<<LOGVAR(sms->getL3TI())<<LOGVAR(l3msg->TI());
	//			}
	//			if (sms && l3TISigBits(sms->getL3TI()) == l3TISigBits(l3msg->TI())) {
	//				tran = sms;
	//				break;
	//			}
	//			// For MO-SMS the TI in the transaction is not set until the first CP-DATA message arrives.
	//			// So if no transaction matched this specific TI, we send the message to the primary MO-SMS transaction and hope for the best.
	//			if (tran == NULL) { tran = mmcTE[TE_MOSMS1]; }
	//		}
	//		break;
	//	}
	//	case L3RadioResourcePD:
	//#if 0	// Now both channels share the MMContext so we just send it normally.
	//		if (l3msg->MTI() == L3RRMessage::AssignmentComplete) {
	//			// We have to notify the Procedure when complete, however when rmsimsieassignComplete returns
	//			// we have replaced the MMContext on this channel with the one from the old channel,
	//			// and 'this' has been deleted.  So we pass the tran that needs to be notified to reassignComplete
	//			// to actually do it, and we have to return from here without touching the data again.
	//			if (! postReassignment) {
	//				L3LogicalChannel *chan = this->tsChannel();
	//				chan->reassignComplete();
	//				// Be careful! 'this' is now invalid!  That is why we cached channel().
	//				// Redispatch the message on the now-reassigned channel.
	//				return chan->chanGetContext()->mmDispatchL3Msg(l3msg,true);
	//			}
	//			// else fall through
	//		}
	//#endif
	//		// Fall through for all other RR messages.
	//	case L3MobilityManagementPD:
	//		// TODO: This is a hack.  We should split the Procedures into MM and CS parts, and
	//		// run the MM procedure first to identify the channel, then send a message to the CS procedure to start it.
	//		tran = mmcTE[TE_MM] ? mmcTE[TE_MM] : mmcTE[TE_CS1] ? mmcTE[TE_CS1] : mmcTE[TE_MOSMS1] ? mmcTE[TE_MOSMS1] : mmcTE[TE_MTSMS];
	//		break;
	//	default:
	//		LOG(ERR) << "unrecognized L3"<<LOGVAR(pd);
	//		return NULL;	// hopeless.
	//}
	if (tran && ! tran->deadOrRemoved()) {
		LOG(DEBUG) << tran;
		// We pass chan, not mmcChan.  They are != only during channel reassignment.
		return tran->lockAndInvokeL3Msg(l3msg);
	}
	return false;
}
#endif

// Arguments are the L3 frame and if the frame is non-primitive the result of parsel3(frame).
// l3msg may be NULL for primitives or unparseable messages.
// Frame may be NULL when we send a naked message.
bool MMContext::mmDispatchL3Frame(const L3Frame *frame, const L3Message *msg)
{
	// Update: We cannot hold the global lock while invoking state machines because they can block.
	// As an interim measure, just dont lock this and hope for the best.
	//ScopedLock lock(gMMLock,__FILE__,__LINE__);	// I think this is unnecessary, but be safe.
	// Does any transaction want this frame/message?
	RefCntPointer<TranEntry> tran = findTran(frame,msg);
	LOG(DEBUG) << *frame << tran.self();
	if (tran == (TranEntry*)NULL) { return false; }

	if (tran->deadOrRemoved()) {
		if (msg) {
			LOG(INFO) <<"Received message for expired transaction. "<<*msg;
		} else {
			LOG(INFO) <<"Received unparseable frame for expired transaction. "<<*frame;
		}
		return false;
	}
	return tran->lockAndInvokeFrame(frame,msg);
}


void MMContext::mmcPageReceived() const
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// I think this is unnecessary, but be safe.
	// TODO: We should do a single authentication, if necessary, before starting both MTC and MTSMS

	RefCntPointer<TranEntry> tran1 = mmGetTran(MMContext::TE_CS1);
	if (! tran1.isNULL()) {
		// The MS sent a page response when there is an active voice call?  Either that or we are totally goofed up.
		LOG(ERR) <<mmcChan <<" received page response while MS had active voice call:"<<tran1.self();
	}
	RefCntPointer<TranEntry> tran2 = mmGetTran(MMContext::TE_MTSMS);
	if (! tran2.isNULL()) {
		LOG(ERR) <<mmcChan <<" received page response while MS had active MT-SMS:"<<tran2.self();
	}

	// We dont need to do anything else.  The service loop will notice and start new transactions.
}

// TODO: We need to save when each TI was last used instead of just round-robin them.
unsigned MMContext::mmGetNextTI()
{
	mNextTI++;
	// L3TI values are 0-7, with bit 4 set when communicating a TI sent by the other side.
	// Avoid the value 7.  It is an extension mechanism in some protocols and an error indication in others.
	if (mNextTI >= 7) { mNextTI = 0; }
	return mNextTI;
}

#if UNUSED
bool MMLayer::mmStartMTDialog(SipDialog *dialog, SipMessage *invite)
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// I think this is unnecessary, but be safe.

#if UNUSED
	// Find any active transaction for this IMSI with an assigned TCH or SDCCH.
	L3LogicalChannel *chan = gTransactionTable.findChannel(mobileID);
	if (chan) {
		// If the type is TCH and the service is SMS, get the SACCH.
		// Otherwise, for now, just say chan=NULL.
		if (serviceType==L3CMServiceType::MobileTerminatedShortMessage && chan->chtype()==FACCHType) {
			chan = chan->getL2Channel()->SACCH();	// GSM Specific.
		} else {
			// FIXME -- This will change to support multiple transactions.
			// (pat) Yes.  For voice calls we need to initiate a call-waiting notification.
			// For SMS we need to add to the SMS queue.
			chan = NULL;
		}
	}
#endif

	TODO: Add this isBusy check

	// So we will need a new channel.
	// Check gBTS for channel availability.
	if (!chan && !channelAvailable) {
		LOG(CRIT) << "MTC CONGESTION, no channel availble";
		// FIXME -- We need the retry-after header.
		//newSendEarlyError(msg,proxy.c_str(),503,"Service Unvailable");
		dialog->sendError(503,"Service Unavailable");
		return;
	}
	if (chan) { LOG(INFO) << "using existing channel " << chan->descriptiveString(); }
	else { LOG(INFO) << "set up MTC paging for channel=" << requiredChannel; }

	// Check for new user busy condition.
	if (!chan && gTransactionTable.isBusy(mobileID)) {
		LOG(NOTICE) << "user busy: " << mobileID;
		//newSendEarlyError(msg,proxy.c_str(),486,"Busy Here");
		dialog->sendError(503,"Service Unavailable");
		dialog->detach();
		return;
	}

	return true;
}
#endif

void MMUser::mmuAddMT(TranEntry *tran)
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// Way overkill.
	//ScopedLock lock(mmuLock,__FILE__,__LINE__);
	mmuPageTimer.future(gConfig.GSM.Timer.T3113);
	switch (tran->servicetype()) {
	case L3CMServiceType::MobileTerminatedCall:
		mmuMTCq.push_back(tran);
		break;
	case L3CMServiceType::MobileTerminatedShortMessage:
		mmuMTSMSq.push_back(tran);
		break;
	case L3CMServiceType::SupplementaryService:
		mmuMTSSq.push_back(tran);
		break;
	default:
		assert(0);
	}
}

void MMUser::mmuText(std::ostream&os) const
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// way overkill
	//ScopedLock lock(mmuLock,__FILE__,__LINE__);
	os << " MMUser(";
	os <<LOGVAR2("state",mmuState) <<LOGVAR2("imsi",mmuImsi) <<LOGVAR2("tmsi",mmuTmsi);
	if (mmuContext) {
		os << " channel:" << mmuContext->tsChannel();
	} else {
		os << " channel:(unattached)";
	}
	os << " queued transactions:";
	//os <<LOGVAR2("MTC_queue_size",mmuMTCq.size()) <<LOGVAR2("SMS_queue_size",mmuMTSMSq.size());
	for (MMUQueue_t::const_iterator it = mmuMTCq.begin(); it != mmuMTCq.end(); ++it) {
		const TranEntry *tran = *it;
		os << " CS:" << tran->tranID();
	}
	for (MMUQueue_t::const_iterator it = mmuMTSMSq.begin(); it != mmuMTSMSq.end(); ++it) {
		const TranEntry *tran = *it;
		os << " SMS:" << tran->tranID();
	}
	os << ")";
}
string MMUser::mmuText() const { std::ostringstream ss; mmuText(ss); return ss.str(); }
std::ostream& operator<<(std::ostream& os, const MMUser&mmu) { mmu.mmuText(os); return os; }
std::ostream& operator<<(std::ostream& os, const MMUser*mmu) { if (mmu) mmu->mmuText(os); else os << "(null MMUser)"; return os; }

void MMContext::MMContextInit()
{
	// (pat) The BLU phone seems to have a bug that a new MTC beginning too soon after a previous MTC with the same TI
	// seems to hang the phone, even though we definitely went through the CC release procedure whose specific
	// purpose is to release the TI for recycling.  Making the initial TI random seems to help.
	mNextTI = rand() & 0x7;		// Not supposed to matter what we pick here.
	mmcMMU = NULL;
	mmcChan = NULL;
	mmcChannelUseCnt = 1;
	//mVoiceTrans = NULL;
	memset(mmcTE,0,sizeof(mmcTE));
	mmcOpenTime = time(NULL);
	LOG(DEBUG)<<"MMContext ALLOC "<<(void*)this;
}

// Called only from L3LogicalChannel::chanGetContext()
MMContext::MMContext(L3LogicalChannel *wChan)
{
	MMContextInit();
	mmcChan = wChan;
}

MMContext *MMContext::tsDup()
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	mmcChannelUseCnt++;		// There are now two channels referring to the same MMContext.
	LOG(DEBUG) << *this;
	return this;
}

string MMContext::mmGetImsi(bool verbose)
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// I think this is unnecessary, but be safe.
	return mmcMMU ? mmcMMU->mmuGetImsi(verbose) : (verbose ? string("no-MMUser") : string(""));
}

void MMContext::l3sendm(const GSM::L3Message& msg, const GSM::Primitive& prim/*=GSM::DATA*/, SAPI_t SAPI/*=0*/)
{
	WATCHINFO("sendm "<<this <<LOGVAR(prim)<<LOGVAR(SAPI)<<" "<<msg);
	mmcChan->l2sendm(msg,prim,SAPI);
}

void MMContext::mmcText(std::ostream&os) const
{
	// Called from CLI so we need to lock the MMContext, and the only way we can do that is the global lock.
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// way overkill.
	os << " MMContext(";
	os <<mmcChan;
	os <<LOGVAR(mmcChannelUseCnt);
	os <<LOGVAR2("duration",mmcDuration());
	if (mmcMMU) { os <<LOGVAR(mmcMMU); }
	if (mmcTE[TE_MM] != NULL) { os <<LOGVAR2("MM",*mmcTE[TE_MM]); }
	if (mmcTE[TE_CS1] != NULL) { os <<LOGVAR2("CS",*mmcTE[TE_CS1]); }
	if (mmcTE[TE_MOSMS1] != NULL) { os <<LOGVAR2("MO-SMS",*mmcTE[TE_MOSMS1]); }
	if (mmcTE[TE_MOSMS2] != NULL) { os <<LOGVAR2("MO-SMS2",*mmcTE[TE_MOSMS2]); }
	if (mmcTE[TE_MTSMS] != NULL) { os <<LOGVAR2("MT-SMS",*mmcTE[TE_MTSMS]); }
	if (mmcTE[TE_SS] != NULL) { os <<LOGVAR2("SS",*mmcTE[TE_SS]); }
	os << ")";
}

std::ostream& operator<<(std::ostream& os, const MMContext&mmc) { mmc.mmcText(os); return os; }
std::ostream& operator<<(std::ostream& os, const MMContext*mmc) { if (mmc) mmc->mmcText(os); else os << "(null Context)"; return os; }


void MMContext::getTranIds(TranEntryList &tranlist) const
{
	// This is called from the CLI via L3LogicalChannel, which locks the L3LogicalChannel, guaranteeing
	// that the MMContext is not freed while we are here.  We need to lock this particular MMContext
	// to avoid changes to mmcTE, but we dont have a private lock in each MMContext, only the global lock,
	// so we lock that, even though it is way overkill.
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// Way overkill, but necessary to lock MMContext.
	tranlist.clear();
	for (unsigned ati = TE_first; ati < TE_num; ati++) {
		if (mmcTE[ati] != NULL) { tranlist.push_back(mmcTE[ati]->tranID()); }
	}
	if (mmcMMU) {
		// TODO
	}
}

RefCntPointer<TranEntry> MMContext::mmGetTran(unsigned ati) const
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// overkill to lock the world but its the lock we have.
	assert(ati < TE_num);
	return mmcTE[ati];
}

// Connect the Transaction to this channel.  Sets pointers in both directions.
// After this, the RefCntPointer in mmcTE takes over the job of deleting the transaction when the last pointer to it disappears.
void MMContext::mmConnectTran(ActiveTranIndex ati, TranEntry *tran)
{
	devassert(gMMLock.lockcnt());		// Caller locked it.
	// When a primary transaction is deleted we may promote the secondary transaction, so keep trying to make sure we delete them all:
	for (unsigned tries = 0; tries < 3; tries++) {
		if (mmcTE[ati] != NULL) {
			LOG(ERR) << "Transaction over-writing existing transaction"
					<<LOGVAR2("old_transaction",*mmcTE[ati])<<LOGVAR2("new_transaction",tran);
			// (pat) This is a bug somewhere.
			mmcTE[ati]->teCancel(TermCause::Local(L3Cause::L3_Internal_Error));
		}
	}
	mmcTE[ati] = tran;			// Takes charge of tran; increments the refcnt
	tran->teSetContext(this);	// And set the back pointer.

}

// Connect the Transaction to this channel.  Sets pointers in both directions.
void MMContext::mmConnectTran(TranEntry *tran)
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// I think this is unnecessary, but be safe.
	ActiveTranIndex txi;
	switch (tran->servicetype()) {
		case L3CMServiceType::MobileTerminatedCall:
		case L3CMServiceType::MobileOriginatedCall:
		case L3CMServiceType::EmergencyCall:
		case L3CMServiceType::HandoverCall:
			txi = TE_CS1;
			break;
		case L3CMServiceType::ShortMessage:							// specifically, MO-SMS
			txi = mmcTE[TE_MOSMS1]!=NULL ? TE_MOSMS2 : TE_MOSMS1;
			break;
		case L3CMServiceType::MobileTerminatedShortMessage:
			txi = TE_MTSMS;
			break;
		case L3CMServiceType::LocationUpdateRequest:
			txi = TE_MM;
			break;

		case L3CMServiceType::SupplementaryService:
			WATCHINFO("connect tran for SS");
			txi = TE_SS;
			break;
		//VoiceCallGroup=9,
		//VoiceBroadcast=10,
		//LocationService=11,		// (pat) See GSM 04.71.  Has nothing to do with MM Location Update.
	default:
		assert(0);
	}
	mmConnectTran(txi,tran);
}


// This is called only from TranEntry which is already in the process of deleting the transaction.
void MMContext::mmDisconnectTran(TranEntry *tran)
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// I think this is unnecessary, but be safe.
	for (unsigned tx = 0; tx < TE_num; tx++) {
		if (mmcTE[tx] == tran) {
			LOG(DEBUG) << "found "<<tran;
			mmcTE[tx].free();
			assert(mmcTE[tx] == 0);
			if (tx == TE_MOSMS1) {
				// Promote the secondary SMS transaction to primary.
				mmcTE[TE_MOSMS1] = mmcTE[TE_MOSMS2];
				mmcTE[TE_MOSMS2] = NULL;
			}
			return;
		}
	}
	// This is pretty bad.
	LOG(ERR) << "Attempt to remove transaction "<<tran->tranID()<<" not found in MMContext";
}


// Does nothing if already unlinked.
void MMContext::mmcUnlink()
{
	devassert(gMMLock.lockcnt());		// Caller locked it.
	MMContext *mmc = this;
	MMUser *mmu = mmc->mmcMMU;
	// Detach MMUser from MMContext:
	if (mmu) {
		assert(mmu->mmuContext == mmc);
		assert(mmc->mmcMMU == mmu);
		mmc->mmcMMU = NULL;		// old comment: Deletes its RefCntPointer and may delete it.
		mmu->mmuContext = NULL;
	}
}


// Move transactions from oldmmc to this, which is the current MMC serving the handset.
// We do this when we have positive knowledge that the oldmmc channel has been abandoned by the handset.
// This happens when the MS was on one MMC and has been moved or showed up on another MMC.
void MMContext::mmcMoveTransactions(MMContext *oldmmc)
{
	for (unsigned ati = TE_first; ati < TE_num; ati++) {
		if (! oldmmc->mmcTE[ati].isNULL()) {
			if (mmcTE[ati].isNULL()) {
				// Disconnect the old tran but be careful not to delete it.
				// To be sure we have to keep a pointer to it through this operation.
				RefCntPointer<TranEntry> oldtran = oldmmc->mmcTE[ati];
				oldmmc->mmcTE[ati] = NULL;
				oldtran->teSetContext(NULL);	// Not necessary, but be tidy.
				mmConnectTran((ActiveTranIndex)ati, oldtran.self());
			} else {
				// (pat) Disaster.  There is a corresponding transaction already running on the new MMC,
				// for example, old voice transaction and new voice transaction.
				// I don't think this is possible for double paging responses (see comments at mmcLink and NewPagingResponseHandler)
				// because we call mmcLink immediately when the second page is received, so the new MMC is empty.
				// I'm not sure about other cases; the logic is too complicated.
				// We will keep the new (more recent) transaction and the old transaction on the
				// old MMC will be dropped when that channel is closed.
				LOG(ERR) << "Handset has changed channels and has transactions running on the both channels.  "
					<<LOGVAR2("old channel",oldmmc->tsChannel()) <<LOGVAR2("new channel",tsChannel())
					<<LOGVAR2("transaction being deleted",oldmmc->mmcTE[ati].self());
				// We dont do anything.  The transaction will be delete when the old channel is closed,
				// which the caller should do immediately.  We could call teCancel here but teCancel is tricky
				// and I would like to reduce the number of calls to it.  This probably doesnt happen anyway.
				// If this does happen, we could be more clever, like a voice transaction could move to the secondary slot, etc.
			}
		}
	}
}

// This is called whenever we have positively identified an MS so we can connect the MMU to the MMC.
// That includes:
// 1. when we receive a PagingResponse message (which includes an IMSI or TMSI; note that PagingResponse
// is an L3 message which means the MS has already negotiated L2 LAPDm connection to send it.)
// 2. MOC call control when we identify the MS
// 3. Mobility Management after authorization.
// 4. From SMS somewhere too.
void MMContext::mmcLink(MMUser *mmu)
{
	devassert(gMMLock.lockcnt());		// Caller locked it.
	//RefCntPointer<MMUser> saveme(mmu);	// Dont delete mmu during this procedure.
	MMContext *mmc = this;
	if (mmc->mmcMMU == mmu) {
		// Already connected.
		devassert(mmu->mmuContext == mmc);	// We always maintain pointers both ways.
		return;
	}
	// Detach the mmu from its existing channel, if any.  That happens when the MS disappeared temporarily
	// and then came back on another channel, for example, handover to another BTS and back.
	// (pat 4-2014) It also happens for paging response: Sometimes the MS sends two RACHes in a row,
	// which allocates two channels (say A and B.)
	// We send two immediate assignments, and the MS may respond to both!  First it does an L1 LAPDm negotiation on A
	// and sends a Paging Response there, which connects its MMU to the MMContext for A, then it
	// does an L2 LAPDm negotiation on B and sends a second Paging Response there, so we get here with this == channel B
	// but with the MMU attached to channel A.  So we must disconnect the existing channel and move the MMU to the new channel.
	// We have to move the transactions from the old MMContext to the new; for example if there was only one MTC transaction and
	// it has already been moved from the MMU to the MMC, then the MMU is empty of transactions which will release channel B
	// immmediately in mmCheckNewActivity.
	if (MMContext *oldmmc = mmu->mmuContext) {
		if (oldmmc != mmc) {
			LOG(DEBUG) <<"reconnecting mmu"<<LOGVAR(mmu)<<LOGVAR(oldmmc)<<LOGVAR(mmc);
			// pat 4-2014: Move the transactions from the old to the new mmc.
			mmcMoveTransactions(oldmmc);
		}
		oldmmc->mmcUnlink();
	}
	mmc->mmcUnlink();
	mmc->mmcMMU = mmu;
	mmu->mmuContext = mmc;
}


void MMContext::mmcFree(TermCause cause)
{
	assert(this->mmcMMU == NULL);
	devassert(gMMLock.lockcnt());		// Caller locked it.

	// Cancel all the enclosed transactions and their dialogs.
	for (unsigned i = 0; i < TE_num; i++) {
		// When a primary transaction is deleted we may promote the secondary transaction, so delete them all:
		for (unsigned tries = 0; tries < 3; tries++) {
			if (mmcTE[i] != NULL) { mmcTE[i]->teCancel(cause); }	// Removes the transaction from mmcTE via mmDisconnectTran
		}
		assert(mmcTE[i].self() == NULL);	// teCancel removed it.
	}
	LOG(DEBUG)<<"MMContext DELETE "<<(void*)this;
	delete this;
}

// The logical channel no longer points to this Context, so release it.
// The cause is used only for reporting purposes for any transactions still extent;
// the underlying channel has already been released so we cannot send a cause code downstream,
// and if there are any new SIP dialogs upstream we should normally start re-paging the handset
// to create a new mmcontext rather than cancelling them.
// TODO: We may want to cancel any SIP dialogs based on the cause.
void MMLayer::mmFreeContext(MMContext *mmc,TermCause cause)
{
	// There can be multiple logical channels pointing to the same Context, so decrement
	// the channel use count and delete only when 0.
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	LOG(DEBUG) << mmc;
	if (--mmc->mmcChannelUseCnt > 0) return;

	MMUser *mmu = mmc->mmcMMU;
	mmc->mmcUnlink();	// resets mmcMMU

	// This channel was closed normally, or because of channel loss (for example, reassign failure) or an internal error.
	// In the latter cases there could be running SipDialogs on the channel and we have to tell them something.
	// The SipCode 408 allows the peer to retry immediately.

	// Paul at Null Team says:
	// 408 is reserved for SIP protocol timeouts (no answer to SIP message)
	// 504 indicates some other timeout beyond SIP (interworking)
	// 480 indicates some temporary form of resource unavailability or congestion but resource is accessible and can be checked
	// 503 indicates the service is unavailable but does not imply for how long
	//SipCode sipcode(480,"Temporarily Unavailable");

	if (mmu) {
		// FIXME It is possible for new SIP dialogs to have started between the time we decided
		// to close this channel and now.  It is also possible that we closed the channel
		// because of loss of contact with the MS.  In either case, if the MMU has dialogs,
		// dont delete it - just leave it alone and we will start repaging this MS again.
		// TODO: But first, walk though dialogs and cancel any that need it.
		if (mmu->mmuIsEmpty()) {
			mmu->mmuFree(NULL,TermCause::Local(L3Cause::No_Transaction_Expected));	// TermCause is not used because there are no dialogs.
		}
	}
	mmc->mmcFree(cause);
}

void MMLayer::mmMTRepage(const string imsi)
{
	// Renew the page timer.
	LOG(DEBUG) <<LOGVAR(imsi);
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	MMUser *mmu = mmFindByImsi(imsi,false);
	if (mmu) {
		// This has no effect unless we are paging, ie, if the MMUser has not yet connected to an MMChannel.
		mmu->mmuPageTimer.future(gConfig.GSM.Timer.T3113);
	} else {
		LOG(DEBUG) << "repeated INVITE/MESSAGE with no MMUser record";
	}
}


// Called when we have positively identified the MS associated with chan, so now we
// want to connect the channel to its MMUser.
void MMLayer::mmAttachByImsi(L3LogicalChannel *chan, string imsi)
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	// Find or create the MMUser.
	WATCHINFO("attachMMC" <<LOGVAR(imsi) <<LOGVAR(chan));
	MMUser *mmu = mmFindByImsi(imsi,true);
	MMContext *mmc = chan->chanGetContext(true);
	// They are linked together from now on.
	mmc->mmcLink(mmu);
	LOG(DEBUG);

	// TODO: The MM procedure may have blocked tmsis and imsis.
	// So now we want to unblock any previously blocked imsi/tmsi
}


bool MMLayer::mmTerminateByImsi(string imsi)
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	MMUser *mmu = mmFindByImsi(imsi,false);
	if (!mmu) { return false; }	// Not found.
	MMContext* mmc = mmu->mmuContext;
	if (!mmc) {
		// There is no channel, just kill off the MMUser, which will stop paging and cancel the SIP dialogs.
		mmu->mmuFree(NULL,TermCause::Local(L3Cause::Operator_Intervention));
		return true;
	}
	if (mmc->tsChannel()->chanRunning()) {
		// Dont call chanClose from here because it sends a message which would block the calling thread.
		//mmc->tsChannel()->chanClose(L3RRCause::PreemptiveRelease,RELEASE);  DONT DO THIS!
		// FIXME: We would like to send an RR Release message first.
		mmc->mmcTerminationRequested = true;
	}
	return true;
}

// This is the way MMUsers are created from the SIP side.
void MMLayer::mmAddMT(TranEntry *tran)
{
	LOG(DEBUG) <<this<<LOGVAR(tran);
	{	ScopedLock lock(gMMLock,__FILE__,__LINE__);
		string imsi(tran->subscriberIMSI());
		MMUser *mmu = mmFindByImsi(imsi,true);
		// Is there a guaranteed tmsi?
		// We will delay this until we page in case an LUR is occurring right now.
		//if (uint32_t tmsi = gTMSITable.tmsiTabGetTMSI(imsi,true)) { mmu->mmuTmsi = /*tran->subscriber().mTmsi =*/ tmsi; }
		assert(mmu);
		mmu->mmuAddMT(tran);
	}
	mmPageSignal.signal();
}

MMUser *MMLayer::mmFindByImsi(string imsi,	// Do not change this to a reference.  We need a copy of the string
		// to insert into the map.  If pass by reference here the map points to the string from the caller,
		// which may have long since gone out of scope.  What a great language.
	bool create)
{
	LOG(DEBUG) <<LOGVAR(imsi) <<LOGVAR(create);
	devassert(gMMLock.lockcnt());		// Caller locked it.
	MMUser *result;
	const char *what = "existing ";
	if (create) {
		// Use insert so we only traverse the map tree once.  If element does not exist, a pair with MMUser*==NULL is inserted.
		// This wonderful insert method returns a pair<MMUserMap::iterator,bool> but we only want the iterator.
		//pair<MMUserMap::iterator,bool> result = MMUsers.insert(pair<string,MMUser*>(imsi,(MMUser*)NULL));
		bool exists = MMUsers.find(imsi) != MMUsers.end();
		LOG(DEBUG) << LOGVAR(imsi)<<LOGVAR(exists);
		MMUserMap::iterator it = MMUsers.insert(pair<string,MMUser*>(imsi,(MMUser*)NULL)).first;
		result = it->second;
		if (result == NULL) {
			result = it->second = new MMUser(imsi);
			LOG(DEBUG) << "inserting new MMUser "<<(void*)result;
			what = "new ";
		}
		LOG(DEBUG) << "MMUsers["<<imsi<<"]="<<(void*)MMUsers[imsi];
	} else {
		MMUserMap::const_iterator it = MMUsers.find(imsi);
		result = (it == MMUsers.end()) ? NULL : it->second;
	}
	LOG(DEBUG) <<what <<LOGVAR(result);
	return result;
}


TMSI_t MMUser::mmuGetTmsi()
{
	if (! this->mmuDidTmsiCheck) {
		this->mmuDidTmsiCheck = true;
		if (uint32_t tmsi = gTMSITable.tmsiTabGetTMSI(mmuImsi,true)) { this->mmuTmsi = tmsi; }
	}
	return this->mmuTmsi;
}

MMUser *MMLayer::mmFindByTmsi(uint32_t tmsi)
{
	devassert(gMMLock.lockcnt());		// Caller locked it.
	//ScopedLock lock(gMMLock,__FILE__,__LINE__);
	MMUser *result = NULL;
	for (MMUserMap::iterator it = MMUsers.begin(); it != MMUsers.end(); ++it) {
		MMUser *mmu = it->second;
		// (pat) The handset we want could be simultaneously doing an MM procedure that is establishing a TMSI,
		// so we should check the tmsi table every single time this happens.
		// However, we are currently doing an expensive sql lookup so only check once.
		TMSI_t mmutmsi = mmu->mmuGetTmsi();
		if (mmutmsi.valid() && mmutmsi.value() == tmsi) { result = mmu; break; }
	}
	LOG(DEBUG) << LOGVAR(result);
	return result;
}

MMUser *MMLayer::mmFindByMobileId(L3MobileIdentity&mid)
{
	devassert(gMMLock.lockcnt());		// Caller locked it.
	if (mid.isIMSI()) {
		string imsi = mid.digits();
		return mmFindByImsi(imsi,false);
	} else {
		assert(mid.isTMSI());
		return mmFindByTmsi(mid.TMSI());
	}
}

// When called from the paging thread loop this function is responsible for noticing expired MMUsers and deleting them.
void MMLayer::mmGetPages(NewPagingList_t &pages)
{

	assert(pages.size() == 0);	// Caller passes us a new list each time.

	{
		ScopedLock lock(gMMLock,__FILE__,__LINE__);
		LOG(DEBUG) <<"before "<<LOGVAR(MMUsers.size());
		pages.reserve(MMUsers.size());
		for (MMUserMap::iterator it = MMUsers.begin(); it != MMUsers.end(); ) {
			MMUser *mmu = it->second;
			MMUserMap::iterator thisone = it++;
			LOG(DEBUG)<<LOGVAR(mmu);
			if (mmu->mmuIsAttached()) { // Is it already attached to a radio channel?
				LOG(DEBUG) << "MMUser already attached:"<<mmu->mmuImsi;
				continue;
			}
			if (mmu->mmuPageTimer.passed()) {
				// Expired.  Get rid of it.
				LOG(INFO) << "Page expired for imsi="<<mmu->mmuImsi;
				// Erasing from a map invalidates the iterator, but not the iteration.
				// (pat) The SIP error for no page should probably not be 480 Temporarily Unavailable,
				// because that implies we know that the user is at the BTS, but if it did not answer the page, we do not.
				// Paul at Null Team recommended 504.
				// FIXME URGENTLY:  Dont do an mmFree within the gMMLock, although we need to make sure it does not disappear.
				//mmu->mmuFree(&thisone,TermCause::Local(L3Cause::No_Paging_Response));
				// (pat 10-10-2014) Stop passing the iterator, supersitiously.
				mmu->mmuFree(NULL,TermCause::Local(L3Cause::No_Paging_Response));
				continue;
			}
			// TODO: We could add a check for a "provisional IMSI" 

			NewPagingEntry tmp(mmu->mmuGetInitialChanType(), mmu->mmuImsi);
			pages.push_back(tmp);
		}
		LOG(DEBUG) <<"after "<<LOGVAR(MMUsers.size());
	}
	if (pages.size()) LOG(DEBUG) <<LOGVAR(pages.size());
}

// Not used.  This is only documentation how to do this now.
//void MMLayer::mmWaitForPages(NewPagingList_t &pages, bool wait)
//{
//	ScopedLock lock(gMMLock,__FILE__,__LINE__);
//	while (1) {
//		// Code goes here.
//		// ...
//		//
//		if (!wait) { return; }
//		// We need to provide a timeout so that we free expired pages in a timely manner; if we dont
//		// then that MMUser is essentially locked because incoming SIP invites get a busy return, and it
//		// wont get released until some other unrelated page intervenes.
//		mmPageSignal.wait(gMMLock,500);
//		// Need a while loop here because the wait does not guarantee it was signalled.
//	}
//}

// For use by the CLI: create a copy of the paging list and print it.
void MMLayer::printPages(ostream &os)
{
	// This does not need to lock anything.  The mmGetPages provides locked access to the MMUser list.
	NewPagingList_t pages;
	gMMLayer.mmGetPages(pages);
	for (NewPagingList_t::iterator it = pages.begin(); it != pages.end(); ++it) {
		NewPagingEntry &pe = *it;
		os <<pe.text();
	}
}

// Connect the channel with its MMUser based on the received page.
// Return true if ok, or false if not found, which will tell caller to release the channel.
// Could be not found because the SIP side gave up while we were waiting for the page.
bool MMLayer::mmPageReceived(MMContext *mmchan, L3MobileIdentity &mobileId)
{
	// The MMC can be deleted independently until it is tied to a channel.
	MMUser *mmu;
	{	ScopedLock lock(gMMLock,__FILE__,__LINE__);
		mmu = gMMLayer.mmFindByMobileId(mobileId);
		if (mmu == NULL) {
			return false;
		}

		// We do not 'delete' pages - only unattached MMUser are paged,
		// so the act of attaching the MMUser to a MMContext (below) causes us to stop paging this MS.

		// This is what ties the MMUser and MMContext together for MT services.
		// They are linked together from now on.
		//mmchan = mmchan->chanGetContext(true);
		mmchan->mmcLink(mmu);
		// At this point the MMC cannot be deleted until the L3LogicalChannel is released,
		// so we no longer need the MM lock, and we should release it before locking
		// the MMUser to avoid deadlock.
	}

	// TODO: If there is a channel lock, this might want to use that.
	LOG(INFO) << "paging reponse for " << mmu;
	mmchan->mmcPageReceived();	// Just prints errors.
	return true;
}

// If unattached flag, print only unattached Contexts, used from CLI.
void MMLayer::printMMUsers(std::ostream&os, bool onlyUnattached)
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	for (MMUserMap::iterator it = MMUsers.begin(); it != MMUsers.end(); ++it) {
		MMUser *mmu = it->second;
		if (onlyUnattached && mmu->mmuIsAttached()) { continue; }
		mmu->mmuText(os);
		os << endl;
	}
}

void MMLayer::printMMInfo(std::ostream&os)
{
	L2ChanList chans;
	gBTS.getChanVector(chans);
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	for (L2ChanList::iterator it = chans.begin(); it != chans.end(); it++) {
		L3LogicalChannel *chan = dynamic_cast<L3LogicalChannel*>(*it);
		// (pat) When we used a separate mChanLock in the L3LogicalChannel, then deadlock was possible here.
		// chanGetContext calls mChanLock, but there could be some
		// other thread waiting in a L3LogicalChannel method with mChanLock already locked
		// and waiting for the gMMLock, which is locked above.
		// I saw this deadlock when two channel assignments happened simultaneously, and printChansV4
		// and printMMInfo tried to run simultaneously.
		MMContext *mmc = chan->chanGetContext(false);
		if (mmc) {
			mmc->mmcText(os);
			os << endl;
		}
	}
	printMMUsers(os,false);
}

string MMLayer::printMMInfo()
{
	ostringstream ss;
	printMMInfo(ss);
	return ss.str();
}

void controlInit()
{
	LOG(DEBUG);
	gTMSITable.tmsiTabOpen(gConfig.getStr("Control.Reporting.TMSITable").c_str());
	LOG(DEBUG);
	gNewTransactionTable.ttInit();
	LOG(DEBUG);
	TranInit();
}

};
