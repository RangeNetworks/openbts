/*
* Copyright 2008 Free Software Foundation, Inc.
*

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.
*/



#include <BitVector.h>
#include "SMSTransfer.h"

using namespace std;
using namespace SMS;




ostream& SMS::operator<<(ostream& os, SMSPrimitive prim)
{
	switch(prim) {
	
		case SM_RL_DATA_REQ: os<<" SM-RL-DATA-REQ"; break;
		case SM_RL_DATA_IND: os<<" SM-RL-DATA-IND"; break;
		case SM_RL_MEMORY_AVAIL_IND: os<<" SM-RL-MEMORY-AVAIL-IND"; break;
		case SM_RL_REPORT_REQ: os<<" SM-RL-REPORT-REQ"; break;
		case SM_RL_REPORT_IND: os<<" SM-RL-REPORT-IND"; break;
		case MNSMS_ABORT_REQ: os<<" MNSMS-ABORT-REQ"; break;
		case MNSMS_DATA_IND: os<<" MNSMS-DATA-IND"; break;
		case MNSMS_DATA_REQ: os<<"MNSMS-DATA-REQ "; break;
		case MNSMS_EST_REQ: os<<"MNSMS-EST-REQ "; break;
		case MNSMS_EST_IND: os<<"MNSMS-EST-IND "; break;
		case MNSMS_ERROR_IND: os<<"MNSMS-ERROR-IND "; break;
		case MNSMS_REL_REQ: os<<"MNSMS-REL-REQ "; break;
		case UNDEFINED_PRIMITIVE: os<<"undefined "; break;

	}
	return os;
}

ostream& SMS::operator<<(ostream& os, const RLFrame& msg)
{
	os<<"primitive="<<msg.primitive();
	os<<" data=(";
	msg.hex(os);
	os<< ")";
	return os;
}

ostream& SMS::operator<<(ostream& os, const TLFrame& msg)
{
	os<<"primitive="<<msg.primitive();
	os<<" data=(";
	msg.hex(os);
	os<< ")";
	return os;
}



