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


#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#include <signal.h>
#include <stdlib.h>

#include <ortp/ortp.h>
#include <osipparser2/osip_md5.h>
#include <osipparser2/sdp_message.h>

#include "SIPInterface.h"
#include "SIPUtility.h"


using namespace SIP;
using namespace std;



#define DEBUG 1





bool SIP::get_owner_ip( osip_message_t * msg, char * o_addr )
{
	osip_body_t * sdp_body = (osip_body_t*)osip_list_get(&msg->bodies, 0);
	if (!sdp_body) return false;
	char * sdp_str = sdp_body->body;
	if (!sdp_str) return false;

	sdp_message_t * sdp;
	sdp_message_init(&sdp);
	sdp_message_parse(sdp, sdp_str);
	strcpy(o_addr, sdp->o_addr);
	return true;
}

bool SIP::get_rtp_params(const osip_message_t * msg, char * port, char * ip_addr )
{
	osip_body_t * sdp_body = (osip_body_t*)osip_list_get(&msg->bodies, 0);
	if (!sdp_body) return false;
	char * sdp_str = sdp_body->body;
	if (!sdp_str) return false;

	sdp_message_t * sdp;
	sdp_message_init(&sdp);
	sdp_message_parse(sdp, sdp_str);

	strcpy(port,sdp_message_m_port_get(sdp,0));
	strcpy(ip_addr, sdp->c_connection->c_addr);
	return true;
}

void SIP::make_tag(char * tag)
{
	uint64_t r1 = random();
	uint64_t r2 = random();
	uint64_t val = (r1<<32) + r2;
	
	// map [0->26] to [a-z] 
	int k;
	for (k=0; k<16; k++) {
		tag[k] = val%26+'a';
		val = val >> 4;
	}
	tag[k] = '\0';
}

void SIP::make_branch( char * branch )
{
	uint64_t r1 = random();
	uint64_t r2 = random();
	uint64_t val = (r1<<32) + r2;
	sprintf(branch,"z9hG4bKobts28%llx", val);
}

/* get the return address from the SIP VIA header
   if port not available, guess at 5060
   if header not available, guess from SIP.Proxy.SMS 
   -kurtis 
*/
string SIP::get_return_address(osip_message_t * msg){

	string result;
	osip_via* via = (osip_via *) osip_list_get(&msg->vias,0);
	if (via){
		result = string(via->host);
		if (via->port) {
			result += string(":") + string(via->port);
		}
		else {
			result += string(":") + string("5060"); //assume SIP
		}
	}
	else { //no via header? Take best guess from conf -k
		result = gConfig.getStr("SIP.Proxy.SMS");
	}
	return result;
}

// vim: ts=4 sw=4
