/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * These are the definitions needed for the tsnmap type.  The tsnmap is used
 * to track out of order TSNs received.
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
 *   Jon Grimm             <jgrimm@us.ibm.com>
 *   La Monte H.P. Yarroll <piggy@acm.org>
 *   Karl Knutson          <karl@athena.chicago.il.us>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */
#include <net/sctp/constants.h>

#ifndef __sctp_tsnmap_h__
#define __sctp_tsnmap_h__

/* RFC 2960 12.2 Parameters necessary per association (i.e. the TCB)
 * Mapping  An array of bits or bytes indicating which out of
 * Array    order TSN's have been received (relative to the
 *          Last Rcvd TSN). If no gaps exist, i.e. no out of
 *          order packets have been received, this array
 *          will be set to all zero. This structure may be
 *          in the form of a circular buffer or bit array.
 */
struct sctp_tsnmap {
	/* This array counts the number of chunks with each TSN.
	 * It points at one of the two buffers with which we will
	 * ping-pong between.
	 */
	__u8 *tsn_map;

	/* This marks the tsn which overflows the tsn_map, when the
	 * cumulative ack point reaches this point we know we can switch
	 * maps (tsn_map and overflow_map swap).
	 */
	__u32 overflow_tsn;

	/* This is the overflow array for tsn_map.
	 * It points at one of the other ping-pong buffers.
	 */
	__u8 *overflow_map;

	/* This is the TSN at tsn_map[0].  */
	__u32 base_tsn;

	/* Last Rcvd   : This is the last TSN received in
	 * TSN	       : sequence. This value is set initially by
	 *             : taking the peer's Initial TSN, received in
	 *             : the INIT or INIT ACK chunk, and subtracting
	 *             : one from it.
	 *
	 * Throughout most of the specification this is called the
	 * "Cumulative TSN ACK Point".  In this case, we
	 * ignore the advice in 12.2 in favour of the term
	 * used in the bulk of the text.
	 */
	__u32 cumulative_tsn_ack_point;

	/* This is the minimum number of TSNs we can track.  This corresponds
	 * to the size of tsn_map.   Note: the overflow_map allows us to
	 * potentially track more than this quantity.
	 */
	__u16 len;

	/* This is the highest TSN we've marked.  */
	__u32 max_tsn_seen;

	/* Data chunks pending receipt. used by SCTP_STATUS sockopt */
	__u16 pending_data;

	/* Record duplicate TSNs here.  We clear this after
	 * every SACK.  Store up to SCTP_MAX_DUP_TSNS worth of
	 * information.
	 */
	__u32 dup_tsns[SCTP_MAX_DUP_TSNS];
	__u16 num_dup_tsns;

	/* Record gap ack block information here.  */
	struct sctp_gap_ack_block gabs[SCTP_MAX_GABS];

	int malloced;

	__u8 raw_map[0];
};

struct sctp_tsnmap_iter {
	__u32 start;
};

/* Create a new tsnmap.  */
struct sctp_tsnmap *sctp_tsnmap_new(__u16 len, __u32 init_tsn, int gfp);

/* Dispose of a tsnmap.  */
void sctp_tsnmap_free(struct sctp_tsnmap *);

/* This macro assists in creation of external storage for variable length
 * internal buffers.  We double allocate so the overflow map works.
 */
#define sctp_tsnmap_storage_size(count) (sizeof(__u8) * (count) * 2)

/* Initialize a block of memory as a tsnmap.  */
struct sctp_tsnmap *sctp_tsnmap_init(struct sctp_tsnmap *, __u16 len,
				     __u32 initial_tsn);

/* Test the tracking state of this TSN.
 * Returns:
 *   0 if the TSN has not yet been seen
 *  >0 if the TSN has been seen (duplicate)
 *  <0 if the TSN is invalid (too large to track)
 */
int sctp_tsnmap_check(const struct sctp_tsnmap *, __u32 tsn);

/* Mark this TSN as seen.  */
void sctp_tsnmap_mark(struct sctp_tsnmap *, __u32 tsn);

/* Retrieve the Cumulative TSN ACK Point.  */
static inline __u32 sctp_tsnmap_get_ctsn(const struct sctp_tsnmap *map)
{
	return map->cumulative_tsn_ack_point;
}

/* Retrieve the highest TSN we've seen.  */
static inline __u32 sctp_tsnmap_get_max_tsn_seen(const struct sctp_tsnmap *map)
{
	return map->max_tsn_seen;
}

/* How many duplicate TSNs are stored? */
static inline __u16 sctp_tsnmap_num_dups(struct sctp_tsnmap *map)
{
	return map->num_dup_tsns;
}

/* Return pointer to duplicate tsn array as needed by SACK. */
static inline __u32 *sctp_tsnmap_get_dups(struct sctp_tsnmap *map)
{
	map->num_dup_tsns = 0;
	return map->dup_tsns;
}

/* How many gap ack blocks do we have recorded? */
__u16 sctp_tsnmap_num_gabs(struct sctp_tsnmap *map);

/* Refresh the count on pending data. */
__u16 sctp_tsnmap_pending(struct sctp_tsnmap *map);

/* Return pointer to gap ack blocks as needed by SACK. */
static inline struct sctp_gap_ack_block *sctp_tsnmap_get_gabs(struct sctp_tsnmap *map)
{
	return map->gabs;
}

/* Is there a gap in the TSN map?  */
static inline int sctp_tsnmap_has_gap(const struct sctp_tsnmap *map)
{
	int has_gap;

	has_gap = (map->cumulative_tsn_ack_point != map->max_tsn_seen);
	return has_gap;
}

/* Mark a duplicate TSN.  Note:  limit the storage of duplicate TSN
 * information.
 */
static inline void sctp_tsnmap_mark_dup(struct sctp_tsnmap *map, __u32 tsn)
{
	if (map->num_dup_tsns < SCTP_MAX_DUP_TSNS)
		map->dup_tsns[map->num_dup_tsns++] = htonl(tsn);
}

/* Renege a TSN that was seen.  */
void sctp_tsnmap_renege(struct sctp_tsnmap *, __u32 tsn);

/* Is there a gap in the TSN map? */
int sctp_tsnmap_has_gap(const struct sctp_tsnmap *);

/* Initialize a gap ack block interator from user-provided memory.  */
void sctp_tsnmap_iter_init(const struct sctp_tsnmap *,
			   struct sctp_tsnmap_iter *);

/* Get the next gap ack blocks.  We return 0 if there are no more
 * gap ack blocks.
 */
int sctp_tsnmap_next_gap_ack(const struct sctp_tsnmap *,
	struct sctp_tsnmap_iter *,__u16 *start, __u16 *end);

#endif /* __sctp_tsnmap_h__ */
