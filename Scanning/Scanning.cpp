/*
* Copyright 2011, 2014 Range Networks, Inc.
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

#include "Scanning.h"
#include <time.h>
#include <sqlite3.h>
#include <sqlite3util.h>

#include <iostream>

#include <Logger.h>

#include <GSML3RRMessages.h>


using namespace std;




static const char* createEnvironmentMap = {
	"CREATE TABLE IF NOT EXISTS ENVIRONMENT_MAP ("
	"KEYSTRING TEXT UNIQUE NOT NULL, "		// key string of MCC:MNC:LAC:CI
	"TIMESTAMP INTEGER NOT NULL, "			// Unix timestamp of last update
	"BAND INTEGER NOT NULL, "				// operating band (850, 900, 1800, 1900)
	"C0 INTEGER NOT NULL, "					// C0 ARFCN
	"MCC INTEGER NOT NULL, "				// GSM MCC
	"MNC INTEGER NOT NULL, "				// GSM MNC
	"LAC INTEGER NOT NULL, "				// GSM LAC
	"CI INTEGER NOT NULL, "					// GSM CI
	"FREQ_OFFSET FLOAT NOT NULL, "			// C0 carrier offset in Hz
	"NEIGHBORS TEXT NOT NULL, "				// GSM neighbor list
	"CA TEXT NOT NULL "					// GSM cell allocation
	")"
};

static const char* createSpectrumMap = {
	"CREATE TABLE IF NOT EXISTS SPECTRUM_MAP ("
	"BAND INTEGER NOT NULL, "				// operating band (850, 900, 1800, 1900)
	"TIMESTAMP INTEGER NOT NULL, "			// Unix time of the measurement
	"ARFCN INTEGER NOT NULL, "				// ARFCN in question
	"RSSI FLOAT NOT NULL, "					// RSSI in dBm
	"LINK TEXT CHECK(LINK IN ('up', 'down')) NOT NULL, "					// Link direction
	"FREQ FLOAT NOT NULL"					// Frequency
	")"
};


const int SpectrumMap::LinkDirection::Up = 1;
const int SpectrumMap::LinkDirection::Down = 2;

SpectrumMap::SpectrumMap(const char* path)
{
	int rc = sqlite3_open(path,&mDB);
	if (rc) {
		LOG(ALERT) << "Cannot open environment map database: " << sqlite3_errmsg(mDB);
		sqlite3_close(mDB);
		mDB = NULL;
		return;
	}
	if (!sqlite3_command(mDB,createSpectrumMap)) {
		LOG(ALERT) << "Cannot create spectrum map";
	}
}

SpectrumMap::~SpectrumMap()
{
	if (mDB) sqlite3_close(mDB);
}


void SpectrumMap::clear()
{
	sqlite3_command(mDB,"DELETE FROM SPECTRUM_MAP WHERE 1");
}

void SpectrumMap::power(GSM::GSMBand band, unsigned ARFCN, float freq, LinkDirection& linkDir, float dBm)
{
	char q1[200];
	sprintf(q1,"INSERT OR REPLACE INTO SPECTRUM_MAP (BAND,TIMESTAMP,ARFCN,RSSI,FREQ,LINK) "
				"VALUES (%u,%u,%u,%f,%f,'%s')",
				(int)band,(unsigned)time(NULL),ARFCN,dBm,freq,linkDir.string());
	bool s = sqlite3_command(mDB,q1);
	if (!s) LOG(ALERT) << "write to spectrum map failed";
}

void SpectrumMap::power(GSM::GSMBand band, unsigned ARFCN, LinkDirection& linkDir, float dBm)
{
	float frequency = 1000.0F;

	if (linkDir == SpectrumMap::LinkDirection::Up) {
		frequency *= GSM::uplinkFreqKHz(band, ARFCN);
	} else if (linkDir == SpectrumMap::LinkDirection::Down) {
		frequency *= GSM::downlinkFreqKHz(band, ARFCN);
	} else {
		// Invalid direction
		return;
	}

	power(band, ARFCN, frequency, linkDir, dBm);
}

/*void SpectrumMap::power(GSM::GSMBand band, unsigned ARFCN, float freq, float dBm)
{

}*/

ARFCNList SpectrumMap::topPower(GSM::GSMBand band, unsigned count) const
{
	char q[200];
	sprintf(q,"SELECT ARFCN FROM SPECTRUM_MAP WHERE BAND=%u ORDER BY RSSI DESC", band);

	ARFCNList retVal;
	sqlite3_stmt *stmt;
	sqlite3_prepare_statement(mDB,&stmt,q);
	int src = sqlite3_run_query(mDB,stmt);
	while ((retVal.size()<count) && src==SQLITE_ROW) {
		unsigned ARFCN = (unsigned)sqlite3_column_int(stmt,0);
		retVal.push_back(ARFCN);
		src = sqlite3_run_query(mDB,stmt);
	}
	sqlite3_finalize(stmt);
	return retVal;
}


ARFCNList SpectrumMap::minimumPower(GSM::GSMBand band, unsigned count) const
{
	char q[200];
	sprintf(q,"SELECT ARFCN FROM SPECTRUM_MAP WHERE BAND=%u ORDER BY RSSI", band);

	ARFCNList retVal;
	sqlite3_stmt *stmt;
	sqlite3_prepare_statement(mDB,&stmt,q);
	int src = sqlite3_run_query(mDB,stmt);
	while ((retVal.size()<count) && src==SQLITE_ROW) {
		unsigned ARFCN = (unsigned)sqlite3_column_int(stmt,0);
		retVal.push_back(ARFCN);
		src = sqlite3_run_query(mDB,stmt);
	}
	sqlite3_finalize(stmt);
	return retVal;
}





void ScanList::add(const ARFCNList& ARFCNs)
{
	for (ARFCNList::const_iterator i=ARFCNs.begin(); i!=ARFCNs.end(); ++i) {
		add(*i);
	}
}


void ScanList::add(const std::vector<unsigned>& ARFCNs)
{
	for (unsigned i=0; i<ARFCNs.size(); i++) {
		add(ARFCNs[i]);
	}
}


void ScanList::add(unsigned ARFCN)
{
	if (alreadyListed(ARFCN)) return;
	mNewARFCNs.push_back(ARFCN);
}


bool ScanList::alreadyListed(unsigned ARFCN) const
{
	for (ARFCNList::const_iterator i=mNewARFCNs.begin(); i!=mNewARFCNs.end(); ++i) {
		if (*i == ARFCN) return true;
	}
	for (ARFCNList::const_iterator i=mOldARFCNs.begin(); i!=mOldARFCNs.end(); ++i) {
		if (*i == ARFCN) return true;
	}
	return false;
}

int ScanList::next()
{
	if (mNewARFCNs.size()==0) return -1;
	unsigned ARFCN = mNewARFCNs.front();
	mNewARFCNs.pop_front();
	mOldARFCNs.push_back(ARFCN);
	return ARFCN;
}


void ScanList::clear()
{
	mNewARFCNs.clear();
	mOldARFCNs.clear();
}


// vim: ts=4 sw=4
