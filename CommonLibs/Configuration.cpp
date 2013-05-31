/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012 Range Networks, Inc.
*
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


#include "Configuration.h"
#include "Logger.h"
#include <fstream>
#include <iostream>
#include <string.h>

#ifdef DEBUG_CONFIG
#define	debugLogEarly gLogEarly
#else
#define	debugLogEarly
#endif


using namespace std;

char gCmdName[20] = {0}; // Use a char* to avoid avoid static initialization of string, and race at startup.

static const char* createConfigTable = {
	"CREATE TABLE IF NOT EXISTS CONFIG ("
		"KEYSTRING TEXT UNIQUE NOT NULL, "
		"VALUESTRING TEXT, "
		"STATIC INTEGER DEFAULT 0, "
		"OPTIONAL INTEGER DEFAULT 0, "
		"COMMENTS TEXT DEFAULT ''"
	")"
};



float ConfigurationRecord::floatNumber() const
{
	float val;
	sscanf(mValue.c_str(),"%f",&val);
	return val;
}


ConfigurationTable::ConfigurationTable(const char* filename, const char *wCmdName, ConfigurationKeyMap wSchema)
{
	gLogEarly(LOG_INFO, "opening configuration table from path %s", filename);
	// Connect to the database.
	int rc = sqlite3_open(filename,&mDB);
	// (pat) When I used malloc here, sqlite3 sporadically crashes.
	if (wCmdName) {
		strncpy(gCmdName,wCmdName,18);
		gCmdName[18] = 0;
		strcat(gCmdName,":");
	}
	if (rc) {
		gLogEarly(LOG_EMERG, "cannot open configuration database at %s, error message: %s", filename, sqlite3_errmsg(mDB));
		sqlite3_close(mDB);
		mDB = NULL;
		return;
	}
	// Create the table, if needed.
	if (!sqlite3_command(mDB,createConfigTable)) {
		gLogEarly(LOG_EMERG, "cannot create configuration table in database at %s, error message: %s", filename, sqlite3_errmsg(mDB));
	}

	// Build CommonLibs schema
	ConfigurationKey *tmp;
	tmp = new ConfigurationKey("Log.Alarms.Max","20",
		"alarms",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::VALRANGE,
		"10:20",// educated guess
		false,
		"Maximum number of alarms to remember inside the application."
	);
	mSchema[tmp->getName()] = *tmp;
	free(tmp);

	tmp = new ConfigurationKey("Log.File","",
		"",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::FILEPATH_OPT,// audited
		"",
		false,
		"Path to use for textfile based logging.  "
			"By default, this feature is disabled.  "
			"To enable, specify an absolute path to the file you wish to use, eg: /tmp/my-debug.log.  "
			"To disable again, execute \"unconfig Log.File\"."
	);
	mSchema[tmp->getName()] = *tmp;
	free(tmp);

	tmp = new ConfigurationKey("Log.Level","NOTICE",
		"",
		ConfigurationKey::CUSTOMER,
		ConfigurationKey::CHOICE,
		"EMERG|EMERGENCY - report serious faults associated with service failure or hardware damage,"
			"ALERT|ALERT - report likely service disruption caused by misconfiguration or poor connectivity,"
			"CRIT|CRITICAL - report anomalous events that are likely to degrade service,"
			"ERR|ERROR - report internal errors of the software that may result in degradation of service in unusual circumstances,"
			"WARNING|WARNING - report anomalous events that may indicate a degradation of normal service,"
			"NOTICE|NOTICE - report anomalous events that probably do not affect service but may be of interest to network operators,"
			"INFO|INFORMATION - report normal events,"
			"DEBUG|DEBUG - only for use by developers and will degrade system performance",
		false,
		"Default logging level when no other level is defined for a file."
	);
	mSchema[tmp->getName()] = *tmp;
	free(tmp);

	// Add application specific schema
	mSchema.insert(wSchema.begin(), wSchema.end());

	// Init the cross checking callback to something predictable
	mCrossCheck = NULL;
}

string ConfigurationTable::getDefaultSQL(const std::string& program, const std::string& version)
{
	stringstream ss;
	ConfigurationKeyMap::iterator mp;

	ss << "--" << endl;
	ss << "-- This file was generated using: " << program << " --gensql" << endl;
	ss << "-- binary version: " << version << endl;
	ss << "--" << endl;
	ss << "-- Future changes should not be put in this file directly but" << endl;
	ss << "-- rather in the program's ConfigurationKey schema." << endl;
	ss << "--" << endl;
	ss << "PRAGMA foreign_keys=OFF;" << endl;
	ss << "BEGIN TRANSACTION;" << endl;
	ss << "CREATE TABLE CONFIG ( KEYSTRING TEXT UNIQUE NOT NULL, VALUESTRING TEXT, STATIC INTEGER DEFAULT 0, OPTIONAL INTEGER DEFAULT 0, COMMENTS TEXT DEFAULT '');" << endl;

	mp = mSchema.begin();
	while (mp != mSchema.end()) {
		ss << "INSERT INTO \"CONFIG\" VALUES(";
			// name
			ss << "'" << mp->first << "',";
			// default
			ss << "'" << mp->second.getDefaultValue() << "',";
			// static
			if (mp->second.isStatic()) {
				ss << "1";
			} else {
				ss << "0";
			}
			ss << ",";
			// optional
			ss << "0,";
			// description
			ss << "'";
			if (mp->second.getType() == ConfigurationKey::BOOLEAN) {
				ss << "1=enabled, 0=disabled - ";
			}
			ss << mp->second.getDescription();
			if (mp->second.isStatic()) {
				ss << "  Static.";
			}
			ss << "'";
		ss << ");" << endl;
		mp++;
	}

	ss << "COMMIT;" << endl;
	ss << endl;

	return ss.str();
}

string ConfigurationTable::getTeX(const std::string& program, const std::string& version)
{
	stringstream ss;
	ConfigurationKeyMap::iterator mp;

	ss << "% START AUTO-GENERATED CONTENT" << endl;
	ss << "% -- these sections were generated using: " << program << " --gentex" << endl;
	ss << "% -- binary version: " << version << endl;

	ss << "\\subsection{Customer Site Parameters}" << endl;
	ss << "These parameters must be changed to fit your site." << endl;
	ss << "\\begin{itemize}" << endl;
	mp = mSchema.begin();
	while (mp != mSchema.end()) {
		if (mp->second.getVisibility() == ConfigurationKey::CUSTOMERSITE) {
			ss << "	\\item ";
				// name
				ss << mp->first << " -- ";
				// description
				ss << mp->second.getDescription();
			ss << endl;
		}
		mp++;
	}
	ss << "\\end{itemize}" << endl;
	ss << endl;

	ss << "\\subsection{Customer Tuneable Parameters}" << endl;
	ss << "These parameters can be changed to optimize your site." << endl;
	ss << "\\begin{itemize}" << endl;
	mp = mSchema.begin();
	while (mp != mSchema.end()) {
		if (mp->second.getVisibility() != ConfigurationKey::CUSTOMERSITE &&
			(
				mp->second.getVisibility() == ConfigurationKey::CUSTOMER ||
				mp->second.getVisibility() == ConfigurationKey::CUSTOMERTUNE ||
				mp->second.getVisibility() == ConfigurationKey::CUSTOMERWARN
			)) {
			ss << "	\\item ";
				// name
				ss << mp->first << " -- ";
				// description
				ss << mp->second.getDescription();
			ss << endl;
		}
		mp++;
	}
	ss << "\\end{itemize}" << endl;
	ss << endl;

	ss << "\\subsection{Developer/Factory Parameters}" << endl;
	ss << "These parameters should only be changed by when developing new code." << endl;
	ss << "\\begin{itemize}" << endl;
	mp = mSchema.begin();
	while (mp != mSchema.end()) {
		if (mp->second.getVisibility() == ConfigurationKey::FACTORY ||
			mp->second.getVisibility() == ConfigurationKey::DEVELOPER) {
			ss << "	\\item ";
				// name
				ss << mp->first << " -- ";
				// description
				ss << mp->second.getDescription();
			ss << endl;
		}
		mp++;
	}
	ss << "\\end{itemize}" << endl;
	ss << "% END AUTO-GENERATED CONTENT" << endl;
	ss << endl;

	string tmp = Utils::replaceAll(ss.str(), "^", "\\^");
	return Utils::replaceAll(tmp, "_", "\\_");
}

bool ConfigurationTable::defines(const string& key)
{
	try {
		ScopedLock lock(mLock);
		return lookup(key).defined();
	} catch (ConfigurationTableKeyNotFound) {
		debugLogEarly(LOG_ALERT, "configuration parameter %s not found", key.c_str());
		return false;
	}
}

bool ConfigurationTable::keyDefinedInSchema(const std::string& name)
{
	return mSchema.find(name) == mSchema.end() ? false : true;
}

bool ConfigurationTable::isValidValue(const std::string& name, const std::string& val) {
	bool ret = false;

	ConfigurationKey key = mSchema[name];

	switch (key.getType()) {
		case ConfigurationKey::BOOLEAN: {
			if (val == "1" || val == "0") {
				ret = true;
			}
			break;
		}

		case ConfigurationKey::CHOICE_OPT: {
			if (val.length() == 0) {
				ret = true;
				break;
			}
		}
		case ConfigurationKey::CHOICE: {
			int startPos = -1;
			uint endPos = 0;

			std::string tmp = key.getValidValues();

			do {
				startPos++;
				if ((endPos = tmp.find('|', startPos)) != std::string::npos) {
					if (val == tmp.substr(startPos, endPos-startPos)) {
						ret = true;
						break;
					}
				} else {
					if (val == tmp.substr(startPos, tmp.find(',', startPos)-startPos)) {
						ret = true;
						break;
					}
				}

			} while ((startPos = tmp.find(',', startPos)) != (int)std::string::npos);
			break;
		}

		case ConfigurationKey::CIDR_OPT: {
			if (val.length() == 0) {
				ret = true;
				break;
			}
		}
		case ConfigurationKey::CIDR: {
			uint delimiter;
			std::string ip;
			int cidr = -1;

			delimiter = val.find('/');
			if (delimiter != std::string::npos) {
				ip = val.substr(0, delimiter);
				std::stringstream(val.substr(delimiter+1)) >> cidr;
				if (ConfigurationKey::isValidIP(ip) && 0 <= cidr && cidr <= 32) {
					ret = true;
				}
			}
			break;
		}

		case ConfigurationKey::FILEPATH_OPT: {
			if (val.length() == 0) {
				ret = true;
				break;
			}
		}
		case ConfigurationKey::FILEPATH: {
			regex_t r;
			const char* expression = "^[a-zA-Z0-9/_.-]+$";
			int result = regcomp(&r, expression, REG_EXTENDED);
			if (result) {
				char msg[256];
				regerror(result,&r,msg,255);
				break;//abort();
			}
			if (regexec(&r, val.c_str(), 0, NULL, 0)==0) {
				ret = true;
			}
			regfree(&r);
			break;
		}

		case ConfigurationKey::IPADDRESS_OPT: {
			if (val.length() == 0) {
				ret = true;
				break;
			}
		}
		case ConfigurationKey::IPADDRESS: {
			ret = ConfigurationKey::isValidIP(val);
			break;
		}

		case ConfigurationKey::IPANDPORT: {
			uint delimiter;
			std::string ip;
			int port = -1;

			delimiter = val.find(':');
			if (delimiter != std::string::npos) {
				ip = val.substr(0, delimiter);
				std::stringstream(val.substr(delimiter+1)) >> port;
				if (ConfigurationKey::isValidIP(ip) && 1 <= port && port <= 65535) {
					ret = true;
				}
			}
			break;
		}

		case ConfigurationKey::MIPADDRESS_OPT: {
			if (val.length() == 0) {
				ret = true;
				break;
			}
		}
		case ConfigurationKey::MIPADDRESS: {
			int startPos = -1;
			uint endPos = 0;

			do {
				startPos++;
				endPos = val.find(' ', startPos);
				if (ConfigurationKey::isValidIP(val.substr(startPos, endPos-startPos))) {
					ret = true;
				} else {
					ret = false;
					break;
				}

			} while ((startPos = endPos) != (int)std::string::npos);
			break;
		}

		case ConfigurationKey::PORT_OPT: {
			if (val.length() == 0) {
				ret = true;
				break;
			}
		}
		case ConfigurationKey::PORT: {
			int intVal;

			std::stringstream(val) >> intVal;

			if (1 <= intVal && intVal <= 65535) {
				ret = true;
			}
			break;
		}

		case ConfigurationKey::REGEX_OPT: {
			if (val.length() == 0) {
				ret = true;
				break;
			}
		}
		case ConfigurationKey::REGEX: {
			regex_t r;
			const char* expression = val.c_str();
			int result = regcomp(&r, expression, REG_EXTENDED);
			if (result) {
				char msg[256];
				regerror(result,&r,msg,255);
			} else {
				ret = true;
			}
			regfree(&r);
			break;
		}

		case ConfigurationKey::STRING_OPT: {
			if (val.length() == 0) {
				ret = true;
				break;
			}
		}
		case ConfigurationKey::STRING: {
			regex_t r;
			const char* expression = key.getValidValues().c_str();
			int result = regcomp(&r, expression, REG_EXTENDED);
			if (result) {
				char msg[256];
				regerror(result,&r,msg,255);
				break;//abort();
			}
			if (regexec(&r, val.c_str(), 0, NULL, 0)==0) {
				ret = true;
			}
			regfree(&r);
			break;
		}

		case ConfigurationKey::VALRANGE: {
			regex_t r;
			int result;
			if (key.getValidValues().find('.') != std::string::npos) {
				result = regcomp(&r, "^[0-9.-]+$", REG_EXTENDED);
			} else {
				result = regcomp(&r, "^[0-9-]+$", REG_EXTENDED);
			}
			if (result) {
				char msg[256];
				regerror(result,&r,msg,255);
				break;//abort();
			}
			if (regexec(&r, val.c_str(), 0, NULL, 0)!=0) {
				ret = false;
			} else if (key.getValidValues().find('.') != std::string::npos) {
				ret = ConfigurationKey::isInValRange<float>(key, val, false);
			} else {
				ret = ConfigurationKey::isInValRange<int>(key, val, true);
			}

			regfree(&r);
			break;
		}
	}

	return ret;
}

ConfigurationKeyMap ConfigurationTable::getSimilarKeys(const std::string& snippet) {
	ConfigurationKeyMap tmp;

	ConfigurationKeyMap::const_iterator mp = mSchema.begin();
	while (mp != mSchema.end()) {
		if (mp->first.find(snippet) != std::string::npos) {
			tmp[mp->first] = mp->second;
		}
		mp++;
	}

	return tmp;
}

const ConfigurationRecord& ConfigurationTable::lookup(const string& key)
{
	assert(mDB);
	checkCacheAge();
	// We assume the caller holds mLock.
	// So it is OK to return a reference into the cache.

	// Check the cache.
	// This is cheap.
	ConfigurationMap::const_iterator where = mCache.find(key);
	if (where!=mCache.end()) {
		if (where->second.defined()) return where->second;
		throw ConfigurationTableKeyNotFound(key);
	}

	// Check the database.
	// This is more expensive.
	char *value = NULL;
	sqlite3_single_lookup(mDB,"CONFIG",
			"KEYSTRING",key.c_str(),"VALUESTRING",value);

	// value found, cache the result
	if (value) {
		mCache[key] = ConfigurationRecord(value);
	// key definition found, cache the default
	} else if (keyDefinedInSchema(key)) {
		mCache[key] = ConfigurationRecord(mSchema[key].getDefaultValue());
	// total miss, cache the error
	} else {
		mCache[key] = ConfigurationRecord(false);
		throw ConfigurationTableKeyNotFound(key);
	}

	free(value);

	// Leave mLock locked.  The caller holds it still.
	return mCache[key];
}



bool ConfigurationTable::isStatic(const string& key)
{
	if (keyDefinedInSchema(key)) {
		return mSchema[key].isStatic();
	} else {
		return false;
	}
}




string ConfigurationTable::getStr(const string& key)
{
	// We need the lock because rec is a reference into the cache.
	try {
		ScopedLock lock(mLock);
		return lookup(key).value();
	} catch (ConfigurationTableKeyNotFound) {
		// Raise an alert and re-throw the exception.
		debugLogEarly(LOG_ALERT, "configuration parameter %s has no defined value", key.c_str());
		throw ConfigurationTableKeyNotFound(key);
	}
}


bool ConfigurationTable::getBool(const string& key)
{
	try {
		return getNum(key) != 0;
	} catch (ConfigurationTableKeyNotFound) {
		// Raise an alert and re-throw the exception.
		debugLogEarly(LOG_ALERT, "configuration parameter %s has no defined value", key.c_str());
		throw ConfigurationTableKeyNotFound(key);
	}
}


long ConfigurationTable::getNum(const string& key)
{
	// We need the lock because rec is a reference into the cache.
	try {
		ScopedLock lock(mLock);
		return lookup(key).number();
	} catch (ConfigurationTableKeyNotFound) {
		// Raise an alert and re-throw the exception.
		debugLogEarly(LOG_ALERT, "configuration parameter %s has no defined value", key.c_str());
		throw ConfigurationTableKeyNotFound(key);
	}
}


float ConfigurationTable::getFloat(const string& key)
{
	try {
		ScopedLock lock(mLock);
		return lookup(key).floatNumber();
	} catch (ConfigurationTableKeyNotFound) {
		// Raise an alert and re-throw the exception.
		debugLogEarly(LOG_ALERT, "configuration parameter %s has no defined value", key.c_str());
		throw ConfigurationTableKeyNotFound(key);
	}
}

std::vector<string> ConfigurationTable::getVectorOfStrings(const string& key)
{
	// Look up the string.
	char *line=NULL;
	try {
		ScopedLock lock(mLock);
		const ConfigurationRecord& rec = lookup(key);
		line = strdup(rec.value().c_str());
	} catch (ConfigurationTableKeyNotFound) {
		// Raise an alert and re-throw the exception.
		debugLogEarly(LOG_ALERT, "configuration parameter %s has no defined value", key.c_str());
		throw ConfigurationTableKeyNotFound(key);
	}

	assert(line);
	char *lp = line;
	
	// Parse the string.
	std::vector<string> retVal;
	while (lp) {
		while (*lp==' ') lp++;
		if (*lp == '\0') break;
		char *tp = strsep(&lp," ");
		if (!tp) break;
		retVal.push_back(tp);
	}
	free(line);
	return retVal;
}


std::vector<unsigned> ConfigurationTable::getVector(const string& key)
{
	// Look up the string.
	char *line=NULL;
	try {
		ScopedLock lock(mLock);
		const ConfigurationRecord& rec = lookup(key);
		line = strdup(rec.value().c_str());
	} catch (ConfigurationTableKeyNotFound) {
		// Raise an alert and re-throw the exception.
		debugLogEarly(LOG_ALERT, "configuration parameter %s has no defined value", key.c_str());
		throw ConfigurationTableKeyNotFound(key);
	}

	assert(line);
	char *lp = line;

	// Parse the string.
	std::vector<unsigned> retVal;
	while (lp) {
		// Watch for multiple or trailing spaces.
		while (*lp==' ') lp++;
		if (*lp=='\0') break;
		retVal.push_back(strtol(lp,NULL,0));
		strsep(&lp," ");
	}
	free(line);
	return retVal;
}


bool ConfigurationTable::remove(const string& key)
{
	assert(mDB);

	ScopedLock lock(mLock);
	// Clear the cache entry and the database.
	ConfigurationMap::iterator where = mCache.find(key);
	if (where!=mCache.end()) mCache.erase(where);
	// Really remove it.
	string cmd = "DELETE FROM CONFIG WHERE KEYSTRING=='"+key+"'";
	return sqlite3_command(mDB,cmd.c_str());
}



void ConfigurationTable::find(const string& pat, ostream& os) const
{
	// Prepare the statement.
	string cmd = "SELECT KEYSTRING,VALUESTRING FROM CONFIG WHERE KEYSTRING LIKE \"%" + pat + "%\"";
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(mDB,&stmt,cmd.c_str())) return;
	// Read the result.
	int src = sqlite3_run_query(mDB,stmt);
	while (src==SQLITE_ROW) {
		const char* value = (const char*)sqlite3_column_text(stmt,1);
		os << sqlite3_column_text(stmt,0) << " ";
		int len = 0;
		if (value) {
			len = strlen(value);
		}
		if (len && value) os << value << endl;
		else os << "(disabled)" << endl;
		src = sqlite3_run_query(mDB,stmt);
	}
	sqlite3_finalize(stmt);
}


ConfigurationRecordMap ConfigurationTable::getAllPairs() const
{
	ConfigurationRecordMap tmp;

	// Prepare the statement.
	string cmd = "SELECT KEYSTRING,VALUESTRING FROM CONFIG";
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(mDB,&stmt,cmd.c_str())) return tmp;
	// Read the result.
	int src = sqlite3_run_query(mDB,stmt);
	while (src==SQLITE_ROW) {
		const char* key = (const char*)sqlite3_column_text(stmt,0);
		const char* value = (const char*)sqlite3_column_text(stmt,1);
		if (key && value) {
			tmp[string(key)] = ConfigurationRecord(value);
		} else if (key && !value) {
			tmp[string(key)] = ConfigurationRecord(false);
		}
		src = sqlite3_run_query(mDB,stmt);
	}
	sqlite3_finalize(stmt);

	return tmp;
}

bool ConfigurationTable::set(const string& key, const string& value)
{
	assert(mDB);
	ScopedLock lock(mLock);
	string cmd = "INSERT OR REPLACE INTO CONFIG (KEYSTRING,VALUESTRING,OPTIONAL) VALUES (\"" + key + "\",\"" + value + "\",1)";
	bool success = sqlite3_command(mDB,cmd.c_str());
	// Cache the result.
	if (success) mCache[key] = ConfigurationRecord(value);
	return success;
}

bool ConfigurationTable::set(const string& key, long value)
{
	char buffer[30];
	sprintf(buffer,"%ld",value);
	return set(key,buffer);
}


bool ConfigurationTable::set(const string& key)
{
	assert(mDB);
	ScopedLock lock(mLock);
	string cmd = "INSERT OR REPLACE INTO CONFIG (KEYSTRING,VALUESTRING,OPTIONAL) VALUES (\"" + key + "\",NULL,1)";
	bool success = sqlite3_command(mDB,cmd.c_str());
	if (success) mCache[key] = ConfigurationRecord(true);
	return success;
}


void ConfigurationTable::checkCacheAge()
{
	// mLock is set by caller 
	static time_t timeOfLastPurge = 0;
	time_t now = time(NULL);
	// purge every 3 seconds
	// purge period cannot be configuration parameter
	if (now - timeOfLastPurge < 3) return;
	timeOfLastPurge = now;
	// this is purge() without the lock
	ConfigurationMap::iterator mp = mCache.begin();
	while (mp != mCache.end()) {
		ConfigurationMap::iterator prev = mp;
		mp++;
		mCache.erase(prev);
	}
}


void ConfigurationTable::purge()
{
	ScopedLock lock(mLock);
	ConfigurationMap::iterator mp = mCache.begin();
	while (mp != mCache.end()) {
		ConfigurationMap::iterator prev = mp;
		mp++;
		mCache.erase(prev);
	}
}


void ConfigurationTable::setUpdateHook(void(*func)(void *,int ,char const *,char const *,sqlite3_int64))
{
	assert(mDB);
	sqlite3_update_hook(mDB,func,NULL);
}


void ConfigurationTable::setCrossCheckHook(vector<string> (*wCrossCheck)(const string&))
{
	mCrossCheck = wCrossCheck;
}


vector<string> ConfigurationTable::crossCheck(const string& key) {
	vector<string> ret;

	if (mCrossCheck != NULL) {
		ret = mCrossCheck(key);
	}

	return ret;
}

void HashString::computeHash()
{
	// FIXME -- Someone needs to review this hash function.
	const char* cstr = c_str();
	mHash = 0;
	for (unsigned i=0; i<size(); i++) {
		mHash = mHash ^ (mHash >> 32);
		mHash = mHash*127 + cstr[i];
	}
}


void SimpleKeyValue::addItem(const char* pair_orig)
{
	char *pair = strdup(pair_orig);
	char *key = pair;
	char *mark = strchr(pair,'=');
	if (!mark) return;
	*mark = '\0';
	char *value = mark+1;
	mMap[key] = value;
	free(pair);
}



const char* SimpleKeyValue::get(const char* key) const
{
	HashStringMap::const_iterator p = mMap.find(key);
	if (p==mMap.end()) return NULL;
	return p->second.c_str();
}


void SimpleKeyValue::addItems(const char* pairs_orig)
{
	char *pairs = strdup(pairs_orig);
	char *thisPair;
	while ((thisPair=strsep(&pairs," "))!=NULL) {
		addItem(thisPair);
	}
	free(pairs);
}


bool ConfigurationKey::isValidIP(const std::string& ip) {
	struct sockaddr_in sa;
	return inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr)) != 0;
}


void ConfigurationKey::getMinMaxStepping(const ConfigurationKey &key, std::string &min, std::string &max, std::string &stepping) {
	uint delimiter;
	int startPos;
	uint endPos;

	std::string tmp = key.getValidValues();
	stepping = "1";

	// grab steps if they're defined
	startPos = tmp.find('(');
	if (startPos != (int)std::string::npos) {
		endPos = tmp.find(')');
		stepping = tmp.substr(startPos+1, endPos-startPos-1);
		tmp = tmp.substr(0, startPos);
	}
	startPos = 0;

	delimiter = tmp.find(':', startPos);
	min = tmp.substr(startPos, delimiter-startPos);
	max = tmp.substr(delimiter+1, tmp.find(',', delimiter)-delimiter-1);
}


template<class T> bool ConfigurationKey::isInValRange(const ConfigurationKey &key, const std::string& val, const bool isInteger) {
	bool ret = false;

	T convVal;
	T min;
	T max;
	T steps;
	std::string strMin;
	std::string strMax;
	std::string strSteps;

	std::stringstream(val) >> convVal;

	ConfigurationKey::getMinMaxStepping(key, strMin, strMax, strSteps);
	std::stringstream(strMin) >> min;
	std::stringstream(strMax) >> max;
	std::stringstream(strSteps) >> steps;

	// TODO : only ranges checked, steps not enforced
	if (isInteger) {
		if (val.find('.') == std::string::npos && min <= convVal && convVal <= max) {
			ret = true;
		}
	} else {
		if (min <= convVal && convVal <= max) {
			ret = true;
		}
	}

	return ret;
}

const std::string ConfigurationKey::getARFCNsString() {
	stringstream ss;
	int i;
	float downlink;
	float uplink;

	// 128:251 GSM850
	downlink = 869.2;
	uplink = 824.2;
	for (i = 128; i <= 251; i++) {
		ss << i << "|GSM850 #" << i << " : " << downlink << " MHz downlink / " << uplink << " MHz uplink,";
		downlink += 0.2;
		uplink += 0.2;
	}

	// 1:124 PGSM900
	downlink = 935.2;
	uplink = 890.2;
	for (i = 1; i <= 124; i++) {
		ss << i << "|PGSM900 #" << i << " : " << downlink << " MHz downlink / " << uplink << " MHz uplink,";
		downlink += 0.2;
		uplink += 0.2;
	}

	// 512:885 DCS1800
	downlink = 1805.2;
	uplink = 1710.2;
	for (i = 512; i <= 885; i++) {
		ss << i << "|DCS1800 #" << i << " : " << downlink << " MHz downlink / " << uplink << " MHz uplink,";
		downlink += 0.2;
		uplink += 0.2;
	}

	// 512:810 PCS1900
	downlink = 1930.2;
	uplink = 1850.2;
	for (i = 512; i <= 810; i++) {
		ss << i << "|PCS1900 #" << i << " : " << downlink << " MHz downlink / " << uplink << " MHz uplink,";
		downlink += 0.2;
		uplink += 0.2;
	}

	ss << endl;

	return ss.str();
}

const std::string ConfigurationKey::visibilityLevelToString(const ConfigurationKey::VisibilityLevel& visibility) {
	std::string ret = "UNKNOWN ERROR";

	switch (visibility) {
		case ConfigurationKey::CUSTOMER:
			ret = "customer - can be freely changed by the customer without any detriment to their system";
			break;
		case ConfigurationKey::CUSTOMERSITE:
			ret = "customer site - these values are different for each BTS and should not be left default";
			break;
		case ConfigurationKey::CUSTOMERTUNE:
			ret = "customer tune - should only be changed to tune an installation to better suit the physical environment or MS usage pattern";
			break;
		case ConfigurationKey::CUSTOMERWARN:
			ret = "customer warn - a warning will be presented and confirmation required before changing this sensitive setting";
			break;
		case ConfigurationKey::DEVELOPER:
			ret = "developer - should only be changed by developers to debug/optimize the implementation";
			break;
		case ConfigurationKey::FACTORY:
			ret = "factory - set once at the factory, should never be changed";
			break;
	}

	return ret;
}

const std::string ConfigurationKey::typeToString(const ConfigurationKey::Type& type) {
	std::string ret = "UNKNOWN ERROR";

	switch (type) {
		case BOOLEAN:
			ret = "boolean";
			break;
		case CHOICE_OPT:
			ret = "multiple choice (optional)";
			break;
		case CHOICE:
			ret = "multiple choice";
			break;
		case CIDR_OPT:
			ret = "CIDR notation (optional)";
			break;
		case CIDR:
			ret = "CIDR notation";
			break;
		case FILEPATH_OPT:
			ret = "file path (optional)";
			break;
		case FILEPATH:
			ret = "file path";
			break;
		case IPADDRESS_OPT:
			ret = "IP address (optional)";
			break;
		case IPADDRESS:
			ret = "IP address";
			break;
		case IPANDPORT:
			ret = "IP address and port";
			break;
		case MIPADDRESS_OPT:
			ret = "space-separated list of IP addresses (optional)";
			break;
		case MIPADDRESS:
			ret = "space-separated list of IP addresses";
			break;
		case PORT_OPT:
			ret = "IP port (optional)";
			break;
		case PORT:
			ret = "IP port";
			break;
		case REGEX_OPT:
			ret = "regular expression (optional)";
			break;
		case REGEX:
			ret = "regular expression";
			break;
		case STRING_OPT:
			ret = "string (optional)";
			break;
		case STRING:
			ret = "string";
			break;
		case VALRANGE:
			ret = "value range";
			break;
	}

	return ret;
}

void ConfigurationKey::printKey(const ConfigurationKey &key, const std::string& currentValue, ostream& os) {
	os << key.getName() << " ";
	if (!currentValue.length()) {
		os << "(disabled)";
	} else {
		os << currentValue;
	}
	if (currentValue.compare(key.getDefaultValue()) == 0) {
		os << "     [default]";
	}
	os << endl;
}

void ConfigurationKey::printDescription(const ConfigurationKey &key, ostream& os) {
	std::string tmp;

	os << " - description:      " << key.getDescription() << std::endl;
	if (key.getUnits().length()) {
		os << " - units:            " << key.getUnits() << std::endl;
	}
	os << " - type:             " << ConfigurationKey::typeToString(key.getType()) << std::endl;
	if (key.getDefaultValue().length()) {
		os << " - default value:    " << key.getDefaultValue() << std::endl;
	}
	os << " - visibility level: " << ConfigurationKey::visibilityLevelToString(key.getVisibility()) << std::endl;
	os << " - static:           " << key.isStatic() << std::endl;

	tmp = key.getValidValues();
	if (key.getType() == ConfigurationKey::VALRANGE) {
		int startPos = tmp.find('(');
		uint delimiter = 0;
		if (startPos != (int)std::string::npos) {
			tmp = tmp.substr(0, startPos);
		}
		startPos = -1;

		do {
			startPos++;
			delimiter = tmp.find(':', startPos);
			os << " - valid values:     " << "from " << tmp.substr(startPos, delimiter-startPos) << " to "
				<< tmp.substr(delimiter+1, tmp.find(',', delimiter)-delimiter-1) << std::endl;

		} while ((startPos = tmp.find(',', startPos)) != (int)std::string::npos);

	} else if (key.getType() == ConfigurationKey::CHOICE) {
		int startPos = -1;
		uint endPos = 0;

		do {
			startPos++;
			if ((endPos = tmp.find('|', startPos)) != std::string::npos) {
				os << " - valid values:     " << tmp.substr(startPos, endPos-startPos);
				os << " = " << tmp.substr(endPos+1, tmp.find(',', endPos)-endPos-1) << std::endl;
			} else {
				os << " - valid values:     " << tmp.substr(startPos, tmp.find(',', startPos)-startPos) << std::endl;
			}

		} while ((startPos = tmp.find(',', startPos)) != (int)std::string::npos);

	} else if (key.getType() == ConfigurationKey::BOOLEAN) {
		os << " - valid values:     0 = disabled" << std::endl;
		os << " - valid values:     1 = enabled" << std::endl;

	} else if (key.getType() == ConfigurationKey::STRING) {
		os << " - valid val regex:  " << tmp << std::endl;

	} else if (key.getValidValues().length()) {
		os << " - raw valid values: " << tmp << std::endl;
	}
}


// vim: ts=4 sw=4
