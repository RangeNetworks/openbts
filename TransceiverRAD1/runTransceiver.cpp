/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
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


#include <stdlib.h>

#include "Transceiver.h"
#include "RAD1Device.h"
#include "DummyLoad.h"

#include <time.h>
#include <signal.h>

#include <GSMCommon.h>
#include <Logger.h>
#include <Configuration.h>

using namespace std;

ConfigurationTable gConfig("/etc/OpenBTS/OpenBTS.db");


volatile bool gbShutdown = false;
static void ctrlCHandler(int signo)
{
   cout << "Received shutdown signal" << endl;;
   gbShutdown = true;
}


int main(int argc, char *argv[])
{
  if ( signal( SIGINT, ctrlCHandler ) == SIG_ERR )
  {
    cerr << "Couldn't install signal handler for SIGINT" << endl;
    exit(1);
  }

  if ( signal( SIGTERM, ctrlCHandler ) == SIG_ERR )
  {
    cerr << "Couldn't install signal handler for SIGTERM" << endl;
    exit(1);
  }
  // Configure logger.
  gLogInit("transceiver",gConfig.getStr("Log.Level").c_str(),LOG_LOCAL7);

  int numARFCN=1;
  if (argc>1) numARFCN = atoi(argv[1]);

#ifdef SINGLEARFCN
  numARFCN=1;
#endif

  srandom(time(NULL));

  int mOversamplingRate = 1;
  switch(numARFCN) {
   
  case 1: 
	mOversamplingRate = 1;
	break;
  case 2:
	mOversamplingRate = 6;
 	break;
  case 3:
	mOversamplingRate = 8;
	break;
  case 4:
	mOversamplingRate = 12;
	break;
  case 5:
	mOversamplingRate = 16;
	break;
  default:
	break;
  }
  //int mOversamplingRate = numARFCN/2 + numARFCN;
  //mOversamplingRate = 15; //mOversamplingRate*2;
  //if ((numARFCN > 1) && (mOversamplingRate % 2)) mOversamplingRate++;
  RAD1Device *usrp = new RAD1Device(mOversamplingRate*1625.0e3/6.0);
  //DummyLoad *usrp = new DummyLoad(mOversamplingRate*1625.0e3/6.0);
  usrp->make(); 

  RadioInterface* radio = new RadioInterface(usrp,3,SAMPSPERSYM,mOversamplingRate,false,numARFCN);
  Transceiver *trx = new Transceiver(5700,"127.0.0.1",SAMPSPERSYM,GSM::Time(2,0),radio,
				     numARFCN,mOversamplingRate,false);
  trx->receiveFIFO(radio->receiveFIFO());

/*
  signalVector *gsmPulse = generateGSMPulse(2,1);
  BitVector normalBurstSeg = "0000101010100111110010101010010110101110011000111001101010000";
  BitVector normalBurst(BitVector(normalBurstSeg,gTrainingSequence[0]),normalBurstSeg);
  signalVector *modBurst = modulateBurst(normalBurst,*gsmPulse,8,1);
  signalVector *modBurst9 = modulateBurst(normalBurst,*gsmPulse,9,1);
  signalVector *interpolationFilter = createLPF(0.6/mOversamplingRate,6*mOversamplingRate,1);
  signalVector totalBurst1(*modBurst,*modBurst9);
  signalVector totalBurst2(*modBurst,*modBurst);
  signalVector totalBurst(totalBurst1,totalBurst2);
  scaleVector(totalBurst,usrp->fullScaleInputValue());
  double beaconFreq = -1.0*(numARFCN-1)*200e3;
  signalVector finalVec(625*mOversamplingRate);
  for (int j = 0; j < numARFCN; j++) {
	signalVector *frequencyShifter = new signalVector(625*mOversamplingRate);
	frequencyShifter->fill(1.0);
	frequencyShift(frequencyShifter,frequencyShifter,2.0*M_PI*(beaconFreq+j*400e3)/(1625.0e3/6.0*mOversamplingRate));
  	signalVector *interpVec = polyphaseResampleVector(totalBurst,mOversamplingRate,1,interpolationFilter);
	multVector(*interpVec,*frequencyShifter);
	addVector(finalVec,*interpVec); 	
  }
  signalVector::iterator itr = finalVec.begin();
  short finalVecShort[2*finalVec.size()];
  short *shortItr = finalVecShort;
  while (itr < finalVec.end()) {
	*shortItr++ = (short) (itr->real());
	*shortItr++ = (short) (itr->imag());
	itr++;
  }
  usrp->loadBurst(finalVecShort,finalVec.size());
*/
  trx->start();
  //int i = 0;
  while(!gbShutdown) { sleep(1); } //i++; if (i==60) exit(1);}

  cout << "Shutting down transceiver..." << endl;

//  trx->stop();
  delete trx;
//  delete radio;
}
