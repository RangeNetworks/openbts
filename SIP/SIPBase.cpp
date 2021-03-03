/**@file SIP Base -- SIP IETF RFC-3261, RTP IETF RFC-3550. */
/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
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



#define LOG_GROUP LogGroup::SIP		// Can set Log.Level.SIP for debugging

#include <stdio.h>
#include <stdlib.h>
#include <iostream>

#include <sys/types.h>
#include <semaphore.h>

#include <Logger.h>
#include <Timeval.h>
#include <Reporting.h>
#include <OpenBTSConfig.h>

#include "SIPMessage.h"
#include "SIPParse.h"	// For SipParam
#include "SIPBase.h"
#include "SIP2Interface.h"
#include "SIPUtility.h"
#include "SIPRtp.h"
#include "config.h"		// For VERSION

#undef WARNING

int gCountSipDialogs = 0;

namespace SIP {
using namespace std;


// These need to be declared here instead of in the header because of interaction with InterthreadQueue.h.
SipEngine::~SipEngine() {}
SipEngine::SipEngine()	 { mTranId = 0; }

void SipBaseProtected::_define_vtable() {}
//void DialogMessage::_define_vtable() {}


const char* SipStateString(SipState s)
{
	switch(s)
	{	
		case SSNullState: return "NullState";
		case SSTimeout: return "Timeout";
		case Starting: return "Starting";
		case Proceeding: return "Proceeding";
		case Ringing: return "Ringing";
		case Connecting: return "Connecting";
		case Active: return "Active";
		case SSFail: return "SSFail"; 
		case MOCBusy: return "MOCBusy";
		case MODClearing: return "MODClearing";
		case MODCanceling: return "MODCanceling";
		case MODError: return "MODError";
		case MTDClearing: return "MTDClearing";
		case MTDCanceling: return "MTDCanceling";
		case Canceled: return "Canceled";
		case Cleared: return "Cleared";
		//case SipRegister: return "SipRegister";
		//case SipUnregister: return "SipUnregister";
		case MOSMSSubmit: return "SMS-Submit";
		case HandoverInbound: return "HandoverInbound";
		//case HandoverInboundReferred: return "HandoverInboundReferred";
		case HandoverOutbound: return "HandoverOutbound";
		//default: return NULL;
	}
	return NULL;
}

ostream& operator<<(ostream& os, SipState s)
{
	const char* str = SipStateString(s);
	if (str) os << str;
	else os << "?" << ((int)s) << "?";
	return os;
}

// Should we cancel a transaction based on the SIP state?
bool SipBaseProtected::sipIsStuck() const
{
	switch (vgetDialogType()) {
		case SIPDTUndefined:
			LOG(ERR) << "undefined dialog type?"; return false;
		case SIPDTRegister:
		case SIPDTUnregister:
			return false;	// The registrar dialogs are immortal.
		default:
			break;	// Call and Message dialogs should either proceed to Active or eventually die on their own.
	}
	unsigned age = mStateAge.elapsed();
	// These ages were copied from the old pre-l3-rewrite code in TransactionTable.cpp
	switch (getSipState()) {
		case Active:
			return false;		// Things are copascetic. Let this SIP dialog run.
		case SSFail:
		case HandoverInbound:
		case Proceeding:
		case Canceled:
		case Cleared:
			// Stuck in these states longer than 30 seconds?  Grounds for terminating the transaction.
			return age > 30*1000;
		default:
			// Stuck in any other state longer than 180 seconds?  Grounds for terminating the transaction.
			return age > 180*1000;
	}
}

static unsigned nextInviteCSeqNum() 	// formerly: random()%600;
{
	static unsigned seqNum = 0;
	if (seqNum == 0) {
		seqNum = random() & 0xfffff;	// Any old number will do.
	}
	return ++seqNum;
}

DialogStateVars::DialogStateVars()
{
	// generate a tag now.
	// (pat) It identifies our side of the SIP dialog to the peer.
	// It is placed in the to-tag for both responses and requests.
	// For responses we munge the saved INVITE immediately so all responses use this to-tag.
	//mLocalTag=make_tag();

	// set our CSeq in case we need one
	// (pat 8-1-2014) We do not want to contaminate class SipBase with an external reference to gSipInterface 
	//mLocalCSeq = gSipInterface.nextInviteCSeqNum(); 	// formerly: random()%600;
	mLocalCSeq = nextInviteCSeqNum(); 	// formerly: random()%600;
}

string DialogStateVars::dsToString() const
{
	ostringstream ss;
	ss << LOGVARM(mCallId);
	ss << LOGVARM(mLocalHeader.value())<<LOGVARM(mRemoteHeader.value());
	ss << LOGVARM(mLocalCSeq) << LOGVARM(mRouteSet);
	ss << LOGVAR2("proxy",mProxy.ipToText());
	return ss.str();
}

void DialogStateVars::updateProxy(const char *sqlOption)
{
	string proxy = gConfig.getStr(sqlOption);
	if (! proxy.empty() && ! mProxy.ipSet(proxy,sqlOption)) {
		LOG(ALERT) << "cannot resolve IP address for"<<LOGVAR(proxy)<<" specified by"<<LOGVAR(sqlOption);
	}
}

static unsigned gDialogId = 1;

// (pat) This constructor performs only base initialization.
// Final initialization is in the dialog constructor.
// For MO the final init of mRemote... is in sendMessage or sendInvite, which is called from the dialog constructor.
void SipBase::SipBaseInit(DialogType wDialogType, string proxy, const char * proxyProvenance) // , TranEntryId wTranId)
{
	//mTranId = 0;
	mDialogId = gDialogId++;
	mLastResponse = NULL;
	mInvite = NULL;
	mDialogType = wDialogType;
	//mTranId = wTranId;

	if (! mProxy.ipSet(proxy,proxyProvenance)) {
		LOG(ALERT) << "cannot resolve IP address for " << proxy << sbText();
	}
}

SipBase::~SipBase()
{
	if (mInvite) { delete mInvite; }
	mInvite = NULL;
	gCountSipDialogs--;
}

void SipBase::sbText(std::ostringstream&os) const
{
	os <<" SipBase(";
	//os  <<LOGVARM(mTranId);
	os <<LOGVAR2("localUsername",sipLocalUsername()) << LOGVAR2("remoteUsername",sipRemoteUsername());
	os <<LOGVARM(mInviteViaBranch);
	os << " DialogStateVars=(" << dsToString() << ")";

	// Add the first line of the last response.
	if (mLastResponse) { os << LOGVARM(mLastResponse); }

	os <<LOGVAR2("SipState",getSipState());
	os <<LOGVARM(mDialogType);
	os <<")";
}

string SipBase::sbText() const
{
	std::ostringstream ss;
	sbText(ss);
	return ss.str();
}



void SipBase::saveInviteOrMessage(const SipMessage *INVITE, bool /*mine*/)
{
	mInvite = new SipMessage();
	*mInvite = *INVITE;
}



// This can only be called for MO responses: MOC, MOD.
void SipBase::saveMOResponse(SipMessage *response)
{
	// This is used only for sdp, and only for the invite response, so we should just save that...
	mLastResponse = new SipMessage;
	*mLastResponse = *response;

	// TODO: Multipart messages.
	if (response->msmContentType == "application/sdp") {
		mSdpAnswer = response->msmBody;
	}

	// The To: is the remote party and might have a new tag.
	// (pat) But only for a response to one of our requests. 
	dsSetRemoteHeader(&response->msmTo);
	if (response->msmCSeqMethod == "INVITE") {
		// Handle the Contact.
		// If the INVITE is canceled early we get a 487 error which does not have a Contact.
		// TODO: Handle the routeset.
		string uristr;
		if (! response->msmContactValue.empty() && crackUri(response->msmContactValue,NULL,&uristr,NULL)) {
			SipUri uri(uristr);
			mProxy.ipSet(uri.uriHostAndPort(), "INVITE Contact");
		}
	}
}


void DialogStateVars::dsSetLocalMO(const FullMobileId &msid, bool addTag)
{
	// We dont really have to check for collisions; our ids were vanishingly unlikely to collide
	// before and now the time is encoded into it as well.
	//string callid;
	//do { callid = globallyUniqueId(""); } while (gSipInterface.findDialogsByCallId(callid,false));
	dsSetCallId(globallyUniqueId(""));

	string username;
	{
		// IMSI gets prefixed with "IMSI" to form a SIP username
		username = msid.fmidUsername();
	}
	dsSetLocalUri(makeUri(username,localIPAndPort()));
	//mSipUsername = username;
	LOG(DEBUG) << "set user MO"<<LOGVAR(msid)<<LOGVAR(username)<<LOGVARM(mCallId) << dsToString();
	if (addTag) {
		mLocalHeader.setTag(make_tag());
	}
}

// We always need a local tag, but handover adds its own.
void DialogStateVars::dsSetLocalHeaderMT(SipPreposition *toheader, bool addTag)
{
	LOG(DEBUG);
	// We will parse the contact to see if there is already a tag, which would be an error.
	mLocalHeader = *toheader;
	if (addTag) {
		if (!mLocalHeader.mTag.empty()) {
			LOG(ERR) <<"MT Dialog initiation had a pre-existing to-tag:"<<toheader;
			// Now what?  Keep the tag I guess.
			// The dialog is probably impossible.
		} else {
			mLocalHeader.setTag(make_tag());
		}
	}
}


// Like this:  Note no via branch, but registrar added one.
// REGISTER sip:127.0.0.1 SIP/2.0^M		// this is the proxy IP
// Via: SIP/2.0/UDP 127.0.0.1:5062;branch^M
// From: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1>;tag=vzmikpkffdomsjvb^M
// To: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1>^M
// Call-ID: 196117285@127.0.0.1^M
// CSeq: 226 REGISTER^M
// Contact: <sip:IMSI001010002220002@127.0.0.1:5062>;expires=5400^M
// User-Agent: OpenBTS 3.0TRUNK Build Date May  3 2013^M
// Max-Forwards: 70^M
// P-PHY-Info: OpenBTS; TA=1 TE=0.392578 UpRSSI=-26.000000 TxPwr=33 DnRSSIdBm=-111^M
// P-Access-Network-Info: 3GPP-GERAN; cgi-3gpp=0010103ef000a^M
// P-Preferred-Identity: <sip:IMSI001010002220002@127.0.0.1:5060>^M
// Content-Length: 0^M
// 
// SIP/2.0 401 Unauthorized^M
// Via: SIP/2.0/UDP localhost:5064;branch=1;received=string_address@foo.bar^M
// Via: SIP/2.0/UDP 127.0.0.1:5062;branch^M
// From: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1>;tag=vzmikpkffdomsjvb^M
// To: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1>^M
// Call-ID: 196117285@127.0.0.1^M
// CSeq: 226 REGISTER^M
// Contact: <sip:IMSI001010002220002@127.0.0.1:5062>;expires=5400^M
// WWW-Authenticate: Digest  nonce=972ed867f7284fca8670e44a71f97040^M
// User-agent: OpenBTS 3.0TRUNK Build Date May  3 2013^M
// Max-forwards: 70^M
// P-phy-info: OpenBTS; TA=1 TE=0.392578 UpRSSI=-26.000000 TxPwr=33 DnRSSIdBm=-111^M
// P-access-network-info: 3GPP-GERAN; cgi-3gpp=0010103ef000a^M
// P-preferred-identity: <sip:IMSI001010002220002@127.0.0.1:5060>^M
// Content-Length: 0^M
// 
// REGISTER sip:127.0.0.1 SIP/2.0^M
// Via: SIP/2.0/UDP 127.0.0.1:5062;branch^M
// From: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1>;tag=yyourwrzymenfffa^M
// To: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1>^M
// Call-ID: 2140400952@127.0.0.1^M
// CSeq: 447 REGISTER^M
// Contact: <sip:IMSI001010002220002@127.0.0.1:5062>;expires=5400^M
// Authorization: Digest, nonce=83775658971b9dc469ad88c3c3d5250b, uri=001010002220002, response=4f9eb260^M
// User-Agent: OpenBTS 3.0TRUNK Build Date May  3 2013^M
// Max-Forwards: 70^M
// P-PHY-Info: OpenBTS; TA=1 TE=0.423828 UpRSSI=-63.000000 TxPwr=13 DnRSSIdBm=-48^M
// P-Access-Network-Info: 3GPP-GERAN; cgi-3gpp=0010103ef000a^M
// P-Preferred-Identity: <sip:IMSI001010002220002@127.0.0.1:5060>^M
// Content-Length: 0^M



// A request inside an invite is ACK, CANCEL, BYE, INFO, or re-INVITE.
// The only target-refresh-request is re-INVITE, which can change the "Dialog State".
// For ACK sec 17.1.1.3 the To must equal the To of the response being acked, which specifically contains a tag.
// ACK contains only one via == top via of original request with matching branch.
// TODO: ACK must copy route header fields from INVITE.
// sec 9.1 For CANCEL, all fields must == INVITE except the method.  Note CSeq must match too.  Must have single via
// matching the [invite] request being cancelled, with matching branch.  TODO: Must copy route header from request.
// 15.1.1: BYE is a new request within a dialog as per section 12.2.
// transaction constructed as per with a new tag, branch, etc.
// It should not include CONTACT because it is non-target-refresh.
// Implicit parameters from SipBase: mCallId, mRemoteUsername, mRemoteDomain, mCSeq, etc.
SipMessage *SipBase::makeRequest(string method,string requestUri, string whoami, SipPreposition*toHeader, SipPreposition*fromHeader, string branch)
{
	LOG(INFO) << "SIP term info makeRequest: " << method;
	SipMessage *invite = new SipMessage();
	invite->smInit();
	invite->msmReqMethod = method;
	invite->msmCallId = this->mCallId;
	//string toContact = makeUri(mRemoteUsername,mRemoteDomain);	// dialed_number@remote_ip
	//string fromContact = makeUri(mSipUsername,localIP());
	//invite->msmReqUri = makeUri(mRemoteUsername,mRemoteDomain);
	invite->msmReqUri = requestUri;
	invite->smAddViaBranch(this,branch);
	// invite->copyTermCausetoMessage(this);  // SVGDBG not needed for this message type ??
	invite->msmCSeqMethod = invite->msmReqMethod;
	invite->msmCSeqNum = mLocalCSeq;		// We dont need to advance for an initial request; caller advances if necessary.

	string realm = gConfig.getStr("SIP.Realm");
	if (realm.length() > 0) {
		string tousername = toHeader->uriUsername();
		string fromusername = fromHeader->uriUsername();
		string toTag = toHeader->getTag();
		string fromTag = fromHeader->getTag();
		string toUriString = makeUri(tousername ,realm,0);
		string fromUriString = makeUri(fromusername ,realm,0);
		invite->msmTo = SipPreposition("",toUriString, toTag );
		invite->msmFrom = SipPreposition("",fromUriString, fromTag );
	} else {
		invite->msmTo = *toHeader;
		invite->msmFrom = *fromHeader;
	}

	invite->msmContactValue = localContact(whoami);
	string prefid = this->preferredIdentity(whoami);
	static const string cPreferredIdentityString("P-Preferred-Identity");
	invite->smAddHeader(cPreferredIdentityString,prefid);

	// The caller has not added the content yet: saveInviteOrMessage(invite,true);
	return invite;
}

// This is an *initial* request only, for INVITE or MESSAGE.
// This version generates the request from the values in the DialogStateVars.
SipMessage *SipBase::makeInitialRequest(string method)
{
	string requestUri = dsRemoteURI();
	this->mInviteViaBranch = make_branch();
	return makeRequest(method,requestUri,sipLocalUsername(),&mRemoteHeader,&mLocalHeader,this->mInviteViaBranch);
}




bool SipBase::dgIsInvite() const
{
	SipMessage *invite = getInvite();
	return invite && invite->isINVITE();
}

// Are these the same dialogs?
bool SipBase::sameDialog(SipBase *other)
{
	if (this->callId() != other->callId()) { return false; }
	if (this->dsLocalTag() != other->dsLocalTag()) { return false; }
	if (this->dsRemoteTag() != other->dsRemoteTag()) { return false; }
	// Good enough.
	return true;
}

// Does this incoming message want to be processed by this dialog?
// NOTE: This is temporary until we fully support SipTransactions, in which case this will be
// used only for in-dialog requests, and in that case both to- and from-tags must match.
// This may not be correct for group calls - we may want to search each of several possible
// matching dialogs for the transaction matching the via-branch.
bool SipBase::matchMessage(SipMessage *msg)
{
	// The caller already checked the callid, but lets do it again in case someone modifies the code later.
	if (msg->smGetCallId() != this->callId()) { return false; }
	// This code could be simplified by combining logic, but I left it verbose for clarity
	if (msg->isRequest()) {
		// If it is a request sent by the peer, the remote tag must match.  Both empty is ok.
		if (msg->smGetRemoteTag() != this->dsRemoteTag()) { return false; }
		// The local tag in the message is either empty (meaning the peer has not received it yet) or matches.
		string msgLocalTag = msg->smGetLocalTag();
		if (! msgLocalTag.empty()) {
			if (msgLocalTag != this->dsLocalTag()) { return false; }
		}
	} else {
		// If it is a reply, it means the original request was sent by us.  The local tags must match.  Both empty is ok.
		if (msg->smGetLocalTag() != this->dsLocalTag()) { return false; }
		// The remote tag in the dialog is either empty (has not been set yet, will probably be set by this message), or matches.
		string remoteTag = dsRemoteTag();
		if (! remoteTag.empty()) {
			if (remoteTag != msg->smGetRemoteTag()) { return false; }
		}
	}
	return true;	// Good enough.
}


// 17.2.3 tells how to match requests to server transactions, but that does not apply to this.

/* return true if this is exactly the same invite (not a re-invite) as the one we have stored */
bool SipBase::sameInviteOrMessage(SipMessage * msg)
{
	ScopedLock lock(mDialogLock,__FILE__,__LINE__);	// probably unnecessary.
	assert(getInvite());
	if (NULL == msg){
		LOG(NOTICE) << "trying to compare empty message" <<sbText();
		return false;
	}
	// We are assuming that the callids match.
	// Otherwise, this would not have been called.
	// FIXME -- Check callids and assrt if they down match.
	// So we just check the CSeq.
	// FIXME -- Check all of the pointers along these chains and log ERR if anthing is missing.
	return sameMsg(msg,getInvite());
}

static string encodeSpaces(string str)
{
	char *buf = (char*)alloca(str.size()+2), *bp;
	const char *sp = str.c_str(), *esp = str.c_str() + str.size();
	for (bp = buf; sp < esp; bp++, sp++) {
		*bp = (*sp == ' ') ? '\t' : *sp;
	}
	return string(buf,str.size());		// we did not copy the trailing nul so must specify size.
}

// This message is created on BTS1 to send to BTS2.
string SipBase::dsHandoverMessage(string peer) const
{
	SipMessage *msg = new SipMessageHandoverRefer(this,peer);
	string str = msg->smGenerate(OpenBTSUserAgent());
	//LOG(DEBUG) <<LOGVAR(str);
	// We are temporarily sending this over the peering interface in a string of space-separated parameters.
	// So to get rid of the spaces, replace them with tabs.
	string result= encodeSpaces(str);
	//LOG(DEBUG) <<LOGVAR(result);
	return result;
}



SipCallbacks::ttAddMessage_functype SipCallbacks::ttAddMessage_callback = (SipCallbacks::ttAddMessage_functype) 0;

void SipCallbacks::ttAddMessage(TranEntryId tranid,SIP::DialogMessage *dmsg) {
	if (ttAddMessage_callback) { ttAddMessage_callback(tranid,dmsg); }
}

SipCallbacks::writePrivateHeaders_functype SipCallbacks::writePrivateHeaders_callback = (SipCallbacks::writePrivateHeaders_functype) 0;

void SipCallbacks::writePrivateHeaders(SipMessage *msg, const L3LogicalChannel *l3chan) {
	if (writePrivateHeaders_callback) { writePrivateHeaders_callback(msg,l3chan); }
}

string OpenBTSUserAgent()
{
	// FIXME: This is broken...
	static const char* userAgent1 = "OpenBTS " VERSION " Build Date " TIMESTAMP_ISO;
	string userAgent = string(userAgent1);
	return userAgent;
}

};
// vim: ts=4 sw=4
