/**@file GSM Radio Resource procedures, GSM 04.18 and GSM 04.08. */

/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012, 2014 Range Networks, Inc.
*

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.
*/


#define LOG_GROUP LogGroup::Control


#include <stdio.h>
#include <stdlib.h>
#include <list>

#include <Defines.h>
#include "ControlCommon.h"
#include "RadioResource.h"
#include "L3CallControl.h"
#include "L3MMLayer.h"

#include <GSMLogicalChannel.h>
#include <GSMConfig.h>
#include "../GPRS/GPRSExport.h"

#include <NeighborTable.h>
#include <Peering.h>
#include <SIPDialog.h>

#include <Reporting.h>
#include <Logger.h>
#undef WARNING




using namespace std;
using namespace GSM;
using namespace Control;




/**
	Determine the channel type needed.
	This is based on GSM 04.08 9.1.8, Table 9.3 and 9.3a.
	The following is assumed about the global BTS capabilities:
	- We do not support call reestablishment.
	- We do not support GPRS.
	@param RA The request reference from the channel request message.
	@return channel type code, undefined if not a supported service
*/
static
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
static
bool requestingLUR(unsigned RA)
{
	int NECI = gConfig.getNum("GSM.CellSelection.NECI");
	if (NECI==0) return ((RA>>5) == 0x00);
	 else return ((RA>>4) == 0x00);
}






/** Decode RACH bits and send an immediate assignment; may block waiting for a channel for an SOS call. */
static
void AccessGrantResponder(
		unsigned RA, const GSM::Time& when,
		float RSSI, float timingError)
{
	// RR Establishment.
	// Immediate Assignment procedure, "Answer from the Network"
	// GSM 04.08 3.3.1.1.3.
	// Given a request reference, try to allocate a channel
	// and send the assignment to the handset on the CCCH.
	// This GSM's version of medium access control.
	// Papa Legba, open that door...
	// The RA bits are defined in 44.018 9.1.8

	gReports.incr("OpenBTS.GSM.RR.RACH.TA.All",(int)(timingError));
	gReports.incr("OpenBTS.GSM.RR.RACH.RA.All",RA);

	// Are we holding off new allocations?
	if (gBTS.hold()) {
		LOG(NOTICE) << "ignoring RACH due to BTS hold-off";
		return;
	}

	// Check "when" against current clock to see if we're too late.
	// Calculate maximum number of frames of delay.
	// See GSM 04.08 3.3.1.1.2 for the logic here.
	static const unsigned txInteger = gConfig.getNum("GSM.RACH.TxInteger");
	static const int maxAge = GSM::RACHSpreadSlots[txInteger] + GSM::RACHWaitSParam[txInteger];
	// Check burst age.
	int age = gBTS.time() - when;
	ChannelType chtype = decodeChannelNeeded(RA);
	int lur = requestingLUR(RA);
	int gprs = (chtype == PSingleBlock1PhaseType) || (chtype == PSingleBlock2PhaseType); 

	// This is for debugging.
	if (GPRS::GPRSDebug && gprs) {
		Time now = gBTS.time();
		LOG(NOTICE) << "RACH" <<LOGVAR(now) <<LOGVAR(chtype) <<LOGVAR(lur) <<LOGVAR(gprs)
		<<LOGVAR(when)<<LOGVAR(age)<<LOGVAR2("TE",timingError)<<LOGVAR(RSSI)<<LOGHEX(RA);
	}
	LOG(INFO) << "**Incoming Burst**"<<LOGVAR(lur)<<LOGVAR(gprs)
		<<LOGVAR(when)<<LOGVAR(age)<<LOGVAR2("TE",timingError)<<LOGVAR(RSSI)<<LOGHEX(RA);

	//LOG(INFO) << "Incoming Burst: RA=0x" << hex << RA << dec
	//	<<LOGVAR(when) <<LOGVAR(age)
	//	<< " delay=" << timingError <<LOGVAR(chtype);
	if (age>maxAge) {
		LOG(WARNING) << "ignoring RACH bust with age " << age;
		// FIXME -- What was supposed to be happening here?
		//gBTS.growT3122()/1000;		// Hmmm...
		gBTS.growT3122();	// (pat) removed the /1000
		return;
	}

	if (timingError>gConfig.getNum("GSM.MS.TA.Max")) {
		LOG(NOTICE) << "ignoring RACH burst with delay="<<timingError<<LOGVAR(chtype);
		return;
	}

	if (chtype == PSingleBlock1PhaseType || chtype == PSingleBlock2PhaseType) {
		// This is a request for a GPRS TBF.  It will get queued in the GPRS code
		// and handled when the GPRS MAC service loop gets around to it.
		// If GPRS is not enabled or is busy, it may just get dropped.
		GPRS::GPRSProcessRACH(RA,when,RSSI,timingError);
		return;
	}

	// Get an AGCH to send on.
	CCCHLogicalChannel *AGCH = gBTS.getAGCH();
	// Someone had better have created a least one AGCH.
	assert(AGCH);
	// Check AGCH load now.
	// (pat) The default value is 5, so about 1.25 second for a system
	// with a C0T0 beacon with only one CCCH.
	if ((int)AGCH->load()>gConfig.getNum("GSM.CCCH.AGCH.QMax")) {
		LOG(CRIT) << "AGCH congestion";
		return;
	}

	// Check for location update.
	// This gives LUR a lower priority than other services.
	// (pat): LUR = Location Update Request Message
	if (requestingLUR(RA)) {
		// Don't answer this LUR if it will not leave enough channels open for other operations.
		if ((int)gBTS.SDCCHAvailable()<=gConfig.getNum("GSM.Channels.SDCCHReserve")) {
			// (pat) Dont print this alarming message or tell the handsets to go away until we have
			// been up long enough for the channels to clear their initial recyclable state.
			// See L1Decoder::recyclable()
			unsigned startupdelay = (max(T3101ms,max(T3109ms,T3111ms)) +999)/ 1000;
			if (gBTS.uptime() <= (time_t)startupdelay) { return; }
			unsigned waitTime = gBTS.growT3122()/1000;
			LOG(CRIT) << "LUR congestion, RA=" << RA << " T3122=" << waitTime;
			const L3ImmediateAssignmentReject reject(L3RequestReference(RA,when),waitTime);
			LOG(DEBUG) << "LUR rejection, sending " << reject;
			AGCH->l2sendm(reject);
			return;
		}
	}

	// Allocate the channel according to the needed type indicated by RA.
	// The returned channel is already open and ready for the transaction.
	L2LogicalChannel *LCH = NULL;
	switch (chtype) {
		case TCHFType: LCH = gBTS.getTCH(); break;
		case SDCCHType: LCH = gBTS.getSDCCH(); break;
#if 0
        // GSM04.08 sec 3.5.2.1.2
        case PSingleBlock1PhaseType:
        case PSingleBlock2PhaseType:
            {
                L3RRMessage *msg = GPRS::GPRSProcessRACH(chtype,
                        L3RequestReference(RA,when),
                        RSSI,timingError,AGCH);
                if (msg) {
                    AGCH->l2sendm(*msg);
                    delete msg;
                }
                return;
            }
#endif
		// If we don't support the service, assign to an SDCCH and we can reject it in L3.
		case UndefinedCHType:
			LOG(NOTICE) << "RACH burst for unsupported service RA=" << RA;
			LCH = gBTS.getSDCCH();
			break;
		// We should never be here.
		default: assert(0);
	}


	// Nothing available?
	if (!LCH) {
		// Rejection, GSM 04.08 3.3.1.1.3.2.
		{
			unsigned waitTime = gBTS.growT3122()/1000;
			// TODO: If all channels are statically allocated for gprs, dont throw an alert.
			LOG(CRIT) << "congestion, RA=" << RA << " T3122=" << waitTime;
			const L3ImmediateAssignmentReject reject(L3RequestReference(RA,when),waitTime);
			LOG(DEBUG) << "rejection, sending " << reject;
			AGCH->l2sendm(reject);
		}
		return;
	}

	// (pat) gprs todo: Notify GPRS that the MS is getting a voice channel.
	// It may imply abandonment of packet contexts, if the phone does not
	// support DTM (Dual Transfer Mode.)  There may be other housekeeping
	// for DTM phones; haven't looked into it.

	// Set the channel physical parameters from the RACH burst.
	LCH->setPhy(RSSI,timingError,gBTS.clock().systime(when.FN()));
	gReports.incr("OpenBTS.GSM.RR.RACH.TA.Accepted",(int)(timingError));

	// Assignment, GSM 04.08 3.3.1.1.3.1.
	// Create the ImmediateAssignment message.
	// Woot!! We got a channel! Thanks to Legba!
	int initialTA = (int)(timingError + 0.5F);
	if (initialTA<0) initialTA=0;
	if (initialTA>62) initialTA=62;
	const L3ImmediateAssignment assign(
		L3RequestReference(RA,when),
		LCH->channelDescription(),
		L3TimingAdvance(initialTA)
	);
	LOG(INFO) << "sending L3ImmediateAssignment " << assign;
	// (pat) This call appears to block.
	// (david) Not anymore. It got fixed in the trunk while you were working on GPRS.
	// (doug) Adding in a delay to make sure SI5/6 get out before IA.
	// (pat) I believe this sleep is necessary partly because of a delay in starting the SI5/SI6 service loop;
	// see SACCHLogicalChannel::serviceLoop() which sleeps when the channel is inactive.
	// I reduced the delays in both places.
	//sleepFrames(20);
	sleepFrames(4);
	AGCH->l2sendm(assign);

	// On successful allocation, shrink T3122.
	gBTS.shrinkT3122();
}



void* Control::AccessGrantServiceLoop(void*)
{
	while (true) {
		ChannelRequestRecord *req = gBTS.nextChannelRequest();
		if (!req) continue;
		AccessGrantResponder(
			req->RA(), req->frame(),
			req->RSSI(), req->timingError()
		);
		delete req;
	}
	return NULL;
}



GSM::ChannelType NewPagingEntry::getChanType()
{
	if (mWantCS && gConfig.getBool("Control.VEA")) {
		// Very early assignment.
		return GSM::TCHFType;
	} else {
		return GSM::SDCCHType;
	}
}

// This does a lot of mallocing.
L3MobileIdentity NewPagingEntry::getMobileId()
{
	if (mTmsi.valid()) {
		// page by tmsi
		return L3MobileIdentity(mTmsi.value());
	} else {
		// page by imsi
		return L3MobileIdentity(mImsi.c_str());
	}
}

static unsigned newPageAll()
{
	LOG(DEBUG);
	NewPagingList_t pages;
	gMMLayer.mmGetPages(pages,true);		// Blocks until there are some.

	LOG(INFO) << "paging " << pages.size() << " mobile(s)";

	// Page remaining entries, two at a time if possible.
	// These PCH send operations are non-blocking.
	for (NewPagingList_t::iterator lp = pages.begin(); lp != pages.end(); ) {
		// FIXME -- This completely ignores the paging groups.
		// HACK -- So we send every page twice.
		// That will probably mean a different Pager for each subchannel.
		// See GSM 04.08 10.5.2.11 and GSM 05.02 6.5.2.
		const L3MobileIdentity& id1 = lp->getMobileId();
		ChannelType type1 = lp->getChanType();
		++lp;
		if (lp==pages.end()) {
			// Just one ID left?
			LOG(DEBUG) << "paging " << id1;
			gBTS.getPCH(0)->l2sendm(L3PagingRequestType1(id1,type1));
			gBTS.getPCH(0)->l2sendm(L3PagingRequestType1(id1,type1));
			break;
		}
		// Page by pairs when possible.
		const L3MobileIdentity& id2 = lp->getMobileId();
		ChannelType type2 = lp->getChanType();
		++lp;
		LOG(DEBUG) << "paging " << id1 << " and " << id2;
		gBTS.getPCH(0)->l2sendm(L3PagingRequestType1(id1,type1,id2,type2));
		gBTS.getPCH(0)->l2sendm(L3PagingRequestType1(id1,type1,id2,type2));
	}

	return pages.size();
}


size_t Pager::pagingEntryListSize()
{
	NewPagingList_t pages;
	gMMLayer.mmGetPages(pages,false);
	return pages.size();
}

void Pager::start()
{
	if (mRunning) return;
	mRunning=true;
	mPagingThread.start((void* (*)(void*))PagerServiceLoopAdapter, (void*)this);
}



void* Control::PagerServiceLoopAdapter(Pager *pager)
{
	pager->serviceLoop();
	return NULL;
}

void Pager::serviceLoop()
{
	while (mRunning) {
		// Wait for pending activity to clear the channel.
		// This wait is what causes PCH to have lower priority than AGCH.
		// getPCH returns the minimum load PCH.  Each PCH sends one paging message per 51-multiframe.
		if (unsigned load = gBTS.getPCH()->load()) {
			LOG(DEBUG) << "Pager waiting with load " << load;
			sleepFrames(51); // There could be multiple paging channels, in which case this is too long.
			continue;
		}
		newPageAll();
	}
}



// vim: ts=4 sw=4
