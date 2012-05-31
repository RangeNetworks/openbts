/*
* Copyright 2008 Free Software Foundation, Inc.
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
#include "LinkedLists.h"
#include "radioDevice.h"

/** samples per GSM symbol */
#define SAMPSPERSYM 1 
#define INCHUNK    (625)
#define OUTCHUNK   (625)

/** class used to organize GSM bursts by GSM timestamps */
class radioVector : public signalVector {

private:

  GSM::Time mTime;   ///< the burst's GSM timestamp 
  int mARFCN;

public:
  /** constructor */
  radioVector(const signalVector& wVector,
              GSM::Time& wTime,
              int& wARFCN): signalVector(wVector),mTime(wTime),mARFCN(wARFCN){};

  /** timestamp read and write operators */
  GSM::Time time() const { return mTime;}
  void time(const GSM::Time& wTime) { mTime = wTime;}

  /** ARFCN read and write operators */
  int ARFCN() const { return mARFCN;}
  void ARFCN(const int& wARFCN) { mARFCN = wARFCN;}

  /** comparison operator, used for sorting */
  bool operator>(const radioVector& other) const {return mTime > other.mTime;}

};

/** a priority queue of radioVectors, i.e. GSM bursts, sorted so that earliest element is at top */
class VectorQueue : public InterthreadPriorityQueue<radioVector> {

public:

  /** the top element of the queue */
  GSM::Time nextTime() const;

  /**
    Get stale burst, if any.
    @param targTime The target time.
    @return Pointer to burst older than target time, removed from queue, or NULL.
  */
  radioVector* getStaleBurst(const GSM::Time& targTime);

  /**
    Get current burst, if any.
    @param targTime The target time.
    @return Pointer to burst at the target time, removed from queue, or NULL.
  */
  radioVector* getCurrentBurst(const GSM::Time& targTime);


};

/** a FIFO of radioVectors */
class VectorFIFO {

private:

      PointerFIFO mQ;
      Mutex      mLock;

public:

      unsigned size() {return mQ.size();}

      void put(radioVector *ptr) {ScopedLock lock(mLock); mQ.put((void*) ptr);}

      radioVector *get() { ScopedLock lock(mLock); return (radioVector*) mQ.get();}

};


/** the basestation clock class */
class RadioClock {

private:

  GSM::Time mClock;
  Mutex mLock;
  Signal updateSignal;

public:

  /** Set clock */
  void set(const GSM::Time& wTime) { ScopedLock lock(mLock); mClock = wTime; updateSignal.signal();}
  //void set(const GSM::Time& wTime) { ScopedLock lock(mLock); mClock = wTime; updateSignal.broadcast();;}

  /** Increment clock */
  void incTN() { ScopedLock lock(mLock); mClock.incTN(); updateSignal.signal();}
  //void incTN() { ScopedLock lock(mLock); mClock.incTN(); updateSignal.broadcast();}

  /** Get clock value */
  GSM::Time get() { ScopedLock lock(mLock); return mClock; }

  /** Wait until clock has changed */
  //void wait() {ScopedLock lock(mLock); updateSignal.wait(mLock,1);}
  // FIXME -- If we take away the timeout, a lot of threads don't start.  Why?
  void wait() {ScopedLock lock(mLock); updateSignal.wait(mLock);}

};


/** class to interface the transceiver with the USRP */
class RadioInterface {

private:

  Thread mAlignRadioServiceLoopThread;	      ///< thread that synchronizes transmit and receive sections

  VectorFIFO  mReceiveFIFO;		      ///< FIFO that holds receive  bursts

  RadioDevice *mRadio;			      ///< the USRP object
 
  short *sendBuffer; //[2*2*INCHUNK];
  unsigned sendCursor;

  short *rcvBuffer; //[2*2*OUTCHUNK];
  unsigned rcvCursor;
 
  bool underrun;			      ///< indicates writes to USRP are too slow
  bool overrun;				      ///< indicates reads from USRP are too slow
  TIMESTAMP writeTimestamp;		      ///< sample timestamp of next packet written to USRP
  TIMESTAMP readTimestamp;		      ///< sample timestamp of next packet read from USRP

  RadioClock mClock;                          ///< the basestation clock!

  int samplesPerSymbol;			      ///< samples per GSM symbol
  int receiveOffset;                          ///< offset b/w transmit and receive GSM timestamps, in timeslots
  int mRadioOversampling;
  int mTransceiverOversampling;

  bool mOn;				      ///< indicates radio is on

  double powerScaling;

  bool loadTest;
  int mNumARFCNs;
  signalVector *finalVec, *finalVec9;

  /** format samples to USRP */ 
  short *radioifyVector(signalVector &wVector, short *shortVector, double scale, bool zeroOut);

  /** format samples from USRP */
  void unRadioifyVector(short *shortVector, signalVector &wVector);

  /** push GSM bursts into the transmit buffer */
  void pushBuffer(void);

  /** pull GSM bursts from the receive buffer */
  void pullBuffer(void);

public:

  /** start the interface */
  void start();

  /** constructor */
  RadioInterface(RadioDevice* wRadio = NULL,
		 int receiveOffset = 3,
		 int wRadioOversampling = SAMPSPERSYM,
		 int wTransceiverOversampling = SAMPSPERSYM,
		 bool wLoadTest = false,
		 unsigned int wNumARFCNS = 1,
		 GSM::Time wStartTime = GSM::Time(0));
    
  /** destructor */
  ~RadioInterface();

  void setSamplesPerSymbol(int wSamplesPerSymbol) {if (!mOn) samplesPerSymbol = wSamplesPerSymbol;}

  int getSamplesPerSymbol() { return samplesPerSymbol;}

  /** check for underrun, resets underrun value */
  bool isUnderrun() { bool retVal = underrun; underrun = false; return retVal;}
  
  /** attach an existing USRP to this interface */
  void attach(RadioDevice *wRadio, int wRadioOversampling) {if (!mOn) {mRadio = wRadio; mRadioOversampling = SAMPSPERSYM;} }

  /** return the receive FIFO */
  VectorFIFO* receiveFIFO() { return &mReceiveFIFO;}

  /** return the basestation clock */
  RadioClock* getClock(void) { return &mClock;};

 /** set receive gain */
  double setRxGain(double dB) {if (mRadio) return mRadio->setRxGain(dB); else return -1;}

  /** get receive gain */
  double getRxGain(void) {if (mRadio) return mRadio->getRxGain(); else return -1;}


  /** set transmit frequency */
  bool tuneTx(double freq, double adjFreq);

  /** set receive frequency */
  bool tuneRx(double freq, double adjFreq);

  /** drive transmission of GSM bursts */
  void driveTransmitRadio(signalVector &radioBurst, bool zeroBurst);

  /** drive reception of GSM bursts */
  void driveReceiveRadio();

  void setPowerAttenuation(double atten);

  /** returns the full-scale transmit amplitude **/
  double fullScaleInputValue();

  /** returns the full-scale receive amplitude **/
  double fullScaleOutputValue();


protected:

  /** drive synchronization of Tx/Rx of USRP */
  void alignRadio();

  /** reset the interface */
  void reset();

  friend void *AlignRadioServiceLoopAdapter(RadioInterface*);

};

/** synchronization thread loop */
void *AlignRadioServiceLoopAdapter(RadioInterface*);
