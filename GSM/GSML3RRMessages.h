/**@file @brief L3 Radio Resource messages, GSM 04.08 9.1. */

/*
* Copyright 2008, 2010 Free Software Foundation, Inc.
* Copyright 2011 Kestrel Signal Processing, Inc.
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




#ifndef GSML3RRMESSAGES_H
#define GSML3RRMESSAGES_H

#include "GSMCommon.h"
#include "GSML3Message.h"
#include "GSML3CommonElements.h"
#include "GSML3RRElements.h"

namespace GSM {


/**
	This a virtual class for L3 messages in the Radio Resource protocol.
	These messages are defined in GSM 04.08 9.1.
*/
class L3RRMessage : public L3Message {

	public:

	/** RR Message MTIs, GSM 04.08 Table 10.1/3. */
	enum MessageType {
		///@name System Information
		//@{
		SystemInformationType1=0x19,
		SystemInformationType2=0x1a,
		SystemInformationType2bis=0x02,
		SystemInformationType2ter=0x03,
		SystemInformationType3=0x1b,
		SystemInformationType4=0x1c,
		SystemInformationType5=0x1d,
		SystemInformationType5bis=0x05,
		SystemInformationType5ter=0x06,
		SystemInformationType6=0x1e,
		SystemInformationType7=0x1f,
		SystemInformationType8=0x18,
		SystemInformationType9=0x04,
		SystemInformationType13=0x00,
		SystemInformationType16=0x3d,
		SystemInformationType17=0x3e,
		//@}
		///@name Channel Management
		//@{
		AssignmentCommand=0x2e,
		AssignmentComplete=0x29,
		AssignmentFailure=0x2f,
		ChannelRelease=0x0d,
		ImmediateAssignment=0x3f,
		ImmediateAssignmentExtended=0x39,
		ImmediateAssignmentReject=0x3a,
		AdditionalAssignment=0x3b,
		//@}
		///@name Paging
		//@{
		PagingRequestType1=0x21,
		PagingRequestType2=0x22,
		PagingRequestType3=0x24,
		PagingResponse=0x27,
		//@}
		///@name Handover
		//@{
		HandoverCommand=0x2b,
		//@}
		///@name ciphering
		//@{
		CipheringModeCommand=0x35,
		//@}
		///@name miscellaneous
		//@{
		ChannelModeModify=0x10,
		RRStatus=0x12,
		ChannelModeModifyAcknowledge=0x17,
		ClassmarkChange=0x16,
		ClassmarkEnquiry=0x13,
		MeasurementReport = 0x15,
		GPRSSuspensionRequest=0x34,
		//@}
		///@name special cases -- assigned >8-bit codes to avoid conflicts
		//@{
		SynchronizationChannelInformation=0x100,
		ChannelRequest=0x101,
		//@}
		///@name application information - used for RRLP
		//@{
		ApplicationInformation=0x38,
		//@}
	};


	L3RRMessage():L3Message() { } 
	
	/** Return the L3 protocol discriptor. */
	L3PD PD() const { return L3RadioResourcePD; }

	void text(std::ostream&) const;
};

std::ostream& operator<<(std::ostream& os, L3RRMessage::MessageType);



/** Subclass for L3 RR Messages with no rest octets.  */
class L3RRMessageNRO : public L3RRMessage {

	public:

	L3RRMessageNRO():L3RRMessage() { }

	size_t fullBodyLength() const { return l2BodyLength(); }

};

/** Subclass for L3 RR messages with rest octets */
class L3RRMessageRO : public L3RRMessage {

	public:

	L3RRMessageRO():L3RRMessage() { }

	virtual size_t restOctetsLength() const =0;

	size_t fullBodyLength() const { return l2BodyLength() + restOctetsLength(); }

};


/**
	A Factory function to return a L3RRMessage of the specified MTI.
	Returns NULL if the MTI is not supported.
*/
L3RRMessage* L3RRFactory(L3RRMessage::MessageType MTI);

/**
	Parse a complete L3 radio resource message into its object type.
	@param source The L3 bits.
	@return A pointer to a new message or NULL on failure.
*/
L3RRMessage* parseL3RR(const L3Frame& source);


/** Paging Request Type 1, GSM 04.08 9.1.22 */
class L3PagingRequestType1 : public L3RRMessageNRO {

	private:

	std::vector<L3MobileIdentity> mMobileIDs;
	ChannelType mChannelsNeeded[2];


	public:

	L3PagingRequestType1()
		:L3RRMessageNRO()
	{
		// The empty paging request is a single untyped mobile ID.
		mMobileIDs.push_back(L3MobileIdentity());
		mChannelsNeeded[0]=AnyDCCHType;
		mChannelsNeeded[1]=AnyDCCHType;
	}

	L3PagingRequestType1(const L3MobileIdentity& wId, ChannelType wType)
		:L3RRMessageNRO()
	{
		mMobileIDs.push_back(wId);
		mChannelsNeeded[0]=wType;
		mChannelsNeeded[1]=AnyDCCHType;
	}

	L3PagingRequestType1(const L3MobileIdentity& wId1, ChannelType wType1,
			const L3MobileIdentity& wId2, ChannelType wType2)
		:L3RRMessageNRO()
	{
		mMobileIDs.push_back(wId1);
		mChannelsNeeded[0]=wType1;
		mMobileIDs.push_back(wId2);
		mChannelsNeeded[1]=wType2;
	}

	unsigned chanCode(ChannelType) const;

	int MTI() const { return PagingRequestType1; }

	size_t l2BodyLength() const;
	void writeBody(L3Frame& dest, size_t& wp) const;
	void text(std::ostream&) const;
};




/** Paging Response, GSM 04.08 9.1.25 */
class L3PagingResponse : public L3RRMessageNRO {

	private:

	L3MobileStationClassmark2 mClassmark;
	L3MobileIdentity mMobileID;

	public:

	const L3MobileIdentity& mobileID() const { return mMobileID; }

	int MTI() const { return PagingResponse; }

	size_t l2BodyLength() const;
	void parseBody(const L3Frame& source, size_t &rp);
	void text(std::ostream&) const;

};







/**
	System Information Message Type 1, GSM 04.08 9.1.31
	- Cell Channel Description 10.5.2.1b M V 16 
	- RACH Control Parameters 10.5.2.29 M V 3 
*/
class L3SystemInformationType1 : public L3RRMessageNRO {

	private:

	L3FrequencyList mCellChannelDescription;
	L3RACHControlParameters mRACHControlParameters;

	public:

	L3SystemInformationType1():L3RRMessageNRO() {}

	void RACHControlParameters(const L3RACHControlParameters& wRACHControlParameters)
		{ mRACHControlParameters = wRACHControlParameters; }

	void cellChannelDescription(const L3FrequencyList& wCellChannelDescription)
		{ mCellChannelDescription = wCellChannelDescription; }

	int MTI() const { return (int)SystemInformationType1; }

	size_t l2BodyLength() const { return 19; }
	void writeBody(L3Frame &dest, size_t &wp) const;
	void text(std::ostream&) const;
};






/**
	System Information Type 2, GSM 04.08 9.1.32.
	- BCCH Frequency List 10.5.2.22 M V 16 
	- NCC Permitted 10.5.2.27 M V 1 
	- RACH Control Parameter 10.5.2.29 M V 3 
*/
class L3SystemInformationType2 : public L3RRMessageNRO {

	private:

	L3NeighborCellsDescription mBCCHFrequencyList;
	L3NCCPermitted mNCCPermitted;
	L3RACHControlParameters	mRACHControlParameters;

	public:

	L3SystemInformationType2():L3RRMessageNRO() {}

	void BCCHFrequencyList(const L3NeighborCellsDescription& wBCCHFrequencyList)
		{ mBCCHFrequencyList = wBCCHFrequencyList; }

	void NCCPermitted(const L3NCCPermitted& wNCCPermitted)
		{ mNCCPermitted = wNCCPermitted; }

	void RACHControlParameters(const L3RACHControlParameters& wRACHControlParameters)
		{ mRACHControlParameters = wRACHControlParameters; }

	int MTI() const { return (int)SystemInformationType2; }

	size_t l2BodyLength() const { return 20; }
	void writeBody(L3Frame &dest, size_t &wp) const;
	void text(std::ostream&) const;
};





/**
	System Information Type 3, GSM 04.08 9.1.35
	- Cell Identity 10.5.1.1 M V 2 
	- Location Area Identification 10.5.1.3 M V 5 
	- Control Channel Description 10.5.2.11 M V 3 
	- Cell Options (BCCH) 10.5.2.3 M V 1 
	- Cell Selection Parameters 10.5.2.4 M V 2 
	- RACH Control Parameters 10.5.2.29 M V 3 
*/
class L3SystemInformationType3 : public L3RRMessageRO {

	private:

	L3CellIdentity mCI;
	L3LocationAreaIdentity mLAI;
	L3ControlChannelDescription mControlChannelDescription;
	L3CellOptionsBCCH mCellOptions;
	L3CellSelectionParameters mCellSelectionParameters;
	L3RACHControlParameters mRACHControlParameters;

	bool mHaveRestOctets;
	L3SI3RestOctets mRestOctets;

	public:

	L3SystemInformationType3()
		:L3RRMessageRO(),
		mHaveRestOctets(gConfig.defines("GSM.SI3RO"))
	{ }

	void CI(const L3CellIdentity& wCI) { mCI = wCI; }

	void LAI(const L3LocationAreaIdentity& wLAI) { mLAI = wLAI; }

	void controlChannelDescription(const L3ControlChannelDescription& wControlChannelDescription)
		{ mControlChannelDescription = wControlChannelDescription; }

	void cellOptions(const L3CellOptionsBCCH& wCellOptions)
		{ mCellOptions = wCellOptions; }

	void cellSelectionParameters (const L3CellSelectionParameters& wCellSelectionParameters)
		{ mCellSelectionParameters = wCellSelectionParameters; }

	void RACHControlParameters(const L3RACHControlParameters& wRACHControlParameters)
		{ mRACHControlParameters = wRACHControlParameters; }

	int MTI() const { return (int)SystemInformationType3; }

	size_t l2BodyLength() const { return 16; }
	size_t restOctetsLength() const { return mRestOctets.lengthV(); }
	void writeBody(L3Frame &dest, size_t &wp) const;
	void text(std::ostream&) const;
};






/**
	System Information Type 4, GSM 04.08 9.1.36
	- Location Area Identification 10.5.1.3 M V 5 
	- Cell Selection Parameters 10.5.2.4 M V 2 
	- RACH Control Parameters 10.5.2.29 M V 3 
*/
class L3SystemInformationType4 : public L3RRMessageNRO {

	private:

	L3LocationAreaIdentity mLAI;
	L3CellSelectionParameters mCellSelectionParameters;
	L3RACHControlParameters mRACHControlParameters;

	public:

	L3SystemInformationType4();

	//void LAI(const L3LocationAreaIdentity& wLAI) { mLAI = wLAI; }

	//void cellSelectionParameters (const L3CellSelectionParameters& wCellSelectionParameters)
//		{ mCellSelectionParameters = wCellSelectionParameters; }

//	void RACHControlParameters(const L3RACHControlParameters& wRACHControlParameters)
//		{ mRACHControlParameters = wRACHControlParameters; }

	int MTI() const { return (int)SystemInformationType4; }

	size_t l2BodyLength() const;

	void writeBody(L3Frame &dest, size_t &wp) const;
	void text(std::ostream&) const;
};




/**
	System Information Type 5, GSM 04.08 9.1.37
	- BCCH Frequency List 10.5.2.22 M V 16 
*/
class L3SystemInformationType5 : public L3RRMessageNRO {

	private:

	L3NeighborCellsDescription mBCCHFrequencyList;

	public:

	L3SystemInformationType5():L3RRMessageNRO() { }

	void BCCHFrequencyList(const L3NeighborCellsDescription& wBCCHFrequencyList)
		{ mBCCHFrequencyList = wBCCHFrequencyList; }

	int MTI() const { return (int)SystemInformationType5; }

	size_t l2BodyLength() const { return 16; }
	void writeBody(L3Frame &dest, size_t &wp) const;
	void text(std::ostream&) const;
};





/**
	System Information Type 6, GSM 04.08 9.1.40
	- Cell Identity 10.5.1.11 M V 2 
	- Location Area Identification 10.5.1.3 M V 5 
	- Cell Options (SACCH) 10.5.2.3 M V 1 
	- NCC Permitted 10.5.2.27 M V 1 
*/
class L3SystemInformationType6 : public L3RRMessageNRO {

	private:

	L3CellIdentity mCI;
	L3LocationAreaIdentity mLAI;
	L3CellOptionsSACCH mCellOptions;
	L3NCCPermitted mNCCPermitted;

	public:

	L3SystemInformationType6():L3RRMessageNRO() {}

	void CI(const L3CellIdentity& wCI) { mCI = wCI; }

	void LAI(const L3LocationAreaIdentity& wLAI) { mLAI = wLAI; }

	void cellOptions(const L3CellOptionsSACCH& wCellOptions)
		{ mCellOptions = wCellOptions; }

	void NCCPermitted(const L3NCCPermitted& wNCCPermitted)
		{ mNCCPermitted = wNCCPermitted; }

	int MTI() const { return (int)SystemInformationType6; }

	size_t l2BodyLength() const { return 9; }
	void writeBody(L3Frame &dest, size_t &wp) const;
	void text(std::ostream&) const;
};




/** Immediate Assignment, GSM 04.08 9.1.18 */
class L3ImmediateAssignment : public L3RRMessageNRO {

private:

	L3PageMode mPageMode;
	L3DedicatedModeOrTBF mDedicatedModeOrTBF;
	L3RequestReference mRequestReference;
	L3ChannelDescription mChannelDescription;  
	L3TimingAdvance mTimingAdvance;

public:


	L3ImmediateAssignment(
				const L3RequestReference& wRequestReference,
				const L3ChannelDescription& wChannelDescription,
				const L3TimingAdvance& wTimingAdvance = L3TimingAdvance(0))
		:L3RRMessageNRO(),
		mRequestReference(wRequestReference),
		mChannelDescription(wChannelDescription),
		mTimingAdvance(wTimingAdvance)
	{}

	int MTI() const { return (int)ImmediateAssignment; }
	size_t l2BodyLength() const { return 9; }

	void writeBody(L3Frame &dest, size_t &wp) const;
	void text(std::ostream&) const;

};



/** Immediate Assignment Reject, GSM 04.08 9.1.20 */
class L3ImmediateAssignmentReject : public L3RRMessageNRO {

private:

	L3PageMode mPageMode;
	std::vector<L3RequestReference> mRequestReference;
	L3WaitIndication mWaitIndication;			///< All entries get the same wait indication.

public:


	L3ImmediateAssignmentReject(const L3RequestReference& wRequestReference, unsigned seconds)
		:L3RRMessageNRO(),
		mWaitIndication(seconds)
	{ mRequestReference.push_back(wRequestReference); }

	int MTI() const { return (int)ImmediateAssignmentReject; }

	size_t l2BodyLength() const { return 17; }
	void writeBody(L3Frame &dest, size_t &wp) const;
	void text(std::ostream&) const;

};






/** GSM 04.08 9.1.7 */
class L3ChannelRelease : public L3RRMessageNRO {

private:

	L3RRCause mRRCause;

public:
	
	/** The default cause is 0x0, "normal event". */
	L3ChannelRelease(const L3RRCause& cause = L3RRCause(0x0))
		:L3RRMessageNRO(),mRRCause(cause)
	{}

	int MTI() const { return (int) ChannelRelease; }

	size_t l2BodyLength() const { return mRRCause.lengthV(); }
	void writeBody( L3Frame &dest, size_t &wp ) const; 
	void text(std::ostream&) const;
};




/**
	GSM 04.08 9.1.8
	The channel request message is special because the timestamp
	is implied by the receive time but not actually present in
	the message.
	This messages has no parse or write methods, but is used to
	transfer information from L1 to the control layer.
*/
class L3ChannelRequest : public L3RRMessageNRO {

	private:

	unsigned mRA;		///< request reference
	GSM::Time mTime;		///< receive timestamp

	public:

	L3ChannelRequest(unsigned wRA, const GSM::Time& wTime)
		:L3RRMessageNRO(),
		mRA(wRA), mTime(wTime)
	{}

	/**@name Accessors. */
	//@{
	unsigned RA() const { return mRA; }
	const GSM::Time& time() const { return mTime; }
	//@}

	int MTI() const { return (int)ChannelRequest; }

	size_t l2BodyLength() const { return 0; }
	void writeBody( L3Frame &dest, size_t &wp ) const; 
	void parseBody(const L3Frame&, size_t&);
	void text(std::ostream&) const;
};





/** GSM 04.08 9.1.2 */
class L3AssignmentCommand : public L3RRMessageNRO {

private:

	L3ChannelDescription mChannelDescription;
	L3PowerCommand	mPowerCommand;
	
	bool mHaveMode1;
	L3ChannelMode mMode1;	

public:

	L3AssignmentCommand(const L3ChannelDescription& wChannelDescription,
			const L3ChannelMode& wMode1 )
		:L3RRMessageNRO(),
		mChannelDescription(wChannelDescription),
		mHaveMode1(true),mMode1(wMode1)
	{}

	L3AssignmentCommand(const L3ChannelDescription& wChannelDescription)
		:L3RRMessageNRO(),
		mChannelDescription(wChannelDescription),
		mHaveMode1(false)
	{}



	int MTI() const { return (int) AssignmentCommand; }

	size_t l2BodyLength() const;
	void writeBody( L3Frame &dest, size_t &wp ) const; 
	void text(std::ostream&) const;
};


/** GSM 04.08 9.1.3 */
class L3AssignmentComplete : public L3RRMessageNRO {

	private:

	L3RRCause mCause;

	public:

	///@name Accessors.
	//@{
	const L3RRCause& cause() const { return mCause; }
	//@}

	int MTI() const { return (int) AssignmentComplete; }

	size_t l2BodyLength() const { return 1; }
	void parseBody( const L3Frame &src, size_t &rp );
	void text(std::ostream&) const;

};


/** GSM 04.08 9.1.3 */
class L3AssignmentFailure : public L3RRMessageNRO {

	private:

	L3RRCause mCause;

	public:

	///@name Accessors.
	//@{
	const L3RRCause& cause() const { return mCause; }
	//@}

	int MTI() const { return (int) AssignmentFailure; }

	size_t l2BodyLength() const { return 1; }
	void parseBody( const L3Frame &src, size_t &rp );
	void text(std::ostream&) const;

};


/** GSM 04.08 9.1.29 */
class L3RRStatus : public L3RRMessageNRO {

	private:

	L3RRCause mCause;

	public:

	///@name Accessors.
	//@{
	const L3RRCause& cause() const { return mCause; }
	//@}

	int MTI() const { return (int) RRStatus; }

	size_t l2BodyLength() const { return 1; }
	void parseBody( const L3Frame &src, size_t &rp );
	void text(std::ostream&) const;

};



/** GSM 04.08 9.1.5 */
class L3ChannelModeModify : public L3RRMessageNRO {

	private:

	L3ChannelDescription mDescription;
	L3ChannelMode mMode;

	public:

	L3ChannelModeModify(const L3ChannelDescription& wDescription,
						const L3ChannelMode& wMode)
		:L3RRMessageNRO(),
		mDescription(wDescription),
		mMode(wMode)
	{}

	int MTI() const { return (int) ChannelModeModify; }

	size_t l2BodyLength() const
		{ return mDescription.lengthV() + mMode.lengthV(); }

	void writeBody(L3Frame&, size_t&) const;
	void text(std::ostream&) const;
};


/** GSM 04.08 9.1.6 */
class L3ChannelModeModifyAcknowledge : public L3RRMessageNRO {

	private:

	L3ChannelDescription mDescription;
	L3ChannelMode mMode;

	public:

	const L3ChannelDescription& description() const { return mDescription; }
	const L3ChannelMode& mode() const { return mMode; }

	int MTI() const { return (int) ChannelModeModifyAcknowledge; }

	size_t l2BodyLength() const
		{ return mDescription.lengthV() + mMode.lengthV(); }

	void parseBody(const L3Frame&, size_t&);
	void text(std::ostream&) const;
};


/** GSM 04.08 9.1.21 */
class L3MeasurementReport : public L3RRMessageNRO {

	private:
	// This is a placeholder. We don't really parse anything yet.
	L3MeasurementResults mResults;

	public:

	int MTI() const { return (int) MeasurementReport; }
	size_t l2BodyLength() const { return mResults.lengthV(); }

	void parseBody(const L3Frame&, size_t&);
	void text(std::ostream&) const;

	const L3MeasurementResults results() const { return mResults; }

};


/** GSM 04.08 9.1.53 */
class L3ApplicationInformation : public L3RRMessageNRO {

	private:

	L3APDUID mID;
	L3APDUFlags mFlags;
	L3APDUData mData;

	public:

	~L3ApplicationInformation();

	L3ApplicationInformation();
	// data is the first argument to allow the rest to default, since that is the common case,
	// sending a single (cr=0, first=0, last=0) RRLP (id=0) APDU wrapped in a ApplicationInformation L3 packet.
	L3ApplicationInformation(BitVector& data, unsigned protocolIdentifier=0,
	                         unsigned cr=0, unsigned firstSegment=0, unsigned lastSegment=0);

	///@name Accessors.
	//@{
	const L3APDUID& id() const { return mID; }
	const L3APDUFlags& flags() const { return mFlags; }
	const L3APDUData& data() const { return mData; }
	//@}

	int MTI() const { return (int) ApplicationInformation; }

	size_t l2BodyLength() const;
	void writeBody( L3Frame&, size_t&) const;
	void parseBody( const L3Frame &src, size_t &rp );
	void text(std::ostream&) const;

};


/** GSM 04.08 9.1.13b */
class L3GPRSSuspensionRequest : public L3RRMessageNRO {

	public:

	int MTI() const { return (int) GPRSSuspensionRequest; }

	size_t l2BodyLength() const { return 11; }

	void parseBody(const L3Frame&, size_t&);
};


/** GSM 04.08 9.1.12 */
class L3ClassmarkEnquiry : public L3RRMessageNRO {

	public:

	int MTI() const { return (int) ClassmarkEnquiry; }

	size_t l2BodyLength() const { return 0; }
	void writeBody(L3Frame&, size_t&) const {}
};


/** GSM 04.08 9.1.11 */
class L3ClassmarkChange : public L3RRMessageNRO {

	protected:

	L3MobileStationClassmark2 mClassmark;
	bool mHaveAdditionalClassmark;
	L3MobileStationClassmark3 mAdditionalClassmark;

	public:

	int MTI() const { return (int) ClassmarkChange; }

	size_t l2BodyLength() const;
	void parseBody(const L3Frame&, size_t&);
	void text(std::ostream&) const;

	const L3MobileStationClassmark2& classmark() const { return mClassmark; }
};


} // GSM



#endif
// vim: ts=4 sw=4
