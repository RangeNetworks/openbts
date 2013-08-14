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

//#include <unistd.h>
#include "GPRSInternal.h"
#include "GSMCommon.h"
#include "GSML3RRMessages.h"	// for L3RRMessage
#include "GSML3RRElements.h"	// for L3RequestReference
#define MAC_IMPLEMENTATION 1
#include "MAC.h"
#include "FEC.h"
#include "TBF.h"
#include "RLCEngine.h"
#include "RLCMessages.h"
#if INTERNAL_SGSN==0
#include "BSSG.h"
#endif
#include "Ggsn.h"	// For GgsnInit

#include <Globals.h>

extern bool gLogToConsole;

namespace GPRS {

struct TFIList *gTFIs;
RLCBSN_t gBSNNext = 0;		// The next Block Sequence Number that will be sent on the downlink.
RLCBSN_t gBSNPrev = 0;
L2MAC gL2MAC;
Stats_t Stats;
static unsigned ChIdleCounter = 0;
static unsigned ChCongestionCounter = 0;
int ExtraClockDelay = 0;

bool gFixTFIBug = 1; // Default bug fix on.
bool gFixSyncUseClock = 0;
int gFixIdleFrame = 0;	// Default bug fix off now
int gFixDRX = 1;		// Assume DRX mode if MS does not respond after first try.
bool gFixIAUsePoll = 1;// Default on.
// 6-28-2012: The ConvertForeignTLLI definitively does not work if done every time.
// The multitech modem appears to need this conversion in a special case, fixed in sgsn.
bool gFixConvertForeignTLLI = 0;	// Was only needed for external SGSN, which was sending bad TLLIs.

// This is the downlink queue from the Sgsn.
static InterthreadQueue2<SGSN::GprsSgsnDownlinkPdu> sgsnDownlinkQueue;

// Extended Dynamic Uplink TBF support.
// If we grant a USF for an uplink TBF with more uplink TNs than downlink TNs,
// first we must grant the same USF for every downlink channel for this TBF,
// and second we must not grant a USF to any other TBF for the uplink
// channels belonging to this TBF.
// This is per-ARFCN, but we pre-sort the channels so that we process one ARFCN
// at a time, and the timeslots in order from 0..7.  The multislot configuration
// insures that we will see the downlink channel before we see the uplink one.
struct ExtDyn {
	int sCurrentCN;
	unsigned sReservedUplinkSlots;

	bool isUplinkReserved(PDCHL1FEC *pdch) {
		return sReservedUplinkSlots & (1<<pdch->TN());
	}

	void reserveUplink(PDCHL1Uplink *up) {
		sReservedUplinkSlots |= (1<<up->TN());
	}

	void edReset() {
		sReservedUplinkSlots = 0;
		sCurrentCN = -1;
	}

	// We process one ARFCN at a time in order, so we only need 8 bits of memory.
	// Reset it each time we start a new ARFCN.
	void edSetCn(int cn) {
		if (cn != sCurrentCN) {
			sReservedUplinkSlots = 0;
			sCurrentCN = cn;
		}
	}
} extDyn;

// Get the number quietly.
// Use this for debug options we dont need the user to see.
int configGetNumQ(const char *name, int defaultvalue)
{
	if (gConfig.defines(name)) {
		//return gConfig.getNum(name,defaultvalue);
		const char *strval = gConfig.getStr(name).c_str();
		return strtol(strval,NULL,0); // strtol allows hex
	} else {
		return defaultvalue;
	}
}

// Dont bother with a fancy specification (eg: 2x4) because we are going
// to dynamically allocate channels soon.
int configGprsChannelsMinCn() { return gConfig.getNum("GPRS.Channels.Min.CN"); }
int configGprsChannelsMinC0() { return gConfig.getNum("GPRS.Channels.Min.C0"); }
int configGprsChannelsMin() { return configGprsChannelsMinC0() + configGprsChannelsMinCn(); }
#if GPRS_CHANNELS_MAX_SUPPORTED
	// We are currently doing only static assignment, so take this out for now.
int configGprsChannelsMax() { return gConfig.getNum("GPRS.Channels.Max"); }
#endif
int configGprsMultislotMaxUplink() { return gConfig.getNum("GPRS.Multislot.Max.Uplink"); }
int configGprsMultislotMaxDownlink() { return gConfig.getNum("GPRS.Multislot.Max.Downlink"); }

//struct GPRSConfig GPRSConfig; not needed.
unsigned GPRSDebug = 0;
unsigned gGprsWatch = 0;

void GPRSSetDebug(int value)
{
	GPRSDebug = value;
	if (GPRSDebug) {
		// Dont change these so that we can test the normal use.
	}
}

bool GPRSConfig::IsEnabled()
{

	// BEGINCONFIG
	// 'GPRS.Enable',1,0,0,'Enable GPRS service: 0 or 1.  If enabled, GPRS service is advertised in the C0T0 beacon, and GPRS service may be started on demand.  See also GPRS.Channels.*'
	// ENDCONFIG
	if (gConfig.getNum("GPRS.Enable")) return true;
	return false;	// nope
}

bool GPRSConfig::sgsnIsInternal()
{
	// 2-2012: I added an extra sql parameter GPRS.SGSN.External that you must set to use the
	// external sgsn, in order to avoid people with existing sql from using it by accident.
	const char *sql_sgsn_host = "GPRS.SGSN.Host";
	const char *sql_sgsn_external = "GPRS.SGSN.External";
	if (!gConfig.defines(sql_sgsn_host)) return true;
	if (!gConfig.defines(sql_sgsn_external)) return true;
	if (0 == gConfig.getNum(sql_sgsn_external)) return true;
	return false;	// Use external SGSN.Host
}

// (mike) pretty sure this function is not used anywhere...
unsigned GPRSConfig::GetRAColour()
{
	// BEGINCONFIG
	// 'GPRS.RA_COLOUR',0,0,0,'GPRS Routing Area Color as advertised in the C0T0 beacon'
	// ENDCONFIG
	if (gConfig.defines("GPRS.RA_COLOUR")) {
		return gConfig.getNum("GPRS.RA_COLOUR");
	}
	return 0;
}

// GSM04.60 12.24.  The T3192 code is placed in System Information 13, GSM04.08.
// The code specifies one of the following values in msec.
static unsigned T3192Codes[8] = {
	500, 1000, 1500, 0, 80, 120, 160, 200,
};

// 3GPP 04.60 12.24 GPRS Cell Options Notes:
// NMO is Network Mode of Operation:  See GSM 03.60 6.3.3.1
// 		Network Mode of Operation II (IE value 1) is the most brain-dead,
//		no dual-transfer-mode, the GPRS attached MS uses CCCH.
// 4-24-2012: I am testing NMO 1 to be used in UMTS, however we cannot use
// it in GPRS until we support paging for CS calls on the GPRS data channel.
// BEGINCONFIG
// 'GPRS.CellOptions.T3168Code',5,1,0,'Timer 3168 in the MS controls the wait time after sending a Packet Resource Request to initiate a TBF before giving up or reattempting a Packet Access Procedure, which may imply sending a new RACH.  This code is broadcast to the MS in the C0T0 beacon in the GPRS Cell Options IE.  See GSM 04.60 12.24.  Range 0..7 to represent 0.5sec to 4sec in 0.5sec steps.'
// 'GPRS.CellOptions.T3192Code',0,1,0,'Timer 3192 in the MS specifies the time MS continues to listen on PDCH after all downlink TBFs are finished, and is used to reduce unnecessary RACH traffic.  This code is broadcast to the MS in the C0T0 beacon in the GPRS Cell Options IE. The value must be one of the codes described in GSM 04.60 12.24.  Value 0 implies 500msec.'
// ENDCONFIG
// DRX_TIMER irrelevant since we dont use DRX mode at all.
// ACCESS_BURST_TYPE 0 => use 8 bit format of Packet Channel Request Message,
//		(that is, if we were using PRACH, which we are not.)
// CONTROL_ACK_TYPE 1 >= default format for Packet Control Acknowledgement is RLC/MAC block,
//		ie, not special.
// BS_CV_MAX is the max number of RLC blocks used by any RLC message.
// NW_EXT_UTBF is extended uplink TBF mode: 44.060 9.3.1b and 9.3.1.3
GPRSCellOptions_t::GPRSCellOptions_t() :
	mNMO(gConfig.getNum("GPRS.NMO")-1),	// IE value is spec NMO - 1.
	//mNMO(1),		// Dont let customers change this.
	mT3168Code(gConfig.getNum("GPRS.CellOptions.T3168Code")), // T3168Code 5 => 5000msec
	mT3192Code(gConfig.getNum("GPRS.CellOptions.T3192Code")), // T3192Code 0 => 500msec.
	// Took this out of the gConfig options.
	// mDRX_TIMER_MAX(gConfig.getNum("GPRS.CellOptions.DRX_TIMER_MAX",0)),
	mDRX_TIMER_MAX(7),	// It is actually non-drx timer max. 
					// The MS uses the min of this and a param sent in gprs attach.
	mACCESS_BURST_TYPE(0),
	mCONTROL_ACK_TYPE(1),	// Packet Control Acknowledgement is an RLC/BLOCK, not a RACH burst.
	mBS_CV_MAX(1),	// This is determined by the system, not the user.
	mNW_EXT_UTBF(gConfig.getNum("GPRS.Uplink.Persist") > 0)
{
	// Sanity check some values.
	if (RN_BOUND(mNMO,0,2) != mNMO) {
		GLOG(ERR) << "NMO [Network Mode of Operation] must be 1,2,3";	// we subtracted 1
		mNMO = 1;
	}
	if (RN_BOUND(mT3168Code,0,7) != mT3168Code) {
		GLOG(ERR) << "mT3168 value " << mT3168Code << " must be in range 0..7";
		mT3168Code = 1;
	}
	if (RN_BOUND(mT3192Code,0,7) != mT3192Code) {
		GLOG(ERR) << "mT3192 value " << mT3192Code << " must be in range 0..7";
		mT3192Code = 0;
	}
}

// This function probes the config file the first time it is called.
GPRSCellOptions_t &GPRSGetCellOptions()
{
	static GPRSCellOptions_t *GPRSCellOptions = NULL;
	if (!GPRSCellOptions) GPRSCellOptions = new GPRSCellOptions_t();
	return *GPRSCellOptions;
}

USFList::USFList()
{
	//mRandomUSF=0;
	for (int i=0;i<USFTotal;i++) { mlUSFs[i].muMS = NULL; }
}

MSInfo *USFList::getUSFMS(int usf)
{
	UsfInfo *info = &mlUSFs[usf];
	if (info->muDeadTime.valid()) {
		if (info->muDeadTime.expired()) {
			info->muDeadTime.setInvalid();	// Can use the USF now.
			info->muMS = 0;
		}
		return NULL;
	}
	return info->muMS;
}

// Find or set a USF for this ms.  Return -1 on failure.
int USFList::allocateUSF(MSInfo *ms)
{
	int usf, freeusf = -1;
	// We want to run through the whole list to see if there was a USF for this MS previously.
	for (usf = USFMIN; usf <= USFMAX; usf++) {
		if (mlUSFs[usf].muMS == ms) {
			// The MS can reuse its own USF.  The muDeadTime is how long other MS cannot use it.
			mlUSFs[usf].muDeadTime.setInvalid();	// USF is back in use.
			return usf;
		}
		if (freeusf == -1 && getUSFMS(usf) == NULL) { freeusf = usf; }
	}
	if (freeusf >= USFMIN) {
		mlUSFs[freeusf].muMS = ms;
		return freeusf;
	}
	return -1;	// All in use.
}

// Free the USFs assigned to this ms.
// We could have saved the usf number in the MS to avoid searching for it,
// but the list is short and this does not happen often.
// If wReserve, the tbf died, so we must reserve the USF resource for 5 seconds.
// Note that the same MS can reuse its own reserved USF if it positively re-establishes communication.
int USFList::freeUSF(MSInfo *ms, bool wReserve)
{
	for (int usf = USFMIN; usf <= USFMAX; usf++) {
		UsfInfo *info = &mlUSFs[usf];
		if (info->muMS == ms) {
			if (wReserve) {
				info->muDeadTime.setFuture(5000);	// This is 5 seconds, not programmable.
			} else {
				info->muMS = 0;
				info->muDeadTime.setInvalid();		// Make sure usf is reusable.
			}
			return usf;
		}
	}
	return 0;
}

// Return any USF that is assigned to an MS.
// Used when there is nothing else to do with the uplink channel.
// Not really random, the MS are serviced round-robin.
// We dont want to just give the USF to the MS with the oldest response time,
// because it could be out of range, or whatever, and we may never hear from it.
//int USFList::getRandomUSF()
//{
//	for (int i = USFMIN; i <= USFMAX; i++) {
//		if (++mRandomUSF > USFMAX) { mRandomUSF = USFMIN; }
//		if (mlUSFs[mRandomUSF]) { return mRandomUSF; }
//	}
//	return 0;	// There are none.
//}

void USFList::usfDump(std::ostream&os)
{
	int i;
	os << "USFList=(";
	for (i = USFMIN; i <= USFMAX; i++) {
		os << " " << i << "=>";
		MSInfo *ms = mlUSFs[i].muMS;
		if (ms) {
			os << ms;
			long remaining = mlUSFs[i].muDeadTime.remaining();
			if (remaining) { os << LOGVAR(remaining); }
		} else {
			os << "free";
		}
	}
	//os << RN_PRETTY_TEXT1(mRandomUSF);
	os << ")\n";
}

int USFList::getUsf(unsigned upbsn)	// bsn from the uplink burst.
{
	// GSM 05.02 6.3.2.2.1: The USF field in downlink block N signals
	// that uplink block (N+1) is assigned to that MS.
	// 7-6-2012 update: This is also used for extended uplink TBF mode now.
	unsigned downbsn = upbsn-1;
	return (sRememberUsfBsn[downbsn%32] == downbsn ? ((signed)sRememberUsf[downbsn%32]) : -1);
}

void USFList::setUsf(unsigned downusf, unsigned downbsn)		// Save usf for current downlink burst.
{
	sRememberUsf[downbsn%32] = downusf;
	sRememberUsfBsn[downbsn%32] = downbsn;
}


void L2MAC::macConfigInit()
{
	GPRSSetDebug(configGetNumQ("GPRS.Debug",0));
	gGprsWatch = configGetNumQ("GPRS.WATCH",0);
	gLogToConsole = configGetNumQ("Log.ToConsole",0);

	GPRSCellOptions_t& gco = GPRSGetCellOptions();
	// BEGINCONFIG
	// 'GPRS.Counters.N3101',20,0,0,'Counts unused USF responses to detect nonresponsive MS.  Should be > 8.  See GSM04.60 sec 13.'
	// 'GPRS.Counters.N3103',8,0,0,'Counts ACK/NACK attempts to detect nonresponsive MS.  See GSM04.60 sec 13.'
	// 'GPRS.Counters.N3105',12,0,0,'Counts unused RRBP responses to detect nonresponsive MS. See GSM04.60 sec 13.'
	// 'GPRS.Timers.T3169',5000,0,0,'Nonresponsive uplink tbf timer, in msecs. See GSM04.60 sec 13'
	// 'GPRS.Timers.T3191',5000,0,0,'Nonresponsive downlink tbf timer, in msecs. See GSM04.60 sec 13'
	// 'GPRS.Timers.T3193',0,0,0,'Timer T3193 (in msecs) in the base station corresponds to T3192 in the MS, which is set by GPRS.CellOptions.T3192Code.  The T3193 value should be slightly longer than that specified by the T3192Code.  If 0, the BTS will fill in a default value based on T3192Code.'
	// 'GPRS.Timers.T3195',5000,0,0, 'Nonresponsive MS timer, in msecs. See GSM04.60 sec 13'
	// ENDCONFIG
	macN3101Max = gConfig.getNum("GPRS.Counters.N3101");
	macN3103Max = gConfig.getNum("GPRS.Counters.N3103");
	macN3105Max = gConfig.getNum("GPRS.Counters.N3105");
	macT3169Value = gConfig.getNum("GPRS.Timers.T3169");	// in msecs.
	macT3191Value = gConfig.getNum("GPRS.Timers.T3191");	// in msecs.
	macT3193Value = gConfig.getNum("GPRS.Timers.T3193");	// fixed below.
	macT3195Value = gConfig.getNum("GPRS.Timers.T3195");	// in msecs.
	macT3168Value = (gco.mT3168Code + 1) * 500;			// in msecs
	//macTNonResponsive = gConfig.getNUM("GPRS.Timers.MS.NonResponsive")	// in msecs

	// Spec says T3193 (in network) should be longer than T3192 (in MS).
	// If unspecified, add 50 msecs to T3192.
	// TODO: We should really have a dead zone near the end of this timer
	// where we just leave the MS alone, but it doesnt hurt anything,
	// just wastes airwaves trying to contact the MS in the wrong mode.
	unsigned T3192Value = T3192Codes[gco.mT3192Code];
	if (macT3193Value == 0) {
		macT3193Value = T3192Value + 50;	// Add some extra msecs.
	}
	if (macT3193Value < T3192Value) {
		static unsigned lastT3193Value = 0, lastT3192Value = 0;
		if (lastT3193Value != macT3193Value || lastT3192Value != T3192Value) {
			GLOG(ERR) << "T3193 value " << macT3193Value << " should be longer than"
				" T3192 value " << T3192Value << 
				" (T3192code=" << gco.mT3192Code << ")";
			lastT3193Value = macT3193Value;
			lastT3192Value = T3192Value;
		}
	}
	static bool firsttime = true;
	if (firsttime) {
		GLOG(INFO) << "Note: GPRS T3192 = " << T3192Value;
		GLOG(INFO) << "Note: GPRS T3193 = " << macT3193Value;
		firsttime = 0;
	}

	// These 'timers' are in seconds converted to RLC block counts.
	// Note: We must keep the MS structure around long enough to handle downlink SGSN
	// message responses to an uplink message, but this period is short.
	// If either the MS or SGSN wants to start a new TBF, they send a new TLLI
	// to create a new MSInfo struct.  However, in the future we may use the statistics
	// gathered from a previous TBF to determine the ChannelCoding (speed) to use
	// for future TBFs.
	// BEGINCONFIG
	// 'GPRS.Timers.MS.Idle',600,0,0,'How long an MS is idle before the BTS forgets about it.'
	// 'GPRS.Timers.Channels.Idle',6000,0,0,'How long a GPRS channel is idle before being returned to the pool of channels.  Also depends on Channels.Min.  Currently the channel cannot be returned to the pool while there is any GPRS activity on any channel.'
	// 'GPRS.Channels.Congestion.Timer',60,0,0,'How long GPRS congestion exceeds the Congestion.Threshold before we attempt to allocate another channel for GPRS'
	// 'GPRS.Channels.Congestion.Threshold',200,0,0,'The GPRS channel is considered congested if the desired bandwidth exceeds available bandwidth by this amount, specified in percent.'
	// ENDCONFIG
	macMSIdleMax = gConfig.getNum("GPRS.Timers.MS.Idle") * RLCBlocksPerSecond;
	macChIdleMax = gConfig.getNum("GPRS.Timers.Channels.Idle") * RLCBlocksPerSecond;
	macChCongestionMax = gConfig.getNum("GPRS.Channels.Congestion.Timer") * RLCBlocksPerSecond;
	// database number specified in percent:
	macChCongestionThreshold = gConfig.getNum("GPRS.Channels.Congestion.Threshold") / 100.0;
	macDownlinkPersist = gConfig.getNum("GPRS.Downlink.Persist");
	static bool thisMessageHasBeenPrinted = false;
	if (macDownlinkPersist && !thisMessageHasBeenPrinted) {
		thisMessageHasBeenPrinted = true;
		LOG(ALERT) << "GPRS.Downlink.Persist is not implemented and config value should be 0!";
	}
	macDownlinkKeepAlive = gConfig.getNum("GPRS.Downlink.KeepAlive");
	macUplinkPersist = gConfig.getNum("GPRS.Uplink.Persist");
	macUplinkKeepAlive = gConfig.getNum("GPRS.Uplink.KeepAlive");

	if (macSingleStepMode) {
		// Set these to maximum values so we can single step the service loop
		// without these timers going off.
		macMSIdleMax = macChIdleMax = 0x7fffffff;
		macT3191Value = macT3193Value  =  0x7fffffff;
	}

	gFixIdleFrame = configGetNumQ("GPRS.FixIdleFrame",gFixIdleFrame);
	gFixTFIBug = configGetNumQ("GPRS.FixTFIBug",gFixTFIBug); // Default bug fix on.
	gFixDRX = configGetNumQ("GPRS.FixDRX",(int)gFixDRX); // Default to 4 sendAssignment tries.
	gFixIAUsePoll = configGetNumQ("GPRS.FixIAUsePoll",gFixIAUsePoll);
	gFixConvertForeignTLLI = configGetNumQ("GPRS.FixForeignTlli",gFixConvertForeignTLLI);
}

void L2MAC::macAddTBF(TBF *tbf) {
	macTBFs.push_back(tbf); // Usually already locked, so lock is recursive
	//macTBFs.push_back_safely(tbf); // Usually already locked, so lock is recursive
}

void L2MAC::macForgetTBF(TBF *tbf, bool forever)
{
	GPRSLOG(2) << "forget "<<tbf->tbfid(0);
	macTBFs.remove(tbf);
	// lock unnecessary, using macLock now:
	//macTBFs.remove_safely(tbf); // Usually already locked, so lock is recursive
	//ScopedLock lock2(macExpiredTBFs.mListLock);
	if (forever) {
		macExpiredTBFs.remove(tbf);	// Just in case it was on this list.
		GPRSLOG(2) << "delete ",tbf->tbfid(0);
		delete tbf;
		return;
	}
	macExpiredTBFs.push_front(tbf);
	unsigned keepExpired = gConfig.getNum("GPRS.TBF.KeepExpiredCount");
	while (macExpiredTBFs.size() > keepExpired) {
		TBF *tbf2 = macExpiredTBFs.back();
		macExpiredTBFs.pop_back();	// returns void, the nitwits.
		GPRSLOG(2) << "delete ",tbf2->tbfid(0);
		delete tbf2;
	}
}

void L2MAC::macAddMS(MSInfo *ms) { macMSs.push_back(ms); }

void L2MAC::macForgetMS(MSInfo *ms, bool forever)
{
	macMSs.remove(ms);
	// lock unnecessary, using macLock now:
	//macMSs.remove_safely(ms); // Usually already locked, so lock is recursive
	//ScopedLock lock2(macExpiredMSs.mListLock);
	if (forever) {
		macExpiredMSs.remove(ms);	// Just in case it was on this list.
		delete ms;
		return;
	}
	macExpiredMSs.push_front(ms);
	unsigned keepExpired = gConfig.getNum("GPRS.MS.KeepExpiredCount");
	while (macExpiredMSs.size() > keepExpired) {
		MSInfo *ms2 = macExpiredMSs.back();
		macExpiredMSs.pop_back();
		delete ms2;
	}
}

// The MS list will be short.  Just look through linearly.
MSInfo *L2MAC::macFindMSByTlli(uint32_t tlli, int create /*=0*/)
{
	MSInfo *ms;
	RN_MAC_FOR_ALL_MS(ms) {
		// When the MS performs a Detach procedure, it will change its existing tlli
		// from a local tlli to a foreign tlli.  Instead of having the SGSN inform us
		// of these events, just ignore whether the tlli is local or foreign.
		if (tlliEq(ms->msTlli, tlli) || tlliEq(ms->msOldTlli,tlli)) {
			// This is very important.
			// If the SGSN looks up an MS by TLLI it is because it is about to use it,
			// and we dont want it to disappear before it does.
			ms->msIdleCounter = 0;
			return ms;
		}
	}
	if (! create) { return NULL; }
	ms = new MSInfo(tlli);
	GPRSLOG(1) << "New MS:"<<ms<<LOGHEX(tlli);
	return ms;
}

// Are GSM channels allocated to GPRS service?
// Note this is unrelated to the service loop, because that runs forever even
// if no channels are currently active.
int L2MAC::macActiveChannels()
{
	ScopedLock lock(macLock);	// Probably overkill, but this function called from CLI.
	return macPDCHs.size();
}

int L2MAC::macActiveChannelsC(unsigned cn)
{
	ScopedLock lock(macLock);	// This function may be called from CLI.
	int cnt = 0;
	PDCHL1FEC *ch;
	RN_MAC_FOR_ALL_PDCH(ch) {
		if (ch->CN() == 0) { cnt++; }
	}
	return cnt;
}

static void macAddOneChannel(TCHFACCHLogicalChannel *lchan)
{
	PDCHL1FEC *pch = new PDCHL1FEC(lchan);
	gL2MAC.macPDCHs.push_back(pch);
	gL2MAC.macPacchs.clear();	// Must rebuild the pacch list.
	pch->mchStart();
	GLOG(INFO) << "GPRS AddChannel " << pch << " total active="
		<<gL2MAC.macActiveChannels()<<"\n";
}

// Channel allocation for GPRS.
// If GPRS is enabled, we need at least one channel to handle GPRS registration activity,
// and it will be used often so it should be on the first ARFCN, CN0.
// That channel used for registration does not need to be completely dedicated;
// it can also be used for multislot assignments on adjacent channels.
// So the rule is that GPRS.Channels.Min channels will be allocated
// at startup from CN0 up to the limit available on CN0, thereafter all gprs
// channels will be allocated in adjacent groups with preference given to channels
// adjacent to existing gprs channels, and secondarily to groups of channels
// as close as possible to the end of the channel list.
// RR channels will be allocated from the front of the channel list.
// If GPRS is enabled and GPRS.Channels.Min is 0, what effectively will happen
// if there are any phones at all within range is that one channel will be
// allocated shortly after startup from the end of the channel list.
// OLD:
// Allocate another GSM channel for GPRS use.
// TODO: We would prefer adjacent channels for multislot use.
bool L2MAC::macAddChannel()
{
	ChIdleCounter = ChCongestionCounter = 0;
	// BEGINCONFIG
	// 'GPRS.Channels.Max',4,0,0,'Maximum number of channels allocated for GPRS service.'
	// 'GPRS.Channels.Min',0,0,0,'Minimum number of channels allocated for GPRS service once it starts.'
	// ENDCONFIG
	macCheckChannels();

	// TODO: Dynamic channel assignment.
#if GPRS_CHANNELS_MAX_SUPPORTED
	if (macActiveChannels() >= configGprsChannelsMax()) {
		return false;
	}
#endif

	if (! macActiveChannels()) {
		// When you first start the BTS you will not be able to allocate until some
		// timer expires, which I measured as 4 seconds.
		// So dont bother reporting until after 5 seconds.
		time_t now; time(&now);
		if (now - macStartTime < 5) { return false; }
		// And dont print this message any more often than 10 seconds.
		static time_t lastMessageTime = 0;
		if (now - lastMessageTime < 10) { return false; }
		GLOG(INFO) << "GPRS: Unable to allocate channel, all are busy";
		time(&lastMessageTime);
		return false;
	}

	return true;
}

// Try to free a GPRS channel, returning it to GSM RR use.
// 5-24-2012: We must not free the channel that is our PACCH.
bool L2MAC::macFreeChannel()
{
	ChIdleCounter = ChCongestionCounter = 0;
	if (macActiveChannels() <= configGprsChannelsMin()) { return false; }

	//PDCHL1FEC *pdch = gL2MAC.macPickChannel();	// pick the least busy channel;
	PDCHL1FEC *pdch = gL2MAC.macPDCHs.back();
	GLOG(INFO) << "GPRS freeing channel" << pdch;
	GPRSLOG(1) << "GPRS freeing channel " << pdch;
	delete pdch;	// Among other things, removes from macPDCHs before freeing it.
	macPacchs.clear();	// Must rebuild the pacch list.
	return true;
}


// This is called during channel destruction to clean up any references to the channel.
// The channel better not be in use.
// Delete any tbfs using the channel.  Detach any MSs using the channel.
// Remove the channel from the list in use by GPRS.
void L2MAC::macForgetCh(PDCHL1FEC*pch)
{
	pch->mchStop();	// TODO: This should set a timer before the channel goes back to RR use.

	// Delete any tbfs that used this channel.
	// Warning: delete tbf removes the tbf from the list we are traversing, so be careful.
	TBF *tbf;
	RN_MAC_FOR_ALL_TBF(tbf) {
		if (tbf->canUseDownlink(pch->downlink()) || tbf->canUseUplink(pch->uplink())) {
			tbf->mtCancel(MSStopCause::ShutDown,TbfNoRetry);	// Deletes tbf.
		}
	}
	// Detach any ms that might be using this channel.
	// FIXME: This needs work.  We need to send the MS new channel assignments which
	// is a long procedure and may need a new state.
	MSInfo *ms;
	RN_MAC_FOR_ALL_MS(ms) {
		if (ms->canUseDownlink(pch->downlink()) || ms->canUseUplink(pch->uplink())) {
			ms->msReassignChannels();
		}
		// TODO: Without its PACCH the MSInfo is useless, should kill it off?
		if (ms->msPacch == pch) { ms->msPacch = NULL; }
	}
	macPDCHs.remove(pch);
	macPacchs.clear();	// Must rebuild the pacch list.
	GPRSLOG(1) << "macForgetChannel, remaining="<<macPDCHs.size();
}

// Return a mask of the channels available on this arfcn.
unsigned L2MAC::macFindChannels(unsigned arfcn)
{
	int resultmask = 0;
	PDCHL1FEC *ch;
	RN_MAC_FOR_ALL_PDCH(ch) {
		if (arfcn == ch->ARFCN()) {
			resultmask |= 1 << ch->TN();
		}
	}
	return resultmask;
}

PDCHL1FEC *L2MAC::macFindChannel(unsigned arfcn, unsigned tn)
{
	PDCHL1FEC *ch;
	RN_MAC_FOR_ALL_PDCH(ch) {
		if (arfcn == ch->ARFCN() && tn == ch->TN()) { return ch; }
	}
	return NULL;
}

static void dumpPdch()
{

	PDCHL1FEC *ch;
	printf("PDCHs=%d:",gL2MAC.macPDCHs.size());
	RN_MAC_FOR_ALL_PDCH(ch) { printf(" %s",ch->shortId()); }
	printf("\n");
	printf("PACCHs=%d",gL2MAC.macPacchs.size());
	RN_MAC_FOR_ALL_PACCH(ch) { printf(" %s",ch->shortId()); }
	printf("\n");
}

// Given an list of adjacent channels, try to derive optimal PACCH
// and stick them in the macPacchs list.
static void macPacchAddAdjCh(PDCHL1FEC**alist,int asize)
{
	int downslots = configGprsMultislotMaxDownlink();	// TODO: add a separate chunk size.
	int upslots = configGprsMultislotMaxUplink();
	int chunk = upslots>downslots ? upslots : downslots;
	//printf("first chunk=%d\n",chunk);
	chunk = RN_BOUND(chunk,1,4);

	if (asize < chunk) {
		// We cannot optimize this adjacency set.
		if (asize == 1) {
			GLOG(WARNING) << "GPRS: single channel cannot be used multislot: "<<alist[0];
			return;
		} else {
			// This is not a full-bandwidth multislot channel.
			GLOG(WARNING) << "GPRS: channels cannot be used in optimum multislot configuration: "<<alist[0];
			GLOG(INFO) <<format("a:adding PACCH=%d asize=%d chunk=%d\n",1,asize,chunk);//dumpPdch();
			gL2MAC.macPacchs.push_back(alist[1]);
		}
		return;
	}

	// For 2-down/2-up we can use either channel.
	// For 3-down/2-up, if chs numbered 0,1,2 we can use 1 or 2.
	// For 2-down/3-up, if chs numbered 0,1,2 we can use 0 only.
	// For 4-down/1-up, if chs numbered 0,1,2,3 we can use 2 only.
	// For 1-down/4-up, if chs numbered 0,1,2,3 we can use 0 only.
	// The best idea is to allocate channels in pairs, in which case
	// the first one is the PACCH.  Then:
	// for 4-down/1-up DDPD we also utilize the pair on the left,
	// for 1-down/4-up PUUU we also utilize the pair on the right.
	int offset = 0;	// For chunk == 1 or 2.
	if (downslots < upslots) {
		offset = 0;		// Only first channel can be PACCH.
	} else {
		if (chunk == 3) {
			offset = 1;		// PACCH on the middle channel.
		} else if (chunk == 4) {
			offset = (downslots >= upslots) ? 2 : 0;
		}
	}
	int full = asize/chunk;		// How many full bandwidth pacch.
	for (int i = 0; i < full; i++) {
		int n = i*chunk+offset;
		devassert(n < asize);
		if (n < asize) {
			GLOG(INFO)<<format("b:adding PACCH=%d asize=%d chunk=%d\n",n,asize,chunk);//dumpPdch();
			gL2MAC.macPacchs.push_back(alist[n]);
		}
	}

	// Do something with the leftover.
	int leftover = asize - full*chunk;
	if (leftover == 1) {
		// FIXME: do something about this, like maybe a separate chunk size option,
		// or maybe how many channels to allocate on each arfcn.
		// For now, just drop the channels.
		forgetit:
		GLOG(WARNING) << "GPRS: "<<leftover<<" channels cannot be used in multislot configuration";
		return;
	}
	if (leftover>1) {
		// If asize == 8 then we can only get here if chunk == 3, but check anyway:
		if (asize == 8 && chunk == 3) {
			// In this case we will have have 3 pacchs of size 3,3,2 where
			// the top pacch shares one tn with the one below.
			//printf("c:adding %d asize=%d\n",6,asize);dumpPdch();
			gL2MAC.macPacchs.push_back(alist[6]);
		} else if (asize > chunk) {
			// Go ahead and make a pacch that shares with adjacent ch below.
			// If it were a full ARFCN, it could share above or below, so it
			// would not matter much what we pick for PACCH, but in that case,
			// we would not be in this branch.
			// So the only sharing opportunity is with the channels below.
			int lastpacch = asize - leftover + offset;
			if (lastpacch < 0 || lastpacch >= asize) {
				GLOG(ERR) << "logic error in leftover PACCH calculation";	// Dont crash.
			} else {
				gL2MAC.macPacchs.push_back(alist[lastpacch]);
				GLOG(INFO)<<format("c:adding PACCH=%d asize=%d chunk=%d leftover=%d\n",lastpacch,asize,chunk,leftover);//dumpPdch();
			}
		} else {
			// This is a lonely set of timeslots smaller than the chunk size.
			// This PACCH can never be the full speed requested.
			// TODO: When we support multiple QoS, these channels will become useful.
			goto forgetit;	// But for now, dump them.
		}
	}
}

static void macPacchRebuild()
{
	int size = gL2MAC.macPDCHs.size();
	PDCHL1FEC *ch;

	//printf("macPacchRebuild:"); dumpPdch();

	// The chunk size is how far apart the pacchs will be.
	// If it is 3, and we have a whole arfcn, 8/3 doesnt work,
	// one TN will be shared by two of them.
	if (size <= 2) {
		// Not much we can optimize about this.
		// In 2-down/2-up, either of the two channels can be pacch, so use both.
		// This case could be handled below also, but it is better
		// to add both channels instead of just one.
		RN_MAC_FOR_ALL_PDCH(ch) { gL2MAC.macPacchs.push_back(ch); }
		return;
	}

	// Look for adjacent channels and process each such set.
	PDCHL1FEC *alist[8];	// List of adjacent channels, max of 8 TNs per arfcn.
	int asize = 0;			// Number of channels in alist.

	int cn = -1, tn = -1;
	RN_MAC_FOR_ALL_PDCH(ch) {
		if (asize == 0) {
			alist[asize++] = ch;
		} else if (cn == (int)ch->CN() && tn+1 == (int)ch->TN()) {
			alist[asize++] = ch;		// This is an adjacent channel.
		} else {						// This is not an adjacent ch
			// Process a list of adjacent channels.
			macPacchAddAdjCh(alist,asize);
			asize = 0;	// Start a new adjacency list.
		}
		cn = ch->CN();
		tn = ch->TN();
	}
	if (asize) { macPacchAddAdjCh(alist,asize); }

	if (GPRSDebug) { printf("after macPacchRebuild:"); dumpPdch(); }

	// Check for disaster.  This would happen if all the channels were singletons.
	if (gL2MAC.macPacchs.size() == 0) {
		GLOG(WARNING) << "GPRS: No channels found that can be used in multislot configuration";
		RN_MAC_FOR_ALL_PDCH(ch) { gL2MAC.macPacchs.push_back(ch); }
	}
}

// Return a GPRS channel to use.
// Try to pick the least busy channel.
// For an uplink it would be nice to make sure we pick a channel that has free USFs,
// but I'm not going to worry about it.
PDCHL1FEC *L2MAC::macPickChannel()
{
	int size = macPDCHs.size();
	if (size == 0) { return NULL; }	// Dont think this can happen.
	if (0) {
		// Phase 1: Original code:
		static int roundrobin = 0;
		if (++roundrobin >= size) { roundrobin = 0; }
		return macPDCHs[roundrobin];
	}
	//printf("macPickChannel:"); dumpPdch();

	// To give the phones better bandwidth, only return channels that can be
	// used in an optimum (or close to optimum) multislot config.
	// We keep the channels we will use as pacch in the macPacchs list.

	// Rebuild the pacch list if necessary.
	// The list is cleared whenever we add/remove from macPDCHs.
	if (macPacchs.size() == 0) { macPacchRebuild(); }

	//printf("macPickChannel after rebuild:"); dumpPdch();

	// Determine the approximate load on each pacch and pick the least busy.
	int npacch = macPacchs.size();
	devassert(npacch);
	PDCHL1FEC *ch, *bestch = NULL;
	int bestload = 0;			// unneeded init to make gcc happy.
	for (RListIterator<typeof(ch)> itr(macPacchs); itr.next(ch); ) {
		int load = 0;
		MSInfo *ms;
		RN_MAC_FOR_ALL_MS(ms) {
			// TODO: Use totalsize instead of size, which requires changing the q type
			// TODO: Add in the uplink load too.
			// TODO: The PACCH for assignments that favor uplink over downlink
			// are one off assignments that favor downlink over uplink, so test
			// the load on all the assigned channels, not just PACCH.
			// Add 1 so an unallocated pacch wins over an allocated one, even if not loaded.
			if (ms->msPacch == ch) {
				// The msTrafficMetric measures the relative past utilization of the channel in blocks sent,
				// while downlinkqueuesize is in bytes.  Multiply to kind of even out their influence.
				// Add 1 in case nobody is sending anything we will still differentiate empty channels.
				int msload = ms->msDownlinkQueue.size() + ms->msTrafficMetric * 30;
				load += 1 + msload;
				GPRSLOG(2) << "macPickChannel loop"<<LOGVAR(ch)<<ms<<LOGVAR(msload) << LOGVAR(load);
			}
		}
		if (bestch == NULL || load < bestload) {
			bestch = ch; bestload = load;
		}
		GPRSLOG(2) << "macPickChannel intermediate"<<LOGVAR(bestch) << LOGVAR(ch) << LOGVAR(load);
	}
	GPRSLOG(2) << "macPickChannel result "<<LOGVAR(bestch);

	// And there you have it.
	return bestch;
}



// NOTE: This function runs asynchronously.
// Start up GPRS if it is not started already.
// We return success only if we have allocated at least one GSM channel
// for GPRS service and started all the service threads.
static void *macThreadFunc(void *arg);	// forward decl
bool L2MAC::macStart()
{
	if (! GPRSConfig::IsEnabled()) return false;
	macStopFlag = 0;
	time(&macStartTime);

	// macStart() is called from the RACH code which occurs asynchronously to GPRS code, so lock.
	// In other words, we dont want a second RACH to reenter macStart() while it is working.
	ScopedLock lock(macLock);

	if (macRunning) { return true; }

	if (GPRSConfig::sgsnIsInternal()) {
		if (!SGSN::Ggsn::start()) {
			GLOG(ERR) << "GGSN failed to init";
			return false;
		}
	} else {
#if INTERNAL_SGSN==0
		if (! BSSG::gBSSG.BSSGOpen()) { return false; }
#endif
	}

	// Sanity test and print warnings.
	if (configGprsMultislotMaxDownlink() > 1 || configGprsMultislotMaxUplink() > 1) {
		const char *multislotmsg = "A multislot configuration, required for high-speed GPRS service, is suggested by the config options GPRS.Multislot.Max.Downlink or GPRS.Multislot.Max.Uplink";
#if GPRS_CHANNELS_MAX_SUPPORTED
		if (configGprsChannelsMax() <= 1) {
			GLOG(WARNING) << multislotmsg << " but is not possible because GPRS.Channels.Max <= 1";
		} else
#endif
		if (configGprsChannelsMin() <= 1) {
			GLOG(WARNING) << multislotmsg << " but is unlikely to be achieved because GPRS.Channels.Min <= 1";
		}
	}

	// Allocate initial channels, if specified.
	// Update: Channel allocation will not work until OpenBTS has been
	// running a few seconds.  I suspect the channels are created with
	// their recyclable timers running and we cannot allocate them until
	// they expire.  So dont even try.  The mac service loop will try again later.
	//int minchans = gConfig.getNum("GPRS.Channels.Min",0);
	//if (minchans > 0) {
	//	for (int i = 0; i < minchans && i < 8; i++) {
	//		if (!macAddChannel()) break;
	//	}
	//}

	if (! macSingleStepMode) {
		macRunning = true;		// set this first to avoid an unlikely race condition since
								// the lock above is released before thread starts.
		macThread.start(macThreadFunc,this);
	}
	GLOG(INFO) << "GPRS service thread started";
	return true;
}

// External entry point.
void gprsStart() { gL2MAC.macStart(); }

// This is for debugging and does not try to kill off MS and TBF,
// in fact, we probably want to leave those alone for post-mortem examination.
void L2MAC::macStop(bool channelstoo)
{
	ScopedLock lock(macLock);	// prevents a RACH from interrupting us.
	if (macRunning) {
		macStopFlag = true;
		macThread.join();
	}

	// Cant just delete the channels while the serviceloop is running or we crash.
	if (channelstoo) {
		for (int sanitychk = 0; sanitychk < 20 && macActiveChannels(); sanitychk++) {
			if (! macFreeChannel()) break;
		}
	}
	GPRSLOG(1) << "macStop successful\n";
}



L1UplinkReservation::L1UplinkReservation()
{
	ScopedLock lock(mLock);	// Overkill.  We dont need to lock this, no one is using it yet.
	RLCBlockReservation *rp = &mReservations[0];
	for (int i = mReservationSize-1; i >= 0; i--,rp++) { rp->mrBSN = -1; }
}

void mac_debug()
{
	//PDCHL1FEC *pdch;
	//RN_MAC_FOR_ALL_PDCH(pdch) {
	//	pdch->debug_test();
	//}
}



// Find an available RadioBlock on this uplink.
// If restype indicates RRBP, also return the RRBP
// (Relative Reserved Block Period) GSM04.60 10.4.5.
// The RRBP can specify reservations 3 - 6 BSN periods in the future:
// rrbp: -1: invalid; 0: 3 BSN; 1: 4 BSN; 2: 5 BSN; 3: 6 BSN.
// Otherwise, make a reservation after afterBSN and return the reserved absolute BSN
// as an integer, or -1 on failure.
RLCBSN_t L1UplinkReservation::makeReservationInt(
	RLCBlockReservation::type restype, RLCBSN_t afterBSN, TBF *tbf,
	RadData *rd,
	int *prrbp,		// RRBP 0..3 returned here.
	MsgTransactionType mttype)
{
	ScopedLock lock(mLock);
	RLCBSN_t bsn, first, lastplus1;
	// On my Toshiba the BTS and radio are becoming desynchronized to the point
	// where the uplink blocks arrive at the same time as the downlink
	// blocks are sent!  When this happens the RRBP reservations are not far
	// enough in advance to be answered.  To fix that, use a minimum RRBP
	// greater than 0.
	int minrrbp = gConfig.getNum("GPRS.RRBP.Min");
	if (tbf) {
		// Count the reservations for reporting purposes.
		switch (restype) {
		case RLCBlockReservation::ForPoll: tbf->mtMS->msCountCcchReservations.addTotal(); break;
		case RLCBlockReservation::ForRRBP: tbf->mtMS->msCountRbbpReservations.addTotal(); break;
		default: break;
		}
		// This I/O is so stupid...
		if (rd) {
			GPRSLOG(1) << "makeReservation"<<tbf<<tbf->mtMS <<LOGVAR(restype)<<LOGVAR(afterBSN)
				<<",fn="<<(afterBSN.valid() ? afterBSN.FN() : 0) << LOGVAR(mttype)
				<< LOGVAR(rd->mRSSI) << LOGVAR(rd->mTimingError);
		} else {
			GPRSLOG(1) << "makeReservation"<<tbf<<tbf->mtMS <<LOGVAR(restype)<<LOGVAR(afterBSN)
				<<",fn="<<(afterBSN.valid() ? afterBSN.FN() : 0) << LOGVAR(mttype);
		}
	} else {
		if (rd) {
			GPRSLOG(1) << "makeReservation" <<LOGVAR(restype)<<LOGVAR(afterBSN)
				<<",fn="<<(afterBSN.valid() ? afterBSN.FN() : 0) << LOGVAR(mttype)
				<< LOGVAR(rd->mRSSI) << LOGVAR(rd->mTimingError);
		} else {
			GPRSLOG(1) << "makeReservation" <<LOGVAR(restype)<<LOGVAR(afterBSN)
				<<",fn="<<(afterBSN.valid() ? afterBSN.FN() : 0) << LOGVAR(mttype);
		}
	}

	if (restype == RLCBlockReservation::ForRRBP) {
		first = gBSNNext + (3 + minrrbp);
		lastplus1 = gBSNNext + (3 + 4);	// one past the last.
	} else {
		first = (afterBSN.valid() ? afterBSN : gBSNNext);
		lastplus1 = (gBSNNext + mReservationSize - 1);
	}
	first.normalize();	// Be sure.
	lastplus1.normalize();	// Be sure.
	int rrbp = minrrbp;
	for (bsn = first; bsn != lastplus1; bsn=bsn+1, ++rrbp) {
		// DEBUG: The RRBP is not being answered, so try increasing the time advance:
		//if (GPRSDebug) { if (rrbp < 2) continue; }
		RLCBlockReservation *rp = &mReservations[bsn % mReservationSize];
		if (rp->mrBSN == bsn) { continue; } // This block is already reserved.
		if (gFixIdleFrame && (bsn & 1)) {
			// The FEC is going to send USF only on the even frames,
			// which means uplink data blocks arrive only on odd frames,
			// so the even frames are guaranteed to be empty of uplink.
			// So only make reservations for the odd frames.
			continue;
		}
		rp->mrType = restype;
		rp->mrSubType = mttype;
		rp->mrBSN = bsn;
		rp->mrTBF = tbf;
		if (rd) { rp->mrRadData = *rd; }
		GPRSLOG(1) << "    reservation result:"<<LOGVAR(bsn)<<LOGVAR(bsn.FN())<<LOGVAR(rrbp)<<"\n";
		if (prrbp) { *prrbp = rrbp; }
		mac_debug();
		return bsn;
	}
	GPRSLOG(1) << "   reservation failure"<<LOGVAR(first)<<LOGVAR(lastplus1)<<"\n";
	mac_debug();
	return -1;	// Abject failure.
}


// We need to make a block uplink reservation on CCCH, so we need to know exactly
// when the AGCH message is going to be transmitted.
// DAB GPRS - That will happen if the CCCH just came out of an idle period.
// DAB GPRS - A better way to fix this is to call AGCH->resync() 
// DAB GPRS - inside of getNextMsgSendTime().
// pat - Done, and it worked.

// 12 blocks is one 52-multiframe.  For now, add an extra multframe
// to the AGCH load to make sure it is in the future.
// Update: This seems to be too far in the future for the Multitech Modem.
// TODO: This could be reduced down to a few blocks in the future.
// Update: 12 blocks in the future did not work, trying 24.  Now trying 36.
// Return the reservation time
// TODO: The DRX mode needs to know which channel the MS is on, based on IMSI.  see GSM05.02 6.5
RLCBSN_t L1UplinkReservation::makeCCCHReservation(
	CCCHLogicalChannel *AGCH,
	RLCBlockReservation::type type, TBF *tbf, RadData *rd,
	bool forDRX,
	MsgTransactionType mttype)	// The sub-state that this reservation is for.
{
	mac_debug();
	// TODO: Add a separate GPRS qmax, since it seems like MS cant handle much delay.
	int qmax = gConfig.getNum("GSM.CCCH.AGCH.QMax");
	if (qmax > 0 && AGCH->load()>(unsigned)qmax) {
		if (type == RLCBlockReservation::ForRACH) {
			GPRSLOG(1) << "RACH dropped due to AGCH congestion.\n";
		} else if (type == RLCBlockReservation::ForPoll) {
			GLOG(INFO) << "CCCH congestion prevented Packet Downlink Assignment Message";
		}
		return RLCBSN_t(-1);	// invalid.
	}

	RLCBSN_t resbsn;	// BSN of the immediate assignment uplink reservation.
	// advanceblocks is time between MS receiving reservation and reacting.
	int advanceblocks = 4; // We will default to 4.
	Time sendTime = AGCH->getNextMsgSendTime();
	unsigned fn = sendTime.FN();

	if (1) {
		// new way:
		// Converting to an RLC block rounds down:
		resbsn = FrameNumber2BSN(fn);
		// We have to give the MS a little time to respond.
		// GSM05.10 sec 6.11.1, and I quote:
		// "If the MS is required to transmit a PACKET CONTROL ACKNOWLEDGEMENT subsequent
		// to an assignment message (see 3GPP TS 04.60), the MS shall be ready to 
		// transmit and receive on the new assignment no later than the
		// next occurrence of block B((x+2) mod 12) where block B(x) is
		// radio block containing the PACKET CONTROL ACKNOWLEDGEMENT."
		if (gConfig.defines("GPRS.MS.ResponseTime")) {
			advanceblocks = gConfig.getNum("GPRS.MS.ResponseTime") + ExtraClockDelay;
		}

		// We also have to add one to compensate for FrameNumber2BSN rounding down.
		resbsn = resbsn + advanceblocks + 1;
		if (forDRX) {
			// FIXME TODO: Fix this total hack.
			// We dont know which paging channel, so make sure the reservation is beyond any of them.
			// The BS_PA_MFRMS number of 51-multiframes used for paging is
			// advertised in the beacon as 2.
			// TODO: This magic number should not be hard-coded here.
			resbsn = resbsn + 22;	// Should be enough.  Note: Must be even for gFixIdleFrame.
		}
	} else {
		// old way that worked:

		// For debugging, add a variable advance amount.
		//int advanceframes = gConfig.getNum("GPRS.advanceframes",0);
		advanceblocks = gConfig.getNum("GPRS.advanceblocks");	// This worked!

		resbsn = gBSNNext + (int32_t)(12 * AGCH->load() + advanceblocks);	// This is in blocks.
	}
	mac_debug();
	return makeReservationInt(type,resbsn,tbf,rd,NULL,mttype);
}

// Make an RRBP reservation if possible.  Return the rrbp (range 0 to 3), or -1 if failed.
RLCBSN_t L1UplinkReservation::makeRRBPReservation(TBF *tbf,
	int *prrbp,		// The RRBP result, 0..3
	MsgTransactionType mttype)	// The sub-state that this reservation is for.
{
	return makeReservationInt(RLCBlockReservation::ForRRBP,-1,tbf,NULL,prrbp,mttype);
}

// Return the reservation for the specified block timeslot, or NULL if none.
// Return true if found, and return TFI in *TFI.
// bsn can be in the past or future.
RLCBlockReservation *L1UplinkReservation::getReservation(RLCBSN_t bsn)
{
	ScopedLock lock(mLock);
	RLCBlockReservation *rp = &mReservations[bsn % mReservationSize];
	if (rp->mrBSN == bsn) { return rp;}
	return NULL;
}
	
// If TBF is NULL, clear the reservation.
// If a TBF is specified, only clear the res if it is for that TBF.
void L1UplinkReservation::clearReservation(RLCBSN_t bsn, TBF *tbf)
{
	ScopedLock lock(mLock);
	RLCBlockReservation *rp = &mReservations[bsn % mReservationSize];
	if (tbf && rp->mrTBF != tbf) { return; }
	rp->mrBSN = -1;
	rp->mrTBF = NULL;	// not necessary, but lets be neat.
}

// See if this block the MS sent to us corresponds to a reservation,
// and if so, update the counters in the corresponding TBF.
RLCBlockReservation::type L1UplinkReservation::recvReservation(
	RLCBSN_t bsn,	// The BSN of a received block.
	TBF**restbf,		// Return tbf specified by reservation here, just for debugging.
	RadData *prd,	// Return radio data here.
	PDCHL1FEC *ch)	// Implicit in this pointer, but easier to pass it.
{
	ScopedLock lock(mLock);	// This lock is probably no longer necessary.
	RLCBlockReservation::type result = RLCBlockReservation::None;
	RLCBlockReservation *rp = &mReservations[bsn % mReservationSize];
	*restbf = 0;
	if (rp->mrBSN == bsn) {
		result = rp->mrType;
		if (prd) { *prd = rp->mrRadData; }
		if (rp->mrTBF) {
			devassert(rp->mrType != RLCBlockReservation::ForRACH);
			TBF *rtbf = rp->mrTBF;
			GPRSLOG(1) << "recvReservation " <<rp->mrType <<LOGVAR2("ttype",rp->mrSubType)
				<<rtbf <<rtbf->mtMS <<LOGVAR(bsn) <<" "<<ch;
			*restbf = rtbf;
			rtbf->mtRecvAck(rp->mrSubType);
			switch (rp->mrType) {
			case RLCBlockReservation::ForPoll: rtbf->mtMS->msCountCcchReservations.addGood(); break;
			case RLCBlockReservation::ForRRBP: rtbf->mtMS->msCountRbbpReservations.addGood(); break;
			default: break;
			}
		} else {
			GPRSLOG(1) << "recvReservation"<<rp->mrType<<" tbf=null" <<LOGVAR(bsn)<<" "<<ch;
		}
	} else {
		//GPRSLOG(1) << "recvReservation unrecognized"<<LOGVAR(bsn)<<"\n";
	}
	clearReservation(bsn,NULL); // Be tidy.
	return result;
}

void L1UplinkReservation::dumpReservations(std::ostream&os)
{
	os << "Reservations=(";
	RLCBlockReservation *res = &mReservations[0];
	for (int i = 0; i < mReservationSize; i++, res++) {
		if (res->mrBSN.valid()) {
			os << " ";
			os << res;
		}
	}
	os << ")\n";
}


// Return the USF of an MS that might want to use the uplink.
// TODO: Fix this not to worry about RRBP since I added makeReservation.
static
int findNeedyUSF(PDCHL1FEC *pdch)
{
	int usf;
	GPRSLOG(512) << "findNeedyUSF start for "<<pdch;

	if (extDyn.isUplinkReserved(pdch)) {
		GPRSLOG(512) << "findNeedyUSF extDyn.isUplinkReserved "<<pdch;
		return 0;
	}

	// In order to be fair to multiple MS on the same channel
	// we will look at all the USFs on this channel and pick the best one.
	TBF *besttbf = NULL;
	int bestusf;
	int bestage;	// how long since the tbf was issued a USF.

	// This is an unused uplink block.
	// Look around for an uplink TBF on this channel with data to send.
	for (usf = USFMIN; usf <= USFMAX; usf++) {
		MSInfo *ms = pdch->getUSFMS(usf);
		if (ms) {
			// This USF on this channel is in use by this MS.
			// Lets see if the MS wants to send something to us.
			TBF *tbf;
			RN_MS_FOR_ALL_TBF(ms,tbf) {
				GPRSLOG(512) << "findNeedyUSF "<<ms <<" testing" <<tbf;
				if (tbf->mtDir != RLCDir::Up) continue;
				// We dont include state DataFinal, because then we have already
				// received all the blocks and are waiting for an RRBP reservation,
				// which does not need a USF.
				// We will include DataReassign to let an uplink proceed
				// during the reassignment process.
				TBFState::type tstate = tbf->mtGetState();
				if (tstate != TBFState::DataTransmit &&
				    tstate != TBFState::DataReassign) continue;

				// At this point, we know the TBF wants to use this uplink slot.

				// Does the tbf have both uplink and downlink on this channel?
				// We would not have allocated the USF otherwise, so assert.
				unsigned tn = pdch->TN();
				devassert(ms->msCanUseUplinkTn(tn));
				devassert(ms->msCanUseDownlinkTn(tn));

				// For extended dynamic we must reserve all the uplink channels simultaneously.
				if (ms->isExtendedDynamic()) {
					// Make sure this is the first channel of the allocation.
					// (This is only necessary if there are multiple down channels,
					//  which only occurs for a 2-down/3-up config.)
					// We keep the lists sorted to facilitate this test.
					if (tn != ms->msPCHUps.front()->TN()) {goto nextusf;}
					PDCHL1Uplink *up2;
					RN_FOR_ALL(PDCHL1UplinkList_t,ms->msPCHUps,up2) {
						// The USF in downlink block N reserves uplink block N+1
						if (up2->parent()->getReservation(gBSNNext + 1)) {
							// The extended dynamic TBF cannot use the uplink
							// if there is a reservation on any of its uplink channels.
							// If the extended dynamic tbf has exclusive use of
							// all its channels, or at least, if there are no other
							// PACCH sharing any channels, this will not happen except
							// on the PACCH of this ms, because there could be multiple
							// extended dynamic tbfs sharing the same PACCH.
							goto nextusf;
						}
					}
					// Success!  Reserve all the uplinks belonging to this tbf.
					// According to the spec, we only have to transmit the USF
					// on the first downlink for the tbf, which will happen below.
					// So all we have to is reserve the channels, we dont need
					// to save the usf.  Doesnt matter if we reserve the current
					// timeslot since we are already past the test, so just do all.
					RN_FOR_ALL(PDCHL1UplinkList_t,ms->msPCHUps,up2) {
						extDyn.reserveUplink(up2);
					}
					// Fall through to return the usf for this tbf.
				}


				// If an uplink tbf is stalled, the MS is waiting for an acknak
				// from the network, so there is not much point in giving it an uplink USF.
				// An intereseting point is that it might get its acknack before
				// this downlink block gets to it, so maybe we should go ahead and
				// give it a usf anyway, but I didnt.  It might get the usf below, anyway.
				// 6-7-2012: Above is WRONG.  The MS will continue sending the old
				// blocks and it needs USF to do so; without them it will hang forever.
				//if (tbf->stalled()) continue;

				int thisage = gBSNNext - ms->msLastUsfGrant;
				GPRSLOG(512) << "findNeedyUSF for "<<ms <<LOGVAR(usf)<<LOGVAR(thisage)<<tbf;
				if (besttbf) {
					// We want to keep the TBF with the longest age.
					if (thisage < bestage) continue;
				}
				// This is the most needy TBF encountered so far.
				besttbf = tbf;
				bestusf = usf;
				bestage = thisage;
			}
		}
		nextusf:;
	}

	if (besttbf) {
		// In state DataReassign, dont penalize the MS for not listening,
		// because we dont know if it is still on this chan or not.
		besttbf->mtMS->msCountUSFGrant(besttbf->mtGetState() == TBFState::DataTransmit);
		GPRSLOG(4)<<LOGVAR(bestusf)<<LOGVAR(besttbf)<<"\n";
		return bestusf;
	}

	// 06-07-2012: Dont send USFs that we dont know exactly what they are doing.
	// Formerly I allocated the USF to any one of the MSs that still have a USF on this channel.
	// But if a TBF gets cancelled due to cause=3101 and we keep sending the USF,
	// then the MS becomes confused and the iphone even deactivated its
	// pdp context and gave up on us.
	return 0;
}

static bool disabledByDutyFactor()
{
		// 4-23-2012: For testing, try throttling down the uplink to
		// leave empty USFs.  The downlink is failing sometimes,
		// so the point of this to see if this helps.
		// Update: This made no difference, and if you
		// set the duty factor less than 50%, the Blackberry says
		// unable to connect to internet.
		//block->mUSF = findNeedyUSF(pdch);
		int dutyfactor = configGetNumQ("GPRS.UplinkDutyFactor",100);
		int nope = false;
		if (dutyfactor < 0) { 		// If it is zero, it is probably just an sql goof.
			GLOG(ERR) << "Invalid GPRS.UplinkDutyFactor:"<<dutyfactor;
		} else if (dutyfactor <= 25) { 		// 1 in 4
			if (4*((int)gBSNNext/4) != gBSNNext) {nope = true;}
		} else if (dutyfactor <= 34) {	// 1 in 3
			if ((3*((int)gBSNNext/3)) != gBSNNext) {nope = true;}
		} else if (dutyfactor <= 50) {	// 1 in 2
			if (gBSNNext & 1) {nope = true;}
		} else if (dutyfactor <= 75) {	// 3 in 4
			if (4*((int)gBSNNext/4) == gBSNNext) {nope = true;}
		}
		return nope;
}


// Set the RRBP and USF fields for a downlink block, either data or message.
// If tbf is non-NULL, notify it if block is passed, which means it will be sent.
// makeres is: 0 = no res, 1 = optional res, 2 = required res.
// Return true if it is ok to send the block.
// Note: the USF field specifies the use of the next higher numbered return block,
// which will actually be sent 2 blocks after this one goes out, due to the staggering
// of incoming and outgoing block numbers.
bool setMACFields(MACDownlinkHeader *block, PDCHL1FEC *pdch, TBF *tbf,
	int makeres,				// 0 = no res, 1 = optional res, 2 = required res.
	MsgTransactionType mttype,	// Type of reservation
	unsigned *pcounter)			// If non-NULL, incremented if a reservation is made.
{
	int rrbp = -1;
	// Update: This block may be sent multiple times if an RLC data block is resent.
	// Therefore we must init it thoroughly:
	block->mSP = 0;
	block->mUSF = 0;
	if (makeres) {
		mac_debug();
		RLCBSN_t bsn = pdch->makeRRBPReservation(tbf,&rrbp,mttype);
		if (bsn.valid()) {
			if (tbf) { tbf->mtSetAckExpected(bsn,mttype); }
		} else {
			devassert(rrbp == -1);
			if (makeres == 2) { return false; } // Caller will try again later.
		}
	}
	if (rrbp != -1) {
		block->setRRBP(rrbp);
		if (pcounter) (*pcounter)++;
	}

	// GSM 05.02 6.3.2.2.1: The USF field in downlink block N signals
	// that uplink block (N+1) is assigned to that MS.

	if (! pdch->getReservation(gBSNNext + 1) &&
		! disabledByDutyFactor() &&
		! extDyn.isUplinkReserved(pdch)) {
		block->mUSF = findNeedyUSF(pdch);
		// Remember the usf sent on this channel at time gBSNNext
		pdch->setUsf(block->mUSF,gBSNNext);	// ok to be 0.
	} else {
		pdch->setUsf(0,gBSNNext);	// ok to be 0.
	}
	return true;
}


// WARNING: This small function runs in a different thread than the rest of the GPRS code.
void GPRSProcessRACH(unsigned RA,
	const GSM::Time &when,
	float RSSI, float timingError)
{
	if (! GPRSConfig::IsEnabled()) return;
	ChIdleCounter = 0;
	GPRSLOG(1) << "Received RACH"<<LOGVAR(RA)<<LOGVAR(when)<<LOGVAR(RSSI)<<LOGVAR(timingError)<<"\n";
	Stats.countRach++;
	gReports.incr("GPRS.RACH");

	RachInfo *rip = new RachInfo(RA,when,RadData(RSSI,timingError));
	gL2MAC.macRachQ.write(rip);

	if (! gL2MAC.macStart()) {
		GPRSLOG(1) << "MAC failed to init!\n";
	}
}


// GSM 4.08 sec3.5.2
// Return an L3RRMessage to be sent on the AGCH or NULL.
// The returned msg is a GSM RR message because the MS is still listening to the
// beacon in packet-idle-mode, so this msg is sent on CCCH.
// We dont even know what MS we are talking to yet; we dont have a TMSI.
// Timers:
// T3146 in MS, started when MS sends its maximum number of RACH (M),
// stopped when it gets Immediate Assignment (or I.A. Reject)
// MS gives up when it expires.
// Min value T+2*S (S and T defined in 3.3.1.1.2: T is Tx-integer broadcast
// on BCCH, S defined in GSM04.08 table 2.1)
// Max value 5 secs.
// T3141 in network started when Immediate Asignment sent (which is unacknowledged mode -
// note that it is for a single block access so the receipt of that block is the acknowledgement.)
// when it expires.
// Power control in GSM03.64 sec6.5.8
void RachInfo::serviceRach()
{
	GPRSLOG(1) << "MAC serviceRACH\n";
	ChIdleCounter = 0;
	CCCHLogicalChannel *AGCH = gBTS.getAGCH();
	/** code moved to makeCCCHReservation()
	//int qmax = gConfig.getNum("GSM.CCCH.AGCH.QMax");
	//if (qmax > 0 && AGCH->load()>(unsigned)qmax) {
	//	GPRSLOG(1) << "RACH dropped due to AGCH congestion.\n";
	//}
	***/

	// 5-24-2012: Each MS must have a single requestCh assigned.
	// Since we dont know what MS rached us at this point, that
	// implies we either have to use only one channel as the PAACH
	// for all MS, or we would have to change the MS channel once
	// we get a response back from it.  For now, the former.
	// When this was a random channel, all kinds of problems occurred.
	PDCHL1FEC *chan = gL2MAC.macPickChannel();	// pick the least busy channel;
	//PDCHL1FEC *chan = gL2MAC.macPDCHs.front();	// First channel is our PAACH.
	if (chan == NULL) {
		GPRSLOG(1) << "MAC serviceRACH failed to find available channel!\n";
		return;
	}

	RLCBSN_t RBN;	// check .valid for error.
	switch (mRA & 0xf8) {
	case 0x78: {	// MS requests an uplink TBF.
		// GSM04.18 sec3.5.2.1.3.1 says: if one phase access is requested,
        // the network may grant either a one phase access or a single block
        // packet access, which forces the MS to do a two phase access.
		// In order to implement single-phase access, we would have to implement code
		// to identify the MS using one of the contention resolution methods
		// in GSM04.60 sec7.1.2.3, and detect failures via timeouts, making
		// all the state machines more complicated, so we wont.
        // So fall through to Single Phase Access...

		/***
		int TFI = chan->downlink()->allocateTFI();
		if (TFI >= 0) {
			const GSM::L3RequestReference &reqref,	// Specifies when the RACH burst occurred.
			result = new L3ImmediateAssignment(
						GSM::L3RequestReference(rachinfo->RA,rachinfo->when)
						chan->uplink->packetChannelDescription(),
						GSM::L3TimingAdvance(GetTimingAdvance(timingError)), true);
			result->packetAssign()->setPacketUplinkAssignDynamic(TFI,CSNum);
			return result;
		}
		// Else, unlikely case of all TFIs in use.  Fall through to single phase access.
		****/
	}
	case 0x70: {	// MS requests a single uplink block.

		RBN = chan->makeCCCHReservation(AGCH,RLCBlockReservation::ForRACH,NULL,&mRadData,0,MsgTransNone);

		Time now = gBTS.time();

		if (! RBN.valid()) {
			// Abject failure.  This is probably due to AGCH congestion,
			// and we printed one message already.
			GPRSLOG(1) << "serviceRACH failed to make a reservation at"
				<<LOGVAR(gBSNNext) <<LOGVAR(now);
			// The MS may try another RACH for us again later,
			// or give up and try some other cell that it can also hear.
			return;
		}
		GPRSLOG(1) << "serviceRACH at "<<LOGVAR(gBSNNext) <<LOGVAR(now) 
			<<" with reservation at "<<LOGVAR(RBN) <<"=" <<RBN.FN() << " frames.";

		// GSM 4.08 3.5.2.1.3.1: The immediate assignment message includes
		// the frame when the RACH was received (in reqref)
		// 11-22 NOTE!  The downlink must be true.  If you set it to false,
		// the multitech modem simply fails to respond.
		GSM::L3ImmediateAssignment result(
					GSM::L3RequestReference(mRA,mWhen),
					chan->packetChannelDescription(),
					GSM::L3TimingAdvance(GetTimingAdvance(mRadData.mTimingError)),
					true,false);	// tbf, downlink


		// The immediate assignment has a TFI, but we do not set it for a single block assignment.
		L3IAPacketAssignment *pa = result.packetAssign();
		pa->setPacketUplinkAssignSingleBlock(RBN.FN());

		// We dont know what MS we are talking to, so set the power params to global defaults.
		// We could fiddle with the power gamma based on RSSI, but not implemented,
		// and probably unnecessary.  It is not really necessary to change these at all here.
		pa->setPacketPowerOptions(GetPowerAlpha(),GetPowerGamma());

		GPRSLOG(1) << "GPRS serviceRACH sending L3ImmediateAssignment:" << result;
		AGCH->send(result);
		break;
	}
	default:
		devassert(0);
	}
}

void L2MAC::macServiceRachQ()
{
	RachInfo *rip;
	while ((rip = macRachQ.readNoBlock())) {
		GPRSLOG(1) << "GPRS: servicing RACHQ";
		LOGWATCHF("RACH %s\n",strrchr(timestr().c_str(),':')+1);
		// TODO: Check the burst age again, in case start of GPRS service was delayed.
		// If the RACH is too old, discard it.

		if (!macActiveChannels()) {
			// No GPRS channels allocated; attempt to get one.
			macAddChannel();
		}
		if (!macActiveChannels()) {
			GPRSLOG(1) << "GPRS: RACHQ failed to allocate channels";
			// Failed to allocate any channels at all.
			// Toss the RACH.  The MS will just have to try again later.
			//macRachQ.write(rip);	// We reordered the RACHs, but should not matter.
		} else {
			rip->serviceRach();
		}
		delete rip;
	}
}

// The RLC block just arrived on pdch.  Figure out to which TBF it belongs and
// pass it on to the RLCEngine for assembly into a PDU.
static void processRLCUplinkDataBlock(PDCHL1FEC *pdch, RLCRawBlock *src,TBF *restbf)
{
	RLCUplinkDataBlock *rb = new RLCUplinkDataBlock(src);

	// If this flag is set, data blocks are supposed to arrive
	// only on odd numbered blocks
	if (gFixIdleFrame && (0==(src->mBSN&1))) {
		GPRSLOG(1) << "@@@OOPS: Even numbered uplink data block:"<<src->mBSN;
	}

	// Reassociate the block with the TBF to which it belongs:
	//GPRSLOG(1) << "### Uplink Data Block tfi="<<rb->mTFI<< " bsn="<<src->mBSN;
	TBF *tbf = pdch->getTFITBF(rb->mTFI,RLCDir::Up);
	if (tbf) {
		// The rd data is from the RACH, and is pretty old;
		// Use the new RSSI from RLCRawBlock.
		//if (rd.mValid) {
		//	tbf->mtMS->setRadData(rd);
		//	tbf->setRadData(rd);
		//}
		// In the calling function processUplinkBlock we looked up the TBF by USF
		// and already counted it.  Here we are looking up the TBF by TFI.
		// I am leaving in both calls in case there is a problem, but we wont
		// double count the block for reporting purposes.
		tbf->mtMS->setRadData(src->mRD);
		tbf->mtMS->talkedUp(true);	// Mark time, but dont double count.

		//GPRSLOG(1) << "### Uplink Data Block tfi="<<rb->mTFI
		//	<<" tbf="<<tbf << " bsn="<<src->mBSN;
		if (GPRSDebug&2) {
			std::ostringstream oss;
			rb->RLCUplinkDataBlockHeader::text(oss);
			GPRSLOG(2) << "Uplink Data Block tfi="<<rb->mTFI
				<<" tbf=" <<tbf <<" bsn="<<src->mBSN <<tbf<<oss.str();
		}
		if (restbf && restbf != tbf) {
			GLOG(ERR) << "Incoming block reservation does not match "<<tbf<< LOGVAR(restbf);
		}
		tbf->engineRecvDataBlock(rb,pdch->TN());
	} else {
		//GPRSLOG(1) << "### Uplink Data Block with unknown tfi="<<rb->mTFI
		//	<< " bsn="<<src->mBSN;
		GLOG(WARNING) << "Uplink Data Block with TFI="<<rb->mTFI
			<< " bsn="<<src->mBSN <<" unassociated with TBF";
	}
}


// The MS is requesting an uplink TBF.
// Create the TBF, then go ahead and try to attach it right now.
// The MS may not have channels assigned yet.
// (Happens if this is the first time we have heard from this MS; it is the second
// part of a two phase uplink assignment.)
// LISTEN UP:  If there is already an uplink TBF running for this MS and you send a second
// Packet Uplink Assignment, the MS *immediately* starts using the new TFI
// for the in-progress TBF, which would cause that one to report a "fail".
// The data may or may not be lost, because if the TBF has not yet had any acknacks,
// the data will get through on the second TBF, else if there have been acknacks,
// the data is lost as far as we are concerned.
static void processUplinkResourceRequest(
	PDCHL1FEC *requestch,	// The channel the request arrived on.
	RLCMsgPacketResourceRequest *rmsg,
	RLCBlockReservation::type restype, RadData &rd)
{
	MSInfo *ms = rmsg->getMS(requestch,true);
	if (!ms) {
		// 3-20120: This happened if the MS tried to identify itself using a TFI,
		// but the TBF for that TFI is already deleted.  Just ignore it, nothing else we can do.
		GPRSLOG(1) << "UplinkResourceRequest for unidentified MS on ch:" << requestch;
		return;
	}
	ms->talkedUp();
	if (rd.mValid) {
		ms->setRadData(rd.mRSSI,rd.mTimingError);
	}
	// Store the channel quality report info.
	ms->msCValue.addPoint(rmsg->mCValue);
	if (rmsg->mSignVarPresent) { ms->msSigVar.addPoint(rmsg->mSignVar); } // Not present for two phase access.
	for (int tn=0; tn < 8; tn++) {
		if (rmsg->mILevelPresent[tn]) { ms->msILevel.addPoint(rmsg->mILevelTN[tn]); }
	}

	bool isRach = (restype == RLCBlockReservation::ForRACH);
	if (isRach) {
		// After an MS in PacketIdle mode contacts us, we send it a single
		// block uplink assignment, and this is its answer.
		// The MS will listen on this PDCH until T3168 expires.
		// Regardless of what we do about this RACH.
		// TODO: Should we only set this if the purpose of the rach is an assignment,
		// not for measurements?
		// This timer blocks downlink assignments so its a performance issue.
		ms->msT3168.set();
	}

	// 6-8-2012: This is the previous code; now handled below.
	// // If there is an existing uplink tbf for this MS, ignore this request.
	// // We just drop it on the ground.
	// // FIXME TODO: That is not right - the uplink tbf request is probably changing
	// // the priority of the existing tbf, and we need to resend the (identical) assignment
	// // to make the MS happy, or it will kill off the uplink TBF after some timer.
	// TBF *existingtbf;
	// if (ms->msCountActiveTBF(RLCDir::Up, &existingtbf)) {
	// 	GPRSLOG(1) << ms << " dropping uplink request, duplicates:"<<existingtbf;
	// 	return;
	// }

	// The MS was found using the TLLI, if any.
	//UNNECESSARY:if (rmsg->mTLLIPresent) { ms->msTlli = rmsg->mTLLI; }

	// TODO LATER: We may want to assign more channels based on params in the rmsg.
	// If this uplink request was RACH initiated, we did not know the MS before
	// we got this message, so channel assignment was a shot in the dark.
	// We need to remember the requestch because the ms is already listening on it
	// as PACCH, and will continue to do so unless and until we tell it otherwise.
	if (ms->msPacch && ms->msPacch != requestch) {
		// This does not really matter.
		GPRSLOG(1) << ms <<" ch:"<<ms->msPacch<<" different from msg ch:"<<requestch;
	}

	// If the MS is calling us on RACH, the old channel assignments are at best
	// meaningless and at worst incorrect.
		// The MS RACHed while a TBF is active.
		// If we get here, the MS may have two channel assignments: the one
		// in the existing TBF, and the one that was assigned for the
		// single block assignment on which this channel request was sent.
		// 06-07: I saw this happen when the MS is in packet idle mode and we
		// have just sent it a DIA [downlink immediate assignment] on CCCH but
		// then we get a RACH before it can respond.
		// 04.18 3.5.2.1.2 says and I quote:
		// "If the mobile station receives an IMMEDIATE ASSIGNMENT message during
		// the packet access procedure indicating a packet downlink assignment procedure,
		// the mobile station shall abort the packet access procedure and respond to the
		// IMMEDIATE ASSIGNMENT message as specified in section 3.5.3.1.2.
		// The mobile station shall then attempt an establishment of uplink TBF,
		// using the procedure specified in GSM 04.60 which is applicable in packet transfer mode.
		// There is a paragraph above that appears to contradict, but it applies
		// only to the Packet Pause procedure.

		// In that case, we should just throw this Packet Resource Request away -
		// although we put the MS on this channel, it should move to the channel
		// specified by the immediate assignment when that start-time rolls around.  We hope.
		// If the TBF is in some other mode, this is a potential disaster.
		// 6-8: I am going to try to fix the mixup by immediately
		// reissuing a new assignment for the MS.  Update: That did not work.

		// But why did the MS rach us at all?
		// Before I supported CRD [Channel Request Description] in the acknacks, the MS had
		// to RACH during a long downlink to get our attention, but that is no longer necessary.
		// In one case on the iphone the phone started an uplink TBF using the CRD in a downlinkacknack,
		// but the uplink TBF was killed with cause=3101, and the iphone eventually RACHed for a new uplink TBF.
		// It continued responding to the downlink TBF right up until the RACH, but afterward the downlink
		// TBF was dead, and would not respond to sendreassign, but that may have
		// been because the sendreassigns were on a different channel than the
		// Packet Resource Request.
		// Maybe we dont have to cancel the downlink at all?

		// If the downlink is new, it may be the weird ignored case
		// because it may have started while the packetrequest was in flight.
		// In that case we should cancel the downlink TBF instead.
		// Update: I'm always cancelling.
	TBF *atbf;	// There could be both up and down tbfs, so check both.
	bool ignore = 0;
	if (isRach && ms->msCountActiveTBF(RLCDir::Down, &atbf)) {
		GLOG(ERR) << ms <<" RACH while TBF transmitting:"<<atbf << " cancelled";
		// We should differentiate the case where the downlink happened via
		// immediate assignment that passed the RACH in flight.
		// The network sends the One Block Assignment and the DIA
		// with long lead times, so they may arrive in either order.
		// If they arrive in this order:
		// MS ----> RACH, anonymous -->
		// 		Immediate downlink assignment with TLLI <--- Network sends
		// 		One block assignment for RACH, anonymous <-- Network sends
		// MS <-- downlink assignment
		// MS ---> ack to downlink --->
		// MS <-- One block assignment, ignored.
		// MS -- XXX --> No response to one block assignment.
		// Then the MS should not send the PacketResourceRequest, so if we
		// get here with a downlink tbf in transmit state, it is because
		// the TBF failed and the MS is retrying.
		// If they arrive in this order:
		// MS <-- one block assignment
		// MS --> Packet Resource Request
		//			After sending Packet Resource Request MS leaves "Packet Idle Mode".
		// MS <-- Immediate Assignment.
		// Then the downlink TBF will be in DataWaiting1 mode.
		// We have two choices:
		// o We could pretend we did not hear this Packet Resource Request
		// and wait for the Immediate Assignment to arrive, but I dont think that will work
		// o We establish the uplink TBF on PACCH and just let the SendAssignment code
		// wait for the reservation time for this Downlink Immediate Assignment message to pass,
		// after which that code will try again on PACCH.

		// We dont find out they are the same MS until now.
		// The safe thing to do is cancel the immediate assignment and send a new
		// assignment on the new PACCH.
		// In any case, this sucks, because we did not deliver the downlink TBF reliably -
		// unless I kept track of the TBFs that have been fully acked and resend the others?
		//atbf->mtSetState(TBFState::DataReassign);
		if (atbf->isTransmitting()) {
			// I think we can retry the TBF immediately, but until that is proved,
			// we will wait for the timeout before retrying.
			atbf->mtCancel(MSStopCause::Rach,TbfRetryAfterWait);
		}
		ignore = true;
		GLOG(WARNING) << ms <<" Ignoring RACH while TBF active:"<<atbf;
	}

	if (ms->msCountTransmittingTBF(RLCDir::Up, &atbf)) {
		if (isRach) {
			// I dont know if we should kill this TBF or not.
			// Before I supported uplink requests in downlinkacknack I thought the Blackberry was using
			// RACH to request new uplink TBFs but there were still so many bugs then that I'm not sure.
			atbf->mtCancel(MSStopCause::Rach,TbfRetryInapplicable);
			GLOG(WARNING) << ms <<" RACH while uplink TBF transmitting, TBF cancelled:"<<atbf;
			// This is an abnormal condition.
			// For safety sake we are going to ignore this rach and make the
			// MS do another one.
			ignore = true;
			GLOG(WARNING) << ms <<" Ignoring RACH while TBF active:"<<atbf;
		} else {

			// New: Fall through to call newUpTbf
			//	// A Packet Resource Request during un uplink TBF is a request to
			//	// change something about an existing TBF, eg, priority.
			//	// We are required to send a new reassignment within some time period
			//	// or the MS will unilaterally drop the connection.
			//	// Dont count state DataFinal, because the TBF is already ending by then.
			//	if (atbf->mtGetState() != TBFState::DataFinal) {
			//		GLOG(INFO) << ms <<" Uplink TBF reassignment request delayed until current TBF finished:"<<atbf;
			//		ms->msSetUplinkRequest(rmsg->mCRD);
			//	} else {
			//		atbf->mtSetState(TBFState::DataReassign);
			//		GLOG(INFO) << ms <<" Uplink TBF reassignment request: "<<atbf;
			//	}
			//	return;
		}
	}

	if (ignore) { return; }

	// We must always use the most recent PACCH.
	// FIXME TODO: What do we do if the old tbf is still in datafinal state?
	// In that state it is still using the old PACCH for the final acknack...
	if (isRach) {
		ms->msDeassignChannels();
		ms->msPacch = requestch;
	}

	// Allocate a new uplink TBF.
	// If a tlli was present in the message we must send it to the sgsn for the one
	// special case where an ms loses registration and tries to re-register;
	// in this special case the request tlli will not match the msTlli because
	// the ms has already undergone the change tlli procedure.
	uint32_t tlli = rmsg->mTLLIPresent ? (uint32_t) rmsg->mTLLI : (uint32_t) ms->msTlli;
	TBF *tbf = TBF::newUpTBF(ms,rmsg->mCRD,tlli,true);
	if (tbf == NULL) return;
	//tbf->mtUnAckMode = rmsg->mCRD.mRLCMode;

	GPRSLOG(1) <<"UplinkResourceRequest" <<LOGHEX(rmsg->mTLLI) <<ms
		<<tbf <<" rlcmode=" <<tbf->mtUnAckMode
		<<" count tbfs=",ms->msCountActiveTBF(RLCDir::Up);
}

static void processResourceRequest(
	PDCHL1FEC *pdch,RLCMsgPacketResourceRequest* rmsg,
	RLCBlockReservation::type restype, RadData &rd)
{
	int accessType = rmsg->mAccessTypePresent ? (int) rmsg->mAccessType : 0;
	//GPRSLOG(1) << "processResourceRequest"<<LOGVAR(accessType);
	GPRSLOG(1) << "processResourceRequest "<<rmsg->str();
	// TODO: This switch is not very interesting...
	switch (accessType) {	// GSM04.60 table 11.2.16.2
		case 0:	// Two Phase Access Request [second half of it]
		case 3: // Mobility Management Procedure.
		case 1: // Page Response
		case 2: // Cell Update
			processUplinkResourceRequest(pdch,rmsg,restype,rd);
			break;
	}
}

static void processDownlinkAckNack(PDCHL1FEC *pdch,RLCMsgPacketDownlinkAckNack* rmsg,RadData &rd)
{
	int tfi = rmsg->getTFI();
	TBF *tbf = pdch->getTFITBF(tfi,RLCDir::Down);
	if (tbf) {
		MSInfo *ms = tbf->mtMS;
		ms->talkedUp();
		ms->setRadData(rd);
		// Store the Channel Quality Report for reporting purposes.
		ms->msCValue.addPoint(rmsg->mCQR.mCValue);
		ms->msRXQual.addPoint(rmsg->mCQR.mRXQual);
		ms->msSigVar.addPoint(rmsg->mCQR.mSignVar);
		for (int tn = 0; tn < 8; tn++) {
			// Dont bother to differentiate this per timeslot.
			if (rmsg->mCQR.mHaveILevel[tn]) {ms->msILevel.addPoint(rmsg->mCQR.mILevel[tn]);}
		}

		tbf->engineRecvAckNack(rmsg);	// process the ack/nack part of the msg.
		if (rmsg->mHaveChannelRequest) { // Process the channel request, if any.
			TBF *uptbf = TBF::newUpTBF(ms,rmsg->mCRD,ms->msTlli,false);

			if (uptbf) {
				GPRSLOG(1) <<"Uplink TBF from downlink AckNack" <<ms
					<<tbf <<" rlcmode=" <<tbf->mtUnAckMode
					<<" count tbfs=",ms->msCountActiveTBF(RLCDir::Up);
			}
		}
	} else {
		GPRSLOG(1) <<"ERROR: downlinkacknack for "<<LOGVAR(tfi)<<" with no TBF!";
	}
}

static void uplinkCommon(uint32_t tlli, RadData &rd, const char *culprit)
{
	MSInfo *ms = gL2MAC.macFindMSByTlli(tlli,false);
	if (ms) {
		ms->talkedUp();
		ms->setRadData(rd);
	} else {
		GLOG(ERR) << "TLLI"<<LOGHEX(tlli)<<" in "<<culprit<<" message not found";
	}
}

static void processControlAcknowledgement(PDCHL1FEC*pdch, RLCMsgPacketControlAcknowledgement*rmsg,RadData &rd)
{
	// TODO: We should add a debug check to make sure the MS identified by this TLLI
	// is the same one in the reservation.
	GPRSLOG(1) << "processControlAck "<<rmsg->str();

	// We dont have to do anything; recvReservation informed the TBF.
	uplinkCommon(rmsg->mTLLI,rd,"ControlAck");
}

static void processUplinkDummy(PDCHL1FEC *pdch, RLCMsgPacketUplinkDummyControlBlock* rmsg, RadData &rd)
{
	GPRSLOG(1) << "processDummyUplink "<<rmsg->str();
	uplinkCommon(rmsg->mTLLI,rd,"DummyUplinkControl");
}

// The src block is the decoded uplink BitVector from the radio.
// The BitVector we are passed was allocated; delete when we are finished.
static void processUplinkBlock(PDCHL1FEC *pdch, RLCRawBlock *src)
{
	// Possibly handle timers. See: XCCHL1Decoder:handleGoodFrame();
	// TODO ...

	char buf[40];
	GPRSLOG(1) <<"-----> processUplinkBlock mac type="<<MACPayloadType::name(src->mmac.mPayloadType) << " ch:"<<pdch<<pdch->getAnsweringUsfText(buf,src->mBSN);
	RadData rd;
	TBF *restbf;
	RLCUplinkMessage::MessageType mtype;
	mac_debug();
	RLCBlockReservation::type restype = pdch->recvReservation(src->mBSN,&restbf,&rd,pdch);
	
	// Reset N3101.  We are doing it based on the presence of the burst rather.
	// Someday we should look inside the burst and make sure it really came from the MS we expected.
	// This code works for persistent (extended) uplink mode too; in that mode
	// the MS sends control blocks if it does not have data.
	// TODO: Can elide code still extant down alot of these branches that resets msN3101.
	int usf = pdch->getUsf(src->mBSN);
	if (usf != -1) {
		MSInfo *usfms = pdch->getUSFMS(usf);
		if (usfms) {
			usfms->msN3101 = 0;
			usfms->talkedUp();
		}
	}

	switch (src->mmac.mPayloadType) {
		case MACPayloadType::RLCData:
			if (restype != RLCBlockReservation::None) {
				// This happened alot before fixing the transceiverRAD1.
				GLOG(ERR) << "ERROR: Received reservation in RLC data block";
			}
			processRLCUplinkDataBlock(pdch,src,restbf);
			break;
		case MACPayloadType::RLCControl: {
			mtype = (RLCUplinkMessage::MessageType) src->mData.peekField(8,6);
			GPRSLOG(1) << "processUplinkMessage:" <<RLCUplinkMessage::name(mtype);

			RLCUplinkMessage* msg = RLCUplinkMessageParse(src);
			if (msg == NULL) {
				// 3-2012: NULL indicates error in message.
				// Already logged an error, so just ignore it here.
				return;
			}

			switch (msg->mMessageType) {
			case RLCUplinkMessage::PacketControlAcknowledgement:
				processControlAcknowledgement(pdch,(RLCMsgPacketControlAcknowledgement*)msg,src->mRD);
				break;
			case RLCUplinkMessage::PacketDownlinkAckNack:
				processDownlinkAckNack(pdch,(RLCMsgPacketDownlinkAckNack*)msg,src->mRD);
				break;
			case RLCUplinkMessage::PacketResourceRequest:
				processResourceRequest(pdch,(RLCMsgPacketResourceRequest*) msg,restype,src->mRD);
				break;
			case RLCUplinkMessage::PacketUplinkDummyControlBlock:
				processUplinkDummy(pdch,(RLCMsgPacketUplinkDummyControlBlock*)msg,src->mRD);
				break;
			default: // Just ignore unrecognized messages.
				GLOG(INFO) << "GPRS: Ignoring UplinkMessage:"
					<< RLCUplinkMessage::name(msg->mMessageType);
				break;
			} // switch msg->mMessageType

			delete msg;
			break;
		}
		case MACPayloadType::RLCControlExt: {
			mtype = (RLCUplinkMessage::MessageType) src->mData.peekField(8,6);
			GLOG(INFO) << "GPRS: Ignoring uplink RLCControlExt block, msgtype="<<mtype;
			break;
		}
		default:
			GLOG(INFO) << "GPRS: Ignoring block with payloadtype="<<src->mmac.mPayloadType;
			break;
	} // switch mac.mPayloadType
}


#if INTERNAL_SGSN==0
static void processBSSGMessages()
{
	BSSG::BSSGDownlinkMsg *dmsg;
	while ((dmsg = BSSG::gBSSG.BSSGReadLowSide())) {
		BSSG::BSPDUType::type msgtype = dmsg->getPDUType();
		switch (msgtype) {
		case BSSG::BSPDUType::DL_UNITDATA: {

			BSSG::BSSGMsgDLUnitData *dlmsg = new BSSG::BSSGMsgDLUnitData(dmsg); 
			GPRSLOG(1) << "BSSG <=== "<<dlmsg->str()<<timestr();
			// Note that the ByteVector from dmsg that contains the PDU
			// has been moved to dlmsg.
			//devassert(dlmsg->getRefCnt() == 2);
			delete dmsg;
			//devassert(dlmsg->getRefCnt() == 1);

			// Find or create this MS.
			MSInfo *ms = NULL;
			if (dlmsg->mbdHaveOldTLLI) {
				ms = bssgMSChangeTLLI(dlmsg->mbdOldTLLI,dlmsg->mbdTLLI);
			}
			if (!ms) { ms = gL2MAC.macFindMSByTlli(dlmsg->mbdTLLI, true); }
#if 0	// Code prior to internal sgsn
			ms->msDownlinkQueue.write(dlmsg);
#else
			SGSN::GprsSgsnDownlinkPdu *dlpdu = new SGSN::GprsSgsnDownlinkPdu(dlmsg->mbdPDU,ms->msTlli,0,"BSSGMsgDlUnitData");
			//devassert(dlmsg->getRefCnt() == 2);
			//dlpdu->mDlData = dlmsg->mbdPDU;
			//dlpdu->mDescr = "BSSGMsgDLUnitData";
			delete dlmsg;
			ms->msDownlinkQueue.write(dlpdu);
#endif

			// If the MS queue is too full, we should do flow control,
			// but the sgsn does not implement it yet, so not much point.
			break;
		}

		// TODO: Other BSSG messages:
		case BSSG::BSPDUType::RA_CAPABILITY:	// network->BSS
		case BSSG::BSPDUType::PTM_UNITDATA:	// not currently used
		// PDUs between GMM SAPs:
		case BSSG::BSPDUType::PAGING_PS:	// network->BSS request to page MS for packet connection.
		case BSSG::BSPDUType::PAGING_CS:	// network->BSS request to page MS for RR connection.
		case BSSG::BSPDUType::RA_CAPABILITY_UPDATE_ACK:	// network->BSS Radio Access Capability and IMSI.
		case BSSG::BSPDUType::FLOW_CONTROL_BVC_ACK: // network->BSS
		case BSSG::BSPDUType::FLOW_CONTROL_MS_ACK:	// network->BSS
		default:
			// See the list of unimplemented messages in BsRecvMsg()
			GPRSLOG(1) << "BSSG Downlink Message Ignored:"<<dmsg;
			//<<BSSG::BSPDUType::name(msgtype) << " size="<<dmsg->size();
			break;
		}
	}
}
#endif

static void writeQueue(MSInfo *ms, SGSN::GprsSgsnDownlinkPdu *dlpdu)
{
	ms->msDownlinkQueue.write(dlpdu);
	ms->msDownlinkQStat.addPoint(ms->msDownlinkQueue.totalSize());
	ms->msDownlinkQOldest = dlpdu->mDlTime;
}

// Send messages to the MSInfo where they belong.
static void processSgsnMessages()
{
	SGSN::GprsSgsnDownlinkPdu *dlpdu;
	while ((dlpdu = sgsnDownlinkQueue.readNoBlock())) {
    	MSInfo *ms = gL2MAC.macFindMSByTlli(dlpdu->mTlli, false);
		if (ms) {
			writeQueue(ms,dlpdu);
			ms->msAliasTlli(dlpdu->mAliasTlli);
		} else if (dlpdu->mAliasTlli && (ms = gL2MAC.macFindMSByTlli(dlpdu->mAliasTlli, false))) {
			// This is a TLLI change procedure but the new MS does not already exist.
			// This can happen in the case where the MS did not use the TLLI
			// in the AttachComplete procedure (an error on the part of the MS)
			// or if there was no AttachComplete, in case the SGSN sends an
			// AttachAccept without a PTMSI, so the MS does not answer.
			// In any case, we will be doing the change TLLI procedure when
			// this message is processed.
			writeQueue(ms,dlpdu);
			ms->msAliasTlli(dlpdu->mTlli);
		} else {
			// The downlink message from the SGSN should be for an MSInfo
			// that has only recently sent something to the SGSN so it should exist.
			// And we do not support paging.
			// We must have an MSInfo to know RSSI and TimingError,
			// otherwise we would have to page the MS.
			GLOG(ERR) << "Downlink message from SGSN for non-existent TLLI:"
				<<LOGHEX2("tlli",dlpdu->mTlli);
			delete dlpdu;	// Done with that.
		}
	}
}

// Compute a measure of GPRS channel utilization so we can decide when we need more bandwidth.
// For now we will compute only downlink bandwidth and ignore uplink.
// You cant just count how many RLC blocks are in TBFs, because the associated MS
// could be stalled and the TBF may not be using any bandwidth.
// The utilization is the approximate number of TBFs that are waiting to send blocks now,
// so 1.0 means the channel is pretty much full, and 2.0 means we could
// probably fully utilize two channels, at least at this instant.
// Return the utilization.
float L2MAC::macComputeUtilization()
{
	ScopedLock lock(macLock);	// This function called from CLI.
	int numReady = 0;
	float utilization = 0.0;	// Desired downlink utilization at this moment.
	TBF *tbf;
	RN_MAC_FOR_ALL_TBF(tbf) {
		switch (tbf->mtGetState()) {
			case TBFState::DataReadyToConnect:
			case TBFState::DataWaiting1:
			case TBFState::DataWaiting2:
				numReady++;		// These TBFs are waiting to send a message.
				continue;
			case TBFState::DataTransmit:
			case TBFState::DataReassign:
			//case TBFState::DataStalled:
				utilization += tbf->engineDesiredUtilization();
				continue;
			default: continue;
		}
	}
	utilization += numReady;

	// We want to average the utilization over some timespan.
	// This number should be picked to match the delay we use before closing
	// GPRS chanels, whatever that will be.
	const unsigned NumFramesToAverage = 48*5;	// roughly 5 seconds worth of RLC blocks.
	return macDownlinkUtilization =
		(utilization + macDownlinkUtilization * (NumFramesToAverage-1)) / NumFramesToAverage;
}

void L2MAC::macCheckChannels()
{
	int minChC0 = configGprsChannelsMinC0();
	int minChCn = configGprsChannelsMinCn();
	if (minChC0 < 0) { minChC0 = 0; }
	if (minChCn < 0) { minChCn = 0; }
	bool addedChannels = false;
	//GPRSLOG(2)<<"macCheckChannel"<<LOGVAR(minChC0)<<LOGVAR(minChCn)<<LOGVAR2("active",macActiveChannels());
	if (minChC0 + minChCn > (int)macActiveChannels()) {
		// Allocate from CN0.
		int active0 = macActiveChannelsC(0);
		{
			TCHFACCHLogicalChannel *lchan;
			for ( ; active0 < minChC0 && (lchan = gBTS.getTCH(true,true)); active0++) {
				//GPRSLOG(2)<<"macCheckChannel loop"<<LOGVAR(active0)<<LOGVAR(minChC0)<<LOGVAR2("lchan",lchan->TN());
				// We dont "open" the logical channel, which means we dont start
				// the various timers referred to in L1Decoder::recyclable().
				// It probably doesnt matter whether it is 'open' or not, because
				// we hook the bursts before they get to the GSM logical channel classes.
				macAddOneChannel(lchan);
				addedChannels = true;
			}
		}

		// Allocate from other ARFCNs
		// This should probably allow a specified number of channels
		// on each arfcn.
		{
			int activecn = (int)macActiveChannels() - active0;
			int nfound, need = minChCn - activecn;
			TCHFACCHLogicalChannel *results[8];
			// TODO: Prevent this from allocating from C0 if user misconfigures.
			for (; need > 0 && (nfound = gBTS.getTCHGroup(need,results)); need -= nfound) {
				for (int i = 0; i < nfound; i++) {
					macAddOneChannel(results[i]);
					addedChannels = true;
				}
			}
		}
	}
	if (addedChannels) {
		// We must keep the channel list sorted all the time because
		// PACCH selection and extended uplink TBF both eexpect it.
		gL2MAC.macPDCHs.sort(chCompareFunc);
	}
#if 0
	int minchans = configGprsChannelsMin();
	if (minchans > 0) {
		// We are doing startup.  Allocate the initial channels from CN0.
		int need = minchans - (int)macActiveChannels();
		TCHFACCHLogicalChannel *lchan;
		// Allocate from CN0 first.
		for ( ; need > 0 && (lchan = gBTS.getTCH(true,true)); need--) {
			macAddOneChannel(lchan);
		}
		// If we still need more, allocate them from the end of the list.
		int nfound;
		TCHFACCHLogicalChannel *results[8];
		for ( ; need > 0 && (nfound = gBTS.getTCHGroup(need,results)); need -= nfound) {
			for (int i = 0; i < nfound; i++) {
				macAddOneChannel(results[i]);
			}
		}
	}
#endif

	// TODO: Switch code to new channel allocator, but this code
	// will move to where the channels are allocated, not here.
	if (macTBFs.size()) {
		ChIdleCounter = 0;
		// If there are TBFs but no channels, try to allocate one.
		// TBFs get added not only from the MAC but also indirectly by the BSSG.
		// Note that we may not get the channel, in which case we will try each loop iteration.
		if (!macActiveChannels()) { macAddChannel(); }
	} else {
		// No TBFs exist.
		if (ChIdleCounter++ > macChIdleMax) {
			// Return a channel to GSM RR use.
			// We dont do this unless there is no activity at all,
			// which means that if there are multiple channels allocated we cant
			// downsize if the activity is merely low - it has to be stopped.
			macFreeChannel();
		}
	}

	// Maybe add another channel.
	// We average the congestion measurement by incrementing it or decrementing it once each loop.
	// todo: This test is too simple; needs to take into account how many channels allocated.
	/**** TODO: This will be replaced by dynamic allocation.
	if (macComputeUtilization() > macChCongestionThreshold) {
		if (ChCongestionCounter++ > macChCongestionMax) {
			macAddChannel();
		}
	} else {
		if (ChCongestionCounter > 0) { ChCongestionCounter--; }
	}
	****/
}


// Advance gBSNNext by the specified amount.
// The reason this is a function instead of just adding one to gBSNNext
// is to clean up old reservations behind us as we go.
static void advanceBSNNext(int amt)
{
	while (amt > 0) {
		amt--;
		gBSNPrev = gBSNNext;
		++gBSNNext;

		PDCHL1FEC *pdch;
		RN_MAC_FOR_ALL_PDCH(pdch) {	// for all channels assigned to GPRS.
			RLCBlockReservation *res;
			// For debug purposes, show unanswered reservations, and show them more nearly
			// when they are supposed to arrive than when we actually clear them out below:
			if (GPRSDebug) {
				RLCBSN_t rprevdeb(gBSNNext-(BSNLagTime+ExtraClockDelay+2));
				res = pdch->getReservation(rprevdeb);
				if (res) {
					GPRSLOG(1) << "Reservation unanswered "<<res->mrType<<LOGVAR2("ttype",res->mrSubType)<<" "<<res->mrTBF 
						<<" bsn=" <<rprevdeb <<" fn="<<rprevdeb.FN() << *res;
				}
			}

			// Clean up any old reservations.
			// The incoming decoded blocks seem to lag by several blocks, so wait
			// a few extra to make sure we dont remove a reservation before
			// all hope of receiving it has really passed.
			RLCBSN_t rprev(gBSNNext-(BSNLagTime+ExtraClockDelay+4));
			//if ((res = pdch->getReservation(rprev)) &&
				//res->mrType != RLCBlockReservation::ForRRBP) {
				//if (ExtraClockDelay < 4) {
				//	ExtraClockDelay++;
				//	GPRSLOG(1) << "*** Increasing Clock Delay to:"<<ExtraClockDelay;
				//}
			//}
			pdch->clearReservation(rprev,NULL);
		}
	}
}

// After a crash the current time is going backwards occassionally.
// which is screwing up this code.
// If you reboot, it is ok, but I am putting in some code to deal with it.
// Here is the /var/log/OpenBTS.log:
// My message:
//Nov 10 22:48:49 ToshibaLap openbts: DEBUG GPRS:now=777040 waiting for 777044
//Nov 10 22:48:49 ToshibaLap transceiver: ERR 3086998384 rnrad1Core.cpp:52:usbMsg: libusb_control_transfer failed: No such device (it may have been disconnected)
//Nov 10 22:48:49 ToshibaLap transceiver: ERR 3086998384 rnrad1Core.cpp:52:usbMsg: libusb_control_transfer failed: No such device (it may have been disconnected)
// My message:
//Nov 10 22:48:49 ToshibaLap openbts: DEBUG GPRS:unexpected gBSNNext delta: delta=(1306) gBSNNext=(179319) fnnext=(777049) fnnow=(775743)
//
// I am also getting this occassionally:
//Nov 10 22:48:45 ToshibaLap transceiver: ERR 3074030448 fusb.cpp:445:reload_read_buffer: No libusb events
//
// Work around it by checking if time has run backwards, and switching to
// calling sleep instead of catching up to the goofed up GSM clock.
static void serviceLoopSynchronize(bool firsttime)
{
	static double timeprev;
	Time tstart = gBTS.time();

	if (GPRSDebug) {
		// This is just a debug check that the GSM master clock is sane.
		// Apparently it is.
		static int fnprev;
		int fnnow = tstart.FN();
		if (!firsttime) {
			int fndelta = GSM::FNDelta(fnnow,fnprev);
			GPRSLOG(16) << "GSM FN delta="<<fndelta<<"\n";
			// 12-12-2011: The FN does run backwards ocassionally,
			// because the Clock mClock we use is only approximate,
			// and it is updated reguarly by the radio.
			// I saw these results one night:
			// INFO GPRS,2,35939:GSM FN ran backwards by -71
			// INFO GPRS,2,506726:GSM FN ran backwards by -93
			// INFO GPRS,2,134158:GSM FN ran backwards by -1851

			if (fndelta < 0) {
				//GPRSLOG(1) << "GSM FN ran backwards by " << fndelta << "\n";
				GLOG(ERR) << "*** GSM FN ran backwards by " << fndelta << "\n";
				//std::cout << "** ERROR: GSM FN ran backwards by "<< fndelta << "\n";
				//gFixSyncUseClock = true;	// Switch to alternate methodology
			}
			if (fndelta > 6) {
				GPRSLOG(4) << "GSM FN ran forwards by " << fndelta << "\n";
			}
		}
		fnprev = fnnow;
	}

	if (gFixSyncUseClock) {
		// Work around the bug where time goes backwards after a crash.
		// The whole BTS is probably non-operational, but at least I can keep debugging.
		// Try just waiting about a block time between each loop.
		sleepf(RLCBlockTime);
		//useconds_t usecs = (useconds_t) (1000000.0 / 48.0);
		//usleep(usecs);
	} else {
		// Wait for the prev frame to come around.
		Time tprev(gBSNPrev.FN());
		tprev = tprev - configGetNumQ("GPRS.ThreadAdvance",0);
		gBTS.clock().wait(tprev);

		// Check if the wait is screwing up: It is.
		// On the ITX board the FN moves backwards 2 units sporadically,
		// which (I hope) is because the gBTS clock was resynchronized with the radio
		// causing it to run backwards.
		Time tnow = gBTS.time();
		int deltaAfterWait = GSM::FNDelta(tnow.FN(),tprev.FN());
		// The deltaAfterWait is usually -1, so ignore that.
		if (deltaAfterWait > 3 || deltaAfterWait < -1) {
			GPRSLOG(2) << "gBTS.clock.wait unexpected wait time: "<<LOGVAR(deltaAfterWait);
		}

		// See if this thread was stalled in the last iteration.
		// If time has already advanced beyond the start of gBSNNext,
		// then advance gBSNNext until it is definitively ahead of the clock.
		// Code copied from L1Encoder::resync()
		// We started the previous iteration of the loop with time
		// set at the start of gBSNPrev
		// Has time already run past the beginning of gBSNNext?
		int delta = GSM::FNDelta(tnow.FN(),gBSNNext.FN());
		if (delta > 0 || delta < -(51*26)) {
			// Set bsnFixed to the beginning frame of the RLC block containing tnow.
			// | gBSNPrev | gBSNNext | ... | bsnFixed |
			RLCBSN_t bsnFixed = FrameNumber2BSN(tnow.FN());
			// Then advance gBSNNext to the next block time modulo ghyperframe.
			if (delta > 0) {
				advanceBSNNext(bsnFixed - gBSNNext);
			} else {
				// This is really a disaster.
				gBSNNext = bsnFixed + 1;
			}
			GPRSLOG(2) << "unexpected gBSNNext delta:" <<LOGVAR(delta)
					<<LOGVAR(tnow) <<LOGVAR(gBSNNext) <<LOGVAR(gBSNNext.FN()) <<LOGVAR(bsnFixed)
					<< " seconds=" << (timef() - timeprev)  << "\n";
		}

	}
	if (GPRSDebug) timeprev = timef();
}


// (pat) This is the main service loop for the entire GPRS subsystem.
// It processes in time increments of one radio block time unit (4 or 5 GSM frames), about 18 ms.
// We only use one thread, so no additional locks needed in the entire GPRS code
// beyond those in the i/o queues.
// It is attached to the L1 layer below via thread-safe queues in the L1Encoder/L2Decoder classes
// attached to each PCH (packet physical channel), and attached to the L3 layer above via
// thread-safe queues in the BSSGSP interface.
// 
// We have to keep the downlink PCH encoder classes fed with data on time.
// We would like to delay as long as possible before feeding them, so that we
// can assign uplink resources at the last minute to keep system throughput and
// response time as high as possible.
// However, it is ok to run this loop in advance of the actual needed data time as far
// as necessary to make it work.  If problem arise later on, instead of breaking
// this into multiple threads, I recommend simply increasing the advance.
// This is global instead of one per channel because some RadioBlocks may be multislot,
// and feed multiple channels simultaneously.
//
void L2MAC::macServiceLoop()
{
	double starttime = 0;	// useless initialization to shut up gcc.
	GPRSLOG(16) << "macServiceLoop:" << LOGVAR(gBSNNext);

	if (GPRSDebug) { starttime = timef(); }
	mac_debug();

	// Step: Each incoming RACH will need a single block assignment.
	// We do this first because if no channels are assigned to GPRS,
	// this will allocate the first GPRS channel as a side effect.
	macServiceRachQ();
	GPRSLOG(16) << "macServiceLoop: after serviceRachQ";

	// Step: Maybe add or free some radio channels.
	macCheckChannels();

	// Step:  Process uplink RadioBlocks from the last timeslot.
	// Do this first because it may change TBF states so that they have
	// downlink messages to send.
	PDCHL1FEC *pdch;
	RN_MAC_FOR_ALL_PDCH(pdch) {	// for all channels assigned to GPRS.
		pdch->debug_test();
		while (RLCRawBlock *src = pdch->uplink()->mchUplinkData.readNoBlock()) {
			processUplinkBlock(pdch,src);
			delete src;
		}
	}

	GPRSLOG(16) << "macServiceLoop: after uplink service";

	// Step:  Service unattached TBFs.  (An attached TBF has resources
	// assigned to its MS. Attached TBFs are handled by dlService().)
	// This could have been combined in servicing the MS,
	// but efficiency is not an issue and this seems more clear.
	// Be a little careful since we are deleting from the list we are iterating.
	{
		TBF *tbf;
		RN_MAC_FOR_ALL_TBF(tbf) {
			// This call may remove the tbf from the list being iterated:
			tbf->mtServiceUnattached();
		}
	}

	// Step: Service the MSs; they may want to start new TBFs.
	MSInfo *ms;
	RN_MAC_FOR_ALL_MS(ms) {
		ms->msService();
	}
	GPRSLOG(16) << "macServiceLoop: after ms service";

	// Step:  Feed each downlink PDCH with RadioBlocks.
	// As a side effect, this services all the [connected] TBFs in round robin order.
	extDyn.edReset();
	RN_MAC_FOR_ALL_PDCH(pdch) {	// for all channels assigned to GPRS.
		extDyn.edSetCn(pdch->CN());
		pdch->downlink()->dlService();
	}
	GPRSLOG(16) << "macServiceLoop: after downlink service";

	// LONG RANGE TODO: At the end of a TBF, if the downlink TBF queue is low,
	// we should send flow control to BSSG.  See engineRecvAckNack
	// But sadly, the SGSN does not implement flow control, so dont bother.

	// Step: Service the BSSG queue.
	// This mostly means moving any downlink PDUs into their MS.
	if (! GPRSConfig::sgsnIsInternal()) {
#if INTERNAL_SGSN==0
		processBSSGMessages();
#endif
		GPRSLOG(16) << "macServiceLoop: after bssg service";
	} else {
		processSgsnMessages();
	}

	// Step: gather statistics about this loop.
	if (GPRSDebug) {
		double elapsed = timef() - starttime;
		Stats.macServiceLoopTime.addPoint(elapsed);
	}
}

static void *macThreadFunc(void *arg)
{
	gL2MAC.macRunning = true;

	// Set the current RLC BSN.
	//Time tstart = gBTS.time().FN();
	Time tstart = gBTS.time();
	int fnstart = tstart.FN();
	gBSNNext = FrameNumber2BSN(fnstart);

	ChIdleCounter = ChCongestionCounter = 0;

	// Lets start synced up on a 52-multiframe boundary.
	if (0) { // why bother?
		int offset = fnstart % 52;
		if (offset) {
			fnstart += (52 - offset);
			gBTS.clock().wait(fnstart);
		}
	}

	GPRSLOG(1) << "macServiceLoop starting:" << LOGVAR(gBSNNext) <<LOGVAR(fnstart);

	// We need to run the service loop even if there are no channels allocated because
	// the BSSG may add downlink PDUs to an MS which will create a new TBF,
	// which will add a channel on demand.
	bool firsttime = true;		// First iteration.
	while (!gL2MAC.macStopFlag) {
		gL2MAC.macConfigInit();
		advanceBSNNext(1);
		serviceLoopSynchronize(firsttime);
		firsttime = false;

		{ 
			ScopedLock lock(gL2MAC.macLock);
			gL2MAC.macServiceLoop();
		}
	}

	GPRSLOG(1) << "macServiceLoop ending:" << LOGVAR(gBSNNext);

	gL2MAC.macRunning = false;
	return NULL;
}


// When the MS RACHes into the GSM RR/MM side for any reason, we must cancel ongoing TBFs.
// The danged MS will RACH into the GSM stack right in the middle of downlink TBFs, without warning,
// and subsequently considers the TBFs cancelled.  We want to cancel them on our side
// as soon as possible for two reasons: first to avoid data loss as much as possible, since otherwise
// the downlink TBF will just continue until stalled, and second, because if the MS comes back
// before the downlink TBF timesout, it considers it a new TBF, which completely botches up
// the acknack state, and the TBF gets stuck, which at best causes about a 10 second loss of service,
// and at worst I saw the Blackberry GPRS detach and reattach after this confusion.
// The spec says not a peep about this; I think they expect us to use NMO 1 if we want this to work properly.
// Unfortunately, the GSM stack knows IMSIs and TMSIs, which means a normal BTS would have to track
// this mapping itself, but since we use an internal SGSN which knows the IMSI mapping,
// we can just forward the request there.  If the IMSI is registered, the SGSN will send us a cancel message.
// WARNING: This routine runs in a different thread.
void GPRSNotifyGsmActivity(const char *imsi)
{
	SGSN::Sgsn::notifyGsmActivity(imsi);
}


}; // namespace




// These routines are the interface between the SGSN and GPRS:
namespace SGSN {
    MSUEAdapter *SgsnAdapter::findMs(uint32_t handle) {
		GPRS::MSInfo *ms = GPRS::gL2MAC.macFindMSByTlli(handle, false);
		return ms;
    }

    bool SgsnAdapter::isUmts() {
        return false;
    }

	// The SGSN writes pdus here instead of directly to an MS because
	// the SGSN may be running in a different thread driven from the miniggsn.
	void SgsnAdapter::saWriteHighSide(GprsSgsnDownlinkPdu *dlpdu) {
		GPRS::sgsnDownlinkQueue.write(dlpdu);
	}

    // This allocates the RB and sends the message to the UE then returns.
    // We wont know if the UE gets the message until it replies.
    //RabStatus SgsnAdapter::allocateRabForPdp(uint32_t msHandle,int rbid, ByteVector &qos)
	//{
	//	// Always successful in GPRS.
	//	RabStatus result(RabStatus::RabAllocated,(SmCauseType)0);
	//	return result;
    //}

	// This is a complete no-op in gprs.
	//void SgsnAdapter::startIntegrityProtection(uint32_t urnti, std::string Kcs) {}
};	// namespace SGSN
