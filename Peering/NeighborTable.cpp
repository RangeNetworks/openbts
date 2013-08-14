/*
 * Copright 2011 Range Networks, Inc.
 * All rights reserved.
*/

#include "NeighborTable.h"
#include "Peering.h"

#include <Logger.h>
#include <Globals.h>

#include <sqlite3.h>
#include <sqlite3util.h>

#include <time.h>

#include <sstream>

using namespace Peering;
using namespace std;






static const char* createNeighborTable = {
	"CREATE TABLE IF NOT EXISTS NEIGHBOR_TABLE ("
		"IPADDRESS TEXT UNIQUE NOT NULL, "	// IP address of peer BTS
		"UPDATED INTEGER DEFAULT 0, "	// timestamp of last update
		"HOLDOFF INTEGER DEFAULT 0, "	// hold off until after this time
		"C0 INTEGER DEFAULT NULL, "		// peer BTS C0 ARFCN
		"BSIC INTEGER DEFAULT NULL"		// peer BTS BSIC
	")"
};






void NeighborTable::NeighborTableInit(const char* wPath)
{
	int rc = sqlite3_open(wPath,&mDB);
	if (rc) {
		LOG(ALERT) << "Cannot open NeighborTable database: " << sqlite3_errmsg(mDB);
		sqlite3_close(mDB);
		mDB = NULL;
		return;
	}
	if (!sqlite3_command(mDB,createNeighborTable)) {
		LOG(ALERT) << "Cannot create Neighbor table";
	}
	// Set high-concurrency WAL mode.
	if (!sqlite3_command(mDB,enableWAL)) {
		LOG(ALERT) << "Cannot enable WAL mode on database at " << wPath << ", error message: " << sqlite3_errmsg(mDB);
	}

	// Fill the database.
	fill();
}



void NeighborTable::fill()
{
	mConfigured.clear();
	// Stuff the neighbor ip addresses into the table without any other info.
	// Let existing information persist for current neighbors.
	// NeighborTable::refresh() will get updated infomation when it's available.
	vector<string> neighbors = gConfig.getVectorOfStrings("GSM.Neighbors");
	LOG(DEBUG) << "neighbor list length " << neighbors.size();
	unsigned short port = gConfig.getNum("Peering.Port");
	for (unsigned int i = 0; i < neighbors.size(); i++) {
		struct sockaddr_in address;
		const char *host = neighbors[i].c_str();
		LOG(DEBUG) << "resolving host name for " << host;
		if (!resolveAddress(&address, host, port)) {
			LOG(CRIT) << "cannot resolve host name for " << host;
			// these two seem to want to get set even if addNeighbor isn't called
			mBCCSet = getBCCSet();
			mARFCNList = getARFCNs();
			continue;
		}
		addNeighbor(&address);
	}
	// get a list of ipaddresses in neighbor table that aren't configured
	vector<string> toBeDeleted;
	sqlite3_stmt *stmt;
	const char *query = "SELECT IPADDRESS FROM NEIGHBOR_TABLE";
	if (sqlite3_prepare_statement(mDB,&stmt,query)) {
		LOG(ALERT) << "read of neighbor table failed: " << query;
		return;
	}
	int src = sqlite3_step(stmt);
	while (src==SQLITE_ROW) {
		const char* ipaddress = (const char*)sqlite3_column_text(stmt,0);
		if (!ipaddress) {
			LOG(ALERT) << "null address in neighbor table";
			src = sqlite3_step(stmt);
			continue;
		}
		if (mConfigured.find(ipaddress) == mConfigured.end()) {
			toBeDeleted.push_back(ipaddress);
		}
		src = sqlite3_step(stmt);
	}
	sqlite3_finalize(stmt);
	// remove entries in neighbor table that aren't configured
	char query2[400];
	for (unsigned int i = 0; i < toBeDeleted.size(); i++) {
		sprintf(query2, "delete from NEIGHBOR_TABLE where IPADDRESS = '%s'", toBeDeleted[i].c_str());
		sqlite3_command(mDB, query2);
	}
}


void NeighborTable::addNeighbor(const struct ::sockaddr_in* address)
{
	ScopedLock lock(mLock);
	// Get a string for the sockaddr_in.
	char addrString[256];
	const char *ret = inet_ntop(AF_INET,&(address->sin_addr),addrString,255);
	if (!ret) {
		LOG(ERR) << "cannot parse peer socket address";
		return;
	}
	LOG(DEBUG) << "adding " << addrString << ":" << ntohs(address->sin_port) << " to neighbor table";

	char query[200];
	sprintf(query,
		"INSERT OR IGNORE INTO NEIGHBOR_TABLE (IPADDRESS) "
		"VALUES ('%s:%d')",
		addrString,(int)ntohs(address->sin_port));
	sqlite3_command(mDB,query);
	// flag the entry as configured
	char *p = 1+index(query, '\'');
	char *q = index(p, '\'');
	*q = 0;
	mConfigured.insert(p);

	// update mBCCSet
	mBCCSet = getBCCSet();

	// update mARFCNList and check for a change
	mARFCNList = getARFCNs();
}





bool NeighborTable::addInfo(const struct ::sockaddr_in* address, unsigned updated, unsigned C0, unsigned BSIC)
{
	ScopedLock lock(mLock);
	// Get a string for the sockaddr_in.
	char addrString[256];
	const char *ret = inet_ntop(AF_INET,&(address->sin_addr),addrString,255);
	if (!ret) {
		LOG(ERR) << "cannot parse peer socket address";
		return false;
	}
	LOG(DEBUG) << "updating " << addrString << ":" << ntohs(address->sin_port) << " in neighbor table";

	char query[200];
	unsigned int dummy;  // C0 is arbitrary integer column.  just want to know if ipaddress is in table.
	sprintf(query, "%s:%d", addrString,(int)ntohs(address->sin_port));
	if (!sqlite3_single_lookup(mDB, "NEIGHBOR_TABLE", "IPADDRESS", query, "C0", dummy)) {
		LOG(NOTICE) << "Ignoring unsolicited 'RSP NEIGHBOR_PARAMS' from " << query;
		return false;
	}
	sprintf(query,
		"REPLACE INTO NEIGHBOR_TABLE (IPADDRESS,UPDATED,C0,BSIC,HOLDOFF) "
		"VALUES ('%s:%d',%u,%u,%u,0) ",
		addrString,(int)ntohs(address->sin_port),updated,C0,BSIC);
	if (!sqlite3_command(mDB,query)) {
		LOG(ALERT) << "write to neighbor table failed: " << query;
		return false;
	}

	// update mBCCSet
	mBCCSet = getBCCSet();

	// update mARFCNList and check for a change
	std::vector<unsigned> newARFCNs = getARFCNs();
	bool change = (newARFCNs!=mARFCNList);
	if (change) mARFCNList = newARFCNs;

	return change;
}


void NeighborTable::refresh()
{
	fill();
	time_t now = time(NULL);
	time_t then = now - gConfig.getNum("Peering.Neighbor.RefreshAge");
	char query[400];
	sprintf(query,"SELECT IPADDRESS FROM NEIGHBOR_TABLE WHERE UPDATED < %u",(unsigned)then);
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(mDB,&stmt,query)) {
		LOG(ALERT) << "read of neighbor table failed: " << query;
		return;
	}
	int src = sqlite3_step(stmt);
	while (src==SQLITE_ROW) {
		const char* addrString = (const char*)sqlite3_column_text(stmt,0);
		if (!addrString) {
			LOG(ALERT) << "null address in neighbor table";
			src = sqlite3_step(stmt);
			continue;
		}
		struct sockaddr_in address;
		if (!resolveAddress(&address, addrString)) {
			LOG(ALERT) << "cannot resolve neighbor address " << addrString;
			src = sqlite3_step(stmt);
			continue;
		}
		LOG(INFO) << "sending neighbor param request to " << addrString;
		gPeerInterface.sendNeighborParamsRequest(&address);
		src = sqlite3_step(stmt);
	}
	sqlite3_finalize(stmt);
}


char* NeighborTable::getAddress(unsigned BCCH_FREQ_NCELL, unsigned BSIC)
{
	// There is a potential race condition here where mARFCNList could have changed
	// between the sending of SI5 and the receipt of the corresponding measurement report.
	// That will not be a serious problem as long as BSICs are unique,
	// which they should be.  The method will just return NULL.

	LOG(DEBUG) << "BCCH_FREQ_NCELL=" << BCCH_FREQ_NCELL << " BSIC=" << BSIC;
	char *retVal = NULL;
	char query[500];
	int C0 = getARFCN(BCCH_FREQ_NCELL);
	if (C0 < 0) return NULL;
	sprintf(query,"SELECT IPADDRESS FROM NEIGHBOR_TABLE WHERE BSIC=%d AND C0=%d",BSIC,C0);
	LOG(DEBUG) << query;
	sqlite3_stmt *stmt;
	int prc = sqlite3_prepare_statement(mDB,&stmt,query);
	if (prc) {
		LOG(ALERT) << "cannot prepare statement for " << query;
		return NULL;
	}
	int src = sqlite3_run_query(mDB,stmt);
	if (src==SQLITE_ROW) {
		const char* ptr = (const char*)sqlite3_column_text(stmt,0);
		retVal = strdup(ptr);
	}
	sqlite3_finalize(stmt);
	return retVal;
}

/* Return the ARFCN given its position in the BCCH channel list.
 * The way I read GSM 04.08 10.5.2.20, they take the ARFCNs, sort them
 * in ascending order, move the first to the last if it's 0,
 * then BCCH-FREQ-NCELL is a position in that list.
 */
int NeighborTable::getARFCN(unsigned BCCH_FREQ_NCELL)
{
	ScopedLock lock(mLock);
	LOG(DEBUG) << "BCCH_FREQ_NCELL=" << BCCH_FREQ_NCELL;
	if (BCCH_FREQ_NCELL >= mARFCNList.size()) {
		LOG(ALERT) << "BCCH-FREQ-NCELL not in BCCH channel list";
		return -1;
	}
	return mARFCNList[BCCH_FREQ_NCELL];
}



void NeighborTable::holdOff(const char* address, unsigned seconds)
{
	assert(address);
	LOG(DEBUG) << "address " << address << " seconds " << seconds;

	if (!seconds) return;

	time_t holdoffTime = time(NULL) + seconds;
	char query[200];
	sprintf(query,"UPDATE NEIGHBOR_TABLE SET HOLDOFF=%u WHERE IPADDRESS='%s'",
		holdoffTime, address);
	if (!sqlite3_command(mDB,query)) {
		LOG(ALERT) << "cannot access neighbor table";
	}
}

void NeighborTable::holdOff(const struct sockaddr_in* peer, unsigned seconds)
{
	if (!seconds) return;

	char addrString[256];
	const char *ret = inet_ntop(AF_INET,&(peer->sin_addr),addrString,255);
	if (!ret) {
		LOG(ERR) << "cannot parse peer socket address";
		return;
	}
	return holdOff(addrString,seconds);
}


bool NeighborTable::holdingOff(const char* address)
{
	unsigned holdoffTime;
	if (!sqlite3_single_lookup(mDB,"NEIGHBOR_TABLE","IPADDRESS",address,"HOLDOFF",holdoffTime)) {
		LOG(ALERT) << "cannot read neighbor table";
		return false;
	}
	time_t now = time(NULL);
	LOG(DEBUG) << "hold-off time for " << address << ": " << holdoffTime << ", now: " << now;
	return now < (time_t)holdoffTime;
}

std::vector<unsigned> NeighborTable::getARFCNs() const
{
	char query[500];
	vector<unsigned> bcchChannelList;
	sprintf(query,"SELECT C0 FROM NEIGHBOR_TABLE WHERE BSIC > -1 ORDER BY UPDATED DESC LIMIT %u", gConfig.getNum("GSM.Neighbors.NumToSend"));
	sqlite3_stmt *stmt;
	int prc = sqlite3_prepare_statement(mDB,&stmt,query);
	if (prc) {
		LOG(ALERT) << "cannot prepare statement for " << query;
		return bcchChannelList;
	}
	int src = sqlite3_run_query(mDB,stmt);
	while (src==SQLITE_ROW) {
		unsigned ARFCN = sqlite3_column_int(stmt,0);
		bcchChannelList.push_back(ARFCN);
		src = sqlite3_step(stmt);
	}
	sqlite3_finalize(stmt);
	return bcchChannelList;
}



int NeighborTable::getBCCSet() const
{
	int set = 0;
	static const char query[] = "SELECT BSIC FROM NEIGHBOR_TABLE";
	sqlite3_stmt *stmt;
	int prc = sqlite3_prepare_statement(mDB,&stmt,query);
	if (prc) {
		LOG(ALERT) << "cannot prepare statement for " << query;
		return -1;
	}
	int src = sqlite3_run_query(mDB,stmt);
	while (src==SQLITE_ROW) {
		unsigned BCC = sqlite3_column_int(stmt,0);
		set |= (1 << BCC);
		src = sqlite3_step(stmt);
	}
	sqlite3_finalize(stmt);
	return set;
}

