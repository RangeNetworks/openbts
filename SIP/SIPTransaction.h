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
// Written by Pat Thompson.
#ifndef _SIPTRANSACTION_H_
#define _SIPTRANSACTION_H_ 1

#include "SIPUtility.h"	// For SipTimer, IPAddressSpec
#include "SIPBase.h"

namespace SIP {
using namespace std;
using namespace Control;

// (pat) The RFC3261 Transaction Layer is responsible for resending messages.
// Note that a SIP Transaction is defined with 4 layers, one of which is absurdly called
// the "Transaction Layer" which is what this code implements.
// RFC3261 distinguishes only INVITE and non-INVITE transactions, but in reality there are 4 substantially
// different kinds SIP Transactions, each of which has a Client (initiating) and Server (receiving) side, for 8 types total.
// 		a. INVITE,
// 		b. non-INVITE and outside of any dialog.
// 		c. non-INVITE within a dialog.
// 		d. REGISTER, which are sufficiently different to be a whole type by themselves.
// I started by translating the state machines from RFC3261 sec 17 directly into code, and intended
// to use them for all types of SIP Transactions, but that just did not work well.
// The INVITE and non-INVITE types are too different, and additionally, the RFC3261 state machine
// for INVITE only goes part-way, then dumps control onto the Transaction User.
// For non-INVITE server transactions, the only thing we need to do is repeat the message each time the
// same request comes in, which is often more easily handled at the Transaction User level
// (eg if you get a second CANCEL request for a dialog, just send 200 OK again) so that
// code does not need the complicated state machinery.  And one more thing, the message routing
// is clearer if the transaction layer classes are a base class of the client class (either dialog or TU [Transaction User])
// rather than being passed to a separate Transaction Layer machine and back.
// So this is how it ended up:
// The INVITE (a) SIP Transaction code has been moved to SipMTInviteServerTransactionLayer and SipMTInviteServerTransactionLayer,
// which are base classes of the Dialog, and makes passing the messages through the TransactionLayer much clearer.
// The MESSAGE (b) is handled the same way because it was easier to connect to the Control layer with a Dialog.
// It would be better to have a base class which is the connection layer to the Control directory, but I have to stop cleaning up somewhere.
// The (c) Server side is handled by simple sip message handlers in the Dialog class.
// So this class is used only for Client-side (c) and (d) and could be simplified.


DEFINE_MEMORY_LEAK_DETECTOR_CLASS(SipTransaction,MemCheckSipTransaction)
class SipTransaction : public MemCheckSipTransaction, public SipTimers
{
	virtual void _define_vtable();		// Unused method to insure the compiler link phase is passified.
	protected:
	mutable Mutex mstLock;
	IPAddressSpec mstPeer;	// The remote peer.  Copied from mDialog at startup, or specified by Transaction creator.
	string mstBranch;	// no longer used.
	// TODO: Maybe this should be a SipEngine...
	SipDialogRef mstDialog;		// Transaction owner, or NULL for out-of-dialog transactions.
	TranEntryId mstTranId;		// The associed L3 Transaction, if any.  TODO: Now this could use a RefCntPointer to the transaction.

	public:
	string mstMethod; int mstSeqNum;
	string mstCallId;
	virtual string stGetMethodNameV() = 0;

	protected:
	void stWrite(SipMessage *msg);
	bool stIsReliableTransport() const { return mstPeer.ipIsReliableTransport(); }
	string stTransportName() { return mstPeer.ipTransportName(); }
	// Send a message to the TranEntry associated with this Dialog.
	void sendSimpleMessage(DialogState::msgState wInfo, int code);
	// (pat) Yes, it is ugly having specialized methods in a base class.
	void sendAuthFailMessage(int code, string rand, string gsmRejectCode);
	void sendAuthOKMessage(SipMessage *sipmsg);

	// I dont think we are going to use this:
	virtual bool stMatchesMessageV(SipMessage *msg) = 0;
	// Inbound is toward the radio, Outbound is toward the outside world.
	virtual bool TLWriteHighSideV(SipMessage *msg) = 0;	// TL processes an incoming message from the outside world, returns true if should go to TU.
	virtual void TUWriteHighSideV(SipMessage *msg) = 0;		// TU overrides this to receive messages.
	virtual void TUTimeoutV();								// TU may optionally override this to be informed.

	void stSetDialogState(SipState newState, int code, char timer) const;
	//SipDialog *dialog() { return mDialog; }
	//void stSetSipState(SipState wState) { mstDialog->setSipState(wState); }
	void stSetTranEntryId(TranEntryId tid) { mstTranId = tid; }

	private:
	void stSaveRequestId(SipMessage *request) {
		mstMethod = request->msmCSeqMethod;
		mstSeqNum = request->msmCSeqNum;
		mstCallId = request->msmCallId;
	}

	protected:
	// The idiotic C++ constructor paradigm obfuscates construction so badly in this case that we are not going to use it.
	// A SipTransaction is created locked both to make sure the periodic service routine does not process
	// it before it is completely constructed and to avoid the problem of an incoming message being routed
	// to the transaction during its initialization.
	SipTransaction() : mstDialog(NULL), mstTranId(0) { /*mstLock.lock();*/ }
	// A transaction always starts with a request, either inbound request for a server transaction or
	// outbound request for a client transaction.
	// These differ only in how the peer is specified.
	void stInitNonDialogTransaction(TranEntryId tranid, string wBranch, SipMessage *request, const IPAddressSpec *wPeer);	// currently unused
	void stInitNonDialogTransaction(TranEntryId tranid, string wBranch, SipMessage *request, string wProxy, const char* wProxyProvenance);	// currently unused
	void stInitInDialogTransaction(SipDialog *wDialog, string wBranch, SipMessage *request);

	virtual void stDestroyV() = 0;

	void stFail(int code);

	// These objects are used by multiple threads by their nature; the TransactionLayer receives input from:
	//		the external SIP interface; layer3 control; periodic service.
	// Therefore we carefully mutex protect them.
	// Please dont go making more of this class public without mutex protecting it.
	public:
	string stBranch() { return mstBranch; }
	// unused virtual bool stIsTerminated() const = 0;
	virtual void TLWriteLowSideV(SipMessage *msg) = 0;		// TL processes uplink message to the outside world.
	void TLWriteHighSide(SipMessage *msg) {	// SIP Interface sends incoming messages here.
		LOG(DEBUG);
		ScopedLock lock(mstLock);
		TLWriteHighSideV(msg);
	}
	virtual bool TLPeriodicServiceV() = 0;

	//void stUnlock() { mstLock.unlock(); }
	virtual ~SipTransaction() {	// Do not delete this method even if empty.
		// Do we need to lock this?  What is the point.  It is deleted only from
		// inside the SipTUMap class, which holds the mTUMap lock throughout the procedure,
		// preventing any incoming messages.
	}
};
ostream& operator<<(ostream& os, const SipTransaction*st);
ostream& operator<<(ostream& os, const SipTransaction&st);


// SIP Transaction Layer for client (outbound) transactions.
// The transaction layer does not modify messages - it is responsible only for resends.
// Therefore it is informed of all inbound and outbound messages.
// Outbound messages are just saved for possible retransmission.
// Inbound messages may be discarded at this layer if they are repeats.
//
// RFC 3261 17.1.1 and Figure 5.  client INVITE transaction.
// 		Timers A, B, D
// INVITE->peer
// 		<-1xxx peer
//		<-2xxx peer
//			send to dialog, which is responsible for ACK
//		<-3xx,4xx,5xx,6xx peer
//			ACK->peer, send fail to dialog
// MESSAGE,REGISTER->peer
//		<- 1xx peer
// RFC 3261 17.1.2 and Figure 6. client non-INIVITE transaction, eg MESSAGE, REGISTER
//		Timers E, F, K
// (pat) Update: We are no longer using this for MESSAGE transactions.
class SipClientTrLayer : public SipTransaction
{
	SipTimer mTimerAE, mTimerBF, mTimerDK;
	protected:
	bool stIsInvite() { return mstOutRequest.isINVITE(); }	// We ended up not using this class for INVITE, but some code still here.
	enum States {	// These are transaction states, not dialog states.
		stInitializing, stCallingOrTrying, stProceeding, stCompleted, stTerminated
	} mstState;
	// Downlink is toward the radio, Uplink is toward the outside world.
	bool TLWriteHighSideV(SipMessage *msg);	// TL processes an incoming message from the outside world, returns true if should go to TU.
	void TLWriteLowSideV(SipMessage *msg);	// TL processes uplink message to the outside world.
	SipClientTrLayer() { mstState = stInitializing; }
	void stDestroyV() { mstState = stTerminated; }
	
	public:
	SipMessage mstOutRequest;	// outbound request, eg INVITE, MESSAGE, REGISTER.
	// unused bool stIsTerminated() const { return mstState == stTerminated; }
	void setTransactionState(States st) { mstState = st; }
	bool stMatchesMessageV(SipMessage *msg);
	bool TLPeriodicServiceV();
	SipMessage *vstGetRequest();
	// We use a client transaction for REGISTER even though it is not technically a TU, it acts like one
	// except there are no resends, which we implement just by not setting any timers.
	void sctInitRegisterClientTransaction(SipDialog *wRegistrar, TranEntryId tid, SipMessage *request, string branch);
	void sctInitInDialogClientTransaction(SipDialog *wDialog, SipMessage *request, string branch);
	void sctStart();
};

class SipInviteClientTrLayer : public SipClientTrLayer
{
	string stGetMethodNameV() { static const string inviteStr("INVITE"); return inviteStr; }
	void TUWriteHighSideV(SipMessage * /*sipmsg*/) {}	// ??
};


// It is hardly worth the effort to make a transaction for REGISTER, which occurs outside a dialog
// and has only one reply, but we need to know when to destroy it.
struct SipRegisterTU : public SipClientTrLayer
{
	enum Kind { KindRegister=1, KindUnRegister=2 } stKind;
	string stGetMethodNameV() { static const string registerStr("REGISTER"); return registerStr; }
	void TUWriteHighSideV(SipMessage *sipmsg);
	//SipRegisterTU(const FullMobileId &msid, const string &rand, const string &sres, L3LogicalChannel *chan); 		// msid is imsi and/or tmsi
	SipRegisterTU(Kind kind, SipDialog *registrar, TranEntryId tid, SipMessage *request);
};


struct SipMOByeTU: public SipClientTrLayer
{
	string stGetMethodNameV() { static const string cByeStr("BYE"); return cByeStr; }
	void TUWriteHighSideV(SipMessage *sipmsg);
	// TUTimeoutV not needed; on timeout we set dialog state to SSFail.
	//void TUTimeoutV();
	SipMOByeTU(SipDialog *wDialog, string wReasonHeader);
};

struct SipMOCancelTU: public SipClientTrLayer
{
	string stGetMethodNameV() { static const string cCancelStr("CANCEL"); return cCancelStr; }
	void TUWriteHighSideV(SipMessage *sipmsg);
	// TUTimeoutV not needed; on timeout we set dialog state to SSFail.
	//void TUTimeoutV();
	SipMOCancelTU(SipDialog *wDialog, string wReasonHeader);
};


struct SipDtmfTU: public SipClientTrLayer
{
	string stGetMethodNameV() { static const string infostr("INFO"); return infostr; }
	SipDtmfTU(SipDialog *wDialog, unsigned wInfo);
	void TUWriteHighSideV(SipMessage *sipmsg);
};

};	// namespace SIP
#endif
