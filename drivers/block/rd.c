/*
 * ramdisk.c - Multiple ramdisk driver - gzip-loading version - v. 0.8 beta.
 * 
 * (C) Chad Page, Theodore Ts'o, et. al, 1995. 
 *
 * This ramdisk is designed to have filesystems created on it and mounted
 * just like a regular floppy disk.  
 *  
 * It also does something suggested by Linus: use the buffer cache as the
 * ramdisk data.  This makes it possible to dynamically allocate the ramdisk
 * buffer - with some consequences I have to deal with as I write this. 
 * 
 * This code is based on the original ramdisk.c, written mostly by
 * Theodore Ts'o (TYT) in 1991.  The code was largely rewritten by
 * Chad Page to use the buffer cache to store the ramdisk data in
 * 1995; Theodore then took over the driver again, and cleaned it up
 * for inclusion in the mainline kernel.
 *
 * The original CRAMDISK code was written by Richard Lyons, and
 * adapted by Chad Page to use the new ramdisk interface.  Theodore
 * Ts'o rewrote it so that both the compressed ramdisk loader and the
 * kernel decompressor uses the same inflate.c codebase.  The ramdisk
 * loader now also loads into a dynamic (buffer cache based) ramdisk,
 * not the old static ramdisk.  Support for the old static ramdisk has
 * been completely removed.
 */

#include <linux/sched.h>
#include <linux/minix_fs.h>
#include <linux/ext2_fs.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/malloc.h>
#include <linux/ioctl.h>

#include <asm/system.h>
#include <asm/segment.h>

extern void wait_for_keypress(void);

/*
 * 35 has been officially registered as the RAMDISK major number, but
 * so is the original MAJOR number of 1.  We're using 1 in
 * include/linux/major.h for now
 */
#define MAJOR_NR RAMDISK_MAJOR
#include <linux/blk.h>

#define BUILD_CRAMDISK
#define NUM_RAMDISKS 8

void rd_load(void);
static int crd_load(struct file *fp, struct file *outfp);

/* Various static variables go here... mostly used within the ramdisk code only. */

static int rd_length[NUM_RAMDISKS];
static int rd_blocksizes[NUM_RAMDISKS];

/*
 * Parameters for the boot-loading of the ramdisk.  These are set by
 * init/main.c (from arguments to the kernel command line) or from the
 * architecture-specific setup routine (from the stored bootsector
 * information). 
 */
int rd_doload = 0;		/* 1 = load ramdisk, 0 = don't load */
int rd_prompt = 1;		/* 1 = prompt for ramdisk, 0 = don't prompt */
int rd_image_start = 0;		/* starting block # of image */

int rd_loading = 0;

/*
 *  Basically, my strategy here is to set up a buffer-head which can't be
 *  deleted, and make that my Ramdisk.  If the request is outside of the
 *  allocated size, we must get rid of it...
 *
 */
static void rd_request(void)
{
	unsigned int minor;
	int offset, len;

repeat:
	INIT_REQUEST;
	
	minor = MINOR(CURRENT->rq_dev);

	if (minor >= NUM_RAMDISKS) {
		end_request(0);
		goto repeat;
	}
	
	offset = CURRENT->sector << 9;
	len = CURRENT->current_nr_sectors << 9;

	if ((offset + len) > rd_length[minor]) {
		end_request(0);
		goto repeat;
	}

	if (CURRENT->cmd == READ) {	
		memset(CURRENT->buffer, 0, len); 
	}
	set_bit(BH_Protected, &CURRENT->bh->b_state);

	end_request(1);
	goto repeat;
} 

static int rd_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	int err;
	
	if (!inode || !inode->i_rdev) 	
		return -EINVAL;

	switch (cmd) {
		case BLKFLSBUF:
			if (!suser()) return -EACCES;
			invalidate_buffers(inode->i_rdev);
			break;
         	case BLKGETSIZE:   /* Return device size */
			if (!arg)  return -EINVAL;
			err = verify_area(VERIFY_WRITE, (long *) arg,
					  sizeof(long));
			if (err)
				return err;
			put_user(rd_length[MINOR(inode->i_rdev)] / 512, 
				 (long *) arg);
			return 0;
			
		default:
			break;
	};

	return 0;
}

static int rd_open(struct inode * inode, struct file * filp)
{

	if (DEVICE_NR(inode->i_rdev) >= NUM_RAMDISKS)
		return -ENODEV;

	return 0;
}

static struct file_operations fd_fops = {
	NULL,		/* lseek - default */
	block_read,	/* read - block dev write */
	block_write,	/* write - block dev write */
	NULL,		/* readdir - not here! */
	NULL,		/* select */
	rd_ioctl, 	/* ioctl */
	NULL,		/* mmap */
	rd_open,	/* open */
	NULL,		/* no special release code... */
	block_fsync		/* fsync */ 
};

/* This is the registration and initialization section of the ramdisk driver */
int rd_init(void)
{
	int		i;

	if (register_blkdev(MAJOR_NR, "ramdisk", &fd_fops)) {
		printk("RAMDISK2 : Could not get major %d", MAJOR_NR);
		return -EIO;
	}

	blk_dev[MAJOR_NR].request_fn = &rd_request;

	for (i = 0; i < NUM_RAMDISKS; i++) {
		rd_length[i] = (16384 * 1024);
		rd_blocksizes[i] = 1024;
	}

	blksize_size[MAJOR_NR] = rd_blocksizes;

	return 0;
}

/*
 * This routine tries to a ramdisk image to load, and returns the
 * number of blocks to read for a non-compressed image, 0 if the image
 * is a compressed image, and -1 if an image with the right magic
 * numbers could not be found.
 *
 * We currently check for the following magic numbers:
 * 	minix
 * 	ext2
 * 	gzip
 */
int
identify_ramdisk_image(int device, struct file *fp, int start_block)
{
	const int size = 512;
	struct minix_super_block *minixsb;
	struct ext2_super_block *ext2sb;
	int nblocks = -1;
	int max_blocks;
	unsigned char *buf;

	buf = kmalloc(size, GFP_KERNEL);
	if (buf == 0)
		return -1;

	minixsb = (struct minix_super_block *) buf;
	ext2sb = (struct ext2_super_block *) buf;
	memset(buf, 0xe5, size);

	/*
	 * Read block 0 to test for gzipped kernel
	 */
	if (fp->f_op->lseek)
		fp->f_op->lseek(fp->f_inode, fp, start_block * BLOCK_SIZE, 0);
	fp->f_pos = start_block * BLOCK_SIZE;
	
	fp->f_op->read(fp->f_inode, fp, buf, size);

	/*
	 * If it matches the gzip magic numbers, return -1
	 */
	if (buf[0] == 037 && ((buf[1] == 0213) || (buf[1] == 0236))) {
		printk(KERN_NOTICE
		       "RAMDISK: Compressed image found at block %d\n",
		       start_block);
		nblocks = 0;
		goto done;
	}

	/*
	 * Read block 1 to test for minix and ext2 superblock
	 */
	if (fp->f_op->lseek)
		fp->f_op->lseek(fp->f_inode, fp,
				(start_block+1) * BLOCK_SIZE, 0);
	fp->f_pos = (start_block+1) * BLOCK_SIZE;

	fp->f_op->read(fp->f_inode, fp, buf, size);
		
	/* Try minix */
	if (minixsb->s_magic == MINIX_SUPER_MAGIC ||
	    minixsb->s_magic == MINIX_SUPER_MAGIC2) {
		printk(KERN_NOTICE
		       "RAMDISK: Minix filesystem found at block %d\n",
		       start_block);
		nblocks = minixsb->s_nzones << minixsb->s_log_zone_size;
		goto done;
	}

	/* Try ext2 */
	printk("ext2 magic = %d\n", ext2sb->s_magic);
	if (ext2sb->s_magic == EXT2_SUPER_MAGIC) {
		printk(KERN_NOTICE
		       "RAMDISK: Ext2 filesystem found at block %d\n",
		       start_block);
		nblocks = ext2sb->s_blocks_count;
		goto done;
	}
	printk(KERN_NOTICE
	       "RAMDISK: Couldn't find valid ramdisk image starting at %d.\n",
	       start_block);
	
done:
	if (fp->f_op->lseek)
		fp->f_op->lseek(fp->f_inode, fp, start_block * BLOCK_SIZE, 0);
	fp->f_pos = start_block * BLOCK_SIZE;	

	if ((nblocks > 0) && blk_size[MAJOR(device)]) {
		max_blocks = blk_size[MAJOR(device)][MINOR(device)];
		max_blocks -= start_block;
		if (nblocks > max_blocks) {
			printk(KERN_NOTICE
			       "RAMDISK: Restricting filesystem size "
			       "from %d to %d blocks.\n",
			       nblocks, max_blocks);
			nblocks = max_blocks;
		}
	}
	kfree(buf);
	return nblocks;
}

/*
 * This routine loads in the ramdisk image.
 */
void rd_load()
{
	struct inode inode, out_inode;
	struct file infile, outfile;
	unsigned short fs;
	int device, ram_device;
	int nblocks, i;
	char *buf;

	if (rd_doload == 0)
		return;
	
	device = ROOT_DEV;
	ram_device = (MAJOR_NR << 8);

	if (MAJOR(device) != FLOPPY_MAJOR) return;

	if (rd_prompt) {
		printk(KERN_NOTICE
		       "VFS: Insert ramdisk floppy and press ENTER\n");
		wait_for_keypress();
	}

	memset(&infile, 0, sizeof(infile));
	memset(&inode, 0, sizeof(inode));
	inode.i_rdev = device;
	infile.f_mode = 1; /* read only */
	infile.f_inode = &inode;

	memset(&outfile, 0, sizeof(outfile));
	memset(&out_inode, 0, sizeof(out_inode));
	out_inode.i_rdev = ram_device;
	outfile.f_mode = 3; /* read/write */
	outfile.f_inode = &out_inode;

	if (blkdev_open(&inode, &infile) != 0) return;
	if (blkdev_open(&out_inode, &outfile) != 0) return;

	fs = get_fs();
	set_fs(KERNEL_DS);
	rd_loading = 1;
	
	nblocks = identify_ramdisk_image(device, &infile, rd_image_start);
	if (nblocks < 0)
		goto done;

	if (nblocks == 0) {
#ifdef BUILD_CRAMDISK
		if (crd_load(&infile, &outfile) == 0)
			goto successful_load;
#else
		printk(KERN_NOTICE
		       "RAMDISK: Kernel does not support compressed "
		       "ramdisk images\n");
#endif
		goto done;
	}

	if (nblocks > (rd_length[0] >> BLOCK_SIZE_BITS)) {
		printk("RAMDISK: image too big! (%d/%d blocks)\n",
		       nblocks, rd_length[0] >> BLOCK_SIZE_BITS);
		goto done;
	}
		
	/*
	 * OK, time to copy in the data
	 */
	buf = kmalloc(BLOCK_SIZE, GFP_KERNEL);
	if (buf == 0) {
		printk(KERN_ERR "RAMDISK: could not allocate buffer\n");
		goto done;
	}
	for (i=0; i < nblocks; i++) {
		infile.f_op->read(infile.f_inode, &infile, buf,
				  BLOCK_SIZE);
		outfile.f_op->write(outfile.f_inode, &outfile, buf,
				    BLOCK_SIZE);
	}
	kfree(buf);

successful_load:
	invalidate_buffers(ROOT_DEV);
	ROOT_DEV = (MAJOR_NR << 8);

done:
	if (infile.f_op->release)
		infile.f_op->release(&inode, &infile);
	set_fs(fs);
	rd_loading = 0;
}

#ifdef BUILD_CRAMDISK

#include <string.h>

/*
 * gzip declarations
 */

#define OF(args)  args

#define memzero(s, n)     memset ((s), 0, (n))


typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;

#define INBUFSIZ 4096
#define WSIZE 0x8000    /* window size--must be a power of two, and */
			/*  at least 32K for zip's deflate method */

static uch *inbuf;
static uch *window;

static unsigned insize = 0;  /* valid bytes in inbuf */
static unsigned inptr = 0;   /* index of next byte to be processed in inbuf */
static unsigned outcnt = 0;  /* bytes in output buffer */
static exit_code = 0;
static long bytes_out = 0;
static struct file *crd_infp, *crd_outfp;

#define get_byte()  (inptr < insize ? inbuf[inptr++] : fill_inbuf())
		
/* Diagnostic functions (stubbed out) */
#define Assert(cond,msg)
#define Trace(x)
#define Tracev(x)
#define Tracevv(x)
#define Tracec(c,x)
#define Tracecv(c,x)

#define STATIC static

static int  fill_inbuf(void);
static void flush_window(void);
static void *malloc(int size);
static void free(void *where);
static void error(char *m);
static void gzip_mark(void **);
static void gzip_release(void **);

#include "../../lib/inflate.c"

static void *malloc(int size)
{
	return kmalloc(size, GFP_KERNEL);
}

static void free(void *where)
{
	kfree(where);
}

static void gzip_mark(void **ptr)
{
}

static void gzip_release(void **ptr)
{
}


/* ===========================================================================
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 */
static int fill_inbuf()
{
	if (exit_code) return -1;
	
	insize = crd_infp->f_op->read(crd_infp->f_inode, crd_infp,
				      inbuf, INBUFSIZ);
	if (insize == 0) return -1;

	inptr = 1;

	return inbuf[0];
}

/* ===========================================================================
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
static void flush_window()
{
    ulg c = crc;         /* temporary variable */
    unsigned n;
    uch *in, ch;
    
    crd_outfp->f_op->write(crd_outfp->f_inode, crd_outfp, window,
			   outcnt);
    in = window;
    for (n = 0; n < outcnt; n++) {
	    ch = *in++;
	    c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
    }
    crc = c;
    bytes_out += (ulg)outcnt;
    outcnt = 0;
}

static void error(char *x)
{
	printk(KERN_ERR "%s", x);
	exit_code = 1;
}

static int
crd_load(struct file * fp, struct file *outfp)
{
	int result;
	
	crd_infp = fp;
	crd_outfp = outfp;
	inbuf = kmalloc(INBUFSIZ, GFP_KERNEL);
	if (inbuf == 0) {
		printk(KERN_ERR "RAMDISK: Couldn't allocate gzip buffer\n");
		return -1;
	}
	window = kmalloc(WSIZE, GFP_KERNEL);
	if (window == 0) {
		printk(KERN_ERR "RAMDISK: Couldn't allocate gzip window\n");
		kfree(inbuf);
		return -1;
	}
	makecrc();
	result = gunzip();
	kfree(inbuf);
	kfree(window);
	return result;
}

#endif



