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

#define LOG_GROUP LogGroup::SIP		// Can set Log.Level.SIP for debugging
#include <config.h>
#include <OpenBTSConfig.h>
#include "SIPRtp.h"
#include "SIPBase.h"
#include "SIP2Interface.h"

#include <ortp/telephonyevents.h>
#undef WARNING		// The nimrods defined this to "warning"
#undef CR			// This too

// Type is RtpCallback, but args determined by rtp_signal_table_emit2
extern "C" {
	void ourRtpTimestampJumpCallback(RtpSession *session, unsigned long timestamp,unsigned long dialogid)
	{
		SIP::SipDialogRef dialog = SIP::gSipInterface.dmFindDialogByRtp(session);
		if (dialog.self()) {
			LOG(NOTICE) << "RTP timestamp jump"<<LOGVAR(timestamp)<<LOGVAR(dialogid)<<dialog;
			// This is called from the same thread that is calling rxFrame or txFrame, so no problem.
			if (dialog->mRxTime) {
				rtp_session_resync(session);
				dialog->mRxTime = 0;
				dialog->mRxRealTime = 0;
			}
		} else {
			LOG(ALERT) << "RTP timestamp jump, but no dialog"<<LOGVAR(dialogid);
		}
	}
};

int gCountRtpSessions = 0;
int gCountRtpSockets = 0;

namespace SIP {
const bool rtpUseRealTime = true;	// Enables a bug fix for the RTP library.

void SipRtp::rtpStop()
{
	if (mSession) {
		RtpSession *save = mSession;
		mSession = NULL;		// Prevent rxFrame and txFrame from using it, which is overkill because we dont call this until the state is not Active.
		rtp_session_destroy(save);
		gCountRtpSessions--;
		gCountRtpSockets--;
	}
}

void SipRtp::rtpInit()
{
	mSession = NULL; 
	mTxTime = 0;
	mRxTime = 0;
	mRxRealTime = 0;
	mTxRealTime = 0;
	mDTMF = 0;
	mDTMFDuration = 0;
	mDTMFEnding = 0;
	mRTPPort = 0; 	//to make sure noise doesn't magically equal a valid RTP port
}

void SipRtp::rtpText(std::ostringstream&os) const
{
	os <<LOGVARM(mTxTime) << LOGVARM(mRxTime);
	//os <<LOGVAR(mRTPRemIP);
	os <<LOGVAR(mRTPPort);
	os <<LOGVARM(mCodec);
	// warning: The unbelievably stupid << sends the mDTMS char verbatim, and it is 0, which prematurely terminates the string.
	unsigned dtmf = mDTMF;
	os <<LOGVAR(dtmf) <<LOGVARM(mDTMFDuration)<<LOGVARM(mDTMFStartTime);
}


void SipRtp::initRTP1(const char *d_ip_addr, unsigned d_port, unsigned dialogId)
{
	LOG(DEBUG) << LOGVAR(d_ip_addr)<<LOGVAR(d_port)<<vsdbText();

	if(mSession == NULL) {
		mSession = rtp_session_new(RTP_SESSION_SENDRECV);
		gCountRtpSessions++;
	}

	bool rfc2833 = gConfig.getBool("SIP.DTMF.RFC2833");
	if (rfc2833) {
		RtpProfile* profile = rtp_session_get_send_profile(mSession);
		int index = gConfig.getNum("SIP.DTMF.RFC2833.PayloadType");
		// (pat) The &payload_type_telephone_event comes from the RTP library PayloadType
		rtp_profile_set_payload(profile,index,&payload_type_telephone_event);
		// Do we really need this next line?
		rtp_session_set_send_profile(mSession,profile);
	}

	// 8-6-2013: I tried turning scheduling mode to FALSE, but it didnt seem to work at all.
	// There is an interesting problem when you dial 2600 since the rxFrame is blocking on its own txFrame,
	// a paradox that somehow doesnt hang.
	if (rtpUseRealTime) {
		// Disable blocking and scheduling in the RTP library block.
		rtp_session_set_blocking_mode(mSession, FALSE);
		rtp_session_set_scheduling_mode(mSession, FALSE);
	} else {
		// Let the RTP library block.
		rtp_session_set_blocking_mode(mSession, TRUE);
		rtp_session_set_scheduling_mode(mSession, TRUE);
	}

	rtp_session_set_connected_mode(mSession, TRUE);
	rtp_session_set_symmetric_rtp(mSession, TRUE);
	// Hardcode RTP session type to GSM full rate (GSM 06.10).
	// FIXME -- Make this work for multiple vocoder types.
	rtp_session_set_payload_type(mSession, 3);
	// (pat added) The last argument is user payload data that is passed to ourRtpTimestampJumpCallback()
	// I was going to use the dialogId but decided to look up the dialog by mSession.
	rtp_session_signal_connect(mSession,"timestamp_jump",(RtpCallback)ourRtpTimestampJumpCallback,dialogId);

	gCountRtpSockets++;
#ifdef ORTP_NEW_API
	rtp_session_set_local_addr(mSession, "0.0.0.0", mRTPPort, -1);
#else
	rtp_session_set_local_addr(mSession, "0.0.0.0", mRTPPort);
#endif
	rtp_session_set_remote_addr(mSession, d_ip_addr, d_port);
	WATCHF("*** initRTP local=%d remote=%s %d\n",mRTPPort,d_ip_addr,d_port);

	int speechBuffer = gConfig.getNum("GSM.SpeechBuffer");

	// The RTP library sets the default here to 5000, but I think the cost of resynchronization is very
	// low and we should do it more often to reduce horrendous sound quality that otherwise occurs
	// when there is extraneous delay in the received frames.  Such delay occurs due to the transmitter
	// using the TCH for FACCH, for in-call-SMS, and other reasons.
	// The horrendous sound I suspect is caused by improper computation of the rxframe number internally.
	// I'm setting it down such that we will resynchronize whenever we get more than one frame off, where one frame = 160.  
	// Update: That worked great as long as there was no jump, but
	// there seems to be a bug in the RTP library that the time jump limit must exceed the buffered amount
	// or it constantly tries to resync.
	// Update: When the timestamp jump occurs there is a very noticeable silence presumably while the jitter
	// buffer is reloaded, so instead of setting this low, we will leave it high (it must be set to
	// something to handle the negative timestamp jump after handover) and go back to setting rxTime from the actual time.
	//unsigned jump_limit = max(300,((speechBuffer+159+160) / 160));
	unsigned jump_limit = 5000;
	LOG(DEBUG)<<LOGVAR(jump_limit);
	rtp_session_set_time_jump_limit(mSession,jump_limit);

	if (speechBuffer == 0) {
		// (pat) Special case value turns off the rtp jitter compensation entirely.
		// There is some intrinsic buffering both in GSML1FEC and between us and the transceiver.
		rtp_session_enable_jitter_buffer(mSession,FALSE); 
	} else if (speechBuffer == 1) {
		// (pat) The default is adaptive speech buffer, so we just do nothing in this case.
	} else {
		// (pat 8-6-2013) The RTP adaptive jitter buffer does not work well with OpenBTS because it assumes
		// the source stream is continuous in time, but it is not in our case.  When FACCH is used or there
		// is some other drop out, the lost time appears to be added to the total delay through the de-jitter algorithm.
		// At least some phones seem to turn the TCH off completely during an in-call SMS.
		// So lets turn it off.  This code was copied from rtp_session_init().
		// (pat update) The RTP library goes south if you set the jitter buffer size over about 300 msecs,
		// so heck with it - just use the adaptive algorithm.  It is mostly fixed now anyway.
		{
			//int packets = speechBuffer/20 + 10;		// Number of 20 msec packets needed for buffering, plus some slop.
			JBParameters jbp;
			jbp.min_size=RTP_DEFAULT_JITTER_TIME;	// Not used by RTP code but we are setting it anyway.
			jbp.nom_size=speechBuffer;				// Original default was 80 msecs.
			jbp.max_size=-1;						// Not used by RTP code.
			// The max_packets must be large enough to cover the speech buffer size plus a lot of slop, not sure how much.
			jbp.max_packets= 100;/* maximum number of packet allowed to be queued */
			jbp.adaptive=FALSE;
			rtp_session_enable_jitter_buffer(mSession,TRUE);		// redundant, this is the default.
			rtp_session_set_jitter_buffer_params(mSession,&jbp);
		}
	}

	// Check for event support.
	int code = rtp_session_telephone_events_supported(mSession);
	if (code == -1) {
		if (rfc2833) { LOG(CRIT) << "RTP session does not support selected DTMF method RFC-2833" <<vsdbText(); }
		else { LOG(CRIT) << "RTP session does not support telephone events" <<vsdbText(); }
	}

}


static int get_rtp_tev_type(char dtmf){
        switch (dtmf){
                case '1': return TEV_DTMF_1;
                case '2': return TEV_DTMF_2;
                case '3': return TEV_DTMF_3;
                case '4': return TEV_DTMF_4;
                case '5': return TEV_DTMF_5;
                case '6': return TEV_DTMF_6;
                case '7': return TEV_DTMF_7;
                case '8': return TEV_DTMF_8;
                case '9': return TEV_DTMF_9;
                case '0': return TEV_DTMF_0;
                case '*': return TEV_DTMF_STAR;
                case '#': return TEV_DTMF_POUND;
                case 'a':
                case 'A': return TEV_DTMF_A;
                case 'B':
                case 'b': return TEV_DTMF_B;
                case 'C':
                case 'c': return TEV_DTMF_C;
                case 'D':
                case 'd': return TEV_DTMF_D;
		case '!': return TEV_FLASH;
                default:
		    LOG(WARNING) << "Bad dtmf: " << dtmf;
		    return -1;
                }
}

bool SipRtp::txDtmf()
{
	ScopedLock lock(mRtpLock);
	//true means start
	bool start = (mDTMFDuration == 0);
	mblk_t *m = rtp_session_create_telephone_event_packet(mSession,start);
	if (!m) {
		// (pat) Failed because, and I quote: "the rtp session cannot support telephony event (because the rtp profile
		// it is bound to does not include a telephony event payload type)."
		LOG(DEBUG) << "fail";
		return false;
	}
	// (pat) Max RTP event duration is 8 seconds, so if we exceed that turn it off.  Back it off a little (5 packets, 100ms) because
	// we also need to send the three end packets and it is not clear to me if the DTMFDuration should be incremented or not
	// during the end packets, but we are incrementing.
	// (8 * 50 * 160) - (5 * 160) = 63200.
	if (!mDTMFEnding && mDTMFDuration >= 63200) {
		mDTMFEnding = 1;
	}
	//volume 10 for some magic reason, arg 3 is true to send an end packet.
	// (pat) The magic reason is that the spec says DTMF tones below a certain dB should be ignored by the receiver, which is dumber than snot.
	int code = rtp_session_add_telephone_event(mSession,m,get_rtp_tev_type(mDTMF),!!mDTMFEnding,10,mDTMFDuration);
	int bytes = rtp_session_sendm_with_ts(mSession,m,mDTMFStartTime);
	LOG(DEBUG) <<LOGVAR(start) <<LOGVAR(mDTMF) <<LOGVAR(mDTMFEnding) <<LOGVAR(mDTMFDuration) <<LOGVAR(code) <<LOGVAR(bytes);
	// (pat) There is a buglet that this time would be incorrect if we flushed some RTP packets.
	mDTMFDuration += 160;
	if (mDTMFEnding) {
		// We need to send the end packet three times.
		if (mDTMFEnding++ >= 3) {
			mDTMFEnding = 0;
			mDTMF = 0;
		}
	}
	return (!code && bytes > 0);
}

// Return true if ok, false on failure.
bool SipRtp::startDTMF(char key)
{
	LOG (DEBUG) << key <<vsdbText();
	if (vgetSipState()!=Active) return false;
	if (get_rtp_tev_type(key) < 0){
		LOG(WARNING) << "DTMF (using RFC-2833) sent invalid key, numeric value="<<(int) key;
	    return false;
	}
	// (pat) It is ok to start a new DTMF before the old one ended.
	mDTMF = key;
	mDTMFDuration = 0;
	mDTMFStartTime = mTxTime;
	mDTMFEnding = 0;

	if (! txDtmf()) {
		// Error?  Turn off DTMF sending.
		LOG(WARNING) << "DTMF RFC-2833 failed on start." <<vsdbText();
		mDTMF = 0;
		return false;
	}
	return true;
}

void SipRtp::stopDTMF()
{
	//false means not start
	/***
		mblk_t *m = rtp_session_create_telephone_event_packet(mSession,false);
		//volume 10 for some magic reason, end is true
		int code = rtp_session_add_telephone_event(mSession,m,get_rtp_tev_type(mDTMF),true,10,mDTMFDuration);
		int bytes = rtp_session_sendm_with_ts(mSession,m,mDTMFStartTime);
		mDTMFDuration += 160;
		LOG (DEBUG) << "DTMF RFC-2833 sending " << mDTMF << " " << mDTMFDuration <<vsdbText();
		// Turn it off if there's an error.
		if (code || bytes <= 0)
	***/
	if (!mDTMF) {
		LOG(WARNING) << "stop DTMF command received after DTMF ended.";
		return;
	}
	mDTMFEnding = 1;
	if (! txDtmf()) {
	    LOG(ERR) << "DTMF RFC-2833 failed at end" <<vsdbText();
		mDTMF = 0;
	}

}


// send frame in the uplink direction.
// The gsm bit rate is 13500 and clock rate is 8000.  Time is 20ms or 50 packets/sec.
// The 160 that we send is divided by payload->clock_rate in rtp_session_ts_to_time to yield 20ms.
void SipRtp::txFrame(AudioFrame* frame, unsigned numFlushed)
{
	if(vgetSipState()!=Active) return;
	ScopedLock lock(mRtpLock);

	// HACK -- Hardcoded for GSM/8000.
	// FIXME: Make this work for multiple vocoder types. (pat) fixed, but codec is still hard-coded in initRTP1.
	int nbytes = frame->sizeBytes();
	// (pat 8-2013) Our send stream is discontinous.  After a discontinuity, the sound degrades.
	// I think this is caused by bugs in the RTP library.

	mTxTime += (numFlushed+1)*160;
	int result = rtp_session_send_with_ts(mSession, frame->begin(), nbytes, mTxTime);
	LOG(DEBUG) << LOGVAR(mTxTime) <<LOGVAR(result);

	// (pat) The result is the number of bytes sent over the network, which includes nbytes data + 12 bytes of RTP header.
	if (result < nbytes || result != 33+12) {
		LOG(DEBUG) << "rtp_session_send_with_ts("<<nbytes<<") returned "<<result <<vsdbText();
	}

	if (mDTMF) {
		//false means not start
		/*****
			mblk_t *m = rtp_session_create_telephone_event_packet(mSession,false);
			//volume 10 for some magic reason, false means not end
			int code = rtp_session_add_telephone_event(mSession,m,get_rtp_tev_type(mDTMF),false,10,mDTMFDuration);
			int bytes = rtp_session_sendm_with_ts(mSession,m,mDTMFStartTime);
			mDTMFDuration += 160;
			//LOG (DEBUG) << "DTMF RFC-2833 sending " << mDTMF << " " << mDTMFDuration <<vsdbText();
			if (code || bytes <=0)
		***/
		if (! txDtmf())
		{
			LOG(ERR) << "DTMF RFC-2833 failed after start." <<vsdbText();
			mDTMF=0; // Turn it off if there's an error.
		}
	}
}


AudioFrame *SipRtp::rxFrame()
{
	if(vgetSipState()!=Active) {
		LOG(DEBUG) <<"skip"<<LOGVAR(vgetSipState());
		return 0; 
	}
	int more = 0;

	// The buffer size is:
	//  For GSM, 260 bits + 4 bit header is 33 bytes.
	//  For TFO (3GPP 28.062 5.2.2) is 40 bytes.
	//  For AMR 'compact transport format', variable size, max is 244 bits + 10 bit header??
	// (pat) We will not support TFO for years, if ever.
	// TODO: Make the rxFrame buffer big enough (160?) for G.711.  But we dont support that yet.
	const int maxsize = 33;

	// (pat 8-2013) I tried rtp_session_get_current_recv_ts but it just doesnt work; returns garbage.
	// It is frightening how much we depend on the extremely buggy RTP library.
	//uint32_t suggestedRxTime = rtp_session_get_current_recv_ts(mSession);

	// (pat 8-2013) The RTP library has a scheduling mode and a blocking mode whereby it blocks the thread
	// until the specified time has elapsed, which is 20ms.
	// But after a discontinuity in the send data stream the RTP scheduler seems to get confused and rxFrame
	// returns 0 for long periods of time.
	// A discontinuity can be artificially generated at the time of writing using an in-call-SMS,
	// which currently blocks TCH while the current thread runs SACCH, a bug to be fixed,
	// however discontinuities could occur for other reasons so the code should not break when one occurs.
	// I added this code controlled by rtpUseRealTime so we could handle the real elapsed time ourselves and not block.
	// This code below suffers no such instability, so we are using it.  Someday if we switch RTP libraries,
	// someone can try turning off rtpUseRealTime - but you have to TEST DISCONTINUITIES, which is hard,
	// so dont just turn this off, try a test call, and pronounce it fixed.
	if (rtpUseRealTime) {
		struct timeval tv;
		gettimeofday(&tv,NULL);
		uint64_t realTime = (uint64_t)tv.tv_sec * (1000 * 1000) + (uint64_t)tv.tv_usec;		// time in usecs.
		if (mRxRealTime == 0) {
			// First time, init for next pass through...
			devassert(mRxTime == 0);
			mRxRealTime = realTime;
		} else {
			uint64_t delay = realTime - mRxRealTime;		// total elapsed time in usecs.
			uint64_t delayInFrames = delay / (1000 * 20);  // 20ms per frame	// elapsed time in frames.
			unsigned proposedRxTime = delayInFrames * 160;	// elapsed time in RTP 'time' units.  (160 / 8000 bitrate == 20 msecs)
			LOG(DEBUG) << format("realTime=%.3f delay=%.3f delayInFrames=%u RxTime=%u proposed=%u",
				realTime/1e6,delay/1e6,(unsigned)delayInFrames,mRxTime,proposedRxTime);
			if (proposedRxTime <= mRxTime) {
				LOG(DEBUG) <<"skip, insufficient time passed";
				return NULL;
			}
			// We will snag the next frame with a number equal or higher than this.  If there are none available, we get NULL.
			// When there is a discontinuity of missing frames, we get a bunch of NULL frames during the discontinuity, then
			// things pick up normally again.
			mRxTime += 160;
		}
	}

	AudioFrame *result = new AudioFrame(maxsize);  // Make it big enough for anything we might support.
	int ret = rtp_session_recv_with_ts(mSession, result->begin(), maxsize, mRxTime, &more);
	// (pat) You MUST increase rxTime even if rtp_session_recv... returns 0.
	// This looks like a bug in the RTP lib to me, specifically, here:
	//  Excerpt from rtpsession.c: rtp_session_recvm_with_ts():
	//	 // prevent reading from the sockets when two consecutives calls for a same timestamp*/
	//	 if (user_ts==session->rtp.rcv_last_app_ts)
	//			 read_socket=FALSE;
	// The bug is the above should also check the qempty() flag.
	// It should only manifest when blocking mode is off but we had it on when I thought I saw troubles.
	// I tried incrementing by just 1 when ret is 0, but that resulted in no sound at all.

	if (!rtpUseRealTime) { mRxTime += 160; }

	//LOG(DEBUG) << "rtp_session_recv returns("<<LOGVAR(mRxTime)<<LOGVAR(more)<<")="<<LOGVAR(ret) <<vsdbText();
	devassert(ret <= maxsize);   // Check for bugs in rtp library or Audio type setup.
	LOG(DEBUG) <<LOGVAR(vgetSipState())<<LOGVAR(mRxTime)<<LOGVAR(ret);
	if (ret <= 0) { delete result; return NULL; }
	result->setSizeBits(ret * 8);
	// (pat) Added warning; you could get ALOT of these:
	// Update: It is not that the frame is over-sized, it is that there is another frame already in the queue.
	//if (more) { LOG(WARNING) << "Incoming RTP frame over-sized, extra ignored." <<vsdbText(); }
	if (more) { LOG(WARNING) << "Incoming RTP lagging behind clock." <<vsdbText(); }
	return result;
}


};	// namespace SIP

