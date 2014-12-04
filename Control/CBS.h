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

#ifndef _SMSCB_H_
#define _SMSCB_H_ 1
#include <vector>
#include <ostream>

namespace Control {


// Short Message Cell Broadcast Service
// GSM 3.41 9.3.2
// See also WEA = Wireless Emergency Alert services on the internet.
// See discussion on wiki ticket #1281.
// Note that USSD is another way to show messages directly on a handset.
struct CBMessage {
	// The handset displays the message if it has not seen it before within the specified Geographical Scope.
	enum GeographicalScope {
		GS_Immediate = 0,
		GS_PLMN = 1,
		GS_LocationArea = 2,
		GS_Cell = 3,
	};
	GeographicalScope mGS;	// Geographical Scope: 
	Bool_z mGS_change;
	bool setGS(int gs) {	// Return false if out of range.
		if (gs < 0 || gs > 3) { mGS = GS_Immediate; return false; }
		mGS = (GeographicalScope) gs;
		mGS_change = true;
		return true;
	}

	UInt_z mMessageCode;		// Unique id for message.
	Bool_z mMessageCode_change;
	bool setMessageCode(unsigned mc) { mMessageCode = mc; mMessageCode_change = true; return true; }

	UInt_z mUpdateNumber;		// Another unique id for message.
	Bool_z mUpdateNumber_change;
	bool setUpdateNumber(unsigned un) { mUpdateNumber = un; mUpdateNumber_change = true; return true; }

	// The message id identifies the message type (bad name.)
	// In particular, values above 0x3e7 (where did they get that?) are reserved.
	// The only interesting one is 0x3e9 GPS Assistance data.
	UInt_z mMessageId;			// Yet another unique id for the message.
	Bool_z mMessageId_change;
	bool setMessageId(unsigned id) { mMessageId = id; mMessageId_change = true; return true; }
		
	// Language code is not implemented.
	// This should be Data Coding Scheme, not 'language code'.

	// See GSM 3.38 section 5.
	// (pat) The historical SMSCB database has an unsigned "language_code" entry that is not implemented.
	// But there are many different ways to specify and represent the lanugage:
	// 1.  Using the Data Coding Scheme as per GSM 3.38 section 5;
	// 2.  By preceding the default encoding with 2 characters;
	// 3.  Using UCS2, which uses 16 bit characters.
	// The character encoding in the message can use 7-bit, 8-bit, 16-bit, or 7-bit+extension-characters.
	// Unfortunately, DCS == 0 is a valid value (german), so I'm not even sure how we should store the language code,
	// but probably need both the DCS and the language code, which would require a CBS database update, which would
	// mean we would have to check for and handle older revs of the database, so I am punting this.
	// So here is some code to interface to the SMSCB database, but it doesnt do anything.
	UInt_z mLanguageCode;			// 2 ascii chars encoded as a short word.
	Bool_z mLanguageCode_change;
	bool setLanguageCode(unsigned lc) { mLanguageCode = lc; mLanguageCode_change = true; return true; }
	bool setLanguage(string lc) {
		if (lc.size() == 0 || lc == "-") {
			return setLanguageCode(0);
		} else if (lc.size() == 2) {
			return setLanguageCode(((unsigned)lc[1] << 8) | lc[0]);
		} else {
			return false;
		}
	}
	string getLanguage() {
		// If mLanguageCode is 0 it will return ""
		char buf[3]; buf[0] = mLanguageCode & 0x7f; buf[1] = (mLanguageCode>>8)&0x7f; buf[2] = 0;
		return string(buf);
	}

	string mMessageText;	// The message itself.
	bool setMessageText(string mt) { mMessageText = mt; return true; }

	bool match(CBMessage &other) {
		return mGS == other.mGS && mMessageCode == other.mMessageCode && mUpdateNumber == other.mUpdateNumber &&
			mMessageId == other.mMessageId && mLanguageCode == other.mLanguageCode && mMessageText == other.mMessageText;
	}

	// These are OpenBTS specific fields:
	UInt_z mSendCount;
	time_t mSendTime;
	UInt_z mRowId;	// Identifies this message in our database.
	Bool_z mRowId_change;
	bool setRow(int row) { mRowId = row; mRowId_change = true; return true; }

	CBMessage() : mGS(GS_Immediate), mSendTime(0) {}

	string cbtext();
	void cbtext(std::ostream &os);
};

int CBSClearMessages();
int CBSDeleteMessage(CBMessage &msg);
int CBSAddMessage(CBMessage &msg, string &errMsg);
bool CBSGetMessages(vector<CBMessage> &result, string text="");

};	// namespace

#endif
