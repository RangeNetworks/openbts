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


ConfigurationTable::ConfigurationTable(const char* filename, const char *wCmdName)
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
}



bool ConfigurationTable::defines(const string& key)
{
	assert(mDB);
	ScopedLock lock(mLock);

	// Check the cache.
	checkCacheAge();
	ConfigurationMap::const_iterator where = mCache.find(key);
	if (where!=mCache.end()) return where->second.defined();

	// Check the database.
	char *value = NULL;
	sqlite3_single_lookup(mDB,"CONFIG","KEYSTRING",key.c_str(),"VALUESTRING",value);

	// Cache the result.
	if (value) {
		mCache[key] = ConfigurationRecord(value);
		free(value);
		return true;
	}
	
	mCache[key] = ConfigurationRecord(false);
	return false;
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

	// Nothing defined?
	if (!value) {
		// Cache the failure.
		mCache[key] = ConfigurationRecord(false);
		throw ConfigurationTableKeyNotFound(key);
	}

	// Cache the result.
	mCache[key] = ConfigurationRecord(value);
	free(value);

	// Leave mLock locked.  The caller holds it still.
	return mCache[key];
}



bool ConfigurationTable::isStatic(const string& key) const
{
	assert(mDB);
	unsigned stat;
	bool success = sqlite3_single_lookup(mDB,"CONFIG","KEYSTRING",key.c_str(),"STATIC",stat);
	if (success) return (bool)stat;
	return false;
}

bool ConfigurationTable::isRequired(const string& key) const
{
	assert(mDB);
	unsigned optional;
	bool success = sqlite3_single_lookup(mDB,"CONFIG","KEYSTRING",key.c_str(),"OPTIONAL",optional);
	if (success) return !((bool)optional);
	return false;
}




string ConfigurationTable::getStr(const string& key)
{
	// We need the lock because rec is a reference into the cache.
	try {
		ScopedLock lock(mLock);
		return lookup(key).value();
	} catch (ConfigurationTableKeyNotFound) {
		// Raise an alert and re-throw the exception.
		gLogEarly(LOG_ALERT, "configuration parameter %s has no defined value", key.c_str());
		throw ConfigurationTableKeyNotFound(key);
	}
}

string ConfigurationTable::getStr(const string& key, const char* defaultValue)
{
	try {
		ScopedLock lock(mLock);
		return lookup(key).value();
	} catch (ConfigurationTableKeyNotFound) {
		gLogEarly(LOG_NOTICE, "deinfing missing parameter %s with value %s", key.c_str(),defaultValue);
		set(key,defaultValue);
		return string(defaultValue);
	}
}


bool ConfigurationTable::getBool(const string& key)
{
	try {
		return getNum(key) != 0;
	} catch (ConfigurationTableKeyNotFound) {
		return false;
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
		gLogEarly(LOG_ALERT, "configuration parameter %s has no defined value", key.c_str());
		throw ConfigurationTableKeyNotFound(key);
	}
}


long ConfigurationTable::getNum(const string& key, long defaultValue)
{
	try {
		ScopedLock lock(mLock);
		return lookup(key).number();
	} catch (ConfigurationTableKeyNotFound) {
		gLogEarly(LOG_NOTICE, "deinfing missing parameter %s with value %ld", key.c_str(),defaultValue);
		set(key,defaultValue);
		return defaultValue;
	}
}


float ConfigurationTable::getFloat(const string& key)
{
	// We need the lock because rec is a reference into the cache.
	ScopedLock lock(mLock);
	return lookup(key).floatNumber();
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
		gLogEarly(LOG_ALERT, "configuration parameter %s has no defined value", key.c_str());
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


std::vector<string> ConfigurationTable::getVectorOfStrings(const string& key, const char* defaultValue){
	try {
		return getVectorOfStrings(key);
	} catch (ConfigurationTableKeyNotFound) {
		set(key,defaultValue);
		return getVectorOfStrings(key);
	}
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
		gLogEarly(LOG_ALERT, "configuration parameter %s has no defined value", key.c_str());
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


bool ConfigurationTable::unset(const string& key)
{
	assert(mDB);
	if (!defines(key)) return true;
	if (isRequired(key)) return false;

	ScopedLock lock(mLock);
	// Clear the cache entry and the database.
	ConfigurationMap::iterator where = mCache.find(key);
	if (where!=mCache.end()) mCache.erase(where);
	// Don't delete it; just set VALUESTRING to NULL.
	string cmd = "UPDATE CONFIG SET VALUESTRING=NULL WHERE KEYSTRING=='"+key+"'";
	return sqlite3_command(mDB,cmd.c_str());
}

bool ConfigurationTable::remove(const string& key)
{
	assert(mDB);
	if (isRequired(key)) return false;

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
		if (value) os << value << endl;
		else os << "(null)" << endl;
		src = sqlite3_run_query(mDB,stmt);
	}
	sqlite3_finalize(stmt);
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



// vim: ts=4 sw=4
