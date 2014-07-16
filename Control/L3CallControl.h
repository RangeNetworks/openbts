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

#ifndef _L3CALLCONTROL_H_
#define _L3CALLCONTROL_H_ 1

#include "L3StateMachine.h"
#include <GSMCommon.h>
#include <GSML3Message.h>

namespace Control {
void startMOC(const GSM::L3MMMessage *l3msg, MMContext *dcch, L3CMServiceType::TypeCode serviceType);
void initMTC(TranEntry *tran);
void startInboundHandoverMachine(TranEntry *tran);

};	// namespace

#endif
