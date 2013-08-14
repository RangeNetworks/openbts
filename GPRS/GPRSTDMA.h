/**@file GPRS TDMA parameters. */
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


#ifndef GPRSTDMA_H
#define GPRSTDMA_H


#include "GSMCommon.h"
#include "GSMTDMA.h"

namespace GPRS {
// (pat) We wont use this to program the radio right now, and probably never.
// However, it is needed to init a LogicalChannel.  The info needs to duplicate
// that for an RR TCH, which is what we are really using.
// Currently we will hook to the existing RR logical channels to get RLC blocks.
// In the future, we probably would not use this either, we would probably modify
// TRXManager to send the entire channel stream directly to us, and then direct
// the packets internally; not use the ARFCNManager::mDemuxTable,
// which is what this TDMA_MAPPING is primarily for.

/** A macro to save some typing when we set up TDMA maps. */
// This is copied from ../GSM/GSMTDMA.cpp
#define MAKE_TDMA_MAPPING(NAME,TYPEANDOFFSET,DOWNLINK,UPLINK,ALLOWEDSLOTS,C0ONLY,REPEAT) \
 	const GSM::TDMAMapping g##NAME##Mapping(TYPEANDOFFSET,DOWNLINK,UPLINK,ALLOWEDSLOTS,C0ONLY, \
 		REPEAT,sizeof(NAME##Frames)/sizeof(unsigned),NAME##Frames)


/** PDCH TDMA from GSM 03.64 6.1.2, GSM 05.02 Clause 7 Table 6 of 9. */
// (pat) This was the orignal; does not look correct to me:
// const unsigned PDCHFrames[] = {0,1,2,3, 4,5,6,7, 8,9,10,11, 13,14,15,16, 16,17,18,19,
//	20,21,22,23, 25,26,27,28, 29,30,31,32, 33,34,35,36, 38,39,40,41, 42,43,44,45, 46,47,48,49};

// (pat) This is first line (PDTCH/F PACCH/F) of GSM05.02 clause 7 table  6 of 9
// Note that we skip over frames 12, 25, 38 and 51, which are used for other purposes.
// TODO: I dont know if we are going to handle the frame mapping this way,
// or let the GPRS code handle all the frames, including the PTCCH (timing advance) slots.
const unsigned PDTCHFFrames[] = {0,1,2,3, 4,5,6,7, 8,9,10,11, 13,14,15,16, 17,18,19,20,
	21,22,23,24, 26,27,28,29, 30,31,32,33, 34,35,36,37, 39,40,41,42, 43,44,45,46, 47,48,49,50 };
const unsigned PTCCHFrames[] = { 12, 38 };
const unsigned PDIdleFrames[] = { 25, 51 };

// PDCH is the name of the packet data channel, comprised of PDTCH, PTCCH, and 2 idle frames.
MAKE_TDMA_MAPPING(PDTCHF,GSM::TDMA_PDTCHF,true,true,0xff,false,52); // Makes gPDTCHFMapping
MAKE_TDMA_MAPPING(PTCCH,GSM::TDMA_PTCCH,true,false,0xff,false,52);
MAKE_TDMA_MAPPING(PDIdle,GSM::TDMA_PDIDLE,true,false,0xff,false,52);

const GSM::MappingPair gPDTCHPair(gPDTCHFMapping,gPDTCHFMapping);
const GSM::MappingPair gPTCCHPair(gPTCCHMapping);


}; // namespace GPRS

#endif
