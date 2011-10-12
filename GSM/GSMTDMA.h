/**@file Common-use GSM declarations, most from the GSM 04.xx and 05.xx series. */
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



#ifndef GSMTDMA_H
#define GSMTDMA_H


#include "GSMCommon.h"


namespace GSM {


/**
	A description of a channel's multiplexing pattern.
	From GSM 05.02 Clause 7.
	This object encodes a line from tables 1-4 in the spec.
	The columns of interest in this encoding are:
		- 1, Channel Designation
		- 2, Subchannel
		- 3, Direction
		- 4, Allowable Time Slot Assignments
		- 5, Allowable RF Channel Assignments
		- 7, Repeat Length in TDMA Frames
		- 8, Interleaved Block TDMA Frame Mapping

	Col 6, Burst Type, is implied by 1 & 2 and encoded into the transcevier source code.
*/
class TDMAMapping {

	public:

	/// The longest "repeat length" of any channel we support is 104 for the SACCH/TF.
	static const unsigned mMaxRepeatLength = 104;

	private:

	TypeAndOffset mTypeAndOffset;				///< col 1, 2, encoded as per GSM 04.08 10.5.2.5
	bool mDownlink;								///< col 3, true for downlink channels
	bool mUplink;								///< col 3, true for uplink channels
	char mAllowedSlots;							///< col 4, an 8-bit mask
	bool mC0Only;								///< col 5, true if channel is limited to C0
	unsigned mRepeatLength;						///< col 7
	unsigned mNumFrames;						///< number of occupied frames in col 8
	const unsigned *mFrameMapping;				///< col 8
	unsigned mReverseMapping[mMaxRepeatLength];	///< index reversal of mapping, -1 means unused


	public:


	/**
		Construct a TDMAMapping, encoding one line of GSM 05.02 Clause 7 Tables 1-4.
		@param wTypeAndOffset Encoding of "Channel designnation".  See GSM 04.08 10.5.2.5.
		@param wDownlink True for downlink and bidirectional hannels
		@param wUplink True for uplink and bidirectional channels
		@param wRepeatLength "Repeat Length in TDMA Frames"
		@param wNumFrames Number of occupied TDMA frames in frame mapping.
		@param wFrameMapping "Interleaved Block TDMA Frame Mapping" -- MUST PERSIST!!
	*/
	TDMAMapping(TypeAndOffset wTypeAndOffset,
		bool wDownlink, bool wUplink, char wAllowedSlots, bool wC0Only,
		unsigned wRepeatLength, unsigned wNumFrames, const unsigned *wFrameMapping);

	/** Given a count of frames sent, return the corresponding frame number. */
	unsigned frameMapping(unsigned count) const
		{ return mFrameMapping[count % mNumFrames]; }

	/** Given a frame number, return the corresponding count, modulo patten length. */
	int reverseMapping(unsigned FN) const
		{ return mReverseMapping[FN % mRepeatLength]; }

	/**@name Simple accessors. */
	//@{
	unsigned numFrames() const { return mNumFrames; }

	unsigned repeatLength() const { return mRepeatLength; }

	TypeAndOffset typeAndOffset() const { return mTypeAndOffset; }

	bool uplink() const { return mUplink; }

	bool downlink() const { return mDownlink; }

	bool C0Only() const { return mC0Only; }
	//@}

	///< Return true if this channel is allowed on this slot.
	bool allowedSlot(unsigned slot) const
		{ return mAllowedSlots & (1<<slot); }
};



/**@name Mux parameters for standard channels, from GSM 05.03 Clause 7 Tables 1-4. */
//@{
/**@name Beacon channels */
//@{
extern const TDMAMapping gFCCHMapping;		///< GSM 05.02 Clause 7 Table 3 Line 1 B0-B4
extern const TDMAMapping gSCHMapping;		///< GSM 05.02 Clause 7 Table 3 Line 2 B0-B4
extern const TDMAMapping gBCCHMapping;		///< GSM 05.02 Clause 7 Table 3 Line 3
/// GSM 05.02 Clause 7 Table 3 Line 7 B0-B50, excluding C-V SDCCH parts (SDCCH/4 and SCCH/C4)
extern const TDMAMapping gRACHC5Mapping;
extern const TDMAMapping gCCCH_0Mapping;	///< GSM 05.02 Clause 7 Table 3 Line 5 B0
extern const TDMAMapping gCCCH_1Mapping;	///< GSM 05.02 Clause 7 Table 3 Line 5 B1
extern const TDMAMapping gCCCH_2Mapping;	///< GSM 05.02 Clause 7 Table 3 Line 5 B2
extern const TDMAMapping gCCCH_3Mapping;	///< GSM 05.02 Clause 7 Table 3 Line 5 B3
extern const TDMAMapping gCCCH_4Mapping;	///< GSM 05.02 Clause 7 Table 3 Line 5 B4
extern const TDMAMapping gCCCH_5Mapping;	///< GSM 05.02 Clause 7 Table 3 Line 5 B5
extern const TDMAMapping gCCCH_6Mapping;	///< GSM 05.02 Clause 7 Table 3 Line 5 B6
extern const TDMAMapping gCCCH_7Mapping;	///< GSM 05.02 Clause 7 Table 3 Line 5 B7
extern const TDMAMapping gCCCH_8Mapping;	///< GSM 05.02 Clause 7 Table 3 Line 5 B8
//@}
/**@name SDCCH */
//@{
///@name For Combination V
//@{
extern const TDMAMapping gSDCCH_4_0DMapping;	///< GSM 05.02 Clause 7 Table 3 Line 10/0D
extern const TDMAMapping gSDCCH_4_0UMapping;	///< GSM 05.02 Clause 7 Table 3 Line 10/0U
extern const TDMAMapping gSDCCH_4_1DMapping;
extern const TDMAMapping gSDCCH_4_1UMapping;
extern const TDMAMapping gSDCCH_4_2DMapping;
extern const TDMAMapping gSDCCH_4_2UMapping;
extern const TDMAMapping gSDCCH_4_3DMapping;
extern const TDMAMapping gSDCCH_4_3UMapping;
//@}
///@name For Combination VII
//@{
extern const TDMAMapping gSDCCH_8_0DMapping;
extern const TDMAMapping gSDCCH_8_0UMapping;
extern const TDMAMapping gSDCCH_8_1DMapping;
extern const TDMAMapping gSDCCH_8_1UMapping;
extern const TDMAMapping gSDCCH_8_2DMapping;
extern const TDMAMapping gSDCCH_8_2UMapping;
extern const TDMAMapping gSDCCH_8_3DMapping;
extern const TDMAMapping gSDCCH_8_3UMapping;
extern const TDMAMapping gSDCCH_8_4DMapping;
extern const TDMAMapping gSDCCH_8_4UMapping;
extern const TDMAMapping gSDCCH_8_5DMapping;
extern const TDMAMapping gSDCCH_8_5UMapping;
extern const TDMAMapping gSDCCH_8_6DMapping;
extern const TDMAMapping gSDCCH_8_6UMapping;
extern const TDMAMapping gSDCCH_8_7DMapping;
extern const TDMAMapping gSDCCH_8_7UMapping;
//@}
//@}
/**@name SACCH */
//@{
/**name SACCH for SDCCH */
//@{
///@name For Combination V
//@{
extern const TDMAMapping gSACCH_C4_0DMapping;
extern const TDMAMapping gSACCH_C4_0UMapping;
extern const TDMAMapping gSACCH_C4_1DMapping;
extern const TDMAMapping gSACCH_C4_1UMapping;
extern const TDMAMapping gSACCH_C4_2DMapping;
extern const TDMAMapping gSACCH_C4_2UMapping;
extern const TDMAMapping gSACCH_C4_3DMapping;
extern const TDMAMapping gSACCH_C4_3UMapping;
//@}
///@name For Combination VII
//@{
extern const TDMAMapping gSACCH_C8_0DMapping;
extern const TDMAMapping gSACCH_C8_0UMapping;
extern const TDMAMapping gSACCH_C8_1DMapping;
extern const TDMAMapping gSACCH_C8_1UMapping;
extern const TDMAMapping gSACCH_C8_2DMapping;
extern const TDMAMapping gSACCH_C8_2UMapping;
extern const TDMAMapping gSACCH_C8_3DMapping;
extern const TDMAMapping gSACCH_C8_3UMapping;
extern const TDMAMapping gSACCH_C8_4DMapping;
extern const TDMAMapping gSACCH_C8_4UMapping;
extern const TDMAMapping gSACCH_C8_5DMapping;
extern const TDMAMapping gSACCH_C8_5UMapping;
extern const TDMAMapping gSACCH_C8_6DMapping;
extern const TDMAMapping gSACCH_C8_6UMapping;
extern const TDMAMapping gSACCH_C8_7DMapping;
extern const TDMAMapping gSACCH_C8_7UMapping;
//@}
//@}
/**@name SACCH for TCH/F on different timeslots. */
//@{
extern const TDMAMapping gSACCH_TF_T0Mapping;
extern const TDMAMapping gSACCH_TF_T1Mapping;
extern const TDMAMapping gSACCH_TF_T2Mapping;
extern const TDMAMapping gSACCH_TF_T3Mapping;
extern const TDMAMapping gSACCH_TF_T4Mapping;
extern const TDMAMapping gSACCH_TF_T5Mapping;
extern const TDMAMapping gSACCH_TF_T6Mapping;
extern const TDMAMapping gSACCH_TF_T7Mapping;
//@}
//@}
/**name FACCH+TCH/F placement */
//@{
extern const TDMAMapping gFACCH_TCHFMapping;
//@}
/**@name Test fixtures. */
extern const TDMAMapping gLoopbackTestFullMapping;
extern const TDMAMapping gLoopbackTestHalfUMapping;
extern const TDMAMapping gLoopbackTestHalfDMapping;
//@}


/** Combined uplink/downlink information. */
class MappingPair {

	private:

	const TDMAMapping& mDownlink;
	const TDMAMapping& mUplink;

	public:

	MappingPair(const TDMAMapping& wDownlink, const TDMAMapping& wUplink)
		:mDownlink(wDownlink), mUplink(wUplink)
	{}

	MappingPair(const TDMAMapping& wMapping)
		:mDownlink(wMapping), mUplink(wMapping)
	{}

	const TDMAMapping& downlink() const { return mDownlink; }
	const TDMAMapping& uplink() const { return mUplink; }

};


/**@name Common placement pairs. */
//@{
/**@ SDCCH placement pairs. */
//@{
extern const MappingPair gSDCCH_4_0Pair;
extern const MappingPair gSDCCH_4_1Pair;
extern const MappingPair gSDCCH_4_2Pair;
extern const MappingPair gSDCCH_4_3Pair;
extern const MappingPair gSDCCH_8_0Pair;
extern const MappingPair gSDCCH_8_1Pair;
extern const MappingPair gSDCCH_8_2Pair;
extern const MappingPair gSDCCH_8_3Pair;
extern const MappingPair gSDCCH_8_4Pair;
extern const MappingPair gSDCCH_8_5Pair;
extern const MappingPair gSDCCH_8_6Pair;
extern const MappingPair gSDCCH_8_7Pair;
//@}
/**@ SACCH-for-SDCCH placement pairs. */
//@{
extern const MappingPair gSACCH_C4_0Pair;
extern const MappingPair gSACCH_C4_1Pair;
extern const MappingPair gSACCH_C4_2Pair;
extern const MappingPair gSACCH_C4_3Pair;
extern const MappingPair gSACCH_C8_0Pair;
extern const MappingPair gSACCH_C8_1Pair;
extern const MappingPair gSACCH_C8_2Pair;
extern const MappingPair gSACCH_C8_3Pair;
extern const MappingPair gSACCH_C8_4Pair;
extern const MappingPair gSACCH_C8_5Pair;
extern const MappingPair gSACCH_C8_6Pair;
extern const MappingPair gSACCH_C8_7Pair;
//@}
/**@name Traffic channels. */
//@{
extern const MappingPair gFACCH_TCHFPair;
extern const MappingPair gSACCH_FT_T0Pair;
extern const MappingPair gSACCH_FT_T1Pair;
extern const MappingPair gSACCH_FT_T2Pair;
extern const MappingPair gSACCH_FT_T3Pair;
extern const MappingPair gSACCH_FT_T4Pair;
extern const MappingPair gSACCH_FT_T5Pair;
extern const MappingPair gSACCH_FT_T6Pair;
extern const MappingPair gSACCH_FT_T7Pair;
//@}
//@}



/** A CompleteMapping includes uplink, downlink and the SACCH. */
class CompleteMapping {

	private:

	const MappingPair& mLCH;
	const MappingPair& mSACCH;

	public:

	CompleteMapping(const MappingPair& wLCH, const MappingPair& wSACCH)
		:mLCH(wLCH), mSACCH(wSACCH)
	{}

	const MappingPair& LCH() const { return mLCH; }
	const MappingPair& SACCH() const { return mSACCH; }

};



/**@name Complete placements for common channel types. */
//@{
/**@name SDCCH/4 */
//@{
extern const CompleteMapping gSDCCH_4_0;
extern const CompleteMapping gSDCCH_4_1;
extern const CompleteMapping gSDCCH_4_2;
extern const CompleteMapping gSDCCH_4_3;
extern const CompleteMapping gSDCCH4[4];
//@}
/**@name SDCCH/8 */
//@{
extern const CompleteMapping gSDCCH_8_0;
extern const CompleteMapping gSDCCH_8_1;
extern const CompleteMapping gSDCCH_8_2;
extern const CompleteMapping gSDCCH_8_3;
extern const CompleteMapping gSDCCH_8_4;
extern const CompleteMapping gSDCCH_8_5;
extern const CompleteMapping gSDCCH_8_6;
extern const CompleteMapping gSDCCH_8_7;
extern const CompleteMapping gSDCCH8[8];
//@}
/**@name TCH/F on different slots. */
//@{
extern const CompleteMapping gTCHF_T0;
extern const CompleteMapping gTCHF_T1;
extern const CompleteMapping gTCHF_T2;
extern const CompleteMapping gTCHF_T3;
extern const CompleteMapping gTCHF_T4;
extern const CompleteMapping gTCHF_T5;
extern const CompleteMapping gTCHF_T6;
extern const CompleteMapping gTCHF_T7;
extern const CompleteMapping gTCHF_T[8];
//@}
//@}


}; 	// namespace GSM


#endif

// vim: ts=4 sw=4
