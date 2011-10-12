/**@file Declarations for common-use control-layer functions. */
/*
* Copyright 2008-2011 Free Software Foundation, Inc.
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/



#ifndef SMSCONTROL_H
#define SMSCONTROL_H

#include <SMSMessages.h>

namespace GSM {
class L3Message;
class LogicalChannel;
class SDCCHLogicalChannel;
class SACCHLogicalChannel;
class TCHFACCHLogicalChannel;
class L3CMServiceRequest;
};


namespace Control {

/** MOSMS state machine.  */
void MOSMSController(const GSM::L3CMServiceRequest *req, GSM::LogicalChannel *LCH);

/** MOSMS-with-parallel-call state machine.  */
void InCallMOSMSStarter(TransactionEntry *parallelCall);

/** MOSMS-with-parallel-call state machine.  */
void InCallMOSMSController(const SMS::CPData *msg, TransactionEntry* transaction, GSM::SACCHLogicalChannel *LCH);
/**
	Basic SMS delivery from an established CM.
	On exit, SAP3 will be in ABM and LCH will still be open.
	Throws exception for failures in connection layer or for parsing failure.
	@return true on success in relay layer.
*/
bool deliverSMSToMS(const char *callingPartyDigits, const char* message, const char* contentType, unsigned TI, GSM::LogicalChannel *LCH);

/** MTSMS */
void MTSMSController(TransactionEntry* transaction, GSM::LogicalChannel *LCH);

}




#endif

// vim: ts=4 sw=4
