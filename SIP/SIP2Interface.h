/*
* Copyright 2008 Free Software Foundation, Inc.
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



#ifndef SIP2INTERFACE_H
#define SIP2INTERFACE_H

#include <Interthread.h>
#include <Sockets.h>

#include <string>
#include <GSML3CCElements.h>
#include "SIPMessage.h"
#include "SIPDialog.h"
#include "SIPTransaction.h"

#define PAT_TEST_SIP_DIRECT 0



namespace SIP {
using namespace std;

static const string cInviteStr("INVITE");

extern void SIPInterfaceStart();
extern void printDialogs(std::ostream&os);

typedef std::map<std::string,SipTransaction*> TUMap_t;

// We need two separate maps: one for requests and one for replies.
// For request matching we use SipDialogMap.  We do not need to recognize in-dialog request repeats because
// we always just generate an immediate response for those anyway.  So it only needs to map INVITEs.
// For reply matching we use SipTUMap, cf.
class SipDialogMap {
	//Mutex mDialogMapLock;
	typedef InterthreadMap1<string,SipDialogRef> DialogMap_t;
	DialogMap_t mDialogMap;
	string makeTagKey(string callid, string localTag);
	public:
	SipDialogRef findDialogByMsg(SipMessage *msg);
	void dmAddCallDialog(SipDialog*dialog);
	void dmAddLocalTag(SipDialog*dialog);
	void printDialogs(ostream&os);
	void dmPeriodicService();
	bool dmRemoveDialog(SipBase *dialog);
	//SipBase *dmFindDialogById(unsigned id);
	SipDialogRef dmFindDialogByRtp(RtpSession *session);
};

// Match incoming replies to the TU that made the outgoing request.
// The things saved here are SIP 'Transaction Users' as defined in RFC3261.
// The TransactionUsers are the layer that sends/receives original messages and has some behavior related to our application.
// RFC3261 17.1.3: When a response is received we are supposed find the client transaction using the branch in the top via.
// Unfortunately, as of 6/2013 our SR is non-compliant.  It adds an extra via with a branch of "1".  Gotta love that.
// An alternate way to find the transaction is by the CSeq.  So why is there a branch at all?  For reasons
// that do not apply to us, namely 1.  The branch each proxy to have its own private ids
// 2.  On the SIP Transaction Server side, the same request may arrive multiple times by multiple
// paths through the intermediate SIP proxies, and those requests can be distinguished by branch, but in our case we
// would want to view those as multiple identical requests and respond to all but the first with a repeated request error.
class SipTUMap {
	InterthreadMap<string,SipTransaction> mTUMap;
	// (pat 7-23-2013) We are supposed to use the via-branch to identify the transaction, but unfortunately
	// sipauthserve is non-compliant and does not return it.
	string tuMakeKey(string callid, string method, int seqnum) {
		if (method == "ACK") { method = cInviteStr; }	// Irrelevant, since we dont use TUs for ACK.
		return format("%s %s %d",callid,method,seqnum);
	}
	string tuMakeKey(SipMessage *msg) { return tuMakeKey(msg->msmCallId, msg->msmCSeqMethod, msg->msmCSeqNum); }
	string tuMakeKey(SipTransaction*tup) { return tuMakeKey(tup->mstCallId,tup->mstMethod,tup->mstSeqNum); }

	public:
	void tuMapAdd(SipTransaction*tup);
	void tuMapRemove(SipTransaction*tup, bool /*whine*/=true);
	void tuMapPeriodicService();
	// Attempt to dispatch an incoming SIP message to a TU; return true if a transaction wanted this message.
	// This has to be locked so someone doesnt delete the TU between the time we get its pointer
	// and send it the message.
	bool tuMapDispatch(SipMessage*msg);
};


class SipInterface
{
	char mReadBuffer[MAX_UDP_LENGTH+500];		///< buffer for UDP reads.  The +500 is way overkill.

	UDPSocket *mSIPSocket;
	Mutex mSocketLock;

	// SIP Message CSeq numbers for initial out-of-dialog transcations, which includes the initial INVITE:
	// We want these to be unique, and the easiest way is to increment a counter.
	unsigned mMessageCSeqNum;
	unsigned mInfoCSeqNum;	// This is for INFO messages outside a dialog.  Inside a dialog they must use dsNextCSeq()
	unsigned mInviteCSeqNum;	// For the initial invite, then in-invite numbers advance from there.

	public:
	SipInterface() {
		// sipauthserve appears to have a bug that it does not properly differentiate messages
		// from different BTS, possibly only if they have the same IP address, but in any case,
		// using random numbers to init makes encountering that bug quite rare.
		mMessageCSeqNum = random() & 0xfffff;
		mInfoCSeqNum = random() & 0xfffff;	// Any old number will do.
		mInviteCSeqNum = random() & 0xfffff;	// Any old number will do.
	}

	void siWrite(const struct sockaddr_in*, SipMessage *);	// new
	/** Start the SIP drive loop. */
	void siDrive2();
	void siInit();
	virtual void newDriveIncoming(char *content) = 0;

	unsigned nextMessageCSeqNum() { return ++mMessageCSeqNum; }
	unsigned nextInfoCSeqNum() { return ++mInfoCSeqNum; }
	unsigned nextInviteCSeqNum() { return ++mInviteCSeqNum; }
};

class MySipInterface  : public SipDialogMap, public SipTUMap, public SipInterface
{

public:
	Thread mDriveThread;	
	Thread mPeriodicServiceThread;	

	// (pat) Formerly all downlink SIP messages were added to the FIFO from which they were read via polling.
	// Now messages are delivered to the SipDialog state machine, which sends SIP responses immediately, and
	// queues any L3 bound messages as DialogMessages into a queue destined for L3 processing,
	// whence the messages will be sent to the TransactionEntries.
	// How many SIPEngine/SipDialog objects are there?
	// Formerly (GSM only, prior to UMTS and L3 rewrite) there could be only one SIPEngine per LogicalChannel;
	// each TCH might have a SIPEngine in the TransactionEntry used for voice traffic, and each SDDCH/FACCH might have a SIPEngine
	// used for registration.  Each LogicalChannel was driven by a separate thread to make the polling scheme work.
	// Now for GSM there can be at least two TransactionEntries per MS or UE (for call-hold/waiting),
	// and in UMTS the maximum number of UEs is dependent on the spreading factor used for voice calls, maybe 128.
	// Update: There is really no limit on the number of simultaneous SIPEngines and TransactionEntries, because
	// we start a new one for each inbound SMS or voice call; if there are too many they will get destroyed
	// in the upcoming connection-management layer.

	// DialogMap maps the SIP callid to the SipDialog state machine that runs the state machine.
	// The SIPEngine class doesnt contain a state machine, but SipDialog does.
	typedef ThreadSafeList<SipDialogRef> DeadDialogListType;
	DeadDialogListType mDeadDialogs;

	/**
		Create the SIP interface to watch for incoming SIP messages.
	*/
	MySipInterface()
		// (pat) Dont do this! There is a constructor race between SIPInterface and ConfigurationTable needed by gConfig.
		// :mSIPSocket(gConfig.getNum("SIP.Local.Port"))
	{
	}

	
	void msiInit();

	/** Receive, parse and dispatch a single SIP message. */
	void newDriveIncoming(char *content);
	void purgeDeadDialogs();

	/**
		Look for incoming INVITE messages to start MTC.
		@param msg The SIP message to check.
		@return true if the message is a new INVITE
	*/
	void handleInvite(SipMessage *msg,bool isINVITE);
	bool newCheckInvite(SipMessage *msg);
	bool checkTU(SipMessage *msg);

	/**
		Send an error response before a transaction is even created.
	*/
	void newSendEarlyError(SipMessage *cause, int code, const char * reason);
};


/*@addtogroup Globals */
//@{
/** A single global SIPInterface in the global namespace. */
extern MySipInterface gSipInterface;
//@}

}; // namespace SIP.

#endif // SIP2INTERFACE_H
// vim: ts=4 sw=4
