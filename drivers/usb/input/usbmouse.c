/*
 * $Id: usbmouse.c,v 1.15 2001/12/27 10:37:41 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *
 *  USB HIDBP Mouse support
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or 
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.6"
#define DRIVER_AUTHOR "Vojtech Pavlik <vojtech@ucw.cz>"
#define DRIVER_DESC "USB HID Boot Protocol mouse driver"
#define DRIVER_LICENSE "GPL"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

struct usb_mouse {
	signed char data[8];
	char name[128];
	char phys[64];
	struct usb_device *usbdev;
	struct input_dev dev;
	struct urb *irq;
	int open;
};

static void usb_mouse_irq(struct urb *urb)
{
	struct usb_mouse *mouse = urb->context;
	signed char *data = mouse->data;
	struct input_dev *dev = &mouse->dev;

	if (urb->status) return;

	input_report_key(dev, BTN_LEFT,   data[0] & 0x01);
	input_report_key(dev, BTN_RIGHT,  data[0] & 0x02);
	input_report_key(dev, BTN_MIDDLE, data[0] & 0x04);
	input_report_key(dev, BTN_SIDE,   data[0] & 0x08);
	input_report_key(dev, BTN_EXTRA,  data[0] & 0x10);

	input_report_rel(dev, REL_X,     data[1]);
	input_report_rel(dev, REL_Y,     data[2]);
	input_report_rel(dev, REL_WHEEL, data[3]);

	input_sync(dev);
}

static int usb_mouse_open(struct input_dev *dev)
{
	struct usb_mouse *mouse = dev->private;

	if (mouse->open++)
		return 0;

	mouse->irq->dev = mouse->usbdev;
	if (usb_submit_urb(mouse->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void usb_mouse_close(struct input_dev *dev)
{
	struct usb_mouse *mouse = dev->private;

	if (!--mouse->open)
		usb_unlink_urb(mouse->irq);
}

static void *usb_mouse_probe(struct usb_device *dev, unsigned int ifnum,
			     const struct usb_device_id *id)
{
	struct usb_interface *iface;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_mouse *mouse;
	int pipe, maxp;
	char path[64];
	char *buf;

	iface = &dev->actconfig->interface[ifnum];
	interface = &iface->altsetting[iface->act_altsetting];

	if (interface->bNumEndpoints != 1) return NULL;

	endpoint = interface->endpoint + 0;
	if (!(endpoint->bEndpointAddress & 0x80)) return NULL;
	if ((endpoint->bmAttributes & 3) != 3) return NULL;

	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

	if (!(mouse = kmalloc(sizeof(struct usb_mouse), GFP_KERNEL))) return NULL;
	memset(mouse, 0, sizeof(struct usb_mouse));

	mouse->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!mouse->irq) {
		kfree(mouse);
		return NULL;
	}

	mouse->usbdev = dev;

	mouse->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	mouse->dev.keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_MIDDLE);
	mouse->dev.relbit[0] = BIT(REL_X) | BIT(REL_Y);
	mouse->dev.keybit[LONG(BTN_MOUSE)] |= BIT(BTN_SIDE) | BIT(BTN_EXTRA);
	mouse->dev.relbit[0] |= BIT(REL_WHEEL);

	mouse->dev.private = mouse;
	mouse->dev.open = usb_mouse_open;
	mouse->dev.close = usb_mouse_close;

	usb_make_path(dev, path, 64);
	sprintf(mouse->phys, "%s/input0", path);

	mouse->dev.name = mouse->name;
	mouse->dev.phys = mouse->phys;
	mouse->dev.id.bustype = BUS_USB;
	mouse->dev.id.vendor = dev->descriptor.idVendor;
	mouse->dev.id.product = dev->descriptor.idProduct;
	mouse->dev.id.version = dev->descriptor.bcdDevice;

	if (!(buf = kmalloc(63, GFP_KERNEL))) {
		kfree(mouse);
		return NULL;
	}

	if (dev->descriptor.iManufacturer &&
		usb_string(dev, dev->descriptor.iManufacturer, buf, 63) > 0)
			strcat(mouse->name, buf);
	if (dev->descriptor.iProduct &&
		usb_string(dev, dev->descriptor.iProduct, buf, 63) > 0)
			sprintf(mouse->name, "%s %s", mouse->name, buf);

	if (!strlen(mouse->name))
		sprintf(mouse->name, "USB HIDBP Mouse %04x:%04x",
			mouse->dev.id.vendor, mouse->dev.id.product);

	kfree(buf);

	FILL_INT_URB(mouse->irq, dev, pipe, mouse->data, maxp > 8 ? 8 : maxp,
		usb_mouse_irq, mouse, endpoint->bInterval);

	input_register_device(&mouse->dev);

	printk(KERN_INFO "input: %s on %s\n", mouse->name, path);

	return mouse;
}

static void usb_mouse_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_mouse *mouse = ptr;
	usb_unlink_urb(mouse->irq);
	input_unregister_device(&mouse->dev);
	usb_free_urb(mouse->irq);
	kfree(mouse);
}

static struct usb_device_id usb_mouse_id_table [] = {
	{ USB_INTERFACE_INFO(3, 1, 2) },
    { }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, usb_mouse_id_table);

static struct usb_driver usb_mouse_driver = {
	.name =		"usb_mouse",
	.probe =	usb_mouse_probe,
	.disconnect =	usb_mouse_disconnect,
	.id_table =	usb_mouse_id_table,
};

static int __init usb_mouse_init(void)
{
	usb_register(&usb_mouse_driver);
	info(DRIVER_VERSION ":" DRIVER_DESC);
	return 0;
}

static void __exit usb_mouse_exit(void)
{
	usb_deregister(&usb_mouse_driver);
}

module_init(usb_mouse_init);
module_exit(usb_mouse_exit);
