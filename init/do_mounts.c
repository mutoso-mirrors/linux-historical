#include <linux/sched.h>
#include <linux/ctype.h>
#include <linux/blk.h>
#include <linux/fd.h>
#include <linux/tty.h>
#include <linux/suspend.h>
#include <linux/root_dev.h>
#include <linux/security.h>

#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/nfs_mount.h>
#include <linux/minix_fs.h>
#include <linux/ext2_fs.h>
#include <linux/romfs_fs.h>

#include "do_mounts.h"

#define BUILD_CRAMDISK

extern int get_filesystem_list(char * buf);

extern asmlinkage long sys_mkdir(const char *name, int mode);
extern asmlinkage long sys_rmdir(const char *name);
extern asmlinkage long sys_chdir(const char *name);
extern asmlinkage long sys_fchdir(int fd);
extern asmlinkage long sys_chroot(const char *name);

int __initdata rd_doload;	/* 1 = load RAM disk, 0 = don't load */

int root_mountflags = MS_RDONLY | MS_VERBOSE;
static char * __initdata root_device_name;
static char __initdata saved_root_name[64];

/* this is initialized in init/main.c */
dev_t ROOT_DEV;

static int __init load_ramdisk(char *str)
{
	rd_doload = simple_strtol(str,NULL,0) & 3;
	return 1;
}
__setup("load_ramdisk=", load_ramdisk);

static int __init readonly(char *str)
{
	if (*str)
		return 0;
	root_mountflags |= MS_RDONLY;
	return 1;
}

static int __init readwrite(char *str)
{
	if (*str)
		return 0;
	root_mountflags &= ~MS_RDONLY;
	return 1;
}

__setup("ro", readonly);
__setup("rw", readwrite);

static dev_t __init try_name(char *name, int part)
{
	char path[64];
	char buf[32];
	int range;
	dev_t res;
	char *s;
	int len;
	int fd;

	/* read device number from .../dev */

	sprintf(path, "/sys/block/%s/dev", name);
	fd = open(path, 0, 0);
	if (fd < 0)
		goto fail;
	len = read(fd, buf, 32);
	close(fd);
	if (len <= 0 || len == 32 || buf[len - 1] != '\n')
		goto fail;
	buf[len - 1] = '\0';
	res = (dev_t) simple_strtoul(buf, &s, 16);
	if (*s)
		goto fail;

	/* if it's there and we are not looking for a partition - that's it */
	if (!part)
		return res;

	/* otherwise read range from .../range */
	sprintf(path, "/sys/block/%s/range", name);
	fd = open(path, 0, 0);
	if (fd < 0)
		goto fail;
	len = read(fd, buf, 32);
	close(fd);
	if (len <= 0 || len == 32 || buf[len - 1] != '\n')
		goto fail;
	buf[len - 1] = '\0';
	range = simple_strtoul(buf, &s, 10);
	if (*s)
		goto fail;

	/* if partition is within range - we got it */
	if (part < range)
		return res + part;
fail:
	return (dev_t) 0;
}

/*
 *	Convert a name into device number.  We accept the following variants:
 *
 *	1) device number in hexadecimal	represents itself
 *	2) /dev/nfs represents Root_NFS (0xff)
 *	3) /dev/<disk_name> represents the device number of disk
 *	4) /dev/<disk_name><decimal> represents the device number
 *         of partition - device number of disk plus the partition number
 *	5) /dev/<disk_name>p<decimal> - same as the above, that form is
 *	   used when disk name of partitioned disk ends on a digit.
 *
 *	If name doesn't have fall into the categories above, we return 0.
 *	Driverfs is used to check if something is a disk name - it has
 *	all known disks under bus/block/devices.  If the disk name
 *	contains slashes, name of driverfs node has them replaced with
 *	dots.  try_name() does the actual checks, assuming that driverfs
 *	is mounted on rootfs /sys.
 */

dev_t name_to_dev_t(char *name)
{
	char s[32];
	char *p;
	dev_t res = 0;
	int part;

	sys_mkdir("/sys", 0700);
	if (sys_mount("sysfs", "/sys", "sysfs", 0, NULL) < 0)
		goto out;

	if (strncmp(name, "/dev/", 5) != 0) {
		res = (dev_t) simple_strtoul(name, &p, 16);
		if (*p)
			goto fail;
		goto done;
	}
	name += 5;
	res = Root_NFS;
	if (strcmp(name, "nfs") == 0)
		goto done;

	if (strlen(name) > 31)
		goto fail;
	strcpy(s, name);
	for (p = s; *p; p++)
		if (*p == '/')
			*p = '.';
	res = try_name(s, 0);
	if (res)
		goto done;

	while (p > s && isdigit(p[-1]))
		p--;
	if (p == s || !*p || *p == '0')
		goto fail;
	part = simple_strtoul(p, NULL, 10);
	*p = '\0';
	res = try_name(s, part);
	if (res)
		goto done;

	if (p < s + 2 || !isdigit(p[-2]) || p[-1] != 'p')
		goto fail;
	p[-1] = '\0';
	res = try_name(s, part);
done:
	sys_umount("/sys", 0);
out:
	sys_rmdir("/sys");
	return res;
fail:
	res = (dev_t) 0;
	goto done;
}

static int __init root_dev_setup(char *line)
{
	strncpy(saved_root_name, line, 63);
	return 1;
}

__setup("root=", root_dev_setup);

static char * __initdata root_mount_data;
static int __init root_data_setup(char *str)
{
	root_mount_data = str;
	return 1;
}

static char * __initdata root_fs_names;
static int __init fs_names_setup(char *str)
{
	root_fs_names = str;
	return 1;
}

__setup("rootflags=", root_data_setup);
__setup("rootfstype=", fs_names_setup);

static void __init get_fs_names(char *page)
{
	char *s = page;

	if (root_fs_names) {
		strcpy(page, root_fs_names);
		while (*s++) {
			if (s[-1] == ',')
				s[-1] = '\0';
		}
	} else {
		int len = get_filesystem_list(page);
		char *p, *next;

		page[len] = '\0';
		for (p = page-1; p; p = next) {
			next = strchr(++p, '\n');
			if (*p++ != '\t')
				continue;
			while ((*s++ = *p++) != '\n')
				;
			s[-1] = '\0';
		}
	}
	*s = '\0';
}
static void __init mount_block_root(char *name, int flags)
{
	char *fs_names = __getname();
	char *p;

	get_fs_names(fs_names);
retry:
	for (p = fs_names; *p; p += strlen(p)+1) {
		int err = sys_mount(name, "/root", p, flags, root_mount_data);
		switch (err) {
			case 0:
				goto out;
			case -EACCES:
				flags |= MS_RDONLY;
				goto retry;
			case -EINVAL:
				continue;
		}
	        /*
		 * Allow the user to distinguish between failed open
		 * and bad superblock on root device.
		 */
		printk ("VFS: Cannot open root device \"%s\" or %s\n",
			root_device_name, kdevname (to_kdev_t(ROOT_DEV)));
		printk ("Please append a correct \"root=\" boot option\n");
		panic("VFS: Unable to mount root fs on %s",
			kdevname(to_kdev_t(ROOT_DEV)));
	}
	panic("VFS: Unable to mount root fs on %s", kdevname(to_kdev_t(ROOT_DEV)));
out:
	putname(fs_names);
	sys_chdir("/root");
	ROOT_DEV = current->fs->pwdmnt->mnt_sb->s_dev;
	printk("VFS: Mounted root (%s filesystem)%s.\n",
		current->fs->pwdmnt->mnt_sb->s_type->name,
		(current->fs->pwdmnt->mnt_sb->s_flags & MS_RDONLY) ? " readonly" : "");
}
 
#ifdef CONFIG_ROOT_NFS
static int __init mount_nfs_root(void)
{
	void *data = nfs_root_data();

	if (data && sys_mount("/dev/root","/root","nfs",root_mountflags,data) == 0)
		return 1;
	return 0;
}
#endif

#if defined(CONFIG_BLK_DEV_RAM) || defined(CONFIG_BLK_DEV_FD)
static void __init change_floppy(char *fmt, ...)
{
	struct termios termios;
	char buf[80];
	char c;
	int fd;
	va_list args;
	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);
	fd = open("/dev/root", O_RDWR | O_NDELAY, 0);
	if (fd >= 0) {
		sys_ioctl(fd, FDEJECT, 0);
		close(fd);
	}
	printk(KERN_NOTICE "VFS: Insert %s and press ENTER\n", buf);
	fd = open("/dev/console", O_RDWR, 0);
	if (fd >= 0) {
		sys_ioctl(fd, TCGETS, (long)&termios);
		termios.c_lflag &= ~ICANON;
		sys_ioctl(fd, TCSETSF, (long)&termios);
		read(fd, &c, 1);
		termios.c_lflag |= ICANON;
		sys_ioctl(fd, TCSETSF, (long)&termios);
		close(fd);
	}
}
#endif

#ifdef CONFIG_BLK_DEV_RAM

int __initdata rd_prompt = 1;	/* 1 = prompt for RAM disk, 0 = don't prompt */

static int __init prompt_ramdisk(char *str)
{
	rd_prompt = simple_strtol(str,NULL,0) & 1;
	return 1;
}
__setup("prompt_ramdisk=", prompt_ramdisk);

int __initdata rd_image_start;		/* starting block # of image */

static int __init ramdisk_start_setup(char *str)
{
	rd_image_start = simple_strtol(str,NULL,0);
	return 1;
}
__setup("ramdisk_start=", ramdisk_start_setup);

static int __init crd_load(int in_fd, int out_fd);

/*
 * This routine tries to find a RAM disk image to load, and returns the
 * number of blocks to read for a non-compressed image, 0 if the image
 * is a compressed image, and -1 if an image with the right magic
 * numbers could not be found.
 *
 * We currently check for the following magic numbers:
 * 	minix
 * 	ext2
 *	romfs
 * 	gzip
 */
static int __init 
identify_ramdisk_image(int fd, int start_block)
{
	const int size = 512;
	struct minix_super_block *minixsb;
	struct ext2_super_block *ext2sb;
	struct romfs_super_block *romfsb;
	int nblocks = -1;
	unsigned char *buf;

	buf = kmalloc(size, GFP_KERNEL);
	if (buf == 0)
		return -1;

	minixsb = (struct minix_super_block *) buf;
	ext2sb = (struct ext2_super_block *) buf;
	romfsb = (struct romfs_super_block *) buf;
	memset(buf, 0xe5, size);

	/*
	 * Read block 0 to test for gzipped kernel
	 */
	lseek(fd, start_block * BLOCK_SIZE, 0);
	read(fd, buf, size);

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

	/* romfs is at block zero too */
	if (romfsb->word0 == ROMSB_WORD0 &&
	    romfsb->word1 == ROMSB_WORD1) {
		printk(KERN_NOTICE
		       "RAMDISK: romfs filesystem found at block %d\n",
		       start_block);
		nblocks = (ntohl(romfsb->size)+BLOCK_SIZE-1)>>BLOCK_SIZE_BITS;
		goto done;
	}

	/*
	 * Read block 1 to test for minix and ext2 superblock
	 */
	lseek(fd, (start_block+1) * BLOCK_SIZE, 0);
	read(fd, buf, size);

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
	if (ext2sb->s_magic == cpu_to_le16(EXT2_SUPER_MAGIC)) {
		printk(KERN_NOTICE
		       "RAMDISK: ext2 filesystem found at block %d\n",
		       start_block);
		nblocks = le32_to_cpu(ext2sb->s_blocks_count);
		goto done;
	}

	printk(KERN_NOTICE
	       "RAMDISK: Couldn't find valid RAM disk image starting at %d.\n",
	       start_block);
	
done:
	lseek(fd, start_block * BLOCK_SIZE, 0);
	kfree(buf);
	return nblocks;
}
#endif

static int __init rd_load_image(char *from)
{
	int res = 0;

#ifdef CONFIG_BLK_DEV_RAM
	int in_fd, out_fd;
	unsigned long rd_blocks, devblocks;
	int nblocks, i, disk;
	char *buf;
	unsigned short rotate = 0;
#if !defined(CONFIG_ARCH_S390) && !defined(CONFIG_PPC_ISERIES)
	char rotator[4] = { '|' , '/' , '-' , '\\' };
#endif

	out_fd = open("/dev/ram", O_RDWR, 0);
	if (out_fd < 0)
		goto out;

	in_fd = open(from, O_RDONLY, 0);
	if (in_fd < 0)
		goto noclose_input;

	nblocks = identify_ramdisk_image(in_fd, rd_image_start);
	if (nblocks < 0)
		goto done;

	if (nblocks == 0) {
#ifdef BUILD_CRAMDISK
		if (crd_load(in_fd, out_fd) == 0)
			goto successful_load;
#else
		printk(KERN_NOTICE
		       "RAMDISK: Kernel does not support compressed "
		       "RAM disk images\n");
#endif
		goto done;
	}

	/*
	 * NOTE NOTE: nblocks suppose that the blocksize is BLOCK_SIZE, so
	 * rd_load_image will work only with filesystem BLOCK_SIZE wide!
	 * So make sure to use 1k blocksize while generating ext2fs
	 * ramdisk-images.
	 */
	if (sys_ioctl(out_fd, BLKGETSIZE, (unsigned long)&rd_blocks) < 0)
		rd_blocks = 0;
	else
		rd_blocks >>= 1;

	if (nblocks > rd_blocks) {
		printk("RAMDISK: image too big! (%d/%ld blocks)\n",
		       nblocks, rd_blocks);
		goto done;
	}
		
	/*
	 * OK, time to copy in the data
	 */
	if (sys_ioctl(in_fd, BLKGETSIZE, (unsigned long)&devblocks) < 0)
		devblocks = 0;
	else
		devblocks >>= 1;

	if (strcmp(from, "/dev/initrd") == 0)
		devblocks = nblocks;

	if (devblocks == 0) {
		printk(KERN_ERR "RAMDISK: could not determine device size\n");
		goto done;
	}

	buf = kmalloc(BLOCK_SIZE, GFP_KERNEL);
	if (buf == 0) {
		printk(KERN_ERR "RAMDISK: could not allocate buffer\n");
		goto done;
	}

	printk(KERN_NOTICE "RAMDISK: Loading %d blocks [%ld disk%s] into ram disk... ", 
		nblocks, ((nblocks-1)/devblocks)+1, nblocks>devblocks ? "s" : "");
	for (i = 0, disk = 1; i < nblocks; i++) {
		if (i && (i % devblocks == 0)) {
			printk("done disk #%d.\n", disk++);
			rotate = 0;
			if (close(in_fd)) {
				printk("Error closing the disk.\n");
				goto noclose_input;
			}
			change_floppy("disk #%d", disk);
			in_fd = open(from, O_RDONLY, 0);
			if (in_fd < 0)  {
				printk("Error opening disk.\n");
				goto noclose_input;
			}
			printk("Loading disk #%d... ", disk);
		}
		read(in_fd, buf, BLOCK_SIZE);
		write(out_fd, buf, BLOCK_SIZE);
#if !defined(CONFIG_ARCH_S390) && !defined(CONFIG_PPC_ISERIES)
		if (!(i % 16)) {
			printk("%c\b", rotator[rotate & 0x3]);
			rotate++;
		}
#endif
	}
	printk("done.\n");
	kfree(buf);

successful_load:
	res = 1;
done:
	close(in_fd);
noclose_input:
	close(out_fd);
out:
	sys_unlink("/dev/ram");
#endif
	return res;
}

static int __init rd_load_disk(int n)
{
#ifdef CONFIG_BLK_DEV_RAM
	if (rd_prompt)
		change_floppy("root floppy disk to be loaded into RAM disk");
	create_dev("/dev/ram", MKDEV(RAMDISK_MAJOR, n), NULL);
#endif
	return rd_load_image("/dev/root");
}

static void __init mount_root(void)
{
#ifdef CONFIG_ROOT_NFS
	if (MAJOR(ROOT_DEV) == UNNAMED_MAJOR) {
		if (mount_nfs_root()) {
			sys_chdir("/root");
			ROOT_DEV = current->fs->pwdmnt->mnt_sb->s_dev;
			printk("VFS: Mounted root (nfs filesystem).\n");
			return;
		}
		printk(KERN_ERR "VFS: Unable to mount root fs via NFS, trying floppy.\n");
		ROOT_DEV = Root_FD0;
	}
#endif
	create_dev("/dev/root", ROOT_DEV, root_device_name);
#ifdef CONFIG_BLK_DEV_FD
	if (MAJOR(ROOT_DEV) == FLOPPY_MAJOR) {
		/* rd_doload is 2 for a dual initrd/ramload setup */
		if (rd_doload==2) {
			if (rd_load_disk(1)) {
				ROOT_DEV = Root_RAM1;
				create_dev("/dev/root", ROOT_DEV, NULL);
			}
		} else
			change_floppy("root floppy");
	}
#endif
	mount_block_root("/dev/root", root_mountflags);
}

#ifdef CONFIG_BLK_DEV_INITRD

unsigned int real_root_dev;	/* do_proc_dointvec cannot handle kdev_t */
static int __initdata old_fd, root_fd;
static int __initdata mount_initrd = 1;

static int __init no_initrd(char *str)
{
	mount_initrd = 0;
	return 1;
}

__setup("noinitrd", no_initrd);

static int __init do_linuxrc(void * shell)
{
	static char *argv[] = { "linuxrc", NULL, };
	extern char * envp_init[];

	close(old_fd);close(root_fd);
	close(0);close(1);close(2);
	setsid();
	(void) open("/dev/console",O_RDWR,0);
	(void) dup(0);
	(void) dup(0);
	return execve(shell, argv, envp_init);
}

static void __init handle_initrd(void)
{
	int error;
	int i, pid;

	real_root_dev = ROOT_DEV;
	create_dev("/dev/root.old", Root_RAM0, NULL);
	/* mount initrd on rootfs' /root */
	mount_block_root("/dev/root.old", root_mountflags & ~MS_RDONLY);
	sys_mkdir("/old", 0700);
	root_fd = open("/", 0, 0);
	old_fd = open("/old", 0, 0);
	/* move initrd over / and chdir/chroot in initrd root */
	sys_chdir("/root");
	sys_mount(".", "/", NULL, MS_MOVE, NULL);
	sys_chroot(".");
	mount_devfs_fs ();

	pid = kernel_thread(do_linuxrc, "/linuxrc", SIGCHLD);
	if (pid > 0) {
		while (pid != waitpid(-1, &i, 0))
			yield();
	}

	/* move initrd to rootfs' /old */
	sys_fchdir(old_fd);
	sys_mount("/", ".", NULL, MS_MOVE, NULL);
	/* switch root and cwd back to / of rootfs */
	sys_fchdir(root_fd);
	sys_chroot(".");
	close(old_fd);
	close(root_fd);
	umount_devfs("/old/dev");

	if (real_root_dev == Root_RAM0) {
		sys_chdir("/old");
		return;
	}

	ROOT_DEV = real_root_dev;
	mount_root();

	printk(KERN_NOTICE "Trying to move old root to /initrd ... ");
	error = sys_mount("/old", "/root/initrd", NULL, MS_MOVE, NULL);
	if (!error)
		printk("okay\n");
	else {
		int fd = open("/dev/root.old", O_RDWR, 0);
		printk("failed\n");
		printk(KERN_NOTICE "Unmounting old root\n");
		sys_umount("/old", MNT_DETACH);
		printk(KERN_NOTICE "Trying to free ramdisk memory ... ");
		if (fd < 0) {
			error = fd;
		} else {
			error = sys_ioctl(fd, BLKFLSBUF, 0);
			close(fd);
		}
		printk(!error ? "okay\n" : "failed\n");
	}
}

static int __init initrd_load(void)
{
	if (!mount_initrd)
		return 0;

	create_dev("/dev/ram", MKDEV(RAMDISK_MAJOR, 0), NULL);
	create_dev("/dev/initrd", MKDEV(RAMDISK_MAJOR, INITRD_MINOR), NULL);
	/* Load the initrd data into /dev/ram0. Execute it as initrd unless
	 * /dev/ram0 is supposed to be our actual root device, in
	 * that case the ram disk is just set up here, and gets
	 * mounted in the normal path. */
	if (rd_load_image("/dev/initrd") && ROOT_DEV != Root_RAM0) {
		handle_initrd();
		return 1;
	}
	return 0;
}
#else
static inline int initrd_load(void) { return 0; }
#endif

/*
 * Prepare the namespace - decide what/where to mount, load ramdisks, etc.
 */
void __init prepare_namespace(void)
{
	int is_floppy;

	mount_devfs();

	md_run_setup();

	if (saved_root_name[0]) {
		root_device_name = saved_root_name;
		ROOT_DEV = name_to_dev_t(root_device_name);
		if (strncmp(root_device_name, "/dev/", 5) == 0)
			root_device_name += 5;
	}

	is_floppy = MAJOR(ROOT_DEV) == FLOPPY_MAJOR;

	create_dev("/dev/root", ROOT_DEV, NULL);

	/* This has to be before mounting root, because even readonly mount of reiserfs would replay
	   log corrupting stuff */
	software_resume();

	if (initrd_load())
		goto out;

	if (is_floppy && rd_doload && rd_load_disk(0))
		ROOT_DEV = Root_RAM0;

	mount_root();
out:
	umount_devfs("/dev");
	sys_mount(".", "/", NULL, MS_MOVE, NULL);
	sys_chroot(".");
	security_sb_post_mountroot();
	mount_devfs_fs ();
}

#if defined(BUILD_CRAMDISK) && defined(CONFIG_BLK_DEV_RAM)

/*
 * gzip declarations
 */

#define OF(args)  args

#ifndef memzero
#define memzero(s, n)     memset ((s), 0, (n))
#endif

typedef unsigned char  uch;
typedef unsigned short ush;
typedef unsigned long  ulg;

#define INBUFSIZ 4096
#define WSIZE 0x8000    /* window size--must be a power of two, and */
			/*  at least 32K for zip's deflate method */

static uch *inbuf;
static uch *window;

static unsigned insize;  /* valid bytes in inbuf */
static unsigned inptr;   /* index of next byte to be processed in inbuf */
static unsigned outcnt;  /* bytes in output buffer */
static int exit_code;
static int unzip_error;
static long bytes_out;
static int crd_infd, crd_outfd;

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

#include "../lib/inflate.c"

static void __init *malloc(int size)
{
	return kmalloc(size, GFP_KERNEL);
}

static void __init free(void *where)
{
	kfree(where);
}

static void __init gzip_mark(void **ptr)
{
}

static void __init gzip_release(void **ptr)
{
}


/* ===========================================================================
 * Fill the input buffer. This is called only when the buffer is empty
 * and at least one byte is really needed.
 * Returning -1 does not guarantee that gunzip() will ever return.
 */
static int __init fill_inbuf(void)
{
	if (exit_code) return -1;
	
	insize = read(crd_infd, inbuf, INBUFSIZ);
	if (insize == 0) {
		error("RAMDISK: ran out of compressed data\n");
		return -1;
	}

	inptr = 1;

	return inbuf[0];
}

/* ===========================================================================
 * Write the output window window[0..outcnt-1] and update crc and bytes_out.
 * (Used for the decompressed data only.)
 */
static void __init flush_window(void)
{
    ulg c = crc;         /* temporary variable */
    unsigned n, written;
    uch *in, ch;
    
    written = write(crd_outfd, window, outcnt);
    if (written != outcnt && unzip_error == 0) {
	printk(KERN_ERR "RAMDISK: incomplete write (%d != %d) %ld\n",
	       written, outcnt, bytes_out);
	unzip_error = 1;
    }
    in = window;
    for (n = 0; n < outcnt; n++) {
	    ch = *in++;
	    c = crc_32_tab[((int)c ^ ch) & 0xff] ^ (c >> 8);
    }
    crc = c;
    bytes_out += (ulg)outcnt;
    outcnt = 0;
}

static void __init error(char *x)
{
	printk(KERN_ERR "%s", x);
	exit_code = 1;
	unzip_error = 1;
}

static int __init crd_load(int in_fd, int out_fd)
{
	int result;

	insize = 0;		/* valid bytes in inbuf */
	inptr = 0;		/* index of next byte to be processed in inbuf */
	outcnt = 0;		/* bytes in output buffer */
	exit_code = 0;
	bytes_out = 0;
	crc = (ulg)0xffffffffL; /* shift register contents */

	crd_infd = in_fd;
	crd_outfd = out_fd;
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
	if (unzip_error)
		result = 1;
	kfree(inbuf);
	kfree(window);
	return result;
}

#endif  /* BUILD_CRAMDISK && CONFIG_BLK_DEV_RAM */

