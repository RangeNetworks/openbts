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

#include "PowerManager.h"
#include <Logger.h>
#include <Globals.h>
#include <GSMConfig.h>
#include <TRXManager.h>
#include <ControlCommon.h>

extern TransceiverManager gTRX;

using namespace GSM;



void PowerManager::increasePower()
{
	int maxAtten = gConfig.getNum("GSM.Radio.PowerManager.MaxAttenDB");
	int minAtten = gConfig.getNum("GSM.Radio.PowerManager.MinAttenDB");
	if (mAtten==minAtten) {
		LOG(DEBUG) << "power already at maximum";
		return;
	}
	mAtten--;	// raise power by reducing attenuation
	if (mAtten<minAtten) mAtten=minAtten;
	if (mAtten>maxAtten) mAtten=maxAtten;
	LOG(INFO) << "power increased to -" << mAtten << " dB";
	mRadio->setPower(mAtten);
}

void PowerManager::reducePower()
{
	int maxAtten = gConfig.getNum("GSM.Radio.PowerManager.MaxAttenDB");
	int minAtten = gConfig.getNum("GSM.Radio.PowerManager.MinAttenDB");
	if (mAtten==maxAtten) {
		LOG(DEBUG) << "power already at minimum";
		return;
	}
	mAtten++; // reduce power be increasing attenuation
	if (mAtten<minAtten) mAtten=minAtten;
	if (mAtten>maxAtten) mAtten=maxAtten;
	LOG(INFO) << "power decreased to -" << mAtten << " dB";
	mRadio->setPower(mAtten);
}


// internal method, does the control step
void PowerManager::internalControlStep()
{
	unsigned target = gConfig.getNum("GSM.Radio.PowerManager.TargetT3122");
	LOG(DEBUG) << "Avg T3122 " << mAveragedT3122 << ", target " << target;
	// Adapt the power.
	if (mAveragedT3122 > target) reducePower();
	else increasePower();
}


void PowerManager::sampleT3122()
{
	// Tweak it down a little just in case there's no activity.
	mSamples[mNextSampleIndex] = gBTS.shrinkT3122();
	unsigned numSamples = gConfig.getNum("GSM.Radio.PowerManager.NumSamples");
	mNextSampleIndex = (mNextSampleIndex + 1) % numSamples;
	long sum = 0;
	for (unsigned i=0; i<numSamples; i++) sum += mSamples[i];
	mAveragedT3122 = sum / numSamples;
}


PowerManager::PowerManager()
	: mAveragedT3122(gConfig.getNum("GSM.Radio.PowerManager.TargetT3122"))
	, mAtten(gConfig.getNum("GSM.Radio.PowerManager.MaxAttenDB"))
	, mNextSampleIndex(0)
{
	// We don't actually set any power here, since the radio may not exist yet.
	assert(gConfig.getNum("GSM.Radio.PowerManager.NumSamples")<100);
	bzero(mSamples,sizeof(mSamples));
	LOG(INFO) << "setting initial power to -" << mAtten << " dB";
}



// This is called to actually do the control
void PowerManager::serviceLoop()
{
	while (1) {
		usleep(1000*gConfig.getNum("GSM.Radio.PowerManager.SamplePeriod"));
		sampleT3122();
		if (mLast.elapsed() < gConfig.getNum("GSM.Radio.PowerManager.Period")) continue;
		internalControlStep();
		mLast.now();
	}
}


void* GSM::PowerManagerServiceLoopAdapter(PowerManager *pm)
{
	pm->serviceLoop();
	return NULL;
}


void PowerManager::start()
{
	mRadio = gTRX.ARFCN(0);
	mRadio->setPower(mAtten);
	mThread.start((void*(*)(void*))PowerManagerServiceLoopAdapter,this);
}


// vim: ts=4 sw=4
