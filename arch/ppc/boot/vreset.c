/*
 * vreset.c
 *
 * Initialize the VGA control registers to 80x25 text mode.
 *
 * Adapted from a program by:
 *                                      Steve Sellgren
 *                                      San Francisco Indigo Company
 *                                      sfindigo!sellgren@uunet.uu.net
 */
 
unsigned char CRTC[24] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00, /*0x07, 0x80, */
    0x9C, 0xAE, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3};
unsigned char SEQ[5] = {0x3, 0x0, 0x3, 0x0, 0x2};
unsigned char GC[9] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x10, 0xE, 0x0, 0xFF};
unsigned char AC[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 
    0x0C, 0x00, 0x0F, 0x08, 0x00};

#include "iso_font.h"

static const unsigned char color_LUT[] =
   {
	0x00, 0x00, 0x00, /* 0 - black */
	0x00, 0x00, 0x2A, /* 1 - blue */
	0x00, 0x2A, 0x00, /* 2 - green */
	0x00, 0x2A, 0x2A, /* 3 - cyan */
	0x2A, 0x00, 0x00, /* 4 - red */
	0x2A, 0x00, 0x2A, /* 5 - magenta */
	0x2A, 0x2A, 0x00, /* 6 - brown */
	0x2A, 0x2A, 0x2A, /* 7 - white */
	0x00, 0x00, 0x15, /* 8 - gray */
	0x00, 0x00, 0x3F, /* 9 - light blue */
	0x00, 0x2A, 0x15, /* 10 - light green */
	0x00, 0x2A, 0x3F, /* 11 - light cyan */
	0x2A, 0x00, 0x15, /* 12 - light red */
	0x2A, 0x00, 0x3F, /* 13 - light magenta */
	0x2A, 0x2A, 0x15, /* 14 - yellow */
	0x2A, 0x2A, 0x3F, /* 15 - bright white */
   };

static inline
outw(int port, unsigned short val)
{
	outb(port, val >> 8);
	outb(port+1, val);
}
 
vga_init(unsigned char *ISA_mem)
{
  int i, j;
  int value;
  unsigned char *font_page = (unsigned char *) &ISA_mem[0xA0000];

  /* See if VGA already in TEXT mode - exit if so! */
  outb(0x3CE, 0x06);
  if ((inb(0x3CF) & 0x01) == 0) return;
    
  /* From the S3 manual */
  outb(0x46E8, 0x10);  /* Put into setup mode */
  outb(0x3C3, 0x10);
  outb(0x102, 0x01);   /* Enable registers */
  outb(0x46E8, 0x08);  /* Enable video */
  outb(0x3C3, 0x08);
  outb(0x4AE8, 0x00);

  outb(0x42E8, 0x80);  /* Reset graphics engine? */

  outb(0x3D4, 0x38);  /* Unlock all registers */
  outb(0x3D5, 0x48);
  outb(0x3D4, 0x39);
  outb(0x3D5, 0xA5);
  outb(0x3D4, 0x40);
  outb(0x3D5, inb(0x3D5)|0x01);
  outb(0x3D4, 0x33);
  outb(0x3D5, inb(0x3D5)&~0x52);
  outb(0x3D4, 0x35);
  outb(0x3D5, inb(0x3D5)&~0x30);
  outb(0x3D4, 0x3A);
  outb(0x3D5, 0x00);
  outb(0x3D4, 0x53);
  outb(0x3D5, 0x00);
  outb(0x3D4, 0x31);
  outb(0x3D5, inb(0x3D5)&~0x4B);
  outb(0x3D4, 0x58);
  outb(0x3D5, 0);

  outb(0x3D4, 0x54);
  outb(0x3D5, 0x38);
  outb(0x3D4, 0x60);
  outb(0x3D5, 0x07);
  outb(0x3D4, 0x61);
  outb(0x3D5, 0x80);
  outb(0x3D4, 0x62);
  outb(0x3D5, 0xA1);
  outb(0x3D4, 0x69);  /* High order bits for cursor address */
  outb(0x3D5, 0);
  
  outb(0x3D4, 0x32);
  outb(0x3D5, inb(0x3D5)&~0x10);

  outb(0x3C2, 0x67);

  /* Initialize DAC */
  outb(0x3C6,0xFF);
  inb(0x3C7);
  outb(0x3C8,0x00);
  inb(0x3C7);
  for (i=0; i<sizeof(color_LUT); i++) {
    outb(0x3C9, color_LUT[i]);
  }
  for (i; i<768; i += 3) {
    outb(0x3C9, 0x3F); /* White? */
    outb(0x3C9, 0x3F); /* White? */
    outb(0x3C9, 0x3F); /* White? */
  }

  /* Load font */
  NOP(inb(0x3DA));  /* Reset Address/Data FlipFlop for Attribute ctlr */
  outb(0x3C0,0x30); outb(0x3C0, 0x01); /* graphics mode */
  outw(0x3C4, 0x0001);    /* reset sequencer */
  outw(0x3C4, 0x0204);    /* write to plane 2 */
  outw(0x3C4, 0x0407);    /* enable plane graphics */
  outw(0x3C4, 0x0003);    /* reset sequencer */
  outw(0x3CE, 0x0402);    /* read plane 2 */
  outw(0x3CE, 0x0500);    /* write mode 0, read mode 0 */
  outw(0x3CE, 0x0600);    /* set graphics */
  for (i = 0;  i < sizeof(font);  i += 16) {
    for (j = 0;  j < 16;  j++) {
      font_page[(2*i)+j] = font[i+j];
    }
  }

  for (i = 0; i < 24; i++) {
    outb(0x3D4, i);
    outb(0x3D5, CRTC[i]);
  }
  for (i = 0; i < 5; i++) {
    outb(0x3C4, i);
    outb(0x3C5, SEQ[i]);
  }
  for (i = 0; i < 9; i++) {
    outb(0x3CE, i);
    outb(0x3CF, GC[i]);
  }
  value = inb(0x3DA);         /* reset flip-flop */
  for (i = 0; i < 16; i++) {
    outb(0x3C0, i);
    outb(0x3C0, AC[i]);
  }
  for (i = 16; i < 21; i++) {
    outb(0x3C0, i | 0x20);
    outb(0x3C0, AC[i]);
  }
  outb(0x3C2, 0x23);
}

static int
NOP(int x)
{
}
