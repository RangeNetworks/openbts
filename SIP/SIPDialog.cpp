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

#include "SIPBase.h"
#include "SIPDialog.h"
#include "SIP2Interface.h"
#include "SIPTransaction.h"
#include <Reporting.h>	// For gReports.
#include <L3TranEntry.h>
#include <L3TermCause.h>
#include <L3StateMachine.h>
#include <GSML3MMElements.h>	// for L3CMServiceType
#include <GSML3CCElements.h>	// for L3Cause
#include <algorithm>

namespace SIP {
using namespace Control;
SipDialog *gRegisterDialog = NULL;

// Only the SipMTInviteServerTransactionLayer and SipMOInviteClientTransactionLayer are allowed to call
// the underlying sipWrite method directly for the invite transactions.
void SipDialogBase::sipWrite(SipMessage *sipmsg)
{
	if (!mProxy.mipValid) {
		LOG(ERR) << "Attempt to write to invalid proxy ignored, address:"<<mProxy.mipName;
		return;
	}
	gSipInterface.siWrite(&mProxy.mipSockAddr,sipmsg);
}

SipDialog *SipDialogBase::dgGetDialog()
{
	return dynamic_cast<SipDialog*>(this);
}

SipState SipDialogBase::MODSendCANCEL(TermCause cause)
{
	LOG(INFO) << "SIP term info MODSendCANCEL cause: " << cause;
	LOG(INFO) << sbText();
	setSipState(MODCanceling);  // (pat) MOD sent a cancel.
	SipMOCancelTU *cancelTU = new SipMOCancelTU(dynamic_cast<SipDialog*>(this),cause.tcGetSipReasonHeader());
	//cancelTU->mstOutRequest.addCallTerminationReasonSM(CallTerminationCause::eQ850, cause.tcGetCCCause(), ""); // MODSendCANCEL
	cancelTU->sctStart();
	return getSipState(); 
}

void SipDialogBase::initRTP()
{
	SdpInfo sdp;
	sdp.sdpParse(getSdpRemote().c_str());
	initRTP1(sdp.sdpHost.c_str(),sdp.sdpRtpPort,mDialogId);
}


string SipDialogBase::makeSDPOffer()
{
	SdpInfo sdp;
	sdp.sdpInitOffer(this);
	return sdp.sdpValue();
	//return makeSDP("0","0");
}

// mCodec is an implicit parameter, consisting of the chosen codec.
string SipDialogBase::makeSDPAnswer()
{
	SdpInfo answer;
	answer.sdpInitOffer(this);
	mSdpAnswer = answer.sdpValue();
	return mSdpAnswer;
}

void SipDialogBase::MTCInitRTP()
{
	initRTP();
}

void SipDialogBase::MOCInitRTP()
{
	initRTP();
}


void SipDialogBase::sdbText(std::ostringstream&os, bool verbose) const
{
	sbText(os);

	if (verbose || IS_LOG_LEVEL(DEBUG)) {
		rtpText(os);
		//os << "proxy=(" << mProxy.ipToText() << ")";
	}
}

string SipDialogBase::sdbText() const
{
	std::ostringstream ss;
	sdbText(ss);
	return ss.str();
}

void SipMOInviteClientTransactionLayer::MOUssdSendINVITE(string ussd, const L3LogicalChannel *chan)
{
	static const char* xmlUssdTemplate = 
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<ussd-data>\n"
		" <language>en</language>\n"
		" <ussd-string>%s</ussd-string>\n"
		"</ussd-data>\n";

	LOG(INFO) << "user " << sipLocalUsername() << " state " << getSipState() <<sdbText();

	static const string cInviteStr("INVITE");
	SipMessage *invite = makeInitialRequest(cInviteStr);
	// This is dumber than snot.  We have to put in a dummy sdp with port 0.
	mRTPPort = 0;
	invite->smAddBody(string("application/sdp"),makeSDPOffer());

	// Add RFC-4119 geolocation XML to content area, if available.
	// TODO: This makes it a multipart message, needs to be tested.
	string xml = format(xmlUssdTemplate,ussd);
	invite->smAddBody(string("application/vnd.3gpp.ussd+xml"),xml);

	SipCallbacks::writePrivateHeaders(invite,chan);
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

	LOG(INFO) << "user " << sipLocalUsername() << " state " << getSipState() <<sdbText();
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
					<< format(" -> SIP/%s@%s:%s",remoteIMSI.c_str(),remoteIPStr.c_str(),remotePortStr.c_str()) <<sdbText();
			if (remoteIPStr != "" && remotePort) {
				//directBTSConnection = true;
				mRemoteUsername = remoteIMSI;
				mProxyIP = remoteIPStr;
				mProxyPort = remotePort;
				LOG(INFO) << "Calling BTS direct: "<<wCalledUsername
					<< format(" -> SIP/%s@%s:%u",mRemoteUsername.c_str(),mProxyIP.c_str(),mProxyPort) <<sdbText();
			}
		}
	}
	//mRemoteUsername = "IMSI001690000000002";	// pats iphone
#endif
	
	LOG(DEBUG) <<sdbText();

	static const string cInviteStr("INVITE");
	SipMessage *invite = makeInitialRequest(cInviteStr);
	invite->smAddBody(string("application/sdp"),makeSDPOffer());


	string username = sipLocalUsername();
	WATCH("MOC imsi="<<username);

	if (this->dsPAssociatedUri.size()) {
		invite->smAddHeader("P-Associated-URI",this->dsPAssociatedUri);
	}
	if (this->dsPAssertedIdentity.size()) {
		invite->smAddHeader("P-Asserted-Identity",this->dsPAssertedIdentity);
	}

	SipCallbacks::writePrivateHeaders(invite,chan);
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
	LOG(INFO) << sdbText();
	//LOG(INFO) << "user " << mSipUsername << " state " << getSipState() <<sdbText();

	static const string cAckstr("ACK");
	SipMessageAckOrCancel ack(cAckstr,mInvite);
	ack.msmTo = *dsRequestToHeader();		// Must get the updated to-tag.
	sipWrite(&ack);
	// we dont care mTimerD.set(T4);
}

void SipMOInviteClientTransactionLayer::MOSMSSendMESSAGE(const string &messageText, const string &contentType)
{
	LOG(INFO) << "SIP send to " << dsRequestToHeader() <<" MESSAGE " << messageText <<sdbText();
	assert(mDialogType == SIPDTMOSMS);
	gReports.incr("OpenBTS.SIP.MESSAGE.Out");
	
	static const string cMessagestr("MESSAGE");
	SipMessage *msg = makeInitialRequest(cMessagestr);
	msg->smAddBody(contentType,messageText);
	moWriteLowSide(msg);
	delete msg;
	setSipState(MOSMSSubmit);
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

void SipMTInviteServerTransactionLayer::MTCSendTrying()
{
	SipMessage *invite = getInvite();
	if (invite==NULL) {
		setSipState(SSFail);
		gReports.incr("OpenBTS.SIP.Failed.Local");
	}
	LOG(INFO) << sdbText();
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
	LOG(INFO) <<sdbText();
	assert(invite);
	LOG(DEBUG) << "send ringing" <<sdbText();
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
	LOG(INFO) <<sdbText();
	SipMessageReply ok(invite,200,string("OK"),this);
	ok.smAddBody(string("application/sdp"),makeSDPAnswer());	// TODO: This should be a reply to the originating SDP offer.
	// (pat) Chan is NULL when in a weird special handled in dialogCancel.
	if (chan) SipCallbacks::writePrivateHeaders(&ok,chan);
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
	LOG(INFO) <<sdbText();
	// If this operation was initiated from the CLI, there was no MESSAGE
	if (mInvite) {	// It is a MESSAGE in this case, not an INVITE
		//2-2014: the reply to MESSAGE must include the to-field, so we pass the dialog to SIpMessageReply
		SipMessageReply reply(mInvite,code,string(explanation),this);			// previous: NULL);
		sipWrite(&reply);
	} else {
		LOG(INFO) << "clearing CLI-generated transaction" <<sdbText();
	}
	setSipState(code == 200 ? Cleared : SSFail);
}

// This can only be used for early errors before we get the ACK.
void SipMTInviteServerTransactionLayer::MTCEarlyError(TermCause cause)	// The message must be 300-699.
{
	LOG(DEBUG) << LOGVAR(cause);
	string reason;
	int sipcode = cause.tcGetSipCodeAndReason(reason);
	devassert(sipcode);
	// Double check cause validity.
	if (sipcode == 0) { // This should never happen; this is just a last resort in case of bugs.
		sipcode = 486; reason = "No_User_Responding";
	}
	// TODO: What if we were already ACKed?
	setSipState(MODError);
	SipMessageReply reply(getInvite(),sipcode,reason,this); // The message must be 300-699.
	reply.msmReasonHeader = cause.tcGetSipReasonHeader();		// Returns a SIP "Reason:" header string.
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

SipDialog *getRegistrar()
{
	if (gRegisterDialog == NULL) {
		gRegisterDialog = SipDialog::newSipDialogRegister1();
	} else {
		// This allows the user to change SIP.Proxy.Registration from the CLI.
		gRegisterDialog->updateProxy("SIP.Proxy.Registration");
	}
	return gRegisterDialog;
}


// We wrap our REGISTER messages inside a dialog object, even though it is technically not a dialog.
SipMessage *SipDialog::makeRegisterMsg(DialogType wMethod, const L3LogicalChannel* chan, string RAND, const FullMobileId &msid, const char *SRES)
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
				msg->msmAuthorizationValue = format("Digest realm=\"%s\", username=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\", algorithm=MD5, qop=\"auth\" ",
					realm.c_str(), authUsername.c_str(), RAND.c_str(), authUri.c_str(), response.c_str());
			} else {
				// (pat 9-2014)  These fields are all supposed to be quoted.  It was a mistake not to quote them.
				// The above code that quotes the fields was used for kazoo, so the unquoted strings are only use for sipauthserve.
				// Also remove the extraneous leading comma.
				// RFC3261 says the fields are quoted.
				//msg->msmAuthorizationValue = format("Digest, nonce=%s, uri=%s, response=%s",RAND.c_str(),msid.mImsi.c_str(),SRES);
				msg->msmAuthorizationValue = format("Digest nonce=\"%s\", uri=\"%s\", response=\"%s\"",RAND.c_str(),msid.mImsi.c_str(),SRES);
			}
		}
	} else if (wMethod == SIPDTUnregister ) {
		expires = 0;
	} else { assert(0); }
	// We use myURI instead of localContact because the SIPDialog for REGISTER is shared by all REGISTER
	// users and does not contain the personal info for this user.
	//msg->msmContactValue = format("<%s>;expires=%u",myUriString.c_str(),expires);
	msg->msmContactValue = localContact(username,expires);
	SipCallbacks::writePrivateHeaders(msg,chan);
	return msg;
}

void SipDialog::dgReset()
{
	mPrevDialogState = DialogState::dialogUndefined; sipStopTimers();
	//mDownlinkFifo.clear();
}


void SipDialog::MODSendBYE(TermCause cause)
{
	LOG(INFO) <<sdbText();
	LOG(INFO) << "SIP term info MODSendBYE cause: " << cause; // SVGDBG

	setSipState(MODClearing);
	SipMOByeTU *byeTU = new SipMOByeTU(this,cause.tcGetSipReasonHeader());
	//byeTU->mstOutRequest.addCallTerminationReasonSM(CallTerminationCause::eQ850, cause.tcGetCCCause(), ""); // MODSendBYE
	byeTU->sctStart();
}

void SipDialog::sendInfoDtmf(unsigned bcdkey)
{
	// Has a previous DTMF not finished yet?

	// Start a new Sip INFO Transaction to send the key off.
	SipDtmfTU *dtmfTU = new SipDtmfTU(this,bcdkey);
	dtmfTU->sctStart();
}

// (pat) This is the post-l3-rewrite way, most initialization during construction.
SipDialog *SipDialog::newSipDialogMT(DialogType dtype, SipMessage *req)
{
	LOG(DEBUG);
	assert(dtype == SIPDTMTC || dtype == SIPDTMTSMS);
	string proxy = req->smGetProxy();	// Get it from the top via.
	if (proxy.empty()) {
		LOG(ERR) << "Missing proxy (from top via) in MT SIP message:"<<req;
		// Guess at a proxy and try to keep going.
		proxy = gConfig.getStr(dtype == SIPDTMTSMS ? "SIP.Proxy.SMS" : "SIP.Proxy.Speech");
	}
	SipDialog *dialog = new SipDialog(dtype,proxy,"INVITE or MESSAGE via");
	// 2-2014: RFC 3267 8.2.6.2 says the UAS (sip server) MUST add a "to" tag to a response, and MAY add a "to" tag to a provisional (100) response.
	// The reason is in case the request is forked, the client could distinguish responses from multiple servers, a case that would not happen for us.
	dialog->dsSetLocalHeaderMT(&req->msmTo,dtype == SIPDTMTC);
	dialog->dsSetRemoteHeader(&req->msmFrom);
	//dialog->mSipUsername = req->smUriUsername();	// IMSI/TMSI is in both the URI and the To: header.
	// TODO: Validate username - must be valid IMSI or TMSI.
	ScopedLock lock(dialog->mDialogLock,__FILE__,__LINE__);	// probably unnecessary.
	dialog->dsSetCallId(req->msmCallId);
	dialog->mSdpOffer = req->msmBody;	// Only useful for MTC, a no-op for MTSMS.
	dialog->saveInviteOrMessage(req,false);
	gSipInterface.dmAddCallDialog(dialog);
	return dialog;
}

// There is just one SipDialog that handles all REGISTER requests.
SipDialog *SipDialog::newSipDialogRegister1() 		// caller imsi
{
	LOG(DEBUG);
	SipDialog *dialog = new SipDialog(SIPDTRegister,gConfig.getStr("SIP.Proxy.Registration"),"SIP.Proxy.Registration");
	// RFC3261 10.2: REGISTER fields are different from normal requests.
	// The Request URL is the IP address (only) of the Registrar.
	// The To: is the 'address of record' formatted as a SIP URI.
	// The From: is the 'responsible party' and is equal to To: unless it is a third-party registration.
	// What about tags?  I dont think it needs them because it is not a dialog creating request, but we add them
	// anyway and it hasn't hurt anything.
	dialog->dsSetCallId(globallyUniqueId(""));
	gSipInterface.dmAddCallDialog(dialog);
	return dialog;
}

// Open an MOSMS [Mobile Originated Short Message Service] SIP Transaction and send the invite.
// We use a dialog for this even though it is just a message because it was easier to interface
// to the Control directory without changing anything.
SipDialog *SipDialog::newSipDialogMOSMS(
	TranEntryId tranid,
	const FullMobileId &fromMsId,		// caller imsi
	const string &calledDigits,		// number being called, or it may be config option SIP.SMSC
	const string &body,
	const string &contentType)
{
	LOG(DEBUG) <<LOGVAR(fromMsId)<<LOGVAR2("called",calledDigits); //<<LOGVAR2("tranid",wTranId);
	// This is weird - use the local IP address as the domain of the remote user?
	SipDialog *dialog = new SipDialog(SIPDTMOSMS,gConfig.getStr("SIP.Proxy.SMS"),"SIP.Proxy.SMS");
	dialog->dsSetLocalMO(fromMsId, true);
	string calledDomain = dialog->localIP();
	dialog->dsSetRemoteUri(makeUri(calledDigits,calledDomain));
	dialog->smsBody = body;				// Temporary until smqueue is fixed.
	dialog->smsContentType = contentType;		// Temporary until smqueue is fixed.

	// Must lock once we do dmAddCallDialog to prevent the SIPInterface threads from accessing this dialog
	// while we finish construction.
	ScopedLock lock(dialog->mDialogLock,__FILE__,__LINE__);
	gSipInterface.dmAddCallDialog(dialog);
	//dialog->MOSMSSendMESSAGE(calledDigits,calledDomain,body,contentType);
	gNewTransactionTable.ttSetDialog(tranid,dialog);		// Must do this before the dialog receives any messages.
	dialog->MOSMSSendMESSAGE(body,contentType);
	return dialog;
}


// SIP-URI          =  "sip:" [ userinfo ] hostport
// userinfo         =  ( user / telephone-subscriber ) [ ":" password ] "@"
// user             =  1*( unreserved / escaped / user-unreserved )
// unreserved  =  alphanum / mark
// mark        =  "-" / "_" / "." / "!" / "~" / "*" / "'" / "(" / ")"
// escaped     =  "%" HEXDIG HEXDIG
// user-unreserved  =  "&" / "=" / "+" / "$" / "," / ";" / "?" / "/"
// Any other character needs to be escaped.  RFC 2396.
//
// In general, URI encoding (RFC2396) is kind of complicated - for example, we need a-priori knowledge if ";" in the string is
// marking a URI parameter, in which case it needs to be left alone, or if it is part of the URI component, in which case it must be escaped.
// RFC3267 talks about URBNF the URI string may contain:
// This handles the USSD special case.  A USSD request can contain only digits, letters * and #,
// and of these the only special one is "#" turns into "%23".
string escapeUssdUri(string ss)
{
	size_t pos = 0;
	while (1) {
		pos = ss.find(pos,'#');
		if (pos == string::npos) return ss;
		ss.replace(pos,1,"%23");	// it is ok not to advance pos because replacement "%23" does not include search "#".
	}
	return ss;
}

SipDialog *SipDialog::newSipDialogMOUssd(
	TranEntryId tranid,
	const FullMobileId &fromMsId,		// caller imsi
	const string &wUssd,			// USSD string entered by user to send to network.
	L3LogicalChannel *chan
	)
{
	LOG(DEBUG) << "MOUssd (INVITE)"<<LOGVAR(fromMsId)<<LOGVARM(wUssd);
	// TODO: The SIPEngine constructor calls sipSetUser.  FIX IT.  Maybe I just need to replace SIPEngine.
	const char *proxyOption = "SIP.Proxy.USSD";
	string proxy = gConfig.getStr(proxyOption);
	LOG(DEBUG) << LOGVAR(proxyOption) <<LOGVAR(proxy);
	if (proxy.length() > 259) {	// TODO: This should be in the config checker, if anywhere.
		LOG(ALERT) << "Configured " <<proxyOption <<" hostname is greater than 253 bytes!"; 
	} 
	SipDialog *dialog = new SipDialog(SIPDTMOUssd,proxy,proxyOption);
	dialog->dsSetLocalMO(fromMsId,true);
	gReports.incr("OpenBTS.SIP.INVITE.Out");
	// Must lock once we do dmAddCallDialog to prevent the SIPInterface threads from accessing this dialog
	// while we finish construction.
	ScopedLock lock(dialog->mDialogLock,__FILE__,__LINE__);	// Must lock before dmAddCallDialog.

	if (proxy == "testmode") {
		gNewTransactionTable.ttSetDialog(tranid,dialog);		// Must do this before the dialog receives any messages.
		DialogUssdMessage *dmsg = new DialogUssdMessage(tranid,DialogState::dialogBye,0);
		dmsg->dmMsgPayload = "Hello from OpenBTS.  You entered:"+wUssd;
		LOG(DEBUG) << "USSD test mode"<<LOGVAR(chan)<<LOGVAR(tranid)<<LOGVAR(fromMsId)<<dmsg->dmMsgPayload;
		dialog->dialogQueueMessage(dmsg);
		return dialog;
	}

	// Yes, you really do put the USSD request in the To: of the SIP as per 3GPP 24.090 Appendix A, but we need to
	// escape the "#" character.  We also add a "dialstring" tag.
	// Like this: INVITE sip:*135%23;phone-context=home1.net;user=dialstring SIP/2.0
	// The obvious way to pass a USSD string is as a "tel:" domain, but evidently that was too easy, so we
	// must add the user=dialstring tag as per RFC 4967, and then THAT requires a "context" as per RFC3966 because
	// this is not a "global" tel number, and therefore by definition it must be a "local" tel number, which requires
	// a context, but we learn from RFC 4967 that we can stick in anything we want for the context, since it is meaningless
	// when used for a USSD string.  This spec is not exactly an "A-team" effort.

	string ussdEscaped = escapeUssdUri(wUssd);
	string ussdPerSpec = ussdEscaped + ";phone-context=irrelevant.net;user=dialstring";
	dialog->dsSetRemoteUri(makeUri(ussdPerSpec,dialog->localIP()));
	// TODO: What about codecs?  The example in 24.390 annex A has them.

	gSipInterface.dmAddCallDialog(dialog);
	gNewTransactionTable.ttSetDialog(tranid,dialog);		// Must do this before the dialog receives any messages.
	dialog->MOUssdSendINVITE(wUssd,chan);
	return dialog;
}

// Open an MOC [Mobile Originated Call] dialog and send the invite.
SipDialog *SipDialog::newSipDialogMOC(
	TranEntryId tranid,
	const FullMobileId &fromMsId,			// caller imsi
	const string &wCalledDigits,		// number being called, or empty for an emergency call.
	CodecSet wCodecs,				// phone capabilities
	L3LogicalChannel *chan
	)
{

	LOG(DEBUG) << "MOC SIP (INVITE)"<<LOGVAR(fromMsId)<<LOGVAR2("called",wCalledDigits);
	// TODO: The SIPEngine constructor calls sipSetUser.  FIX IT.  Maybe I just need to replace SIPEngine.
	const char *proxyOption = "SIP.Proxy.Speech";

	string proxy = gConfig.getStr(proxyOption);
	LOG(DEBUG) << LOGVAR(proxyOption) <<LOGVAR(proxy);
	if (proxy.length() > 259) {	// TODO: This should be in the config checker, if anywhere.
		LOG(ALERT) << "Configured " <<proxyOption <<" hostname is greater than 253 bytes!"; 
	} 

	SipDialog *dialog = new SipDialog(SIPDTMOC,proxy,proxyOption);
	dialog->dsSetLocalMO(fromMsId,true);

	{
		gReports.incr("OpenBTS.SIP.INVITE.Out");
		dialog->dsSetRemoteUri(makeUri(wCalledDigits,dialog->localIP()));
	}

	string username = fromMsId.fmidUsername();
	if (username.length()) {
		devassert(username.substr(0,4) == "IMSI");
		string pAssociatedUri, pAssertedIdentity;
		gTMSITable.getSipIdentities(username.substr(4),pAssociatedUri,pAssertedIdentity); // They may be empty.
		dialog->dsPAssociatedUri = pAssociatedUri;
		dialog->dsPAssertedIdentity = pAssertedIdentity;
		WATCH("MOC"<<LOGVAR(username)<<LOGVAR(pAssociatedUri)<<LOGVAR(pAssertedIdentity));
		//if (pAssociatedUri.size()) { invite->smAddHeader("P-Associated-URI",pAssociatedUri); }
		//if (pAssertedIdentity.size()) { invite->smAddHeader("P-Asserted-Identity",pAssertedIdentity); }
	}

	dialog->mRTPPort = Control::allocateRTPPorts();
	dialog->mCodec = wCodecs;

	// Must lock once we do dmAddCallDialog to prevent the SIPInterface threads from accessing this dialog
	// while we finish construction.
	ScopedLock lock(dialog->mDialogLock,__FILE__,__LINE__);	// Must lock before dmAddCallDialog.
	gSipInterface.dmAddCallDialog(dialog);
	gNewTransactionTable.ttSetDialog(tranid,dialog);		// Must do this before the dialog receives any messages.
	dialog->MOCSendINVITE(chan);
	return dialog;
}

// This is called in BS2 after a handover complete is received.  It is an inbound handover, but an outoing MO re-INVITE.
// We take the SIP REFER message created by BS1 and send it to the SIP server as a re-INVITE.
// Note that the MS may go from BS1 to BS2 and back to BS1, in which case there may
// already be an existing dialog in some non-Active state.
SipDialog *SipDialog::newSipDialogHandover(TranEntry *tran, string sipReferStr)
{
	LOG(DEBUG)<<LOGVAR(tran) <<LOGVAR(sipReferStr);
	static const string inviteStr("INVITE");

	// Init the Dialog State from the SIP REFER message.
	SipMessage *msg = sipParseBuffer(sipReferStr.c_str());
	if (msg == NULL) { return NULL; }	// Message already printed.
	SipUri referto(msg->msmHeaders.paramFind("Refer-To"));
	string proxy = referto.uriHostAndPort();
	// 7-23 wrong: SipDialog *dialog = new SipDialog(SIPDTMTC,proxy);
	SipDialog *dialog = new SipDialog(SIPDTMOC,proxy,"REFER message");
	dialog->mIsHandover = true;
	dialog->dsSetRemoteHeader(&msg->msmTo);
	dialog->dsSetLocalHeader(&msg->msmFrom);
	dialog->dsSetCallId(msg->msmCallId);
	// TODO: If any other intervening messages were sent by BTS1 between the REFER and now the CSeqNum will not be correct.
	dialog->mLocalCSeq = msg->msmCSeqNum + 1;
	// We copied the peer SDP we got from the SIP server into the handover message passed from BS1 to BS2;
	// I dont think we need to save sdpResponse here - we are going to use it for the last time immediately below.
	dialog->mCodec = tran->getCodecs();			// TODO: We need to renegotiate this, or set it from SDP.  There is no point even setting this here.

	// Get remote RTP from SIP REFER message, init RTP, create new SDP offer from previous SDP response.
	// The incoming SDP has the codec previously negotiated, so it should still be ok.
	dialog->mRTPPort = Control::allocateRTPPorts();
	SdpInfo sdpRemote;
	sdpRemote.sdpParse(msg->msmBody);
	SdpInfo sdpLocal = sdpRemote;	// In particular, we are copying the sessionId and versionId.
	// Send our local RTP port to the SIP server.
	sdpLocal.sdpRtpPort = dialog->mRTPPort;
	sdpLocal.sdpHost = dialog->localIP();
	dialog->mSdpOffer = sdpLocal.sdpValue();

	// Make the re-INVITE
	SipMessage *invite = dialog->makeInitialRequest(inviteStr);
	invite->smAddBody(string("application/sdp"),dialog->mSdpOffer);

	// Send it off.
	ScopedLock lock(dialog->mDialogLock,__FILE__,__LINE__);
	gSipInterface.dmAddCallDialog(dialog);
	dialog->moWriteLowSide(invite);
	delete invite;	// moWriteLowSide saved a copy of this.
	dialog->setSipState(HandoverInbound);

	return dialog;
}


SipDialog::~SipDialog()
{
	// nothing
}

TranEntry *SipDialog::findTranEntry()
{
	if (this->mTranId == 0) {
		// No attached transaction.  Can happen if we jumped the gun (the dialog is created before the transaction
		// and there could be a race with an incoming message) or if we responded with an early error
		// to a dialog and never created a transaction for it, for example, 486 Busy Here.
		return NULL;
	}
	return gNewTransactionTable.ttFindById(this->mTranId);
}

// If it is not an IMSI we think it may be a phone number.
static bool isPhoneNumber(string thing)
{
	if (thing.size() == 0) { return false; }	// Not a phone number.
	if (0 == strncasecmp(thing.c_str(),"IMSI",4)) { return false; }	// It is an IMSI, not a phone number.
	return true;	// Well, maybe it is a phone number.
}

static string removeUriFluff(string thing)
{
	if (unsigned first = thing.find('<') != string::npos) {	// Remove the angle brackets.
		thing = thing.substr(first);                        // chop off initial angle bracket.
		thing = thing.substr(0,thing.find_last_of('>'));    // chop off trailing angle bracket, and anything following.
	}
	thing = thing.substr(0,thing.find_last_of('@'));		// Chop off the ip address, if any.
	const char *str = thing.c_str();
	// Very clever, that a phone number may be prefixed with either sip: or tel:
	if (0 == strncasecmp(str,"sip:",4) || 0 == strncasecmp(str,"tel:",4)) {
		thing = thing.substr(4);
	} else if (0 == strncasecmp(str,"sips:",5)) {	// secure not supported, but always hopeful....
		thing = thing.substr(5);
	}
	LOG(DEBUG) <<LOGVAR(thing);
	return thing;	// Hopefully, what is remaining is a phone number.
}


TranEntry *SipDialog::createMTTransaction(SipMessage *invite)
{
	TranEntry *tran = NULL;
	string callerId;
	string callerIdSource = gConfig.getStr("GSM.CallerID.Source");
	if (0 == strcasecmp(callerIdSource.c_str(),"auto")) {
		// (pat 6-2014) Added automatic caller id identification.
		// We can do this automatically because we know whether the displayname, etc, are imsi because they are preceded by "IMSI".
		// btw, the P-Asserted-Identity should by an IMSI; the phone number is supposed to be in the P-Associated-URI.
		callerId = sipRemoteDisplayname();	// This is out best guess.
		LOG(DEBUG) << "CallerID=auto: sipRemoteDisplayname="<<callerId;
		if (! isPhoneNumber(callerId)) {
			// Well that wasnt it, try again...
			callerId = removeUriFluff(invite->msmHeaders.paramFind("P-Associated-URI"));
			LOG(DEBUG) << "CallerID=auto: P-Associated-URI="<<callerId;
		}
		if (! isPhoneNumber(callerId)) {
			// Keep trying...
			callerId = removeUriFluff(invite->msmHeaders.paramFind("P-Asserted-Identity"));
			LOG(DEBUG) << "CallerID=auto: P-Asserted-Identity="<<callerId;
		}
		if (! isPhoneNumber(callerId)) {
			// The SIP username is not likely a phone number, but it is our last hope.
			callerId = removeUriFluff(sipRemoteUsername());
			LOG(DEBUG) << "CallerID=auto: sipRemoteUserName="<<callerId;
		}
		if (! isPhoneNumber(callerId)) {
			callerId = string("");	// Did not find a phone number, sigh, zero it out.
			LOG(DEBUG) << "CallerID=auto: giving up";
		}
	} else if (callerIdSource.compare("username") == 0) {
		callerId = sipRemoteUsername();
		LOG(INFO) << "source=username, callerId = " << callerId;
	} else if (callerIdSource.compare("p-asserted-identity") == 0) {
		string tmpcid = invite->msmHeaders.paramFind("P-Asserted-Identity");
		unsigned first = tmpcid.find("<sip:");
		unsigned last = tmpcid.find_last_of("@");
		callerId = tmpcid.substr(first+5, last-first-5);
		LOG(INFO) << "source=p-asserted-identity, callerId = " << callerId;
	} else {
		callerId = sipRemoteDisplayname();
		LOG(INFO) << "source=username, callerId = " << callerId;
	}
	FullMobileId msid;
	msid.mImsi = invite->smGetInviteImsi();
	if (invite->isINVITE()) {
		tran = TranEntry::newMTC(this,msid,GSM::L3CMServiceType::MobileTerminatedCall,callerId);
		// Tell the sender we are trying.
		this->MTCSendTrying();
	} else {
		devassert(0);
	}
	return tran;
}

// If the cause is handoverOutbound, kill the dialog now: dont send a BYE, dont wait for any other incoming messsages.
// Used for outbound handover, where the SIP session was transferred to another BTS.
// cause == This is the reason a Transaction (TranEntry) was cancelled
// Layer3 may call this multiple times just to be safe and make sure the dialog is truly cancelled;
// generally the first call includes the real causes, then the second call is made when the transaction
// is destroyed and the cause is some bogus cause.
void SipDialog::dialogCancel(TermCause cause)
{
	SIP::SipState state = this->getSipState();
	//bool bTerminationAdded = false;

	LOG(INFO) << "SIP term info dialogCancel begin cancel cause: " << cause;  // SVGDBG

	WATCH("dialogCancel"<<LOGVAR(state)<<LOGVAR(cause) )
	ScopedLock lock(mDialogLock,__FILE__,__LINE__);

	LOG(DEBUG) << dialogText(); // "SIP state " << state;   //SVGDBG switch to LOG(INFO)

	if (cause.tcGetValue() == L3Cause::Handover_Outbound) {
			// Terminate the dialog instantly.  Dont send anything on the SIP interface.
			sipStopTimers();
			// We need to remove the callid of the terminated outbound dialog queue from SIPInterface in case
			// the same call is handerovered back, it would then be a duplicate.
			gSipInterface.dmRemoveDialog(this);
			LOG(INFO) << "SIP term info dialogCancel dmRemoveDialog return";
			return;
	}

	// why aren't we checking for failed here? -kurtis ; we are now. -david
	if (this->sipIsFinished()) {
		// No bye or cancel message will be sent.
		LOG(INFO) << "SIP term info dialogCancel sipIsFinished return SIP state: " << this->getSipState();
		return;
	}
	switch (mDialogType) {
	case SIPDTRegister:
	case SIPDTUnregister:
		// The Register is not a full dialog so we dont send anything when we cancel.
		break;
	case SIPDTMOSMS:
	case SIPDTMTSMS:
	case SIPDTMOUssd:
		setSipState(Cleared);
		break;
	case SIPDTMTC:
	case SIPDTMOC:
		switch (state) {
			case SSTimeout:
			case MOSMSSubmit:	// Should never see a message state in an INVITE dialog.
				LOG(ERR) "Unexpected SIP State:"<<state;
				break;
			case Active:			// (pat) MOC received OK; MTC sent ACK
			caseActive:
				//Changes state to clearing
				this->MODSendBYE(cause);
				//bTerminationAdded = true;
				//then cleared
				sipStartTimers(); // formerly: MODWaitForBYEOK();
				break;
			case SSNullState:	// (pat) MTC initial state - nothing sent yet. MOC not used because sends INVITE on construction.
			case Starting:		// (pat) MOC or MOSMS or inboundHandover sent INVITE; MTC not used.
			case Proceeding:	// (pat) MOC received Trying, Queued, BeingForwarded; MTC sent Trying
			case Ringing:		// (pat) MOC received Ringing, notably not used for MTC sent Ringing.
			case MOCBusy:		// (pat) MOC received Busy; MTC not used.
			case Connecting:		// (pat) MTC sent OK.
			case HandoverInbound:
				if (mDialogType == SIPDTMOC) {
					// To cancel the invite before the ACK is received we must send CANCEL instead of BYE.
					this->MODSendCANCEL(cause); //Changes state to MODCanceling
					//bTerminationAdded = true;
				} else {
					// We are the INVITE recipient server and have not received the ACK yet, so we must send an error response.
					// Way back in version 3 this was used for MTC also.
					// RFC3261 (SIP) is internally inconsistent describing the error codes - the 4xxx and 5xx generic
					// descriptions are contradicted by specific error code descriptions.
					// This is from Paul Chitescu at Null Team:
					// "A 504 Server Timeout seems the most adequate response [to MS not responding to page.]
					// 408 is reserved for SIP protocol timeouts (no answer to SIP message)
					// 504 indicates some other timeout beyond SIP (interworking)
					// 480 indicates some temporary form of resource unavailability or congestion but 
					// resource is accessible and can be checked"
					// 486 "Busy Here" implies that we found the MS but it really is busy.
					// 503 indicates the service is unavailable but does not imply for how long
					// TODO: We should probably send different codes for different reasons.
					// Note: We previously sent 480.
					//this->MTCEarlyError(480, "Temporarily Unavailable"); // The message must be 300-699.

					if (cause.tcGetCCCause() == L3Cause::Normal_Call_Clearing) {
						// (pat 7-2014) The handset MTC hung up normally but the SIP Dialog here is not in the Active state.
						// This weird case occurs if the handset sends a disconnect before it sends a connect,
						// which can happen on some if the user answers and hangs up immediately.
						// We need to send a 200 OK and then BYE instead of sending an early error.
						// We dont care about codecs because we are clearing immediately so just regurgitate the codecs from the INVITE.
						LOG(DEBUG) << "SIP SPECIAL CASE: Disconnect before connect";
						this->MTCSendOK(this->vGetCodecs(), NULL);
						goto caseActive;
					} else {
						this->MTCEarlyError(cause);
					}
#if PREVIOUS_CODE
// (pat) Keeping this old code here for a while to show what SIP responses were returned by the version 4 code.
// Now SIP responses are formulated from the TermCause by tcGetSipCodeAndReason()
					int sipcode = 486; const char *reason = "No answer";
					switch (cause) {
						case CancelCauseHandoverOutbound:
						case CancelCauseSipInternalError:
							assert(0);		// handled above
						case CancelCauseNormalDisconnect:		// 0 Loss of contact with MS or an error.
						case CancelCauseBusy:			// MS is here and unavailable.
							sipcode = 486; reason = "Busy Here";
							break;

						case CancelCauseUnknown:		// 0 Loss of contact with MS or an error.
							if (l3Cause == L3Cause::SwitchingEquipmentCongestion) {
								sipcode = 503; reason = "Normal circuit congestion";
							}
							else if (l3Cause == L3Cause::NoUserResponding) {
								sipcode = 408; reason = "No user responding";
							}
							else if (l3Cause == L3Cause::CallRejected) {
								sipcode = 603; reason = "Call rejected";
							}

							break;

						case CancelCauseCongestion:		// This reason is never used MS is here but no channel avail or other congestion.
							sipcode = 503; reason = "Normal circuit congestion";
							break;

						case CancelCauseNoAnswerToPage:	// Not used   We dont have any clue if the MS is in this area or not.
							// The MS is not here or turned off.
							sipcode = 408; reason = "No user responding";
							break;
						case CancelCauseOperatorIntervention:
							sipcode = 487; reason = "Request Terminated Operator Intervention";
							break;
					}
					this->MTCEarlyError(sipcode,reason); // The message must be 300-699.
#endif
					//bTerminationAdded = true;
				}
				break;
			case MODClearing:	// (pat) MOD sent BYE
			case MODCanceling:	// (pat) MOD sent a cancel.
			case MODError:		// (pat) MOD sent an error response.
			case MTDClearing:	// (pat) MTD received BYE.
			case MTDCanceling:	// (pat) MTD received CANCEL
			case Canceled:		// (pat) received OK to CANCEL.
			case Cleared:		// (pat) MTD sent OK to BYE, or MTD internal error loss of FIFO, or MOSMS received OK, or MTSMS sent OK.
			case SSFail:
				// Some kind of clearing already in progress.  Do not repeat.
				break;
			case HandoverOutbound:	// We never used this state.
				// Not sure what to do with these.
				break;
		}
		break;
	default:
		assert(0);
		break;
	}

	LOG(INFO) << "SIP term info end dialogCancel, cancel cause: " << cause << " mDialogType: " << mDialogType <<LOGVAR(state)<<LOGVAR(cause);
		// << " bTerminationAdded: " << bTerminationAdded;

	// (pat) The TermCause has the complete termination reason.  The cause was sent in the error response, CANCEL message, or BYE message;
	// we dont need to save it in the dialog.
	// Save termination reason
	//if (!bTerminationAdded) {
	//		if (! cause.tcIsEmpty()) {  // Don't log unknown reason codes
	//			LOG(INFO) << "SIP term info add message from dialogCancel"<<LOGVAR(cause);  // SVGDBG
	//			addCallTerminationReasonDlg(CallTerminationCause::eQ850, cause.tcGetCCCause(), ""); // dialCancel
	//		}
	//}

} // dialogCancel


void SipEngine::dialogQueueMessage(DialogMessage *dmsg)
{
	// This was used when there was just one layer3 thread:
	// TODO: We may still use this for UMTS.
	//Control::gCSL3StateMachine.csl3Write(new Control::GenericL3Msg(dmsg,callID()));
	// Now we enqueue dialog messages in a queue in their dialog, and let L3 fish it out from there.
	// We dont enqueue on the GSM LogicalChannel because that may change from, eg, SDCCH to FACCH before this message is processed.
	LOG(DEBUG) << "sending DialogMessage to L3 " /*<<dialogText()*/ <<LOGVAR(dmsg);
	//mDownlinkFifo.write(dmsg);
	if (mTranId == 0) {
		// pat 5-2014: There will not be a transaction if the dialog was cancelled in handleInvite because the
		// called user is already busy with no transaction slot available.  This case will go away when
		// we support call-hold/call-wait.
		LOG(DEBUG) << "Warning: dialog with no attached transaction; DialogMessage ignored.";
		delete dmsg;
		return;
	}
	gNewTransactionTable.ttAddMessage(mTranId,dmsg);
}

bool SipDialog::permittedTransition(DialogState::msgState oldState, DialogState::msgState newState)
{
	if (newState > oldState) { return true; }	// That was easy!
	if (newState == oldState) {
		// Allow multiple proceeding/ringing notifications:
		if (newState == DialogState::dialogProceeding || newState == DialogState::dialogRinging) { return true; }
	}
	return false;
}

void SipDialog::dialogPushState(
	SipState newSipState,				// The new sip state.
	int code,						// The SIP message code that caused the state change, or 0 for ACK or total failures.
	char timer)
{
	SipState oldSipState = getSipState();
	DialogState::msgState oldDialogState = getDialogState();
	setSipState(newSipState);

	// If it is a new state, inform L3.
	DialogState::msgState nextDialogState = getDialogState();	// based on the newSipState we just set.
	LOG(DEBUG) <<LOGVAR(oldSipState)<<LOGVAR(newSipState)<<LOGVAR(getSipState())<<LOGVAR(mPrevDialogState)<<LOGVAR(oldDialogState)<<LOGVAR(nextDialogState)<<dialogText();
	if (nextDialogState == DialogState::dialogStarted) {
		// This state is used for MO transactions just to indicate the dialog is active,
		// but the MO state machine already knows that since it created the dialog,
		// so we dont return this state as a notification.
		return;
	}
	if (permittedTransition(mPrevDialogState,nextDialogState)) {
		DialogMessage *dmsg = new DialogMessage(mTranId,nextDialogState,code);
		dialogQueueMessage(dmsg);
	} else {
		LOG(DEBUG) << "no dialog state change";
	}
	mPrevDialogState = nextDialogState;

	// A timer may be specified if the SIP state is one indicating failure.
	// The timer letter corresponds to one of those specified in RFC3261, and specifies the dialog
	// should not be destroyed until the timer expires.
	switch (timer) {
		case 0:
			break;	// default, no timer specified.
		case 'D':
			// RFC3261 17.1.1.2 says set Timer D to 32s instead of 64*T1.  Whatever.
			if (dsPeer()->ipIsReliableTransport()) {
				mTimerD.setOnce(32000);
			}
			break;
		case 'K':
			mTimerK.setOnce(T4);
			break;
		default: assert(0);
	}
}


void SipDialog::dialogChangeState(
	SipMessage *sipmsg)						// The message that caused the state change, or NULL for total failures.
{
	dialogPushState(getSipState(),sipmsg?sipmsg->smGetCode():0);
	//LOG(DEBUG) <<dialogText();
	//// If it is a new state, inform L3.
	//DialogState::msgState nextDialogState = getDialogState();
	//if (nextDialogState == DialogState::dialogStarted) {
	//	// This state is used for MO transactions just to indicate the dialog is active,
	//	// but the MO state machine already knows that since it created the dialog,
	//	// so we dont return this state as a notification.
	//	return;
	//}
	//if (permittedTransition(mPrevDialogState,nextDialogState)) {
	//	unsigned code = sipmsg ? sipmsg->smGetCode() : 0;
	//	DialogMessage *dmsg = new DialogMessage(mTranId,nextDialogState,code);
	//	// done by the register TU
	//	dialogQueueMessage(dmsg);
	//} else {
	//	LOG(DEBUG) << "no dialog state change";
	//}
	//mPrevDialogState = nextDialogState;
}

// Only a small subset of SIP states are passed to the L3 Control layer as dialog states.
DialogState::msgState SipDialog::getDialogState() const
{
	// Do not add a default case so that if someone adds a new SipState they will get a warning here.
	// Therefore we define every state including the impossible ones.
	switch (getSipState()) {
	case SSNullState:
		return DialogState::dialogUndefined;
	case Starting:		// (pat) MOC or MOSMS or inboundHandover sent INVITE; MTC not used.
		return DialogState::dialogStarted;
	case Proceeding:		// (pat) MOC received Trying, Queued, BeingForwarded; MTC sent Trying
	case Connecting:		// (pat) MTC sent OK.
		// TODO: Is this correct for MTC Connecting?
		return DialogState::dialogProceeding;
	case Ringing:		// (pat) MOC received Ringing, notably not used for MTC sent Ringing, which is probably a bug of no import.
		return DialogState::dialogRinging;
	case Active:			// (pat) MOC received OK; MTC sent ACK
		return DialogState::dialogActive;

	case MODClearing:	// (pat) MOD sent BYE
	case MODCanceling:	// (pat) MOD sent a cancel.
	case MTDClearing:	// (pat) MTD received BYE.
	case MTDCanceling:	// (pat) received CANCEL
	case Canceled:		// (pat) received OK to CANCEL.
	case Cleared:		// (pat) MTD sent OK to BYE, or MTD internal error loss of FIFO, or MOSMS received OK, or MTSMS sent OK.
		return DialogState::dialogBye;

	case MOCBusy:			// (pat) MOC received Busy; MTC not used.
	case SSTimeout:
	case MODError:		// (pat) MOD sent a cancel.
	case SSFail:
		return DialogState::dialogFail;

	//case SipRegister:	// (pat) This SIPEngine is being used for registration, none of the other stuff applies.
	//case SipUnregister:	// (pat) This SIPEngine is being used for registration, none of the other stuff applies.
	case MOSMSSubmit:		// (pat) SMS message submitted, "MESSAGE" method.  Set but never used.
	case HandoverInbound:
	case HandoverOutbound:
		return DialogState::dialogUndefined;
	}
	devassert(0);
	return DialogState::dialogUndefined;
}


// Handle response to INVITE or MESSAGE.
// Only responses (>=200) to INVITE get an ACK.  Specifically, not MESSAGE.
void SipDialog::handleInviteResponse(int status,
	bool sendAck)		// TRUE if transaction is INVITE.  We used to use this for MESSAGE also, in which case it was false.
{
	LOG(DEBUG) <<LOGVAR(status) <<LOGVAR(sendAck);
	switch (status) {
		// class 1XX: Provisional messages
		case 100:	// Trying
		case 181:	// Call Is Being Forwarded
		case 182:	// Queued
		case 183:	// Session Progress FIXME we need to setup the sound channel (early media)
			dialogPushState(Proceeding,status);
			break;
		case 180:	// Ringing
			mReceived180 = true;
			dialogPushState(Ringing,status);
			break;

		// class 2XX: Success
		case 200:	// OK
				// Save the response and update the state,
				// but the ACK doesn't happen until the call connects.
			dialogPushState(Active,status);
			break;

		// class 3xx: Redirection
		case 300:	// Multiple Choices
		case 301:	// Moved Permanently
		case 302:	// Moved Temporarily
		case 305:	// Use Proxy
		case 380:	// Alternative Service
			LOG(NOTICE) << "redirection not supported code " << status <<sdbText();
			dialogPushState(SSFail,status, 'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.3xx");
			// TODO: What if it is not MOC?
			if (sendAck) MOCSendACK();
			break;
		// Anything 400 or above terminates the call, so we ACK.
		// FIXME -- It would be nice to save more information about the
		// specific failure cause.

		// class 4XX: Request failures
		case 405:	// Method Not Allowed
			// We must not ACK to "405 Method Not Allowed" or you could have an infinite loop.  Saw this with smqueue.
			dialogPushState(SSFail,status, 'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.4xx");
			break;

		case 400:	// Bad Request
		case 401:	// Unauthorized: Used only by registrars. Proxys should use proxy authorization 407
		case 402:	// Payment Required (Reserved for future use)
		case 403:	// Forbidden
		case 404:	// Not Found: User not found
		case 406:	// Not Acceptable
		case 407:	// Proxy Authentication Required
		case 408:	// Request Timeout: Couldn't find the user in time
		case 409:	// Conflict
		case 410:	// Gone: The user existed once, but is not available here any more.
		case 413:	// Request Entity Too Large
		case 414:	// Request-URI Too Long
		case 415:	// Unsupported Media Type
		case 416:	// Unsupported URI Scheme
		case 420:	// Bad Extension: Bad SIP Protocol Extension used, not understood by the server
		case 421:	// Extension Required
		case 422:	// Session Interval Too Small
		case 423:	// Interval Too Brief
		case 480:	// Temporarily Unavailable
		case 481:	// Call/Transaction Does Not Exist
		case 482:	// Loop Detected
		case 483:	// Too Many Hops
		case 484:	// Address Incomplete
		case 485:	// Ambiguous
			LOG(NOTICE) << "request failure code " << status <<sdbText();
			dialogPushState(SSFail,status, 'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.4xx");
			if (sendAck) MOCSendACK();
			break;

		case 486:	// Busy Here
			LOG(NOTICE) << "remote end busy code " << status <<sdbText();
			dialogPushState(MOCBusy,status,'D');
			// TODO: What if it is not MOC?
			if (sendAck) MOCSendACK();
			break;
		case 487:	// Request Terminated
		case 488:	// Not Acceptable Here
		case 491:	// Request Pending
		case 493:	// Undecipherable: Could not decrypt S/MIME body part
			LOG(NOTICE) << "request failure code " << status <<sdbText();
			dialogPushState(SSFail,status,'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.4xx");
			if (sendAck) MOCSendACK();
			break;

		// class 5XX: Server failures
		case 500:	// Server Internal Error
		case 501:	// Not Implemented: The SIP request method is not implemented here
		case 502:	// Bad Gateway
		case 503:	// Service Unavailable
		case 504:	// Server Time-out
		case 505:	// Version Not Supported: The server does not support this version of the SIP protocol
		case 513:	// Message Too Large
			LOG(NOTICE) << "server failure code " << status <<sdbText();
			dialogPushState(SSFail,status,'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.5xx");
			// TODO: What if it is not MOC?
			if (sendAck) MOCSendACK();
			break;

		// class 6XX: Global failures
		case 600:	// Busy Everywhere
		case 603:	// Decline
			dialogPushState(MOCBusy,status,'D');
			if (sendAck) MOCSendACK();
			break;
		case 604:	// Does Not Exist Anywhere
		case 606:	// Not Acceptable
			LOG(NOTICE) << "global failure code " << status <<sdbText();
			dialogPushState(SSFail,status,'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.6xx");
			if (sendAck) MOCSendACK();
		default:
			LOG(NOTICE) << "unhandled status code " << status <<sdbText();
			dialogPushState(SSFail,status,'D');
			gReports.incr("OpenBTS.SIP.Failed.Remote.xxx");
			if (sendAck) MOCSendACK();
	}
}

// Look for <tag>blah</tag> in xmlin and return "blah".
static string xmlFind(const char *xmlin, const char *tag)
{
	char tagbuf[56];
	assert(strlen(tag) < 50);
	sprintf(tagbuf,"<%s>",tag);
	const char *start = strstr(xmlin,tagbuf);
	if (!start) return string("");
	const char *result = start + strlen(tagbuf);
	sprintf(tagbuf,"</%s>",tag);
	const char *end = strstr(start,tagbuf);
	if (!start) return string("");
	return string(result,end-result);
}

// The incoming USSD BYE message could have a payload to be sent to the MS.
void SipDialog::handleUssdBye(SipMessage *msg)
{
	// There could be multiple BYE messages, hopefully all identical, but we only want to send one DialogMessage.
	if (getSipState() == Cleared) return;
	DialogUssdMessage *dmsg = new DialogUssdMessage(mTranId,DialogState::dialogBye,0);
	// Is it is ok for there to be no response string?
	// We have to send something to the MS so in that case return an empty string.
	if (msg->smGetMessageContentType().find("application/vnd.3gpp.ussd+xml") == string::npos) {
		LOG(INFO) << "UUSD response does not contain correct body type";
	} else {
		dmsg->dmMsgPayload = xmlFind(msg->smGetMessageBody().c_str(),"ussd-string");
		if (dmsg->dmMsgPayload == "") {
			// This is ok.
			LOG(INFO) << "Missing UUSD response does not contain correct body type";
		}
	}
	dialogQueueMessage(dmsg);
	if (dsPeer()->ipIsReliableTransport()) {
		dialogPushState(Cleared,0);
	} else {
		dialogPushState(MTDClearing,0);
		setTimerJ();
	}
}


// The SIPInterface sends this to us based on mCallId.
// We will process the message and possibly send replies or DialogMessages to the L3 State machine.
// Blah, this should be handled by Dialog sub-classes.
void SipDialog::dialogWriteDownlink(SipMessage *msg)
{
	LOG(DEBUG) <<"received SIP" /*<<LOGVAR2("SIP.state",sipState())*/ <<" msg:"<<msg->text() <<dialogText();
	sipStopTimers();
	ScopedLock lock(mDialogLock,__FILE__,__LINE__);

	unsigned code = msg->smGetCode();

	//if (code == 200) { saveResponse(msg); }
	//if (code >= 400) { mFailCode = code; }

	//SipDialog::msgState nextDialogState = sipMessage2DialogState(msg);

	switch (mDialogType) {
	case SIPDTRegister:
	case SIPDTUnregister:
		LOG(ERR) << "REGISTER transaction received unexpected message:"<<msg;
		break;
	case SIPDTMOUssd:
		LOG(DEBUG);
		if (code == 0 && msg->isBYE()) {	// It is a SIP Request.  Switch based on the method.
			// Grab any xml ussd response from the BYE message.
			handleUssdBye(msg);
		}
		goto otherwise;
	case SIPDTMOC: // This is a MOC transaction.
	case SIPDTMTC: // This is a MTC transaction.  Could be an inbound handover.
		LOG(DEBUG);
		otherwise:
		if (code == 0) {	// It is a SIP Request.  Switch based on the method.
			if (msg->isBYE()) {
				SipMTBye(msg);
			} else if (msg->isCANCEL()) {
				// This is an error since we have already passed the ACK stage, but lets cancel the dialog anyway.
				SipMTCancel(msg);
			} else {
				// Not expecting any others.  Must send 405 error.
				LOG(ALERT)<<"SIP Message ignored:"<<msg;	// TEMPORARY: Make this show up.
				LOG(WARNING)<<"SIP Message ignored:"<<msg;
				SipMessageReply oops(msg,405,string("Method Not Allowed"),this);
				sipWrite(&oops);
			}
		} else {
			// This should have matched a Transaction somewhere.
			// We cant send an error back for an unrecognized response or we get in an infinite loop.
			LOG(ALERT) << "SIP response not handled:"<<msg;
		}
		break;
	case SIPDTMOSMS:
	case SIPDTMTSMS:
		LOG(ERR) << "MESSAGE transaction received unexpected message:"<<msg;
		break;
	default:
		assert(0);
	}
	dialogChangeState(msg);
	delete msg;
}

// This is only called after the dialog has already been removed from the active dialogs,
// so we dont have to check the dialog state, all we have to check is there is nothing pointing to it
// that would cause a crash if genuinely deleted.
bool SipDialog::dgIsDeletable() const
{
	ScopedLock lock(mDialogLock,__FILE__,__LINE__);
	switch (mDialogType) {
		case SIPDTMOC:
		case SIPDTMTC:
		case SIPDTMOSMS:
		case SIPDTMTSMS:
		case SIPDTMOUssd:
			return gNewTransactionTable.ttIsDialogReleased(this->mTranId);
		// We never expire the dialog associated with REGISTER.
		case SIPDTRegister:
		case SIPDTUnregister:
		case SIPDTUndefined:
			return false;	// We never delete the Register dialog.
		default:
			assert(0);
	}
}

// Called periodicially to check for SIP timer expiration.
bool SipDialog::dialogPeriodicService()
{
	// Take care.  This is a potential deadlock if somone tries to add a locked SipDialog into the DialogMap,
	// because the kicker code locks the whole DialogMap against modification.
	ScopedLock lock(mDialogLock,__FILE__,__LINE__);
	// Now we use TransactionUsers for client transactions, so this code handles only server transactions.
	// The in-dialog server transactions are trivial - the transaction-layer simply resends the final
	// response each time the request is received.
	switch (mDialogType) {
		case SIPDTUndefined:
		case SIPDTRegister:
		case SIPDTUnregister:
			// FIXME: I dont think we delete these, ever.
			break;
		case SIPDTMTSMS:
		case SIPDTMTC:
			return mtPeriodicService();
			break;
		case SIPDTMOSMS:
		case SIPDTMOC:
		case SIPDTMOUssd:
			return moPeriodicService();
			break;
		//default: break;
	}
	return false;
}


const char *DialogState::msgStateString(DialogState::msgState dstate)
{
	switch (dstate) {
		case DialogState::dialogUndefined: return "undefined";
		case DialogState::dialogStarted: return "Started";
		case DialogState::dialogProceeding: return "Proceeding";
		case DialogState::dialogRinging: return "Ringing";
		case DialogState::dialogActive: return "Active";
		case DialogState::dialogBye: return "Bye";
		case DialogState::dialogFail: return "Fail";
		case DialogState::dialogDtmf: return "DTMF";
	};
	return "unknown_DialogState";
}

string SipDialog::dialogText(bool verbose) const
{
	std::ostringstream ss;
	ss << " SipDialog("<<LOGVARM(mTranId) ;
	ss << LOGVAR2("state",getDialogState()) <<LOGVARM(mPrevDialogState);
	//ss << LOGVAR2("fifo",mDownlinkFifo.size());
	sdbText(ss,verbose);
	// The C++ virtual inheritance is so broken we cant use it.  Gag me.
	switch (mDialogType) {
		case SIPDTMTC: case SIPDTMTSMS:
			ss << mttlText();
			break;
		case SIPDTMOC: case SIPDTMOSMS: case SIPDTMOUssd:
			ss << motlText();
			break;
		default: ss << "."; break;
	}
	ss <<")";
	return ss.str();
}

std::ostream& operator<<(std::ostream& os, const SipDialog*dg) {
	if (dg) os << dg->dialogText(); else os << "(null SipDialog)";
	return os;
}
std::ostream& operator<<(std::ostream& os, const SipDialog&dg) { os << dg.dialogText(); return os; }	// stupid language
std::ostream& operator<<(std::ostream& os, const SipDialogRef&dr) {
	SipDialog *dg = dr.self();
	if (dg) os << dg->dialogText(); else os << "(null SipDialog)";
	return os;
}

std::ostream& operator<<(std::ostream& os, const DialogState::msgState dstate)
{
	os << DialogState::msgStateString(dstate);
	return os;
}

std::ostream& operator<<(std::ostream& os, const DialogMessage*dmsg)
{
	if (dmsg) {
		os <<"DialogMessage("<<LOGVAR2("MsgState",DialogState::msgStateString(dmsg->mMsgState)) <<LOGVAR2("StatusCode",dmsg->mSipStatusCode)<<")";
	} else {
		os << "(null DialogMessage)";
	}
	return os;
}

std::ostream& operator<<(std::ostream& os, const DialogMessage&dmsg) { os << &dmsg; return os; }	// stupid language


};	// namespace
