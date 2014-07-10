/**@file SIP Base -- SIP IETF RFC-3261, RTP IETF RFC-3550. */
/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2011, 2012, 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribuion.
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

#include <ortp/telephonyevents.h>

#include <config.h>
#include <Logger.h>
#include <Timeval.h>
#include <GSMConfig.h>
#include <ControlTransfer.h>
#include <GSMCommon.h>
#include <GSMLogicalChannel.h>
#include <Reporting.h>
#include <Globals.h>
#include <L3TranEntry.h>	// For HandoverEntry

#include "SIPMessage.h"
#include "SIPParse.h"	// For SipParam
#include "SIPBase.h"
#include "SIPDialog.h"
#include "SIP2Interface.h"
#include "SIPUtility.h"

#undef WARNING

int gCountRtpSessions = 0;
int gCountRtpSockets = 0;
int gCountSipDialogs = 0;

namespace SIP {
using namespace std;
using namespace Control;

const bool rtpUseRealTime = true;	// Enables a bug fix for the RTP library.

bool gPeerIsBuggySmqueue = true;

// These need to be declared here instead of in the header because of interaction with InterthreadQueue.h.
SipEngine::~SipEngine() {}
SipEngine::SipEngine()	 { mTranId = 0; }

void SipBaseProtected::_define_vtable() {}
void DialogMessage::_define_vtable() {}


string makeUriWithTag(string username, string ip, string tag)
{
	return format("<sip:%s@%s>;tag=%s",username,ip,tag);
}
string makeUri(string username, string ip, unsigned port)
{
	if (port) {
		return format("sip:%s@%s:%u",username,ip,port);
	} else {
		return format("sip:%s@%s",username,ip);
	}
}

static int get_rtp_tev_type(char dtmf){
        switch (dtmf){
                case '1': return TEV_DTMF_1;
                case '2': return TEV_DTMF_2;
                case '3': return TEV_DTMF_3;
                case '4': return TEV_DTMF_4;
                case '5': return TEV_DTMF_5;
                case '6': return TEV_DTMF_6;
                case '7': return TEV_DTMF_7;
                case '8': return TEV_DTMF_8;
                case '9': return TEV_DTMF_9;
                case '0': return TEV_DTMF_0;
                case '*': return TEV_DTMF_STAR;
                case '#': return TEV_DTMF_POUND;
                case 'a':
                case 'A': return TEV_DTMF_A;
                case 'B':
                case 'b': return TEV_DTMF_B;
                case 'C':
                case 'c': return TEV_DTMF_C;
                case 'D':
                case 'd': return TEV_DTMF_D;
		case '!': return TEV_FLASH;
                default:
		    LOG(WARNING) << "Bad dtmf: " << dtmf;
		    return -1;
                }
}


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
	switch (getDialogType()) {
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

void SipRtp::rtpStop()
{
	if (mSession) {
		RtpSession *save = mSession;
		mSession = NULL;		// Prevent rxFrame and txFrame from using it, which is overkill because we dont call this until the state is not Active.
		rtp_session_destroy(save);
		gCountRtpSessions--;
		gCountRtpSockets--;
	}
}

void SipRtp::rtpInit()
{
	mSession = NULL; 
	mTxTime = 0;
	mRxTime = 0;
	mRxRealTime = 0;
	mTxRealTime = 0;
	mDTMF = 0;
	mDTMFDuration = 0;
	mDTMFEnding = 0;
	mRTPPort = 0; 	//to make sure noise doesn't magically equal a valid RTP port
}

DialogStateVars::DialogStateVars()
{
	// generate a tag now.
	// (pat) It identifies our side of the SIP dialog to the peer.
	// It is placed in the to-tag for both responses and requests.
	// For responses we munge the saved INVITE immediately so all responses use this to-tag.
	//mLocalTag=make_tag();

	// set our CSeq in case we need one
	mLocalCSeq = gSipInterface.nextInviteCSeqNum(); 	// formerly: random()%600;
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

static unsigned gDialogId = 1;

// (pat) This constructor preforms only base initialization.
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

void SipRtp::rtpText(std::ostringstream&os) const
{
	os <<LOGVARM(mTxTime) << LOGVARM(mRxTime);
	//os <<LOGVAR(mRTPRemIP);
	os <<LOGVAR(mRTPPort);
	os <<LOGVARM(mCodec);
	// warning: The unbelievably stupid << sends the mDTMS char verbatim, and it is 0, which prematurely terminates the string.
	unsigned dtmf = mDTMF;
	os <<LOGVAR(dtmf) <<LOGVARM(mDTMFDuration)<<LOGVARM(mDTMFStartTime);
}

void SipBase::sbText(std::ostringstream&os, bool verbose) const
{
	os <<" SipBase(";
	//os  <<LOGVARM(mTranId);
	os <<LOGVAR2("localUsername",sipLocalUsername()) << LOGVAR2("remoteUsername",sipRemoteUsername());
	os <<LOGVARM(mInviteViaBranch);
	os << " DialogStateVars=(" << dsToString() << ")";

	// Add the first line of the last response.
	if (mLastResponse) { os << LOGVARM(mLastResponse); }

	if (IS_LOG_LEVEL(DEBUG)) verbose = true;
	if (verbose) {
		rtpText(os);
		//os << "proxy=(" << mProxy.ipToText() << ")";
	}

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
	mLastResponse = new SipMessage();
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
	
void writePrivateHeaders(SipMessage *msg, const L3LogicalChannel *l3chan)
{
	// P-PHY-Info
	// This is a non-standard private header in OpenBTS.
	// TODO: If we add the MSC params to this, especially L3TI, the SIP message will completely encapsulate handover.
	// TA=<timing advance> TE=<TA error> UpRSSI=<uplink RSSI> TxPwr=<MS tx power> DnRSSIdBm=<downlink RSSI>
	// Get the values.
	if (l3chan) {
		char phy_info[200];
		// (pat) TODO: This is really cheating.
		const GSM::L2LogicalChannel *chan = l3chan->getL2Channel();
		MSPhysReportInfo *phys = chan->getPhysInfo();
		snprintf(phy_info,200,"OpenBTS; TA=%d TE=%f UpRSSI=%f TxPwr=%d DnRSSIdBm=%d time=%9.3lf",
			phys->actualMSTiming(), phys->timingError(),
			phys->RSSI(), phys->actualMSPower(),
			chan->measurementResults().RXLEV_FULL_SERVING_CELL_dBm(),
			phys->timestamp());
		static const string cPhyInfoString("P-PHY-Info");
		msg->smAddHeader(cPhyInfoString,phy_info);
	}

	// P-Access-Network-Info
	// See 3GPP 24.229 7.2.  This is a genuine specified header.
	char cgi_3gpp[256];
	snprintf(cgi_3gpp,256,"3GPP-GERAN; cgi-3gpp=%s%s%04x%04x",
		gConfig.getStr("GSM.Identity.MCC").c_str(),gConfig.getStr("GSM.Identity.MNC").c_str(),
		(unsigned)gConfig.getNum("GSM.Identity.LAC"),(unsigned)gConfig.getNum("GSM.Identity.CI"));
	static const string cAccessNetworkInfoString("P-Access-Network-Info");
	msg->smAddHeader(cAccessNetworkInfoString, cgi_3gpp);
 
	// FIXME -- Use the subscriber registry to look up the E.164
	// and make a second P-Preferred-Identity header.
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



// We wrap our REGISTER messages inside a dialog object, even though it is technically not a dialog.
SipMessage *SipBase::makeRegisterMsg(DialogType wMethod, const L3LogicalChannel* chan, string RAND, const FullMobileId &msid, const char *SRES)
{
	// TODO: We need to make a transaction here...
	static const string registerStr("REGISTER");
	// The request URI is special for REGISTER;
	string reqUri = string("sip:") + proxyIP();
	// We formerly allocated a new Dialog for each REGISTER message so the IMSI was stashed in the dialog, and localSipUri() worked.
	//SipPreposition myUri(localSipUri());
	string username = msid.fmidUsername();
	// RFC3261 is somewhat contradictory on the From-tag and To-tag.
	// The documentation for 'from' says the from-tag is always included.
	// The examples in 24.1 show a From-tag but no To-tag.
	// The To-tag includes the optional <>, and Paul at null team incorrectly thought the <> were required,
	// so we will include them as that appears to be common practice.

	string myUriString;
	string authUri;
	string authUsername;
	string realm = gConfig.getStr("SIP.Realm");
	if (realm.length() > 0) {
		authUri = string("sip:") + realm;
		authUsername = string("IMSI") + msid.mImsi;
		myUriString = makeUri(username,realm,0);
	} else {
		myUriString = makeUri(username,dsPeer()->mipName,0);	// The port, if any, is already in mipName.
	}

	//string fromUriString = makeUriWithTag(username,dsPeer()->mipName,make_tag());	// The port, if any, is already in mipName.
	SipPreposition toHeader("",myUriString,"");
	SipPreposition fromHeader("",myUriString,make_tag());
	dsNextCSeq();	// Advance CSeqNum.
	SipMessage *msg = makeRequest(registerStr,reqUri,username,&toHeader,&fromHeader,make_branch());

	// Replace the Contact header so we can set the expires option.  What a botched up spec.
	// Replace the P-Preferred-Identity
	unsigned expires;
	if (wMethod == SIPDTRegister ) {
		expires = 60*gConfig.getNum("SIP.RegistrationPeriod");
		if (SRES && strlen(SRES)) {
			if (realm.length() > 0) {
				string response = makeResponse(authUsername, realm, SRES, registerStr, authUri, RAND);
				msg->msmAuthorizationValue = format("Digest realm=\"%s\", username=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\", algorithm=MD5 ",
					realm.c_str(), authUsername.c_str(), RAND.c_str(), authUri.c_str(), response.c_str());
			} else {
				msg->msmAuthorizationValue = format("Digest, nonce=%s, uri=%s, response=%s",RAND.c_str(),msid.mImsi.c_str(),SRES);
			}
		}
	} else if (wMethod == SIPDTUnregister ) {
		expires = 0;
	} else { assert(0); }
	// We use myURI instead of localContact because the SIPDialog for REGISTER is shared by all REGISTER
	// users and does not contain the personal info for this user.
	//msg->msmContactValue = format("<%s>;expires=%u",myUriString.c_str(),expires);
	msg->msmContactValue = localContact(username,expires);
	writePrivateHeaders(msg,chan);
	return msg;
}




string SipBase::makeSDPOffer()
{
	SdpInfo sdp;
	sdp.sdpInitOffer(this);
	return sdp.sdpValue();
	//return makeSDP("0","0");
}

// mCodec is an implicit parameter, consisting of the chosen codec.
string SipBase::makeSDPAnswer()
{
	SdpInfo answer;
	answer.sdpInitOffer(this);
	mSdpAnswer = answer.sdpValue();
	return mSdpAnswer;
}


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
	SipMessage *invite = new SipMessage();
	invite->msmReqMethod = method;
	invite->msmCallId = this->mCallId;
	//string toContact = makeUri(mRemoteUsername,mRemoteDomain);	// dialed_number@remote_ip
	//string fromContact = makeUri(mSipUsername,localIP());
	//invite->msmReqUri = makeUri(mRemoteUsername,mRemoteDomain);
	invite->msmReqUri = requestUri;
	invite->smAddViaBranch(this,branch);
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

void SipMOInviteClientTransactionLayer::MOUssdSendINVITE(string ussd, const L3LogicalChannel *chan)
{
	static const char* xmlUssdTemplate = 
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<ussd-data>\n"
		" <language>en</language>\n"
		" <ussd-string>%s</ussd-string>\n"
		"</ussd-data>\n";

	LOG(INFO) << "user " << sipLocalUsername() << " state " << getSipState() <<sbText();

	static const string cInviteStr("INVITE");
	SipMessage *invite = makeInitialRequest(cInviteStr);
	// This is dumber than snot.  We have to put in a dummy sdp with port 0.
	mRTPPort = 0;
	invite->smAddBody(string("application/sdp"),makeSDPOffer());

	// Add RFC-4119 geolocation XML to content area, if available.
	// TODO: This makes it a multipart message, needs to be tested.
	string xml = format(xmlUssdTemplate,ussd);
	invite->smAddBody(string("application/vnd.3gpp.ussd+xml"),xml);

	writePrivateHeaders(invite,chan);
	moWriteLowSide(invite);
	LOG(DEBUG) <<LOGVAR(invite);
	delete invite;
	setSipState(Starting);
}

//old args: const char * calledUser, const char * calledDomain, short rtpPort, Control::CodecSet codec,
void SipMOInviteClientTransactionLayer::MOCSendINVITE(const L3LogicalChannel *chan)
{
	static const char* xmlGeoprivTemplate = 
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		 "<presence xmlns=\"urn:ietf:params:xml:ns:pidf\"\n"
			"xmlns:gp=\"urn:ietf:params:xml:ns:pidf:geopriv10\"\n"
			"xmlns:gml=\"urn:opengis:specification:gml:schema-xsd:feature:v3.0\"\n"
			"entity=\"pres:%s@%s\">\n"
		  "<tuple id=\"1\">\n"
		   "<status>\n"
			"<gp:geopriv>\n"
			 "<gp:location-info>\n"
			  "<gml:location>\n"
			   "<gml:Point gml:id=\"point1\" srsName=\"epsg:4326\">\n"
				"<gml:coordinates>%s</gml:coordinates>\n"
			   "</gml:Point>\n"
			  "</gml:location>\n"
			 "</gp:location-info>\n"
			 "<gp:usage-rules>\n"
			  "<gp:retransmission-allowed>no</gp:retransmission-allowed>\n"
			 "</gp:usage-rules>\n"
			"</gp:geopriv>\n"
		   "</status>\n"
		  "</tuple>\n"
		 "</presence>\n";

	LOG(INFO) << "user " << sipLocalUsername() << " state " << getSipState() <<sbText();
#if PAT_TEST_SIP_DIRECT
	// (pat 7-23-2013): This code has eroded beyond recoverability...
	//bool directBtsConnection = false;
	// exten => _X.,1,Set(Name=${ODBC_SQL(select dial from dialdata_table where exten=\"${EXTEN}\")})
	// exten => _X.,n,GotoIf($["${Name}"=""] ?other-lines,${EXTEN},1)
	// exten => _X.,n,Set(IPAddr=${ODBC_SQL(select ipaddr from sip_buddies where username=\"${Name}\")})
	// exten => _X.,n,GotoIf($["${IPAddr}"=""] ?other-lines,${EXTEN},1)
	// exten => _X.,n,Set(Port=${ODBC_SQL(select port from sip_buddies where username=\"${Name}\")})
	// exten => _X.,n,GotoIf($["${Port}"!=""] ?dialNum)
	// exten => _X.,n,Set(Port=5062) ; Port was not set, so set to default. Gets around bug in subscriberRegistry
	// exten => _X.,n(dialNum),Dial(SIP/${Name}@${IPAddr}:${Port})
	if (gConfig.getStr("SIP.Proxy.Mode") == string("direct")) {
		// Is this IMSI registered directly on a BTS?
		string remoteIMSI = gSubscriberRegistry.getIMSI(wCalledUsername);
		string remoteIPStr, remotePortStr;
		if (remoteIMSI != "") {
			remoteIPStr = gSubscriberRegistry.imsiGet(remoteIMSI,"ipaddr");
			remotePortStr = gSubscriberRegistry.imsiGet(remoteIMSI,"port");
			unsigned remotePort = (remotePortStr != "") ? atoi(remotePortStr.c_str()) : 0;
			LOG(DEBUG) << "BTS direct test: "<<wCalledUsername
					<< format(" -> SIP/%s@%s:%s",remoteIMSI.c_str(),remoteIPStr.c_str(),remotePortStr.c_str()) <<sbText();
			if (remoteIPStr != "" && remotePort) {
				//directBTSConnection = true;
				mRemoteUsername = remoteIMSI;
				mProxyIP = remoteIPStr;
				mProxyPort = remotePort;
				LOG(INFO) << "Calling BTS direct: "<<wCalledUsername
					<< format(" -> SIP/%s@%s:%u",mRemoteUsername.c_str(),mProxyIP.c_str(),mProxyPort) <<sbText();
			}
		}
	}
	//mRemoteUsername = "IMSI001690000000002";	// pats iphone
#endif
	
	LOG(DEBUG) <<sbText();

	static const string cInviteStr("INVITE");
	SipMessage *invite = makeInitialRequest(cInviteStr);
	invite->smAddBody(string("application/sdp"),makeSDPOffer());


	string username = sipLocalUsername();
	WATCH("MOC imsi="<<username);
	if (username.substr(0,4) == "IMSI") {
		string pAssociatedUri, pAssertedIdentity;
		gTMSITable.getSipIdentities(username.substr(4),pAssociatedUri,pAssertedIdentity);
		WATCH("MOC"<<LOGVAR(username)<<LOGVAR(pAssociatedUri)<<LOGVAR(pAssertedIdentity));
		if (pAssociatedUri.size()) {
			invite->smAddHeader("P-Associated-URI",pAssociatedUri);
		}
		if (pAssertedIdentity.size()) {
			invite->smAddHeader("P-Asserted-Identity",pAssertedIdentity);
		}
	}

	writePrivateHeaders(invite,chan);
	moWriteLowSide(invite);
	delete invite;
	setSipState(Starting);
};


void SipMOInviteClientTransactionLayer::handleSMSResponse(SipMessage *sipmsg)
{
	// There are only three cases, and here they are:
	stopTimers();
	int code = sipmsg->smGetCode();
	if (code < 200) {
		// (pat) 11-26 This was wrong: mTimerBF.set(64*T1);
		return;		// Ignore 100 Trying.
	}
	// Smqueue incorrectly requires us to add a from-tag, and sends a 400 error if we dont.
	// So if we get a 400 error, see if the user agent is blank (which is what smqueue does, yate adds a correct user-agent)
	// then make an attempt to redeliver the message with a from-tag.
	// What a botch-up.
	if (code == 400) {
		string useragent = sipmsg->msmHeaders.paramFind("User-Agent");
		if (useragent == "") {
			LOG(INFO) << "Message delivery failed; no user-agent specified.  Assuming smqueue and resending MESSAGE";
			gPeerIsBuggySmqueue = true;
			// Resend the message, and this time it will add the from-tag.
			if (dgGetDialog()->MOSMSRetry()) { return; }
		}
	}
	dialogPushState((code/100)*100 == 200 ? Cleared : SSFail,code);
	mTimerK.setOnce(T4);	// 17.1.2.2: Timer K Controls when the dialog is destroyed.
}

void SipInviteServerTransactionLayerBase::SipMTCancel(SipMessage *sipmsg)
{
	assert(sipmsg->isCANCEL());
	SipMessageReply cancelok(sipmsg,200,string("OK"),this);
	sipWrite(&cancelok);
	if (dsPeer()->ipIsReliableTransport()) {
		// It no longer matters whether we use Canceled or MTDCanceling state, and we should get rid of some states.
		dialogPushState(Canceled,0);
	} else {
		dialogPushState(MTDCanceling,0);
		setTimerJ();
	}
}

void SipInviteServerTransactionLayerBase::SipMTBye(SipMessage *sipmsg)
{
	assert(sipmsg->isBYE());
	gReports.incr("OpenBTS.SIP.BYE.In");
	SipMessageReply byeok(sipmsg,200,string("OK"),this);
	sipWrite(&byeok);
	if (dsPeer()->ipIsReliableTransport()) {
		dialogPushState(Cleared,0);
	} else {
		dialogPushState(MTDClearing,0);
		setTimerJ();
	}
}

// Incoming message from SIPInterface.
void SipMOInviteClientTransactionLayer::MOWriteHighSide(SipMessage *sipmsg)
{
	int code = sipmsg->smGetCode();
	LOG(DEBUG) <<LOGVAR(code) <<LOGVAR(dgIsInvite());
	if (code == 0) {	// It is a SIP Request.  Switch based on the method.
		if (sipmsg->isCANCEL()) {
			mTimerBF.stop();
			// I dont think the peer is supposed to do this, but lets cancel it:
			SipMTCancel(sipmsg);
		} else if (sipmsg->isBYE()) {
			// A BYE should not be sent by the peer until dialog established, and we are supposed to send 405 error.
			// but instead we are going to quietly terminate the dialog anyway.
			SipMTBye(sipmsg);
		} else {
			// Not expecting any others.
			// Must send 405 error.
			LOG(WARNING)<<"SIP Message ignored:"<<sipmsg;
			SipMessageReply oops(sipmsg,405,string("Method Not Allowed"),this);
			sipWrite(&oops);
		}
	} else {		// It is a SIP reply.
		saveMOResponse(sipmsg);	LOG(DEBUG)<<"saveResponse"<<sipmsg;
		if (dgIsInvite()) {	// It is INVITE
			stopTimers();	// Yes we stop the timers for every possible case.  TimerBF will be restarted when handleInviteResponse sends the reply.
			handleInviteResponse(code,true);
		} else {	// It is a MESSAGE
			handleSMSResponse(sipmsg);
		}
	}
}

// Outgoing message.
void SipMOInviteClientTransactionLayer::moWriteLowSide(SipMessage *sipmsg)
{
	if (sipmsg->isINVITE() || sipmsg->isMESSAGE()) {	// It cant be anything else.
		if (!dsPeer()->ipIsReliableTransport()) { mTimerAE.set(2*T1); }
		mTimerBF.setOnce(64*T1);	// RFC3261 17.1.2.2.2  Timer F
		// This assert is not true in the weird case where we resend an SMS message.
		// We should not be using this code for MESSAGE in the first place - it should be a TU.
		//assert(mInvite == 0);
		saveInviteOrMessage(sipmsg,true);
	}
	sipWrite(sipmsg);
}

// (pat) Counter-intuitively, the "ACK" is a SIP Request, not a SIP Response.
// Therefore its first line includes a Request-URI, and the request-uri is also placed in the "To" field.
// RFC2234 13.1: "The procedure for sending this ACK depends on the type of response.
// 		For final responses between 300 and 699, the ACK processing is done in the transaction layer and follows
//		one set of rules (See Section 17).  For 2xx responses, the ACK is generated by the UAC core. (section 13)"
// 17.1.1.3: "The ACK request constructed by the client transaction MUST contain
//		values for the Call-ID, From, and Request-URI that are equal to the
//		values of those header fields in the request passed to the transport
//		by the client transaction (call this the "original request").  The To
//		header field in the ACK MUST equal the To header field in the
//		response being acknowledged, and therefore will usually differ from
//		the To header field in the original request by the addition of the
//		tag parameter.  The ACK MUST contain a single Via header field, and
//		this MUST be equal to the top Via header field of the original
//		request.  The CSeq header field in the ACK MUST contain the same
//		value for the sequence number as was present in the original request,
//		but the method parameter MUST be equal to "ACK".
//
void SipMOInviteClientTransactionLayer::MOCSendACK()
{
	assert(! mTimerAE.isActive() && ! mTimerBF.isActive());
	LOG(INFO) << sbText();
	//LOG(INFO) << "user " << mSipUsername << " state " << getSipState() <<sbText();

	static const string cAckstr("ACK");
	SipMessageAckOrCancel ack(cAckstr,mInvite);
	ack.msmTo = *dsRequestToHeader();		// Must get the updated to-tag.
	sipWrite(&ack);
	// we dont care mTimerD.set(T4);
}

void SipMOInviteClientTransactionLayer::MOSMSSendMESSAGE(const string &messageText, const string &contentType)
{
	LOG(INFO) << "SIP send to " << dsRequestToHeader() <<" MESSAGE " << messageText <<sbText();
	assert(mDialogType == SIPDTMOSMS);
	gReports.incr("OpenBTS.SIP.MESSAGE.Out");
	
	static const string cMessagestr("MESSAGE");
	SipMessage *msg = makeInitialRequest(cMessagestr);
	msg->smAddBody(contentType,messageText);
	moWriteLowSide(msg);
	delete msg;
	setSipState(MOSMSSubmit);
}


// This is a temporary routine to work around bugs in smqueue.
// Resend the message with changes (gPeerIsBuggySmqueue, set/reset by the caller) to see if it works any better.
bool SipMOInviteClientTransactionLayer::MOSMSRetry()
{
	LOG(INFO);
	SipDialog *oldDialog = dgGetDialog();
	FullMobileId fromMsId(oldDialog->sipLocalUsername());
	SipDialog *newDialog = SipDialog::newSipDialogMOSMS(
		mTranId,
		fromMsId,		// caller imsi
		oldDialog->sipRemoteUsername(),
		oldDialog->smsBody,
		oldDialog->smsContentType);
	gNewTransactionTable.ttSetDialog(oldDialog->mTranId,newDialog);
	return true;	// success
	//dialog->mLocalCSeq = gSipInterface.nextInviteCSeqNum(); 	// formerly: random()%600;
	//dialog->dsSetCallId(globallyUniqueId(""));
	//dialog->mLocalHeader.setTag(gPeerIsBuggySmqueue ? make_tag() : "");
	//dialog->MOSMSSendMESSAGE(mInvite->msmBody,mInvite->msmContentType);
}

// Return TRUE to remove the dialog.
bool SipMOInviteClientTransactionLayer::moPeriodicService()
{
	if (mTimerAE.expired()) {	// Resend timer.
		// The HANDOVER is inbound, but the invite is outbound like MOC.
		if (getSipState() == Starting || getSipState() == HandoverInbound || getSipState() == MOSMSSubmit) {
			sipWrite(getInvite());
			mTimerAE.setDouble();
		} else {
			mTimerAE.stop();
		}
	} else if (mTimerBF.expired() || mTimerD.expired()) {	// Dialog killer timer.
		// Whoops.  No response from peer.
		stopTimers();
		dialogPushState(SSFail,0);
		return true;
	} else if (mTimerK.expired()) {		// Normal exit delay to absorb resends.
		stopTimers();
		if (! dgIsInvite()) { return true; }	// It is a SIP MESSAGE.
	} else if (sipIsFinished()) {
		// If one of the kill timers is active, wait for it to expire, otherwise kill now.
		return (mTimerBF.isActive() || mTimerD.isActive() || mTimerK.isActive()) ? false : true;
	}
	return false;
}

string SipMOInviteClientTransactionLayer::motlText() const
{
	ostringstream os;
	os <<LOGVAR2("Timers: AE",mTimerAE) <<LOGVAR2("BF",mTimerBF) <<LOGVAR2("K",mTimerK) <<LOGVAR2("D",mTimerD);
	return os.str();
}


SipState SipBase::MODSendCANCEL()
{
	LOG(INFO) << sbText();
	setSipState(MODCanceling);	// (pat) MOD sent a cancel, see forceSIPClearing.
	SipMOCancelTU *cancelTU = new SipMOCancelTU(dynamic_cast<SipDialog*>(this));
	cancelTU->sctStart();
	return getSipState();
}



// Type is RtpCallback, but args determined by rtp_signal_table_emit2
extern "C" {
	void ourRtpTimestampJumpCallback(RtpSession *session, unsigned long timestamp,unsigned long dialogid)
	{
		SipBase *dialog = gSipInterface.dmFindDialogByRtp(session);
		if (dialog) {
			LOG(NOTICE) << "RTP timestamp jump"<<LOGVAR(timestamp)<<LOGVAR(dialogid)<<dialog;
			// This is called from the same thread that is calling rxFrame or txFrame, so no problem.
			if (dialog->mRxTime) {
				rtp_session_resync(session);
				dialog->mRxTime = 0;
				dialog->mRxRealTime = 0;
			}
		} else {
			LOG(ALERT) << "RTP timestamp jump, but no dialog"<<LOGVAR(dialogid);
		}
	}
};

void SipRtp::initRTP1(const char *d_ip_addr, unsigned d_port, unsigned dialogId)
{
	LOG(DEBUG) << LOGVAR(d_ip_addr)<<LOGVAR(d_port)<<sbText();

	if(mSession == NULL) {
		mSession = rtp_session_new(RTP_SESSION_SENDRECV);
		gCountRtpSessions++;
	}

	bool rfc2833 = gConfig.getBool("SIP.DTMF.RFC2833");
	if (rfc2833) {
		RtpProfile* profile = rtp_session_get_send_profile(mSession);
		int index = gConfig.getNum("SIP.DTMF.RFC2833.PayloadType");
		// (pat) The &payload_type_telephone_event comes from the RTP library PayloadType
		rtp_profile_set_payload(profile,index,&payload_type_telephone_event);
		// Do we really need this next line?
		rtp_session_set_send_profile(mSession,profile);
	}

	// 8-6-2013: I tried turning scheduling mode to FALSE, but it didnt seem to work at all.
	// There is an interesting problem when you dial 2600 since the rxFrame is blocking on its own txFrame,
	// a paradox that somehow doesnt hang.
	if (rtpUseRealTime) {
		// Disable blocking and scheduling in the RTP library block.
		rtp_session_set_blocking_mode(mSession, FALSE);
		rtp_session_set_scheduling_mode(mSession, FALSE);
	} else {
		// Let the RTP library block.
		rtp_session_set_blocking_mode(mSession, TRUE);
		rtp_session_set_scheduling_mode(mSession, TRUE);
	}

	rtp_session_set_connected_mode(mSession, TRUE);
	rtp_session_set_symmetric_rtp(mSession, TRUE);
	// Hardcode RTP session type to GSM full rate (GSM 06.10).
	// FIXME -- Make this work for multiple vocoder types.
	rtp_session_set_payload_type(mSession, 3);
	// (pat added) The last argument is user payload data that is passed to ourRtpTimestampJumpCallback()
	// I was going to use the dialogId but decided to look up the dialog by mSession.
	rtp_session_signal_connect(mSession,"timestamp_jump",(RtpCallback)ourRtpTimestampJumpCallback,dialogId);

	gCountRtpSockets++;
#ifdef ORTP_NEW_API
	rtp_session_set_local_addr(mSession, "0.0.0.0", mRTPPort, -1);
#else
	rtp_session_set_local_addr(mSession, "0.0.0.0", mRTPPort);
#endif
	rtp_session_set_remote_addr(mSession, d_ip_addr, d_port);
	WATCHF("*** initRTP local=%d remote=%s %d\n",mRTPPort,d_ip_addr,d_port);

	int speechBuffer = gConfig.getNum("GSM.SpeechBuffer");

	// The RTP library sets the default here to 5000, but I think the cost of resynchronization is very
	// low and we should do it more often to reduce horrendous sound quality that otherwise occurs
	// when there is extraneous delay in the received frames.  Such delay occurs due to the transmitter
	// using the TCH for FACCH, for in-call-SMS, and other reasons.
	// The horrendous sound I suspect is caused by improper computation of the rxframe number internally.
	// I'm setting it down such that we will resynchronize whenever we get more than one frame off, where one frame = 160.  
	// Update: That worked great as long as there was no jump, but
	// there seems to be a bug in the RTP library that the time jump limit must exceed the buffered amount
	// or it constantly tries to resync.
	// Update: When the timestamp jump occurs there is a very noticeable silence presumably while the jitter
	// buffer is reloaded, so instead of setting this low, we will leave it high (it must be set to
	// something to handle the negative timestamp jump after handover) and go back to setting rxTime from the actual time.
	//unsigned jump_limit = max(300,((speechBuffer+159+160) / 160));
	unsigned jump_limit = 5000;
	LOG(DEBUG)<<LOGVAR(jump_limit);
	rtp_session_set_time_jump_limit(mSession,jump_limit);

	if (speechBuffer == 0) {
		// (pat) Special case value turns off the rtp jitter compensation entirely.
		// There is some intrinsic buffering both in GSML1FEC and between us and the transceiver.
		rtp_session_enable_jitter_buffer(mSession,FALSE); 
	} else if (speechBuffer == 1) {
		// (pat) The default is adaptive speech buffer, so we just do nothing in this case.
	} else {
		// (pat 8-6-2013) The RTP adaptive jitter buffer does not work well with OpenBTS because it assumes
		// the source stream is continuous in time, but it is not in our case.  When FACCH is used or there
		// is some other drop out, the lost time appears to be added to the total delay through the de-jitter algorithm.
		// At least some phones seem to turn the TCH off completely during an in-call SMS.
		// So lets turn it off.  This code was copied from rtp_session_init().
		// (pat update) The RTP library goes south if you set the jitter buffer size over about 300 msecs,
		// so heck with it - just use the adaptive algorithm.  It is mostly fixed now anyway.
		{
			//int packets = speechBuffer/20 + 10;		// Number of 20 msec packets needed for buffering, plus some slop.
			JBParameters jbp;
			jbp.min_size=RTP_DEFAULT_JITTER_TIME;	// Not used by RTP code but we are setting it anyway.
			jbp.nom_size=speechBuffer;				// Original default was 80 msecs.
			jbp.max_size=-1;						// Not used by RTP code.
			// The max_packets must be large enough to cover the speech buffer size plus a lot of slop, not sure how much.
			jbp.max_packets= 100;/* maximum number of packet allowed to be queued */
			jbp.adaptive=FALSE;
			rtp_session_enable_jitter_buffer(mSession,TRUE);		// redundant, this is the default.
			rtp_session_set_jitter_buffer_params(mSession,&jbp);
		}
	}

	// Check for event support.
	int code = rtp_session_telephone_events_supported(mSession);
	if (code == -1) {
		if (rfc2833) { LOG(CRIT) << "RTP session does not support selected DTMF method RFC-2833" <<sbText(); }
		else { LOG(CRIT) << "RTP session does not support telephone events" <<sbText(); }
	}

}

void SipBase::initRTP()
{
	SdpInfo sdp;
	sdp.sdpParse(getSdpRemote().c_str());
	initRTP1(sdp.sdpHost.c_str(),sdp.sdpRtpPort,mDialogId);
}

void SipBase::MTCInitRTP()
{
	initRTP();
}

void SipBase::MOCInitRTP()
{
	initRTP();
}


bool SipRtp::txDtmf()
{
	ScopedLock lock(mRtpLock);
	//true means start
	bool start = (mDTMFDuration == 0);
	mblk_t *m = rtp_session_create_telephone_event_packet(mSession,start);
	if (!m) {
		// (pat) Failed because, and I quote: "the rtp session cannot support telephony event (because the rtp profile
		// it is bound to does not include a telephony event payload type)."
		LOG(DEBUG) << "fail";
		return false;
	}
	// (pat) Max RTP event duration is 8 seconds, so if we exceed that turn it off.  Back it off a little (5 packets, 100ms) because
	// we also need to send the three end packets and it is not clear to me if the DTMFDuration should be incremented or not
	// during the end packets, but we are incrementing.
	// (8 * 50 * 160) - (5 * 160) = 63200.
	if (!mDTMFEnding && mDTMFDuration >= 63200) {
		mDTMFEnding = 1;
	}
	//volume 10 for some magic reason, arg 3 is true to send an end packet.
	// (pat) The magic reason is that the spec says DTMF tones below a certain dB should be ignored by the receiver, which is dumber than snot.
	int code = rtp_session_add_telephone_event(mSession,m,get_rtp_tev_type(mDTMF),!!mDTMFEnding,10,mDTMFDuration);
	int bytes = rtp_session_sendm_with_ts(mSession,m,mDTMFStartTime);
	LOG(DEBUG) <<LOGVAR(start) <<LOGVAR(mDTMF) <<LOGVAR(mDTMFEnding) <<LOGVAR(mDTMFDuration) <<LOGVAR(code) <<LOGVAR(bytes);
	// (pat) There is a buglet that this time would be incorrect if we flushed some RTP packets.
	mDTMFDuration += 160;
	if (mDTMFEnding) {
		// We need to send the end packet three times.
		if (mDTMFEnding++ >= 3) {
			mDTMFEnding = 0;
			mDTMF = 0;
		}
	}
	return (!code && bytes > 0);
}

// Return true if ok, false on failure.
bool SipRtp::startDTMF(char key)
{
	LOG (DEBUG) << key <<sbText();
	if (getSipState()!=Active) return false;
	if (get_rtp_tev_type(key) < 0){
		LOG(WARNING) << "DTMF (using RFC-2833) sent invalid key, numeric value="<<(int) key;
	    return false;
	}
	// (pat) It is ok to start a new DTMF before the old one ended.
	mDTMF = key;
	mDTMFDuration = 0;
	mDTMFStartTime = mTxTime;
	mDTMFEnding = 0;

	if (! txDtmf()) {
		// Error?  Turn off DTMF sending.
		LOG(WARNING) << "DTMF RFC-2833 failed on start." <<sbText();
		mDTMF = 0;
		return false;
	}
	return true;
}

void SipRtp::stopDTMF()
{
	//false means not start
	/***
		mblk_t *m = rtp_session_create_telephone_event_packet(mSession,false);
		//volume 10 for some magic reason, end is true
		int code = rtp_session_add_telephone_event(mSession,m,get_rtp_tev_type(mDTMF),true,10,mDTMFDuration);
		int bytes = rtp_session_sendm_with_ts(mSession,m,mDTMFStartTime);
		mDTMFDuration += 160;
		LOG (DEBUG) << "DTMF RFC-2833 sending " << mDTMF << " " << mDTMFDuration <<sbText();
		// Turn it off if there's an error.
		if (code || bytes <= 0)
	***/
	if (!mDTMF) {
		LOG(WARNING) << "stop DTMF command received after DTMF ended.";
		return;
	}
	mDTMFEnding = 1;
	if (! txDtmf()) {
	    LOG(ERR) << "DTMF RFC-2833 failed at end" <<sbText();
		mDTMF = 0;
	}

}


// send frame in the uplink direction.
// The gsm bit rate is 13500 and clock rate is 8000.  Time is 20ms or 50 packets/sec.
// The 160 that we send is divided by payload->clock_rate in rtp_session_ts_to_time to yield 20ms.
void SipRtp::txFrame(GSM::AudioFrame* frame, unsigned numFlushed)
{
	if(getSipState()!=Active) return;
	ScopedLock lock(mRtpLock);

	// HACK -- Hardcoded for GSM/8000.
	// FIXME: Make this work for multiple vocoder types. (pat) fixed, but codec is still hard-coded in initRTP1.
	int nbytes = frame->sizeBytes();
	// (pat 8-2013) Our send stream is discontinous.  After a discontinuity, the sound degrades.
	// I think this is caused by bugs in the RTP library.

	mTxTime += (numFlushed+1)*160;
	int result = rtp_session_send_with_ts(mSession, frame->begin(), nbytes, mTxTime);
	LOG(DEBUG) << LOGVAR(mTxTime) <<LOGVAR(result);

	// (pat) The result is the number of bytes sent over the network, which includes nbytes data + 12 bytes of RTP header.
	if (result < nbytes || result != 33+12) {
		LOG(DEBUG) << "rtp_session_send_with_ts("<<nbytes<<") returned "<<result <<sbText();
	}

	if (mDTMF) {
		//false means not start
		/*****
			mblk_t *m = rtp_session_create_telephone_event_packet(mSession,false);
			//volume 10 for some magic reason, false means not end
			int code = rtp_session_add_telephone_event(mSession,m,get_rtp_tev_type(mDTMF),false,10,mDTMFDuration);
			int bytes = rtp_session_sendm_with_ts(mSession,m,mDTMFStartTime);
			mDTMFDuration += 160;
			//LOG (DEBUG) << "DTMF RFC-2833 sending " << mDTMF << " " << mDTMFDuration <<sbText();
			if (code || bytes <=0)
		***/
		if (! txDtmf())
		{
			LOG(ERR) << "DTMF RFC-2833 failed after start." <<sbText();
			mDTMF=0; // Turn it off if there's an error.
		}
	}
}


GSM::AudioFrame *SipRtp::rxFrame()
{
	if(getSipState()!=Active) {
		LOG(DEBUG) <<"skip"<<LOGVAR(getSipState());
		return 0; 
	}
	int more = 0;

	// The buffer size is:
	//  For GSM, 260 bits + 4 bit header is 33 bytes.
	//  For TFO (3GPP 28.062 5.2.2) is 40 bytes.
	//  For AMR 'compact transport format', variable size, max is 244 bits + 10 bit header??
	// (pat) We will not support TFO for years, if ever.
	// TODO: Make the rxFrame buffer big enough (160?) for G.711.  But we dont support that yet.
	const int maxsize = 33;

	// (pat 8-2013) I tried rtp_session_get_current_recv_ts but it just doesnt work; returns garbage.
	// It is frightening how much we depend on the extremely buggy RTP library.
	//uint32_t suggestedRxTime = rtp_session_get_current_recv_ts(mSession);

	// (pat 8-2013) The RTP library has a scheduling mode and a blocking mode whereby it blocks the thread
	// until the specified time has elapsed, which is 20ms.
	// But after a discontinuity in the send data stream the RTP scheduler seems to get confused and rxFrame
	// returns 0 for long periods of time.
	// A discontinuity can be artificially generated at the time of writing using an in-call-SMS,
	// which currently blocks TCH while the current thread runs SACCH, a bug to be fixed,
	// however discontinuities could occur for other reasons so the code should not break when one occurs.
	// I added this code controlled by rtpUseRealTime so we could handle the real elapsed time ourselves and not block.
	// This code below suffers no such instability, so we are using it.  Someday if we switch RTP libraries,
	// someone can try turning off rtpUseRealTime - but you have to TEST DISCONTINUITIES, which is hard,
	// so dont just turn this off, try a test call, and pronounce it fixed.
	if (rtpUseRealTime) {
		struct timeval tv;
		gettimeofday(&tv,NULL);
		uint64_t realTime = (uint64_t)tv.tv_sec * (1000 * 1000) + (uint64_t)tv.tv_usec;		// time in usecs.
		if (mRxRealTime == 0) {
			// First time, init for next pass through...
			devassert(mRxTime == 0);
			mRxRealTime = realTime;
		} else {
			uint64_t delay = realTime - mRxRealTime;		// total elapsed time in usecs.
			uint64_t delayInFrames = delay / (1000 * 20);  // 20ms per frame	// elapsed time in frames.
			unsigned proposedRxTime = delayInFrames * 160;	// elapsed time in RTP 'time' units.  (160 / 8000 bitrate == 20 msecs)
			LOG(DEBUG) << format("realTime=%.3f delay=%.3f delayInFrames=%u RxTime=%u proposed=%u",
				realTime/1e6,delay/1e6,(unsigned)delayInFrames,mRxTime,proposedRxTime);
			if (proposedRxTime <= mRxTime) {
				LOG(DEBUG) <<"skip, insufficient time passed";
				return NULL;
			}
			// We will snag the next frame with a number equal or higher than this.  If there are none available, we get NULL.
			// When there is a discontinuity of missing frames, we get a bunch of NULL frames during the discontinuity, then
			// things pick up normally again.
			mRxTime += 160;
		}
	}

	GSM::AudioFrame *result = new GSM::AudioFrame(maxsize);  // Make it big enough for anything we might support.
	int ret = rtp_session_recv_with_ts(mSession, result->begin(), maxsize, mRxTime, &more);
	// (pat) You MUST increase rxTime even if rtp_session_recv... returns 0.
	// This looks like a bug in the RTP lib to me, specifically, here:
	//  Excerpt from rtpsession.c: rtp_session_recvm_with_ts():
	//	 // prevent reading from the sockets when two consecutives calls for a same timestamp*/
	//	 if (user_ts==session->rtp.rcv_last_app_ts)
	//			 read_socket=FALSE;
	// The bug is the above should also check the qempty() flag.
	// It should only manifest when blocking mode is off but we had it on when I thought I saw troubles.
	// I tried incrementing by just 1 when ret is 0, but that resulted in no sound at all.

	if (!rtpUseRealTime) { mRxTime += 160; }

	//LOG(DEBUG) << "rtp_session_recv returns("<<LOGVAR(mRxTime)<<LOGVAR(more)<<")="<<LOGVAR(ret) <<sbText();
	devassert(ret <= maxsize);   // Check for bugs in rtp library or Audio type setup.
	LOG(DEBUG) <<LOGVAR(getSipState())<<LOGVAR(mRxTime)<<LOGVAR(ret);
	if (ret <= 0) { delete result; return NULL; }
	result->setSizeBits(ret * 8);
	// (pat) Added warning; you could get ALOT of these:
	// Update: It is not that the frame is over-sized, it is that there is another frame already in the queue.
	//if (more) { LOG(WARNING) << "Incoming RTP frame over-sized, extra ignored." <<sbText(); }
	if (more) { LOG(WARNING) << "Incoming RTP lagging behind clock." <<sbText(); }
	return result;
}



bool SipBase::dgIsInvite() const
{
	SipMessage *invite = getInvite();
	return invite && invite->isINVITE();
}

// Are these the same dialogs?
bool SipBase::sameDialog(SipDialog *other)
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

// Only the SipMTInviteServerTransactionLayer and SipMOInviteClientTransactionLayer are allowed to call
// the underlying sipWrite method directly for the invite transactions.
void SipBase::sipWrite(SipMessage *sipmsg)
{
	if (!mProxy.mipValid) {
		LOG(ERR) << "Attempt to write to invalid proxy ignored, address:"<<mProxy.mipName;
		return;
	}
	gSipInterface.siWrite(&mProxy.mipSockAddr,sipmsg);
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
	string str = msg->smGenerate();
	//LOG(DEBUG) <<LOGVAR(str);
	// We are temporarily sending this over the peering interface in a string of space-separated parameters.
	// So to get rid of the spaces, replace them with tabs.
	string result= encodeSpaces(str);
	//LOG(DEBUG) <<LOGVAR(result);
	return result;
}



SipDialog *SipBase::dgGetDialog()
{
	return dynamic_cast<SipDialog*>(this);
}

void SipMTInviteServerTransactionLayer::MTCSendTrying()
{
	SipMessage *invite = getInvite();
	if (invite==NULL) {
		setSipState(SSFail);
		gReports.incr("OpenBTS.SIP.Failed.Local");
	}
	LOG(INFO) << sbText();
	if (getSipState()==SSFail) return;
	SipMessageReply trying(invite,100,string("Trying"),this);
	mtWriteLowSide(&trying);
	setSipState(Proceeding);
}

void SipMTInviteServerTransactionLayer::MTSMSSendTrying()
{
	MTCSendTrying();
}


void SipMTInviteServerTransactionLayer::MTCSendRinging()
{
	if (getSipState()==SSFail) return;
	SipMessage *invite = getInvite();
	LOG(INFO) <<sbText();
	assert(invite);
	LOG(DEBUG) << "send ringing" <<sbText();
	SipMessageReply ringing(invite,180,string("Ringing"),this);
	mtWriteLowSide(&ringing);
	setSipState(Proceeding);
}

void SipMTInviteServerTransactionLayer::MTCSendOK(CodecSet wCodec, const L3LogicalChannel *chan)
{
	if (getSipState()==SSFail) { devassert(0); }
	SipMessage *invite = getInvite();
	gReports.incr("OpenBTS.SIP.INVITE-OK.Out");
	mRTPPort = allocateRTPPorts();
	mCodec = wCodec;
	LOG(INFO) <<sbText();
	SipMessageReply ok(invite,200,string("OK"),this);
	ok.smAddBody(string("application/sdp"),makeSDPAnswer());	// TODO: This should be a reply to the originating SDP offer.
	writePrivateHeaders(&ok,chan);
	mtWriteLowSide(&ok);
	setSipState(Connecting);
	// In RFC-3261 the Transaction Layer no longer handles timers after the OK is sent.
	// The Transport Layer alone is not capabable of sending the 200 OK reliably because then the
	// INVITE server transaction ends, and the INVITE client transaction no longer resends INVITEs after
	// receiving a provisional response.  Rather, the way that would end up being handled is by starting a new
	// INVITE transaction, which is totally not what we want to do.  So we will push out the 2xx OK
	// until we get the ACK.  Doesnt matter for reliable transports.
	if (dgIsInvite()) { setTimerG(); }
	setTimerH();
}

string SipMTInviteServerTransactionLayer::mttlText() const
{
	ostringstream ss;
	ss << LOGVAR2("Timers: G",mTimerG) <<LOGVAR2("H",mTimerH) <<LOGVAR2("J",mTimerJ);
	return ss.str();
}

// Doesnt seem like messages need the private headers.
void SipMTInviteServerTransactionLayer::MTSMSReply(int code, const char *explanation) // , const L3LogicalChannel *chan)
{
	LOG(INFO) <<sbText();
	// If this operation was initiated from the CLI, there was no MESSAGE
	if (mInvite) {	// It is a MESSAGE in this case, not an INVITE
		//2-2014: the reply to MESSAGE must include the to-field, so we pass the dialog to SIpMessageReply
		SipMessageReply reply(mInvite,code,string(explanation),this);			// previous: NULL);
		sipWrite(&reply);
	} else {
		LOG(INFO) << "clearing CLI-generated transaction" <<sbText();
	}
	setSipState(code == 200 ? Cleared : SSFail);
}

// This can only be used for early errors before we get the ACK.
void SipMTInviteServerTransactionLayer::MTCEarlyError(int code, const char*reason)	// The message must be 300-699.
{
	LOG(DEBUG);
	// TODO: What if we were already ACKed?
	setSipState(MODError);
	SipMessageReply reply(getInvite(),code,string(reason),this);
	mtWriteLowSide(&reply);
	if (dgIsInvite()) { setTimerG(); }
	setTimerH();
}

// This is called for the second and subsequent received INVITEs as well as the ACK.
// We send the current response, whatever it is.
void SipMTInviteServerTransactionLayer::MTWriteHighSide(SipMessage *sipmsg) {	// Incoming message from SIPInterface.
	LOG(DEBUG);
	SipState state = getSipState();
	if (sipmsg->smGetCode() == 0) {
		if (sipmsg->isINVITE()) {
			if (mtLastResponse.smIsEmpty()) { MTCSendTrying(); }
			else { sipWrite(&mtLastResponse); }
		} else if (sipmsg->isACK()) {
			if (state == SSNullState || state == Proceeding || state == Connecting) {
				dialogPushState(Active,0);
				gSipInterface.dmAddLocalTag(dgGetDialog());
			} else {
				// We could be failed or canceled, so ignore the ACK.
			}
			stopTimers();	// happiness
			// The spec shows a short Timer I being started here, but all it does is specify a time
			// when the dialog will stop absorbing additional ACKS, thus suppressing error messages.
			// Well, how about if we just never throw an error for that?  Done.
		} else if (sipmsg->isCANCEL()) {
			SipMTCancel(sipmsg);
		} else if (sipmsg->isBYE()) {		// TODO: This should not happen.
			SipMTBye(sipmsg);
		} else if (sipmsg->isMESSAGE()) {
			// It is a duplicate MESSAGE.  Resend the current response.
			if (mtLastResponse.smIsEmpty()) {
				if (! gConfig.getBool("SIP.RFC3428.NoTrying")) { MTSMSSendTrying(); }	// Otherwise just ignore the duplicate MESSAGE.
			} else {
				sipWrite(&mtLastResponse);
			}
		} else {
			// Not expecting any others.  Must send 405 error.
			LOG(WARNING)<<"SIP Message ignored:"<<sipmsg;
			SipMessageReply oops(sipmsg,405,string("Method Not Allowed"),this);
			sipWrite(&oops);
		}
	} else {
		LOG(WARNING)<<"SIP Message ignored:"<<sipmsg;
	}
}

// Return TRUE to remove the dialog.
bool SipMTInviteServerTransactionLayer::mtPeriodicService()
{
	if (mTimerG.expired()) {	// Resend timer.
		if (getSipState() == SSFail || getSipState() == Active) {
			sipWrite(mLastResponse);
			mTimerG.setDouble(T2);	// Will send again later.
		} else {
			// This could happen if a CANCEL started before the ACK was received.
			// Not sure what to do - I think we will let the CANCEL take precedence, so stop sending this response.
			mTimerG.stop();
		}
	} else if (mTimerJ.expired() || mTimerH.expired()) {	// Dialog killer timers.
		stopTimers();	// probably redundant.
		// Time to destroy the Dialog.
		if (dgIsInvite()) {
			// Whoops.  No ACK received.  Notify L3 and remove the dialog
			dialogPushState(SSFail,0);
		} else {
			// No need to notify, just remove the dialog.
		}
		return true; // Stop the dialog now.  It will be deleted by the periodic service loop after the associated L3 transaction ends.
	} else if (sipIsFinished()) {
		// If one of the kill timers is active, wait for it to expire, otherwise kill now.
		return (mTimerJ.isActive() || mTimerH.isActive()) ? false : true;
	}
	return false;
}

};
// vim: ts=4 sw=4
