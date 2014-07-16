/* 
* Copyright 2014, Range Networks, Inc.
*

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.
*/

#define LOG_GROUP LogGroup::GSM		// Can set Log.Level.GSM for debugging

#include "GSMRadioResource.h"
#include "GSMCommon.h"
#include "GSMConfig.h"
#include "GSMLogicalChannel.h"
#include "GSMCCCH.h"
#include <ControlTransfer.h>
#include <L3TranEntry.h>	// For L3TranEntryId, NewTransactionTable.
#include <Globals.h>

namespace GSM {
using namespace Control;

/**
	Determine the channel type needed.  GSM 04.08 9.1.8, Table 9.3 and 9.3a.
	Assumes we do not support call reestablishment.
	@param RA The request reference from the channel request message.
	@return channel type code, undefined if not a supported service
*/
ChannelType decodeChannelNeeded(unsigned RA)
{
	// This code is based on GSM 04.08 Table 9.9. section 9.1.8

	unsigned RA4 = RA>>4;
	unsigned RA5 = RA>>5;

	// We use VEA for all emergency calls, regardless of configuration.
	if (RA5 == 0x05) return TCHFType;		// emergency call

	// Answer to paging, Table 9.9a.
	// We don't support TCH/H, so it's wither SDCCH or TCH/F.
	// The spec allows for "SDCCH-only" MS.  We won't support that here.
	// FIXME -- So we probably should not use "any channel" in the paging indications.
	if (RA5 == 0x04) return TCHFType;		// any channel or any TCH.
	if (RA4 == 0x01) return SDCCHType;		// SDCCH
	if (RA4 == 0x02) return TCHFType;		// TCH/F
	if (RA4 == 0x03) return TCHFType;		// TCH/F
	if ((RA&0xf8) == 0x78 && RA != 0x7f) return PSingleBlock1PhaseType;
	if ((RA&0xf8) == 0x70) return PSingleBlock2PhaseType;

	int NECI = gConfig.getNum("GSM.CellSelection.NECI");
	if (NECI==0) {
		if (RA5 == 0x07) return SDCCHType;		// MOC or SDCCH procedures
		if (RA5 == 0x00) return SDCCHType;		// location updating
	} else {
		if (gConfig.getBool("Control.VEA")) {
			// Very Early Assignment
			if (RA5 == 0x07) return TCHFType;		// MOC for TCH/F
			if (RA4 == 0x04) return TCHFType;		// MOC, TCH/H sufficient
		} else {
			// Early Assignment
			if (RA5 == 0x07) return SDCCHType;		// MOC for TCH/F
			if (RA4 == 0x04) return SDCCHType;		// MOC, TCH/H sufficient
		}
		if (RA4 == 0x00) return SDCCHType;		// location updating
		if (RA4 == 0x01) return SDCCHType;		// other procedures on SDCCH
	}

	// Anything else falls through to here.
	// We are still ignoring data calls, LMU.
	return UndefinedCHType;
}

/** Return true if RA indicates LUR. */
bool requestingLUR(unsigned RA)
{
	int NECI = gConfig.getNum("GSM.CellSelection.NECI");
	if (NECI==0) return ((RA>>5) == 0x00);
	else return ((RA>>4) == 0x00);
}



// Return a RACH channel request message (what we call RA) for various types of channel requests.
// The RA is only 8 bits.
static unsigned createFakeRachRA(FakeRachType rachtype)
{
	switch (rachtype) {
		default: devassert(0);
		case FakeRachTCH:            return 0xe0 | (0x1f & random());	// top 3 bits are 0x7.
		case FakeRachSDCCH:          return 0x10 | (0x0f & random());	// top 4 bits are 0001
		case FakeRachLUR:            return 0x00 | (0x0f & random());	// top 4 bits are 0000
		case FakeRachGPRS:           return 0x70 | (0x7 & random());	// top 5 bits are 01110.
		case FakeRachAnswerToPaging: return 0x80 | (0x1f & random());	// top 3 bits are 100.
	}
}

FakeRachType fakeRachTypeTranslate(const char *name)
{
	if (strcasestr(name,"tch")) return FakeRachTCH;
	if (strcasestr(name,"sdcch")) return FakeRachSDCCH;
	if (strcasestr(name,"lur")) return FakeRachLUR;
	if (strcasestr(name,"gprs")) return FakeRachGPRS;
	if (strcasestr(name,"pag")) return FakeRachAnswerToPaging;
	LOG(ERR) <<"Unrecognized fake rach type: " <<name;
	return FakeRachTCH;	// Make something up.
}

// Enqueue a fake rach of the specified type.  Used to test/exercise the CCCH code.
void createFakeRach(FakeRachType rachtype)
{
	Time now = gBTS.time();
	AccessGrantResponder(createFakeRachRA(rachtype),now,0,1,0);
	//ChannelRequestRecord *req = new ChannelRequestRecord(createFakeRachRA(rachtype),now,0,1);
	//gBTS.channelRequest(req);
}

std::ostream& operator<<(std::ostream& os, const RachInfo &rach)
{
	unsigned RA = rach.mRA;
	ChannelType chtype = decodeChannelNeeded(RA);
	os <<LOGVAR(chtype) <<LOGVAR2("lur",requestingLUR(RA))
				<<LOGHEX(RA) <<LOGVAR2("TE",rach.initialTA())<<LOGVAR2("RSSI",rach.RSSI()) <<LOGVAR2("when",rach.mWhen) <<LOGVAR(rach.mTN);
	return os;
}

std::ostream& operator<<(std::ostream& os, const RachInfo *rach) { os << &rach; return os; }	// idiotic language


// RR Establishment.  GSM 04.08 3.3.1.1.3.  The RA bits are defined in 44.018 9.1.8
// Triage the RACHes, prioritize them, put them on RachQ.
// TODO: Merge RachInfo with ChannelRequestRecord and pass it in here.
void AccessGrantResponder(
		unsigned RA, const GSM::Time& when,
		float RSSI, float timingError,
		int TN)	// The TN the RACH arrived on.  Only non-0 if there are multiple beacon timeslots.
{
	gReports.incr("OpenBTS.GSM.RR.RACH.TA.All",(int)(timingError));
	gReports.incr("OpenBTS.GSM.RR.RACH.RA.All",RA);

	// Are we holding off new allocations?
	if (gBTS.btsHold()) {
		LOG(NOTICE) << "ignoring RACH due to BTS hold-off";
		return;
	}

	{
		// Screen for delay.
		int MaxTA = gConfig.getNum("GSM.MS.TA.Max");
		if (timingError>MaxTA) {
			RachInfo tmpRACH(RA,when,RadData(RSSI,timingError));	// Temporary just so we can print it more easily.
			LOG(NOTICE) << "ignoring RACH burst TE > "<<MaxTA<<":" <<tmpRACH;
			return;
		}
	}

	int initialTA = (int)(timingError + 0.5F);
	if (initialTA<0) initialTA=0;
	if (initialTA>62) initialTA=62;

	RachInfo *rip = new RachInfo(RA,when,RadData(RSSI,initialTA),TN);
	LOG(DEBUG) << "Incoming RACH:"  <<*rip <<LOGVAR2("now",gBTS.time()) <<LOGVAR2("when%42432",rip->mWhen.FN() % 42432);
	enqueueRach(rip);
}

};
