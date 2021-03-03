/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
* Copyright 2014 Range Networks, Inc.
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

/* tomr had to add direct path to OpenBTS.db */
ConfigurationTable gConfig("/etc/OpenBTS/OpenBTS.db");

using namespace std;

int main(int argc, char *argv[]) {

  gLogInit("openbts","INFO",LOG_LOCAL7);

  int whichBoard = 0;
  if (argc > 1) whichBoard = atoi(argv[1]);
  //if (argc>2) gSetLogFile(argv[2]);

  RAD1Device *usrp = new RAD1Device(52.0e6/192.0);

  usrp->make(false, 0);

  double freqkHz = 0.0;
  if (argc > 2) freqkHz = (double) atoi(argv[2]);

  TIMESTAMP timestamp;

  if (!usrp->setRxFreq(freqkHz*1.0e3,108)) printf("RX failed!");

  usrp->start();

  /* tomr added default gain value to 53 and 3rd arg for setting an alt value */
  unsigned int rxgain = 53;

  if (argc > 3) {
        rxgain = atoi(argv[3]);
        printf("Updated RxGain = %d\n", rxgain);
  } else 
	printf("Deafult RxGain Setting = %d\n", rxgain);

  usrp->setRxGain(rxgain);

  bool movingAverage = false;
  if (argc > 4) {
	movingAverage = (atoi(argv[4])!=0);
  }
  printf("Moving average = %d\n",movingAverage);

  int numOfSamples = -1;
  if (argc > 5) {
    numOfSamples = atoi(argv[5]);
  }
  int currSampleNum = 0;

  bool underrun;

  usrp->updateAlignment(20000);
  usrp->updateAlignment(21000);

  timestamp = 30000;
  double sum = 0.0;
  unsigned long num = 0;
  
  double rcvCeil = usrp->fullScaleOutputValue()*usrp->fullScaleOutputValue();

  while (1) {
    short readBuf[512*2];
    int rd = usrp->readSamples(readBuf,512,&underrun,timestamp);
    if (rd) {
      LOG(INFO) << "rcvd. data@:" << timestamp;
      for (int i = 0; i < 512; i++) {
        uint32_t *wordPtr = (uint32_t *) &readBuf[2*i];
        *wordPtr = usrp_to_host_u32(*wordPtr); 
	//printf ("%llu: %d %d\n", timestamp+i,readBuf[2*i],readBuf[2*i+1]);
        sum += (readBuf[2*i+1]*readBuf[2*i+1] + readBuf[2*i]*readBuf[2*i]);
        num++;
        if (num % 10000 == 0) {
		printf("RSSI: %f\n",10*log10(rcvCeil/(sum/(double) num)));
		  usrp->setRxGain(rxgain);

		if (movingAverage) {
		   sum = 0;
		   num = 0;
		}

          if (numOfSamples > 0) {
            if ((currSampleNum + 1) < numOfSamples) {
              currSampleNum++;
            } else {
              return 0;
            }
          }
	}
      }
      timestamp += rd;
    }
  }

  return 0;
}
