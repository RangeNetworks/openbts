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

#include <stdint.h>
#include <poll.h>
#include "LLC.h"
#define GGSN_IMPLEMENTATION 1
#include "SgsnBase.h"
#include "Sgsn.h"
#include "Ggsn.h"
#include "miniggsn.h"
//#include "MSInfo.h"	// To dump MSInfo
#include "GPRSL3Messages.h"
#define CASENAME(x) case x: return #x;

	// GSM04.08 (24.008 better) 9.5.1 describes Activate PDP Context Request Message.
	// The incoming IP address is in pdp->eua.v
	// GSM04.08 10.5.6.4 describes the Packed Data Protocol Address.
	// If second byte (the length) is 2 and type is IP, DHCP assigns an IP address.
	// We dont support anything else.
	// We are just going to ignore the incoming address, and assign it one from our range.
	// We are permitted to change IPv6 to use IPv4, but since we ignore it all, doesnt matter.
	// GSM04.08 10.5.6.4 describes Packet Data Protocol Configuration Options.
	// This is there solely to support optional PPP tunneling all the way from
	// TE (Terminal Equipment) attached to the MS, through the SGSN and GGSN, and out
	// to some internet endpoint elsewhere.  Special support is needed both to establish
	// the PPP connection and to service it, since it is its own protocol and packets
	// for PPP connection types need to be passed through the GGSN.
	// We will not support PPP now.

namespace SGSN {
Ggsn gGgsn;

static uint32_t mg_dns[2] = {0,0};

const char *SmCause::name(unsigned mt, bool ornull)
{
	switch (mt) {
		CASENAME(Operator_Determined_Barring)
		CASENAME(MBMS_bearer_capabilities_insufficient_for_the_service)
		CASENAME(LLC_or_SNDCP_failure)
		CASENAME(Insufficient_resources)
		CASENAME(Missing_or_unknown_APN)
		CASENAME(Unknown_PDP_address_or_PDP_type)
		CASENAME(User_authentication_failed)
		CASENAME(Activation_rejected_by_GGSN_Serving_GW_or_PDN_GW)
		CASENAME(Activation_rejected_unspecified)
		CASENAME(Service_option_not_supported)
		CASENAME(Requested_service_option_not_subscribed)
		CASENAME(Service_option_temporarily_out_of_order)
		CASENAME(NSAPI_already_used)
		CASENAME(Regular_deactivation)
		CASENAME(QoS_not_accepted)
		CASENAME(Network_failure)
		CASENAME(Reactivation_required)
		CASENAME(Feature_not_supported)
		CASENAME(Semantic_error_in_the_TFT_operation)
		CASENAME(Syntactical_error_in_the_TFT_operation)
		CASENAME(Unknown_PDP_context)
		CASENAME(Semantic_errors_in_packet_filter)
		CASENAME(Syntactical_errors_in_packet_filter)
		CASENAME(PDP_context_without_TFT_already_activated)
		CASENAME(Multicast_group_membership_timeout)
		CASENAME(Activation_rejected_BCM_violation)
		CASENAME(PDP_type_IPv4_only_allowed)
		CASENAME(PDP_type_IPv6_only_allowed)
		CASENAME(Single_address_bearers_only_allowed)
		CASENAME(Collision_with_network_initiated_request)
		CASENAME(Invalid_transaction_identifier_value)
		CASENAME(Semantically_incorrect_message)
		CASENAME(Invalid_mandatory_information)
		CASENAME(Message_type_nonexistent_or_not_implemented)
		CASENAME(Message_type_not_compatible_with_the_protocol_state)
		CASENAME(Information_element_nonexistent_or_not_implemented)
		CASENAME(Conditional_IE_error)
		CASENAME(Message_not_compatible_with_the_protocol_state)
		CASENAME(Protocol_error_unspecified)
		CASENAME(APN_restriction_value_incompatible_with_active_PDP_context)
		case 0:
			return ornull ? 0 : "SmCause type 0";
		default:
			return ornull ? 0 : "SmCause type unrecognized";
	}
}

static void sethighpri()
{
	pthread_t me = pthread_self();
	int policy; struct sched_param sp;
	pthread_getschedparam(me,&policy,&sp);
	SGSNLOG("service loop"<<LOGVAR(policy)<<LOGVAR2("priority",sp.sched_priority));
	policy = SCHED_FIFO;	// Gives this thread higher priority.
	pthread_setschedparam(me,policy,&sp);
}


void *miniGgsnReadServiceLoop(void *arg)
{
	Ggsn *ggsn = (Ggsn*)arg;
	sethighpri();
	while (ggsn->active()) {
		struct pollfd fds[1];
		fds[0].fd = tun_fd;
		fds[0].events = POLLIN;
		fds[0].revents = 0;		// being cautious
		// We time out occassionally to check if the user wants to shut the sgsn down.
		if (-1 == poll(fds,1,ggsn->mStopTimeout)) {
			SGSNERROR("ggsn: poll failure");
			return 0;
		}
		if (fds[0].revents & POLLIN) {
			miniggsn_handle_read();
		}
	}
	return 0;
}

void *miniGgsnWriteServiceLoop(void *arg)
{
	sethighpri();
	Ggsn *ggsn = (Ggsn*)arg;
	while (ggsn->active()) {
		// 8-6-2012 This interthreadqueue is clumping things up.  Try taking out the timeout.
		//PdpPdu *npdu = ggsn->mTxQ.read(ggsn->mStopTimeout);
		PdpPdu *npdu = ggsn->mTxQ.read();
		if (npdu) {
			miniggsn_snd_npdu_by_mgc(npdu->mgp, npdu->mpdu.begin(), npdu->mpdu.size());
			delete npdu;
		}
	}
	return 0;
}

void addShellRequest(const char *wCmd,GmmInfo*gmm,PdpContext *pdp)
{
	ShellRequest *req = new ShellRequest();
	req->msrCommand = wCmd;
	req->msrArg1 = gmm->mImsi.hexstr();
	if (pdp && pdp->mgp) {
		char ipaddr[40], nsapi[10];
		ip_ntoa(pdp->mgp->mg_ip, ipaddr);
		req->msrArg2 = ipaddr;
		sprintf(nsapi,"%d",pdp->mNSapi);
		req->msrArg3 = nsapi;
	}
	gGgsn.mShellQ.write(req);
}

void addShellRequest(const char *wCmd,const char *arg1)
{
	ShellRequest *req = new ShellRequest();
	req->msrCommand = wCmd;
	req->msrArg1 = arg1;
	gGgsn.mShellQ.write(req);
}

// Lurk on the mShellQ and execute a shell script to process requests found.
void *miniGgsnShellServiceLoop(void *arg)
{
	Ggsn *ggsn = (Ggsn*)arg;
	std::string shname = gConfig.getStr("GGSN.ShellScript");
	while (ggsn->active()) {
		ShellRequest *req = ggsn->mShellQ.read(ggsn->mStopTimeout);
		if (! req) continue;
		runcmd("/bin/sh","sh",shname.c_str(), req->msrCommand.c_str(), req->msrArg1.c_str(),
			req->msrArg2.c_str(),req->msrArg3.c_str());
		delete req;
	}
	return 0;
}

// Return true on success
bool Ggsn::start()
{
	if (gGgsn.mActive) { return false; }
	if (!miniggsn_init()) { return false; }
	gGgsn.mGgsnRecvThread.start(miniGgsnReadServiceLoop,&gGgsn);
	gGgsn.mGgsnSendThread.start(miniGgsnWriteServiceLoop,&gGgsn);
	if (gConfig.getStr("GGSN.ShellScript").size() > 1) {
		gGgsn.mGgsnShellThread.start(miniGgsnShellServiceLoop,&gGgsn);
		gGgsn.mShellThreadActive = true;
	}
	gGgsn.mActive = true;
	//time_t now; time(&now);
	//char timebuf[30];
	//ctime_r(&now,timebuf);
	//addShellRequest("Start",timebuf);
	addShellRequest("Start","");
	return true;
}

void Ggsn::stop()
{
	if (!gGgsn.mActive) {return;}
	gGgsn.mGgsnRecvThread.join();
	gGgsn.mGgsnSendThread.join();
	if (gGgsn.mShellThreadActive) {
		gGgsn.mGgsnShellThread.join();
		gGgsn.mShellThreadActive = false;
	}
	gGgsn.mActive = false;
}

// Call this to update the PCO for retransmission to the MS.
// We cannot just make up a PCO ahead of time and then send it out to
// all the MS for two reasons:
// o The incoming options have uniquifying transaction identifiers that are
// different each time, so we must modify the incoming pcoReq each time while
// carefully preserving those modifiers.
// o There are two major formats for the PCO options - see below.
static void setPco(ByteVector &resultpco, ByteVector &pcoReq)
{
	if (mg_dns[0] == 0) { ip_finddns(mg_dns); }

	// GSM 24.008 10.5.6.3: Procotol Configuration Options.
	// These are the "negotiated" address params wrapped in PPP,
	// except they are not very negotiated; we are going to cram them
	// into the MS and it can accept them or die.
	// The first byte is the header, followed by any number of:
	//	protocol id (2 octets)
	//	length of contents (1 octet)
	// I have seen this supplied by the MS in two different ways:
	// - as two separate 0x8021 requests, each with a single option request
	//	(Blackberry)
	// - as a single 0x8021 request with two option requests for the two DNS servers.
	//	(Samsung phone)
	resultpco.clone(pcoReq);
	unsigned char *pc = resultpco.begin();
	unsigned char *end = pc + resultpco.size();
	 __attribute__((unused)) const char *protname = "";
	if (*pc++ != 0x80) {
		MGERROR("SGSN: Unrecognized PCO Config Protocol: %d\n",pc[-1]);
	} else while (pc < end) {
		unsigned proto = (pc[0] << 8) + pc[1];
		pc += 2;
		unsigned ipcplen = *pc++;
		if (proto == 0x8021) {	// IP Control Protocol.
			// IPCP looks like this:
			// 1 byte: command: 1 => configure-request
			// 1 byte: transaction uniquifying identifier.
			// 2 bytes: total length of ipcp (even though length above was a byte)
			// Followed by IPCP options, where each option is:
			//		1 byte: option code
			//		1 byte: total option length N (should be 6)
			//		N bytes: data
			if (pc[0] == 1) {	// command = configure-request
				// pc[1] is the uniquifying identifier, leave it alone.
				// pc[2]&pc[3] are another length.
				// Followed by one or more 6 byte option consisting of:
					// pc[4]: IPCP option code
					// pc[5]: length (6)
					// pc[6-9]: data
				// Note: the 4 byte header length is included in the option_len
				unsigned ipcp_len = pc[3];  // pc[2] better be 0.
				unsigned char *op;
				for (op = &pc[4]; op < pc + ipcp_len; op += op[1]) {
					const char *what = "";
					switch (op[0]) {
					case 0x81:	// primary dns.
						pc[0] = 2;	// IPCP command = ACK
						if (op[1] < 6) {
							bad_ipcp_opt_len:
							MGERROR("SGSN: Invalid PCO IPCP Config Option Length: opt=0x%x len=%d\n",
							op[0], op[1]);
							goto next_protocol;
						}
						memcpy(&op[2], &mg_dns[0], 4);	// addr in network order.
						break;
					case 0x83:	// secondary dns
						pc[0] = 2;	// IPCP command = ACK
						if (op[1] < 6) { goto bad_ipcp_opt_len; }
						memcpy(&op[2], &mg_dns[mg_dns[1] ? 1 : 0], 4);	// addr in network order.
						break;
					case 2:
						what = "IP Compression Protocol"; goto bad_ipcp;
					case 0x82:
						what = "primary NBNS [NetBios Name Service]"; goto bad_ipcp;
					case 0x84:
						what = "secondary NBNS [NetBios Name Service]"; goto bad_ipcp;
					default: bad_ipcp:
						// It would be nice to send an SMS message that the phone is set up improperly.
						MGWARN("SGSN: warning: ignoring PDP Context activation IPCP option %d %s\n",pc[4],what);
						break;
					}
				}
			}
		} else if (proto == 0xc021) {	// LCP: 
			protname = "(LCP [Link Control Protocol] for PPP)";
			goto unsupported_protocol;
		} else if (proto == 0xc223) {	// CHAP:
			protname = "(CHAP [Challenge-Handshake Authentication Protocol] for PPP)";
			goto unsupported_protocol;
		} else if (proto == 0xc023) {	// PAP: Password authentication protocol.
			protname = "(PAP [Password Authentication Protocol] for PPP)";
			goto unsupported_protocol;
		} else {
			// If we see any of these options are non-empty, the MS may be configured to use PPP.
			// It is hopeless; user must reconfigure the MS.
			unsupported_protocol:
			if (ipcplen) {
				// 6-12: Remove this message for the 3.0 release because we get
				// lots of bogus complaints about PAP.
				//MGWARN("SGSN: warning: ignoring PDP Context activation sub-protocol 0x%x %s; MS may require reconfiguration.\n",proto,protname);
			}
		}
		next_protocol:
		pc += ipcplen;
	}
}

static void setIpAddr(ByteVector &result,mg_con_t *mgp)
{
	// This is the IP address, only IPv4 supported.
	// If the MS asks for IPv6, it is supposed to accept IPv4 anyway.
	result = ByteVector(6);// IPv4 address + 2 byte header.
	// 3GPP 24.008 10.5.6.4
	result.setByte(0,0x01); // IETF allocated address, which is all we support.
	result.setByte(1,0x21);	// IPv4.
	// This is a special case - we cannot use setUIint32 because it converts to network order,
	// but the ip address is already in network order, which is what 3GPP requires, so just copy it.
	memcpy(result.begin()+2, &mgp->mg_ip, 4);
}

// TODO: We have to set the transaction identifier for the PdpContext?
//void sendPdpDeactivateAll(SgsnInfo *si, unsigned cause) //SmCause::Cause cause)
void sendPdpDeactivateAll(SgsnInfo *si, SmCause::Cause cause)
{
	// TODO: what should transactionId be?
	// This is a downlink command, and we are supposed to allocate a new transaction id for each one,
	// but since this is the only one we ever send, maybe 0 is ok.
	int transactionId = 0;
	L3SmMsgDeactivatePdpContextRequest deact(transactionId,(SmCause::Cause)cause,true);
	//si->getLlcEntity(LlcSapi::GPRSMM)->lleWriteHighSide(deact);
	si->sgsnWriteHighSideMsg(deact);
}

//void sendSmStatus(SgsnInfo *si,SmCause::Cause cause)
//{
//	L3SmMsgSmStatus smstat(cause);
//	LlcDlFrame bv(1000);
//	smstat.smwrite(bv);
//	SGSNLOG("Sending "<<smstat.str() <<" "<<si);
//	si->getLlcEntity(LlcSapi::GPRSMM)
//		->lleWriteHighSide(bv,false,"pdp context reject");
//}

static void sendPdpContextReject(SgsnInfo *si,SmCause::Cause cause,unsigned ti)
{
	L3SmMsgActivatePdpContextReject pdpRej(ti,cause);
	si->sgsnWriteHighSideMsg(pdpRej);
}

void sendPdpContextAccept(SgsnInfo *si, PdpContext *pdp)
{
	L3SmMsgActivatePdpContextAccept pdpa(pdp->mTransactionId);
	pdpa.mLlcSapi = pdp->mLlcSapi;

	// On the blackberry, the qosReq is 3 bytes, but it will fail is you just return that.
	// Must return the whole shebang.
	SmQoS resultQoS(12); // The full 12 byte QoS works.
	resultQoS.defaultPS(pdp->mRabStatus.mRateDownlink,pdp->mRabStatus.mRateUplink);
	pdpa.mQoS = resultQoS;

	pdpa.mRadioPriority = 2;	// 2 is a medium priority. Why do we pass this to the MS at all?
	setPco(pdpa.mPco,pdp->mPcoReq);
	setIpAddr(pdpa.mPdpAddress,pdp->mgp);
	// No Packet Flow Identifier - not implemented.
	// No SM cause, unless "the network accepts the requested PDN connectivity with restrictions."
	SGSNLOG("Sending "<<pdpa.str() <<" "<<si);	//done by sgsnWriteHighSideMsg

	// Send L3 Activate PDP Context Accept it on its way.
	si->sgsnWriteHighSideMsg(pdpa);
	addShellRequest("PdpActivate",si->getGmm(),pdp);
}

// TODO: All the messages below should include the phone tlli.
static void handleActivatePdpContextRequest(SgsnInfo *si, L3SmMsgActivatePdpContextRequest &pdpr)
{
	unsigned ti = pdpr.mTransactionId;
	const char *name = "Activate Pdp Context Request:";
	// For UMTS, the MS that does not support GPRS is supposed to set the LlcSapi
	// to "LLC-non-assigned" which is value 0, and then the Network is supposed to send the same one back.
	// So just dont bother to validate llc sapi at all in UMTS, since we dont use it for anything.
#if RN_UMTS == 0
	if (! LlcEngine::isValidDataSapi(pdpr.mLlcSapi)) {
		SGSNWARN(name<<"invalid llc sapi:"<<pdpr.mLlcSapi);
		sendPdpContextReject(si,SmCause::Invalid_mandatory_information,ti);
		return;
	}
#endif
	if (pdpr.mNSapi < 5 || pdpr.mNSapi > 15) {
		SGSNWARN(name<<"invalid ns sapi:"<<pdpr.mNSapi);
		sendPdpContextReject(si,SmCause::Invalid_mandatory_information,ti);
		return;
	}
	// After the BTS is power cycled, the MS may attempt to re-establish the connection by
	// sending ActivatePdpContextRequest first thing.  We want to force it to re-attach,
	// but the Blackberry, for one, does not follow the spec on this procedure and
	// generally ignores the 'implicity_detached' status message.
	// Not too surprising, because unlike us, power-cycling their SGSN for testing is
	// probably not that easy for the MS manufacturers.
	if (!si->isRegistered()) {
		sendPdpContextReject(si,SmCause::Message_not_compatible_with_the_protocol_state,ti);
		// Send a GMM message too, just to be safe.
		sendImplicitlyDetached(si);
		return;
	}
	GmmInfo *gmm = si->getGmm();
	assert(gmm); // if isRegistered is true then mGmmp is set by definition.

	PdpContext *pdp = gmm->getPdp(pdpr.mNSapi);
	bool duplicateRequest = false;
	if (pdp) {
		if (pdp->mTransactionId == pdpr.mTransactionId) {
			// Another request for the same pdp.
			duplicateRequest = true;
			if (pdp->mNSapi != (int) pdpr.mNSapi) {
				// TODO: We should punt at this point.
				SGSNWARN(name<<"duplicate request with different ns sapi:"<<pdpr.mNSapi);
			}
		} else {
			SGSNWARN(name<<"ns sapi already in use:"<<pdpr.mNSapi);
			sendPdpContextReject(si,SmCause::NSAPI_already_used,ti);
			return;
		}
	}

	if (duplicateRequest) {
		SGSNLOG("Duplicate PdpContextRequest");
	} else {
		// Allocate an IP address.
		mg_con_t *mgp = mg_con_find_free(gmm->mPTmsi,pdpr.mNSapi);
		if (mgp == NULL) {
			SGSNERROR(name<<"out of ip addresses");
			sendPdpContextReject(si,SmCause::Insufficient_resources,ti);
			return;
		}

		// Okey dokey.  Allocate and hook up.
		//pdp = allocPdp(pdpr.mNSapi,pdpr.mLlcSapi,mgp);
		//LlcEntityUserData *userdatalle = si->getLlcEntityUserData(pdpr.mLlcSapi);
		//sndcp = new Sndcp(pdpr.mNSapi,pdpr.mLlcSapi,userdatalle);

		pdp = new PdpContext(gmm,mgp,pdpr.mNSapi,pdpr.mLlcSapi);
		gmm->connectPdp(pdp,mgp);

		//mgp->mg_pdp = pdp;
		//sndcp->setPdp(pdp);

#if RN_UMTS
			// For UMTS the PDP context creation is two-part.
			// At this point we must allocate the RB [Radio Bearer] for the UE
			// and we must wait for the acknowledgement message from the UE
			// before we can send the final ActivatePdpContextAccept message.
			// Like this:
			//      L3 ActivatePdpContextRequest, NSAPI=n (n=5..15)
			// MS ---------------------------------> Network
			//      RRC RadioBearerSetup, RbId=n
			// MS <--------------------------------- Network
			//      RRC RadioBearerSetupComplete
			// MS ---------------------------------> Network
			//     L3 ActivatePdpContextAccept NSAPI=n
			// MS <--------------------------------- Network
			pdp->mRabStatus = SgsnAdapter::allocateRabForPdp(si->mMsHandle,pdpr.mNSapi,pdpr.mQoS);
			switch (pdp->mRabStatus.mStatus) {
			case RabStatus::RabFailure:
				SGSNERROR(name<<"Rab Allocation Failure:"<<SmCause::name(pdp->mRabStatus.mFailCode));
				sendPdpContextReject(si,pdp->mRabStatus.mFailCode,ti);
				return;
			case RabStatus::RabPending:
				pdp->mUmtsStatePending = true;
				pdp->mPendingPdpr = pdpr;
				return;
			case RabStatus::RabAllocated:
				// The Rab was allocated previously by this UE.
				// Fall through to resend the accept message.
				break;
			default: assert(0);
			}
#else
			// It is gprs.  The link is already allocated.
			// We ignore the QoS request; what a joke - it barely goes low enough to specify the GPRS bandwidth.
			// For now, just use the base throughput of the GPRS link which is 2.5KBytes/sec less overhead.
			// TODO: set the uplink/downlink rates from the multi-slot class of the MS.
			pdp->mRabStatus.mStatus = RabStatus::RabAllocated;
			pdp->mRabStatus.mRateUplink = pdp->mRabStatus.mRateDownlink = 2;
#endif
	}

	// This must be set each time, because it has uniquifying ids in it:
	pdp->update(pdpr);

	sendPdpContextAccept(si,pdp);

	/*****
	// Create the accept message. 3GPP 24.008 9.5.2.
	//L3SmMsgActivatePdpContextAccept pdpa(pdpr.mTransactionId);
	L3SmMsgActivatePdpContextAccept pdpa(pdp->mTransactionId);
	// Note: we send back the llc sapi was sent to us, even if it is invalid.
	pdpa.mLlcSapi = pdpr.mLlcSapi;
	// Using the qos params from the sender did not work.
	// Send our own default qos params:
	setQoS(pdpa.mQoS,pdpr.mQoS);
	pdpa.mRadioPriority = 2;	// 2 is a medium priority. Why do we pass this to the MS at all?
	setPco(pdpa.mPco,pdpr.mPco);
	setIpAddr(pdpa.mPdpAddress,pdp->mgp);
	// No Packet Flow Identifier - not implemented.
	// No SM cause, unless "the network accepts the requested PDN connectivity with restrictions."
	SGSNLOG("Sending "<<pdpa.str() <<" "<<si);

	// Send L3 Activate PDP Context Accept it on its way.
	si->sgsnWriteHighSideMsg(pdpa);
	****/
}

void handleDeactivatePdpContextRequest(SgsnInfo *si, L3SmMsgDeactivatePdpContextRequest &deact)
{
	unsigned nsapi;
	unsigned nsapiMask = 0;	// Mask of nsapi that were freed.
	if (deact.mTearDownIndicator) {
		nsapiMask = si->freePdpAll(false);
	} else {
		// Look for the pdp with this transaction identifier.
		bool found = false;
		for (nsapi = 0; nsapi < 16; nsapi++) {	// 0-4 are unused, but be safe.
			PdpContext *pdp;
			if ((pdp = si->getPdp(nsapi))) {
				if (pdp->mTransactionId == deact.mTransactionId) {
					addShellRequest("PdpDeactivate",si->getGmm(),pdp);
					si->freePdp(nsapi);
					nsapiMask = 1<<nsapi;
					found = true;
					break;
				}
			}
		}
		if (!found) {
			SGSNWARN("PdpContextDeactivate: pdp context not found for ti="<<deact.mTransactionId);
			// and send what??
		}
	}
	// Send L3 Activate PDP Context Accept on its way.
	L3SmMsgDeactivatePdpContextAccept deactaccept(deact.mTransactionId);
	//gmmlle->lleWriteHighSide(deactaccept);
	si->sgsnWriteHighSideMsg(deactaccept);

	// Deactivate the RABs.  Only does something in UMTS, a no-op in GPRS.
	// 23.060 9.3.4.1 shows the RABs being deactivated after sending
	// the DeactivatePdpContextAccept message, so we will comply,
	// although it probably does not matter.  These are sent in acknowledged mode
	// so it is conceivable that they will not arrive at the UE in order.
	if (nsapiMask) { si->deactivateRabs(nsapiMask); }
}


// The gmmlle is the lle of the GMM entity, the one to which we should return a message.
void Ggsn::handleL3SmMsg(SgsnInfo *si,L3GprsFrame &frame1)
{
	L3SmFrame frame(frame1);
	unsigned mt = frame.getMsgType();	// message type
	//SGSNLOG("CRACKING SM MSG TYPE "<<mt);
	switch (mt) {
	case L3SmMsg::ActivatePDPContextRequest: {
		L3SmMsgActivatePdpContextRequest pdpr(frame);
		SGSNLOG("Received "<<pdpr.str() <<si);
		handleActivatePdpContextRequest(si,pdpr);
		break;
	}
	case L3SmMsg::SMStatus: {
		L3SmMsgSmStatus stmsg(frame);
		SGSNLOG("Received SmStatus: "<<stmsg.str()<<si);
		break;
	}
	//case L3SmMsg::ActivatePDPContextAccept:
	//case L3SmMsg::ActivatePDPContextReject:
	//case RequestPDPContextActivation:
	//case RequestPDPContextActivationReject:

	case L3SmMsg::DeactivatePDPContextRequest: {
		//SGSNLOG("Incoming Deactivate pdp, frame="<<frame<< " bv="<<(ByteVector)frame);
		L3SmMsgDeactivatePdpContextRequest deact(frame);
		SGSNLOG("Received DeactivatePdpContextRequest: "<<deact.str());
		handleDeactivatePdpContextRequest(si,deact);
		break;
	}

	//DeactivatePDPContextAccept:

	//ModifyPDPContextRequest = 0x48,	// network to MS direction.
	//ModifyPDPContextAccept = 0x49,	// MS to netowrk direction.
	//ModifyPDPContextRequestMS = 0x4a,	// MS to network direction.
	//ModifyPDPContextAcceptMS = 0x4b,	// network to MS direction.
	//ModifyPDPContextReject = 0x4c,
	//ActivateSecondaryPDPContextRequest = 0x4d,
	//ActivateSecondaryPDPContextAccept = 0x4e,
	//ActivateSecondaryPDPContextReject = 0x4f,


	// From GSM 24.008:
	//ActivateMBMSContextRequest = 0x56,
	//ActivateMBMSContextAccept = 0x57,
	//ActivateMBMSContextReject = 0x58,
	//RequestMBMSContextActivation = 0x59,
	//RequestMBMSContextActivationReject = 0x5a,

	//RequestSecondaryPDPContextActivation = 0x5b,
	//RequestSecondaryPDPContextActivationReject = 0x5c,
	//Notification = 0x5d,
	default:
		SGSNWARN("Ignoring GPRS SM message type "<<mt<<" " <<L3SmMsg::name(mt));
		break;
	}
}

}; // namespace
