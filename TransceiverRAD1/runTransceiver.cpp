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

ConfigurationTable gConfig("OpenBTS.db");


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

  srandom(time(NULL));

  int mOversamplingRate = 1;
  RAD1Device *usrp = new RAD1Device(mOversamplingRate*1625.0e3/6.0);
  usrp->make(); 

  RadioInterface* radio = new RadioInterface(usrp,3,SAMPSPERSYM,mOversamplingRate,false);
  Transceiver *trx = new Transceiver(5700,"127.0.0.1",SAMPSPERSYM,GSM::Time(2,0),radio);
  trx->receiveFIFO(radio->receiveFIFO());

  trx->start();
  //int i = 0;
  while(!gbShutdown) { sleep(1); } //i++; if (i==60) exit(1);}

  cout << "Shutting down transceiver..." << endl;

//  trx->stop();
  delete trx;
//  delete radio;
}
