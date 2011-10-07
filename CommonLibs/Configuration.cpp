/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
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
#include <fstream>
#include <iostream>
#include <string.h>
#include <syslog.h>

using namespace std;


static const char* createConfigTable = {
	"CREATE TABLE IF NOT EXISTS CONFIG ("
		"KEYSTRING TEXT UNIQUE NOT NULL, "
		"VALUESTRING TEXT, "
		"STATIC INTEGER DEFAULT 0, "
		"OPTIONAL INTEGER DEFAULT 0, "
		"COMMENTS TEXT DEFAULT ''"
	")"
};


ConfigurationTable::ConfigurationTable(const char* filename)
{
	// Connect to the database.
	int rc = sqlite3_open(filename,&mDB);
	if (rc) {
		cerr << "Cannot open configuration database: " << sqlite3_errmsg(mDB);
		sqlite3_close(mDB);
		mDB = NULL;
		return;
	}
	// Create the table, if needed.
	if (!sqlite3_command(mDB,createConfigTable)) {
		cerr << "Cannot create configuration table:" << sqlite3_errmsg(mDB);
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
		// Unlock the mutex before throwing the exception.
		mLock.unlock();
		syslog(LOG_ALERT, "configuration key %s not found", key.c_str());
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
		// Unlock the mutex before throwing the exception.
		mLock.unlock();
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
	ScopedLock lock(mLock);
	return lookup(key).value();
}

string ConfigurationTable::getStr(const string& key, const char* defaultValue)
{
	try {
		return getStr(key);
	} catch (ConfigurationTableKeyNotFound) {
		set(key,defaultValue);
		return string(defaultValue);
	}
}


long ConfigurationTable::getNum(const string& key)
{
	// We need the lock because rec is a reference into the cache.
	ScopedLock lock(mLock);
	return lookup(key).number();
}


long ConfigurationTable::getNum(const string& key, long defaultValue)
{
	try {
		return getNum(key);
	} catch (ConfigurationTableKeyNotFound) {
		set(key,defaultValue);
		return defaultValue;
	}
}



std::vector<unsigned> ConfigurationTable::getVector(const string& key)
{
	// Look up the string.
	mLock.lock();
	const ConfigurationRecord& rec = lookup(key);
	char* line = strdup(rec.value().c_str());
	mLock.unlock();
	// Parse the string.
	std::vector<unsigned> retVal;
	char *lp=line;
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
	// Is it there already?
	char * oldValue = NULL;
	bool exists = sqlite3_single_lookup(mDB,"CONFIG","KEYSTRING",key.c_str(),"VALUESTRING",oldValue);
	// Update or insert as appropriate.
	string cmd;
	if (exists) cmd = "UPDATE CONFIG SET VALUESTRING=\""+value+"\" WHERE KEYSTRING==\""+key+"\"";
	else cmd = "INSERT INTO CONFIG (KEYSTRING,VALUESTRING,OPTIONAL) VALUES (\"" + key + "\",\"" + value + "\",1)";
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
	string cmd = "INSERT INTO CONFIG (KEYSTRING) VALUES (\"" + key + "\")";
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



// vim: ts=4 sw=4
