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

#ifndef _L3SMSCONTROL_H_
#define _L3SMSCONTROL_H_

#include "L3LogicalChannel.h"
#include "ControlCommon.h"

namespace Control {

// 3GPP 4.11 5.2. These are the SMS states.  It is nearly empty.  The only one we care about is awaiting the final ack.
enum SmsState {
	SmsNonexistent,	// Not an SMS state; we use to mean there is no SMS transaction.
	MoSmsIdle,
	MoSmsWaitForAck,
	// In the spec this is a catch-all for any other state.  For us this is a transitory state we dont care about because
	// the TranEntry will be removed momentarily.
	MoSmsMMConnection,
};

void startMOSMS(const GSM::L3MMMessage *l3msg, MMContext *dcch);
void initMTSMS(TranEntry *tran);

};
#endif
