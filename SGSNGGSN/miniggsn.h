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

#ifndef _MINIGGSN_H_
#define _MINIGGSN_H_
#include <time.h>
#include "Logger.h"

namespace SGSN {

struct PdpContext;

// MiniGGSN IP connections.
// This holds the IP address and is slightly different than a PDPContext:
// o The IP address is permanently allocated.
// o The PDPContext is strictly a handle for the MS; it is deleted when PDPContext is deactivated
//		or when the MS is deleted.
// o There can be multiple PDPContext with the same IP address.
// o When a PDPContext is deallocated, there may still be messages for it in the message queue,
// 		so those messages point to the permanet mg_con_s instead of the PdpContext.
// o The IP address must remain reserved for a period of time after a PdpContext is deleted/deactivated.
// Note: This was written in C originally.
typedef struct mg_con_s {
	PdpContext *mg_pdp;		// Points back to the PDP context using this connection.
	uint32_t mg_ptmsi;		// The ptmsi that is using this IP connection.
	int mg_nsapi;			// The nsapi in this ptmsi that is using this IP connection.
	uint32_t mg_ip;			// The IP address used for this connection, in network order.
	// Keep track of the last few tcp packets received:
#define MG_PACKET_HISTORY 60
	struct mg_packets {
		uint16_t source, dest;	// TCP source and dest ports.
		uint16_t totlen;		// IP length, which includes headers.
		uint32_t seq;			// TCP sequence number
		uint32_t saddr, daddr;	// source and destination IP addr
		uint16_t ipid, ipfragoff;	// Added 3-2012
	} mg_packets[MG_PACKET_HISTORY];
	int mg_oldest_packet;
	double mg_time_last_close;
} mg_con_t;
#define MG_CON_DEFINED

unsigned char *miniggsn_rcv_npdu(int *error, int *plen, uint32_t *dstaddr);
int miniggsn_snd_npdu(PdpContext *pctx,unsigned char *npdu, unsigned len);
int miniggsn_snd_npdu_by_mgc(mg_con_t *mgp,unsigned char *npdu, unsigned len);
void miniggsn_handle_read();
bool miniggsn_init();
mg_con_t *mg_con_find_free(uint32_t ptmsi, int nsapi);
void mg_con_close(mg_con_t *mgp);
void mg_con_open(mg_con_t *mgp,PdpContext *pdp);

//extern int pinghttp(char *whoto,char *whofrom,mg_con_t *mgp);

extern int tun_fd;

// From iputils.h:
bool ip_addr_crack(const char *address,uint32_t *paddr, uint32_t *pmask);
char *ip_ntoa(int32_t ip, char *buf);
char *ip_sockaddr2a(void * /*struct sockaddr * */ sap,char *buf);
int ip_add_addr(char *ifname, int32_t ipaddr, int maskbits);
const char *ip_proto_name(int ipproto);
unsigned int ip_checksum(void *ptr, unsigned len, void *dummyhdr);
void ip_hdr_dump(unsigned char *packet, const char *msg);
int runcmd(const char *path, ...);
int ip_tun_open(const char *tname, const char *addrstr);
void ip_init();
int ip_finddns(uint32_t*);
uint32_t *ip_findmyaddr();
extern double pat_timef(), pat_elapsedf();


// Logging.  These date from when this was part of the SGSN, written in C.
extern FILE *mg_log_fp;
extern time_t gGgsnInitTime;
	// formerly: fprintf(mg_log_fp,"%.1f:",pat_timef() - gGgsnInitTime);
#define MGLOGF(...) if (mg_log_fp) { \
	fprintf(mg_log_fp,"%s:",timestr().c_str()); \
	fprintf(mg_log_fp, __VA_ARGS__); \
	fputc('\n',mg_log_fp); \
	fflush(mg_log_fp); \
	}
#define MGLOG(stuff) {std::ostringstream ss; ss<<stuff; MGLOGF("%s",ss.str().c_str());}

#define MGDEBUG(lev,...) MGLOGF(__VA_ARGS__) {char *tmp;if (asprintf(&tmp,__VA_ARGS__)>0){LOG(DEBUG)<<tmp;free(tmp);}}

#define MGERROR(...) {MGLOGF(__VA_ARGS__) char *tmp;if (asprintf(&tmp,__VA_ARGS__)>0){LOG(ERR)<<tmp;free(tmp);}}
#define MGWARN(...) {MGLOGF(__VA_ARGS__) char *tmp;if (asprintf(&tmp,__VA_ARGS__)>0){LOG(WARNING)<<tmp;free(tmp);}}
#define MGINFO(...) {MGLOGF(__VA_ARGS__) char *tmp;if (asprintf(&tmp,__VA_ARGS__)>0){LOG(INFO)<<tmp;free(tmp);}}
#define MGINFO2(...) {MGINFO(__VA_ARGS__) \
	printf(__VA_ARGS__);putchar('\n');fflush(stdout); }


};	// namespace
#endif
