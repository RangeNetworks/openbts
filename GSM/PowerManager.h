/*
* Copyright 2009 Free Software Foundation, Inc.
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/

#ifndef __POWER_CONTROL__
#define __POWER_CONTROL__

#include <iostream>

#include <Timeval.h>
#include <Threads.h>

// forward declaration
//class Timeval;

// Make it all inline for now - no change to automake

class ARFCNManager;


namespace GSM {


class PowerManager {

  private:
	ARFCNManager* mRadio;
	volatile int mAtten;	///< current attenuation.
	void pmSetAttenDirect(int atten);

  public:
	PowerManager() : mAtten(-9999) {}
	void pmStart();
	void pmSetAtten(int atten);
	int power() { return -mAtten; }
};

extern PowerManager gPowerManager;


}	// namespace GSM


#endif // __POWER_CONTROL__

// vim: ts=4 sw=4
