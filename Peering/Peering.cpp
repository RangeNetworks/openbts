/**@file Messages for peer-to-peer protocol */
/*
* Copyright 2011, 2014 Range Networks, Inc.

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

#define LOG_GROUP LogGroup::Control

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
//                                                NeighborTable Refresh every N seconds:
//                                             X --- REQ NEIGHBOR_PARAMS -------------> X
//                                             X <-- RSP NEIGHBOR_PARAMS C0 BSIC ------ X
//                                             X --- REQ NEIGHBOR_PARAMS v=2 ---------> X
//                                             X <-- RSP NEIGHBOR_PARAMS key=value... - X
//
// 
//                                             A ------------- REQ HANDOVER -----------> a
//                                                   (BS1tranid, L3TI, IMSI, called/caller, SIP REFER message)
//                                             C <------------ RSP HANDOVER ------------ a
//                                                   (BS1tranid, cause, L3HandoverCommand)
// <------------- L3HandoverCommand ---------- D
// -------------------------------- handover access -----------------------------------> b
// <------------------------- physical information (with TA) --------------------------- c
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


string sockaddr2string(const struct sockaddr_in* peer, bool noempty)
{
	// Get a string for the sockaddr_in.
	char addrString[256];
	const char *ret = inet_ntop(AF_INET,&(peer->sin_addr),addrString,255);
	if (!ret) {
		LOG(ERR) << "cannot parse peer socket address";
	 	return string(noempty ? "<error: cannot parse peer socket address>" : "");
	}
	return format("%s:%d",addrString,(int)ntohs(peer->sin_port));
}


// Return a pointer to arg1, or an empty string.  Remember it is a pointer into the message so it is not terminated.
static const char *getPeeringMsgArg1(const char *message)
{
	const char *arg = strchr(message,' ');
	if (arg) {
		while (*arg == ' ') { arg++; }
		return arg;
	} else {
		return message + strlen(message);	// If no argument, return pointer to end of string, not a NULL pointer.
	}
}

// (pat 9-2014) Print out peering messages.  We triage the messages depending on the log level.
// INFO level - Handover messages only.
// DEBUG level - all messages.
static void logMessage(const char*sendOrRecv, const struct sockaddr_in* peer, const char *message)
{
	const char *arg1 = getPeeringMsgArg1(message);
	if (0 == strncmp(arg1,"HANDOVER",8)) {
		LOG(INFO) << "Peering "<<sendOrRecv <<LOGVAR2("peer",sockaddr2string(peer,true)) <<LOGVAR(message);
		// We used to watch everything except REQ NEI messages: if (strncmp(message,"REQ NEI",7))
		const char *eol = strchr(message,'\n');
		WATCHLEVEL(INFO," Peering "<<sendOrRecv <<LOGVAR2("peer",sockaddr2string(peer,true))
				<<LOGVAR2("message",string(message,eol?eol-message:strlen(message))));	// first line of message; they used to be long.
	} else {
		// At DEBUG level log all messages.
		LOG(DEBUG) << "Peering "<<sendOrRecv<<LOGVAR2("peer",sockaddr2string(peer,true)) <<LOGVAR(message);
	}

}

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


static void* foo(void*)
{
	gPeerInterface.serviceLoop1(NULL);
	return NULL;
}


static void* bar(void*)
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
	while (!gBTS.btsShutdown()) {
		gNeighborTable.ntRefresh();
		// (pat) This sleep is how often we look through the neighbor table, not how often we ping the peers;
		// that is determined inside the neighbor table loop by searching for peers who have not been refreshed
		// in the Peering.Neighbor.RefreshAge.
		// We do not need 1 second granularity here, and querying that quickly may be interfering with the ability
		// of gNeighborTable.getAddress() to access the NeighborTable.
		sleep(10);
	}
	return NULL;
}

// this loop services, among other things, the IND and ACK HANDOVER_COMPLETE, which affects the 
// handover gap in the call.  so it needs to be short.  Like 10msec = 10000usec
void* PeerInterface::serviceLoop2(void*)
{
	// gTRX.C0() needs some time to get ready
	sleep(8);
	while (!gBTS.btsShutdown()) {
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
	//LOG(INFO) << "received " << mReadBuffer;
	LOG(DEBUG) << "received " << mReadBuffer;

	process(mSocket.source(),mReadBuffer);
}


void PeerInterface::process(const struct sockaddr_in* peer, const char* message)
{
	logMessage("receive",peer,message);

	// neighbor message?
	if (strncmp(message+3," NEIGHBOR_PARAMS",16)==0)
		return processNeighborParams(peer,message);

	// must be handover related
	string peerString = sockaddr2string(peer, true);
	LOG(INFO) << "received from"<<LOGVAR2("peer",peerString) <<LOGVAR(message);

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
	try {

	// Create a unique id so we can tell if we send a message to ourself.
	static uint32_t btsid = 0;
	while (btsid == 0) { btsid = (uint32_t) random(); }

	// (pat) This is the original format, which I now call version 1.
	static const char rspFormatV1[] = "RSP NEIGHBOR_PARAMS %u %u";      // C0 BSIC
	// (pat) This is the 3-2014 format.  Extra args are ignored by older versions of OpenBTS.
	//static const char rspFormat[] = "RSP NEIGHBOR_PARAMS %u %u %u %d %u %u %u";  // C0 BSIC uniqueID noise numArfcns tchavail tchtotal

	LOG(DEBUG) << "got message " << message;
	if (0 == strncmp(message,"REQ ",4)) {
		// REQ?  Send a RSP.
		char rsp[150];
		if (! strchr(message,'=')) {
			// Send version 1.
			snprintf(rsp, sizeof(rsp), rspFormatV1, gTRX.C0(), gBTS.BSIC());
		} else {
			// Send version 2.
			int myNoise = gTRX.ARFCN(0)->getNoiseLevel();
			unsigned tchTotal = gBTS.TCHTotal();
			unsigned tchAvail = tchTotal - gBTS.TCHActive();
			snprintf(rsp, sizeof(rsp), "RSP NEIGHBOR_PARAMS V=2 C0=%u BSIC=%u btsid=%u noise=%d arfcns=%d TchAvail=%u TchTotal=%u",
					gTRX.C0(), gBTS.BSIC(), btsid, myNoise, (int)gConfig.getNum("GSM.Radio.ARFCNs"), tchAvail, tchTotal);
			sendMessage(peer,rsp);
		}
		return;
	}

	if (0 == strncmp(message,"RSP ",4)) {
		// RSP?  Digest it.
		NeighborEntry newentry;

		if (! strchr(message,'=')) {
			// Version 1 message.
			int r = sscanf(message, rspFormatV1, &newentry.mC0, &newentry.mBSIC);
			if (r != 2) {
				logAlert(format("badly formatted peering message: %s",message));
				return;
			}
		} else {
			SimpleKeyValue keys;
			keys.addItems(message + sizeof("RSP NEIGHBOR_PARAMS"));	// sizeof is +1 which is ok - we are skipping the initial space.

			{	bool valid;
				unsigned neighborID = keys.getNum("btsid",valid);
				if (valid && neighborID == btsid) {
					LOG(ERR) << "BTS is in its own GSM.Neighbors list.";
					return;
				}
			}

			newentry.mC0 = keys.getNumOrBust("C0");
			newentry.mBSIC = keys.getNumOrBust("BSIC");
			newentry.mNoise = keys.getNumOrBust("noise");
			newentry.mNumArfcns = keys.getNumOrBust("arfcns");
			newentry.mTchAvail = keys.getNumOrBust("TchAvail");
			newentry.mTchTotal = keys.getNumOrBust("TchTotal");
		}

		newentry.mIPAddress = sockaddr2string(peer, false);
		if (newentry.mIPAddress.size() == 0) {
			LOG(ERR) << "cannot parse peer socket address for"<<LOGVAR2("C0",newentry.mC0)<<LOGVAR2("BSIC",newentry.mBSIC);
			return;
		}


		// Did the neighbor list change?
		bool change = gNeighborTable.ntAddInfo(newentry);
		// no change includes unsolicited RSP NEIGHBOR_PARAMS.  drop it.
		if (!change) return;

		// It there a BCC conflict?
		int ourBSIC = gBTS.BSIC();
		if (newentry.mC0 == (int)gTRX.C0()) {
			if (newentry.mBSIC == ourBSIC) {
				logAlert(format("neighbor with matching ARFCN.C0 + BSIC [Base Station Identifier] codes: C0=%d BSIC=%u",newentry.mC0,newentry.mBSIC));
			} else {
				// Two BTS on the same ARFCN close enough to be neighbors, which is probably a bad idea, but legal.
				// Is it worth an ALERT?
				LOG(WARNING) << format("neighbor with matching ARFCN.C0 but different BSIC [Base Station Identifier] code: C0=%d, BSIC=%u, my BSIC=%u",
						newentry.mC0,newentry.mBSIC,gTRX.C0());
			}
		}

		// 3-2014: Warn for overlapping ARFCN use.  Fixes ticket #857
		int myC0 = gTRX.C0();
		int myCEnd = myC0 + gConfig.getNum("GSM.Radio.ARFCNs") - 1;
		int neighborCEnd = newentry.mC0 + newentry.mNumArfcns - 1;
		bool overlap = myC0 <= neighborCEnd && myCEnd >= (int) newentry.mC0;
		if (overlap) {
			LOG(WARNING) << format("neighbor IP=%s BSIC=%d ARFCNs=%u to %d overlaps with this BTS ARFCNs=%d to %d",
					newentry.mIPAddress.c_str(), newentry.mBSIC, newentry.mC0, neighborCEnd, myC0, myCEnd);
		}

		// Is there an NCC conflict?
		// (pat) ARFCN-C0 (the one carrying BCCH) and BSIC consisting of NCC (Network Color Code) and BCC. (Base station Color Code)
		int neighborNCC = newentry.mBSIC >> 3;
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

	LOG(ALERT) << "unrecognized Peering message: " << message;

	} catch (SimpleKeyValueException &e) {
		LOG(ERR) << format("invalid message (%s) from peer %s: %s",e.what(),sockaddr2string(peer,true),message);
	}
}

void PeerInterface::sendNeighborParamsRequest(const struct sockaddr_in* peer)
{
	sendMessage(peer,"REQ NEIGHBOR_PARAMS V=2");	// (pat) The v=2 requests version 2 of the response.
	// Get a string for the sockaddr_in.
	char addrString[256];
	const char *ret = inet_ntop(AF_INET,&(peer->sin_addr),addrString,255);
	if (!ret) {
		LOG(ALERT) << "cannot parse peer socket address";
		return;
	}
	LOG(DEBUG) << "requested parameters from " << addrString << ":" << ntohs(peer->sin_port);
}

static void addHandoverPenalty(GSM::L2LogicalChannel *chan,NeighborEntry &nentry, const char *cause)
{
	if (chan == NULL) { return; }	// Huh?
	if (cause == NULL) {
		LOG(WARNING)<<"Empty cause in handover record, handover penalty ignored";
		return;
	}

	int penaltyTime = 30;	// 30 seconds.

	NeighborPenalty npenalty;
	npenalty.mARFCN = nentry.mC0;
	npenalty.mBSIC = nentry.mBSIC;
	npenalty.mPenaltyTime.future(penaltyTime * 1000);
	chan->chanSetHandoverPenalty(npenalty);
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

	// (pat 3-2014) This is a new test: look for the peer address.  Previously we just looked for the transaction.
	// I didnt really want to test this, I just needed the ARFCN.  Could get it by looking up the peer in the transaction too.
	NeighborEntry nentry;
	if (! gNeighborTable.ntFindByPeerAddr(peer, &nentry)) {
		LOG(WARNING)<<"Could not find handover neighbor from peer address:"<< sockaddr2string(peer, true);
		return;	// The transaction will die a death by expiry.
	}

	// and the channel that goes with it
	GSM::L2LogicalChannel* chan = NULL;
	unsigned horef;

	// if this is the first REQ HANDOVER
	if (!transaction) {
		WATCH(Utils::timestr() << " Peering recv:"<<message);

		//LOG(INFO) << "initial REQ HANDOVER for " << mobileID << " " << oldTransID;
		LOG(DEBUG) << "initial REQ HANDOVER for " << mobileID << " " << oldTransID;

		// Get a channel allocation.
		// For now, we are assuming a full-rate channel.
		// And check gBTS.hold()
		time_t start = time(NULL);
		if (!gBTS.btsHold()) {
			chan = gBTS.getTCH(); 	// (pat) Starts T3101.  Better finish before it expires.
			if (!chan) { LOG(CRIT) << "congestion, incoming handover request denied"; }
		}
		LOG(DEBUG) << "getTCH took " << (time(NULL) - start) << " seconds";

		// (doug) FIXME -- Somehow, getting from getTCH above to the test below can take several seconds.
		// (doug) FIXME -- #797.

		// If getTCH took so long that there's too little time left in T3101, ignore this REQ and get the next one.
		if (chan && chan->debug3101remaining() < 1000) {
			LOG(NOTICE) << "handover TCH allocation took too long; risk of T3101 timeout; trying again";
			chan->l2sendp(L3_HARDRELEASE_REQUEST);	// (pat) added 9-6-2013
			return;
		}

		// If there's no channel available, send failure.
		if (!chan) {
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

		// This prevents a reverse handover from BTS2->BTS1 after a successful handover from BTS1->BTS2.
		addHandoverPenalty(chan,nentry,params.get("cause"));

		// Set the channel state.
		// This starts T3103.
		chan->handoverPending(true,horef);
	} else {
		chan = transaction->getL2Channel();
		horef = transaction->getHandoverEntry(true)->mInboundReference;
		LOG(DEBUG) << *transaction;
	}

	// (pat 6-2014) FIXME We dont have TA yet, so we are just starting the channel with 0 TA.
	// What is the correct procedure?  Should we not start SACCH until we receive the HandoverReference?
	chan->lcstart();

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
	gNeighborTable.setHoldOff(peer,holdoff);

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
	logMessage("send",peer,message);
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

bool PeerInterface::sendHandoverRequest(string peer, const RefCntPointer<TranEntry> tran, string cause)
{
	string msg = string("REQ HANDOVER ") + tran->handoverString(peer,cause);
	struct sockaddr_in peerAddr;
	if (!resolveAddress(&peerAddr,peer.c_str())) {
		LOG(ALERT) << "cannot resolve peer address " << peer;
		return false;
	}
	//LOG(INFO) <<LOGVAR(peer) <<LOGVAR(msg);
	LOG(DEBUG) <<LOGVAR(peer) <<LOGVAR(msg);
	gPeerInterface.sendMessage(&peerAddr,msg.c_str());
	return true;
}

};
