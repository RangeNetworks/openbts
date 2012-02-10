/*
* Copyright 2008-2011 Free Software Foundation, Inc.
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



#ifndef TMSITABLE_H
#define TMSITABLE_H

#include <map>

#include <Timeval.h>
#include <Threads.h>


struct sqlite3;

namespace GSM {
class L3LocationUpdatingRequest;
class L3MobileStationClassmark2;
class L3MobileIdentity;
}


namespace Control {

class TMSITable {

	private:

	sqlite3 *mDB;			///< database connection


	public:

	/**
			Open the database connection.  
			@param wPath Path to sqlite3 database file.
			@return 0 if the database was successfully opened and initialized; 1 otherwise
	*/
	int open(const char* wPath);

	~TMSITable();

	/**
		Create a new entry in the table.
		@param IMSI	The IMSI to create an entry for.
		@param The associated LUR, if any.
		@return The assigned TMSI.
	*/
	unsigned assign(const char* IMSI, const GSM::L3LocationUpdatingRequest* lur=NULL);

	/**
		Find an IMSI in the table.
		This is a log-time operation.
		@param TMSI The TMSI to find.
		@return Pointer to IMSI to be freed by the caller, or NULL.
	*/
	char* IMSI(unsigned TMSI) const;

	/**
		Find a TMSI in the table.
		This is a linear-time operation.
		@param IMSI The IMSI to mach.
		@return A TMSI value or zero on failure.
	*/
	unsigned TMSI(const char* IMSI) const;

	/** Write entries as text to a stream. */
	void dump(std::ostream&) const;
	
	/** Clear the table completely. */
	void clear();

	/** Set the IMEI. */
	bool IMEI(const char* IMSI, const char* IMEI);

	/** Set the classmark. */
	bool classmark(const char* IMSI, const GSM::L3MobileStationClassmark2& classmark);

	/** Get the next TI value to use for this IMSI or TMSI. */
	unsigned nextL3TI(const char* IMSI);

	private:

	/** Update the "accessed" time on a record. */
	void touch(unsigned TMSI) const;
};


}

#endif

// vim: ts=4 sw=4
