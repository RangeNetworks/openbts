/*
* Copyright 2011 Range Networks, Inc.
* All Rights Reserved.
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

#include "MSInfo.h"
#include "TBF.h"
#include "FEC.h"
#include "RLCMessages.h"
#include "BSSG.h"
#include "RLCEngine.h"
#include "Sgsn.h"
#include <string.h>		// for strchr
#include <iomanip>		// For fmtflags, setprecision

namespace GPRS {

   
// This stores the multislot class info that we need from 3GPP 45.002 annex B
// We dont save Ttb or Trb because:
// Trb is 1 or less for all multislot classes >= 2, so we satisfy the constraint
// by leaving one tn timeslot between transmit and receive timeslots.
// Ttb is 1 or less for all multislot classes with Rx > 2.
// Also see 3GPP 45.002 table 6.4.2.2.1
struct MultislotClass {
    uint8_t mMultislotClass;
    uint8_t mMultislotRx;   // Max downlink channels.
    uint8_t mMultislotTx;   // Max uplink channels.
    uint8_t mMultislotSum;  // Max Rx + Tx
};

// The MS uses the idle frames in the 52-multiframe to make measurements.
// I think the Tta and Tra apply to the frames surrounding the idle frame.
// 45.008 10.2.3.2.1 says that if the MS does not have time to perform
// the measurement it can skip a transmit or receive timeslot
// adjacent to the idle frame.

static const int sMultislotMax = 45;	// Table entries numbered 1-45.
static MultislotClass sMultislotInfo[sMultislotMax] = {
	{1, 	1,	1,	2},
	{2, 	2,	1,	3},
	{3, 	2,	2,	3},
	{4, 	3,	1,	4},
	{5, 	2,	2,	4},
	{6, 	3,	2,	4},
	{7, 	3,	3,	4},
	{8, 	4,	1,	5},
	{9, 	3,	2,	5},
	{10,	4,	2,	5},
	{11,	4,	3,	5},
	{12,	4,	4,	5},
	{13,	3,	3,	16},	// These ones with sum=16 are class 2 MS,
	{14,	4,	4,	16},	// ie, send and receive simultaneously.
	{15,	5,	5,	16},
	{16,	6,	6,	16},
	{17,	7,	7,	16},
	{18,	8,	8,	16},
	{19,	6,	2,	16},
	{20,	6,	3,	16},
	{21,	6,	4,	16},
	{22,	6,	4,	16},
	{23,	6,	6,	16},
	{24,	8,	2,	16},
	{25,	8,	3,	16},
	{26,	8,	4,	16},
	{27,	8,	4,	16},
	{28,	8,	6,	16},
	{29,	8,	8,	16},
	{30,	5,	1,	6},
	{31,	5,	2,	6},
	{32,	5,	3,	6},
	{33,	5,	4,	6},
	{34,	5,	5,	6},
	{35,	5,	1,	6},
	{36,	5,	2,	6},
	{37,	5,	3,	6},
	{38,	5,	4,	6},
	{39,	5,	5,	6},
	{40,	6,	1,	7},
	{41,	6,	2,	7},
	{42,	6,	3,	7},
	{43,	6,	4,	7},
	{44,	6,	5,	7},
	{45,	6,	6,	7}
};


// Return an entry from the 45.002 Annex B table.
// We use T(rb) and T(tb) instead of T(ra), T(ta) because the latter are only used
// when there is a requirement for adjacent cell measurements.
// We dont need to worry much about T(rb) and T(tb) because we will
// always just keep Trb >= 1.  This constraint is satisfied by allocating
// adjacent channels when Rx <= 2 or Tx <= 2.
static MultislotClass getMultislotClass(MSInfo *ms)
{
	int multislotclass = ms->sgsnGetMultislotClass(ms->msTlli);
	GPRSLOG(1) << ms <<LOGVAR(multislotclass);
	// -1 means not specified and 0 or >45 are illegal.
	if (multislotclass <= 0 || multislotclass > sMultislotMax) {
		return sMultislotInfo[0];	// No multislot capability.
	} else {
		MultislotClass result = sMultislotInfo[multislotclass-1];
		devassert((int)result.mMultislotClass == multislotclass);
		return result;
	}
}

MSInfo::MSInfo(uint32_t tlli)
	: msDebugId(++Stats.countMSInfo),
	msTlli(tlli),
	msPacch(0),
	//msMode(RROperatingMode::PacketIdle),
	msT3191(gL2MAC.macT3191Value),
	msT3193(gL2MAC.macT3193Value),
	msT3168(gL2MAC.macT3168Value)
	//msTxxxx(5000)		// Needs initialization to prevent abort when we test it,
						// but we will set it again to the real value when we use it.
{
	gReports.incr("GPRS.MSInfo");
	gL2MAC.macAddMS(this);
}

bool MSInfo::msIsSuspended()
{
	SGSN::GmmState::state state = sgsnGetRegistrationState(this->msTlli);
	return state == SGSN::GmmState::GmmRegisteredSuspsended;
}

// Extended dynamic mode means # channels up > # channels down.
bool MSInfo::msCanUseExtendedUplink()
{
	// The Blackberry and iphone set the GeranFeaturePackI bit, but
	// the danged Multitech modems truncate the MS capabilities before
	// the GeranFeaturePackI bit, even though they do support extended uplink TBF
	// They are multislot class 12, so if the MS multislot class > 10, assume ok.
	return gL2MAC.macUplinkPersist > 0 && this->msIsRegistered()
		&& (this->sgsnGetGeranFeaturePackI(this->msTlli) ||
		    this->sgsnGetMultislotClass(this->msTlli) > 10);
}

bool MSInfo::msIsRegistered()
{
	SGSN::GmmState::state state = sgsnGetRegistrationState(this->msTlli);
	return state == SGSN::GmmState::GmmRegisteredNormal ||
		       state == SGSN::GmmState::GmmRegisteredSuspsended;
}

void MSInfo::msCountUSFGrant(bool penalize)
{
	// It does not matter that this is not the exact slot when the USF was granted,
	// because the msLastUsfGrant is used only to compare the same value in other MS.
	msLastUsfGrant = gBSNNext;
	msNumDataUSFGrants++;
	// Note: We are supposed to set N3101 if the uplink USF block
	// is unused, not when we grant it - so add a bit to
	// the N3101 max count to account for this.
	if (penalize) {
		msN3101++;
	}
}


const char *RROperatingMode::name(RROperatingMode::type mode)
{
	switch (mode) {
		CASENAME(PacketIdle)
		CASENAME(PacketTransfer)
		//CASENAME(DualTransfer)
		//CASENAME(Camped)
	}
	return "unrecognized";	// Not reached, but makes gcc happy.
}
std::ostream& operator<<(std::ostream& os, const RROperatingMode::type &mode)
{
	os << RROperatingMode::name(mode);
	return os;
}


void MSInfo::msDelete(bool forever)
{
	gL2MAC.macForgetMS(this,forever); // MS destruction happens in here.
}

// Relinquish any USFs belonging to this MS, if they are no longer in use by any TBFs.
// This function is in the MS, not the TBF, because there could be multiple TBFs
// sharing the same USFs.  (Each TBF has a unique TFI, but share the USF.)
void MSInfo::msCleanUSFs()
{
	bool anyactiveuplinks = false;
	TBF *tbf;
	RN_MS_FOR_ALL_TBF(this,tbf) {
		//if (tbf->mtDir == RLCDir::Up && tbf->mtGetState() != TBFState::Deleting)
		// Update 8-13: Dead TBFs are no longer needed to reserve USFs, but
		// they are still attached, so ignore them for this purpose.
		if (tbf->mtDir == RLCDir::Up && tbf->mtAttached && tbf->mtGetState() != TBFState::Dead) {
			anyactiveuplinks = true;
			break;
		}
	}

	int chcnt = 0;
	int freedusf = 0;
	if (!anyactiveuplinks) {
		// Relinquish any USFs we may have.
		PDCHL1Uplink *up;
		RN_FOR_ALL(PDCHL1UplinkList_t,msPCHUps,up) {
			chcnt++;
			freedusf = up->mchParent->freeUSF(this,false);
		}
		msNumDataUSFGrants = 0;
		msAckNackUSFGrant = -1;
		for (int u = 0; u < USFMAX; u++) { msUSFs[u] = 0; }	// Not used, but be tidy.
	}
	GPRSLOG(1) <<"CleanUSFs"<<this<<LOGVAR(anyactiveuplinks)<<LOGVAR(chcnt)<<LOGVAR(freedusf);
}

// The TBF died, so reserve the USF resources for 5 seconds.
void MSInfo::msFailUSFs()
{
	PDCHL1Uplink *up;
	RN_FOR_ALL(PDCHL1UplinkList_t,msPCHUps,up) {
		up->mchParent->freeUSF(this,true);
	}
}

//void MSInfo::setUSFGrantBSN(int usf)
//{
	//devassert(usf >= USFMIN && usf <= USFMAX);
	//msLastUSFGrantBSN = gBSNNext;
//}


// How many timeslots assigned so far?
//static int chsum(MSInfo *ms)
//{
//	return ms->msPCHDowns.size() + ms->msPCHUps.size();
//}

// The multitech modem supports:
// 	o precendence class (1,2,3,0)
//  o mean throughput, which is not precise enough:
// mean=9 	50,000 (~111 bit/s)
// mean=10 100,000 (~0.22 kbit/s)
// mean=11 200 000(~0.44 kbit/s)
// mean=12 500 000(~1.11 kbit/s)
// mean=13 1,000,000 (~2.2 kbit/s)  <- class 2 shared x 8
// mean=14 2,000,000 (~4.4 kbit/s)  <- class 2 shared x 4
// mean=15 5,000,000 (~11.1 kbit/s)	<- class 2 shared x 2
// mean=16 10,000,000 (~22 kbit/s)	<- class 2 dedicated.
// mean=17 20,000,000 (~44 kbit/s)	<- map to class 3 dedicated
// mean=18 50,000,000 (~111 kbit/s) <- map to class 4 dedicated.  What about 4U,4D?
//
// So that leaves guaranteed and maximum UL/DL values from 1-63Kbps:
// The max UL/DL will determine the class, and guaranteed the sharing factor.
// That doesnt work very well because class 2 and class 4 have the same max UL/DL.
// An sql option can determine how to handle 'best effort', either as the best
// available or a specified class.


// This should always succeed unless there is a logical bug during startup channel allocation.
// 45.008 10.1.1.2 describes what the MS will do if the multislot config does not give
// it sufficient time to take measurements.
// There is a BA(GPRS) list of BCCH carriers to monitor - where?
// 45.008 10.1.1.2: The MS shall perform the measurements during the block
// period where the polling response is sent.
//
// Here are pictures of the multislot assignments we make, the numbers are TN.
// Tr is the number of receive set-up timeslots;
// Tt is the number of transmit set-up timeslots.
// We can use either Tra and Ttb or Trb and Ttb.
// For multislot classes less than 10, Tra < Tta so we must leave the extra timeslots
// between receive and transmit to utilize the maximum number of slots.
// We are trying to avoid moving PACCH; in the pictures below TN1 is the PACCH.
// GSM uplink trails downlink separated by two empty slots.
// Normal single channel, Tr = 2, Tt = 4
// 1D | 2  | 3  | 4  | 5  | 6  | 7  | 8
// 6  | 7  | 8  | 1U | 2  | 3  | 4  | 5
// 2-down/2-up, Tr = 1, Tt = 3:  Uses TN1,2 PACCH on TN1 or 2, chs: P+1 or P-1
// 1D | 2D | 3  | 4  | 5  | 6  | 7  | 8
// 6  | 7  | 8  | 1U | 2U | 3  | 4  | 5
// 3-down/2-up, Tr = 1, Tt = 2:  Uses TN8,1,2 PACCH on TN1 or 2, chs: P+1&-1 or P-1&-2
// 1D | 2D | 3  | 4  | 5  | 6  | 7  | 8D
// 6  | 7  | 8  | 1U | 2U | 3  | 4  | 5
// 4-down/1-up, Tr = 1, Tt = 2:  Uses TN7,8,1,2 PACCH on TN1, chs: P+1&-1&-2
// 1D | 2D | 3  | 4  | 5  | 6  | 7D | 8D
// 6  | 7  | 8  | 1U | 2  | 3  | 4  | 5

// The multislot class 12 and higher support 1-down/4-up, and they also have
// Tta = Tra so we can put the extra empty slots on either side, but that doesnt help.
// 3GPP 45.002 table 6.4.2.2.1 specifies that Tra applies in these cases,
// so we dont have a choice.
// For extended dynamic uplink the lowest numbered uplink must be the downlink.
// Note: There is something called "Shifted USF" but it only applies to super
// high multislot classes.
// 2-down/3-up, uses TN1,2,3 PACCH on TN1 or 2, chs: P+1&+2 or P+1&-1.
// 1D | 2D | 3  | 4  | 5  | 6  | 7  | 8
// 6  | 7  | 8  | 1U | 2U | 3U | 4  | 5
// 1-down/4-up, Tra=2, Ttb=1 uses TN1,2,3,4, ie P+1&P+2&P+3
// 1D | 2  | 3  | 4  | 5  | 6  | 7  | 8
// 6  | 7  | 8  | 1U | 2U | 3U | 4U | 5


// The following are illegal because for extended dynamic uplink
// the lowest numbered uplink must be the downlink.
// Also, 3GPP 45.002 table 6.4.2.2.1 specifies that Tra applies in these cases, not Tta.
// 2-down/3-up, Tr = 1, Tt = 2:  Uses TN8,1,2 PACCH on TN1 or 8, chs: P+1&-1 or P+1&+2
// 1D | 2  | 3  | 4  | 5  | 6  | 7  | 8D
// 6  | 7  | 8U | 1U | 2U | 3  | 4  | 5
// 1-down/4-up, Tr=1 Tt=2 uses TN8,1,2,3 PACCH on TN1, chs: P-1&P+1&P+2
// 1D | 2  | 3  | 4  | 5  | 6  | 7  | 8
// 6  | 7  | 8U | 1U | 2U | 3U | 4  | 5

// Here are Tt=1 alternatives:
// Aternative 4-down/1-up for class 12, Tr = 2, Tt = 1:  Uses TN6,7,8,1 PACCH on TN1
// chs: P-1&P-2&P-3
// 1D | 2  | 3  | 4  | 5  | 6D | 7D | 8D
// 6  | 7  | 8  | 1U | 2  | 3  | 4  | 5
// So we can do 3-down/2-up and 2-down/3-up on 3 adjacent channels,
// but if we want to do 4-down/1-up and 1-down/4-up without moving PACCH
// we need 6 adjacent channels.
//
// GPRS Classes of service.
// Full bandwidth full duplex (4D/1U to 1D/4U), requires 5xTN or 2 per ARFCN.
// Full bandwidth assymetric only takes 4 adjacent TN, ex: 3D/1U to 1D/4U
// These may be shared or not.  Strategically we may move the PACCHs around later.
// The more typical 2D/2U and uses PACCH on every other ARFCN, has a guaranteed
// bandwidth of 2x because even though the neighbor may encroach on it, it can
// also encroach on its neighbor.
// How about Guaranteed Bandwidth and Minimum Bandwidth in up/down.
// The ones that make sense, assuming 1 channel = 12Kpbs, are:
// class 5 adjacent TN: 48D/48U half-duplex,
// class 4 adjacent TN: 48D/36U, 36D/48U half-duplex,  <- must specify which
// Or we could call the above class 4, 4D and 4U.
// class 3 adjacent TN: 36D/36U half-duplex, no need to specify, can dynamically
//				change between 36D/24U or 24D/36U full-duplex
// class 2 adjacent TN: 24D/24U full-duplex
// class 1, uses same as above but shared: 12U/12U.
// If you allocate 4 TNs on an ARFCN, if they are both class 2, then the bandwidth
// of the upper one can borrow from the lower one only when it is not busy,
// otherwise they share as follows:
//		one shared D TN:  18D/24U  and 30D/24U
//		two shared D TN:  12D/24U  and 36D/24U
//
// The sql options can specify reserved allocation of any number of classes, 
// for example: '2x4,3x3,1x2'.
// For dynamic allocation, it could be based on one or more of the following:
//		the QoS from the modem;
//		the class to allocate, or maybe max and min classes.  (default class 2)
//		or maybe the only thing we support for dynamic is class 2.
//		total number of dynamic gprs channels to allocate;
//		number of voice TCH to leave open;
//		
// We can let the multitech modem specify the QoS.


// Until we support per-imsi bandwidth, it would be first-come first serve, or we
// could have the customer program the QoS in the multitech modem.
// If more phones register than will fit, we need to know if we are allowed to share,
// so we really need the sharing number, too.
// Or we could just use the priority: 
// Allowing multiple classes makes no sense at all until we support per-imsi bandwidth,
// so for now I guess it is just a single static and a single dynamic class:
// StaticAllocation '2x4', DynamicAllocationClass.Max '4' DynamicAllocationClass.Min '2'
// Or we could specify the bandwidth, for example: 2x48D/48U or 4x24D/24U
// 

const unsigned cTnPerArfcn = 8;
static unsigned addTn(unsigned tn, int offset)
{
	return (unsigned) (tn + offset) % cTnPerArfcn;
}
static bool tnavail(unsigned chmask,unsigned tn)
{
	return chmask & (1 << (tn % cTnPerArfcn));
}

// The tnlist describes the tn slots used in this multislot configuration.
bool MSInfo::msAddCh(unsigned chmask, const char *tnlist)
{
	const char *cp, *pp = strchr(tnlist,'P');
	if (!pp) {
		devassert(pp);	// Caller goofed.  tnlist must include a 'P' for PACCH location.
		return false;	// And thats a failure.
	}
	int before = pp-tnlist;					// Number of channels needed before PACCH.
	// Check for channel availability.
	unsigned tn, tnfirst = addTn(msPacch->TN(),- before);
#if 0
	unsigned tn = msPacch->TN();
	int after = strlen(tnlist)-before-1;	// Number of channels needed after PACCH.
	if (before && !tnavail(chmask,tn-1)) {return false;}
	if (before>1 && !tnavail(chmask,tn-2)) {return false;}
	if (after && !tnavail(chmask,tn+1)) {return false;}
	if (after>1 && !tnavail(chmask,tn+2)) {return false;}
#endif
	GPRSLOG(4) << "msAddCh" <<LOGVAR(tnlist)<<LOGVAR(before)<<LOGVAR(tnfirst);
	for (tn=tnfirst,cp=tnlist; *cp; cp++, tn=addTn(tn,1)) {
		if (*cp == 'P') { continue; }	// PACCH already allocated, so it wont look avail.
		if (!tnavail(chmask,tn)) {return false;}
	}

	// Allocate the channels.
	tn = addTn(msPacch->TN(),- before);
	for (tn=tnfirst,cp=tnlist; *cp; cp++, tn=addTn(tn,1)) {
		GPRSLOG(4) << "msAddCh loop" <<LOGVAR(tnlist)<<LOGVAR(tnfirst)<<LOGVAR(*cp)<<LOGVAR(tn);
		PDCHL1FEC *ch = gL2MAC.macFindChannel(msPacch->ARFCN(),tn);
		switch (*cp) {
		case 'D':	// downlink only timeslot.
			msPCHDowns.push_back(ch->downlink());
			break;
		case 'U':	// uplink only timeslot.
			msPCHUps.push_back(ch->uplink());
			break;
		case 'B':	// bidirection timeslot other than PACCH.
			msPCHDowns.push_back(ch->downlink());
			msPCHUps.push_back(ch->uplink());
			break;
		case 'P':	// PACCH, already added so skip.
			break;
		}
	}
	return true; // finished
}

// Try for a specific number of down and up timeslots.
// If there are multiple ways to satisfy the request, try them all.
// The PACCH has already been added to the channels.
// The letters correspond to timeslots and mean:
// D: downlink, U=uplink, B=bidir, P=PACCH (which is also bidir.)
// Any bidir timeslot could be the PACCH.
bool MSInfo::msTrySlots(unsigned chmask,int down,int up)
{
	// For extended dynamic uplink (ie, if number uplink channels > number downlink)
	// then the first channel must be bidirectional, and the USF of the first
	// channel allocates the uplink for all TNs.
	switch ((down<<4)+up) {
	case 0x41: return msAddCh(chmask,"DDPD");
	case 0x14: return msAddCh(chmask,"PUUU");
	case 0x32: return msAddCh(chmask,"DPB") || msAddCh(chmask,"DBP");
	case 0x23: return msAddCh(chmask,"PBU") || msAddCh(chmask,"BPU");
	case 0x22: return msAddCh(chmask,"PB") || msAddCh(chmask,"BP");
	// These are oddballs that would only be used for testing:
	case 0x31: return msAddCh(chmask,"DPD") || msAddCh(chmask,"DDP");
	case 0x13: return msAddCh(chmask,"PUU");
	case 0x21: return msAddCh(chmask,"PD") || msAddCh(chmask,"DP");
	case 0x12: return msAddCh(chmask,"PU");
	default: devassert(0); return false;
	}
}

// Assign channels for the requested timeslots.
// If the multislot request cannot be satisfied, try others.
bool MSInfo::msAssignChannels2(int maxdown, int maxup, int sum)
{
	PDCHL1FEC *pdch1;
	if (msPacch) {
		pdch1 = msPacch;
	} else {	// Does this happen?
		pdch1 = gL2MAC.macPickChannel();
	}
	if (pdch1 == NULL) {
		GPRSLOG(1) << "msAssignChannels failed" <<this;
		return false;
	}

	msPCHUps.push_back(pdch1->uplink());
	msPCHDowns.push_back(pdch1->downlink());
	msPacch = pdch1;

#if 0
	if (maxdown <= 1 && maxup <= 1) { return; }

	// Look for adjacent channels before (minus) and after (plus) our PACCH tn.
	// We look up to two channels in both directions.
	int plus = 0, minus = 0;
	PDCHL1FEC *cplus[2]; cplus[0] = cplus[1] = 0;
	PDCHL1FEC *cminus[2]; cminus[0] = cminus[1] = 0;

	unsigned tn = pdch1->TN();
	for (plus = 0; plus < 2; plus++) {
		cplus[plus] = gL2MAC.macFindChannel(pdch1->ARFCN(),(unsigned)(tn+plus)%8);
		if (!cplus[plus]) break;
	}
	for (minus = 0; minus < 2; minus++) {
		cminus[minus] = gL2MAC.macFindChannel(pdch1->ARFCN(),(unsigned)(tn-minus)%8);
		if (!cminus[minus]) break;
	}
#endif

	unsigned mask = gL2MAC.macFindChannels(pdch1->ARFCN());
	GPRSLOG(2)<<format("AssignChannels3, down/up=%d/%d sum=%d mask=0x%x",
		maxdown,maxup,sum,mask);

	// Try to match the available channels to the request.
	// If we cannot fulfill the request, try for something else.
	// If the up and down specs are incompatible we give the down priority.
	// If a 4x1 or 1x4 request cannot be satisfied, should we go ahead
	// and do 3x2 or 2x3 instead of 3x1 or 1x3?
	// The way the user does that is ask for 4x2 or 2x4, and they'll get
	// 4x1 or 1x4 if avail, else 3x2 or 2x3.
	int up21 = maxup >= 2 ? 2 : 1;
	int down21 = maxdown >= 2 ? 2 : 1;
	if (maxdown >= 4) {
		msTrySlots(mask,4,1) || msTrySlots(mask,3,up21) || msTrySlots(mask,2,up21);
	} else if (maxup >= 4) {
		msTrySlots(mask,1,4) || msTrySlots(mask,down21,3) || msTrySlots(mask,down21,2);
	} else if (maxdown == 3) {
		msTrySlots(mask,3,up21) || msTrySlots(mask,2,up21);
	} else if (maxup == 3) {
		msTrySlots(mask,down21,3) || msTrySlots(mask,down21,2);
	} else if (maxdown == 2) {
		// The sum test is only needed for multislot class 3,
		// which is the only class that can not do 2-down/2-up.
		if (sum == 3) up21 = 1;
		msTrySlots(mask,2,up21);
	} else if (maxup == 2) {
		if (sum == 3) down21 = 1;
		msTrySlots(mask,down21,2);
	}

	msPCHUps.sort(chCompareFunc);
	msPCHDowns.sort(chCompareFunc);

#if 0
	if (pdch2) {
		if (maxdown > 1) { msPCHDowns.push_back(pdch2->downlink()); }

		// Do we want to add a third or fourth channel?  They will be downlink only.
		// We can satisfy multislot constraints (Tra=1 and Ttb=2) using
		// either or both of the timeslots immediately prior to the two
		// channels already allocated, but note that these ones cannot be PACCH.
		if ((int)msPCHDowns.size() < maxdown && chsum(this) < slots.mMultislotSum) {
			int tn2 = pdch2->TN();
			int tn = tn1<tn2 ? tn1 : tn2;	// tn is the lower of the two ch assigned.
			if (--tn < 0) { tn = 7; }
			PDCHL1FEC *pdch3 = gL2MAC.macFindChannel(pdch1->ARFCN(),tn);
			if (pdch3) {
				msPCHDowns.push_back(pdch3->downlink());

				// Can we do a 4-down config?
				// In that case we will sacrifice an uplink TN.
				if ((int)msPCHDowns.size() < maxdown && 5 <= slots.mMultislotSum) {
					if (--tn < 0) { tn = 7; }
					PDCHL1FEC *pdch4 = gL2MAC.macFindChannel(pdch1->ARFCN(),tn);
					if (pdch4) {
						msPCHDowns.push_back(pdch4->downlink());
						maxup = 1;	// Limit to one channel up now.
					}
				}
			}
		}

		// The chsum test is only needed for multislot class 3,
		// which is the only class that can not do 2-down/2-up.
		if (maxup > 1 && chsum(this) < slots.mMultislotSum) {
			msPCHUps.push_back(pdch2->uplink());
		}
	} // have pdch2.
#endif
	return true;
}

bool MSInfo::msAssignChannels()
{
	// Get the channels we will use...
	// For now, just use the one the request came in on.
	// It would be better to make sure we pick a channel that has free USFs,
	// but I'm not going to worry about it.
	if (msPCHDowns.size() == 0) {
		devassert(msPCHUps.size() == 0);

		// Check for multislot.  Limit to sql options.
		// User can control via sql: Multislot.Max, which can be over-ridden
		// by: Multislot.Max.Uplink, Multislot.Max.Downlink.
		int maxdown=1, maxup = 1;
		maxdown = configGprsMultislotMaxDownlink();
		maxup = configGprsMultislotMaxUplink();
		// Defend against garbage input:
		if (maxdown < 1) {maxdown = 1;}
		if (maxup < 1) {maxup = 1;}

		MultislotClass slots;
		if (maxdown > 1 || maxup > 1) {
			// Check the multislot class of the phone:
			slots = getMultislotClass(this);

			// Limit to phone capabilities.
			if (maxdown > (int)slots.mMultislotRx) { maxdown = slots.mMultislotRx; }
			if (maxup > (int)slots.mMultislotTx) { maxup = slots.mMultislotTx; }
			GPRSLOG(1)<<format("Multislot mscap=%d/%d max,down/up=%d/%d",
				slots.mMultislotRx,slots.mMultislotTx,maxdown,maxup);

			// Dont bother with this test.  Would only occur if you set
			// maxup < maxdown on a multislot class phone, and so far all of
			// them support extended dynamic.  The user should never set the
			// config options this way except for multiclass 12, and it has to support this.
			//if (maxup > maxdown && ! msCanUseExtendedDynamic()) {
			//	maxdown = maxup;
			//}

			// Update: This test moved into msAssignChannels2
			// This test is only needed for multislot class 3,
			// which is the only class that can not do 2-down/2-up,
			// and needs to be converted to 2-down/1-up,
			// but we'll go ahead and do an exhaustive check.
			//while (maxdown + maxup > slots.mMultislotSum) {
			//	if (maxup > 1) {
			//		maxup--;
			//	} else if (maxdown > 1) {
			//		maxdown--;
			//	} else {
			//		break;		// This is impossible, but be safe.
			//	}
			//}
		}

		msAssignChannels2(maxdown,maxup,slots.mMultislotSum);

		LOGWATCHF("Channel Assign, max:down/up=%d/%d ch down/up=%d/%d\n",
			maxdown,maxup,msPCHDowns.size(),msPCHUps.size());

        // If we are multislot, log a message:
        if (msPCHDowns.size() > 1) {
            std::ostringstream os;
            msDumpChannels(os);
            LOG(INFO) << "Multislot assignment for "<<this<<os;
        }

	} else {
		devassert(msPCHUps.size() != 0);
	}
	return true;
}

void MSInfo::msDeassignChannels()
{
	TBF *tbf;
	RN_MS_FOR_ALL_TBF(this,tbf) {
		if (tbf->isTransmitting()) {
			GLOG(ERR) << "DeassignChannels while TBF transmitting:"<<tbf;
			// The caller was suppsed to prevent this.
			// No recovery is possible; just kill it.
			tbf->mtCancel(MSStopCause::Goof,TbfNoRetry);
		}
	}
	// We dont call this if there are any active TBFs, but
	// there could be attached TBFs that have not started yet.
	// We must de-attach them to release the channels.
	msPCHDowns.clear();
	msPCHUps.clear();
}

// TODO:
void MSInfo::msReassignChannels()
{
	msDeassignChannels();
	// TODO?
	// tbf->mtDeReattach();
}

unsigned MSInfo::msGetDownlinkQueuedBytes()
{
	// Figure out how many bytes stacked up in the queue for this MS.
	unsigned nbytes = 0;
	TBF *tbf;
	RN_MS_FOR_ALL_TBF(this,tbf) {
		if (tbf->mtDir != RLCDir::Down) continue;
		nbytes += tbf->engineDownPDUSize();
	}
	return nbytes;
}

// The MS is in PacketTransfer mode if any TBFs are currently running, stalled or not.
RROperatingMode::type MSInfo::getRROperatingMode()
{
	if (msCountTBF2(RLCDir::Either,TbfMTransmitting,NULL)) {
		return RROperatingMode::PacketTransfer;
	} else {
		return RROperatingMode::PacketIdle;
	}
}


// Count how many TBFs exist in the specified direction (which may be Either)
// are in the specified TbfMacroState.
// Normally there will only be one TBF.
// We also have to prevent starting a bunch of redundant nearly identical
// TBFs when the phone sends us a bunch of RACHes in a row.
int MSInfo::msCountTBF1(RLCDir::type dir, TbfMacroState tbfmstate, TBF**ptbf) const
{
	int count = 0;
	TBF *tbf;
	RN_MS_FOR_ALL_TBF(this,tbf) {
		if (dir == RLCDir::Either || tbf->mtDir == dir) {
			if (tbfmstate == TbfMActive && !tbf->isActive()) continue;
			if (tbfmstate == TbfMTransmitting && !tbf->isTransmitting()) continue;
			// We always ignore tbfs that are in the process of being deleted.
			if (tbf->mtGetState() == TBFState::Deleting) continue;
			if (tbf->mtGetState() == TBFState::Unused) continue;	// shouldnt happen
			if (ptbf) { *ptbf = tbf; }
			count++;
		}
	}
	return count;
}

// Count TBFs for this real MS, which means all MSInfo belonging to the real MS.
int MSInfo::msCountTBF2(RLCDir::type dir, TbfMacroState tbfmstate, TBF**ptbf)
{
	int count = msCountTBF1(dir,tbfmstate,ptbf);
	if (msAltTlli) {
		MSInfo *ms2 = gL2MAC.macFindMSByTlli(msAltTlli,false);
		if (ms2 == NULL) {
			// The old MSInfo expired naturally, and we will never have to worry about it again.
			msAltTlli = 0;
		} else {
			count += ms2->msCountTBF1(dir,tbfmstate,ptbf);
		}
	}
	return count;
}

int MSInfo::msCountActiveTBF(RLCDir::type dir, TBF**ptbf)
{
	return msCountTBF2(dir,TbfMActive,ptbf);
}

int MSInfo::msCountTransmittingTBF(RLCDir::type dir, TBF**ptbf)
{
	return msCountTBF2(dir,TbfMTransmitting,ptbf);
}

// Downlink time slots as defined by GSM04.60 12.18
// And I quote: Bit 8 indicates the status of timeslot 0,
// bit 7 indicates the status of timeslot 1, etc.
// Note that TN() runs 0..7 (see Time::incTN())
unsigned char MSInfo::msGetDownlinkTimeslots(MultislotSymmetry sym)
{
	unsigned char result = 0;
	//if (sym == MultislotSymmetric && msPCHUps.size() < msPCHDowns.size()) {
	//	// If the uplink and downlink tn size are  assymetric,
	//	// use the smaller array, which is always valid in both directions.
	//	PDCHL1Uplink *up;
	//	RN_FOR_ALL(PDCHL1UplinkList_t,msPCHUps,up) {
	//		result |= (1 << (7 - up->TN()));
	//	}
	//} else {
		PDCHL1Downlink *down;
		RN_FOR_ALL(PDCHL1DownlinkList_t,msPCHDowns,down) {
			result |= (1 << (7 - down->TN()));
		}
	//}
	return result;
}


//std::ostream& operator<<(std::ostream& os, const Statistic<ChannelCodingType> &stat) { stat.text(os); return os; }

void SignalQuality::dumpSignalQuality(std::ostream&os) const
{
	ios_base::fmtflags savedfoobarflags = os.flags();
	os.precision(2);
	os << "\t" << fixed;
	os << LOGVAR2("TimingError",msTimingError);
	os << LOGVAR2("RSSI",msRSSI);
	os << LOGVAR2("CV",msCValue);
	os << LOGVAR2("ILev",msILevel);
	os << LOGVAR2("RXQual",msRXQual);
	os << LOGVAR2("SigVar",msSigVar);
	os << LOGVAR2("ChCoding",msChannelCoding);
	os.flags(savedfoobarflags);		// What were these guys thinking?

	//ChannelCodingType ccup = msGetChannelCoding(RLCDir::Up);
	//ChannelCodingType ccdown = msGetChannelCoding(RLCDir::Down);
	//int cc = min((int)ccup,(int)ccdown);
	//if (ccup == ccdown) {
	//	os << LOGVAR2("ChannelCoding",(int)ccup);
	//} else {
	//	os << format(" ChannelCoding=%dup/%ddown",ccup,ccdown);
	//}
	os << "\n";
}


void SignalQuality::setRadData(RadData &rd)
{
	msRSSI.addPoint((int)rd.mRSSI);
	msTimingError.addPoint(rd.mTimingError);
}

void SignalQuality::setRadData(float wRSSI,float wTimingError)
{
	msRSSI.addPoint((int)wRSSI);
	msTimingError.addPoint(wTimingError);
}

// Determine whether we should use slow or fast channel coding for the specified direction.
ChannelCodingType MSInfo::msGetChannelCoding(RLCDirType wdir) const
{
	// Initial channel coding is determined from RSSI from most recent burst from MS.
	// If the signal strength was low (less than -40db) then use the slow speed.
	// TODO: For subsequent TBFs we should use statistics from previous TBFs.
	// BEGINCONFIG
	// 'GPRS.ChannelCodingControl.RSSI',-40,0,0,'If the initial signal strength is less than this amount in DB GPRS uses a lower bandwidth but more robust encoding CS-1'
	// ENDCONFIG

	// Allow user full control over the codecs with these options:
	const char *option = (wdir == RLCDir::Up) ? "GPRS.Codecs.Uplink" : "GPRS.Codecs.Downlink";
	const char *codecs = gConfig.getStr(option).c_str();
	// We only support CS1 and CS4.
	bool cs1allowed = strchr(codecs,'1');
	bool cs4allowed = strchr(codecs,'4');
	if (cs1allowed && cs4allowed) {
		// Choose codec based on initial signal strength:
		int fastRSSI = gConfig.getNum("GPRS.ChannelCodingControl.RSSI");
		return (msRSSI.getCurrent() < fastRSSI) ? ChannelCodingCS1 : ChannelCodingCS4;
	} else if (cs4allowed) {
		return ChannelCodingCS4;
	} else {
		return ChannelCodingCS1;
	}
}

// UNUSED
// Not a MSInfo member function, but still related to MSInfo.
// This function is (was) used to implement CHANGE-TLLI from the BSSG interface.
// 3-2012: (pat) removed as procedurally incorrect.  See notes at MSInfo struct.
// We need to keep a separate MSInfo for each TLLI.
// We might want to move some of the running timer info from the MSInfo for the old-TLLI
// to the MSInfo for the new-TLLI, but probably not even that because at the time this
// happens, the MSInfo for the new-TLLI will be the most recently used one, because
// we just received the attach-accept message.
// 6-2012: (pat) Changed my mind and replaced with msChangeTlli, msAliasTlli.
MSInfo *bssgMSChangeTLLI(uint32_t oldTLLI,uint32_t newTLLI)
{
    MSInfo *ms = NULL;
    GPRSLOG(1) << "MSChangeTLLI"<<LOGHEX(oldTLLI)<<LOGHEX(newTLLI);
	// (pat) 6-16-2012: I modified this code without testing,
	// since we longer use the BSSG interface.
    if ((ms = gL2MAC.macFindMSByTlli(newTLLI, false))) {
		ms->msChangeTlli(newTLLI);
	} else if ((ms = gL2MAC.macFindMSByTlli(oldTLLI, false))) {
		ms->msAliasTlli(newTLLI);
		ms->msChangeTlli(newTLLI);
	}
    //if ((ms = gL2MAC.macFindMSByTlli(oldTLLI, false))) {
    //    if (oldTLLI == ms->msTLLI) {
    //        ms->msSetTLLI(newTLLI);
    //    }
    //}
    return ms;
}


// This code is used with the integrated SGSN.
// The MS is identified by multilple TLLIs.
// The sgsn places the current tlli to be used in downlink in each message.
// This function makes sure that newTlli is the current one.
void MSInfo::msChangeTlli(uint32_t newTlli)
{
	if (! tlliEq(newTlli,msTlli)) {
		// If the message is in the queue for this MS, the MS must have been
		// identified by either msTlli or msOldTlli.
		devassert(tlliEq(newTlli,msOldTlli));
		MSInfo *ms2 = gL2MAC.macFindMSByTlli(newTlli,false);
		if (ms2 != this) {
			// This is kind of a serious problem.
			// It would only happen if the MS just happens to use a TLLI that is assigned by the SGSN.
			LOG(ERR) << "Changing TLLI of"<<this<<" from"<<LOGHEX(msTlli)<< " to"<<LOGHEX(newTlli)<<" MS already exists:"<<ms2;
			ms2->msDelete(false);
		}

		msOldTlli = msTlli;
		msDeprecated = false;
		if (msAltTlli) {
			MSInfo *ms3 = gL2MAC.macFindMSByTlli(msAltTlli,false);
			if (ms3 && ms3 != this) { msDeprecated = true; }
		}
	}
	// The newTlli may differ by the TLLI_LOCAL_BIT, so always set msTlli.
	msTlli = newTlli;
}

// In addition to alias tllis, we also accept foreign TLLIs, see macFindMSByTlli.
void MSInfo::msAliasTlli(uint32_t otherTlli)
{
	if (otherTlli == 0) return;
	if (!tlliEq(otherTlli,msTlli) && !tlliEq(otherTlli, msOldTlli)) {
		MSInfo *ms2 = gL2MAC.macFindMSByTlli(otherTlli,false);
		if (ms2) {
			devassert(ms2 != this);
			// Set the AltTlli so these two MSInfo structs reference each other,
			// since they are the same MS, and wont try to start simultaneous TBFs.
			// Conceivably there could be more than just two TLLIs, but I think it
			// is ok because the MS uses them serially and it will all just work out.
			// IMPORTANT:  The alt tlli is NOT the old tlli.
			// See the comments at MSinfo
			ms2->msAltTlli = this->msTlli;
			this->msAltTlli = ms2->msTlli;
		} else {
			// This code is processed by MAC when this message is first seen.
			// Set oldTlli so that we will know the TLLI is the same MS.
			// This may be switched by msChangeTlli when the message is processed.
			this->msOldTlli = otherTlli;
		}
	}
}

// Return index in data history arrays arrays, which is the current 48-block-multiframe
unsigned StatHits::histind()
{
	// There are approx 48 blocks per second.
	unsigned now = (gBSNNext / 48);	// Current 48-block-multiframe, modulo the hyperframe.
	unsigned i = now % cNumHist;
	if (now != mWhen[i]) {
		// Saved data is over 10 seconds old; clear it.
		mHistory[i].clear();
	}
	mWhen[i] = now;
	return i;
}

void StatHits::getStats(float *pER, int *pTotal, float *pWorstER, int *pWorstTotal)
{
	*pWorstER = 0.0;
	*pWorstTotal = 0;
	int total = 0, good = 0;
	int now = (gBSNNext / 48);	// Current 48-block-multiframe, modulo the hyperframe.

	StatTotalHits *hp = &mHistory[0];
	for (int i = 0; i < cNumHist; i++, hp++) {
		int age = (int)now - (int)mWhen[i];	// age of data in history bucket.
//printf(" i=%d now=%d when=%d age=%d",i,now,(int)mWhen[i],age);
		if (age < 0) { age += RLCBSN_t::BSNPeriodicity / 48; } // Account for BSN wrap.
		if (age >= cNumHist) { hp->clear(); continue; }	// Data too old.
		if (!hp->mTotal) {continue;}	// empty
		good += hp->mGood;
		total += hp->mTotal;
		float thisER = (float)(hp->mTotal-hp->mGood) / hp->mTotal;
		if (thisER > *pWorstER) {
			*pWorstER = thisER;
			*pWorstTotal = hp->mTotal;
		}
	}
	*pTotal = total;
	*pER = total ?  (float)(total-good)/total : 0;
//printf(" total=%d ER=%g\n",total,*pER);
}

// Format a number with up to 2 significant digits if < 1,
// but dont go less than .01 or to exponential notation.
// Kind of amazing there is no default format for this.
std::string fmtfloat2(float num)
{
	if (num < 0.005) {			// < 0.01 is 0
		return string("0");
	} else if (num < 0.2) {		// .01 - .19, 2 sig digit
		return format("%.2f",num).substr(1);
	} else if (num < 1) {		// .2 - .9, 1 sig digit ok.
		return format("%.1f",num).substr(1);
	} else if (num < 10) {		// 1.0 - 9.9, 2 sig digit
		return format("%.1f",num);
	} else {					// 10 - infinity, whatever digits needed.
		return format("%.0f",num);
	}
}


static void	putER(std::ostream&os, const char*label, float er, int total)
{
	// Like this: "99.9% (7) low: 4% (2)"
	os << label << fmtfloat2(er) << "% (" << total << ")";
}

// Print the average for the last N seconds and worst second.
void StatHits::textRecent(std::ostream &os)
{
	float avgER, worstER;
	int total, worstTotal;
	getStats(&avgER,&total,&worstER,&worstTotal);
	putER(os,"",avgER,total);
	// Dont bother to print worst if there is none.
	if (worstER) { putER(os," low:",worstER,worstTotal); }
}

void StatHits::textTotal(std::ostream&os)
{
	float er = mTotal.mTotal ? ((double)mTotal.mTotal - mTotal.mGood) / mTotal.mTotal : 0;
	putER(os,"",er,(int)mTotal.mTotal);
}

void MSInfo::msDumpCommon(std::ostream&os) const
{
	os << "\t";
	os << LOGVAR(msNumDataUSFGrants);
	os << LOGVAR(msAckNackUSFGrant);
	if (msOldTlli) os << LOGHEX(msOldTlli);
	if (msAltTlli) os << LOGHEX(msAltTlli);
	if (msDeprecated) os << LOGVAR(msDeprecated);
	if (msPacch) { os << " Pacch="; msPacch->shortId(); }
	os << LOGVAR2("idle",msIdleCounter);
	//os << LOGVAR(msTimingErrorCount);
	os << "\n";
}

void MSInfo::msDumpChannels(std::ostream &os) const
{
	PDCHL1Uplink *up; PDCHL1Downlink *down;
	os <<" channels:";
	int howmany = 0;
	RN_FOR_ALL_CONST(PDCHL1DownlinkList_t,msPCHDowns,down) {
		if (howmany++ == 0) os << " down=(";
		os << format(" %d:%d",down->CN(),down->TN());
	}
	if (howmany) os << ")";
	howmany = 0;
	RN_FOR_ALL_CONST(PDCHL1UplinkList_t,msPCHUps,up) {
		if (howmany++ == 0) os << " up=(";
		int tn = up->TN();
		os << format(" %d:%d,usf=%d",up->CN(),tn,(int)msUSFs[tn]);
	}
	if (howmany) os << ")";
}

void MSStat::msStatDump(const char *indent, std::ostream &os)
{
	os << indent;
	os << " dataER:"; msCountBlocks.textTotal(os);
	os << " recent:"; msCountBlocks.textRecent(os);
	{
		int fails = msCountTbfFail + msCountTbfNoConnect;
		float tbfER = (float)fails/msCountTbfs;
		//os << format(" tbfER:%.1f%% (%d)", 100.0*tbfER,(int)msCountTbfs);
		os << " tbfER:";
		putER(os,"",tbfER,(int)msCountTbfs);
	}
	os << "\n" << indent;
	os << " rrbpER:"; msCountRbbpReservations.textTotal(os);
	os << " recent:"; msCountRbbpReservations.textRecent(os);
	os << " ccchER:"; msCountCcchReservations.textTotal(os);
	os << " recent:"; msCountCcchReservations.textRecent(os);
	os << "\n";
}

void MSInfo::msDump(std::ostream&os, SGSN::PrintOptions options)
{
	int i;
	RROperatingMode::type rrmode = getRROperatingMode();
	os << this		// Dumps the operator<< value, which is sprintf(MS#%d,msDebugId)
		//<< LOGHEX(msTlli)		// The TLLI is in the default id now, so do not reprint it.
		<< LOGVAR(rrmode)
		<< " Bytes:" << msBytesUp << "up/" << msBytesDown << "down"
		// The TrafficMetric is total number of blocks sent and received,
		// decayed by 1/2 every 24 blocks, so max is 48/channel.
		// The metric will be > 100% if multiple channels are used simultaneously.
		// Note: channel = uplink or downlink, so single slot MS with uplink and downlink TBFs
		// could reach 200%.  Even a one-way TBF uses some of the other direction so can exceed 100%.
		//<< format(" Utilization=%.1f%%",100.0 * msTrafficMetric / 48.0)
		<< " Utilization=" << fmtfloat2(100.0 * msTrafficMetric / 48.0) << "%"
		<< "\n";
	os << "\t"; sgsnPrint(msTlli,options | SGSN::printNoMsId,os);
	dumpSignalQuality(os);
	msStatDump("\t",os);

	if (!(options & SGSN::printVerbose)) {return;}

	// In case the queue is completely stalled or suspended, add a new data point for
	// the current max delay if it is greater.
	if (msDownlinkQueue.size()) {
		double curage = msDownlinkQOldest.elapsed()/1000.0;
		if (curage > msDownlinkQDelay.getCurrent()) { msDownlinkQDelay.addPoint(curage); }
	}
	os << "\t" << LOGVAR2("DownlinkQ:bytes",msDownlinkQStat)
		<< LOGVAR2("delay",msDownlinkQDelay)
		<< "\n";

	os << "\t TBFs:" <<LOGVAR2("total",msCountTbfs);
	if (msCountTbfFail) { os<<LOGVAR2("failed",msCountTbfFail); }
	if (msCountTbfNoConnect) { os<<LOGVAR2("NC",msCountTbfNoConnect); }
	os <<"\n";

	os << "\t current=(";
		TBF *tbf;
		RN_MS_FOR_ALL_TBF(this,tbf) {
			//os << " "<<tbf << (tbf->mtDir == RLCDir::Up ? "(up)" : "(down)");
			os << " "<<tbf <<tbf->mtDir;
		}
	os << ")\n";

	os << "\t USFs=(";
		for (i = 0; i < 8; i++) { os << " " << msUSFs[i]; }
	os << " )\n";

	os << "\t"; msDumpChannels(os); os << "\n";
	msDumpCommon(os);
}

string MSInfo::id() const
{
	char buf[100];
	sprintf(buf," MS#%d,TLLI=%x", msDebugId,(uint32_t)msTlli);
	if (msOldTlli) { sprintf(buf+strlen(buf),",%x",(uint32_t)msOldTlli); }
	return string(buf);
}
std::ostream& operator<<(std::ostream& os, const MSInfo*ms)
{
	if (ms) {
		os << ms->id();	// not efficient, but only for debugging.
	} else {
		os << " MS#(null)";
	}
	return os;
}

};	// namespace GPRS
