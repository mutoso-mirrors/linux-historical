/* Audio driver for the NeoMagic 256AV and 256ZX chipsets in native
   mode, with AC97 mixer support.

   Overall design and parts of this code stolen from vidc_*.c and
   skeleton.c.

   Yeah, there are a lot of magic constants in here.  You tell ME what
   they are.  I just get this stuff psychically, remember? 

   This driver was written by someone who wishes to remain anonymous. 
   It is in the public domain, so share and enjoy.  Try to make a profit
   off of it; go on, I dare you.  */

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/module.h>
#include "sound_config.h"
#include "soundmodule.h"
#include "nm256.h"
#include "nm256_coeff.h"

int nm256_debug = 0;

/* The size of the playback reserve. */
#define NM256_PLAY_WMARK_SIZE 512

static struct audio_driver nm256_audio_driver;

static int nm256_grabInterrupt (struct nm256_info *card);
static int nm256_releaseInterrupt (struct nm256_info *card);
static void nm256_interrupt (int irq, void *dev_id, struct pt_regs *dummy);
static void nm256_interrupt_zx (int irq, void *dev_id, 
				  struct pt_regs *dummy);

/* These belong in linux/pci.h. */
#define PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO 0x8005
#define PCI_DEVICE_ID_NEOMAGIC_NM256ZX_AUDIO 0x8006

/* List of cards.  */
static struct nm256_info *nmcard_list;

/* Locate the card in our list. */
static struct nm256_info *
nm256_find_card (int dev)
{
    struct nm256_info *card;

    for (card = nmcard_list; card != NULL; card = card->next_card)
	if (card->dev[0] == dev || card->dev[1] == dev)
	    return card;

    return NULL;
}

/* Ditto, but find the card struct corresponding to the mixer device DEV 
   instead. */
static struct nm256_info *
nm256_find_card_for_mixer (int dev)
{
    struct nm256_info *card;

    for (card = nmcard_list; card != NULL; card = card->next_card)
	if (card->mixer_oss_dev == dev)
	    return card;

    return NULL;
}

static int usecache = 0;
static int buffertop = 0;

/* Check to see if we're using the bank of cached coefficients. */
int
nm256_cachedCoefficients (struct nm256_info *card)
{
    return usecache;
}

/* The actual rates supported by the card. */
static int samplerates[9] = {
    8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 99999999
};

/* Set the card samplerate, word size and stereo mode to correspond to
   the settings in the CARD struct for the specified device in DEV.
   We keep two separate sets of information, one for each device; the
   hardware is not actually configured until a read or write is
   attempted. */

int
nm256_setInfo (int dev, struct nm256_info *card)
{
    int x;
    int w;
    int targetrate;

    if (card->dev[0] == dev)
	w = 0;
    else if (card->dev[1] == dev)
	w = 1;
    else
	return -ENODEV;

    targetrate = card->sinfo[w].samplerate;

    if ((card->sinfo[w].bits != 8 && card->sinfo[w].bits != 16)
	|| targetrate < samplerates[0]
	|| targetrate > samplerates[7])
	return -EINVAL;

    for (x = 0; x < 8; x++)
	if (targetrate < ((samplerates[x] + samplerates[x + 1]) / 2))
	    break;

    if (x < 8) {
	u8 speedbits = ((x << 4) & NM_RATE_MASK)
	    | (card->sinfo[w].bits == 16 ? NM_RATE_BITS_16: 0) 
	    | (card->sinfo[w].stereo ? NM_RATE_STEREO : 0);

	card->sinfo[w].samplerate = samplerates[x];

	if (card->dev_for_play == dev && card->playing) {
	    nm256_loadCoefficient (card, 0, x);
	    nm256_writePort8 (card, 2,
				NM_PLAYBACK_REG_OFFSET + NM_RATE_REG_OFFSET,
				speedbits);
	}

	if (card->dev_for_record == dev && card->recording) {
	    nm256_loadCoefficient (card, 1, x);
	    nm256_writePort8 (card, 2, 
				NM_RECORD_REG_OFFSET + NM_RATE_REG_OFFSET,
				speedbits);
	}
	return 0;
    }
    else
	return -EINVAL;
}

/* Start the play process going. */
static void
startPlay (struct nm256_info *card)
{
    if (! card->playing) {
	card->playing = 1;
	if (nm256_grabInterrupt (card) == 0) {
	    nm256_setInfo (card->dev_for_play, card);

	    /* Enable playback engine and interrupts. */
	    nm256_writePort8 (card, 2, NM_PLAYBACK_ENABLE_REG,
				NM_PLAYBACK_ENABLE_FLAG | NM_PLAYBACK_FREERUN);

	    /* Enable both channels. */
	    nm256_writePort16 (card, 2, NM_AUDIO_MUTE_REG, 0x0);
	}
    }
}

/* Request one chunk of AMT bytes from the recording device.  When the
   operation is complete, the data will be copied into BUFFER and the
   function DMAbuf_inputintr will be invoked. */

static void
nm256_startRecording (struct nm256_info *card, char *buffer, u32 amt)
{
    u32 endpos;
    int enableEngine = 0;
    u32 ringsize = card->recordBufferSize;

    if (amt > (ringsize / 2)) {
	/* Of course this won't actually work right, because the
	   caller is going to assume we will give what we got asked
	   for. */
	printk (KERN_ERR "NM256: Read request too large: %d\n", amt);
	amt = ringsize / 2;
    }

    if (amt < 8) {
	printk (KERN_ERR "NM256: Read request too small; %d\n", amt);
	return;
    }

    /* If we're not currently recording, set up the start and end registers
       for the recording engine. */
    if (! card->recording) {
	card->recording = 1;
	if (nm256_grabInterrupt (card) == 0) {
	    card->curRecPos = 0;
	    nm256_setInfo (card->dev_for_record, card);
	    nm256_writePort32 (card, 2, NM_RBUFFER_START, card->abuf2);
	    nm256_writePort32 (card, 2, NM_RBUFFER_END,
				 card->abuf2 + ringsize);

	    nm256_writePort32 (card, 2, NM_RBUFFER_CURRP,
				 card->abuf2 + card->curRecPos);
	    enableEngine = 1;
	}
	else {
	    /* Not sure what else to do here.  */
	    return;
	}
    }

    endpos = card->abuf2 + ((card->curRecPos + amt) % ringsize);

    card->recBuf = buffer;
    card->requestedRecAmt = amt;
    nm256_writePort32 (card, 2, NM_RBUFFER_WMARK, endpos);
    /* Enable recording engine and interrupts. */
    if (enableEngine)
	nm256_writePort8 (card, 2, NM_RECORD_ENABLE_REG,
			    NM_RECORD_ENABLE_FLAG | NM_RECORD_FREERUN);
}

/* Stop the play engine. */
static void
stopPlay (struct nm256_info *card)
{
    /* Shut off sound from both channels. */
    nm256_writePort16 (card, 2, NM_AUDIO_MUTE_REG,
			 NM_AUDIO_MUTE_LEFT | NM_AUDIO_MUTE_RIGHT);
    /* Disable play engine. */
    nm256_writePort8 (card, 2, NM_PLAYBACK_ENABLE_REG, 0);
    if (card->playing) {
	nm256_releaseInterrupt (card);

	/* Reset the relevant state bits. */
	card->playing = 0;
	card->curPlayPos = 0;
    }
}

/* Stop recording. */
static void
stopRecord (struct nm256_info *card)
{
    /* Disable recording engine. */
    nm256_writePort8 (card, 2, NM_RECORD_ENABLE_REG, 0);

    if (card->recording) {
	nm256_releaseInterrupt (card);

	card->recording = 0;
	card->curRecPos = 0;
    }
}

/* Ring buffers, man.  That's where the hip-hop, wild-n-wooly action's at.
   1972?

   Write AMT bytes of BUFFER to the playback ring buffer, and start the
   playback engine running.  It will only accept up to 1/2 of the total
   size of the ring buffer.  */

static void
nm256_write_block (struct nm256_info *card, char *buffer, u32 amt)
{
    u32 ringsize = card->playbackBufferSize;
    u32 endstop;

    if (amt > (ringsize / 2)) {
	printk (KERN_ERR "NM256: Write request too large: %d\n", amt);
	amt = (ringsize / 2);
    }

    if (amt < NM256_PLAY_WMARK_SIZE) {
	printk (KERN_ERR "NM256: Write request too small: %d\n", amt);
	return;
    }

    card->curPlayPos %= ringsize;

    card->requested_amt = amt;

    if ((card->curPlayPos + amt) >= ringsize) {
	u32 rem = ringsize - card->curPlayPos;

	nm256_writeBuffer8 (card, buffer, 1,
			      card->abuf1 + card->curPlayPos,
			      rem);
	if (amt > rem)
	    nm256_writeBuffer8 (card, buffer, 1, card->abuf1,
				  amt - rem);
    } 
    else
	nm256_writeBuffer8 (card, buffer, 1,
			      card->abuf1 + card->curPlayPos,
			      amt);

    /* Setup the start-n-stop-n-limit registers, and start that engine
       goin'. 

       Normally we just let it wrap around to avoid the click-click
       action scene. */
    if (! card->playing) {
	/* The PBUFFER_END register in this case points to one "word"
	   before the end of the buffer. */
	int w = (card->dev_for_play == card->dev[0] ? 0 : 1);
	int wordsize = (card->sinfo[w].bits == 16 ? 2 : 1)
	    * (card->sinfo[w].stereo ? 2 : 1);

	/* Need to set the not-normally-changing-registers up. */
	nm256_writePort32 (card, 2, NM_PBUFFER_START,
			     card->abuf1 + card->curPlayPos);
	nm256_writePort32 (card, 2, NM_PBUFFER_END,
			     card->abuf1 + ringsize - wordsize);
	nm256_writePort32 (card, 2, NM_PBUFFER_CURRP,
			     card->abuf1 + card->curPlayPos);
    }
    endstop = (card->curPlayPos + amt - NM256_PLAY_WMARK_SIZE) % ringsize;
    nm256_writePort32 (card, 2, NM_PBUFFER_WMARK, card->abuf1 + endstop);
    if (! card->playing)
	startPlay (card);
}

/* We just got a card playback interrupt; process it. */
static void
nm256_get_new_block (struct nm256_info *card)
{
    /* Check to see how much got played so far. */
    u32 amt = nm256_readPort32 (card, 2, NM_PBUFFER_CURRP) - card->abuf1;

    if (amt >= card->playbackBufferSize) {
	printk (KERN_ERR "NM256: Sound playback pointer invalid!\n");
	amt = 0;
    }

    if (amt < card->curPlayPos)
	amt = (card->playbackBufferSize - card->curPlayPos) + amt;
    else
	amt -= card->curPlayPos;

    if (card->requested_amt > (amt + NM256_PLAY_WMARK_SIZE)) {
	u32 endstop = 
	    card->curPlayPos + card->requested_amt - NM256_PLAY_WMARK_SIZE;
	nm256_writePort32 (card, 2, NM_PBUFFER_WMARK, card->abuf1 + endstop);
    } else {
	card->curPlayPos += card->requested_amt;
	/* Get a new block to write.  This will eventually invoke
	   nm256_write_block ().  */
	DMAbuf_outputintr (card->dev_for_play, 1);
    }
}

/* Ultra cheez-whiz.  But I'm too lazy to grep headers. */
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))

/* Read the last-recorded block from the ring buffer, copy it into the
   saved buffer pointer, and invoke DMAuf_inputintr() with the recording
   device. */

static void
nm256_read_block (struct nm256_info *card)
{
    /* Grab the current position of the recording pointer. */
    u32 currptr = nm256_readPort32 (card, 2, NM_RBUFFER_CURRP) - card->abuf2;
    u32 amtToRead = card->requestedRecAmt;
    u32 ringsize = card->recordBufferSize;

    if (currptr >= card->recordBufferSize) {
	printk (KERN_ERR "NM256: Sound buffer record pointer invalid!\n");
        currptr = 0;
    }

    /* This test is probably redundant; we shouldn't be here unless
       it's true.  */
    if (card->recording) {
	/* If we wrapped around, copy everything from the start of our
	   recording buffer to the end of the buffer. */
	if (currptr < card->curRecPos) {
	    u32 amt = MIN (ringsize - card->curRecPos, amtToRead);

	    nm256_readBuffer8 (card, card->recBuf, 1,
				 card->abuf2 + card->curRecPos,
				 amt);
	    amtToRead -= amt;
	    card->curRecPos += amt;
	    card->recBuf += amt;
	    if (card->curRecPos == ringsize)
		card->curRecPos = 0;
	}

	if ((card->curRecPos < currptr) && (amtToRead > 0)) {
	    u32 amt = MIN (currptr - card->curRecPos, amtToRead);
	    nm256_readBuffer8 (card, card->recBuf, 1,
				 card->abuf2 + card->curRecPos, amt);
	    card->curRecPos = ((card->curRecPos + amt) % ringsize);
	}
	card->recBuf = NULL;
	card->requestedRecAmt = 0;
	DMAbuf_inputintr (card->dev_for_record);
    }
}
#undef MIN

/* Initialize the hardware and various other card data we'll need
   later. */
static void
nm256_initHw (struct nm256_info *card)
{
    int x;

    card->playbackBufferSize = 16384;
    card->recordBufferSize = 16384;

    card->coeffBuf = card->bufend - NM_MAX_COEFFICIENT;
    card->abuf2 = card->coeffBuf - card->recordBufferSize;
    card->abuf1 = card->abuf2 - card->playbackBufferSize;
    card->allCoeffBuf = card->abuf2 - (NM_TOTAL_COEFF_COUNT * 4);

    /* Fixed setting. */
    card->mixer = NM_MIXER_BASE;

    card->playing = 0;
    card->is_open_play = 0;
    card->curPlayPos = 0;

    card->recording = 0;
    card->is_open_record = 0;
    card->curRecPos = 0;

    card->coeffsCurrent = 0;

    card->opencnt[0] = 0; card->opencnt[1] = 0;

    /* Reset everything. */
    nm256_writePort8 (card, 2, 0, 0x11);

    /* Disable recording. */
    nm256_writePort8 (card, 2, NM_RECORD_ENABLE_REG, 0);
    nm256_writePort16 (card, 2, 0x214, 0);

    /* Reasonable default settings, but largely unnecessary. */
    for (x = 0; x < 2; x++) {
	card->sinfo[x].bits = 8;
	card->sinfo[x].stereo = 0;
	card->sinfo[x].samplerate = 8000;
    }
}

/* Handle a potential interrupt for the device referred to by DEV_ID. */

static void
nm256_interrupt (int irq, void *dev_id, struct pt_regs *dummy)
{
    struct nm256_info *card = (struct nm256_info *)dev_id;
    u16 status;
    static int badintrcount = 0;

    if ((card == NULL) || (card->magsig != NM_MAGIC_SIG)) {
	printk (KERN_ERR "NM256: Bad card pointer\n");
	return;
    }

    status = nm256_readPort16 (card, 2, NM_INT_REG);

    /* Not ours. */
    if (status == 0) {
	if (badintrcount++ > 1000) {
	    printk (KERN_ERR "NM256: Releasing interrupt, over 1000 invalid interrupts\n");
	    nm256_releaseInterrupt (card);
	}
	return;
    }

    badintrcount = 0;

    if (status & NM_PLAYBACK_INT) {
	status &= ~NM_PLAYBACK_INT;
	NM_ACK_INT (card, NM_PLAYBACK_INT);

	if (card->playing)
	    nm256_get_new_block (card);
    }

    if (status & NM_RECORD_INT) {
	status &= ~NM_RECORD_INT;
	NM_ACK_INT (card, NM_RECORD_INT);

	if (card->recording)
	    nm256_read_block (card);
    }

    if (status & NM_MISC_INT_1) {
	u8 cbyte;

	status &= ~NM_MISC_INT_1;
	printk (KERN_ERR "NM256: Got misc interrupt #1\n");
	NM_ACK_INT (card, NM_MISC_INT_1);
	nm256_writePort16 (card, 2, NM_INT_REG, 0x8000);
	cbyte = nm256_readPort8 (card, 2, 0x400);
	nm256_writePort8 (card, 2, 0x400, cbyte | 2);
    }

    if (status & NM_MISC_INT_2) {
	u8 cbyte;

	status &= ~NM_MISC_INT_2;
	printk (KERN_ERR "NM256: Got misc interrupt #2\n");
	NM_ACK_INT (card, NM_MISC_INT_2);
	cbyte = nm256_readPort8 (card, 2, 0x400);
	nm256_writePort8 (card, 2, 0x400, cbyte & ~2);
    }

    if (status) {
	printk (KERN_ERR "NM256: Fire in the hole! Unknown status 0x%x\n",
		status);
	/* Pray. */
	NM_ACK_INT (card, status);
    }
}

/* Handle a potential interrupt for the device referred to by DEV_ID.
   This handler is for the 256ZX.  */

static void
nm256_interrupt_zx (int irq, void *dev_id, struct pt_regs *dummy)
{
    struct nm256_info *card = (struct nm256_info *)dev_id;
    u32 status;
    static int badintrcount = 0;

    if ((card == NULL) || (card->magsig != NM_MAGIC_SIG)) {
	printk (KERN_ERR "NM256: Bad card pointer\n");
	return;
    }

    status = nm256_readPort32 (card, 2, NM_INT_REG);

    /* Not ours. */
    if (status == 0) {
	if (badintrcount++ > 1000) {
	    printk (KERN_ERR "NM256: Releasing interrupt, over 1000 invalid interrupts\n");
	    nm256_releaseInterrupt (card);
	}
	return;
    }

    badintrcount = 0;

    if (status & NM2_PLAYBACK_INT) {
	status &= ~NM2_PLAYBACK_INT;
	NM2_ACK_INT (card, NM2_PLAYBACK_INT);

	if (card->playing)
	    nm256_get_new_block (card);
    }

    if (status & NM2_RECORD_INT) {
	status &= ~NM2_RECORD_INT;
	NM2_ACK_INT (card, NM2_RECORD_INT);

	if (card->recording)
	    nm256_read_block (card);
    }

    if (status & NM2_MISC_INT_1) {
	u8 cbyte;

	status &= ~NM2_MISC_INT_1;
	printk (KERN_ERR "NM256: Got misc interrupt #1\n");
	NM2_ACK_INT (card, NM2_MISC_INT_1);
	cbyte = nm256_readPort8 (card, 2, 0x400);
	nm256_writePort8 (card, 2, 0x400, cbyte | 2);
    }

    if (status & NM2_MISC_INT_2) {
	u8 cbyte;

	status &= ~NM2_MISC_INT_2;
	printk (KERN_ERR "NM256: Got misc interrupt #2\n");
	NM2_ACK_INT (card, NM2_MISC_INT_2);
	cbyte = nm256_readPort8 (card, 2, 0x400);
	nm256_writePort8 (card, 2, 0x400, cbyte & ~2);
    }

    if (status) {
	printk (KERN_ERR "NM256: Fire in the hole! Unknown status 0x%x\n",
		status);
	/* Pray. */
	NM2_ACK_INT (card, status);
    }
}

/* Request our interrupt. */
static int
nm256_grabInterrupt (struct nm256_info *card)
{
    if (card->has_irq++ == 0) {
	if (request_irq (card->irq, card->introutine, SA_SHIRQ,
			 "NM256_audio", card) < 0) {
	    printk (KERN_ERR "NM256: can't obtain IRQ %d\n", card->irq);
	    return -1;
	}
    }
    return 0;
}

/* Release our interrupt. */
static int
nm256_releaseInterrupt (struct nm256_info *card)
{
    if (card->has_irq <= 0) {
	printk (KERN_ERR "nm256: too many calls to releaseInterrupt\n");
	return -1;
    }
    card->has_irq--;
    if (card->has_irq == 0) {
	free_irq (card->irq, card);
    }
    return 0;
}

static int
nm256_isReady (struct ac97_hwint *dev)
{
    struct nm256_info *card = (struct nm256_info *)dev->driver_private;
    int t2 = 10;
    u32 testaddr;
    u16 testb;
    int done = 0;

    if (card->magsig != NM_MAGIC_SIG) {
	printk (KERN_ERR "NM256: Bad magic signature in isReady!\n");
	return 0;
    }

    if (card->rev == REV_NM256AV) {
	testaddr = 0xa06;
	testb = 0x0100;
    } else if (card->rev == REV_NM256ZX) {
	testaddr = 0xa08;
	testb = 0x0800;
    } else {
	return -1;
    }

    while (t2-- > 0) {
	if ((nm256_readPort16 (card, 2, testaddr) & testb) == 0) {
	    done = 1;
	    break;
	}
	udelay (100);
    }
    return done;
}

static int
nm256_readAC97Reg (struct ac97_hwint *dev, u8 reg)
{
    struct nm256_info *card = (struct nm256_info *)dev->driver_private;

    if (card->magsig != NM_MAGIC_SIG) {
	printk (KERN_ERR "NM256: Bad magic signature in readAC97Reg!\n");
	return -EINVAL;
    }

    if (reg < 128) {
	int res;

	nm256_isReady (dev);
	res = nm256_readPort16 (card, 2, card->mixer + reg);
        udelay (1000);
	return res;
    }
    else
	return -EINVAL;
}

static int
nm256_writeAC97Reg (struct ac97_hwint *dev, u8 reg, u16 value)
{
    unsigned long flags;
    int tries = 2;
    int done = 0;
    u32 base;

    struct nm256_info *card = (struct nm256_info *)dev->driver_private;

    if (card->magsig != NM_MAGIC_SIG) {
	printk (KERN_ERR "NM256: Bad magic signature in writeAC97Reg!\n");
	return -EINVAL;
    }

    base = card->mixer;

    save_flags (flags);
    cli ();

    nm256_isReady (dev);

    /* Wait for the write to take, too. */
    while ((tries-- > 0) && !done) {
	nm256_writePort16 (card, 2, base + reg, value);
	if (nm256_isReady (dev)) {
	    done = 1;
	    break;
	}

    }

    restore_flags (flags);
    udelay (1000);

    return ! done;
}

struct initialValues 
{
    unsigned short port;
    unsigned short value;
};

static struct initialValues nm256_ac97_initial_values[] = 
{
    { 0x0002, 0x8000 },
    { 0x0004, 0x0000 },
    { 0x0006, 0x0000 },
    { 0x000A, 0x0000 },
    { 0x000C, 0x0008 },
    { 0x000E, 0x8008 },
    { 0x0010, 0x8808 },
    { 0x0012, 0x8808 },
    { 0x0014, 0x8808 },
    { 0x0016, 0x8808 },
    { 0x0018, 0x0808 },
    { 0x001A, 0x0000 },
    { 0x001C, 0x0B0B },
    { 0x0020, 0x0000 },
    { 0xffff, 0xffff }
};

static int
nm256_resetAC97 (struct ac97_hwint *dev)
{
    struct nm256_info *card = (struct nm256_info *)dev->driver_private;
    int x;

    if (card->magsig != NM_MAGIC_SIG) {
	printk (KERN_ERR "NM256: Bad magic signature in resetAC97!\n");
	return -EINVAL;
    }

    /* Reset the card.  'Tis magic!  */
    nm256_writePort8 (card, 2, 0x6c0, 1);
    nm256_writePort8 (card, 2, 0x6cc, 0x87);
    nm256_writePort8 (card, 2, 0x6cc, 0x80);
    nm256_writePort8 (card, 2, 0x6cc, 0x0);

    for (x = 0; nm256_ac97_initial_values[x].port != 0xffff; x++) {
	ac97_put_register (dev,
			   nm256_ac97_initial_values[x].port, 
			   nm256_ac97_initial_values[x].value);
    }

    return 0;
}

/* We don't do anything special here.  */
static int
nm256_default_mixer_ioctl (int dev, unsigned int cmd, caddr_t arg)
{
    struct nm256_info *card = nm256_find_card_for_mixer (dev);
    if (card != NULL)
	return ac97_mixer_ioctl (&(card->mdev), cmd, arg);
    else
	return -ENODEV;
}

static struct mixer_operations nm256_mixer_operations = {
    "NeoMagic",
    "NM256AC97Mixer",
    nm256_default_mixer_ioctl
};

/* I "love" C sometimes.  Got braces?  */
static struct ac97_mixer_value_list mixer_defaults[] = {
    { SOUND_MIXER_VOLUME,  { { 85, 85 } } },
    { SOUND_MIXER_SPEAKER, { { 100 } } },
    { SOUND_MIXER_PCM,     { { 65, 65 } } },
    { SOUND_MIXER_CD,      { { 65, 65 } } },
    { -1,                  {  { 0,  0 } } }
};

static int
nm256_install_mixer (struct nm256_info *card)
{
    int mixer;

    card->mdev.reset_device = nm256_resetAC97;
    card->mdev.read_reg = nm256_readAC97Reg;
    card->mdev.write_reg = nm256_writeAC97Reg;
    card->mdev.driver_private = (void *)card;

    if (ac97_init (&(card->mdev)))
	return -1;

    mixer = sound_alloc_mixerdev();
    if (num_mixers >= MAX_MIXER_DEV) {
	printk ("NM256 mixer: Unable to alloc mixerdev\n");
	return -1;
    }

    mixer_devs[mixer] = &nm256_mixer_operations;
    card->mixer_oss_dev = mixer;

    /* Some reasonable default values.  */
    ac97_set_values (&(card->mdev), mixer_defaults);

    printk(KERN_INFO "Initialized AC97 mixer\n");
    return 0;
}

/* See if the signature left by the NM256 BIOS is intact; if so, we use
   the associated address as the end of our buffer. */
static void
nm256_peek_for_sig (struct nm256_info *card, u32 port1addr)
{
    char *temp = ioremap_nocache (port1addr + card->port1_end - 0x0400, 16);
    u32 sig;

    if (temp == NULL) {
	printk (KERN_ERR "NM256: Unable to scan for card signature in video RAM\n");
	return;
    }
    memcpy_fromio (&sig, temp, sizeof (u32));
    if ((sig & 0xffff0000) == 0x4e4d0000) {
	memcpy_fromio (&(card->bufend), temp + 4, sizeof (u32));
	printk (KERN_INFO "NM256: Found card signature in video RAM: 0x%x\n",
		card->bufend);
    }

    release_region ((unsigned long) temp, 16);
}

/* Install a driver for the soundcard referenced by PCIDEV. */

static int
nm256_install(struct pci_dev *pcidev, enum nm256rev rev, char *verstr)
{
    struct nm256_info *card;
    u32 port1addr = pcidev->resource[0].start;
    u32 port2addr = pcidev->resource[1].start;
    int x;

    card = kmalloc (sizeof (struct nm256_info), GFP_KERNEL);
    if (card == NULL) {
	printk (KERN_ERR "NM256: out of memory!\n");
	return 0;
    }

    card->magsig = NM_MAGIC_SIG;
    card->playing  = 0;
    card->recording = 0;
    card->rev = rev;

    /* The NM256 has two memory ports.  The first port is nothing
       more than a chunk of video RAM, which is used as the I/O ring
       buffer.  The second port has the actual juicy stuff (like the
       mixer and the playback engine control registers). */

    card->ports[1] = ioremap_nocache (port2addr, NM_PORT2_SIZE);

    if (card->ports[1] == NULL) {
	printk (KERN_ERR "NM256: Unable to remap port 2\n");
	kfree_s (card, sizeof (struct nm256_info));
	return 0;
    }

    if (card->rev == REV_NM256AV) {
	card->port1_end = 2560 * 1024;
	card->introutine = nm256_interrupt;
    } 
    else {
	if (nm256_readPort8 (card, 2, 0xa0b) != 0)
	    card->port1_end = 6144 * 1024;
	else
	    card->port1_end = 4096 * 1024;

	card->introutine = nm256_interrupt_zx;
    }

    /* Default value. */
    card->bufend = card->port1_end - 0x1400;

    if (buffertop >= 98304 && buffertop < card->port1_end)
	card->bufend = buffertop;
    else
	nm256_peek_for_sig (card, port1addr);

    card->port1_start = card->bufend - 98304;

    printk (KERN_INFO "NM256: Mapping port 1 from 0x%x - 0x%x\n",
	    card->port1_start, card->port1_end);

    card->ports[0] =
	ioremap_nocache (port1addr + card->port1_start,
			 card->port1_end - card->port1_start);

    if (card->ports[0] == NULL) {
	printk (KERN_ERR "NM256: Unable to remap port 1\n");
	release_region ((unsigned long) card->ports[1], NM_PORT2_SIZE);
	kfree_s (card, sizeof (struct nm256_info));
	return 0;
    }

    /* See if we can get the interrupt. */

    card->irq = pcidev->irq;
    card->has_irq = 0;

    if (nm256_grabInterrupt (card) != 0) {
	release_region ((unsigned long) card->ports[0], 
			card->port1_end - card->port1_start);
	release_region ((unsigned long) card->ports[1], NM_PORT2_SIZE);
	kfree_s (card, sizeof (struct nm256_info));
	return 0;
    }

    nm256_releaseInterrupt (card);

    /*
     *	Init the board.
     */

    nm256_initHw (card);

    for (x = 0; x < 2; x++) {
	if ((card->dev[x] = 
	     sound_install_audiodrv(AUDIO_DRIVER_VERSION,
				    "NM256", &nm256_audio_driver,
				    sizeof(struct audio_driver),
				    DMA_NODMA, AFMT_U8 | AFMT_S16_LE,
				    NULL, -1, -1)) >= 0) {
	    /* 1K minimum buffer size. */
	    audio_devs[card->dev[x]]->min_fragment = 10;
	    /* Maximum of 8K buffer size. */
	    audio_devs[card->dev[x]]->max_fragment = 13;
	}
	else {
	    printk(KERN_ERR "NM256: Too many PCM devices available\n");
	    release_region ((unsigned long) card->ports[0], 
			    card->port1_end - card->port1_start);
	    release_region ((unsigned long) card->ports[1], NM_PORT2_SIZE);
	    kfree_s (card, sizeof (struct nm256_info));
	    return 0;
	}
    }

    /* Insert the card in the list.  */
    card->next_card = nmcard_list;
    nmcard_list = card;

    printk(KERN_INFO "Initialized NeoMagic %s audio in PCI native mode\n",
	   verstr);

    /* 
     * And our mixer.  (We should allow support for other mixers, maybe.)
     */

    nm256_install_mixer (card);

    return 1;
}

/*
 * 	This loop walks the PCI configuration database and finds where
 *	the sound cards are.
 */
 
int
init_nm256(void)
{
    struct pci_dev *pcidev = NULL;
    int count = 0;

    if(! pci_present())
	return -ENODEV;

    while((pcidev = pci_find_device(PCI_VENDOR_ID_NEOMAGIC,
				    PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO,
				    pcidev)) != NULL) {
	count += nm256_install(pcidev, REV_NM256AV, "256AV");
    }

    while((pcidev = pci_find_device(PCI_VENDOR_ID_NEOMAGIC,
				    PCI_DEVICE_ID_NEOMAGIC_NM256ZX_AUDIO,
				    pcidev)) != NULL) {
	count += nm256_install(pcidev, REV_NM256ZX, "256ZX");
    }

    if (count == 0)
	return -ENODEV;

    printk (KERN_INFO "Done installing NM256 audio driver.\n");
    return 0;
}

/*
 * Open the device
 *
 * DEV  - device
 * MODE - mode to open device (logical OR of OPEN_READ and OPEN_WRITE)
 *
 * Called when opening the DMAbuf               (dmabuf.c:259)
 */
static int
nm256_audio_open(int dev, int mode)
{
    struct nm256_info *card = nm256_find_card (dev);
    int w;
	
    if (card == NULL)
	return -ENODEV;

    if (card->dev[0] == dev)
	w = 0;
    else if (card->dev[1] == dev)
	w = 1;
    else
	return -ENODEV;

    if (card->opencnt[w] > 0)
	return -EBUSY;

    /* No bits set? Huh? */
    if (! ((mode & OPEN_READ) || (mode & OPEN_WRITE)))
	return -EIO;

    /* If it's open for both read and write, and the card's currently
       being read or written to, then do the opposite of what has
       already been done.  Otherwise, don't specify any mode until the
       user actually tries to do I/O. */

    if ((mode & OPEN_WRITE) && (mode & OPEN_READ)) {
	if (card->is_open_play)
	    mode = OPEN_WRITE;
	else if (card->is_open_record)
	    mode = OPEN_READ;
	else mode = 0;
    }
	
    if (mode & OPEN_WRITE) {
	if (card->is_open_play == 0) {
	    card->dev_for_play = dev;
	    card->is_open_play = 1;
	}
	else
	    return -EBUSY;
    }

    if (mode & OPEN_READ) {
	if (card->is_open_record == 0) {
	    card->dev_for_record = dev;
	    card->is_open_record = 1;
	}
	else
	    return -EBUSY;
    }

    card->opencnt[w]++;
    return 0;
}

/*
 * Close the device
 *
 * DEV  - device
 *
 * Called when closing the DMAbuf               (dmabuf.c:477)
 *      after halt_xfer
 */
static void
nm256_audio_close(int dev)
{
    struct nm256_info *card = nm256_find_card (dev);
	
    if (card != NULL) {
	int w;

	if (card->dev[0] == dev)
	    w = 0;
	else if (card->dev[1] == dev)
	    w = 1;
	else
	    return;

	card->opencnt[w]--;
	if (card->opencnt[w] <= 0) {
	    card->opencnt[w] = 0;

	    if (card->dev_for_play == dev) {
		stopPlay (card);
		card->is_open_play = 0;
		card->dev_for_play = -1;
	    }

	    if (card->dev_for_record == dev) {
		stopRecord (card);
		card->is_open_record = 0;
		card->dev_for_record = -1;
	    }
	}
    }
}

static int
nm256_audio_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
    int ret;
    u32 oldinfo;
    int w;

    struct nm256_info *card = nm256_find_card (dev);

    if (card == NULL)
	return -ENODEV;

    if (dev == card->dev[0])
	w = 0;
    else
	w = 1;

    switch (cmd)
	{
	case SOUND_PCM_WRITE_RATE:
	    get_user_ret(ret, (int *) arg, -EFAULT);

	    if (ret != 0) {
		oldinfo = card->sinfo[w].samplerate;
		card->sinfo[w].samplerate = ret;
		ret = nm256_setInfo(dev, card);
		if (ret != 0)
		    card->sinfo[w].samplerate = oldinfo;
	    }
	    if (ret == 0)
		ret = card->sinfo[w].samplerate;
	    break;

	case SOUND_PCM_READ_RATE:
	    ret = card->sinfo[w].samplerate;
	    break;

	case SNDCTL_DSP_STEREO:
	    get_user_ret(ret, (int *) arg, -EFAULT);

	    card->sinfo[w].stereo = ret ? 1 : 0;
	    ret = nm256_setInfo (dev, card);
	    if (ret == 0)
		ret = card->sinfo[w].stereo;

	    break;

	case SOUND_PCM_WRITE_CHANNELS:
	    get_user_ret(ret, (int *) arg, -EFAULT);

	    if (ret < 1 || ret > 3)
		ret = card->sinfo[w].stereo + 1;
	    else {
		card->sinfo[w].stereo = ret - 1;
		ret = nm256_setInfo (dev, card);
		if (ret == 0)
		    ret = card->sinfo[w].stereo + 1;
	    }
	    break;

	case SOUND_PCM_READ_CHANNELS:
	    ret = card->sinfo[w].stereo + 1;
	    break;

	case SNDCTL_DSP_SETFMT:
	    get_user_ret(ret, (int *) arg, -EFAULT);

	    if (ret != 0) {
		oldinfo = card->sinfo[w].bits;
		card->sinfo[w].bits = ret;
		ret = nm256_setInfo (dev, card);
		if (ret != 0)
		    card->sinfo[w].bits = oldinfo;
	    }
	    if (ret == 0)
		ret = card->sinfo[w].bits;
	    break;

	case SOUND_PCM_READ_BITS:
	    ret = card->sinfo[w].bits;
	    break;

	default:
	    return -EINVAL;
	}
    return put_user(ret, (int *) arg);
}

/* Given the dev DEV and an associated physical buffer PHYSBUF, return
   a pointer to the actual buffer in kernel space. */

static char *
nm256_getDMAbuffer (int dev, unsigned long physbuf)
{
    struct audio_operations *adev = audio_devs[dev];
    struct dma_buffparms *dmap = adev->dmap_out;
    char *dma_start =
	(char *)(physbuf - (unsigned long)dmap->raw_buf_phys 
		 + (unsigned long)dmap->raw_buf);

    return dma_start;
}


/*
 * Output a block to sound device
 *
 * dev          - device number
 * buf          - physical address of buffer
 * total_count  - total byte count in buffer
 * intrflag     - set if this has been called from an interrupt 
 *				  (via DMAbuf_outputintr)
 * restart_dma  - set if engine needs to be re-initialised
 *
 * Called when:
 *  1. Starting output                                  (dmabuf.c:1327)
 *  2.                                                  (dmabuf.c:1504)
 *  3. A new buffer needs to be sent to the device      (dmabuf.c:1579)
 */
static void
nm256_audio_output_block(int dev, unsigned long physbuf,
				       int total_count, int intrflag)
{
    struct nm256_info *card = nm256_find_card (dev);

    if (card != NULL) {
	char *dma_buf = nm256_getDMAbuffer (dev, physbuf);
	card->is_open_play = 1;
	card->dev_for_play = dev;
	nm256_write_block (card, dma_buf, total_count);
    }
}

static void
nm256_audio_start_input(int dev, unsigned long physbuf, int count,
			  int intrflag)
{
    struct nm256_info *card = nm256_find_card (dev);

    if (card != NULL) {
	char *dma_buf = nm256_getDMAbuffer (dev, physbuf);
	card->is_open_record = 1;
	card->dev_for_record = dev;
	nm256_startRecording (card, dma_buf, count);
    }
}

static int
nm256_audio_prepare_for_input(int dev, int bsize, int bcount)
{
    struct nm256_info *card = nm256_find_card (dev);

    if (card == NULL) 
	return -ENODEV;

    if (card->is_open_record && card->dev_for_record != dev)
	return -EBUSY;

    audio_devs[dev]->dmap_in->flags |= DMA_NODMA;
    return 0;
}

/*
 * Prepare for outputting samples to `dev'
 *
 * Each buffer that will be passed will be `bsize' bytes long,
 * with a total of `bcount' buffers.
 *
 * Called when:
 *  1. A trigger enables audio output                   (dmabuf.c:978)
 *  2. We get a write buffer without dma_mode setup     (dmabuf.c:1152)
 *  3. We restart a transfer                            (dmabuf.c:1324)
 */
static int
nm256_audio_prepare_for_output(int dev, int bsize, int bcount)
{
    struct nm256_info *card = nm256_find_card (dev);

    if (card == NULL)
	return -ENODEV;

    if (card->is_open_play && card->dev_for_play != dev)
	return -EBUSY;

    audio_devs[dev]->dmap_out->flags |= DMA_NODMA;
    return 0;
}

/* Stop the current operations associated with DEV.  */
static void
nm256_audio_reset(int dev)
{
    struct nm256_info *card = nm256_find_card (dev);

    if (card != NULL) {
	if (card->dev_for_play == dev)
	    stopPlay (card);
	if (card->dev_for_record == dev)
	    stopRecord (card);
    }
}

static int
nm256_audio_local_qlen(int dev)
{
    return 0;
}

static struct audio_driver nm256_audio_driver =
{
    nm256_audio_open,			/* open                 */
    nm256_audio_close,			/* close                */
    nm256_audio_output_block,		/* output_block         */
    nm256_audio_start_input,    	/* start_input          */
    nm256_audio_ioctl,			/* ioctl                */
    nm256_audio_prepare_for_input,	/* prepare_for_input    */
    nm256_audio_prepare_for_output,	/* prepare_for_output   */
    nm256_audio_reset,			/* reset                */
    nm256_audio_local_qlen,		/*+local_qlen           */
    NULL,				/*+copy_from_user       */
    NULL,				/*+halt_input           */
    NULL,				/* halt_output          */
    NULL,				/*+trigger              */
    NULL,				/*+set_speed            */
    NULL,				/*+set_bits             */
    NULL,				/*+set_channels         */
};

EXPORT_SYMBOL(init_nm256);

#ifdef MODULE

static int loaded = 0;

MODULE_PARM (usecache, "i");
MODULE_PARM (buffertop, "i");
MODULE_PARM (nm256_debug, "i");

int
init_module (void)
{
    nmcard_list = NULL;
    printk (KERN_INFO "NeoMagic 256AV/256ZX audio driver, version 1.0\n");

    if (init_nm256 () == 0) {
	SOUND_LOCK;
	loaded = 1;
	return 0;
    }
    else
	return -ENODEV;
}

void
cleanup_module (void)
{
    if (loaded) {
	struct nm256_info *card;
	struct nm256_info *next_card;

	SOUND_LOCK_END;

	for (card = nmcard_list; card != NULL; card = next_card) {
	    stopPlay (card);
	    stopRecord (card);
	    if (card->has_irq)
		free_irq (card->irq, card);
	    release_region ((unsigned long) card->ports[0], 
			    card->port1_end - card->port1_start);
	    release_region ((unsigned long) card->ports[1], NM_PORT2_SIZE);
	    sound_unload_mixerdev (card->mixer_oss_dev);
	    sound_unload_audiodev (card->dev[0]);
	    sound_unload_audiodev (card->dev[1]);
	    next_card = card->next_card;
	    kfree_s (card, sizeof (struct nm256_info));
	}
	nmcard_list = NULL;
    }
}
#endif

/*
 * Local variables:
 *  c-basic-offset: 4
 * End:
 */
