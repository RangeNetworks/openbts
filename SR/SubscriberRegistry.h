/*
* Copyright 2011 Kestrel Signal Processing, Inc.
* Copyright 2011 Free Software Foundation, Inc.
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

#ifndef SubscriberRegistry_H
#define SubscriberRegistry_H

#include <map>
#include <stdlib.h>
#include <Logger.h>
// #include <Timeval.h>
// #include <Threads.h>
#include <map>
#include <string>
#include "sqlite3.h"

using namespace std;

class SubscriberRegistry {

	private:

	sqlite3 *mDB;			///< database connection


	public:

	~SubscriberRegistry();

	/**
			Initialize the subscriber registry using parameters from gConfig.
			@return 0 if the database was successfully opened and initialized; 1 otherwise
	*/
	int init();

	typedef enum {
		SUCCESS=0,		///< operation successful
		FAILURE=1,		///< operation not successful
		DELAYED=2,		///< operation successful, but effect delayed
		TRYAGAIN=3		///< operation not attempted, try again later
	} Status;


	sqlite3 *db()
	{
		return mDB;
	}



	/**
		Resolve an ISDN or other numeric address to an IMSI.
		@param ISDN Any numeric address, E.164, local extension, etc.
	*/
	string getIMSI(string ISDN);

	/**
		Given an IMSI, return the local CLID, which should be a numeric address.
		@param IMSI The subscriber IMSI.
	*/
	string getCLIDLocal(string IMSI);

	/**
		Given an IMSI, return the global CLID, which should be a numeric address.
		@param IMSI The subscriber IMSI.
			
	*/
	string getCLIDGlobal(string IMSI);

	/**
		Given an IMSI, return the IP address of the most recent registration.
		@param IMSI The subscriber IMSI
		@return The Registration IP for this IMSI, "111.222.333.444:port",
			
	*/
	string getRegistrationIP(string IMSI);

	/**
		Set a specific variable indexed by imsi from sip_buddies
		@param imsi The user's IMSI or SIP username.
		@param key to index into table
	*/
	string imsiGet(string imsi, string key);

	/**
		Set a specific variable indexed by imsi_from sip_buddies
		@param imsi The user's IMSI or SIP username.
		@param key to index into table
		@param value to set indexed by the key
	*/
	Status imsiSet(string imsi, string key, string value);

	/**
		Add a new user to the SubscriberRegistry.
		@param IMSI The user's IMSI or SIP username.
		@param CLID The user's local CLID.
	*/
	Status addUser(string IMSI, string CLID);


	/**
		Set the current time as the time of the most recent registration for an IMSI.
		@param IMSI The user's IMSI or SIP username.
	*/
	Status setRegTime(string IMSI);


	string mapCLIDGlobal(string local);


	bool useGateway(string ISDN);


	/**
		Update the RRLP location for user
		@param name IMSI to be updated
		@param lat Latitude
		@param lon Longitude
		@param err Approximate Error
	*/
	Status RRLPUpdate(string name, string lat, string lon, string err);

	private:


	/**
		Run sql statments locally.
		@param stmt The sql statements.
		@param resultptr Set this to point to the result of executing the statements.
	*/
	Status sqlLocal(const char* stmt, char **resultptr);

	string sqlQuery(string unknownColumn, string table, string knownColumn, string knownValue);

	/**
		Run an sql update.
		@param stmt The update statement.
	*/
	Status sqlUpdate(string stmt);


};



#endif

// vim: ts=4 sw=4
