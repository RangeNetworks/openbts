/**@file Global system parameters. */
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

#include "config.h"
#include <Globals.h>
#include <CLI.h>
#include <TMSITable.h>
#include <URLEncode.h>

#define PROD_CAT "P"

#define FEATURES "+GPRS "

const char *gVersionString = "release " VERSION FEATURES PROD_CAT " built " __DATE__ " rev" SVN_REV " ";

const char* gOpenBTSWelcome =
	//23456789123456789223456789323456789423456789523456789623456789723456789
	"OpenBTS\n"
	"Copyright 2008, 2009, 2010 Free Software Foundation, Inc.\n"
	"Copyright 2010 Kestrel Signal Processing, Inc.\n"
	"Copyright 2011, 2012, 2013 Range Networks, Inc.\n"
	"Release " VERSION " " PROD_CAT " formal build date " __DATE__ " rev" SVN_REV "\n"
	"\"OpenBTS\" is a registered trademark of Range Networks, Inc.\n"
	"\nContributors:\n"
	"  Range Networks, Inc.:\n"
	"    David Burgess, Harvind Samra, Donald Kirker, Doug Brown,\n"
	"    Pat Thompson, Kurtis Heimerl\n"
	"  Kestrel Signal Processing, Inc.:\n"
	"    David Burgess, Harvind Samra, Raffi Sevlian, Roshan Baliga\n"
	"  GNU Radio:\n"
	"    Johnathan Corgan\n"
	"  Others:\n"
	"    Anne Kwong, Jacob Appelbaum, Joshua Lackey, Alon Levy\n"
	"    Alexander Chemeris, Alberto Escudero-Pascual\n"
	"Incorporated L/GPL libraries and components:\n"
	"  libosip2, LGPL, 2.1 Copyright 2001-2007 Aymeric MOIZARD jack@atosc.org\n"
	"  libortp, LGPL, 2.1 Copyright 2001 Simon MORLAT simon.morlat@linphone.org\n"
	"  libusb, LGPL 2.1, various copyright holders, www.libusb.org\n"
	"Incorporated BSD/MIT-style libraries and components:\n"
	"  A5/1 Pedagogical Implementation, Simplified BSD License, Copyright 1998-1999 Marc Briceno, Ian Goldberg, and David Wagner\n"
	"Incorporated public domain libraries and components:\n"
	"  sqlite3, released to public domain 15 Sept 2001, www.sqlite.org\n"
	"\n"
	"\nThis program comes with ABSOLUTELY NO WARRANTY.\n"
	"\nUse of this software may be subject to other legal restrictions,\n"
	"including patent licensing and radio spectrum licensing.\n"
	"All users of this software are expected to comply with applicable\n"
	"regulations and laws.  See the LEGAL file in the source code for\n"
	"more information.\n"
	"\n"
;


CommandLine::Parser gParser;

GSM::Z100Timer watchdog;
Mutex watchdogLock;

void gResetWatchdog()
{
	watchdogLock.lock();
	watchdog.set(gConfig.getNum("Control.WatchdogMinutes")*60*1000);
	watchdogLock.unlock();
	LOG(DEBUG) << "reset watchdog timer, expires in " << watchdog.remaining() / 1000 << " seconds";
}

size_t gWatchdogRemaining()
{
	// Returns remaning time in seconds.
	ScopedLock lock(watchdogLock);
	return watchdog.remaining() / 1000;
}

bool gWatchdogExpired()
{
	ScopedLock lock(watchdogLock);
	return watchdog.expired();
}
