/*
* Copyright 2008 Free Software Foundation, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/




#ifndef SIP_UTILITY_H
#define SIP_UTILITY_H


namespace SIP {

/**@name SIP-specific exceptions. */
//@{
class SIPException {
	protected:
	unsigned mTransactionID;

	public:
	SIPException(unsigned wTransactionID=0)
		:mTransactionID(wTransactionID)
	{ }

	unsigned transactionID() const { return mTransactionID; }
};

class SIPError : public SIPException {};
class SIPTimeout : public SIPException {};
//@}


/** Codec codes, from RFC-3551, Table 4. */
enum RTPCodec {
	RTPuLaw=0,
	RTPGSM610=3
};


/** Get owner IP address; return NULL if none found. */
bool get_owner_ip( osip_message_t * msg, char * o_addr );

/** Get RTP parameters; return NULL if none found. */
bool get_rtp_params(const osip_message_t * msg, char * port, char * ip_addr );

void make_tag( char * tag );

void make_branch(char * branch);


};
#endif
// vim: ts=4 sw=4
