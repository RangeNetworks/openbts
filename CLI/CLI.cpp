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

#include <stdlib.h>
#include <string>
#include <map>
#include <iostream>

#include <Globals.h>
#include <Logger.h>
#include "CLI.h"

namespace CommandLine {

using std::map; using std::string; using std::ostream;

/** Standard responses in the CLI, much mach erorrCode enum. */
static const char* standardResponses[] = {
	"success", // 0
	"wrong number of arguments", // 1
	"bad argument(s)", // 2
	"command not found", // 3
	"too many arguments for parser", // 4
	"command failed", // 5
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
	LOG(INFO) << "CLI executing command:" <<(line?line:"null");
	CLIStatus retVal = execute(newLine,os);
	free(newLine);
	if (retVal < CLI_EXIT || retVal > FAILURE) {
		os << "Unrecognized CLI command exit status: "<<retVal << endl;
	} else if (retVal != SUCCESS) {
		os << standardResponses[retVal] << endl;
	}
	return retVal;
}


// (pat) This is no longer used - see CLIServer.
static void *commandLineFunc(Parser *parser)
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
					if (parser->process(inbuf, cout, cin)<0) {
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
			// The parser returns -1 on exit.
			if (parser->process(inbuf,cout)<0) break;
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
	commandLineThread.start( (void*(*)(void*)) commandLineFunc,this);
}


const char * Parser::help(const string& cmd) const
{
	HelpTable::const_iterator hp = mHelpTable.find(cmd);
	if (hp==mHelpTable.end()) return "no help available";
	return hp->second.c_str();
}

void Parser::printHelp(ostream &os) const
{
	ParseTable::const_iterator cp = this->begin();
	os << endl << "Type \"help\" followed by the command name for help on that command." << endl << endl;
	int c=0;
	const int cols = 3;
	while (cp != this->end()) {
		const string& wd = cp->first;
		os << wd << '\t';
		if (wd.size()<8) os << '\t';
		++cp;
		c++;
		if (c%cols==0) os << endl;
	}
	if (c%cols!=0) os << endl;
}


// Parse options in optstring out of argc,argv.
// The optstring is a space separated list of options.  The options need not start with '-'.  To recognize just "-" or "--" just add it in.
// Return a map containing the options found; if option in optstring was followed by ':', map value will be the next argv argument, otherwise "true".
// Leave argc,argv pointing at the first argument after the options, ie, on return argc is the number of non-option arguments remaining in argv.
// This routine does not allow combining options, ie, -a -b != -ab
map<string,string> cliParse(int &argc, char **&argv, ostream &os, const char *optstring)
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

};
