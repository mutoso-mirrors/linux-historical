#define __KERNEL_SYSCALLS__
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/init.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/major.h>
#include <linux/root_dev.h>

asmlinkage long sys_unlink(const char *name);
asmlinkage long sys_mknod(const char *name, int mode, unsigned dev);
asmlinkage long sys_newstat(char * filename, struct stat * statbuf);
asmlinkage long sys_ioctl(int fd, int cmd, unsigned long arg);
asmlinkage long sys_mkdir(const char *name, int mode);
asmlinkage long sys_rmdir(const char *name);
asmlinkage long sys_chdir(const char *name);
asmlinkage long sys_fchdir(int fd);
asmlinkage long sys_chroot(const char *name);
asmlinkage long sys_mount(char *dev_name, char *dir_name, char *type,
				 unsigned long flags, void *data);
asmlinkage long sys_umount(char *name, int flags);

dev_t name_to_dev_t(char *name);
void  change_floppy(char *fmt, ...);
void  mount_block_root(char *name, int flags);
void  mount_root(void);
extern int root_mountflags;
extern char *root_device_name;

#ifdef CONFIG_DEVFS_FS

void mount_devfs(void);
void umount_devfs(char *path);
int  create_dev(char *name, dev_t dev, char *devfs_name);

#else

static inline void mount_devfs(void) {}
static inline void umount_devfs(const char *path) {}

static inline int create_dev(char *name, dev_t dev, char *devfs_name)
{
	sys_unlink(name);
	return sys_mknod(name, S_IFBLK|0600, new_encode_dev(dev));
}

#endif

#if BITS_PER_LONG == 32
asmlinkage long sys_stat64(char *name, struct stat64 *stat);
static inline u32 bstat(char *name)
{
	struct stat64 stat;
	if (sys_stat64(name, &stat) != 0)
		return 0;
	if (!S_ISBLK(stat.st_mode))
		return 0;
	if (stat.st_rdev != (u32)stat.st_rdev)
		return 0;
	return stat.st_rdev;
}
#else
static inline u32 bstat(char *name)
{
	struct stat stat;
	if (sys_newstat(name, &stat) != 0)
		return 0;
	if (!S_ISBLK(stat.st_mode))
		return 0;
	return stat.st_rdev;
}
#endif

#ifdef CONFIG_BLK_DEV_RAM

int __init rd_load_disk(int n);
int __init rd_load_image(char *from);

#else

static inline int rd_load_disk(int n) { return 0; }
static inline int rd_load_image(char *from) { return 0; }

#endif

#ifdef CONFIG_BLK_DEV_INITRD

int __init initrd_load(void);

#else

static inline int initrd_load(void) { return 0; }

#endif

#ifdef CONFIG_BLK_DEV_MD

void md_run_setup(void);

#else

static inline void md_run_setup(void) {}

#endif
