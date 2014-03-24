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
		"IPADDRESS TEXT UNIQUE NOT NULL, "	// IP address of peer BTS  (pat) it includes the port.
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


// Does the ipaddr include a port specification?
// This will fail miserably with ipv6.
bool includesPort(const char *ipaddr)
{
	const char *colon = strrchr(ipaddr,':');
	const char *dot = strrchr(ipaddr,'.');
	return colon && colon > dot;	// Works if dot is NULL too.
}


// (pat) 5-25: Allow the config neighbor list to optionally include a port.
// (pat) Neighbor discovery should be moved into SR.  Each BTS should register,
// then when they want to look up a LAC reported by the MS they could just ask SR.
void NeighborTable::fill()
{
	mConfigured.clear();
	if (mDB == NULL) { return; }	// we already threw an ALERT.
	// Stuff the neighbor ip addresses into the table without any other info.
	// Let existing information persist for current neighbors.
	// NeighborTable::refresh() will get updated infomation when it's available.
	vector<string> neighbors = gConfig.getVectorOfStrings("GSM.Neighbors");
	LOG(DEBUG) << "neighbor list length " << neighbors.size();
	unsigned short port = gConfig.getNum("Peering.Port");
	for (unsigned int i = 0; i < neighbors.size(); i++) {
		struct sockaddr_in address;
		// (pat) The measurement report allows only 32 ARFCN slots, the last of which we reserve for 3G to avoid confusion.
		// However, we cannot check that here, because some of the neighbors may share ARFCNs, and we cannot tell
		// that until they report in.
		// Do not check this here: if (i == 31) ...
		const char *host = neighbors[i].c_str();
		LOG(DEBUG) << "resolving host name for " << host;
		bool validAddr;
		if (includesPort(host)) {
			LOG(DEBUG) << "resolving host name for " << host;
			validAddr = resolveAddress(&address, host);
		} else {
			LOG(DEBUG) << "resolving host name for " << host <<" + port:" <<port;
			validAddr = resolveAddress(&address, host, port);
		}
		if (!validAddr) {
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


// (pat) This just adds the address; the BSIC will be filled in later when we hear from the peer.
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

	// update mBCCSet  (pat - why?)
	mBCCSet = getBCCSet();

	// update mARFCNList and check for a change
	mARFCNList = getARFCNs();
}





bool NeighborTable::addInfo(const struct ::sockaddr_in* address,	// neighbor IP
	unsigned updated,	// current time.  (pat) This is the wrong type.
	unsigned C0, unsigned BSIC)		// Describes the neighbor radio.
{
	if (mDB == NULL) { return false; }	// we already threw an ALERT.
	ScopedLock lock(mLock);
	// Get a string for the sockaddr_in.
	char addrString[256];
	const char *ret = inet_ntop(AF_INET,&(address->sin_addr),addrString,255);
	if (!ret) {
		LOG(ERR) << "cannot parse peer socket address";
		return false;
	}

	char query[200];
	unsigned oldC0;  // C0 is arbitrary integer column.  just want to know if ipaddress is in table.
	unsigned oldBSIC;
	sprintf(query, "%s:%d", addrString,(int)ntohs(address->sin_port));
	if (!sqlite3_single_lookup(mDB, "NEIGHBOR_TABLE", "IPADDRESS", query, "C0", oldC0) ||
	    !sqlite3_single_lookup(mDB, "NEIGHBOR_TABLE", "IPADDRESS", query, "BSIC", oldBSIC)) {
		LOG(NOTICE) << "Ignoring unsolicited 'RSP NEIGHBOR_PARAMS' from " << query;
		return false;
	}
	LOG(DEBUG) << "updating " << addrString << ":" << ntohs(address->sin_port) << " in neighbor table" <<LOGVAR(C0)<<LOGVAR(BSIC)<<LOGVAR(oldC0) <<LOGVAR(oldBSIC);
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

	// (pat) Added test to see if C0 or BCC changed.  That would not change the beacon but we need to recheck for conflicts
	// with the current BTS.
	bool result = change || (oldC0 != C0) || oldBSIC != BSIC;
	LOG(DEBUG) << LOGVAR(result);
	return result;
}


void NeighborTable::refresh()
{
	if (mDB == NULL) { return; }	// we already threw an ALERT.
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


string NeighborTable::getAddress(unsigned BCCH_FREQ_NCELL, unsigned BSIC)
{
	if (mDB == NULL) { return string(""); }	// we already threw an ALERT.
	// There is a potential race condition here where mARFCNList could have changed
	// between the sending of SI5 and the receipt of the corresponding measurement report.
	// That will not be a serious problem as long as BSICs are unique,
	// which they should be.  The method will just return NULL.

	LOG(DEBUG) << "BCCH_FREQ_NCELL=" << BCCH_FREQ_NCELL << " BSIC=" << BSIC;
	string retVal;
	char query[500];
	int C0 = getARFCN(BCCH_FREQ_NCELL);
	if (C0 < 0) return string("");
	sprintf(query,"SELECT IPADDRESS FROM NEIGHBOR_TABLE WHERE BSIC=%d AND C0=%d",BSIC,C0);
	LOG(DEBUG) << query;
	sqlite3_stmt *stmt;
	int prc = sqlite3_prepare_statement(mDB,&stmt,query);
	if (prc) {
		LOG(ALERT) << "cannot prepare statement for " << query;
		return string("");	// (pat 7-30-2013)  This was formerly NULL, which crashes when executed.
	}
	int src = sqlite3_run_query(mDB,stmt);
	if (src==SQLITE_ROW) {
		const char* ptr = (const char*)sqlite3_column_text(stmt,0);
		retVal = string(ptr);
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
		LOG(INFO) << "BCCH-FREQ-NCELL not in BCCH channel list";
		return -1;
	}
	return mARFCNList[BCCH_FREQ_NCELL];
}



void NeighborTable::holdOff(const char* address, unsigned seconds)
{
	if (mDB == NULL) { return; }	// we already threw an ALERT.
	assert(address);
	LOG(DEBUG) << "address " << address << " seconds " << seconds;

	if (!seconds) return;

	time_t holdoffTime = time(NULL) + seconds;
	char query[200];
	sprintf(query,"UPDATE NEIGHBOR_TABLE SET HOLDOFF=%lu WHERE IPADDRESS='%s'",
		holdoffTime, address);
	if (!sqlite3_command(mDB,query)) {
		LOG(ALERT) << "cannot access neighbor table";
	}
}

void NeighborTable::holdOff(const struct sockaddr_in* peer, unsigned seconds)
{
	if (mDB == NULL) { return; }	// we already threw an ALERT.
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
	if (mDB == NULL) { return true; }	// we already threw an ALERT.
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
	sprintf(query,"SELECT C0 FROM NEIGHBOR_TABLE WHERE BSIC > -1 ORDER BY UPDATED DESC LIMIT %lu", gConfig.getNum("GSM.Neighbors.NumToSend"));
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

