/**@file Module for performance-reporting mechanisms. */
/*
* Copyright 2012 Range Networks, Inc.
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

#ifndef REPORTING_H
#define REPORTING_H

#include <sqlite3util.h>
#include <ostream>


/**
	Collect performance statistics into a database.
	Parameters are counters or max/min trackers, all integer.
*/
class ReportingTable {

	private:

	sqlite3* mDB;				///< database connection
	int mFacility;				///< rsyslogd facility



	public:

	/**
		Open the database connection;
		create the table if it does not exist yet.
	*/
	ReportingTable(const char* filename);

	/** Create a new parameter. */
	bool create(const char* paramName);

	/** Create an indexed parameter set. */
	bool create(const char* baseBame, unsigned minIndex, unsigned maxIndex);

	/** Increment a counter. */
	bool incr(const char* paramName);

	/** Increment an indexed counter. */
	bool incr(const char* baseName, unsigned index);

	/** Take a max of a parameter. */
	bool max(const char* paramName, unsigned newVal);

	/** Take a max of an indexed parameter. */
	bool max(const char* paramName, unsigned index, unsigned newVal);

	/** Clear a value.  */
	bool clear(const char* paramName);

	/** Clear an indexed value.  */
	bool clear(const char* paramName, unsigned index);

	/** Dump the database to a stream. */
	void dump(std::ostream&) const;

};

#endif


// vim: ts=4 sw=4
