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

#include <stdio.h>

#include "MsgBase.h"

void MsgCommonWrite::_define_vtable() {}
void MsgCommonLength::_define_vtable() {}
void MsgCommonText::_define_vtable() {}

// Copied from same functions in L3Frame:
static const unsigned fillPattern[8] = {0,0,1,0,1,0,1,1};

void MsgCommonWrite::writeField(const ItemWithValueAndWidth&item,const char*)
{
	mResult.writeField(wp,item.getValue(),item.getWidth());
}

void MsgCommonWrite::writeField(uint64_t value, unsigned len, const char *, Type2Str)
{
	mResult.writeField(wp,value,len);
}

void MsgCommonWrite::writeOptFieldLH(uint64_t value, unsigned len, int present, const char *)
{
	if (present) { writeH(); writeField(value,len); } else { writeL(); }
}

// pat added: write an Optional Field controlled by an initial 0/1 field.
void MsgCommonWrite::writeOptField01(uint64_t value, unsigned len, int present, const char*)
{
	if (present) { write1(); writeField(value,len); } else { write0(); }
}

void MsgCommonWrite::writeH()
{
	unsigned fillBit = fillPattern[wp%8];	// wp is in MsgCommon
	writeField(!fillBit,1);
}


void MsgCommonWrite::writeL()
{
	unsigned fillBit = fillPattern[wp%8];	// wp is in MsgCommon
	writeField(fillBit,1);
}

void MsgCommonWrite::writeBitMap(bool*bitmap,unsigned bitmaplen, const char*name)
{
	for (unsigned i=0; i<bitmaplen; i++) {
		writeField(bitmap[i],1,name);
	}
}

#if 0
static void truncateredundant(char *str, int len)
{
	char *end = str + len - 1, *cp = end;
	// Chop off trailing chars that are replicated.
	int lastch = *cp;
	for (; cp > str; cp--) {
		if (*cp != lastch) {
			if (cp < end-6) { strcpy(cp+2,"..."); }
			break;
		}
	}
}
#endif

#define TOHEX(v) ((v) + ((v) < 10 ? '0' : ('a'-10)))
void MsgCommonText::writeBitMap(bool*bitmap,unsigned bitmaplen, const char*name)
{
	char txtbits[bitmaplen+6], *tp = txtbits;
	unsigned i, accum = 0;
	for (i=0; i<bitmaplen; i++) {
		accum = (accum<<1) + (bitmap[i] ? 1 : 0);
		if (((i+1) & 3) == 0) {
			*tp++ = TOHEX(accum);
			accum = 0;
		}
	}
	//if (i & 3) { *tp++ = TOHEX(accum); }  Our bitmap is always evenly % 4, so dont bother.
	*tp = 0;
	//truncateredundant(txtbits,bitmaplen);
	mos << " " << name << "=(" << txtbits << ")";
}

// This could fail multi-threaded, but it is only used for debug output.
const char *tohex(int val)
{
	static char buf[20];
	sprintf(buf,"0x%x",val);
	return buf;
}
