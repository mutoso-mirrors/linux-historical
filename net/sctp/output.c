/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 International Business Machines, Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * These functions handle output processing.
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
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Jon Grimm             <jgrimm@austin.ibm.com>
 *    Sridhar Samudrala     <sri@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/init.h>
#include <net/inet_ecn.h>
#include <net/icmp.h>

#ifndef TEST_FRAME
#include <net/tcp.h>
#endif /* TEST_FRAME (not defined) */

#include <linux/socket.h> /* for sa_family_t */
#include <net/sock.h>

#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* Forward declarations for private helpers. */
static void sctp_packet_reset(sctp_packet_t *packet);
static sctp_xmit_t sctp_packet_append_data(sctp_packet_t *packet,
					   sctp_chunk_t *chunk);

/* Config a packet.
 * This appears to be a followup set of initializations.)
 */
sctp_packet_t *sctp_packet_config(sctp_packet_t *packet,
				  __u32 vtag,
				  int ecn_capable,
				  sctp_packet_phandler_t *prepend_handler)
{
	int packet_empty = (packet->size == SCTP_IP_OVERHEAD);

	packet->vtag = vtag;
	packet->ecn_capable = ecn_capable;
	packet->get_prepend_chunk = prepend_handler;
	packet->has_cookie_echo = 0;
	packet->ipfragok = 0;

	/* We might need to call the prepend_handler right away.  */
	if (packet_empty)
		sctp_packet_reset(packet);
	return packet;
}

/* Initialize the packet structure. */
sctp_packet_t *sctp_packet_init(sctp_packet_t *packet,
				struct sctp_transport *transport,
				__u16 sport,
				__u16 dport)
{
	packet->transport = transport;
	packet->source_port = sport;
	packet->destination_port = dport;
	skb_queue_head_init(&packet->chunks);
	packet->vtag = 0;
	packet->ecn_capable = 0;
	packet->get_prepend_chunk = NULL;
	packet->has_cookie_echo = 0;
	packet->ipfragok = 0;
	packet->malloced = 0;
	sctp_packet_reset(packet);
	return packet;
}

/* Free a packet.  */
void sctp_packet_free(sctp_packet_t *packet)
{
	sctp_chunk_t *chunk;

        while (NULL != 
	       (chunk = (sctp_chunk_t *)skb_dequeue(&packet->chunks))) {
		sctp_free_chunk(chunk);
	}

	if (packet->malloced)
		kfree(packet);
}

/* This routine tries to append the chunk to the offered packet. If adding
 * the chunk causes the packet to exceed the path MTU and COOKIE_ECHO chunk
 * is not present in the packet, it transmits the input packet.
 * Data can be bundled with a packet containing a COOKIE_ECHO chunk as long
 * as it can fit in the packet, but any more data that does not fit in this
 * packet can be sent only after receiving the COOKIE_ACK.
 */
sctp_xmit_t sctp_packet_transmit_chunk(sctp_packet_t *packet,
				       sctp_chunk_t *chunk)
{
	sctp_xmit_t retval;
	int error = 0;

	switch ((retval = (sctp_packet_append_chunk(packet, chunk)))) {
	case SCTP_XMIT_PMTU_FULL:
		if (!packet->has_cookie_echo) {
			error = sctp_packet_transmit(packet);
			if (error < 0)
				chunk->skb->sk->err = -error;

			/* If we have an empty packet, then we can NOT ever
			 * return PMTU_FULL.
			 */
			retval = sctp_packet_append_chunk(packet, chunk);
		}
		break;

	case SCTP_XMIT_MUST_FRAG:
	case SCTP_XMIT_RWND_FULL:
	case SCTP_XMIT_OK:
		break;
	};

	return retval;
}

/* Append a chunk to the offered packet reporting back any inability to do
 * so.
 */
sctp_xmit_t sctp_packet_append_chunk(sctp_packet_t *packet, sctp_chunk_t *chunk)
{
	sctp_xmit_t retval = SCTP_XMIT_OK;
	__u16 chunk_len = WORD_ROUND(ntohs(chunk->chunk_hdr->length));
	size_t psize = packet->size;
	size_t pmtu;
	int too_big;

	pmtu  = ((packet->transport->asoc) ?
		 (packet->transport->asoc->pmtu) :
		 (packet->transport->pmtu));

	too_big = (psize + chunk_len > pmtu);

	/* Decide if we need to fragment or resubmit later. */
	if (too_big) {
		int packet_empty = (packet->size == SCTP_IP_OVERHEAD);

		/* Both control chunks and data chunks with TSNs are
		 * non-fragmentable.
		 */
		int fragmentable = sctp_chunk_is_data(chunk) && 
			(!chunk->has_tsn);
		if (packet_empty) {
			if (fragmentable) {
				retval = SCTP_XMIT_MUST_FRAG;
				goto finish;
			} else {
				/* The packet is too big but we can
				 * not fragment it--we have to just
				 * transmit and rely on IP
				 * fragmentation.
				 */
				packet->ipfragok = 1;
				goto append;
			}
		} else { /* !packet_empty */
			retval = SCTP_XMIT_PMTU_FULL;
			goto finish;
		}
	} else {
		/* The chunk fits in the packet.  */
		goto append;
	}

append:
	/* We believe that this chunk is OK to add to the packet (as
	 * long as we have the cwnd for it).
	 */

	/* DATA is a special case since we must examine both rwnd and cwnd
	 * before we send DATA.
	 */
	if (sctp_chunk_is_data(chunk)) {
		retval = sctp_packet_append_data(packet, chunk);
		if (SCTP_XMIT_OK != retval)
			goto finish;
	} else if (SCTP_CID_COOKIE_ECHO == chunk->chunk_hdr->type) {
		packet->has_cookie_echo = 1;
	}

	/* It is OK to send this chunk.  */
	skb_queue_tail(&packet->chunks, (struct sk_buff *)chunk);
	packet->size += chunk_len;
finish:
	return retval;
}

/* All packets are sent to the network through this function from
 * sctp_outq_tail().
 *
 * The return value is a normal kernel error return value.
 */
int sctp_packet_transmit(sctp_packet_t *packet)
{
	struct sctp_transport *transport = packet->transport;
	sctp_association_t *asoc = transport->asoc;
	struct sctphdr *sh;
	__u32 crc32;
	struct sk_buff *nskb;
	sctp_chunk_t *chunk;
	struct sock *sk;
	int err = 0;
	int padding;		/* How much padding do we need?  */
	__u8 packet_has_data = 0;
	struct dst_entry *dst;

	/* Do NOT generate a chunkless packet... */
	if (skb_queue_empty(&packet->chunks))
		return err;

	/* Set up convenience variables... */
	chunk = (sctp_chunk_t *) (packet->chunks.next);
	sk = chunk->skb->sk;

	/* Allocate the new skb.  */
	nskb = dev_alloc_skb(packet->size);
	if (!nskb) {
		err = -ENOMEM;
		goto out;
	}

	/* Make sure the outbound skb has enough header room reserved. */
	skb_reserve(nskb, SCTP_IP_OVERHEAD);

	/* Set the owning socket so that we know where to get the
	 * destination IP address.
	 */
	skb_set_owner_w(nskb, sk);

	/**
	 * 6.10 Bundling
	 *
	 *    An endpoint bundles chunks by simply including multiple
	 *    chunks in one outbound SCTP packet.  ...
	 */

	/**
	 * 3.2  Chunk Field Descriptions
	 *
	 * The total length of a chunk (including Type, Length and
	 * Value fields) MUST be a multiple of 4 bytes.  If the length
	 * of the chunk is not a multiple of 4 bytes, the sender MUST
	 * pad the chunk with all zero bytes and this padding is not
	 * included in the chunk length field.  The sender should
	 * never pad with more than 3 bytes.
	 *
	 * [This whole comment explains WORD_ROUND() below.]
	 */
	SCTP_DEBUG_PRINTK("***sctp_transmit_packet***\n");
	while (NULL != (chunk = (sctp_chunk_t *)
			skb_dequeue(&packet->chunks))) {
		chunk->num_times_sent++;
		chunk->sent_at = jiffies;
		if (sctp_chunk_is_data(chunk)) {
			sctp_chunk_assign_tsn(chunk);

			/* 6.3.1 C4) When data is in flight and when allowed
			 * by rule C5, a new RTT measurement MUST be made each
			 * round trip.  Furthermore, new RTT measurements
			 * SHOULD be made no more than once per round-trip
			 * for a given destination transport address.
			 */
			if ((1 == chunk->num_times_sent) &&
			    (!transport->rto_pending)) {
				chunk->rtt_in_progress = 1;
				transport->rto_pending = 1;
			}
			packet_has_data = 1;
		}
		memcpy(skb_put(nskb, chunk->skb->len),
		       chunk->skb->data, chunk->skb->len);
		padding = WORD_ROUND(chunk->skb->len) - chunk->skb->len;
		memset(skb_put(nskb, padding), 0, padding);
		SCTP_DEBUG_PRINTK("%s %p[%s] %s 0x%x, %s %d, %s %d, %s %d, "
				  "%s %d\n",
				  "*** Chunk", chunk,
				  sctp_cname(SCTP_ST_CHUNK(
					  chunk->chunk_hdr->type)),
				  chunk->has_tsn ? "TSN" : "No TSN",
				  chunk->has_tsn ?
				  ntohl(chunk->subh.data_hdr->tsn) : 0,
				  "length", ntohs(chunk->chunk_hdr->length),
				  "chunk->skb->len", chunk->skb->len,
				  "num_times_sent", chunk->num_times_sent,
				  "rtt_in_progress", chunk->rtt_in_progress);

		/*
		 * If this is a control chunk, this is our last
		 * reference. Free data chunks after they've been
		 * acknowledged or have failed.
		 */
		if (!sctp_chunk_is_data(chunk))
			sctp_free_chunk(chunk);
	}

	/* Build the SCTP header.  */
	sh = (struct sctphdr *)skb_push(nskb, sizeof(struct sctphdr));
	sh->source = htons(packet->source_port);
	sh->dest   = htons(packet->destination_port);

	/* From 6.8 Adler-32 Checksum Calculation:
	 * After the packet is constructed (containing the SCTP common
	 * header and one or more control or DATA chunks), the
	 * transmitter shall:
	 *
	 * 1) Fill in the proper Verification Tag in the SCTP common
	 *    header and initialize the checksum field to 0's.
	 */
	sh->vtag     = htonl(packet->vtag);
	sh->checksum = 0;

	/* 2) Calculate the Adler-32 checksum of the whole packet,
	 *    including the SCTP common header and all the
	 *    chunks.
	 *
	 * Note: Adler-32 is no longer applicable, as has been replaced
	 * by CRC32-C as described in <draft-ietf-tsvwg-sctpcsum-02.txt>.
	 */
	crc32 = sctp_start_cksum((__u8 *)sh, nskb->len);
	crc32 = sctp_end_cksum(crc32);

	/* 3) Put the resultant value into the checksum field in the
	 *    common header, and leave the rest of the bits unchanged.
	 */
	sh->checksum = htonl(crc32);

	/* FIXME:  Delete the rest of this switch statement once phase 2
	 * of address selection (ipv6 support) drops in.
	 */
	switch (transport->ipaddr.sa.sa_family) {
	case AF_INET6:
		SCTP_V6(inet6_sk(sk)->daddr = transport->ipaddr.v6.sin6_addr;)
		break;
	};

	/* IP layer ECN support
	 * From RFC 2481
	 *  "The ECN-Capable Transport (ECT) bit would be set by the
	 *   data sender to indicate that the end-points of the
	 *   transport protocol are ECN-capable."
	 *
	 * If ECN capable && negotiated && it makes sense for
	 * this packet to support it (e.g. post ECN negotiation)
	 * then lets set the ECT bit
	 *
	 * FIXME:  Need to do something else for IPv6
	 */
	if (packet->ecn_capable) {
		INET_ECN_xmit(nskb->sk);
	} else {
		INET_ECN_dontxmit(nskb->sk);
	}

	/* Set up the IP options.  */
	/* BUG: not implemented
	 * For v4 this all lives somewhere in sk->opt...
	 */

	/* Dump that on IP!  */
	if (asoc && asoc->peer.last_sent_to != transport) {
		/* Considering the multiple CPU scenario, this is a
		 * "correcter" place for last_sent_to.  --xguo
		 */
		asoc->peer.last_sent_to = transport;
	}

	if (packet_has_data) {
		struct timer_list *timer;
		unsigned long timeout;

		transport->last_time_used = jiffies;

		/* Restart the AUTOCLOSE timer when sending data. */
		if ((SCTP_STATE_ESTABLISHED == asoc->state) &&
		    (asoc->autoclose)) {
			timer = &asoc->timers[SCTP_EVENT_TIMEOUT_AUTOCLOSE];
			timeout = asoc->timeouts[SCTP_EVENT_TIMEOUT_AUTOCLOSE];

			if (!mod_timer(timer, jiffies + timeout))
				sctp_association_hold(asoc);
		}
	}

	dst = transport->dst;
	if (!dst || dst->obsolete) {
		sctp_transport_route(transport, NULL, sctp_sk(sk));
		sctp_assoc_sync_pmtu(asoc);
	}

	nskb->dst = dst_clone(transport->dst);
	if (!nskb->dst)
		goto no_route;

	SCTP_DEBUG_PRINTK("***sctp_transmit_packet*** skb length %d\n",
			  nskb->len);
	(*transport->af_specific->queue_xmit)(nskb, packet->ipfragok);
out:
	packet->size = SCTP_IP_OVERHEAD;
	return err;
no_route:
	kfree_skb(nskb);
	IP_INC_STATS_BH(IpOutNoRoutes);
	err = -EHOSTUNREACH;
	goto out;
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

/*
 * This private function resets the packet to a fresh state.
 */
static void sctp_packet_reset(sctp_packet_t *packet)
{
	sctp_chunk_t *chunk = NULL;

	packet->size = SCTP_IP_OVERHEAD;

	if (packet->get_prepend_chunk)
		chunk = packet->get_prepend_chunk(packet->transport->asoc);

	/* If there a is a prepend chunk stick it on the list before
	 * any other chunks get appended.
	 */
	if (chunk)
		sctp_packet_append_chunk(packet, chunk);
}

/* This private function handles the specifics of appending DATA chunks.  */
static sctp_xmit_t sctp_packet_append_data(sctp_packet_t *packet, 
					   sctp_chunk_t *chunk)
{
	sctp_xmit_t retval = SCTP_XMIT_OK;
	size_t datasize, rwnd, inflight;
	struct sctp_transport *transport = packet->transport;
	__u32 max_burst_bytes;

	/* RFC 2960 6.1  Transmission of DATA Chunks
	 *
	 * A) At any given time, the data sender MUST NOT transmit new data to
	 * any destination transport address if its peer's rwnd indicates
	 * that the peer has no buffer space (i.e. rwnd is 0, see Section
	 * 6.2.1).  However, regardless of the value of rwnd (including if it
	 * is 0), the data sender can always have one DATA chunk in flight to
	 * the receiver if allowed by cwnd (see rule B below).  This rule
	 * allows the sender to probe for a change in rwnd that the sender
	 * missed due to the SACK having been lost in transit from the data
	 * receiver to the data sender.
	 */

	rwnd = transport->asoc->peer.rwnd;
	inflight = transport->asoc->outqueue.outstanding_bytes;

	datasize = sctp_data_size(chunk);

	if (datasize > rwnd) {
		if (inflight > 0) {
			/* We have (at least) one data chunk in flight,
			 * so we can't fall back to rule 6.1 B).
			 */
			retval = SCTP_XMIT_RWND_FULL;
			goto finish;
		}
	}

	/* sctpimpguide-05 2.14.2
	 * D) When the time comes for the sender to
	 * transmit new DATA chunks, the protocol parameter Max.Burst MUST
	 * first be applied to limit how many new DATA chunks may be sent.
	 * The limit is applied by adjusting cwnd as follows:
	 * 	if ((flightsize + Max.Burst * MTU) < cwnd)
	 *		cwnd = flightsize + Max.Burst * MTU
	 */
	max_burst_bytes = transport->asoc->max_burst * transport->asoc->pmtu;
	if ((transport->flight_size + max_burst_bytes) < transport->cwnd) {
		transport->cwnd = transport->flight_size + max_burst_bytes;
		SCTP_DEBUG_PRINTK("%s: cwnd limited by max_burst: "
				  "transport: %p, cwnd: %d, "
				  "ssthresh: %d, flight_size: %d, "
				  "pba: %d\n",
				  __FUNCTION__, transport,
				  transport->cwnd,
				  transport->ssthresh,
				  transport->flight_size,
				  transport->partial_bytes_acked);
	}

	/* RFC 2960 6.1  Transmission of DATA Chunks
	 *
	 * B) At any given time, the sender MUST NOT transmit new data
	 * to a given transport address if it has cwnd or more bytes
	 * of data outstanding to that transport address.
	 */
	/* RFC 7.2.4 & the Implementers Guide 2.8.
	 *
	 * 3) ...
	 *    When a Fast Retransmit is being performed the sender SHOULD
	 *    ignore the value of cwnd and SHOULD NOT delay retransmission.
	 */
	if (!chunk->fast_retransmit) {
		if (transport->flight_size >= transport->cwnd) {
			retval = SCTP_XMIT_RWND_FULL;
			goto finish;
		}
	}

	/* Keep track of how many bytes are in flight over this transport. */
	transport->flight_size += datasize;

	/* Keep track of how many bytes are in flight to the receiver. */
	transport->asoc->outqueue.outstanding_bytes += datasize;

	/* Update our view of the receiver's rwnd. */
	if (datasize < rwnd) {
		rwnd -= datasize;
	} else {
		rwnd = 0;
	}

	transport->asoc->peer.rwnd = rwnd;

finish:
	return retval;
}
