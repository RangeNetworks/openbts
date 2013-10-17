/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
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



#include "Transceiver.h"
#include "radioDevice.h"
#include "DummyLoad.h"

#include <time.h>
#include <signal.h>

#include <GSMCommon.h>
#include <Logger.h>
#include <Configuration.h>

#define CONFIGDB            "/etc/OpenBTS/OpenBTS.db"

/* Samples-per-symbol for downlink path
 *     4 - Uses precision modulator (more computation, less distortion)
 *     1 - Uses minimized modulator (less computation, more distortion)
 *
 *     Other values are invalid. Receive path (uplink) is always
 *     downsampled to 1 sps
 */
#define SPS                 4

using namespace std;

ConfigurationKeyMap getConfigurationKeys();
ConfigurationTable gConfig(CONFIGDB, 0, getConfigurationKeys());

volatile bool gbShutdown = false;

static void ctrlCHandler(int signo)
{
   cout << "Received shutdown signal" << endl;;
   gbShutdown = true;
}

/*
 * Attempt to open and test the database file before
 * accessing the configuration table. We do this because
 * the global table constructor cannot provide notification
 * in the event of failure.
 */
int testConfig(const char *filename)
{
  int rc, val = 9999;
  sqlite3 *db;
  std::string test = "sadf732zdvj2";

  const char *keys[3] = {
    "Log.Level",
    "TRX.Port",
    "TRX.IP",
  };

  /* Try to open the database  */
  rc = sqlite3_open(filename, &db);
  if (rc || !db) {
    std::cerr << "Config: Database could not be opened" << std::endl;
    return -1;
  } else {
    sqlite3_close(db);
  }

  /* Attempt to set a value in the global config */
  if (!gConfig.set(test, val)) {
    std::cerr << "Config: Failed to set test key - "
              << "permission to access the database?" << std::endl;
    return -1;
  } else {
    gConfig.remove(test);
  }

  /* Attempt to query */
  for (int i = 0; i < 3; i++) {
    try {
      gConfig.getStr(keys[i]); 
    } catch (...) {
      std::cerr << "Config: Failed query on " << keys[i] << std::endl;
      return -1;
    }
  }

  return 0; 
}

int main(int argc, char *argv[])
{
  int trxPort;
  std::string deviceArgs, logLevel, trxAddr;

  if (argc == 3)
  {
    deviceArgs = std::string(argv[2]);
  }
  else
  {
    deviceArgs = "";
  }

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
  if (testConfig(CONFIGDB) < 0) {
    std::cerr << "Config: Database failure" << std::endl;
    return EXIT_FAILURE;
  }

  logLevel = gConfig.getStr("Log.Level");
  trxPort = gConfig.getNum("TRX.Port");
  trxAddr = gConfig.getStr("TRX.IP");
  gLogInit("transceiver", logLevel.c_str(), LOG_LOCAL7);

  srandom(time(NULL));

  RadioDevice *usrp = RadioDevice::make(SPS);
  int radioType = usrp->open(deviceArgs);
  if (radioType < 0) {
    LOG(ALERT) << "Transceiver exiting..." << std::endl;
    return EXIT_FAILURE;
  }

  RadioInterface* radio;
  switch (radioType) {
  case RadioDevice::NORMAL:
    radio = new RadioInterface(usrp, 3, SPS, false);
    break;
  case RadioDevice::RESAMP_64M:
  case RadioDevice::RESAMP_100M:
    radio = new RadioInterfaceResamp(usrp, 3, SPS, false);
    break;
  default:
    LOG(ALERT) << "Unsupported configuration";
    return EXIT_FAILURE;
  }
  if (!radio->init(radioType)) {
    LOG(ALERT) << "Failed to initialize radio interface";
  }

  Transceiver *trx = new Transceiver(trxPort, trxAddr.c_str(),
                                     SPS, GSM::Time(3,0), radio);
  if (!trx->init()) {
    LOG(ALERT) << "Failed to initialize transceiver";
  }
  trx->receiveFIFO(radio->receiveFIFO());
  trx->start();

  while (!gbShutdown) {
    sleep(1);
  }

  cout << "Shutting down transceiver..." << endl;

  delete trx;
}

ConfigurationKeyMap getConfigurationKeys()
{
	ConfigurationKeyMap map;
	ConfigurationKey *tmp;

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

	return map;
}
