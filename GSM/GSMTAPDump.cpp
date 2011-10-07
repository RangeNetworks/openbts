/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
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


#include "GSMTAPDump.h"
#include "GSMTransfer.h"
#include <Sockets.h>
#include <Globals.h>

UDPSocket GSMTAPSocket;

void gWriteGSMTAP(unsigned ARFCN, unsigned TS, unsigned FN, const GSM::L2Frame& frame)
{
	if (!gConfig.defines("GSMTAP.TargetIP")) return;

	unsigned port = GSMTAP_UDP_PORT;	// default port for GSM-TAP
	if (gConfig.defines("GSMTAP.TargetPort"))
		port = gConfig.getNum("GSMTAP.TargetPort");

	// Write a GSMTAP packet to the configured destination.
	GSMTAPSocket.destination(port,gConfig.getStr("GSMTAP.TargetIP").c_str());
	char buffer[MAX_UDP_LENGTH];
	gsmtap_hdr header(ARFCN,TS,FN);
	memcpy(buffer,&header,sizeof(header));
	frame.pack((unsigned char*)buffer+sizeof(header));
	GSMTAPSocket.write(buffer, sizeof(header) + frame.size()/8);
}



// vim: ts=4 sw=4
