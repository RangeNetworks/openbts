/**@file Declarations for common-use control-layer functions. */
/*
* Copyright 2014 Range Networks, Inc.
*
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

#ifndef _CODECSET_H_
#define _CODECSET_H_ 1

namespace GSM { class L3MobileIdentity; };

namespace Control {

// Meaning of these bits is hard to find: It is in 48.008 3.2.2.11:
enum CodecType { 	// Codec Bitmap defined in 26.103 6.2.  It is one or two bytes
	// low bit of first byte in bitmap
	CodecTypeUndefined = 0,
	GSM_FR = 0x1, // aka GSM610
	GSM_HR = 0x2,
	GSM_EFR = 0x4,
	AMR_FR = 0x8,
	AMR_HR = 0x10,
	UMTS_AMR = 0x20,
	UMTS_AMR2 = 0x40,
	TDMA_EFR = 0x80,		// high bit of first byte in bitmap
	// We can totally ignore the second byte:
	PDC_EFR = 0x100,		// low bit of second byte in bitmap
	AMR_FR_WB = 0x200,
	UMTS_AMR_WB = 0x400,
	OHR_AMR = 0x800,
	OFR_AMR_WB = 0x1000,
	OHR_AMR_WB = 0x2000,
	// then two reserved bits.
	
	// In addition the above codecs defined in the GSM spec and used on the air-interface,
	// we will put other codecs we might want to use for RTP on the SIP interface in here too
	// so we can use the same CodecSet in the SIP directory.
	// This is not in the spec, but use this value to indicate none of the codecs above.
	PCMULAW = 0x10000,		// G.711 PCM, 64kbps. comes in two flavors: uLaw and aLaw.
	PCMALAW = 0x20000 		// We dont support it yet.
							// There is also G711.1, which is slighly wider band, 96kbps.
};

const char *CodecType2Name(CodecType ct);

// (pat) Added 10-22-2012.
// 3GPP 24.008 10.5.4.32 and 3GPP 26.103
class CodecSet {
	public:
	CodecType mCodecs;	// It is a set of CodecEnum
	bool isSet(CodecType bit) { return mCodecs & bit; }
	bool isEmpty() { return !mCodecs; }
	CodecSet(): mCodecs(CodecTypeUndefined) {}
	CodecSet(CodecType wtype) : mCodecs(wtype) {}
	// Allow logical OR of two CodecSets together.
	void orSet(CodecSet other) { mCodecs = (CodecType) (mCodecs | other.mCodecs); }
	void orType(CodecType vals) { mCodecs =  (CodecType) (mCodecs | vals); }
	CodecSet operator|(CodecSet other) { return CodecSet((CodecType)(mCodecs | other.mCodecs)); }
	void text(std::ostream&) const;
	friend std::ostream& operator<<(std::ostream& os, const CodecSet&);
};

};	// namespace Control
#endif
