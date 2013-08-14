/*
* Copyright 2008, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/



#ifndef GSML3MESSAGE_H
#define GSML3MESSAGE_H

#include "GSMCommon.h"
#include "GSMTransfer.h"

namespace GSM {


/**@name L3 Processing Errors */
//@{

class L3ReadError : public GSMError {
	public:
	L3ReadError():GSMError() {}
};
#define L3_READ_ERROR {throw L3ReadError();}

class L3WriteError : public GSMError {
	public:
	L3WriteError():GSMError() {}
};
#define L3_WRITE_ERROR {throw L3WriteError();}

//@}



/**
	This is virtual base class for the messages of GSM's L3 signalling layer.
	It defines almost nothing, but is the origination of other classes.
*/
class L3Message {

	public: 

	virtual ~L3Message() {}

	/** Return the expected message body length in bytes, not including L3 header or rest octets. */
	virtual size_t l2BodyLength() const = 0;

	/**
		Body length not including header but including rest octets.
		In subclasses with no rest octets, this returns l2BodyLength.
		(pat) in BYTES!!!
	*/
	virtual size_t fullBodyLength() const =0;
	
	/** Return the expected message length in bytes, including L3 header, but not including rest octets.  */
	size_t L2Length() const { return l2BodyLength()+2; }

	/** Length ((pat) in BYTES!!) including header and rest octets. */
	size_t FullLength() const { return fullBodyLength()+2; }

	/** Return number of BITS needed to hold message and header.  */
	size_t bitsNeeded() const { return 8*FullLength(); }

	/**
	  The parse() method reads and decodes L3 message bits.
	  This method invokes parseBody, assuming that the L3 header
	  has already been read.
	*/
	virtual void parse(const L3Frame& source);

	/**
		Write message PD, MTI and data bits into a BitVector buffer.
		This method invokes writeBody.
		This method is overridden in the CC protocol.
	*/
	virtual void write(L3Frame& dest) const;

	/**
		Generate an L3Frame for this message.
		The caller is responsible for deleting the memory.
	*/
	L3Frame* frame(GSM::Primitive prim=DATA) const;

	/** Return the L3 protocol discriptor. */
	virtual GSM::L3PD PD() const =0;

	/** Return the messag type indicator (MTI). */
	virtual int MTI() const =0;


	protected:

	/**
		Write the L3 message body, a method defined in some subclasses.
		If not defined, this will assert at runtime.
	*/
	virtual void writeBody(L3Frame& dest, size_t &writePosition) const;

	/**
		The parseBody() method starts processing at the first byte following the
		message type octet in the L3 message, which the caller indicates with the
		readPosition argument.
		If not defined, this will assert at runtime.
	*/
	virtual void parseBody(const L3Frame& source, size_t &readPosition);


	public:

	/** Generate a human-readable representation of a message. */
	virtual void text(std::ostream& os) const;

};




/**@name Utility functions for message parsers. */
//@{
/**
	Skip an unused LV element while parsing.
	@return number of bits skipped.
*/
size_t skipLV(const L3Frame& source, size_t &readPosition);

/**
	Skip an unused TLV element while parsing.
	@return number of bits skipped.
*/
size_t skipTLV(unsigned IEI, const L3Frame& source, size_t &readPosition);

/**
	Skip an unused TV element while parsing.
	@return number of bits skipped.
*/
size_t skipTV(unsigned IEI, size_t numBits, const L3Frame& source, size_t &readPosition);
//@}




/**
	Parse a complete L3 message into its object type.
	Caller is responsible for deleting allocated memory.
	@param source The L3 bits.
	@return A pointer to a new message or NULL on failure.
*/
L3Message* parseL3(const L3Frame& source);


std::ostream& operator<<(std::ostream& os, const GSM::L3Message& msg);






/**
	Abstract class used for GSM L3 information elements.
	See GSM 04.07 11.2.1.1.4 for a description of TLV element formatting.
	To quote the spec, four categories of standard information elements are defined: 
		- information elements of format V or TV with value part consisting of 1/2 octet (type 1); 
		- information elements of format T with value part consisting of 0 octets (type 2); 
		- information elements of format V or TV with value part that has fixed length of at least one octet (type 3); 
		- information elements of format TLV or LV with value part consisting of zero, one or more octets (type 4); 

*/
class L3ProtocolElement {

	public:

	virtual ~L3ProtocolElement() {}


	/**
	  Return the length of the value part of the element in bytes.
	  This is the core length method, referenced by all other length methods.
	  Return zero for 1/2 octet fields (type 1 elements).
	*/
	virtual size_t lengthV() const =0;

	size_t lengthTV() const { return lengthV() + 1; }

	size_t lengthLV() const { return lengthV() + 1; }

	size_t lengthTLV() const { return lengthLV() + 1; }


	/**
	  The parseV method decodes L3 message bits from fixed-length value parts.
	  This is the core parse method for fixed-length parsable elements and
	  all other parse methods use it.
	  @param src The L3Frame to be parsed.
	  @param rp Bit index of read position (updated by read).
	*/
	virtual void parseV(const L3Frame& src, size_t &rp ) =0;

	/**
	  The parseV method decodes L3 message bits from variable-length value parts.
	  This is the core parse method for variable-length parsable elements and
	  all other parse methods use it.
	  @param src The L3Frame to be parsed.
	  @param rp Bit index of read position (updated by read).
	  @param expectedLength Length of available field, in bytes.
	*/
	virtual void parseV(const L3Frame& src, size_t &rp, size_t expectedLength) =0;


	/**
	  Parse LV format.
	  @param src The L3Frame to be parsed.
	  @param rp Bit index of read position (updated by read).
	*/
	void parseLV(const L3Frame& src, size_t &rp);

	/**
	  Parse TV format.
	  @param IEI The expected T part value.
	  @param src The L3Frame to be parsed.
	  @param rp Bit index of read position (updated by read).
	  @return true if the IEI matched and the element was actually read.
	*/
	bool parseTV(unsigned IEI, const L3Frame& src, size_t &rp);

	/**
	  Parse TLV format.
	  @param IEI The expected T part value.
	  @param src The L3Frame to be parsed.
	  @param rp read index (updated by read).
	  @return true if the IEI matched and the element was actually read.
	*/
	bool parseTLV(unsigned IEI, const L3Frame& src, size_t &rp);

	/**
		Write the V format.
		This is the core write method for writable elements and
		all other write methods use it.
		@param dest The target L3Frame.
		@param wp The write index (updated by write).
	*/
	virtual void writeV(L3Frame& dest, size_t &wp) const =0;

	/**
		Write LV format.
		@param dest The target L3Frame.
		@param wp The write index (updated by write).
	*/
	void writeLV(L3Frame& dest, size_t &wp) const;

	/**
		Write TV format.
		@param IEI The "information element identifier", ie, the T part.
		@param dest The target buffer.
		@param wp The buffer write pointer (updated by write).
	*/
	void writeTV(unsigned IEI, L3Frame& dest, size_t &wp) const;

	/**
		Write TLV format.
		@param IEI The "information element identifier", the T part.
		@param dest The target L3Frame.
		@param wp The write index (updated by write).
	*/
	void writeTLV(unsigned IEI, L3Frame& dest, size_t &wp) const;

	/** Generate a human-readable form of the element. */
	virtual void text(std::ostream& os) const
		{ os << "(no text())"; }


	protected:

	/**
		Skip over all unsupported extended octets in elements that use extension bits.
		@param src The L3Frame to skip along.
		@param rp The read pointer.
	*/
	void skipExtendedOctets( const L3Frame& src, size_t &rp );


};


std::ostream& operator<<(std::ostream& os, const L3ProtocolElement& elem);

// Pat added:  A Non-Aligned Message Element that is not an L3ProtocolElement because
// it is not in TLV format, and is not byte or half-byte aligned,
// but is rather just a stream of bits, often used in the Message RestOctets.
class GenericMessageElement {
	public:
	// We dont use these virtual functions except for text().
	// They are basically here as documentation.
	virtual size_t lengthBits() const = 0;
	virtual void writeBits(L3Frame& dest, size_t &wp) const = 0;
	virtual void text(std::ostream& os) const = 0;
};

std::ostream& operator<<(std::ostream& os, const GenericMessageElement& elem);


}; // GSM


#endif

// vim: ts=4 sw=4
