/*
* Copyright 2008-2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2012, 2014 Range Networks, Inc.
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

#define LOG_GROUP LogGroup::GSM		// Can set Log.Level.GSM for debugging

#define TESTTCHL1FEC


#include "GSML1FEC.h"
#include "GSMCommon.h"
#include "GSMTransfer.h"
//#include "GSMSAPMux.h"
#include "GSMConfig.h"
#include "GSMTDMA.h"
#include "GSMTAPDump.h"
#include "GSMLogicalChannel.h"
#include <ControlCommon.h>
#include <OpenBTSConfig.h>
#include <TRXManager.h>
#include <Logger.h>
#include <TMSITable.h>
#include <assert.h>
#include <math.h>
#include <time.h>

#include "../GPRS/GPRSExport.h"

#undef WARNING

namespace GSM {
using namespace std;
using namespace SIP;	// For AudioFrame

#undef OBJLOG
#define OBJLOG(level) LOG(level) <<descriptiveString()<<" "
#define BLATHER DEBUG	// (pat 4-2014) These were formerly INFO but there is one message for each frame, which is too much.



// (pat) David says this is the initial power level we want to send to handsets.
// 33 translates to 2 watts, which is the max in the low band.  
// The power control loop will rapidly turn down the power once SACCH is established.
// This value is in DB.  It is converted to an ordered MS power based on tables in GSM 5.05 4.1.
const float cInitialPower = 33; 


// The actual phone settings change every 4 bursts, so average over at least 4.
static const unsigned cAveragePeriodTiming = 8; // How many we measurement reports over which we average timing, minus 1.
//static const unsigned cAveragePeriodRSSI = 8; // How many measurement reports over which we average RSSI, minus 1.
//static const unsigned cAveragePeriodSNR = 8; // How many frames over which we average SNR, minus 1.
static const unsigned cFERMemory = 208; // How many we frames we average FER, minus 1.  For reporting.

/*

	Notes on reading the GSM specifications.

	Every FEC section in GSM 05.03 uses standard names for the bits at
	different stages of the encoding/decoding process.

	This is all described formally in GSM 05.03 2.2.

	"d"	-- data bits.  The actual payloads from L2 and the vocoders.
	"p" -- parity bits.  These are calculated from d.
	"u" -- uncoded bits.  A concatenation of d, p and inner tail bits.
	"c" -- coded bits.  These are the convolutionally encoded from u.
	"i" -- interleaved bits.  These are the output of the interleaver.
	"e" -- "encrypted" bits.  These are the channel bits in the radio bursts.

	The "e" bits are call "encrypted" even when encryption is not used.

	The encoding process is:

	L2 -> d -> -> calc p -> u -> c -> i -> e -> radio bursts

	The decoding process is:

	radio bursts -> e -> i -> c -> u -> check p -> d -> L2

	Bit ordering in d is LSB-first in each octet.
	Bit ordering everywhere else in the OpenBTS code is MSB-first
	in every field to give contiguous fields across byte boundaries.
	We use the BitVector2::LSB8MSB() method to translate.

*/



/**@name Power control utility functions based on GSM 05.05 4.1.1 */
//@{

/** Power control codes for GSM400, GSM850, EGSM900 from GSM 05.05 4.1.1. */
static const int powerCommandLowBand[32] = 
{
	39, 39, 39, 37,	// 0-3
	35, 33, 31, 29,	// 4-7
	27, 25, 23, 21,	// 8-11
	19, 17, 15, 13,	// 12-15
	11, 9, 7, 5,	// 16-19
	5, 5, 5, 5,		// 20-23
	5, 5, 5, 5,		// 24-27
	5, 5, 5, 5		// 28-31
};

/** Power control codes for DCS1800 from GSM 05.05 4.1.1. */
static const int powerCommand1800[32] =
{
	30, 28, 26, 24,	// 0-3
	22, 20, 18, 16,	// 4-7
	14, 12, 10, 8,	// 8-11
	6, 4, 2, 0,		// 12-15
	0, 0, 0, 0,		// 16-19
	0, 0, 0, 0,		// 20-23
	0, 0, 0, 0,		// 24-27
	0, 36, 24, 23	// 28-31
};

/** Power control codes for PCS1900 from GSM 05.05 4.1.1. */
static const int powerCommand1900[32] =
{
	30, 28, 26, 24,	// 0-3
	22, 20, 18, 16,	// 4-7
	14, 12, 10, 8,	// 8-11
	6, 4, 2, 0,		// 12-15
	0, 0, 0, 0,		// 16-19
	0, 0, 0, 0,		// 20-23
	0, 0, 0, 0,		// 24-27
	0, 0, 0, 0,		// 28-31
};


const int* pickTable()
{
	unsigned band = gBTS.band();


	switch (band) {
		case GSM850:
		case EGSM900:
			return powerCommandLowBand;
			break;
		case DCS1800:
			return powerCommand1800;
			break;
		case PCS1900:
			return powerCommand1900;
			break;
		default: return NULL;
	}
}


int decodePower(unsigned code)
{
	static const int *table = pickTable();
	assert(table);
	return table[code];

}


/** Given a power level in dBm, encode the control code. */
unsigned encodePower(int power)
{
	static const int *table = pickTable();
	assert(table);
	unsigned minErr = abs(power - table[0]);
	unsigned code = 0;
	for (int i=1; i<32; i++) {
		unsigned thisErr = abs(power - table[i]);
		if (thisErr==0) return i;
		if (thisErr<minErr) {
			minErr = thisErr;
			code = i;
		}
	}
	return code;
}


//@}





/*
	L1Encoder base class methods.
*/


L1Encoder::L1Encoder(unsigned wCN, unsigned wTN, const TDMAMapping& wMapping, L1FEC *wParent)
	:mDownstream(NULL),
	mMapping(wMapping),
	mCN(wCN),mTN(wTN),
	mTSC(gBTS.BCC()),			// Note that TSC (Training Sequence Code) is hardcoded to the BCC.
	mParent(wParent),
	mTotalFrames(0),
	mPrevWriteTime(gBTS.time().FN(),wTN),
	mNextWriteTime(gBTS.time().FN(),wTN),
	mRunning(false),
	mEncrypted(ENCRYPT_NO),
	mEncryptionAlgorithm(0)
{
	assert((int)mCN<gConfig.getNum("GSM.Radio.ARFCNs"));
#ifndef TESTTCHL1FEC
	assert(mMapping.allowedSlot(mTN));
	assert(mMapping.downlink());
	mNextWriteTime.rollForward(mMapping.frameMapping(0),mMapping.repeatLength());
	mPrevWriteTime.rollForward(mMapping.frameMapping(0),mMapping.repeatLength());
#endif // TESTTCHL1FEC
	// Compatibility with C0 will be checked in the ARFCNManager.
	// Build the descriptive string.
	ostringstream ss;
	ss << wMapping.typeAndOffset();
	mDescriptiveString = format("C%dT%d %s", wCN, wTN, ss.str().c_str());
}

ostream& operator<<(std::ostream& os, const L1Encoder *encp)
{
	os <<"L1Encoder "<<(encp ? encp->descriptiveString() : "NULL");
	return os;
}


void L1Encoder::rollForward()
{
	// Calculate the TDMA parameters for the next transmission.
	// This implements GSM 05.02 Clause 7 for the transmit side.
	mPrevWriteTime = mNextWriteTime;
	mTotalFrames++;
	ScopedLock lock(mWriteTimeLock,__FILE__,__LINE__);	// (pat) Protects getNextWriteTime.
	mNextWriteTime.rollForward(mMapping.frameMapping(mTotalFrames),mMapping.repeatLength());
}




TypeAndOffset L1Encoder::typeAndOffset() const
{
	return mMapping.typeAndOffset();
}


void L1Encoder::encInit()
{
	OBJLOG(BLATHER) << "L1Encoder";
	handoverPending(false);
	ScopedLock lock(mEncLock,__FILE__,__LINE__);
	mTotalFrames=0;
	resync(true);	// (pat 4-2014) Force mNextWriteTime to be recalculated at channel initiation.
	mPrevWriteTime = gBTS.time();	// (pat) Prevents the first write after opening the channel from blocking in waitToSend called from transmit.
	// (doug) Turning off encryption when the channel closes would be a nightmare
	// (catching all the ways, and performing the handshake under less than
	// ideal conditions), so we leave encryption on to the bitter end,
	// then clear the encryption flag here, when the channel gets reused.
	mEncrypted = ENCRYPT_NO;
	mEncryptionAlgorithm = 0;
	// (pat) On very first initialization, start sending the dummy bursts;
	// this allows us to get rid of the dopey 'starting' of all the channels when the BTS is turned on.
	if (mCN == 0 && !mEncEverActive) { sendDummyFill(); }
}

void L1Encoder::encStart()
{
	if (!mRunning) { mRunning = true; serviceStart(); }
	mEncActive = true;
	mEncEverActive = true;
}


// (pat) sendDummyFill does not block, but it advances the mNextWriteTime.
void L1Encoder::close()
{
	OBJLOG(BLATHER) << "L1Encoder";
	ScopedLock lock(mEncLock,__FILE__,__LINE__);
	if (mEncActive) { sendDummyFill(); }
	mEncActive = false;
}


bool L1Encoder::encActive() const
{
	const L1Decoder *sib = sibling();
	if (sib) {
		return mEncActive && (sib->decActive());
	} else {
		return mEncActive;
	}
}


L1Decoder* L1Encoder::sibling()
{
	if (!mParent) return NULL;
	return mParent->decoder();
}


const L1Decoder* L1Encoder::sibling() const
{
	if (!mParent) return NULL;
	return mParent->decoder();
}

void L1Encoder::resync(bool force)
{
	// If the encoder's clock is far from the current BTS clock,
	// get it caught up to something reasonable.
	Time now = gBTS.time();
	int32_t delta = mNextWriteTime-now;
	OBJLOG(DEBUG) << "L1Encoder next=" << mNextWriteTime << " now=" << now << " delta=" << delta;
	if (force || (delta<0) || (delta>(51*26))) {
		mNextWriteTime = now;
		mNextWriteTime.TN(mTN);
		mTotalFrames = 0;	// (pat 4-2014) Make sure we start at beginning of mapping.
		{ ScopedLock lock(mWriteTimeLock,__FILE__,__LINE__);	// (pat) Protects getNextWriteTime.
		  mNextWriteTime.rollForward(mMapping.frameMapping(mTotalFrames),mMapping.repeatLength());
		}
		OBJLOG(DEBUG) <<"L1Encoder RESYNC "<< " next=" << mNextWriteTime << " now=" << now;
	}
}

Time L1Encoder::getNextWriteTime()
{
	resync();
	ScopedLock lock(mWriteTimeLock,__FILE__,__LINE__);
	return mNextWriteTime;
}


void L1Encoder::waitToSend() const
{
	// Block until the BTS clock catches up to the
	// most recently transmitted burst.
	gBTS.clock().wait(mPrevWriteTime);
}


void L1Encoder::sendDummyFill()
{
	// Send the L1 idle filling pattern, if any.
	// For C0, that's the dummy burst.
	// For Cn, don't do anything.
	OBJLOG(DEBUG) <<"L1Encoder";
	resync();
	// (pat) FIXME: On other ARFCNs we need to disable the transceiver auto-filling.  See wiki ticket 1141.
	// (pat) In the meantime, we must send the dummy burst to inform the MS that this channel is disabled;
	// this is required specifically for SACCH.
	// To preserve the old behavior, we will leave other arfcns non-transmitting until the first time they are used,
	// but ever after we have to send the filler pattern.
	if (mCN==0 || mEncEverActive) {
		for (unsigned i=0; i<mMapping.numFrames(); i++) {
			mFillerBurst.time(mNextWriteTime);
			mDownstream->writeHighSideTx(mFillerBurst,"dummy");
			rollForward();
		}
		mFillerSendTime = gBTS.clock().systime2(mNextWriteTime);	// (pat) The time when the last burst of the filler will be delivered.
	}
}

bool L1Encoder::l1IsIdle() const
{
	return ! mEncActive && mFillerSendTime.passed();
}

unsigned L1Encoder::ARFCN() const
{
	assert(mDownstream);
	return mDownstream->ARFCN();
}

int unhex(const char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	assert(0);
}


// Given IMSI, copy Kc.  Return true iff there *is* a Kc.
bool imsi2kc(string wIMSI, unsigned char *wKc)
{
	string kc = gTMSITable.getKc(wIMSI.c_str());
	if (kc.length() == 0) return false;
	while (kc.length() < 16) {
		kc = '0' + kc;
	}
	assert(kc.length() == 16);
	unsigned char *dst = wKc;
	for (size_t p = 0; p < kc.length(); p += 2) {
		*dst++ = (unhex(kc[p]) << 4) | (unhex(kc[p+1]));
	}
	// Dont leave this message in production code:
	LOG(DEBUG) << format("decrypt maybe imsi=%s Kc=%s",wIMSI.c_str(),kc.c_str());
	return true;
}


// Turn on encryption phase-in, which is watching for bad frames and
// retrying them with encryption.
// Return false and leave encryption off if there's no Kc.
bool L1Decoder::decrypt_maybe(string wIMSI, int wA5Alg)
{
	if (!imsi2kc(wIMSI, mKc)) return false;
	mEncrypted = ENCRYPT_MAYBE;
	mEncryptionAlgorithm = wA5Alg;
	LOG(DEBUG) << format("decrypt maybe imsi=%s algorithm=%d",wIMSI.c_str(),mEncryptionAlgorithm);
	return true;
}


unsigned L1Decoder::ARFCN() const
{
	assert(mParent);
	return mParent->ARFCN();
}


TypeAndOffset L1Decoder::typeAndOffset() const
{
	return mMapping.typeAndOffset();
}

void DecoderStats::decoderStatsInit()
{
	mAveFER = 0;
	mAveSNR = 0;
	mAveBER = 0;
	mSNRCount = 0;
	mStatTotalFrames = 0;
	mStatStolenFrames = 0;
	mStatBadFrames = 0;
}

ostream& operator<<(std::ostream& os, const L1Decoder *decp)
{
	os <<"L1Decoder "<<(decp ? decp->descriptiveString() : "NULL");
	return os;
}

string L1Decoder::displayTimers() const
{
	ostringstream ss;
	// No point in showing T3103 for handover - its too fast.
	//ss <<LOGVARM(mT3101) <<LOGVARM(mT3109) <<LOGVARM(mT3111);
	ss <<LOGVARM(mBadFrameTracker);
	return ss.str();
}

void L1Decoder::decInit()
{
	handoverPending(false,0);
	ScopedLock lock(mDecLock,__FILE__,__LINE__);
	//if (!mRunning) decStart();
	//mRunning = true;
	mDecoderStats.decoderStatsInit();
	//mFER=0.0F;
	mBadFrameTracker = 0;
	//mT3111.reset();
	//mT3109.reset(gConfig.GSM.Timer.T3109);
	//mT3101.reset();
	// Turning off encryption when the channel closes would be a nightmare
	// (catching all the ways, and performing the handshake under less than
	// ideal conditions), so we leave encryption on to the bitter end,
	// then clear the encryption flag here, when the channel gets reused.
	mEncrypted = ENCRYPT_NO;
	mEncryptionAlgorithm = 0;
	//mActive = true;
}

void L1Decoder::decStart()
{
	//mT3111.reset();
	// Pat changed initial open state on T3109 from inactive via reset to active via set,
	// so that it is easier to test in GSMLogicalChannel.
	//mT3109.set(); 		//old: mT3109.reset();
	//mT3101.set();
	mDecActive = true;
}


void L1Decoder::close()
{
	mDecActive = false;
}

bool L1Decoder::decActive() const
{
	return mDecActive;
}

L1Encoder* L1Decoder::sibling()
{
	if (!mParent) return NULL;
	return mParent->encoder();
}


const L1Encoder* L1Decoder::sibling() const
{
	if (!mParent) return NULL;
	return mParent->encoder();
}


void DecoderStats::countSNR(const RxBurst &burst)
{
	// setting to 0 disables:
	mLastSNR = burst.getNormalSNR();
	if (int SNRAveragePeriod = gConfig.getNum("GSM.Radio.SNRAveragePeriod")) {
		int count = min((int)mSNRCount,SNRAveragePeriod);
		mAveSNR = (mLastSNR  + count * mAveSNR) / (count+1);
		mSNRCount++;
	}
}


void L1Decoder::countStolenFrame(unsigned nframes)
{
	// (pat 1-16-2014) Stolen frames should not affect FER reporting.
	mDecoderStats.mStatTotalFrames += nframes;
	mDecoderStats.mStatStolenFrames += nframes;
}

void L1Decoder::countGoodFrame(unsigned nframes)	// Number of bursts to count.
{
	// Subtract 2 for each good frame, but dont go below zero.
	mBadFrameTracker = mBadFrameTracker <= 1 ? 0 : mBadFrameTracker-2;
	static const float a = 1.0F / ((float)cFERMemory);
	static const float b = 1.0F - a;
	mDecoderStats.mAveFER *= b;
	mDecoderStats.mStatTotalFrames += nframes;
	OBJLOG(BLATHER) <<"L1Decoder FER=" << mDecoderStats.mAveFER;
}

void L1Decoder::countBER(unsigned bec, unsigned frameSize)
{
	static const float a = 1.0F / ((float)cFERMemory);
	static const float b = 1.0F - a;
	float thisBER = (float) bec / frameSize;
	mDecoderStats.mLastBER = thisBER;
	mDecoderStats.mAveBER = b*mDecoderStats.mAveBER + a * thisBER;
}


void L1Decoder::countBadFrame(unsigned nframes)	// Number of bursts to count.
{
	mBadFrameTracker++;
	static const float a = 1.0F / ((float)cFERMemory);
	static const float b = 1.0F - a;
	mDecoderStats.mAveFER = b*mDecoderStats.mAveFER + a;
	mDecoderStats.mStatTotalFrames += nframes;
	mDecoderStats.mStatBadFrames += nframes;
	OBJLOG(BLATHER) <<"L1Decoder FER=" << mDecoderStats.mAveFER;
}

void SACCHL1Decoder::countBadFrame(unsigned nframes)
{
	RSSIBumpDown(gConfig.getNum("Control.SACCHTimeout.BumpDown"));
	L1Decoder::countBadFrame(nframes);
}


void L1Encoder::handoverPending(bool flag)
{
	if (flag) {
		bool ok = mDownstream->setHandover(mTN);
		if (!ok) OBJLOG(ALERT) << "handover setup failed";
	} else {
		bool ok = mDownstream->clearHandover(mTN);
		if (!ok) OBJLOG(ALERT) << "handover clear failed";
	}
}


HandoverRecord& L1FEC::handoverPending(bool flag, unsigned handoverRef)
{
	assert(mEncoder);
	assert(mDecoder);
	mEncoder->handoverPending(flag);
	return mDecoder->handoverPending(flag, handoverRef);
}


// (pat) This can only be called during initialization, because installDecoder aborts
// if it is called twice on the same channel.
// This routine is used for traffic channels.
void L1FEC::downstream(ARFCNManager* radio)
{
	if (mEncoder) mEncoder->downstream(radio);
	if (mDecoder) radio->installDecoder(mDecoder);
}

void L1FEC::l1start()
{
	LOG(DEBUG);
	if (mDecoder) mDecoder->decStart();
	if (mEncoder) mEncoder->encStart();
}


void L1FEC::l1init()
{
	LOG(DEBUG);
	if (mDecoder) mDecoder->decInit();
	if (mEncoder) mEncoder->encInit();
}

void L1FEC::l1close()
{
	LOG(DEBUG) <<descriptiveString();
	if (mEncoder) mEncoder->close();	// Does the sendDummyFill.
	if (mDecoder) mDecoder->close();
}


// Active means it is currently sending and receiving.
// recyclable is used to tell when the channel is reusable.
bool L1FEC::l1active() const
{
	// non-sacch encode-only channels are always active.
	// Otherwise, the decoder is the better indicator.
	if (mDecoder) {
		return mDecoder->decActive();
	} else {
		return (mEncoder!=NULL);	// (pat) The send-only channels are always active.
	}
}



// (pat) Note that GPRS has an option to use a PRACH burst identical to RACH bursts.
// (pat) This routine is fed immediately from the radio in TRXManager;
// wDemuxTable points to this routine.
// The RACH is enqueued, and a separate thread runs AccessGrantResponder on the RACH,
// although I'm not sure why.
void RACHL1Decoder::writeLowSideRx(const RxBurst& burst)
{
	// The L1 FEC for the RACH is defined in GSM 05.03 4.6.

	// Decode the burst.
	const SoftVector e(burst.segment(49,36));
	//e.decode(mVCoder,mU);
	mVCoder.decode(e,mU);

	// To check validity, we have 4 tail bits and 6 parity bits.
	// False alarm rate for random inputs is 1/1024.

	// Check the tail bits -- should all the zero.
	if (mU.peekField(14,4)) {
		countBadFrame(1);
		return;
	}

	// Check the parity.
	// The parity word is XOR'd with the BSIC. (GSM 05.03 4.6.)
	unsigned sentParity = ~mU.peekField(8,6);
	unsigned checkParity = mD.parity(mParity);
	unsigned encodedBSIC = (sentParity ^ checkParity) & 0x03f;
	if (encodedBSIC != gBTS.BSIC()) {
		countBadFrame(1);
		return;
	}

	// We got a valid RACH burst.
	// The "payload" is an 8-bit field, "RA", defined in GSM 04.08 9.1.8.
	// The channel assignment procedure is in GSM 04.08 3.3.1.1.3.
	// It requires knowledge of the RA value and the burst receive time.
	// The RACH L2 is so thin that we don't even need code for it.
	// Just pass the required information directly to the control layer.

	countGoodFrame(1);
	countBER(mVCoder.getBEC(),36);
	mD.LSB8MSB();
	unsigned RA = mD.peekField(0,8);
	OBJLOG(INFO) <<"RACHL1Decoder received RA=" << RA << " at time " << burst.time() \
		<< " with RSSI=" << burst.RSSI() << " timingError=" << burst.timingError() <<LOGVAR(TN());
	//gBTS.channelRequest(new ChannelRequestRecord(RA,burst.time(),burst.RSSI(),burst.timingError()));
	AccessGrantResponder(RA,burst.time(),burst.RSSI(),burst.timingError(),TN());
}







/*
	XCCHL1Encoder and Decoder methods.
	The "XCCH" L1 components are based on GSM 05.03 4.1.
	These are the most commonly used control channel L1 format
	in GSM and are offer here as examples.
*/



XCCHL1Decoder::XCCHL1Decoder(
		unsigned wCN,
		unsigned wTN,
		const TDMAMapping& wMapping,
		L1FEC *wParent)
	:L1Decoder(wCN,wTN,wMapping,wParent)
{
}

SharedL1Decoder::SharedL1Decoder()
	: mBlockCoder(0x10004820009ULL, 40, 224),
	mC(456),
	mU(228), 
	mP(mU.segment(184,40)),mDP(mU.head(224)),mD(mU.head(184)),
	mHParity(0x06f,6,8),mHU(18),mHD(mHU.head(8))
{
	for (int i=0; i<4; i++) {
		mE[i] = SoftVector(114);
		mI[i] = SoftVector(114);
		// Fill with zeros just to make Valgrind happy.
		mE[i].fill(0);
		mI[i].fill(0);
	}
}



void XCCHL1Decoder::writeLowSideRx(const RxBurst& inBurst)
{
	OBJLOG(DEBUG) <<"XCCHL1Decoder " << inBurst;
	// If the channel is closed, ignore the burst.
	if (!decActive()) {
		OBJLOG(DEBUG) <<"XCCHL1Decoder not active, ignoring input";
		return;
	} 
	mDecoderStats.countSNR(inBurst);
	// save frame number for possible decrypting
	int B = mMapping.reverseMapping(inBurst.time().FN()) % 4;
	mFN[B] = inBurst.time().FN();

	// Accept the burst into the deinterleaving buffer.
	// Return true if we are ready to interleave.
	if (!processBurst(inBurst)) return;
	if (mEncrypted == ENCRYPT_YES) {
		decrypt();
	}
	if (mEncrypted == ENCRYPT_MAYBE) {
		saveMi();
	}
	deinterleave();
	if (decode()) {
		countGoodFrame(1);
		countBER(mVCoder.getBEC(),mC.size());
		mD.LSB8MSB();
		handleGoodFrame();
	} else {
		if (mEncrypted == ENCRYPT_MAYBE) {
			// We don't want to start decryption until we get the (encrypted) layer 2 acknowledgement
			// of the Ciphering Mode Command, so we start maybe decrypting when we send the command,
			// and when the frame comes along, we'll see that it doesn't pass normal decoding, but
			// when we try again with decryption, it will pass.  Unless it's just noise.
			OBJLOG(DEBUG) << "XCCHL1Decoder: try decoding again with decryption";
			restoreMi();
			decrypt();
			deinterleave();
			if (decode()) {
				OBJLOG(DEBUG) << "XCCHL1Decoder: success on 2nd try";
				// We've successfully decoded an encrypted frame.  Start decrypting all uplink frames.
				mEncrypted = ENCRYPT_YES;
				// Also start encrypting downlink frames.
				parent()->encoder()->mEncrypted = ENCRYPT_YES;
				parent()->encoder()->mEncryptionAlgorithm = mEncryptionAlgorithm;
				countGoodFrame(1);
				mD.LSB8MSB();
				handleGoodFrame();
				countBER(mVCoder.getBEC(),mC.size());
			} else {
				countBadFrame(1);
			}
		} else {
			countBadFrame(1);
		}
	}
}


void XCCHL1Decoder::saveMi()
{
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 114; j++) {
			mE[i].settfb(j, mI[i].softbit(j));
		}
	}
}


void XCCHL1Decoder::restoreMi()
{
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 114; j++) {
			mI[i].settfb(j, mE[i].softbit(j));
		}
	}
}


void XCCHL1Decoder::decrypt()
{
	// decrypt y
	for (int i = 0; i < 4; i++) {
		unsigned char block1[15];
		unsigned char block2[15];
		// 03.20 C.1.2
		// 05.02 3.3.2.2.1
		int fn = mFN[i];
		int t1 = fn / (26*51);
		int t2 = fn % 26;
		int t3 = fn % 51;
		int count = (t1<<11) | (t3<<5) | t2;
		LOG(DEBUG) <<LOGVAR(fn) <<LOGVAR(count);
		if (mEncryptionAlgorithm == 1) {
			A51_GSM(mKc, 64, count, block1, block2);
		} else if (mEncryptionAlgorithm == 3) {
			A53_GSM(mKc, 64, count, block1, block2);
		} else {
			devassert(0);
		}
		for (int j = 0; j < 114; j++) {
			if ((block2[j/8] & (0x80 >> (j%8)))) {
				mI[i].settfb(j, 1.0 - mI[i].softbit(j));
			}
		}
	}
}

bool XCCHL1Decoder::processBurst(const RxBurst& inBurst)
{
	OBJLOG(DEBUG) <<"XCCHL1Decoder " << inBurst;
	// Accept the burst into the deinterleaving buffer.
	// Return true if we are ready to interleave.

	// TODO -- One quick test of burst validity is to look at the tail bits.
	// We could do that as a double-check against putting garbage into
	// the interleaver or accepting bad parameters.
	// (pat) Wrong!  Dont do that.  The tail bits are there to help the viterbi decoder by steering
	// it with known final bits, something we dont use at present.  But if you discard frames based
	// on non-zero tail bits you could be incorrectly discarding perfectly good frames because
	// of one bad inconsequential bit.

	// The reverse index runs 0..3 as the bursts arrive.
	// It is the "B" index of GSM 05.03 4.1.4 and 4.1.5.
	int B = mMapping.reverseMapping(inBurst.time().FN()) % 4;
	// A negative value means that the demux is misconfigured.
	assert(B>=0);

	// Pull the data fields (e-bits) out of the burst and put them into i[B][].
	// GSM 05.03 4.1.5
	inBurst.data1().copyToSegment(mI[B],0);
	inBurst.data2().copyToSegment(mI[B],57);

	// If the burst index is 0, save the time
	// FIXME -- This should be moved to the deinterleave methods.
	if (B==0)
		mReadTime = inBurst.time();

	// If the burst index is 3, then this is the last burst in the L2 frame.
	// Return true to indicate that we are ready to deinterleave.
	return B==3;

	// TODO -- This is sub-optimal because it ignores the case
	// where the B==3 burst is simply missing, even though the soft decoder
	// might actually be able to recover the frame.  (pat) Dream on.
	// It also allows for the mixing of bursts from different frames.
	// If we were more clever, we'd be watching for B to roll over as well.
}




void SharedL1Decoder::deinterleave()
{
	// Deinterleave i[][] to c[].
	// This comes directly from GSM 05.03, 4.1.4.
	for (int k=0; k<456; k++) {
		int B = k%4;
		int j = 2*((49*k) % 57) + ((k%8)/4);
		mC[k] = mI[B][j];
		// Mark this i[][] bit as unknown now.
		// This makes it possible for the soft decoder to work around
		// a missing burst.
		mI[B][j] = 0.5F;
	}
}


bool SharedL1Decoder::decode()
{
	// Apply the convolutional decoder and parity check.
	// Return true if we recovered a good L2 frame.

	// Convolutional decoding c[] to u[].
	// GSM 05.03 4.1.3
	OBJLOG(DEBUG) <<"XCCHL1Decoder "<< mC;
	//mC.decode(mVCoder,mU);
	mVCoder.decode(mC,mU);
	OBJLOG(DEBUG) <<"XCCHL1Decoder "<< mU;

	// The GSM L1 u-frame has a 40-bit parity field.
	// False detections are EXTREMELY rare.
	// Parity check of u[].
	// GSM 05.03 4.1.2.
	mP.invert();							// parity is inverted
	// The syndrome should be zero.
	OBJLOG(DEBUG) <<"XCCHL1Decoder d[]:p[]=" << mDP;
	unsigned syndrome = mBlockCoder.syndrome(mDP);
	OBJLOG(DEBUG) <<"XCCHL1Decoder syndrome=" << hex << syndrome << dec;
	// Simulate high FER for testing?
	if (random()%100 < gConfig.getNum("Test.GSM.SimulatedFER.Uplink")) {
		OBJLOG(NOTICE) << "XCCHL1Decoder simulating dropped uplink frame at " << mReadTime;
		return false;
	}
	return (syndrome==0);
}



void XCCHL1Decoder::handleGoodFrame()
{
	OBJLOG(DEBUG) <<"XCCHL1Decoder u[]=" << mU;

	/*** Moved to L2LogicalChannel.
	{
		ScopedLock lock(mDecLock,__FILE__,__LINE__);
		// Keep T3109 from timing out.
		//mT3109.set();
		// If this is the first good frame of a new transaction,
		// stop T3101 and tell L2 we're alive down here.
		if (mT3101.active()) {
			mT3101.reset();
			// This does not block; goes to a InterthreadQueue L2LAPdm::mL1In
			if (mUpstream!=NULL) mUpstream->writeLowSide(L2Frame(ESTABLISH));
		}
	}
	***/

	// Get the d[] bits, the actual payload in the radio channel.
	// Undo GSM's LSB-first octet encoding.
	OBJLOG(DEBUG) <<"XCCHL1Decoder d[]=" << mD;

	if (mUpstream) {
		// Are we fuzzing ourselves?
		if (random()%100 < gConfig.getNum("Test.GSM.UplinkFuzzingRate")) {
			size_t i = random() % mD.size();
			mD[i] = 1 - mD[i];
			OBJLOG(NOTICE) << "XCCHL1Decoder fuzzing input frame, flipped bit " << i;
		}
		// Send all bits to GSMTAP
		if (gConfig.getBool("Control.GSMTAP.GSM")) {
			// FIXME -- This repeatLengh>51 is a bit of a hack.
			gWriteGSMTAP(ARFCN(),TN(),mReadTime.FN(),typeAndOffset(),mMapping.repeatLength()>51,true,mD);
		}
		// Build an L2 frame and pass it up.
		const BitVector2 L2Part(mD.tail(headerOffset()));
		OBJLOG(DEBUG) <<"XCCHL1Decoder L2=" << L2Part;
		mUpstream->writeLowSide(L2Frame(L2Part/*,DATA*/));
	} else {
		OBJLOG(ERR) << "XCCHL1Decoder with no uplink connected.";
	}
}

// Get the physical parameters of the burst.
void MSPhysReportInfo::processPhysInfo(const RxBurst &inBurst)
{
	// RSSI is dB wrt full scale.
	unsigned count = min((int)mReportCount,(int)gConfig.getNum("GSM.Radio.RSSIAveragePeriod"));
	mRSSI = (inBurst.RSSI()  + count * mRSSI) / (count+1);

	// Timing error is a float in symbol intervals.
	// (pat) It is the timing error of the received bursts which means it is relative to the Timing Advance currently in use.
	count = min(mReportCount,cAveragePeriodTiming);
	mTimingError = (inBurst.timingError()  + count * mTimingError) / (count+1);

	// Timestamp
	mTimestamp = gBTS.clock().systime(inBurst.time());

	OBJLOG(INFO) << "SACCHL1Decoder " << " RSSI=" <<mRSSI << " burst.RSSI="<<inBurst.RSSI() \
		<< " timestamp=" << mTimestamp \
			<< " timingError=" << inBurst.timingError() << LOGVARM(mReportCount);
	mReportCount++;
}


bool SACCHL1Decoder::processBurst(const RxBurst& inBurst)
{
	// TODO -- One quick test of burst validity is to look at the tail bits.
	// We could do that as a double-check against putting garbage into
	// the interleaver or accepting bad parameters.

	// TODO: We shouldnt save the phys info if the burst is bad.
	processPhysInfo(inBurst);
	return XCCHL1Decoder::processBurst(inBurst);
}


void SACCHL1Decoder::handleGoodFrame()
{
	// GSM 04.04 7
	OBJLOG(DEBUG) << "SACCHL1Decoder "<<" phy header " << mU.head(16);
	mActualMSPower = decodePower(mU.peekField(3,5));
	int TAField = mU.peekField(9,7);
	if (TAField<64) mActualMSTiming = TAField;
	OBJLOG(BLATHER) << "actuals pow=" << mActualMSPower << " TA=" << mActualMSTiming;
	XCCHL1Decoder::handleGoodFrame();
}


// Process the 184 bit frame, starting at offset, add parity, encode.
// Result is left in mI, representing 4 radio bursts.
void SharedL1Encoder::encodeFrame41(const BitVector2 &src, int offset, bool copy)
{
	if (copy) src.copyToSegment(mU,offset);
	OBJLOG(DEBUG) << "before d[]=" << mD;
	mD.LSB8MSB();
	OBJLOG(DEBUG) << "after d[]=" << mD;
	encode41();
	interleave41();
}


XCCHL1Encoder::XCCHL1Encoder(
		unsigned wCN,
		unsigned wTN,
		const TDMAMapping& wMapping,
		L1FEC* wParent)
	: SharedL1Encoder(),
	  L1Encoder(wCN,wTN,wMapping,wParent)
{
	mFillerBurst = TxBurst(gDummyBurst);

	// Set up the training sequence and stealing bits
	// since they'll be the same for all bursts.

	// stealing bits for a control channel, GSM 05.03 4.2.5, 05.02 5.2.3.
	// (pat) For a GPRS channel these bits are used for the encoding, 1,1 implies CS-1.
	// Since we will be sharing the channels between GSM and GPRS, we cannot depend
	// on this preinitialization surviving.  This is so minor, I am just going
	// to set these anew inside transmit().
	//mBurst.Hl(1);
	//mBurst.Hu(1);

	// training sequence, GSM 05.02 5.2.3
	gTrainingSequence[mTSC].copyToSegment(mBurst,61);
}


// Default initialization is as for XCCH channels (SACCH) or CS-1 encoding.
// Pat says: From GSM04.03sec5.1 the 40 bit parity is generated by the polynomial:
// g(D) = (D23 + 1)*(D17 + D3 + 1) = 1 + D3 + D17 + D23 + D26 + D40,
// which are the bits set in Parity initialization below.
void SharedL1Encoder::initInterleave(int mIsize)
{
	// Set up the interleaving buffers.
	for(int k = 0; k<mIsize; k++) {
		mI[k].resize(114);
		mE[k].resize(114);
		// Fill with zeros just to make Valgrind happy.
		mI[k].fill(0);
		mE[k].fill(0);
	}
}

SharedL1Encoder::SharedL1Encoder():
	mBlockCoder(0x10004820009ULL, 40, 224),
	mC(456), mU(228),
	mD(mU.head(184)),
	mP(mU.segment(184,40))
{
	initInterleave(4);
	mU.zero();	// zeros out the tail bits.
	mC.zero();	// Be safe; only happens once, ever.
}


void XCCHL1Encoder::writeHighSide(const L2Frame& frame)
{
	//assert(frame.primitive() == DATA);
	if (!encActive()) { OBJLOG(INFO) << "XCCHL1Encoder::writeHighSide sending on non-active channel "; }
	resync();
	sendFrame(frame);
}



void XCCHL1Encoder::sendFrame(const L2Frame& frame)
{
	OBJLOG(DEBUG) << frame;
	// Make sure there's something down there to take the bursts.
	if (mDownstream==NULL) {
		LOG(WARNING) << "XCCHL1Encoder with no downstream";
		return;
	}

	// This comes from GSM 05.03 4.1

	// Send to GSMTAP
	frame.copyToSegment(mU,headerOffset());
	if (gConfig.getBool("Control.GSMTAP.GSM")) {
		gWriteGSMTAP(ARFCN(),TN(),mNextWriteTime.FN(),typeAndOffset(),mMapping.repeatLength()>51,false,mU);
	}


	// Copy the L2 frame into u[] for processing.
	// GSM 05.03 4.1.1.
	//LOG(DEBUG) << "mU=" << mU.inspect();
	//LOG(DEBUG) << "mD=" << mD.inspect();
	// Process the 184 bit frame, leave result in mI.
	//mFECEnc.encodeFrame41(frame,headerOffset(),mFECEnc.mVCoder);
	encodeFrame41(frame,headerOffset(), false);
	const int qCS1[8] = { 1,1,1,1,1,1,1,1 };   // magically identifies CS-1.
	transmit(mI,mE,qCS1);
}


void SharedL1Encoder::encode41()
{
	// Perform the FEC encoding of GSM 05.03 4.1.2 and 4.1.3

	// GSM 05.03 4.1.2
	// Generate the parity bits.
	mBlockCoder.writeParityWord(mD,mP);
	OBJLOG(DEBUG) << "u[]=" << mU;
	// GSM 05.03 4.1.3
	// Apply the convolutional encoder.
	//mU.encode(mVCoder,mC);
	mVCoder.encode(mU,mC);
	OBJLOG(DEBUG) << "c[]=" << mC;
}



void SharedL1Encoder::interleave41()
{
	// GSM 05.03, 4.1.4.  Verbatim.
	for (int k=0; k<456; k++) {
		int B = k%4;
		int j = 2*((49*k) % 57) + ((k%8)/4);
		mI[B][j] = mC[k];
	}
}



// (pat) This code is not used for gprs, but part of the L1Encoder is now shared
// with gprs, and the stealing bits may be modified if the channel is used for
// gprs, so this function is modified to set the stealing bits properly
// before each transmission, rather than having them be static.
// The qbits, also called stealing bits, are defined in GSM05.03.
// For GPRS they specify the encoding type: CS-1 through CS-4.
void L1Encoder::transmit(BitVector2 *mI, BitVector2 *mE, const int *qbits)
{
	// Format the bits into the bursts.
	// GSM 05.03 4.1.5, 05.02 5.2.3
	waitToSend();		// Don't get too far ahead of the clock.

	if (!mDownstream) {
		// For some testing, we might not have a radio connected.
		// That's OK, as long as we know it.
		LOG(WARNING) << "XCCHL1Encoder with no radio, dumping frames";
		return;
	}

	// add noise
	// the noise insertion happens below, merged in with the ciphering
	int p = gConfig.getFloat("GSM.Cipher.CCHBER") * (float)0xFFFFFF;

	for (int qi=0,B=0; B<4; B++) {
		mBurst.time(mNextWriteTime);
		// encrypt y
		if (mEncrypted == ENCRYPT_YES) {
			unsigned char block1[15];
			unsigned char block2[15];
			unsigned char *kc = parent()->decoder()->kc();
			// 03.20 C.1.2
			// 05.02 3.3.2.2.1
			int fn = mNextWriteTime.FN();
			int t1 = fn / (26*51);
			int t2 = fn % 26;
			int t3 = fn % 51;
			int count = (t1<<11) | (t3<<5) | t2;
			if (mEncryptionAlgorithm == 1) {
				A51_GSM(kc, 64, count, block1, block2);
			} else if (mEncryptionAlgorithm == 3) {
				A53_GSM(kc, 64, count, block1, block2);
			} else {
				devassert(0);
			}
			for (int i = 0; i < 114; i++) {
				int b = p ? (random() & 0xFFFFFF) < p : 0;
				b = b ^ (block1[i/8] >> (7-(i%8)));
				mE[B].settfb(i, mI[B].bit(i) ^ (b&1));
			}
		} else {
			if (p) {
				for (int i = 0; i < 114; i++) {
					int b = (random() & 0xFFFFFF) < p;
					mE[B].settfb(i, mI[B].bit(i) ^ b);
				}
			} else {
				// no noise or encryption. use mI below.
			}
		}

		// Copy in the "encrypted" bits, GSM 05.03 4.1.5, 05.02 5.2.3.
		if (p || mEncrypted == ENCRYPT_YES) {
			OBJLOG(DEBUG) << "transmit mE["<<B<<"]=" << mE[B];
			mE[B].segment(0,57).copyToSegment(mBurst,3);
			mE[B].segment(57,57).copyToSegment(mBurst,88);
		} else {
			// no noise or encryption.  use mI.
			OBJLOG(DEBUG) << "transmit mI["<<B<<"]=" << mI[B];
			mI[B].segment(0,57).copyToSegment(mBurst,3);
			mI[B].segment(57,57).copyToSegment(mBurst,88);
		}
		mBurst.Hl(qbits[qi++]);
		mBurst.Hu(qbits[qi++]);
		// Send it to the radio.
		OBJLOG(DEBUG) << "transmit mBurst=" << mBurst;
		mDownstream->writeHighSideTx(mBurst,"Shared");
		rollForward();
	}
}



void GeneratorL1Encoder::serviceStart()
{
	//L1Encoder::encStart();
	mSendThread.start((void*(*)(void*))GeneratorL1EncoderServiceLoopAdapter,(void*)this);
}



void *GeneratorL1EncoderServiceLoopAdapter(GeneratorL1Encoder* gen)
{
	gen->serviceLoop();
	// DONTREACH
	return NULL;
}

void GeneratorL1Encoder::serviceLoop()
{
	while (mRunning && !gBTS.btsShutdown()) {
		resync();
		waitToSend();
		generate();
	}
}




SCHL1Encoder::SCHL1Encoder(L1FEC* wParent, unsigned wTN)
	:GeneratorL1Encoder(0,wTN,gSCHMapping,wParent),
	mBlockCoder(0x0575,10,25),
	mU(25+10+4), mE(78),
	mD(mU.head(25)),mP(mU.segment(25,10)),
	mE1(mE.segment(0,39)),mE2(mE.segment(39,39))
{
	// The SCH extended training sequence.
	// GSM 05.02 5.2.5.
	static const BitVector2 xts("1011100101100010000001000000111100101101010001010111011000011011");
	xts.copyToSegment(mBurst,42);
	// Set the tail bits in u[] now, just once.
	mU.fillField(35,0,4);
}



void SCHL1Encoder::generate()
{
	OBJLOG(DEBUG) << "SCHL1Encoder " << mNextWriteTime;
	assert(mDownstream);
	// Data, GSM 04.08 9.1.30
	size_t wp=0;
	mD.writeField(wp,gBTS.BSIC(),6);
	mD.writeField(wp,mNextWriteTime.T1(),11);
	mD.writeField(wp,mNextWriteTime.T2(),5);
	mD.writeField(wp,mNextWriteTime.T3p(),3);
	mD.LSB8MSB();
	// Encoding, GSM 05.03 4.7
	// Parity
	mBlockCoder.writeParityWord(mD,mP);
	// Convolutional encoding
	//mU.encode(mVCoder,mE);
	mVCoder.encode(mU,mE);
	// Mapping onto a burst, GSM 05.02 5.2.5.
	mBurst.time(mNextWriteTime);
	mE1.copyToSegment(mBurst,3);
	mE2.copyToSegment(mBurst,106);
	// Send it already!
	mDownstream->writeHighSideTx(mBurst,"SCH");
	rollForward();
}






FCCHL1Encoder::FCCHL1Encoder(L1FEC *wParent, unsigned wTN)
	:GeneratorL1Encoder(0,wTN,gFCCHMapping,wParent)
{
	mBurst.zero();
	mFillerBurst.zero();
}


void FCCHL1Encoder::generate()
{
	OBJLOG(DEBUG) << "FCCHL1Encoder " << mNextWriteTime;
	assert(mDownstream);
	resync();
	for (int i=0; i<5; i++) {
		mBurst.time(mNextWriteTime);
		mDownstream->writeHighSideTx(mBurst,"FCCH");
		rollForward();
	}
	sleep(1);
}




void NDCCHL1Encoder::serviceStart()
{
	//L1Encoder::encStart();
	mSendThread.start((void*(*)(void*))NDCCHL1EncoderServiceLoopAdapter,(void*)this);
}



void *NDCCHL1EncoderServiceLoopAdapter(NDCCHL1Encoder* gen)
{
	gen->serviceLoop();
	// DONTREACH
	return NULL;
}

void NDCCHL1Encoder::serviceLoop()
{
	while (mRunning && !gBTS.btsShutdown()) {
		generate();
	}
}





void BCCHL1Encoder::generate()
{
	OBJLOG(DEBUG) << "BCCHL1Encoder " << mNextWriteTime;
	// BCCH mapping, GSM 05.02 6.3.1.3
	// Since we're not doing GPRS or VGCS, it's just SI1-4 over and over.
	// pat 8-2011: If we are doing GPRS, the SI13 must be in slot 4.
	switch (mNextWriteTime.TC()) {
		// (pat) Maps to: XCCHL1Encoder::writeHighSide.
		case 0: writeHighSide(gBTS.SI1Frame()); return;
		case 1: writeHighSide(gBTS.SI2Frame()); return;
		case 2: writeHighSide(gBTS.SI3Frame()); return;
		case 3: writeHighSide(gBTS.SI4Frame()); return;
		//case 4: writeHighSide(GPRS::GPRSConfig::IsEnabled() ? gBTS.SI13Frame() : gBTS.SI3Frame());
		case 4: writeHighSide(gBTS.SI13() ? gBTS.SI13Frame() : gBTS.SI3Frame());
			return;
		case 5: writeHighSide(gBTS.SI2Frame()); return;
		case 6: writeHighSide(gBTS.SI3Frame()); return;
		case 7: writeHighSide(gBTS.SI4Frame()); return;
		default: assert(0);
	}
}




// TCH_FS
TCHFACCHL1Decoder::TCHFACCHL1Decoder(
	unsigned wCN,
	unsigned wTN,
	const TDMAMapping& wMapping,
	L1FEC *wParent)
	:XCCHL1Decoder(wCN,wTN, wMapping, wParent)
{
	for (int i=0; i<8; i++) {
		mE[i] = SoftVector(114);
		mI[i] = SoftVector(114);
		// Fill with zeros just to make Valgrind happy.
		mI[i].fill(.0);
		mE[i].fill(.0);
	}
}

ViterbiBase *newViterbi(AMRMode mode)
{
	switch (mode) {
		case TCH_AFS12_2: return new ViterbiTCH_AFS12_2();
		case TCH_AFS10_2: return new ViterbiTCH_AFS10_2();
		case TCH_AFS7_95: return new ViterbiTCH_AFS7_95();
		case TCH_AFS7_4:  return new ViterbiTCH_AFS7_4();
		case TCH_AFS6_7:  return new ViterbiTCH_AFS6_7();
		case TCH_AFS5_9:  return new ViterbiTCH_AFS5_9();
		case TCH_AFS5_15: return new ViterbiTCH_AFS5_15();
		case TCH_AFS4_75: return new ViterbiTCH_AFS4_75();
		case TCH_FS: return new ViterbiR2O4();
		default: assert(0);
	}
};

void TCHFRL1Decoder::setAmrMode(AMRMode wMode)
{
	mAMRMode = wMode;
	int kd = gAMRKd[wMode];	// decoded payload size.
	mTCHD.resize(kd);
	mPrevGoodFrame.resize(kd);
	mPrevGoodFrame.zero();	// When switching modes the contents of this are garbage, so zero.
	mNumBadFrames = 0;
	setViterbi(wMode);
	if (wMode == TCH_FS) {
		assert(kd == 260);
		mTCHU.resize(189);
		//mClass1_c.dup(mC.head(378));		// no longer used
		mClass1A_d.dup(mTCHD.head(50));
		//mClass2_c.dup(mC.segment(378,78));	// no longer used.
		mTCHParity = Parity(0x0b,3,50);
	} else {
		mTCHU.resize(kd+6);		// why +6?
		mClass1A_d.dup(mTCHD.head(mClass1ALth));
		mTCHParity = Parity(0x06f,6,gAMRClass1ALth[wMode]);
		mAMRBitOrder = gAMRBitOrder[wMode];
		mClass1ALth = gAMRClass1ALth[wMode];
		mClass1BLth = gAMRKd[wMode] - gAMRClass1ALth[wMode];
		mTCHUC.resize(gAMRTCHUCLth[wMode]);
		mPuncture = gAMRPuncture[wMode];
		mPunctureLth = gAMRPunctureLth[wMode];
		setViterbi(wMode);	//mViterbiSet.getViterbi(wMode);
	}
}


void TCHFACCHL1Decoder::writeLowSideRx(const RxBurst& inBurst)
{
	L1FEC *fparent = parent();
	if (fparent->mGprsReserved) {	// Channel is reserved for gprs.
		if (parent()->mGPRSFEC) {	// If set, bursts are delivered to this FEC in GPRS.
			GPRS::GPRSWriteLowSideRx(inBurst, parent()->mGPRSFEC);
		}
		return;	// done
	}
	OBJLOG(DEBUG) << "TCHFACCHL1Decoder " << inBurst <<LOGVAR(mHandoverPending);	// <<LOGVAR(mT3101.remaining());
	// If the channel is closed, ignore the burst.
	if (!decActive()) {
		OBJLOG(DEBUG) << "TCHFACCHL1Decoder not active, ignoring input";
		return;
	}
	ScopedLock lock(mDecLock,__FILE__,__LINE__);	// this better be redundant.
	if (mHandoverPending && ! mT3103.expired()) {
		// If this channel is waiting for an inbound handover,
		// try to decode a handover access burst.
		// GSM 05.03 4.9, 4.6
		// Based on the RACHL1Decoder.

		//LOG(DEBUG) << "handover access " << inBurst;
		OBJLOG(NOTICE) << "handover access " << inBurst;

		// Decode the burst.
		const SoftVector e(inBurst.segment(49,36));
		//e.decode(mVCoder,mHU);
		mVCoder.decode(e,mHU);
		OBJLOG(DEBUG) << "handover access U=" << mHU;
		// Check the tail bits -- should all the zero.
		if (mHU.peekField(14,4)) return;
		// Check the parity.
		unsigned sentParity = ~mHU.peekField(8,6);
		unsigned checkParity = mHD.parity(mHParity);
		unsigned encodedBSIC = (sentParity ^ checkParity) & 0x03f;
		OBJLOG(DEBUG) << "handover access sentParity " << sentParity
			<< " checkParity " << checkParity
			<< " endcodedBSIC " << encodedBSIC;
		if (encodedBSIC != gBTS.BSIC()) return;
		// OK.  So we got a burst.
		mHD.LSB8MSB();
		unsigned ref = mHD.peekField(0,8);

		// l3rewrite validates the handover ref down here in L1 rather than calling up to L3.
		// oops, guess it doesnt.
		// if (!Control::SaveHandoverAccess(ref,inBurst.RSSI(),inBurst.timingError(),inBurst.time())) return;
		// mUpstream->writeLowSide(HANDOVER_ACCESS);

		if (ref == mHandoverRef) { 
			OBJLOG(NOTICE) << "queuing HANDOVER_ACCESS ref=" << ref;
			mT3103.reset();
			double when = gBTS.clock().systime(inBurst.time());
			mHandoverRecord = HandoverRecord(inBurst.RSSI(),inBurst.timingError(),when);
			mUpstream->writeLowSide(L2Message(HANDOVER_ACCESS));
			// (pat) FIXME: We need to set the PHY params from the handover burst so that SACCH will be transmitting the correct TA.
		} else {
			OBJLOG(ERR) << "no inbound handover with reference " << ref;
		}

		return;
	}
	mDecoderStats.countSNR(inBurst);
	processBurst(inBurst);
}



// (pat) How the burst gets here:
// TRXManager.cpp has a wDemuxTable for each frame+timeslot with a pointer to
// a virtual L1Decoder::writeLowSideRx() function.  For traffic channels, this maps to 
// XCCHL1Decoder::writeLowSideRx(), which checks active(), and if true,
// then calls this, and if this returns true, goes ahead with decoding.
bool TCHFACCHL1Decoder::processBurst( const RxBurst& inBurst)
{
	// Accept the burst into the deinterleaving buffer.
	// Return true if we are ready to interleave.

	// TODO -- One quick test of burst validity is to look at the tail bits.
	// We could do that as a double-check against putting garbage into
	// the interleaver or accepting bad parameters.
	// (pat) I think the above is a bad idea since there is no error correction on the tail bits.
	// If our viterbi decoder were smarter it would be generating estimated BER during decoding.

	// The reverse index runs 0..7 as the bursts arrive.
	// It is the "B" index of GSM 05.03 3.1.3 and 3.1.4.
	int B = mMapping.reverseMapping(inBurst.time().FN()) % 8;
	// A negative value means that the demux is misconfigured.
	assert(B>=0);
	OBJLOG(DEBUG) << "TCHFACCHL1Decoder B=" << B << " " << inBurst;

	// Pull the data fields (e-bits) out of the burst and put them into i[B][].
	// GSM 05.03 3.1.4
	inBurst.data1().copyToSegment(mI[B],0);
	inBurst.data2().copyToSegment(mI[B],57);

	// save the frame numbers for each burst for possible decryption later
	mFN[B] = inBurst.time().FN();
	stealBitsL[B] = inBurst.Hl();
	stealBitsU[B] = inBurst.Hu();

	// Every 4th frame is the start of a new block.
	// So if this isn't a "4th" frame, return now.
	if (B%4!=3) return false;

	if (mEncrypted == ENCRYPT_MAYBE) {
		saveMi();
	}

	if (mEncrypted == ENCRYPT_YES) {
		decrypt(B);
	}

	// Deinterleave according to the diagonal "phase" of B.
	// See GSM 05.03 3.1.3.
	// Deinterleaves i[] to c[]
	if (B==3) deinterleaveTCH(4);
	else deinterleaveTCH(0);

	// See if this was the end of a stolen frame, GSM 05.03 4.2.5.
	// (pat) There are 8 bits to determine if the frame is stolen.  If they are all set one
	// way or the other, that is a pretty good indication the frame is stolen or not, but
	// if they are ambiguous, we will try decoding the frame as FACCH to check parity, which is a much stronger condition.
	//bool stolen = inBurst.Hl();
	unsigned stolenbits = 0;		// Number of stolen bit markers.  In the range 0 .. 8
	if (B == 3) {
		stolenbits = stealBitsU[4] + stealBitsU[5] + stealBitsU[6] + stealBitsU[7] +
			stealBitsL[0] + stealBitsL[1] + stealBitsL[2] + stealBitsL[3];
	} else {
		stolenbits = stealBitsU[0] + stealBitsU[1] + stealBitsU[2] + stealBitsU[3] +
			stealBitsL[4] + stealBitsL[5] + stealBitsL[6] + stealBitsL[7];
	}
	OBJLOG(DEBUG) <<"TCHFACCHL1Decoder Hl=" << inBurst.Hl() << " Hu=" << inBurst.Hu();
	bool okFACCH = false;
	if (stolenbits) {	// If any of the 8 stolen bits are set, try decoding as FACCH.
		okFACCH = decode();	// Calls SharedL1Decoder::decode() to decode mC into mU
		if (!okFACCH && mEncrypted == ENCRYPT_MAYBE) {
			// (doug) We don't want to start decryption until we get the (encrypted) layer 2 acknowledgement
			// of the Ciphering Mode Command, so we start maybe decrypting when we send the command,
			// and when the frame comes along, we'll see that it doesn't pass normal decoding, but
			// when we try again with decryption, it will pass.  Unless it's just noise.
			OBJLOG(DEBUG) << "TCHFACCHL1Decoder: try decoding again with decryption";
			restoreMi();
			decrypt(-1);
			// re-deinterleave
			if (B==3) deinterleaveTCH(4);
			else deinterleaveTCH(0);
			// re-decode
			okFACCH = decode();
			if (okFACCH) {
				OBJLOG(DEBUG) << "TCHFACCHL1Decoder: success on 2nd try";
				// We've successfully decoded an encrypted frame.  Start decrypting all uplink frames.
				mEncrypted = ENCRYPT_YES;
				// Also start encrypting downlink frames.
				parent()->encoder()->mEncrypted = ENCRYPT_YES;
				parent()->encoder()->mEncryptionAlgorithm = mEncryptionAlgorithm;
			}
		}

		if (okFACCH) {	// This frame was stolen for sure.
			OBJLOG(DEBUG) <<"TCHFACCHL1Decoder good FACCH frame";
			//countGoodFrame();
			mD.LSB8MSB();
			// This also resets T3109.
			handleGoodFrame();
		}
	}

	// Always feed the traffic channel, even on a stolen frame.
	// decodeTCH will handle the GSM 06.11 bad frame processing.
	// (pat) If the frame was truly stolen but was too corrupt to decrypt we dont want to push it
	// into the audio frame because there are only 3 parity bits on the audio frame so the chance of
	// misinterpreting it is significant.  To try to reduce that probability I am adding a totally
	// arbitrary check on the number of stealing bits that were set; I made this number up from thin air.
	bool traffic = decodeTCH(okFACCH || stolenbits > 5,&mC);
	// Now keep statistics...
	if (okFACCH) {
		countStolenFrame(1);	// was 4, why?  We are counting frames, which occur every 4 bursts (even though they are spread over 8 bursts.)
		countBER(mVCoder.getBEC(),378);
	} else if (traffic) {
		OBJLOG(DEBUG) <<"TCHFACCHL1Decoder good TCH frame";
		countGoodFrame(1);	// was 4, why?
		countBER(mVCoder.getBEC(),378);
		// Don't let the channel timeout.
		//ScopedLock lock(mDecLock,__FILE__,__LINE__);
		// (pat 4-2014) There are only 3 parity bits on the speech frame so the false-positive detection probability is high,
		// resulting in setting T3109 and preventing the channel from being recycled.  Not sure what to do, because
		// there may not be any FACCH frames to set T3109.
		//mT3109.set();
	}
	else countBadFrame(4);

	return true;	// note: result not used by this class.
}


void TCHFACCHL1Decoder::saveMi()
{
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 114; j++) {
			mE[i].settfb(j, mI[i].softbit(j));
		}
	}
}

void TCHFACCHL1Decoder::restoreMi()
{
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 114; j++) {
			mI[i].settfb(j, mE[i].softbit(j));
		}
	}
}

void TCHFACCHL1Decoder::decrypt(int B)
{
	// decrypt x
	unsigned char block1[15];
	unsigned char block2[15];
	int bb = B==7 ? 4 : 0;
	int be = B<0 ? 8 : bb+4;
	for (int i = bb; i < be; i++) {
		// 03.20 C.1.2
		// 05.02 3.3.2.2.1
		int fn = mFN[i];
		int t1 = fn / (26*51);
		int t2 = fn % 26;
		int t3 = fn % 51;
		int count = (t1<<11) | (t3<<5) | t2;
		if (mEncryptionAlgorithm == 1) {
			A51_GSM(mKc, 64, count, block1, block2);
		} else if (mEncryptionAlgorithm == 3) {
			A53_GSM(mKc, 64, count, block1, block2);
		} else {
			devassert(0);
		}
		for (int j = 0; j < 114; j++) {
			if ((block2[j/8] & (0x80 >> (j%8)))) {
				mI[i].settfb(j, 1.0 - mI[i].softbit(j));
			}
		}
	}
}



void TCHFACCHL1Decoder::deinterleaveTCH(int blockOffset )
{
	OBJLOG(DEBUG) <<"TCHFACCHL1Decoder blockOffset=" << blockOffset;
	for (int k=0; k<456; k++) {
		int B = ( k + blockOffset ) % 8;
		int j = 2*((49*k) % 57) + ((k%8)/4);
		mC[k] = mI[B][j];
		mI[B][j] = 0.5F;
	}
}

void TCHFACCHL1Decoder::addToSpeechQ(AudioFrame *newFrame)  { mSpeechQ.write(newFrame); }


// (pat) See GSM 6.12 5.2 and 5.03 table 2 (in section 5.4)
// I did not get this to work in that I did not see any silence frames.
static bool isSIDFrame(BitVector &frame)
{
	// A SID frame is marked by all zeros in particular positions in the RPE pulse data.
	// If all the above bits are 0, it is a SID frame.
	const char *frameMarker[4] = {
		// There are 13 RPE pulses per frame.
		// In the first three sub-frames, two bits of each RPE pulse are considered.
		// I dont know which direction the bits go within each variable.
		"00x00x00x00x00x00x00x00x00x00x00x00x00x",
		"00x00x00x00x00x00x00x00x00x00x00x00x00x",
		"00x00x00x00x00x00x00x00x00x00x00x00x00x",
		// In the fourth sub-frame bit one is included only for the first 4 RPE pulses (numbers 64-67 inclusive)
		"00x00x00x00x0xx0xx0xx0xx0xx0xx0xx0xx0xx" };
		//"x00x00x00x00x00x00x00x00x00x00x00x00x00",
		//"x00x00x00x00x00x00x00x00x00x00x00x00x00",
		//"x00x00x00x00x00x00x00x00x00x00x00x00x00",
		//"x00x00x00x00xx0xx0xx0xx0xx0xx0xx0xx0xx0"

	if (0) {
		// Print out the SID bits
		char buf[200], *bp = buf;
		for (unsigned f = 0; f < 4; f++) {		// For each voice sub-frame
			const char *fp = frame.begin() + 36 + 17 + f * 56; 	// RPE params start at bit 17.
			const char *zb = frameMarker[f];
			for (; *zb; zb++) {
				if (*zb == '0') *bp++ = '0' + *fp++;
			}
		}
		*bp = 0;
		printf("SID=%s\n",buf);
	}

	for (unsigned f = 0; f < 4; f++) {		// For each voice sub-frame
		const char *fp = frame.begin() + 36 + 17 + f * 56; 	// RPE params start at bit 17.
		const char *zb = frameMarker[f];
		for (; *zb; zb++, fp++) {
			if (*zb != 'x') {
				if (*fp != 0) { return false; }	// Not a SID frame.
			}
		}
	}
	return true;	// All the non-X bits were 0 so it is a SID frame.
}

// GSM Full Rate Speech Frame GSM 6.10 1.7
struct BitPos { unsigned start; unsigned length; };
static BitPos GSMFRS_LAR_Positions[8] = {	// Log Area Ratio bit positions within full rate speech frame.
	{ 0, 6 },
	{ 6, 6 },
	{ 12, 5 },
	{ 17, 5 },
	{ 22, 4 },
	{ 26, 4 },
	{ 30, 3 },
	{ 33, 3 }
	};

struct GSMFRSpeechFrame {
	BitVector &fr;
	static const unsigned frsHeaderSize = 36;
	static const unsigned frsSubFrameSize = 56;
	GSMFRSpeechFrame(BitVector &other) : fr(other) {}
	unsigned getLAR(unsigned n) {
		assert(n >= 1 && n <= 8);
		BitPos lar = GSMFRS_LAR_Positions[n-1];
		return fr.peekField(lar.start,lar.length);
	}
	void setLAR(unsigned n, unsigned value) {
		assert(n >= 1 && n <= 8);
		BitPos lar = GSMFRS_LAR_Positions[n-1];
		fr.fillField(lar.start,value,lar.length);
	}
	void setLTPLag(unsigned subFrame /*0..3*/, unsigned value) {
		fr.fillField(frsHeaderSize + frsSubFrameSize * subFrame + 0, value, 7);
	}
	void setLTPGain(unsigned subFrame, unsigned value) {
		fr.fillField(frsHeaderSize + frsSubFrameSize * subFrame + 7, value, 2);
	}
	void setRPEGridPosition(unsigned subFrame, unsigned value) {
		fr.fillField(frsHeaderSize + frsSubFrameSize * subFrame + 9, value, 2);
	}
	int getBlockAmplitude(unsigned subFrame) {
		return (int) fr.peekField(frsHeaderSize + frsSubFrameSize * subFrame + 11, 6);
	}
	void setBlockAmplitude(unsigned subFrame, unsigned value) {
		fr.fillField(frsHeaderSize + frsSubFrameSize * subFrame + 11, value, 6);
	}
	void setLPEPulse(unsigned subFrame, unsigned pulseIndex /*1..13*/ , unsigned value) {
		assert(pulseIndex >= 1 && pulseIndex <= 13);
		fr.fillField(frsHeaderSize + frsSubFrameSize * subFrame + 17 + 3*(pulseIndex-1), value, 3);
	}
};

// (pat 1-2014) Make the BitVector into a silence frame.
// GSM 6.11 section 6 table 1 describes the silence frame, 6.10 1.7 the bit positions.
static void createSilenceFrame(BitVector &frame)
{
	GSMFRSpeechFrame sf(frame);
	sf.setLAR(1,42);
	sf.setLAR(2,39);
	sf.setLAR(3,21);
	sf.setLAR(4,10);
	sf.setLAR(5,9);
	sf.setLAR(6,4);
	sf.setLAR(7,3);
	sf.setLAR(8,2);
	for (unsigned f = 0; f <= 3; f++) {
		sf.setLTPGain(f,0);
		sf.setLTPLag(f,40);
		sf.setRPEGridPosition(f,1);
		sf.setBlockAmplitude(f,0);
		sf.setLPEPulse(f,1,3);
		sf.setLPEPulse(f,2,4);
		sf.setLPEPulse(f,3,3);
		sf.setLPEPulse(f,4,4);
		sf.setLPEPulse(f,5,4);
		sf.setLPEPulse(f,6,3);
		sf.setLPEPulse(f,7,3);
		sf.setLPEPulse(f,8,3);
		sf.setLPEPulse(f,9,4);
		sf.setLPEPulse(f,10,4);
		sf.setLPEPulse(f,11,4);
		sf.setLPEPulse(f,12,3);
		sf.setLPEPulse(f,13,3);
	}
}

// The input vector is an argument to make testing easier; we dont have to try to cram it into mC buried in the stack.
bool TCHFRL1Decoder::decodeTCH_GSM(bool stolen,const SoftVector *wC)
{
	// GSM 05.02 3.1.2, but backwards

	// Simulate high FER for testing?
	if (random()%100 < gConfig.getNum("Test.GSM.SimulatedFER.Uplink")) {
		OBJLOG(DEBUG) << "simulating dropped uplink vocoder frame at " << mReadTime;
		stolen = true;
	}

	// If the frame wasn't stolen, we'll update this with parity later.
	bool good = !stolen;

	// Good or bad, we will be sending *something* to the speech channel.
	// Allocate it in this scope.
	//unsigned char * newFrame = new unsigned char[33];
	AudioFrame *newFrame = new AudioFrameRtp(TCH_FS);

	if (!stolen) {

		// 3.1.2.2
		// decode from c[] to u[]
		//mClass1_c.decode(mVCoder,mTCHU);
		//wC->head(378).decode(mVCoder,mTCHU);
		mVCoder.decode(wC->head(378),mTCHU);
	
		// 3.1.2.2
		// copy class 2 bits c[] to d[]
		//mClass2_c.sliced().copyToSegment(mTCHD,182);
		wC->segment(378,78).sliced().copyToSegment(mTCHD,182);
	
		// 3.1.2.1
		// copy class 1 bits u[] to d[]
		for (unsigned k=0; k<=90; k++) {
			mTCHD[2*k] = mTCHU[k];
			mTCHD[2*k+1] = mTCHU[184-k];
		}
	
		// 3.1.2.1
		// check parity of class 1A
		unsigned sentParity = (~mTCHU.peekField(91,3)) & 0x07;
		unsigned calcParity = mClass1A_d.parity(mTCHParity) & 0x07;

		// 3.1.2.2
		// Check the tail bits, too.
		// (pat) Update: No we do not want to check the tail bits, because one of these
		// being bad would cause discarding the vector.
		//unsigned tail = mTCHU.peekField(185,4);
	
		OBJLOG(DEBUG) <<"TCHFACCHL1Decoder c[]=" << mC.begin()<<"="<< mC;
		//OBJLOG(DEBUG) <<"TCHFACCHL1Decoder mclass1_c[]=" << mClass1_c.begin()<< "="<<mClass1_c;
		OBJLOG(DEBUG) <<"TCHFACCHL1Decoder u[]=" << mTCHU;
		OBJLOG(DEBUG) <<"TCHFACCHL1Decoder d[]=" << mTCHD;
		OBJLOG(DEBUG) <<"TCHFACCHL1Decoder sentParity=" << sentParity \
			<< " calcParity=" << calcParity; // << " tail=" << tail;
		good = (sentParity==calcParity); //  && (tail==0);
		if (good) {
			// Undo Um's importance-sorted bit ordering.
			// See GSM 05.03 3.1 and Table 2.
#if 0		// pre-pat code:
			BitVector2 payload = mGsmVFrame.payload();
			mTCHD.unmap(g610BitOrder,260,payload);
			mGsmVFrame.pack(newFrame->begin());
			// Save a copy for bad frame processing.
			mGsmPrevGoodFrame.clone(mGsmVFrame);
#endif
			mTCHD.unmap(g610BitOrder,260,mPrevGoodFrame);	// Put the completed decoded data in mPrevGoodFrame.
			newFrame->append(mPrevGoodFrame);				// And copy it into the RTP audio frame.
			mNumBadFrames = 0;
		}
	}

	// We end up here for bad frames.
	// We also jump here directly for stolen frames.
	if (!good) {
		// Bad frame processing, GSM 06.11.
		// Attenuate block amplitudes and randomize grid positions.
		// The spec give the bit-packing format in GSM 06.10 1.7.
		// Note they start counting bits from 1, not 0.
		// (pat) The first 36 bits are LAR filter reflection coefficient parameters applicable to the entire 20ms frame.
		// See GSM 06.11 
		// These are followed by 4 sets of 56 bits of parameters for each 5ms sub-frame, consiting of:
		//	7 bits: N1, LTP lag
		//	2 bits: b1, LTP gain
		//	2 bits: M1, RPE grid position
		//	6 bits: Xmax, Block Amplitude
		//	3 bits * 13: x1(0) - x1(12), RPE-pulses
		// Annex 2 is Subjective relevance of speech coder bits.
		// Get xmax of the final sub-frame.
		// (pat) We only modify voice frames, not silence frames.
		// 1-2014 We dont get SID frames because we dont yet support DTX as specified in the beacon in L3CellOptionsBCCH,
		// so this code is zeroed out until we do...
		if (1 || !isSIDFrame(mPrevGoodFrame)) {
			mNumBadFrames++;
			if (mNumBadFrames >= 32) {
				createSilenceFrame(mPrevGoodFrame);
			} else  {
				// (pat 1-2014)  I changed this a little, but it did not help much.
				GSMFRSpeechFrame sf(mPrevGoodFrame);
				int xmax = sf.getBlockAmplitude(3); // The 'variable name' in 6.10 is 'xmax'.
				// Note: previously code took the average xmax of the prevGoodFrame, not the last xmax.
				// "Age" the frame.
				for (unsigned f=0; f<4; f++) {
					// First bad frame is an extrapolation of previous good frame, which we do by copying
					// the last xmax into all locations.  Subsequent bad frames are muted.
					// decrement block amplitude xmax.  I am lowering this faster than spec.
					if (mNumBadFrames > 1) { xmax -= 4; }
					if (xmax < 0) xmax = 0;
					sf.setBlockAmplitude(f,xmax);
					// randomize grid positions
					sf.setRPEGridPosition(f,random());
					if (xmax == 0) {
						// Dont kill the LTP gain until xmax is 0, or it sounds cruddy.  And it still sounds cruddy anyway.
						// mPrevGoodFrame.fillField(36 + 7 + f*56,0,2);
						createSilenceFrame(mPrevGoodFrame);
						mNumBadFrames = 32;		// So we dont have to do this again...
					}
				}
			}
		} else {
			printf("found SID frame\n"); fflush(stdout);
		}
		newFrame->append(mPrevGoodFrame);
	} else {
		//printf("good xmax=%u\n", (unsigned) mPrevGoodFrame.peekField(36 + 11,6));
	}

	// Good or bad, we must feed the speech channel.
	OBJLOG(DEBUG) <<"TCHFACCHL1Decoder sending" <<LOGVAR(*newFrame);
	addToSpeechQ(newFrame);
	return good;
}


bool TCHFRL1Decoder::decodeTCH_AFS(bool stolen, const SoftVector *wC)
{
	// GSM 05.03 3.1.2, but backwards
	// except for full speed AMR, which is 3.9.4

	// If the frame wasn't stolen, we'll update this with parity later.
	bool good = !stolen;

	// Good or bad, we will be sending *something* to the speech channel.
	// Allocate it in this scope.
	AudioFrame *newFrame = new AudioFrameRtp(mAMRMode);

	if (!stolen) {

		// 3.9.4.4
		// unpuncture from c[] to uc[]
		SoftVector cMinus8 = wC->segment(0, wC->size() - 8); // 8 id bits
		cMinus8.copyUnPunctured(mTCHUC, mPuncture, mPunctureLth);

		// 3.9.4.4
		// decode from uc[] to u[]
		mViterbi->decode(mTCHUC,mTCHU);

		// 3.9.4.3 -- class 1a bits in u[] to d[]
		for (unsigned k=0; k < mClass1ALth; k++) {
			mTCHD[k] = mTCHU[k];
		}

		// 3.9.4.3 -- class 1b bits in u[] to d[]
		for (unsigned k=0; k < mClass1BLth; k++) {
			mTCHD[k+mClass1ALth] = mTCHU[k+mClass1ALth+6];
		}

		// 3.9.4.3
		// check parity of class 1A
		unsigned sentParity = (~mTCHU.peekField(mClass1ALth,6)) & 0x3f;
		BitVector2 class1A = mTCHU.segment(0, mClass1ALth);
		unsigned calcParity = class1A.parity(mTCHParity) & 0x3f;

		OBJLOG(DEBUG) <<"TCHFACCHL1Decoder c[]=" << *wC;	// Does a copy.  Gotta love it.
		//OBJLOG(DEBUG) <<"TCHFACCHL1Decoder uc[]=" << mTCHUC;
		OBJLOG(DEBUG) <<"TCHFACCHL1Decoder u[]=" << mTCHU;
		OBJLOG(DEBUG) <<"TCHFACCHL1Decoder d[]=" << mTCHD;
		OBJLOG(DEBUG) <<"TCHFACCHL1Decoder sentParity=" << sentParity \
			<< " calcParity=" << calcParity;

		good = sentParity == calcParity;
		if (good) {
			// Undo Um's importance-sorted bit ordering.
			// See GSM 05.03 3.9.4.2 and Tables 7-14.
			//BitVector2 payload = mAmrVFrame.payload();
			//mTCHD.unmap(mAMRBitOrder,mKd,payload);
			//mAmrVFrame.pack(newFrame->begin());
			//// Save a copy for bad frame processing.
			//mAmrPrevGoodFrame.clone(mAmrVFrame);

			mTCHD.unmap(mAMRBitOrder,mPrevGoodFrame.size(),mPrevGoodFrame);	// Put the completed decoded data in mPrevGoodFrame.
			newFrame->append(mPrevGoodFrame);				// And copy it into the RTP audio frame.
		}
	}

	// We end up here for bad frames.
	// We also jump here directly for stolen frames.
	if (!good) {
		// FIXME: This cannot be correct for AMR.
#if FIXME
		// Bad frame processing, GSM 06.11.
		// Attenuate block amplitudes and randomize grid positions.
		// The spec give the bit-packing format in GSM 06.10 1.7.
		// Note they start counting bits from 1, not 0.
		int xmax = 0;
		for (unsigned i=0; i<4; i++) {
			xmax += mPrevGoodFrame.peekField(48+i*56-1,6);
		}
		xmax /= 4;
		// "Age" the frame.
		for (unsigned i=0; i<4; i++) {
			// decrement xmax
			if (xmax>0) xmax--;
			mPrevGoodFrame.fillField(48+i*56-1,xmax,6);
			// randomize grid positions
			mPrevGoodFrame.fillField(46+i*56-1,random(),2);
		}
#endif
		newFrame->append(mPrevGoodFrame);
	}

	// Good or bad, we must feed the speech channel.
	OBJLOG(DEBUG) <<"TCHFACCHL1Decoder sending" <<LOGVAR(*newFrame);
	addToSpeechQ(newFrame);
	return good;
}

bool TCHFRL1Decoder::decodeTCH(bool stolen, const SoftVector *wC)	// result goes to sendTCHUp()
{
	// Simulate high FER for testing?
	if (random()%100 < gConfig.getNum("Test.GSM.SimulatedFER.Uplink")) {
		OBJLOG(DEBUG) << "simulating dropped uplink vocoder frame at " << mReadTime;
		stolen = true;
	}

	// (pat) Slight weirdness to avoid modifying existing GSM_FR code too much.
	return (mAMRMode == TCH_FS) ? decodeTCH_GSM(stolen,wC) : decodeTCH_AFS(stolen,wC);
}


void TCHFACCHL1EncoderRoutine( TCHFACCHL1Encoder * encoder )
{
	while (!gBTS.btsShutdown()) {
		encoder->dispatch();
	}
}


// (pat) Leaving this here as a comment.
//GSMFRL1Encoder::GSMFRL1Encoder() :
//	mTCHU(189),mTCHD(260),
//	mClass1_c(mC.head(378)),
//	mClass1A_d(mTCHD.head(50)),
//	mClass2_d(mTCHD.segment(182,78)),
//	mTCHParity(0x0b,3,50)
//{
//}


void TCHFRL1Encoder::setAmrMode(AMRMode wMode)
{
	assert(wMode <= TCH_FS);
	mAMRMode = wMode;
	int kd = gAMRKd[wMode];		// The decoded payload size.
	mTCHRaw.resize(kd);
	mTCHD.resize(kd);
	if (wMode == TCH_FS) {
		mTCHU.resize(189);
		mClass1_c.dup(mC.head(378));
		mClass1A_d.dup(mTCHD.head(50));
		mClass2_d.dup(mTCHD.segment(182,78));
		mTCHParity = Parity(0x0b,3,50);
	} else {
		mTCHU.resize(kd+6);
		mAMRBitOrder = gAMRBitOrder[wMode];
		mClass1ALth = gAMRClass1ALth[wMode];
		mClass1BLth = kd - gAMRClass1ALth[wMode];
		mTCHUC.resize(gAMRTCHUCLth[wMode]);
		mPuncture = gAMRPuncture[wMode];
		mPunctureLth = gAMRPunctureLth[wMode];
		mTCHParity = Parity(0x06f,6,gAMRClass1ALth[wMode]);
		setViterbi(wMode); 	//mViterbi = mViterbiSet.getViterbi(wMode);
	}
}


// TCH_FS
TCHFACCHL1Encoder::TCHFACCHL1Encoder(
	unsigned wCN,
	unsigned wTN,
	const TDMAMapping& wMapping,
	L1FEC *wParent)
	:XCCHL1Encoder(wCN, wTN, wMapping, wParent), 
	mPreviousFACCH(true),mOffset(0)
{
	for(int k = 0; k<8; k++) {
		mI[k].resize(114);
		mE[k].resize(114);
		// Fill with zeros just to make Valgrind happy.
		mI[k].fill(0);
		mE[k].fill(0);
	}
}


void TCHFACCHL1Encoder::serviceStart()
{
	//L1Encoder::encStart();
	OBJLOG(DEBUG) <<"TCHFACCHL1Encoder";
	mEncoderThread.start((void*(*)(void*))TCHFACCHL1EncoderRoutine,(void*)this);
}




void TCHFACCHL1Encoder::encInit()
{
	// There was more stuff here at one time to justify overriding the default.
	// But it's gone now.
	XCCHL1Encoder::encInit();
	mPreviousFACCH = true;
}


void TCHFRL1Encoder::encodeTCH_GSM(const AudioFrame* aFrame)
{
	assert(mTCHRaw.size() == 260 && mTCHD.size() == 260);
	// GSM 05.03 3.1.2
	OBJLOG(DEBUG) <<"TCHFACCHL1Encoder";

	// The incoming ByteVector is an RTP payload type 3 (GSM), which uses the standard 4 bit RTP header.
	AudioFrameRtp rtpFrame(TCH_FS,aFrame);
	rtpFrame.getPayload(&mTCHRaw);		// Get the RTP frame payload into a BitVector2.
	mTCHRaw.map(g610BitOrder,260,mTCHD);

	// Reorder bits by importance.
	// See GSM 05.03 3.1 and Table 2.
	//mGsmVFrame.payload().map(g610BitOrder,260,mTCHD);

	// 3.1.2.1 -- parity bits
	BitVector2 p = mTCHU.segment(91,3);
   	mTCHParity.writeParityWord(mClass1A_d,p);

	// 3.1.2.1 -- copy class 1 bits d[] to u[]
	for (unsigned k=0; k<=90; k++) {
		mTCHU[k] = mTCHD[2*k];
		mTCHU[184-k] = mTCHD[2*k+1];
	}

	// 3.1.2.1 -- tail bits in u[]
	// TODO -- This should only be needed once, in the constructor.
	for (unsigned k=185; k<=188; k++) mTCHU[k]=0;

	// 3.1.2.2 -- encode u[] to c[] for class 1
	//mTCHU.encode(mVCoder,mClass1_c);
	mVCoder.encode(mTCHU,mClass1_c);

	// 3.1.2.2 -- copy class 2 d[] to c[]
	mClass2_d.copyToSegment(mC,378);

	// So the encoded speech frame is now in c[]
	// and ready for the interleaver.
}

void TCHFRL1Encoder::encodeTCH_AFS(const AudioFrame* aFrame)
{
	// We dont support SID frames.
	// GSM 05.02 3.9
	OBJLOG(DEBUG) <<"TCHFACCHL1Encoder TCH_AFS";

	// Reorder bits by importance while copying speech frame to d[]
	// See GSM 05.03 3.9.4.2 and Tables 7-14.
	//mAmrVFrame.payload().map(mAMRBitOrder, mKd, mTCHD);

	AudioFrameRtp rtpFrame(mAMRMode,aFrame);
	rtpFrame.getPayload(&mTCHRaw);
	mTCHRaw.map(mAMRBitOrder, mTCHD.size(), mTCHD);

	// 3.9.4.3 -- class 1a bits in d[] to u[]
	for (unsigned k=0; k < mClass1ALth; k++) {
		mTCHU[k] = mTCHD[k];
	}

	// 3.9.4.3 -- parity bits from d[] to u[]
	BitVector2 pFrom = mTCHD.segment(0, mClass1ALth);
	BitVector2 pTo = mTCHU.segment(mClass1ALth, 6);
   	mTCHParity.writeParityWord(pFrom, pTo);

	// 3.9.4.3 -- class 1b bits in d[] to u[]
	for (unsigned k=0; k < mClass1BLth; k++) {
		mTCHU[k+mClass1ALth+6] = mTCHD[k+mClass1ALth];
	}

	// 3.9.4.4 -- encode u[] to uc[]
	mViterbi->encode(mTCHU,mTCHUC);

	// 3.9.4.4 -- copy uc[] to c[] with puncturing
	BitVector2 cMinus8 = mC.segment(0, mC.size()-8);  // 8 id bits
	mTCHUC.copyPunctured(cMinus8, mPuncture, mPunctureLth);

	// So the encoded speech frame is now in c[]
	// and ready for the interleaver.
	// Puncturing brought the frame size to 448 bits, regardless of mode.
	// TCH_AFS interleaver (3.9.4.5) is same as TCH/FS (3.1.3).
	// TCH_AFS mapper (3.9.4.6) is same as TCH/FS (3.1.4).
}

void TCHFRL1Encoder::encodeTCH(const AudioFrame* aFrame)
{
	// (pat) Slight weirdness to avoid modifying existing GSM_FR code.
	if (mAMRMode == TCH_FS) { encodeTCH_GSM(aFrame); } else { encodeTCH_AFS(aFrame); }
}


void TCHFACCHL1Encoder::sendFrame( const L2Frame& frame )
{
	OBJLOG(DEBUG) << "TCHFACCHL1Encoder " << frame;
	// Simulate high FER for testing.
	if (random()%100 < gConfig.getNum("Test.GSM.SimulatedFER.Downlink")) {
		OBJLOG(NOTICE) << "simulating dropped downlink frame at " << mNextWriteTime;
		return;
	}
	mL2Q.write(new L2Frame(frame));
}



void TCHFACCHL1Encoder::dispatch()
{

	// No downstream?  That's a problem.
	assert(mDownstream);

	// Get right with the system clock.
	resync();

	// If the channel is not active, wait for a multiframe and return.
	// Most channels do not need this, becuase they are entirely data-driven
	// from above.  TCH/FACCH, however, must feed the interleaver on time.
	if (!encActive()) {
		{ ScopedLock lock(mWriteTimeLock,__FILE__,__LINE__);	// (pat) Protects getNextWriteTime.
		  mNextWriteTime += 26;
		}
		gBTS.clock().wait(mNextWriteTime);
		return;
	}

	// Let previous data get transmitted.
	resync();
	waitToSend();
	
	// flag to control stealing bits
	bool currentFACCH = false; 
	
	// Speech latency control.
	// Since Asterisk is local, latency should be small.
	OBJLOG(DEBUG) <<"TCHFACCHL1Encoder speechQ.size=" << mSpeechQ.size();
	int maxQ = gConfig.getNum("GSM.MaxSpeechLatency");
	while ((int)mSpeechQ.size() > maxQ) delete mSpeechQ.read();

	// Send, by priority: (1) FACCH, (2) TCH, (3) filler.
	if (L2Frame *fFrame = mL2Q.readNoBlock()) {
		OBJLOG(DEBUG) <<"TCHFACCHL1Encoder FACCH " << *fFrame;
		currentFACCH = true;
		// Send to GSMTAP
		if (gConfig.getBool("Control.GSMTAP.GSM")) {
			gWriteGSMTAP(ARFCN(),TN(),mNextWriteTime.FN(),typeAndOffset(),mMapping.repeatLength()>51,false,*fFrame);
		}
		// Copy the L2 frame into u[] for processing.
		// GSM 05.03 4.1.1.
		fFrame->LSB8MSB();
		fFrame->copyTo(mU);
		// Encode u[] to c[], GSM 05.03 4.1.2 and 4.1.3.
		encode41();
		OBJLOG(DEBUG) <<"TCHFACCHL1Encoder FACCH c[]=" << mC;
		delete fFrame;
		// Flush the vocoder FIFO to limit latency.
		while (mSpeechQ.size()>0) delete mSpeechQ.read();
	} else if (AudioFrame *tFrame = mSpeechQ.readNoBlock()) {
		OBJLOG(DEBUG) <<"TCHFACCHL1Encoder TCH " << *tFrame;
		// Encode the speech frame into c[] as per GSM 05.03 3.1.2.
		encodeTCH(tFrame);
		delete tFrame;
		OBJLOG(DEBUG) <<"TCHFACCHL1Encoder TCH c[]=" << mC;
	} else {
		// We have no ready data but must send SOMETHING.
		if (!mPreviousFACCH) {
			// This filler pattern was captured from a Nokia 3310, BTW.
			static const BitVector2 fillerC("110100001000111100000000111001111101011100111101001111000000000000110111101111111110100110101010101010101010101010101010101010101010010000110000000000000000000000000000000000000000001101001111000000000000000000000000000000000000000000000000111010011010101010101010101010101010101010101010101001000011000000000000000000110100111100000000111001111101101000001100001101001111000000000000000000011001100000000000000000000000000000000000000000000000000000000001");
			fillerC.copyTo(mC);
		} else {
			// FIXME -- This could be a lot more efficient.
			currentFACCH = true;
			L2Frame frame(L2IdleFrame());
			frame.LSB8MSB();
			frame.copyTo(mU);
			encode41();
		}
		OBJLOG(DEBUG) <<"TCHFACCHL1Encoder filler FACCH=" << currentFACCH << " c[]=" << mC;
	}

	// Interleave c[] to i[].
	interleave31(mOffset);

	// randomly toggle bits in control channel bursts
	// the toggle happens below, merged in with the ciphering
	int p = currentFACCH ? gConfig.getFloat("GSM.Cipher.CCHBER") * (float)0xFFFFFF : 0;

	// "mapping on a burst"
	// Map c[] into outgoing normal bursts, marking stealing flags as needed.
	// GMS 05.03 3.1.4.
	for (int B=0; B<4; B++) {
		// set TDMA position
		mBurst.time(mNextWriteTime);
		// encrypt x
		if (mEncrypted == ENCRYPT_YES) {
			unsigned char block1[15];
			unsigned char block2[15];
			unsigned char *kc = parent()->decoder()->kc();
			// 03.20 C.1.2
			// 05.02 3.3.2.2.1
			int fn = mNextWriteTime.FN();
			int t1 = fn / (26*51);
			int t2 = fn % 26;
			int t3 = fn % 51;
			int count = (t1<<11) | (t3<<5) | t2;
			if (mEncryptionAlgorithm == 1) {
				A51_GSM(kc, 64, count, block1, block2);
			} else if (mEncryptionAlgorithm == 3) {
				A53_GSM(kc, 64, count, block1, block2);
			} else {
				devassert(0);
			}
			for (int i = 0; i < 114; i++) {
				int b = p ? (random() & 0xFFFFFF) < p : 0;
				b = b ^ (block1[i/8] >> (7-(i%8)));
				mE[B+mOffset].settfb(i, mI[B+mOffset].bit(i) ^ (b&1));
			}
		} else {
			if (p) {
				for (int i = 0; i < 114; i++) {
					int b = (random() & 0xFFFFFF) < p;
					mE[B+mOffset].settfb(i, mI[B+mOffset].bit(i) ^ b);
				}
			} else {
				// no noise and no encryption - use mI below
			}
		}
		// copy in the bits
		if (p || mEncrypted == ENCRYPT_YES) {
			mE[B+mOffset].segment(0,57).copyToSegment(mBurst,3);
			mE[B+mOffset].segment(57,57).copyToSegment(mBurst,88);
		} else {
			// no noise and no encryption - use mI
			mI[B+mOffset].segment(0,57).copyToSegment(mBurst,3);
			mI[B+mOffset].segment(57,57).copyToSegment(mBurst,88);
		}
		// stealing bits
		mBurst.Hu(currentFACCH);
		mBurst.Hl(mPreviousFACCH);
		// send
		OBJLOG(DEBUG) <<"TCHFACCHEncoder sending burst=" << mBurst;
		mDownstream->writeHighSideTx(mBurst,"FACCH");	
		rollForward();
	}	

	// Update the offset for the next transmission.
	if (mOffset==0) mOffset=4;
	else mOffset=0;

	// Save the stealing flag.
	mPreviousFACCH = currentFACCH;
}



void TCHFACCHL1Encoder::interleave31(int blockOffset)
{
	// GSM 05.03, 3.1.3
	for (int k=0; k<456; k++) {
		int B = ( k + blockOffset ) % 8;
		int j = 2*((49*k) % 57) + ((k%8)/4);
		mI[B][j] = mC[k];
	}
}



void SACCHL1FEC::setPhy(const SACCHL1FEC& other)
{
	mSACCHDecoder->setPhy(*other.mSACCHDecoder);
	mSACCHEncoder->setPhy(*other.mSACCHEncoder);
}

void SACCHL1FEC::l1InitPhy(float RSSI, float timingError, double wTimestamp)
{
	mSACCHDecoder->initPhy(RSSI,timingError,wTimestamp);
	mSACCHEncoder->initPhy(RSSI,timingError);
}

void MSPhysReportInfo::sacchInit1()
{
	mActualMSTiming = 0.0F;
	// (pat) 6-2013: Bug fix: RSSI must be inited each time SACCH is opened, and to RSSITarget, not to 0.
	mRSSI = gConfig.getNum("GSM.Radio.RSSITarget");
	// The RACH was sent at full power, but full power probably depends on the MS power class.  We just use a constant.
	mActualMSPower = cInitialPower;
	mReportCount = 0;
	mTimingError = 0;
	mTimestamp = 0;
}

//void MSPhysReportInfo::sacchInit2(float wRSSI, float wTimingError, double wTimestamp)
//{
//	// Used to initialize L1 phy parameters.
//	// This is similar to the code for the closed loop tracking,
//	// except that there's no damping.
//	sacchInit1();	// first make sure everything is inited.
//	// Do NOT set mReportCount - we use that to tell when the first measurement report arrives.
//	// FIXME: We may want to set the initial power based on the RACH power instead of just using a constant cInitialPower.
//	mTimestamp=wTimestamp;
//	mTimingError = wTimingError;
//	mRSSI = wRSSI;
//	OBJLOG(BLATHER) << "SACCHL1Encoder init" <<LOGVARM(wRSSI) <<LOGVARM(wTimingError) <<LOGVARM(wTimestamp);
//}


void SACCHL1Decoder::decInit()
{
	OBJLOG(DEBUG) << "SACCHL1Decoder";
	// Set initial defaults for power and timing advance.
	// We know the handset sent the RACH burst at max power and 0 timing advance.
	// (pat) But what is the max power?  Does it depend on the MS class?
	// Measured values should be set after opening with setPhy.
	sacchInit1();
	XCCHL1Decoder::decInit();		// (pat) maps to L1Decoder::decInit()
}

//void SACCHL1Decoder::open2(float wRSSI, float wTimingError, double wTimestamp)
//{
//	OBJLOG(DEBUG) << "SACCHL1Decoder";
//	// Set initial defaults for power and timing advance.
//	// We know the handset sent the RACH burst at max power and 0 timing advance.
//	// (pat) But what is the max power?  Does it depend on the MS class?
//	// Measured values should be set after opening with setPhy.
//	sacchInit2(wRSSI,wTimingError,wTimestamp);
//	XCCHL1Decoder::open();		// (pat) This appears to map to L1Decoder::open()
//}



void MSPhysReportInfo::initPhy(float wRSSI, float wTimingError, double wTimestamp)
{
	// Used to initialize L1 phy parameters.
	mRSSI=wRSSI;
	mTimingError=wTimingError;
	mTimestamp=wTimestamp;
	OBJLOG(INFO) << "SACCHL1Decoder RSSI=" << wRSSI << " timingError=" << wTimingError << " timestamp=" << wTimestamp;
}

void MSPhysReportInfo::setPhy(const SACCHL1Decoder& other)
{
	// Used to initialize a new SACCH L1 phy parameters
	// from those of a preexisting established channel.
	mActualMSPower = other.mActualMSPower;
	mActualMSTiming = other.mActualMSTiming;
	mRSSI=other.mRSSI;
	mReportCount = other.mReportCount;
	mTimingError=other.mTimingError;
	mTimestamp=other.mTimestamp;
	OBJLOG(INFO) << "SACCHL1Decoder actuals RSSI=" << mRSSI << " timingError=" << mTimingError \
		<< " timestamp=" << mTimestamp \
		<< " MSPower=" << mActualMSPower << " MSTiming=" << mActualMSTiming;
}


static float boundMSPower(float orderedMSPower)
{
	float maxPower = gConfig.getNum("GSM.MS.Power.Max");
	float minPower = gConfig.getNum("GSM.MS.Power.Min");
	if (orderedMSPower>maxPower) orderedMSPower=maxPower;
	else if (orderedMSPower<minPower) orderedMSPower=minPower;
	return orderedMSPower;
}

void SACCHL1Encoder::setMSPower(float orderedPower)
{
	mOrderedMSPower = boundMSPower(orderedPower);
}

void SACCHL1Encoder::setMSTiming(float orderedTiming)
{
	mOrderedMSTiming = orderedTiming;
	float maxTiming = gConfig.getNum("GSM.MS.TA.Max");
	if (mOrderedMSTiming<0.0F) mOrderedMSTiming=0.0F;
	else if (mOrderedMSTiming>maxTiming) mOrderedMSTiming=maxTiming;
}


void SACCHL1Encoder::initPhy(float wRSSI, float wTimingError)
{
	// Used to initialize L1 phy parameters.
	// This is similar to the code for the closed loop tracking, except that there's no damping.
	//SACCHL1Decoder &sib = *SACCHSibling();
	// RSSI
	// (pat 4-2014) This is used only on channel initialization.  The RSSI comes from the RACH burst which is
	// always delivered at full power.  So we want to ignore the actual MS power, which is not known yet.
	// Arbitrarily goose the initial power up a little (10) by adjusting RSSITarget, just to make sure we get an ok initialization,
	// in case the RSSI measured power was inaccurate, or there is noise in the channel.
	//float RSSI = sib.getRSSI();
	float RSSITarget = gConfig.getNum("GSM.Radio.RSSITarget") + 10;
	float deltaP = wRSSI - RSSITarget;
	//float actualPower = sib.actualMSPower();	// This is just set to cInitialPower.
	//setMSPower(actualPower - deltaP);
	setMSPower(cInitialPower - deltaP);
	//OBJLOG(INFO) <<"SACCHL1Encoder RSSI=" << wRSSI << " target=" << RSSITarget
	//	<< " deltaP=" << deltaP << " actual=" << actualPower << " order=" << mOrderedMSPower;
	// Timing Advance
	// (pat) This is called at channel init so we have not received any measurement reports from the MS yet,
	// so the actualMSTiming must be the initialized value, that is, 0, unless some stray measurement report
	// comes in on the channel, in which case we should ignore it.
	// float timingError = sib.timingError();
	// float actualTiming = sib.actualMSTiming();
	// mOrderedMSTiming = actualTiming + timingError;
	setMSTiming(wTimingError);
	//OBJLOG(INFO) << "SACCHL1Encoder init timingError=" << sib.timingError()  << " actual=" << sib.actualMSTiming() << " ordered=" << mOrderedMSTiming;
	OBJLOG(INFO) << "SACCHL1Encoder init" <<LOGVARM(wTimingError) <<LOGVARM(wRSSI) <<LOGVAR(deltaP) <<LOGVARM(mOrderedMSPower);
}

const char* SACCHL1Encoder::descriptiveString() const
{
	// It is a const wannabe.
	if (mSacchDescriptiveString.length() == 0) {
		Unconst(this)->mSacchDescriptiveString = string(L1Encoder::descriptiveString()) + "-SACCH";
	}
	return mSacchDescriptiveString.c_str();
}


void SACCHL1Encoder::setPhy(const SACCHL1Encoder& other)
{
	// Used to initialize a new SACCH L1 phy parameters
	// from those of a preexisting established channel.
	mOrderedMSPower = other.mOrderedMSPower;
	mOrderedMSTiming = other.mOrderedMSTiming;
	OBJLOG(BLATHER) << "SACCHL1Encoder orders MSPower=" << mOrderedMSPower << " MSTiming=" << mOrderedMSTiming;
}



SACCHL1Encoder::SACCHL1Encoder(unsigned wCN, unsigned wTN, const TDMAMapping& wMapping, SACCHL1FEC *wParent)
	:XCCHL1Encoder(wCN,wTN,wMapping,(L1FEC*)wParent),
	mSACCHParent(wParent),
	mOrderedMSPower(cInitialPower),mOrderedMSTiming(0)
{ }


void SACCHL1Encoder::encInit()
{
	OBJLOG(BLATHER) <<"SACCHL1Encoder";
	setMSPower(cInitialPower);
	mOrderedMSTiming = 0;
	XCCHL1Encoder::encInit();		// (pat 4-2014) goes to L1Encoder::encInit
}



SACCHL1Encoder* SACCHL1Decoder::SACCHSibling() 
{
	return mSACCHParent->encoder();
}

SACCHL1Decoder* SACCHL1Encoder::SACCHSibling() 
{
	return mSACCHParent->decoder();
}



void SACCHL1Encoder::sendFrame(const L2Frame& frame)
{
	OBJLOG(BLATHER) << "SACCHL1Encoder " << frame;

	// Physical header, GSM 04.04 6, 7.1
	// Power and timing control, GSM 05.08 4, GSM 05.10 5, 6.

	SACCHL1Decoder &sib = *SACCHSibling();

	if (sib.isValid()) {
		// Power.  GSM 05.08 4.
		// Power expressed in dBm, RSSI in dB wrt max.
		float RSSI = sib.getRSSI();
		float RSSITarget = gConfig.getNum("GSM.Radio.RSSITarget");
		// (pat) RSSI and RSSITarget are both negative, so deltaP is positive if power is too high.
		float deltaP = RSSI - RSSITarget;
		// SNRTarget == 0 disables:
		if (float SNRTarget = gConfig.getNum("GSM.Radio.SNRTarget")) {
			float SNR = sib.getAveSNR();
			if (deltaP > 0 && SNR < SNRTarget) {	// If RSSITarget is met but SNR looks bad...
				// How do we decide what the target power should be from SNR?  And I dont want to call log().
				// We only change upward based on SNR - we rely on RSSITarget to keep the power down.
				deltaP = SNR - SNRTarget;	// So how about something simple like this?  eg: SNR == 10 is ok, SNR==6 would be bad, so add 4dB?  
			}
		}
		float actualPower = sib.actualMSPower();
		int configPowerDamping = gConfig.getNum("GSM.MS.Power.Damping");
		// Use the power damping algorithm.
		float targetMSPower = actualPower - deltaP;
		float powerDamping = configPowerDamping*0.01F;
		if (configPowerDamping < 90 && deltaP < 4) {
			// (pat 2-2014) Adjust the power in the upward direction faster than in the downward direction
			// if we are in danger of losing the signal.
			// This is intended to lessen signal degradation from, eg, just turning your head.
			// But this may not be worth the effort.  If the signal has dropped much lower than this
			// we will already have lost communication with the MS so we will never get here,
			// instead RSSIBumpDown will be used, although that could indirectly induce this greater jump here.
			powerDamping /= 2;	// This should probably be log response.
		}
		// (pat) Now, how fast does RSSI as seen in OpenBTS respond to changes ordered in the MS by mOrderedPower?
		// Do we need the damping factor to take that into account, or should we instead wait a bit after ordering
		// a power change before ordering another?  I guess we rely on the powerDamping factor to handle it.
		setMSPower(powerDamping*mOrderedMSPower + (1.0F-powerDamping)*targetMSPower);
		OBJLOG(DEBUG) <<"SACCHL1Encoder RSSI=" << RSSI << " target=" << RSSITarget
			<< " deltaP=" << deltaP << " actual=" << actualPower << " order=" << mOrderedMSPower;
		// Timing.  GSM 05.10 5, 6.
		// Time expressed in symbol periods.
		float timingError = sib.timingError();
		float actualTiming = sib.actualMSTiming();
		float targetMSTiming = actualTiming + timingError;
		float TADamping = gConfig.getNum("GSM.MS.TA.Damping")*0.01F;
		setMSTiming(TADamping*mOrderedMSTiming + (1.0F-TADamping)*targetMSTiming);
		OBJLOG(DEBUG) << "SACCHL1Encoder timingError=" << timingError
			<< " actualTA=" << actualTiming << " orderedTA=" << mOrderedMSTiming
			<< " targetTA=" << targetMSTiming;

	}
	// Write physical header into mU and then call base class.

	// SACCH physical header, GSM 04.04 6.1, 7.1.
	mU.fillField(0,encodePower(mOrderedMSPower),8);
	mU.fillField(8,(int)(mOrderedMSTiming+0.5F),8);	// timing (GSM 04.04 6.1)
	OBJLOG(INFO) <<"SACCHL1Encoder orders pow=" << mOrderedMSPower << " TA=" << mOrderedMSTiming << " with header " << mU.head(16);

	// Encode the rest of the frame.
	XCCHL1Encoder::sendFrame(frame);
}


void CBCHL1Encoder::sendFrame(const L2Frame& frame)
{
	OBJLOG(DEBUG);
	// Sync to (FN/51)%8==0 at the start of a new block.
	if (frame.peekField(4,4)==0) {
		mNextWriteTime.rollForward(mMapping.frameMapping(0),51*8);
	}
	// Transmit.
	XCCHL1Encoder::sendFrame(frame);
}

#ifdef TESTTCHL1FEC


BitVector2 randomBitVector(int n)
{
	BitVector2 t(n);
	for (int i = 0; i < n; i++) t[i] = random()%2;
	return t;
}

void TestTCHL1FEC()
{
	const TDMAMapping hack((TypeAndOffset)0,0,0,0,0,0,0,0);
	TCHFACCHL1Encoder encoder(0, 0, hack, 0);
	TCHFACCHL1Decoder decoder(0, 0, hack, 0);
	for (unsigned modei = 0; modei <= TCH_FS; modei++) {
		int modeii = modei == 0 ? (int)TCH_FS : modei-1;		// test full rate GSM first.
		AMRMode mode = (AMRMode)modeii;
		unsigned inSize = gAMRKd[mode];
		bool ok = true;
		cout <<LOGVAR(mode) <<LOGVAR(inSize) <<" ";
		encoder.setAmrMode(mode);
		decoder.setAmrMode(mode);
		assert(encoder.getTCHPayloadSize() == inSize);
		for (unsigned trial = 0; trial < 10; trial++) {
			bool ok1 = true;
			BitVector2 r(inSize);
			if (trial == 0) { r.zero(); }
			else { r = randomBitVector(inSize); }
			BitVector2 pay1, pay2;
			AudioFrame *aFrame = new AudioFrameRtp(mode);
			aFrame->append(r);
			encoder.encodeTCH(aFrame);		// Leaves the result in mC
			LOG(BLATHER) <<LOGVAR(encoder.mC);
			SoftVector softC = encoder.mC;	// Convert mC to a SoftVector.
			ok1 = decoder.decodeTCH(false,&softC);	// Decoder leaves result in the mSpeechQ.
			if (!ok1) {
				cout << LOGVAR(trial) <<" decode fail";
				ok = false;
			}
			AudioFrame * aframe = decoder.recvTCH();		// Pulls it out of mSpeechQ.
			LOG(BLATHER)<<LOGVAR(*aframe);

			pay1 = r;
			// Unmap the RTP frame:
			AudioFrameRtp gsmFrame(mode,aframe);
			pay2 = BitVector2(inSize);
			gsmFrame.getPayload(&pay2);

			if (pay1.size() != pay2.size()) {
				cout <<LOGVAR(trial) <<" diff sizes" << endl;
				ok = ok1 = false;
			}

			if (ok1) for (unsigned i = 0; i < pay1.size() && ok; i++) {
				if (pay1.bit(i) == pay2.bit(i)) continue;
				cout <<LOGVAR(trial) <<" values differ at " << i << endl;
				ok = ok1 = false;
				break;
			}
			//cout <<LOGVAR(pay1) << endl;
			//cout <<LOGVAR(pay2) << endl;
			if (!ok1) { ok = false; }	// redundant, but make sure
			if (ok1) { cout <<LOGVAR(trial) << " OK"; }
		}
		cout << (ok ? " OK" : " FAIL") << endl;
	}
	exit(0);
}

#endif // TESTTCHL1FEC

}; // namespace GSM


// vim: ts=4 sw=4
