/**@file Messages for peer-to-peer protocol */
/*
 * Copright 2011, 2014 Range Networks, Inc.
 * All rights reserved.
*/


#include "Peering.h"
#include "NeighborTable.h"

#include <Logger.h>
#include <Globals.h>
#include <GSMConfig.h>
#include <GSMLogicalChannel.h>
#include <GSML3RRElements.h>
#include <L3TranEntry.h>
//#include <TransactionTable.h>

#undef WARNING

namespace Peering {
using namespace Control;


// Original Diagram courtesy Doug:
// MS                                         BS1                                       BS2                       switch
// 
//                                             A ------------- REQ HANDOVER -----------> a
//                                                   (BS1tranid, L3TI, IMSI, called/caller, SIP REFER message)
//                                             C <------------ RSP HANDOVER ------------ a
//                                                   (BS1tranid, cause, L3HandoverCommand)
// <------------- L3HandoverCommand ---------- D
// -------------------------------- handover access -----------------------------------> b
// <------------------------------- physical information ------------------------------- c
// -------------------------------- handover complete ---------------------------------> d
//                                                                                       e ----- re-INVITE ------>
//                                                                                       f <-------- OK ----------
//                                                                                       g --------- ACK -------->
//                                             E <------- IND HANDOVER_COMPLETE -------- h  (Note: These were reversed in
//                                                          (BS2tranid)
//                                             E -------- ACK HANDOVER_COMPLETE -------> h         original diagram)
//                                                          (BS2tranid)
// 
// 
// 
// A = handover determination                                                            ac = stuff transaction entry
//    We could use a REFER for this message, with a content including both SDP and MSC params.
// B = build string for handover determination                                           ah = transaction entry definition
// BH = SIPEngine.h
// A = processHandoverRequest in BS2
//		If resources are available:
//		calls TranEntry::newHandover(peer,horef,params,chan,oldTransID) to squirrel away:
//			IMSI, either called (for MOC) or calling (for MTC) number, L3TI and the SIP REFER message.
//			Note that everything but L3TI is also available in the SIP REFER message.
// C = processHandoverResponse in BS1
// d when we receive the handover complete the InboundHandoverMachine calls newSipDialogHandover, which sends the re-INVITE.
//   then when the dialog becomas active, we send the IND HANDOVER_COMPLETE.
// E = processHandoverComplete in BS1



void PeerMessageFIFOMap::addFIFO(unsigned transactionID)
{
	PeerMessageFIFO* newFIFO = new PeerMessageFIFO;
	write(transactionID,newFIFO);
}

void PeerMessageFIFOMap::removeFIFO(unsigned transactionID)
{
	if (!remove(transactionID)) { LOG(DEBUG) << "attempt to remove non-existent FIFO " << transactionID; }
}


char* PeerMessageFIFOMap::readFIFO(unsigned transactionID, unsigned timeout)
{
	PeerMessageFIFO* FIFO = readNoBlock(transactionID);
	if (!FIFO) {
		LOG(NOTICE) << "attempt to read non-existent FIFO " << transactionID;
		return NULL;
	}
	return FIFO->read(timeout);
}


void PeerMessageFIFOMap::writeFIFO(unsigned transactionID, const char* msg)
{
	PeerMessageFIFO* FIFO = readNoBlock(transactionID);
	if (!FIFO) {
		LOG(NOTICE) << "attempt to write non-existent FIFO " << transactionID;
		return;
	}
	FIFO->write(strdup(msg));
}



PeerInterface::PeerInterface()
	:mSocket(gConfig.getNum("Peering.Port")),
	mReferenceCounter(0)
{
	mSocket.nonblocking();
}


void* foo(void*)
{
	gPeerInterface.serviceLoop1(NULL);
	return NULL;
}


void* bar(void*)
{
	gPeerInterface.serviceLoop2(NULL);
	return NULL;
}


void PeerInterface::start()
{
	// FIXME - mServer.start(serviceLoop, NULL); wouldn't compile and I'm not smart enough to figure it out
	mServer1.start(foo, NULL);
	mServer2.start(bar, NULL);
}


// mucking about every second with the neighbor tables is plenty
void* PeerInterface::serviceLoop1(void*)
{
	// gTRX.C0() needs some time to get ready
	sleep(8);
	while (1) {
		gNeighborTable.refresh();
		sleep(1);	// (pat) Every second?  Give me a break.
	}
	return NULL;
}

// this loop services, among other things, the IND and ACK HANDOVER_COMPLETE, which affects the 
// handover gap in the call.  so it needs to be short.  Like 10msec = 10000usec
void* PeerInterface::serviceLoop2(void*)
{
	// gTRX.C0() needs some time to get ready
	sleep(8);
	while (1) {
		drive();
		usleep(10000);
	}
	return NULL;
}


void PeerInterface::drive()
{
	int numRead = mSocket.read(mReadBuffer);
	if (numRead<0) {
		return;
	}

	mReadBuffer[numRead] = '\0';
	LOG(INFO) << "received " << mReadBuffer;

	process(mSocket.source(),mReadBuffer);
}


void PeerInterface::process(const struct sockaddr_in* peer, const char* message)
{
	LOG(DEBUG) << message;
	// The REQ HANDOVER is only watched if it is the first.
	if (strstr(message,"HANDOVER") && ! strstr(message,"REQ HANDOVER")) WATCH(Utils::timestr() << " Peering recv:"<<message);
	// neighbor message?
	if (strncmp(message+3," NEIGHBOR_PARAMS",16)==0)
		return processNeighborParams(peer,message);

	// must be handover related

	// Initial inbound handover request?
	if (strncmp(message,"REQ HANDOVER ",13)==0)
		return processHandoverRequest(peer,message);

	// Handover response? ("Handover Accept" in the ladder.)
	if (strncmp(message,"RSP HANDOVER ",13)==0)
		return processHandoverResponse(peer,message);

	// IND HANDOVER_COMPLETE
	if (strncmp(message,"IND HANDOVER_COMPLETE ", 22)==0)
		return processHandoverComplete(peer,message);

	// IND HANDOVER_FAILURE
	if (strncmp(message,"IND HANDOVER_FAILURE ", 21)==0)
		return processHandoverFailure(peer,message);

	// Other handover messages go into the FIFO map.
	// (pat) It is an ACK message, and we need to queue it because the 'senduntilack' is running in a different thread.
	// FIXME -- We need something here to spot malformed messages.
	unsigned transactionID;
	sscanf(message, "%*s %*s %u", &transactionID);
	mFIFOMap.writeFIFO(transactionID,message);
}

// Suppress fast identical ALERT messages.
static const unsigned alertRepeatTime = 300;
static void logAlert(string message)
{
	static map<string,time_t> mSuppressedAlerts;
	map<string,time_t>::iterator it = mSuppressedAlerts.find(message);
	if (it == mSuppressedAlerts.end()) {
		LOG(ALERT) << message;
		mSuppressedAlerts[message] = time(NULL);
	} else {
		time_t when = it->second;
		if ((signed)(time(NULL) - when) > (signed)alertRepeatTime) {
			// One ALERT every five minutes.  The rest are demoted to ERR.
			LOG(ALERT) << message;
			mSuppressedAlerts[message] = time(NULL);
		} else {
			LOG(ERR) << message;
		}
	}
}

// pats TODO: We should check for conflicts when we start up.
// pats TODO: Check for more than 31 ARFCNs and report.
// pats TODO: We should check for ARFCN+BSIC conflicts among the neighbors.  That implies sucking the whole neighbor table in,
// but dont worry about now it because the whole thing should move to SR imho.
void PeerInterface::processNeighborParams(const struct sockaddr_in* peer, const char* message)
{
	static const char rspFormat[] = "RSP NEIGHBOR_PARAMS %u %u";

	LOG(DEBUG) << "got message " << message;
	if (0 == strncmp(message,"REQ ",4)) {
		// REQ?  Send a RSP.
		char rsp[100];
		sprintf(rsp, rspFormat, gTRX.C0(), gBTS.BSIC());
		sendMessage(peer,rsp);
		return;
	}

	if (0 == strncmp(message,"RSP ",4)) {
		// RSP?  Digest it.
		// (pat) ARFCN-C0 (the one carrying BCCH) and BSIC consisting of NCC (Network Color Code) and BCC. (Base station Color Code)
		unsigned neighborC0, neighborBSIC;
		int r = sscanf(message, rspFormat, &neighborC0, &neighborBSIC);
		if (r!=2) {
			logAlert(format("badly formatted peering message: %s",message));
			return;
		}
		// Did the neighbor list change?
		bool change = gNeighborTable.addInfo(peer,(unsigned)time(NULL),neighborC0,neighborBSIC);
		// no change includes unsolicited RSP NEIGHBOR_PARAMS.  drop it.
		if (!change) return;
		// It there a BCC conflict?
		unsigned ourBSIC = gBTS.BSIC();
		//int neighborBCC = neighborBSIC & 0x07;
		//int ourBCC = ourBSIC & 0x07;
		// (pat) 5-2013: This message was incorrect, because the BTS uniquifying information is not just the BCC,
		// it is the full C0+BSIC, so fixed it.
		// Note that this message also comes out over and over again.
		// if (BCC == ourBCC) { LOG(ALERT) << "neighbor with matching BCC " << ourBCC; }
		if (neighborC0 == gTRX.C0()) {
			if (neighborBSIC == ourBSIC) {
				logAlert(format("neighbor with matching ARFCN.C0 + BSIC [Base Station Identifier] codes: C0=%u BSIC=%u",neighborC0,neighborBSIC));
			} else {
				// (pat) This seems like a bad idea too, with two BTS on the same ARFCN close enough to be neighbors,
				// but I dont know if it is worth an ALERT?
				LOG(WARNING) << format("neighbor with matching ARFCN.C0 but different BSIC [Base Station Identifier] code: C0=%u, BSIC=%u, my BSIC=%u",neighborC0,neighborBSIC,gTRX.C0());
			}
		}
		// Is there an NCC conflict?
		int neighborNCC = neighborBSIC >> 3;
		int NCCMaskBit = 1 << neighborNCC;
		int ourNCCMask = gConfig.getNum("GSM.CellSelection.NCCsPermitted");
		ourNCCMask |= 1 << gConfig.getNum("GSM.Identity.BSIC.NCC");
		if ((NCCMaskBit & ourNCCMask) == 0) { 
			//LOG(ALERT) << "neighbor with NCC " << neighborNCC << " not in NCCsPermitted";
			logAlert(format("neighbor with NCC=%u not in NCCsPermitted",neighborNCC));
		}
		// There was a change, so regenerate the beacon
		gBTS.regenerateBeacon();
		return;
	}

	LOG(ALERT) << "unrecognized message: " << message;
}


void PeerInterface::sendNeighborParamsRequest(const struct sockaddr_in* peer)
{
	sendMessage(peer,"REQ NEIGHBOR_PARAMS");
	// Get a string for the sockaddr_in.
	char addrString[256];
	const char *ret = inet_ntop(AF_INET,&(peer->sin_addr),addrString,255);
	if (!ret) {
		LOG(ALERT) << "cannot parse peer socket address";
		return;
	}
	LOG(DEBUG) << "requested parameters from " << addrString << ":" << ntohs(peer->sin_port);
}


// (pat) This is BS2 which has received a request from BS1 to transfer the MS from BS1 to BS2.
// Manufacture a TransactionEntry and SIPEngine from the peering message.
void PeerInterface::processHandoverRequest(const struct sockaddr_in* peer, const char* message)
{
	// This is "Handover Request" in the ladder diagram; we are "BS2" accepting it.
	assert(message);

	unsigned oldTransID;	// (pat) tran on BS1 that wants to come to BS2.
	if (!sscanf(message,"REQ HANDOVER %u ", &oldTransID)) {
		LOG(ALERT) << "cannot parse peering message " << message;
		return;
	}

	// Break message into space-delimited tokens, stuff into a SimpleKeyValue and then unpack it.
	SimpleKeyValue params;
	params.addItems(message);
	const char* IMSI = params.get("IMSI");
	GSM::L3MobileIdentity mobileID = GSM::L3MobileIdentity(IMSI);

	// find existing transaction record if this is a duplicate REQ HANDOVER
	Control::TranEntry* transaction = gNewTransactionTable.ttFindHandoverOther(mobileID, oldTransID);

	// and the channel that goes with it
	GSM::L2LogicalChannel* chan = NULL;
	unsigned horef;

	// if this is the first REQ HANDOVER
	if (!transaction) {
		WATCH(Utils::timestr() << " Peering recv:"<<message);

		LOG(INFO) << "initial REQ HANDOVER for " << mobileID << " " << oldTransID;

		// Get a channel allocation.
		// For now, we are assuming a full-rate channel.
		// And check gBTS.hold()
		time_t start = time(NULL);
		if (!gBTS.hold()) { chan = gBTS.getTCH(); }	// (pat) Starts T3101.  Better finish before it expires.
		LOG(DEBUG) << "getTCH took " << (time(NULL) - start) << " seconds";

		// FIXME -- Somehow, getting from getTCH above to the test below can take several seconds.
		// FIXME -- #797.

		// If getTCH took so long that there's too little time left in T3101, ignore this REQ and get the next one.
		if (chan && chan->SACCH()->debugGetL1()->decoder()->debug3101remaining() < 1000) {
			LOG(NOTICE) << "handover TCH allocation took too long; risk of T3101 timeout; trying again";
			chan->l2sendp(HARDRELEASE);	// (pat) added 9-6-2013
			return;
		}

		// If there's no channel available, send failure.
		if (!chan) {
			LOG(CRIT) << "congestion, incoming handover request denied";
			char rsp[50];
			// RR Cause 101 "cell allocation not available"
			// GSM 04.08 10.5.2.31
			sprintf(rsp,"RSP HANDOVER %u 101", oldTransID);
			sendMessage(peer,rsp);
			return;
		}

		// build a new transaction record.
		// Allocate a new inbound handover reference.  It is placed in the L3HandoverCommand and then used
		// in Layer1 as a really cheap validation on an inbound handover access
		// to make sure the incoming handset is the one we want.
		horef = 0xff & (++mReferenceCounter);
		transaction = Control::TranEntry::newHandover(peer,horef,params,chan,oldTransID);
		mFIFOMap.addFIFO(transaction->tranID());
		LOG(INFO) "creating new transaction " << *transaction;

		// Set the channel state.
		// This starts T3103.
		chan->handoverPending(true,horef);
	} else {
		chan = transaction->getL2Channel();
		horef = transaction->getHandoverEntry(true)->mInboundReference;
		LOG(DEBUG) << *transaction;
	}

	// Send accept.
	// FIXME TODO_NOW: Get rid of this sleepFrames...
	sleepFrames(30);	// Pat added delay to let SACCH get started.

	const GSM::L3ChannelDescription desc = chan->channelDescription();
#if 1
	// Build the L3 HandoverCommand that BS1 will send to the phone to tell it to come to us, BS2.
	L3HandoverCommand handoverMsg(
		GSM::L3CellDescription(gTRX.C0(),gBTS.NCC(),gBTS.BCC()),
		GSM::L3ChannelDescription2(desc),
		GSM::L3HandoverReference(horef),
		GSM::L3PowerCommandAndAccessType(),
		GSM::L3SynchronizationIndication(true, true));

	L3Frame handoverFrame(handoverMsg);
	string handoverHex = handoverFrame.hexstr();
	char rsp[50];
	sprintf(rsp,"RSP HANDOVER %u 0 0x%s",oldTransID,handoverHex.c_str());
#else
	char rsp[50];
	sprintf(rsp,"RSP HANDOVER %u 0  %u  %u %u %u  %u %u %u %u",
		oldTransID,
		horef,
		gTRX.C0(), gBTS.NCC(), gBTS.BCC(),
		desc.typeAndOffset(), desc.TN(), desc.TSC(), desc.ARFCN()
		);
#endif
	sendMessage(peer,rsp);
	return;
}

void PeerInterface::processHandoverComplete(const struct sockaddr_in* peer, const char* message)
{
	// This is "IND HANDOVER" in the ladder diagram; we are "BS1" receiving it.
	unsigned transactionID;
	if (!sscanf(message,"IND HANDOVER_COMPLETE %u", &transactionID)) {
		LOG(ALERT) << "cannot parse peering message " << message;
		return;
	}

	// Don't need to HARDRELEASE channel because that happens (if all is successful) in
	// outboundHandoverTransfer (Control/CallControl.cpp).
	// Don't need to remove the transaction because that happens (if all is successful) in 
	// Control::callManagementLoop (Control/CallControl.cpp).
	// So why is this here if we don't do anything?
	// 1. IND/ACK HANDOVER_COMPLETE is useful indication of completion in OpenBTS log or GSMTAP trace.
	// 2. This is a placeholder in case we DO need to do something later.

	// FIXME -- We need to speed up channel recycling.  See #816.

	char rsp[50];
	sprintf(rsp,"ACK HANDOVER_COMPLETE %u", transactionID);
	sendMessage(peer,rsp);

	return;
}


void PeerInterface::processHandoverFailure(const struct sockaddr_in* peer, const char* message)
{
	// This indication means that inbound handover processing failed
	// on the other BTS.  This BTS cannot handover this call right now.
	unsigned transactionID;
	unsigned cause;
	unsigned holdoff = gConfig.getNum("GSM.Handover.FailureHoldoff");
	unsigned res = sscanf(message,"IND HANDOVER_FAILURE %u %u %u", &transactionID, &cause, &holdoff);
	if (res==0) {
		LOG(ALERT) << "cannot parse peering message " << message;
		return;
	}

	if (res<3) {
		LOG(ALERT) << "peering message missing parameters " << message;
	}

	// Set holdoff on this BTS.

	// FIXME -- We need to decide what else to do here.  See #817.
	gNeighborTable.holdOff(peer,holdoff);

	char rsp[50];
	sprintf(rsp,"ACK HANDOVER_FAILURE %u", transactionID);
	sendMessage(peer,rsp);

	return;
}



// (pat) BS1 receives this message from BS2 to allow transferring an MS to BS2.
// We set a state in the transaction that causes the serviceloop to send an L3HandoverCommand to the MS.
void PeerInterface::processHandoverResponse(const struct sockaddr_in* peer, const char* message)
{
	unsigned cause;
	unsigned transactionID;
	LOG(DEBUG) <<LOGVAR(message);
#if 1
	// This is "Handover Accept" in the ladder diagram; we are "BS1" receiving it.
	// FIXME -- Error-check for correct message format.
	char handoverCommandBuffer[82]; handoverCommandBuffer[0] = 0;
	int n = sscanf(message,"RSP HANDOVER %u %u 0x%80s", &transactionID, &cause, handoverCommandBuffer);

	if (n < 3 || strlen(handoverCommandBuffer) < 4) {	// It is bigger than 8.  I'm just quickly checking for emptiness.
		LOG(ERR) << "Invalid peering handover message:"<<message;
		return;
	}

	if (cause) {
		LOG(NOTICE) << "handover of" <<LOGVAR(transactionID) << " refused with"<<LOGVAR(cause);
		return;
	}

	// TODO: We should lock the transaction at this point.
	Control::TranEntry *transaction = gNewTransactionTable.ttFindById(transactionID);
	if (!transaction) {
		LOG(NOTICE) << "received handover response with no matching transaction " << transactionID;
		return;
	}
	HandoverEntry *hop = transaction->getHandoverEntry(true);
	hop->mHexEncodedL3HandoverCommand = string(handoverCommandBuffer);
	transaction->setGSMState(CCState::HandoverOutbound);
#else
	unsigned reference;
	unsigned C0, NCC, BCC;
	unsigned typeAndOffset, TN, TSC, ARFCN;

	// This is "Handover Accept" in the ladder diagram; we are "BS1" receiving it.
	// FIXME -- Error-check for correct message format.
	sscanf(message,"RSP HANDOVER %u %u  %u  %u %u %u  %u %u %u %u",
		&transactionID, &cause,
		&reference,
		&C0, &NCC, &BCC,
		&typeAndOffset, &TN, &TSC, &ARFCN
		);

	if (cause) {
		LOG(NOTICE) << "handover of transaction " << transactionID << " refused with cause " << cause;
		return;
	}

	Control::TranEntry *transaction = gNewTransactionTable.ttFindById(transactionID);
	if (!transaction) {
		LOG(NOTICE) << "received handover response with no matching transaction " << transactionID;
		return;
	}

	// Set the handover parameters and state to HandoverOutbound.
	// The state change will trigger the call management loop
	// to send the Handover Command to the handset.
	transaction->setOutboundHandover(
		GSM::L3HandoverReference(reference),
		GSM::L3CellDescription(C0,NCC,BCC),
		GSM::L3ChannelDescription2((GSM::TypeAndOffset)typeAndOffset,TN,TSC,ARFCN),
		GSM::L3PowerCommandAndAccessType(),
		GSM::L3SynchronizationIndication(true, true)
	);
#endif
}




void PeerInterface::sendMessage(const struct sockaddr_in* peer, const char *message)
{
	LOG(DEBUG) << "sending message: " << message;
	const char *eol = strchr(message,'\n');
	if (!strstr(message,"REQ NEI")) {
		WATCH(Utils::timestr() << " Peering send:"<<string(message,eol?eol-message:strlen(message)));	// first line of message.
	}
	ScopedLock lock(mLock);
	mSocket.send((const struct sockaddr*)peer,message);
}

// (pat) The FIFO is specific to the transaction id, which is why there cant be any other acks in there.
// We may leave a whole bunch of acks in the fifo but the fifo is used solely for this single message
// and is destroyed when the TranEntry is destroyed.
bool PeerInterface::sendUntilAck(const Control::HandoverEntry* hop, const char* message)
{
	LOG(DEBUG) << "sending message until ACK: " << message;
	const struct sockaddr_in* peer = &hop->mInboundPeer;
	char *ack = NULL;
	unsigned timeout = gConfig.getNum("Peering.ResendTimeout");
	unsigned count = gConfig.getNum("Peering.ResendCount");
	while (!ack && count>0) {
		sendMessage(peer,message);
		ack = mFIFOMap.readFIFO(hop->tranID(),timeout);
		count--;
	}

	// Timed out?
	if (!ack) {
		LOG(ALERT) << "lost contact with peer: ";// TODO: << *hop;
		return false;
	}
	LOG(DEBUG) << "ack message: " << ack;

	// FIXME -- Check to be sure it's the right host acking the right message.
	// See #832.
	// (pat) Any such above problems will go away when we switch peering to the sip interface.
	if (strncmp(ack,"ACK ",4)==0) {
		free(ack);
		return true;
	}

	// Not the expected ACK?
	LOG(CRIT) << "expecting ACK, got " << *ack;
	free(ack);
	return false;
}

// This is sent by BS2.
void PeerInterface::sendHandoverComplete(const Control::HandoverEntry* hop)
{
	char ind[100];
	sprintf(ind,"IND HANDOVER_COMPLETE %u", hop->tranID());
	gPeerInterface.sendUntilAck(hop,ind);
}

void PeerInterface::sendHandoverFailure(const Control::HandoverEntry *hop, GSM::RRCause cause,unsigned holdoff)
{
	char ind[100];
	sprintf(ind,"IND HANDOVER_FAILURE %u %u %u", hop->tranID(),cause,holdoff);
	gPeerInterface.sendUntilAck(hop,ind);
}

bool PeerInterface::sendHandoverRequest(string peer, const RefCntPointer<TranEntry> tran)
{
	string msg = string("REQ HANDOVER ") + tran->handoverString(peer);
	struct sockaddr_in peerAddr;
	if (!resolveAddress(&peerAddr,peer.c_str())) {
		LOG(ALERT) << "cannot resolve peer address " << peer;
		return false;
	}
	LOG(DEBUG) <<LOGVAR(peer) <<LOGVAR(msg);
	gPeerInterface.sendMessage(&peerAddr,msg.c_str());
	return true;
}

};
