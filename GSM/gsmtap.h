#ifndef _GSMTAP_H
#define _GSMTAP_H

/* gsmtap header, pseudo-header in front of the actua GSM payload*/

#include <sys/types.h>
#ifdef __linux__
#  include <arpa/inet.h>
#endif

#define GSMTAP_VERSION		0x01

#define GSMTAP_TYPE_UM		0x01	/* A Layer 2 MAC block (23 bytes) */
#define GSMTAP_TYPE_ABIS	0x02
#define GSMTAP_TYPE_UM_BURST	0x03	/* raw burst bits */

#define GSMTAP_BURST_UNKNOWN		0x00
#define GSMTAP_BURST_FCCH		0x01
#define GSMTAP_BURST_PARTIAL_SCH	0x02
#define GSMTAP_BURST_SCH		0x03
#define GSMTAP_BURST_CTS_SCH		0x04
#define GSMTAP_BURST_COMPACT_SCH	0x05
#define GSMTAP_BURST_NORMAL		0x06
#define GSMTAP_BURST_DUMMY		0x07
#define GSMTAP_BURST_ACCESS		0x08
#define GSMTAP_BURST_NONE		0x09

#define GSMTAP_UDP_PORT		4729	/* officially registered with IANA */

struct gsmtap_hdr {
	u_int8_t version;		/* version, set to 0x01 currently */
	u_int8_t hdr_len;		/* length in number of 32bit words */
	u_int8_t type;			/* see GSMTAP_TYPE_* */
	u_int8_t timeslot;		/* timeslot (0..7 on Um) */

	u_int16_t arfcn;		/* ARFCN (frequency).
					 * highest bit 1 == uplink */
	u_int8_t noise_db;		/* noise figure in dB */
	u_int8_t signal_db;		/* signal level in dB */

	u_int32_t frame_number;		/* GSM Frame Number (FN) */

	u_int8_t burst_type;		/* Type of burst, see above */
	u_int8_t antenna_nr;		/* Antenna Number */
	u_int16_t res;			/* reserved for future use (RFU) */

	gsmtap_hdr(unsigned ARFCN, unsigned TS, unsigned FN)
	{
		version = GSMTAP_VERSION;
		type = GSMTAP_TYPE_UM;
		burst_type = GSMTAP_BURST_NONE;
		antenna_nr = 0;
		noise_db = 0;
		signal_db = 0;

		timeslot = TS;
		arfcn = htons(ARFCN);
		frame_number = htonl(FN);
	}

} __attribute__((packed));


/* PCAP related definitions */
#define TCPDUMP_MAGIC   0xa1b2c3d4
#ifndef LINKTYPE_GSMTAP
#define LINKTYPE_GSMTAP	2342
#endif
struct pcap_timeval {
	int32_t tv_sec;
	int32_t tv_usec;
};
	
struct pcap_sf_pkthdr {
	struct pcap_timeval ts;		/* time stamp */
	u_int32_t caplen;		/* lenght of portion present */
	u_int32_t len;			/* length of this packet */
};


#endif /* _GSMTAP_H */
