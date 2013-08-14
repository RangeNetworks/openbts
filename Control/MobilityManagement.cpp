/**@file GSM/SIP Mobility Management, GSM 04.08. */
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


#include <Timeval.h>

#include "ControlCommon.h"
#include "MobilityManagement.h"
#include "SMSControl.h"
#include "CallControl.h"
#include "RRLPServer.h"

#include <GSMLogicalChannel.h>
#include <GSML3RRMessages.h>
#include <GSML3MMMessages.h>
#include <GSML3CCMessages.h>
#include <GSMConfig.h>

using namespace std;

#include <SIPInterface.h>
#include <SIPUtility.h>
#include <SIPMessage.h>
#include <SIPEngine.h>
#include <SubscriberRegistry.h>

using namespace SIP;

#include <Regexp.h>
#include <Reporting.h>
#include <Logger.h>
#undef WARNING


using namespace GSM;
using namespace Control;


/** Controller for CM Service requests, dispatches out to multiple possible transaction controllers. */
void Control::CMServiceResponder(const L3CMServiceRequest* cmsrq, LogicalChannel* DCCH)
{
	assert(cmsrq);
	assert(DCCH);
	LOG(INFO) << *cmsrq;
	switch (cmsrq->serviceType().type()) {
		case L3CMServiceType::MobileOriginatedCall:
			gReports.incr("OpenBTS.GSM.MM.CMServiceRequest.MOC");
			MOCStarter(cmsrq,DCCH);
			break;
		case L3CMServiceType::ShortMessage:
			gReports.incr("OpenBTS.GSM.MM.CMServiceRequest.MOSMS");
			MOSMSController(cmsrq,DCCH);
			break;
		default:
			gReports.incr("OpenBTS.GSM.MM.CMServiceRequest.Unhandled");
			LOG(NOTICE) << "service not supported for " << *cmsrq;
			// Cause 0x20 means "serivce not supported".
			DCCH->send(L3CMServiceReject(0x20));
			DCCH->send(L3ChannelRelease());
	}
	// The transaction may or may not be cleared,
	// depending on the assignment type.
}




/** Controller for the IMSI Detach transaction, GSM 04.08 4.3.4. */
void Control::IMSIDetachController(const L3IMSIDetachIndication* idi, LogicalChannel* DCCH)
{
	assert(idi);
	assert(DCCH);
	LOG(INFO) << *idi;

	// The IMSI detach maps to a SIP unregister with the local Asterisk server.
	try { 
		// FIXME -- Resolve TMSIs to IMSIs.  (pat) And when you do call GPRSNotifyGsmActivity() on it.
		if (idi->mobileID().type()==IMSIType) {
			const char *digits = idi->mobileID().digits();
			GPRS::GPRSNotifyGsmActivity(digits);
			SIPEngine engine(gConfig.getStr("SIP.Proxy.Registration").c_str(), digits);
			engine.unregister();
		}
	}
	catch(SIPTimeout) {
		LOG(ALERT) "SIP registration timed out.  Is Asterisk running?";
	}
	// No reponse required, so just close the channel.
	DCCH->send(L3ChannelRelease());
	// Many handsets never complete the transaction.
	// So force a shutdown of the channel.
	DCCH->send(HARDRELEASE);
}




/**
	Send a given welcome message from a given short code.
	@return true if it was sent
*/
bool sendWelcomeMessage(const char* messageName, const char* shortCodeName, const char *IMSI, LogicalChannel* DCCH, const char *whiteListCode = NULL)
{
	if (!gConfig.defines(messageName) || !gConfig.defines(shortCodeName)) return false;
	if (!gConfig.getStr(messageName).length() || !gConfig.getStr(shortCodeName).length()) return false;
	LOG(INFO) << "sending " << messageName << " message to handset";
	ostringstream message;
	message << gConfig.getStr(messageName) << " IMSI:" << IMSI;
	if (whiteListCode) {
		message << ", white-list code: " << whiteListCode;
	}
	// This returns when delivery is acked in L3.
	deliverSMSToMS(
		gConfig.getStr(shortCodeName).c_str(),
		message.str().c_str(), "text/plain",
		random()%7,DCCH);
	return true;
}


/**
	Check if a phone is white-listed.
	@param name name of subscriber
	@return true if phone was already white-listed
*/
bool isPhoneWhiteListed(string name)
{

	// if name isn't in SR, then put it in with white-list flag off
	string id = gSubscriberRegistry.imsiGet(name, "id");
	if (id.empty()) {
		//we used to create a user here, but that's almost certainly wrong. -kurtis
		LOG(CRIT) << "Checking whitelist of a user that doesn't exist. Reject";
		//return not-white-listed
		return false;
	}
	// check flag
	string whiteListFlag = gSubscriberRegistry.imsiGet(name, "whiteListFlag");
	if (whiteListFlag.empty()){
		LOG(CRIT) << "SR query error";
		return false;
	}
	return (whiteListFlag == "0");
}

/**
	Generate white-list code.
	@param name name of subscriber
	@return the white-list code.
	Also put it into the SR database.
*/
string genWhiteListCode(string name)
{
	// generate code
	uint32_t wlc = (uint32_t)rand();
	ostringstream os2;
	os2 << hex << wlc;
	string whiteListCode = os2.str();
	// write to SR
	if (gSubscriberRegistry.imsiSet(name, "whiteListCode", whiteListCode)){
		LOG(CRIT) << "SR update error";
		return "";
	}
	// and return it
	return whiteListCode;
}



/**
	Controller for the Location Updating transaction, GSM 04.08 4.4.4.
	@param lur The location updating request.
	@param DCCH The Dm channel to the MS, which will be released by the function.
*/
void Control::LocationUpdatingController(const L3LocationUpdatingRequest* lur, LogicalChannel* DCCH)
{
	assert(DCCH);
	assert(lur);
	LOG(INFO) << *lur;

	// The location updating request gets mapped to a SIP
	// registration with the SIP registrar.

	// We also allocate a new TMSI for every handset we encounter.
	// If the handset is allowed to register it may receive a TMSI reassignment.
	gReports.incr("OpenBTS.GSM.MM.LUR.Start");

	// Resolve an IMSI and see if there's a pre-existing IMSI-TMSI mapping.
	// This operation will throw an exception, caught in a higher scope,
	// if it fails in the GSM domain.
	L3MobileIdentity mobileID = lur->mobileID();
	bool sameLAI = (lur->LAI() == gBTS.LAI());
	unsigned preexistingTMSI = resolveIMSI(sameLAI,mobileID,DCCH);
	const char *IMSI = mobileID.digits();
	// IMSIAttach set to true if this is a new registration.
	bool IMSIAttach = (preexistingTMSI==0);

	// We assign generate a TMSI for every new phone we see,
	// even if we don't actually assign it.
	unsigned newTMSI = 0;
	if (!preexistingTMSI) newTMSI = gTMSITable.assign(IMSI,lur);

	// White-listing.
	const string name = "IMSI" + string(IMSI);
	if (gConfig.getBool("Control.LUR.WhiteList")) {
		LOG(INFO) << "checking white-list for " << name;
		if (!isPhoneWhiteListed(name)) {
			// not white-listed.  reject phone.
			LOG(INFO) << "is NOT white-listed";
			DCCH->send(L3LocationUpdatingReject(gConfig.getNum("Control.LUR.WhiteListing.RejectCause")));
			if (!preexistingTMSI) {
				// generate code (and put in SR) and send message if first time.
				string whiteListCode = genWhiteListCode(name);
				LOG(INFO) << "generated white-list code: " << whiteListCode;
				sendWelcomeMessage("Control.LUR.WhiteListing.Message", "Control.LUR.WhiteListing.ShortCode", IMSI, DCCH, whiteListCode.c_str());
			}
			// Release the channel and return.
			DCCH->send(L3ChannelRelease());
			return;
		} else {
			LOG(INFO) << "IS white-listed";
		}
	} else {
		LOG(INFO) << "not checking white-list for " << name;
	}

	// Try to register the IMSI.
	// This will be set true if registration succeeded in the SIP world.
	bool success = false;
	string RAND;
	try {
		SIPEngine engine(gConfig.getStr("SIP.Proxy.Registration").c_str(),IMSI);
		LOG(DEBUG) << "waiting for registration of " << IMSI << " on " << gConfig.getStr("SIP.Proxy.Registration");
		success = engine.Register(SIPEngine::SIPRegister, DCCH, &RAND); 
	}
	catch(SIPTimeout) {
		LOG(ALERT) "SIP registration timed out.  Is the proxy running at " << gConfig.getStr("SIP.Proxy.Registration");
		// Reject with a "network failure" cause code, 0x11.
		DCCH->send(L3LocationUpdatingReject(0x11));
		gReports.incr("OpenBTS.GSM.MM.LUR.Timeout");
		// HACK -- wait long enough for a response
		// FIXME -- Why are we doing this?
		sleep(4);
		// Release the channel and return.
		DCCH->send(L3ChannelRelease());
		return;
	}

	// Query for classmark?
	// This needs to happen before encryption.
	// It can't be optional if encryption is desired.
	if (IMSIAttach && (gConfig.getBool("GSM.Cipher.Encrypt") || gConfig.getBool("Control.LUR.QueryClassmark"))) {
		DCCH->send(L3ClassmarkEnquiry());
		L3Message* msg = getMessage(DCCH);
		L3ClassmarkChange *resp = dynamic_cast<L3ClassmarkChange*>(msg);
		if (!resp) {
			if (msg) {
				LOG(WARNING) << "Unexpected message " << *msg;
				delete msg;
			}
			throw UnexpectedMessage();
		}
		LOG(INFO) << *resp;
		const L3MobileStationClassmark2& classmark = resp->classmark();
		if (!gTMSITable.classmark(IMSI,classmark))
			LOG(WARNING) << "failed access to TMSITable";
		delete msg;
	}

	// Did we get a RAND for challenge-response?
	if (RAND.length() != 0) {
		// Get the mobile's SRES.
		LOG(INFO) << "sending " << RAND << " to mobile";
		uint64_t uRAND;
		uint64_t lRAND;
		gSubscriberRegistry.stringToUint(RAND, &uRAND, &lRAND);
		gReports.incr("OpenBTS.GSM.MM.Authenticate.Request");
		DCCH->send(L3AuthenticationRequest(0,L3RAND(uRAND,lRAND)));
		L3Message* msg = getMessage(DCCH);
		L3AuthenticationResponse *resp = dynamic_cast<L3AuthenticationResponse*>(msg);
		if (!resp) {
			if (msg) {
				LOG(WARNING) << "Unexpected message " << *msg;
				delete msg;
			}
			// FIXME -- We should differentiate between wrong message and no message at all.
			throw UnexpectedMessage();
		}
		LOG(INFO) << *resp;
		uint32_t mobileSRES = resp->SRES().value();
		delete msg;
		// verify SRES 
		try {
			SIPEngine engine(gConfig.getStr("SIP.Proxy.Registration").c_str(),IMSI);
			LOG(DEBUG) << "waiting for authentication of " << IMSI << " on " << gConfig.getStr("SIP.Proxy.Registration");
			ostringstream os;
			os << hex << mobileSRES;
			string SRESstr = os.str();
			success = engine.Register(SIPEngine::SIPRegister, DCCH, &RAND, IMSI, SRESstr.c_str()); 
			if (!success) {
				gReports.incr("OpenBTS.GSM.MM.Authenticate.Failure");
				LOG(CRIT) << "authentication failure for IMSI " << IMSI;
				DCCH->send(L3AuthenticationReject());
				DCCH->send(L3ChannelRelease());
				return;
			}
			if (gConfig.getBool("GSM.Cipher.Encrypt")) {
				int encryptionAlgorithm = gTMSITable.getPreferredA5Algorithm(IMSI);
				if (!encryptionAlgorithm) {
					LOG(DEBUG) << "A5/3 and A5/1 not supported: NOT sending Ciphering Mode Command on " << *DCCH << " for " << mobileID;
				} else if (DCCH->decryptUplink_maybe(mobileID.digits(), encryptionAlgorithm)) {
					LOG(DEBUG) << "sending Ciphering Mode Command on " << *DCCH << " for " << mobileID;
					DCCH->send(GSM::L3CipheringModeCommand(
						GSM::L3CipheringModeSetting(true, encryptionAlgorithm),
						GSM::L3CipheringModeResponse(false)));
				} else {
					LOG(DEBUG) << "no ki: NOT sending Ciphering Mode Command on " << *DCCH << " for " << mobileID;
				}
			}
			gReports.incr("OpenBTS.GSM.MM.Authenticate.Success");
		}
		catch(SIPTimeout) {
			LOG(ALERT) "SIP authentication timed out.  Is the proxy running at " << gConfig.getStr("SIP.Proxy.Registration");
			// Reject with a "network failure" cause code, 0x11.
			DCCH->send(L3LocationUpdatingReject(0x11));
			// HACK -- wait long enough for a response
			// FIXME -- Why are we doing this?
			sleep(4);
			// Release the channel and return.
			DCCH->send(L3ChannelRelease());
			return;
		}
	}

	// This allows us to configure Open Registration
	bool openRegistration = false;
	string openRegistrationPattern = gConfig.getStr("Control.LUR.OpenRegistration");
	if (openRegistrationPattern.length()) {
		Regexp rxp(openRegistrationPattern.c_str());
		openRegistration = rxp.match(IMSI);
		string openRegistrationRejectPattern = gConfig.getStr("Control.LUR.OpenRegistration.Reject");
		if (openRegistrationRejectPattern.length()) {
			Regexp rxpReject(openRegistrationRejectPattern.c_str());
			bool openRegistrationReject = rxpReject.match(IMSI);
			openRegistration = openRegistration && !openRegistrationReject;
		}
	}

	// Query for IMEI?
	// FIXME -- This needs to happen before sending the REGISTER method, so we can put it in a SIP header.
	// See ticket #1101.
	unsigned rejectCause = gConfig.getNum("Control.LUR.UnprovisionedRejectCause");
	if (gConfig.getBool("Control.LUR.QueryIMEI")) {
		DCCH->send(L3IdentityRequest(IMEIType));
		L3Message* msg = getMessage(DCCH);
		L3IdentityResponse *resp = dynamic_cast<L3IdentityResponse*>(msg);
		if (!resp) {
			if (msg) {
				LOG(WARNING) << "Unexpected message " << *msg;
				delete msg;
			}
			throw UnexpectedMessage();
		}
		LOG(INFO) << *resp;
		string new_imei = resp->mobileID().digits();
		if (!gTMSITable.IMEI(IMSI,new_imei.c_str())){
			LOG(WARNING) << "failed access to TMSITable";
		}
		//query subscriber registry for old imei, update if necessary
		string old_imei = gSubscriberRegistry.imsiGet(name, "hardware");
		
		//if we have a new imei and either there's no old one, or it is different...
		if (!new_imei.empty() && (old_imei.empty() || old_imei != new_imei)){
			LOG(INFO) << "Updating IMSI" << IMSI << " to IMEI:" << new_imei;
			if (gSubscriberRegistry.imsiSet(name,"RRLPSupported", "1")) {
			 	LOG(INFO) << "SR RRLPSupported update problem";
			}
			if (gSubscriberRegistry.imsiSet(name,"hardware", new_imei)) {
				LOG(INFO) << "SR hardware update problem";
			}
		}
		delete msg;
	}

	// We fail closed unless we're configured otherwise
	if (!success && !openRegistration) {
		LOG(INFO) << "registration FAILED: " << mobileID;
		DCCH->send(L3LocationUpdatingReject(rejectCause));
		if (!preexistingTMSI) {
			sendWelcomeMessage( "Control.LUR.FailedRegistration.Message",
				"Control.LUR.FailedRegistration.ShortCode", IMSI,DCCH);
		}
		// Release the channel and return.
		DCCH->send(L3ChannelRelease());
		return;
	}

	// If success is true, we had a normal registration.
	// Otherwise, we are here because of open registration.
	// Either way, we're going to register a phone if we arrive here.

	if (success) {
		LOG(INFO) << "registration SUCCESS: " << mobileID;
	} else {
		LOG(INFO) << "registration ALLOWED: " << mobileID;
	}


	// Send the "short name" and time-of-day.
	if (IMSIAttach && gConfig.defines("GSM.Identity.ShortName")) {
		DCCH->send(L3MMInformation(gConfig.getStr("GSM.Identity.ShortName").c_str()));
	}
	// Accept. Make a TMSI assignment, too, if needed.
	if (preexistingTMSI || !gConfig.getBool("Control.LUR.SendTMSIs")) {
		DCCH->send(L3LocationUpdatingAccept(gBTS.LAI()));
	} else {
		assert(newTMSI);
		DCCH->send(L3LocationUpdatingAccept(gBTS.LAI(),newTMSI));
		// Wait for MM TMSI REALLOCATION COMPLETE (0x055b).
		L3Frame* resp = DCCH->recv(1000);
		// FIXME -- Actually check the response type.
		if (!resp) {
			LOG(NOTICE) << "no response to TMSI assignment";
		} else {
			LOG(INFO) << *resp;
		}
		delete resp;
	}

	if (gConfig.getBool("Control.LUR.QueryRRLP")) {
		// Query for RRLP
		if (!sendRRLP(mobileID, DCCH)) {
			LOG(INFO) << "RRLP request failed";
 		}
	}

	// If this is an IMSI attach, send a welcome message.
	if (IMSIAttach) {
		if (success) {
			sendWelcomeMessage( "Control.LUR.NormalRegistration.Message",
				"Control.LUR.NormalRegistration.ShortCode", IMSI, DCCH);
		} else {
			sendWelcomeMessage( "Control.LUR.OpenRegistration.Message",
				"Control.LUR.OpenRegistration.ShortCode", IMSI, DCCH);
		}
		// set unix time of most recent registration
		// No - this happending in the registration proxy.
		//gSubscriberRegistry.setRegTime(name);
	}

	// Release the channel and return.
	DCCH->send(L3ChannelRelease());
	return;
}




// vim: ts=4 sw=4
