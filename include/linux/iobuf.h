/*
 * iobuf.h
 *
 * Defines the structures used to track abstract kernel-space io buffers.
 *
 */

#ifndef __LINUX_IOBUF_H
#define __LINUX_IOBUF_H

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <asm/atomic.h>

/*
 * The kiobuf structure describes a physical set of pages reserved
 * locked for IO.  The reference counts on each page will have been
 * incremented, and the flags field will indicate whether or not we have
 * pre-locked all of the pages for IO.
 *
 * kiobufs may be passed in arrays to form a kiovec, but we must
 * preserve the property that no page is present more than once over the
 * entire iovec.
 */

#define KIO_MAX_ATOMIC_IO	512 /* in kb */
#define KIO_STATIC_PAGES	(KIO_MAX_ATOMIC_IO / (PAGE_SIZE >> 10) + 1)
#define KIO_MAX_SECTORS		(KIO_MAX_ATOMIC_IO * 2)

/* The main kiobuf struct */

struct kiobuf 
{
	int		nr_pages;	/* Pages actually referenced */
	int		array_len;	/* Space in the allocated lists */
	int		offset;		/* Offset to start of valid data */
	int		length;		/* Number of valid bytes of data */

	/* Keep separate track of the physical addresses and page
	 * structs involved.  If we do IO to a memory-mapped device
	 * region, there won't necessarily be page structs defined for
	 * every address. */

	struct page **	maplist;

	unsigned int	locked : 1;	/* If set, pages has been locked */
	
	/* Always embed enough struct pages for atomic IO */
	struct page *	map_array[KIO_STATIC_PAGES];
	sector_t	blocks[KIO_MAX_SECTORS];

	/* Dynamic state for IO completion: */
	atomic_t	io_count;	/* IOs still in progress */
	int		errno;		/* Status of completed IO */
	void		(*end_io) (struct kiobuf *); /* Completion callback */
	wait_queue_head_t wait_queue;
};


/* mm/memory.c */

int	map_user_kiobuf(int rw, struct kiobuf *, unsigned long va, size_t len);
void	unmap_kiobuf(struct kiobuf *iobuf);
int	lock_kiovec(int nr, struct kiobuf *iovec[], int wait);
int	unlock_kiovec(int nr, struct kiobuf *iovec[]);
void	mark_dirty_kiobuf(struct kiobuf *iobuf, int bytes);

/* fs/iobuf.c */

int	end_kio_request(struct kiobuf *, int);
void	simple_wakeup_kiobuf(struct kiobuf *);
int	alloc_kiovec(int nr, struct kiobuf **);
void	free_kiovec(int nr, struct kiobuf **);
int	expand_kiobuf(struct kiobuf *, int);
void	kiobuf_wait_for_io(struct kiobuf *);
extern int alloc_kiobuf_bhs(struct kiobuf *);
extern void free_kiobuf_bhs(struct kiobuf *);

/* fs/buffer.c */

int	brw_kiovec(int rw, int nr, struct kiobuf *iovec[], 
		   struct block_device *bdev, sector_t [], int size);

/* fs/bio.c */
void	ll_rw_kio(int rw, struct kiobuf *kio, struct block_device *bdev, sector_t block);

#endif /* __LINUX_IOBUF_H */
