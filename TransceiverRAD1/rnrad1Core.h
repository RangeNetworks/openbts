/*
* Copyright 2011 Free Software Foundation, Inc.
*
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

#ifndef __RNRAD1CORE_H__
#define __RNRAD1CORE_H__

#include "commands.h"
#include "fpga_regs.h"
#include "fusb.h"
#include "interfaces.h"
#include "spi.h"
#include "ids.h"
#include "ad9862.h"

#include <stdexcept>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <cstdio>
#include <stdlib.h>

#include "Logger.h"

const int RAD1_HASH_SIZE = 16;

int usbMsg (struct libusb_device_handle *udh,
	       int request, int value, int index,
	    unsigned char *bytes, int len);

bool rad1GetHash (libusb_device_handle *udh, int which,
		  unsigned char hash[RAD1_HASH_SIZE]);

bool rad1SetHash (libusb_device_handle *udh, int which,
		  const unsigned char hash[RAD1_HASH_SIZE]);

bool rad1LoadFirmware (libusb_device_handle *udh, const char *filename,
			      unsigned char hash[RAD1_HASH_SIZE]);

bool rad1LoadFpga (libusb_device_handle *udh, const char *filename,
			  unsigned char hash[RAD1_HASH_SIZE]);

bool rad1_load_standard_bits (int nth, bool force,
			      const std::string fpga_filename,
			      const std::string firmware_filename,
			      libusb_context *ctx);

class rnrad1Core
{

protected:
  libusb_device_handle		*mudh;
  struct libusb_context		*mctx;
  int				 mUsbDataRate;	// bytes/sec
  int				 mBytesPerPoll;	// how often to poll for overruns
  long   			 mFpgaMasterClockFreq;

  static const int		 MAX_REGS = 128;
  unsigned int			 mFpgaShadows[MAX_REGS];

public: 
  rnrad1Core(int which_board,
	     int interface,
             int altinterface,
	      const std::string fpgaFilename,
	      const std::string firmwareFilename,
	     bool skipLoad);

  libusb_device_handle* getHandle() { return mudh;}

  libusb_context* getContext() {return mctx;}

  int sendRqst (int request, bool flag);

  int checkUnderrun (unsigned char *status);

  int checkOverrun (unsigned char *status);

  bool writeAuxDac (int dac, int value);

  bool readAuxAdc (bool isTx, int adc, int *value);

  int readAuxAdc (bool isTx, int adc);

  ~rnrad1Core();

  long fpgaMasterClockFreq () const { return mFpgaMasterClockFreq; }

  void setFpgaMasterClockFreq (long master_clock) { mFpgaMasterClockFreq = master_clock; }

  int usbDataRate () const { return mUsbDataRate; }

  void setUsbDataRate (int rate); 

  static const int READ_FAILED = -99999;

  bool writeEeprom (int i2cAddr, int eepromOffset, const std::string buf);

  std::string readEeprom (int i2cAddr, int eepromOffset, int len);

  bool writeI2c (int i2cAddr, unsigned char *buf, int len);

  bool readI2c (int i2cAddr, unsigned char *buf, int len);

  bool setAdcOffset (int adc, int offset);

  bool setDacOffset (int dac, int offset, int pin);

  bool setAdcBufferBypass (int adc, bool bypass);

  bool setDcOffsetClEnable(int bits, int mask);

  bool setLed (int which_led, bool on);

  bool writeFpgaReg (int regno, int value);	//< 7-bit regno, 32-bit value

  bool readFpgaReg (int regno, int *value);	//< 7-bit regno, 32-bit value

  int  readFpgaReg (int regno);

  bool write9862 (int regno, unsigned char value);

  bool read9862 (int regno, unsigned char *value) const;

  int  read9862 (int regno) const;

  bool writeSpi (int optionalHeader, int enables, int format, unsigned char *buf, int len);

  bool readSpi (int optionalHeader, int enables, int format, unsigned char *buf, int len) const;

  bool start ();

};

#endif
