/*
 * driver/usb/usb-core.c
 *
 * (C) Copyright David Waite 1999
 * based on code from usb.c, by Linus Torvalds
 *
 * The purpose of this file is to pull any and all generic modular code from
 * usb.c and put it in a separate file. This way usb.c is kept as a generic
 * library, while this file handles starting drivers, etc.
 *
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>

/*
 * USB core
 */

int usb_hub_init(void);
void usb_hub_cleanup(void);
int usb_major_init(void);
void usb_major_cleanup(void);


/*
 * Cleanup
 */

static void __exit usb_exit(void)
{
	usb_major_cleanup();
	usbdevfs_cleanup();
	usb_hub_cleanup();
}

/*
 * Init
 */

static int __init usb_init(void)
{
	usb_major_init();
	usbdevfs_init();
	usb_hub_init();

	return 0;
}

module_init(usb_init);
module_exit(usb_exit);
