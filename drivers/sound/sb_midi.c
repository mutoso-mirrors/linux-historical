/*
 * sound/sb_dsp.c
 *
 * The low level driver for the Sound Blaster DS chips.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */

#include <linux/config.h>
#include "sound_config.h"

#ifdef CONFIG_SBDSP
#ifdef CONFIG_MIDI

#include "sb.h"
#undef SB_TEST_IRQ

/*
 * The DSP channel can be used either for input or output. Variable
 * 'sb_irq_mode' will be set when the program calls read or write first time
 * after open. Current version doesn't support mode changes without closing
 * and reopening the device. Support for this feature may be implemented in a
 * future version of this driver.
 */


static int sb_midi_open(int dev, int mode,
	     void            (*input) (int dev, unsigned char data),
	     void            (*output) (int dev)
)
{
	sb_devc *devc = midi_devs[dev]->devc;
	unsigned long flags;

	if (devc == NULL)
		return -ENXIO;

	save_flags(flags);
	cli();
	if (devc->opened)
	{
		restore_flags(flags);
		return -EBUSY;
	}
	devc->opened = 1;
	restore_flags(flags);

	devc->irq_mode = IMODE_MIDI;
	devc->midi_broken = 0;

	sb_dsp_reset(devc);

	if (!sb_dsp_command(devc, 0x35))	/* Start MIDI UART mode */
	{
		  devc->opened = 0;
		  return -EIO;
	}
	devc->intr_active = 1;

	if (mode & OPEN_READ)
	{
		devc->input_opened = 1;
		devc->midi_input_intr = input;
	}
	return 0;
}

static void sb_midi_close(int dev)
{
	sb_devc *devc = midi_devs[dev]->devc;
	unsigned long flags;

	if (devc == NULL)
		return;

	save_flags(flags);
	cli();
	sb_dsp_reset(devc);
	devc->intr_active = 0;
	devc->input_opened = 0;
	devc->opened = 0;
	restore_flags(flags);
}

static int sb_midi_out(int dev, unsigned char midi_byte)
{
	sb_devc *devc = midi_devs[dev]->devc;

	if (devc == NULL)
		return 1;

	if (devc->midi_broken)
		return 1;

	if (!sb_dsp_command(devc, midi_byte))
	{
		devc->midi_broken = 1;
		return 1;
	}
	return 1;
}

static int sb_midi_start_read(int dev)
{
	return 0;
}

static int sb_midi_end_read(int dev)
{
	sb_devc *devc = midi_devs[dev]->devc;

	if (devc == NULL)
		return -ENXIO;

	sb_dsp_reset(devc);
	devc->intr_active = 0;
	return 0;
}

static int sb_midi_ioctl(int dev, unsigned cmd, caddr_t arg)
{
        return -EINVAL;
}

void sb_midi_interrupt(sb_devc * devc)
{
	unsigned long   flags;
	unsigned char   data;

	if (devc == NULL)
		return;

	save_flags(flags);
	cli();

	data = inb(DSP_READ);
	if (devc->input_opened)
		devc->midi_input_intr(devc->my_mididev, data);

	restore_flags(flags);
}

#define MIDI_SYNTH_NAME	"Sound Blaster Midi"
#define MIDI_SYNTH_CAPS	0
#include "midi_synth.h"

static struct midi_operations sb_midi_operations =
{
	{
		"Sound Blaster", 0, 0, SNDCARD_SB
	},
	&std_midi_synth,
	{0},
	sb_midi_open,
	sb_midi_close,
	sb_midi_ioctl,
	sb_midi_out,
	sb_midi_start_read,
	sb_midi_end_read,
	NULL,
	NULL,
	NULL,
	NULL
};

void sb_dsp_midi_init(sb_devc * devc)
{
	int dev;

	if (devc->model < 2)	/* No MIDI support for SB 1.x */
		return;

	dev = sound_alloc_mididev();

	if (dev == -1)
	{
		printk(KERN_ERR "sb_midi: Too many midi devices detected\n");
		return;
	}
	std_midi_synth.midi_dev = dev;
	devc->my_mididev = dev;
	std_midi_synth.midi_dev = devc->my_mididev = dev;
	midi_devs[dev] = (struct midi_operations *)kmalloc(sizeof(struct midi_operations), GFP_KERNEL);
	if (midi_devs[dev] == NULL)
	{
		printk(KERN_WARNING "soundblaster: Failed to allocate MIDI memory.\n");
		sound_unload_mididev(dev);
		  return;
	}
	memcpy((char *) midi_devs[dev], (char *) &sb_midi_operations,
	       sizeof(struct midi_operations));

	midi_devs[dev]->devc = devc;


	midi_devs[dev]->converter = (struct synth_operations *)kmalloc(sizeof(struct synth_operations), GFP_KERNEL);
	if (midi_devs[dev]->converter == NULL)
	{
		  printk(KERN_WARNING "soundblaster: Failed to allocate MIDI memory.\n");
		  kfree(midi_devs[dev]);
		  sound_unload_mididev(dev);
		  return;
	}
	memcpy((char *) midi_devs[dev]->converter, (char *) &std_midi_synth,
	       sizeof(struct synth_operations));

	midi_devs[dev]->converter->id = "SBMIDI";
	sequencer_init();
}

#endif
#endif
