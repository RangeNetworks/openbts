/**@file SMSCB Control (L3), GSM 03.41. */
/*
* Copyright 2010 Kestrel Signal Processing, Inc.
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

#define LOG_GROUP LogGroup::Control

#include "ControlCommon.h"
#include "CBS.h"
#include <GSMLogicalChannel.h>
#include <GSMConfig.h>
#include <GSMSMSCBL3Messages.h>
#include <Reporting.h>
#include <sqlite3.h>
#include <sqlite3util.h>

// (pat) See GSM 03.41.  SMSCB is a broadcast message service on a dedicated broadcast channel and unrelated to anything else.
// Broadcast messages are completely unacknowledged.  They are repeated perpetually.

namespace Control {

static sqlite3 *sCBSDB = NULL;


static const char* createSMSCBTable = {
	"CREATE TABLE IF NOT EXISTS SMSCB ("
		"GS INTEGER NOT NULL, "
		"MESSAGE_CODE INTEGER NOT NULL, "
		"UPDATE_NUMBER INTEGER NOT NULL, "
		"MSGID INTEGER NOT NULL, "
		"LANGUAGE_CODE INTEGER NOT NULL, "	// (pat) A 2 character string encoded as a 2 byte integer.
		"MESSAGE TEXT NOT NULL, "
		"SEND_TIME INTEGER DEFAULT 0, "
		"SEND_COUNT INTEGER DEFAULT 0"
	")"

};


static sqlite3* CBSConnectDatabase(bool whine)
{
	string path = gConfig.getStr("Control.SMSCB.Table");
	if (path.length() == 0) { return NULL; }

	if (sCBSDB) { return sCBSDB; }

	int rc = sqlite3_open(path.c_str(),&sCBSDB);
	if (rc) {
		if (whine) LOG(EMERG) << "Cannot open Cell Broadcast Service database on path " << path << ": " << sqlite3_errmsg(sCBSDB);
		sqlite3_close(sCBSDB);
		sCBSDB = NULL;
		return NULL;
	}
	if (!sqlite3_command(sCBSDB,createSMSCBTable)) {
		if (whine) LOG(EMERG) << "Cannot create Cell Broadcast Service table";
		return NULL;
	}
	// Set high-concurrency WAL mode.
	if (!sqlite3_command(sCBSDB,enableWAL)) {
		if (whine) LOG(EMERG) << "Cannot enable WAL mode on database at " << path << ", error message: " << sqlite3_errmsg(sCBSDB);
	}
	return sCBSDB;
}


static int cbsRunQuery(string query)
{
	if (!CBSConnectDatabase(true)) { return 0; }
	LOG(DEBUG) << LOGVAR(query);
	if (! sqlite_command(sCBSDB,query.c_str())) {
		LOG(INFO) << "CBS SQL query failed"<<LOGVAR(query);
		return 0;
	}
	int changes = sqlite3_changes(sCBSDB);
	return changes;
}


// The crackRowNames, crackCBMessageFromDB and CBSGetMessages must be kept matching.
// These row names match crackCBMessageFromDB.
static const char *crackRowNames = "GS,MESSAGE_CODE,UPDATE_NUMBER,MSGID,LANGUAGE_CODE,MESSAGE,SEND_COUNT,SEND_TIME,ROWID";

static void crackCBMessageFromDB(CBMessage &result, sqlite3_stmt* stmt)
{
	result.setGS((CBMessage::GeographicalScope)sqlite3_column_int(stmt,0));
	result.setMessageCode((unsigned)sqlite3_column_int(stmt,1));
	result.setUpdateNumber((unsigned)sqlite3_column_int(stmt,2));
	result.setMessageId((unsigned)sqlite3_column_int(stmt,3));
	result.setLanguageCode((unsigned)sqlite3_column_int(stmt,4));
	result.setMessageText(string((const char*)sqlite3_column_text(stmt,5)));
	result.mSendCount = (unsigned)sqlite3_column_int(stmt,6);
	result.mSendTime = (unsigned)sqlite3_column_int(stmt,7);
	result.mRowId = (unsigned)sqlite3_column_int(stmt,8);
}

// Return false on error;  return true on success, which means we accessed the table - the result size is the number of entries.
bool CBSGetMessages(vector<CBMessage> &result, string text)
{
	if (!CBSConnectDatabase(true)) { return false; }
	result.clear();
	sqlite3_stmt *stmt = NULL;
	string query = format("SELECT %s FROM SMSCB ",crackRowNames);
	if (text.size()) {
		query += format("WHERE MESSAGE=='%s'",text);
	}
	int rc;
	if ((rc = sqlite3_prepare_statement(sCBSDB,&stmt,query.c_str()))) {
		LOG(DEBUG) << "sqlite3_prepare_statement failed code="<<rc;
		return false;
	}
	while (SQLITE_ROW == (rc=sqlite3_run_query(sCBSDB,stmt))) {
		CBMessage msg;
		crackCBMessageFromDB(msg, stmt);
		result.push_back(msg);
		LOG(DEBUG) <<LOGVAR(rc) <<LOGVAR(msg.cbtext());
	}
	sqlite3_finalize(stmt); // Finalize ASAP to unlock the database.
	LOG(DEBUG) <<"final"<<LOGVAR(rc);
	return true;
}

int CBSClearMessages()
{
	return cbsRunQuery("DELETE FROM SMSCB WHERE 1");
}

static string strJoin(vector<string> &fields,string separator)
{
	string result;
	int cnt = 0;
	for (vector<string>::iterator it = fields.begin(); it != fields.end(); it++, cnt++) {
		if (cnt) result.append(separator);
		result.append(*it);
	}
	return result;
}

static void update1field(const char *col, unsigned uval, vector<string>*cols, vector<string>*vals, vector<string>*both)
{
	if (cols) { cols->push_back(col); }
	if (vals) { vals->push_back(format("%u",uval)); }
	if (both) { both->push_back(format("%s=%u",col,uval)); }
}

static void update1field(const char *col, string sval, vector<string>*cols, vector<string>*vals, vector<string>*both)
{
	if (cols) { cols->push_back(col); }
	if (vals) { vals->push_back(format("'%s'",sval)); }
	if (both) { both->push_back(format("%s='%s'",col,sval)); }
}

// The all flag is for INSERT which must update all the DB fields with the "NOT NULL" option. Oops.
static void CBMessage2SQLFields(CBMessage &msg, vector<string>*cols, vector<string>*vals, vector<string>*both, bool all)
{
	if (all || msg.mGS_change) { update1field("GS",msg.mGS,cols,vals,both); }
	if (all || msg.mMessageCode_change) { update1field("MESSAGE_CODE",msg.mMessageCode,cols,vals,both); }
	if (all || msg.mUpdateNumber_change) { update1field("UPDATE_NUMBER",msg.mUpdateNumber,cols,vals,both); }
	if (all || msg.mMessageId_change) { update1field("MSGID",msg.mMessageId,cols,vals,both); }
	if (all || msg.mLanguageCode_change) { update1field("LANGUAGE_CODE",msg.mLanguageCode,cols,vals,both); }
	//if (all || msg.mLanguage_change) { update1field("LANGUAGE_CODE",msg.getLanguageCode(),cols,vals,both); }
	if (all || msg.mMessageText.size()) { update1field("MESSAGE",msg.mMessageText,cols,vals,both); }
	// ROWID is a synthetic field.
	if (msg.mRowId_change) { update1field("ROWID",msg.mRowId,cols,vals,both); }
}

// Deletes all messages that match msg, which must have at least one field set.
int CBSDeleteMessage(CBMessage &msg)
{
	vector<string> fields;
	CBMessage2SQLFields(msg,NULL,NULL,&fields,false);
	if (fields.size()) {
		string query = format("DELETE FROM SMSCB WHERE %s",strJoin(fields,","));
		return cbsRunQuery(query);
	} else {
		return 0;	// If the CBMessage contained no fields, dont delete all messages, just return 0.
	}
}


int CBSAddMessage(CBMessage &msg, string &errorMsg)
{
	if (msg.mMessageText.size() == 0) {
		errorMsg = string("Attempt to add message with no text");
		return 0;
	}
	if (!CBSConnectDatabase(true)) {
		errorMsg = string("could not write to database");
		return 0;
	}


	// Does the message exist already?
	vector<CBMessage> existing;
	CBSGetMessages(existing,msg.mMessageText);
	for (vector<CBMessage>::iterator it = existing.begin(); it != existing.end(); it++) {
		if (msg.match(*it)) {
			errorMsg = string("Attempt to add duplicate message");
			return 0;
		}
	}

	// TODO: If the message matches an existing we should increment the update_number.

	// like this: INSERT OR REPLACE INTO SMSCB (GS,MESSAGE_CODE,UPDATE_NUMBER,MSGID,LANGUAGE_CODE,MESSAGE) VALUES (0,0,0,0,0,'whatever')
	// Cannot use REPLACE: REPLACE only works if INSERT would generate a constraint conflict, ie, duplicate UNIQ field.
	vector<string> cols, vals;
	// We must update all the fields with the history "NOT NULL" value in the database.  Oops.
	CBMessage2SQLFields(msg, &cols, &vals, NULL,true);
	string query = format("INSERT INTO SMSCB (%s) VALUES (%s)",strJoin(cols,","),strJoin(vals,","));
	return cbsRunQuery(query);
}


static void encode7(char mc, int &shift, unsigned int &dp, int &buf, char *thisPage)
{
	buf |= (mc & 0x7F) << shift--;
	if (shift < 0) {
		shift = 7;
	} else {
		thisPage[dp++] = buf & 0xFF;
		buf = buf >> 8;
	}
}


// (pat 8-2014) I added the CBMessage class and added CLI cbscmd to manipulate the database,
// but I did not change the basic encoding and transmit logic below nor did I enable the language option.
static void CBSSendMessage(CBMessage &msg, GSM::CBCHLogicalChannel* CBCH)
{
	// Figure out how many pages to send.
	const unsigned maxLen = 40*15;
	unsigned messageLen = msg.mMessageText.length();
	if (messageLen>maxLen) {
		LOG(ALERT) << "SMSCB message ID " << msg.mMessageId << " to long; truncating to " << maxLen << " char.";
		messageLen = maxLen;
	}
	unsigned numPages = messageLen / 40;
	if (messageLen % 40) numPages++;
	unsigned mp = 0;

	LOG(INFO) << "sending message ID=" << msg.mMessageId << " code=" << msg.mMessageCode << " in " << numPages << " pages: " << msg.mMessageText;

	// Break into pages and send each page.
	for (unsigned page=0; page<numPages; page++) {
		// Encode the mesage into pages.
		// We use UCS2 encoding for the message,
		// even though the input text is ASCII for now.
		char thisPage[82];
		unsigned dp = 0;
		int codingScheme;
		// (pat) If we want to implement languages we should support DCS of GSM 3.38 first, not UCS2.
		if (false && msg.mLanguageCode) {
			codingScheme = 0x11; // UCS2
			thisPage[dp++] = msg.mLanguageCode >> 8;
			thisPage[dp++] = msg.mLanguageCode & 0x0ff;
			while (dp<82 && mp<messageLen) {
				// UCS2 uses 16-bit characters.
				// (pat) Setting the high byte to 0 is just wrong - the user would want to put the 16-bit characters in the database,
				// that is the point of using UCS2.
				thisPage[dp++] = 0;
				thisPage[dp++] = msg.mMessageText[mp++];
			}
			while (dp<82)  { thisPage[dp++] = 0; thisPage[dp++]='\r'; }
		} else {
			// 03.38 section 5
			codingScheme = 0x10; // 'default' codiing scheme
			int buf = 0;
			int shift = 0;
			// The spec (above) says to put this language stuff in, but it doesn't work on my samsung galaxy y.  (dbrown)
			// encode7(languageCode >> 8, shift, dp, buf, thisPage);
			// encode7(languageCode & 0xFF, shift, dp, buf, thisPage);
			// encode7('\r', shift, dp, buf, thisPage);
			while (dp<81 && mp<messageLen) {
				encode7(msg.mMessageText[mp++], shift, dp, buf, thisPage);
			}
			while (dp<81)  { encode7('\r', shift, dp, buf, thisPage); }
			thisPage[dp++] = buf;
		}
		// Format the page into an L3 message.
		GSM::L3SMSCBMessage message(
			GSM::L3SMSCBSerialNumber(msg.mGS,msg.mMessageCode,msg.mUpdateNumber),
			GSM::L3SMSCBMessageIdentifier(msg.mMessageId),
			GSM::L3SMSCBDataCodingScheme(codingScheme),
			GSM::L3SMSCBPageParameter(page+1,numPages),
			GSM::L3SMSCBContent(thisPage)
		);
		// Send it.
		LOG(DEBUG) << "sending L3 message page " << page+1 << ": " << message;
		CBCH->l2sendm(message);
	}
}


string CBMessage::cbtext()
{
	ostringstream os;
	os <<LOGVARM(mGS)<<LOGVARM(mMessageCode)<<LOGVARM(mUpdateNumber)<<LOGVARM(mMessageId)<<LOGVAR(mMessageText);
	return os.str();
}

void CBMessage::cbtext(std::ostream &os)
{
	os <<cbtext();
}


// (pat) The SMSCB name is misleading; this has nothing to do with SMS.  It is Cell Broadcast Service.
void* SMSCBSender(void*)
{
	// Connect to the database.
	// Just keep trying until it connects.
	bool whine = true;
	while (!CBSConnectDatabase(whine)) { sleep(2); whine = false; }
	LOG(NOTICE) << "SMSCB service starting";

	// Get a channel.
	GSM::CBCHLogicalChannel* CBCH = gBTS.getCBCH();

	while (1) {
		// Get the next message ready to send.
		// (pat) The "ROWID" is not a column name, it is sql-lite magic to return the row number.
		int sqlresult;
		CBMessage msg;
		{
			string query = format("SELECT %s FROM SMSCB WHERE SEND_TIME==(SELECT min(SEND_TIME) FROM SMSCB)",crackRowNames);
			sqlite3_stmt *stmt;
			if (sqlite3_prepare_statement(sCBSDB,&stmt,query.c_str())) {
				LOG(ALERT) << "Cannot access SMSCB database: " << sqlite3_errmsg(sCBSDB);
				sleep(1);
				continue;
			}
			// Send the message or sleep briefly.
			sqlresult = sqlite3_run_query(sCBSDB,stmt);
			if (sqlresult == SQLITE_ROW) {
				crackCBMessageFromDB(msg, stmt);
			}
			// Finalize ASAP to unlock the database.
			sqlite3_finalize(stmt);
		}
		LOG(DEBUG) <<LOGVAR(sqlresult) <<LOGVAR(msg.cbtext());

		switch (sqlresult) {
			case SQLITE_ROW: {
				CBSSendMessage(msg,CBCH);
				// Update send count and send time in the database.
				char query[100];
				snprintf(query,100,"UPDATE SMSCB SET SEND_TIME = %u, SEND_COUNT = %u WHERE ROWID == %u",
					(unsigned)time(NULL), msg.mSendCount+1, (unsigned)msg.mRowId);
				if (!sqlite3_command(sCBSDB,query)) LOG(ALERT) << "SMSCB database timestamp update failed: " << sqlite3_errmsg(sCBSDB);
				continue;
			}
			case SQLITE_DONE:
				// Empty database.
				break;
			default:
				LOG(ALERT) << "SCSCB database failure: " << sqlite3_errmsg(sCBSDB);
				break;
		}
		sleep(1);
	}
	// keep the compiler from whining
	return NULL;
}

};	// namespace

// vim: ts=4 sw=4
