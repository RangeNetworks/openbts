/**@file RRLPServer */
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

using namespace std;

#include "ControlCommon.h"
#include "RRLPServer.h"

#include <GSMLogicalChannel.h>
#include <GSML3MMMessages.h>

#include <SubscriberRegistry.h>

#include <Logger.h>

using namespace GSM;
using namespace Control;

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
	//if IMEI tagging enabled, check if this IMEI (which is updated elsewhere) has RRLP disabled
	//otherwise just go on
	if (gConfig.defines("Control.LUR.QueryIMEI")){
		//check supported bit
		string supported= gSubscriberRegistry.imsiGet(name, "RRLPSupported");
		if(supported.empty() || supported == "0"){
			LOG(INFO) << "RRLP not supported for " << name;
			trouble = true;
		}
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
		if (response.find("latitude") != response.end() && 
		    response.find("longitude") != response.end() && 
		    response.find("positionError") != response.end()) {
			if (!gSubscriberRegistry.RRLPUpdate(name, response["latitude"], 
							    response["longitude"], 
							    response["positionError"])){
				LOG(INFO) << "SR update problem";
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
			int cause = resp->peekField(16, 8);
			delete resp;
			switch (cause) {
				case 97:
					LOG(INFO) << "MS says: message not implemented";
					//Rejection code only useful if we're gathering IMEIs
					if (gConfig.defines("Control.LUR.QueryIMEI")){
						// flag unsupported in SR so we don't waste time on it again
						if (gSubscriberRegistry.imsiSet(name, "RRLPSupported", "0")) {
							LOG(INFO) << "SR update problem";
						}
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

bool sendRRLP(GSM::L3MobileIdentity mobileID, GSM::LogicalChannel *LCH)
{
	// Query for RRLP
	RRLPServer wRRLPServer(mobileID, LCH);
	if (!wRRLPServer.assist()) {
		LOG(INFO) << "assist problem";
	}
	// can still try to check location even if assist didn't work
	if (!wRRLPServer.locate()) {
		LOG(INFO) << "locate problem";
		return false;
	}
	return true;
}
