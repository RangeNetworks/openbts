/*
* Copyright 2008, 2014 Free Software Foundation, Inc.
* Copyright 2014 Range Networks, Inc.
*

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribution.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.
*/


#define LOG_GROUP LogGroup::SMS

#include <BitVector.h>
#include "SMSTransfer.h"
#include "SMSMessages.h"

using namespace std;
using namespace SMS;




#if UNUSED_PRIMITIVE
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
		case SMS_UNDEFINED_PRIMITIVE: os<<"undefined "; break;

	}
	return os;
}
#endif

void RLFrame::text(std::ostream& os) const
{
#if UNUSED_PRIMITIVE
	os<<"primitive="<<primitive();
#endif
	if (size() >= 16) {
		os <<LOGVAR2("MTI",MTI());
		switch (MTI()) {
			case 0: case 1: os << " (RP-DATA)";
				try {
					// GSM 4.11 7.3.1
					size_t rp = 16;
					RPData rpdata;
					rpdata.parseBody(*this,rp);
					rpdata.text(os);
					//os << "RP-Originator=("; rpdata.mOriginator.text(os); os << ")";
					//os << "RP-Destination=("; rpdata.mDestination.text(os); os << ")";
					// The rpdata.mUserData.mTPDU is a TLFrame, which we must parse into one of the messages in GSM 3.40 9.2
					TLMessage *tlmsg = parseTPDU(rpdata.mUserData.mTPDU);
					if (tlmsg) {
						os << " TLMessage=(";
						tlmsg->text(os);
						os << ")";
						delete tlmsg;
					}
#if 0
					int RPOriginatorAddressLength = 8*readField(rp,8);
					if (rp + RPOriginatorAddressLength > size()) {
						os << " out-of-bounds"<<LOGVAR(RPOriginatorAddressLength) << " remaining:"<<size(); 
						break;
					}
					const BitVector2 RPOriginatorAddress ( segment(rp,RPOriginatorAddressLength));
					os <<LOGVAR(rp)<<" bitvector:"<<RPOriginatorAddress;
					os << " orig:"<<inspect()<<" "<<peekField(rp,8) << " copy:"<<RPOriginatorAddress.inspect()<<" "<<RPOriginatorAddress.peekField(0,8);
					os << " RP-OriginatorAddress=("; RPOriginatorAddress.hex(os); os <<")";
					rp += RPOriginatorAddressLength;

					int RPDestinationAddressLength = 8*readField(rp,8);	// In network->MS direction this is zero.
					if (rp + RPDestinationAddressLength > size()) {
						os << " out-of-bounds"<<LOGVAR(RPDestinationAddressLength) << " remaining:"<<size();
						break;
					}
					const BitVector RPDestinationAddress = segment(rp,RPDestinationAddressLength);
					os << " RP-DestinationAddress=("; RPDestinationAddress.hex(os); os <<")";
					rp += RPDestinationAddressLength;

					int RPUserDataLength = 8*readField(rp,8);
					if (rp + RPUserDataLength > size()) {
						os << " out-of-bounds"<<LOGVAR(RPUserDataLength) << " remaining:"<<size();
						break;
					}
					const BitVector2 RPUserData = segment(rp,RPUserDataLength);
					rp += RPUserDataLength;
					TLFrame tpdu(RPUserData);	// GSM 4.11 8.2.5.3
					os << " TPDU=("; os << tpdu; os << ")";
#endif
				} catch (...) {
					os << " (error parsing rp-data) ";
				}
				break;
			case 2: case 3: os << " (RP-ACK)"; break;
			case 4: case 5:
				os << " (RP-ERROR)";
				os << " cause="<<RPErrorCause();
				break;
			case 6: case 7: os << " (RP-SMMA)"; break;
		}
		os <<LOGVAR2("reference",reference());
	}

	os<<" data=(";
	hex(os);
	os<< ")";
}

ostream& SMS::operator<<(ostream& os, const RLFrame& msg)
{
	msg.text(os);
	return os;
}

ostream& SMS::operator<<(ostream& os, const TLFrame& msg)
{
#if UNUSED_PRIMITIVE
	os<<"primitive="<<msg.primitive();
#endif
	if (msg.size() >= 8) { os <<LOGVAR2("MTI",msg.MTI()); }
	os<<" data=(";
	msg.hex(os);
	os<< ")";
	return os;
}



