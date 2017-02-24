/**@file Declarations for TransactionTable and related classes. */
/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012, 2014 Range Networks, Inc.
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



#ifndef L3TRANSACTIONTABLE_H
#define L3TRANSACTIONTABLE_H


#include <stdio.h>
#include <list>

#include <Logger.h>
#include <Interthread.h>
#include <Timeval.h>
#include <Sockets.h>


#include <GSML3CommonElements.h>
#include <GSML3MMElements.h>
#include <GSML3CCElements.h>
#include <GSML3RRElements.h>
#include <SIPExport.h>
//#include <SIPBase.h>
#include "ControlTransfer.h"
#include "L3StateMachine.h"
#include "L3MobilityManagement.h"
#include "L3Utils.h"

extern int gCountTranEntry;

struct sqlite3;


/**@namespace Control This namepace is for use by the control layer. */
namespace Control {
class CSL3StateMachine;
class MachineBase;
class MMContext;

typedef std::map<std::string, GSM::Z100Timer> TimerTable;		// Not used for l3rewrite



// During handover from BS1 to BS2, BS1 sends a message and BS2 stores the info here.
class HandoverEntry {
	protected:
	friend class TranEntry;
	friend class NewTransactionTable;

	TranEntryId mMyTranID;	// Back pointer to TranEntry that owns this, only we use the ID not a pointer.
	public: TranEntryId tranID() const { return mMyTranID; }
	protected:
	unsigned mHandoverOtherBSTransactionID;

	public:
	struct ::sockaddr_in mInboundPeer;		///< other BTS in inbound handover
	// The inboundReference is generated in BS2 and placed in the L3HandoverCommand
	// and sent to L1FEC to be recognized as the horeference, and is not further needed,
	// but here it is anyway:
	unsigned mInboundReference;			///< handover reference.

	string mSipReferStr;
	string mHexEncodedL3HandoverCommand;

	struct ::sockaddr_in mOutboundPeer;

	protected:
	void initHandoverEntry(
		const struct sockaddr_in* peer,
		unsigned wHandoverReference,
		unsigned wHandoverOtherBSTransactionID,
		SimpleKeyValue &params);

	public:
	HandoverEntry(const TranEntry*tran);
};

// Comments by pat:
// === Prior to l3write: ===
// The various Procedures (MTC, MOC, etc) were handled by functions, and the fact that
// a procedure was underway was implicit in the fact that a handler function (eg MOCController) was being run.
// The TransactionEntry creation was delayed until the IMSI was known.
// All the DCCHDispatch code is only involved in the initial message starting an L3 Procedure - after that the handler functions poll.
// The TransactionEntry was not involved in message delivery except for the responses to ImmediateAssignment, paging, and handover.
// The LocationUpdating procedure did not use a TransactionEntry at all.
// The TI [Transaction Identifiers] were simply allocated round robin, which is incorrect but at least
// reduces the risk of accidently using an active one with an MS.

// === Changes with l3rewrite: ===
// The TranEntry is always associated with a radio-link to an MS or UE, represented by an L3LogicalChannel.
// I repeat: If we have a link to a phone, we have one or more TranEntrys, which is the defining characteristic of TranEntry.
// In GSM the L3LogicalChannel is identified by association with an L2LogicalChannel (SDCCH or TCH.)
// In UMTS the L3LogicalChannel is identified by a UNRTI identity.
// (fyi, in UMTS many UE share the physical radio channel and the URNTI is used to tell them apart;
// the URNTI is assigned by the BTS to the UE the very first thing, before the UE identity is known.)
// A TranEntry is always created for each L3 procedure, which is a series of L3 messages with the phone,
// including LUR [Location Update Request.]
// In 3GPP parlance an "L3 Transaction" involves a CC or SMS transaction with an associated TI [Transaction Identifier]".
// Our TranEntry may or may not represent an "L3 Transaction" in this sense, for example, the LUR case is not an "L3 Transaction",
// and we also allocate TranEntrys for the MO case as soon as we have a channel, which is before the TI is allocated.
// A TranEntry is not allocated a TI until it is started on the MS, so queued MT transactions do not yet have a TI.
// There may be multiple TranEntrys per L3LogicalChannel.
// Specifically, the spec allows four simultaneous active L3 Transactions on the phone:
// two CS [Circuit Switched, ie voice call] Transactions (the active one and the "on-hold" one) plus one SMS transaction in each direction.
// Additional MT CS or SMS transactions are queued in the MM Context until they can be started.
// The TranEntry may optionally be associated with a SipDialog, which is created only if/when the SIP Dialog is created.
// Specifically, BTS originated SMS transactions never have a SIP Dialog, and the LUR only uses a SipDialog briefly for registration purposes.

// === Mobility Management ===
// The MM Context is used to group all the transactions occuring for a single MS, identified by IMSI,
// to support and coordinate simultaneous Transactions.
// The specs do not clearly articulate the massive differences in MobilityManagement between MT and MO transactions.
// For MT transactions, the transaction identity is by IMSI, even if a TMSI is used for communication with the phone.
// The incoming MT Transactions are queued in an MM Context until an "MM Connection"
// is established with the phone, which just means that we have a radio-link with an established IMSI identity.
// For MO transactions, including LUR, the MS identity is not initially known and the TranEntry is identified only by the channel.
// (This is true for GSM, UMTS, and even GPRS.)
// Rarely, the MS may identify itself immediately by IMSI, but usually we must run an identification and/or authorization process.
// At some point we will learn the MS identity (IMSI), at which time we can attach it to its IMSI-based MM Context, and may initiate
// transactions that are queued in the MM Context.  (Except for MO-SOS calls, which may never have an IMSI identity,
// which means they may never link to an MM Context.)
// The MM Context is responsible for deciding what channel to use for a transaction, specifically, if there is already
// a channel to an MS then a queued MT transaction will try to start on that channel, otherwise page the MS.
// When a CS connection ends, if there are queued SMS transactions we are supposed to move the MS from TCH to SDCCH.
// When a radio-link is lost, the MM Context must be notified so it can clean up the SIP side of all queued MT transactions.
// NOTE: 3GPP 04.11 2.2 indicates that SMS messages use the following channels:
//		TCH not allocated : use SDCCH
//		TCH not allocated -> TCH allocated : SDCCH -> SACCH
//		TCH allocated : SACCH
//		TCH allocated -> TCH not allocated : SACCH -> SACCH opt. SDCCH3
// I believe this means when we start a call we have to recognize the new SACCH channel for the existing SMS procedures.
// It looks like SMS over GPRS uses separate transport with different messages.

// There are weird cases related to TMSIs, because in the MO case the TMSI->IMSI mapping, even if saved in the TMSI table or received from the VLR,
// is not known authoritatively until we have authenticated the phone.  While we are doing any MM procedure with an MS identified by TMSI,
// any MT transactions queued in the MM Context for the IMSI associated with the TMSI must be blocked until the TMSI is resolved
// one way or the other.
// TMSI collisions are possible.  For example, multiple MS could answer a page by TMSI; we could process them serially
// or conceivably we could run authorization procedures on both of them simultaneously.
// An MS that is initially identified by TMSI and subsequently fails authentication is supposed to stop using that TMSI,
// allowing other MS in the cell with the same TMSI to try answering the page.
// Reasons for TMSI collisions include reboot of the TMSI allocation authority (the VLR or SubscriberRegistry),
// or because phones roaming in have established TMSIs from a previously visited VLR with the same PLMN+LAC.
// The MS remembers its assigned TMSI *forever* in the SIM card until it is reassigned
// (or over-written by running out of memory slots to remember TMSIs as a result of visiting too many different PLMNs),
// so even in a perfect world it would still be impossible to avoid TMSI collisions.

// === State Machines ===
// Each active TranEntry is running a single L3Procedure state machine.
// L3Procedures may call others, so there is a stack of L3Procedures in the TranEntry, but only the top one is active.
// Almost all message delivery (excluding the initial response to paging and ImmediateAssignment) is done by
// funneling all messages through a dispatcher (CSL3StateMachine) which assocates the message with a TranEntry,
// and then invokes the current L3Procedure that is pointed to by that TranEntry. 
// For GSM:
// The uplink messages are associated with the TranEntry by arriving on a L3LogicalChannel dedicated to that MS.
// This works because each L3LogicalChannel (SDCCH, SACCH, FACCH) is dedicated to a single MS.
// The same MS may have both a SACCH and either a FACCH or DCCH; these messages are steered to the same TransactionEntries.
// Note that there does not appear to be anything preventing an MS from allocating multiple TCH+FACCH or SDCCH,
// but we dont worry about that.  I believe the current code would even work as long as the MS used the same
// LogicalChannels for all the messages associated with a particular Procedure, but we dont currently bother to check
// the IMSI to try to match up possible multiple logical channels.
// Multiple TranEntrys on the same MS are differentiated by the TI [Transaction Identifier], which was formerly just ignored.

// Destruction:  Formerly TransactionEntrys were kept around a long time partly because they were also the parent of SIP session.
// Now when the radio-link to the MS is released, all TransactionEntries can be released immediately.
// The MM Context must also be deleted immediately - it is not designed to be a permanent TMSI-IMSI mapping repository.
// SIP Dialog destruction is handled separately in the SIP directory by timer.
// There are timers on the underlying L2LogicalChannel that prevent channel reuse which prevents stray messages
// from reaching L3 after a channel release.

// These variables may not be modified even by TranEntry except via the accessor methods.
class TranEntryProtected
{
	// No one may set mGSMState directly, call setGSMState
	CallState mGSMState;				///< the GSM/ISDN/Q.931 call state  (pat)  No it is not; the enum has been contaminated.
	Timeval mStateTimer;					///< timestamp of last state change.

	public:
	CallState getGSMState() const; 			// (pat) This is the old call state and will eventually just go away.
	void setGSMState(CallState wState);
	bool isStuckOrRemoved() const;


	unsigned stateAge() const { /*ScopedLock lock(mLock);*/ return mStateTimer.elapsed(); }
	
	/** Return true if clearing is in progress in the GSM side. */
	bool clearingGSM() const;
	void stateText(ostream &os) const;

	virtual TranEntryId tranID() const = 0;	// This is our private transaction id, not the layer 3 TI mL3TI
	TranEntryProtected() : mGSMState(CCState::NullState) { mStateTimer.now(); }
	// (pat 6-2014) It looks like Dave added this.  It would be needed to preserve the state timer
	// if the TranEntry were ever copied.  Is it?  This may have been part of the staletranentry that was deleted.
	TranEntryProtected(TranEntryProtected &old)
	{
		mGSMState = old.mGSMState;
		mStateTimer = old.mStateTimer;
	}
};

// pat added 6-2014.
// Call Data Record, created from a TranEntry.
// The information we dont know is left empty.
struct L3CDR {
	string cdrType;	// MOC, MTC, Emergency.
	TranEntryId cdrTid;
	string cdrToImsi, cdrFromImsi;
	string cdrToNumber, cdrFromNumber;
	string cdrPeer;
	time_t cdrConnectTime;
	long cdrDuration;	// Connect duration, as distinct from total duration.
	int cdrMessageSize;	// For SMS
	string cdrToHandover, cdrFromHandover;
	TermCause cdrCause;

	static void cdrWriteHeader(FILE *pf);
	void cdrWriteEntry(FILE *pf);
};

// pat added 6-2014.
// This is sent L3CDR records.  It writes them to a file.
class CdrService {
	Mutex cdrLock;
	InterthreadQueue<L3CDR> mCdrQueue;
	Thread cdrServiceThread;
	FILE *mpf;
	int cdrCurrentDay;
	Bool_z cdrServiceRunning;
	public:
	CdrService() : mpf(NULL), cdrCurrentDay(-1) {}
	void cdrServiceStart();
	void cdrOpenFile();
	static void *cdrServiceLoop(void*);
	void cdrAdd(L3CDR*cdrp) { mCdrQueue.write(cdrp); }
};
extern CdrService gCdrService;

DEFINE_MEMORY_LEAK_DETECTOR_CLASS(TranEntry,MemCheckTranEntry)
class TranEntry : public MemCheckTranEntry, public RefCntBase, public TranEntryProtected, public L3TimerList
{
	friend class NewTransactionTable;
	friend class MachineBase;
	friend class MMContext;
	//mutable Mutex mLock;					///< thread-safe control, shared from gTransactionTable
	mutable HandoverEntry *mHandover;	// Usually null.  see getHandoverEntry()


	protected:

	// (pat) After l3rewrite sip-side and radio-side contention is handled by funneling all messages into a single queue.
	// I am keeping a lock for safety anyway in case contention is added later, for example, by handling timers in a different thread.
	// We lock the TranEntry while we are running a state machine procedure.
	// Cant use the old lock without either breaking the existing code or rewriting all the accessor functions,
	// so here is a new lock for the l3rewrite.
	mutable Mutex mL3RewriteLock;					///< thread-safe control, shared from gTransactionTable
	mutable Mutex mAnotherLock;		// This one is for the few items in TranEntry that are shared between threads.

	private:
	/**@name Stable variables, fixed in the constructor or written only once. */
	//@{
	TranEntryId mID;					///< the internal transaction ID, assigned by a TransactionTable

	// (pat) Even though this is of type L3MobileIdentity, it is the subscriber id used for SIP messages,
	// and currently it must be an IMSI.  It is not something sent to the MS.
	FullMobileId mSubscriber;			///< some kind of subscriber ID, preferably IMSI
	private:
	GSM::L3CMServiceType mService;			///< the associated service type

	// 24.007 11.2.3.1.3: The TI [Transaction Identifier] is 3 bits plus a flag.
	// The flag is 0 when it belongs to a transaction initiated by its sender, else 1.
	// The same TI can be used simultaneously in both direction, distinguished by the flag.
	// The TI 7 is reserved for an extension mechanism which only applies to certain protocols (ie, Call Control)
	// and is an error otherwise, but we should not use that value.
	// The value of the flag we store here is 0 if we initiated the transaction, 1 if MS initiated.
	unsigned mL3TI;							///< the L3 short transaction ID, the version we *send* to the MS
	static const unsigned cL3TIInvalid = 16;	// valid values are 0-7

	GSM::L3CalledPartyBCDNumber mCalled;	///< the associated called party number, if known
	GSM::L3CallingPartyBCDNumber mCalling;	///< the associated calling party number, if known

	CodecSet mCodecs;					// (pat) This comment is wrong: This is for the MS, saved from the L3Setup message.
	public: CodecSet getCodecs() { return mCodecs; }
	public: void setCodecs(CodecSet wCodecs) { mCodecs = wCodecs; }

	public:
	InterthreadQueue<SIP::DialogMessage> mTranInbox;
	// SIP Message processing is blocked during the AssignTCHF procedure.
	//Bool_z mSipDialogMessagesBlocked;
	std::string mMessage;					///< text message payload
	std::string mContentType;				///< text message payload content type
	//@}


	TermCause mFinalDisposition;		// How transaction ended.





	// (pat) In v1 (Pre-l3-rewrite), this was a value, not a pointer.
	// In v2, the TranEntry represents a connection to an MS,
	// and the SipDialog is a decoupled separate structure managed in the SIP directory.
	// The TranEntry usually becomes tied to a SipDialog
	// when the SipDialog is started, but some transactions
	// (notably MTSMS initiated from the BTS) dont ever have a SipDialog.
	// SipDialog and TranEntry are decoupled, so running all
	// the SIP messages through TranEntry is no longer necessary.
	// In the near future, SipDialog will survive past the end of the
	// transaction and we will be able to destroy TranEntrys
	// immediately upon loss of radio link.
	private:
	SIP::SipDialogRef mDialog;		// (pat) post-l3-rewrite only.

	SIP::SipDialog *getDialog() { return mDialog.self(); }
	SIP::SipDialog *getDialog() const { return mDialog.self(); }	// grrr

	public:
	void setDialog(SIP::SipDialog *dialog);		// Also passed through to here from MachineBase
	void teCloseDialog(TermCause cause);

	private:
	mutable SIP::SipState mPrevSipState;	///< previous SIP state, prior to most recent transactions

	unsigned mNumSQLTries;					///< number of SQL tries for DB operations

	MMContext *mContext;
	// For MO Transactions the Context is set at construction.
	// For MT Transactions the Transacton sits in a MMUser until it is started on a channel,
	// and the Context is set then.  This is used by MMContext.
	friend class MMUser;
	protected:	void teSetContext(MMContext *wContext) { mContext = wContext; }

	private:

	L3Cause::AnyCause mTerminationRequested;

	public:	// But only used by MobilityManagement routines.
	// TODO: Maybe this should move into the MMContext.
	MMSharedData *mMMData;

	// Constructor Methodology:
	// The actual C++ constructors are private, which means they may not be used directly
	// to create TranEntrys.  You must use one of the static constructor methods below.
	private:
	void TranEntryInit();	// Basic initialization for TranEntry.

	TranEntry(
		SIP::SipDialog *wDialog,
		//const GSM::L3MobileIdentity& wSubscriber,
		const GSM::L3CMServiceType& wService);

	private:
	static TranEntry *newMO(MMContext *wChan, const GSM::L3CMServiceType& wService);
	public:
	static TranEntry *newMOSMS(MMContext* wChannel);
	static TranEntry *newMOC(MMContext* wChannel, L3CMServiceType::TypeCode serviceType);
	static TranEntry *newMOC(MMContext* wChannel);
	static TranEntry *newMOMM(MMContext* wChannel);
	static TranEntry *newMOSSD(MMContext* wChannel);


	// This is the post-l3-rewrite
	static TranEntry *newMTC(
		SIP::SipDialog *wDialog,
		const FullMobileId& msid,
		const GSM::L3CMServiceType& wService,	// MobileTerminatedCall or UndefinedType for generic page from CLI.
		string wCallerId);

	static TranEntry *newMTSMS(
		SIP::SipDialog *dialog,
		const FullMobileId& msid,
		const GSM::L3CallingPartyBCDNumber& wCalling,
		string smsBody,
		string smsContentType);

	static TranEntry *newMTSS(
		const FullMobileId& msid,
		string ssBody,
		string ssType);

	static TranEntry *newHandover(const struct sockaddr_in* peer,
		unsigned wHandoverReference,
		SimpleKeyValue &params,
		L3LogicalChannel *wChannel,
		unsigned wHandoverOtherBSTransactionID);

	/** Form used for handover requests; argument is taken from the message string. */
	/** unused
	TranEntry(const struct ::sockaddr_in* peer,
		unsigned wHandoverReference,
		SimpleKeyValue &params,
		const char *proxy,
		L3LogicalChannel* wChannel,
		unsigned otherTransactionID);
	**/


	/** Set the outbound handover parameters and set the state to HandoverOutbound. */
	void setOutboundHandover(
		const GSM::L3HandoverReference& reference,
		const GSM::L3CellDescription& cell,
		const GSM::L3ChannelDescription2& chan,
		const GSM::L3PowerCommandAndAccessType& pwrCmd,
		const GSM::L3SynchronizationIndication& synch
			);

	/** Set the inbound handover parameters on the channel; state should alread be HandoverInbound. */
	//void setInboundHandover(float wRSSI, float wTimingError);


	/** Delete the database entry upon destruction. */
	~TranEntry();

	/**@name Accessors. */
	//@{
	unsigned getL3TI() const;
	void setL3TI(unsigned wL3TI);
	bool matchL3TI(unsigned ti, bool fromMS);	// Does this ti match this transaction?
	bool isL3TIValid() const {
		return getL3TI() != TranEntry::cL3TIInvalid;
	}

	const L3LogicalChannel* channel() const;
	L3LogicalChannel* channel();
	GSM::L2LogicalChannel* getL2Channel() const;
	L3LogicalChannel *getTCHFACCH();	// return channel but make sure it is a TCH/FACCH.

	//void setChannel(L3LogicalChannel* wChannel);

	FullMobileId& subscriber() { return mSubscriber; }
	void setSubscriberImsi(string imsi, bool andAttach);
	string subscriberIMSI() { return mSubscriber.mImsi; }

	//const GSM::L3CMServiceType& service() const { return mService; }
	const GSM::L3CMServiceType service() const { return mService; }
	GSM::CMServiceTypeCode servicetype() const { return service().type(); }

	const GSM::L3CalledPartyBCDNumber& called() const { return mCalled; }
	void setCalled(const GSM::L3CalledPartyBCDNumber&);

	const GSM::L3CallingPartyBCDNumber& calling() const { return mCalling; }

	//const char* message() const { return mMessage.c_str(); }
	//void message(const char *wMessage, size_t length);
	//const char* messageType() const { return mContentType.c_str(); }
	//void messageType(const char *wContentType);

	TranEntryId tranID() const { return mID; }

	// FIXME: This is where we need to do choose the codec from the ones in the SDP message
	// the capabilities of the MS.
	CodecSet chooseCodec() {
		return CodecSet(GSM_FR);	// punt for the moment.
	}

	//@}

	L3Cause::AnyCause terminationRequested();

	/**@name SIP-side operations */
	//@{

	//unused SIP::SipState getSipState() const;
	unsigned getRTPPort() const;


	// Obviously, these are only for TransactionEntries for voice calls.
	void txFrame(SIP::AudioFrame* frame, unsigned numFlushed);
	SIP::AudioFrame *rxFrame();

	//@}

	/** Retrns true if the transaction is "dead". */
	//bool teDead() const;

	/** Returns true if dead, or if removal already requested. */
	bool deadOrRemoved() const;

	/** Dump information as text for debugging. */
	void text(std::ostream&) const;
	string text() const;

	/** Genrate an encoded string for handovers. */
	std::string handoverString(string peer,string cause) const;

	private:

	/** Set up a new entry in gTransactionTable's sqlite3 database. */
	//void insertIntoDatabase();

	/** Run a database query. */
	void runQuery(const char* query) const;

	/** Echo latest SipState to the database. */
	SIP::SipState echoSipState(SIP::SipState state) const;

	// (pat) This is genuine removal, and partial cleanup if the TranEntry is dirty.

	/** Removal status. */
	private: void teRemove(TermCause cause);		// Keep private.  Dont call from Procedures.  Call teCloseCall or teCancel or chanClose instead.
	public:
	void teCancel(TermCause cause) { teRemove(cause); }		// Used from MM.  Cancel dialog and delete transaction, do not notify MM layer.  Used within MMLayer.
	void teCloseCallNow(TermCause cause, bool sendCause);
	// If the channel ends being released, this is the cause.
	
	////////////////////////////////// l3rwrite ///////////////////////////////////////////
	// (pat) l3rewrite stuff:
	//MachineBase *mCurrentProcedure;		// TODO: Delete in destructor.
	//MachineBase *currentProcedure() const { return mCurrentProcedure; }
	private:
	list<MachineBase*> mProcStack;
	MachineBase *tePopMachine();
	void tePushProcedure(MachineBase *);
	public:
	MachineBase *currentProcedure() const { return mProcStack.size() ? mProcStack.back() : NULL; }

	TranEntry *tran() { return this; }	// virtual callback for class ProcCommon

	void teSetProcedure(MachineBase *wProc, bool wDeleteCurrent=false);
	MachineStatus handleRecursion(MachineStatus status);
	bool handleMachineStatus(MachineStatus status);

	// Start the specified Procedure.  If it was invoked because of a L3Message, include it.
	//void teStartProcedure(MachineBase *wProc, const GSM::L3Message *l3msg=0);

	// Send a message to the current Procedure, either l3msg or lch.
	// lch is the channel this message arrived on.  It is information we have, but I dont think it is useful.
	// I wonder if there are any cases where lch may not be the L3LogicalChannel that initiated the Procedure?
	// It probably doesnt matter - we use the L3LogicalChannel to send return messages to the MS,
	// and the initial channel that created the Procedure is probably the correct one.
	// For example if lch is FACCH, we cannot send anything downstrem on that.
	public:
	// Update: We dont need to lock these in GSM because each channel runs in a separate thread.
	bool lockAndInvokeL3Msg(const GSM::L3Message *l3msg /*, const L3LogicalChannel *lch*/);
	bool lockAndInvokeFrame(const L3Frame *frame, const L3Message *l3msg);
	bool lockAndInvokeSipMsg(const SIP::DialogMessage *sipmsg);
	bool lockAndInvokeSipMsgs();
	bool lockAndStart(MachineBase *wProc=NULL);
	bool lockAndStart(MachineBase *wProc, GSM::L3Message *l3msg);
	//bool teInvokeState(unsigned state);
	//bool teInvokeStart(MachineBase *proc);
	bool lockAndInvokeTimeout(L3Timer *timer);
	void terminateHook();
	MMContext *teGetContext() { return mContext; }
	HandoverEntry *getHandoverEntry(bool create) const;	// Creates if necessary.
	TranEntry *unconst() const { return const_cast<TranEntry*>(this); }

	// Is this handset ever communicated with us?
	bool teIsTalking();

	// The following block is used to contain the fields from the TRANSACTION_TABLE that are useful for statistics
	public:
	time_t mStartTime;		// transaction creation time.
	time_t mConnectTime;	// When call is connected, or 0 if never.
	L3CDR *createCDR(bool makeCMR, TermCause cause);
};

std::ostream& operator<<(std::ostream& os, const TranEntry&);
std::ostream& operator<<(std::ostream& os, const TranEntry*);



/** A map of transactions keyed by ID. */
class NewTransactionMap : public std::map<TranEntryId,TranEntry*> {};

/**
	A table for tracking the states of active transactions.
*/
class NewTransactionTable {
	friend class TranEntry;
	friend class CSL3StateMachine;

	private:

#if EXTERNAL_TRANSACTION_TABLE
	sqlite3 *mDB;			///< database connection
#endif

	NewTransactionMap mTable;
	mutable Mutex mttLock;
	unsigned mIDCounter;

	public:
	/**
		Initialize a transaction table.
		@param path Path fto sqlite3 database file.
	*/
	void ttInit();

#if EXTERNAL_TRANSACTION_TABLE
	NewTransactionTable() : mDB(0) {}	// (pat) Make sure no garbage creeps in.
	~NewTransactionTable();
#endif

	/**
		Return a new ID for use in the table.
	*/
	unsigned ttNewID();

	/**
		Insert a new entry into the table; deleted by the table later.
		@param value The entry to insert into the table; will be deleted by the table later.
	*/
	void ttAdd(TranEntry* value);

	/**
		Find an entry and return a pointer into the table.
		@param wID The TransactioEntry:mID to search
		@return NULL if ID is not found or was dead
	*/
	TranEntry* ttFindById(TranEntryId wID);
	bool ttIsTalking(TranEntryId tranid);
	// (pat) This is a temporary routine to return the state asynchronously.
	// Should be replaced by using a RefCntPointer in the NewTransactionTable and returning that.
	CallState ttGetGSMStateById(TranEntryId wId);
	bool ttIsDialogReleased(TranEntryId wID);
	bool ttSetDialog(TranEntryId tid, SIP::SipDialog *dialog);	// Unlike TranEntry::setDialog(), this can be used from other directories, other threads.

	// (pat added)
	// unused: TranEntry* ttFindByDialog(SIP::SipDialog *psip);
	void ttAddMessage(TranEntryId tranid,SIP::DialogMessage *sipmsg);
	// (pat added)
	//TranEntry *ttFindByL3Msg(GSM::L3Message *l3msg, L3LogicalChannel *lch);

	/**
		Find the longest-running non-SOS call.
		@return NULL if there are no calls or if all are SOS.
	*/
	TranEntryId findLongestCall();

	/**
		Return the availability of this particular RTP port
		@return True if Port is available, False otherwise
	*/
	bool RTPAvailable(unsigned rtpPort);

	/**
		Fand an entry by its handover reference.
		@param ref The 8-bit handover reference.
		@return NULL if ID is not found or was dead
	*/
	TranEntry* ttFindByInboundHandoverRef(unsigned ref);

	/**
		Remove an entry from the table and from gSIPMessageMap.
		@param wID The transaction ID to search.
		@return True if the ID was really in the table and deleted.
	*/
	bool ttRemove(unsigned wID);

	bool ttTerminate(TranEntryId tid, L3Cause::BSSCause cause);


	//bool remove(TranEntry* transaction) { return remove(transaction->tranID()); }

	/**
		Remove an entry from the table and from gSIPMessageMap,
		if it is in the Paging state.
		@param wID The transaction ID to search.
		@return True if the ID was really in the table and deleted.
	*/
	//bool removePaging(unsigned wID);


	/**
		Find an entry by its channel pointer; returns first entry found.
		Also clears dead entries during search.
		@param chan The channel pointer.
		@return pointer to entry or NULL if no active match
	*/
	//TranEntry* ttFindByLCH(const L3LogicalChannel *chan);

	// The channel died.  Clean up all the TransactionEntries.  This is a mobility management function.
	//void ttLostChannel(const L3LogicalChannel *chan);

	/** Find a transaction in the HandoverInbound state on the given channel. */
	//TranEntry* ttFindByInboundHandoverChan(const L3LogicalChannel *chan);

	/**
		Find an entry by its SACCH channel pointer; returns first entry found.
		Also clears dead entries during search.
		@param chan The channel pointer.
		@return pointer to entry or NULL if no active match
	*/
	//TranEntry* ttFindBySACCH(const GSM::SACCHLogicalChannel *chan);

	/**
		Find an entry by its channel type and offset.
		Also clears dead entries during search.
		@param chan The channel pointer to the first record found.
		@return pointer to entry or NULL if no active match
	*/
	// (pat) unused, removed.
	//TranEntry* ttFindByTypeAndOffset(GSM::TypeAndOffset chanDesc);

	/**
		Find an entry in the given state by its mobile ID.
		Also clears dead entries during search.
		@param mobileID The mobile to search for.
		@return pointer to entry or NULL if no match
	*/
	// (pat) This one is unused too.
	// TranEntry* ttFindByMobileIDState(const GSM::L3MobileIdentity& mobileID, CallState state);

	/** Return true if there is an ongoing call for this user. */
	//bool isBusy(const GSM::L3MobileIdentity& mobileID);


	/** Find by subscriber and SIP call ID. */
	//TranEntry* ttFindBySIPCallId(const GSM::L3MobileIdentity& mobileID, const char* callID);

	/** Find by subscriber and handover other BS transaction ID. */
	TranEntry* ttFindHandoverOther(const GSM::L3MobileIdentity& mobileID, unsigned transactionID);

	/** Check for duplicated SMS delivery attempts. */
	//bool duplicateMessage(const GSM::L3MobileIdentity& mobileID, const std::string& wMessage);


	/**
		Find the channel, if any, used for current transactions by this mobile ID.
		@param mobileID The target mobile subscriber.
		@return pointer to TCH/FACCH, SDCCH or NULL.
	*/
	//L3LogicalChannel* findChannel(const GSM::L3MobileIdentity& mobileID);

	/** Count the number of transactions using a particular channel. */
	//unsigned countChan(const L3LogicalChannel*);

	size_t size() { ScopedLock lock(mttLock); return mTable.size(); }

	size_t dump(std::ostream& os, bool showAll=false) const;

	/** Generate a unique handover reference. */
	//unsigned generateInboundHandoverReference(TranEntry* transaction);

	private:


#if EXTERNAL_TRANSACTION_TABLE
	/** Accessor to database connection. */
	sqlite3* getDB() { return mDB; }
#endif

	/**
		Remove "dead" entries from the table.
		A "dead" entry is a transaction that is no longer active.
		The caller should hold mLock.
	*/
	//void clearDeadEntries();

	/**
		Remove and entry from the table and from gSIPInterface.
	*/
	//void innerRemove(TransactionMap::iterator);


	/** Check to see if a given outbound handover reference is in use. */
	//bool outboundReferenceUsed(unsigned ref);

	// pat added - create an L3TI that is not currently in use.
	// The gTMSITable.nextL3TI(wSubscriber.digits()) is just wrong - if anything it should be a bit-mask
	// of the TIs that are currently in use.
	// I dont think it needs to be in the TMSI table at all - if the BTS crashes all transactions are dead
	// so we can start over with TIs.
	//unsigned makeNewTI() {
	//}
};


extern void TranInit();

}; // Control namespace



#endif

// vim: ts=4 sw=4


