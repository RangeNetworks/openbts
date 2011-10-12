/**@file GSM/SIP Call Control -- GSM 04.08, ISDN ITU-T Q.931, SIP IETF RFC-3261, RTP IETF RFC-3550. */
/*
* Copyright 2008, 2009, 2010 Free Software Foundation, Inc.
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

#ifndef CALLCONTROL_H
#define CALLCONTROL_H


namespace GSM {
class LogicalChannel;
class TCHFACCHLogicalChannel;
class L3CMServiceRequest;
};

namespace Control {

class TransactionEntry;



/**@name MOC */
//@{
/** Run the MOC to the point of alerting, doing early assignment if needed. */
void MOCStarter(const GSM::L3CMServiceRequest*, GSM::LogicalChannel*);
/** Complete the MOC connection. */
void MOCController(TransactionEntry*, GSM::TCHFACCHLogicalChannel*);
/** Set up an emergency call, assuming very early assignment. */
void EmergencyCall(const GSM::L3CMServiceRequest*, GSM::LogicalChannel*);
//@}


/**@name MTC */
//@{
/** Run the MTC to the point of alerting, doing early assignment if needed. */
void MTCStarter(TransactionEntry*, GSM::LogicalChannel*);
/** Complete the MTC connection. */
void MTCController(TransactionEntry*, GSM::TCHFACCHLogicalChannel*);
//@}


/**@name Test Call */
//@{
/** Run the test call. */
void TestCall(TransactionEntry*, GSM::LogicalChannel*);
//@}

/** Create a new transaction entry and start paging. */
void initiateMTTransaction(TransactionEntry* transaction,
		GSM::ChannelType chanType, unsigned pageTime);


}


#endif
