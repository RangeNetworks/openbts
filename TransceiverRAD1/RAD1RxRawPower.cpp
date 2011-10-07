/*
* Copyright 2008, 2009, 2011 Free Software Foundation, Inc.
* Copyright 2011 Range Newtworks, Inc.
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



#include <stdint.h>
#include <stdio.h>
#include <Logger.h>
#include <Configuration.h>
#include "RAD1Device.h"

ConfigurationTable gConfig;

using namespace std;

int main(int argc, char *argv[]) {

  gLogInit("openbts","INFO",LOG_LOCAL7);

  int whichBoard = 0;
  if (argc > 1) whichBoard = atoi(argv[1]);
  //if (argc>2) gSetLogFile(argv[2]);

  RAD1Device *rad1 = new RAD1Device(52.0e6/192.0);

  rad1->make();

  double freq = 0.0;
  if (argc > 2) freq = (double) atoi(argv[2]);

  TIMESTAMP timestamp;

  if (!rad1->setRxFreq(freq*1.0e6,118)) printf("RX failed!");

  rad1->start();

  rad1->setRxGain(47);

  bool underrun;

  rad1->updateAlignment(20000);
  rad1->updateAlignment(21000);

  timestamp = 30000;
  double sum = 0.0;
  unsigned long num = 0;
  
  double rcvCeil = rad1->fullScaleOutputValue()*rad1->fullScaleOutputValue();

  while (1) {
    short readBuf[512*2];
    printf("reading data...\n");
    int rd = rad1->readSamples(readBuf,512,&underrun,timestamp);
    if (rd) {
      LOG(INFO) << "rcvd. data@:" << timestamp;
      float pwr = 0;
      for (int i = 0; i < 512; i++) {
        sum += (readBuf[2*i+1]*readBuf[2*i+1] + readBuf[2*i]*readBuf[2*i]);
        pwr += (readBuf[2*i+1]*readBuf[2*i+1] + readBuf[2*i]*readBuf[2*i]);
        num++;
        if (num % 10000 == 0) printf("RSSI: %f\n",10*log10(rcvCeil/(sum/(double) num)));
      }
      timestamp += rd;
    }
  }

}
