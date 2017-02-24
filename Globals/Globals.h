/**@file Global system parameters. */
/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* Copyright 2011, 2014 Range Networks, Inc.
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

/*
	This file keeps global system parameters.
*/

#ifndef GLOBALS_H
#define GLOBALS_H

#if !defined(BUILD_CLI)
#include <GSMCommon.h>
#include <OpenBTSConfig.h>
#include <CLI.h>
#include <PhysicalStatus.h>
//#include <TMSITable.h>
#include <TRXManager.h>
#include <Reporting.h>

#ifndef RN_DEVERLOPER_MODE
#define RN_DEVERLOPER_MODE 0
#endif

#ifndef RN_DISABLE_MEMORY_LEAK_TEST
#define RN_DISABLE_MEMORY_LEAK_TEST 1
#endif

namespace GPRS { extern unsigned GPRSDebug; }

/** Date-and-time string, defined in OpenBTS.cpp. */
extern const char* gDateTime;

/**
	Just about everything goes into the configuration table.
	This should be defined in the main body of the top-level application.
*/
extern OpenBTSConfig gConfig;
#endif // !defined(BUILD_CLI)

/** The OpenBTS welcome message. */
extern const char* gOpenBTSWelcome;

/** The OpenBTS version string. */
extern const char *gVersionString;

#if !defined(BUILD_CLI)
/** The central parser. */
extern CommandLine::Parser gParser;

/** The global TMSI table. */
//extern Control::TMSITable gTMSITable;

/** The physical status reporting table */
extern GSM::PhysicalStatus gPhysStatus;

/** The global transceiver interface. */
extern TransceiverManager gTRX;

/** A global watchdog timer. */
void gResetWatchdog();
size_t gWatchdogRemaining();
bool gWatchdogExpired();

extern ReportingTable gReports;

extern const char *gVERSION(void);
extern const char *gFEATURES(void);
extern const char *gPROD_CAT(void);
#endif // !defined(BUILD_CLI)
#endif
