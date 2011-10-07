/**@file GSM/SIP Mobility Management, GSM 04.08. */
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


#include <Timeval.h>

#include "ControlCommon.h"
#include "MobilityManagement.h"
#include "SMSControl.h"
#include "CallControl.h"

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
			MOCStarter(cmsrq,DCCH);
			break;
		case L3CMServiceType::ShortMessage:
			MOSMSController(cmsrq,DCCH);
			break;
		default:
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
		// FIXME -- Resolve TMSIs to IMSIs.
		if (idi->mobileID().type()==IMSIType) {
			SIPEngine engine(gConfig.getStr("SIP.Proxy.Registration").c_str(), idi->mobileID().digits());
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
	if (!gConfig.defines(messageName)) return false;
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



class RRLPServer
{
	public:
		RRLPServer(L3MobileIdentity wMobileID, LogicalChannel *wDCCH);
		// tell server to send location assistance to mobile
		bool assist();
		// tell server to ask mobile for location
		bool locate();
	private:
		RRLPServer(); // not allowed
		string url;
		L3MobileIdentity mobileID;
		LogicalChannel *DCCH;
		string query;
		string name;
		bool transact();
		bool trouble;
};

RRLPServer::RRLPServer(L3MobileIdentity wMobileID, LogicalChannel *wDCCH)
{
	trouble = false;
	url = gConfig.getStr("GSM.RRLP.SERVER.URL", "");
	if (url.length() == 0) {
		LOG(INFO) << "RRLP not configured";
		trouble = true;
		return;
	}
	mobileID = wMobileID;
	DCCH = wDCCH;
	// name of subscriber
	name = string("IMSI") + mobileID.digits();
	// don't continue if IMSI has a RRLP support flag and it's false
	unsigned int supported = 0;
	if (sqlite3_single_lookup(gSubscriberRegistry.db(), "sip_buddies", "name", name.c_str(),
							"RRLPSupported", supported) && !supported) {
		LOG(INFO) << "RRLP not supported for " << name;
		trouble = true;
	}
}

bool RRLPServer::assist()
{
	if (trouble) return false;
	query = "query=assist";
	return transact();
}

bool RRLPServer::locate()
{
	if (trouble) return false;
	query = "query=loc";
	return transact();
}

void clean(char *line)
{
	char *p = line + strlen(line) - 1;
	while (p > line && *p <= ' ') *p-- = 0;
}

string getConfig()
{
	const char *configs[] = {
		"GSM.RRLP.ACCURACY",
		"GSM.RRLP.RESPONSETIME",
		"GSM.RRLP.ALMANAC.URL",
		"GSM.RRLP.EPHEMERIS.URL",
		"GSM.RRLP.ALMANAC.REFRESH.TIME",
		"GSM.RRLP.EPHEMERIS.REFRESH.TIME",
		"GSM.RRLP.SEED.LATITUDE",
		"GSM.RRLP.SEED.LONGITUDE",
		"GSM.RRLP.SEED.ALTITUDE",
		"GSM.RRLP.EPHEMERIS.ASSIST.COUNT",
		"GSM.RRLP.ALMANAC.ASSIST.PRESENT",
		0
	};
	string config = "";
	for (const char **p = configs; *p; p++) {
		string configValue = gConfig.getStr(*p, "");
		if (configValue.length() == 0) return "";
		config.append("&");
		config.append(*p);
		config.append("=");
		if (0 == strcmp((*p) + strlen(*p) - 3, "URL")) {
			// TODO - better to have urlencode and decode, but then I'd have to look for them
			char buf[3];
			buf[2] = 0;
			for (const char *q = configValue.c_str(); *q; q++) {
				sprintf(buf, "%02x", *q);
				config.append(buf);
			}
		} else {
			config.append(configValue);
		}
	}
	return config;
}

bool RRLPServer::transact()
{
	vector<string> apdus;
	while (true) {
		// bounce off server
		string esc = "'";
		string config = getConfig();
		if (config.length() == 0) return false;
		string cmd = "wget -qO- " + esc + url + "?" + query + config + esc;
		LOG(INFO) << "*************** "  << cmd;
		FILE *result = popen(cmd.c_str(), "r");
		if (!result) {
			LOG(CRIT) << "popen call \"" << cmd << "\" failed";
			return NULL;
		}
		// build map of responses, and list of apdus
		map<string,string> response;
		size_t nbytes = 1500;
		char *line = (char*)malloc(nbytes+1);
		while (!feof(result)) {
			if (!fgets(line, nbytes, result)) break;
			clean(line);
			LOG(INFO) << "server return: " << line;
			char *p = strchr(line, '=');
			if (!p) continue;
			string lhs = string(line, 0, p-line);
			string rhs = string(line, p+1-line, string::npos);
			if (lhs == "apdu") {
				apdus.push_back(rhs);
			} else {
				response[lhs] = rhs;
			}
		}
		free(line);
		pclose(result);

		// quit if error
		if (response.find("error") != response.end()) {
			LOG(INFO) << "error from server: " << response["error"];
			return false;
		}

		// quit if ack from assist unless there are more apdu messages
		if (response.find("ack") != response.end()) {
			LOG(INFO) << "ack from mobile, decoded by server";
			if (apdus.size() == 0) {
				return true;
			} else {
				LOG(INFO) << "more apdu messages";
			}
		}

		// quit if location decoded 
		if (response.find("latitude") != response.end() && response.find("longitude") != response.end() && response.find("positionError") != response.end()) {
			ostringstream os;
			os << "insert into RRLP (name, latitude, longitude, error, time) values (" <<
				'"' << name << '"' << "," <<
				response["latitude"] << "," <<
				response["longitude"] << "," <<
				response["positionError"] << "," <<
				"datetime('now')"
			")";
			LOG(INFO) << os.str();
			if (!sqlite3_command(gSubscriberRegistry.db(), os.str().c_str())) {
				LOG(INFO) << "sqlite3_command problem";
				return false;
			}
			return true;
		}

		// bounce off mobile
		if (apdus.size() == 0) {
			LOG(INFO) << "missing apdu for mobile";
			return false;
		}
		string apdu = apdus[0];
		apdus.erase(apdus.begin());
		BitVector bv(apdu.size()*4);
		if (!bv.unhex(apdu.c_str())) {
			LOG(INFO) << "BitVector::unhex problem";
			return false;
		}

		DCCH->send(L3ApplicationInformation(bv));
		// Receive an L3 frame with a timeout.  Timeout loc req response time max + 2 seconds.
		L3Frame* resp = DCCH->recv(130000);
		if (!resp) {
			return false;
		}
		LOG(INFO) << "RRLPQuery returned " << *resp;
		if (resp->primitive() != DATA) {
			LOG(INFO) << "didn't receive data";
			switch (resp->primitive()) {
				case ESTABLISH: LOG(INFO) << "channel establihsment"; break;
				case RELEASE: LOG(INFO) << "normal channel release"; break;
				case DATA: LOG(INFO) << "multiframe data transfer"; break;
				case UNIT_DATA: LOG(INFO) << "datagram-type data transfer"; break;
				case ERROR: LOG(INFO) << "channel error"; break;
				case HARDRELEASE: LOG(INFO) << "forced release after an assignment"; break;
				default: LOG(INFO) << "unrecognized primitive response"; break;
			}
			delete resp;
			return false;
		}
		const unsigned PD_RR = 6;
		LOG(INFO) << "resp.pd = " << resp->PD();
		if (resp->PD() != PD_RR) {
			LOG(INFO) << "didn't receive an RR message";
			delete resp;
			return false;
		}
		const unsigned MTI_RR_STATUS = 18;
		if (resp->MTI() == MTI_RR_STATUS) {
			ostringstream os2;
			int cause = resp->peekField(16, 8);
			delete resp;
			switch (cause) {
				case 97:
					LOG(INFO) << "MS says: message not implemented";
					// flag unsupported in SR so we don't waste time on it again
					os2 << "update sip_buddies set RRLPSupported = \"0\" where name = \"" << name << "\"";
					if (!sqlite3_command(gSubscriberRegistry.db(), os2.str().c_str())) {
						LOG(INFO) << "sqlite3_command problem";
					}
					return false;
				case 98:
					LOG(INFO) << "MS says: message type not compatible with protocol state";
					return false;
				default:
					LOG(INFO) << "unknown RR_STATUS response, cause = " << cause;
					return false;
			}
		}
		const unsigned MTI_RR_APDU = 56;
		if (resp->MTI() != MTI_RR_APDU) {
			LOG(INFO) << "received unexpected RR Message " << resp->MTI();
			delete resp;
			return false;
		}

		// looks like a good APDU
		BitVector *bv2 = (BitVector*)resp;
		BitVector bv3 = bv2->tail(32);
		ostringstream os;
		bv3.hex(os);
		apdu = os.str();
		delete resp;

		// next query for server
		query = "query=apdu&apdu=" + apdu;
	}
	// not reached
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
	// registration with the Asterisk server.

	// We also allocate a new TMSI for every handset we encounter.
	// If the handset is allow to register it may receive a TMSI reassignment.

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

	// Try to register the IMSI.
	// This will be set true if registration succeeded in the SIP world.
	bool success = false;
	try {
		SIPEngine engine(gConfig.getStr("SIP.Proxy.Registration").c_str(),IMSI);
		LOG(DEBUG) << "waiting for registration of " << IMSI << " on " << gConfig.getStr("SIP.Proxy.Registration");
		success = engine.Register(SIPEngine::SIPRegister); 
	}
	catch(SIPTimeout) {
		LOG(ALERT) "SIP registration timed out.  Is the proxy running at " << gConfig.getStr("SIP.Proxy.Registration");
		// Reject with a "network failure" cause code, 0x11.
		DCCH->send(L3LocationUpdatingReject(0x11));
		// HACK -- wait long enough for a response
		// FIXME -- Why are we doing this?
		sleep(4);
		// Release the channel and return.
		DCCH->send(L3ChannelRelease());
		return;
	}

	if (gConfig.defines("Control.LUR.QueryRRLP")) {
		// Query for RRLP
		RRLPServer wRRLPServer(mobileID, DCCH);
		if (!wRRLPServer.assist()) {
			LOG(INFO) << "RRLPServer::assist problem";
		}
		// can still try to check location even if assist didn't work
		if (!wRRLPServer.locate()) {
			LOG(INFO) << "RRLPServer::locate problem";
		}
	}

	// This allows us to configure Open Registration
	bool openRegistration = gConfig.defines("Control.LUR.OpenRegistration");

	// Query for IMEI?
	if (IMSIAttach && gConfig.defines("Control.LUR.QueryIMEI")) {
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
		if (!gTMSITable.IMEI(IMSI,resp->mobileID().digits()))
			LOG(WARNING) << "failed access to TMSITable";
		delete msg;
	}

	// Query for classmark?
	if (IMSIAttach && gConfig.defines("Control.LUR.QueryClassmark")) {
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

	// We fail closed unless we're configured otherwise
	if (!success && !openRegistration) {
		LOG(INFO) << "registration FAILED: " << mobileID;
		DCCH->send(L3LocationUpdatingReject(gConfig.getNum("Control.LUR.UnprovisionedRejectCause")));
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
	if (preexistingTMSI || !gConfig.defines("Control.LUR.SendTMSIs")) {
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

	// If this is an IMSI attach, send a welcome message.
	if (IMSIAttach) {
		if (success) {
			sendWelcomeMessage( "Control.LUR.NormalRegistration.Message",
				"Control.LUR.NormalRegistration.ShortCode", IMSI, DCCH);
		} else {
			sendWelcomeMessage( "Control.LUR.OpenRegistration.Message",
				"Control.LUR.OpenRegistration.ShortCode", IMSI, DCCH);
		}
	}

	// Release the channel and return.
	DCCH->send(L3ChannelRelease());
	return;
}




// vim: ts=4 sw=4
