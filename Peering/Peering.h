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


#ifndef PERRINGMESSAGES_H
#define PERRINGMESSAGES_H

#include <Interthread.h>
#include <Sockets.h>
#include <Timeval.h>
#include <Globals.h>
#include <Utils.h>
//#include <ControlTransfer.h>
#include <GSML3RRElements.h>


namespace Control {
class TranEntry;
class HandoverEntry;
};



namespace Peering {

typedef InterthreadQueue<char> _PeerMessageFIFO;

class PeerMessageFIFO : public _PeerMessageFIFO {

	private:

	Timeval mExpiration;

	public:

	PeerMessageFIFO()
		:_PeerMessageFIFO(),
		mExpiration(gConfig.getNum("GSM.Timer.T3103")*2)
	{ }

	~PeerMessageFIFO() { clear(); }

	bool expired() const { return mExpiration.passed(); }

};


class PeerMessageFIFOMap : public InterthreadMap<unsigned,PeerMessageFIFO> {

	public:

	void addFIFO(unsigned transactionID);

	void removeFIFO(unsigned transactionID);

	/** Returned C-string must be free'd by the caller. */
	char *readFIFO(unsigned transactionID, unsigned timeout);

	/** Makes a copy of the string into the FIFO with strdup. */
	void writeFIFO(unsigned transactionID, const char* msg);

};




class PeerInterface {

	private:

	UDPSocket mSocket;
	char mReadBuffer[2048];
	PeerMessageFIFOMap mFIFOMap;		///< one FIFO per active handover transaction

	Mutex mLock;

	volatile unsigned mReferenceCounter;

	Thread mServer1;
	Thread mServer2;

	/**
		Send a message repeatedly until the ACK arrives.
		@param transaction Carries the peer address and transaction ID.
		@param message The IND message to send.
		@erturn true on ack, false on timeout
	*/
	// (pat) Made this private and put the methods that use it in Peering.cpp
	bool sendUntilAck(const Control::HandoverEntry*, const char* message);
	/**
		Send a message on the peering interface.
		@param IP The IP address of the remote peer.
		@param The message to send.
	*/
	void sendMessage(const struct ::sockaddr_in* peer, const char* message);

	public:

	/** Initialize the interface.  */
	PeerInterface();

	/** Start the service loops. */
	void start();

	/** service loops. */
	void* serviceLoop1(void*);
	void* serviceLoop2(void*);

	void sendNeighborParamsRequest(const struct ::sockaddr_in* peer);

	/** Remove a FIFO with the given transaction ID. */
	void removeFIFO(unsigned transactionID) { mFIFOMap.removeFIFO(transactionID); }



	void drive();

	/**
		Parse and dispatch an inbound message.
	*/
	void parse(const struct ::sockaddr_in* peer, const char* message);


	/**
		Parse and process an inbound message.
		@param peer The source address.
		@param message The message text.
	*/
	void process(const struct ::sockaddr_in* peer, const char* message);

	//@{

	/** Process the NEIGHBOR_PARMS message/response. */
	void processNeighborParams(const struct ::sockaddr_in* peer, const char* message);	// Pre-3-2014 message format.

	/** Process REQ HANDOVER. */
	void processHandoverRequest(const struct ::sockaddr_in* peer, const char* message);

	/** Process RSP HANDOVER. */
	void processHandoverResponse(const struct ::sockaddr_in* peer, const char* message);

	/** Process IND HANDOVER_COMPLETE */
	void processHandoverComplete(const struct sockaddr_in* peer, const char* message);

	/** Process IND HANDOVER_FAILURE */
	void processHandoverFailure(const struct sockaddr_in* peer, const char* message);

	public:

	/** Send IND HANDOVER_COMPLETE */
	void sendHandoverComplete(const Control::HandoverEntry* hop);

	/** Send IND HANDOVER_FAILURE */
	void sendHandoverFailure(const Control::HandoverEntry *hop,GSM::RRCause cause,unsigned holdoff);

	/** Send REQ HANDOVER */
	bool sendHandoverRequest(string peer, const RefCntPointer<Control::TranEntry> tran, string cause);

	//@}
};


extern string sockaddr2string(const struct sockaddr_in* peer, bool noempty);

}; //namespace

extern Peering::PeerInterface gPeerInterface;

#endif


