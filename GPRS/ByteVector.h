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

#ifndef _BYTEVECTOR_H_
#define _BYTEVECTOR_H_
#include <stdint.h>
#include <arpa/inet.h>
#include "MemoryLeak.h"
#include "BitVector.h"
#include "ScalarTypes.h"
#include "Logger.h"
// Originally based on BitVector, based on Vector

// ByteVector is like a Vector but for objects of type... guess what?
// ByteVector also has an efficient append facility.
// A ByteVector consists of packed memory that is byte-aligned on the left side
// and bit-aligned on the right side.  Both the left and right side can be moved
// back and forth within the constraints of the originally allocated memory.
// See: trimLeft, trimRight, growLeft, and the many append functions.
// The basic strategy is that ByteVectors are always allocated initially
// such that size() is the full allocated size, so if you want to use the append
// feature you must call setAppendP() (or conceivably trimRight) to set the location
// where you want to start appending.
// Exceeding the edges of the allocated memory area throws ByteVectorError
// There are two classes defined here:
// o ByteVector points to a memory area that it manages.
// All segments derived from a ByteVector share the same memory using refcnts.
// When the last segment is deleted, the memory is freed.
// o ByteVectorTemp is identical but does not 'own' the memory, rather it points into
// an area of memory that it does not manage.  It allows you to use the rather extensive
// set of ByteVector manipulation functions on some other memory, or derive a segment
// using segmentTemp when you know for sure that the derived segment is temporary and
// will not outlive the original ByteVector.
// It is unwise to expand the left or right side of a ByteVectorTemp because there is
// no protection for exceeding the bounds of the memory area, however those functions
// are not currently over-ridden in ByteVectorTemp to remove them, but they should be eliminated.
// ByteVector is the base class and can refer to either ByteVectors that own memory,
// or ByteVectorTemps that do not.

// I started inheriting from Vector but we could only reuse a few lines of code
// from there so it is not worth the trouble.  It would be better to push
// the appending ability down into Vector.  But appending creates a different
// concept of size() than Vector which would be a major change.
// To avoid confusion with Vector type functions resize() is not defined in ByteVector.

#define NEW_SEGMENT_SEMANTICS 1
#define BYTEVECTOR_REFCNT 1		// ByteVectors now use reference counting

#define BVASSERT(expr) {if (!(expr)) throw ByteVectorError();}
class ByteVectorError {};

typedef uint8_t ByteType;
void sethtonl(ByteType *cp,unsigned value);
void sethtons(ByteType *cp,unsigned value);
uint16_t getntohs(ByteType *cp);
uint32_t getntohl(ByteType *cp);

class ItemWithSize {
	virtual size_t sizeBits() const =0;
	virtual size_t sizeBytes() const =0;
};

class ByteVectorTemp;
class Dorky {};	// Used to force use of a particular constructor.

class ByteVector //: public Vector<ByteType>
	: public ItemWithSize
{
	ByteType* mData;		///< allocated data block, if any
	ByteType* mStart;		///< start of useful data, always >=mData and <=mAllocEnd
	unsigned mSizeBits;		///< size of useful data in bits.
	ByteType *mAllocEnd;	///< end of allocated data + 1.

	//unsigned mBitInd;
	unsigned bitind() { return mSizeBits % 8; }

#if BYTEVECTOR_REFCNT
	// The first mDataOffset bytes of mData is a short reference count of the number
	// of ByteVectors pointing at it.
	static const int mDataOffset = sizeof(short);
	int setRefCnt(int val) { return ((short*)mData)[0] = val; }
	int decRefCnt() { return setRefCnt(((short*)mData)[0] - 1); }
	void incRefCnt() { setRefCnt(((short*)mData)[0] + 1); }
#endif


	void init(size_t newSize);	/** set size and allocated size to that specified */
	void initstr(const char *wdata, unsigned wlen) { init(wlen); setAppendP(0); append(wdata,wlen); }
	void initstr(const char *wdata) { initstr(wdata,strlen(wdata)); }
	void dup(const ByteVector& other);

	// Constructor: A ByteVector that does not own any memory, but is just a segment
	// of some other memory.  This constructor is private.
	// The public way to do this is to use segmentTemp or create a ByteVectorTemp.
	protected:
	ByteVector(Dorky,ByteType*wstart,ByteType*wend)
		: mData(0), mStart(wstart), mSizeBits(8*(wend-wstart)), mAllocEnd(wend) {}
	//: mData(0), mStart(wstart), mEnd(wend), mAllocEnd(wend), mBitInd(0) {}

	public:
	void clear();	// Release the memory used by this ByteVector.
	// clone semantics are weird: copies data from other to self.
	void clone(const ByteVector& other); /** Copy data from another vector. */
#if BYTEVECTOR_REFCNT
	int getRefCnt() { return mData ? ((short*)mData)[0] : 0; }
#endif

	const ByteType* begin() const { return mStart; }
	ByteType*begin() { return mStart; }

	// This is the allocSize from mStart, not from mData.
	size_t allocSize() const { return mAllocEnd - mStart; }
	//size_t sizeBytes() const { return mEnd - mStart + !!mBitInd; }	// size in bytes
	size_t sizeBytes() const { return (mSizeBits+7)/8; }	// size in bytes
	size_t size() const { return sizeBytes(); }	// size in bytes
	//size_t sizeBits() const { return size() * 8 + mBitInd; }	// size in bits
	size_t sizeBits() const { return mSizeBits; }	// size in bits
	size_t sizeRemaining() const { // Remaining full bytes for appends
		return (mAllocEnd - mStart) - sizeBytes();
		//int result = mAllocEnd - mEnd - !!mBitInd;
		//return result >= 0 ? (size_t) result : 0;
	}

	void resetSize() { mSizeBits = 8*allocSize(); }

	// Set the Write Position for appends.  This is equivalent to setting size().
	void setAppendP(size_t bytep, unsigned bitp=0) {
		BVASSERT(bytep + !!bitp <= allocSize());
		mSizeBits = bytep*8 + bitp;
		//mEnd=mStart+bytep; mBitInd=bitp;
	}
	void setSizeBits(size_t bits) {
		BVASSERT((bits+7)/8 <= allocSize());
		mSizeBits = bits;
		//mEnd=mStart+(bits/8); mBitInd=bits%8;
	}

	// Concat unimplemented.
	//ByteVector(const ByteVector& source1, const ByteVector source2);

	/** Constructor: An empty Vector of a given size. */
	ByteVector(size_t wSize=0) { RN_MEMCHKNEW(ByteVector); init(wSize); }

	// Constructor: A ByteVector whose contents is from a string with optional length specified.
	// A copy of the string is malloced into the ByteVector.
	// ByteType is "unsigned char" so we need both signed and unsigned versions to passify C++.
	ByteVector(const ByteType *wdata,int wlen) { RN_MEMCHKNEW(ByteVector) initstr((const char*)wdata,wlen); }
	ByteVector(const ByteType *wdata) { RN_MEMCHKNEW(ByteVector) initstr((const char*)wdata); }
	ByteVector(const char *wdata,int wlen) { RN_MEMCHKNEW(ByteVector) initstr(wdata,wlen); }
	ByteVector(const char *wdata) { RN_MEMCHKNEW(ByteVector) initstr(wdata); }

	// Constructor: A ByteVector which is a duplicate of some other.
	// They both 'own' the memory using refcounts.
	// The const is tricky - other is modified despite the 'const', because only the ByteVector itself is 'const',
	// the memory it points to is not.
	// See also: alias.
	ByteVector(ByteVector& other) : mData(0) { RN_MEMCHKNEW(ByteVector) dup(other); }
	ByteVector(const ByteVector&other) : mData(0) { RN_MEMCHKNEW(ByteVector) dup(other); }

	ByteVector(const BitVector &other) { RN_MEMCHKNEW(ByteVector) init((other.size()+7)/8); setAppendP(0); append(other); }
	virtual ~ByteVector() { RN_MEMCHKDEL(ByteVector) clear(); }

	// Make a duplicate of the vector where both point into shared memory, and increment the refcnt.
	// Use clone if you want a completely distinct copy.
	void operator=(ByteVector& other) { dup(other); }

	// The BitVector class implements this with a clone().
	// However, this gets called if a hidden intermediary variable is required
	// to implement the assignment, for example: x = segment(...);
	// In this case it is safer for the class to call clone to be safe,
	// however, that is probably never what is wanted by the calling code,
	// so I am leaving it out of here.  Only use this type of assignment if the
	// other does not own the memory.
	// Update: With refcnts, these issues evaporate, and the two forms
	// of assignment are identical.
	void operator=(const ByteVector& other) {
#if BYTEVECTOR_REFCNT
		dup(other);
#else
		BVASSERT(other.mData == NULL);	// Dont use assignment in this case.
		set(other);
#endif
	}

	static int compare(const ByteVector &bv1, const ByteVector &bv2);
	bool eql(const ByteVector &other) const;
	bool operator==(const ByteVector &other) const { return eql(other); }
	bool operator!=(const ByteVector &other) const { return !eql(other); }
	// This is here so you put ByteVectors in a map, which needs operator< defined.
	bool operator<(const ByteVector &other) const { return compare(*this,other)<0; }
	bool operator>(const ByteVector &other) const { return compare(*this,other)>0; }
	bool operator<=(const ByteVector &other) const { return compare(*this,other)<=0; }
	bool operator>=(const ByteVector &other) const { return compare(*this,other)>=0; }

	/**@name Functions identical to Vector: */
	// Return a segment of a ByteVector that shares the same memory as the original.
	// The const is tricky - the original ByteVector itself is not modified, but the memory it points to is.
	ByteVector segment(size_t start, size_t span) const;
	// For the const version, since we are not allowed to modify the original
	// to change the refcnt, we have to either return a segment that does not own memory,
	// or a completely new piece of memory.  So I am using a different name so that
	// it is obvious that the memory ownership is being stripped off the ByteVector.
	const ByteVectorTemp segmentTemp(size_t start, size_t span) const;

	// Copy other to this starting at start.
	// The 'this' ByteVector must be allocated large enough to hold other.
	void setSegment(size_t start, ByteVector&other);

	bool isOwner() { return !!mData; }	// Do we own any memory ourselves?

	// Trim specified number of bytes from left or right in place.
	// growLeft is the opposite: move the mStart backward by amt, throw error if no room.
	// New space is uninitialized.
	// These are for byte-aligned only, so we assert bit index is 0.
	void trimLeft(unsigned amt);
	void trimRight(unsigned amt);
	ByteType *growLeft(unsigned amt);
	void growRight(unsigned amt);

	void fill(ByteType byte, size_t start, size_t span);
	void fill(ByteType byte, size_t start) { fill(byte,start,size()-start); }
	void fill(ByteType byte) { fill(byte,0,size()); }
	void appendFill(ByteType byte, size_t span);
	// Zero out the rest of the ByteVector:
	void appendZero() {
		if (bitind()) appendField(0,8-bitind());	// 0 fill partial byte.
		appendFill(0,allocSize() - size());		// 0 fill remaining bytes.
	}

	// Copy part of this ByteVector to a segment of another.
	// The specified span must not exceed our size, and it must fit in the target ByteVector.
	void copyToSegment(ByteVector& other, size_t start, size_t span) const;

	/** Copy all of this Vector to a segment of another Vector. */
	void copyToSegment(ByteVector& other, size_t start=0) const;

	// pat 2-2012: I am modifying this to use the refcnts, so to get
	// a segment with an incremented refcnt, use: alias().segment(...)
	//ByteVector alias() { return segment(0,size()); }
	ByteVector head(size_t span) const { return segment(0,span); }
	//const ByteVector headTemp(size_t span) const { return segmentTemp(0,span); }
	ByteVector tail(size_t start) const { return segment(start,size()-start); }
	//const ByteVector tailTemp(size_t start) const { return segmentTemp(start,size()-start); }

	// GSM04.60 10.0b.3.1: Note that fields in RLC blocks use network order,
	// meaning most significant byte first (cause they started on Sun workstations.)
	// It is faster to use htons, etc, than unpacking these ourselves.
	// Note that this family of functions all assume byte-aligned fields.
	// See setField/appendField for bit-aligned fields.
	unsigned grow(unsigned amt);
	unsigned growBits(unsigned amt);
	void setByte(size_t ind, ByteType byte) { BVASSERT(ind < size()); mStart[ind] = byte; }
	void setUInt16(size_t writeIndex,unsigned value);	// 2 byte value
	void setUInt32(size_t writeIndex, unsigned value);	// 4 byte value
	void appendByte(unsigned value) { BVASSERT(bitind()==0); setByte(grow(1),value); }
	void appendUInt16(unsigned value) { BVASSERT(bitind()==0); setUInt16(grow(2),value); }
	void appendUInt32(unsigned value) { BVASSERT(bitind()==0); setUInt32(grow(4),value); }
	ByteType getByte(size_t ind) const { BVASSERT(ind < size()); return mStart[ind]; }
	ByteType getNibble(size_t ind,int hi) const {
		ByteType val = getByte(ind); return hi ? (val>>4) : val & 0xf;
	}

	unsigned getUInt16(size_t readIndex) const;	// 2 byte value
	unsigned getUInt32(size_t readIndex) const;	// 4 byte value
	ByteType readByte(size_t &rp) { return getByte(rp++); }
	unsigned readUInt16(size_t &rp);
	unsigned readUInt32(size_t &rp);
	unsigned readLI(size_t &rp);	// GSM8.16 1 or 2 octet length indicator.

	void append(const ByteVector&other);
	void append(const BitVector&other);
	void append(const ByteType*bytes, unsigned len);
	void append(const char*bytes, unsigned len) { append((const ByteType*)bytes,len); }
	void appendLI(unsigned len);	// GSM8.16 1 or 2 octet length indicator.
	void append(const ByteVector*other) { append(*other); }
	void append(const BitVector*other) { append(*other); }

	// Set from another ByteVector.
	// The other is typically a segment which does not own the memory, ie:
	// v1.set(v2.segment())  The other is not a reference because
	// the result of segment() is not a variable to which
	// the reference operator can be applied.
	// This is not really needed any more because the refcnts take care of these cases.
	void set(ByteVector other)	// That's right.  No ampersand.
	{
#if BYTEVECTOR_REFCNT
		// Its ok, everything will work.
		dup(other);
#else
		BVASSERT(other.mData == NULL);	// Dont use set() in this case.
		clear();
		mStart=other.mStart; mEnd=other.mEnd; mAllocEnd = other.mAllocEnd; mBitInd = other.mBitInd;
#endif
	}

	// Bit aligned operations.
	// The "2" suffix versions specify both byte and bit index in the range 0..7.
	// The "R1" suffix versions are identical to the "2" suffix versions,
	// but with start bit numbered from 1 like this: 8 | 7 | ... | 1
	// This is just a convenience because all the specs number the bits this weird way.
	// The no suffix versions take a bit pos ranging from 0 to 8*size()-1

	// Get a bit from the specified byte, numbered like this: 0 | 1 | ... | 7
	bool getBit2(size_t byteIndex, unsigned bitIndex) const;
	// Get a bit in same bit order, but with start bit numbered from 1 like this: 8 | 7 | ... | 1
	// Many GSM L3 specs specify numbering this way.
	bool getBitR1(size_t byteIndex, unsigned bitIndex) const {
		return getBit2(byteIndex,8-bitIndex);
	}
	bool getBit(unsigned bitPos) const { return getBit2(bitPos/8,bitPos%8); }
	void setBit2(size_t byteIndex, unsigned bitIndex, unsigned val);
	void setBit(unsigned bitPos, unsigned val) { setBit2(bitPos/8,bitPos%8,val); }

	// Set/get fields giving both byte and bit index.
	void setField2(size_t byteIndex, size_t bitIndex, uint64_t value,unsigned lengthBits);
	uint64_t getField2(size_t byteIndex, size_t bitIndex, unsigned lengthBits) const;
	uint64_t getFieldR1(size_t byteIndex, size_t bitIndex, unsigned length) const {
		return getField2(byteIndex,8-bitIndex,length);
	}

	// Set/get bit field giving bit position treating the entire ByteVector as a string of bits.
	void setField(size_t bitPos, uint64_t value, unsigned lengthBits) {
		setField2(bitPos/8,bitPos%8,value,lengthBits);
	}
	uint64_t getField(size_t bitPos, unsigned lengthBits) const {	// aka peekField
		return getField2(bitPos/8,bitPos%8,lengthBits);
	}

	// Identical to getField, but add lengthBits to readIndex.
	uint64_t readField(size_t& readIndex, unsigned lengthBits) const {
		uint64_t result = getField(readIndex,lengthBits);
		readIndex += lengthBits;
		return result;
	}

	void appendField(uint64_t value,unsigned lengthBits);
	// This works for Field<> data types.
	void appendField(ItemWithValueAndWidth &item) {
		appendField(item.getValue(),item.getWidth());
	}

	std::string str() const;
	std::string hexstr() const;
};

class ByteVectorTemp : public ByteVector
{
	public:
	ByteVectorTemp(ByteType*wstart,ByteType*wend) : ByteVector(Dorky(),wstart,wend) {}
	ByteVectorTemp(size_t) { assert(0); }

	// Constructor: A ByteVector whose contents is from a string with optional length specified.
	// These cannot be const because the ByteVectorTemp points into the original memory
	// and has methods to modify it.
	// ByteType is "unsigned char" so we need both signed and unsigned versions to passify C++.
	// All the explicit casts are required.  This is so brain dead.
	ByteVectorTemp(ByteType *wdata,int wlen) : ByteVector(Dorky(),wdata,wdata+wlen) {}
	ByteVectorTemp(ByteType *wdata) : ByteVector(Dorky(),wdata,wdata+strlen((char*)wdata)) {}
	ByteVectorTemp(char *wdata,int wlen)  : ByteVector(Dorky(),(ByteType*)wdata,(ByteType*)wdata+wlen) {}

	ByteVectorTemp(char *wdata) : ByteVector(Dorky(),(ByteType*)wdata,(ByteType*)wdata+strlen((char*)wdata)) {}
	ByteVectorTemp(ByteVector& other) : ByteVector(Dorky(),other.begin(),other.begin()+other.size()) {}

	ByteVectorTemp(BitVector &) { assert(0); }
};

// Warning: C++ prefers an operator<< that is const to one that is not.
std::ostream& operator<<(std::ostream&os, const ByteVector&vec);
#endif
