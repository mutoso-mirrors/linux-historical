/*
 *  linux/include/nfsd/state.h
 *
 *  Copyright (c) 2001 The Regents of the University of Michigan.
 *  All rights reserved.
 *
 *  Kendrick Smith <kmsmith@umich.edu>
 *  Andy Adamson <andros@umich.edu>
 *  
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the University nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _NFSD4_STATE_H
#define _NFSD4_STATE_H

#include <linux/list.h>

#define NFS4_OPAQUE_LIMIT 1024
typedef struct {
	u32             cl_boot;
	u32             cl_id;
} clientid_t;

typedef struct {
	u32             so_boot;
	u32             so_stateownerid;
	u32             so_fileid;
} stateid_opaque_t;

typedef struct {
	u32                     si_generation;
	stateid_opaque_t        si_opaque;
} stateid_t;
#define si_boot           si_opaque.so_boot
#define si_stateownerid   si_opaque.so_stateownerid
#define si_fileid         si_opaque.so_fileid

extern stateid_t zerostateid;
extern stateid_t onestateid;

#define ZERO_STATEID(stateid)       (!memcmp((stateid), &zerostateid, sizeof(stateid_t)))
#define ONE_STATEID(stateid)        (!memcmp((stateid), &onestateid, sizeof(stateid_t)))

/*
 * struct nfs4_client - one per client.  Clientids live here.
 * 	o Each nfs4_client is hashed by clientid.
 *
 * 	o Each nfs4_clients is also hashed by name 
 * 	  (the opaque quantity initially sent by the client to identify itself).
 * 	  
 *	o cl_perclient list is used to ensure no dangling stateowner references
 *	  when we expire the nfs4_client
 */
struct nfs4_client {
	struct list_head	cl_idhash; 	/* hash by cl_clientid.id */
	struct list_head	cl_strhash; 	/* hash by cl_name */
	struct list_head	cl_perclient; 	/* list: stateowners */
	struct list_head        cl_lru;         /* tail queue */
	struct xdr_netobj	cl_name; 	/* id generated by client */
	nfs4_verifier		cl_verifier; 	/* generated by client */
	time_t                  cl_time;        /* time of last lease renewal */
	u32			cl_addr; 	/* client ipaddress */
	struct svc_cred		cl_cred; 	/* setclientid principal */
	clientid_t		cl_clientid;	/* generated by server */
	nfs4_verifier		cl_confirm;	/* generated by server */
};

static inline void
update_stateid(stateid_t *stateid)
{
	stateid->si_generation++;
}

/* A reasonable value for REPLAY_ISIZE was estimated as follows:  
 * The OPEN response, typically the largest, requires 
 *   4(status) + 8(stateid) + 20(changeinfo) + 4(rflags) +  8(verifier) + 
 *   4(deleg. type) + 8(deleg. stateid) + 4(deleg. recall flag) + 
 *   20(deleg. space limit) + ~32(deleg. ace) = 112 bytes 
 */

#define NFSD4_REPLAY_ISIZE       112 

/*
 * Replay buffer, where the result of the last seqid-mutating operation 
 * is cached. 
 */
struct nfs4_replay {
	u32			rp_status;
	unsigned int		rp_buflen;
	char			*rp_buf;
	unsigned		intrp_allocated;
	int			rp_openfh_len;
	char			rp_openfh[NFS4_FHSIZE];
	char			rp_ibuf[NFSD4_REPLAY_ISIZE];
};

/*
* nfs4_stateowner can either be an open_owner, or a lock_owner
*
*    so_idhash:  stateid_hashtbl[] for open owner, lockstateid_hashtbl[]
*         for lock_owner
*    so_strhash: ownerstr_hashtbl[] for open_owner, lock_ownerstr_hashtbl[]
*         for lock_owner
*    so_perclient: nfs4_client->cl_perclient entry - used when nfs4_client
*         struct is reaped.
*    so_perfilestate: heads the list of nfs4_stateid (either open or lock) 
*         and is used to ensure no dangling nfs4_stateid references when we 
*         release a stateowner.
*    so_perlockowner: (open) nfs4_stateid->st_perlockowner entry - used when
*         close is called to reap associated byte-range locks
*/
struct nfs4_stateowner {
	struct list_head        so_idhash;   /* hash by so_id */
	struct list_head        so_strhash;   /* hash by op_name */
	struct list_head        so_perclient; /* nfs4_client->cl_perclient */
	struct list_head        so_perfilestate; /* list: nfs4_stateid */
	struct list_head        so_perlockowner; /* nfs4_stateid->st_perlockowner */
	int			so_is_open_owner; /* 1=openowner,0=lockowner */
	u32                     so_id;
	struct nfs4_client *    so_client;
	u32                     so_seqid;    
	struct xdr_netobj       so_owner;     /* open owner name */
	int                     so_confirmed; /* successful OPEN_CONFIRM? */
	struct nfs4_replay	so_replay;
};

/*
*  nfs4_file: a file opened by some number of (open) nfs4_stateowners.
*    o fi_perfile list is used to search for conflicting 
*      share_acces, share_deny on the file.
*/
struct nfs4_file {
	struct list_head        fi_hash;    /* hash by "struct inode *" */
	struct list_head        fi_perfile; /* list: nfs4_stateid */
	struct inode		*fi_inode;
	u32                     fi_id;      /* used with stateowner->so_id 
					     * for stateid_hashtbl hash */
};

/*
* nfs4_stateid can either be an open stateid or (eventually) a lock stateid
*
* (open)nfs4_stateid: one per (open)nfs4_stateowner, nfs4_file
*
* 	st_hash: stateid_hashtbl[] entry or lockstateid_hashtbl entry
* 	st_perfile: file_hashtbl[] entry.
* 	st_perfile_state: nfs4_stateowner->so_perfilestate
*       st_perlockowner: (open stateid) list of lock nfs4_stateowners
* 	st_share_access: used only for open stateid
* 	st_share_deny: used only for open stateid
*/

struct nfs4_stateid {
	struct list_head              st_hash; 
	struct list_head              st_perfile;
	struct list_head              st_perfilestate; 
	struct list_head              st_perlockowner;
	struct nfs4_stateowner      * st_stateowner;
	struct nfs4_file            * st_file;
	stateid_t                     st_stateid;
	struct file                   st_vfs_file;
	int                           st_vfs_set;
	unsigned int                  st_share_access;
	unsigned int                  st_share_deny;
};

/* flags for preprocess_seqid_op() */
#define CHECK_FH                0x00000001
#define CONFIRM                 0x00000002
#define OPEN_STATE              0x00000004
#define LOCK_STATE              0x00000008
#define RDWR_STATE              0x00000010

#define seqid_mutating_err(err)                       \
	(((err) != nfserr_stale_clientid) &&    \
	((err) != nfserr_bad_seqid) &&          \
	((err) != nfserr_stale_stateid) &&      \
	((err) != nfserr_bad_stateid))

extern time_t nfs4_laundromat(void);
extern int nfsd4_renew(clientid_t *clid);
extern int nfs4_preprocess_stateid_op(struct svc_fh *current_fh, 
		stateid_t *stateid, int flags, struct nfs4_stateid **stpp);
extern int nfs4_share_conflict(struct svc_fh *current_fh, 
		unsigned int deny_type);
extern void nfs4_lock_state(void);
extern void nfs4_unlock_state(void);
#endif   /* NFSD4_STATE_H */
