/*
* Copyright 2008 Free Software Foundation, Inc.
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



#ifndef SMS_TRANSFER_H
#define SMS_TRANSFER_H

#include <ostream>
#include <BitVector.h>
#include <GSMTransfer.h>

namespace SMS {




enum SMSPrimitive {

	// Relay layer primitives for network 
	//side of connection. GSM 04.11 Table 3 
	SM_RL_DATA_REQ=0,		// MT SMS-TPDU
	SM_RL_DATA_IND=1,		// MO SMS-TPDU	
	SM_RL_MEMORY_AVAIL_IND=2, 	// None (dont know it well ever use
	SM_RL_REPORT_REQ=3,
	SM_RL_REPORT_IND=4,

	// MNSMS service primitives on the network side,
	//as defined in GSM 04.11 3.2.2 table 3.2 
	MNSMS_ABORT_REQ=5, 	// Cause
	MNSMS_DATA_IND = 6,	// MO RPDU
	MNSMS_DATA_REQ = 7,	// MT RPDU 
	MNSMS_EST_REQ=8,	// MT RPDU
	MNSMS_EST_IND=9,	// MO RPDU
	MNSMS_ERROR_IND=10,	// Cause
	MNSMS_REL_REQ=11,	// Cause
	UNDEFINED_PRIMITIVE=-1
};


std::ostream& operator<<(std::ostream& os, SMSPrimitive);



class RLFrame : public GSM::L3Frame 
{	

	SMSPrimitive mPrimitive;	
	
	public:

	unsigned MTI() const { return peekField(5,3); }	

	unsigned reference() const { return peekField(8,8); }
	
	RLFrame(SMSPrimitive wPrimitive=UNDEFINED_PRIMITIVE, size_t len=0)
		:L3Frame(GSM::DATA,len), mPrimitive(wPrimitive)
	{ }

	RLFrame(const BitVector& source, SMSPrimitive wPrimitive=UNDEFINED_PRIMITIVE)
		:L3Frame(source), mPrimitive(wPrimitive)
	{ }

	SMSPrimitive primitive() const { return mPrimitive; }
};

std::ostream& operator<<(std::ostream& os, const RLFrame& );


class TLFrame : public GSM::L3Frame 
{

	SMSPrimitive mPrimitive;	
	
	public:

	unsigned MTI() const { return peekField(6,2); }	
	
	TLFrame(SMSPrimitive wPrimitive=UNDEFINED_PRIMITIVE, size_t len=0)
		:L3Frame(GSM::DATA,len), mPrimitive(wPrimitive)
	{ }

	TLFrame(const BitVector& source, SMSPrimitive wPrimitive=UNDEFINED_PRIMITIVE)
		:L3Frame(source), mPrimitive(wPrimitive)
	{ }

	SMSPrimitive primitive() const { return mPrimitive; }

};


std::ostream& operator<<( std::ostream& os, const TLFrame& );

};  //namespace SMS {

#endif 
 
