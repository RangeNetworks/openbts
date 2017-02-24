/*
* Copyright 2008 Free Software Foundation, Inc.
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribution.
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
#include <signal.h>

#include <ortp/ortp.h>

#include "SIPUtility.h"
#include "SIPMessage.h"
#include "SIPBase.h"
#include <OpenBTSConfig.h>
#undef WARNING		// The nimrods defined this to "warning"
#undef CR			// This too

using namespace std;

namespace SIP {


// Matching replies to requests:
// RFC3261 is an evolved total botch-up.  Forensically, messages were originally identified by method + cseq,
// then the random call-id was added, but that did not allow differentiation of multiple replies,
// so the to-tag and and apparently completely redundant from-tag were added, but that was insufficient for proxies,
// so finally the via-branch was added, which now trumps all other id methods.
// The call-id is constant for all transactions within a dialog, and for all REGISTER to a Registrar.
// The via-branch is unique for all requests except for CANCEL and ACK, which use the via-branch of the request being cancelled,
// however, this is a new feature.  The via header is copied into the response.
// The current rules as per RFC3261 are as follows:
// 17.1.3 specifies how to match responses to client transactions:
// 		o  If the via branch and the cseq-method match the request.   (cseq-method needed because CANCEL
//		uses the same via-branch of the initial INVITE.)  This is unique because we added that via-branch ourselves.
// 		Note that this rule is universally applicable for both SIP endpoints and proxies; as a sip-endpoint
//		we could use a different system, for example call-id + from-tag + cseq-method + cseq-number.
//		Note that this rule matches responses to requests, but does not differentiate multiple replies
//		from different endpoints to the same request, which would be differentiated by from/to-tag for dialogs.
//		For direct BTS-to-BTS connection where there are two calls on the same BTS talking to each other directly
//		without an intervening SIP server, then the via-branch, call-id are identical
//		for both dialogs.  (Because the the response copies call-id, from-tag and via-header from the request.)
//		Options for differentiating the messages are to use the local-tag (which is identical in both dialogs
//		but is the from-tag in the originating dialog and the to-tag in the terminating dialog.
//		or to use the to-tag (which is not known when original response to dialog is received),
//		or the CSEQ-number and make sure the CSEQ-number of the two dialogs are non-overlapping.
//		A -- INVITE -> B
//		A <- 200 OK -- B (with to-tag provided by B)
//		A -- ACK    -> B
// 17.2.3 specifies how to match requests for the purpose of identifying a duplicate request.
// 		1.  If via-branch begins with "z9hG4bK" (they couldnt rev the spec number?) then:
//		top via branch and via sent-by match, and method matches request except for ACK which matches INVITE.
// 		2. Otherwise: The request-URI, to-tag, from-tag, call-id, cseq, and top-via-header all match those of the
//		request that is being duplicated.
//		For duplicated ACK detection, the to-tag must match the to-tag of the response sent by the server (of course,
//		which is just another way of saying the ACK is the same as the previous ACK.)
//		Note: the initial INVITE request does not contain a to-tag, so the 'to-tag' part of this rule for matching
//		a duplicated INVITE means the to-tag is empty, because a re-INVITE will include the to-tag of the final response
//		to the INVITE.


// 8.1.3.3: The Via header in an inbound response must have only one via, or it should be discarded.
// The vias in an inbound request include all the proxies, and must be copied verbatim to the outbound response.
// ACK and CANCEL have a single via equal the top via header of the original request, which is interesting
// because it means the proxies must be stateful.
// 18.2.1: for server transport layer: if top via-sent-by is a domain name, must add "received" param with IP address it came from.

// Request inside INVITE
// Write:
// BYE sip:2600@127.0.0.1 SIP/2.0^M
// Via: SIP/2.0/UDP 127.0.0.1:5062;branch=z9hG4bKobts28b7bf3680791a653^M
// From: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1>;tag=tagvcocwyifsgstxlvb^M
// To: <sip:2600@127.0.0.1>;tag=as1ad40dc5^M
// Call-ID: 987045165@127.0.0.1^M
// CSeq: 198 BYE^M
// Contact: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1:5062>^M
// User-Agent: OpenBTS 4.0TRUNK Build Date May 25 2013^M
// Max-Forwards: 70^M
// Content-Length: 0^M
// 
// Receive:
// SIP/2.0 200 OK^M
// Via: SIP/2.0/UDP 127.0.0.1:5062;branch=z9hG4bKobts28b7bf3680791a653;received=127.0.0.1;rport=5062^M
// From: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1>;tag=tagvcocwyifsgstxlvb^M
// To: <sip:2600@127.0.0.1>;tag=as1ad40dc5^M
// Call-ID: 987045165@127.0.0.1^M
// CSeq: 198 BYE^M
// Server: Asterisk PBX 10.0.0^M
// Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, SUBSCRIBE, NOTIFY, INFO, PUBLISH^M
// Supported: replaces, timer^M
// Content-Length: 0^M

// Request outside INVITE
// Write:
// REGISTER sip:127.0.0.1 SIP/2.0^M
// Via: SIP/2.0/UDP 127.0.0.1:5062;branch^M
// From: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1>;tag=tagxwojprsxydjpfaqf^M
// To: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1>^M
// Call-ID: 1543864025@127.0.0.1^M
// CSeq: 244 REGISTER^M
// Contact: <sip:IMSI001010002220002@127.0.0.1:5062>;expires=5400^M
// Authorization: Digest, nonce=7fa68566fa8af919c926247b1b2a04c7, uri=001010002220002, response=a4b2f099^M
// User-Agent: OpenBTS 4.0TRUNK Build Date May 25 2013^M
// Max-Forwards: 70^M
// P-PHY-Info: OpenBTS; TA=0 TE=0.122070 UpRSSI=-36.000000 TxPwr=17 DnRSSIdBm=-77^M
// P-Access-Network-Info: 3GPP-GERAN; cgi-3gpp=0010103ef000a^M
// P-Preferred-Identity: <sip:IMSI001010002220002@127.0.0.1:5060>^M
// Content-Length: 0^M
// Receive:
// SIP/2.0 200 OK^M
// Via: SIP/2.0/UDP localhost:5064;branch=1;received=string_address@foo.bar^M
// Via: SIP/2.0/UDP 127.0.0.1:5062;branch^M
// From: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1>;tag=tagxwojprsxydjpfaqf^M
// To: IMSI001010002220002 <sip:IMSI001010002220002@127.0.0.1>^M
// Call-ID: 1543864025@127.0.0.1^M
// CSeq: 244 REGISTER^M
// Contact: <sip:IMSI001010002220002@127.0.0.1:5062>;expires=5400^M
// User-agent: OpenBTS 4.0TRUNK Build Date May 25 2013^M
// Max-forwards: 70^M
// P-phy-info: OpenBTS; TA=0 TE=0.122070 UpRSSI=-36.000000 TxPwr=17 DnRSSIdBm=-77^M
// P-access-network-info: 3GPP-GERAN; cgi-3gpp=0010103ef000a^M
// P-preferred-identity: <sip:IMSI001010002220002@127.0.0.1:5060>^M
// Content-Length: 0^M

// Receive:
// MESSAGE sip:IMSI001690000000002@127.0.0.1:5062 SIP/2.0^M
// Via: SIP/2.0/UDP 127.0.0.1:5063;branch=123^M
// From: 101 <sip:101@127.0.0.1>;tag=9679^M
// To: <sip:2222222@127.0.0.1>^M
// Call-ID: xqwLM/@127.0.0.1^M
// CSeq: 9679 MESSAGE^M
// Content-Type: application/vnd.3gpp.sms^M
// Content-Length:   158^M
// Write:
// SIP/2.0 200 OK^M
// Via: SIP/2.0/UDP 127.0.0.1:5063;branch=123^M
// From: 101 <sip:101@127.0.0.1>;tag=9679^M
// To: <sip:2222222@127.0.0.1>;tag=onkcqsbyygpcavoa^M
// Call-ID: xqwLM/@127.0.0.1^M
// CSeq: 9679 MESSAGE^M
// User-Agent: OpenBTS 3.0TRUNK Build Date May  3 2013^M
// P-PHY-Info: OpenBTS; TA=1 TE=-0.012695 UpRSSI=-47.250000 TxPwr=9 DnRSSIdBm=-48^M
// P-Access-Network-Info: 3GPP-GERAN; cgi-3gpp=0010103ef000a^M
// P-Preferred-Identity: <sip:IMSI001690000000002@127.0.0.1:5060>^M
// Content-Length: 0^M




static void appendHeader(string *result,const char *name,string value)
{
	if (value.empty()) return;
	result->append(name);
	result->append(": ");
	result->append(value);
	result->append("\r\n");
}
static void appendHeader(string *result,const char *name,const char *value)
{
	if (*value == 0) return;
	result->append(name);
	result->append(": ");
	result->append(value);
	result->append("\r\n");
}
static void appendHeader(string *result,const char *name,int val)
{
	char buf[30];
	snprintf(buf,30,"%d",val);
	appendHeader(result,name,buf);
}

// Generate the string representing this sip message.
string SipMessage::smGenerate(string userAgent)
{
	string result;
	result.reserve(1000);
	char buf[200];

	// First line.
	if (msmCode == 0) {
		// It is a request
		string uri = this->msmReqUri;	// This includes the URI params and headers, if any.
		snprintf(buf,200,"%s %s SIP/2.0\r\n", this->msmReqMethod.c_str(), uri.c_str());
	} else {
		// It is a reply.
		snprintf(buf,200,"SIP/2.0 %u %s\r\n", msmCode, msmReason.c_str());
	}
	result.append(buf);

	appendHeader(&result,"To",this->msmTo.value());
	appendHeader(&result,"From",this->msmFrom.value());

	appendHeader(&result,"Via",this->msmVias);

	appendHeader(&result,"Route",this->msmRoutes);

	appendHeader(&result,"Call-ID",this->msmCallId);

	snprintf(buf,200,"%d %s",msmCSeqNum,msmCSeqMethod.c_str());
	appendHeader(&result,"CSeq",buf);

	if (! msmContactValue.empty()) { appendHeader(&result,"Contact",msmContactValue); }
	if (! msmAuthorizationValue.empty()) { appendHeader(&result,"Authorization",msmAuthorizationValue); }
	// The WWW-Authenticate header occurs only in inbound replies from the Registrar, so we ignore it here.
	if (! msmMaxForwards.empty()) { appendHeader(&result,"Max-Forwards",msmMaxForwards); }

	// These are other headers we dont otherwise process.
	for (SipParamList::iterator it = msmHeaders.begin(); it != msmHeaders.end(); it++) {
		// Take out the headers we are going to add below.
		const char *name = it->mName.c_str();
		if (strcasecmp(name,"User-Agent") && /*strcasecmp(name,"Max-Forwards") &&*/
		    strcasecmp(name,"Content-Type") && strcasecmp(name,"Content-Length")) {
			appendHeader(&result,it->mName.c_str(),it->mValue);
		}
	}
	appendHeader(&result,"User-Agent",userAgent);

	// Append termination cause text see examples below
	// Reason: SIP ;cause=580 ;text="Precondition Failure"
	// Reason: Q.850 ;cause=16 ;text="Terminated"
	// SVG Added 4/21/14 for Lynx  SIPCallTermination

	//LOG(INFO) << "SIP term info copy SIP call termination reasons to SIP message, count: " << SIPMsgCallTerminationList.size(); // SVGDBG
	//string sTemp = SIPMsgCallTerminationList.getTextForAllMsgs();;
	//LOG(INFO) << "SIP term info in smGenerate text: " << sTemp.c_str(); // SVGDBG
	//result.append(sTemp.c_str());
	if (msmReasonHeader.size()) { appendHeader(&result,"Reason",msmReasonHeader); }

	// Create the body, if any.
	appendHeader(&result,"Content-Type",msmContentType);
	appendHeader(&result,"Content-Length",msmBody.size());
	result.append("\r\n");
	result.append(msmBody);
	msmContent = result;
	LOG(INFO) << "SIP term info generated SIP msg: \r\n" << result.c_str();  //SVGDBG

	return msmContent;
}

// Copy the top via from other into this.
void SipMessage::smCopyTopVia(SipMessage *other)
{
	this->msmVias = commaListFront(other->msmVias);
}

// Add a new Via with a new unique branch.
void SipMessage::smAddViaBranch3(string transport, string proxy, string branch)
{
	string newvia = format("SIP/2.0/%s %s;branch=%s\r\n",transport,proxy,branch);
	commaListPushFront(&msmVias,newvia);
}

void SipMessage::smAddViaBranch(string transport, string branch)
{
	smAddViaBranch3(transport,localIPAndPort(),branch);
}

void SipMessage::smAddViaBranch(SipBase *dialog, string branch)
{
	// Add a visible hint to the tag for debugging.  Use the Request Method, if any.
	string newvia = format("SIP/2.0/%s %s;branch=%s\r\n",dialog->transportName(),dialog->localIPAndPort(),branch);
	commaListPushFront(&msmVias,newvia);
}
string SipMessage::smPopVia()		// Pop and return the top via; modifies msmVias.
{
	return commaListPopFront(&msmVias);
}

string SipMessage::smGetProxy() const
{
	string topvia = commaListFront(msmVias);
	if (topvia.empty()) { return string(""); }	// oops
	SipVia via(topvia);
	return via.mSentBy;
}

void SipMessage::addCallTerminationReasonSM(CallTerminationCause::termGroup group, int cause, string desc) {
	LOG(INFO) << "SIP term info addCallTerminationReasonSM cause: " << cause;
	SIPMsgCallTerminationList.add(group, cause, desc);
}


string SipMessage::smGetBranch()
{
	return SipVia(msmVias).mViaBranch;
}

string SipMessage::smGetReturnIPAndPort()
{
	string contactUri;
	if (! crackUri(msmContactValue, NULL,&contactUri,NULL)) {
		LOG(ERR);
	}
	string contact = SipUri(contactUri).uriHostAndPort();
	return contact;
}


string SipMessage::text(bool verbose) const
{
	std::ostringstream ss;
	ss << "SipMessage(";
	{ int code = smGetCode(); ss <<LOGVAR(code); }
	if (!msmReqMethod.empty()) ss <<LOGVAR2("ReqMethod",msmReqMethod);
	if (!msmReason.empty()) ss <<LOGVAR2("reason",msmReason);
	if (!msmCSeqMethod.empty()) ss<<" CSeq="<<msmCSeqNum<< " "<<msmCSeqMethod;
	if (!msmCallId.empty()) ss <<LOGVAR2("callid",msmCallId);
	if (!msmReqUri.empty()) ss << LOGVAR2("ReqUri",msmReqUri);
	{ string To = msmTo.value(); if (!To.empty()) ss << LOGVAR(To); }
	{ string From = msmFrom.value(); if (!From.empty()) ss<<LOGVAR(From); }
	if (!msmVias.empty()) ss <<LOGVAR2("Vias",msmVias);
	if (!msmRoutes.empty()) ss <<LOGVAR2("Routes",msmRoutes);
	if (!msmRecordRoutes.empty()) ss <<LOGVAR2("RecordRoutes",msmRecordRoutes);
	if (!msmContactValue.empty()) ss <<LOGVAR2("Contact",msmContactValue);
	//list<SipBody> msmBodies;
	if (!msmContentType.empty()) ss<<LOGVAR2("ContentType",msmContentType);
	if (!msmBody.empty()) ss<<LOGVAR2("Body",msmBody);
	//if (!msmAuthenticateValue.empty()) ss<<LOGVARM(msmAuthenticateValue);
	if (!msmAuthorizationValue.empty()) ss<<LOGVARM(msmAuthorizationValue);
	for (SipParamList::const_iterator it = msmHeaders.begin(); it != msmHeaders.end(); it++) {
		ss <<" header "<<it->mName <<"="<<it->mValue;
	}

	if (verbose) { ss << LOGVARM(msmContent); } else { ss << LOGVAR2("firstLine",smGetFirstLine()); }
	ss << ")";
	return ss.str();
}
ostream& operator<<(ostream& os, const SipMessage&msg) { os << msg.text(); return os; }
ostream& operator<<(ostream& os, const SipMessage*msg) { os << (msg ? msg->text(false) : "(null SipMessage)"); return os; }

string SipMessage::smGetFirstLine() const
{
	return string(msmContent,0, msmContent.find('\n'));
}

static bool isNumeric(const char *str)
{
	for ( ; *str; str++) { if (!isdigit(*str)) return false; }
	return true;
}

// Validate the IMSI string "IMSI"+digits; return pointer to digits or NULL if invalid.
const char* extractIMSI(const char *IMSI)
{
	// Form of the name is IMSI<digits>, and it should always be 18 or 19 char.
	// IMSIs are 14 or 15 char + "IMSI" prefix
	unsigned namelen = strlen(IMSI);
	if (strncmp(IMSI,"IMSI",4) || (namelen>19) || (namelen<18) || !isNumeric(IMSI+4)) {
		// Note that if you use this on a non-INVITE it will fail, because it may have fields like "222-0123" which are not IMSIs.
		// Dont warn here.  We print a better warning just once in newCheckInvite.
		// LOG(WARNING) << "INVITE with malformed username \"" << IMSI << "\"";
		return "";
	}
	// Skip first 4 char "IMSI".
	return IMSI+4;
}

string SipMessage::smGetPrecis() const
{
	const char *methodname = smGetMethodName();	// May be empty, but never NULL
	if (*methodname) {
		return format("method=%s to=%s",methodname,smGetToHeader().c_str());
	} else {
		return format("response code=%d %s %s to=%s",smGetCode(),smGetReason().c_str(),smCSeqMethod().c_str(),smGetToHeader().c_str());
	}
}


// Multipart SIP body looks like this:
// --terminator
// Content-Type: ...
// eol
// body
// eol
// --terminator
// Content-Type: ...
// eol
// body
// eol
// --terminator--
void SipMessage::smAddBody(string contentType, string body1)
{
	static string separator("--zzyzx\r\n");
	static string terminator("--zzyzx--\r\n");
	static string eol("\r\n");
	if (msmBody.empty()) {
		msmContentType = contentType;
		msmBody = body1;
	} else {
		// TODO: TEST THIS!
		string ctline = format("Content-Type: %s\r\n",contentType.c_str());
		string newbody;
		if (msmContentType.substr(0,strlen("multipart")) != "multipart") {
			// We are switching to multipart now.
			// Move the original content-type/body into the multipart body.
			newbody = separator + ctline + eol + msmBody + eol;
			msmContentType = "multipart/mixed;boundary=zzyzx";
		} else {
			newbody = msmBody;
			// Chop off the previous terminator.
			newbody = newbody.substr(0,newbody.size() - terminator.size());
		}
		// Add the new contentType and body.
		newbody.reserve(msmBody.size() + body1.size() + 100);
		// This requires C++11:
		// newbody.append(separator, ctline, eol, body1, eol, terminator);
		newbody.append(separator + ctline + eol + body1 +  eol +  terminator);
		msmBody = newbody;
	}
}

string SipMessage::smGetMessageBody() const
{
	//if (msmBodies.size() != 1) {
	//	LOG(ERR) << "Message Body invalid "<<msmBodies.size();
	//	return "";
	//}
	//return msmBodies.front().mBody;
	return msmBody;
}

string SipMessage::smGetMessageContentType() const
{
	return msmContentType;
}

string SipMessage::smUriUsername()
{
	string result = SipUri(msmReqUri).uriUsername();
	LOG(DEBUG) <<LOGVAR(msmReqUri) <<LOGVAR(msmTo.value()) <<LOGVAR(result);
	return result;
	//SipUri uri; uri.uriParse(msmReqUri);
	//string username = uri.username();
	//LOG(DEBUG) << LOGVAR(msmReqUri) <<LOGVAR(username);
	//return username;
}

string SipMessage::smGetInviteImsi()
{
	// Get request username (IMSI) from assuming this is the invite message.
	string username = smUriUsername();
	LOG(DEBUG) <<LOGVAR(username) <<LOGVAR(extractIMSI(sipSkipPrefix1(username.c_str())));
	return string(extractIMSI(sipSkipPrefix1(username.c_str())));
}

string SipMessage::smGetRand401()
{
	SipParamList params;
	string authenticate = this->msmHeaders.paramFind("www-authenticate");
	if (authenticate.size() == 0) return "";
	parseAuthenticate(authenticate, params);
	return params.paramFind("nonce");
}



// TODO: This could now be constructed from the dialog state instead of saving the INVITE.
SipMessageAckOrCancel::SipMessageAckOrCancel(string method, SipMessage *other) {
	this->smInit();
	this->msmCallId = other->msmCallId;
	this->msmReqMethod = method;
	this->msmCSeqMethod = method;
	//this->msmReqUri = format("sip:%s@%s",this->mRemoteUsername.c_str(),this->mRemoteDomain.c_str());	// dialed_number@remote_ip
	this->msmReqUri = other->msmReqUri;
	this->msmTo = other->msmTo;		// Has the tag already fixed.
	this->msmFrom = other->msmFrom;	// Has the tag already fixed.
	// For ACK and CANCEL the CSeq equals the CSeq of the INVITE.
	// Note: For others (BYE), they need their own CSeq.
	this->msmCSeqNum = other->msmCSeqNum;
	this->smCopyTopVia(other);
}

// (pat 5-2014) I dont think OpenBTS currently uses any of the below; it just sends all messages and replies to the single
// proxy specified by config option for each of Registration, Speech, and SMS type messages.
// RFC3261 section 4 page 16 describes routing as follows:
// 1. The INVITE is necessarily sent via proxies, which add their own "via" headers.
// 2.  The reply to the INVITE must include the "via" headers so it can get back.
// 3. Subsequently, if there is a Contact field, all messages bypass the proxies and are sent directly to the Contact.
// 4. But the proxies might want to see the messages too, so they can add a "required-route" parameter which trumps
// the "contact" header and specifies that messages are sent there instead.  This is called "Loose Routing."  What a mess.
// And I quote: "These procedures separate the destination of the request (present in the Request-URI) from
//			the set of proxies that need to be visited along the way (present in the Route header field)."
// In contrast, A Strict Router "follows the Route processing rules of RFC 2543 and many prior work in
//			progress versions of this RFC. That rule caused proxies to destroy the contents of the Request-URI
//			when a Route header field was present."
// 8.1.1.1: Normally the request-URI is equal to the To: field.  But if there is a configured proxy (our case)
// this is called a "pre-existing route set" and we must follow 12.2.1.1 using the request-URI as the
// remote target URI(???)
// 12.2: The route-set is immutably defined by the initial INVITE.  You can change the remote-URI in a re-INVITE
// (aka target-refresh-request) but not the route-set.
// Remote-URI: Intial request remote-URI must == To: field.

// Vias:
// o Initial request contains one via which is the resolved return address plus a transaction branch param.

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
//
// TODO:
// 12.2.1.1 The request-URI must be set as follows:
// 1. If there is a non-empty route-set and the first route-set URI contains "lr" param, request-URI = remote-target-URI
//		and must include a Route Header field including all params.
// 2. If there is a non-empty route-set and the first UR does not contain "lr" param,
//		request-URI = first route-route URI with params stripped, and generate new route set with first route removed,
//		andn then add the remote-target-URI at the end of the route header.
// 3. If no route-set, request-URI = remote-target-URI
// The remote-target-URI is set by the Contact header only from a INVITE or re-INVITE.
SipMessageRequestWithinDialog::SipMessageRequestWithinDialog(string reqMethod, SipBase *dialog, string branch) {
	this->smInit();
	this->msmCallId = dialog->callId();
	this->msmReqMethod = reqMethod;
	this->msmCSeqMethod = reqMethod;
	//this->msmReqUri = dialog->mInvite->msmReqUri;		// TODO: WRONG! see comments above.
	this->msmReqUri = dialog->dsInDialogRequestURI();		// TODO: WRONG! see comments above.
	this->msmTo = *dialog->dsRequestToHeader();
	this->msmFrom = *dialog->dsRequestFromHeader();
	this->msmCSeqNum = dialog->dsNextCSeq();	// The BYE seq number must be incremental within the enclosing INVITE dialog.
	if (branch.empty()) { branch = make_branch(reqMethod.c_str()); }
	this->smAddViaBranch(dialog,branch);
}

// This message exists to encapsulate the Dialog State into a SIP message.
// It is created on BS1 to send to BS2, which then uses it as a re-invite.
// So fill it out as a re-invite but leaving the parts based on the BS2 address empty, ie, no via or contact.
// Note that when we send it to BS2 the via and contact are BS1, which BS2 must fix.
// We need to pass the the remote proxy address that we derived from the 'contact' from the peer,
// that was passed in in the initial incoming invite or the response to our outgoing invite.
// We place it in the Refer-To header, and BS2 turns the REFER into an INVITE to that URI.
// This goes in the URI of the header, implying that we cannot send this message to BS2 using normal SIP
// unless we move that to another field, eg, Contact.
SipMessageHandoverRefer::SipMessageHandoverRefer(const SipBase *dialog, string peer)
{
	// The request URI will be the BTS we are sending it to...
	this->smInit();
	this->msmReqMethod = string("REFER");
	this->msmReqUri = makeUri("handover",peer);
	this->msmTo = *dialog->dsRequestToHeader();
	this->msmFrom = *dialog->dsRequestFromHeader();
	this->msmCallId = dialog->callId();
	this->msmCSeqMethod = this->msmReqMethod;	// It is "INVITE".
	// The CSeq num of the are-INVITE follows.
	// RFC3261 14.1: The Cseq for re-INVITE follows same rules for in-dialog requests, namely, CSeq num must be
	// incremented by exactly one.  We do not know if BTS-2 is going to send this this re-INVITE or not,
	// so we send the current CSeqNum without incrementing it and let BTS-2 increment it.
	this->msmCSeqNum = dialog->mLocalCSeq;
	SipParam refer("Refer-To",dialog->dsRemoteURI());	// This is the proxy from the Contact or route header.
	this->msmHeaders.push_back(refer);

	// We need to send the SDP answer, which is the local SDP for MTC or the remote SDP for MOC,
	// but we need to send the peer (remote) RTP port in either case.
	// For the re-invite, you can put nearly anything in here, but you must not just use
	// the identical o= line of the original without at least incrementing the version number.
	SdpInfo sdpRefer, sdpRemote;
	//sdpRefer.sdpInitOffer(dialog);
	sdpRemote.sdpParse(dialog->getSdpRemote());

	// Put the remote SIP server port in the REFER message.  BTS-2 will grab it then substitute its own local port.
	//sdpRefer.sdpRtpPort = sdpRemote.sdpRtpPort;

	sdpRefer.sdpInitRefer(dialog, sdpRemote.sdpRtpPort);

	// TODO: Put the remote session id and version, incrementing the version.  Paul at yate says these should be 0

	//SdpInfo sdpRefer; sdpRefer.sdpParse(dialog->mSdpAnswer);
	//sdpRefer.sdpUsername = dialog->sipLocalusername();

	// Update: This did not work for asterisk either.
	//sdpRefer.sdpUsername = dialog->sipLocalUsername(); // This modification was recommended by Paul for Yate.

	//this->smAddBody(string("application/sdp"),sdpRefer.sdpValue());
	// Try just using the sdpremote verbatim?  Gosh, that worked too.
	this->smAddBody(string("application/sdp"),sdpRefer.sdpValue());

	// The via and contact will be filled in by BS2:
	// this->smAddViaBranch(dialog);
	// this->msmContactValue = dialog->dsRemoteURI();
	// TODO: route set.
}

// section 4: must preserve the vias in the reply to INVITE.
// 12.1.1: Dialog reply must have a contact.
// TODO: Preserve the route set.
// If dialog is non-NULL this is a reply within a dialog.  Note that we create a SipDialog class
// for REGISTER and MESSAGE, and those are not dialogs.
SipMessageReply::SipMessageReply(SipMessage *request, int code, string reason, SipBase *dialog) {
	LOG(INFO) << "SIP term info SipMessageReply SIP code: " << code;
	msmCode = code;
	msmReason = reason;
	if (dialog) {
		// TODO: The route set goes here too.
		// This is a reply, so the initiating request was incoming, so to is local and from is remote.
		msmTo = *dialog->dsReplyToHeader();
		msmFrom = *dialog->dsReplyFromHeader();
	} else {
		msmTo = request->msmTo;
		msmFrom = request->msmFrom;
	}
	msmVias = request->msmVias;
	msmCSeqNum = request->msmCSeqNum;
	msmCSeqMethod = request->msmCSeqMethod;
	msmCallId = request->msmCallId;
	if (dialog && request->msmReqMethod == "INVITE") {
		msmContactValue = dialog->localContact(dialog->sipLocalUsername());
	}
	// These fields are not needed:
	// string mReqMethod;			// It is a reply, not a request.
	//string mContactValue;			// The request is non-target-refresh so contact is meaningless.
}

bool sameMsg(SipMessage *msg1, SipMessage *msg2)
{
	return msg1->msmCSeqNum == msg2->msmCSeqNum && msg1->msmCSeqMethod == msg2->msmCSeqMethod;
}

// Return false if the Max-Forwards is 1 or less or non-integral.
bool SipMessage::smDecrementMaxFowards()
{
	if (msmMaxForwards.size()) {
		assert(msmMaxForwards[0] != ' ');
		int val = atoi(msmMaxForwards.c_str());	// If it is not numeric, it will return 0 silently.
		val--;
		if (val <= 0) { val = 0; }
		msmMaxForwards = format("%d",val);
		return val > 0;
	} else {
		msmMaxForwards = string("70");
		return true;
	}
}


};	// namespace SIP
