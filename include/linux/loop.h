#ifndef _LINUX_LOOP_H
#define _LINUX_LOOP_H

/*
 * include/linux/loop.h
 *
 * Written by Theodore Ts'o, 3/29/93.
 *
 * Copyright 1993 by Theodore Ts'o.  Redistribution of this file is
 * permitted under the GNU General Public License.
 */

#define LO_NAME_SIZE	64
#define LO_KEY_SIZE	32

#ifdef __KERNEL__

/* Possible states of device */
enum {
	Lo_unbound,
	Lo_bound,
	Lo_rundown,
};

struct loop_device {
	int		lo_number;
	int		lo_refcnt;
	int		lo_offset;
	int		lo_encrypt_type;
	int		lo_encrypt_key_size;
	int		lo_flags;
	int		(*transfer)(struct loop_device *, int cmd,
				    char *raw_buf, char *loop_buf, int size,
				    sector_t real_block);
	char		lo_name[LO_NAME_SIZE];
	char		lo_encrypt_key[LO_KEY_SIZE];
	__u32           lo_init[2];
	uid_t		lo_key_owner;	/* Who set the key */
	int		(*ioctl)(struct loop_device *, int cmd, 
				 unsigned long arg); 

	struct file *	lo_backing_file;
	struct block_device *lo_device;
	unsigned	lo_blocksize;
	void		*key_data; 
	char		key_reserved[48]; /* for use by the filter modules */

	int		old_gfp_mask;

	spinlock_t		lo_lock;
	struct bio 		*lo_bio;
	struct bio		*lo_biotail;
	int			lo_state;
	struct semaphore	lo_sem;
	struct semaphore	lo_ctl_mutex;
	struct semaphore	lo_bh_mutex;
	atomic_t		lo_pending;

	request_queue_t		lo_queue;
};

typedef	int (* transfer_proc_t)(struct loop_device *, int cmd,
				char *raw_buf, char *loop_buf, int size,
				int real_block);

#endif /* __KERNEL__ */

/*
 * Loop flags
 */
#define LO_FLAGS_DO_BMAP	1
#define LO_FLAGS_READ_ONLY	2

#include <asm/posix_types.h>	/* for __kernel_old_dev_t */
#include <asm/types.h>		/* for __u64 */

/* Backwards compatibility version */
struct loop_info {
	int		   lo_number;		/* ioctl r/o */
	__kernel_old_dev_t lo_device; 		/* ioctl r/o */
	unsigned long	   lo_inode; 		/* ioctl r/o */
	__kernel_old_dev_t lo_rdevice; 		/* ioctl r/o */
	int		   lo_offset;
	int		   lo_encrypt_type;
	int		   lo_encrypt_key_size; 	/* ioctl w/o */
	int		   lo_flags;			/* ioctl r/o */
	char		   lo_name[LO_NAME_SIZE];
	unsigned char	   lo_encrypt_key[LO_KEY_SIZE]; /* ioctl w/o */
	unsigned long	   lo_init[2];
	char		   reserved[4];
};

struct loop_info64 {
	__u64		   lo_device; 		/* ioctl r/o */
	__u64		   lo_inode; 		/* ioctl r/o */
	__u64		   lo_rdevice; 		/* ioctl r/o */
	__u64		   lo_offset;
	__u32		   lo_number;		/* ioctl r/o */
	__u32		   lo_encrypt_type;
	__u32		   lo_encrypt_key_size; 	/* ioctl w/o */
	__u32		   lo_flags;			/* ioctl r/o */
	__u8		   lo_name[LO_NAME_SIZE];
	__u8		   lo_encrypt_key[LO_KEY_SIZE]; /* ioctl w/o */
	__u64		   lo_init[2];
};

/*
 * Loop filter types
 */

#define LO_CRYPT_NONE	  0
#define LO_CRYPT_XOR	  1
#define LO_CRYPT_DES	  2
#define LO_CRYPT_FISH2    3    /* Brand new Twofish encryption */
#define LO_CRYPT_BLOW     4
#define LO_CRYPT_CAST128  5
#define LO_CRYPT_IDEA     6
#define LO_CRYPT_DUMMY    9
#define LO_CRYPT_SKIPJACK 10
#define MAX_LO_CRYPT	20

#ifdef __KERNEL__
/* Support for loadable transfer modules */
struct loop_func_table {
	int number; 	/* filter type */ 
	int (*transfer)(struct loop_device *lo, int cmd, char *raw_buf,
			char *loop_buf, int size, sector_t real_block);
	int (*init)(struct loop_device *, const struct loop_info64 *); 
	/* release is called from loop_unregister_transfer or clr_fd */
	int (*release)(struct loop_device *); 
	int (*ioctl)(struct loop_device *, int cmd, unsigned long arg);
	/* lock and unlock manage the module use counts */ 
	void (*lock)(struct loop_device *);
	void (*unlock)(struct loop_device *);
}; 

int loop_register_transfer(struct loop_func_table *funcs);
int loop_unregister_transfer(int number); 

#endif
/*
 * IOCTL commands --- we will commandeer 0x4C ('L')
 */

#define LOOP_SET_FD		0x4C00
#define LOOP_CLR_FD		0x4C01
#define LOOP_SET_STATUS		0x4C02
#define LOOP_GET_STATUS		0x4C03
#define LOOP_SET_STATUS64	0x4C04
#define LOOP_GET_STATUS64	0x4C05

#endif
