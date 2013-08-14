/**@ The global table of neighboring BTS units. */
/*
 * Copright 2011 Range Networks, Inc.
 * All rights reserved.
*/


#ifndef NEIGHBORTABLE_H
#define NEIGHBORTABLE_H

#include <Sockets.h>
#include <vector>
#include <Threads.h>
#include <set>

using namespace std;


struct sqlite3;

namespace Peering {

class NeighborTable {

	private:

	struct sqlite3* mDB;		///< database connection
	std::vector<unsigned> mARFCNList;	///< current ARFCN list
	int mBCCSet;				///< set of current BCCs
	mutable Mutex mLock;

	set<string> mConfigured;   ///< which ipaddresses in the table are configured

	public:

	/** Age parameter value for undefined records. */
	static const unsigned UNKNOWN = 0xFFFFFFFF;

	// (pat) In order to prevent crashes caused by static initialization races, I added
	// an empty constructor and moved initialization to the function NeighborTableInit.
	NeighborTable() : mDB(0), mBCCSet(0) {}
	/** Constructor opens the database and creates/populates the new table as needed. */
	void NeighborTableInit(const char* dbPath);

	/** Fill the neighbor table from GSM.Neighbors config. */
	void fill();

	/** Create a new neighbor record if it does not already exist. */
	void addNeighbor(const struct sockaddr_in* address);

	/**
	  Add new information to a neighbor record to the table.
	  @return true if the neighbor ARFCN list changed
	*/
	bool addInfo(const struct sockaddr_in* address, unsigned updated, unsigned C0, unsigned BSIC);

	/** Gets age of parameters in seconds or UNDEFINED if unknown. */
	unsigned paramAge(const char* address);

	/** Returns a C-string that must be free'd by the caller. */
	char* getAddress(unsigned BCCH_FREQ_NCELL, unsigned BSIC);

	/** Return the ARFCN given its position in the BCCH channel list (GSM 04.08 10.5.2.20). */
	int getARFCN(unsigned BCCH_FREQ_NCELL);

	/** Start the holdoff timer on this neighbor. */
	void holdOff(const char* address, unsigned seconds);

	/** Start the holdoff timer on this neighbor. */
	void holdOff(const struct sockaddr_in* peer, unsigned seconds);

	/** Return true if we are holding off requests to this neighbor. */
	bool holdingOff(const char* address);

	/** Send out new requests for old entries. */
	void refresh();

	/**
		Get the neighbor cell ARFCNs as a vector.
		mARFCNList is updated by every call to add().
		This returns a copy to be thread-safe.
	*/
	std::vector<unsigned> ARFCNList() const { ScopedLock lock(mLock); return mARFCNList; }

	/** Get the neighbor cell BCC set as a bitmask. */
	int BCCSet() const { return mBCCSet; }

	private:

	/** Get the neighbor cell BCC set as a bitmask. */
	int getBCCSet() const;

	/** Get the neighbor cell ARFCNs as a vector. */
	std::vector<unsigned> getARFCNs() const;
};



} //namespace



extern Peering::NeighborTable gNeighborTable;

#endif

