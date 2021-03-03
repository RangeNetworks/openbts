/**@file Messages for Call Control, GSM 04.08 9.3. */

/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* Copyright 2011, 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.mcall

*/



#ifndef GSML3CCMESSAGES_H
#define GSML3CCMESSAGES_H

#include "GSMCommon.h"
#include "GSML3Message.h"
#include "GSML3CommonElements.h"
#include "GSML3CCElements.h"
#include "GSML3SSMessages.h"
#include <ControlTransfer.h>


namespace GSM { 



/**
	This a virtual class for L3 messages in the Call Control protocol.
	These messages are defined in GSM 04.08 9.3.

	GSM call control is based on and nearly identical to ISDN call control.
	ISDN call control is defined in ITU-T Q.931.
*/
class L3CCMessage : public L3Message {

	private:

	unsigned mTI;		///< short transaction ID, GSM 04.07 11.2.3.1.3; upper bit is originator flag
						// Uppder bit is 0 for originating side, see GSM 04.07 11.2.3.1.3.

	public:

	/** GSM 04.08, Table 10.3, bit 6 is "don't care" */
	enum MessageType {
		/**@name call establishment */
		//@{
		Alerting=0x01,
		CallConfirmed=0x08,
		CallProceeding=0x02,
		Connect=0x07,
		Setup=0x05,
		EmergencySetup=0x0e,
		ConnectAcknowledge=0x0f,
		Progress=0x03,
		//@}
		/**@name call clearing */
		//@{
		Disconnect=0x25,		// 37
		Release=0x2d,
		ReleaseComplete=0x2a,	// 42
		//@}
		/**@name DTMF */
		//@{
		StartDTMF=0x35,
		StopDTMF=0x31,
		StopDTMFAcknowledge=0x32,
		StartDTMFAcknowledge=0x36,
		StartDTMFReject=0x37,
		//@}
		/**@name In-call services */
		//@{
		Hold=0x18,
		HoldReject=0x1a,
		//@}
		/**@name error reporting */
		//@{
		CCStatus= 0x3d
		//@}
	};

	L3CCMessage(unsigned wTI)
		:L3Message(),mTI(wTI)
	{}


	size_t fullBodyLength() const { return l2BodyLength(); }

	/** Override the write method to include transaction identifiers in header. */
	void write(L3Frame& dest) const;

	L3PD PD() const { return L3CallControlPD; }

	unsigned TI() const { return mTI; }
	void TI(unsigned wTI) { mTI = wTI; }

	void text(std::ostream&) const;
};


class L3CCCommonIEs {
	public:
	Bool_z mHaveFacility;
	L3SupServFacilityIE mFacility;
	// The SS Version is variable length, which is nonsense, because it is one byte which can take the values 0 or 1.  Woo hoo!
	Bool_z mHaveSSVersion;
	L3SupServVersionIndicator mSSVersion;

	void ccCommonText(std::ostream&) const;
	void ccCommonParse( const L3Frame &src, size_t &rp );
	void ccCommonWrite( L3Frame &dest, size_t &wp ) const;
	size_t ccCommonLength() const;
};


std::ostream& operator<<(std::ostream& os, L3CCMessage::MessageType MTI);


/**
	Parse a complete L3 call control message into its object type.
	@param source The L3 bits.
	@return A pointer to a new message or NULL on failure.
*/
L3CCMessage* parseL3CC(const L3Frame& source);

/**
	A Factory function to return a L3CCMessage of the specified MTI.
	Returns NULL if the MTI is not supported.
*/
L3CCMessage* L3CCFactory(L3CCMessage::MessageType MTI);


/** GSM 04.08 9.3.19 */
class L3Release : public L3CCMessage, public L3CCCommonIEs  {

	private:

	// We're ignoring "facility" and "user-user" for now.
	bool mHaveCause;
	L3CauseElement mCause;

	public:

	L3Release(unsigned wTI=7)
		:L3CCMessage(wTI),
		mHaveCause(false)
	{}

	L3Release(unsigned wTI, const CCCause wCause)
		:L3CCMessage(wTI),
		mHaveCause(true),mCause(wCause)
	{}

	bool haveCause() const { return mHaveCause; }
	const L3CauseElement& cause() const { return mCause; }
	int MTI() const { return Release; }
	void writeBody( L3Frame &dest, size_t &wp ) const;
	void parseBody( const L3Frame &src, size_t &rp );
	size_t l2BodyLength() const;

	void text(std::ostream&) const;

};


class L3CCStatus : public L3CCMessage 
{
private:
	L3CauseElement mCause;
	L3CallState mCallState;
public:

	L3CCStatus(unsigned wTI=7)
		:L3CCMessage(wTI)
	{}

	L3CCStatus(unsigned wTI, const CCCause wCause, const L3CallState &wCallState)
		:L3CCMessage(wTI), 
		mCause(wCause), 
		mCallState(wCallState)
	{}
	
	const L3CauseElement& cause() const { return mCause; }
	const L3CallState callState() const { return mCallState; }

	int MTI() const { return CCStatus; }
	void writeBody( L3Frame &dest, size_t &wp ) const;
	void parseBody( const L3Frame &src, size_t &rp );
	size_t l2BodyLength() const { return 4; }

	void text(std::ostream&) const;
};



/** GSM 04.08 9.3.19 */
class L3ReleaseComplete : public L3CCMessage, public L3CCCommonIEs {

	private:

	// We're ignoring "facility" and "user-user" for now.
	bool mHaveCause;
	L3CauseElement mCause;

	public:

	L3ReleaseComplete(unsigned wTI=7)
		:L3CCMessage(wTI),
		mHaveCause(false)
	{}

	L3ReleaseComplete(unsigned wTI, const CCCause wCause)
		:L3CCMessage(wTI),
		mHaveCause(true),mCause(wCause)
	{}

	int MTI() const { return ReleaseComplete; }
	void writeBody( L3Frame &dest, size_t &wp ) const;
	void parseBody( const L3Frame &src, size_t &rp );
	size_t l2BodyLength() const;

	void text(std::ostream&) const;

};




/**
	GSM 04.08 9.3.23
	This message can have different forms for uplink and downlink
	but the TLV format is flexiable enough to allow us to use one class for both.
*/
class L3Setup : public L3CCMessage, public L3CCCapabilities, public L3CCCommonIEs
{
	// We fill in IEs one at a time as we need them.

	/// Calling Party BCD Number (0x5C O TLV 3-19 ).
	Bool_z mHaveCallingPartyBCDNumber;
	L3CallingPartyBCDNumber mCallingPartyBCDNumber;

	/// Called Party BCD Number (0x5E O TLV 3-19).
	Bool_z mHaveCalledPartyBCDNumber;
	L3CalledPartyBCDNumber mCalledPartyBCDNumber;

	Bool_z mHaveSignal;
	L3Signal mSignal;
public:

	L3Setup(unsigned wTI=7)
		:L3CCMessage(wTI)
	{ }

	
	L3Setup(unsigned wTI, const L3CalledPartyBCDNumber& wCalledPartyBCDNumber)
		:L3CCMessage(wTI) ,
		mHaveCalledPartyBCDNumber(true),mCalledPartyBCDNumber(wCalledPartyBCDNumber)
	{ }
	
	// (pat) This is cleverly converting callingPartyBCDNumber to a char string and back.
	L3Setup(unsigned wTI, const L3CallingPartyBCDNumber& wCallingPartyBCDNumber)
		:L3CCMessage(wTI), 
		mHaveCallingPartyBCDNumber(true),mCallingPartyBCDNumber(wCallingPartyBCDNumber)
	{ }

	void setSignal(L3Signal &whatever) { mHaveSignal = true; mSignal = whatever; }


	/** Accessors */
	//@{
	bool haveCalledPartyBCDNumber() const { return mHaveCalledPartyBCDNumber; }

	const L3CalledPartyBCDNumber& calledPartyBCDNumber() const
		{ assert(mHaveCalledPartyBCDNumber); return mCalledPartyBCDNumber; }
	//@}

	int MTI() const { return Setup; }
	void writeBody( L3Frame &dest, size_t &wp ) const;
	void parseBody( const L3Frame &src, size_t &rp );

	size_t l2BodyLength() const;

	void text(std::ostream&) const;
};


/**
	GSM 04.08 9.3.8
*/
// (pat) 5-2013: This uses a subset of the IEs as L3Setup, so we can use that class to parse it.
//class L3EmergencySetup : public L3CCMessage
class L3EmergencySetup : public L3Setup
{
public:

	//L3EmergencySetup(unsigned wTI=7) :L3CCMessage(wTI) { }
	
	int MTI() const { return EmergencySetup; }
	//void parseBody(const L3Frame &src, size_t &rp) {}	// parseBody supplied by L3Setup.
	size_t l2BodyLength() const { return 0; }	// We dont write this message, so we dont care about this.
};




/** GSM 04.08 9.3.3 */
class L3CallProceeding : public L3CCMessage {

	private:

	// We'fill in IEs one at a time as we need them.

	// (pat) Should be modified to use L3CCCapabilities.
	//bool mHaveBearerCapability;
	L3BearerCapability mBearerCapability;

	bool mHaveProgress;
	L3ProgressIndicator mProgress;

public:

	L3CallProceeding(unsigned wTI=7)
		:L3CCMessage(wTI),
		//mHaveBearerCapability(false),
		mHaveProgress(false)
	{}
	
	int MTI() const { return CallProceeding; }
	void writeBody( L3Frame & dest, size_t &wp ) const;
	void parseBody( const L3Frame &src, size_t &wp );
	size_t l2BodyLength() const;

	void text(std::ostream&) const;
};




/**
	GSM 04.08 9.3.1.
	Even though uplink and downlink forms have different optional fields,
	we can use a single message for both sides.
*/
class L3Alerting : public L3CCMessage, public L3CCCommonIEs 
{
	private:

	Bool_z mHaveProgress;
	L3ProgressIndicator mProgress; 		///< Progress appears in downlink only.

	public:

	L3Alerting(unsigned wTI=7)
		:L3CCMessage(wTI)
		//,mHaveProgress(false)
	{}

	L3Alerting(unsigned wTI,const L3ProgressIndicator& wProgress)
		:L3CCMessage(wTI),
		mHaveProgress(true),mProgress(wProgress)
	{}

	int MTI() const { return Alerting; }
	void writeBody(L3Frame &dest, size_t &wp) const;
	void parseBody(const L3Frame& src, size_t &rp);
	size_t l2BodyLength() const;

	void text(std::ostream&) const;
};




/** GSM 04.08 9.3.5 */
class L3Connect : public L3CCMessage 
{
	private:

	bool mHaveProgress;
	L3ProgressIndicator mProgress; 		///< Progress appears in uplink only.

	public:

	L3Connect(unsigned wTI=7)
		:L3CCMessage(wTI),
		mHaveProgress(false)
	{}

	L3Connect(unsigned wTI, const L3ProgressIndicator& wProgress)
		:L3CCMessage(wTI),
		mHaveProgress(true),mProgress(wProgress)
	{}

	int MTI() const { return Connect; }
	void writeBody( L3Frame &dest, size_t &wp ) const;
	void parseBody(const L3Frame &src, size_t &wp);
	size_t l2BodyLength() const;
	void text(std::ostream&) const;
};




/** GSM 04.08 9.3.6 */
class L3ConnectAcknowledge : public L3CCMessage 
{
public:

	L3ConnectAcknowledge(unsigned wTI=7)
		:L3CCMessage(wTI)
	{}

	int MTI() const { return ConnectAcknowledge; }
	void writeBody( L3Frame &/*dest*/, size_t &/*wp*/ ) const {}
	void parseBody( const L3Frame &/*src*/, size_t &/*rp*/ ) {}
	size_t l2BodyLength() const { return 0; }

};


/** GSM 04.08 9.3.7 */
class L3Disconnect : public L3CCMessage {

private:

	L3CauseElement mCause;

public:

	/** Initialize with default cause of 0x10 "normal call clearing". */
	L3Disconnect(unsigned wTI=7, const CCCause wCause = L3Cause::Normal_Call_Clearing)
		:L3CCMessage(wTI),
		mCause(wCause)
	{}

	const L3CauseElement& cause() const { return mCause; }
	int MTI() const { return Disconnect; }
	void writeBody( L3Frame &dest, size_t &wp ) const;
	void parseBody( const L3Frame &src, size_t &rp );
	size_t l2BodyLength() const { return mCause.lengthLV(); }
	void text(std::ostream&) const;
};



/** GSM 04.08 9.3.2 */
class L3CallConfirmed : public L3CCMessage, public L3CCCapabilities {

private:

	// (pat) BearerCapability is sent by GSM phone
	//Bool_z mHaveBearerCapability;
	//L3BearerCapability mBearerCapability;
	Bool_z mHaveCause;
	L3CauseElement mCause;

	// (pat) SupportedCodecList is sent by UMTS phone
	//Bool_z mHaveSupportedCodecs;
	//L3SupportedCodecList mSupportedCodecs;

public:

	L3CallConfirmed(unsigned wTI=7)
		:L3CCMessage(wTI)
		//mHaveCause(false),
		//mHaveSupportedCodecs(false)
	{}

	int MTI() const { return CallConfirmed; }
	void parseBody(const L3Frame &src, size_t &rp);
	size_t l2BodyLength() const;

	void text(std::ostream& os) const;

};



/** GSM 04.08 9.3.24 */
class L3StartDTMF : public L3CCMessage {

	private:

	L3KeypadFacility mKey;

	public:

	L3StartDTMF(unsigned wTI=7)
		:L3CCMessage(wTI)
	{}

	const L3KeypadFacility& key() const { return mKey; }
	int MTI() const { return StartDTMF; }
	void parseBody(const L3Frame &src, size_t &rp) { mKey.parseTV(0x2c,src,rp); }
	size_t l2BodyLength() const { return mKey.lengthTV(); }

	void text(std::ostream& os) const;
};


/** GSM 04.08 9.3.25 */
class L3StartDTMFAcknowledge : public L3CCMessage {

	private:

	L3KeypadFacility mKey;

	public:

	L3StartDTMFAcknowledge(unsigned wTI, const L3KeypadFacility& wKey)
		:L3CCMessage(wTI),
		mKey(wKey)
	{}

	int MTI() const { return StartDTMFAcknowledge; }
	void writeBody(L3Frame &dest, size_t &wp) const { mKey.writeTV(0x2C,dest,wp); }
	size_t l2BodyLength() const { return mKey.lengthTV(); }

	void text(std::ostream& os) const;
};




/** GSM 04.08 9.3.26 */
class L3StartDTMFReject : public L3CCMessage {

	private:

	L3CauseElement mCause;

	public:

	/** Reject with default cause 0x3f, "service or option not available". */
	L3StartDTMFReject(unsigned wTI, const CCCause wCause)
		:L3CCMessage(wTI),
		mCause(wCause)
	{}

	int MTI() const { return StartDTMFReject; }
	void writeBody(L3Frame &src, size_t &rp) const { mCause.writeLV(src,rp); }
	size_t l2BodyLength() const { return mCause.lengthLV(); }

	void text(std::ostream& os) const;
};


/** GSM 04.08 9.3.29 */
class L3StopDTMF : public L3CCMessage {

	public:

	L3StopDTMF(unsigned wTI=7)
		:L3CCMessage(wTI)
	{}

	int MTI() const { return StopDTMF; }
	void parseBody(const L3Frame &/*src*/, size_t &/*rp*/) { }
	size_t l2BodyLength() const { return 0; };
};


/** GSM 04.08 9.3.30 */
class L3StopDTMFAcknowledge : public L3CCMessage {

	public:

	L3StopDTMFAcknowledge(unsigned wTI)
		:L3CCMessage(wTI)
	{}

	int MTI() const { return StopDTMFAcknowledge; }
	void writeBody(L3Frame&, size_t&) const { }
	size_t l2BodyLength() const { return 0; }
};





/** GSM 04.08 9.3.17 */
// pat 2-2014: GSM 4.08 5.5.1: The ProgressIndicator is used to start in-band tones and announcements
// if the value is 1-3 or 6-20.
class L3Progress : public L3CCMessage
{
	L3ProgressIndicator mProgress; 

	public:

	L3Progress(unsigned wTI, const L3ProgressIndicator& wProgress)
		:L3CCMessage(wTI),
		mProgress(wProgress)
	{}

	L3Progress(unsigned wTI)
		:L3CCMessage(wTI)
	{}

	int MTI() const { return Progress; }
	void writeBody( L3Frame &dest, size_t &wp ) const;
	void parseBody(const L3Frame &src, size_t &wp);
	size_t l2BodyLength() const;
	void text(std::ostream&) const;
};


/** GSM 04.08 9.3.10 */
class L3Hold : public L3CCMessage 
{
public:

	L3Hold(unsigned wTI=7)
		:L3CCMessage(wTI)
	{}

	int MTI() const { return Hold; }
	void writeBody( L3Frame &/*dest*/, size_t &/*wp*/ ) const {}
	void parseBody( const L3Frame &/*src*/, size_t &/*rp*/ ) {}
	size_t l2BodyLength() const { return 0; }

};



/** GSM 04.08 9.3.12 */
class L3HoldReject : public L3CCMessage {

	private:

	L3CauseElement mCause;

	public:

	/** Reject with default cause 0x3f, "service or option not available". */
	L3HoldReject(unsigned wTI, const CCCause wCause)
		:L3CCMessage(wTI),
		mCause(wCause)
	{}

	int MTI() const { return HoldReject; }
	void writeBody(L3Frame &src, size_t &rp) const { mCause.writeLV(src,rp); }
	size_t l2BodyLength() const { return mCause.lengthLV(); }

	void text(std::ostream& os) const;
};




/** GSM 04.08 9.3.6 */


};	// GSM


#endif
// vim: ts=4 sw=4
