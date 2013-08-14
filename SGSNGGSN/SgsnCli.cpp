/*
* Copyright 2011 Range Networks, Inc.
* All Rights Reserved.
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

#include <list>
#include "Sgsn.h"
#include "Utils.h"
#include "Globals.h"
using namespace Utils;

struct CliError {
	string mMessage;
	CliError(string wMessage) : mMessage(wMessage) {}
};

struct CliErrorBadNumArgs : CliError {
	CliErrorBadNumArgs() : CliError("wrong number of arguments") {}
};

#define RN_CMD_OPTION(opt) (argi<argc && 0==strcmp(argv[argi],opt) ? ++argi : 0)
#define RN_CMD_ARG (argi<argc ? argv[argi++] : NULL)
//#define RN_CMD_OPTION(o) (argc>1 && 0==strncmp(argv[1],o,strlen(o)) ? argc--,argv++,1 : 0)
#define strmatch(what,pat) (0==strncmp(what,pat,strlen(pat)))

namespace SGSN {


static void sgsnCliFind(char *what, char *idstr, GmmInfo **pgmm, SgsnInfo **psi, ostream&os)
{
	*pgmm = 0;
	*psi = 0;
	if (what == NULL || idstr == NULL) {
		throw CliErrorBadNumArgs();
	}
	if (strmatch(what,"imsi")) {
		// The imsi bytevector is nibble packed.
		ByteVector bimsi(strlen(idstr)/2 + 3);	// Add some slop
		bimsi.setAppendP(0);
		for (char *cp = idstr; *cp; cp++) {
			if (*cp >= '0' && *cp <= '9') {
				bimsi.appendField(*cp - '0',4);
			} else {
				throw CliError(format("invalid imsi: %s",idstr));
			}
		}
		*pgmm = findGmmByImsi(bimsi,0);
		if (*pgmm == NULL) {
			throw CliError(format("SGSN record for imsi '%s' not found",idstr));
		}
	} else if (strmatch(what,"tlli")) {
		uint32_t tlli = strtoul(idstr,0,16);
		if (tlli == 0) {
			tlli = strtoul(idstr,0,10); // Try for 0x notation
		}
		if (tlli == 0) {
			throw CliError(format("Invalid tlli argument: %s",idstr));
		}
		*psi = findSgsnInfoByHandle(tlli,false);
		if (*psi == NULL) {
			throw CliError(format("SGSN record for tlli 0x%x not found.",tlli));
		}
	} else {
		throw CliError(format("unrecognized argument: %s",what));
	}
}

static void sgsnCliList(int argc, char **argv, int argi, ostream&os)
{
	char *what = RN_CMD_ARG;
	char *idstr = RN_CMD_ARG;
	if (!what) {
		gmmDump(os);	// List all
	} else {
		GmmInfo *gmm; SgsnInfo *si;
		sgsnCliFind(what,idstr,&gmm,&si,os);
		if (gmm) {
			gmmInfoDump(gmm,os,0);
		} else if (si && si->getGmm()) {
			gmmInfoDump(si->getGmm(),os,0);
		} else if (si) {
			sgsnInfoDump(si,os);
		}
	}
}

static void sgsnCliFree(int argc, char **argv, int argi, ostream&os)
{
	char *what = RN_CMD_ARG;
	char *idstr = RN_CMD_ARG;
	if (!idstr) throw CliErrorBadNumArgs();
	GmmInfo *gmm; SgsnInfo *si;
	sgsnCliFind(what,idstr,&gmm,&si,os);
	if (gmm) {
		cliGmmDelete(gmm);
	} else if (si && si->getGmm()) {
		cliGmmDelete(si->getGmm());
	} else if (si) {
		if (! cliSgsnInfoDelete(si)) {
			os << "Can not delete; it is in use.  Instead, delete the GMM context by imsi.";
		}
	}
}

static void sgsnCliHelp(int argc, char **argv, int argi, ostream&os);
static struct SgsnSubCmds {
	const char *name;
	void (*subcmd)(int argc, char **argv, int argi,std::ostream&os);
	const char *syntax;
} sgsnSubCmds[] = {
	{ "list",sgsnCliList, "list  [(imsi|tlli) id]  # list all or specified MS" },
	{ "free",sgsnCliFree, "free (imsi|tlli) id     # Delete something" },
	{ "help",sgsnCliHelp, "help                  # print this help" },
	//{ "stat",gprsStats, "stat  # Show GPRS statistics" },
	//{ "debug",gprsDebug,	"debug [level]  # Set debug level; 0 turns off" },
	//{ "stop",gprsStop,	"stop [-c]  # stop gprs thread and if -c release channels" },
	{ NULL,NULL }
};

static void sgsnCliHelp(int argc, char **argv, int argi, ostream&os)
{
	os << "sgsn sub-commands to control SGSN/GGSN sub-system.  Syntax: sgsn subcommand <options...>\n";
	os << "subcommands are:\n";
	struct SgsnSubCmds *gscp;
	for (gscp = sgsnSubCmds; gscp->name; gscp++) {
		os << "\t" << gscp->syntax;
		os << "\n";
	}
}


// For now, just do a list.
int sgsnCLI(int argc, char **argv, std::ostream &os)
{
	if (argc <= 1) { sgsnCliHelp(0,0,0,os); return 0; }
	int argi = 1;	// The number of arguments consumed so far; argv[0] was "sgsn"
	char *subcmd = argv[argi++];

	struct SgsnSubCmds *gscp;
	for (gscp = sgsnSubCmds; gscp->name; gscp++) {
		if (0 == strcasecmp(subcmd,gscp->name)) {
			try {
				gscp->subcmd(argc,argv,argi,os);
			} catch (CliError &e) {
				os << "sgsn:"<<e.mMessage <<"\n";
				return 0;	// We handled any error; dont let CLI print more warnings.
			}
			return 0;
		}
	}

	os << "sgsn: unrecognized sub-command: "<<subcmd<<"\n";
	return 2;	// bad command return - CLI will print a message to type 'sgsn help'.
}

};	// Namespace
