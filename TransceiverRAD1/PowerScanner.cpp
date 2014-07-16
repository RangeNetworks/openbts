/*
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2012, 2013, 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/

#include <stdint.h>
#include <stdio.h>
#include <Logger.h>
#include <Configuration.h>
#include "RAD1Device.h"
#include <Interthread.h>
#include <Scanning.h>
#include <GSMCommon.h>

#include <list>

ConfigurationKeyMap getAllConfigurationKeys();
ConfigurationTable gConfig("/etc/OpenBTS/OpenBTS.db", "PowerScanner", getAllConfigurationKeys());


using namespace std;

typedef unsigned (*FrequencyConverter)(GSM::GSMBand, unsigned);
int scanDirection(RAD1Device *rad, TIMESTAMP &timestamp,             // Physical RAD values
                  GSM::GSMBand band, int startARFCN, int stopARFCN,  // GSM band values
                  FrequencyConverter arfcnToFreqKHz,                 // Frequency determiner
		  SpectrumMap::LinkDirection linkDir,
                  SpectrumMap &spectrumMap);


class ARFCNVector {

  private:

    double mPower;
    unsigned mARFCN;
    float mFreq;
    SpectrumMap::LinkDirection mLinkDir;

  public:

    ARFCNVector(double &wPower, unsigned &wARFCN, float &freq, SpectrumMap::LinkDirection &linkDir) {
        mPower = wPower;
        mARFCN = wARFCN;
        mFreq = freq;
        mLinkDir = linkDir;
    }

    unsigned ARFCN() const { return mARFCN;}

    double power() const {return mPower;}

    SpectrumMap::LinkDirection linkDirection() const { return mLinkDir; }

    float frequency() const { return mFreq; }

    bool operator>(const ARFCNVector &other) const {return mPower < other.mPower;}
};


int main(int argc, char *argv[])
{
  try {
    GSM::GSMBand band = (GSM::GSMBand)gConfig.getNum("GSM.Radio.Band");
    SpectrumMap spectrumMap(gConfig.getStr("PowerScanner.DBPath").c_str());
    int startARFCN, stopARFCN;
    switch (band) {
      case GSM::GSM850: startARFCN=130; stopARFCN=251; break;
      case GSM::EGSM900: startARFCN=0; stopARFCN=124; break;
      case GSM::DCS1800: startARFCN=512; stopARFCN=885; break;
      case GSM::PCS1900: startARFCN=512; stopARFCN=810; break;
      default:
        LOG(ALERT) << "Unsupported GSM Band specified (config key GSM.Radio.Band). Exiting...";
        exit(-1);
    }

    RAD1Device *rad = new RAD1Device(1625.0e3/6.0);
    rad->make(false, 0);
    rad->start();
    TIMESTAMP timestamp = 19000;

    cout << endl << "Scanning Uplink" << endl;
    scanDirection(rad, timestamp, band, startARFCN, stopARFCN, &GSM::uplinkFreqKHz, SpectrumMap::LinkDirection::Up, spectrumMap);
    cout << endl << "Scanning Downlink" << endl;
    scanDirection(rad, timestamp, band, startARFCN, stopARFCN, &GSM::downlinkFreqKHz, SpectrumMap::LinkDirection::Down, spectrumMap);

  } catch (ConfigurationTableKeyNotFound e) {
    cout << "Required configuration parameter " << e.key() << " not defined, exiting.";
    return -1;
  }

  return 0;
}

int scanDirection(RAD1Device *rad, TIMESTAMP &timestamp, GSM::GSMBand band, int startARFCN, int stopARFCN,
    FrequencyConverter arfcnToFreqKHz, SpectrumMap::LinkDirection linkDir, SpectrumMap &spectrumMap)
{
  list<ARFCNVector> results;
  float integrationTime = ((float)gConfig.getNum("PowerScanner.IntegrationTime"))/1000.0F;

  float startFreq = arfcnToFreqKHz(band, startARFCN) * 1000.0F;
  rad->setRxFreq(startFreq);

  rad->updateAlignment(40000);
  rad->updateAlignment(41000);

  rad->setRxGain(gConfig.getNum("PowerScanner.RxGain"));

  for (unsigned ARFCN=startARFCN; ARFCN <= stopARFCN; ARFCN++) {
    float currFreq = arfcnToFreqKHz(band, ARFCN) * 1000.0F;
    double sum = 0.0;
    double num = 0.0;
    rad->setRxFreq(currFreq);

    while (num < integrationTime*270833.33) {
      short readBuf[512*2];
      bool underrun;
      int rd = rad->readSamples(readBuf,512,&underrun,timestamp);
      if (rd) {
	for (int i = 0; i < rd; i++) {
	  uint32_t *wordPtr = (uint32_t *) &readBuf[2*i];
	  *wordPtr = usrp_to_host_u32(*wordPtr);
	  sum += (readBuf[2*i+1]*readBuf[2*i+1] + readBuf[2*i]*readBuf[2*i]);
	  num += 1.0;
	}
      }
      timestamp += rd;
    }

    double currPower = sum/num;
	// HACK
    printf("At freq %f (ARFCN %5d), the average power is %10.2f\n",currFreq,ARFCN,currPower);
    ARFCNVector res(currPower,ARFCN, currFreq, linkDir);
    results.push_back(res);
  }

  for (list<ARFCNVector>::iterator rp=results.begin(); rp!=results.end(); ++rp) {
    double currPower = rp->power();
    unsigned ARFCN = rp->ARFCN();
    float freq = rp->frequency();
    SpectrumMap::LinkDirection linkDir = rp->linkDirection();

    if (currPower==0.0) continue;

    double dBm = 10.0F*log10(currPower) + gConfig.getNum("PowerScanner.dBmOffset");
    spectrumMap.power(band,ARFCN,freq,linkDir,dBm);
  }

  return 0;
}

ConfigurationKeyMap getAllConfigurationKeys()
{
	extern ConfigurationKeyMap getConfigurationKeys();
	ConfigurationKeyMap map = getConfigurationKeys();
	ConfigurationKey *tmp;

	tmp = new ConfigurationKey("PowerScanner.dBmOffset","0",
		"dBm",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"0:100",
		false,
		"Calibrated dBm level corresponding to full scale on the receiver."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("PowerScanner.DBPath","/var/run/PowerScannerResults.db",
		"",
		ConfigurationKey::FACTORY,
		ConfigurationKey::FILEPATH,
		"",
		true,
		"Path to the scanning results database."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("PowerScanner.IntegrationTime","250",
		"milliseconds",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"50:5000",
		false,
		"Power detection integration time in milliseconds."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	tmp = new ConfigurationKey("PowerScanner.RxGain","97",
		"dB",
		ConfigurationKey::FACTORY,
		ConfigurationKey::VALRANGE,
		"0:200",
		false,
		"Receiver gain for the power scanner program, raw value to setRxGain."
	);
	map[tmp->getName()] = *tmp;
	delete tmp;

	return map;
}
