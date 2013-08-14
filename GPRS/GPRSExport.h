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

// (pat) This is the GPRS exported include for use by clients in other directories.

#ifndef GPRSEXPORT_H
#define GPRSEXPORT_H
#include <ostream>
// The user of this file must include these first, to avoid circular .h files:
//#include "GSMConfig.h"		// For Time
//#include "GSMCommon.h"		// For ChannelType

// You must not include anything from the GSM directory to avoid circular calls
// that read files out of order, but we need transparent pointers to these classes,
// so they must be defined first.
namespace GSM {
	class RxBurst;
	class L3RRMessage;
	class CCCHLogicalChannel;
	class L3RequestReference;
	class Time;
};

namespace GPRS {

struct GPRSConfig {
	static unsigned GetRAColour();
	static bool IsEnabled();
	static bool sgsnIsInternal();
};

enum ChannelCodingType {	// Compression/Coding schemes CS-1 to CS-4 coded as 0-3
	ChannelCodingCS1,
	ChannelCodingCS2,
	ChannelCodingCS3,
	ChannelCodingCS4,
	ChannelCodingMax = ChannelCodingCS4,
};

// See notes at GPRSCellOptions_t::GPRSCellOptions_t()
struct GPRSCellOptions_t {
	unsigned mNMO;
    unsigned mT3168Code;        // range 0..7
    unsigned mT3192Code;        // range 0..7
    unsigned mDRX_TIMER_MAX;
    unsigned mACCESS_BURST_TYPE;
    unsigned mCONTROL_ACK_TYPE;
    unsigned mBS_CV_MAX;
	bool mNW_EXT_UTBF;	// Extended uplink TBF 44.060 9.3.1b and 9.3.1.3
	GPRSCellOptions_t();
};

extern const int GPRSUSFEncoding[8];

extern GPRSCellOptions_t &GPRSGetCellOptions();

// The following are not in a class because we dont want to include the entire GPRS class hierarchy.

// The function by which bursts are delivered to GPRS.
class PDCHL1FEC;
extern void GPRSWriteLowSideRx(const GSM::RxBurst&, PDCHL1FEC*);


// The function by which RACH messages are delivered to GPRS.
extern void GPRSProcessRACH(unsigned RA, const GSM::Time &when, float RSSI, float timingError);

extern int GetPowerAlpha();
extern int GetPowerGamma();
extern unsigned GPRSDebug;
extern void GPRSSetDebug(int value);
extern void GPRSNotifyGsmActivity(const char *imsi);

// Hook into CLI/CLI.cpp:Parser class for GPRS sub-command.
int gprsCLI(int,char**,std::ostream&);
int configGprsChannelsMin();

void gprsStart();	// External entry point to start gprs service.

}; // namespace GPRS

// GPRSLOG is no longer used outside the GPRS directory.
/****
 * #ifndef GPRSLOG
 * #include "Logger.h"
 * #define GPRSLOG(level) if (GPRS::GPRSDebug & (level)) \
 *	Log(LOG_DEBUG).get() <<"GPRS,"<<(level)<<":"
 * #endif
***/

#endif
