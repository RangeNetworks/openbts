/**@file @brief L3 Radio Resource messages related to GPRS */
/*
* Copyright 2008, 2010 Free Software Foundation, Inc.
* Copyright 2011 Kestrel Signal Processing, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#include <typeinfo>
#include <iostream>

#include "GSML3RRMessages.h"
#include "../GPRS/GPRSExport.h"
#include <Logger.h>


namespace GSM {


// GSM 04.60 sec 12.24
void L3GPRSCellOptions::writeBits(L3Frame& dest, size_t &wp) const
{
	GPRS::GPRSCellOptions_t& gco = GPRS::GPRSGetCellOptions();
	dest.writeField(wp,gco.mNMO,2);
	dest.writeField(wp,gco.mT3168Code,3);
	dest.writeField(wp,gco.mT3192Code,3);
	dest.writeField(wp,gco.mDRX_TIMER_MAX,3);
	dest.writeField(wp,gco.mACCESS_BURST_TYPE,1);
	dest.writeField(wp,gco.mCONTROL_ACK_TYPE,1);
	dest.writeField(wp,gco.mBS_CV_MAX,4);
	dest.writeField(wp,0,1);	// optional PAN_ fields omitted.
	LOG(INFO)<< "beacon"<<LOGVAR2("NW_EXT_UTBF",gco.mNW_EXT_UTBF);
	if (!gco.mNW_EXT_UTBF) {
		dest.writeField(wp,0,1);	// optional extension information omitted
	} else {
		dest.writeField(wp,1,1);	// extension information included.
		unsigned extlen = 6;		// 6 bits of extension information.
		dest.writeField(wp,extlen,6);	// length of extension.
		// R99 extensions:
		dest.writeField(wp,0,1);	// No EGPRS.
		dest.writeField(wp,0,1);	// No PFC_FEATURE_MODE
		dest.writeField(wp,0,1);	// No DTM_SUPPORT
		dest.writeField(wp,0,1);	// No BSS_PAGING_COORDINATION
		// Rel-4 extensions:
		// I tried setting CCN to 1 to get the MS to indicate GERAN feature pack I support.
		// CCN is network assisted cell change and is also part of GERAN feature pack I.
		dest.writeField(wp,0,1);	// CCN_ACTIVE. CCN described 44.060 5.5.1.1a
		dest.writeField(wp,gco.mNW_EXT_UTBF,1);	// Finally.
		// Rel-6 extensions:
		// We dont want any of these, but here they are as documentation.
		//dest.writeField(wp,0,1);	// No MULTIPLE_TBF_CAPABILITY
		//dest.writeField(wp,0,1);	// No EXT_UTBF_NODATA
		//dest.writeField(wp,0,1);	// No DTM_ENHANCEMENTS_CAPABILITY
		//dest.writeField(wp,0,1);	// No MBMS procedures
		// End of Rel extensions, we are allowed to truncate here.
		dest.writeField(wp,0,1);	// Required spare bit - this is very confusing in the spec.
	}
}

size_t L3GPRSCellOptions::lengthBits() const
{
	GPRS::GPRSCellOptions_t& gco = GPRS::GPRSGetCellOptions();
	size_t result = 2+3+3+3+1+1+4+1+1;
	if (gco.mNW_EXT_UTBF) {
		result += 6 + 6 + 1;	// 6 bit len + 6 bits of extension + 1 spare bit.
	}
	return result;
}

void L3GPRSCellOptions::text(ostream& os) const
{
	GPRS::GPRSCellOptions_t& gco = GPRS::GPRSGetCellOptions();
	os << "NMO=" << gco.mNMO;
	os << " T3168Code=" << gco.mT3168Code;
	os << " T3192Code=" << gco.mT3192Code;
	os << " DRX_TIMER_MAX=" << gco.mDRX_TIMER_MAX;
	os << " ACCESS_BURST_TYPE=" << gco.mACCESS_BURST_TYPE;
	os << " CONTROL_ACK_TYPE=" << gco.mCONTROL_ACK_TYPE;
	os << " BS_CV_MAX=" << gco.mBS_CV_MAX;
	os << LOGVAR2("NW_EXT_UTBF",gco.mNW_EXT_UTBF);
}



const char *L3IAPacketAssignment::IAPacketAssignmentTypeText(enum IAPacketAssignmentType type) const
{
	switch (type) {
	case PacketUplinkAssignUninitialized: return "";	// No rest octets neeeded for this.
	case PacketUplinkAssignFixed: return "Fixed Packet Uplink Assignment";
	case PacketUplinkAssignDynamic: return "Dynamic Packet Uplink Assignment";
	case PacketUplinkAssignSingleBlock: return "Single Block Packet Uplink Assignment";
	case PacketDownlinkAssign: return "Packet Downlink Assignment";
	default:
		LOG(ERR) << "unrecognized packet assignment type code " << (int)type;
		return "??Unknown Assignment Type??";
	}
}

void L3IAPacketAssignment::setPacketUplinkAssignSingleBlock(unsigned TBFStartingTime)
{
	mPacketAssignmentType = PacketUplinkAssignSingleBlock;
	mTBFStartingTimePresent = true;
	mTBFStartingTime = TBFStartingTime;
	mChannelCodingCommand = 0;	// use CS-1; redundant
}

void L3IAPacketAssignment::setPacketUplinkAssignDynamic(unsigned TFI, unsigned CSNum, unsigned USF)
{
	mPacketAssignmentType = PacketUplinkAssignDynamic;
	mTFIPresent = true;
	mTFIAssignment = TFI;
	mChannelCodingCommand = CSNum;	// CS-1, etc.
	mUSFGranularity = 0;		// redundant.
	mUSF = USF;
}

void L3IAPacketAssignment::setPacketDownlinkAssign(
	unsigned wTLLI, unsigned wTFI,unsigned wCSNum,
	unsigned wRLCMode,unsigned wTAValid)
{
	mPacketAssignmentType = PacketDownlinkAssign;
	mTLLI = wTLLI;
	mTFIPresent = true;
	mTFIAssignment = wTFI;
	mChannelCodingCommand = wCSNum;	// CS-1, etc.
	mRLCMode = wRLCMode;
	mTAValid = wTAValid; // We provided a valid TimingAdvance.
}

void L3IAPacketAssignment::setPacketPollTime(unsigned wTBFStartingTime)
{
	mPolling = true;
	mTBFStartingTimePresent = true;
	mTBFStartingTime = wTBFStartingTime;
}

void L3IAPacketAssignment::setPacketUplinkAssignFixed()
{
	mPacketAssignmentType = PacketUplinkAssignFixed;
	// unimplemented
	assert(0);
}

// We broadcast alpha in the SI13 message, but not gamma.
// This gives us a chance to over-ride the alpha,gamma for an individual MS,
// or based on RSSI from the RACH.
// Dont know if we will use this, but here it is anyway.
// If this is not called, the default value of gamma == 0 means MS broadcasts at full power,
// moderated only by the alpha broadcast on SI13.
void L3IAPacketAssignment::setPacketPowerOptions(unsigned wAlpha, unsigned wGamma)
{
	mAlpha = wAlpha; mGamma = wGamma; mAlphaPresent = true;
}

// TBF Starting Time GSM 04.08 10.5.2.38
static void writeStartTime(MsgCommon &dest, unsigned startframe)
{
	std::ostream*os = dest.getStream();	// non-NULL for text() function.

	// The names T1, T2, T3 are defined in table 10.5.79
	unsigned T1 = (startframe/1326)%32;
	unsigned T3 = startframe%51;
	unsigned T2 = startframe%26;

	// Recompute original startframe:
	// Note that T3-T2 may be negative:
	int recomputed = 51 * (((int)T3-(int)T2) % 26) + T3 + 51 * 26 * T1;
	unsigned startframemod = (startframe % 42432);

	// If we are writing text(), output both the original startframe and 
	// the computed T1,T2,T3.
	if (os) {
		// The recomputed time may be a much smaller number than startframe.
		*os << " TBFStartFrame=" <<startframe << "=(" << "T=" <<recomputed;
	}

	// Note: The fields are written here Most-Significant-Bit first in each byte,
	// then the bytes are reversed in the encoder before being sent to the radio.
	// 		 	7      6      5      4      3     2      1      0
	//	  [			T1[4:0]                 ][   T3[5:3]        ]  Octet 1
	//	  [       T3[2:0]     ][            T2[4:0]             ]  Octet 2
	dest.writeField(T1,5,"T1p");
	dest.writeField(T3,6,"T3");	// Yes T3 comes before T2.
	dest.writeField(T2,5,"T2");

	// This just doesnt work, despite the documentation.
	if (os && recomputed != (int)startframemod) {
		*os << " TBF Start Time miscalculation: "
			<<LOGVAR(startframemod) <<"!=" <<LOGVAR(recomputed);
	}

	if (os) { *os << ")"; }
}


// (pat) The uplink assignment is always initiated by the MS using a RACH,
// so the MS is identified by the request reference, and this message
// does not contain a TLLI.
void L3IAPacketAssignment::writePacketUplinkAssignment(MsgCommon &dest) const
{
	// The IA Rest Octets start with some bits to indicate a Packet Uplink Assignment:
	// GSM04.08 sec 10.5.2.16
	dest.writeH();
	dest.writeH();
	dest.write0();
	dest.write0();
	if (mPacketAssignmentType == PacketUplinkAssignFixed ||
		mPacketAssignmentType == PacketUplinkAssignDynamic) {
		dest.write1();
		dest.WRITE_FIELD(mTFIAssignment, 5);
		dest.WRITE_FIELD(mPolling, 1);
		switch (mPacketAssignmentType) {
		case PacketUplinkAssignDynamic:
			dest.write0();
			dest.WRITE_FIELD(mUSF, 3);
			dest.WRITE_FIELD(mUSFGranularity, 1);
			dest.write0(); 	// No downlink power parameters present.
			// mPowerOption.writePower(dest,0);
			break;
		case PacketUplinkAssignFixed:
			dest.write1();
			dest.WRITE_FIELD(mAllocationBitmapLength, 5);
			if (mAllocationBitmapLength) {
				dest.WRITE_FIELD(mAllocationBitmap, mAllocationBitmapLength);
			}
			dest.write0();	// No downlink power parameters present.
			// mPowerOption.writePower(dest,0);
			break;
		default: assert(0);
		}
		dest.WRITE_FIELD(mChannelCodingCommand, 2);
		dest.WRITE_FIELD(mTLLIBlockChannelCoding, 1);
		if (dest.write01(mAlphaPresent)) { dest.WRITE_FIELD(mAlpha,4); }
		dest.WRITE_FIELD(mGamma,5);
		if (dest.write01(mTimingAdvanceIndexPresent)) {
			dest.WRITE_FIELD(mTimingAdvanceIndex,4);
		}
		if (dest.write01(mTBFStartingTimePresent)) {
			writeStartTime(dest,mTBFStartingTime);
		}
	} else {	// single block assignment.
		dest.write0();	// uplink assignment type designator
		if (dest.write01(mAlphaPresent)) { dest.WRITE_FIELD(mAlpha,4); }
		dest.WRITE_FIELD(mGamma,5);
		dest.write0(); dest.write1();	// As per 10.5.2.16 Note 1.
		assert(mTBFStartingTimePresent);	// required for single block uplink assignment.
		writeStartTime(dest,mTBFStartingTime);
		dest.writeL();	// No downlink power parameters present.
		//mPowerOption.writePower(dest,1);
	}
}

// (pat) The downlink assignment may be (normally is) initiated by the network,
// in which case the "request reference" in the Immediate Assignment message
// is set to an impossible value, and the MS is identified by the TLLI.
void L3IAPacketAssignment::writePacketDownlinkAssignment(MsgCommon &dest) const
{
	// The IA Rest Octets start with some bits to indicate a Packet Downlink Assignment:
	// GSM04.08 sec 10.5.2.16
	dest.writeH();
	dest.writeH();
	dest.write0();
	dest.write1();
	dest.writeField(mTLLI,32,"TLLI",tohex);
	if (dest.write01(mTFIPresent)) {
		dest.WRITE_FIELD(mTFIAssignment,5);
		dest.WRITE_FIELD(mRLCMode,1);
		dest.WRITE_OPT_FIELD01(mAlpha,4,mAlphaPresent);
		dest.WRITE_FIELD(mGamma,5);
		dest.WRITE_FIELD(mPolling, 1);
		dest.WRITE_FIELD(mTAValid, 1);
	}
	dest.WRITE_OPT_FIELD01(mTimingAdvanceIndex,4,mTimingAdvanceIndexPresent);
	if (dest.write01(mTBFStartingTimePresent)) {
		writeStartTime(dest,mTBFStartingTime);
	}
	dest.write0();	// No downlink power parameters present.
	dest.writeL();	// No Egprs.
}

void L3IAPacketAssignment::writeIAPacketAssignment(MsgCommon &dest) const
{
	// (pat) These messages are the rest octets in an Immediate Assignment Message.
	switch (mPacketAssignmentType) {
	case PacketUplinkAssignUninitialized:
		return;	// No rest octets neeeded for this.
	case PacketUplinkAssignFixed:
	case PacketUplinkAssignDynamic:
	case PacketUplinkAssignSingleBlock:
		return writePacketUplinkAssignment(dest);
	case PacketDownlinkAssign:
		return writePacketDownlinkAssignment(dest);
	}
}

void L3IAPacketAssignment::writeBits(L3Frame &frame, size_t &wp) const
{
	MsgCommonWrite tmp(frame,wp);
	writeIAPacketAssignment(tmp);
	wp = tmp.wp;
}

// Return the length of this puppy.
size_t L3IAPacketAssignment::lengthBits() const
{
	MsgCommonLength dest;
	writeIAPacketAssignment(dest);
	return dest.wp;
}

// Print a human readable version of this puppy.
void L3IAPacketAssignment::text(std::ostream& os) const
{
	if (mPacketAssignmentType != PacketUplinkAssignUninitialized) {
		os << " " << IAPacketAssignmentTypeText(mPacketAssignmentType);
		MsgCommonText dest(os);
		writeIAPacketAssignment(dest);
	}
}


#if 0	// Currently unused
void L3GPRSPowerControlParameters::writeBits(L3Frame& dest, size_t &wp) const
{
	dest.writeField(wp,mAlpha,4);
	// We dont use any of these gamma fields at the moment:
	dest.writeField(wp,0,1);	// GAMMA_TM0
	dest.writeField(wp,0,1);	// GAMMA_TM1
	dest.writeField(wp,0,1);	// GAMMA_TM2
	dest.writeField(wp,0,1);	// GAMMA_TM3
	dest.writeField(wp,0,1);	// GAMMA_TM4
	dest.writeField(wp,0,1);	// GAMMA_TM5
	dest.writeField(wp,0,1);	// GAMMA_TM6
	dest.writeField(wp,0,1);	// GAMMA_TM7
}


void L3GPRSPowerControlParameters::text(ostream& os) const
{
	os << "Alpha=" << mAlpha;
}
#endif

L3GPRSSI13PowerControlParameters::L3GPRSSI13PowerControlParameters()
	: mAlpha(GPRS::GetPowerAlpha()),
	mTAvgW(gConfig.getNum("GPRS.MS.Power.T_AVG_W")),
	mTAvgT(gConfig.getNum("GPRS.MS.Power.T_AVG_T")),
	mPCMeasChan(0),
	mNAvgI(15)	// We dont use this so dont bother putting in sql.
{}

void L3GPRSSI13PowerControlParameters::writeBits(L3Frame& dest, size_t &wp) const
{
	// TODO: use WRITE_ITEM from MsgBase.h 
	dest.writeField(wp,mAlpha,4);
	dest.writeField(wp,mTAvgW,5);
	dest.writeField(wp,mTAvgT,5);
	dest.writeField(wp,mPCMeasChan,1);
	dest.writeField(wp,mNAvgI,4);
}


void L3GPRSSI13PowerControlParameters::text(ostream& os) const
{
	os << "(" <<LOGVAR(mAlpha) <<LOGVAR(mTAvgW) <<LOGVAR(mTAvgT)
		<<LOGVAR(mPCMeasChan) <<LOGVAR(mNAvgI) << ")";
}
};
