/*
 * These are the public elements of the Linux kernel AX.25 code. A similar
 * file netrom.h exists for the NET/ROM protocol.
 */
 
#ifndef	AX25_KERNEL_H
#define	AX25_KERNEL_H
 
#define PF_AX25		AF_AX25
#define AX25_MTU	256
#define AX25_MAX_DIGIS	6	/* This is wrong, should be 8 */

#define AX25_WINDOW	1
#define AX25_T1		2
#define AX25_N2		3
#define AX25_T3		4
#define AX25_T2		5
#define	AX25_BACKOFF	6
#define	AX25_EXTSEQ	7
#define	AX25_HDRINCL	8
#define AX25_IDLE	9
#define AX25_PACLEN	10
#define AX25_MAXQUEUE	11

#define AX25_KILL	99

#define SIOCAX25GETUID		(SIOCPROTOPRIVATE+0)
#define SIOCAX25ADDUID		(SIOCPROTOPRIVATE+1)
#define SIOCAX25DELUID		(SIOCPROTOPRIVATE+2)
#define SIOCAX25NOUID		(SIOCPROTOPRIVATE+3)
#define SIOCAX25OPTRT		(SIOCPROTOPRIVATE+4)
#define SIOCAX25CTLCON		(SIOCPROTOPRIVATE+5)

#define AX25_SET_RT_IPMODE	2

#define AX25_NOUID_DEFAULT	0
#define AX25_NOUID_BLOCK	1

typedef struct {
	char ax25_call[7];	/* 6 call + SSID (shifted ascii!) */
} ax25_address;

struct sockaddr_ax25 {
	sa_family_t sax25_family;
	ax25_address sax25_call;
	int sax25_ndigis;
	/* Digipeater ax25_address sets follow */
};

#define sax25_uid	sax25_ndigis

struct full_sockaddr_ax25 {
	struct sockaddr_ax25 fsa_ax25;
	ax25_address fsa_digipeater[AX25_MAX_DIGIS];
};

struct ax25_routes_struct {
	ax25_address port_addr;
	ax25_address dest_addr;
	unsigned char digi_count;
	ax25_address digi_addr[AX25_MAX_DIGIS];
};

struct ax25_route_opt_struct {
	ax25_address port_addr;
	ax25_address dest_addr;
	int cmd;
	int arg;
};

struct ax25_ctl_struct {
	ax25_address port_addr;
	ax25_address source_addr;
	ax25_address dest_addr;
	unsigned int cmd;
	unsigned long arg;
};

#endif
