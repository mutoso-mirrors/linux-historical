/*
 * sound/audio.c
 *
 * Device file manager for /dev/audio
 *
 * Copyright by Hannu Savolainen 1993
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

#ifdef CONFIGURE_SOUNDCARD
#ifndef EXCLUDE_AUDIO

#include "ulaw.h"
#include "coproc.h"

#define ON		1
#define OFF		0

static int      wr_buff_no[MAX_AUDIO_DEV];	/*

						 * != -1, if there is
						 * a incomplete output
						 * block in the queue.
						 */
static int      wr_buff_size[MAX_AUDIO_DEV], wr_buff_ptr[MAX_AUDIO_DEV];

static int      audio_mode[MAX_AUDIO_DEV];
static int      dev_nblock[MAX_AUDIO_DEV];	/* 1 if in noblocking mode */

#define		AM_NONE		0
#define		AM_WRITE	1
#define 	AM_READ		2

static char    *wr_dma_buf[MAX_AUDIO_DEV];
static int      audio_format[MAX_AUDIO_DEV];
static int      local_conversion[MAX_AUDIO_DEV];

static int
set_format (int dev, long fmt)
{
  if (fmt != AFMT_QUERY)
    {

      local_conversion[dev] = 0;

      if (!(audio_devs[dev]->format_mask & fmt))	/* Not supported */
	if (fmt == AFMT_MU_LAW)
	  {
	    fmt = AFMT_U8;
	    local_conversion[dev] = AFMT_MU_LAW;
	  }
	else
	  fmt = AFMT_U8;	/* This is always supported */

      audio_format[dev] = DMAbuf_ioctl (dev, SNDCTL_DSP_SETFMT, (ioctl_arg) fmt, 1);
    }

  if (local_conversion[dev])	/* This shadows the HW format */
    return local_conversion[dev];

  return audio_format[dev];
}

int
audio_open (int dev, struct fileinfo *file)
{
  int             ret;
  long            bits;
  int             dev_type = dev & 0x0f;
  int             mode = file->mode & O_ACCMODE;

  dev = dev >> 4;

  if (dev_type == SND_DEV_DSP16)
    bits = 16;
  else
    bits = 8;

  if ((ret = DMAbuf_open (dev, mode)) < 0)
    return ret;

  if (audio_devs[dev]->coproc)
    if ((ret = audio_devs[dev]->coproc->
	 open (audio_devs[dev]->coproc->devc, COPR_PCM)) < 0)
      {
	audio_release (dev, file);
	printk ("Sound: Can't access coprocessor device\n");

	return ret;
      }

  local_conversion[dev] = 0;

  if (DMAbuf_ioctl (dev, SNDCTL_DSP_SETFMT, (ioctl_arg) bits, 1) != bits)
    {
      audio_release (dev, file);
      return -ENXIO;
    }

  if (dev_type == SND_DEV_AUDIO)
    {
      set_format (dev, AFMT_MU_LAW);
    }
  else
    set_format (dev, bits);

  wr_buff_no[dev] = -1;
  audio_mode[dev] = AM_NONE;
  wr_buff_size[dev] = wr_buff_ptr[dev] = 0;
  dev_nblock[dev] = 0;

  return ret;
}

void
audio_release (int dev, struct fileinfo *file)
{
  int             mode;

  dev = dev >> 4;
  mode = file->mode & O_ACCMODE;

  if (wr_buff_no[dev] >= 0)
    {
      DMAbuf_start_output (dev, wr_buff_no[dev], wr_buff_ptr[dev]);

      wr_buff_no[dev] = -1;
    }

  if (audio_devs[dev]->coproc)
    audio_devs[dev]->coproc->close (audio_devs[dev]->coproc->devc, COPR_PCM);
  DMAbuf_release (dev, mode);
}

#if defined(NO_INLINE_ASM) || !defined(i386)
static void
translate_bytes (const unsigned char *table, unsigned char *buff, int n)
{
  unsigned long   i;

  if (n <= 0)
    return;

  for (i = 0; i < n; ++i)
    buff[i] = table[buff[i]];
}

#else
extern inline void
translate_bytes (const void *table, void *buff, int n)
{
  if (n > 0)
    {
      __asm__ ("cld\n"
	       "1:\tlodsb\n\t"
	       "xlatb\n\t"
	       "stosb\n\t"
    "loop 1b\n\t":
    :	   "b" ((long) table), "c" (n), "D" ((long) buff), "S" ((long) buff)
    :	       "bx", "cx", "di", "si", "ax");
    }
}

#endif

int
audio_write (int dev, struct fileinfo *file, const snd_rw_buf * buf, int count)
{
  int             c, p, l;
  int             err;

  dev = dev >> 4;

  p = 0;
  c = count;

  if ((audio_mode[dev] & AM_READ) && !(audio_devs[dev]->flags & DMA_DUPLEX))
    {				/* Direction change */
      wr_buff_no[dev] = -1;
    }

  if (audio_devs[dev]->flags & DMA_DUPLEX)
    audio_mode[dev] |= AM_WRITE;
  else
    audio_mode[dev] = AM_WRITE;

  if (!count)			/*
				 * Flush output
				 */
    {
      if (wr_buff_no[dev] >= 0)
	{
	  DMAbuf_start_output (dev, wr_buff_no[dev], wr_buff_ptr[dev]);

	  wr_buff_no[dev] = -1;
	}
      return 0;
    }

  while (c)
    {				/*
				 * Perform output blocking
				 */
      if (wr_buff_no[dev] < 0)	/*
				 * There is no incomplete buffers
				 */
	{
	  if ((wr_buff_no[dev] = DMAbuf_getwrbuffer (dev, &wr_dma_buf[dev],
						     &wr_buff_size[dev],
						     dev_nblock[dev])) < 0)
	    {
	      /* Handle nonblocking mode */
	      if (dev_nblock[dev] && wr_buff_no[dev] == -EAGAIN)
		return p;	/* No more space. Return # of accepted bytes */
	      return wr_buff_no[dev];
	    }
	  wr_buff_ptr[dev] = 0;
	}

      l = c;
      if (l > (wr_buff_size[dev] - wr_buff_ptr[dev]))
	l = (wr_buff_size[dev] - wr_buff_ptr[dev]);

      if (!audio_devs[dev]->copy_from_user)
	{			/*
				 * No device specific copy routine
				 */
	  memcpy_fromfs (&wr_dma_buf[dev][wr_buff_ptr[dev]], &((buf)[p]), l);
	}
      else
	audio_devs[dev]->copy_from_user (dev,
			      wr_dma_buf[dev], wr_buff_ptr[dev], buf, p, l);


      /*
       * Insert local processing here
       */

      if (local_conversion[dev] == AFMT_MU_LAW)
	{
	  /*
	   * This just allows interrupts while the conversion is running
	   */
	  sti ();
	  translate_bytes (ulaw_dsp, (unsigned char *) &wr_dma_buf[dev][wr_buff_ptr[dev]], l);
	}

      c -= l;
      p += l;
      wr_buff_ptr[dev] += l;

      if (wr_buff_ptr[dev] >= wr_buff_size[dev])
	{
	  if ((err = DMAbuf_start_output (dev, wr_buff_no[dev], wr_buff_ptr[dev])) < 0)
	    {
	      return err;
	    }

	  wr_buff_no[dev] = -1;
	}

    }

  return count;
}

int
audio_read (int dev, struct fileinfo *file, snd_rw_buf * buf, int count)
{
  int             c, p, l;
  char           *dmabuf;
  int             buff_no;

  dev = dev >> 4;
  p = 0;
  c = count;

  if ((audio_mode[dev] & AM_WRITE) && !(audio_devs[dev]->flags & DMA_DUPLEX))
    {
      if (wr_buff_no[dev] >= 0)
	{
	  DMAbuf_start_output (dev, wr_buff_no[dev], wr_buff_ptr[dev]);

	  if (!(audio_devs[dev]->flags & DMA_DUPLEX))
	    wr_buff_no[dev] = -1;
	}
    }

  if (audio_devs[dev]->flags & DMA_DUPLEX)
    audio_mode[dev] |= AM_READ;
  else
    audio_mode[dev] = AM_READ;

  while (c)
    {
      if ((buff_no = DMAbuf_getrdbuffer (dev, &dmabuf, &l,
					 dev_nblock[dev])) < 0)
	{
	  /* Nonblocking mode handling. Return current # of bytes */

	  if (dev_nblock[dev] && buff_no == -EAGAIN)
	    return p;

	  return buff_no;
	}

      if (l > c)
	l = c;

      /*
       * Insert any local processing here.
       */

      if (local_conversion[dev] == AFMT_MU_LAW)
	{
	  /*
	   * This just allows interrupts while the conversion is running
	   */
	  sti ();

	  translate_bytes (dsp_ulaw, (unsigned char *) dmabuf, l);
	}

      memcpy_tofs (&((buf)[p]), dmabuf, l);

      DMAbuf_rmchars (dev, buff_no, l);

      p += l;
      c -= l;
    }

  return count - c;
}

int
audio_ioctl (int dev, struct fileinfo *file,
	     unsigned int cmd, ioctl_arg arg)
{

  dev = dev >> 4;

  if (((cmd >> 8) & 0xff) == 'C')
    {
      if (audio_devs[dev]->coproc)	/* Coprocessor ioctl */
	return audio_devs[dev]->coproc->ioctl (audio_devs[dev]->coproc->devc, cmd, arg, 0);
      else
	printk ("/dev/dsp%d: No coprocessor for this device\n", dev);

      return -ENXIO;
    }
  else
    switch (cmd)
      {
      case SNDCTL_DSP_SYNC:
	if (wr_buff_no[dev] >= 0)
	  {
	    DMAbuf_start_output (dev, wr_buff_no[dev], wr_buff_ptr[dev]);

	    wr_buff_no[dev] = -1;
	  }
	return DMAbuf_ioctl (dev, cmd, arg, 0);
	break;

      case SNDCTL_DSP_POST:
	if (wr_buff_no[dev] >= 0)
	  {
	    DMAbuf_start_output (dev, wr_buff_no[dev], wr_buff_ptr[dev]);

	    wr_buff_no[dev] = -1;
	  }
	return 0;
	break;

      case SNDCTL_DSP_RESET:
	wr_buff_no[dev] = -1;
	audio_mode[dev] = AM_NONE;
	return DMAbuf_ioctl (dev, cmd, arg, 0);
	break;

      case SNDCTL_DSP_GETFMTS:
	return snd_ioctl_return ((int *) arg, audio_devs[dev]->format_mask);
	break;

      case SNDCTL_DSP_SETFMT:
	return snd_ioctl_return ((int *) arg, set_format (dev, get_fs_long ((long *) arg)));

      case SNDCTL_DSP_GETISPACE:
	if ((audio_mode[dev] & AM_WRITE) && !(audio_devs[dev]->flags & DMA_DUPLEX))
	  return -EBUSY;

	{
	  audio_buf_info  info;

	  int             err = DMAbuf_ioctl (dev, cmd, (ioctl_arg) & info, 1);

	  if (err < 0)
	    return err;

	  memcpy_tofs ((&((char *) arg)[0]), (char *) &info, sizeof (info));
	  return 0;
	}

      case SNDCTL_DSP_GETOSPACE:
	if ((audio_mode[dev] & AM_READ) && !(audio_devs[dev]->flags & DMA_DUPLEX))
	  return -EBUSY;

	{
	  audio_buf_info  info;

	  int             err = DMAbuf_ioctl (dev, cmd, (ioctl_arg) & info, 1);

	  if (err < 0)
	    return err;

	  if (wr_buff_no[dev] != -1)
	    info.bytes += wr_buff_size[dev] - wr_buff_ptr[dev];

	  memcpy_tofs ((&((char *) arg)[0]), (char *) &info, sizeof (info));
	  return 0;
	}

      case SNDCTL_DSP_NONBLOCK:
	dev_nblock[dev] = 1;
	return 0;
	break;

      case SNDCTL_DSP_GETCAPS:
	{
	  int             info = 1;	/* Revision level of this ioctl() */

	  if (audio_devs[dev]->flags & DMA_DUPLEX)
	    info |= DSP_CAP_DUPLEX;

	  if (audio_devs[dev]->coproc)
	    info |= DSP_CAP_COPROC;

	  if (audio_devs[dev]->local_qlen)	/* Device has hidden buffers */
	    info |= DSP_CAP_BATCH;

	  if (audio_devs[dev]->trigger)		/* Supports SETTRIGGER */
	    info |= DSP_CAP_TRIGGER;

	  memcpy_tofs ((&((char *) arg)[0]), (char *) &info, sizeof (info));
	  return 0;
	}
	break;

      default:
	return DMAbuf_ioctl (dev, cmd, arg, 0);
      }
}

long
audio_init (long mem_start)
{
  /*
     * NOTE! This routine could be called several times during boot.
   */
  return mem_start;
}

int
audio_select (int dev, struct fileinfo *file, int sel_type, select_table * wait)
{

  dev = dev >> 4;

  switch (sel_type)
    {
    case SEL_IN:
      if (!(audio_mode[dev] & AM_READ) && !(audio_devs[dev]->flags & DMA_DUPLEX))
	return 0;		/* Not recording */


      return DMAbuf_select (dev, file, sel_type, wait);
      break;

    case SEL_OUT:
      if (!(audio_mode[dev] & AM_WRITE) && !(audio_devs[dev]->flags & DMA_DUPLEX))
	return 0;		/* Wrong direction */

      if (wr_buff_no[dev] != -1)
	{
	  return 1;		/* There is space in the current buffer */
	}

      return DMAbuf_select (dev, file, sel_type, wait);
      break;

    case SEL_EX:
      return 0;
    }

  return 0;
}


#endif
#endif
