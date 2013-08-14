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

//#include <osmocom/core/talloc.h>
//#include <osmocom/core/select.h>
//#include <osmocom/core/rate_ctr.h>
//#include <openbsc/gsm_04_08_gprs.h>

//#include <openbsc/signal.h>
//#include <openbsc/debug.h>
//#include <openbsc/sgsn.h>
//#include <openbsc/gprs_llc.h>
//#include <openbsc/gprs_bssgp.h>
//#include <openbsc/gprs_sgsn.h>
//#include <openbsc/gprs_gmm.h>
//#include <openbsc/socket.h>	// pat added for make_sock

#include <netdb.h>			// pat added for gethostbyname
#include <netinet/ip.h>		// pat added for IPv4 iphdr
#include <netinet/tcp.h>		// pat added for tcphdr
#include <netinet/udp.h>		// pat added for udphdr

#include <linux/if.h>			// pat added.
#include <linux/if_tun.h>			// pat added.
#include <sys/ioctl.h>			// pat added.
#include <assert.h>				// pat added
#include <stdarg.h>				// pat added
#include <time.h>				// pat added.
#include <sys/types.h>
#include <wait.h>
//#include "miniggsn.h"

#ifndef MG_CON_DEFINED
// MiniGGSN IP connections.  One of these structs for each PDP context.
// Each PDP context will be mapped to a new IP address.
// There should be a pointer to this struct in the sgsn_pdp_ctx,
// but I am trying to keep all changes local for now.
typedef struct mg_con_s {
    struct sgsn_pdp_ctx *mg_pctx;   // Points back to the PDP context using this connection.
	uint32_t mg_ip;                 // The IP address used for this connection, in network order.
	int inited;
} mg_con_t;

#endif

// From iputils.h:
char *ip_ntoa(int32_t ip, char *buf);
char *ip_sockaddr2a(void * /*struct sockaddr * */ sap,char *buf);
int ip_add_addr(char *ifname, int32_t ipaddr, int maskbits);
unsigned int ip_checksum(void *ptr, unsigned len, void *dummyhdr);
void ip_hdr_dump(unsigned char *packet, const char *msg);
int ip_tun_open(char *tname, char *addrstr);
void ip_init();


// These options are used only for standalone testing.
struct options_s {
    int bind;
	int printall;	// Print entire tcp response.
	int raw_bind;
	char *from;
	int v;
	int use_tunnel;
	int repeat;
};
struct options_s options;

#if STANDALONE
char *tun_if_name = "mstun";
int tun_fd = -1;
struct options_s options;
static char *mg_base_ip_str = "192.168.99.1";	// This did not raw bind.
static char *mg_base_ip_route = "192.168.99.0/24";
#define MGERROR(...) {printf("error:"); printf(__VA_ARGS__);}
#define MGINFO(...) {printf(__VA_ARGS__);}
#else
char *miniggsn_rcv_npdu(struct osmo_fd *bfd, int *error, int *plen) { return 0; }
int miniggsn_snd_npdu(struct sgsn_pdp_ctx *pctx,unsigned char *npdu, unsigned len) { return 0; }
int miniggsn_snd_npdu_by_mgc(mg_con_t *mgp,char *npdu, unsigned len) { return 0; }
void miniggsn_delete_pdp_ctx(struct sgsn_pdp_ctx *pctx) {}
struct sgsn_pdp_ctx *miniggsn_create_pdp_conf(struct pdp_t *pdp,struct sgsn_pdp_ctx *pctx) { return 0;}
void miniggsn_init();
#endif

#if STANDALONE

// ip address is in network order;  return as dotted string.
char *ip_ntoa(int32_t ip, char *buf)
{
	static char sbuf[30];
	if (buf == NULL) { buf = sbuf;}
	ip = ntohl(ip);
	sprintf(buf,"%d.%d.%d.%d",(ip>>24)&0xff,(ip>>16)&0xff,(ip>>8)&0xff,ip&0xff);
	return buf;
}
// IP standard checksum, see wikipedia "IPv4 Header"
// len is in bytes, will normally be 20 == sizeof(struct iphdr).
unsigned int ip_checksum(void *ptr, unsigned len, void *dummyhdr)
{
	//if (len != 20) printf("WARNING: unexpected header length in ip_checksum\n");
    uint32_t i, sum = 0;
    uint16_t *pp = (uint16_t*)ptr;
	while (len > 1) { sum += *pp++; len -= 2; }
	if (len == 1) {
		uint16_t foo = 0;
		unsigned char *cp = (unsigned char*)&foo;
		*cp = *(unsigned char *)pp;
		sum += foo;
	}
	if (dummyhdr) {	// For TCP and UDP the dummy header is 3 words = 6 shorts.
		pp = (uint16_t*)dummyhdr;
		for (i = 0; i < 6; i++) { sum += pp[i]; }
	}
	//printf("intermediate sum=0x%x\n",sum);
    // Convert from 2s complement to 1s complement:
	//sum = ((sum >> 16)&0xffff) + (sum & 0xffff); /* add hi 16 to low 16 */
	sum = ((sum >> 16)) + (sum & 0xffff); /* add hi 16 to low 16 */
	sum += (sum >> 16);         /* add carry */
	//printf("intermediate sum16=0x%x result=0x%x\n",sum,~sum);
    return 0xffff & ~sum;
}

void ip_hdr_dump(unsigned char *packet, const char *msg)
{
	struct iptcp {	// This is only accurate if no ip options specified.
		struct iphdr ip;
		struct tcphdr tcp;
	};
	struct iptcp *pp = (struct iptcp*)packet;
	char nbuf[100];
	printf("%s: ",msg);
	printf("%d bytes protocol=%d saddr=%s daddr=%s version=%d ihl=%d tos=%d id=%d\n",
		ntohs(pp->ip.tot_len),pp->ip.protocol,ip_ntoa(pp->ip.saddr,nbuf),ip_ntoa(pp->ip.daddr,NULL),
		pp->ip.version,pp->ip.ihl,pp->ip.tos, ntohs(pp->ip.id));
	printf("\tcheck=%d computed=%d frag=%d ttl=%d\n",
		pp->ip.check,ip_checksum(packet,sizeof(struct iphdr),NULL),
		ntohs(pp->ip.frag_off),pp->ip.ttl);
	printf("\ttcp SYN=%d ACK=%d FIN=%d RES=%d sport=%u dport=%u\n",
		pp->tcp.syn,pp->tcp.ack,pp->tcp.fin,pp->tcp.rst,
		ntohs(pp->tcp.source),ntohs(pp->tcp.dest));
	printf("\t\tseq=%u ackseq=%u window=%u check=%u\n",
		ntohl(pp->tcp.seq),ntohl(pp->tcp.ack_seq),htons(pp->tcp.window),htons(pp->tcp.check));
}
char *ip_sockaddr2a(void * /*struct sockaddr * */ sap,char *buf)
{
	static char sbuf[100];
	if (buf == NULL) { buf = sbuf;}
	struct sockaddr_in *to = (struct sockaddr_in*)sap;
	sprintf(buf,"proto=%d%s port=%d ip=%s",
		ntohs(to->sin_family),
		to->sin_family == AF_INET ? "(AF_INET)" : "",
		ntohs(to->sin_port),
		ip_ntoa(to->sin_addr.s_addr,NULL));
	return buf;
}
// Run the command.
// This is not ip specific, but used to call linux "ip" and "route" commands.
// If commands starts with '|', capture stdout and return the file descriptor to read it.
int runcmd(const char *path, ...)
{
	int pipefd[2];
	int dopipe = 0;
	if (*path == '|') {
		path++;
		dopipe = 1;
		if (pipe(pipefd) == -1) {
			MGERROR("could not create pipe: %s",strerror(errno));
			return -1;
		}
	}
	int pid = fork();
	if (pid == -1) {
		MGERROR("could not fork: %s",strerror(errno));
		return -1;
	}
	if (pid) {	// This is the parent; wait for child to exit, then return childs status.
		int status;
		if (dopipe) {
			close(pipefd[1]); // Close unused write end of pipe.
		}
		waitpid(pid,&status,0);
		return dopipe ? pipefd[0] : status;	// Return read end of pipe, if piped.
	}
	// This is the child process.
	if (dopipe) {
		close(pipefd[0]); // Close unused read end of pipe.
		dup2(pipefd[1],1);	// Capture stdout to pipe.
		close(pipefd[1]);	// Close now redundant fd.
	}

	// Gather args into argc,argv;
	int argc = 0; char *argv[100];
	va_list ap;
	va_start(ap, path);
	do {
		argv[argc] = va_arg(ap,char*);
	} while (argv[argc++]);
	argv[argc] = NULL;
	va_end(ap);

	// Print them out.
	// But dont print if piped, because it goes into the pipe!
	if (! dopipe) {
		char buf[208], *bp = buf, *ep = &buf[200];
		int i;
		for (i = 0; argv[i]; i++) {
			int len = strlen(argv[i]);
			if (bp + len > ep) { strcpy(bp,"..."); break; }
			strcpy(bp,argv[i]);
			bp += len;
			*bp++ = ' ';
			*bp = 0;
		}
		buf[200] = 0;
		MGINFO("%s",buf);
	}

	// exec them.
	execv(path,argv);
	exit(2);	// Just in case.
	return 0;	// This is never used.
}
// Return an array of ip addresses, terminated by a -1 address.
uint32_t *ip_findmyaddr()
{
#define maxaddrs 5
	static uint32_t addrs[maxaddrs+1];
	int n = 0;
	int fd = runcmd("|/bin/hostname","hostname","-I", NULL);
	if (fd < 0) {
		failed:
		addrs[0] = (unsigned) -1;	// converts to all 1s
		return addrs;
	}
	//printf("ip_findmyaddr fd=%d\n",fd);
	const int bufsize = 2000;
	char buf[bufsize];
	int size = read(fd,buf,bufsize);
	if (size < 0) { goto failed; }
	buf[bufsize-1] = 0;
	if (size < bufsize) { buf[size] = 0; }
	char *cp = buf;
	//printf("ip_findmyaddr buf=%s\n",buf);
	while (n < maxaddrs) {
		char *ep = strchr(cp,'\n');
		if (ep) *ep = 0;
		if (strlen(cp)) {
			uint32_t addr = inet_addr(cp);
			//printf("ip_findmyaddr cp=%s = 0x%x\n",cp,addr);
			if (addr != 0 && addr != (unsigned)-1) {
				addrs[n++] = inet_addr(cp);
			}
		}
		if (!ep) {
			addrs[n] = (unsigned) -1;	// terminate the list.
			return addrs;
		}
		cp = ep+1;
	}
	addrs[maxaddrs] = (unsigned) -1;	// converts to all 1s
	return addrs;
}
int ip_tun_open(char *tname, char *addrstr) { assert(0); }
#endif


typedef struct {
	int tot_len;
	struct iphdr *ip;		// Pointer to ip header in buf ( == buf).
	struct tcphdr *tcp;		// Pointer to tcp header in buf, if it is tcp.
	struct udphdr *udp;		// Pointer to udp header in buf, if it is udp.
	char *payload;			// Pointer to data in buf.
	char storage[0];		// Storage for packet data, if this packet_t was malloced.
} packet_t;

// Return -1 on error
static int ip_findaddr(char *spec, struct sockaddr *addr, char *hostname)
{
	struct sockaddr_in *to = (struct sockaddr_in*)addr;
	memset(addr, 0, sizeof(struct sockaddr));
    to->sin_family = AF_INET;
	if (inet_aton(spec,&to->sin_addr)) {
		if (hostname) strcpy(hostname,spec);
	} else {
		//alternate: getaddrinfo()
        struct hostent *hp = gethostbyname(spec);	// func is deprecated.
        if (hp) {
            to->sin_family = hp->h_addrtype;
            memcpy(&to->sin_addr,hp->h_addr, hp->h_length);
			if (hostname) strcpy(hostname,hp->h_name);
        } else {
			return -1;	// failure
        }
    }
	return 0;	// ok
}

// Add an ip header to the packet and return in malloced memory.
// ipsrc,ipdst,srcport,dstport in network order.
// payload may be NULL to just send the header with flags.
packet_t *
ip_add_hdr2(int protocol, uint32_t ipsrc, uint32_t ipdst,
	char *payload, struct tcphdr *tcpin, unsigned ipid)
{
	int hdrsize = sizeof(struct iphdr);
	int paylen = payload ? strlen(payload) : 0;
	struct tcphdr *tcp;
	struct udphdr *udp;
	switch (protocol) {
		case IPPROTO_TCP: hdrsize += sizeof(struct tcphdr); break;
		case IPPROTO_UDP: hdrsize += sizeof(struct udphdr); break;
		default: break; // Assume must ip header.
	}
	struct {	// dummy ip header, including just part of the IP header, added to tcp checksum.
		uint32_t saddr, daddr;
		uint8_t zeros;
		uint8_t protocol;
		uint16_t tcp_len; // This is in network order!
	} tcpdummyhdr;

	packet_t *result = malloc(sizeof(packet_t) + hdrsize + paylen + 3);
	struct iphdr *packet_header = (struct iphdr*)result->storage;
	result->ip = packet_header;
	result->tcp = (struct tcphdr*)((char*)result->storage + sizeof(struct iphdr));
	result->udp = (struct udphdr*)((char*)result->storage + sizeof(struct iphdr));
	result->payload = result->storage + hdrsize;
	result->tot_len = hdrsize + paylen;
	//while (result->tot_len & 0x3) { result->storage[result->tot_len++] = 0; }
	memset(packet_header,0,hdrsize);
	if (payload) memcpy(result->payload,payload,paylen);

	packet_header->ihl = 5;
	packet_header->version = 4;
	// tos, id, frag_off == 0
	packet_header->ttl = 64;
	packet_header->id = ipid;
	packet_header->protocol = protocol;
	packet_header->saddr = ipsrc;
	packet_header->daddr = ipdst;
	packet_header->tot_len = htons(result->tot_len);
	packet_header->check = ip_checksum(packet_header,sizeof(*packet_header),NULL);
	// Double check:
	/*
	if (0 != ip_checksum(packet_header,sizeof(*packet_header),NULL)) {
		printf("WARNING: computed checksum invalid: check=%d computed=%d\n",
			packet_header->check,ip_checksum(packet_header,sizeof(*packet_header),NULL));
	}
	*/

	switch (protocol) {
		case IPPROTO_UDP:
			assert(0);	// unimplemented.
			udp = result->udp;
			udp->len = htons(paylen + sizeof(*udp));
			//udp->source = srcport;
			//udp->dest = dstport;
			// Checksum includes udp header plus IP pseudo-header!
			// But checksum is optional, so just set to 0.
			udp->check = 0;
			break;
		case IPPROTO_TCP:
			tcp = result->tcp;
			memcpy(tcp,tcpin,sizeof(struct tcphdr));
			tcp->doff = 5;		// tcp header size in words.
			tcp->window = htons(0x1111);
			// create the dummy header.
			tcpdummyhdr.saddr = packet_header->saddr;
			tcpdummyhdr.daddr = packet_header->daddr;
			tcpdummyhdr.zeros = 0;
			tcpdummyhdr.protocol = protocol;
			// This length excludes the length of the ip header.
			unsigned tcplen = result->tot_len - sizeof(struct iphdr);
			tcpdummyhdr.tcp_len = htons(tcplen);
			tcp->check = ip_checksum(tcp,tcplen,&tcpdummyhdr);
			break;
			// successful options were: <mss 1460,sackOK,timestamp 7124617 0,nop,wscale 7>
	}


	return result;
}

static void pinghttpPrintReply(char *result,int len,char*hostname,int recvcnt)
{
	if (len < 0) { printf("unexpected result len: %d\n",len); return; }
	int plen = len;	// how much to print
	if (0 == recvcnt) {
		printf("pinghttp: %s replies %d bytes:\n--->",hostname,len);
		if (!options.printall) {
			// Prune the response a little.
			// Just print the first line.
			char *c1 = memchr(result,'\n',len);
			if (c1 && c1-result < len) plen = c1-result;
			printf("%.*s\n",plen,result);
		}
	}
	if (options.printall) {
		printf("%.*s\n",plen,result);
	}
}

int pinghttpsend(packet_t *packet,mg_con_t *mgp,int sfd,struct sockaddr *toaddr)
{
	int rc;
	usleep(100000);
	if (options.v) ip_hdr_dump(packet->storage,"sending:");
	//struct iphdr *pip = (struct iphdr*)packet->s;
	//printf("packet->len = %d tot_len=%d\n",packet->tot_len,ntohs(pip->tot_len));
	if (options.use_tunnel) {
		rc = write(tun_fd,packet->storage,packet->tot_len);
	} else if (mgp) {
#ifndef STANDALONE
		rc = miniggsn_snd_npdu_by_mgc(mgp,packet->storage,packet->tot_len);
#endif
	} else {
		// Note: when I left the non-raw socket connected above and fell through
		// to this code, it looked like the raw socket may not receive on this port until
		// the connected socket is closed, not sure.
		rc = sendto(sfd,packet->storage,packet->tot_len,0,toaddr,sizeof(struct sockaddr));
	}
	if (rc < 0) {
		printf("sendto failed: %s\n",strerror(errno));
		return 2;
	}
	return 0;
}



// Ping using http port 80, see if anything is there.
// It is not a real 'ping', which uses ICMP protocol.
// This implements a subset of the TCP handshaking protocol.
// We assume there is only data packet (the httpget message) and we assume
// all packets are delivered, ie, no retries.
// It runs the protocol all the way to the final closure (FIN) acknowledgements,
// to make sure the connection is completely closed.  Otherwise you can only
// run one of these tests to any host until that host times our connection out.
int pinghttp(char *whoto,char *whofrom,mg_con_t *mgp)
{
	struct sockaddr toaddr;
	struct sockaddr_in *to = (struct sockaddr_in*) &toaddr;
	uint16_t dstport = htons(80);	// Connect to HTTP port 80.
	char hostname[300];
	int one = 1;
	//char nbuf[100];
	const int rbufsize = 1500;
	char rbuf[rbufsize+1];
	int sfd = -1;		// socket fd
	int raw_mode = mgp || options.use_tunnel || whofrom;
	int rc;

	if (ip_findaddr(whoto,&toaddr,hostname)) {
		printf("pinghttp: unknown to host %s\n", whoto);
		return 2;
	}
	to->sin_port = dstport;		
	if (toaddr.sa_family == AF_INET) {
		printf("pinghost %s AF_INET addr %s\n",hostname,inet_ntoa(to->sin_addr));
	}

	char httpget[300];
	//sprintf(httpget, "GET /index.html HTTP/1.1\r\nHost: %s\r\n\r\n",hostname);
	sprintf(httpget, "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n");

	//sprintf(httpget, "GET /index.html HTTP/1.1\r\nHost: %s\r\n\r\n\r\n",inet_ntoa(to->sin_addr));
	//sprintf(httpget, "GET /index.html\r\n\r\n");
	//sprintf(httpget, "GET /index.html HTTP/1.0\r\n\r\n\r\n");

	if (! raw_mode) {
		// Simple pinghttp version uses a regular socket and connect.
		// The kernel handles IP headers and TCP handshakes.
		// Send the httpget message, print any reply.
		sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sfd < 0) { printf("pinghttp: socket() failed: %s\n",strerror(errno)); return 2; }

		if (connect(sfd,&toaddr,sizeof(struct sockaddr))) {
			close(sfd);
			printf("pinghttp: connect() to %s failed: %s\n",hostname,strerror(errno));
			return 2;
		}

		if (0) {
			// Out of interest, get the srcport assigned by connect above.
			struct sockaddr sockname; socklen_t socknamelen = sizeof(struct sockaddr);
			getsockname(sfd,&sockname,&socknamelen);
			// Add one to the port, just hope it is free.
			//srcport = htons(ntohs(((struct sockaddr_in*)&sockname)->sin_port) + 10);
		}

		if (options.v) printf("pinghttp to %s send %s\n",hostname,httpget); 
		rc = send(sfd,httpget,strlen(httpget),0);
		if (rc < 0) { printf("pinghttp: send failed: %s\n",strerror(errno)); return 2; }
		rc = recv(sfd,rbuf,rbufsize,0);
		if (rc <= 0 || rc > rbufsize) {
			printf("pinghttp: recv failed rc=%d error:%s\n",rc,strerror(errno)); return 2;
		}
		pinghttpPrintReply(rbuf,rc,hostname,0);
		return 0;
	}

	// We are going to use raw mode.
	// We will add the IP headers to the packet, and handle TCP handshake ourselves.

	uint32_t dstip = to->sin_addr.s_addr;
	uint32_t srcip = 0;
	packet_t reply;
	uint16_t srcport = htons(4000 + (time(NULL) & 0xfff)); //made up

	if (whofrom) {
		//srcip = inet_addr(whofrom);	// only allows IP numbers.
		//if (srcip == INADDR_NONE) {
		//	printf("pinghttp: Cannot find specified from address: %s\n",whofrom);
		//	return 2;
		//}
		struct sockaddr fromaddr;
		char fromhostname[300];
		if (ip_findaddr(whofrom,&fromaddr,fromhostname)) {
			printf("pinghttp: unknown from host %s\n", whofrom);
			return 2;
		}
		srcip = ((struct sockaddr_in*)&fromaddr)->sin_addr.s_addr;	// already swizzled.
	} else if (mgp) {
		srcip = mgp->mg_ip;
	} else {
		assert(0);
	}



	if (whofrom && !(mgp || options.use_tunnel)) {
		// open raw socket
		// If it going through nat, we should be able to pick nearly any port,
		// and the nat will change it if it conflicts.
		// NOTE: When you use RAW sockets, the kernel tcp driver will become confused
		// by this tcp traffic and insert packets into the stream to goof it up.
		// To prevent that use:
		// iptables -A OUTPUT -p tcp --tcp-flags RST RST -j DROP
		//
		sfd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
		if (sfd < 0) { printf("cannot open raw socket\n"); return 2;}
		setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
		setsockopt(sfd,IPPROTO_IP,IP_HDRINCL,&one,sizeof(one));

		// Binding is unnecessary to simply read/write the socket.
		if (options.bind) {
			struct sockaddr_in bindto;
			memset(&bindto,0,sizeof(bindto));
			bindto.sin_family = AF_INET;
			bindto.sin_port = 0;
			bindto.sin_addr.s_addr = srcip;
			if (bind(sfd,(struct sockaddr*)&bindto,sizeof(bindto))) {
				printf("bindto %s failed\n",ip_sockaddr2a(&bindto,NULL));
			}

			// Try using bindtodevice on the receive rfd.
#if 0
			char *dummy = "foo";
			rfd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
			if (rfd < 0) { printf("cannot open raw socket\n"); return 2;}
			setsockopt(rfd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));
			setsockopt(rfd,IPPROTO_IP,IP_HDRINCL,&one,sizeof(one));
			if (setsockopt(rfd,SOL_SOCKET,SO_BINDTODEVICE,dummy,strlen(dummy)+1)) {
				printf("BINDTODEVICE failed\n");
			}
#endif
		}
	}
	int rfd = sfd;

	// seqloc is our sequence number.  seqrem is the servers sequence number.
	//unsigned seqloc = 2580167164u;		// got from a successful TCP session.
	//unsigned seqloc = 1580160000u + (rand() & 0xffff);
	unsigned startloc = time(NULL)*9;// + (rand() & 0xffff);
	unsigned seqloc = startloc;
	//unsigned seqloc_syn = 0;		// seqloc of SYN sent.
	unsigned seqloc_fin = 0;		// seqloc of FIN sent.
	unsigned seqrem = 0;
	unsigned seg_ack = 0;
	int sendid = 1;
	unsigned ipid = 0xfff & (time(NULL) * 9);
	struct tcphdr tcpb;

	// States from RFC 793 page 22
	enum {
		tcp_start,
		tcp_syn_sent,
		tcp_syn_received,
		tcp_established,
		tcp_fin_wait1,
		tcp_fin_wait2,
		tcp_close_wait,
		tcp_closing,
		tcp_last_ack,
		tcp_time_wait,
		tcp_closed,
		tcp_data_finished,	// State added to initiate close connect on our side.
	} state = tcp_start;
	char *state_name[20];
	state_name[tcp_start] = "tcp_start";
	state_name[tcp_syn_sent] = "tcp_syn_sent";
	state_name[tcp_syn_received] = "tcp_syn_received";
	state_name[tcp_established] = "tcp_established";
	state_name[tcp_fin_wait1] = "tcp_fin_wait1";
	state_name[tcp_fin_wait2] = "tcp_fin_wait2";
	state_name[tcp_close_wait] = "tcp_close_wait";
	state_name[tcp_closing] = "tcp_closing";
	state_name[tcp_last_ack] = "tcp_last_ack";
	state_name[tcp_time_wait] = "tcp_time_wait";
	state_name[tcp_closed] = "tcp_closed";
	state_name[tcp_data_finished] = "tcp_data_finished";

	reply.tcp = 0;	// unnecessary, makes gcc happy.

	enum { SYN = 1, FIN = 2 };

	int tcpsend(int flags, char *payload) {
		memset(&tcpb,0,sizeof(tcpb));
		tcpb.source = srcport;
		tcpb.dest = dstport;
		tcpb.seq = htonl(seqloc);
		tcpb.ack_seq = htonl(seqrem);	// probably not right.

		int next_seqloc = seqloc;
		packet_t *packet;
		if (flags & SYN) {
			tcpb.syn = 1;
			next_seqloc++;
			//seqloc_syn = next_seqloc;
		} else {
			tcpb.ack = 1;
		}
		if (flags & FIN) {
			tcpb.fin = 1;
			next_seqloc++;
			seqloc_fin = next_seqloc;
		}
		if (payload) {
			tcpb.psh = 1;
			next_seqloc += strlen(payload);
			// the packet id for you.  How kind of it.
			if (options.use_tunnel && !sendid) {
				sendid = 1;
				ipid = (time(NULL) & 0xfff)  + (rand() & 0xfff);
			}
		}
		packet = ip_add_hdr2(IPPROTO_TCP, srcip, dstip, payload,&tcpb,htons(ipid));
		if (pinghttpsend(packet,mgp,sfd,&toaddr)) return 1;
		if (sendid) { ipid++; }
		seqloc = next_seqloc;
		return 0;
	} // tcpsend


	int tries = 0;
	int recvcnt = 0;
	while (tries++ < 20) {
		int prevstate = state;

		switch (state) {
		case tcp_start:
			state = tcp_syn_sent;
			tcpsend(SYN,NULL);
			break;
		case tcp_syn_sent:
			if (reply.tcp->syn) {
				// OK, now gotta send back an ack without data first.
				// We dont inc seqrem here - that is now done after reply below.
				// seqrem++;		// First byte is number 1, not number 0.
				if (reply.tcp->ack) {
					// If you dont send this first naked ACK, the host responds with another SYN.
					tcpsend(0,NULL);
					// If you dont send the data now, host times out waiting for something to do.
					state = tcp_established;
					goto send_data;
				} else {
					// We have to wait for an ACK.
					state = tcp_syn_received;
					tcpsend(0,NULL);
				}
			} else {
				// We were waiting for a SYN but did not get it.  Oops.
				printf("in tcp state syn_sent expecting SYN but did not get it?\n");
				// This happens if a previous connection is still open.
				// go listen some more
			}
			break;
		case tcp_syn_received:
			if (reply.tcp->ack) {
				//seqrem++;		// First byte is number 1, not number 0.
				state = tcp_established;
				tcpsend(0,httpget);
			} else {
				printf("int tcp state syn_received expecting ACK but did not get it?\n");
				// go listen some more
			}
			break;
		case tcp_established:
			if (reply.tcp->fin) {
				state = tcp_close_wait;
				tcpsend(FIN,NULL);
				if (seg_ack <= startloc+1) {
					printf("remote connection closed before we could send data\n");
				}
			} else if (seg_ack <= startloc+1) {
				send_data:
				tcpsend(0,httpget); // Now send or resend the data;
			} else {
				// We only have one packet to send, so if we got it then we are done.
				// Advance seqloc
				//seqloc = seg_ack;  moved to after reply.
				state = tcp_fin_wait1;
				tcpsend(FIN,NULL);
			}
			break;
		case tcp_data_finished:
			state = tcp_fin_wait1;
			tcpsend(FIN,NULL);
			break;
		case tcp_fin_wait1:
			// We sent a FIN, now we have to wait for a FIN or ACK back.
			if (reply.tcp->fin) {
				state = tcp_closing;
				tcpsend(0,NULL);
			} else {
				assert(seqloc_fin);
				if (seg_ack > seqloc_fin) {
					state = tcp_fin_wait2;
				}
			}
			//printf("fin_wait1 end=%d next=%s\n",seg_ack>=seqloc_fin, state_name[state]);
			break;
		case tcp_fin_wait2:
			// We are waiting for a FIN.
			if (reply.tcp->fin) {
				state = tcp_time_wait;
			} else {
				printf("tcp in fin_wait2 expected FIN but did not get it\n");
			}
			tcpsend(0,NULL);
			break;
		case tcp_close_wait:
			state = tcp_last_ack;
			tcpsend(FIN,NULL);
			break;
		case tcp_last_ack:
			if (reply.tcp->ack) {
				state = tcp_closed;
			}
			break;
		case tcp_closing:
			// Waiting for ACK of FIN, but who cares?  We are done.
			if (reply.tcp->ack) {
				state = tcp_time_wait;
			}
			state = tcp_time_wait;
			tcpsend(0,NULL);
			break;
		default:
			assert(0);	// unhandled state
		}

		if (state == tcp_time_wait || state == tcp_closed || state == tcp_closing) break;

		wait_again:
		// Wait for a reply...
		if (options.use_tunnel) {
			// The read only returns what is available, not full rc.
			rc = read(tun_fd,rbuf,rbufsize);
		} else if (mgp) {
#ifndef STANDALONE
			char *msg = miniggsn_rcv_npdu(&mgp->mg_tcpfd, &error, &plen);
#endif
			rc = -1;
			//if (msg == NULL) { rc = -1; }
		} else {
			struct sockaddr recvaddr; socklen_t recvaddrlen = sizeof(struct sockaddr);
			// Note: For a connected socket, the recvaddr is given garbage.
			rc = recvfrom(rfd,rbuf,rbufsize,0,&recvaddr,&recvaddrlen);
			printf("Reply %d bytes from socket: %s\n",rc,ip_sockaddr2a(&recvaddr,NULL));
		}

		if (rc < 0) { printf("pinghttp: recv error: %s\n",strerror(errno)); return 2; }
		if (rc == 0) {
			printf("received 0 length packet - means shutdown\n");
			break;
		}

		// Reply received.
		// Both headers are variable size, ipdr size in ->ihl, tcp in ->doff.
		reply.ip = (struct iphdr*)rbuf;
		reply.tcp = (struct tcphdr*) (rbuf + 4 * reply.ip->ihl);
		int hdrsize = 4*reply.ip->ihl + 4*reply.tcp->doff;
		if (rc < hdrsize) {
			printf("bytes received %d less than header size %d?\n",rc,hdrsize);
			continue;
		}
		if (reply.tcp->dest != srcport) {
			printf("Message to port %d (not port %d) ignored\n",ntohs(reply.tcp->dest),ntohs(srcport));
			goto wait_again;
		}

		// Process the reply:

		reply.payload = rbuf + hdrsize;
		int payloadlen = rc - hdrsize;
		int seg_len = payloadlen + (reply.tcp->syn ? 1 : 0) + (reply.tcp->fin ? 1 : 0);
		seqrem = ntohl(reply.tcp->seq) + seg_len;
		if (reply.tcp->ack) {
			seg_ack = ntohl(reply.tcp->ack_seq);
			seqloc = seg_ack;
		}
		if (options.v) printf("state=%s received %d bytes %s%s next state=%s\n",
			state_name[prevstate],rc-hdrsize,
			reply.tcp->syn?"SYN":"",  reply.tcp->fin?" FIN":"",
			state_name[state]);
		if (options.v) ip_hdr_dump(rbuf,"received:");

		if (payloadlen) {
			pinghttpPrintReply(reply.payload,payloadlen,hostname,recvcnt++);
		}
	} // whileloop
	if (sfd >= 0) close(sfd);
	return 0;
}

#if STANDALONE
struct options_s options = {0};

int main(int argc, char *argv[])
{
	int xflag = 0;	// Extended test.
	//options.from = "192.168.99.2";
	uint32_t *addrs = ip_findmyaddr();
	char mebuf[30];
	options.from = ip_ntoa(addrs[0],mebuf);	// Hope this is the right one.

#if STANDALONE
#else
	ip_init();
#endif
	//miniggsn_init();
	next_option:
	argc--; argv++;
	while (argc && argv[0][0] == '-') {
		if (0==strcmp(*argv,"-v")) { options.v=1; goto next_option; }
		if (0==strcmp(*argv,"-x")) { xflag=1; goto next_option; }
		if (0==strcmp(*argv,"-r")) { options.repeat=1; goto next_option; }
		if (0==strcmp(*argv,"-b")) { options.bind=1; goto next_option; }
		if (0==strcmp(*argv,"-a")) { options.printall=1; goto next_option; }
		if (0==strcmp(*argv,"-t")) { options.use_tunnel=1; goto next_option; }
		if (0==strcmp(*argv,"-T")) { options.use_tunnel=1; argc--;argv++;
			if (argc <= 0) { printf("expected arg to -t"); return 2; }
			mg_base_ip_route = *argv;
			goto next_option;
		}
		if (0==strcmp(*argv,"-f")) {
			argc--; argv++;
			if (argc <= 0) { printf("expected arg to -f"); return 2; }
			options.from = *argv;
			goto next_option;
		}
		printf("Unrecognized option: %s\n",*argv); exit(2);
	}
	if (argc <= 0) {
		printf("syntax: pinghttp [-v] [-a] [-t] [-x] [-T tun_addr] [-f from_ip] hostname\n");
		printf("	-v: verbose dump of http traffic in and out\n");
		printf("	-a: print entire http response\n");
		printf("	-t: use a tunnel (instead of a raw socket)\n");
		printf("	-x: emulate miniggsn; only one of -t or -x may be specified\n");
		printf("	-T <addr>: set the tunnel address, default -T %s\n",mg_base_ip_route);
		printf("	-f <addr>: set the from address, default -f %s\n",mg_base_ip_str);
		printf("		Note: if specified the -f address should be in address range specified by -T,\n");
		printf("		for example: 192.168.99.2\n");
		printf("	hostname: call the http server at this host.  Dont use google.com\n");
		exit(2);
	}

	if (geteuid() != 0) {
		printf("Warning: you should be root to run this!\n"); fflush(stdout);
	}

	if (options.use_tunnel) {
		if (!options.from) { printf("need -f addr\n"); exit(2); }
		tun_fd = ip_tun_open(tun_if_name,mg_base_ip_route);
		if (tun_fd < 0) {
			printf("Could not open %s\n",tun_if_name);
			exit(2);
		}
	}

	int stat = 0;
	if (xflag) {
		// Emulate some MS allocating some IP addresses.
		mg_con_t cons[4];	// Up to four simulated outgoing phone connections.
		int32_t base_ip = inet_addr(mg_base_ip_str);
		if (base_ip == INADDR_NONE) {
			printf("miniggsn: Cannot grok specified base ip address: %s\n",mg_base_ip_str);
			exit(2);
		}
		printf("base_ip=0x%x=%s\n",base_ip,ip_ntoa(base_ip,NULL));
		base_ip = ntohl(base_ip);

		int i;
		for (i=0; i < 4; i++) {
			memset(&cons[i],0,sizeof(mg_con_t));
			cons[i].mg_ip = htonl(base_ip + 1 + i);
		}
		// Try sending a raw ping packet.
		stat = pinghttp(argv[0],NULL,&cons[0]);
	} else {
		stat = pinghttp(argv[0],options.from,0);
	}
	if (stat == 0) { printf("http ping %s success\n",argv[0]); }
	return stat;
}
#endif
