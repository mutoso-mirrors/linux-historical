/*
 * sound/trix.c
 *
 * Low level driver for the MediaTriX AudioTriX Pro
 * (MT-0002-PC Control Chip)
 *
 * Copyright by Hannu Savolainen 1995
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

#if defined(CONFIGURE_SOUNDCARD) && !defined(EXCLUDE_TRIX)

#ifdef INCLUDE_TRIX_BOOT
#include "trix_boot.h"
#endif

static int      kilroy_was_here = 0;	/* Don't detect twice */
static int      sb_initialized = 0;
static int      mpu_initialized = 0;

static sound_os_info *trix_osp = NULL;

static unsigned char
trix_read (int addr)
{
  outb ((unsigned char) addr, 0x390);	/* MT-0002-PC ASIC address */
  return inb (0x391);		/* MT-0002-PC ASIC data */
}

static void
trix_write (int addr, int data)
{
  outb ((unsigned char) addr, 0x390);	/* MT-0002-PC ASIC address */
  outb ((unsigned char) data, 0x391);	/* MT-0002-PC ASIC data */
}

static void
download_boot (int base)
{
#ifdef INCLUDE_TRIX_BOOT
  int             i = 0, n = sizeof (trix_boot);

  trix_write (0xf8, 0x00);	/* ??????? */
  outb (0x01, base + 6);	/* Clear the internal data pointer */
  outb (0x00, base + 6);	/* Restart */

  /*
     *  Write the boot code to the RAM upload/download register.
     *  Each write increments the internal data pointer.
   */
  outb (0x01, base + 6);	/* Clear the internal data pointer */
  outb (0x1A, 0x390);		/* Select RAM download/upload port */

  for (i = 0; i < n; i++)
    outb (trix_boot[i], 0x391);
  for (i = n; i < 10016; i++)	/* Clear up to first 16 bytes of data RAM */
    outb (0x00, 0x391);
  outb (0x00, base + 6);	/* Reset */
  outb (0x50, 0x390);		/* ?????? */
#endif
}

static int
trix_set_wss_port (struct address_info *hw_config)
{
  unsigned char   addr_bits;

  if (check_region (0x390, 2))
    {
      printk ("AudioTriX: Config port I/O conflict\n");
      return 0;
    }

  if (kilroy_was_here)		/* Already initialized */
    return 0;

  if (trix_read (0x15) != 0x71)	/* No asic signature */
    {
      DDB (printk ("No AudioTriX ASIC signature found\n"));
      return 0;
    }

  request_region (0x390, 2, "AudioTriX");

  kilroy_was_here = 1;

  /*
   * Reset some registers.
   */

  trix_write (0x13, 0);
  trix_write (0x14, 0);

  /*
     * Configure the ASIC to place the codec to the proper I/O location
   */

  switch (hw_config->io_base)
    {
    case 0x530:
      addr_bits = 0;
      break;
    case 0x604:
      addr_bits = 1;
      break;
    case 0xE80:
      addr_bits = 2;
      break;
    case 0xF40:
      addr_bits = 3;
      break;
    default:
      return 0;
    }

  trix_write (0x19, (trix_read (0x19) & 0x03) | addr_bits);
  return 1;
}

/*
 *    Probe and attach routines for the Windows Sound System mode of
 *      AudioTriX Pro
 */

int
probe_trix_wss (struct address_info *hw_config)
{
  /*
     * Check if the IO port returns valid signature. The original MS Sound
     * system returns 0x04 while some cards (AudioTriX Pro for example)
     * return 0x00.
   */
  if (check_region (hw_config->io_base, 8))
    {
      printk ("AudioTriX: MSS I/O port conflict\n");
      return 0;
    }

  trix_osp = hw_config->osp;

  if (!trix_set_wss_port (hw_config))
    return 0;

  if ((inb (hw_config->io_base + 3) & 0x3f) != 0x00)
    {
      DDB (printk ("No MSS signature detected on port 0x%x\n", hw_config->io_base));
      return 0;
    }

  if (hw_config->irq > 11)
    {
      printk ("AudioTriX: Bad WSS IRQ %d\n", hw_config->irq);
      return 0;
    }

  if (hw_config->dma != 0 && hw_config->dma != 1 && hw_config->dma != 3)
    {
      printk ("AudioTriX: Bad WSS DMA %d\n", hw_config->dma);
      return 0;
    }

  if (hw_config->dma2 != -1)
    if (hw_config->dma2 != 0 && hw_config->dma2 != 1 && hw_config->dma2 != 3)
      {
	printk ("AudioTriX: Bad capture DMA %d\n", hw_config->dma2);
	return 0;
      }

  /*
     * Check that DMA0 is not in use with a 8 bit board.
   */

  if (hw_config->dma == 0 && inb (hw_config->io_base + 3) & 0x80)
    {
      printk ("AudioTriX: Can't use DMA0 with a 8 bit card\n");
      return 0;
    }

  if (hw_config->irq > 7 && hw_config->irq != 9 && inb (hw_config->io_base + 3) & 0x80)
    {
      printk ("AudioTriX: Can't use IRQ%d with a 8 bit card\n", hw_config->irq);
      return 0;
    }

  return ad1848_detect (hw_config->io_base + 4, NULL, hw_config->osp);
}

long
attach_trix_wss (long mem_start, struct address_info *hw_config)
{
  static unsigned char interrupt_bits[12] =
  {-1, -1, -1, -1, -1, -1, -1, 0x08, -1, 0x10, 0x18, 0x20};
  char            bits;

  static unsigned char dma_bits[4] =
  {1, 2, 0, 3};

  int             config_port = hw_config->io_base + 0, version_port = hw_config->io_base + 3;
  int             dma1 = hw_config->dma, dma2 = hw_config->dma2;

  trix_osp = hw_config->osp;

  if (!kilroy_was_here)
    {
      DDB (printk ("AudioTriX: Attach called but not probed yet???\n"));
      return mem_start;
    }

  /*
     * Set the IRQ and DMA addresses.
   */

  bits = interrupt_bits[hw_config->irq];
  if (bits == -1)
    {
      printk ("AudioTriX: Bad IRQ (%d)\n", hw_config->irq);
      return mem_start;
    }

  outb (bits | 0x40, config_port);
  if ((inb (version_port) & 0x40) == 0)
    printk ("[IRQ Conflict?]");

  if (hw_config->dma2 == -1)	/* Single DMA mode */
    {
      bits |= dma_bits[dma1];
      dma2 = dma1;
    }
  else
    {
      unsigned char   tmp;

      tmp = trix_read (0x13) & ~30;
      trix_write (0x13, tmp | 0x80 | (dma1 << 4));

      tmp = trix_read (0x14) & ~30;
      trix_write (0x14, tmp | 0x80 | (dma2 << 4));
    }

  outb (bits, config_port);	/* Write IRQ+DMA setup */

  ad1848_init ("AudioTriX Pro", hw_config->io_base + 4,
	       hw_config->irq,
	       dma1,
	       dma2,
	       0,
	       hw_config->osp);
  request_region (hw_config->io_base, 4, "MSS config");
  return mem_start;
}

int
probe_trix_sb (struct address_info *hw_config)
{

  int             tmp;
  unsigned char   conf;
  static char     irq_translate[] =
  {-1, -1, -1, 0, 1, 2, -1, 3};

#ifndef INCLUDE_TRIX_BOOT
  return 0;			/* No boot code -> no fun */
#endif
  if (!kilroy_was_here)
    return 0;			/* AudioTriX Pro has not been detected earlier */

  if (sb_initialized)
    return 0;

  if (check_region (hw_config->io_base, 16))
    {
      printk ("AudioTriX: SB I/O port conflict\n");
      return 0;
    }

  if (hw_config->io_base & 0xffffff8f != 0x200)
    return 0;

  tmp = hw_config->irq;
  if (tmp > 7)
    return 0;
  if (irq_translate[tmp] == -1)
    return 0;

  tmp = hw_config->dma;
  if (tmp != 1 && tmp != 3)
    return 0;

  conf = 0x84;			/* DMA and IRQ enable */
  conf |= hw_config->io_base & 0x70;	/* I/O address bits */
  conf |= irq_translate[hw_config->irq];
  if (hw_config->dma == 3)
    conf |= 0x08;
  trix_write (0x1b, conf);

  download_boot (hw_config->io_base);
  sb_initialized = 1;

  return 1;
}

long
attach_trix_sb (long mem_start, struct address_info *hw_config)
{
#ifndef EXCLUDE_SB
  extern int      sb_no_recording;

  sb_dsp_disable_midi ();
  sb_no_recording = 1;
#endif
  printk (" <AudioTriX (SB)>");
  return mem_start;
}

long
attach_trix_mpu (long mem_start, struct address_info *hw_config)
{
#if (!defined(EXCLUDE_MPU401) || !defined(EXCLUDE_MPU_EMU)) && !defined(EXCLUDE_MIDI)
  return attach_mpu401 (mem_start, hw_config);
#else
  return mem_start;
#endif
}

int
probe_trix_mpu (struct address_info *hw_config)
{
#if (!defined(EXCLUDE_MPU401) || !defined(EXCLUDE_MPU_EMU)) && !defined(EXCLUDE_MIDI)
  unsigned char   conf;
  static char     irq_bits[] =
  {-1, -1, -1, 1, 2, 3, -1, 4, -1, 5};

  if (!kilroy_was_here)
    return 0;			/* AudioTriX Pro has not been detected earlier */

  if (!sb_initialized)
    return 0;

  if (mpu_initialized)
    return 0;

  if (check_region (hw_config->io_base, 4))
    {
      printk ("AudioTriX: MPU I/O port conflict\n");
      return 0;
    }

  if (hw_config->irq > 9)
    return 0;

  if (irq_bits[hw_config->irq] == -1)
    return 0;

  switch (hw_config->io_base)
    {
    case 0x330:
      conf = 0x00;
      break;
    case 0x370:
      conf = 0x04;
      break;
    case 0x3b0:
      conf = 0x08;
      break;
    case 0x3f0:
      conf = 0x0c;
      break;
    default:
      return 0;			/* Invalid port */
    }

  conf |= irq_bits[hw_config->irq] << 4;

  trix_write (0x19, (trix_read (0x19) & 0x83) | conf);

  mpu_initialized = 1;

  return probe_mpu401 (hw_config);
#else
  return 0;
#endif
}

void
unload_trix_wss (struct address_info *hw_config)
{
  int             dma2 = hw_config->dma2;

  if (dma2 == -1)
    dma2 = hw_config->dma;

  release_region (0x390, 2);
  release_region (hw_config->io_base, 4);

  ad1848_unload (hw_config->io_base + 4,
		 hw_config->irq,
		 hw_config->dma,
		 dma2,
		 0);
}

void
unload_trix_mpu (struct address_info *hw_config)
{
#if (!defined(EXCLUDE_MPU401) || !defined(EXCLUDE_MPU_EMU)) && !defined(EXCLUDE_MIDI)
  unload_mpu401 (hw_config);
#endif
}
void
unload_trix_sb (struct address_info *hw_config)
{
}


#endif
