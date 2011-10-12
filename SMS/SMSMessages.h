/*
* Copyright 2008 Free Software Foundation, Inc.
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


/*
	As a simplification, we are supporting only the default 7-bit alphabet.
*/




#ifndef SMS_MESSAGE_H
#define SMS_MESSAGE_H

#include <stdio.h>
#include "SMSTransfer.h"
#include <GSML3Message.h>
#include <GSML3CCElements.h>
#include <GSML3MMElements.h>


namespace SMS {


class SMSReadError : public GSM::GSMError {
	public:
	SMSReadError():GSMError() {}
};
#define SMS_READ_ERROR {throw SMSReadError();}




/**@name SMS Transport Layer (TL) */
//@{

// FIXME -- All parsers for TL messages and elements should return a success/fail indication.

/**@name Elements for SMS Transport Layer (TL) */
//@{

/** A base class for elements of GSM 03.40 9.1.2 and 9.2.3 */
class TLElement {
	public:
	virtual ~TLElement() {}
	virtual size_t length() const =0;
	virtual void parse(const TLFrame&, size_t&) =0;
	virtual void write(TLFrame&, size_t&) const =0;
	virtual void text(std::ostream&) const {}
};

std::ostream& operator<<(std::ostream& os, const TLElement& msg);

/**
	GSM 03.40 9.1.2.5
	This is very similar to a Q.931-style BCD number.
	Especially since we don't support non-numeric addresses.
*/
class TLAddress : public TLElement {

private:

	GSM::TypeOfNumber mType;
	GSM::NumberingPlan mPlan;
	GSM::L3BCDDigits mDigits;

public:

	TLAddress():TLElement() {}

	TLAddress(GSM::TypeOfNumber wType, GSM::NumberingPlan wPlan, const char* wDigits)
		:TLElement(),
		mType(wType),mPlan(wPlan),mDigits(wDigits)
	{ }

	TLAddress(const char* wDigits)
		:TLElement(),
		mType(GSM::NationalNumber),mPlan(GSM::E164Plan),mDigits(wDigits)
	{ }

	const char *digits() const { return mDigits.digits(); }
	GSM::TypeOfNumber type() const { return mType; }
	GSM::NumberingPlan plan() const { return mPlan; }

	size_t length() const { return 2 + mDigits.lengthV(); }
	void parse(const TLFrame&, size_t&);
	void write(TLFrame&, size_t&) const;
	void text(std::ostream&) const;
};



/** GSM 03.40 9.2.3.12 */
class TLValidityPeriod : public TLElement {

private:

	unsigned mVPF;
	Timeval mExpiration;

public:


	/** Default validity period of one week, no format specified. */
	TLValidityPeriod(unsigned wVPF=0xFF)
		:TLElement(),
		mVPF(wVPF),
		mExpiration(7*24*60*60*1000)
	{ }

	void VPF(unsigned wVPF) { mVPF=wVPF; }

	size_t length() const;
	void parse(const TLFrame&, size_t&);
	void write(TLFrame&, size_t&) const;
	void text(std::ostream&) const;
};



class TLTimestamp : public TLElement {

	private:

	GSM::L3TimeZoneAndTime mTime;

	public:

	const Timeval& time() const { return mTime.time(); }
	void time(const Timeval& wTime) { mTime.time(wTime); }

	size_t length() const { return mTime.lengthV(); }
	void write(TLFrame& dest, size_t& wp) const { mTime.writeV((GSM::L3Frame&)(BitVector&)dest, wp); }
	void parse(const TLFrame& src, size_t& rp) { mTime.parseV((GSM::L3Frame&)(BitVector&)src, rp); }
};



/** GSM 03.40 9.2.3.24 */
class TLUserData : public TLElement {

	private:

	unsigned mDCS;		///< data coding scheme
	bool mUDHI;			///< header indicator
	unsigned mLength; ///< TP-User-Data-Length, see GSM 03.40 Fig. 9.2.3.24(a),
	                  ///< GSM 03.40 Fig. 9.2.3.24(b) and GSM 03.40 9.2.3.16.
	BitVector mRawData;  ///< raw packed data

	public:

	/** Initialize the DCS with a non-valid value. */
	TLUserData(unsigned wDCS=0x100, bool wUDHI=false)
		:TLElement(),
		mDCS(wDCS),
		mUDHI(wUDHI),
		mLength(0)
	{
	}

	/** Initialize from a raw encoded data. */
	TLUserData(unsigned wDCS, const BitVector wRawData, unsigned wLength,
	           bool wUDHI=false)
		:TLElement(),
		mDCS(wDCS),
		mUDHI(wUDHI),
		mLength(wLength)
	{
		mRawData.clone(wRawData);
	}

	/** Initialize from a simple C string. */
	TLUserData(const char* text, GSM::GSMAlphabet alphabet=GSM::ALPHABET_7BIT, bool wUDHI=false)
		:TLElement(),
		mDCS(0),
		mUDHI(wUDHI),
		mLength(0)
	{
		switch(alphabet) {
		case GSM::ALPHABET_7BIT:
			encode7bit(text);
			break;
		case GSM::ALPHABET_8BIT:
		case GSM::ALPHABET_UCS2:
		default:
			//LOG(WARNING) << "Unsupported alphabet: " << alphabet;
			break;
		}
	}

	void DCS(unsigned wDCS) { mDCS=wDCS; }
	unsigned DCS() const { return mDCS; }
	void UDHI(unsigned wUDHI) { mUDHI=wUDHI; }
	unsigned UDHI() const { return mUDHI; }
	/** Encode text into this element, using 7-bit alphabet */
	void encode7bit(const char *text);
	/** Decode text from this element, using 7-bit alphabet */
	std::string decode() const;

	/** This length includes a byte for the length field. */
	size_t length() const;

	/** Parse, including the initial length byte. */
	void parse(const TLFrame&, size_t&);

	void write(TLFrame&, size_t&) const;
	void text(std::ostream&) const;
};



//@} // SMS TL Elements

/**@name Messages for SMS Transport Layer (TL) */
//@{

/** GSM 03.40 9.2 */
class TLMessage {

	protected:

	/**@name Standard TLheader bits from GSM 03.40 9.2.3.
		- 0	MTI (9.2.3.1)
		- 1	MTI
		- 2	MMS (9.2.3.2), RD (9.2.3.25)
		- 3	VPF (9.2.3.3)
		- 4	VPF
		- 5	SRI (9.2.3.4), SRR (9.2.3.5), SRQ (9.2.3.26)
		- 6	UDHI (9.2.3.23)
		- 7	RP (9.2.3.17)
	*/
	//@{
	bool mMMS;			///< more messages to send
	bool mRD;			///< reject duplicates
	unsigned mVPF;		///< validity period format
	bool mSRR;			///< status report request
	bool mSRI;			///< status report indication
	bool mSRQ;			///< status report qualifier
	bool mUDHI;			///< user-data header-indicator
	bool mRP;			///< reply path
	//@}

	public:

	/** Maximum size of user data field. */
	static const unsigned maxData = 160;

	/** GSM 03.40 9.2.3.1 */
	enum MessageType {
		DELIVER = 0x0,        // SC -> MS
		DELIVER_REPORT = 0x0, // MS -> SC
		STATUS_REPORT = 0x2,  // SC -> MS
		COMMAND = 0x02,       // MS -> SC
		SUBMIT = 0x1,         // MS -> SC
		SUBMIT_REPORT = 0x1   // SC -> MS
	};

	TLMessage()
		:mMMS(false),mSRI(false),mRP(false)
	{}

	virtual ~TLMessage(){}

	virtual int MTI() const=0;

	/** The bodtLength is everything beyond the header byte. */
	virtual size_t l2BodyLength() const = 0;

	virtual size_t length() const { return 1+l2BodyLength(); }

	size_t bitsNeeded() const { return length()*8; }

	virtual void parse( const TLFrame& frame );
	virtual void parseBody( const TLFrame& frame, size_t &rp) =0;

	virtual void write( TLFrame& frame ) const;
	virtual void writeBody( TLFrame& frame, size_t &rp) const =0;

	virtual void text(std::ostream& os) const
		{ os << MTI(); }

	protected:

	/**@name Readers and writers for standard header bits. */
	//@{
	// Note that offset is reversed, i'=7-i.
	void writeMTI(TLFrame& fm) const { fm.fillField(6,MTI(),2); }
	void writeMMS(TLFrame& fm) const { fm[5]=mMMS; }
	void parseMMS(const TLFrame& fm) { mMMS=fm[5]; }
	void writeRD(TLFrame& fm) const { fm[5]=mRD; }
	void parseRD(const TLFrame& fm) { mRD=fm[5]; }
	void writeVPF(TLFrame& fm) const { fm.fillField(3,mVPF,2); }
	void parseVPF(const TLFrame& fm) { mVPF = fm.peekField(3,2); }
	void writeSRR(TLFrame& fm) const { fm[2]=mSRR; }
	void parseSRR(const TLFrame& fm) { mSRR=fm[2]; }
	void writeSRI(TLFrame& fm) const { fm[2]=mSRI; }
	void parseSRI(const TLFrame& fm) { mSRI=fm[2]; }
	void writeSRQ(TLFrame& fm) const { fm[2]=mSRQ; }
	void parseSRQ(const TLFrame& fm) { mSRQ=fm[2]; }
	void writeUDHI(TLFrame& fm, bool udhi) const { fm[1]=udhi; }
	bool parseUDHI(const TLFrame& fm) { return fm[1]; }
	void writeRP(TLFrame& fm) const { fm[0]=mRP; }
	void parseRP(const TLFrame& fm) { mRP=fm[0]; }
	void writeUnused(TLFrame& fm) const { fm.fill(0,3,2); } ///< Fill unused bits with 0s
	//@}
};

std::ostream& operator<<(std::ostream& os, TLMessage::MessageType val);
std::ostream& operator<<(std::ostream& os, const TLMessage& msg);



/** GSM 03.40 9.2.2.2, uplink */
class TLSubmit : public TLMessage {

	private:

	unsigned mMR;			///< message reference
	TLAddress mDA;			///< destination address
	unsigned mPI;			///< protocol identifier
	unsigned mDCS;			///< data coding scheme
	TLValidityPeriod mVP;	///< validity period
	TLUserData mUD;			///< user data

	public:

	int MTI() const { return SUBMIT; }

	const TLAddress& DA() const { return mDA; }
	const TLUserData& UD() const { return mUD; }

	size_t l2BodyLength() const;
	void writeBody(TLFrame&, size_t&) const { assert(0); }
	void parseBody(const TLFrame&, size_t&); 
	virtual void text(std::ostream&) const;
};



/** GMS 03.40 9.2.2.2a, downlink */
class TLSubmitReport : public TLMessage
{

	private: 
	// We are leaving out the optional fields.
	unsigned mFC;							///< failure cause
	unsigned mPI;							///< parameter indicator
	TLTimestamp mSCTS;			///< service center timestamp

	public:

	size_t l2BodyLength() const { return 1 + 1 + 7; }
	void writeBody(TLFrame& frame, size_t& wp ) const;
	void parseBody(const TLFrame&, size_t&) { assert(0); }
	virtual void text( std::ostream& os ) const;
};


/** GSM 03.40 9.2.2.1, downlink */
class TLDeliver : public TLMessage {

	private:

	TLAddress mOA;			///< origination address, GSM 03.40 9.3.2.7
	unsigned mPID;			///< TL-PID, GSM 03.40 9.2.3.9
	// DCS is taken from mUD.
	TLTimestamp mSCTS;		///< service center timestamp, GSM 03.40 9.2.3.11
	TLUserData mUD;			///< user data

	public:

	TLDeliver(const TLAddress& wOA, const TLUserData& wUD, unsigned wPID=0)
		:TLMessage(),
		mOA(wOA),mPID(wPID),mUD(wUD)
	{ }

	TLDeliver(const TLUserData& wUD)
		:TLMessage(),
		mUD(wUD)
	{}
	
	int MTI() const { return DELIVER; }

	size_t l2BodyLength() const;
	void writeBody( TLFrame& frame, size_t& wp ) const;
	void parseBody(const TLFrame&, size_t&) { assert(0); }
	virtual void text( std::ostream& os ) const;
};


TLMessage * parseTL( const TLFrame& frame );


//@} // SMS TL Messages


//@} // SMS TL



/**@name Elements and Messages for SMS RP (RL Layer) */
//@{

/**@name Elements for SMS RP (RL Layer) */
//@{

/** A common class for RP addresses, GSM 04.11 8.2.5 */
class RPAddress : public GSM::L3CalledPartyBCDNumber {

	public:

	RPAddress():L3CalledPartyBCDNumber() {}

	RPAddress(const char* wDigits)
		:L3CalledPartyBCDNumber(wDigits)
	{}

	RPAddress(const GSM::L3CallingPartyBCDNumber& other)
		:L3CalledPartyBCDNumber(other)
	{}

};

/** GSM 04.11 8.2.5.3 */
class RPUserData : public GSM::L3ProtocolElement {

	private:

	// The BitVector is a placeholder for a higher-layer object.
	TLFrame mTPDU;

	public:

	RPUserData()
		:L3ProtocolElement(),
		mTPDU()
	{}

	RPUserData(const TLFrame& wTPDU)
		:L3ProtocolElement(),
		mTPDU(wTPDU)
	{}

	RPUserData(const TLMessage& TLM)
		:L3ProtocolElement()
	{
		TLM.write(mTPDU);
	}


	const TLFrame& TPDU() const { return mTPDU; }

	size_t lengthV() const
	{
		size_t len = mTPDU.size()/8;
		if (mTPDU.size()%8) len++;
		return len;
	}

	void writeV(GSM::L3Frame& dest, size_t &wp) const;
	void parseV(const GSM::L3Frame& src, size_t &rp) { assert(0); }
	void parseV(const GSM::L3Frame& src, size_t &rp, size_t expectedLength);

	void text(std::ostream& os) const { mTPDU.hex(os); }
};


/**
	GSM 04.11 8.2.5.4.
	We ignore the diagnostics field.
*/
class RPCause : public GSM::L3ProtocolElement {


	private:

	unsigned mValue;

	public:

	RPCause(unsigned wValue)
		:L3ProtocolElement(),
		mValue(wValue)
	{}

	size_t lengthV() const { return 1; }


	void writeV(GSM::L3Frame& dest, size_t &wp) const
		{ dest.writeField(wp,mValue,8); }

	void parseV(const GSM::L3Frame& src, size_t &rp)
		{ mValue = src.readField(rp,8); }

	void parseV(const GSM::L3Frame& src, size_t &rp, size_t expectedLength)
		{ mValue = src.peekField(rp,8); rp += 8*expectedLength; }

	void text(std::ostream& os) const
		{ os << std::hex << "0x" << mValue << std::dec; }
};

//@}





/**@name Messages for SMS RP (RL Layer) */
//@{

/** The L4 RP message, GSM 04.11 7.3, 8.2. */
class RPMessage {

	public:

	unsigned mReference;

	/** Table 8.3 GSM 04.11, add 1 for downlink */
	enum MessageType {
		Data=0x0,
		Ack=0x2,
		Error=0x4,
		SMMA=0x6
	};

	RPMessage(unsigned wReference=0)
		:mReference(wReference)
	{}

	virtual ~RPMessage(){}

	unsigned reference() const { return mReference; }

	virtual int MTI() const=0;

	virtual size_t l2BodyLength() const = 0;

	size_t length() const { return l2BodyLength()+2; }

	size_t bitsNeeded() const { return length()*8; }

	void parse( const RLFrame& frame );

	virtual void parseBody(const RLFrame& frame, size_t &rp) =0; 

	/** For the network side, the write method adds 1 to the MTI. */
	void write( RLFrame& frame ) const;

	virtual void writeBody(RLFrame& frame, size_t &wp) const  =0;

	virtual void text(std::ostream& os) const;

};

std::ostream& operator<<(std::ostream&, RPMessage::MessageType);
std::ostream& operator<<(std::ostream& os, const RPMessage& msg);




/** GSM 04.11 7.3.1 */
class RPData : public RPMessage {

	private:

	RPAddress mOriginator;			///< originating SMSC
	RPAddress mDestination;			///< destination SMSC
	RPUserData mUserData;			///< payload

	public:

	RPData():RPMessage() {}

	/** This is a constructor for the downlink version of the message. */
	RPData(unsigned ref, const RPAddress& wOriginator, const TLMessage& TLM)
		:RPMessage(ref),
		mOriginator(wOriginator),mUserData(TLM)
	{}

	const TLFrame& TPDU() const { return mUserData.TPDU(); }

	int MTI() const { return Data; }
	void parseBody( const RLFrame& frame, size_t &rp); 		
	void writeBody( RLFrame & frame, size_t &wp ) const;

	size_t l2BodyLength() const
		{ return mOriginator.lengthLV() + mDestination.lengthLV() + mUserData.lengthLV(); }

	void text(std::ostream&) const;
};


/** GSM 04.11 7.3.2 */
class RPSMMA : public RPMessage {

	public:

	int MTI() const { return SMMA; }
	//void parseBody( const RLFrame& frame, size_t &rp); 		
	//void writeBody( RLFrame & frame, size_t &wp ) const;

	size_t l2BodyLength() const { return 0; }
};


/** GSM 04.11 7.3.3 */
class RPAck : public RPMessage {

	// We're ignoring the user data field.

	public:

	RPAck(unsigned mReference)
		:RPMessage(mReference)
	{}
	
	int MTI() const { return Ack; }

	void writeBody(RLFrame& frame, size_t &wp) const {}
	void parseBody( const RLFrame& frame, size_t &rp) {}

	size_t l2BodyLength() const { return 0; }
};


/** GSM 04.11 7.3.4 */
class RPError : public RPMessage {

	private:

	// Ignore user data for now.
	RPCause mCause;

	public:
	
	RPError(const RPCause& wCause, unsigned mReference)
		:RPMessage(mReference),
		mCause(wCause)
	{}

	int MTI() const { return Error; }

	void writeBody(RLFrame& frame, size_t &wp) const;
	void parseBody(const RLFrame& frame, size_t &rp);

	size_t l2BodyLength() const { return mCause.lengthLV(); }

	void text(std::ostream&) const;
};


//@}

//@} // SMS RL





/**@name Elements for SMS CP (CM Layer) */
//@{

/**@name Elements for SMS CP (CM Layer) */
//@{

/** GSM 04.11 8.1.4.2 */
class CPCause : public GSM::L3ProtocolElement {

	private:

	unsigned mValue;

	public:

	CPCause(unsigned wValue)
		:L3ProtocolElement(),
		mValue(wValue)
	{}

	size_t lengthV() const { return 1; }

	void writeV(GSM::L3Frame& dest, size_t &wp) const
		{ dest.writeField(wp,mValue,8); }

	void parseV(const GSM::L3Frame& src, size_t &rp, size_t) { assert(0); }

	void parseV(const GSM::L3Frame& src, size_t &rp)
		{ mValue = src.readField(rp,8); }

	void text(std::ostream& os) const
		{ os << std::hex << "0x" << mValue << std::dec; }
};


/** GSM 04.11 8.1.4.1 */
class CPUserData : public GSM::L3ProtocolElement {

	private:

	RLFrame mRPDU;		///< relay-layer protocol data unit

	public:

	CPUserData()
		:L3ProtocolElement()
	{}

	CPUserData(const RPMessage& RPM)
		:L3ProtocolElement(),
		mRPDU(RPM.bitsNeeded())
	{
		RPM.write(mRPDU);
	}

	const RLFrame& RPDU() const { return mRPDU; }

	size_t lengthV() const { return mRPDU.size()/8; }
	void writeV(GSM::L3Frame& dest, size_t &wp) const;
	void parseV(const GSM::L3Frame& src, size_t &rp, size_t expectedLength);
	void parseV(const GSM::L3Frame& src, size_t &rp) { assert(0); }
	void text(std::ostream& os) const { mRPDU.hex(os); }
};

//@} // CP Elements



/**@name Messages for SMS CP (SMS CM layer) */
//@{

/**
	A virtual class for SMS CM-layer messages.
	See GSM 04.11 7.
	This is probably nearly the same as GSM::L3CCMessage,
	but with different message types.
*/

class CPMessage : public GSM::L3Message 
{
	private:

	/**@name Header information ref. Figure 8. GSM 04.11 */
	//@{
	unsigned mTI; 	///< short transaction ID, GSM 04.07 11.2.3.1.3
	//@}


	public:

	/** Message type defined in GSM 04.11 8.1.3 Table 8.1 */
	enum MessageType {
		DATA=0x01,
		ACK=0x04,
		ERROR=0x10
	};


	CPMessage(unsigned wTI)
		:L3Message(),
		mTI(wTI)
	{}

	size_t fullBodyLength() const { return l2BodyLength(); }

	/** Override the write method to include transaction identifiers in header. */
	void write(GSM::L3Frame& dest) const;

	GSM::L3PD PD() const { return GSM::L3SMSPD; }

	unsigned TI() const { return mTI; }
	void TI(unsigned wTI){ mTI=wTI; }
	
	void text(std::ostream&) const;

};


std::ostream& operator<<(std::ostream& os, CPMessage::MessageType MTI);

/**
	Parse a complete SMS L3 (CM) message.
	This is the top-level SMS parser, called along side other L3 parsers.
*/
CPMessage * parseSMS( const GSM::L3Frame& frame );

/**
   Parse msgtext from a hex string to RPData struct.
   @param hexstring RPData encoded into hex-string.
   @return Pointer to parsed RPData or NULL on error.
*/
RPData *hex2rpdata(const char *hexstring);

/**
	Parse a TPDU.
	Currently only SMS-SUBMIT is supported.
	@param TPDU The TPDU.
	@return Pointer to parsed TLMessage or NULL on error.
*/
TLMessage *parseTPDU(const TLFrame& TPDU);

/** A factory method for SMS L3 (CM) messages. */
CPMessage * CPFactory( CPMessage::MessageType MTI );



/** GSM 04.11 7.2.1 */
class CPAck : public CPMessage 
{	
	public:

	CPAck( unsigned wTI=7)
		:CPMessage(wTI)
 	{ }

	int MTI() const { return ACK; }

	size_t l2BodyLength() const { return 0; }
	void parseBody(const GSM::L3Frame& dest, size_t &rp) {};
	void writeBody(GSM::L3Frame& dest, size_t &wp) const {};

	void text(std::ostream& os) const { CPMessage::text(os); }
};



/** GSM 04.11 7.2.3 */
class CPError : public CPMessage 
{ 
	private:

	CPCause mCause;

	public:

	CPError(unsigned wTI=7)
		:CPMessage(wTI),
		mCause(0x7f)
 	{ }

	CPError(unsigned wTI, const CPCause& wCause)
		:CPMessage(wTI),
		mCause(wCause)
 	{ }

	int MTI() const { return ERROR; }
	size_t l2BodyLength() const { return mCause.lengthV(); }
	void writeBody(GSM::L3Frame& dest, size_t &wp) const;
	void parseBody(const GSM::L3Frame&, size_t&) { assert(0); }
};



/** GSM 04.11 7.2.1 */
class CPData : public CPMessage
{
	private:

	CPUserData mData;

	public:	

	CPData(unsigned wTI=7)
		:CPMessage(wTI)
 	{ }

	CPData(unsigned wTI, const RPMessage& RPM)
		:CPMessage(wTI),
		mData(RPM)
 	{ }

	const CPUserData& data() const { return mData; }
	const RLFrame& RPDU() const { return mData.RPDU(); }

	int MTI() const { return DATA; }
	size_t l2BodyLength() const { return mData.lengthLV(); }
	void parseBody(const GSM::L3Frame& dest, size_t &rp);
	void writeBody(GSM::L3Frame& dest, size_t &wp) const;
	void text(std::ostream&) const;
};


//@} // CP messages

//@} // SMS CP



}; // namespace SMS

#endif

// vim: ts=4 sw=4
