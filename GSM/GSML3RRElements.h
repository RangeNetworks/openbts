/**@file @brief Elements for Radio Resource messsages, GSM 04.08 10.5.2. */
/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012, 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/


#ifndef GSML3RRELEMENTS_H
#define GSML3RRELEMENTS_H

#include <vector>
#include "L3Enums.h"
#include "GSML3Message.h"
#include "GSML3GPRSElements.h"
#include <OpenBTSConfig.h>


namespace GSM {


/** Cell Options (BCCH), GSM 04.08 10.5.2.3 */
class L3CellOptionsBCCH : public L3ProtocolElement {

	private:

	unsigned mPWRC;					///< 1 -> downlink power control may be used
	unsigned mDTX;					///< discontinuous transmission state
	// (pat) From GSM 5.08 5.2: RADIO_LINK_TIMEOUT is a counter used by the MS to decide when the radio link
	// quality is poor enough to cancel the call.  In short, it is a count of how many SACCH messages are lost.
	// From GSM 5.08 section 9 table 1 we learn the value is in SACCH blocks (which are 480ms each) in the range 4-64,
	// ie 15 steps of 4 SACCH blocks each, which translated is about 2 second granularity.
	unsigned mRADIO_LINK_TIMEOUT;	///< timeout to declare dead phy link

	public:

	/** Sets defaults for no use of DTX or downlink power control. */
	L3CellOptionsBCCH()
		:L3ProtocolElement()
	{
		// Values dictated by the current implementation are hard-coded.
		mPWRC=0;
		mDTX=2;
		// Configuarable values.
		mRADIO_LINK_TIMEOUT= gConfig.getNum("GSM.CellOptions.RADIO-LINK-TIMEOUT");
	}

	size_t lengthV() const { return 1; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;
};




/** Cell Options (SACCH), GSM 04.08 10.5.2.3a */
class L3CellOptionsSACCH : public L3ProtocolElement {

	private:

	unsigned mPWRC;					///< 1 -> downlink power control may be used
	unsigned mDTX;					///< discontinuous transmission state
	unsigned mRADIO_LINK_TIMEOUT;	///< timeout to declare dead phy link  See comments at L3CellOptionsBCCH

	public:

	/** Sets defaults for no use of DTX or downlink power control. */
	L3CellOptionsSACCH()
		:L3ProtocolElement()
	{
		// Values dictated by the current implementation are hard-coded.
		mPWRC=0;
		mDTX=2;
		// Configuarable values.
		mRADIO_LINK_TIMEOUT=gConfig.getNum("GSM.CellOptions.RADIO-LINK-TIMEOUT");
	}

	size_t lengthV() const { return 1; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;
	
};





/** Cell Selection Parameters, GSM 04.08 10.5.2.4 */
class L3CellSelectionParameters : public L3ProtocolElement {

	private:

	unsigned mACS;
	unsigned mNECI;
	unsigned mCELL_RESELECT_HYSTERESIS;
	unsigned mMS_TXPWR_MAX_CCH;
	unsigned mRXLEV_ACCESS_MIN;

	public:

	/** Sets defaults to reduce gratuitous handovers. */
	L3CellSelectionParameters()
		:L3ProtocolElement()
	{
		// Values dictated by the current implementation are hard-coded.
		mACS=0;		// We don't support SI16 & SI17 yet.
		// Configurable values.
		mNECI=gConfig.getNum("GSM.CellSelection.NECI");
		mMS_TXPWR_MAX_CCH=gConfig.getNum("GSM.CellSelection.MS-TXPWR-MAX-CCH");
		mRXLEV_ACCESS_MIN=gConfig.getNum("GSM.CellSelection.RXLEV-ACCESS-MIN");
		mCELL_RESELECT_HYSTERESIS=gConfig.getNum("GSM.CellSelection.CELL-RESELECT-HYSTERESIS");
	}

	size_t lengthV() const { return 2; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};




/** Control Channel Description, GSM 04.08 10.5.2.11 */
class L3ControlChannelDescription : public L3ProtocolElement {

	public:

	unsigned mATT;				///< 1 -> IMSI attach/detach
	// BS_AG_BLKS_RES is the number of CCCH NOT used for paging, so the MS does not have to bother listening to them for paging.
	unsigned mBS_AG_BLKS_RES;	///< access grant channel reservation.
	// Values for CCCH_CONF defined in 4.08 10.5.2.11.  1 -> C-V beacon, 2->C-IV beacon.
	unsigned mCCCH_CONF;			///< channel combination for CCCH.
	// BS_PA_MFRMS represents the number of consecutive 51-multiframes used to spread pages.
	// In 5.02 6.5.2 it is assumed to be in the range 2..9,
	// but in 4.08 10.5.2.11 the same exact variable name (BS_PA_MFRMS) has the range 0..7.
	// We are going to use the values 2..9 here, and translate it to 0..7 when writing it to the Control Channel Description IE.
	unsigned mBS_PA_MFRMS;		///< BS_PA_MFRMS in the range 2..9.
	unsigned mT3212;				///< periodic updating timeout

	/** Sets reasonable defaults for a single-ARFCN system. */
	L3ControlChannelDescription();

	bool isCCCHCombined() { return mCCCH_CONF == 1; }
	void validate();

	size_t lengthV() const { return 3; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};

extern L3ControlChannelDescription *gControlChannelDescription;



/**
	A generic frequency list element base class, GSM 04.08 10.5.2.13.
	This implementation supports only the "variable bit map" format
	(GSM 04.08 10.5.2.13.7).
*/
class L3FrequencyList : public L3ProtocolElement {

	protected:

	std::vector<unsigned> mARFCNs;		///< ARFCN list to encode/decode

	public:

	/** Default constructor creates an empty list. */
	L3FrequencyList():L3ProtocolElement() {}

	L3FrequencyList(const std::vector<unsigned>& wARFCNs)
		:L3ProtocolElement(),
		mARFCNs(wARFCNs)
	{}

	//void push_back(unsigned ARFCN) { mARFCNs.push_back(ARFCN); }
	void ARFCNs(const std::vector<unsigned>& wARFCNs) { mARFCNs=wARFCNs; }
	const std::vector<unsigned>& ARFCNs() const { return mARFCNs; }

	size_t lengthV() const { return 16; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

	private:

	/**@name ARFCN set browsing. */
	//@{
	/** Return minimum-numbered ARFCN. */
	unsigned base() const;

	/** Return numeric spread of ARFNs. */
	unsigned spread() const;

	/** Return true if a given ARFCN is in the list. */
	bool contains(unsigned wARFCN) const;
	//@}
};



/**
	Cell Channel Description, GSM 04.08 10.5.2.1b.
	This element is used to provide the Cell Allocation
	for frequency hopping configurations.
	It lists the ARFCNs available for hopping and 
	normally lists all of the ARFCNs for the system.
	It is mandatory, even if you don't use hopping.
*/
class L3CellChannelDescription : public L3FrequencyList {

	public:

	L3CellChannelDescription()
		:L3FrequencyList()
	{}


	void writeV(L3Frame& dest, size_t &wp) const;

};





/**
	Neighbor Cells Description, GSM 04.08 or 44.018 10.5.2.22 and 10.5.2.1b
	Note that in the spec the NeighborCellDescription is just a variation of the CellChannelDescription IE,
	which is where the main documentation resides.
	(A kind of frequency list.)
	This element describes neighboring cells that may be 
	candidates for handovers.
*/
class L3NeighborCellsDescription : public L3FrequencyList {

	public:

	L3NeighborCellsDescription() {}

	//L3NeighborCellsDescription()
	//	:L3FrequencyList(gConfig.getVector("GSM.CellSelection.Neighbors"))
	//{}

	L3NeighborCellsDescription(const std::vector<unsigned>& neighbors)
		:L3FrequencyList(neighbors)
	{}

	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }

	void text(std::ostream&) const;

};




/** NCC Permitted, GSM 04.08 10.5.2.27 */
class L3NCCPermitted : public L3ProtocolElement {

	private:

	unsigned mPermitted;			///< NCC allowance mask (NCCs 0-7)

	public:

	/** Get default parameters from gConfig. */
	L3NCCPermitted()
		:L3ProtocolElement()
	{
		mPermitted = gConfig.getNum("GSM.CellSelection.NCCsPermitted");
		mPermitted |= 1 << gConfig.getNum("GSM.Identity.BSIC.NCC");
	}

	size_t lengthV() const { return 1; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};



// T3126 is how long the MS listens to CCCH after a RACH, and is dependent on parameters broadcast in L3RACHControlParameters.
// 4.08/24.008 says value is T+2S but no longer than 5secs, where T and S are defined in 44.018 3.3.1.2:
// T is the "Tx Integer" broadcast on BCCH (we default to 14), S is defined by table 3.3.1.1.2.1:
// See RACHSpreadSlots and RACHWaitSParam

// (pat) Location Updating is triggered by MM layer, which then requests an RR connection, ie, RACH procedure.
// The RR layer retries M (max retrans) times, which we set to 1, then waits T3126 (max 5 seconds) and if no reply,
// either Immediate Assignment or Immediate Assignment Reject, signals a failure to the MM layer.
// The Immediate Assignment Reject can specify 4 MS and a delay up to 255 seconds.
// Random Access failure behavior is in GSM 4.08 4.4.4.9 paragraph c.
// MS starts T3213 (4sec) for this cell, and retries Loc Update.
// But first, if there are any other suitable cells tries cell reselection first with a 5 second prohibition
// timer to retry this cell.
// MS tries random access procedure twice, then if the update status is UPDATED and stored LAI matches:
//		if attempt counter < 4: keep status updated and retry after T3211 (15sec.)
//		otherwise: set update status to NOT UPDATED and enter MM IDLE sub-state ATTEMPTING TO UPDATE,
//		and retry after T3211 (15Sec) if attempt counter < 4, else T3212 (broadcast on BCCH, in minutes,
//		and as of 2-2014 is set to 0!


/** RACH Control Parameters GSM 04.08 10.5.2.29 */
class L3RACHControlParameters : public L3ProtocolElement {

	private:

	unsigned mMaxRetrans;		///< code for 1-7 RACH retransmission attempts
	// 44.018 Table 10.5.2.29.1: coding of MaxRetransmissions:
	//	0 => Maximum 1 retransmission.
	//	1 => Maximum 2 retransmission.
	//	2 => Maximum 4 retransmission.
	//	3 => Maximum 7 retransmission.
	unsigned mTxInteger;		///< code for 3-50 slots to spread transmission
	unsigned mCellBarAccess;	///< if true, phones cannot camp
	unsigned mRE;				///< if true, call reestablishment is not allowed
	uint16_t mAC;				///< mask of barring flags for the 16 access classes

	public:

	/** Default constructor parameters allows all access. */
	L3RACHControlParameters()
		:L3ProtocolElement()
	{
		// Values ditected by imnplementation are hard-coded.
		mRE=1;
		mCellBarAccess=0;
		// Configurable values.
		mMaxRetrans = gConfig.getNum("GSM.RACH.MaxRetrans");
		mTxInteger = gConfig.getNum("GSM.RACH.TxInteger");
		mAC = gConfig.getNum("GSM.RACH.AC");
	}

	size_t lengthV() const { return 3; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};





/** PageMode, GSM 04.08 10.5.2.26 */
// (pat) The page mode is included in Immediate Assignment and Paging messages.
// If the message is sent on a paging channel, it applies to ALL MS using that paging channel, not just the addressed MS.
// GSM 4.08 3.3.2.1.1: The page mode type "Paging Reorganization" causes the MS to listen to all CCCH and BCCH messages
// until it sees a new Page Mode.
class L3PageMode : public L3ProtocolElement
{


	unsigned mPageMode;

public:

	/** Default mode is "normal paging". */
	L3PageMode(unsigned wPageMode=0)
		:L3ProtocolElement(),
		mPageMode(wPageMode)
	{}

	size_t lengthV() const { return 1; }
	void writeV( L3Frame& dest, size_t &wp ) const;
	void parseV( const L3Frame &src, size_t &rp );
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};



/** DedicatedModeOrTBF, GSM 04.08 10.5.2.25b */
class L3DedicatedModeOrTBF : public L3ProtocolElement {

	// (pat) This is poorly named: mDownlink must be TRUE for a TBF, even if it is an uplink tbf.
	unsigned mDownlink;		///< Indicates the IA reset octets contain additional information.
	unsigned mTMA;			///< This is part of a 2-message assignment.
	unsigned mDMOrTBF;		///< Dedicated link (circuit-switched) or temporary block flow (GPRS/pakcet).

	
public:
	
	L3DedicatedModeOrTBF(bool forTBF, bool wDownlink)
		:L3ProtocolElement(),
		mDownlink(wDownlink), mTMA(0), mDMOrTBF(forTBF)
	{}

	size_t lengthV() const { return 1; }
	void writeV(L3Frame &dest, size_t &wp ) const;
	void parseV( const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};



/** ChannelDescription, GSM 04.18 10.5.2.5
	(pat) The Packet Channel Description, GSM 04.18 10.5.2.25a, is the
	same as the Channel Description except with mTypeAndOffset always 1,
	and for the addition of indirect frequency hopping encoding,
	which is irrelevant for us at the moment.
*/
class L3ChannelDescription : public L3ProtocolElement {


//                  Channel Description Format.
//          7      6      5      4      3     2      1      0
//	  [ ChannelTypeTDMAOffset[4:0]      ][   TN[2:0]		]  Octect 1
//    [         TSC       ][ H=0 ][ SPARE(0,0)][ ARFCN[9:8] ]  Octect 2
//    [                    [ H=1 ][  MAIO[5:2]              ]  Octect 2
//    [                ARFCN[7:0]                           ]  Octect 3 H=0
//    [ MAIO[1:0]  ][        HSN[5:0]                       ]  Octect 3 H=1
//

	// Octet 2.
	TypeAndOffset mTypeAndOffset; // 5 bit
	unsigned mTN; 		//3 bit 	Timeslot Number

	// Octet 3 & 4.
	unsigned mTSC; 		// 3 bit	Training Sequence Number.
	unsigned mHFlag; 	// 1 bit	If true, it is a hopping channel.
	unsigned mARFCN;	// 10 bit overflows
	unsigned mMAIO;		// 6 bit overflows	Mobile Allocation Index Offset (for hopping channels)
	unsigned mHSN;		// 6 bit	Hopping Sequence Number (for hopping channels)
	
public:

	/** Non-hopping initializer. */
	L3ChannelDescription(TypeAndOffset wTypeAndOffset, unsigned wTN,
			unsigned wTSC, unsigned wARFCN)
		:L3ProtocolElement(),
		mTypeAndOffset(wTypeAndOffset),mTN(wTN),
		mTSC(wTSC),
		mHFlag(0),
		mARFCN(wARFCN),
		mMAIO(0),mHSN(0)
	{ }

	/** Blank initializer */
	L3ChannelDescription()
		:mTypeAndOffset(TDMA_MISC),
		mTN(0),mTSC(0),mHFlag(0),mARFCN(0),mMAIO(0),mHSN(0)
	{ }

	bool initialized() { return mTypeAndOffset != TDMA_MISC; }
	

	size_t lengthV() const  { return 3; }
	void writeV( L3Frame &dest, size_t &wp ) const;
	void parseV(const L3Frame& src, size_t &rp);
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

	TypeAndOffset typeAndOffset() const { return mTypeAndOffset; }
	unsigned TN() const { return mTN; }
	unsigned TSC() const{ return mTSC; }
	unsigned ARFCN() const { return mARFCN; }
};

/** GSM 44.018 10.5.2.5a */
class L3ChannelDescription2 : public L3ChannelDescription {

	public:
	
	L3ChannelDescription2(TypeAndOffset wTypeAndOffset, unsigned wTN,
		unsigned wTSC, unsigned wARFCN)
		:L3ChannelDescription(wTypeAndOffset,wTN,wTSC,wARFCN)
	{ }

	L3ChannelDescription2(const L3ChannelDescription &other) : L3ChannelDescription(other) {}

	L3ChannelDescription2() { }
};






/** RequestReference, GSM 04.08 10.5.2.30 */
class L3RequestReference : public L3ProtocolElement
{

//                  Request Reference Format.
//          7      6      5      4      3     2      1      0
//    [                 RequestReference [7:0]              ]  Octet 2
//    [         T1[4:0]                 ][   T3[5:3]        ]  Octet 3
//    [       T3[2:0]     ][            T2[4:0]             ]  Octet 4

	unsigned mRA;			///< random tag from original RACH burst

	/**@name Timestamp of the corresponing RACH burst. */
	//@{
	unsigned mT1p;		///< T1 mod 32
	unsigned mT2;
	unsigned mT3;
	//@}

public:

	L3RequestReference() {}

	L3RequestReference(unsigned wRA, const GSM::Time& when)
		:mRA(wRA),
		mT1p(when.T1()%32),mT2(when.T2()),mT3(when.T3())
	{}

	size_t lengthV() const { return 3; }
	void writeV(L3Frame &, size_t &wp ) const;
	void parseV( const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};



/** Timing Advance, GSM 04.08 10.5.2.40 */
class L3TimingAdvance : public L3ProtocolElement
{
//							TimingAdvance
//          7      6      5      4      3     2      1      0
//    [    spare(0,0)     ][      TimingAdvance [5:0]              ]  Octet 1

	unsigned mTimingAdvance;
	
public:

	L3TimingAdvance(unsigned wTimingAdvance=0)
		:L3ProtocolElement(),
		mTimingAdvance(wTimingAdvance)
	{}
	
	size_t lengthV() const { return 1; }
	void writeV(L3Frame&, size_t &wp) const;
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};




/** GSM 04.08 10.5.2.31 */
// 44.018 10.5.2.31
// (pat) The default RR cause 0 indicates "Normal Event"
class L3RRCauseElement : public L3ProtocolElement
{
	private: RRCause mCauseValue;

	public:

	/** Constructor cause defaults to "normal event". */
	L3RRCauseElement(RRCause wValue=L3RRCause::Normal_Event)
		:L3ProtocolElement()
	{ mCauseValue=wValue; }

	RRCause causeValue() const { return mCauseValue; }

	size_t lengthV() const { return 1; }
	void writeV(L3Frame&, size_t&) const;
	void parseV(const L3Frame&, size_t&);
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};





/** GSM 04.08 10.5.2.28 */
class L3PowerCommand : public L3ProtocolElement
{
	unsigned mCommand;

public:

	L3PowerCommand(unsigned wCommand=0)
		:L3ProtocolElement(),
		mCommand(wCommand)
	{}

	size_t lengthV() const { return 1; }
	void writeV( L3Frame &dest, size_t &wp ) const;
	void parseV( const L3Frame&, size_t&);
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};



/** GSM 04.08 10.5.2.6 */
class L3ChannelMode : public L3ProtocolElement {

public:

	enum Mode 
	{
		SignallingOnly=0,
		SpeechV1=1,	// GSM FR or HR
		SpeechV2=2,	// GSM EFR (half rate not defined in this version of the protocol.)
		SpeechV3=3	// AMR FR or HR
	};

private:

	Mode mMode;

public:

	L3ChannelMode(Mode wMode=SignallingOnly)
		:L3ProtocolElement(), 
		mMode(wMode)
	{}

	bool isAMR() const { return mMode == SpeechV3; }

	bool operator==(const L3ChannelMode& other) const { return mMode==other.mMode; }
	bool operator!=(const L3ChannelMode& other) const { return mMode!=other.mMode; }

	size_t lengthV() const { return 1; }
	void writeV(L3Frame&, size_t&) const;
	void parseV(const L3Frame&, size_t&);
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};


// 3GPP 44.018 10.5.2.21aa
// (pat) 10-2012: Multi-rate element needed for AMR codecs when L3ChannelMode is set to SpeechV3.
// Not to be confused with unrelated GPRS multi-slot.
// It is sent in the L3 Channel Mode Modify, possibly among others.
// It is a variable length element with room to specify multiple codecs,
// but we do not support AMR rate adaptation yet so we use it with just one codec.
class L3MultiRateConfiguration : public L3ProtocolElement {
	// IE specifies a set of 4 among the many AMR codecs, but there are only 3 possible configurations 
	// that are ever of any interest to us:  AMR_FR, AMR_HR, and if we ever support
	// rate-adaptation, the recommended multi-rate set from 28.062 table 7.11.3.1.3-2,
	// which is Config-NB-1 includes codecs: 12.2, 7.4, 5.9, 4.75 for FR_AMR and compatible with GSM AMR.
	// For multi-rate we must also specify the threshold and hystersis to switch codecs, which are black
	// magic and possibly dependent on MS manufacturer - gotta love that, and we also have to implement
	// the TFO/TrFO (Tandem Free/Transcoder Free) Operation controller, not to mention implementing
	// codec negotation through SIP.  It is a lot of work.

	// The options byte is a bit field defined in 10.5.2.21aa.1.
	// There are only 3 possible values that will ever by useful:
	enum AmrCodecSet {
		codec_set_AMR_FR = 0x80,
		codec_set_AMR_HR = 0x10,
		codec_set_UMTS_AMR = 0x80 + 0x10 + 4 + 1	// AMR multi-rate config, in case we ever use it.
		};
	unsigned mOptions;	// octet "3".
	AmrCodecSet mAmrCodecSet;		// Choose one of the above.
	public:
	L3MultiRateConfiguration(bool halfrate=0) {
		mOptions = 0x20;	// AMR version 1 (ie, not WB), no noise suppression, no initial codec.
		mAmrCodecSet = halfrate ? codec_set_AMR_HR : codec_set_AMR_FR;
	}
	// excluding IEI and the length byte itself, the fixed length for a single non-adaptive codec is 2.
	size_t lengthV() const { return 2; }
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }	// Use this one for TLV format
	void writeV(L3Frame&, size_t&) const;
	void text(std::ostream&) const;
};

std::ostream& operator<<(std::ostream&, L3ChannelMode::Mode);




/** GSM 04.08 10.5.2.43 */
class L3WaitIndication : public L3ProtocolElement {

	public:

	unsigned mValue;		///< T3122 or T3142 value in seconds

	L3WaitIndication(unsigned seconds)
		:L3ProtocolElement(),
		mValue(seconds)
	{}

	size_t lengthV() const { return 1; }
	void writeV(L3Frame& dest, size_t &wp) const
		{ dest.writeField(wp,mValue,8); }
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream& os) const { os << mValue; }

};

/**
 Application Information Information Elements (encapsulates the RRLP message)
 */

/** GSM 04.08 10.5.2.48 */
class L3APDUID : public L3ProtocolElement {

	private:

	unsigned mProtocolIdentifier;

	public:

	/** Default Protocol Identifier is RRLP=0, the only one defined so far (rest are reserved). */
	L3APDUID(unsigned protocolIdentifier=0)
		:L3ProtocolElement(),
		mProtocolIdentifier(protocolIdentifier)
	{}

	size_t lengthV() const { return 1; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&);
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream& os) const;

};


/** GSM 04.08 10.5.2.49 */
class L3APDUFlags : public L3ProtocolElement {

	private:

    // TODO - use bool for flags?
	unsigned mCR;
	unsigned mFirstSegment;
	unsigned mLastSegment;
    // TODO - put enums for CR, FirstSegment, LastSegment

	public:

	/** Default is the flags for a single segment APDU - one that fits in a single
        Application Information message **/
	L3APDUFlags(unsigned cr=0, unsigned firstSegment=0, unsigned lastSegment=0)
		:L3ProtocolElement(),
		mCR(cr), mFirstSegment(firstSegment), mLastSegment(lastSegment)
	{}

	size_t lengthV() const { return 1; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&);
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream& os) const;

};


/** GSM 04.08 10.5.2.50 */
class L3APDUData : public L3ProtocolElement {

	private:

    BitVector2 mData; // will contain a RRLP message

	public:

    virtual ~L3APDUData();

	/** Default is a zero length APDUData IE */
	L3APDUData();
    L3APDUData(BitVector2 data);

	size_t lengthV() const
	{
		// Return number of bytes neede to hold mData
		size_t sz = mData.size();
		size_t ln = sz/8;
		if (sz % 8) ln++;
		return ln;
	}

	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV( const L3Frame& src, size_t &rp, size_t expectedLength );
	void parseV(const L3Frame&, size_t&) { abort(); }
	void text(std::ostream& os) const;

};




/** GSM 04.08 10.5.2.20 */
// (pat) Also see 5.08
class L3MeasurementResults : public L3ProtocolElement {

	private:

	bool mBA_USED;
	bool mDTX_USED;
	bool mMEAS_VALID;		///< 0 for valid, 1 for non-valid  NOTE!!
	// (pat) 5.08 8.1.4 defines RXLEV values.  MS measures RMS signal level in the range -110dB to -48dB.
	// RXLEV 0 = < -110 dBm + SCALE
	// RXLEV 1 = < -109 dBm + SCALE
	// ...
	// RXLEV 63 = > -48 dBm + SCALE
	// The SCALE is used for Enhanced Measurement Report and is inconsequential for normal report.
	// The MS always reports the 6 best cells from among the candidates in the BA list sent by the BTS.
	unsigned mRXLEV_FULL_SERVING_CELL;
	unsigned mRXLEV_SUB_SERVING_CELL;
	unsigned mRXQUAL_FULL_SERVING_CELL;
	unsigned mRXQUAL_SUB_SERVING_CELL;

	unsigned mNO_NCELL;
	unsigned mRXLEV_NCELL[6];
	unsigned mBCCH_FREQ_NCELL[6];
	unsigned mBSIC_NCELL[6];

	public:

	L3MeasurementResults()
		:L3ProtocolElement(),
		mMEAS_VALID(true),			// true means invalid.
		mRXLEV_FULL_SERVING_CELL(0),
		mRXLEV_SUB_SERVING_CELL(0),
		mRXQUAL_FULL_SERVING_CELL(0),
		mRXQUAL_SUB_SERVING_CELL(0),
		mNO_NCELL(0)
	{ }

	size_t lengthV() const { return 16; }
	void writeV(L3Frame&, size_t&) const { assert(0); }
	void parseV(const L3Frame&, size_t&);
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream& os) const;
	string text() const;
	
	/**@name Accessors. */
	//@{

	bool BA_USED() const { return mBA_USED; }
	bool DTX_USED() const { return mDTX_USED; }
	// The value MEAS_VALID == 0 means the serving cell measurements were valid; it is intuitively backwards.
	//bool MEAS_VALID() const { return mMEAS_VALID; }	// Get rid of this intuitively backwards method.
	bool isServingCellValid() const { return mMEAS_VALID == 0; }
	// (pat) The _RAW versions return the raw data from the IE, which must be scaled to convert to dB.
	unsigned RXLEV_FULL_SERVING_CELL_RAW() const { return mRXLEV_FULL_SERVING_CELL; }
	unsigned RXLEV_SUB_SERVING_CELL_RAW() const { return mRXLEV_SUB_SERVING_CELL; }
	unsigned RXQUAL_FULL_SERVING_CELL() const { return mRXQUAL_FULL_SERVING_CELL; }
	unsigned RXQUAL_SUB_SERVING_CELL() const { return mRXQUAL_SUB_SERVING_CELL; }

	unsigned NO_NCELL() const { return mNO_NCELL; }
	unsigned RXLEV_NCELL_RAW(unsigned i) const { assert(i<mNO_NCELL); return mRXLEV_NCELL[i]; }
	unsigned RXLEV_NCELLs(unsigned *) const;
	unsigned BCCH_FREQ_NCELL(unsigned i) const { assert(i<mNO_NCELL); return mBCCH_FREQ_NCELL[i]; }
	unsigned BCCH_FREQ_NCELLs(unsigned *) const;
	unsigned BSIC_NCELL(unsigned i) const { assert(i<mNO_NCELL); return mBSIC_NCELL[i]; }
	unsigned BSIC_NCELLs(unsigned *) const;
	//@}

	/**@ Real-unit conversions. */
	//@{
	/** Given an encoded level, return a value in dBm. */
	int decodeLevToDBm(unsigned lev) const;
	/** Given an encoded quality, return a BER. */
	float decodeQualToBER(unsigned qual) const;
	/**@ Converted accessors. */
	//@{
	int RXLEV_FULL_SERVING_CELL_dBm() const
		{ return decodeLevToDBm(mRXLEV_FULL_SERVING_CELL); }
	int RXLEV_SUB_SERVING_CELL_dBm() const
		{ return decodeLevToDBm(mRXLEV_SUB_SERVING_CELL); }
	float RXQUAL_FULL_SERVING_CELL_BER() const
		{ return decodeQualToBER(mRXQUAL_FULL_SERVING_CELL); }
	float RXQUAL_SUB_SERVING_CELL_BER() const
		{ return decodeQualToBER(mRXQUAL_SUB_SERVING_CELL); }
	int RXLEV_NCELL_dBm(unsigned i) const
		{ assert(i<mNO_NCELL); return decodeLevToDBm(mRXLEV_NCELL[i]); }
	//@}
	//@}

};


/** GSM 04.08 10.5.2.2 */
class L3CellDescription : public L3ProtocolElement {

	protected:

	unsigned mARFCN;
	unsigned mNCC;
	unsigned mBCC;

	public:

	L3CellDescription( unsigned wARFCN, unsigned wNCC, unsigned wBCC)
		:L3ProtocolElement(),
		mARFCN(wARFCN),
		mNCC(wNCC),mBCC(wBCC)
	{ }

	L3CellDescription() { }

	size_t lengthV() const { return 2; }
	void writeV(L3Frame&, size_t&) const;
	void parseV(const L3Frame&, size_t&);
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;
};


/** GSM 04.08 10.5.2.15 */
class L3HandoverReference : public L3ProtocolElement
{
	protected:

	unsigned mValue;	// Range 0 to 255


	public:

	L3HandoverReference(unsigned wValue)
		:L3ProtocolElement(),
		mValue(wValue)
	{}

	L3HandoverReference() { }

	size_t lengthV() const { return 1; }
	void writeV(L3Frame &, size_t &wp ) const;
	void parseV( const L3Frame&, size_t&, size_t) { abort(); }
	void parseV(const L3Frame&src, size_t&rp) { mValue = src.readField(rp,8); }
	void text(std::ostream&) const;

	unsigned value() const { return mValue; }
};

/** GSM 04.08 10.5.2.9 */
class L3CipheringModeSetting : public L3ProtocolElement
{
	protected:

	bool mCiphering;
	int mAlgorithm;  // algorithm is A5/mAlgorithm, 1 means A5_1, which is encoded in the IE as 0.

	public:

	// Pass in the algorithm number, ie 1 means A5_1, 2 means A5_2, etc.
	L3CipheringModeSetting(bool wCiphering, int wAlgorithm)
		:mCiphering(wCiphering), mAlgorithm(wAlgorithm)
	{
		// devassert(wAlgorithm >= 1 && wAlgorithm <= 7);
	}

	size_t lengthV() const;
	void writeV(L3Frame&, size_t& wp) const;
	void parseV( const L3Frame&, size_t&, size_t) { abort(); }
	void parseV(const L3Frame&, size_t&) { abort(); }
	void text(std::ostream&) const;
};

/** GSM 04.08 10.5.2.10 */
class L3CipheringModeResponse : public L3ProtocolElement
{
	protected:

	bool mIncludeIMEISV;

	public:

	L3CipheringModeResponse(bool wIncludeIMEISV)
		:mIncludeIMEISV(wIncludeIMEISV)
	{ }

	size_t lengthV() const;
	void writeV(L3Frame&, size_t& wp) const;
	void parseV( const L3Frame&, size_t&, size_t) { abort(); }
	void parseV(const L3Frame&, size_t&) { abort(); }
	void text(std::ostream&) const;
};

/** GSM 04.08 10.5.2.39 */
class L3SynchronizationIndication : public L3ProtocolElement
{
	protected:

	bool mNCI;		// Normal Cell Indication.  0 => ignore TA out of range. 1 => handover fail on TA out of range.
	bool mROT;		// Report Observed Time difference in Handover Complete.
	int mSI;		// Synchronization Indication.  0 = unsynchronized.

	public:

	L3SynchronizationIndication(bool wNCI, bool wROT, int wSI = 0)
		:L3ProtocolElement(),
		mNCI(wNCI),
		mROT(wROT),
		mSI(wSI & 3)
	{}

	L3SynchronizationIndication() { }

	size_t lengthV() const { return 1; }
	void writeV(L3Frame &, size_t &wp ) const;
	void parseV( const L3Frame&, size_t&, size_t) { abort(); }
	void parseV(const L3Frame&, size_t&);
	void text(std::ostream&) const;

	unsigned NCI() const { return mNCI; }
	unsigned ROT() const { return mROT; }
};


/** GSM 04.08 10.5.2.28a */
// (pat) Defaults to 0 which is maximum possible power.
class L3PowerCommandAndAccessType : public L3PowerCommand { };






/** A special subclass for rest octets, just in case we need it later. */
class L3RestOctets : public L3ProtocolElement {

};


class L3SI3RestOctets : public L3RestOctets {

	private:

	// We do not yet support the full parameter set.
	bool mHaveSI3RestOctets;

	bool mHaveSelectionParameters;
	bool mCBQ;
	unsigned mCELL_RESELECT_OFFSET;		// 6 bits
	unsigned mTEMPORARY_OFFSET;			// 3 bits
	unsigned mPENALTY_TIME;				// 5 bits
	unsigned mRA_COLOUR;				// (pat) In GPRS_Indicator, 3 bits
	bool mHaveGPRS;						// (pat)

	public:

	L3SI3RestOctets();

	size_t lengthV() const;
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV( const L3Frame&, size_t&, size_t) { abort(); }
	void parseV(const L3Frame&, size_t&) { abort(); }
	void text(std::ostream& os) const;

};


#if 0
/** GSM 04.60 12.24 */
// (pat) Someone kindly added this before I got here.
// This info is included in the SI13 rest octets.
class L3GPRSCellOptions : public L3ProtocolElement {

	private:

	unsigned mNMO;			// Network Mode of Operation See GSM 03.60 6.3.3.1
	unsigned mT3168;		// range 0..7
	unsigned mT3192;		// range 0..7
	unsigned mDRX_TIMER_MAX;
	unsigned mACCESS_BURST_TYPE;
	unsigned mCONTROL_ACK_TYPE;
	unsigned mBS_VC_MAX;

	public:

	L3GPRSCellOptions()
		:L3ProtocolElement(),
		mNMO(2),	// (pat) 2 == Network Mode of Operation III, which means
					// GPRS attached MS uses Packet Paging channel
					// if allocated (which it wont be), otherwise CCCH.
		mT3168(gConfig.getNum("GPRS.CellOptions.T3168Code")),
		mT3192(gConfig.getNum("GPRS.CellOptions.T3192Code")),
		mDRX_TIMER_MAX(gConfig.getNum("GPRS.CellOptions.DRX_TIMER_MAX")),
		mACCESS_BURST_TYPE(0),	// (pat) 0 == use 8 bit format of Packet Channel Request Message.
		mCONTROL_ACK_TYPE(1),	// (pat) 1 == default format for Packet Control Acknowledgement
								// is RLC/MAC block, ie, not special.
		mBS_VC_MAX(gConfig.getNum("GPRS.CellOptions.BS_VC_MAX"))
	{ }

	size_t lengthV() const { return 2+3+3+3+1+1+4+1+1; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV( const L3Frame&, size_t&, size_t) { abort(); }
	void parseV(const L3Frame&, size_t&) { abort(); }
	void text(std::ostream& os) const;
};
#endif



#if 0
/** GSM 04.60 12.13 */
class L3GPRSPowerControlParameters : public L3ProtocolElement {

	private:

	unsigned mALPHA;	///< GSM 04.60 Table 12.9.2

	public:

	L3GPRSPowerControlParameters()
		:L3ProtocolElement(),
		mALPHA(gConfig.getNum("GPRS.PowerControl.ALPHA"))
	{ }

	size_t lengthV() const { return 4+8; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV( const L3Frame&, size_t&, size_t) { abort(); }
	void parseV(const L3Frame&, size_t&) { abort(); }
	void text(std::ostream& os) const;
};
#endif



/** GSM 04.08 10.5.2.37b */
class L3SI13RestOctets : public L3RestOctets {

	private:

	unsigned mRAC;				///< routing area code GSM03.03
	bool mSPGC_CCCH_SUP;			///< indicates support of SPLIT_PG_CYCLE on CCCH
	unsigned mPRIORITY_ACCESS_THR;
	unsigned mNETWORK_CONTROL_ORDER;	///< network reselection behavior
	const L3GPRSCellOptions mCellOptions;
	const L3GPRSSI13PowerControlParameters mPowerControlParameters;

	public:

	L3SI13RestOctets()
		:L3RestOctets(),
		mRAC(gConfig.getNum("GPRS.RAC")),	// (pat) aka Routing Area Code.
		mSPGC_CCCH_SUP(false),	// SPLIT_PG_CYCLE is NOT supported on CCCH.
		// See GSM04.08 table 10.5.76: Value 6 means any priority packet access allowed.
		mPRIORITY_ACCESS_THR(gConfig.getNum("GPRS.PRIORITY-ACCESS-THR")),
		// (pat) GSM05.08 sec 10.1.4: This controls whether the MS
		// does cell reselection or the network, and appears to apply
		// only to GPRS mode.  Value NC2 = 2 means the network
		// performs cell reselection, but it has a side-effect that
		// the MS continually sends Measurement reports, and these
		// clog up the RACH channel so badly that the MS cannot send
		// enough uplink messages to make the SGSN happy.
		// The reporting interval values are as follows,
		// defined in GSM04.60 11.2.23 table 11.2.23.2
		// NC_REPORTING_PERIOD_I - used in packet idle mode.
		// NC_REPORTING_PERIOD_T - used in packet transfer mode.
		// They can be in PSI5, SI2quarter (but I dont see them there),
		// or by a PACKET MEASUREMENT ORDER msg.
		// I am going to set this to 0 for now instead of 2.
		// Update: Lets set it back to see if the MS keeps sending measurement reports when it is non-responsive.
		// Update: If the MS is requested to make measurement reports it
		// reduces the multislot capability.  Measurements described 45.008
		mNETWORK_CONTROL_ORDER(gConfig.getNum("GPRS.NC.NetworkControlOrder")),
		mPowerControlParameters()	// redundant explicit call
	{ }

	size_t lengthBits() const
	{ return 1+3+4+1+1+8+1+3+2+mCellOptions.lengthBits()+mPowerControlParameters.lengthBits(); }
	size_t lengthV() const
	{ return (lengthBits() + 7) / 8; }

	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV( const L3Frame&, size_t&, size_t) { abort(); }
	void parseV(const L3Frame&, size_t&) { abort(); }
	void text(std::ostream& os) const;

};

unsigned countBeaconTimeslots(int BS_CC_CHANS);

} // GSM


#endif



// vim: ts=4 sw=4
