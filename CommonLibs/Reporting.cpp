/**@file Module for performance-reporting mechanisms. */
/*
* Copyright 2012 Range Networks, Inc.
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

#include "Reporting.h"
#include "Logger.h"
#include <stdio.h>
#include <string.h>

static const char* createReportingTable = {
	"CREATE TABLE IF NOT EXISTS REPORTING ("
		"NAME TEXT UNIQUE NOT NULL, "
		"VALUE INTEGER DEFAULT 0, "
		"CLEAREDTIME INTEGER NOT NULL, "
		"UPDATETIME INTEGER DEFAULT 0 "
	")"
};


ReportingTable::ReportingTable(const char* filename)
{
	gLogEarly(LOG_INFO | mFacility, "opening reporting table from path %s", filename);
	// Connect to the database.
	int rc = sqlite3_open(filename,&mDB);
	if (rc) {
		gLogEarly(LOG_EMERG | mFacility, "cannot open reporting database at %s, error message: %s", filename, sqlite3_errmsg(mDB));
		sqlite3_close(mDB);
		mDB = NULL;
		return;
	}
	// Create the table, if needed.
	if (!sqlite3_command(mDB,createReportingTable)) {
		gLogEarly(LOG_EMERG | mFacility, "cannot create reporting table in database at %s, error message: %s", filename, sqlite3_errmsg(mDB));
	}
}


bool ReportingTable::create(const char* paramName)
{
	char cmd[200];
	sprintf(cmd,"INSERT OR IGNORE INTO REPORTING (NAME,CLEAREDTIME) VALUES (\"%s\",%ld)", paramName, time(NULL));
	if (!sqlite3_command(mDB,cmd)) {
		gLogEarly(LOG_CRIT|mFacility, "cannot create reporting parameter %s, error message: %s", paramName, sqlite3_errmsg(mDB));
		return false;
	}
	return true;
}



bool ReportingTable::incr(const char* paramName)
{
	char cmd[200];
	sprintf(cmd,"UPDATE REPORTING SET VALUE=VALUE+1, UPDATETIME=%ld WHERE NAME=\"%s\"", time(NULL), paramName);
	if (!sqlite3_command(mDB,cmd)) {
		gLogEarly(LOG_CRIT|mFacility, "cannot increment reporting parameter %s, error message: %s", paramName, sqlite3_errmsg(mDB));
		return false;
	}
	return true;
}



bool ReportingTable::max(const char* paramName, unsigned newVal)
{
	char cmd[200];
	sprintf(cmd,"UPDATE REPORTING SET VALUE=MAX(VALUE,%u), UPDATETIME=%ld WHERE NAME=\"%s\"", newVal, time(NULL), paramName);
	if (!sqlite3_command(mDB,cmd)) {
		gLogEarly(LOG_CRIT|mFacility, "cannot maximize reporting parameter %s, error message: %s", paramName, sqlite3_errmsg(mDB));
		return false;
	}
	return true;
}


bool ReportingTable::clear(const char* paramName)
{
	char cmd[200];
	sprintf(cmd,"UPDATE REPORTING SET VALUE=0, UPDATETIME=0, CLEAREDTIME=%ld WHERE NAME=\"%s\"", time(NULL), paramName);
	if (!sqlite3_command(mDB,cmd)) {
		gLogEarly(LOG_CRIT|mFacility, "cannot clear reporting parameter %s, error message: %s", paramName, sqlite3_errmsg(mDB));
		return false;
	}
	return true;
}


bool ReportingTable::create(const char* baseName, unsigned minIndex, unsigned maxIndex)
{
	size_t sz = strlen(baseName);
	for (unsigned i = minIndex; i<=maxIndex; i++) {
		char name[sz+10];
		sprintf(name,"%s.%u",baseName,i);
		if (!create(name)) return false;
	}
	return true;
}

bool ReportingTable::incr(const char* baseName, unsigned index)
{
	char name[strlen(baseName)+10];
	sprintf(name,"%s.%u",baseName,index);
	return incr(name);
}


bool ReportingTable::max(const char* baseName, unsigned index, unsigned newVal)
{
	char name[strlen(baseName)+10];
	sprintf(name,"%s.%u",baseName,index);
	return max(name,newVal);
}


bool ReportingTable::clear(const char* baseName, unsigned index)
{
	char name[strlen(baseName)+10];
	sprintf(name,"%s.%u",baseName,index);
	return clear(name);
}




