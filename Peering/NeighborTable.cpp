/*
* Copyright 2011, 2014 Range Networks, Inc.

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

#include "NeighborTable.h"
#include "Peering.h"

#include <Logger.h>
#include <Globals.h>

#include <sqlite3.h>
#include <sqlite3util.h>

#include <time.h>

#include <sstream>
#include <algorithm>

using namespace Peering;
using namespace std;
static void runSomeTests();


#if NEIGHBOR_TABLE_ON_DISK
static const char* createNeighborTable = {
	"CREATE TABLE IF NOT EXISTS NEIGHBOR_TABLE ("
		"IPADDRESS TEXT UNIQUE NOT NULL, "	// IP address of peer BTS  (pat) it includes the port.
		"UPDATED INTEGER DEFAULT 0, "	// timestamp of last update
		"HOLDOFF INTEGER DEFAULT 0, "	// hold off until after this time
		"C0 INTEGER DEFAULT NULL, "		// peer BTS C0 ARFCN
		"BSIC INTEGER DEFAULT NULL"		// peer BTS BSIC
	")"
};
#endif

// Return a copy of the pentry if *entry is non-NULL.
bool NeighborTable::ntFindByIP(string ip, NeighborEntry *pentry)
{
	ScopedLock lock(mLock);
	NeighborTableMap::iterator mit = mNeighborMap.find(ip);
	if (mit == mNeighborMap.end()) { return false; }
	if (pentry) { *pentry = mit->second; }
	return true;
}

// Return a copy of the pentry if *entry is non-NULL.
bool NeighborTable::ntFindByPeerAddr(const struct ::sockaddr_in* peer, NeighborEntry *pentry)
{
	string ipaddr = sockaddr2string(peer, false);
	ScopedLock lock(mLock);
	NeighborTableMap::iterator mit = mNeighborMap.find(ipaddr);
	if (mit == mNeighborMap.end()) { return false; }
	if (pentry) { *pentry = mit->second; }
	return true;
}

// The C0 ARFCN is not sufficient because there could be multiple BTS on the same ARFCN, so we need to match the BSIC too.
bool NeighborTable::ntFindByArfcn(int arfcn, int bsic, NeighborEntry *pentry)
{
	if (arfcn < 0 || bsic < 0) {
		// These were uninitialized values.  It would not have hurt anything to just let this fall through and search for them.
		return false;
	}
	// Requires a brute force search.
	for (NeighborTableMap::iterator mit = mNeighborMap.begin(); mit != mNeighborMap.end(); mit++) {
		NeighborEntry &entry = mit->second;
		if (entry.mC0 == arfcn && (int)entry.mBSIC == bsic) {
			if (pentry) { *pentry = entry; }
			return true;
		}
	}
	return false;
}



void NeighborTable::NeighborTableInit(const char* wPath)
{
	if (0) runSomeTests();
#if NEIGHBOR_TABLE_ON_DISK
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
#endif

	// Fill the database.
	ntFill();
}


// Does the ipaddr include a port specification?
// This will fail miserably with ipv6.
static bool includesPort(const char *ipaddr)
{
	const char *colon = strrchr(ipaddr,':');
	const char *dot = strrchr(ipaddr,'.');
	return colon && colon > dot;	// Works if dot is NULL too.
}


// Create a set of IP addresses of resolved neighbor addresses.
static void makeNeighborSet(set<string> &neighborSet)
{
	// Stuff the neighbor ip addresses into the table without any other info.
	// Let existing information persist for current neighbors.
	// NeighborTable::refresh() will get updated infomation when it's available.
	vector<string> neighbors = gConfig.getVectorOfStrings("GSM.Neighbors");
	LOG(DEBUG) << "neighbor list length " << neighbors.size();

	unsigned short defaultPort = gConfig.getNum("Peering.Port");
	for (unsigned int i = 0; i < neighbors.size(); i++) {
		struct sockaddr_in peer;
		// (pat) The measurement report allows only 32 ARFCN slots, the last of which we reserve for 3G to avoid confusion.
		// However, we cannot check that here, because some of the neighbors may share ARFCNs, and we cannot tell
		// that until they report in.
		// Do not check this here: if (i == 31) ...
		const char *host = neighbors[i].c_str();
		LOG(DEBUG) << "resolving host name for " << host;
		bool validAddr;
		if (includesPort(host)) {
			LOG(DEBUG) << "resolving host name for " << host;
			validAddr = resolveAddress(&peer, host);
		} else {
			LOG(DEBUG) << "resolving host name for " << host <<" + port:" <<defaultPort;
			validAddr = resolveAddress(&peer, host, defaultPort);
		}
		if (!validAddr) {
			LOG(CRIT) << "cannot resolve host name for neighbor:" << host;
			// these two seem to want to get set even if addNeighbor isn't called
			//mBCCSet = getBCCSet();
			//mARFCNList = getARFCNs();
			continue;
		}
#if NEIGHBOR_TABLE_ON_DISK
		addNeighbor(&peer);
#else
		string neighborIP = sockaddr2string(&peer,false);
		if (neighborSet.find(neighborIP) == neighborSet.end()) {
			LOG(DEBUG) <<"Inserting into set neighborIP="<<neighborIP;
			neighborSet.insert(neighborIP);
		} else {
			LOG(ERR) << "Neighbor appears twice in GSM.Neighbors: "<<neighborIP;
		}
#endif
	}
}


// (pat) 5-25: Allow the config neighbor list to optionally include a port.
// (pat) Neighbor discovery should be moved into SR.  Each BTS should register,
// then when they want to look up a LAC reported by the MS they could just ask SR.
void NeighborTable::ntFill()
{
	static const char *GSMNeighbors = "GSM.Neighbors";
#if NEIGHBOR_TABLE_ON_DISK
	mConfigured.clear();
	if (mDB == NULL) { return; }	// we already threw an ALERT.
#endif

	string configNeighbors = gConfig.getStr(GSMNeighbors);
	{
		static string prevConfigNeighbors;
		if (configNeighbors == prevConfigNeighbors) {
			// No change.  This is the usual case.
			return;
		}
		prevConfigNeighbors = configNeighbors;
	}

	LOG(INFO) <<"Updating Neighbor Table from "<<GSMNeighbors<<"="<<configNeighbors;

	set<string> neighborSet;
	makeNeighborSet(neighborSet);

	ScopedLock lock(mLock);

#if NEIGHBOR_TABLE_ON_DISK
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
		snprintf(query2,sizeof(query2), "delete from NEIGHBOR_TABLE where IPADDRESS = '%s'", toBeDeleted[i].c_str());
		sqlite3_command(mDB, query2);
	}
#else
	// Delete items in map that are not in the newly configured neighbors list.
	for (NeighborTableMap::iterator mit = mNeighborMap.begin(); mit != mNeighborMap.end(); ) {
		string key = mit->first;
		NeighborTableMap::iterator thisone = mit++;
		if (neighborSet.find(key) == neighborSet.end()) {
		LOG(DEBUG) <<"Deleting Neighbor IPaddress="<<key;
			mNeighborMap.erase(thisone);
		}
	}

	// Add any new elements needed.
	for (set<string>::iterator it = neighborSet.begin(); it != neighborSet.end(); it++) {
		LOG(DEBUG) <<"Adding Neighbor IPaddress="<<*it;
		mNeighborMap[*it].mIPAddress = *it;		// Creates new entry if necessary.
	}

	// Update ARFCN list because some neighbors might have been eliminated.
	//mBCCSet = getBCCSet();
	mARFCNList = getARFCNs();
#endif
}


#if NEIGHBOR_TABLE_ON_DISK
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
	snprintf(query,sizeof(query),
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
	//mBCCSet = getBCCSet();

	// update mARFCNList and check for a change
	mARFCNList = getARFCNs();
}
#endif





//bool NeighborTable::addInfo(const struct ::sockaddr_in* address,	// neighbor IP
//	unsigned updated,	// current time.  (pat) This is the wrong type.
//	unsigned C0, unsigned BSIC,		// Describes the neighbor radio.
//	int noise)						// noise level (not supported by NEIGHBOR_TABLE_ON_DISK)
bool NeighborTable::ntAddInfo(NeighborEntry &newentry)
{
	ScopedLock lock(mLock);
#if NEIGHBOR_TABLE_ON_DISK
	if (mDB == NULL) { return false; }	// we already threw an ALERT.
	// Get a string for the sockaddr_in.
	char addrString[256];
	const char *ret = inet_ntop(AF_INET,&(address->sin_addr),addrString,255);
	if (!ret) {
		LOG(ERR) << "cannot parse peer socket address";
		return false;
	}

	//char query[200];
	unsigned oldC0;  // C0 is arbitrary integer column.  just want to know if ipaddress is in table.
	unsigned oldBSIC;
	//snprintf(query, sizeof(query), "%s:%d", addrString,(int)ntohs(address->sin_port));
	if (!sqlite3_single_lookup(mDB, "NEIGHBOR_TABLE", "IPADDRESS", newentry.mIPAddress, "C0", oldC0) ||
	    !sqlite3_single_lookup(mDB, "NEIGHBOR_TABLE", "IPADDRESS", newentry.mIPAddress, "BSIC", oldBSIC)) {
		LOG(NOTICE) << "Ignoring unsolicited 'RSP NEIGHBOR_PARAMS' from " << newentry.mIPAddress;
		return false;
	}
	LOG(DEBUG) << "updating " << addrString << ":" << ntohs(address->sin_port) << " in neighbor table" <<LOGVAR(C0)<<LOGVAR(BSIC)<<LOGVAR(oldC0) <<LOGVAR(oldBSIC);
	// (pat) Why would we set the HOLDOFF to 0 every time we see a RSP to neighbor info?
	newentry.mUpdated = time(NULL);
	snprintf(query,sizeof(query),
		"REPLACE INTO NEIGHBOR_TABLE (IPADDRESS,UPDATED,C0,BSIC,HOLDOFF) "
		"VALUES ('%s',%u,%u,%u,0) ",
		newentry.mIPAddress,newentry.mUpdated,newentry.mC0,newentry.mBSIC);
	if (!sqlite3_command(mDB,query)) {
		LOG(ALERT) << "write to neighbor table failed: " << query;
		return false;
	}
#else
	NeighborTableMap::iterator mit = mNeighborMap.find(newentry.mIPAddress);
	if (mit == mNeighborMap.end()) {
		LOG(NOTICE) << "Ignoring unsolicited 'RSP NEIGHBOR_PARAMS' from " << newentry.mIPAddress;
		return false;	// (pat) Added 10-17-2014.  Oops.
	}
	NeighborEntry &oldentry = mit->second;
	// (pat) Added test to see if C0 or BCC changed.  That would not change the beacon but we need to recheck for conflicts
	// with the current BTS.
	bool change = oldentry.mC0 != newentry.mC0 || oldentry.mBSIC != newentry.mBSIC;
	newentry.mUpdated = time(NULL);
	newentry.mHoldoff = oldentry.mHoldoff;		// Preserve holdoff if any.
	oldentry = newentry;
#endif
	// update mBCCSet
	//mBCCSet = getBCCSet();

	// update mARFCNList and check for a change
	std::vector<unsigned> newARFCNs = getARFCNs();
	if (newARFCNs!=mARFCNList) {
		mARFCNList = newARFCNs;
		change = true;
	}

	LOG(DEBUG) << LOGVAR(change);
	return change;
}

void NeighborTable::pingPeer(string ipString)
{
	struct sockaddr_in address;
	if (!resolveAddress(&address, ipString.c_str())) {
		LOG(ERR) << "cannot resolve neighbor address=" << ipString;
	} else {
		LOG(INFO) << "sending neighbor param request to:" << ipString;
		gPeerInterface.sendNeighborParamsRequest(&address);
	}
}

void NeighborTable::ntRefresh()
{
	ntFill();
	ScopedLock lock(mLock);
	time_t now = time(NULL);
	time_t then = now - gConfig.getNum("Peering.Neighbor.RefreshAge");
#if NEIGHBOR_TABLE_ON_DISK
	if (mDB == NULL) { return; }	// we already threw an ALERT.
	char query[100];
	snprintf(query,sizeof(query),"SELECT IPADDRESS FROM NEIGHBOR_TABLE WHERE UPDATED < %u",(unsigned)then);
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
		pingPeer(addrString);
		src = sqlite3_step(stmt);
	}
	sqlite3_finalize(stmt);
#else
	for (NeighborTableMap::iterator mit = mNeighborMap.begin(); mit != mNeighborMap.end(); mit++) {
		NeighborEntry &entry = mit->second;
		if (entry.mUpdated < then)  {
			pingPeer(entry.mIPAddress);
		}
	}
#endif
}


string NeighborTable::getAddress(unsigned arfcn, unsigned BSIC, string &whatswrong)
{
	LOG(DEBUG) <<LOGVAR(arfcn) <<LOGVAR(BSIC);
	ScopedLock lock(mLock);
	whatswrong = "unknown error";
#if NEIGHBOR_TABLE_ON_DISK
	if (mDB == NULL) {
		whatswrong = "Neighbor Table not opened";
		return string("");	// we already threw an ALERT.
	}
	// There is a potential race condition here where mARFCNList could have changed
	// between the sending of SysInfo2 or SysInfo5 and the receipt of the corresponding measurement report.
	// That will not be a serious problem as long as BSICs are unique,
	// which they should be.  The method will just return NULL.

	string retVal;
	char query[100];
	snprintf(query,sizeof(query),"SELECT IPADDRESS FROM NEIGHBOR_TABLE WHERE BSIC=%d AND C0=%d",BSIC,arfcn);
	LOG(DEBUG) << query;
	sqlite3_stmt *stmt;
	int prc = sqlite3_prepare_statement(mDB,&stmt,query);
	if (prc) {
		LOG(ALERT) << "cannot prepare statement for " << query;
		whatswrong = "sqlite prepare failed";
		return string("");	// (pat 7-30-2013)  This was formerly NULL, which crashes when executed.
	}
	int src = sqlite3_run_query(mDB,stmt);
	if (src==SQLITE_ROW) {
		const char* ptr = (const char*)sqlite3_column_text(stmt,0);
		retVal = string(ptr);
	} else {
		whatswrong = "arfcn not found in sqlite NeighborTable";
	}
	sqlite3_finalize(stmt);
	return retVal;
#else
	// Brute force search.
	for (NeighborTableMap::iterator mit = mNeighborMap.begin(); mit != mNeighborMap.end(); mit++) {
		NeighborEntry &entry = mit->second;
		if ((int)arfcn == entry.mC0 && (int)BSIC == entry.mBSIC) {
			return entry.mIPAddress;
		}
	}
	whatswrong = format("arfcn %u not found in sqlite NeighborTable",arfcn);
	return string("");
#endif
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

int NeighborTable::getFreqIndexForARFCN(unsigned arfcn)
{
	ScopedLock lock(mLock);
	int freqIndex = 0;
	for (std::vector<unsigned>::iterator it = mARFCNList.begin(); it != mARFCNList.end(); it++, freqIndex++) {
		if (*it == arfcn) return freqIndex;
	}
	return -1;	// Not found.
}



void NeighborTable::setHoldOff(string ipaddress, unsigned seconds)
{
	ScopedLock lock(mLock);
#if NEIGHBOR_TABLE_ON_DISK
	if (mDB == NULL) { return; }	// we already threw an ALERT.
	assert(ipaddress.size());
	LOG(DEBUG) <<LOGVAR(ipaddress) <<LOGVAR(seconds);

	if (!seconds) return;

	time_t holdoffTime = time(NULL) + seconds;
	char query[200];
	snprintf(query,sizeof(query),"UPDATE NEIGHBOR_TABLE SET HOLDOFF=%lu WHERE IPADDRESS='%s'",
		holdoffTime, ipaddress.c_str());
	if (!sqlite3_command(mDB,query)) {
		LOG(ALERT) << "cannot access neighbor table";
	}
#else
	NeighborTableMap::iterator mit = mNeighborMap.find(ipaddress);
	if (mit == mNeighborMap.end()) {
		LOG(NOTICE) << "Can not set holdoff for unknown IP address:"<<ipaddress;
		return;	// (pat) Added 10-17-2014.  Oops.
	}
	NeighborEntry &entry = mit->second;
	entry.mHoldoff = time(NULL) + seconds;
#endif
}

void NeighborTable::setHoldOff(const struct sockaddr_in* peer, unsigned seconds)
{
	if (!seconds) return;

	string addrString = sockaddr2string(peer, false);
	if (addrString.size() == 0) {
		LOG(ERR) << "cannot parse peer socket address";
		return;
	}
	return setHoldOff(addrString,seconds);
}


bool NeighborTable::holdingOff(const char* ipaddress)
{
	ScopedLock lock(mLock);
#if NEIGHBOR_TABLE_ON_DISK
	if (mDB == NULL) { return true; }	// we already threw an ALERT.
	unsigned holdoffTime;
	if (!sqlite3_single_lookup(mDB,"NEIGHBOR_TABLE","IPADDRESS",ipaddress,"HOLDOFF",holdoffTime)) {
		LOG(ALERT) << "cannot read neighbor table";
		return false;
	}
	time_t now = time(NULL);
	LOG(DEBUG) << "hold-off time for " << ipaddress << ": " << holdoffTime << ", now: " << now;
	return now < (time_t)holdoffTime;
#else
	NeighborEntry entry;
	if (! ntFindByIP(ipaddress,&entry)) {
		LOG(NOTICE) << "Can not find unknown IP address:"<<ipaddress;
	}
	return entry.getHoldoff() > 0;
#endif
}

// This should be a library utility routine.
template <typename T>
static void uniquify(vector<T> &vec)
{
	if (vec.size() < 2) return;
	unsigned i = 0, j = 1;
	while (j < vec.size()) {		// (pat 10-17-2014) was: (j<vec.size()-1)
		if (vec[i] != vec[j]) {
			i++;
			if (i != j) { vec[i] = vec[j]; }
		}
		j++;
	}
	// i indexes the new final element.
	vec.resize(i+1);
}

template <typename T>
static void rollLeft(vector<T> &vec)
{
	T first = vec.front();
	for (unsigned i = 0; i < vec.size()-1; i++) {
		vec[i] = vec[i+1];
	}
	vec.back() = first;
}

std::vector<unsigned> NeighborTable::getARFCNs() const
{
	ScopedLock lock(mLock);
	vector<unsigned> bcchChannelList;
#if NEIGHBOR_TABLE_ON_DISK
	char query[200];
	// (pat) ORDER BY UPDATED looks wrong to me.
	// 3GPP 44.018 10.5.2.20 says the ARFCNs are in increasing order of ARFCN, except that ARFCN 0 must appear last.
	snprintf(query,sizeof(query),"SELECT C0 FROM NEIGHBOR_TABLE WHERE BSIC > -1 ORDER BY UPDATED DESC LIMIT %lu", gConfig.getNum("GSM.Neighbors.NumToSend"));
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
#else
	int limit = gConfig.getNum("GSM.Neighbors.NumToSend");
	for (NeighborTableMap::const_iterator mit = mNeighborMap.begin(); mit != mNeighborMap.end(); mit++) {
		const NeighborEntry &entry = mit->second;
		if (entry.mBSIC >= 0 && entry.mC0 >= 0) {	// Ignore entries for which the peer has not responded.
			bcchChannelList.push_back(entry.mC0);
		}
		if ((int)bcchChannelList.size() >= limit) break;
	}

	if (bcchChannelList.size()) {
		// Sort it.
		sort(bcchChannelList.begin(),bcchChannelList.end());

		// Eliminate duplicate ARFCNs.
		uniquify(bcchChannelList);

		// If first element is ARFCN 0, move it to the back.  This required by the spec.
		if (bcchChannelList.size() >= 2 && bcchChannelList[0] == 0) {
			rollLeft(bcchChannelList);
		}
	}
	if (IS_LOG_LEVEL(DEBUG)) {
		ostringstream ss;
		ss << "bcchChannelList size="<<bcchChannelList.size() <<" content=";
		for (vector<unsigned>::iterator it = bcchChannelList.begin(); it != bcchChannelList.end(); it++) { ss <<" "<<*it; }
		LOG(DEBUG) << ss.str();
	}
	return bcchChannelList;
#endif
}



#if unused
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
#endif

string NeighborEntry::neC0PlusBSIC() const
{
	return format("%d:%d",(int)mC0,(int)mBSIC);
}

// How many seconds ago the value was updated, or 0.
// This is only for display; it does not work for comparison, because 0 is not handled properly.
long NeighborEntry::getUpdated() const
{
	if (mUpdated == 0) { return 0; }
	long updated = time(NULL) - mUpdated;	// elapsed time since updated in seconds.
	devassert(updated >= 0);		// This can not go negative because it is a time in the past.
	return updated;
}

long NeighborEntry::getHoldoff() const
{
	if (mHoldoff == 0) { return 0; }
	time_t now = time(NULL);
	if (now > mHoldoff) { return 0; }	// holdoff time is passed.
	return mHoldoff - now;		// seconds remaining.
}

void NeighborTable::getNeighborVector(std::vector<NeighborEntry> &nvec)
{
	ScopedLock lock(mLock);
	nvec.clear();
	for (NeighborTableMap::iterator mit = mNeighborMap.begin(); mit != mNeighborMap.end(); mit++) {
		LOG(DEBUG) <<"Pushing IPaddr="<<mit->first <<" entry.mIPAddress="<<mit->second.mIPAddress;
		nvec.push_back(mit->second);
	}
}

bool NeighborTable::neighborCongestion(unsigned arfcn, unsigned bsic)
{
	return false;
}

static void run1test(vector<int> &foo)
{
	for (vector<int>::iterator it = foo.begin(); it != foo.end(); it++) printf("%d ",*it);
	printf("\n");
	sort(foo.begin(),foo.end());
	printf("after sort: "); for (vector<int>::iterator it = foo.begin(); it != foo.end(); it++) printf("%d ",*it);
	printf("\n");
	uniquify(foo);
	printf("after uniquify: "); for (vector<int>::iterator it = foo.begin(); it != foo.end(); it++) printf("%d ",*it);
	printf("\n");
	rollLeft(foo);
	printf("after rollLeft: "); for (vector<int>::iterator it = foo.begin(); it != foo.end(); it++) printf("%d ",*it);
	printf("\n");
}

static void runSomeTests()
{
	vector<int> foo;
	foo.push_back(7);
	foo.push_back(19);
	foo.push_back(7);
	foo.push_back(19);
	foo.push_back(5);
	foo.push_back(4);
	foo.push_back(0);
	foo.push_back(3);
	foo.push_back(3);
	foo.push_back(2);
	foo.push_back(0);

	run1test(foo);

	foo.clear();
	foo.push_back(19);
	foo.push_back(19);
	run1test(foo);

	foo.clear();
	foo.push_back(1);
	foo.push_back(2);
	run1test(foo);

	foo.clear();
	foo.push_back(1);
	run1test(foo);
}
