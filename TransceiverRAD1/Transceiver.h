/*
* Copyright 2008 Free Software Foundation, Inc.
*
* This software is distributed under the terms of the GNU Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/



/*
	Compilation switches
	TRANSMIT_LOGGING	write every burst on the given slot to a log
*/

#include "radioInterface.h"
#include "Interthread.h"
#include "GSMCommon.h"
#include "Sockets.h"

#include <sys/types.h>
#include <sys/socket.h>

/** Define this to be the slot number to be logged. */
//#define TRANSMIT_LOGGING 1

#define MAXARFCN 5


/** Codes for burst types of received bursts*/
typedef enum {
  OFF,               ///< timeslot is off
  TSC,	       ///< timeslot should contain a normal burst
  RACH,	       ///< timeslot should contain an access burst
  IDLE	       ///< timeslot is an idle (or dummy) burst
} CorrType;

class Demodulator;
class Transceiver;

typedef struct ThreadStruct {
   Transceiver *trx;
   unsigned ARFCN;
} ThreadStruct;

/** The Transceiver class, responsible for physical layer of basestation */
class Transceiver {
  
private:

  GSM::Time mTransmitLatency;     ///< latency between basestation clock and transmit deadline clock
  GSM::Time mLatencyUpdateTime;   ///< last time latency was updated

  UDPSocket *mDataSocket[MAXARFCN];	  ///< socket for writing to/reading from GSM core
  UDPSocket *mControlSocket[MAXARFCN];	  ///< socket for writing/reading control commands from GSM core
  UDPSocket mClockSocket;	  ///< socket for writing clock updates to GSM core

  VectorQueue  mTransmitPriorityQueue;   ///< priority queue of transmit bursts received from GSM core
  VectorFIFO*  mTransmitFIFO;     ///< radioInterface FIFO of transmit bursts 
  VectorFIFO*  mReceiveFIFO;      ///< radioInterface FIFO of receive bursts 
  VectorFIFO*  mDemodFIFO[MAXARFCN];

  Thread *mFIFOServiceLoopThread;  ///< thread to push/pull bursts into transmit/receive FIFO
  Thread *mRFIFOServiceLoopThread;
  Thread *mDemodServiceLoopThread[MAXARFCN];     ///< threads for demodulating individual ARFCNs
  Thread *mControlServiceLoopThread[MAXARFCN];       ///< thread to process control messages from GSM core
  Thread *mTransmitPriorityQueueServiceLoopThread[MAXARFCN];///< thread to process transmit bursts from GSM core

  GSM::Time mTransmitDeadlineClock;       ///< deadline for pushing bursts into transmit FIFO 
  GSM::Time mLastClockUpdateTime;         ///< last time clock update was sent up to core
  GSM::Time mStartTime;

  RadioInterface *mRadioInterface;	  ///< associated radioInterface object
  double txFullScale;                     ///< full scale input to radio
  double rxFullScale;

  Mutex mControlLock;
  Mutex mTransmitPriorityQueueLock;

  bool mLoadTest;

  /** Codes for channel combinations */
  typedef enum {
    FILL,               ///< Channel is transmitted, but unused
    I,                  ///< TCH/FS
    II,                 ///< TCH/HS, idle every other slot
    III,                ///< TCH/HS
    IV,                 ///< FCCH+SCH+CCCH+BCCH, uplink RACH
    V,                  ///< FCCH+SCH+CCCH+BCCH+SDCCH/4+SACCH/4, uplink RACH+SDCCH/4
    VI,                 ///< CCCH+BCCH, uplink RACH
    VII,                ///< SDCCH/8 + SACCH/8
    NONE,               ///< Channel is inactive, default
    LOOPBACK            ///< similar go VII, used in loopback testing
  } ChannelCombination;


  /** unmodulate a modulated burst */
#ifdef TRANSMIT_LOGGING
  void unModulateVector(signalVector wVector); 
#endif

  /** modulate and add a burst to the transmit queue */
  void addRadioVector(BitVector &burst,
		      int RSSI,
		      GSM::Time &wTime,
		      int ARFCN);

  /** Push modulated burst into transmit FIFO corresponding to a particular timestamp */
  void pushRadioVector(GSM::Time &nowTime);

  /** Pull and demodulate a burst from the receive FIFO */ 
  void pullRadioVector(void);
   
  /** Set modulus for specific timeslot */
  void setModulus(int timeslot);

  /** send messages over the clock socket */
  void writeClockInterface(void);

  signalVector *gsmPulse;              ///< the GSM shaping pulse for modulation

  int mSamplesPerSymbol;               ///< number of samples per GSM symbol

  bool mOn;			       ///< flag to indicate that transceiver is powered on
  ChannelCombination mChanType[MAXARFCN][8];     ///< channel types for all timeslots
  double mTxFreq;                      ///< the transmit frequency
  double mRxFreq;                      ///< the receive frequency
  int mPower;                          ///< the transmit power in dB
  unsigned mTSC;                       ///< the midamble sequence code
  int fillerModulus[8];                ///< modulus values of all timeslots, in frames
  signalVector *fillerTable[102][8];   ///< table of modulated filler waveforms for all timeslots
  unsigned mMaxExpectedDelay;            ///< maximum expected time-of-arrival offset in GSM symbols

  unsigned int mNumARFCNs;
  bool mMultipleARFCN;
  unsigned char mOversamplingRate;
  double mFreqOffset;
  signalVector *frequencyShifter[MAXARFCN];
  signalVector *decimationFilter;
  signalVector *interpolationFilter;

  Demodulator *mDemodulators[MAXARFCN];




public:

  /** Transceiver constructor 
      @param wBasePort base port number of UDP sockets
      @param TRXAddress IP address of the TRX manager, as a string
      @param wSamplesPerSymbol number of samples per GSM symbol
      @param wTransmitLatency initial setting of transmit latency
      @param radioInterface associated radioInterface object
  */
  Transceiver(int wBasePort,
	      const char *TRXAddress,
	      int wSamplesPerSymbol,
	      GSM::Time wTransmitLatency,
	      RadioInterface *wRadioInterface,
	      unsigned int wNumARFCNs,
	      unsigned int wOversamplingRate,
	      bool wLoadTest);
   
  /** Destructor */
  ~Transceiver();

  /** start the Transceiver */
  void start();

  bool multiARFCN() {return mMultipleARFCN;}

  /** return the expected burst type for the specified timestamp */
  CorrType expectedCorrType(GSM::Time currTime, int ARFCN);

  /** attach the radioInterface receive FIFO */
  void receiveFIFO(VectorFIFO *wFIFO) { mReceiveFIFO = wFIFO;}

  /** attach the radioInterface transmit FIFO */
  void transmitFIFO(VectorFIFO *wFIFO) { mTransmitFIFO = wFIFO;}

  VectorFIFO *demodFIFO(unsigned ARFCN) { return mDemodFIFO[ARFCN]; }

  RadioInterface *radioInterface(void) { return mRadioInterface; }

  unsigned samplesPerSymbol(void) { return mSamplesPerSymbol; }

  UDPSocket *dataSocket(int ARFCN) { return mDataSocket[ARFCN]; }

  signalVector *GSMPulse(void) { return gsmPulse; }

  unsigned maxDelay(void) { return mMaxExpectedDelay; }

  unsigned getTSC(void) { return mTSC; }

protected:

  /** drive reception and demodulation of GSM bursts */ 
  void driveReceiveFIFO();

  /** drive transmission of GSM bursts */
  void driveTransmitFIFO();

  /** drive handling of control messages from GSM core */
  void driveControl(unsigned ARFCN);

  /**
    drive modulation and sorting of GSM bursts from GSM core
    @return true if a burst was transferred successfully
  */
  bool driveTransmitPriorityQueue(unsigned ARFCN);

  friend void *FIFOServiceLoopAdapter(Transceiver *);

  friend void *RFIFOServiceLoopAdapter(Transceiver *);
   
  friend void *ControlServiceLoopAdapter(ThreadStruct *);

  friend void *TransmitPriorityQueueServiceLoopAdapter(ThreadStruct *);

  void reset();
};

/** FIFO thread loop */
void *FIFOServiceLoopAdapter(Transceiver *);

void *RFIFOServiceLoopAdapter(Transceiver *);

/** control message handler thread loop */
void *ControlServiceLoopAdapter(ThreadStruct *);

/** transmit queueing thread loop */
void *TransmitPriorityQueueServiceLoopAdapter(ThreadStruct *);

class Demodulator {

 private:

  int mARFCN;
  Transceiver *mTRX;
  RadioInterface *mRadioInterface;
  VectorFIFO *mDemodFIFO;
  double mEnergyThreshold;             ///< threshold to determine if received data is potentially a GSM burst
  GSM::Time    prevFalseDetectionTime; ///< last timestamp of a false energy detection
  GSM::Time    channelEstimateTime[8]; ///< last timestamp of each timeslot's channel estimate
  signalVector *channelResponse[8];    ///< most recent channel estimate of all timeslots
  float        SNRestimate[8];         ///< most recent SNR estimate of all timeslots
  signalVector *DFEForward[8];         ///< most recent DFE feedforward filter of all timeslots
  signalVector *DFEFeedback[8];        ///< most recent DFE feedback filter of all timeslots
  float        chanRespOffset[8];      ///< most recent timing offset, e.g. TOA, of all timeslots
  complex      chanRespAmplitude[8];   ///< most recent channel amplitude of all timeslots
  signalVector *gsmPulse;
  unsigned     mTSC;
  unsigned     mSamplesPerSymbol;

  UDPSocket    *mTRXDataSocket;

  unsigned     mMaxExpectedDelay;

  double rxFullScale;                     ///< full scale output to radio

  SoftVector* demodRadioVector(radioVector *rxBurst,
                              GSM::Time &wTime,
                              int &RSSI,
                              int &timingOffset);

 public:

  Demodulator(int wARFCN,
	      Transceiver *wTRX,
	      GSM::Time wStartTime);

  double getEnergyThreshold() {return mEnergyThreshold;}


 //protected:

  void driveDemod(bool wSingleARFCN = true);
 protected:  
  friend void *DemodServiceLoopAdapter(Demodulator *);

};

void *DemodServiceLoopAdapter(Demodulator *);
