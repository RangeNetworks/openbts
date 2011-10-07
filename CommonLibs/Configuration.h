/*
* Copyright 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
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


#ifndef CONFIGURATION_H
#define CONFIGURATION_H


#include "sqlite3util.h"

#include <assert.h>
#include <stdlib.h>

#include <map>
#include <vector>
#include <string>
#include <iostream>

#include <Threads.h>
#include <stdint.h>


/** A class for configuration file errors. */
class ConfigurationTableError {};

/** An exception thrown when a given config key isn't found. */
class ConfigurationTableKeyNotFound : public ConfigurationTableError {

	private:

	std::string mKey;

	public:

	ConfigurationTableKeyNotFound(const std::string& wKey)
		:mKey(wKey)
	{ std::cerr << "cannot find configuration value " << mKey << std::endl; }

	const std::string& key() const { return mKey; }

};


class ConfigurationRecord {

	private:

	std::string mValue;
	long mNumber;
	bool mDefined;

	public:

	ConfigurationRecord(bool wDefined=true):
		mDefined(wDefined)
	{ }

	ConfigurationRecord(const std::string& wValue):
		mValue(wValue),
		mNumber(strtol(wValue.c_str(),NULL,0)),
		mDefined(true)
	{ }

	ConfigurationRecord(const char* wValue):
		mValue(std::string(wValue)),
		mNumber(strtol(wValue,NULL,0)),
		mDefined(true)
	{ }


	const std::string& value() const { return mValue; }
	long number() const { return mNumber; }
	bool defined() const { return mDefined; }

};


/** A string class that uses a hash function for comparison. */
class HashString : public std::string {


	protected:

	uint64_t mHash;

	void computeHash();


	public:

	HashString(const char* src)
		:std::string(src)
	{
		computeHash();
	}

	HashString(const std::string& src)
		:std::string(src)
	{
		computeHash();
	}

	HashString()
	{
		mHash=0;
	}

	HashString& operator=(std::string& src)
	{
		std::string::operator=(src);
		computeHash();
		return *this;
	}

	HashString& operator=(const char* src)
	{
		std::string::operator=(src);
		computeHash();
		return *this;
	}

	bool operator==(const HashString& other)
	{
		return mHash==other.mHash;
	}

	bool operator<(const HashString& other)
	{
		return mHash<other.mHash;
	}

	bool operator>(const HashString& other)
	{
		return mHash<other.mHash;
	}

	uint64_t hash() const { return mHash; }

};


typedef std::map<HashString, ConfigurationRecord> ConfigurationMap;


/**
	A class for maintaining a configuration key-value table,
	based on sqlite3 and a local map-based cache.
	Thread-safe, too.
*/
class ConfigurationTable {

	private:

	sqlite3* mDB;				///< database connection
	ConfigurationMap mCache;	///< cache of recently access configuration values
	mutable Mutex mLock;		///< control for multithreaded access to the cache

	public:


	ConfigurationTable(const char* filename = ":memory:");

	/** Return true if the key is used in the table.  */
	bool defines(const std::string& key);

	/** Return true if this key is identified as static. */
	bool isStatic(const std::string& key) const;

	/** Return true if this key is identified as required (!optional). */
	bool isRequired(const std::string& key) const;

	/**
		Get a string parameter from the table.
		Throw ConfigurationTableKeyNotFound if not found.
	*/
	std::string getStr(const std::string& key);


	/**
		Get a string parameter from the table.
		Define the parameter to the default value if not found.
	*/
	std::string getStr(const std::string& key, const char* defaultValue);


	/**
		Get a numeric parameter from the table.
		Throw ConfigurationTableKeyNotFound if not found.
	*/
	long getNum(const std::string& key);

	/**
		Get a numeric parameter from the table.
		Define the parameter to the default value if not found.
	*/
	long getNum(const std::string& key, long defaultValue);

	/**
		Get a numeric vector from the table.
	*/
	std::vector<unsigned> getVector(const std::string& key);

	/** Get length of a vector */
	unsigned getVectorLength(const std::string &key) 
		{ return getVector(key).size(); }

	/** Set or change a value in the table.  */
	bool set(const std::string& key, const std::string& value);

	/** Set or change a value in the table.  */
	bool set(const std::string& key, long value);

	/** Create an entry in the table, no value though. */
	bool set(const std::string& key);

	/**
		Remove a key from the table.
		Will not remove static or required values.
		@param key The key of the item to be deleted.
		@return true if anything was actually removed.
	*/
	bool unset(const std::string& key);

	/** Search the table, dumping to a stream. */
	void find(const std::string& pattern, std::ostream&) const;

	/** Define the callback to purge the cache whenever the database changes. */
	void setUpdateHook(void(*)(void *,int ,char const *,char const *,sqlite3_int64));

	/** purege cache if it exceeds a certain age */
	void checkCacheAge();

	/** Delete all records from the cache. */
	void purge();


	private:

	/**
		Attempt to lookup a record, cache if needed.
		Throw ConfigurationTableKeyNotFound if not found.
		Caller should hold mLock because the returned reference points into the cache.
	*/
	const ConfigurationRecord& lookup(const std::string& key);

};



#endif


// vim: ts=4 sw=4
