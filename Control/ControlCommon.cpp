/**@file Common-use functions for the control layer. */

/*
* Copyright 2008, 2010 Free Software Foundation, Inc.
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


#include "ControlCommon.h"
#include "TransactionTable.h"

#include <GSMLogicalChannel.h>
#include <GSML3Message.h>
#include <GSML3CCMessages.h>
#include <GSML3RRMessages.h>
#include <GSML3MMMessages.h>
#include <GSMConfig.h>

#include <SIPEngine.h>
#include <SIPInterface.h>

#include <Logger.h>
#undef WARNING


using namespace std;
using namespace GSM;
using namespace Control;





// FIXME -- getMessage should return an L3Frame, not an L3Message.
// This will mean moving all of the parsing into the control layer.
// FIXME -- This needs an adjustable timeout.

L3Message* getMessageCore(LogicalChannel *LCH, unsigned SAPI)
{
	unsigned timeout_ms = LCH->N200() * T200ms;
	L3Frame *rcv = LCH->recv(timeout_ms,SAPI);
	if (rcv==NULL) {
		LOG(NOTICE) << "timeout";
		throw ChannelReadTimeout();
	}
	LOG(DEBUG) << "received " << *rcv;
	Primitive primitive = rcv->primitive();
	if (primitive!=DATA) {
		LOG(NOTICE) << "unexpected primitive " << primitive;
		delete rcv;
		throw UnexpectedPrimitive();
	}
	L3Message *msg = parseL3(*rcv);
	delete rcv;
	if (msg==NULL) {
		LOG(NOTICE) << "unparsed message";
		throw UnsupportedMessage();
	}
	return msg;
}

// FIXME -- getMessage should return an L3Frame, not an L3Message.
// This will mean moving all of the parsing into the control layer.
// FIXME -- This needs an adjustable timeout.

L3Message* Control::getMessage(LogicalChannel *LCH, unsigned SAPI)
{
	L3Message *msg = getMessageCore(LCH,SAPI);
	// Handsets should not be sending us GPRS suspension requests.
	// But if they do, we should ignore them.
	// They should not send more than one in any case, but we need to be
	// ready for whatever crazy behavior they throw at us.
	unsigned count = gConfig.getNum("GSM.Control.GPRSMaxIgnore",5);
	while (count && dynamic_cast<const GSM::L3GPRSSuspensionRequest*>(msg)) {
		LOG(NOTICE) << "ignoring GPRS suspension request";
		msg = getMessageCore(LCH,SAPI);
		count--;
	}
	return msg;
}


/* Resolve a mobile ID to an IMSI and return TMSI if it is assigned. */
unsigned  Control::resolveIMSI(bool sameLAI, L3MobileIdentity& mobileID, LogicalChannel* LCH)
{
	// Returns known or assigned TMSI.
	assert(LCH);
	LOG(DEBUG) << "resolving mobile ID " << mobileID << ", sameLAI: " << sameLAI;

	// IMSI already?  See if there's a TMSI already, too.
	if (mobileID.type()==IMSIType) return gTMSITable.TMSI(mobileID.digits());

	// IMEI?  WTF?!
	// FIXME -- Should send MM Reject, cause 0x60, "invalid mandatory information".
	if (mobileID.type()==IMEIType) throw UnexpectedMessage();

	// Must be a TMSI.
	// Look in the table to see if it's one we assigned.
	unsigned TMSI = mobileID.TMSI();
	char* IMSI = NULL;
	if (sameLAI) IMSI = gTMSITable.IMSI(TMSI);
	if (IMSI) {
		// We assigned this TMSI already; the TMSI/IMSI pair is already in the table.
		mobileID = L3MobileIdentity(IMSI);
		LOG(DEBUG) << "resolving mobile ID (table): " << mobileID;
		free(IMSI);
		return TMSI;
	}
	// Not our TMSI.
	// Phones are not supposed to do this, but many will.
	// If the IMSI's not in the table, ASK for it.
	LCH->send(L3IdentityRequest(IMSIType));
	// FIXME -- This request times out on T3260, 12 sec.  See GSM 04.08 Table 11.2.
	L3Message* msg = getMessage(LCH);
	L3IdentityResponse *resp = dynamic_cast<L3IdentityResponse*>(msg);
	if (!resp) {
		if (msg) delete msg;
		throw UnexpectedMessage();
	}
	mobileID = resp->mobileID();
	LOG(INFO) << resp;
	delete msg;
	LOG(DEBUG) << "resolving mobile ID (requested): " << mobileID;
	// FIXME -- Should send MM Reject, cause 0x60, "invalid mandatory information".
	if (mobileID.type()!=IMSIType) throw UnexpectedMessage();
	// Return 0 to indicate that we have not yet assigned our own TMSI for this phone.
	return 0;
}



/* Resolve a mobile ID to an IMSI. */
void  Control::resolveIMSI(L3MobileIdentity& mobileIdentity, LogicalChannel* LCH)
{
	// Are we done already?
	if (mobileIdentity.type()==IMSIType) return;

	// If we got a TMSI, find the IMSI.
	if (mobileIdentity.type()==TMSIType) {
		char *IMSI = gTMSITable.IMSI(mobileIdentity.TMSI());
		if (IMSI) mobileIdentity = L3MobileIdentity(IMSI);
		free(IMSI);
	}

	// Still no IMSI?  Ask for one.
	if (mobileIdentity.type()!=IMSIType) {
		LOG(NOTICE) << "MOC with no IMSI or valid TMSI.  Reqesting IMSI.";
		LCH->send(L3IdentityRequest(IMSIType));
		// FIXME -- This request times out on T3260, 12 sec.  See GSM 04.08 Table 11.2.
		L3Message* msg = getMessage(LCH);
		L3IdentityResponse *resp = dynamic_cast<L3IdentityResponse*>(msg);
		if (!resp) {
			if (msg) delete msg;
			throw UnexpectedMessage();
		}
		mobileIdentity = resp->mobileID();
		delete msg;
	}

	// Still no IMSI??
	if (mobileIdentity.type()!=IMSIType) {
		// FIXME -- This is quick-and-dirty, not following GSM 04.08 5.
		LOG(WARNING) << "MOC setup with no IMSI";
		// Cause 0x60 "Invalid mandatory information"
		LCH->send(L3CMServiceReject(L3RejectCause(0x60)));
		LCH->send(L3ChannelRelease());
		// The SIP side and transaction record don't exist yet.
		// So we're done.
		return;
	}
}





// vim: ts=4 sw=4
