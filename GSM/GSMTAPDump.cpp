/*
* Copyright 2008, 2009 Free Software Foundation, Inc.
*

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

* This software is distributed under multiple licenses; see the COPYING file in the main directory for licensing information for this specific distribuion.
*/


#include "GSMTAPDump.h"
#include "GSMTransfer.h"
#include <Sockets.h>
#include <Globals.h>

UDPSocket GSMTAPSocket;

void gWriteGSMTAP(unsigned ARFCN, unsigned TS, unsigned FN,
                  GSM::TypeAndOffset to, bool is_saach,
				  bool ul_dln,	// (pat) This flag means uplink
                  const BitVector& frame,
				  unsigned wType)	// Defaults to GSMTAP_TYPE_UM
{
	char buffer[MAX_UDP_LENGTH];
	int ofs = 0;

	// Check if GSMTap is enabled
	if (!gConfig.defines("Control.GSMTAP.TargetIP")) return;

	// Port configuration
	unsigned port = GSMTAP_UDP_PORT;	// default port for GSM-TAP
	if (gConfig.defines("Control.GSMTAP.TargetPort"))
		port = gConfig.getNum("Control.GSMTAP.TargetPort");

	// Set socket destination
	GSMTAPSocket.destination(port,gConfig.getStr("Control.GSMTAP.TargetIP").c_str());

	// Decode TypeAndOffset
	uint8_t stype, scn;

	switch (to) {
		case GSM::TDMA_BEACON_BCCH:
			stype = GSMTAP_CHANNEL_BCCH;
			scn = 0;
			break;

		case GSM::TDMA_BEACON_CCCH:
			stype = GSMTAP_CHANNEL_CCCH;
			scn = 0;
			break;

		case GSM::SDCCH_4_0:
		case GSM::SDCCH_4_1:
		case GSM::SDCCH_4_2:
		case GSM::SDCCH_4_3:
			stype = GSMTAP_CHANNEL_SDCCH4;
			scn = to - GSM::SDCCH_4_0;
			break;

		case GSM::SDCCH_8_0:
		case GSM::SDCCH_8_1:
		case GSM::SDCCH_8_2:
		case GSM::SDCCH_8_3:
		case GSM::SDCCH_8_4:
		case GSM::SDCCH_8_5:
		case GSM::SDCCH_8_6:
		case GSM::SDCCH_8_7:
			stype = GSMTAP_CHANNEL_SDCCH8;
			scn = to - GSM::SDCCH_8_0;
			break;

		case GSM::TCHF_0:
			stype = GSMTAP_CHANNEL_TCH_F;
			scn = 0;
			break;

		case GSM::TCHH_0:
		case GSM::TCHH_1:
			stype = GSMTAP_CHANNEL_TCH_H;
			scn = to - GSM::TCHH_0;
			break;

		case GSM::TDMA_PDCH:	// packet data traffic logical channel, full speed.
			stype = GSMTAP_CHANNEL_PACCH;
			//stype = GSMTAP_CHANNEL_PDCH;
			scn = 0;	// Is this correct?
			break;
		case GSM::TDMA_PTCCH:		// packet data timing advance logical channel
			stype = GSMTAP_CHANNEL_PTCCH;
			scn = 0;
			break;
		case GSM::TDMA_PACCH:
			stype = GSMTAP_CHANNEL_PACCH;
			scn = 0;
			break;
		default:
			LOG(NOTICE) << "GSMTAP unsupported type-and-offset " << to;
			stype = GSMTAP_CHANNEL_UNKNOWN;
			scn = 0;
			break;
	}

	if (is_saach)
		stype |= GSMTAP_CHANNEL_ACCH;

	// Flags in ARFCN
	if (gConfig.getNum("GSM.Radio.Band") == 1900)
		ARFCN |= GSMTAP_ARFCN_F_PCS;

	if (ul_dln)
		ARFCN |= GSMTAP_ARFCN_F_UPLINK;

	// Build header
	struct gsmtap_hdr *header = (struct gsmtap_hdr *)buffer;
	header->version			= GSMTAP_VERSION;
	header->hdr_len			= sizeof(struct gsmtap_hdr) >> 2;
	header->type			= wType;	//GSMTAP_TYPE_UM;
	header->timeslot		= TS;
	header->arfcn			= htons(ARFCN);
	header->signal_dbm		= 0; /* FIXME */
	header->snr_db			= 0; /* FIXME */
	header->frame_number	= htonl(FN);
	header->sub_type		= stype;
	header->antenna_nr		= 0;
	header->sub_slot		= scn;
	header->res				= 0;

	ofs += sizeof(*header);

	// Add frame data
	frame.pack((unsigned char*)&buffer[ofs]);
	ofs += (frame.size() + 7) >> 3;

	// Write the GSMTAP packet
	GSMTAPSocket.write(buffer, ofs);
}



// vim: ts=4 sw=4
