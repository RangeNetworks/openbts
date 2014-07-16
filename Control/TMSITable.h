/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
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
#include <vector>

#include <Timeval.h>
#include <Threads.h>
#include <ScalarTypes.h>


struct sqlite3;

namespace GSM {
class L3LocationAreaIdentity;
class L3MobileStationClassmark2;
class L3MobileIdentity;
}


namespace Control {
using namespace std;
class MMSharedData;
class TMSITable;
class TSqlQuery;

enum Authorization {
	AuthUnauthorized,
	AuthAuthorized,			// genuinely authorized by Registrar
	AuthOpenRegistration,
	AuthFailOpen,
};

class TmsiTableStore {
	friend class TMSITable;
	friend class TSqlQuery;

	Bool_z store_valid;	// Set when we have either updated the store or confirmed the imsi does not exist in the TMSI_TABLE.
	string imei;                Bool_z imei_changed;
	string kc;                  Bool_z kc_changed;
	string associatedUri;       Bool_z associatedUri_changed;
	string assertedIdentity;    Bool_z assertedIdentity_changed;
	Int_z assigned;             Bool_z assigned_changed;
	Int_z a5support;            Bool_z a5support_changed;
	Int_z powerClass;           Bool_z powerClass_changed;
	// non-zero auth means authorized or accepted via openregistration or network failure.
	Enum_z<Authorization> auth;	Bool_z auth_changed;

	Int_z authExpiry;			Bool_z authExpiry_changed;
	Int_z rejectCode;           Bool_z rejectCode_changed;
	// We dont have a welcomeSent_valid because if the IMSI is not in the TMSI_TABLE welcomeSent defaults to 0 which is correct.
	Int_z welcomeSent;			Bool_z welcomeSent_changed;

	public:
	Authorization getAuth() { devassert(store_valid); return auth; }
	bool isAuthorized() { return getAuth() != AuthUnauthorized; }	// authorized or passed open registration or network failure and FailOpen
	int getAuthExpiry() { devassert(store_valid); return authExpiry; }
	int getRejectCode() { devassert(store_valid); return rejectCode; }
	int getWelcomeSent() { devassert(store_valid); return welcomeSent; }

	void setImei(string wImei) { imei = wImei; imei_changed = true; }
	// Argument type is MMRejectCause but I dont want to include GSML3MMElements.h to avoid circular includes.
	void setRejectCode(int val) {
		auth = AuthUnauthorized; auth_changed = true;
		rejectCode = (int) val; rejectCode_changed = true;
		}
	void setAuthorized(Authorization wAuth) {
		auth = wAuth; auth_changed = true;
		rejectCode = 0; rejectCode_changed = true;
		}
	void setAssigned(int val) { assigned = val; assigned_changed = true; }
	void setKc(string wKc) { kc = wKc; kc_changed = true; }
	void setAssociatedUri(string wAssociatedUri) { associatedUri = wAssociatedUri; associatedUri_changed = true; }
	void setAssertedIdentity(string wAssertedIdentity) { assertedIdentity = wAssertedIdentity; assertedIdentity_changed = true; }
	void setWelcomeSent(int val) { if (welcomeSent != val) { welcomeSent = val; welcomeSent_changed = true; } }
	void setClassmark(int wa5support,int wpowerClass) {
		a5support = wa5support; a5support_changed = true;
		powerClass = wpowerClass; powerClass_changed = true;
	}
	string getImei() { return imei; }
};

class TMSITable {

	private:

	sqlite3 *mTmsiDB;			///< database connection
	void tmsiTabCleanup();
	void tmsiTabInit();


	public:

	/**
			Open the database connection.  
			@param wPath Path to sqlite3 database file.
			@return 0 if the database was successfully opened and initialized; 1 otherwise
	*/
	std::string mTablePath;			///< The path used to create the table.
	int tmsiTabOpen(const char* wPath);

	TMSITable() : mTmsiDB(0) {}
	~TMSITable();

	/**
		Create a new entry in the table.
		@param IMSI	The IMSI to create an entry for.
		@param The associated LUR, if any.
		@return The assigned TMSI.
	*/
	//uint32_t tmsiTabAssign(const string IMSI, const GSM::L3LocationAreaIdentity * lur, uint32_t oldTmsi, TmsiTableStore *store);
	//uint32_t assign(const string IMSI, const GSM::L3LocationAreaIdentity * lur, uint32_t oldTmsi, const string imei);
	//uint32_t assign(const string imsi, const MMSharedData *shdata);
	void tmsiTabUpdate(string imsi, TmsiTableStore *store);
	uint32_t tmsiTabCreateOrUpdate(const string imsi, TmsiTableStore *store, const GSM::L3LocationAreaIdentity * lai, uint32_t oldTmsi);
	bool dropTmsi(uint32_t tmsi);
	bool dropImsi(const char *imsi);
	unsigned allocateTmsi();
	//void tmsiTabSetAuthAndAssign(string imsi, int auth, int assigned);
	//void tmsiTabSetAuth(string imsi, int auth);
	void tmsiTabSetRejected(string imsi,int rejectCode);

	/**
		Find an IMSI in the table.
		This is a log-time operation.
		@param TMSI The TMSI to find.
		@return Pointer to IMSI to be freed by the caller, or NULL.
	*/
	bool tmsiTabGetStore(string imsi, TmsiTableStore *store) const;
	std::string tmsiTabGetIMSI(unsigned TMSI, unsigned *pAuthorizationResult) const;
	unsigned tmsiTabCheckAuthorization(string imsi) const;

	/**
		Find a TMSI in the table.
		This is a linear-time operation.
		@param IMSI The IMSI to mach.
		@return A TMSI value or zero on failure.
	*/
	unsigned tmsiTabGetTMSI(const string IMSI, bool onlyIfKnown) const;
	void tmsiTabReallocationComplete(unsigned TMSI) const;

	std::vector< std::vector<std::string> > tmsiTabView(int verbosity, bool rawFlag, unsigned maxrows) const;
	/** Write entries as text to a stream. */
	void tmsiTabDump(int verbosity,bool rawFlag,std::ostream&, bool ShowAll = false, bool taboption = false) const;
	
	/** Clear the table completely. */
	void tmsiTabClear();
	// Clear the authorization cache.
	void tmsiTabClearAuthCache();

	/** Set the IMEI. */
	//void setIMEI(string IMSI, string IMEI);

	/** Set the classmark. */
	bool classmark(const char* IMSI, const GSM::L3MobileStationClassmark2& classmark);

	/** Get the preferred A5 algorithm (3, 1, or 0). */
	int tmsiTabGetPreferredA5Algorithm(const char* IMSI);

#if UNUSED
	/** Save a RAND/SRES pair. */
	void putAuthTokens(const char* IMSI, uint64_t upperRAND, uint64_t lowerRAND, uint32_t SRES);

	/** Get a RAND/SRES pair. */
	bool getAuthTokens(const char* IMSI, uint64_t &upperRAND, uint64_t &lowerRAND, uint32_t &SRES) const;

	/** Save Kc. */
	void putKc(const char* imsi, string Kc, string pAssociatedUri, string pAssertedIdentity);
#endif

	/** Get Kc. */
	std::string getKc(const char* IMSI) const;

	void getSipIdentities(string imsi, string &pAssociatedUri,string &pAssertedIdentity) const;

	bool runQuery(const char *query,int checkChanges=0) const;
	private:
	bool tmsiTabCheckVersion();

	/** Update the "accessed" time on a record. */
	void tmsiTabTouchTmsi(unsigned TMSI) const;
	void tmsiTabTouchImsi(string IMSI) const;
};


bool configTmsiTestMode();
bool configSendTmsis();

extern unsigned getPreferredA5Algorithm(unsigned A5Bits);

} // namespace Control

// This gTMSITable is historically in the global namespace.
extern Control::TMSITable gTMSITable;

#endif

// vim: ts=4 sw=4
