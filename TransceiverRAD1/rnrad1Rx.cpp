/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
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

#include "rnrad1.h"

using namespace ad9862;

rnrad1Rx::rnrad1Rx (int whichBoard,
		    unsigned int wDecimRate,
		    const std::string fpgaFilename = "",
		    const std::string firmwareFilename = "")
  : rnrad1Core(whichBoard,RAD1_RX_INTERFACE,RAD1_RX_ALTINTERFACE,fpgaFilename,firmwareFilename,false)
{

  mDevHandle = 0;
  mEndptHandle = 0;
  mBytesSeen = 0;
  mEnabled = false;
  mDecimRate = wDecimRate;

  // initialize rx specific registers
  // initialize registers that are common to rx and tx
  bool result=true;
  result &= write9862(REG_RX_PWR_DN,0);
  result &= write9862(REG_RX_A,0);      // minimum gain = 0x00 (max gain = 0x14)
  result &= write9862(REG_RX_B,0);      // minimum gain = 0x00 (max gain = 0x14)
  result &= write9862(REG_RX_MISC,RX_MISC_HS_DUTY_CYCLE | RX_MISC_CLK_DUTY);
  result &= write9862(REG_RX_IF,RX_IF_USE_CLKOUT1 | RX_IF_2S_COMP);
  result &= write9862(REG_RX_DIGITAL,RX_DIGITAL_2_CHAN);
  if (!result) {
    LOG(ERR) << "Failed to init AD9862 RX regs";
    exit(1);
  }

  // Reset the rx path and leave it disabled.
  enable (false);

  sendRqst(VRQ_FPGA_SET_RX_RESET, 1);
  usleep(10);
  sendRqst(VRQ_FPGA_SET_RX_RESET, 0);

  setSampleRateDivisor (2);	// usually correct

  setDcOffsetClEnable(0xf, 0xf);	// enable DC offset removal control loops

  // check fusb buffering parameters
  int blockSize = 4096; //fusb::default_block_size();
  int numBlocks = 128; //std::max (1, fusb::default_buffer_size() / blockSize);

  mDevHandle = fusb::make_devhandle (getHandle(), getContext());
  mEndptHandle = mDevHandle->make_ephandle (RAD1_RX_ENDPOINT, true,
					   blockSize, numBlocks);

  writeFpgaReg(FR_ATR_MASK_1,0);
  writeFpgaReg(FR_ATR_TXVAL_1,0);
  writeFpgaReg(FR_ATR_RXVAL_1,0);
  writeFpgaReg(FR_ATR_MASK_3,0);
  writeFpgaReg(FR_ATR_TXVAL_3,0);
  writeFpgaReg(FR_ATR_RXVAL_3,0);

  mSwMux = 0;
  mHwMux = 0;

  writeFpgaReg(FR_RX_FORMAT,0x00000300);
  writeHwMuxReg();
  setDecimRate(mDecimRate);
  unsigned int mux = 0x00000010;
  setMux(mux);
  writeFpgaReg(FR_MODE, 0);
  setRxFreq(0);
}


rnrad1Rx::~rnrad1Rx()
{
  enable (false);
  
  delete mEndptHandle;
  delete mDevHandle;
  
  // initialize registers that are common to rx and tx
  bool result= write9862(REG_RX_PWR_DN,0x1);
  
}


bool rnrad1Rx::writeHwMuxReg()
{
  bool s = disable();
  bool ok = writeFpgaReg (FR_RX_MUX, mHwMux | 1);
  restore (s);
  return ok;
}

bool rnrad1Rx::setRxFreq (double freq)
{
  int   v = (int) rint (freq / (double) adcRate() * pow (2.0, 32.0));
  mRxFreq = v * (double) adcRate() / pow (2.0, 32.0);
  return writeFpgaReg (FR_RX_FREQ_0, v);
}

rnrad1Rx *rnrad1Rx::make(int whichBoard,
			 unsigned int wDecimRate,
			 const std::string fpgaFilename = "",
			 const std::string firmwareFilename = "")
{
  try {
    rnrad1Rx *u = new rnrad1Rx(whichBoard,
			       wDecimRate,
			       fpgaFilename,
			       firmwareFilename);
    return u;
  }
  catch (...) {
    return NULL;
  }
}

bool rnrad1Rx::setDecimRate (unsigned int rate)
{
  if ((rate & 0x1) || rate < 4 || rate > 256){
      LOG(ERR) << "decimation rate must be EVEN and in [4, 256]";
      return false;
    }

  mDecimRate = rate;
  setUsbDataRate ((adcRate()/rate) * (2 * sizeof (short)));

  bool s = disable ();
  int v = mDecimRate/2 - 1;
  bool ok = writeFpgaReg (FR_DECIM_RATE, v);
  restore (s);
  return ok;
}

bool rnrad1Rx::setMux (int mux)
{
  int mHwMux = 0;
  for (int i = 0; i < 8; i++){
    int t = (mux >> (4 * i)) & 0x3;
    mHwMux |= t << (2 * i + 4);
  }
  mSwMux = mux;
  return writeHwMuxReg ();
}


bool rnrad1Rx::start()
{
  if (!rnrad1Core::start ())	// invoke parent's method
    return false;
  
  // fire off reads before asserting rx_enable
  
  if (!mEndptHandle->start ()){
    LOG(ERR) << "Can't start USB RX stream";
    return false;
  }

  if (!enable (true)){
    LOG(ERR) << "Can't enable RX";
    return false;
  }

  return true;
}

bool rnrad1Rx::setSampleRateDivisor (unsigned int div)
{
  return writeFpgaReg (FR_RX_SAMPLE_RATE_DIV, div - 1);
}

int rnrad1Rx::read (void *buf, int len, bool *overrun)
{
  int	r;
  
  if (overrun) *overrun = false;
  
  if (len < 0 || (len % 512) != 0){
    LOG(ERR) << "read: invalid length = " << len;
    return -1;
  }
  
  r = mEndptHandle->read (buf, len);
  if (r > 0) mBytesSeen += r;
  
  if (overrun != 0 && mBytesSeen >= mBytesPerPoll){
    mBytesSeen = 0;
    *overrun = true;
    unsigned char status;
    if (checkOverrun(&status) != 1)
      LOG(ERR) << "Overrun check failed";
    *overrun = status;
  }

  return r;
}

bool rnrad1Rx::enable (bool on)
{
  mEnabled = on;
  return (sendRqst(VRQ_FPGA_SET_RX_ENABLE, on) == 0);
}

// conditional disable, return prev state
bool rnrad1Rx::disable ()
{
  bool enabled = enable ();
  if (enabled) enable (false);
  return enabled;
}

// conditional set
void rnrad1Rx::restore (bool on)
{
  if (on != enable ()) enable (on);
}

bool rnrad1Rx::setPga (int amp, double gain)
{
  if (amp < 0 || amp > 1)
    return false;
  
  gain = std::min(pgaMax(), std::max(pgaMin(), gain));
  int intGain = (int) rint((gain - pgaMin()) / pgaDbPerStep());
  int reg = (amp & 1 == 0) ? REG_RX_A : REG_RX_B;
  // read current value to get input buffer bypass flag.
  unsigned char curRx;
  if (!read9862(reg, &curRx))
    return false;
  
  curRx = (curRx & RX_X_BYPASS_INPUT_BUFFER) | (intGain & 0x7f);
  return write9862(reg, curRx);
  
}

double rnrad1Rx::pga (int amp) const
{
  if (amp < 0 || amp > 1) return READ_FAILED;
  int reg = (amp & 1 == 0) ? REG_RX_A : REG_RX_B;
  unsigned char v;
  if (!read9862 (reg, &v)) return READ_FAILED;
  return (pgaDbPerStep() * (v & 0x1f)) + pgaMin();
}

bool rnrad1Rx::writeOE (int value, int mask) 
{
  return writeFpgaReg(FR_OE_1, (mask << 16) | (value & 0xffff));
}

bool rnrad1Rx::writeIO (int value, int mask)
{
  return writeFpgaReg(FR_IO_1, (mask << 16) | (value & 0xffff));
}

bool rnrad1Rx::readIO (int *value)
{
  int t;
  int reg = 0 + 1;      // FIXME, *very* magic number (fix in serial_io.v)
  if (!readFpgaReg(reg, &t)) return false;
  *value = (t >> 16) & 0xffff;        // FIXME, more magic
  return true;
}

int rnrad1Rx::readIO (void)
{
  int   value;
  if (!readIO(&value)) return READ_FAILED;
  return value;
}

bool rnrad1Rx::writeRefClk(int value)
{
  return writeFpgaReg(FR_RX_A_REFCLK, value);
}

bool rnrad1Rx::writeAuxDac (int dac, int value)
{
  return rnrad1Core::writeAuxDac(dac, value);
}

bool rnrad1Rx::readAuxAdc (int adc, int *value)
{
  return rnrad1Core::readAuxAdc(false, adc, value);
}

int  rnrad1Rx::readAuxAdc (int adc)
{
  int retVal;
  rnrad1Rx::readAuxAdc(adc, &retVal);
  return retVal;
}

int rnrad1Rx::blockSize() const { return mEndptHandle->block_size(); }


