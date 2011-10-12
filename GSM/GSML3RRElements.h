/**@file @brief Elements for Radio Resource messsages, GSM 04.08 10.5.2. */
/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
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


#ifndef GSML3RRELEMENTS_H
#define GSML3RRELEMENTS_H

#include <vector>
#include "GSML3Message.h"
#include <Globals.h>


namespace GSM {


/** Cell Options (BCCH), GSM 04.08 10.5.2.3 */
class L3CellOptionsBCCH : public L3ProtocolElement {

	private:

	unsigned mPWRC;					///< 1 -> downlink power control may be used
	unsigned mDTX;					///< discontinuous transmission state
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
		mRADIO_LINK_TIMEOUT= gConfig.getNum("GSM.RADIO-LINK-TIMEOUT");
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
	unsigned mRADIO_LINK_TIMEOUT;	///< timeout to declare dead phy link

	public:

	/** Sets defaults for no use of DTX or downlink power control. */
	L3CellOptionsSACCH()
		:L3ProtocolElement()
	{
		// Values dictated by the current implementation are hard-coded.
		mPWRC=0;
		mDTX=2;
		// Configuarable values.
		mRADIO_LINK_TIMEOUT=gConfig.getNum("GSM.RADIO-LINK-TIMEOUT");
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

	private:

	unsigned mATT;				///< 1 -> IMSI attach/detach
	unsigned mBS_AG_BLKS_RES;	///< access grant channel reservation
	unsigned mCCCH_CONF;			///< channel combination for CCCH
	unsigned mBS_PA_MFRMS;		///< paging channel configuration
	unsigned mT3212;				///< periodic updating timeout

	public:

	/** Sets reasonable defaults for a single-ARFCN system. */
	L3ControlChannelDescription():L3ProtocolElement()
	{
		// Values dictated by the current implementation are hard-coded.
		mBS_AG_BLKS_RES=2;			// reserve 2 CCCHs for access grant
		mBS_PA_MFRMS=0;				// minimum PCH spacing
		// Configurable values.
		mATT=(unsigned)gConfig.defines("Control.LUR.AttachDetach");
		mCCCH_CONF=gConfig.getNum("GSM.CCCH.CCCH-CONF");
		mT3212=gConfig.getNum("GSM.Timer.T3212")/6;
	}

	size_t lengthV() const { return 3; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};



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
	Neighbor Cells Description, GSM 04.08 10.5.2.22
	(A kind of frequency list.)
	This element describes neighboring cells that may be 
	candidates for handovers.
*/
class L3NeighborCellsDescription : public L3FrequencyList {

	public:

	L3NeighborCellsDescription()
		:L3FrequencyList(gConfig.getVector("GSM.CellSelection.Neighbors"))
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
	}

	size_t lengthV() const { return 1; }
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV(const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};




/** RACH Control Parameters GSM 04.08 10.5.2.29 */
class L3RACHControlParameters : public L3ProtocolElement {

	private:

	unsigned mMaxRetrans;		///< code for 1-7 RACH retransmission attempts
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

	unsigned mDownlink;		///< Indicates the IA reset octets contain additional information.
	unsigned mTMA;			///< This is part of a 2-message assignment.
	unsigned mDMOrTBF;		///< Dedicated link (circuit-switched) or temporary block flow (GPRS/pakcet).

	
public:
	
	L3DedicatedModeOrTBF()
		:L3ProtocolElement(),
		mDownlink(0), mTMA(0), mDMOrTBF(0)
	{}

	size_t lengthV() const { return 1; }
	void writeV(L3Frame &dest, size_t &wp ) const;
	void parseV( const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};



/** ChannelDescription, GSM 04.08 10.5.2.5 */
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
	unsigned mTN; 		//3 bit 

	// Octet 3 & 4.
	unsigned mTSC; 		// 3 bit
	unsigned mHFlag; 	// 1 bit
	unsigned mARFCN;	// 10 bit overflows
	unsigned mMAIO;		// 6 bit overflows
	unsigned mHSN;		// 6 bit
	
public:

	/** Non-hopping initializer. */
	L3ChannelDescription(TypeAndOffset wTypeAndOffset, unsigned wTN,
			unsigned wTSC, unsigned wARFCN)
		:mTypeAndOffset(wTypeAndOffset),mTN(wTN),
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
	

	size_t lengthV() const  { return 3; }
	void writeV( L3Frame &dest, size_t &wp ) const;
	void parseV(const L3Frame& src, size_t &rp);
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

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
class L3RRCause : public L3ProtocolElement
{
	int mCauseValue;

	public:

	/** Constructor cause defaults to "normal event". */
	L3RRCause(int wValue=0)
		:L3ProtocolElement()
	{ mCauseValue=wValue; }

	int causeValue() const { return mCauseValue; }

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
	void parseV( const L3Frame&, size_t&) { assert(0); }
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};



/** GSM 04.08 10.5.2.6 */
class L3ChannelMode : public L3ProtocolElement {

public:

	enum Mode 
	{
		SignallingOnly=0,
		SpeechV1=1,
		SpeechV2=2,
		SpeechV3=3
	};

private:

	Mode mMode;

public:

	L3ChannelMode(Mode wMode=SignallingOnly)
		:L3ProtocolElement(), 
		mMode(wMode)
	{}

	bool operator==(const L3ChannelMode& other) const { return mMode==other.mMode; }
	bool operator!=(const L3ChannelMode& other) const { return mMode!=other.mMode; }

	size_t lengthV() const { return 1; }
	void writeV(L3Frame&, size_t&) const;
	void parseV(const L3Frame&, size_t&);
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream&) const;

};

std::ostream& operator<<(std::ostream&, L3ChannelMode::Mode);




/** GSM 04.08 10.5.2.43 */
class L3WaitIndication : public L3ProtocolElement {

	private:

	unsigned mValue;		///< T3122 or T3142 value in seconds

	public:

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

    BitVector mData; // will contain a RRLP message

	public:

    virtual ~L3APDUData();

	/** Default is a zero length APDUData IE */
	L3APDUData();
    L3APDUData(BitVector data);

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
class L3MeasurementResults : public L3ProtocolElement {

	private:

	bool mBA_USED;
	bool mDTX_USED;
	bool mMEAS_VALID;		///< 0 for valid, 1 for non-valid
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
		mMEAS_VALID(false),
		mNO_NCELL(0)
	{ }

	size_t lengthV() const { return 16; }
	void writeV(L3Frame&, size_t&) const { assert(0); }
	void parseV(const L3Frame&, size_t&);
	void parseV(const L3Frame&, size_t& , size_t) { assert(0); }
	void text(std::ostream& os) const;
	
	/**@name Accessors. */
	//@{

	bool BA_USED() const { return mBA_USED; }
	bool DTX_USED() const { return mDTX_USED; }
	bool MEAS_VALID() const { return mMEAS_VALID; }
	unsigned RXLEV_FULL_SERVING_CELL() const { return mRXLEV_FULL_SERVING_CELL; }
	unsigned RXLEV_SUB_SERVING_CELL() const { return mRXLEV_SUB_SERVING_CELL; }
	unsigned RXQUAL_FULL_SERVING_CELL() const { return mRXQUAL_FULL_SERVING_CELL; }
	unsigned RXQUAL_SUB_SERVING_CELL() const { return mRXQUAL_SUB_SERVING_CELL; }

	unsigned NO_NCELL() const { return mNO_NCELL; }
	unsigned RXLEV_NCELL(unsigned i) const { assert(i<mNO_NCELL); return mRXLEV_NCELL[i]; }
	unsigned RXLEV_NCELL(unsigned *) const;
	unsigned BCCH_FREQ_NCELL(unsigned i) const { assert(i<mNO_NCELL); return mBCCH_FREQ_NCELL[i]; }
	unsigned BCCH_FREQ_NCELL(unsigned *) const;
	unsigned BSIC_NCELL(unsigned i) const { assert(i<mNO_NCELL); return mBSIC_NCELL[i]; }
	unsigned BSIC_NCELL(unsigned *) const;
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




/** A special subclass for rest octets, just in case we need it later. */
class L3RestOctets : public L3ProtocolElement {

};


class L3SI3RestOctets : public L3RestOctets {

	private:

	// We do not yet support the full parameter set.

	bool mHaveSelectionParameters;
	bool mCBQ;
	unsigned mCELL_RESELECT_OFFSET;
	unsigned mTEMPORARY_OFFSET;
	unsigned mPENALTY_TIME;

	public:

	L3SI3RestOctets();

	size_t lengthV() const;
	void writeV(L3Frame& dest, size_t &wp) const;
	void parseV( const L3Frame&, size_t&, size_t) { abort(); }
	void parseV(const L3Frame&, size_t&) { abort(); }
	void text(std::ostream& os) const;

};


} // GSM


#endif



// vim: ts=4 sw=4
