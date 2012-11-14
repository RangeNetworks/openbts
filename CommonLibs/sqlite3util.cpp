/*
* Written by David A. Burgess, Kestrel Signal Processing, Inc., 2010
* The author disclaims copyright to this source code.
*/


#include "sqlite3.h"
#include "sqlite3util.h"

#include <string.h>
#include <unistd.h>
#include <stdio.h>


// Wrappers to sqlite operations.
// These will eventually get moved to commonlibs.

int sqlite3_prepare_statement(sqlite3* DB, sqlite3_stmt **stmt, const char* query)
{
	int prc = sqlite3_prepare_v2(DB,query,strlen(query),stmt,NULL);
	if (prc) {
		fprintf(stderr,"sqlite3_prepare_v2 failed for \"%s\": %s\n",query,sqlite3_errmsg(DB));
		sqlite3_finalize(*stmt);
	}
	return prc;
}

int sqlite3_run_query(sqlite3* DB, sqlite3_stmt *stmt)
{
	int src = SQLITE_BUSY;
	while (src==SQLITE_BUSY) {
		src = sqlite3_step(stmt);
		if (src==SQLITE_BUSY) {
			usleep(100000);
		}
	}
	if ((src!=SQLITE_DONE) && (src!=SQLITE_ROW)) {
		fprintf(stderr,"sqlite3_run_query failed: %s: %s\n", sqlite3_sql(stmt), sqlite3_errmsg(DB));
	}
	return src;
}


bool sqlite3_exists(sqlite3* DB, const char *tableName,
		const char* keyName, const char* keyData)
{
	size_t stringSize = 100 + strlen(tableName) + strlen(keyName) + strlen(keyData);
	char query[stringSize];
	sprintf(query,"SELECT * FROM %s WHERE %s == \"%s\"",tableName,keyName,keyData);
	// Prepare the statement.
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(DB,&stmt,query)) return false;
	// Read the result.
	int src = sqlite3_run_query(DB,stmt);
	sqlite3_finalize(stmt);
	// Anything there?
	return (src == SQLITE_ROW);
}



bool sqlite3_single_lookup(sqlite3* DB, const char *tableName,
		const char* keyName, const char* keyData,
		const char* valueName, unsigned &valueData)
{
	size_t stringSize = 100 + strlen(valueName) + strlen(tableName) + strlen(keyName) + strlen(keyData);
	char query[stringSize];
	sprintf(query,"SELECT %s FROM %s WHERE %s == \"%s\"",valueName,tableName,keyName,keyData);
	// Prepare the statement.
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(DB,&stmt,query)) return false;
	// Read the result.
	int src = sqlite3_run_query(DB,stmt);
	bool retVal = false;
	if (src == SQLITE_ROW) {
		valueData = (unsigned)sqlite3_column_int64(stmt,0);
		retVal = true;
	}
	sqlite3_finalize(stmt);
	return retVal;
}


// This function returns an allocated string that must be free'd by the caller.
bool sqlite3_single_lookup(sqlite3* DB, const char* tableName,
		const char* keyName, const char* keyData,
		const char* valueName, char* &valueData)
{
	valueData=NULL;
	size_t stringSize = 100 + strlen(valueName) + strlen(tableName) + strlen(keyName) + strlen(keyData);
	char query[stringSize];
	sprintf(query,"SELECT %s FROM %s WHERE %s == \"%s\"",valueName,tableName,keyName,keyData);
	// Prepare the statement.
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(DB,&stmt,query)) return false;
	// Read the result.
	int src = sqlite3_run_query(DB,stmt);
	bool retVal = false;
	if (src == SQLITE_ROW) {
		const char* ptr = (const char*)sqlite3_column_text(stmt,0);
		if (ptr) valueData = strdup(ptr);
		retVal = true;
	}
	sqlite3_finalize(stmt);
	return retVal;
}


// This function returns an allocated string that must be free'd by tha caller.
bool sqlite3_single_lookup(sqlite3* DB, const char* tableName,
		const char* keyName, unsigned keyData,
		const char* valueName, char* &valueData)
{
	valueData=NULL;
	size_t stringSize = 100 + strlen(valueName) + strlen(tableName) + strlen(keyName) + 20;
	char query[stringSize];
	sprintf(query,"SELECT %s FROM %s WHERE %s == %u",valueName,tableName,keyName,keyData);
	// Prepare the statement.
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(DB,&stmt,query)) return false;
	// Read the result.
	int src = sqlite3_run_query(DB,stmt);
	bool retVal = false;
	if (src == SQLITE_ROW) {
		const char* ptr = (const char*)sqlite3_column_text(stmt,0);
		if (ptr) valueData = strdup(ptr);
		retVal = true;
	}
	sqlite3_finalize(stmt);
	return retVal;
}




bool sqlite3_command(sqlite3* DB, const char* query)
{
	// Prepare the statement.
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(DB,&stmt,query)) return false;
	// Run the query.
	int src = sqlite3_run_query(DB,stmt);
	return src==SQLITE_DONE;
}



