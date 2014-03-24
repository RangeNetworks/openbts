/**@file RRLPServer */
/*
* Copyright 2008, 2009, 2010, 2011 Free Software Foundation, Inc.
* Copyright 2011, 2014 Range Networks, Inc.
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
#define LOG_GROUP LogGroup::Control
#include <Timeval.h>

using namespace std;

#include "ControlCommon.h"
#include "L3MMLayer.h"
#include "RRLPServer.h"

#include <GSMLogicalChannel.h>
#include <GSML3MMMessages.h>
#include <GSML3RRMessages.h> // for L3ApplicationInformation

#include <Logger.h>
#include <Reporting.h>

namespace Control {
using namespace GSM;

// (pat) Notes:
// Relevant documents are GSM 3.71 and 4.31.
// LCS [Location Services] includes positioning using TOA [Time of Arrival] or E-OTD or GPS.
// E-OTD Enhanced Observed Time Difference - MS determines postion from synchronized cell beacons.
// MO-LR Mobile Originated Location Request.
// MT-LR Moble Terminated, where LCS client is external to PLMN
// NI-LR Network Induced, where LCS client is within LCS servier, eg, Emergency call Origination.
// NA-EXRD North American Emergency Services Routing Digits, identifies emergency services provider and LCS client, including cell.
// MLC, SMLC, GMLC: [Serving, Gateway] Mobile Location Center - the thing on the network that does this.
// Location Service Request may be LIR [Location Immediate Request] or LDR [Location Deferred Request].
// Location Preparation Procedure:
// NA

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
		string configValue = gConfig.getStr(*p);
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

RRLPServer::RRLPServer(string wSubscriber)
{
	mTrouble = false;
	mURL = gConfig.getStr("GSM.RRLP.SERVER.URL");
	if (mURL.length() == 0) {
		LOG(INFO) << "RRLP not configured";
		mTrouble = true;
		return;
	}
	//mDCCH = wLCH;
	mName = wSubscriber;
	// don't continue if IMSI has a RRLP support flag and it's false
	//if IMEI tagging enabled, check if this IMEI (which is updated elsewhere) has RRLP disabled
	//otherwise just go on
	if (gConfig.getBool("Control.LUR.QueryIMEI")){
		//check supported bit
		// TODO : disabled because of upcoming RRLP changes and need to rip out direct SR access
		//string supported= gSubscriberRegistry.imsiGet(mName, "RRLPSupported");
		//if(supported.empty() || supported == "0"){
		//	LOG(INFO) << "RRLP not supported for " << mName;
		//	mTrouble = true;
		//}
	}
}

// send a location request
void RRLPServer::rrlpSend(L3LogicalChannel *chan)
{
	mQuery = "query=loc";
	// server encodes location request into apdu
	serve();
	if (mAPDUs.size() == 0) {
		LOG(ERR) << "no apdu to send to mobile";
		return;
	}
	if (mAPDUs.size() > 1) {
		// there should only be extra apdus for query=assist
		LOG(ERR) << "ignoring extra apdus for mobile";
	}

	string apdu = mAPDUs[0];
	mAPDUs.clear();
	BitVector2 bv(apdu.size()*4);
	if (!bv.unhex(apdu.c_str())) {
		LOG(INFO) << "BitVector::unhex problem";
		return;
	}
	chan->l3sendm(L3ApplicationInformation(bv));
}

// decode a RRLP response from the mobile
void RRLPServer::rrlpRecv(const GSM::L3Message *resp)
{
	if (!resp) {
		LOG(INFO) << "NULL message";
		return;
	}
	LOG(INFO) << "resp.pd = " << resp->PD();
	if (resp->PD() != GSM::L3RadioResourcePD) {
		LOG(INFO) << "didn't receive an RR message";
		return;
	}
	if (resp->MTI() != L3RRMessage::ApplicationInformation) {
		LOG(INFO) << "received unexpected RR Message " << resp->MTI();
		return;
	}

	// looks like a good APDU
	BitVector2 *bv2 = (BitVector2*)resp;
	BitVector2 bv3 = bv2->tail(32);
	ostringstream os;
	bv3.hex(os);
	string apdu = os.str();
	mQuery = "query=apdu&apdu=" + apdu;
	// server will decode the apdu
	serve();
	if (mResponse.find("latitude") != mResponse.end() && 
		mResponse.find("longitude") != mResponse.end() && 
		mResponse.find("positionError") != mResponse.end()) {
		// TODO : disabled because of upcoming RRLP changes and need to rip out direct SR access
		//if (!gSubscriberRegistry.RRLPUpdate(mName, mResponse["latitude"], 
		//					mResponse["longitude"], 
		//					mResponse["positionError"])){
		//	LOG(INFO) << "SR update problem";
		//}
	}
	// TODO - a failed location request would be a good time to send assistance
	// TODO - an "ack" response means send the next apdu if there is another; otherwise send a location request

}

// encodes or decodes apdus
void RRLPServer::serve()
{
	string esc = "'";
	string config = getConfig();
	if (config.length() == 0) return;
	string cmd = "wget -qO- " + esc + mURL + "?" + mQuery + config + esc;		// (pat) Could use format() here.
	LOG(INFO) << "*************** "  << cmd;
	FILE *result = popen(cmd.c_str(), "r");
	if (!result) {
		LOG(CRIT) << "popen call \"" << cmd << "\" failed";
		return;
	}
	// build map of responses, and list of apdus
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
			mAPDUs.push_back(rhs);
		} else {
			mResponse[lhs] = rhs;
		}
	}
	free(line);
	pclose(result);
	if (mResponse.find("error") != mResponse.end()) {
		LOG(INFO) << "error from server: " << mResponse["error"];
	}
}

// (pat 9-20-2013) Message recommended by doug for RLLP testing.
void sendPirates(L3LogicalChannel *chan)
{
	string bvs =
	"0010010000010000010000000100111000000100000000000000111110000000000000000000000000000"
	"0000000000000000000000000000000000000000000000000000000000000011011000011100101101100"
	"1000000010000000000110111011110111010111010101011110100001110010110100101100100011011"
	"1101010111110001111110111011110101111010100000000110000000010111111001100100001101100"
	"0000101000010000110010010011111101100101011000100010000000011111111111110011001001111"
	"0010000100111011011101000000000100000101001110110001100000000011000111010100000001101"
	"0101011110011000101110001011111101111111101000110000111101110110100111011000000010000"
	"0000000001010001000000000000000000000000000000000000000000000000000000000000000000000"
	"0000000000000000000100011000011100101101100100000001000000000100100100100111000000000"
	"0011100010111100111110101101011010000001101111000101110010111101111110001010100001100"
	"0000001011010000011000000111100100100110000001010100001000011011100011100011111010101"
	"1000100010000000100000000000000010011100111101111111010011010010011111111110111110100"
	"1111011000000101001110011011001101001110100100111100010010011111001101100100111111110"
	"1010010011110110011001100000000100000010001000000000110011100000000000000000000000000"
	"0000000000000000000000000000000000000000000000000000000000000001101011001110010110110"
	"0100000001000000000000001010010111010000000100110001100101111101011011101010110111001"
	"0110111110000111010101000010001011001011010000000110101010110000000001001110010010100"
	"1100110100001000011011010011101100110010101100010001000000001111111111000110001110100"
	"0100101001011101110100011111111111111110100110101000111111001101001000100110010100000"
	"0100010110000111001011101111010100111111110100100101110111001110010000000";
	BitVector2 bv(bvs.c_str());
	chan->l3sendm(L3ApplicationInformation(bv));
	//L3Frame* resp = channel()->l2recv(130000);
	//LOG(ALERT) << "RRLP returned " << *resp;
}

bool sendRRLP(GSM::L3MobileIdentity wMobileID, L3LogicalChannel *wLCH)
{
	// (pat) DEBUG TEST...
	sendPirates(wLCH); return true;

	string sub = wMobileID.digits();
	RRLPServer wRRLPServer(sub);
	if (wRRLPServer.trouble()) return false;
	wRRLPServer.rrlpSend(wLCH);
	return true; // TODO - what to return here?
}

void recvRRLP(MMContext *wLCH, const GSM::L3Message *wMsg)
{
	LOG(DEBUG) <<LOGVAR(wMsg);
	string sub = string("IMSI") + "000000000000000";  // TODO - this will be call to getIMSI (not db one)
	RRLPServer wRRLPServer(sub);
	if (wRRLPServer.trouble()) return;
	wRRLPServer.rrlpRecv(wMsg);
}

};	// namespace
