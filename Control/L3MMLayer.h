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

#ifndef _L3MMLAYER_H
#define _L3MMLAYER_H 1

#include <Logger.h>
#include <Interthread.h>
#include <Timeval.h>


#include <GSMTransfer.h>
#include "ControlCommon.h"
#include "PagingEntry.h"
#include "L3TranEntry.h"		// Needed because InterthreadQueue deletes its elements on destruction.
//#include "RadioResource.h"		// For Paging
#include "L3Utils.h"
#include <SIPDialog.h>

namespace Control {
class MMLayer;
class MMContext;
typedef std::map<string,MMUser*> MMUserMap;
using namespace GSM;


#if 0
// A list of pointers with methods designed for pointers that return NULL on error.
template<class T>
class PtrList : public std::list<T*> {
	//typedef typename std::list<T>::iterator itr_t;
	//typedef typename std::list<T> type_t;
	public:
	T* pop_front_ptr() {
		if (this->empty()) { return NULL; }
		T* result = this->front();
		this->pop_front();
		return result;
	}
};
#endif



// This is the per-IMSI data for a subscriber, and a data-cache for data whose primary storage is persistent.
// It is now persistent beyond the life of a single transaction call to save RRLP status.
// Anything that needs to be persistent across reboots or shared via handover needs to be backed up
// to the external TMSI table or to the external subscriber registry.
// TODO: We should check the TMSI table when we create the MMUser
DEFINE_MEMORY_LEAK_DETECTOR_CLASS(MMUser,MemCheckMMUser)
class MMUser : public MemCheckMMUser /*: public RefCntBase*/ {
	// This is lock is to add/remove from the Transaction queues, since they are written from a thread in the SIP directory
	// and read from the thread running the LogicalChannel.
	friend class MMContext;
	mutable Mutex mmuLock;
	Timeval mmuPageTimer;
	protected:
	typedef PtrList<TranEntry> MMUQueue_t;
	MMUQueue_t mmuMTCq;
	MMUQueue_t mmuMTSMSq;
	MMUQueue_t mmuMTSSq;
	friend class MMLayer;
	MMState mmuState;
	MMContext* mmuContext;

	protected: string mmuImsi;		// Just the imsi, without "IMSI"
	public: 	string mmuGetImsi(bool verbose) { return mmuImsi.empty() ? (verbose ? "no-imsi" : "") : mmuImsi; }

	protected:	TMSI_t mmuTmsi;
				Bool_z mmuDidTmsiCheck;	// Have we looked up the IMSI in the TMSI table yet?
	public: 	TMSI_t mmuGetTmsi();

	protected:
	void mmuFree(MMUserMap::iterator *it,TermCause cause /*= TermCauseUnknown*/);	// This is the destructor.  It is not public.  Can only delete from gMMLayer because we must lock the universe first.

	GSM::ChannelType mmuGetInitialChanType() const;

	void MMUserInit() { mmuState = MMStateUndefined; mmuContext = NULL; }
	public:
	MMUser(string& wImsi);
	//MMUser(string& wImsi, TMSI_t wTmsi);

	void mmuAddMT(TranEntry *tran);
	//void mmuPageReceived(L3LogicalChannel *chan);
	//void mmuClose();		// TODO
	bool mmuIsAttached() { return mmuContext != NULL; }	// Are we attached to a radio channel?
	bool mmuIsEmpty();
	void mmuCleanupDialogs();	// Let go of any dead dialogs
	//void mmuCallFinished(L3LogicalChannel *chan,MMCause cause);
	bool mmuServiceMTQueues();
	void mmuText(std::ostream&os) const;
	string mmuText() const;
};
std::ostream& operator<<(std::ostream& os, const MMUser&mmu);
std::ostream& operator<<(std::ostream& os, const MMUser*mmu);

typedef std::vector< RefCntPointer<TranEntry> > TranEntryVector;

// This is the set of actively runnning TranEntrys on an L3LogicalChannel.
// TODO: The MM operations should run directly on the MMContext, not in a TranEntry.
DEFINE_MEMORY_LEAK_DETECTOR_CLASS(MMContext,MemCheckMMContext)
class MMContext : public MemCheckMMContext /*: public RefCntBase*/ {
	friend class MMLayer;
	friend class MMUser;
	private:
	//mutable Mutex mmcLock;	// mostly unused
	int mmcChannelUseCnt;
	L3LogicalChannel *mmcChan;
	//RefCntPointer<MMUser> mmcMMU;
	MMUser* mmcMMU;
	//L3Timer TChReassignment;

	void mmcMoveTransactions(MMContext *oldmmc);
	protected:
	void mmcUnlink();
	void mmcLink(MMUser *mmu);
	time_t mmcOpenTime;

	// These are the Transactions/Procedures that may be active simultaneously:
	public:
	Bool_z mmcTerminationRequested;
	NeighborPenalty mmcHandoverPenalty;
	void chanSetHandoverPenalty(NeighborPenalty &wPenalty) { mmcHandoverPenalty = wPenalty; }



	enum ActiveTranIndex {
		TE_first = 0,		// Start of table.
		TE_MM = 0,			// One MM Procedure.
		TE_CS1,			// Primary CS Transaction.
		TE_CSHold,		// CS transaction on hold.
		// Dont reorder these without checking for 'for' loops in L3MMLayer.cpp
		TE_MOSMS1,		// The primary MO-SMS.
		TE_MOSMS2,		// The follow-on MO-SMS.
		TE_MTSMS,		// Only one MT-SMS allowed at a time.
		TE_SS,			// Dedicated supplementary services transaction.
		TE_num			// Not a Transaction; The max number of entries in this table.
	};
	RefCntPointer<TranEntry> mmcTE[TE_num];
	unsigned mNextTI;
	InterthreadQueue<const L3Message> mmcServiceRequests;	// Incoming CM Service Request messages.
	void startSMSTran(TranEntry *tran);
	void startSSTran(TranEntry *tran);

	void MMContextInit();
	void mmcFree(TermCause cause);	// This is the destructor.  It is not public.  Can only delete from gMMLayer because we must lock the MMUserMap first.

	public:
	MMContext(L3LogicalChannel *chan);
	bool mmInMobilityManagement();	// Is a mobility management procedure running?
	//void mmClose();
	L3LogicalChannel *tsChannel() { return mmcChan; }
	MMContext *tsDup();
	void mmcPageReceived() const;
	time_t mmcDuration() const { return time(NULL) - mmcOpenTime; }

	RefCntPointer<TranEntry> mmGetTran(unsigned ati) const;
	void mmGetTranList( TranEntryVector &tranlist);
	void mmConnectTran(ActiveTranIndex ati, TranEntry *tran);
	void mmConnectTran(TranEntry *tran);
	void mmDisconnectTran(TranEntry *tran);

	unsigned mmGetNextTI();
	void getTranIds(TranEntryList &tranlist) const;
	// By returning a RefCntPointer we prevent destruction of the transaction during use by caller.
	RefCntPointer<TranEntry> tsGetVoiceTran() const { return mmcTE[TE_CS1]; }
	//void tsSetVoiceTran(TranEntry*tran) { mmcTE[TE_CS1] = tran; }
	void mmSetChannel(L3LogicalChannel *wChan) { mmcChan = wChan; }
	string mmGetImsi(bool verbose);		// If the IMSI is known, return it, else ""

	bool mmIsEmpty();
	bool mmCheckNewActivity();	// Check for new activity.  Return true if any found. Also checks for normal channel release.
	bool mmCheckSipMsgs();		// Return true if anything happened.
	bool mmCheckTimers();		// Return true if anything happened.
	RefCntPointer<TranEntry> findTran(const L3Frame *frame, const L3Message *l3msg) const;
	bool mmDispatchL3Frame(const L3Frame *frame, const L3Message *msg);
	void l3sendm(const GSM::L3Message& msg, const GSM::Primitive& prim=GSM::L3_DATA, SAPI_t SAPI=SAPI0);
	void mmcText(std::ostream&os) const;
};
std::ostream& operator<<(std::ostream& os, const MMContext&mmc);
std::ostream& operator<<(std::ostream& os, const MMContext*mmc);

// Maps imsi to MMUser.  No imsi, no MMUser.

extern Mutex gMMLock;
class MMLayer {
	friend class MMUser;
	// Locking rules:
	// The thread running the L3LogicalChannel service loop "owns" the MMContext on its channel,
	// so it is allowed to manipulate it without locking this global Mutex.
	// Once the MMUser is 'attached' it cannot be deleted except by the the L3LogicalChannel thread,
	// so the MMUser can also be used by that thread without fear of destruction during use.
	// But MMUsers are created by an external thread (specifically, from SIPInterface) and may
	// be destroyed as a result of expired pages, so this global Mutex is used to 
	// to add/remove MMUsers, to search for MMUsers, paging,
	// to connect/disconnect an MMUser with/from a MMContext.
	// It is also used to create/delete MMContexts which is probably unnecessary.
	// The MMUser::Mutex is used only to protect the queues inside MMUser,
	// used to add/remove transactions to those queues.
	//Mutex gMMLock;
	Signal mmPageSignal;						///< signal to wake the paging loop
	MMUserMap MMUsers;
	public:
	void mmGetPages(NewPagingList_t &pages);
	void printPages(std::ostream &os);
	bool mmPageReceived(MMContext *mmchan, L3MobileIdentity &mobileId);
	// Add a new MT transaction, and signal the pager to come notice it.
	void mmAddMT(TranEntry *tran);
	void mmFreeContext(MMContext *mmc,TermCause cause);
	// This is called when the MT SIP engine on the other side has sent us another message.
	// (pat) We could be blocked for several reasons, including paging, waiting for LUR to complete, waiting for channel to change, etc.
	// But if we are paging, reset the paging timer so we keep paging.
	void mmMTRepage(const string imsi);	// Reset the paging timer so we continue paging.
	void mmAttachByImsi(L3LogicalChannel *chan, string imsi);
	bool mmTerminateByImsi(string imsi);
	//bool mmStartMTDialog(SIP::SipDialog*dialog, SIP::SipMessage*invite);
	MMUser *mmFindByImsi(string imsi, bool create=false);
	MMUser *mmFindByTmsi(uint32_t tmsi);
	MMUser *mmFindByMobileId(L3MobileIdentity&mid);
	void printMMUsers(std::ostream&os, bool onlyUnattached);
	void printMMInfo(std::ostream&os);
	string mmGetNeighborTextByImsi(string imsi, bool full);
	string printMMInfo();

	// Is the single MTC slot busy?
	bool mmIsBusy(string &imsi) {
		ScopedLock lock(gMMLock,__FILE__,__LINE__);
		MMUser *mmu = mmFindByImsi(imsi,false);
		LOG(DEBUG) <<LOGVAR(imsi)<<LOGVAR(mmu);
		if (!mmu) return false;
		if (mmu->mmuMTCq.size()) return true;	// Someone already waiting in the MTC queue.
		if (!mmu->mmuContext) return false;
		LOG(DEBUG) <<"mmc="<<mmu->mmuContext;
		return mmu->mmuContext->tsGetVoiceTran() != NULL;
	}

	RefCntPointer<TranEntry> mmFindVoiceTranByImsi(string &imsi) {
		ScopedLock lock(gMMLock,__FILE__,__LINE__);
		LOG(DEBUG);
		MMUser *mmu = mmFindByImsi(imsi,false);
		LOG(DEBUG) <<mmu;
		if (!mmu) return NULL;
		MMContext *mmc = mmu->mmuContext;
		LOG(DEBUG) <<mmc;
		if (!mmc) return NULL;
		LOG(DEBUG) << mmc->tsGetVoiceTran().self();
		return mmc->tsGetVoiceTran();
	}
};

extern MMLayer gMMLayer;

};	// namespace Control

#endif
