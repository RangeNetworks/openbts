#ifndef SQLITE3UTIL_H
#define SQLITE3UTIL_H

#include <sqlite3.h>

int sqlite3_prepare_statement(sqlite3* DB, sqlite3_stmt **stmt, const char* query);

int sqlite3_run_query(sqlite3* DB, sqlite3_stmt *stmt);

bool sqlite3_single_lookup(sqlite3* DB, const char *tableName,
		const char* keyName, const char* keyData,
		const char* valueName, unsigned &valueData);

bool sqlite3_single_lookup(sqlite3* DB, const char* tableName,
		const char* keyName, const char* keyData,
		const char* valueName, char* &valueData);

// This function returns an allocated string that must be free'd by the caller.
bool sqlite3_single_lookup(sqlite3* DB, const char* tableName,
		const char* keyName, unsigned keyData,
		const char* valueName, char* &valueData);

bool sqlite3_exists(sqlite3* DB, const char* tableName,
		const char* keyName, const char* keyData);

/** Run a query, ignoring the result; return true on success. */
bool sqlite3_command(sqlite3* DB, const char* query);

#endif
