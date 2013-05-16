/*
* Copyright 2011 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012 Range Networks, Inc.
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

static const char* createRateTable = {
"create table if not exists rates (service varchar(30) not null, rate integer not null)"
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
		"dtmfmode              VARCHAR(7) DEFAULT 'info', "
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
		"allow                 VARCHAR(100) DEFAULT 'gsm' not null, "
		"fullcontact           VARCHAR(80), "
		"ipaddr                VARCHAR(45), "
		"port                  int(5) DEFAULT 5062, "
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
		"busylevel             int(11) default 1, "
		"allowoverlap          VARCHAR(3) DEFAULT 'no', "
		"allowsubscribe        VARCHAR(3) DEFAULT 'no', "
		"allowtransfer         VARCHAR(3) DEFAULT 'no', "
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
		"canreinvite           CHAR(3) DEFAULT 'no' not null, "
		"mask                  VARCHAR(95), "
		"musiconhold           VARCHAR(100), "
		"restrictcid           CHAR(3), "
		"calllimit             int(5) default 1, "
		"WhiteListFlag         timestamp not null default '0', "
		"WhiteListCode         varchar(8) not null default '0', "
		"rand                  varchar(33) default '', "
		"sres                  varchar(33) default '', "
		"ki                    varchar(33) default '', "
		"kc                    varchar(33) default '', "
		"prepaid               int(1) DEFAULT 0 not null, "	// flag to indicate prepaid customer
		"account_balance       int(9) default 0 not null, "	// current account, neg is debt, pos is credit
		"RRLPSupported         int(1) default 1 not null, "
  		"hardware              VARCHAR(20), "
		"regTime               INTEGER default 0 NOT NULL, " // Unix time of most recent registration
		"a3_a8                 varchar(45) default NULL"
    ")"
};


int SubscriberRegistry::init()
{
	string ldb = gConfig.getStr("SubscriberRegistry.db");
	size_t p = ldb.find_last_of('/');
	if (p == string::npos) {
		LOG(EMERG) << "SubscriberRegistry.db not in a directory?";
		mDB = NULL;
		return 1;
	}
	string dir = ldb.substr(0, p);
	struct stat buf;
	if (stat(dir.c_str(), &buf)) {
		LOG(EMERG) << dir << " does not exist";
		mDB = NULL;
		return 1;
	} 
	int rc = sqlite3_open(ldb.c_str(),&mDB);
	if (rc) {
		LOG(EMERG) << "Cannot open SubscriberRegistry database: " << ldb << " error: " << sqlite3_errmsg(mDB);
		sqlite3_close(mDB);
		mDB = NULL;
		return 1;
	}
	if (!sqlite3_command(mDB,createRRLPTable)) {
		LOG(EMERG) << "Cannot create RRLP table";
		return 1;
	}
	if (!sqlite3_command(mDB,createDDTable)) {
		LOG(EMERG) << "Cannot create DIALDATA_TABLE table";
		return 1;
	}
	if (!sqlite3_command(mDB,createRateTable)) {
		LOG(EMERG) << "Cannot create rate table";
		return 1;
	}
	if (!sqlite3_command(mDB,createSBTable)) {
		LOG(EMERG) << "Cannot create SIP_BUDDIES table";
		return 1;
	}
	if (!getCLIDLocal("IMSI001010000000000")) {
		// This is a test SIM provided with the BTS.
		if (addUser("IMSI001010000000000", "2100") != SUCCESS) {
        		LOG(EMERG) << "Cannot insert test SIM";
		}
	}
	return 0;
}



SubscriberRegistry::~SubscriberRegistry()
{
	if (mDB) sqlite3_close(mDB);
}

SubscriberRegistry::Status SubscriberRegistry::sqlLocal(const char *query, char **resultptr)
{
	LOG(INFO) << query;

	if (!resultptr) {
		if (!sqlite3_command(db(), query)) return FAILURE;
		return SUCCESS;
	}

	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(db(), &stmt, query)) {
		LOG(ERR) << "sqlite3_prepare_statement problem with query \"" << query << "\"";
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



char *SubscriberRegistry::sqlQuery(const char *unknownColumn, const char *table, const char *knownColumn, const char *knownValue)
{
	char *result = NULL;
	SubscriberRegistry::Status st;
	ostringstream os;
	os << "select " << unknownColumn << " from " << table << " where " << knownColumn << " = \"" << knownValue << "\"";
	// try to find locally
	st = sqlLocal(os.str().c_str(), &result);
	if ((st == SUCCESS) && result) {
		// got it.  return it.
		LOG(INFO) << "result = " << result;
		return result;
	}
	LOG(INFO) << "not found: " << os.str();
	return NULL;
}



SubscriberRegistry::Status SubscriberRegistry::sqlUpdate(const char *stmt)
{
	LOG(INFO) << stmt;
	SubscriberRegistry::Status st = sqlLocal(stmt, NULL);
	// status of local is only important one because asterisk talks to that db directly
	// must update local no matter what
	return st;
}

string SubscriberRegistry::imsiGet(string imsi, string key)
{
	string name = imsi.substr(0,4) == "IMSI" ? imsi : "IMSI" + imsi;
	return sqlQuery(key.c_str(), "sip_buddies", "name", imsi.c_str());
}

bool SubscriberRegistry::imsiSet(string imsi, string key, string value)
{
	string name = imsi.substr(0,4) == "IMSI" ? imsi : "IMSI" + imsi;
	ostringstream os;
	os << "update sip_buddies set " << key << " = \"" << value << "\" where name = \"" << name << "\"";
	return sqlUpdate(os.str().c_str()) == FAILURE;
}

char *SubscriberRegistry::getIMSI(const char *ISDN)
{
	if (!ISDN) {
		LOG(WARNING) << "SubscriberRegistry::getIMSI attempting lookup of NULL ISDN";
		return NULL;
	}
	LOG(INFO) << "getIMSI(" << ISDN << ")";
	return sqlQuery("dial", "dialdata_table", "exten", ISDN);
}



char *SubscriberRegistry::getCLIDLocal(const char* IMSI)
{
	if (!IMSI) {
		LOG(WARNING) << "SubscriberRegistry::getCLIDLocal attempting lookup of NULL IMSI";
		return NULL;
	}
	LOG(INFO) << "getCLIDLocal(" << IMSI << ")";
	return sqlQuery("callerid", "sip_buddies", "username", IMSI);
}



char *SubscriberRegistry::getCLIDGlobal(const char* IMSI)
{
	if (!IMSI) {
		LOG(WARNING) << "SubscriberRegistry::getCLIDGlobal attempting lookup of NULL IMSI";
		return NULL;
	}
	LOG(INFO) << "getCLIDGlobal(" << IMSI << ")";
	return sqlQuery("callerid", "sip_buddies", "username", IMSI);
}



char *SubscriberRegistry::getRegistrationIP(const char* IMSI)
{
	if (!IMSI) {
		LOG(WARNING) << "SubscriberRegistry::getRegistrationIP attempting lookup of NULL IMSI";
		return NULL;
	}
	LOG(INFO) << "getRegistrationIP(" << IMSI << ")";
	return sqlQuery("ipaddr", "sip_buddies", "username", IMSI);
}



SubscriberRegistry::Status SubscriberRegistry::setRegTime(const char* IMSI)
{
	if (!IMSI) {
		LOG(WARNING) << "SubscriberRegistry::setRegTime attempting set for NULL IMSI";
		return FAILURE;
	}
	unsigned now = (unsigned)time(NULL);
	ostringstream os;
	os << "update sip_buddies set regTime = " << now  << " where username = " << '"' << IMSI << '"';
	return sqlUpdate(os.str().c_str());
}



SubscriberRegistry::Status SubscriberRegistry::addUser(const char* IMSI, const char* CLID)
{
	if (!IMSI) {
		LOG(WARNING) << "SubscriberRegistry::addUser attempting add of NULL IMSI";
		return FAILURE;
	}
	if (!CLID) {
		LOG(WARNING) << "SubscriberRegistry::addUser attempting add of NULL CLID";
		return FAILURE;
	}
	LOG(INFO) << "addUser(" << IMSI << "," << CLID << ")";
	ostringstream os;
	os << "insert into sip_buddies (name, username, type, context, host, callerid, canreinvite, allow, dtmfmode, ipaddr, port) values (";
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
	os << ",";
	os << "\"" << "5062" << "\"";
	os << ")";
	os << ";";
	SubscriberRegistry::Status st = sqlUpdate(os.str().c_str());
	ostringstream os2;
	os2 << "insert into dialdata_table (exten, dial) values (";
	os2 << "\"" << CLID << "\"";
	os2 << ",";
	os2 << "\"" << IMSI << "\"";
	os2 << ")";
	SubscriberRegistry::Status st2 = sqlUpdate(os2.str().c_str());
	return st == SUCCESS && st2 == SUCCESS ? SUCCESS : FAILURE;
}


// For handover.  Only remove the local cache.  BS2 will have updated the global.
SubscriberRegistry::Status SubscriberRegistry::removeUser(const char* IMSI)
{
	if (!IMSI) {
		LOG(WARNING) << "SubscriberRegistry::addUser attempting add of NULL IMSI";
		return FAILURE;
	}
	LOG(INFO) << "removeUser(" << IMSI << ")";
	string server = gConfig.getStr("SubscriberRegistry.UpstreamServer");
	if (server.length() == 0) {
		LOG(INFO) << "not removing user if no upstream server";
		return FAILURE;
	}
	ostringstream os;
	os << "delete from sip_buddies where name = ";
	os << "\"" << IMSI << "\"";
	os << ";";
	LOG(INFO) << os.str();
	SubscriberRegistry::Status st = sqlLocal(os.str().c_str(), NULL);
	ostringstream os2;
	os2 << "delete from dialdata_table where dial = ";
	os2 << "\"" << IMSI << "\"";
	LOG(INFO) << os2.str();
	SubscriberRegistry::Status st2 = sqlLocal(os2.str().c_str(), NULL);
	return st == SUCCESS && st2 == SUCCESS ? SUCCESS : FAILURE;
}



char *SubscriberRegistry::mapCLIDGlobal(const char *local)
{
	if (!local) {
		LOG(WARNING) << "SubscriberRegistry::mapCLIDGlobal attempting lookup of NULL local";
		return NULL;
	}
	LOG(INFO) << "mapCLIDGlobal(" << local << ")";
	char *IMSI = getIMSI(local);
	if (!IMSI) return NULL;
	char *global = getCLIDGlobal(IMSI);
	free(IMSI);
	return global;
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
	return sqlUpdate(os.str().c_str());
}

void SubscriberRegistry::stringToUint(string strRAND, uint64_t *hRAND, uint64_t *lRAND)
{
	assert(strRAND.size() == 32);
	string strhRAND = strRAND.substr(0, 16);
	string strlRAND = strRAND.substr(16, 16);
	stringstream ssh;
	ssh << hex << strhRAND;
	ssh >> *hRAND;
	stringstream ssl;
	ssl << hex << strlRAND;
	ssl >> *lRAND;
}

string SubscriberRegistry::uintToString(uint64_t h, uint64_t l)
{
	ostringstream os1;
	os1.width(16);
	os1.fill('0');
	os1 << hex << h;
	ostringstream os2;
	os2.width(16);
	os2.fill('0');
	os2 << hex << l;
	ostringstream os3;
	os3 << os1.str() << os2.str();
	return os3.str();
}

string SubscriberRegistry::uintToString(uint32_t x)
{
	ostringstream os;
	os.width(8);
	os.fill('0');
	os << hex << x;
	return os.str();
}

bool SubscriberRegistry::useGateway(const char* ISDN)
{
	// FIXME -- Do something more general in Asterisk.
	// This is a hack for Burning Man.
	int cmp = strncmp(ISDN,"88351000125",11);
	return cmp!=0;
}



SubscriberRegistry::Status SubscriberRegistry::setPrepaid(const char *IMSI, bool yes)
{
	ostringstream os;
	os << "update sip_buddies set prepaid = " << (yes ? 1 : 0)  << " where username = " << '"' << IMSI << '"';
	return sqlUpdate(os.str().c_str());
}


SubscriberRegistry::Status SubscriberRegistry::isPrepaid(const char *IMSI, bool &yes)
{
	char *st = sqlQuery("prepaid", "sip_buddies", "username", IMSI);
	if (!st) {
		LOG(NOTICE) << "cannot get prepaid status for username " << IMSI;
		return FAILURE;
	}
	yes = *st == '1';
	free(st);
	return SUCCESS;
}


SubscriberRegistry::Status SubscriberRegistry::balanceRemaining(const char *IMSI, int &balance)
{
	char *st = sqlQuery("account_balance", "sip_buddies", "username", IMSI);
	if (!st) {
		LOG(NOTICE) << "cannot get balance for " << IMSI;
		return FAILURE;
	}
	balance = (int)strtol(st, (char **)NULL, 10);
	free(st);
	return SUCCESS;
}


SubscriberRegistry::Status SubscriberRegistry::addMoney(const char *IMSI, int moneyToAdd)
{
	ostringstream os;
	os << "update sip_buddies set account_balance = account_balance + " << moneyToAdd << " where username = " << '"' << IMSI << '"';
	if (sqlUpdate(os.str().c_str()) == FAILURE) {
		LOG(NOTICE) << "cannot update rate for username " << IMSI;
		return FAILURE;
	}
	return SUCCESS;
}

int SubscriberRegistry::serviceCost(const char* service)
{
	char *rateSt = sqlQuery("rate", "rates", "service", service);
	if (!rateSt) {
		LOG(ALERT) << "cannot get rate for service " << service;
		return -1;
	}
	int rate = (int)strtol(rateSt, (char **)NULL, 10);
	free(rateSt);
	return rate;
}

SubscriberRegistry::Status SubscriberRegistry::serviceUnits(const char *IMSI, const char* service, int &units)
{
	int balance;
	Status stat = balanceRemaining(IMSI,balance);
	if (stat == FAILURE) return FAILURE;
	int rate = serviceCost(service);
	if (rate<0) return FAILURE;
	units = balance / rate;
	return SUCCESS;
}





// vim: ts=4 sw=4









