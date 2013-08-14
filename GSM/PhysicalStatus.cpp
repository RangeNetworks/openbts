/**@file Declarations for PhysicalStatus and related classes. */

/*
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012 Range Networks, Inc.
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

/*
 * Copyright 2010 Kestrel Signal Processing, Inc.
 * All rights reserved.
 *
 */


#include "PhysicalStatus.h"
#include <Logger.h>
#include <Globals.h>
#include <sqlite3.h>
#include <sqlite3util.h>

#include <NeighborTable.h>
#include <GSML3RRElements.h>
#include <GSMLogicalChannel.h>

#include <iostream>
#include <iomanip>
#include <math.h>
#include <string>

using namespace std;
using namespace GSM;


static const char* createPhysicalStatus = {
	"CREATE TABLE IF NOT EXISTS PHYSTATUS ("
		"CN_TN_TYPE_AND_OFFSET STRING PRIMARY KEY, "		// CnTn <chan>-<index>
		"ARFCN INTEGER DEFAULT NULL, "						// actual ARFCN
		"ACCESSED INTEGER DEFAULT 0, "						// Unix time of last update
		"RXLEV_FULL_SERVING_CELL INTEGER DEFAULT NULL, "	// from the most recent measurement report
		"RXLEV_SUB_SERVING_CELL INTEGER DEFAULT NULL, "		// from the most recent measurement report
		"RXQUAL_FULL_SERVING_CELL_BER FLOAT DEFAULT NULL, "	// from the most recent measurement report
		"RXQUAL_SUB_SERVING_CELL_BER FLOAT DEFAULT NULL, "	// from the most recent measurement report
		"RSSI FLOAT DEFAULT NULL, "							// RSSI relative to full scale input
		"TIME_ERR FLOAT DEFAULT NULL, "						// timing advance error in symbol periods
		"TRANS_PWR INTEGER DEFAULT NULL, "					// handset tx power in dBm
		"TIME_ADVC INTEGER DEFAULT NULL, "					// handset timing advance in symbol periods
		"FER FLOAT DEFAULT NULL, "							// uplink FER
		"NCELL_ARFCN INTEGER DEFAULT NULL, "				// ARFCN of strongest neighbor
		"NCELL_RSSI INTEGER DEFAULT NULL "				// RSSI of strongest neighbor
	")"
};

int PhysicalStatus::open(const char* wPath)
{
	int rc = sqlite3_open(wPath, &mDB);
	if (rc) {
		LOG(EMERG) << "Cannot open PhysicalStatus database at " << wPath << ": " << sqlite3_errmsg(mDB);
		sqlite3_close(mDB);
		mDB = NULL;
		return 1;
	}
	if (!sqlite3_command(mDB, createPhysicalStatus)) {
		LOG(EMERG) << "Cannot create TMSI table";
		return 1;
	}
	// Set high-concurrency WAL mode.
	if (!sqlite3_command(mDB,enableWAL)) {
		LOG(EMERG) << "Cannot enable WAL mode on database at " << wPath << ", error message: " << sqlite3_errmsg(mDB);
	}
	return 0;
}

PhysicalStatus::~PhysicalStatus()
{
	if (mDB) sqlite3_close(mDB);
}

bool PhysicalStatus::createEntry(const LogicalChannel* chan)
{
	assert(mDB);
	assert(chan);

	ScopedLock lock(mLock);

	const char* chanString = chan->descriptiveString();
	LOG(DEBUG) << chan->descriptiveString();

	/* Check to see if the key exists. */
	if (!sqlite3_exists(mDB, "PHYSTATUS", "CN_TN_TYPE_AND_OFFSET", chanString)) {
		/* No? Ok, it should now. */
		char query[500];
		sprintf(query, "INSERT INTO PHYSTATUS (CN_TN_TYPE_AND_OFFSET, ACCESSED) VALUES "
					   "(\"%s\", %u)",
				chanString, (unsigned)time(NULL));
		return sqlite3_command(mDB, query);
	}

	return false;
}

bool PhysicalStatus::setPhysical(const LogicalChannel* chan,
								const L3MeasurementResults& measResults)
{
	// TODO -- It would be better if the argument what just the channel
	// and the key was just the descriptiveString.

	assert(mDB);
	assert(chan);

	// If MEAS_VALID is true, we don't have valid measurements.
	// Really.  See GSM 04.08 10.5.2.20.
	if (measResults.MEAS_VALID()) return true; 

	ScopedLock lock(mLock);

	createEntry(chan);

	int CN = -1;
	if (measResults.NO_NCELL()>0) CN = measResults.BCCH_FREQ_NCELL(0);
	int ARFCN = -1;
	if (CN>=0) {
		std::vector<unsigned> ARFCNList = gNeighborTable.ARFCNList();
		size_t sz = ARFCNList.size();
		if (sz!=0) {
			if (CN<sz) ARFCN=ARFCNList[CN];
			else { LOG(NOTICE) << "BCCH index " << CN << " does not match ARFCN list of size " << sz; }
		} else {
			LOG(DEBUG) << "empty measurement list";
		}
	}

	char query[500];

	if (ARFCN<0) {
		sprintf(query,
			"UPDATE PHYSTATUS SET "
			"RXLEV_FULL_SERVING_CELL=%d, "
			"RXLEV_SUB_SERVING_CELL=%d, "
			"RXQUAL_FULL_SERVING_CELL_BER=%f, "
			"RXQUAL_SUB_SERVING_CELL_BER=%f, "
			"RSSI=%f, "
			"TIME_ERR=%f, "
			"TRANS_PWR=%u, "
			"TIME_ADVC=%u, "
			"FER=%f, "
			"ACCESSED=%u, "
			"ARFCN=%u "
			"WHERE CN_TN_TYPE_AND_OFFSET==\"%s\"",
			measResults.RXLEV_FULL_SERVING_CELL_dBm(),
			measResults.RXLEV_SUB_SERVING_CELL_dBm(),
			measResults.RXQUAL_FULL_SERVING_CELL_BER(),
			measResults.RXQUAL_SUB_SERVING_CELL_BER(),
			chan->RSSI(), chan->timingError(),
			chan->actualMSPower(), chan->actualMSTiming(),
			chan->FER(),
			(unsigned)time(NULL),
			chan->ARFCN(),
			chan->descriptiveString());
	} else {
		sprintf(query,
			"UPDATE PHYSTATUS SET "
			"RXLEV_FULL_SERVING_CELL=%d, "
			"RXLEV_SUB_SERVING_CELL=%d, "
			"RXQUAL_FULL_SERVING_CELL_BER=%f, "
			"RXQUAL_SUB_SERVING_CELL_BER=%f, "
			"RSSI=%f, "
			"TIME_ERR=%f, "
			"TRANS_PWR=%u, "
			"TIME_ADVC=%u, "
			"FER=%f, "
			"ACCESSED=%u, "
			"ARFCN=%u ,"
			"NCELL_ARFCN=%u, "
			"NCELL_RSSI=%d "
			"WHERE CN_TN_TYPE_AND_OFFSET==\"%s\"",
			measResults.RXLEV_FULL_SERVING_CELL_dBm(),
			measResults.RXLEV_SUB_SERVING_CELL_dBm(),
			measResults.RXQUAL_FULL_SERVING_CELL_BER(),
			measResults.RXQUAL_SUB_SERVING_CELL_BER(),
			chan->RSSI(), chan->timingError(),
			chan->actualMSPower(), chan->actualMSTiming(),
			chan->FER(),
			(unsigned)time(NULL),
			chan->ARFCN(),
			(unsigned)ARFCN,
			measResults.RXLEV_NCELL_dBm(0),
			chan->descriptiveString()
			);
	}

	LOG(DEBUG) << "Query: " << query;

	return sqlite3_command(mDB, query);
}

#if 0
void PhysicalStatus::dump(ostream& os) const
{
	sqlite3_stmt *stmt;
	sqlite3_prepare_statement(mDB, &stmt,
		"SELECT CN,TN,TYPE_AND_OFFSET,FER,RSSI,TRANS_PWR,TIME_ADVC,RXLEV_FULL_SERVING_CELL,RXQUAL_FULL_SERVING_CELL_BER FROM PHYSTATUS");

	while (sqlite3_run_query(mDB,stmt) == SQLITE_ROW) {
		os << setw(2) << sqlite3_column_int(stmt, 0) << " " << sqlite3_column_int(stmt, 1);
		os << " " << setw(9) << (TypeAndOffset)sqlite3_column_int(stmt, 2);

		char buffer[1024];
		sprintf(buffer, "%10d %5.2f %4d %5d %4d",
			sqlite3_column_int(stmt, 3),
			100.0*sqlite3_column_double(stmt, 4), (int)round(sqlite3_column_double(stmt, 5)),
			sqlite3_column_int(stmt, 6), sqlite3_column_int(stmt, 7));
		os << " " << buffer;

		sprintf(buffer, "%5d %5.2f",
			sqlite3_column_int(stmt, 8), 100.0*sqlite3_column_double(stmt, 9));
		os << " " << buffer;

		os << endl;
	}
	sqlite3_finalize(stmt);
}
#endif


// vim: ts=4 sw=4
