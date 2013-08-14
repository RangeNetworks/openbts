/*
* Copyright 2010 Kestrel Signal Processing, Inc.
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



#ifndef GSML3SMSCBMESSAGES_H
#define GSML3SMSCBMESSAGES_H

#include "GSML3Message.h"
#include <iostream>

namespace GSM {

/* Elements of SMSCB messages, from GSM 03.41 9.3. */

/** GSM 03.41 9.3.2.1 */
class L3SMSCBSerialNumber : public L3ProtocolElement {

	private:
	unsigned mGS;			///< geographic scope
	unsigned mMessageCode;		///< code classifying message content
	unsigned mUpdateNumber;		///< so MS knows to reload this message

	public:

	L3SMSCBSerialNumber(unsigned wGS, unsigned wMessageCode, unsigned wUpdateNumber):
		mGS(wGS),
		mMessageCode(wMessageCode), mUpdateNumber(wUpdateNumber)
	{ }

	void parseV(const L3Frame&, size_t&, size_t) { assert(0); }
	void parseV(const L3Frame&, size_t&) { assert(0); }

	void writeV(L3Frame&, size_t&) const;
	size_t lengthV() const { return 2; }
	void text(std::ostream& os) const;

};

/** GSM 03.41 9.3.2.2 */
class L3SMSCBMessageIdentifier : public L3ProtocolElement {

	private:
	unsigned mValue;

	public:

	L3SMSCBMessageIdentifier(unsigned wValue):
		mValue(wValue)
	{ }

	void parseV(const L3Frame&, size_t&, size_t) { assert(0); }
	void parseV(const L3Frame&, size_t&) { assert(0); }

	void writeV(L3Frame&, size_t&) const;
	size_t lengthV() const { return 2; }
	void text(std::ostream& os) const;

};

/** GSM 03.41 9.3.2.3 */
class L3SMSCBDataCodingScheme : public L3ProtocolElement {

	private:
	unsigned mValue;

	public:

	L3SMSCBDataCodingScheme(unsigned wValue):
		mValue(wValue)
	{ }

	void parseV(const L3Frame&, size_t&, size_t) { assert(0); }
	void parseV(const L3Frame&, size_t&) { assert(0); }

	void writeV(L3Frame&, size_t&) const;
	size_t lengthV() const { return 1; }
	void text(std::ostream& os) const;

};


/** GSM 03.41 9.3.2.4 */
class L3SMSCBPageParameter : public L3ProtocolElement {

	private:
	unsigned mNumber;
	unsigned mTotal;

	public:

	L3SMSCBPageParameter(unsigned wNumber, unsigned wTotal):
		mNumber(wNumber),mTotal(wTotal)
	{ }

	void parseV(const L3Frame&, size_t&, size_t) { assert(0); }
	void parseV(const L3Frame&, size_t&) { assert(0); }

	void writeV(L3Frame&, size_t&) const;
	size_t lengthV() const { return 1; }
	void text(std::ostream& os) const;

};



/** GSM 03.41 9.3.2.5 */
class L3SMSCBContent : public L3ProtocolElement {

	private:
	char mData[82];		///< raw data

	public:

	L3SMSCBContent(const char *wData)
		{ bcopy(wData,mData,82); }

	void parseV(const L3Frame&, size_t&, size_t) { assert(0); }
	void parseV(const L3Frame&, size_t&) { assert(0); }

	void writeV(L3Frame&, size_t&) const;
	size_t lengthV() const { return 82; }
	void text(std::ostream& os) const;
};




/**
	L3 definition of the SMSCB message.
	This message group does not follow the normal structure of
	most Um L3 messages and is not an L3Message subclass.
	See GSM 03.41 9.3.1.
*/
class L3SMSCBMessage {

	private:
	L3SMSCBSerialNumber mSerialNumber;
	L3SMSCBMessageIdentifier mMessageIdentifier;
	L3SMSCBDataCodingScheme mDataCodingScheme;
	L3SMSCBPageParameter mPageParameter;
	L3SMSCBContent mContent;

	public:
	L3SMSCBMessage(
		L3SMSCBSerialNumber wSerialNumber,
		L3SMSCBMessageIdentifier wMessageIdentifier,
		L3SMSCBDataCodingScheme wDataCodingScheme,
		L3SMSCBPageParameter wPageParameter,
		L3SMSCBContent wContent
	):
		mSerialNumber(wSerialNumber),
		mMessageIdentifier(wMessageIdentifier),
		mDataCodingScheme(wDataCodingScheme),
		mPageParameter(wPageParameter),
		mContent(wContent)
	{ }

	void write(L3Frame&) const;
	void text(std::ostream&) const;
};

std::ostream& operator<<(std::ostream&, const L3SMSCBMessage&);

}

#endif

// vim: ts=4 sw=4
