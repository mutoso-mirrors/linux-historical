/*
 *	NET3:	Implementation of the ICMP protocol layer. 
 *	
 *		Alan Cox, <alan@cymru.net>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Some of the function names and the icmp unreach table for this
 *	module were derived from [icmp.c 1.0.11 06/02/93] by
 *	Ross Biro, Fred N. van Kempen, Mark Evans, Alan Cox, Gerhard Koerting.
 *	Other than that this module is a complete rewrite.
 *
 *	Fixes:
 *		Mike Shaver	:	RFC1122 checks.
 *		Alan Cox	:	Multicast ping reply as self.
 *		Alan Cox	:	Fix atomicity lockup in ip_build_xmit 
 *					call.
 *		Alan Cox	:	Added 216,128 byte paths to the MTU 
 *					code.
 *		Martin Mares	:	RFC1812 checks.
 *		Martin Mares	:	Can be configured to follow redirects 
 *					if acting as a router _without_ a
 *					routing protocol (RFC 1812).
 *		Martin Mares	:	Echo requests may be configured to 
 *					be ignored (RFC 1812).
 *		Martin Mares	:	Limitation of ICMP error message 
 *					transmit rate (RFC 1812).
 *		Martin Mares	:	TOS and Precedence set correctly 
 *					(RFC 1812).
 *		Martin Mares	:	Now copying as much data from the 
 *					original packet as we can without
 *					exceeding 576 bytes (RFC 1812).
 *	Willy Konynenberg	:	Transparent proxying support.
 *		Keith Owens	:	RFC1191 correction for 4.2BSD based 
 *					path MTU bug.
 *		Thomas Quinot	:	ICMP Dest Unreach codes up to 15 are
 *					valid (RFC 1812).
 *
 *
 * RFC1122 (Host Requirements -- Comm. Layer) Status:
 * (boy, are there a lot of rules for ICMP)
 *  3.2.2 (Generic ICMP stuff)
 *   MUST discard messages of unknown type. (OK)
 *   MUST copy at least the first 8 bytes from the offending packet
 *     when sending ICMP errors. (OBSOLETE -- see RFC1812)
 *   MUST pass received ICMP errors up to protocol level. (OK)
 *   SHOULD send ICMP errors with TOS == 0. (OBSOLETE -- see RFC1812)
 *   MUST NOT send ICMP errors in reply to:
 *     ICMP errors (OK)
 *     Broadcast/multicast datagrams (OK)
 *     MAC broadcasts (OK)
 *     Non-initial fragments (OK)
 *     Datagram with a source address that isn't a single host. (OK)
 *  3.2.2.1 (Destination Unreachable)
 *   All the rules govern the IP layer, and are dealt with in ip.c, not here.
 *  3.2.2.2 (Redirect)
 *   Host SHOULD NOT send ICMP_REDIRECTs.  (OK)
 *   MUST update routing table in response to host or network redirects.
 *     (host OK, network OBSOLETE)
 *   SHOULD drop redirects if they're not from directly connected gateway
 *     (OK -- we drop it if it's not from our old gateway, which is close
 *      enough)
 * 3.2.2.3 (Source Quench)
 *   MUST pass incoming SOURCE_QUENCHs to transport layer (OK)
 *   Other requirements are dealt with at the transport layer.
 * 3.2.2.4 (Time Exceeded)
 *   MUST pass TIME_EXCEEDED to transport layer (OK)
 *   Other requirements dealt with at IP (generating TIME_EXCEEDED).
 * 3.2.2.5 (Parameter Problem)
 *   SHOULD generate these (OK)
 *   MUST pass received PARAMPROBLEM to transport layer (NOT YET)
 *   	[Solaris 2.X seems to assert EPROTO when this occurs] -- AC
 * 3.2.2.6 (Echo Request/Reply)
 *   MUST reply to ECHO_REQUEST, and give app to do ECHO stuff (OK, OK)
 *   MAY discard broadcast ECHO_REQUESTs. (We don't, but that's OK.)
 *   MUST reply using same source address as the request was sent to.
 *     We're OK for unicast ECHOs, and it doesn't say anything about
 *     how to handle broadcast ones, since it's optional.
 *   MUST copy data from REQUEST to REPLY (OK)
 *     unless it would require illegal fragmentation (OK)
 *   MUST pass REPLYs to transport/user layer (OK)
 *   MUST use any provided source route (reversed) for REPLY. (NOT YET)
 * 3.2.2.7 (Information Request/Reply)
 *   MUST NOT implement this. (I guess that means silently discard...?) (OK)
 * 3.2.2.8 (Timestamp Request/Reply)
 *   MAY implement (OK)
 *   SHOULD be in-kernel for "minimum variability" (OK)
 *   MAY discard broadcast REQUESTs.  (OK, but see source for inconsistency)
 *   MUST reply using same source address as the request was sent to. (OK)
 *   MUST reverse source route, as per ECHO (NOT YET)
 *   MUST pass REPLYs to transport/user layer (requires RAW, just like 
 *	ECHO) (OK)
 *   MUST update clock for timestamp at least 15 times/sec (OK)
 *   MUST be "correct within a few minutes" (OK)
 * 3.2.2.9 (Address Mask Request/Reply)
 *   MAY implement (OK)
 *   MUST send a broadcast REQUEST if using this system to set netmask
 *     (OK... we don't use it)
 *   MUST discard received REPLYs if not using this system (OK)
 *   MUST NOT send replies unless specifically made agent for this sort
 *     of thing. (OK)
 *
 *
 * RFC 1812 (IPv4 Router Requirements) Status (even longer):
 *  4.3.2.1 (Unknown Message Types)
 *   MUST pass messages of unknown type to ICMP user iface or silently discard
 *     them (OK)
 *  4.3.2.2 (ICMP Message TTL)
 *   MUST initialize TTL when originating an ICMP message (OK)
 *  4.3.2.3 (Original Message Header)
 *   SHOULD copy as much data from the offending packet as possible without
 *     the length of the ICMP datagram exceeding 576 bytes (OK)
 *   MUST leave original IP header of the offending packet, but we're not
 *     required to undo modifications made (OK)
 *  4.3.2.4 (Original Message Source Address)
 *   MUST use one of addresses for the interface the orig. packet arrived as
 *     source address (OK)
 *  4.3.2.5 (TOS and Precedence)
 *   SHOULD leave TOS set to the same value unless the packet would be 
 *     discarded for that reason (OK)
 *   MUST use TOS=0 if not possible to leave original value (OK)
 *   MUST leave IP Precedence for Source Quench messages (OK -- not sent 
 *	at all)
 *   SHOULD use IP Precedence = 6 (Internetwork Control) or 7 (Network Control)
 *     for all other error messages (OK, we use 6)
 *   MAY allow configuration of IP Precedence (OK -- not done)
 *   MUST leave IP Precedence and TOS for reply messages (OK)
 *  4.3.2.6 (Source Route)
 *   SHOULD use reverse source route UNLESS sending Parameter Problem on source
 *     routing and UNLESS the packet would be immediately discarded (NOT YET)
 *  4.3.2.7 (When Not to Send ICMP Errors)
 *   MUST NOT send ICMP errors in reply to:
 *     ICMP errors (OK)
 *     Packets failing IP header validation tests unless otherwise noted (OK)
 *     Broadcast/multicast datagrams (OK)
 *     MAC broadcasts (OK)
 *     Non-initial fragments (OK)
 *     Datagram with a source address that isn't a single host. (OK)
 *  4.3.2.8 (Rate Limiting)
 *   SHOULD be able to limit error message rate (OK)
 *   SHOULD allow setting of rate limits (OK, in the source)
 *  4.3.3.1 (Destination Unreachable)
 *   All the rules govern the IP layer, and are dealt with in ip.c, not here.
 *  4.3.3.2 (Redirect)
 *   MAY ignore ICMP Redirects if running a routing protocol or if forwarding
 *     is enabled on the interface (OK -- ignores)
 *  4.3.3.3 (Source Quench)
 *   SHOULD NOT originate SQ messages (OK)
 *   MUST be able to limit SQ rate if originates them (OK as we don't 
 *	send them)
 *   MAY ignore SQ messages it receives (OK -- we don't)
 *  4.3.3.4 (Time Exceeded)
 *   Requirements dealt with at IP (generating TIME_EXCEEDED).
 *  4.3.3.5 (Parameter Problem)
 *   MUST generate these for all errors not covered by other messages (OK)
 *   MUST include original value of the value pointed by (OK)
 *  4.3.3.6 (Echo Request)
 *   MUST implement echo server function (OK)
 *   MUST process at ER of at least max(576, MTU) (OK)
 *   MAY reject broadcast/multicast ER's (We don't, but that's OK)
 *   SHOULD have a config option for silently ignoring ER's (OK)
 *   MUST have a default value for the above switch = NO (OK)
 *   MUST have application layer interface for Echo Request/Reply (OK)
 *   MUST reply using same source address as the request was sent to.
 *     We're OK for unicast ECHOs, and it doesn't say anything about
 *     how to handle broadcast ones, since it's optional.
 *   MUST copy data from Request to Reply (OK)
 *   SHOULD update Record Route / Timestamp options (??)
 *   MUST use reversed Source Route for Reply if possible (NOT YET)
 *  4.3.3.7 (Information Request/Reply)
 *   SHOULD NOT originate or respond to these (OK)
 *  4.3.3.8 (Timestamp / Timestamp Reply)
 *   MAY implement (OK)
 *   MUST reply to every Timestamp message received (OK)
 *   MAY discard broadcast REQUESTs.  (OK, but see source for inconsistency)
 *   MUST reply using same source address as the request was sent to. (OK)
 *   MUST use reversed Source Route if possible (NOT YET)
 *   SHOULD update Record Route / Timestamp options (??)
 *   MUST pass REPLYs to transport/user layer (requires RAW, just like 
 *	ECHO) (OK)
 *   MUST update clock for timestamp at least 16 times/sec (OK)
 *   MUST be "correct within a few minutes" (OK)
 * 4.3.3.9 (Address Mask Request/Reply)
 *   MUST have support for receiving AMRq and responding with AMRe (OK, 
 *	but only as a compile-time option)
 *   SHOULD have option for each interface for AMRe's, MUST default to 
 *	NO (NOT YET)
 *   MUST NOT reply to AMRq before knows the correct AM (OK)
 *   MUST NOT respond to AMRq with source address 0.0.0.0 on physical
 *    	interfaces having multiple logical i-faces with different masks
 *	(NOT YET)
 *   SHOULD examine all AMRe's it receives and check them (NOT YET)
 *   SHOULD log invalid AMRe's (AM+sender) (NOT YET)
 *   MUST NOT use contents of AMRe to determine correct AM (OK)
 *   MAY broadcast AMRe's after having configured address masks (OK -- doesn't)
 *   MUST NOT do broadcast AMRe's if not set by extra option (OK, no option)
 *   MUST use the { <NetPrefix>, -1 } form of broadcast addresses (OK)
 * 4.3.3.10 (Router Advertisement and Solicitations)
 *   MUST support router part of Router Discovery Protocol on all networks we
 *     support broadcast or multicast addressing. (OK -- done by gated)
 *   MUST have all config parameters with the respective defaults (OK)
 * 5.2.7.1 (Destination Unreachable)
 *   MUST generate DU's (OK)
 *   SHOULD choose a best-match response code (OK)
 *   SHOULD NOT generate Host Isolated codes (OK)
 *   SHOULD use Communication Administratively Prohibited when administratively
 *     filtering packets (NOT YET -- bug-to-bug compatibility)
 *   MAY include config option for not generating the above and silently
 *	discard the packets instead (OK)
 *   MAY include config option for not generating Precedence Violation and
 *     Precedence Cutoff messages (OK as we don't generate them at all)
 *   MUST use Host Unreachable or Dest. Host Unknown codes whenever other hosts
 *     on the same network might be reachable (OK -- no net unreach's at all)
 *   MUST use new form of Fragmentation Needed and DF Set messages (OK)
 * 5.2.7.2 (Redirect)
 *   MUST NOT generate network redirects (OK)
 *   MUST be able to generate host redirects (OK)
 *   SHOULD be able to generate Host+TOS redirects (NO as we don't use TOS)
 *   MUST have an option to use Host redirects instead of Host+TOS ones (OK as
 *     no Host+TOS Redirects are used)
 *   MUST NOT generate redirects unless forwarding to the same i-face and the
 *     dest. address is on the same subnet as the src. address and no source
 *     routing is in use. (OK)
 *   MUST NOT follow redirects when using a routing protocol (OK)
 *   MAY use redirects if not using a routing protocol (OK, compile-time option)
 *   MUST comply to Host Requirements when not acting as a router (OK)
 *  5.2.7.3 (Time Exceeded)
 *   MUST generate Time Exceeded Code 0 when discarding packet due to TTL=0 (OK)
 *   MAY have a per-interface option to disable origination of TE messages, but
 *     it MUST default to "originate" (OK -- we don't support it)
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <net/route.h>
#include <net/protocol.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/raw.h>
#include <net/snmp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <net/checksum.h>

#define min(a,b)	((a)<(b)?(a):(b))

/*
 *	Statistics
 */
 
struct icmp_mib icmp_statistics;

/* An array of errno for error messages from dest unreach. */
/* RFC 1122: 3.2.2.1 States that NET_UNREACH, HOS_UNREACH and SR_FAIELD MUST be considered 'transient errs'. */

struct icmp_err icmp_err_convert[] = {
  { ENETUNREACH,	0 },	/*	ICMP_NET_UNREACH	*/
  { EHOSTUNREACH,	0 },	/*	ICMP_HOST_UNREACH	*/
  { ENOPROTOOPT,	1 },	/*	ICMP_PROT_UNREACH	*/
  { ECONNREFUSED,	1 },	/*	ICMP_PORT_UNREACH	*/
  { EMSGSIZE,		0 },	/*	ICMP_FRAG_NEEDED	*/
  { EOPNOTSUPP,		0 },	/*	ICMP_SR_FAILED		*/
  { ENETUNREACH,	1 },	/* 	ICMP_NET_UNKNOWN	*/
  { EHOSTDOWN,		1 },	/*	ICMP_HOST_UNKNOWN	*/
  { ENONET,		1 },	/*	ICMP_HOST_ISOLATED	*/
  { ENETUNREACH,	1 },	/*	ICMP_NET_ANO		*/
  { EHOSTUNREACH,	1 },	/*	ICMP_HOST_ANO		*/
  { ENETUNREACH,	0 },	/*	ICMP_NET_UNR_TOS	*/
  { EHOSTUNREACH,	0 },	/*	ICMP_HOST_UNR_TOS	*/
  { EHOSTUNREACH,	1 },	/*	ICMP_PKT_FILTERED	*/
  { EHOSTUNREACH,	1 },	/*	ICMP_PREC_VIOLATION	*/
  { EHOSTUNREACH,	1 }	/*	ICMP_PREC_CUTOFF	*/
};

/*
 *	A spare long used to speed up statistics updating
 */
 
unsigned long dummy;

/*
 *	ICMP transmit rate limit control structures. We use a relatively simple
 *	approach to the problem: For each type of ICMP message with rate limit
 *	we count the number of messages sent during some time quantum. If this
 *	count exceeds given maximal value, we ignore all messages not separated
 *	from the last message sent at least by specified time.
 */

#define XRLIM_CACHE_SIZE 16		/* How many destination hosts do we cache */

struct icmp_xrl_cache			/* One entry of the ICMP rate cache */
{
	__u32 daddr;			/* Destination address */
	unsigned long counter;		/* Message counter */
	unsigned long next_reset;	/* Time of next reset of the counter */
	unsigned long last_access;	/* Time of last access to this entry (LRU) */
	unsigned int restricted;	/* Set if we're in restricted mode */
	unsigned long next_packet;	/* When we'll allow a next packet if restricted */
};

struct icmp_xrlim
{
	unsigned long timeout;		/* Time quantum for rate measuring */
	unsigned long limit;		/* Maximal number of messages per time quantum allowed */
	unsigned long delay;		/* How long we wait between packets when restricting */
	struct icmp_xrl_cache cache[XRLIM_CACHE_SIZE];	/* Rate cache */
};

/*
 *	ICMP control array. This specifies what to do with each ICMP.
 */

struct icmp_control
{
	unsigned long *output;		/* Address to increment on output */
	unsigned long *input;		/* Address to increment on input */
	void (*handler)(struct icmphdr *icmph, struct sk_buff *skb, int len);
	unsigned long error;		/* This ICMP is classed as an error message */
	struct icmp_xrlim *xrlim;	/* Transmit rate limit control structure or NULL for no limits */
};

static struct icmp_control icmp_pointers[NR_ICMP_TYPES+1];

/*
 *	Build xmit assembly blocks
 */

struct icmp_bxm
{
	void *data_ptr;
	int data_len;
	struct icmphdr icmph;
	unsigned long csum;
	struct ip_options replyopts;
	unsigned char  optbuf[40];
};

/*
 *	The ICMP socket. This is the most convenient way to flow control
 *	our ICMP output as well as maintain a clean interface throughout
 *	all layers. All Socketless IP sends will soon be gone.
 */
	
struct inode icmp_inode;
struct socket *icmp_socket=&icmp_inode.u.socket_i;

/*
 *	Send an ICMP frame.
 */


/*
 *	Initialize the transmit rate limitation mechanism.
 */

#ifndef CONFIG_NO_ICMP_LIMIT

__initfunc(static void xrlim_init(void))
{
	int type, entry;
	struct icmp_xrlim *xr;

	for (type=0; type<=NR_ICMP_TYPES; type++) {
		xr = icmp_pointers[type].xrlim;
		if (xr) {
			for (entry=0; entry<XRLIM_CACHE_SIZE; entry++)
				xr->cache[entry].daddr = INADDR_NONE;
		}
	}
}

/*
 *	Check transmit rate limitation for given message.
 *
 *	RFC 1812: 4.3.2.8 SHOULD be able to limit error message rate
 *			  SHOULD allow setting of rate limits (we allow 
 *			  in the source)
 */

static int xrlim_allow(int type, __u32 addr)
{
	struct icmp_xrlim *r;
	struct icmp_xrl_cache *c;
	unsigned long now;

	if (type > NR_ICMP_TYPES)		/* No time limit present */
		return 1;
	r = icmp_pointers[type].xrlim;
	if (!r)
		return 1;

	for (c = r->cache; c < &r->cache[XRLIM_CACHE_SIZE]; c++)	
	  /* Cache lookup */
		if (c->daddr == addr)
			break;

	now = jiffies;		/* Cache current time (saves accesses to volatile variable) */

	if (c == &r->cache[XRLIM_CACHE_SIZE]) {		/* Cache miss */
		unsigned long oldest = now;		/* Find the oldest entry to replace */
		struct icmp_xrl_cache *d;
		c = r->cache;
		for (d = r->cache; d < &r->cache[XRLIM_CACHE_SIZE]; d++)
			if (!d->daddr) {		/* Unused entry */
				c = d;
				break;
			} else if (d->last_access < oldest) {
				oldest = d->last_access;
				c = d;
			}
		c->last_access = now;			/* Fill the entry with new data */
		c->daddr = addr;
		c->counter = 1;
		c->next_reset = now + r->timeout;
		c->restricted = 0;
		return 1;
	}

	c->last_access = now;
	if (c->next_reset > now) {			/* Let's increment the counter */
		c->counter++;
		if (c->counter == r->limit) {		/* Limit exceeded, start restrictions */
			c->restricted = 1;
			c->next_packet = now + r->delay;
			return 0;
		}
		if (c->restricted) {			/* Any restrictions pending? */
			if (c->next_packet > now)
				return 0;
			c->next_packet = now + r->delay;
			return 1;
		}
	} else {					/* Reset the counter */
		if (c->counter < r->limit)		/* Switch off all restrictions */
			c->restricted = 0;
		c->next_reset = now + r->timeout;
		c->counter = 0;
	}

	return 1;					/* Send the packet */
}

#endif /* CONFIG_NO_ICMP_LIMIT */

/*
 *	Maintain the counters used in the SNMP statistics for outgoing ICMP
 */
 
static void icmp_out_count(int type)
{
	if (type>NR_ICMP_TYPES)
		return;
	(*icmp_pointers[type].output)++;
	icmp_statistics.IcmpOutMsgs++;
}
 
/*
 *	Checksum each fragment, and on the first include the headers and final checksum.
 */
 
static int icmp_glue_bits(const void *p, char *to, unsigned int offset, unsigned int fraglen)
{
	struct icmp_bxm *icmp_param = (struct icmp_bxm *)p;
	struct icmphdr *icmph;
	unsigned long csum;

	if (offset) {
		icmp_param->csum=csum_partial_copy(icmp_param->data_ptr+offset-sizeof(struct icmphdr), 
				to, fraglen,icmp_param->csum);
		return 0;
	}

	/*
	 *	First fragment includes header. Note that we've done
	 *	the other fragments first, so that we get the checksum
	 *	for the whole packet here.
	 */
	csum = csum_partial_copy((void *)&icmp_param->icmph,
		to, sizeof(struct icmphdr), 
		icmp_param->csum);
	csum = csum_partial_copy(icmp_param->data_ptr,
		to+sizeof(struct icmphdr),
		fraglen-sizeof(struct icmphdr), csum);
	icmph=(struct icmphdr *)to;
	icmph->checksum = csum_fold(csum);
	return 0;
}
 
/*
 *	Driving logic for building and sending ICMP messages.
 */

static void icmp_reply(struct icmp_bxm *icmp_param, struct sk_buff *skb)
{
	struct sock *sk=icmp_socket->sk;
	struct ipcm_cookie ipc;
	struct rtable *rt = (struct rtable*)skb->dst;
	u32 daddr;

	if (ip_options_echo(&icmp_param->replyopts, skb))
		return;

	icmp_param->icmph.checksum=0;
	icmp_param->csum=0;
	icmp_out_count(icmp_param->icmph.type);

	sk->ip_tos = skb->nh.iph->tos;
	daddr = ipc.addr = rt->rt_src;
	ipc.opt = &icmp_param->replyopts;
	if (ipc.opt->srr)
		daddr = icmp_param->replyopts.faddr;
	if (ip_route_output(&rt, daddr, rt->rt_spec_dst, RT_TOS(skb->nh.iph->tos), NULL))
		return;
	ip_build_xmit(sk, icmp_glue_bits, icmp_param, 
		icmp_param->data_len+sizeof(struct icmphdr),
		&ipc, rt, MSG_DONTWAIT);
	ip_rt_put(rt);
}


/*
 *	Send an ICMP message in response to a situation
 *
 *	RFC 1122: 3.2.2	MUST send at least the IP header and 8 bytes of header. MAY send more (we do).
 *			MUST NOT change this header information.
 *			MUST NOT reply to a multicast/broadcast IP address.
 *			MUST NOT reply to a multicast/broadcast MAC address.
 *			MUST reply to only the first fragment.
 */

void icmp_send(struct sk_buff *skb_in, int type, int code, unsigned long info)
{
	struct iphdr *iph;
	struct icmphdr *icmph;
	int room;
	struct icmp_bxm icmp_param;
	struct rtable *rt = (struct rtable*)skb_in->dst;
	struct ipcm_cookie ipc;
	u32 saddr;
	u8  tos;
	
	/*
	 *	Find the original header
	 */
	 
	iph = skb_in->nh.iph;
	
	/*
	 *	No replies to physical multicast/broadcast
	 */
	 
	if (skb_in->pkt_type!=PACKET_HOST)
		return;
		
	/*
	 *	Now check at the protocol level
	 */
	if (!rt)
		return;
	if (rt->rt_flags&(RTF_BROADCAST|RTF_MULTICAST))
		return;
	 
		
	/*
	 *	Only reply to fragment 0. We byte re-order the constant
	 *	mask for efficiency.
	 */
	 
	if (iph->frag_off&htons(IP_OFFSET))
		return;
		
	/* 
	 *	If we send an ICMP error to an ICMP error a mess would result..
	 */
	 
	if (icmp_pointers[type].error) {
		/*
		 *	We are an error, check if we are replying to an ICMP error
		 */
		 
		if (iph->protocol==IPPROTO_ICMP) {
			icmph = (struct icmphdr *)((char *)iph + (iph->ihl<<2));
			/*
			 *	Assume any unknown ICMP type is an error. This isn't
			 *	specified by the RFC, but think about it..
			 */
			if (icmph->type>NR_ICMP_TYPES || icmp_pointers[icmph->type].error)
				return;
		}
	}

	/*
	 *	Check the rate limit
	 */

#ifndef CONFIG_NO_ICMP_LIMIT
	if (!xrlim_allow(type, iph->saddr))
		return;
#endif

	/*
	 *	Construct source address and options.
	 */
	
	saddr = iph->daddr;
	if (!(rt->rt_flags&RTF_LOCAL))
		saddr = 0;

	tos = icmp_pointers[type].error ?
		((iph->tos & IPTOS_TOS_MASK) | IPTOS_PREC_INTERNETCONTROL) :
			iph->tos;

	if (ip_route_output(&rt, iph->saddr, saddr, RT_TOS(tos), NULL))
		return;
	
	if (ip_options_echo(&icmp_param.replyopts, skb_in)) {
		ip_rt_put(rt);
		return;
	}

	/*
	 *	Prepare data for ICMP header.
	 */

	icmp_param.icmph.type=type;
	icmp_param.icmph.code=code;
	icmp_param.icmph.un.gateway = info;
	icmp_param.icmph.checksum=0;
	icmp_param.csum=0;
	icmp_param.data_ptr=iph;
	icmp_out_count(icmp_param.icmph.type);
	icmp_socket->sk->ip_tos = tos;
	ipc.addr = iph->saddr;
	ipc.opt = &icmp_param.replyopts;
	if (icmp_param.replyopts.srr) {
		ip_rt_put(rt);
		if (ip_route_output(&rt, icmp_param.replyopts.faddr, saddr, RT_TOS(tos), NULL))
			return;
	}

	/* RFC says return as much as we can without exceeding 576 bytes. */

	room = rt->u.dst.pmtu;
	if (room > 576)
		room = 576;
	room -= sizeof(struct iphdr) - icmp_param.replyopts.optlen;
	
	icmp_param.data_len=(iph->ihl<<2)+skb_in->len;
	if (icmp_param.data_len > room)
		icmp_param.data_len = room;
	
	ip_build_xmit(icmp_socket->sk, icmp_glue_bits, &icmp_param, 
		icmp_param.data_len+sizeof(struct icmphdr),
		&ipc, rt, MSG_DONTWAIT);

	ip_rt_put(rt);
}


/* 
 *	Handle ICMP_DEST_UNREACH, ICMP_TIME_EXCEED, and ICMP_QUENCH. 
 */

static void icmp_unreach(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
	struct iphdr *iph;
	int hash;
	struct inet_protocol *ipprot;
	unsigned char *dp;
	struct sock *raw_sk;
	
	/*
	 *	Incomplete header ?
	 */
	 
	if(skb->len<sizeof(struct iphdr)+8)
	{
		kfree_skb(skb, FREE_READ);
		return;
	}
	
	iph = (struct iphdr *) (icmph + 1);
	dp = (unsigned char*)iph;
	
	if(icmph->type==ICMP_DEST_UNREACH) {
		switch(icmph->code & 15) {
			case ICMP_NET_UNREACH:
				break;
			case ICMP_HOST_UNREACH:
				break;
			case ICMP_PROT_UNREACH:
				break;
			case ICMP_PORT_UNREACH:
				break;
			case ICMP_FRAG_NEEDED:
				if (ipv4_config.no_pmtu_disc)
					printk(KERN_INFO "ICMP: %s: fragmentation needed and DF set.\n",
					       in_ntoa(iph->daddr));
				else {
					unsigned short new_mtu;
					new_mtu = ip_rt_frag_needed(iph, ntohs(icmph->un.frag.mtu));
					if (!new_mtu) {
						kfree_skb(skb, FREE_READ);
						return;
					}
					icmph->un.frag.mtu = htons(new_mtu);
				}
				break;
			case ICMP_SR_FAILED:
				printk(KERN_INFO "ICMP: %s: Source Route Failed.\n", in_ntoa(iph->daddr));
				break;
			default:
				break;
		}
		if (icmph->code>NR_ICMP_UNREACH) {
			kfree_skb(skb, FREE_READ);
			return;
		}
	}
	
	/*
	 *	Throw it at our lower layers
	 *
	 *	RFC 1122: 3.2.2 MUST extract the protocol ID from the passed header.
	 *	RFC 1122: 3.2.2.1 MUST pass ICMP unreach messages to the transport layer.
	 *	RFC 1122: 3.2.2.2 MUST pass ICMP time expired messages to transport layer.
	 */
	 
	/*
	 *	Check the other end isnt violating RFC 1122. Some routers send
	 *	bogus responses to broadcast frames. If you see this message
	 *	first check your netmask matches at both ends, if it does then
	 *	get the other vendor to fix their kit.
	 */
	 
	if(__ip_chk_addr(iph->daddr)==IS_BROADCAST)
	{
		printk("%s sent an invalid ICMP error to a broadcast.\n",
			in_ntoa(skb->nh.iph->saddr));
		kfree_skb(skb, FREE_READ);
	}

	/*
	 *	Deliver ICMP message to raw sockets. Pretty useless feature?
	 */

	/* Note: See raw.c and net/raw.h, RAWV4_HTABLE_SIZE==MAX_INET_PROTOS */
	hash = iph->protocol & (MAX_INET_PROTOS - 1);
	if ((raw_sk = raw_v4_htable[hash]) != NULL) 
	{
		raw_sk = raw_v4_lookup(raw_sk, iph->protocol, iph->saddr, iph->daddr);
		while (raw_sk) 
		{
			raw_err(raw_sk, skb);
			raw_sk = raw_v4_lookup(raw_sk->next, iph->protocol,
					       iph->saddr, iph->daddr);
		}
	}

	/*
	 *	This can't change while we are doing it. 
	 */

	ipprot = (struct inet_protocol *) inet_protos[hash];
	while(ipprot != NULL) {
		struct inet_protocol *nextip;

		nextip = (struct inet_protocol *) ipprot->next;
	
		/* 
		 *	Pass it off to everyone who wants it. 
		 */

		/* RFC1122: OK. Passes appropriate ICMP errors to the */
		/* appropriate protocol layer (MUST), as per 3.2.2. */

		if (iph->protocol == ipprot->protocol && ipprot->err_handler)
			ipprot->err_handler(skb, dp);

		ipprot = nextip;
  	}

	kfree_skb(skb, FREE_READ);
}


/*
 *	Handle ICMP_REDIRECT. 
 */

static void icmp_redirect(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
	struct iphdr *iph;
	unsigned long ip;

	/*
	 *	Get the copied header of the packet that caused the redirect
	 */
	 
	iph = (struct iphdr *) (icmph + 1);
	ip = iph->daddr;


	switch(icmph->code & 7) {
		case ICMP_REDIR_NET:
		case ICMP_REDIR_NETTOS:
			/*
			 *	As per RFC recommendations now handle it as
			 *	a host redirect.
			 */
			 
		case ICMP_REDIR_HOST:
		case ICMP_REDIR_HOSTTOS:
			ip_rt_redirect(skb->nh.iph->saddr, ip, icmph->un.gateway, iph->saddr, iph->tos, skb->dev);
			break;
		default:
			break;
  	}
  	/*
  	 *	Discard the original packet
  	 */
  	 
  	kfree_skb(skb, FREE_READ);
}

/*
 *	Handle ICMP_ECHO ("ping") requests. 
 *
 *	RFC 1122: 3.2.2.6 MUST have an echo server that answers ICMP echo requests.
 *	RFC 1122: 3.2.2.6 Data received in the ICMP_ECHO request MUST be included in the reply.
 *	RFC 1812: 4.3.3.6 SHOULD have a config option for silently ignoring echo requests, MUST have default=NOT.
 *	See also WRT handling of options once they are done and working.
 */
 
static void icmp_echo(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
#ifndef CONFIG_IP_IGNORE_ECHO_REQUESTS
	struct icmp_bxm icmp_param;

	icmp_param.icmph=*icmph;
	icmp_param.icmph.type=ICMP_ECHOREPLY;
	icmp_param.data_ptr=(icmph+1);
	icmp_param.data_len=len;
	icmp_reply(&icmp_param, skb);
#endif
	kfree_skb(skb, FREE_READ);
}

/*
 *	Handle ICMP Timestamp requests. 
 *	RFC 1122: 3.2.2.8 MAY implement ICMP timestamp requests.
 *		  SHOULD be in the kernel for minimum random latency.
 *		  MUST be accurate to a few minutes.
 *		  MUST be updated at least at 15Hz.
 */
 
static void icmp_timestamp(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
	struct timeval tv;
	__u32 times[3];		/* So the new timestamp works on ALPHA's.. */
	struct icmp_bxm icmp_param;
	
	/*
	 *	Too short.
	 */
	 
	if(len<12) {
		icmp_statistics.IcmpInErrors++;
		kfree_skb(skb, FREE_READ);
		return;
	}
	
	/*
	 *	Fill in the current time as ms since midnight UT: 
	 */
	 
	do_gettimeofday(&tv);
	times[1] = htonl((tv.tv_sec % 86400) * 1000 + tv.tv_usec / 1000);
	times[2] = times[1];
	memcpy((void *)&times[0], icmph+1, 4);		/* Incoming stamp */
	icmp_param.icmph=*icmph;
	icmp_param.icmph.type=ICMP_TIMESTAMPREPLY;
	icmp_param.icmph.code=0;
	icmp_param.data_ptr=&times;
	icmp_param.data_len=12;
	icmp_reply(&icmp_param, skb);
	kfree_skb(skb,FREE_READ);
}


/* 
 *	Handle ICMP_ADDRESS_MASK requests.  (RFC950)
 *
 * RFC1122 (3.2.2.9).  A host MUST only send replies to 
 * ADDRESS_MASK requests if it's been configured as an address mask 
 * agent.  Receiving a request doesn't constitute implicit permission to 
 * act as one. Of course, implementing this correctly requires (SHOULD) 
 * a way to turn the functionality on and off.  Another one for sysctl(), 
 * I guess. -- MS
 *
 * RFC1812 (4.3.3.9).	A router MUST implement it.
 *			A router SHOULD have switch turning it on/off.
 *		      	This switch MUST be ON by default.
 *
 * Gratuitous replies, zero-source replies are not implemented,
 * that complies with RFC. DO NOT implement them!!! All the idea
 * of broadcast addrmask replies as specified in RFC950 is broken.
 * The problem is that it is not uncommon to have several prefixes
 * on one physical interface. Moreover, addrmask agent can even be
 * not aware of existing another prefixes.
 * If source is zero, addrmask agent cannot choose correct prefix.
 * Gratuitous mask announcements suffer from the same problem.
 * RFC1812 explains it, but still allows to use ADDRMASK,
 * that is pretty silly. --ANK
 */
 
static void icmp_address(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
	struct icmp_bxm icmp_param;
	struct rtable *rt = (struct rtable*)skb->dst;
	struct device *dev = skb->dev;

	if (!ipv4_config.addrmask_agent ||
	    ZERONET(rt->rt_src) ||
	    rt->rt_src_dev != rt->u.dst.dev ||
	    !(rt->rt_flags&RTCF_DIRECTSRC) ||
	    (rt->rt_flags&RTF_GATEWAY) ||
	    !(dev->ip_flags&IFF_IP_ADDR_OK) ||
	    !(dev->ip_flags&IFF_IP_MASK_OK)) {
		kfree_skb(skb, FREE_READ);
		return;
	}

	icmp_param.icmph.type=ICMP_ADDRESSREPLY;
	icmp_param.icmph.code=0;
	icmp_param.icmph.un.echo = icmph->un.echo;
	icmp_param.data_ptr=&dev->pa_mask;
	icmp_param.data_len=4;
	icmp_reply(&icmp_param, skb);
	kfree_skb(skb, FREE_READ);
}

/*
 * RFC1812 (4.3.3.9).	A router SHOULD listen all replies, and complain
 *			loudly if an inconsistency is found.
 */

static void icmp_address_reply(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
	struct rtable *rt = (struct rtable*)skb->dst;
	struct device *dev = skb->dev;
	u32 mask;

	if (!ipv4_config.log_martians ||
	    len < 4 ||
	    !(rt->rt_flags&RTCF_DIRECTSRC) ||
	    (rt->rt_flags&RTF_GATEWAY) ||
	    !(dev->ip_flags&IFF_IP_ADDR_OK) ||
	    !(dev->ip_flags&IFF_IP_MASK_OK)) {
		kfree_skb(skb, FREE_READ);
		return;
	}

	mask = *(u32*)&icmph[1];
	if (mask != dev->pa_mask)
		printk(KERN_INFO "Wrong address mask %08lX from %08lX/%s\n",
		       ntohl(mask), ntohl(rt->rt_src), dev->name);
	kfree_skb(skb, FREE_READ);
}

static void icmp_discard(struct icmphdr *icmph, struct sk_buff *skb, int len)
{
	kfree_skb(skb, FREE_READ);
}

#ifdef CONFIG_IP_TRANSPARENT_PROXY
/*
 *	Check incoming icmp packets not addressed locally, to check whether
 *	they relate to a (proxying) socket on our system.
 *	Needed for transparent proxying.
 *
 *	This code is presently ugly and needs cleanup.
 *	Probably should add a chkaddr entry to ipprot to call a chk routine
 *	in udp.c or tcp.c...
 */

/* This should work with the new hashes now. -DaveM */
extern struct sock *tcp_v4_lookup(u32 saddr, u16 sport, u32 daddr, u16 dport);
extern struct sock *udp_v4_lookup(u32 saddr, u16 sport, u32 daddr, u16 dport);

int icmp_chkaddr(struct sk_buff *skb)
{
	struct icmphdr *icmph=(struct icmphdr *)(skb->nh.raw + skb->nh.iph->ihl*4);
	struct iphdr *iph = (struct iphdr *) (icmph + 1);
	void (*handler)(struct icmphdr *icmph, struct sk_buff *skb, int len) = icmp_pointers[icmph->type].handler;

	if (handler == icmp_unreach || handler == icmp_redirect) {
		struct sock *sk;

		switch (iph->protocol) {
		case IPPROTO_TCP:
			{
			struct tcphdr *th = (struct tcphdr *)(((unsigned char *)iph)+(iph->ihl<<2));

			sk = tcp_v4_lookup(iph->daddr, th->dest, iph->saddr, th->source);
			if (!sk) return 0;
			if (sk->saddr != iph->saddr) return 0;
			if (sk->daddr != iph->daddr) return 0;
			if (sk->dummy_th.dest != th->dest) return 0;
			/*
			 * This packet came from us.
			 */
			return 1;
			}
		case IPPROTO_UDP:
			{
			struct udphdr *uh = (struct udphdr *)(((unsigned char *)iph)+(iph->ihl<<2));

			sk = udp_v4_lookup(iph->daddr, uh->dest, iph->saddr, uh->source);
			if (!sk) return 0;
			if (sk->saddr != iph->saddr && __ip_chk_addr(iph->saddr) != IS_MYADDR)
				return 0;
			/*
			 * This packet may have come from us.
			 * Assume it did.
			 */
			return 1;
			}
		}
	}
	return 0;
}

#endif

/* 
 *	Deal with incoming ICMP packets.
 */
 
int icmp_rcv(struct sk_buff *skb, unsigned short len)
{
	struct icmphdr *icmph = skb->h.icmph;
	struct rtable *rt = (struct rtable*)skb->dst;

	icmp_statistics.IcmpInMsgs++;
	
	if(len < sizeof(struct icmphdr))
	{
		icmp_statistics.IcmpInErrors++;
		printk(KERN_INFO "ICMP: runt packet\n");
		kfree_skb(skb, FREE_READ);
		return 0;
	}
 	
  	/*
	 *	Validate the packet
  	 */
	
	if (ip_compute_csum((unsigned char *) icmph, len)) {
		icmp_statistics.IcmpInErrors++;
		printk(KERN_INFO "ICMP: failed checksum from %s!\n", in_ntoa(skb->nh.iph->saddr));
		kfree_skb(skb, FREE_READ);
		return(0);
	}
	
	/*
	 *	18 is the highest 'known' ICMP type. Anything else is a mystery
	 *
	 *	RFC 1122: 3.2.2  Unknown ICMP messages types MUST be silently discarded.
	 */
	 
	if (icmph->type > NR_ICMP_TYPES) {
		icmp_statistics.IcmpInErrors++;		/* Is this right - or do we ignore ? */
		kfree_skb(skb,FREE_READ);
		return(0);
	}
	
	/*
	 *	Parse the ICMP message 
	 */

	if (rt->rt_flags&(RTF_BROADCAST|RTF_MULTICAST)) {
		/*
		 *	RFC 1122: 3.2.2.6 An ICMP_ECHO to broadcast MAY be silently ignored (we don't as it is used
		 *	by some network mapping tools).
		 *	RFC 1122: 3.2.2.8 An ICMP_TIMESTAMP MAY be silently discarded if to broadcast/multicast.
		 */
		if (icmph->type != ICMP_ECHO &&
		    icmph->type != ICMP_TIMESTAMP &&
		    icmph->type != ICMP_ADDRESS &&
		    icmph->type != ICMP_ADDRESSREPLY) {
			icmp_statistics.IcmpInErrors++;
			kfree_skb(skb, FREE_READ);
			return(0);
  		}
	}

	len -= sizeof(struct icmphdr);
	(*icmp_pointers[icmph->type].input)++;
	(icmp_pointers[icmph->type].handler)(icmph, skb, len);
	return 0;
}

/*
 *	This table defined limits of ICMP sending rate for various ICMP messages.
 */

static struct icmp_xrlim
	xrl_unreach = { 4*HZ, 80, HZ/4 },		/* Host Unreachable */
	xrl_generic = { 3*HZ, 30, HZ/4 };		/* All other errors */

/*
 *	This table is the definition of how we handle ICMP.
 */
 
static struct icmp_control icmp_pointers[NR_ICMP_TYPES+1] = {
/* ECHO REPLY (0) */
 { &icmp_statistics.IcmpOutEchoReps, &icmp_statistics.IcmpInEchoReps, icmp_discard, 0, NULL },
 { &dummy, &icmp_statistics.IcmpInErrors, icmp_discard, 1, NULL },
 { &dummy, &icmp_statistics.IcmpInErrors, icmp_discard, 1, NULL },
/* DEST UNREACH (3) */
 { &icmp_statistics.IcmpOutDestUnreachs, &icmp_statistics.IcmpInDestUnreachs, icmp_unreach, 1, &xrl_unreach },
/* SOURCE QUENCH (4) */
 { &icmp_statistics.IcmpOutSrcQuenchs, &icmp_statistics.IcmpInSrcQuenchs, icmp_unreach, 1, NULL },
/* REDIRECT (5) */
 { &icmp_statistics.IcmpOutRedirects, &icmp_statistics.IcmpInRedirects, icmp_redirect, 1, NULL },
 { &dummy, &icmp_statistics.IcmpInErrors, icmp_discard, 1, NULL },
 { &dummy, &icmp_statistics.IcmpInErrors, icmp_discard, 1, NULL },
/* ECHO (8) */
 { &icmp_statistics.IcmpOutEchos, &icmp_statistics.IcmpInEchos, icmp_echo, 0, NULL },
 { &dummy, &icmp_statistics.IcmpInErrors, icmp_discard, 1, NULL },
 { &dummy, &icmp_statistics.IcmpInErrors, icmp_discard, 1, NULL },
/* TIME EXCEEDED (11) */
 { &icmp_statistics.IcmpOutTimeExcds, &icmp_statistics.IcmpInTimeExcds, icmp_unreach, 1, &xrl_generic },
/* PARAMETER PROBLEM (12) */
/* FIXME: RFC1122 3.2.2.5 - MUST pass PARAM_PROB messages to transport layer */
 { &icmp_statistics.IcmpOutParmProbs, &icmp_statistics.IcmpInParmProbs, icmp_discard, 1, &xrl_generic },
/* TIMESTAMP (13) */
 { &icmp_statistics.IcmpOutTimestamps, &icmp_statistics.IcmpInTimestamps, icmp_timestamp, 0, NULL },
/* TIMESTAMP REPLY (14) */
 { &icmp_statistics.IcmpOutTimestampReps, &icmp_statistics.IcmpInTimestampReps, icmp_discard, 0, NULL },
/* INFO (15) */
 { &dummy, &dummy, icmp_discard, 0, NULL },
/* INFO REPLY (16) */
 { &dummy, &dummy, icmp_discard, 0, NULL },
/* ADDR MASK (17) */
 { &icmp_statistics.IcmpOutAddrMasks, &icmp_statistics.IcmpInAddrMasks, icmp_address, 0, NULL },
/* ADDR MASK REPLY (18) */
 { &icmp_statistics.IcmpOutAddrMaskReps, &icmp_statistics.IcmpInAddrMaskReps, icmp_address_reply, 0, NULL }
};

__initfunc(void icmp_init(struct net_proto_family *ops))
{
	int err;

	icmp_inode.i_mode = S_IFSOCK;
	icmp_inode.i_sock = 1;
	icmp_inode.i_uid = 0;
	icmp_inode.i_gid = 0;

	icmp_socket->inode = &icmp_inode;
	icmp_socket->state = SS_UNCONNECTED;
	icmp_socket->type=SOCK_RAW;

	if ((err=ops->create(icmp_socket, IPPROTO_ICMP))<0)
		panic("Failed to create the ICMP control socket.\n");
	icmp_socket->sk->allocation=GFP_ATOMIC;
	icmp_socket->sk->num = 256;		/* Don't receive any data */
	icmp_socket->sk->ip_ttl = MAXTTL;
#ifndef CONFIG_NO_ICMP_LIMIT
	xrlim_init();
#endif
}

