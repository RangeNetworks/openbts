/**@ The global table of neighboring BTS units. */
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


#ifndef NEIGHBORTABLE_H
#define NEIGHBORTABLE_H

#define NEIGHBOR_TABLE_ON_DISK 0

#include <ScalarTypes.h>
#include <Sockets.h>
#include <vector>
#include <Threads.h>
#include <set>
#include <map>

using namespace std;


struct sqlite3;

namespace Peering {

struct NeighborEntry {
	string mIPAddress;	// always contains a port ("x.x.x.x:p")
	time_t mUpdated;	// Updated is 0, or is in the past.
	time_t mHoldoff;	// Holdoff is 0, or is in the future.
	long getUpdated() const, getHoldoff() const;
	// All these values except noise are unsigned, but we use int so -1 means not sent by peer.
	int mC0;			// int because -1 is used as an error.
	int mBSIC;
	int mNumArfcns;
	Int_z mNoise;
	int mTchAvail;
	int mTchTotal;
	string neC0PlusBSIC() const;
	NeighborEntry() : mUpdated(0), mHoldoff(0), mC0(-1), mBSIC(-1), mNumArfcns(-1), mTchAvail(-1), mTchTotal(-1) {}
};

typedef vector<NeighborEntry> NeighborEntryVector;

class NeighborTable {

	private:

#if NEIGHBOR_TABLE_ON_DISK
	struct sqlite3* mDB;		///< database connection
	set<string> mConfigured;   ///< which ipaddresses in the table are configured
#else
	typedef std::map<string,NeighborEntry> NeighborTableMap;
	NeighborTableMap mNeighborMap;
#endif
	std::vector<unsigned> mARFCNList;	///< current ARFCN list
	mutable Mutex mLock;

	/** Get the neighbor cell ARFCNs as a vector. */
	std::vector<unsigned> getARFCNs() const;

	public:

	// (pat) In order to prevent crashes caused by static initialization races, I added
	// an empty constructor and moved initialization to the function NeighborTableInit.
#if NEIGHBOR_TABLE_ON_DISK
	// (pat) The BCC set is not used anywhere...
	int mBCCSet;				///< set of current BCCs
	NeighborTable() : mDB(0), mBCCSet(0) {}
	/** Create a new neighbor record if it does not already exist. */
	void addNeighbor(const struct sockaddr_in* address);
#else
	NeighborTable() {}
#endif
	bool ntFindByIP(string ip, NeighborEntry *entry);
	bool ntFindByPeerAddr(const struct ::sockaddr_in* peer, NeighborEntry *entry);
	bool ntFindByArfcn(int arfcn, int bsic, NeighborEntry *entry);

	/** Constructor opens the database and creates/populates the new table as needed. */
	void NeighborTableInit(const char* dbPath);

	/** Fill the neighbor table from GSM.Neighbors config. */
	void ntFill();

	/**
	  Add new information to a neighbor record to the table.
	  @return true if the neighbor ARFCN list changed
	*/
	bool ntAddInfo(NeighborEntry &entry);

	/** Gets age of parameters in seconds or UNDEFINED if unknown. */
	unsigned paramAge(const char* address);

	/** Returns a C-string that must be free'd by the caller. */
	string getAddress(unsigned arfcn, unsigned BSIC, string &whatswrong);

	/** Return the ARFCN given its position (frequency index) in the BCCH channel list (GSM 04.08 10.5.2.20). */
	int getARFCN(unsigned BCCH_FREQ_NCELL);

	// (pat 3-2014) Return the Frequency Index given an ARFCN, or -1 on error.
	int getFreqIndexForARFCN(unsigned arfcn);

	/** Start the holdoff timer on this neighbor. */
	void setHoldOff(string ipaddress, unsigned seconds);

	/** Start the holdoff timer on this neighbor. */
	void setHoldOff(const struct sockaddr_in* peer, unsigned seconds);

	/** Return true if we are holding off requests to this neighbor. */
	bool holdingOff(const char* ipaddress);

	/** Send out new requests for old entries. */
	void ntRefresh();
	void pingPeer(string ipaddr);

	/**
		Get the neighbor cell ARFCNs as a vector.
		mARFCNList is updated by every call to add().
		This returns a copy to be thread-safe.
	*/
	std::vector<unsigned> ARFCNList() const { ScopedLock lock(mLock); return mARFCNList; }

	/** Get the neighbor cell BCC set as a bitmask. */
	// unused: int BCCSet() const { return mBCCSet; }
	/** Get the neighbor cell BCC set as a bitmask. */
	//int getBCCSet() const;

	// We pass a copy of the neighbor table to the CLI to avoid locking issues.
	void getNeighborVector(std::vector<NeighborEntry> &nvec);

	// Is this neighbor congested?
	bool neighborCongestion(unsigned arfcn, unsigned bsic);
};



} //namespace



extern Peering::NeighborTable gNeighborTable;

#endif

