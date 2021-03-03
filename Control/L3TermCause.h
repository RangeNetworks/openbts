/**@file Declarations for common-use control-layer functions. */
/*
* Copyright 2013 Range Networks, Inc.
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

#ifndef _TERMCAUSE_H_
#define _TERMCAUSE_H_
#include <assert.h>
#include <string>

#include <L3Enums.h>
#include <ScalarTypes.h>	// for Int_z, From CommonLibs
#include <Logger.h>

namespace SIP { class SipDialog; }

namespace Control {
using std::string;
using GSM::L3Cause;


// This is the reason a Transaction (TranEntry) was cancelled as desired to be known by the high side.
// It has nothing to do with the cancel causes on the low side, for example, CC Cause (for cloasing a single call)
// or RR Cause (for closing an entire channel.)
// Why cant we just use CC or RR Cause?  Because some of the reasons do not exist in any other single list, for example, NoAnswerToPage.
// An established SipDialog is ended by a SIP BYE, and an MO [Mobile Originated] SipDialog is canceled early using
// a SIP CANCEL, so this is used only for the case of an INVITE response where the ACK message has not been sent,
// or as a non-invite message (eg, SMS MESSAGE) error response.  As such, there are only a few codes that
// the SIP side cares about.  The vast majority of plain old errors, for example, loss of contact with the MS
// or reassignFailure will just map to the same SIP code so we use TermCauseUnknown, however, all such cases
// are distinguished from TermCauseNoAnswerToPage in that we know the MS is on the current system.

// pat added 6-2014, so every client that closes a transaction is forced to explicitly specify all needed cancellation information.
class TermCause {
	L3Cause::AnyCause mtcAnyCause;	// The full extended cause.  Self-inits to 0.
	Int_z mtcSipCode;
	string mtcSipReason;
	public:
	enum Side { SideLocal, SideRemote } mtcInstigator;	// Which side closed the channel?

	int tcGetValue() { return mtcAnyCause.value; }
	string tcGetStr();
	bool tcIsEmpty() { return mtcAnyCause.isEmpty(); }

	L3Cause::CCCause tcGetCCCause();	// Returns the nearest Call-Control-Cause as per GSM 4.08 10.5.4.11.
	int tcGetSipCodeAndReason(string &sipreason);	// Returns the nearest SIP code, and the sip reason phrase derived from the real cause.
	string tcGetSipReasonHeader();		// Returns a SIP "Reason:" header string.

	// constructors:
	TermCause() { assert(mtcAnyCause.value == 0 && mtcSipCode == 0); }

	// This constructor is used to terminate a transaction by this BTS.
	// The TermCode is translated as needed to an L3Cause, SIP code, and SIP reason.
	//TermCause(Side side,TermCode code);
	static TermCause Local(L3Cause::AnyCause cause) {
		TermCause self;
		self.mtcInstigator = SideLocal;
		self.mtcAnyCause = cause;
		self.mtcSipCode = 0;
		LOG(DEBUG)<<self;
		return self;
	}

	// This constructor used to terminate a transaction from peer BTS via SIP.
	// This SIP code is saved here so we can put it the CDR, but will not be sent outbound to a dialog.
	static TermCause Remote(GSM::AnyCause wCause, int wSipCode, string wSipReason)
		{
			TermCause self;
			self.mtcInstigator = SideRemote;
			self.mtcAnyCause = wCause;
			self.mtcSipCode = wSipCode;
			self.mtcSipReason = wSipReason;
			LOG(DEBUG)<<self;
			return self;
		}
	friend std::ostream& operator<<(std::ostream& os, TermCause &cause);
};
std::ostream& operator<<(std::ostream& os, TermCause &cause);

extern TermCause dialog2TermCause(SIP::SipDialog *dialog);
extern TermCause dialog2ByeCause(SIP::SipDialog *dialog);

};	// namespace Control

#endif
