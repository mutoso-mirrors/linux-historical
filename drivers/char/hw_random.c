/*
 	Hardware driver for the Intel/AMD Random Number Generators (RNG)
	(c) Copyright 2003 Red Hat Inc <jgarzik@redhat.com>
 
 	derived from
 
        Hardware driver for the AMD 768 Random Number Generator (RNG)
        (c) Copyright 2001 Red Hat Inc <alan@redhat.com>

 	derived from
 
	Hardware driver for Intel i810 Random Number Generator (RNG)
	Copyright 2000,2001 Jeff Garzik <jgarzik@pobox.com>
	Copyright 2000,2001 Philipp Rumpf <prumpf@mandrakesoft.com>

	Please read Documentation/hw_random.txt for details on use.

	----------------------------------------------------------
	This software may be used and distributed according to the terms
        of the GNU General Public License, incorporated herein by reference.

 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/random.h>
#include <linux/miscdevice.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/uaccess.h>


/*
 * core module and version information
 */
#define RNG_VERSION "0.9.0"
#define RNG_MODULE_NAME "hw_random"
#define RNG_DRIVER_NAME   RNG_MODULE_NAME " hardware driver " RNG_VERSION
#define PFX RNG_MODULE_NAME ": "


/*
 * debugging macros
 */
#undef RNG_DEBUG /* define to enable copious debugging info */

#ifdef RNG_DEBUG
/* note: prints function name for you */
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#define RNG_NDEBUG        /* define to disable lightweight runtime checks */
#ifdef RNG_NDEBUG
#define assert(expr)
#else
#define assert(expr) \
        if(!(expr)) {                                   \
        printk( "Assertion failed! %s,%s,%s,line=%d\n", \
        #expr,__FILE__,__FUNCTION__,__LINE__);          \
        }
#endif

#define RNG_MISCDEV_MINOR		183 /* official */

static int rng_dev_open (struct inode *inode, struct file *filp);
static int rng_dev_release (struct inode *inode, struct file *filp);
static ssize_t rng_dev_read (struct file *filp, char *buf, size_t size,
			     loff_t * offp);

static int __init intel_init (struct pci_dev *dev);
static void __exit intel_cleanup(void);
static unsigned int intel_data_present (void);
static u32 intel_data_read (void);

static int __init amd_init (struct pci_dev *dev);
static void __exit amd_cleanup(void);
static unsigned int amd_data_present (void);
static u32 amd_data_read (void);

struct rng_operations {
	int (*init) (struct pci_dev *dev);
	void (*cleanup) (void);
	unsigned int (*data_present) (void);
	u32 (*data_read) (void);
	unsigned int n_bytes; /* number of bytes per ->data_read */
};
static struct rng_operations *rng_ops;

static struct semaphore rng_open_sem;	/* Semaphore for serializing rng_open/release */

static struct file_operations rng_chrdev_ops = {
	.owner		= THIS_MODULE,
	.open		= rng_dev_open,
	.release	= rng_dev_release,
	.read		= rng_dev_read,
};


static struct miscdevice rng_miscdev = {
	RNG_MISCDEV_MINOR,
	RNG_MODULE_NAME,
	&rng_chrdev_ops,
};

enum {
	rng_hw_none,
	rng_hw_intel,
	rng_hw_amd,
};

static struct rng_operations rng_vendor_ops[] __initdata = {
	/* rng_hw_none */
	{ },

	/* rng_hw_intel */
	{ intel_init, intel_cleanup, intel_data_present,
	  intel_data_read, 1 },

	/* rng_hw_amd */
	{ amd_init, amd_cleanup, amd_data_present, amd_data_read, 4 },
};

/*
 * Data for PCI driver interface
 *
 * This data only exists for exporting the supported
 * PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 * register a pci_driver, because someone else might one day
 * want to register another driver on the same PCI id.
 */
static struct pci_device_id rng_pci_tbl[] __initdata = {
	{ 0x1022, 0x7443, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_amd },

	{ 0x8086, 0x2418, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_intel },
	{ 0x8086, 0x2428, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_intel },
	{ 0x8086, 0x2448, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_intel },
	{ 0x8086, 0x244e, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_intel },
	{ 0x8086, 0x245e, PCI_ANY_ID, PCI_ANY_ID, 0, 0, rng_hw_intel },

	{ 0, },	/* terminate list */
};
MODULE_DEVICE_TABLE (pci, rng_pci_tbl);


/***********************************************************************
 *
 * Intel RNG operations
 *
 */

/*
 * RNG registers (offsets from rng_mem)
 */
#define INTEL_RNG_HW_STATUS			0
#define         INTEL_RNG_PRESENT		0x40
#define         INTEL_RNG_ENABLED		0x01
#define INTEL_RNG_STATUS			1
#define         INTEL_RNG_DATA_PRESENT		0x01
#define INTEL_RNG_DATA				2

/*
 * Magic address at which Intel PCI bridges locate the RNG
 */
#define INTEL_RNG_ADDR				0xFFBC015F
#define INTEL_RNG_ADDR_LEN			3

/* token to our ioremap'd RNG register area */
static void *rng_mem;

static inline u8 intel_hwstatus (void)
{
	assert (rng_mem != NULL);
	return readb (rng_mem + INTEL_RNG_HW_STATUS);
}

static inline u8 intel_hwstatus_set (u8 hw_status)
{
	assert (rng_mem != NULL);
	writeb (hw_status, rng_mem + INTEL_RNG_HW_STATUS);
	return intel_hwstatus ();
}

static unsigned int intel_data_present(void)
{
	assert (rng_mem != NULL);

	return (readb (rng_mem + INTEL_RNG_STATUS) & INTEL_RNG_DATA_PRESENT) ?
		1 : 0;
}

static u32 intel_data_read(void)
{
	assert (rng_mem != NULL);

	return readb (rng_mem + INTEL_RNG_DATA);
}

static int __init intel_init (struct pci_dev *dev)
{
	int rc;
	u8 hw_status;

	DPRINTK ("ENTER\n");

	rng_mem = ioremap (INTEL_RNG_ADDR, INTEL_RNG_ADDR_LEN);
	if (rng_mem == NULL) {
		printk (KERN_ERR PFX "cannot ioremap RNG Memory\n");
		rc = -EBUSY;
		goto err_out;
	}

	/* Check for Intel 82802 */
	hw_status = intel_hwstatus ();
	if ((hw_status & INTEL_RNG_PRESENT) == 0) {
		printk (KERN_ERR PFX "RNG not detected\n");
		rc = -ENODEV;
		goto err_out_free_map;
	}

	/* turn RNG h/w on, if it's off */
	if ((hw_status & INTEL_RNG_ENABLED) == 0)
		hw_status = intel_hwstatus_set (hw_status | INTEL_RNG_ENABLED);
	if ((hw_status & INTEL_RNG_ENABLED) == 0) {
		printk (KERN_ERR PFX "cannot enable RNG, aborting\n");
		rc = -EIO;
		goto err_out_free_map;
	}

	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out_free_map:
	iounmap (rng_mem);
	rng_mem = NULL;
err_out:
	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}

static void __exit intel_cleanup(void)
{
	u8 hw_status;

	hw_status = intel_hwstatus ();
	if (hw_status & INTEL_RNG_ENABLED)
		intel_hwstatus_set (hw_status & ~INTEL_RNG_ENABLED);
	else
		printk(KERN_WARNING PFX "unusual: RNG already disabled\n");
	iounmap(rng_mem);
	rng_mem = NULL;
}

/***********************************************************************
 *
 * AMD RNG operations
 *
 */

static u32 pmbase;			/* PMxx I/O base */
static struct pci_dev *amd_dev;

static unsigned int amd_data_present (void)
{
      	return inl(pmbase + 0xF4) & 1;
}


static u32 amd_data_read (void)
{
	return inl(pmbase + 0xF0);
}

static int __init amd_init (struct pci_dev *dev)
{
	int rc;
	u8 rnen;

	DPRINTK ("ENTER\n");

	pci_read_config_dword(dev, 0x58, &pmbase);

	pmbase &= 0x0000FF00;

	if (pmbase == 0)
	{
		printk (KERN_ERR PFX "power management base not set\n");
		rc = -EIO;
		goto err_out;
	}

	pci_read_config_byte(dev, 0x40, &rnen);
	rnen |= (1 << 7);	/* RNG on */
	pci_write_config_byte(dev, 0x40, rnen);

	pci_read_config_byte(dev, 0x41, &rnen);
	rnen |= (1 << 7);	/* PMIO enable */
	pci_write_config_byte(dev, 0x41, rnen);

	printk(KERN_INFO PFX "AMD768 system management I/O registers at 0x%X.\n", pmbase);

	amd_dev = dev;

	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out:
	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}

static void __exit amd_cleanup(void)
{
	u8 rnen;

	pci_read_config_byte(amd_dev, 0x40, &rnen);
	rnen &= ~(1 << 7);	/* RNG off */
	pci_write_config_byte(amd_dev, 0x40, rnen);

	/* FIXME: twiddle pmio, also? */
}

/***********************************************************************
 *
 * /dev/hwrandom character device handling (major 10, minor 183)
 *
 */

static int rng_dev_open (struct inode *inode, struct file *filp)
{
	if ((filp->f_mode & FMODE_READ) == 0)
		return -EINVAL;
	if (filp->f_mode & FMODE_WRITE)
		return -EINVAL;

	/* wait for device to become free */
	if (filp->f_flags & O_NONBLOCK) {
		if (down_trylock (&rng_open_sem))
			return -EAGAIN;
	} else {
		if (down_interruptible (&rng_open_sem))
			return -ERESTARTSYS;
	}
	return 0;
}


static int rng_dev_release (struct inode *inode, struct file *filp)
{
	up(&rng_open_sem);
	return 0;
}


static ssize_t rng_dev_read (struct file *filp, char *buf, size_t size,
			     loff_t * offp)
{
	static spinlock_t rng_lock = SPIN_LOCK_UNLOCKED;
	unsigned int have_data;
	u32 data = 0;
	ssize_t ret = 0;

	while (size) {
		spin_lock(&rng_lock);

		have_data = 0;
		if (rng_ops->data_present()) {
			data = rng_ops->data_read();
			have_data = rng_ops->n_bytes;
		}

		spin_unlock (&rng_lock);

		while (have_data > 0) {
			if (put_user((u8)data, buf++)) {
				ret = ret ? : -EFAULT;
				break;
			}
			size--;
			ret++;
			have_data--;
			data>>=8;
		}

		if (filp->f_flags & O_NONBLOCK)
			return ret ? : -EAGAIN;

		if(need_resched())
		{
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}
		else
			udelay(200);	/* FIXME: We could poll for 250uS ?? */

		if (signal_pending (current))
			return ret ? : -ERESTARTSYS;
	}
	return ret;
}



/*
 * rng_init_one - look for and attempt to init a single RNG
 */
static int __init rng_init_one (struct pci_dev *dev)
{
	int rc;

	DPRINTK ("ENTER\n");

	assert(rng_ops != NULL);

	rc = rng_ops->init(dev);
	if (rc)
		goto err_out;

	rc = misc_register (&rng_miscdev);
	if (rc) {
		printk (KERN_ERR PFX "misc device register failed\n");
		goto err_out_cleanup_hw;
	}

	DPRINTK ("EXIT, returning 0\n");
	return 0;

err_out_cleanup_hw:
	rng_ops->cleanup();
err_out:
	DPRINTK ("EXIT, returning %d\n", rc);
	return rc;
}



MODULE_AUTHOR("The Linux Kernel team");
MODULE_DESCRIPTION("H/W Random Number Generator (RNG) driver");
MODULE_LICENSE("GPL");


/*
 * rng_init - initialize RNG module
 */
static int __init rng_init (void)
{
	int rc;
	struct pci_dev *pdev;
	const struct pci_device_id *ent;

	DPRINTK ("ENTER\n");

	init_MUTEX (&rng_open_sem);

	/* Probe for Intel, AMD RNGs */
	pci_for_each_dev(pdev) {
		ent = pci_match_device (rng_pci_tbl, pdev);
		if (ent) {
			rng_ops = &rng_vendor_ops[ent->driver_data];
			goto match;
		}
	}

	DPRINTK ("EXIT, returning -ENODEV\n");
	return -ENODEV;

match:
	rc = rng_init_one (pdev);
	if (rc)
		return rc;

	printk (KERN_INFO RNG_DRIVER_NAME " loaded\n");

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


/*
 * rng_init - shutdown RNG module
 */
static void __exit rng_cleanup (void)
{
	DPRINTK ("ENTER\n");

	misc_deregister (&rng_miscdev);

	if (rng_ops->cleanup)
		rng_ops->cleanup();

	DPRINTK ("EXIT\n");
}


module_init (rng_init);
module_exit (rng_cleanup);
