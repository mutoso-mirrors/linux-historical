/* 
 * Universal Host Controller Interface driver for USB (take II).
 *
 * (c) 1999 Georg Acher, acher@in.tum.de (executive slave) (base guitar)
 *          Deti Fliegl, deti@fliegl.de (executive slave) (lead voice)
 *          Thomas Sailer, sailer@ife.ee.ethz.ch (chief consultant) (cheer leader)
 *          Roman Weissgaerber, weissg@vienna.at (virt root hub) (studio porter)
 *          
 * HW-initalization based on material of
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999 Johannes Erdfelt
 * (C) Copyright 1999 Randy Dunlap
 *
 * $Id: usb-uhci.c,v 1.169 2000/01/20 19:50:11 acher Exp $
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/interrupt.h>	/* for in_interrupt() */
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

/* This enables debug printks */
//#define DEBUG
/* This enables all symbols to be exported, to ease debugging oopses */
#define DEBUG_SYMBOLS
/* This enables an extra UHCI slab for memory debugging */
//#define DEBUG_SLAB

#include "usb.h"
#include "usb-uhci.h"
#include "uhci-debug.h"

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
static int handle_apm_event (apm_event_t event);
#endif

#ifdef DEBUG_SYMBOLS
#define _static
#ifndef EXPORT_SYMTAB
#define EXPORT_SYMTAB
#endif
#else
#define _static static
#endif

#ifdef DEBUG_SLAB
static kmem_cache_t *uhci_desc_kmem;
static kmem_cache_t *urb_priv_kmem;
#endif

_static int rh_submit_urb (purb_t purb);
_static int rh_unlink_urb (purb_t purb);
static puhci_t devs = NULL;

/* used by userspace UHCI data structure dumper */
puhci_t *uhci_devices = &devs;

/*-------------------------------------------------------------------*/
_static void queue_urb (puhci_t s, struct list_head *p, int do_lock)
{
	unsigned long flags=0;

	if (do_lock) 
		spin_lock_irqsave (&s->urb_list_lock, flags);
		
	list_add_tail (p, &s->urb_list);
	
	if (do_lock) 
		spin_unlock_irqrestore (&s->urb_list_lock, flags);
}

/*-------------------------------------------------------------------*/
_static void dequeue_urb (puhci_t s, struct list_head *p, int do_lock)
{
	unsigned long flags=0;
	
	if (do_lock) 
		spin_lock_irqsave (&s->urb_list_lock, flags);

	list_del (p);

	if (do_lock) 
		spin_unlock_irqrestore (&s->urb_list_lock, flags);
}

/*-------------------------------------------------------------------*/
_static int alloc_td (puhci_desc_t * new, int flags)
{
#ifdef DEBUG_SLAB
	*new= kmem_cache_alloc(uhci_desc_kmem, in_interrupt ()? SLAB_ATOMIC : SLAB_KERNEL);
#else
	*new = (uhci_desc_t *) kmalloc (sizeof (uhci_desc_t), in_interrupt ()? GFP_ATOMIC : GFP_KERNEL);
#endif
	if (!*new)
		return -ENOMEM;
	
	memset (*new, 0, sizeof (uhci_desc_t));
	(*new)->hw.td.link = UHCI_PTR_TERM | (flags & UHCI_PTR_BITS);	// last by default

	(*new)->type = TD_TYPE;
	mb();
	INIT_LIST_HEAD (&(*new)->vertical);
	INIT_LIST_HEAD (&(*new)->horizontal);
	
	return 0;
}
/*-------------------------------------------------------------------*/
/* insert td at last position in td-list of qh (vertical) */
_static int insert_td (puhci_t s, puhci_desc_t qh, puhci_desc_t new, int flags)
{
	uhci_desc_t *prev;
	unsigned long xxx;
	
	spin_lock_irqsave (&s->td_lock, xxx);

	list_add_tail (&new->vertical, &qh->vertical);

	if (qh->hw.qh.element & UHCI_PTR_TERM) {
		// virgin qh without any tds
		qh->hw.qh.element = virt_to_bus (new);	/* QH's cannot have the DEPTH bit set */
	}
	else {
		// already tds inserted 
		prev = list_entry (new->vertical.prev, uhci_desc_t, vertical);
		// implicitely remove TERM bit of prev
		prev->hw.td.link = virt_to_bus (new) | (flags & UHCI_PTR_DEPTH);
	}
	mb();
	spin_unlock_irqrestore (&s->td_lock, xxx);
	
	return 0;
}
/*-------------------------------------------------------------------*/
/* insert new_td after td (horizontal) */
_static int insert_td_horizontal (puhci_t s, puhci_desc_t td, puhci_desc_t new, int flags)
{
	uhci_desc_t *next;
	unsigned long xxx;
	
	spin_lock_irqsave (&s->td_lock, xxx);

	next = list_entry (td->horizontal.next, uhci_desc_t, horizontal);
	new->hw.td.link = td->hw.td.link;
	mb();
	list_add (&new->horizontal, &td->horizontal);
	td->hw.td.link = virt_to_bus (new);
	mb();
	spin_unlock_irqrestore (&s->td_lock, xxx);	
	
	return 0;
}
/*-------------------------------------------------------------------*/
_static int unlink_td (puhci_t s, puhci_desc_t element)
{
	uhci_desc_t *next, *prev;
	int dir = 0;
	unsigned long xxx;
	
	spin_lock_irqsave (&s->td_lock, xxx);
	
	next = list_entry (element->vertical.next, uhci_desc_t, vertical);
	
	if (next == element) {
		dir = 1;
		next = list_entry (element->horizontal.next, uhci_desc_t, horizontal);
		prev = list_entry (element->horizontal.prev, uhci_desc_t, horizontal);
	}
	else {
		prev = list_entry (element->vertical.prev, uhci_desc_t, vertical);
	}
	
	if (prev->type == TD_TYPE)
		prev->hw.td.link = element->hw.td.link;
	else
		prev->hw.qh.element = element->hw.td.link;
	
	mb ();
	
	if (dir == 0)
		list_del (&element->vertical);
	else
		list_del (&element->horizontal);
	
	spin_unlock_irqrestore (&s->td_lock, xxx);	
	
	return 0;
}
/*-------------------------------------------------------------------*/
_static int delete_desc (puhci_desc_t element)
{
#ifdef DEBUG_SLAB
	kmem_cache_free(uhci_desc_kmem, element);
#else
	kfree (element);
#endif
	return 0;
}
/*-------------------------------------------------------------------*/
// Allocates qh element
_static int alloc_qh (puhci_desc_t * new)
{
#ifdef DEBUG_SLAB
	*new= kmem_cache_alloc(uhci_desc_kmem, in_interrupt ()? SLAB_ATOMIC : SLAB_KERNEL);
#else
	*new = (uhci_desc_t *) kmalloc (sizeof (uhci_desc_t), in_interrupt ()? GFP_ATOMIC : GFP_KERNEL);
#endif	
	if (!*new)
		return -ENOMEM;
	
	memset (*new, 0, sizeof (uhci_desc_t));
	(*new)->hw.qh.head = UHCI_PTR_TERM;
	(*new)->hw.qh.element = UHCI_PTR_TERM;
	(*new)->type = QH_TYPE;
	mb();
	INIT_LIST_HEAD (&(*new)->horizontal);
	INIT_LIST_HEAD (&(*new)->vertical);
	
	dbg("Allocated qh @ %p", *new);
	
	return 0;
}
/*-------------------------------------------------------------------*/
// inserts new qh before/after the qh at pos
// flags: 0: insert before pos, 1: insert after pos (for low speed transfers)
_static int insert_qh (puhci_t s, puhci_desc_t pos, puhci_desc_t new, int flags)
{
	puhci_desc_t old;
	unsigned long xxx;

	spin_lock_irqsave (&s->qh_lock, xxx);

	if (!flags) {
		// (OLD) (POS) -> (OLD) (NEW) (POS)
		old = list_entry (pos->horizontal.prev, uhci_desc_t, horizontal);
		list_add_tail (&new->horizontal, &pos->horizontal);
		new->hw.qh.head = MAKE_QH_ADDR (pos) ;
		mb();
		if (!(old->hw.qh.head & UHCI_PTR_TERM))
			old->hw.qh.head = MAKE_QH_ADDR (new) ;
	}
	else {
		// (POS) (OLD) -> (POS) (NEW) (OLD)
		old = list_entry (pos->horizontal.next, uhci_desc_t, horizontal);
		list_add (&new->horizontal, &pos->horizontal);
		new->hw.qh.head = MAKE_QH_ADDR (old);
		mb();
		pos->hw.qh.head = MAKE_QH_ADDR (new) ;
	}

	mb ();
	
	spin_unlock_irqrestore (&s->qh_lock, xxx);
	
	return 0;
}
/*-------------------------------------------------------------------*/
_static int unlink_qh (puhci_t s, puhci_desc_t element)
{
	puhci_desc_t next, prev;
	unsigned long xxx;

	spin_lock_irqsave (&s->qh_lock, xxx);
	
	next = list_entry (element->horizontal.next, uhci_desc_t, horizontal);
	prev = list_entry (element->horizontal.prev, uhci_desc_t, horizontal);
	prev->hw.qh.head = element->hw.qh.head;
	mb ();
	list_del (&element->horizontal);
	
	spin_unlock_irqrestore (&s->qh_lock, xxx);
	
	return 0;
}
/*-------------------------------------------------------------------*/
_static int delete_qh (puhci_t s, puhci_desc_t qh)
{
	puhci_desc_t td;
	struct list_head *p;

	list_del (&qh->horizontal);
	
	while ((p = qh->vertical.next) != &qh->vertical) {
		td = list_entry (p, uhci_desc_t, vertical);
		unlink_td (s, td);
		delete_desc (td);
	}
	
	delete_desc (qh);
	
	return 0;
}
/*-------------------------------------------------------------------*/
_static void clean_td_chain (puhci_desc_t td)
{
	struct list_head *p;
	puhci_desc_t td1;

	if (!td)
		return;
	
	while ((p = td->horizontal.next) != &td->horizontal) {
		td1 = list_entry (p, uhci_desc_t, horizontal);
		delete_desc (td1);
	}
	
	delete_desc (td);
}
/*-------------------------------------------------------------------*/
// Removes ALL qhs in chain (paranoia!)
_static void cleanup_skel (puhci_t s)
{
	unsigned int n;
	puhci_desc_t td;

	dbg("cleanup_skel");
	
	for (n = 0; n < 8; n++) {
		td = s->int_chain[n];
		clean_td_chain (td);
	}

	if (s->iso_td) {
		for (n = 0; n < 1024; n++) {
			td = s->iso_td[n];
			clean_td_chain (td);
		}
		kfree (s->iso_td);
	}

	if (s->framelist)
		free_page ((unsigned long) s->framelist);

	if (s->control_chain) {
		// completed init_skel?
		struct list_head *p;
		puhci_desc_t qh, qh1;

		qh = s->control_chain;
		while ((p = qh->horizontal.next) != &qh->horizontal) {
			qh1 = list_entry (p, uhci_desc_t, horizontal);
			delete_qh (s, qh1);
		}
		delete_qh (s, qh);
	}
	else {
		if (s->control_chain)
			kfree (s->control_chain);
		if (s->bulk_chain)
			kfree (s->bulk_chain);
		if (s->chain_end)
			kfree (s->chain_end);
	}
	dbg("cleanup_skel finished");	
}
/*-------------------------------------------------------------------*/
// allocates framelist and qh-skeletons
// only HW-links provide continous linking, SW-links stay in their domain (ISO/INT)
_static int init_skel (puhci_t s)
{
	int n, ret;
	puhci_desc_t qh, td;
	
	dbg("init_skel");
	
	s->framelist = (__u32 *) get_free_page (GFP_KERNEL);

	if (!s->framelist)
		return -ENOMEM;

	memset (s->framelist, 0, 4096);

	dbg("allocating iso desc pointer list");
	s->iso_td = (puhci_desc_t *) kmalloc (1024 * sizeof (puhci_desc_t), GFP_KERNEL);
	
	if (!s->iso_td)
		goto init_skel_cleanup;

	s->control_chain = NULL;
	s->bulk_chain = NULL;
	s->chain_end = NULL;

	dbg("allocating iso descs");
	for (n = 0; n < 1024; n++) {
	 	// allocate skeleton iso/irq-tds
		ret = alloc_td (&td, 0);
		if (ret)
			goto init_skel_cleanup;
		s->iso_td[n] = td;
		s->framelist[n] = ((__u32) virt_to_bus (td));
	}

	dbg("allocating qh: chain_end");
	ret = alloc_qh (&qh);
	
	if (ret)
		goto init_skel_cleanup;
	
	s->chain_end = qh;

	dbg("allocating qh: bulk_chain");
	ret = alloc_qh (&qh);
	
	if (ret)
		goto init_skel_cleanup;
	
	insert_qh (s, s->chain_end, qh, 0);
	s->bulk_chain = qh;
	dbg("allocating qh: control_chain");
	ret = alloc_qh (&qh);
	
	if (ret)
		goto init_skel_cleanup;
	
	insert_qh (s, s->bulk_chain, qh, 0);
	s->control_chain = qh;
	for (n = 0; n < 8; n++)
		s->int_chain[n] = 0;

	dbg("allocating skeleton INT-TDs");
	
	for (n = 0; n < 8; n++) {
		puhci_desc_t td;

		alloc_td (&td, 0);
		if (!td)
			goto init_skel_cleanup;
		s->int_chain[n] = td;
		if (n == 0) {
			s->int_chain[0]->hw.td.link = virt_to_bus (s->control_chain) | UHCI_PTR_QH;
		}
		else {
			s->int_chain[n]->hw.td.link = virt_to_bus (s->int_chain[0]);
		}
	}

	dbg("Linking skeleton INT-TDs");
	
	for (n = 0; n < 1024; n++) {
		// link all iso-tds to the interrupt chains
		int m, o;
		dbg("framelist[%i]=%x",n,s->framelist[n]);
		if ((n&127)==127) 
			((puhci_desc_t) s->iso_td[n])->hw.td.link = virt_to_bus(s->int_chain[0]);
		else {
			for (o = 1, m = 2; m <= 128; o++, m += m) {
				// n&(m-1) = n%m
				if ((n & (m - 1)) == ((m - 1) / 2)) {
					((puhci_desc_t) s->iso_td[n])->hw.td.link = virt_to_bus (s->int_chain[o]);
				}
			}
		}
	}

	mb();
	//uhci_show_queue(s->control_chain);   
	dbg("init_skel exit");
	return 0;		// OK

      init_skel_cleanup:
	cleanup_skel (s);
	return -ENOMEM;
}

/*-------------------------------------------------------------------*/
_static void fill_td (puhci_desc_t td, int status, int info, __u32 buffer)
{
	td->hw.td.status = status;
	td->hw.td.info = info;
	td->hw.td.buffer = buffer;
}

/*-------------------------------------------------------------------*/
//                         LOW LEVEL STUFF
//          assembles QHs und TDs for control, bulk and iso
/*-------------------------------------------------------------------*/
_static int uhci_submit_control_urb (purb_t purb)
{
	puhci_desc_t qh, td;
	puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
	purb_priv_t purb_priv = purb->hcpriv;
	unsigned long destination, status;
	int maxsze = usb_maxpacket (purb->dev, purb->pipe, usb_pipeout (purb->pipe));
	unsigned long len, bytesrequested;
	char *data;

	dbg("uhci_submit_control start");
	alloc_qh (&qh);		// alloc qh for this request

	if (!qh)
		return -ENOMEM;

	alloc_td (&td, UHCI_PTR_DEPTH);		// get td for setup stage

	if (!td) {
		delete_qh (s, qh);
		return -ENOMEM;
	}

	/* The "pipe" thing contains the destination in bits 8--18 */
	destination = (purb->pipe & PIPE_DEVEP_MASK) | USB_PID_SETUP;

	/* 3 errors */
	status = (purb->pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE |
		(purb->transfer_flags & USB_DISABLE_SPD ? 0 : TD_CTRL_SPD) | (3 << 27);

	/*  Build the TD for the control request, try forever, 8 bytes of data */
	fill_td (td, status, destination | (7 << 21), virt_to_bus (purb->setup_packet));

	/* If direction is "send", change the frame from SETUP (0x2D)
	   to OUT (0xE1). Else change it from SETUP to IN (0x69). */

	destination ^= (USB_PID_SETUP ^ USB_PID_IN);	/* SETUP -> IN */
	
	if (usb_pipeout (purb->pipe))
		destination ^= (USB_PID_IN ^ USB_PID_OUT);	/* IN -> OUT */

	insert_td (s, qh, td, 0);	// queue 'setup stage'-td in qh
#if 0
	dbg("SETUP to pipe %x: %x %x %x %x %x %x %x %x", purb->pipe,
		purb->setup_packet[0], purb->setup_packet[1], purb->setup_packet[2], purb->setup_packet[3],
		purb->setup_packet[4], purb->setup_packet[5], purb->setup_packet[6], purb->setup_packet[7]);
	//uhci_show_td(td);
#endif

	/*  Build the DATA TD's */
	len = purb->transfer_buffer_length;
	bytesrequested = len;
	data = purb->transfer_buffer;
	
	while (len > 0) {
		int pktsze = len;

		alloc_td (&td, UHCI_PTR_DEPTH);
		if (!td) {
			delete_qh (s, qh);
			return -ENOMEM;
		}

		if (pktsze > maxsze)
			pktsze = maxsze;

		destination ^= 1 << TD_TOKEN_TOGGLE;	// toggle DATA0/1

		fill_td (td, status, destination | ((pktsze - 1) << 21),
			 virt_to_bus (data));	// Status, pktsze bytes of data

		insert_td (s, qh, td, UHCI_PTR_DEPTH);	// queue 'data stage'-td in qh

		data += pktsze;
		len -= pktsze;
	}

	/*  Build the final TD for control status */
	/* It's only IN if the pipe is out AND we aren't expecting data */
	destination &= ~0xFF;
	
	if (usb_pipeout (purb->pipe) | (bytesrequested == 0))
		destination |= USB_PID_IN;
	else
		destination |= USB_PID_OUT;

	destination |= 1 << TD_TOKEN_TOGGLE;	/* End in Data1 */

	alloc_td (&td, UHCI_PTR_DEPTH);
	
	if (!td) {
		delete_qh (s, qh);
		return -ENOMEM;
	}

	/* no limit on errors on final packet , 0 bytes of data */
	fill_td (td, status | TD_CTRL_IOC, destination | (UHCI_NULL_DATA_SIZE << 21),
		 0);

	insert_td (s, qh, td, UHCI_PTR_DEPTH);	// queue status td


	list_add (&qh->desc_list, &purb_priv->desc_list);

	purb->status = USB_ST_URB_PENDING;
	queue_urb (s, &purb->urb_list,1);	// queue before inserting in desc chain

	//uhci_show_queue(qh);

	/* Start it up... put low speed first */
	if (purb->pipe & TD_CTRL_LS)
		insert_qh (s, s->control_chain, qh, 1);	// insert after control chain
	else
		insert_qh (s, s->bulk_chain, qh, 0);	// insert before bulk chain
	
	//uhci_show_queue(qh);
	dbg("uhci_submit_control end");
	return 0;
}
/*-------------------------------------------------------------------*/
_static int uhci_submit_bulk_urb (purb_t purb)
{
	puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
	purb_priv_t purb_priv = purb->hcpriv;
	puhci_desc_t qh, td;
	unsigned long destination, status;
	char *data;
	unsigned int pipe = purb->pipe;
	int maxsze = usb_maxpacket (purb->dev, pipe, usb_pipeout (pipe));
	int info, len;

	/* shouldn't the clear_halt be done in the USB core or in the client driver? - Thomas */
	if (usb_endpoint_halted (purb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe)) &&
	    usb_clear_halt (purb->dev, usb_pipeendpoint (pipe) | (pipe & USB_DIR_IN)))
		return -EPIPE;

	if (!maxsze)
		return -EMSGSIZE;
	/* FIXME: should tell the client that the endpoint is invalid, i.e. not in the descriptor */

	alloc_qh (&qh);		// get qh for this request

	if (!qh)
		return -ENOMEM;

	/* The "pipe" thing contains the destination in bits 8--18. */
	destination = (pipe & PIPE_DEVEP_MASK) | usb_packetid (pipe);

	/* 3 errors */
	status = (pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE |
		((purb->transfer_flags & USB_DISABLE_SPD) ? 0 : TD_CTRL_SPD) | (3 << 27);

	/* Build the TDs for the bulk request */
	len = purb->transfer_buffer_length;
	data = purb->transfer_buffer;
	dbg("uhci_submit_bulk_urb: pipe %x, len %d", pipe, len);
	
	while (len > 0) {
		int pktsze = len;

		alloc_td (&td, UHCI_PTR_DEPTH);

		if (!td) {
			delete_qh (s, qh);
			return -ENOMEM;
		}

		if (pktsze > maxsze)
			pktsze = maxsze;

		// pktsze bytes of data 
		info = destination | ((pktsze - 1) << 21) |
			(usb_gettoggle (purb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe)) << TD_TOKEN_TOGGLE);

		fill_td (td, status, info, virt_to_bus (data));

		data += pktsze;
		len -= pktsze;

		if (!len)
			td->hw.td.status |= TD_CTRL_IOC;	// last one generates INT
		//dbg("insert td %p, len %i",td,pktsze);

		insert_td (s, qh, td, UHCI_PTR_DEPTH);
		
		/* Alternate Data0/1 (start with Data0) */
		usb_dotoggle (purb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe));
	}

	list_add (&qh->desc_list, &purb_priv->desc_list);

	purb->status = USB_ST_URB_PENDING;
	queue_urb (s, &purb->urb_list,1);

	insert_qh (s, s->chain_end, qh, 0);	// insert before end marker
	//uhci_show_queue(s->bulk_chain);

	dbg("uhci_submit_bulk_urb: exit");
	return 0;
}
/*-------------------------------------------------------------------*/
// unlinks an urb by dequeuing its qh, waits some frames and forgets it
// Problem: unlinking in interrupt requires waiting for one frame (udelay)
// to allow the whole structures to be safely removed
_static int uhci_unlink_urb (purb_t purb)
{
	puhci_t s;
	puhci_desc_t qh;
	puhci_desc_t td;
	purb_priv_t purb_priv;
	unsigned long flags=0;
	struct list_head *p;

	if (!purb)		// you never know...
		return -1;

	s = (puhci_t) purb->dev->bus->hcpriv;	// get pointer to uhci struct

	if (usb_pipedevice (purb->pipe) == s->rh.devnum)
		return rh_unlink_urb (purb);

	if(!in_interrupt()) {
		spin_lock_irqsave (&s->unlink_urb_lock, flags);		// do not allow interrupts
	}
	
	//dbg("unlink_urb called %p",purb);
	if (purb->status == USB_ST_URB_PENDING) {
		// URB probably still in work
		purb_priv = purb->hcpriv;
		dequeue_urb (s, &purb->urb_list,1);
		purb->status = USB_ST_URB_KILLED;	// mark urb as killed
		
		if(!in_interrupt()) {
			spin_unlock_irqrestore (&s->unlink_urb_lock, flags);	// allow interrupts from here
		}

		switch (usb_pipetype (purb->pipe)) {
		case PIPE_ISOCHRONOUS:
		case PIPE_INTERRUPT:
			for (p = purb_priv->desc_list.next; p != &purb_priv->desc_list; p = p->next) {
				td = list_entry (p, uhci_desc_t, desc_list);
				unlink_td (s, td);
			}
			// wait at least 1 Frame
			if (in_interrupt ())
				udelay (1000);
			else
				wait_ms(1);
			while ((p = purb_priv->desc_list.next) != &purb_priv->desc_list) {
				td = list_entry (p, uhci_desc_t, desc_list);
				list_del (p);
				delete_desc (td);
			}
			break;

		case PIPE_BULK:
		case PIPE_CONTROL:
			qh = list_entry (purb_priv->desc_list.next, uhci_desc_t, desc_list);

			unlink_qh (s, qh);	// remove this qh from qh-list
			// wait at least 1 Frame

			if (in_interrupt ())
				udelay (1000);
			else
				wait_ms(1);
			delete_qh (s, qh);		// remove it physically

		}
		
#ifdef DEBUG_SLAB
		kmem_cache_free(urb_priv_kmem, purb->hcpriv);
#else
		kfree (purb->hcpriv);
#endif
		if (purb->complete) {
			dbg("unlink_urb: calling completion");
			purb->complete ((struct urb *) purb);
			usb_dec_dev_use (purb->dev);
		}
		return 0;
	}
	else {
		if(!in_interrupt())
			spin_unlock_irqrestore (&s->unlink_urb_lock, flags);	// allow interrupts from here
	}

	return 0;
}
/*-------------------------------------------------------------------*/
// In case of ASAP iso transfer, search the URB-list for already queued URBs
// for this EP and calculate the earliest start frame for the new
// URB (easy seamless URB continuation!)
_static int find_iso_limits (purb_t purb, unsigned int *start, unsigned int *end)
{
	purb_t u, last_urb = NULL;
	puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
	struct list_head *p = s->urb_list.next;
	int ret=-1;
	unsigned long flags;
	
	spin_lock_irqsave (&s->urb_list_lock, flags);

	for (; p != &s->urb_list; p = p->next) {
		u = list_entry (p, urb_t, urb_list);
		// look for pending URBs with identical pipe handle
		// works only because iso doesn't toggle the data bit!
		if ((purb->pipe == u->pipe) && (purb->dev == u->dev) && (u->status == USB_ST_URB_PENDING)) {
			if (!last_urb)
				*start = u->start_frame;
			last_urb = u;
		}
	}
	
	if (last_urb) {
		*end = (last_urb->start_frame + last_urb->number_of_packets) & 1023;
		ret=0;
	}
	
	spin_unlock_irqrestore(&s->urb_list_lock, flags);
	
	return ret;	// no previous urb found

}
/*-------------------------------------------------------------------*/
// adjust start_frame according to scheduling constraints (ASAP etc)

_static int iso_find_start (purb_t purb)
{
	puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
	unsigned int now;
	unsigned int start_limit = 0, stop_limit = 0, queued_size;
	int limits;

	now = UHCI_GET_CURRENT_FRAME (s) & 1023;

	if ((unsigned) purb->number_of_packets > 900)
		return -EFBIG;
	
	limits = find_iso_limits (purb, &start_limit, &stop_limit);
	queued_size = (stop_limit - start_limit) & 1023;

	if (purb->transfer_flags & USB_ISO_ASAP) {
		// first iso
		if (limits) {
			// 10ms setup should be enough //FIXME!
			purb->start_frame = (now + 10) & 1023;
		}
		else {
			purb->start_frame = stop_limit;		//seamless linkage

			if (((now - purb->start_frame) & 1023) <= (unsigned) purb->number_of_packets) {
				dbg("iso_find_start: warning, ASAP gap, should not happen");
				dbg("iso_find_start: now %u start_frame %u number_of_packets %u pipe 0x%08x",
					now, purb->start_frame, purb->number_of_packets, purb->pipe);
// The following code is only for debugging purposes...
#if 0
				{
					puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
					struct list_head *p;
					purb_t u;
					int a = -1, b = -1;
					unsigned long flags;

					spin_lock_irqsave (&s->urb_list_lock, flags);
					p=s->urb_list.next;

					for (; p != &s->urb_list; p = p->next) {
						u = list_entry (p, urb_t, urb_list);
						if (purb->dev != u->dev)
							continue;
						dbg("urb: pipe 0x%08x status %d start_frame %u number_of_packets %u",
							u->pipe, u->status, u->start_frame, u->number_of_packets);
						if (!usb_pipeisoc (u->pipe))
							continue;
						if (a == -1)
							a = u->start_frame;
						b = (u->start_frame + u->number_of_packets - 1) & 1023;
					}
					spin_unlock_irqrestore(&s->urb_list_lock, flags);
				}
#endif
				purb->start_frame = (now + 5) & 1023;	// 5ms setup should be enough //FIXME!
				//return -EAGAIN; //FIXME
			}
		}
	}
	else {
		purb->start_frame &= 1023;
		if (((now - purb->start_frame) & 1023) < (unsigned) purb->number_of_packets) {
			dbg("iso_find_start: now between start_frame and end");
			return -EAGAIN;
		}
	}

	/* check if either start_frame or start_frame+number_of_packets-1 lies between start_limit and stop_limit */
	if (limits)
		return 0;

	if (((purb->start_frame - start_limit) & 1023) < queued_size ||
	    ((purb->start_frame + purb->number_of_packets - 1 - start_limit) & 1023) < queued_size) {
		dbg("iso_find_start: start_frame %u number_of_packets %u start_limit %u stop_limit %u",
			purb->start_frame, purb->number_of_packets, start_limit, stop_limit);
		return -EAGAIN;
	}

	return 0;
}
/*-------------------------------------------------------------------*/
// submits USB interrupt (ie. polling ;-) 
// ASAP-flag set implicitely
// if period==0, the the transfer is only done once (usb_scsi need this...)

_static int uhci_submit_int_urb (purb_t purb)
{
	puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
	purb_priv_t purb_priv = purb->hcpriv;
	int nint, n, ret;
	puhci_desc_t td;
	int status, destination;
	int now;
	int info;
	unsigned int pipe = purb->pipe;

	//dbg("SUBMIT INT");

	if (purb->interval < 0 || purb->interval >= 256)
		return -EINVAL;

	if (purb->interval == 0)
		nint = 0;
	else {
		for (nint = 0, n = 1; nint <= 8; nint++, n += n)	// round interval down to 2^n
		 {
			if (purb->interval < n) {
				purb->interval = n / 2;
				break;
			}
		}
		nint--;
	}
	dbg("Rounded interval to %i, chain  %i", purb->interval, nint);

	now = UHCI_GET_CURRENT_FRAME (s) & 1023;
	purb->start_frame = now;	// remember start frame, just in case...

	purb->number_of_packets = 1;

	// INT allows only one packet
	if (purb->transfer_buffer_length > usb_maxpacket (purb->dev, pipe, usb_pipeout (pipe)))
		return -EINVAL;

	ret = alloc_td (&td, UHCI_PTR_DEPTH);

	if (ret)
		return -ENOMEM;

	status = (pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_IOC |
		(purb->transfer_flags & USB_DISABLE_SPD ? 0 : TD_CTRL_SPD) | (3 << 27);

	destination = (purb->pipe & PIPE_DEVEP_MASK) | usb_packetid (purb->pipe) |
		(((purb->transfer_buffer_length - 1) & 0x7ff) << 21);


	info = destination | (usb_gettoggle (purb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe)) << TD_TOKEN_TOGGLE);

	fill_td (td, status, info, virt_to_bus (purb->transfer_buffer));
	list_add_tail (&td->desc_list, &purb_priv->desc_list);

	purb->status = USB_ST_URB_PENDING;
	queue_urb (s, &purb->urb_list,1);

	insert_td_horizontal (s, s->int_chain[nint], td, UHCI_PTR_DEPTH);	// store in INT-TDs

	usb_dotoggle (purb->dev, usb_pipeendpoint (pipe), usb_pipeout (pipe));

#if 0
	td = tdm[purb->number_of_packets];
	fill_td (td, TD_CTRL_IOC, 0, 0);
	insert_td_horizontal (s, s->iso_td[(purb->start_frame + (purb->number_of_packets) * purb->interval + 1) & 1023], td, UHCI_PTR_DEPTH);
	list_add_tail (&td->desc_list, &purb_priv->desc_list);
#endif

	return 0;
}
/*-------------------------------------------------------------------*/
_static int uhci_submit_iso_urb (purb_t purb)
{
	puhci_t s = (puhci_t) purb->dev->bus->hcpriv;
	purb_priv_t purb_priv = purb->hcpriv;
	int pipe=purb->pipe;
	int maxsze = usb_maxpacket (purb->dev, pipe, usb_pipeout (pipe));
	int n, ret, last=0;
	puhci_desc_t td, *tdm;
	int status, destination;
	unsigned long flags;
	spinlock_t lock;

	spin_lock_init (&lock);
	spin_lock_irqsave (&lock, flags);	// Disable IRQs to schedule all ISO-TDs in time

	ret = iso_find_start (purb);	// adjusts purb->start_frame for later use

	if (ret)
		goto err;

	tdm = (puhci_desc_t *) kmalloc (purb->number_of_packets * sizeof (puhci_desc_t), in_interrupt ()? GFP_ATOMIC : GFP_KERNEL);

	if (!tdm) {
		ret = -ENOMEM;
		goto err;
	}

	// First try to get all TDs
	for (n = 0; n < purb->number_of_packets; n++) {
		dbg("n:%d purb->iso_frame_desc[n].length:%d", n, purb->iso_frame_desc[n].length);
		if (!purb->iso_frame_desc[n].length) {
			// allows ISO striping by setting length to zero in iso_descriptor
			tdm[n] = 0;
			continue;
		}
		if(purb->iso_frame_desc[n].length > maxsze) {
			err("submit_iso: purb->iso_frame_desc[%d].length(%d)>%d",n , purb->iso_frame_desc[n].length, maxsze);
			tdm[n] = 0;
			continue;
		}
		ret = alloc_td (&td, UHCI_PTR_DEPTH);
		if (ret) {
			int i;	// Cleanup allocated TDs

			for (i = 0; i < n; n++)
				if (tdm[i])
					kfree (tdm[i]);
			kfree (tdm);
			ret = -ENOMEM;
			goto err;
		}
		last=n;
		tdm[n] = td;
	}

	status = TD_CTRL_ACTIVE | TD_CTRL_IOS;	//| (purb->transfer_flags&USB_DISABLE_SPD?0:TD_CTRL_SPD);

	destination = (purb->pipe & PIPE_DEVEP_MASK) | usb_packetid (purb->pipe);

	
	// Queue all allocated TDs
	for (n = 0; n < purb->number_of_packets; n++) {
		td = tdm[n];
		if (!td)
			continue;
			
		if (n  == last)
			status |= TD_CTRL_IOC;

		fill_td (td, status, destination | (((purb->iso_frame_desc[n].length - 1) & 0x7ff) << 21),
			 virt_to_bus (purb->transfer_buffer + purb->iso_frame_desc[n].offset));
		list_add_tail (&td->desc_list, &purb_priv->desc_list);
	
		if (n == last) {
			purb->status = USB_ST_URB_PENDING;
			queue_urb (s, &purb->urb_list,1);
		}
		insert_td_horizontal (s, s->iso_td[(purb->start_frame + n) & 1023], td, UHCI_PTR_DEPTH);	// store in iso-tds
		//uhci_show_td(td);

	}

	kfree (tdm);
	dbg("ISO-INT# %i, start %i, now %i", purb->number_of_packets, purb->start_frame, UHCI_GET_CURRENT_FRAME (s) & 1023);
	ret = 0;

      err:
	spin_unlock_irqrestore (&lock, flags);
	return ret;

}
/*-------------------------------------------------------------------*/
_static int search_dev_ep (puhci_t s, purb_t purb)
{
	unsigned long flags;
	struct list_head *p = s->urb_list.next;
	purb_t tmp;
	unsigned int mask = usb_pipecontrol(purb->pipe) ? (~USB_DIR_IN) : (~0);

	dbg("search_dev_ep:");
	spin_lock_irqsave (&s->urb_list_lock, flags);
	for (; p != &s->urb_list; p = p->next) {
		tmp = list_entry (p, urb_t, urb_list);
		dbg("urb: %p", tmp);
		// we can accept this urb if it is not queued at this time 
		// or if non-iso transfer requests should be scheduled for the same device and pipe
		if ((!usb_pipeisoc(purb->pipe) && tmp->dev == purb->dev && !((tmp->pipe ^ purb->pipe) & mask)) ||
		    (purb == tmp)) {
			spin_unlock_irqrestore (&s->urb_list_lock, flags);
			return 1;	// found another urb already queued for processing
		}
	}
	spin_unlock_irqrestore (&s->urb_list_lock, flags);
	return 0;
}
/*-------------------------------------------------------------------*/
_static int uhci_submit_urb (purb_t purb)
{
	puhci_t s;
	purb_priv_t purb_priv;
	int ret = 0;

	if (!purb->dev || !purb->dev->bus)
		return -ENODEV;

	s = (puhci_t) purb->dev->bus->hcpriv;
	//dbg("submit_urb: %p type %d",purb,usb_pipetype(purb->pipe));

	if (usb_pipedevice (purb->pipe) == s->rh.devnum)
		return rh_submit_urb (purb);	/* virtual root hub */

	usb_inc_dev_use (purb->dev);

	if (search_dev_ep (s, purb)) {
		usb_dec_dev_use (purb->dev);
		return -ENXIO;	// urb already queued

	}

#ifdef DEBUG_SLAB
	purb_priv = kmem_cache_alloc(urb_priv_kmem, in_interrupt ()? SLAB_ATOMIC : SLAB_KERNEL);
#else
	purb_priv = kmalloc (sizeof (urb_priv_t), in_interrupt ()? GFP_ATOMIC : GFP_KERNEL);
#endif
	if (!purb_priv) {
		usb_dec_dev_use (purb->dev);
		return -ENOMEM;
	}

	purb->hcpriv = purb_priv;
	INIT_LIST_HEAD (&purb_priv->desc_list);
	purb_priv->short_control_packet=0;
	dbg("submit_urb: scheduling %p", purb);

	switch (usb_pipetype (purb->pipe)) {
	case PIPE_ISOCHRONOUS:
		ret = uhci_submit_iso_urb (purb);
		break;
	case PIPE_INTERRUPT:
		ret = uhci_submit_int_urb (purb);
		break;
	case PIPE_CONTROL:
		//dump_urb (purb);
		ret = uhci_submit_control_urb (purb);
		break;
	case PIPE_BULK:
		ret = uhci_submit_bulk_urb (purb);
		break;
	default:
		ret = -EINVAL;
	}

	dbg("submit_urb: scheduled with ret: %d", ret);

	if (ret != USB_ST_NOERROR) {
		usb_dec_dev_use (purb->dev);
#ifdef DEBUG_SLAB
		kmem_cache_free(urb_priv_kmem, purb_priv);
#else
		kfree (purb_priv);
#endif
		return ret;
	}
/*
	purb->status = USB_ST_URB_PENDING;
	queue_urb (s, &purb->urb_list,1);
	dbg("submit_urb: exit");
*/
	return 0;
}
/*-------------------------------------------------------------------
 Virtual Root Hub
 -------------------------------------------------------------------*/

_static __u8 root_hub_dev_des[] =
{
	0x12,			/*  __u8  bLength; */
	0x01,			/*  __u8  bDescriptorType; Device */
	0x00,			/*  __u16 bcdUSB; v1.0 */
	0x01,
	0x09,			/*  __u8  bDeviceClass; HUB_CLASSCODE */
	0x00,			/*  __u8  bDeviceSubClass; */
	0x00,			/*  __u8  bDeviceProtocol; */
	0x08,			/*  __u8  bMaxPacketSize0; 8 Bytes */
	0x00,			/*  __u16 idVendor; */
	0x00,
	0x00,			/*  __u16 idProduct; */
	0x00,
	0x00,			/*  __u16 bcdDevice; */
	0x00,
	0x00,			/*  __u8  iManufacturer; */
	0x00,			/*  __u8  iProduct; */
	0x00,			/*  __u8  iSerialNumber; */
	0x01			/*  __u8  bNumConfigurations; */
};


/* Configuration descriptor */
_static __u8 root_hub_config_des[] =
{
	0x09,			/*  __u8  bLength; */
	0x02,			/*  __u8  bDescriptorType; Configuration */
	0x19,			/*  __u16 wTotalLength; */
	0x00,
	0x01,			/*  __u8  bNumInterfaces; */
	0x01,			/*  __u8  bConfigurationValue; */
	0x00,			/*  __u8  iConfiguration; */
	0x40,			/*  __u8  bmAttributes; 
				   Bit 7: Bus-powered, 6: Self-powered, 5 Remote-wakwup, 4..0: resvd */
	0x00,			/*  __u8  MaxPower; */

     /* interface */
	0x09,			/*  __u8  if_bLength; */
	0x04,			/*  __u8  if_bDescriptorType; Interface */
	0x00,			/*  __u8  if_bInterfaceNumber; */
	0x00,			/*  __u8  if_bAlternateSetting; */
	0x01,			/*  __u8  if_bNumEndpoints; */
	0x09,			/*  __u8  if_bInterfaceClass; HUB_CLASSCODE */
	0x00,			/*  __u8  if_bInterfaceSubClass; */
	0x00,			/*  __u8  if_bInterfaceProtocol; */
	0x00,			/*  __u8  if_iInterface; */

     /* endpoint */
	0x07,			/*  __u8  ep_bLength; */
	0x05,			/*  __u8  ep_bDescriptorType; Endpoint */
	0x81,			/*  __u8  ep_bEndpointAddress; IN Endpoint 1 */
	0x03,			/*  __u8  ep_bmAttributes; Interrupt */
	0x08,			/*  __u16 ep_wMaxPacketSize; 8 Bytes */
	0x00,
	0xff			/*  __u8  ep_bInterval; 255 ms */
};


_static __u8 root_hub_hub_des[] =
{
	0x09,			/*  __u8  bLength; */
	0x29,			/*  __u8  bDescriptorType; Hub-descriptor */
	0x02,			/*  __u8  bNbrPorts; */
	0x00,			/* __u16  wHubCharacteristics; */
	0x00,
	0x01,			/*  __u8  bPwrOn2pwrGood; 2ms */
	0x00,			/*  __u8  bHubContrCurrent; 0 mA */
	0x00,			/*  __u8  DeviceRemovable; *** 7 Ports max *** */
	0xff			/*  __u8  PortPwrCtrlMask; *** 7 ports max *** */
};

/*-------------------------------------------------------------------------*/
/* prepare Interrupt pipe transaction data; HUB INTERRUPT ENDPOINT */
_static int rh_send_irq (purb_t purb)
{

	int len = 1;
	int i;
	puhci_t uhci = purb->dev->bus->hcpriv;
	unsigned int io_addr = uhci->io_addr;
	__u16 data = 0;

	for (i = 0; i < uhci->rh.numports; i++) {
		data |= ((inw (io_addr + USBPORTSC1 + i * 2) & 0xa) > 0 ? (1 << (i + 1)) : 0);
		len = (i + 1) / 8 + 1;
	}

	*(__u16 *) purb->transfer_buffer = cpu_to_le16 (data);
	purb->actual_length = len;
	purb->status = USB_ST_NOERROR;

	if ((data > 0) && (uhci->rh.send != 0)) {
		dbg("Root-Hub INT complete: port1: %x port2: %x data: %x",
		     inw (io_addr + USBPORTSC1), inw (io_addr + USBPORTSC2), data);
		purb->complete (purb);

	}
	return USB_ST_NOERROR;
}

/*-------------------------------------------------------------------------*/
/* Virtual Root Hub INTs are polled by this timer every "intervall" ms */
_static int rh_init_int_timer (purb_t purb);

_static void rh_int_timer_do (unsigned long ptr)
{
	int len;

	purb_t purb = (purb_t) ptr;
	puhci_t uhci = purb->dev->bus->hcpriv;

	if (uhci->rh.send) {
		len = rh_send_irq (purb);
		if (len > 0) {
			purb->actual_length = len;
			if (purb->complete)
				purb->complete (purb);
		}
	}
	rh_init_int_timer (purb);
}

/*-------------------------------------------------------------------------*/
/* Root Hub INTs are polled by this timer */
_static int rh_init_int_timer (purb_t purb)
{
	puhci_t uhci = purb->dev->bus->hcpriv;

	uhci->rh.interval = purb->interval;
	init_timer (&uhci->rh.rh_int_timer);
	uhci->rh.rh_int_timer.function = rh_int_timer_do;
	uhci->rh.rh_int_timer.data = (unsigned long) purb;
	uhci->rh.rh_int_timer.expires = jiffies + (HZ * (purb->interval < 30 ? 30 : purb->interval)) / 1000;
	add_timer (&uhci->rh.rh_int_timer);

	return 0;
}

/*-------------------------------------------------------------------------*/
#define OK(x) 			len = (x); break

#define CLR_RH_PORTSTAT(x) \
		status = inw(io_addr+USBPORTSC1+2*(wIndex-1)); \
		status = (status & 0xfff5) & ~(x); \
		outw(status, io_addr+USBPORTSC1+2*(wIndex-1))

#define SET_RH_PORTSTAT(x) \
		status = inw(io_addr+USBPORTSC1+2*(wIndex-1)); \
		status = (status & 0xfff5) | (x); \
		outw(status, io_addr+USBPORTSC1+2*(wIndex-1))


/*-------------------------------------------------------------------------*/
/****
 ** Root Hub Control Pipe
 *************************/


_static int rh_submit_urb (purb_t purb)
{
	struct usb_device *usb_dev = purb->dev;
	puhci_t uhci = usb_dev->bus->hcpriv;
	unsigned int pipe = purb->pipe;
	devrequest *cmd = (devrequest *) purb->setup_packet;
	void *data = purb->transfer_buffer;
	int leni = purb->transfer_buffer_length;
	int len = 0;
	int status = 0;
	int stat = USB_ST_NOERROR;
	int i;
	unsigned int io_addr = uhci->io_addr;
	__u16 cstatus;

	__u16 bmRType_bReq;
	__u16 wValue;
	__u16 wIndex;
	__u16 wLength;

	if (usb_pipetype (pipe) == PIPE_INTERRUPT) {
		dbg("Root-Hub submit IRQ: every %d ms", purb->interval);
		uhci->rh.urb = purb;
		uhci->rh.send = 1;
		uhci->rh.interval = purb->interval;
		rh_init_int_timer (purb);

		return USB_ST_NOERROR;
	}


	bmRType_bReq = cmd->requesttype | cmd->request << 8;
	wValue = le16_to_cpu (cmd->value);
	wIndex = le16_to_cpu (cmd->index);
	wLength = le16_to_cpu (cmd->length);

	for (i = 0; i < 8; i++)
		uhci->rh.c_p_r[i] = 0;

	dbg("Root-Hub: adr: %2x cmd(%1x): %04x %04x %04x %04x",
	     uhci->rh.devnum, 8, bmRType_bReq, wValue, wIndex, wLength);

	switch (bmRType_bReq) {
		/* Request Destination:
		   without flags: Device, 
		   RH_INTERFACE: interface, 
		   RH_ENDPOINT: endpoint,
		   RH_CLASS means HUB here, 
		   RH_OTHER | RH_CLASS  almost ever means HUB_PORT here 
		 */

	case RH_GET_STATUS:
		*(__u16 *) data = cpu_to_le16 (1);
		OK (2);
	case RH_GET_STATUS | RH_INTERFACE:
		*(__u16 *) data = cpu_to_le16 (0);
		OK (2);
	case RH_GET_STATUS | RH_ENDPOINT:
		*(__u16 *) data = cpu_to_le16 (0);
		OK (2);
	case RH_GET_STATUS | RH_CLASS:
		*(__u32 *) data = cpu_to_le32 (0);
		OK (4);		/* hub power ** */
	case RH_GET_STATUS | RH_OTHER | RH_CLASS:
		status = inw (io_addr + USBPORTSC1 + 2 * (wIndex - 1));
		cstatus = ((status & USBPORTSC_CSC) >> (1 - 0)) |
			((status & USBPORTSC_PEC) >> (3 - 1)) |
			(uhci->rh.c_p_r[wIndex - 1] << (0 + 4));
		status = (status & USBPORTSC_CCS) |
			((status & USBPORTSC_PE) >> (2 - 1)) |
			((status & USBPORTSC_SUSP) >> (12 - 2)) |
			((status & USBPORTSC_PR) >> (9 - 4)) |
			(1 << 8) |	/* power on ** */
			((status & USBPORTSC_LSDA) << (-8 + 9));

		*(__u16 *) data = cpu_to_le16 (status);
		*(__u16 *) (data + 2) = cpu_to_le16 (cstatus);
		OK (4);

	case RH_CLEAR_FEATURE | RH_ENDPOINT:
		switch (wValue) {
		case (RH_ENDPOINT_STALL):
			OK (0);
		}
		break;

	case RH_CLEAR_FEATURE | RH_CLASS:
		switch (wValue) {
		case (RH_C_HUB_OVER_CURRENT):
			OK (0);	/* hub power over current ** */
		}
		break;

	case RH_CLEAR_FEATURE | RH_OTHER | RH_CLASS:
		switch (wValue) {
		case (RH_PORT_ENABLE):
			CLR_RH_PORTSTAT (USBPORTSC_PE);
			OK (0);
		case (RH_PORT_SUSPEND):
			CLR_RH_PORTSTAT (USBPORTSC_SUSP);
			OK (0);
		case (RH_PORT_POWER):
			OK (0);	/* port power ** */
		case (RH_C_PORT_CONNECTION):
			SET_RH_PORTSTAT (USBPORTSC_CSC);
			OK (0);
		case (RH_C_PORT_ENABLE):
			SET_RH_PORTSTAT (USBPORTSC_PEC);
			OK (0);
		case (RH_C_PORT_SUSPEND):
/*** WR_RH_PORTSTAT(RH_PS_PSSC); */
			OK (0);
		case (RH_C_PORT_OVER_CURRENT):
			OK (0);	/* port power over current ** */
		case (RH_C_PORT_RESET):
			uhci->rh.c_p_r[wIndex - 1] = 0;
			OK (0);
		}
		break;

	case RH_SET_FEATURE | RH_OTHER | RH_CLASS:
		switch (wValue) {
		case (RH_PORT_SUSPEND):
			SET_RH_PORTSTAT (USBPORTSC_SUSP);
			OK (0);
		case (RH_PORT_RESET):
			SET_RH_PORTSTAT (USBPORTSC_PR);
			wait_ms (10);
			uhci->rh.c_p_r[wIndex - 1] = 1;
			CLR_RH_PORTSTAT (USBPORTSC_PR);
			udelay (10);
			SET_RH_PORTSTAT (USBPORTSC_PE);
			wait_ms (10);
			SET_RH_PORTSTAT (0xa);
			OK (0);
		case (RH_PORT_POWER):
			OK (0);	/* port power ** */
		case (RH_PORT_ENABLE):
			SET_RH_PORTSTAT (USBPORTSC_PE);
			OK (0);
		}
		break;

	case RH_SET_ADDRESS:
		uhci->rh.devnum = wValue;
		OK (0);

	case RH_GET_DESCRIPTOR:
		switch ((wValue & 0xff00) >> 8) {
		case (0x01):	/* device descriptor */
			len = min (leni, min (sizeof (root_hub_dev_des), wLength));
			memcpy (data, root_hub_dev_des, len);
			OK (len);
		case (0x02):	/* configuration descriptor */
			len = min (leni, min (sizeof (root_hub_config_des), wLength));
			memcpy (data, root_hub_config_des, len);
			OK (len);
		case (0x03):	/*string descriptors */
			stat = -EPIPE;
		}
		break;

	case RH_GET_DESCRIPTOR | RH_CLASS:
		root_hub_hub_des[2] = uhci->rh.numports;
		len = min (leni, min (sizeof (root_hub_hub_des), wLength));
		memcpy (data, root_hub_hub_des, len);
		OK (len);

	case RH_GET_CONFIGURATION:
		*(__u8 *) data = 0x01;
		OK (1);

	case RH_SET_CONFIGURATION:
		OK (0);
	default:
		stat = -EPIPE;
	}


	dbg("Root-Hub stat port1: %x port2: %x",
	     inw (io_addr + USBPORTSC1), inw (io_addr + USBPORTSC2));

	purb->actual_length = len;
	purb->status = stat;
	if (purb->complete)
		purb->complete (purb);
	return USB_ST_NOERROR;
}
/*-------------------------------------------------------------------------*/

_static int rh_unlink_urb (purb_t purb)
{
	puhci_t uhci = purb->dev->bus->hcpriv;

	dbg("Root-Hub unlink IRQ");
	uhci->rh.send = 0;
	del_timer (&uhci->rh.rh_int_timer);
	return 0;
}
/*-------------------------------------------------------------------*/

/*
 * Map status to standard result codes
 *
 * <status> is (td->status & 0xFE0000) [a.k.a. uhci_status_bits(td->status)
 * <dir_out> is True for output TDs and False for input TDs.
 */
_static int uhci_map_status (int status, int dir_out)
{
	if (!status)
		return USB_ST_NOERROR;
	if (status & TD_CTRL_BITSTUFF)	/* Bitstuff error */
		return -EPROTO;
	if (status & TD_CTRL_CRCTIMEO) {	/* CRC/Timeout */
		if (dir_out)
			return -ETIMEDOUT;
		else
			return -EILSEQ;
	}
	if (status & TD_CTRL_NAK)	/* NAK */
		return -ETIMEDOUT;
	if (status & TD_CTRL_BABBLE)	/* Babble */
		return -EPIPE;
	if (status & TD_CTRL_DBUFERR)	/* Buffer error */
		return -ENOSR;
	if (status & TD_CTRL_STALLED)	/* Stalled */
		return -EPIPE;
	if (status & TD_CTRL_ACTIVE)	/* Active */
		return USB_ST_NOERROR;

	return -EPROTO;
}

/*
 * Only the USB core should call uhci_alloc_dev and uhci_free_dev
 */
_static int uhci_alloc_dev (struct usb_device *usb_dev)
{
	return 0;
}

_static int uhci_free_dev (struct usb_device *usb_dev)
{
	return 0;
}

/*
 * uhci_get_current_frame_number()
 *
 * returns the current frame number for a USB bus/controller.
 */
_static int uhci_get_current_frame_number (struct usb_device *usb_dev)
{
	return UHCI_GET_CURRENT_FRAME ((puhci_t) usb_dev->bus->hcpriv);
}

struct usb_operations uhci_device_operations =
{
	uhci_alloc_dev,
	uhci_free_dev,
	uhci_get_current_frame_number,
	uhci_submit_urb,
	uhci_unlink_urb
};

/* 
 * For IN-control transfers, process_transfer gets a bit more complicated,
 * since there are devices that return less data (eg. strings) than they
 * have announced. This leads to a queue abort due to the short packet,
 * the status stage is not executed. If this happens, the status stage
 * is manually re-executed.
 * FIXME: Stall-condition may override 'nearly' successful CTRL-IN-transfer
 * when the transfered length fits exactly in maxsze-packets. A bit
 * more intelligence is needed to detect this and finish without error.
 */
_static int process_transfer (puhci_t s, purb_t purb)
{
	int ret = USB_ST_NOERROR;
	purb_priv_t purb_priv = purb->hcpriv;
	struct list_head *qhl = purb_priv->desc_list.next;
	puhci_desc_t qh = list_entry (qhl, uhci_desc_t, desc_list);
	struct list_head *p = qh->vertical.next;
	puhci_desc_t desc= list_entry (purb_priv->desc_list.prev, uhci_desc_t, desc_list);
	puhci_desc_t last_desc = list_entry (desc->vertical.prev, uhci_desc_t, vertical);
	int data_toggle = usb_gettoggle (purb->dev, usb_pipeendpoint (purb->pipe), usb_pipeout (purb->pipe));	// save initial data_toggle


	// extracted and remapped info from TD
	int maxlength;
	int actual_length;
	int status = USB_ST_NOERROR;

	dbg("process_transfer: urb contains bulk/control request");


	/* if the status phase has been retriggered and the
	   queue is empty or the last status-TD is inactive, the retriggered
	   status stage is completed
	 */
#if 1
	if (purb_priv->short_control_packet && 
		((qh->hw.qh.element == UHCI_PTR_TERM) ||(!(last_desc->hw.td.status & TD_CTRL_ACTIVE)))) 
		goto transfer_finished;
#endif
	purb->actual_length=0;

	for (; p != &qh->vertical; p = p->next) {
		desc = list_entry (p, uhci_desc_t, vertical);

		if (desc->hw.td.status & TD_CTRL_ACTIVE)	// do not process active TDs
			return ret;

		// extract transfer parameters from TD
		actual_length = (desc->hw.td.status + 1) & 0x7ff;
		maxlength = (((desc->hw.td.info >> 21) & 0x7ff) + 1) & 0x7ff;
		status = uhci_map_status (uhci_status_bits (desc->hw.td.status), usb_pipeout (purb->pipe));

		// see if EP is stalled
		if (status == -EPIPE) {
			// set up stalled condition
			usb_endpoint_halt (purb->dev, usb_pipeendpoint (purb->pipe), usb_pipeout (purb->pipe));
		}

		// if any error occured stop processing of further TDs
		if (status != USB_ST_NOERROR) {
			// only set ret if status returned an error
			uhci_show_td (desc);
			ret = status;
			purb->error_count++;
			break;
		}
		else if ((desc->hw.td.info & 0xff) != USB_PID_SETUP)
			purb->actual_length += actual_length;

#if 0
		//	if (i++==0)
		       	uhci_show_td (desc);	// show first TD of each transfer
#endif

		// got less data than requested
		if ( (actual_length < maxlength)) {
			if (purb->transfer_flags & USB_DISABLE_SPD) {
				ret = USB_ST_SHORT_PACKET;	// treat as real error
				dbg("process_transfer: SPD!!");
				break;	// exit after this TD because SP was detected
			}

			// short read during control-IN: re-start status stage
			if ((usb_pipetype (purb->pipe) == PIPE_CONTROL)) {
				if (uhci_packetid(last_desc->hw.td.info) == USB_PID_OUT) {
			
					qh->hw.qh.element = virt_to_bus (last_desc);  // re-trigger status stage
					dbg("short packet during control transfer, retrigger status stage @ %p",last_desc);
					uhci_show_td (desc);
					uhci_show_td (last_desc);
					purb_priv->short_control_packet=1;
					return 0;
				}
			}
			// all other cases: short read is OK
			data_toggle = uhci_toggle (desc->hw.td.info);
			break;
		}

		data_toggle = uhci_toggle (desc->hw.td.info);
		//dbg("process_transfer: len:%d status:%x mapped:%x toggle:%d", actual_length, desc->hw.td.status,status, data_toggle);      

	}
	usb_settoggle (purb->dev, usb_pipeendpoint (purb->pipe), usb_pipeout (purb->pipe), !data_toggle);
	transfer_finished:

	/* APC BackUPS Pro kludge */     
	/* It tries to send all of the descriptor instead of */
	/*  the amount we requested */   
	if (desc->hw.td.status & TD_CTRL_IOC &&  
		status & TD_CTRL_ACTIVE &&   
		status & TD_CTRL_NAK )
	{
		dbg("APS WORKAROUND");
		ret=0;
		status=0;
	}

	unlink_qh (s, qh);
	delete_qh (s, qh);

	purb->status = status;
	                                                  	
	dbg("process_transfer: urb %p, wanted len %d, len %d status %x err %d",
		purb,purb->transfer_buffer_length,purb->actual_length, purb->status, purb->error_count);
	//dbg("process_transfer: exit");
#if 0
	if (purb->actual_length){
		char *uu;
		uu=purb->transfer_buffer;
		dbg("%x %x %x %x %x %x %x %x",
			*uu,*(uu+1),*(uu+2),*(uu+3),*(uu+4),*(uu+5),*(uu+6),*(uu+7));
	}
#endif
	return ret;
}

_static int process_interrupt (puhci_t s, purb_t purb)
{
	int i, ret = USB_ST_URB_PENDING;
	purb_priv_t purb_priv = purb->hcpriv;
	struct list_head *p = purb_priv->desc_list.next;
	puhci_desc_t desc = list_entry (purb_priv->desc_list.prev, uhci_desc_t, desc_list);

	int actual_length;
	int status = USB_ST_NOERROR;

	//dbg("urb contains interrupt request");

	for (i = 0; p != &purb_priv->desc_list; p = p->next, i++)	// Maybe we allow more than one TD later ;-)
	{
		desc = list_entry (p, uhci_desc_t, desc_list);

		if (desc->hw.td.status & TD_CTRL_ACTIVE) {
			// do not process active TDs
			//dbg("TD ACT Status @%p %08x",desc,desc->hw.td.status);
			break;
		}

		if (!desc->hw.td.status & TD_CTRL_IOC) {
			// do not process one-shot TDs, no recycling
			break;
		}
		// extract transfer parameters from TD

		actual_length = (desc->hw.td.status + 1) & 0x7ff;
		status = uhci_map_status (uhci_status_bits (desc->hw.td.status), usb_pipeout (purb->pipe));

		// see if EP is stalled
		if (status == -EPIPE) {
			// set up stalled condition
			usb_endpoint_halt (purb->dev, usb_pipeendpoint (purb->pipe), usb_pipeout (purb->pipe));
		}

		// if any error occured: ignore this td, and continue
		if (status != USB_ST_NOERROR) {
			uhci_show_td (desc);
			purb->error_count++;
			goto recycle;
		}
		else
			purb->actual_length = actual_length;

		// FIXME: SPD?

	recycle:
		if (purb->complete) {
			//dbg("process_interrupt: calling completion, status %i",status);
			purb->status = status;
			purb->complete ((struct urb *) purb);
			purb->status = USB_ST_URB_PENDING;
		}

		// Recycle INT-TD if interval!=0, else mark TD as one-shot
		if (purb->interval) {
			desc->hw.td.info &= ~(1 << TD_TOKEN_TOGGLE);
			if (status==0) {
				desc->hw.td.info |= (usb_gettoggle (purb->dev, usb_pipeendpoint (purb->pipe),
				      usb_pipeout (purb->pipe)) << TD_TOKEN_TOGGLE);
				usb_dotoggle (purb->dev, usb_pipeendpoint (purb->pipe), usb_pipeout (purb->pipe));
			} else {
				desc->hw.td.info |= (!usb_gettoggle (purb->dev, usb_pipeendpoint (purb->pipe),
				      usb_pipeout (purb->pipe)) << TD_TOKEN_TOGGLE);
			}
			desc->hw.td.status= (purb->pipe & TD_CTRL_LS) | TD_CTRL_ACTIVE | TD_CTRL_IOC |
				(purb->transfer_flags & USB_DISABLE_SPD ? 0 : TD_CTRL_SPD) | (3 << 27);
			wmb();
		}
		else {
			desc->hw.td.status &= ~TD_CTRL_IOC; // inactivate TD
		}
	}

	return ret;
}


_static int process_iso (puhci_t s, purb_t purb)
{
	int i;
	int ret = USB_ST_NOERROR;
	purb_priv_t purb_priv = purb->hcpriv;
	struct list_head *p = purb_priv->desc_list.next;
	puhci_desc_t desc = list_entry (purb_priv->desc_list.prev, uhci_desc_t, desc_list);

	dbg("urb contains iso request");
	if (desc->hw.td.status & TD_CTRL_ACTIVE)
		return USB_ST_PARTIAL_ERROR;	// last TD not finished

	purb->error_count = 0;
	purb->actual_length = 0;
	purb->status = USB_ST_NOERROR;

	for (i = 0; p != &purb_priv->desc_list; p = p->next, i++) {
		desc = list_entry (p, uhci_desc_t, desc_list);

		//uhci_show_td(desc);
		if (desc->hw.td.status & TD_CTRL_ACTIVE) {
			// means we have completed the last TD, but not the TDs before
			desc->hw.td.status &= ~TD_CTRL_ACTIVE;
			dbg("TD still active (%x)- grrr. paranoia!", desc->hw.td.status);
			ret = USB_ST_PARTIAL_ERROR;
			purb->iso_frame_desc[i].status = ret;
			unlink_td (s, desc);
			// FIXME: immediate deletion may be dangerous
			goto err;
		}

		unlink_td (s, desc);

		if (purb->number_of_packets <= i) {
			dbg("purb->number_of_packets (%d)<=(%d)", purb->number_of_packets, i);
			ret = USB_ST_URB_INVALID_ERROR;
			goto err;
		}

		if (purb->iso_frame_desc[i].offset + purb->transfer_buffer != bus_to_virt (desc->hw.td.buffer)) {
			// Hm, something really weird is going on
			dbg("Pointer Paranoia: %p!=%p", purb->iso_frame_desc[i].offset + purb->transfer_buffer, bus_to_virt (desc->hw.td.buffer));
			ret = USB_ST_URB_INVALID_ERROR;
			purb->iso_frame_desc[i].status = ret;
			goto err;
		}
		purb->iso_frame_desc[i].actual_length = (desc->hw.td.status + 1) & 0x7ff;
		purb->iso_frame_desc[i].status = uhci_map_status (uhci_status_bits (desc->hw.td.status), usb_pipeout (purb->pipe));
		purb->actual_length += purb->iso_frame_desc[i].actual_length;

	      err:

		if (purb->iso_frame_desc[i].status != USB_ST_NOERROR) {
			purb->error_count++;
			purb->status = purb->iso_frame_desc[i].status;
		}
		dbg("process_iso: len:%d status:%x",
		     purb->iso_frame_desc[i].length, purb->iso_frame_desc[i].status);

		delete_desc (desc);
		list_del (p);
	}
	dbg("process_iso: exit %i (%d)", i, ret);
	return ret;
}


_static int process_urb (puhci_t s, struct list_head *p)
{
	int ret = USB_ST_NOERROR;
	purb_t purb;

	spin_lock(&s->urb_list_lock);
	purb=list_entry (p, urb_t, urb_list);
	dbg("found queued urb: %p", purb);

	switch (usb_pipetype (purb->pipe)) {
	case PIPE_CONTROL:
	case PIPE_BULK:
		ret = process_transfer (s, purb);
		break;
	case PIPE_ISOCHRONOUS:
		ret = process_iso (s, purb);
		break;
	case PIPE_INTERRUPT:
		ret = process_interrupt (s, purb);
		break;
	}

	spin_unlock(&s->urb_list_lock);

	if (purb->status != USB_ST_URB_PENDING) {
		int proceed = 0;
		dbg("dequeued urb: %p", purb);
		dequeue_urb (s, p, 1);

#ifdef DEBUG_SLAB
		kmem_cache_free(urb_priv_kmem, purb->hcpriv);
#else
		kfree (purb->hcpriv);
#endif

		if ((usb_pipetype (purb->pipe) != PIPE_INTERRUPT)) {
			purb_t tmp = purb->next;	// pointer to first urb
			int is_ring = 0;
			
			if (purb->next) {
				do {
					if (tmp->status != USB_ST_URB_PENDING) {
						proceed = 1;
						break;
					}
					tmp = tmp->next;
				}
				while (tmp != NULL && tmp != purb->next);
				if (tmp == purb->next)
					is_ring = 1;
			}

			// In case you need the current URB status for your completion handler
			if (purb->complete && (!proceed || (purb->transfer_flags & USB_URB_EARLY_COMPLETE))) {
				dbg("process_transfer: calling early completion");
				purb->complete ((struct urb *) purb);
				if (!proceed && is_ring && (purb->status != USB_ST_URB_KILLED))
					uhci_submit_urb (purb);
			}

			if (proceed && purb->next) {
				// if there are linked urbs - handle submitting of them right now.
				tmp = purb->next;	// pointer to first urb

				do {
					if ((tmp->status != USB_ST_URB_PENDING) && (tmp->status != USB_ST_URB_KILLED) && uhci_submit_urb (tmp) != USB_ST_NOERROR)
						break;
					tmp = tmp->next;
				}
				while (tmp != NULL && tmp != purb->next);	// submit until we reach NULL or our own pointer or submit fails

				if (purb->complete && !(purb->transfer_flags & USB_URB_EARLY_COMPLETE)) {
					dbg("process_transfer: calling completion");
					purb->complete ((struct urb *) purb);
				}
			}
			usb_dec_dev_use (purb->dev);
		}
	}

	return ret;
}

_static void uhci_interrupt (int irq, void *__uhci, struct pt_regs *regs)
{
	puhci_t s = __uhci;
	unsigned int io_addr = s->io_addr;
	unsigned short status;
	struct list_head *p, *p2;

	/*
	 * Read the interrupt status, and write it back to clear the
	 * interrupt cause
	 */

	status = inw (io_addr + USBSTS);

	if (!status)		/* shared interrupt, not mine */
		return;

	dbg("interrupt");

	if (status != 1) {
		warn("interrupt, status %x", status);
		
		// remove host controller halted state
		if ((status&0x20) && (s->running)) {
			// more to be done - check TDs for invalid entries
			// but TDs are only invalid if somewhere else is a (memory ?) problem
			outw (USBCMD_RS | inw(io_addr + USBCMD), io_addr + USBCMD);
		}
		//uhci_show_status (s);
	}
	//beep(1000);           
	/*
	 * the following is very subtle and was blatantly wrong before
	 * traverse the list in *reverse* direction, because new entries
	 * may be added at the end.
	 * also, because process_urb may unlink the current urb,
	 * we need to advance the list before
	 * - Thomas Sailer
	 */

	spin_lock(&s->unlink_urb_lock);
	spin_lock (&s->urb_list_lock);
	p = s->urb_list.prev;
	spin_unlock (&s->urb_list_lock);

	while (p != &s->urb_list) {
		p2 = p;
		p = p->prev;
		process_urb (s, p2);
	}

	spin_unlock(&s->unlink_urb_lock);	

	outw (status, io_addr + USBSTS);
#ifdef __alpha
	mb ();			// ?
#endif
	dbg("done\n\n");
}

_static void reset_hc (puhci_t s)
{
	unsigned int io_addr = s->io_addr;

	s->apm_state = 0;
	/* Global reset for 50ms */
	outw (USBCMD_GRESET, io_addr + USBCMD);
	wait_ms (50);
	outw (0, io_addr + USBCMD);
	wait_ms (10);
}

_static void start_hc (puhci_t s)
{
	unsigned int io_addr = s->io_addr;
	int timeout = 1000;

	/*
	 * Reset the HC - this will force us to get a
	 * new notification of any already connected
	 * ports due to the virtual disconnect that it
	 * implies.
	 */
	outw (USBCMD_HCRESET, io_addr + USBCMD);

	while (inw (io_addr + USBCMD) & USBCMD_HCRESET) {
		if (!--timeout) {
			err("USBCMD_HCRESET timed out!");
			break;
		}
	}

	/* Turn on all interrupts */
	outw (USBINTR_TIMEOUT | USBINTR_RESUME | USBINTR_IOC | USBINTR_SP, io_addr + USBINTR);

	/* Start at frame 0 */
	outw (0, io_addr + USBFRNUM);
	outl (virt_to_bus (s->framelist), io_addr + USBFLBASEADD);

	/* Run and mark it configured with a 64-byte max packet */
	outw (USBCMD_RS | USBCMD_CF | USBCMD_MAXP, io_addr + USBCMD);
	s->apm_state = 1;
	s->running = 1;
}

_static void __exit uhci_cleanup_dev(puhci_t s)
{
	struct usb_device *root_hub = s->bus->root_hub;
	if (root_hub)
		usb_disconnect (&root_hub);

	usb_deregister_bus (s->bus);
	s->running = 0;
	reset_hc (s);
	release_region (s->io_addr, s->io_size);
	free_irq (s->irq, s);
	usb_free_bus (s->bus);
	cleanup_skel (s);
	kfree (s);

}

_static int __init uhci_start_usb (puhci_t s)
{				/* start it up */
	/* connect the virtual root hub */
	struct usb_device *usb_dev;

	usb_dev = usb_alloc_dev (NULL, s->bus);
	if (!usb_dev)
		return -1;

	s->bus->root_hub = usb_dev;
	usb_connect (usb_dev);

	if (usb_new_device (usb_dev) != 0) {
		usb_free_dev (usb_dev);
		return -1;
	}

	return 0;
}

_static int __init alloc_uhci (int irq, unsigned int io_addr, unsigned int io_size)
{
	puhci_t s;
	struct usb_bus *bus;

	s = kmalloc (sizeof (uhci_t), GFP_KERNEL);
	if (!s)
		return -1;

	memset (s, 0, sizeof (uhci_t));
	INIT_LIST_HEAD (&s->urb_list);
	spin_lock_init (&s->urb_list_lock);
	spin_lock_init (&s->qh_lock);
	spin_lock_init (&s->td_lock);
	spin_lock_init (&s->unlink_urb_lock);
	s->irq = -1;
	s->io_addr = io_addr;
	s->io_size = io_size;
	s->next = devs;	//chain new uhci device into global list	

	bus = usb_alloc_bus (&uhci_device_operations);
	if (!bus) {
		kfree (s);
		return -1;
	}

	s->bus = bus;
	bus->hcpriv = s;

	/* UHCI specs says devices must have 2 ports, but goes on to say */
	/* they may have more but give no way to determine how many they */
	/* have, so default to 2 */
	/* According to the UHCI spec, Bit 7 is always set to 1. So we try */
	/* to use this to our advantage */

	for (s->maxports = 0; s->maxports < (io_size - 0x10) / 2; s->maxports++) {
		unsigned int portstatus;

		portstatus = inw (io_addr + 0x10 + (s->maxports * 2));
		dbg("port %i, adr %x status %x", s->maxports,
			io_addr + 0x10 + (s->maxports * 2), portstatus);
		if (!(portstatus & 0x0080))
			break;
	}
	dbg("Detected %d ports", s->maxports);

	/* This is experimental so anything less than 2 or greater than 8 is */
	/*  something weird and we'll ignore it */
	if (s->maxports < 2 || s->maxports > 8) {
		dbg("Port count misdetected, forcing to 2 ports");
		s->maxports = 2;
	}

	s->rh.numports = s->maxports;

	if (init_skel (s)) {
		usb_free_bus (bus);
		kfree(s);
		return -1;
	}

	request_region (s->io_addr, io_size, MODNAME);
	reset_hc (s);
	usb_register_bus (s->bus);

	start_hc (s);

	if (request_irq (irq, uhci_interrupt, SA_SHIRQ, MODNAME, s)) {
		err("request_irq %d failed!",irq);
		usb_free_bus (bus);
		reset_hc (s);
		release_region (s->io_addr, s->io_size);
		cleanup_skel(s);
		kfree(s);
		return -1;
	}

	s->irq = irq;

	if(uhci_start_usb (s) < 0) {
		uhci_cleanup_dev(s);
		return -1;
	}
	
	//chain new uhci device into global list
	devs = s;

	return 0;
}

_static int __init start_uhci (struct pci_dev *dev)
{
	int i;

	/* Search for the IO base address.. */
	for (i = 0; i < 6; i++) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,3,8)
		unsigned int io_addr = dev->resource[i].start;
		unsigned int io_size =
		dev->resource[i].end - dev->resource[i].start + 1;
		if (!(dev->resource[i].flags & 1))
			continue;
#else
		unsigned int io_addr = dev->base_address[i];
		unsigned int io_size = 0x14;
		if (!(io_addr & 1))
			continue;
		io_addr &= ~1;
#endif

		/* Is it already in use? */
		if (check_region (io_addr, io_size))
			break;
		/* disable legacy emulation */
		pci_write_config_word (dev, USBLEGSUP, USBLEGSUP_DEFAULT);

		return alloc_uhci(dev->irq, io_addr, io_size);
	}
	return -1;
}

#ifdef CONFIG_APM
_static int handle_apm_event (apm_event_t event)
{
	static int down = 0;
	puhci_t s = devs;
	dbg("handle_apm_event(%d)", event);
	switch (event) {
	case APM_SYS_SUSPEND:
	case APM_USER_SUSPEND:
		if (down) {
			dbg("received extra suspend event");
			break;
		}
		while (s) {
			reset_hc (s);
			s = s->next;
		}
		down = 1;
		break;
	case APM_NORMAL_RESUME:
	case APM_CRITICAL_RESUME:
		if (!down) {
			dbg("received bogus resume event");
			break;
		}
		down = 0;
		while (s) {
			start_hc (s);
			s = s->next;
		}
		break;
	}
	return 0;
}
#endif

int __init uhci_init (void)
{
	int retval = -ENODEV;
	struct pci_dev *dev = NULL;
	u8 type;
	int i=0;

#ifdef DEBUG_SLAB
	char *slabname=kmalloc(16, GFP_KERNEL);

	if(!slabname)
		return -ENOMEM;

	strcpy(slabname, "uhci_desc");
	uhci_desc_kmem = kmem_cache_create(slabname, sizeof(uhci_desc_t), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	
	if(!uhci_desc_kmem) {
		err("kmem_cache_create for uhci_desc failed (out of memory)");
		return -ENOMEM;
	}

	slabname=kmalloc(16, GFP_KERNEL);

	if(!slabname)
		return -ENOMEM;

	strcpy(slabname, "urb_priv");	
	urb_priv_kmem = kmem_cache_create(slabname, sizeof(urb_priv_t), 0, SLAB_HWCACHE_ALIGN, NULL, NULL);
	
	if(!urb_priv_kmem) {
		err("kmem_cache_create for urb_priv_t failed (out of memory)");
		return -ENOMEM;
	}
#endif	
	info(VERSTR);

	for (;;) {
		dev = pci_find_class (PCI_CLASS_SERIAL_USB << 8, dev);
		if (!dev)
			break;

		/* Is it UHCI */
		pci_read_config_byte (dev, PCI_CLASS_PROG, &type);
		if (type != 0)
			continue;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,3,8)
		pci_enable_device (dev);
#endif
		if(!dev->irq)
		{
			err("Found UHCI device with no IRQ assigned. Check BIOS settings!");
			continue;
		}

		/* Ok set it up */
		retval = start_uhci (dev);
	
		if (!retval)
			i++;

	}

#ifdef CONFIG_APM
	if(i)
		apm_register_callback (&handle_apm_event);
#endif
	return retval;
}

void __exit uhci_cleanup (void)
{
	puhci_t s;
	while ((s = devs)) {
		devs = devs->next;
		uhci_cleanup_dev(s);
	}
#ifdef DEBUG_SLAB
	kmem_cache_shrink(uhci_desc_kmem);
	kmem_cache_shrink(urb_priv_kmem);
#endif
}

#ifdef MODULE
int init_module (void)
{
	return uhci_init ();
}

void cleanup_module (void)
{
#ifdef CONFIG_APM
	apm_unregister_callback (&handle_apm_event);
#endif
	uhci_cleanup ();
}

#endif //MODULE
