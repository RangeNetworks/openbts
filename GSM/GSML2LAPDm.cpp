/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
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

/*
Many elements follow Daniele Orlandi's <daniele@orlandi.com> vISDN/Q.921
implementation, although no code is copied directly.
*/



/*
	As a general rule, the caller need not hold
	mLock when sending U frames but should hold the 
	lock when transmitting S or I frames.

	This implementation is for use on the BTS side,
	however, some MS functions are included for unit-
	testing.  For actual use in a BTS, these will need
	to be compelted.
*/

#include "GSML2LAPDm.h"
#include "GSMSAPMux.h"
#include <Logger.h>

using namespace std;
using namespace GSM;

//#define NDEBUG


ostream& GSM::operator<<(ostream& os, L2LAPDm::LAPDState state)
{
	switch (state) {
		case L2LAPDm::LinkReleased: os << "LinkReleased"; break;
		case L2LAPDm::AwaitingEstablish: os << "AwaitingEstablish"; break;
		case L2LAPDm::AwaitingRelease: os << "AwaitingRelease"; break;
		case L2LAPDm::LinkEstablished: os << "LinkEstablished"; break;
		case L2LAPDm::ContentionResolution: os << "ContentionResolution"; break;
		default: os << "?" << (int)state << "?";
	}
	return os;
}




void CCCHL2::writeHighSide(const GSM::L3Frame& l3)
{
	OBJLOG(DEBUG) <<"CCCHL2::writeHighSide " << l3;
	assert(mDownstream);
	assert(l3.primitive()==UNIT_DATA);
	L2Header header(L2Length(l3.L2Length()));
	mDownstream->writeHighSide(L2Frame(header,l3));
}



L2LAPDm::L2LAPDm(unsigned wC, unsigned wSAPI)
	:mRunning(false),
	mC(wC),mR(1-wC),mSAPI(wSAPI),
	mMaster(NULL),
	mT200(T200ms),
	mIdleFrame(DATA)
{
	// sanity checks
	assert(mC<2);
	assert(mSAPI<4);

	clearState();

	// Set the idle frame as per GSM 04.06 5.4.2.3.
	mIdleFrame.fillField(8*0,(mC<<1)|1,8);		// address
	mIdleFrame.fillField(8*1,3,8);				// control
	mIdleFrame.fillField(8*2,1,8);				// length
}


void L2LAPDm::writeL1(const L2Frame& frame)
{
	OBJLOG(DEBUG) <<"L2LAPDm::writeL1 " << frame;
	//assert(mDownstream);
	if (!mDownstream) return;
	ScopedLock lock(mLock);
	mDownstream->writeHighSide(frame);
}


void L2LAPDm::writeL1NoAck(const L2Frame& frame)
{
	// Caller need not hold mLock.
	OBJLOG(DEBUG) <<"L2LAPDm::writeL1NoAck " << frame;
	writeL1(frame);
}


void L2LAPDm::writeL1Ack(const L2Frame& frame)
{
	// Caller should hold mLock.
	// GSM 04.06 5.4.4.2
	OBJLOG(DEBUG) <<"L2LAPDm::writeL1Ack " << frame;
	frame.copyTo(mSentFrame);
	mSentFrame.primitive(frame.primitive());
	writeL1(frame);
	mT200.set(T200());
}



void L2LAPDm::waitForAck()
{
	// Block until any pending ack is received.
	// Caller should hold mLock.
	OBJLOG(DEBUG) <<"L2LAPDm::waitForAck state=" << mState << " VS=" << mVS << " VA=" << mVA;
	while (true) {
		if (mState==LinkReleased) break;
		if ((mState==ContentionResolution) && (mVS==mVA)) break;
		if ((mState==LinkEstablished) && (mVS==mVA)) break;
		// HACK -- We should not need a timeout here.
		mAckSignal.wait(mLock,N200()*T200ms);
		OBJLOG(DEBUG) <<"L2LAPDm::waitForAck state=" << mState << " VS=" << mVS << " VA=" << mVA;
	}
}



void L2LAPDm::releaseLink(Primitive releaseType)
{
	OBJLOG(DEBUG) << "mState=" << mState;
	// Caller should hold mLock.
	mState = LinkReleased;
	mEstablishmentInProgress = false;
	mAckSignal.signal();
	if (mSAPI==0) writeL1(releaseType);
	mL3Out.write(new L3Frame(releaseType));
}


void L2LAPDm::clearCounters()
{
	OBJLOG(DEBUG) << "mState=" << mState;
	// Caller should hold mLock.
	// This is called upon establishment or re-establihment of ABM.
	mT200.reset();
	mVS = 0;
	mVA = 0;
	mVR = 0;
	mRC = 0;
	mIdleCount=0;
	mRecvBuffer.clear();
	discardIQueue();
}


void L2LAPDm::clearState(Primitive releaseType)
{
	OBJLOG(DEBUG) << "mState=" << mState;
	// Caller should hold mLock.
	// Reset the state machine.
	clearCounters();
	releaseLink(releaseType);
}



void L2LAPDm::processAck(unsigned NR)
{
	// GSM 04.06 5.5.3, 5.7.2.
	// Q.921 5.6.3.2, 5.8.2.
	// Equivalent to vISDN datalink.c:lapd_ack_frames,
	// but much simpler for LAPDm.
	// Caller should hold mLock.
	OBJLOG(DEBUG) << "NR=" << NR << " VA=" << mVA << " VS=" << mVS;
	mVA=NR;
	if (mVA==mVS) {
		mRC=0;
		mT200.reset();
	}
	mAckSignal.signal();
}



void L2LAPDm::bufferIFrameData(const L2Frame& frame)
{
	// Concatenate I-frames to form the L3 frame.
	/*
		GSM 04.06 5.5.2 states:
		When a data link layer entity is not in an own receiver busy condition
		and receives a valid I frame whose send sequence number is equal to the
		current receive state variable V(R), the data link layer entity shall: 
			- if the M bit is set to "0", concatenate it with previously
			received frames with the M bit set to "1", if any, and pass the complete
			layer 3 message unit to the layer 3 entity using the primitive
			DL-DATA-INDICATION; 
			- if the M bit is set to "1", store the information field of the frame and
			concatenate it with previously received frames with the M bit set to "1",
			if any (Note: no information is passed to the layer 3 entity); 
	*/

	OBJLOG(DEBUG) << frame;
	if (!frame.M()) {
		// The last or only frame.
		if (mRecvBuffer.size()==0) {
			// The only frame -- just send it up.
			OBJLOG(DEBUG) << "single frame message";
			mL3Out.write(new L3Frame(frame));
			return;
		}
		// The last of several -- concat and send it up.
		OBJLOG(DEBUG) << "last frame of message";
		mL3Out.write(new L3Frame(mRecvBuffer,frame.L3Part()));
		mRecvBuffer.clear();
		return;
	}

	// One segment of many -- concat.
	// This is inefficient but simple.
	mRecvBuffer = L3Frame(mRecvBuffer,frame.L3Part());
	OBJLOG(DEBUG) <<"buffering recvBuffer=" << mRecvBuffer;
}


void L2LAPDm::unexpectedMessage()
{
	OBJLOG(NOTICE) << "mState=" << mState;
	// vISDN datalink.c:unexpeced_message
	// For LAPD, vISDN just keeps trying.
	// For LAPDm, just terminate the link.
	// Caller should hold mLock.
	abnormalRelease();
}


void L2LAPDm::abnormalRelease()
{
	// Caller should hold mLock.
	OBJLOG(INFO) << "state=" << mState;
	// GSM 04.06 5.6.4.
	// We're cutting a corner here that we'll
	// clean up when L3 is more stable.
	mL3Out.write(new L3Frame(ERROR));
	sendUFrameDM(true);
	writeL1(ERROR);
	clearState();
}



void L2LAPDm::retransmissionProcedure()
{
	// Caller should hold mLock.
	// vISDN datalink.c:lapd_invoke_retransmission_procedure
	// GSM 04.08 5.5.7, bullet point (a)
	OBJLOG(DEBUG) << "VS=" << mVS << " VA=" << mVA << " RC=" << mRC;
	mRC++;
	writeL1(mSentFrame);
	mT200.set(T200());
	mAckSignal.signal();
}




void L2LAPDm::open()
{
	OBJLOG(DEBUG);
	{
		ScopedLock lock(mLock);
		if (!mRunning) {
			// We can't call this from the constructor,
			// since N201 may not be defined yet.
			mMaxIPayloadBits = 8*N201(L2Control::IFormat);
			mRunning = true;
			mUpstreamThread.start((void *(*)(void*))LAPDmServiceLoopAdapter,this);
		}
		mL3Out.clear();
		mL1In.clear();
		clearCounters();
		mState = LinkReleased;
		mAckSignal.signal();
	}

	if (mSAPI==0) sendIdle();
}


void *GSM::LAPDmServiceLoopAdapter(L2LAPDm *lapdm)
{
	lapdm->serviceLoop();
	return NULL;
}



void L2LAPDm::writeHighSide(const L3Frame& frame)
{
	OBJLOG(DEBUG) << frame;
	switch (frame.primitive()) {
		case UNIT_DATA:
			// Send the data in a single U-Frame.
			sendUFrameUI(frame);
			break;
		case DATA:
			// Send the data as a series of I-Frames.
			mLock.lock();
			sendMultiframeData(frame);
			mLock.unlock();
			break;
		case ESTABLISH:
			// GSM 04.06 5.4.1.2
			// vISDN datalink.c:lapd_establish_datalink_procedure
			// The BTS side should never call this in SAP0.
			// See note in GSM 04.06 5.4.1.1.
			assert(mSAPI!=0 || mC==0);
			if (mState==LinkEstablished) break;
			mLock.lock();
			clearCounters();
			mState=AwaitingEstablish;
			mLock.unlock();
			sendUFrameSABM();
			break;
		case RELEASE:
			// GSM 04.06 5.4.4.2
			// vISDN datalink.c:lapd_dl_release_request
			if (mState==LinkReleased) break;
			mLock.lock();
			if (mState==LinkEstablished) waitForAck();
			clearCounters();
			mEstablishmentInProgress=false;
			mState=AwaitingRelease;
			mT200.set(T200());	// HACK?
			// Send DISC and wait for UA.
			// Don't return until released.
			sendUFrameDISC();
			waitForAck();
			mLock.unlock();
			break;
		case ERROR:
			// Forced release.
			mLock.lock();
			abnormalRelease();
			mLock.unlock();
			break;
		case HARDRELEASE:
			mLock.lock();
			clearState(HARDRELEASE);
			mLock.unlock();
			break;
		default:
			OBJLOG(ERR) << "unhandled primitive in L3->L2 " << frame;
			assert(0);
	}
}





void L2LAPDm::writeLowSide(const L2Frame& frame)
{
	OBJLOG(DEBUG) << frame;
	mL1In.write(new L2Frame(frame));
}



void L2LAPDm::serviceLoop()
{
	mLock.lock();
	while (mRunning) {
		// Block for up to T200 expiration, then check T200.
		// Allow other threads to modify state while blocked.
		// Add 2 ms to prevent race condition due to roundoff error.
		unsigned timeout = mT200.remaining() + 2;
		// If SAP0 is released, other SAPs need to release also.
		if (mMaster) {
			if (mMaster->mState==LinkReleased) mState=LinkReleased;
		}
		if (!mT200.active()) {
			if (mState==LinkReleased) timeout=3600000;
			else timeout = T200();
		}
		OBJLOG(DEBUG) << "read blocking up to " << timeout << " ms, state=" << mState;
		mLock.unlock();
		// FIXME -- If the link is released, there should be no timeout at all.
		L2Frame* frame = mL1In.read(timeout);
		mLock.lock();
		if (frame!=NULL) {
			OBJLOG(DEBUG) << "state=" << mState << " received " << *frame;
			receiveFrame(*frame);
			delete frame;
		}
		if (mT200.expired()) T200Expiration();
	}
	mLock.unlock();
}
	



void L2LAPDm::T200Expiration()
{
	// Caller should hold mLock.
	// vISDN datalink.c:timer_T200.
	// GSM 04.06 5.4.1.3, 5.4.4.3, 5.5.7, 5.7.2.
	OBJLOG(INFO) << "state=" << mState << " RC=" << mRC;
	mT200.reset();
	switch (mState) {
		case AwaitingRelease:
			releaseLink();
			break;
		case ContentionResolution:
		case LinkEstablished:
		case AwaitingEstablish:
			if (mRC>N200()) abnormalRelease();
			else retransmissionProcedure();
			break;
		default:
			break;
	}
}









void L2LAPDm::receiveFrame(const GSM::L2Frame& frame)
{
	OBJLOG(DEBUG) << frame;

	// Caller should hold mLock.

	// Accept and process an incoming frame on the L1->L2 interface.
	// See vISDN datalink.c:lapd_dlc_recv for another example.

	// Since LAPDm is a lot simpler than LAPD, there are a lot fewer primitives.

	// FIXME -- This is a HACK to fix channels that get stuck in wierd states.
	// But if channels are stuck in wierd states, it means there's a bug somehwere.
/*
	if (stuckChannel(frame)) {
		OBJLOG(ERR) << "detected stuck channel, releasing in L2";
		abnormalRelease();
		return;
	}
*/

	switch (frame.primitive()) {
		case ESTABLISH:
			// This primitive means the first L2 frame is on the way.
			clearCounters();
			break;
		case DATA:
			// Dispatch on the frame type.
			switch (frame.controlFormat()) {
				case L2Control::IFormat: receiveIFrame(frame); break;
				case L2Control::SFormat: receiveSFrame(frame); break;
				case L2Control::UFormat: receiveUFrame(frame); break;
			}
			break;
		default:
			OBJLOG(ERR) << "unhandled primitive in L1->L2 " << frame;
			assert(0);
	}
}


void L2LAPDm::receiveUFrame(const L2Frame& frame)
{
	// Also see vISDN datalink.c:lapd_socket_handle_uframe
	OBJLOG(DEBUG) << frame;
	switch (frame.UFrameType()) {
		case L2Control::SABMFrame: receiveUFrameSABM(frame); break;
		case L2Control::DMFrame: receiveUFrameDM(frame); break;
		case L2Control::UIFrame: receiveUFrameUI(frame); break;
		case L2Control::DISCFrame: receiveUFrameDISC(frame); break;
		case L2Control::UAFrame: receiveUFrameUA(frame); break;
		default:
			OBJLOG(NOTICE) << " could not parse U-Bits " << frame;
			unexpectedMessage();
			return;
	}
}



void L2LAPDm::receiveUFrameSABM(const L2Frame& frame)
{
	// Caller should hold mLock.
	// Process the incoming SABM command.
	// GSM 04.06 3.8.2, 5.4.1
	// Q.921 5.5.1.2.
	// Also borrows from vISDN datalink.c:lapd_socket_handle_uframe_sabm.
	OBJLOG(INFO) << "state=" << mState << " " << frame;
	// Ignore frame if P!=1.
	// See GSM 04.06 5.4.1.2.
	if (!frame.PF()) return;
	// Dispatch according to current state.
	// BTW, LAPDm can always enter multiframe mode when requested,
	// so that's another big simplification over ISDN/LAPD.
	switch (mState) {
		case LinkReleased:
			// GSM 04.06 5.4.5, 5.4.1.2, 5.4.1.4
			clearCounters();
			mEstablishmentInProgress = true;
			// Tell L3 what happened.
			mL3Out.write(new L3Frame(ESTABLISH));
			if (frame.L()) {
				// Presence of an L3 payload indicates contention resolution.
				// GSM 04.06 5.4.1.4.
				mState=ContentionResolution;
				mContentionCheck = frame.sum();
				mL3Out.write(new L3Frame(frame.L3Part(),DATA));
				// Echo back payload.
				sendUFrameUA(frame);
			} else {
				mState=LinkEstablished;
				sendUFrameUA(frame.PF());
			}
			break;
		case ContentionResolution:
			// GSM 04.06 5.4.1.4
			// This guards against the remote possibility that two handsets
			// are sending on the same channel at the same time.
			// vISDN's LAPD doesn't need/do this since peers are hard-wired
			if (frame.sum()!=mContentionCheck) break;
			mState=LinkEstablished;
			sendUFrameUA(frame);
			break;
		case AwaitingEstablish:
			// Huh?  This would mean both sides sent SABM at the same time/
			// That should not happen in GSM.
			sendUFrameUA(frame.PF());
			OBJLOG(WARNING) << "simulatenous SABM attempts";
			break;
		case AwaitingRelease:
			// If we are awaiting release, we will not enter ABM.
			// So we send DM to indicate that.
			sendUFrameDM(frame.PF());
			break;
		case LinkEstablished:
			// Latency in contention resolution, GSM 04.06 5.4.2.1.
			if (mEstablishmentInProgress) {
				if (frame.L()) sendUFrameUA(frame);
				else sendUFrameUA(frame.PF());
				break;
			}
			if (frame.L())  {
				abnormalRelease();
				break;
			}
			// Re-establishment procedure, GSM 04.06 5.6.3.
			// This basically resets the ack engine.
			// We should not actually see this, as of rev 2.4.
			OBJLOG(WARNING) << "reestablishment not really supported";
			sendUFrameUA(frame.PF());
			clearCounters();
			break;
		default:
			unexpectedMessage();
			return;
	}
}



void L2LAPDm::receiveUFrameDISC(const L2Frame& frame)
{
	// Caller should hold mLock.
	OBJLOG(INFO) << "state=" << mState << " " << frame;
	mEstablishmentInProgress = false;
	switch (mState) {
		case AwaitingEstablish:
			clearState();
			break;
		case LinkReleased:
			// GSM 04.06 5.4.5
			sendUFrameDM(frame.PF());
			clearState();
			break;
		case ContentionResolution:
		case LinkEstablished:
			// Shut down the link and ack with UA.
			// GSM 04.06 5.4.4.2.
			sendUFrameUA(frame.PF());
			clearState();
			break;
		case AwaitingRelease:
			// We can arrive here if both ends sent DISC at the same time.
			// GSM 04.06 5.4.6.1.
			sendUFrameUA(frame.PF());
			break;
		default:
			unexpectedMessage();
			return;
	}
}


void L2LAPDm::receiveUFrameUA(const L2Frame& frame)
{
	// Caller should hold mLock.
	// GSM 04.06 3.8.8
	// vISDN datalink.c:lapd_socket_handle_uframe_ua

	OBJLOG(INFO) << "state=" << mState << " " << frame;
	if (!frame.PF()) {
		unexpectedMessage();
		return;
	}

	switch (mState) {
		case AwaitingEstablish:
			// We sent SABM and the peer responded.
			clearCounters();
			mState = LinkEstablished;
			mAckSignal.signal();
			mL3Out.write(new L3Frame(ESTABLISH));
			break;
		case AwaitingRelease:
			// We sent DISC and the peer responded.
			clearState();
			break;
		default:
			unexpectedMessage();
			return;
	}
}


void L2LAPDm::receiveUFrameDM(const L2Frame& frame)
{
	// Caller should hold mLock.
	OBJLOG(INFO) << "state=" << mState << " " << frame;
	// GSM 04.06 5.4.5
	if (mState==LinkReleased) return;
	// GSM 04.06 5.4.6.3
	if (!frame.PF()) return;

	// Because we do not support multiple TEIs in LAPDm,
	// and because we should never get DM in response to SABM,
	// this procedure is much simpler that in vISDN LAPD.
	// Unlike LAPD, there's also no reason for LAPDm to not be
	// able to establish ABM, so if we get this message
	// we know the channel is screwed up.

	clearState();
}



void L2LAPDm::receiveUFrameUI(const L2Frame& frame)
{
	// The zero-length frame is the idle frame.
	if (frame.L()==0) return;
	OBJLOG(INFO) << "state=" << mState << " " << frame;
	mL3Out.write(new L3Frame(frame.tail(24),UNIT_DATA));
}






void L2LAPDm::receiveSFrame(const L2Frame& frame)
{
	// Caller should hold mLock.
	// See GSM 04.06 5.4.1.4.
	mEstablishmentInProgress = false;
	switch (frame.SFrameType()) {
		case L2Control::RRFrame: receiveSFrameRR(frame); break;
		case L2Control::REJFrame: receiveSFrameREJ(frame); break;
		default: unexpectedMessage(); return;
	}
}


void L2LAPDm::receiveSFrameRR(const L2Frame& frame)
{
	// Caller should hold mLock.
	OBJLOG(INFO) << "state=" << mState << " " << frame;
	// GSM 04.06 3.8.5.
	// Q.921 3.6.6.
	// vISDN datalink.c:lapd_handle_sframe_rr
	// Again, since LAPDm allows only one outstanding frame
	// (k=1), this is a lot simpler than in LAPD.
	switch (mState) {
		case ContentionResolution:
			mState = LinkEstablished;
			// continue to next case...
		case LinkEstablished:
			// "inquiry response procedure"
			// Never actually seen that happen in GSM...
			if ((frame.CR()!=mC) && (frame.PF())) {
				sendSFrameRR(true);
			}
			processAck(frame.NR());
			break;
		default:
			// ignore
			return;
	}
}

void L2LAPDm::receiveSFrameREJ(const L2Frame& frame)
{
	// Caller should hold mLock.
	OBJLOG(INFO) << "state=" << mState << " " << frame;
	// GSM 04.06 3.8.6, 5.5.4
	// Q.921 3.7.6, 5.6.4.
	// vISDN datalink.c:lapd_handle_s_frame_rej.
	switch (mState) {
		case ContentionResolution:
			mState = LinkEstablished;
			// continue to next case...
		case LinkEstablished:
			// FIXME -- The spec says to do this but it breaks multiframe transmission.
			//mVS = mVA = frame.NR();
			processAck(frame.NR());
			if (frame.PF()) {
				if (frame.CR()!=mC) sendSFrameRR(true);
				else {
					unexpectedMessage();
					return;
				}
			}
			// Since k=1, there's really nothing to retransmit,
			// other than what was just rejected, so kust stop sending it.
			sendIdle();
			break;
		default:
			// ignore
			break;
	}
	// Send an idle frame to clear any repeating junk on the channel.
	sendIdle();
}



void L2LAPDm::receiveIFrame(const L2Frame& frame)
{
	// Caller should hold mLock.
	// See GSM 04.06 5.4.1.4.
	mEstablishmentInProgress = false;
	OBJLOG(INFO) << "state=" << mState << " NS=" << frame.NS() << " NR=" << frame.NR() << " " << frame;
	// vISDN datalink.c:lapd_handle_iframe
	// GSM 04.06 5.5.2, 5.7.1
	// Q.921 5.6.2, 5.8.1
	switch (mState) {
		case ContentionResolution:
			mState=LinkEstablished;
			// continue to next case...
		case LinkEstablished:
			processAck(frame.NR());
			if (frame.NS()==mVR) {
				mVR = (mVR+1)%8;
				bufferIFrameData(frame);
				sendSFrameRR(frame.PF());
			} else {
				// GSM 04.06 5.7.1.
				// Q.921 5.8.1.
				sendSFrameREJ(frame.PF());
			}
		case LinkReleased:
			// GSM 04.06 5.4.5
			break;
		default:
			// ignore
			break;
	}
}



void L2LAPDm::sendSFrameRR(bool FBit)
{
	// GSM 04.06 3.8.5.
	// The caller should hold mLock.
	OBJLOG(INFO) << "F=" << FBit << " state=" << mState << " VS=" << mVS << " VR=" << mVR;
	L2Address address(mR,mSAPI);
	L2Control control(L2Control::SFormat,FBit,0);
	static const L2Length length;
	L2Header header(address,control,length);
	header.control().NR(mVR);
	writeL1NoAck(L2Frame(header));
}


void L2LAPDm::sendSFrameREJ(bool FBit)
{
	// GSM 04.06 3.8.6.
	// The caller should hold mLock.
	OBJLOG(INFO) << "F=" << FBit << " state=" << mState;
	L2Address address(mR,mSAPI);
	L2Control control(L2Control::SFormat,FBit,2);
	static const L2Length length;
	L2Header header(address,control,length);
	header.control().NR(mVR);
	writeL1NoAck(L2Frame(header));
}




void L2LAPDm::sendUFrameDM(bool FBit)
{
	// The caller need not hold mLock.
	OBJLOG(INFO) << "F=" << FBit << " state=" << mState;
	L2Address address(mR,mSAPI);
	L2Control control(L2Control::UFormat,FBit,0x03);
	static const L2Length length;
	L2Header header(address,control,length);
	writeL1NoAck(L2Frame(header));
}


void L2LAPDm::sendUFrameUA(bool FBit)
{
	// The caller need not hold mLock.
	OBJLOG(INFO) << "F=" << FBit << " state=" << mState;
	L2Address address(mR,mSAPI);
	L2Control control(L2Control::UFormat,FBit,0x0C);
	static const L2Length length;
	L2Header header(address,control,length);
	writeL1NoAck(L2Frame(header));
}


void L2LAPDm::sendUFrameUA(const L2Frame& frame)
{
	// Send UA frame with a echoed payload.
	// This is used in the contention resolution procedure.
	// GSM 04.06 5.4.1.4.
	// The caller need not hold mLock.
	OBJLOG(INFO) << "state=" << mState << " " << frame;
	L2Address address(mR,mSAPI);
	L2Control control(L2Control::UFormat,frame.PF(),0x0C);
	L2Length length(frame.L());
	L2Header header(address,control,length);
	writeL1NoAck(L2Frame(header,frame.L3Part()));
}




void L2LAPDm::sendUFrameSABM()
{
	// GMS 04.06 3.8.2, 5.4.1
	// The caller need not hold mLock.
	OBJLOG(INFO) << "state=" << mState;
	L2Address address(mC,mSAPI);
	L2Control control(L2Control::UFormat,1,0x07);
	static const L2Length length;
	L2Header header(address,control,length);
	writeL1Ack(L2Frame(header));
}


void L2LAPDm::sendUFrameDISC()
{
	// GMS 04.06 3.8.3, 5.4.4.2
	// The caller need not hold mLock.
	OBJLOG(INFO) << "state=" << mState;
	L2Address address(mC,mSAPI);
	L2Control control(L2Control::UFormat,1,0x08);
	static const L2Length length;
	L2Header header(address,control,length);
	writeL1Ack(L2Frame(header));
}


void L2LAPDm::sendUFrameUI(const L3Frame& l3)
{
	// GSM 04.06 3.8.4, 5.3.2, not supporting the short header format.
	// The caller need not hold mLock.
	OBJLOG(INFO) << "state=" << mState << " payload=" << l3;
	L2Address address(mC,mSAPI);
	L2Control control(L2Control::UFormat,1,0x00);
	L2Length length(l3.L2Length());
	L2Header header(address,control,length);
	writeL1NoAck(L2Frame(header,l3));
}




void L2LAPDm::sendMultiframeData(const L3Frame& l3)
{
	// See GSM 04.06 5.8.5
	assert(l3.length()<=251);

	// Implements GSM 04.06 5.4.2
	// Caller holds mLock.
	OBJLOG(INFO) << "state=" << mState << " payload=" << l3;
	// HACK -- Sleep before returning to prevent fast spinning
	// in SACCH L3 during release.
	if (mState==LinkReleased) {
		OBJLOG(ERR) << "attempt to send DATA on released LAPm channel";
		abnormalRelease();
		sleepFrames(51);
		return;
	}
	mDiscardIQueue = false;
	size_t bitsRemaining = l3.size();
	size_t sendIndex = 0;
	OBJLOG(DEBUG) << "sendIndex=" << sendIndex<< " bitsRemaining=" << bitsRemaining;
	while (bitsRemaining>0) {
		size_t thisChunkSize = bitsRemaining;
		bool MBit = false;
		if (thisChunkSize==0) break;
		if (thisChunkSize>mMaxIPayloadBits) {
			thisChunkSize = mMaxIPayloadBits;
			MBit = true;
		}
		// This is the point where we block and allow
		// the upstream thread to modify the LAPDm state.
		waitForAck();
		// Did we abort multiframe mode while waiting?
		if (mDiscardIQueue) {
			OBJLOG(DEBUG) <<"aborting (discard)";
			break;
		}
		if ((mState!=LinkEstablished) && (mState!=ContentionResolution)) {
			OBJLOG(DEBUG) << "aborting, state=" << mState;
			break;
		}
		OBJLOG(DEBUG) << "state=" << mState
				<< " sendIndex=" << sendIndex << " thisChunkSize=" << thisChunkSize
				<< " bitsRemaining=" << bitsRemaining << " MBit=" << MBit;
		sendIFrame(l3.segment(sendIndex,thisChunkSize),MBit);
		sendIndex += thisChunkSize;
		bitsRemaining -= thisChunkSize;
	}
}



void L2LAPDm::sendIFrame(const BitVector& payload, bool MBit)
{
	// Caller should hold mLock.
	// GSM 04.06 5.5.1
	OBJLOG(INFO) << "M=" << MBit << " VS=" << mVS  << " payload=" << payload;
	// Lots of sanity checking.
	assert(mState!=LinkReleased);
	assert(payload.size() <= mMaxIPayloadBits);
	assert(payload.size()%8 == 0);
	if (MBit) assert(payload.size()==mMaxIPayloadBits);
	// Build the header and send it.
	L2Address address(mC,mSAPI);
	L2Control control(mVR,mVS,0);
	L2Length length(payload.size()/8,MBit);
	L2Header header(address,control,length);
	mVS = (mVS+1)%8;
	writeL1Ack(L2Frame(header,payload));
}


bool L2LAPDm::stuckChannel(const L2Frame& frame)
{
	// Check for excessive idling.
	if (frame.DCCHIdle()) mIdleCount++;
	else mIdleCount=0;
	return mIdleCount > maxIdle();
}





// vim: ts=4 sw=4
