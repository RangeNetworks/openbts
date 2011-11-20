/*
* Copyright 2009, 2010 Free Software Foundation, Inc.
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

#include <string.h>
#include <cstdio>
#include <fstream>
#include <string>

#include "Configuration.h"
#include "Logger.h"


using namespace std;

// Reference to a global config table, used all over the system.
extern ConfigurationTable gConfig;


/**@ The global alarms table. */
//@{
Mutex           alarmsLock;
list<string>    alarmsList;
void            addAlarm(const string&);
//@}




/** Names of the logging levels. */
const char *levelNames[] = {
	"EMERG", "ALERT", "CRIT", "ERR", "WARNING", "NOTICE", "INFO", "DEBUG"
};
int numLevels = 8;


/** Given a string, return the corresponding level name. */
int lookupLevel(const string& name)
{
	// Reverse search, since the numerically larger levels are more common.
	for (int i=numLevels-1; i>=0; i--) {
		if (name == levelNames[i]) return i;
	}
	// This should never be called with a bogus name.
	LOG(ERR) << "undefined logging level " << name << "defaulting to ERR";
	return LOG_ERR;
}


int getLoggingLevel(const char* filename)
{
	// Default level?
	if (!filename) return lookupLevel(gConfig.getStr("Log.Level"));

	// This can afford to be inefficient since it is not called that often.
	const string keyName = string("Log.Level.") + string(filename);
	if (gConfig.defines(keyName)) return lookupLevel(gConfig.getStr(keyName));
	return lookupLevel(gConfig.getStr("Log.Level"));
}



int gGetLoggingLevel(const char* filename)
{
	// This is called a lot and needs to be efficient.

	static Mutex sLogCacheLock;
	static map<uint64_t,int>  sLogCache;
	static unsigned sCacheCount;
	static const unsigned sCacheRefreshCount = 1000;

	if (filename==NULL) return gGetLoggingLevel("");

	HashString hs(filename);
	uint64_t key = hs.hash();

	sLogCacheLock.lock();
	// Time for a cache flush?
	if (sCacheCount>sCacheRefreshCount) {
		sLogCache.clear();
		sCacheCount=0;
	}
	// Is it cached already?
	map<uint64_t,int>::const_iterator where = sLogCache.find(key);
	sCacheCount++;
	if (where!=sLogCache.end()) {
		int retVal = where->second;
		sLogCacheLock.unlock();
		return retVal;
	}
	// Look it up in the config table and cache it.
	// FIXME: Figure out why unlock and lock below fix the config table deadlock.
	sLogCacheLock.unlock();
	int level = getLoggingLevel(filename);
	sLogCacheLock.lock();
	sLogCache.insert(pair<uint64_t,int>(key,level));
	sLogCacheLock.unlock();
	return level;
}





// copies the alarm list and returns it. list supposed to be small.
list<string> gGetLoggerAlarms()
{
    alarmsLock.lock();
    list<string> ret;
    // excuse the "complexity", but to use std::copy with a list you need
    // an insert_iterator - copy technically overwrites, doesn't insert.
    insert_iterator< list<string> > ii(ret, ret.begin());
    copy(alarmsList.begin(), alarmsList.end(), ii);
    alarmsLock.unlock();
    return ret;
}

/** Add an alarm to the alarm list. */
void addAlarm(const string& s)
{
    alarmsLock.lock();
    alarmsList.push_back(s);
	unsigned maxAlarms = gConfig.getNum("Log.Alarms.Max");
    while (alarmsList.size() > maxAlarms) alarmsList.pop_front();
    alarmsLock.unlock();
}


Log::~Log()
{
	if (mDummyInit) return;
	// Anything at or above LOG_CRIT is an "alarm".
	// Save alarms in the local list and echo them to stderr.
	if (mPriority <= LOG_CRIT) {
		addAlarm(mStream.str().c_str());
		cerr << mStream.str() << endl;
	}
	// Current logging level was already checked by the macro.
	// So just log.
	syslog(mPriority, "%s", mStream.str().c_str());
}


Log::Log(const char* name, const char* level, int facility)
{
	mDummyInit = true;
	gLogInit(name, level, facility);
}


ostringstream& Log::get()
{
	assert(mPriority<numLevels);
	mStream << levelNames[mPriority] <<  ' ';
	return mStream;
}



void gLogInit(const char* name, const char* level, int facility)
{
	// Set the level.
	if (level) {
		gConfig.set("Log.Level",level);
	} else {
		if (!gConfig.defines("Log.Level")) {
			gConfig.set("Log.Level","WARNING");
		}
	}

	// Define other logging parameters in the global config.
	if (!gConfig.defines("Log.Alarms.Max")) {
		gConfig.set("Log.Alarms.Max",DEFAULT_MAX_ALARMS);
	}

	// Open the log connection.
	openlog(name,0,facility);
}




// vim: ts=4 sw=4
