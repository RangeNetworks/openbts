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




#ifndef SIP_MESSAGE_H
#define SIP_MESSAGE_H

namespace SIP {



osip_message_t * sip_register( const char * sip_username, short timeout, short local_port, const char * local_ip, 
const char * proxy_ip, const char * from_tag, const char * via_branch, const char * call_id, int cseq);



osip_message_t * sip_message( const char * dialed_number, const char * sip_username, short local_port, const char * local_ip, const char * proxy_ip, const char * from_tag, const char * via_branch, const char * call_id, int cseq, const char* message, const char* content_type=NULL);

osip_message_t * sip_invite( const char * dialed_number, short rtp_port,const char * sip_username, short local_port, const char * local_ip, const char * proxy_ip, const char * from_tag, const char * via_branch, const char * call_id, int cseq, unsigned codec);

osip_message_t * sip_invite5031(short rtp_port,const char * sip_username, short local_port, const char * local_ip, const char* proxy_ip, const char * from_tag, const char * via_branch, const char * call_id, int cseq, unsigned codec);


osip_message_t * sip_ack( const char * req_uri, const char * dialed_number, const char * sip_username, short wlocal_port, const char * local_ip, const char * proxy_ip, const osip_from_t* from_header, const osip_to_t* to_header, const char * via_branch, const osip_call_id_t* call_id_header, int cseq);


osip_message_t * sip_bye( const char * req_uri, const char * dialed_number, const char * sip_username, short local_port, const char * local_ip, const char * proxy_ip, short proxy_port, const osip_from_t *from_header, const osip_to_t * to_header, const char * via_branch, const osip_call_id_t* call_id_header, int cseq);

osip_message_t * sip_cancel( osip_message_t * invite,  const char * host, const char * username, short  port);

osip_message_t * sip_okay_sdp( osip_message_t * inv, const char * sip_username, const char * local_ip, short wlocal_port, short rtp_port, unsigned audio_codecs );

osip_message_t * sip_okay( osip_message_t * inv, const char * sip_username, const char * local_ip, short wlocal_port);

osip_message_t * sip_temporarily_unavailable( osip_message_t * invite,  const char * host, const char * username, short  port);

osip_message_t * sip_info(unsigned info, const char *dialed_number, short rtp_port,const char * sip_username, short local_port, const char * local_ip, const char * proxy_ip, const char * from_tag, const char * via_branch, const osip_call_id_t* call_id_header, int cseq);

osip_message_t * sip_b_okay( osip_message_t * bye  );

osip_message_t * sip_trying( osip_message_t * invite, const char * sip_username, const char * local_ip);

osip_message_t * sip_ringing( osip_message_t * invite, const char * sip_username, const char * local_ip);



};
#endif

