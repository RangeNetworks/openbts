/**@file SMSCB Control (L3), GSM 03.41. */
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

#include "ControlCommon.h"
#include <GSMLogicalChannel.h>
#include <GSMConfig.h>
#include <GSMSMSCBL3Messages.h>
#include <Reporting.h>
#include <sqlite3.h>
#include <sqlite3util.h>


static const char* createSMSCBTable = {
	"CREATE TABLE IF NOT EXISTS SMSCB ("
		"GS INTEGER NOT NULL, "
		"MESSAGE_CODE INTEGER NOT NULL, "
		"UPDATE_NUMBER INTEGER NOT NULL, "
		"MSGID INTEGER NOT NULL, "
		"LANGUAGE_CODE INTEGER NOT NULL, "
		"MESSAGE TEXT NOT NULL, "
		"SEND_TIME INTEGER DEFAULT 0, "
		"SEND_COUNT INTEGER DEFAULT 0"
	")"
};



sqlite3* SMSCBConnectDatabase(const char* path, sqlite3 **DB)
{
	int rc = sqlite3_open(path,DB);
	if (rc) {
		LOG(EMERG) << "Cannot open SMSCB database on path " << path << ": " << sqlite3_errmsg(*DB);
		sqlite3_close(*DB);
		*DB = NULL;
		return NULL;
	}
	if (!sqlite3_command(*DB,createSMSCBTable)) {
		LOG(EMERG) << "Cannot create SMSCB table";
		return NULL;
	}
	// Set high-concurrency WAL mode.
	if (!sqlite3_command(*DB,enableWAL)) {
		LOG(EMERG) << "Cannot enable WAL mode on database at " << path << ", error message: " << sqlite3_errmsg(*DB);
	}
	return *DB;
}


void encode7(char mc, int &shift, unsigned int &dp, int &buf, char *thisPage)
{
	buf |= (mc & 0x7F) << shift--;
	if (shift < 0) {
		shift = 7;
	} else {
		thisPage[dp++] = buf & 0xFF;
		buf = buf >> 8;
	}
}


void SMSCBSendMessage(sqlite3* DB, sqlite3_stmt* stmt, GSM::CBCHLogicalChannel* CBCH)
{
	// Get the message parameters.
	// These column numbers need to line up with the argeuments to the SELEECT.
	unsigned GS = (unsigned)sqlite3_column_int(stmt,0);
	unsigned messageCode = (unsigned)sqlite3_column_int(stmt,1);
	unsigned updateNumber = (unsigned)sqlite3_column_int(stmt,2);
	unsigned messageID = (unsigned)sqlite3_column_int(stmt,3);
	char* messageText = strdup((const char*)sqlite3_column_text(stmt,4));
	unsigned languageCode = (unsigned)sqlite3_column_int(stmt,5);
	unsigned sendCount = (unsigned)sqlite3_column_int(stmt,6);
	unsigned rowid = (unsigned)sqlite3_column_int(stmt,7);
	// Done with the database entry.
	// Finalize ASAP to unlock the database.
	sqlite3_finalize(stmt);

	// Figure out how many pages to send.
	const unsigned maxLen = 40*15;
	unsigned messageLen = strlen((const char*)messageText);
	if (messageLen>maxLen) {
		LOG(ALERT) << "SMSCB message ID " << messageID << " to long; truncating to " << maxLen << " char.";
		messageLen = maxLen;
	}
	unsigned numPages = messageLen / 40;
	if (messageLen % 40) numPages++;
	unsigned mp = 0;

	LOG(INFO) << "sending message ID=" << messageID << " code=" << messageCode << " in " << numPages << " pages: " << messageText;

	// Break into pages and send each page.
	for (unsigned page=0; page<numPages; page++) {
		// Encode the mesage into pages.
		// We use UCS2 encoding for the message,
		// even though the input text is ASCII for now.
		char thisPage[82];
		unsigned dp = 0;
		int codingScheme;
		if (false) { // in case anybody wants to make the encoding selectable
			codingScheme = 0x11; // UCS2
			thisPage[dp++] = languageCode >> 8;
			thisPage[dp++] = languageCode & 0x0ff;
			while (dp<82 && mp<messageLen) {
				thisPage[dp++] = 0;
				thisPage[dp++] = messageText[mp++];
			}
			while (dp<82)  { thisPage[dp++] = 0; thisPage[dp++]='\r'; }
		} else {
			// 03.38 section 5
			codingScheme = 0x10; // 7'
			int buf = 0;
			int shift = 0;
			// The spec (above) says to put this language stuff in, but it doesn't work on my samsung galaxy y.
			// encode7(languageCode >> 8, shift, dp, buf, thisPage);
			// encode7(languageCode & 0xFF, shift, dp, buf, thisPage);
			// encode7('\r', shift, dp, buf, thisPage);
			while (dp<81 && mp<messageLen) {
				encode7(messageText[mp++], shift, dp, buf, thisPage);
			}
			while (dp<81)  { encode7('\r', shift, dp, buf, thisPage); }
			thisPage[dp++] = buf;
		}
		// Format the page into an L3 message.
		GSM::L3SMSCBMessage message(
			GSM::L3SMSCBSerialNumber(GS,messageCode,updateNumber),
			GSM::L3SMSCBMessageIdentifier(messageID),
			GSM::L3SMSCBDataCodingScheme(codingScheme),
			GSM::L3SMSCBPageParameter(page+1,numPages),
			GSM::L3SMSCBContent(thisPage)
		);
		// Send it.
		LOG(DEBUG) << "sending L3 message page " << page+1 << ": " << message;
		CBCH->send(message);
	}
	free(messageText);

	// Update send count and send time in the database.
	char query[100];
	sprintf(query,"UPDATE SMSCB SET SEND_TIME = %u, SEND_COUNT = %u WHERE ROWID == %u",
		(unsigned)time(NULL), sendCount+1, rowid);
	if (!sqlite3_command(DB,query)) LOG(ALERT) << "timestamp update failed: " << sqlite3_errmsg(DB);
}






void* Control::SMSCBSender(void*)
{
	// Connect to the database.
	// Just keep trying until it connects.
	sqlite3 *DB;
	while (!SMSCBConnectDatabase(gConfig.getStr("Control.SMSCB.Table").c_str(),&DB)) { sleep(1); }
	LOG(NOTICE) << "SMSCB service starting";

	// Get a channel.
	GSM::CBCHLogicalChannel* CBCH = gBTS.getCBCH();

	while (1) {
		// Get the next message ready to send.
		const char* query =
			"SELECT"
			" GS,MESSAGE_CODE,UPDATE_NUMBER,MSGID,MESSAGE,LANGUAGE_CODE,SEND_COUNT,ROWID"
			" FROM SMSCB"
			" WHERE SEND_TIME==(SELECT min(SEND_TIME) FROM SMSCB)";
		sqlite3_stmt *stmt;
		if (sqlite3_prepare_statement(DB,&stmt,query)) {
			LOG(ALERT) << "Cannot access SMSCB database: " << sqlite3_errmsg(DB);
			sleep(1);
			continue;
		}
		// Send the message or sleep briefly.
		int result = sqlite3_run_query(DB,stmt);
		if (result==SQLITE_ROW) SMSCBSendMessage(DB,stmt,CBCH);
		else sleep(1);
		// Log errors.
		if ((result!=SQLITE_ROW) && (result!=SQLITE_DONE))
			 LOG(ALERT) << "SCSCB database failure: " << sqlite3_errmsg(DB);
	}
	// keep the compiler from whining
	return NULL;
}


// vim: ts=4 sw=4
