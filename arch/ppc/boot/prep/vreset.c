/*
 * vreset.c
 *
 * Initialize the VGA control registers to 80x25 text mode.
 *
 * Adapted from a program by:
 *                                      Steve Sellgren
 *                                      San Francisco Indigo Company
 *                                      sfindigo!sellgren@uunet.uu.net
 *
 * Original concept by:
 *                                      Gary Thomas <gdt@linuxppc.org>
 * Adapted for Moto boxes by:
 *                                      Pat Kane & Mark Scott, 1996
 * Adapted for IBM portables by:
 *                                      Takeshi Ishimoto
 * Multi-console support:
 *                                      Terje Malmedal <terje.malmedal@usit.uio.no>
 */

#include "iso_font.h"
#include "nonstdio.h"

extern char *vidmem;
extern int lines, cols;
struct VaRegs;

/*
 * VGA Register
 */
struct VgaRegs
{
	unsigned short io_port;
	unsigned char  io_index;
	unsigned char  io_value;
};

void unlockVideo(int slot);
void setTextRegs(struct VgaRegs *svp);
void setTextCLUT(int shift);
void clearVideoMemory(void);
void loadFont(unsigned char *ISA_mem);

static void mdelay(int ms)
{
	for (; ms > 0; --ms)
		udelay(1000);
}

/*
 * Default console text mode registers  used to reset
 * graphics adapter.
 */
#define NREGS 54
#define ENDMK  0xFFFF  /* End marker */

#define S3Vendor	0x5333
#define CirrusVendor    0x1013
#define DiamondVendor   0x100E
#define MatroxVendor    0x102B
#define ParadiseVendor  0x101C

struct VgaRegs GenVgaTextRegs[NREGS+1] = {
	/* port		index	value  */
	/* SR Regs */
	{ 0x3c4,	0x1,	0x0 },
	{ 0x3c4,	0x2,	0x3 },
	{ 0x3c4,	0x3,	0x0 },
	{ 0x3c4,	0x4,	0x2 },
	 /* CR Regs */
	{ 0x3d4,	0x0,	0x5f },
	{ 0x3d4,	0x1,	0x4f },
	{ 0x3d4,	0x2,	0x50 },
	{ 0x3d4,	0x3,	0x82 },
	{ 0x3d4,	0x4,	0x55 },
	{ 0x3d4,	0x5,	0x81 },
	{ 0x3d4,	0x6,	0xbf },
	{ 0x3d4,	0x7,	0x1f },
	{ 0x3d4,	0x8,	0x00 },
	{ 0x3d4,	0x9,	0x4f },
	{ 0x3d4,	0xa,	0x0d },
	{ 0x3d4,	0xb,	0x0e },
	{ 0x3d4,	0xc,	0x00 },
	{ 0x3d4,	0xd,	0x00 },
	{ 0x3d4,	0xe,	0x00 },
	{ 0x3d4,	0xf,	0x00 },
	{ 0x3d4,	0x10,	0x9c },
	{ 0x3d4,	0x11,	0x8e },
	{ 0x3d4,	0x12,	0x8f },
	{ 0x3d4,	0x13,	0x28 },
	{ 0x3d4,	0x14,	0x1f },
	{ 0x3d4,	0x15,	0x96 },
	{ 0x3d4,	0x16,	0xb9 },
	{ 0x3d4,	0x17,	0xa3 },
	 /* GR Regs */
	{ 0x3ce,	0x0,	0x0 },
	{ 0x3ce,	0x1,	0x0 },
	{ 0x3ce,	0x2,	0x0 },
	{ 0x3ce,	0x3,	0x0 },
	{ 0x3ce,	0x4,	0x0 },
	{ 0x3ce,	0x5,	0x10 },
	{ 0x3ce,	0x6,	0xe },
	{ 0x3ce,	0x7,	0x0 },
	{ 0x3ce,	0x8,	0xff },
	{ ENDMK }
};

struct RGBColors
{
  unsigned char r, g, b;
};

/*
 * Default console text mode color table.
 * These values were obtained by booting Linux with
 * text mode firmware & then dumping the registers.
 */
struct RGBColors TextCLUT[256] =
{
	/* red	green	blue  */
	{ 0x0,	0x0,	0x0 },
	{ 0x0,	0x0,	0x2a },
	{ 0x0,	0x2a,	0x0 },
	{ 0x0,	0x2a,	0x2a },
	{ 0x2a,	0x0,	0x0 },
	{ 0x2a,	0x0,	0x2a },
	{ 0x2a,	0x2a,	0x0 },
	{ 0x2a,	0x2a,	0x2a },
	{ 0x0,	0x0,	0x15 },
	{ 0x0,	0x0,	0x3f },
	{ 0x0,	0x2a,	0x15 },
	{ 0x0,	0x2a,	0x3f },
	{ 0x2a,	0x0,	0x15 },
	{ 0x2a,	0x0,	0x3f },
	{ 0x2a,	0x2a,	0x15 },
	{ 0x2a,	0x2a,	0x3f },
	{ 0x0,	0x15,	0x0 },
	{ 0x0,	0x15,	0x2a },
	{ 0x0,	0x3f,	0x0 },
	{ 0x0,	0x3f,	0x2a },
	{ 0x2a,	0x15,	0x0 },
	{ 0x2a,	0x15,	0x2a },
	{ 0x2a,	0x3f,	0x0 },
	{ 0x2a,	0x3f,	0x2a },
	{ 0x0,	0x15,	0x15 },
	{ 0x0,	0x15,	0x3f },
	{ 0x0,	0x3f,	0x15 },
	{ 0x0,	0x3f,	0x3f },
	{ 0x2a,	0x15,	0x15 },
	{ 0x2a,	0x15,	0x3f },
	{ 0x2a,	0x3f,	0x15 },
	{ 0x2a,	0x3f,	0x3f },
	{ 0x15,	0x0,	0x0 },
	{ 0x15,	0x0,	0x2a },
	{ 0x15,	0x2a,	0x0 },
	{ 0x15,	0x2a,	0x2a },
	{ 0x3f,	0x0,	0x0 },
	{ 0x3f,	0x0,	0x2a },
	{ 0x3f,	0x2a,	0x0 },
	{ 0x3f,	0x2a,	0x2a },
	{ 0x15,	0x0,	0x15 },
	{ 0x15,	0x0,	0x3f },
	{ 0x15,	0x2a,	0x15 },
	{ 0x15,	0x2a,	0x3f },
	{ 0x3f,	0x0,	0x15 },
	{ 0x3f,	0x0,	0x3f },
	{ 0x3f,	0x2a,	0x15 },
	{ 0x3f,	0x2a,	0x3f },
	{ 0x15,	0x15,	0x0 },
	{ 0x15,	0x15,	0x2a },
	{ 0x15,	0x3f,	0x0 },
	{ 0x15,	0x3f,	0x2a },
	{ 0x3f,	0x15,	0x0 },
	{ 0x3f,	0x15,	0x2a },
	{ 0x3f,	0x3f,	0x0 },
	{ 0x3f,	0x3f,	0x2a },
	{ 0x15,	0x15,	0x15 },
	{ 0x15,	0x15,	0x3f },
	{ 0x15,	0x3f,	0x15 },
	{ 0x15,	0x3f,	0x3f },
	{ 0x3f,	0x15,	0x15 },
	{ 0x3f,	0x15,	0x3f },
	{ 0x3f,	0x3f,	0x15 },
	{ 0x3f,	0x3f,	0x3f },
	{ 0x39,	0xc,	0x5 },
	{ 0x15,	0x2c,	0xf },
	{ 0x26,	0x10,	0x3d },
	{ 0x29,	0x29,	0x38 },
	{ 0x4,	0x1a,	0xe },
	{ 0x2,	0x1e,	0x3a },
	{ 0x3c,	0x25,	0x33 },
	{ 0x3c,	0xc,	0x2c },
	{ 0x3f,	0x3,	0x2b },
	{ 0x1c,	0x9,	0x13 },
	{ 0x25,	0x2a,	0x35 },
	{ 0x1e,	0xa,	0x38 },
	{ 0x24,	0x8,	0x3 },
	{ 0x3,	0xe,	0x36 },
	{ 0xc,	0x6,	0x2a },
	{ 0x26,	0x3,	0x32 },
	{ 0x5,	0x2f,	0x33 },
	{ 0x3c,	0x35,	0x2f },
	{ 0x2d,	0x26,	0x3e },
	{ 0xd,	0xa,	0x10 },
	{ 0x25,	0x3c,	0x11 },
	{ 0xd,	0x4,	0x2e },
	{ 0x5,	0x19,	0x3e },
	{ 0xc,	0x13,	0x34 },
	{ 0x2b,	0x6,	0x24 },
	{ 0x4,	0x3,	0xd },
	{ 0x2f,	0x3c,	0xc },
	{ 0x2a,	0x37,	0x1f },
	{ 0xf,	0x12,	0x38 },
	{ 0x38,	0xe,	0x2a },
	{ 0x12,	0x2f,	0x19 },
	{ 0x29,	0x2e,	0x31 },
	{ 0x25,	0x13,	0x3e },
	{ 0x33,	0x3e,	0x33 },
	{ 0x1d,	0x2c,	0x25 },
	{ 0x15,	0x15,	0x5 },
	{ 0x32,	0x25,	0x39 },
	{ 0x1a,	0x7,	0x1f },
	{ 0x13,	0xe,	0x1d },
	{ 0x36,	0x17,	0x34 },
	{ 0xf,	0x15,	0x23 },
	{ 0x2,	0x35,	0xd },
	{ 0x15,	0x3f,	0xc },
	{ 0x14,	0x2f,	0xf },
	{ 0x19,	0x21,	0x3e },
	{ 0x27,	0x11,	0x2f },
	{ 0x38,	0x3f,	0x3c },
	{ 0x36,	0x2d,	0x15 },
	{ 0x16,	0x17,	0x2 },
	{ 0x1,	0xa,	0x3d },
	{ 0x1b,	0x11,	0x3f },
	{ 0x21,	0x3c,	0xd },
	{ 0x1a,	0x39,	0x3d },
	{ 0x8,	0xe,	0xe },
	{ 0x22,	0x21,	0x23 },
	{ 0x1e,	0x30,	0x5 },
	{ 0x1f,	0x22,	0x3d },
	{ 0x1e,	0x2f,	0xa },
	{ 0x0,	0x1c,	0xe },
	{ 0x0,	0x1c,	0x15 },
	{ 0x0,	0x1c,	0x1c },
	{ 0x0,	0x15,	0x1c },
	{ 0x0,	0xe,	0x1c },
	{ 0x0,	0x7,	0x1c },
	{ 0xe,	0xe,	0x1c },
	{ 0x11,	0xe,	0x1c },
	{ 0x15,	0xe,	0x1c },
	{ 0x18,	0xe,	0x1c },
	{ 0x1c,	0xe,	0x1c },
	{ 0x1c,	0xe,	0x18 },
	{ 0x1c,	0xe,	0x15 },
	{ 0x1c,	0xe,	0x11 },
	{ 0x1c,	0xe,	0xe },
	{ 0x1c,	0x11,	0xe },
	{ 0x1c,	0x15,	0xe },
	{ 0x1c,	0x18,	0xe },
	{ 0x1c,	0x1c,	0xe },
	{ 0x18,	0x1c,	0xe },
	{ 0x15,	0x1c,	0xe },
	{ 0x11,	0x1c,	0xe },
	{ 0xe,	0x1c,	0xe },
	{ 0xe,	0x1c,	0x11 },
	{ 0xe,	0x1c,	0x15 },
	{ 0xe,	0x1c,	0x18 },
	{ 0xe,	0x1c,	0x1c },
	{ 0xe,	0x18,	0x1c },
	{ 0xe,	0x15,	0x1c },
	{ 0xe,	0x11,	0x1c },
	{ 0x14,	0x14,	0x1c },
	{ 0x16,	0x14,	0x1c },
	{ 0x18,	0x14,	0x1c },
	{ 0x1a,	0x14,	0x1c },
	{ 0x1c,	0x14,	0x1c },
	{ 0x1c,	0x14,	0x1a },
	{ 0x1c,	0x14,	0x18 },
	{ 0x1c,	0x14,	0x16 },
	{ 0x1c,	0x14,	0x14 },
	{ 0x1c,	0x16,	0x14 },
	{ 0x1c,	0x18,	0x14 },
	{ 0x1c,	0x1a,	0x14 },
	{ 0x1c,	0x1c,	0x14 },
	{ 0x1a,	0x1c,	0x14 },
	{ 0x18,	0x1c,	0x14 },
	{ 0x16,	0x1c,	0x14 },
	{ 0x14,	0x1c,	0x14 },
	{ 0x14,	0x1c,	0x16 },
	{ 0x14,	0x1c,	0x18 },
	{ 0x14,	0x1c,	0x1a },
	{ 0x14,	0x1c,	0x1c },
	{ 0x14,	0x1a,	0x1c },
	{ 0x14,	0x18,	0x1c },
	{ 0x14,	0x16,	0x1c },
	{ 0x0,	0x0,	0x10 },
	{ 0x4,	0x0,	0x10 },
	{ 0x8,	0x0,	0x10 },
	{ 0xc,	0x0,	0x10 },
	{ 0x10,	0x0,	0x10 },
	{ 0x10,	0x0,	0xc },
	{ 0x10,	0x0,	0x8 },
	{ 0x10,	0x0,	0x4 },
	{ 0x10,	0x0,	0x0 },
	{ 0x10,	0x4,	0x0 },
	{ 0x10,	0x8,	0x0 },
	{ 0x10,	0xc,	0x0 },
	{ 0x10,	0x10,	0x0 },
	{ 0xc,	0x10,	0x0 },
	{ 0x8,	0x10,	0x0 },
	{ 0x4,	0x10,	0x0 },
	{ 0x0,	0x10,	0x0 },
	{ 0x0,	0x10,	0x4 },
	{ 0x0,	0x10,	0x8 },
	{ 0x0,	0x10,	0xc },
	{ 0x0,	0x10,	0x10 },
	{ 0x0,	0xc,	0x10 },
	{ 0x0,	0x8,	0x10 },
	{ 0x0,	0x4,	0x10 },
	{ 0x8,	0x8,	0x10 },
	{ 0xa,	0x8,	0x10 },
	{ 0xc,	0x8,	0x10 },
	{ 0xe,	0x8,	0x10 },
	{ 0x10,	0x8,	0x10 },
	{ 0x10,	0x8,	0xe },
	{ 0x10,	0x8,	0xc },
	{ 0x10,	0x8,	0xa },
	{ 0x10,	0x8,	0x8 },
	{ 0x10,	0xa,	0x8 },
	{ 0x10,	0xc,	0x8 },
	{ 0x10,	0xe,	0x8 },
	{ 0x10,	0x10,	0x8 },
	{ 0xe,	0x10,	0x8 },
	{ 0xc,	0x10,	0x8 },
	{ 0xa,	0x10,	0x8 },
	{ 0x8,	0x10,	0x8 },
	{ 0x8,	0x10,	0xa },
	{ 0x8,	0x10,	0xc },
	{ 0x8,	0x10,	0xe },
	{ 0x8,	0x10,	0x10 },
	{ 0x8,	0xe,	0x10 },
	{ 0x8,	0xc,	0x10 },
	{ 0x8,	0xa,	0x10 },
	{ 0xb,	0xb,	0x10 },
	{ 0xc,	0xb,	0x10 },
	{ 0xd,	0xb,	0x10 },
	{ 0xf,	0xb,	0x10 },
	{ 0x10,	0xb,	0x10 },
	{ 0x10,	0xb,	0xf },
	{ 0x10,	0xb,	0xd },
	{ 0x10,	0xb,	0xc },
	{ 0x10,	0xb,	0xb },
	{ 0x10,	0xc,	0xb },
	{ 0x10,	0xd,	0xb },
	{ 0x10,	0xf,	0xb },
	{ 0x10,	0x10,	0xb },
	{ 0xf,	0x10,	0xb },
	{ 0xd,	0x10,	0xb },
	{ 0xc,	0x10,	0xb },
	{ 0xb,	0x10,	0xb },
	{ 0xb,	0x10,	0xc },
	{ 0xb,	0x10,	0xd },
	{ 0xb,	0x10,	0xf },
	{ 0xb,	0x10,	0x10 },
	{ 0xb,	0xf,	0x10 },
	{ 0xb,	0xd,	0x10 },
	{ 0xb,	0xc,	0x10 },
	{ 0x0,	0x0,	0x0 },
	{ 0x0,	0x0,	0x0 },
	{ 0x0,	0x0,	0x0 },
	{ 0x0,	0x0,	0x0 },
	{ 0x0,	0x0,	0x0 },
	{ 0x0,	0x0,	0x0 },
	{ 0x0,	0x0,	0x0 }
};

unsigned char AC[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00};

static int scanPCI(int start_slt);
static int PCIVendor(int);
#ifdef DEBUG
static void printslots(void);
#endif
extern void puthex(unsigned long);
extern void puts(const char *);
static void unlockS3(void);

static inline void
outw(int port, unsigned short val)
{
	outb(port, val >> 8);
	outb(port+1, val);
}

int
vga_init(unsigned char *ISA_mem)
{
	int slot;
	struct VgaRegs *VgaTextRegs;

	/* See if VGA already in TEXT mode - exit if so! */
	outb(0x3CE, 0x06);
	if ((inb(0x3CF) & 0x01) == 0){
		puts("VGA already in text mode\n");
		return 0;
	}

	/* If no VGA responding in text mode, then we have some work to do...
	 */
	slot = -1;
	while((slot = scanPCI(slot)) > -1) { /* find video card in use  */
		unlockVideo(slot);           /* enable I/O to card      */
		VgaTextRegs = GenVgaTextRegs;

		switch (PCIVendor(slot)) {
		default:
			break;
		case(S3Vendor):
			unlockS3();
			break;

		case(CirrusVendor):
			outw(0x3C4, 0x0612);       /* unlock ext regs */
			outw(0x3C4, 0x0700);       /* reset ext sequence mode */
			break;

		case(ParadiseVendor):                 /* IBM Portable 850 */
			outw(0x3ce, 0x0f05);      /* unlock pardise registers */
			outw(0x3c4, 0x0648);
			outw(0x3d4, 0x2985);
			outw(0x3d4, 0x34a6);
			outb(0x3ce, 0x0b);       /* disable linear addressing */
			outb(0x3cf, inb(0x3cf) & ~0x30);
			outw(0x3c4, 0x1400);
			outb(0x3ce, 0x0e);       /* disable 256 color mode */
			outb(0x3cf, inb(0x3cf) & ~0x01);
			outb(0xd00, 0xff);       /* enable auto-centering */
			if (!(inb(0xd01) & 0x03)) {
				outb(0x3d4, 0x33);
				outb(0x3d5, inb(0x3d5) & ~0x90);
				outb(0x3d4, 0x32);
				outb(0x3d5, inb(0x3d5) | 0x04);
				outw(0x3d4, 0x0250);
				outw(0x3d4, 0x07ba);
				outw(0x3d4, 0x0900);
				outw(0x3d4, 0x15e7);
				outw(0x3d4, 0x2a95);
			}
			outw(0x3d4, 0x34a0);
			break;

	#if 0 /* Untested - probably doesn't work */
		case(MatroxVendor):
		case(DiamondVendor):
			puts("VGA Chip Vendor ID: ");
			puthex(PCIVendor(slot));
			puts("\n");
			mdelay(1000);
	#endif
		};

		outw(0x3C4, 0x0120);           /* disable video              */
		setTextRegs(VgaTextRegs);      /* initial register setup     */
		setTextCLUT(0);                /* load color lookup table    */
		loadFont(ISA_mem);             /* load font                  */
		setTextRegs(VgaTextRegs);      /* reload registers           */
		outw(0x3C4, 0x0100);           /* re-enable video            */
		clearVideoMemory();

		if (PCIVendor(slot) == S3Vendor) {
			outb(0x3c2, 0x63);                  /* MISC */
		} /* endif */

	#ifdef DEBUG
		printslots();
		mdelay(5000);
	#endif

		mdelay(1000);	/* give time for the video monitor to come up */
        }
	return (1);  /* 'CRT' I/O supported */
}

/*
 * Write to VGA Attribute registers.
 */
void
writeAttr(unsigned char index, unsigned char data, unsigned char videoOn)
{
	unsigned char v;
	v = inb(0x3da);   /* reset attr. address toggle */
	if (videoOn)
		outb(0x3c0, (index & 0x1F) | 0x20);
	else
		outb(0x3c0, (index & 0x1F));
	outb(0x3c0, data);
}

void
setTextRegs(struct VgaRegs *svp)
{
	int i;

	/*
	 *  saved settings
	 */
	while( svp->io_port != ENDMK ) {
		outb(svp->io_port,   svp->io_index);
		outb(svp->io_port+1, svp->io_value);
		svp++;
	}

	outb(0x3c2, 0x67);  /* MISC */
	outb(0x3c6, 0xff);  /* MASK */

	for ( i = 0; i < 0x10; i++)
		writeAttr(i, AC[i], 0);  /* pallete */
	writeAttr(0x10, 0x0c, 0);    /* text mode */
	writeAttr(0x11, 0x00, 0);    /* overscan color (border) */
	writeAttr(0x12, 0x0f, 0);    /* plane enable */
	writeAttr(0x13, 0x08, 0);    /* pixel panning */
	writeAttr(0x14, 0x00, 1);    /* color select; video on  */
}

void
setTextCLUT(int shift)
{
	int i;

	outb(0x3C6, 0xFF);
	i = inb(0x3C7);
	outb(0x3C8, 0);
	i = inb(0x3C7);

	for ( i = 0; i < 256; i++) {
		outb(0x3C9, TextCLUT[i].r << shift);
		outb(0x3C9, TextCLUT[i].g << shift);
		outb(0x3C9, TextCLUT[i].b << shift);
	}
}

void
loadFont(unsigned char *ISA_mem)
{
	int i, j;
	unsigned char *font_page = (unsigned char *) &ISA_mem[0xA0000];

	outb(0x3C2, 0x67);
	/*
	 * Load font
	 */
	i = inb(0x3DA);  /* Reset Attr toggle */

	outb(0x3C0,0x30);
	outb(0x3C0, 0x01);      /* graphics mode */

	outw(0x3C4, 0x0001);    /* reset sequencer */
	outw(0x3C4, 0x0204);    /* write to plane 2 */
	outw(0x3C4, 0x0406);    /* enable plane graphics */
	outw(0x3C4, 0x0003);    /* reset sequencer */
	outw(0x3CE, 0x0402);    /* read plane 2 */
	outw(0x3CE, 0x0500);    /* write mode 0, read mode 0 */
	outw(0x3CE, 0x0605);    /* set graphics mode */

	for (i = 0;  i < sizeof(font);  i += 16) {
		for (j = 0;  j < 16;  j++) {
			__asm__ volatile("eieio");
			font_page[(2*i)+j] = font[i+j];
		}
	}
}

static void
unlockS3(void)
{
        int s3_device_id;
	outw(0x3d4, 0x3848);
	outw(0x3d4, 0x39a5);
	outb(0x3d4, 0x2d);
	s3_device_id = inb(0x3d5) << 8;
	outb(0x3d4, 0x2e);
	s3_device_id |= inb(0x3d5);

	if (s3_device_id != 0x8812) {
		/* From the S3 manual */
		outb(0x46E8, 0x10);  /* Put into setup mode */
		outb(0x3C3, 0x10);
		outb(0x102, 0x01);   /* Enable registers */
		outb(0x46E8, 0x08);  /* Enable video */
		outb(0x3C3, 0x08);
		outb(0x4AE8, 0x00);

#if 0
		outb(0x42E8, 0x80);  /* Reset graphics engine? */
#endif

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
	} else {
                outw(0x3c4, 0x0806);            /* IBM Portable 860 */
                outw(0x3c4, 0x1041);
                outw(0x3c4, 0x1128);
                outw(0x3d4, 0x4000);
                outw(0x3d4, 0x3100);
                outw(0x3d4, 0x3a05);
                outw(0x3d4, 0x6688);
                outw(0x3d4, 0x5800);            /* disable linear addressing */
                outw(0x3d4, 0x4500);            /* disable H/W cursor */
                outw(0x3c4, 0x5410);            /* enable auto-centering */
                outw(0x3c4, 0x561f);
                outw(0x3c4, 0x1b80);            /* lock DCLK selection */
                outw(0x3d4, 0x3900);            /* lock S3 registers */
                outw(0x3d4, 0x3800);
	} /* endif */
}

/*
 * cursor() sets an offset (0-1999) into the 80x25 text area.
 */
void
cursor(int x, int y)
{
	int pos = (y*cols)+x;
	outb(0x3D4, 14);
	outb(0x3D5, pos >> 8);
	outb(0x3D4, 15);
	outb(0x3D5, pos);
}

void
clearVideoMemory(void)
{
	int i, j;
	for (i = 0;  i < lines;  i++) {
		for (j = 0;  j < cols;  j++) {
			vidmem[((i*cols)+j)*2] = 0x20;	/* fill with space character */
			vidmem[((i*cols)+j)*2+1] = 0x07;  /* set bg & fg attributes */
		}
	}
}

/* ============ */


#define NSLOTS 8
#define NPCIREGS  5


/*
 should use devfunc number/indirect method to be totally safe on
 all machines, this works for now on 3 slot Moto boxes
*/

struct PCI_ConfigInfo {
  unsigned long * config_addr;
  unsigned long regs[NPCIREGS];
} PCI_slots [NSLOTS] = {

    { (unsigned long *)0x80808000, {0xDEADBEEF,} },   /* onboard */
    { (unsigned long *)0x80800800, {0xDEADBEEF,} },   /* onboard */
    { (unsigned long *)0x80801000, {0xDEADBEEF,} },   /* onboard */
    { (unsigned long *)0x80802000, {0xDEADBEEF,} },   /* onboard */
    { (unsigned long *)0x80804000, {0xDEADBEEF,} },   /* onboard */
    { (unsigned long *)0x80810000, {0xDEADBEEF,} },   /* slot A/1 */
    { (unsigned long *)0x80820000, {0xDEADBEEF,} },   /* slot B/2 */
    { (unsigned long *)0x80840000, {0xDEADBEEF,} }    /* slot C/3 */
};



/*
 * The following code modifies the PCI Command register
 * to enable memory and I/O accesses.
 */
void
unlockVideo(int slot)
{
       volatile unsigned char * ppci;

        ppci =  (unsigned char * )PCI_slots[slot].config_addr;
	ppci[4] = 0x0003;         /* enable memory and I/O accesses */
	ppci[0x10] = 0x00000;     /* turn off memory mapping */
	ppci[0x11] = 0x00000;     /* mem_base = 0 */
	ppci[0x12] = 0x00000;
	ppci[0x13] = 0x00000;
	__asm__ volatile("eieio");

	outb(0x3d4, 0x11);
	outb(0x3d5, 0x0e);   /* unlock CR0-CR7 */
}

long
SwapBytes(long lv)   /* turn little endian into big indian long */
{
    long t;
    t  = (lv&0x000000FF) << 24;
    t |= (lv&0x0000FF00) << 8;
    t |= (lv&0x00FF0000) >> 8;
    t |= (lv&0xFF000000) >> 24;
    return(t);
}


#define DEVID   0
#define CMD     1
#define CLASS   2
#define MEMBASE 4

int
scanPCI(int start_slt)
{
	int slt, r;
	struct PCI_ConfigInfo *pslot;
	int theSlot = -1;
	int highVgaSlot = 0;

	for ( slt = start_slt + 1; slt < NSLOTS; slt++) {
		pslot = &PCI_slots[slt];
		for ( r = 0; r < NPCIREGS; r++) {
			pslot->regs[r] = SwapBytes ( pslot->config_addr[r] );
		}
		/* card in slot ? */
		if ( pslot->regs[DEVID] != 0xFFFFFFFF ) {
			/* VGA ? */
			if ( ((pslot->regs[CLASS] & 0xFFFFFF00) == 0x03000000) ||
			     ((pslot->regs[CLASS] & 0xFFFFFF00) == 0x00010000)) {
				highVgaSlot = slt;
				/* did firmware enable it ? */
				if ( (pslot->regs[CMD] & 0x03) ) {
					theSlot = slt;
					break;
				}
			}
		}
	}

	return ( theSlot );
}

/* return Vendor ID of card in the slot */
static
int PCIVendor(int slotnum) {
 struct PCI_ConfigInfo *pslot;

 pslot = &PCI_slots[slotnum];

return (pslot->regs[DEVID] & 0xFFFF);
}

#ifdef DEBUG
static
void printslots(void)
{
	int i;
#if 0
	struct PCI_ConfigInfo *pslot;
#endif
	for(i=0; i < NSLOTS; i++) {
#if 0
		pslot = &PCI_slots[i];
		printf("Slot: %d, Addr: %x, Vendor: %08x, Class: %08x\n",
		       i, pslot->config_addr, pslot->regs[0], pslot->regs[2]);
#else
		puts("PCI Slot number: "); puthex(i);
		puts(" Vendor ID: ");
		puthex(PCIVendor(i)); puts("\n");
#endif
	}
}
#endif /* DEBUG */
