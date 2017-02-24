/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2011, 2014 Range Networks, Inc.
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


// Comments by pat:
// Documents: RFC-3261: The SIP bible.
//	3GPP-24-228: ladder diagrams for SIP flows.
//	3GPP-24-229: sec 7 has timer values, extensions to SIP headers for authentication, etc.
// SIP = Session Initiation Procotol
// UAC = User Agent Client.
// UAS = User Agent Server.
// TU = Transaction User.
// Transaction: a request and all related responses.
// Dialog: "A peer-to-peer SIP relationship between two user agents that persists for some time".  Established by INVITE.
// Session: "A collection of participants and the media streams between them"  Established by INVITE, using SDP.
// The idea is that a dialog may inolve multiple transactions to do in-session modifications like call-hold or transfer.
// RFS-3261 sec 5: The SIP layer protocols are:  1. syntax and encoding; 2. Transport; 3. Transaction; 4. Transaction User (TU)
// Transport Layer:
// The Transport Layer is responsible for selecting TCP/UDP/other and handling via requests, which alter where
// the responses would go.  sec 18.2.1: TransportLayer is supposed to filter messages and silently throw away invalid ones.
// TransportLayer is supposed to be able to auto-switch between TCP & UDP, but that may not apply to us
// because our messages are always tiny - voice data packets.
// TransportLayer is supposed to process the Via field to provide redirection.
// Transaction Layer:
// A Transaction = a request along with all responses to that request.
// TransactionLayer matches responses to requests (SIPInterface.cpp) handles retransmissions and timeouts (SIPEngine)
// TransactionLayer is supposed to one of the simple state machines in sec 17.  State machine type is chosen by message type.
// Transaction User:
// Sends a request and expects a response.  It can send a CANCEL, which the TransactionLayer state machine must deal with.

#define LOG_GROUP LogGroup::SIP		// Can set Log.Level.SIP for debugging

#include <typeinfo>

#include <GSML3CCElements.h>
#include <GSMLogicalChannel.h>
#include <GSMConfig.h>
#include <Globals.h>	// For gReports.
#include <L3TranEntry.h>
#include <L3MMLayer.h>

#include <Sockets.h>
#include <OpenBTSConfig.h>

#include "SIPUtility.h"
#include "SIP2Interface.h"
#include "SIPMessage.h"

#include <Logger.h>

#undef WARNING



namespace SIP {
using namespace std;
using namespace GSM;
using namespace Control;


//static bool sipGetDirectMode() { return gConfig.getStr("SIP.Proxy.Mode") == string("direct"); }


// SIPInterface method definitions.


// CallId should include the host, but currently the remotehost is goofed up in sipSetUser,
// so for now continue to use just the sip user number.
//static string callIdGetNumber(string callid)
//{
//	//unsigned at = callid.find_first_of('@');
//	// The test is not necessary but may avoid an extra malloc.
//	//return (at == string::npos) ? callid : callid.substr(0,at);
//}

string SipDialogMap::makeTagKey(string callid, string localTag)
{
	// We are not using the remote-tag.  The remote-tag (aka the to-tag) is not known at dialog creation time.
	// The local tag is sufficient to disambiguate the two dialogs with the same callid for direct bts-to-bts communication.
	// Note that whether the local tag is in the "from" or "to" depends on the message direction.
	return format("%s-%s",callid,localTag);
}


// Find a dialog from an incoming Message.
// An outgoing INVITE has a local tag immediately, which is returned by the peer.
// An incoming INVITE does not have a local tag yet until the ACK is received, so ACK must be handled specially.
SipDialogRef SipDialogMap::findDialogByMsg(SipMessage *msg)
{
	// This is an incoming message, so if there is a code then it is a reply so the Dialog was outbound.
	string callid = msg->smGetCallId(), localtag = msg->smGetLocalTag();
	string key = makeTagKey(callid,localtag);
	// We dont need a copy of the reference because it is still in the map and therefore cannot be destroyed.
	SipDialogRef dialog = mDialogMap.readNoBlock(key);
	if (! dialog.self() && msg->isACK()) {
		// For ACK try without the local tag.
		// The ACK and all subsequent messages from the peer include our local tag that it regurgitates.
		key = makeTagKey(callid,"");
		dialog = mDialogMap.readNoBlock(key);
	}
	return dialog;
}

// Add the local tag to the lookup key for this dialog.
void SipDialogMap::dmAddLocalTag(SipDialog *dialog)
{
	string callid = dialog->callId(), localtag = dialog->dsLocalTag();
	devassert(! localtag.empty());
	ScopedLock lock(mDialogMap.qGetLock());
	SipDialogRef fnd;
	string oldkey = makeTagKey(callid,"");
	string newkey = makeTagKey(callid, localtag);
	if (mDialogMap.getNoBlock(oldkey,fnd,true)) {	// Removes from the map.
		devassert(fnd.self() == dialog);
		// I am adding dialog instead of fnd in case of some bug where the old did not exist, we ignore it and keep going.
		mDialogMap.write(newkey,SipDialogRef(dialog));
	} else {
		// If it is a duplicate ACK it will already be moved.
		if (! mDialogMap.getNoBlock(newkey,fnd,false)) {	// Do not remove from map
			LOG(ERR) << "Could not find dialog"<<LOGVAR(callid)<<LOGVAR(localtag);
		}
	}
}

// This removes the SipDialog from the map so it will no longer receive SIP messages.
// It moves it to the mDeadDialogs queue, whence it will be deleted when we
// are sure there is no transaction still pointing to it.
// TODO: There may still be transactions running though... Do they get their messages?
bool SipDialogMap::dmRemoveDialog(SipBase *dialog)
{
	string callid = dialog->callId(), localtag = dialog->dsLocalTag();
	SipDialogRef dialog1;
	bool extant1 = mDialogMap.getNoBlock(makeTagKey(callid,localtag),dialog1);	// Removes the element.
	if (extant1) {
		gSipInterface.mDeadDialogs.push_back(dialog1);
	}
	SipDialogRef dialog2;
	bool extant2 = mDialogMap.getNoBlock(makeTagKey(callid,""),dialog2);		// Removes the element.
	if (extant2) {
		gSipInterface.mDeadDialogs.push_back(dialog2);
	}
	LOG(DEBUG) << LOGVAR(callid) <<LOGVAR(localtag) <<LOGVAR(extant1) <<LOGVAR(extant2);
	return extant1;
}

// For outgoing [client] invites the invite should have the local tag already and is put in the map using the localtag.
// For incoming [server] invites, we add the call dialog initially without the local tag, then later add the localtag to the key
// when we get the ACK.
void SipDialogMap::dmAddCallDialog(SipDialog*dialog)
{
	string callid = dialog->callId();
	string key = makeTagKey(callid, dialog->dgIsServer() ? string("") : dialog->dsLocalTag());
	SipDialogRef previous = mDialogMap.readNoBlock(key);
	LOG(DEBUG) <<LOGVAR(key);
	if (previous.self()) {
		dmRemoveDialog(previous.self());
		if (dialog->mIsHandover) {
			// What happened is the dialog went to another BTS and is now coming back before
			// the previous dialog has completely timed out.  We must destroy the old dialog immediately.
		} else {
			LOG(ERR) << "Adding duplicate dialog."<<LOGVAR(previous)<<LOGVAR(dialog);
		}
	}
	// Calling SipDialogRef here 'takes over' the deallocation of the Dialog from this point on;
	// The dialog will be deleted when the last reference to it is decremented.
	mDialogMap.write(key,SipDialogRef(dialog));
}


#if UNUSED
// Call the method on each extant SipDialog.
// The method must be defined in class SipDialog as: void method(SipDialog *);
void MySipInterface::iterateDialogs(SipDialogMethodPointer method)
{
	ScopedLock lock(mDialogMapLock);
	for (SipDialogMap::iterator it1 = mDialogMap.begin(); it1 != mDialogMap.end(); it1++) {
		SipDialogList_t &dialogs = it1->second;
		for (SipDialogList_t::iterator it2 = dialogs.begin(); it2 != dialogs.end(); it2++) {
			SipDialog *dialog = *it2;
			(dialog->*method)();
		}
	}
}
#endif

void SipDialogMap::dmPeriodicService() 
{
	try {
#if USE_SCOPED_ITERATORS
		DialogMap_t::ScopedIterator it(mDialogMap);
#else
		ScopedLock lock(mDialogMap.qGetLock());
		DialogMap_t::iterator it;
#endif
		for (it = mDialogMap.begin(); it != mDialogMap.end(); ) {
			SipDialogRef dialog = it->second;
			it++;
			if (dialog->dialogPeriodicService()) {
				gSipInterface.dmRemoveDialog(dialog.self());
			}
		}
	} catch(exception &e) {
		// We dont expect any throws; just being ultra cautious.
		LOG(ERR) << "SIP processing exception "<<e.what() << " " << typeid(&e).name();
	} catch(...) {
		LOG(CRIT) << "Unhandled exception attempted to kill OpenBTS SIP processor";	// pat added
		devassert(0);
	}
	//LOG(DEBUG) << "end";
}

void SipTUMap::tuMapAdd(SipTransaction*tup) {
	LOG(DEBUG) <<LOGVAR(tup);
	string key = tuMakeKey(tup);
	if (SipTransaction *existingTU = mTUMap.readNoBlock(key)) {
		LOG(ERR) << "Warning: adding second SipTransaction with branch:"<<tup->stBranch()
				<<LOGVAR(key) <<LOGVAR(existingTU) <<LOGVAR(tup);
	}
	// Deletes the old.
	// Take care not to create deadlock here?  To delete the old SipTransaction we have to lock it.
	mTUMap.write(key,tup);
}

void SipTUMap::tuMapRemove(SipTransaction*tup, bool /*whine*/) {
	LOG(DEBUG) <<LOGVAR(tup);
	mTUMap.remove(tuMakeKey(tup));	// This deletes it!
}

void SipTUMap::tuMapPeriodicService() {
	ScopedLock lock(mTUMap.qGetLock());
	int cnt = 0;
	//InterthreadMap<string,SipTransaction>::ScopedIterator sit(mTUMap);
	for (TUMap_t::iterator sit = mTUMap.begin(); sit != mTUMap.end();) {
		SipTransaction *me = sit->second;
		sit++;
		bool deleteme = me->TLPeriodicServiceV();
		LOG(DEBUG) <<LOGVAR(deleteme)<<LOGVAR(me);
		if (deleteme) {
			gSipInterface.tuMapRemove(me,true);
			cnt++;
		}
	}
	if (cnt) LOG(DEBUG) <<"finished, number deleted="<<cnt;
}

// Attempt to dispatch an incoming SIP message to a TU; return true if a transaction wanted this message.
// This has to be locked so someone doesnt delete the TU between the time we get its pointer
// and send it the message.
bool SipTUMap::tuMapDispatch(SipMessage*msg) {
	ScopedLock lock(mTUMap.qGetLock());
	string key = tuMakeKey(msg);
	SipTransaction *tup = mTUMap.readNoBlock(key);
	LOG(DEBUG) <<LOGVAR(msg) <<LOGVAR(tup);
	if (! tup) { return false; }		// Eventually this will be an error, but not everything is transitioned to use TU yet.
	WATCH("SIP recv: Sending message to TU "<< tup->stGetMethodNameV());
	tup->TLWriteHighSide(msg);
	return true;
}

// Look at all the dead dialogs and delete any that can be deleted safely.
// They can be deleted if their SIP timers have expired and no TranEntry still points to them.
void MySipInterface::purgeDeadDialogs()
{
#if USE_SCOPED_ITERATORS
	DeadDialogListType::ScopedIterator sit(mDeadDialogs);
#else
	ScopedLock lock(mDeadDialogs.getLock());
	DeadDialogListType::iterator sit;
#endif
	for (sit = mDeadDialogs.begin(); sit != mDeadDialogs.end();) {
		SipDialogRef dialog = *sit;
		LOG(DEBUG) << "purgeDeadDialogs"<<LOGVAR2("deletable",dialog->dgIsDeletable()) <<*dialog <<LOGVAR2("ttIsDeletable",gNewTransactionTable.ttIsDialogReleased(dialog->mTranId));
		if (dialog->dgIsDeletable()) {
			sit = mDeadDialogs.erase(sit);
			//delete dialog;
			dialog.free();
		} else {
			sit++;
		}
	}
}

SipDialogRef SipDialogMap::dmFindDialogByRtp(RtpSession *session)
{
#if USE_SCOPED_ITERATORS
	DialogMap_t::ScopedIterator sit(mDialogMap);
#else
	ScopedLock lock(mDialogMap.qGetLock());
	DialogMap_t::iterator sit;
#endif
	for (sit = mDialogMap.begin(); sit != mDialogMap.end(); sit++) {
		SipDialogRef dialog = sit->second;
		if (dialog->mSession == session) {
			return dialog;
		}
	}
	return SipDialogRef();	// An empty one.
}

#if UNUSED
SipBase *SipDialogMap::dmFindDialogById(unsigned id)
{
#if USE_SCOPED_ITERATORS
	DialogMap_t::ScopedIterator sit(mDialogMap);
#else
	ScopedLock lock(mDialogMap.qGetLock());
	DialogMap_t::iterator sit;
#endif
	for (sit = mDialogMap.begin(); sit != mDialogMap.end(); sit++) {
		SipDialog *dialog = sit->second;
		if (dialog->mDialogId == id) {
			return (SipBase*)dialog;
		}
	}
	return NULL;
}
#endif

void SipDialogMap::printDialogs(ostream&os)
{
#if USE_SCOPED_ITERATORS
	DialogMap_t::ScopedIterator sit(mDialogMap);
#else
	ScopedLock lock(mDialogMap.qGetLock());
	DialogMap_t::iterator sit;
#endif
	for (sit = mDialogMap.begin(); sit != mDialogMap.end(); sit++) {
		SipDialogRef dialog = sit->second;
		os << dialog->dialogText(false) << "\n";
	}
//	ScopedLock lock(mDialogMapLock);
//	for (DialogListMap_t::iterator it1 = mDialogMap.begin(); it1 != mDialogMap.end(); it1++) {
//		SipDialogList_t &dialogs = it1->second;
//		for (SipDialogList_t::iterator it2 = dialogs.begin(); it2 != dialogs.end(); it2++) {
//			SipDialog *existing = *it2;
//			os << existing->dialogText(false) << "\n";
//		}
//	}
}

void printDialogs(ostream &os)
{
	gSipInterface.printDialogs(os);
}



// This does NOT delete the msg.
// This writes all SIP messages
void SipInterface::siWrite(const struct sockaddr_in* dest, SipMessage *msg) 
{
	string msgstr = msg->smGenerate(OpenBTSUserAgent());
	string firstLine = msgstr.substr(0,msgstr.find('\n'));

	// For debug purposes dump the address assuming IPv4.
	//uint32_t addr = ntohl(dest->sin_addr.s_addr);
	char netbuf[102];
	inet_ntop(AF_INET,&(dest->sin_addr),netbuf,100);
	uint16_t port = ntohs(dest->sin_port);

	//WATCHF("SIP write %s:%d %s to=%s\n",netbuf,port,firstLine.c_str(),msg->msmToValue.c_str());
	WATCHINFO("SIP write "<<msg->smGetPrecis());
	//LOG(INFO) << "write " << firstLine;
	LOG(DEBUG) << "write " <<netbuf <<":"<<port <<" " <<msgstr;

	if (random()%100 < gConfig.getNum("Test.SIP.SimulatedPacketLoss")) {
		LOG(NOTICE) << "simulating dropped outbound SIP packet: " << firstLine;
		return;
	}

	// We think that large packets will probably be fragmented automatically and the underlying
	// send or sendto will return an error if the packet is too long.  So dont check the length here.
	//int size = msgstr.size();
	//if (size > MAX_UDP_LENGTH) {
	//	LOG(NOTICE) << "SIP Message length exceeds UDP limit, dropped; message:"<<msgstr;
	//}

	mSocketLock.lock();
	mSIPSocket->send((const struct sockaddr*)dest,msgstr.c_str(),msgstr.size());
	mSocketLock.unlock();
}

// If this message is handled by an existing TransactonUser return true;
// We only create client TUs [Transaction Users] so only replies are sent to TUs.
bool MySipInterface::checkTU(SipMessage *msg)
{
	if (msg->msmCode == 0) { return false; }	// This is a request and only replies go directly to TUs.
	// 7-23-2013 Dont touch the via-branch.  Neither sipauthserve nor smqueue are compliant so in defiance
	// of RFC3261 17.1.3 we cannot use the via-branch.
	//string branch = msg->smGetBranch();
	//if (branch.empty()) {
	//	// This may indicate a non-compliant peer.
	//	LOG(ERR) << "Reply with no via branch:"<<msg;
	//	return false;		// This is serious.
	//}

	return tuMapDispatch(msg);
}

void MySipInterface::newDriveIncoming(char *content)
{
	LOG(DEBUG) << "SIP recv:"<<content;

	SipMessage *msg = sipParseBuffer(content);
	if (!msg) { return; }

	WATCHINFO("SIP recv "<<msg->smGetPrecis());

	try {
		if (newCheckInvite(msg)) { delete msg; return; }

		const char *methodname = msg->smGetMethodName();	// May be empty, but never NULL
		if (*methodname) { LOG(DEBUG) << "non-initiating SIP method " << methodname; }

		if (checkTU(msg)) { delete msg; return; }

		// Send message to the appropriate SipDialog.
		SipDialogRef dialog = findDialogByMsg(msg);
		if (dialog.self()) {
			if (msg->smGetCode()) {
				// All replies go to the TUs, so if we did not find one above, this is an error.
				LOG(NOTICE) << "SIP reply to non-existent SIP transaction "<<msg;
				newSendEarlyError(msg,500,"Server Error");
			}
			dialog->dialogWriteDownlink(msg);
		} else {
			string callid = msg->smGetCallId();
			LOG(NOTICE) << "unrecognized"<<LOGVAR(callid) <<LOGVAR2("SIP Message:",msg);
			// Alert the SIP peer that this user is bogus.  We did not do this pre-l3-rewrite.
			// Asterisk seems to send 500 for this type of error.
			newSendEarlyError(msg,404,"Not Found");
			delete msg;
		}

	} catch(exception &e) {
		LOG(ERR) << "SIP processing exception "<<e.what() << " " << typeid(&e).name();
	} catch(...) {
		LOG(CRIT) << "Unhandled exception attempted to kill OpenBTS SIP processor";	// pat added
		devassert(0);
	}
}



// Warning: This assumes the cause message is a SIP request, not a SIP response.
void MySipInterface::newSendEarlyError(SipMessage *cause, int code, const char * reason)
{
	// If the message that caused the error is a 400 class error response, we must not send
	// a response error to prevent a fast infinite message loop with the peer.
	// In fact, we will ignore any response, and only return errors to requests.
	if (cause->smGetCode() != 0) { return; }	// Ignore responses.

	IPAddressSpec peer;
	if (! peer.ipSet(cause->smGetProxy(),"incoming SIP message")) {
		return;	// If the peer address is invalid, not much else we can do about it.
	}
	SipMessageReply err(cause,code,string(reason),NULL);
	siWrite(&peer.mipSockAddr,&err);
}



// Return true if the message was handled here.
void MySipInterface::handleInvite(SipMessage *msg, bool isINVITE)
{
	// Get request username (IMSI) from invite request-uri, which is in the first line.
	string toIMSIDigits = msg->smGetInviteImsi(); 	// This is just the numbers.
	if (toIMSIDigits.length() == 0) {
		// FIXME -- Send appropriate error (404) on SIP interface.
		LOG(WARNING) << "Incoming INVITE/MESSAGE with no IMSI:"<<msg;
		newSendEarlyError(msg,404,"Not Found - To header is not an IMSI");
		return; // Message has been handled as much as it ever will be.
	}

	// pat TODO: Convert all this stuff to methods of SipMessage
	// Get the SIP call ID.
	string outboundCallIdNum = msg->smGetCallId();
	if (outboundCallIdNum.empty()) {
		// FIXME -- Send appropriate error on SIP interface.
		LOG(WARNING) << "Incoming INVITE/MESSAGE with no call ID";
		newSendEarlyError(msg,400,"Bad Request");
		return; // Message has been handled as much as it ever will be.
	}

	SipPreposition from = msg->msmFrom; 	// from(msg->msmFromValue);

	// Get the caller ID if it's available.
	string callerId = from.uriUsername(), callerHost = from.uriHostAndPort();
	LOG(DEBUG) << "callerId " << callerId << "@" << callerHost;


	// Check SIP map.  Repeated entry?  Page again.
	//string inboundCallIdNum = outboundCallIdNum;
	// (pat) TODO: This must be locked so we can delete dialogs sometime.
	SipDialogRef existing = findDialogByMsg(msg);
#if PAT_TEST_SIP_DIRECT
	// This is no longer needed...
	//const char *callIdHost = osip_call_id_get_host(msg->omsg()->call_id);	
	//if (isINVITE && sipGetDirectMode() && existing) {
	//	// TODO: Check the invite to see if it comes directly from another Range BTS.
	//	// If it does, the MOC and MTC may be on the same BTS, in which case we need to create
	//	// an extra fifo for the MTC.  The INVITE and the ACK will use this separate fifo.
	//	// TODO: Temporarily assume same BTS:
	//	const char *callerIMSI = extractIMSI(callerId.c_str());
	//	if (callerIMSI && *callerIMSI) {
	//		LOG(DEBUG) << "Calling L3MobileIdentity " << callerIMSI;
	//		L3MobileIdentity callerMobileID(callerIMSI);
	//		LOG(DEBUG) << "after";
	//		//if (TranEntry* transactionOC = gNewTransactionTable.ttFindBySIPCallId(callerMobileID,outboundCallIdNum))
	//		if (TranEntry* transactionOC = existing->findTranEntry()) {
	//			inboundCallIdNum = outboundCallIdNum + string("_TC");
	//			LOG(DEBUG) << "Changing"<<LOGVAR(inboundCallIdNum); 
	//			//if (asprintf(&newCallIdNum,"%s_TC",callIDNum)) {}	// The useless 'if' shuts up gcc.
	//			// Set outbound SIP header of OC transaction to the TC SIPEngine.
	//			transactionOC->getDialog()->setOutboundCallid(callIdHost,inboundCallIdNum.c_str());
	//		}
	//	}
	//}
#endif

	// (pat) Looks for a pending invite still in the queue.  I didnt write this.
	// Check for repeat INVITE, MESSAGE or re-INVITE.  Respond to re-INVITE saying we don't support it. 
	if (existing.self()) {
		WATCH("SIP Message is repeat dialog"<<LOGVAR2("state",existing->getSipState()));
		// sameINVITE checks if it is a duplicate INVITE.  If it is not a duplicate, it is a RE-INVITE.
		if (existing->sameInviteOrMessage(msg)) {
			// This is a duplicate identical INVITE.
			// If the via branch is the same, it is from an impatient SIP server.
			// TODO: If the via branch is different, the INVITE arrived by two different proxy paths,
			// and we are supposed to send "answered elsewhere".


			// Send the duplicate message to the TL, which may resend the current response.
			LOG(DEBUG) << "Sending same invite to existing";
			existing->MTWriteHighSide(msg);

			// If the Dialog is still pending, we want to repage.  For that it is easier to look at the
			// the Transaction side - if there is no transaction the dialog was terminated previously.

			// We want to keep paging this TransactionEntry.  We cant just send a message through the Dialog because
			// there is not yet any L3Procedure started on the TransactionEntry.

			TranEntry* transaction1= existing->findTranEntry();
			if (!transaction1) {
				LOG(DEBUG) << "repeated INVITE/MESSAGE with no transaction record";
			}
			//LOG(INFO) << "pre-existing transaction record: " << transaction1;	// uggh.  This could crash if tran freed here.

			// And if no channel is established yet, page again.  The check for that is handled over in the Control directory.
			// (pat) We could be blocked for several reasons, including paging, waiting for LUR to complete, waiting for channel
			// to change, etc.  But if we are paging, reset the paging timer so we keep paging.
			gMMLayer.mmMTRepage(toIMSIDigits);

			return; // Message has been handled as much as it ever will be.
		} else {
			// This is a re-invite.
			// (pat) TODO: Make a better error message handler in SipDialog.
			/* don't cancel the call */
			LOG(CRIT) << "got reinvite on" <<LOGVAR(existing) << " SIP re-INVITE: " << msg;
			// This message is not in this dialog, so dont use this: existing->MODSendERROR(msg, 488, "Not Acceptable Here", false);
			newSendEarlyError(msg,488,"Not Acceptable Here");
			return; // true because the message has been handled as much as it ever will be.
		}
	}

	// Create the transaction.
	// (pat) Comments:
	// mCallId is in all messages both directions. mCallIdHeader is the header built from mCallId.
	// For MT, callid is decided by the peer; it is set here from the incoming INVITE.
	// For MO, callid is decided by us, and set by calling SIPEngine::user() from the imsi, called by the TranEntry constructor.
	// In both cases mCallIdHeader is set from saveInviteOrMessage.
	// The inbound/outbound callid distinction is for the special case where the same BTS is both.
	// newMT above calls SIPEngine(...,imsi from mobileID) calls sipSetUser(imsi) which sets the mCallId for an MO transaction.
	// Then this SIPUser invocation overwrites mCallId with the callid for an MT transaction.
	//FullMobileId msid;
	//msid.mImsi = toIMSIDigits;

	// Doesnt matter if functions succeed or fail below.  We return true from this function to indicate the SIP messages was handled.

	LOG(INFO) << msg->smGetPrecis() <<LOGVAR2("imsi",toIMSIDigits);
	TranEntry *tran = NULL;
	if (isINVITE) {	// as opposed to MESSAGE
		SipDialog *dialog = SipDialog::newSipDialogMT(SIPDTMTC, msg);

		if (gMMLayer.mmIsBusy(toIMSIDigits)) {
			// There is already a voice transaction running on this imsi.
			// We need to send supplementary services - see 04.80 and 04.83
			//dialog->MTCEarlyError(486,"Busy Here");
			LOG(INFO) << "SIP term info dialogCancel called in handleInvite";
			dialog->dialogCancel(TermCause::Local(L3Cause::User_Busy));
			return;
		}
		// Queue on MM for this IMSI.
		// TODO: createMTTransaction still does messages too.
		tran = dialog->createMTTransaction(msg);
	} else {

		// Create an incipient TranEntry.  It does not have a TI yet.
		FullMobileId msid;
		msid.mImsi = toIMSIDigits;

		// zero length is okay
		string smsBody = msg->smGetMessageBody();
		// Zero length message body is okay at this point
		string smsContentType = msg->smGetMessageContentType();
		if (smsContentType == "") {
			LOG(NOTICE) << "MT-SMS incoming MESSAGE method with no content type (or memory error) for " << msid.mImsi;
			// TODO: Should this be fatal?
		}
		SipDialog *dialog = SipDialog::newSipDialogMT(SIPDTMTSMS, msg);
		tran = TranEntry::newMTSMS(dialog,msid,callerId.c_str(),smsBody,smsContentType);
	}
	gMMLayer.mmAddMT(tran);
}

bool MySipInterface::newCheckInvite(SipMessage *msg)
{
	// Check for INVITE or MESSAGE methods.
	// Check channel availability now, too,
	// even if we are not actually assigning the channel yet.
	if (msg->isINVITE()) {
		gReports.incr("OpenBTS.SIP.INVITE.In");
		handleInvite(msg,true);
		return true;
	} else if (msg->isMESSAGE()) {
		gReports.incr("OpenBTS.SIP.MESSAGE.In");
		handleInvite(msg,false);
		return true;
	} else {

		// Is this a message for an existing INVITE transaction, ie, that part of the INVITE before ACK?
		// The findDialogMsg also finds a matching MESSAGE dialog or REGISTER dialog, but we are subsequently
		// testing against the INVITE via-branch, which will find only INVITE transactions.
		SipDialogRef existing = findDialogByMsg(msg);
		if (existing.self()) {
			// The ACK and CANCEL message are sent to the INVITE server transaction.
			// Other messages (BYE, INFO, etc) would be sent to the TU created for them.
			if (msg->smGetCode() ?  msg->smGetBranch() == existing->mInviteViaBranch : msg->isACK() || msg->isCANCEL()) {
				WATCH("SIP Sending message to existing dialog"<<LOGVAR2("state",existing->getSipState()));
				LOG(DEBUG) << "Sending message to existing dialog"<<LOGVAR(msg->smGetCode()) <<LOGVAR(msg->smGetBranch()) << LOGVAR(existing->mInviteViaBranch) << LOGVAR(msg->smGetMethodName()) << existing->dialogText();
				if (! existing->dgWriteHighSide(msg)) {
					LOG(ERR) << "Confusing SIP message not handled, to dialog:" <<existing->dialogText(false)<< " message was:"<<msg;
				}
				return true;	// We handled it or threw it away.
			}
		}
	}
	return false;
}


// All inbound SIP messages go here for processing.  SVGDBG
void SipInterface::siDrive2() 
{
	// All inbound SIP messages go here for processing.

	LOG(DEBUG) << "blocking on socket";
	int numRead = mSIPSocket->read(mReadBuffer);
	if (numRead<0) {
		LOG(ALERT) << "cannot read SIP socket.";
		return;
	}
	if (numRead<10) {
		LOG(WARNING) << "malformed packet (" << numRead << " bytes) on SIP socket";
		return;
	}
	mReadBuffer[numRead] = '\0';
	if (random()%100 < gConfig.getNum("Test.SIP.SimulatedPacketLoss")) {
		LOG(NOTICE) << "simulating dropped inbound SIP packet: " << mReadBuffer;
		return;
	}

	newDriveIncoming(mReadBuffer);
}

// This is the thread that processes all in comming SIP messages
static void driveLoop2( MySipInterface * si)
{
	while (! gBTS.btsShutdown()) {
		si->siDrive2();
	}
}

// (pat) Every now and then check every SipDialog engine for SIP timer expiration.
static void periodicServiceLoop(MySipInterface *si)
{
	while (! gBTS.btsShutdown()) {
		si->tuMapPeriodicService();
		si->dmPeriodicService();
		si->purgeDeadDialogs();
		// This timing is entirely non-critical, so dont bother to compute the exact next timeout,
		// just delay a while and retry.
		// Implicit assumption that Timer.E < Timer.F
		unsigned howlong = gConfig.getNum("SIP.Timer.E")/2;
		if (howlong < 250) { howlong = 250; }	// Dont eat all the cpu cycles if someone accidently sets this too low.
		msleep(howlong);
	}
}

MySipInterface gSipInterface;	// Here it is.

// Pat added to hook messages from the ORTP library.  See ortp_set_log_handler in ortp.c.
extern "C" {
	static void ortpLogFunc(OrtpLogLevel /*lev unused*/, const char *fmt, va_list args)
	{
		// This floods the system with error messages, so regulate output to the console.
		static time_t lasttime = 0;	// No more than one message per minute.
		char buf[202];
		vsnprintf(buf,200,fmt,args);
		time_t now = time(NULL);
		if (now - lasttime > 60) {
			lasttime = now;
			// This used to have a higher priority message.
			// I demoted them both to NOTICE because this code seems to be working fine, and it is invoked
			// every time the MS gets an in-call SMS.
			LOG(NOTICE) << "RTP library:"<<buf;
		} else {
			LOG(NOTICE) << "RTP library:"<<buf;
		}
	}
}

void SipInterface::siInit()
{
	// We use random() alot in here for SIP tags, CSeq number starting points.
	// You MUST call srandom first or the numbers returned by random are deterministic, which will result in collisions
	// between "random" numbers on different BTS that are using identical random number sequences.
	struct timeval now;
	gettimeofday(&now,NULL);
	srandom(now.tv_usec);

	mSIPSocket = new UDPSocket(gConfig.getNum("SIP.Local.Port"));
}


void MySipInterface::msiInit()
{
	siInit();
	// Start the ortp stuff. 
	ortp_init();
	ortp_scheduler_init();
	// FIXME -- Can we coordinate this with the global logger?
	//ortp_set_log_level_mask(ORTP_MESSAGE|ORTP_WARNING|ORTP_ERROR);

	ortp_set_log_handler(ortpLogFunc);

	mDriveThread.start((void *(*)(void*))driveLoop2, &gSipInterface );
	mPeriodicServiceThread.start((void *(*)(void*))periodicServiceLoop, &gSipInterface );
}


void SIPInterfaceStart()
{
	gSipInterface.msiInit();
}


};	// namespace SIP


// vim: ts=4 sw=4
