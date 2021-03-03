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


#define LOG_GROUP LogGroup::SIP		// Can set Log.Level.SIP for debugging

#include "SIPUtility.h"
#include "SIPBase.h"
#include "SIP2Interface.h"
#include "SIPMessage.h"
#include "SIPTransaction.h"
#include "SIPExport.h"

namespace SIP {
static const string cINVITEstr("INVITE");
static const string cREGISTERstr("REGISTER");

// Note: Most of the documentation is in SIPMessage.cpp

// TERMS:
// URI = Uniform Resource Identifier.  19.1: In SIP it includes sip:user:password@host:port;parameters?headers
// 		userinfo part of a URI is: The name:password@  The userinfo is optional.
// UAC = User Agent Client (request initiator)
// UAS = User Agent Server (request recipient)
// UA = User Agent, either client or server.
// Session = the SDP session for media created in a dialog.
// SIP Layers:
// 1. Message syntax/encoding
// 2. Transport Layer sec 18.
// 3. Transaction Layer: handles matching responses to requests, retransmissions, timeouts.
//		(Transaction Layer is kind of a crappy name since the whole thing is called a Transaction)
// 4. TU = Trasaction User.

// The From and To in a response match a request, ie, in a response they are the reverse of the message direction.
// Tags 8.1.1:
// The from-tag identifies the client and must be in a request.
// The to-tag identifies the peer.  A request must not have a To-tag.  To-tag is set in the response.



// Requests:
// Request-Line - first line in SIP request, contains: method Request-URI SIP-Version
// 		The request-URI is sip or sips uri indicating user or service to which this request is addressed.
// Responses:
//	Status-Line - first line in SIP response, contains: SIP-Version Status-Code Reason-Phrase

void SipTransaction::_define_vtable() {}
void SipTransaction::TUTimeoutV() { LOG(DEBUG); }

void SipTransaction::stInitNonDialogTransaction(TranEntryId tranid, string wBranch, SipMessage *request, const IPAddressSpec *wPeer) {
	mstTranId = tranid;
	mstBranch = wBranch;
	stSaveRequestId(request);
	mstPeer = *wPeer;
}
void SipTransaction::stInitNonDialogTransaction(TranEntryId tranid, string wBranch, SipMessage *request, string wProxy, const char *wProxyProvenance) {
	mstTranId = tranid;
	mstBranch = wBranch;
	stSaveRequestId(request);
	mstPeer.ipSet(wProxy, wProxyProvenance);
}
void SipTransaction::stInitInDialogTransaction(SipDialog *wDialog, string wBranch, SipMessage *request) {
	mstDialog = wDialog;
	mstTranId = wDialog->mTranId;
	mstPeer = *wDialog->dsPeer();
	mstBranch = wBranch;
	stSaveRequestId(request);
}

void SipTransaction::stWrite(SipMessage *sipmsg)
{
	if (!mstPeer.mipValid) {
		LOG(ERR) << "Attempt to write to invalid peer address:"<<mstPeer.mipName;
		return;
	}
	gSipInterface.siWrite(&mstPeer.mipSockAddr,sipmsg);
}


// Send a simple message that does not change the dialog state.
void SipTransaction::sendSimpleMessage(DialogState::msgState wInfo, int code)
{
	DialogMessage *dmsg = new DialogMessage(mstTranId,wInfo,code);
	SipCallbacks::ttAddMessage(mstTranId,dmsg);
}

void SipTransaction::sendAuthFailMessage(int code, string rand, string gsmRejectCode)
{
	devassert(mstTranId);
	// The tran id is 0 for unregister messages.  If one of those gets this far it is a bug.
	if (! mstTranId) { return; }

	DialogChallengeMessage *dmsg = new DialogChallengeMessage(mstTranId, DialogState::dialogFail,code);
	dmsg->dmRand = rand;
	dmsg->dmRejectCause = atoi(gsmRejectCode.c_str());
	devassert(mstTranId);
	// The tran id is 0 for unregister messages.  If one of those gets this far it is a bug.
	if (mstTranId) { SipCallbacks::ttAddMessage(mstTranId,dmsg); }
}

void SipTransaction::sendAuthOKMessage(SipMessage *sipmsg)
{
	devassert(mstTranId);
	// The tran id is 0 for unregister messages.  If one of those gets this far it is a bug.
	if (! mstTranId) { return; }

	string imsi = sipmsg->msmTo.uriUsername(); // The To: and From: have the same value for a REGISTER message.
	// Validate the imsi:
	if (imsi.empty()) {
		LOG(ERR) << "can't find imsi to store kc";
		sendAuthFailMessage(400,"","");
		return;
	}
	if (0 == strncasecmp(imsi.c_str(),"imsi",4)) {
		// happiness
	} else if (0 == strncasecmp(imsi.c_str(),"tmsi",4)) {
		// TODO: In future the user name could be a TMSI.
		LOG(ERR) << "SIP REGISTER message with TMSI not supported, userid="<<imsi;
		sendAuthFailMessage(400,"","");
		return;
	} else {
		LOG(ERR) << "SIP REGISTER message with invalid IMSI:"<<imsi<<" Message:"<<sipmsg->msmContent;
		sendAuthFailMessage(400,"","");
		return;
	}

	DialogAuthMessage *dmsg = new DialogAuthMessage(mstTranId, DialogState::dialogActive,200);
	dmsg->dmPAssociatedUri = sipmsg->msmHeaders.paramFind("P-Associated-URI");	// case doesnt matter
	dmsg->dmPAssertedIdentity = sipmsg->msmHeaders.paramFind("P-Asserted-Identity");

	string authinfo = sipmsg->msmHeaders.paramFind("Authentication-Info");
	if (! authinfo.empty()) {
		SipParamList params;
		parseToParams(authinfo, params);
		dmsg->dmKc = params.paramFind("cnonce"); // This is the way SR passes the Kc.
	} else {
		// There will not be a cnonce if the Registrar does not know it.  Dont worry about it.
		//LOG(INFO) << "No Authenticate-Info header in SIP REGISTER response:"<<sipmsg->msmContent;
	}

	if (dmsg->dmKc.empty()) {
		dmsg->dmKc = sipmsg->msmHeaders.paramFind("P-GSM-Kc"); // This is the way Yate passes the Kc.
	}

	WATCHF("CNONCE: imsi=%s Kc=%s\n",imsi.c_str(),dmsg->dmKc.c_str());
	if (! dmsg->dmKc.empty()) {
		LOG(DEBUG) << "Storing Kc:"<<LOGVAR(imsi)<<LOGVAR(dmsg->dmKc.size());	// We dont put Kc itself in the log.
		//gTMSITable.putKc(imsi.c_str()+4, kc, pAssociatedUri, pAssertedIdentity);
	} else {
		LOG(NOTICE) << "No Kc in SIP REGISTER response:"<<sipmsg->msmContent;
	}

	//NewTransactionTable_ttAddMessage(mstTranId,dmsg);
	SipCallbacks::ttAddMessage(mstTranId,dmsg);
}

// The cause is not currently used.
void SipTransaction::stSetDialogState(SipState newState, int code, char timer) const
{
	if (SipDialog *dialog = mstDialog.self()) {
		dialog->dialogPushState(newState,code,timer);
	}
}

// This is the default.  The INVITE ones are static and live in the dialog so they are not destroyed.
//void SipTransaction::stDestroyV()
//{
//	if (mstDialog) {
//		// Do what?
//		//mstDialog->removeTransaction(this);
//	}
//	gSipInterface.tuMapRemove(this,true);
//	delete this;
//}

void SipTransaction::stFail(int code)
{
	stSetDialogState(SSFail,code,0);	// Dont need more timer, this is invoked after timer expiry.
	stDestroyV();
}

ostream& operator<<(ostream& os, const SipTransaction&st)
{
	os << " SipTransaction("<<LOGVAR2("method",st.mstMethod)<<LOGVAR2("seqNum",st.mstSeqNum)<<")";
	return os;
}
ostream& operator<<(ostream& os, const SipTransaction*st)
{
	if (st) os << *st;
	else os << "(null SipTransaction)";
	return os;
}

// 17.1.3: Matching Responses to Client [outbound] Transactions
//	1.  Branch param in topmost Via matches.
//	2.  Method in CSeq header matches. - needed because CANCEL shares same branch param as the INVITE.
// Note: We dont need to worry about the branch param because we have only one dialog, and one each of the possible methods.
bool SipClientTrLayer::stMatchesMessageV(SipMessage * /*msg*/)
{
	// We are not going to do it this way.
	// return (msg->smViaBranch() == mstOutRequest.smViaBranch() && msg->smCSeqMethod() == mstOutRequest.smCSeqMethod());
	return false;
}

// 17.2.3: Matching requests to Server [inbound] Transactions:
//	If branch has the magic cookie z9hG4bK:
//	1. branch param in request equals the top via header branch of the request that created the transaction
//	2. and the sent-by value in in top via of request is equal
//	3. method matches, exceptfor ACK, in which case the method of the requested that created the transaction is INVITE.
// bool SipServerTrLayer::stMatchesMessageV(SipMessage *msg)
// {
// 	SipVia msgvia(msg->msmVias), requestvia(mstInRequest.msmVias);
// 	if (msgvia.mViaBranch == requestvia.mViaBranch && msgvia.mSentBy == requestvia.mSentBy) {
// 		if (msg->isACK()) {
// 			return mstInRequest.msmReqMethod == "INVITE";
// 		} else {
// 			return msg->msmReqMethod == mstInRequest.msmReqMethod;
// 		}
// 	}
// 	return false;
// }


// The transactions methods we support are:
//	INVITE, ACK, MESSAGE, BYE, CANCEL, INFO.


bool SipClientTrLayer::TLWriteHighSideV(SipMessage *sipmsg)
{
	int code = sipmsg->smGetCode();
	LOG(DEBUG) <<LOGVAR(mstState) <<LOGVAR(code);
	// This is overly verbose to exactly match the state machine described in RFC3261.
	switch (mstState) {
	case stInitializing:
		// Someone sent us a reply before we sent the outgoing message?
		LOG(ERR) << "SIP Message received on uninitialized client transaction."<<LOGVAR(sipmsg) <<this;
		return false;
	case stCallingOrTrying:
	case stProceeding:
		switch ((code / 100) * 100) {
		case 100:
			mstState = stProceeding;
			TUWriteHighSideV(sipmsg);
			return true;
		default:
			mstState = stCompleted;
			TUWriteHighSideV(sipmsg);
			if (stIsInvite()) {
				mstState = stTerminated;
				if (code >= 300) { mstDialog->MOCSendACK(); }
				else { stDestroyV(); return true; } // The TU is responssible for sending the ACK, after connection setup.
			}
			if (stIsReliableTransport()) { stDestroyV(); return true; } else { mTimerDK.set(32*1000); }
			return true;
		}
	case stCompleted:
		switch ((code / 100) * 100) {
		case 100:
		case 200:
			return false;	// suppress duplicate messages.
		default:
			if (stIsInvite()) { mstDialog->MOCSendACK(); }
			return false;	// suppress duplicate messages.
		}
	case stTerminated:
		// Now what?
		LOG(ERR) << "SIP Message received on terminated client transaction."<<LOGVAR(sipmsg) <<this;
		return false;
	}
	assert(0);
	TUWriteHighSideV(sipmsg);
	return true;	// For outbound transactions, TransactionLayer sends all inbound replies to the TU.
}

// This only gets called once.
// Looks like it never gets called SVG
// Outbound
void SipClientTrLayer::TLWriteLowSideV(SipMessage *request)
{
	LOG(DEBUG);
	ScopedLock lock(mstLock);
	// For an Outbound Transaction, there is only one request except for invite, which also sends an ACK.
	if (! mstOutRequest.msmReqMethod.empty()) { assert(request->isACK()); }
	mstOutRequest = *request;
	devassert(mstState == stInitializing);
	stWrite(request);
	mstState = stCallingOrTrying;
}

void SipClientTrLayer::sctInitRegisterClientTransaction(SipDialog *wRegistrar, TranEntryId tid, SipMessage *request, string branch)
{
	//stInitInDialogTransaction(wDialog, branch, request);	// Do this first.
	stInitNonDialogTransaction(tid, branch,request,wRegistrar->dsPeer());
	mstOutRequest = *request;
}

void SipClientTrLayer::sctInitInDialogClientTransaction(SipDialog *wDialog, SipMessage *request, string branch)
{
	stInitInDialogTransaction(wDialog, branch, request);	// Do this first.
	mTimerBF.setOnce(64*T1);	// this is init time so we could have used just set.
	if (!stIsReliableTransport()) { mTimerAE.set(T1); }
	mstOutRequest = *request;
}

void SipClientTrLayer::sctStart()
{
	LOG(DEBUG);
	ScopedLock lock(mstLock);
	// Must add to the tu map before sending the message because the reply can be fast enough to cause a race.
	gSipInterface.tuMapAdd(this);
	devassert(mstState == stInitializing);
	stWrite(&mstOutRequest);	// Send the initial message.
	mstState = stCallingOrTrying;
}

// Return TRUE to delete it.
bool SipClientTrLayer::TLPeriodicServiceV()
{
	LOG(DEBUG) << LOGVAR(mTimerDK) <<LOGVAR(mTimerBF) <<LOGVAR(mTimerAE);
	ScopedLock lock(mstLock);
	if (mstState == stInitializing) { return false; }
	if (mstState == stTerminated) { return true; }
	// The timerDK is the time in the Completed state, and used just to suck up additional incoming replies.
	if (mTimerDK.expired()) { stDestroyV(); return true; }	// The transaction completed, a message was sent to layer3, so we just exit.
	if (mTimerBF.expired()) {
		if (mstState == stCallingOrTrying || mstState == stProceeding) {
			stFail(408); // 408 - SIP Timeout.  Must send a fail message to layer3.
		}
		TUTimeoutV();
		stDestroyV();
		return true;
	}
	if (mstState == stCallingOrTrying && mTimerAE.expired()) { stWrite(&mstOutRequest); mTimerAE.setDouble(); }
	return false;
}



// Original RFC-3261 section 17 Figure 7.
//
//                   |INVITE
//                   |pass INV to TU
//INVITE             V send 100 if TU won't in 200ms
//send response+-----------+
//    +--------|           |--------+101-199 from TU
//    |        | Proceeding|        |send response
//    +------->|           |<-------+
//             |           | Transport Err.
//             |           | Inform TU
//             |           |--------------->+
//             +-----------+                |
//300-699 from TU |     |2xx from TU        |
//send response   |     |send response      |
//                |     +------------------>+
//                |                         |
//INVITE          V          Timer G fires  |
//send response+-----------+ send response  |
//    +--------|           |--------+       |
//    |        | Completed |        |       |
//    +------->|           |<-------+       |
//             +-----------+                |
//                |     |                   |
//            ACK |     |                   |
//            -   |     +------------------>+
//                |        Timer H fires    |
//                V        or Transport Err.|
//              +-----------+ Inform TU     |
//              |           |               |
//              | Confirmed |               |
//              |           |               |
//              +-----------+               |
//                   |                      |
//                   |Timer I fires         |
//                   |-                     |
//                   |                      |
//                   V                      |
//             +-----------+                |
//             |           |                |
//             | Terminated|<---------------+
//             |           |
//             +-----------+
//Figure 7: INVITE server transaction



// // The request should be INVITE or ACK.
// // Return TRUE if the message should be sent to the TU.
// bool SipServerTrLayer::TLWriteHighSideV(SipMessage *wRequest)
// {
// 	LOG(DEBUG);
// 	switch (mstState) {
// 	case stTrying:
// 		// Discard retransmissions until the TU server responds.
// 		// For invites, if this takes longer than 200ms we are supposed to manufacture a 100 Trying response,
// 		// but we're just going to hope that does not happen and treat it the same.
// 		if (sameMsg(wRequest,&mstInRequest)) {
// 			// discard duplicate.
// 			// TODO: Arent we supposed to send 100 Trying?
// 			return false;
// 		}
// 		TUWriteHighSideV(wRequest);
// 		return true;
// 	case stProceeding:
// 	case stCompleted:
// 		// The TU already sent something so resend it.
// 		// As specified in RFC3261 on receipt of a request we are required to resend the last response,
// 		// even if we just did so, which is a waste of resources.
// 		// I am modifying this slightly so we do not send two duplicate responses immediately after eaach other.
// 		if (mLastSent.elapsed() >= 100) {
// 			stWrite(&mstResponse);	// resend most recent response.
// 		}
// 		return false; // This is a repeat message.
// 	case stConfirmed:
// 		// This Confirmed state exists simply to ignore this duplicate ACK.
// 		// TODO: print an error if it is not an ACK.
// 		return false;		// This is a repeat ACK.
// 	case stTerminated:
// 		// Now what?
// 		LOG(ERR) << "SIP Message received on terminated server transaction."<<LOGVAR(wRequest) <<this;
// 		return false;
// 	}
// 	// Only applicable to invites; the ACK method is inbound also.
// 	if (wRequest->isACK()) {
// 		mstState = stConfirmed;
// 		if (stIsReliableTransport()) { stDestroyV(); } else { mTimerI.set(T4); }
// 	}
// 	TUWriteHighSideV(wRequest);
// 	return true;
// }
// 
// // The transaction was established by some incoming request, and now the TU is sending a response.
// void SipInviteServerTrLayer::TLWriteLowSideV(SipMessage *wResponse)
// {
// 	LOG(DEBUG);
// 	ScopedLock lock(mstLock);
// 	mstResponse = *wResponse;
// 	stWrite(&mstResponse); mLastSent.now();
// 	assert(stIsInvite());
// 
// 	switch (mstState) {
// 	case stTrying:
// 	case stProceeding:
// 		switch ((wResponse->msmCode / 100) * 100) {
// 		case 100:	// sending provisional response.
// 			//assert(mstState == stTrying || mstState == stProceeding);
// 			mstState = stProceeding;
// 			stSetDialogState(Proceeding,wResponse->msmCode);
// 			return;
// 		case 200:
// 			// The spec indicates that the TL layer is terminated when 200 is sent,
// 			// and I quote: "retransmissions of the 2xx responses are handled by the TU"
// 			// maybe because they have SDP content that may change?
// 			mstState = stTerminated;
// 			// stDestroyV();
// 			return;
// 		default:
// 			//assert(mstState != stConfirmed);
// 			mstState = stCompleted;
// 			mTimerHJ.set(64*T1);
// 			if (!stIsReliableTransport()) {
// 				mTimerG.set(min(T2,mGCount * T1));
// 				mGCount *= 2;
// 			}
// 			return;
// 		}
// 	case stCompleted:
// 	case stConfirmed:
// 	case stTerminated: // TODO: Is this right?
// 		assert(0);
// 	}
// }
// 
// void SipNonInviteServerTrLayer::TLWriteLowSideV(SipMessage *wResponse)
// {
// 	LOG(DEBUG);
// 	ScopedLock lock(mstLock);
// 	mstResponse = *wResponse;
// 	stWrite(&mstResponse); mLastSent.now();
// 	assert(! stIsInvite());
// 	switch (mstState) {
// 	case stTrying:
// 	case stProceeding:
// 		switch ((wResponse->msmCode / 100) * 100) {
// 		case 100:	// sending provisional response.
// 			mstState = stProceeding;
// 			stSetDialogState(Proceeding,wResponse->msmCode);
// 			return;
// 		default:
// 			mstState = stCompleted;
// 			// We have to rely on the TU layer to set the dialog state.
// 			//setDialogState(?);
// 			if (stIsReliableTransport()) { vstDestroy(); } else { mTimerHJ.set(64*T1); }
// 			return;
// 		}
// 	case stCompleted:
// 	case stConfirmed:
// 	case stTerminated:
// 		assert(0);
// 	}
// }
// 
// bool SipServerTrLayer::TLPeriodicServiceV()
// {
// 	LOG(DEBUG) << LOGVAR(mTimerHJ) <<LOGVAR(mTimerI) <<LOGVAR(mTimerG);
// 	ScopedLock lock(mstLock);
// 	if (mstState == stTerminated) { return true; }
// 	if (mstState == stTrying) return false;	// This is the initial state meaning nothing has been sent yet.
// 	if (mTimerHJ.expired()) { stDestroyV(); return true; }
// 	if (mTimerI.expired()) { stDestroyV(); return true; }
// 	if (mTimerG.expired()) {
// 		stWrite(&mstResponse); mLastSent.now();
// 		mTimerG.set(min(T2,mGCount * T1));
// 		mGCount *= 2;
// 	}
// 	return false;
// }

// ===========================================================================================


// It is hardly worth the effort to make a transaction for REGISTER, which occurs outside a dialog
// and has only one reply, but we need to know when to destroy it.
// Note: The registrar may return messages, which we must ignore for the Unregister case since there is no transaction to receive them.
void SipRegisterTU::TUWriteHighSideV(SipMessage *sipmsg)
{
	int code = sipmsg->msmCode;
	static const char *pRejectCauseHeader = "P-GSM-Reject-Cause";
	static const char *whatami = stKind == KindRegister ? "SIP Register " : "SIP UnRegister ";
	LOG(DEBUG) <<LOGVAR(code);
	if (code == 0) {
		// A register transaction does not receive any requests.
		LOG(ERR) << whatami <<"received unexpected message:"<<sipmsg;
		stDestroyV();
	} else if (code == 401) {
		//setDialogState(Fail,sipmsg);
		LOG(INFO) <<whatami <<"received:"<<sipmsg;
		if (stKind == KindRegister) {
			sendAuthFailMessage(code,sipmsg->smGetRand401(),sipmsg->msmHeaders.paramFind(pRejectCauseHeader));
		}
		setTransactionState(stCompleted);
	} else if (code == 200) {
		// (pat) We can no longer stash Kc directly in the TMSI table because the entry is not created until the MS is validated.
		if (stKind == KindRegister) {
			sendAuthOKMessage(sipmsg);
		}
	} else switch ((code/100) * 100) {
		case 100: return;	// we dont care.
		case 200: LOG(ERR) <<whatami<<"received invalid code:"<<code <<" in message:"<<sipmsg;	// Code in the range 201-299 is allegedly impossible.
		default:
			if (stKind == KindRegister) {
				sendAuthFailMessage(code,"",sipmsg->msmHeaders.paramFind(pRejectCauseHeader));
			}
			return;
	}
}

SipRegisterTU::SipRegisterTU(SipRegisterTU::Kind wKind, SipDialog *registrar, TranEntryId tid, SipMessage *request)
{
	// Kinda dumb to fish out the branch from the request, but its ok.
	//SipDialog *registrar = getRegistrar();
	//SipMessage *request = registrar->makeRegisterMsg(SIPDTRegister,chan,rand,msid,sres.c_str());
	// It is in the dummy dialog established for the registrar.
	stKind = wKind;
	sctInitRegisterClientTransaction(registrar, tid, request, request->smGetBranch());
}

void startRegister(TranEntryId tid, const FullMobileId &msid, const string rand, const string sres, L3LogicalChannel *chan) 		// msid is imsi and/or tmsi
{
	LOG(DEBUG) <<LOGVAR(msid)<<LOGVAR(rand)<<LOGVAR(sres);
	// Kinda dumb to fish out the branch from the request, but its ok.
	SipDialog *registrar = getRegistrar();
	SipMessage *request = registrar->makeRegisterMsg(SIPDTRegister,chan,rand,msid,sres.c_str());
	SipRegisterTU *reg = new SipRegisterTU(SipRegisterTU::KindRegister,registrar,tid,request);
	delete request;		// sctInitRegisterTransaction made a copy.  Kind of wasteful.
	reg->sctStart();
}

void startUnregister(const FullMobileId &msid, L3LogicalChannel *chan)
{
	LOG(DEBUG) <<LOGVAR(msid);
	SipDialog *registrar = getRegistrar();
	SipMessage *request = registrar->makeRegisterMsg(SIPDTUnregister,chan,"",msid,NULL);
	SipRegisterTU *reg = new SipRegisterTU(SipRegisterTU::KindUnRegister,registrar,(TranEntryId)0,request);
	delete request;		// sctInitRegisterTransaction made a copy.  Kind of wasteful.
	reg->sctStart();
}


// ===========================================================================================

void SipMOByeTU::TUWriteHighSideV(SipMessage *sipmsg) {
	LOG(INFO) << "SIP term info msmCode: " << sipmsg->msmCode;
	if (sipmsg->msmCode >= 200) { stSetDialogState(Cleared,sipmsg->msmCode,'K'); }
}
//void SipMOByeTU::TUTimeoutV() { stSetDialogState(Cleared,0); }

SipMOByeTU::SipMOByeTU(SipDialog *wDialog, string wReasonHeader) // : SipClientTrLayer(wDialog->dsPeer(), make_branch(),wDialog)
{
	LOG(INFO) << "SIP term info SipMOByeTU"; // SVGDBG
	string branch = make_branch();
	SipMessage *bye = new SipMessageRequestWithinDialog(stGetMethodNameV(),wDialog,branch);
	bye->msmReasonHeader = wReasonHeader;
	sctInitInDialogClientTransaction(wDialog, bye, branch);
	delete bye;
}

void SipMOCancelTU::TUWriteHighSideV(SipMessage *sipmsg) {
	if (sipmsg->msmCode >= 200) { stSetDialogState(Canceled,sipmsg->msmCode,'K'); }
}

//void SipMOCancelTU::TUTimeoutV() { stSetDialogState(Canceled,0); }

SipMOCancelTU::SipMOCancelTU(SipDialog *wDialog,string wReasonHeader) { // : SipClientTrLayer(wDialog->dsPeer(), make_branch(),wDialog
	LOG(INFO) << "SIP term info SipMOCancelTU";  // Mobile originate

	string branch = make_branch();
	SipMessage *cancelMsg = new SipMessageRequestWithinDialog(this->stGetMethodNameV(),wDialog,branch);
	cancelMsg->msmReasonHeader = wReasonHeader;
	//wDialog->getTermList().copyEntireList(cancelMsg->getTermList());  // SVGDBG SipMOCancelTU
	this->sctInitInDialogClientTransaction(wDialog, cancelMsg, branch);  // Message gets copied in here
	delete cancelMsg;
}

SipDtmfTU::SipDtmfTU(SipDialog *wDialog, unsigned wInfo) //: SipClientTrLayer(wDialog->dsPeer(), make_branch(),wDialog)
{
	string branch = make_branch();
	SipMessage *msg = new SipMessageRequestWithinDialog(stGetMethodNameV(),wDialog,branch);
	static const string applicationDtmf("application/dtmf-relay");
	string body;
	switch (wInfo) {
		case 11: body = string("Signal=*\nDuration=200"); break;
		case 12: body = string("Signal=#\nDuration=200"); break;
		default: body = format("Signal=%i\nDuration=200",wInfo); break;
	}
	msg->smAddBody(applicationDtmf,body);
	sctInitInDialogClientTransaction(wDialog, msg, branch);
	delete msg;
}

void SipDtmfTU::TUWriteHighSideV(SipMessage *sipmsg) {
	switch ((sipmsg->msmCode*100)/100) {
	case 100: return;	// keep going.
	default:
		sendSimpleMessage(DialogState::dialogDtmf,sipmsg->msmCode);
		return;
	}
}

};	// namespace SIP
