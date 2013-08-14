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

//TODO - include the TCP sequence number in the message,
// and identify the duplicate packets,
// then rerun a test downloading a single jpg
// with tossing off, to see what is happening.

// iptables -t nat -A POSTROUTING -o eth0 -j SNAT --to 192.168.1.8
// iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
// iptables -t nat -F  # Flush current tables
// iptables -t nat -L  # List current tables
// ---

// Options:
//	o 2 tunnels.  First is read/write by the ggsn.  Second is configured via linux.
//		This gives us a pseudo-'device' so we can use linux forwarding.
// 		iptables -t nat -A POSTROUTING -out eth0 -j MASQUERADE
// 		iptables -A FORWARD -i tun2 -j ACCEPT
//		Another advantage is the tunnel could go to a remote machine.
//		Or the first tunnel could terminate back in the OpenBTS.
// o Do NAT myself.
// o Use linux NAT.  Write MS->Net packets to router.  Bind raw socket on 192.168.1.8 for
//		all packets, looking for our own.

// NOTES:
// The ip tunnel local/remote specify the outer transport layer IP addresses in the IPIP
// protocol header prepended to packets sent on the tunnel.
// The ifconfig adds two local addresses
//
// Tunnel Example:
// 	Network A: net 10.0.1.0/24 router 10.0.1.1  public address 172.16.17.18 on public net C
// 	Network B: net 10.0.2.0/24 router 10.0.2.1  public address 172.19.20.21 on public net C
//	Network A:
//		ip tunnel add netb mode gre remote 172.19.20.21 local 172.16.17.18
//		# Apparently this tunnel is the router.
//		ip addr add 10.0.1.1 dev netb
//		ip route add 10.0.2.0/24 dev netb
//	Or for ipip tunneling:
//		ifconfig tunl0 10.0.1.1 pointopoint 172.19.20.21
//		route add -net 10.0.2.0 netmask 255.255.255.0 dev netb
// Network B:
//		ip tunnel add neta mode gre remote 172.16.17.18 local 172.19.20.21
//		ip addr add 10.0.2.1 dev neta
//		ip addr add 10.0.1.0/24 dev neta
//	Or for ipip tunneling:
//		ifconfig tunl0 10.0.2.1 pointopoint 172.19.20.21
//		route add -net 10.0.2.0 netmask 255.255.255.0 dev tunl0
//
//
// Another Tunnel example from IPIPNotes:
// 		Router_1 eth0: 1.2.3.4  - Asterisk system
//		Router_2 eth0: 4.3.2.1 eth1: 10.0.0.1  NAT private network and SIP phones.
// Router_1
//		ip tunnel add iptun mode ipip remote 4.3.2.1 local 10.0.1.1
//		route add -net 10.0.2.0/24 dev iptun
// Router_2
//		ip tunnel add iptun mode ipip remote 1.2.3.4 local 10.0.2.1
//		route add -net 10.0.1.0/24 dev iptun
// Router_1
//		route add -net 10.0.0.0/24 dev iptun
//		route add -net 10.0.0.0/24 gw 10.0.0.1
//		Note: The comment is backwards.
//		Allows you to ping any device on 10.0.0.0/24 from router_1 (not from router_2)
//		Allows any 10.0.0.) device to ping router1 using 10.0.1.1
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#include <netdb.h>			// pat added for gethostbyname
#include <netinet/ip.h>		// pat added for IPv4 iphdr
#include <netinet/tcp.h>		// pat added for tcphdr
#include <netinet/udp.h>		// pat added for udphdr

#include <linux/if.h>			// pat added.
#include <linux/if_tun.h>			// pat added.
//#include <sys/ioctl.h>			// pat added, then removed because it defines NCC used in GSMConfig.
#include <assert.h>				// pat added
#include <stdarg.h>				// pat added
#include <time.h>				// pat added.
#include <sys/time.h>
#include <sys/types.h>
#include <wait.h>
#include "miniggsn.h"
#undef NCC	// Make sure.  This is defined in ioctl.h, but used as a name in GSMConfig.h.
#include "Ggsn.h"
#include <Configuration.h>

// A mini-GGSN included inside the SGSN.
// Each MS will be assigned a dynamic IP address.
// We will use one 256-wide bank of IP address, eg: 192.168.99.1 - 192.68.99.254.
// This needs to be a config option.
// The addresses are be serviced by a NAT running on this box.
// An alternative architecture would be to get IP addresses from a DHCP server
// on whatever router is doing the NAT.
// The NAT could be tied to a static IP; a dynamic IP; or if the bts has two IP addresses,
// the NAT could be tied to the second static or dynamic IP address used solely for phones;
// or tied to a tunnel that goes elsewhere.
// Since there are so many possibilibies, the nat is configured externally.

namespace SGSN {

int pdpWriteHighSide(PdpContext *pdp, unsigned char *packet, unsigned len);
int tun_fd = -1; // This is the tunnel we use to talk with the MSs.
FILE *mg_log_fp = NULL;		// Extra log file for IP traffic.
int mg_debug_level = 0;


// old:
//static char const *mg_base_ip_str = "192.168.99.1";	// This did not raw bind.
//static char const *mg_base_ip_route = "192.168.99.0/24";
//static char const *mg_net_mask = "255.255.255.0";	// todo: get this from "/24" above.
static struct GgsnConfig {
	unsigned mgMaxPduSize;
	int mgMaxConnections;		// The maximum number of PDP contexts, which is roughly
								// the maximum number of simultaneous MS allowed.
	unsigned mgIpTimeout;	// Dont reuse a connection for this many seconds.
	unsigned mgIpTossDup;	// Toss duplicate packets.

} ggConfig;

// Mini-Firewall rules
struct GgsnFirewallRule {
	GgsnFirewallRule *next;
	uint32_t ipBasenl;
	uint32_t ipMasknl;
	GgsnFirewallRule(GgsnFirewallRule *wnext,uint32_t wipbasenl, uint32_t wmasknl) :
		next(wnext),ipBasenl(wipbasenl),ipMasknl(wmasknl) {}
};
GgsnFirewallRule *gFirewallRules = NULL;
void addFirewallRule(uint32_t ipbasenl,uint32_t masknl) {
	gFirewallRules = new GgsnFirewallRule(gFirewallRules,ipbasenl,masknl);
}


static mg_con_t *mg_cons = 0;


// Now in Utils.cpp
//const char *timestr()
//{
//    struct timeval tv;
//    struct tm tm;
//    static char result[30];
//    gettimeofday(&tv,NULL);
//    localtime_r(&tv.tv_sec,&tm);
//    unsigned tenths = tv.tv_usec / 100000;  // Rounding down is ok.
//    sprintf(result," %02d:%02d:%02d.%1d",tm.tm_hour,tm.tm_min,tm.tm_sec,tenths);
//    return result;
//}



// Find a free IP connection or return NULL.
// Each PDP context activation gets a new IP address.
// The IP addresses are recycled after tiimeout.
// 7-10-2012: Each MS may ask for several IP addresses with different NSAPI.
// The IMSI+NSAPI now maps to an IP address in a semi-permanent way, so the MS
// effectively has a static IP address within the range assigned by the BTS,
// until the BTS is power cycled.  The ptmsi is a unique id associated with the imsi.
mg_con_t *mg_con_find_free(uint32_t ptmsi, int nsapi)
{
	// Start by looking for this specific old connection:
	// TODO: This should use a map for efficiency.
	int i;
	mg_con_t *mgp = &mg_cons[0];
	for (i=0; i < ggConfig.mgMaxConnections; i++, mgp++) {
		if (mgp->mg_ptmsi == ptmsi && mgp->mg_nsapi == nsapi) {
			return mgp;
		}
	}

	// Look for an unused IP address.
	double now = pat_timef();
	static int mgnextindex = 0;
	for (i=0; i < ggConfig.mgMaxConnections; i++) {
		mgp = &mg_cons[mgnextindex];
		if (++mgnextindex == ggConfig.mgMaxConnections) { mgnextindex = 0; }
		if (mgp->mg_pdp == NULL) {
			// Dont reuse an ip address for mg_ip_timeout.
			// TCP packets will continue to arrive for an IP address
			// for quite some time after it becomes inactive.
			if (mgp->mg_time_last_close && mgp->mg_time_last_close + ggConfig.mgIpTimeout > now) continue;
			//mgp->mg_pdp = pctx;
			mgp->mg_ptmsi = ptmsi;
			mgp->mg_nsapi = nsapi;
			return mgp;
		}
	}
	return NULL;
}

void mg_con_open(mg_con_t *mgp,PdpContext *pdp)
{
	mgp->mg_pdp = pdp;
}

void mg_con_close(mg_con_t *mgp)
{
	mgp->mg_pdp = NULL;
	mgp->mg_time_last_close = pat_timef();
}

#if 0
static mg_con_t *mg_con_find_by_ctx(PdpContext *pctx)
{
	int i; mg_con_t *mgp = mg_cons;
	for (i=0; i < ggConfig.mgMaxConnections; i++, mgp++) {
		if (mgp->mg_pdp == pctx) { return mgp; }
	}
	return NULL;
}
#endif

static mg_con_t *mg_con_find_by_ip(uint32_t addr)
{
	int i; mg_con_t *mgp = mg_cons;
	for (i=0; i < ggConfig.mgMaxConnections; i++, mgp++) {
		if (mgp->mg_ip == addr) { return mgp; }
	}
	return NULL;
}

static bool verbose = true;

static char *packettoa(char *result,unsigned char *packet, int len)
{
	struct iphdr *iph = (struct iphdr*)packet;
	char nbuf1[40], nbuf2[40];
	if (verbose && iph->protocol == IPPROTO_TCP) {
		struct tcphdr *tcph = (struct tcphdr*) (packet + 4 * iph->ihl);
		sprintf(result,"proto=%s %d byte packet seq=%u ack=%u id=%u frag=%u from %s:%d to %s:%d",
			ip_proto_name(iph->protocol), len, tcph->seq, tcph->ack_seq,
			iph->id, iph->frag_off,
			ip_ntoa(iph->saddr,nbuf1),tcph->source,
			ip_ntoa(iph->daddr,nbuf2),tcph->dest);
	} else {
		sprintf(result,"proto=%s %d byte packet from %s to %s",
			ip_proto_name(iph->protocol), len,
			ip_ntoa(iph->saddr,nbuf1),
			ip_ntoa(iph->daddr,nbuf2));
	}
	return result;
}


unsigned char *miniggsn_rcv_npdu(int *plen, uint32_t *dstaddr)
{
	static unsigned char *recvbuf = NULL;

	if (recvbuf == NULL) {
		recvbuf = (unsigned char*)malloc(ggConfig.mgMaxPduSize+2);
		if (!recvbuf) { /**error = -ENOMEM;*/ return NULL; }
	}

	// The O_NONBLOCK was set by default!  Is not happening any more.
	{
		int flags = fcntl(tun_fd,F_GETFL,0);
		if (flags & O_NONBLOCK) {
			//MGDEBUG(4,"O_NONBLOCK = %d",O_NONBLOCK & flags);
			flags &= ~O_NONBLOCK;	// Want Blocking!
			int fcntlstat = fcntl(tun_fd,F_SETFL,flags);
			MGWARN("ggsn: WARNING: Turning off tun_fd blocking flag, fcntl=%d",fcntlstat);
		}
	}

	// We can just read from the tunnel.
	int ret = read(tun_fd,recvbuf,ggConfig.mgMaxPduSize);
	if (ret < 0) {
		MGERROR("ggsn: error: reading from tunnel: %s", strerror(errno));
		//*error = ret;
		return NULL;
	} else if (ret == 0) {
		MGERROR("ggsn: error: zero bytes reading from tunnel: %s", strerror(errno));
		//*error = ret;	// huh?
		return NULL;
	} else {
		struct iphdr *iph = (struct iphdr*)recvbuf;
		{
			char infobuf[200];
			MGINFO("ggsn: received %s at %s",packettoa(infobuf,recvbuf,ret), timestr().c_str());
			//MGLOGF("ggsn: received proto=%s %d byte npdu from %s for %s at %s",
				//ip_proto_name(iph->protocol), ret,
				//ip_ntoa(iph->saddr,nbuf),
				//ip_ntoa(iph->daddr,NULL), timestr());
		}

		*dstaddr = iph->daddr;
		// TODO: Do we have to allocate a new buffer?
		*plen = ret;
		// Zero terminate for the convenience of the pinger.
		recvbuf[ret] = 0;
		return recvbuf;
	}
}

// If this is a duplicate TCP packet, throw it away.
// The MS is so slow to respond that the servers often send dups
// which are unnecessary because we have reliable communication between here
// and the MS, so just toss them.
// Update 3-2012: Always do the check to print messages for dup packets even if not discarded.
static int mg_toss_dup_packet(mg_con_t*mgp,unsigned char *packet, int packetlen)
{
	struct iphdr *iph = (struct iphdr*)packet;
	if (iph->protocol != IPPROTO_TCP) { return 0; }
	struct tcphdr *tcph = (struct tcphdr*) (packet + 4 * iph->ihl);
	if (tcph->rst | tcph->urg) { return 0; }
	int i;
	for (i = 0; i < MG_PACKET_HISTORY; i++) {
		// 3-2012: Jpegs are not going through the system properly.
		// I am adding some more checks here to see if we are tossing packets inappropriately.
		// The tot_len includes headers, but if they are not the same in the duplicate packet, oh well.
		// TODO: If the connection is reset we should zero out our history.
		if (mgp->mg_packets[i].saddr == iph->saddr &&
		    mgp->mg_packets[i].daddr == iph->daddr &&
		    mgp->mg_packets[i].totlen == iph->tot_len &&
		    //mgp->mg_packets[i].ipfragoff == iph->frag_off &&
		    //mgp->mg_packets[i].ipid == iph->id &&
		    mgp->mg_packets[i].seq == tcph->seq &&
		    mgp->mg_packets[i].source == tcph->source &&
		    mgp->mg_packets[i].dest == tcph->dest
			) {
				const char *what = ggConfig.mgIpTossDup ? "discarding " : "";
				char buf1[40],buf2[40];
				MGINFO("ggsn: %sduplicate %d byte packet seq=%d frag=%d id=%d src=%s:%d dst=%s:%d",what,
					packetlen,tcph->seq,iph->frag_off,iph->id,
					ip_ntoa(iph->saddr,buf1),tcph->source,
					ip_ntoa(iph->daddr,buf2),tcph->dest);
				return ggConfig.mgIpTossDup;	// Toss duplicate tcp packet if option set.
			}
	}
	i = mgp->mg_oldest_packet;
	if (++mgp->mg_oldest_packet >= MG_PACKET_HISTORY) { mgp->mg_oldest_packet = 0; }
	mgp->mg_packets[i].saddr = iph->saddr;
	mgp->mg_packets[i].daddr = iph->daddr;
	mgp->mg_packets[i].totlen = iph->tot_len;
	//mgp->mg_packets[i].ipfragoff = iph->frag_off;
	//mgp->mg_packets[i].ipid = iph->id;
	mgp->mg_packets[i].seq = tcph->seq;
	mgp->mg_packets[i].source = tcph->source;
	mgp->mg_packets[i].dest = tcph->dest;
	return 0;	// Do not toss.
}

// There is data available on the socket.  Go get it.
// see handle_nsip_read()
void miniggsn_handle_read()
{
	int packetlen;
	uint32_t dstaddr;
	unsigned char *packet = miniggsn_rcv_npdu(&packetlen, &dstaddr);
	if (!packet) { return; }

	// We need to reassociate the packet with the PdpContext to which it belongs.
	mg_con_t *mgp = mg_con_find_by_ip(dstaddr);
	if (mgp == NULL || mgp->mg_pdp == NULL) {
		MGERROR("ggsn: error: cannot find PDP context for incoming packet for IP dstaddr=%s",
			ip_ntoa(dstaddr,NULL));
		return;	// -1;
	}

	if (mg_toss_dup_packet(mgp,packet,packetlen)) { return; }

	PdpContext *pdp = mgp->mg_pdp;
	//MGDEBUG(2,"miniggsn_handle_read pdp=%p",pdp);
	pdp->pdpWriteHighSide(packet,packetlen);
}


// The npdu is a raw packet including the ip header.
int miniggsn_snd_npdu_by_mgc(mg_con_t *mgp,unsigned char *npdu, unsigned len)
{
    // Verify the IP header.
    struct iphdr *ipheader = (struct iphdr*)npdu;
    // The return address must be the MS itself.
    uint32_t ms_ip_address = mgp->mg_ip;
    uint32_t packet_source_ip_addr = ipheader->saddr;
    uint32_t packet_dest_ip_addr = ipheader->daddr;

	char infobuf[200];
	MGINFO("ggsn: writing %s at %s",packettoa(infobuf,npdu,len),timestr().c_str());
	//MGLOGF("ggsn: writing proto=%s %d byte npdu to %s from %s at %s",
		//ip_proto_name(ipheader->protocol),
		//len,ip_ntoa(packet_dest_ip_addr,NULL),
		//ip_ntoa(packet_source_ip_addr,nbuf), timestr().c_str());

#define MUST_HAVE(assertion) \
    if (! (assertion)) { MGERROR("ggsn: Packet failed test, discarded: %s",#assertion); return -1; }

    if (mg_debug_level > 2) ip_hdr_dump(npdu,"npdu");
    MUST_HAVE(ipheader->version == 4);	// 4 as in IPv4
    MUST_HAVE(ipheader->ihl >= 5);		// Minimum header length is 5 words.

    int checksum = ip_checksum(ipheader,sizeof(*ipheader),NULL);
    MUST_HAVE(checksum == 0);				// If fails, packet is bad.

    MUST_HAVE(ipheader->ttl > 0);		// Time to live - how many hops allowed.


	// The blackberry sends ICMP packets, so we better support.
	// I'm just going to allow any protocol through.
    //MUST_HAVE(ipheader->protocol == IPPROTO_TCP || 
	//	ipheader->protocol == IPPROTO_UDP ||
	//	ipheader->protocol == IPPROTO_ICMP);

    MUST_HAVE(packet_source_ip_addr == ms_ip_address);

#if OLD_FIREWALL
    // The destination address may not be a local address on this machine
	// as configured by the operator.
	// Note these are all in network order, so be careful.
    //uint32_t local_ip_addr = inet_addr(mg_base_ip_str); // probably "192.168.99.1"
	uint32_t net_mask = inet_addr(mg_net_mask);			// probably "255.255.255.0"
    MUST_HAVE((packet_dest_ip_addr & net_mask) != (local_ip_addr & net_mask));
#endif

	for (GgsnFirewallRule *rp = gFirewallRules; rp; rp = rp->next) {
		MUST_HAVE((packet_dest_ip_addr & rp->ipMasknl) != (rp->ipBasenl & rp->ipMasknl));
	}

    // Decrement ttl and recompute checksum.  We are doing this in place.
    ipheader->ttl--;
    ipheader->check = 0;
    //ipheader->check = htons(ip_checksum(ipheader,sizeof(*ipheader),NULL));
    ipheader->check = ip_checksum(ipheader,sizeof(*ipheader),NULL);

	// Just write to the MS-side tunnel device.

	int result = write(tun_fd,npdu,len);
	if (result != (int) len) {
		MGERROR("ggsn: error: write(tun_fd,%d) result=%d %s",len,result,strerror(errno));
	}
    return 0;
}

#if 0 // not needed
// Route an n-pdu from the MS out to the internet.
// Called by SNDCP when it has received/re-assembled a N-PDU
int miniggsn_snd_npdu(PdpContext *pctx,unsigned char *npdu, unsigned len)
{
	// Find the fd from the pctx;  We should put this in the pdp_ctx.
	mg_con_t *mgp = mg_con_find_by_ctx(pctx);
	if (mgp == NULL) { return -1; }		// Whoops
	return miniggsn_snd_npdu_by_mgc(mgp, npdu, len);
}
#endif

time_t gGgsnInitTime;

bool miniggsn_init()
{
	static int initstatus = -1;		// -1: uninited; 0:init failed; 1: init succeeded.
	if (initstatus >= 0) {return initstatus;}
	initstatus = 0;	// assume failure.


	// We init config options at GGSN startup.
	// They cannot be changed while running.
	// To change an option, you would have to stop and restart the GGSN.
	ggConfig.mgIpTimeout = gConfig.getNum("GGSN.IP.ReuseTimeout");
	ggConfig.mgMaxPduSize = gConfig.getNum("GGSN.IP.MaxPacketSize");
	ggConfig.mgMaxConnections = gConfig.getNum("GGSN.MS.IP.MaxCount");
	ggConfig.mgIpTossDup = gConfig.getBool("GGSN.IP.TossDuplicatePackets");


	string logfile = gConfig.getStr("GGSN.Logfile.Name");
	if (logfile.length()) {
		mg_log_fp = fopen(logfile.c_str(),"w");
		if (mg_log_fp == 0) {
			MGERROR("could not open %s log file:%s","GGSN.Logfile.Name",logfile.c_str());
		}
	}

	// This is the first message in the newly opened file.
	time(&gGgsnInitTime);
	MGINFO("Initializing Mini GGSN %s",ctime(&gGgsnInitTime));	// ctime includes a newline.

	if (mg_log_fp) {
		mg_debug_level = 1;
		MGINFO("GGSN logging to file %s",logfile.c_str());
	}

	if (ggConfig.mgMaxConnections > 254) {
		MGERROR("GGSN.MS.IP.MaxCount specifies too many connections (%d) specifed, using 254",
			ggConfig.mgMaxConnections);
		ggConfig.mgMaxConnections = 254;
	}

	// We need three IP things:
	// 1. the route expressed using "/maskbits" notation,
	// 2. the base ip address,
	// 3. the mask for the MS (which has very little to do with the netmask of the host we are running on.)
	// All three can be derived from the ipRoute, if specfied.
	// But conceivably the user might want to start their base ip address elsewhere.

	const char *ip_base_str = gConfig.getStr("GGSN.MS.IP.Base").c_str();
	uint32_t mgIpBasenl = inet_addr(ip_base_str);
	if (mgIpBasenl == INADDR_NONE) {
		MGERROR("miniggn: GGSN.MS.IP.Base address invalid:%s",ip_base_str);
		return false;
	}

	if ((ntohl(mgIpBasenl) & 0xff) == 0) {
		MGERROR("miniggn: GGSN.MS.IP.Base address should not end in .0 but proceeding anyway: %s",ip_base_str);
	}

	const char *route_str = 0;
	char route_buf[40];
	string route_save;
	if (gConfig.defines("GGSN.MS.IP.Route")) {
		route_save = gConfig.getStr("GGSN.MS.IP.Route");
		route_str = route_save.c_str();
	}

	uint32_t route_basenl, route_masknl;
	if (route_str && *route_str && *route_str != ' ') {
		if (strlen(route_str) > strlen("aaa.bbb.ccc.ddd/yy") + 2) {	// add some slop.
			MGWARN("miniggn: GGSN.MS.IP.Route address is too long:%s",route_str);
			// but use it anyway.
		}

		if (! ip_addr_crack(route_str,&route_basenl,&route_masknl) || route_basenl == INADDR_NONE) {
			MGWARN("miniggsn: GGSN.MS.IP.Route is not a valid ip address: %s",route_str);
			// but use it anyway.
		}
		if (route_masknl == 0) {
			MGWARN("miniggsn: GGSN.MS.IP.Route is not a valid route, /mask part missing or invalid: %sn",
				route_str);
			// but use it anyway.
		}

		// We would like to check that the base ip is within the ip route range,
		// which is tricky, but check the most common case:
		if ((route_basenl&route_masknl) != (mgIpBasenl&route_masknl)) {
			MGWARN("miniggsn: GGSN.MS.IP.Base = %s ip address does not appear to be in range of GGSN.MS.IP.Route = %s",
				ip_base_str, route_str);
			// but use it anyway.
		}
	} else {
		// Manufacture a route string.  Assume route is 24 bits.
		route_masknl = inet_addr("255.255.255.0");
		route_basenl = mgIpBasenl & route_masknl;	// Set low byte to 0.
		ip_ntoa(route_basenl,route_buf);
		strcat(route_buf,"/24");
		route_str = route_buf;
	}

	// Firewall rules:
	bool firewall_enable;
	if ((firewall_enable = gConfig.getNum("GGSN.Firewall.Enable"))) {
		// Block anything in the routed range:
		addFirewallRule(route_basenl,route_masknl);
		// Block local loopback:
		uint32_t tmp_basenl,tmp_masknl;
		if (ip_addr_crack("127.0.0.1/24",&tmp_basenl,&tmp_masknl)) {
			addFirewallRule(tmp_basenl,tmp_masknl);
		}
		// Block the OpenBTS station itself:
		uint32_t *myaddrs = ip_findmyaddr();
		for ( ; *myaddrs != (unsigned)-1; myaddrs++) {
			addFirewallRule(*myaddrs,0xffffffff);
		}
		if (firewall_enable >= 2) {
			// Block all private addresses:
			// 16-bit block (/16 prefix, 256 × C) 	192.168.0.0 	192.168.255.255 	65536
			uint32_t private_addrnl = inet_addr("192.168.0.0");
			uint32_t private_masknl = inet_addr("255.255.0.0");
			addFirewallRule(private_addrnl,private_masknl);
			// 20-bit block (/12 prefix, 16 × B) 	172.16.0.0 	172.31.255.255 	1048576
			private_addrnl = inet_addr("172.16.0.0");
			private_masknl = inet_addr("255.240.0.0");
			addFirewallRule(private_addrnl,private_masknl);
			// 24-bit block (/8 prefix, 1 × A) 	10.0.0.0 	10.255.255.255 	16777216
			private_addrnl = inet_addr("10.0.0.0");
			private_masknl = inet_addr("255.0.0.0");
			addFirewallRule(private_addrnl,private_masknl);
		}
	}

	MGINFO("GGSN Configuration:");
		MGINFO("  GGSN.MS.IP.Base=%s", ip_ntoa(mgIpBasenl,NULL));
		MGINFO("  GGSN.MS.IP.MaxCount=%d", ggConfig.mgMaxConnections);
		MGINFO("  GGSN.MS.IP.Route=%s", route_str);
		MGINFO("  GGSN.IP.MaxPacketSize=%d", ggConfig.mgMaxPduSize);
		MGINFO("  GGSN.IP.ReuseTimeout=%d", ggConfig.mgIpTimeout);
		MGINFO("  GGSN.Firewall.Enable=%d", firewall_enable);
		MGINFO("  GGSN.IP.TossDuplicatePackets=%d", ggConfig.mgIpTossDup);
	if (firewall_enable) {
		MGINFO("GGSN Firewall Rules:");
		for (GgsnFirewallRule *rp = gFirewallRules; rp; rp = rp->next) {
			char buf1[40], buf2[40];
			MGINFO("  block ip=%s mask=%s",ip_ntoa(rp->ipBasenl,buf1),ip_ntoa(rp->ipMasknl,buf2));
		}
	}
	uint32_t dns[2];	// We dont use the result, we just want to print out the DNS servers now.
	ip_finddns(dns);	// The dns servers are polled again later.

	const char *tun_if_name = gConfig.getStr("GGSN.TunName").c_str();

	if (tun_fd == -1) {
		ip_init();
		tun_fd = ip_tun_open(tun_if_name,route_str);
		if (tun_fd < 0) {
			MGERROR("ggsn: ERROR: Could not open tun device %s",tun_if_name);
			return false;
		}
	}

	// DEBUG: Try it again.
	//printf("DEBUG: Opening tunnel again: %d\n",ip_tun_open(tun_if_name,route_str));

	if (mg_cons) free(mg_cons);
	mg_cons = (mg_con_t*)calloc(ggConfig.mgMaxConnections,sizeof(mg_con_t));
	if (mg_cons == 0) {
		MGERROR("ggsn: ERROR: out of memory");
		return false;
	}
	//memset(mg_cons,0,sizeof(mg_cons));

	uint32_t base_iphl = ntohl(mgIpBasenl);
	// 8-15:  no dont do this.  It subverts the purpose of the BASE ip address.
	//base_iphl &= ~255;			// In case they specify 192.168.2.1, make it 192.168.2.0
	int i;
	// If the last digit is 0 (192.168.99.0), change it to 1 for the first IP addr served.
	if ((base_iphl & 255) == 0) { base_iphl++; }
	for (i=0; i < ggConfig.mgMaxConnections; i++) {
		mg_cons[i].mg_ip = htonl(base_iphl + i);
		//mg_cons[i].mg_ip = htonl(base_iphl + 1 + i);
		// DEBUG!!!!!  Use my own ip address.
		//mg_cons[i].mg_ip = inet_addr("192.168.1.99");
		//printf("adding IP=%s\n",ip_ntoa(mg_cons[i].mg_ip,NULL));
	}
	initstatus = 1;
	return initstatus;
}


};	// namespace
