/*
 * drivers/sbus/audio/amd7930.c
 *
 * Copyright (C) 1996,1997 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 *
 * This is the lowlevel driver for the AMD7930 audio chip found on all
 * sun4c machines and some sun4m machines.
 *
 * The amd7930 is actually an ISDN chip which has a very simple
 * integrated audio encoder/decoder. When Sun decided on what chip to
 * use for audio, they had the brilliant idea of using the amd7930 and
 * only connecting the audio encoder/decoder pins.
 *
 * Thanks to the AMD engineer who was able to get us the AMD79C30
 * databook which has all the programming information and gain tables.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sbus.h>

#include "audio.h"
#include "amd7930.h"

#define MAX_DRIVERS 1

static struct sparcaudio_driver drivers[MAX_DRIVERS];
static int num_drivers;

/* Each amd7930 chip has two bi-directional B channels and a D
 * channel available to the uproc.  This structure handles all
 * the buffering needed to transmit and receive via a single channel.
 */

#define CHANNEL_AVAILABLE	0x00
#define CHANNEL_INUSE_AUDIO_IN	0x01
#define CHANNEL_INUSE_AUDIO_OUT	0x02
#define CHANNEL_INUSE_ISDN_B1	0x04
#define CHANNEL_INUSE_ISDN_B2	0x08
#define CHANNEL_INUSE		0xff

struct amd7930_channel {
	/* Channel status */
	unsigned char channel_status;

	/* Current buffer that the driver is playing on channel */
	volatile __u8 * output_ptr;
	volatile unsigned long output_count;
	unsigned char xmit_idle_char;

	/* Callback routine (and argument) when output is done on */
	void (*output_callback)();
	void * output_callback_arg;

	/* Current buffer that the driver is recording on channel */
	volatile __u8 * input_ptr;
	volatile unsigned long input_count;
	volatile unsigned long input_limit;

	/* Callback routine (and argument) when input is done on */
	void (*input_callback)();
	void * input_callback_arg;
};

/* Private information we store for each amd7930 chip. */
struct amd7930_info {
	struct amd7930_channel D;
	struct amd7930_channel Bb;
	struct amd7930_channel Bc;

	/* Pointers to which B channels are being used for what
	 * These three fields (Baudio, Bisdn[0], and Bisdn[1]) will either
	 * be NULL or point to one of the Bb/Bc structures above.
	 */
	struct amd7930_channel *Baudio;
	struct amd7930_channel *Bisdn[2];

	/* Device registers information. */
	struct amd7930 *regs;
	unsigned long regs_size;
	struct amd7930_map map;

	/* Volume information. */
	int pgain, rgain, mgain;

	/* Device interrupt information. */
	int irq;
	volatile int ints_on;


	/* Someone to signal when the ISDN LIU state changes */
	int liu_state;
	void (*liu_callback)(void *);
	void *liu_callback_arg;
};



/* Output a 16-bit quantity in the order that the amd7930 expects. */
#define amd7930_out16(regs,v) ({ regs->dr = v & 0xFF; regs->dr = (v >> 8) & 0xFF; })


/*
 * gx, gr & stg gains.  this table must contain 256 elements with
 * the 0th being "infinity" (the magic value 9008).  The remaining
 * elements match sun's gain curve (but with higher resolution):
 * -18 to 0dB in .16dB steps then 0 to 12dB in .08dB steps.
 */
static __const__ __u16 gx_coeff[256] = {
	0x9008, 0x8b7c, 0x8b51, 0x8b45, 0x8b42, 0x8b3b, 0x8b36, 0x8b33,
	0x8b32, 0x8b2a, 0x8b2b, 0x8b2c, 0x8b25, 0x8b23, 0x8b22, 0x8b22,
	0x9122, 0x8b1a, 0x8aa3, 0x8aa3, 0x8b1c, 0x8aa6, 0x912d, 0x912b,
	0x8aab, 0x8b12, 0x8aaa, 0x8ab2, 0x9132, 0x8ab4, 0x913c, 0x8abb,
	0x9142, 0x9144, 0x9151, 0x8ad5, 0x8aeb, 0x8a79, 0x8a5a, 0x8a4a,
	0x8b03, 0x91c2, 0x91bb, 0x8a3f, 0x8a33, 0x91b2, 0x9212, 0x9213,
	0x8a2c, 0x921d, 0x8a23, 0x921a, 0x9222, 0x9223, 0x922d, 0x9231,
	0x9234, 0x9242, 0x925b, 0x92dd, 0x92c1, 0x92b3, 0x92ab, 0x92a4,
	0x92a2, 0x932b, 0x9341, 0x93d3, 0x93b2, 0x93a2, 0x943c, 0x94b2,
	0x953a, 0x9653, 0x9782, 0x9e21, 0x9d23, 0x9cd2, 0x9c23, 0x9baa,
	0x9bde, 0x9b33, 0x9b22, 0x9b1d, 0x9ab2, 0xa142, 0xa1e5, 0x9a3b,
	0xa213, 0xa1a2, 0xa231, 0xa2eb, 0xa313, 0xa334, 0xa421, 0xa54b,
	0xada4, 0xac23, 0xab3b, 0xaaab, 0xaa5c, 0xb1a3, 0xb2ca, 0xb3bd,
	0xbe24, 0xbb2b, 0xba33, 0xc32b, 0xcb5a, 0xd2a2, 0xe31d, 0x0808,
	0x72ba, 0x62c2, 0x5c32, 0x52db, 0x513e, 0x4cce, 0x43b2, 0x4243,
	0x41b4, 0x3b12, 0x3bc3, 0x3df2, 0x34bd, 0x3334, 0x32c2, 0x3224,
	0x31aa, 0x2a7b, 0x2aaa, 0x2b23, 0x2bba, 0x2c42, 0x2e23, 0x25bb,
	0x242b, 0x240f, 0x231a, 0x22bb, 0x2241, 0x2223, 0x221f, 0x1a33,
	0x1a4a, 0x1acd, 0x2132, 0x1b1b, 0x1b2c, 0x1b62, 0x1c12, 0x1c32,
	0x1d1b, 0x1e71, 0x16b1, 0x1522, 0x1434, 0x1412, 0x1352, 0x1323,
	0x1315, 0x12bc, 0x127a, 0x1235, 0x1226, 0x11a2, 0x1216, 0x0a2a,
	0x11bc, 0x11d1, 0x1163, 0x0ac2, 0x0ab2, 0x0aab, 0x0b1b, 0x0b23,
	0x0b33, 0x0c0f, 0x0bb3, 0x0c1b, 0x0c3e, 0x0cb1, 0x0d4c, 0x0ec1,
	0x079a, 0x0614, 0x0521, 0x047c, 0x0422, 0x03b1, 0x03e3, 0x0333,
	0x0322, 0x031c, 0x02aa, 0x02ba, 0x02f2, 0x0242, 0x0232, 0x0227,
	0x0222, 0x021b, 0x01ad, 0x0212, 0x01b2, 0x01bb, 0x01cb, 0x01f6,
	0x0152, 0x013a, 0x0133, 0x0131, 0x012c, 0x0123, 0x0122, 0x00a2,
	0x011b, 0x011e, 0x0114, 0x00b1, 0x00aa, 0x00b3, 0x00bd, 0x00ba,
	0x00c5, 0x00d3, 0x00f3, 0x0062, 0x0051, 0x0042, 0x003b, 0x0033,
	0x0032, 0x002a, 0x002c, 0x0025, 0x0023, 0x0022, 0x001a, 0x0021,
	0x001b, 0x001b, 0x001d, 0x0015, 0x0013, 0x0013, 0x0012, 0x0012,
	0x000a, 0x000a, 0x0011, 0x0011, 0x000b, 0x000b, 0x000c, 0x000e,
};

static __const__ __u16 ger_coeff[] = {
	0x431f, /* 5. dB */
	0x331f, /* 5.5 dB */
	0x40dd, /* 6. dB */
	0x11dd, /* 6.5 dB */
	0x440f, /* 7. dB */
	0x411f, /* 7.5 dB */
	0x311f, /* 8. dB */
	0x5520, /* 8.5 dB */
	0x10dd, /* 9. dB */
	0x4211, /* 9.5 dB */
	0x410f, /* 10. dB */
	0x111f, /* 10.5 dB */
	0x600b, /* 11. dB */
	0x00dd, /* 11.5 dB */
	0x4210, /* 12. dB */
	0x110f, /* 13. dB */
	0x7200, /* 14. dB */
	0x2110, /* 15. dB */
	0x2200, /* 15.9 dB */
	0x000b, /* 16.9 dB */
	0x000f  /* 18. dB */
};
#define NR_GER_COEFFS (sizeof(ger_coeff) / sizeof(ger_coeff[0]))

/* Enable amd7930 interrupts atomically. */
static __inline__ void amd7930_enable_ints(struct amd7930_info *info)
{
	register unsigned long flags;

	if (info->ints_on)
		return;

	save_and_cli(flags);
	info->regs->cr = AMR_INIT;
	info->regs->dr = AM_INIT_ACTIVE;
	restore_flags(flags);

	info->ints_on = 1;
}

/* Disable amd7930 interrupts atomically. */
static __inline__ void amd7930_disable_ints(struct amd7930_info *info)
{
	register unsigned long flags;

	if (!info->ints_on)
		return;

	save_and_cli(flags);
	info->regs->cr = AMR_INIT;
	info->regs->dr = AM_INIT_ACTIVE | AM_INIT_DISABLE_INTS;
	restore_flags(flags);

	info->ints_on = 0;
}  

/* Idle amd7930 (no interrupts, no audio, no data) */
static __inline__ void amd7930_idle(struct amd7930_info *info)
{
	register unsigned long flags;

	if (!info->ints_on)
		return;

	save_and_cli(flags);
	info->regs->cr = AMR_INIT;
	info->regs->dr = 0;
	restore_flags(flags);

	info->ints_on = 0;
}  

/* Commit the local copy of the MAP registers to the amd7930. */
static void amd7930_write_map(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;
	struct amd7930      *regs = info->regs;
	struct amd7930_map  *map  = &info->map;
	unsigned long flags;

	save_and_cli(flags);

	regs->cr = AMR_MAP_GX;
	amd7930_out16(regs, map->gx);

	regs->cr = AMR_MAP_GR;
	amd7930_out16(regs, map->gr);

	regs->cr = AMR_MAP_STGR;
	amd7930_out16(regs, map->stgr);

	regs->cr = AMR_MAP_GER;
	amd7930_out16(regs, map->ger);

	regs->cr = AMR_MAP_MMR1;
	regs->dr = map->mmr1;

	regs->cr = AMR_MAP_MMR2;
	regs->dr = map->mmr2;

	restore_flags(flags);
}

/* Update the MAP registers with new settings. */
static void amd7930_update_map(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;
	struct amd7930_map  *map  = &info->map;
	int level;

	map->gx = gx_coeff[info->rgain];
	map->stgr = gx_coeff[info->mgain];

	level = (info->pgain * (256 + NR_GER_COEFFS)) >> 8;
	if (level >= 256) {
		map->ger = ger_coeff[level - 256];
		map->gr = gx_coeff[255];
	} else {
		map->ger = ger_coeff[0];
		map->gr = gx_coeff[level];
	}

	/* force output to speaker for now */
	map->mmr2 |= AM_MAP_MMR2_LS;

	/* input from external microphone */
	map->mmr2 |= AM_MAP_MMR2_AINB;

	amd7930_write_map(drv);
}

/* Bit of a hack here - if the HISAX ISDN driver has got INTSTAT debugging
 * turned on, we send debugging characters to the ISDN driver:
 *
 *   i# - Interrupt received - number from 0 to 7 is low three bits of IR
 *   >  - Loaded a single char into the Dchan xmit FIFO
 *   +  - Finished loading an xmit packet into the Dchan xmit FIFO
 *   <  - Read a single char from the Dchan recv FIFO
 *   !  - Finished reading a packet from the Dchan recv FIFO
 *
 * This code needs to be removed if anything other than HISAX uses the ISDN
 * driver, since D.output_callback_arg is assumed to be a certain struct ptr
 */

#include "../../isdn/hisax/hisax.h"
#include "../../isdn/hisax/isdnl1.h"

#ifdef L2FRAME_DEBUG

inline void debug_info(struct amd7930_info *info, char c) {
	struct IsdnCardState *cs;

	if (!info || !info->D.output_callback_arg) return;

	cs = (struct IsdnCardState *)info->D.output_callback_arg;

	if (!cs || !cs->status_write) return;

	if (cs->debug & L1_DEB_INTSTAT) {
		*(cs->status_write++) = c;
		if (cs->status_write > cs->status_end)
			cs->status_write = cs->status_buf;
	}
}

#else

#define debug_info(info,c)

#endif


static void fill_D_xmit_fifo(struct amd7930_info *info)
{
	/* Send next byte(s) of outgoing data. */
	while (info->D.output_ptr && info->D.output_count > 0 &&
               (info->regs->dsr2 & AMR_DSR2_TBE)) {

		/* Send the next byte and advance buffer pointer. */
		info->regs->dctb = *(info->D.output_ptr);
		info->D.output_ptr++;
		info->D.output_count--;

		debug_info(info, '>');
        }
}

static void transceive_Dchannel(struct amd7930_info *info)
{
	__u8 dummy;
	int lbrp=0;	/* Last Byte of Received Packet (LBRP) */

#define D_XMIT_ERRORS (AMR_DER_COLLISION | AMR_DER_UNRN)
#define D_RECV_ERRORS (AMR_DER_RABRT | AMR_DER_RFRAME | AMR_DER_FCS | \
			AMR_DER_OVFL | AMR_DER_UNFL | AMR_DER_OVRN)

	/* Transmit if we can */
	fill_D_xmit_fifo(info);

	/* Done with the xmit buffer? Notify the midlevel driver. */
	if (info->D.output_ptr != NULL && info->D.output_count == 0) {
		info->D.output_ptr = NULL;
		info->D.output_count = 0;
		debug_info(info, '+');
		if (info->D.output_callback)
			(*info->D.output_callback)
				(info->D.output_callback_arg,
				 info->regs->der);
				 /* info->regs->der & D_XMIT_ERRORS); */
	}

	/* Read the next byte(s) of incoming data. */

	while (info->regs->dsr2 & AMR_DSR2_RBA) {

		if (info->D.input_ptr &&
		    (info->D.input_count < info->D.input_limit)) {

			/* Get the next byte and advance buffer pointer. */

			*(info->D.input_ptr) = info->regs->dcrb;
			info->D.input_ptr++;
			info->D.input_count++;

		} else {

			/* Overflow - should be detected by chip via RBLR
			 * so we'll just consume data until we see LBRP
			 */

			dummy = info->regs->dcrb;

		}

		debug_info(info, '<');

		if (info->regs->dsr2 & AMR_DSR2_LBRP) {

			/* End of recv packet? Notify the midlevel driver. */

			__u8 der;

			debug_info(info, '!');

			info->D.input_ptr = NULL;

			der = info->regs->der & D_RECV_ERRORS;

			/* Read receive byte count - advances FIFOs */
			info->regs->cr = AMR_DLC_DRCR;
			dummy = info->regs->dr;
			dummy = info->regs->dr;

			if (info->D.input_callback)
				(*info->D.input_callback)
					(info->D.input_callback_arg, der,
					 info->D.input_count);
		}

	}
}

long amd7930_xmit_idles=0;

static void transceive_Bchannel(struct amd7930_channel *channel,
	__volatile__ __u8 *io_reg)
{
	/* Send the next byte of outgoing data. */
	if (channel->output_ptr && channel->output_count > 0) {

		/* Send the next byte and advance buffer pointer. */
		*io_reg = *(channel->output_ptr);
		channel->output_ptr++;
		channel->output_count--;

		/* Done with the buffer? Notify the midlevel driver. */
		if (channel->output_count == 0) {
			channel->output_ptr = NULL;
			channel->output_count = 0;
			if (channel->output_callback)
				(*channel->output_callback)
					(channel->output_callback_arg);
		}
	} else {
		*io_reg = channel->xmit_idle_char;
		amd7930_xmit_idles++;
        }

	/* Read the next byte of incoming data. */
	if (channel->input_ptr && channel->input_count > 0) {

		/* Get the next byte and advance buffer pointer. */
		*(channel->input_ptr) = *io_reg;
		channel->input_ptr++;
		channel->input_count--;

		/* Done with the buffer? Notify the midlevel driver. */
		if (channel->input_count == 0) {
			channel->input_ptr = NULL;
			channel->input_count = 0;
			if (channel->input_callback)
				(*channel->input_callback)
					(channel->input_callback_arg);
		}
	}
}

/* Interrupt handler (The chip takes only one byte per interrupt. Grrr!) */
static void amd7930_interrupt(int irq, void *dev_id, struct pt_regs *intr_regs)
{
	struct sparcaudio_driver *drv = (struct sparcaudio_driver *)dev_id;
	struct amd7930_info *info = (struct amd7930_info *)drv->private;
	struct amd7930 *regs = info->regs;
	__u8 ir;
	__u8 lsr;

	/* Clear the interrupt. */
	ir = regs->ir;

	if (ir & AMR_IR_BBUF) {
		if (info->Bb.channel_status == CHANNEL_INUSE)
			transceive_Bchannel(&info->Bb, &info->regs->bbtb);
		if (info->Bc.channel_status == CHANNEL_INUSE)
			transceive_Bchannel(&info->Bc, &info->regs->bctb);
	}

	if (ir & (AMR_IR_DRTHRSH | AMR_IR_DTTHRSH | AMR_IR_DSRI)) {
		debug_info(info, 'i');
		debug_info(info, '0' + (ir&7));
		transceive_Dchannel(info);
	}

	if (ir & AMR_IR_LSRI) {
		regs->cr = AMR_LIU_LSR;
		lsr = regs->dr;

                info->liu_state = (lsr&0x7) + 2;

                if (info->liu_callback)
			(*info->liu_callback)(info->liu_callback_arg);
        }
}


static int amd7930_open(struct inode * inode, struct file * file,
			struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	/* Set the default audio parameters. */
	info->rgain = 128;
	info->pgain = 200;
	info->mgain = 0;
	amd7930_update_map(drv);

	MOD_INC_USE_COUNT;

	return 0;
}

static void amd7930_release(struct inode * inode, struct file * file,
			    struct sparcaudio_driver *drv)
{
	/* amd7930_disable_ints(drv->private); */
	MOD_DEC_USE_COUNT;
}

static void request_Baudio(struct amd7930_info *info)
{
	if (info->Bb.channel_status == CHANNEL_AVAILABLE) {

		info->Bb.channel_status = CHANNEL_INUSE;
		info->Baudio = &info->Bb;

		/* Multiplexor map - audio (Ba) to Bb */
		info->regs->cr = AMR_MUX_MCR1;
		info->regs->dr = AM_MUX_CHANNEL_Ba | (AM_MUX_CHANNEL_Bb << 4);

		/* Enable B channel interrupts */
		info->regs->cr = AMR_MUX_MCR4;
		info->regs->dr = AM_MUX_MCR4_ENABLE_INTS;

	} else if (info->Bc.channel_status == CHANNEL_AVAILABLE) {

		info->Bc.channel_status = CHANNEL_INUSE;
		info->Baudio = &info->Bc;

		/* Multiplexor map - audio (Ba) to Bc */
		info->regs->cr = AMR_MUX_MCR1;
		info->regs->dr = AM_MUX_CHANNEL_Ba | (AM_MUX_CHANNEL_Bc << 4);

		/* Enable B channel interrupts */
		info->regs->cr = AMR_MUX_MCR4;
		info->regs->dr = AM_MUX_MCR4_ENABLE_INTS;

	}
}

static void release_Baudio(struct amd7930_info *info)
{
	if (info->Baudio) {
		info->Baudio->channel_status = CHANNEL_AVAILABLE;
		info->regs->cr = AMR_MUX_MCR1;
		info->regs->dr = 0;
		info->Baudio = NULL;

		if (info->Bb.channel_status == CHANNEL_AVAILABLE &&
		    info->Bc.channel_status == CHANNEL_AVAILABLE) {

			/* Disable B channel interrupts */
			info->regs->cr = AMR_MUX_MCR4;
			info->regs->dr = 0;
		}
	}
}

static void amd7930_start_output(struct sparcaudio_driver *drv,
				 __u8 * buffer, unsigned long count)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	if (! info->Baudio) {
		request_Baudio(info);
	}

	if (info->Baudio) {
		info->Baudio->output_ptr = buffer;
		info->Baudio->output_count = count;
        	info->Baudio->output_callback = (void *) &sparcaudio_output_done;
	        info->Baudio->output_callback_arg = (void *) drv;
		info->Baudio->xmit_idle_char = 0;
	}
}

static void amd7930_stop_output(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	if (info->Baudio) {
		info->Baudio->output_ptr = NULL;
		info->Baudio->output_count = 0;
		if (! info->Baudio->input_ptr)
			release_Baudio(info);
	}
}

static void amd7930_start_input(struct sparcaudio_driver *drv,
				__u8 * buffer, unsigned long count)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	if (! info->Baudio) {
		request_Baudio(info);
	}

	if (info->Baudio) {
		info->Baudio->input_ptr = buffer;
		info->Baudio->input_count = count;
		info->Baudio->input_callback = (void *) &sparcaudio_input_done;
		info->Baudio->input_callback_arg = (void *) drv;
	}
}

static void amd7930_stop_input(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	if (info->Baudio) {
		info->Baudio->input_ptr = NULL;
		info->Baudio->input_count = 0;
		if (! info->Baudio->output_ptr)
			release_Baudio(info);
	}

}

static void amd7930_sunaudio_getdev(struct sparcaudio_driver *drv,
				 audio_device_t * audinfo)
{
	strncpy(audinfo->name, "amd7930", sizeof(audinfo->name) - 1);
	strncpy(audinfo->version, "x", sizeof(audinfo->version) - 1);
	strncpy(audinfo->config, "audio", sizeof(audinfo->config) - 1);
}

static int amd7930_sunaudio_getdev_sunos(struct sparcaudio_driver *drv)
{
	return AUDIO_DEV_AMD;
}

static int amd7930_set_output_volume(struct sparcaudio_driver *drv, int vol)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	info->pgain = vol;
	amd7930_update_map(drv);
	return 0;
}

static int amd7930_get_output_volume(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	return info->pgain;
}

static int amd7930_set_input_volume(struct sparcaudio_driver *drv, int vol)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	info->rgain = vol;
	amd7930_update_map(drv);
	return 0;
}

static int amd7930_get_input_volume(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	return info->rgain;
}

static int amd7930_set_monitor_volume(struct sparcaudio_driver *drv, int vol)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	info->mgain = vol;
	amd7930_update_map(drv);
	return 0;
}

/* Cheats. The amd has the minimum capabilities we support */
static int amd7930_get_output_balance(struct sparcaudio_driver *drv)
{
	return AUDIO_MID_BALANCE;
}

static int amd7930_get_input_balance(struct sparcaudio_driver *drv)
{
	return AUDIO_MID_BALANCE;
}

static int amd7930_get_output_channels(struct sparcaudio_driver *drv)
{
	return AUDIO_MIN_PLAY_CHANNELS;
}

static int amd7930_get_input_channels(struct sparcaudio_driver *drv)
{
	return AUDIO_MIN_REC_CHANNELS;
}

static int amd7930_get_output_precision(struct sparcaudio_driver *drv)
{
	return AUDIO_MIN_PLAY_PRECISION;
}

static int amd7930_get_input_precision(struct sparcaudio_driver *drv)
{
	return AUDIO_MIN_REC_PRECISION;
}

/* This should eventually be made to DTRT, whatever that ends up */
static int amd7930_get_output_port(struct sparcaudio_driver *drv)
{
	return AUDIO_SPEAKER; /* some of these have only HEADPHONE */
}

/* Only a microphone here, so no troubles */
static int amd7930_get_input_port(struct sparcaudio_driver *drv)
{
	return AUDIO_MICROPHONE;
}

/* This chip also supports AUDIO_ENCODING_ALAW, add support later */
static int amd7930_get_output_encoding(struct sparcaudio_driver *drv)
{
	return AUDIO_ENCODING_ULAW;
}

static int amd7930_get_input_encoding(struct sparcaudio_driver *drv)
{
	return AUDIO_ENCODING_ULAW;
}

/* This is what you get. Take it or leave it */
static int amd7930_get_output_rate(struct sparcaudio_driver *drv)
{
	return AMD7930_RATE;
}

static int amd7930_get_input_rate(struct sparcaudio_driver *drv)
{
	return AMD7930_RATE;
}

static int amd7930_get_output_muted(struct sparcaudio_driver *drv)
{
      return 0;
}

static int amd7930_get_output_ports(struct sparcaudio_driver *drv)
{
      return AUDIO_SPEAKER | AUDIO_HEADPHONE;
}

static int amd7930_get_input_ports(struct sparcaudio_driver *drv)
{
      return AUDIO_MICROPHONE;
}

static int amd7930_get_monitor_volume(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	return info->mgain;
}


/*
 *       ISDN operations
 *
 * Many of these routines take an "int dev" argument, which is simply
 * an index into the drivers[] array.  Currently, we only support a
 * single AMD 7930 chip, so the value should always be 0.  B channel
 * operations require an "int chan", which should be 0 for channel B1
 * and 1 for channel B2
 *
 * int amd7930_get_irqnum(int dev)
 *
 *   returns the interrupt number being used by the chip.  ISDN4linux
 *   uses this number to watch the interrupt during initialization and
 *   make sure something is happening.
 *
 * int amd7930_get_liu_state(int dev)
 *
 *   returns the current state of the ISDN Line Interface Unit (LIU)
 *   as a number between 2 (state F2) and 7 (state F7).  0 may also be
 *   returned if the chip doesn't exist or the LIU hasn't been
 *   activated.  The meanings of the states are defined in I.430, ISDN
 *   BRI Physical Layer Interface.  The most important two states are
 *   F3 (shutdown) and F7 (syncronized).
 *
 * void amd7930_liu_init(int dev, void (*callback)(), void *callback_arg)
 *
 *   initializes the LIU and optionally registers a callback to be
 *   signaled upon a change of LIU state.  The callback will be called
 *   with a single opaque callback_arg Once the callback has been
 *   triggered, amd7930_get_liu_state can be used to determine the LIU
 *   current state.
 *
 * void amd7930_liu_activate(int dev, int priority)
 *
 *   requests LIU activation at a given D-channel priority.
 *   Successful activatation is achieved upon entering state F7, which
 *   will trigger any callback previously registered with
 *   amd7930_liu_init.
 *
 * void amd7930_liu_deactivate(int dev)
 *
 *   deactivates LIU.  Outstanding D and B channel transactions are
 *   terminated rudely and without callback notification.  LIU change
 *   of state callback will be triggered, however.
 *
 * void amd7930_dxmit(int dev, __u8 *buffer, unsigned int count,
 *               void (*callback)(void *, int), void *callback_arg)
 *
 *   transmits a packet - specified with buffer, count - over the D-channel
 *   interface.  Buffer should begin with the LAPD address field and
 *   end with the information field.  FCS and flag sequences should not
 *   be included, nor is bit-stuffing required - all these functions are
 *   performed by the chip.  The callback function will be called
 *   DURING THE TOP HALF OF AN INTERRUPT HANDLER and will be passed
 *   both the arbitrary callback_arg and an integer error indication:
 *
 *       0 - successful transmission; ready for next packet
 *   non-0 - error value from chip's DER (D-Channel Error Register):
 *       4 - collision detect
 *     128 - underrun; irq routine didn't service chip fast enough
 *
 *   The callback routine should defer any time-consuming operations
 *   to a bottom-half handler; however, amd7930_dxmit may be called
 *   from within the callback to request back-to-back transmission of
 *   a second packet (without repeating the priority/collision mechanism)
 *
 *   A comment about the "collision detect" error, which is signalled
 *   whenever the echoed D-channel data didn't match the transmitted
 *   data.  This is part of ISDN's normal multi-drop T-interface
 *   operation, indicating that another device has attempted simultaneous
 *   transmission, but can also result from line noise.  An immediate
 *   requeue via amd7930_dxmit is suggested, but repeated collision
 *   errors may indicate a more serious problem.
 *
 * void amd7930_drecv(int dev, __u8 *buffer, unsigned int size,
 *               void (*callback)(void *, int, unsigned int),
 *               void *callback_arg)
 *
 *   register a buffer - buffer, size - into which a D-channel packet
 *   can be received.  The callback function will be called DURING
 *   THE TOP HALF OF AN INTERRUPT HANDLER and will be passed an
 *   arbitrary callback_arg, an integer error indication and the length
 *   of the received packet, which will start with the address field,
 *   end with the information field, and not contain flag or FCS
 *   bytes.  Bit-stuffing will already have been corrected for.
 *   Possible values of second callback argument "error":
 *
 *       0 - successful reception
 *   non-0 - error value from chip's DER (D-Channel Error Register):
 *       1 - recieved packet abort
 *       2 - framing error; non-integer number of bytes received
 *       8 - FCS error; CRC sequence indicated corrupted data
 *      16 - overflow error; packet exceeded size of buffer
 *      32 - underflow error; packet smaller than required five bytes
 *      64 - overrun error; irq routine didn't service chip fast enough
 *
 * int amd7930_bopen(int dev, int chan, u_char xmit_idle_char)
 *
 *   This function should be called before any other operations on a B
 *   channel.  In addition to arranging for interrupt handling and
 *   channel multiplexing, it sets the xmit_idle_char which is
 *   transmitted on the interface when no data buffer is available.
 *   Suggested values are: 0 for ISDN audio; FF for HDLC mark idle; 7E
 *   for HDLC flag idle.  Returns 0 on a successful open; -1 on error,
 *   which is quite possible if audio and the other ISDN channel are
 *   already in use, since the Am7930 can only send two of the three
 *   channels to the processor
 *
 * void amd7930_bclose(int dev, int chan)
 *
 *   Shuts down a B channel when no longer in use.
 *
 * void amd7930_bxmit(int dev, int chan, __u8 *buffer, unsigned int count,
 *               void (*callback)(void *), void *callback_arg)
 *
 *   transmits a raw data block - specified with buffer, count - over
 *   the B channel interface specified by dev/chan.  The callback
 *   function will be called DURING THE TOP HALF OF AN INTERRUPT
 *   HANDLER and will be passed the arbitrary callback_arg
 *
 *   The callback routine should defer any time-consuming operations
 *   to a bottom-half handler; however, amd7930_bxmit may be called
 *   from within the callback to request back-to-back transmission of
 *   another data block
 *
 * void amd7930_brecv(int dev, int chan, __u8 *buffer, unsigned int size,
 *               void (*callback)(void *), void *callback_arg)
 *
 *   receive a raw data block - specified with buffer, size - over the
 *   B channel interface specified by dev/chan.  The callback function
 *   will be called DURING THE TOP HALF OF AN INTERRUPT HANDLER and
 *   will be passed the arbitrary callback_arg
 *
 *   The callback routine should defer any time-consuming operations
 *   to a bottom-half handler; however, amd7930_brecv may be called
 *   from within the callback to register another buffer and ensure
 *   continuous B channel reception without loss of data
 *
 */


int amd7930_get_irqnum(int dev)
{
	struct amd7930_info *info;

	if (dev > num_drivers) {
		return(0);
	}

	info = (struct amd7930_info *) drivers[dev].private;

	return info->irq;
}

int amd7930_get_liu_state(int dev)
{
	struct amd7930_info *info;

	if (dev > num_drivers) {
		return(0);
	}

	info = (struct amd7930_info *) drivers[dev].private;

	return info->liu_state;
}

void amd7930_liu_init(int dev, void (*callback)(), void *callback_arg)
{
	struct amd7930_info *info;
	register unsigned long flags;

	if (dev > num_drivers) {
		return;
	}

	info = (struct amd7930_info *) drivers[dev].private;

	save_and_cli(flags);

	/* Set callback for LIU state change */
        info->liu_callback = callback;
	info->liu_callback_arg = callback_arg;

	/* De-activate the ISDN Line Interface Unit (LIU) */
	info->regs->cr = AMR_LIU_LMR1;
	info->regs->dr = 0;

	/* Request interrupt when LIU changes state from/to F3/F7/F8 */
	info->regs->cr = AMR_LIU_LMR2;
	info->regs->dr = AM_LIU_LMR2_EN_F3_INT |
          AM_LIU_LMR2_EN_F7_INT | AM_LIU_LMR2_EN_F8_INT;

	/* amd7930_enable_ints(info); */

	/* Activate the ISDN Line Interface Unit (LIU) */
	info->regs->cr = AMR_LIU_LMR1;
	info->regs->dr = AM_LIU_LMR1_LIU_ENABL;

	restore_flags(flags);
}

void amd7930_liu_activate(int dev, int priority)
{
	struct amd7930_info *info;
	register unsigned long flags;

	if (dev > num_drivers) {
		return;
	}

	info = (struct amd7930_info *) drivers[dev].private;

	save_and_cli(flags);

        /* Set D-channel access priority
         *
         * I.430 defines a priority mechanism based on counting 1s
         * in the echo channel before transmitting
         *
         * Priority 0 is eight 1s; priority 1 is ten 1s; etc
         */

        info->regs->cr = AMR_LIU_LPR;
        info->regs->dr = priority & 0x0f;

	/* request LIU activation */

	info->regs->cr = AMR_LIU_LMR1;
	info->regs->dr = AM_LIU_LMR1_LIU_ENABL | AM_LIU_LMR1_REQ_ACTIV;

	restore_flags(flags);
}

void amd7930_liu_deactivate(int dev)
{
	struct amd7930_info *info;
	register unsigned long flags;

	if (dev > num_drivers) {
		return;
	}

	info = (struct amd7930_info *) drivers[dev].private;

	save_and_cli(flags);

	/* deactivate LIU */

	info->regs->cr = AMR_LIU_LMR1;
	info->regs->dr = 0;

	restore_flags(flags);
}

void amd7930_dxmit(int dev, __u8 *buffer, unsigned int count,
		   void (*callback)(void *, int), void *callback_arg)
{
	struct amd7930_info *info;
	register unsigned long flags;
	__u8 dmr1;

	if (dev > num_drivers) {
		return;
	}

	info = (struct amd7930_info *) drivers[dev].private;

	save_and_cli(flags);

	if (info->D.output_ptr) {
		restore_flags(flags);
		printk("amd7930_dxmit: transmitter in use\n");
		return;
	}

	info->D.output_ptr = buffer;
	info->D.output_count = count;
	info->D.output_callback = callback;
	info->D.output_callback_arg = callback_arg;

	/* Enable D-channel Transmit Threshold interrupt; disable addressing */
	info->regs->cr = AMR_DLC_DMR1;
	dmr1 = info->regs->dr;
	dmr1 |= AMR_DLC_DMR1_DTTHRSH_INT;
	dmr1 &= ~AMR_DLC_DMR1_EN_ADDRS;
	info->regs->dr = dmr1;

	/* Begin xmit by setting D-channel Transmit Byte Count Reg (DTCR) */
	info->regs->cr = AMR_DLC_DTCR;
	info->regs->dr = count & 0xff;
	info->regs->dr = (count >> 8) & 0xff;

	/* Prime xmit FIFO */
	/* fill_D_xmit_fifo(info); */
	transceive_Dchannel(info);

	restore_flags(flags);
}

void amd7930_drecv(int dev, __u8 *buffer, unsigned int size,
		   void (*callback)(void *, int, unsigned int),
		   void *callback_arg)
{
	struct amd7930_info *info;
	register unsigned long flags;
	__u8 dmr1;

	if (dev > num_drivers) {
		return;
	}

	info = (struct amd7930_info *) drivers[dev].private;

	save_and_cli(flags);

	if (info->D.input_ptr) {
		restore_flags(flags);
		printk("amd7930_drecv: receiver already has buffer!\n");
		return;
	}

	info->D.input_ptr = buffer;
	info->D.input_count = 0;
	info->D.input_limit = size;
	info->D.input_callback = callback;
	info->D.input_callback_arg = callback_arg;

	/* Enable D-channel Receive Threshold interrupt;
	 * Enable D-channel End of Receive Packet interrupt;
	 * Disable address recognition
	 */
	info->regs->cr = AMR_DLC_DMR1;
	dmr1 = info->regs->dr;
	dmr1 |= AMR_DLC_DMR1_DRTHRSH_INT | AMR_DLC_DMR1_EORP_INT;
	dmr1 &= ~AMR_DLC_DMR1_EN_ADDRS;
	info->regs->dr = dmr1;

	/* Set D-channel Receive Byte Count Limit Register */
	info->regs->cr = AMR_DLC_DRCR;
	info->regs->dr = size & 0xff;
	info->regs->dr = (size >> 8) & 0xff;

	restore_flags(flags);
}

int amd7930_bopen(int dev, int chan, u_char xmit_idle_char)
{
	struct amd7930_info *info;
	register unsigned long flags;

	if (dev > num_drivers || chan<0 || chan>1) {
		return -1;
	}

	info = (struct amd7930_info *) drivers[dev].private;

	save_and_cli(flags);

	if (info->Bb.channel_status == CHANNEL_AVAILABLE) {

		info->Bb.channel_status = CHANNEL_INUSE;
		info->Bb.xmit_idle_char = xmit_idle_char;
		info->Bisdn[chan] = &info->Bb;

		/* Multiplexor map - isdn (B1/2) to Bb */
		info->regs->cr = AMR_MUX_MCR2 + chan;
		info->regs->dr = (AM_MUX_CHANNEL_B1 + chan) |
				 (AM_MUX_CHANNEL_Bb << 4);

	} else if (info->Bc.channel_status == CHANNEL_AVAILABLE) {

		info->Bc.channel_status = CHANNEL_INUSE;
		info->Bc.xmit_idle_char = xmit_idle_char;
		info->Bisdn[chan] = &info->Bc;

		/* Multiplexor map - isdn (B1/2) to Bc */
		info->regs->cr = AMR_MUX_MCR2 + chan;
		info->regs->dr = (AM_MUX_CHANNEL_B1 + chan) |
				 (AM_MUX_CHANNEL_Bc << 4);

	} else {
		restore_flags(flags);
		return (-1);
	}

	/* Enable B channel transmit */
	info->regs->cr = AMR_LIU_LMR1;
	info->regs->dr |= AM_LIU_LMR1_B1_ENABL + chan;

	/* Enable B channel interrupts */
	info->regs->cr = AMR_MUX_MCR4;
	info->regs->dr = AM_MUX_MCR4_ENABLE_INTS | AM_MUX_MCR4_REVERSE_Bb | AM_MUX_MCR4_REVERSE_Bc;

	restore_flags(flags);
	return 0;
}

void amd7930_bclose(int dev, int chan)
{
	struct amd7930_info *info;
	register unsigned long flags;

	if (dev > num_drivers || chan<0 || chan>1) {
		return;
	}

	info = (struct amd7930_info *) drivers[dev].private;

	save_and_cli(flags);

	if (info->Bisdn[chan]) {
		info->Bisdn[chan]->channel_status = CHANNEL_AVAILABLE;
		info->regs->cr = AMR_MUX_MCR2 + chan;
		info->regs->dr = 0;
		info->Bisdn[chan] = NULL;

		/* Disable B channel transmit */
		info->regs->cr = AMR_LIU_LMR1;
		info->regs->dr &= ~(AM_LIU_LMR1_B1_ENABL + chan);

		if (info->Bb.channel_status == CHANNEL_AVAILABLE &&
		    info->Bc.channel_status == CHANNEL_AVAILABLE) {

			/* Disable B channel interrupts */
			info->regs->cr = AMR_MUX_MCR4;
			info->regs->dr = 0;
		}
	}

	restore_flags(flags);
}

void amd7930_bxmit(int dev, int chan, __u8 * buffer, unsigned long count,
		   void (*callback)(void *), void *callback_arg)
{
	struct amd7930_info *info;
	struct amd7930_channel *Bchan;
	register unsigned long flags;

	if (dev > num_drivers) {
		return;
	}

	info = (struct amd7930_info *) drivers[dev].private;
	Bchan = info->Bisdn[chan];

	if (Bchan) {
		save_and_cli(flags);

		Bchan->output_ptr = buffer;
		Bchan->output_count = count;
	        Bchan->output_callback = (void *) callback;
        	Bchan->output_callback_arg = callback_arg;

		restore_flags(flags);
	}
}

void amd7930_brecv(int dev, int chan, __u8 * buffer, unsigned long size,
		   void (*callback)(void *), void *callback_arg)
{
	struct amd7930_info *info;
	struct amd7930_channel *Bchan;
	register unsigned long flags;

	if (dev > num_drivers) {
		return;
	}

	info = (struct amd7930_info *) drivers[dev].private;
	Bchan = info->Bisdn[chan];

	if (Bchan) {
		save_and_cli(flags);

		Bchan->input_ptr = buffer;
		Bchan->input_count = size;
		Bchan->input_callback = (void *) callback;
		Bchan->input_callback_arg = callback_arg;

		restore_flags(flags);
	}
}

EXPORT_SYMBOL(amd7930_get_irqnum);
EXPORT_SYMBOL(amd7930_get_liu_state);
EXPORT_SYMBOL(amd7930_liu_init);
EXPORT_SYMBOL(amd7930_liu_activate);
EXPORT_SYMBOL(amd7930_liu_deactivate);
EXPORT_SYMBOL(amd7930_dxmit);
EXPORT_SYMBOL(amd7930_drecv);
EXPORT_SYMBOL(amd7930_bopen);
EXPORT_SYMBOL(amd7930_bclose);
EXPORT_SYMBOL(amd7930_bxmit);
EXPORT_SYMBOL(amd7930_brecv);


/*
 *	Device detection and initialization.
 */

static struct sparcaudio_operations amd7930_ops = {
	amd7930_open,
	amd7930_release,
	NULL,				/* amd7930_ioctl */
	amd7930_start_output,
	amd7930_stop_output,
	amd7930_start_input,
	amd7930_stop_input,
	amd7930_sunaudio_getdev,
	amd7930_set_output_volume,
	amd7930_get_output_volume,
	amd7930_set_input_volume,
	amd7930_get_input_volume,
	amd7930_set_monitor_volume,
	amd7930_get_monitor_volume,
	NULL,			/* amd7930_set_output_balance */
	amd7930_get_output_balance,
	NULL,			/* amd7930_set_input_balance */
	amd7930_get_input_balance,
	NULL,			/* amd7930_set_output_channels */
	amd7930_get_output_channels,
	NULL,			/* amd7930_set_input_channels */
	amd7930_get_input_channels,
	NULL,			/* amd7930_set_output_precision */
	amd7930_get_output_precision,
	NULL,			/* amd7930_set_input_precision */
	amd7930_get_input_precision,
	NULL,			/* amd7930_set_output_port */
	amd7930_get_output_port,
	NULL,			/* amd7930_set_input_port */
	amd7930_get_input_port,
	NULL,			/* amd7930_set_output_encoding */
	amd7930_get_output_encoding,
	NULL,			/* amd7930_set_input_encoding */
	amd7930_get_input_encoding,
	NULL,			/* amd7930_set_output_rate */
	amd7930_get_output_rate,
	NULL,			/* amd7930_set_input_rate */
	amd7930_get_input_rate,
	amd7930_sunaudio_getdev_sunos,
	amd7930_get_output_ports,
	amd7930_get_input_ports,
	NULL,			/* amd7930_set_output_muted */
	amd7930_get_output_muted,
};

/* Attach to an amd7930 chip given its PROM node. */
static int amd7930_attach(struct sparcaudio_driver *drv, int node,
			  struct linux_sbus *sbus, struct linux_sbus_device *sdev)
{
	struct linux_prom_registers regs;
	struct linux_prom_irqs irq;
	struct amd7930_info *info;
	int err;

	/* Allocate our private information structure. */
	drv->private = kmalloc(sizeof(struct amd7930_info), GFP_KERNEL);
	if (!drv->private)
		return -ENOMEM;

	/* Point at the information structure and initialize it. */
	drv->ops = &amd7930_ops;
	info = (struct amd7930_info *)drv->private;
	info->Bb.output_ptr = info->Bb.input_ptr = NULL;
	info->Bb.output_count = info->Bb.input_count = 0;
	info->Bc.output_ptr = info->Bc.input_ptr = NULL;
	info->Bc.output_count = info->Bc.input_count = 0;
	info->ints_on = 1; /* force disable below */

	/* Map the registers into memory. */
	prom_getproperty(node, "reg", (char *)&regs, sizeof(regs));
	if (sbus && sdev)
		prom_apply_sbus_ranges(sbus, &regs, 1, sdev);
	info->regs_size = regs.reg_size;
	info->regs = sparc_alloc_io(regs.phys_addr, 0, regs.reg_size,
					   "amd7930", regs.which_io, 0);
	if (!info->regs) {
		printk(KERN_ERR "amd7930: could not allocate registers\n");
		kfree(drv->private);
		return -EIO;
	}

	/* Put amd7930 in idle mode (interrupts disabled) */
	amd7930_idle(info);

	/* Enable extended FIFO operation on D-channel */
	info->regs->cr = AMR_DLC_EFCR;
	info->regs->dr = AMR_DLC_EFCR_EXTEND_FIFO;
	info->regs->cr = AMR_DLC_DMR4;
	info->regs->dr = /* AMR_DLC_DMR4_RCV_30 | */ AMR_DLC_DMR4_XMT_14;

	/* Attach the interrupt handler to the audio interrupt. */
	prom_getproperty(node, "intr", (char *)&irq, sizeof(irq));
	info->irq = irq.pri;
	request_irq(info->irq, amd7930_interrupt,
		    SA_INTERRUPT, "amd7930", drv);
	enable_irq(info->irq);
	amd7930_enable_ints(info);

	/* Initalize the local copy of the MAP registers. */
	memset(&info->map, 0, sizeof(info->map));
	info->map.mmr1 = AM_MAP_MMR1_GX | AM_MAP_MMR1_GER
			 | AM_MAP_MMR1_GR | AM_MAP_MMR1_STG;

	/* Register the amd7930 with the midlevel audio driver. */
	err = register_sparcaudio_driver(drv);
	if (err < 0) {
		printk(KERN_ERR "amd7930: unable to register\n");
		disable_irq(info->irq);
		free_irq(info->irq, drv);
		sparc_free_io(info->regs, info->regs_size);
		kfree(drv->private);
		return -EIO;
	}

	/* Announce the hardware to the user. */
	printk(KERN_INFO "amd7930 at 0x%lx irq %d\n",
		(unsigned long)info->regs, info->irq);

	/* Success! */
	return 0;
}

#ifdef MODULE
/* Detach from an amd7930 chip given the device structure. */
static void amd7930_detach(struct sparcaudio_driver *drv)
{
	struct amd7930_info *info = (struct amd7930_info *)drv->private;

	unregister_sparcaudio_driver(drv);
	amd7930_idle(info);
	disable_irq(info->irq);
	free_irq(info->irq, drv);
	sparc_free_io(info->regs, info->regs_size);
	kfree(drv->private);
}
#endif

/* Probe for the amd7930 chip and then attach the driver. */
#ifdef MODULE
int init_module(void)
#else
__initfunc(int amd7930_init(void))
#endif
{
	struct linux_sbus *bus;
	struct linux_sbus_device *sdev;
	int node;

	/* Try to find the sun4c "audio" node first. */
	node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(node, "audio");
	if (node && amd7930_attach(&drivers[0], node, NULL, NULL) == 0)
		num_drivers = 1;
	else
		num_drivers = 0;

	/* Probe each SBUS for amd7930 chips. */
	for_all_sbusdev(sdev,bus) {
		if (!strcmp(sdev->prom_name, "audio")) {
			/* Don't go over the max number of drivers. */
			if (num_drivers >= MAX_DRIVERS)
				continue;

			if (amd7930_attach(&drivers[num_drivers],
					   sdev->prom_node, sdev->my_bus, sdev) == 0)
				num_drivers++;
		}
	}

	/* Only return success if we found some amd7930 chips. */
	return (num_drivers > 0) ? 0 : -EIO;
}

#ifdef MODULE
void cleanup_module(void)
{
	register int i;

	for (i = 0; i < num_drivers; i++) {
		amd7930_detach(&drivers[i]);
		num_drivers--;
	}
}
#endif
