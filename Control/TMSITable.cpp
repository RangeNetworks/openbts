/*
* Copyright 2008, 2009, 2010, 2011 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
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


#include "TMSITable.h"
#include <Logger.h>
#include <Globals.h>
#include <sqlite3.h>
#include <sqlite3util.h>

#include <GSML3MMMessages.h>

#include <string>
#include <iostream>
#include <iomanip>

using namespace std;
using namespace Control;


static const char* createTMSITable = {
	"CREATE TABLE IF NOT EXISTS TMSI_TABLE ("
		"TMSI INTEGER PRIMARY KEY AUTOINCREMENT, "
		"CREATED INTEGER NOT NULL, "	// Unix time of record creation
		"ACCESSED INTEGER NOT NULL, "	// Unix time of last encounter
		"APP_FLAGS INTEGER DEFAULT 0, "	// Application-specific flags
		"IMSI TEXT UNIQUE NOT NULL, "	// IMSI
		"IMEI TEXT, "					// IMEI
		"L3TI INTEGER DEFAULT 0,"		// L3 transaction identifier
		"A5_SUPPORT INTEGER, "			// encryption support
		"POWER_CLASS INTEGER, "			// power class
		"OLD_TMSI INTEGER, "			// previous TMSI in old network
		"PREV_MCC INTEGER, "			// previous network MCC
		"PREV_MNC INTEGER, "			// previous network MNC
		"PREV_LAC INTEGER, "			// previous network LAC
		"DEG_LAT FLOAT, "				// RRLP result
		"DEG_LONG FLOAT "				// RRLP result
	")"
};




int TMSITable::open(const char* wPath) 
{
	int rc = sqlite3_open(wPath,&mDB);
	if (rc) {
		LOG(EMERG) << "Cannot open TMSITable database at " << wPath << ": " << sqlite3_errmsg(mDB);
		sqlite3_close(mDB);
		mDB = NULL;
		return 1;
	}
	if (!sqlite3_command(mDB,createTMSITable)) {
		LOG(EMERG) << "Cannot create TMSI table";
        return 1;
	}
    return 0;
}



TMSITable::~TMSITable()
{
	if (mDB) sqlite3_close(mDB);
}




unsigned TMSITable::assign(const char* IMSI, const GSM::L3LocationUpdatingRequest* lur)
{
	// Create or find an entry based on IMSI.
	// Return assigned TMSI.
	assert(mDB);

	LOG(DEBUG) << "IMSI=" << IMSI;
	// Is there already a record?
	unsigned TMSI;
	if (sqlite3_single_lookup(mDB,"TMSI_TABLE","IMSI",IMSI,"TMSI",TMSI)) {
		LOG(DEBUG) << "found TMSI " << TMSI;
		touch(TMSI);
		return TMSI;
	}

	// Create a new record.
	LOG(NOTICE) << "new entry for IMSI " << IMSI;
	char query[1000];
	unsigned now = (unsigned)time(NULL);
	if (!lur) {
		sprintf(query,
				"INSERT INTO TMSI_TABLE (IMSI,CREATED,ACCESSED) "
				"VALUES ('%s',%u,%u)",
				IMSI,now,now);
	} else {
		const GSM::L3LocationAreaIdentity &lai = lur->LAI();
		const GSM::L3MobileIdentity &mid = lur->mobileID();
		if (mid.type()==GSM::TMSIType) {
			sprintf(query,
					"INSERT INTO TMSI_TABLE (IMSI,CREATED,ACCESSED,PREV_MCC,PREV_MNC,PREV_LAC,OLD_TMSI) "
					"VALUES ('%s',%u,%u,%u,%u,%u,%u)",
					IMSI,now,now,lai.MCC(),lai.MNC(),lai.LAC(),mid.TMSI());
		} else {
			sprintf(query,
					"INSERT INTO TMSI_TABLE (IMSI,CREATED,ACCESSED,PREV_MCC,PREV_MNC,PREV_LAC) "
					"VALUES ('%s',%u,%u,%u,%u,%u)",
					IMSI,now,now,lai.MCC(),lai.MNC(),lai.LAC());
		}
	}
	if (!sqlite3_command(mDB,query)) {
		LOG(ALERT) << "TMSI creation failed";
		return 0;
	}
	if (!sqlite3_single_lookup(mDB,"TMSI_TABLE","IMSI",IMSI,"TMSI",TMSI)) {
		LOG(ERR) << "TMSI database inconsistancy";
		return 0;
	}
	return TMSI;
}
	


void TMSITable::touch(unsigned TMSI) const
{
	// Update timestamp.
	char query[100];
	sprintf(query,"UPDATE TMSI_TABLE SET ACCESSED = %u WHERE TMSI == %u",
		(unsigned)time(NULL),TMSI);
	sqlite3_command(mDB,query);
}



// Returned string must be free'd by the caller.
char* TMSITable::IMSI(unsigned TMSI) const
{
	char* IMSI = NULL;
	if (sqlite3_single_lookup(mDB,"TMSI_TABLE","TMSI",TMSI,"IMSI",IMSI)) touch(TMSI);
	return IMSI;
}

unsigned TMSITable::TMSI(const char* IMSI) const
{
	unsigned TMSI=0;
	if (sqlite3_single_lookup(mDB,"TMSI_TABLE","IMSI",IMSI,"TMSI",TMSI)) touch(TMSI);
	return TMSI;
}



void printAge(unsigned seconds, ostream& os)
{
	static const unsigned k=5;
	os << setw(4);
	if (seconds<k*60) {
		os << seconds << 's';
		return;
	}
	unsigned minutes = (seconds+30) / 60;
	if (minutes<k*60) {
		os << minutes << 'm';
		return;
	}
	unsigned hours = (minutes+30) / 60;
	if (hours<k*24) {
		os << hours << 'h';
		return;
	}
	os << (hours+12)/24 << 'd';
}


void TMSITable::dump(ostream& os) const
{
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(mDB,&stmt,"SELECT TMSI,IMSI,CREATED,ACCESSED FROM TMSI_TABLE")) {
		LOG(ERR) << "sqlite3_prepare_statement failed";
		return;
	}
	time_t now = time(NULL);
	while (sqlite3_run_query(mDB,stmt)==SQLITE_ROW) {
		os << hex << setw(8) << sqlite3_column_int64(stmt,0) << ' ' << dec;
		os << sqlite3_column_text(stmt,1) << ' ';
		printAge(now-sqlite3_column_int(stmt,2),os); os << ' ';
		printAge(now-sqlite3_column_int(stmt,3),os); os << ' ';
		os << endl;
	}
	sqlite3_finalize(stmt);
}



void TMSITable::clear()
{
	sqlite3_command(mDB,"DELETE FROM TMSI_TABLE WHERE 1");
}



bool TMSITable::IMEI(const char* IMSI, const char *IMEI)
{
	char query[100];
	sprintf(query,"UPDATE TMSI_TABLE SET IMEI=\"%s\",ACCESSED=%u WHERE IMSI=\"%s\"",
		IMEI,(unsigned)time(NULL),IMSI);
	return sqlite3_command(mDB,query);
}



bool TMSITable::classmark(const char* IMSI, const GSM::L3MobileStationClassmark2& classmark)
{
	int A5Bits = (classmark.A5_1()<<2) + (classmark.A5_2()<<1) + classmark.A5_3();
	char query[100];
	sprintf(query,
		"UPDATE TMSI_TABLE SET A5_SUPPORT=%u,ACCESSED=%u,POWER_CLASS=%u "
		" WHERE IMSI=\"%s\"",
		A5Bits,(unsigned)time(NULL),classmark.powerClass(),IMSI);
	return sqlite3_command(mDB,query);
}



unsigned TMSITable::nextL3TI(const char* IMSI)
{
	// FIXME -- This should be a single atomic operation.
	unsigned l3ti;
	if (!sqlite3_single_lookup(mDB,"TMSI_TABLE","IMSI",IMSI,"L3TI",l3ti)) {
		LOG(ERR) << "cannot read L3TI from TMSI_TABLE, using randon L3TI";
		return random() % 8;
	}
	// Note that TI=7 is a reserved value, so value values are 0-6.  See GSM 04.07 11.2.3.1.3.
	unsigned next = (l3ti+1) % 7;
	char query[200];
	sprintf(query,"UPDATE TMSI_TABLE SET L3TI=%u,ACCESSED=%u WHERE IMSI='%s'",
		next, (unsigned)time(NULL),IMSI);
	if (!sqlite3_command(mDB,query)) {
		LOG(ALERT) << "cannot write L3TI to TMSI_TABLE";
	}
	return next;
}



// vim: ts=4 sw=4
