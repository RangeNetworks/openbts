/*
* Copyright 2008 Free Software Foundation, Inc.
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

#ifndef _RAD1_DEVICE_H_
#define _RAD1_DEVICE_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "radioDevice.h"
#include "rnrad1.h"
#include <sys/time.h>
#include <math.h>
#include <string>
#include <iostream>

#include "bytesex.h"

/** A class to handle a USRP rev 4, with a two RFX900 daughterboards */
class RAD1Device: public RadioDevice {

private:

  static const double masterClockRate; ///< the USRP clock rate
  double desiredSampleRate; 	///< the desired sampling rate
  rnrad1Rx* m_uRx;	///< the USRP receiver
  rnrad1Tx* m_uTx;	///< the USRP transmitter
	
  double actualSampleRate;	///< the actual USRP sampling rate
  unsigned int decimRate;	///< the USRP decimation rate

  unsigned long long samplesRead;	///< number of samples read from USRP
  unsigned long long samplesWritten;	///< number of samples sent to USRP

  bool started;			///< flag indicates USRP has started
  bool skipRx;			///< set if USRP is transmit-only.

  static const unsigned int currDataSize_log2 = 21;
  static const unsigned long currDataSize = (1 << currDataSize_log2);
  short *data;
  unsigned long dataStart;
  unsigned long dataEnd;
  TIMESTAMP timeStart;
  TIMESTAMP timeEnd;
  bool isAligned;

  Mutex writeLock;

  short *currData;		///< internal data buffer when reading from USRP
  TIMESTAMP currTimestamp;	///< timestamp of internal data buffer
  unsigned currLen;		///< size of internal data buffer

  TIMESTAMP timestampOffset;       ///< timestamp offset b/w Tx and Rx blocks
  TIMESTAMP latestWriteTimestamp;  ///< timestamp of most recent ping command
  TIMESTAMP pingTimestamp;	   ///< timestamp of most recent ping response
  static const TIMESTAMP PINGOFFSET = 272;  ///< undetermined delay b/w ping response timestamp and true receive timestamp
  unsigned long hi32Timestamp;
  unsigned long lastPktTimestamp;

  double rxGain;

#ifdef SWLOOPBACK 
  short loopbackBuffer[1000000];
  int loopbackBufferSize;
  double samplePeriod; 

  struct timeval startTime;
  struct timeval lastReadTime;
  bool   firstRead;
#endif

  /** Mess of constants used to control various hardware on the USRP */
  static const unsigned POWER_UP = (1 << 7);
  static const unsigned RX_TXN = (1 << 6);
  static const unsigned RX2_RX1N = (1 << 6);
  static const unsigned ENABLE = (1 << 5);
  static const unsigned PLL_LOCK_DETECT = (1 << 2);
  
  /*static const unsigned SPI_ENABLE_TX_A = 0x10;
  static const unsigned SPI_ENABLE_RX_A = 0x20;
  static const unsigned SPI_ENABLE_TX_B = 0x40;
  static const unsigned SPI_ENABLE_RX_B = 0x80;

  static const unsigned SPI_FMT_MSB = (0 << 7);
  static const unsigned SPI_FMT_HDR_0 = (0 << 5);
  */

  static const float    LO_OFFSET;
  //static const float    LO_OFFSET = 4.0e6;

  static const unsigned R_DIV = 13;
  static const unsigned P = 1;
  unsigned CP2;  // 0.62ma reduce loop BW for 1800
  unsigned CP1;
  static const unsigned DIVSEL = 0;
  unsigned DIV2; // changes with GSM band
  unsigned freq_mult; // changes with GSM band
  static const unsigned CPGAIN = 0;
  
  // R-Register Common Values
  static const unsigned R_RSV = 0; // bits 23,22
  static const unsigned BSC = 3;   // bits 21,20 Div by 8 to be safe
  static const unsigned TEST = 0;  // bit 19
  static const unsigned LDP = 1;   // bit 18
  static const unsigned ABP = 2;   // bit 17,16   6ns, 0 = 3ns
  
  // N-Register Common Values
  static const unsigned N_RSV = 0; // bit 7
  
  // Control Register Common Values
  static const unsigned PD = 0;    // bits 21,20   Normal operation
  static const unsigned PL = 1;    // bits 13,12   7.5mA, -6dbm
  static const unsigned MTLD = 1;  // bit 11       enabled
  static const unsigned CPG = 0;   // bit 10       CP setting 1
  static const unsigned CP3S = 0;  // bit 9        Normal
  static const unsigned PDP = 1;   // bit 8        Positive
  static const unsigned MUXOUT = 1;// bits 7:5     Digital Lock Detect
  static const unsigned CR = 0;    // bit 4        Normal
  static const unsigned PC = 0;    // bits 3,2     Core power 15mA

  // ATR register value
  //static const int FR_ATR_MASK_0 = 20;
  //static const int FR_ATR_TXVAL_0 = 21;
  //static const int FR_ATR_RXVAL_0 = 22;

  /** Compute register values to tune daughterboard to desired frequency */
  bool compute_regs(double freq,
		    unsigned *R,
		    unsigned *control,
		    unsigned *N,
		    double *actual_freq);

  /** Set the transmission frequency */
  bool tx_setFreq(double freq, double *actual_freq);

  /** Set the receiver frequency */
  bool rx_setFreq(double freq, double *actual_freq);
  
 public:

  /** Object constructor */
  RAD1Device (double _desiredSampleRate);

  /** Instantiate the USRP */
  bool make(bool skipRx = false); 

  /** Start the USRP */
  bool start();

  /** Stop the USRP */
  bool stop();

  /**
	Read samples from the USRP.
	@param buf preallocated buf to contain read result
	@param len number of samples desired
	@param overrun Set if read buffer has been overrun, e.g. data not being read fast enough
	@param timestamp The timestamp of the first samples to be read
	@param underrun Set if USRP does not have data to transmit, e.g. data not being sent fast enough
	@param RSSI The received signal strength of the read result
	@return The number of samples actually read
  */
  int  readSamples(short *buf, int len, bool *overrun, 
		   TIMESTAMP timestamp = 0xffffffff,
		   bool *underrun = NULL,
		   unsigned *RSSI = NULL);
  /**
        Write samples to the USRP.
        @param buf Contains the data to be written.
        @param len number of samples to write.
        @param underrun Set if USRP does not have data to transmit, e.g. data not being sent fast enough
        @param timestamp The timestamp of the first sample of the data buffer.
        @param isControl Set if data is a control packet, e.g. a ping command
        @return The number of samples actually written
  */
  int  writeSamples(short *buf, int len, bool *underrun, 
		    TIMESTAMP timestamp = 0xffffffff,
		    bool isControl = false);
 
  /** Update the alignment between the read and write timestamps */
  bool updateAlignment(TIMESTAMP timestamp);
  
  /** Set the transmitter frequency */
  bool setTxFreq(double wFreq, double wAdjFreq = 0);

  /** Set the receiver frequency */
  bool setRxFreq(double wFreq, double wAdjFreq = 0);

  /** Returns the starting write Timestamp*/
  TIMESTAMP initialWriteTimestamp(void) { return 40000;}

  /** Returns the starting read Timestamp*/
  TIMESTAMP initialReadTimestamp(void) { return 40000;}

  /** returns the full-scale transmit amplitude **/
  double fullScaleInputValue() {return 32000.0;}

  /** returns the full-scale receive amplitude **/
  double fullScaleOutputValue() {return 9450.0;}

  /** sets the receive chan gain, returns the gain setting **/
  double setRxGain(double dB);

  /** get the current receive gain */
  double getRxGain(void) {return rxGain;}

  /** return maximum Rx Gain **/
  double maxRxGain(void) {return 97.0;}

  /** return minimum Rx Gain **/
  double minRxGain(void) {return 7.0;}

  /** sets the transmit chan gain, returns the gain setting **/
  double setTxGain(double dB);

  /** return maximum Tx Gain **/
  double maxTxGain(void) {return 0.0;}

  /** return minimum Rx Gain **/
  double minTxGain(void) {return -20.0;}


  /** Return internal status values */
  inline double getTxFreq() { return 0;}
  inline double getRxFreq() { return 0;}
  inline double getSampleRate() {return actualSampleRate;}
  inline double numberRead() { return samplesRead; }
  inline double numberWritten() { return samplesWritten;}

};

#endif // _RAD1_DEVICE_H_

