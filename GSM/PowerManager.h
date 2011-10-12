/*
* Copyright 2009 Free Software Foundation, Inc.
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

	Thread mThread;
	ARFCNManager* mRadio;
	unsigned mAveragedT3122;	///< averaged over the last NumSamples taken SamplePeriod apart kept in mSamples
	volatile unsigned mAtten;	///< current attenuation - either set by ourselves or by setPower
	Timeval  mLast;				///< when controller operated last time
	unsigned mSamples[100];
	unsigned  mNextSampleIndex;



	void increasePower();
	void reducePower();

	// internal method, does the control step
	void internalControlStep();

	void sampleT3122();

	void serviceLoop();

public:

	PowerManager();

	void start();

	int power() { return -mAtten; }

	friend void* PowerManagerServiceLoopAdapter(PowerManager *pm);

};


void *PowerManagerServiceLoopAdapter(PowerManager *pm);



}	// namespace GSM


#endif // __POWER_CONTROL__

// vim: ts=4 sw=4
