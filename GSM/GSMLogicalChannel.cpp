/**@file Logical Channel.  */

/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011, 2014 Range Networks, Inc.
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

// 6-2014: Pat Thompson heavily rewrote this.

#define LOG_GROUP LogGroup::GSM		// Can set Log.Level.GSM for debugging


// Pretty picture courtesy pat.
//
// +---------------------------------+
// |                                 |
// |           Layer3                |
// |                                 |
// +---------------------------------+
//        ^            |
//        |            |
//        |            v
// +---------------------------------+    +------------------------------------+    +----------------------------+
// |      L2LogicalChannel           |    |             SAPMux                 |    |       L2LAPDm/SAP0         |
// |   l2recv       l2sendf/m/p      |    |                                    |    |                            |
// |     |             |             |    |                                    |    |                            |
// |     |             v             |    |                                    |    |                            |
// | Queue mL3Out    Queue mL3In     |    |                                    |    |                            |
// |     ^             |             |    |                                    |    |                            |
// |     |             v             |    |                                    |    |                            |
// |     |       MessageServiceLoop->|--->| sapWriteFromL3 ------>X----------->|--->| l2dlWriteHighSide          |
// |     \                           |    |                       |            |    |                            |
// |      --------------- writeToL3  |<--------------------------------X<-----------| writeL3                    |
// |                                 |    |                       |    ^       |    |                            |
// |                                 |    |                       |    |       |    |                            |
// |       ------------------------->|--->| sapWriteFromL1 -->X--------------->|--->| l2dlWriteLowSide           |
// |      /                          |    |                   |   |    |       |    |          -> Queue mL1In    |
// |      |             --writeToL1  |<------------------X<-------------------------| writeL1                    |
// |      |            /             |    |              ^    |   |    |       |    +----------------------------+
// |      |            |             |    |              |    |   |    |       |                                  
// |      |            |             |    |              |    |   |    |       |    +----------------------------+
// |      |            |             |    |              |    |   |    |       |    |       L2LAPDm/SAP3         |
// |      |            |             |    |              |    |   \    |       |    |                            |
// |      |            |             |    |              |    |    ----------->|--->| l2dlWriteHighSide          |
// |      |            |             |    |              |    |        \       |    |                            |
// |      |            |             |    |              |    |         ------------| writeL3                    |
// |      |            |             |    |              |    |                |    |                            |
// |      |            |             |    |              |    \                |    |                            |
// |      |            |             |    |              |     --------------->|--->| l2dlWriteLowSide           |
// |      |            |             |    |              \                     |    |           -> Queue mL1In   |
// |      |            |             |    |               --------------------------| writeL1                    |
// |      |            |             |    |                                    |    +----------------------------+
// |   writeLowSide    v             |    |                                    |                                  
// +---------------------------------+    +------------------------------------+                                  
//        ^            |
//        |            |
//        |            v
// +---------------------------------+
// | handleGoodFrame  writeHighSide  |
// |         L1FEC                   |
// +---------------------------------+


#include "GSML3RRElements.h"
#include "GSML3Message.h"
#include "GSML3RRMessages.h"
#include "GSMSMSCBL3Messages.h"
#include "GSMLogicalChannel.h"
#include "GSMConfig.h"

#include <ControlTransfer.h>
#include <L3Handover.h>
#include "GPRSExport.h"
#include <Globals.h>

#include <Logger.h>


using namespace std;
namespace GSM {

// Tell C++ to put the class vtables here.
void L2LogicalChannel::_define_vtable() {}
void L2LogicalChannelBase::_define_vtable() {}
void SACCHLogicalChannel::_define_vtable() {}

// Comments from David Burgess from wki ticket 1141:
// We are not generating correct idle sequences in L1. Most handsets are not sensitive to this, but some are.
// Unfortunately, one of the most sensitive, the Nokia 1600, is common in some of our target markets.
// As far as I can tell, the correct idle behaviors are:
// for an unused channel on C0 - dummy burst
// for an unused channel on other Cn - dead air, non-transmitting
// for an open-but-idle SDCCH - LAPDm L2 idle frames (an empty unit data frame, see GSM 04.06)
// for an open-but-idle TCH+FACCH prior to activating vocoder - LAPDm L2 idle frames
// for an open-but-idle TCH+FACCH after activating vocoder - silent vocoder frames 

// Semaphore works now and reduces cpu% on idle SACCH from 0.3 to negligible, but the thread start/stop logic is pretty weird
// and I am not sure all the cases are handled, so dont enable this.
#define USE_SEMAPHORE 0

void L2LogicalChannelBase::startl1()
{
	LOG(DEBUG) <<descriptiveString();
	if (mL1) mL1->l1start();
}

void L2SAPMux::flushL3In()
{
	while (L3Frame *l3f = mL3In.readNoBlock()) {
		if (l3f->primitive() != L3_RELEASE_REQUEST && l3f->primitive() != L3_HARDRELEASE_REQUEST) {
			LOG(ERR)<< "channel closure caused message to be discarded:"<<l3f<<" "<<descriptiveString();
		}
		delete l3f;
	}
}

void L2SAPMux::l2stop()
{
	LOG(DEBUG) <<descriptiveString();
	mL1->l1close();
	// Clear the LAPDm input queue.
	flushL3In();
	// Put the message in the queue and let the service loop serialize the request.
	mL3In.write(new L3Frame(SAPI0,L3_HARDRELEASE_REQUEST));		// Reset LAPDm.
	mL3In.write(new L3Frame(SAPI3,L3_HARDRELEASE_REQUEST));
}

void L2SAPMux::sapStart()
{
	LOG(DEBUG) <<descriptiveString();
	devassert(mL1);
	startl1();
	for (int s=0; s<4; s++) {
		// (pat) This starts sending LAPDm idle frames.
		if (mL2[s]) mL2[s]->l2dlOpen(descriptiveString());
	}
}

void L2LogicalChannelBase::connect(L1FEC *wL1)
{
	mL1 = wL1;
	if (wL1) wL1->upstream(this);
}

void L2SAPMux::sapInit(L2DL *sap0, L2DL *sap3)
{
	LOG(DEBUG);
	mL2[0] = sap0; mL2[3] = sap3;
	for (int s=0; s<4; s++) {
		if (mL2[s]) { mL2[s]->l2Downstream(this); }
	}
	if (sap0 && sap3) {
		dynamic_cast<L2LAPDm*>(sap3)->master(dynamic_cast<L2LAPDm*>(sap0));
	}
}

void L2SAPMux::sapWriteFromL1(const L2Frame& frame)
{
	OBJLOG(DEBUG) << frame;
	unsigned sap;

	// (pat) Add switch to validate upstream primitives.  The upstream only generates a few primitives;
	// the rest are created in L2LAPDm.
	switch (frame.primitive()) {
		case L2_DATA:
			sap = frame.SAPI();	
			assert(sap == SAPI0 || sap == SAPI3);
			if (mL2[sap]) {
				mL2[sap]->l2dlWriteLowSide(frame);
			} else {
				LOG(WARNING) << "received DATA for unsupported"<<LOGVAR(sap);
			}
			return;
		case PH_CONNECT:
			// All this does is fully reset LAPDm; copy it out to every SAP.
			for (int s = 0; s <= 3; s++) {
				if (mL2[s]) mL2[s]->l2dlWriteLowSide(frame);	// Note: the frame may have the wrong SAP in it, but LAPDm doesnt care.
			}
			return;
		default:
			// If you get this assertion, make SURE you know what will happen upstream to that primitive.
			devassert(0);
			return;		// make g++ happy.
	}
}

bool L2SAPMux::multiframeMode(SAPI_t sap) const {
	unsigned sapi = SAP2SAPI(sap);
	assert(mL2[sapi]);
	return mL2[SAP2SAPI(sapi)]->multiframeMode();
}

bool L2LogicalChannel::multiframeMode(SAPI_t sap) const {
	if (SAPIsSacch(sap)) {
		return getSACCH()->L2SAPMux::multiframeMode(sap);
	} else {
		return L2SAPMux::multiframeMode(sap);
	}
}

LAPDState L2SAPMux::getLapdmState(SAPI_t sap) const { return mL2[SAP2SAPI(sap)] ? mL2[SAP2SAPI(sap)]->getLapdmState() : LAPDStateUnused; }

// For DCCH channels (FACCH, SACCH, SDCCH):
// This function calls virtual L2DL::l2dlWriteHighSide(L3Frame) which maps
// to L2LAPDm::l2dlWriteHighSide() which interprets the primitive, and then
// sends traffic data through sendUFrameUI(L3Frame) which creates an L2Frame
// and sends it through several irrelevant functions to L2LAPDm::writeL1
// which calls (SAPMux)mDownstream->SAPMux::writeHighSide(L2Frame),
// which does nothing but call mL1->writeHighSide(L2Frame), which is a pass-through
// except that the SapMux uses mDownStream which is copied from mL1, so there is a
// chance to redirect it.  But wouldn't that be an error?
// Anyway, L1Encoder::writeHighSide is usually overridden.
// For TCH, it goes to XCCHL1Encoder::writeHighSide() which processes
// the L2Frame primitive, then sends traffic data to TCHFACCHL1Encoder::sendFrame(),
// which just enqueues the frame - it does not block.
// A thread runs GSM::TCHFACCHL1EncoderRoutine() which
// calls TCHFACCHL1Encoder::dispatch() which is synchronized with the gBTS clock,
// unsynchronized with the queue, because it must send data no matter what.
// Eventually it encodes the data and
// calls (ARFCNManager*)mDownStream->writeHighSideTx(), which writes to the socket.
// 
// From here and below only SAPI0 and SAPI3 are used.
void L2SAPMux::sapWriteFromL3(const L3Frame& frame)
{
	LOG(DEBUG) <<LOGVAR(frame);
	SAPI_t sap = frame.getSAPI();
	if (!(sap == SAPI0 || sap == SAPI3 || sap == SAPI0Sacch || sap == SAPI3Sacch)) {
		devassert(0);
		sap = SAPI0;	// This is a bug fix to avoid a crash.
	}
	unsigned sapi = SAP2SAPI(sap);
	devassert(mL2[sapi]);
	if (!mL2[sapi]) { return; }	// Not initialized yet?  Should never happen.
	// On SACCH L3_UNIT_DATA frames block for 480ms.
	// L3_DATA frames could block for minutes.
	if (mL2[sapi]) mL2[sapi]->l2dlWriteHighSide(frame);
}

// This is the start of a normal or error-caused release procedure, both of which are identical.
// deactivate SACCH means stop transmitting LAPDm frames, so that the handset will time-out based on RADIO_LINK_TIMEOUT
// and release the channel at the RR level.
// On C0 that means to start transmitting dummy frames instead of LAPDm idle frames.
// If you want to stop SACCH immediately, call l2stop() directly so you dont start the mT3109 timer.
void L2LogicalChannel::startNormalRelease()
{
	LOG(DEBUG) <<this;
	if (!mT3109.active()) { mT3109.set(); }
	getSACCH()->l2stop(); // Go to LAPDm 'null' state immediately.
	// We stopped SACCH and are dropping the channel, so no more messages should be sent.
	// It is the responsibility of layer3 to make sure it does not happen from that direction,
	// and if it happens from the handset nothing we can do about it.
	flushL3In();
	mL3In.write(new L3Frame(SAPI0,L3_RELEASE_REQUEST));
	mL3In.write(new L3Frame(SAPI3,L3_RELEASE_REQUEST));
}

void L2LogicalChannel::l2sendf(const L3Frame& frame)
{
	SAPI_t sap = frame.getSAPI();
	assert(sap == SAPI0 || sap == SAPI3 || sap == SAPI0Sacch || sap == SAPI3Sacch);
	WATCHINFO("l2sendf "<<channelDescription() <<LOGVAR(sap) <<LOGVAR(chtype()) <<" " <<frame);
	if (SAPIsSacch(sap)) { getSACCH()->l2sendf(frame); return; }
	switch (frame.primitive()) {
		case L3_ESTABLISH_REQUEST:
			if (sap != SAPI3) { LOG(NOTICE) << "unexpected"<<LOGVAR(sap)<<" in "<<LOGVAR(frame); }
			break;
		case L3_DATA:
		case L3_UNIT_DATA:
			break;
		case L3_RELEASE_REQUEST:
			// Normal release initiated by layer3.
			if (sap == SAPI0) { 	// Total deactivation requested.
				startNormalRelease();
				return;
			}
			break;
		case L3_HARDRELEASE_REQUEST:
			// This is a full immediate release.
			if (sap == SAPI0) {
				immediateRelease();
				return;
			}
			LOG(NOTICE) << "unexpected"<<LOGVAR(sap)<<" in "<<LOGVAR(frame);
			break;
		default:
			assert(0);
			return;
	}
	mL3In.write(new L3Frame(frame));
}

void SACCHLogicalChannel::l2sendf(const L3Frame& frame)
{
	SAPI_t sap = frame.getSAPI();
	devassert(SAPIsSacch(sap));
	if (sap != SAPI3Sacch) { LOG(NOTICE) << "unexpected"<<LOGVAR(sap)<<" in "<<LOGVAR(frame); }
	LOG(INFO) <<channelDescription() <<LOGVAR(sap) <<LOGVAR(chtype()) <<" " <<frame;
	switch (frame.primitive()) {
		case L3_ESTABLISH_REQUEST:
			// Fall through.
		case L3_DATA:
		case L3_UNIT_DATA:
		case L3_RELEASE_REQUEST:
		case L3_HARDRELEASE_REQUEST:
			mL3In.write(new L3Frame(frame));
			return;
		default:
			assert(0);
	}
}

L3Frame * L2LogicalChannel::l2recv(unsigned timeout_ms)
{
	//LOG(DEBUG);
	L3Frame *result = mL3Out.read(timeout_ms);
	if (result) WATCHINFO("l2recv " << this <<LOGVAR2("sap",result->getSAPI()) <<LOGVAR(result));
	return result;
}

void L2LogicalChannel::writeToL3(L3Frame*frame)
{
	LOG(DEBUG) <<this <<LOGVAR(*frame);
	switch (frame->primitive()) {
		case L3_ESTABLISH_INDICATION:
		case L3_ESTABLISH_CONFIRM:
		case HANDOVER_ACCESS:
		case L3_DATA:
		case L3_UNIT_DATA:
			break;
		case MDL_ERROR_INDICATION:
			// Normal release procedure initiated by handset, or due to error at LAPDm level.
			// An error is handled identically to a normal release, because the handset may still be listening to us
			// even though we lost contact with it, and we want to tell it to release as gracefully as possible
			// even though the channel condition may suck.
			if (frame->getSAPI() == SAPI0) {
				// Release on host chan sap 0 is a total release.  We will start the release now.
				// FIXME: Are we supposed to wait for any pending SMS requests on SACCH to clear first?
				startNormalRelease();
			} else {
				// We dont kill the whole link for SAP3 release.
				// Pass the message on to layer3 to abort whatever transaction is running on SAP3
			}
			break;
		case L3_DATA_CONFIRM:			// Sent from LAPDm when data delivered, but we dont care.
			WATCHINFO(this <<LOGVAR2("sap",frame->getSAPI()) <<LOGVAR(frame));
			delete frame;
			return;
		case L3_RELEASE_INDICATION:
		case L3_RELEASE_CONFIRM:			// Sent from LAPDm when link release is confirmed, but we dont care.
			if (frame->getSAPI() == SAPI0) {
				mT3109.reset();
				mT3111.set();
			}
			break;
		default:
			assert(0);
	}
	mL3Out.write(frame);
}


void SACCHLogicalChannel::writeToL3(L3Frame*frame)
{
	switch (frame->primitive()) {
		case L3_DATA:
		case L3_UNIT_DATA:
			if (processMeasurementReport(frame)) { return; }
			// Fall Through...
		case L3_ESTABLISH_CONFIRM:
		case L3_ESTABLISH_INDICATION:
			// The uplink message queue resides in L2LogicalChannel
			mHost->writeToL3(frame);	// TODO: frame should include channel indicator, but l3 doesnt care.  Would be nice for debugging.
			return;
		case L3_RELEASE_INDICATION:
		case MDL_ERROR_INDICATION:
			frame->mSapi = (SAPI_t) (frame->mSapi | SAPChannelFlag);
			mHost->writeToL3(frame);	// FIXME: frame should include channel indicator.
			return;
		case L3_DATA_CONFIRM:			// Sent from LAPDm when data delivered, but we dont care.
		case L3_RELEASE_CONFIRM:		// Sent from LAPDm when link release is confirmed, but we dont care.
			delete frame;		// We dont do anything with this; we are releasing regardless of whether we get a confirm or not.
			return;
		default:
			assert(0);
	}
}


// FIXME: It blocks until L2LAPDm::sendIdle returns.  That does not need to block.
// Other channels call it too, which is somewhat nonsensical; the l2open for other channels is empty.
// There is no close in L2 - the L1 encoder/decoder are closed individually when L2 sends a RELEASE/HARDRELEASE primitive.
void L2LogicalChannel::lcstart()
{
	LOG(DEBUG) <<this;
	devassert(mSACCH);
	if (mSACCH) mSACCH->sapStart();
	sapStart();
	mL3Out.clear();
	mT3101.set(T3101ms);
	mT3109.reset(gConfig.GSM.Timer.T3109);	// redundant with init in lcinit but cant be too careful.
	mT3111.reset(T3111ms);					// redundant with init in lcinit but cant be too careful.
}


// (pat) For data channels this is called by getTCH or getSDCCH.
// TODO: Go through all the getTCH/getSDCCH users and make sure they lcstart.
void L2LogicalChannel::lcinit()
{
	LOG(DEBUG) <<this;
	assert(mL1);
	if (mL1) mL1->l1init();		// (pat) L1FEC::l1init()
	devassert(mSACCH);
	if (mSACCH) mSACCH->sacchInit();
	// We set T3101 now so the channel will become recyclable if the caller does nothing with it;
	// the caller has this long to call lcstart before the channel goes back to the recyclable pool.
	mT3101.set(T3101ms);	// It will be started again in l2start.
	mT3109.reset(gConfig.GSM.Timer.T3109);
	mT3111.reset(T3111ms);
	//mTRecycle.reset(500);	// (pat) Must set a dummy value.
	if (!mlcMessageLoopRunning) {
		mlcMessageLoopRunning=true;
		mlcMessageServiceThread.start2((void*(*)(void*))MessageServiceLoop,this,8000*sizeof(void*));
	}
	if (!mlcControlLoopRunning) {
		mlcControlLoopRunning=true;
		mlcControlServiceThread.start2((void*(*)(void*))ControlServiceLoop,this,8000*sizeof(void*));
	}
}

void L2LogicalChannel::lcopen()
{
	LOG(INFO) <<"open channel "<<this;
	lcinit();
	lcstart();
}


// old TODO: We want to call L2LAPDm::abnormalRelease but it blocks, so we really need to send that thread a message.

// GSM 5.08 6.7.1: The MS may monitor TCH-SACCH or SDCCH-SACCH instead of BCCH on surrounding cells
// to speed up cell reselection process.  That implies that the SACCH are running constantly!
// "The received signal level measurements on surrounding cells made during the last 5 seconds on the TCH
// or SDCCH may be averaged and used, where possible, to speed up the process. However, it should be
// noted that the received signal level monitoring while on the TCH or SDCCH is on carriers in BA
// (SACCH), while the carriers to be monitored for cell reselection are in BA (BCCH) or BA (GPRS)."


bool L2LogicalChannel::recyclable()
{
	//return mTRecycle.expired();
	return (mT3101.expired() || mT3109.expired() || mT3111.expired()) && mL1->encoder()->l1IsIdle() && getSACCH()->mL1->encoder()->l1IsIdle();
}

// We know this channel is now unused.  Finish deactivating it and mark it for reuse.
// The SACCH was already deactivated.  Just close the main channel and become recyclable.
void L2LogicalChannel::immediateRelease()
{
	LOG(DEBUG) <<this;
	mT3101.reset();
	mT3109.reset();
	mT3111.expire();	// make sure the channel becomes recyclable
#if 0 // fixed a better way
	// (pat) When we release the channel the LAPDm state machine sends a dummy frame on the channel.
	// We dont want to try to reuse the channel until that clears.
	// FIXME: Do this better - go into the encoder and save the time when dummy frame is sent.
	if (getSACCH()->l1active()) {
		// We dont need the delay if this is a normal release, meaning the sacch was deactivated a long time ago.
		mTRecycle.set(500);	// (pat) Channel will be recyclable when this expires.  SACCH is 480ms.  Could actually be up to 1 second.
	} else {
		mTRecycle.expire();	// Recycle now.
	}
#endif
	getSACCH()->l2stop(); // Done already in the T3109 or T3111 expiry cases.
	this->l2stop();
}

// Service the main SDCCH or TCH/FACCH L2LogicalChannel.
// We are just looking for released channels, which is timer based and so has to be checked from a service thread.
// Called from the service loop for the SACCH, because that is where the thread lives.
// WARNING: Runs in a different thread than everything else which runs in the LAPDm service handler thread.
void L2LogicalChannel::serviceHost()
{
	LOG(DEBUG);
	// We do not test T3101 on SACCH because sometimes the first measurement report does not
	// arrive in time to keep T3101 from expiring there.
	if (mT3101.expired()) {
		// Failure of MS to seize channel.
		// Layer3 has not been started, so all we do is close down LAPDm immediately.
		LOG(INFO) <<this <<" channel closed on T3101 expiry";
		immediateRelease();
	} else if (mT3109.expired()) {
		// The handset never responded to the LAPDm DISC, but we have waited long enough for it to disconnect.
		LOG(INFO) <<this <<" channel closed on T3109 expiry";
		immediateRelease();
	} else if (mT3111.expired()) {
		// Happiest outcome.  The LAPDm disconnected with a full disconnect handshake.
		LOG(INFO) <<this <<" channel closed on T3109 expiry";
		immediateRelease();
	}
}

void *L2LogicalChannel::ControlServiceLoop(L2LogicalChannel* hostchan)
{
	while (!gBTS.btsShutdown()) {
		sleepFrames(26);	// We dont have to do this very often.
		if (hostchan->l1active()) {
			hostchan->serviceHost();
		}

		// The SACCH is deactivated before the host channel, so we check l1active to see if SACCH is still running.
		SACCHLogicalChannel *sacch = hostchan->getSACCH();
		if (sacch->l1active() && sacch->sacchRadioFailure()) {
			// GSM 4.08 3.4.13.2:  layer2 is supposed to inform layer3 directly, bypassing LAPDm.
			// The layer3 response is identical to a normal RELEASE from layer3: deactivate the SACCH,
			// start T3109, recycle the channel when it expires.
			// Just because we cannot hear the MS on SACCH does not mean that it cannot hear it, or that
			// we have completely lost contact, so LAPDm can go ahead with the normal release procedure,
			// ie, send a DISC on the main link and wait for a response - if we get it we can use T3111.
			hostchan->startNormalRelease();
		}
	}
	//hostchan->mlcControlLoopRunning=false;
	return NULL;
}

// This drives messages from layer3 down through LAPDm, layer1, and all the way to the radio.
void *L2LogicalChannel::MessageServiceLoop(L2LogicalChannel* hostchan)
{
	WATCHINFO("Starting MessageServiceLoop for "<<hostchan);
	while (!gBTS.btsShutdown()) {
		L3Frame *l3fp = hostchan->mL3In.read(52*4);	// Add a delay so will exit at BTS shutdown.
		// (pat) The frames may be L3_DATA which will block until delivered, which could take minutes,
		// which is why we need a separate thread to drive this.
		if (l3fp) {
			hostchan->sapWriteFromL3(*l3fp);
			delete l3fp;
		}
	}
	//hostchan->mlcMessageLoopRunning = false;
	return NULL;
}

// Return true if the phy link is either off or failed.  Those are the conditions under which Layer3 should punt.
// GSM 5.08 5.3: Radio link failure in the BSS is based on the error rate in the uplink SACCH or
// RXLEV/RXQUAL measurements of the MS.  We use only the former.
bool L2LogicalChannel::radioFailure() const {
	return !l1active() || getSACCH()->sacchRadioFailure();
}

// (pat) This is the primary way of detecting loss of contact with a handset.
bool SACCHLogicalChannel::sacchRadioFailure() const
{
	return mSACCHL1->decoder()->mBadFrameTracker > gConfig.GSM.BTS.RADIO_LINK_TIMEOUT;
}


void L2LogicalChannelBase::downstream(ARFCNManager* radio)
{
	assert(mL1);	// This is L1FEC
	mL1->downstream(radio);
}


// (pat) This is only called during initialization, using the createCombination*() functions.
// The L1FEC->downstream hooks the radio to this logical channel, permanently.
void L2LogicalChannel::downstream(ARFCNManager* radio)
{
	mL1->downstream(radio);
	if (mSACCH) mSACCH->mL1->downstream(radio);
}


// Serialize and send an L3Message with a given primitive.
// The msg is not deleted; its value is used before return.
void L2LogicalChannelBase::l2sendm(const L3Message& msg, GSM::Primitive prim, SAPI_t SAPI)
{
	//OBJLOG(INFO) << "L3" <<LOGVAR(SAPI) << " sending " << msg;
	WATCHINFO("l2sendm "<<this <<LOGVAR(SAPI) << " sending " << msg);
	l2sendf(L3Frame(msg,prim,SAPI));
}

void L2LogicalChannel::writeLowSide(const L2Frame& frame)
{
	// If this is the first good frame of a new transaction,
	// stop T3101 and tell L2 we're alive down here.
	if (mT3101.active()) {
		mT3101.reset();
		// Inform L3 that we are alive down here.
		// This does not block; goes to a InterthreadQueue L2LAPdm::mL1In
		sapWriteFromL1(L2Frame(PH_CONNECT));
	}
	switch (frame.primitive()) {
		case HANDOVER_ACCESS:	// Only send this on SAPI 0.
			writeToL3(new L3Frame(SAPI0,HANDOVER_ACCESS));
			break;
		default:
			sapWriteFromL1(frame);
			break;
	}
}


// This frame comes down from L2LAPDm::writeL1.
// RELEASE:  Normal release request from Layer3, including imsi detach, or lost radio contact.
//		deactivate SACCH, leave main L2LogicalChannel going, wait T3109
//		You would think that if layer3 sent an l3 channel release, we could release the link sooner,
//		but there is no guarantee that l3 message got through yet, so we have to wait for the L2 LAPDm DISC indication
//		before we can switch to T3111.
//		GSM 4.08 3.4.13.1.1 NOTE 1: Layer3 Channel Release on the main signaling link causes all other signaling links
//		to be terminated by "local end release" which 4.06 5.4.4 defines as immediately entering idle state, wherein
//		LAPDm can respond to handshakes but send nothing new.
//		Also 24.011 (SMS) 2.3: SAP3 may be released explicitly (LAPDm DISC) or implicitly by channel release.
//		I infer, therefore, that is the job of Layer3 to make that sure that the other links (main-SAP3 and SACCH-SAP0,SAP3) are 
//		released before the main link, in other words, to make sure SMS or RR (eg, RRLP) messages are finished.
//		Question: is it necessary to send RELEASE primitives on those other links to terminate multi-frame-mode?
//		I would think so, because otherwise the handset could initiate a new multi-frame-mode command while the ChannelRelease
//		from the BTS is in flight on the main link.
//		btw, SACCH-SAP3 may be is used for SMS, but I dont know what SACCH-SAP0 would ever be used for.
// HARDRELEASE:
//		Handover failure, or post-successful channel reassignment.
void L2LogicalChannel::writeToL1(const L2Frame& frame)
{
	// The SAP may or may not be present, depending on the channel type.
	OBJLOG(DEBUG) << frame;
	switch (frame.primitive()) {
		case L2_DATA:
			mL1->writeHighSide(frame);
			break;
		default:
			OBJLOG(ERR) << "unhandled primitive " << frame.primitive() << " in L2->L1";
			devassert(0);
	}
}

void SACCHLogicalChannel::writeToL1(const L2Frame& frame)
{
	// The SAP may or may not be present, depending on the channel type.
	OBJLOG(DEBUG) << frame;
	switch (frame.primitive()) {
		case L2_DATA:
			mL1->writeHighSide(frame);
			break;
		default:
			OBJLOG(ERR) << "unhandled primitive " << frame.primitive() << " in L2->L1";
			devassert(0);
	}
}


L3ChannelDescription L2LogicalChannelBase::channelDescription() const
{
	// In some debug cases, L1 may not exist, so we fake this information.
	if (mL1==NULL) return L3ChannelDescription(TDMA_MISC,0,0,0);

	// In normal cases, we get this information from L1.
	return L3ChannelDescription(
		mL1->typeAndOffset(),
		mL1->TN(),
		mL1->TSC(),
		mL1->ARFCN()
	);
}

ChannelHistory *L2LogicalChannel::getChannelHistory()
{
	return mSACCH ? mSACCH->getChannelHistory() : NULL;
}

string L2LogicalChannel::displayTimers() const
{
	ostringstream ss;
	ss <<LOGVARM(mT3101) <<LOGVARM(mT3109) <<LOGVARM(mT3111);
	if (mSACCH) {
		// The only thing we want to display from L1 is the bad-frame-tracker variable, which is only useful in SACCH.
		ss <<mSACCH->mL1->displayTimers();
	}
	return ss.str();
}


SDCCHLogicalChannel::SDCCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const CompleteMapping& wMapping)
{
	mL1 = new SDCCHL1FEC(wCN,wTN,wMapping.LCH());
	// SAP0 is RR/MM/CC, SAP3 is SMS
	// SAP1 and SAP2 are not used.
	L2LAPDm *sap0 = new SDCCHL2(1,SAPI0);		// derived from L2LAPDm
	L2LAPDm *sap3 = new SDCCHL2(1,SAPI3);
	LOG(DEBUG) << "LAPDm pairs SAP0=" << sap0 << " SAP3=" << sap3;
	sapInit(sap0,sap3);
	mSACCH = new SACCHLogicalChannel(wCN,wTN,wMapping.SACCH(),this);
	connect(mL1);
}


SACCHLogicalChannel::SACCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const MappingPair& wMapping,
		/*const*/ L2LogicalChannel *wHost)
		: mHost(wHost)
{
	mSACCHL1 = new SACCHL1FEC(wCN,wTN,wMapping);
	mL1 = mSACCHL1;
	// SAP0 is RR, SAP3 is SMS
	// SAP1 and SAP2 are not used.
	L2LAPDm *sap0 = new SACCHL2(1,SAPI0);	// derived from L2LAPDm
	L2LAPDm *sap3 = new SACCHL2(1,SAPI3);
	sapInit(sap0,sap3);
	connect(mL1);
	//assert(mSACCH==NULL);
#if USE_SEMAPHORE
	int sval, semstat= sem_getvalue(&mOpenSignal,&sval);
	LOG(DEBUG) << descriptiveString() << " SEM_INIT " <<LOGVAR(semstat) <<LOGVAR(sval);
	sem_init(&mOpenSignal,0,0);
#endif
}


void SACCHLogicalChannel::sacchInit()
{
	LOG(DEBUG) <<this;
	if (mL1) mL1->l1init();		// (pat) L1FEC::l1init()
	neighborClearMeasurements();
	//mAverageRXLEV_SUB_SERVICING_CELL = 0;
	// Just make sure any stray messages are flushed when we reactivate the channel.
	while (L3Message *straymsg = mTxQueue.readNoBlock()) { delete straymsg; }
	mMeasurementResults = L3MeasurementResults();	// clear it
#if USE_SEMAPHORE
	//cout << descriptiveString() << " POST " <<sem_getvalue(&mOpenSignal,&sval) <<LOGVAR(sval) <<endl;
	int sval, semstat= sem_getvalue(&mOpenSignal,&sval);
	LOG(DEBUG) << descriptiveString() << " SEM_POST " <<LOGVAR(semstat) <<LOGVAR(sval);
	sem_post(&mOpenSignal);	// Note: you must open the L2LogicalChannel before starting the SACCH service loop.
#endif
	if (!mSacchRunning) {
		mSacchRunning=true;
		// This thread pushes data through lapdm and all the way to L1Encoder.
		mSacchServiceThread.start2((void*(*)(void*))SACCHServiceLoop,this,12000*sizeof(void*));
	}
	LOG(DEBUG);
}

static L3Message* parseSACCHMessage(const L3Frame *l3frame)
{
	if (!l3frame) return NULL;
	LOG(DEBUG) << *l3frame;
	Primitive prim = l3frame->primitive();
	if ((prim!=L3_DATA) && (prim!=L3_UNIT_DATA)) {
		LOG(INFO) << "non-data primitive " << prim;
		return NULL;
	}
	L3Message* message = parseL3(*l3frame);
	if (!message) {
		LOG(WARNING) << "SACCH received unparsable L3 frame " << *l3frame;
		WATCHF("SACCH received unparsable L3 frame PD=%d MTI=%d",l3frame->PD(),l3frame->MTI());
	}
	return message;
}

bool SACCHLogicalChannel::processMeasurementReport(L3Frame *rrFrame)
{
	if (! (rrFrame->isData() && rrFrame->PD() == L3RadioResourcePD && rrFrame->MTI() == L3RRMessage::MeasurementReport)) { return false; }

	// Neither of these 'ifs' should fail, but be safe.
	if (const L3Message* rrMessage = parseSACCHMessage(rrFrame)) {
		if (const L3MeasurementReport* measurement = dynamic_cast<typeof(measurement)>(rrMessage)) {
			OBJLOG(INFO) << "SACCH measurement report " <<this <<" "<< mMeasurementResults;
			mMeasurementResults = measurement->results();
			//if (mMeasurementResults.MEAS_VALID() == 0) {
			//	addSelfRxLev(mMeasurementResults.RXLEV_SUB_SERVING_CELL_dBm());
			//}
			// Add the measurement results to the sql table (pat - no longer used)
			// Note that the typeAndOffset of a SACCH match the host channel.
			gPhysStatus.setPhysical(this, mMeasurementResults);
			// Check for handover requirement.
			// (pat) TODO: This may block while waiting for a reply from a Peer BTS.
			Control::HandoverDetermination(&mMeasurementResults,this);
		}
		delete rrMessage;
	}
	delete rrFrame;
	return true;
}

// This sends Layer3 messages into the high side of LAPDm.
// This blocks until a message is sent.
// Routine relies on l2sendf passing directly through LAPDm and going all the way to L1Encoder::transmit, which calls waitToSend.
void SACCHLogicalChannel::serviceSACCH(unsigned &count)
{
	LOG(DEBUG);
	// Send any outbound messages.  If the tx queue is empty send alternating SI5/6.
	if (L3Frame *l3fp = mL3In.readNoBlock()) {
		// We are writing on SAPI0 (instead of SAPI0Sacch), which is ok.  At the SAP layer it is just SAPI0 or SAPI3
		sapWriteFromL3(*l3fp);
		delete l3fp;
	} else {
		// Send alternating SI5/SI6.
		// These L3Frames were created with the UNIT_DATA primivitive.
		// (pat) blocks using waitToSend until L1Encoder::mPrevWriteTime
		OBJLOG(DEBUG) << "sending SI5/6 on SACCH";
		if (count%2) sapWriteFromL3(gBTS.SI5Frame());
		else sapWriteFromL3(gBTS.SI6Frame());
		count++;
	}
	
	// RSSIBumpDown moved to SACCHL1Decoder::countBadFrame();
}


// (pat) This is started when SACCH is opened, and runs forever.
// The SACCHLogicalChannel are created by the SDCCHLogicalChannel and TCHFACCHLogicalChannel constructors.
void *SACCHLogicalChannel::SACCHServiceLoop(SACCHLogicalChannel* sacch)
{
	WATCHINFO("Starting SACCHServiceLoop for "<<sacch);
	unsigned count = 0;
	while (!gBTS.btsShutdown()) {
		//if (gBTS.time().FN() % 104 == 0) { OBJLOG(DEBUG) <<LOGVAR(l1active()) <<LOGVAR(recyclable()); }

		// Throttle back if not active.
		// This is equivalent to testing if the L2LAPDm is in 'null' state as defined in GSM 4.06.
		// In null state we send dummy frames; in L2LAPDm idle state we send L2 idle frames.
		// The dummy frame is sent down to the transceiver when L2LAPDm is closed.
		if (! sacch->l1active()) {
			// pat 5-2013: Vastly reducing the delays here and in L2LAPDm to try to reduce
			// random failures of handover and channel reassignment from SDCCH to TCHF.
			// Update: The further this sleep is reduced, the more reliable handover becomes.
			// I left it at 4 for a while but handover still failed sometimes.
			//sleepFrames(51);
#if USE_SEMAPHORE
			sleepFrames(51);	// In case the semaphore does not work.
			// (pat) Update: Getting rid of the sleep entirely.  We will use a semaphore instead.
			// Note that the semaphore call may return on signal, which is ok here.
			int sval, semstat= sem_getvalue(&mOpenSignal,&sval);
			//cout << descriptiveString() << " WAIT " <<sem_getvalue(&mOpenSignal,&sval) <<LOGVAR(sval) <<endl;
			LOG(DEBUG) << descriptiveString() << " SEM_WAIT " <<LOGVAR(semstat) <<LOGVAR(sval);
			sem_wait(&mOpenSignal);
			// sem_post sem_overview
			semstat= sem_getvalue(&mOpenSignal,&sval);
			//cout << descriptiveString() << " AFTER " <<LOGVAR(semstat) <<LOGVAR(sval) <<endl;
			LOG(DEBUG) << descriptiveString() << " SEM_AFTER " <<sem_getvalue(&mOpenSignal,&sval) <<LOGVAR(sval);
#else
			// (pat 3-2014) Changing this sleepFrames(51) to sleepFrames(2) had an enormous impact
			// on idle cpu utilization, from 11.2% to 8.5% with a C-5 beacon, ie, with only 4 SACCH running.
			// TODO: Can use fewer CPU cycles by using something like waitToSend to sleep until the next SACCH transmission time.
			sleepFrames(1);
#endif
			// A clever way to avoid the sleep above would be to wait for ESTABLISH primitive.
			// (But which do you wait on - the tx or the rx queue?
			continue;	// paranoid, check again.
		}

		// (pat 4-2014) Added to detect RR failure.  This is needed because a Layer3 MMContext is not allocated
		// until the first L3 message arrives.  If the channel fails without sending an L3 message we would never
		// notice unless we are watching these RR level timers.
		// Close the host channel, which will also close this SACCH.
		sacch->serviceSACCH(count);
	}
	//sacch->mSacchRunning = false;		dont think we would restart this one; would alloc a new one.
	return NULL;
}


// These have to go into the .cpp file to prevent an illegal forward reference.
void L2LogicalChannel::l1InitPhy(float wRSSI, float wTimingError, double wTimestamp)
	{ assert(mSACCH); mSACCH->l1InitPhy(wRSSI,wTimingError,wTimestamp); }
void L2LogicalChannel::setPhy(const L2LogicalChannel& other)
	{ assert(mSACCH); mSACCH->setPhy(*other.mSACCH); }
MSPhysReportInfo * L2LogicalChannel::getPhysInfo() const {
	assert(mSACCH); return mSACCH->getPhysInfo();
}
const L3MeasurementResults& L2LogicalChannel::measurementResults() const
	{ assert(mSACCH); return mSACCH->measurementResults(); }



TCHFACCHLogicalChannel::TCHFACCHLogicalChannel(
		unsigned wCN,
		unsigned wTN,
		const CompleteMapping& wMapping)
{
	mTCHL1 = new TCHFACCHL1FEC(wCN,wTN,wMapping.LCH());
	mL1 = mTCHL1;
	// SAP0 is RR/MM/CC, SAP3 is SMS
	// SAP1 and SAP2 are not used.
	L2LAPDm *sap0 = new FACCHL2(1,SAPI0);
	L2LAPDm *sap3 = new FACCHL2(1,SAPI3);
	sapInit(sap0,sap3);
	mSACCH = new SACCHLogicalChannel(wCN,wTN,wMapping.SACCH(),this);
	connect(mL1);
}




CBCHLogicalChannel::CBCHLogicalChannel(int wCN, int wTN, const CompleteMapping& wMapping)
{
	mL1 = new CBCHL1FEC(wCN, wTN, wMapping.LCH());
	L2DL *sap0 = new CBCHL2;
	sapInit(sap0,NULL);
	mSACCH = new SACCHLogicalChannel(wCN,wTN,wMapping.SACCH(),this);
	connect(mL1);
}


void CBCHLogicalChannel::l2sendm(const L3SMSCBMessage& msg)
{
	L3Frame frame(L3_UNIT_DATA,88*8);
	msg.write(frame);
	l2sendf(frame);
}

void CBCHLogicalChannel::l2sendf(const L3Frame& frame)
{
	if (mL2[0]) {	// Should always be set, but protects against a race during startup.
		mL2[0]->l2dlWriteHighSide(frame);
	}
}

void CBCHLogicalChannel::cbchOpen()
{
	if (mL1) mL1->l1init();		// (pat) L1FEC::l1init()
	sapStart(); // startl1();
	devassert(mSACCH);
	if (mSACCH) {
		mSACCH->sacchInit();
		mSACCH->sapStart();
	}
}

ostream& operator<<(ostream& os, const L2LogicalChannelBase& chan)
{
	os << chan.descriptiveString();
	return os;
}

std::ostream& operator<<(std::ostream&os, const L2LogicalChannelBase*ch)
{
	if (ch) { os <<*ch; } else { os << "(null L2Logicalchannel)"; }
	return os;
}
ostream& operator<<(ostream& os, const L2LogicalChannel& chan) { return operator<<(os,(L2LogicalChannelBase&)chan); }
ostream& operator<<(ostream& os, const L2LogicalChannel* chan) { return operator<<(os,(L2LogicalChannelBase*)chan); }

};


// vim: ts=4 sw=4

