/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012 Range Networks, Inc.
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

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#include <Configuration.h>
std::vector<std::string> configurationCrossCheck(const std::string& key);
static const char *cOpenBTSConfigEnv = "OpenBTSConfigFile";
// Load configuration from a file.
ConfigurationTable gConfig(getenv(cOpenBTSConfigEnv)?getenv(cOpenBTSConfigEnv):"/etc/OpenBTS/OpenBTS.db","OpenBTS", getConfigurationKeys());
#include <Logger.h>
Log dummy("openbts",gConfig.getStr("Log.Level").c_str(),LOG_LOCAL7);

// Set up the performance reporter.
#include <Reporting.h>
ReportingTable gReports(gConfig.getStr("Control.Reporting.StatsTable").c_str());

#include <TRXManager.h>
#include <GSML1FEC.h>
#include <GSMConfig.h>
#include <GSMSAPMux.h>
#include <GSML3RRMessages.h>
#include <GSMLogicalChannel.h>

#include <ControlCommon.h>
#include <TransactionTable.h>

#include <SIPInterface.h>
#include <Globals.h>

#include <CLI.h>
#include <PowerManager.h>
#include <Configuration.h>
#include <PhysicalStatus.h>
#include <SubscriberRegistry.h>
#include "NeighborTable.h"
#include <Peering.h>

#include <sys/wait.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

// (pat) mcheck.h is for mtrace, which permits memory leak detection.
// Set env MALLOC_TRACE=logfilename
// Call mtrace() in the program.
// post-process the logfilename with mtrace (a perl script.)
// #include <mcheck.h>

using namespace std;
using namespace GSM;

int gBtsXg = 0;		// Enable gprs

const char* gDateTime = __DATE__ " " __TIME__;


// All of the other globals that rely on the global configuration file need to
// be declared here.

// The TMSI Table.
Control::TMSITable gTMSITable;

// The transaction table.
Control::TransactionTable gTransactionTable;

// Physical status reporting
GSM::PhysicalStatus gPhysStatus;

// The global SIPInterface object.
SIP::SIPInterface gSIPInterface;

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

// Subscriber registry and http authentication
SubscriberRegistry gSubscriberRegistry;

/** The global peering interface. */
Peering::PeerInterface gPeerInterface;

/** The global neighbor table. */
Peering::NeighborTable gNeighborTable;


/** Define a function to call any time the configuration database changes. */
void purgeConfig(void*,int,char const*, char const*, sqlite3_int64)
{
	LOG(INFO) << "purging configuration cache";
	gConfig.purge();
	gBTS.regenerateBeacon();
	gResetWatchdog();
}



const char* transceiverPath = "./transceiver";

pid_t gTransceiverPid = 0;

void startTransceiver()
{
	//if local kill the process currently listening on this port
	char killCmd[32];
	if (gConfig.getStr("TRX.IP") == "127.0.0.1"){
		sprintf(killCmd,"fuser -k -n udp %d",(int)gConfig.getNum("TRX.Port"));
		if (system(killCmd)) {}
	}

	// Start the transceiver binary, if the path is defined.
	// If the path is not defined, the transceiver must be started by some other process.
	char TRXnumARFCN[4];
	sprintf(TRXnumARFCN,"%1d",(int)gConfig.getNum("GSM.Radio.ARFCNs"));
	std::string extra_args = gConfig.getStr("TRX.Args");
	LOG(NOTICE) << "starting transceiver " << transceiverPath << " w/ " << TRXnumARFCN << " ARFCNs and Args:" << extra_args;
	gTransceiverPid = vfork();
	LOG_ASSERT(gTransceiverPid>=0);
	if (gTransceiverPid==0) {
		// Pid==0 means this is the process that starts the transceiver.
	    execlp(transceiverPath,transceiverPath,TRXnumARFCN,extra_args.c_str(),(void*)NULL);
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
	// count of SOS INVITEs sent from the SIP layer; these are not included in ..INVITE.OUT
	gReports.create("OpenBTS.SIP.INVITE-SOS.Out");
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
}




int main(int argc, char *argv[])
{
	// mtrace();	// Enable memory leak detection.  Unfortunately, huge amounts of code have been started in the constructors above.
	// TODO: Properly parse and handle any arguments
	if (argc > 1) {
		for (int argi = 1; argi < argc; argi++) {		// Skip argv[0] which is the program name.
			if (!strcmp(argv[argi], "--version") ||
			    !strcmp(argv[argi], "-v")) {
				cout << gVersionString << endl;
				continue;
			}
			if (!strcmp(argv[argi], "--gensql")) {
				cout << gConfig.getDefaultSQL(string(argv[0]), gVersionString) << endl;
				continue;
			}
			if (!strcmp(argv[argi], "--gentex")) {
				cout << gConfig.getTeX(string(argv[0]), gVersionString) << endl;
				continue;
			}

			// (pat) Adding support for specified sql file.
			// Unfortunately, the Config table was inited quite some time ago,
			// so stick this arg in the environment, whence the ConfigurationTable can find it, and then reboot.
			if (!strcmp(argv[argi],"--config")) {
				if (++argi == argc) {
					LOG(ALERT) <<"Missing argument to -sql option";
					exit(2);
				}
				setenv(cOpenBTSConfigEnv,argv[argi],1);
				execl(argv[0],"OpenBTS",NULL);
				LOG(ALERT) <<"execl failed?  Exiting...";
				exit(0);
			}
			if (!strcmp(argv[argi],"--help")) {
				printf("OpenBTS [--version --gensql --genex] [--config file.db]\n");
				printf("OpenBTS exiting...\n");
				exit(0);
			}

			printf("OpenBTS: unrecognized argument: %s\nexiting...\n",argv[argi]);
		}

		return 0;
	}

	createStats();

	gConfig.setCrossCheckHook(&configurationCrossCheck);

	gReports.incr("OpenBTS.Starts");

	gNeighborTable.NeighborTableInit(
		gConfig.getStr("Peering.NeighborTable.Path").c_str());

	int sock = socket(AF_UNIX,SOCK_DGRAM,0);
	if (sock<0) {
		perror("creating CLI datagram socket");
		LOG(ALERT) << "cannot create socket for CLI";
		gReports.incr("OpenBTS.Exit.CLI.Socket");
		exit(1);
	}

	try {

	srandom(time(NULL));

	gConfig.setUpdateHook(purgeConfig);
	LOG(ALERT) << "OpenBTS (re)starting, ver " << VERSION << " build date " << __DATE__;

	COUT("\n\n" << gOpenBTSWelcome << "\n");
	gTMSITable.open(gConfig.getStr("Control.Reporting.TMSITable").c_str());
	gTransactionTable.init(gConfig.getStr("Control.Reporting.TransactionTable").c_str());
	gPhysStatus.open(gConfig.getStr("Control.Reporting.PhysStatusTable").c_str());
	gBTS.init();
	gSubscriberRegistry.init();
	gParser.addCommands();

	COUT("\nStarting the system...");

	// is the radio running?
	// Start the transceiver interface.
	LOG(INFO) << "checking transceiver";
	//gTRX.ARFCN(0)->powerOn();
	//sleep(gConfig.getNum("TRX.Timeout.Start"));
	//bool haveTRX = gTRX.ARFCN(0)->powerOn(false); // (pat) Dont power on the radio before initing it, particularly SETTSC below; radio can crash.
	bool haveTRX = gTRX.ARFCN(0)->powerOff();

	Thread transceiverThread;
	if (!haveTRX) {
		transceiverThread.start((void*(*)(void*)) startTransceiver, NULL);
		// sleep to let the FPGA code load
		// TODO: we should be "pinging" the radio instead of sleeping
		sleep(5);
	} else {
		LOG(NOTICE) << "transceiver already running";
	}

	// Start the SIP interface.
	gSIPInterface.start();

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
	C0radio->setPower(gConfig.getNum("GSM.Radio.PowerManager.MinAttenDB"));

	//
	// Create a C-V channel set on C0T0.
	//

	// C-V on C0T0
	C0radio->setSlot(0,5);
	// SCH
	SCHL1FEC SCH;
	SCH.downstream(C0radio);
	SCH.open();
	// FCCH
	FCCHL1FEC FCCH;
	FCCH.downstream(C0radio);
	FCCH.open();
	// BCCH
	BCCHL1FEC BCCH;
	BCCH.downstream(C0radio);
	BCCH.open();
	// RACH
	RACHL1FEC RACH(gRACHC5Mapping);
	RACH.downstream(C0radio);
	RACH.open();
	// CCCHs
	CCCHLogicalChannel CCCH0(gCCCH_0Mapping);
	CCCH0.downstream(C0radio);
	CCCH0.open();
	CCCHLogicalChannel CCCH1(gCCCH_1Mapping);
	CCCH1.downstream(C0radio);
	CCCH1.open();
	CCCHLogicalChannel CCCH2(gCCCH_2Mapping);
	CCCH2.downstream(C0radio);
	CCCH2.open();
	// use CCCHs as AGCHs
	gBTS.addAGCH(&CCCH0);
	gBTS.addAGCH(&CCCH1);
	gBTS.addAGCH(&CCCH2);

	// C-V C0T0 SDCCHs
	SDCCHLogicalChannel C0T0SDCCH[4] = {
		SDCCHLogicalChannel(0,0,gSDCCH_4_0),
		SDCCHLogicalChannel(0,0,gSDCCH_4_1),
		SDCCHLogicalChannel(0,0,gSDCCH_4_2),
		SDCCHLogicalChannel(0,0,gSDCCH_4_3),
	};
	Thread C0T0SDCCHControlThread[4];
	// Subchannel 2 used for CBCH if SMSCB enabled.
	bool SMSCB = (gConfig.getStr("Control.SMSCB.Table").length() != 0);
	CBCHLogicalChannel CBCH(gSDCCH_4_2);
	Thread CBCHControlThread;
	for (int i=0; i<4; i++) {
		if (SMSCB && (i==2)) continue;
		C0T0SDCCH[i].downstream(C0radio);
		C0T0SDCCHControlThread[i].start((void*(*)(void*))Control::DCCHDispatcher,&C0T0SDCCH[i]);
		C0T0SDCCH[i].open();
		gBTS.addSDCCH(&C0T0SDCCH[i]);
	}
	// Install CBCH if used.
	if (SMSCB) {
		LOG(INFO) << "creating CBCH for SMSCB";
		CBCH.downstream(C0radio);
		CBCH.open();
		gBTS.addCBCH(&CBCH);
		CBCHControlThread.start((void*(*)(void*))Control::SMSCBSender,NULL);
	}


	//
	// Configure the other slots.
	//

	// Count configured slots.
	unsigned sCount = 1;


	if (!gConfig.defines("GSM.Channels.NumC1s")) {
		int numChan = numARFCNs*7;
		LOG(CRIT) << "GSM.Channels.NumC1s not defined. Defaulting to " << numChan << ".";
		gConfig.set("GSM.Channels.NumC1s",numChan);
	}
	if (!gConfig.defines("GSM.Channels.NumC7s")) {
		int numChan = numARFCNs-1;
		LOG(CRIT) << "GSM.Channels.NumC7s not defined. Defaulting to " << numChan << ".";
		gConfig.set("GSM.Channels.NumC7s",numChan);
	}

	if (gConfig.getBool("GSM.Channels.C1sFirst")) {
		// Create C-I slots.
		for (int i=0; i<gConfig.getNum("GSM.Channels.NumC1s"); i++) {
			gBTS.createCombinationI(gTRX,sCount/8,sCount%8);
			sCount++;
		}
	}

	// Create C-VII slots.
	for (int i=0; i<gConfig.getNum("GSM.Channels.NumC7s"); i++) {
		gBTS.createCombinationVII(gTRX,sCount/8,sCount%8);
		sCount++;
	}

	if (!gConfig.getBool("GSM.Channels.C1sFirst")) {
		// Create C-I slots.
		for (int i=0; i<gConfig.getNum("GSM.Channels.NumC1s"); i++) {
			gBTS.createCombinationI(gTRX,sCount/8,sCount%8);
			sCount++;
		}
	}

	if (sCount<(numARFCNs*8)) {
		LOG(CRIT) << "Only " << sCount << " timeslots configured in an " << numARFCNs << "-ARFCN system.";
	}

	// Set up idle filling on C0 as needed for unconfigured slots..
	while (sCount<8) {
		gBTS.createCombination0(gTRX,sCount);
		sCount++;
	}

	/* (pat) See GSM 05.02 6.5.2 and 3.3.2.3
		Note: The number of different paging subchannels on       
		the CCCH is:                                        
                                                           
		MAX(1,(3 - BS-AG-BLKS-RES)) * BS-PA-MFRMS           
			if CCCH-CONF = "001"                        
		(9 - BS-AG-BLKS-RES) * BS-PA-MFRMS                  
			for other values of CCCH-CONF               
	*/

	// Set up the pager.
	// Set up paging channels.
	// HACK -- For now, use a single paging channel, since paging groups are broken.
	gBTS.addPCH(&CCCH2);

	// Be sure we are not over-reserving.
	if (gConfig.getNum("GSM.Channels.SDCCHReserve")>=(int)gBTS.SDCCHTotal()) {
		unsigned val = gBTS.SDCCHTotal() - 1;
		LOG(CRIT) << "GSM.Channels.SDCCHReserve too big, changing to " << val;
		gConfig.set("GSM.Channels.SDCCHReserve",val);
	}


	// OK, now it is safe to start the BTS.
	gBTS.start();


	struct sockaddr_un cmdSockName;
	cmdSockName.sun_family = AF_UNIX;
	const char* sockpath = gConfig.getStr("CLI.SocketPath").c_str();
	char rmcmd[strlen(sockpath)+5];
	sprintf(rmcmd,"rm -f %s",sockpath);
	if (system(rmcmd)) {}
	strcpy(cmdSockName.sun_path,sockpath);
	LOG(INFO) "binding CLI datagram socket at " << sockpath;
	if (bind(sock, (struct sockaddr *) &cmdSockName, sizeof(struct sockaddr_un))) {
		perror("binding name to cmd datagram socket");
		LOG(ALERT) << "cannot bind socket for CLI at " << sockpath;
		gReports.incr("OpenBTS.Exit.CLI.Socket");
		exit(1);
	}

	COUT("\nsystem ready\n");
	COUT("\nuse the OpenBTSCLI utility to access CLI\n");
	LOG(INFO) << "system ready";

	while (1) {
		char cmdbuf[1000];
		struct sockaddr_un source;
		socklen_t sourceSize = sizeof(source);
		int nread = recvfrom(sock,cmdbuf,sizeof(cmdbuf)-1,0,(struct sockaddr*)&source,&sourceSize);
		gReports.incr("OpenBTS.CLI.Command");
		cmdbuf[nread]='\0';
		LOG(INFO) << "received command \"" << cmdbuf << "\" from " << source.sun_path;
		std::ostringstream sout;
		int res = gParser.process(cmdbuf,sout);
		const std::string rspString= sout.str();
		const char* rsp = rspString.c_str();
		LOG(INFO) << "sending " << strlen(rsp) << "-char result to " << source.sun_path;
		if (sendto(sock,rsp,strlen(rsp)+1,0,(struct sockaddr*)&source,sourceSize)<0) {
			LOG(ERR) << "can't send CLI response to " << source.sun_path;
			gReports.incr("OpenBTS.CLI.Command.ResponseFailure");
		}
		// res<0 means to exit the application
		if (res<0) break;
	}

	} // try

	catch (ConfigurationTableKeyNotFound e) {
		LOG(EMERG) << "required configuration parameter " << e.key() << " not defined, aborting";
		gReports.incr("OpenBTS.Exit.Error.ConfigurationParamterNotFound");
	}

	//if (gTransceiverPid) kill(gTransceiverPid, SIGKILL);
	close(sock);

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
			warning << "GPRS.ChannelCodingControl.RSSI (" << gprs << ") should normally be 10db higher than GSM.Radio.RSSITarget (" << gsm << ")";
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

	// Control.LUR.WhiteList depends on Control.WhiteListing.Message, Control.LUR.WhiteListing.RejectCause and Control.WhiteListing.ShortCode
	} else if (key.compare("Control.LUR.WhiteList") == 0 || key.compare("Control.WhiteListing.Message") == 0 ||
				key.compare("Control.LUR.WhiteListing.RejectCause") == 0 || key.compare("Control.WhiteListing.ShortCode") == 0) {
		if (gConfig.getBool("Control.LUR.WhiteList")) {
			if (!gConfig.getStr("Control.WhiteListing.Message").length()) {
				warning << "Control.LUR.WhiteList is enabled but will not be functional until Control.WhiteListing.Message is set";
				warnings.push_back(warning.str());
				warning.str(std::string());
			} else if (!gConfig.getStr("Control.LUR.WhiteListing.RejectCause").length()) {
				warning << "Control.LUR.WhiteList is enabled but will not be functional until Control.WhiteListing.RejectCause is set";
				warnings.push_back(warning.str());
				warning.str(std::string());
			} else if (!gConfig.getStr("Control.WhiteListing.ShortCode").length()) {
				warning << "Control.LUR.WhiteList is enabled but will not be functional until Control.WhiteListing.ShortCode is set";
				warnings.push_back(warning.str());
				warning.str(std::string());
			}
		}

	// GSM.CellSelection.NCCsPermitted needs to contain our own GSM.Identity.BSIC.NCC
	} else if (key.compare("GSM.CellSelection.NCCsPermitted") == 0 || key.compare("GSM.Identity.BSIC.NCC") == 0) {
		int ourNCCMask = gConfig.getNum("GSM.CellSelection.NCCsPermitted");
		int NCCMaskBit = 1 << gConfig.getNum("GSM.Identity.BSIC.NCC");
		if ((NCCMaskBit & ourNCCMask) == 0) {
			warning << "GSM.CellSelection.NCCsPermitted is not set to a mask which contains the local network color code defined in GSM.Identity.BSIC.NCC. ";
			warning << "Set GSM.CellSelection.NCCsPermitted to " << NCCMaskBit;
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
		}
	}

	return warnings;
}

// vim: ts=4 sw=4
