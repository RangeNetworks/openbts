/**@file Declarations for common-use control-layer functions. */
/*
* Copyright 2013 Range Networks, Inc.
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
#ifndef CONTROLTRANSFER_H
#define CONTROLTRANSFER_H
#include <stdint.h>
#include <string>
#include <vector>
#include <assert.h>
#include <GSML3CommonElements.h>

namespace SIP { class DialogMessage; };
namespace GSM { class L3Frame; class L2LogicalChannel; }

extern int gCountTranEntry;

namespace Control {
using namespace std;
class TranEntry;
class HandoverEntry;
class TransactionEntry;
class L3LogicalChannel;
typedef unsigned TranEntryId;
typedef vector<TranEntryId> TranEntryList;

extern bool l3rewrite();
extern void l3start();
extern void controlInit();
extern unsigned allocateRTPPorts();

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

class TMSI_t {
	bool mValid;
	uint32_t mVal;
	public:
	TMSI_t() : mValid(false) {}
	TMSI_t(uint32_t wVal) : mValid(true), mVal(wVal) {}
	TMSI_t &operator=(uint32_t wVal) { mValid=true; mVal = wVal; return *this; }
	TMSI_t &operator=(const TMSI_t &other) { mVal=other.mVal; mValid=other.mValid; return *this; }
	bool valid() const { return mValid; }
	uint32_t value() const { assert(valid()); return mVal; }
};
std::ostream& operator<<(std::ostream& os, const TMSI_t&tmsi);

struct FullMobileId {
	string mImsi;
	TMSI_t mTmsi;
	string mImei;
	string fmidUsername() const;	// "IMSI" or "TMSI" or "IMEI" + digits.
	bool fmidMatch(const GSM::L3MobileIdentity &mobileId) const;
	void fmidSet(string value);
	FullMobileId() {}	// Nothing needed.
	FullMobileId(const string wAnything) { fmidSet(wAnything); }		// Default is an imsi.
};
std::ostream& operator<<(std::ostream& os, const FullMobileId&msid);

/** Call states based on GSM 04.08 5 and ITU-T Q.931 */
// (pat) These are the states in 4.08 defined in 5.1.2.2 used in procedures described in 5.2.
// 5.1.1 has a picture of the state machine including these states.
// The "Call State" numeric values are defined in 10.5.4.6
// The network side states are N-numbers, and UE side are U-numbers, but with the same numeric values.
struct CCState {
	enum CallState {
		NullState = 0,
		Paging = 2,				// state N0.1 aka MTC MMConnectionPending
		// AnsweredPaging is not a CallControl state.
		// AnsweredPaging = 100,		// Not a GSM Call Control state.  Intermediate state used by OpenBTS between Paging and CallPresent
		MOCInitiated = 1,	// state N1 "Call initiated".
				// 5.1.2.1.3 specifies state U1 in MS for MOC entered when MS requests "call establishment".
				// 4.1.2.2.3 specifies state N1 in network received "call establishment request" but has not responded.
				// Since these are CC states, we previously assumed that "call establishment" meant
				// an L3Setup message, not CM Service Request.
				// However, 24.008 11.3 implies that "CallInitiated" state starts when CM Service Request is sent.
				// The optional authorization procedure intervenes between receipt of CM Service Request and sending the Accept.
		MOCProceeding = 3,	// state N3.  network sent L3CallProceeding in response to Setup or EmergencySetup.
		MOCDelivered = 4,	// N4. MOC network sent L3Alerting.  Not used in pre-l3rewrite code.
		CallPresent = 6,	// N6. MTC network sent L3Setup, started T303, waiting for L3CallConfirmed.
		CallReceived = 7,	// N7. MTC network recv L3CallAlerting.  We use it in MOC to indicate SIP active.
		// ConnectRequest = 8 // N8.  We do not use.
		MTCConfirmed = 9,	// N9. network received L3CallConfirmed.
		Active = 10,			// N10. MOC: network received L3ConnectAcknowledge, MTC: network sent L3ConnectAcknowledge
		DisconnectIndication = 12, // N12: Network sent a disconnect
		// There is a DisconnectRequest state in the MS, but not in the network.
		// MTCModify = 27, // N27 not used
		ReleaseRequest = 19 ,	// N19: Network sent a Release message (per 24.008 5.4.2).
		ConnectIndication = 28,	// N28.  MOC network sent L3Connect, start T313

		// (pat) These are NOT call control states, but we use the CallState for all types of TransactionEntry.
		SMSDelivering = 101,		// MT-SMS set when paging answered; MT-SMS initial TransactionEntry state is NullState.
		SMSSubmitting = 102,		// MO-SMS TransactionEntry initial state.

		// (pat) These seem to be call control states to me, but they are not defined
		// for the 10.5.4.6 "Call State" IE, so I am just making up values for them:
		HandoverInbound = 103,		// TransactionEntry initial state for inbound handover.
		HandoverProgress = 104,
		HandoverOutbound = 105,
		//BusyReject,		// pat removed, not used
	};
	static const char* callStateString(CallState state);
	static bool isInCall(CallState state);
};
typedef CCState::CallState CallState;


// This is the reason a Transaction (TranEntry) was cancelled as desired to be known by the high side.
// It has nothing to do with the cancel causes on the low side, for example, CC Cause (for cloasing a single call)
// or RR Cause (for closing an entire channel.)
// An established SipDialog is ended by a SIP BYE, and an MO [Mobile Originated] SipDialog is canceled early using
// a SIP CANCEL, so this is used only for the case of an INVITE response where the ACK message has not been sent,
// or as a non-invite message (eg, SMS MESSAGE) error response.  As such, there are only a few codes that
// the SIP side cares about.  The vast majority of plain old errors, for example, loss of contact with the MS
// or reassignFailure will just map to the same SIP code so we use CancelCauseUnknown, however, all such cases
// are distinguished from CancelCauseNoAnswerToPage in that we know the MS is on the current system.
// we just .
enum CancelCause {
	// Used for anything other than the specific causes below.  It is not "unknown" so much as we just
	// dont need to distinguish among various failure or hangup causes because we send the same SIP code for them all.
	CancelCauseUnknown = 0,		// Not completely unknown - we know that the MS was on this system.
	CancelCauseNoAnswerToPage,	// We dont have any clue if the MS is in this area or not.
	CancelCauseBusy,			// The MS is here.  A future call may succeed.
	CancelCauseCongestion,			// The MS is here, but no resources.  A future call may succeed.
	CancelCauseHandoverOutbound,	// A special case - the Dialog has been moved elsewhere.
	CancelCauseSipInternalError,	// Special case of the SipDialog itself being internally inconsistent.
	CancelCauseOperatorIntervention,	// Killed from console.
};


/** Return a human-readable string for a GSM::CallState. */
const char* CallStateString(CallState state);

std::ostream& operator<<(std::ostream& os, CallState state);

#if UNUSED_BUT_SAVE_FOR_UMTS
// A message to the CS L3 state machine.  The message may come from a GSM LogicalChannel (FACCH, SDCCH, or SACCH), GPRS, or SIP.
// This is part of the L3 rewrite.
class GenericL3Msg {
	public:
	enum GenericL3MsgType {
		MsgTypeLCH,
		MsgTypeSIP
	};
	enum GenericL3MsgType ml3Type;
	const char *typeName();
	GSM::L3Frame *ml3frame;
	GSM::L2LogicalChannel *ml3ch;
	SIP::DialogMessage *mSipMsg;
	const std::string mCallId;	// TODO: Now unused, remove.

	//GenericL3Msg(GSM::L3Frame *wFrame, L3LogicalChannel *wChan) : ml3Type(MsgTypeLCH), ml3frame(wFrame),ml3ch(dynamic_cast<L3LogicalChannel*>(wChan)),mSipMsg(0) { assert(ml3frame); }
	GenericL3Msg(GSM::L3Frame *wFrame, GSM::L2LogicalChannel *wChan) : ml3Type(MsgTypeLCH), ml3frame(wFrame),ml3ch(wChan),mSipMsg(0) { assert(ml3frame); }
	GenericL3Msg(SIP::DialogMessage *wSipMsg, std::string wCallId) : ml3Type(MsgTypeSIP), ml3frame(0),ml3ch(0), mSipMsg(wSipMsg), mCallId(wCallId) { assert(mSipMsg); }
	~GenericL3Msg();
};
#endif


void NewTransactionTable_ttAddMessage(TranEntryId tranid,SIP::DialogMessage *dmsg);

};
#endif
