/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */
#ifdef __KERNEL__

#include <linux/string.h>
#include <linux/locks.h>
#include <linux/sched.h>
#include <linux/reiserfs_fs.h>

#else

#include "nokernel.h"

#endif


// find where objectid map starts
#define objectid_map(s,rs) (old_format_only (s) ? \
                         (__u32 *)((struct reiserfs_super_block_v1 *)rs + 1) :\
			 (__u32 *)(rs + 1))


#ifdef CONFIG_REISERFS_CHECK

static void check_objectid_map (struct super_block * s, __u32 * map)
{
    if (le32_to_cpu (map[0]) != 1)
	reiserfs_panic (s, "vs-15010: check_objectid_map: map corrupted");

    // FIXME: add something else here
}

#endif


/* When we allocate objectids we allocate the first unused objectid.
   Each sequence of objectids in use (the odd sequences) is followed
   by a sequence of objectids not in use (the even sequences).  We
   only need to record the last objectid in each of these sequences
   (both the odd and even sequences) in order to fully define the
   boundaries of the sequences.  A consequence of allocating the first
   objectid not in use is that under most conditions this scheme is
   extremely compact.  The exception is immediately after a sequence
   of operations which deletes a large number of objects of
   non-sequential objectids, and even then it will become compact
   again as soon as more objects are created.  Note that many
   interesting optimizations of layout could result from complicating
   objectid assignment, but we have deferred making them for now. */


/* get unique object identifier */
__u32 reiserfs_get_unused_objectid (struct reiserfs_transaction_handle *th)
{
    struct super_block * s = th->t_super;
    struct reiserfs_super_block * rs = SB_DISK_SUPER_BLOCK (s);
    __u32 * map = objectid_map (s, rs);
    __u32 unused_objectid;


#ifdef CONFIG_REISERFS_CHECK
    check_objectid_map (s, map);
#endif

    reiserfs_prepare_for_journal(s, SB_BUFFER_WITH_SB(s), 1) ;
                                /* comment needed -Hans */
    unused_objectid = le32_to_cpu (map[1]);
    if (unused_objectid == U32_MAX) {
	printk ("REISERFS: get_objectid: no more object ids\n");
	reiserfs_restore_prepared_buffer(s, SB_BUFFER_WITH_SB(s)) ;
	return 0;
    }

    /* This incrementation allocates the first unused objectid. That
       is to say, the first entry on the objectid map is the first
       unused objectid, and by incrementing it we use it.  See below
       where we check to see if we eliminated a sequence of unused
       objectids.... */
    map[1] = cpu_to_le32 (unused_objectid + 1);

    /* Now we check to see if we eliminated the last remaining member of
       the first even sequence (and can eliminate the sequence by
       eliminating its last objectid from oids), and can collapse the
       first two odd sequences into one sequence.  If so, then the net
       result is to eliminate a pair of objectids from oids.  We do this
       by shifting the entire map to the left. */
    if (le16_to_cpu (rs->s_oid_cursize) > 2 && map[1] == map[2]) {
	memmove (map + 1, map + 3, (le16_to_cpu (rs->s_oid_cursize) - 3) * sizeof(__u32));
	//rs->s_oid_cursize -= 2;
	rs->s_oid_cursize = cpu_to_le16 (le16_to_cpu (rs->s_oid_cursize) - 2);
    }

    journal_mark_dirty(th, s, SB_BUFFER_WITH_SB (s));
    s->s_dirt = 1;
    return unused_objectid;
}


/* makes object identifier unused */
void reiserfs_release_objectid (struct reiserfs_transaction_handle *th, 
				__u32 objectid_to_release)
{
    struct super_block * s = th->t_super;
    struct reiserfs_super_block * rs = SB_DISK_SUPER_BLOCK (s);
    __u32 * map = objectid_map (s, rs);
    int i = 0;

    //return;
#ifdef CONFIG_REISERFS_CHECK
    check_objectid_map (s, map);
#endif

    reiserfs_prepare_for_journal(s, SB_BUFFER_WITH_SB(s), 1) ;
    journal_mark_dirty(th, s, SB_BUFFER_WITH_SB (s)); 
    s->s_dirt = 1;


    /* start at the beginning of the objectid map (i = 0) and go to
       the end of it (i = disk_sb->s_oid_cursize).  Linear search is
       what we use, though it is possible that binary search would be
       more efficient after performing lots of deletions (which is
       when oids is large.)  We only check even i's. */
    while (i < le16_to_cpu (rs->s_oid_cursize)) {
	if (objectid_to_release == le32_to_cpu (map[i])) {
	    /* This incrementation unallocates the objectid. */
	    //map[i]++;
	    map[i] = cpu_to_le32 (le32_to_cpu (map[i]) + 1);

	    /* Did we unallocate the last member of an odd sequence, and can shrink oids? */
	    if (map[i] == map[i+1]) {
		/* shrink objectid map */
		memmove (map + i, map + i + 2, 
			 (le16_to_cpu (rs->s_oid_cursize) - i - 2) * sizeof (__u32));
		//disk_sb->s_oid_cursize -= 2;
		rs->s_oid_cursize = cpu_to_le16 (le16_to_cpu (rs->s_oid_cursize) - 2);

#ifdef CONFIG_REISERFS_CHECK
		if (le16_to_cpu (rs->s_oid_cursize) < 2 || 
		    le16_to_cpu (rs->s_oid_cursize) > le16_to_cpu (rs->s_oid_maxsize))
		    reiserfs_panic (s, "vs-15005: reiserfs_release_objectid: "
				    "objectid map corrupted cur_size == %d (max == %d)",
				    le16_to_cpu (rs->s_oid_cursize), le16_to_cpu (rs->s_oid_maxsize));
#endif
	    }
	    return;
	}

	if (objectid_to_release > le32_to_cpu (map[i]) && 
	    objectid_to_release < le32_to_cpu (map[i + 1])) {
	    /* size of objectid map is not changed */
	    if (objectid_to_release + 1 == le32_to_cpu (map[i + 1])) {
		//objectid_map[i+1]--;
		map[i + 1] = cpu_to_le32 (le32_to_cpu (map[i + 1]) - 1);
		return;
	    }

	    if (rs->s_oid_cursize == rs->s_oid_maxsize)
		/* objectid map must be expanded, but there is no space */
		return;

	    /* expand the objectid map*/
	    memmove (map + i + 3, map + i + 1, 
		     (le16_to_cpu (rs->s_oid_cursize) - i - 1) * sizeof(__u32));
	    map[i + 1] = cpu_to_le32 (objectid_to_release);
	    map[i + 2] = cpu_to_le32 (objectid_to_release + 1);
	    rs->s_oid_cursize = cpu_to_le16 (le16_to_cpu (rs->s_oid_cursize) + 2);
	    return;
	}
	i += 2;
    }

    reiserfs_warning ("vs-15010: reiserfs_release_objectid: tried to free free object id (%lu)", 
		      objectid_to_release);
}


int reiserfs_convert_objectid_map_v1(struct super_block *s) {
    struct reiserfs_super_block *disk_sb = SB_DISK_SUPER_BLOCK (s);
    int cur_size = le16_to_cpu(disk_sb->s_oid_cursize) ;
    int new_size = (s->s_blocksize - SB_SIZE) / sizeof(__u32) / 2 * 2 ;
    int old_max = le16_to_cpu(disk_sb->s_oid_maxsize) ;
    struct reiserfs_super_block_v1 *disk_sb_v1 ;
    __u32 *objectid_map, *new_objectid_map ;
    int i ;

    disk_sb_v1=(struct reiserfs_super_block_v1 *)(SB_BUFFER_WITH_SB(s)->b_data);
    objectid_map = (__u32 *)(disk_sb_v1 + 1) ;
    new_objectid_map = (__u32 *)(disk_sb + 1) ;

    if (cur_size > new_size) {
	/* mark everyone used that was listed as free at the end of the objectid
	** map 
	*/
	objectid_map[new_size - 1] = objectid_map[cur_size - 1] ;
	disk_sb->s_oid_cursize = cpu_to_le16(new_size) ;
    }
    /* move the smaller objectid map past the end of the new super */
    for (i = new_size - 1 ; i >= 0 ; i--) {
        objectid_map[i + (old_max - new_size)] = objectid_map[i] ; 
    }


    /* set the max size so we don't overflow later */
    disk_sb->s_oid_maxsize = cpu_to_le16(new_size) ;

    /* finally, zero out the unused chunk of the new super */
    memset(disk_sb->s_unused, 0, sizeof(disk_sb->s_unused)) ;
    return 0 ;
}

