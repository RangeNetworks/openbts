/*
* Copyright 2011 Range Networks, Inc.
*
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
#include "ByteVector.h"

// Set the char[2] array at ip to a 16-bit int value, swizzling bytes as needed for network order.
void sethtons(ByteType *cp,unsigned value)
{
	uint16_t tmp = htons(value);
	ByteType *tp = (ByteType*)&tmp;
	cp[0]=tp[0]; cp[1]=tp[1];	// Overkill but safe.
}

// Set the char[4] array at ip to a 32-bit int value, swizzling bytes as needed for network order.
void sethtonl(ByteType *cp,unsigned value)
{
	uint32_t tmp = htonl(value);
	ByteType *tp = (ByteType*)&tmp;
	cp[0]=tp[0]; cp[1]=tp[1]; cp[2]=tp[2]; cp[3]=tp[3];
}

uint16_t getntohs(ByteType *cp)
{
	uint16_t tmp;
	ByteType *tp = (ByteType*)&tmp;
	tp[0]=cp[0]; tp[1]=cp[1];
	return ntohs(tmp);
}

uint32_t getntohl(ByteType *cp)
{
	uint32_t tmp;
	ByteType *tp = (ByteType*)&tmp;
	tp[0]=cp[0]; tp[1]=cp[1]; tp[2]=cp[2]; tp[3]=cp[3];
	return ntohl(tmp);
}

void ByteVector::clear()
{
	if (mData) {
#if BYTEVECTOR_REFCNT
		if (decRefCnt() <= 0) { delete[] mData; RN_MEMCHKDEL(ByteVectorData) }
#else
		delete[] mData;
#endif
	}
	mSizeBits = 0;
	mData = NULL;
}

void ByteVector::init(size_t size)
{
	//mBitInd = 0;
	if (size == 0) {
		mData = mStart = 0;
	} else {
#if BYTEVECTOR_REFCNT
		RN_MEMCHKNEW(ByteVectorData)
		mData = new ByteType[size + mDataOffset];
		setRefCnt(1);
		mStart = mData + mDataOffset;
#else
		mData = new ByteType[size];
		mStart = mData;
#endif
	}
	mAllocEnd = mStart + size;
	mSizeBits = size*8;
}

// Make a full memory copy of other.
// We clone only the filled in area, not the unused allocated area.
void ByteVector::clone(const ByteVector &other)
{
	clear();
	init(other.size());
	memcpy(mStart,other.mStart,other.size());
}

// Make this a copy of other.
// It it owns memory, share it using refcnts.
// Formerly: moved ownership of allocated data to ourself.
void ByteVector::dup(const ByteVector &other)
{
	clear();
	mData=other.mData;
	mStart=other.mStart;
	mSizeBits=other.mSizeBits;
	mAllocEnd = other.mAllocEnd;
	//mBitInd = other.mBitInd;
#if BYTEVECTOR_REFCNT
	if (mData) incRefCnt();
#else
	other.mData=NULL;
#endif
}

// Return a segment of a ByteVector that shares the same memory as the original.
ByteVector ByteVector::segment(size_t start, size_t span) const
{
#if NEW_SEGMENT_SEMANTICS
	BVASSERT(start+span <= size());
	ByteVector result(*this);
	result.mStart = mStart + start;
	result.mSizeBits = span*8;
	//result.mEnd = result.mStart + span;
	//BVASSERT(result.mEnd<=mEnd);
	return result;
#else
	ByteType* wStart = mStart + start;
	ByteType* wEnd = wStart + span;
	BVASSERT(wEnd<=mEnd);
	return ByteVector(wStart,wEnd);
#endif
}

// This returns a segment that does not share ownership of the original memory,
// so when the original is deleted, this is destroyed also, and without warning.
// Very easy to insert bugs in your code, which is why it is called segmentTemp to indicate
// that it is a ByteVector for temporary use only.
const ByteVectorTemp ByteVector::segmentTemp(size_t start, size_t span) const
{
	BVASSERT(start+span <= size());
	ByteType* wStart = mStart + start;
	ByteType* wEnd = wStart + span;
	//BVASSERT(wEnd<=mEnd);
	return ByteVectorTemp(wStart,wEnd);
}

// Copy other to this starting at start.
// The 'this' ByteVector must be allocated large enough to hold other.
// Unlike Vector, the size() is increased to make it fit, up to the allocated size.
void ByteVector::setSegment(size_t start, ByteVector&other)
{
	BVASSERT(start <= size());	// If start == size(), nothing is copied.
	BVASSERT(bitind() == 0);	// This function only allowed on byte-aligned data.
	ByteType* base = mStart + start;
	int othersize = other.size();
	BVASSERT(mAllocEnd - base >= othersize);
	memcpy(base,other.mStart,othersize);
	//if (mEnd - base < othersize) { mEnd = base + othersize; }	// Grow size() if necessary.
	if (mSizeBits/8 < start+othersize) { mSizeBits = (start+othersize)*8; }
}

// Copy part of this ByteVector to a segment of another.
// The specified span must not exceed our size, and it must fit in the target ByteVector.
// Unlike Vector, the size() of other is increased to make it fit, up to the allocated size.
void ByteVector::copyToSegment(ByteVector& other, size_t start, size_t span) const
{
	ByteType* base = other.mStart + start;
	BVASSERT(start <= other.size());	// If start == size(), nothing is copied.
	BVASSERT(base+span<=other.mAllocEnd);
	//BVASSERT(mStart+span<=mEnd);
	//BVASSERT(base+span<=other.mAllocEnd);
	memcpy(base,mStart,span);
	//if (base+span > other.mEnd) { other.mEnd = base+span; }	// Increase other.size() if necessary.
	if (other.size() < start+span) { other.mSizeBits = (start+span)*8; }
}

/** Copy all of this Vector to a segment of another Vector. */
void ByteVector::copyToSegment(ByteVector& other, size_t start /*=0*/) const
{
	copyToSegment(other,start,size());
}


void ByteVector::append(const ByteType *bytes, unsigned len)
{
	memcpy(&mStart[grow(len)],bytes,len);
}

// Does change size().
void ByteVector::appendFill(ByteType byte, size_t span)
{
	memset(&mStart[grow(span)],byte,span);
}

void ByteVector::append(const ByteVector&other)
{
	append(other.mStart,other.size());
	//BVASSERT(othersize <= mAllocEnd - mEnd);
	//memcpy(mEnd,other.mStart,othersize);
	//mEnd += othersize;
}

// append a BitVector to this, converting the BitVector back to bytes.
void ByteVector::append(const BitVector&other)
{
	int othersizebits = other.size();
	int bitindex = bitind();
	if (bitindex) {
		// Heck with it.  Optimize this if you want to use it.
		int iself = growBits(othersizebits);	// index into this.
		int iother = 0;							// index into other
		// First partial byte
		int rem = 8-bitindex;
		if (rem > othersizebits) rem = othersizebits;
		setField(iself,other.peekField(iother,rem),rem);
		iself += rem; iother += rem;
		// Copy whole bytes.
		for (; othersizebits-iother>=8; iother+=8, iself+=8) {
			setByte(iself/8,other.peekField(iother,8));
		}
		// Final partial byte.
		rem = othersizebits-iother;
		if (rem) {
			setField(iself,other.peekField(iother,rem),rem);
		}
		return;
	} else {
		other.pack(&mStart[growBits(othersizebits)/8]);
	}
	//BVASSERT(othersize <= mAllocEnd - mEnd);
	//other.pack(mEnd);
	//mEnd += othersize;
}

// Length Indicator: GSM08.16 sec 10.1.2
// The length indicator may be 1 or 2 bytes, depending on bit 8,
// which is 0 to indicate a 15 bit length, or 1 to indicate a 7 bit length.
unsigned ByteVector::readLI(size_t &wp)
{
	unsigned byte1 = getByte(wp++);
	if (byte1 & 0x80) { return byte1 & 0x7f; }
	return (byte1 * 256) + getByte(wp++);
}

// This is a two byte length indicator as per GSM 08.16 10.1.2
void ByteVector::appendLI(unsigned len)
{
	if (len < 255) {
		appendByte(len | 0x80);
	} else {
		BVASSERT(len <= 32767);
		appendByte(0x7f&(len>>8));
		appendByte(0xff&(len>>8));
	}
}

// The inverse of trimRight.  Like an append but the new area is uninitialized.
void ByteVector::growRight(unsigned amt)
{
	BVASSERT(!bitind());
	BVASSERT(amt <= size());
	mSizeBits += 8*amt;
}

// The inverse of trimLeft
ByteType* ByteVector::growLeft(unsigned amt)
{
	ByteType *newstart = mStart - amt;
	BVASSERT(newstart >= mData + mDataOffset);
	mSizeBits += 8*amt;
	return mStart = newstart;
}

void ByteVector::trimLeft(unsigned amt)
{
	BVASSERT(amt <= size());
	mStart += amt;
	mSizeBits -= 8*amt;
}

void ByteVector::trimRight(unsigned amt)
{
	BVASSERT(!bitind());
	BVASSERT(amt <= size());
	//mEnd -= amt;
	mSizeBits -= 8*amt;
}

// For appending.
// Grow the vector by the specified amount of bytes and return the index of that location.
unsigned ByteVector::grow(unsigned amt)
{
	unsigned writeIndex = sizeBytes(); // relative to mStart.
	BVASSERT(bitind() == 0);	// If it is not byte-aligned, cant use these functions; use setField instead.
	setSizeBits(mSizeBits + 8*amt);
	//BVASSERT(amt < sizeRemaining());
	//mSizeBits += 8*amt;
	//unsigned writeIndex = mEnd - mStart;
	//mEnd += amt;
	//BVASSERT(mEnd <= mAllocEnd);
	return writeIndex;
}

// For appending.
// Grow the vector by amt in bits; return the old size in bits.
unsigned ByteVector::growBits(unsigned amt)
{
	int oldsizebits = sizeBits();
	setSizeBits(oldsizebits + amt);
	return oldsizebits;
}


// GSM04.60 10.0b.3.1: Note that fields in RLC blocks use network order,
// meaning most significant byte first (cause they started on Sun workstations.)
// It is faster to use htons, etc, than unpacking these ourselves.
void ByteVector::setUInt16(size_t writeIndex,unsigned value) {	// 2 byte value
	BVASSERT(writeIndex <= size() - 2);
	sethtons(&mStart[writeIndex],value);
}

void ByteVector::setUInt32(size_t writeIndex, unsigned value) {	// 4 byte value
	BVASSERT(writeIndex <= size() - 4);
	sethtonl(&mStart[writeIndex],value);
}

// Does not change size().
void ByteVector::fill(ByteType byte, size_t start, size_t span) {
	ByteType *dp=mStart+start;
	ByteType *end=dp+span;
	BVASSERT(end<=mAllocEnd);
	while (dp<end) { *dp++ = byte; }
}

unsigned ByteVector::getUInt16(size_t readIndex) const {	// 2 byte value
	BVASSERT(readIndex <= size() - 2);
	uint16_t tmp;
	ByteType *tp = (ByteType*)&tmp;
	ByteType *cp = &mStart[readIndex];
	tp[0]=cp[0]; tp[1]=cp[1];
	return ntohs(tmp);
}
unsigned ByteVector::readUInt16(size_t &rp) {
	unsigned result = getUInt16(rp);
	rp+=2;
	return result;
}

unsigned ByteVector::getUInt32(size_t readIndex) const {	// 4 byte value
	BVASSERT(readIndex <= size() - 4);
	uint32_t tmp;
	ByteType *tp = (unsigned char*)&tmp;
	ByteType *cp = &mStart[readIndex];
	tp[0]=cp[0]; tp[1]=cp[1]; tp[2]=cp[2]; tp[3]=cp[3];
	return ntohl(tmp);
}
unsigned ByteVector::readUInt32(size_t &rp) {
	unsigned result = getUInt32(rp);
	rp+=4;
	return result;
}


// This function returns just the payload part as a hex string.
// Also works if the ByteVector is encoded as BCD.
// Probably not efficient, but we are using C++.
std::string ByteVector::hexstr() const
{
	int b = 0, numBits = (int) sizeBits();		// Must be int, not unsigned
	std::string ss;
	while (numBits > 0) {
		char ch = getNibble(b,1);
		ss.push_back(ch + (ch > 9 ? ('A'-10) : '0'));
		numBits -= 4;
		if (numBits >= 4) {
			ch = getNibble(b,0);
			ss.push_back(ch + (ch > 9 ? ('A'-10) : '0'));
		}
		b++;
		numBits -= 4;
	}
	return ss;
}

// This function returns "ByteVector(size=... data:...)"
std::string ByteVector::str() const
{
	std::ostringstream ss;
	ss << *this;
	return ss.str();
}

std::ostream& operator<<(std::ostream&os, const ByteVector&vec)
{
	int i, size=vec.size(); char buf[10];
	os <<"ByteVector(size=" <<size <<" data:";
	for (i=0; i < size; i++) {
		sprintf(buf," %02x",vec.getByte(i));
		os << buf;
	}
	os <<")";
	return os;
}

static ByteType bitMasks[8] = { 0x80, 0x40, 0x20, 0x10, 8, 4, 2, 1 };

// Get a bit from the specified byte, numbered like this:
// bits: 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7
bool ByteVector::getBit2(size_t byteIndex, unsigned bitIndex) const
{
	BVASSERT(bitIndex >= 0 && bitIndex <= 7);
	//return !!(getByte(byteIndex) & (1 << (7-bitIndex)));
	return !!(getByte(byteIndex) & bitMasks[bitIndex]);
}

// Get a bit from the specified byte, numbered like this:
// bits: 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7
void ByteVector::setBit2(size_t byteIndex, unsigned bitIndex, unsigned val)
{
	BVASSERT(bitIndex >= 0 && bitIndex <= 7);
	BVASSERT(byteIndex < size());
	ByteType mask = bitMasks[bitIndex];
	mStart[byteIndex] = val ? (mStart[byteIndex] | mask) : (mStart[byteIndex] & ~mask);
}

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

// Write a bit field starting at specified byte and bit, each numbered from 0 
void ByteVector::setField2(size_t byteIndex, size_t bitIndex, uint64_t value,unsigned lengthBits)
{
	BVASSERT(bitIndex >= 0 && bitIndex <= 7);
	// Example: bitIndex = 2, length = 2;
	// 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7
	// 0   0   X   X   0   0   0   0
	// endpos = 4; nbytes = 0; lastbit = 4; nbits = 2; mask = 3; shift = 4;

	unsigned endpos = bitIndex + lengthBits;	// 1 past the 0-based index of the last bit.
	unsigned nbytes = (endpos-1) / 8;		// number of bytes that will be modified, minus 1.
	ByteType *dp = mStart + byteIndex + nbytes;

	unsigned lastbit = endpos % 8;	// index of first bit not to be replaced, or 0.
	// Number of bits to modify in the current byte, starting at the last byte.
	unsigned nbits = lastbit ?  MIN(lengthBits,lastbit) : MIN(lengthBits,8);

	for (int len = lengthBits; len > 0; dp--) {
		// Mask of number of bits to be modified in this byte, starting from LSB.
		unsigned mask = (1 << nbits) - 1;
		ByteType val = value & mask;
		value >>= nbits;
		if (lastbit) {
			// Shift val and mask so they are aligned with the bits to modify in the last byte,
			// noting that we modify the last byte first, since we work backwards.
			int shift = 8 - lastbit;
			mask <<= shift;
			val <<= shift;
		}
		*dp = (*dp & ~mask) | (val & mask);
		len -= nbits;
		nbits = MIN(len,8);

		lastbit = 0;
	}
}

void ByteVector::appendField(uint64_t value,unsigned lengthBits)
{
	setField(growBits(lengthBits),value,lengthBits);
	/*** old
	int endpos = mBitInd + lengthBits;	// 1 past the 0-based index of the last bit.
	int nbytes = (endpos-1) / 8;		// number of new bytes needed.
	if (mBitInd == 0) nbytes++;			// if at 0, the next byte has not been alloced yet.
	setField2(grow(nbytes),mBitInd,value,lengthBits);
	mBitInd = endpos % 8;
	***/
}

// Read a bit field starting at specified byte and bit, each numbered from 0.
// Bit numbering is from high to low, like this:
//  getField bitIndex: 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7
// Note that this is inverted with respect to the numbering scheme used
// in many GSM specs, which looks like this:
//  GSM specs: 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1
// Note that some GSM specs use low-to-high and some use high-to-low numbering.
// Generally, where the BitVector class is used they use low-to-high numbering,
// which is rectified in the FEC classes by the byteswapping the BitVector before being used.
uint64_t ByteVector::getField2(size_t byteIndex, size_t bitIndex, unsigned lengthBits) const
{
	ByteType *dp = mStart + byteIndex;
	int len = (int) lengthBits;
	BVASSERT(bitIndex >= 0 && bitIndex <= 7);
	// Get first byte:
	// This was for bitIndex running from low bit to high bit:
	// int nbits = bitIndex+1;	// Number of bits saved from byte.
	// This is for bitIndex running from 0=>high bit to 7=>low bit:
	int nbits = 8-bitIndex;	// Number of bits saved from byte, ignoring len restriction.
	// Example: bitIndex=3 => 0 | 0 | 0 | X | X | X | X | X  => AND with 0x1f

	uint64_t accum = *dp++ & (0x0ff >> (8-nbits));	// Preserve right-most bits.
	if (len < nbits) { accum >>= (nbits - len); return accum; }
	len -= nbits;

	// Get the full bytes:
	for (; len >= 8; len -= 8) { accum = (accum << 8) | *dp++; }

	// Append high bits of last byte:
	if (len>0) { accum = (accum << len) | (*dp >> (8-len)); }
	return accum;
}

// This is static - there is no 'this' argument.
int ByteVector::compare(const ByteVector &bv1, const ByteVector &bv2)
{
	unsigned bv1size = bv1.sizeBits(), bv2size = bv2.sizeBits();
	unsigned minsize = MIN(bv1size,bv2size);
	unsigned bytes = minsize/8;
	int result;
	// Compare the full bytes.
	if (bytes) {
		if ((result = memcmp(bv1.begin(),bv2.begin(),bytes))) {return result;}
	}
	// Compare the partial byte, if any.
	unsigned rem = minsize%8;
	if (rem) {
		if ((result = (int) bv1.getField2(bytes,0,rem) - (int) bv2.getField2(bytes,0,rem))) {return result;}
	}
	// All bits the same.  The longer guy wins.
	return (int)bv1size - (int)bv2size;
}

// We assume that if the last byte is a partial byte (ie bitsize % 8 != 0)
// then the remaining unused bits are all equal, should be 0.
// If they were set with setField, that will be the case.
bool ByteVector::eql(const ByteVector &other) const
{
	if (sizeBits() != other.sizeBits()) {return false;}	// Quick check to avoid full compare.
	return 0 == compare(*this,other);
	//unsigned bytes = bvsize/8;
	//ByteType *b1 = mStart, *b2 = other.mStart;
	//for (int i = size(); i > 0; i--) { if (*b1++ != *b2++) return false; }
	//return true;
}

#ifdef TEST
void ByteVectorTest()
{
	unsigned byten, bitn, l, i;
	const unsigned bvlen = 20;
	ByteVector bv(bvlen), bv2(bvlen), pat(bvlen);
	BitVector bitv(64);
	int printall = 0;
	int tests = 0;

	ByteVector bctest = ByteVector("12345");
	for (i = 0; i < 5; i++) {
		assert(bctest.getByte(i) == '1'+i);
	}

	bv.fill(3);
	for (i = 0; i < bvlen; i++) {
		assert(bv.getByte(i) == 3);
	}

	for (byten = 0; byten <= 1; byten++) {
		for (bitn = 0; bitn <= 7; bitn++) {
			for (l = 1; l <= 33; l++) {
				tests++;
				uint64_t val = 0xffffffffffull & ((1ull<<l)-1);
				// Test setField vs getField.
				bv.fill(3);	// Pattern we can check for after the test.
				pat.fill(3);
				bv.setField2(byten,bitn,val,l);
				if (printall) {
					std::cout<<"ok:"<<LOGVAR(byten)<<LOGVAR(bitn)<<LOGVAR(l)
						<<LOGVAR(val)<<" result="<<bv<<"\n";
				}
				if (bv.getField2(byten,bitn,l) != val) {
					std::cout<<"setField fail:"<<LOGVAR(byten)<<LOGVAR(bitn)<<LOGVAR(l)
						<<LOGVAR(val)<<" result="<<bv<<"\n";
					break;
				}

				// Make sure the pattern was not disturbed elsewhere.
				if (byten == 1) { assert(bv.getByte(0) == 3); }
				if (bitn) { assert(bv.getField2(byten,0,bitn) == pat.getField2(byten,0,bitn)); }
				int endbit = byten*8 + bitn + l;
				assert(bv.getField(endbit,30) == pat.getField(endbit,30));

				// Test getBit vs getField.
				for (int b1=0; b1<3; b1++) {
					for (int k = 0; k <= 7; k++) {
						tests++;
						if (bv.getBit2(b1,k) != bv.getField2(b1,k,1)) {
							std::cout<<"getBit fail:"<<LOGVAR(byten)<<LOGVAR(bitn)<<LOGVAR(l)<<"\n";
						}
					}
				}

				// Test setField vs BitVector::fillField
				bitv.zero();
				bitv.fillField(byten*8+bitn,val,l);
				bitv.pack(bv2.begin());
				bv.fill(0);
				bv.setField(byten*8+bitn,val,l);
				if (bv != bv2) {
					std::cout<<"ByteVector BitVector mismatch:"<<LOGVAR(byten)<<LOGVAR(bitn)<<LOGVAR(l)<<LOGVAR(val)<<"\n";
					std::cout<<"bv="<<bv<<" bv2="<<bv2<<"\n";
					std::cout <<"bitv="<<bitv<<"\n";
					break;
				}

			}
		}

		// Test fields with large bit counts.
		for (bitn = 1; bitn <= 40; bitn++) {
			for (l = 1; l <= 33; l++) {
				uint64_t val = 0xffffffffffull & ((1ull<<l)-1);
				uint64_t result, expected;
				// Test appendField
				for (int start = 0; start <= 17; start++) {	// start bit for append test
					ByteVector bv(20);
					bv.fill(0);
					bv.setAppendP(byten);
					BVASSERT(bv.size() == byten);

					bv.appendField(0,start);	// Move the starting append position.
					if ((result=bv.sizeBits()) != (expected=(byten*8)+start)) {
						std::cout<<"appendField length1 error"<<LOGVAR(expected)<<LOGVAR(result)<<"\n";
						std::cout<<"at:"<<LOGVAR(byten)<<LOGVAR(bitn)<<LOGVAR(l)<<LOGVAR(val)<<LOGVAR(start)<<"\n";
					}

					bv.appendField(val,bitn);	// This is the test.

					if ((result=bv.sizeBits()) != (expected=(byten*8)+start+bitn)) {
						std::cout<<"appendField length2 error"<<LOGVAR(expected)<<LOGVAR(result)<<"\n";
						std::cout<<"at:"<<LOGVAR(byten)<<LOGVAR(bitn)<<LOGVAR(l)<<LOGVAR(val)<<LOGVAR(start)<<"\n";
					}
					if (bv.getField(0,byten*8+start) != 0) {
						std::cout<<"appendField start error\n";
						std::cout<<"at:"<<LOGVAR(byten)<<LOGVAR(bitn)<<LOGVAR(l)<<LOGVAR(val)<<LOGVAR(start)<<"\n";
					}
					if ((result=bv.getField(byten*8+start,bitn)) != (expected=(val & ((1ull<<bitn)-1)))) {
						bv.setAppendP(10);	// Needed to read beyond end of our test.
						std::cout<<"appendField value error"<<LOGVAR(expected)<<LOGVAR(result)<<"\n";
						std::cout<<"at:"<<LOGVAR(byten)<<LOGVAR(bitn)<<LOGVAR(l)<<LOGVAR(val)<<LOGVAR(start)<<"\n";
						std::cout<<"bv="<<bv.segmentTemp(0,6)<<"\n";
					}
					if (bv.getField(byten*8+start+bitn,32) != 0) {
						std::cout<<"appendField overrun error\n";
						std::cout<<"at:"<<LOGVAR(byten)<<LOGVAR(bitn)<<LOGVAR(l)<<LOGVAR(val)<<LOGVAR(start)<<"\n";
					}
				}
			}
		}
	}
	std::cout<<"Finished ByteVector "<<tests<<" tests\n";
}

int main()
{
	ByteVectorTest();
}
#endif	// ifdef TEST
