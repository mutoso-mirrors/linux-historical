/* hplance.c  : the  Linux/hp300/lance ethernet driver
 *
 * Copyright (C) 05/1998 Peter Maydell <pmaydell@chiark.greenend.org.uk>
 * Based on the Sun Lance driver and the NetBSD HP Lance driver
 * Uses the generic 7990.c LANCE code.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/errno.h>
/* Used for the temporal inet entries and routing */
#include <linux/socket.h>
#include <linux/route.h>
#include <linux/dio.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>

#include "hplance.h"

/* We have 16834 bytes of RAM for the init block and buffers. This places
 * an upper limit on the number of buffers we can use. NetBSD uses 8 Rx
 * buffers and 2 Tx buffers.
 */
#define LANCE_LOG_TX_BUFFERS 1
#define LANCE_LOG_RX_BUFFERS 3

#include "7990.h"                                 /* use generic LANCE code */

/* Our private data structure */
struct hplance_private {
	struct lance_private lance;
};

/* function prototypes... This is easy because all the grot is in the
 * generic LANCE support. All we have to support is probing for boards,
 * plus board-specific init, open and close actions. 
 * Oh, and we need to tell the generic code how to read and write LANCE registers...
 */
static int __devinit hplance_init_one(struct dio_dev *d,
				const struct dio_device_id *ent);
static void __devinit hplance_init(struct net_device *dev, 
				struct dio_dev *d);
static void __devexit hplance_remove_one(struct dio_dev *d);
static void hplance_writerap(void *priv, unsigned short value);
static void hplance_writerdp(void *priv, unsigned short value);
static unsigned short hplance_readrdp(void *priv);
static int hplance_open(struct net_device *dev);
static int hplance_close(struct net_device *dev);

static struct dio_device_id hplance_dio_tbl[] = {
	{ DIO_ID_LAN },
	{ 0 }
};

static struct dio_driver hplance_driver = {
	.name      = "hplance",
	.id_table  = hplance_dio_tbl,
	.probe     = hplance_init_one,
	.remove    = __devexit_p(hplance_remove_one),
};

/* Find all the HP Lance boards and initialise them... */
static int __devinit hplance_init_one(struct dio_dev *d,
				const struct dio_device_id *ent)
{
	struct net_device *dev;
	int err;

	dev = alloc_etherdev(sizeof(struct hplance_private));
	if (!dev)
		return -ENOMEM;

	if (!request_mem_region(d->resource.start, d->resource.end-d->resource.start, d->name))
		return -EBUSY;

	SET_MODULE_OWNER(dev);
        
	hplance_init(dev, d);
	err = register_netdev(dev);
	if (err) {
		free_netdev(dev);
		return err;
	}
	dio_set_drvdata(d, dev);
	return 0;
}

static void __devexit hplance_remove_one(struct dio_dev *d)
{
	struct net_device *dev = dio_get_drvdata(d);

	unregister_netdev(dev);
	free_netdev(dev);
}

/* Initialise a single lance board at the given DIO device */
static void __init hplance_init(struct net_device *dev, struct dio_dev *d)
{
        unsigned long va = (d->resource.start + DIO_VIRADDRBASE);
        struct hplance_private *lp;
        int i;
        
        printk(KERN_INFO "%s: %s; select code %d, addr", dev->name, d->name, d->scode);

        /* reset the board */
        out_8(va+DIO_IDOFF, 0xff);
        udelay(100);                              /* ariba! ariba! udelay! udelay! */

        /* Fill the dev fields */
        dev->base_addr = va;
        dev->open = &hplance_open;
        dev->stop = &hplance_close;
#ifdef CONFIG_NET_POLL_CONTROLLER
        dev->poll_controller = lance_poll;
#endif
        dev->hard_start_xmit = &lance_start_xmit;
        dev->get_stats = &lance_get_stats;
        dev->set_multicast_list = &lance_set_multicast;
        dev->dma = 0;
        
        for (i=0; i<6; i++) {
                /* The NVRAM holds our ethernet address, one nibble per byte,
                 * at bytes NVRAMOFF+1,3,5,7,9...
                 */
                dev->dev_addr[i] = ((in_8(va + HPLANCE_NVRAMOFF + i*4 + 1) & 0xF) << 4)
                        | (in_8(va + HPLANCE_NVRAMOFF + i*4 + 3) & 0xF);
                printk("%c%2.2x", i == 0 ? ' ' : ':', dev->dev_addr[i]);
        }
        
        lp = netdev_priv(dev);
        lp->lance.name = (char*)d->name;                /* discards const, shut up gcc */
        lp->lance.base = va;
        lp->lance.init_block = (struct lance_init_block *)(va + HPLANCE_MEMOFF); /* CPU addr */
        lp->lance.lance_init_block = 0;                 /* LANCE addr of same RAM */
        lp->lance.busmaster_regval = LE_C3_BSWP;        /* we're bigendian */
        lp->lance.irq = d->ipl;
        lp->lance.writerap = hplance_writerap;
        lp->lance.writerdp = hplance_writerdp;
        lp->lance.readrdp = hplance_readrdp;
        lp->lance.lance_log_rx_bufs = LANCE_LOG_RX_BUFFERS;
        lp->lance.lance_log_tx_bufs = LANCE_LOG_TX_BUFFERS;
        lp->lance.rx_ring_mod_mask = RX_RING_MOD_MASK;
        lp->lance.tx_ring_mod_mask = TX_RING_MOD_MASK;
	printk(", irq %d\n", lp->lance.irq);
}

/* This is disgusting. We have to check the DIO status register for ack every
 * time we read or write the LANCE registers.
 */
static void hplance_writerap(void *priv, unsigned short value)
{
	struct lance_private *lp = (struct lance_private *)priv;
	do {
		out_be16(lp->base + HPLANCE_REGOFF + LANCE_RAP, value);
	} while ((in_8(lp->base + HPLANCE_STATUS) & LE_ACK) == 0);
}

static void hplance_writerdp(void *priv, unsigned short value)
{
	struct lance_private *lp = (struct lance_private *)priv;
	do {
		out_be16(lp->base + HPLANCE_REGOFF + LANCE_RDP, value);
	} while ((in_8(lp->base + HPLANCE_STATUS) & LE_ACK) == 0);
}

static unsigned short hplance_readrdp(void *priv)
{
	struct lance_private *lp = (struct lance_private *)priv;
	__u16 value;
	do {
		value = in_be16(lp->base + HPLANCE_REGOFF + LANCE_RDP);
	} while ((in_8(lp->base + HPLANCE_STATUS) & LE_ACK) == 0);
	return value;
}

static int hplance_open(struct net_device *dev)
{
        int status;
        struct lance_private *lp = netdev_priv(dev);
        
        status = lance_open(dev);                 /* call generic lance open code */
        if (status)
                return status;
        /* enable interrupts at board level. */
        out_8(lp->base + HPLANCE_STATUS, LE_IE);

        return 0;
}

static int hplance_close(struct net_device *dev)
{
        struct lance_private *lp = netdev_priv(dev);

        out_8(lp->base + HPLANCE_STATUS, 0);	/* disable interrupts at boardlevel */
        lance_close(dev);
        return 0;
}

int __init hplance_init_module(void)
{
	return dio_module_init(&hplance_driver);
}

void __exit hplance_cleanup_module(void)
{
        dio_unregister_driver(&hplance_driver);
}

module_init(hplance_init_module);
module_exit(hplance_cleanup_module);

MODULE_LICENSE("GPL");
