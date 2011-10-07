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

#include "ad9862.h"

using namespace ad9862;


rnrad1Tx::rnrad1Tx (int whichBoard,
	    	unsigned int wInterpRate,
	    	const std::string fpgaFilename = "",
	    	const std::string firmwareFilename = "")
   : rnrad1Core(whichBoard, RAD1_TX_INTERFACE, RAD1_TX_ALTINTERFACE, fpgaFilename, firmwareFilename, false)
{
  mSwMux = 0x8;
  mHwMux = 0x81;
  mInterpRate = wInterpRate;

  mDevHandle = 0;
  mEndptHandle = 0;
  mBytesSeen = 0;
  mEnabled = true;

 // initialize rx specific registers
  bool result=true;
  result &= write9862(REG_TX_PWR_DN,0);
  result &= write9862(REG_TX_A_OFFSET_LO,0);
  result &= write9862(REG_TX_A_OFFSET_HI,0);
  result &= write9862(REG_TX_B_OFFSET_LO,0);
  result &= write9862(REG_TX_B_OFFSET_HI,0);
  result &= write9862(REG_TX_A_GAIN,TX_X_GAIN_COARSE_FULL);
  result &= write9862(REG_TX_B_GAIN,TX_X_GAIN_COARSE_FULL);
  result &= write9862(REG_TX_PGA,0xff); // maximum gain (0 dB)
  result &= write9862(REG_TX_MISC,0);
  result &= write9862(REG_TX_IF,TX_IF_USE_CLKOUT1
                         	  | TX_IF_I_FIRST
                         	  | TX_IF_INV_TX_SYNC
                         	  | TX_IF_2S_COMP
                         	  | TX_IF_INTERLEAVED);
  result &= write9862(REG_TX_DIGITAL,TX_DIGITAL_2_DATA_PATHS
                         	     | TX_DIGITAL_INTERPOLATE_4X);
  result &= write9862(REG_TX_MODULATOR,TX_MODULATOR_DISABLE_NCO
                                       | TX_MODULATOR_COARSE_MODULATION_NONE),
  result &= write9862(REG_TX_NCO_FTW_7_0,0);
  result &= write9862(REG_TX_NCO_FTW_15_8,0);
  result &= write9862(REG_TX_NCO_FTW_23_16,0);
  if (!result) {
    LOG(ERR) << "Failed to init AD9862 TX regs";
    exit(1);
  }

  // Reset the tx path and leave it disabled.
  enable (false);

  sendRqst(VRQ_FPGA_SET_TX_RESET, 1);
  usleep(10);
  sendRqst(VRQ_FPGA_SET_TX_RESET, 0);

  setSampleRateDivisor (4);	// we're using interp x4

  // check fusb buffering parameters
  int blockSize = 4096; //fusb::default_block_size();
  int numBlocks = 128; //std::max (1, fusb::default_buffer_size() / blockSize);

  mDevHandle = fusb::make_devhandle (getHandle(), getContext());
  mEndptHandle = mDevHandle->make_ephandle (RAD1_TX_ENDPOINT, false,
					   blockSize, numBlocks);

  writeFpgaReg(FR_ATR_MASK_0,0);
  writeFpgaReg(FR_ATR_TXVAL_0,0);
  writeFpgaReg(FR_ATR_RXVAL_0,0);
  writeFpgaReg(FR_ATR_MASK_2,0);
  writeFpgaReg(FR_ATR_TXVAL_2,0);
  writeFpgaReg(FR_ATR_RXVAL_2,0);

  // initialize registers that are common to rx and tx
  result=true;
  result &= write9862 (REG_TX_IF,TX_IF_USE_CLKOUT1 | TX_IF_I_FIRST | TX_IF_2S_COMP | TX_IF_INTERLEAVED);
  result &= write9862 (REG_TX_DIGITAL, TX_DIGITAL_2_DATA_PATHS | TX_DIGITAL_INTERPOLATE_4X); 
  if (!result){
    LOG(ERR) << "failed to init AD9862 TX regs";
    exit(1);
  }
  writeHwMuxReg ();
  if (!setInterpRate (mInterpRate)){
    LOG(ERR) << "failed to set interpolation rate";
    exit(1);
  }
  int mux = 0x00000098;
  if (!setMux (mux)){
    LOG(ERR) << "failed to set tx mux";
    exit(1);
  }
  
  mTxModulatorShadow = (TX_MODULATOR_DISABLE_NCO
			| TX_MODULATOR_COARSE_MODULATION_NONE);
  setTxFreq (0);
}

  
bool rnrad1Tx::enable (bool on)
{
  mEnabled = on;
  return (sendRqst(VRQ_FPGA_SET_TX_ENABLE, on) == 0);
}

bool rnrad1Tx::disable ()	// conditional disable, return prev state
{
  bool enabled = enable ();
  if (enabled) enable (false);
  return enabled;
}

void rnrad1Tx::restore (bool on)	// conditional set
{
  if (on != enable ()) enable (on);
}

bool rnrad1Tx::writeHwMuxReg()
{
  bool s = disable ();
  bool ok = writeFpgaReg (FR_TX_MUX, mHwMux | 1);
  restore (s);
  return ok;
}

bool rnrad1Tx::setSampleRateDivisor (unsigned int div)
{
  return writeFpgaReg (FR_TX_SAMPLE_RATE_DIV, div - 1);
}

int rnrad1Tx::write (const void *buf, int len, bool *underrun)
{
  int	r;
  
  if (underrun)
    *underrun = false;
  
  if (len < 0 || (len % 512) != 0) {
    LOG(ERR) << "write: invalid length = " << len;
    return -1;
  }
  
  r = mEndptHandle->write (buf, len);
  if (r > 0)
    mBytesSeen += r;

  // NOTE: may get bogus underrun during first write
  if (underrun != 0 && mBytesSeen >= mBytesPerPoll){
    mBytesSeen = 0;
    *underrun = true;
    unsigned char status;
    if (checkUnderrun(&status) != 1)  
      LOG(ERR) << "Underrun check failed";
    *underrun = status;
  }
  
  return r;
}

bool rnrad1Tx::setPga (int amp, double gain)
{
  if (amp < 0 || amp > 3)
    return false;
  
  gain = std::min(pgaMax(),std::max(pgaMin(), gain));
  
  int intGain = (int) rint((gain - pgaMin()) / pgaDbPerStep());
  
  // 0 and 1 are same, as are 2 and 3
  return write9862(REG_TX_PGA, intGain);
  
}

double rnrad1Tx::pga (int amp) const
{
  if (amp < 0 || amp > 3)
    return READ_FAILED;

  unsigned char v;
  bool ok = read9862 (REG_TX_PGA, &v);
  if (!ok)
    return READ_FAILED;

  return (pgaDbPerStep() * v) + pgaMin();
}

bool rnrad1Tx::writeOE (int value, int mask)
{
  return writeFpgaReg(FR_OE_0,(mask << 16) | (value & 0xffff));
}

bool rnrad1Tx::writeIO (int value, int mask)
{
  return writeFpgaReg(FR_IO_0,(mask << 16) | (value & 0xffff));
}

bool rnrad1Tx::readIO (int *value)
{
  int t;
  int reg = 0 + 1;      // FIXME, *very* magic number (fix in serial_io.v)
  bool ok = readFpgaReg(reg, &t);
  if (!ok)
    return false;
  
  *value = t & 0xffff;                // FIXME, more magic
  return true;
}

int rnrad1Tx::readIO (void)
{
  int   value;
  if (!readIO(&value))
    return READ_FAILED;
  return value;
}


bool rnrad1Tx::writeRefClk(int value)
{
  return writeFpgaReg(FR_TX_A_REFCLK, value);
}

bool rnrad1Tx::writeAuxDac (int dac, int value)
{
  return rnrad1Core::writeAuxDac(dac, value);
}

bool rnrad1Tx::readAuxAdc (int adc, int *value)
{
  return rnrad1Core::readAuxAdc(true, adc, value);
}

int rnrad1Tx::readAuxAdc (int adc)
{
  int retVal;
  rnrad1Tx::readAuxAdc(adc, &retVal);
  return retVal; 
}

rnrad1Tx::~rnrad1Tx ()
{
  delete mEndptHandle;
  delete mDevHandle;

  // initialize registers that are common to rx and tx
  bool result=true;
  result &= write9862(REG_TX_PWR_DN, TX_PWR_DN_TX_DIGITAL | TX_PWR_DN_TX_ANALOG_BOTH);
  result &= write9862(REG_TX_MODULATOR, TX_MODULATOR_DISABLE_NCO | TX_MODULATOR_COARSE_MODULATION_NONE);
}

rnrad1Tx* rnrad1Tx::make(int which_board,
		unsigned int wInterpRate,
		const std::string fpgaFilename = "",
		const std::string firmwareFilename = "")
{
  try {
    rnrad1Tx* u  = new rnrad1Tx(which_board, wInterpRate,
				fpgaFilename, firmwareFilename);
    return u;
  }
  catch (...){
    return NULL;
  }
}


bool rnrad1Tx::setInterpRate (unsigned int rate)
{
  if ((rate & 0x3) || rate < 4 || rate > 512){
    LOG(ERR) << "interpolation rate must be in [4, 512] and a multiple of 4";
    return false;
  }

  mInterpRate = rate;
  setUsbDataRate ((dacRate () / rate) * (2 * sizeof (short)));

  // We're using the interp by 4 feature of the 9862 so that we can
  // use its fine modulator.  Thus, we reduce the FPGA's interpolation rate
  // by a factor of 4.

  bool s = disable();
  bool ok = writeFpgaReg (FR_INTERP_RATE, mInterpRate/4 - 1);
  restore (s);
  return ok;
}

bool rnrad1Tx::setMux (int mux)
{
  mSwMux = mux;
  mHwMux = mux << 4;
  return writeHwMuxReg ();
}

bool rnrad1Tx::setTxFreq (double freq)
{
  
  // split freq into fine and coarse components
  
  double	coarse;
  
  double freq1 = dacRate () / 8; // First coarse frequency
  double freq2 = dacRate () / 4; // Second coarse frequency
  double limit1 = freq1 / 2; // Midpoint of [0 , freq1] range
  double limit2 = (freq1 + freq2) / 2; // Midpoint of [freq1 , freq2] range
  double highLimit = (double)44e6; // Highest meaningful frequency

  if ((freq < -highLimit) || (freq > highLimit))
    return false;

  mTxModulatorShadow &= ~TX_MODULATOR_CM_MASK;
  if (freq < -limit2) {
    coarse = -freq2;
    mTxModulatorShadow |= TX_MODULATOR_COARSE_MODULATION_F_OVER_4;
    mTxModulatorShadow |= TX_MODULATOR_NEG_COARSE_TUNE;
  }
  else if (freq < -limit1) {
    coarse = -freq1;
    mTxModulatorShadow |= TX_MODULATOR_COARSE_MODULATION_F_OVER_8;
    mTxModulatorShadow |= TX_MODULATOR_NEG_COARSE_TUNE;
  }
  else if (freq < limit1) {
    coarse = 0;
  }
  else if (freq < limit2) {
    coarse = freq1;
    mTxModulatorShadow |= TX_MODULATOR_COARSE_MODULATION_F_OVER_8;
  }
  else if (freq <= highLimit) {
    coarse = freq2;
    mTxModulatorShadow |= TX_MODULATOR_COARSE_MODULATION_F_OVER_4;
  }

  
  double fine = freq - coarse;
  double sign = 2.0*(fine > 0.0) - 1.0;
  unsigned int v = (int) rint (fabs (fine) / (double) (dacRate()/4) * pow (2.0, 24.0));
  mTxFreq = coarse + v * (double) (dacRate()/4) / pow (2.0, 24.0) * sign;

  bool ok = true;
  ok &= write9862 (REG_TX_NCO_FTW_23_16, (unsigned char) ((v >> 16) & 0xff));
  ok &= write9862 (REG_TX_NCO_FTW_15_8,  (unsigned char) ((v >> 8) & 0xff));
  ok &= write9862 (REG_TX_NCO_FTW_7_0,   (unsigned char) (v & 0xff));

  mTxModulatorShadow |= TX_MODULATOR_ENABLE_NCO;
  
  if (fine < 0)
    mTxModulatorShadow |= TX_MODULATOR_NEG_FINE_TUNE;
  else
    mTxModulatorShadow &= ~TX_MODULATOR_NEG_FINE_TUNE;

  ok &= write9862 (REG_TX_MODULATOR, mTxModulatorShadow);
  
  return ok;
}


bool rnrad1Tx::start()
{
  if (!rnrad1Core::start ())
    return false;
  
  if (!enable (true)){
    LOG(ERR) << "Can't enable TX"; 
    return false;
  }
  
  if (!mEndptHandle->start ()){
    LOG(ERR) << "Can't start USB TX stream";
    return false;
  }
  
  return true;
}



