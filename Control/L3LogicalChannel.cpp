/*
* Copyright 2013, 2014 Range Networks, Inc.
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
// Written by Pat Thompson

#define LOG_GROUP LogGroup::Control		// Can set Log.Level.Control for debugging

#include "L3LogicalChannel.h"
#include "L3MMLayer.h"
#include <GSMLogicalChannel.h>	// Needed for getL2Channel()
#include <GSMConfig.h>			// For gBTS

namespace Control {
using namespace GSM;

void L3LogicalChannel::L3LogicalChannelReset()
{
	LOG(DEBUG) << this;
	ScopedLock lock(gMMLock,__FILE__,__LINE__);	// FIXMENOW Added 10-23-2013
	// We could reset mNextChan too, but it is unused unless needed so dont bother.
	chanFreeContext(TermCause::Local(L3Cause::No_Transaction_Expected));	// If we do cancel any dialogs, it is in error.
	LOG(DEBUG);
	if (mNextChan && mNextChan->mChState == chReassignTarget) {
		// This rare case may occur for channel loss or if the MS sends, for example, an IMSI Detach
		// during the channel reassignment procedure.
		LOG(INFO) << "lost contact with MS during reassignment procedure "<<this;	// Or some other error.
		// TODO:
		// This state indicates the other channel is idle, but it is dangerous to free
		// it from here because it could receive a primitive and become active at any time.
		// If that happens it will get a new MMContext and try to fire up, not sure what happens then.
		mNextChan->chanFreeContext(TermCause::Local(L3Cause::No_Transaction_Expected));
		mNextChan = NULL;
	}
	LOG(DEBUG);
}

void L3LogicalChannel::L3LogicalChannelInit()
{
	// We must NOT reset mNextChan,mPrevChan as part of the LogicalChannelReset because these must
	// survive the establishment and release of channels during the reassignment procedure.
	// We dont use them for anything else, so it is ok to end with them set.
	mNextChan = NULL;
	//mPrevChan = NULL;
	mChState = chIdle;
	mChContext = NULL;
	L3LogicalChannelReset();
}

L3LogicalChannel::~L3LogicalChannel()
{
	chanFreeContext(TermCause::Local(L3Cause::No_Transaction_Expected));
}

// This virtual method should never be called; it is over-ridden by the sub-class for all channel types that matter.
const char * L3LogicalChannel::descriptiveString() const {
	return "undefined";
}

L2LogicalChannel * L3LogicalChannel::getL2Channel() {
	// We dont need a dynamic cast since L2 and L3 LogicalChannel are always allocated together.
	// But UMTS may change that.
	return dynamic_cast<L2LogicalChannel*>(this);
}

// TODO: This should probably be removed and the few uses replaced by specific functions,
// like getChannelDescription and getSACCH.
const L2LogicalChannel * L3LogicalChannel::getL2Channel() const {
	return dynamic_cast<const L2LogicalChannel*>(this);
}

L3LogicalChannel *L3LogicalChannel::getSACCHL3() {
	return dynamic_cast<L3LogicalChannel*>(this->getL2Channel()->getSACCH());
}

void L3LogicalChannel::l3sendm(const GSM::L3Message& msg, const GSM::Primitive& prim/*=GSM::DATA*/, SAPI_t SAPI/*=0*/)
{
	WATCHINFO("l3sendm"<<LOGVAR(prim)<<LOGVAR(SAPI)<<LOGVARP(msg)<<" "<<this);	// 'this' is the descriptive string of the channel.
	l2sendm(msg,prim,SAPI);
}

// These days this is used only for the handover command, which was sent as a pre-formed L3-message from BTS2 to BTS1.
// Note that SAP is encoded in the L3Frame.
void L3LogicalChannel::l3sendf(const GSM::L3Frame& frame)
{
	// 3-14-2014 pat: Changed the LOG levels, formerly we sent the frame to INFO and the message to DEBUG, but the frame is raw, so I reversed it.
	LOG(DEBUG) <<this <<LOGVARP(frame);
	if (IS_LOG_LEVEL(INFO)) {
		if (const L3Message *msg = parseL3(frame)) {
			WATCHINFO(this <<" sendf "<<*msg);
			delete msg;
		}
	}
	l2sendf(frame);
}

// WARNING: If you send a RELEASE and the channel is not responding, this blocks for 30 seconds.
// We only call this from the thread service loop, so it is the last thing we ever do in the thread.
void L3LogicalChannel::l3sendp(const GSM::Primitive& prim, SAPI_t SAPI)
{
	WATCHINFO("l3sendp"<<LOGVAR(prim)<<LOGVAR(SAPI)<<" "<<this);	// 'this' is the descriptive string of the channel.
	//if (prim == RELEASE || prim == HARDRELEASE) {
		//chanSetState(chReleased);	// Inform the service loop it should exit.
	//}
	l2sendp(prim,SAPI);
};


L3Frame* L3LogicalChannel::waitForEstablishOrHandover()
{
	while (true) {
		L3Frame *req = l2recv();
		LOG(DEBUG) <<LOGVAR(req);
		if (req==NULL) continue;
		if (req->primitive()==L3_ESTABLISH_INDICATION) return req;
		if (req->primitive()==HANDOVER_ACCESS) return req;
		LOG(INFO) << "L3LogicalChannel: Ignored primitive:"<<req->primitive();
		delete req;
	}
	return NULL;	// to keep the compiler happy
}

MMContext *L3LogicalChannel::chanGetContext(bool create)
{
	//LOG(DEBUG);
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	//LOG(DEBUG);
	if (mChContext == NULL) {
		if (create) { mChContext = new MMContext(this); }
	}
	return mChContext;
}

void L3LogicalChannel::chanSetHandoverPenalty(NeighborPenalty &penalty)
{
	chanGetContext(false)->chanSetHandoverPenalty(penalty);
}

// WARNING: This is called from the CLI thread.
string L3LogicalChannel::chanGetImsi(bool verbose) const
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	return mChContext ? mChContext->mmGetImsi(verbose) : string(verbose ? "no-MMChannel" : "");
}

// WARNING: This is called from the CLI thread.
time_t L3LogicalChannel::chanGetDuration() const
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	return mChContext ? mChContext->mmcDuration() : 0;
}

//void L3LogicalChannel::chanSetContext(MMContext* wContext)
//{
//	chanFreeContext();
//	mChContext = wContext;
//	mChContext->mmSetChannel(this);
//}

// The sipcode would be used if a SipDialog on this channel is still active, which indicates a channel loss failure
// or server error.
void L3LogicalChannel::chanFreeContext(TermCause cause)
{
	LOG(DEBUG);
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	LOG(DEBUG);
	MMContext *save = mChContext;
	mChContext = NULL;
	if (save) {
		LOG(DEBUG) <<this;
		gMMLayer.mmFreeContext(save,cause);
	}
	LOG(DEBUG);
}

// See 44.018 3.1.4: "Change of Dedicated Channels"
bool L3LogicalChannel::reassignAllocNextTCH()		// For a channel reassignment procedure.
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	GSM::TCHFACCHLogicalChannel *tch = gBTS.getTCH();
	if (tch==NULL) {
		LOG(DEBUG) << LOGVAR2("curchan",this)<<LOGVAR2("nextchan","null,congestion");
		return false;
	}
	// Copy the phy params from old to new channel, then fire it up.
	tch->setPhy(*getL2Channel());
	tch->lcstart();

	LOG(DEBUG) << LOGVAR2("curchan",this)<<LOGVAR2("nextchan",tch);
	// When we receive confirmation from the MS, mNextChannel will become mChannel.
	mNextChan = dynamic_cast<L3LogicalChannel*>(tch);
	// (pat) TODO: If not VEA, we should try doing the tch->open() here to see if it reduces the number
	// of channel reassignment failures.
	return true;
}

// This is run on the old channel.
void L3LogicalChannel::reassignStart()
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	LOG(DEBUG) << this << LOGVAR(mNextChan);
	// The current channel is the SDCCH, and mNextChan is the allocated TCH the MS will use next.
	assert(mNextChan);		// reassignmentAllocNextTCH was called first.

	// We set this state so when the LAPDm RELEASE arrives on this channel we dont kill everything off,
	// which is the normal reaction to a RELEASE.
	// Update: now we use the MMContext mmcChannelUseCnt.
	//chanSetState(L3LogicalChannel::chReassignPending);

	//mNextChan->chanSetContext();  Dont call this yet.  It changes the channel back pointer.
	if (mNextChan->mChState != chIdle) {
		LOG(ERR) <<"At start of channel reassignment target channel is not idle:"
			<<LOGVAR2("next-chan",mNextChan) <<LOGVAR2("prev-chan",this);
	}
	mNextChan->chanFreeContext(TermCause::Local(L3Cause::No_Transaction_Expected));	// This is supposed to be a no-op.
	mNextChan->mChContext = mChContext->tsDup();	// Must set directly.  Does not change the channel back pointer.
	// We set this state on nextChan in case of channel loss - see L3LogicalChannelReset
	mNextChan->chanSetState(L3LogicalChannel::chReassignTarget);

	GSM::L2LogicalChannel *tch = mNextChan->getL2Channel();
	GSM::L2LogicalChannel *sdcch = this->getL2Channel();
	// Note we do not want to do a HARDRELEASE if this fails, because that bypasses the very timer we are supposed to be using.
	LOG(INFO) << "sending AssignmentCommand for " << tch << " on " << this;
	tch->lcopen();	// This sets T3101 as a side effect.
	tch->setPhy(*sdcch);
}

// This occurs on the channel being assigned from.
// We need to release the nextChannel.  Caller takes care of this chan.
// Release of nextChan also occurs if a channel drops out of the main service loop and the nextChan state is still ReassignTarget
void L3LogicalChannel::reassignFailure()
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	LOG(DEBUG) << this;
	// Clean up the next chan we were trying to reassign to.
	if (mNextChan) {
		devassert(mNextChan->isTCHF());
		mNextChan->chanSetState(L3LogicalChannel::chRequestRelease);	// This will probably block the chan for 30 seconds.
		// If we never received the ESTABLISH primitive on nextChan, then the service loop is not runnning,
		// so it will never detect the change of state on nextChan, so we must free the channel here.
		// The service loops check dcch->chanRunning(), so they will not do anything that might try to use the Context we are freeing..
		mNextChan->chanFreeContext(TermCause::Local(L3Cause::Channel_Assignment_Failure));
		mNextChan = NULL;
	} else {
		LOG(ERR) << "reassignment failure but no nextChan? "<<this;
	}


	// channel()->l3sendm(L3ReleaseComplete(l3ti,l3cause));	// Release the transaction identifier.
	// We might as well drop the whole channel.
	// Old code had cause 4, but that does not seem right:
	// RR Cause 0x04 -- "abnormal release, no activity on the radio path"
	// Caller does this now.
	//l3sendm(GSM::L3ChannelRelease(L3RRCause::NoActivityOnTheRadio));
	// The MS is supposed to release the channel, which will send a RELEASE primitive up to Layer3 to close the channel.
}

// Both old and new L3LogicalChannel point to the same MMContext.
// The message arrives on the new channel, but we run this function on the old channel
// because we have not changed the LogicalChannel that the Context points to yet.
// mNextChan still points to the new channel.
// Beware that the two channels are serviced by different threads.
void L3LogicalChannel::reassignComplete()
{
	{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	//timerStop(TChReassignment);		// Handled by assignTCHFProcedure, which is notified after us.

	if (!mChContext) {
		// Logic error.
		LOG(ERR) << "received channel reassignment complete on dead channel:"<<this;
		l3sendm(GSM::L3ChannelRelease(L3RRCause::Normal_Event));
		chanSetState(chRequestRelease);
		return;
	}
	if (!mNextChan) {
		// Logic error.
		LOG(ERR) << "received channel reassignment complete with no nextchan allocated"<<this;
		l3sendm(GSM::L3ChannelRelease(L3RRCause::Normal_Event));
		chanSetState(chRequestRelease);
		return;
	}
	if (mNextChan->mChState != chEstablished) {
		// The nextChan was supposed to get an ESTABLISH primitive then the L3AssignComplete command in order to get here.
		// There could be a logic error or the MS may have dropped the channel at this inopportune moment,
		// so it is not necessarily an error.
		LOG(NOTICE)<< "Next channel in unexpected state, dropping channel"<<LOGVARM(mNextChan);
		chanSetState(chRequestRelease);
		return;
	}
	mChContext->mmSetChannel(mNextChan);
	LOG(INFO) <<"successful channel reassignment" <<LOGVAR2("from-channel",this) <<LOGVAR2("to-channel",mNextChan);
	mNextChan = NULL;
	} // release lock

	// FIXME: There is a race for the new channel to get its ESTABLISH before this old one gets this hardrelease.
	msleep(400);
	chanSetState(chRequestHardRelease);	// Done with this channel.
	//chanSetState(L3LogicalChannel::chReassignComplete);	// Redundant with sending the HARDRELEASE, this will cause the service loop to exit.
}

#if UNUSED
void L3LogicalChannel::chanLost()
{
	LOG(DEBUG)<<this;
	// Just in case this gets called with a next channel, release it too, although it would eventually time out on its own.
	if (mNextChan) {
		mNextChan->chanSetState(L3LogicalChannel::chReassignFailure);
		//mNextChan->mPrevChan = NULL;
		mNextChan = NULL;
	}
	chanFreeContext();
}
#endif

// Set the flag, which will perform the channel release from the channel serviceloop.
void L3LogicalChannel::chanRelease(Primitive prim,TermCause cause)
{
	OBJLOG(DEBUG) << prim;
	//chanFreeContext(cause);
	switch (prim) {
		case L3_HARDRELEASE_REQUEST:
			chanSetState(L3LogicalChannel::chRequestHardRelease);
			return;
		case L3_RELEASE_REQUEST:
			chanSetState(L3LogicalChannel::chRequestRelease);
			return;
		default:
			assert(0);
	}
}

// This completely releases the channel and all transactions on it.
// FIXME no it doesnt, and L2 can hang when we send the primitive, so these transactions and dialogs
// are not cleaned up until the next time the channel is used.  Very bad.
// pat 7-2014 Update: Above bug probably fixed by GSMLogicalChannel rewrite.
void L3LogicalChannel::chanClose(RRCause rrcause,Primitive prim,TermCause upstreamCause)
{
	// Note: timer expiry may indicate unresponsive MS so this may block for 30 seconds.
	l3sendm(L3ChannelRelease(rrcause));
	chanRelease(prim,upstreamCause);
}


//void L3LogicalChannel::chanSetVoiceTran(TranEntry *tran)
//{
//	MMContext *set = chanGetContext(true);
//	set->tsSetVoiceTran(tran);
//}

RefCntPointer<TranEntry> L3LogicalChannel::chanGetVoiceTran()
{
	MMContext *set = chanGetContext(true);
	return set->tsGetVoiceTran();
}

//void L3LogicalChannel::chanEnqueueFrame(L3Frame *frame)
//{
//	ml3UplinkQ.write(frame);
//}

// When L3 wants to drop a channel, it must set a flag in the L3LogicalChannel, which will be queried here.
// Return false to drop the channel.
bool L3LogicalChannel::chanRunning()
{
	// Check for channel release.
	switch (this->mChState) {
		case L3LogicalChannel::chEstablished:
		case L3LogicalChannel::chReassignTarget:
		//case L3LogicalChannel::chReassignPending:
			return true;	// Still running.
		case L3LogicalChannel::chIdle:			// seeing this would be a bug.
		case L3LogicalChannel::chRequestRelease:
		case L3LogicalChannel::chRequestHardRelease:
		//case L3LogicalChannel::chReassignFailure:
		//case L3LogicalChannel::chReassignComplete:
		//case L3LogicalChannel::chReleased:
			return false;
	}
	return false;
}

const char *L3LogicalChannel::ChannelState2Text(ChannelState chstate)
{
	switch (chstate) {
	case L3LogicalChannel::chIdle: return "Idle";
	case L3LogicalChannel::chEstablished: return "Established";
	//case L3LogicalChannel::chReleased: return "Released";
	case L3LogicalChannel::chRequestRelease: return "RequestRelease";
	case L3LogicalChannel::chRequestHardRelease: return "RequestHardRelease";
	case L3LogicalChannel::chReassignTarget: return "ReassignTarget";
	//case L3LogicalChannel::chReassignPending: return "ReassignPending";
	//case L3LogicalChannel::chReassignComplete: return "ReassignmentComplete";
	//case L3LogicalChannel::chReassignFailure: return "ReassignmentFailure";
	}
	return "(chstate undefined)";
}

// Info about just this L3LogicalChannel
std::ostream& L3LogicalChannel::chanText(ostream& os) const
{
	os << descriptiveString() << LOGVAR2("state",ChannelState2Text(mChState));
	if (mNextChan) { os <<LOGVAR2("nextchan",mNextChan->descriptiveString()); }
	return os;
}

// Info about just the underlying MMContext for this L3LogicalChannel.
// Warning: This is called from the CLI thread.
std::ostream& L3LogicalChannel::chanContextText(ostream& os) const
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	if (MMContext *mmc = Unconst(this)->chanGetContext(false)) {
		mmc->mmcText(os);
	}
	return os;
}

ostream& operator<<(ostream& os, const L3LogicalChannel& chan) { chan.chanText(os); return os; }

std::ostream& operator<<(std::ostream&os, const L3LogicalChannel*ch) {
	if (ch) { ch->chanText(os); } else { os << "(null channel)"; }
	return os;
}

// pat FIXME - Called from CLI so must lock channel.
// Warning: This is called from the CLI thread.
void L3LogicalChannel::getTranIds(TranEntryList &tids) const
{
	ScopedLock lock(gMMLock,__FILE__,__LINE__);
	tids.clear();
	if (const MMContext *set = Unconst(this)->chanGetContext(false)) {
		set->getTranIds(tids);
	}
}


bool L3LogicalChannel::isTCHF() const
{
	return chtype()==GSM::FACCHType;
}

bool L3LogicalChannel::isSDCCH() const
{
	return chtype()==GSM::SDCCHType;
}

// For use by the CLI.
void printChansInfo(std::ostream&os)
{
	L2ChanList chans;
	gBTS.getChanVector(chans);
	for (L2ChanList::iterator it = chans.begin(); it != chans.end(); it++) {
		L3LogicalChannel *chan = dynamic_cast<L3LogicalChannel*>(*it);
		os << chan;
		chan->chanContextText(os);
		os << endl;
	}
}

};	// namespace
