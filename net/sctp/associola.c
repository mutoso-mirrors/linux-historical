/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 International Business Machines Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * This module provides the abstraction for an SCTP association.
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
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Xingang Guo           <xingang.guo@intel.com>
 *    Hui Huang             <hui.huang@nokia.com>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/sched.h>

#include <linux/slab.h>
#include <linux/in.h>
#include <net/ipv6.h>
#include <net/sctp/sctp.h>

/* Forward declarations for internal functions. */
static void sctp_assoc_bh_rcv(sctp_association_t *asoc);


/* 1st Level Abstractions. */

/* Allocate and initialize a new association */
sctp_association_t *sctp_association_new(const sctp_endpoint_t *ep,
					 const struct sock *sk,
					 sctp_scope_t scope, int priority)
{
	sctp_association_t *asoc;

	asoc = t_new(sctp_association_t, priority);
	if (!asoc)
		goto fail;

	if (!sctp_association_init(asoc, ep, sk, scope, priority))
		goto fail_init;

	asoc->base.malloced = 1;
	SCTP_DBG_OBJCNT_INC(assoc);

	return asoc;

fail_init:
	kfree(asoc);
fail:
	return NULL;
}

/* Intialize a new association from provided memory. */
sctp_association_t *sctp_association_init(sctp_association_t *asoc,
					  const sctp_endpoint_t *ep,
					  const struct sock *sk,
					  sctp_scope_t scope,
					  int priority)
{
	struct sctp_opt *sp;
	int i;

	/* Retrieve the SCTP per socket area.  */
	sp = sctp_sk((struct sock *)sk);

	/* Init all variables to a known value.  */
	memset(asoc, 0, sizeof(sctp_association_t));

	/* Discarding const is appropriate here.  */
	asoc->ep = (sctp_endpoint_t *)ep;
	sctp_endpoint_hold(asoc->ep);

	/* Hold the sock.  */
	asoc->base.sk = (struct sock *)sk;
	sock_hold(asoc->base.sk);

	/* Initialize the common base substructure.  */
	asoc->base.type = SCTP_EP_TYPE_ASSOCIATION;

	/* Initialize the object handling fields.  */
	atomic_set(&asoc->base.refcnt, 1);
	asoc->base.dead = 0;
	asoc->base.malloced = 0;

	/* Initialize the bind addr area.  */
	sctp_bind_addr_init(&asoc->base.bind_addr, ep->base.bind_addr.port);
	asoc->base.addr_lock = RW_LOCK_UNLOCKED;

	asoc->state = SCTP_STATE_CLOSED;
	asoc->state_timestamp = jiffies;

	/* Set things that have constant value.  */
	asoc->cookie_life.tv_sec = sctp_proto.valid_cookie_life / HZ;
	asoc->cookie_life.tv_usec = (sctp_proto.valid_cookie_life % HZ) *
					1000000L / HZ;

	asoc->pmtu = 0;
	asoc->frag_point = 0;

	/* Initialize the default association max_retrans and RTO values.  */
	asoc->max_retrans = ep->proto->max_retrans_association;
	asoc->rto_initial = ep->proto->rto_initial;
	asoc->rto_max = ep->proto->rto_max;
	asoc->rto_min = ep->proto->rto_min;

	asoc->overall_error_threshold = 0;
	asoc->overall_error_count = 0;

	/* Initialize the maximum mumber of new data packets that can be sent
	 * in a burst.
	 */
	asoc->max_burst = ep->proto->max_burst;

	/* Copy things from the endpoint.  */
	for (i = SCTP_EVENT_TIMEOUT_NONE; i < SCTP_NUM_TIMEOUT_TYPES; ++i) {
		asoc->timeouts[i] = ep->timeouts[i];
		init_timer(&asoc->timers[i]);
		asoc->timers[i].function = sctp_timer_events[i];
		asoc->timers[i].data = (unsigned long) asoc;
	}

	/* Pull default initialization values from the sock options.
	 * Note: This assumes that the values have already been
	 * validated in the sock.
	 */
	asoc->c.sinit_max_instreams = sp->initmsg.sinit_max_instreams;
	asoc->c.sinit_num_ostreams  = sp->initmsg.sinit_num_ostreams;
	asoc->max_init_attempts	= sp->initmsg.sinit_max_attempts;
	asoc->max_init_timeo    = sp->initmsg.sinit_max_init_timeo * HZ;

	/* Allocate storage for the ssnmap after the inbound and outbound
	 * streams have been negotiated during Init.
	 */
	asoc->ssnmap = NULL;

	/* Set the local window size for receive.
	 * This is also the rcvbuf space per association.
	 * RFC 6 - A SCTP receiver MUST be able to receive a minimum of
	 * 1500 bytes in one SCTP packet.
	 */
	if (sk->rcvbuf < SCTP_DEFAULT_MINWINDOW)
		asoc->rwnd = SCTP_DEFAULT_MINWINDOW;
	else
		asoc->rwnd = sk->rcvbuf;

	asoc->a_rwnd = 0;

	asoc->rwnd_over = 0;

	/* Use my own max window until I learn something better.  */
	asoc->peer.rwnd = SCTP_DEFAULT_MAXWINDOW;

	/* Set the sndbuf size for transmit.  */
	asoc->sndbuf_used = 0;

	init_waitqueue_head(&asoc->wait);

	asoc->c.my_vtag = sctp_generate_tag(ep);
	asoc->peer.i.init_tag = 0;     /* INIT needs a vtag of 0. */
	asoc->c.peer_vtag = 0;
	asoc->c.my_ttag   = 0;
	asoc->c.peer_ttag = 0;

	asoc->c.initial_tsn = sctp_generate_tsn(ep);

	asoc->next_tsn = asoc->c.initial_tsn;

	asoc->ctsn_ack_point = asoc->next_tsn - 1;
	asoc->highest_sacked = asoc->ctsn_ack_point;
	asoc->last_cwr_tsn = asoc->ctsn_ack_point;
	asoc->unack_data = 0;

	SCTP_DEBUG_PRINTK("myctsnap for %s INIT as 0x%x.\n",
			  asoc->ep->debug_name,
			  asoc->ctsn_ack_point);

	/* ADDIP Section 4.1 Asconf Chunk Procedures
	 *
	 * When an endpoint has an ASCONF signaled change to be sent to the
	 * remote endpoint it should do the following:
	 * ...
	 * A2) a serial number should be assigned to the chunk. The serial
	 * number should be a monotonically increasing number. All serial
	 * numbers are defined to be initialized at the start of the
	 * association to the same value as the initial TSN.
	 */
	asoc->addip_serial = asoc->c.initial_tsn;

	/* Make an empty list of remote transport addresses.  */
	INIT_LIST_HEAD(&asoc->peer.transport_addr_list);

	/* RFC 2960 5.1 Normal Establishment of an Association
	 *
	 * After the reception of the first data chunk in an
	 * association the endpoint must immediately respond with a
	 * sack to acknowledge the data chunk.  Subsequent
	 * acknowledgements should be done as described in Section
	 * 6.2.
	 *
	 * [We implement this by telling a new association that it
	 * already received one packet.]
	 */
	asoc->peer.sack_needed = 1;

	/* Create an input queue.  */
	sctp_inq_init(&asoc->base.inqueue);
	sctp_inq_set_th_handler(&asoc->base.inqueue,
				    (void (*)(void *))sctp_assoc_bh_rcv,
				    asoc);

	/* Create an output queue.  */
	sctp_outq_init(asoc, &asoc->outqueue);
	sctp_outq_set_output_handlers(&asoc->outqueue,
				      sctp_packet_init,
				      sctp_packet_config,
				      sctp_packet_append_chunk,
				      sctp_packet_transmit_chunk,
				      sctp_packet_transmit);

	if (NULL == sctp_ulpq_init(&asoc->ulpq, asoc))
		goto fail_init;

	/* Set up the tsn tracking. */
	sctp_tsnmap_init(&asoc->peer.tsn_map, SCTP_TSN_MAP_SIZE, 0);

	skb_queue_head_init(&asoc->addip_chunks);

	asoc->need_ecne = 0;

	asoc->debug_name = "unnamedasoc";
	asoc->eyecatcher = SCTP_ASSOC_EYECATCHER;

	/* Assume that peer would support both address types unless we are
	 * told otherwise.
	 */
	asoc->peer.ipv4_address = 1;
	asoc->peer.ipv6_address = 1;
	INIT_LIST_HEAD(&asoc->asocs);

	asoc->autoclose = sp->autoclose;

	return asoc;

fail_init:
	sctp_endpoint_put(asoc->ep);
	sock_put(asoc->base.sk);
	return NULL;
}

/* Free this association if possible.  There may still be users, so
 * the actual deallocation may be delayed.
 */
void sctp_association_free(sctp_association_t *asoc)
{
	struct sctp_transport *transport;
	sctp_endpoint_t *ep;
	struct list_head *pos, *temp;
	int i;

	ep = asoc->ep;
	list_del(&asoc->asocs);

	/* Mark as dead, so other users can know this structure is
	 * going away.
	 */
	asoc->base.dead = 1;

	/* Dispose of any data lying around in the outqueue. */
	sctp_outq_free(&asoc->outqueue);

	/* Dispose of any pending messages for the upper layer. */
	sctp_ulpq_free(&asoc->ulpq);

	/* Dispose of any pending chunks on the inqueue. */
	sctp_inq_free(&asoc->base.inqueue);

	/* Free ssnmap storage. */
	sctp_ssnmap_free(asoc->ssnmap);

	/* Clean up the bound address list. */
	sctp_bind_addr_free(&asoc->base.bind_addr);

	/* Do we need to go through all of our timers and
	 * delete them?   To be safe we will try to delete all, but we
	 * should be able to go through and make a guess based
	 * on our state.
	 */
	for (i = SCTP_EVENT_TIMEOUT_NONE; i < SCTP_NUM_TIMEOUT_TYPES; ++i) {
		if (timer_pending(&asoc->timers[i]) &&
		    del_timer(&asoc->timers[i]))
			sctp_association_put(asoc);
	}

	/* Free peer's cached cookie. */
	if (asoc->peer.cookie) {
		kfree(asoc->peer.cookie);
	}

	/* Release the transport structures. */
	list_for_each_safe(pos, temp, &asoc->peer.transport_addr_list) {
		transport = list_entry(pos, struct sctp_transport, transports);
		list_del(pos);
		sctp_transport_free(transport);
	}

	asoc->eyecatcher = 0;

	sctp_association_put(asoc);
}

/* Cleanup and free up an association. */
static void sctp_association_destroy(sctp_association_t *asoc)
{
	SCTP_ASSERT(asoc->base.dead, "Assoc is not dead", return);

	sctp_endpoint_put(asoc->ep);
	sock_put(asoc->base.sk);

	if (asoc->base.malloced) {
		kfree(asoc);
		SCTP_DBG_OBJCNT_DEC(assoc);
	}
}


/* Add a transport address to an association.  */
struct sctp_transport *sctp_assoc_add_peer(sctp_association_t *asoc,
					   const union sctp_addr *addr,
					   int priority)
{
	struct sctp_transport *peer;
	struct sctp_opt *sp;
	unsigned short port;

	/* AF_INET and AF_INET6 share common port field. */
	port = addr->v4.sin_port;

	/* Set the port if it has not been set yet.  */
	if (0 == asoc->peer.port) {
		asoc->peer.port = port;
	}

	/* Check to see if this is a duplicate. */
	peer = sctp_assoc_lookup_paddr(asoc, addr);
	if (peer)
		return peer;

	peer = sctp_transport_new(addr, priority);
	if (!peer)
		return NULL;

	sctp_transport_set_owner(peer, asoc);

	/* Initialize the pmtu of the transport. */
	sctp_transport_pmtu(peer);

	/* If this is the first transport addr on this association,
	 * initialize the association PMTU to the peer's PMTU.
	 * If not and the current association PMTU is higher than the new
	 * peer's PMTU, reset the association PMTU to the new peer's PMTU.
	 */
	if (asoc->pmtu) {
		asoc->pmtu = min_t(int, peer->pmtu, asoc->pmtu);
	} else {
		asoc->pmtu = peer->pmtu;
	}

	SCTP_DEBUG_PRINTK("sctp_assoc_add_peer:association %p PMTU set to "
			  "%d\n", asoc, asoc->pmtu);

	asoc->frag_point = asoc->pmtu -
		(SCTP_IP_OVERHEAD + sizeof(sctp_data_chunk_t));

	/* The asoc->peer.port might not be meaningful yet, but
	 * initialize the packet structure anyway.
	 */
	(asoc->outqueue.init_output)(&peer->packet,
				     peer,
				     asoc->base.bind_addr.port,
				     asoc->peer.port);

	/* 7.2.1 Slow-Start
	 *
	 * o The initial cwnd before data transmission or after a
	 *   sufficiently long idle period MUST be <= 2*MTU.
	 *
	 * o The initial value of ssthresh MAY be arbitrarily high
	 *   (for example, implementations MAY use the size of the
	 *   receiver advertised window).
	 */
	peer->cwnd = asoc->pmtu * 2;

	/* At this point, we may not have the receiver's advertised window,
	 * so initialize ssthresh to the default value and it will be set
	 * later when we process the INIT.
	 */
	peer->ssthresh = SCTP_DEFAULT_MAXWINDOW;

	peer->partial_bytes_acked = 0;
	peer->flight_size = 0;

	peer->error_threshold = peer->max_retrans;

	/* Update the overall error threshold value of the association
	 * taking the new peer's error threshold into account.
	 */
	asoc->overall_error_threshold =
		min(asoc->overall_error_threshold + peer->error_threshold,
		    asoc->max_retrans);

	/* By default, enable heartbeat for peer address. */
	peer->hb_allowed = 1;

	/* Initialize the peer's heartbeat interval based on the
	 * sock configured value.
	 */
	sp = sctp_sk(asoc->base.sk);
	peer->hb_interval = sp->paddrparam.spp_hbinterval * HZ;

	/* Attach the remote transport to our asoc.  */
	list_add_tail(&peer->transports, &asoc->peer.transport_addr_list);

	/* If we do not yet have a primary path, set one.  */
	if (NULL == asoc->peer.primary_path) {
		asoc->peer.primary_path = peer;
		/* Set a default msg_name for events. */
		memcpy(&asoc->peer.primary_addr, &peer->ipaddr,
		       sizeof(union sctp_addr));
		asoc->peer.active_path = peer;
		asoc->peer.retran_path = peer;
	}

	if (asoc->peer.active_path == asoc->peer.retran_path)
		asoc->peer.retran_path = peer;

	return peer;
}

/* Lookup a transport by address. */
struct sctp_transport *sctp_assoc_lookup_paddr(const sctp_association_t *asoc,
					  const union sctp_addr *address)
{
	struct sctp_transport *t;
	struct list_head *pos;

	/* Cycle through all transports searching for a peer address. */

	list_for_each(pos, &asoc->peer.transport_addr_list) {
		t = list_entry(pos, struct sctp_transport, transports);
		if (sctp_cmp_addr_exact(address, &t->ipaddr))
			return t;
	}

	return NULL;
}

/* Engage in transport control operations.
 * Mark the transport up or down and send a notification to the user.
 * Select and update the new active and retran paths.
 */
void sctp_assoc_control_transport(sctp_association_t *asoc,
				  struct sctp_transport *transport,
				  sctp_transport_cmd_t command,
				  sctp_sn_error_t error)
{
	struct sctp_transport *t = NULL;
	struct sctp_transport *first;
	struct sctp_transport *second;
	struct sctp_ulpevent *event;
	struct list_head *pos;
	int spc_state = 0;

	/* Record the transition on the transport.  */
	switch (command) {
	case SCTP_TRANSPORT_UP:
		transport->active = 1;
		spc_state = ADDRESS_AVAILABLE;
		break;

	case SCTP_TRANSPORT_DOWN:
		transport->active = 0;
		spc_state = ADDRESS_UNREACHABLE;
		break;

	default:
		return;
	};

	/* Generate and send a SCTP_PEER_ADDR_CHANGE notification to the
	 * user.
	 */
	event = sctp_ulpevent_make_peer_addr_change(asoc,
				(struct sockaddr_storage *) &transport->ipaddr,
				0, spc_state, error, GFP_ATOMIC);
	if (event)
		sctp_ulpq_tail_event(&asoc->ulpq, event);

	/* Select new active and retran paths. */

	/* Look for the two most recently used active transports.
	 *
	 * This code produces the wrong ordering whenever jiffies
	 * rolls over, but we still get usable transports, so we don't
	 * worry about it.
	 */
	first = NULL; second = NULL;

	list_for_each(pos, &asoc->peer.transport_addr_list) {
		t = list_entry(pos, struct sctp_transport, transports);

		if (!t->active)
			continue;
		if (!first || t->last_time_heard > first->last_time_heard) {
			second = first;
			first = t;
		}
		if (!second || t->last_time_heard > second->last_time_heard)
			second = t;
	}

	/* RFC 2960 6.4 Multi-Homed SCTP Endpoints
	 *
	 * By default, an endpoint should always transmit to the
	 * primary path, unless the SCTP user explicitly specifies the
	 * destination transport address (and possibly source
	 * transport address) to use.
	 *
	 * [If the primary is active but not most recent, bump the most
	 * recently used transport.]
	 */
	if (asoc->peer.primary_path->active &&
	    first != asoc->peer.primary_path) {
		second = first;
		first = asoc->peer.primary_path;
	}

	/* If we failed to find a usable transport, just camp on the
	 * primary, even if it is inactive.
	 */
	if (NULL == first) {
		first = asoc->peer.primary_path;
		second = asoc->peer.primary_path;
	}

	/* Set the active and retran transports.  */
	asoc->peer.active_path = first;
	asoc->peer.retran_path = second;
}

/* Hold a reference to an association. */
void sctp_association_hold(sctp_association_t *asoc)
{
	atomic_inc(&asoc->base.refcnt);
}

/* Release a reference to an association and cleanup
 * if there are no more references.
 */
void sctp_association_put(sctp_association_t *asoc)
{
	if (atomic_dec_and_test(&asoc->base.refcnt))
		sctp_association_destroy(asoc);
}

/* Allocate the next TSN, Transmission Sequence Number, for the given
 * association.
 */
__u32 __sctp_association_get_next_tsn(sctp_association_t *asoc)
{
	/* From Section 1.6 Serial Number Arithmetic:
	 * Transmission Sequence Numbers wrap around when they reach
	 * 2**32 - 1.  That is, the next TSN a DATA chunk MUST use
	 * after transmitting TSN = 2*32 - 1 is TSN = 0.
	 */
	__u32 retval = asoc->next_tsn;
	asoc->next_tsn++;
	asoc->unack_data++;

	return retval;
}

/* Allocate 'num' TSNs by incrementing the association's TSN by num. */
__u32 __sctp_association_get_tsn_block(sctp_association_t *asoc, int num)
{
	__u32 retval = asoc->next_tsn;

	asoc->next_tsn += num;
	asoc->unack_data += num;

	return retval;
}


/* Compare two addresses to see if they match.  Wildcard addresses
 * only match themselves.
 *
 * FIXME: We do not match address scopes correctly.
 */
int sctp_cmp_addr_exact(const union sctp_addr *ss1,
			const union sctp_addr *ss2)
{
	struct sctp_af *af;

	af = sctp_get_af_specific(ss1->sa.sa_family);
	if (!af)
		return 0;

	return af->cmp_addr(ss1, ss2);
}

/* Return an ecne chunk to get prepended to a packet.
 * Note:  We are sly and return a shared, prealloced chunk.
 */
sctp_chunk_t *sctp_get_ecne_prepend(sctp_association_t *asoc)
{
	sctp_chunk_t *chunk;
	int need_ecne;
	__u32 lowest_tsn;

	/* Can be called from task or bh.   Both need_ecne and
	 * last_ecne_tsn are written during bh.
	 */
	need_ecne = asoc->need_ecne;
	lowest_tsn = asoc->last_ecne_tsn;

	if (need_ecne) {
		chunk = sctp_make_ecne(asoc, lowest_tsn);

		/* ECNE is not mandatory to the flow.  Being unable to
		 * alloc mem is not deadly.  We are just unable to help
		 * out the network.  If we run out of memory, just return
		 * NULL.
		 */
	} else {
		chunk = NULL;
	}

	return chunk;
}

/* Use this function for the packet prepend callback when no ECNE
 * packet is desired (e.g. some packets don't like to be bundled).
 */
sctp_chunk_t *sctp_get_no_prepend(sctp_association_t *asoc)
{
	return NULL;
}

/*
 * Find which transport this TSN was sent on.
 */
struct sctp_transport *sctp_assoc_lookup_tsn(sctp_association_t *asoc, __u32 tsn)
{
	struct sctp_transport *active;
	struct sctp_transport *match;
	struct list_head *entry, *pos;
	struct sctp_transport *transport;
	sctp_chunk_t *chunk;
	__u32 key = htonl(tsn);

	match = NULL;

	/*
	 * FIXME: In general, find a more efficient data structure for
	 * searching.
	 */

	/*
	 * The general strategy is to search each transport's transmitted
	 * list.   Return which transport this TSN lives on.
	 *
	 * Let's be hopeful and check the active_path first.
	 * Another optimization would be to know if there is only one
	 * outbound path and not have to look for the TSN at all.
	 *
	 */

	active = asoc->peer.active_path;

	list_for_each(entry, &active->transmitted) {
		chunk = list_entry(entry, sctp_chunk_t, transmitted_list);

		if (key == chunk->subh.data_hdr->tsn) {
			match = active;
			goto out;
		}
	}

	/* If not found, go search all the other transports. */
	list_for_each(pos, &asoc->peer.transport_addr_list) {
		transport = list_entry(pos, struct sctp_transport, transports);

		if (transport == active)
			break;
		list_for_each(entry, &transport->transmitted) {
			chunk = list_entry(entry, sctp_chunk_t,
					   transmitted_list);
			if (key == chunk->subh.data_hdr->tsn) {
				match = transport;
				goto out;
			}
		}
	}
out:
	return match;
}

/* Is this the association we are looking for? */
struct sctp_transport *sctp_assoc_is_match(sctp_association_t *asoc,
					   const union sctp_addr *laddr,
					   const union sctp_addr *paddr)
{
	struct sctp_transport *transport;

	sctp_read_lock(&asoc->base.addr_lock);

	if ((asoc->base.bind_addr.port == laddr->v4.sin_port) &&
	    (asoc->peer.port == paddr->v4.sin_port)) {
		transport = sctp_assoc_lookup_paddr(asoc, paddr);
		if (!transport)
			goto out;

		if (sctp_bind_addr_match(&asoc->base.bind_addr, laddr,
					 sctp_sk(asoc->base.sk)))
			goto out;
	}
	transport = NULL;

out:
	sctp_read_unlock(&asoc->base.addr_lock);
	return transport;
}

/* Do delayed input processing.  This is scheduled by sctp_rcv(). */
static void sctp_assoc_bh_rcv(sctp_association_t *asoc)
{
	sctp_endpoint_t *ep;
	sctp_chunk_t *chunk;
	struct sock *sk;
	struct sctp_inq *inqueue;
	int state, subtype;
	sctp_assoc_t associd = sctp_assoc2id(asoc);
	int error = 0;

	/* The association should be held so we should be safe. */
	ep = asoc->ep;
	sk = asoc->base.sk;

	inqueue = &asoc->base.inqueue;
	while (NULL != (chunk = sctp_inq_pop(inqueue))) {
		state = asoc->state;
		subtype = chunk->chunk_hdr->type;

		/* Remember where the last DATA chunk came from so we
		 * know where to send the SACK.
		 */
		if (sctp_chunk_is_data(chunk))
			asoc->peer.last_data_from = chunk->transport;

		if (chunk->transport)
			chunk->transport->last_time_heard = jiffies;

		/* Run through the state machine. */
		error = sctp_do_sm(SCTP_EVENT_T_CHUNK, SCTP_ST_CHUNK(subtype),
				   state, ep, asoc, chunk, GFP_ATOMIC);

		/* Check to see if the association is freed in response to
		 * the incoming chunk.  If so, get out of the while loop.
		 */
		if (!sctp_id2assoc(sk, associd))
			break;

		/* If there is an error on chunk, discard this packet. */
		if (error && chunk)
			chunk->pdiscard = 1;
	}

}

/* This routine moves an association from its old sk to a new sk.  */
void sctp_assoc_migrate(sctp_association_t *assoc, struct sock *newsk)
{
	struct sctp_opt *newsp = sctp_sk(newsk);

	/* Delete the association from the old endpoint's list of
	 * associations.
	 */
	list_del(&assoc->asocs);

	/* Release references to the old endpoint and the sock.  */
	sctp_endpoint_put(assoc->ep);
	sock_put(assoc->base.sk);

	/* Get a reference to the new endpoint.  */
	assoc->ep = newsp->ep;
	sctp_endpoint_hold(assoc->ep);

	/* Get a reference to the new sock.  */
	assoc->base.sk = newsk;
	sock_hold(assoc->base.sk);

	/* Add the association to the new endpoint's list of associations.  */
	sctp_endpoint_add_asoc(newsp->ep, assoc);
}

/* Update an association (possibly from unexpected COOKIE-ECHO processing).  */
void sctp_assoc_update(sctp_association_t *asoc, sctp_association_t *new)
{
	/* Copy in new parameters of peer. */
	asoc->c = new->c;
	asoc->peer.rwnd = new->peer.rwnd;
	asoc->peer.sack_needed = new->peer.sack_needed;
	asoc->peer.i = new->peer.i;
	sctp_tsnmap_init(&asoc->peer.tsn_map, SCTP_TSN_MAP_SIZE,
			 asoc->peer.i.initial_tsn);

	/* FIXME:
	 *    Do we need to copy primary_path etc?
	 *
	 *    More explicitly, addresses may have been removed and
	 *    this needs accounting for.
	 */

	/* If the case is A (association restart), use
	 * initial_tsn as next_tsn. If the case is B, use
	 * current next_tsn in case data sent to peer
	 * has been discarded and needs retransmission.
	 */
	if (SCTP_STATE_ESTABLISHED == asoc->state) {

		asoc->next_tsn = new->next_tsn;
		asoc->ctsn_ack_point = new->ctsn_ack_point;

		/* Reinitialize SSN for both local streams
		 * and peer's streams.
		 */
		sctp_ssnmap_clear(asoc->ssnmap);

	} else {
		asoc->ctsn_ack_point = asoc->next_tsn - 1;
		if (!asoc->ssnmap) {
			/* Move the ssnmap. */
			asoc->ssnmap = new->ssnmap;
			new->ssnmap = NULL;
		}
	}

}

/* Choose the transport for sending a shutdown packet.
 * Round-robin through the active transports, else round-robin
 * through the inactive transports as this is the next best thing
 * we can try.
 */
struct sctp_transport *sctp_assoc_choose_shutdown_transport(sctp_association_t *asoc)
{
	struct sctp_transport *t, *next;
	struct list_head *head = &asoc->peer.transport_addr_list;
	struct list_head *pos;

	/* If this is the first time SHUTDOWN is sent, use the active
	 * path.
	 */
	if (!asoc->shutdown_last_sent_to)
		return asoc->peer.active_path;

	/* Otherwise, find the next transport in a round-robin fashion. */

	t = asoc->shutdown_last_sent_to;
	pos = &t->transports;
	next = NULL;

	while (1) {
		/* Skip the head. */
		if (pos->next == head)
			pos = head->next;
		else
			pos = pos->next;

		t = list_entry(pos, struct sctp_transport, transports);

		/* Try to find an active transport. */

		if (t->active) {
			break;
		} else {
			/* Keep track of the next transport in case
			 * we don't find any active transport.
			 */
			if (!next)
				next = t;
		}

		/* We have exhausted the list, but didn't find any
		 * other active transports.  If so, use the next
		 * transport.
		 */
		if (t == asoc->shutdown_last_sent_to) {
			t = next;
			break;
		}
	}

	return t;
}

/* Update the association's pmtu and frag_point by going through all the
 * transports. This routine is called when a transport's PMTU has changed.
 */
void sctp_assoc_sync_pmtu(sctp_association_t *asoc)
{
	struct sctp_transport *t;
	struct list_head *pos;
	__u32 pmtu = 0;

	if (!asoc)
		return;

	/* Get the lowest pmtu of all the transports. */
	list_for_each(pos, &asoc->peer.transport_addr_list) {
		t = list_entry(pos, struct sctp_transport, transports);
		if (!pmtu || (t->pmtu < pmtu))
			pmtu = t->pmtu;
	}

	if (pmtu) {
		asoc->pmtu = pmtu;
		asoc->frag_point = pmtu - (SCTP_IP_OVERHEAD +
					   sizeof(sctp_data_chunk_t));
	}

	SCTP_DEBUG_PRINTK("%s: asoc:%p, pmtu:%d, frag_point:%d\n",
			  __FUNCTION__, asoc, asoc->pmtu, asoc->frag_point);
}

/* Increase asoc's rwnd by len and send any window update SACK if needed. */
void sctp_assoc_rwnd_increase(sctp_association_t *asoc, int len)
{
	sctp_chunk_t *sack;
	struct timer_list *timer;

	if (asoc->rwnd_over) {
		if (asoc->rwnd_over >= len) {
			asoc->rwnd_over -= len;
		} else {
			asoc->rwnd += (len - asoc->rwnd_over);
			asoc->rwnd_over = 0;
		}
	} else {
		asoc->rwnd += len;
	}

	SCTP_DEBUG_PRINTK("%s: asoc %p rwnd increased by %d to (%u, %u) "
			  "- %u\n", __FUNCTION__, asoc, len, asoc->rwnd,
			  asoc->rwnd_over, asoc->a_rwnd);

	/* Send a window update SACK if the rwnd has increased by at least the
	 * minimum of the association's PMTU and half of the receive buffer.
	 * The algorithm used is similar to the one described in
	 * Section 4.2.3.3 of RFC 1122.
	 */
	if ((asoc->state == SCTP_STATE_ESTABLISHED) &&
	    (asoc->rwnd > asoc->a_rwnd) &&
	    ((asoc->rwnd - asoc->a_rwnd) >=
		     min_t(__u32, (asoc->base.sk->rcvbuf >> 1), asoc->pmtu))) {
		SCTP_DEBUG_PRINTK("%s: Sending window update SACK- asoc: %p "
				  "rwnd: %u a_rwnd: %u\n",
				  __FUNCTION__, asoc, asoc->rwnd, asoc->a_rwnd);
		sack = sctp_make_sack(asoc);
		if (!sack)
			return;

		/* Update the last advertised rwnd value. */
		asoc->a_rwnd = asoc->rwnd;

		asoc->peer.sack_needed = 0;

		sctp_outq_tail(&asoc->outqueue, sack);

		/* Stop the SACK timer.  */
		timer = &asoc->timers[SCTP_EVENT_TIMEOUT_SACK];
		if (timer_pending(timer) && del_timer(timer))
			sctp_association_put(asoc);
	}
}

/* Decrease asoc's rwnd by len. */
void sctp_assoc_rwnd_decrease(sctp_association_t *asoc, int len)
{
	SCTP_ASSERT(asoc->rwnd, "rwnd zero", return);
	SCTP_ASSERT(!asoc->rwnd_over, "rwnd_over not zero", return);
	if (asoc->rwnd >= len) {
		asoc->rwnd -= len;
	} else {
		asoc->rwnd_over = len - asoc->rwnd;
		asoc->rwnd = 0;
	}
	SCTP_DEBUG_PRINTK("%s: asoc %p rwnd decreased by %d to (%u, %u)\n",
			  __FUNCTION__, asoc, len, asoc->rwnd, asoc->rwnd_over);
}

/* Build the bind address list for the association based on info from the
 * local endpoint and the remote peer.
 */
int sctp_assoc_set_bind_addr_from_ep(sctp_association_t *asoc, int priority)
{
	sctp_scope_t scope;
	int flags;

	/* Use scoping rules to determine the subset of addresses from
	 * the endpoint.
	 */
	scope = sctp_scope(&asoc->peer.active_path->ipaddr);
	flags = (PF_INET6 == asoc->base.sk->family) ? SCTP_ADDR6_ALLOWED : 0;
	if (asoc->peer.ipv4_address)
		flags |= SCTP_ADDR4_PEERSUPP;
	if (asoc->peer.ipv6_address)
		flags |= SCTP_ADDR6_PEERSUPP;

	return sctp_bind_addr_copy(&asoc->base.bind_addr,
				   &asoc->ep->base.bind_addr,
				   scope, priority, flags);
}

/* Build the association's bind address list from the cookie.  */
int sctp_assoc_set_bind_addr_from_cookie(sctp_association_t *asoc,
					 sctp_cookie_t *cookie, int priority)
{
	int var_size2 = ntohs(cookie->peer_init->chunk_hdr.length);
	int var_size3 = cookie->raw_addr_list_len;
	__u8 *raw_addr_list = (__u8 *)cookie + sizeof(sctp_cookie_t) +
				      var_size2;

	return sctp_raw_to_bind_addrs(&asoc->base.bind_addr, raw_addr_list,
				      var_size3, asoc->ep->base.bind_addr.port,
				      priority);
}
