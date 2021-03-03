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

ConfigurationTable gConfig("/etc/OpenBTS/OpenBTS.db");

using namespace std;

int main(int argc, char *argv[]) {

  // Configure logger.
  if (argc>1) gLogInit(argv[1]);
  else gLogInit("DEBUG");

  int deviceID = 0;
  if (argc>2) {
  	deviceID = atoi(argv[2]);
  }

  gLogInit("openbts",argv[1],LOG_LOCAL7);


  //if (argc>2) gSetLogFile(argv[2]);

  RAD1Device *usrp = new RAD1Device(52.0e6/192.0);

  usrp->make(false, deviceID);

  TIMESTAMP timestamp;

  if (!usrp->setTxFreq(925.2e6,113)) printf("TX failed!");
  if (!usrp->setRxFreq(925.2e6,113)) printf("RX failed!");

  usrp->start();

  usrp->setRxGain(57);

  LOG(INFO) << "Looping...";
  bool underrun;

  short data[]={0x00,0x02};

  usrp->updateAlignment(20000);
  usrp->updateAlignment(21000);

  int numpkts = 1;
  short data2[512*2*numpkts];
  for (int i = 0; i < 512*numpkts; i++) {
    data2[i<<1] = 30000;//4096*cos(2*3.14159*(i % 126)/126);
    data2[(i<<1) + 1] = 30000;//4096*sin(2*3.14159*(i % 126)/126);
  }

  for (int i = 0; i < 1; i++) 
    usrp->writeSamples((short*) data2,512*numpkts,&underrun,102000+i*1000);

  timestamp = 19000;
  double sum = 0.0;
  unsigned long num = 0;
  while (1) {
    short readBuf[512*2];
    printf("reading data...\n");
    int rd = usrp->readSamples(readBuf,512,&underrun,timestamp);
    if (rd) {
      LOG(INFO) << "rcvd. data@:" << timestamp;
      float pwr = 0;
      for (int i = 0; i < 512; i++) {
        uint32_t *wordPtr = (uint32_t *) &readBuf[2*i];
        *wordPtr = usrp_to_host_u32(*wordPtr); 
	printf ("%llu: %d %d\n", timestamp+i,readBuf[2*i],readBuf[2*i+1]);
        sum += (readBuf[2*i+1]*readBuf[2*i+1] + readBuf[2*i]*readBuf[2*i]);
        pwr += (readBuf[2*i+1]*readBuf[2*i+1] + readBuf[2*i]*readBuf[2*i]);
        num++;
        //if (num % 10000 == 0) printf("avg pwr: %f\n",sum/num);
      }
      printf("For %llu to %llu, power is %f\n",timestamp,timestamp+511,pwr);
      timestamp += rd;
      //usrp->writeSamples((short*) data2,512*numpkts,&underrun,timestamp+1000);
    }
  }

}
