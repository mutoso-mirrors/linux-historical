#define DISABLED_OPTIONS 	(B(OPT_PNP)|B(OPT_AEDSP16))
/*
 * sound/configure.c  - Configuration program for the Linux Sound Driver
 *
 * Copyright by Hannu Savolainen 1993-1995
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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define B(x)	(1 << (x))

/*
 * Option numbers
 */

#define OPT_PAS		0
#define OPT_SB		1
#define OPT_ADLIB	2
#define OPT_LAST_MUTUAL	2

#define OPT_GUS		3
#define OPT_MPU401	4
#define OPT_UART6850	5
#define OPT_PSS		6
#define OPT_GUS16	7
#define OPT_GUSMAX	8
#define OPT_MSS		9
#define OPT_SSCAPE	10
#define OPT_TRIX	11
#define OPT_MAD16	12
#define OPT_CS4232	13
#define OPT_MAUI	14
#define OPT_PNP		15

#define OPT_HIGHLEVEL   16	/* This must be same than the next one */
#define OPT_SBPRO	16
#define OPT_SB16	17
#define OPT_AEDSP16     18
#define OPT_AUDIO	19
#define OPT_MIDI_AUTO	20
#define OPT_MIDI	21
#define OPT_YM3812_AUTO	22
#define OPT_YM3812	23
#define OPT_SEQUENCER	24
#define OPT_LAST	24	/* Last defined OPT number */

#define DUMMY_OPTS (B(OPT_MIDI_AUTO)|B(OPT_YM3812_AUTO))

#define ANY_DEVS (B(OPT_AUDIO)|B(OPT_MIDI)|B(OPT_SEQUENCER)|B(OPT_GUS)| \
		  B(OPT_MPU401)|B(OPT_PSS)|B(OPT_GUS16)|B(OPT_GUSMAX)| \
		  B(OPT_MSS)|B(OPT_SSCAPE)|B(OPT_UART6850)|B(OPT_TRIX)| \
		  B(OPT_MAD16)|B(OPT_CS4232)|B(OPT_MAUI))
#define AUDIO_CARDS (B (OPT_PSS) | B (OPT_SB) | B (OPT_PAS) | B (OPT_GUS) | \
		B (OPT_MSS) | B (OPT_GUS16) | B (OPT_GUSMAX) | B (OPT_TRIX) | \
		B (OPT_SSCAPE)| B(OPT_MAD16) | B(OPT_CS4232))
#define MIDI_CARDS (B (OPT_PSS) | B (OPT_SB) | B (OPT_PAS) | B (OPT_MPU401) | \
		    B (OPT_GUS) | B (OPT_TRIX) | B (OPT_SSCAPE)|B(OPT_MAD16) | \
		    B (OPT_CS4232)|B(OPT_MAUI))
#define MPU_DEVS (B(OPT_PSS)|B(OPT_SSCAPE)|B(OPT_TRIX)|B(OPT_MAD16)|\
		  B(OPT_CS4232)|B(OPT_PNP)|B(OPT_MAUI))
#define AD1848_DEVS (B(OPT_GUS16)|B(OPT_MSS)|B(OPT_PSS)|B(OPT_GUSMAX)|\
		     B(OPT_SSCAPE)|B(OPT_TRIX)|B(OPT_MAD16)|B(OPT_CS4232)|\
		     B(OPT_PNP))
/*
 * Options that have been disabled for some reason (incompletely implemented
 * and/or tested). Don't remove from this list before looking at file
 * experimental.txt for further info.
 */

typedef struct
  {
    unsigned long   conditions;
    unsigned long   exclusive_options;
    char            macro[20];
    int             verify;
    int             alias;
    int             default_answ;
  }

hw_entry;


/*
 * The rule table for the driver options. The first field defines a set of
 * options which must be selected before this entry can be selected. The
 * second field is a set of options which are not allowed with this one. If
 * the fourth field is zero, the option is selected without asking
 * confirmation from the user.
 *
 * With this version of the rule table it is possible to select just one type of
 * hardware.
 *
 * NOTE!        Keep the following table and the questions array in sync with the
 * option numbering!
 */

hw_entry        hw_table[] =
{
/*
 * 0
 */
  {0, 0, "PAS", 1, 0, 0},
  {0, 0, "SB", 1, 0, 0},
  {0, B (OPT_PAS) | B (OPT_SB), "ADLIB", 1, 0, 0},

  {0, 0, "GUS", 1, 0, 0},
  {0, 0, "MPU401", 1, 0, 0},
  {0, 0, "UART6850", 1, 0, 0},
  {0, 0, "PSS", 1, 0, 0},
  {B (OPT_GUS), 0, "GUS16", 1, 0, 0},
  {B (OPT_GUS), B (OPT_GUS16), "GUSMAX", 1, 0, 0},
  {0, 0, "MSS", 1, 0, 0},
  {0, 0, "SSCAPE", 1, 0, 0},
  {0, 0, "TRIX", 1, 0, 0},
  {0, 0, "MAD16", 1, 0, 0},
  {0, 0, "CS4232", 1, 0, 0},
  {0, 0, "MAUI", 1, 0, 0},
  {0, 0, "PNP", 1, 0, 0},

  {B (OPT_SB), B (OPT_PAS), "SBPRO", 1, 0, 1},
  {B (OPT_SB) | B (OPT_SBPRO), B (OPT_PAS), "SB16", 1, 0, 1},
  {B (OPT_SBPRO) | B (OPT_MSS) | B (OPT_MPU401), 0, "AEDSP16", 1, 0, 0},
  {AUDIO_CARDS, 0, "AUDIO", 1, 0, 1},
  {B (OPT_MPU401) | B (OPT_MAUI), 0, "MIDI_AUTO", 0, OPT_MIDI, 0},
  {MIDI_CARDS, 0, "MIDI", 1, 0, 1},
  {B (OPT_ADLIB), 0, "YM3812_AUTO", 0, OPT_YM3812, 0},
  {B (OPT_PSS) | B (OPT_SB) | B (OPT_PAS) | B (OPT_ADLIB) | B (OPT_MSS) | B (OPT_PSS), B (OPT_YM3812_AUTO), "YM3812", 1, 0, 1},
  {B (OPT_MIDI) | B (OPT_YM3812) | B (OPT_YM3812_AUTO) | B (OPT_GUS), 0, "SEQUENCER", 0, 0, 1}
};

char           *questions[] =
{
  "ProAudioSpectrum 16 support",
  "SoundBlaster support",
  "Generic OPL2/OPL3 FM synthesizer support",
  "Gravis Ultrasound support",
  "MPU-401 support (NOT for SB16)",
  "6850 UART Midi support",
  "PSS (ECHO-ADI2111) support",
  "16 bit sampling option of GUS (_NOT_ GUS MAX)",
  "GUS MAX support",
  "Microsoft Sound System support",
  "Ensoniq Soundscape support",
  "MediaTriX AudioTriX Pro support",
  "Support for MAD16 and/or Mozart based cards",
  "Support for Crystal CS4232 based (PnP) cards",
  "Support for Turtle Beach Wave Front (Maui, Tropez) synthesizers",
  "Support for PnP soundcards (_EXPERIMENTAL_)",

  "SoundBlaster Pro support",
  "SoundBlaster 16 support",
  "Audio Excel DSP 16 initialization support",
  "/dev/dsp and /dev/audio supports (usually required)",
  "This should not be asked",
  "MIDI interface support",
  "This should not be asked",
  "FM synthesizer (YM3812/OPL-3) support",
  "/dev/sequencer support",
  "Is the sky really falling"
};

struct kludge
  {
    char           *name;
    int             mask;
  }
extra_options[] =
{
  {
    "MPU_EMU", MPU_DEVS
  }
  ,
  {
    "AD1848", AD1848_DEVS
  }
  ,
  {
    NULL, 0
  }
};

char           *oldconf = "/etc/soundconf";

int             old_config_used = 0;
int             def_size, sb_base = 0;

unsigned long   selected_options = 0;
int             sb_dma = 0;

int             dump_only = 0;

void            build_defines (void);

#include "hex2hex.h"
int             bin2hex (char *path, char *target, char *varname);

int
can_select_option (int nr)
{

  if (hw_table[nr].conditions)
    if (!(hw_table[nr].conditions & selected_options))
      return 0;

  if (hw_table[nr].exclusive_options)
    if (hw_table[nr].exclusive_options & selected_options)
      return 0;

  if (DISABLED_OPTIONS & B (nr))
    return 0;

  return 1;
}

int
think_positively (int def_answ)
{
  char            answ[512];
  int             len;

  if ((len = read (0, answ, sizeof (answ))) < 1)
    {
      fprintf (stderr, "\n\nERROR! Cannot read stdin\n");

      perror ("stdin");
      printf ("invalid_configuration__run_make_config_again\n");
      exit (-1);
    }

  if (len < 2)			/*
				 * There is an additional LF at the end
				 */
    return def_answ;

  answ[len - 1] = 0;

  if (!strcmp (answ, "y") || !strcmp (answ, "Y"))
    return 1;

  return 0;
}

int
ask_value (char *format, int default_answer)
{
  char            answ[512];
  int             len, num;

play_it_again_Sam:

  if ((len = read (0, answ, sizeof (answ))) < 1)
    {
      fprintf (stderr, "\n\nERROR! Cannot read stdin\n");

      perror ("stdin");
      printf ("invalid_configuration__run_make_config_again\n");
      exit (-1);
    }

  if (len < 2)			/*
				 * There is an additional LF at the end
				 */
    return default_answer;

  answ[len - 1] = 0;

  if (sscanf (answ, format, &num) != 1)
    {
      fprintf (stderr, "Illegal format. Try again: ");
      goto play_it_again_Sam;
    }

  return num;
}

#define FMT_HEX 1
#define FMT_INT 2

void
ask_int_choice (int mask, char *macro,
		char *question,
		int format,
		int defa,
		char *choices)
{
  int             num, i;

  if (dump_only)
    {

      for (i = 0; i < OPT_LAST; i++)
	if (mask == B (i))
	  {
	    int             j;

	    for (j = 0; j < strlen (choices); j++)
	      if (choices[j] == '\'')
		choices[j] = '_';

	    printf ("\nif [ \"$CONFIG_%s\" = \"y\" ]; then\n",
		    hw_table[i].macro);
	    if (format == FMT_INT)
	      printf ("int '%s %s' %s %d\n", question, choices, macro, defa);
	    else
	      printf ("hex '%s %s' %s %x\n", question, choices, macro, defa);
	    printf ("fi\n");
	  }
    }
  else
    {
      if (!(mask & selected_options))
	return;

      fprintf (stderr, "\n%s\n", question);
      fprintf (stderr, "Possible values are: %s\n", choices);

      if (format == FMT_INT)
	{
	  if (defa == -1)
	    fprintf (stderr, "\t(-1 disables this feature)\n");
	  fprintf (stderr, "The default value is %d\n", defa);
	  fprintf (stderr, "Enter the value: ");
	  num = ask_value ("%d", defa);
	  if (num == -1)
	    return;
	  fprintf (stderr, "%s set to %d.\n", question, num);
	  printf ("#define %s %d\n", macro, num);
	}
      else
	{
	  if (defa == 0)
	    fprintf (stderr, "\t(0 disables this feature)\n");
	  fprintf (stderr, "The default value is %x\n", defa);
	  fprintf (stderr, "Enter the value: ");
	  num = ask_value ("%x", defa);
	  if (num == 0)
	    return;
	  fprintf (stderr, "%s set to %x.\n", question, num);
	  printf ("#define %s 0x%x\n", macro, num);
	}
    }
}

void
rebuild_file (char *line)
{
  char           *method, *new, *old, *var, *p;

  method = p = line;

  while (*p && *p != ' ')
    p++;
  *p++ = 0;

  old = p;
  while (*p && *p != ' ')
    p++;
  *p++ = 0;

  new = p;
  while (*p && *p != ' ')
    p++;
  *p++ = 0;

  var = p;
  while (*p && *p != ' ')
    p++;
  *p++ = 0;

  fprintf (stderr, "Rebuilding file %s (%s %s)\n", new, method, old);

  if (strcmp (method, "bin2hex") == 0)
    {
      if (!bin2hex (old, new, var))
	{
	  fprintf (stderr, "Rebuild failed\n");
	  exit (-1);
	}
    }
  else if (strcmp (method, "hex2hex") == 0)
    {
      if (!hex2hex (old, new, var))
	{
	  fprintf (stderr, "Rebuild failed\n");
	  exit (-1);
	}
    }
  else
    {
      fprintf (stderr, "Failed to build '%s' - unknown method %s\n",
	       new, method);
      exit (-1);
    }
}

int
use_old_config (char *filename)
{
  char            buf[1024];
  int             i = 0;

  FILE           *oldf;

  fprintf (stderr, "Copying old configuration from %s\n", filename);

  if ((oldf = fopen (filename, "r")) == NULL)
    {
      fprintf (stderr, "Couldn't open previous configuration file\n");
      perror (filename);
      return 0;
    }

  while (fgets (buf, 1024, oldf) != NULL)
    {
      char            tmp[100];

      if (buf[0] != '#')
	{
	  printf ("%s", buf);

	  strncpy (tmp, buf, 8);
	  tmp[8] = 0;

	  if (strcmp (tmp, "/*build ") == 0)
	    rebuild_file (&buf[8]);

	  continue;
	}

      strncpy (tmp, buf, 8);
      tmp[8] = 0;

      if (strcmp (tmp, "#define ") == 0)
	{
	  char           *id = &buf[8];

	  i = 0;
	  while (id[i] && id[i] != ' ' &&
		 id[i] != '\t' && id[i] != '\n')
	    i++;

	  strncpy (tmp, id, i);
	  tmp[i] = 0;

	  if (strcmp (tmp, "SELECTED_SOUND_OPTIONS") == 0)
	    continue;

	  if (strcmp (tmp, "KERNEL_SOUNDCARD") == 0)
	    continue;

	  tmp[8] = 0;		/* Truncate the string */
	  if (strcmp (tmp, "EXCLUDE_") == 0)
	    continue;		/* Skip excludes */

	  strncpy (tmp, id, i);
	  tmp[7] = 0;		/* Truncate the string */

	  if (strcmp (tmp, "CONFIG_") == 0)
	    {
	      strncpy (tmp, &id[7], i - 7);
	      tmp[i - 7] = 0;

	      for (i = 0; i <= OPT_LAST; i++)
		if (strcmp (hw_table[i].macro, tmp) == 0)
		  {
		    selected_options |= (1 << i);
		    break;
		  }
	      continue;
	    }

	  printf ("%s", buf);
	  continue;
	}

      if (strcmp (tmp, "#undef  ") == 0)
	{
	  char           *id = &buf[8];

	  i = 0;
	  while (id[i] && id[i] != ' ' &&
		 id[i] != '\t' && id[i] != '\n')
	    i++;

	  strncpy (tmp, id, i);
	  tmp[7] = 0;		/* Truncate the string */
	  if (strcmp (tmp, "CONFIG_") == 0)
	    continue;

	  strncpy (tmp, id, i);

	  tmp[8] = 0;		/* Truncate the string */
	  if (strcmp (tmp, "EXCLUDE_") != 0)
	    continue;		/* Not a #undef  EXCLUDE_ line */
	  strncpy (tmp, &id[8], i - 8);
	  tmp[i - 8] = 0;

	  for (i = 0; i <= OPT_LAST; i++)
	    if (strcmp (hw_table[i].macro, tmp) == 0)
	      {
		selected_options |= (1 << i);
		break;
	      }
	  continue;
	}

      printf ("%s", buf);
    }
  fclose (oldf);

  for (i = 0; i <= OPT_LAST; i++)
    if (!hw_table[i].alias)
      if (selected_options & B (i))
	printf ("#define CONFIG_%s\n", hw_table[i].macro);
      else
	printf ("#undef  CONFIG_%s\n", hw_table[i].macro);


  printf ("\n");

  i = 0;

  while (extra_options[i].name != NULL)
    {
      if (selected_options & extra_options[i].mask)
	printf ("#define CONFIG_%s\n", extra_options[i].name);
      else
	printf ("#undef  CONFIG_%s\n", extra_options[i].name);
      i++;
    }

  printf ("\n");

  printf ("#define SELECTED_SOUND_OPTIONS\t0x%08x\n", selected_options);
  fprintf (stderr, "Old configuration copied.\n");

  build_defines ();
  old_config_used = 1;
  return 1;
}

void
build_defines (void)
{
  FILE           *optf;
  int             i;

  if ((optf = fopen (".defines", "w")) == NULL)
    {
      perror (".defines");
      exit (-1);
    }


  for (i = 0; i <= OPT_LAST; i++)
    if (!hw_table[i].alias)
      if (selected_options & B (i))
	fprintf (optf, "CONFIG_%s=y\n", hw_table[i].macro);


  fprintf (optf, "\n");

  i = 0;

  while (extra_options[i].name != NULL)
    {
      if (selected_options & extra_options[i].mask)
	fprintf (optf, "CONFIG_%s=y\n", extra_options[i].name);
      i++;
    }

  fprintf (optf, "\n");
  fclose (optf);
}

void
ask_parameters (void)
{
  int             num;

  build_defines ();
  /*
   * IRQ and DMA settings
   */

  ask_int_choice (B (OPT_AEDSP16), "AEDSP16_BASE",
		  "I/O base for Audio Excel DSP 16",
		  FMT_HEX,
		  0x220,
		  "220 or 240");

  ask_int_choice (B (OPT_SB), "SBC_BASE",
		  "I/O base for SB",
		  FMT_HEX,
		  0x220,
		  "");

  ask_int_choice (B (OPT_SB), "SBC_IRQ",
		  "SoundBlaster IRQ",
		  FMT_INT,
		  7,
		  "");

  ask_int_choice (B (OPT_SB), "SBC_DMA",
		  "SoundBlaster DMA",
		  FMT_INT,
		  1,
		  "");

  ask_int_choice (B (OPT_SB), "SB_DMA2",
		  "SoundBlaster 16 bit DMA (if required)",
		  FMT_INT,
		  -1,
		  "5, 6 or 7");

  ask_int_choice (B (OPT_SB), "SB_MPU_BASE",
		  "MPU401 I/O base of SB16, Jazz16 and ES1688",
		  FMT_HEX,
		  0,
		  "");

  ask_int_choice (B (OPT_SB), "SB_MPU_IRQ",
		  "SB MPU401 IRQ (SB16, Jazz16 and ES1688)",
		  FMT_INT,
		  -1,
		  "");

  ask_int_choice (B (OPT_PAS), "PAS_IRQ",
		  "PAS16 IRQ",
		  FMT_INT,
		  10,
		  "");

  ask_int_choice (B (OPT_PAS), "PAS_DMA",
		  "PAS16 DMA",
		  FMT_INT,
		  3,
		  "");

  if (selected_options & B (OPT_PAS))
    {
      fprintf (stderr, "\nEnable Joystick port on ProAudioSpectrum (n/y) ? ");
      if (think_positively (0))
	printf ("#define PAS_JOYSTICK_ENABLE\n");


      fprintf (stderr, "PAS16 could be noisy with some mother boards\n"
	       "There is a command line switch (was it :T?)\n"
	       "in the DOS driver for PAS16 which solves this.\n"
	       "Don't enable this feature unless you have problems!\n"
	       "Do you have to use this switch with DOS (y/n) ?");
      if (think_positively (0))
	printf ("#define BROKEN_BUS_CLOCK\n");
    }


  ask_int_choice (B (OPT_GUS), "GUS_BASE",
		  "I/O base for GUS",
		  FMT_HEX,
		  0x220,
		  "210, 220, 230, 240, 250 or 260");


  ask_int_choice (B (OPT_GUS), "GUS_IRQ",
		  "GUS IRQ",
		  FMT_INT,
		  15,
		  "");

  ask_int_choice (B (OPT_GUS), "GUS_DMA",
		  "GUS DMA",
		  FMT_INT,
		  6,
		  "");

  ask_int_choice (B (OPT_GUS), "GUS_DMA2",
		  "Second DMA channel for GUS",
		  FMT_INT,
		  -1,
		  "");

  ask_int_choice (B (OPT_GUS16), "GUS16_BASE",
		  "I/O base for the 16 bit daughtercard of GUS",
		  FMT_HEX,
		  0x530,
		  "530, 604, E80 or F40");


  ask_int_choice (B (OPT_GUS16), "GUS16_IRQ",
		  "GUS 16 bit daughtercard IRQ",
		  FMT_INT,
		  7,
		  "3, 4, 5, 7, or 9");

  ask_int_choice (B (OPT_GUS16), "GUS16_DMA",
		  "GUS DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_MPU401), "MPU_BASE",
		  "I/O base for MPU401",
		  FMT_HEX,
		  0x330,
		  "");

  ask_int_choice (B (OPT_MPU401), "MPU_IRQ",
		  "MPU401 IRQ",
		  FMT_INT,
		  9,
		  "");

  ask_int_choice (B (OPT_MAUI), "MAUI_BASE",
		  "I/O base for Maui",
		  FMT_HEX,
		  0x330,
		  "210, 230, 260, 290, 300, 320, 338 or 330");

  ask_int_choice (B (OPT_MAUI), "MAUI_IRQ",
		  "Maui IRQ",
		  FMT_INT,
		  9,
		  "5, 9, 12 or 15");

  ask_int_choice (B (OPT_UART6850), "U6850_BASE",
		  "I/O base for UART 6850 MIDI port",
		  FMT_HEX,
		  0,
		  "(Unknown)");

  ask_int_choice (B (OPT_UART6850), "U6850_IRQ",
		  "UART6850 IRQ",
		  FMT_INT,
		  -1,
		  "(Unknown)");

  ask_int_choice (B (OPT_PSS), "PSS_BASE",
		  "PSS I/O base",
		  FMT_HEX,
		  0x220,
		  "220 or 240");

  ask_int_choice (B (OPT_PSS), "PSS_MSS_BASE",
		  "PSS audio I/O base",
		  FMT_HEX,
		  0x530,
		  "530, 604, E80 or F40");

  ask_int_choice (B (OPT_PSS), "PSS_MSS_IRQ",
		  "PSS audio IRQ",
		  FMT_INT,
		  11,
		  "7, 9, 10 or 11");

  ask_int_choice (B (OPT_PSS), "PSS_MSS_DMA",
		  "PSS audio DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_PSS), "PSS_MPU_BASE",
		  "PSS MIDI I/O base",
		  FMT_HEX,
		  0x330,
		  "");

  ask_int_choice (B (OPT_PSS), "PSS_MPU_IRQ",
		  "PSS MIDI IRQ",
		  FMT_INT,
		  9,
		  "3, 4, 5, 7 or 9");

  ask_int_choice (B (OPT_MSS), "MSS_BASE",
		  "MSS/WSS I/O base",
		  FMT_HEX,
		  0x530,
		  "530, 604, E80 or F40");

  ask_int_choice (B (OPT_MSS), "MSS_IRQ",
		  "MSS/WSS IRQ",
		  FMT_INT,
		  11,
		  "7, 9, 10 or 11");

  ask_int_choice (B (OPT_MSS), "MSS_DMA",
		  "MSS/WSS DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_SSCAPE), "SSCAPE_BASE",
		  "Soundscape MIDI I/O base",
		  FMT_HEX,
		  0x330,
		  "");

  ask_int_choice (B (OPT_SSCAPE), "SSCAPE_IRQ",
		  "Soundscape MIDI IRQ",
		  FMT_INT,
		  9,
		  "");

  ask_int_choice (B (OPT_SSCAPE), "SSCAPE_DMA",
		  "Soundscape initialization DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_SSCAPE), "SSCAPE_MSS_BASE",
		  "Soundscape audio I/O base",
		  FMT_HEX,
		  0x534,
		  "534, 608, E84 or F44");

  ask_int_choice (B (OPT_SSCAPE), "SSCAPE_MSS_IRQ",
		  "Soundscape audio IRQ",
		  FMT_INT,
		  11,
		  "7, 9, 10 or 11");

  ask_int_choice (B (OPT_SSCAPE), "SSCAPE_MSS_DMA",
		  "Soundscape audio DMA",
		  FMT_INT,
		  0,
		  "0, 1 or 3");

  if (selected_options & B (OPT_SSCAPE))
    {
      int             reveal_spea;

      fprintf (stderr, "Is your SoundScape card made/marketed by Reveal or Spea? ");
      reveal_spea = think_positively (0);
      if (reveal_spea)
	printf ("#define REVEAL_SPEA\n");

    }

  ask_int_choice (B (OPT_TRIX), "TRIX_BASE",
		  "AudioTriX audio I/O base",
		  FMT_HEX,
		  0x530,
		  "530, 604, E80 or F40");

  ask_int_choice (B (OPT_TRIX), "TRIX_IRQ",
		  "AudioTriX audio IRQ",
		  FMT_INT,
		  11,
		  "7, 9, 10 or 11");

  ask_int_choice (B (OPT_TRIX), "TRIX_DMA",
		  "AudioTriX audio DMA",
		  FMT_INT,
		  0,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_TRIX), "TRIX_DMA2",
		  "AudioTriX second (duplex) DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_TRIX), "TRIX_MPU_BASE",
		  "AudioTriX MIDI I/O base",
		  FMT_HEX,
		  0x330,
		  "330, 370, 3B0 or 3F0");

  ask_int_choice (B (OPT_TRIX), "TRIX_MPU_IRQ",
		  "AudioTriX MIDI IRQ",
		  FMT_INT,
		  9,
		  "3, 4, 5, 7 or 9");

  ask_int_choice (B (OPT_TRIX), "TRIX_SB_BASE",
		  "AudioTriX SB I/O base",
		  FMT_HEX,
		  0x220,
		  "220, 210, 230, 240, 250, 260 or 270");

  ask_int_choice (B (OPT_TRIX), "TRIX_SB_IRQ",
		  "AudioTriX SB IRQ",
		  FMT_INT,
		  7,
		  "3, 4, 5 or 7");

  ask_int_choice (B (OPT_TRIX), "TRIX_SB_DMA",
		  "AudioTriX SB DMA",
		  FMT_INT,
		  1,
		  "1 or 3");

  ask_int_choice (B (OPT_CS4232), "CS4232_BASE",
		  "CS4232 audio I/O base",
		  FMT_HEX,
		  0x530,
		  "530, 604, E80 or F40");

  ask_int_choice (B (OPT_CS4232), "CS4232_IRQ",
		  "CS4232 audio IRQ",
		  FMT_INT,
		  11,
		  "5, 7, 9, 11, 12 or 15");

  ask_int_choice (B (OPT_CS4232), "CS4232_DMA",
		  "CS4232 audio DMA",
		  FMT_INT,
		  0,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_CS4232), "CS4232_DMA2",
		  "CS4232 second (duplex) DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_CS4232), "CS4232_MPU_BASE",
		  "CS4232 MIDI I/O base",
		  FMT_HEX,
		  0x330,
		  "330, 370, 3B0 or 3F0");

  ask_int_choice (B (OPT_CS4232), "CS4232_MPU_IRQ",
		  "CS4232 MIDI IRQ",
		  FMT_INT,
		  9,
		  "5, 7, 9, 11, 12 or 15");

  ask_int_choice (B (OPT_MAD16), "MAD16_BASE",
		  "MAD16 audio I/O base",
		  FMT_HEX,
		  0x530,
		  "530, 604, E80 or F40");

  ask_int_choice (B (OPT_MAD16), "MAD16_IRQ",
		  "MAD16 audio IRQ",
		  FMT_INT,
		  11,
		  "7, 9, 10 or 11");

  ask_int_choice (B (OPT_MAD16), "MAD16_DMA",
		  "MAD16 audio DMA",
		  FMT_INT,
		  3,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_MAD16), "MAD16_DMA2",
		  "MAD16 second (duplex) DMA",
		  FMT_INT,
		  0,
		  "0, 1 or 3");

  ask_int_choice (B (OPT_MAD16), "MAD16_MPU_BASE",
		  "MAD16 MIDI I/O base",
		  FMT_HEX,
		  0x330,
		  "300, 310, 320 or 330 (0 disables)");

  ask_int_choice (B (OPT_MAD16), "MAD16_MPU_IRQ",
		  "MAD16 MIDI IRQ",
		  FMT_INT,
		  9,
		  "5, 7, 9 or 10");
  ask_int_choice (B (OPT_AUDIO), "DSP_BUFFSIZE",
		  "Audio DMA buffer size",
		  FMT_INT,
		  65536,
		  "4096, 16384, 32768 or 65536");
}

void
dump_script (void)
{
  int             i;

  for (i = 0; i <= OPT_LAST; i++)
    if (!(DUMMY_OPTS & B (i)))
      if (!(DISABLED_OPTIONS & B (i)))
	{
	  printf ("bool '%s' CONFIG_%s\n", questions[i], hw_table[i].macro);
	}

  dump_only = 1;
  selected_options = 0;
  ask_parameters ();

  printf ("#\n$MAKE -C drivers/sound kernelconfig || exit 1\n");
}

void
dump_fixed_local (void)
{
  int             i = 0;

  printf ("/* Computer generated file. Please don't edit! */\n\n");
  printf ("#define KERNEL_COMPATIBLE_CONFIG\n\n");
  printf ("#define SELECTED_SOUND_OPTIONS\t0x%08x\n\n", selected_options);

  while (extra_options[i].name != NULL)
    {
      int             n = 0, j;

      printf ("#if ");

      for (j = 0; j < OPT_LAST; j++)
	if (!(DISABLED_OPTIONS & B (j)))
	  if (extra_options[i].mask & B (j))
	    {
	      if (n)
		printf (" || ");
	      if (!(n++ % 2))
		printf ("\\\n  ");

	      printf ("defined(CONFIG_%s)", hw_table[j].macro);
	    }

      printf ("\n");
      printf ("#\tdefine CONFIG_%s\n", extra_options[i].name);
      printf ("#endif\n\n");
      i++;
    }
}

void
dump_fixed_defines (void)
{
  int             i = 0;

  printf ("# Computer generated file. Please don't edit\n\n");

  while (extra_options[i].name != NULL)
    {
      int             n = 0, j;

      for (j = 0; j < OPT_LAST; j++)
	if (!(DISABLED_OPTIONS & B (j)))
	  if (extra_options[i].mask & B (j))
	    {
	      printf ("ifdef CONFIG_%s\n", hw_table[j].macro);
	      printf ("CONFIG_%s=y\n", extra_options[i].name);
	      printf ("endif\n\n");
	    }

      i++;
    }
}

int
main (int argc, char *argv[])
{
  int             i, num, full_driver = 1;
  char            answ[10];
  char            old_config_file[200];

  if (getuid () != 0)		/* Not root */
    {
      char           *home;

      if ((home = getenv ("HOME")) != NULL)
	{
	  sprintf (old_config_file, "%s/.soundconf", home);
	  oldconf = old_config_file;
	}
    }

  if (argc > 1)
    {
      if (strcmp (argv[1], "-o") == 0 &&
	  use_old_config (oldconf))
	exit (0);
      else if (strcmp (argv[1], "script") == 0)
	{
	  dump_script ();
	  exit (0);
	}
      else if (strcmp (argv[1], "fixedlocal") == 0)
	{
	  dump_fixed_local ();
	  exit (0);
	}
      else if (strcmp (argv[1], "fixeddefines") == 0)
	{
	  dump_fixed_defines ();
	  exit (0);
	}
    }

  fprintf (stderr, "\nConfiguring the sound support\n\n");

  if (access (oldconf, R_OK) == 0)
    {
      fprintf (stderr, "Old configuration exists in %s. Use it (y/n) ? ",
	       oldconf);
      if (think_positively (1))
	if (use_old_config (oldconf))
	  exit (0);

    }

  printf ("/*\tGenerated by configure. Don't edit!!!!\t*/\n");
  printf ("/*\tMaking changes to this file is not as simple as it may look.\t*/\n\n");
  printf ("/*\tIf you change the CONFIG_ settings in local.h you\t*/\n");
  printf ("/*\t_have_ to edit .defines too.\t*/\n\n");

  {
    /*
     * Partial driver
     */

    full_driver = 0;

    for (i = 0; i <= OPT_LAST; i++)
      if (can_select_option (i))
	{
	  if (!(selected_options & B (i)))	/*
						 * Not selected yet
						 */
	    if (!hw_table[i].verify)
	      {
		if (hw_table[i].alias)
		  selected_options |= B (hw_table[i].alias);
		else
		  selected_options |= B (i);
	      }
	    else
	      {
		int             def_answ = hw_table[i].default_answ;

		fprintf (stderr,
			 def_answ ? "  %s (y/n) ? " : "  %s (n/y) ? ",
			 questions[i]);
		if (think_positively (def_answ))
		  if (hw_table[i].alias)
		    selected_options |= B (hw_table[i].alias);
		  else
		    selected_options |= B (i);
	      }
	}
  }

  if (selected_options & B (OPT_SBPRO))
    {
      fprintf (stderr, "Do you want support for the mixer of SG NX Pro ? ");
      if (think_positively (0))
	printf ("#define __SGNXPRO__\n");
    }

  if (selected_options & B (OPT_SB))
    {
      fprintf (stderr, "Do you want support for the MV Jazz16 (ProSonic etc.) ? ");
      if (think_positively (0))
	{
	  fprintf (stderr, "Do you have SoundMan Wave (n/y) ? ");

	  if (think_positively (0))
	    {
	      printf ("#define SM_WAVE\n");

	    midi0001_again:
	      fprintf
		(stderr,
		 "Logitech SoundMan Wave has a microcontroller which must be initialized\n"
		 "before MIDI emulation works. This is possible only if the microcode\n"
		 "file is compiled into the driver.\n"
		 "Do you have access to the MIDI0001.BIN file (y/n) ? ");
	      if (think_positively (1))
		{
		  char            path[512];

		  fprintf (stderr,
			   "Enter full name of the MIDI0001.BIN file (pwd is sound): ");
		  scanf ("%s", path);
		  fprintf (stderr, "including microcode file %s\n", path);

		  if (!bin2hex (path, "smw-midi0001.h", "smw_ucode"))
		    {
		      fprintf (stderr, "couldn't open %s file\n",
			       path);
		      fprintf (stderr, "try again with correct path? ");
		      if (think_positively (1))
			goto midi0001_again;
		    }
		  else
		    {
		      printf ("#define SMW_MIDI0001_INCLUDED\n");
		      printf ("/*build bin2hex %s smw-midi0001.h smw_ucode */\n", path);
		    }
		}
	    }
	}
    }

  if (selected_options & B (OPT_SBPRO))
    {
      fprintf (stderr, "\n\nThe Logitech SoundMan Games supports 44 kHz in stereo\n"
	       "while the standard SB Pro supports just 22 kHz/stereo\n"
	       "You have an option to enable the SM Games mode.\n"
	       "However do enable it only if you are _sure_ that your\n"
	       "card is a SM Games. Enabling this feature with a\n"
	       "plain old SB Pro _will_ cause troubles with stereo mode.\n"
	       "\n"
	       "DANGER! Read the above once again before answering 'y'\n"
	       "Answer 'n' in case you are unsure what to do!\n");
      fprintf (stderr, "Do you have a Logitech SoundMan Games (n/y) ? ");
      if (think_positively (0))
	printf ("#define SM_GAMES\n");
    }

  if (selected_options & B (OPT_SB16))
    selected_options |= B (OPT_SBPRO);

  if (selected_options & B (OPT_AEDSP16))
    {
      int             sel1 = 0;

      if (selected_options & B (OPT_SBPRO))
	{
	  fprintf (stderr, "Do you want support for the Audio Excel SoundBlaster pro mode ? ");
	  if (think_positively (1))
	    {
	      printf ("#define AEDSP16_SBPRO\n");
	      sel1 = 1;
	    }
	}

      if ((selected_options & B (OPT_MSS)) && (sel1 == 0))
	{
	  fprintf (stderr, "Do you want support for the Audio Excel Microsoft Sound System mode? ");
	  if (think_positively (1))
	    {
	      printf ("#define AEDSP16_MSS\n");
	      sel1 = 1;
	    }
	}

      if (sel1 == 0)
	{
	  printf ("invalid_configuration__run_make_config_again\n");
	  fprintf (stderr, "ERROR!!!!!\nYou must select at least one mode when using Audio Excel!\n");
	  exit (-1);
	}
      if (selected_options & B (OPT_MPU401))
	printf ("#define AEDSP16_MPU401\n");
    }

  if (selected_options & B (OPT_PSS))
    {
    genld_again:
      fprintf
	(stderr,
       "if you wish to emulate the soundblaster and you have a DSPxxx.LD.\n"
	 "then you must include the LD in the kernel.\n"
	 "Do you wish to include a LD (y/n) ? ");
      if (think_positively (1))
	{
	  char            path[512];

	  fprintf (stderr,
		   "Enter the path to your LD file (pwd is sound): ");
	  scanf ("%s", path);
	  fprintf (stderr, "including LD file %s\n", path);

	  if (!bin2hex (path, "synth-ld.h", "pss_synth"))
	    {
	      fprintf (stderr, "couldn't open %s as the ld file\n",
		       path);
	      fprintf (stderr, "try again with correct path? ");
	      if (think_positively (1))
		goto genld_again;
	    }
	  else
	    {
	      printf ("#define PSS_HAVE_LD\n");
	      printf ("/*build bin2hex %s synth-ld.h pss_synth */\n", path);
	    }
	}
      else
	{
	  FILE           *sf = fopen ("synth-ld.h", "w");

	  fprintf (sf, "/* automaticaly generated by configure */\n");
	  fprintf (sf, "unsigned char pss_synth[1];\n"
		   "#define pss_synthLen 0\n");
	  fclose (sf);
	}
    }

  if (selected_options & B (OPT_TRIX))
    {
    hex2hex_again:
      fprintf (stderr, "MediaTriX audioTriX Pro has a onboard microcontroller\n"
	       "which needs to be initialized by downloading\n"
	       "the code from file TRXPRO.HEX in the DOS driver\n"
	       "directory. If you don't have the TRXPRO.HEX handy\n"
	       "you may skip this step. However SB and MPU-401\n"
	       "modes of AudioTriX Pro will not work without\n"
	       "this file!\n"
	       "\n"
	       "Do you want to include TRXPRO.HEX in your kernel (y/n) ? ");

      if (think_positively (1))
	{
	  char            path[512];

	  fprintf (stderr,
		 "Enter the path to your TRXPRO.HEX file (pwd is sound): ");
	  scanf ("%s", path);
	  fprintf (stderr, "including HEX file %s\n", path);

	  if (!hex2hex (path, "trix_boot.h", "trix_boot"))
	    goto hex2hex_again;
	  printf ("/*build hex2hex %s trix_boot.h trix_boot */\n", path);
	  printf ("#define INCLUDE_TRIX_BOOT\n");
	}
    }

  if (!(selected_options & ANY_DEVS))
    {
      printf ("invalid_configuration__run_make_config_again\n");
      fprintf (stderr, "\n*** This combination is useless. Sound driver disabled!!! ***\n\n");
      exit (0);
    }

  for (i = 0; i <= OPT_LAST; i++)
    if (!hw_table[i].alias)
      if (selected_options & B (i))
	printf ("#define CONFIG_%s\n", hw_table[i].macro);
      else
	printf ("#undef  CONFIG_%s\n", hw_table[i].macro);


  printf ("\n");

  i = 0;

  while (extra_options[i].name != NULL)
    {
      if (selected_options & extra_options[i].mask)
	printf ("#define CONFIG_%s\n", extra_options[i].name);
      else
	printf ("#undef  CONFIG_%s\n", extra_options[i].name);
      i++;
    }

  printf ("\n");

  ask_parameters ();

  printf ("#define SELECTED_SOUND_OPTIONS\t0x%08x\n", selected_options);
  fprintf (stderr, "The sound driver is now configured.\n");

#if defined(SCO) || defined(ISC) || defined(SYSV)
  fprintf (stderr, "Remember to update the System file\n");
#endif

  if (!old_config_used)
    {
      fprintf (stderr, "Save copy of this configuration to %s (y/n)", oldconf);
      if (think_positively (1))
	{
	  char            cmd[200];

	  sprintf (cmd, "cp local.h %s", oldconf);

	  fclose (stdout);
	  if (system (cmd) != 0)
	    perror (cmd);
	}
    }
  exit (0);
}

int
bin2hex (char *path, char *target, char *varname)
{
  int             fd;
  int             count;
  char            c;
  int             i = 0;

  if ((fd = open (path, 0)) > 0)
    {
      FILE           *sf = fopen (target, "w");

      fprintf (sf, "/* automaticaly generated by configure */\n");
      fprintf (sf, "static unsigned char %s[] = {\n", varname);
      while (1)
	{
	  count = read (fd, &c, 1);
	  if (count == 0)
	    break;
	  if (i != 0 && (i % 10) == 0)
	    fprintf (sf, "\n");
	  fprintf (sf, "0x%02x,", c & 0xFFL);
	  i++;
	}
      fprintf (sf, "};\n"
	       "#define %sLen %d\n", varname, i);
      fclose (sf);
      close (fd);
      return 1;
    }

  return 0;
}
