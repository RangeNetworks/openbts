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

#include <netdb.h>			// pat added for gethostbyname
#include <netinet/ip.h>		// pat added for IPv4 iphdr
#include <netinet/tcp.h>		// pat added for tcphdr
#include <netinet/udp.h>		// pat added for udphdr

#include <linux/if.h>			// pat added.
#include <linux/if_tun.h>			// pat added.
#include <sys/ioctl.h>			// pat added.	This defines NCC, which is bad, because used in GSMConfig.h
#undef NCC	// Just in case you want to include GSM files later.
#include <assert.h>				// pat added
#include <stdarg.h>				// pat added
#include <sys/time.h>				// pat added.
#include <time.h>				// pat added.
#include <sys/types.h>
#include <wait.h>
#include <ctype.h>
#include "miniggsn.h"
#include <Globals.h>		// for gConfig
#include <Utils.h>

namespace SGSN {
#define EXPORT
// Returns fractional seconds.
double pat_timef()
{   
    struct timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_usec / 1000000.0 + tv.tv_sec;
}

double pat_elapsedf()
{
	static double start = 0;
	double now = pat_timef();
	if (start == 0) start = now;
	return now - start;
}


// ip address is in network order;  return as dotted string.
EXPORT char *ip_ntoa(int32_t ip, char *buf)
{
	static char sbuf[30];
	if (buf == NULL) { buf = sbuf;}
	ip = ntohl(ip);
	sprintf(buf,"%d.%d.%d.%d",(ip>>24)&0xff,(ip>>16)&0xff,(ip>>8)&0xff,ip&0xff);
	return buf;
}

// Given an address like "192.168.1.0/24" return the base ip and the mask.
EXPORT bool ip_addr_crack(const char *input,uint32_t *paddr, uint32_t *pmask)
{
	char ip_buf[42];
	if (strlen(input) > 40) return false;
	strcpy(ip_buf,input);
	// Look for the '/';
	char *sl = strchr(ip_buf,'/');
	if (sl) *sl = 0;
	*paddr = inet_addr(ip_buf);
	if (sl == 0) {
		// Indicates no mask specified:
		*pmask = 0;
	} else {
		int maskbits = atoi(sl+1);
		if (maskbits < 0 || maskbits >32) {
			*pmask = 0;		// be safe.
			return false;	// but return invalid.
		}
		*pmask = htonl(~ ( (1u<<(32-maskbits)) - 1 ));
	}
	return true;
}

EXPORT char *ip_sockaddr2a(void * /*struct sockaddr * */ sap,char *buf)
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

// Add an address to the interface.  ifname is like "eth0" or "tun1"
// You have to be root to do this.
// Note that if you want to bind to a non-local address must set the ip_nonlocal_bind
// kernel option set in /proc.  Update: that did not work, so use ip_addr_add()
// on the ethernet card if you want to send directly to the machine.
EXPORT int ip_add_addr(char *ifname, int32_t ipaddr, int maskbits)
{
	char fulladdr[100];
	sprintf(fulladdr,"%s/%d",ip_ntoa(ipaddr,NULL),maskbits);
	if (fork() == 0) {
		execl("/sbin/ip","ip","addr","add",fulladdr,"dev",ifname,NULL);
		exit(0);	// Just in case.
	}
	return 0;
}

EXPORT const char *ip_proto_name(int ipproto)
{
	static char buf[10];
    switch(ipproto) {
		case IPPROTO_TCP: return "tcp";
		case IPPROTO_UDP: return "udp";
		case IPPROTO_ICMP: return "icmp";
		default: sprintf(buf,"%d",ipproto); return buf;
	}
}

// IP standard checksum, see wikipedia "IPv4 Header"
// len is in bytes, will normally be 20 == sizeof(struct iphdr).
EXPORT unsigned int ip_checksum(void *ptr, unsigned len, void *dummyhdr)
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

#if 0
// from ifconfig.c - skfd is undefined
static int set_ip_using(const char *name, int c, unsigned long ip)
{
    struct ifreq ifr;
    struct sockaddr_in sin;

    /*safe_*/strncpy(ifr.ifr_name, name, IFNAMSIZ);
    memset(&sin, 0, sizeof(struct sockaddr));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = ip;
    memcpy(&ifr.ifr_addr, &sin, sizeof(struct sockaddr));
    if (ioctl(skfd, c, &ifr) < 0)
    return -1;
    return 0;
}
#endif

EXPORT void ip_hdr_dump(unsigned char *packet, const char *msg)
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

// Run the command.
// This is not ip specific, but used to call linux "ip" and "route" commands.
// If commands starts with '|', capture stdout and return the file descriptor to read it.
EXPORT int runcmd(const char *path, ...)
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
		int result = 0;
		for (int quartersecs = 0; quartersecs < 5*4; quartersecs++) {
			if (waitpid(pid,&status,WNOHANG) == pid) {
				// WARNING: This assumes the amount of info returned by the command
				// is small enough to fit in the pipe.
				return dopipe ? pipefd[0] : status;	// Return read end of pipe, if piped.
			}
			usleep(250*1000);
		}
		MGERROR("sub-process did not complete in 5 seconds: %s",path);
		return -1;
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
	_exit(2);	// Just in case.
	return 0;	// This is never used.
}


// The addrstr is the tunnel address and must include the mask, eg: "192.168.2.0/24"
EXPORT int ip_tun_open(const char *tname, const char *addrstr) // int32_t ipaddr, int maskbits)
{
	struct ifreq ifr;
	int fd;
	const char *clonedev = "/dev/net/tun";
	if ((fd = open(clonedev,O_RDWR)) < 0) {
		// Hmmph.  Try to create the tunnel device.
		runcmd("/sbin/modprobe","modprobe","tun",NULL);
		runcmd("/sbin/modprobe","modprobe","ipip",NULL);
		sleep(2);
	}
	if ((fd = open(clonedev,O_RDWR)) < 0) {
		MGERROR("error: Could not open: %s\n",clonedev);
		return -1;
	}

	// This attaches to our existing mstun interface, if any, because
	// of the magic TUNSETPERSIST flag.
	memset(&ifr,0,sizeof(ifr));
	strcpy(ifr.ifr_name,tname);
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;	// Disable packet info.
	if (ioctl(fd,TUNSETIFF,&ifr) < 0) {
		MGERROR("could not create tunnel %s: ioctl error: %s\n",tname,strerror(errno));
		return -1;
	}
	if (ioctl(fd,TUNSETPERSIST,1) < 0) {
		MGERROR("could not setpersist tunnel %s: ioctl error: %s\n",tname,strerror(errno));
	}

	// This (and only this) magic works:
	// We invoke with:
	// ./miniggsn -v -t 192.168.1.75/32 -f 192.168.1.75 router
	// or: ./miniggsn -v -t 192.168.2.0/24 -f 192.168.2.1 router
	runcmd("/sbin/ip","ip","link","set",tname,"up",NULL);
	// TODO: Instead of individual routes, we can also do:
	// ip route add to 192.168.3.0/24 dev mstun  # Note you must use .0, not .1
	//runcmd("/sbin/ip","ip","route","add","to",options.from,"dev",tname,NULL);
	runcmd("/sbin/ip","ip","route","add","to",addrstr,"dev",tname,NULL);
	// Establishing a "forwarding route" causes linux proxy-arp to automagically add an ARP
	// for mstun, even though it has the "NOARP" option set.  What a mess.

	// Now we can set the interface addr using ip command.
	//ip_add_addr(tname,ipaddr,maskbits);
	// The addrstr must include the correct maskbits, like /24, or it just doesnt work.
	// If you call any of these, the output is no longer routed unless
	// you called TUNSETPERSIST first.
	//runcmd("/sbin/ifconfig","ifconfig",tname,"arp",NULL);
	//runcmd("/sbin/ip","ip","addr","add",addrstr,"dev",tname,NULL);
	//runcmd("/sbin/iptables","iptables","-A","FORWARD","-i",tname,"-j","ACCEPT", NULL);


	// todo #include <linux/sockios.h>
	// SIOCADDRT struct ifreq*	/* add routing table entry	*/
	// SIOCGIFADDR get interface address.
	// SIOCADDRT add interface address.
	// SIOCSIFADDR, SIOCSIFNETMASK 
	/*
	if (set_ip_using(buf, SIOCSIFADDR, ip) == -1) {
		MGERROR("ioctl SIOCSIFADDR failed\n"); return 2;
	}
	if (set_ip_using(buf, SIOCSIFNETMASK , mask) == -1) {
		MGERROR("ioctl SIOCSIFNETMASK failed\n"); return 2;
	}
	*/
	// We wont set a broadcast address using SIOCSIFBRDADDR

	return fd;
}

static int setprocoption(const char *procfn)
{
	int fd;
	const char *str = "1\n";
	fd = open(procfn,1);
	if (fd < 0) { MGERROR("Can not open file %s error: %s\n",procfn,strerror(errno)); return 2;}
	int __attribute__((unused)) foobar=write(fd,str,2);	// the foobar shuts up g++
	close(fd);
	return 0;
}

void ip_init()
{
	// Enable port forwarding and non-local bind
	if (setprocoption("/proc/sys/net/ipv4/ip_forward")) /*exit(2)*/ ;
	// This did not work:
	if (setprocoption("/proc/sys/net/ipv4/ip_nonlocal_bind")) /*exit(2)*/ ;
}

static int ip_getDnsDefault(uint32_t *dns)
{
	int nfnd = 0;
	FILE *pf = fopen("/etc/resolv.conf","r");
	if (pf == NULL) {
		MGERROR("GGSN: DNS servers: error: could not open /etc/resolv.conf\n");
		return 0;
	}
	char buf[300];
	while (fgets(buf,299,pf)) {
		buf[299] = 0;
		char *cp = buf;
		while (*cp && isspace(*cp)) cp++;
		if (0 == strncasecmp(cp,"nameserver",strlen("nameserver"))) {
			cp += strlen("nameserver");
			while (*cp && isspace(*cp)) cp++;
			uint32_t addr;
			if (0 == inet_aton(cp,(struct in_addr*) &addr)) {
				MGERROR("GGSN: Error: Invalid IP address in /etc/resolv.conf at: %s\n",buf);
				continue;
			}
			dns[nfnd++] = addr;
			if (nfnd == 2) break;
		}
	}
	fclose(pf);
	return nfnd;
}

// If a DNS option was specified, put the addresses in dns[2] and return number found.
// The DNS may be a list of space separated servers.
static int ip_getDnsOption(uint32_t *dns)
{
	std::string sdns = gConfig.getStr("GGSN.DNS");
	int len = sdns.length();
	if (len <= 0) {return 0;}

	char *copy = strcpy((char*)alloca(len+1),sdns.c_str());	// make a copy of the option string.
	// Insist on dotted notation to catch stupid errors like setting it to '1' by mistake
	if (!strchr(copy,'.')) {
		MGWARN("Invalid GGSN.DNS option ignored: '%s'",copy);
		return 0;
	}
	char *argv[3];
	int argc = cstrSplit(copy,argv,3);						// split into argv
	if (argc > 2) {
		MGWARN("GGSN: invalid GGSN.DNS option, more than 2 servers specified: '%s'\n",sdns.c_str());
		// But go ahead and use the first two.
		argc = 2;
	}

	for (int i = 0; i < argc; i++) {
		if (0 == inet_aton(argv[i],(struct in_addr*) &dns[i])) {	// is it an invalid IP address?
			// failed.
			MGERROR("GGSN: unrecognized GGSN.DNS option: %s\n",sdns.c_str());
			return i;
		}
	}
	return argc;
}

// Find the dns addresses.  Return the number of dns addresses found.
// dns must point to uint32_t dns[2];
EXPORT int ip_finddns(uint32_t *dns)
{
	dns[0] = dns[1] = 0;
	int nfnd = ip_getDnsOption(dns);
	if (!nfnd) {
		nfnd = ip_getDnsDefault(dns);
	}

	// Print out the dns servers whenever they change.
	// If dns were not found, they will print as 0s, which is a good enough message.
	static uint32_t prevdns[2] = {0,0};
	if (dns[0] == 0) {
		// This is a disaster.
		MGWARN("GGSN: No DNS servers found; GPRS service will not work");
	} else if (dns[0] != prevdns[0] || dns[1] != prevdns[1]) {
		char bf[2][30];
		MGINFO("GGSN: DNS servers: %s %s",ip_ntoa(dns[0],bf[0]),ip_ntoa(dns[1],bf[1]));
		prevdns[0] = dns[0];
		prevdns[1] = dns[1];
	}
	return nfnd;
}

// Return an array of ip addresses, terminated by a -1 address.
EXPORT uint32_t *ip_findmyaddr()
{
	const int maxaddrs = 5;
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

}; // namespace
