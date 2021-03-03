/**@file Global system parameters. */
/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011-2021 Range Networks, Inc.
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

#include "config.h"
#include <Globals.h>
#include <CLI.h>
#include <TMSITable.h>
#include <URLEncode.h>

#define PROD_CAT "P"

#define FEATURES "+GPRS "

const char *gVERSION(void) { return VERSION; }
const char *gFEATURES(void) { return FEATURES; }
const char *gPROD_CAT(void) { return PROD_CAT; }

CommandLine::Parser gParser;

static Timeval watchdog;
static bool watchdogActive = false;
static Mutex watchdogLock;

void gResetWatchdog()
{
	ScopedLock lock(watchdogLock);
	int value = gConfig.getNum("Control.WatchdogMinutes");
	if (value <= 0) { watchdogActive = false; }	// The stupid timer core-dumps if you call reset with a 0 value.
	else { watchdog.future(value*60*1000); }
	LOG(DEBUG) << "reset watchdog timer, expires in " << watchdog.remaining() / 1000 << " seconds";
}

size_t gWatchdogRemaining()
{
	// Returns remaning time in seconds.
	ScopedLock lock(watchdogLock);
	return watchdogActive ? watchdog.remaining() / 1000 : 0;
}

bool gWatchdogExpired()
{
	ScopedLock lock(watchdogLock);
	return watchdogActive ? watchdog.passed() : false;
}
