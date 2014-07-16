/**@file Messages for call independent Supplementary Service Control, GSM 04.80 2.2. */

/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* Copyright 2011, 2014 Range Networks, Inc.

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


// (pat) About Supplementary Services [SS].
// References:
// 		24.010 describes it over-all.
//		24.080 has the Layer3 message descriptions.
//		24.390 has the high side SIP interface to IMS.
// Supplementary Services come in two types:
//		o some ordinary L3 messages are called SS messages:
//			hold, hold ack, hold reject, retrieve, retrieve ack and retrieve reject.
//		o Generic SS messages, which is everything else, defined in this file.
// Generic SS messages come in two types: pre-defined SS messages, or USupServ, which just transfers text strings
//		between the network and the MS.
// SS messages come in two flavors: MMI and Application Mode.  They can be network initiated or MS initiated.
// We support only MS initiated MMI messages.


#ifndef _GSML3SSMESSAGES_H_
#define _GSML3SSMESSAGES_H_

#include <string>
#include "GSMCommon.h"
#include "GSML3Message.h"
#include "GSML3CCElements.h"
// #include "GSML3SSElements.h"		This file is in the features/uSupServ branch.
// #include "GSML3SSComponents.h"		This file is in the features/uSupServ branch.



namespace GSM { 

/**
This a virtual class for L3  Messages for call independent Supplementary Service Control.
These messages are defined in GSM 04.80 2.2.
*/
class L3SupServMessage : public L3Message {

	protected:

	unsigned mTI;		///< short transaction ID, GSM 04.07 11.2.3.1.3; upper bit is originator flag

	public:

	/** GSM 04.80, Table 3.1 */
	enum MessageType {
		ReleaseComplete=0x2a,
		Facility=0x3a,
		Register=0x3b
	};

	L3SupServMessage(unsigned wTI=7)
		:L3Message(),mTI(wTI)
	{}

	size_t fullBodyLength() const { return l2BodyLength(); }

	/** Override the write method to include transaction identifiers in header. */
	void write(L3Frame& dest) const;

	L3PD PD() const { return L3NonCallSSPD; }

	unsigned TI() const { return mTI; }
	void setTI(unsigned wTI) { mTI = wTI; }

	void text(std::ostream&) const;
};

// The SS Register message version indicator is a TLV with one byte length.
// We use this to parse it.
struct L3OneByteProtocolElement : L3ProtocolElement {
	Bool_z mExtant;
	uint8_t mValue;
	L3OneByteProtocolElement() {}
	size_t lengthV() const { return 1; }
   	void writeV(L3Frame&dest, size_t&wp) const { dest.writeField(wp,mValue,8); }
	void parseV(const L3Frame&src, size_t&rp) { mValue = src.readField(rp,8); mExtant = true; }	// For parseTV.
	void parseV(const L3Frame&src, size_t&rp, size_t expectedLength);							// For parseLV or parseTLV
	void text(std::ostream&os) const { if (mExtant) os << mValue; }
};


/** GSM 04.08 10.5.4.1 */
// This is a TLV format Information Element.
class L3SupServFacilityIE : public L3OctetAlignedProtocolElement
{
	virtual void _define_vtable();
	public:
	void text(std::ostream&) const;
	L3SupServFacilityIE(std::string wData) : L3OctetAlignedProtocolElement(wData) {}
	L3SupServFacilityIE() {}
#if 0
	protected:
	unsigned char mComponents[255];	// (pat) Content described in 24.080 3.6.1.
	size_t mComponentSize;
	public:
	Bool_z mExtant;

	L3SupServFacilityIE(const char *wComponents, size_t wComponentSize)
		:L3ProtocolElement(),
		mComponentSize(wComponentSize)
	{
		devassert(wComponentSize<256);
		if (mComponentSize >=256) { mComponentSize = 255; }
		mExtant = true;
		memcpy(mComponents,wComponents,mComponentSize);
	}

	L3SupServFacilityIE():L3ProtocolElement(),mComponentSize(0) { }

	size_t componentSize() const { return mComponentSize; }
	const unsigned char* components() const { return mComponents; }

	size_t lengthV() const { return mComponentSize; }
   	void writeV(L3Frame&dest, size_t&wp) const;
	// This parse just cracks the components out.
	void parseV(const L3Frame&src, size_t&rp, size_t expectedLength);	// This form must be used for TLV format.
	void parseV(const L3Frame&, size_t&) { assert(0); }		// This form illegal for T/TV format.
#endif
};

// 24.008 10.5.4.24
class L3SupServVersionIndicator : public L3OctetAlignedProtocolElement {
	virtual void _define_vtable();
	public:
	L3SupServVersionIndicator(std::string wData) : L3OctetAlignedProtocolElement(wData) {}
	L3SupServVersionIndicator() {}
};


std::ostream& operator<<(std::ostream& os, const L3SupServVersionIndicator& msg);
std::ostream& operator<<(std::ostream& os, const L3SupServFacilityIE& msg);
std::ostream& operator<<(std::ostream& os, const GSM::L3SupServMessage& msg);
std::ostream& operator<<(std::ostream& os, const GSM::L3SupServMessage* msg);
std::ostream& operator<<(std::ostream& os, L3SupServMessage::MessageType mTranId);



/**
Parse a complete L3 call independent supplementary service control message into its object type.
@param source The L3 bits.
@return A pointer to a new message or NULL on failure.
*/
L3SupServMessage* parseL3SupServ(const L3Frame& source);

/**
A Factory function to return a L3SupServMessage of the specified mTranId.
Returns NULL if the MTI is not supported.
*/
L3SupServMessage* L3SupServFactory(L3SupServMessage::MessageType MTI);
	
/** Facility Message GSM 04.80/24.080 2.3. */
class L3SupServFacilityMessage : public L3SupServMessage {
	L3SupServFacilityIE mFacility;

	public:
	L3SupServFacilityMessage(unsigned wTranId, const L3SupServFacilityIE& wFacility)
		:L3SupServMessage(wTranId),
		mFacility(wFacility)
	{}
	L3SupServFacilityMessage() {}

	string getMapComponents() const { return mFacility.mData; }

	int MTI() const { return Facility; }
	void writeBody( L3Frame &dest, size_t &wp ) const;
	void parseBody( const L3Frame &src, size_t &rp );
	size_t l2BodyLength() const {return mFacility.lengthLV();}
	void text(std::ostream& os) const;

};

/** Register Message GSM 04.80/24.080 2.4. */
class L3SupServRegisterMessage : public L3SupServMessage {

	L3SupServFacilityIE mFacility;
	L3OneByteProtocolElement mVersionIndicator;
	
	public:
	L3SupServRegisterMessage(unsigned wTranId, const L3SupServFacilityIE& wFacility)
		:L3SupServMessage(wTranId),
		mFacility(wFacility)
	{ }
	L3SupServRegisterMessage() { }

	bool haveVersionIndicator() const {return mVersionIndicator.mExtant;}	
	uint8_t versionIndicator() const {assert(haveVersionIndicator()); return mVersionIndicator.mValue;}

	string getMapComponents() const { return mFacility.mData; }

	int MTI() const { return Register; }
	void writeBody( L3Frame &dest, size_t &wp ) const;
	void parseBody( const L3Frame &src, size_t &rp );
	size_t l2BodyLength() const;
	void text(std::ostream&) const;

};

/** Release Complete Message GSM 04.80/24.080 2.5. */
struct L3SupServReleaseCompleteMessage : public L3SupServMessage {

	L3SupServFacilityIE mFacility;

	L3CauseElement mCause;		// It is an L3 Cause as described in 24.008 10.5.4.11
	bool mHaveCause;

	L3SupServReleaseCompleteMessage() : mHaveCause(false) {}
	L3SupServReleaseCompleteMessage(unsigned wTranId) :
		L3SupServMessage(wTranId), mHaveCause(false) {}
	L3SupServReleaseCompleteMessage(unsigned wTranId, CCCause wCause) :
		L3SupServMessage(wTranId), mCause(wCause), mHaveCause(true) {}
	L3SupServReleaseCompleteMessage(unsigned wTranId, L3SupServFacilityIE &wFacility) :
		L3SupServMessage(wTranId), mFacility(wFacility), mHaveCause(false) {}

	bool haveFacility() const {return mFacility.mExtant; }

	// This is an outgoing message.  It does not need accessors.
	//bool haveCause() const {return mHaveCause;}
	//const L3Cause& cause() const {assert(mHaveCause); return mCause;}

	int MTI() const { return ReleaseComplete; }
	void writeBody( L3Frame &dest, size_t &wp ) const;
	void parseBody( const L3Frame &src, size_t &rp );
	size_t l2BodyLength() const;
	void text(std::ostream&) const;

};
L3SupServMessage * parseL3SS(const L3Frame& source);

}
#endif
