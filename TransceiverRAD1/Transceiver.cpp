/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
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


#include <stdio.h>
#include "Transceiver.h"
#include <Logger.h>
#include <Configuration.h>

extern ConfigurationTable gConfig;

Transceiver::Transceiver(int wBasePort,
			 const char *TRXAddress,
			 int wSamplesPerSymbol,
			 GSM::Time wTransmitLatency,
			 RadioInterface *wRadioInterface,
			 unsigned int wNumARFCNs,
			 unsigned int wOversamplingRate,
			 bool wLoadTest)
	:mClockSocket(wBasePort,TRXAddress,wBasePort+100)
{
  //GSM::Time startTime(0,0);
  //GSM::Time startTime(gHyperframe/2 - 4*216*60,0);
  GSM::Time startTime = mStartTime = GSM::Time(random() % gHyperframe,0);

  mFIFOServiceLoopThread = new Thread(2*32768);  ///< thread to push bursts into transmit FIFO
  mRFIFOServiceLoopThread = new Thread(4*32768);
  for (int j = 0; j< wNumARFCNs; j++) { 
    mControlServiceLoopThread[j] = new Thread(32768);
    mTransmitPriorityQueueServiceLoopThread[j] = new Thread(32768);
    if (wNumARFCNs > 1) mDemodServiceLoopThread[j] = new Thread(32768);
    mDemodFIFO[j] = new VectorFIFO;
    mDataSocket[j] = new UDPSocket(wBasePort+2*(j+1),TRXAddress,wBasePort+100+2*(j+1));
    mControlSocket[j] = new UDPSocket(wBasePort+2*j+1,TRXAddress,wBasePort+100+2*j+1);
  }

  mSamplesPerSymbol = wSamplesPerSymbol;
  mRadioInterface = wRadioInterface;
  mTransmitLatency = wTransmitLatency;
  mTransmitDeadlineClock = startTime;
  mLastClockUpdateTime = startTime;
  mLatencyUpdateTime = startTime;
  mRadioInterface->getClock()->set(startTime);
  mMaxExpectedDelay = 1;

  mNumARFCNs = wNumARFCNs;
  mOversamplingRate = wOversamplingRate;
  mLoadTest = wLoadTest;

  LOG(INFO) << "running " << mNumARFCNs << " ARFCNs";

  // generate pulse and setup up signal processing library
  gsmPulse = generateGSMPulse(2,mSamplesPerSymbol);
  LOG(DEBUG) << "gsmPulse: " << *gsmPulse;
  sigProcLibSetup(mSamplesPerSymbol);

  txFullScale = mRadioInterface->fullScaleInputValue();
  rxFullScale = mRadioInterface->fullScaleOutputValue();

  // initialize filler tables with dummy bursts, initialize other per-timeslot variables
  for (int i = 0; i < 8; i++) {
    signalVector* modBurst = modulateBurst(gDummyBurst,*gsmPulse,
					   8 + (i % 4 == 0),
					   mSamplesPerSymbol);
    scaleVector(*modBurst,txFullScale);
    fillerModulus[i] = 26;
    for (int j = 0; j < 102; j++) {
      fillerTable[j][i] = new signalVector(*modBurst);
    }
    delete modBurst;
    for (int j = 0; j < mNumARFCNs; j++) {
      mChanType[j][i] = NONE;
    }
  }

  mFreqOffset = 0.0;
  mMultipleARFCN = (mNumARFCNs > 1);
  if (mMultipleARFCN) {
     //mOversamplingRate = mNumARFCNs/2 + mNumARFCNs;
     //mOversamplingRate = 15; //mOversamplingRate*4;
     //if (mOversamplingRate % 2) mOversamplingRate++;
     double beaconFreq = -1.0*(mNumARFCNs-1)*200e3;
     for (int j = 0; j < mNumARFCNs; j++) {
        frequencyShifter[j] = new signalVector(157*mOversamplingRate);
        frequencyShifter[j]->fill(complex(1.0,0.0));
        frequencyShifter[j]->isRealOnly(false);
        frequencyShift(frequencyShifter[j],frequencyShifter[j],2.0*M_PI*(beaconFreq+j*400e3)/(1625.0e3/6.0*mOversamplingRate));
     }

     int filtLen = 6*mOversamplingRate;
     decimationFilter = createLPF(0.5/mOversamplingRate,filtLen,1); 
     interpolationFilter = createLPF(0.5/mOversamplingRate,filtLen,1);
     scaleVector(*interpolationFilter,mOversamplingRate);
     mFreqOffset = -beaconFreq;
     mRadioInterface->setSamplesPerSymbol(SAMPSPERSYM*mOversamplingRate);

     // refill filler table
    for (int i = 0; i < 8; i++) {
      signalVector* modBurst = modulateBurst(gDummyBurst,*gsmPulse,
                                             8 + (i % 4 == 0),
                                             mSamplesPerSymbol);
      scaleVector(*modBurst,txFullScale/mNumARFCNs);
      signalVector *interpVec = polyphaseResampleVector(*modBurst,mOversamplingRate,1,interpolationFilter);
      //signalVector *interpVec = new signalVector(modBurst->size()*mOversamplingRate);
      //interpVec->fill(txFullScale);
      multVector(*interpVec,*frequencyShifter[0]);
      for (int j = 0; j < 102; j++) {
        delete fillerTable[j][i];
        fillerTable[j][i] = new signalVector(*interpVec);
      }
      delete modBurst;
      delete interpVec;
    }
  }

  mOn = false;
  mTxFreq = 0.0;
  mRxFreq = 0.0;
  mPower = -10;

  mControlLock.unlock();
  mTransmitPriorityQueueLock.unlock();

}

Transceiver::~Transceiver()
{
  delete gsmPulse;
  sigProcLibDestroy();
  mTransmitPriorityQueue.clear();
}
  

void Transceiver::addRadioVector(BitVector &burst,
				 int RSSI,
				 GSM::Time &wTime,
				 int ARFCN)
{
  // modulate and stick into queue 
  signalVector* modBurst = modulateBurst(burst,*gsmPulse,
					 8 + (wTime.TN() % 4 == 0),
					 mSamplesPerSymbol);
  /*complex rScale = complex(2*M_PI*((float) rand()/(float) RAND_MAX),(2*M_PI*((float) rand()/(float) RAND_MAX)));
  rScale = rScale/rScale.abs();
  scaleVector(*modBurst,rScale);*/
  float headRoom = (mNumARFCNs > 1) ? 0.5 : 1.0;
  scaleVector(*modBurst,txFullScale * headRoom * pow(10,-RSSI/10)/mNumARFCNs);
  radioVector *newVec = new radioVector(*modBurst,wTime,ARFCN);

  // upsample and filter and freq shift
  if (mMultipleARFCN) {
    signalVector *interpVec = polyphaseResampleVector(*((signalVector *)newVec),mOversamplingRate,1,interpolationFilter);
    //LOG(DEBUG) << "newVec size: " << newVec->size() << ", interpVec: " << interpVec->size();
    delete newVec;
    
    //if (ARFCN!=0) printf("ARFCN: %d\n",ARFCN);
    multVector(*interpVec,*frequencyShifter[ARFCN]);

    newVec = new radioVector(*interpVec,wTime,ARFCN);
    delete interpVec;
  }

  mTransmitPriorityQueue.write(newVec);

  delete modBurst;
}

#ifdef TRANSMIT_LOGGING
void Transceiver::unModulateVector(signalVector wVector) 
{
  SoftVector *burst = demodulateBurst(wVector,
				   *gsmPulse,
				   mSamplesPerSymbol,
				   1.0,0.0);
  LOG(DEBUG) << "LOGGED BURST: " << *burst;

/*
  unsigned char burstStr[gSlotLen+1];
  SoftVector::iterator burstItr = burst->begin();
  for (int i = 0; i < gSlotLen; i++) {
    // FIXME: Demod bits are inverted!
    burstStr[i] = (unsigned char) ((*burstItr++)*255.0);
  }
  burstStr[gSlotLen]='\0';
  LOG(DEBUG) << "LOGGED BURST: " << burstStr;
*/
  delete burst;
}
#endif

void Transceiver::pushRadioVector(GSM::Time &nowTime)
{

  // dump stale bursts, if any
  while (radioVector* staleBurst = mTransmitPriorityQueue.getStaleBurst(nowTime)) {
    // Even if the burst is stale, put it in the fillter table.
    // (It might be an idle pattern.)
    LOG(NOTICE) << "dumping STALE burst in TRX->USRP interface";
    if (staleBurst->ARFCN()==0) {
      const GSM::Time& nextTime = staleBurst->time();
      int TN = nextTime.TN();
      int modFN = nextTime.FN() % fillerModulus[TN];
    // FIXME!!!!!!
      delete fillerTable[modFN][TN];
      fillerTable[modFN][TN] = staleBurst;
    }
    else
      delete staleBurst;
  }
  
  int TN = nowTime.TN();
  int modFN = nowTime.FN() % fillerModulus[nowTime.TN()];

  bool addFiller = true;
  radioVector *sendVec = NULL;

  // if queue contains data at the desired timestamp, stick it into FIFO
  while (radioVector *next = (radioVector*) mTransmitPriorityQueue.getCurrentBurst(nowTime)) {
    //LOG(DEBUG) << "transmitFIFO: wrote burst " << next << " at time: " << nowTime;
    if (next->ARFCN() == 0) {
      delete fillerTable[modFN][TN];
      fillerTable[modFN][TN] = new signalVector(*(next));
      addFiller = false;
    }
    if (!sendVec)
      sendVec = next;
    else {
      addVector(*sendVec,*next);
      delete next;
    }
  }

  if (addFiller) {
    // pull filler data, and push to radio FIFO
    int dummy = 0;
    radioVector *tmpVec = new radioVector(*fillerTable[modFN][TN],nowTime,dummy);
    if (!sendVec)
      sendVec = tmpVec;
    else {
       addVector(*sendVec,*tmpVec);
       delete tmpVec;
    }
  }

  //LOG(DEBUG) << "sendVec size: " << sendVec->size();

  // otherwise, pull filler data, and push to radio FIFO
  mRadioInterface->driveTransmitRadio(*sendVec,(mChanType[0][TN]==NONE));

  delete sendVec;

}

void Transceiver::setModulus(int timeslot)
{

  switch (mChanType[0][timeslot]) {
  case NONE:
  case I:
  case II:
  case III:
  case FILL:
    fillerModulus[timeslot] = 26;
    break;
  case IV:
  case VI:
  case V:
    fillerModulus[timeslot] = 51;
    break;
    //case V: 
  case VII:
    fillerModulus[timeslot] = 102;
    break;
  default:
    break;
  }
}


CorrType Transceiver::expectedCorrType(GSM::Time currTime, int ARFCN)
{
  
  unsigned burstTN = currTime.TN();
  unsigned burstFN = currTime.FN();

  switch (mChanType[ARFCN][burstTN]) {
  case NONE:
    return OFF;
    break;
  case FILL:
    return IDLE;
    break;
  case I:
    return TSC;
    /*if (burstFN % 26 == 25) 
      return IDLE;
    else
      return TSC;*/
    break;
  case II:
    if (burstFN % 2 == 1)
      return IDLE;
    else
      return TSC;
    break;
  case III:
    return TSC;
    break;
  case IV:
  case VI:
    return RACH;
    break;
  case V: {
    int mod51 = burstFN % 51;
    if ((mod51 <= 36) && (mod51 >= 14))
      return RACH;
    else if ((mod51 == 4) || (mod51 == 5))
      return RACH;
    else if ((mod51 == 45) || (mod51 == 46))
      return RACH;
    else
      return TSC;
    break;
  }
  case VII:
    if ((burstFN % 51 <= 14) && (burstFN % 51 >= 12))
      return IDLE;
    else
      return TSC;
    break;
  case LOOPBACK:
    if ((burstFN % 51 <= 50) && (burstFN % 51 >=48))
      return IDLE;
    else
      return TSC;
    break;
  default:
    return OFF;
    break;
  }

}
    
void Transceiver::pullRadioVector()
{
  radioVector *rxBurst = NULL;
  rxBurst = (radioVector *) mReceiveFIFO->get();
  if (!rxBurst) return;

  //LOG(DEBUG) << "receiveFIFO: read radio vector " << rxBurst << " at time: " << rxBurst->time() << ", new size: " << mReceiveFIFO->size();

  GSM::Time theTime = rxBurst->time();
  int timeslot = rxBurst->time().TN();

  for (int i = 0; i < mNumARFCNs; i++) {
    CorrType corrType = expectedCorrType(rxBurst->time(),i);
    if ((corrType == OFF) || (corrType == IDLE)) continue;
    radioVector *ARFCNVec = new radioVector(*(signalVector *)rxBurst,theTime,i);
    if (mMultipleARFCN) {
      multVector(*ARFCNVec,*frequencyShifter[mNumARFCNs-1-i]);
      signalVector *rcvVec = polyphaseResampleVector(*ARFCNVec,1,mOversamplingRate,decimationFilter);
      delete ARFCNVec;
      ARFCNVec = new radioVector(*rcvVec,theTime,i);
      delete rcvVec;
    }
    //LOG(INFO) << "putting " << ARFCNVec << " in queue " << i << " at time " << theTime;
    mDemodFIFO[i]->put(ARFCNVec);
  }

  delete rxBurst;
}

void Transceiver::start()
{
  for(int i = 0; i < mNumARFCNs; i++) {
    ThreadStruct *cs = new ThreadStruct;
    cs->trx = this;
    cs->ARFCN = i;
    mControlServiceLoopThread[i]->start((void * (*)(void*))ControlServiceLoopAdapter,(void*) cs);
  }
}

void Transceiver::reset()
{
  mTransmitPriorityQueue.clear();
  //mTransmitFIFO->clear();
  //mReceiveFIFO->clear();
}

  
void Transceiver::driveControl(unsigned ARFCN)
{

  int MAX_PACKET_LENGTH = 100;

  // check control socket
  char buffer[MAX_PACKET_LENGTH];
  int msgLen = -1;
  buffer[0] = '\0';
 
  msgLen = mControlSocket[ARFCN]->read(buffer);

  mControlLock.lock();

  if (msgLen < 1) {
    mControlLock.unlock();
    return;
  }

  char cmdcheck[4];
  char command[MAX_PACKET_LENGTH];
  char response[MAX_PACKET_LENGTH];

  sscanf(buffer,"%3s %s",cmdcheck,command);
 
  writeClockInterface();

  if (strcmp(cmdcheck,"CMD")!=0) {
    LOG(ERR) << "bogus message on control interface";
    mControlLock.unlock();
    return;
  }
  LOG(INFO) << "command is " << buffer;

  if (strcmp(command,"POWEROFF")==0) {
    // turn off transmitter/demod
    sprintf(response,"RSP POWEROFF 0"); 
  }
  else if (strcmp(command,"POWERON")==0) {
    // turn on transmitter/demod
    if (!mTxFreq || !mRxFreq) 
      sprintf(response,"RSP POWERON 1");
    else {
      sprintf(response,"RSP POWERON 0");
      if (!mOn) {
        // Prepare for thread start
        mPower = -20;
        mRadioInterface->start();

        // Start radio interface threads.
        writeClockInterface();
        generateRACHSequence(*gsmPulse,mSamplesPerSymbol);

        mRFIFOServiceLoopThread->start((void * (*)(void*))RFIFOServiceLoopAdapter,(void*) this);
        mFIFOServiceLoopThread->start((void * (*)(void*))FIFOServiceLoopAdapter,(void*) this);

	for (int i = 0; i < mNumARFCNs; i++) {
          ThreadStruct *cs = new ThreadStruct;
          cs->trx = this;
          cs->ARFCN = i;
          mTransmitPriorityQueueServiceLoopThread[i]->start((void * (*)(void*))TransmitPriorityQueueServiceLoopAdapter,(void*) cs);
	  Demodulator *demod = new Demodulator(i,this,mStartTime);
	  mDemodulators[i] = demod;
	  if (mNumARFCNs > 1) mDemodServiceLoopThread[i]->start((void * (*)(void*))DemodServiceLoopAdapter,(void*) demod);
	}

        //mRFIFOServiceLoopThread->start((void * (*)(void*))RFIFOServiceLoopAdapter,(void*) this);
        //mFIFOServiceLoopThread->start((void * (*)(void*))FIFOServiceLoopAdapter,(void*) this);


        mOn = true;
      }
    }
  }
  else if (strcmp(command,"SETMAXDLY")==0) {
    // FIXME -- Use the configuration table instead.
    //set expected maximum time-of-arrival
    int maxDelay;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&maxDelay);
    mMaxExpectedDelay = maxDelay; // 1 GSM symbol is approx. 1 km
    sprintf(response,"RSP SETMAXDLY 0 %d",maxDelay);
  }
  else if (strcmp(command,"SETRXGAIN")==0) {
    // FIXME -- Use the configuration table instead.
    //set expected maximum time-of-arrival
    int newGain;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&newGain);
    newGain = mRadioInterface->setRxGain(newGain);
    sprintf(response,"RSP SETRXGAIN 0 %d",newGain);
  }
  else if (strcmp(command,"NOISELEV")==0) {
    // FIXME -- Use the status table instead.
    if (mOn) {
      sprintf(response,"RSP NOISELEV 0 %d",
              (int) round(20.0*log10(rxFullScale/mDemodulators[0]->getEnergyThreshold())));
    }
    else {
      sprintf(response,"RSP NOISELEV 1  0");
    }
  }   
  else if (strcmp(command,"SETPOWER")==0) {
    // set output power in dB
    int dbPwr;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&dbPwr);
    if (!mOn) 
      sprintf(response,"RSP SETPOWER 1 %d",dbPwr);
    else {
      if (ARFCN==0) {
        mPower = dbPwr;
        mRadioInterface->setPowerAttenuation(dbPwr + gConfig.getNum("TRX.TxAttenOffset"));
      }
      sprintf(response,"RSP SETPOWER 0 %d",dbPwr);
    }
  }
  else if (strcmp(command,"ADJPOWER")==0) {
    // FIXME -- Use the configuration table instead.
    // adjust power in dB steps
    int dbStep;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&dbStep);
    if (!mOn) 
      sprintf(response,"RSP ADJPOWER 1 %d",mPower);
    else {
      if (ARFCN==0) 
        mPower += dbStep;
      sprintf(response,"RSP ADJPOWER 0 %d",mPower);
    }
  }
  else if (strcmp(command,"RXTUNE")==0) {
    // tune receiver
    int freqKhz;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&freqKhz);
    mRxFreq = freqKhz*1.0e3+mFreqOffset;
    if ((ARFCN==0) && !mRadioInterface->tuneRx(mRxFreq,gConfig.getNum("TRX.RadioFrequencyOffset"))) {
       LOG(ALERT) << "RX failed to tune";
       sprintf(response,"RSP RXTUNE 1 %d",freqKhz);
    }
    else
       sprintf(response,"RSP RXTUNE 0 %d",freqKhz);
  }
  else if (strcmp(command,"TXTUNE")==0) {
    // tune txmtr
    int freqKhz;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&freqKhz);
    //freqKhz = 890e3;
    mTxFreq = freqKhz*1.0e3+mFreqOffset;
    if ((ARFCN==0) && !mRadioInterface->tuneTx(mTxFreq,gConfig.getNum("TRX.RadioFrequencyOffset"))) {
       LOG(ALERT) << "TX failed to tune";
       sprintf(response,"RSP TXTUNE 1 %d",freqKhz);
    }
    else
       sprintf(response,"RSP TXTUNE 0 %d",freqKhz);
  }
  else if (strcmp(command,"SETTSC")==0) {
    // set TSC
    int TSC;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&TSC);
    if (mOn)
      sprintf(response,"RSP SETTSC 1 %d",TSC);
    else {
      if (ARFCN==0) {
        mTSC = TSC;
        generateMidamble(*gsmPulse,mSamplesPerSymbol,TSC);
      }
      sprintf(response,"RSP SETTSC 0 %d",TSC);
    }
  }
  else if (strcmp(command,"SETSLOT")==0) {
    // set TSC 
    int  corrCode;
    int  timeslot;
    sscanf(buffer,"%3s %s %d %d",cmdcheck,command,&timeslot,&corrCode);
    //sscanf(buffer,"%3s %s %d %d %d",cmdcheck,command,&timeslot,&corrCode,&ARFCN);
    if ((timeslot < 0) || (timeslot > 7)) {
      LOG(ERR) << "bogus message on control interface";
      sprintf(response,"RSP SETSLOT 1 %d %d",timeslot,corrCode);
    }
    else if ((ARFCN < 0) || (ARFCN >= MAXARFCN)) {
      LOG(ERR) << "bogus message on control interface";
      sprintf(response,"RSP SETSLOT 1 %d %d",timeslot,corrCode);
    }
    else {
      mChanType[ARFCN][timeslot] = (ChannelCombination) corrCode;
      setModulus(timeslot);
      sprintf(response,"RSP SETSLOT 0 %d %d",timeslot,corrCode);
    }
  }
  else {
    LOG(ERR) << "bogus command " << command << " on control interface.";
  }

  mControlSocket[ARFCN]->write(response,strlen(response)+1);

  mControlLock.unlock();


}

bool Transceiver::driveTransmitPriorityQueue(unsigned ARFCN) 
{


#if 1 
  char buffer[gSlotLen+50];

  // check data socket
  size_t msgLen = mDataSocket[ARFCN]->read(buffer);

  mTransmitPriorityQueueLock.lock();

  if (msgLen!=gSlotLen+1+4+1) {
    LOG(ERR) << "badly formatted packet on GSM->TRX interface";
    mTransmitPriorityQueueLock.unlock();
    return false;
  }

  int timeSlot = (int) buffer[0];
  uint64_t frameNum = 0;
  for (int i = 0; i < 4; i++)
    frameNum = (frameNum << 8) | (0x0ff & buffer[i+1]);
  
  /*
  if (GSM::Time(frameNum,timeSlot) >  mTransmitDeadlineClock + GSM::Time(51,0)) {
    // stale burst
    //LOG(DEBUG) << "FAST! "<< GSM::Time(frameNum,timeSlot);
    //writeClockInterface();
    }*/

/*
  DAB -- Just let these go through the demod.
  if (GSM::Time(frameNum,timeSlot) < mTransmitDeadlineClock) {
    // stale burst from GSM core
    LOG(NOTICE) << "STALE packet on GSM->TRX interface at time "<< GSM::Time(frameNum,timeSlot);
    return false;
  }
*/
  
  // periodically update GSM core clock
  //LOG(DEBUG) << "mTransmitDeadlineClock " << mTransmitDeadlineClock
  //		<< " mLastClockUpdateTime " << mLastClockUpdateTime;
  if (mTransmitDeadlineClock > mLastClockUpdateTime + GSM::Time(216,0))
    writeClockInterface();


  //LOG(DEBUG) << "rcvd. burst at: " << GSM::Time(frameNum,timeSlot);
  
  int RSSI = (int) buffer[5];
  static BitVector newBurst(gSlotLen);
  BitVector::iterator itr = newBurst.begin();
  char *bufferItr = buffer+6;
  while (itr < newBurst.end()) 
    *itr++ = *bufferItr++;
  
  GSM::Time currTime = GSM::Time(frameNum,timeSlot);
  
  addRadioVector(newBurst,RSSI,currTime,ARFCN);
  
  //LOG(DEBUG) "added burst - time: " << currTime << ", RSSI: " << RSSI; // << ", data: " << newBurst; 

  mTransmitPriorityQueueLock.unlock();
#else
  RadioClock *radioClock = (mRadioInterface->getClock());

  if (mOn) {
    radioClock->wait(); // wait until clock updates
      // time to push burst to transmit FIFO
      mTransmitPriorityQueueLock.lock();
      for (int i = 0; i < 4; i++)
      {
      int iSlot = radioClock->get().TN(); //mTransmitDeadlineClock.TN();
      static BitVector newBurst(gSlotLen);
      GSM::Time currTime = GSM::Time(mTransmitDeadlineClock.FN()+50,(iSlot+i)%8);
      addRadioVector(newBurst,0,currTime,ARFCN);
      //printf("adding %d %d\n",mTransmitDeadlineClock.FN(),iSlot);     
      }
      mTransmitPriorityQueueLock.unlock();
  }
#endif


  return true;


}
 
void Transceiver::driveReceiveFIFO()
{
  mRadioInterface->driveReceiveRadio();

  pullRadioVector();
}


void Transceiver::driveTransmitFIFO() 
{

  /**
      Features a carefully controlled latency mechanism, to 
      assure that transmit packets arrive at the radio/USRP
      before they need to be transmitted.

      Deadline clock indicates the burst that needs to be
      pushed into the FIFO right NOW.  If transmit queue does
      not have a burst, stick in filler data.
  */


  RadioClock *radioClock = (mRadioInterface->getClock());
  
  if (mOn) {
    radioClock->wait(); // wait until clock updates
    //LOG(DEBUG) << "radio clock " << radioClock->get();
    while (radioClock->get() + mTransmitLatency > mTransmitDeadlineClock) {
      // if underrun, then we're not providing bursts to radio/USRP fast
      //   enough.  Need to increase latency by one GSM frame.
      if (mRadioInterface->isUnderrun()) {
        // only do latency update every 10 frames, so we don't over update
	if (radioClock->get() > mLatencyUpdateTime + GSM::Time(100,0)) {
	  mTransmitLatency = mTransmitLatency + GSM::Time(1,0);
	  LOG(NOTICE) << "new latency: " << mTransmitLatency;
	  mLatencyUpdateTime = radioClock->get();
	}
      }
      else {
        // if underrun hasn't occurred in the last sec (216 frames) drop
        //    transmit latency by a timeslot
	if (mTransmitLatency > GSM::Time(1,1)) {
            if (radioClock->get() > mLatencyUpdateTime + GSM::Time(216,0)) {
	    mTransmitLatency.decTN();
	    LOG(NOTICE) << "reduced latency: " << mTransmitLatency;
	    mLatencyUpdateTime = radioClock->get();
	  }
	}
      }
      // time to push burst to transmit FIFO
      pushRadioVector(mTransmitDeadlineClock);
      mTransmitDeadlineClock.incTN();
    }
    
  }
  // FIXME -- This should not be a hard spin.
  // But any delay here causes us to throw omni_thread_fatal.
  //else radioClock->wait();
}



void Transceiver::writeClockInterface()
{
  char command[50];
  // FIME -- See tracker #315.
  //sprintf(command,"IND CLOCK %llu",(unsigned long long) (mTransmitDeadlineClock.FN()+10));
  sprintf(command,"IND CLOCK %llu",(unsigned long long) (mTransmitDeadlineClock.FN()+2));

  LOG(INFO) << "ClockInterface: sending " << command;

  mClockSocket.write(command,strlen(command)+1);

  mLastClockUpdateTime = mTransmitDeadlineClock;

}   
  
void *FIFOServiceLoopAdapter(Transceiver *transceiver)
{
  while (1) {
    //transceiver->driveReceiveFIFO();
    transceiver->driveTransmitFIFO();
    pthread_testcancel();
  }
  return NULL;
}

void *RFIFOServiceLoopAdapter(Transceiver *transceiver)
{
  bool isMulti = transceiver->multiARFCN();
  while (1) {
    transceiver->driveReceiveFIFO();
    if (!isMulti) transceiver->mDemodulators[0]->driveDemod(true);
    //transceiver->driveTransmitFIFO();
    pthread_testcancel();
  }
  return NULL;
}


void *ControlServiceLoopAdapter(ThreadStruct *ts)
{
  Transceiver *transceiver = ts->trx;
  unsigned ARFCN = ts->ARFCN;
  while (1) {
    transceiver->driveControl(ARFCN);
    pthread_testcancel();
  }
  return NULL;
}

void *DemodServiceLoopAdapter(Demodulator *demodulator)
{
  while(1) {
    demodulator->driveDemod();
    pthread_testcancel();
  }
  return NULL;
}

void *TransmitPriorityQueueServiceLoopAdapter(ThreadStruct *ts)
{
  Transceiver *transceiver = ts->trx;
  unsigned ARFCN = ts->ARFCN;
  while (1) {
    bool stale = false;
    // Flush the UDP packets until a successful transfer.
    while (!transceiver->driveTransmitPriorityQueue(ARFCN)) {
      stale = true; 
    }
    if (stale) {
      // If a packet was stale, remind the GSM stack of the clock.
      transceiver->writeClockInterface();
    }
    pthread_testcancel();
  }
  return NULL;
}


Demodulator::Demodulator(int wARFCN,
			 Transceiver *wTRX,
			 GSM::Time wStartTime) 
{

  assert(wTRX);

  mARFCN = wARFCN;
  mTRX = wTRX;
  mRadioInterface = mTRX->radioInterface();
  mTRXDataSocket = mTRX->dataSocket(mARFCN);
  mSamplesPerSymbol = mTRX->samplesPerSymbol();
  mDemodFIFO = mTRX->demodFIFO(mARFCN);
  signalVector *gsmPulse = mTRX->GSMPulse();
  mTSC = mTRX->getTSC();

  rxFullScale = mRadioInterface->fullScaleOutputValue();

  LOG(DEBUG) << "Creating demodulator for ARFCN " << mARFCN << " with TSC " << mTSC;

  for (unsigned i = 0; i < 8; i++) {
      channelResponse[i] = NULL;
      DFEForward[i] = NULL;
      DFEFeedback[i] = NULL;
      channelEstimateTime[i] = wStartTime;
      mEnergyThreshold = 7.07;
  }

  prevFalseDetectionTime = wStartTime;

}

void Demodulator::driveDemod(bool wSingleARFCN) 
{

  //LOG(DEBUG) << "calling driveDemod ";

  radioVector *demodBurst = NULL;
  SoftVector *rxBurst = NULL;
  int RSSI;
  int TOA;  // in 1/256 of a symbol
  GSM::Time burstTime;

  //RadioClock *radioClock = (mRadioInterface->getClock());
  //radioClock->wait();

  demodBurst = mDemodFIFO->get();
  if (!wSingleARFCN) {
    while  (!demodBurst) {
      RadioClock *radioClock = (mRadioInterface->getClock());
      radioClock->wait();
      demodBurst = mDemodFIFO->get();
    }
  }
  else {
    if (!demodBurst) return;
  }

  mMaxExpectedDelay = mTRX->maxDelay();  

  rxBurst = demodRadioVector(demodBurst,burstTime,RSSI,TOA);

  if (rxBurst) { 

    LOG(DEBUG) << "burst parameters: "
          << " ARFCN: " << mARFCN
	  << " time: " << burstTime
	  << " RSSI: " << RSSI
	  << " TOA: "  << TOA
	  << " bits: " << *rxBurst;
    
    char burstString[gSlotLen+10];
    burstString[0] = burstTime.TN();
    for (int i = 0; i < 4; i++)
      burstString[1+i] = (burstTime.FN() >> ((3-i)*8)) & 0x0ff;
    burstString[5] = RSSI;
    burstString[6] = (TOA >> 8) & 0x0ff;
    burstString[7] = TOA & 0x0ff;
    SoftVector::iterator burstItr = rxBurst->begin();

    for (unsigned int i = 0; i < gSlotLen; i++) {
      burstString[8+i] =(char) round((*burstItr++)*255.0);
    }
    burstString[gSlotLen+9] = '\0';
    delete rxBurst;

    mTRXDataSocket->write(burstString,gSlotLen+10);
  }

}

SoftVector *Demodulator::demodRadioVector(radioVector *rxBurst,
					 GSM::Time &wTime,
					 int &RSSI,
					 int &timingOffset)
{

  bool needDFE = (mMaxExpectedDelay > 1);

  int timeslot = rxBurst->time().TN();
  
  CorrType corrType = mTRX->expectedCorrType(rxBurst->time(),mARFCN);

  //LOG(INFO) << "Demoding ptr " << rxBurst << " at " << rxBurst->time() << " for ARFCN " << mARFCN;

  if ((corrType==OFF) || (corrType==IDLE)) {
    //LOG(DEBUG) << "Illegal burst";
    delete rxBurst;
    return NULL;
  }
 
  // check to see if received burst has sufficient 
  signalVector *vectorBurst = rxBurst;
  //LOG(DEBUG) << "vectorBurst: " << vectorBurst << " rxBurst: " << rxBurst;
  complex amplitude = 0.0;
  float TOA = 0.0;
  float avgPwr = 0.0;
  /*if (!energyDetect(*vectorBurst,20*mSamplesPerSymbol,mEnergyThreshold,&avgPwr)) {
     LOG(DEBUG) << "Estimated Energy: " << sqrt(avgPwr) << ", at time " << rxBurst->time();
     double framesElapsed = rxBurst->time()-prevFalseDetectionTime;
     if (framesElapsed > 50) {  // if we haven't had any false detections for a while, lower threshold
	//mEnergyThreshold -= 1.0;
        prevFalseDetectionTime = rxBurst->time();
     }
     //LOG(INFO) << "Low burst energy.";
     delete rxBurst;
     LOG(INFO) << "Deleting " << rxBurst;
     return NULL;
  }*/
  LOG(DEBUG) << "Estimated Energy: " << sqrt(avgPwr) << ", at time " << rxBurst->time();

  // run the proper correlator
  bool success = false;
  if (corrType==TSC) {
    LOG(DEBUG) << "looking for TSC at time: " << rxBurst->time();
    signalVector *channelResp;
    double framesElapsed = rxBurst->time()-channelEstimateTime[timeslot];
    bool estimateChannel = false;
    //if ((framesElapsed > 50) || (channelResponse[timeslot]==NULL)) {
    {	
	if (channelResponse[timeslot]) delete channelResponse[timeslot];
        if (DFEForward[timeslot]) delete DFEForward[timeslot];
        if (DFEFeedback[timeslot]) delete DFEFeedback[timeslot];
        channelResponse[timeslot] = NULL;
        DFEForward[timeslot] = NULL;
        DFEFeedback[timeslot] = NULL;
	estimateChannel = true;
    }
    estimateChannel = true;
    if (!needDFE) estimateChannel = false;
    float chanOffset;

    success = analyzeTrafficBurst(*vectorBurst,
				  mTSC,
				  3.0,
				  mSamplesPerSymbol,
				  &amplitude,
				  &TOA,
				  mMaxExpectedDelay, 
                                  estimateChannel,
				  &channelResp,
				  &chanOffset);

    if (success) {
      LOG(DEBUG) << "FOUND TSC!!!!!! " << amplitude << " " << TOA;
      //mEnergyThreshold -= 0.1F;
      if (mEnergyThreshold < 0.0) mEnergyThreshold = 0.0;
      SNRestimate[timeslot] = amplitude.norm2()/(mEnergyThreshold*mEnergyThreshold+1.0); // this is not highly accurate
      if (estimateChannel) {
         LOG(DEBUG) << "estimating channel...";
         channelResponse[timeslot] = channelResp;
       	 chanRespOffset[timeslot] = chanOffset;
         chanRespAmplitude[timeslot] = amplitude;
	 scaleVector(*channelResp, complex(1.0,0.0)/amplitude);
         designDFE(*channelResp, SNRestimate[timeslot], 7, &DFEForward[timeslot], &DFEFeedback[timeslot]);
         channelEstimateTime[timeslot] = rxBurst->time();  
         LOG(DEBUG) << "SNR: " << SNRestimate[timeslot] << ", DFE forward: " << *DFEForward[timeslot] << ", DFE backward: " << *DFEFeedback[timeslot];
      }
    }
    else {
      double framesElapsed = rxBurst->time()-prevFalseDetectionTime; 
      LOG(DEBUG) << "wTime: " << rxBurst->time() << ", pTime: " << prevFalseDetectionTime << ", fElapsed: " << framesElapsed;
      //mEnergyThreshold += 0.1F*exp(-framesElapsed);
      prevFalseDetectionTime = rxBurst->time();
      channelResponse[timeslot] = NULL;
    }
  }
  else {
    // RACH burst
    success = detectRACHBurst(*vectorBurst,
			      3.0,  // detection threshold
			      mSamplesPerSymbol,
			      &amplitude,
			      &TOA);
    if (success) {
      LOG(DEBUG) << "FOUND RACH!!!!!! " << amplitude << " " << TOA;
      //mEnergyThreshold -= 0.1F;
      if (mEnergyThreshold < 0.0) mEnergyThreshold = 0.0;
      channelResponse[timeslot] = NULL; 
    }
    else {
      double framesElapsed = rxBurst->time()-prevFalseDetectionTime;
      //mEnergyThreshold += 0.1F*exp(-framesElapsed);
      prevFalseDetectionTime = rxBurst->time();
    }
  }
  LOG(DEBUG) << "energy Threshold = " << mEnergyThreshold; 

  // demodulate burst
  SoftVector *burst = NULL;
  if ((rxBurst) && (success)) {
    if ((corrType==RACH) || (!needDFE)) {
      burst = demodulateBurst(*vectorBurst,
			      *gsmPulse,
			      mSamplesPerSymbol,
			      amplitude,TOA);
    }
    else { // TSC
      scaleVector(*vectorBurst,complex(1.0,0.0)/amplitude);
      burst = equalizeBurst(*vectorBurst,
			    TOA-chanRespOffset[timeslot],
			    mSamplesPerSymbol,
			    *DFEForward[timeslot],
			    *DFEFeedback[timeslot]);
    }
    wTime = rxBurst->time();
    // FIXME:  what is full scale for the USRP?  we get more that 12 bits of resolution...
    RSSI = (int) floor(20.0*log10(rxFullScale/amplitude.abs()));
    LOG(DEBUG) << "RSSI: " << RSSI;
    timingOffset = (int) round(TOA*256.0/mSamplesPerSymbol);
  }

  //if (burst) LOG(DEEPDEBUG) << "burst: " << *burst << '\n';
  LOG(DEBUG) << "Deleting rxBurst";
  delete rxBurst;

  return burst;
}
