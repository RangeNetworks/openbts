/**@file Declarations for PhysicalStatus and related classes. */
/*
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011 Range Networks, Inc.
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



#ifndef PHYSICALSTATUS_H
#define PHYSICALSTATUS_H

#include <map>

#include <Timeval.h>
#include <Threads.h>


struct sqlite3;


namespace GSM {

class L3MeasurementResults;
class LogicalChannel;

/**
	A table for tracking the state of channels.
*/
class PhysicalStatus {

private:

	Mutex mLock;		///< to reduce the load on the filesystem locking
	sqlite3 *mDB;		///< database connection

public:

	/**
		Initialize a physical status reporting table.
		@param path Path fto sqlite3 database file.
		@return 0 if the database was successfully opened and initialized; 1 otherwise
	*/
	int open(const char*wPath);

	~PhysicalStatus();

	/** 
		Add reporting information associated with a channel to the table.
		@param chan The channel to report.
		@param measResults The measurement report.
		@return The result of the SQLite query: true for the query being executed successfully, false otherwise.
	*/
	bool setPhysical(const LogicalChannel* chan, const L3MeasurementResults& measResults);

	/**
		Dump the physical status table to the output stream.
		@param os The output stream to dump the channel information to.
	*/
//	void dump(std::ostream& os) const;

	private:

	/** 
		Create entry in table. This is for the initial creation.
		@param chan The channel to create an entry for.
		@return The result of the SQLite query: true for the query being executed successfully, false otherwise.
	*/
	bool createEntry(const LogicalChannel* chan);


};


}

#endif

// vim: ts=4 sw=4
