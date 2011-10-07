/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
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

	TMSITable(const char*wPath);

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
