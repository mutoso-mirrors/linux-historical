/*  devfs (Device FileSystem) utilities.

    Copyright (C) 1999-2002  Richard Gooch

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Richard Gooch may be reached by email at  rgooch@atnf.csiro.au
    The postal address is:
      Richard Gooch, c/o ATNF, P. O. Box 76, Epping, N.S.W., 2121, Australia.

    ChangeLog

    19991031   Richard Gooch <rgooch@atnf.csiro.au>
               Created.
    19991103   Richard Gooch <rgooch@atnf.csiro.au>
               Created <_devfs_convert_name> and supported SCSI and IDE CD-ROMs
    20000203   Richard Gooch <rgooch@atnf.csiro.au>
               Changed operations pointer type to void *.
    20000621   Richard Gooch <rgooch@atnf.csiro.au>
               Changed interface to <devfs_register_series>.
    20000622   Richard Gooch <rgooch@atnf.csiro.au>
               Took account of interface change to <devfs_mk_symlink>.
               Took account of interface change to <devfs_mk_dir>.
    20010519   Richard Gooch <rgooch@atnf.csiro.au>
               Documentation cleanup.
    20010709   Richard Gooch <rgooch@atnf.csiro.au>
               Created <devfs_*alloc_major> and <devfs_*alloc_devnum>.
    20010710   Richard Gooch <rgooch@atnf.csiro.au>
               Created <devfs_*alloc_unique_number>.
    20010730   Richard Gooch <rgooch@atnf.csiro.au>
               Documentation typo fix.
    20010806   Richard Gooch <rgooch@atnf.csiro.au>
               Made <block_semaphore> and <char_semaphore> private.
    20010813   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed bug in <devfs_alloc_unique_number>: limited to 128 numbers
    20010818   Richard Gooch <rgooch@atnf.csiro.au>
               Updated major masks up to Linus' "no new majors" proclamation.
	       Block: were 126 now 122 free, char: were 26 now 19 free.
    20020324   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed bug in <devfs_alloc_unique_number>: was clearing beyond
	       bitfield.
    20020326   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed bitfield data type for <devfs_*alloc_devnum>.
               Made major bitfield type and initialiser 64 bit safe.
    20020413   Richard Gooch <rgooch@atnf.csiro.au>
               Fixed shift warning on 64 bit machines.
    20020428   Richard Gooch <rgooch@atnf.csiro.au>
               Copied and used macro for error messages from fs/devfs/base.c 
    20021013   Richard Gooch <rgooch@atnf.csiro.au>
               Documentation fix.
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <asm/bitops.h>

#define PRINTK(format, args...) \
   {printk (KERN_ERR "%s" format, __FUNCTION__ , ## args);}


/*  Private functions follow  */

/**
 *	devfs_register_tape - Register a tape device in the "/dev/tapes" hierarchy.
 *	@de: Any tape device entry in the device directory.
 */

int devfs_register_tape (devfs_handle_t de)
{
    int pos;
    devfs_handle_t slave;
    char name[32], dest[64];
    static unsigned int tape_counter;
    int n = tape_counter++;

    pos = devfs_generate_path (de, dest + 3, sizeof dest - 3);
    if (pos < 0) return -1;
    strncpy (dest + pos, "../", 3);
    sprintf (name, "tapes/tape%u", n);
    devfs_mk_symlink (NULL, name, DEVFS_FL_DEFAULT, dest + pos, &slave, NULL);
    return n;
}   /*  End Function devfs_register_tape  */
EXPORT_SYMBOL(devfs_register_tape);

void devfs_unregister_tape(int num)
{
	if (num >= 0)
		devfs_remove("tapes/tape%u", num);
}

EXPORT_SYMBOL(devfs_unregister_tape);

struct major_list
{
    spinlock_t lock;
    unsigned long bits[256 / BITS_PER_LONG];
};
#if BITS_PER_LONG == 32
#  define INITIALISER64(low,high) (low), (high)
#else
#  define INITIALISER64(low,high) ( (unsigned long) (high) << 32 | (low) )
#endif

/*  Block majors already assigned:
    0-3, 7-9, 11-63, 65-99, 101-113, 120-127, 199, 201, 240-255
    Total free: 122
*/
static struct major_list block_major_list =
{SPIN_LOCK_UNLOCKED,
    {INITIALISER64 (0xfffffb8f, 0xffffffff),  /*  Majors 0-31,    32-63    */
     INITIALISER64 (0xfffffffe, 0xff03ffef),  /*  Majors 64-95,   96-127   */
     INITIALISER64 (0x00000000, 0x00000000),  /*  Majors 128-159, 160-191  */
     INITIALISER64 (0x00000280, 0xffff0000),  /*  Majors 192-223, 224-255  */
    }
};

/*  Char majors already assigned:
    0-7, 9-151, 154-158, 160-211, 216-221, 224-230, 240-255
    Total free: 19
*/
static struct major_list char_major_list =
{SPIN_LOCK_UNLOCKED,
    {INITIALISER64 (0xfffffeff, 0xffffffff),  /*  Majors 0-31,    32-63    */
     INITIALISER64 (0xffffffff, 0xffffffff),  /*  Majors 64-95,   96-127   */
     INITIALISER64 (0x7cffffff, 0xffffffff),  /*  Majors 128-159, 160-191  */
     INITIALISER64 (0x3f0fffff, 0xffff007f),  /*  Majors 192-223, 224-255  */
    }
};


/**
 *	devfs_alloc_major - Allocate a major number.
 *	@type: The type of the major (DEVFS_SPECIAL_CHR or DEVFS_SPECIAL_BLK)

 *	Returns the allocated major, else -1 if none are available.
 *	This routine is thread safe and does not block.
 */

int devfs_alloc_major (char type)
{
    int major;
    struct major_list *list;

    list = (type == DEVFS_SPECIAL_CHR) ? &char_major_list : &block_major_list;
    spin_lock (&list->lock);
    major = find_first_zero_bit (list->bits, 256);
    if (major < 256) __set_bit (major, list->bits);
    else major = -1;
    spin_unlock (&list->lock);
    return major;
}   /*  End Function devfs_alloc_major  */
EXPORT_SYMBOL(devfs_alloc_major);


/**
 *	devfs_dealloc_major - Deallocate a major number.
 *	@type: The type of the major (DEVFS_SPECIAL_CHR or DEVFS_SPECIAL_BLK)
 *	@major: The major number.
 *	This routine is thread safe and does not block.
 */

void devfs_dealloc_major (char type, int major)
{
    int was_set;
    struct major_list *list;

    if (major < 0) return;
    list = (type == DEVFS_SPECIAL_CHR) ? &char_major_list : &block_major_list;
    spin_lock (&list->lock);
    was_set = __test_and_clear_bit (major, list->bits);
    spin_unlock (&list->lock);
    if (!was_set) PRINTK ("(): major %d was already free\n", major);
}   /*  End Function devfs_dealloc_major  */
EXPORT_SYMBOL(devfs_dealloc_major);


struct minor_list
{
    int major;
    unsigned long bits[256 / BITS_PER_LONG];
    struct minor_list *next;
};

struct device_list
{
    struct minor_list *first, *last;
    int none_free;
};

static DECLARE_MUTEX (block_semaphore);
static struct device_list block_list;

static DECLARE_MUTEX (char_semaphore);
static struct device_list char_list;


/**
 *	devfs_alloc_devnum - Allocate a device number.
 *	@type: The type (DEVFS_SPECIAL_CHR or DEVFS_SPECIAL_BLK).
 *
 *	Returns the allocated device number, else NODEV if none are available.
 *	This routine is thread safe and may block.
 */

dev_t devfs_alloc_devnum (char type)
{
    int minor;
    struct semaphore *semaphore;
    struct device_list *list;
    struct minor_list *entry;

    if (type == DEVFS_SPECIAL_CHR)
    {
	semaphore = &char_semaphore;
	list = &char_list;
    }
    else
    {
	semaphore = &block_semaphore;
	list = &block_list;
    }
    if (list->none_free) return 0;  /*  Fast test  */
    down (semaphore);
    if (list->none_free)
    {
	up (semaphore);
	return 0;
    }
    for (entry = list->first; entry != NULL; entry = entry->next)
    {
	minor = find_first_zero_bit (entry->bits, 256);
	if (minor >= 256) continue;
	__set_bit (minor, entry->bits);
	up (semaphore);
	return MKDEV(entry->major, minor);
    }
    /*  Need to allocate a new major  */
    if ( ( entry = kmalloc (sizeof *entry, GFP_KERNEL) ) == NULL )
    {
	list->none_free = 1;
	up (semaphore);
	return 0;
    }
    memset (entry, 0, sizeof *entry);
    if ( ( entry->major = devfs_alloc_major (type) ) < 0 )
    {
	list->none_free = 1;
	up (semaphore);
	kfree (entry);
	return 0;
    }
    __set_bit (0, entry->bits);
    if (list->first == NULL) list->first = entry;
    else list->last->next = entry;
    list->last = entry;
    up (semaphore);
    return MKDEV(entry->major, 0);
}   /*  End Function devfs_alloc_devnum  */
EXPORT_SYMBOL(devfs_alloc_devnum);


/**
 *	devfs_dealloc_devnum - Dellocate a device number.
 *	@type: The type (DEVFS_SPECIAL_CHR or DEVFS_SPECIAL_BLK).
 *	@devnum: The device number.
 *
 *	This routine is thread safe and may block.
 */

void devfs_dealloc_devnum (char type, dev_t devnum)
{
    int major, minor;
    struct semaphore *semaphore;
    struct device_list *list;
    struct minor_list *entry;

    if (!devnum) return;
    if (type == DEVFS_SPECIAL_CHR)
    {
	semaphore = &char_semaphore;
	list = &char_list;
    }
    else
    {
	semaphore = &block_semaphore;
	list = &block_list;
    }
    major = MAJOR (devnum);
    minor = MINOR (devnum);
    down (semaphore);
    for (entry = list->first; entry != NULL; entry = entry->next)
    {
	int was_set;

	if (entry->major != major) continue;
	was_set = __test_and_clear_bit (minor, entry->bits);
	if (was_set) list->none_free = 0;
	up (semaphore);
	if (!was_set)
	    PRINTK ( "(): device %s was already free\n", kdevname (to_kdev_t(devnum)) );
	return;
    }
    up (semaphore);
    PRINTK ( "(): major for %s not previously allocated\n",
	     kdevname (to_kdev_t(devnum)) );
}   /*  End Function devfs_dealloc_devnum  */
EXPORT_SYMBOL(devfs_dealloc_devnum);


/**
 *	devfs_alloc_unique_number - Allocate a unique (positive) number.
 *	@space: The number space to allocate from.
 *
 *	Returns the allocated unique number, else a negative error code.
 *	This routine is thread safe and may block.
 */

int devfs_alloc_unique_number (struct unique_numspace *space)
{
    int number;
    unsigned int length;

    /*  Get around stupid lack of semaphore initialiser  */
    spin_lock (&space->init_lock);
    if (!space->sem_initialised)
    {
	sema_init (&space->semaphore, 1);
	space->sem_initialised = 1;
    }
    spin_unlock (&space->init_lock);
    down (&space->semaphore);
    if (space->num_free < 1)
    {
	void *bits;

	if (space->length < 16) length = 16;
	else length = space->length << 1;
	if ( ( bits = vmalloc (length) ) == NULL )
	{
	    up (&space->semaphore);
	    return -ENOMEM;
	}
	if (space->bits != NULL)
	{
	    memcpy (bits, space->bits, space->length);
	    vfree (space->bits);
	}
	space->num_free = (length - space->length) << 3;
	space->bits = bits;
	memset (bits + space->length, 0, length - space->length);
	space->length = length;
    }
    number = find_first_zero_bit (space->bits, space->length << 3);
    --space->num_free;
    __set_bit (number, space->bits);
    up (&space->semaphore);
    return number;
}   /*  End Function devfs_alloc_unique_number  */
EXPORT_SYMBOL(devfs_alloc_unique_number);


/**
 *	devfs_dealloc_unique_number - Deallocate a unique (positive) number.
 *	@space: The number space to deallocate from.
 *	@number: The number to deallocate.
 *
 *	This routine is thread safe and may block.
 */

void devfs_dealloc_unique_number (struct unique_numspace *space, int number)
{
    int was_set;

    if (number < 0) return;
    down (&space->semaphore);
    was_set = __test_and_clear_bit (number, space->bits);
    if (was_set) ++space->num_free;
    up (&space->semaphore);
    if (!was_set) PRINTK ("(): number %d was already free\n", number);
}   /*  End Function devfs_dealloc_unique_number  */
EXPORT_SYMBOL(devfs_dealloc_unique_number);
