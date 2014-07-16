/*
* Copyright 2009, 2014 Free Software Foundation, Inc.
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

*/


#ifndef OPENBTSCLI_H
#define OPENBTSCLI_H

#include <string>
#include <map>
#include <iostream>


namespace CommandLine {
using std::string; using std::ostream;

enum CLIStatus { SUCCESS=0,
	BAD_NUM_ARGS=1,
	BAD_VALUE=2,
	NOT_FOUND=3,
	TOO_MANY_ARGS=4,
	FAILURE=5,
	CLI_EXIT=-1
	};


struct CLIParseError {
	string msg;
	CLIParseError(string wMsg) : msg(wMsg) {}
};


/** A table for matching strings to actions. */
typedef CLIStatus (*CLICommand)(int,char**,ostream&);
typedef std::map<std::string,CLICommand> ParseTable;

/** The help table. */
typedef std::map<std::string,std::string> HelpTable;

class Parser {

	private:

	ParseTable mParseTable;
	HelpTable mHelpTable;
	static const int mMaxArgs = 10;

	/**
		Parse and execute a command string.
		@line a writeable copy of the original line
		@cline the original line
		@os output stream
		@return status code
	*/
	CLIStatus execute(char* line, ostream& os) const;
	int Execute(bool console, const char cmdbuf[], int outfd);
	void Prompt() const;

	public:
	std::string mCommandName;	// Name of the running program, eg, "OpenBTS"

	void addCommands();

	/**
		Process a command line.
		@return 0 on sucess, -1 on exit request, error codes otherwise
	*/
	CLIStatus process(const char* line, ostream& os) const;
	void startCommandLine();	// (pat) Start a simple command line processor.

	/** Add a command to the parsing table. */
	void addCommand(const char* name, CLICommand func, const char* helpString)
		{ mParseTable[name] = func; mHelpTable[name]=helpString; }

	ParseTable::const_iterator begin() const { return mParseTable.begin(); }
	ParseTable::const_iterator end() const { return mParseTable.end(); }

	/** Return a help string. */
	const char *help(const std::string& cmd) const;
	void printHelp(ostream &os) const;		// print help for all commands into os.
	void cliServer();		// Run the command processor.

};

extern std::map<string,string> cliParse(int &argc, char **&argv, ostream &os, const char *optstring);

extern CLIStatus configCmd(string mode, int argc, char** argv, ostream& os);
extern CLIStatus unconfig(int argc, char** argv, ostream& os);
extern CLIStatus rmconfig(int argc, char** argv, ostream& os);
extern CLIStatus rawconfig(int argc, char** argv, ostream& os);

extern CLIStatus printChansV4(std::ostream& os,bool showAll, int verbosity = 0, bool tabSeparated = false);

} 	// namespace CommandLine


#endif
// vim: ts=4 sw=4
