/* SCTP kernel reference Implementation
 * Copyright (c) 2003 International Business Machines, Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * These functions manipulate sctp SSN tracker.
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
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>


/* Storage size needed for map includes 2 headers and then the
 * specific needs of in or out streams.
 */
static inline size_t sctp_ssnmap_size(__u16 in, __u16 out)
{
	return sizeof(struct sctp_ssnmap) + (in + out) * sizeof(__u16);
}


/* Create a new sctp_ssnmap.
 * Allocate room to store at least 'len' contiguous TSNs.
 */
struct sctp_ssnmap *sctp_ssnmap_new(__u16 in, __u16 out, int gfp)
{
	struct sctp_ssnmap *retval;

	retval = kmalloc(sctp_ssnmap_size(in, out), gfp);

	if (!retval)
		goto fail;

	if (!sctp_ssnmap_init(retval, in, out))
		goto fail_map;

	retval->malloced = 1;
	SCTP_DBG_OBJCNT_INC(ssnmap);

	return retval;

fail_map:
	kfree(retval);
fail:
	return NULL;
}


/* Initialize a block of memory as a ssnmap.  */
struct sctp_ssnmap *sctp_ssnmap_init(struct sctp_ssnmap *map, __u16 in,
				     __u16 out)
{
	memset(map, 0x00, sctp_ssnmap_size(in, out));

	/* Start 'in' stream just after the map header. */
	map->in.ssn = (__u16 *)&map[1];
	map->in.len = in;

	/* Start 'out' stream just after 'in'. */
	map->out.ssn = &map->in.ssn[in];
	map->out.len = out;

	return map;
}

/* Clear out the ssnmap streams.  */
void sctp_ssnmap_clear(struct sctp_ssnmap *map)
{
	size_t size;

	size = (map->in.len + map->out.len) * sizeof(__u16);
	memset(map->in.ssn, 0x00, size);
}

/* Dispose of a ssnmap.  */
void sctp_ssnmap_free(struct sctp_ssnmap *map)
{
	if (map && map->malloced) {
		kfree(map);
		SCTP_DBG_OBJCNT_DEC(ssnmap);
	}
}
