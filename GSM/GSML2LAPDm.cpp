/*
* Copyright 2008, 2009, 2014 Free Software Foundation, Inc.
* Copyright 2014 Range Networks, Inc.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for
* licensing information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/

#define LOG_GROUP LogGroup::GSM		// Can set Log.Level.GSM for debugging

/*
Many elements follow Daniele Orlandi's <daniele@orlandi.com> vISDN/Q.921
implementation, although no code is copied directly.
6-2014: Pat Thompson rewrote the Layer1 and layer3 connections, primitives and messages.
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
//#include "GSMSAPMux.h"
#include "GSMLogicalChannel.h"
#include <Logger.h>
#include <GSML3RRMessages.h>
#include <L3StateMachine.h>

using namespace std;
using namespace GSM;

//#define NDEBUG


ostream& GSM::operator<<(ostream& os, LAPDState state)
{
	switch (state) {
		case LinkReleased: os << "LinkReleased"; break;
		case AwaitingEstablish: os << "AwaitingEstablish"; break;
		case AwaitingRelease: os << "AwaitingRelease"; break;
		case LinkEstablished: os << "LinkEstablished"; break;
		case ContentionResolution: os << "ContentionResolution"; break;
		default: os << "?" << (int)state << "?";
	}
	return os;
}




L2LAPDm::L2LAPDm(unsigned wC, SAPI_t wSAPI)
	:mRunning(false),
	mC(wC),mR(1-wC),mSAPI(wSAPI),
	mMaster(NULL),
	mState(LAPDStateUnused),
	mT200(T200ms),
	mIdleFrame(/*DATA*/)
{
	// sanity checks
	assert(mC<2);
	//assert(mSAPI<4);
	assert(mSAPI == SAPI0 || mSAPI == SAPI3);	// SAPI 1 and 2 are never used in the spec.

	//releaseLink(true,L3_RELEASE_INDICATION);	// (pat) This sends release in both directions, which is undesirable at initialization.
	clearCounters();

	// Set the idle frame as per GSM 04.06 5.4.2.3.
	mIdleFrame.fillField(8*0,(mC<<1)|1,8);		// address
	mIdleFrame.fillField(8*1,3,8);				// control
	mIdleFrame.fillField(8*2,1,8);				// length
	if (gConfig.getBool("GSM.Cipher.ScrambleFiller")) mIdleFrame.randomizeFiller(8*4);
}


void L2LAPDm::writeL1(const L2Frame& frame)
{
	OBJLOG(DEBUG) <<"L2LAPDm::writeL1 " << frame;
	if (!mL2Downstream) return;		// only possible during initialization, and maybe not even then.
	// It is tempting not to lock this, but if we don't,
	// the ::open operation can result in contention in L1.
	ScopedLock lock(mL1Lock);
	//mL2Downstream->sapWriteHighSide(frame);	// (pat) This blocks in L1Encoder::transmit until the time has passed.
	mL2Downstream->writeToL1(frame);	// (pat) This blocks in L1Encoder::transmit until the start time has passed.
}


// For FACCHL2 and SDCCHL2 we can dispense with the service loop and send L3 messages
// directly to the L3 state machine.  Prior to l3rewrite this was done by DCCHDispatchMessage.
void L2LAPDm::writeL3(L3Frame *frame)
{
	OBJLOG(DEBUG) << LOGVAR(frame->size())<<LOGVAR(frame->primitive());
	//mL3Out.write(frame);
	mL2Downstream->writeToL3(frame);
}

// For SACCH we retain the L2 service loop to handle measurement reports and power levels.
// If SACCHLogicalChannel::serviceLoop() does not handle the message, then it will come here and go up to Layer3.
//void SACCHL2::writeL3(L3Frame *frame)
//{
//	OBJLOG(DEBUG) << LOGVAR(frame->size());
//	mL3Out.write(frame);
//}


void L2LAPDm::writeL1NoAck(const L2Frame& frame)
{
	// Caller need not hold mLock.
	OBJLOG(DEBUG) <<"L2LAPDm::writeL1NoAck " << frame;
	writeL1(frame);
}


// This writes something that expects an ACK.  It does not write an ACK.
void L2LAPDm::writeL1Ack(const L2Frame& frame)
{
	// Caller should hold mLock.  (pat) Because we are modifying mSentFrame.
	// (pat) Lock is held when called from sendMultiFrameData and sendUFrameDISC, but not sendUFrameSABM.  Why?  I'm fixing it.
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
	//OBJLOG(DEBUG) <<"L2LAPDm::waitForAck state=" << mState << " VS=" << mVS << " VA=" << mVA;
	OBJLOG(DEBUG) <<"L2LAPDm::waitForAck ";		// OBJLOG prints the entire LAPDm object now.
	while (true) {
		if (mState==LinkReleased) break;
		if ((mState==ContentionResolution) && (mVS==mVA)) break;
		if ((mState==LinkEstablished) && (mVS==mVA)) break;
		// HACK -- We should not need a timeout here.
		// (pat) When we send a RELEASE the calling thread blocks FOREVER so this timeout
		// is needed to prevent the channel from hanging.  It will be gone for 30 secs.
		mAckSignal.wait(mLock,N200()*T200ms);
		//OBJLOG(DEBUG) <<"L2LAPDm::waitForAck state=" << mState << " VS=" << mVS << " VA=" << mVA;
		OBJLOG(DEBUG) <<"L2LAPDm::waitForAck ";
	}
}



// (pat 4-2014) The high side sends a L3ChannelRelease and then a RELEASE or HARDRELEASE.
// The handset may send the UFrameDISC before we send ours, in which case this sends a RELEASE
// down to L1FEC which does a close() on the main channel, and subsequently we get the RELEASE from above
// and send the UFrameDISC resulting in a "writeHighSide sending on non-active channel" message.
// Also note that the handset may continue to send up measurement reports for some time after the main channel is closed.
// I dont see where we make the SACCH non active; I think we just wait for valid messages to stop coming and it times out via T3109.
void L2LAPDm::releaseLink(bool notifyL3, Primitive releaseType)
{
	OBJLOG(DEBUG) <<LOGVAR(releaseType) <<this;
	clearCounters();
	// Caller should hold mLock.
	mState = LinkReleased;
	mEstablishmentInProgress = false;
	mAckSignal.signal();

	// (pat) This line of code is magic and loaded with assumed dependencies, which are dictated by GSM specs:
	// The SAPMux has multiple uplink and just one downlink connection.
	// From here the RELEASE or HARDRELEASE traverses the hierarchy downward and is finally processed
	// by XCCHL1Encoder::writeHighSide(), where it releases the physical channel (starts sending idle fills.)
	// For the connections we care about SAP0 is the RR/MM/CC message connection, and SAP3 is SMS.
	// This means that the RELEASE/HARDRELEASE does not close the dowstream channel if arriving from an SMS connection,
	// which is dictated in GSM 4.06 5.4
	// We no longer send anything to L1.  L2LogicalChannel lurs on the L3 connection and handles it.
	// (pat) After sending the DM we are not supposed to close the channel until we allow some time for a reply;
	// that is now handled by L2LogicalChannel::T3111
	if (notifyL3) {
		writeL3(new L3Frame(mSAPI,releaseType));
	}
	// old: if (mSAPI==0) writeL1(L2Message(releaseType));
	// print this at the end of this routine; during initialization the object is not yet inited.
}


void L2LAPDm::clearCounters()
{
	OBJLOG(DEBUG);//<< "mState=" << mState;
	// Caller should hold mLock.
	// This is called upon establishment or re-establihment of ABM.
	mT200.reset();
	mVS = 0;
	mVA = 0;
	mVR = 0;
	mRC = 0;
	mIdleCount=0;
	mRecvBuffer.clear();
	// Signal sendMultiframeData to discard outgoing frame.
	mDiscardIQueue = true;
}


void L2LAPDm::processAck(unsigned NR)
{
	// GSM 04.06 5.5.3, 5.7.2.
	// Q.921 5.6.3.2, 5.8.2.
	// Equivalent to vISDN datalink.c:lapd_ack_frames,
	// but much simpler for LAPDm.
	// Caller should hold mLock.
	OBJLOG(DEBUG);//<< "NR=" << NR << " VA=" << mVA << " VS=" << mVS;
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
			writeL3(new L3Frame(mSAPI,frame));
			return;
		}
		// The last of several -- concat and send it up.
		OBJLOG(DEBUG) << "last frame of message";
		writeL3(new L3Frame(mSAPI,mRecvBuffer,frame.L3Part()));
		mRecvBuffer.clear();
		return;
	}

	// One segment of many -- concat.
	// This is inefficient but simple.
	//mRecvBuffer = L3Frame(mRecvBuffer,frame.L3Part());	// RecvBuffer is a BitVector2!!
	mRecvBuffer = BitVector2(mRecvBuffer,frame.L3Part());
	OBJLOG(DEBUG) <<"buffering recvBuffer=" << mRecvBuffer;
}


void L2LAPDm::unexpectedMessage(int whence)
{
	OBJLOG(NOTICE) <<" from:"<<whence;//<< "mState=" << mState;
	// vISDN datalink.c:unexpeced_message
	// For LAPD, vISDN just keeps trying.
	// For LAPDm, just terminate the link.
	// Caller should hold mLock.
	abnormalRelease(true);		// (pat) Send a DM response; thats what it is for.
}


void L2LAPDm::abnormalRelease(bool sendDM)
{
	// Caller should hold mLock.
	OBJLOG(INFO);//<< "state=" << mState;
	// GSM 04.06 5.6.4.
	// We're cutting a corner here that we'll
	// clean up when L3 is more stable.
	// (pat) FIXME - abnormal release is called from both directions.
	// (pat) maybe add a couple of bits to indicate errors coming from the two ends.
	if (sendDM) sendUFrameDM(true);
	releaseLink(true,MDL_ERROR_INDICATION);
}



void L2LAPDm::retransmissionProcedure()
{
	// Caller should hold mLock.
	// vISDN datalink.c:lapd_invoke_retransmission_procedure
	// GSM 04.08 5.5.7, bullet point (a)
	OBJLOG(DEBUG);//<< "VS=" << mVS << " VA=" << mVA << " RC=" << mRC;
	mRC++;
	writeL1(mSentFrame);
	mT200.set(T200());
	mAckSignal.signal();
}




void L2LAPDm::l2dlOpen(std::string wDescriptiveString)
{
	myid = wDescriptiveString;
	OBJLOG(DEBUG);
	{
		ScopedLock lock(mLock);
		OBJLOG(DEBUG);
		if (!mRunning) {
			OBJLOG(DEBUG);
			// We can't call this from the constructor,
			// since N201 may not be defined yet.
			mMaxIPayloadBits = 8*N201(L2Control::IFormat);
			mRunning = true;
			// Try a 32K stack for 32 bit machines.
			mUpstreamThread.start2((void *(*)(void*))LAPDmServiceLoopAdapter,this, 8000*sizeof(void*));
		}
		OBJLOG(DEBUG);
		//mL3Out.clear();
		mL1In.clear();
		clearCounters();
		mState = LinkReleased;
		mAckSignal.signal();
	}

	OBJLOG(DEBUG);
	if (mSAPI==0) sendIdle();
	OBJLOG(DEBUG);
}

void *GSM::LAPDmServiceLoopAdapter(L2LAPDm *lapdm)
{
	lapdm->lapServiceLoop();
	return NULL;
}


void L2LAPDm::normalRelease()
{
	LOG(DEBUG) <<this;
	// GSM 04.06 5.4.4.2
	// vISDN datalink.c:lapd_dl_release_request
	ScopedLock lock(mLock);
	if (mState==LinkReleased || mState==AwaitingRelease) return;
	if (mState==LinkEstablished) waitForAck();	// (pat) Does not wait if nothing has been sent.
	// (pat) The handset may ack with a DISC, so check again:
	if (mState==LinkReleased || mState==AwaitingRelease) return;	// Cant be AwaitingRelease, but check anyway.
	LOG(DEBUG) <<this;
	clearCounters();
	mEstablishmentInProgress=false;
	mState=AwaitingRelease;
	mT200.set(T200());	// HACK?
	// Send DISC and wait for UA.
	// Don't return until released.
	sendUFrameDISC();
	waitForAck();
	LOG(DEBUG) <<this;
}


void L2LAPDm::l2dlWriteHighSide(const L3Frame& frame)
{
	OBJLOG(DEBUG) << frame;
	switch (frame.primitive()) {
		case L3_UNIT_DATA:
			// Send the data in a single U-Frame.
			sendUFrameUI(frame);
			break;
		case L3_DATA:
			// Send the data as a series of I-Frames.
			mLock.lock();
			if (sendMultiframeData(frame)) {
				writeL3(new L3Frame(mSAPI,L3_DATA_CONFIRM));
			}
			mLock.unlock();
			break;
		case L3_ESTABLISH_REQUEST:	// (pat) Establish SABM mode.
			// GSM 04.06 5.4.1.2
			// vISDN datalink.c:lapd_establish_datalink_procedure
			// The BTS side should never call this in SAP0.
			// See note in GSM 04.06 5.4.1.1.  (pat) And I quote:  "For SAPI 0 the data link is always established by the MS."
			devassert(mSAPI!=0 || mC==0);	// (pat) Could be on SAPI 0 only in test mode.
			if (mState==LinkEstablished) {
				// We are supposed to send ESTABLISH_CONFIRM, but since L3 does not care which side starts the establish process
				// or distinguish between ESTABLISH_CONFIRM and ESTABLISH_INDICATION.
				writeL3(new L3Frame(mSAPI,L3_ESTABLISH_CONFIRM));
				break;
			}
			mLock.lock();
			clearCounters();
			mState=AwaitingEstablish;
			//mLock.unlock();  (pat 5-2014) I am changing to hold lock while calling sendUFrameSABM, since it modifies mSentFrame.
			sendUFrameSABM();
			mLock.unlock();
			break;
		case L3_RELEASE_REQUEST:
			normalRelease();
			break;
		case L3_HARDRELEASE_REQUEST:
			mLock.lock();
			releaseLink(false,(Primitive)0);
			mLock.unlock();
			break;
#if 0
		case ERROR:
			devassert(0);	// Layer3 never sends this; only RELEASE or HARDRELEASE.
			// Forced release.
			mLock.lock();
			abnormalRelease(true);	// (pat) Send a DM to alert the peer.
			mLock.unlock();
			break;
#endif
		default:
			OBJLOG(ERR) << "unhandled primitive in L3->L2 " << frame;
			devassert(0);
	}
}





void L2LAPDm::l2dlWriteLowSide(const L2Frame& frame)
{
	OBJLOG(DEBUG) << frame;
	mL1In.write(new L2Frame(frame));
}



void L2LAPDm::lapServiceLoop()
{
	mLock.lock();
	while (mRunning) {
		// Block for up to T200 expiration, then check T200.
		// Allow other threads to modify state while blocked.
		// Add 2 ms to prevent race condition due to roundoff error.
		unsigned timeout = mT200.remaining() + 2;
		// If SAP0 is released, other SAPs need to release also.

		// (pat 6-2014) This seems to be a bug fix to compensate for improper initialization,
		// and it depended on the mMaster only being set on the host channel,
		// since if you set it on sacch it will keep anything from ever going through since sap0 is always in idle (LinkReleased) state.
		// if (mMaster) {
		//	if (mMaster->mState==LinkReleased) {
		//		OBJLOG(DEBUG) << "master release";
		//		mState=LinkReleased;
		//	}
		// }

		if (!mT200.active()) {
			if (mState==LinkReleased) timeout=3600000;
			else timeout = T200();	// currently 3.6sec  (pat) Looks like .9 secs on TCH,SDCCH and 4*.9 on SACCH.  Why?
			// (pat) FIXME: T3111 must be longer that this, right?
		}
		OBJLOG(DEBUG) << "read blocking up to " << timeout << " ms ";//, state=" << mState;
		mLock.unlock();
		// FIXME -- If the link is released, there should be no timeout at all.
		L2Frame* frame = mL1In.read(timeout);
		LOG(DEBUG);
		mLock.lock();
		if (frame!=NULL) {
			//OBJLOG(DEBUG) << "state=" << mState << " received " << *frame;
			OBJLOG(DEBUG) << " received " << *frame;
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
		case AwaitingRelease: // GSM 4.06 5.4.4.3
			// (pat) 5.4.4.3 says retransmit the DISC.
			// old: releaseLink(MDL_ERROR_INDICATION); break;
			// Fall Through
		case ContentionResolution:
		case LinkEstablished:
		case AwaitingEstablish:		// (pat) SABM sent.  Should only happen on SAPI 3 since MS establishes link SAPI 0
			// (pat) 4.06 5.5.7: L2 is supposed to query L3 whether to release link or attempt re-establishment, but we just release.
			// It either does nothing ("local end release") or starts a normal release procedure which would send a DISC.
			if (mRC>N200()) abnormalRelease(false);	// (pat) do not send DM.  If anything, we would send DISC.
			else retransmissionProcedure();
			break;
		default:
			break;
	}
}









// (pat) Receive a frame from layer 1.
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
		//case ESTABLISH:
		case PH_CONNECT:
			// This primitive means the first L2 frame is on the way.
			// (pat) We dont send an ESTABLISH primitive upstream to L3 until we get the SABM command.
			clearCounters();
			mState = LinkReleased;	// Idle state.
			break;
		case L2_DATA:
			// Dispatch on the frame type.
			switch (frame.controlFormat()) {
				case L2Control::IFormat: receiveIFrame(frame); break;
				case L2Control::SFormat: receiveSFrame(frame); break;
				case L2Control::UFormat: receiveUFrame(frame); break;
			}
			break;
		case HANDOVER_ACCESS:
			devassert(0);	// This primitive should now be handled in L2.
			writeL3(new L3Frame(mSAPI,HANDOVER_ACCESS));
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
			unexpectedMessage(1);
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
	//OBJLOG(INFO) << "state=" << mState << " " << frame;
	OBJLOG(INFO) << frame;
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
			writeL3(new L3Frame(mSAPI,L3_ESTABLISH_INDICATION));
			if (frame.L()) {
				// Presence of an L3 payload indicates contention resolution.
				// GSM 04.06 5.4.1.4.
				if (mSAPI==0) mState=ContentionResolution; // only SAP 0 is permitted to enter Contention Resolution
				mContentionCheck = frame.sum();
				writeL3(new L3Frame(mSAPI,frame.L3Part(),L3_DATA));
				// Echo back payload.
				sendUFrameUA(frame);
			} else {
				mState=LinkEstablished;
				sendUFrameUA(frame.PF());
			}
			break;
		case ContentionResolution:
			// (pat) FIXME: This is wrong... Read the spec.
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
				// (pat) Means SABM includes a command, which would attempt to re-establish contention resolution.
				abnormalRelease(true);	// (pat) send DM
				break;
			}
			// Re-establishment procedure, GSM 04.06 5.6.3.
			// This basically resets the ack engine.
			// The most common reason for this is failed handover.
			sendUFrameUA(frame.PF());
			clearCounters();
			break;
		default:
			unexpectedMessage(2);
			return;
	}
}



void L2LAPDm::receiveUFrameDISC(const L2Frame& frame)
{
	// Caller should hold mLock.
	OBJLOG(INFO) << frame;
	mEstablishmentInProgress = false;
	switch (mState) {
		case AwaitingEstablish:
			releaseLink(true,L3_RELEASE_INDICATION);
			break;
		case LinkReleased:
			// GSM 04.06 5.4.5
			sendUFrameDM(frame.PF());
			releaseLink(false,(Primitive)0);
			break;
		case ContentionResolution:
		case LinkEstablished:
			// Shut down the link and ack with UA.
			// GSM 04.06 5.4.4.2.
			sendUFrameUA(frame.PF());
			releaseLink(true,L3_RELEASE_INDICATION);
			break;
		case AwaitingRelease:
			// We can arrive here if both ends sent DISC at the same time.
			// GSM 04.06 5.4.6.1.
			sendUFrameUA(frame.PF());
			releaseLink(true,L3_RELEASE_CONFIRM);
			break;
		default:
			unexpectedMessage(3);
			return;
	}
}


void L2LAPDm::receiveUFrameUA(const L2Frame& frame)
{
	// Caller should hold mLock.
	// GSM 04.06 3.8.8
	// vISDN datalink.c:lapd_socket_handle_uframe_ua

	OBJLOG(INFO) << frame;
	if (!frame.PF()) {
		// (pat) TODO: This looks wrong.  GSM 4.06 5.4.1.2 quote: 'A UA response with the F bit set to "0" shall be ignored.'
		unexpectedMessage(4);
		return;
	}

	switch (mState) {
		case AwaitingEstablish:
			// We sent SABM and the peer responded.
			clearCounters();
			mState = LinkEstablished;
			mAckSignal.signal();
			// We used to use L3_ESTABLISH_INDICATION instead of L3_ESTABLISH_CONFIRM since there is no reason to distinguish between them.
			writeL3(new L3Frame(mSAPI,L3_ESTABLISH_CONFIRM));
			break;
		case AwaitingRelease:
			// We sent DISC and the peer responded.
			releaseLink(true,L3_RELEASE_CONFIRM);
			break;
		case LinkEstablished: // Pat added: This is probably just a duplicate SABM establishment acknowledgement.
			// We could check more carefully if that is true, but who cares...
			break;
		case ContentionResolution:
			// (pat 3-13-2014) It is a bug to get a UA in ContentionResolution, but the Blackberry does this sometimes
			// if you do two testcalls back to back, so dont abort the link because of it...
			// (pat 4-7-2014) Update: The problem occurs when you do an endcall followed by a testcall in less than 30 seconds.
			// (pat 6-2014) and that may be because we did not deactivate SACCH properly at the end of the call?
			LOG(NOTICE) << "Ignoring unexpected LAPDm UA frame in ContentionResolution mode, which may be a bug in the MS.";
			return;
		default:
			unexpectedMessage(5);
			return;
	}
}


// (pat) Disconnect Mode response.  Indicates some error in the peer.  Per 5.4.2 table 7 we give up.
void L2LAPDm::receiveUFrameDM(const L2Frame& frame)
{
	// Caller should hold mLock.
	OBJLOG(INFO) << frame;
	// GSM 04.06 5.4.6.3: "A received DM with F=0 colliding with SABM or DISC is ignored."
	if (!frame.PF()) return;

	// Because we do not support multiple TEIs in LAPDm,
	// and because we should never get DM in response to SABM,
	// this procedure is much simpler that in vISDN LAPD.
	// Unlike LAPD, there's also no reason for LAPDm to not be
	// able to establish ABM, so if we get this message
	// we know the channel is screwed up.

	// 5.4.6.2:
	switch (mState) {
	case LAPDStateUnused:
	case LinkReleased:
		// GSM 04.06 5.4.5: In idle state all "other" frame types ignored.
		return;
	case AwaitingRelease:
		// GSM 4.06 5.4.4.2 
		releaseLink(true,L3_RELEASE_CONFIRM);
		return;
	case AwaitingEstablish:
	case LinkEstablished:
	case ContentionResolution:
		// GSM 4.06 4.1.3.5: Unsolicited DM response is an error.
		// Rel. 8 of 4.06, Sect. 5.4.1.2 says reset T200 and send up a RELEASE_INDICATION to L3.
		// Confusing b/c Sect. 5.4.2.2 suggests (not requires) sending an MDL_ERROR_INDICATION
		// But nothing about releasing the link, so don't do it!
		//releaseLink(true,MDL_ERROR_INDICATION);
	        mT200.set(T200());
                writeL3(new L3Frame(mSAPI,L3_RELEASE_INDICATION));
		//releaseLink(true,L3_RELEASE_INDICATION);
		return;
	
	}

}



void L2LAPDm::receiveUFrameUI(const L2Frame& frame)
{
	// The zero-length frame is the idle frame.
	if (frame.L()==0) return;
	OBJLOG(INFO) << frame;
	writeL3(new L3Frame(mSAPI,frame.alias().tail(24),L3_UNIT_DATA));
}






void L2LAPDm::receiveSFrame(const L2Frame& frame)
{
	// Caller should hold mLock.
	// See GSM 04.06 5.4.1.4.
	mEstablishmentInProgress = false;
	switch (frame.SFrameType()) {
		case L2Control::RRFrame: receiveSFrameRR(frame); break;
		case L2Control::REJFrame: receiveSFrameREJ(frame); break;
		default: unexpectedMessage(6); return;
	}
}


void L2LAPDm::receiveSFrameRR(const L2Frame& frame)		// RR == Receive Ready.
{
	// Caller should hold mLock.
	OBJLOG(INFO) << frame << LOGVAR2("PF",frame.PF());
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
			// (pat) It seems to be sent by the Blackberry after a channel change from SDCCH to TCH,
			// and it is sent on FACCH every 1/2 second forever because we dont answer.
			// PF is the Poll/Final bit 3.5.1.  true means sender is polling receiver; receiver responds with PF=true.
#if ORIGINAL
			if ((frame.CR()!=mC) && (frame.PF())) {
				sendSFrameRR(true);
			}
#else
			if ((frame.CR()==mC) && (frame.PF())) {	// If it is a command 5.8.1 says return an S RR frame.
				sendSFrameRR(true);	// Must return a frame with P==1.
			}
#endif
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
	OBJLOG(INFO) << frame;
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
					unexpectedMessage(7);
					return;
				}
			}
			// Since k=1, there's really nothing to retransmit,
			// other than what was just rejected, so just stop sending it.
			sendIdle();
			break;
		default:
			// ignore
			break;
	}
	// Send an idle frame to clear any repeating junk on the channel.
	// (pat 5-2014) these sendIdle are unnecessary and possibly wrong.  On SAPI 3 we send SIB5/SIB6 and on SAPI0 
	// the idle frame is sent by layer1 dispatch only if necessary.
	sendIdle();
}



void L2LAPDm::receiveIFrame(const L2Frame& frame)
{
	// Caller should hold mLock.
	// See GSM 04.06 5.4.1.4.
	mEstablishmentInProgress = false;
	OBJLOG(INFO) << " NS=" << frame.NS() << " NR=" << frame.NR() << " " << frame;
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
	L2Address address(mR,mSAPI);	// C/R == 0, ie, this is a response.
	L2Control control(L2Control::SFormat,FBit,0);	// (pat) 4.06 3.8.1: Sbits == 0 is supervisory "Receive Ready"
	static const L2Length length;
	L2Header header(address,control,length);
	header.control().NR(mVR);
	OBJLOG(INFO) << "F=" << FBit <<LOGVAR(header) <<LOGVAR(address);		// " state=" << mState << " VS=" << mVS << " VR=" << mVR;
	writeL1NoAck(L2Frame(header));
}


void L2LAPDm::sendSFrameREJ(bool FBit)
{
	// GSM 04.06 3.8.6.
	// The caller should hold mLock.
	OBJLOG(INFO) << "F=" << FBit;// " state=" << mState;
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
	OBJLOG(INFO) << "F=" << FBit;// " state=" << mState;
	L2Address address(mR,mSAPI);
	L2Control control(L2Control::UFormat,FBit,0x03);
	static const L2Length length;
	L2Header header(address,control,length);
	writeL1NoAck(L2Frame(header));
}


void L2LAPDm::sendUFrameUA(bool FBit)
{
	// The caller need not hold mLock.
	OBJLOG(INFO) << "F=" << FBit;// " state=" << mState;
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
	OBJLOG(INFO) /*<< "state=" << mState << " "*/ << frame;
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
	OBJLOG(INFO);// "state=" << mState;
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
	OBJLOG(INFO);// "state=" << mState;
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
	OBJLOG(INFO) << "payload=" << l3;
	devassert(mC == 1);		// C/R is always 1 for commands except for testing where we could make a second dummy LAPDm to emulate a handset.
	L2Address address(mC,mSAPI);
	// (pat 6-2014) The U format is used for both control frames (SABM, DISC, DM, UA) and for unacknowledged information transfer.
	// The P/F bit is used for the former, but not for the latter.
	// GSM 4.06 5.2.1: "For unacknowledged information transfer with normal L2 header, the P/F bit is not used and shall be set to "0".
	// Also GSM 4.06 5.3.2 says P is set to 0.
	// This formerly set P to 1, which was wrong, and doug corrected it below for one message type, but it is wrong for all.
	bool PFPollBit = false;
	L2Control control(L2Control::UFormat,PFPollBit,0x00);
	L2Length length(l3.L2Length());
	L2Header header(address,control,length);
	L2Frame l2f = L2Frame(header, l3);		// (pat) Switches primitive to DATA even if l3 primitive was UNIT_DATA
	// doug? FIXME -
	/**** pat 6-2014 corrected above.
	 * if (l3.PD() == L3RadioResourcePD && l3.MTI() == L3RRMessage::PhysicalInformation) {
	 *	l2f.CR(true);	// (pat) It already is 1; it is always 1 for commands from the BTS.  That is the mC above in L2Address above.
	 *	l2f.PF(false);
	 * }
	 */
	writeL1NoAck(l2f);
}





bool L2LAPDm::sendMultiframeData(const L3Frame& l3)
{
	// See GSM 04.06 5.8.5
        OBJLOG(INFO) << "payload=" << l3;
	assert(l3.length()<=251);

	// Implements GSM 04.06 5.4.2
	// Caller holds mLock.
	// HACK -- Sleep before returning to prevent fast spinning
	// in SACCH L3 during release.
	if (mState==LinkReleased) {
		OBJLOG(ERR) << "attempt to send DATA on released LAPm channel";
		abnormalRelease(false);	// (pat) Do not send DM, just send (probably a repeat) RELEASE to L3 to tell it to stop sending us data.
		// pat 5-2013: Vastly reducing the delays here and in L2LogicalChannel to try to reduce
		// random failures of handover and channel reassignment from SDCCH to TCHF.
		// sleepFrames(51);
		sleepFrames(4);	// FIXME - this is just dopey.  
		return false;
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
		waitForAck();	// (pat) Does not wait if nothing pending
		// Did we abort multiframe mode while waiting?
		if (mDiscardIQueue) {
			OBJLOG(DEBUG) <<"aborting (discard)";
			return false;
		}
		if ((mState!=LinkEstablished) && (mState!=ContentionResolution)) {
			OBJLOG(DEBUG) << "aborting, state=" << mState;
			return false;
		}
		OBJLOG(DEBUG)
				<< " sendIndex=" << sendIndex << " thisChunkSize=" << thisChunkSize
				<< " bitsRemaining=" << bitsRemaining << " MBit=" << MBit;
		sendIFrame(l3.cloneSegment(sendIndex,thisChunkSize),MBit);
		sendIndex += thisChunkSize;
		bitsRemaining -= thisChunkSize;
	}
	return true;	// success
}



void L2LAPDm::sendIFrame(const BitVector2& payload, bool MBit)
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



void CBCHL2::l2dlWriteHighSide(const GSM::L3Frame& l3)
{
	OBJLOG(DEBUG) <<"CBCHL2 incoming L3 frame: " << l3;
	assert(mL2Downstream);
	assert(l3.primitive()==L3_UNIT_DATA);
	assert(l3.size()==88*8);
	L2Frame outFrame(L2_DATA);		// (pat) TODO: get rid of the redundant idle fill in the constructor here.
	// Chop the L3 frame into 4 L2 frames.
	for (unsigned i=0; i<4; i++) {
		outFrame.fillField(0,0x02,4);
		outFrame.fillField(4,i,4);
		const BitVector2 thisSeg = l3.cloneSegment(i*22*8,22*8);
		thisSeg.copyToSegment(outFrame,8);
		OBJLOG(DEBUG) << "CBCHL2 outgoing L2 frame: " << outFrame;
		//mL2Downstream->sapWriteHighSide(outFrame);
		mL2Downstream->writeToL1(outFrame);
	}
}


void L2LAPDm::text(std::ostream&os) const
{
	os  <<" LAPDm(" <<myid
		<<'('<<((void*)this)<<')'
		<<LOGVARM(mSAPI)<<LOGVARM(mState);
	if (mState != LAPDStateUnused) {
		os  <<LOGVARM(mC)<<LOGVARM(mR)<<LOGVARM(mVS)<<LOGVARM(mVA)<<LOGVARM(mVR)
			<<LOGVARM(mRC)<<LOGVARM(mEstablishmentInProgress)
			<<LOGVARM(mL1In.size());	//<<LOGVARM(mL3Out.size());
		if (mMaster) {
			os <<LOGVAR2("master.State",mMaster->mState);
		}
	}
	os <<")";
}

ostream& GSM::operator<<(ostream& os, L2LAPDm& thing)
{
	thing.text(os);
	return os;
}

ostream& GSM::operator<<(ostream& os, L2LAPDm* thing)
{
	thing->text(os);
	return os;
}



// vim: ts=4 sw=4
