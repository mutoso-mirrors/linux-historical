/*
 * usbusy2y.c - ALSA USB US-428 Driver
 *
2004-12-14 Karsten Wiese
	Version 0.8.7.1:
	snd_pcm_open for rawusb pcm-devices now returns -EBUSY if called without rawusb's hwdep device being open.

2004-12-02 Karsten Wiese
	Version 0.8.7:
	Use macro usb_maxpacket() for portability.

2004-10-26 Karsten Wiese
	Version 0.8.6:
	wake_up() process waiting in usX2Y_urbs_start() on error.

2004-10-21 Karsten Wiese
	Version 0.8.5:
	nrpacks is runtime or compiletime configurable now with tested values from 1 to 4.

2004-10-03 Karsten Wiese
	Version 0.8.2:
	Avoid any possible racing while in prepare callback.

2004-09-30 Karsten Wiese
	Version 0.8.0:
	Simplified things and made ohci work again.

2004-09-20 Karsten Wiese
	Version 0.7.3:
	Use usb_kill_urb() instead of deprecated (kernel 2.6.9) usb_unlink_urb().

2004-07-13 Karsten Wiese
	Version 0.7.1:
	Don't sleep in START/STOP callbacks anymore.
	us428 channels C/D not handled just for this version, sorry.

2004-06-21 Karsten Wiese
	Version 0.6.4:
	Temporarely suspend midi input
	to sanely call usb_set_interface() when setting format.

2004-06-12 Karsten Wiese
	Version 0.6.3:
	Made it thus the following rule is enforced:
	"All pcm substreams of one usX2Y have to operate at the same rate & format."

2004-04-06 Karsten Wiese
	Version 0.6.0:
	Runs on 2.6.5 kernel without any "--with-debug=" things.
	us224 reported running.

2004-01-14 Karsten Wiese
	Version 0.5.1:
	Runs with 2.6.1 kernel.

2003-12-30 Karsten Wiese
	Version 0.4.1:
	Fix 24Bit 4Channel capturing for the us428.

2003-11-27 Karsten Wiese, Martin Langer
	Version 0.4:
	us122 support.
	us224 could be tested by uncommenting the sections containing USB_ID_US224

2003-11-03 Karsten Wiese
	Version 0.3:
	24Bit support. 
	"arecord -D hw:1 -c 2 -r 48000 -M -f S24_3LE|aplay -D hw:1 -c 2 -r 48000 -M -f S24_3LE" works.

2003-08-22 Karsten Wiese
	Version 0.0.8:
	Removed EZUSB Firmware. First Stage Firmwaredownload is now done by tascam-firmware downloader.
	See:
	http://usb-midi-fw.sourceforge.net/tascam-firmware.tar.gz

2003-06-18 Karsten Wiese
	Version 0.0.5:
	changed to compile with kernel 2.4.21 and alsa 0.9.4

2002-10-16 Karsten Wiese
	Version 0.0.4:
	compiles again with alsa-current.
	USB_ISO_ASAP not used anymore (most of the time), instead
	urb->start_frame is calculated here now, some calls inside usb-driver don't need to happen anymore.

	To get the best out of this:
	Disable APM-support in the kernel as APM-BIOS calls (once each second) hard disable interrupt for many precious milliseconds.
	This helped me much on my slowish PII 400 & PIII 500.
	ACPI yet untested but might cause the same bad behaviour.
	Use a kernel with lowlatency and preemptiv patches applied.
	To autoload snd-usb-midi append a line 
		post-install snd-usb-us428 modprobe snd-usb-midi
	to /etc/modules.conf.

	known problems:
	sliders, knobs, lights not yet handled except MASTER Volume slider.
       	"pcm -c 2" doesn't work. "pcm -c 2 -m direct_interleaved" does.
	KDE3: "Enable full duplex operation" deadlocks.

	
2002-08-31 Karsten Wiese
	Version 0.0.3: audio also simplex;
	simplifying: iso urbs only 1 packet, melted structs.
	ASYNC_UNLINK not used anymore: no more crashes so far.....
	for alsa 0.9 rc3.

2002-08-09 Karsten Wiese
	Version 0.0.2: midi works with snd-usb-midi, audio (only fullduplex now) with i.e. bristol.
	The firmware has been sniffed from win2k us-428 driver 3.09.

 *   Copyright (c) 2002 - 2004 Karsten Wiese
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
*/

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/usb.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>

#include <sound/rawmidi.h>
#include "usx2y.h"
#include "usbusx2y.h"
#include "usX2Yhwdep.h"



MODULE_AUTHOR("Karsten Wiese <annabellesgarden@yahoo.de>");
MODULE_DESCRIPTION("TASCAM "NAME_ALLCAPS" Version 0.8.7.1");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{TASCAM(0x1604), "NAME_ALLCAPS"(0x8001)(0x8005)(0x8007) }}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX; /* Index 0-max */
static char* id[SNDRV_CARDS] = SNDRV_DEFAULT_STR; /* Id for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP; /* Enable this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for "NAME_ALLCAPS".");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for "NAME_ALLCAPS".");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable "NAME_ALLCAPS".");


static int snd_usX2Y_card_used[SNDRV_CARDS];

static void usX2Y_usb_disconnect(struct usb_device* usb_device, void* ptr);
static void snd_usX2Y_card_private_free(snd_card_t *card);

/* 
 * pipe 4 is used for switching the lamps, setting samplerate, volumes ....   
 */
static void i_usX2Y_Out04Int(struct urb* urb, struct pt_regs *regs)
{
#ifdef CONFIG_SND_DEBUG
	if (urb->status) {
		int 		i;
		usX2Ydev_t*	usX2Y = urb->context;
		for (i = 0; i < 10 && usX2Y->AS04.urb[i] != urb; i++);
		snd_printdd("i_usX2Y_Out04Int() urb %i status=%i\n", i, urb->status);
	}
#endif
}

static void i_usX2Y_In04Int(struct urb* urb, struct pt_regs *regs)
{
	int			err = 0;
	usX2Ydev_t		*usX2Y = urb->context;
	us428ctls_sharedmem_t	*us428ctls = usX2Y->us428ctls_sharedmem;

	usX2Y->In04IntCalls++;

	if (urb->status) {
		snd_printdd("Interrupt Pipe 4 came back with status=%i\n", urb->status);
		return;
	}

	//	printk("%i:0x%02X ", 8, (int)((unsigned char*)usX2Y->In04Buf)[8]); Master volume shows 0 here if fader is at max during boot ?!?
	if (us428ctls) {
		int diff = -1;
		if (-2 == us428ctls->CtlSnapShotLast) {
			diff = 0;
			memcpy(usX2Y->In04Last, usX2Y->In04Buf, sizeof(usX2Y->In04Last));
			us428ctls->CtlSnapShotLast = -1;
		} else {
			int i;
			for (i = 0; i < 21; i++) {
				if (usX2Y->In04Last[i] != ((char*)usX2Y->In04Buf)[i]) {
					if (diff < 0)
						diff = i;
					usX2Y->In04Last[i] = ((char*)usX2Y->In04Buf)[i];
				}
			}
		}
		if (0 <= diff) {
			int n = us428ctls->CtlSnapShotLast + 1;
			if (n >= N_us428_ctl_BUFS  ||  n < 0)
				n = 0;
			memcpy(us428ctls->CtlSnapShot + n, usX2Y->In04Buf, sizeof(us428ctls->CtlSnapShot[0]));
			us428ctls->CtlSnapShotDiffersAt[n] = diff;
			us428ctls->CtlSnapShotLast = n;
			wake_up(&usX2Y->us428ctls_wait_queue_head);
		}
	}
	
	
	if (usX2Y->US04) {
		if (0 == usX2Y->US04->submitted)
			do
				err = usb_submit_urb(usX2Y->US04->urb[usX2Y->US04->submitted++], GFP_ATOMIC);
			while (!err && usX2Y->US04->submitted < usX2Y->US04->len);
	} else
		if (us428ctls && us428ctls->p4outLast >= 0 && us428ctls->p4outLast < N_us428_p4out_BUFS) {
			if (us428ctls->p4outLast != us428ctls->p4outSent) {
				int j, send = us428ctls->p4outSent + 1;
				if (send >= N_us428_p4out_BUFS)
					send = 0;
				for (j = 0; j < URBS_AsyncSeq  &&  !err; ++j)
					if (0 == usX2Y->AS04.urb[j]->status) {
						us428_p4out_t *p4out = us428ctls->p4out + send;	// FIXME if more then 1 p4out is new, 1 gets lost.
						usb_fill_bulk_urb(usX2Y->AS04.urb[j], usX2Y->chip.dev,
								  usb_sndbulkpipe(usX2Y->chip.dev, 0x04), &p4out->val.vol, 
								  p4out->type == eLT_Light ? sizeof(us428_lights_t) : 5,
								  i_usX2Y_Out04Int, usX2Y);
						err = usb_submit_urb(usX2Y->AS04.urb[j], GFP_ATOMIC);
						us428ctls->p4outSent = send;
						break;
					}
			}
		}

	if (err) {
		snd_printk("In04Int() usb_submit_urb err=%i\n", err);
	}

	urb->dev = usX2Y->chip.dev;
	usb_submit_urb(urb, GFP_ATOMIC);
}

/*
 * Prepare some urbs
 */
int usX2Y_AsyncSeq04_init(usX2Ydev_t* usX2Y)
{
	int	err = 0,
		i;

	if (NULL == (usX2Y->AS04.buffer = kmalloc(URB_DataLen_AsyncSeq*URBS_AsyncSeq, GFP_KERNEL))) {
		err = -ENOMEM;
	} else
		for (i = 0; i < URBS_AsyncSeq; ++i) {
			if (NULL == (usX2Y->AS04.urb[i] = usb_alloc_urb(0, GFP_KERNEL))) {
				err = -ENOMEM;
				break;
			}
			usb_fill_bulk_urb(	usX2Y->AS04.urb[i], usX2Y->chip.dev,
						usb_sndbulkpipe(usX2Y->chip.dev, 0x04),
						usX2Y->AS04.buffer + URB_DataLen_AsyncSeq*i, 0,
						i_usX2Y_Out04Int, usX2Y
				);
		}
	return err;
}

int usX2Y_In04_init(usX2Ydev_t* usX2Y)
{
	int	err = 0;
	if (! (usX2Y->In04urb = usb_alloc_urb(0, GFP_KERNEL)))
		return -ENOMEM;

	if (! (usX2Y->In04Buf = kmalloc(21, GFP_KERNEL))) {
		usb_free_urb(usX2Y->In04urb);
		return -ENOMEM;
	}
	 
	init_waitqueue_head(&usX2Y->In04WaitQueue);
	usb_fill_int_urb(usX2Y->In04urb, usX2Y->chip.dev, usb_rcvintpipe(usX2Y->chip.dev, 0x4),
			 usX2Y->In04Buf, 21,
			 i_usX2Y_In04Int, usX2Y,
			 10);
	err = usb_submit_urb(usX2Y->In04urb, GFP_KERNEL);
	return err;
}

static void usX2Y_unlinkSeq(snd_usX2Y_AsyncSeq_t* S)
{
	int	i;
	for (i = 0; i < URBS_AsyncSeq; ++i) {
		if (S[i].urb) {
			usb_kill_urb(S->urb[i]);
			usb_free_urb(S->urb[i]);
			S->urb[i] = NULL;
		}
	}
	kfree(S->buffer);
}


static struct usb_device_id snd_usX2Y_usb_id_table[] = {
	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	0x1604,
		.idProduct =	USB_ID_US428 
	},
	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	0x1604,
		.idProduct =	USB_ID_US122 
	},
 	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	0x1604,
		.idProduct =	USB_ID_US224
	},
	{ /* terminator */ }
};

static snd_card_t* usX2Y_create_card(struct usb_device* device)
{
	int		dev;
	snd_card_t*	card;
	for (dev = 0; dev < SNDRV_CARDS; ++dev)
		if (enable[dev] && !snd_usX2Y_card_used[dev])
			break;
	if (dev >= SNDRV_CARDS)
		return NULL;
	card = snd_card_new(index[dev], id[dev], THIS_MODULE, sizeof(usX2Ydev_t));
	if (!card)
		return NULL;
	snd_usX2Y_card_used[usX2Y(card)->chip.index = dev] = 1;
	card->private_free = snd_usX2Y_card_private_free;
	usX2Y(card)->chip.dev = device;
	usX2Y(card)->chip.card = card;
	init_waitqueue_head(&usX2Y(card)->prepare_wait_queue);
	init_MUTEX (&usX2Y(card)->prepare_mutex);
	INIT_LIST_HEAD(&usX2Y(card)->chip.midi_list);
	strcpy(card->driver, "USB "NAME_ALLCAPS"");
	sprintf(card->shortname, "TASCAM "NAME_ALLCAPS"");
	sprintf(card->longname, "%s (%x:%x if %d at %03d/%03d)",
		card->shortname, 
		le16_to_cpu(device->descriptor.idVendor),
		le16_to_cpu(device->descriptor.idProduct),
		0,//us428(card)->usbmidi.ifnum,
		usX2Y(card)->chip.dev->bus->busnum, usX2Y(card)->chip.dev->devnum
		);
	snd_card_set_dev(card, &device->dev);
	return card;
}


static void* usX2Y_usb_probe(struct usb_device* device, struct usb_interface *intf, const struct usb_device_id* device_id)
{
	int		err;
	snd_card_t*	card;
	if (le16_to_cpu(device->descriptor.idVendor) != 0x1604 ||
	    (le16_to_cpu(device->descriptor.idProduct) != USB_ID_US122 &&
	     le16_to_cpu(device->descriptor.idProduct) != USB_ID_US224 &&
	     le16_to_cpu(device->descriptor.idProduct) != USB_ID_US428) ||
	    !(card = usX2Y_create_card(device)))
		return NULL;
	if ((err = usX2Y_hwdep_new(card, device)) < 0  ||
	    (err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return NULL;
	}
	return card;
}

/*
 * new 2.5 USB kernel API
 */
static int snd_usX2Y_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	void *chip;
	chip = usX2Y_usb_probe(interface_to_usbdev(intf), intf, id);
	if (chip) {
		dev_set_drvdata(&intf->dev, chip);
		return 0;
	} else
		return -EIO;
}

static void snd_usX2Y_disconnect(struct usb_interface *intf)
{
	usX2Y_usb_disconnect(interface_to_usbdev(intf),
				 dev_get_drvdata(&intf->dev));
}

MODULE_DEVICE_TABLE(usb, snd_usX2Y_usb_id_table);
static struct usb_driver snd_usX2Y_usb_driver = {
 	.owner =	THIS_MODULE,
	.name =		"snd-usb-usx2y",
	.probe =	snd_usX2Y_probe,
	.disconnect =	snd_usX2Y_disconnect,
	.id_table =	snd_usX2Y_usb_id_table,
};

static void snd_usX2Y_card_private_free(snd_card_t *card)
{
	kfree(usX2Y(card)->In04Buf);
	usb_free_urb(usX2Y(card)->In04urb);
	if (usX2Y(card)->us428ctls_sharedmem)
		snd_free_pages(usX2Y(card)->us428ctls_sharedmem, sizeof(*usX2Y(card)->us428ctls_sharedmem));
	if (usX2Y(card)->chip.index >= 0  &&  usX2Y(card)->chip.index < SNDRV_CARDS)
		snd_usX2Y_card_used[usX2Y(card)->chip.index] = 0;
}

/*
 * Frees the device.
 */
static void usX2Y_usb_disconnect(struct usb_device* device, void* ptr)
{
	if (ptr) {
		usX2Ydev_t* usX2Y = usX2Y((snd_card_t*)ptr);
		struct list_head* p;
		if (usX2Y->chip_status == USX2Y_STAT_CHIP_HUP)	// on 2.6.1 kernel snd_usbmidi_disconnect()
			return;					// calls us back. better leave :-) .
		usX2Y->chip.shutdown = 1;
		usX2Y->chip_status = USX2Y_STAT_CHIP_HUP;
		usX2Y_unlinkSeq(&usX2Y->AS04);
		usb_kill_urb(usX2Y->In04urb);
		snd_card_disconnect((snd_card_t*)ptr);
		/* release the midi resources */
		list_for_each(p, &usX2Y->chip.midi_list) {
			snd_usbmidi_disconnect(p, &snd_usX2Y_usb_driver);
		}
		if (usX2Y->us428ctls_sharedmem) 
			wake_up(&usX2Y->us428ctls_wait_queue_head);
		snd_card_free_in_thread((snd_card_t*)ptr);
	}
}

static int __init snd_usX2Y_module_init(void)
{
	return usb_register(&snd_usX2Y_usb_driver);
}

static void __exit snd_usX2Y_module_exit(void)
{
	usb_deregister(&snd_usX2Y_usb_driver);
}

module_init(snd_usX2Y_module_init)
module_exit(snd_usX2Y_module_exit)
