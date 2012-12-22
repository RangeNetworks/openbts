/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011,2012 Range Networks, Inc.
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

#include <iostream>
#include <fstream>

#include <Configuration.h>
// Load configuration from a file.
ConfigurationTable gConfig("/etc/OpenBTS/OpenBTS.db");

// Set up the performance reporter.
#include <Reporting.h>
ReportingTable gReports(gConfig.getStr("Control.Reporting.StatsTable","/var/log/OpenBTSStats.db").c_str());

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

#include <Logger.h>
#include <CLI.h>
#include <PowerManager.h>
#include <Configuration.h>
#include <PhysicalStatus.h>
#include <SubscriberRegistry.h>

#include <sys/wait.h>

#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

using namespace std;
using namespace GSM;


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

// Our interface to the software-defined radio.
TransceiverManager gTRX(gConfig.getNum("GSM.Radio.ARFCNs"), gConfig.getStr("TRX.IP").c_str(), gConfig.getNum("TRX.Port"));

// Subscriber registry
SubscriberRegistry gSubscriberRegistry;


/** Define a function to call any time the configuration database changes. */
void purgeConfig(void*,int,char const*, char const*, sqlite3_int64)
{
	LOG(INFO) << "purging configuration cache";
	gConfig.purge();
	gBTS.regenerateBeacon();
}



const char* transceiverPath = "./transceiver";

pid_t gTransceiverPid = 0;

void startTransceiver()
{
	// kill any stray transceiver process
	system("killall transceiver");

	// Start the transceiver binary, if the path is defined.
	// If the path is not defined, the transceiver must be started by some other process.
    char TRXnumARFCN[16];
    sprintf(TRXnumARFCN,"%1d", static_cast<int>(gConfig.getNum("GSM.Radio.ARFCNs")));
	LOG(NOTICE) << "starting transceiver " << transceiverPath << " " << TRXnumARFCN;
	gTransceiverPid = vfork();
	LOG_ASSERT(gTransceiverPid>=0);
	if (gTransceiverPid==0) {
		// Pid==0 means this is the process that starts the transceiver.
		execlp(transceiverPath,transceiverPath,TRXnumARFCN,NULL);
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

	// count of exit events driven from the CLI
	gReports.create("OpenBTS.Exit.Normal.CLI");
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
	// count of CM Service requests for emergency calls
	gReports.create("OpenBTS.GSM.MM.CMServiceRequest.SOS");
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
}




int main(int argc, char *argv[])
{
	// TODO: Properly parse and handle any arguments
	if (argc > 1) {
		for (int argi = 0; argi < argc; argi++) {
			if (!strcmp(argv[argi], "--version") ||
			    !strcmp(argv[argi], "-v")) {
				cout << gVersionString << endl;
			}
		}

		return 0;
	}

	createStats();
 
	gReports.incr("OpenBTS.Starts");

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
	gLogInit("openbts",gConfig.getStr("Log.Level").c_str(),LOG_LOCAL7);
	LOG(ALERT) << "OpenBTS starting, ver " << VERSION << " build date " << __DATE__;

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
	//sleep(gConfig.getNum("TRX.Timeout.Start",2));
	bool haveTRX = gTRX.ARFCN(0)->powerOn();

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
	C0radio->powerOn();
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
	for (int i=0; i<4; i++) {
		C0T0SDCCH[i].downstream(C0radio);
		C0T0SDCCHControlThread[i].start((void*(*)(void*))Control::DCCHDispatcher,&C0T0SDCCH[i]);
		C0T0SDCCH[i].open();
		gBTS.addSDCCH(&C0T0SDCCH[i]);
	}


	//
	// Configure the other slots.
	//

	// Count configured slots.
	unsigned sCount = 1;

	if (gConfig.defines("GSM.Channels.C1sFirst")) {
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

	if (!gConfig.defines("GSM.Channels.C1sFirst")) {
		// Create C-I slots.
		for (int i=0; i<gConfig.getNum("GSM.Channels.NumC1s"); i++) {
			gBTS.createCombinationI(gTRX,sCount/8,sCount%8);
			sCount++;
		}
	}


	// Set up idle filling on C0 as needed.
	while (sCount<8) {
		gBTS.createCombination0(gTRX,sCount);
		sCount++;
	}

	/*
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
	if (gConfig.getNum("GSM.Channels.SDCCHReserve",0)>=(int)gBTS.SDCCHTotal()) {
		unsigned val = gBTS.SDCCHTotal() - 1;
		LOG(CRIT) << "GSM.Channels.SDCCHReserve too big, changing to " << val;
		gConfig.set("GSM.Channels.SDCCHReserve",val);
	}


	// OK, now it is safe to start the BTS.
	gBTS.start();


	cout << "\nsystem ready\n";
	cout << "\nuse the OpenBTSCLI utility to access CLI\n";
	LOG(INFO) << "system ready";

	struct sockaddr_un cmdSockName;
	cmdSockName.sun_family = AF_UNIX;
	const char* sockpath = gConfig.getStr("CLI.SocketPath","/var/run/OpenBTS/command").c_str();
	char rmcmd[strlen(sockpath)+5];
	sprintf(rmcmd,"rm %s",sockpath);
	system(rmcmd);
	strcpy(cmdSockName.sun_path,sockpath);
	if (bind(sock, (struct sockaddr *) &cmdSockName, sizeof(struct sockaddr_un))) {
		perror("binding name to cmd datagram socket");
		LOG(ALERT) << "cannot bind socket for CLI at " << sockpath;
		gReports.incr("OpenBTS.Exit.CLI.Socket");
		exit(1);
	}

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
		gReports.incr("OpenBTS.Exit.Normal.CLI");
	}

	} // try

	catch (ConfigurationTableKeyNotFound e) {
		LOG(EMERG) << "required configuration parameter " << e.key() << " not defined, aborting";
		gReports.incr("OpenBTS.Exit.Error.ConfigurationParamterNotFound");
	}

	//if (gTransceiverPid) kill(gTransceiverPid, SIGKILL);
	close(sock);

}

// vim: ts=4 sw=4
