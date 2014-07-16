/*
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/

#ifndef _SIPRTP_H_
#define _SIPRTP_H_ 1

#include <Threads.h>
#include <CodecSet.h>
#include <ByteVector.h>
#include "SIPBase.h"

#include <ortp/ortp.h>
#undef WARNING		// The nimrods defined this to "warning"
#undef CR			// This too

extern int gCountRtpSessions;
extern int gCountRtpSockets;

namespace SIP {

typedef ByteVector AudioFrame;

class SipRtp {
	Mutex mRtpLock;
	public:
	/**@name RTP state and parameters. */
	//@{
	unsigned mRTPPort;
	Control::CodecSet mCodec;
	RtpSession * mSession;		///< RTP media session
	unsigned int mTxTime;		///< RTP transmission timestamp in 8 kHz samples
	unsigned int mRxTime;		///< RTP receive timestamp in 8 kHz samples
	uint64_t mRxRealTime;		// In msecs.
	uint64_t mTxRealTime;		// In msecs.
	//@}

	/**@name RFC-2833 DTMF state. */
	//@{
	// (pat) Dont change mDTMF to char.  The unbelievably stupid <<mDTMS will write the 0 directly on the string prematurely terminating it.
	unsigned mDTMF;					///< current DTMF digit, \0 if none
	unsigned mDTMFDuration;		///< duration of DTMF event so far
	unsigned mDTMFStartTime;	///< start time of the DTMF key event
	unsigned mDTMFEnding;		///< Counts number of rtp end events sent; we are supposed to send three.
	//@}

	/** Return RTP session */
	RtpSession * RTPSession() const { return mSession; }

	/** Return the RTP Port being used. */
	unsigned RTPPort() const { return mRTPPort; }

	bool txDtmf();

	/** Set up to start sending RFC2833 DTMF event frames in the RTP stream. */
	bool startDTMF(char key);

	/** Send a DTMF end frame and turn off the DTMF events. */
	void stopDTMF();

	/** Send a vocoder frame over RTP. */
	void txFrame(AudioFrame* frame, unsigned numFlushed);

	/**
		Receive a vocoder frame over RTP.
		@param The vocoder frame
		@return audio data or NULL on error or no data.
	*/
	AudioFrame * rxFrame();
	void initRTP1(const char *d_ip_addr, unsigned d_port, unsigned dialogId);

	virtual string vsdbText() const = 0;
	virtual SipState vgetSipState() const = 0;

	void rtpInit();
	SipRtp() { rtpInit(); }
	void rtpStop();
	// The virtual keyword is not currently needed since we dont use pointers to SipRtp as a base class.
	virtual ~SipRtp() { rtpStop(); }
	void rtpText(std::ostringstream&os) const;
};

};
#endif
