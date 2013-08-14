/**@file Messages for peer-to-peer protocol */
/*
 * Copright 2011 Range Networks, Inc.
 * All rights reserved.
*/


#include "Peering.h"
#include "NeighborTable.h"

#include <Logger.h>
#include <Globals.h>
#include <GSMConfig.h>
#include <GSMLogicalChannel.h>
#include <GSML3RRElements.h>
#include <TransactionTable.h>

#undef WARNING

using namespace Peering;




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
		sleep(1);
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
	// FIXME -- We need something here to spot malformed messages.
	unsigned transactionID;
	sscanf(message, "%*s %*s %u", &transactionID);
	mFIFOMap.writeFIFO(transactionID,message);
}


void PeerInterface::processNeighborParams(const struct sockaddr_in* peer, const char* message)
{
	static const char rspFormat[] = "RSP NEIGHBOR_PARAMS %d %d";

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
		unsigned C0, BSIC;
		int r = sscanf(message, rspFormat, &C0, &BSIC);
		if (r!=2) {
			LOG(ALERT) << "badly formatted peering message: " << message;
			return;
		}
		// Did the neighbor list change?
		bool change = gNeighborTable.addInfo(peer,(unsigned)time(NULL),C0,BSIC);
		// no change includes unsolicited RSP NEIGHBOR_PARAMS.  drop it.
		if (!change) return;
		// It there a BCC conflict?
		int ourBSIC = gBTS.BSIC();
		int BCC = BSIC & 0x07;
		int ourBCC = ourBSIC & 0x07;
		if (BCC == ourBCC) { LOG(ALERT) << "neighbor with matching BCC " << ourBCC; }
		// Is there an NCC conflict?
		int NCC = BSIC >> 3;
		int NCCMaskBit = 1 << NCC;
		int ourNCCMask = gConfig.getNum("GSM.CellSelection.NCCsPermitted");
		if ((NCCMaskBit & ourNCCMask) == 0) { 
			LOG(ALERT) << "neighbor with NCC " << NCC << " not in NCCsPermitted";
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


void PeerInterface::processHandoverRequest(const struct sockaddr_in* peer, const char* message)
{
	// This is "Handover Request" in the ladder diagram; we are "BS2" accepting it.
	assert(message);
	unsigned oldTransID;
	if (!sscanf(message,"REQ HANDOVER %u ", &oldTransID)) {
		LOG(ALERT) << "cannot parse peering message " << message;
		return;
	}
	LOG(DEBUG) << message;

	// Break message into space-delimited tokens, stuff into a SimpleKeyValue and then unpack it.
	SimpleKeyValue params;
	params.addItems(message);
	const char* IMSI = params.get("IMSI");
	GSM::L3MobileIdentity mobileID = GSM::L3MobileIdentity(IMSI);
	//const char *callID = params.get("CallID");
	const char *proxy = params.get("Proxy");

	// find existing transaction record if this is a duplicate REQ HANDOVER
	Control::TransactionEntry* transaction = gTransactionTable.find(mobileID, oldTransID);

	// and the channel that goes with it
	GSM::LogicalChannel* chan = NULL;
	if (transaction) {
		chan = transaction->channel();
		LOG(DEBUG) << *transaction;
	}

	// if this is the first REQ HANDOVER
	if (!transaction) {

		LOG(INFO) << "initial REQ HANDOVER for " << mobileID << " " << oldTransID;

		// Get a channel allocation.
		// For now, we are assuming a full-rate channel.
		// And check gBTS.hold()
		time_t start = time(NULL);
		if (!gBTS.hold()) chan = gBTS.getTCH();
		LOG(DEBUG) << "getTCH took " << (time(NULL) - start) << " seconds";

		// FIXME -- Somehow, getting from getTCH above to the test below can take several seconds.
		// FIXME -- #797.

		// If getTCH took so long that there's too little time left in T3101, ignore this REQ and get the next one.
		if (chan && chan->SACCH()->debugGetL1()->decoder()->debug3101remaining() < 1000) {
			LOG(NOTICE) << "handover TCH allocation took too long; risk of T3101 timeout; trying again";
			return;
		}

		// If there's no channel available, send failure.
		if (!chan) {
			LOG(CRIT) << "congestion";
			char rsp[50];
			// RR Cause 101 "cell allocation not available"
			// GSM 04.08 10.5.2.31
			sprintf(rsp,"RSP HANDOVER %u 101", oldTransID);
			sendMessage(peer,rsp);
			return;
		}

		// build a new transaction record
		transaction = new Control::TransactionEntry(peer,mReferenceCounter++,params,proxy,chan,oldTransID);
		mFIFOMap.addFIFO(transaction->ID());
		gTransactionTable.add(transaction);
		LOG(INFO) "creating new transaction " << *transaction;

		// Set the channel state.
		chan->handoverPending(true);
	}

	// Send accept.
	const GSM::L3ChannelDescription desc = chan->channelDescription();
	char rsp[50];
	sprintf(rsp,"RSP HANDOVER %u 0  %u  %u %u %u  %u %u %u %u",
		oldTransID,
		transaction->inboundReference(),
		gTRX.C0(), gBTS.NCC(), gBTS.BCC(),
		desc.typeAndOffset(), desc.TN(), desc.TSC(), desc.ARFCN()
		);
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




void PeerInterface::processHandoverResponse(const struct sockaddr_in* peer, const char* message)
{
	unsigned cause;
	unsigned transactionID;
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

	Control::TransactionEntry *transaction = gTransactionTable.find(transactionID);
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
}




void PeerInterface::sendMessage(const struct sockaddr_in* peer, const char *message)
{
	LOG(DEBUG) << "sending message: " << message;
	ScopedLock lock(mLock);
	mSocket.send((const struct sockaddr*)peer,message);
}

bool PeerInterface::sendUntilAck(const Control::TransactionEntry* transaction, const char* message)
{
	LOG(DEBUG) << "sending message until ACK: " << message;
	const struct sockaddr_in* peer = transaction->inboundPeer();
	char *ack = NULL;
	unsigned timeout = gConfig.getNum("Peering.ResendTimeout");
	unsigned count = gConfig.getNum("Peering.ResendCount");
	while (!ack && count>0) {
		sendMessage(peer,message);
		ack = mFIFOMap.readFIFO(transaction->ID(),timeout);
		count--;
	}

	// Timed out?
	if (!ack) {
		LOG(ALERT) << "lost contact with peer: " << *transaction;
		return false;
	}
	LOG(DEBUG) << "ack message: " << ack;

	// FIXME -- Check to be sure it's the right host acking the right message.
	// See #832.
	if (strncmp(ack,"ACK ",4)==0) {
		free(ack);
		return true;
	}

	// Not the expected ACK?
	LOG(CRIT) << "expecting ACK, got " << *ack;
	free(ack);
	return false;
}



