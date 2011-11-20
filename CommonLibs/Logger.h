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


#ifndef LOGGER_H
#define LOGGER_H

#include <syslog.h>
#include <stdint.h>
#include <stdio.h>
#include <sstream>
#include <list>
#include <map>
#include <string>
#include "Threads.h"


#define _LOG(level) \
	Log(LOG_##level).get() << pthread_self() \
	<< " " __FILE__  ":"  << __LINE__ << ":" << __FUNCTION__ << ": "

#ifdef NDEBUG
#define LOG(wLevel) \
	if (LOG_##wLevel!=LOG_DEBUG && gGetLoggingLevel(__FILE__)>=LOG_##wLevel) _LOG(wLevel)
#else
#define LOG(wLevel) \
	if (gGetLoggingLevel(__FILE__)>=LOG_##wLevel) _LOG(wLevel)
#endif


#define OBJLOG(wLevel) \
	LOG(wLevel) << "obj: " << this << ' '

#define LOG_ASSERT(x) { if (!(x)) LOG(EMERG) << "assertion " #x " failed"; } assert(x);


#define DEFAULT_MAX_ALARMS 10


/**
	A C++ stream-based thread-safe logger.
	Derived from Dr. Dobb's Sept. 2007 issue.
	Updated to use syslog.
	This object is NOT the global logger;
	every log record is an object of this class.
*/
class Log {

	public:

	protected:

	std::ostringstream mStream;		///< This is where we buffer up the log entry.
	int mPriority;					///< Priority of current repot.
	bool mDummyInit;

	public:

	Log(int wPriority)
		:mPriority(wPriority), mDummyInit(false)
	{ }

	Log(const char* name, const char* level=NULL, int facility=LOG_USER);

	// Most of the work is in the desctructor.
	/** The destructor actually generates the log entry. */
	~Log();

	std::ostringstream& get();
};



std::list<std::string> gGetLoggerAlarms();		///< Get a copy of the recent alarm list.


/**@ Global control and initialization of the logging system. */
//@{
/** Initialize the global logging system. */
void gLogInit(const char* name, const char* level=NULL, int facility=LOG_USER);
/** Get the logging level associated with a given file. */
int gGetLoggingLevel(const char *filename=NULL);
//@}


#endif

// vim: ts=4 sw=4
