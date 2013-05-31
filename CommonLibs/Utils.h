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

#ifndef GPRSUTILS_H
#define GPRSUTILS_H
#include <stdint.h>
#include <stdarg.h>
#include <string>
#include <string.h>
#include <math.h>		// for sqrtf
#include "Logger.h"


namespace Utils {

extern double timef();					// high resolution time
extern const std::string timestr();		// A timestamp to print in messages.
extern void sleepf(double howlong);	// high resolution sleep
extern int gcd(int x, int y);

// It is irritating to create a string just to interface to the brain-damaged
// C++ stream class, but this is only used for debug messages.
std::string format(const char *fmt, ...) __attribute__((format (printf,1,2)));

int cstrSplit(char *in, char **pargv,int maxargc, const char *splitchars=NULL);

// For classes with a text() function, provide a function to return a String,
// and also a standard << stream function that takes a pointer to the object.
// We dont provide the function that takes a reference to the object
// because it is too highly overloaded and generally doesnt work.
class Text2Str {
	public:
	virtual void text(std::ostream &os) const = 0;
	std::string str() const;
};
std::ostream& operator<<(std::ostream& os, const Text2Str *val);

#if 0
// Generic Activity Timer.  Lots of controls to make everybody happy.
class ATimer {
	double mStart;
	//bool mActive;
	double mLimitTime;
	public:
	ATimer() : mStart(0), mLimitTime(0) { }
	ATimer(double wLimitTime) : mStart(0), mLimitTime(wLimitTime) { }
	void start() { mStart=timef(); }
	void stop() { mStart=0; }
	bool active() { return !!mStart; }
	double elapsed() { return timef() - mStart; }
	bool expired() { return elapsed() > mLimitTime; }
};
#endif


struct BitSet {
	unsigned mBits;
	void setBit(unsigned whichbit) { mBits |= 1<<whichbit; }
	void clearBit(unsigned whichbit) { mBits &= ~(1<<whichbit); }
	unsigned getBit(unsigned whichbit) const { return mBits & (1<<whichbit); }
	bool isSet(unsigned whichbit) const { return mBits & (1<<whichbit); }
	unsigned bits() const { return mBits; }
	operator int(void) const { return mBits; }
	BitSet() { mBits = 0; }
};

// Store current, min, max and compute running average and standard deviation.
template<class Type> struct Statistic {
	Type mCurrent, mMin, mMax;		// min,max optional initialization so you can print before adding any values.
	unsigned mCnt;
	double mSum;
	//double mSum2;	// sum of squares.
	// (Type) cast needed in case Type is an enum, stupid language.
	Statistic() : mCurrent((Type)0), mMin((Type)0), mMax((Type)0), mCnt(0), mSum(0) /*,mSum2(0)*/ {}
	// Set the current value and add a statisical point.
	void addPoint(Type val) {
		mCurrent = val;
		if (mCnt == 0 || val < mMin) {mMin = val;}
		if (mCnt == 0 || val > mMax) {mMax = val;}
		mCnt++;
		mSum += val;
		//mSum2 += val * val;
	}
	Type getCurrent() const {	// Return current value.
		return mCnt ? mCurrent : 0;
	}
	double getAvg() const { 			// Return average.
		return mCnt==0 ? 0 : mSum/mCnt; 
	};
	//float getSD() const { 	// Return standard deviation.  Use low precision square root function.
	//	return mCnt==0 ? 0 : sqrtf(mCnt * mSum2 - mSum*mSum) / mCnt;
	//}

	void text(std::ostream &os) const {	// Print everything in parens.
		os << "("<<mCurrent;
		if (mMin != mMax) {	// Not point in printing all this stuff if min == max.
			os <<LOGVAR2("min",mMin)<<LOGVAR2("max",mMax)<<LOGVAR2("avg",getAvg());
			if (mCnt <= 999999) {
				os <<LOGVAR2("N",mCnt);
			} else { // Shorten this up:
				char buf[10], *ep;
				sprintf(buf,"%.3g",round(mCnt));
				if ((ep = strchr(buf,'e')) && ep[1] == '+') { strcpy(ep+1,ep+2); }
				os << LOGVAR2("N",buf);
			}
			// os<<LOGVAR2("sd",getSD())  standard deviation not interesting
		}
		os << ")";
		// " min="<<mMin <<" max="<<mMax <<format(" avg=%4g sd=%3g)",getAvg(),getSD());
	}
	// Not sure if this works:
	//std::string statStr() const {
	//	return (std::string)mCurrent + " min=" + (std::string) mMin +" max="+(string)mMax+ format(" avg=%4g sd=%3g",getAvg(),getSD());
	//}
};

// This I/O mechanism is so dumb:
std::ostream& operator<<(std::ostream& os, const Statistic<int> &stat);
std::ostream& operator<<(std::ostream& os, const Statistic<unsigned> &stat);
std::ostream& operator<<(std::ostream& os, const Statistic<float> &stat);
std::ostream& operator<<(std::ostream& os, const Statistic<double> &stat);


// Yes, they botched and left this out:
std::ostream& operator<<(std::ostream& os, std::ostringstream& ss);

std::ostream &osprintf(std::ostream &os, const char *fmt, ...) __attribute__((format (printf,2,3)));

std::string replaceAll(const std::string input, const std::string search, const std::string replace);

};	// namespace

using namespace Utils;

#endif
