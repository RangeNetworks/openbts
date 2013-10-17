/*
* Copyright 2008 Free Software Foundation, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/



#include "sigProcLib.h"  
#include "GSMCommon.h"
#include "LinkedLists.h"
#include "radioDevice.h"
#include "radioVector.h"
#include "radioClock.h"

/** class to interface the transceiver with the USRP */
class RadioInterface {

protected:

  Thread mAlignRadioServiceLoopThread;	      ///< thread that synchronizes transmit and receive sections

  VectorFIFO  mReceiveFIFO;		      ///< FIFO that holds receive  bursts

  RadioDevice *mRadio;			      ///< the USRP object

  int mSPSTx;
  int mSPSRx;
  signalVector *sendBuffer;
  signalVector *recvBuffer;
  unsigned sendCursor;
  unsigned recvCursor;

  short *convertRecvBuffer;
  short *convertSendBuffer;

  bool underrun;			      ///< indicates writes to USRP are too slow
  bool overrun;				      ///< indicates reads from USRP are too slow
  TIMESTAMP writeTimestamp;		      ///< sample timestamp of next packet written to USRP
  TIMESTAMP readTimestamp;		      ///< sample timestamp of next packet read from USRP

  RadioClock mClock;                          ///< the basestation clock!

  int receiveOffset;                          ///< offset b/w transmit and receive GSM timestamps, in timeslots

  bool mOn;				      ///< indicates radio is on

  double powerScaling;

  bool loadTest;
  int mNumARFCNs;
  signalVector *finalVec, *finalVec9;

private:

  /** format samples to USRP */ 
  int radioifyVector(signalVector &wVector,
                     float *floatVector,
                     bool zero);

  /** format samples from USRP */
  int unRadioifyVector(float *floatVector, signalVector &wVector);

  /** push GSM bursts into the transmit buffer */
  virtual void pushBuffer(void);

  /** pull GSM bursts from the receive buffer */
  virtual void pullBuffer(void);

public:

  /** start the interface */
  void start();

  /** intialization */
  virtual bool init(int type);
  virtual void close();

  /** constructor */
  RadioInterface(RadioDevice* wRadio = NULL,
		 int receiveOffset = 3,
		 int wSPS = 4,
		 GSM::Time wStartTime = GSM::Time(0));
    
  /** destructor */
  virtual ~RadioInterface();

  /** check for underrun, resets underrun value */
  bool isUnderrun();
  
  /** attach an existing USRP to this interface */
  void attach(RadioDevice *wRadio, int wRadioOversampling);

  /** return the receive FIFO */
  VectorFIFO* receiveFIFO() { return &mReceiveFIFO;}

  /** return the basestation clock */
  RadioClock* getClock(void) { return &mClock;};

  /** set transmit frequency */
  bool tuneTx(double freq);

  /** set receive frequency */
  bool tuneRx(double freq);

  /** set receive gain */
  double setRxGain(double dB);

  /** get receive gain */
  double getRxGain(void);

  /** drive transmission of GSM bursts */
  void driveTransmitRadio(signalVector &radioBurst, bool zeroBurst);

  /** drive reception of GSM bursts */
  void driveReceiveRadio();

  void setPowerAttenuation(double atten);

  /** returns the full-scale transmit amplitude **/
  double fullScaleInputValue();

  /** returns the full-scale receive amplitude **/
  double fullScaleOutputValue();

  /** set thread priority on current thread */
  void setPriority() { mRadio->setPriority(); }

  /** get transport window type of attached device */ 
  enum RadioDevice::TxWindowType getWindowType() { return mRadio->getWindowType(); }

#if USRP1
protected:

  /** drive synchronization of Tx/Rx of USRP */
  void alignRadio();

  friend void *AlignRadioServiceLoopAdapter(RadioInterface*);
#endif
};

#if USRP1
/** synchronization thread loop */
void *AlignRadioServiceLoopAdapter(RadioInterface*);
#endif

class RadioInterfaceResamp : public RadioInterface {

private:
  signalVector *innerSendBuffer;
  signalVector *outerSendBuffer;
  signalVector *innerRecvBuffer;
  signalVector *outerRecvBuffer;

  void pushBuffer();
  void pullBuffer();

public:

  RadioInterfaceResamp(RadioDevice* wRadio = NULL,
		       int receiveOffset = 3,
		       int wSPS = 4,
		       GSM::Time wStartTime = GSM::Time(0));

  ~RadioInterfaceResamp();

  bool init(int type);
  void close();
};
