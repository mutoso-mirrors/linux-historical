/*
 * $Id: ct82c710.c,v 1.11 2001/09/25 10:12:07 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 *  82C710 C&T mouse port chip driver for Linux
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

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/err.h>

#include <asm/io.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("82C710 C&T mouse port chip driver");
MODULE_LICENSE("GPL");

/*
 * ct82c710 interface
 */

#define CT82C710_DEV_IDLE     0x01		/* Device Idle */
#define CT82C710_RX_FULL      0x02		/* Device Char received */
#define CT82C710_TX_IDLE      0x04		/* Device XMIT Idle */
#define CT82C710_RESET        0x08		/* Device Reset */
#define CT82C710_INTS_ON      0x10		/* Device Interrupt On */
#define CT82C710_ERROR_FLAG   0x20		/* Device Error */
#define CT82C710_CLEAR        0x40		/* Device Clear */
#define CT82C710_ENABLE       0x80		/* Device Enable */

#define CT82C710_IRQ          12

#define CT82C710_DATA         ct82c710_iores.start
#define CT82C710_STATUS       (ct82c710_iores.start + 1)

static struct serio *ct82c710_port;
static struct platform_device *ct82c710_device;
static struct resource ct82c710_iores;

/*
 * Interrupt handler for the 82C710 mouse port. A character
 * is waiting in the 82C710.
 */

static irqreturn_t ct82c710_interrupt(int cpl, void *dev_id, struct pt_regs * regs)
{
	return serio_interrupt(ct82c710_port, inb(CT82C710_DATA), 0, regs);
}

/*
 * Wait for device to send output char and flush any input char.
 */

static int ct82c170_wait(void)
{
	int timeout = 60000;

	while ((inb(CT82C710_STATUS) & (CT82C710_RX_FULL | CT82C710_TX_IDLE | CT82C710_DEV_IDLE))
		       != (CT82C710_DEV_IDLE | CT82C710_TX_IDLE) && timeout) {

		if (inb_p(CT82C710_STATUS) & CT82C710_RX_FULL) inb_p(CT82C710_DATA);

		udelay(1);
		timeout--;
	}

	return !timeout;
}

static void ct82c710_close(struct serio *serio)
{
	if (ct82c170_wait())
		printk(KERN_WARNING "ct82c710.c: Device busy in close()\n");

	outb_p(inb_p(CT82C710_STATUS) & ~(CT82C710_ENABLE | CT82C710_INTS_ON), CT82C710_STATUS);

	if (ct82c170_wait())
		printk(KERN_WARNING "ct82c710.c: Device busy in close()\n");

	free_irq(CT82C710_IRQ, NULL);
}

static int ct82c710_open(struct serio *serio)
{
	unsigned char status;

	if (request_irq(CT82C710_IRQ, ct82c710_interrupt, 0, "ct82c710", NULL))
		return -1;

	status = inb_p(CT82C710_STATUS);

	status |= (CT82C710_ENABLE | CT82C710_RESET);
	outb_p(status, CT82C710_STATUS);

	status &= ~(CT82C710_RESET);
	outb_p(status, CT82C710_STATUS);

	status |= CT82C710_INTS_ON;
	outb_p(status, CT82C710_STATUS);	/* Enable interrupts */

	while (ct82c170_wait()) {
		printk(KERN_ERR "ct82c710: Device busy in open()\n");
		status &= ~(CT82C710_ENABLE | CT82C710_INTS_ON);
		outb_p(status, CT82C710_STATUS);
		free_irq(CT82C710_IRQ, NULL);
		return -1;
	}

	return 0;
}

/*
 * Write to the 82C710 mouse device.
 */

static int ct82c710_write(struct serio *port, unsigned char c)
{
	if (ct82c170_wait()) return -1;
	outb_p(c, CT82C710_DATA);
	return 0;
}

/*
 * See if we can find a 82C710 device. Read mouse address.
 */

static int __init ct82c710_probe(void)
{
	outb_p(0x55, 0x2fa);				/* Any value except 9, ff or 36 */
	outb_p(0xaa, 0x3fa);				/* Inverse of 55 */
	outb_p(0x36, 0x3fa);				/* Address the chip */
	outb_p(0xe4, 0x3fa);				/* 390/4; 390 = config address */
	outb_p(0x1b, 0x2fa);				/* Inverse of e4 */
	outb_p(0x0f, 0x390);				/* Write index */
	if (inb_p(0x391) != 0xe4)			/* Config address found? */
		return -1;				/* No: no 82C710 here */

	outb_p(0x0d, 0x390);				/* Write index */
	ct82c710_iores.start = inb_p(0x391) << 2;	/* Get mouse I/O address */
	ct82c710_iores.end = ct82c710_iores.start + 1;
	ct82c710_iores.flags = IORESOURCE_IO;
	outb_p(0x0f, 0x390);
	outb_p(0x0f, 0x391);				/* Close config mode */

	return 0;
}

static struct serio * __init ct82c710_allocate_port(void)
{
	struct serio *serio;

	serio = kmalloc(sizeof(struct serio), GFP_KERNEL);
	if (serio) {
		memset(serio, 0, sizeof(struct serio));
		serio->id.type = SERIO_8042;
		serio->open = ct82c710_open;
		serio->close = ct82c710_close;
		serio->write = ct82c710_write;
		serio->dev.parent = &ct82c710_device->dev;
		strlcpy(serio->name, "C&T 82c710 mouse port", sizeof(serio->name));
		snprintf(serio->phys, sizeof(serio->phys), "isa%04lx/serio0", CT82C710_DATA);
	}

	return serio;
}

int __init ct82c710_init(void)
{
	if (ct82c710_probe())
		return -ENODEV;

	ct82c710_device = platform_device_register_simple("ct82c710", -1, &ct82c710_iores, 1);
	if (IS_ERR(ct82c710_device))
		return PTR_ERR(ct82c710_device);

	if (!(ct82c710_port = ct82c710_allocate_port())) {
		platform_device_unregister(ct82c710_device);
		return -ENOMEM;
	}

	serio_register_port(ct82c710_port);

	printk(KERN_INFO "serio: C&T 82c710 mouse port at %#lx irq %d\n",
		CT82C710_DATA, CT82C710_IRQ);

	return 0;
}

void __exit ct82c710_exit(void)
{
	serio_unregister_port(ct82c710_port);
	platform_device_unregister(ct82c710_device);
}

module_init(ct82c710_init);
module_exit(ct82c710_exit);
