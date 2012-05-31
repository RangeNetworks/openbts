/**@file SIP Call Control -- SIP IETF RFC-3261, RTP IETF RFC-3550. */
/*
* Copyright 2008, 2009, 2010, 2011 Free Software Foundation, Inc.
* Copyright 2011 Range Networks, Inc.
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/



#include <stdio.h>
#include <stdlib.h>
#include <iostream>

#include <sys/types.h>
#include <semaphore.h>

#include <ortp/telephonyevents.h>

#include <Logger.h>
#include <Timeval.h>
#include <GSMConfig.h>
#include <ControlCommon.h>
#include <GSMCommon.h>

#include "SIPInterface.h"
#include "SIPUtility.h"
#include "SIPMessage.h"
#include "SIPEngine.h"

#undef WARNING

using namespace std;
using namespace SIP;
using namespace Control;




const char* SIP::SIPStateString(SIPState s)
{
	switch(s)
	{	
		case NullState: return "Null";
		case Timeout: return "Timeout";
		case Starting: return "Starting";
		case Proceeding: return "Proceeding";
		case Ringing: return "Ringing";
		case Connecting: return "Connecting";
		case Active: return "Active";
		case Fail: return "Fail"; 
		case Busy: return "Busy";
		case MODClearing: return "MODClearing";
		case MODCanceling: return "MODCanceling";
		case MTDClearing: return "MTDClearing";
		case MTDCanceling: return "MTDCanceling";
		case Canceled: return "Canceled";
		case Cleared: return "Cleared";
		case MessageSubmit: return "SMS-Submit";
		default: return NULL;
	}
}

ostream& SIP::operator<<(ostream& os, SIPState s)
{
	const char* str = SIPStateString(s);
	if (str) os << str;
	else os << "?" << s << "?";
	return os;
}



SIPEngine::SIPEngine(const char* proxy, const char* IMSI)
	:mCSeq(random()%1000),
	mMyToFromHeader(NULL), mRemoteToFromHeader(NULL),
	mCallIDHeader(NULL),
	mSIPPort(gConfig.getNum("SIP.Local.Port")),
	mSIPIP(gConfig.getStr("SIP.Local.IP")),
	mINVITE(NULL), mLastResponse(NULL), mBYE(NULL),
	mCANCEL(NULL), mUNAVAIL(NULL), mSession(NULL), 
	mTxTime(0), mRxTime(0), mState(NullState),
	mDTMF('\0'),mDTMFDuration(0)
{
	assert(proxy);
	if (IMSI) user(IMSI);
	if (!resolveAddress(&mProxyAddr,proxy)) {
		LOG(ALERT) << "cannot resolve IP address for " << proxy;
		return;
	}
	char host[256];
	const char* ret = inet_ntop(AF_INET,&(mProxyAddr.sin_addr),host,255);
	if (!ret) {
		LOG(ALERT) << "cannot translate proxy IP address";
		return;
	}
	mProxyIP = string(host);
	mProxyPort = ntohs(mProxyAddr.sin_port);

	// generate a tag now
	char tmp[50];
	make_tag(tmp);
	mMyTag=tmp;	
	// set our CSeq in case we need one
	mCSeq = random()%600;

	//to make sure noise doesn't magically equal a valid RTP port
	mRTPPort = 0;
}


SIPEngine::~SIPEngine()
{
	if (mINVITE!=NULL) osip_message_free(mINVITE);
	if (mLastResponse!=NULL) osip_message_free(mLastResponse);
	if (mBYE!=NULL) osip_message_free(mBYE);
	if (mCANCEL!=NULL) osip_message_free(mCANCEL);
	if (mUNAVAIL!=NULL) osip_message_free(mUNAVAIL);
	// FIXME -- Do we need to dispose of the RtpSesion *mSesison?
}



void SIPEngine::saveINVITE(const osip_message_t *INVITE, bool mine)
{
	// Instead of cloning, why not just keep the old one?
	// Because that doesn't work in all calling contexts.
	// This simplifies the call-handling logic.
	if (mINVITE!=NULL) osip_message_free(mINVITE);
	osip_message_clone(INVITE,&mINVITE);

	// #238-private
	if (mINVITE==NULL){
		LOG(ALERT) << "Message cloning failed, skipping this message.";
		return;
	} 

	mCallIDHeader = mINVITE->call_id;

	// If this our own INVITE?  Did we initiate the transaciton?
	if (mine) {
		mMyToFromHeader = mINVITE->from;
		mRemoteToFromHeader = mINVITE->to;
		return;
	}

	// It's not our own.  The From: is the remote party.
	mMyToFromHeader = mINVITE->to;
	mRemoteToFromHeader = mINVITE->from;

	// We need to set our tag, too.
	osip_from_set_tag(mMyToFromHeader, strdup(mMyTag.c_str()));
}



void SIPEngine::saveResponse(osip_message_t *response)
{
	if (mLastResponse!=NULL) osip_message_free(mLastResponse);
	osip_message_clone(response,&mLastResponse);

	// The To: is the remote party and might have an new tag.
	mRemoteToFromHeader = mLastResponse->to;
}



void SIPEngine::saveBYE(const osip_message_t *BYE, bool mine)
{
	// Instead of cloning, why not just keep the old one?
	// Because that doesn't work in all calling contexts.
	// This simplifies the call-handling logic.
	if (mBYE!=NULL) osip_message_free(mBYE);
	osip_message_clone(BYE,&mBYE);
}

void SIPEngine::saveCANCEL(const osip_message_t *CANCEL, bool mine)
{
	// Instead of cloning, why not just keep the old one?
	// Because that doesn't work in all calling contexts.
	// This simplifies the call-handling logic.
	if (mCANCEL!=NULL) osip_message_free(mCANCEL);
	osip_message_clone(CANCEL,&mCANCEL);
}

void SIPEngine::saveUNAVAIL(const osip_message_t *UNAVAIL, bool mine)
{
	// Instead of cloning, why not just keep the old one?
	// Because that doesn't work in all calling contexts.
	// This simplifies the call-handling logic.
	if (mUNAVAIL!=NULL) osip_message_free(mUNAVAIL);
	osip_message_clone(UNAVAIL,&mUNAVAIL);
}

/* we're going to figure if the from field is us or not */
bool SIPEngine::instigator()
{
	assert(mINVITE);
	osip_uri_t * from_uri = mINVITE->from->url;
	return (!strncmp(from_uri->username,mSIPUsername.c_str(),15) &&
		!strncmp(from_uri->host, mSIPIP.c_str(), 30));
}

void SIPEngine::user( const char * IMSI )
{
	LOG(DEBUG) << "IMSI=" << IMSI;
	unsigned id = random();
	char tmp[20];
	sprintf(tmp, "%u", id);
	mCallID = tmp; 
	// IMSI gets prefixed with "IMSI" to form a SIP username
	mSIPUsername = string("IMSI") + IMSI;
}
	


void SIPEngine::user( const char * wCallID, const char * IMSI, const char *origID, const char *origHost) 
{
	LOG(DEBUG) << "IMSI=" << IMSI << " " << wCallID << " " << origID << "@" << origHost;  
	mSIPUsername = string("IMSI") + IMSI;
	mCallID = string(wCallID);
	mRemoteUsername = string(origID);
	mRemoteDomain = string(origHost);
}


bool SIPEngine::Register( Method wMethod )
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState << " " << wMethod << " callID " << mCallID;

	// Before start, need to add mCallID
	gSIPInterface.addCall(mCallID);

	// Initial configuration for sip message.
	// Make a new from tag and new branch.
	// make new mCSeq.
	
	// Generate SIP Message 
	// Either a register or unregister. Only difference 
	// is expiration period.
	osip_message_t * reg; 
	if (wMethod == SIPRegister ){
		reg = sip_register( mSIPUsername.c_str(), 
			60*gConfig.getNum("SIP.RegistrationPeriod"),
			mSIPPort, mSIPIP.c_str(), 
			mProxyIP.c_str(), mMyTag.c_str(), 
			mViaBranch.c_str(), mCallID.c_str(), mCSeq
		); 
	} else if (wMethod == SIPUnregister ) {
		reg = sip_register( mSIPUsername.c_str(), 
			0,
			mSIPPort, mSIPIP.c_str(), 
			mProxyIP.c_str(), mMyTag.c_str(), 
			mViaBranch.c_str(), mCallID.c_str(), mCSeq
		);
	} else { assert(0); }
 
	LOG(DEBUG) << "writing registration " << reg;
	gSIPInterface.write(&mProxyAddr,reg);	

	bool success = false;
	osip_message_t *msg = NULL;
	Timeval timeout(gConfig.getNum("SIP.Timer.F"));
	while (!timeout.passed()) {
		try {
			// SIPInterface::read will throw SIPTIimeout if it times out.
			// It should not return NULL.
			msg = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.E"));
		} catch (SIPTimeout) {
			// send again
			gSIPInterface.write(&mProxyAddr,reg);	
			continue;
		}

		assert(msg);
		int status = msg->status_code;
		LOG(INFO) << "received status " << msg->status_code << " " << msg->reason_phrase;
		// specific status
		if (status==200) {
			LOG(INFO) << "REGISTER success";
			success = true;
			break;
		}
		if (status==401) {
			LOG(INFO) << "REGISTER fail -- unauthorized";
			break;
		}
		if (status==404) {
			LOG(INFO) << "REGISTER fail -- not found";
			break;
		}
		if (status>=200) {
			LOG(NOTICE) << "REGISTER unexpected response " << status;
			break;
		}
	}

	if (!msg) {
		LOG(ALERT) << "SIP REGISTER timed out; is the registration server " << mProxyIP << ":" << mProxyPort << " OK?";
		throw SIPTimeout();
	}

	osip_message_free(reg);
	osip_message_free(msg);
	gSIPInterface.removeCall(mCallID);	
	return success;
}



const char* geoprivTemplate = 
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

SIPState SIPEngine::SOSSendINVITE(short wRtp_port, unsigned  wCodec)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	// Before start, need to add mCallID
	gSIPInterface.addCall(mCallID);
	
	// Set Invite params. 
	// new CSEQ and codec 
	char tmp[50];
	make_branch(tmp);
	mViaBranch = tmp;
	mCodec = wCodec;
	mCSeq++;

	mRemoteDomain = gConfig.getStr("Control.Emergency.Destination.Host");
	mRemoteUsername = gConfig.getStr("Control.Emergency.Destination.User");

	mRTPPort= wRtp_port;
	
	LOG(DEBUG) << "To: " << mRemoteUsername << "@" << mRemoteDomain;
	LOG(DEBUG) << "From: " << mSIPUsername << "@" << mSIPIP;

	osip_message_t *invite;
	if (gConfig.defines("Control.Emergency.RFC5031")) {
		invite = sip_invite5031(
			mRTPPort, mSIPUsername.c_str(), 
			mSIPPort, mSIPIP.c_str(), mProxyIP.c_str(),
			mMyTag.c_str(), mViaBranch.c_str(), mCallID.c_str(), mCSeq, mCodec); 
	} else {
		invite = sip_invite(
			mRemoteUsername.c_str(), mRTPPort, mSIPUsername.c_str(), 
			mSIPPort, mSIPIP.c_str(), mProxyIP.c_str(), 
			mMyTag.c_str(), mViaBranch.c_str(), mCallID.c_str(), mCSeq, mCodec); 
	}

	// Add IMS emergency call headers with osip_message_set_header.

	// P-Access-Network-Info
	// See 3GPP 24.229 7.2.
	char cgi_3gpp[50];
	sprintf(cgi_3gpp,"3GPP-GERAN; cgi-3gpp=%s%s%04x%04x",
		gConfig.getStr("GSM.Identity.MCC").c_str(),gConfig.getStr("GSM.Identity.MNC").c_str(),
		(unsigned)gConfig.getNum("GSM.Identity.LAC"),(unsigned)gConfig.getNum("GSM.Identity.CI"));
	osip_message_set_header(invite,"P-Access-Network-Info",cgi_3gpp);

	// P-Preferred-Identity
	// See RFC-3325.
	char pref_id[50];
	sprintf(pref_id,"<sip:%s@%s>",
		mSIPUsername.c_str(),
		gConfig.getStr("Control.Emergency.GatewaySwitch").c_str());
	osip_message_set_header(invite,"P-Preferred-Identity",pref_id);

	// FIXME -- Use the subscriber registry to look up the E.164
	// and make a second P-Preferred-Identity header.

	// Add RFC-4119 geolocation XML to content area, if available.
	if (gConfig.defines("Control.Emergency.Geolocation")) {
		char xml[strlen(geoprivTemplate) + 100];
		sprintf(xml,geoprivTemplate,
			mSIPUsername.c_str(), gConfig.getStr("Control.Emergency.GatewaySwitch").c_str(),
			gConfig.getStr("Control.Emergency.Geolocation").c_str());
		osip_message_set_content_type(invite, strdup("application/pidf+xml"));
		char tmp[20];
		sprintf(tmp,"%u",strlen(xml));
		osip_message_set_content_length(invite, strdup(tmp));
		osip_message_set_body(invite,xml,strlen(xml));
	}
	
	// Send Invite to Asterisk.
	gSIPInterface.write(&mProxyAddr,invite);
	saveINVITE(invite,true);
	osip_message_free(invite);
	mState = Starting;
	return mState;
};


SIPState SIPEngine::MOCSendINVITE( const char * wCalledUsername, 
	const char * wCalledDomain , short wRtp_port, unsigned  wCodec)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	// Before start, need to add mCallID
	gSIPInterface.addCall(mCallID);
	
	// Set Invite params. 
	// new CSEQ and codec 
	char tmp[50];
	make_branch(tmp);
	mViaBranch = tmp;
	mCodec = wCodec;
	mCSeq++;

	mRemoteUsername = wCalledUsername;
	mRemoteDomain = wCalledDomain;
	mRTPPort= wRtp_port;
	
	LOG(DEBUG) << "mRemoteUsername=" << mRemoteUsername;
	LOG(DEBUG) << "mSIPUsername=" << mSIPUsername;

	osip_message_t * invite = sip_invite(
		mRemoteUsername.c_str(), mRTPPort, mSIPUsername.c_str(), 
		mSIPPort, mSIPIP.c_str(), mProxyIP.c_str(), 
		mMyTag.c_str(), mViaBranch.c_str(), mCallID.c_str(), mCSeq, mCodec); 

	// P-Access-Network-Info
	// See 3GPP 24.229 7.2.
	char cgi_3gpp[50];
	sprintf(cgi_3gpp,"3GPP-GERAN; cgi-3gpp=%s%s%04x%04x",
		gConfig.getStr("GSM.Identity.MCC").c_str(),gConfig.getStr("GSM.Identity.MNC").c_str(),
		(unsigned)gConfig.getNum("GSM.Identity.LAC"),(unsigned)gConfig.getNum("GSM.Identity.CI"));
	osip_message_set_header(invite,"P-Access-Network-Info",cgi_3gpp);

	
	// Send Invite.
	gSIPInterface.write(&mProxyAddr,invite);
	saveINVITE(invite,true);
	osip_message_free(invite);
	mState = Starting;
	return mState;
};


SIPState SIPEngine::MOCResendINVITE()
{
	assert(mINVITE);
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	gSIPInterface.write(&mProxyAddr,mINVITE);
	return mState;
}

SIPState  SIPEngine::MOCWaitForOK()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;

	osip_message_t * msg;

	// Read off the fifo. if time out will
	// clean up and return false.
	try {
		msg = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.A"));
	}
	catch (SIPTimeout& e) { 
		LOG(DEBUG) << "timeout";
		mState = Timeout;
		return mState;
	}

	int status = msg->status_code;
	LOG(DEBUG) << "received status " << status;
	saveResponse(msg);
	switch (status) {
		case 100:	// Trying - this maybe should go to ringing too -kurtis
			mState = Proceeding;
			break;
		case 180:	// Ringing
		case 183:	// Progress - 
			//We keep sending invited until we 
			//enter Ringing, so 183 need to do that -kurtis
			mState = Ringing;
			break;
		case 200:	// OK
			// Save the response and update the state,
			// but the ACK doesn't happen until the call connects.
			mState = Active;
			break;
		// Anything 400 or above terminates the call, so we ACK.
		// FIXME -- It would be nice to save more information about the
		// specific failure cause.
		case 486:
		case 503:
			mState = Busy;
			MOCSendACK();
			break;
		default:
			LOG(NOTICE) << "unhandled status code " << status;
			mState = Fail;
			MOCSendACK();
	}
	osip_message_free(msg);
	LOG(DEBUG) << " new state: " << mState;
	return mState;
}


SIPState SIPEngine::MOCSendACK()
{
	assert(mLastResponse);

	LOG(INFO) << "user " << mSIPUsername << " state " << mState;

	osip_message_t* ack = sip_ack( mRemoteDomain.c_str(),
		mRemoteUsername.c_str(), 
		mSIPUsername.c_str(),
		mSIPPort, mSIPIP.c_str(), mProxyIP.c_str(), 
		mMyToFromHeader, mRemoteToFromHeader,
		mViaBranch.c_str(), mCallIDHeader, mCSeq
	);

	gSIPInterface.write(&mProxyAddr,ack);
	osip_message_free(ack);	

	return mState;
}


SIPState SIPEngine::MODSendBYE()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	assert(mINVITE);
	char tmp[50];
	make_branch(tmp);
	mViaBranch = tmp;
	mCSeq++;

	osip_message_t * bye = sip_bye(mRemoteDomain.c_str(), mRemoteUsername.c_str(), 
		mSIPUsername.c_str(),
		mSIPPort, mSIPIP.c_str(), mProxyIP.c_str(), mProxyPort,
		mMyToFromHeader, mRemoteToFromHeader,
		mViaBranch.c_str(), mCallIDHeader, mCSeq );

	gSIPInterface.write(&mProxyAddr,bye);
	saveBYE(bye,true);
	osip_message_free(bye);
	mState = MODClearing;
	return mState;
}

SIPState SIPEngine::MODSendUNAVAIL()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	assert(mINVITE);

	osip_message_t * unavail = sip_temporarily_unavailable(mINVITE, mSIPIP.c_str(),
							       mSIPUsername.c_str(), mSIPPort);
	gSIPInterface.write(&mProxyAddr,unavail);
	saveUNAVAIL(unavail, true);
	osip_message_free(unavail);
	mState = MODCanceling;
	return mState;
}

SIPState SIPEngine::MODSendCANCEL()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	assert(mINVITE);

	osip_message_t * cancel = sip_cancel(mINVITE, mSIPIP.c_str(),
					     mSIPUsername.c_str(), mSIPPort);
	gSIPInterface.write(&mProxyAddr,cancel);
	saveCANCEL(cancel, true);
	osip_message_free(cancel);
	mState = MODCanceling;
	return mState;
}
	

SIPState SIPEngine::MODResendBYE()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	assert(mState==MODClearing);
	assert(mBYE);
	gSIPInterface.write(&mProxyAddr,mBYE);
	return mState;
}

SIPState SIPEngine::MODResendCANCEL()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	assert(mState==MODCanceling);
	assert(mCANCEL);
	gSIPInterface.write(&mProxyAddr,mCANCEL);
	return mState;
}

SIPState SIPEngine::MODResendUNAVAIL()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	assert(mState==MODCanceling);
	assert(mUNAVAIL);
	gSIPInterface.write(&mProxyAddr,mUNAVAIL);
	return mState;
}

/* there shouldn't be any more communications on this fifo, but we might
   get a 487 RequestTerminated. We only need to respond and move on -kurtis */
SIPState SIPEngine::MODWaitFor487()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	osip_message_t * msg;
	try {
		msg = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.E"));
	}
	catch (SIPTimeout& e) {
		LOG(NOTICE) << "487 Timeout";
		return mState;
	}
	//ok, message arrived
	if (msg->status_code != 487){
		LOG(WARNING) << "unexpected " << msg->status_code << 
		  " response to CANCEL, from proxy " << mProxyIP << ":" << mProxyPort;
		return mState;
	} else {
		osip_message_t* ack = sip_ack( mRemoteDomain.c_str(),
					       mRemoteUsername.c_str(), 
					       mSIPUsername.c_str(),
					       mSIPPort, mSIPIP.c_str(), mProxyIP.c_str(), 
					       mMyToFromHeader, mRemoteToFromHeader,
					       mViaBranch.c_str(), mCallIDHeader, mCSeq
	);
		gSIPInterface.write(&mProxyAddr,ack);
		osip_message_free(ack);
		return mState;
	}
}

SIPState SIPEngine::MODWaitForBYEOK()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	bool responded = false;
	Timeval timeout(gConfig.getNum("SIP.Timer.F")); 
	while (!timeout.passed()) {
		try {
			osip_message_t * ok = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.E"));
			responded = true;
			unsigned code = ok->status_code;
			saveResponse(ok);
			osip_message_free(ok);
			if (code!=200) {
				LOG(WARNING) << "unexpected " << code << " response to BYE, from proxy " << mProxyIP << ":" << mProxyPort << ". Assuming other end has cleared";
			}
			break;
		}
		catch (SIPTimeout& e) {
			LOG(NOTICE) << "response timeout, resending BYE";
			MODResendBYE();
		}
	}

	if (!responded) { LOG(ALERT) << "lost contact with proxy " << mProxyIP << ":" << mProxyPort; }

	mState = Cleared;

	return mState;
}

SIPState SIPEngine::MODWaitForCANCELOK()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	bool responded = false;
	Timeval timeout(gConfig.getNum("SIP.Timer.F")); 
	while (!timeout.passed()) {
		try {
			osip_message_t * ok = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.E"));
			responded = true;
			unsigned code = ok->status_code;
			saveResponse(ok);
			osip_message_free(ok);
			if (code!=200) {
				LOG(WARNING) << "unexpected " << code << " response to CANCEL, from proxy " << mProxyIP << ":" << mProxyPort << ". Assuming other end has cleared";
			}
			break;
		}
		catch (SIPTimeout& e) {
			LOG(NOTICE) << "response timeout, resending CANCEL";
			MODResendCANCEL();
		}
	}

	if (!responded) { LOG(ALERT) << "lost contact with proxy " << mProxyIP << ":" << mProxyPort; }

	mState = Canceled;

	return mState;
}

SIPState SIPEngine::MODWaitForUNAVAILACK()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	bool responded = false;
	Timeval timeout(gConfig.getNum("SIP.Timer.F")); 
	while (!timeout.passed()) {
		try {
			osip_message_t * ack = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.E"));
			responded = true;
			saveResponse(ack);
			if ((NULL == ack->sip_method) || !strncmp(ack->sip_method,"ACK", 4)) {
				LOG(WARNING) << "unexpected response to UNAVAIL, from proxy " << mProxyIP << ":" << mProxyPort << ". Assuming other end has cleared";
			}
			osip_message_free(ack);
			break;
		}
		catch (SIPTimeout& e) {
			LOG(NOTICE) << "response timeout, resending UNAVAIL";
			MODResendUNAVAIL();
		}
	}

	if (!responded) { LOG(ALERT) << "lost contact with proxy " << mProxyIP << ":" << mProxyPort; }

	mState = Canceled;

	return mState;
}

SIPState SIPEngine::MTDCheckBYE()
{
	//LOG(DEBUG) << "user " << mSIPUsername << " state " << mState;
	// If the call is not active, there should be nothing to check.
	if (mState!=Active) return mState;

	// Need to check size of osip_message_t* fifo,
	// so need to get fifo pointer and get size.
	// HACK -- reach deep inside to get damn thing
	int fifoSize = gSIPInterface.fifoSize(mCallID);


	// Size of -1 means the FIFO does not exist.
	// Treat the call as cleared.
	if (fifoSize==-1) {
		LOG(NOTICE) << "MTDCheckBYE attempt to check BYE on non-existant SIP FIFO";
		mState=Cleared;
		return mState;
	}

	// If no messages, there is no change in state.
	if (fifoSize==0) return mState;	
		
	osip_message_t * msg = gSIPInterface.read(mCallID);
	

	if (msg->sip_method) {
		if (strcmp(msg->sip_method,"BYE")==0) { 
			LOG(DEBUG) << "found msg="<<msg->sip_method;	
			saveBYE(msg,false);
			mState =  MTDClearing;
		}
		//repeated ACK, send OK
		//pretty sure this never happens, but someone else left a fixme before... -kurtis
		if (strcmp(msg->sip_method,"ACK")==0) { 	
			LOG(DEBUG) << "Not responding to repeated ACK. FIXME";
		}
	}

	//repeated OK, send ack
	//MOC because that's the only time we ACK
	if (msg->status_code==200){
		LOG(DEBUG) << "Repeated OK, resending ACK";
		MOCSendACK();
	}

	osip_message_free(msg);
	return mState;
} 


SIPState SIPEngine::MTDSendBYEOK()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	assert(mBYE);
	osip_message_t * okay = sip_b_okay(mBYE);
	gSIPInterface.write(&mProxyAddr,okay);
	osip_message_free(okay);
	mState = Cleared;
	return mState;
}

SIPState SIPEngine::MTDSendCANCELOK()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	assert(mCANCEL);
	osip_message_t * okay = sip_b_okay(mCANCEL);
	gSIPInterface.write(&mProxyAddr,okay);
	osip_message_free(okay);
	mState = Canceled;
	return mState;
}


SIPState SIPEngine::MTCSendTrying()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	if (mINVITE==NULL) mState=Fail;
	if (mState==Fail) return mState;
	osip_message_t * trying = sip_trying(mINVITE, mSIPUsername.c_str(), mProxyIP.c_str());
	gSIPInterface.write(&mProxyAddr,trying);
	osip_message_free(trying);
	mState=Proceeding;
	return mState;
}


SIPState SIPEngine::MTCSendRinging()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	assert(mINVITE);

	LOG(DEBUG) << "send ringing";
	osip_message_t * ringing = sip_ringing(mINVITE, 
		mSIPUsername.c_str(), mProxyIP.c_str());
	gSIPInterface.write(&mProxyAddr,ringing);
	osip_message_free(ringing);

	mState = Proceeding;
	return mState;
}



SIPState SIPEngine::MTCSendOK( short wRTPPort, unsigned wCodec )
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	assert(mINVITE);
	mRTPPort = wRTPPort;
	mCodec = wCodec;
	LOG(DEBUG) << "port=" << wRTPPort << " codec=" << mCodec;
	// Form ack from invite and new parameters.
	osip_message_t * okay = sip_okay_sdp(mINVITE, mSIPUsername.c_str(),
		mSIPIP.c_str(), mSIPPort, mRTPPort, mCodec);
	gSIPInterface.write(&mProxyAddr,okay);
	osip_message_free(okay);
	mState=Connecting;
	return mState;
}

SIPState SIPEngine::MTCWaitForACK()
{
	// wait for ack,set this to timeout of 
	// of call channel.  If want a longer timeout 
	// period, need to split into 2 handle situation 
	// like MOC where this fxn if called multiple times. 
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	osip_message_t * ack;

	// FIXME -- This is supposed to retransmit BYE on timer I.
	try {
		ack = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.H"));
	}
	catch (SIPTimeout& e) {
		LOG(NOTICE) << "timeout";
		mState = Timeout;	
		return mState;
	}
	catch (SIPError& e) {
		LOG(NOTICE) << "read error";
		mState = Fail;
		return mState;
	}

	if (ack->sip_method==NULL) {
		LOG(NOTICE) << "SIP message with no method, status " <<  ack->status_code;
		mState = Fail;
		osip_message_free(ack);
		return mState;	
	}

	LOG(INFO) << "received sip_method="<<ack->sip_method;

	// check for duplicated INVITE
	if( strcmp(ack->sip_method,"INVITE") == 0){ 
		LOG(NOTICE) << "received duplicate INVITE";
	}
	// check for the ACK
	else if( strcmp(ack->sip_method,"ACK") == 0){ 
		LOG(INFO) << "received ACK";
		mState=Active;
	}
	// check for the CANCEL
	else if( strcmp(ack->sip_method,"CANCEL") == 0){ 
		LOG(INFO) << "received CANCEL";
		saveCANCEL(ack, false);
		mState=MTDCanceling;
	}
	// check for strays
	else {
		LOG(NOTICE) << "unexpected Message "<<ack->sip_method; 
		mState = Fail;
	}

	osip_message_free(ack);
	return mState;	
}


SIPState SIPEngine::MTCCheckForCancel()
{
	// wait for ack,set this to timeout of 
	// of call channel.  If want a longer timeout 
	// period, need to split into 2 handle situation 
	// like MOC where this fxn if called multiple times. 
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	osip_message_t * msg;

	try {
		//block for very small amount of time
		msg = gSIPInterface.read(mCallID,1);
	}
	catch (SIPTimeout& e) {
		return mState;
	}
	catch (SIPError& e) {
		LOG(NOTICE) << "read error";
		mState = Fail;
		return mState;
	}

	if (msg->sip_method==NULL) {
		LOG(NOTICE) << "SIP message with no method, status " <<  msg->status_code;
		mState = Fail;
		osip_message_free(msg);
		return mState;	
	}

	LOG(INFO) << "received sip_method=" << msg->sip_method;

	// check for duplicated INVITE
	if (strcmp(msg->sip_method,"INVITE")==0) { 
		LOG(NOTICE) << "received duplicate INVITE";
	}
	// check for the CANCEL
	else if (strcmp(msg->sip_method,"CANCEL")==0) { 
		LOG(INFO) << "received CANCEL";
		saveCANCEL(msg, false);
		mState=MTDCanceling;
	}
	// check for strays
	else {
		LOG(NOTICE) << "unexpected Message " << msg->sip_method; 
		mState = Fail;
	}

	osip_message_free(msg);
	return mState;
}


void SIPEngine::InitRTP(const osip_message_t * msg )
{
	if(mSession == NULL)
		mSession = rtp_session_new(RTP_SESSION_SENDRECV);

	bool rfc2833 = gConfig.defines("SIP.DTMF.RFC2833");
	if (rfc2833) {
		RtpProfile* profile = rtp_session_get_send_profile(mSession);
		int index = gConfig.getNum("SIP.DTMF.RFC2833.PayloadType");
		rtp_profile_set_payload(profile,index,&payload_type_telephone_event);
		// Do we really need this next line?
		rtp_session_set_send_profile(mSession,profile);
	}

	rtp_session_set_blocking_mode(mSession, TRUE);
	rtp_session_set_scheduling_mode(mSession, TRUE);
	rtp_session_set_connected_mode(mSession, TRUE);
	rtp_session_set_symmetric_rtp(mSession, TRUE);
	// Hardcode RTP session type to GSM full rate (GSM 06.10).
	// FIXME -- Make this work for multiple vocoder types.
	rtp_session_set_payload_type(mSession, 3);

	char d_ip_addr[20];
	char d_port[10];
	get_rtp_params(msg, d_port, d_ip_addr);
	LOG(DEBUG) << "IP="<<d_ip_addr<<" "<<d_port<<" "<<mRTPPort;

	rtp_session_set_local_addr(mSession, "0.0.0.0", mRTPPort );
	rtp_session_set_remote_addr(mSession, d_ip_addr, atoi(d_port));

	// Check for event support.
	int code = rtp_session_telephone_events_supported(mSession);
	if (code == -1) {
		if (rfc2833) { LOG(ALERT) << "RTP session does not support selected DTMF method RFC-2833"; }
		else { LOG(WARNING) << "RTP session does not support telephone events"; }
	}

}


void SIPEngine::MTCInitRTP()
{
	assert(mINVITE);
	InitRTP(mINVITE);
}


void SIPEngine::MOCInitRTP()
{
	assert(mLastResponse);
	InitRTP(mLastResponse);
}




bool SIPEngine::startDTMF(char key)
{
	LOG (DEBUG) << key;
	if (mState!=Active) return false;
	mDTMF = key;
	mDTMFDuration = 0;
	mDTMFStartTime = mTxTime;
	int code = rtp_session_send_dtmf2(mSession,mDTMF,mDTMFStartTime,mDTMFDuration);
	mDTMFDuration += 160;
	if (!code) return true;
	// Error?  Turn off DTMF sending.
	LOG(WARNING) << "DTMF RFC-2833 failed on start.";
	mDTMF = '\0';
	return false;
}

void SIPEngine::stopDTMF()
{
	mDTMF='\0';
}


void SIPEngine::txFrame(unsigned char* frame )
{
	if(mState!=Active) return;

	// HACK -- Hardcoded for GSM/8000.
	// FIXME -- Make this work for multiple vocoder types.
	rtp_session_send_with_ts(mSession, frame, 33, mTxTime);
	mTxTime += 160;		

	if (mDTMF) {
		// Any RFC-2833 action?
		int code = rtp_session_send_dtmf2(mSession,mDTMF,mDTMFStartTime,mDTMFDuration);
		mDTMFDuration += 160;
		LOG (DEBUG) << "DTMF RFC-2833 sending " << mDTMF << " " << mDTMFDuration;
		// Turn it off if there's an error.
		if (code) {
			LOG(ERR) << "DTMF RFC-2833 failed after start.";
			mDTMF='\0';
		}
	}
}


int SIPEngine::rxFrame(unsigned char* frame)
{
	if(mState!=Active) return 0; 

	int more;
	int ret=0;
	// HACK -- Hardcoded for GSM/8000.
	// FIXME -- Make this work for multiple vocoder types.
	ret = rtp_session_recv_with_ts(mSession, frame, 33, mRxTime, &more);
	mRxTime += 160;
	return ret;
}




SIPState SIPEngine::MOSMSSendMESSAGE(const char * wCalledUsername, 
	const char * wCalledDomain , const char *messageText, const char *contentType)
{
	LOG(DEBUG) << "mState=" << mState;
	LOG(INFO) << "SIP send to " << wCalledUsername << "@" << wCalledDomain << " MESSAGE " << messageText;
	// Before start, need to add mCallID
	gSIPInterface.addCall(mCallID);
	
	// Set MESSAGE params. 
	char tmp[50];
	make_branch(tmp);
	mViaBranch = tmp;
	mCSeq++;

	mRemoteUsername = wCalledUsername;
	mRemoteDomain = wCalledDomain;

	osip_message_t * message = sip_message(
		mRemoteUsername.c_str(), mSIPUsername.c_str(), 
		mSIPPort, mSIPIP.c_str(), mProxyIP.c_str(), 
		mMyTag.c_str(), mViaBranch.c_str(), mCallID.c_str(), mCSeq,
		messageText, contentType); 
	
	// Send Invite to the SIP proxy.
	gSIPInterface.write(&mProxyAddr,message);
	saveINVITE(message,true);
	osip_message_free(message);
	mState = MessageSubmit;
	return mState;
};


SIPState SIPEngine::MOSMSWaitForSubmit()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;

	try {
		osip_message_t * ok = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.A"));
		// That should never return NULL.
		assert(ok);
		if((ok->status_code==200) || (ok->status_code==202) ) {
			mState = Cleared;
			LOG(INFO) << "successful";
		}
		osip_message_free(ok);
	}

	catch (SIPTimeout& e) {
		LOG(ALERT) << "SIP MESSAGE timed out; is the SMS server " << mProxyIP << ":" << mProxyPort << " OK?"; 
		mState = Fail;
	}

	return mState;

}



SIPState SIPEngine::MTSMSSendOK()
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	// If this operation was initiated from the CLI, there was no INVITE.
	if (!mINVITE) {
		LOG(INFO) << "clearing CLI-generated transaction";
		mState=Cleared;
		return mState;
	}
	// Form ack from invite and new parameters.
	osip_message_t * okay = sip_okay(mINVITE, mSIPUsername.c_str(),
		mSIPIP.c_str(), mSIPPort);
	gSIPInterface.write(&mProxyAddr,okay);
	osip_message_free(okay);
	mState=Cleared;
	return mState;
}



bool SIPEngine::sendINFOAndWaitForOK(unsigned wInfo)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;

	char tmp[50];
	make_branch(tmp);
	mViaBranch = tmp;
	mCSeq++;
	osip_message_t * info = sip_info( wInfo,
		mRemoteUsername.c_str(), mRTPPort, mSIPUsername.c_str(), 
		mSIPPort, mSIPIP.c_str(), mProxyIP.c_str(), 
		mMyTag.c_str(), mViaBranch.c_str(), mCallIDHeader, mCSeq); 
	gSIPInterface.write(&mProxyAddr,info);
	osip_message_free(info);

	try {
		// This will timeout on failure.  It will not return NULL.
		osip_message_t *msg = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.A"));
		LOG(DEBUG) << "received status " << msg->status_code << " " << msg->reason_phrase;
		bool retVal = (msg->status_code==200);
		osip_message_free(msg);
		if (!retVal) LOG(CRIT) << "DTMF RFC-2967 failed.";
		return retVal;
	}
	catch (SIPTimeout& e) { 
		LOG(NOTICE) << "timeout";
		return false;
	}

};




// vim: ts=4 sw=4
