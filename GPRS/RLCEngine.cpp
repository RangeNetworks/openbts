/*
* Copyright 2011 Range Networks, Inc.
* All Rights Reserved.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/


// File contains RLCUpEngine and RLCDownEngine, which are classes that extend
// TBF (Temporary Block Flow) to do the data block uplink/downlink movement functions for a TBF.

#include "BSSG.h"
#include "RLCEngine.h"
#include "TBF.h"
#include "MAC.h"
#include "FEC.h"
#include "Sgsn.h"
#include "RLCMessages.h"

namespace GPRS {

// If WaitForStall is true, a stalled TBF will send only one block at a time
// until it gets a response from the MS.
// If false, stalled downlink TBFs transfer the blocks continually
// until the time out.
//static int WaitForStall = false;

/** RLC block size in bits for given coding standard, GSM 04.60 Table 10.2.1, plus MAC header. */
// Index is a ChannelCodingType, 0-3 for CS-1 to CS-4.
const unsigned RLCBlockSizeBytesMax = 53;
unsigned RLCBlockSizeInBits[4] =
{
	// (pat) MAC header, plus RLC data block in octets, plus spare bits.
	// Table 10.2.1 does not include the 8-bit MAC header, so add 1.
	(1+22) * 8 + 0,		// CS-1
	(1+32) * 8 + 7,		// CS-2
	(1+38) * 8 + 3,		// CS-3
	(1+52) * 8 + 7		// CS-4

	// (pat) What was here before, but 319 does not appear correct:
	// 184, 	// CS-1  22 octets plus MAC header
	// 271,	// CS-2  32 octets plus MAC header
	// 319,	// CS-3  38 octets plus MAC header
	// 431	// CS-4  52 octets plus MAC header
};

unsigned RLCPayloadSizeInBytes[4] =
{
	// Table 10.2.1 includes the 2 octets for the RLC header, so subtract those out
	// for the payload size.
	(22-2), 		// CS-1
	(32-2), 		// CS-2
	(38-2), 		// CS-3
	(52-2) 		// CS-4
};



/*
	Arithmetic for the sequence numbers and arrays.
	The index for mSt.VB, mSt.TxQ, mSt.VN and mSt.RxQ is straight BSN.
	The downengine base index into mSt.VB is mSt.VA.
	The upengine base index into mSt.VN us mSt.VQ.
	In any of these arrays, the current window is base index through
	(base index + mWS) % mSNS.
*/
unsigned RLCEngineBase::addSN(int sn1, int sn2) const	// Allow negative numbers.
{
	return ((unsigned)((int)sn1 + (int)sn2)) % (unsigned) mSNS;
}

void RLCEngineBase::incSN(unsigned &psn)
{
	psn = addSN(psn,1);
}

// Previously this returned -mWS <= result <= +mWS
// Which results in the following, which should return identical numbers, returning:
// deltaSN(64, 0) = 64
// deltaSN(63, 127) = -64
// The above is the all important stall condition.
// I tried to fix this returning a number in the range: -mWS < result <= +mWS,
// but then it failed somewhere else.
// Instead of being so careful around the edge condition, I am switching to deltaSNS
// Note that this is the opposite edge condition of deltaBSN.
int RLCEngineBase::deltaSN(unsigned sn1, unsigned sn2) const
{
	int delta = sn1 - sn2;
	assert(!(delta >= (int) mSNS));
	assert(!(delta <= - (int) mSNS));
	if (delta <= -(int) mWS) delta += mSNS;		// modulo the sequence space
	if (delta > (int) mWS) delta -= mSNS;
	return delta;
}

// New delta functions
// Assume that sn1 >= sn2 in modulo arithmetic and do the delta on that basis.
// This function is safer than deltaSN at the edge condition, which is the stall condition.
// For safety we will allow an over-run of one.
// -1 <= result < +mSNS
int RLCEngineBase::deltaSNS(unsigned sn1, unsigned sn2) const
{
	int delta = sn1 - sn2;
	assert(!(delta >= (int) mSNS));
	assert(!(delta <= - (int) mSNS));
	if (delta < -1) delta += mSNS;		// modulo the sequence space
	if (delta >= (int)mSNS) delta -= mSNS;
	return delta;
}

// Are the RLC BSNs equal?  Using a function is just documentation that
// we are doing a comparison of BSNs.
bool RLCEngineBase::deltaEQ(unsigned sn1, unsigned sn2) const
{
	return sn1 == sn2;
}

static bool queueFrontExistsAndIsNotTlliChangeCommand(MSInfo *ms)
{
	SGSN::GprsSgsnDownlinkPdu *dlmsg = ms->msDownlinkQueue.front();
	if (!dlmsg) { return false; }
	if (dlmsg->mTlli != ms->msTlli) {
		// We will terminate the downlink TBF and leave this new TBF with
		// the TLLI change command sitting in the queue to handle
		// at the next time we talk to this MS.
		LOGWATCHF("TLLI change command from %x to %x\n",(unsigned)ms->msTlli,(unsigned)dlmsg->mTlli);
		return false;
	}
	return true;
}

// We leave persistent mode when mDownFinished is set.
bool RLCDownEngine::dlPersistentMode() { return !mDownFinished && (gL2MAC.macDownlinkPersist > 0); }
//bool RLCDownEngine::dataAvail() { return mDownPDU.size() || mtMS->msDownlinkQueue.size(); }
bool RLCDownEngine::dataAvail() {
	return mDownPDU.size() || queueFrontExistsAndIsNotTlliChangeCommand(mtMS);
}


// Advance mSt.VA forward over blocks that have been previously acked;
// This is the only function permitted to modify VA.
// End condition is VA == TxQNum meaning VA is one beyond last in queue.
void RLCDownEngine::advanceVA()
{
	//for (; mSt.VA<mSt.TxQNum && mSt.VB[mSt.VA]; mSt.VA++)
	//for (; deltaSNS(mSt.VA,mSt.TxQNum)<0 && mSt.VB[mSt.VA]; incSN(mSt.VA))
	for (; !deltaEQ(mSt.VA,mSt.TxQNum) && mSt.VB[mSt.VA]; incSN(mSt.VA)) {
		// We must clean up behind ourselves now that the count wraps around.
		// Where to do it?  We must not delete the final block because it is resent,
		// but with persistent mode the final block changes every time there
		// is an incoming TBF.
		//if (mSt.VB[mSt.VA] && mSt.TxQ[mSt.VA] && addSN(mSt.VA,1) != mSt.TxQNum) {
			//delete mSt.TxQ[mSt.VA];
			//mSt.TxQ[mSt.VA] = NULL;
		//}
	}
}

// Do we need to resend this block now?
bool RLCDownEngine::resendNeeded(int bsn)
{
	if (mSt.VB[bsn]) { return false; }	// block positively acknowledged.
	if (mDownStalled) { return true; }	// in this case, resend every block ever sent.
	// We dont know if bsn is greater than mResendSsn so use deltaSN.
	if (deltaSN(bsn,mResendSsn) < 0) {
		return true;
	} else {
		// Even though this block has not been positively acknowledged, the MS has not
		// yet had a chance to do so, so dont resend it, ie, it is after the SSN
		// of the most recent ack.  We may want to modify this for the case
		// where the MS does not respond to an ack, and count those blocks as
		// needing resend as well?  Not sure.
		// As of 6-12, when the MS misses an ack it is because it cant hear anything
		// so all the blocks need to be resent.
		return false;
	}
}

// Advance mSt.VS forward to the next unacked block, if any.
// The final condition is we leave VS == TxQNum, which is one greater than the last block.
void RLCDownEngine::advanceVS()
{
	// This may set VS to TxQNum, which is the next block that has never been sent yet.
	if (!mtUnAckMode) {	// if it is ack mode
		// We used to advance or positively acknowledged blocks,
		// but now we advance over blocks that have not been negatively acknowledged,
		// unless we are stalled, in which case we resend
		// Changed 6-11-2012
		//for (; deltaSN(mSt.VS,mSt.TxQNum)<0 && mSt.VB[mSt.VS]; incSN(mSt.VS)) continue;
		for (; !deltaEQ(mSt.VS,mSt.TxQNum) && !resendNeeded(mSt.VS); incSN(mSt.VS)) continue;

		// Check for stall:
		// Previously, when we allowed the numbers to wrap,
		// we also set mDownStalled when the tbf was finished to prevent
		// incrementing VS any more, but we go ahead and increment VS one past
		// the end so it stops at TxQNum.
#if FAST_TBF
		// VS is always >= VA
		// Even though we are allowed mWS (64) outstanding blocks,
		// we are only going to send only 63 to stay away from the edge condition,
		// both for our own code, and possibly for bugs on the MS side as well.
		if (deltaSNS(mSt.VS,mSt.VA) >= (int)(mWS-1)) {
			mDownStalled = true;
			mSt.VS = mSt.VA;	// Start over from oldest unacked block.
		}
#else
		//if ((mSt.VS >= mSt.TxQNum || mSt.VS - mSt.VA >= mWS))
		if (mSt.VS >= mSt.TxQNum || deltaSN(mSt.VS,mSt.VA) >= (int)mWS) {
			mDownStalled = true;
			mSt.VS = mSt.VA;	// Start over from oldest unacked block.
		}
#endif
	}
}


// (pat) Process an AckNack block from the MS (cell phone)
// Here we just mark the blocks that got through ok.
// The serviceloop will resend the blocks via engineService()
void RLCDownEngine::engineRecvAckNack(const RLCMsgPacketDownlinkAckNack *msg)
{
	mtRecvAck(MsgTransTransmit);	// Make sure.
	// mtMS->msN3101 = 0;  Removed 6-7: 3101 is for uplink only.
	mDownStalled = false;	// Until proven otherwise.
	// Mark the acks.
	mNumDownBlocksSinceAckNack = 0;
	const RLCMsgPacketAckNackDescriptionIE& AND = msg->mAND;
	if (AND.mFinalAckIndication) {
		// All done.  We need to ack the entire area covered by the window,
		// but we will overkill and ack the entire queue to be safe.
		for (unsigned i=0; i<mSNS; i++) { mSt.VB[i] = true; }
		mAllAcked = true;	// should be redundant with check below.
	} else {
		// The logic here is really contorted; see comments at engineUpAckNack.
		// And I quote: "The bitmap is indexed relative to SSN as follows:
		//			"BSN = (SSN - bit_number) modulo 128, for bit_number = 1 to 64.
		// "The BSN values represented range from (SSN - 1) mod 128 to (SSN - 64) mod 128."
		// The bits are numbered backwards in the field from 1 to 64.
		// The MS selected the SSN in this message, and it is not necessarily related
		// to any of our state variables, except that it must be in range.
		// NOTE: SSN is VR in the receiver, which is 1 greater than highest block received.
		// This is difficult to test, but I have observed that the MS resends
		// the blocks we think it should, so I think this is working.
		bool receivedNewAcks = false;
		{	unsigned absn = AND.mSSN;
			for (int i=1; i<=AND.mbitmapsize; i++) {
				absn = addSN(absn,-1);
				if (AND.mBitMap[AND.mbitmapsize - i]) {
					if (! mSt.VB[absn]) { receivedNewAcks = true; }
					mSt.VB[absn] = true;
				} else {
					// The MS does not necessarily set bits which have
					// been acked previously, so lack of a bit means nothing.
				}
			}
		}

		// This code detects the condition that the downlinkAckNack did not advance VA at all.
		// There is no speced counter to detect this condition, and under normal circumstances
		// maybe it would not occur.  But if a TBF fails and we send a new TBF, the Blackberry
		// thinks the new TBF is a continuation of the the old and sends us an acknack instructing
		// us to resend blocks from the old TBF, which is no longer possible, and the result
		// is we keep doing the same downlinkAckNack handshake literally forever.
		// This is despite setting of the RLCMsgPacketDownlinkAssignment->mControlAck bit in the
		// downlink assignment, so it may be a bug in the Blackberry.
		// Update 6-12: The above bug is definitively fixed.  However, TBFs still get stuck here
		// due to non-responsive MS, but that case is also detected by N3105.
		bool stuck = (AND.mSSN == mPrevAckSsn);
		if (stuck && !receivedNewAcks) {
			LOGWATCHF("T%s STUCK at %d\n",getTBF()->tbfid(1),(int)AND.mSSN);
			if ((int)mTotalBlocksSent - (int)mPrevAckBlockCount > configGetNumQ("GPRS.TBF.Downlink.NStuck",250)) {
				mtCancel(MSStopCause::Stuck,TbfRetryAfterRelease);
				goto finished;
			}
		} else {
			mPrevAckBlockCount = mTotalBlocksSent;
		}
		mPrevAckSsn = AND.mSSN;
		mResendSsn = AND.mSSN;	// Default is to resend negatively acked blocks.
		if (stuck || mDownFinished || mDownStalled) {
			mResendSsn = mSt.TxQNum;	// In these cases, resend all blocks.
		} else {
			// The downlink acknack lags behind VS by an amount that depends on how
			// many downchannels are being used.  If it looks like the SSN
			// was out of sync farther than that, resend those blocks too.
			int slip = 6 * mtMS->msPCHDowns.size();
			if (deltaSNS(mSt.TxQNum,mResendSsn) > slip) {
				mResendSsn = addSN(mSt.TxQNum,-slip);
			}
		}

		// TEST!!! This was a total hack to test the bitmap interpretation.
		// If we missed any blocks, resend them all.
		// Worked in that RLCEngine resent all blocks, but did not help at all.
		//if (cntmissed && AND.mSSN) {
		//	for (int i=1; i <= AND.mbitmapsize; i++) {
		//		absn = AND.mSSN - i;
		//		mSt.VB[absn] = false;	// resend all the blocks referenced in the bitmap.
		//		if (absn == 0) break;
		//	}
		//}
	}

	advanceVA();

	//if (deltaSN(mSt.VA,mSt.TxQNum) >= 0)		// Did the MS ack all the blocks?
	if (deltaEQ(mSt.VA,mSt.TxQNum)) {		// Did the MS ack all the blocks?
		mAllAcked = true;
		GPRSLOG(1) << getTBF() <<msg->str() <<engineDumpStr();
		if (mDownFinished) {
			mtFinishSuccess();	// All done; delete the tbf.
		}
		//mtRecvAck();
		return;
	} else {
		// The last block sent must be the final block.
		// To implement that, we just unack the final block.
#if FAST_TBF
		// For persist mode, we dont need to unack the block
		// because if we get a new TBF we dont need to resend this block any more.
		// But I dont think we need to do this at all any more, because
		// when VS == TxQNum, we know to resend the final block.
#else
		// It doesnt matter if the MS re-acks this block or not, since it is
		// the final block, we always have to resend it at the end,
		// and the MS will send the FBI when it is acked.
		// Effectively, there is no point in an ack indicator for the final block.
		mSt.VB[addSN(mSt.TxQNum,-1)] = false;
#endif
	}

	// Reset mSt.VS to the oldest unacked block:
	mSt.VS = mSt.VA;
	advanceVS();	// Then skip over blocks that have not been negatively acknowledged.

	// We dont really have to check for stall here.
	// It is ok to wait until after the next block is sent.

	finished:
	//GPRSLOG(1) << getTBF() <<msg->str() <<LOGVAR(mSt.TxQNum) <<LOGVAR(mSt.VA) <<LOGVAR(mSt.VS) << GPRSLOG(mResendSsn);
	GPRSLOG(1) << getTBF() <<msg->str() <<engineDumpStr();
}

// To debug everything in RLCUpEngine: GPRS.Debug = 1+2+64+2048+4096=6211
void RLCUpEngine::addUpPDU(BitVector& seg)
{
	if (mUpPDU == NULL) {
#if INTERNAL_SGSN
		mUpPDU = new ByteVector(RLC_PDU_MAX_LEN);
		mUpPDU->setAppendP(0);
#else
		mUpPDU = new BSSG::BSSGMsgULUnitData(RLC_PDU_MAX_LEN,mtMS->msTlli);
		mUpPDU->mTBFId = mtDebugId;
#endif
	}
	// Do not use str() here, because it tries to print the pdu contents,
	// but the pdu is incomplete.
	mUpPDU->append(seg);
	GPRSLOG(4096) << "addUpPDU:after="<<mUpPDU->hexstr();
}


// Send the completed PDU on its way.
void RLCUpEngine::sendPDU()
{
	GPRSLOG(2048) <<"sendPDU"<<LOGVAR2("size",mUpPDU->size())<<LOGVAR2("pdu",mUpPDU->hexstr());
	mtMS->msBytesUp += mUpPDU->size();
#if INTERNAL_SGSN
	mtMS->sgsnWriteLowSide(*mUpPDU,getTBF()->mtTlli);
	delete mUpPDU;	// decrements refcnt; llc may have saved a ref to the bytevector.
#else
	mUpPDU->setLength();
	if (GPRSConfig::sgsnIsInternal()) {
		// LLC does not want the BSSG header.
		// The payload is an LLC message.
		ByteVector payload = mUpPDU->getPayload();
		//SGSN::Sgsn::sgsnWriteLowSide(payload,mtMS->msTlli);
		mtMS->sgsnWriteLowSide(payload,getTBF()->mtTlli);
		delete mUpPDU;	// decrements refcnt; llc may have saved a ref to the bytevector.
	} else {
		BSSG::BSSGWriteLowSide(mUpPDU);
	}
#endif
	mUpPDU = 0;
}

struct seghdr {
	unsigned LI;
	unsigned M;
	unsigned E;
};

static void dumpsegs(seghdr *segs, int n)
{
	for (int j = 0; j < n; j++) {
		struct seghdr& seg = segs[j];
		GLOG(INFO) <<"\t" << LOGVAR(seg.LI) << LOGVAR(seg.M) << LOGVAR(seg.E);
	}
}

// Process any blocks that have been received, advancing the window.
// The MS is allowed to send multiple PDUs in a TBF, so we have to handle it.
// Even in unack mode, we will assemble the blocks in mSt.RxQ to descramble multislot transmissions.
void RLCUpEngine::engineUpAdvanceWindow()
{
	mIncompletePDU = false;
	//if (BSN==mSt.VQ) {
	//	while (mSt.VN[mSt.VQ] && mSt.VQ!=mSt.VR) { mSt.VQ = addSN(mSt.VQ,1); }
	//}
	// TODO: Unacknowledged mode.
	while (mSt.RxQ[mSt.VQ /*% mSNS*/]) {
		RLCUplinkDataBlock *block = mSt.RxQ[mSt.VQ];
		mSt.RxQ[mSt.VQ] = NULL;	// redundant; they will also be deleted when suredly past
		//mSt.VN[mSt.VQ] = false;	// We cant do this here.
		mSt.VQ = addSN(mSt.VQ,1);

		RLCUplinkDataSegment payload(block->getPayload());
		GPRSLOG(64) << "RLCUpEngine payload=" << payload.hexstr() <<LOGVAR2("o",payload.isOwner());

		// Each block can have multiple LLC segments.  The max size is payloadSize.
		// The block size is specified by the channel coding command, but I saw the Blackberry
		// send blocks of a lower codec sometimes of its own initiative.
		// So dont bother to error check the block size.

		if (block->mE) {
			// Whole payload is appended to the current PDU.
			GPRSLOG(64) << "RLCUpEngine E=1,"<<LOGVAR(payload.size());
			addUpPDU(payload);
			if (block->mmac.isFinal()) { sendPDU(); }	// Annex B Example 6.
		} else {
			// The RLC block contains length indicator octets.  Crack them out.
			struct seghdr segs[16];

			int n;		// Number of length indicators; last one has E==1.
			bool end = 0;
			for (n = 0; !end; n++) {
				if (n == 16) {
					GLOG(ERR) << "GPRS: more than 16 segments per RLC block";
					dumpsegs(segs,15);
					break;	// This block is almost certianly trash...
				}
				segs[n].LI = payload.LIByteLI();
				segs[n].E = payload.LIByteE();
				end = segs[n].E;
				segs[n].M = payload.LIByteM();
				payload.set(payload.tail(8));
			}
			unsigned original_size = payload.size();

			// GSM 04.60 sec 10.4.14 and Appendix B.
			// Use the length indicators to slice up the payload into segments.
			for (int i = 0; i < n; i++) {
				unsigned lenbytes = segs[i].LI;
				unsigned sizebytes = payload.size()/8;
				GPRSLOG(64) << "RLCUpEngine seg:"<<LOGVAR(i)<<LOGVAR(n)<<LOGVAR(lenbytes)<<LOGVAR(sizebytes)<<LOGVAR(payload.size()) <<LOGVAR2("o",payload.isOwner());
				// Update: 44.060 B.6 says if the final block (indicated by CV==0) no need to
				// add any length indicators to denote termination of PDU????
				if (lenbytes == 0) {
					lenbytes = sizebytes;	// lenbytes==0 means use the rest of the payload.
					// Special case: If length == 0 in the final block,
					// it means the PDU was unfinished.
					if (block->mmac.isFinal()) {
						GPRSLOG(64) << "RLCUpEngine mIncompletePDU";
						mIncompletePDU = true;	// But we dont currently use this.
					}
				} else {
					// Sanity check here.  Log any bogus blocks.
					if (lenbytes > sizebytes) {
						GLOG(ERR)<<"Uplink PDU with with nonsensical segments:";
						//GLOG(INFO)<<"\tpayloadlen="<<original_size <<" E="<<block->mE;
						GLOG(ERR)<< "\t" << LOGVAR(original_size) << LOGVAR(block->mE);
						dumpsegs(segs,n);
						// what to do?  Just save what there really is.
						lenbytes = sizebytes;
					}
				}
				BitVector foo(payload.segment(0,8*lenbytes));
				addUpPDU(foo);
				if (segs[i].LI) { sendPDU(); }
				payload.set(payload.tail(8*lenbytes));
			}
			// Final M bit means add rest of the payload to the nextpdu.
			if (payload.size() && segs[n-1].M) {
				GPRSLOG(64) << "RLCUpEngine M=1:"<<LOGVAR(payload.size()) <<LOGVAR2("o",payload.isOwner());
				addUpPDU(payload);
			}
		}

		mtUpPersistTimer.setNow();

		if (mtUpState == RlcUpPersistFinal) { mtUpState = RlcUpTransmit; }
		if (block->mmac.isFinal()) {
			if (mUpPersistentMode && ! mtPerformReassign) {
				mtUpState = RlcUpQuiescent;
			} else {
				mtUpState = RlcUpFinished;
			}
			// 5-22-2012: Re-enabled this:
			if (mUpPDU && !mIncompletePDU) { sendPDU(); }
		} else {
			// This is a new block, so we are not quiescent any more.
			if (mtUpState == RlcUpQuiescent) { mtUpState = RlcUpTransmit; }
		}
		delete block;
	}
}

// Amazingly, we need to give the MS an RRBP reservation in case it wants to change
// the priority of the TBF.  All those zillions of USFs we sent it, and on which
// it responded with control blocks, were not enough.
// Update: Some MSs (iphone) send the packet resource request on the USF, ie, using PACCH.
bool RLCUpEngine::sendNonFinalAckNack(PDCHL1Downlink *down)
{
	MSInfo *ms = mtMS;
	// Before we issue another ack nack, wait until we have given this MS
	// an uplink block to respond via usf.  This is an efficiency issue
	// if the uplink is being used by other MS as well.  However, if the MAC does not
	// find any other pressing need for the current uplink block, it will be granted
	// to any one of the active uplink TBFs, so we may get a block that way too.
	// Note there can be multiple uplink channels, and a USF could be granted for
	// this MS on any of them.  Note that this would get complicated if there
	// are multiple uplink TBFs.
	if (ms->msAckNackUSFGrant == ms->msNumDataUSFGrants) { return false; }

	// These messages are sent without acknowledgement, except for the last one.
	// If we were more clever, we would insure that the USF in this message
	// is for the intended MS recipient, then if we received an uplink block
	// we would know the message was (probably) received.  (It can still fail
	// because the USF in the header is sent with better error correction.)
	// These messages are not specifically counted.  The TBF cancellation
	// occurs if the MS stops answering USFs.
	// It would be nice to detect stuck (non-advancing) uplinks, but the MS
	// will do that, cancel the TBF, stop answering USFs, and then we will cancel.
	RLCMsgPacketUplinkAckNack * msg = engineUpAckNack();
	GPRSLOG(1) <<getTBF() <<" "<<msg->str();
	//down->send1MsgFrame(getTBF(),msg,0,MsgTransNone,NULL);
	down->send1MsgFrame(getTBF(),msg,1,MsgTransTransmit,NULL);
	ms->msAckNackUSFGrant = ms->msNumDataUSFGrants;	// Remember this number.
	mNumUpBlocksSinceAckNack = 0;
	return true;
}

#if UPLINK_PERSIST
// For persistent uplink we need to know when all current TBFs have completed
// so the uplink is quiescent.
// bool RLCUpEngine::isQuiescent()
//{
//	return mSt.VQ == mSt.VR && NULL==mUpPDU;
//}
#endif

// See if this up engine wants to send something on the downlink.
// It would be an AckNack message.
// Return true if we sent something.
bool RLCUpEngine::engineService(PDCHL1Downlink *down)
{
	TBF *tbf = getTBF();
	if (! tbf->isPrimary(down)) { return false; }

	if (mtUpState == RlcUpFinished) {
		finalstate:
		// Send the final acknack
		// We wait for the MS to send a PacketControlAcknowledgment
		// to the final acknack msg we sent it, to make sure it got it.
		// Otherwise, it may keep trying to send us blocks forever.
		// An alternate strategy would be to resend the acknack whenever
		// we receive a data block from it, but it would be harder to tell
		// when to release the resources.
		if (mtGotAck(MsgTransDataFinal,true)) { // Woo hoo!
			mtFinishSuccess();
			return false;	// We did not need to use the downlink.
		}
		if (mtMsgPending()) { return false; }
		RLCMsgPacketUplinkAckNack * msg = engineUpAckNack();
		GPRSLOG(1) <<tbf <<" "<<msg->str();
		int result = down->send1MsgFrame(tbf,msg,2,MsgTransDataFinal,&mtN3103);
		if (result) {
			if (mtUnAckMode) {
				// We dont bother to find out if the acknack gets through.
				// In fact, the only reason we used an RRBP was
				// to allow the MS to initiate another uplink transfer.
				mtFinishSuccess();
			} else {
				mtMsgSetWait(MsgTransDataFinal);	// Wait for response before trying again.
			}
		}
		return result;
	}

	if (mUpStalled || mNumUpBlocksSinceAckNack >= mNumUpPerAckNack) {
		// But absolutely do not send two reservations at once:
		if (! mtMsgPending(MsgTransTransmit)) return sendNonFinalAckNack(down);
	}

#if UPLINK_PERSIST
	// Persistent uplink uses Extended Uplink TBF defined in 44.060 9.3.1b and 9.5
	if (mUpPersistentMode) {
		//LOGWATCHF("X%s UpState=%d keepalive=%d persist=%d\n",tbf->tbfid(1),mtUpState,mtUpKeepAliveTimer.elapsed(),mtUpPersistTimer.elapsed());
		// Note: The link is 'quiescent' if there are no new unique blocks,
		// but there could be lots of duplicate blocks, so we still need to
		// send acknacks.
		if (mtUpState == RlcUpQuiescent) {
			if (mtUpPersistTimer.elapsed() > (int)gL2MAC.macUplinkPersist || mtPerformReassign) {
				// Time to kill the TBF.
				// As per 44.060 9.5 we do that by sending a PacketUplinkAckNack with the
				// final ack bit set.  But since we are constantly transmiting USFs, there
				// could be a new uplink TBF starting right now, so first we have to
				// stop transmitting USFs, then wait to make sure the MS didnt start
				// another TBF, before we can kill it off.
				mtUpState = RlcUpPersistFinal;
				// There could be USFs sent in this period, so we need to wait out
				// this block period, and a USF in block N gets replied in block N+1,
				// so we have to wait for N+2, or 3 block periods total.
				mDataPersistFinalEndBSN = gBSNNext + 3;
				return false;
			} else if (mtUpKeepAliveTimer.elapsed() > (int)gL2MAC.macUplinkKeepAlive) {
				// This updates the keepalive timer if the message is sent:
				bool result = sendNonFinalAckNack(down);
				if (result) LOGWATCHF("K%s keepalive=%d persist=%d\n",getTBF()->tbfid(1),
						mtUpKeepAliveTimer.elapsed(),mtUpPersistTimer.elapsed());
				return result;
			}
		} else if (mtUpState == RlcUpPersistFinal) {
				// We hang out in this state until the expiry BSN is reached.
				// If a new TBF starts in the meantime it will throw us
				// back into DataTransmit state.
				if (gBSNNext <= mDataPersistFinalEndBSN) {return false;}
				// Now it is really time to finish.
				mtUpState = RlcUpFinished;
				mtSetState(TBFState::DataFinal);
				goto finalstate;
		}
		//GLOG(ERR) << "persistent mode timer expiration: invalid TBF state:"<<tbf;
	}
#endif
	return false;
}

float RLCUpEngine::engineDesiredUtilization()
{
	// Active uplink engine wants approximately this downlink bandwidth.
	return 1.0 / mNumUpPerAckNack;
}

// (pat) Receive a block from the MS (cell phone)
// Note: function receives allocated block, someone must destroy when finished.
// The someone is engineUpAdvanceWindow or the class destructor.

// For persistent mode listen up, because this is tricky.
// The MS sends us the final block, but then continues to send
// previous blocks until it gets the acknack.
// Therefore we must not set the mtUpState based on receiving a block in this function,
// we can only change mtUpState in engineUpAdvanceWindow, which knows if the block is new or not.
void RLCUpEngine::engineRecvDataBlock(RLCUplinkDataBlock* block, int tn)
{
	switch (mtGetState()) {
	case TBFState::DataWaiting1:
		mtMS->msT3168.reset();	// Way overkill, this should not be running.
		mtSetState(TBFState::DataTransmit);
		break;
	case TBFState::Dead:
		GLOG(ERR) <<getTBF()<<"received uplink data block after expiration";
		delete block;
		return;
	default:
		break;
	}
	//mtUpPersistTimer.setNow();
	mtMS->msN3101 = 0;
	// Mark the ack flags and save the block.
	unsigned BSN = block->mBSN;		// This is modulo mSNS
	LOGWATCHF("B%s tn=%d block=%d cc=%d %s %s\n",getTBF()->tbfid(1),tn,BSN,(int)block->mUpCC,
		block->mmac.isFinal()?"final":"",mSt.VN[BSN]?"dup":"");
	if (mSt.RxQ[BSN]) {
		// If BSN < VQ in modulo arithmetic, this is a duplicate block that
		// we have already scanned past, and we dont need to save it.
		// However, to be safe, we wont do the above test, instead we'll just save the
		// block and delete it when it is surely past below.
		// If we had more energy, we might check that the two blocks are the same.
		if (mSt.VN[BSN] == false) { GLOG(ERR) << getTBF() << " VN out of sync" <<LOGVAR(BSN) << LOGVAR(mSt.VN); }
		delete mSt.RxQ[BSN];
		mtMS->msCountBlocks.addMiss();
	} else {
		mUniqueBlocksReceived++;
		mtMS->msCountBlocks.addHit();
	}
	mSt.VN[BSN]=true;
	mSt.RxQ[BSN]=block;
	// We must use deltaSN, not deltaSNS, because we dont know which is higher.
	// Have to subtract 1 first to keep the edge condition from failing.
	int VRm1 = addSN(mSt.VR,-1);
	int deltaR = deltaSN(BSN,VRm1);
	if (deltaR>0) {
		unsigned past = addSN(mSt.VR, -mWS - 2);	// -2 to be safe
		mSt.VR=addSN(BSN,1);
		unsigned pastend = addSN(mSt.VR, -mWS - 2);

		// We clear out the VN behind us.
		// Since the acknack msg only stretches back the 64 acks before VR,
		// it is safe to clear before those.
		// 12-28-2012: Change to -2 from -1.
		//mSt.VN[(mSt.VR - mWS - 2) % mSNS] = false;

		for ( ; past != pastend; incSN(past)) {
			mSt.VN[past] = false;
			if (mSt.RxQ[past]) { delete mSt.RxQ[past]; mSt.RxQ[past] = 0; }
		}
	}

	mUpStalled = block->mmac.mSI;
	//mtSetState(mUpStalled ? TBFState::DataStalled : TBFState::DataTransmit);
	mNumUpBlocksSinceAckNack++;
	mTotalBlocksReceived++;

	// Add any finished blocks to the PDU, possibly send PDUs.
	engineUpAdvanceWindow();	// sets mtUpState to RlcUpFinished if TBF is complete.

	if (mtUpState == RlcUpFinished) {
		// We always send a final ack/nack for both acknowledged and unacknowledged mode.
		// old: Calls mtMsgReset() - very important. update: now each msgtransaction has its own type.
		mtSetState(TBFState::DataFinal);
	}
}



// Send Packet Uplink Ack/Nack, GSM 04.60 11.2.6
RLCMsgPacketUplinkAckNack * RLCUpEngine::engineUpAckNack()
{
	RLCMsgPacketAckNackDescriptionIE AND;
	// Spec says that if mFinalAckIndication is set, the rest is ignored.
	AND.mFinalAckIndication = (mtUpState == RlcUpFinished);
	// GSM04.60 9.1.8.
	// Also read 12.3: Ack/Nack description carefully.
	// The phrase: "Mapping of the bitmap is defined on sub-clause 11" means look at the
	// start of section 11, to learn that the bitmap is indexed backwards relative to SSN which
	// is defined as the most recent block received (aka V(R) in the receiver),
	// then modulo 128, but the bit encoding itself runs from MSB to LSB,
	// so it ends up going forwards, but the bits of interest
	// are at the high end of the bitmap.
	AND.mSSN = mSt.VR;	// Thats right: VR, not VQ.
	for (int i = 1; i <= AND.mbitmapsize; i++) {
		//AND.mBitMap[AND.mbitmapsize - i] = mSt.VN[(mSt.VR-i) % mSNS];
		AND.mBitMap[AND.mbitmapsize - i] = mSt.VN[addSN(mSt.VR,-i)];
	}
	RLCMsgPacketUplinkAckNack *msg = new RLCMsgPacketUplinkAckNack(getTBF(), AND);
#if UPLINK_PERSIST
	if (mUpPersistentMode) {
		mtUpKeepAliveTimer.setNow();
	}
	LOGWATCHF("A%s SSN=%d state=%d keepalive=%d persist=%d\n",getTBF()->tbfid(1),(int)AND.mSSN,
		mtUpState,mtUpKeepAliveTimer.elapsed(),mtUpPersistTimer.elapsed());
#endif
	return msg;
}


// Is the downlink engine stalled, waiting for acknack messages?
// Assumes the downlink is unifinished.
// Stalled applies only to acknowledged mode.
// We never send more than SNS blocks in a single TBF,
// so we dont have to worry about wraparound.
//bool RLCDownEngine::stalled()
//{
//	if (!mDownStalled && !mtUnAckMode &&
//		(mSt.VS >= mSt.TxQNum || mSt.VS - mSt.VA >= mWS)) {
//		mDownStalled = true;
//	}
//	return mDownStalled;
//}

// Deliver a PDU to the MS.
// Before FAST_TBF: This function primes the RLCEngine by chopping the PDU into RLCBlocks,
// placing them in the mSt.TxQ.  They will be physically sent by the serviceloop.
void RLCDownEngine::engineWriteHighSide(SGSN::GprsSgsnDownlinkPdu *dlmsg)
{
#if FAST_TBF
	// We dont do the pdus one at a time.  Just leave the PDU on the queue and the
	// engine will pull it off.
	mtMS->msDownlinkQueue.write_front(dlmsg);
#else
	// The pdus are sent one at a time.
	TBF *tbf = getTBF();
	tbf->mtDescription = dlmsg->mDescr;
	// The mDownPDU owns the malloced memory.  Many of the other downlink blocks are segments into it.
	// The mDownPDU is deleted automatically with the RLCDownEngine.
	mDownPDU = dlmsg->mDlData;
	LOGWATCHF("engineWriteHighSide %d tn=%d\n",mDownPDU.size(),tbf->mtMS->msPacch->TN());
	//assert(mDownPDU.getRefCnt() == 2);	// Not true during a retry.
	mDownlinkPdu = dlmsg;
	//assert(mDownPDU.getRefCnt() == 1);
	//std::cout<<"engineWriteHighSide, ref="<<dlmsg->mDlData.getRefCnt()<<"\n";
	GPRSLOG(1) <<tbf <<" <=== engineWriteHighSide: size="<<mDownPDU.size() <<" "<< dlmsg->mDescr<<timestr();
	GPRSLOG(2048)<<" <=== engineWriteHighSide size="<<mDownPDU.size()<<" pdu:"<<mDownPDU.hexstr();
//#if !FAST_TBF
	unsigned bsn = 0;
	// Update: We dont need to take ownership anymore.
	//assert(mDownPDU.isOwner() && ! pdu.isOwner());

	// (pat) old comment: if size == remaining there is a special case [but only if multiple pdus per tbf].
	unsigned remaining = mDownPDU.size();	// remaining in bytes
	unsigned fp = 0;	// pointer into pdu
	while (remaining) {
		RLCDownlinkDataBlock *block = new RLCDownlinkDataBlock(mtChannelCoding());
		unsigned payloadsize = block->getPayloadSize();
		block->mBSN = bsn++;
		if (remaining >= payloadsize) {
			block->mE = 1;	// No extension octet follows.
			// In this case the payload ByteVector segment points directly into the mDownPDU.
			// I tried allocating here, did not help the cause=3105 failure.
			//block->mPayload.clone(mDownPDU.segment(fp,payloadsize));
			block->mPayload.set(mDownPDU.segmentTemp(fp,payloadsize));
			remaining -= payloadsize;
			fp += payloadsize;
		} else {
			// The data will not fill the block.
			// In this case, we will use a ByteVector with allocated memory.
			// The confusing "singular case" mentioned in GSM04.60 10.4.14 does not happen
			// to us because we do not put multiple PDUs in a block.  Yet.
			block->mE = 0;	// redundant
			ByteVector payload(payloadsize);	// Allocate a new one. What horrible syntax.
			ByteVector seg = mDownPDU.segmentTemp(fp,remaining);

			// We have to add an extension octet to specify the PDU segment length.
			int sbh = RLCSubBlockHeader::makeoctet(remaining,0,1);
			payload.setByte(0, sbh);

			// Add the PDU segment.
			payload.setSegment((size_t)1, seg);

			// Note: unused RLC data field filled with 0x2b as per 04.60 10.4.16
			int fillsize = payloadsize - remaining - 1;
			payload.fill(0x2b,remaining+1,fillsize);
			// Try cloning the memory here, but didnt help
			// block->mPayload.clone(payload);
			block->mPayload = payload;	// dups the memory
			remaining = 0;
		}
		// The TFI is not set yet!  TFI will be set by send1Frame just before transmit.
		//block->mTFI = mtTFI;
		if (!remaining) { block->mFBI = true; }
		mSt.TxQ[mSt.TxQNum++] = block;
	}
#endif
}

// Return the block at vs.
// If vs is past the end of the previously sent blocks, then:
// if we have sent the FBI, return that block,
// otherwise get the next block, or NULL if no more data avail,
// which can only happen in persistent mode.
RLCDownlinkDataBlock *RLCDownEngine::getBlock(unsigned vs,
	int tn)	// Timeslot Number, for debugging only.
{
#if FAST_TBF
	mTotalDataBlocksSent++;
	if (vs == mSt.TxQNum) {
		// Did we finish?  Look at the next-to-last block to see if it has fbi set.
		RLCDownlinkDataBlock *prev = mSt.TxQ[addSN(vs,-1)];
		if (prev && prev->mFBI) { return prev; }

		// Manufacture the next block.
		mUniqueDataBlocksSent++;
		mtMS->msCountBlocks.addHit();
		// Clean up behind ourselves when wrapping around.
		if (mSt.TxQ[mSt.TxQNum]) { delete mSt.TxQ[mSt.TxQNum]; mSt.TxQ[mSt.TxQNum] = 0; }
		RLCDownlinkDataBlock *block = engineFillBlock(mSt.TxQNum,tn);
		if (block == NULL) { return NULL; }
		mAllAcked = false;	// It is a brand new block.
		GPRSLOG(4096) << "getBlock"<<LOGVAR(vs)<<":"<<block->str();
		mSt.TxQ[mSt.TxQNum] = block;
		//mSt.sendTime[mSt.TxQNum] = gBSNNext;
		mSt.VB[mSt.TxQNum] = false;		// block needs an ack.
		incSN(mSt.TxQNum);
	} else {
		mtMS->msCountBlocks.addMiss();
	}
#endif
	assert(mSt.TxQ[vs]);
	return mSt.TxQ[vs];
}

#if FAST_TBF
// Depending on dlPersistentmode, when we run out of data we can either terminate
// the downlink tbf by setting the fbi indicator in the last block sent,
// or we can leave the TBF open pending further data, which means the routine
// will send NULL when data exhausted.
// This routine always returns NULL if it is out of data, but in non-persistent-mode
// the caller notices the FBI bit and does not call it any more after
// receiving the last block.
RLCDownlinkDataBlock* RLCDownEngine::engineFillBlock(unsigned bsn,
	int tn)	// Timeslot Number, for debugging only.
{
	const int maxPdus = 10;
	int li[maxPdus];
	int mbit[maxPdus];		// mbit = 1 implies a new PDU starts after the current one.
	ByteVector pdus[maxPdus];
	int pducnt = 0;
	int licnt = 0;
	bool fbi = false;		// final block indicator

	// Create the block.
	RLCDownlinkDataBlock *block = new RLCDownlinkDataBlock(mtChannelCoding());
	int payloadsize = block->getPayloadSize();
	int payloadavail = payloadsize;

	bool nonIdle = !!mDownPDU.size();

	// First make a list of the pdus to go in the rlc block.

	// Dont think it is possible for licnt to reach maxPdus without pducnt hitting maxPdus first, but test anyway.
	while (payloadavail>0 && pducnt < maxPdus && licnt < maxPdus) {

		// Is there any more data?
		if (mDownPDU.size() == 0) {
			// For testing, if SinglePduMode send just one pdu at a time:
			// The first pdu was loaded by engineWriteHighSide, so we just ignore the q.
			if (configGetNumQ("GPRS.SinglePduMode",0)) {break;}
			if (queueFrontExistsAndIsNotTlliChangeCommand(mtMS)) {
				SGSN::GprsSgsnDownlinkPdu *dlmsg = mtMS->msDownlinkQueue.readNoBlock();
				assert(dlmsg);
				mtMS->msDownlinkQDelay.addPoint(dlmsg->mDlTime.elapsed()/1000.0);
				mDownPDU = dlmsg->mDlData;
				mtMS->msBytesDown += mDownPDU.size();
				if (! dlmsg->isKeepAlive()) { nonIdle = true; }
				// Remember the last sdu for possible retry on failure.
				if (mDownlinkPdu) {delete mDownlinkPdu;}
				mDownlinkPdu = dlmsg;
				getTBF()->mtDescription = dlmsg->mDescr;
				LOGWATCHF("pdu %d\n",mDownPDU.size());
				GPRSLOG(1) <<getTBF() <<" <=== engineWriteHighSidePull: size="<<mDownPDU.size() <<" "<< dlmsg->mDescr<<timestr();
				GPRSLOG(2048)<<" <=== engineWriteHighSidePull size="<<mDownPDU.size()<<" pdu:"<<mDownPDU.hexstr();
			}

			// DEBUG: Disable TBF wrap around.
			// If the new pdu clearly wont fit, dont add it.
			// 6-11: This was added for debugging but clearly works fine now and could be removed.
			if (configGetNumQ("GPRS.TBF.nowrap",0)) {
				if (mSt.TxQNum + (mDownPDU.size() / (payloadsize-1)) >= mSNS-1) {
					LOGWATCHF("debug: Skipping wrap-around\n");
					fbi = true;
					break;
				}
			}
		}

		int sdusize = mDownPDU.size();	// sdu remaining bytes
		if (sdusize == 0) {break;}	// No more incoming data.

		if (sdusize > payloadavail || (sdusize == payloadavail && pducnt)) {
			if (pducnt) { mbit[licnt-1] = 1; }
			pdus[pducnt++] = mDownPDU.head(payloadavail);
			mDownPDU.trimLeft(payloadavail);
			payloadavail = 0;
		} else if (sdusize == payloadavail && pducnt == 0) {
			// Special case for single pdu exactly fills the block.
			// If this is the final block, we set FBI and can ommit the length indicator.
			// Otherwise we put out all but the last byte of sdu and use a special zero length indicator.
			// The next rlc block will get the final byte of this sdu.
			// I disabled this special case out, just to be safe; the penalty
			// is occassionally sending an extra block with only one byte in it.
			if (0 && mtMS->msDownlinkQueue.size() == 0 && ! dlPersistentMode()) {
				// This is the final block, so the fbi indicator tells the MS
				// the data ends at the end of the block and we do not need
				// the 'singular case' 0 li field.
				fbi = true;	// Make sure to set this here, in case an asynchronous
							// process adds data between here and the time we
							// check msDownlinkQueue.size() at the end of this loop.
				pdus[pducnt++] = mDownPDU;
				mDownPDU.trimLeft(payloadavail);
				//LOGWATCHF("debug: exactly full block\n");
			} else {
				// Not the final block; need to add the 'singular case' 0 li field,
				// and final byte of the pdu will go in the next block.
				li[licnt] = 0;
				mbit[licnt] = 0;
				licnt++;
				payloadavail--;	// For the li field.
				pdus[pducnt++] = mDownPDU.head(payloadavail);
				mDownPDU.trimLeft(payloadavail);
				//LOGWATCHF("debug: singular case block\n");
			}
			payloadavail = 0;
		} else {	// sdusize < payloadavail
			if (payloadavail == 1) {break;}	// too small to use.
			if (pducnt) { mbit[licnt-1] = 1; }
			pdus[pducnt++] = mDownPDU;
			mDownPDU.trimLeft(sdusize);
			li[licnt] = sdusize;
			mbit[licnt] = 0;	// Until proven otherwise.
			licnt++;
			payloadavail--;	// For the li field
			payloadavail -= sdusize;
		}
	}

	if (pducnt == 0) {
		// There is no data ready to go.
		delete block;
		return NULL;
	}

	if (!dataAvail() && !dlPersistentMode()) { fbi = true; }

	if (configGetNumQ("GPRS.SinglePduMode",0)) {
		// For testing, send just one pdu at a time:
		if (mDownPDU.size() == 0) { fbi = true; }
	}

	if (fbi) mDownFinished = true;
	block->mFBI = fbi;
	block->mBSN = bsn;
	block->mIdle = !nonIdle;

	// DEBUG:
	//static unsigned lastbsn = 0;
	//if (fbi) { lastbsn = 0; }
	//if (bsn < lastbsn) {
	//	// This is a BUG.
	//	printf("BUG\n");
	//	GPRSLOG(1) << "BUG HERE"<<getTBF();
	//}

	char report[300];
	if (GPRSDebug || configGetNumQ("GPRS.WATCH",0)) sprintf(report,"T%s tn=%d block=%d cc=%d qn=%d fbi=%d",getTBF()->tbfid(1),tn,bsn,(int)block->mChannelCoding,mSt.TxQNum,fbi);
	if (licnt == 0) {
		// Entire block is payload.
		block->mE = 1;	// No extension octet follows.
		assert(pducnt == 1);
		assert(pdus[0].size() == (unsigned)payloadsize);
		block->mPayload = pdus[0];
	} else {
		block->mPayload = ByteVector(payloadsize);
		block->mPayload.setAppendP(0);
		// Add the licnts
		for (int i = 0; i < licnt; i++) {
			// Add the extension octet to specify the PDU segment length.
			int sbh = RLCSubBlockHeader::makeoctet(li[i],mbit[i],i == licnt-1);
			if (GPRSDebug) sprintf(report+strlen(report)," li=%d:%d:%d",li[i],mbit[i],i==licnt-1);
			block->mPayload.appendByte(sbh);
		}
		// Add the pdu segments.
		for (int j = 0; j < pducnt; j++) {
			block->mPayload.append(pdus[j]);
			if (GPRSDebug) sprintf(report+strlen(report)," seg=%d",pdus[j].size());
		}
		// Add filler, if any.  Unused RLC data field filled with 0x2b as per 04.60 10.4.16
		int fillsize = payloadsize - block->mPayload.size();
		if (GPRSDebug) sprintf(report+strlen(report)," fill=%d",fillsize);
		if (fillsize) block->mPayload.appendFill(0x2b,fillsize);
	}
	LOGWATCHF("%s\n",report);

	// The TFI is not set yet!  TFI will be set by send1Frame just before transmit.
	//block->mTFI = mtTFI;
	return block;
}
#endif

// Is this the final unacked block?
// Note that it may not be the last block in mSt.VB
// because we may be resending some previous block.
//bool RLCDownEngine::isLastUABlock()
//{
//	if (mtUnAckMode) {
//		return mSt.VS == (mSt.TxQNum-1);
//	}
//	int cnt = 0;	// count of unacked blocks.
//	for (unsigned i = mSt.VA; i < mSt.TxQNum; i++) {
//		if (!mSt.VB[i]) { cnt++; }		// Are we still waiting for an ack for this block?
//		if (cnt > 1) { return false; }
//	}
//	assert(cnt == 1);
//	return true;
//}

// How many blocks to go, but dont bother to count more than 5.
// Now that we manufacture downlink rlc blocks on demand, this is difficult to calculate,
// so just return a guess.
//unsigned RLCDownEngine::blocksToGo()
//{
//	unsigned i, cnt = 0;	// count of unacked blocks remaining.
//	for (i = mSt.VS; deltaSN(i,mSt.TxQNum)<0; incSN(i)) {
//		if (!mSt.VB[i]) { cnt++; }		// Are we still waiting for an ack for this block?
//		if (cnt > 5) break;
//	}
//#if FAST_TBF
//	if (cnt < 5) {
//		// Estimate number of blocks from remaining data.
//		int payloadsize = mtPayloadSize();
//		cnt += mDownPDU.size() / payloadsize;
//		if (cnt > 5) return cnt;
//		if (configGetNumQ("GPRS.SinglePduMode",0)) {return cnt;}
//		if (mtMS->msDownlinkQueue.size()) {
//			// Just assume its a bunch of data.
//			return 6;
//		}
//	}
//#endif
//	return cnt;
//}


// See if the TBF in this RLCEngine has some RLC data or control messages to send.
// Return TRUE if we sent something on the downlink.
bool RLCDownEngine::engineService(PDCHL1Downlink *down)
{
	//TBFState::type state = mtGetState();
	//stalled(); // Are we stalled?  Sets mDownStalled.
	//if (state == TBFState::DataFinal || (WaitForStall && mDownStalled)) {
		// The expectedack time is set in send1DataFrame.
		// The RRBP ACK is optional, so mtExpectedAckBSN may not yet
		// be valid, but mtMsgPending handles that.
		//if (mtMsgPending()) { return false; }
	//}

	// Logic restructured and this code moved below...
	//if (mAllAcked && ! dataAvail()) { return false; }

	// Note: When an acknack is received, mSt.VS is set back to the first
	// negatively acknowledged block, so we advanceVS() after using VS, not before.
	// After final block sent, VS is left == TxNum.
	bool advanced = true;
	RLCDownlinkDataBlock *block = getBlock(mSt.VS,down->TN());

	if (block == NULL) {
		// This happens when data is exhausted, in which case VS == TxNum.
		// Only send the final block on the primary channel.
		if (!getTBF()->isPrimary(down)) {return false;}

		if (mAllAcked) {
			// All current TBFs have been sent and acknowledged.
			// This case does not occur if we are doing one TBF at a time,
			// so we dont need to check for that case specially.
			assert(dlPersistentMode());
			//if (dlPersistentMode())
			if (mtDownKeepAliveTimer.elapsed() > (int)gL2MAC.macDownlinkKeepAlive) {
				// Start a new keep-alive on its way.
				mtMS->sgsnSendKeepAlive();
				mtDownKeepAliveTimer.setNow();
			}
			return false;	// Nothing to do.
		}

		// After sending all unacknowledged blocks we just send the final block
		// over and over.  Alternatively we could start over at VA.
		// If so, it should be done only if no other TBFs have something more
		// important to do.

		assert(mDownFinished);
		// Back up one to get the final block to resend.
		assert(mSt.VS == mSt.TxQNum);
		block = getBlock(addSN(mSt.VS,-1),down->TN());
		advanced = false;
		assert(block);
	}

	if (block->mIdle) {
		assert(dlPersistentMode());
		if (!mDownFinished && mtDownPersistTimer.expired()) {
			// Time to kill the tbf.
			block->mFBI = true;
			mDownFinished = true;
		}
	} else if (dlPersistentMode()) {
		// We are sending a non-idle block.
		if (gL2MAC.macDownlinkKeepAlive) mtDownKeepAliveTimer.setNow();
		if (gL2MAC.macDownlinkPersist) mtDownPersistTimer.setNow();
	}

	// TODO:
	// 44.060 9.3.1 says that after all pending unacked blocks have been transmitted,
	// you can start over again.


	// Astonishingly, we have to resend the final block every time we
	// send any acknowledged blocks.
	// You can not just set the FBI indicator in the last block sent.
	// I am not sure if the MS will not re-ack the final block, so do not
	// reset its ack indicator, just retransmit it.
	if (block->mFBI)
	{
		if (!getTBF()->isPrimary(down)) {return false;}
		// We need an acknowledgment for the final block, even in unacknowledged mode.
		// We are allowed to have three RRBPs pending at a time, total for the MS, which includes
		// both uplink and downlink TBFs, so we will only have one at a time
		// for each of the two possible uplink and downlink tbfs,
		// so wait if we already have an earlier RRBP out.
		if (mtMsgPending()) { return false; }
		//mtMsgReset();
		if (! down->send1DataFrame(this, block, 2, MsgTransDataFinal,&mtN3105)) { return false; }
		// This is not needed:
		//mtMsgSetWait();	// Dont resend again until reservation passed.
		if (mtUnAckMode) {
			// We're done.  We send it once and forget it.
			// TODO: unacknowledged mode - are we supposed to wait for the RRBP reply?
			mtFinishSuccess();	// We just sent the last block.
		} else {
			// This mandated timer is dumb.  This time period is no different than
			// the rest of the downlink in that we are
			// still counting unanswered RRBPs, and will detect
			// a non-responsive MS that way.
			mtMS->msT3191.set();
		}
	} else {
		assert(mtGetState() == TBFState::DataTransmit);
		int rrbpflag = 0;
		if (mtUnAckMode) {
			// 9.3.3.5: For unack mode do not have to set RRBP except in final block.
			rrbpflag = 0;		// redundant assignment.
		} else if (mDownStalled) {
			if (!getTBF()->isPrimary(down)) {return false;}
			// Every block we send will include a reservation.
			// If there is only one ms, we should probably go ahead and send
			// other blocks too, so the behavior should depend on the utilization,
			// and also on how big this TBF is, ie, we should have another variable
			// that tracks when we start a stall resend, send all those blocks,
			// then possibly wait for the reservation.
			if (mtMsgPending()) { return false; }
			rrbpflag = 1;
		} else if (getTBF()->isPrimary(down)) {
			// Only send RRBP reservations on the primary channel.

			//static const int RrbpGuardBlocks = 5;
			// The mNumDownPerAckNack and the RrbpGuardBlocks must be chosen to prevent a stall,
			// meaning the sum has to be less than 64; not a problem.
			if (++mNumDownBlocksSinceAckNack >= mNumDownPerAckNack) {
				rrbpflag=1;
			}
			// TODO: This test just does not work with persistent mode,
			// because it prevents us from sending the rrbp in the last block;
			// this test depended on that case being handled by the
			// if (mFBI) branch above.
			// This blocksRemaining test is only an efficiency issue,
			// so get rid of it for now:
			//int blocksRemaining = blocksToGo();
			//if (blocksRemaining <= RrbpGuardBlocks) { rrbpflag = 0; }
			// In this case we dont need to wait for any reservation.
			if (rrbpflag && mtMsgPending()) {
				rrbpflag = 0;	// Dont do two RRBPs at once.
			}
		}

		bool result = down->send1DataFrame(this,block,rrbpflag,MsgTransTransmit,&mtN3105);
		assert(result);	// always succeeds.
#if FAST_TBF
		if (advanced) {
			incSN(mSt.VS);	// Skip block we just sent.
			advanceVS();
		}
#else
		if (!mDownStalled) {
			incSN(mSt.VS);	// Skip block we just sent.
			advanceVS();
		}
#endif
	}

	mTotalBlocksSent++;
	return true;
}

float RLCDownEngine::engineDesiredUtilization()
{
	// Very approximately, stalled downlink TBF wants to retry every few blocks.
	if (stalled()) { return 0.2; }
	return 1.0;	// Transmitting downlink TBF wants the entire bandwidth.
}

// Make sure the blocks are deleted.
RLCDownEngine::~RLCDownEngine()
{
	unsigned i;
	for (i = 0; i < mSNS; i++) {	// overkill, but safe.
		if (mSt.TxQ[i]) { delete mSt.TxQ[i]; mSt.TxQ[i] = NULL; }
	}
#if INTERNAL_SGSN==0
	if (mBSSGDlMsg) { delete mBSSGDlMsg; }
#endif
	if (mDownlinkPdu) { delete mDownlinkPdu; }
	mDownlinkPdu = 0;	// extreme paranoia
}

RLCUpEngine::RLCUpEngine(MSInfo *wms,int wOctetCount) :
		TBF(wms,RLCDir::Up),
		mUpPDU(0),
		mBytesPending(wOctetCount)
{
	mtUpState = RlcUpTransmit;
	memset(&mSt,0,sizeof(mSt));
	mStartUsfGrants = wms->msNumDataUSFGrants;
	// Use the same criteria for persistent mode as for extended uplink.
	// Can only use this if the phone supports geran feature package I?
	mUpPersistentMode = mtMS->msCanUseExtendedUplink();
}

RLCUpEngine::~RLCUpEngine()
{
	unsigned i;
	for (i = 0; i < mSNS; i++) {	// overkill, but safe.
		if (mSt.RxQ[i]) { delete mSt.RxQ[i]; mSt.RxQ[i] = NULL; }
	}
	if (mUpPDU) { delete mUpPDU; }
}

void RLCDownEngine::engineDump(std::ostream &os) const
{
	os <<LOGVAR2("VS",mSt.VS)<<LOGVAR2("VA",mSt.VA)
		<<LOGVAR2("TxQNum",mSt.TxQNum) <<LOGVAR2("stalled",stalled())
		<<LOGVAR(mPrevAckSsn)<<LOGVAR(mResendSsn);
}

void RLCUpEngine::engineDump(std::ostream &os) const
{
	os <<LOGVAR2("VR",mSt.VR)<<LOGVAR2("VQ",mSt.VQ)
		<<LOGVAR2("stalled",stalled())
		<<LOGVAR(mNumUpBlocksSinceAckNack)
		<<LOGVAR(mtUpState);
}

};
