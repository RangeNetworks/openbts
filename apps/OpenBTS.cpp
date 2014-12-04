/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012, 2013, 2014 Range Networks, Inc.
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

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <config.h>	// For VERSION
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "OpenBTSConfig.h"
std::vector<std::string> configurationCrossCheck(const std::string& key);
std::string getARFCNsString(unsigned band);
// Load configuration from a file.
static const char *cOpenBTSConfigEnv = "OpenBTSConfigFile";
static const char *cOpenBTSConfigFile = getenv(cOpenBTSConfigEnv)?getenv(cOpenBTSConfigEnv):"/etc/OpenBTS/OpenBTS.db";
OpenBTSConfig gConfig(cOpenBTSConfigFile,"OpenBTS", getConfigurationKeys());


#include <Logger.h>
Log dummy("openbts",gConfig.getStr("Log.Level").c_str(),LOG_LOCAL7);

// Set up the performance reporter.
#include <Reporting.h>
ReportingTable gReports(gConfig.getStr("Control.Reporting.StatsTable").c_str());

#include <TRXManager.h>
//#include <GSML1FEC.h>
#include <GSMConfig.h>
//#include <GSMSAPMux.h>
//#include <GSML3RRMessages.h>
#include <GSMLogicalChannel.h>

#include <ControlTransfer.h>
#include <Control/TMSITable.h>

#include <Globals.h>

#include <CLI.h>
#include <PowerManager.h>
#include <Configuration.h>
#include <PhysicalStatus.h>
#include <SIP2Interface.h>
#include "NeighborTable.h"
#include <Peering.h>
#include <GSML3RRElements.h>
#include <NodeManager.h>

#include <sys/wait.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include "SelfDetect.h"

// (pat) mcheck.h is for mtrace, which permits memory leak detection.
// Set env MALLOC_TRACE=logfilename
// Call mtrace() in the program.
// post-process the logfilename with mtrace (a perl script.)
//#include <mcheck.h>

using namespace std;
using namespace GSM;

const char* gDateTime = TIMESTAMP_ISO;


// All of the other globals that rely on the global configuration file need to
// be declared here.
// (pat) That is because the order that constructors are called is indeterminate, and we must
// ensure that the ConfigurationTable is constructed before any other classes.
// In general it is unwise to put non-trivial initialization code in constructors for this reason.
// If you dont call gConfig in your class constructor, you dont need to init your class here.
// Another way to handle this would be to substitute gConfig.get...(...) throughout OpenBTS with
// a function call that inits the ConfigurationTable if needed.
// It would be much kinder on the compiler as well.  And if someone goes to that effort, while you
// are at it change the char* arguments to constants.

// The TMSI Table.
//moved to Control directory: Control::TMSITable gTMSITable;

// The transaction table.
// moved to Control directory: Control::TransactionTable gTransactionTable;

// Physical status reporting
GSM::PhysicalStatus gPhysStatus;

// Configure the BTS object based on the config file.
// So don't create this until AFTER loading the config file.
GSMConfig gBTS;

// Note to all from pat:
// It is inadvisable to statically initialize any non-trivial entity here because
// the underlying dependencies may not yet have undergone their static initialization.
// For example, if any of these throw an alarm, the system will crash because
// the Logger may not have been initialized yet.

// Our interface to the software-defined radio.
TransceiverManager gTRX(gConfig.getNum("GSM.Radio.ARFCNs"), gConfig.getStr("TRX.IP").c_str(), gConfig.getNum("TRX.Port"));

/** The global peering interface. */
Peering::PeerInterface gPeerInterface;

/** The global neighbor table. */
Peering::NeighborTable gNeighborTable;

/** The remote node manager. */
NodeManager gNodeManager;

/** Define a function to call any time the configuration database changes. */
void purgeConfig(void*,int,char const*, char const*, sqlite3_int64)
{
	// (pat) NO NO NO.  Do not call LOG from here - it may result in infinite recursion.
	// LOG(INFO) << "purging configuration cache";
	gConfig.purge();
	gConfig.configUpdateKeys();
	// (pat) FIXME: We cannot regenerate the beacon too often because the changemark is only 2 bits;
	// we need to be more careful to update the beacon only when it really changes.
	gBTS.regenerateBeacon();
	gResetWatchdog();
	gLogGroup.setAll();
}



const char* transceiverPath = "./transceiver";

pid_t gTransceiverPid = 0;

void startTransceiver()
{
	//if local kill the process currently listening on this port
	if (gConfig.getStr("TRX.IP") == "127.0.0.1"){
		char killCmd[32];
		snprintf(killCmd,31,"fuser -k -n udp %d",(int)gConfig.getNum("TRX.Port"));
		if (system(killCmd)) {}
	}

	// Start the transceiver binary, if the path is defined.
	// If the path is not defined, the transceiver must be started by some other process.
	char TRXnumARFCN[4];
	sprintf(TRXnumARFCN,"%1d",(int)gConfig.getNum("GSM.Radio.ARFCNs"));
	//std::string extra_args = gConfig.getStr("TRX.Args");	// (pat 3-2014) remvoed pending demonstrated need.
	string usernotice = format("starting transceiver %s with %s ARFCNs", transceiverPath, TRXnumARFCN);
	if (getenv(cOpenBTSConfigEnv)) {
		usernotice += " using config file: ";
		usernotice += cOpenBTSConfigFile;
	}

	static char *argv[10]; int argc = 0;
	argv[argc++] = const_cast<char*>(transceiverPath);
	argv[argc++] = TRXnumARFCN;
	argv[argc] = NULL;

	LOG(ALERT) << usernotice;
	gTransceiverPid = vfork();
	LOG_ASSERT(gTransceiverPid>=0);
	if (gTransceiverPid==0) {
		// Pid==0 means this is the process that starts the transceiver.
	    execvp(transceiverPath,argv);
		LOG(EMERG) << "cannot find " << transceiverPath;
		_exit(1);
	} else {
		int status;
		waitpid(gTransceiverPid, &status,0);
		LOG(EMERG) << "Transceiver quit with status " << status << ". Exiting.";
		exit(2);
	}
}




void createStats()
{
	// count of OpenBTS start events
	gReports.create("OpenBTS.Starts");

	// count of watchdog restarts
	gReports.create("OpenBTS.Exit.Error.Watchdog");
	// count of aborts due to problems with CLI socket
	gReports.create("OpenBTS.Exit.Error.CLISocket");
	// count of aborts due to loss of transceiver heartbeat
	gReports.create("OpenBTS.Exit.Error.TransceiverHeartbeat");
	// count of aborts due to underfined nono-optional configuration parameters
	gReports.create("OpenBTS.Exit.Error.ConfigurationParameterNotFound");

	// count of CLI commands sent to OpenBTS
	gReports.create("OpenBTS.CLI.Command");
	// count of CLI commands where responses could not be returned
	gReports.create("OpenBTS.CLI.Command.ResponseFailure");

	// count of SIP transactions that failed with 3xx responses from the remote end
	gReports.create("OpenBTS.SIP.Failed.Remote.3xx");
	// count of SIP transactions that failed with 4xx responses from the remote end
	gReports.create("OpenBTS.SIP.Failed.Remote.4xx");
	// count of SIP transactions that failed with 5xx responses from the remote end
	gReports.create("OpenBTS.SIP.Failed.Remote.5xx");
	// count of SIP transactions that failed with 6xx responses from the remote end
	gReports.create("OpenBTS.SIP.Failed.Remote.6xx");
	// count of SIP transactions that failed with unrecognized responses from the remote end
	gReports.create("OpenBTS.SIP.Failed.Remote.xxx");
	// count of SIP transactions that failed due to local-end errors
	gReports.create("OpenBTS.SIP.Failed.Local");
	// count of timeout events on SIP socket reads 
	gReports.create("OpenBTS.SIP.ReadTimeout");
	// count of SIP messages that were never properly acked
	gReports.create("OpenBTS.SIP.LostProxy");
	// count of SIP message not sent due to unresolvable host name
	gReports.create("OpenBTS.SIP.UnresolvedHostname");
	// count of INVITEs received in the SIP layer
	gReports.create("OpenBTS.SIP.INVITE.In");
	// count of INVITEs sent from the in SIP layer
	gReports.create("OpenBTS.SIP.INVITE.Out");
	// count of INVITE-OKs sent from the in SIP layer (connection established)
	gReports.create("OpenBTS.SIP.INVITE-OK.Out");
	// count of MESSAGEs received in the in SIP layer
	gReports.create("OpenBTS.SIP.MESSAGE.In");
	// count of MESSAGESs sent from the SIP layer
	gReports.create("OpenBTS.SIP.MESSAGE.Out");
	// count of REGISTERSs sent from the SIP layer
	gReports.create("OpenBTS.SIP.REGISTER.Out");
	// count of BYEs sent from the SIP layer
	gReports.create("OpenBTS.SIP.BYE.Out");
	// count of BYEs received in the SIP layer
	gReports.create("OpenBTS.SIP.BYE.In");
	// count of BYE-OKs sent from SIP layer (final disconnect handshake)
	gReports.create("OpenBTS.SIP.BYE-OK.Out");
	// count of BYE-OKs received in SIP layer (final disconnect handshake)
	gReports.create("OpenBTS.SIP.BYE-OK.In");

	// count of initiated LUR attempts
	gReports.create("OpenBTS.GSM.MM.LUR.Start");
	// count of LUR attempts where the server timed out
	gReports.create("OpenBTS.GSM.MM.LUR.Timeout");
	//gReports.create("OpenBTS.GSM.MM.LUR.Success");
	//gReports.create("OpenBTS.GSM.MM.LUR.NotFound");
	//gReports.create("OpenBTS.GSM.MM.LUR.Allowed");
	//gReports.create("OpenBTS.GSM.MM.LUR.Rejected");
	// count of all authentication attempts
	gReports.create("OpenBTS.GSM.MM.Authenticate.Request");
	// count of authentication attempts the succeeded
	gReports.create("OpenBTS.GSM.MM.Authenticate.Success");
	// count of authentication attempts that failed
	gReports.create("OpenBTS.GSM.MM.Authenticate.Failure");
	// count of the number of TMSIs assigned to users
	gReports.create("OpenBTS.GSM.MM.TMSI.Assigned");
	//gReports.create("OpenBTS.GSM.MM.TMSI.Unknown");
	// count of CM Service requests for MOC
	gReports.create("OpenBTS.GSM.MM.CMServiceRequest.MOC");
	// count of CM Service requests for MOSMS
	gReports.create("OpenBTS.GSM.MM.CMServiceRequest.MOSMS");
	// count of CM Service requests for services we don't support
	gReports.create("OpenBTS.GSM.MM.CMServiceRequest.Unhandled");

	// count of mobile-originated SMS submissions initiated
	gReports.create("OpenBTS.GSM.SMS.MOSMS.Start");
	// count of mobile-originated SMS submissions competed (got CP-ACK for RP-ACK)
	gReports.create("OpenBTS.GSM.SMS.MOSMS.Complete");
	// count of mobile-temrinated SMS deliveries initiated
	gReports.create("OpenBTS.GSM.SMS.MTSMS.Start");
	// count of mobile-temrinated SMS deliveries completed (got RP-ACK)
	gReports.create("OpenBTS.GSM.SMS.MTSMS.Complete");

	// count of mobile-originated setup messages
	gReports.create("OpenBTS.GSM.CC.MOC.Setup");
	// count of mobile-terminated setup messages
	gReports.create("OpenBTS.GSM.CC.MTC.Setup");
	// count of mobile-terminated release messages
	gReports.create("OpenBTS.GSM.CC.MTD.Release");
	// count of mobile-originated disconnect messages
	gReports.create("OpenBTS.GSM.CC.MOD.Disconnect");
	// total number of minutes of carried calls
	gReports.create("OpenBTS.GSM.CC.CallMinutes");
	// count of dropped calls
	gReports.create("OpenBTS.GSM.CC.DroppedCalls");

	// count of CS (non-GPRS) channel assignments
	gReports.create("OpenBTS.GSM.RR.ChannelAssignment");
	//gReports.create("OpenBTS.GSM.RR.ChannelRelease");
	// count of number of times the beacon was regenerated
	gReports.create("OpenBTS.GSM.RR.BeaconRegenerated");
	// count of successful channel assignments
	gReports.create("OpenBTS.GSM.RR.ChannelSiezed");
	//gReports.create("OpenBTS.GSM.RR.LinkFailure");
	//gReports.create("OpenBTS.GSM.RR.Paged.IMSI");
	//gReports.create("OpenBTS.GSM.RR.Paged.TMSI");
	//gReports.create("OpenBTS.GSM.RR.Handover.Inbound.Request");
	//gReports.create("OpenBTS.GSM.RR.Handover.Inbound.Accept");
	//gReports.create("OpenBTS.GSM.RR.Handover.Inbound.Success");
	//gReports.create("OpenBTS.GSM.RR.Handover.Outbound.Request");
	//gReports.create("OpenBTS.GSM.RR.Handover.Outbound.Accept");
	//gReports.create("OpenBTS.GSM.RR.Handover.Outbound.Success");
	// histogram of timing advance for accepted RACH bursts
	gReports.create("OpenBTS.GSM.RR.RACH.TA.Accepted",0,63);

	//gReports.create("Transceiver.StaleBurst");
	//gReports.create("Transceiver.Command.Received");
	//gReports.create("OpenBTS.TRX.Command.Sent");
	//gReports.create("OpenBTS.TRX.Command.Failed");
	//gReports.create("OpenBTS.TRX.FailedStart");
	//gReports.create("OpenBTS.TRX.LostLink");

	// GPRS
	// number of RACH bursts processed for GPRS
	gReports.create("GPRS.RACH");
	// number of TBFs assigned
	gReports.create("GPRS.TBF");
	// number of MSInfo records generated
	gReports.create("GPRS.MSInfo");

	// (pat) 1-2014 Added RTP thread performance reporting.
	gReports.create("OpenBTS.RTP.AverageSlack");	// Average head room.
	gReports.create("OpenBTS.RTP.MinSlack");	// Minimum slack.  Negative is a whoops.
}



// (pat) Using multiple radios on the same CPU:
// 1. Provide a seprate config OpenBTS.db file for each OpenBTS + transceiver pair.
//		I run each OpenBTS+transceiver pair in a separate directory with its own OpenBTS.db set as below.
//		To set the config file You can use the --config option or set the OpenBTSConfigFile environment variable,
//		which also works with gdb.
// 2. Set TRX.RadioNumber to 1,2,3,...
// 3. Set transceiver communication TRX.Port differently.  TRX uses >100 ports, so use: 5700, 5900, 6100, etc.
// 4. Set GSM.Radio.C0 differently.
// 5. Set GSM.Identity.BSIC.BCC differently.
// 6. Set GSM.Identity.LAC differently, maybe, but this depends on what you want to do.
// 7. Change all the external application ports: Peering.Port, RTP.Start, SIP.Local.Port
// 8. The neighbor tables need to point at each other.  See example below.
// 9. Change the Peering.NeighborTable.Path.  I just set it to a .db in the current directory.  Changing the other .db files is optional
// 10. If you have old radios, dont forget to set the TRX.RadioFrequencyOffset for each radio.
// 11. Doug recommends increasing GSM.Ny1 for handover testing.
// Note reserved ports: SR uses port 5064 and asterisk uses port 5060.
// Example using two radios on one computer:
// TRX.RadioNumber				1			2
// TRX.Port						5700		5900
// GSM.Radio.C0					51			60
// GSM.Identity.BSIC.BCC		2			3
// GSM.Identity.LAC				1007		1008
// SIP.Local.Port				5062		5066
// NodeManager.Commands.Port	45060		45062
// CLI.Port						49300		49302
// RTP.Start					16484		16600
// Peering.Port					16001		16002
// GSM.Neighbors		127.0.0.1:16002		127.0.0.1:16001
// Each BTS needs separate versions of these .db files that normally reside in /var/run:  Just put them in the cur dir like this:
// Peering.NeighborTable.Path NeighborTable.db
// Control.Reporting.TransactionTable TransactionTable.db
// Control.Reporting.TMSITable TMSITable.db
// Control.Reporting.StatsTable StatsTable.db
// Control.Reporting.PhysStatusTable PhysStatusTable.db

namespace GSM { extern void TestTCHL1FEC(); };	// (pat) This is cheating, but I dont want to include the whole GSML1FEC.h.

/** Application specific NodeManager logic for handling requests. */
JsonBox::Object nmHandler(JsonBox::Object& request)
{
	JsonBox::Object response;
	std::string command = request["command"].getString();

	if (command.compare("monitor") == 0) {
		response["code"] = JsonBox::Value(200);
		response["data"]["noiseRSSI"] = JsonBox::Value(0 - gTRX.ARFCN(0)->getNoiseLevel());
		response["data"]["msTargetRSSI"] = JsonBox::Value((signed)gConfig.getNum("GSM.Radio.RSSITarget"));
		// FIXME -- This needs to take GPRS channels into account. See #762. (note from CLI::load section)
		response["data"]["gsmSDCCHActive"] = JsonBox::Value((int)gBTS.SDCCHActive());
		response["data"]["gsmSDCCHTotal"] = JsonBox::Value((int)gBTS.SDCCHTotal());
		response["data"]["gsmTCHActive"] = JsonBox::Value((int)gBTS.TCHActive());
		response["data"]["gsmTCHTotal"] = JsonBox::Value((int)gBTS.TCHTotal());
		response["data"]["gsmAGCHQueue"] = JsonBox::Value((int)gBTS.AGCHLoad());
		response["data"]["gsmPCHQueue"] = JsonBox::Value((int)gBTS.PCHLoad());
	} else if (command.compare("tmsis") == 0) {
		int verbosity = 2;
		bool rawFlag = true;
		unsigned maxRows = 10000;
		vector< vector<string> > view = gTMSITable.tmsiTabView(verbosity, rawFlag, maxRows);

		int count = 0;
		JsonBox::Array a;
		for (vector< vector<string> >::iterator it = view.begin(); it != view.end(); ++it) {
			// skip the header line
			// TODO : use the header line to grab appropriate fields and indexes
			if (count == 0) {
				count++;
				continue;
			}
			vector<string> &row = *it;
			JsonBox::Object o;
			o["IMSI"] = row.at(0);
			o["TMSI"] = row.at(1);
			o["IMEI"] = row.at(2);
			o["AUTH"] = row.at(3);
			o["CREATED"] = row.at(4);
			o["ACCESSED"] = row.at(5);
			o["TMSI_ASSIGNED"] = row.at(6);
			o["PTMSI_ASSIGNED"] = row.at(7);
			o["AUTH_EXPIRY"] = row.at(8);
			o["REJECT_CODE"] = row.at(9);
			o["ASSOCIATED_URI"] = row.at(10);
			o["ASSERTED_IDENTITY"] = row.at(11);
			o["WELCOME_SENT"] = row.at(12);
			o["A5_SUPPORT"] = row.at(13);
			o["POWER_CLASS"] = row.at(14);
			o["RRLP_STATUS"] = row.at(15);
			o["OLD_TMSI"] = row.at(16);
			o["OLD_MCC"] = row.at(17);
			o["OLD_MNC"] = row.at(18);
			o["OLD_LAC"] = row.at(19);
			a.push_back(JsonBox::Value(o));
		}
		response["code"] = JsonBox::Value(200);
		response["data"] = JsonBox::Value(a);
	} else {
            response["code"] = JsonBox::Value(501);
	}

	return response;
}

static bool bAllowMultipleInstances = false;

void processArgs(int argc, char *argv[])
{
	// TODO: Properly parse and handle any arguments
	if (argc > 1) {
		bool testflag = false;
		for (int argi = 1; argi < argc; argi++) {		// Skip argv[0] which is the program name.
			if (!strcmp(argv[argi], "--version") || !strcmp(argv[argi], "-v")) {
				// Print the version number and exit immediately.
				cout << gVersionString << endl;
				exit(0);
			}
			if (!strcmp(argv[argi], "--test")) {
				testflag = true;
				continue;
			}
			if (!strcmp(argv[argi], "--gensql")) {
				cout << gConfig.getDefaultSQL(string(argv[0]), gVersionString) << endl;
				exit(0);
			}
			if (!strcmp(argv[argi], "--gentex")) {
				cout << gConfig.getTeX(string(argv[0]), gVersionString) << endl;
				exit(0);
			}

			// Allow multiple occurrences of the program to run.
			if (!strcmp(argv[argi], "-m")) {
				bAllowMultipleInstances = true;
				continue;
			}

			// (pat) Adding support for specified sql config file.
			// Unfortunately, the Config table was inited quite some time ago,
			// so stick this arg in the environment, whence the ConfigurationTable can find it, and then reboot.
			if (!strcmp(argv[argi],"--config")) {
				if (++argi == argc) {
					LOG(ALERT) <<"Missing argument to --config option";
					exit(2);
				}
				setenv(cOpenBTSConfigEnv,argv[argi],1);
				execl(argv[0],"OpenBTS",NULL);
				LOG(ALERT) <<"execl failed?  Exiting...";
				exit(0);
			}
			if (!strcmp(argv[argi],"--help")) {
				printf("OpenBTS [--version --gensql --gentex] [--config file.db]\n");
				printf("OpenBTS exiting...\n");
				exit(0);
			}

			printf("OpenBTS: unrecognized argument: %s\nexiting...\n",argv[argi]);
		}

		if (testflag) { GSM::TestTCHL1FEC(); exit(0); }
	}
}

std::deque<TimeSlot> timeSlotList;

static int initTimeSlots()	// Return how many slots used by beacon.
{
	// The first timeslot is special for the beacon:
	unsigned beaconSlots = 1;

	int numARFCNs = gConfig.getNum("GSM.Radio.ARFCNs");
	int scount = 0;
	for (int cn = 0; cn < numARFCNs; cn++) {
		for (int tn = 0; tn < 8; tn++) {
			if (cn == 0 && (beaconSlots & (1<<tn))) {
				// This cn,tn is used by the beacon.
				scount++;
				continue;
			}
			timeSlotList.push_back(TimeSlot(cn,tn));
		}
	}
	return scount;
}

// (pat 3-2014) A collection of routines to retrieve or validate the timeslot configuration.
// This no longer writes the config variables; they are recalculated at each OpenBTS startup.
// I expect customers to mostly use the 'auto' setting now.
struct TimeSlots {

	// Return the max possible C1 + C7 slots.  Now that we support multiple beacon types it depends on CCCH-CONF.
	static int maxC1plusC7() {
		unsigned numARFCNs = gConfig.getNum("GSM.Radio.ARFCNs");
		return 8*numARFCNs - countBeaconTimeslots(gConfig.getNum("GSM.CCCH.CCCH-CONF"));
	}

	// Default number of C7s is determined by CCCH-CONF and number of ARFCNs.
	static int defaultC7s() {
		// If CCCH-CONF is not 1 we need at least 1 x C7.  Well, not really, if VEA is set and user is willing to forego SMS.
		int minC7s = (gConfig.getNum("GSM.CCCH.CCCH-CONF") == 1) ? 0 : 1;
		return max(minC7s,(int)gConfig.getNum("GSM.Radio.ARFCNs")-1);
	}

	static int getNumC7s() {
		if (!gConfig.defines("GSM.Channels.NumC7s")) {
			LOG(CRIT) << "GSM.Channels.NumC7s not defined. Defaulting to " << defaultC7s() << ".";
			return defaultC7s();
		}
		if (gConfig.getStr("GSM.Channels.NumC7s") == "auto") {
			return defaultC7s(); // Same thing as above, but silently.
		}
		return gConfig.getNum("GSM.Channels.NumC7s");
	}

	// Default number of C1s is all timeslots except beacon and C7s.
	static int defaultC1s() {
		return maxC1plusC7() - getNumC7s();
	}

	// (pat) If config NumC1s or NumC7s is undefined or "auto", we will set ok values, but the caller still
	// must check that getNumC1s() + getNumC7s() <= maxC1plusC7() to catch the case where the user set specific and invalid values.
	static int getNumC1s() {
		if (!gConfig.defines("GSM.Channels.NumC1s")) {
			LOG(CRIT) << "GSM.Channels.NumC1s not defined. Defaulting to " << defaultC1s() << ".";
			return defaultC1s();
		}
		if (gConfig.getStr("GSM.Channels.NumC1s") == "auto") {
			return defaultC1s();		// Same thing as above, but silently.
		}
		return gConfig.getNum("GSM.Channels.NumC1s");
	}
};


int main(int argc, char *argv[])
{
	//mtrace();       // (pat) Enable memory leak detection.  Unfortunately, huge amounts of code have been started in the constructors above.
	gLogGroup.setAll();
	processArgs(argc, argv);

	// register ourself to prevent two instances (and check that no other
	// one is running).  Note that this MUST be done after the logger gets
	// initialized.
	if (!bAllowMultipleInstances) gSelf.RegisterProgram(argv[0]);

	createStats();

	gConfig.setCrossCheckHook(&configurationCrossCheck);

	gReports.incr("OpenBTS.Starts");

	gNeighborTable.NeighborTableInit(
		gConfig.getStr("Peering.NeighborTable.Path").c_str());


	try {

	srandom(time(NULL));

	gConfig.setUpdateHook(purgeConfig);
	LOG(ALERT) << "OpenBTS (re)starting, ver " << VERSION << " build date/time " << TIMESTAMP_ISO;
	LOG(ALERT) << "OpenBTS reading config file "<<cOpenBTSConfigFile;

	COUT("\n\n" << gOpenBTSWelcome << "\n");
	Control::controlInit();		// init Layer3: TMSITable, TransactionTable.
	gPhysStatus.open(gConfig.getStr("Control.Reporting.PhysStatusTable").c_str());
	gBTS.gsmInit();
	gParser.addCommands();

	COUT("\nStarting the system...");

	// (pat 3-16-2014) If there are multiple instances of OpenBTS running, dont go talking to some random transceiver.
	// (pat) We dont - we talk to the transceiver on the specified port.
	bool haveTRX = false;
	//if (! bAllowMultipleInstances) {
		// is the radio running?
		// Start the transceiver interface.
		LOG(INFO) << "checking transceiver";
		//gTRX.ARFCN(0)->powerOn();
		//sleep(gConfig.getNum("TRX.Timeout.Start"));
		//bool haveTRX = gTRX.ARFCN(0)->powerOn(false);		This prints an inapplicable warning message.
		haveTRX = gTRX.ARFCN(0)->trxRunning();			// This does not print an inapplicable warning message.
	//}

	Thread transceiverThread;
	if (!haveTRX) {
		//LOG(ALERT) << "starting the transceiver";
		transceiverThread.start((void*(*)(void*)) startTransceiver, NULL);
		// sleep to let the FPGA code load
		// TODO: we should be "pinging" the radio instead of sleeping
		sleep(5);
	} else {
		LOG(NOTICE) << "transceiver already running";
	}

	// Start the SIP interface.
	SIP::SIPInterfaceStart();

	// Start the peer interface
	gPeerInterface.start();

	// Sync factory calibration as defaults from radio EEPROM
	signed sdrsn = gTRX.ARFCN(0)->getFactoryCalibration("sdrsn");
	if (sdrsn != 0 && sdrsn != 65535) {
		signed val;

		val = gTRX.ARFCN(0)->getFactoryCalibration("band");
		if (gConfig.isValidValue("GSM.Radio.Band", val)) {
			gConfig.mSchema["GSM.Radio.Band"].updateDefaultValue(val);
		}

		val = gTRX.ARFCN(0)->getFactoryCalibration("freq");
		if (gConfig.isValidValue("TRX.RadioFrequencyOffset", val)) {
			gConfig.mSchema["TRX.RadioFrequencyOffset"].updateDefaultValue(val);
		}

		val = gTRX.ARFCN(0)->getFactoryCalibration("rxgain");
		if (gConfig.isValidValue("GSM.Radio.RxGain", val)) {
			gConfig.mSchema["GSM.Radio.RxGain"].updateDefaultValue(val);
		}

		val = gTRX.ARFCN(0)->getFactoryCalibration("txgain");
		if (gConfig.isValidValue("TRX.TxAttenOffset", val)) {
			gConfig.mSchema["TRX.TxAttenOffset"].updateDefaultValue(val);
		}
	}

	// Limit valid ARFCNs to current band
	gConfig.mSchema["GSM.Radio.C0"].updateValidValues(getARFCNsString(gConfig.getNum("GSM.Radio.Band")));

	//
	// Configure the radio.
	//

	gTRX.start();

	// Set up the interface to the radio.
	// Get a handle to the C0 transceiver interface.
	ARFCNManager* C0radio = gTRX.ARFCN(0);

	// Tuning.
	// Make sure its off for tuning.
	//C0radio->powerOff();
	// Get the ARFCN list.
	unsigned C0 = gConfig.getNum("GSM.Radio.C0");
	unsigned numARFCNs = gConfig.getNum("GSM.Radio.ARFCNs");
	for (unsigned i=0; i<numARFCNs; i++) {
		// Tune the radios.
		unsigned ARFCN = C0 + i*2;
		LOG(INFO) << "tuning TRX " << i << " to ARFCN " << ARFCN;
		ARFCNManager* radio = gTRX.ARFCN(i);
		radio->tune(ARFCN);
	}

	// Send either TSC or full BSIC depending on radio need
	if (gConfig.getBool("GSM.Radio.NeedBSIC")) {
		// Send BSIC to 
		C0radio->setBSIC(gBTS.BSIC());
	} else {
		// Set TSC same as BCC everywhere.
		C0radio->setTSC(gBTS.BCC());
	}

	// Set maximum expected delay spread.
	C0radio->setMaxDelay(gConfig.getNum("GSM.Radio.MaxExpectedDelaySpread"));

	// Set Receiver Gain
	C0radio->setRxGain(gConfig.getNum("GSM.Radio.RxGain"));

	// Turn on and power up.
	C0radio->powerOn(true);
	// (pat 3-2014) This previously started OpenBTS at maximum power (which is MinAtten)
	// We want to bring the radio up at lowest power and ramp up.
	C0radio->setPower(gConfig.getNum("GSM.Radio.PowerManager.MaxAttenDB")); // Previously: "GSM.Radio.PowerManager.MinAttenDB"


	// (pat) GSM 5.02 6.4 describes the permitted channel combinations.
	// I am leaving out the SACCH in these descriptions; all TCH or SDCCH include the same number of SACCH.
	// Combination-V is BCCH beacon + 3 AGCH + 4 SDCCH.  See GSM 5.02 figure 7 (page 59)
	//		Note: A complete C-V mapping requires two consecutive 51-multiframes because of the way SACCH are interleaved.
	// Combination-IV is BCCH beacon + 9 AGCH.
	//		BS_CC_CHANS specifies how many timeslots support CCCH, from 1 to 4.
	//		BS_AG_BLKS_RES specifies the number of beacon AGCH NOT used by paging.
	// Combination-VII is 8 SDCCH.
	// Combination-I is a TCH/F+FACCH+SACCH (ie, traffic channel.)

	gBTS.createBeacon(C0radio);


	//
	// Configure the other slots.
	//

	// sanity check on channel counts
	// the clamp here could be improved to take the customer's current ratio of C1:C7 and scale it back to fit in the window
	if (TimeSlots::maxC1plusC7() < TimeSlots::getNumC1s() + TimeSlots::getNumC7s()) {
		LOG(CRIT) << "scaling back GSM.Channels.NumC1s and GSM.Channels.NumC7s to fit inside number of available timeslots.";
		// NOTE!!! Must set NumC7s before calling defaultC1s.
		//gConfig.set("GSM.Channels.NumC7s",TimeSlots::defaultC7s());
		//gConfig.set("GSM.Channels.NumC1s",TimeSlots::defaultC1s());
		// Update: Just set them both to auto permanently.
		gConfig.set("GSM.Channels.NumC7s","auto");
		gConfig.set("GSM.Channels.NumC1s","auto");
	}

	// Count configured slots.
	int sCount = initTimeSlots();	// Returns number of timeslots used by beacon.

	gNumC7s = TimeSlots::getNumC7s();
	gNumC1s = TimeSlots::getNumC1s();
	LOG(NOTICE) << format("Creating %d Combination-1 (TCH/F) timeslots and %d Combination-7 (SDCCH) timeslots",gNumC1s,gNumC7s);

	if (gConfig.getBool("GSM.Channels.C1sFirst")) {
		// Create C-I slots.
		for (int i=0; timeSlotList.size() && i<gNumC1s; i++) {
			TimeSlot ts = timeSlotList.front();
			timeSlotList.pop_front();
			gBTS.createCombinationI(gTRX,ts.mCN,ts.mTN);
			sCount++;
		}
	}

	// Create C-VII slots.
	for (int i=0; timeSlotList.size() && i<gNumC7s; i++) {
		TimeSlot ts = timeSlotList.front();
		timeSlotList.pop_front();
		gBTS.createCombinationVII(gTRX,ts.mCN,ts.mTN);
		sCount++;
	}

	if (!gConfig.getBool("GSM.Channels.C1sFirst")) {
		// Create C-I slots.
		for (int i=0; timeSlotList.size() && i<gNumC1s; i++) {
			TimeSlot ts = timeSlotList.front();
			timeSlotList.pop_front();
			gBTS.createCombinationI(gTRX,ts.mCN,ts.mTN);
			sCount++;
		}
	}

	if (sCount<(numARFCNs*8)) {
		LOG(CRIT) << "Only " << sCount << " timeslots configured in an " << numARFCNs << "-ARFCN system.";
	}

	// Set up idle filling on C0 as needed for unconfigured slots..
	while (timeSlotList.size() && timeSlotList.front().mCN == 0) {
		timeSlotList.pop_front();
		gBTS.createCombination0(gTRX,sCount);
		sCount++;
	}

	// Be sure we are not over-reserving.
	if (0 == gBTS.SDCCHTotal()) {
		LOG(CRIT) << "No SDCCH channels are allocated!  OpenBTS may not function properly.";
	} else if (gConfig.getNum("GSM.Channels.SDCCHReserve")>=(int)gBTS.SDCCHTotal()) {
		int val = gBTS.SDCCHTotal() - 1;
		if (val < 0) { val = 0; }
		LOG(CRIT) << "GSM.Channels.SDCCHReserve too big, changing to " << val;
		gConfig.set("GSM.Channels.SDCCHReserve",val);
	}


	// OK, now it is safe to start the BTS.
	gBTS.gsmStart();


	LOG(INFO) << "system ready";

	gNodeManager.setAppLogicHandler(&nmHandler);
	gNodeManager.start(gConfig.getNum("NodeManager.Commands.Port"), gConfig.getNum("NodeManager.Events.Port"));

	COUT("\nsystem ready\n");
	COUT("\nuse the OpenBTSCLI utility to access CLI\n");
	gParser.cliServer();	// (pat) This does not return unless the user directs us to kill OpenBTS.

	LOG(ALERT) << "exiting OpenBTS ...";
	// End CLI Interface code

	} // try

	catch (ConfigurationTableKeyNotFound e) {
		LOG(EMERG) << "required configuration parameter " << e.key() << " not defined, aborting";
		gReports.incr("OpenBTS.Exit.Error.ConfigurationParamterNotFound");
	}
	catch (exception e) {
		// (pat) This is C++ standard exception.  It will be thrown for string or STL [Standard Template Library] errors.
		// They are also thrown from the zmq library used by the NodeManager, but the numnuts dont put any useful information in the e.what(0 field.
		// man zmq_cpp for more info.
		LOG(EMERG) << "C++ standard exception occurred: "<<e.what();
	}
	catch (...) {
		LOG(EMERG) << "Unrecognized C++  exception occurred, exiting...";
		//printf("OpenBTS: exception occurred, exiting...\n"); fflush(stdout);
	}


	//if (gTransceiverPid) kill(gTransceiverPid, SIGKILL);

	exit(0);

}

/** Return warning strings about a potential conflicting value */
vector<string> configurationCrossCheck(const string& key) {
	vector<string> warnings;
	ostringstream warning;

	// GSM.Timer.T3113 should equal SIP.Timer.B
	if (key.compare("GSM.Timer.T3113") == 0 || key.compare("SIP.Timer.B") == 0) {
		string gsm = gConfig.getStr("GSM.Timer.T3113");
		string sip = gConfig.getStr("SIP.Timer.B");
		if (gsm.compare(sip) != 0) {
			warning << "GSM.Timer.T3113 (" << gsm << ") and SIP.Timer.B (" << sip << ") should usually have the same value";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}

	// Control.VEA depends on GSM.CellSelection.NECI
	} else if (key.compare("Control.VEA") == 0 || key.compare("GSM.CellSelection.NECI") == 0) {
		if (gConfig.getBool("Control.VEA") && gConfig.getStr("GSM.CellSelection.NECI").compare("1") != 0) {
			warning << "Control.VEA is enabled but will not be functional until GSM.CellSelection.NECI is set to \"1\"";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}

	// GSM.Timer.T3212 should be a factor of six and shorter than SIP.RegistrationPeriod
	} else if (key.compare("GSM.Timer.T3212") == 0 || key.compare("SIP.RegistrationPeriod") == 0) {
		int gsm = gConfig.getNum("GSM.Timer.T3212");
		int sip = gConfig.getNum("SIP.RegistrationPeriod");
		if (key.compare("GSM.Timer.T3212") == 0 && gsm % 6) {
			warning << "GSM.Timer.T3212 should be a factor of 6";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}
		if (gsm >= sip) {
			warning << "GSM.Timer.T3212 (" << gsm << ") should be shorter than SIP.RegistrationPeriod (" << sip << ")";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}

	// GPRS.ChannelCodingControl.RSSI should normally be 10db more than GSM.Radio.RSSITarget
	} else if (key.compare("GPRS.ChannelCodingControl.RSSI") == 0 || key.compare("GSM.Radio.RSSITarget") == 0) {
		int gprs = gConfig.getNum("GPRS.ChannelCodingControl.RSSI");
		int gsm = gConfig.getNum("GSM.Radio.RSSITarget");
		if ((gprs - gsm) != 10) {
			warning << "GPRS.ChannelCodingControl.RSSI (" << gprs << ") should normally be 10db greater than GSM.Radio.RSSITarget (" << gsm << ")";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}

	// TODO : This NEEDS to be an error not a warning. OpenBTS will fail to start because of an assert if an invalid value is used.
	// GSM.Radio.C0 needs to be inside the valid range of ARFCNs for GSM.Radio.Band
	} else if (key.compare("GSM.Radio.C0") == 0 || key.compare("GSM.Radio.Band") == 0) {
		int c0 = gConfig.getNum("GSM.Radio.C0");
		string band = gConfig.getStr("GSM.Radio.Band");
		string range;
		if (band.compare("850") == 0 && (c0 < 128 || 251 < c0)) {
			range = "128-251";
		} else if (band.compare("900") == 0 && (c0 < 1 || 124 < c0)) {
			range = "1-124";
		} else if (band.compare("1800") == 0 && (c0 < 512 || 885 < c0)) {
			range = "512-885";
		} else if (band.compare("1900") == 0 && (c0 < 512 || 810 < c0)) {
			range = "512-810";
		}
		if (range.length()) {
			warning << "GSM.Radio.C0 (" << c0 << ") falls outside the valid range of ARFCNs " << range << " for GSM.Radio.Band (" << band << ")";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}

	// SGSN.Timer.ImplicitDetach should be at least 240 seconds greater than SGSN.Timer.RAUpdate"
	} else if (key.compare("SGSN.Timer.ImplicitDetach") == 0 || key.compare("SGSN.Timer.RAUpdate") == 0) {
		int detach = gConfig.getNum("SGSN.Timer.ImplicitDetach");
		int update = gConfig.getNum("SGSN.Timer.RAUpdate");
		if ((detach - update) < 240) {
			warning << "SGSN.Timer.ImplicitDetach (" << detach << ") should be at least 240 seconds greater than SGSN.Timer.RAUpdate (" << update << ")";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}

	// Control.LUR.FailedRegistration.Message depends on Control.LUR.FailedRegistration.ShortCode
	} else if (key.compare("Control.LUR.FailedRegistration.Message") == 0 || key.compare("Control.LUR.FailedRegistration.ShortCode") == 0) {
		if (gConfig.getStr("Control.LUR.FailedRegistration.Message").length() && !gConfig.getStr("Control.LUR.FailedRegistration.ShortCode").length()) {
			warning << "Control.LUR.FailedRegistration.Message is enabled but will not be functional until Control.LUR.FailedRegistration.ShortCode is set";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}

	// Control.LUR.NormalRegistration.Message depends on Control.LUR.NormalRegistration.ShortCode
	} else if (key.compare("Control.LUR.NormalRegistration.Message") == 0 || key.compare("Control.LUR.NormalRegistration.ShortCode") == 0) {
		if (gConfig.getStr("Control.LUR.NormalRegistration.Message").length() && !gConfig.getStr("Control.LUR.NormalRegistration.ShortCode").length()) {
			warning << "Control.LUR.NormalRegistration.Message is enabled but will not be functional until Control.LUR.NormalRegistration.ShortCode is set";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}

	// Control.LUR.OpenRegistration depends on Control.LUR.OpenRegistration.ShortCode
	} else if (key.compare("Control.LUR.OpenRegistration") == 0 || key.compare("Control.LUR.OpenRegistration.ShortCode") == 0) {
		if (gConfig.getStr("Control.LUR.OpenRegistration").length() && !gConfig.getStr("Control.LUR.OpenRegistration.ShortCode").length()) {
			warning << "Control.LUR.OpenRegistration is enabled but will not be functional until Control.LUR.OpenRegistration.ShortCode is set";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}

	// TODO : SIP.SMSC is actually broken with the verification bits, no way to set value as null
	// SIP.SMSC should normally be NULL if SMS.MIMIEType is "text/plain" and "smsc" if SMS.MIMEType is "application/vnd.3gpp".
	} else if (key.compare("SMS.MIMEType") == 0 || key.compare("SIP.SMSC") == 0) {
		string sms = gConfig.getStr("SMS.MIMEType");
		string sip = gConfig.getStr("SIP.SMSC");
		if (sms.compare("application/vnd.3gpp.sms") == 0 && sip.compare("smsc") != 0) {
			warning << "SMS.MIMEType is set to \"application/vnc.3gpp.sms\", SIP.SMSC should usually be set to \"smsc\"";
			warnings.push_back(warning.str());
			warning.str(std::string());
		} else if (sms.compare("text/plain") == 0 && sip.compare("") != 0) {
			warning << "SMS.MIMEType is set to \"text/plain\", SIP.SMSC should usually be empty (use unconfig to clear)";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}


	// SIP.Local.IP cannot be 127.0.0.1 when any of the SIP.Proxy.* settings are non-localhost
	} else if (key.compare("SIP.Local.IP") == 0 ||
				key.compare("SIP.Proxy.Registration") == 0 || key.compare("SIP.Proxy.SMS") == 0 ||
				key.compare("SIP.Proxy.Speech") == 0 || key.compare("SIP.Proxy.USSD") == 0) {
		string loopback = "127.0.0.1";
		string local = gConfig.getStr("SIP.Local.IP");
		if (local.compare(loopback) == 0) {
			string registration = gConfig.getStr("SIP.Proxy.Registration");
			string sms = gConfig.getStr("SIP.Proxy.SMS");
			string speech = gConfig.getStr("SIP.Proxy.Speech");
			string ussd = gConfig.getStr("SIP.Proxy.USSD");
			if (registration.find(loopback) == std::string::npos ||
				sms.find(loopback) == std::string::npos || speech.find(loopback) == std::string::npos ||
				(ussd.length() && ussd.find(loopback) == std::string::npos)) {
				warning << "A non-local IP is being used for one or more SIP.Proxy.* settings but SIP.Local.IP is still set to 127.0.0.1. ";
				warning << "Set SIP.Local.IP to the IP address of this machine as seen by the proxies.";
				warnings.push_back(warning.str());
				warning.str(std::string());
			}
		}

	// GSM.MS.Power.Min cannot be higher than GSM.MS.Power.Max
	} else if (key.compare("GSM.MS.Power.Min") == 0 || key.compare("GSM.MS.Power.Max") == 0) {
		if (gConfig.getNum("GSM.MS.Power.Min") > gConfig.getNum("GSM.MS.Power.Max")) {
			warning << "GSM.MS.Power.Min is set higher than GSM.MS.Power.Max. Swap the values or set a new minimum.";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}

	// GSM.Channels.NumC1s + GSM.Channels.NumC1s must fall within allowed timeslots.
	} else if (key.compare("GSM.Radio.ARFCNs") == 0 || key.compare("GSM.Channels.NumC1s") == 0 || key.compare("GSM.Channels.NumC7s") == 0) {
		int max = TimeSlots::maxC1plusC7();
		int current = TimeSlots::getNumC1s() + TimeSlots::getNumC7s();
		if (max < current) {
			warning << "There are only " << max << " channels available but " << current << " are configured. ";
			warning << "Reduce GSM.Channels.NumC1s and/or GSM.Channels.NumC7s accordingly.";
			warnings.push_back(warning.str());
			warning.str(std::string());
		} else if (max > current) {
			int avail = max-current;
			if (avail == 1) {
				warning << "There is still " << avail << " channel available for additional capacity. ";
			} else {
				warning << "There are still " << avail << " channels available for additional capacity. ";
			}
			warning << "Increase GSM.Channels.NumC1s and/or GSM.Channels.NumC7s accordingly.";
			warnings.push_back(warning.str());
			warning.str(std::string());
		}
	}

	return warnings;
}

// vim: ts=4 sw=4
