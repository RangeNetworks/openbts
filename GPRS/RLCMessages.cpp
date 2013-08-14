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

/**@file GPRS L2 RLC Messages, from GSM 04.60 Section 11 */

//#include <iostream>
#include "Defines.h"
//#include "GSMCommon.h"
#include <BitVector.h>
#include <Logger.h>
#define RLCHDR_IMPLEMENTATION 1
#include "RLCHdr.h"
#include "TBF.h"
#define RLCMESSAGES_IMPLEMENTATION 1
#include "RLCMessages.h"
#include "MAC.h"
#include "FEC.h"


namespace GPRS {

const char *RLCUplinkMessage::name(MessageType type)
{
	static char buf[50];
	switch (type) {
		CASENAME(PacketCellChangeFailure)
		CASENAME(PacketControlAcknowledgement)
		CASENAME(PacketDownlinkAckNack)
		CASENAME(PacketUplinkDummyControlBlock)
		CASENAME(PacketMeasurementReport)
		CASENAME(PacketEnhancedMeasurementReport)
		CASENAME(PacketResourceRequest)
		CASENAME(PacketMobileTBFStatus)
		CASENAME(PacketPSIStatus)
		CASENAME(EGPRSPacketDownlinkAckNack)
		CASENAME(PacketPause)
		CASENAME(AdditionalMSRadioAccessCapabilities)
		default:
			sprintf(buf,"RLCUplinkMessageType %d (unknown)",(int)type);
			return buf;
	}
}

const char *RLCDownlinkMessage::name(MessageType type)
{
	static char buf[50];
	switch (type) {
		CASENAME(PacketAccessReject )
		CASENAME(PacketCellChangeOrder)
		CASENAME(PacketDownlinkAssignment)
		CASENAME(PacketMeasurementOrder )
		CASENAME(PacketPagingRequest )
		CASENAME(PacketPDCHRelease)
		CASENAME(PacketPollingRequest)
		CASENAME(PacketPowerControlTimingAdvance)
		CASENAME(PacketPRACHParameters)
		CASENAME(PacketQueueingNotification)
		CASENAME(PacketTimeslotReconfigure)
		CASENAME(PacketTBFRelease)
		CASENAME(PacketUplinkAckNack)
		CASENAME(PacketUplinkAssignment)
		CASENAME(PacketDownlinkDummyControlBlock)
		CASENAME(PSI1)
		CASENAME(PSI2)
		CASENAME(PSI3)
		CASENAME(PSI3bis)
		CASENAME(PSI4)
		CASENAME(PSI5)
		CASENAME(PSI6)
		CASENAME(PSI7)
		CASENAME(PSI8)
		CASENAME(PSI13)
		CASENAME(PSI14)
		CASENAME(PSI3ter)
		CASENAME(PSI3quater)
		CASENAME(PSI15)
		default:
			sprintf(buf,"RLCDownlinkMessageType %d (unknown)",(int)type);
			return buf;
	}
}

MSInfo *RLCMsgPacketResourceRequest::getMS(PDCHL1FEC *chan, bool create)
{
	// If MS is identified by a TLLI, the msg is not specifically associated with a TBF.
	if (mTLLIPresent) {
		MSInfo *ms = gL2MAC.macFindMSByTlli(mTLLI, create);
		return ms;
	} else {
		RLCDirType dir = mGTFI.mIsDownlinkTFI ? RLCDir::Down : RLCDir::Up;
		TBF *tbf = chan->getTFITBF(mGTFI.mGTFI,dir);
		if (tbf) return tbf->mtMS;
	}
	return NULL;
}

void RLCMsgPacketUplinkAssignmentDynamicAllocationElt::setFrom(TBF *tbf,MultislotSymmetry sym)
{
	MSInfo *ms = tbf->mtMS;
	setAlpha(ms->msGetAlpha());
	PDCHL1Uplink *up;
	RN_FOR_ALL(PDCHL1UplinkList_t,ms->msPCHUps,up) {
		int tn = up->TN();
		setUSF(tn,ms->msUSFs[tn]);
		setGamma(tn,ms->msGetGamma());
	}
	if (ms->isExtendedDynamic()) {
		mExtendedDynamicAllocation = true;
	}

	//if (sym == MultislotSymmetric && isExtendedDynamic()) {
	//	// This sounds odd, but we need to use the downlink timeslots to program
	//	// the uplink timeslots so that they will be symmetric.
	//	// If they are assymetric, the smaller array is always valid in both directions.
	//	PDCHL1Downlink *down;
	//	RN_FOR_ALL(PDCHL1DownlinkList_t,ms->msPCHDowns,down) {
	//		int tn = down->TN();
	//		setUSF(tn,ms->msUSFs[tn]);
	//		setGamma(tn,ms->msGetGamma());
	//	}
	//} else {
	//	PDCHL1Uplink *up;
	//	RN_FOR_ALL(PDCHL1UplinkList_t,ms->msPCHUps,up) {
	//		int tn = up->TN();
	//		setUSF(tn,ms->msUSFs[tn]);
	//		setGamma(tn,ms->msGetGamma());
	//	}
	//}
}

void RLCMsgPowerControlParametersIE::setFrom(TBF *tbf)
{
	MSInfo *ms = tbf->mtMS;
	setAlpha(ms->msGetAlpha());
	PDCHL1Downlink *down;
	RN_FOR_ALL(PDCHL1DownlinkList_t,ms->msPCHDowns,down) {
		int tn = down->TN();
		setGamma(tn,ms->msGetGamma());
	}
}


/** GSM 04.60 11.2 */
RLCUplinkMessage* RLCUplinkMessageParse(RLCRawBlock *src)
{
	RLCUplinkMessage *result = NULL;

	unsigned mMessageType = src->mData.peekField(8,6);

	// Kyle reported that OpenBTS crashes parsing the RLCMsgMSRACapabilityValuePartIE.
	// If you look at the message that is in, if the RACap is trashed, the message is unusable.
	// So lets catch errors way up at this level, and ignore them on error.
	// In case of error, we are probably permanently losing the memory associated with the messsage, oh well.
	try {
		switch (mMessageType) {
			case RLCUplinkMessage::PacketControlAcknowledgement:
				result = new RLCMsgPacketControlAcknowledgement(src);
				break;
			case RLCUplinkMessage::PacketDownlinkAckNack:
				// Thats right: DownlinkAckNack is an uplink message.
				result = new RLCMsgPacketDownlinkAckNack(src);
				break;
			case RLCUplinkMessage::PacketUplinkDummyControlBlock:
				result = new RLCMsgPacketUplinkDummyControlBlock(src);
				break;
			case RLCUplinkMessage::PacketResourceRequest:
				result = new RLCMsgPacketResourceRequest(src);
				break;
			// case PacketMobileTBFStatus:
			// case AdditionalMSRadioAccessCapabilities:
			default:
				GLOG(INFO) << "unsupported RLC uplink message, type=" << mMessageType;
				//result = new RLCMsgPacketUplinkDummyControlBlock(src);
				return NULL;
		}
		return result;
	} catch (ByteVectorError) {
		GLOG(ERR) << "Parse Error: Premature end of message, type="
			<<mMessageType<<"="<<RLCUplinkMessage::name((RLCUplinkMessage::MessageType)mMessageType);
	}
	return NULL;
};

};	// namespace GPRS
