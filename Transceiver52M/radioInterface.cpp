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
#include "Resampler.h"
#include <Logger.h>

extern "C" {
#include "convert.h"
}

#define CHUNK		625
#define NUMCHUNKS	4

RadioInterface::RadioInterface(RadioDevice *wRadio,
			       int wReceiveOffset,
			       int wSPS,
			       GSM::Time wStartTime)
  : underrun(false), sendCursor(0), recvCursor(0), mOn(false),
    mRadio(wRadio), receiveOffset(wReceiveOffset),
    mSPSTx(wSPS), mSPSRx(1), powerScaling(1.0),
    loadTest(false), sendBuffer(NULL), recvBuffer(NULL),
    convertRecvBuffer(NULL), convertSendBuffer(NULL)
{
  mClock.set(wStartTime);
}

RadioInterface::~RadioInterface(void)
{
  close();
}

bool RadioInterface::init(int type)
{
  if (type != RadioDevice::NORMAL)
    return false;

  close();

  sendBuffer = new signalVector(CHUNK * mSPSTx);
  recvBuffer = new signalVector(NUMCHUNKS * CHUNK * mSPSRx);

  convertSendBuffer = new short[sendBuffer->size() * 2];
  convertRecvBuffer = new short[recvBuffer->size() * 2];

  sendCursor = 0;
  recvCursor = 0;

  return true;
}

void RadioInterface::close()
{
  delete sendBuffer;
  delete recvBuffer;
  delete convertSendBuffer;
  delete convertRecvBuffer;

  sendBuffer = NULL;
  recvBuffer = NULL;
  convertRecvBuffer = NULL;
  convertSendBuffer = NULL;
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
				   bool zero)
{
  if (zero) {
    memset(retVector, 0, wVector.size() * 2 * sizeof(float));
    return wVector.size();
  }

  memcpy(retVector, wVector.begin(), wVector.size() * 2 * sizeof(float));

  return wVector.size();
}

int RadioInterface::unRadioifyVector(float *floatVector,
				     signalVector& newVector)
{
  signalVector::iterator itr = newVector.begin();

  if (newVector.size() > recvCursor) {
    LOG(ALERT) << "Insufficient number of samples in receive buffer";
    return -1;
  }

  for (int i = 0; i < newVector.size(); i++) {
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
#ifdef USRP1
  mAlignRadioServiceLoopThread.start((void * (*)(void*))AlignRadioServiceLoopAdapter,
                                     (void*)this);
#endif
  writeTimestamp = mRadio->initialWriteTimestamp();
  readTimestamp = mRadio->initialReadTimestamp();
  mRadio->start(); 
  LOG(DEBUG) << "Radio started";
  mRadio->updateAlignment(writeTimestamp-10000); 
  mRadio->updateAlignment(writeTimestamp-10000);

  mOn = true;

}

#ifdef USRP1
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
#endif

void RadioInterface::driveTransmitRadio(signalVector &radioBurst, bool zeroBurst)
{
  if (!mOn)
    return;

  radioifyVector(radioBurst,
                 (float *) (sendBuffer->begin() + sendCursor), zeroBurst);

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
  int rcvSz = recvCursor;
  int readSz = 0;
  const int symbolsPerSlot = gSlotLen + 8;

  // while there's enough data in receive buffer, form received 
  //    GSM bursts and pass up to Transceiver
  // Using the 157-156-156-156 symbols per timeslot format.
  while (rcvSz > (symbolsPerSlot + (tN % 4 == 0)) * mSPSRx) {
    signalVector rxVector((symbolsPerSlot + (tN % 4 == 0)) * mSPSRx);
    unRadioifyVector((float *) (recvBuffer->begin() + readSz), rxVector);
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
    readSz += (symbolsPerSlot+(tN % 4 == 0)) * mSPSRx;
    rcvSz -= (symbolsPerSlot+(tN % 4 == 0)) * mSPSRx;

    tN = rcvClock.TN();
  }

  if (readSz > 0) {
    memmove(recvBuffer->begin(),
            recvBuffer->begin() + readSz,
            (recvCursor - readSz) * 2 * sizeof(float));

    recvCursor -= readSz;
  }
}

bool RadioInterface::isUnderrun()
{
  bool retVal = underrun;
  underrun = false;

  return retVal;
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

/* Receive a timestamped chunk from the device */
void RadioInterface::pullBuffer()
{
  bool local_underrun;
  int num_recv;
  float *output;

  if (recvCursor > recvBuffer->size() - CHUNK)
    return;

  /* Outer buffer access size is fixed */
  num_recv = mRadio->readSamples(convertRecvBuffer,
                                 CHUNK,
                                 &overrun,
                                 readTimestamp,
                                 &local_underrun);
  if (num_recv != CHUNK) {
          LOG(ALERT) << "Receive error " << num_recv;
          return;
  }

  output = (float *) (recvBuffer->begin() + recvCursor);

  convert_short_float(output, convertRecvBuffer, 2 * num_recv);

  underrun |= local_underrun;

  readTimestamp += num_recv;
  recvCursor += num_recv;
}

/* Send timestamped chunk to the device with arbitrary size */
void RadioInterface::pushBuffer()
{
  int num_sent;

  if (sendCursor < CHUNK)
    return;

  if (sendCursor > sendBuffer->size())
    LOG(ALERT) << "Send buffer overflow";

  convert_float_short(convertSendBuffer,
                      (float *) sendBuffer->begin(),
                      powerScaling, 2 * sendCursor);

  /* Send the all samples in the send buffer */ 
  num_sent = mRadio->writeSamples(convertSendBuffer,
                                  sendCursor,
                                  &underrun,
                                  writeTimestamp);
  if (num_sent != sendCursor) {
          LOG(ALERT) << "Transmit error " << num_sent;
  }

  writeTimestamp += num_sent;
  sendCursor = 0;
}
