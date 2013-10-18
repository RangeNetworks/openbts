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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define USB_LATENCY_INTRVL		10,0

#if USE_UHD
#  define USB_LATENCY_MIN		6,7
#else
#  define USB_LATENCY_MIN		1,1
#endif

/* Number of running values use in noise average */
#define NOISE_CNT			20

Transceiver::Transceiver(int wBasePort,
			 const char *TRXAddress,
			 int wSPS,
			 GSM::Time wTransmitLatency,
			 RadioInterface *wRadioInterface)
	:mDataSocket(wBasePort+2,TRXAddress,wBasePort+102),
	 mControlSocket(wBasePort+1,TRXAddress,wBasePort+101),
	 mClockSocket(wBasePort,TRXAddress,wBasePort+100),
	 mSPSTx(wSPS), mSPSRx(1), mNoises(NOISE_CNT)
{
  GSM::Time startTime(random() % gHyperframe,0);

  mRxServiceLoopThread = new Thread(32768);
  mTxServiceLoopThread = new Thread(32768);
  mControlServiceLoopThread = new Thread(32768);       ///< thread to process control messages from GSM core
  mTransmitPriorityQueueServiceLoopThread = new Thread(32768);///< thread to process transmit bursts from GSM core

  mRadioInterface = wRadioInterface;
  mTransmitLatency = wTransmitLatency;
  mTransmitDeadlineClock = startTime;
  mLastClockUpdateTime = startTime;
  mLatencyUpdateTime = startTime;
  mRadioInterface->getClock()->set(startTime);
  mMaxExpectedDelay = 0;

  txFullScale = mRadioInterface->fullScaleInputValue();
  rxFullScale = mRadioInterface->fullScaleOutputValue();

  mOn = false;
  mTxFreq = 0.0;
  mRxFreq = 0.0;
  mPower = -10;
  mNoiseLev = 0.0;
}

Transceiver::~Transceiver()
{
  sigProcLibDestroy();
  mTransmitPriorityQueue.clear();
}

bool Transceiver::init()
{
  if (!sigProcLibSetup(mSPSTx)) {
    LOG(ALERT) << "Failed to initialize signal processing library";
    return false;
  }

  // initialize filler tables with dummy bursts
  for (int i = 0; i < 8; i++) {
    signalVector* modBurst = modulateBurst(gDummyBurst,
					   8 + (i % 4 == 0),
					   mSPSTx);
    if (!modBurst) {
      sigProcLibDestroy();
      LOG(ALERT) << "Failed to initialize filler table";
      return false;
    }

    scaleVector(*modBurst,txFullScale);
    fillerModulus[i]=26;
    for (int j = 0; j < 102; j++) {
      fillerTable[j][i] = new signalVector(*modBurst);
    }

    delete modBurst;
    mChanType[i] = NONE;
    channelResponse[i] = NULL;
    DFEForward[i] = NULL;
    DFEFeedback[i] = NULL;
    channelEstimateTime[i] = mTransmitDeadlineClock;
  }

  return true;
}
 
radioVector *Transceiver::fixRadioVector(BitVector &burst,
				 int RSSI,
				 GSM::Time &wTime)
{

  // modulate and stick into queue 
  signalVector* modBurst = modulateBurst(burst,
					 8 + (wTime.TN() % 4 == 0),
					 mSPSTx);
  scaleVector(*modBurst,txFullScale * pow(10,-RSSI/10));

  radioVector *newVec = new radioVector(*modBurst,wTime);
  //fillerActive[ARFCN][wTime.TN()] = (ARFCN==0) || (RSSI != 255);

  delete modBurst;
  return newVec;
}

#ifdef TRANSMIT_LOGGING
void Transceiver::unModulateVector(signalVector wVector) 
{
  SoftVector *burst = demodulateBurst(wVector, mSPSTx, 1.0, 0.0);
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

// If force, set the FillerTable regardless of channel.
// If allocate, must allocate a copy of the incoming vector.
void Transceiver::setFiller(radioVector *rv, bool allocate, bool force)
{
	int TN = rv->getTime().TN() & 0x07;	// (pat) Changed to 0x7 from 0x3.
	if (!force && (IGPRS == mChanType[TN])) {
		LOG(INFO) << "setFiller ignored"<<LOGVAR(TN);
		if (!allocate) { delete rv; }
		return;
	}
	LOG(DEBUG) << "setFiller"<<LOGVAR(TN);
	int modFN = rv->getTime().FN() % fillerModulus[TN];
	delete fillerTable[modFN][TN];
	if (allocate) {
		fillerTable[modFN][TN] = new signalVector(*rv);
	} else {
		fillerTable[modFN][TN] = rv;
	}
}

void Transceiver::pushRadioVector(GSM::Time &nowTime)
{

  // dump stale bursts, if any
  while (radioVector* staleBurst = mTransmitPriorityQueue.getStaleBurst(nowTime)) {
    // Even if the burst is stale, put it in the fillter table.
    // (It might be an idle pattern.)
    LOG(NOTICE) << "dumping STALE burst in TRX->USRP interface";
    setFiller(staleBurst,false,false);
  }

  // Everything from this point down operates in one TN period,
  int TN = nowTime.TN();

  radioVector *sendVec = NULL;
  // if queue contains data at the desired timestamp, stick it into FIFO
  bool addFiller = true;
  while (radioVector *next = (radioVector*) mTransmitPriorityQueue.getCurrentBurst(nowTime)) {
    //LOG(DEBUG) << "transmitFIFO: wrote burst " << next << " at time: " << nowTime;
    LOG(DEBUG) << (sendVec?"adding":"sending")<<" burst " << next << " at time: " << nowTime;
    setFiller(next,true,false);
    addFiller = false;
    if (!sendVec) {
      sendVec = next;
    } else {
      addVector(*sendVec,*next);
      delete next;
    }
  }

  // pull filler data, and set it up to be transmitted
  if (addFiller){
    int modFN = nowTime.FN() % fillerModulus[TN];
    radioVector *tmpVec = new radioVector(*fillerTable[modFN][TN],nowTime);
    if (IGPRS == mChanType[TN]) {
      LOG(DEBUG) << (sendVec?"adding":"setting")<<" GPRS filler burst on T" << TN << " FN " << nowTime.FN();
    }
    if (!sendVec) {
      sendVec = tmpVec;
    } else {
      addVector(*sendVec,*tmpVec);
      delete tmpVec;
    }
  }

  //LOG(DEBUG) << "sendVec size: " << sendVec->size();

  // What if sendVec is still NULL?
  // It can't be if there are no NULLs in the filler table.
  mRadioInterface->driveTransmitRadio(*sendVec,false);
  delete sendVec;

}

void Transceiver::setModulus(int timeslot)
{
  switch (mChanType[timeslot]) {
  case NONE:
  case I:
  case II:
  case III:
  case FILL:
  case IGPRS:
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


Transceiver::CorrType Transceiver::expectedCorrType(GSM::Time currTime)
{
  
  unsigned burstTN = currTime.TN();
  unsigned burstFN = currTime.FN();

  switch (mChanType[burstTN]) {
  case NONE:
    return OFF;
    break;
  case FILL:
    return IDLE;
    break;
  case IGPRS:
  case I:
    return TSC;
    /*if (burstFN % 26 == 25) 
      return IDLE;
    else
      return TSC;*/
    break;
  case II:
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

SoftVector *Transceiver::pullRadioVector(GSM::Time &wTime,
				      int &RSSI,
				      int &timingOffset)
{
  bool needDFE = false;
  bool success = false;
  complex amplitude = 0.0;
  float TOA = 0.0, avg = 0.0;

  radioVector *rxBurst = (radioVector *) mReceiveFIFO->get();

  if (!rxBurst) return NULL;

  int timeslot = rxBurst->getTime().TN();

  CorrType corrType = expectedCorrType(rxBurst->getTime());

  if ((corrType==OFF) || (corrType==IDLE)) {
    delete rxBurst;
    return NULL;
  }

  signalVector *vectorBurst = rxBurst;

  energyDetect(*vectorBurst, 20 * mSPSRx, 0.0, &avg);

  // Update noise level
  mNoiseLev = mNoises.avg();
  avg = sqrt(avg);

  // run the proper correlator
  if (corrType==TSC) {
    LOG(DEBUG) << "looking for TSC at time: " << rxBurst->getTime();
    signalVector *channelResp;
    double framesElapsed = rxBurst->getTime()-channelEstimateTime[timeslot];
    bool estimateChannel = false;
    if ((framesElapsed > 50) || (channelResponse[timeslot]==NULL)) {
	if (channelResponse[timeslot]) delete channelResponse[timeslot];
        if (DFEForward[timeslot]) delete DFEForward[timeslot];
        if (DFEFeedback[timeslot]) delete DFEFeedback[timeslot];
        channelResponse[timeslot] = NULL;
        DFEForward[timeslot] = NULL;
        DFEFeedback[timeslot] = NULL;
	estimateChannel = true;
    }
    if (!needDFE) estimateChannel = false;
    float chanOffset;
    success = analyzeTrafficBurst(*vectorBurst,
				  mTSC,
				  5.0,
				  mSPSRx,
				  &amplitude,
				  &TOA,
				  mMaxExpectedDelay, 
				  estimateChannel,
				  &channelResp,
				  &chanOffset);
    if (success) {
      SNRestimate[timeslot] = amplitude.norm2()/(mNoiseLev*mNoiseLev+1.0); // this is not highly accurate
      if (estimateChannel) {
         LOG(DEBUG) << "estimating channel...";
         channelResponse[timeslot] = channelResp;
       	 chanRespOffset[timeslot] = chanOffset;
         chanRespAmplitude[timeslot] = amplitude;
	 scaleVector(*channelResp, complex(1.0,0.0)/amplitude);
         designDFE(*channelResp, SNRestimate[timeslot], 7, &DFEForward[timeslot], &DFEFeedback[timeslot]);
         channelEstimateTime[timeslot] = rxBurst->getTime();  
         LOG(DEBUG) << "SNR: " << SNRestimate[timeslot] << ", DFE forward: " << *DFEForward[timeslot] << ", DFE backward: " << *DFEFeedback[timeslot];
      }
    }
    else {
      channelResponse[timeslot] = NULL;
      mNoises.insert(avg);
    }
  }
  else {
    // RACH burst
    if (success = detectRACHBurst(*vectorBurst, 6.0, mSPSRx, &amplitude, &TOA))
      channelResponse[timeslot] = NULL;
    else
      mNoises.insert(avg);
  }

  // demodulate burst
  SoftVector *burst = NULL;
  if ((rxBurst) && (success)) {
    if ((corrType==RACH) || (!needDFE)) {
      burst = demodulateBurst(*vectorBurst, mSPSRx, amplitude, TOA);
    } else {
      scaleVector(*vectorBurst,complex(1.0,0.0)/amplitude);
      burst = equalizeBurst(*vectorBurst,
			    TOA-chanRespOffset[timeslot],
			    mSPSRx,
			    *DFEForward[timeslot],
			    *DFEFeedback[timeslot]);
    }
    wTime = rxBurst->getTime();
    RSSI = (int) floor(20.0*log10(rxFullScale/avg));
    LOG(DEBUG) << "RSSI: " << RSSI;
    timingOffset = (int) round(TOA * 256.0 / mSPSRx);
  }

  //if (burst) LOG(DEBUG) << "burst: " << *burst << '\n';

  delete rxBurst;

  return burst;
}

void Transceiver::start()
{
  mControlServiceLoopThread->start((void * (*)(void*))ControlServiceLoopAdapter,(void*) this);
}

void Transceiver::reset()
{
  mTransmitPriorityQueue.clear();
  //mTransmitFIFO->clear();
  //mReceiveFIFO->clear();
}

  
void Transceiver::driveControl()
{

  int MAX_PACKET_LENGTH = 100;

  // check control socket
  char buffer[MAX_PACKET_LENGTH];
  int msgLen = -1;
  buffer[0] = '\0';
 
  msgLen = mControlSocket.read(buffer);

  if (msgLen < 1) {
    return;
  }

  char cmdcheck[4];
  char command[MAX_PACKET_LENGTH];
  char response[MAX_PACKET_LENGTH];

  sscanf(buffer,"%3s %s",cmdcheck,command);
 
  writeClockInterface();

  if (strcmp(cmdcheck,"CMD")!=0) {
    LOG(WARNING) << "bogus message on control interface";
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
        mTxServiceLoopThread->start((void * (*)(void*))TxServiceLoopAdapter,(void*) this);
        mRxServiceLoopThread->start((void * (*)(void*))RxServiceLoopAdapter,(void*) this);
        mTransmitPriorityQueueServiceLoopThread->start((void * (*)(void*))TransmitPriorityQueueServiceLoopAdapter,(void*) this);
        writeClockInterface();

        mOn = true;
      }
    }
  }
  else if (strcmp(command,"SETMAXDLY")==0) {
    //set expected maximum time-of-arrival
    int maxDelay;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&maxDelay);
    mMaxExpectedDelay = maxDelay; // 1 GSM symbol is approx. 1 km
    sprintf(response,"RSP SETMAXDLY 0 %d",maxDelay);
  }
  else if (strcmp(command,"SETRXGAIN")==0) {
    //set expected maximum time-of-arrival
    int newGain;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&newGain);
    newGain = mRadioInterface->setRxGain(newGain);
    sprintf(response,"RSP SETRXGAIN 0 %d",newGain);
  }
  else if (strcmp(command,"NOISELEV")==0) {
    if (mOn) {
      sprintf(response,"RSP NOISELEV 0 %d",
              (int) round(20.0*log10(rxFullScale/mNoiseLev)));
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
      mPower = dbPwr;
      mRadioInterface->setPowerAttenuation(dbPwr);
      sprintf(response,"RSP SETPOWER 0 %d",dbPwr);
    }
  }
  else if (strcmp(command,"ADJPOWER")==0) {
    // adjust power in dB steps
    int dbStep;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&dbStep);
    if (!mOn) 
      sprintf(response,"RSP ADJPOWER 1 %d",mPower);
    else {
      mPower += dbStep;
      sprintf(response,"RSP ADJPOWER 0 %d",mPower);
    }
  }
#define FREQOFFSET 0//11.2e3
  else if (strcmp(command,"RXTUNE")==0) {
    // tune receiver
    int freqKhz;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&freqKhz);
    mRxFreq = freqKhz*1.0e3+FREQOFFSET;
    if (!mRadioInterface->tuneRx(mRxFreq)) {
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
    mTxFreq = freqKhz*1.0e3+FREQOFFSET;
    if (!mRadioInterface->tuneTx(mTxFreq)) {
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
      mTSC = TSC;
      generateMidamble(mSPSRx, TSC);
      sprintf(response,"RSP SETTSC 0 %d", TSC);
    }
  }
  else if (strcmp(command,"HANDOVER")==0) {
    int  timeslot;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&timeslot);
    //sscanf(buffer,"%3s %s %d %d %d",cmdcheck,command,&timeslot,&corrCode,&ARFCN);
    if ((timeslot < 0) || (timeslot > 7)) {
      LOG(ERR) << "bogus message on control interface";
      sprintf(response,"RSP HANDOVER 1 %d",timeslot);
    }
    else {
      //mHandoverActive[ARFCN][timeslot] = true;
      //sprintf(response,"RSP HANDOVER 0 %d",timeslot);
      //handover fails! -kurtis
      sprintf(response,"RSP HANDOVER 1 %d",timeslot);
    }
  }
  else if (strcmp(command,"NOHANDOVER")==0) {
    int  timeslot;
    sscanf(buffer,"%3s %s %d",cmdcheck,command,&timeslot);
    //sscanf(buffer,"%3s %s %d %d %d",cmdcheck,command,&timeslot,&corrCode,&ARFCN);
    if ((timeslot < 0) || (timeslot > 7)) {
      LOG(ERR) << "bogus message on control interface";
      sprintf(response,"RSP NOHANDOVER 1 %d",timeslot);
    }
    else {
      //mHandoverActive[ARFCN][timeslot] = false;
      //sprintf(response,"RSP NOHANDOVER 0 %d",timeslot);
      //hanover fails! -kurtis
      sprintf(response,"RSP NOHANDOVER 1 %d",timeslot);
    }
  }
  else if (strcmp(command,"SETSLOT")==0) {
    // set TSC 
    int  corrCode;
    int  timeslot;
    sscanf(buffer,"%3s %s %d %d",cmdcheck,command,&timeslot,&corrCode);
    if ((timeslot < 0) || (timeslot > 7)) {
      LOG(WARNING) << "bogus message on control interface";
      sprintf(response,"RSP SETSLOT 1 %d %d",timeslot,corrCode);
      return;
    }     
    mChanType[timeslot] = (ChannelCombination) corrCode;
    setModulus(timeslot);
    sprintf(response,"RSP SETSLOT 0 %d %d",timeslot,corrCode);

  }
  else if (strcmp(command,"READFACTORY")==0) {
    // TODO: Actually support reading data from various USRPs
    int ret = 0; //fail everything -kurtis
    //sprintf(response,"RSP READFACTORY 0 %d", ret);
    // READFACTORY FAILS
    sprintf(response,"RSP READFACTORY 1 %d", ret);
  }
  else {
    LOG(WARNING) << "bogus command " << command << " on control interface.";
  }

  mControlSocket.write(response,strlen(response)+1);

}

bool Transceiver::driveTransmitPriorityQueue() 
{

  char buffer[gSlotLen+50];

  // check data socket
  size_t msgLen = mDataSocket.read(buffer);

  if (msgLen!=gSlotLen+1+4+1) {
    LOG(ERR) << "badly formatted packet on GSM->TRX interface";
    return false;
  }

  int timeSlot = (int) buffer[0];
  int fillerFlag = timeSlot & SET_FILLER_FRAME;	// Magic flag says this is a filler burst.
  timeSlot = timeSlot & 0x7;
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

  LOG(DEBUG) << "rcvd. burst at: " << GSM::Time(frameNum,timeSlot) <<LOGVAR(fillerFlag);
  
  int RSSI = (int) buffer[5];
  static BitVector newBurst(gSlotLen);
  BitVector::iterator itr = newBurst.begin();
  char *bufferItr = buffer+6;
  while (itr < newBurst.end()) 
    *itr++ = *bufferItr++;
  
  GSM::Time currTime = GSM::Time(frameNum,timeSlot);

  radioVector *newVec = fixRadioVector(newBurst,RSSI,currTime);

  if (false && fillerFlag) {
	setFiller(newVec,false,true);
  } else {
	mTransmitPriorityQueue.write(newVec);
  }
  
  //LOG(DEBUG) "added burst - time: " << currTime << ", RSSI: " << RSSI; // << ", data: " << newBurst; 

  return true;


}
 
void Transceiver::driveReceiveFIFO() 
{

  SoftVector *rxBurst = NULL;
  int RSSI;
  int TOA;  // in 1/256 of a symbol
  GSM::Time burstTime;

  mRadioInterface->driveReceiveRadio();

  rxBurst = pullRadioVector(burstTime,RSSI,TOA);

  if (rxBurst) { 

    LOG(DEBUG) << "burst parameters: "
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

    mDataSocket.write(burstString,gSlotLen+10);
  }

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
    //radioClock->wait(); // wait until clock updates
    LOG(DEBUG) << "radio clock " << radioClock->get();
    while (radioClock->get() + mTransmitLatency > mTransmitDeadlineClock) {
      // if underrun, then we're not providing bursts to radio/USRP fast
      //   enough.  Need to increase latency by one GSM frame.
      if (mRadioInterface->getWindowType() == RadioDevice::TX_WINDOW_USRP1) {
        if (mRadioInterface->isUnderrun()) {
          // only update latency at the defined frame interval
          if (radioClock->get() > mLatencyUpdateTime + GSM::Time(USB_LATENCY_INTRVL)) {
            mTransmitLatency = mTransmitLatency + GSM::Time(1,0);
            LOG(INFO) << "new latency: " << mTransmitLatency;
            mLatencyUpdateTime = radioClock->get();
          }
        }
        else {
          // if underrun hasn't occurred in the last sec (216 frames) drop
          //    transmit latency by a timeslot
          if (mTransmitLatency > GSM::Time(USB_LATENCY_MIN)) {
              if (radioClock->get() > mLatencyUpdateTime + GSM::Time(216,0)) {
              mTransmitLatency.decTN();
              LOG(INFO) << "reduced latency: " << mTransmitLatency;
              mLatencyUpdateTime = radioClock->get();
            }
          }
        }
      }
      // time to push burst to transmit FIFO
      pushRadioVector(mTransmitDeadlineClock);
      mTransmitDeadlineClock.incTN();
    }
  }

  radioClock->wait();
}



void Transceiver::writeClockInterface()
{
  char command[50];
  // FIXME -- This should be adaptive.
  sprintf(command,"IND CLOCK %llu",(unsigned long long) (mTransmitDeadlineClock.FN()+2));

  LOG(INFO) << "ClockInterface: sending " << command;

  mClockSocket.write(command,strlen(command)+1);

  mLastClockUpdateTime = mTransmitDeadlineClock;

}

void *RxServiceLoopAdapter(Transceiver *transceiver)
{
  transceiver->setPriority();

  while (1) {
    transceiver->driveReceiveFIFO();
    pthread_testcancel();
  }
  return NULL;
}

void *TxServiceLoopAdapter(Transceiver *transceiver)
{
  while (1) {
    transceiver->driveTransmitFIFO();
    pthread_testcancel();
  }
  return NULL;
}

void *ControlServiceLoopAdapter(Transceiver *transceiver)
{
  while (1) {
    transceiver->driveControl();
    pthread_testcancel();
  }
  return NULL;
}

void *TransmitPriorityQueueServiceLoopAdapter(Transceiver *transceiver)
{
  while (1) {
    bool stale = false;
    // Flush the UDP packets until a successful transfer.
    while (!transceiver->driveTransmitPriorityQueue()) {
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
