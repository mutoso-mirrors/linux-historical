/*
 * sound/gus_wave.c
 *
 * Driver for the Gravis UltraSound wave table synth.
 *
 * Copyright by Hannu Savolainen 1993, 1994
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "sound_config.h"
#include <linux/ultrasound.h>
#include "gus_hw.h"

#if defined(CONFIGURE_SOUNDCARD) && !defined(EXCLUDE_GUS)

#define MAX_SAMPLE	150
#define MAX_PATCH	256

struct voice_info
  {
    unsigned long   orig_freq;
    unsigned long   current_freq;
    unsigned long   mode;
    int             bender;
    int             bender_range;
    int             panning;
    int             midi_volume;
    unsigned int    initial_volume;
    unsigned int    current_volume;
    int             loop_irq_mode, loop_irq_parm;
#define LMODE_FINISH		1
#define LMODE_PCM		2
#define LMODE_PCM_STOP		3
    int             volume_irq_mode, volume_irq_parm;
#define VMODE_HALT		1
#define VMODE_ENVELOPE		2
#define VMODE_START_NOTE	3

    int             env_phase;
    unsigned char   env_rate[6];
    unsigned char   env_offset[6];

    /*
     * Volume computation parameters for gus_adagio_vol()
     */
    int             main_vol, expression_vol, patch_vol;

    /* Variables for "Ultraclick" removal */
    int             dev_pending, note_pending, volume_pending, sample_pending;
    char            kill_pending;
    long            offset_pending;

  };

static struct voice_alloc_info *voice_alloc;

extern int      gus_base;
extern int      gus_irq, gus_dma;
static int      gus_dma2 = -1;
static int      dual_dma_mode = 0;
static long     gus_mem_size = 0;
static long     free_mem_ptr = 0;
static int      gus_busy = 0;
static int      gus_no_dma = 0;
static int      nr_voices = 0;
static int      gus_devnum = 0;
static int      volume_base, volume_scale, volume_method;
static int      gus_recmask = SOUND_MASK_MIC;
static int      recording_active = 0;
static int      only_read_access = 0;
static int      only_8_bits = 0;

int             gus_wave_volume = 60;
int             gus_pcm_volume = 80;
int             have_gus_max = 0;
static int      gus_line_vol = 100, gus_mic_vol = 0;
static unsigned char mix_image = 0x00;

int             gus_timer_enabled = 0;

/*
 * Current version of this driver doesn't allow synth and PCM functions
 * at the same time. The active_device specifies the active driver
 */
static int      active_device = 0;

#define GUS_DEV_WAVE		1	/* Wave table synth */
#define GUS_DEV_PCM_DONE	2	/* PCM device, transfer done */
#define GUS_DEV_PCM_CONTINUE	3	/* PCM device, transfer done ch. 1/2 */

static int      gus_sampling_speed;
static int      gus_sampling_channels;
static int      gus_sampling_bits;

static struct wait_queue *dram_sleeper = NULL;
static volatile struct snd_wait dram_sleep_flag =
{0};

/*
 * Variables and buffers for PCM output
 */
#define MAX_PCM_BUFFERS		(32*MAX_REALTIME_FACTOR)	/* Don't change */

static int      pcm_bsize, pcm_nblk, pcm_banksize;
static int      pcm_datasize[MAX_PCM_BUFFERS];
static volatile int pcm_head, pcm_tail, pcm_qlen;
static volatile int pcm_active;
static volatile int dma_active;
static int      pcm_opened = 0;
static int      pcm_current_dev;
static int      pcm_current_block;
static unsigned long pcm_current_buf;
static int      pcm_current_count;
static int      pcm_current_intrflag;

extern sound_os_info *gus_osp;

struct voice_info voices[32];

static int      freq_div_table[] =
{
  44100,			/* 14 */
  41160,			/* 15 */
  38587,			/* 16 */
  36317,			/* 17 */
  34300,			/* 18 */
  32494,			/* 19 */
  30870,			/* 20 */
  29400,			/* 21 */
  28063,			/* 22 */
  26843,			/* 23 */
  25725,			/* 24 */
  24696,			/* 25 */
  23746,			/* 26 */
  22866,			/* 27 */
  22050,			/* 28 */
  21289,			/* 29 */
  20580,			/* 30 */
  19916,			/* 31 */
  19293				/* 32 */
};

static struct patch_info *samples;
static long     sample_ptrs[MAX_SAMPLE + 1];
static int      sample_map[32];
static int      free_sample;
static int      mixer_type = 0;


static int      patch_table[MAX_PATCH];
static int      patch_map[32];

static struct synth_info gus_info =
{"Gravis UltraSound", 0, SYNTH_TYPE_SAMPLE, SAMPLE_TYPE_GUS, 0, 16, 0, MAX_PATCH};

static void     gus_poke (long addr, unsigned char data);
static void     compute_and_set_volume (int voice, int volume, int ramp_time);
extern unsigned short gus_adagio_vol (int vel, int mainv, int xpn, int voicev);
extern unsigned short gus_linear_vol (int vol, int mainvol);
static void     compute_volume (int voice, int volume);
static void     do_volume_irq (int voice);
static void     set_input_volumes (void);
static void     gus_tmr_install (int io_base);

#define	INSTANT_RAMP		-1	/* Instant change. No ramping */
#define FAST_RAMP		0	/* Fastest possible ramp */

static void
reset_sample_memory (void)
{
  int             i;

  for (i = 0; i <= MAX_SAMPLE; i++)
    sample_ptrs[i] = -1;
  for (i = 0; i < 32; i++)
    sample_map[i] = -1;
  for (i = 0; i < 32; i++)
    patch_map[i] = -1;

  gus_poke (0, 0);		/* Put a silent sample to the beginning */
  gus_poke (1, 0);
  free_mem_ptr = 2;

  free_sample = 0;

  for (i = 0; i < MAX_PATCH; i++)
    patch_table[i] = -1;
}

void
gus_delay (void)
{
  int             i;

  for (i = 0; i < 7; i++)
    inb (u_DRAMIO);
}

static void
gus_poke (long addr, unsigned char data)
{				/* Writes a byte to the DRAM */
  unsigned long   flags;

  save_flags (flags);
  cli ();
  outb (0x43, u_Command);
  outb (addr & 0xff, u_DataLo);
  outb ((addr >> 8) & 0xff, u_DataHi);

  outb (0x44, u_Command);
  outb ((addr >> 16) & 0xff, u_DataHi);
  outb (data, u_DRAMIO);
  restore_flags (flags);
}

static unsigned char
gus_peek (long addr)
{				/* Reads a byte from the DRAM */
  unsigned long   flags;
  unsigned char   tmp;

  save_flags (flags);
  cli ();
  outb (0x43, u_Command);
  outb (addr & 0xff, u_DataLo);
  outb ((addr >> 8) & 0xff, u_DataHi);

  outb (0x44, u_Command);
  outb ((addr >> 16) & 0xff, u_DataHi);
  tmp = inb (u_DRAMIO);
  restore_flags (flags);

  return tmp;
}

void
gus_write8 (int reg, unsigned int data)
{				/* Writes to an indirect register (8 bit) */
  unsigned long   flags;

  save_flags (flags);
  cli ();

  outb (reg, u_Command);
  outb ((unsigned char) (data & 0xff), u_DataHi);

  restore_flags (flags);
}

unsigned char
gus_read8 (int reg)
{				/* Reads from an indirect register (8 bit). Offset 0x80. */
  unsigned long   flags;
  unsigned char   val;

  save_flags (flags);
  cli ();
  outb (reg | 0x80, u_Command);
  val = inb (u_DataHi);
  restore_flags (flags);

  return val;
}

unsigned char
gus_look8 (int reg)
{				/* Reads from an indirect register (8 bit). No additional offset. */
  unsigned long   flags;
  unsigned char   val;

  save_flags (flags);
  cli ();
  outb (reg, u_Command);
  val = inb (u_DataHi);
  restore_flags (flags);

  return val;
}

void
gus_write16 (int reg, unsigned int data)
{				/* Writes to an indirect register (16 bit) */
  unsigned long   flags;

  save_flags (flags);
  cli ();

  outb (reg, u_Command);

  outb ((unsigned char) (data & 0xff), u_DataLo);
  outb ((unsigned char) ((data >> 8) & 0xff), u_DataHi);

  restore_flags (flags);
}

unsigned short
gus_read16 (int reg)
{				/* Reads from an indirect register (16 bit). Offset 0x80. */
  unsigned long   flags;
  unsigned char   hi, lo;

  save_flags (flags);
  cli ();

  outb (reg | 0x80, u_Command);

  lo = inb (u_DataLo);
  hi = inb (u_DataHi);

  restore_flags (flags);

  return ((hi << 8) & 0xff00) | lo;
}

void
gus_write_addr (int reg, unsigned long address, int is16bit)
{				/* Writes an 24 bit memory address */
  unsigned long   hold_address;
  unsigned long   flags;

  save_flags (flags);
  cli ();
  if (is16bit)
    {
      /*
       * Special processing required for 16 bit patches
       */

      hold_address = address;
      address = address >> 1;
      address &= 0x0001ffffL;
      address |= (hold_address & 0x000c0000L);
    }

  gus_write16 (reg, (unsigned short) ((address >> 7) & 0xffff));
  gus_write16 (reg + 1, (unsigned short) ((address << 9) & 0xffff));
  /* Could writing twice fix problems with GUS_VOICE_POS() ? Lets try... */
  gus_delay ();
  gus_write16 (reg, (unsigned short) ((address >> 7) & 0xffff));
  gus_write16 (reg + 1, (unsigned short) ((address << 9) & 0xffff));
  restore_flags (flags);
}

static void
gus_select_voice (int voice)
{
  if (voice < 0 || voice > 31)
    return;

  outb (voice, u_Voice);
}

static void
gus_select_max_voices (int nvoices)
{
  if (nvoices < 14)
    nvoices = 14;
  if (nvoices > 32)
    nvoices = 32;

  voice_alloc->max_voice = nr_voices = nvoices;

  gus_write8 (0x0e, (nvoices - 1) | 0xc0);
}

static void
gus_voice_on (unsigned int mode)
{
  gus_write8 (0x00, (unsigned char) (mode & 0xfc));
  gus_delay ();
  gus_write8 (0x00, (unsigned char) (mode & 0xfc));
}

static void
gus_voice_off (void)
{
  gus_write8 (0x00, gus_read8 (0x00) | 0x03);
}

static void
gus_voice_mode (unsigned int m)
{
  unsigned char   mode = (unsigned char) (m & 0xff);

  gus_write8 (0x00, (gus_read8 (0x00) & 0x03) |
	      (mode & 0xfc));	/* Don't touch last two bits */
  gus_delay ();
  gus_write8 (0x00, (gus_read8 (0x00) & 0x03) | (mode & 0xfc));
}

static void
gus_voice_freq (unsigned long freq)
{
  unsigned long   divisor = freq_div_table[nr_voices - 14];
  unsigned short  fc;

  fc = (unsigned short) (((freq << 9) + (divisor >> 1)) / divisor);
  fc = fc << 1;

  gus_write16 (0x01, fc);
}

static void
gus_voice_volume (unsigned int vol)
{
  gus_write8 (0x0d, 0x03);	/* Stop ramp before setting volume */
  gus_write16 (0x09, (unsigned short) (vol << 4));
}

static void
gus_voice_balance (unsigned int balance)
{
  gus_write8 (0x0c, (unsigned char) (balance & 0xff));
}

static void
gus_ramp_range (unsigned int low, unsigned int high)
{
  gus_write8 (0x07, (unsigned char) ((low >> 4) & 0xff));
  gus_write8 (0x08, (unsigned char) ((high >> 4) & 0xff));
}

static void
gus_ramp_rate (unsigned int scale, unsigned int rate)
{
  gus_write8 (0x06, (unsigned char) (((scale & 0x03) << 6) | (rate & 0x3f)));
}

static void
gus_rampon (unsigned int m)
{
  unsigned char   mode = (unsigned char) (m & 0xff);

  gus_write8 (0x0d, mode & 0xfc);
  gus_delay ();
  gus_write8 (0x0d, mode & 0xfc);
}

static void
gus_ramp_mode (unsigned int m)
{
  unsigned char   mode = (unsigned char) (m & 0xff);

  gus_write8 (0x0d, (gus_read8 (0x0d) & 0x03) |
	      (mode & 0xfc));	/* Leave the last 2 bits alone */
  gus_delay ();
  gus_write8 (0x0d, (gus_read8 (0x0d) & 0x03) | (mode & 0xfc));
}

static void
gus_rampoff (void)
{
  gus_write8 (0x0d, 0x03);
}

static void
gus_set_voice_pos (int voice, long position)
{
  int             sample_no;

  if ((sample_no = sample_map[voice]) != -1)
    if (position < samples[sample_no].len)
      if (voices[voice].volume_irq_mode == VMODE_START_NOTE)
	voices[voice].offset_pending = position;
      else
	gus_write_addr (0x0a, sample_ptrs[sample_no] + position,
			samples[sample_no].mode & WAVE_16_BITS);
}

static void
gus_voice_init (int voice)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();
  gus_select_voice (voice);
  gus_voice_volume (0);
  gus_voice_off ();
  gus_write_addr (0x0a, 0, 0);	/* Set current position to 0 */
  gus_write8 (0x00, 0x03);	/* Voice off */
  gus_write8 (0x0d, 0x03);	/* Ramping off */
  voice_alloc->map[voice] = 0;
  voice_alloc->alloc_times[voice] = 0;
  restore_flags (flags);

}

static void
gus_voice_init2 (int voice)
{
  voices[voice].panning = 0;
  voices[voice].mode = 0;
  voices[voice].orig_freq = 20000;
  voices[voice].current_freq = 20000;
  voices[voice].bender = 0;
  voices[voice].bender_range = 200;
  voices[voice].initial_volume = 0;
  voices[voice].current_volume = 0;
  voices[voice].loop_irq_mode = 0;
  voices[voice].loop_irq_parm = 0;
  voices[voice].volume_irq_mode = 0;
  voices[voice].volume_irq_parm = 0;
  voices[voice].env_phase = 0;
  voices[voice].main_vol = 127;
  voices[voice].patch_vol = 127;
  voices[voice].expression_vol = 127;
  voices[voice].sample_pending = -1;
}

static void
step_envelope (int voice)
{
  unsigned        vol, prev_vol, phase;
  unsigned char   rate;
  long int        flags;

  if (voices[voice].mode & WAVE_SUSTAIN_ON && voices[voice].env_phase == 2)
    {
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      gus_rampoff ();
      restore_flags (flags);
      return;
      /*
       * Sustain phase begins. Continue envelope after receiving note off.
       */
    }

  if (voices[voice].env_phase >= 5)
    {				/* Envelope finished. Shoot the voice down */
      gus_voice_init (voice);
      return;
    }

  prev_vol = voices[voice].current_volume;
  phase = ++voices[voice].env_phase;
  compute_volume (voice, voices[voice].midi_volume);
  vol = voices[voice].initial_volume * voices[voice].env_offset[phase] / 255;
  rate = voices[voice].env_rate[phase];

  save_flags (flags);
  cli ();
  gus_select_voice (voice);

  gus_voice_volume (prev_vol);


  gus_write8 (0x06, rate);	/* Ramping rate */

  voices[voice].volume_irq_mode = VMODE_ENVELOPE;

  if (((vol - prev_vol) / 64) == 0)	/* No significant volume change */
    {
      restore_flags (flags);
      step_envelope (voice);	/* Continue the envelope on the next step */
      return;
    }

  if (vol > prev_vol)
    {
      if (vol >= (4096 - 64))
	vol = 4096 - 65;
      gus_ramp_range (0, vol);
      gus_rampon (0x20);	/* Increasing volume, with IRQ */
    }
  else
    {
      if (vol <= 64)
	vol = 65;
      gus_ramp_range (vol, 4030);
      gus_rampon (0x60);	/* Decreasing volume, with IRQ */
    }
  voices[voice].current_volume = vol;
  restore_flags (flags);
}

static void
init_envelope (int voice)
{
  voices[voice].env_phase = -1;
  voices[voice].current_volume = 64;

  step_envelope (voice);
}

static void
start_release (int voice, long int flags)
{
  if (gus_read8 (0x00) & 0x03)
    return;			/* Voice already stopped */

  voices[voice].env_phase = 2;	/* Will be incremented by step_envelope */

  voices[voice].current_volume =
    voices[voice].initial_volume =
    gus_read16 (0x09) >> 4;	/* Get current volume */

  voices[voice].mode &= ~WAVE_SUSTAIN_ON;
  gus_rampoff ();
  restore_flags (flags);
  step_envelope (voice);
}

static void
gus_voice_fade (int voice)
{
  int             instr_no = sample_map[voice], is16bits;
  long int        flags;

  save_flags (flags);
  cli ();
  gus_select_voice (voice);

  if (instr_no < 0 || instr_no > MAX_SAMPLE)
    {
      gus_write8 (0x00, 0x03);	/* Hard stop */
      voice_alloc->map[voice] = 0;
      restore_flags (flags);
      return;
    }

  is16bits = (samples[instr_no].mode & WAVE_16_BITS) ? 1 : 0;	/* 8 or 16 bits */

  if (voices[voice].mode & WAVE_ENVELOPES)
    {
      start_release (voice, flags);
      return;
    }

  /*
   * Ramp the volume down but not too quickly.
   */
  if ((int) (gus_read16 (0x09) >> 4) < 100)	/* Get current volume */
    {
      gus_voice_off ();
      gus_rampoff ();
      gus_voice_init (voice);
      return;
    }

  gus_ramp_range (65, 4030);
  gus_ramp_rate (2, 4);
  gus_rampon (0x40 | 0x20);	/* Down, once, with IRQ */
  voices[voice].volume_irq_mode = VMODE_HALT;
  restore_flags (flags);
}

static void
gus_reset (void)
{
  int             i;

  gus_select_max_voices (24);
  volume_base = 3071;
  volume_scale = 4;
  volume_method = VOL_METHOD_ADAGIO;

  for (i = 0; i < 32; i++)
    {
      gus_voice_init (i);	/* Turn voice off */
      gus_voice_init2 (i);
    }

  inb (u_Status);		/* Touch the status register */

  gus_look8 (0x41);		/* Clear any pending DMA IRQs */
  gus_look8 (0x49);		/* Clear any pending sample IRQs */

  gus_read8 (0x0f);		/* Clear pending IRQs */

}

static void
gus_initialize (void)
{
  unsigned long   flags;
  unsigned char   dma_image, irq_image, tmp;

  static unsigned char gus_irq_map[16] =
  {0, 0, 0, 3, 0, 2, 0, 4, 0, 1, 0, 5, 6, 0, 0, 7};

  static unsigned char gus_dma_map[8] =
  {0, 1, 0, 2, 0, 3, 4, 5};

  save_flags (flags);
  cli ();
  gus_write8 (0x4c, 0);		/* Reset GF1 */
  gus_delay ();
  gus_delay ();

  gus_write8 (0x4c, 1);		/* Release Reset */
  gus_delay ();
  gus_delay ();

  /*
   * Clear all interrupts
   */

  gus_write8 (0x41, 0);		/* DMA control */
  gus_write8 (0x45, 0);		/* Timer control */
  gus_write8 (0x49, 0);		/* Sample control */

  gus_select_max_voices (24);

  inb (u_Status);		/* Touch the status register */

  gus_look8 (0x41);		/* Clear any pending DMA IRQs */
  gus_look8 (0x49);		/* Clear any pending sample IRQs */
  gus_read8 (0x0f);		/* Clear pending IRQs */

  gus_reset ();			/* Resets all voices */

  gus_look8 (0x41);		/* Clear any pending DMA IRQs */
  gus_look8 (0x49);		/* Clear any pending sample IRQs */
  gus_read8 (0x0f);		/* Clear pending IRQs */

  gus_write8 (0x4c, 7);		/* Master reset | DAC enable | IRQ enable */

  /*
   * Set up for Digital ASIC
   */

  outb (0x05, gus_base + 0x0f);

  mix_image |= 0x02;		/* Disable line out (for a moment) */
  outb (mix_image, u_Mixer);

  outb (0x00, u_IRQDMAControl);

  outb (0x00, gus_base + 0x0f);

  /*
   * Now set up the DMA and IRQ interface
   *
   * The GUS supports two IRQs and two DMAs.
   *
   * Just one DMA channel is used. This prevents simultaneous ADC and DAC.
   * Adding this support requires significant changes to the dmabuf.c, dsp.c
   * and audio.c also.
   */

  irq_image = 0;
  tmp = gus_irq_map[gus_irq];
  if (!tmp)
    printk ("Warning! GUS IRQ not selected\n");
  irq_image |= tmp;
  irq_image |= 0x40;		/* Combine IRQ1 (GF1) and IRQ2 (Midi) */

  dual_dma_mode = 1;
  if (gus_dma2 == gus_dma || gus_dma2 == -1)
    {
      dual_dma_mode = 0;
      dma_image = 0x40;		/* Combine DMA1 (DRAM) and IRQ2 (ADC) */

      tmp = gus_dma_map[gus_dma];
      if (!tmp)
	printk ("Warning! GUS DMA not selected\n");

      dma_image |= tmp;
    }
  else
    /* Setup dual DMA channel mode for GUS MAX */
    {
      dma_image = gus_dma_map[gus_dma];
      if (!dma_image)
	printk ("Warning! GUS DMA not selected\n");

      tmp = gus_dma_map[gus_dma2] << 3;
      if (!tmp)
	{
	  printk ("Warning! Invalid GUS MAX DMA\n");
	  tmp = 0x40;		/* Combine DMA channels */
	  dual_dma_mode = 0;
	}

      dma_image |= tmp;
    }

  /*
   * For some reason the IRQ and DMA addresses must be written twice
   */

  /*
   * Doing it first time
   */

  outb (mix_image, u_Mixer);	/* Select DMA control */
  outb (dma_image | 0x80, u_IRQDMAControl);	/* Set DMA address */

  outb (mix_image | 0x40, u_Mixer);	/* Select IRQ control */
  outb (irq_image, u_IRQDMAControl);	/* Set IRQ address */

  /*
   * Doing it second time
   */

  outb (mix_image, u_Mixer);	/* Select DMA control */
  outb (dma_image, u_IRQDMAControl);	/* Set DMA address */

  outb (mix_image | 0x40, u_Mixer);	/* Select IRQ control */
  outb (irq_image, u_IRQDMAControl);	/* Set IRQ address */

  gus_select_voice (0);		/* This disables writes to IRQ/DMA reg */

  mix_image &= ~0x02;		/* Enable line out */
  mix_image |= 0x08;		/* Enable IRQ */
  outb (mix_image, u_Mixer);	/*
				 * Turn mixer channels on
				 * Note! Mic in is left off.
				 */

  gus_select_voice (0);		/* This disables writes to IRQ/DMA reg */

  gusintr (0, NULL);		/* Serve pending interrupts */
  restore_flags (flags);
}

int
gus_wave_detect (int baseaddr)
{
  unsigned long   i;
  unsigned long   loc;

  gus_base = baseaddr;

  gus_write8 (0x4c, 0);		/* Reset GF1 */
  gus_delay ();
  gus_delay ();

  gus_write8 (0x4c, 1);		/* Release Reset */
  gus_delay ();
  gus_delay ();

  /* See if there is first block there.... */
  gus_poke (0L, 0xaa);
  if (gus_peek (0L) != 0xaa)
    return (0);

  /* Now zero it out so that I can check for mirroring .. */
  gus_poke (0L, 0x00);
  for (i = 1L; i < 1024L; i++)
    {
      int             n, failed;

      /* check for mirroring ... */
      if (gus_peek (0L) != 0)
	break;
      loc = i << 10;

      for (n = loc - 1, failed = 0; n <= loc; n++)
	{
	  gus_poke (loc, 0xaa);
	  if (gus_peek (loc) != 0xaa)
	    failed = 1;

	  gus_poke (loc, 0x55);
	  if (gus_peek (loc) != 0x55)
	    failed = 1;
	}

      if (failed)
	break;
    }
  gus_mem_size = i << 10;
  return 1;
}

static int
guswave_ioctl (int dev,
	       unsigned int cmd, ioctl_arg arg)
{

  switch (cmd)
    {
    case SNDCTL_SYNTH_INFO:
      gus_info.nr_voices = nr_voices;
      memcpy_tofs ((&((char *) arg)[0]), &gus_info, sizeof (gus_info));
      return 0;
      break;

    case SNDCTL_SEQ_RESETSAMPLES:
      reset_sample_memory ();
      return 0;
      break;

    case SNDCTL_SEQ_PERCMODE:
      return 0;
      break;

    case SNDCTL_SYNTH_MEMAVL:
      return gus_mem_size - free_mem_ptr - 32;

    default:
      return -EINVAL;
    }
}

static int
guswave_set_instr (int dev, int voice, int instr_no)
{
  int             sample_no;

  if (instr_no < 0 || instr_no > MAX_PATCH)
    return -EINVAL;

  if (voice < 0 || voice > 31)
    return -EINVAL;

  if (voices[voice].volume_irq_mode == VMODE_START_NOTE)
    {
      voices[voice].sample_pending = instr_no;
      return 0;
    }

  sample_no = patch_table[instr_no];
  patch_map[voice] = -1;

  if (sample_no < 0)
    {
      printk ("GUS: Undefined patch %d for voice %d\n", instr_no, voice);
      return -EINVAL;		/* Patch not defined */
    }

  if (sample_ptrs[sample_no] == -1)	/* Sample not loaded */
    {
      printk ("GUS: Sample #%d not loaded for patch %d (voice %d)\n",
	      sample_no, instr_no, voice);
      return -EINVAL;
    }

  sample_map[voice] = sample_no;
  patch_map[voice] = instr_no;
  return 0;
}

static int
guswave_kill_note (int dev, int voice, int note, int velocity)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();
  /* voice_alloc->map[voice] = 0xffff; */
  if (voices[voice].volume_irq_mode == VMODE_START_NOTE)
    {
      voices[voice].kill_pending = 1;
      restore_flags (flags);
    }
  else
    {
      restore_flags (flags);
      gus_voice_fade (voice);
    }

  restore_flags (flags);
  return 0;
}

static void
guswave_aftertouch (int dev, int voice, int pressure)
{
}

static void
guswave_panning (int dev, int voice, int value)
{
  if (voice >= 0 || voice < 32)
    voices[voice].panning = value;
}

static void
guswave_volume_method (int dev, int mode)
{
  if (mode == VOL_METHOD_LINEAR || mode == VOL_METHOD_ADAGIO)
    volume_method = mode;
}

static void
compute_volume (int voice, int volume)
{
  if (volume < 128)
    voices[voice].midi_volume = volume;

  switch (volume_method)
    {
    case VOL_METHOD_ADAGIO:
      voices[voice].initial_volume =
	gus_adagio_vol (voices[voice].midi_volume, voices[voice].main_vol,
			voices[voice].expression_vol,
			voices[voice].patch_vol);
      break;

    case VOL_METHOD_LINEAR:	/* Totally ignores patch-volume and expression */
      voices[voice].initial_volume =
	gus_linear_vol (volume, voices[voice].main_vol);
      break;

    default:
      voices[voice].initial_volume = volume_base +
	(voices[voice].midi_volume * volume_scale);
    }

  if (voices[voice].initial_volume > 4030)
    voices[voice].initial_volume = 4030;
}

static void
compute_and_set_volume (int voice, int volume, int ramp_time)
{
  int             curr, target, rate;
  unsigned long   flags;

  compute_volume (voice, volume);
  voices[voice].current_volume = voices[voice].initial_volume;

  save_flags (flags);
  cli ();
  /*
     * CAUTION! Interrupts disabled. Enable them before returning
   */

  gus_select_voice (voice);

  curr = gus_read16 (0x09) >> 4;
  target = voices[voice].initial_volume;

  if (ramp_time == INSTANT_RAMP)
    {
      gus_rampoff ();
      gus_voice_volume (target);
      restore_flags (flags);
      return;
    }

  if (ramp_time == FAST_RAMP)
    rate = 63;
  else
    rate = 16;
  gus_ramp_rate (0, rate);

  if ((target - curr) / 64 == 0)	/* Close enough to target. */
    {
      gus_rampoff ();
      gus_voice_volume (target);
      restore_flags (flags);
      return;
    }

  if (target > curr)
    {
      if (target > (4095 - 65))
	target = 4095 - 65;
      gus_ramp_range (curr, target);
      gus_rampon (0x00);	/* Ramp up, once, no IRQ */
    }
  else
    {
      if (target < 65)
	target = 65;

      gus_ramp_range (target, curr);
      gus_rampon (0x40);	/* Ramp down, once, no irq */
    }
  restore_flags (flags);
}

static void
dynamic_volume_change (int voice)
{
  unsigned char   status;
  unsigned long   flags;

  save_flags (flags);
  cli ();
  gus_select_voice (voice);
  status = gus_read8 (0x00);	/* Get voice status */
  restore_flags (flags);

  if (status & 0x03)
    return;			/* Voice was not running */

  if (!(voices[voice].mode & WAVE_ENVELOPES))
    {
      compute_and_set_volume (voice, voices[voice].midi_volume, 1);
      return;
    }

  /*
   * Voice is running and has envelopes.
   */

  save_flags (flags);
  cli ();
  gus_select_voice (voice);
  status = gus_read8 (0x0d);	/* Ramping status */
  restore_flags (flags);

  if (status & 0x03)		/* Sustain phase? */
    {
      compute_and_set_volume (voice, voices[voice].midi_volume, 1);
      return;
    }

  if (voices[voice].env_phase < 0)
    return;

  compute_volume (voice, voices[voice].midi_volume);

}

static void
guswave_controller (int dev, int voice, int ctrl_num, int value)
{
  unsigned long   flags;
  unsigned long   freq;

  if (voice < 0 || voice > 31)
    return;

  switch (ctrl_num)
    {
    case CTRL_PITCH_BENDER:
      voices[voice].bender = value;

      if (voices[voice].volume_irq_mode != VMODE_START_NOTE)
	{
	  freq = compute_finetune (voices[voice].orig_freq, value,
				   voices[voice].bender_range);
	  voices[voice].current_freq = freq;

	  save_flags (flags);
	  cli ();
	  gus_select_voice (voice);
	  gus_voice_freq (freq);
	  restore_flags (flags);
	}
      break;

    case CTRL_PITCH_BENDER_RANGE:
      voices[voice].bender_range = value;
      break;
    case CTL_EXPRESSION:
      value /= 128;
    case CTRL_EXPRESSION:
      if (volume_method == VOL_METHOD_ADAGIO)
	{
	  voices[voice].expression_vol = value;
	  if (voices[voice].volume_irq_mode != VMODE_START_NOTE)
	    dynamic_volume_change (voice);
	}
      break;

    case CTL_PAN:
      voices[voice].panning = (value * 2) - 128;
      break;

    case CTL_MAIN_VOLUME:
      value = (value * 100) / 16383;

    case CTRL_MAIN_VOLUME:
      voices[voice].main_vol = value;
      if (voices[voice].volume_irq_mode != VMODE_START_NOTE)
	dynamic_volume_change (voice);
      break;

    default:
      break;
    }
}

static int
guswave_start_note2 (int dev, int voice, int note_num, int volume)
{
  int             sample, best_sample, best_delta, delta_freq;
  int             is16bits, samplep, patch, pan;
  unsigned long   note_freq, base_note, freq, flags;
  unsigned char   mode = 0;

  if (voice < 0 || voice > 31)
    {
      printk ("GUS: Invalid voice\n");
      return -EINVAL;
    }

  if (note_num == 255)
    {
      if (voices[voice].mode & WAVE_ENVELOPES)
	{
	  voices[voice].midi_volume = volume;
	  dynamic_volume_change (voice);
	  return 0;
	}

      compute_and_set_volume (voice, volume, 1);
      return 0;
    }

  if ((patch = patch_map[voice]) == -1)
    {
      return -EINVAL;
    }

  if ((samplep = patch_table[patch]) == -1)
    {
      return -EINVAL;
    }

  note_freq = note_to_freq (note_num);

  /*
   * Find a sample within a patch so that the note_freq is between low_note
   * and high_note.
   */
  sample = -1;

  best_sample = samplep;
  best_delta = 1000000;
  while (samplep >= 0 && sample == -1)
    {
      delta_freq = note_freq - samples[samplep].base_note;
      if (delta_freq < 0)
	delta_freq = -delta_freq;
      if (delta_freq < best_delta)
	{
	  best_sample = samplep;
	  best_delta = delta_freq;
	}
      if (samples[samplep].low_note <= note_freq &&
	  note_freq <= samples[samplep].high_note)
	sample = samplep;
      else
	samplep = samples[samplep].key;		/*
						   * Follow link
						 */
    }
  if (sample == -1)
    sample = best_sample;

  if (sample == -1)
    {
      printk ("GUS: Patch %d not defined for note %d\n", patch, note_num);
      return 0;			/* Should play default patch ??? */
    }

  is16bits = (samples[sample].mode & WAVE_16_BITS) ? 1 : 0;
  voices[voice].mode = samples[sample].mode;
  voices[voice].patch_vol = samples[sample].volume;

  if (voices[voice].mode & WAVE_ENVELOPES)
    {
      int             i;

      for (i = 0; i < 6; i++)
	{
	  voices[voice].env_rate[i] = samples[sample].env_rate[i];
	  voices[voice].env_offset[i] = samples[sample].env_offset[i];
	}
    }

  sample_map[voice] = sample;

  base_note = samples[sample].base_note / 100;	/* Try to avoid overflows */
  note_freq /= 100;

  freq = samples[sample].base_freq * note_freq / base_note;

  voices[voice].orig_freq = freq;

  /*
   * Since the pitch bender may have been set before playing the note, we
   * have to calculate the bending now.
   */

  freq = compute_finetune (voices[voice].orig_freq, voices[voice].bender,
			   voices[voice].bender_range);
  voices[voice].current_freq = freq;

  pan = (samples[sample].panning + voices[voice].panning) / 32;
  pan += 7;
  if (pan < 0)
    pan = 0;
  if (pan > 15)
    pan = 15;

  if (samples[sample].mode & WAVE_16_BITS)
    {
      mode |= 0x04;		/* 16 bits */
      if ((sample_ptrs[sample] >> 18) !=
	  ((sample_ptrs[sample] + samples[sample].len) >> 18))
	printk ("GUS: Sample address error\n");
    }

  /*************************************************************************
   *    CAUTION!        Interrupts disabled. Don't return before enabling
   *************************************************************************/

  save_flags (flags);
  cli ();
  gus_select_voice (voice);
  gus_voice_off ();
  gus_rampoff ();

  restore_flags (flags);

  if (voices[voice].mode & WAVE_ENVELOPES)
    {
      compute_volume (voice, volume);
      init_envelope (voice);
    }
  else
    {
      compute_and_set_volume (voice, volume, 0);
    }

  save_flags (flags);
  cli ();
  gus_select_voice (voice);

  if (samples[sample].mode & WAVE_LOOP_BACK)
    gus_write_addr (0x0a, sample_ptrs[sample] + samples[sample].len -
		    voices[voice].offset_pending, is16bits);	/* start=end */
  else
    gus_write_addr (0x0a, sample_ptrs[sample] + voices[voice].offset_pending,
		    is16bits);	/* Sample start=begin */

  if (samples[sample].mode & WAVE_LOOPING)
    {
      mode |= 0x08;

      if (samples[sample].mode & WAVE_BIDIR_LOOP)
	mode |= 0x10;

      if (samples[sample].mode & WAVE_LOOP_BACK)
	{
	  gus_write_addr (0x0a,
			  sample_ptrs[sample] + samples[sample].loop_end -
			  voices[voice].offset_pending, is16bits);
	  mode |= 0x40;
	}

      gus_write_addr (0x02, sample_ptrs[sample] + samples[sample].loop_start,
		      is16bits);	/* Loop start location */
      gus_write_addr (0x04, sample_ptrs[sample] + samples[sample].loop_end,
		      is16bits);	/* Loop end location */
    }
  else
    {
      mode |= 0x20;		/* Loop IRQ at the end */
      voices[voice].loop_irq_mode = LMODE_FINISH;	/* Ramp down at the end */
      voices[voice].loop_irq_parm = 1;
      gus_write_addr (0x02, sample_ptrs[sample],
		      is16bits);	/* Loop start location */
      gus_write_addr (0x04, sample_ptrs[sample] + samples[sample].len - 1,
		      is16bits);	/* Loop end location */
    }
  gus_voice_freq (freq);
  gus_voice_balance (pan);
  gus_voice_on (mode);
  restore_flags (flags);

  return 0;
}

/*
 * New guswave_start_note by Andrew J. Robinson attempts to minimize clicking
 * when the note playing on the voice is changed.  It uses volume
 * ramping.
 */

static int
guswave_start_note (int dev, int voice, int note_num, int volume)
{
  long int        flags;
  int             mode;
  int             ret_val = 0;

  save_flags (flags);
  cli ();
  if (note_num == 255)
    {
      if (voices[voice].volume_irq_mode == VMODE_START_NOTE)
	{
	  voices[voice].volume_pending = volume;
	}
      else
	{
	  ret_val = guswave_start_note2 (dev, voice, note_num, volume);
	}
    }
  else
    {
      gus_select_voice (voice);
      mode = gus_read8 (0x00);
      if (mode & 0x20)
	gus_write8 (0x00, mode & 0xdf);		/* No interrupt! */

      voices[voice].offset_pending = 0;
      voices[voice].kill_pending = 0;
      voices[voice].volume_irq_mode = 0;
      voices[voice].loop_irq_mode = 0;

      if (voices[voice].sample_pending >= 0)
	{
	  restore_flags (flags);	/* Run temporarily with interrupts enabled */
	  guswave_set_instr (voices[voice].dev_pending, voice,
			     voices[voice].sample_pending);
	  voices[voice].sample_pending = -1;
	  save_flags (flags);
	  cli ();
	  gus_select_voice (voice);	/* Reselect the voice (just to be sure) */
	}

      if ((mode & 0x01) || (int) ((gus_read16 (0x09) >> 4) < 2065))
	{
	  ret_val = guswave_start_note2 (dev, voice, note_num, volume);
	}
      else
	{
	  voices[voice].dev_pending = dev;
	  voices[voice].note_pending = note_num;
	  voices[voice].volume_pending = volume;
	  voices[voice].volume_irq_mode = VMODE_START_NOTE;

	  gus_rampoff ();
	  gus_ramp_range (2000, 4065);
	  gus_ramp_rate (0, 63);	/* Fastest possible rate */
	  gus_rampon (0x20 | 0x40);	/* Ramp down, once, irq */
	}
    }
  restore_flags (flags);
  return ret_val;
}

static void
guswave_reset (int dev)
{
  int             i;

  for (i = 0; i < 32; i++)
    {
      gus_voice_init (i);
      gus_voice_init2 (i);
    }
}

static int
guswave_open (int dev, int mode)
{
  int             err;

  if (gus_busy)
    return -EBUSY;

  gus_initialize ();
  voice_alloc->timestamp = 0;

  if ((err = DMAbuf_open_dma (gus_devnum)) < 0)
    {
      printk ("GUS: Loading saples without DMA\n");
      gus_no_dma = 1;		/* Upload samples using PIO */
    }
  else
    gus_no_dma = 0;

  {
    dram_sleep_flag.aborting = 0;
    dram_sleep_flag.mode = WK_NONE;
  };
  gus_busy = 1;
  active_device = GUS_DEV_WAVE;

  gus_reset ();

  return 0;
}

static void
guswave_close (int dev)
{
  gus_busy = 0;
  active_device = 0;
  gus_reset ();

  if (!gus_no_dma)
    DMAbuf_close_dma (gus_devnum);
}

static int
guswave_load_patch (int dev, int format, const snd_rw_buf * addr,
		    int offs, int count, int pmgr_flag)
{
  struct patch_info patch;
  int             instr;
  long            sizeof_patch;

  unsigned long   blk_size, blk_end, left, src_offs, target;

  sizeof_patch = (long) &patch.data[0] - (long) &patch;		/* Header size */

  if (format != GUS_PATCH)
    {
      printk ("GUS Error: Invalid patch format (key) 0x%x\n", format);
      return -EINVAL;
    }

  if (count < sizeof_patch)
    {
      printk ("GUS Error: Patch header too short\n");
      return -EINVAL;
    }

  count -= sizeof_patch;

  if (free_sample >= MAX_SAMPLE)
    {
      printk ("GUS: Sample table full\n");
      return -ENOSPC;
    }

  /*
   * Copy the header from user space but ignore the first bytes which have
   * been transferred already.
   */

  memcpy_fromfs (&((char *) &patch)[offs], &((addr)[offs]), sizeof_patch - offs);

  instr = patch.instr_no;

  if (instr < 0 || instr > MAX_PATCH)
    {
      printk ("GUS: Invalid patch number %d\n", instr);
      return -EINVAL;
    }

  if (count < patch.len)
    {
      printk ("GUS Warning: Patch record too short (%d<%d)\n",
	      count, (int) patch.len);
      patch.len = count;
    }

  if (patch.len <= 0 || patch.len > gus_mem_size)
    {
      printk ("GUS: Invalid sample length %d\n", (int) patch.len);
      return -EINVAL;
    }

  if (patch.mode & WAVE_LOOPING)
    {
      if (patch.loop_start < 0 || patch.loop_start >= patch.len)
	{
	  printk ("GUS: Invalid loop start\n");
	  return -EINVAL;
	}

      if (patch.loop_end < patch.loop_start || patch.loop_end > patch.len)
	{
	  printk ("GUS: Invalid loop end\n");
	  return -EINVAL;
	}
    }

  free_mem_ptr = (free_mem_ptr + 31) & ~31;	/* 32 byte alignment */

#define GUS_BANK_SIZE (256*1024)

  if (patch.mode & WAVE_16_BITS)
    {
      /*
       * 16 bit samples must fit one 256k bank.
       */
      if (patch.len >= GUS_BANK_SIZE)
	{
	  printk ("GUS: Sample (16 bit) too long %d\n", (int) patch.len);
	  return -ENOSPC;
	}

      if ((free_mem_ptr / GUS_BANK_SIZE) !=
	  ((free_mem_ptr + patch.len) / GUS_BANK_SIZE))
	{
	  unsigned long   tmp_mem =	/* Aling to 256K */
	  ((free_mem_ptr / GUS_BANK_SIZE) + 1) * GUS_BANK_SIZE;

	  if ((tmp_mem + patch.len) > gus_mem_size)
	    return -ENOSPC;

	  free_mem_ptr = tmp_mem;	/* This leaves unusable memory */
	}
    }

  if ((free_mem_ptr + patch.len) > gus_mem_size)
    return -ENOSPC;

  sample_ptrs[free_sample] = free_mem_ptr;

  /*
   * Tremolo is not possible with envelopes
   */

  if (patch.mode & WAVE_ENVELOPES)
    patch.mode &= ~WAVE_TREMOLO;

  memcpy ((char *) &samples[free_sample], &patch, sizeof_patch);

  /*
   * Link this_one sample to the list of samples for patch 'instr'.
   */

  samples[free_sample].key = patch_table[instr];
  patch_table[instr] = free_sample;

  /*
   * Use DMA to transfer the wave data to the DRAM
   */

  left = patch.len;
  src_offs = 0;
  target = free_mem_ptr;

  while (left)			/* Not completely transferred yet */
    {
      /* blk_size = audio_devs[gus_devnum]->buffsize; */
      blk_size = audio_devs[gus_devnum]->dmap_out->bytes_in_use;
      if (blk_size > left)
	blk_size = left;

      /*
       * DMA cannot cross 256k bank boundaries. Check for that.
       */
      blk_end = target + blk_size;

      if ((target >> 18) != (blk_end >> 18))
	{			/* Split the block */

	  blk_end &= ~(256 * 1024 - 1);
	  blk_size = blk_end - target;
	}

      if (gus_no_dma)
	{
	  /*
	   * For some reason the DMA is not possible. We have to use PIO.
	   */
	  long            i;
	  unsigned char   data;


	  for (i = 0; i < blk_size; i++)
	    {
	      data = get_fs_byte (&((addr)[sizeof_patch + i]));
	      if (patch.mode & WAVE_UNSIGNED)

		if (!(patch.mode & WAVE_16_BITS) || (i & 0x01))
		  data ^= 0x80;	/* Convert to signed */
	      gus_poke (target + i, data);
	    }
	}
      else
	{
	  unsigned long   address, hold_address;
	  unsigned char   dma_command;
	  unsigned long   flags;

	  /*
	   * OK, move now. First in and then out.
	   */

	  memcpy_fromfs (audio_devs[gus_devnum]->dmap_out->raw_buf, &((addr)[sizeof_patch + src_offs]), blk_size);

	  save_flags (flags);
	  cli ();
/******** INTERRUPTS DISABLED NOW ********/
	  gus_write8 (0x41, 0);	/* Disable GF1 DMA */
	  DMAbuf_start_dma (gus_devnum,
			    audio_devs[gus_devnum]->dmap_out->raw_buf_phys,
			    blk_size, DMA_MODE_WRITE);

	  /*
	   * Set the DRAM address for the wave data
	   */

	  address = target;

	  if (audio_devs[gus_devnum]->dmachan1 > 3)
	    {
	      hold_address = address;
	      address = address >> 1;
	      address &= 0x0001ffffL;
	      address |= (hold_address & 0x000c0000L);
	    }

	  gus_write16 (0x42, (address >> 4) & 0xffff);	/* DRAM DMA address */

	  /*
	   * Start the DMA transfer
	   */

	  dma_command = 0x21;	/* IRQ enable, DMA start */
	  if (patch.mode & WAVE_UNSIGNED)
	    dma_command |= 0x80;	/* Invert MSB */
	  if (patch.mode & WAVE_16_BITS)
	    dma_command |= 0x40;	/* 16 bit _DATA_ */
	  if (audio_devs[gus_devnum]->dmachan1 > 3)
	    dma_command |= 0x04;	/* 16 bit DMA _channel_ */

	  gus_write8 (0x41, dma_command);	/* Lets bo luteet (=bugs) */

	  /*
	   * Sleep here until the DRAM DMA done interrupt is served
	   */
	  active_device = GUS_DEV_WAVE;


	  {
	    unsigned long   tl;

	    if (HZ)
	      tl = current->timeout = jiffies + (HZ);
	    else
	      tl = 0xffffffff;
	    dram_sleep_flag.mode = WK_SLEEP;
	    interruptible_sleep_on (&dram_sleeper);
	    if (!(dram_sleep_flag.mode & WK_WAKEUP))
	      {
		if (current->signal & ~current->blocked)
		  dram_sleep_flag.aborting = 1;
		else if (jiffies >= tl)
		  dram_sleep_flag.mode |= WK_TIMEOUT;
	      }
	    dram_sleep_flag.mode &= ~WK_SLEEP;
	  };
	  if ((dram_sleep_flag.mode & WK_TIMEOUT))
	    printk ("GUS: DMA Transfer timed out\n");
	  restore_flags (flags);
	}

      /*
       * Now the next part
       */

      left -= blk_size;
      src_offs += blk_size;
      target += blk_size;

      gus_write8 (0x41, 0);	/* Stop DMA */
    }

  free_mem_ptr += patch.len;

  if (!pmgr_flag)
    pmgr_inform (dev, PM_E_PATCH_LOADED, instr, free_sample, 0, 0);
  free_sample++;
  return 0;
}

static void
guswave_hw_control (int dev, unsigned char *event)
{
  int             voice, cmd;
  unsigned short  p1, p2;
  unsigned long   plong, flags;

  cmd = event[2];
  voice = event[3];
  p1 = *(unsigned short *) &event[4];
  p2 = *(unsigned short *) &event[6];
  plong = *(unsigned long *) &event[4];

  if ((voices[voice].volume_irq_mode == VMODE_START_NOTE) &&
      (cmd != _GUS_VOICESAMPLE) && (cmd != _GUS_VOICE_POS))
    do_volume_irq (voice);

  switch (cmd)
    {

    case _GUS_NUMVOICES:
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      gus_select_max_voices (p1);
      restore_flags (flags);
      break;

    case _GUS_VOICESAMPLE:
      guswave_set_instr (dev, voice, p1);
      break;

    case _GUS_VOICEON:
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      p1 &= ~0x20;		/* Don't allow interrupts */
      gus_voice_on (p1);
      restore_flags (flags);
      break;

    case _GUS_VOICEOFF:
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      gus_voice_off ();
      restore_flags (flags);
      break;

    case _GUS_VOICEFADE:
      gus_voice_fade (voice);
      break;

    case _GUS_VOICEMODE:
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      p1 &= ~0x20;		/* Don't allow interrupts */
      gus_voice_mode (p1);
      restore_flags (flags);
      break;

    case _GUS_VOICEBALA:
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      gus_voice_balance (p1);
      restore_flags (flags);
      break;

    case _GUS_VOICEFREQ:
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      gus_voice_freq (plong);
      restore_flags (flags);
      break;

    case _GUS_VOICEVOL:
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      gus_voice_volume (p1);
      restore_flags (flags);
      break;

    case _GUS_VOICEVOL2:	/* Just update the software voice level */
      voices[voice].initial_volume =
	voices[voice].current_volume = p1;
      break;

    case _GUS_RAMPRANGE:
      if (voices[voice].mode & WAVE_ENVELOPES)
	break;			/* NO-NO */
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      gus_ramp_range (p1, p2);
      restore_flags (flags);
      break;

    case _GUS_RAMPRATE:
      if (voices[voice].mode & WAVE_ENVELOPES)
	break;			/* NJET-NJET */
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      gus_ramp_rate (p1, p2);
      restore_flags (flags);
      break;

    case _GUS_RAMPMODE:
      if (voices[voice].mode & WAVE_ENVELOPES)
	break;			/* NO-NO */
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      p1 &= ~0x20;		/* Don't allow interrupts */
      gus_ramp_mode (p1);
      restore_flags (flags);
      break;

    case _GUS_RAMPON:
      if (voices[voice].mode & WAVE_ENVELOPES)
	break;			/* EI-EI */
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      p1 &= ~0x20;		/* Don't allow interrupts */
      gus_rampon (p1);
      restore_flags (flags);
      break;

    case _GUS_RAMPOFF:
      if (voices[voice].mode & WAVE_ENVELOPES)
	break;			/* NEJ-NEJ */
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      gus_rampoff ();
      restore_flags (flags);
      break;

    case _GUS_VOLUME_SCALE:
      volume_base = p1;
      volume_scale = p2;
      break;

    case _GUS_VOICE_POS:
      save_flags (flags);
      cli ();
      gus_select_voice (voice);
      gus_set_voice_pos (voice, plong);
      restore_flags (flags);
      break;

    default:;
    }
}

static int
gus_sampling_set_speed (int speed)
{

  if (speed <= 0)
    speed = gus_sampling_speed;

  if (speed < 4000)
    speed = 4000;

  if (speed > 44100)
    speed = 44100;

  gus_sampling_speed = speed;

  if (only_read_access)
    {
      /* Compute nearest valid recording speed  and return it */

      speed = (9878400 / (gus_sampling_speed + 2)) / 16;
      speed = (9878400 / (speed * 16)) - 2;
    }
  return speed;
}

static int
gus_sampling_set_channels (int channels)
{
  if (!channels)
    return gus_sampling_channels;
  if (channels > 2)
    channels = 2;
  if (channels < 1)
    channels = 1;
  gus_sampling_channels = channels;
  return channels;
}

static int
gus_sampling_set_bits (int bits)
{
  if (!bits)
    return gus_sampling_bits;

  if (bits != 8 && bits != 16)
    bits = 8;

  if (only_8_bits)
    bits = 8;

  gus_sampling_bits = bits;
  return bits;
}

static int
gus_sampling_ioctl (int dev, unsigned int cmd, ioctl_arg arg, int local)
{
  switch (cmd)
    {
    case SOUND_PCM_WRITE_RATE:
      if (local)
	return gus_sampling_set_speed ((int) arg);
      return snd_ioctl_return ((int *) arg, gus_sampling_set_speed (get_fs_long ((long *) arg)));
      break;

    case SOUND_PCM_READ_RATE:
      if (local)
	return gus_sampling_speed;
      return snd_ioctl_return ((int *) arg, gus_sampling_speed);
      break;

    case SNDCTL_DSP_STEREO:
      if (local)
	return gus_sampling_set_channels ((int) arg + 1) - 1;
      return snd_ioctl_return ((int *) arg, gus_sampling_set_channels (get_fs_long ((long *) arg) + 1) - 1);
      break;

    case SOUND_PCM_WRITE_CHANNELS:
      if (local)
	return gus_sampling_set_channels ((int) arg);
      return snd_ioctl_return ((int *) arg, gus_sampling_set_channels (get_fs_long ((long *) arg)));
      break;

    case SOUND_PCM_READ_CHANNELS:
      if (local)
	return gus_sampling_channels;
      return snd_ioctl_return ((int *) arg, gus_sampling_channels);
      break;

    case SNDCTL_DSP_SETFMT:
      if (local)
	return gus_sampling_set_bits ((int) arg);
      return snd_ioctl_return ((int *) arg, gus_sampling_set_bits (get_fs_long ((long *) arg)));
      break;

    case SOUND_PCM_READ_BITS:
      if (local)
	return gus_sampling_bits;
      return snd_ioctl_return ((int *) arg, gus_sampling_bits);

    case SOUND_PCM_WRITE_FILTER:	/* NOT POSSIBLE */
      return snd_ioctl_return ((int *) arg, -EINVAL);
      break;

    case SOUND_PCM_READ_FILTER:
      return snd_ioctl_return ((int *) arg, -EINVAL);
      break;

    }
  return -EINVAL;
}

static void
gus_sampling_reset (int dev)
{
  if (recording_active)
    {
      gus_write8 (0x49, 0x00);	/* Halt recording */
      set_input_volumes ();
    }
}

static int
gus_sampling_open (int dev, int mode)
{
  if (gus_busy)
    return -EBUSY;

  gus_initialize ();

  gus_busy = 1;
  active_device = 0;

  gus_reset ();
  reset_sample_memory ();
  gus_select_max_voices (14);

  pcm_active = 0;
  dma_active = 0;
  pcm_opened = 1;
  if (mode & OPEN_READ)
    {
      recording_active = 1;
      set_input_volumes ();
    }
  only_read_access = !(mode & OPEN_WRITE);
  only_8_bits = mode & OPEN_READ;
  if (only_8_bits)
    audio_devs[dev]->format_mask = AFMT_U8;
  else
    audio_devs[dev]->format_mask = AFMT_U8 | AFMT_S16_LE;

  return 0;
}

static void
gus_sampling_close (int dev)
{
  gus_reset ();
  gus_busy = 0;
  pcm_opened = 0;
  active_device = 0;

  if (recording_active)
    {
      gus_write8 (0x49, 0x00);	/* Halt recording */
      set_input_volumes ();
    }

  recording_active = 0;
}

static void
gus_sampling_update_volume (void)
{
  unsigned long   flags;
  int             voice;

  if (pcm_active && pcm_opened)
    for (voice = 0; voice < gus_sampling_channels; voice++)
      {
	save_flags (flags);
	cli ();
	gus_select_voice (voice);
	gus_rampoff ();
	gus_voice_volume (1530 + (25 * gus_pcm_volume));
	gus_ramp_range (65, 1530 + (25 * gus_pcm_volume));
	restore_flags (flags);
      }
}

static void
play_next_pcm_block (void)
{
  unsigned long   flags;
  int             speed = gus_sampling_speed;
  int             this_one, is16bits, chn;
  unsigned long   dram_loc;
  unsigned char   mode[2], ramp_mode[2];

  if (!pcm_qlen)
    return;

  this_one = pcm_head;

  for (chn = 0; chn < gus_sampling_channels; chn++)
    {
      mode[chn] = 0x00;
      ramp_mode[chn] = 0x03;	/* Ramping and rollover off */

      if (chn == 0)
	{
	  mode[chn] |= 0x20;	/* Loop IRQ */
	  voices[chn].loop_irq_mode = LMODE_PCM;
	}

      if (gus_sampling_bits != 8)
	{
	  is16bits = 1;
	  mode[chn] |= 0x04;	/* 16 bit data */
	}
      else
	is16bits = 0;

      dram_loc = this_one * pcm_bsize;
      dram_loc += chn * pcm_banksize;

      if (this_one == (pcm_nblk - 1))	/* Last fragment of the DRAM buffer */
	{
	  mode[chn] |= 0x08;	/* Enable loop */
	  ramp_mode[chn] = 0x03;	/* Disable rollover bit */
	}
      else
	{
	  if (chn == 0)
	    ramp_mode[chn] = 0x04;	/* Enable rollover bit */
	}

      save_flags (flags);
      cli ();
      gus_select_voice (chn);
      gus_voice_freq (speed);

      if (gus_sampling_channels == 1)
	gus_voice_balance (7);	/* mono */
      else if (chn == 0)
	gus_voice_balance (0);	/* left */
      else
	gus_voice_balance (15);	/* right */

      if (!pcm_active)		/* Playback not already active */
	{
	  /*
	   * The playback was not started yet (or there has been a pause).
	   * Start the voice (again) and ask for a rollover irq at the end of
	   * this_one block. If this_one one is last of the buffers, use just
	   * the normal loop with irq.
	   */

	  gus_voice_off ();
	  gus_rampoff ();
	  gus_voice_volume (1530 + (25 * gus_pcm_volume));
	  gus_ramp_range (65, 1530 + (25 * gus_pcm_volume));

	  gus_write_addr (0x0a, dram_loc, is16bits);	/* Starting position */
	  gus_write_addr (0x02, chn * pcm_banksize, is16bits);	/* Loop start */

	  if (chn != 0)
	    gus_write_addr (0x04, pcm_banksize + (pcm_bsize * pcm_nblk) - 1,
			    is16bits);	/* Loop end location */
	}

      if (chn == 0)
	gus_write_addr (0x04, dram_loc + pcm_datasize[this_one] - 1,
			is16bits);	/* Loop end location */
      else
	mode[chn] |= 0x08;	/* Enable looping */

      if (pcm_datasize[this_one] != pcm_bsize)
	{
	  /*
	   * Incompletely filled block. Possibly the last one.
	   */
	  if (chn == 0)
	    {
	      mode[chn] &= ~0x08;	/* Disable looping */
	      mode[chn] |= 0x20;	/* Enable IRQ at the end */
	      voices[0].loop_irq_mode = LMODE_PCM_STOP;
	      ramp_mode[chn] = 0x03;	/* No rollover bit */
	    }
	  else
	    {
	      gus_write_addr (0x04, dram_loc + pcm_datasize[this_one],
			      is16bits);	/* Loop end location */
	      mode[chn] &= ~0x08;	/* Disable looping */
	    }
	}

      restore_flags (flags);
    }

  for (chn = 0; chn < gus_sampling_channels; chn++)
    {
      save_flags (flags);
      cli ();
      gus_select_voice (chn);
      gus_write8 (0x0d, ramp_mode[chn]);
      gus_voice_on (mode[chn]);
      restore_flags (flags);
    }

  pcm_active = 1;
}

static void
gus_transfer_output_block (int dev, unsigned long buf,
			   int total_count, int intrflag, int chn)
{
  /*
   * This routine transfers one block of audio data to the DRAM. In mono mode
   * it's called just once. When in stereo mode, this_one routine is called
   * once for both channels.
   *
   * The left/mono channel data is transferred to the beginning of dram and the
   * right data to the area pointed by gus_page_size.
   */

  int             this_one, count;
  unsigned long   flags;
  unsigned char   dma_command;
  unsigned long   address, hold_address;

  save_flags (flags);
  cli ();

  count = total_count / gus_sampling_channels;

  if (chn == 0)
    {
      if (pcm_qlen >= pcm_nblk)
	printk ("GUS Warning: PCM buffers out of sync\n");

      this_one = pcm_current_block = pcm_tail;
      pcm_qlen++;
      pcm_tail = (pcm_tail + 1) % pcm_nblk;
      pcm_datasize[this_one] = count;
    }
  else
    this_one = pcm_current_block;

  gus_write8 (0x41, 0);		/* Disable GF1 DMA */
  DMAbuf_start_dma (dev, buf + (chn * count), count, DMA_MODE_WRITE);

  address = this_one * pcm_bsize;
  address += chn * pcm_banksize;

  if (audio_devs[dev]->dmachan1 > 3)
    {
      hold_address = address;
      address = address >> 1;
      address &= 0x0001ffffL;
      address |= (hold_address & 0x000c0000L);
    }

  gus_write16 (0x42, (address >> 4) & 0xffff);	/* DRAM DMA address */

  dma_command = 0x21;		/* IRQ enable, DMA start */

  if (gus_sampling_bits != 8)
    dma_command |= 0x40;	/* 16 bit _DATA_ */
  else
    dma_command |= 0x80;	/* Invert MSB */

  if (audio_devs[dev]->dmachan1 > 3)
    dma_command |= 0x04;	/* 16 bit DMA channel */

  gus_write8 (0x41, dma_command);	/* Kickstart */

  if (chn == (gus_sampling_channels - 1))	/* Last channel */
    {
      /*
       * Last (right or mono) channel data
       */
      dma_active = 1;		/* DMA started. There is a unacknowledged buffer */
      active_device = GUS_DEV_PCM_DONE;
      if (!pcm_active && (pcm_qlen > 0 || count < pcm_bsize))
	{
	  play_next_pcm_block ();
	}
    }
  else
    {
      /*
         * Left channel data. The right channel
         * is transferred after DMA interrupt
       */
      active_device = GUS_DEV_PCM_CONTINUE;
    }

  restore_flags (flags);
}

static void
gus_sampling_output_block (int dev, unsigned long buf, int total_count,
			   int intrflag, int restart_dma)
{
  pcm_current_buf = buf;
  pcm_current_count = total_count;
  pcm_current_intrflag = intrflag;
  pcm_current_dev = dev;
  gus_transfer_output_block (dev, buf, total_count, intrflag, 0);
}

static void
gus_sampling_start_input (int dev, unsigned long buf, int count,
			  int intrflag, int restart_dma)
{
  unsigned long   flags;
  unsigned char   mode;

  save_flags (flags);
  cli ();

  DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ);

  mode = 0xa0;			/* DMA IRQ enabled, invert MSB */

  if (audio_devs[dev]->dmachan2 > 3)
    mode |= 0x04;		/* 16 bit DMA channel */
  if (gus_sampling_channels > 1)
    mode |= 0x02;		/* Stereo */
  mode |= 0x01;			/* DMA enable */

  gus_write8 (0x49, mode);

  restore_flags (flags);
}

static int
gus_sampling_prepare_for_input (int dev, int bsize, int bcount)
{
  unsigned int    rate;

  rate = (9878400 / (gus_sampling_speed + 2)) / 16;

  gus_write8 (0x48, rate & 0xff);	/* Set sampling rate */

  if (gus_sampling_bits != 8)
    {
      printk ("GUS Error: 16 bit recording not supported\n");
      return -EINVAL;
    }

  return 0;
}

static int
gus_sampling_prepare_for_output (int dev, int bsize, int bcount)
{
  int             i;

  long            mem_ptr, mem_size;

  mem_ptr = 0;
  mem_size = gus_mem_size / gus_sampling_channels;

  if (mem_size > (256 * 1024))
    mem_size = 256 * 1024;

  pcm_bsize = bsize / gus_sampling_channels;
  pcm_head = pcm_tail = pcm_qlen = 0;

  pcm_nblk = MAX_PCM_BUFFERS;
  if ((pcm_bsize * pcm_nblk) > mem_size)
    pcm_nblk = mem_size / pcm_bsize;

  for (i = 0; i < pcm_nblk; i++)
    pcm_datasize[i] = 0;

  pcm_banksize = pcm_nblk * pcm_bsize;

  if (gus_sampling_bits != 8 && pcm_banksize == (256 * 1024))
    pcm_nblk--;

  return 0;
}

static int
gus_local_qlen (int dev)
{
  return pcm_qlen;
}

static void
gus_copy_from_user (int dev, char *localbuf, int localoffs,
		    const snd_rw_buf * userbuf, int useroffs, int len)
{
  if (gus_sampling_channels == 1)
    {
      memcpy_fromfs (&localbuf[localoffs], &((userbuf)[useroffs]), len);
    }
  else if (gus_sampling_bits == 8)
    {
      int             in_left = useroffs;
      int             in_right = useroffs + 1;
      char           *out_left, *out_right;
      int             i;

      len /= 2;
      localoffs /= 2;
      out_left = &localbuf[localoffs];
      out_right = out_left + pcm_bsize;

      for (i = 0; i < len; i++)
	{
	  *out_left++ = get_fs_byte (&((userbuf)[in_left]));
	  in_left += 2;
	  *out_right++ = get_fs_byte (&((userbuf)[in_right]));
	  in_right += 2;
	}
    }
  else
    {
      int             in_left = useroffs / 2;
      int             in_right = useroffs / 2 + 1;
      short          *out_left, *out_right;
      int             i;

      len /= 4;
      localoffs /= 4;

      out_left = (short *) &localbuf[localoffs];
      out_right = out_left + (pcm_bsize / 2);

      for (i = 0; i < len; i++)
	{
	  *out_left++ = get_fs_word (&(((short *) userbuf)[in_left]));
	  in_left += 2;
	  *out_right++ = get_fs_word (&(((short *) userbuf)[in_right]));
	  in_right += 2;
	}
    }
}

static struct audio_operations gus_sampling_operations =
{
  "Gravis UltraSound",
  NEEDS_RESTART,
  AFMT_U8 | AFMT_S16_LE,
  NULL,
  gus_sampling_open,
  gus_sampling_close,
  gus_sampling_output_block,
  gus_sampling_start_input,
  gus_sampling_ioctl,
  gus_sampling_prepare_for_input,
  gus_sampling_prepare_for_output,
  gus_sampling_reset,
  gus_sampling_reset,
  gus_local_qlen,
  gus_copy_from_user
};

static void
guswave_setup_voice (int dev, int voice, int chn)
{
  struct channel_info *info =
  &synth_devs[dev]->chn_info[chn];

  guswave_set_instr (dev, voice, info->pgm_num);

  voices[voice].expression_vol =
    info->controllers[CTL_EXPRESSION];	/* Just msb */
  voices[voice].main_vol =
    (info->controllers[CTL_MAIN_VOLUME] * 100) / 128;
  voices[voice].panning =
    (info->controllers[CTL_PAN] * 2) - 128;
  voices[voice].bender = info->bender_value;
}

static void
guswave_bender (int dev, int voice, int value)
{
  int             freq;
  unsigned long   flags;

  voices[voice].bender = value - 8192;
  freq = compute_finetune (voices[voice].orig_freq, value - 8192,
			   voices[voice].bender_range);
  voices[voice].current_freq = freq;

  save_flags (flags);
  cli ();
  gus_select_voice (voice);
  gus_voice_freq (freq);
  restore_flags (flags);
}

static int
guswave_patchmgr (int dev, struct patmgr_info *rec)
{
  int             i, n;

  switch (rec->command)
    {
    case PM_GET_DEVTYPE:
      rec->parm1 = PMTYPE_WAVE;
      return 0;
      break;

    case PM_GET_NRPGM:
      rec->parm1 = MAX_PATCH;
      return 0;
      break;

    case PM_GET_PGMMAP:
      rec->parm1 = MAX_PATCH;

      for (i = 0; i < MAX_PATCH; i++)
	{
	  int             ptr = patch_table[i];

	  rec->data.data8[i] = 0;

	  while (ptr >= 0 && ptr < free_sample)
	    {
	      rec->data.data8[i]++;
	      ptr = samples[ptr].key;	/* Follow link */
	    }
	}
      return 0;
      break;

    case PM_GET_PGM_PATCHES:
      {
	int             ptr = patch_table[rec->parm1];

	n = 0;

	while (ptr >= 0 && ptr < free_sample)
	  {
	    rec->data.data32[n++] = ptr;
	    ptr = samples[ptr].key;	/* Follow link */
	  }
      }
      rec->parm1 = n;
      return 0;
      break;

    case PM_GET_PATCH:
      {
	int             ptr = rec->parm1;
	struct patch_info *pat;

	if (ptr < 0 || ptr >= free_sample)
	  return -EINVAL;

	memcpy (rec->data.data8, (char *) &samples[ptr],
		sizeof (struct patch_info));

	pat = (struct patch_info *) rec->data.data8;

	pat->key = GUS_PATCH;	/* Restore patch type */
	rec->parm1 = sample_ptrs[ptr];	/* DRAM location */
	rec->parm2 = sizeof (struct patch_info);
      }
      return 0;
      break;

    case PM_SET_PATCH:
      {
	int             ptr = rec->parm1;
	struct patch_info *pat;

	if (ptr < 0 || ptr >= free_sample)
	  return -EINVAL;

	pat = (struct patch_info *) rec->data.data8;

	if (pat->len > samples[ptr].len)	/* Cannot expand sample */
	  return -EINVAL;

	pat->key = samples[ptr].key;	/* Ensure the link is correct */

	memcpy ((char *) &samples[ptr], rec->data.data8,
		sizeof (struct patch_info));

	pat->key = GUS_PATCH;
      }
      return 0;
      break;

    case PM_READ_PATCH:	/* Returns a block of wave data from the DRAM */
      {
	int             sample = rec->parm1;
	int             n;
	long            offs = rec->parm2;
	int             l = rec->parm3;

	if (sample < 0 || sample >= free_sample)
	  return -EINVAL;

	if (offs < 0 || offs >= samples[sample].len)
	  return -EINVAL;	/* Invalid offset */

	n = samples[sample].len - offs;		/* Num of bytes left */

	if (l > n)
	  l = n;

	if (l > sizeof (rec->data.data8))
	  l = sizeof (rec->data.data8);

	if (l <= 0)
	  return -EINVAL;	/*
				   * Was there a bug?
				 */

	offs += sample_ptrs[sample];	/*
					 * Begin offsess + offset to DRAM
					 */

	for (n = 0; n < l; n++)
	  rec->data.data8[n] = gus_peek (offs++);
	rec->parm1 = n;		/*
				 * Nr of bytes copied
				 */
      }
      return 0;
      break;

    case PM_WRITE_PATCH:	/*
				 * Writes a block of wave data to the DRAM
				 */
      {
	int             sample = rec->parm1;
	int             n;
	long            offs = rec->parm2;
	int             l = rec->parm3;

	if (sample < 0 || sample >= free_sample)
	  return -EINVAL;

	if (offs < 0 || offs >= samples[sample].len)
	  return -EINVAL;	/*
				   * Invalid offset
				 */

	n = samples[sample].len - offs;		/*
						   * Nr of bytes left
						 */

	if (l > n)
	  l = n;

	if (l > sizeof (rec->data.data8))
	  l = sizeof (rec->data.data8);

	if (l <= 0)
	  return -EINVAL;	/*
				   * Was there a bug?
				 */

	offs += sample_ptrs[sample];	/*
					 * Begin offsess + offset to DRAM
					 */

	for (n = 0; n < l; n++)
	  gus_poke (offs++, rec->data.data8[n]);
	rec->parm1 = n;		/*
				 * Nr of bytes copied
				 */
      }
      return 0;
      break;

    default:
      return -EINVAL;
    }
}

static int
guswave_alloc (int dev, int chn, int note, struct voice_alloc_info *alloc)
{
  int             i, p, best = -1, best_time = 0x7fffffff;

  p = alloc->ptr;
  /*
     * First look for a completely stopped voice
   */

  for (i = 0; i < alloc->max_voice; i++)
    {
      if (alloc->map[p] == 0)
	{
	  alloc->ptr = p;
	  return p;
	}
      if (alloc->alloc_times[p] < best_time)
	{
	  best = p;
	  best_time = alloc->alloc_times[p];
	}
      p = (p + 1) % alloc->max_voice;
    }

  /*
     * Then look for a releasing voice
   */

  for (i = 0; i < alloc->max_voice; i++)
    {
      if (alloc->map[p] == 0xffff)
	{
	  alloc->ptr = p;
	  return p;
	}
      p = (p + 1) % alloc->max_voice;
    }

  if (best >= 0)
    p = best;

  alloc->ptr = p;
  return p;
}

static struct synth_operations guswave_operations =
{
  &gus_info,
  0,
  SYNTH_TYPE_SAMPLE,
  SAMPLE_TYPE_GUS,
  guswave_open,
  guswave_close,
  guswave_ioctl,
  guswave_kill_note,
  guswave_start_note,
  guswave_set_instr,
  guswave_reset,
  guswave_hw_control,
  guswave_load_patch,
  guswave_aftertouch,
  guswave_controller,
  guswave_panning,
  guswave_volume_method,
  guswave_patchmgr,
  guswave_bender,
  guswave_alloc,
  guswave_setup_voice
};

static void
set_input_volumes (void)
{
  unsigned long   flags;
  unsigned char   mask = 0xff & ~0x06;	/* Just line out enabled */

  if (have_gus_max)		/* Don't disturb GUS MAX */
    return;

  save_flags (flags);
  cli ();

  /*
   *    Enable channels having vol > 10%
   *      Note! bit 0x01 means the line in DISABLED while 0x04 means
   *            the mic in ENABLED.
   */
  if (gus_line_vol > 10)
    mask &= ~0x01;
  if (gus_mic_vol > 10)
    mask |= 0x04;

  if (recording_active)
    {
      /*
       *    Disable channel, if not selected for recording
       */
      if (!(gus_recmask & SOUND_MASK_LINE))
	mask |= 0x01;
      if (!(gus_recmask & SOUND_MASK_MIC))
	mask &= ~0x04;
    }

  mix_image &= ~0x07;
  mix_image |= mask & 0x07;
  outb (mix_image, u_Mixer);

  restore_flags (flags);
}

int
gus_default_mixer_ioctl (int dev, unsigned int cmd, ioctl_arg arg)
{
#define MIX_DEVS	(SOUND_MASK_MIC|SOUND_MASK_LINE| \
			 SOUND_MASK_SYNTH|SOUND_MASK_PCM)
  if (((cmd >> 8) & 0xff) == 'M')
    {
      if (cmd & IOC_IN)
	switch (cmd & 0xff)
	  {
	  case SOUND_MIXER_RECSRC:
	    gus_recmask = get_fs_long ((long *) arg) & MIX_DEVS;
	    if (!(gus_recmask & (SOUND_MASK_MIC | SOUND_MASK_LINE)))
	      gus_recmask = SOUND_MASK_MIC;
	    /* Note! Input volumes are updated during next open for recording */
	    return snd_ioctl_return ((int *) arg, gus_recmask);
	    break;

	  case SOUND_MIXER_MIC:
	    {
	      int             vol = get_fs_long ((long *) arg) & 0xff;

	      if (vol < 0)
		vol = 0;
	      if (vol > 100)
		vol = 100;
	      gus_mic_vol = vol;
	      set_input_volumes ();
	      return snd_ioctl_return ((int *) arg, vol | (vol << 8));
	    }
	    break;

	  case SOUND_MIXER_LINE:
	    {
	      int             vol = get_fs_long ((long *) arg) & 0xff;

	      if (vol < 0)
		vol = 0;
	      if (vol > 100)
		vol = 100;
	      gus_line_vol = vol;
	      set_input_volumes ();
	      return snd_ioctl_return ((int *) arg, vol | (vol << 8));
	    }
	    break;

	  case SOUND_MIXER_PCM:
	    gus_pcm_volume = get_fs_long ((long *) arg) & 0xff;
	    if (gus_pcm_volume < 0)
	      gus_pcm_volume = 0;
	    if (gus_pcm_volume > 100)
	      gus_pcm_volume = 100;
	    gus_sampling_update_volume ();
	    return snd_ioctl_return ((int *) arg, gus_pcm_volume | (gus_pcm_volume << 8));
	    break;

	  case SOUND_MIXER_SYNTH:
	    {
	      int             voice;

	      gus_wave_volume = get_fs_long ((long *) arg) & 0xff;

	      if (gus_wave_volume < 0)
		gus_wave_volume = 0;
	      if (gus_wave_volume > 100)
		gus_wave_volume = 100;

	      if (active_device == GUS_DEV_WAVE)
		for (voice = 0; voice < nr_voices; voice++)
		  dynamic_volume_change (voice);	/* Apply the new vol */

	      return snd_ioctl_return ((int *) arg, gus_wave_volume | (gus_wave_volume << 8));
	    }
	    break;

	  default:
	    return -EINVAL;
	  }
      else
	switch (cmd & 0xff)	/*
				 * Return parameters
				 */
	  {

	  case SOUND_MIXER_RECSRC:
	    return snd_ioctl_return ((int *) arg, gus_recmask);
	    break;

	  case SOUND_MIXER_DEVMASK:
	    return snd_ioctl_return ((int *) arg, MIX_DEVS);
	    break;

	  case SOUND_MIXER_STEREODEVS:
	    return snd_ioctl_return ((int *) arg, 0);
	    break;

	  case SOUND_MIXER_RECMASK:
	    return snd_ioctl_return ((int *) arg, SOUND_MASK_MIC | SOUND_MASK_LINE);
	    break;

	  case SOUND_MIXER_CAPS:
	    return snd_ioctl_return ((int *) arg, 0);
	    break;

	  case SOUND_MIXER_MIC:
	    return snd_ioctl_return ((int *) arg, gus_mic_vol | (gus_mic_vol << 8));
	    break;

	  case SOUND_MIXER_LINE:
	    return snd_ioctl_return ((int *) arg, gus_line_vol | (gus_line_vol << 8));
	    break;

	  case SOUND_MIXER_PCM:
	    return snd_ioctl_return ((int *) arg, gus_pcm_volume | (gus_pcm_volume << 8));
	    break;

	  case SOUND_MIXER_SYNTH:
	    return snd_ioctl_return ((int *) arg, gus_wave_volume | (gus_wave_volume << 8));
	    break;

	  default:
	    return -EINVAL;
	  }
    }
  else
    return -EINVAL;
}

static struct mixer_operations gus_mixer_operations =
{
  "Gravis Ultrasound",
  gus_default_mixer_ioctl
};

static long
gus_default_mixer_init (long mem_start)
{
  if (num_mixers < MAX_MIXER_DEV)	/*
					 * Don't install if there is another
					 * mixer
					 */
    mixer_devs[num_mixers++] = &gus_mixer_operations;

  if (have_gus_max)
    {
/*
 *  Enable all mixer channels on the GF1 side. Otherwise recording will
 *  not be possible using GUS MAX.
 */
      mix_image &= ~0x07;
      mix_image |= 0x04;	/* All channels enabled */
      outb (mix_image, u_Mixer);
    }
  return mem_start;
}

long
gus_wave_init (long mem_start, struct address_info *hw_config)
{
  unsigned long   flags;
  unsigned char   val;
  char           *model_num = "2.4";
  int             gus_type = 0x24;	/* 2.4 */

  int             irq = hw_config->irq, dma = hw_config->dma, dma2 = hw_config->dma2;

  if (irq < 0 || irq > 15)
    {
      printk ("ERROR! Invalid IRQ#%d. GUS Disabled", irq);
      return mem_start;
    }

  if (dma < 0 || dma > 7)
    {
      printk ("ERROR! Invalid DMA#%d. GUS Disabled", dma);
      return mem_start;
    }

  gus_irq = irq;
  gus_dma = dma;
  gus_dma2 = dma2;

  if (gus_dma2 == -1)
    gus_dma2 = dma;

  /*
     * Try to identify the GUS model.
     *
     *  Versions < 3.6 don't have the digital ASIC. Try to probe it first.
   */

  save_flags (flags);
  cli ();
  outb (0x20, gus_base + 0x0f);
  val = inb (gus_base + 0x0f);
  restore_flags (flags);

  if (val != 0xff && (val & 0x06))	/* Should be 0x02?? */
    {
      /*
         * It has the digital ASIC so the card is at least v3.4.
         * Next try to detect the true model.
       */

      val = inb (u_MixSelect);

      /*
         * Value 255 means pre-3.7 which don't have mixer.
         * Values 5 thru 9 mean v3.7 which has a ICS2101 mixer.
         * 10 and above is GUS MAX which has the CS4231 codec/mixer.
         *
       */

      if (val == 255 || val < 5)
	{
	  model_num = "3.4";
	  gus_type = 0x34;
	}
      else if (val < 10)
	{
	  model_num = "3.7";
	  gus_type = 0x37;
	  mixer_type = ICS2101;
	  request_region (u_MixSelect, 1, "GUS mixer");
	}
      else
	{
	  model_num = "MAX";
	  gus_type = 0x40;
	  mixer_type = CS4231;
#ifndef EXCLUDE_GUSMAX
	  {
	    unsigned char   max_config = 0x40;	/* Codec enable */

	    if (gus_dma2 == -1)
	      gus_dma2 = gus_dma;

	    if (gus_dma > 3)
	      max_config |= 0x10;	/* 16 bit capture DMA */

	    if (gus_dma2 > 3)
	      max_config |= 0x20;	/* 16 bit playback DMA */

	    max_config |= (gus_base >> 4) & 0x0f;	/* Extract the X from 2X0 */

	    outb (max_config, gus_base + 0x106);	/* UltraMax control */
	  }

	  if (ad1848_detect (gus_base + 0x10c, NULL, hw_config->osp))
	    {

	      gus_mic_vol = gus_line_vol = gus_pcm_volume = 100;
	      gus_wave_volume = 90;
	      have_gus_max = 1;
	      ad1848_init ("GUS MAX", gus_base + 0x10c,
			   -irq,
			   gus_dma2,	/* Playback DMA */
			   gus_dma,	/* Capture DMA */
			   1,	/* Share DMA channels with GF1 */
			   hw_config->osp);
	    }
	  else
	    printk ("[Where's the CS4231?]");
#else
	  printk ("\n\n\nGUS MAX support was not compiled in!!!\n\n\n\n");
#endif
	}
    }
  else
    {
      /*
         * ASIC not detected so the card must be 2.2 or 2.4.
         * There could still be the 16-bit/mixer daughter card.
       */
    }


  printk (" <Gravis UltraSound %s (%dk)>", model_num, (int) gus_mem_size / 1024);

  sprintf (gus_info.name, "Gravis UltraSound %s (%dk)", model_num, (int) gus_mem_size / 1024);

  if (num_synths >= MAX_SYNTH_DEV)
    printk ("GUS Error: Too many synthesizers\n");
  else
    {
      voice_alloc = &guswave_operations.alloc;
      synth_devs[num_synths++] = &guswave_operations;
#ifndef EXCLUDE_SEQUENCER
      gus_tmr_install (gus_base + 8);
#endif
    }


  samples = (struct patch_info *) (sound_mem_blocks[sound_num_blocks] = kmalloc ((MAX_SAMPLE + 1) * sizeof (*samples), GFP_KERNEL));
  if (sound_num_blocks < 1024)
    sound_num_blocks++;;

  reset_sample_memory ();

  gus_initialize ();

  if (num_audiodevs < MAX_AUDIO_DEV)
    {
      audio_devs[gus_devnum = num_audiodevs++] = &gus_sampling_operations;
      audio_devs[gus_devnum]->dmachan1 = dma;
      audio_devs[gus_devnum]->dmachan2 = dma2;
      audio_devs[gus_devnum]->buffsize = DSP_BUFFSIZE;
      if (dma2 != dma && dma2 != -1)
	audio_devs[gus_devnum]->flags |= DMA_DUPLEX;
    }
  else
    printk ("GUS: Too many PCM devices available\n");

  /*
     *  Mixer dependent initialization.
   */

  switch (mixer_type)
    {
    case ICS2101:
      gus_mic_vol = gus_line_vol = gus_pcm_volume = 100;
      gus_wave_volume = 90;
      request_region (u_MixSelect, 1, "GUS mixer");
      return ics2101_mixer_init (mem_start);

    case CS4231:
      /* Initialized elsewhere (ad1848.c) */
    default:
      return gus_default_mixer_init (mem_start);
    }
}

void
gus_wave_unload (void)
{
#ifndef EXCLUDE_GUSMAX
  if (have_gus_max)
    {
      ad1848_unload (gus_base + 0x10c,
		     -gus_irq,
		     gus_dma2,	/* Playback DMA */
		     gus_dma,	/* Capture DMA */
		     1);	/* Share DMA channels with GF1 */
    }
#endif

  if (mixer_type == ICS2101)
    {
      release_region (u_MixSelect, 1);
    }
}

static void
do_loop_irq (int voice)
{
  unsigned char   tmp;
  int             mode, parm;
  unsigned long   flags;

  save_flags (flags);
  cli ();
  gus_select_voice (voice);

  tmp = gus_read8 (0x00);
  tmp &= ~0x20;			/*
				 * Disable wave IRQ for this_one voice
				 */
  gus_write8 (0x00, tmp);

  if (tmp & 0x03)		/* Voice stopped */
    voice_alloc->map[voice] = 0;

  mode = voices[voice].loop_irq_mode;
  voices[voice].loop_irq_mode = 0;
  parm = voices[voice].loop_irq_parm;

  switch (mode)
    {

    case LMODE_FINISH:		/*
				 * Final loop finished, shoot volume down
				 */

      if ((int) (gus_read16 (0x09) >> 4) < 100)		/*
							 * Get current volume
							 */
	{
	  gus_voice_off ();
	  gus_rampoff ();
	  gus_voice_init (voice);
	  break;
	}
      gus_ramp_range (65, 4065);
      gus_ramp_rate (0, 63);	/*
				 * Fastest possible rate
				 */
      gus_rampon (0x20 | 0x40);	/*
				 * Ramp down, once, irq
				 */
      voices[voice].volume_irq_mode = VMODE_HALT;
      break;

    case LMODE_PCM_STOP:
      pcm_active = 0;		/* Signal to the play_next_pcm_block routine */
    case LMODE_PCM:
      {
	int             flag;	/* 0 or 2 */

	pcm_qlen--;
	pcm_head = (pcm_head + 1) % pcm_nblk;
	if (pcm_qlen && pcm_active)
	  {
	    play_next_pcm_block ();
	  }
	else
	  {			/* Underrun. Just stop the voice */
	    gus_select_voice (0);	/* Left channel */
	    gus_voice_off ();
	    gus_rampoff ();
	    gus_select_voice (1);	/* Right channel */
	    gus_voice_off ();
	    gus_rampoff ();
	    pcm_active = 0;
	  }

	/*
	   * If the queue was full before this interrupt, the DMA transfer was
	   * suspended. Let it continue now.
	 */
	if (dma_active)
	  {
	    if (pcm_qlen == 0)
	      flag = 1;		/* Underflow */
	    else
	      flag = 0;
	    dma_active = 0;
	  }
	else
	  flag = 2;		/* Just notify the dmabuf.c */
	DMAbuf_outputintr (gus_devnum, flag);
      }
      break;

    default:;
    }
  restore_flags (flags);
}

static void
do_volume_irq (int voice)
{
  unsigned char   tmp;
  int             mode, parm;
  unsigned long   flags;

  save_flags (flags);
  cli ();

  gus_select_voice (voice);

  tmp = gus_read8 (0x0d);
  tmp &= ~0x20;			/*
				 * Disable volume ramp IRQ
				 */
  gus_write8 (0x0d, tmp);

  mode = voices[voice].volume_irq_mode;
  voices[voice].volume_irq_mode = 0;
  parm = voices[voice].volume_irq_parm;

  switch (mode)
    {
    case VMODE_HALT:		/*
				 * Decay phase finished
				 */
      restore_flags (flags);
      gus_voice_init (voice);
      break;

    case VMODE_ENVELOPE:
      gus_rampoff ();
      restore_flags (flags);
      step_envelope (voice);
      break;

    case VMODE_START_NOTE:
      restore_flags (flags);
      guswave_start_note2 (voices[voice].dev_pending, voice,
		  voices[voice].note_pending, voices[voice].volume_pending);
      if (voices[voice].kill_pending)
	guswave_kill_note (voices[voice].dev_pending, voice,
			   voices[voice].note_pending, 0);

      if (voices[voice].sample_pending >= 0)
	{
	  guswave_set_instr (voices[voice].dev_pending, voice,
			     voices[voice].sample_pending);
	  voices[voice].sample_pending = -1;
	}
      break;

    default:;
    }
}

void
gus_voice_irq (void)
{
  unsigned long   wave_ignore = 0, volume_ignore = 0;
  unsigned long   voice_bit;

  unsigned char   src, voice;

  while (1)
    {
      src = gus_read8 (0x0f);	/*
				 * Get source info
				 */
      voice = src & 0x1f;
      src &= 0xc0;

      if (src == (0x80 | 0x40))
	return;			/*
				 * No interrupt
				 */

      voice_bit = 1 << voice;

      if (!(src & 0x80))	/*
				 * Wave IRQ pending
				 */
	if (!(wave_ignore & voice_bit) && (int) voice < nr_voices)	/*
									   * Not done
									   * yet
									 */
	  {
	    wave_ignore |= voice_bit;
	    do_loop_irq (voice);
	  }

      if (!(src & 0x40))	/*
				 * Volume IRQ pending
				 */
	if (!(volume_ignore & voice_bit) && (int) voice < nr_voices)	/*
									   * Not done
									   * yet
									 */
	  {
	    volume_ignore |= voice_bit;
	    do_volume_irq (voice);
	  }
    }
}

void
guswave_dma_irq (void)
{
  unsigned char   status;

  status = gus_look8 (0x41);	/* Get DMA IRQ Status */
  if (status & 0x40)		/* DMA interrupt pending */
    switch (active_device)
      {
      case GUS_DEV_WAVE:
	if ((dram_sleep_flag.mode & WK_SLEEP))
	  {
	    dram_sleep_flag.mode = WK_WAKEUP;
	    wake_up (&dram_sleeper);
	  };
	break;

      case GUS_DEV_PCM_CONTINUE:	/* Left channel data transferred */
	gus_transfer_output_block (pcm_current_dev, pcm_current_buf,
				   pcm_current_count,
				   pcm_current_intrflag, 1);
	break;

      case GUS_DEV_PCM_DONE:	/* Right or mono channel data transferred */
	if (pcm_qlen < pcm_nblk)
	  {
	    int             flag = (1 - dma_active) * 2;	/* 0 or 2 */

	    if (pcm_qlen == 0)
	      flag = 1;		/* Underrun */
	    dma_active = 0;
	    DMAbuf_outputintr (gus_devnum, flag);
	  }
	break;

      default:;
      }

  status = gus_look8 (0x49);	/*
				 * Get Sampling IRQ Status
				 */
  if (status & 0x40)		/*
				 * Sampling Irq pending
				 */
    {
      DMAbuf_inputintr (gus_devnum);
    }

}

#ifndef EXCLUDE_SEQUENCER
/*
 * Timer stuff
 */

static volatile int select_addr, data_addr;
static volatile int curr_timer = 0;

void
gus_timer_command (unsigned int addr, unsigned int val)
{
  int             i;

  outb ((unsigned char) (addr & 0xff), select_addr);

  for (i = 0; i < 2; i++)
    inb (select_addr);

  outb ((unsigned char) (val & 0xff), data_addr);

  for (i = 0; i < 2; i++)
    inb (select_addr);
}

static void
arm_timer (int timer, unsigned int interval)
{

  curr_timer = timer;

  if (timer == 1)
    {
      gus_write8 (0x46, 256 - interval);	/* Set counter for timer 1 */
      gus_write8 (0x45, 0x04);	/* Enable timer 1 IRQ */
      gus_timer_command (0x04, 0x01);	/* Start timer 1 */
    }
  else
    {
      gus_write8 (0x47, 256 - interval);	/* Set counter for timer 2 */
      gus_write8 (0x45, 0x08);	/* Enable timer 2 IRQ */
      gus_timer_command (0x04, 0x02);	/* Start timer 2 */
    }

  gus_timer_enabled = 0;
}

static unsigned int
gus_tmr_start (int dev, unsigned int usecs_per_tick)
{
  int             timer_no, resolution;
  int             divisor;

  if (usecs_per_tick > (256 * 80))
    {
      timer_no = 2;
      resolution = 320;		/* usec */
    }
  else
    {
      timer_no = 1;
      resolution = 80;		/* usec */
    }

  divisor = (usecs_per_tick + (resolution / 2)) / resolution;

  arm_timer (timer_no, divisor);

  return divisor * resolution;
}

static void
gus_tmr_disable (int dev)
{
  gus_write8 (0x45, 0);		/* Disable both timers */
  gus_timer_enabled = 0;
}

static void
gus_tmr_restart (int dev)
{
  if (curr_timer == 1)
    gus_write8 (0x45, 0x04);	/* Start timer 1 again */
  else
    gus_write8 (0x45, 0x08);	/* Start timer 2 again */
}

static struct sound_lowlev_timer gus_tmr =
{
  0,
  gus_tmr_start,
  gus_tmr_disable,
  gus_tmr_restart
};

static void
gus_tmr_install (int io_base)
{
  select_addr = io_base;
  data_addr = io_base + 1;

  sound_timer_init (&gus_tmr, "GUS");
}
#endif

#endif
