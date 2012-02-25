/*
* Copyright 2011 Range Networks, Inc.
* Copyright 2011 Free Software Foundation, Inc.
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

#include "SubscriberRegistry.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "sqlite3.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <Configuration.h>

extern ConfigurationTable gConfig;


using namespace std;

static const char* createRRLPTable = {
    "CREATE TABLE IF NOT EXISTS RRLP ("
		"id				INTEGER PRIMARY KEY, "
		"name           VARCHAR(80) not null, "
		"latitude       real not null, "
		"longitude      real not null, "
		"error          real not null, "
		"time           text not null "
    ")"
};

static const char* createDDTable = {
    "CREATE TABLE IF NOT EXISTS DIALDATA_TABLE ("
		"id				INTEGER PRIMARY KEY, "
		"exten           VARCHAR(40)     NOT NULL        DEFAULT '', "
		"dial			VARCHAR(128)    NOT NULL        DEFAULT '' "
    ")"
};

static const char* createSBTable = {
    "CREATE TABLE IF NOT EXISTS SIP_BUDDIES ("
		"id                    integer primary key, "
		"name                  VARCHAR(80) not null, "
		"context               VARCHAR(80), "
		"callingpres           VARCHAR(30) DEFAULT 'allowed_not_screened', "
		"deny                  VARCHAR(95), "
		"permit                VARCHAR(95), "
		"secret                VARCHAR(80), "
		"md5secret             VARCHAR(80), "
		"remotesecret          VARCHAR(250), "
		"transport             VARCHAR(10), "
		"host                  VARCHAR(31) default '' not null, "
		"nat                   VARCHAR(5) DEFAULT 'no' not null, "
		"type                  VARCHAR(10) DEFAULT 'friend' not null, "
		"accountcode           VARCHAR(20), "
		"amaflags              VARCHAR(13), "
		"callgroup             VARCHAR(10), "
		"callerid              VARCHAR(80), "
		"defaultip             VARCHAR(40) DEFAULT '0.0.0.0', "
		"dtmfmode              VARCHAR(7) DEFAULT 'rfc2833', "
		"fromuser              VARCHAR(80), "
		"fromdomain            VARCHAR(80), "
		"insecure              VARCHAR(4), "
		"language              CHAR(2), "
		"mailbox               VARCHAR(50), "
		"pickupgroup           VARCHAR(10), "
		"qualify               CHAR(3), "
		"regexten              VARCHAR(80), "
		"rtptimeout            CHAR(3), "
		"rtpholdtimeout        CHAR(3), "
		"setvar                VARCHAR(100), "
		"disallow              VARCHAR(100) DEFAULT 'all', "
		"allow                 VARCHAR(100) DEFAULT 'g729;ilbc;gsm;ulaw;alaw' not null, "
		"fullcontact           VARCHAR(80), "
		"ipaddr                VARCHAR(45), "
		"port                  int(5) DEFAULT 0, "
		"username              VARCHAR(80), "
		"defaultuser           VARCHAR(80), "
		"subscribecontext      VARCHAR(80), "
		"directmedia           VARCHAR(3), "
		"trustrpid             VARCHAR(3), "
		"sendrpid              VARCHAR(3), "
		"progressinband        VARCHAR(5), "
		"promiscredir          VARCHAR(3), "
		"useclientcode         VARCHAR(3), "
		"callcounter           VARCHAR(3), "
		"busylevel             int(11), "
		"allowoverlap          VARCHAR(3) DEFAULT 'yes', "
		"allowsubscribe        VARCHAR(3) DEFAULT 'yes', "
		"allowtransfer         VARCHAR(3) DEFAULT 'yes', "
		"ignoresdpversion      VARCHAR(3) DEFAULT 'no', "
		"template              VARCHAR(100), "
		"videosupport          VARCHAR(6) DEFAULT 'no', "
		"maxcallbitrate        int(11), "
		"rfc2833compensate     VARCHAR(3) DEFAULT 'yes', "
		"'session-timers'      VARCHAR(10) DEFAULT 'accept', "
		"'session-expires'     int(6) DEFAULT 1800, "
		"'session-minse'       int(6) DEFAULT 90, "
		"'session-refresher'   VARCHAR(3) DEFAULT 'uas', "
		"t38pt_usertpsource    VARCHAR(3), "
		"outboundproxy         VARCHAR(250), "
		"callbackextension     VARCHAR(250), "
		"registertrying        VARCHAR(3) DEFAULT 'yes', "
		"timert1               int(6) DEFAULT 500, "
		"timerb                int(9), "
		"qualifyfreq           int(6) DEFAULT 120, "
		"contactpermit         VARCHAR(250), "
		"contactdeny           VARCHAR(250), "
		"lastms                int(11) DEFAULT 0 not null, "
		"regserver             VARCHAR(100), "
		"regseconds            int(11) DEFAULT 0 not null, "
		"useragent             VARCHAR(100), "
		"cancallforward        CHAR(3) DEFAULT 'yes' not null, "
		"canreinvite           CHAR(3) DEFAULT 'yes' not null, "
		"mask                  VARCHAR(95), "
		"musiconhold           VARCHAR(100), "
		"restrictcid           CHAR(3), "
		"calllimit             int(5), "
		"WhiteListFlag         timestamp not null default '0', "
		"WhiteListCode         varchar(8) not null default '0', "
		"rand                  varchar(33) default '', "
		"sres                  varchar(33) default '', "
		"ki                    varchar(33) default '', "
		"kc                    varchar(33) default '', "
		"RRLPSupported         int(1) default 1 not null, "
  		"hardware              VARCHAR(20), " 
		"regTime               INTEGER default 0 NOT NULL," // Unix time of most recent registration
		"a3_a8                 varchar(45) default NULL"
    ")"
};


int SubscriberRegistry::init()
{
	string ldb = gConfig.getStr("SubscriberRegistry.db");
	int rc = sqlite3_open(ldb.c_str(),&mDB);
	if (rc) {
		LOG(EMERG) << "Cannot open SubscriberRegistry database: " << sqlite3_errmsg(mDB);
		sqlite3_close(mDB);
		mDB = NULL;
		return FAILURE;
	}
	if (!sqlite3_command(mDB,createRRLPTable)) {
		LOG(EMERG) << "Cannot create RRLP table";
	  return FAILURE;
	}
	if (!sqlite3_command(mDB,createDDTable)) {
		LOG(EMERG) << "Cannot create DIALDATA_TABLE table";
		return FAILURE;
	}
	if (!sqlite3_command(mDB,createSBTable)) {
		LOG(EMERG) << "Cannot create SIP_BUDDIES table";
		return FAILURE;
	}
	return SUCCESS;
}



SubscriberRegistry::~SubscriberRegistry()
{
	if (mDB) sqlite3_close(mDB);
}




SubscriberRegistry::Status SubscriberRegistry::sqlLocal(const char* query, char **resultptr)
{
	LOG(INFO) << query;

	if (!resultptr) {
		if (!sqlite3_command(db(), query)) return FAILURE;
		return SUCCESS;
	}

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(db(), &stmt, query)) {
		LOG(ERR) << "sqlite3_prepare_statement problem";
		return FAILURE;
	}
	int src = sqlite3_run_query(db(), stmt);
	if (src != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return FAILURE;
	}
	char *column = (char*)sqlite3_column_text(stmt, 0);
	if (!column) {
		LOG(ERR) << "Subscriber registry returned a NULL column.";
		sqlite3_finalize(stmt);
		return FAILURE;
	}
	*resultptr = strdup(column);
	sqlite3_finalize(stmt);
	return SUCCESS;
}



string SubscriberRegistry::sqlQuery(string unknownColumn, string table, string knownColumn, string knownValue)
{
	char *result = NULL;
	SubscriberRegistry::Status st;
	ostringstream os;
	os << "select " << unknownColumn << " from " << table << " where " << knownColumn << " = \"" << knownValue << "\"";
	st = sqlLocal(os.str().c_str(), &result);
	if ((st == SUCCESS) && result) {
		LOG(INFO) << "result = " << result;
		return result;
	} else {
		return "";
	}
}



SubscriberRegistry::Status SubscriberRegistry::sqlUpdate(string stmt)
{
	LOG(INFO) << stmt;
	return sqlLocal(stmt.c_str(), NULL);
}

string SubscriberRegistry::imsiGet(string imsi, string key)
{
	string name = imsi.substr(0,4) == "IMSI" ? imsi : "IMSI" + imsi;
	return sqlQuery(key, "sip_buddies", "name", imsi);
}

SubscriberRegistry::Status SubscriberRegistry::imsiSet(string imsi, string key, string value)
{
	string name = imsi.substr(0,4) == "IMSI" ? imsi : "IMSI" + imsi;
	ostringstream os;
	os << "update sip_buddies set " << key << " = \"" << value << "\" where name = \"" << name << "\"";
	return sqlUpdate(os.str());
}

string SubscriberRegistry::getIMSI(string ISDN)
{
	if (ISDN.empty()) {
		LOG(WARNING) << "SubscriberRegistry::getIMSI attempting lookup of NULL ISDN";
		return "";
	}
	LOG(INFO) << "getIMSI(" << ISDN << ")";
	return sqlQuery("dial", "dialdata_table", "exten", ISDN);
}



string SubscriberRegistry::getCLIDLocal(string IMSI)
{
	if (IMSI.empty()) {
		LOG(WARNING) << "SubscriberRegistry::getCLIDLocal attempting lookup of NULL IMSI";
		return "";
	}
	LOG(INFO) << "getCLIDLocal(" << IMSI << ")";
	return sqlQuery("callerid", "sip_buddies", "name", IMSI);
}



string SubscriberRegistry::getCLIDGlobal(string IMSI)
{
	if (IMSI.empty()) {
		LOG(WARNING) << "SubscriberRegistry::getCLIDGlobal attempting lookup of NULL IMSI";
		return "";
	}
	LOG(INFO) << "getCLIDGlobal(" << IMSI << ")";
	return sqlQuery("callerid", "sip_buddies", "name", IMSI);
}



string SubscriberRegistry::getRegistrationIP(string IMSI)
{
	if (IMSI.empty()) {
		LOG(WARNING) << "SubscriberRegistry::getRegistrationIP attempting lookup of NULL IMSI";
		return "";
	}
	LOG(INFO) << "getRegistrationIP(" << IMSI << ")";
	return sqlQuery("ipaddr", "sip_buddies", "name", IMSI);
}



SubscriberRegistry::Status SubscriberRegistry::setRegTime(string IMSI)
{
	if (IMSI.empty()) {
		LOG(WARNING) << "SubscriberRegistry::setRegTime attempting set for NULL IMSI";
		return FAILURE;
	}
	unsigned now = (unsigned)time(NULL);
	ostringstream os;
	os << "update sip_buddies set regTime = " << now  << " where name = " << '"' << IMSI << '"';
	return sqlUpdate(os.str());
}



SubscriberRegistry::Status SubscriberRegistry::addUser(string IMSI, string CLID)
{
	if (IMSI.empty()) {
		LOG(WARNING) << "SubscriberRegistry::addUser attempting add of NULL IMSI";
		return FAILURE;
	}
	if (CLID.empty()) {
		LOG(WARNING) << "SubscriberRegistry::addUser attempting add of NULL CLID";
		return FAILURE;
	}
	if (!getIMSI(CLID).empty() || !getCLIDLocal(IMSI).empty()) {
		LOG(WARNING) << "SubscriberRegistry::addUser attempting user duplication";
		//technically failure, but let's move on
		return SUCCESS;
	}
	LOG(INFO) << "addUser(" << IMSI << "," << CLID << ")";
	ostringstream os;
	os << "insert into sip_buddies (name, username, type, context, host, callerid, canreinvite, allow, dtmfmode, ipaddr) values (";
	os << "\"" << IMSI << "\"";
	os << ",";
	os << "\"" << IMSI << "\"";
	os << ",";
	os << "\"" << "friend" << "\"";
	os << ",";
	os << "\"" << "phones" << "\"";
	os << ",";
	os << "\"" << "dynamic" << "\"";
	os << ",";
	os << "\"" << CLID << "\"";
	os << ",";
	os << "\"" << "no" << "\"";
	os << ",";
	os << "\"" << "gsm" << "\"";
	os << ",";
	os << "\"" << "info" << "\"";
	os << ",";
	os << "\"" << "127.0.0.1" << "\"";
	os << ")";
	os << ";";
	SubscriberRegistry::Status st = sqlUpdate(os.str());
	ostringstream os2;
	os2 << "insert into dialdata_table (exten, dial) values (";
	os2 << "\"" << CLID << "\"";
	os2 << ",";
	os2 << "\"" << IMSI << "\"";
	os2 << ")";
	SubscriberRegistry::Status st2 = sqlUpdate(os2.str());
	return st == SUCCESS && st2 == SUCCESS ? SUCCESS : FAILURE;
}



string SubscriberRegistry::mapCLIDGlobal(string local)
{
	if (local.empty()) {
		LOG(WARNING) << "SubscriberRegistry::mapCLIDGlobal attempting lookup of NULL local";
		return "";
	}
	LOG(INFO) << "mapCLIDGlobal(" << local << ")";
	string IMSI = getIMSI(local);
	if (IMSI.empty()) return "";
	return getCLIDGlobal(IMSI);
}

SubscriberRegistry::Status SubscriberRegistry::RRLPUpdate(string name, string lat, string lon, string err){
	ostringstream os;
	os << "insert into RRLP (name, latitude, longitude, error, time) values (" <<
	  '"' << name << '"' << "," <<
	  lat << "," <<
	  lon << "," <<
	  err << "," <<
	  "datetime('now')"
	  ")";
	LOG(INFO) << os.str();
	return sqlUpdate(os.str());
}

bool SubscriberRegistry::useGateway(string ISDN)
{
	// FIXME -- Do something more general in Asterisk.
	// This is a hack for Burning Man.
	int cmp = strncmp(ISDN.c_str(),"88351000125",11);
	return cmp!=0;
}






// vim: ts=4 sw=4
