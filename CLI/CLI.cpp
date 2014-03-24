/*
* Copyright 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2012, 2014 Range Networks, Inc.
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
#include <iomanip>
#include <fstream>
#include <iterator>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <algorithm>		// for sort()

#include <config.h>

// #include <Config.h>
#include <Logger.h>

#include <GSMConfig.h>
#include <GSMLogicalChannel.h>
#include <ControlTransfer.h>
#include <L3TranEntry.h>
#include <TRXManager.h>
#include <PowerManager.h>
#include <TMSITable.h>
#include <RadioResource.h>
#include <NeighborTable.h>
#include <Defines.h>
#include <GPRSExport.h>
#include <MAC.h>
#include <L3MMLayer.h>
#include <Utils.h>
#include <SIP2Interface.h>
#include <Peering.h>

std::string getARFCNsString(unsigned band);

namespace SGSN {
	// Hack.
	extern CommandLine::CLIStatus sgsnCLI(int argc, char **argv, std::ostream &os);
};

#include <Globals.h>

#include "CLI.h"

#undef WARNING

extern TransceiverManager gTRX;

namespace CommandLine {
using namespace std;
using namespace Control;

/** Standard responses in the CLI, much mach erorrCode enum. */
static const char* standardResponses[] = {
	"success", // 0
	"wrong number of arguments", // 1
	"bad argument(s)", // 2
	"command not found", // 3
	"too many arguments for parser", // 4
	"command failed", // 5
};


struct CLIParseError {
	string msg;
	CLIParseError(string wMsg) : msg(wMsg) {}
};



CLIStatus Parser::execute(char* line, ostream& os) const
{
	LOG(INFO) << "executing console command: " << line;

	// tokenize
	char *argv[mMaxArgs];
	int argc = 0;
	// This is (almost) straight from the man page for strsep.
	while (line && argc < mMaxArgs) {
		while (*line == ' ') { line++; }
		if (! *line) { break; }
		char *anarg = line;
		if (*line == '"') {		// We allow a quoted string as a single argument.  Quotes themselves are removed.
			line++; anarg++;
			char *endquote = strchr(line,'"');
			if (endquote == NULL) {
				os << "error: Missing quote."<<endl;
				return FAILURE;
			}
			if (!(endquote[1] == 0 || endquote[1] == ' ')) {
				os << "error: Embedded quotes not allowed." << endl;
				return FAILURE;
			}
			*endquote = 0;
			line = endquote+1;
		} else if (strsep(&line," ") == NULL) {
			break;
		}
		argv[argc++] = anarg;
	}
	//for (ap=argv; (*ap=strsep(&line," ")) != NULL; ) {
	//	if (**ap != '\0') {
	//		if (++ap >= &argv[mMaxArgs]) break;
	//		else argc++;
	//	}
	//}
	// Blank line?
	if (!argc) return SUCCESS;
	// Find the command.
	//printf("args=%d\n",argc);
	//for (int i = 0; i < argc; i++) { printf("argv[%d]=%s\n",i,argv[i]); }
	ParseTable::const_iterator cfp = mParseTable.find(argv[0]);
	if (cfp == mParseTable.end()) {
		return NOT_FOUND;
	}
	CLICommand func;
	func = cfp->second;
	// Do it.
	CLIStatus retVal;
	try {
		retVal = (*func)(argc,argv,os);
	} catch (CLIParseError &pe) {
		os << pe.msg << endl;
		retVal = SUCCESS;	// Dont print any further messages.
	}
	// Give hint on bad # args.
	if (retVal==BAD_NUM_ARGS) os << help(argv[0]) << endl;
	return retVal;
}


// This is called from a runloop in apps/OpenBTS.cpp
// If it returns a negative number OpenBTS exists.
CLIStatus Parser::process(const char* line, ostream& os) const
{
	static Mutex oneCommandAtATime;
	ScopedLock lock(oneCommandAtATime);
	char *newLine = strdup(line);
	CLIStatus retVal = execute(newLine,os);
	free(newLine);
	if (retVal < 0 || retVal > FAILURE) {
		os << "Unrecognized CLI command exit status: "<<retVal << endl;
	} else if (retVal != SUCCESS) {
		os << standardResponses[retVal] << endl;
	}
	return retVal;
}


static void *commandLineFunc(void *arg)
{
	const char *prompt = "OpenBTS> ";		//gConfig.getStr("CLI.Prompt");  CLI.Prompt no longer defined.
	try {
#ifdef HAVE_READLINE
		using_history();
		while (true) {
			clearerr(stdin);	// Control-D may set the eof bit which causes getline to return immediately.  Fix it.
			char *inbuf = readline(prompt);
			if (inbuf) {
				if (*inbuf) {
					add_history(inbuf);
					// The parser returns -1 on exit.
					if (gParser.process(inbuf, cout, cin)<0) {
						free(inbuf);
						break;
					}
				}
				free(inbuf);
			} else {
				printf("EOF ignored\n");
			}
			sleep(1); // in case something goofs up here, dont steal all the cpu cycles.
		}
#else
		while (true) {
			//cout << endl << 
			cout << endl << prompt;
			cout.flush();
			char inbuf[1024];
			cin.clear();	// Control-D may set the eof bit which causes getline to return immediately.  Fix it.
			cin.getline(inbuf,1024,'\n');		// istream::getline
			if (!cin.fail()) {
				// The parser returns -1 on exit.
				if (gParser.process(inbuf,cout)<0) break;
			}
			sleep(1); // in case something goofs up here, dont steal all the cpu cycles.
		}
#endif
	} catch (ConfigurationTableKeyNotFound e) {
		LOG(EMERG) << "required configuration parameter " << e.key() << " not defined, aborting";
		gReports.incr("OpenBTS.Exit.Error.ConfigurationParamterNotFound");
	}

	exit(0);	// Exit OpenBTS
	return NULL;
}

void Parser::startCommandLine()	// (pat) Start a simple command line processor as a separate thread.
{
	static Thread commandLineThread;
	commandLineThread.start(commandLineFunc,NULL);
}


const char * Parser::help(const string& cmd) const
{
	HelpTable::const_iterator hp = mHelpTable.find(cmd);
	if (hp==mHelpTable.end()) return "no help available";
	return hp->second.c_str();
}


// Parse options in optstring out of argc,argv.
// The optstring is a space separated list of options.  The options need not start with '-'.  To recognize just "-" or "--" just add it in.
// Return a map containing the options found; if option in optstring was followed by ':', map value will be the next argv argument, otherwise "true".
// Leave argc,argv pointing at the first argument after the options, ie, on return argc is the number of non-option arguments remaining in argv.
// This routine does not allow combining options, ie, -a -b != -ab
static map<string,string> cliParse(int &argc, char **&argv, ostream &os, const char *optstring)
{
	map<string,string> options;		// The result
	// Skip the command name.
	argc--, argv++;
	// Parse args.
	for ( ; argc > 0; argc--, argv++ ) {
		char *arg = argv[0];
		// The argv to match may not contain ':' to prevent the pathological case, for example, where optionlist contains "-a:" and command line arg is "-a:"
		if (strchr(arg,':')) { return options; }	// Can't parse this, too dangerous.
		const char *op = strstr(optstring,arg);
		if (op && (op == optstring || op[-1] == ' ')) {
			const char *ep = op + strlen(arg);
			if (*ep == ':') {
				// This valid option requires an argument.
				argc--, argv++;
				if (argc <= 0) { throw CLIParseError(format("expected argument after: %s",arg)); }
				options[arg] = string(argv[0]);
				continue;
			} else if (*ep == 0 || *ep == ' ') {
				// This valid option does not require an argument.
				options[arg] = string("true");
				continue;
			} else {
				// Partial match of something in optstring; drop through to treat it like any other argument.
			}
		} else {
			break;	// Return when we find the first non-option.
		}
		// An argument beginning with - and not in optstring is an unrecognized option and is an error.
		if (*arg == '-') { throw CLIParseError(format("unrecognized argument: %s",arg)); }
		return options;
	}
	return options;
}


/**@name Commands for the CLI. */
//@{

// forward refs
static CLIStatus printStats(int argc, char** argv, ostream& os);

/*
	A CLI command takes the argument in an array.
	It returns 0 on success.
*/

/** Display system uptime and current GSM frame number. */
static CLIStatus uptime(int argc, char** argv, ostream& os)
{
	if (argc!=1) return BAD_NUM_ARGS;
	os.precision(2);

	time_t now = time(NULL);
	const char* timestring = ctime(&now);
	// no endl since ctime includes a "\n" in the string
	os << "Unix time " << now << ", " << timestring;

	os << "watchdog timer expires in " << (gWatchdogRemaining() / 60) << " minutes" << endl;

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
static CLIStatus showHelp(int argc, char** argv, ostream& os)
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
	return SUCCESS;
}



/** A function to return -1, the exit code for the caller. */
static CLIStatus exit_function(int argc, char** argv, ostream& os)
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
	LOG(ALERT) << "exiting OpenBTS as directed by command line...";	// This is sent to the log file.
	os << endl << "exiting..." << endl;								// This is sent to OpenBTSCLI
	// We have to return CLI_EXIT rather than just exiting so we can send the result to OpenBTSCLI.
	return CLI_EXIT;
}


/** Print or clear the TMSI table. */
static const char *tmsisHelp = "[-a | -l | -ll | -r | clear | dump [-l] <filename> | delete -tmsi <tmsi> | delete -imsi <imsi> | query <query> set name=value] --\n"
	"   default print the TMSI table;  -l or -ll gives longer listing;\n"
	"   -a lists all TMSIs, default is to show most recent 100 in table\n"
	"   -r raw TMSI table listing\n"
	"   clear - clear the TMSI table;\n"
	"   dump - dump the TMSI table to specified filename;\n"
	"   delete - delete entry for specified imsi or tmsi;\n"
	"   set name=value - set TMSI database field name to value.  If value is a string use apostrophes, eg: set IMSI='12345678901234'\n"
	"   query - run sql query, which may be quoted, eg: tmsis query \"UPDATE TMSI_TABLE SET AUTH=0 WHERE IMSI=='123456789012'\" This option may be removed in future."
	;
static CLIStatus tmsis(int argc, char** argv, ostream& os)
{
	// (pat) We used to allow just "dump" or "clear", so be backward compatible for a while.
	map<string,string> options = cliParse(argc,argv,os,"-a -l -ll -r dump: clear delete -imsi: -tmsi: query: set:");
	string imsiopt = options["-imsi"];
	string tmsiopt = options["-tmsi"];
	unsigned tmsi = strtoul(tmsiopt.c_str(),NULL,0);	// No bad effect if option is empty.
	string myquery;
	if (argc) return BAD_NUM_ARGS;
	int verbose = 0;
	if (options.count("-l")) { verbose = 1; }
	if (options.count("-ll")) { verbose = 2; }
	bool showAll = options.count("-a");
	if (options.count("clear")) {
		os << "clearing TMSI table" << endl;
		gTMSITable.tmsiTabClear();
		return SUCCESS;
	}
	if (options.count("dump")) {
		ofstream fileout;
		string filename = options["dump"];
		if (filename.size() == 0) { os << "bad filename"<<endl; return FAILURE; }
		os << "dumping TMSI table to " << filename << endl;
		fileout.open(filename.c_str(), ios::out); // erases existing!
		gTMSITable.tmsiTabDump(verbose,options.count("-r"),fileout,showAll);
		return SUCCESS;
	}
	if (options.count("delete")) {
		if (tmsiopt.size()) {
			if (gTMSITable.dropTmsi(tmsi)) {
				os << format("Deleted TMSI table entry for 0x%x",tmsi) << endl;
			} else {
				os << format("Cound not delete TMSI table entry for 0x%x",tmsi) << endl;
				return FAILURE;
			}
		} else if (imsiopt.size()) {
			if (gTMSITable.dropImsi(imsiopt.c_str())) {
				os << format("Deleted TMSI table entry for %s",imsiopt) << endl;
			} else {
				os << format("Cound not delete TMSI table entry for %s",imsiopt) << endl;
				return FAILURE;
			}
		} else {
			oops2:
			os << "expecting: -tmsi or -imsi option" << endl;
			return FAILURE;
		}
		return SUCCESS;
	}
	if (options.count("set")) {
		if (tmsiopt.size()) {
			myquery = format("UPDATE TMSI_TABLE SET %s WHERE TMSI==%u",options["set"],tmsi);
		} else if (imsiopt.size()) {
			myquery = format("UPDATE TMSI_TABLE SET %s WHERE IMSI==%s",options["set"],options["-imsi"]);
		} else {
			goto oops2;
		}
		goto runmyquery;
	}
	if (options.count("query")) {
		myquery = options["query"];
		runmyquery:
		if (gTMSITable.runQuery(myquery.c_str(),0)) {
			os << "Query success."<<endl;
			return SUCCESS;
		} else {
			os << "Query failed:"<<myquery<<endl;
			return FAILURE;
		}
	}
	gTMSITable.tmsiTabDump(verbose,options.count("-r"),os,showAll);
	return SUCCESS;
}

int isIMSI(const char *imsi)
{
	if (!imsi)
		return 0;
	if (strlen(imsi) != 15)
		return 0;
	
	for (unsigned i = 0; i < strlen(imsi); i++) {
		if (!isdigit(imsi[i]))
			return 0;
	}

	return 1;
}

/** Submit an SMS for delivery to an IMSI. */
static CLIStatus sendsimple(int argc, char** argv, ostream& os)
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
		"MESSAGE sip:IMSI%s@127.0.0.1 SIP/2.0\r\n"
		"Via: SIP/2.0/TCP 127.0.0.1:%d;branch=%x\r\n"	// (pat) The via MUST contain the port.
		"Max-Forwards: 2\r\n"
		"From: %s <sip:%s@127.0.0.1:%d>;tag=%d\r\n"
		"To: sip:IMSI%s@127.0.0.1\r\n"
		"Call-ID: %x@127.0.0.1:%d\r\n"
		"CSeq: 1 MESSAGE\r\n"
		"Content-Type: text/plain\nContent-Length: %u\r\n"
		"\r\n%s\r\n";
	static char buffer[1500];
	snprintf(buffer,1499,form,
		IMSI, sock.port(), // via
		(unsigned)random(), // branch
		srcAddr,srcAddr,sock.port(), // from
		(unsigned)random(), // tag
		IMSI, // to imsi
		(unsigned)random(), sock.port(), // Call-ID
		strlen(txtBuf), txtBuf);	// Content-Type and content.
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


/** Submit an SMS for delivery to an IMSI on this BTS. */
static CLIStatus sendsms(int argc, char** argv, ostream& os)
{
	if (argc<4) return BAD_NUM_ARGS;

	char *IMSI = argv[1];
	char *srcAddr = argv[2];
	string rest = "";
	for (int i=3; i<argc; i++) rest = rest + argv[i] + " ";

	if (!isIMSI(IMSI)) {
		os << "Invalid IMSI. Enter 15 digits only.";
		return BAD_VALUE;
	}

	// We just use the IMSI, dont try to find a tmsi.
	FullMobileId msid(IMSI);
	Control::TranEntry *tran = Control::TranEntry::newMTSMS(
						NULL,	// No SIPDialog
						msid,
						GSM::L3CallingPartyBCDNumber(srcAddr),
						rest,					// message body
						string("text/plain"));	// messate content type
	Control::gMMLayer.mmAddMT(tran);
	os << "message submitted for delivery" << endl;
	return SUCCESS;
}


/** Print current usage loads. */
static CLIStatus printStats(int argc, char** argv, ostream& os)
{
	// FIXME -- This needs to take GPRS channels into account. See #762.
	if (argc!=1) return BAD_NUM_ARGS;
	os << "== GSM ==" << endl;
	os << "SDCCH load: " << gBTS.SDCCHActive() << '/' << gBTS.SDCCHTotal() << endl;
	os << "TCH/F load: " << gBTS.TCHActive() << '/' << gBTS.TCHTotal() << endl;
	os << "AGCH/PCH load: " << gBTS.AGCHLoad() << ',' << gBTS.PCHLoad() << " (target <= 3)" << endl;
	// paging table size
	os << "Paging table size: " << gBTS.pager().pagingEntryListSize() << endl;
	os << "Transactions: " << gNewTransactionTable.size() << endl;
	// 3122 timer current value (the number of seconds an MS should hold off the next RACH)
	os << "T3122: " << gBTS.T3122() << " ms (target " << gConfig.getNum("GSM.Radio.PowerManager.TargetT3122") << " ms)" << endl;
	os << "== GPRS ==" << endl;
	// (pat) We are not using dynamic channel allocation so I removed GPRS.Channels.Max until such time as we do.
	// Also I did not understand what is the point of printing out something that is in the config?
	//os << "chans mn/mx/dn/up: "
	//	<< gConfig.getNum("GPRS.Channels.Min") << '/' << gConfig.getNum("GPRS.Channels.Max")
	//	<< '/' << gConfig.getNum("GPRS.Multislot.Max.Downlink") << '/' << gConfig.getNum("GPRS.Multislot.Max.Uplink") << endl;
	os << "current PDCHs: " << GPRS::gL2MAC.macActiveChannels() << endl;
	os << "utilization: " << 100 * GPRS::gL2MAC.macComputeUtilization() << "%" << endl;
	return SUCCESS;
}



/** Get/Set MCC, MNC, LAC, CI. */
static CLIStatus cellID(int argc, char** argv, ostream& os)
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
	if (!gConfig.isValidValue("GSM.Identity.MCC", argv[1])) {
		os << "MCC must be three digits" << endl;
		return BAD_VALUE;
	}
	if (!gConfig.isValidValue("GSM.Identity.MNC", argv[2])) {
		os << "MNC must be two or three digits" << endl;
		return BAD_VALUE;
	}
	if (!gConfig.isValidValue("GSM.Identity.LAC", argv[3])) {
		os << "Invalid value for LAC" << endl;
		return BAD_VALUE;
	}
	if (!gConfig.isValidValue("GSM.Identity.CI", argv[4])) {
		os << "Invalid value for CI" << endl;
		return BAD_VALUE;
	}

	gTMSITable.tmsiTabClear();
	gConfig.set("GSM.Identity.MCC",argv[1]);
	gConfig.set("GSM.Identity.MNC",argv[2]);
	gConfig.set("GSM.Identity.LAC",argv[3]);
	gConfig.set("GSM.Identity.CI",argv[4]);
	return SUCCESS;
}

static CLIStatus handover(int argc, char** argv, ostream& os)
{
	if (argc!=3) return BAD_NUM_ARGS;
	string imsi(strncasecmp(argv[1],"IMSI",4)==0 ? argv[1]+4 : argv[1]);	// Allow "IMSI1234..." or just "1234..."
	RefCntPointer<TranEntry> tran = gMMLayer.mmFindVoiceTranByImsi(imsi);
	if (tran == 0) {
		os << "IMSI not found:"<<imsi;
		return BAD_VALUE;
	}
	string peer;

	// Allow "N0" to pick the first neighbor, etc.
	if (argv[2][0] == 'N' || argv[2][0] == 'n') {
		int nth = atoi(argv[2]+1);
		vector<string> neighbors = gConfig.getVectorOfStrings("GSM.Neighbors");
		if (nth < 0 || nth >= (int)neighbors.size()) {
			os << format("Specified neighbor index '%d' out of bounds.  There are %d neighbors.",nth,neighbors.size());
			return BAD_VALUE;
		}
		peer = neighbors[nth];
	} else {
		peer = string(argv[2]);
	}

	if (! gPeerInterface.sendHandoverRequest(peer,tran)) {
		return BAD_VALUE;
	}
	return SUCCESS;	// success of the handover CLI command, not success of the handover.
}


/** Print table of current transactions. */
// (pat) In version 4 this dumps the MM layer.
static CLIStatus calls(int argc, char** argv, ostream& os)
{
	map<string,string> options = cliParse(argc,argv,os,"-a -all -t -m -s");
	if (argc) return BAD_NUM_ARGS;
	bool showAll = options.count("-a") || options.count("-all");	// -a and -all are synonyms.
	bool trans = options.count("-t");
	bool mm = options.count("-m");
	bool sip = options.count("-s");
#if 0
	bool showAll = false;
	bool trans = false;
	bool mm = false;
	bool sip = false;
	// Parse args.
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			for (char*op = argv[i]+1; *op; op++) {
				switch (*op) {
					case 'm': mm = true; continue;
					case 'a': showAll = true; continue;
					case 's': sip = true; continue;
					case 't': trans = true; continue;
					case 'l': continue;	// Allows -all without complaint.
					default:
						os << "unrecognized argument:"<<argv[i]<<" ";
						return BAD_VALUE;
				}
			}
		} else {
			// This command doesnt take any non-option arguments.
			os << "unrecognized argument:"<<argv[i]<<" ";
			return BAD_VALUE;
		}
	}
#endif
	// If no specific options, default is to show transactions
	if (!(trans || mm || sip)) { trans = true; }

	if (trans) {
		size_t count = gNewTransactionTable.dump(os,showAll);
		os << endl << count << " transactions in table" << endl;
	}
	if (mm) {
		Control::gMMLayer.printMMInfo(os);
	}
	if (sip) {
		SIP::printDialogs(os);
	}
	return SUCCESS;
}


/** Print table of current transactions in tabular format. */
// (pat) In version 4 this dumps the MM layer.
static CLIStatus transactions(int argc, char** argv, ostream& os)
{
	map<string,string> options = cliParse(argc,argv,os,"purge");
	if (argc>1) {
		return BAD_NUM_ARGS;
	}

	TranEntry::header(os);
	size_t count = gStaleTransactionTable.dumpTable(os);
	os << endl << count << " completed transaction records in table" << endl;

	if (options.count("purge")) {
		gStaleTransactionTable.clearTable();
		os << "records purged" << endl;
	}

	return SUCCESS;
}



/** Print or modify the global configuration table. */
static CLIStatus rawconfig(int argc, char** argv, ostream& os)
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
	string previousVal;
	if (existing) {
		previousVal = gConfig.getStr(argv[1]);
	}
	if (!gConfig.set(argv[1],val)) {
		os << "DB ERROR: " << argv[1] << " change failed" << endl;
		return FAILURE;
	}
	if (gConfig.isStatic(argv[1])) {
		os << argv[1] << " is static; change takes effect on restart" << endl;
	}
	if (!existing) {
		os << "defined new config " << argv[1] << " as \"" << val << "\"" << endl;
	} else {
		os << argv[1] << " changed from \"" << previousVal << "\" to \"" << val << "\"" << endl;
	}
	return SUCCESS;
}

static CLIStatus trxfactory(int argc, char** argv, ostream& os)
{
	if (argc!=1) return BAD_NUM_ARGS;

	signed val = gTRX.ARFCN(0)->getFactoryCalibration("sdrsn");
	if (val == 0 || val == 65535) {
		os << "Reading factory calibration not supported on this radio." << endl;
		return SUCCESS;
	}
	os << "Factory Information" << endl;
	os << "  SDR Serial Number = " << val << endl;

	val = gTRX.ARFCN(0)->getFactoryCalibration("rfsn");
	os << "  RF Serial Number = " << val << endl;

	val = gTRX.ARFCN(0)->getFactoryCalibration("band");
	os << "  GSM.Radio.Band = ";
	if (val == 0) {
		os << "multi-band";
	} else {
		os << val;
	}
	os << endl;

	val = gTRX.ARFCN(0)->getFactoryCalibration("rxgain");
	os << "  GSM.Radio.RxGain = " << val << endl;

	val = gTRX.ARFCN(0)->getFactoryCalibration("txgain");
	os << "  TRX.TxAttenOffset = " << val << endl;

	val = gTRX.ARFCN(0)->getFactoryCalibration("freq");
	os << "  TRX.RadioFrequencyOffset = " << val << endl;

	return SUCCESS;
}

/** Audit the current configuration. */
static CLIStatus audit(int argc, char** argv, ostream& os)
{
	ConfigurationKeyMap::iterator mp;
	stringstream ss;

	// value errors
	mp = gConfig.mSchema.begin();
	while (mp != gConfig.mSchema.end()) {
		if (!gConfig.isValidValue(mp->first, gConfig.getStr(mp->first))) {
			ss << mp->first << " \"" << gConfig.getStr(mp->first) << "\" (\"" << mp->second.getDefaultValue() << "\")" << endl;
		}
		mp++;
	}
	if (ss.str().length()) {
		os << "+---------------------------------------------------------------------+" << endl;
		os << "| ERROR : Invalid Values [key current-value (default)]                |" << endl;
		os << "|   To use the default value again, execute: rmconfig key             |" << endl;
		os << "+---------------------------------------------------------------------+" << endl;
		os << ss.str();
		os << endl;
		ss.str("");
	}

	// factory calibration warnings
	signed sdrsn = gTRX.ARFCN(0)->getFactoryCalibration("sdrsn");
	if (sdrsn != 0 && sdrsn != 65535) {
		string factoryValue;
		string configValue;

		factoryValue = gConfig.mSchema["GSM.Radio.Band"].getDefaultValue();
		configValue = gConfig.getStr("GSM.Radio.Band");
		// only warn on band changes if the unit is not multi-band
		if (gTRX.ARFCN(0)->getFactoryCalibration("band") != 0 && configValue.compare(factoryValue) != 0) {
			ss << "GSM.Radio.Band \"" << configValue << "\" (\"" << factoryValue << "\")" << endl;
		}

		factoryValue = gConfig.mSchema["GSM.Radio.RxGain"].getDefaultValue();
		configValue = gConfig.getStr("GSM.Radio.RxGain");
		if (configValue.compare(factoryValue) != 0) {
			ss << "GSM.Radio.RxGain \"" << configValue << "\" (\"" << factoryValue << "\")" << endl;
		}

		factoryValue = gConfig.mSchema["TRX.TxAttenOffset"].getDefaultValue();
		configValue = gConfig.getStr("TRX.TxAttenOffset");
		if (configValue.compare(factoryValue) != 0) {
			ss << "TRX.TxAttenOffset \"" << configValue << "\" (\"" << factoryValue << "\")" << endl;
		}

		factoryValue = gConfig.mSchema["TRX.RadioFrequencyOffset"].getDefaultValue();
		configValue = gConfig.getStr("TRX.RadioFrequencyOffset");
		if (configValue.compare(factoryValue) != 0) {
			ss << "TRX.RadioFrequencyOffset \"" << configValue << "\" (\"" << factoryValue << "\")" << endl;
		}

		if (ss.str().length()) {
			os << "+---------------------------------------------------------------------+" << endl;
			os << "| WARNING : Factory Radio Calibration [key current-value (factory)]   |" << endl;
			os << "|   To use the factory value again, execute: rmconfig key             |" << endl;
			os << "+---------------------------------------------------------------------+" << endl;
			os << ss.str();
			os << endl;
			ss.str("");
		}
	}

	// cross check warnings
	vector<string> allWarnings;
	vector<string> warnings;
	vector<string>::iterator warning;
	mp = gConfig.mSchema.begin();
	while (mp != gConfig.mSchema.end()) {
		warnings = gConfig.crossCheck(mp->first);
		allWarnings.insert(allWarnings.end(), warnings.begin(), warnings.end());
		mp++;
	}
	sort(allWarnings.begin(), allWarnings.end());
	allWarnings.erase(unique(allWarnings.begin(), allWarnings.end() ), allWarnings.end());
	warning = allWarnings.begin();
	while (warning != allWarnings.end()) {
		ss << *warning << endl;
		warning++;
	}
	if (ss.str().length()) {
		os << "+---------------------------------------------------------------------+" << endl;
		os << "| WARNING : Cross-Check Values                                        |" << endl;
		os << "|   To quiet these warnings, follow the advice given.                 |" << endl;
		os << "+---------------------------------------------------------------------+" << endl;
		os << ss.str();
		os << endl;
		ss.str("");
	}

	// site-specific values
	mp = gConfig.mSchema.begin();
	while (mp != gConfig.mSchema.end()) {
		if (mp->second.getVisibility() == ConfigurationKey::CUSTOMERSITE) {
			if (gConfig.getStr(mp->first).compare(gConfig.mSchema[mp->first].getDefaultValue()) == 0) {
				ss << mp->first << " \"" << gConfig.mSchema[mp->first].getDefaultValue() << "\"" << endl;
			}
		}
		mp++;
	}
	if (ss.str().length()) {
		os << "+---------------------------------------------------------------------+" << endl;
		os << "| WARNING : Site Values Which Are Still Default [key current-value]   |" << endl;
		os << "|   These should be set to fit your installation: config key value    |" << endl;
		os << "+---------------------------------------------------------------------+" << endl;
		os << ss.str();
		os << endl;
		ss.str("");
	}


	// non-default values
	mp = gConfig.mSchema.begin();
	while (mp != gConfig.mSchema.end()) {
		if (mp->second.getVisibility() != ConfigurationKey::CUSTOMERSITE) {
			if (gConfig.getStr(mp->first).compare(gConfig.mSchema[mp->first].getDefaultValue()) != 0) {
				ss << mp->first << " \"" << gConfig.getStr(mp->first) << "\" (\"" << mp->second.getDefaultValue() << "\")" << endl;
			}
		}
		mp++;
	}
	if (ss.str().length()) {
		os << "+---------------------------------------------------------------------+" << endl;
		os << "| INFO : Non-Default Values [key current-value (default)]             |" << endl;
		os << "|   To use the default value again, execute: rmconfig key             |" << endl;
		os << "+---------------------------------------------------------------------+" << endl;
		os << ss.str();
		os << endl;
		ss.str("");
	}

	// unknown pairs
	ConfigurationRecordMap pairs = gConfig.getAllPairs();
	ConfigurationRecordMap::iterator mp2 = pairs.begin();
	while (mp2 != pairs.end()) {
		if (!gConfig.keyDefinedInSchema(mp2->first)) {
			// also kindly ignore SIM.Prog keys for now so the users don't kill their ability to program SIMs
			string family = "SIM.Prog.";
			if (mp2->first.substr(0, family.size()) != family) {
				ss << mp2->first << " \"" << mp2->second.value() << "\"" << endl;
			}
		}
		mp2++;
	}
	if (ss.str().length()) {
		os << "+---------------------------------------------------------------------+" << endl;
		os << "| INFO : Custom/Deprecated Key/Value Pairs [key current-value]        |" << endl;
		os << "|   To clean up any extraneous keys, execute: rmconfig key            |" << endl;
		os << "+---------------------------------------------------------------------+" << endl;
		os << ss.str();
		os << endl;
		ss.str("");
	}

	return SUCCESS;
}

/** Print or modify the global configuration table. */
CLIStatus _config(string mode, int argc, char** argv, ostream& os)
{
	// no args, just print
	if (argc==1) {
		ConfigurationKeyMap::iterator mp = gConfig.mSchema.begin();
		while (mp != gConfig.mSchema.end()) {
			if (mode.compare("customer") == 0) {
				if (mp->second.getVisibility() == ConfigurationKey::CUSTOMER ||
					mp->second.getVisibility() == ConfigurationKey::CUSTOMERSITE ||
					mp->second.getVisibility() == ConfigurationKey::CUSTOMERTUNE ||
					mp->second.getVisibility() == ConfigurationKey::CUSTOMERWARN) {
						ConfigurationKey::printKey(mp->second, gConfig.getStr(mp->first), os);
				}
			} else if (mode.compare("developer") == 0) {
				ConfigurationKey::printKey(mp->second, gConfig.getStr(mp->first), os);
			}
			mp++;
		}
		return SUCCESS;
	}

	// one arg
	if (argc==2) {
		// matches exactly? print single key
		if (gConfig.keyDefinedInSchema(argv[1])) {
			ConfigurationKey::printKey(gConfig.mSchema[argv[1]], gConfig.getStr(argv[1]), os);
			ConfigurationKey::printDescription(gConfig.mSchema[argv[1]], os);
			os << endl;
		// ...otherwise print all similar keys
		} else {
			int foundCount = 0;
			ConfigurationKeyMap matches = gConfig.getSimilarKeys(argv[1]);
			ConfigurationKeyMap::iterator mp = matches.begin();
			while (mp != matches.end()) {
				if (mode.compare("customer") == 0) {
					if (mp->second.getVisibility() == ConfigurationKey::CUSTOMER ||
						mp->second.getVisibility() == ConfigurationKey::CUSTOMERSITE ||
						mp->second.getVisibility() == ConfigurationKey::CUSTOMERTUNE ||
						mp->second.getVisibility() == ConfigurationKey::CUSTOMERWARN) {
							ConfigurationKey::printKey(mp->second, gConfig.getStr(mp->first), os);
							foundCount++;
					}
				} else if (mode.compare("developer") == 0) {
					ConfigurationKey::printKey(mp->second, gConfig.getStr(mp->first), os);
					foundCount++;
				}
				mp++;
			}
			if (!foundCount) {
				os << argv[1] << " - no keys matched";
				if (mode.compare("customer") == 0) {
					os << ", developer/factory keys can be accessed with \"devconfig.\"";
				} else if (mode.compare("developer") == 0) {
					os << ", custom keys can be accessed with \"rawconfig.\"";
				}
				os << endl;
			}
		}
		return SUCCESS;
	}

	// >1 args: set new value
	string val;
	for (int i=2; i<argc; i++) {
		val.append(argv[i]);
		if (i!=(argc-1)) val.append(" ");
	}
	if (!gConfig.keyDefinedInSchema(argv[1])) {
		os << argv[1] << " is not a valid key, change failed. If you're trying to define a custom key/value pair (e.g. the Log.Level.Filename.cpp pairs), use \"rawconfig.\"" << endl;
		return SUCCESS;
	}
	if (mode.compare("customer") == 0) {
		if (gConfig.mSchema[argv[1]].getVisibility() == ConfigurationKey::DEVELOPER) {
			os << argv[1] << " should only be changed by developers. Use \"devconfig\" if you are ABSOLUTELY sure this needs to be changed." << endl;
			return SUCCESS;
		}
		if (gConfig.mSchema[argv[1]].getVisibility() == ConfigurationKey::FACTORY) {
			os << argv[1] << " should only be set once by the factory. Use \"devconfig\" if you are ABSOLUTELY sure this needs to be changed." << endl;
			return SUCCESS;
		}
	}
	if (!gConfig.isValidValue(argv[1], val)) {
		os << argv[1] << " new value \"" << val << "\" is invalid, change failed.";
		if (mode.compare("developer") == 0) {
			os << " To override the configuration value checks, use \"rawconfig.\"";
		}
		os << endl;
		return SUCCESS;
	}

	string previousVal = gConfig.getStr(argv[1]);
	if (val.compare(previousVal) == 0) {
		os << argv[1] << " is already set to \"" << val << "\", nothing changed" << endl;
		return SUCCESS;
	}
// TODO : removing of default values from DB disabled for now. Breaks webui.
//	if (val.compare(gConfig.mSchema[argv[1]].getDefaultValue()) == 0) {
//		if (!gConfig.remove(argv[1])) {
//			os << argv[1] << " storing new value (default) failed" << endl;
//			return SUCCESS;
//		}
//	} else {
		if (!gConfig.set(argv[1],val)) {
			os << "DB ERROR: " << argv[1] << " could not be updated" << endl;
			return FAILURE;
		}
//	}
	if (string(argv[1]).compare("GSM.Radio.Band") == 0) {
		gConfig.mSchema["GSM.Radio.C0"].updateValidValues(getARFCNsString(gConfig.getNum("GSM.Radio.Band")));
	}
	vector<string> warnings = gConfig.crossCheck(argv[1]);
	vector<string>::iterator warning = warnings.begin();
	while (warning != warnings.end()) {
		os << "WARNING: " << *warning << endl;
		warning++;
	}
	if (gConfig.isStatic(argv[1])) {
		os << argv[1] << " is static; change takes effect on restart" << endl;
	}
	os << argv[1] << " changed from \"" << previousVal << "\" to \"" << val << "\"" << endl;

	return SUCCESS;
}

/** Print or modify the global configuration table. Customer access. */
static CLIStatus config(int argc, char** argv, ostream& os)
{
	return _config("customer", argc, argv, os);
}

/** Print or modify the global configuration table. Developer/factory access. */
static CLIStatus devconfig(int argc, char** argv, ostream& os)
{
	return _config("developer", argc, argv, os);
}

/** Disable a configuration key. */
static CLIStatus unconfig(int argc, char** argv, ostream& os)
{
	if (argc!=2) return BAD_NUM_ARGS;

	if (!gConfig.defines(argv[1])) {
		os << argv[1] << " is not in the table" << endl;
		return BAD_VALUE;
	}

	if (gConfig.keyDefinedInSchema(argv[1]) && !gConfig.isValidValue(argv[1], "")) {
		os << argv[1] << " is not disableable" << endl;
		return BAD_VALUE;
	}

	if (!gConfig.set(argv[1], "")) {
		os << "DB ERROR: " << argv[1] << " could not be disabled" << endl;
		return FAILURE;
	}

	os << argv[1] << " disabled" << endl;

	return SUCCESS;
}


/** Set a configuration value back to default or remove from table if custom key. */
static CLIStatus rmconfig(int argc, char** argv, ostream& os)
{
	if (argc!=2) return BAD_NUM_ARGS;

	if (!gConfig.defines(argv[1])) {
		os << argv[1] << " is not in the table" << endl;
		return BAD_VALUE;
	}

	// TODO : removing of default values from DB disabled for now. Breaks webui.
	if (gConfig.keyDefinedInSchema(argv[1])) {
		if (!gConfig.set(argv[1],gConfig.mSchema[argv[1]].getDefaultValue())) {
			os << "DB ERROR: " << argv[1] << " could not be set back to the default value" << endl;
			return FAILURE;
		}

		os << argv[1] << " set back to its default value" << endl;
		vector<string> warnings = gConfig.crossCheck(argv[1]);
		vector<string>::iterator warning = warnings.begin();
		while (warning != warnings.end()) {
			os << "WARNING: " << *warning << endl;
			warning++;
		}
		if (gConfig.isStatic(argv[1])) {
			os << argv[1] << " is static; change takes effect on restart" << endl;
		}
		return SUCCESS;
	}

	if (!gConfig.remove(argv[1])) {
		os << "DB ERROR: " << argv[1] << " could not be removed from the configuration table" << endl;
		return FAILURE;
	}

	os << argv[1] << " removed from the configuration table" << endl;

	return SUCCESS;
}



/** Change the registration timers. */
static CLIStatus regperiod(int argc, char** argv, ostream& os)
{
	if (argc==1) {
		os << "T3212 is " << gConfig.getNum("GSM.Timer.T3212") << " minutes" << endl;
		os << "SIP registration period is " << gConfig.getNum("SIP.RegistrationPeriod") << " minutes" << endl;
		return SUCCESS;
	}

	if (argc>3) return BAD_NUM_ARGS;

	unsigned newT3212 = strtol(argv[1],NULL,10);
	if (!gConfig.isValidValue("GSM.Timer.T3212", argv[1])) {
		os << "valid T3212 range is 6..1530 minutes" << endl;
		return BAD_VALUE;
	}

	// By default, make SIP registration period 1.5x the GSM registration period.
	unsigned SIPRegPeriod = newT3212 * 1.5;
	char SIPRegPeriodStr[10];
	sprintf(SIPRegPeriodStr, "%u", SIPRegPeriod);
	if (argc==3) {
		SIPRegPeriod = strtol(argv[2],NULL,10);
		sprintf(SIPRegPeriodStr, "%s", argv[2]);
	}
	if (!gConfig.isValidValue("SIP.RegistrationPeriod", SIPRegPeriodStr)) {
		os << "valid SIP registration range is 6..2298 minutes" << endl;
		return BAD_VALUE;
	}

	// Set the values in the table and on the GSM beacon.
	gConfig.set("SIP.RegistrationPeriod",SIPRegPeriod);
	gConfig.set("GSM.Timer.T3212",newT3212);
	// Done.
	return SUCCESS;
}


/** Print the list of alarms kept by the logger, i.e. the last LOG(ALARM) << <text> */
static CLIStatus alarms(int argc, char** argv, ostream& os)
{
	std::ostream_iterator<std::string> output( os, "\n" );
	std::list<std::string> alarms = gGetLoggerAlarms();
	std::copy( alarms.begin(), alarms.end(), output );
	return SUCCESS;
}


/** Version string. */
static CLIStatus version(int argc, char **argv, ostream& os)
{
	if (argc!=1) return BAD_NUM_ARGS;
	os << gVersionString << endl;
	return SUCCESS;
}

/** Show start-up notices. */
static CLIStatus notices(int argc, char **argv, ostream& os)
{
	if (argc!=1) return BAD_NUM_ARGS;
	os << endl << gOpenBTSWelcome << endl;
	return SUCCESS;
}

static CLIStatus page(int argc, char **argv, ostream& os)
{
	if (argc==1) {
		Control::gMMLayer.printPages(os);
		return SUCCESS;
	}
	return BAD_NUM_ARGS;

	// (pat 7-30-2013)  I think David removed the page command because it did not work in rev 3,
	// but I had already added rev 4 code, and here it is in case someone wants to test it:

	if (argc!=3) return BAD_NUM_ARGS;
	char *IMSI = argv[1];
	if (strlen(IMSI)>15) {
		os << IMSI << " is not a valid IMSI" << endl;
		return BAD_VALUE;
	}
	// (pat) Implement pat by just sending an SMS.
	Control::FullMobileId msid(IMSI);
	Control::TranEntry *tran = Control::TranEntry::newMTSMS(
						NULL,	// No SIPDialog
						msid,
						GSM::L3CallingPartyBCDNumber("0"),
						string(""),			// message body
						string(""));		// messate content type
	Control::gMMLayer.mmAddMT(tran);
	return SUCCESS;
}


static CLIStatus testcall(int argc, char **argv, ostream& os)
{
	if (argc!=3) return BAD_NUM_ARGS;
	char *IMSI = argv[1];
	if (strlen(IMSI)!=15) {
		os << IMSI << " is not a valid IMSI" << endl;
		return BAD_VALUE;
	}
	FullMobileId msid(IMSI);
	TranEntry *tran = TranEntry::newMTC(
			NULL,	// No SIPDialog
			msid,
			GSM::L3CMServiceType::TestCall,
			string("0"));
			//GSM::L3CallingPartyBCDNumber("0"));
	Control::gMMLayer.mmAddMT(tran);		// start paging.
	return SUCCESS;
}


// Return the Transaction Id or zero is an invalid value.
static TranEntryId transactionId(const char *id)
{
	unsigned result;
	if (toupper(*id) == 'T') result = atoi(id+1);
	else result = atoi(id);
	return (TranEntryId) result;
}


static CLIStatus endcall(int argc, char **argv, ostream& os)
{
	if (argc!=2) return BAD_NUM_ARGS;
	TranEntryId tid = transactionId(argv[1]);
	if (tid == 0) {
		os << argv[1] << " is not a valid transaction id";
		return BAD_VALUE;
	}
	if (! gNewTransactionTable.ttTerminate(tid)) {
		os << argv[1] << " not found in transaction table";
		return BAD_VALUE;
	}
	return SUCCESS;
}


static void addGprsInfo(vector<string> &row, const GSM::L2LogicalChannel* chan)
{
	// "CN TN chan      transaction active recyc UPFER RSSI TXPWR TXTA DNLEV DNBER Neighbor Neighbor";
	row.clear();
	string empty("-");
	row.push_back(format("%d",chan->CN()));		// CN
	row.push_back(format("%d",chan->TN()));		// TN
	row.push_back(string("GPRS"));				// chan type
	row.push_back(empty);						// transaction
	row.push_back(string(chan->active()?"true":"false"));	// active
	row.push_back(string(chan->recyclable()?"true":"false"));	// recyclable
	// TODO: We could report gprs chan->PDCHCommon::FER() and we should keep gprs chan utilization per channel.
}

// Return a descriptive string of the current channel state.
static string chanState(const L2LogicalChannel *chan)
{
	if (! chan->active()) { return string("inactive"); }
	LAPDState lstate = chan->getLapdmState();
	if (lstate) {
		ostringstream ss; ss << lstate;
		return ss.str();
	} else {
		return string("active");
	}
}

// This allocates a ton of tiny strings.  It doesnt matter; this function is only called interactively.
static void addChanInfo(vector<string> &row, TranEntryList tids, const GSM::L2LogicalChannel* chan, const vector<string> &fields)
{
	row.clear();
	for (unsigned i = 0; i < fields.size(); i++) {
		string field = fields[i];
		MSPhysReportInfo *phys = NULL;
		GSM::L3MeasurementResults meas = chan->SACCH()->measurementResults();
		if (chan->SACCH()) { meas = chan->SACCH()->measurementResults(); }

		if (field == "CN") {
			row.push_back(format("%d",chan->CN()));
		} else if (field == "TN") {
			row.push_back(format("%d",chan->TN()));
		} else if (field == "chan") {
			ostringstream foo; foo << chan->typeAndOffset();
			row.push_back(foo.str());
		} else if (field == "transaction") {
			// Add all the transaction ids.
			string tranids;
			for (TranEntryList::iterator it = tids.begin(); it != tids.end(); it++) {
				if (tranids.size()) tranids += " ";
				tranids += format("T%d",*it);
			}
			row.push_back(tranids);

			if (chan->inUseByGPRS()) {
				row.push_back(string("GPRS"));
				// (pat) Thats all we can say about this channel, although we could show the aggregate FER of all MS using the channel.
				return;
			}
		} else if (field == "LAPDm") {
			row.push_back(chanState(chan));
		} else if (field == "recyc") {
			row.push_back(string(chan->recyclable()?"true":"false"));
		} else if (field == "FER") {
			row.push_back(format("%-5.2f",100.0*chan->FER()));
		} else if (field == "RSSI") {
			if (phys == 0) { phys = chan->getPhysInfo(); }
			row.push_back(format("%-4d",(int)round(phys->RSSI())));
		} else if (field == "TA") {	// The TA measured by the BTS.
			// (pat) This was requested by Mark to get a more accurate distance measure of the MS.
			if (phys == 0) { phys = chan->getPhysInfo(); }
			row.push_back(format("%-.1f",phys->timingError()));
		} else if (field == "TXPWR") {
			if (phys == 0) { phys = chan->getPhysInfo(); }
			row.push_back(format("%-5d",(int)round(phys->actualMSPower())));
		} else if (field == "TXTA") {	// The TA reported by the MS.
			if (phys == 0) { phys = chan->getPhysInfo(); }
			row.push_back(format("%-4d",(int)round(phys->actualMSTiming())));
		} else if (field == "DNLEV") {
			if (chan->SACCH() && meas.MEAS_VALID() == 0) {	// Yes, 0 means meas_valid.
				row.push_back(format("%-5d", meas.RXLEV_FULL_SERVING_CELL_dBm()));
			} else {
				row.push_back("");
			}
		} else if (field == "DNBER") {
			if (chan->SACCH() && meas.MEAS_VALID() == 0) {	// Yes, 0 means meas_valid.
				row.push_back(format("%-5.2f", 100.0*meas.RXQUAL_FULL_SERVING_CELL_BER()));
			} else {
				row.push_back("");
			}
		} else if (field == "Neighbor") {	// Print Neighbor ARFCN and Neighbor dBm
			if (chan->SACCH() && meas.NO_NCELL()) {
				unsigned CN = meas.BCCH_FREQ_NCELL(0);
				std::vector<unsigned> ARFCNList = gNeighborTable.ARFCNList();
				if (CN>=ARFCNList.size()) {
					LOG(NOTICE) << "BCCH index " << CN << " does not match ARFCN list of size " << ARFCNList.size();
					goto skipneighbors;
				}
				row.push_back(format("%-8u", ARFCNList[CN]));
				row.push_back(format("%-8d",meas.RXLEV_NCELL_dBm(0)));
			} else {
				skipneighbors:
				row.push_back("");
				row.push_back("");
			}
			i++;	// We printed both Neighbor fields so skip over.
			assert(fields[i] == "Neighbor");
		} else if (field == "IMSI") {
			row.push_back(chan->chanGetImsi(true));
		} else if (field == "Frames") {
			DecoderStats ds = chan->getDecoderStats();
			row.push_back(format("%d/%d/%d",ds.mStatBadFrames,ds.mStatStolenFrames,ds.mStatTotalFrames));
		} else if (field == "SNR") {
			DecoderStats ds = chan->getDecoderStats();
			row.push_back(format("%.3g",ds.mAveSNR));
		} else if (field == "BER") {
			DecoderStats ds = chan->getDecoderStats();
			row.push_back(format("%.3g",100.0 * ds.mAveBER));
		}
	}
}

	// old: "  active - true if the channel is or recently was in use;
static const char *chansHelp = "[-a -l -tab] -- report PHY status for active channels, or if -a all channels.\n"
    "  -l for longer listing, -tab for tab-separated output format\n"
	"  CN - Channel Number; TN - Timeslot Number; chan type - the dedicated channel type, or GPRS if reserved for Packet Services;\n"
	"  transaction id - One or more Layer 3 transactions running on this channel;\n"
	"  LAPDm state - The current acknowledged message state, if any, otherwise 'active' or 'inactive';\n"
	"  recyc - true if channel is recyclable, ie, can be reused now;\n"
	"  RSSI - Uplink signal level dB above noise floor measured by BTS, should be near config parameter GSM.Radio.RSSITarget;\n"
	"  SNR - Signal to Noise Ratio measured by BTS, higher is better, less than 10 is probably unusable;\n"
	"  BER - Bit Error Rate before decoding measured by BTS, as a percentage;\n"
	"  FER - voice frame loss rate as a percentage measured by BTS;\n"
	"  TA - Timing advance in symbol periods measured by the BTS;\n"
	"  TXPWR - Uplink transmit power dB reported by MS;\n"
	"  TXTA - Timing advance in symbol periods reported by MS;\n"
	"  DNLEV - Downlink signal level dB reported by MS;\n"
	"  DNBER - Downlink Bit Error rate percentage reported by MS;\n"
	"  Neighbor ARFCN and dBm - One of the neighbors channel and downlink RSSI reported by the MS;\n"
	"         may also be: 'no-MMContext' to indicate the layer2 channel is open but has not yet sent any layer3 messages;\n"
	"         or 'no-MMUser' to indicate that layer3 is connected but the IMSI is not yet known.\n"
	"  IMSI - International Mobile Subscriber Id of the MS on this channel, reported only if known;\n"
	"  Frames - number of bad, stolen, and total frames sent, only for traffic channels;\n"
	;

CLIStatus printChansV4(std::ostream& os,bool showAll, bool longList, bool tabSeparated)
{
	// The "active" field needs to be a big enough for the longest lapdm states here:
	//                                                 LinkEstablished
	//                                                 ContentionResolve  
	//                                                 AwaitingEstablish
	// The spacing in these headers no longer matters.
	const char *header1, *header2;

	// The spaces in these headers are removed later; they are there only to make the columns line up so
	// we can make sure we have the two column headers correct.
	if (longList) {
		header1 = "CN TN chan transaction LAPDm recyc RSSI SNR FER BER TA  TXPWR TXTA DNLEV DNBER IMSI Frames     Neighbor Neighbor";
		header2 = "_  _  type id          state _     dB   _   pct pct sym dBm   sym  dBm   pct   _    bad/st/tot ARFCN    dBm";
	} else {
		header1 = "CN TN chan transaction LAPDm recyc RSSI SNR FER BER TXPWR TXTA DNLEV DNBER IMSI";
		header2 = "_  _  type id          state _     dB   _   pct pct dBm   sym  dBm   pct   ";
		// This is the original version 4 list:
		//header1 = "CN TN chan transaction LAPDm recyc FER RSSI TXPWR TXTA DNLEV DNBER Neighbor Neighbor IMSI";
		//header2 = "_  _  type id          state _     pct dB   dBm   sym  dBm   pct    ARFCN    dBm";
	}

	LOG(DEBUG);
	prettyTable_t tab;
	vector<string> vh1, vh2;
	tab.push_back(stringSplit(vh1,header1));
	tab.push_back(stringSplit(vh2,header2));


	using namespace GSM;

	//gPhysStatus.dump(os);
	//os << endl << "Old data reporting: " << endl;

	L2ChanList chans;
	gBTS.getChanVector(chans);
	LOG(DEBUG);
	Control::TranEntryList tids;
	for (L2ChanList::iterator it = chans.begin(); it != chans.end(); it++) {
		const L2LogicalChannel *chan = *it;
		LOG(DEBUG);
		chan->getTranIds(tids);		// Get transactions using this channel.
		LOG(DEBUG);
		// It would be a bug to have a non-empty tids on an inactive channel, but in that case we really want to show it.
		if (chan->active() || ! tids.empty() || showAll) {
			vector<string> row;
			addChanInfo(row,tids,chan,vh1);
			tab.push_back(row);
		} else if (chan->inUseByGPRS() && showAll) {
			vector<string> row;
			addGprsInfo(row,chan);
			tab.push_back(row);
		}
	}
	printPrettyTable(tab,os,tabSeparated);
	return SUCCESS;
}

static CLIStatus chans(int argc, char **argv, ostream& os)
{
	// bool showAll = false;
	// if (argc==2) showAll = true;
	map<string,string> options = cliParse(argc,argv,os,"-a -l -tab");
	bool showAll = options.count("-a");
	bool longList = options.count("-l");
	bool tabSeparated = options.count("-tab");
	if (argc) return BAD_NUM_ARGS;
	printChansV4(os,showAll,longList,tabSeparated);
	return SUCCESS;
}

#if OLD_VERSION
void printChanInfo(unsigned transID, const GSM::L2LogicalChannel* chan, ostream& os)
{
	os << setw(2) << chan->CN() << " " << chan->TN();
	os << " " << setw(9) << chan->typeAndOffset();
	os << " " << setw(12) << transID;
	os << " " << setw(6) << chan->active();
	os << " " << setw(5) << chan->recyclable();
	char buffer[200];
	snprintf(buffer,199,"%5.2f %4d %5d %4d",
		100.0*chan->FER(), (int)round(chan->RSSI()),
		chan->actualMSPower(), chan->actualMSTiming());
	os << " " << buffer;

	if (!chan->SACCH()) {
		os << endl;
		return;
	}

	const GSM::L3MeasurementResults& meas = chan->SACCH()->measurementResults();

	if (meas.MEAS_VALID()) {
		os << endl;
		return;
	}

	snprintf(buffer,199,"%5d %5.2f",
		meas.RXLEV_FULL_SERVING_CELL_dBm(),
		100.0*meas.RXQUAL_FULL_SERVING_CELL_BER());
	os << " " << buffer;

	if (meas.NO_NCELL()==0) {
		os << endl;
		return;
	}
	unsigned CN = meas.BCCH_FREQ_NCELL(0);
	std::vector<unsigned> ARFCNList = gNeighborTable.ARFCNList();
	if (CN>=ARFCNList.size()) {
		LOG(NOTICE) << "BCCH index " << CN << " does not match ARFCN list of size " << ARFCNList.size();
		os << endl;
		return;
	}
	snprintf(buffer,199,"%8u %8d",ARFCNList[CN],meas.RXLEV_NCELL_dBm(0));
	os << " " << buffer;
	os << endl;
}


CLIStatus printChansV4(std::ostream& os,bool showAll)
{
	using namespace GSM;
	os << "CN TN chan      transaction active recyc UPFER RSSI TXPWR TXTA DNLEV DNBER Neighbor Neighbor" << endl;
	os << "CN TN type      id                       pct    dB   dBm  sym   dBm   pct    ARFCN    dBm" << endl;

	//gPhysStatus.dump(os);
	//os << endl << "Old data reporting: " << endl;

	L2ChanList chans;
	gBTS.getChanVector(chans);
	Control::TranEntryList tids;
	for (L2ChanList::iterator it = chans.begin(); it != chans.end(); it++) {
		const L2LogicalChannel *chan = *it;
		chan->getTranIds(tids);
		// It would be a bug to have a non-empty tids on an active channel, but in that case we really want to show it.
		if (chan->active() || ! tids.empty() || showAll) {
			// TODO: There could be multiple TIDs; we should print them all.
			printChanInfo(tids.empty()?0:tids.front(),chan,os);
		}
	}
	os << endl;
	return SUCCESS;
}

static CLIStatus chans(int argc, char **argv, ostream& os)
{
	bool showAll = false;
	if (argc==2) showAll = true;
	if (argc>2) return BAD_NUM_ARGS;
	printChansV4(os,showAll);
	return SUCCESS;
}
#endif




static CLIStatus power(int argc, char **argv, ostream& os)
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
	if (min>max) {
		os << "Min is larger than max" << endl;
		return BAD_VALUE;
	}

	if (!gConfig.isValidValue("GSM.Radio.PowerManager.MinAttenDB", argv[1])) {
		os << "Invalid new value for min.  It must be in range (";
		os << gConfig.mSchema["GSM.Radio.PowerManager.MinAttenDB"].getValidValues() << ")" << endl;
		return BAD_VALUE;
	}
	if (!gConfig.isValidValue("GSM.Radio.PowerManager.MaxAttenDB", argv[2])) {
		os << "Invalid new value for max.  It must be in range (";
		os << gConfig.mSchema["GSM.Radio.PowerManager.MaxAttenDB"].getValidValues() << ")" << endl;
		return BAD_VALUE;
	}

	gConfig.set("GSM.Radio.PowerManager.MinAttenDB",argv[1]);
	gConfig.set("GSM.Radio.PowerManager.MaxAttenDB",argv[2]);

	os << "new attenuation bounds "
		<< gConfig.getNum("GSM.Radio.PowerManager.MinAttenDB")
		<< " to "
		<< gConfig.getNum("GSM.Radio.PowerManager.MaxAttenDB")
		<< " dB" << endl;

	return SUCCESS;
}


static CLIStatus rxgain(int argc, char** argv, ostream& os)
{
        os << "current RX gain is " << gConfig.getNum("GSM.Radio.RxGain") << " dB" << endl;
        if (argc==1) return SUCCESS;
	if (argc!=2) return BAD_NUM_ARGS;

	if (!gConfig.isValidValue("GSM.Radio.RxGain", argv[1])) {
		os << "Invalid new value for RX gain.  It must be in range (";
		os << gConfig.mSchema["GSM.Radio.RxGain"].getValidValues() << ")" << endl;
		return BAD_VALUE;
	}

        int newGain = gTRX.ARFCN(0)->setRxGain(atoi(argv[1]));
        os << "new RX gain is " << newGain << " dB" << endl;
  
        gConfig.set("GSM.Radio.RxGain",newGain);

        return SUCCESS;
}

static CLIStatus txatten(int argc, char** argv, ostream& os)
{
        os << "current TX attenuation is " << gConfig.getNum("TRX.TxAttenOffset") << " dB" << endl;
        if (argc==1) return SUCCESS;
        if (argc!=2) return BAD_NUM_ARGS;

		if (!gConfig.isValidValue("TRX.TxAttenOffset", argv[1])) {
			os << "Invalid new value for TX attenuation.  It must be in range (";
			os << gConfig.mSchema["TRX.TxAttenOffset"].getValidValues() << ")" << endl;
			return BAD_VALUE;
		}

        int newAtten = gTRX.ARFCN(0)->setTxAtten(atoi(argv[1]));
        os << "new TX attenuation is " << newAtten << " dB" << endl;

        gConfig.set("TRX.TxAttenOffset",newAtten);

        return SUCCESS;
}


static CLIStatus freqcorr(int argc, char** argv, ostream& os)
{
        os << "current freq. offset is " << gConfig.getNum("TRX.RadioFrequencyOffset") << endl;
        if (argc==1) return SUCCESS;
        if (argc!=2) return BAD_NUM_ARGS;

		if (!gConfig.isValidValue("TRX.RadioFrequencyOffset", argv[1])) {
			os << "Invalid new value for freq. offset  It must be in range (";
			os << gConfig.mSchema["TRX.RadioFrequencyOffset"].getValidValues() << ")" << endl;
			return BAD_VALUE;
		}

        int newOffset = gTRX.ARFCN(0)->setFreqOffset(atoi(argv[1]));
        os << "new freq. offset is " << newOffset << endl;

        gConfig.set("TRX.RadioFrequencyOffset",newOffset);

        return SUCCESS;
}



static CLIStatus noise(int argc, char** argv, ostream& os)
{
	if (argc!=1) return BAD_NUM_ARGS;

	int noise = gTRX.ARFCN(0)->getNoiseLevel();
	int target = abs(gConfig.getNum("GSM.Radio.RSSITarget"));
	int diff = noise - target;

	os << "noise RSSI is -" << noise << " dB wrt full scale" << endl;
	os << "MS RSSI target is -" << target << " dB wrt full scale" << endl;
	if (diff <= 0) {
		os << "WARNING: the current noise level exceeds the MS RSSI target, uplink connectivity will be impossible." << endl;
	} else if (diff <= 10) {
		os << "WARNING: the current noise level is approaching the MS RSSI target, uplink connectivity will be extremely limited." << endl;
	} else {
		os << "INFO: the current noise level is acceptable." << endl;
	}

	return SUCCESS;
}

static CLIStatus sysinfo(int argc, char** argv, ostream& os)
{
        if (argc!=1) return BAD_NUM_ARGS;

	const GSM::L3SystemInformationType1 *SI1 = gBTS.SI1();
	if (SI1) os << *SI1 << endl;
	const GSM::L3SystemInformationType2 *SI2 = gBTS.SI2();
	if (SI2) os << *SI2 << endl;
	const GSM::L3SystemInformationType3 *SI3 = gBTS.SI3();
	if (SI3) os << *SI3 << endl;
	const GSM::L3SystemInformationType4 *SI4 = gBTS.SI4();
	if (SI4) os << *SI4 << endl;
	const GSM::L3SystemInformationType5 *SI5 = gBTS.SI5();
	if (SI5) os << *SI5 << endl;
	const GSM::L3SystemInformationType6 *SI6 = gBTS.SI6();
	if (SI6) os << *SI6 << endl;

	return SUCCESS;
}


static CLIStatus neighbors(int argc, char** argv, ostream& os)
{
	map<string,string> options = cliParse(argc,argv,os,"-tab");
	if (argc) return BAD_NUM_ARGS;
	bool tabSeparated = options.count("-tab");
	FILE *result = NULL;
	char cmd[200];
	snprintf(cmd,200,"sqlite3 -separator ' ' %s 'select IPADDRESS,C0,BSIC from neighbor_table'",
		gConfig.getStr("Peering.NeighborTable.Path").c_str());
	result = popen(cmd,"r");
#if 1		// pat was here.
	prettyTable_t tab;
	vector<string> vh;
	tab.push_back(stringSplit(vh,"host C0  BSIC")); vh.clear();
	tab.push_back(stringSplit(vh,"---- --- ----")); vh.clear();
	if (result) {
		char line[202];
		while (fgets(line, 200, result)) {
			tab.push_back(stringSplit(vh,line)); vh.clear();
		}
	}
	printPrettyTable(tab,os,tabSeparated);
#else
	os << "host C0 BSIC" << endl;
	char *line = (char*)malloc(200);
	while (!feof(result)) {
		if (!fgets(line, 200, result)) break;
		os << line;
	}
	free(line);
	os << endl;
#endif
	if (result) pclose(result);
	return SUCCESS;
}


static CLIStatus crashme(int argc, char** argv, ostream& os)
{
	char *nullp = 0x0;
	// we actually have to output this,
	// or the compiler will optimize it out
	os << *nullp;
	return FAILURE;
}


static CLIStatus stats(int argc, char** argv, ostream& os)
{
	char cmd[BUFSIZ];
	if (argc==2) {
		if (strcmp(argv[1],"clear")==0) {
				gReports.clear();
				os << "stats table (gReporting) cleared" << endl;
				return SUCCESS;
		}
		snprintf(cmd,sizeof(cmd)-1,
			"sqlite3 %s 'select name||\": \"||value||\" events over \"||((%lu-clearedtime)/60)||\" minutes\" from reporting where name like \"%%%s%%\";'",
			gConfig.getStr("Control.Reporting.StatsTable").c_str(),
			time(NULL), argv[1]);
	}
	else if (argc==1)
	{
		snprintf(cmd,sizeof(cmd)-1,"sqlite3 %s 'select name||\": \"||value||\" events over \"||((%lu-clearedtime)/60)||\" minutes\" from reporting;'",
			gConfig.getStr("Control.Reporting.StatsTable").c_str(), time(NULL));
	} else
	{
	    return BAD_NUM_ARGS;
	}
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


static CLIStatus memStat(int argc, char** argv, ostream& os)
{
	// These counters are always available.
	os << "Count"<<LOGVAR2("TranEntry",gCountTranEntry)<<LOGVAR2("SipDialog",gCountSipDialogs)
		<<LOGVAR2("RTPSessions",gCountRtpSessions) <<LOGVAR2("RTPSockets",gCountRtpSockets);
	// The counters printed by gMemStats are only available if we were compiled with the memory checker enabled.
	gMemStats.text(os);
	return SUCCESS;
}


//@} // CLI commands



void Parser::addCommands()
{
	addCommand("uptime", uptime, "-- show BTS uptime and BTS frame number.");
	addCommand("help", showHelp, "[command] -- list available commands or gets help on a specific command.");
	addCommand("shutdown", exit_function, "[wait] -- shut down or restart OpenBTS, either immediately, or waiting for existing calls to clear with a timeout in seconds");
	addCommand("tmsis", tmsis, tmsisHelp);
	addCommand("sendsms", sendsms, "IMSI src# message... -- send direct SMS to IMSI on this BTS, addressed from source number src#.");
	addCommand("sendsimple", sendsimple, "IMSI src# message... -- send SMS to IMSI via SIP interface, addressed from source number src#.");
	addCommand("load", printStats, "-- print the current activity loads.");
	addCommand("cellid", cellID, "[MCC MNC LAC CI] -- get/set location area identity (MCC, MNC, LAC) and cell ID (CI)");
	addCommand("calls", calls, "[-m | -a | -s | -t] -- print transaction table [or -m: mobility management tables or -s: SIP dialogs].  Note this includes both CS (voice call) and SMS transactions.  If -a specified with -t, show all transactions, else only active");
	addCommand("trans", transactions, "[purge] -- print-only or print-and-purge completed transaction table (tabular format)");
	addCommand("rawconfig", rawconfig, "[] OR [patt] OR [key val(s)] -- print the current configuration, print configuration values matching a pattern, or set/change a configuration value");
	addCommand("trxfactory", trxfactory, "-- print the radio's factory calibration and meta information");
	addCommand("audit", audit, "-- audit the current configuration for troubleshooting");
	addCommand("config", config, "[] OR [patt] OR [key val(s)] -- print the current configuration, print configuration values matching a pattern, or set/change a configuration value");
	addCommand("devconfig", devconfig, "[] OR [patt] OR [key val(s)] -- print the current configuration, print configuration values matching a pattern, or set/change a configuration value");
	addCommand("regperiod", regperiod, "[GSM] [SIP] -- get/set the registration period (GSM T3212), in MINUTES");
	addCommand("alarms", alarms, "-- show latest alarms");
	addCommand("version", version,"-- print the version string");
	addCommand("page", page, "print the paging table");
	addCommand("chans", chans, chansHelp);
	addCommand("power", power, "[minAtten maxAtten] -- report current attentuation or set min/max bounds");
        addCommand("rxgain", rxgain, "[newRxgain] -- get/set the RX gain in dB");
        addCommand("txatten", txatten, "[newTxAtten] -- get/set the TX attenuation in dB");
	addCommand("freqcorr", freqcorr, "[newOffset] -- get/set the new radio frequency offset");
        addCommand("noise", noise, "-- report receive noise level in RSSI dB");
	addCommand("rmconfig", rmconfig, "key -- set a configuration value back to its default or remove a custom key/value pair");
	addCommand("unconfig", unconfig, "key -- disable a configuration key by setting an empty value");
	addCommand("notices", notices, "-- show startup copyright and legal notices");
	addCommand("endcall", endcall,"trans# -- terminate the given transaction");
	//addCommand("testcall", testcall, "IMSI time -- initiate a TCHF test call to a given IMSI with a given paging time");
	addCommand("sysinfo", sysinfo, "-- print current system information messages");
	addCommand("neighbors", neighbors, "-- dump the neighbor table");
	addCommand("gprs", GPRS::gprsCLI,"GPRS mode sub-command.  Type: gprs help for more");
	addCommand("sgsn", SGSN::sgsnCLI,"SGSN mode sub-command.  Type: sgsn help for more");
	addCommand("crashme", crashme, "force crash of OpenBTS for testing purposes");
	addCommand("stats", stats,"[patt] OR clear -- print all, or selected, performance counters, OR clear all counters");
	addCommand("handover", handover,"imsi neighbor -- attempt handover to neighbor specified by ip address");
	addCommand("memstat", memStat, "-- internal testing command: print memory use stats");

}


};


// vim: ts=4 sw=4
