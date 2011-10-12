/**@file Global system parameters. */
/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* Copyright 2011 Range Networks, Inc.
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

/*
	This file keeps global system parameters.
*/

#ifndef GLOBALS_H
#define GLOBALS_H

#include <Configuration.h>
#include <CLI.h>
#include <PhysicalStatus.h>
#include <TMSITable.h>
#include <SubscriberRegistry.h>



/** Date-and-time string, defined in OpenBTS.cpp. */
extern const char* gDateTime;

/**
	Just about everything goes into the configuration table.
	This should be defined in the main body of the top-level application.
*/
extern ConfigurationTable gConfig;

/** The OpenBTS welcome message. */
extern const char* gOpenBTSWelcome;

/** The central parser. */
extern CommandLine::Parser gParser;

/** The global TMSI table. */
extern Control::TMSITable gTMSITable;

/** The physical status reporting table */
extern GSM::PhysicalStatus gPhysStatus;

/** The subscriber registry */
extern SubscriberRegistry gSubscriberRegistry;

#endif
