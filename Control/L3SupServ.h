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

#ifndef _L3SUPSERV_H_
#define _L3SUPSERV_H_ 1


#include "ControlCommon.h"
#include "L3StateMachine.h"
#include <GSML3MMMessages.h>

namespace Control {

// The base class for SS [Supplementary Services]
class SSDBase : public MachineBase {
	protected:
	MachineStatus handleSSMessage(const GSM::L3Message*ssmsg);
	SSDBase(TranEntry *wTran) : MachineBase(wTran) {}
};

void startMOSSD(const GSM::L3CMServiceRequest*cmsrq, MMContext *mmchan);
void initMTSS(TranEntry *tran);
string ssMap2Ussd(const unsigned char *mapcmd,unsigned maplen);

};
#endif
