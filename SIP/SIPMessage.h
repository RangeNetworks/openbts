/*
* Copyright 2008 Free Software Foundation, Inc.
* Copyright 2011 Range Networks, Inc.
*
* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

*/




#ifndef SIP_MESSAGE_H
#define SIP_MESSAGE_H

namespace SIP {



osip_message_t * sip_register( const char * sip_username, short timeout, short local_port, const char * local_ip, 
const char * proxy_ip, const char * from_tag, const char * via_branch, const char * call_id, int cseq,
string *RAND, const char *IMSI, const char *SRES);



osip_message_t * sip_message( const char * dialed_number, const char * sip_username, short local_port, const char * local_ip, const char * proxy_ip, const char * from_tag, const char * via_branch, const char * call_id, int cseq, const char* message, const char* content_type=NULL);

osip_message_t * sip_invite( const char * dialed_number, short rtp_port,const char * sip_username, short local_port, const char * local_ip, const char * proxy_ip, const char * from_tag, const char * via_branch, const char * call_id, int cseq, unsigned codec);

osip_message_t * sip_reinvite( const char* RemoteUsername, const char* RemoteIP, const char* SIPDisplayname, const char* SIPUsername, const char* fromTag, const char* fromUsername, const char* fromIP, const char* toTag, const char* toUsername, const char* toIP, const char* viaBranch, const char* callID, const char* callIP, int cseq, unsigned codec, short wRTPPort, const char* wSessionID, const char* wSessionVersion);

osip_message_t * sip_invite_referred( const char * dialed_number, short rtp_port,const char * sip_username, short local_port, const char * local_ip, const char * proxy_ip, const char * from_tag, const char * via_branch, const char * call_id, int cseq, unsigned codec);

osip_message_t * sip_ack( const char * req_uri, const char * dialed_number, const char * sip_username, short wlocal_port, const char * local_ip, const char * proxy_ip, const osip_from_t* from_header, const osip_to_t* to_header, const char * via_branch, const osip_call_id_t* call_id_header, int cseq);


osip_message_t * sip_bye( const char * req_uri, const char * dialed_number, const char * sip_username, short local_port, const char * local_ip, const char * proxy_ip, short proxy_port, const osip_from_t *from_header, const osip_to_t * to_header, const char * via_branch, const osip_call_id_t* call_id_header, int cseq);

osip_message_t * sip_cancel( osip_message_t * invite,  const char * host, const char * username, short  port);

osip_message_t * sip_okay_sdp( osip_message_t * inv, const char * sip_username, const char * local_ip, short wlocal_port, short rtp_port, unsigned audio_codecs );

osip_message_t * sip_okay( osip_message_t * inv, const char * sip_username, const char * local_ip, short wlocal_port);

osip_message_t * sip_error( osip_message_t * invite,  const char * host, const char * username, short port, short code, const char* reason);

osip_message_t * sip_info(unsigned info, const char *dialed_number, short rtp_port,const char * sip_username, short local_port, const char * local_ip, const char * proxy_ip, const char * from_tag, const char * via_branch, const osip_call_id_t* call_id_header, int cseq);

osip_message_t * sip_b_okay( osip_message_t * bye  );

osip_message_t * sip_trying( osip_message_t * invite, const char * sip_username, const char * local_ip);

osip_message_t * sip_ringing( osip_message_t * invite, const char * sip_username, const char * local_ip);



};
#endif

