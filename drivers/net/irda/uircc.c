/*********************************************************************
 *                
 * Filename:      uircc.c
 * Version:       0.3
 * Description:   Driver for the Sharp Universal Infrared 
 *                Communications Controller (UIRCC v 1.3)
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Dec 26 10:59:03 1998
 * Modified at:   Wed May 19 15:29:56 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli, All Rights Reserved.
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Troms� admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *
 *     Applicable Models : Tecra 510CDT, 500C Series, 530CDT, 520CDT,
 *     740CDT, Portege 300CT, 660CDT, Satellite 220C Series, 
 *     Satellite Pro, 440C Series, 470CDT, 460C Series, 480C Series
 *
 ********************************************************************/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/malloc.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>

#include <net/irda/wrapper.h>
#include <net/irda/irda.h>
#include <net/irda/irmod.h>
#include <net/irda/irlap_frame.h>
#include <net/irda/irda_device.h>

#include <net/irda/uircc.h>
#include <net/irda/irport.h>

static char *driver_name = "uircc";

#define CHIP_IO_EXTENT 16

static unsigned int io[]  = { 0x300, ~0, ~0, ~0 };
static unsigned int io2[] = { 0x3e8, 0, 0, 0};
static unsigned int irq[] = { 11, 0, 0, 0 };
static unsigned int dma[] = { 5, 0, 0, 0 };

static struct uircc_cb *dev_self[] = { NULL, NULL, NULL, NULL};

/* Some prototypes */
static int  uircc_open(int i, unsigned int iobase, unsigned int board_addr, 
		       unsigned int irq, unsigned int dma);
#ifdef MODULE
static int  uircc_close(struct irda_device *idev);
#endif /* MODULE */
static int  uircc_probe(int iobase, int board_addr, int irq, int dma);
static int  uircc_dma_receive(struct irda_device *idev); 
static int  uircc_dma_receive_complete(struct irda_device *idev, int iobase);
static int  uircc_hard_xmit(struct sk_buff *skb, struct device *dev);
static void uircc_dma_write(struct irda_device *idev, int iobase);
static void uircc_change_speed(struct irda_device *idev, int baud);
static void uircc_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void uircc_wait_until_sent(struct irda_device *idev);
static int  uircc_is_receiving(struct irda_device *idev);
static int uircc_toshiba_cmd(int *retval, int arg0, int arg1, int arg2);
static int  uircc_net_init(struct device *dev);
static int  uircc_net_open(struct device *dev);
static int  uircc_net_close(struct device *dev);

/*
 * Function uircc_init ()
 *
 *    Initialize chip. Just try to find out how many chips we are dealing with
 *    and where they are
 */
int __init uircc_init(void)
{
	int i;

	for ( i=0; (io[i] < 2000) && (i < 4); i++) {
		int ioaddr = io[i];
		if (check_region(ioaddr, CHIP_IO_EXTENT) < 0)
			continue;
		if (uircc_open(i, io[i], io2[i], irq[i], dma[i]) == 0)
			return 0;
	}
	return -ENODEV;
}

/*
 * Function uircc_cleanup ()
 *
 *    Close all configured chips
 *
 */
#ifdef MODULE
static void uircc_cleanup(void)
{
	int i;

        DEBUG(4, __FUNCTION__ "()\n");

	for (i=0; i < 4; i++) {
		if (dev_self[i])
			uircc_close(&(dev_self[i]->idev));
	}
}
#endif /* MODULE */

/*
 * Function uircc_open (iobase, irq)
 *
 *    Open driver instance
 *
 */
static int uircc_open(int i, unsigned int iobase, unsigned int iobase2, 
		      unsigned int irq, unsigned int dma)
{
	struct uircc_cb *self;
	struct irda_device *idev;
	int ret;

	DEBUG(4, __FUNCTION__ "()\n");

	if ((uircc_probe(iobase, iobase2, irq, dma)) == -1)
		return -1;
	
	/*
	 *  Allocate new instance of the driver
	 */
	self = kmalloc( sizeof(struct uircc_cb), GFP_KERNEL);
	if (self == NULL) {
		printk(KERN_ERR "IrDA: Can't allocate memory for "
		       "IrDA control block!\n");
		return -ENOMEM;
	}
	memset(self, 0, sizeof(struct uircc_cb));
   
	/* Need to store self somewhere */
	dev_self[i] = self;

	idev = &self->idev;

	/* Initialize IO */
	idev->io.iobase    = iobase;
        idev->io.iobase2   = iobase2; /* Used by irport */
        idev->io.irq       = irq;
        idev->io.io_ext    = CHIP_IO_EXTENT;
        idev->io.io_ext2   = 8;       /* Used by irport */
        idev->io.dma       = dma;
        idev->io.fifo_size = 16;

	/* Lock the port that we need */
	ret = check_region(idev->io.iobase, idev->io.io_ext);
	if (ret < 0) { 
		DEBUG(0, __FUNCTION__ "(), can't get iobase of 0x%03x\n",
		      idev->io.iobase);
		/* uircc_cleanup( self->idev);  */
		return -ENODEV;
	}
	ret = check_region(idev->io.iobase2, idev->io.io_ext2);
	if (ret < 0) { 
		DEBUG(0, __FUNCTION__ "(), can't get iobase of 0x%03x\n",
		      idev->io.iobase2);
		/* uircc_cleanup( self->idev);  */
		return -ENODEV;
	}
	request_region(idev->io.iobase, idev->io.io_ext, idev->name);
        request_region(idev->io.iobase2, idev->io.io_ext2, idev->name);

	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&idev->qos);
	
	/* The only value we must override it the baudrate */
	idev->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200/*IR_576000|IR_1152000 |(IR_4000000 << 8)*/;

	idev->qos.min_turn_time.bits = 0x0f;
	irda_qos_bits_to_value(&idev->qos);

	idev->flags = IFF_FIR|IFF_SIR|IFF_DMA|IFF_PIO;
	
	/* Specify which buffer allocation policy we need */
	idev->rx_buff.flags = GFP_KERNEL | GFP_DMA;
	idev->tx_buff.flags = GFP_KERNEL | GFP_DMA;

	/* Max DMA buffer size needed = (data_size + 6) * (window_size) + 6; */
	idev->rx_buff.truesize = 4000; 
	idev->tx_buff.truesize = 4000;
	
	/* Initialize callbacks */
	idev->change_speed    = uircc_change_speed;
	idev->wait_until_sent = uircc_wait_until_sent;
	idev->is_receiving    = uircc_is_receiving;
     
	/* Override the network functions we need to use */
	idev->netdev.init            = uircc_net_init;
	idev->netdev.hard_start_xmit = uircc_hard_xmit;
	idev->netdev.open            = uircc_net_open;
	idev->netdev.stop            = uircc_net_close;

	irport_start(idev, iobase2);

	/* Open the IrDA device */
	irda_device_open(idev, driver_name, self);
	
	return 0;
}

/*
 * Function uircc_close (idev)
 *
 *    Close driver instance
 *
 */
#ifdef MODULE
static int uircc_close(struct irda_device *idev)
{
	struct uircc_cb *self;
	int iobase;
	int status;

	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(idev != NULL, return -1;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return -1;);

        iobase = idev->io.iobase;
	self = (struct uircc_cb *) idev->priv;

	/* Some magic to disable FIR and enable SIR */
	uircc_toshiba_cmd(&status, 0xffff, 0x001b, 0x0000);

	/* Disable modem */
	outb(0x00, iobase+UIRCC_CR10);

	irport_stop(idev, idev->io.iobase2);

	/* Release the PORT that this driver is using */
	DEBUG(4, __FUNCTION__ "(), Releasing Region %03x\n", idev->io.iobase);
	release_region(idev->io.iobase, idev->io.io_ext);

	if (idev->io.iobase2) {
		DEBUG(4, __FUNCTION__ "(), Releasing Region %03x\n", 
		      idev->io.iobase2);
		release_region(idev->io.iobase2, idev->io.io_ext2);
	}
	irda_device_close(idev);

	kfree(self);

	return 0;
}
#endif /* MODULE */

/*
 * Function uircc_probe (iobase, board_addr, irq, dma)
 *
 *    Returns non-negative on success.
 *
 */
static int uircc_probe(int iobase, int iobase2, int irq, int dma) 
{
	int version;
	
	DEBUG(4, __FUNCTION__ "()\n");

	/* read the chip version, should be 0x03 */
	version = inb(iobase+UIRCC_SR8);

	if (version != 0x03) {
		DEBUG(0, __FUNCTION__ "(), Wrong chip version");	
		return -1;
	}
        printk(KERN_INFO "Sharp UIRCC IrDA driver loaded. Version: 0x%02x\n",
	       version);

	/* Reset chip */
	outb(UIRCC_CR0_SYS_RST, iobase+UIRCC_CR0);

	/* Initialize some registers */
	outb(0x03, iobase+UIRCC_CR15);
	outb(0, iobase+UIRCC_CR11);
	outb(0, iobase+UIRCC_CR9);

	DEBUG(0, __FUNCTION__ "(), sr15=%#x\n", inb(iobase+UIRCC_SR15));

	/* Enable DMA single mode */
	outb(UIRCC_CR1_RX_DMA|UIRCC_CR1_TX_DMA|UIRCC_CR1_MUST_SET, 
	     iobase+UIRCC_CR1);

	/* Disable interrupts */
	outb(0xff, iobase+UIRCC_CR2); 

	/* Set self poll address */

	return 0;
}

/*
 * Function uircc_change_speed (idev, baud)
 *
 *    Change the speed of the device
 *
 */
static void uircc_change_speed(struct irda_device *idev, int speed)
{
	struct uircc_cb *self;
	int iobase; 
	int modem = UIRCC_CR10_SIR;
	int status;

	DEBUG(0, __FUNCTION__ "()\n");

	/* Just test the high speed stuff */
	/*speed = 4000000;*/

	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);

	self = idev->priv;
	iobase = idev->io.iobase;

	/* Update accounting for new speed */
	idev->io.baudrate = speed;

	/* Disable interrupts */	
	outb(0xff, iobase+UIRCC_CR2);

	switch (speed) {
	case 9600:
	case 19200:
	case 37600:
	case 57600:
	case 115200:
 		irport_start(idev, idev->io.iobase2);
		irport_change_speed(idev, speed);

		/* Some magic to disable FIR and enable SIR */
		uircc_toshiba_cmd(&status, 0xffff, 0x001b, 0x0000);

		modem = UIRCC_CR10_SIR;
		break;
	case 576000:		
		
		DEBUG(0, __FUNCTION__ "(), handling baud of 576000\n");
		break;
	case 1152000:

		DEBUG(0, __FUNCTION__ "(), handling baud of 1152000\n");
		break;
	case 4000000:
		irport_stop(idev, idev->io.iobase2);

		/* Some magic to disable SIR and enable FIR */
		uircc_toshiba_cmd(&status, 0xffff, 0x001b, 0x0001);

		modem = UIRCC_CR10_FIR;
		DEBUG(0, __FUNCTION__ "(), handling baud of 4000000\n");

		/* Set self pole address */
		//outb(0xfe, iobase+UIRCC_CR8);

	 	/* outb(0x10, iobase+UIRCC_CR11); */
		break;
	default:
		DEBUG( 0, __FUNCTION__ "(), unknown baud rate of %d\n", speed);
		break;
	}

	/* Set appropriate speed mode */
	outb(modem, iobase+UIRCC_CR10);

	idev->netdev.tbusy = 0;
	
	/* Enable some interrupts so we can receive frames */
	if (speed > 115200) {
		/* Enable DMA single mode */
		outb(UIRCC_CR1_RX_DMA|UIRCC_CR1_TX_DMA|UIRCC_CR1_MUST_SET, 
		     iobase+UIRCC_CR1);

 		/* Enable all interrupts  */
		outb(0, iobase+UIRCC_CR2); 
 		uircc_dma_receive(idev);
 	}    	
}

/*
 * Function uircc_hard_xmit (skb, dev)
 *
 *    Transmit the frame!
 *
 */
static int uircc_hard_xmit(struct sk_buff *skb, struct device *dev)
{
	struct irda_device *idev;
	int iobase;
	int mtt;
	
	idev = (struct irda_device *) dev->priv;

	ASSERT(idev != NULL, return 0;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return 0;);

	iobase = idev->io.iobase;

	DEBUG(4, __FUNCTION__ "(%ld), skb->len=%d\n", jiffies, (int) skb->len);
	
	/* Reset carrier latch */
	/*outb(0x02, iobase+UIRCC_CR0);*/

	/* Use irport for SIR speeds */
	if (idev->io.baudrate <= 115200) {
		return irport_hard_xmit(skb, dev);
	}
	
	DEBUG(0, __FUNCTION__ "(), sr0=%#x, sr1=%#x, sr2=%#x, sr3=%#x, sr10=%#x, sr11=%#x\n",
	      inb(iobase+UIRCC_SR0), inb(iobase+UIRCC_SR3),
	      inb(iobase+UIRCC_SR2), inb(iobase+UIRCC_SR3), 
	      inb(iobase+UIRCC_SR10), inb(iobase+UIRCC_SR11));

	/* Lock transmit buffer */
	if (irda_lock((void *) &dev->tbusy) == FALSE)
		return -EBUSY;

	memcpy(idev->tx_buff.data, skb->data, skb->len);

	/* Make sure that the length is a multiple of 16 bits */
	if (skb->len & 0x01)
		skb->len++;

	idev->tx_buff.len = skb->len;
	idev->tx_buff.data = idev->tx_buff.head;
	
	mtt = irda_get_mtt(skb);
	
	/* Use udelay for delays less than 50 us. */
	if (mtt)
		udelay(mtt);
	
	/* Enable transmit interrupts */
 	outb(0, iobase+UIRCC_CR2);

	uircc_dma_write(idev, iobase);
	
	dev_kfree_skb(skb);

	return 0;
}

/*
 * Function uircc_dma_write (idev, iobase)
 *
 *    Transmit data using DMA
 *
 */
static void uircc_dma_write(struct irda_device *idev, int iobase)
{
	struct uircc_cb *self;

	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);

	self = idev->priv;

	/* Receiving disable */
	self->cr3 &= ~UIRCC_CR3_RECV_EN;
	outb(self->cr3, iobase+UIRCC_CR3);

	/* Set modem */
	outb(0x80, iobase+UIRCC_CR10);

	/* Enable transmit DMA */
	outb(UIRCC_CR1_TX_DMA|UIRCC_CR1_MUST_SET, iobase+UIRCC_CR1);

	ASSERT((((__u32)(idev->tx_buff.data)) & 0x01) != 0x01, return;);

	setup_dma(idev->io.dma, idev->tx_buff.data, idev->tx_buff.len, 
		  DMA_MODE_WRITE);
	
	idev->io.direction = IO_XMIT;

	/* Set frame length (should be the real length without padding */
	outb(idev->tx_buff.len & 0xff, iobase+UIRCC_CR4); /* Low byte */
	outb(idev->tx_buff.len >> 8, iobase+UIRCC_CR5);   /* High byte */

	/* Enable transmit and transmit CRC */
	self->cr3 |= (UIRCC_CR3_XMIT_EN|UIRCC_CR3_TX_CRC_EN);
	outb(self->cr3, iobase+UIRCC_CR3);
}

/*
 * Function uircc_dma_xmit_complete (idev)
 *
 *    The transfer of a frame in finished. This function will only be called 
 *    by the interrupt handler
 *
 */
static void uircc_dma_xmit_complete( struct irda_device *idev, int underrun)
{
	struct uircc_cb *self;
	int iobase;
	int len;

	DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(idev != NULL, return;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return;);

	self = idev->priv;

	iobase = idev->io.iobase;

	/* Select TX counter */
	outb(UIRCC_CR0_CNT_SWT, iobase+UIRCC_CR0);

	/* Read TX length counter */
	len  = inb(iobase+UIRCC_SR4);      /* Low byte */
	len |= inb(iobase+UIRCC_SR5) << 8; /* High byte */

	DEBUG(4, __FUNCTION__ "(), sent %d bytes\n", len);

	/* Disable transmit */
	self->cr3 &= ~UIRCC_CR3_XMIT_EN;
	outb(self->cr3, iobase+UIRCC_CR3);

	/* Transmit reset (just to be sure) */
	outb(UIRCC_CR0_XMIT_RST, iobase+UIRCC_CR0);
	
	/* Check for underrrun! */
	if (underrun) {
		idev->stats.tx_errors++;
		idev->stats.tx_fifo_errors++;		
	} else {
		idev->stats.tx_packets++;
		idev->stats.tx_bytes += idev->tx_buff.len;
	}

	/* Unlock tx_buff and request another frame */
	idev->netdev.tbusy = 0; /* Unlock */
	idev->media_busy = FALSE;
	
	/* Tell the network layer, that we can accept more frames */
	mark_bh(NET_BH);
}

/*
 * Function uircc_dma_receive (idev)
 *
 *    Get ready for receiving a frame. The device will initiate a DMA
 *    if it starts to receive a frame.
 *
 */
static int uircc_dma_receive(struct irda_device *idev) 
{
	struct uircc_cb *self;
	int iobase;

	ASSERT(idev != NULL, return -1;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return -1;);

	DEBUG(4, __FUNCTION__ "\n");

	self = idev->priv;
	iobase= idev->io.iobase;

	/* Transmit disable */
	/* self->cr3 &= ~UIRCC_CR3_XMIT_EN; */
	self->cr3 = 0;
	outb(self->cr3, iobase+UIRCC_CR3);

	/* Transmit reset (just in case) */
	outb(UIRCC_CR0_XMIT_RST|0x17, iobase+UIRCC_CR0);

	/* Set modem */
	outb(0x08, iobase+UIRCC_CR10);

	/* Enable receiving with CRC */
	self->cr3 = (UIRCC_CR3_RECV_EN|UIRCC_CR3_RX_CRC_EN);
	outb(self->cr3, iobase+UIRCC_CR3);

	/* Make sure Rx DMA is set */
 	outb(UIRCC_CR1_RX_DMA|UIRCC_CR1_MUST_SET, iobase+UIRCC_CR1);

	/* Rx reset */
	/* outb(UIRCC_CR0_RECV_RST, iobase+UIRCC_CR0); */

	setup_dma(idev->io.dma, idev->rx_buff.data, 
		  idev->rx_buff.truesize, DMA_MODE_READ);
	
	/* driver->media_busy = FALSE; */
	idev->io.direction = IO_RECV;
	idev->rx_buff.data = idev->rx_buff.head;

#if 0
	/* Enable receiving with CRC */
	self->cr3 = (UIRCC_CR3_RECV_EN|UIRCC_CR3_RX_CRC_EN);
	outb(self->cr3, iobase+UIRCC_CR3);
#endif
	DEBUG(4, __FUNCTION__ "(), cr3=%#x\n", self->cr3);
	
	/* Address check? */

	return 0;
}

/*
 * Function uircc_dma_receive_complete (idev)
 *
 *    Finished with receiving frames
 *
 *    
 */
static int uircc_dma_receive_complete(struct irda_device *idev, int iobase)
{
	struct sk_buff *skb;
	struct uircc_cb *self;
	int len;

	self = idev->priv;

	DEBUG(0, __FUNCTION__ "()\n");

	/* Check for CRC or framing error */
	if (inb(iobase+UIRCC_SR0) & UIRCC_SR0_RX_CRCFRM) {
		DEBUG(0, __FUNCTION__ "(), CRC or FRAME error\n");
		return -1;
	}

	/* Select receive length counter */
	outb(0x00, iobase+UIRCC_CR0);

	/* Read frame length */
	len = inb(iobase+UIRCC_SR4);       /* Low byte */
	len |= inb(iobase+UIRCC_SR5) << 8; /* High byte */

	DEBUG(0, __FUNCTION__ "(), len=%d\n", len);

	/* Receiving disable */
	self->cr3 &= ~UIRCC_CR3_RECV_EN;
	outb(self->cr3, iobase+UIRCC_CR3);

	skb = dev_alloc_skb(len+1);
	if (skb == NULL)  {
		printk(KERN_INFO __FUNCTION__ 
		       "(), memory squeeze, dropping frame.\n");
				/* Restore bank register */
		return FALSE;
	}
			
	/* Make sure IP header gets aligned */
	skb_reserve(skb, 1);

	/* Copy frame without CRC */
	/* if ( idev->io.baudrate < 4000000) { */
/* 		skb_put( skb, len-2); */
/* 		memcpy( skb->data, idev->rx_buff.head, len-2); */
/* 	} else { */
/* 		skb_put( skb, len-4); */
/* 		memcpy( skb->data, idev->rx_buff.head, len-4); */
/* 	} */

	skb_put(skb, len);
	memcpy(skb->data, idev->rx_buff.data, len);
	idev->stats.rx_packets++;

	skb->dev = &idev->netdev;
	skb->mac.raw  = skb->data;
	skb->protocol = htons(ETH_P_IRDA);
	netif_rx(skb);

	return TRUE;
}

/*
 * Function uircc_interrupt (irq, dev_id, regs)
 *
 *    An interrupt from the chip has arrived. Time to do some work
 *
 */
static void uircc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	__u8 sr3;
	int iobase;

	struct irda_device *idev = (struct irda_device *) dev_id;

	if (idev == NULL) {
		printk( KERN_WARNING "%s: irq %d for unknown device.\n", 
			driver_name, irq);
		return;
	}
	
	if (idev->io.baudrate <= 115200)
		return irport_interrupt( irq, dev_id, regs);

	iobase = idev->io.iobase;

	/* Read interrupt status */
	sr3 = inb( iobase+UIRCC_SR3); 
	if (!sr3) {
		DEBUG(4,"**\n");
		return;
	}
	idev->netdev.interrupt = 1;

	DEBUG(4, __FUNCTION__ "(), sr3=%#x, sr2=%#x, sr10=%#x\n", 
	      inb( iobase+UIRCC_SR3), inb( iobase+UIRCC_SR2), 
	      inb( iobase+UIRCC_SR10));

	/*
	 *  Check what interrupt this is. The UIRCC will not report two
	 *  different interrupts at the same time!
	 */
	switch(sr3) {
	case UIRCC_SR3_RX_EOF: /* Check for end of frame */
		uircc_dma_receive_complete(idev, iobase);
		break;
	case UIRCC_SR3_TXUR:   /* Check for transmit underrun */
		uircc_dma_xmit_complete(idev, TRUE);
		uircc_dma_receive(idev);
		outb(0, iobase+UIRCC_CR2); 
		break;
	case UIRCC_SR3_TX_DONE:
		uircc_dma_xmit_complete(idev, FALSE);
		uircc_dma_receive(idev);

		outb(0x0d, iobase+UIRCC_CR2);
		break;
	case UIRCC_SR3_TMR_OUT:
		/* Disable timer */
		outb(inb(iobase+UIRCC_CR11) & ~UIRCC_CR11_TMR_EN, 
		     iobase+UIRCC_CR11);
		break;
	default:
		DEBUG(0, __FUNCTION__ "(), unknown interrupt status=%#x\n",
		      sr3);
		break;
	}	
	idev->netdev.interrupt = 0;
}

/*
 * Function uircc_wait_until_sent (idev)
 *
 *    This function should put the current thread to sleep until all data 
 *    have been sent, so it is safe to change the speed.
 */
static void uircc_wait_until_sent( struct irda_device *idev)
{
	/* Just delay 60 ms */
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(6);
}

/*
 * Function uircc_is_receiving (idev)
 *
 *    Return TRUE is we are currently receiving a frame
 *
 */
static int uircc_is_receiving( struct irda_device *idev)
{
	int status = FALSE;
	/* int iobase; */

	ASSERT(idev != NULL, return FALSE;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return FALSE;);

	if (idev->io.baudrate > 115200) {
		
	} else 
		status = (idev->rx_buff.state != OUTSIDE_FRAME);
	
	return status;
}

/*
 * Function uircc_net_init (dev)
 *
 *    Initialize network device
 *
 */
static int uircc_net_init( struct device *dev)
{
	DEBUG( 4, __FUNCTION__ "()\n");

	/* Setup to be a normal IrDA network device driver */
	irda_device_setup(dev);

	/* Insert overrides below this line! */

	return 0;
}


/*
 * Function uircc_net_open (dev)
 *
 *    Start the device
 *
 */
static int uircc_net_open(struct device *dev)
{
	struct irda_device *idev;
	int iobase;
	
	DEBUG( 4, __FUNCTION__ "()\n");
	
	ASSERT(dev != NULL, return -1;);
	idev = (struct irda_device *) dev->priv;
	
	ASSERT(idev != NULL, return 0;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return 0;);
	
	iobase = idev->io.iobase;

	if (request_irq(idev->io.irq, uircc_interrupt, 0, idev->name, 
			(void *) idev)) {
		return -EAGAIN;
	}
	/*
	 * Always allocate the DMA channel after the IRQ,
	 * and clean up on failure.
	 */
	if (request_dma(idev->io.dma, idev->name)) {
		free_irq(idev->io.irq, idev);
		return -EAGAIN;
	}
		
	/* Ready to play! */
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	/* turn on interrupts */
	
	MOD_INC_USE_COUNT;

	return 0;
}

/*
 * Function uircc_net_close (dev)
 *
 *    Stop the device
 *
 */
static int uircc_net_close(struct device *dev)
{
	struct irda_device *idev;
	int iobase;

	DEBUG(4, __FUNCTION__ "()\n");
	
	/* Stop device */
	dev->tbusy = 1;
	dev->start = 0;

	ASSERT(dev != NULL, return -1;);
	idev = (struct irda_device *) dev->priv;
	
	ASSERT(idev != NULL, return 0;);
	ASSERT(idev->magic == IRDA_DEVICE_MAGIC, return 0;);
	
	iobase = idev->io.iobase;

	disable_dma(idev->io.dma);

	/* Disable interrupts */
       
	free_irq(idev->io.irq, idev);
	free_dma(idev->io.dma);

	MOD_DEC_USE_COUNT;

	return 0;
}

/*
 * Function uircc_toshiba_cmd (arg0, arg1, arg2)
 *
 *    disable FIR: uircc_toshiba_cmd(&status, 0xffff, 0x001b, 0x0000);
 *    enable  FIR: uircc_toshiba_cmd(&status, 0xffff, 0x001b, 0x0001);
 *    IRDA status: uircc_toshiba_cmd(&status, 0xfefe, 0x001b, 0x0000);
 */
static int uircc_toshiba_cmd(int *retval, int arg0, int arg1, int arg2)
{
	char return_code = 0;

	__asm__ volatile ("inb   $0xb2,%%al; "
			  "movb  %%ah,%%al;  "
			  : /* Output */
			  "=al"  (return_code),
			  "=ecx" (*retval)
			  : /* Input */
			  "ax" (arg0),
			  "bx" (arg1),
			  "cx" (arg2)
		);
	/*
	 * Return
	 * 0x00 = OK
	 * 0x80 = Function not supported by system
	 * 0x83 = Input data error
	 */
	return (int) return_code;
}

#ifdef MODULE

/*
 * Function init_module (void)
 *
 *    
 *
 */
int init_module(void)
{
	return uircc_init();
}

/*
 * Function cleanup_module (void)
 *
 *    
 *
 */
void cleanup_module(void)
{
	uircc_cleanup();
}
#endif /* MODULE */

