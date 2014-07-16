/**@file Declarations for common-use control-layer functions. */
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



#ifndef CONTROLCOMMON_H
#define CONTROLCOMMON_H


#include <stdio.h>
#include <list>

#include <Logger.h>
//#include <Interthread.h>
#include <Timeval.h>


//#include <GSML3CommonElements.h>
//#include <GSML3MMElements.h>
//#include <GSML3CCElements.h>
//#include <GSML3RRMessages.h>
//#include <SIPEngine.h>

//#include "TMSITable.h"
#include <ControlTransfer.h>

// These are macros for manufacturing states for state machines.
// Since we are using C++, they cannot be classes if we want to use switch().
// The pd selects an address space for each group of messages, whose mti must <256.
// The pd might be: L3CallControlPD, L3MobilityManagementPD, L3RadioResourcePD, etc.
// Note we overload pd==0 (Group Call Control) for naked states in the state machines, which is ok because
// we dont support Group Call and even if we did, the Group Call MTIs (GSM 04.68 9.3) will probably not collide.
// We use higher unused pds for our own purposes: 16 is for SIP Dialog messages, 17 for L3Frame primitives.
#define L3CASE_RAW(pd,mti) (((pd)<<8)|(mti))
#define L3CASE_CC(mti) L3CASE_RAW(L3CallControlPD,L3CCMessage::mti)			// pd == 3
#define L3CASE_MM(mti) L3CASE_RAW(L3MobilityManagementPD,L3MMMessage::mti)		// pd == 5
#define L3CASE_RR(mti) L3CASE_RAW(L3RadioResourcePD,L3RRMessage::mti)			// pd == 6
#define L3CASE_SS(mti) L3CASE_RAW(L3NonCallSSPD,L3SupServMessage::mti)			// pd == 6
#define L3CASE_SMS(mti) L3CASE_RAW(L3SMSPD,CPMessage::mti)			// pd == 9
#define L3CASE_PRIMITIVE(prim) L3CASE_RAW(17,prim)
// L3CASE_DIALOG_STATE is for a raw dialog state number, and L3CASE_SIP is for a dialog state name.
#define L3CASE_DIALOG_STATE(sipDialogState) L3CASE_RAW(16,sipDialogState)	// SIP messages use pd == 16, which is unused by GSM.
#define L3CASE_SIP(dialogStateName) L3CASE_DIALOG_STATE(SIP::DialogState::dialogStateName)
//unused: #define L3CASE_ERROR L3CASE_RAW(18,0)


// Enough forward refs to prevent "kitchen sink" includes and circularity.

namespace GSM {
class L3Message;
class SDCCHLogicalChannel;
class SACCHLogicalChannel;
class TCHFACCHLogicalChannel;
class L3CMServiceRequest;
};


/**@namespace Control This namepace is for use by the control layer. */
namespace Control {

class TranEntry;
class NewTransactionTable;
class MMContext;
class MMUser;
class L3LogicalChannel;

/**@name Call control time-out values (in ms) from ITU-T Q.931 Table 9-1 and GSM 04.08 Table 11.4. */
//@{
#ifndef RACETEST
const unsigned T301ms=60000;		///< recv ALERT --> recv CONN
const unsigned T302ms=12000;		///< send SETUP ACK --> any progress
const unsigned T303ms=30000;		///< send SETUP --> recv CALL CONF or REL COMP
const unsigned T304ms=20000;		///< recv SETUP ACK --> any progress
const unsigned T305ms=30000;		///< send DISC --> recv REL or DISC
const unsigned T308ms=30000;		///< send REL --> rev REL or REL COMP
const unsigned T310ms=30000;		///< recv CALL CONF --> recv ALERT, CONN, or DISC
const unsigned T313ms=30000;		///< send CONNECT --> recv CONNECT ACK
const unsigned T3270ms=12000;		///< send IdentityRequest -> recv IdentityResponse
const unsigned TR1Mms=40000;			///< MO-SMS timer.  3GPP 4.11 10.0 says: 35s < TR1M < 45s
const unsigned TR2Mms=15000;			///< MT-SMS timer.  3GPP 4.11 10.0 says: 12s < TR2M < 20s
#else
// These are reduced values to force testing of poor network behavior.
const unsigned T301ms=18000;		///< recv ALERT --> recv CONN
const unsigned T302ms=1200;		///< send SETUP ACK --> any progress
const unsigned T303ms=400;			///< send SETUP --> recv CALL CONF or REL COMP
const unsigned T304ms=2000;		///< recv SETUP ACK --> any progress
const unsigned T305ms=3000;		///< send DISC --> recv REL or DISC
const unsigned T308ms=3000;		///< send REL --> rev REL or REL COMP
const unsigned T310ms=3000;		///< recv CALL CONF --> recv ALERT, CONN, or DISC
const unsigned T313ms=3000;		///< send CONNECT --> recv CONNECT ACK
const unsigned T3270ms=12000;		///< send IdentityRequest -> recv IdentityResponse
const unsigned TR1Mms=40000;			///< MO-SMS timer. 3GPP 4.11 10.0 says: 35s < TR1M < 45s
const unsigned TR2Mms=15000;			///< MT-SMS timer. 3GPP 4.11 10.0 says: 12s < TR1M < 20s
#endif
//@}

// These enums are here because they need to be in a file included before others.
// Mobility Management Sublayer 4.08 4.1
// The primary purpose is to set between CM Call Management requests and RR channel allocation.
// There can be multiple simultaneous requests and MMLayer mediates them.
// The MM sublayer is not an exposed entity in the sense that nobody ever sees it or this state.
// The official Mobility Management states defined on the Network Side are 4.08 4.1.2.3.
	//1. IDLE The MM sublayer is not active except possibly when the RR sublayer is in Group Receive mode.
	//2. WAIT FOR RR CONNECTION aka paging.
	//3. MM CONNECTION ACTIVE The MM sublayer has a RR connection to a mobile station. One or more MM connections are active, or no
	//	MM connection is active but an RRLP procedure is ongoing.
	//4. IDENTIFICATION INITIATED The identification procedure has been started by the network. The timer T3270 is running.
	//5. AUTHENTICATION INITIATED The authentication procedure has been started by the network. The timer T3260 is running.
	//6. TMSI REALLOCATION INITIATED The TMSI reallocation procedure has been started by the network. The timer T3250 is running.
	//7. CIPHERING MODE INITIATED The cipher mode setting procedure has been requested to the RR sublayer.
	//8a. WAIT FOR MOBILE ORIGINATED MM CONNECTION A CM SERVICE REQUEST message is received and processed, and the MM sublayer
	//	awaits the "opening message" of the MM connection.
	// 9. WAIT FOR REESTABLISHMENT The RR connection to a mobile station with one or more active MM connection has been lost.
	//	The network awaits a possible re-establishment request from the mobile station.
// We use our own MM states that are only vaguely related.
// The spec assumes that the MM entity and CM entity are separate, but we run all procedures on top of a TransactionEntry object,
// so we can combine some MM states.
enum MMState {
	MMStateUndefined,
	MMStatePaging,		// aka "2. WAIT FOR CONNECTION"
	MMStateActive,		// aka "3. MM CONNECTION ACTIVE"
			// In our case this means that we are not planning to change the channel either.
	MMStateChannelChange,	// We are busy changing the channel, eg, SDCCH to TCH, which blocks everything.
	MMBlocked,	// blocked for LUR, Identification or Authentication in progress, either on this IMSI (blocked for sure)
				// or on this TMSI (speculatively blocked.)  The outcome of the procedure may determine whether
				// we keep or discard this MMUser and any pending CM requests.
};


// The MM reasons that may cause a call to end.
//enum MMCause {
//	MMNormalEvent,		// Means the MMLayer can try something else on this MS.
//	MMCongestion,
//	MMImsiDetach,	// Means the MMLayer should terminate everything on this MS.
//	MMFailure,		// Authenticaion/id or other major failure.  terminate everything
//};




/**@name Dispatch controllers for specific channel types. */
//@{
//void FACCHDispatcher(GSM::TCHFACCHLogicalChannel *TCHFACCH);
//void SDCCHDispatcher(GSM::SDCCHLogicalChannel *SDCCH);
void DCCHDispatcher(L3LogicalChannel *DCCH);
//@}




/**
	SMSCB sender function
*/
void *SMSCBSender(void*);





/**@name Control-layer exceptions. */
//@{

/**
	A control layer excpection includes a pointer to a transaction.
	The transaction might require some clean-up action, depending on the exception.
*/
class ControlLayerException : public std::exception {

	private:

	unsigned mTransactionID;

	public:

	ControlLayerException(unsigned wTransactionID=0)
		:mTransactionID(wTransactionID)
	{
		LOG(INFO) << "ControlLayerException";
	}

	unsigned transactionID() { return mTransactionID; }
};

/** Thrown when the control layer gets the wrong message */
class UnexpectedMessage : public ControlLayerException {
	public:
	UnexpectedMessage(unsigned wTransactionID=0)
		:ControlLayerException(wTransactionID)
	{
		LOG(INFO) << "UnexpectedMessage Exception";
	}
};

/** Thrown when recvL3 returns NULL */
class ChannelReadTimeout : public ControlLayerException {
	public:
	ChannelReadTimeout(unsigned wTransactionID=0)
		:ControlLayerException(wTransactionID)
	{}
};

/** Thrown when L3 can't parse an incoming message */
class UnsupportedMessage : public ControlLayerException {
	public:
	UnsupportedMessage(unsigned wTransactionID=0)
		:ControlLayerException(wTransactionID)
	{
		LOG(INFO) << "UnsupportedMessage Exception";
	}
};

/** Thrown when the control layer gets the wrong primitive */
class UnexpectedPrimitive : public ControlLayerException {
	public:
	UnexpectedPrimitive(unsigned wTransactionID=0)
		:ControlLayerException(wTransactionID)
	{
		LOG(INFO) << "UnexpectedPrimitive Exception";
	}
};

/**  Thrown when a T3xx expires */
class Q931TimerExpired : public ControlLayerException {
	public:
	Q931TimerExpired(unsigned wTransactionID=0)
		:ControlLayerException(wTransactionID)
	{}
};

/** Thrown if we touch a removed transaction. */
class RemovedTransaction : public ControlLayerException {
	public:
	RemovedTransaction(unsigned wTransactionID=0)
		:ControlLayerException(wTransactionID)
	{
		LOG(INFO) << "RemovedTransaction Exception";
	}
};


//@}



}	//Control



/**@addtogroup Globals */
//@{
/** A single global transaction table in the global namespace. */
extern Control::NewTransactionTable gNewTransactionTable;
//@}



#endif

// vim: ts=4 sw=4
