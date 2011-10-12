/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


#include "GSMTDMA.h"


using namespace GSM;




TDMAMapping::TDMAMapping(TypeAndOffset
		wTypeAndOffset, bool wDownlink, bool wUplink, char wAllowedSlots, bool wC0Only,
		unsigned wRepeatLength, unsigned wNumFrames, const unsigned *wFrameMapping)
	:mTypeAndOffset(wTypeAndOffset),
	mDownlink(wDownlink),mUplink(wUplink),mAllowedSlots(wAllowedSlots),mC0Only(wC0Only),
	mRepeatLength(wRepeatLength),mNumFrames(wNumFrames),mFrameMapping(wFrameMapping)
{
	// Sanity check.
	assert(mRepeatLength<=mMaxRepeatLength);

	// Default, -1, means a non-occupied position.
	for (unsigned i=0; i<mMaxRepeatLength; i++) mReverseMapping[i]=-1;

	// Fill in the reverse map, precomputed for speed.
	for (unsigned i=0; i<mNumFrames; i++) {
		unsigned mapping = mFrameMapping[i];
		assert(mapping<mRepeatLength);
		mReverseMapping[mapping] = i;
	}
}





/** A macro to save some typing when we set up TDMA maps. */
#define MAKE_TDMA_MAPPING(NAME,TYPEANDOFFSET,DOWNLINK,UPLINK,ALLOWEDSLOTS,C0ONLY,REPEAT) \
	const TDMAMapping GSM::g##NAME##Mapping(TYPEANDOFFSET,DOWNLINK,UPLINK,ALLOWEDSLOTS,C0ONLY, \
		REPEAT,sizeof(NAME##Frames)/sizeof(unsigned),NAME##Frames)

const unsigned LoopbackTestFullFrames[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47};
MAKE_TDMA_MAPPING(LoopbackTestFull,TDMA_MISC,true,true,0xff,false,51);

const unsigned FCCHFrames[] = {0,10,20,30,40};
MAKE_TDMA_MAPPING(FCCH,TDMA_BEACON,true,false,0x01,true,51);

const unsigned SCHFrames[] = {1,11,21,31,41};
MAKE_TDMA_MAPPING(SCH,TDMA_BEACON,true,false,0x01,true,51);

const unsigned BCCHFrames[] = {2,3,4,5};
MAKE_TDMA_MAPPING(BCCH,TDMA_BEACON_BCCH,true,false,0x55,true,51);

// Note that we removed frames for the SDCCH components of the Combination-V C0T0.
const unsigned RACHC5Frames[] = {4,5,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,45,46};
MAKE_TDMA_MAPPING(RACHC5,TDMA_BEACON,false,true,0x55,true,51);

// CCCH 0-2 are used in C-IV and C-V.  The others are used in C-IV only.

const unsigned CCCH_0Frames[] = {6,7,8,9};
MAKE_TDMA_MAPPING(CCCH_0,TDMA_BEACON_CCCH,true,false,0x55,true,51);

const unsigned CCCH_1Frames[] = {12,13,14,15};
MAKE_TDMA_MAPPING(CCCH_1,TDMA_BEACON_CCCH,true,false,0x55,true,51);

const unsigned CCCH_2Frames[] = {16,17,18,19};
MAKE_TDMA_MAPPING(CCCH_2,TDMA_BEACON_CCCH,true,false,0x55,true,51);

const unsigned CCCH_3Frames[] = {22,23,24,25};
MAKE_TDMA_MAPPING(CCCH_3,TDMA_BEACON_CCCH,true,false,0x55,true,51);

// TODO -- Other CCCH subchannels 4-8 for support of C-IV.

const unsigned SDCCH_4_0DFrames[] = {22,23,24,25};
MAKE_TDMA_MAPPING(SDCCH_4_0D,SDCCH_4_0,true,false,0x01,true,51);

const unsigned SDCCH_4_0UFrames[] = {37,38,39,40};
MAKE_TDMA_MAPPING(SDCCH_4_0U,SDCCH_4_0,false,true,0x01,true,51);

const unsigned SDCCH_4_1DFrames[] = {26,27,28,29};
MAKE_TDMA_MAPPING(SDCCH_4_1D,SDCCH_4_1,true,false,0x01,true,51);

const unsigned SDCCH_4_1UFrames[] = {41,42,43,44};
MAKE_TDMA_MAPPING(SDCCH_4_1U,SDCCH_4_1,false,true,0x01,true,51);

const unsigned SDCCH_4_2DFrames[] = {32,33,34,35};
MAKE_TDMA_MAPPING(SDCCH_4_2D,SDCCH_4_2,true,false,0x01,true,51);

const unsigned SDCCH_4_2UFrames[] = {47,48,49,50};
MAKE_TDMA_MAPPING(SDCCH_4_2U,SDCCH_4_2,false,true,0x01,true,51);

const unsigned SDCCH_4_3DFrames[] = {36,37,38,39};
MAKE_TDMA_MAPPING(SDCCH_4_3D,SDCCH_4_3,true,false,0x01,true,51);

const unsigned SDCCH_4_3UFrames[] = {0,1,2,3};
MAKE_TDMA_MAPPING(SDCCH_4_3U,SDCCH_4_3,false,true,0x01,true,51);


const unsigned SACCH_C4_0DFrames[] = {42,43,44,45};
MAKE_TDMA_MAPPING(SACCH_C4_0D,SDCCH_4_0,true,false,0x01,true,102);

const unsigned SACCH_C4_0UFrames[] = {57,58,59,60};
MAKE_TDMA_MAPPING(SACCH_C4_0U,SDCCH_4_0,false,true,0x01,true,102);

const unsigned SACCH_C4_1DFrames[] = {46,47,48,49};
MAKE_TDMA_MAPPING(SACCH_C4_1D,SDCCH_4_1,true,false,0x01,true,102);

const unsigned SACCH_C4_1UFrames[] = {61,62,63,64};
MAKE_TDMA_MAPPING(SACCH_C4_1U,SDCCH_4_1,false,true,0x01,true,102);

const unsigned SACCH_C4_2DFrames[] = {93,94,95,96};
MAKE_TDMA_MAPPING(SACCH_C4_2D,SDCCH_4_2,true,false,0x01,true,102);

const unsigned SACCH_C4_2UFrames[] = {6,7,8,9};
MAKE_TDMA_MAPPING(SACCH_C4_2U,SDCCH_4_2,false,true,0x01,true,102);

const unsigned SACCH_C4_3DFrames[] = {97,98,99,100};
MAKE_TDMA_MAPPING(SACCH_C4_3D,SDCCH_4_3,true,false,0x01,true,102);

const unsigned SACCH_C4_3UFrames[] = {10,11,12,13};
MAKE_TDMA_MAPPING(SACCH_C4_3U,SDCCH_4_3,false,true,0x01,true,102);


const unsigned SDCCH_8_0DFrames[] = {0,1,2,3};
MAKE_TDMA_MAPPING(SDCCH_8_0D,SDCCH_8_0,true,false,0xFF,true,51);

const unsigned SDCCH_8_0UFrames[] = {15,16,17,18};
MAKE_TDMA_MAPPING(SDCCH_8_0U,SDCCH_8_0,false,true,0xFF,true,51);

const unsigned SDCCH_8_1DFrames[] = {4,5,6,7};
MAKE_TDMA_MAPPING(SDCCH_8_1D,SDCCH_8_1,true,false,0xFF,true,51);

const unsigned SDCCH_8_1UFrames[] = {19,20,21,22};
MAKE_TDMA_MAPPING(SDCCH_8_1U,SDCCH_8_1,false,true,0xFF,true,51);

const unsigned SDCCH_8_2DFrames[] = {8,9,10,11};
MAKE_TDMA_MAPPING(SDCCH_8_2D,SDCCH_8_2,true,false,0xFF,true,51);

const unsigned SDCCH_8_2UFrames[] = {23,24,25,26};
MAKE_TDMA_MAPPING(SDCCH_8_2U,SDCCH_8_2,false,true,0xFF,true,51);

const unsigned SDCCH_8_3DFrames[] = {12,13,14,15};
MAKE_TDMA_MAPPING(SDCCH_8_3D,SDCCH_8_3,true,false,0xFF,true,51);

const unsigned SDCCH_8_3UFrames[] = {27,28,29,30};
MAKE_TDMA_MAPPING(SDCCH_8_3U,SDCCH_8_3,false,true,0xFF,true,51);

const unsigned SDCCH_8_4DFrames[] = {16,17,18,19};
MAKE_TDMA_MAPPING(SDCCH_8_4D,SDCCH_8_4,true,false,0xFF,true,51);

const unsigned SDCCH_8_4UFrames[] = {31,32,33,34};
MAKE_TDMA_MAPPING(SDCCH_8_4U,SDCCH_8_4,false,true,0xFF,true,51);

const unsigned SDCCH_8_5DFrames[] = {20,21,22,23};
MAKE_TDMA_MAPPING(SDCCH_8_5D,SDCCH_8_5,true,false,0xFF,true,51);

const unsigned SDCCH_8_5UFrames[] = {35,36,37,38};
MAKE_TDMA_MAPPING(SDCCH_8_5U,SDCCH_8_5,false,true,0xFF,true,51);

const unsigned SDCCH_8_6DFrames[] = {24,25,26,27};
MAKE_TDMA_MAPPING(SDCCH_8_6D,SDCCH_8_6,true,false,0xFF,true,51);

const unsigned SDCCH_8_6UFrames[] = {39,40,41,42};
MAKE_TDMA_MAPPING(SDCCH_8_6U,SDCCH_8_6,false,true,0xFF,true,51);

const unsigned SDCCH_8_7DFrames[] = {28,29,30,31};
MAKE_TDMA_MAPPING(SDCCH_8_7D,SDCCH_8_7,true,false,0xFF,true,51);

const unsigned SDCCH_8_7UFrames[] = {43,44,45,46};
MAKE_TDMA_MAPPING(SDCCH_8_7U,SDCCH_8_7,false,true,0xFF,true,51);


const unsigned SACCH_C8_0DFrames[] = {32,33,34,35};
MAKE_TDMA_MAPPING(SACCH_C8_0D,SDCCH_8_0,true,false,0xFF,true,102);

const unsigned SACCH_C8_0UFrames[] = {47,48,49,50};
MAKE_TDMA_MAPPING(SACCH_C8_0U,SDCCH_8_0,false,true,0xFF,true,102);

const unsigned SACCH_C8_1DFrames[] = {36,37,38,39};
MAKE_TDMA_MAPPING(SACCH_C8_1D,SDCCH_8_1,true,false,0xFF,true,102);

const unsigned SACCH_C8_1UFrames[] = {51,52,53,54};
MAKE_TDMA_MAPPING(SACCH_C8_1U,SDCCH_8_1,false,true,0xFF,true,102);

const unsigned SACCH_C8_2DFrames[] = {40,41,42,43};
MAKE_TDMA_MAPPING(SACCH_C8_2D,SDCCH_8_2,true,false,0xFF,true,102);

const unsigned SACCH_C8_2UFrames[] = {55,56,57,58};
MAKE_TDMA_MAPPING(SACCH_C8_2U,SDCCH_8_2,false,true,0xFF,true,102);

const unsigned SACCH_C8_3DFrames[] = {44,45,46,47};
MAKE_TDMA_MAPPING(SACCH_C8_3D,SDCCH_8_3,true,false,0xFF,true,102);

const unsigned SACCH_C8_3UFrames[] = {59,60,61,62};
MAKE_TDMA_MAPPING(SACCH_C8_3U,SDCCH_8_3,false,true,0xFF,true,102);

const unsigned SACCH_C8_4DFrames[] = {83,84,85,86};
MAKE_TDMA_MAPPING(SACCH_C8_4D,SDCCH_8_4,true,false,0xFF,true,102);

const unsigned SACCH_C8_4UFrames[] = {98,99,100,101};
MAKE_TDMA_MAPPING(SACCH_C8_4U,SDCCH_8_4,false,true,0xFF,true,102);

const unsigned SACCH_C8_5DFrames[] = {87,88,89,90};
MAKE_TDMA_MAPPING(SACCH_C8_5D,SDCCH_8_5,true,false,0xFF,true,102);

const unsigned SACCH_C8_5UFrames[] = {0,1,2,3};
MAKE_TDMA_MAPPING(SACCH_C8_5U,SDCCH_8_5,false,true,0xFF,true,102);

const unsigned SACCH_C8_6DFrames[] = {91,92,93,94};
MAKE_TDMA_MAPPING(SACCH_C8_6D,SDCCH_8_6,true,false,0xFF,true,102);

const unsigned SACCH_C8_6UFrames[] = {4,5,6,7};
MAKE_TDMA_MAPPING(SACCH_C8_6U,SDCCH_8_6,false,true,0xFF,true,102);

const unsigned SACCH_C8_7DFrames[] = {95,96,97,98};
MAKE_TDMA_MAPPING(SACCH_C8_7D,SDCCH_8_7,true,false,0xFF,true,102);

const unsigned SACCH_C8_7UFrames[] = {8,9,10,11};
MAKE_TDMA_MAPPING(SACCH_C8_7U,SDCCH_8_7,false,true,0xFF,true,102);



const unsigned SACCH_TF_T0Frames[] = {12,38,64,90};
MAKE_TDMA_MAPPING(SACCH_TF_T0,TCHF_0,true,true,0x01,true,104);

const unsigned SACCH_TF_T1Frames[] = {25,51,77,103};
MAKE_TDMA_MAPPING(SACCH_TF_T1,TCHF_0,true,true,0x02,true,104);

const unsigned SACCH_TF_T2Frames[] = {38,64,90,12};
MAKE_TDMA_MAPPING(SACCH_TF_T2,TCHF_0,true,true,0x04,true,104);

const unsigned SACCH_TF_T3Frames[] = {51,77,103,25};
MAKE_TDMA_MAPPING(SACCH_TF_T3,TCHF_0,true,true,0x08,true,104);

const unsigned SACCH_TF_T4Frames[] = {64,90,12,38};
MAKE_TDMA_MAPPING(SACCH_TF_T4,TCHF_0,true,true,0x10,true,104);

const unsigned SACCH_TF_T5Frames[] = {77,103,25,51};
MAKE_TDMA_MAPPING(SACCH_TF_T5,TCHF_0,true,true,0x20,true,104);

const unsigned SACCH_TF_T6Frames[] = {90,12,38,64};
MAKE_TDMA_MAPPING(SACCH_TF_T6,TCHF_0,true,true,0x40,true,104);

const unsigned SACCH_TF_T7Frames[] = {103,25,51,77};
MAKE_TDMA_MAPPING(SACCH_TF_T7,TCHF_0,true,true,0x80,true,104);

const unsigned FACCH_TCHFFrames[] = {0,1,2,3,4,5,6,7,8,9,10,11,13,14,15,16,17,18,19,20,21,22,23,24};
MAKE_TDMA_MAPPING(FACCH_TCHF,TCHF_0,true,true,0xff,true,26);







const MappingPair GSM::gSDCCH_4_0Pair(gSDCCH_4_0DMapping,gSDCCH_4_0UMapping);
const MappingPair GSM::gSDCCH_4_1Pair(gSDCCH_4_1DMapping,gSDCCH_4_1UMapping);
const MappingPair GSM::gSDCCH_4_2Pair(gSDCCH_4_2DMapping,gSDCCH_4_2UMapping);
const MappingPair GSM::gSDCCH_4_3Pair(gSDCCH_4_3DMapping,gSDCCH_4_3UMapping);
const MappingPair GSM::gSDCCH_8_0Pair(gSDCCH_8_0DMapping,gSDCCH_8_0UMapping);
const MappingPair GSM::gSDCCH_8_1Pair(gSDCCH_8_1DMapping,gSDCCH_8_1UMapping);
const MappingPair GSM::gSDCCH_8_2Pair(gSDCCH_8_2DMapping,gSDCCH_8_2UMapping);
const MappingPair GSM::gSDCCH_8_3Pair(gSDCCH_8_3DMapping,gSDCCH_8_3UMapping);
const MappingPair GSM::gSDCCH_8_4Pair(gSDCCH_8_4DMapping,gSDCCH_8_4UMapping);
const MappingPair GSM::gSDCCH_8_5Pair(gSDCCH_8_5DMapping,gSDCCH_8_5UMapping);
const MappingPair GSM::gSDCCH_8_6Pair(gSDCCH_8_6DMapping,gSDCCH_8_6UMapping);
const MappingPair GSM::gSDCCH_8_7Pair(gSDCCH_8_7DMapping,gSDCCH_8_7UMapping);

const MappingPair GSM::gSACCH_C4_0Pair(gSACCH_C4_0DMapping,gSACCH_C4_0UMapping);
const MappingPair GSM::gSACCH_C4_1Pair(gSACCH_C4_1DMapping,gSACCH_C4_1UMapping);
const MappingPair GSM::gSACCH_C4_2Pair(gSACCH_C4_2DMapping,gSACCH_C4_2UMapping);
const MappingPair GSM::gSACCH_C4_3Pair(gSACCH_C4_3DMapping,gSACCH_C4_3UMapping);
const MappingPair GSM::gSACCH_C8_0Pair(gSACCH_C8_0DMapping,gSACCH_C8_0UMapping);
const MappingPair GSM::gSACCH_C8_1Pair(gSACCH_C8_1DMapping,gSACCH_C8_1UMapping);
const MappingPair GSM::gSACCH_C8_2Pair(gSACCH_C8_2DMapping,gSACCH_C8_2UMapping);
const MappingPair GSM::gSACCH_C8_3Pair(gSACCH_C8_3DMapping,gSACCH_C8_3UMapping);
const MappingPair GSM::gSACCH_C8_4Pair(gSACCH_C8_4DMapping,gSACCH_C8_4UMapping);
const MappingPair GSM::gSACCH_C8_5Pair(gSACCH_C8_5DMapping,gSACCH_C8_5UMapping);
const MappingPair GSM::gSACCH_C8_6Pair(gSACCH_C8_6DMapping,gSACCH_C8_6UMapping);
const MappingPair GSM::gSACCH_C8_7Pair(gSACCH_C8_7DMapping,gSACCH_C8_7UMapping);

const MappingPair GSM::gFACCH_TCHFPair(gFACCH_TCHFMapping,gFACCH_TCHFMapping);

const MappingPair GSM::gSACCH_FT_T0Pair(gSACCH_TF_T0Mapping, gSACCH_TF_T0Mapping);
const MappingPair GSM::gSACCH_FT_T1Pair(gSACCH_TF_T1Mapping, gSACCH_TF_T1Mapping);
const MappingPair GSM::gSACCH_FT_T2Pair(gSACCH_TF_T2Mapping, gSACCH_TF_T2Mapping);
const MappingPair GSM::gSACCH_FT_T3Pair(gSACCH_TF_T3Mapping, gSACCH_TF_T3Mapping);
const MappingPair GSM::gSACCH_FT_T4Pair(gSACCH_TF_T4Mapping, gSACCH_TF_T4Mapping);
const MappingPair GSM::gSACCH_FT_T5Pair(gSACCH_TF_T5Mapping, gSACCH_TF_T5Mapping);
const MappingPair GSM::gSACCH_FT_T6Pair(gSACCH_TF_T6Mapping, gSACCH_TF_T6Mapping);
const MappingPair GSM::gSACCH_FT_T7Pair(gSACCH_TF_T7Mapping, gSACCH_TF_T7Mapping);



const CompleteMapping GSM::gSDCCH_4_0(gSDCCH_4_0Pair,gSACCH_C4_0Pair);
const CompleteMapping GSM::gSDCCH_4_1(gSDCCH_4_1Pair,gSACCH_C4_1Pair);
const CompleteMapping GSM::gSDCCH_4_2(gSDCCH_4_2Pair,gSACCH_C4_2Pair);
const CompleteMapping GSM::gSDCCH_4_3(gSDCCH_4_3Pair,gSACCH_C4_3Pair);
const CompleteMapping GSM::gSDCCH4[4] = {
	GSM::gSDCCH_4_0, GSM::gSDCCH_4_1, GSM::gSDCCH_4_2, GSM::gSDCCH_4_3,
};

const CompleteMapping GSM::gSDCCH_8_0(gSDCCH_8_0Pair,gSACCH_C8_0Pair);
const CompleteMapping GSM::gSDCCH_8_1(gSDCCH_8_1Pair,gSACCH_C8_1Pair);
const CompleteMapping GSM::gSDCCH_8_2(gSDCCH_8_2Pair,gSACCH_C8_2Pair);
const CompleteMapping GSM::gSDCCH_8_3(gSDCCH_8_3Pair,gSACCH_C8_3Pair);
const CompleteMapping GSM::gSDCCH_8_4(gSDCCH_8_4Pair,gSACCH_C8_4Pair);
const CompleteMapping GSM::gSDCCH_8_5(gSDCCH_8_5Pair,gSACCH_C8_5Pair);
const CompleteMapping GSM::gSDCCH_8_6(gSDCCH_8_6Pair,gSACCH_C8_6Pair);
const CompleteMapping GSM::gSDCCH_8_7(gSDCCH_8_7Pair,gSACCH_C8_7Pair);
const CompleteMapping GSM::gSDCCH8[8] = {
	GSM::gSDCCH_8_0, GSM::gSDCCH_8_1, GSM::gSDCCH_8_2, GSM::gSDCCH_8_3,
	GSM::gSDCCH_8_4, GSM::gSDCCH_8_5, GSM::gSDCCH_8_6, GSM::gSDCCH_8_7,
};

const CompleteMapping GSM::gTCHF_T0(gFACCH_TCHFPair,gSACCH_FT_T0Pair);
const CompleteMapping GSM::gTCHF_T1(gFACCH_TCHFPair,gSACCH_FT_T1Pair);
const CompleteMapping GSM::gTCHF_T2(gFACCH_TCHFPair,gSACCH_FT_T2Pair);
const CompleteMapping GSM::gTCHF_T3(gFACCH_TCHFPair,gSACCH_FT_T3Pair);
const CompleteMapping GSM::gTCHF_T4(gFACCH_TCHFPair,gSACCH_FT_T4Pair);
const CompleteMapping GSM::gTCHF_T5(gFACCH_TCHFPair,gSACCH_FT_T5Pair);
const CompleteMapping GSM::gTCHF_T6(gFACCH_TCHFPair,gSACCH_FT_T6Pair);
const CompleteMapping GSM::gTCHF_T7(gFACCH_TCHFPair,gSACCH_FT_T7Pair);
const CompleteMapping GSM::gTCHF_T[8] = {
	GSM::gTCHF_T0, GSM::gTCHF_T1, GSM::gTCHF_T2, GSM::gTCHF_T3,
	GSM::gTCHF_T4, GSM::gTCHF_T5, GSM::gTCHF_T6, GSM::gTCHF_T7,
};



