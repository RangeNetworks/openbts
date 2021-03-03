/**@file Idle-mode dispatcher for dedicated control channels. */

/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
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


#define LOG_GROUP LogGroup::Control


#include "ControlCommon.h"
//#include "L3CallControl.h"
#include "L3MobilityManagement.h"
#include "L3StateMachine.h"
#include "L3LogicalChannel.h"
#include <GSMConfig.h>

#include <Logger.h>
#undef WARNING
#include <Reporting.h>
#include <Globals.h>

using namespace std;
using namespace GSM;
using namespace Control;




/** Example of a closed-loop, persistent-thread control function for the DCCH. */
// (pat) DCCH is a TCHFACCHLogicalChannel or SDCCHLogicalChannel
void Control::DCCHDispatcher(L3LogicalChannel *DCCH)
{
	while (! gBTS.btsShutdown()) {
		// This 'try' is redundant, but we are ultra-cautious here since a mistake means a crash.
		try {
			// Wait for a transaction to start.
			LOG(DEBUG);
			LOG(DEBUG) << "waiting for " << *DCCH << " ESTABLISH or HANDOVER_ACCESS";
			L3Frame *frame = DCCH->waitForEstablishOrHandover();
			LOG(DEBUG) << *DCCH << " received " << *frame;
			gResetWatchdog();
			L3DCCHLoop(DCCH,frame); // This will not return until the channel is released.
		}
		catch (...) {
			LOG(ERR) << "channel killed by unexpected exception ";
		}

#if 0
		// Catch the various error cases.

		catch (RemovedTransaction except) {
			LOG(ERR) << "attempt to use removed transaciton " << except.transactionID();
		}
		catch (ChannelReadTimeout except) {
			LOG(NOTICE) << "ChannelReadTimeout";
			// Cause 0x03 means "abnormal release, timer expired".
			DCCH->l2sendm(L3ChannelRelease((RRCause)0x03));
			gTransactionTable.remove(except.transactionID());
		}
		catch (UnexpectedPrimitive except) {
			LOG(NOTICE) << "UnexpectedPrimitive";
			// Cause 0x62 means "message type not not compatible with protocol state".
			DCCH->l2sendm(L3ChannelRelease((RRCause)0x62));
			if (except.transactionID()) gTransactionTable.remove(except.transactionID());
		}
		catch (UnexpectedMessage except) {
			LOG(NOTICE) << "UnexpectedMessage";
			// Cause 0x62 means "message type not not compatible with protocol state".
			DCCH->l2sendm(L3ChannelRelease((RRCause)0x62));
			if (except.transactionID()) gTransactionTable.remove(except.transactionID());
		}
		catch (UnsupportedMessage except) {
			LOG(NOTICE) << "UnsupportedMessage";
			// Cause 0x61 means "message type not implemented".
			DCCH->l2sendm(L3ChannelRelease((RRCause)0x61));
			if (except.transactionID()) gTransactionTable.remove(except.transactionID());
		}
		catch (Q931TimerExpired except) {
			LOG(NOTICE) << "Q.931 T3xx timer expired";
			// Cause 0x03 means "abnormal release, timer expired".
			// TODO -- Send diagnostics.
			DCCH->l2sendm(L3ChannelRelease((RRCause)0x03));
			if (except.transactionID()) gTransactionTable.remove(except.transactionID());
		}
		catch (SIP::SIPTimeout except) {
			// FIXME -- The transaction ID should be an argument here.
			LOG(WARNING) << "Uncaught SIPTimeout, will leave a stray transcation";
			// Cause 0x03 means "abnormal release, timer expired".
			DCCH->l2sendm(L3ChannelRelease((RRCause)0x03));
			if (except.transactionID()) gTransactionTable.remove(except.transactionID());
		}
		catch (SIP::SIPError except) {
			// FIXME -- The transaction ID should be an argument here.
			LOG(WARNING) << "Uncaught SIPError, will leave a stray transcation";
			// Cause 0x01 means "abnormal release, unspecified".
			DCCH->l2sendm(L3ChannelRelease((RRCause)0x01));
			if (except.transactionID()) gTransactionTable.remove(except.transactionID());
		}
#endif
	}
}




// vim: ts=4 sw=4
