/**@file Declarations for PhysicalStatus and related classes. */
/*
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011 Range Networks, Inc.
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
