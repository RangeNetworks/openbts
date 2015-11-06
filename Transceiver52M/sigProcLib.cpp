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

extern "C" {
#include "convolve.h"
}
/* Clipping detection threshold */
#define CLIP_THRESH     30000.0f

#define TABLESIZE 1024

/** Lookup tables for trigonometric approximation */
float cosTable[TABLESIZE+1]; // add 1 element for wrap around
float sinTable[TABLESIZE+1];

/** Constants */
static const float M_PI_F = (float)M_PI;
static const float M_2PI_F = (float)(2.0*M_PI);
static const float M_1_2PI_F = 1/M_2PI_F;

/* Precomputed rotation vectors */
static signalVector *GMSKRotationN = NULL;
static signalVector *GMSKReverseRotationN = NULL;
static signalVector *GMSKRotation1 = NULL;
static signalVector *GMSKReverseRotation1 = NULL;

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
  float        toa;
  complex      gain;
};

/*
 * Gaussian and empty modulation pulses. Like the correlation sequences,
 * store the runtime (Gaussian) buffer separately because of needed alignment
 * for SSE instructions.
 */
struct PulseSequence {
  PulseSequence() : c0(NULL), c1(NULL), empty(NULL),
		    c0_buffer(NULL), c1_buffer(NULL)
  {
  }

  ~PulseSequence()
  {
    delete c0;
    delete c1;
    delete empty;
    free(c0_buffer);
    free(c1_buffer);
  }

  signalVector *c0;
  signalVector *c1;
  signalVector *empty;
  void *c0_buffer;
  void *c1_buffer;
};

CorrelationSequence *gMidambles[] = {NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL};
CorrelationSequence *gRACHSequence = NULL;
PulseSequence *GSMPulse = NULL;
PulseSequence *GSMPulse1 = NULL;

void sigProcLibDestroy()
{
  for (int i = 0; i < 8; i++) {
    delete gMidambles[i];
    gMidambles[i] = NULL;
  }

  delete GMSKRotationN;
  delete GMSKReverseRotationN;
  delete GMSKRotation1;
  delete GMSKReverseRotation1;
  delete gRACHSequence;
  delete GSMPulse;
  delete GSMPulse1;

  GMSKRotationN = NULL;
  GMSKRotation1 = NULL;
  GMSKReverseRotationN = NULL;
  GMSKReverseRotation1 = NULL;
  gRACHSequence = NULL;
  GSMPulse = NULL;
  GSMPulse1 = NULL;
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
  GMSKRotationN = new signalVector(157 * sps);
  GMSKReverseRotationN = new signalVector(157 * sps);
  signalVector::iterator rotPtr = GMSKRotationN->begin();
  signalVector::iterator revPtr = GMSKReverseRotationN->begin();
  float phase = 0.0;
  while (rotPtr != GMSKRotationN->end()) {
    *rotPtr++ = expjLookup(phase);
    *revPtr++ = expjLookup(-phase);
    phase += M_PI_F / 2.0F / (float) sps;
  }

  GMSKRotation1 = new signalVector(157);
  GMSKReverseRotation1 = new signalVector(157);
  rotPtr = GMSKRotation1->begin();
  revPtr = GMSKReverseRotation1->begin();
  phase = 0.0;
  while (rotPtr != GMSKRotation1->end()) {
    *rotPtr++ = expjLookup(phase);
    *revPtr++ = expjLookup(-phase);
    phase += M_PI_F / 2.0F;
  }
}

static void GMSKRotate(signalVector &x, int sps)
{
  signalVector::iterator rotPtr, xPtr = x.begin();

  if (sps == 1)
    rotPtr = GMSKRotation1->begin();
  else
    rotPtr = GMSKRotationN->begin();

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

static void GMSKReverseRotate(signalVector &x, int sps)
{
  signalVector::iterator rotPtr, xPtr= x.begin();

  if (sps == 1)
    rotPtr = GMSKReverseRotation1->begin();
  else
    rotPtr = GMSKReverseRotationN->begin();

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

static bool generateC1Pulse(int sps, PulseSequence *pulse)
{
  int len;

  if (!pulse)
    return false;

  switch (sps) {
  case 4:
    len = 8;
    break;
  default:
    return false;
  }

  pulse->c1_buffer = convolve_h_alloc(len);
  pulse->c1 = new signalVector((complex *)
                                  pulse->c1_buffer, 0, len);
  pulse->c1->isRealOnly(true);

  /* Enable alignment for SSE usage */
  pulse->c1->setAligned(true);

  signalVector::iterator xP = pulse->c1->begin();

  switch (sps) {
  case 4:
    /* BT = 0.30 */
    *xP++ = 0.0;
    *xP++ = 8.16373112e-03;
    *xP++ = 2.84385729e-02;
    *xP++ = 5.64158904e-02;
    *xP++ = 7.05463553e-02;
    *xP++ = 5.64158904e-02;
    *xP++ = 2.84385729e-02;
    *xP++ = 8.16373112e-03;
  }

  return true;
}

static PulseSequence *generateGSMPulse(int sps, int symbolLength)
{
  int len;
  float arg, avg, center;
  PulseSequence *pulse;

  /* Store a single tap filter used for correlation sequence generation */
  pulse = new PulseSequence();
  pulse->empty = new signalVector(1);
  pulse->empty->isRealOnly(true);
  *(pulse->empty->begin()) = 1.0f;

  /*
   * For 4 samples-per-symbol use a precomputed single pulse Laurent
   * approximation. This should yields below 2 degrees of phase error at
   * the modulator output. Use the existing pulse approximation for all
   * other oversampling factors.
   */
  switch (sps) {
  case 4:
    len = 16;
    break;
  default:
    len = sps * symbolLength;
    if (len < 4)
      len = 4;
  }

  pulse->c0_buffer = convolve_h_alloc(len);
  pulse->c0 = new signalVector((complex *) pulse->c0_buffer, 0, len);
  pulse->c0->isRealOnly(true);

  /* Enable alingnment for SSE usage */
  pulse->c0->setAligned(true);

  signalVector::iterator xP = pulse->c0->begin();

  if (sps == 4) {
    *xP++ = 0.0;
    *xP++ = 4.46348606e-03;
    *xP++ = 2.84385729e-02;
    *xP++ = 1.03184855e-01;
    *xP++ = 2.56065552e-01;
    *xP++ = 4.76375085e-01;
    *xP++ = 7.05961177e-01;
    *xP++ = 8.71291644e-01;
    *xP++ = 9.29453645e-01;
    *xP++ = 8.71291644e-01;
    *xP++ = 7.05961177e-01;
    *xP++ = 4.76375085e-01;
    *xP++ = 2.56065552e-01;
    *xP++ = 1.03184855e-01;
    *xP++ = 2.84385729e-02;
    *xP++ = 4.46348606e-03;
    generateC1Pulse(sps, pulse);
  } else {
    center = (float) (len - 1.0) / 2.0;

    /* GSM pulse approximation */
    for (int i = 0; i < len; i++) {
      arg = ((float) i - center) / (float) sps;
      *xP++ = 0.96 * exp(-1.1380 * arg * arg -
			 0.527 * arg * arg * arg * arg);
    }

    avg = sqrtf(vectorNorm2(*pulse->c0) / sps);
    xP = pulse->c0->begin();
    for (int i = 0; i < len; i++)
      *xP++ /= avg;
  }

  return pulse;
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

static signalVector *rotateBurst(const BitVector &wBurst,
                                 int guardPeriodLength, int sps)
{
  int burst_len;
  signalVector *pulse, rotated, *shaped;
  signalVector::iterator itr;

  pulse = GSMPulse1->empty;
  burst_len = sps * (wBurst.size() + guardPeriodLength);
  rotated = signalVector(burst_len);
  itr = rotated.begin();

  for (unsigned i = 0; i < wBurst.size(); i++) {
    *itr = 2.0 * (wBurst[i] & 0x01) - 1.0;
    itr += sps;
  }

  GMSKRotate(rotated, sps);
  rotated.isRealOnly(false);

  /* Dummy filter operation */
  shaped = convolve(&rotated, pulse, NULL, START_ONLY);
  if (!shaped)
    return NULL;

  return shaped;
}

static signalVector *modulateBurstLaurent(const BitVector &bits,
					  int guard_len, int sps)
{
  int burst_len;
  float phase;
  signalVector *c0_pulse, *c1_pulse, c0_burst, c1_burst, *c0_shaped, *c1_shaped;
  signalVector::iterator c0_itr, c1_itr;

  /*
   * Apply before and after bits to reduce phase error at burst edges.
   * Make sure there is enough room in the burst to accomodate all bits.
   */
  if (guard_len < 4)
    guard_len = 4;

  c0_pulse = GSMPulse->c0;
  c1_pulse = GSMPulse->c1;

  burst_len = sps * (bits.size() + guard_len);

  c0_burst = signalVector(burst_len);
  c0_burst.isRealOnly(true);
  c0_itr = c0_burst.begin();

  c1_burst = signalVector(burst_len);
  c1_burst.isRealOnly(true);
  c1_itr = c1_burst.begin();

  /* Padded differential start bits */
  *c0_itr = 2.0 * (0x00 & 0x01) - 1.0;
  c0_itr += sps;

  /* Main burst bits */
  for (unsigned i = 0; i < bits.size(); i++) {
    *c0_itr = 2.0 * (bits[i] & 0x01) - 1.0;
    c0_itr += sps;
  }

  /* Padded differential end bits */
  *c0_itr = 2.0 * (0x01 & 0x01) - 1.0;

  /* Generate C0 phase coefficients */
  GMSKRotate(c0_burst, sps);
  c0_burst.isRealOnly(false);

  c0_itr = c0_burst.begin();
  c0_itr += sps * 2;
  c1_itr += sps * 2;

  /* Start magic */
  phase = 2.0 * ((0x01 & 0x01) ^ (0x01 & 0x01)) - 1.0;
  *c1_itr = *c0_itr * Complex<float>(0, phase);
  c0_itr += sps;
  c1_itr += sps;

  /* Generate C1 phase coefficients */
  for (unsigned i = 2; i < bits.size(); i++) {
    phase = 2.0 * ((bits[i - 1] & 0x01) ^ (bits[i - 2] & 0x01)) - 1.0;
    *c1_itr = *c0_itr * Complex<float>(0, phase);

    c0_itr += sps;
    c1_itr += sps;
  }

  /* End magic */
  int i = bits.size();
  phase = 2.0 * ((bits[i-1] & 0x01) ^ (bits[i-2] & 0x01)) - 1.0;
  *c1_itr = *c0_itr * Complex<float>(0, phase);

  /* Primary (C0) and secondary (C1) pulse shaping */
  c0_shaped = convolve(&c0_burst, c0_pulse, NULL, START_ONLY);
  c1_shaped = convolve(&c1_burst, c1_pulse, NULL, START_ONLY);

  /* Sum shaped outputs into C0 */
  c0_itr = c0_shaped->begin();
  c1_itr = c1_shaped->begin();
  for (unsigned i = 0; i < c0_shaped->size(); i++ )
    *c0_itr++ += *c1_itr++;

  delete c1_shaped;

  return c0_shaped;
}

static signalVector *modulateBurstBasic(const BitVector &bits,
					int guard_len, int sps)
{
  int burst_len;
  signalVector *pulse, burst, *shaped;
  signalVector::iterator burst_itr;

  if (sps == 1)
    pulse = GSMPulse1->c0;
  else
    pulse = GSMPulse->c0;

  burst_len = sps * (bits.size() + guard_len);

  burst = signalVector(burst_len);
  burst.isRealOnly(true);
  burst_itr = burst.begin();

  /* Raw bits are not differentially encoded */
  for (unsigned i = 0; i < bits.size(); i++) {
    *burst_itr = 2.0 * (bits[i] & 0x01) - 1.0;
    burst_itr += sps;
  }

  GMSKRotate(burst, sps);
  burst.isRealOnly(false);

  /* Single Gaussian pulse approximation shaping */
  shaped = convolve(&burst, pulse, NULL, START_ONLY);

  return shaped;
}

/* Assume input bits are not differentially encoded */
signalVector *modulateBurst(const BitVector &wBurst, int guardPeriodLength,
			    int sps, bool emptyPulse)
{
  if (emptyPulse)
    return rotateBurst(wBurst, guardPeriodLength, sps);
  else if (sps == 4)
    return modulateBurstLaurent(wBurst, guardPeriodLength, sps);
  else
    return modulateBurstBasic(wBurst, guardPeriodLength, sps);
}

float sinc(float x)
{
  if ((x >= 0.01F) || (x <= -0.01F)) return (sinLookup(x)/x);
  return 1.0F;
}

bool delayVector(signalVector &wBurst, float delay)
{
  int whole, h_len = 20;
  float frac;
  complex *data;
  signalVector *h, *shift;
  signalVector::iterator itr;

  whole = floor(delay);
  frac = delay - whole;

  /* Sinc interpolated fractional shift (if allowable) */
  if (fabs(frac) > 1e-2) {
    data = (complex *) convolve_h_alloc(h_len);
    h = new signalVector(data, 0, h_len);
    h->setAligned(true);
    h->isRealOnly(true);

    itr = h->end();
    for (int i = 0; i < h_len; i++)
      *--itr = (complex) sinc(M_PI_F * (i - h_len / 2 - frac));

    shift = convolve(&wBurst, h, NULL, NO_DELAY);

    delete h;
    free(data);

    if (!shift)
      return false;

    wBurst.clone(*shift);
    delete shift;
  }

  /* Integer sample shift */
  if (whole < 0) {
    whole = -whole;
    signalVector::iterator wBurstItr = wBurst.begin();
    signalVector::iterator shiftedItr = wBurst.begin() + whole;

    while (shiftedItr < wBurst.end())
      *wBurstItr++ = *shiftedItr++;
    while (wBurstItr < wBurst.end())
      *wBurstItr++ = 0.0;
  } else {
    signalVector::iterator wBurstItr = wBurst.end() - 1;
    signalVector::iterator shiftedItr = wBurst.end() - 1 - whole;

    while (shiftedItr >= wBurst.begin())
      *wBurstItr-- = *shiftedItr--;
    while (wBurstItr >= wBurst.begin())
      *wBurstItr-- = 0.0;
  }

  return true;
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

static complex fastPeakDetect(const signalVector &rxBurst, float *index)
{
  float val, max = 0.0f;
  complex amp;
  int _index = -1;

  for (int i = 0; i < rxBurst.size(); i++) {
    val = rxBurst[i].norm2();
    if (val > max) {
      max = val;
      _index = i;
      amp = rxBurst[i];
    }
  }

  if (index)
    *index = (float) _index;

  return amp;
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
  float toa;
  complex *data = NULL;
  signalVector *autocorr = NULL, *midamble = NULL;
  signalVector *midMidamble = NULL, *_midMidamble = NULL;

  if ((tsc < 0) || (tsc > 7))
    return false;

  delete gMidambles[tsc];

  /* Use middle 16 bits of each TSC. Correlation sequence is not pulse shaped */
  midMidamble = modulateBurst(gTrainingSequence[tsc].alias().segment(5,16), 0, sps, true);
  if (!midMidamble)
    return false;

  /* Simulated receive sequence is pulse shaped */
  midamble = modulateBurst(gTrainingSequence[tsc].alias(), 0, sps, false);
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
  gMidambles[tsc]->gain = peakDetect(*autocorr, &toa, NULL);

  /* For 1 sps only
   *     (Half of correlation length - 1) + midpoint of pulse shape + remainder
   *     13.5 = (16 / 2 - 1) + 1.5 + (26 - 10) / 2
   */
  if (sps == 1)
    gMidambles[tsc]->toa = toa - 13.5;
  else
    gMidambles[tsc]->toa = 0;

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
  float toa;
  complex *data = NULL;
  signalVector *autocorr = NULL;
  signalVector *seq0 = NULL, *seq1 = NULL, *_seq1 = NULL;

  delete gRACHSequence;

  seq0 = modulateBurst(gRACHSynchSequence, 0, sps, false);
  if (!seq0)
    return false;

  seq1 = modulateBurst(gRACHSynchSequence.alias().segment(0, 40), 0, sps, true);
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
  gRACHSequence->gain = peakDetect(*autocorr, &toa, NULL);

  /* For 1 sps only
   *     (Half of correlation length - 1) + midpoint of pulse shaping filer
   *     20.5 = (40 / 2 - 1) + 1.5
   */
  if (sps == 1)
    gRACHSequence->toa = toa - 20.5;
  else
    gRACHSequence->toa = 0.0;

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

static float computePeakRatio(signalVector *corr,
                              int sps, float toa, complex amp)
{
  int num = 0;
  complex *peak;
  float rms, avg = 0.0;

  peak = corr->begin() + (int) rint(toa);

  /* Check for bogus results */
  if ((toa < 0.0) || (toa > corr->size()))
    return 0.0;

  for (int i = 2 * sps; i <= 5 * sps; i++) {
    if (peak - i >= corr->begin()) {
      avg += (peak - i)->norm2();
      num++;
    }
    if (peak + i < corr->end()) {
      avg += (peak + i)->norm2();
      num++;
    }
  }

  if (num < 2)
    return 0.0;

  rms = sqrtf(avg / (float) num) + 0.00001;

  return (amp.abs()) / rms;
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

/*
 * Detect a burst based on correlation and peak-to-average ratio
 *
 * For one sampler-per-symbol, perform fast peak detection (no interpolation)
 * for initial gating. We do this because energy detection should be disabled.
 * For higher oversampling values, we assume the energy detector is in place
 * and we run full interpolating peak detection.
 */
static int detectBurst(signalVector &burst,
                       signalVector &corr, CorrelationSequence *sync,
                       float thresh, int sps, complex *amp, float *toa,
                       int start, int len)
{
  /* Correlate */
  if (!convolve(&burst, sync->sequence, &corr,
                CUSTOM, start, len, sps, 0)) {
    return -SIGERR_INTERNAL;
  }

  /* Peak detection - place restrictions at correlation edges */
  *amp = fastPeakDetect(corr, toa);

  if ((*toa < 3 * sps) || (*toa > len - 3 * sps))
    return 0;

  /* Peak -to-average ratio */
  if (computePeakRatio(&corr, sps, *toa, *amp) < thresh)
    return 0;

  /* Compute peak-to-average ratio. Reject if we don't have enough values */
  *amp = peakDetect(corr, toa, NULL);

  /* Normalize our channel gain */
  *amp = *amp / sync->gain;

  /* Compenate for residual rotation with dual Laurent pulse */
  if (sps == 4)
    *amp = *amp * complex(0.0, 1.0);

  /* Compensate for residuate time lag */
  *toa = *toa - sync->toa;

  return 1;
}

static int detectClipping(signalVector &burst, float thresh)
{
	for (size_t i = 0; i < burst.size(); i++) {
		if (fabs(burst[i].real()) > thresh)
			return 1;
		if (fabs(burst[i].imag()) > thresh)
			return 1;
	}

	return 0;
}

/* 
 * RACH burst detection
 *
 * Correlation window parameters:
 *   target: Tail bits + RACH length (reduced from 41 to a multiple of 4)
 *   head: Search 4 symbols before target 
 *   tail: Search 10 symbols after target
 */
int detectRACHBurst(signalVector &rxBurst,
		    float thresh,
		    int sps,
		    complex *amp,
		    float *toa)
{
  int rc, start, target, head, tail, len;
  float _toa;
  complex _amp;
  signalVector corr;
  CorrelationSequence *sync;

  if ((sps != 1) && (sps != 4))
    return -SIGERR_UNSUPPORTED;

  if (detectClipping(rxBurst, CLIP_THRESH))
    return -SIGERR_CLIP;

  target = 8 + 40;
  head = 4;
  tail = 10;

  start = (target - head) * sps - 1;
  len = (head + tail) * sps;
  sync = gRACHSequence;
  corr = signalVector(len);

  rc = detectBurst(rxBurst, corr, sync,
                   thresh, sps, &_amp, &_toa, start, len);
  if (rc < 0) {
    return -1;
  } else if (!rc) {
    if (amp)
      *amp = 0.0f;
    if (toa)
      *toa = 0.0f;
    return 0;
  }

  /* Subtract forward search bits from delay */
  if (toa)
    *toa = _toa - head * sps;
  if (amp)
    *amp = _amp;

  return 1;
}

/* 
 * Normal burst detection
 *
 * Correlation window parameters:
 *   target: Tail + data + mid-midamble + 1/2 remaining midamblebits
 *   head: Search 4 symbols before target
 *   tail: Search 4 symbols + maximum expected delay
 */
int analyzeTrafficBurst(signalVector &rxBurst, unsigned tsc, float thresh,
                        int sps, complex *amp, float *toa, unsigned max_toa,
                        bool chan_req, signalVector **chan, float *chan_offset)
{
  int rc, start, target, head, tail, len;
  complex _amp;
  float _toa;
  signalVector corr;
  CorrelationSequence *sync;

  if ((tsc < 0) || (tsc > 7) || ((sps != 1) && (sps != 4)))
    return -SIGERR_UNSUPPORTED;

  target = 3 + 58 + 16 + 5;
  head = 4;
  tail = 4 + max_toa;

  start = (target - head) * sps - 1;
  len = (head + tail) * sps;
  sync = gMidambles[tsc];
  corr = signalVector(len);

  rc = detectBurst(rxBurst, corr, sync,
                   thresh, sps, &_amp, &_toa, start, len);
  if (rc < 0) {
    return -SIGERR_INTERNAL;
  } else if (!rc) {
    if (amp)
      *amp = 0.0f;
    if (toa)
      *toa = 0.0f;
    return 0;
  }

  /* Subtract forward search bits from delay */
  _toa -= head * sps;
  if (toa)
    *toa = _toa;
  if (amp)
    *amp = _amp;

  /* Equalization not currently supported */
  if (chan_req) {
    *chan = new signalVector(6 * sps);

    if (chan_offset)
      *chan_offset = 0.0;
  }

  return 1;
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
  GMSKReverseRotate(*shapedBurst, sps);

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
  signalVector::iterator rotPtr = GMSKRotationN->begin();
  signalVector::iterator revRotPtr = GMSKReverseRotationN->begin();

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

bool sigProcLibSetup(int sps)
{
  if ((sps != 1) && (sps != 4))
    return false;

  initTrigTables();
  initGMSKRotationTables(sps);

  GSMPulse1 = generateGSMPulse(1, 2);
  if (sps > 1)
    GSMPulse = generateGSMPulse(sps, 2);

  if (!generateRACHSequence(1)) {
    sigProcLibDestroy();
    return false;
  }

  return true;
}
