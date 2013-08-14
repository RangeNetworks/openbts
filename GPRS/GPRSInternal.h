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

#ifndef GPRSINTERNAL_H
#define GPRSINTERNAL_H
#include <stdint.h>
#include "GPRSRLC.h"


namespace GPRS {
	// FEC.h:
	class PDCHL1FEC;
	//class PTCCHL1Uplink;
	//class PDIdleL1Uplink;
	class PDCHL1Uplink;
	class PDCHL1Downlink;

	// MAC.h:
	class RLCBSN_t;
	class MSInfo;
	class L2MAC;
	class L1UplinkReservation;
	class L1USFTable;

	// GPRSL2RLCEngine.h:
	class RLCEngine;

	//TBF.h:
	class TBF;


	// RLCEngine.h:
	class RLCUpEngine;
	class RLCDownEngine;

	// RLCHdr.h:
	class RLCDownlinkBlock;
	class RLCUplinkDataBlock;
	class RLCDownlinkDataBlock;
	struct MACDownlinkHeader;
	struct MACUplinkHeader;
	struct RLCDownlinkControlBlockHeader;
	struct RLCUplinkControlBlockHeader;
	struct RLCSubBlockHeader;
	struct RLCSubBlockTLLI;
	struct RLCDownlinkDataBlockHeader;
	struct RLCUplinkDataBlockHeader;

	// RLCMessages.h:
	class RLCMessage;
	class RLCDownlinkMessage;
	class RLCUplinkMessage;
	struct RLCMsgPacketControlAcknowledgement;
	struct RLCMsgElementPacketAckNackDescription;
	struct RLCMsgElementChannelRequestDescription;
	struct RLCMsgElementRACapabilityValuePart;
	struct RLCMsgPacketDownlinkAckNack;
	struct RLCMsgPacketResourceRequest;
	struct RLCMsgPacketUplinkDummyControlBlock;
	struct RLCMsgPacketResourceRequest;
	//struct RLCMsgPacketMobileTBFStatus;
	class RLCMsgPacketUplinkAssignment;
	class RLCMsgPacketDownlinkAssignment;
	class RLCMsgPacketAccessReject;
	class RLCMsgPacketTBFRelease;
	class RLCMsgPacketUplinkAckNack;

	// LLC.h:
	class LLCFrame;

	//GPRSL2RLCElements.h:class RLCElement
	//GPRSL2RLCElements.h:class RLCAckNackDescription : public RLCElement
	//GPRSL2RLCElements.h:class RLCChannelQualityReport : public RLCElement
	//GPRSL2RLCElements.h:class RLCPacketTimingAdvance : public RLCElement
	//GPRSL2RLCElements.h:class RLCPacketPowerControlParameters : public RLCElement
	//GPRSL2RLCElements.h:class RLCChannelRequestDescription : public RLCElement

	// GPRSL2RLCMessages.h:
	//class RLCDownlinkMessage;
	//class RLCPacketDownlinkAckNack;
	//class RLCPacketUplinkAckNack;
	//class RLCPacketDownlinkControlBlock;
	//class RLCPacketUplinkControlBlock;

	int GetTimingAdvance(float timingError);
	int GetPowerAlpha();
	int GetPowerGamma();
	extern unsigned GPRSDebug;
	extern unsigned gGprsWatch;
	extern std::string fmtfloat2(float num);
};

#include "Defines.h"
#include "GSMConfig.h"		// For Time
#include "GSMCommon.h"		// For ChannelType
#include "GPRSExport.h"
#include "Utils.h"

#include "Logger.h"
// Redefine GPRSLOG to include the current RLC BSN when called in this directory.
#ifdef GPRSLOG
#undef GPRSLOG
#endif
// 6-18-2012: If someone sets Log.Level to DEBUG, show everything.
#define GPRSLOG(level) if (GPRS::GPRSDebug & (level) || IS_LOG_LEVEL(DEBUG)) \
	_LOG(DEBUG) <<"GPRS"<<(level)<<","<<GPRS::gBSNNext<<":"
#define LOGWATCHF(...) if (GPRS::gGprsWatch&1) printf(__VA_ARGS__); GPRSLOG(1)<<"watch:"<<format(__VA_ARGS__);

// If gprs debugging is on, print these messages regardless of Log.Level.
#define GLOG(wLevel) if (GPRSDebug || IS_LOG_LEVEL(wLevel)) _LOG(wLevel) << " "<<timestr()<<","<<GPRS::gBSNNext<<":"

// Like assert() but dont core dump unless we are testing.
#define devassert(code) {if (GPRS::GPRSDebug||IS_LOG_LEVEL(DEBUG)) {assert(code);} else if (!(code)) {LOG(ERR)<<"assertion failed:"<< #code;}}


#endif
