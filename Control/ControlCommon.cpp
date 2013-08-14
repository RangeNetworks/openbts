/**@file Common-use functions for the control layer. */

/*
* Copyright 2008, 2010 Free Software Foundation, Inc.
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
#include <Sgsn.h>

#include <Logger.h>
#include <Reporting.h>
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
		LOG(NOTICE) << "L3 read timeout";
		throw ChannelReadTimeout();
	}
	LOG(DEBUG) << "received " << *rcv;
	Primitive primitive = rcv->primitive();
	if (primitive!=DATA) {
		LOG(ERR) << "unexpected primitive " << primitive;
		delete rcv;
		throw UnexpectedPrimitive();
	}
	L3Message *msg = parseL3(*rcv);
	if (msg==NULL) {
		LOG(WARNING) << "unparsed message:" << *rcv;
		delete rcv;
		return NULL;
		//throw UnsupportedMessage();
	}
	delete rcv;
	LOG(DEBUG) << *msg;
	return msg;
}


// FIXME -- getMessage should return an L3Frame, not an L3Message.
// This will mean moving all of the parsing into the control layer.
// FIXME -- This needs an adjustable timeout.

L3Message* Control::getMessage(LogicalChannel *LCH, unsigned SAPI)
{
	L3Message *msg = getMessageCore(LCH,SAPI);
	// Handsets should not be sending us GPRS suspension requests when GPRS support is not enabled.
	// But if they do, we should ignore them.
	// They should not send more than one in any case, but we need to be
	// ready for whatever crazy behavior they throw at us.

	// The suspend procedure includes MS<->BSS and BSS<->SGSN messages.
	// GSM44.018 3.4.25 GPRS Suspension Procedure and 9.1.13b: GPRS Suspension Request message.
	// Also 23.060 16.2.1.1 Suspend/Resume procedure general.
	// GSM08.18: Suspend Procedure talks about communication between the BSS and SGSN,
	// and is not applicable to us when using the internal SGSN.
	// Note: When call is finished the RR is supposed to include a GPRS resumption IE, but if it does not,
	// 23.060 16.2.1.1.1 says the MS will do a GPRS RoutingAreaUpdate to get the
	// GPRS service back, so we are not worrying about it.
	// (pat 3-2012) Send the message to the internal SGSN.
	// It returns true if GPRS and the internal SGSN are enabled.
	// If we are using an external SGSN, we could send the GPRS suspend request to the SGSN via the BSSG,
	// but that has no hope of doing anything useful. See ticket #613.
	// First, We are supposed to automatically detect when we should do the Resume procedure.
	// Second: An RA-UPDATE, which gets send to the SGSN, does something to the CC state
	// that I dont understand yet.
	// We dont do any of the above.
	unsigned count = gConfig.getNum("GSM.Control.GPRSMaxIgnore");
	const GSM::L3GPRSSuspensionRequest *srmsg;
	while (count && (srmsg = dynamic_cast<const GSM::L3GPRSSuspensionRequest*>(msg))) {
		if (! SGSN::Sgsn::handleGprsSuspensionRequest(srmsg->mTLLI,srmsg->mRaId)) {
			LOG(NOTICE) << "ignoring GPRS suspension request";
		}
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
	if (mobileID.type()==IMSIType) {
		GPRS::GPRSNotifyGsmActivity(mobileID.digits());
		return gTMSITable.TMSI(mobileID.digits());
	}

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
		GPRS::GPRSNotifyGsmActivity(IMSI);
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
		if (IMSI) {
			GPRS::GPRSNotifyGsmActivity(IMSI);
			mobileIdentity = L3MobileIdentity(IMSI);
		}
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
		if (mobileIdentity.type()==IMSIType) {
			GPRS::GPRSNotifyGsmActivity(mobileIdentity.digits());
		}
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
