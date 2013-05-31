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

#include <unistd.h>		// For usleep
#include <sys/time.h>	// For gettimeofday
#include <stdio.h>		// For vsnprintf
#include <ostream>		// For ostream
#include <sstream>		// For ostringstream
#include <string.h>		// For strcpy
//#include "GSMCommon.h"
#include "Utils.h"
#include "MemoryLeak.h"

namespace Utils {

MemStats gMemStats;
int gMemLeakDebug = 0;

MemStats::MemStats()
{
	memset(mMemNow,0,sizeof(mMemNow));
	memset(mMemTotal,0,sizeof(mMemTotal));
	memset(mMemName,0,sizeof(mMemName));
}

void MemStats::text(std::ostream &os)
{
	os << "Structs current total:\n";
	for (int i = 0; i < mMax; i++) {
		os << "\t" << (mMemName[i] ? mMemName[i] : "unknown") << " " << mMemNow[i] << " " << mMemTotal[i] << "\n";
	}
}

void MemStats::memChkNew(MemoryNames memIndex, const char *id)
{
	/*std::cout << "new " #type "\n";*/
	mMemNow[memIndex]++;
	mMemTotal[memIndex]++;
	mMemName[memIndex] = id;
}

void MemStats::memChkDel(MemoryNames memIndex, const char *id)
{
	/*std::cout << "del " #type "\n";*/
	mMemNow[memIndex]--;
	if (mMemNow[memIndex] < 0) {
		LOG(ERR) << "Memory underflow on type "<<id;
		if (gMemLeakDebug) assert(0);
		mMemNow[memIndex] += 100;	// Prevent another message for a while.
	}
}

std::ostream& operator<<(std::ostream& os, std::ostringstream& ss)
{
	return os << ss.str();
}

std::ostream &osprintf(std::ostream &os, const char *fmt, ...)
{
	va_list ap;
	char buf[300];
	va_start(ap,fmt);
	int n = vsnprintf(buf,300,fmt,ap);
	va_end(ap);
	if (n >= (300-4)) { strcpy(&buf[(300-4)],"..."); }
	os << buf;
	return os;
}

std::string format(const char *fmt, ...)
{
	va_list ap;
	char buf[300];
	va_start(ap,fmt);
	int n = vsnprintf(buf,300,fmt,ap);
	va_end(ap);
	if (n >= (300-4)) { strcpy(&buf[(300-4)],"..."); }
	return std::string(buf);
}

// Return time in seconds with high resolution.
// Note: In the past I found this to be a surprisingly expensive system call in linux.
double timef()
{
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_usec / 1000000.0 + tv.tv_sec;
}

const std::string timestr()
{
	struct timeval tv;
	struct tm tm;
	gettimeofday(&tv,NULL);
	localtime_r(&tv.tv_sec,&tm);
	unsigned tenths = tv.tv_usec / 100000;	// Rounding down is ok.
	return format(" %02d:%02d:%02d.%1d",tm.tm_hour,tm.tm_min,tm.tm_sec,tenths);
}

// High resolution sleep for the specified time.
// Return FALSE if time is already past.
void sleepf(double howlong)
{
	if (howlong <= 0.00001) return;		// Less than 10 usecs, forget it.
	usleep((useconds_t) (1000000.0 * howlong));
}

//bool sleepuntil(double untilwhen)
//{
	//double now = timef();
	//double howlong = untilwhen - now;		// Fractional time in seconds.
	// We are not worrying about overflow because all times should be in the near future.
	//if (howlong <= 0.00001) return false;		// Less than 10 usecs, forget it.
	//sleepf(sleeptime);
//}

std::string Text2Str::str() const
{
	std::ostringstream ss;
	text(ss);
	return ss.str();
}

std::ostream& operator<<(std::ostream& os, const Text2Str *val)
{
	std::ostringstream ss;
	if (val) {
		val->text(ss);
		os << ss.str(); 
	} else {
		os << "(null)";
	}
	return os;
}

// Greatest Common Denominator.
// This is by Doug Brown.
int gcd(int x, int y)
{
	if (x > y) {
		return x % y == 0 ? y : gcd(y, x % y);
	} else {
		return y % x == 0 ? x : gcd(x, y % x);
	}
}


// Split a C string into an argc,argv array in place; the input string is modified.
// Returns argc, and places results in argv, up to maxargc elements.
// The final argv receives the rest of the input string from maxargc on,
// even if it contains additional splitchars.
// The correct idiom for use is to make a copy of your string, like this:
// char *copy = strcpy((char*)alloca(the_string.length()+1),the_string.c_str());
// char *argv[2];
// int argc = cstrSplit(copy,argv,2,NULL);
// If you want to detect the error of too many arguments, add 1 to argv, like this:
// char *argv[3];
// int argc = cstrSplit(copy,argv,3,NULL);
// if (argc == 3) { error("too many arguments"; }
int cstrSplit(char *in, char **pargv,int maxargc, const char *splitchars)
{
	if (splitchars == NULL) { splitchars = " \t\r\n"; }	// Default is any space.
	int argc = 0;
	while (argc < maxargc) {
		while (*in && strchr(splitchars,*in)) {in++;}	// scan past any splitchars
		if (! *in) return argc;					// return if finished.
		pargv[argc++] = in;						// save ptr to start of arg.
		in = strpbrk(in,splitchars);			// go to end of arg.
		if (!in) return	argc;					// return if finished.
		*in++ = 0;								// zero terminate this arg.
	}
	return argc;
}

std::ostream& operator<<(std::ostream& os, const Statistic<int> &stat) { stat.text(os); return os; }
std::ostream& operator<<(std::ostream& os, const Statistic<unsigned> &stat) { stat.text(os); return os; }
std::ostream& operator<<(std::ostream& os, const Statistic<float> &stat) { stat.text(os); return os; }
std::ostream& operator<<(std::ostream& os, const Statistic<double> &stat) { stat.text(os); return os; }

std::string replaceAll(const std::string input, const std::string search, const std::string replace)
{
	std::string output = input;
 	int index = 0;

	while (true) {
		index = output.find(search, index);
		if (index == std::string::npos) {
			break;
		}

		output.replace(index, replace.length(), replace);
		index += replace.length();
	}

	return output;
}

};
