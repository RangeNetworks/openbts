/*
* Copyright 2008, 2011 Free Software Foundation, Inc.
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

#include "sigProcLib.h"
#include "GSMCommon.h"
#include "sendLPF_961.h"
#include "rcvLPF_651.h"

extern "C" {
#include "convolve.h"
}

#define TABLESIZE 1024

/** Lookup tables for trigonometric approximation */
float cosTable[TABLESIZE+1]; // add 1 element for wrap around
float sinTable[TABLESIZE+1];

/** Constants */
static const float M_PI_F = (float)M_PI;
static const float M_2PI_F = (float)(2.0*M_PI);
static const float M_1_2PI_F = 1/M_2PI_F;

/** Static vectors that contain a precomputed +/- f_b/4 sinusoid */ 
signalVector *GMSKRotation = NULL;
signalVector *GMSKReverseRotation = NULL;

/*
 * RACH and midamble correlation waveforms. Store the buffer separately
 * because we need to allocate it explicitly outside of the signal vector
 * constructor. This is because C++ (prior to C++11) is unable to natively
 * perform 16-byte memory alignment required by many SSE instructions.
 */
struct CorrelationSequence {
  CorrelationSequence() : sequence(NULL), buffer(NULL)
  {
  }

  ~CorrelationSequence()
  {
    delete sequence;
    free(buffer);
  }

  signalVector *sequence;
  void         *buffer;
  float        TOA;
  complex      gain;
};

/*
 * Gaussian and empty modulation pulses. Like the correlation sequences,
 * store the runtime (Gaussian) buffer separately because of needed alignment
 * for SSE instructions.
 */
struct PulseSequence {
  PulseSequence() : gaussian(NULL), empty(NULL), buffer(NULL)
  {
  }

  ~PulseSequence()
  {
    delete gaussian;
    delete empty;
    free(buffer);
  }

  signalVector *gaussian;
  signalVector *empty;
  void         *buffer;
};

CorrelationSequence *gMidambles[] = {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
CorrelationSequence *gRACHSequence = NULL;
PulseSequence *GSMPulse = NULL;

void sigProcLibDestroy()
{
  for (int i = 0; i < 8; i++) {
    delete gMidambles[i];
    gMidambles[i] = NULL;
  }

  delete GMSKRotation;
  delete GMSKReverseRotation;
  delete gRACHSequence;
  delete GSMPulse;

  GMSKRotation = NULL;
  GMSKReverseRotation = NULL;
  gRACHSequence = NULL;
  GSMPulse = NULL;
}

// dB relative to 1.0.
// if > 1.0, then return 0 dB
float dB(float x) {
  
  float arg = 1.0F;
  float dB = 0.0F;
  
  if (x >= 1.0F) return 0.0F;
  if (x <= 0.0F) return -200.0F;

  float prevArg = arg;
  float prevdB = dB;
  float stepSize = 16.0F;
  float dBstepSize = 12.0F;
  while (stepSize > 1.0F) {
    do {
      prevArg = arg;
      prevdB = dB;
      arg /= stepSize;
      dB -= dBstepSize;
    } while (arg > x);
    arg = prevArg;
    dB = prevdB;
    stepSize *= 0.5F;
    dBstepSize -= 3.0F;
  }
 return ((arg-x)*(dB-3.0F) + (x-arg*0.5F)*dB)/(arg - arg*0.5F);

}

// 10^(-dB/10), inverse of dB func.
float dBinv(float x) {
  
  float arg = 1.0F;
  float dB = 0.0F;
  
  if (x >= 0.0F) return 1.0F;
  if (x <= -200.0F) return 0.0F;

  float prevArg = arg;
  float prevdB = dB;
  float stepSize = 16.0F;
  float dBstepSize = 12.0F;
  while (stepSize > 1.0F) {
    do {
      prevArg = arg;
      prevdB = dB;
      arg /= stepSize;
      dB -= dBstepSize;
    } while (dB > x);
    arg = prevArg;
    dB = prevdB;
    stepSize *= 0.5F;
    dBstepSize -= 3.0F;
  }

  return ((dB-x)*(arg*0.5F)+(x-(dB-3.0F))*(arg))/3.0F;

}

float vectorNorm2(const signalVector &x) 
{
  signalVector::const_iterator xPtr = x.begin();
  float Energy = 0.0;
  for (;xPtr != x.end();xPtr++) {
	Energy += xPtr->norm2();
  }
  return Energy;
}


float vectorPower(const signalVector &x) 
{
  return vectorNorm2(x)/x.size();
}

/** compute cosine via lookup table */
float cosLookup(const float x)
{
  float arg = x*M_1_2PI_F;
  while (arg > 1.0F) arg -= 1.0F;
  while (arg < 0.0F) arg += 1.0F;

  const float argT = arg*((float)TABLESIZE);
  const int argI = (int)argT;
  const float delta = argT-argI;
  const float iDelta = 1.0F-delta;
  return iDelta*cosTable[argI] + delta*cosTable[argI+1];
}

/** compute sine via lookup table */
float sinLookup(const float x) 
{
  float arg = x*M_1_2PI_F;
  while (arg > 1.0F) arg -= 1.0F;
  while (arg < 0.0F) arg += 1.0F;

  const float argT = arg*((float)TABLESIZE);
  const int argI = (int)argT;
  const float delta = argT-argI;
  const float iDelta = 1.0F-delta;
  return iDelta*sinTable[argI] + delta*sinTable[argI+1];
}


/** compute e^(-jx) via lookup table. */
complex expjLookup(float x)
{
  float arg = x*M_1_2PI_F;
  while (arg > 1.0F) arg -= 1.0F;
  while (arg < 0.0F) arg += 1.0F;

  const float argT = arg*((float)TABLESIZE);
  const int argI = (int)argT;
  const float delta = argT-argI;
  const float iDelta = 1.0F-delta;
   return complex(iDelta*cosTable[argI] + delta*cosTable[argI+1],
		   iDelta*sinTable[argI] + delta*sinTable[argI+1]);
}

/** Library setup functions */
void initTrigTables() {
  for (int i = 0; i < TABLESIZE+1; i++) {
    cosTable[i] = cos(2.0*M_PI*i/TABLESIZE);
    sinTable[i] = sin(2.0*M_PI*i/TABLESIZE);
  }
}

void initGMSKRotationTables(int sps)
{
  GMSKRotation = new signalVector(157 * sps);
  GMSKReverseRotation = new signalVector(157 * sps);
  signalVector::iterator rotPtr = GMSKRotation->begin();
  signalVector::iterator revPtr = GMSKReverseRotation->begin();
  float phase = 0.0;
  while (rotPtr != GMSKRotation->end()) {
    *rotPtr++ = expjLookup(phase);
    *revPtr++ = expjLookup(-phase);
    phase += M_PI_F / 2.0F / (float) sps;
  }
}

bool sigProcLibSetup(int sps)
{
  if ((sps != 1) && (sps != 2) && (sps != 4))
    return false;

  initTrigTables();
  initGMSKRotationTables(sps);
  generateGSMPulse(sps, 2);

  if (!generateRACHSequence(sps)) {
    sigProcLibDestroy();
    return false;
  }

  return true;
}

void GMSKRotate(signalVector &x) {
  signalVector::iterator xPtr = x.begin();
  signalVector::iterator rotPtr = GMSKRotation->begin();
  if (x.isRealOnly()) {
    while (xPtr < x.end()) {
      *xPtr = *rotPtr++ * (xPtr->real());
      xPtr++;
    }
  }
  else {
    while (xPtr < x.end()) {
      *xPtr = *rotPtr++ * (*xPtr);
      xPtr++;
    }
  }
}

void GMSKReverseRotate(signalVector &x) {
  signalVector::iterator xPtr= x.begin();
  signalVector::iterator rotPtr = GMSKReverseRotation->begin();
  if (x.isRealOnly()) {
    while (xPtr < x.end()) {
      *xPtr = *rotPtr++ * (xPtr->real());
      xPtr++;
    }
  }
  else {
    while (xPtr < x.end()) {
      *xPtr = *rotPtr++ * (*xPtr);
      xPtr++;
    }
  }
}

signalVector *convolve(const signalVector *x,
                        const signalVector *h,
                        signalVector *y,
                        ConvType spanType, int start,
                        unsigned len, unsigned step, int offset)
{
  int rc, head = 0, tail = 0;
  bool alloc = false, append = false;
  const signalVector *_x = NULL;

  if (!x || !h)
    return NULL;

  switch (spanType) {
  case START_ONLY:
    start = 0;
    head = h->size();
    len = x->size();
    append = true;
    break;
  case NO_DELAY:
    start = h->size() / 2;
    head = start;
    tail = start;
    len = x->size();
    append = true;
    break;
  case CUSTOM:
    if (start < h->size() - 1) {
      head = h->size() - start;
      append = true;
    }
    if (start + len > x->size()) {
      tail = start + len - x->size();
      append = true;
    }
    break;
  default:
    return NULL;
  }

  /*
   * Error if the output vector is too small. Create the output vector
   * if the pointer is NULL.
   */
  if (y && (len > y->size()))
    return NULL;
  if (!y) {
    y = new signalVector(len);
    alloc = true;
  }

  /* Prepend or post-pend the input vector if the parameters require it */
  if (append)
    _x = new signalVector(*x, head, tail);
  else
    _x = x;

  /*
   * Four convovle types:
   *   1. Complex-Real (aligned)
   *   2. Complex-Complex (aligned)
   *   3. Complex-Real (!aligned)
   *   4. Complex-Complex (!aligned)
   */
  if (h->isRealOnly() && h->isAligned()) {
    rc = convolve_real((float *) _x->begin(), _x->size(),
                       (float *) h->begin(), h->size(),
                       (float *) y->begin(), y->size(),
                       start, len, step, offset);
  } else if (!h->isRealOnly() && h->isAligned()) {
    rc = convolve_complex((float *) _x->begin(), _x->size(),
                          (float *) h->begin(), h->size(),
                          (float *) y->begin(), y->size(),
                          start, len, step, offset);
  } else if (h->isRealOnly() && !h->isAligned()) {
    rc = base_convolve_real((float *) _x->begin(), _x->size(),
                            (float *) h->begin(), h->size(),
                            (float *) y->begin(), y->size(),
                            start, len, step, offset);
  } else if (!h->isRealOnly() && !h->isAligned()) {
    rc = base_convolve_complex((float *) _x->begin(), _x->size(),
                               (float *) h->begin(), h->size(),
                               (float *) y->begin(), y->size(),
                               start, len, step, offset);
  } else {
    rc = -1;
  }

  if (append)
    delete _x;

  if (rc < 0) {
    if (alloc)
      delete y;
    return NULL;
  }

  return y;
}

void generateGSMPulse(int sps, int symbolLength)
{
  int len;
  float arg, center;

  delete GSMPulse;

  /* Store a single tap filter used for correlation sequence generation */
  GSMPulse = new PulseSequence();
  GSMPulse->empty = new signalVector(1);
  GSMPulse->empty->isRealOnly(true);
  *(GSMPulse->empty->begin()) = 1.0f;

  len = sps * symbolLength;
  if (len < 4)
    len = 4;

  /* GSM pulse approximation */
  GSMPulse->buffer = convolve_h_alloc(len);
  GSMPulse->gaussian = new signalVector((complex *)
                                        GSMPulse->buffer, 0, len);
  GSMPulse->gaussian->setAligned(true);
  GSMPulse->gaussian->isRealOnly(true);

  signalVector::iterator xP = GSMPulse->gaussian->begin();

  center = (float) (len - 1.0) / 2.0;

  for (int i = 0; i < len; i++) {
    arg = ((float) i - center) / (float) sps;
    *xP++ = 0.96 * exp(-1.1380 * arg * arg -
                        0.527 * arg * arg * arg * arg);
  }

  float avgAbsval = sqrtf(vectorNorm2(*GSMPulse->gaussian)/sps);
  xP = GSMPulse->gaussian->begin();
  for (int i = 0; i < len; i++) 
    *xP++ /= avgAbsval;
}

signalVector* frequencyShift(signalVector *y,
			     signalVector *x,
			     float freq,
			     float startPhase,
			     float *finalPhase)
{

  if (!x) return NULL;
 
  if (y==NULL) {
    y = new signalVector(x->size());
    y->isRealOnly(x->isRealOnly());
    if (y==NULL) return NULL;
  }

  if (y->size() < x->size()) return NULL;

  float phase = startPhase;
  signalVector::iterator yP = y->begin();
  signalVector::iterator xPEnd = x->end();
  signalVector::iterator xP = x->begin();

  if (x->isRealOnly()) {
    while (xP < xPEnd) {
      (*yP++) = expjLookup(phase)*( (xP++)->real() );
      phase += freq;
    }
  }
  else {
    while (xP < xPEnd) {
      (*yP++) = (*xP++)*expjLookup(phase);
      phase += freq;
    }
  }


  if (finalPhase) *finalPhase = phase;

  return y;
}

signalVector* reverseConjugate(signalVector *b)
{
    signalVector *tmp = new signalVector(b->size());
    tmp->isRealOnly(b->isRealOnly());
    signalVector::iterator bP = b->begin();
    signalVector::iterator bPEnd = b->end();
    signalVector::iterator tmpP = tmp->end()-1;
    if (!b->isRealOnly()) {
      while (bP < bPEnd) {
        *tmpP-- = bP->conj();
        bP++;
      }
    }
    else {
      while (bP < bPEnd) {
        *tmpP-- = bP->real();
        bP++;
      }
    }

    return tmp;
}

/* soft output slicer */
bool vectorSlicer(signalVector *x) 
{

  signalVector::iterator xP = x->begin();
  signalVector::iterator xPEnd = x->end();
  while (xP < xPEnd) {
    *xP = (complex) (0.5*(xP->real()+1.0F));
    if (xP->real() > 1.0) *xP = 1.0;
    if (xP->real() < 0.0) *xP = 0.0;
    xP++;
  }
  return true;
}

/* Assume input bits are not differentially encoded */
signalVector *modulateBurst(const BitVector &wBurst, int guardPeriodLength,
			    int sps, bool emptyPulse)
{
  int burstLen;
  signalVector *pulse, *shapedBurst, modBurst;
  signalVector::iterator modBurstItr;

  if (emptyPulse)
    pulse = GSMPulse->empty;
  else
    pulse = GSMPulse->gaussian;

  burstLen = sps * (wBurst.size() + guardPeriodLength);
  modBurst = signalVector(burstLen);
  modBurstItr = modBurst.begin();

  for (unsigned int i = 0; i < wBurst.size(); i++) {
    *modBurstItr = 2.0*(wBurst[i] & 0x01)-1.0;
    modBurstItr += sps;
  }

  // shift up pi/2
  // ignore starting phase, since spec allows for discontinuous phase
  GMSKRotate(modBurst);

  modBurst.isRealOnly(false);

  // filter w/ pulse shape
  shapedBurst = convolve(&modBurst, pulse, NULL, START_ONLY);
  if (!shapedBurst)
    return NULL;

  return shapedBurst;
}

float sinc(float x) 
{
  if ((x >= 0.01F) || (x <= -0.01F)) return (sinLookup(x)/x);
  return 1.0F;
}

bool delayVector(signalVector &wBurst, float delay)
{
  
  int   intOffset = (int) floor(delay);
  float fracOffset = delay - intOffset;

  // do fractional shift first, only do it for reasonable offsets
  if (fabs(fracOffset) > 1e-2) {
    // create sinc function
    signalVector sincVector(21); 
    sincVector.isRealOnly(true);
    signalVector::iterator sincBurstItr = sincVector.end();
    for (int i = 0; i < 21; i++) 
      *--sincBurstItr = (complex) sinc(M_PI_F*(i-10-fracOffset));
  
    signalVector shiftedBurst(wBurst.size());
    if (!convolve(&wBurst, &sincVector, &shiftedBurst, NO_DELAY))
      return false;
    wBurst.clone(shiftedBurst);
  }

  if (intOffset < 0) {
    intOffset = -intOffset;
    signalVector::iterator wBurstItr = wBurst.begin();
    signalVector::iterator shiftedItr = wBurst.begin()+intOffset;
    while (shiftedItr < wBurst.end())
      *wBurstItr++ = *shiftedItr++;
    while (wBurstItr < wBurst.end())
      *wBurstItr++ = 0.0;
  }
  else {
    signalVector::iterator wBurstItr = wBurst.end()-1;
    signalVector::iterator shiftedItr = wBurst.end()-1-intOffset;
    while (shiftedItr >= wBurst.begin())
      *wBurstItr-- = *shiftedItr--;
    while (wBurstItr >= wBurst.begin())
      *wBurstItr-- = 0.0;
  }
}
  
signalVector *gaussianNoise(int length, 
			    float variance, 
			    complex mean)
{

  signalVector *noise = new signalVector(length);
  signalVector::iterator nPtr = noise->begin();
  float stddev = sqrtf(variance);
  while (nPtr < noise->end()) {
    float u1 = (float) rand()/ (float) RAND_MAX;
    while (u1==0.0)
      u1 = (float) rand()/ (float) RAND_MAX;
    float u2 = (float) rand()/ (float) RAND_MAX;
    float arg = 2.0*M_PI*u2;
    *nPtr = mean + stddev*complex(cos(arg),sin(arg))*sqrtf(-2.0*log(u1));
    nPtr++;
  }

  return noise;
}

complex interpolatePoint(const signalVector &inSig,
			 float ix)
{
  
  int start = (int) (floor(ix) - 10);
  if (start < 0) start = 0;
  int end = (int) (floor(ix) + 11);
  if ((unsigned) end > inSig.size()-1) end = inSig.size()-1;
  
  complex pVal = 0.0;
  if (!inSig.isRealOnly()) {
    for (int i = start; i < end; i++) 
      pVal += inSig[i] * sinc(M_PI_F*(i-ix));
  }
  else {
    for (int i = start; i < end; i++) 
      pVal += inSig[i].real() * sinc(M_PI_F*(i-ix));
  }
   
  return pVal;
}

  
 
complex peakDetect(const signalVector &rxBurst,
		   float *peakIndex,
		   float *avgPwr) 
{
  

  complex maxVal = 0.0;
  float maxIndex = -1;
  float sumPower = 0.0;

  for (unsigned int i = 0; i < rxBurst.size(); i++) {
    float samplePower = rxBurst[i].norm2();
    if (samplePower > maxVal.real()) {
      maxVal = samplePower;
      maxIndex = i;
    }
    sumPower += samplePower;
  }

  // interpolate around the peak
  // to save computation, we'll use early-late balancing
  float earlyIndex = maxIndex-1;
  float lateIndex = maxIndex+1;
  
  float incr = 0.5;
  while (incr > 1.0/1024.0) {
    complex earlyP = interpolatePoint(rxBurst,earlyIndex);
    complex lateP =  interpolatePoint(rxBurst,lateIndex);
    if (earlyP < lateP) 
      earlyIndex += incr;
    else if (earlyP > lateP)
      earlyIndex -= incr;
    else break;
    incr /= 2.0;
    lateIndex = earlyIndex + 2.0;
  }

  maxIndex = earlyIndex + 1.0;
  maxVal = interpolatePoint(rxBurst,maxIndex);

  if (peakIndex!=NULL)
    *peakIndex = maxIndex;

  if (avgPwr!=NULL)
    *avgPwr = (sumPower-maxVal.norm2()) / (rxBurst.size()-1);

  return maxVal;

}

void scaleVector(signalVector &x,
		 complex scale)
{
  signalVector::iterator xP = x.begin();
  signalVector::iterator xPEnd = x.end();
  if (!x.isRealOnly()) {
    while (xP < xPEnd) {
      *xP = *xP * scale;
      xP++;
    }
  }
  else {
    while (xP < xPEnd) {
      *xP = xP->real() * scale;
      xP++;
    }
  }
}

/** in-place conjugation */
void conjugateVector(signalVector &x)
{
  if (x.isRealOnly()) return;
  signalVector::iterator xP = x.begin();
  signalVector::iterator xPEnd = x.end();
  while (xP < xPEnd) {
    *xP = xP->conj();
    xP++;
  }
}


// in-place addition!!
bool addVector(signalVector &x,
	       signalVector &y)
{
  signalVector::iterator xP = x.begin();
  signalVector::iterator yP = y.begin();
  signalVector::iterator xPEnd = x.end();
  signalVector::iterator yPEnd = y.end();
  while ((xP < xPEnd) && (yP < yPEnd)) {
    *xP = *xP + *yP;
    xP++; yP++;
  }
  return true;
}

// in-place multiplication!!
bool multVector(signalVector &x,
                 signalVector &y)
{
  signalVector::iterator xP = x.begin();
  signalVector::iterator yP = y.begin();
  signalVector::iterator xPEnd = x.end();
  signalVector::iterator yPEnd = y.end();
  while ((xP < xPEnd) && (yP < yPEnd)) {
    *xP = (*xP) * (*yP);
    xP++; yP++;
  }
  return true;
}


void offsetVector(signalVector &x,
		  complex offset)
{
  signalVector::iterator xP = x.begin();
  signalVector::iterator xPEnd = x.end();
  if (!x.isRealOnly()) {
    while (xP < xPEnd) {
      *xP += offset;
      xP++;
    }
  }
  else {
    while (xP < xPEnd) {
      *xP = xP->real() + offset;
      xP++;
    }      
  }
}

bool generateMidamble(int sps, int tsc)
{
  bool status = true;
  complex *data = NULL;
  signalVector *autocorr = NULL, *midamble = NULL;
  signalVector *midMidamble = NULL, *_midMidamble = NULL;

  if ((tsc < 0) || (tsc > 7))
    return false;

  delete gMidambles[tsc];

  /* Use middle 16 bits of each TSC. Correlation sequence is not pulse shaped */
  midMidamble = modulateBurst(gTrainingSequence[tsc].segment(5,16), 0, sps, true);
  if (!midMidamble)
    return false;

  /* Simulated receive sequence is pulse shaped */
  midamble = modulateBurst(gTrainingSequence[tsc], 0, sps, false);
  if (!midamble) {
    status = false;
    goto release;
  }

  // NOTE: Because ideal TSC 16-bit midamble is 66 symbols into burst,
  //       the ideal TSC has an + 180 degree phase shift,
  //       due to the pi/2 frequency shift, that 
  //       needs to be accounted for.
  //       26-midamble is 61 symbols into burst, has +90 degree phase shift.
  scaleVector(*midMidamble, complex(-1.0, 0.0));
  scaleVector(*midamble, complex(0.0, 1.0));

  conjugateVector(*midMidamble);

  /* For SSE alignment, reallocate the midamble sequence on 16-byte boundary */
  data = (complex *) convolve_h_alloc(midMidamble->size());
  _midMidamble = new signalVector(data, 0, midMidamble->size());
  _midMidamble->setAligned(true);
  memcpy(_midMidamble->begin(), midMidamble->begin(),
	 midMidamble->size() * sizeof(complex));

  autocorr = convolve(midamble, _midMidamble, NULL, NO_DELAY);
  if (!autocorr) {
    status = false;
    goto release;
  }

  gMidambles[tsc] = new CorrelationSequence;
  gMidambles[tsc]->buffer = data;
  gMidambles[tsc]->sequence = _midMidamble;
  gMidambles[tsc]->gain = peakDetect(*autocorr,&gMidambles[tsc]->TOA, NULL);

release:
  delete autocorr;
  delete midamble;
  delete midMidamble;

  if (!status) {
    delete _midMidamble;
    free(data);
    gMidambles[tsc] = NULL;
  }

  return status;
}

bool generateRACHSequence(int sps)
{
  bool status = true;
  complex *data = NULL;
  signalVector *autocorr = NULL;
  signalVector *seq0 = NULL, *seq1 = NULL, *_seq1 = NULL;

  delete gRACHSequence;

  seq0 = modulateBurst(gRACHSynchSequence, 0, sps, false);
  if (!seq0)
    return false;

  seq1 = modulateBurst(gRACHSynchSequence.segment(0, 40), 0, sps, true);
  if (!seq1) {
    status = false;
    goto release;
  }

  conjugateVector(*seq1);

  /* For SSE alignment, reallocate the midamble sequence on 16-byte boundary */
  data = (complex *) convolve_h_alloc(seq1->size());
  _seq1 = new signalVector(data, 0, seq1->size());
  _seq1->setAligned(true);
  memcpy(_seq1->begin(), seq1->begin(), seq1->size() * sizeof(complex));

  autocorr = convolve(seq0, _seq1, autocorr, NO_DELAY);
  if (!autocorr) {
    status = false;
    goto release;
  }

  gRACHSequence = new CorrelationSequence;
  gRACHSequence->sequence = _seq1;
  gRACHSequence->buffer = data;
  gRACHSequence->gain = peakDetect(*autocorr,&gRACHSequence->TOA, NULL);

release:
  delete autocorr;
  delete seq0;
  delete seq1;

  if (!status) {
    delete _seq1;
    free(data);
    gRACHSequence = NULL;
  }

  return status;
}

int detectRACHBurst(signalVector &rxBurst,
		    float thresh,
		    int sps,
		    complex *amp,
		    float *toa)
{
  int start, len, num = 0;
  float _toa, rms, par, avg = 0.0f;
  complex _amp, *peak;
  signalVector corr, *sync = gRACHSequence->sequence;

  if ((sps != 1) && (sps != 2) && (sps != 4))
    return -1;

  start = 40 * sps;
  len = 24 * sps;
  corr = signalVector(len);

  if (!convolve(&rxBurst, sync, &corr,
                CUSTOM, start, len, sps, 0)) {
    return -1;
  }

  _amp = peakDetect(corr, &_toa, NULL);
  if ((_toa < 3) || (_toa > len - 3))
    goto notfound;

  peak = corr.begin() + (int) rint(_toa);

  for (int i = 2 * sps; i <= 5 * sps; i++) {
    if (peak - i >= corr.begin()) {
      avg += (peak - i)->norm2();
      num++;
    }
    if (peak + i < corr.end()) {
      avg += (peak + i)->norm2();
      num++;
    }
  }

  if (num < 2)
    goto notfound;

  rms = sqrtf(avg / (float) num) + 0.00001;
  par = _amp.abs() / rms;
  if (par < thresh)
    goto notfound;

  /* Subtract forward tail bits from delay */
  if (toa)
    *toa = _toa - 8 * sps;
  if (amp)
    *amp = _amp / gRACHSequence->gain;

  return 1;

notfound:
  if (amp)
    *amp = 0.0f;
  if (toa)
    *toa = 0.0f;

  return 0;
}

bool energyDetect(signalVector &rxBurst,
		  unsigned windowLength,
		  float detectThreshold,
                  float *avgPwr)
{

  signalVector::const_iterator windowItr = rxBurst.begin(); //+rxBurst.size()/2 - 5*windowLength/2;
  float energy = 0.0;
  if (windowLength < 0) windowLength = 20;
  if (windowLength > rxBurst.size()) windowLength = rxBurst.size();
  for (unsigned i = 0; i < windowLength; i++) {
    energy += windowItr->norm2();
    windowItr+=4;
  }
  if (avgPwr) *avgPwr = energy/windowLength;
  return (energy/windowLength > detectThreshold*detectThreshold);
}

int analyzeTrafficBurst(signalVector &rxBurst, unsigned tsc, float thresh,
                        int sps, complex *amp, float *toa, unsigned max_toa,
                        bool chan_req, signalVector **chan, float *chan_offset)
{
  int start, target, len, num = 0;
  complex _amp, *peak;
  float _toa, rms, par, avg = 0.0f;
  signalVector corr, *sync, *_chan;

  if ((tsc < 0) || (tsc > 7) || ((sps != 1) && (sps != 2) && (sps != 4)))
    return -1;

  target = 3 + 58 + 5 + 16;
  start = (target - 8) * sps;
  len = (8 + 8 + max_toa) * sps;

  sync = gMidambles[tsc]->sequence;
  sync = gMidambles[tsc]->sequence;
  corr = signalVector(len);

  if (!convolve(&rxBurst, sync, &corr,
                CUSTOM, start, len, sps, 0)) {
    return -1;
  }

  _amp = peakDetect(corr, &_toa, NULL);
  peak = corr.begin() + (int) rint(_toa);

  /* Check for bogus results */
  if ((_toa < 0.0) || (_toa > corr.size()))
    goto notfound;

  for (int i = 2 * sps; i <= 5 * sps; i++) {
    if (peak - i >= corr.begin()) {
      avg += (peak - i)->norm2();
      num++;
    }
    if (peak + i < corr.end()) {
      avg += (peak + i)->norm2();
      num++;
    }
  }

  if (num < 2)
    goto notfound;

  rms = sqrtf(avg / (float) num) + 0.00001;
  par = (_amp.abs()) / rms;
  if (par < thresh)
    goto notfound;

  /*
   *  NOTE: Because ideal TSC is 66 symbols into burst,
   *      the ideal TSC has an +/- 180 degree phase shift,
   *      due to the pi/4 frequency shift, that 
   *      needs to be accounted for.
   */
  if (amp)
    *amp = _amp / gMidambles[tsc]->gain;

  /* Delay one half of peak-centred correlation length */
  _toa -= sps * 8;

  if (toa)
    *toa = _toa;

  if (chan_req) {
    _chan = new signalVector(6 * sps);

    delayVector(corr, -_toa);
    corr.segmentCopyTo(*_chan, target - 3, _chan->size());
    scaleVector(*_chan, complex(1.0, 0.0) / gMidambles[tsc]->gain);

    *chan = _chan;

    if (chan_offset)
      *chan_offset = 3.0 * sps;;
  }

  return 1;

notfound:
  if (amp)
    *amp = 0.0f;
  if (toa)
    *toa = 0.0f;

  return 0;
}

signalVector *decimateVector(signalVector &wVector,
			     int decimationFactor) 
{
  
  if (decimationFactor <= 1) return NULL;

  signalVector *decVector = new signalVector(wVector.size()/decimationFactor);
  decVector->isRealOnly(wVector.isRealOnly());

  signalVector::iterator vecItr = decVector->begin();
  for (unsigned int i = 0; i < wVector.size();i+=decimationFactor) 
    *vecItr++ = wVector[i];

  return decVector;
}


SoftVector *demodulateBurst(signalVector &rxBurst, int sps,
                            complex channel, float TOA) 
{
  scaleVector(rxBurst,((complex) 1.0)/channel);
  delayVector(rxBurst,-TOA);

  signalVector *shapedBurst = &rxBurst;

  // shift up by a quarter of a frequency
  // ignore starting phase, since spec allows for discontinuous phase
  GMSKReverseRotate(*shapedBurst);

  // run through slicer
  if (sps > 1) {
     signalVector *decShapedBurst = decimateVector(*shapedBurst, sps);
     shapedBurst = decShapedBurst;
  }

  vectorSlicer(shapedBurst);

  SoftVector *burstBits = new SoftVector(shapedBurst->size());

  SoftVector::iterator burstItr = burstBits->begin();
  signalVector::iterator shapedItr = shapedBurst->begin();
  for (; shapedItr < shapedBurst->end(); shapedItr++) 
    *burstItr++ = shapedItr->real();

  if (sps > 1)
    delete shapedBurst;

  return burstBits;

}


// 1.0 is sampling frequency
// must satisfy cutoffFreq > 1/filterLen
signalVector *createLPF(float cutoffFreq,
			int filterLen,
			float gainDC)
{
#if 0
  signalVector *LPF = new signalVector(filterLen-1);
  LPF->isRealOnly(true);
  LPF->setSymmetry(ABSSYM);
  signalVector::iterator itr = LPF->begin();
  double sum = 0.0;
  for (int i = 1; i < filterLen; i++) {
    float ys = sinc(M_2PI_F*cutoffFreq*((float)i-(float)(filterLen)/2.0F));
    float yg = 4.0F * cutoffFreq;
    // Blackman -- less brickwall (sloping transition) but larger stopband attenuation
    float yw = 0.42 - 0.5*cos(((float)i)*M_2PI_F/(float)(filterLen)) + 0.08*cos(((float)i)*2*M_2PI_F/(float)(filterLen));
    // Hamming -- more brickwall with smaller stopband attenuation
    //float yw = 0.53836F - 0.46164F * cos(((float)i)*M_2PI_F/(float)(filterLen+1));
    *itr++ = (complex) ys*yg*yw;
    sum += ys*yg*yw;
  }
#else
  double sum = 0.0;
  signalVector *LPF;
  signalVector::iterator itr;
  if (filterLen == 651) { // receive LPF
    LPF = new signalVector(651);
    LPF->isRealOnly(true);
    itr = LPF->begin();
    for (int i = 0; i < filterLen; i++) {
       *itr++ = complex(rcvLPF_651[i],0.0);
       sum += rcvLPF_651[i];
    }
  }
  else { 
    LPF = new signalVector(961);
    LPF->isRealOnly(true);
    itr = LPF->begin();
    for (int i = 0; i < filterLen; i++) {
       *itr++ = complex(sendLPF_961[i],0.0);
       sum += sendLPF_961[i];
    }
  }
#endif

  float normFactor = gainDC/sum; //sqrtf(gainDC/vectorNorm2(*LPF));
  // normalize power
  itr = LPF->begin();
  for (int i = 0; i < filterLen; i++) {
    *itr = *itr*normFactor;
    itr++;
  }
  return LPF;

}
    


#define POLYPHASESPAN 10

// assumes filter group delay is 0.5*(length of filter)
signalVector *polyphaseResampleVector(signalVector &wVector,
				      int P, int Q,
				      signalVector *LPF)

{
 
  bool deleteLPF = false;
 
  if (LPF==NULL) {
    float cutoffFreq = (P < Q) ? (1.0/(float) Q) : (1.0/(float) P);
    LPF = createLPF(cutoffFreq/3.0,100*POLYPHASESPAN+1,Q);
    deleteLPF = true;
  }

  signalVector *resampledVector = new signalVector((int) ceil(wVector.size()*(float) P / (float) Q));
  resampledVector->fill(0);
  resampledVector->isRealOnly(wVector.isRealOnly());
  signalVector::iterator newItr = resampledVector->begin();

  //FIXME: need to update for real-only vectors
  int outputIx = (LPF->size()+1)/2/Q; //((P > Q) ? P : Q); 
  while (newItr < resampledVector->end()) {
    int outputBranch = (outputIx*Q) % P; 
    int inputOffset = (outputIx*Q - outputBranch)/P;
    signalVector::const_iterator inputItr = wVector.begin() + inputOffset;
    signalVector::const_iterator filtItr  = LPF->begin() + outputBranch;
    while (inputItr >= wVector.end()) {
      inputItr--;
      filtItr+=P;
    }
    complex sum = 0.0;
    if ((LPF->getSymmetry()!=ABSSYM) || (P>1)) {
      if (!LPF->isRealOnly()) {
        while ( (inputItr >= wVector.begin()) && (filtItr < LPF->end()) ) {
	  sum += (*inputItr)*(*filtItr);
	  inputItr--;
	  filtItr += P;
        }
      }
      else {
        while ( (inputItr >= wVector.begin()) && (filtItr < LPF->end()) ) {
	  sum += (*inputItr)*(filtItr->real());
	  inputItr--;
	  filtItr += P;
        }
      }
    }
    else {
      signalVector::const_iterator revInputItr = inputItr- LPF->size() + 1;  
      signalVector::const_iterator filtMidpoint = LPF->begin()+(LPF->size()-1)/2;
      if (!LPF->isRealOnly()) {
	while (filtItr <= filtMidpoint) {
	  if (inputItr < revInputItr) break;
	  if (inputItr == revInputItr) 
	    sum += (*inputItr)*(*filtItr);
          else if ( (inputItr < wVector.end()) && (revInputItr >= wVector.begin()) )
            sum += (*inputItr + *revInputItr)*(*filtItr);
          else if ( inputItr < wVector.end() ) 
	    sum += (*inputItr)*(*filtItr);
          else if ( revInputItr >= wVector.begin() )
	    sum += (*revInputItr)*(*filtItr);
          inputItr--;
	  revInputItr++;
          filtItr++;
        }
      }
      else {
        while (filtItr <= filtMidpoint) {
          if (inputItr < revInputItr) break;
          if (inputItr == revInputItr)
            sum += (*inputItr)*(filtItr->real());
          else if ( (inputItr < wVector.end()) && (revInputItr >= wVector.begin()) ) 
            sum += (*inputItr + *revInputItr)*(filtItr->real());
          else if ( inputItr < wVector.end() ) 
            sum += (*inputItr)*(filtItr->real());
          else if ( revInputItr >= wVector.begin() )
            sum += (*revInputItr)*(filtItr->real());
          inputItr--;
          revInputItr++;
          filtItr++;
        }
      }
    }
    *newItr = sum;
    newItr++;
    outputIx++;
  }
      
  if (deleteLPF) delete LPF;

  return resampledVector;
}


signalVector *resampleVector(signalVector &wVector,
			     float expFactor,
			     complex endPoint)

{

  if (expFactor < 1.0) return NULL;

  signalVector *retVec = new signalVector((int) ceil(wVector.size()*expFactor));

  float t = 0.0;
  
  signalVector::iterator retItr = retVec->begin();
  while (retItr < retVec->end()) {
    unsigned tLow = (unsigned int) floor(t);
    unsigned tHigh = tLow + 1;
    if (tLow > wVector.size()-1) break;
    if (tHigh > wVector.size()) break;
    complex lowPoint = wVector[tLow];
    complex highPoint = (tHigh == wVector.size()) ? endPoint : wVector[tHigh];
    complex a = (tHigh-t);
    complex b = (t-tLow);
    *retItr = (a*lowPoint + b*highPoint);
    t += 1.0/expFactor;
  }

  return retVec;

}
		   

// Assumes symbol-spaced sampling!!!
// Based upon paper by Al-Dhahir and Cioffi
bool designDFE(signalVector &channelResponse,
	       float SNRestimate,
	       int Nf,
	       signalVector **feedForwardFilter,
	       signalVector **feedbackFilter)
{
  
  signalVector G0(Nf);
  signalVector G1(Nf);
  signalVector::iterator G0ptr = G0.begin();
  signalVector::iterator G1ptr = G1.begin();
  signalVector::iterator chanPtr = channelResponse.begin();

  int nu = channelResponse.size()-1;

  *G0ptr = 1.0/sqrtf(SNRestimate);
  for(int j = 0; j <= nu; j++) {
    *G1ptr = chanPtr->conj();
    G1ptr++; chanPtr++;
  }

  signalVector *L[Nf];
  signalVector::iterator Lptr;
  float d;
  for(int i = 0; i < Nf; i++) {
    d = G0.begin()->norm2() + G1.begin()->norm2();
    L[i] = new signalVector(Nf+nu);
    Lptr = L[i]->begin()+i;
    G0ptr = G0.begin(); G1ptr = G1.begin();
    while ((G0ptr < G0.end()) &&  (Lptr < L[i]->end())) {
      *Lptr = (*G0ptr*(G0.begin()->conj()) + *G1ptr*(G1.begin()->conj()) )/d;
      Lptr++;
      G0ptr++;
      G1ptr++;
    }
    complex k = (*G1.begin())/(*G0.begin());

    if (i != Nf-1) {
      signalVector G0new = G1;
      scaleVector(G0new,k.conj());
      addVector(G0new,G0);

      signalVector G1new = G0;
      scaleVector(G1new,k*(-1.0));
      addVector(G1new,G1);
      delayVector(G1new,-1.0);

      scaleVector(G0new,1.0/sqrtf(1.0+k.norm2()));
      scaleVector(G1new,1.0/sqrtf(1.0+k.norm2()));
      G0 = G0new;
      G1 = G1new;
    }
  }

  *feedbackFilter = new signalVector(nu);
  L[Nf-1]->segmentCopyTo(**feedbackFilter,Nf,nu);
  scaleVector(**feedbackFilter,(complex) -1.0);
  conjugateVector(**feedbackFilter);

  signalVector v(Nf);
  signalVector::iterator vStart = v.begin();
  signalVector::iterator vPtr;
  *(vStart+Nf-1) = (complex) 1.0;
  for(int k = Nf-2; k >= 0; k--) {
    Lptr = L[k]->begin()+k+1;
    vPtr = vStart + k+1;
    complex v_k = 0.0;
    for (int j = k+1; j < Nf; j++) {
      v_k -= (*vPtr)*(*Lptr);
      vPtr++; Lptr++;
    }
     *(vStart + k) = v_k;
  }

  *feedForwardFilter = new signalVector(Nf);
  signalVector::iterator w = (*feedForwardFilter)->end();
  for (int i = 0; i < Nf; i++) {
    delete L[i];
    complex w_i = 0.0;
    int endPt = ( nu < (Nf-1-i) ) ? nu : (Nf-1-i);
    vPtr = vStart+i;
    chanPtr = channelResponse.begin();
    for (int k = 0; k < endPt+1; k++) {
      w_i += (*vPtr)*(chanPtr->conj());
      vPtr++; chanPtr++;
    }
    *--w = w_i/d;
  }


  return true;
  
}

// Assumes symbol-rate sampling!!!!
SoftVector *equalizeBurst(signalVector &rxBurst,
		       float TOA,
		       int sps,
		       signalVector &w, // feedforward filter
		       signalVector &b) // feedback filter
{
  signalVector *postForwardFull;

  if (!delayVector(rxBurst, -TOA))
    return NULL;

  postForwardFull = convolve(&rxBurst, &w, NULL,
                             CUSTOM, 0, rxBurst.size() + w.size() - 1);
  if (!postForwardFull)
    return NULL;

  signalVector* postForward = new signalVector(rxBurst.size());
  postForwardFull->segmentCopyTo(*postForward,w.size()-1,rxBurst.size());
  delete postForwardFull;

  signalVector::iterator dPtr = postForward->begin();
  signalVector::iterator dBackPtr;
  signalVector::iterator rotPtr = GMSKRotation->begin();
  signalVector::iterator revRotPtr = GMSKReverseRotation->begin();

  signalVector *DFEoutput = new signalVector(postForward->size());
  signalVector::iterator DFEItr = DFEoutput->begin();

  // NOTE: can insert the midamble and/or use midamble to estimate BER
  for (; dPtr < postForward->end(); dPtr++) {
    dBackPtr = dPtr-1;
    signalVector::iterator bPtr = b.begin();
    while ( (bPtr < b.end()) && (dBackPtr >= postForward->begin()) ) {
      *dPtr = *dPtr + (*bPtr)*(*dBackPtr);
      bPtr++;
      dBackPtr--;
    }
    *dPtr = *dPtr * (*revRotPtr);
    *DFEItr = *dPtr;
    // make decision on symbol
    *dPtr = (dPtr->real() > 0.0) ? 1.0 : -1.0;
    //*DFEItr = *dPtr;
    *dPtr = *dPtr * (*rotPtr);
    DFEItr++;
    rotPtr++;
    revRotPtr++;
  }

  vectorSlicer(DFEoutput);

  SoftVector *burstBits = new SoftVector(postForward->size());
  SoftVector::iterator burstItr = burstBits->begin();
  DFEItr = DFEoutput->begin();
  for (; DFEItr < DFEoutput->end(); DFEItr++) 
    *burstItr++ = DFEItr->real();

  delete postForward;

  delete DFEoutput;

  return burstBits;
}
