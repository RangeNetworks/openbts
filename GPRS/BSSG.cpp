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

#include "Defines.h"
#include "GPRSInternal.h"	// For GPRSLOG()
#include "GSMConfig.h"
#include "Threads.h"
#include "BSSGMessages.h"
#include "BSSG.h"
#include "Utils.h"
#include "errno.h"

#include <sys/socket.h>
#include <arpa/inet.h>

namespace BSSG {
BSSGMain gBSSG;

const unsigned rbufSize = 3000;	// Much bigger than any PDU message.

#if _UNUSED_
static int BSTLVParse(ByteType *data, int &rp,
	IEIType::type expected_ieitype, int expected_length)
{
	int received_ieitype = data[rp++];
	int received_length = data[rp++];
	if (received_ieitype != expected_ieitype || received_length != expected_length) {
		int bstype = data[NSMsg::UnitDataHeaderLen];
		LOG(ERR) << "Error in BSSG msg type="<<bstype
			<<LOGVAR(expected_ieitype) <<LOGVAR(expected_length)
			<<LOGVAR(received_ieitype) <<LOGVAR(received_length);
		return 0;
	}
	int result;
	switch (received_length) {
	case 1: result = data[rp++]; break;
	case 2: result = getntohs(&data[rp]); rp+=2; break;
	case 4: result = getntohl(&data[rp]); rp+=4; break;
	default: assert(0);
	}
	return result;
}
#endif

static void BsRecvSignallingMsg(ByteType *data, int nsize)
{
	int rp = NSMsg::UnitDataHeaderLen;
	int bstype = data[rp++];
	switch (bstype) {

	// PDUs between NM SAPs:
	// We will not use the flow control stuff, block, unblock, etc.
	// However, we will generate the appropriate ACKs to keep the link happy.
	case BSSG::BSPDUType::BVC_BLOCK:
		BSSGWriteLowSide(BVCFactory(BSPDUType::BVC_BLOCK_ACK,0));
		break;
	case BSSG::BSPDUType::BVC_BLOCK_ACK:
		break;

	case BSSG::BSPDUType::BVC_RESET:		// network->BSS request reset everything.
		// Dont know what we should do about reset.
		BSSGWriteLowSide(BVCFactory(BSPDUType::BVC_RESET_ACK,0));
		break;
	case BSSG::BSPDUType::BVC_RESET_ACK:	// BSS->network and network->BSS?
		break;

	case BSSG::BSPDUType::BVC_UNBLOCK:
		BSSGWriteLowSide(BVCFactory(BSPDUType::BVC_UNBLOCK_ACK,0));
		break;
	case BSSG::BSPDUType::BVC_UNBLOCK_ACK:
		break;

	// We ignore all these:
	case BSSG::BSPDUType::SUSPEND_ACK:		// network->MS ACK
	case BSSG::BSPDUType::SUSPEND_NACK:		// network->MS NACK
	case BSSG::BSPDUType::RESUME_ACK:		// network->MS ACK
	case BSSG::BSPDUType::RESUME_NACK:		// network->MS NACK
	case BSSG::BSPDUType::FLUSH_LL:		// newtork->BSS forget this MS (it moved to another cell.)
	case BSSG::BSPDUType::SGSN_INVOKE_TRACE:	// network->BSS request trace an MS
		LOG(WARNING) << "Unimplemented BSSG message:" << BSPDUType::name(bstype);
		return;

	case BSSG::BSPDUType::SUSPEND:			// MS->network request to suspend GPRS service.
	case BSSG::BSPDUType::RESUME:			// MS->network request to resume GPRS service.
	case BSSG::BSPDUType::FLUSH_LL_ACK:	// BSS->network
	case BSSG::BSPDUType::LLC_DISCARDED:	// BSS->network notification of lost PDUs (probably expired)
		LOG(ERR) << "Invalid BSSG message:" << BSPDUType::name(bstype);
		return;
	}
}

void NsRecvMsg(unsigned char *data, int nsize)
{
	NSPDUType::type nstype = (NSPDUType::type) data[0];
	// We dont need to see all the keep alive messages.
	if (nstype != NSPDUType::NS_UNITDATA) { GPRSLOG(4) << "BSSG NsRecvMsg "<<nstype<<LOGVAR(nsize); }
	switch (nstype) {
	case NSPDUType::NS_UNITDATA: {
		int bvci = getntohs(&data[2]);
		if (bvci == BVCI::SIGNALLING) {
			GPRSLOG(4) << "BSSG <=== signalling "<<nstype<<LOGVAR(nsize) <<timestr();
			BsRecvSignallingMsg(data, nsize);
		} else if (bvci == BVCI::PTM) {
			GPRSLOG(4) << "BSSG <=== "<<nstype<<LOGVAR(nsize)<<" ignored" <<timestr();
			// Not implemented
		} else {
			// Send data to the MAC
			// We left the NS header intact.
			BSSGDownlinkMsg *dlmsg = new BSSGDownlinkMsg(data,nsize);
			//GPRSLOG(1) << "BSSG <=== queued "<<dlmsg->str() <<timestr();
			GPRSLOG(1) << "BSSG <=== queued size="<<dlmsg->size() <<timestr();
			gBSSG.mbsRxQ.write(dlmsg);
		}
		break;
	}
	case NSPDUType::NS_RESET:
		gBSSG.mbsResetReceived = true;
		BSSGWriteLowSide(NsFactory(NSPDUType::NS_RESET_ACK));
		break;
	case NSPDUType::NS_RESET_ACK:
		gBSSG.mbsResetAckReceived = true;
		break;
	case NSPDUType::NS_BLOCK:
		gBSSG.mbsBlocked = true;
		BSSGWriteLowSide(NsFactory(NSPDUType::NS_BLOCK_ACK));
		break;
	case NSPDUType::NS_BLOCK_ACK:
		// ignored.
		break;
	case NSPDUType::NS_UNBLOCK:
		gBSSG.mbsBlocked = false;
		BSSGWriteLowSide(NsFactory(NSPDUType::NS_UNBLOCK_ACK));
		break;
	case NSPDUType::NS_UNBLOCK_ACK:
		gBSSG.mbsBlocked = false;
		// ignored.
		break;
	case NSPDUType::NS_STATUS:
		// This happens when the sgsn is stopped and restarted.
		// It probably happens for other reasons too, but lets just
		// assume that is what happened and reset the BSSG link.
		gBSSG.BSSGReset();
		break;
	case NSPDUType::NS_ALIVE:
		gBSSG.mbsAliveReceived = true;
		BSSGWriteLowSide(NsFactory(NSPDUType::NS_ALIVE_ACK));
		break;
	case NSPDUType::NS_ALIVE_ACK:
		gBSSG.mbsAliveAckReceived = true;
		break;
	default:
		LOG(INFO) << "unrecognized NS message received, type "<<nstype;
		break;
	}
}

// Pull messages out of the BSSG socket and put them in the BSSG queue.
void *recvServiceLoop(void *arg)
{
	BSSGMain *bssgp = (BSSGMain*)arg;
	static unsigned char *buf = NULL;
	if (buf == NULL) { buf = (unsigned char*) malloc(rbufSize); }

	bssgp->mbsIsOpen = true;

	int failures = 0;
	while (bssgp->mbsIsOpen && ++failures < 10) {
		ssize_t rsize = recv(bssgp->mbsSGSockfd,buf,rbufSize,0);
		if (rsize > 0) {
			failures = 0;
			NsRecvMsg(buf,rsize);
		} else if (rsize == -1) {
			LOG(ERR) << "Received -1 from BSSG recv(), error:"<<strerror(errno);
			LOG(ERR) << "Above error may mean SGSN is not responding";
		}
	}

	// Oops.  Close the BSSG and kill the other thread.
	LOG(ERR) << "BSSGP aborting; too many failures in a row";
	bssgp->mbsIsOpen = false;
	// Send a message to the sendServiceLoop so that it will notice
	// we have died and die also.
	BSSGWriteLowSide(NsFactory(NSPDUType::NS_BLOCK));
	return NULL;
}

// The send probably does not need to be in a separate thread.
// We could also have used a select or poll system call.
// But it was easier to use two threads.

// OLD: Send this loop an NS_BLOCK message to kill this thread off;
// and we dont normally use that NS message.
// There is a BSSG-level BVC_BLOCK message that we would use to do a temporary data block.
void *sendServiceLoop(void *arg)
{
	BSSGMain *bssgp = (BSSGMain*)arg;
	NSPDUType::type nstype = NSPDUType::NS_RESET;	// init to anything.
	do {
		NSMsg *ulmsg = bssgp->mbsTxQ.read();
		// It is already wrapped up in an NS protocol.
		int msgsize = ulmsg->size();
		ssize_t result = send(bssgp->mbsSGSockfd,ulmsg->begin(),msgsize,0);
		nstype = ulmsg->getNSPDUType();
		int debug_level = 1; //(nstype == NSPDUType::NS_UNITDATA) ? 1 : 4;
		if (GPRS::GPRSDebug & debug_level) {
			GPRSLOG(debug_level) << "BSSG ===> sendServiceLoop sent "
				<<nstype<<LOGVAR(msgsize)<<ulmsg->str()<<timestr();
		}
		if (result != msgsize) {
			LOG(ERR) << "BSSGP invalid send result" << LOGVAR(result) << LOGVAR(msgsize);
		}
		delete ulmsg;
	} while (bssgp->mbsIsOpen /*&& nstype != NSPDUType::NS_BLOCK*/);
	return NULL;
}

static int opensock(uint32_t sgsnIp, int sgsnPort /*,int bssgPort*/ )
{
	//int fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	int sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sockfd < 0) {
		LOG(ERR) << "Could not create socket for BSSGP";
		return -1;
	}


	/******
	{	// We dont want to bind here.  connect will pick a port for us.
		int32_t bssgIp = INADDR_ANY;
		struct sockaddr_in myAddr;
		memset(&myAddr,0,sizeof(myAddr));	// be safe.
		myAddr.sin_family = AF_INET;
		myAddr.sin_addr.s_addr = htonl(bssgIp);
		myAddr.sin_port = htons(bssgPort);
		if (0 != bind(sockfd,(sockaddr*)&myAddr,sizeof(myAddr))) {
			LOG(ERR) << "Could not bind NS socket to" 
				<< LOGVAR(bssgIp) << LOGVAR(bssgPort) << LOGVAR(errno);
			close(sockfd);
			return -1;
		}
	}
	****/

	struct sockaddr_in sgsnAddr;
	memset(&sgsnAddr,0,sizeof(sgsnAddr));
	sgsnAddr.sin_family = AF_INET;
	sgsnAddr.sin_addr.s_addr = sgsnIp;	// This is already in network order.
	sgsnAddr.sin_port = htons(sgsnPort);
	if (0 != connect(sockfd,(sockaddr*)&sgsnAddr,sizeof(sgsnAddr))) {
		LOG(ERR) << "Could not connect NS socket to"
			<< LOGVAR(sgsnIp) << LOGVAR(sgsnPort) << LOGVAR(errno);
		close(sockfd);
		return -1;
	} else {
		GPRSLOG(1) << "connected to SGSN at "<< inet_ntoa(sgsnAddr.sin_addr) <<" port "<<sgsnPort;
	}
	return sockfd;
}

// NOTES: The sgsn uses UDP.
// It does not accept connections using listen() and accept().
// Rather it waits for a packet containing an NS_RESET message,
// then it remembers the IP&port from the IP header, (via recvfrom)
// for future communication with the BTS.
// BEGINCONFIG
// 'GPRS.SGSN.Host','127.0.0.1',0,0, 'IP address of the SGSN required for GPRS service.  The default value is the localhost, ie, SGSN runs on same machine as OpenBTS.'
// 'GPRS.SGSN.port',1920,0,0,'Port number of the SGSN required for GPRS service.  This must match the port specified in the SGSN config file, currently osmo_sgsn.cfg'
// ENDCONFIG
bool BSSGMain::BSSGOpen()
{
	if (mbsIsOpen) return true;
	// TODO: Look up the proper default sgsn port.  Maybe 3386. See pat.txt
	// Originally I specified the BSSG port, but now I let the O.S. pick
	// a free port via connect() call.
	//int bssgPort = gConfig.getNum("GPRS.BSSG.port",1921);

	// Default BVCI to the first available value.
	// The user may want to specify this some day far away.
	mbsBVCI = BVCI::PTP;


	int sgsnPort = gConfig.getNum("GPRS.SGSN.port");
	std::string sgsnHost = gConfig.getStr("GPRS.SGSN.Host");	// Default: localhost
	uint32_t sgsnIp = inet_addr(sgsnHost.c_str());

	if (sgsnIp == INADDR_NONE) {
		LOG(ERR) << "Config GPRS.SGSN.Host value is not a valid IP address: " << sgsnHost << "\n";
	}
	//mbsSGSockfd = openudp(sgsnIp,sgsnPort,bssgPort);
	if (mbsSGSockfd < 0) {
		mbsSGSockfd = opensock(sgsnIp,sgsnPort);
	}
	if (mbsSGSockfd < 0) {
		LOG(ERR) << "Could not init BSSGP due to socket failure";
		return false;
	}
	mbsBlocked = true;
	mbsRecvThread.start(recvServiceLoop,this);
	mbsSendThread.start(sendServiceLoop,this);
	return BSSGReset();
}

bool BSSGMain::BSSGReset()
{
	// BSSG starts out blocked until it receives a reset.
	mbsBlocked = true;
	mbsResetReceived = false;
	mbsResetAckReceived = false;

	// Start communication with SGSN.
	// Initiate NS Reset procedure: GSM 08.16 7.3
	// We are supposed to send NS_STATUS, but it doesnt matter with our sgsn.
	BSSGWriteLowSide(NsFactory(NSPDUType::NS_RESET));
	BSSGWriteLowSide(NsFactory(NSPDUType::NS_UNBLOCK));
	// Wait a bit for the sgsn to respond.
	// Note: The first time we talk to the SGSN it sends us an NS_RESET,
	// but if OpenBTS crashes, the second time it inits it doesnt send NS_RESET,
	// which may be a bug in the SGSN, but in any event we dont want
	// to wait for a RESET msg.
	for (int i = 0; 1; i++) {
		if (mbsResetAckReceived && !mbsBlocked) { break; }
		Utils::sleepf(0.1);

		if (i >= 40) {	// wait 4 seconds
			GPRSLOG(1) << LOGVAR(mbsResetReceived)
				<<LOGVAR(mbsResetAckReceived) <<LOGVAR(mbsBlocked);
			LOG(INFO) << "SGSN failed to respond\n";
			return false;
		}
	}

	// GSM 08.18 8.4: Reset the BVC.  You must do this after verifying NS layer is working.
	// Must reset each BVCI separately.
	// I'm not going to bother to check for acks - if the NS protocol inited,
	// the sgsn is fine and this will init ok too.
	BSSGWriteLowSide(BVCFactory(BSPDUType::BVC_RESET,BVCI::SIGNALLING));
	BSSGWriteLowSide(BVCFactory(BSPDUType::BVC_RESET,gBSSG.mbsBVCI));
	return true;
}

BSSGDownlinkMsg *BSSGMain::BSSGReadLowSide()
{
	return mbsRxQ.readNoBlock();
}

void BSSGWriteLowSide(NSMsg *ulmsg)
{
	if (gBSSG.mbsTestQ) {
		// For testing, deliver messages to this queue instead:
		gBSSG.mbsTestQ->write(ulmsg);
	} else {
		GPRSLOG(1) << "BSSG ===> writelowside " <<ulmsg->str()<<timestr();
		gBSSG.mbsTxQ.write(ulmsg);	// normal mode; block is headed for the SGSN.
	}
}

};
