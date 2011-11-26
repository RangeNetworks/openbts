/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
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

#include "radioInterface.h"
#include <Logger.h>

bool started = false;

RadioInterface::RadioInterface(RadioDevice *wRadio,
			       int wReceiveOffset,
			       int wRadioOversampling,
			       int wTransceiverOversampling,
			       GSM::Time wStartTime)
  : underrun(false), sendCursor(0), rcvCursor(0), mOn(false),
    mRadio(wRadio), receiveOffset(wReceiveOffset),
    samplesPerSymbol(wRadioOversampling), powerScaling(1.0),
    loadTest(false)
{
  mClock.set(wStartTime);
}


RadioInterface::~RadioInterface(void) {
  if (rcvBuffer!=NULL) delete rcvBuffer;
  //mReceiveFIFO.clear();
}

double RadioInterface::fullScaleInputValue(void) {
  return mRadio->fullScaleInputValue();
}

double RadioInterface::fullScaleOutputValue(void) {
  return mRadio->fullScaleOutputValue();
}


void RadioInterface::setPowerAttenuation(double atten)
{
  double rfGain, digAtten;

  rfGain = mRadio->setTxGain(mRadio->maxTxGain() - atten);
  digAtten = atten - mRadio->maxTxGain() + rfGain;

  if (digAtten < 1.0)
    powerScaling = 1.0;
  else
    powerScaling = 1.0/sqrt(pow(10, (digAtten/10.0)));
}

int RadioInterface::radioifyVector(signalVector &wVector,
				   float *retVector,
				   float scale,
				   bool zero)
{
  int i;
  signalVector::iterator itr = wVector.begin();

  if (zero) {
    memset(retVector, 0, wVector.size() * 2 * sizeof(float));
    return wVector.size();
  }

  for (i = 0; i < wVector.size(); i++) {
    retVector[2 * i + 0] = itr->real() * scale;
    retVector[2 * i + 1] = itr->imag() * scale;
    itr++;
  }

  return wVector.size();
}

int RadioInterface::unRadioifyVector(float *floatVector,
				     signalVector& newVector)
{
  int i;
  signalVector::iterator itr = newVector.begin();

  for (i = 0; i < newVector.size(); i++) {
    *itr++ = Complex<float>(floatVector[2 * i + 0],
			    floatVector[2 * i + 1]);
  }

  return newVector.size();
}

bool RadioInterface::tuneTx(double freq)
{
  return mRadio->setTxFreq(freq);
}

bool RadioInterface::tuneRx(double freq)
{
  return mRadio->setRxFreq(freq);
}


void RadioInterface::start()
{
  LOG(INFO) << "starting radio interface...";
  mAlignRadioServiceLoopThread.start((void * (*)(void*))AlignRadioServiceLoopAdapter,
                                     (void*)this);
  writeTimestamp = mRadio->initialWriteTimestamp();
  readTimestamp = mRadio->initialReadTimestamp();
  mRadio->start(); 
  LOG(DEBUG) << "Radio started";
  mRadio->updateAlignment(writeTimestamp-10000); 
  mRadio->updateAlignment(writeTimestamp-10000);

  sendBuffer = new float[2*2*INCHUNK*samplesPerSymbol];
  rcvBuffer = new float[2*2*OUTCHUNK*samplesPerSymbol];
 
  mOn = true;

}

void *AlignRadioServiceLoopAdapter(RadioInterface *radioInterface)
{
  while (1) {
    radioInterface->alignRadio();
    pthread_testcancel();
  }
  return NULL;
}

void RadioInterface::alignRadio() {
  sleep(60);
  mRadio->updateAlignment(writeTimestamp+ (TIMESTAMP) 10000);
}

void RadioInterface::driveTransmitRadio(signalVector &radioBurst, bool zeroBurst) {

  if (!mOn) return;

  radioifyVector(radioBurst, sendBuffer + 2 * sendCursor, powerScaling, zeroBurst);

  sendCursor += radioBurst.size();

  pushBuffer();
}

void RadioInterface::driveReceiveRadio() {

  if (!mOn) return;

  if (mReceiveFIFO.size() > 8) return;

  pullBuffer();

  GSM::Time rcvClock = mClock.get();
  rcvClock.decTN(receiveOffset);
  unsigned tN = rcvClock.TN();
  int rcvSz = rcvCursor;
  int readSz = 0;
  const int symbolsPerSlot = gSlotLen + 8;

  // while there's enough data in receive buffer, form received 
  //    GSM bursts and pass up to Transceiver
  // Using the 157-156-156-156 symbols per timeslot format.
  while (rcvSz > (symbolsPerSlot + (tN % 4 == 0))*samplesPerSymbol) {
    signalVector rxVector((symbolsPerSlot + (tN % 4 == 0))*samplesPerSymbol);
    unRadioifyVector(rcvBuffer+readSz*2,rxVector);
    GSM::Time tmpTime = rcvClock;
    if (rcvClock.FN() >= 0) {
      //LOG(DEBUG) << "FN: " << rcvClock.FN();
      radioVector *rxBurst = NULL;
      if (!loadTest)
        rxBurst = new radioVector(rxVector,tmpTime);
      else {
	if (tN % 4 == 0)
	  rxBurst = new radioVector(*finalVec9,tmpTime);
        else
          rxBurst = new radioVector(*finalVec,tmpTime); 
      }
      mReceiveFIFO.put(rxBurst); 
    }
    mClock.incTN(); 
    rcvClock.incTN();
    //if (mReceiveFIFO.size() >= 16) mReceiveFIFO.wait(8);
    //LOG(DEBUG) << "receiveFIFO: wrote radio vector at time: " << mClock.get() << ", new size: " << mReceiveFIFO.size() ;
    readSz += (symbolsPerSlot+(tN % 4 == 0))*samplesPerSymbol;
    rcvSz -= (symbolsPerSlot+(tN % 4 == 0))*samplesPerSymbol;

    tN = rcvClock.TN();
  }

  if (readSz > 0) {
    rcvCursor -= readSz;
    memmove(rcvBuffer,rcvBuffer+2*readSz,sizeof(float) * 2 * rcvCursor);
  }
}

bool RadioInterface::isUnderrun()
{
  bool retVal = underrun;
  underrun = false;

  return retVal;
}

void RadioInterface::attach(RadioDevice *wRadio, int wRadioOversampling)
{
  if (!mOn) {
    mRadio = wRadio;
    mRadioOversampling = SAMPSPERSYM;
  }
}

double RadioInterface::setRxGain(double dB)
{
  if (mRadio)
    return mRadio->setRxGain(dB);
  else
    return -1;
}

double RadioInterface::getRxGain()
{
  if (mRadio)
    return mRadio->getRxGain();
  else
    return -1;
}
