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

#define LOG_GROUP LogGroup::Control
#include <Logger.h>
#include <Globals.h>
#include <sqlite3.h>
#include <sqlite3util.h>

#include <GSML3MMMessages.h>
#include <Reporting.h>
#include <Globals.h>
#include <GSML3CommonElements.h>

#include <string>
#include <iostream>
#include <iomanip>

#include <sys/stat.h>
#include "TMSITable.h"
#include "ControlTransfer.h"
#include "L3MobilityManagement.h"

using namespace std;

// (pat) For l3rewrite the TMSI will be assigned by the VLR, which will be either a new SubscriberRegistry program or Yate.
// As a result, everything here has to change.
// (pat) The access to any database needs to be message oriented if there is any chance of stalling, which could goof up the invoking state machine.

Control::TMSITable gTMSITable;


namespace Control {
static Mutex sTmsiMutex;		// We have to serialize tmsi creation.

static unsigned sHighestFakeTmsi;	// The highest fake tmsi value in the table to create an entry with no tmsi.
#define MAX_VALID_TMSI 0x3fffffff	// 30 bits.  We dont make TMSIs this high, but the invalid TMSIs start one higher than this.

// Note: we are using the TMSI as the primary key for efficiency, which means we must have a unique tmsi value in the
// TMSITable for every entry whether it is assigned to the MS or not.
// We keep valid TMSIs that we are not sure the MS received, and also TMSIs that may have authorized in the past and subsequently
// become unauthorized.
// For unauthorized phones, we used to go ahead and create valid TMSIs anyway, but I dont want to do that now that TMSIs are
// genuine random numbers and somewhat expensive to allocate, plus there could be many of them and I dont want them
// using up TMSI address space, because eventually we may receive TMSIs from neighbors as well.
// Another alternative was to have a separate TMSI->IMSI mapping table, but I dont think that is any better.
// If you do not specify a primary key, sqlite assigns a new key one higher than the highest existing in the table.
// So I will do is let it go ahead and assign TMSIs 1,2,3,4 for the case where the TMSI is not used,
// Fine, so we will use values up to 64K as valid TMSIs, and values above that for invalid TMSIs.
// This seems to do a good job and sqlite does not seem to increase the database size much when you
// assign primary keys that are not tightly clustered.
// The TMSI_ASSIGNED field indicates if the MS has confirmed receipt of the TMSI.
// Eventually we may receive TMSIs from neighbors as well.
namespace TmsiTableDefinition {

	static const char *tmsiTableVersion = "7";		// Change this version whenever you change anything in the TMSI table.
	// The fields from the tmsi table that are printed by the CLI 'tmsis' command.
	// TMSITabDump assumes the names of some of these header fields to decide the appropriate formatting.
	static string shortHeader =
		"IMSI TMSI IMEI AUTH CREATED ACCESSED TMSI_ASSIGNED";
	static string longHeader = shortHeader + " PTMSI_ASSIGNED AUTH_EXPIRY REJECT_CODE ASSOCIATED_URI ASSERTED_IDENTITY WELCOME_SENT";
		//"IMSI TMSI IMEI AUTH CREATED ACCESSED TMSI_ASSIGNED PTMSI_ASSIGNED AUTH_EXPIRY REJECT_CODE ASSOCIATED_URI ASSERTED_IDENTITY WELCOME_SENT";
	static string longerHeader = longHeader + " A5_SUPPORT POWER_CLASS RRLP_STATUS OLD_TMSI OLD_MCC OLD_MNC OLD_LAC";
		//"IMSI TMSI IMEI AUTH CREATED ACCESSED TMSI_ASSIGNED PTMSI_ASSIGNED AUTH_EXPIRY REJECT_CODE ASSOCIATED_URI ASSERTED_IDENTITY WELCOME_SENT A5_SUPPORT POWER_CLASS RRLP_STATUS OLD_TMSI OLD_MCC OLD_MNC OLD_LAC";
	static const char* create = 
		"CREATE TABLE IF NOT EXISTS TMSI_TABLE ("
			// Valid TMSIs use negative values.  Small positive auto-incremented numbers are dummy values
			// automatically assigned by sqlite used for entries where the TMSI is unspecified.
			"TMSI INTEGER PRIMARY KEY, "	// AUTOINCREMENT  This will also be used as the P-TMSI
			// IMSI is just the digits without "IMSI" in front.
			// (pat) the UNIQUE tells sqlite to create an index on this field to optimize queries.
			"IMSI TEXT UNIQUE NOT NULL, "	// IMSI
			"IMEI TEXT, "					// IMEI
			"CREATED INTEGER NOT NULL, "	// Unix time (seconds) of record creation
			"ACCESSED INTEGER NOT NULL, "	// Unix time (seconds) of last encounter
			// "APP_FLAGS INTEGER DEFAULT 0, "	// Application-specific flags  (pat) No one is using this so I took it out.
			"A5_SUPPORT INTEGER, "			// encryption support
			"POWER_CLASS INTEGER, "			// power class
			"OLD_TMSI INTEGER, "			// previous TMSI in old network
			"OLD_MCC INTEGER, "			// previous network MCC
			"OLD_MNC INTEGER, "			// previous network MNC
			"OLD_LAC INTEGER, "			// previous network LAC
#if CACHE_AUTH	// This code is eroded and no longer functional.
			// (pat 9-2013): We are not storing the authentication info in the TMSI table.
			// We need cached authorization for the case where we are using open registration and TMSIs are being sent,
			// to verify the TMSI to avoid TMSI collisions, however, this should never have been the in the BTS -
			// needs to be in sipauthserve if anywhere, so we always call the Registrar to get fresh challenges
			// For UMTS this is a requirement, so there is no point putting in a special case for 2G GSM to cache a single RAND here.
			// Cached authentication could also be used in the case where the Registrar is unreachable; but in that case we
			// should fall back to using IMSIs and fail open.  We dont handle that well at the moment.
			//"RANDUPPER INTEGER, "			// authentication token
			//"RANDLOWER INTEGER, "			// authentication token
			//"SRES INTEGER, "				// authentication token
#endif
			"kc varchar(33) default '', "	// returned by the Registrar, needed for ciphering.
			"RRLP_STATUS INTEGER DEFAULT 0, "	// Does MS support RRLP?  If so, where are we in the RRLP state machine?
			"DEG_LAT FLOAT, "				// RRLP result
			"DEG_LONG FLOAT, "				// RRLP result
			"ASSOCIATED_URI text default '', "	// Saved from the SIP REGISTER message and inserted into MOC SIP INVITE.
			"ASSERTED_IDENTITY text default '', "	// Saved from the SIP REGISTER message and inserted into MOC SIP INVITE.
			"WELCOME_SENT INTEGER DEFAULT 0,"	// 0 == welcome message not sent yet; 1 == sent by us; 2 == sent by someone else.
			// (pat) We keep the unauthorized entries for several reasons:
			// 1. so we can cache the unauthorized status to reduce loading on the Registrar by using cached unauthorization
			// 2. to prevent sending the (un)welcome message multiple times.
			// 3. if a previously authorized MS becomes unauthorized we want to keep the entry here to reserve the TMSI permanently.
			"AUTH INTEGER DEFAULT 0,"		// Authorization result, 0 == unauthorized.  See enum Authorization for other values.
			"AUTH_EXPIRY INTEGER DEFAULT 0,"	// Absolute time in seconds when authorization expires or 0 for single-use.
			"REJECT_CODE INTEGER DEFAULT 0,"		// Reject code, or 0 if authorized.
			"TMSI_ASSIGNED INTEGER DEFAULT 0,"	// Set when the TMSI has been successfully assigned to the MS, ie, the MS knows it.
			"PTMSI_ASSIGNED INTEGER DEFAULT 0"	// Set when the P-TMSI has been successfully assigned to the MS by the SGSN, ie, the MS knows it.

			// TODO: A bunch of stuff for GPRS.  GPRS could use the TMSI as the P-TMSI if it is limited to 30 bits.
		")";
};

// These are the mappings of allocated tmsis to/from the TMSI_TABLE.
static int tmsi2table(uint32_t tmsi)
{
	return (int) tmsi;
}
static uint32_t table2tmsi(int storedValue)
{
	return storedValue;
}
static bool tmsiIsValid(int rawTmsi)
{
	return rawTmsi != 0 && rawTmsi <= MAX_VALID_TMSI;
}


bool configTmsiTestMode()
{
	return gConfig.getNum("Control.LUR.TestMode");
}

bool configSendTmsis()
{
	return gConfig.getBool("Control.LUR.SendTMSIs");
}


// return true on success
bool TMSITable::runQuery(const char *query, int checkChanges) const
{
	int resultCode, changes=0;
	LOG(DEBUG)<<LOGVAR(query)<<LOGVAR(checkChanges);
	// (pat) It appears that sqlite3_changes returns 0 if the value being replaced matched what was already there.
	if (!sqlite_command(mTmsiDB,query,&resultCode) || (checkChanges && 0 == (changes = sqlite3_changes(mTmsiDB)))) {
		// The changes is only useful if checkChanges is set.
		LOG(ERR) << "TMSI table query failed:" <<LOGVAR(query) <<LOGVAR(resultCode) <<LOGVAR(changes) <<" error:"<<sqlite3_errmsg(mTmsiDB);
		return false;
	}
	return true;
}

// pat 9-2013: I am adding an extra table to hold attributes including a version number of the TMSI table file.
// (pat) If the TMSI_TABLE version does not match expected, drop the TMSI_TABLE before returning, and the caller will recreate it.
bool TMSITable::tmsiTabCheckVersion()
{
	string version = sqlite_get_attr(mTmsiDB,"VERSION");
	if (version == TmsiTableDefinition::tmsiTableVersion) {
		// Success!  The VERSION property matches the expected value.
		return true;
	}

	// Delete the existing tmsi table from the database, caller will recreate it.
	runQuery("DROP TABLE IF EXISTS TMSI_TABLE");

	// Set the version attribute.
	sqlite_set_attr(mTmsiDB,"VERSION",TmsiTableDefinition::tmsiTableVersion);
	return false;
}

// Delete expired TMSITable entries.
void TMSITable::tmsiTabCleanup()
{
	// Delete old TMSIs.
	unsigned maxage = gConfig.getNum("Control.TMSITable.MaxAge");	// In hours.
	if (maxage == 0 || maxage >= 99999) {return;}	// punt if disabled or ridiculous.
	unsigned oldest_allowed = time(NULL) - (maxage * 60*60);
	char query[102];
	snprintf(query,100,"DELETE FROM TMSI_TABLE WHERE ACCESSED <= %u",oldest_allowed);
	runQuery(query,false);
	int changes = sqlite3_changes(mTmsiDB);
	if (changes) {
		LOG(INFO) << "Deleted "<<changes<<" expired entries from TMSITable with age < Control.TMSITable.Maxage="<<maxage<<" hours";
	}
}

void TMSITable::tmsiTabClearAuthCache()
{
	runQuery("UPDATE TMSI_TABLE SET AUTH_EXPIRY=0");
}


void TMSITable::tmsiTabClear()
{
	runQuery("DELETE FROM TMSI_TABLE WHERE 1");
	//clearAuthFailures();
	//authFailures.clear();
}

void TMSITable::tmsiTabInit()
{
	tmsiTabCleanup();

	// Run through the tmsi table to find the highest number tmsi.
	// We could let sqlite just automatically create them except for the initial case
	// where we need to create the first fake tmsi.
	sHighestFakeTmsi = MAX_VALID_TMSI;
	if (1) {
		// Not that it matters, since this is done only once, but because TMSI is a primary key this query is probably optimized.
		sqlQuery qHighestTmsi(mTmsiDB,"TMSI_TABLE","max(TMSI)","");
		if (qHighestTmsi.sqlSuccess()) {
			unsigned highest_tmsi = qHighestTmsi.getResultInt(0);
			LOG(DEBUG) << "Highest TMSI="<<highest_tmsi;
			if (highest_tmsi > sHighestFakeTmsi) { sHighestFakeTmsi = highest_tmsi; }
		} else {
			// Possibly empty table.
		}
	} else {
		// old way, the brute-force approach.
		sqlQuery qinit(mTmsiDB,"TMSI_TABLE","TMSI","");
		while (qinit.sqlResultSize()) {
			int rawtmsi = qinit.getResultInt(0);
			if (rawtmsi > (int)sHighestFakeTmsi) { sHighestFakeTmsi = rawtmsi; }
			if (! qinit.sqlStep()) break;
		}
	}
}


int TMSITable::tmsiTabOpen(const char* wPath) 
{
	// FIXME -- We can't call the logger here because it has not been initialized yet.
	// (pat) I think this has been fixed - the TMSITable initialization is now in main().
	//printf("TMSITable::open(%s)\n",wPath); fflush(stdout);
	LOG(INFO) << "TMSITable::open "<<wPath;
	mTablePath = string(wPath);

	int rc = sqlite3_open(wPath,&mTmsiDB);
	if (rc) {
		// (pat) Lets just create the directory if it does not exist.
		char dirpath[strlen(wPath)+100];
		strcpy(dirpath,wPath);
		char *sp = strrchr(dirpath,'/');
		if (sp) {
			*sp = 0;
			mkdir(dirpath,0777);
			rc = sqlite3_open(wPath,&mTmsiDB);	// try try again.
		}
	}
	if (rc) {
		LOG(EMERG) << "Cannot open TMSITable database at " << wPath << ": " << sqlite3_errmsg(mTmsiDB);
		sqlite3_close(mTmsiDB);
		mTmsiDB = NULL;
		exit(1);
		//return 1;
	}

	tmsiTabCheckVersion();

	if (!sqlite_command(mTmsiDB,TmsiTableDefinition::create)) {
		LOG(EMERG) << "Cannot create TMSI table in file:" <<mTablePath <<" using:" <<TmsiTableDefinition::create ;
		exit(1);
        //return 1;
	}
	// Set high-concurrency WAL mode.
	if (!sqlite_command(mTmsiDB,enableWAL)) {
		LOG(EMERG) << "Cannot enable WAL mode on database at " << wPath << ", error message: " << sqlite3_errmsg(mTmsiDB);
	}
	LOG(INFO) << "Opened TMSI table version "<<TmsiTableDefinition::tmsiTableVersion<< ":"<<wPath;
	// (mike) 2014-06: to free ourselves of the in-tree sqlite3 code, the system library must be used.
	//        However, sqlite3_db_filename is not available in Ubuntu 12.04. Removing dependence for now.
	LOG(DEBUG) << "TEST sqlite3_db_filename:"<< wPath;//sqlite3_db_filename(mTmsiDB,"main");
	tmsiTabInit();
    return 0;
}

TMSITable::~TMSITable()
{
	if (mTmsiDB) sqlite3_close(mTmsiDB);
}

bool TMSITable::dropTmsi(uint32_t tmsi)
{
	char query[100];
	LOG(DEBUG) << "Removing TMSITable entry for"<<LOGVAR(tmsi);
	snprintf(query,100,"DELETE FROM TMSI_TABLE WHERE TMSI == %d",tmsi2table(tmsi));
	return runQuery(query,1);
}

bool TMSITable::dropImsi(const char *imsi)
{
	char query[100];
	LOG(DEBUG) << "Removing TMSITable entry for"<<LOGVAR(imsi);
	snprintf(query,100,"DELETE FROM TMSI_TABLE WHERE IMSI == '%s'",imsi);
	return runQuery(query,1);
}


#if UNUSED
void TMSITable::tmsiTabSetAuthAndAssign(string imsi,int auth, int assigned)
{
	char query[100];
	snprintf(query,100,"UPDATE TMSI_TABLE SET AUTH=%u, ASSIGNED=%u WHERE IMSI == '%s'",auth,assigned,imsi.c_str());
	runQuery(query,1);
}
#endif

// This does nothing if the IMSI is not found in the table.
void TMSITable::tmsiTabSetRejected(string imsi,int rejectCode)
{
	char query[100];
	snprintf(query,100,"UPDATE TMSI_TABLE SET AUTH=0,REJECT_CODE=%d WHERE IMSI == '%s'",rejectCode,imsi.c_str());
	runQuery(query,1);
}

struct TSqlString : public string {
	char buf[100];
	void appendf(const char *fmt,int val) { snprintf(buf,100,fmt,val); append(buf); }
	void appendf(const char *fmt,const char *val) { snprintf(buf,100,fmt,val); append(buf); }
	void appendf(const char *fmt,string val) { snprintf(buf,100,fmt,val.c_str()); append(buf); }
	void appendf(const char *fmt,const char *a,string b) { snprintf(buf,100,fmt,a,b.c_str()); append(buf); }
	void appendf(const char *fmt,const char *a,int b) { snprintf(buf,100,fmt,a,b); append(buf); }
	//void addc(const char *name) { if (cnt++) append(","); append(name); }
	//void addci(int ival) { if (cnt++) append(","); appendf("%d",ival); }
	//void addcs(string sval) { if (cnt++) append(","); appendf("'%s'",sval.c_str()); }
	TSqlString() { reserve(150); }
};

struct TSqlQuery : public TSqlString {
	virtual void addc(const char *name,unsigned ival) = 0;
	virtual void addc(const char *name,string sval) = 0;
	virtual void finish() = 0;
	void addStore(TmsiTableStore *store);
};

struct TSqlUpdate : public TSqlQuery {
	Int_z cnt;		// Count of commas.
	void addc(const char *name,string sval) {
		if (cnt++) this->append(",");
		this->appendf("%s='%s'",name,sval);
	}
	void addc(const char *name,unsigned ival) {
		if (cnt++) this->append(",");
		this->appendf("%s=%u",name,ival);
	}
	void finish() {}
};

struct TSqlInsert : public TSqlQuery {
	TSqlString values;
	Int_z cnt;		// Count of commas.
	void addc(const char *name, string sval) {
		if (cnt++) { this->append(","); values.append(","); }
		this->append(name);
		values.appendf("'%s'",sval);
	}
	void addc(const char *name, unsigned ival) {
		if (cnt++) { this->append(","); values.append(","); }
		this->append(name);
		values.appendf("%u",ival);
	}
	void finish() {
		append(") VALUES (");
		append(this->values);
		append(")");
	}
};

// Flush anything that has changed out to the TMSI table in this SQL query.
void TSqlQuery::addStore(TmsiTableStore *store)
{
	if (store->imei_changed) { addc("IMEI",store->imei); store->imei_changed = false; }
	if (store->auth_changed) { addc("AUTH",store->auth); store->auth_changed = false; }
	if (store->authExpiry_changed) { addc("AUTH_EXPIRY",store->authExpiry); store->authExpiry_changed = false; }
	if (store->rejectCode_changed) { addc("REJECT_CODE",store->rejectCode); store->rejectCode_changed = false; }
	if (store->assigned_changed) { addc("TMSI_ASSIGNED",store->assigned); store->assigned_changed = false; }
	if (store->a5support_changed) { addc("A5_SUPPORT",store->a5support); store->a5support_changed = false; }
	if (store->powerClass_changed) { addc("POWER_CLASS",store->powerClass); store->powerClass_changed = false; }
	if (store->kc_changed) { addc("kc",store->kc); store->kc_changed = false; }
	if (store->associatedUri_changed) { addc("ASSOCIATED_URI",store->associatedUri); store->associatedUri_changed = false; }
	if (store->assertedIdentity_changed) { addc("ASSERTED_IDENTITY",store->assertedIdentity); store->assertedIdentity_changed = false; }
	if (store->welcomeSent_changed) { addc("WELCOME_SENT",store->welcomeSent); store->welcomeSent_changed = false; }
}



unsigned TMSITable::allocateTmsi()
{
	int tmsi;

	// For testing, we will deliberately screw something up.
	if (configTmsiTestMode()) {
		// Deliberately over-write an existing entry to create a TMSI collision.
		// Must use at least two phones - the second one will be assigned the same tmsi as the first.
		sqlQuery q8(mTmsiDB,"TMSI_TABLE","TMSI","");	// Returns all the table rows one by one.
		if (q8.sqlSuccess() == false) {
			WATCH("TMSI table is empty");
		} else {
			int rawtmsi = q8.getResultInt();
			if (tmsiIsValid(rawtmsi)) {
				unsigned tmsi = table2tmsi(rawtmsi);
				WATCH("TMSI table test mode: created deliberate tmsi collision for"<<LOGVAR(tmsi));
				// We must delete the existing entry or we will not be able to reassign it.
				dropTmsi(tmsi);
				return tmsi;
			}
		}
	}

	// Make a new random tmsi that is not in the TMSI table.
	// Since the source code is public, there is no point using a random number generator unless we use a truly random seed.
	// The result may be used as a P-TMSI so it must be less than 30 bits.
	const unsigned mask = 0xfffff;		// We will use a 20 bit tmsi.  That's plenty.
	struct timeval now;
	gettimeofday(&now,NULL);
	unsigned int seed = now.tv_sec + now.tv_usec;
	while (1) {
		tmsi = rand_r(&seed) & mask;
		// (pat) Skip the first 1000 tmsis.  Those low numbers were used by OpenBTS 3.x and we'll just be safe
		// and skip them entirely.
		if (tmsi < 1000) { tmsi += 1000; }
		WATCH("testing"<<LOGVAR(tmsi)<<LOGVAR(seed));
		if (tmsi == 0) continue;
		sqlQuery q7(mTmsiDB,"TMSI_TABLE","TMSI","TMSI",tmsi2table(tmsi));
		if (0 == q7.sqlSuccess()) { break; }
	} 
	return tmsi;
}

void TMSITable::tmsiTabUpdate(string imsi, TmsiTableStore *store)
{
	LOG(INFO) << "update entry for"<<LOGVAR(imsi) <<LOGVAR(store->auth_changed)<<LOGVAR(store->auth)<<LOGVAR(store->assigned_changed)<<LOGVAR(store->assigned);

	ScopedLock lock(sTmsiMutex,__FILE__,__LINE__); // This lock should be redundant - sql serializes access, but it may prevent sql retry failures.
	TSqlUpdate q1;
	q1.append("UPDATE TMSI_TABLE SET ");
	q1.addStore(store);
	if (! q1.cnt) { return; }	// Nothing changed.
	q1.addc("ACCESSED",(unsigned)time(NULL));
	q1.appendf(" WHERE IMSI='%s'",imsi);	// You must include the quotes around IMSI
	runQuery(q1.c_str(),true);
}

// Update or create a new entry in the TMSI table.
// If we are assigning TMSIs to the MS, return the new TMSI, else 0.
// This routine checks to see if the record for this IMSI already exists; the caller actually knows this and could inform us,
// however there is a great deal of logic with many options between the original TMSITable lookup and updating
// the TMSITable entry here, so I deemed it more robust and resistant to future changes if this routine ignores that
// and does another lookup of the IMSI to see if the record already exists.
uint32_t TMSITable::tmsiTabCreateOrUpdate(
	const string imsi,
	TmsiTableStore *store,
	const GSM::L3LocationAreaIdentity * lai,
	uint32_t oldTmsi)
{
	// Create or find an entry based on IMSI.
	// Return assigned TMSI.
	assert(mTmsiDB);
	bool sendTmsis = configSendTmsis();
	unsigned now = (unsigned)time(NULL);

	ScopedLock lock(sTmsiMutex,__FILE__,__LINE__); // This lock should be redundant - sql serializes access, but it may prevent sql retry failures.

	unsigned oldRawTmsi = tmsiTabGetTMSI(imsi,false);
	bool isNewRecord = (oldRawTmsi == 0);

	TSqlQuery *queryp;	// dufus language
	TSqlInsert foo;
	TSqlUpdate bar;
	if (isNewRecord) {
		// Create a new record.
		queryp = &foo; queryp->reserve(200);
		queryp->append("INSERT INTO TMSI_TABLE (");
		queryp->addc("IMSI",imsi);
		queryp->addc("CREATED",now);
		queryp->addc("ACCESSED",now);
	} else {
		queryp = &bar; queryp->reserve(200);
		// Update existing record.
		queryp->append("UPDATE TMSI_TABLE SET ");
	}
	uint32_t tmsi = 0;

	if (sendTmsis) {
		// We are handling several cases here.  If it is a new record we are allocating a new tmsi for the first time,
		// or if it is an existing record that was created without an assigned tmsi, ie, it has a fake tmsi,
		// then we are updating it to a real tmsi.
		// We never go backwards, ie, once we allocate a real tmsi, we never change it back to a fake tmsi,
		// because we want to reserve the tmsi for this handset effectively forever.
		if (! tmsiIsValid(oldRawTmsi)) {
			gReports.incr("OpenBTS.GSM.MM.TMSI.Assigned");
			tmsi = allocateTmsi();
			queryp->addc("TMSI",tmsi2table(tmsi));
		} else {
			tmsi = oldRawTmsi;
		}
	} else {
		if (isNewRecord) {
			// We are creating a new record.
			// sqlite3 would auto-assign a unique TMSI higher than any other in
			// the table, but we need to handle the initial case for the first fake tmsi.
			tmsi = ++sHighestFakeTmsi;
			queryp->addc("TMSI",tmsi2table(tmsi));
		}
	}
	queryp->addStore(store);

	if (isNewRecord && lai) {
		queryp->addc("OLD_MCC",lai->MCC());
		queryp->addc("OLD_MNC",lai->MNC());
		queryp->addc("OLD_LAC",lai->LAC());
		queryp->addc("OLD_TMSI",oldTmsi);
		queryp->finish();
	} else {
		queryp->append(format(" WHERE IMSI=='%s'",imsi));
	}

	if (!runQuery(queryp->c_str(),1)) {
		LOG(ALERT) << "TMSI creation failed for"<<LOGVAR(imsi)<<" query:"<<*queryp;
		return 0;
	}
	LOG(INFO) << (isNewRecord ? "new" : "updated") <<" entry for"<<LOGVAR(imsi)<<LOGHEX(tmsi);

	if (sendTmsis) {	// double check to make sure the database entry made it.
		unsigned tmsicheck;
		if (!sqlite3_single_lookup(mTmsiDB,"TMSI_TABLE","IMSI",imsi.c_str(),"TMSI",tmsicheck) || table2tmsi(tmsicheck) != tmsi) {
			LOG(ERR) << "TMSI database inconsistancy"<<LOGVAR(imsi)<<LOGVAR(tmsi)<<LOGVAR(tmsicheck);
			return 0;
		}
	}
	return tmsi;
}

#if 0	// not so old version
uint32_t TMSITable::tmsiTabAssign(const string imsi, const GSM::L3LocationAreaIdentity * lai, uint32_t oldTmsi,TmsiTableStore *store)
{
	// Create or find an entry based on IMSI.
	// Return assigned TMSI.
	assert(mTmsiDB);

	gReports.incr("OpenBTS.GSM.MM.TMSI.Assigned");

	ScopedLock lock(sTmsiMutex,__FILE__,__LINE__); // This lock should be redundant - sql serializes access, but it may prevent sql retry failures.

	uint32_t tmsi = 0;

	// Create a new record.
	LOG(INFO) << "new entry for"<<LOGVAR(imsi)<<LOGVAR(tmsi);
	unsigned now = (unsigned)time(NULL);
	TSqlInsert query; query.reserve(150);
	bool sendTmsis = configSendTmsis();
	query.append("INSERT INTO TMSI_TABLE (");
	query.addc("IMSI",imsi);
	query.addc("CREATED",now);
	query.addc("ACCESSED",now);
	if (sendTmsis) {
		tmsi = allocateTmsi();
		query.addc("TMSI",tmsi2table(tmsi));
	} else {
		// Nothing needed.  sqlite3 woult auto-assign a unique TMSI higher than any other in
		// the table, but we need to handle the initial case for the first fake tmsi.
		tmsi = ++sHighestFakeTmsi;
		query.addc("TMSI",tmsi2table(tmsi));
	}
	query.addStore(store);

	if (lai) {
		query.addc("OLD_MCC",lai->MCC());
		query.addc("OLD_MNC",lai->MNC());
		query.addc("OLD_LAC",lai->LAC());
		query.addc("OLD_TMSI",oldTmsi);
	}
	query.finish();
	if (!runQuery(query.c_str(),1)) {
		LOG(ALERT) << "TMSI creation failed, query:"<<query;
		return 0;
	}

	if (sendTmsis) {	// double check to make sure the database entry made it.
		unsigned tmsicheck;
		if (!sqlite3_single_lookup(mTmsiDB,"TMSI_TABLE","IMSI",imsi.c_str(),"TMSI",tmsicheck) || table2tmsi(tmsicheck) != tmsi) {
			LOG(ERR) << "TMSI database inconsistancy"<<LOGVAR(imsi)<<LOGVAR(tmsi)<<LOGVAR(tmsicheck);
			return 0;
		}
	}
	return tmsi;
}
#endif

#if old_version
// Create a new entry in the TMSI table.
// This also set the AUTH status to authorized; it is only called on registration success.
// If we are assigning TMSIs to the MS, return the new TMSI, else 0.
uint32_t TMSITable::assign(const string imsi, const GSM::L3LocationAreaIdentity * lai, uint32_t oldTmsi, const string imei)
{
	// Create or find an entry based on IMSI.
	// Return assigned TMSI.
	assert(mTmsiDB);

	gReports.incr("OpenBTS.GSM.MM.TMSI.Assigned");

	ScopedLock lock(sTmsiMutex,__FILE__,__LINE__); // This lock should be redundant - sql serializes access, but it may prevent sql retry failures.

	uint32_t tmsi = 0;

	// Create a new record.
	LOG(INFO) << "new entry for"<<LOGVAR(imsi)<<LOGVAR(tmsi);
	string query;
	query.append("INSERT INTO TMSI_TABLE (");
	bool sendTmsis = configSendTmsis();
	if (sendTmsis) {
		tmsi = allocateTmsi();
		query.add("TMSI",tmsi);
	}
	query.add("IMSI",imsi);
	unsigned now = (unsigned)time(NULL);
	query.add("CREATED",now);
	query.add("ACCESSED",now);
	if (lai) {
		query.add("OLD_MCC",lai->MCC());
		query.add("OLD_MNC",lai->MNC());
		query.add("OLD_LAC",lai->LAC());
		query.add("OLD_TMSI",oldTmsi);
	}
	query.append(") VALUES (");
	query.append(values);
	query.append(")");
	if (!runQuery(query.c_str(),1)) {
		LOG(ALERT) << "TMSI creation failed, query:"<<query;
		return 0;
	}

	if (sendTmsis) {
		unsigned tmsicheck;
		if (!sqlite3_single_lookup(mTmsiDB,"TMSI_TABLE","IMSI",imsi.c_str(),"TMSI",tmsicheck) || tmsicheck != tmsi) {
			LOG(ERR) << "TMSI database inconsistancy"<<LOGVAR(imsi)<<LOGVAR(tmsi)<<LOGVAR(tmsicheck);
			return 0;
		}
	}
	return tmsi;
}
#endif



// Update timestamp by TMSI.
void TMSITable::tmsiTabTouchTmsi(unsigned TMSI) const
{
	char query[100];
	snprintf(query,100,"UPDATE TMSI_TABLE SET ACCESSED = %u WHERE TMSI == %d", (unsigned)time(NULL),tmsi2table(TMSI));
	runQuery(query);
}

// Update timestamp by IMSI.
void TMSITable::tmsiTabTouchImsi(string IMSI) const
{
	char query[100];
	snprintf(query,100,"UPDATE TMSI_TABLE SET ACCESSED = %u WHERE IMSI == '%s'", (unsigned)time(NULL),IMSI.c_str());
	runQuery(query);
}

void TMSITable::tmsiTabReallocationComplete(unsigned TMSI) const
{
	ScopedLock lock(sTmsiMutex,__FILE__,__LINE__); // This lock should be redundant - sql serializes access, but it may prevent sql retry failures.
	char query[100];
	snprintf(query,100,"UPDATE TMSI_TABLE SET TMSI_ASSIGNED = 1 WHERE TMSI == %d",tmsi2table(TMSI));
	runQuery(query);
}


// Pop all the fields we use during mobility management authorization out of the TMSI table.
bool TMSITable::tmsiTabGetStore(string imsi, TmsiTableStore *store) const
{
	store->store_valid = true;	// We have either updated the store or confirmed the imsi does not exist in the TMSI_TABLE.
	sqlQuery q11(mTmsiDB,"TMSI_TABLE","AUTH,AUTH_EXPIRY,TMSI_ASSIGNED,REJECT_CODE,WELCOME_SENT", "IMSI",imsi.c_str());
	if (!q11.sqlSuccess()) {
		LOG(INFO) << "No TMSI_TABLE table entry for"<<LOGVAR(imsi) <<LOGVAR2("query",q11.mQueryString);
		return false;
	}
	store->auth = (Authorization) q11.getResultInt(0);			//store->auth_valid = true;
	store->authExpiry = q11.getResultInt(1);	//store->authExpiry_valid = true;
	store->assigned = q11.getResultInt(2);		//store->assigned_valid = true;
	store->rejectCode = q11.getResultInt(3);	//store->rejectCode_valid = true;
	store->welcomeSent = q11.getResultInt(4);	//store->welcomeSent_valid = true;
	LOG(DEBUG) <<LOGVAR2("auth",store->auth)<<LOGVAR2("authExpiry",store->authExpiry)
		<<LOGVAR2("assigned",store->assigned)<<LOGVAR2("rejectCode",store->rejectCode)<<LOGVAR2("welcomSent",store->welcomeSent);
	return true;
}


string TMSITable::tmsiTabGetIMSI(unsigned tmsi, unsigned *pAuthorizationResult) const
{
	string imsi;
	if (pAuthorizationResult) tmsiTabTouchTmsi(tmsi);
	sqlQuery q4(mTmsiDB,"TMSI_TABLE","IMSI,AUTH", "TMSI",tmsi2table(tmsi));
	if (!q4.sqlSuccess()) {
		if (pAuthorizationResult) { *pAuthorizationResult = 0; }
	} else {
		imsi = q4.getResultText(0);
		int auth = q4.getResultInt(1);
		if (pAuthorizationResult) { *pAuthorizationResult = auth; }
		LOG(DEBUG) <<LOGVAR(tmsi) <<LOGVAR(imsi) <<LOGVAR(auth);
	}
	return imsi;
}

unsigned TMSITable::tmsiTabCheckAuthorization(string imsi) const
{
	tmsiTabTouchImsi(imsi);
	unsigned auth = 0;
	if (! sqlite3_single_lookup(mTmsiDB,"TMSI_TABLE","IMSI",imsi.c_str(),"AUTH",auth)) {
		return 0;	// If IMSI not found, unauthorized.
	}
	return auth;
}

// If onlyIfKnown only return the TMSI if the handset has received and acknowleged it.
// Note that if onlyIfKnown is false, this may return invalid TMSIs; the caller must check validity.
unsigned TMSITable::tmsiTabGetTMSI(const string imsi, bool onlyIfKnown) const
{
	ScopedLock lock(sTmsiMutex,__FILE__,__LINE__); // This lock should be redundant - sql serializes access, but it may prevent sql retry failures.
	sqlQuery q3(mTmsiDB,"TMSI_TABLE","TMSI,TMSI_ASSIGNED", "IMSI",imsi.c_str());
	if (! q3.sqlSuccess()) {
		LOG(DEBUG) << "not found"<<LOGVAR(imsi);
		return 0;
	} else {
		LOG(DEBUG) << "found"<<LOGVAR(imsi)<<LOGVAR(onlyIfKnown)<<LOGVAR(q3.getResultInt(0))<<LOGVAR(q3.getResultInt(1));
		int rawtmsi = q3.getResultInt(0);	// the returned tmsi.
		int tmsiAssigned = q3.getResultInt(1);	// true if the tmsi has been sent to the handset in a tmsi assignment.
		if (onlyIfKnown && tmsiAssigned == 0) { return 0; }
		return table2tmsi(rawtmsi);		// the returned tmsi.
	}
}


void printAge(unsigned seconds, ostream& os)
{
	static const unsigned k=5;
	os << setw(4);
	if (seconds<k*60) {
		os << seconds << 's';
		return;
	}
	unsigned minutes = (seconds+30) / 60;
	if (minutes<k*60) {
		os << minutes << 'm';
		return;
	}
	unsigned hours = (minutes+30) / 60;
	if (hours<k*24) {
		os << hours << 'h';
		return;
	}
	os << (hours+12)/24 << 'd';
}

// Print the seconds as a low-precision age in seconds, minutes, hours, or days.
string prettyAge(unsigned seconds)
{
	static const unsigned k=5;
	if (seconds<k*60) { return format("%ds",seconds); }
	unsigned minutes = (seconds+30) / 60;
	if (minutes<k*60) { return format("%dm",minutes); }
	unsigned hours = (minutes+30) / 60;
	if (hours<k*24) { return format("%dh",hours); }
	return format("%dd", (hours+12)/24);
}


vector< vector<string> > TMSITable::tmsiTabView(int verbosity, bool rawFlag, unsigned maxrows) const
{
	vector< vector<string> > view;
	vector<string> vh1;

	string header1 = verbosity >= 2 ? TmsiTableDefinition::longerHeader :
	                      verbosity == 1 ? TmsiTableDefinition::longHeader : TmsiTableDefinition::shortHeader;
	vector<string> headers = stringSplit(vh1,header1.c_str());
	view.push_back(headers);

	string columns = replaceAll(header1," ",",");
	sqlQuery query(mTmsiDB,"TMSI_TABLE",columns.c_str(),"ORDER BY ACCESSED DESC");	// descending
	unsigned nrows = 1;
	time_t now = time(NULL);
	// If the table is completely empty this returns a row of all empty values.
	while (query.sqlResultSize() && (nrows++ < maxrows)) {
		// Print out the columns.
		vector<string> row; row.clear();
		unsigned col = 0;
		for (col = 0; col < vh1.size(); col++) {
			string header = headers[col];
			//if (header.find("TMSI") != string::npos)	// Needed if we add PTMSI
			if (header == "TMSI") {
				// TMSI.  Use hex and fix negative numbers.
				int rawtmsi = query.getResultInt(col);
				if (rawFlag || tmsiIsValid(rawtmsi)) {
					unsigned tmsi = table2tmsi(rawtmsi);
					row.push_back(format("0x%x",(unsigned) tmsi));
				} else {
					// This is a dummy tmsi created by sqlite.
					row.push_back("-");
				}
			} else if (header == "CREATED" || header == "ACCESSED") {
				// Print seconds as a time value.
				row.push_back(prettyAge(now - query.getResultInt(col)));
			} else if (header == "AUTH_EXPIRY") {
				// The expiry is the absolute time when it expires, or 0 if unknown.
				int expiry = query.getResultInt(col);
				int remaining = expiry - now;
				if (remaining < 0) remaining = 0;
				row.push_back(expiry ? prettyAge(remaining) : "-");
			} else {
				// All other column types.  If they are integer they are converted to "" if NULL else decimal.
				row.push_back(query.getResultText(col));
			}
		}
		view.push_back(row);
		if (! query.sqlStep()) break;
	}
	return view;
}


void TMSITable::tmsiTabDump(int verbosity,bool rawFlag, ostream& os, bool showAll, bool taboption) const
{
	// Dump the TMSI table.
	unsigned maxrows = showAll ? 10000000 : 100;
	vector< vector<string> > view = tmsiTabView(verbosity, rawFlag, maxrows);

#if unused
	// Add the IMSI authorization failures.  They dont have TMSIs, or any other information.
	vector<string> failList;
	getAuthFailures(failList);
	for (vector<string>::iterator it = failList.begin(); it != failList.end(); it++) {
		if (view.size() >= maxrows) break;
		vector<string> failureRow;
		failureRow.push_back(*it);		// The IMSI;
		failureRow.push_back(string("-"));		// TMSI
		failureRow.push_back(string("0"));		// AUTH
		view.push_back(failureRow);
	}
#endif

	if (view.size() >= maxrows) {
		vector<string> tmp;
		tmp.push_back(string("..."));
		view.push_back(tmp);
	}

	printPrettyTable(view,os,taboption);

#if 0	// previous tmsi table dumping code.
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(mTmsiDB,&stmt,"SELECT TMSI,IMSI,CREATED,ACCESSED FROM TMSI_TABLE ORDER BY ACCESSED DESC")) {
		LOG(ERR) << "sqlite3_prepare_statement failed";
		return;
	}
	time_t now = time(NULL);
	while (sqlite3_run_query(mTmsiDB,stmt)==SQLITE_ROW) {
		os << hex << setw(8) << sqlite3_column_int64(stmt,0) << ' ' << dec;
		os << sqlite3_column_text(stmt,1) << ' ';
		printAge(now-sqlite3_column_int(stmt,2),os); os << ' ';
		printAge(now-sqlite3_column_int(stmt,3),os); os << ' ';
		os << endl;
	}
	sqlite3_finalize(stmt);
#endif
}



#if UNUSED
void TMSITable::setIMEI(string IMSI, string IMEI)
{
	// If the IMEI has changed, update it and also reset RRLP_STATUS.
	// The A5_SUPPORT and POWER_CLASS have to change too, but that is done by classmark().
	if (IMEI.size()) {
		sqlQuery qu(mTmsiDB,"TMSI_TABLE","IMEI","IMSI",IMSI.c_str());
		string oldIMEI = qu.getResultText(0);
		if (oldIMEI != IMEI) {
			LOG(INFO) << "Updating" <<LOGVAR(IMSI) << " from" <<LOGVAR(oldIMEI) <<" to" <<LOGVAR(IMEI);
			char query[100];
			snprintf(query,100,"UPDATE TMSI_TABLE SET IMEI='%s',RRLP_STATUS=0 WHERE IMSI == '%s'",IMEI.c_str(),IMSI.c_str());
			runQuery(query,1);
		}
	}
}
#endif



bool TMSITable::classmark(const char* IMSI, const GSM::L3MobileStationClassmark2& classmark)
{
	int A5Bits = classmark.getA5Bits();
	char query[100];
	snprintf(query,100,
		"UPDATE TMSI_TABLE SET A5_SUPPORT=%u,ACCESSED=%u,POWER_CLASS=%u "
		" WHERE IMSI=\"%s\"",
		A5Bits,(unsigned)time(NULL),classmark.powerClass(),IMSI);
	runQuery(query,1);
	return true;
}


// Return 0 for no encryption, or the algorithm number, ie, 1 means A5_1, 2 means A5_2, etc.
unsigned getPreferredA5Algorithm(unsigned A5Bits)
{
	if (A5Bits & GSM::EncryptionAlgorithm::Bit5_3) return 3;
	// if (A5Bits & GSM::EncryptionAlgorithm::Bit5_2) return 2;	// not supported
	if (A5Bits & GSM::EncryptionAlgorithm::Bit5_1) return 1;
	return 0;
}

int TMSITable::tmsiTabGetPreferredA5Algorithm(const char* IMSI)
{
	sqlQuery query(mTmsiDB,"TMSI_TABLE","A5_SUPPORT", "IMSI",IMSI);
	if (!query.sqlSuccess()) return 0;
	int cm = query.getResultInt(0);
#if 0
	char query[200];
	snprintf(query,200, "SELECT A5_SUPPORT from TMSI_TABLE WHERE IMSI=\"%s\"", IMSI);
	sqlite3_stmt *stmt;
	if (sqlite3_prepare_statement(mTmsiDB,&stmt,query)) {
		LOG(ERR) << "sqlite3_prepare_statement failed for " << query;
		return 0;
	}
	if (sqlite3_run_query(mTmsiDB,stmt)!=SQLITE_ROW) {
		// Returning false here just means the IMSI is not there yet.
		sqlite3_finalize(stmt);
		return 0;
	}
	int cm = sqlite3_column_int(stmt,0);
	sqlite3_finalize(stmt);
#endif
	return getPreferredA5Algorithm(cm);
}


#if CACHE_AUTH
void TMSITable::putAuthTokens(const char* IMSI, uint64_t upperRAND, uint64_t lowerRAND, uint32_t SRES)
{
	char query[300];
	snprintf(query,300,"UPDATE TMSI_TABLE SET RANDUPPER=%llu,RANDLOWER=%llu,SRES=%u,ACCESSED=%u WHERE IMSI=\"%s\"",
		upperRAND,lowerRAND,SRES,(unsigned)time(NULL),IMSI);
	if (! (sqlite_command(mTmsiDB,query) && 1 == sqlite3_changes(mTmsiDB))) {
		LOG(ALERT) << "cannot write to TMSI table";
	}
}

bool TMSITable::getAuthTokens(const char* IMSI, uint64_t& upperRAND, uint64_t& lowerRAND, uint32_t& SRES) const
{
	TSqlQuery query(mTmsiDB, "TMSI_TABLE", "RANDUPPER,RANDLOWER,SRES", "IMSI", IMSI);
	if (query.sqlResultSize() == 3) {
		upperRAND = query.getResultInt(0);
		lowerRAND = query.getResultInt(1);
		SRES = query.getResultInt(2);
		return true;
	}
	return false;
}
#endif

#if UNUSED
// Note that we overwrite the P-Associated-URI and P-Asserted-Identity, even if the incoming are empty.  If we dont know them, we dont know them.
void TMSITable::putKc(const char* imsi, string Kc, string pAssociatedUri, string pAssertedIdentity)
{
	char query[200];
	snprintf(query,200,"UPDATE TMSI_TABLE SET kc='%s',ASSOCIATED_URI='%s',ASSERTED_IDENTITY='%s' WHERE IMSI='%s'", Kc.c_str(),  pAssociatedUri.c_str(),pAssertedIdentity.c_str(),imsi);
	// And I quote sqlite.org documentation for "UPDATE": "It is not an error if the WHERE clause does not evaluate true for any row in the table".
	// So if if the IMSI is not found this does not return an error.  We have to check sqlite3_changes() to see if a row changed.
	if (! runQuery(query,1)) {
		// We dont write the query because it has Kc in it.
		LOG(ALERT) << "cannot write Kc or asserted identities to TMSI table for"<<LOGVAR(imsi);
	}
}
#endif

string TMSITable::getKc(const char* IMSI) const
{
	string Kcs;
	if (!sqlite_single_lookup( mTmsiDB, "TMSI_TABLE", "IMSI", IMSI, "kc", Kcs)) {
		LOG(ERR) << "sqlite3_single_lookup failed to find kc for " << IMSI;
		return "";
	}
	return Kcs;
}

void TMSITable::getSipIdentities(string imsi, string &pAssociatedUri,string &pAssertedIdentity) const
{
	sqlQuery query(mTmsiDB, "TMSI_TABLE", "ASSOCIATED_URI,ASSERTED_IDENTITY", "IMSI", imsi.c_str());
	if (query.sqlResultSize() == 2) {
		pAssociatedUri = query.getResultText(0);
		pAssertedIdentity = query.getResultText(1);
	}
	LOG(DEBUG) <<LOGVAR(imsi) <<LOGVAR(pAssociatedUri) <<LOGVAR(pAssertedIdentity);
}

};	// namespace


// vim: ts=4 sw=4
