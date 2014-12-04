/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

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
#include <FactoryCalibration.h>

using namespace std;

ConfigurationKeyMap getConfigurationKeys();
// (pat) We cannot currently make this a command line option because the ConfigurationTable must be statically inited ahead of other classes, a giant foo-bar. 
static const char *cOpenBTSConfigEnv = "OpenBTSConfigFile";
static const char *configFile = getenv(cOpenBTSConfigEnv)?getenv(cOpenBTSConfigEnv):"/etc/OpenBTS/OpenBTS.db";
ConfigurationTable gConfig(configFile,"transceiver", getConfigurationKeys());
FactoryCalibration gFactoryCalibration;

volatile bool gbShutdown = false;
static void ctrlCHandler(int signo)
{
   LOG(NOTICE) << "Received shutdown signal";
   cout << "Received shutdown signal" << endl;;
   gbShutdown = true;
}


int main(int argc, char *argv[])
{
  if ( signal( SIGINT, ctrlCHandler ) == SIG_ERR )
  {
	LOG(ERR) << "Couldn't install signal handler for SIGINT";
    cerr << "Couldn't install signal handler for SIGINT" << endl;
    exit(1);
  }

  if ( signal( SIGTERM, ctrlCHandler ) == SIG_ERR )
  {
	LOG(ERR) << "Couldn't install signal handler for SIGTERM";
    cerr << "Couldn't install signal handler for SIGTERM" << endl;
    exit(1);
  }
  // Configure logger.
  gLogInit("transceiver",gConfig.getStr("Log.Level").c_str(),LOG_LOCAL7);

  int numARFCN=1;
  // pat 3-2014: If no args, get params from config file.  Why do we even bother making this particular parameter a transceiver argument
  // when the rest of the parameters come from the config file?
  if (argc>1) {
  	numARFCN = atoi(argv[1]);
  } else {
  	if (gConfig.defines("GSM.Radio.ARFCNs")) { numARFCN = gConfig.getNum("GSM.Radio.ARFCNs"); }
  }

  int deviceID = 0;
  // (pat 3-2014) DO NOT CHANGE THIS methodology for using multiple radio boards.  I use it for handover testing.
  // This code should be duplicated in RAD1ping.
  if (gConfig.defines("TRX.RadioNumber")) {
	// (pat 3-2014) If you take out this code (again) without consulting pat.thompson, you will be shot.
	// We must use a config file anyway - see comments ConfigurationTable gConfig.
	// Ideally we wouldn't init gConfig until main(), but we cant yet.
  	deviceID = gConfig.getNum("TRX.RadioNumber");	// 1 based number.
  }

  LOG(NOTICE) << format("transceiver: started with ARFCNs=%d on board number %d port %s with config %s\n",numARFCN,deviceID,gConfig.getStr("TRX.Port").c_str(),configFile);
  if (deviceID > 0) { deviceID--; }		// Config TRX.RadioNumber is 1 based, actual board number is 0 based.


  gFactoryCalibration.readEEPROM(deviceID);
 

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
  usrp->make(false, deviceID); 

  RadioInterface* radio = new RadioInterface(usrp,3,SAMPSPERSYM,mOversamplingRate,false,numARFCN);
  Transceiver *trx = new Transceiver(gConfig.getNum("TRX.Port"),gConfig.getStr("TRX.IP").c_str(),SAMPSPERSYM,GSM::Time(2,0),radio,
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

  LOG(NOTICE) << "Shutting down transceiver...";
  cout << "Shutting down transceiver..." << endl;

//  trx->stop();
  delete trx;
//  delete radio;
}

ConfigurationKeyMap getConfigurationKeys()
{
	ConfigurationKeyMap map;
	ConfigurationKey *tmp;

	tmp = new ConfigurationKey("TRX.IP","127.0.0.1",
		"",
		ConfigurationKey::CUSTOMERWARN,
		ConfigurationKey::IPADDRESS,
		"",
		true,
		"IP address of the transceiver application."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("TRX.Port","5700",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::PORT,
		"",
		true,
		"IP port of the transceiver application."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("TRX.RadioFrequencyOffset","128",
		"~170Hz steps",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"96:160",// educated guess
		true,
		"Fine-tuning adjustment for the transceiver master clock.  "
			"Roughly 170 Hz/step.  "
			"Set at the factory.  "
			"Do not adjust without proper calibration."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("TRX.RadioNumber","0",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"0:9",		// Not likely to have 10 radios on the same computer.  Not likely to have >1
		true,
		"If non-0, use multiple radios on the same cpu, numbered 1-9.  Must change TRX.Port also.  Provide a separate config file for each OpenBTS+Radio combination using the environment variable or --config command line option."
	);
	map[tmp->getName()] = *tmp;
	delete(tmp);

	tmp = new ConfigurationKey("TRX.TxAttenOffset","0",
		"dB of attenuation",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"0:100",// educated guess
		true,
		"Hardware-specific gain adjustment for transmitter, matched to the power amplifier, expessed as an attenuationi in dB.  "
			"Set at the factory.  "
			"Do not adjust without proper calibration."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	// (pat 4-2014) Added this after having problems with "radio dropouts" and the solution was to change this to larger number (5).
	{ ConfigurationKey tmp("TRX.LatencyBumpUp","1",
		"time measured in GSM timeslots",
		ConfigurationKey::DEVELOPER,
		ConfigurationKey::VALRANGE,
		"1:10",
		false,
		"Hardware-specific latency adjustment for transceiver clock.  This affects the speed of upward latency adjustments.  "
	);
	map[tmp.getName()] = tmp;
	}

	return map;
}
