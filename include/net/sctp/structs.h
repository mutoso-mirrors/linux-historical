/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 International Business Machines Corp.
 * 
 * This file is part of the SCTP kernel reference Implementation
 * 
 * The SCTP reference implementation is free software; 
 * you can redistribute it and/or modify it under the terms of 
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * The SCTP reference implementation is distributed in the hope that it 
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  
 * 
 * Please send any bug reports or fixes you make to the
 * email addresses:
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 * 
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by: 
 *    Randall Stewart       <randall@sctp.chicago.il.us>
 *    Ken Morneau           <kmorneau@cisco.com>
 *    Qiaobing Xie          <qxie1@email.mot.com>
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Xingang Guo           <xingang.guo@intel.com>
 *    Hui Huang             <hui.huang@nokia.com>
 *    Sridhar Samudrala     <sri@us.ibm.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 *    Dajiang Zhang         <dajiang.zhang@nokia.com> 
 * 
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#ifndef __sctp_structs_h__
#define __sctp_structs_h__

#include <linux/time.h>		/* We get struct timespec.    */
#include <linux/socket.h>	/* linux/in.h needs this!!    */
#include <linux/in.h>		/* We get struct sockaddr_in. */
#include <linux/in6.h>          /* We get struct in6_addr     */
#include <asm/param.h>		/* We get MAXHOSTNAMELEN.     */
#include <asm/atomic.h>		/* This gets us atomic counters.  */
#include <linux/skbuff.h>	/* We need sk_buff_head. */
#include <linux/tqueue.h>	/* We need tq_struct.    */
#include <linux/sctp.h>         /* We need sctp* header structs.  */

/*
 * This is (almost) a direct quote from RFC 2553.
 */

/*
 * Desired design of maximum size and alignment
 */
#define _SS_MAXSIZE    128		/* Implementation specific max size */
#define _SS_ALIGNSIZE  (sizeof (__s64))
				/* Implementation specific desired alignment */
/*
 * Definitions used for sockaddr_storage structure paddings design.
 */
#define _SS_PAD1SIZE   (_SS_ALIGNSIZE - sizeof (sa_family_t))
#define _SS_PAD2SIZE   (_SS_MAXSIZE - (sizeof (sa_family_t)+ \
                              _SS_PAD1SIZE + _SS_ALIGNSIZE))

struct sockaddr_storage {
	sa_family_t  __ss_family;		/* address family */
	/* Following fields are implementation specific */
	char      __ss_pad1[_SS_PAD1SIZE];
				/* 6 byte pad, to make implementation */
				/* specific pad up to alignment field that */
				/* follows explicit in the data structure */
	__s64   __ss_align;	/* field to force desired structure */
				/* storage alignment */
	char      __ss_pad2[_SS_PAD2SIZE];
				/* 112 byte pad to achieve desired size, */
				/* _SS_MAXSIZE value minus size of ss_family */
				/* __ss_pad1, __ss_align fields is 112 */
};

/* A convenience structure for handling sockaddr structures.
 * We should wean ourselves off this.
 */
typedef union {
	struct sockaddr_in v4;
	struct sockaddr_in6 v6;
	struct sockaddr sa;
} sockaddr_storage_t;


/* Forward declarations for data structures. */
struct SCTP_protocol;
struct SCTP_endpoint;
struct SCTP_association;
struct SCTP_transport;
struct SCTP_packet;
struct SCTP_chunk;
struct SCTP_inqueue;
struct SCTP_outqueue;
struct SCTP_bind_addr;
struct sctp_opt;
struct sctp_endpoint_common;


typedef struct SCTP_protocol sctp_protocol_t;
typedef struct SCTP_endpoint sctp_endpoint_t;
typedef struct SCTP_association sctp_association_t;
typedef struct SCTP_transport sctp_transport_t;
typedef struct SCTP_packet sctp_packet_t;
typedef struct SCTP_chunk sctp_chunk_t;
typedef struct SCTP_inqueue sctp_inqueue_t;
typedef struct SCTP_outqueue sctp_outqueue_t;
typedef struct SCTP_bind_addr sctp_bind_addr_t;
typedef struct sctp_opt sctp_opt_t;
typedef struct sctp_endpoint_common sctp_endpoint_common_t;

#include <net/sctp/tsnmap.h>
#include <net/sctp/ulpevent.h>
#include <net/sctp/ulpqueue.h>


/* Structures useful for managing bind/connect. */

typedef struct sctp_bind_bucket {
	unsigned short 	port;
	unsigned short	fastreuse;
	struct sctp_bind_bucket *next;
	struct sctp_bind_bucket **pprev;
	struct sock	        *sk;
} sctp_bind_bucket_t;

typedef struct sctp_bind_hashbucket {
	spinlock_t	lock;
	struct sctp_bind_bucket	*chain;
} sctp_bind_hashbucket_t;

/* Used for hashing all associations.  */
typedef struct sctp_hashbucket {
	rwlock_t	lock;
	sctp_endpoint_common_t  *chain;
} sctp_hashbucket_t __attribute__((__aligned__(8)));


/* The SCTP protocol structure. */
struct SCTP_protocol {
	/* RFC2960 Section 14. Suggested SCTP Protocol Parameter Values
	 *
	 * The following protocol parameters are RECOMMENDED:
	 *
	 * RTO.Initial              - 3  seconds
	 * RTO.Min                  - 1  second
	 * RTO.Max                 -  60 seconds
	 * RTO.Alpha                - 1/8  (3 when converted to right shifts.)
	 * RTO.Beta                 - 1/4  (2 when converted to right shifts.)
	 */
	__u32 rto_initial;
	__u32 rto_min;
	__u32 rto_max;

	/* Note: rto_alpha and rto_beta are really defined as inverse
	 * powers of two to facilitate integer operations.
	 */
	int rto_alpha;
	int rto_beta;

	/* Max.Burst		    - 4 */
	int max_burst;

	/* Valid.Cookie.Life        - 60  seconds  */
	int valid_cookie_life;

	/* Association.Max.Retrans  - 10 attempts
	 * Path.Max.Retrans         - 5  attempts (per destination address)
	 * Max.Init.Retransmits     - 8  attempts
	 */
	int max_retrans_association;
	int max_retrans_path;
	int max_retrans_init;

	/* HB.interval              - 30 seconds  */
	int hb_interval;

	/* The following variables are implementation specific.  */

	/* Default initialization values to be applied to new associations. */
	__u16 max_instreams;
	__u16 max_outstreams;

	/* This is a list of groups of functions for each address
	 * family that we support.
	 */
	struct list_head address_families;

	/* This is the hash of all endpoints. */
	int ep_hashsize;
	sctp_hashbucket_t *ep_hashbucket;

	/* This is the hash of all associations. */
	int assoc_hashsize;
	sctp_hashbucket_t *assoc_hashbucket;

	/* This is the sctp port control hash.  */
	int port_hashsize;
	int port_rover;
	spinlock_t port_alloc_lock;  /* Protects port_rover. */
	sctp_bind_hashbucket_t *port_hashtable;

	/* This is the global local address list.
	 * We actively maintain this complete list of interfaces on
	 * the system by catching routing events.
	 *
	 * It is a list of struct sockaddr_storage_list.
	 */
	struct list_head local_addr_list;
	spinlock_t local_addr_lock;
};


/*
 * Pointers to address related SCTP functions.
 * (i.e. things that depend on the address family.)
 */
typedef struct sctp_func {
	int		(*queue_xmit)	(struct sk_buff *skb);
	int 		(*setsockopt)	(struct sock *sk,
					 int level,
					 int optname,
					 char *optval,
					 int optlen);
	int 		(*getsockopt)	(struct sock *sk,
					 int level,
					 int optname,
					 char *optval,
					 int *optlen);
	int		(*get_dst_mtu)	(const sockaddr_storage_t *address);
	__u16		net_header_len;	
	int		sockaddr_len;
	sa_family_t	sa_family;
	struct list_head list;
} sctp_func_t;

sctp_func_t *sctp_get_af_specific(const sockaddr_storage_t *address);

/* Protocol family functions. */
typedef struct sctp_pf {
	void (*event_msgname)(sctp_ulpevent_t *, char *, int *);
	void (*skb_msgname)(struct sk_buff *, char *, int *);
} sctp_pf_t;

/* SCTP Socket type: UDP or TCP style. */
typedef enum {
	SCTP_SOCKET_UDP = 0,
       SCTP_SOCKET_UDP_HIGH_BANDWIDTH,
       SCTP_SOCKET_TCP
} sctp_socket_type_t;

/* Per socket SCTP information. */
struct sctp_opt {
	/* What kind of a socket is this? */
	sctp_socket_type_t type;

	/* What is our base endpointer? */
	sctp_endpoint_t *ep;

	/* Various Socket Options.  */
	__u16 default_stream;
	__u32 default_ppid;
	struct sctp_initmsg initmsg;
	struct sctp_rtoinfo rtoinfo;
	struct sctp_paddrparams paddrparam;
	struct sctp_event_subscribe subscribe;
	__u32 autoclose;
	__u8 nodelay;
	__u8 disable_fragments;
	sctp_pf_t *pf;
};



/* This is our APPLICATION-SPECIFIC state cookie.
 * THIS IS NOT DICTATED BY THE SPECIFICATION.
 */
/* These are the parts of an association which we send in the cookie.
 * Most of these are straight out of:
 * RFC2960 12.2 Parameters necessary per association (i.e. the TCB)
 *
 */

typedef struct sctp_cookie {

        /* My          : Tag expected in every inbound packet and sent
         * Verification: in the INIT or INIT ACK chunk.
         * Tag         :
         */
        __u32 my_vtag;

        /* Peer's      : Tag expected in every outbound packet except
         * Verification: in the INIT chunk.
         * Tag         :
         */
        __u32 peer_vtag;

        /* The rest of these are not from the spec, but really need to
         * be in the cookie.
         */

	/* My Tie Tag  : Assist in discovering a restarting association. */
	__u32 my_ttag;

	/* Peer's Tie Tag: Assist in discovering a restarting association. */
	__u32 peer_ttag;

        /* When does this cookie expire? */
        struct timeval expiration;

	/* Number of inbound/outbound streams which are set
	 * and negotiated during the INIT process. */
	__u16 sinit_num_ostreams;
	__u16 sinit_max_instreams;

        /* This is the first sequence number I used.  */
	__u32 initial_tsn;

	/* This holds the originating address of the INIT packet.  */
	sockaddr_storage_t peer_addr;

	/* This is a shim for my peer's INIT packet, followed by
	 * a copy of the raw address list of the association.
	 * The length of the raw address list is saved in the
	 * raw_addr_list_len field, which will be used at the time when
	 * the association TCB is re-constructed from the cookie.
	 */
	__u32 raw_addr_list_len;
	sctp_init_chunk_t peer_init[0];
} sctp_cookie_t;


/* The format of our cookie that we send to our peer. */
typedef struct sctp_signed_cookie {
	__u8 signature[SCTP_SECRET_SIZE];
	sctp_cookie_t c;
} sctp_signed_cookie_t;


/* This convenience type allows us to avoid casting when walking
 * through a parameter list.
 */
typedef union {
	__u8 *v;
	sctp_paramhdr_t *p;

	sctp_cookie_preserve_param_t *bht;
	sctp_hostname_param_t *dns;
	sctp_cookie_param_t *cookie;
	sctp_supported_addrs_param_t *sat;
	sctp_ipv4addr_param_t *v4;
	sctp_ipv6addr_param_t *v6;
} sctpParam_t;

/* This is another convenience type to allocate memory for address
 * params for the maximum size and pass such structures around
 * internally.
 */
typedef union {
	sctp_ipv4addr_param_t v4;
	sctp_ipv6addr_param_t v6;
} sctpIpAddress_t;

/* RFC 2960.  Section 3.3.5 Heartbeat.
 *    Heartbeat Information: variable length
 *    The Sender-specific Heartbeat Info field should normally include
 *    information about the sender's current time when this HEARTBEAT
 *    chunk is sent and the destination transport address to which this
 *    HEARTBEAT is sent (see Section 8.3).
 */
typedef struct sctp_sender_hb_info {
	sctp_paramhdr_t param_hdr;
	sockaddr_storage_t daddr;
	unsigned long sent_at;
} sctp_sender_hb_info_t __attribute__((packed));

/* RFC2960 1.4 Key Terms
 *
 * o Chunk: A unit of information within an SCTP packet, consisting of
 * a chunk header and chunk-specific content.
 *
 * As a matter of convenience, we remember the SCTP common header for
 * each chunk as well as a few other header pointers...
 */
struct SCTP_chunk {
	/* These first three elements MUST PRECISELY match the first
	 * three elements of struct sk_buff.  This allows us to reuse
	 * all the skb_* queue management functions.
	 */
	sctp_chunk_t *next;
	sctp_chunk_t *prev;
	struct sk_buff_head *list;

	/* This is our link to the per-transport transmitted list.  */
	struct list_head transmitted_list;

	/* This field is used by chunks that hold fragmented data.
	 * For the first fragment this is the list that holds the rest of
	 * fragments. For the remaining fragments, this is the link to the
	 * frag_list maintained in the first fragment.
	 */
	struct list_head frag_list;

	/* This points to the sk_buff containing the actual data.  */
	struct sk_buff *skb;

	/* These are the SCTP headers by reverse order in a packet.
	 * Note that some of these may happen more than once.  In that
	 * case, we point at the "current" one, whatever that means
	 * for that level of header.
	 */

	/* We point this at the FIRST TLV parameter to chunk_hdr.  */
	sctpParam_t param_hdr;
	union {
		__u8 *v;
		sctp_datahdr_t *data_hdr;
		sctp_inithdr_t *init_hdr;
		sctp_sackhdr_t *sack_hdr;
		sctp_heartbeathdr_t *hb_hdr;
		sctp_sender_hb_info_t *hbs_hdr;
		sctp_shutdownhdr_t *shutdown_hdr;
		sctp_signed_cookie_t *cookie_hdr;
		sctp_ecnehdr_t *ecne_hdr;
		sctp_cwrhdr_t *ecn_cwr_hdr;
		sctp_errhdr_t *err_hdr;
	} subh;

	__u8 *chunk_end;

	sctp_chunkhdr_t *chunk_hdr;

	sctp_sctphdr_t  *sctp_hdr;

	/* This needs to be recoverable for SCTP_SEND_FAILED events. */
	struct sctp_sndrcvinfo sinfo;

	/* Which association does this belong to?  */
	sctp_association_t *asoc;

	/* What endpoint received this chunk? */
	sctp_endpoint_common_t *rcvr;

	/* We fill this in if we are calculating RTT. */
	unsigned long sent_at;

	__u8 rtt_in_progress;  /* Is this chunk used for RTT calculation? */
	__u8 num_times_sent;   /* How man times did we send this? */
	__u8 has_tsn;          /* Does this chunk have a TSN yet? */
	__u8 singleton;        /* Was this the only chunk in the packet? */
	__u8 end_of_packet;    /* Was this the last chunk in the packet? */
	__u8 ecn_ce_done;      /* Have we processed the ECN CE bit? */
	__u8 pdiscard;	  /* Discard the whole packet now? */
	__u8 tsn_gap_acked;	  /* Is this chunk acked by a GAP ACK? */
	__u8 fast_retransmit;    /* Is this chunk fast retransmitted? */ 
	__u8 tsn_missing_report; /* Data chunk missing counter. */

	/* What is the origin IP address for this chunk?  */
	sockaddr_storage_t source;

	/* For an inbound chunk, this tells us where it came from.
	 * For an outbound chunk, it tells us where we'd like it to
	 * go.  It is NULL if we have no preference.
	 */
	sctp_transport_t *transport;
};

sctp_chunk_t *sctp_make_chunk(const sctp_association_t *, __u8 type,
			      __u8 flags, int size);
void sctp_free_chunk(sctp_chunk_t *);
sctp_chunk_t *sctp_copy_chunk(sctp_chunk_t *, int flags);
void  *sctp_addto_chunk(sctp_chunk_t *chunk, int len, const void *data);
int sctp_user_addto_chunk(sctp_chunk_t *chunk, int len, struct iovec *data);
sctp_chunk_t *sctp_chunkify(struct sk_buff *, const sctp_association_t *,
			    struct sock *);
void sctp_init_source(sctp_chunk_t *chunk);
const sockaddr_storage_t *sctp_source(const sctp_chunk_t *chunk);

/* This is a structure for holding either an IPv6 or an IPv4 address.  */
/* sin_family -- AF_INET or AF_INET6
 * sin_port -- ordinary port number
 * sin_addr -- cast to either (struct in_addr) or (struct in6_addr)
 */
struct sockaddr_storage_list {
	struct list_head list;
	sockaddr_storage_t a;
};

typedef sctp_chunk_t *(sctp_packet_phandler_t)(sctp_association_t *);

/* This structure holds lists of chunks as we are assembling for
 * transmission.
 */
struct SCTP_packet {
	/* These are the SCTP header values (host order) for the packet. */
	__u16 source_port;
	__u16 destination_port;
	__u32 vtag;

	/* This contains the payload chunks.  */
	struct sk_buff_head chunks;
	/* This is the total size of all chunks INCLUDING padding.  */
	size_t size;

	/* The packet is destined for this transport address.
	 * The function we finally use to pass down to the next lower
	 * layer lives in the transport structure.
	 */
	sctp_transport_t *transport;

	/* Allow a callback for getting a high priority chunk
	 * bundled early into the packet (This is used for ECNE).
	 */
	sctp_packet_phandler_t *get_prepend_chunk;

	/* This packet should advertise ECN capability to the network
	 * via the ECT bit.
	 */
	int ecn_capable;

	/* This packet contains a COOKIE-ECHO chunk. */
	int has_cookie_echo;

	int malloced;
};

typedef int (sctp_outqueue_thandler_t)(sctp_outqueue_t *, void *);
typedef int (sctp_outqueue_ehandler_t)(sctp_outqueue_t *);
typedef sctp_packet_t *(sctp_outqueue_ohandler_init_t)
	(sctp_packet_t *,
         sctp_transport_t *,
         __u16 sport,
         __u16 dport);
typedef sctp_packet_t *(sctp_outqueue_ohandler_config_t)
        (sctp_packet_t *,
	 __u32 vtag,
	 int ecn_capable,
	 sctp_packet_phandler_t *get_prepend_chunk);
typedef sctp_xmit_t (sctp_outqueue_ohandler_t)(sctp_packet_t *,
                                               sctp_chunk_t *);
typedef int (sctp_outqueue_ohandler_force_t)(sctp_packet_t *);

sctp_outqueue_ohandler_init_t    sctp_packet_init;
sctp_outqueue_ohandler_config_t  sctp_packet_config;
sctp_outqueue_ohandler_t         sctp_packet_append_chunk;
sctp_outqueue_ohandler_t         sctp_packet_transmit_chunk;
sctp_outqueue_ohandler_force_t   sctp_packet_transmit;
void sctp_packet_free(sctp_packet_t *);


/* This represents a remote transport address.
 * For local transport addresses, we just use sockaddr_storage_t.
 *
 * RFC2960 Section 1.4 Key Terms
 *
 *   o  Transport address:  A Transport Address is traditionally defined
 *      by Network Layer address, Transport Layer protocol and Transport
 *      Layer port number.  In the case of SCTP running over IP, a
 *      transport address is defined by the combination of an IP address
 *      and an SCTP port number (where SCTP is the Transport protocol).
 *
 * RFC2960 Section 7.1 SCTP Differences from TCP Congestion control
 *
 *   o  The sender keeps a separate congestion control parameter set for
 *      each of the destination addresses it can send to (not each
 *      source-destination pair but for each destination).  The parameters
 *      should decay if the address is not used for a long enough time
 *      period.
 *
 */
struct SCTP_transport {
	/* A list of transports. */
	struct list_head transports;

	/* Reference counting. */
	atomic_t refcnt;
	int      dead;

	/* This is the peer's IP address and port. */
	sockaddr_storage_t ipaddr;

	/* These are the functions we call to handle LLP stuff.  */
	sctp_func_t *af_specific;

	/* Which association do we belong to?  */
	sctp_association_t *asoc;

	/* RFC2960
	 *
	 * 12.3 Per Transport Address Data
	 *
	 * For each destination transport address in the peer's
	 * address list derived from the INIT or INIT ACK chunk, a
	 * number of data elements needs to be maintained including:
	 */
	__u32 rtt;		/* This is the most recent RTT.  */

	/* RTO         : The current retransmission timeout value.  */
	__u32 rto;

	/* RTTVAR      : The current RTT variation.  */
	__u32 rttvar;

	/* SRTT        : The current smoothed round trip time.  */
	__u32 srtt;

	/* RTO-Pending : A flag used to track if one of the DATA
	 *              chunks sent to this address is currently being
	 *              used to compute a RTT. If this flag is 0,
	 *              the next DATA chunk sent to this destination
	 *              should be used to compute a RTT and this flag
	 *              should be set. Every time the RTT
	 *              calculation completes (i.e. the DATA chunk
	 *              is SACK'd) clear this flag.
         */
	int rto_pending;


	/*
	 * These are the congestion stats.
	 */
	/* cwnd        : The current congestion window.  */
	__u32 cwnd;               /* This is the actual cwnd.  */

	/* ssthresh    : The current slow start threshold value.  */
	__u32 ssthresh;

	/* partial     : The tracking method for increase of cwnd when in
	 * bytes acked : congestion avoidance mode (see Section 6.2.2)
	 */
	__u32 partial_bytes_acked;

	/* Data that has been sent, but not acknowledged. */
	__u32 flight_size;

	/* PMTU       : The current known path MTU.  */
	__u32 pmtu;

	/* When was the last time(in jiffies) that a data packet was sent on
	 * this transport?  This is used to adjust the cwnd when the transport
	 * becomes inactive.
	 */
	unsigned long last_time_used;

	/* Heartbeat interval: The endpoint sends out a Heartbeat chunk to
	 * the destination address every heartbeat interval.
	 */
	int hb_interval;

	/* When was the last time (in jiffies) that we heard from this
	 * transport?  We use this to pick new active and retran paths.
	 */
	unsigned long last_time_heard;

	/* Last time(in jiffies) when cwnd is reduced due to the congestion
	 * indication based on ECNE chunk.
	 */
	unsigned long last_time_ecne_reduced;

	/* state       : The current state of this destination,
	 *             :  i.e. DOWN, UP, ALLOW-HB, NO-HEARTBEAT, etc.
	 */
	struct {
		int active;
		int hb_allowed;
	} state;

	/* These are the error stats for this destination.  */

	/* Error count : The current error count for this destination.  */
	unsigned short error_count;

	/* Error       : Current error threshold for this destination
	 * Threshold   : i.e. what value marks the destination down if
	 *             : errorCount reaches this value.
	 */
	unsigned short error_threshold;

	/* This is the max_retrans value for the transport and will
	 * be initialized to proto.max_retrans.path.  This can be changed
	 * using SCTP_SET_PEER_ADDR_PARAMS socket option.
	 */
	int max_retrans;

	/* We use this name for debugging output... */
	char *debug_name;

	/* Per         : A timer used by each destination.
	 * Destination :
	 * Timer       :
	 *
	 * [Everywhere else in the text this is called T3-rtx. -ed]
	 */
	struct timer_list T3_rtx_timer;

	/* Heartbeat timer is per destination. */
	struct timer_list hb_timer;

	/* Since we're using per-destination retransmission timers
	 * (see above), we're also using per-destination "transmitted"
	 * queues.  This probably ought to be a private struct
	 * accessible only within the outqueue, but it's not, yet.
	 */
	struct list_head transmitted;

	/* We build bundle-able packets for this transport here.  */
	sctp_packet_t packet;

	/* This is the list of transports that have chunks to send.  */
	struct list_head send_ready;

	int malloced; /* Is this structure kfree()able? */
};

extern sctp_transport_t *sctp_transport_new(const sockaddr_storage_t *, int);
extern sctp_transport_t *sctp_transport_init(sctp_transport_t *,
					     const sockaddr_storage_t *, int);
extern void sctp_transport_set_owner(sctp_transport_t *, sctp_association_t *);
extern void sctp_transport_free(sctp_transport_t *);
extern void sctp_transport_destroy(sctp_transport_t *);
extern void sctp_transport_reset_timers(sctp_transport_t *);
extern void sctp_transport_hold(sctp_transport_t *);
extern void sctp_transport_put(sctp_transport_t *);
extern void sctp_transport_update_rto(sctp_transport_t *, __u32);
extern void sctp_transport_raise_cwnd(sctp_transport_t *, __u32, __u32);
extern void sctp_transport_lower_cwnd(sctp_transport_t *, sctp_lower_cwnd_t);

/* This is the structure we use to queue packets as they come into
 * SCTP.  We write packets to it and read chunks from it.  It handles
 * fragment reassembly and chunk unbundling.
 */
struct SCTP_inqueue {
	/* This is actually a queue of sctp_chunk_t each
	 * containing a partially decoded packet.
	 */
	struct sk_buff_head in;
	/* This is the packet which is currently off the in queue and is
	 * being worked on through the inbound chunk processing.
	 */
	sctp_chunk_t *in_progress;

	/* This is the delayed task to finish delivering inbound
	 * messages.
	 */
	struct tq_struct immediate;

	int malloced;        /* Is this structure kfree()able?  */
};

sctp_inqueue_t *sctp_inqueue_new(void);
void sctp_inqueue_init(sctp_inqueue_t *);
void sctp_inqueue_free(sctp_inqueue_t *);
void sctp_push_inqueue(sctp_inqueue_t *, sctp_chunk_t *packet);
sctp_chunk_t *sctp_pop_inqueue(sctp_inqueue_t *);
void sctp_inqueue_set_th_handler(sctp_inqueue_t *,
                                 void (*)(void *), void *);

/* This is the structure we use to hold outbound chunks.  You push
 * chunks in and they automatically pop out the other end as bundled
 * packets (it calls (*output_handler)()).
 *
 * This structure covers sections 6.3, 6.4, 6.7, 6.8, 6.10, 7., 8.1,
 * and 8.2 of the v13 draft.
 *
 * It handles retransmissions.  The connection to the timeout portion
 * of the state machine is through sctp_..._timeout() and timeout_handler.
 *
 * If you feed it SACKs, it will eat them.
 *
 * If you give it big chunks, it will fragment them.
 *
 * It assigns TSN's to data chunks.  This happens at the last possible
 * instant before transmission.
 *
 * When free()'d, it empties itself out via output_handler().
 */
struct SCTP_outqueue {
	sctp_association_t *asoc;

	/* BUG: This really should be an array of streams.
	 * This really holds a list of chunks (one stream).
	 * FIXME: If true, why so?
	 */
	struct sk_buff_head out;

	/* These are control chunks we want to send.  */
	struct sk_buff_head control;

	/* These are chunks that have been sacked but are above the
	 * CTSN, or cumulative tsn ack point.
	 */
	struct list_head sacked;

	/* Put chunks on this list to schedule them for
	 * retransmission.
	 */
	struct list_head retransmit;

	/* Call these functions to send chunks down to the next lower
	 * layer.  This is always SCTP_packet, but we separate the two
	 * structures to make testing simpler.
	 */
	sctp_outqueue_ohandler_init_t	*init_output;
	sctp_outqueue_ohandler_config_t	*config_output;
	sctp_outqueue_ohandler_t	*append_output;
	sctp_outqueue_ohandler_t	*build_output;
	sctp_outqueue_ohandler_force_t	*force_output;

	/* How many unackd bytes do we have in-flight?  */
	__u32 outstanding_bytes;

	/* Is this structure empty?  */
	int empty;

	/* Are we kfree()able? */
	int malloced;
};

sctp_outqueue_t *sctp_outqueue_new(sctp_association_t *);
void sctp_outqueue_init(sctp_association_t *, sctp_outqueue_t *);
void sctp_outqueue_teardown(sctp_outqueue_t *);
void sctp_outqueue_free(sctp_outqueue_t*);
void sctp_force_outqueue(sctp_outqueue_t *);
int sctp_push_outqueue(sctp_outqueue_t *, sctp_chunk_t *chunk);
int sctp_flush_outqueue(sctp_outqueue_t *, int);
int sctp_sack_outqueue(sctp_outqueue_t *, sctp_sackhdr_t *);
int sctp_outqueue_is_empty(const sctp_outqueue_t *);
int sctp_outqueue_set_output_handlers(sctp_outqueue_t *,
                                      sctp_outqueue_ohandler_init_t init,
                                      sctp_outqueue_ohandler_config_t config,
                                      sctp_outqueue_ohandler_t append,
                                      sctp_outqueue_ohandler_t build,
                                      sctp_outqueue_ohandler_force_t force);
void sctp_outqueue_restart(sctp_outqueue_t *);
void sctp_retransmit(sctp_outqueue_t *, sctp_transport_t *, __u8);
void sctp_retransmit_mark(sctp_outqueue_t *, sctp_transport_t *, __u8);


/* These bind address data fields common between endpoints and associations */
struct SCTP_bind_addr {

	/* RFC 2960 12.1 Parameters necessary for the SCTP instance
	 *
	 * SCTP Port:   The local SCTP port number the endpoint is
	 * 		bound to.
	 */
	__u16 port;

	/* RFC 2960 12.1 Parameters necessary for the SCTP instance
	 *
	 * Address List: The list of IP addresses that this instance
	 *	has bound.  This information is passed to one's
	 *	peer(s) in INIT and INIT ACK chunks.
	 */
	struct list_head address_list;

	int malloced;        /* Are we kfree()able?  */
};

sctp_bind_addr_t *sctp_bind_addr_new(int gfp_mask);
void sctp_bind_addr_init(sctp_bind_addr_t *, __u16 port);
void sctp_bind_addr_free(sctp_bind_addr_t *);
int sctp_bind_addr_copy(sctp_bind_addr_t *dest, const sctp_bind_addr_t *src,
			sctp_scope_t scope, int priority,int flags);
int sctp_add_bind_addr(sctp_bind_addr_t *, sockaddr_storage_t *,
		       int priority);
int sctp_del_bind_addr(sctp_bind_addr_t *, sockaddr_storage_t *);
int sctp_bind_addr_has_addr(sctp_bind_addr_t *, const sockaddr_storage_t *);
sctpParam_t sctp_bind_addrs_to_raw(const sctp_bind_addr_t *bp,
				   int *addrs_len,
				   int priority);
int sctp_raw_to_bind_addrs(sctp_bind_addr_t *bp,
			   __u8 *raw_addr_list,
			   int addrs_len,
			   unsigned short port,
			   int priority);

sctp_scope_t sctp_scope(const sockaddr_storage_t *);
int sctp_in_scope(const sockaddr_storage_t *addr, const sctp_scope_t scope);
int sctp_is_any(const sockaddr_storage_t *addr);
int sctp_addr_is_valid(const sockaddr_storage_t *addr);


/* What type of sctp_endpoint_common?  */
typedef enum {
	SCTP_EP_TYPE_SOCKET,
	SCTP_EP_TYPE_ASSOCIATION,
} sctp_endpoint_type_t; 

/*
 * A common base class to bridge the implmentation view of a
 * socket (usually listening) endpoint versus an association's
 * local endpoint.
 * This common structure is useful for several purposes:
 *   1) Common interface for lookup routines.
 *      a) Subfunctions work for either endpoint or association
 *      b) Single interface to lookup allows hiding the lookup lock rather
 *         than acquiring it externally.
 *   2) Common interface for the inbound chunk handling/state machine.
 *   3) Common object handling routines for reference counting, etc.
 *   4) Disentangle association lookup from endpoint lookup, where we
 *      do not have to find our endpoint to find our association.
 *
 */

struct sctp_endpoint_common {
	/* Fields to help us manage our entries in the hash tables. */
	sctp_endpoint_common_t *next;
	sctp_endpoint_common_t **pprev;
	int hashent;

	/* Runtime type information.  What kind of endpoint is this? */
	sctp_endpoint_type_t type;

	/* Some fields to help us manage this object.
	 *   refcnt   - Reference count access to this object.
	 *   dead     - Do not attempt to use this object.
	 *   malloced - Do we need to kfree this object?
	 */
	atomic_t    refcnt;
	char        dead;
	char        malloced;

	/* What socket does this endpoint belong to?  */
	struct sock *sk;

	/* This is where we receive inbound chunks.  */
	sctp_inqueue_t   inqueue;

	/* This substructure includes the defining parameters of the
	 * endpoint:
	 * bind_addr.port is our shared port number.
	 * bind_addr.address_list is our set of local IP addresses.
	 */
	sctp_bind_addr_t bind_addr;

	/* Protection during address list comparisons. */
	rwlock_t   addr_lock;
};


/* RFC Section 1.4 Key Terms
 *
 * o SCTP endpoint: The logical sender/receiver of SCTP packets. On a
 *   multi-homed host, an SCTP endpoint is represented to its peers as a
 *   combination of a set of eligible destination transport addresses to
 *   which SCTP packets can be sent and a set of eligible source
 *   transport addresses from which SCTP packets can be received.
 *   All transport addresses used by an SCTP endpoint must use the
 *   same port number, but can use multiple IP addresses. A transport
 *   address used by an SCTP endpoint must not be used by another
 *   SCTP endpoint. In other words, a transport address is unique
 *   to an SCTP endpoint.
 *
 * From an implementation perspective, each socket has one of these.
 * A TCP-style socket will have exactly one association on one of
 * these.  An UDP-style socket will have multiple associations hanging
 * off one of these.
 */

struct SCTP_endpoint {
	/* Common substructure for endpoint and association. */
	sctp_endpoint_common_t base;

	/* These are the system-wide defaults and other stuff which is
	 * endpoint-independent.
	 */
	sctp_protocol_t *proto;

	/* Associations: A list of current associations and mappings
	 *            to the data consumers for each association. This
	 *            may be in the form of a hash table or other
	 *            implementation dependent structure. The data
	 *            consumers may be process identification
	 *            information such as file descriptors, named pipe
	 *            pointer, or table pointers dependent on how SCTP
	 *            is implemented.
	 */
	/* This is really a list of sctp_association_t entries. */
	struct list_head asocs;

	/* Secret Key: A secret key used by this endpoint to compute
	 *            the MAC.  This SHOULD be a cryptographic quality
	 *            random number with a sufficient length.
	 *	      Discussion in [RFC1750] can be helpful in
	 * 	      selection of the key.
	 */
	__u8 secret_key[SCTP_HOW_MANY_SECRETS][SCTP_SECRET_SIZE];
	int current_key;
	int last_key;
	int key_changed_at;

	/* Default timeouts.  */
	int timeouts[SCTP_NUM_TIMEOUT_TYPES];

	/* Various thresholds.  */

	/* Name for debugging output... */
	char *debug_name;
};

/* Recover the outter endpoint structure. */
static inline sctp_endpoint_t *sctp_ep(sctp_endpoint_common_t *base)
{
	sctp_endpoint_t *ep;

	/* We are not really a list, but the list_entry() macro is
	 * really quite generic to find the address of an outter struct.
	 */
	ep = list_entry(base, sctp_endpoint_t, base);
	return ep;
}

/* These are function signatures for manipulating endpoints.  */
sctp_endpoint_t *sctp_endpoint_new(sctp_protocol_t *, struct sock *, int);
sctp_endpoint_t *sctp_endpoint_init(sctp_endpoint_t *, sctp_protocol_t *,
				    struct sock *, int priority);
void sctp_endpoint_free(sctp_endpoint_t *);
void sctp_endpoint_put(sctp_endpoint_t *);
void sctp_endpoint_hold(sctp_endpoint_t *);
void sctp_endpoint_add_asoc(sctp_endpoint_t *, sctp_association_t *asoc);
sctp_association_t *sctp_endpoint_lookup_assoc(const sctp_endpoint_t *ep,
					       const sockaddr_storage_t *paddr,
					       sctp_transport_t **);
sctp_endpoint_t *sctp_endpoint_is_match(sctp_endpoint_t *,
					const sockaddr_storage_t *);

int sctp_verify_init(const sctp_association_t *asoc, 
		     sctp_cid_t cid, 
		     sctp_init_chunk_t *peer_init, 
		     sctp_chunk_t *chunk, 
		     sctp_chunk_t **err_chunk);
int sctp_verify_param(const sctp_association_t *asoc, 
		      sctpParam_t param, 
		      sctp_cid_t cid, 
		      sctp_chunk_t *chunk, 
		      sctp_chunk_t **err_chunk);
int sctp_process_unk_param(const sctp_association_t *asoc,
			   sctpParam_t param,
			   sctp_chunk_t *chunk,
			   sctp_chunk_t **err_chunk);
void sctp_process_init(sctp_association_t *asoc, sctp_cid_t cid,
		       const sockaddr_storage_t *peer_addr,
		       sctp_init_chunk_t  *peer_init, int priority);
int sctp_process_param(sctp_association_t *asoc,
		       sctpParam_t param,
		       const sockaddr_storage_t *peer_addr,
		       sctp_cid_t cid, int priority);
__u32 sctp_generate_tag(const sctp_endpoint_t *ep);
__u32 sctp_generate_tsn(const sctp_endpoint_t *ep);


/* RFC2960
 *
 * 12. Recommended Transmission Control Block (TCB) Parameters
 *
 * This section details a recommended set of parameters that should
 * be contained within the TCB for an implementation. This section is
 * for illustrative purposes and should not be deemed as requirements
 * on an implementation or as an exhaustive list of all parameters
 * inside an SCTP TCB. Each implementation may need its own additional
 * parameters for optimization.
 */


/* Here we have information about each individual association. */
struct SCTP_association {

	/* A base structure common to endpoint and association.
	 * In this context, it represents the associations's view
	 * of the local endpoint of the association.
	 */
	sctp_endpoint_common_t base;

	/* Associations on the same socket. */
	struct list_head asocs;

	/* This is a signature that lets us know that this is a
	 * sctp_association_t data structure.  Used for mapping an
	 * association id to an association.
	 */
	__u32 eyecatcher;

	/* This is our parent endpoint.  */
	sctp_endpoint_t *ep;

	/* These are those association elements needed in the cookie.  */
	sctp_cookie_t c;

	/* This is all information about our peer.  */
	struct {
		/* rwnd
		 *
		 * Peer Rwnd   : Current calculated value of the peer's rwnd.
		 */
		__u32 rwnd;

		/* transport_addr_list
		 *
		 * Peer        : A list of SCTP transport addresses that the
		 * Transport   : peer is bound to. This information is derived
		 * Address     : from the INIT or INIT ACK and is used to
		 * List        : associate an inbound packet with a given
		 *             : association. Normally this information is
		 *	       : hashed or keyed for quick lookup and access
		 *	       : of the TCB.
		 *
		 * It is a list of SCTP_transport's.
		 */
		struct list_head transport_addr_list;

		/* port
		 *   The transport layer port number.
		 */
		__u16 port;

		/* primary_path
		 *
		 * Primary     : This is the current primary destination
		 * Path        : transport address of the peer endpoint.  It
		 *             : may also specify a source transport address
		 *	       : on this endpoint.
		 *
		 * All of these paths live on transport_addr_list.
		 *
		 * At the bakeoffs, we discovered that the intent of
		 * primaryPath is that it only changes when the ULP
		 * asks to have it changed.  We add the activePath to
		 * designate the connection we are currently using to
		 * transmit new data and most control chunks.
		 */
		sctp_transport_t *primary_path;

		/* Cache the primary path address here, when we
		 * need a an address for msg_name. 
		 */
		sockaddr_storage_t primary_addr;
		
		/* active_path
		 *   The path that we are currently using to
		 *   transmit new data and most control chunks.
		 */
		sctp_transport_t *active_path;

		/* retran_path
		 *
		 * RFC2960 6.4 Multi-homed SCTP Endpoints
		 * ...
		 * Furthermore, when its peer is multi-homed, an
		 * endpoint SHOULD try to retransmit a chunk to an
		 * active destination transport address that is
		 * different from the last destination address to
		 * which the DATA chunk was sent.
		 */
		sctp_transport_t *retran_path;

		/* Pointer to last transport I have sent on.  */
		sctp_transport_t *last_sent_to;

		/* This is the last transport I have recieved DATA on.  */
		sctp_transport_t *last_data_from;

		/*
		 * Mapping  An array of bits or bytes indicating which out of
		 * Array    order TSN's have been received (relative to the
		 *          Last Rcvd TSN). If no gaps exist, i.e. no out of
		 *          order packets have been received, this array
		 *          will be set to all zero. This structure may be
		 *          in the form of a circular buffer or bit array.
		 *
		 * Last Rcvd   : This is the last TSN received in
		 * TSN	       : sequence. This value is set initially by
		 *             : taking the peer's Initial TSN, received in
		 *             : the INIT or INIT ACK chunk, and subtracting
		 *             : one from it.
		 *
		 * Throughout most of the specification this is called the
		 * "Cumulative TSN ACK Point".  In this case, we
		 * ignore the advice in 12.2 in favour of the term
		 * used in the bulk of the text.  This value is hidden
		 * in tsn_map--we get it by calling sctp_tsnmap_get_ctsn().
		 */
		sctp_tsnmap_t tsn_map;
		__u8 _map[sctp_tsnmap_storage_size(SCTP_TSN_MAP_SIZE)];

		/* We record duplicate TSNs here.  We clear this after
		 * every SACK.
		 * FIXME: We should move this into the tsnmap? --jgrimm
		 */
		sctp_dup_tsn_t dup_tsns[SCTP_MAX_DUP_TSNS];
		int next_dup_tsn;

		/* Do we need to sack the peer? */
		uint8_t sack_needed;

		/* These are capabilities which our peer advertised.  */
		__u8	ecn_capable;     /* Can peer do ECN? */
		__u8	ipv4_address;    /* Peer understands IPv4 addresses? */
		__u8	ipv6_address;    /* Peer understands IPv6 addresses? */
		__u8	hostname_address;/* Peer understands DNS addresses? */
		sctp_inithdr_t i;
		int cookie_len;
		void *cookie;

		/* ADDIP Extention (ADDIP)		--xguo */
		/* <expected peer-serial-number> minus 1 (ADDIP sec. 4.2 C1) */
		__u32 addip_serial;
	} peer;

	/* State       : A state variable indicating what state the
	 *	       : association is in, i.e. COOKIE-WAIT,
	 *             : COOKIE-ECHOED, ESTABLISHED, SHUTDOWN-PENDING,
	 *             : SHUTDOWN-SENT, SHUTDOWN-RECEIVED, SHUTDOWN-ACK-SENT.
	 *
	 *              Note: No "CLOSED" state is illustrated since if a
	 *              association is "CLOSED" its TCB SHOULD be removed.
	 *
	 * 		In this implementation we DO have a CLOSED
	 *		state which is used during initiation and shutdown.
	 *
	 * 		State takes values from SCTP_STATE_*.
	 */
	sctp_state_t state;

	/* When did we enter this state?  */
	int state_timestamp;

	/* The cookie life I award for any cookie.  */
	struct timeval cookie_life;
	__u32 cookie_preserve;

	/* Overall     : The overall association error count.
	 * Error Count : [Clear this any time I get something.]
	 */
	int overall_error_count;

	/* Overall     : The threshold for this association that if
	 * Error       : the Overall Error Count reaches will cause
	 * Threshold   : this association to be torn down. 
	 */
	int overall_error_threshold;

	/* These are the association's initial, max, and min RTO values.
	 * These values will be initialized by system defaults, but can
	 * be modified via the SCTP_RTOINFO socket option.
	 */
	__u32 rto_initial;
	__u32 rto_max;
	__u32 rto_min;

	/* Maximum number of new data packets that can be sent in a burst.  */
	int max_burst;

	/* This is the max_retrans value for the association.  This value will
	 * be initialized initialized from system defaults, but can be
	 * modified by the SCTP_ASSOCINFO socket option.
	 */
	int max_retrans;

	/* Maximum number of times the endpoint will retransmit INIT  */
	__u16 max_init_attempts;

	/* How many times have we resent an INIT? */
	__u16 init_retries;

	/* The largest timeout or RTO value to use in attempting an INIT */
	__u16 max_init_timeo;


	int timeouts[SCTP_NUM_TIMEOUT_TYPES];
	struct timer_list timers[SCTP_NUM_TIMEOUT_TYPES];

	/* Transport to which SHUTDOWN chunk was last sent.  */
	sctp_transport_t *shutdown_last_sent_to;

	/* Next TSN    : The next TSN number to be assigned to a new
	 *             : DATA chunk.  This is sent in the INIT or INIT
	 *             : ACK chunk to the peer and incremented each
	 *             : time a DATA chunk is assigned a TSN
	 *             : (normally just prior to transmit or during
	 *	       : fragmentation).
	 */
	__u32 next_tsn;

	/* 
	 * Last Rcvd   : This is the last TSN received in sequence.  This value
	 * TSN         : is set initially by taking the peer's Initial TSN,
	 *             : received in the INIT or INIT ACK chunk, and
	 *             : subtracting one from it.
	 *
	 * Most of RFC 2960 refers to this as the Cumulative TSN Ack Point. 
	 */

	__u32 ctsn_ack_point;

	/* The number of unacknowledged data chunks.  Reported through
	 * the SCTP_STATUS sockopt.
	 */
	__u16 unack_data;

	/* This is the association's receive buffer space.  This value is used
	 * to set a_rwnd field in an INIT or a SACK chunk.
	 */
	__u32 rwnd;

	/* Number of bytes by which the rwnd has slopped.  The rwnd is allowed
	 * to slop over a maximum of the association's frag_point.
	 */
	__u32 rwnd_over;

	/* This is the sndbuf size in use for the association.
	 * This corresponds to the sndbuf size for the association,
	 * as specified in the sk->sndbuf.
	 */
	int sndbuf_used;

	/* This is the wait queue head for send requests waiting on
	 * the association sndbuf space.
	 */
	wait_queue_head_t	wait;

	/* Association : The smallest PMTU discovered for all of the
	 * PMTU        : peer's transport addresses.
	 */
	__u32 pmtu;

	/* The message size at which SCTP fragmentation will occur. */
	__u32 frag_point;

	/* Ack State   : This flag indicates if the next received
	 *             : packet is to be responded to with a
	 *             : SACK. This is initializedto 0.  When a packet
	 *             : is received it is incremented. If this value
	 *             : reaches 2 or more, a SACK is sent and the
	 *             : value is reset to 0. Note: This is used only
	 *             : when no DATA chunks are received out of
	 *	       : order.  When DATA chunks are out of order,
	 *             : SACK's are not delayed (see Section 6).
	 */
	/* Do we need to send an ack?
	 * When counters[SctpCounterAckState] is above 1 we do!
	 */
	int counters[SCTP_NUMBER_COUNTERS];

	struct {
		__u16 stream;
		__u32 ppid;
	} defaults;

	/* This tracks outbound ssn for a given stream.  */
	__u16 ssn[SCTP_MAX_STREAM];

	/* All outbound chunks go through this structure.  */
	sctp_outqueue_t outqueue;

	/* A smart pipe that will handle reordering and fragmentation,
	 * as well as handle passing events up to the ULP.
	 * In the future, we should make this at least dynamic, if
	 * not also some sparse structure.
	 */
	sctp_ulpqueue_t ulpq;
	__u8 _ssnmap[sctp_ulpqueue_storage_size(SCTP_MAX_STREAM)];

	/* Need to send an ECNE Chunk? */
	int need_ecne;

	/* Last TSN that caused an ECNE Chunk to be sent.  */
	__u32 last_ecne_tsn;

	/* Last TSN that caused a CWR Chunk to be sent.  */
	__u32 last_cwr_tsn;

	/* How many duplicated TSNs have we seen?  */
	int numduptsns;

	/* Number of seconds of idle time before an association is closed.  */
	__u32 autoclose;

	/* Name for debugging output... */
	char *debug_name;

	/* These are to support
	 * "SCTP Extensions for Dynamic Reconfiguration of IP Addresses
	 *  and Enforcement of Flow and Message Limits"
	 * <draft-ietf-tsvwg-addip-sctp-02.txt>
	 * or "ADDIP" for short.
	 */

	/* Is the ADDIP extension enabled for this association? */
	int addip_enable;

	/* ADDIP Section 4.1.1 Congestion Control of ASCONF Chunks
	 *
	 * R1) One and only one ASCONF Chunk MAY be in transit and
	 * unacknowledged at any one time.  If a sender, after sending
	 * an ASCONF chunk, decides it needs to transfer another
	 * ASCONF Chunk, it MUST wait until the ASCONF-ACK Chunk
	 * returns from the previous ASCONF Chunk before sending a
	 * subsequent ASCONF. Note this restriction binds each side,
	 * so at any time two ASCONF may be in-transit on any given
	 * association (one sent from each endpoint).
	 *
	 * [This is our one-and-only-one ASCONF in flight.  If we do
	 * not have an ASCONF in flight, this is NULL.]
	 */
	sctp_chunk_t *addip_last_asconf;

	/* ADDIP Section 4.2 Upon reception of an ASCONF Chunk.
	 *
	 * IMPLEMENTATION NOTE: As an optimization a receiver may wish
	 * to save the last ASCONF-ACK for some predetermined period
	 * of time and instead of re-processing the ASCONF (with the
	 * same serial number) it may just re-transmit the
	 * ASCONF-ACK. It may wish to use the arrival of a new serial
	 * number to discard the previously saved ASCONF-ACK or any
	 * other means it may choose to expire the saved ASCONF-ACK.
	 *
	 * [This is our saved ASCONF-ACK.  We invalidate it when a new
	 * ASCONF serial number arrives.]
	 */
	sctp_chunk_t *addip_last_asconf_ack;

	/* These ASCONF chunks are waiting to be sent.
	 *
	 * These chunaks can't be pushed to outqueue until receiving
	 * ASCONF_ACK for the previous ASCONF indicated by
	 * addip_last_asconf, so as to guarantee that only one ASCONF
	 * is in flight at any time.
	 *
	 * ADDIP Section 4.1.1 Congestion Control of ASCONF Chunks
	 *
	 * In defining the ASCONF Chunk transfer procedures, it is
	 * essential that these transfers MUST NOT cause congestion
	 * within the network.  To achieve this, we place these
	 * restrictions on the transfer of ASCONF Chunks:
	 *
	 * R1) One and only one ASCONF Chunk MAY be in transit and
	 * unacknowledged at any one time.  If a sender, after sending
	 * an ASCONF chunk, decides it needs to transfer another
	 * ASCONF Chunk, it MUST wait until the ASCONF-ACK Chunk
	 * returns from the previous ASCONF Chunk before sending a
	 * subsequent ASCONF. Note this restriction binds each side,
	 * so at any time two ASCONF may be in-transit on any given
	 * association (one sent from each endpoint).
	 *
	 *
	 * [I really think this is EXACTLY the sort of intelligence
	 *  which already resides in SCTP_outqueue.  Please move this
	 *  queue and its supporting logic down there.  --piggy]
	 */
	struct sk_buff_head addip_chunks;

	/* ADDIP Section 4.1 ASCONF Chunk Procedures
	 *
	 * A2) A serial number should be assigned to the Chunk. The
	 * serial number should be a monotonically increasing
	 * number. All serial numbers are defined to be initialized at
	 * the start of the association to the same value as the
	 * Initial TSN.
	 *
	 * [and]
	 *
	 * ADDIP
	 * 3.1.1  Address/Stream Configuration Change Chunk (ASCONF)
	 *
	 * Serial Number : 32 bits (unsigned integer)
	 *
	 * This value represents a Serial Number for the ASCONF
	 * Chunk. The valid range of Serial Number is from 0 to
	 * 4294967295 (2**32 - 1).  Serial Numbers wrap back to 0
	 * after reaching 4294967295.
	 */
	__u32 addip_serial;
};


/* An eyecatcher for determining if we are really looking at an
 * association data structure.
 */
enum {
	SCTP_ASSOC_EYECATCHER = 0xa550c123,
};

/* Recover the outter association structure. */
static inline sctp_association_t *sctp_assoc(sctp_endpoint_common_t *base)
{
	sctp_association_t *asoc;

	/* We are not really a list, but the list_entry() macro is
	 * really quite generic find the address of an outter struct.
	 */
	asoc = list_entry(base, sctp_association_t, base);
	return asoc;
}

/* These are function signatures for manipulating associations.  */


sctp_association_t *
sctp_association_new(const sctp_endpoint_t *, const struct sock *,
		     sctp_scope_t scope, int priority);
sctp_association_t *
sctp_association_init(sctp_association_t *, const sctp_endpoint_t *,
		      const struct sock *, sctp_scope_t scope,
		      int priority);
void sctp_association_free(sctp_association_t *);
void sctp_association_put(sctp_association_t *);
void sctp_association_hold(sctp_association_t *);

sctp_transport_t *sctp_assoc_choose_shutdown_transport(sctp_association_t *);
sctp_transport_t *sctp_assoc_lookup_paddr(const sctp_association_t *,
					  const sockaddr_storage_t *);
sctp_transport_t *sctp_assoc_add_peer(sctp_association_t *,
				     const sockaddr_storage_t *address,
				     const int priority);
void sctp_assoc_control_transport(sctp_association_t *, sctp_transport_t *,
				  sctp_transport_cmd_t, sctp_sn_error_t);
sctp_transport_t *sctp_assoc_lookup_tsn(sctp_association_t *, __u32);
sctp_transport_t *sctp_assoc_is_match(sctp_association_t *,
				      const sockaddr_storage_t *,
				      const sockaddr_storage_t *);
void sctp_assoc_migrate(sctp_association_t *, struct sock *);
void sctp_assoc_update(sctp_association_t *dst, sctp_association_t *src);

__u32 __sctp_association_get_next_tsn(sctp_association_t *);
__u32 __sctp_association_get_tsn_block(sctp_association_t *, int);
__u16 __sctp_association_get_next_ssn(sctp_association_t *, __u16 sid);

int sctp_cmp_addr(const sockaddr_storage_t *ss1,
		  const sockaddr_storage_t *ss2);
int sctp_cmp_addr_exact(const sockaddr_storage_t *ss1,
		        const sockaddr_storage_t *ss2);
sctp_chunk_t *sctp_get_ecne_prepend(sctp_association_t *asoc);
sctp_chunk_t *sctp_get_no_prepend(sctp_association_t *asoc);


/* A convenience structure to parse out SCTP specific CMSGs. */
typedef struct sctp_cmsgs {
	struct sctp_initmsg *init;
	struct sctp_sndrcvinfo *info;
} sctp_cmsgs_t;

/* Structure for tracking memory objects */
typedef struct {
	char *label;
	atomic_t *counter;
} sctp_dbg_objcnt_entry_t;

#endif /* __sctp_structs_h__ */
