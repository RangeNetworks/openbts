/*
* Copyright 2011 Range Networks, Inc.
* All Rights Reserved.
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
#ifndef MSGBASE_H
#define MSGBASE_H 1
#include <iostream>
#include <stdlib.h>		// For size_t
#include "Defines.h"
#include "BitVector.h"
#include "ScalarTypes.h"
typedef const char *Type2Str(int);
const char *tohex(int);

// This is a package to help write downlink messages and message elements.
// (Note: A message element class is a helper class that writes part of a message,
// but is not a final class.)
// Also see Field<> and Field_z<> types.
// You write a single function writeCommon(), and this package uses it to instantiate the
// functions from MsgBase in your message class, namely:
//		void writeBody(BitVector &dest, size_t &wp)
//		void writeBody(BitVector &dest)
//		int length()
//		void text(std::ostream&os)
// To use:
// Step 1: For both message classes and message element classes:
// 		- Create a single function named: writeCommon(MsgCommon& dest)
//		which writes out the message using the functions defined in MsgCommon,
// 		or the macros: WRITE_ITEM, WRITE_FIELD, etc.
//		The primary output function is writeField()
// Step 2: Your message classes only (not message element classes)
//		need to define the functions above.  You can do this by inheriting from MsgBody,
//		or just writing these yourself.
//		For examples see class MsgBody or class RLCDownlinkMessage.

class MsgCommon
{
	public:
	size_t wp;
	MsgCommon() : wp(0) {}
	MsgCommon(size_t wwp) : wp(wwp) {}

	// Ignore this.  g++ emits the vtable and typeid in the translation unit where
	// the first virtual method (meeting certain qualifications) is defined, so we use this
	// dummy method to control that and put the vtables in MsgBase.cpp.
	// Do not change the order of these functions here or you will get inscrutable link errors.
	// What a foo bar language.
	virtual void _define_vtable() {}		// Must be the first function.

	// The primary function to write bitfields.
    virtual void writeField(
		uint64_t value, 		// The value to be output to the BitVector by write().
		unsigned len, 			// length in bits of value.
		const char *name=0, 	// name to be output by text() function; if not supplied,
								// then this var does not appear in the text()
		Type2Str cvt=0) = 0;	// optional function to translate value to a string in text().

	// This is used primarily to write variables of type Field or Field_z,
	// for which the width is defined in the type declaration.
    virtual void writeField(const ItemWithValueAndWidth&item, const char *name = 0) = 0;

	// Same as above, but an optional field whose presence is controlled by present.
    virtual void writeOptFieldLH(	// For fields whose presence is indicated by H, absence by L.
        uint64_t value, unsigned len, int present, const char*name = 0) = 0;
	// An alternative idiom to writeOptField01() is:
	// 		if (dst.write01(present)) writeField(value,len,name);
    virtual void writeOptField01(	// For fields whose presence is indicated by 1, absence by 0.
        uint64_t value, unsigned len, int present, const char*name = 0) = 0;
	virtual void writeH() {}
    virtual void writeL() {}
    virtual void write0() {}
    virtual void write1() {}
	virtual bool write01(bool present) {return present;}
	virtual void writeBitMap(bool*value,unsigned bitmaplen, const char*name) = 0;

	// getStream() returns the ostream for the text() function, or NULL for
	// length() or write() functions.  You can use it to make your function
	// do something special for text().
	virtual std::ostream* getStream() { return NULL; }
};

#define WRITE_ITEM(name) writeField(name,#name)
#define WRITE_OPT_ITEM01(name,opt) writeOptField01(name,name.getWidth(),opt,#name)

#define WRITE_FIELD(name,width) writeField(name,width,#name)
#define WRITE_OPT_FIELD01(name,width,opt) writeOptField01(name,width,opt,#name)
#define WRITE_OPT_FIELDLH(name,width,opt) writeOptFieldLH(name,width,opt,#name)
class MsgCommonWrite : public MsgCommon {
	BitVector& mResult;
	public:
	void _define_vtable();
	MsgCommonWrite(BitVector& wResult) : mResult(wResult)  {}
	MsgCommonWrite(BitVector& wResult, size_t &wp) : MsgCommon(wp), mResult(wResult)  {}
	void writeField(uint64_t value, unsigned len, const char * name=0, Type2Str =0);
    void writeField(const ItemWithValueAndWidth&item, const char *name = 0);

	void write0() { writeField(0,1); }
	void write1() { writeField(1,1); }
	bool write01(bool present) { writeField(present,1); return present; }
	void writeH();
	void writeL();

	// Write an Optional Field controlled by an initial L/H field.
	void writeOptFieldLH(uint64_t value, unsigned len, int present, const char * name);

	// Write an Optional Field controlled by an initial 0/1 field.
	void writeOptField01(uint64_t value, unsigned len, int present, const char * name);

	// Write a bitmap.
	void writeBitMap(bool*value,unsigned bitmaplen, const char*name);
};

class MsgCommonLength : public MsgCommon {
	public:
	void _define_vtable();
    void writeH() { wp++; }
    void writeL() { wp++; }
    void write0() { wp++; }
    void write1() { wp++; }
	bool write01(bool present) { wp++; return present;}
    void writeField(uint64_t, unsigned len, const char* =0, Type2Str =0) { wp+=len; }
    //virtual void writeField(const ItemWithValueAndWidth&item, const char *name= 0) { wp = item.getWidth(); }
    virtual void writeField(const ItemWithValueAndWidth&item, const char *) { wp = item.getWidth(); }
    void writeOptFieldLH(uint64_t, unsigned len, int present, const char*) {
		wp++; if (present) wp += len;
	}
    void writeOptField01(uint64_t, unsigned len, int present, const char*) {
		wp++; if (present) wp += len;
	}
	void writeBitMap(bool*,unsigned bitmaplen, const char*) { wp+=bitmaplen; }
};

class MsgCommonText : public MsgCommon {
    std::ostream& mos;
    public:
	void _define_vtable();
    MsgCommonText(std::ostream &os) : mos(os) { }

    void writeField(const ItemWithValueAndWidth&item, const char *name = 0) {
        if (name) { mos << " " << name << "=(" << item.getValue() << ")"; }
	}
    void writeField(uint64_t value, unsigned, const char*name=0, Type2Str cvt=0) {
        if (name) {
			mos << " " << name << "=";
			if (cvt) { mos << cvt(value); } else { mos << value; }
		}
    }

    void writeOptFieldLH(uint64_t value, unsigned, int present, const char*name = 0) {
        if (name && present) { mos << " " << name << "=(" << value << ")"; }
    }

    void writeOptField01(uint64_t value, unsigned, int present, const char*name = 0) {
        if (name && present) { mos << " " << name << "=(" << value << ")"; }
    }
	void writeBitMap(bool*value,unsigned bitmaplen, const char*name);
	std::ostream* getStream() { return &mos; }
};


// This is the base class for the message, that defines the functions that use MsgCommon.
/***
class MsgBody {
	public:
	virtual void writeCommon(MsgCommon &dest) const = 0;

	void writeOptional01(MsgCommon &dest, bool control) const {
		if (control) {
			dest.write1();
			writeCommon(dest);
		} else {
			dest.write0();
		}
	}

	void writeBody(BitVector &vdst, size_t &wwp) const {
		MsgCommonWrite dest(vdst,wwp);
		writeCommon(dest);
		wwp = dest.wp;
	}
	void writeBody(BitVector &vdst) const {
		MsgCommonWrite dest(vdst);
		writeCommon(dest);
	}
	int lengthBodyBits() const {
		MsgCommonLength dest;
		writeCommon(dest);
		return dest.wp;
	}
	void textBody(std::ostream&os) const {
		MsgCommonText dest(os);
		writeCommon(dest);
	}
};
***/

/***
#define INHERIT_MSG_BASE \
	void write(BitVector &dest, size_t &wp) { MsgBase::write(dest,wp); } \
	void write(BitVector &dest) { MsgBase::write(dest); } \
	int length() { return MsgBase::length(); } \
	void text(std::ostream&os) const { MsgBase::text(os); }
***/
	
#endif
