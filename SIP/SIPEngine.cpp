/**@file SIP Call Control -- SIP IETF RFC-3261, RTP IETF RFC-3550. */
/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2011, 2012 Range Networks, Inc.
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
#include <GSMLogicalChannel.h>
#include <Reporting.h>
#include <Globals.h>

#include "SIPInterface.h"
#include "SIPUtility.h"
#include "SIPMessage.h"
#include "SIPEngine.h"
#include "TransactionTable.h"

#undef WARNING

using namespace std;
using namespace SIP;
using namespace Control;

int get_rtp_tev_type(char dtmf){
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
		case HandoverInbound: return "HandoverInbound";
		case HandoverInboundReferred: return "HandoverInboundReferred";
		case HandoverOutbound: return "HandoverOutbound";
		default: return NULL;
	}
}

ostream& SIP::operator<<(ostream& os, SIPState s)
{
	const char* str = SIPStateString(s);
	if (str) os << str;
	else os << "?" << ((int)s) << "?";
	return os;
}



SIPEngine::SIPEngine(const char* proxy, const char* IMSI)
	:mCSeq(random()%1000),
	mMyToFromHeader(NULL), mRemoteToFromHeader(NULL),
	mCallIDHeader(NULL),
	mSIPPort(gConfig.getNum("SIP.Local.Port")),
	mSIPIP(gConfig.getStr("SIP.Local.IP")),
	mINVITE(NULL), mLastResponse(NULL), mBYE(NULL),
	mCANCEL(NULL), mERROR(NULL), mSession(NULL), 
	mTxTime(0), mRxTime(0), mState(NullState), mInstigator(false),
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
	if (mERROR!=NULL) osip_message_free(mERROR);
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

void SIPEngine::saveERROR(const osip_message_t *ERROR, bool mine)
{
	// Instead of cloning, why not just keep the old one?
	// Because that doesn't work in all calling contexts.
	// This simplifies the call-handling logic.
	if (mERROR!=NULL) osip_message_free(mERROR);
	osip_message_clone(ERROR,&mERROR);
}

#if 0
This was replaced with a simple flag set during MO transactions.
/* we're going to figure if the from field is us or not */
bool SIPEngine::instigator()
{
	assert(mINVITE);
	osip_uri_t * from_uri = mINVITE->from->url;
	return (!strncmp(from_uri->username,mSIPUsername.c_str(),15) &&
		!strncmp(from_uri->host, mSIPIP.c_str(), 30));
}
#endif

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

string randy401(osip_message_t *msg)
{
	if (msg->status_code != 401) return "";
	osip_www_authenticate_t *auth = (osip_www_authenticate_t*)osip_list_get(&msg->www_authenticates, 0);
	if (auth == NULL) return "";
	char *rand = osip_www_authenticate_get_nonce(auth);
	string rands = rand ? string(rand) : "";
	if (rands.length()!=32) {
		LOG(WARNING) << "SIP RAND wrong length: " << rands;
		return "";
	}
	return rands;
}


void SIPEngine::writePrivateHeaders(osip_message_t *msg, const GSM::LogicalChannel *chan)
{
	// P-PHY-Info
	// This is a non-standard private header in OpenBTS.
	// TA=<timing advance> TE=<TA error> UpRSSI=<uplink RSSI> TxPwr=<MS tx power>
	// DnRSSIdBm=<downlink RSSI> time=<system time of measurements>
	// Get the values.
	if (chan) {
		LOG(DEBUG);
		char phy_info[400];
		sprintf(phy_info,"OpenBTS; TA=%d TE=%f UpRSSI=%f TxPwr=%d DnRSSIdBm=%d time=%9.3lf",
			chan->actualMSTiming(), chan->timingError(),
			chan->RSSI(), chan->actualMSPower(),
			chan->measurementResults().RXLEV_FULL_SERVING_CELL_dBm(),
			chan->timestamp());
		LOG(DEBUG) << "PHY-info: " << phy_info;
		osip_message_set_header(msg,"P-PHY-Info",phy_info);
	}

	// P-Access-Network-Info
	// See 3GPP 24.229 7.2.
	char cgi_3gpp[256];
	sprintf(cgi_3gpp,"3GPP-GERAN; cgi-3gpp=%s%s%04x%04x",
		gConfig.getStr("GSM.Identity.MCC").c_str(),gConfig.getStr("GSM.Identity.MNC").c_str(),
		(unsigned)gConfig.getNum("GSM.Identity.LAC"),(unsigned)gConfig.getNum("GSM.Identity.CI"));
	osip_message_set_header(msg,"P-Access-Network-Info",cgi_3gpp);
 
	// P-Preferred-Identity
	// See RFC-3325.
	char pref_id[350];
	sprintf(pref_id,"<sip:%s@%s>",
		mSIPUsername.c_str(),
		gConfig.getStr("SIP.Proxy.Speech").c_str());
	osip_message_set_header(msg,"P-Preferred-Identity",pref_id);
	// Check for illegal hostname length. 253 bytes for domain name + 6 bytes for port and colon.
	// This isn't "pretty", but it should be fast, and gives us a ballpark. Their hostname will
	// fail elsewhere if it is longer than 253 bytes (since this assumes a 5 byte port string).
	if (gConfig.getStr("SIP.Proxy.Speech").length() > 259) {
		LOG(ALERT) << "Configured SIP.Proxy.Speech hostname is great than 253 bytes!";
	}

	// FIXME -- Use the subscriber registry to look up the E.164
	// and make a second P-Preferred-Identity header.

}


bool SIPEngine::Register( Method wMethod , const GSM::LogicalChannel* chan, string *RAND, const char *IMSI, const char *SRES)
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
			mViaBranch.c_str(), mCallID.c_str(), mCSeq,
			RAND, IMSI, SRES
		); 
	} else if (wMethod == SIPUnregister ) {
		reg = sip_register( mSIPUsername.c_str(), 
			0,
			mSIPPort, mSIPIP.c_str(), 
			mProxyIP.c_str(), mMyTag.c_str(), 
			mViaBranch.c_str(), mCallID.c_str(), mCSeq,
			NULL, NULL, NULL
		);
	} else { assert(0); }

	writePrivateHeaders(reg,chan);
	gReports.incr("OpenBTS.SIP.REGISTER.Out");
 
	LOG(DEBUG) << "writing registration " << reg;
	gSIPInterface.write(&mProxyAddr,reg);	

	bool success = false;
	osip_message_t *msg = NULL;
	Timeval timeout(gConfig.getNum("SIP.Timer.F"));
	while (!timeout.passed()) {
		try {
			// SIPInterface::read will throw SIPTIimeout if it times out.
			// It should not return NULL.
			msg = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.E"),NULL);
		} catch (SIPTimeout) {
			// send again
			LOG(NOTICE) << "SIP REGISTER packet to " << mProxyIP << ":" << mProxyPort << " timeout; resending"; 
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
			string wRAND = randy401(msg);
			// if rand is included on 401 unauthorized, then the challenge-response game is afoot
			if (wRAND.length() != 0 && RAND != NULL) {
				LOG(INFO) << "REGISTER challenge RAND=" << wRAND;
				*RAND = wRAND;
				osip_message_free(msg);
				osip_message_free(reg);
				return false;
			} else {
				LOG(INFO) << "REGISTER fail -- unauthorized";
				break;
			}
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
	// We remove the call FIFO here because there
	// is no transaction entry associated with the REGISTER.
	gSIPInterface.removeCall(mCallID);	
	return success;
}


float geodecode1(const char **p, int *err, bool colonExpected)
{
	float n = 0;
	const char *q = *p;
	while (**p >= '0' && **p <= '9') {
		n = n * 10 + **p - '0';
		(*p)++;
	}
	if (q == *p) *err = 1;
	if (colonExpected) {
		if (**p == ':') {
			(*p)++;
		} else {
			*err = 1;
		}
	}
	return n;
}


float geodecode(const char **p, int *err)
{
	float n = 0;
	float m = 1;
	while (**p == ' ') {
		(*p)++;
	}
	if (**p == '-') {
		m = -1;
		(*p)++;
	}
	n = geodecode1(p, err, true);
	n += geodecode1(p, err, true)/60.0;
	n += geodecode1(p, err, false)/3600.0;
	if (**p == ' ' || **p == 0) return n * m;
	switch (**p) {
		case 'N':
		case 'E':
			(*p)++;
			return n * m;
		case 'S':
		case 'W':
			(*p)++;
			return n * m * -1.0;
	}
	*err = 1;
	return 0;
}

SIPState SIPEngine::MOCSendINVITE( const char * wCalledUsername, 
	const char * wCalledDomain , short wRtp_port, unsigned  wCodec,
	const GSM::LogicalChannel *chan)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	// Before start, need to add mCallID
	gSIPInterface.addCall(mCallID);
	mInstigator = true;
	gReports.incr("OpenBTS.SIP.INVITE.Out");
	
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

	writePrivateHeaders(invite,chan);

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
	LOG(NOTICE) << "SIP INVITE packet to " << mProxyIP << ":" << mProxyPort << " timedout; resending"; 
	gSIPInterface.write(&mProxyAddr,mINVITE);
	return mState;
}

SIPState  SIPEngine::MOCCheckForOK(Mutex *lock)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	//if (mState==Fail) return Fail;

	osip_message_t * msg;
	//osip_message_t * msg = NULL;

	// Read off the fifo. if time out will
	// clean up and return false.
	try {
		msg = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.A"),lock);
	}
	catch (SIPTimeout& e) { 
		LOG(DEBUG) << "timeout";
		//if we got a 100 TRYING (SIP::Proceeding)
		//don't time out
		if (mState != SIP::Proceeding){
			mState = Timeout;
		}
		return mState;
	}

	int status = msg->status_code;
	LOG(DEBUG) << "received status " << status;
	saveResponse(msg);
	switch (status) {
		// class 1XX: Provisional messages
		case 100:	// Trying
		case 181:	// Call Is Being Forwarded
		case 182:	// Queued
		case 183:	// Session Progress FIXME we need to setup the sound channel (early media)
			mState = Proceeding;
			break;
		case 180:	// Ringing
			mState = Ringing;
			break;

		// calss 2XX: Success
		case 200:	// OK
				// Save the response and update the state,
				// but the ACK doesn't happen until the call connects.
			mState = Active;
			break;

		// class 3xx: Redirection
		case 300:	// Multiple Choices
		case 301:	// Moved Permanently
		case 302:	// Moved Temporarily
		case 305:	// Use Proxy
		case 380:	// Alternative Service
			LOG(NOTICE) << "redirection not supported code " << status;
			mState = Fail;
			gReports.incr("OpenBTS.SIP.Failed.Remote.3xx");
			MOCSendACK();
			break;
		// Anything 400 or above terminates the call, so we ACK.
		// FIXME -- It would be nice to save more information about the
		// specific failure cause.

		// class 4XX: Request failures
		case 400:	// Bad Request
		case 401:	// Unauthorized: Used only by registrars. Proxys should use proxy authorization 407
		case 402:	// Payment Required (Reserved for future use)
		case 403:	// Forbidden
		case 404:	// Not Found: User not found
		case 405:	// Method Not Allowed
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
			LOG(NOTICE) << "request failure code " << status;
			mState = Fail;
			gReports.incr("OpenBTS.SIP.Failed.Remote.4xx");
			MOCSendACK();
			break;

		case 486:	// Busy Here
			LOG(NOTICE) << "remote end busy code " << status;
			mState = Busy;
			MOCSendACK();
			break;
		case 487:	// Request Terminated
		case 488:	// Not Acceptable Here
		case 491:	// Request Pending
		case 493:	// Undecipherable: Could not decrypt S/MIME body part
			LOG(NOTICE) << "request failure code " << status;
			mState = Fail;
			gReports.incr("OpenBTS.SIP.Failed.Remote.4xx");
			MOCSendACK();
			break;

		// class 5XX: Server failures
		case 500:	// Server Internal Error
		case 501:	// Not Implemented: The SIP request method is not implemented here
		case 502:	// Bad Gateway
		case 503:	// Service Unavailable
		case 504:	// Server Time-out
		case 505:	// Version Not Supported: The server does not support this version of the SIP protocol
		case 513:	// Message Too Large
			LOG(NOTICE) << "server failure code " << status;
			mState = Fail;
			gReports.incr("OpenBTS.SIP.Failed.Remote.5xx");
			MOCSendACK();
			break;

		// class 6XX: Global failures
		case 600:	// Busy Everywhere
		case 603:	// Decline
			mState = Busy;
			MOCSendACK();
			break;
		case 604:	// Does Not Exist Anywhere
		case 606:	// Not Acceptable
			LOG(NOTICE) << "global failure code " << status;
			mState = Fail;
			gReports.incr("OpenBTS.SIP.Failed.Remote.6xx");
			MOCSendACK();
		default:
			LOG(NOTICE) << "unhandled status code " << status;
			mState = Fail;
			gReports.incr("OpenBTS.SIP.Failed.Remote.xxx");
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
	gReports.incr("OpenBTS.SIP.BYE.Out");
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

SIPState SIPEngine::MODSendERROR(osip_message_t * cause, int code, const char * reason, bool cancel)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	if (NULL == cause){
		if (!mINVITE){
			LOG(WARNING)  << "Sending ERROR without invite, probably a CLI generated message";
			return mState;
		}
		cause = mINVITE;
	}
	
	osip_message_t * error = sip_error(cause, mSIPIP.c_str(),
					   mSIPUsername.c_str(), mSIPPort, 
					   code, reason);
	gSIPInterface.write(&mProxyAddr,error);
	saveERROR(error, true);
	osip_message_free(error);
	if (cancel){
		mState = MODCanceling;
	}
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
	if (mState!=MODCanceling) LOG(ERR) << "incorrect state for this method";
	assert(mCANCEL);
	gSIPInterface.write(&mProxyAddr,mCANCEL);
	return mState;
}

SIPState SIPEngine::MODResendERROR(bool cancel)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	if (cancel){
		if (mState!=MODCanceling) LOG(ERR) << "incorrect state for this method";
	}
	assert(mERROR);
	gSIPInterface.write(&mProxyAddr,mERROR);
	return mState;
}

/* there shouldn't be any more communications on this fifo, but we might
   get a 487 RequestTerminated. We only need to respond and move on -kurtis */
SIPState SIPEngine::MODWaitFor487(Mutex *lock)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	osip_message_t * msg;
	try {
		msg = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.E"), lock);
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

SIPState SIPEngine::MODWaitForBYEOK(Mutex *lock)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	bool responded = false;
	Timeval timeout(gConfig.getNum("SIP.Timer.F")); 
	while (!timeout.passed()) {
		try {
			osip_message_t * ok = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.E"),lock);
			responded = true;
			unsigned code = ok->status_code;
			saveResponse(ok);
			osip_message_free(ok);
			if (code!=200) {
				LOG(WARNING) << "unexpected " << code << " response to BYE, from proxy " << mProxyIP << ":" << mProxyPort << ". Assuming other end has cleared";
			} else {
				gReports.incr("OpenBTS.SIP.BYE-OK.In");
			}
			break;
		}
		catch (SIPTimeout& e) {
			LOG(NOTICE) << "response timeout, resending BYE";
			MODResendBYE();
		}
	}

	if (!responded) {
		LOG(ALERT) << "lost contact with proxy " << mProxyIP << ":" << mProxyPort;
		gReports.incr("OpenBTS.SIP.LostProxy");
	}

	mState = Cleared;

	return mState;
}

SIPState SIPEngine::MODWaitForCANCELOK(Mutex *lock)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	bool responded = false;
	Timeval timeout(gConfig.getNum("SIP.Timer.F")); 
	while (!timeout.passed()) {
		try {
			osip_message_t * ok = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.E"),lock);
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

	if (!responded) {
		LOG(ALERT) << "lost contact with proxy " << mProxyIP << ":" << mProxyPort;
		gReports.incr("OpenBTS.SIP.LostProxy");
	}

	mState = Canceled;

	return mState;
}

static bool containsResponse(vector<unsigned> *validResponses, unsigned code)
{
	for (int i = 0; i < validResponses->size(); i++) {
		if (validResponses->at(i) == code)
			return true;
	}
	return false;
}

SIPState SIPEngine::MODWaitForResponse(vector<unsigned> *validResponses, Mutex *lock)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	assert(validResponses);
	bool responded = false;
	Timeval timeout(gConfig.getNum("SIP.Timer.F")); 
	while (!timeout.passed()) {
		try {
			osip_message_t * resp = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.E"),lock);
			responded = true;
			unsigned code = resp->status_code;
			if (code==200) {
				saveResponse(resp);
				mState = Canceled;
			}
			if (code==487) {
				osip_message_t* ack = sip_ack( mRemoteDomain.c_str(),
					       mRemoteUsername.c_str(), 
					       mSIPUsername.c_str(),
					       mSIPPort, mSIPIP.c_str(), mProxyIP.c_str(), 
					       mMyToFromHeader, mRemoteToFromHeader,
					       mViaBranch.c_str(), mCallIDHeader, mCSeq);
				gSIPInterface.write(&mProxyAddr,ack);
				osip_message_free(ack);
			}
			osip_message_free(resp);
			if (!containsResponse(validResponses, code)) {
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

	return mState;
}

SIPState SIPEngine::MODWaitForERRORACK(bool cancel, Mutex *lock)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	bool responded = false;
	Timeval timeout(gConfig.getNum("SIP.Timer.F")); 
	while (!timeout.passed()) {
		try {
			osip_message_t * ack = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.E"),lock);
			responded = true;
			saveResponse(ack);
			if ((NULL == ack->sip_method) || !strncmp(ack->sip_method,"ACK", 4)) {
				LOG(WARNING) << "unexpected response to ERROR, from proxy " << mProxyIP << ":" << mProxyPort << ". Assuming other end has cleared";
			}
			osip_message_free(ack);
			break;
		}
		catch (SIPTimeout& e) {
			LOG(NOTICE) << "response timeout, resending ERROR";
			MODResendERROR(cancel);
		}
	}

	if (!responded) {
		LOG(ALERT) << "lost contact with proxy " << mProxyIP << ":" << mProxyPort;
		gReports.incr("OpenBTS.SIP.LostProxy");
	}

	if (cancel){
		mState = Canceled;
	}

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
		
	osip_message_t * msg = gSIPInterface.read(mCallID,0,NULL);
	

	if (msg->sip_method) {
		if (strcmp(msg->sip_method,"BYE")==0) { 
			LOG(DEBUG) << "found msg="<<msg->sip_method;	
			saveBYE(msg,false);
			gReports.incr("OpenBTS.SIP.BYE.In");
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
	gReports.incr("OpenBTS.SIP.BYE-OK.Out");
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
	if (mINVITE==NULL) {
		mState=Fail;
		gReports.incr("OpenBTS.SIP.Failed.Local");
	}
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



SIPState SIPEngine::MTCSendOK( short wRTPPort, unsigned wCodec, const GSM::LogicalChannel *chan)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	assert(mINVITE);
	gReports.incr("OpenBTS.SIP.INVITE-OK.Out");
	mRTPPort = wRTPPort;
	mCodec = wCodec;
	LOG(DEBUG) << "port=" << wRTPPort << " codec=" << mCodec;
	// Form ack from invite and new parameters.
	osip_message_t * okay = sip_okay_sdp(mINVITE, mSIPUsername.c_str(),
		mSIPIP.c_str(), mSIPPort, mRTPPort, mCodec);
	writePrivateHeaders(okay,chan);
	gSIPInterface.write(&mProxyAddr,okay);
	osip_message_free(okay);
	mState=Connecting;
	return mState;
}

SIPState SIPEngine::MTCCheckForACK(Mutex *lock)
{
	// wait for ack,set this to timeout of 
	// of call channel.  If want a longer timeout 
	// period, need to split into 2 handle situation 
	// like MOC where this fxn is called multiple times. 
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	//if (mState==Fail) return mState;
	//osip_message_t * ack = NULL;
	osip_message_t * ack;

	try {
		ack = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.H"), lock);
	}
	catch (SIPTimeout& e) {
		LOG(NOTICE) << "timeout";
		gReports.incr("OpenBTS.SIP.ReadTimeout");
		mState = Timeout;
		return mState;
	}
	catch (SIPError& e) {
		LOG(NOTICE) << "read error";
		gReports.incr("OpenBTS.SIP.Failed.Local");
		mState = Fail;
		return mState;
	}

	if (ack->sip_method==NULL) {
		LOG(NOTICE) << "SIP message with no method, status " <<  ack->status_code;
		gReports.incr("OpenBTS.SIP.Failed.Local");
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
		gReports.incr("OpenBTS.SIP.Failed.Local");
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
	//if (mState=Fail) return Fail;
	//osip_message_t * msg = NULL;
	osip_message_t * msg;

	try {
		msg = gSIPInterface.read(mCallID,0,NULL);
	}
	catch (SIPTimeout& e) {
		gReports.incr("OpenBTS.SIP.ReadTimeout");
		return mState;
	}
	catch (SIPError& e) {
		LOG(NOTICE) << "read error";
		mState = Fail;
		gReports.incr("OpenBTS.SIP.Failed.Local");
		return mState;
	}

	if (msg->sip_method==NULL) {
		LOG(NOTICE) << "SIP message with no method, status " <<  msg->status_code;
		if (mState!=Fail) {
			mState = Fail;
			gReports.incr("OpenBTS.SIP.Failed.Local");
		}
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
		gReports.incr("OpenBTS.SIP.Failed.Local");
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
		if (rfc2833) { LOG(CRIT) << "RTP session does not support selected DTMF method RFC-2833"; }
		else { LOG(CRIT) << "RTP session does not support telephone events"; }
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
	if (get_rtp_tev_type(key) < 0){
	    return false;
	}
	mDTMF = key;
	mDTMFDuration = 0;
	mDTMFStartTime = mTxTime;
	//true means start
	mblk_t *m = rtp_session_create_telephone_event_packet(mSession,true);
	//volume 10 for some magic reason, false means not end
	int code = rtp_session_add_telephone_event(mSession,m,get_rtp_tev_type(mDTMF),false,10,mDTMFDuration);
	int bytes = rtp_session_sendm_with_ts(mSession,m,mDTMFStartTime);
	mDTMFDuration += 160;
	if (!code && bytes > 0) return true;
	// Error?  Turn off DTMF sending.
	LOG(WARNING) << "DTMF RFC-2833 failed on start.";
	mDTMF = '\0';
	return false;
}

void SIPEngine::stopDTMF()
{
	//false means not start
	mblk_t *m = rtp_session_create_telephone_event_packet(mSession,false);
	//volume 10 for some magic reason, end is true
	int code = rtp_session_add_telephone_event(mSession,m,get_rtp_tev_type(mDTMF),true,10,mDTMFDuration);
	int bytes = rtp_session_sendm_with_ts(mSession,m,mDTMFStartTime);
	mDTMFDuration += 160;
	LOG (DEBUG) << "DTMF RFC-2833 sending " << mDTMF << " " << mDTMFDuration;
	// Turn it off if there's an error.
	if (code || bytes <= 0) {
	    LOG(ERR) << "DTMF RFC-2833 failed at end";
	}
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
		//false means not start
		mblk_t *m = rtp_session_create_telephone_event_packet(mSession,false);
		//volume 10 for some magic reason, false means not end
		int code = rtp_session_add_telephone_event(mSession,m,get_rtp_tev_type(mDTMF),false,10,mDTMFDuration);
		int bytes = rtp_session_sendm_with_ts(mSession,m,mDTMFStartTime);
		mDTMFDuration += 160;
		LOG (DEBUG) << "DTMF RFC-2833 sending " << mDTMF << " " << mDTMFDuration;
		// Turn it off if there's an error.
		if (code || bytes <=0) {
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
	const char * wCalledDomain , const char *messageText, const char *contentType,
	const GSM::LogicalChannel *chan)
{
	LOG(DEBUG) << "mState=" << mState;
	LOG(INFO) << "SIP send to " << wCalledUsername << "@" << wCalledDomain << " MESSAGE " << messageText;
	// Before start, need to add mCallID
	gSIPInterface.addCall(mCallID);
	mInstigator = true;
	gReports.incr("OpenBTS.SIP.MESSAGE.Out");
	
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
	
	writePrivateHeaders(message,chan);

	// Send Invite to the SIP proxy.
	gSIPInterface.write(&mProxyAddr,message);
	saveINVITE(message,true);
	osip_message_free(message);
	mState = MessageSubmit;
	return mState;
};


SIPState SIPEngine::MOSMSWaitForSubmit(Mutex *lock)
{
	LOG(INFO) << "user " << mSIPUsername << " state " << mState;

	Timeval timeout(gConfig.getNum("SIP.Timer.B"));
	assert(mINVITE);
	osip_message_t *ok = NULL;
	// have we received a 100 TRYING message? If so, don't retransmit after timeout
	bool recv_trying = false;
	while (!timeout.passed()) {
		try {
			// SIPInterface::read will throw SIPTIimeout if it times out.
			// It should not return NULL.
			ok = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.A"),lock);
		}
		catch (SIPTimeout& e) {
			if (!recv_trying){
				LOG(NOTICE) << "SIP MESSAGE packet to " << mProxyIP << ":" << mProxyPort << " timedout; resending"; 
				gSIPInterface.write(&mProxyAddr,mINVITE);
			} else {
			  	LOG(NOTICE) << "SIP MESSAGE packet to " << mProxyIP << ":" << mProxyPort << " timedout; ignoring (got 100 TRYING)"; 
			}
			continue;
		}
		assert(ok);
		if((ok->status_code==100)) {
			recv_trying = true;
			LOG(INFO) << "received TRYING MESSAGE";
		}
		if((ok->status_code==200) || (ok->status_code==202) ) {
			mState = Cleared;
			LOG(INFO) << "successful SIP MESSAGE SMS submit to " << mProxyIP << ":" << mProxyPort << ": " << mINVITE;
			break;
		}
		//demonstrate that these are not forwarded correctly
		if (ok->status_code >= 400){
			mState = Fail;
			gReports.incr("OpenBTS.SIP.Failed.Remote.4xx");
			LOG (ALERT) << "SIP MESSAGE rejected: " << ok->status_code << " " << ok->reason_phrase;
			break;
		}
		LOG(WARNING) << "unhandled response " << ok->status_code;
		osip_message_free(ok);
		ok = NULL;
	}

	if (!ok) {
		//changed from "throw SIPTimeout()", as this seems more correct -k
		mState = Fail;
		gReports.incr("OpenBTS.SIP.Failed.Local");
		gReports.incr("OpenBTS.SIP.ReadTimeout");
		LOG(ALERT) << "SIP MESSAGE timed out; is the smqueue server " << mProxyIP << ":" << mProxyPort << " OK?";
		gReports.incr("OpenBTS.SIP.LostProxy");
	} else {
		osip_message_free(ok);
	}
	return mState;

}



SIPState SIPEngine::MTSMSSendOK(const GSM::LogicalChannel *chan)
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
	writePrivateHeaders(okay,chan);
	gSIPInterface.write(&mProxyAddr,okay);
	osip_message_free(okay);
	mState=Cleared;
	return mState;
}



bool SIPEngine::sendINFOAndWaitForOK(unsigned wInfo, Mutex *lock)
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

	Timeval timeout(gConfig.getNum("SIP.Timer.F"));
	osip_message_t *ok = NULL;
	while (!timeout.passed()) {
		try {
			// This will timeout on failure.  It will not return NULL.
			ok = gSIPInterface.read(mCallID, gConfig.getNum("SIP.Timer.E"), lock);
			LOG(DEBUG) << "received status " << ok->status_code << " " << ok->reason_phrase;
		}
		catch (SIPTimeout& e) { 
			LOG(NOTICE) << "SIP RFC-2967 INFO packet to " << mProxyIP << ":" << mProxyPort << " timedout; resending"; 
			gSIPInterface.write(&mProxyAddr,info);
			continue;
		}
	}
	osip_message_free(info);
	if (!ok) {
		LOG(ALERT) << "SIP RFC-2967 INFO timed out; is the proxy at " << mProxyIP << ":" << mProxyPort << " OK?";
		gReports.incr("OpenBTS.SIP.LostProxy");
		return false;
	}
	LOG(DEBUG) << "received status " << ok->status_code << " " << ok->reason_phrase;
	bool retVal = (ok->status_code==200);
	osip_message_free(ok);
	if (!retVal) LOG(WARNING) << "SIP RFC-2967 INFO failed on server " << mProxyIP << ":" << mProxyPort << " OK?";
	return retVal;
}

/* reinvite stuff */
/* return true if this is the same invite as the one we have stored */
bool SIPEngine::sameINVITE(osip_message_t * msg){
	assert(mINVITE);
	if (NULL == msg){
		LOG(NOTICE) << "trying to compare empty message";
		return false;
	}
	// We are assuming that the callids match.
	// Otherwise, this would not have been called.
	// FIXME -- Check callids and assrt if they down match.
	// So we just check the CSeq.
	// FIXME -- Check all of the pointers along these chains and log ERR if anthing is missing.

	const char *cn1 = msg->cseq->number;
	if (!cn1) {
		LOG(ERR) << "no cseq in msg";
		return false;
	}
	int n1 = atoi(cn1);

	const char *cn2 = mINVITE->cseq->number;
	if (!cn2) {
		LOG(ERR) << "no cseq in mINVITE";
		return false;
	}
	int n2 = atoi(cn2);

	if (n1!=n2) {
		LOG(NOTICE) << "possible reinvite CSeq A " << cn1 << " (" << n1 << ") CSeq B " << cn2 << " (" << n2 << ")";
	}

	return n1==n2;
}


SIPState SIPEngine::inboundHandoverCheckForOK(Mutex *lock)
{
	return MOCCheckForOK(lock);
}



SIPState SIPEngine::inboundHandoverSendACK()
{
	return MOCSendACK();
}

SIPState SIPEngine::inboundHandoverSendINVITE(TransactionEntry *transaction, unsigned wRTPPort)
{
	// We are "BS2" in the handover ladder diagram.

	LOG(INFO) << "user " << mSIPUsername << " state " << mState;
	// Before start, need to add mCallID
	mCallID = transaction->CallID();
	gSIPInterface.addCall(mCallID);

	// Set Invite params. 
	// New from tag + via branch
	// new CSEQ and codec 
	char tmp[100];
	make_tag(tmp);
	make_branch(tmp);
	mViaBranch = tmp;
	mCodec = transaction->Codec();
	mCSeq = transaction->CSeq();
	mCSeq++;

	mRemoteDomain = transaction->ToIP();
	mRemoteUsername = transaction->ToUsername();
	mRTPPort = wRTPPort;
	mRTPRemPort = transaction->RTPRemPort();
	mRTPRemIP = transaction->RTPRemIP();
	mSIPUsername = transaction->FromUsername();
	string SIPDisplayname = transaction->FromUsername();
	mFromTag = transaction->FromTag();

	if(mSession == NULL) {
		mSession = rtp_session_new(RTP_SESSION_SENDRECV);
		// do what we need to from InitRTP() without a message
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
		rtp_session_set_local_addr(mSession, "0.0.0.0", mRTPPort );
		rtp_session_set_remote_addr(mSession, mRTPRemIP.c_str(), mRTPRemPort);
		// Check for event support.
		int code = rtp_session_telephone_events_supported(mSession);
		if (code == -1) {
			if (rfc2833) { LOG(ALERT) << "RTP session does not support selected DTMF method RFC-2833"; }
			else { LOG(WARNING) << "RTP session does not support telephone events"; }
		}
	}

	// unpack RTP state and shove it into the session structure
	char *items = strdup(transaction->RTPState().c_str());
	char *thisItem;
	vector<long> RTPState;
	while ((thisItem=strsep(&items,","))!=NULL) {
		RTPState.push_back(strtol(thisItem,NULL,10));
	}
	free(items);
	assert(RTPState.size() == 19);
	/* Out of desperation, when the RTP refused to work, I transferred from BS1 to BS2 just about
	 * all the state in this struct.  Well, it turns out NONE of it is necessary.  Something else
	 * entirely was the problem.  (Or, technically, state in a different struct.)  Anyway, I'm
	 * leaving the transferring, and just not copying in anything here.  So if any of it appears
	 * to be important some day, it will be easy to experiment.  You're welcome.
	mSession->rtp.snd_time_offset = RTPState[0];
	mSession->rtp.snd_ts_offset = RTPState[1];
	mSession->rtp.snd_rand_offset = RTPState[2];
	mSession->rtp.snd_last_ts = RTPState[3];
	mSession->rtp.rcv_time_offset = RTPState[4];
	mSession->rtp.rcv_ts_offset = RTPState[5];
	mSession->rtp.rcv_query_ts_offset = RTPState[6];
	mSession->rtp.rcv_last_ts = RTPState[7];
	mSession->rtp.rcv_last_app_ts = RTPState[8];
	mSession->rtp.rcv_last_ret_ts = RTPState[9];
	mSession->rtp.hwrcv_extseq = RTPState[10];
	mSession->rtp.hwrcv_seq_at_last_SR = RTPState[11];
	mSession->rtp.hwrcv_since_last_SR = RTPState[12];
	mSession->rtp.last_rcv_SR_ts = RTPState[13];
	mSession->rtp.last_rcv_SR_time.tv_sec = RTPState[14]; mSession->rtp.last_rcv_SR_time.tv_usec = RTPState[15];
	mSession->rtp.snd_seq = RTPState[16];
	mSession->rtp.last_rtcp_report_snt_r = RTPState[17];
	mSession->rtp.last_rtcp_report_snt_s = RTPState[18];
	mSession->rtp.rtcp_report_snt_interval = RTPState[19];
	mSession->rtp.last_rtcp_packet_count = RTPState[20];
	mSession->rtp.sent_payload_bytes = RTPState[21];
	*/

	osip_message_t * invite = sip_reinvite(
		mRemoteUsername.c_str(), mRemoteDomain.c_str(),
		SIPDisplayname.c_str(), mSIPUsername.c_str(),
		transaction->FromTag().c_str(), transaction->FromUsername().c_str(), transaction->FromIP().c_str(),
		transaction->ToTag().c_str(), transaction->ToUsername().c_str(), transaction->ToIP().c_str(),
		mViaBranch.c_str(), mCallID.c_str(), transaction->CallIP().c_str(),
		mCSeq, mCodec, mRTPPort,
		transaction->SessionID().c_str(), transaction->SessionVersion().c_str());

	// Send Invite to remote party.
	struct sockaddr_in rmt;
	if (!resolveAddress(&rmt, transaction->RmtIP().c_str(), transaction->RmtPort())) {
		LOG(ALERT) << "unable to resolve IP address of remote party to send INVITE";
		mState = Fail;
		gReports.incr("OpenBTS.SIP.Failed");
		gReports.incr("OpenBTS.SIP.LostProxy");
		return mState;
	}
	gSIPInterface.write(&rmt, invite);
	saveINVITE(invite, true);
	osip_message_free(invite);
	// FIXME - is this the right state?
	mState = Starting;
	return mState;
}


// vim: ts=4 sw=4
