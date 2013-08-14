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
