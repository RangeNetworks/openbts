/*
* Copyright 2011 Range Networks, Inc.
* All Rights Reserved.
*
* This software is distributed under multiple licenses;
* see the COPYING file in the main directory for licensing
* information for this specific distribuion.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/

#ifndef BSSG_H
#define BSSG_H
#include "Interthread.h"
#include "Defines.h"
#include "ByteVector.h"
#include "ScalarTypes.h"
#include "BSSGMessages.h"

// BSSG.cpp handles link layer communication to the SGSN.


namespace BSSG {

// GSM 08.16 describes tne NS layer.
// GSM 08.18 sec 10 describes the PDU messages that the SGSN can send to the BSS.
// GSM 48.018 is the updated spec with PS-HANDOVER related commands.
// Definitions:
// NSE - Network Service Entity.  There is one or more NSE inside the BSS for signaling.
// NSEI == Network Service Entity Indicator
// BVC = BSSGP Virtual Connection, transport peer-to-peer through the BTS.
// LSP = Link Selector Parameter, points to one MS, eg, the MS's TLLI.
// NSVCI = Network Service Virtual Connection Identifier.
// NSEI = Network Service Entity Identifier
// BVCI - BSSGP Virtual Connection ID. BVCI 0 = signaling, BVCI 1 = PTM (point-to-multipoint),
//	all other values are point-to-point.
//	GSM08.18sec5.4.1: "This parameter is not part of the BSSGP PDU across
//	the Gb interface, but is used by the network service entity across the Gb."
//	It corresponds to a cell, and can be used instead of routing area id
//	at operators discretion.
// LSP - Link Selector Parameter, something used only inside the BSS or SGSN,
//     		and not transmitted, to uniquely identify NS-VC.  We wont use it.

// Just so you dont have to read all the manuals, here is how it works:
// The NS and BSSG layers are for BTS to SGSN communication.
// The NS protocol is the inner-most layer.  This stuff was designed for frame relay,
// so there is an NSVCI which, despite the name, identifies a specific physical connection,
// and the NSEI, which identifies a group of NSVCIs that connect the BTS to the SGSN.
// The idea is you can take down individual NSVCs without taking down the complete communication link.
// You can also block/unblock individual NS connections.
// The next layer up is the BSSG layer.  Endpoints are identfied by BVCI.
// The first two BVCIs are reserved: BVCI 0 for signaling messages handled directly by the BSSG controller,
// BVCI 1 for point-to-multipoint messages, and all other BVCIs are for BTSs that share this BSSG+NS link.
// So in a normal system, multiple BTS could communicate over a single NSEI consisting of
// a group of NSVCI.  Each BTS would have a BVCI assigned,
// and the network connections have NSVCIs and NSEIs assigned.
// All this is entirely irrelevant to us, because our BTS talks to the SGSN using UDP/IP,
// and the SGSN actually identifies the connection by remembering the endpoint UDP/IP address
// of the BTS when it first calls in, which puts a hard limit of something like 2**48 BTS per SGSN,
// which should be enough.  (That was a joke.)
// However, we still use the NS and BSSG messages that reset and block/unblock the lines,
// so we have to assign dummy numbers for the NSVCI, NSEI, and BVCI.

// The messages supported at the BSSG level include signaling messages (to BVCI 0)
// that support, eg, blocking/unblocking of an individual BTS, UL_UNITDATA and DL_UNITDATA
// to transfer PDUs (Packet Data Units), which consist of L3 Messages or user data
// wrapped in LLC layer wrappers that flow through the BTS as RLCData blocks to L3 in the MS,
// and a few special messages that go to the BTS, like paging and MS capabilities.
// See BSSGMessages.h

const unsigned SGSNTimeout = 1000;	// In msecs

// The main interface to the SGSN.
class BSSGMain {
	public:
	// Only BSSG downlink messages are put on the receive queue; other types are handled immediately.
	// The transmit queue may have any type of BSSG/NS message.
	InterthreadQueue<BSSGDownlinkMsg> mbsRxQ;
	InterthreadQueue<NSMsg> mbsTxQ;
	InterthreadQueue<NSMsg> *mbsTestQ;	// Only used for testing
	Thread mbsRecvThread;
	Thread mbsSendThread;
	int mbsSGSockfd;
	Bool_z mbsIsOpen;

	Bool_z mbsResetReceived;
	Bool_z mbsResetAckReceived;
	Bool_z mbsAliveReceived;
	Bool_z mbsAliveAckReceived;
	Bool_z mbsBlocked;	// We dont implement blocking, but we track the state.

	// These are identifiers for the BSC and NS link, which we dont use.
	UInt_z mbsBVCI;		// Our BTS identifire.  Must not be 0 or 1.
	UInt_z mbsNSVCI;
	UInt_z mbsNSEI;

	BSSGMain() { mbsSGSockfd = -1; }

	bool BSSGOpen();
	bool BSSGReset();

	BSSGDownlinkMsg *BSSGReadLowSide();
};

void BSSGWriteLowSide(NSMsg *ulmsg);

extern BSSGMain gBSSG;

};

#endif
