/*
 * /dev/nvram driver for Power Macintosh.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/fcntl.h>
#include <linux/nvram.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/init.h>

#define NVRAM_SIZE	8192

__openfirmware

static long long nvram_llseek(struct file *file, loff_t offset, int origin)
{
	switch (origin) {
	case 1:
		offset += file->f_pos;
		break;
	case 2:
		offset += NVRAM_SIZE;
		break;
	}
	if (offset < 0)
		return -EINVAL;
	file->f_pos = offset;
	return file->f_pos;
}

static ssize_t read_nvram(struct file *file, char *buf,
			  size_t count, loff_t *ppos)
{
	unsigned int i;
	char *p = buf;

	if (verify_area(VERIFY_WRITE, buf, count))
		return -EFAULT;
	if (*ppos >= NVRAM_SIZE)
		return 0;
	for (i = *ppos; count > 0 && i < NVRAM_SIZE; ++i, ++p, --count)
		if (__put_user(nvram_read_byte(i), p))
			return -EFAULT;
	*ppos = i;
	return p - buf;
}

static ssize_t write_nvram(struct file *file, const char *buf,
			   size_t count, loff_t *ppos)
{
	unsigned int i;
	const char *p = buf;
	char c;

	if (verify_area(VERIFY_READ, buf, count))
		return -EFAULT;
	if (*ppos >= NVRAM_SIZE)
		return 0;
	for (i = *ppos; count > 0 && i < NVRAM_SIZE; ++i, ++p, --count) {
		if (__get_user(c, p))
			return -EFAULT;
		nvram_write_byte(c, i);
	}
	*ppos = i;
	return p - buf;
}

static int nvram_open(struct inode *inode, struct file *file)
{
	return 0;
}

struct file_operations nvram_fops = {
	nvram_llseek,
	read_nvram,
	write_nvram,
	NULL,		/* nvram_readdir */
	NULL,		/* nvram_select */
	NULL,		/* nvram_ioctl */
	NULL,		/* nvram_mmap */
	nvram_open,
	NULL,		/* no special release code */
	NULL		/* fsync */
};

static struct miscdevice nvram_dev = {
	NVRAM_MINOR,
	"nvram",
	&nvram_fops
};

__initfunc(int nvram_init(void))
{
	misc_register(&nvram_dev);
	return 0;
}
