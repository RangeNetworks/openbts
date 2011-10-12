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

#ifndef RNRAD1_H
#define RNRAD1_H

class rnrad1Tx;
class rnrad1Rx;

#include "rnrad1Core.h"


class rnrad1Rx : public rnrad1Core
{
 private:

  unsigned int mDecimRate;
  int mSwMux;
  int mHwMux;
  double mRxFreq;
  
  fusb_devhandle	*mDevHandle;
  fusb_ephandle		*mEndptHandle;
  int			 mBytesSeen;		// how many bytes we've seen
  bool			 mFirstRead;
  bool			 mEnabled;

 protected:
  
  rnrad1Rx (int whichBoard,
	    unsigned int wDecimRate,
	    const std::string fpgaFilename,
	    const std::string firmwareFilename);

  bool writeHwMuxReg();
  bool enable(bool on);
  bool enable () const { return mEnabled; }

  bool disable();		// conditional disable, return prev state
  void restore(bool on);	// conditional set

public:

  ~rnrad1Rx ();

  static rnrad1Rx* make(int whichBoard,
			unsigned int wDecimRate,
			const std::string fpgaFilename,
			const std::string firmwareFilename);

  bool setDecimRate (unsigned int rate);

  bool setMux (int mux);

  bool setRxFreq (double freq);

  double rxFreq(void) {return mRxFreq;}

  bool start();

  bool setSampleRateDivisor (unsigned int div);

  int read (void *buf, int len, bool *overrun);

  long adcRate() const { return fpgaMasterClockFreq(); }

  bool setPga (int which_amp, double gain_in_db);
  double pga (int which_amp) const;
  double pgaMin () const {return 0.0;}
  double pgaMax () const {return 20.0;}
  double pgaDbPerStep () const {return 20.0/20.0;}

  bool writeOE (int value, int mask);
  bool writeIO (int value, int mask);
  bool readIO (int *value);
  int  readIO (void);
  bool writeRefClk(int value);

  bool writeAuxDac (int which_dac, int value);
  bool readAuxAdc (int which_adc, int *value);
  int  readAuxAdc (int which_adc);

  int blockSize() const;
};


class rnrad1Tx: public rnrad1Core
{

 public:

 private:
  fusb_devhandle	*mDevHandle;
  fusb_ephandle		*mEndptHandle;
  int			 mBytesSeen;		// how many bytes we've seen
  bool			 mFirstWrite;
  bool			 mEnabled;
  
  unsigned int mInterpRate;
  int mSwMux;
  int mHwMux;
  double mTxFreq;
  unsigned char mTxModulatorShadow;

 protected:

  rnrad1Tx (int which_board,
	    unsigned int wInterpRate,
	    const std::string fpgaFilename,
	    const std::string firmwareFilename);

  bool enable (bool on);
  bool enable () const { return mEnabled; }
  
  bool disable();		// conditional disable, return prev state
  void restore(bool on);	// conditional set
  
  bool writeHwMuxReg();

public:
  
  bool setSampleRateDivisor (unsigned int div);

  int write (const void *buf, int len, bool *underrun);

  long dacRate() const { return 2*fpgaMasterClockFreq(); }

  bool setPga (int which_amp, double gain_in_db);
  double pga (int which_amp) const;
  double pgaMin () const {return -20.0;}
  double pgaMax () const {return 0.0;}
  double pgaDbPerStep () const {return 20.0/255.0;}

  bool writeOE (int value, int mask);
  bool writeIO (int value, int mask);
  bool readIO (int *value);
  int readIO (void);
  bool writeRefClk(int value);

  bool writeAuxDac (int which_dac, int value);
  bool readAuxAdc (int which_adc, int *value);
  int readAuxAdc (int which_adc);

  int blockSize() const;

  ~rnrad1Tx ();

  static rnrad1Tx* make(int which_board,
			unsigned int wInterpRate,
			const std::string fpga_filename,
			const std::string firmware_filename);

  bool setInterpRate (unsigned int rate);

  bool setMux (int mux);

  bool setTxFreq (double freq);

  double txFreq(void) {return mTxFreq;}

  bool start();
};


#endif
