/**@file @brief L3 Radio Resource messages related to GPRS */
/*
* Copyright 2008, 2010 Free Software Foundation, Inc.
* Copyright 2011 Kestrel Signal Processing, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#ifndef GSML3GPRSELEMENTS_H
#define GSML3GPRSELEMENTS_H
#include "GSML3Message.h"
#include "../GPRS/MsgBase.h"
#include "../GPRS/GPRSExport.h"
#include "ScalarTypes.h"

namespace GSM {



/** Defined in GSM 04.60 12.24 but used in GSM 04.08 10.5.2.37b - SI13 Rest Octets. */
class L3GPRSCellOptions : public GenericMessageElement
{
	public:

	L3GPRSCellOptions() { }

	size_t lengthBits() const;
	void writeBits(L3Frame& dest, size_t &wp) const;
	void text(std::ostream& os) const;
	//void parseV( const L3Frame&, size_t&, size_t) { abort(); }
	//void parseV(const L3Frame&, size_t&) { abort(); }
};


// GSM 04.08 10.5.2.16
// (pat) This message is sent to MS inside the rest octets of an Immediate Assignment message
// on CCCH in response to a CHANNEL REQUEST message sent by the MS on RACH.
// The Packet Uplink and Packet Downlink assignment messages are so similar
// they are combined in one structure.
// Note that there are also a Packet Uplink/Downlink Assignment messages (GSM04.60) that do
// the same thing as these messages, but with completely different format,
// and sent on PACCH not CCCH.
// TODO: Padding to end of message should be as for RR messages, not PDCH messages.
// This should be done by whomever sends this message.
struct L3IAPacketAssignment : GenericMessageElement
{
		// (note: the MS may choose to send a Packet Uplink Request instead.
	// There are three types of packet uplink assignment, and one type of downlink assignment:
	enum IAPacketAssignmentType {
		PacketUplinkAssignUninitialized,
		PacketUplinkAssignFixed,
		PacketUplinkAssignDynamic,
		PacketUplinkAssignSingleBlock,
		PacketDownlinkAssign
	};
	const char *IAPacketAssignmentTypeText(enum IAPacketAssignmentType type) const;

	enum IAPacketAssignmentType mPacketAssignmentType;

	Bool_z mTFIPresent;
	Field_z<5> mTFIAssignment;
	Field_z<1> mPolling;	// Set if MS is being polled for Packet Control Acknowledgement.

	// This part for Uplink Dynamic Allocation Mode [for packet uplink transfer]:
	Field_z<3> mUSF;
	Field_z<1> mUSFGranularity;

	// This part for Uplink Fixed Allocation Mode [for packet uplink transfer]:
	Field_z<5> mAllocationBitmapLength;
	Field_z<32> mAllocationBitmap;		// variable sized, up to 32 bits

	// alpha, gamma for MS power control.  See GSM05.08
	Field_z<4> mAlpha;		Bool_z mAlphaPresent;		// optional param
	Field_z<5> mGamma;

	Field_z<4> mTimingAdvanceIndex;  Bool_z mTimingAdvanceIndexPresent;	// optional param
	// From GSM 04.08 10.5.2.16, and I quote:
	// The TBF starting time is coded using the same coding as the V format
	// of the type 3 information element Starting Time (10.5.2.38).

	Field_z<16> mTBFStartingTime;    Bool_z mTBFStartingTimePresent;	// optional param
	Field_z<2> mChannelCodingCommand;	// CS-1, CS-2, CS-3 or CS-4.
	Field_z<1> mTLLIBlockChannelCoding;
	// (pat) We wont use the downlink power control parameters (P0, etc), so dont even bother.
	// L3AssignmentPowerOption mPowerOption;

	// The following variables used only for Packet Downlink Assignment
	Field_z<32> mTLLI;
	Field_z<1> mRLCMode;
	Field_z<1> mTAValid;	// Is the timingadvance in the main Immediate Assignment Message valid?

	void setPacketUplinkAssignSingleBlock(unsigned TBFStartingTime);
	void setPacketUplinkAssignDynamic(unsigned TFI, unsigned CSNum, unsigned USF);
	void setPacketDownlinkAssign(
		unsigned wTLLI, unsigned wTFI,unsigned wCSNum, unsigned wRLCMode,unsigned wTAValid);
	void setPacketUplinkAssignFixed();
	void setPacketPowerOptions(unsigned wAlpha, unsigned wGamma);
	void setPacketPollTime(unsigned TBFStartingTime);
	void writePacketUplinkAssignment(MsgCommon &dest) const;
	void writePacketDownlinkAssignment(MsgCommon &dest) const; 
	void writeIAPacketAssignment(MsgCommon &dest) const;

	void writeBits(L3Frame &dest, size_t &wp) const;
	size_t lengthBits() const;
	void text(std::ostream& os) const;

	L3IAPacketAssignment() { mPacketAssignmentType = PacketUplinkAssignUninitialized; /*redundant*/ }
};



#if 0 // This is currently unused, so lets indicate so.
/** GSM 04.60 12.13 */
// NOTE: These are the power control parameters for assignment in GSM 4.60,
// not the power control parameters for the SI13 rest octets
// This is NOT a L3ProtocolElement; it is not in TLV format or byte aligned.
class L3GPRSPowerControlParameters : public GenericMessageElement
{

	private:

	unsigned mAlpha;	///< GSM 04.60 Table 12.9.2
	// GSM04.60 12.13
	// (pat) There are 8 gamma values, one for each channel.
	// sec 12.13 says the presence/absense of gamma may be used to denote
	// timeslot for "an uplink TBF", presumably in the absense of a TIMESLOT ALLOCATION IE,
	// but I dont see which uplink TBF assignment would use that and the spec does not say.
	// I dont see why you need gamma in the SI13 message at all; Gamma is assigned
	// in the uplink/downlink assignment messages as a non-optional element.
	// I am going to leave them out entirely for now.
	// unsigned mGamma[8];
	// bool mGammaPresent[8];

	public:

	// Init alpha to the defalt value.
	L3GPRSPowerControlParameters() : mAlpha(GPRS::GetPowerAlpha()) {}

	size_t lengthBits() const { return 4+8; }
	void writeBits(L3Frame& dest, size_t &wp) const;
	void text(std::ostream& os) const;
	//void parseV( const L3Frame&, size_t&, size_t) { abort(); }
	//void parseV(const L3Frame&, size_t&) { abort(); }
};
#endif

/** GSM 04.08 10.5.2.37b Power Control Parameters for SI13 Rest Octets */
// Info has moved to 44.060 12.13.
// NOTE: This is not the same as the Global Power Control Parameters
// in GSM 44.060 12.9a, which include a Pb element.
class L3GPRSSI13PowerControlParameters : public GenericMessageElement
{
	// See GSM 5.08 10.2.1
	// (pat) The MS can regulate its own output power based on measurements it makes.
	// See comments at GetPowerAlpha().  The alpha below is used for initial
	// communication and may be over-ridden later when the MS starts talking to us.
	// The other parameters are "forgetting factors" determining the window period for
	// the MS measurements.  I dont think the values (other than alpha itself)
	// are critical because they are clamped to sane values in the formulas in GSM05.08.
	unsigned mAlpha;	///< Range 0..10 See GSM 04.60 Table 12.9.2
	unsigned mTAvgW;	// The MS measurement 'forgetting factor' in Packet Idle Mode.
	unsigned mTAvgT; 	// The MS measurement 'forgetting factor' in Packet Transfer Mode.
	unsigned mPCMeasChan;		// Which channel Ms monitors: 0 => use BCCH, 1 => PDCH1
	unsigned mNAvgI;	// 'Forgetting factor' for MS reporting to BTS.
	public:
	L3GPRSSI13PowerControlParameters();
	size_t lengthBits() const { return 4+5+5+1+4; }
	void writeBits(L3Frame& dest, size_t &wp) const;
	void text(std::ostream& os) const;
};

}; // namespace
#endif
