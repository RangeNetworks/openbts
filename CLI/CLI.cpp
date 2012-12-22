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

#include <iostream>
#include <iomanip>
#include <fstream>
#include <iterator>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <config.h>

// #include <Config.h>
#include <Logger.h>

#include <GSMConfig.h>
#include <GSMLogicalChannel.h>
#include <ControlCommon.h>
#include <TransactionTable.h>
#include <TRXManager.h>
#include <PowerManager.h>
#include <TMSITable.h>
#include <RadioResource.h>
#include <CallControl.h>

#include <Globals.h>

#include "CLI.h"

#undef WARNING

using namespace std;
using namespace CommandLine;

#define SUCCESS 0
#define BAD_NUM_ARGS 1
#define BAD_VALUE 2
#define NOT_FOUND 3
#define TOO_MANY_ARGS 4
#define FAILURE 5

extern TransceiverManager gTRX;

/** Standard responses in the CLI, much mach erorrCode enum. */
static const char* standardResponses[] = {
	"success", // 0
	"wrong number of arguments", // 1
	"bad argument(s)", // 2
	"command not found", // 3
	"too many arguments for parser", // 4
	"command failed", // 5
};




int Parser::execute(char* line, ostream& os) const
{
	// escape to the shell?
	if (line[0]=='!') {
		os << endl;
		int retVal = system(line+1);
		os << endl << "External call returned " << retVal << endl;
		return SUCCESS;
	}
	// tokenize
	char *argv[mMaxArgs];
	int argc = 0;
	char **ap;
	// This is (almost) straight from the man page for strsep.
	for (ap=argv; (*ap=strsep(&line," ")) != NULL; ) {
		if (**ap != '\0') {
			if (++ap >= &argv[mMaxArgs]) break;
			else argc++;
		}
	}
	// Blank line?
	if (!argc) return SUCCESS;
	// Find the command.
	ParseTable::const_iterator cfp = mParseTable.find(argv[0]);
	if (cfp == mParseTable.end()) {
		return NOT_FOUND;
	}
	int (*func)(int,char**,ostream&);
	func = cfp->second;
	// Do it.
	int retVal = (*func)(argc,argv,os);
	// Give hint on bad # args.
	if (retVal==BAD_NUM_ARGS) os << help(argv[0]) << endl;
	return retVal;
}


int Parser::process(const char* line, ostream& os) const
{
	char *newLine = strdup(line);
	int retVal = execute(newLine,os);
	free(newLine);
	if (retVal>0) os << standardResponses[retVal] << endl;
	return retVal;
}


const char * Parser::help(const string& cmd) const
{
	HelpTable::const_iterator hp = mHelpTable.find(cmd);
	if (hp==mHelpTable.end()) return "no help available";
	return hp->second.c_str();
}



/**@name Commands for the CLI. */
//@{

// forward refs
int printStats(int argc, char** argv, ostream& os);

/*
	A CLI command takes the argument in an array.
	It returns 0 on success.
*/

/** Display system uptime and current GSM frame number. */
int uptime(int argc, char** argv, ostream& os)
{
	if (argc!=1) return BAD_NUM_ARGS;
	os.precision(2);
	os << "Unix time " << time(NULL) << endl;
	int seconds = gBTS.uptime();
	if (seconds<120) {
		os << "uptime " << seconds << " seconds, frame " << gBTS.time() << endl;
		return SUCCESS;
	}
	float minutes = seconds / 60.0F;
	if (minutes<120) {
		os << "uptime " << minutes << " minutes, frame " << gBTS.time() << endl;
		return SUCCESS;
	}
	float hours = minutes / 60.0F;
	if (hours<48) {
		os << "uptime " << hours << " hours, frame " << gBTS.time() << endl;
		return SUCCESS;
	}
	float days = hours / 24.0F;
	os << "uptime " << days << " days, frame " << gBTS.time() << endl;
	return SUCCESS;
}


/** Give a list of available commands or describe a specific command. */
int showHelp(int argc, char** argv, ostream& os)
{
	if (argc==2) {
		os << argv[1] << " " << gParser.help(argv[1]) << endl;
		return SUCCESS;
	}
	if (argc!=1) return BAD_NUM_ARGS;
	ParseTable::const_iterator cp = gParser.begin();
	os << endl << "Type \"help\" followed by the command name for help on that command." << endl << endl;
	int c=0;
	const int cols = 3;
	while (cp != gParser.end()) {
		const string& wd = cp->first;
		os << wd << '\t';
		if (wd.size()<8) os << '\t';
		++cp;
		c++;
		if (c%cols==0) os << endl;
	}
	if (c%cols!=0) os << endl;
	os << endl << "Lines starting with '!' are escaped to the shell." << endl;
	os << endl << "Use <cntrl-A>, <D> to detach from \"screen\", *not* <cntrl-C>." << endl << endl;
	return SUCCESS;
}



/** A function to return -1, the exit code for the caller. */
int exit_function(int argc, char** argv, ostream& os)
{
	unsigned wait =0;
	if (argc>2) return BAD_NUM_ARGS;
	if (argc==2) wait = atoi(argv[1]);

	if (wait!=0)
		 os << "waiting up to " << wait << " seconds for clearing of "
		<< gBTS.TCHActive() << " active calls" << endl;

	// Block creation of new channels.
	gBTS.hold(true);
	// Wait up to the timeout for active channels to release.
	time_t finish = time(NULL) + wait;
	while (time(NULL)<finish) {
		unsigned load = gBTS.SDCCHActive() + gBTS.TCHActive();
		if (load==0) break;
		sleep(1);
	}
	bool loads = false;
	if (gBTS.SDCCHActive()>0) {
		LOG(WARNING) << "dropping " << gBTS.SDCCHActive() << " control transactions on exit";
		loads = true;
	}
	if (gBTS.TCHActive()>0) {
		LOG(WARNING) << "dropping " << gBTS.TCHActive() << " calls on exit";
		loads = true;
	}
	if (loads) {
		os << endl << "exiting with loads:" << endl;
		printStats(1,NULL,os);
	}
	os << endl << "exiting..." << endl;
	return -1;
}



// Forward ref.
int tmsis(int argc, char** argv, ostream& os);

/** Dump TMSI table to a text file. */
int dumpTMSIs(const char* filename)
{
	ofstream fileout;
	fileout.open(filename, ios::out); // erases existing!
	// FIXME -- Check that the file really opened.
	// Fake an argument list to call printTMSIs.
	char* subargv[] = {"tmsis", NULL};
	int subargc = 1;
	return tmsis(subargc, subargv, fileout);
}




/** Print or clear the TMSI table. */
int tmsis(int argc, char** argv, ostream& os)
{
	if (argc>=2) {
		// Clear?
		if (strcmp(argv[1],"clear")==0) {
			if (argc!=2) return BAD_NUM_ARGS;
			os << "clearing TMSI table" << endl;
			gTMSITable.clear();
			return SUCCESS;
		}
		// Dump?
		if (strcmp(argv[1],"dump")==0) {
			if (argc!=3) return BAD_NUM_ARGS;
			os << "dumping TMSI table to " << argv[2] << endl;
			return dumpTMSIs(argv[2]);
		}
		return BAD_VALUE;
	}

	if (argc!=1) return BAD_NUM_ARGS;
	os << "TMSI       IMSI            age  used" << endl;
	gTMSITable.dump(os);
	return SUCCESS;
}

int isIMSI(const char *imsi)
{
	if (!imsi)
		return 0;

	size_t imsiLen = strlen(imsi);
	if (imsiLen != 15)
		return 0;
	
	for (size_t i = 0; i < imsiLen; i++) {
		if (!isdigit(imsi[i]))
			return 0;
	}

	return 1;
}

/** Submit an SMS for delivery to an IMSI. */
int sendsimple(int argc, char** argv, ostream& os)
{
	if (argc<4) return BAD_NUM_ARGS;

	char *IMSI = argv[1];
	char *srcAddr = argv[2];
	string rest = "";
	for (int i=3; i<argc; i++) rest = rest + argv[i] + " ";
	const char *txtBuf = rest.c_str();

	if (!isIMSI(IMSI)) {
		os << "Invalid IMSI. Enter 15 digits only.";
		return BAD_VALUE;
	}

	static UDPSocket sock(0,"127.0.0.1",gConfig.getNum("SIP.Local.Port"));

	static const char form[] =
		"MESSAGE sip:IMSI%s@127.0.0.1 SIP/2.0\n"
		"Via: SIP/2.0/TCP 127.0.0.1;branch=%x\n"
		"Max-Forwards: 2\n"
		"From: %s <sip:%s@127.0.0.1:%d>;tag=%d\n"
		"To: sip:IMSI%s@127.0.0.1\n"
		"Call-ID: %x@127.0.0.1:%d\n"
		"CSeq: 1 MESSAGE\n"
		"Content-Type: text/plain\nContent-Length: %u\n"
		"\n%s\n";
	static char buffer[1500];
	snprintf(buffer,1499,form,
		IMSI, (unsigned)random(), srcAddr,srcAddr,sock.port(),(unsigned)random(), IMSI, (unsigned)random(),sock.port(), strlen(txtBuf), txtBuf);
	sock.write(buffer);

	os << "message submitted for delivery" << endl;

#if 0
	int numRead = sock.read(buffer,10000);
	if (numRead>=0) {
		buffer[numRead]='\0';
		os << "response: " << buffer << endl;
	} else {
		os << "timed out waiting for response";
	}
#endif

	return SUCCESS;
}

/** Submit an SMS for delivery to an IMSI. */
int sendsms(int argc, char** argv, ostream& os)
{
	if (argc<4) return BAD_NUM_ARGS;

	char *IMSI = argv[1];
	char *srcAddr = argv[2];
	string rest = "";
	for (int i=3; i<argc; i++) rest = rest + argv[i] + " ";
	const char *txtBuf = rest.c_str();

	if (!isIMSI(IMSI)) {
		os << "Invalid IMSI. Enter 15 digits only.";
		return BAD_VALUE;
	}

	Control::TransactionEntry *transaction = new Control::TransactionEntry(
		gConfig.getStr("SIP.Proxy.SMS").c_str(),
		GSM::L3MobileIdentity(IMSI),
		NULL,
		GSM::L3CMServiceType::MobileTerminatedShortMessage,
		GSM::L3CallingPartyBCDNumber(srcAddr),
		GSM::Paging,
		txtBuf);
	transaction->messageType("text/plain");
	Control::initiateMTTransaction(transaction,GSM::SDCCHType,30000);
	os << "message submitted for delivery" << endl;
	return SUCCESS;
}

/** DEBUGGING: Sends a special sms that triggers a RRLP message to an IMSI. */
int sendrrlp(int argc, char** argv, ostream& os)
{
	if (argc!=3) return BAD_NUM_ARGS;

	char *IMSI = argv[1];

	UDPSocket sock(0,"127.0.0.1",gConfig.getNum("SIP.Local.Port"));
	unsigned port = sock.port();
	unsigned callID = random();

	// Just fake out a SIP message.
	const char form[] = "MESSAGE sip:IMSI%s@localhost SIP/2.0\nVia: SIP/2.0/TCP localhost;branch=z9hG4bK776sgdkse\nMax-Forwards: 2\nFrom: RRLP@localhost:%d;tag=49583\nTo: sip:IMSI%s@localhost\nCall-ID: %d@127.0.0.1:5063\nCSeq: 1 MESSAGE\nContent-Type: text/plain\nContent-Length: %lu\n\n%s\n";

	char txtBuf[161];
	snprintf(txtBuf,160,"RRLP%s",argv[2]);
	char outbuf[1500];
	snprintf(outbuf,1499,form,IMSI,port,IMSI,callID,strlen(txtBuf),txtBuf);
	sock.write(outbuf);
	sleep(2);
	sock.write(outbuf);
	sock.close();
	os << "RRLP Triggering message submitted for delivery" << endl;

	return SUCCESS;
}



/** Print current usage loads. */
int printStats(int argc, char** argv, ostream& os)
{
	if (argc!=1) return BAD_NUM_ARGS;
	os << "SDCCH load: " << gBTS.SDCCHActive() << '/' << gBTS.SDCCHTotal() << endl;
	os << "TCH/F load: " << gBTS.TCHActive() << '/' << gBTS.TCHTotal() << endl;
	os << "AGCH/PCH load: " << gBTS.AGCHLoad() << ',' << gBTS.PCHLoad() << endl;
	// paging table size
	os << "Paging table size: " << gBTS.pager().pagingEntryListSize() << endl;
	os << "Transactions: " << gTransactionTable.size() << endl;
	// 3122 timer current value (the number of seconds an MS should hold off the next RACH)
	os << "T3122: " << gBTS.T3122() << " ms" << endl;
	return SUCCESS;
}



/** Get/Set MCC, MNC, LAC, CI. */
int cellID(int argc, char** argv, ostream& os)
{
	if (argc==1) {
		os << "MCC=" << gConfig.getStr("GSM.Identity.MCC")
		<< " MNC=" << gConfig.getStr("GSM.Identity.MNC")
		<< " LAC=" << gConfig.getNum("GSM.Identity.LAC")
		<< " CI=" << gConfig.getNum("GSM.Identity.CI")
		<< endl;
		return SUCCESS;
	}

	if (argc!=5) return BAD_NUM_ARGS;

	// Safety check the args!!
	char* MCC = argv[1];
	char* MNC = argv[2];
	if (strlen(MCC)!=3) {
		os << "MCC must be three digits" << endl;
		return BAD_VALUE;
	}
	int MNCLen = strlen(MNC);
	if ((MNCLen<2)||(MNCLen>3)) {
		os << "MNC must be two or three digits" << endl;
		return BAD_VALUE;
	}
	gTMSITable.clear();
	gConfig.set("GSM.Identity.MCC",MCC);
	gConfig.set("GSM.Identity.MNC",MNC);
	gConfig.set("GSM.Identity.LAC",argv[3]);
	gConfig.set("GSM.Identity.CI",argv[4]);
	return SUCCESS;
}




/** Print table of current transactions. */
int calls(int argc, char** /*argv*/, ostream& os)
{
	if (argc > 2)
		return BAD_NUM_ARGS;

	//fix later -kurtis
	//bool showAll = (argc == 2);
	//size_t count = gTransactionTable.dump(os,showAll);
	size_t count = gTransactionTable.dump(os);
	os << endl << count << " transactions in table" << endl;
	return SUCCESS;
}



/** Print or modify the global configuration table. */
int config(int argc, char** argv, ostream& os)
{
	// no args, just print
	if (argc==1) {
		gConfig.find("",os);
		return SUCCESS;
	}

	// one arg, pattern match and print
	if (argc==2) {
		gConfig.find(argv[1],os);
		return SUCCESS;
	}

	// >1 args: set new value
	string val;
	for (int i=2; i<argc; i++) {
		val.append(argv[i]);
		if (i!=(argc-1)) val.append(" ");
	}
	bool existing = gConfig.defines(argv[1]);
	if (gConfig.isStatic(argv[1])) {
		os << argv[1] << " is static; change takes effect on restart" << endl;
	}
	if (!gConfig.set(argv[1],val)) {
		os << argv[1] << " change failed" << endl;
		return BAD_VALUE;
	}
	if (!existing) {
		os << "defined new config " << argv[1] << " as \"" << val << "\"" << endl;
		// Anything created by the CLI is optional.
		//gConfig.makeOptional(argv[1]);
	} else {
		os << "changed " << argv[1] << " to \"" << val << "\"" << endl;
	}
	return SUCCESS;
}

/** Remove a configiuration value. */
int unconfig(int argc, char** argv, ostream& os)
{
	if (argc!=2) return BAD_NUM_ARGS;

	if (gConfig.unset(argv[1])) {
		os << "\"" << argv[1] << "\" removed from the configuration table" << endl;
		return SUCCESS;
	}
	if (gConfig.defines(argv[1])) {
		os << "\"" << argv[1] << "\" could not be removed" << endl;
	} else {
		os << "\"" << argv[1] << "\" was not in the table" << endl;
	}
	return BAD_VALUE;
}


/** Dump current configuration to a file. */
int configsave(int argc, char** argv, ostream& os)
{
	os << "obsolete" << endl;
	return SUCCESS;
}



/** Change the registration timers. */
int regperiod(int argc, char** argv, ostream& os)
{
	if (argc==1) {
		os << "T3212 is " << gConfig.getNum("GSM.Timer.T3212") << " minutes" << endl;
		os << "SIP registration period is " << gConfig.getNum("SIP.RegistrationPeriod")/60 << " minutes" << endl;
		return SUCCESS;
	}

	if (argc>3) return BAD_NUM_ARGS;

	unsigned newT3212 = strtol(argv[1],NULL,10);
	if ((newT3212<6)||(newT3212>1530)) {
		os << "valid T3212 range is 6..1530 minutes" << endl;
		return BAD_VALUE;
	}

	// By defuault, make SIP registration period 1.5x the GSM registration period.
	unsigned SIPRegPeriod = newT3212*90;
	if (argc==3) {
		SIPRegPeriod = 60*strtol(argv[2],NULL,10);
	}

	// Set the values in the table and on the GSM beacon.
	gConfig.set("SIP.RegistrationPeriod",SIPRegPeriod);
	gConfig.set("GSM.Timer.T3212",newT3212);
	// Done.
	return SUCCESS;
}


/** Print the list of alarms kept by the logger, i.e. the last LOG(ALARM) << <text> */
int alarms(int argc, char** argv, ostream& os)
{
	std::ostream_iterator<std::string> output( os, "\n" );
	std::list<std::string> alarms = gGetLoggerAlarms();
	std::copy( alarms.begin(), alarms.end(), output );
	return SUCCESS;
}


/** Version string. */
int version(int argc, char **argv, ostream& os)
{
	if (argc!=1) return BAD_NUM_ARGS;
	os << gVersionString << endl;
	return SUCCESS;
}

/** Show start-up notices. */
int notices(int argc, char **argv, ostream& os)
{
	if (argc!=1) return BAD_NUM_ARGS;
	os << endl << gOpenBTSWelcome << endl;
	return SUCCESS;
}

int page(int argc, char **argv, ostream& os)
{
	if (argc==1) {
		gBTS.pager().dump(os);
		return SUCCESS;
	}
	if (argc!=3) return BAD_NUM_ARGS;
	char *IMSI = argv[1];
	if (strlen(IMSI)>15) {
		os << IMSI << " is not a valid IMSI" << endl;
		return BAD_VALUE;
	}
	Control::TransactionEntry dummy(
		gConfig.getStr("SIP.Proxy.SMS").c_str(),
		GSM::L3MobileIdentity(IMSI),
		NULL,
		GSM::L3CMServiceType::UndefinedType,
		GSM::L3CallingPartyBCDNumber("0"),
		GSM::Paging);
	gBTS.pager().addID(GSM::L3MobileIdentity(IMSI),GSM::SDCCHType,dummy,1000*atoi(argv[2]));
	return SUCCESS;
}



int endcall(int argc, char **argv, ostream& os)
{
	if (argc!=2) return BAD_NUM_ARGS;
	unsigned transID = atoi(argv[1]);
	Control::TransactionEntry* target = gTransactionTable.find(transID);
	if (!target) {
		os << transID << " not found in table";
		return BAD_VALUE;
	}
	target->terminate();
	return SUCCESS;
}




void printChanInfo(unsigned transID, const GSM::LogicalChannel* chan, ostream& os)
{
	os << setw(2) << chan->CN() << " " << chan->TN();
	os << " " << setw(8) << chan->typeAndOffset();
	os << " " << setw(12) << transID;
	char buffer[200];
	snprintf(buffer,199,"%5.2f %4d %5d %4d",
		100.0*chan->FER(), (int)round(chan->RSSI()),
		chan->actualMSPower(), chan->actualMSTiming());
	os << " " << buffer;
	const GSM::L3MeasurementResults& meas = chan->SACCH()->measurementResults();
	if (!meas.MEAS_VALID()) {
		snprintf(buffer,199,"%5d %5.2f",
			meas.RXLEV_FULL_SERVING_CELL_dBm(),
			100.0*meas.RXQUAL_FULL_SERVING_CELL_BER());
		os << " " << buffer;
	} else {
		os << " ----- ------";
	}
	os << endl;
}



int chans(int argc, char **argv, ostream& os)
{
	if (argc!=1) return BAD_NUM_ARGS;

	os << "CN TN chan      transaction UPFER RSSI TXPWR TXTA DNLEV DNBER" << endl;
	os << "CN TN type      id          pct    dB   dBm  sym   dBm   pct" << endl;

	//gPhysStatus.dump(os);
	//os << endl << "Old data reporting: " << endl;

	// SDCCHs
	GSM::SDCCHList::const_iterator sChanItr = gBTS.SDCCHPool().begin();
	while (sChanItr != gBTS.SDCCHPool().end()) {
		const GSM::SDCCHLogicalChannel* sChan = *sChanItr;
		if (sChan->active()) {
			Control::TransactionEntry *trans = gTransactionTable.find(sChan);
			if (trans) printChanInfo(trans->ID(),sChan,os);
			else printChanInfo(0,sChan,os);
		}
		++sChanItr;
	}

	// TCHs
	GSM::TCHList::const_iterator tChanItr = gBTS.TCHPool().begin();
	while (tChanItr != gBTS.TCHPool().end()) {
		const GSM::TCHFACCHLogicalChannel* tChan = *tChanItr;
		if (tChan->active()) {
			Control::TransactionEntry *trans = gTransactionTable.find(tChan);
			if (trans) printChanInfo(trans->ID(),tChan,os);
			else printChanInfo(0,tChan,os);
		}
		++tChanItr;
	}

	os << endl;

	return SUCCESS;
}




int power(int argc, char **argv, ostream& os)
{
	os << "current downlink power " << gBTS.powerManager().power() << " dB wrt full scale" << endl;
	os << "current attenuation bounds "
		<< gConfig.getNum("GSM.Radio.PowerManager.MinAttenDB")
		<< " to "
		<< gConfig.getNum("GSM.Radio.PowerManager.MaxAttenDB")
		<< " dB" << endl;

	if (argc==1) return SUCCESS;
	if (argc!=3) return BAD_NUM_ARGS;

	int min = atoi(argv[1]);
	int max = atoi(argv[2]);
	if (min>max) return BAD_VALUE;

	gConfig.set("GSM.Radio.PowerManager.MinAttenDB",argv[1]);
	gConfig.set("GSM.Radio.PowerManager.MaxAttenDB",argv[2]);

	os << "new attenuation bounds "
		<< gConfig.getNum("GSM.Radio.PowerManager.MinAttenDB")
		<< " to "
		<< gConfig.getNum("GSM.Radio.PowerManager.MaxAttenDB")
		<< " dB" << endl;

	return SUCCESS;
}


int rxgain(int argc, char** argv, ostream& os)
{
        os << "current RX gain is " << gConfig.getNum("GSM.Radio.RxGain") << " dB" << endl;
        if (argc==1) return SUCCESS;
	if (argc!=2) return BAD_NUM_ARGS;

        int newGain = gTRX.ARFCN(0)->setRxGain(atoi(argv[1]));
        os << "new RX gain is " << newGain << " dB" << endl;
  
        gConfig.set("GSM.Radio.RxGain",newGain);

        return SUCCESS;
}

int noise(int argc, char** argv, ostream& os)
{
        if (argc!=1) return BAD_NUM_ARGS;

        int noise = gTRX.ARFCN(0)->getNoiseLevel();
        os << "noise RSSI is -" << noise << " dB wrt full scale" << endl;
	os << "MS RSSI target is " << gConfig.getNum("GSM.Radio.RSSITarget") << " dB wrt full scale" << endl;

        return SUCCESS;
}

int crashme(int argc, char** argv, ostream& os)
{
        char *nullp = 0x0;
	// we actually have to output this,
	// or the compiler will optimize it out
	os << *nullp;
	return FAILURE;
}


int stats(int argc, char** argv, ostream& os)
{

	char cmd[200];
	if (argc==2)
		sprintf(cmd,"sqlite3 %s 'select name||\": \"||value||\" events over \"||((%lu-clearedtime)/60)||\" minutes\" from reporting where name like \"%%%s%%\";'",
			gConfig.getStr("Control.Reporting.StatsTable").c_str(), time(NULL), argv[1]);
	else if (argc==1)
		sprintf(cmd,"sqlite3 %s 'select name||\": \"||value||\" events over \"||((%lu-clearedtime)/60)||\" minutes\" from reporting;'",
			gConfig.getStr("Control.Reporting.StatsTable").c_str(), time(NULL));
	else return BAD_NUM_ARGS;
	FILE *result = popen(cmd,"r");
	char *line = (char*)malloc(200);
	while (!feof(result)) {
		if (!fgets(line, 200, result)) break;
		os << line;
	}
	free(line);
	os << endl;
	pclose(result);
	return SUCCESS;
}


//@} // CLI commands



void Parser::addCommands()
{
	addCommand("uptime", uptime, "-- show BTS uptime and BTS frame number.");
	addCommand("help", showHelp, "[command] -- list available commands or gets help on a specific command.");
	addCommand("exit", exit_function, "[wait] -- exit the application, either immediately, or waiting for existing calls to clear with a timeout in seconds");
	addCommand("tmsis", tmsis, "[\"clear\"] or [\"dump\" filename] -- print/clear the TMSI table or dump it to a file.");
	addCommand("sendsms", sendsms, "IMSI src# message... -- send direct SMS to IMSI, addressed from source number src#.");
	addCommand("sendsimple", sendsimple, "IMSI src# message... -- send SMS to IMSI via SIP interface, addressed from source number src#.");
	//apparently non-function now -kurtis
	//addCommand("sendrrlp", sendrrlp, "<IMSI> <hexstring> -- send RRLP message <hexstring> to <IMSI>.");
	addCommand("load", printStats, "-- print the current activity loads.");
	addCommand("cellid", cellID, "[MCC MNC LAC CI] -- get/set location area identity (MCC, MNC, LAC) and cell ID (CI)");
	addCommand("calls", calls, "-- print the transaction table");
	addCommand("config", config, "[] OR [patt] OR [key val(s)] -- print the current configuration, print configuration values matching a pattern, or set/change a configuration value");
	addCommand("configsave", configsave, "<path> -- write the current configuration to a file");
	addCommand("regperiod", regperiod, "[GSM] [SIP] -- get/set the registration period (GSM T3212), in MINUTES");
	addCommand("alarms", alarms, "-- show latest alarms");
	addCommand("version", version,"-- print the version string");
	addCommand("page", page, "[IMSI time] -- dump the paging table or page the given IMSI for the given period");
	addCommand("chans", chans, "-- report PHY status for active channels");
	addCommand("power", power, "[minAtten maxAtten] -- report current attentuation or set min/max bounds");
        addCommand("rxgain", rxgain, "[newRxgain] -- get/set the RX gain in dB");
        addCommand("noise", noise, "-- report receive noise level in RSSI dB");
	addCommand("unconfig", unconfig, "key -- remove a config value");
	addCommand("notices", notices, "-- show startup copyright and legal notices");
	addCommand("endcall", endcall,"trans# -- terminate the given transaction");
	addCommand("crashme", crashme, "force crash of OpenBTS for testing purposes");
	addCommand("stats", stats,"[patt] -- print all, or selected, performance statistics");
}




// vim: ts=4 sw=4
