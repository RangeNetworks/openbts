/**@file Declarations for common-use control-layer functions. */
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

#define LOG_GROUP LogGroup::Control

#include <stdio.h>
#include <stdlib.h>
#include <Utils.h>
#include <L3Enums.h>
#include "Defines.h"
#include "ControlTransfer.h"
#include "CodecSet.h"

namespace Control {
using namespace GSM;

#define CASENAME(x) case x: return #x;
const char *CodecType2Name(CodecType ct)
{
	switch (ct) {
	CASENAME(GSM_FR)
	CASENAME(GSM_HR)
	CASENAME(GSM_EFR)
	CASENAME(AMR_FR)
	CASENAME(AMR_HR)
	CASENAME(UMTS_AMR)
	CASENAME(UMTS_AMR2)
	CASENAME(TDMA_EFR)
	CASENAME(PDC_EFR)
	CASENAME(AMR_FR_WB)
	CASENAME(UMTS_AMR_WB)
	CASENAME(OHR_AMR)
	CASENAME(OFR_AMR_WB)
	CASENAME(OHR_AMR_WB)
	CASENAME(PCMULAW)
	CASENAME(PCMALAW)
	default: return "CodecType undefined";
	}
}

void CodecSet::text(std::ostream&os) const
{
	if (mCodecs & GSM_FR) os << " GSM_FR";
	if (mCodecs & GSM_HR) os << " GSM_HR";
	if (mCodecs & AMR_FR) os << " AMR_FR";
	if (mCodecs & AMR_HR) os << " AMR_HR";
	if (mCodecs & UMTS_AMR) os << " UMTS_AMR";
	if (mCodecs & UMTS_AMR2) os << " UMTS_AMR2";
	if (mCodecs & TDMA_EFR) os << " TDMA_EFR";
	if (mCodecs & PDC_EFR) os << " PDC_EFR";
	if (mCodecs & AMR_FR_WB) os << " AMR_FR_WB";
	if (mCodecs & UMTS_AMR_WB) os << " UMTS_AMR_WB";
	if (mCodecs & OHR_AMR) os << " OHR_AMR";
	if (mCodecs & OFR_AMR_WB) os << " OFR_AMR";
	if (mCodecs & OHR_AMR_WB) os << " OFR_AMR_WB";
	if (mCodecs & PCMULAW) os << " PCMULAW";
	if (mCodecs & PCMALAW) os << " PCMALAW";
}

std::ostream& operator<<(std::ostream& os, const CodecSet&cs)
{
	cs.text(os);
	return os;
}


const char* CCState::callStateString(CallState state)
{
	switch (state) {
		case NullState: return "null";
		case Paging: return "paging";
		//case AnsweredPaging: return "answered-paging";
		case MOCInitiated: return "MOC-initiated";
		case MOCProceeding: return "MOC-proceeding";
		case MOCDelivered: return "MOC-delivered";
		case MTCConfirmed: return "MTC-confirmed";
		case CallReceived: return "call-received";
		case CallPresent: return "call-present";
		case ConnectIndication: return "connect-indication";
		case Active: return "active";
		case DisconnectIndication: return "disconnect-indication";
		case ReleaseRequest: return "release-request";
		case SMSDelivering: return "SMS-delivery";
		case SMSSubmitting: return "SMS-submission";
		case HandoverInbound: return "HANDOVER Inbound";
		case HandoverProgress: return "HANDOVER Progress";
		case HandoverOutbound: return "HANDOVER Outbound";
		//case BusyReject: return "Busy Reject";	not used
		// Do not add a default case.  Let the compiler whine so we know if we covered all the cases.
		// default: return NULL;
	}
	return "unrecognized CallState";
}

bool CCState::isInCall(CallState state)
{
	switch (state) {
	case Paging:
	//case AnsweredPaging:
	case MOCInitiated:
	case MOCProceeding:
	case MTCConfirmed:
	case CallReceived:
	case CallPresent:
	case ConnectIndication:
	case HandoverInbound:
	case HandoverProgress:
	case HandoverOutbound:
	case Active:
		return true;
	default:
		return false;
	}
}

ostream& operator<<(ostream& os, CallState state)
{
	const char* str = CCState::callStateString(state);
	if (str) os << str;
	else os << "?" << ((int)state) << "?";
	return os;
}

std::ostream& operator<<(std::ostream& os, const TMSI_t&tmsi)
{
	if (tmsi.valid()) {
		char buf[10]; snprintf(buf,sizeof(buf),"0x%x",tmsi.value()); os <<buf;
	} else {
		os <<"(no tmsi)";
	}
	return os;
}

string FullMobileId::fmidUsername() const
{
	if (mImsi.length()) { return string("IMSI") + mImsi; }
	if (mTmsi.valid()) { return format("TMSI%u",mTmsi.value()); }
	if (mImei.length()) { return string("IMEI") + mImei; }
	return string("");
}

// Normally the input is a raw IMSI, ie, not prefixed with "IMSI", but in order to allow
// the output of fmidUsername to be passed in here, check the prefix.
void FullMobileId::fmidSet(string value)
{
	const char *prefix = value.c_str();
	if (strncmp(prefix,"IMSI",4) == 0) {
		mImsi = value.substr(4);
	} else if (strncmp(prefix,"IMEI",4) == 0) {
		mImei = value.substr(4);
	} else if (strncmp(prefix,"TMSI",4) == 0) {
		mTmsi = strtoul(prefix+4,NULL,10);
	} else {
		// Just assume it is an imsi.
		mImsi = value;
	}
}


std::ostream& operator<<(std::ostream& os, const FullMobileId&msid)
{
	os <<LOGVAR2("Imsi",msid.mImsi.length()?msid.mImsi:"\"\"")
		<<LOGVAR2("Tmsi",msid.mTmsi)
		<<LOGVAR2("Imei",msid.mImei.length()?msid.mImei:"\"\"");
	return os;
}


#if UNUSED_BUT_SAVE_FOR_UMTS
GenericL3Msg::~GenericL3Msg()
{
	switch (ml3Type) {
	case MsgTypeLCH: delete ml3frame; break;
	case MsgTypeSIP: delete mSipMsg; break;
	default: assert(0);
	}
}
const char *GenericL3Msg::typeName()
{
	switch (ml3Type) {
	case MsgTypeLCH: return "MsgTypeLCH";
	case MsgTypeSIP: return "MsgTypeSIP";
	default: return "invalid";
	}
}
#endif

string BestNeighbor::text() const
{
	ostringstream ss;
	if (!mValid) {
		ss <<LOGVAR(mValid);
	} else {
		ss <<LOGVAR(mValid) <<LOGVAR(mARFCN) <<LOGVAR(mBSIC) <<LOGVAR(mRxlev) <<LOGVAR(mHandoverCause);
	}
	return ss.str();
}

string NeighborPenalty::text() const
{
	ostringstream ss;
	ss << LOGVAR(mARFCN) <<LOGVAR(mBSIC) <<LOGVAR(mPenaltyTime.remaining());
	return ss.str();
};

std::ostream& operator<<(std::ostream& os, BestNeighbor best) { os <<best.text(); return os; }
std::ostream& operator<<(std::ostream& os, BestNeighbor *best) { os <<best->text(); return os; }
std::ostream& operator<<(std::ostream& os, NeighborPenalty np) { os <<np.text(); return os; }
std::ostream& operator<<(std::ostream& os, NeighborPenalty *np) { os <<np->text(); return os; }

};
