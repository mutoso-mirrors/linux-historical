/*
 *  linux/drivers/video/vgacon.c -- Low level VGA based console driver
 *
 *	Created 28 Sep 1997 by Geert Uytterhoeven
 *
 *	Rewritten by Martin Mares <mj@ucw.cz>, July 1998
 *
 *  This file is based on the old console.c, vga.c and vesa_blank.c drivers.
 *
 *	Copyright (C) 1991, 1992  Linus Torvalds
 *			    1995  Jay Estabrook
 *
 *	User definable mapping table and font loading by Eugene G. Crosser,
 *	<crosser@pccross.msk.su>
 *
 *	Improved loadable font/UTF-8 support by H. Peter Anvin
 *	Feb-Sep 1995 <peter.anvin@linux.org>
 *
 *	Colour palette handling, by Simon Tatham
 *	17-Jun-95 <sgt20@cam.ac.uk>
 *
 *	if 512 char mode is already enabled don't re-enable it,
 *	because it causes screen to flicker, by Mitja Horvat
 *	5-May-96 <mitja.horvat@guest.arnes.si>
 *
 *	Use 2 outw instead of 4 outb_p to reduce erroneous text
 *	flashing on RHS of screen during heavy console scrolling .
 *	Oct 1996, Paul Gortmaker.
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

/* KNOWN PROBLEMS/TO DO ========================================FIXME======== *
 *
 *	- monochrome attribute encoding (convert abscon <-> VGA style)
 *
 *	- Cursor shape fixes
 *
 * KNOWN PROBLEMS/TO DO ==================================================== */


#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/io.h>


#define BLANK 0x0020

#define CAN_LOAD_EGA_FONTS	/* undefine if the user must not do this */
#define CAN_LOAD_PALETTE	/* undefine if the user must not do this */

#undef VGA_CAN_DO_64KB

#define dac_reg		0x3c8
#define dac_val		0x3c9
#define attrib_port	0x3c0
#define seq_port_reg	0x3c4
#define seq_port_val	0x3c5
#define gr_port_reg	0x3ce
#define gr_port_val	0x3cf
#define video_misc_rd	0x3cc
#define video_misc_wr	0x3c2

/*
 *  Interface used by the world
 */

static const char *vgacon_startup(void);
static void vgacon_init(struct vc_data *c, int init);
static void vgacon_deinit(struct vc_data *c);
static void vgacon_cursor(struct vc_data *c, int mode);
static int vgacon_switch(struct vc_data *c);
static int vgacon_blank(struct vc_data *c, int blank);
static int vgacon_get_font(struct vc_data *c, int *w, int *h, char *data);
static int vgacon_set_font(struct vc_data *c, int w, int h, char *data);
static int vgacon_set_palette(struct vc_data *c, unsigned char *table);
static int vgacon_scrolldelta(struct vc_data *c, int lines);
static int vgacon_set_origin(struct vc_data *c);
static void vgacon_save_screen(struct vc_data *c);
static int vgacon_scroll(struct vc_data *c, int t, int b, int dir, int lines);


/* Description of the hardware situation */
static unsigned long   vga_vram_base;		/* Base of video memory */
static unsigned long   vga_vram_end;		/* End of video memory */
static u16             vga_video_port_reg;	/* Video register select port */
static u16             vga_video_port_val;	/* Video register value port */
static unsigned int    vga_video_num_columns;	/* Number of text columns */
static unsigned int    vga_video_num_lines;	/* Number of text lines */
static int	       vga_can_do_color = 0;	/* Do we support colors? */
static unsigned int    vga_default_font_height;	/* Height of default screen font */
static unsigned char   vga_video_type;		/* Card type */
static unsigned char   vga_hardscroll_enabled;
static unsigned char   vga_hardscroll_user_enable = 1;
static unsigned char   vga_font_is_default = 1;
static int	       vga_vesa_blanked;
static int	       vga_palette_blanked;
static int	       vga_is_gfx;


void no_scroll(char *str, int *ints)
{
	/*
	 * Disabling scrollback is required for the Braillex ib80-piezo
	 * Braille reader made by F.H. Papenmeier (Germany).
	 * Use the "no-scroll" bootflag.
	 */
	vga_hardscroll_user_enable = vga_hardscroll_enabled = 0;
}

/*
 * By replacing the four outb_p with two back to back outw, we can reduce
 * the window of opportunity to see text mislocated to the RHS of the
 * console during heavy scrolling activity. However there is the remote
 * possibility that some pre-dinosaur hardware won't like the back to back
 * I/O. Since the Xservers get away with it, we should be able to as well.
 */
static inline void write_vga(unsigned char reg, unsigned int val)
{
#ifndef SLOW_VGA
	unsigned int v1, v2;

	v1 = reg + (val & 0xff00);
	v2 = reg + 1 + ((val << 8) & 0xff00);
	outw(v1, vga_video_port_reg);
	outw(v2, vga_video_port_reg);
#else
	outb_p(reg, vga_video_port_reg);
	outb_p(val >> 8, vga_video_port_val);
	outb_p(reg+1, vga_video_port_reg);
	outb_p(val & 0xff, vga_video_port_val);
#endif
}

__initfunc(static const char *vgacon_startup(void))
{
	const char *display_desc = NULL;
	u16 saved;
	u16 *p;

	if (ORIG_VIDEO_ISVGA == VIDEO_TYPE_VLFB) {
	no_vga:
#ifdef CONFIG_DUMMY_CONSOLE
		conswitchp = &dummy_con;
		return conswitchp->con_startup();
#else
		return NULL;
#endif
	}


	vga_video_num_lines = ORIG_VIDEO_LINES;
	vga_video_num_columns = ORIG_VIDEO_COLS;

	if (ORIG_VIDEO_MODE == 7)	/* Is this a monochrome display? */
	{
		vga_vram_base = 0xb0000;
		vga_video_port_reg = 0x3b4;
		vga_video_port_val = 0x3b5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			vga_video_type = VIDEO_TYPE_EGAM;
			vga_vram_end = 0xb8000;
			display_desc = "EGA+";
			request_region(0x3b0,16,"ega");
		}
		else
		{
			vga_video_type = VIDEO_TYPE_MDA;
			vga_vram_end = 0xb2000;
			display_desc = "*MDA";
			request_region(0x3b0,12,"mda");
			request_region(0x3bf, 1,"mda");
		}
	}
	else				/* If not, it is color. */
	{
		vga_can_do_color = 1;
		vga_vram_base = 0xb8000;
		vga_video_port_reg = 0x3d4;
		vga_video_port_val = 0x3d5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			int i;

			vga_vram_end = 0xc0000;

			if (!ORIG_VIDEO_ISVGA) {
				vga_video_type = VIDEO_TYPE_EGAC;
				display_desc = "EGA";
				request_region(0x3c0,32,"ega");
			} else {
				vga_video_type = VIDEO_TYPE_VGAC;
				display_desc = "VGA+";
				request_region(0x3c0,32,"vga+");

#ifdef VGA_CAN_DO_64KB
				/*
				 * get 64K rather than 32K of video RAM.
				 * This doesn't actually work on all "VGA"
				 * controllers (it seems like setting MM=01
				 * and COE=1 isn't necessarily a good idea)
				 */
				vga_vram_base = 0xa0000;
				vga_vram_end = 0xb0000;
				outb_p (6, 0x3ce) ;
				outb_p (6, 0x3cf) ;
#endif

				/*
				 * Normalise the palette registers, to point
				 * the 16 screen colours to the first 16
				 * DAC entries.
				 */

				for (i=0; i<16; i++) {
					inb_p (0x3da) ;
					outb_p (i, 0x3c0) ;
					outb_p (i, 0x3c0) ;
				}
				outb_p (0x20, 0x3c0) ;

				/* now set the DAC registers back to their
				 * default values */

				for (i=0; i<16; i++) {
					outb_p (color_table[i], 0x3c8) ;
					outb_p (default_red[i], 0x3c9) ;
					outb_p (default_grn[i], 0x3c9) ;
					outb_p (default_blu[i], 0x3c9) ;
				}
			}
		}
		else
		{
			vga_video_type = VIDEO_TYPE_CGA;
			vga_vram_end = 0xba000;
			display_desc = "*CGA";
			request_region(0x3d4,2,"cga");
		}
	}
	vga_vram_base = VGA_MAP_MEM(vga_vram_base);
	vga_vram_end = VGA_MAP_MEM(vga_vram_end);

	/*
	 *	Find out if there is a graphics card present.
	 *	Are there smarter methods around?
	 */
	p = (u16 *)vga_vram_base;
	saved = scr_readw(p);
	scr_writew(0xAA55, p);
	if (scr_readw(p) != 0xAA55) {
		scr_writew(saved, p);
		goto no_vga;
	}
	scr_writew(0x55AA, p);
	if (scr_readw(p) != 0x55AA) {
		scr_writew(saved, p);
		goto no_vga;
	}
	scr_writew(saved, p);

	if (vga_video_type == VIDEO_TYPE_EGAC
	    || vga_video_type == VIDEO_TYPE_VGAC
	    || vga_video_type == VIDEO_TYPE_EGAM) {
		vga_hardscroll_enabled = vga_hardscroll_user_enable;
		vga_default_font_height = ORIG_VIDEO_POINTS;
		video_font_height = ORIG_VIDEO_POINTS;
		/* This may be suboptimal but is a safe bet - go with it */
		video_scan_lines =
			video_font_height * vga_video_num_lines;
	}

	return display_desc;
}

static void vgacon_init(struct vc_data *c, int init)
{
	/* We cannot be loaded as a module, therefore init is always 1 */
	c->vc_can_do_color = vga_can_do_color;
	c->vc_cols = vga_video_num_columns;
	c->vc_rows = vga_video_num_lines;
}

static void vgacon_deinit(struct vc_data *c)
{
	vgacon_set_origin(c);
}

static inline void vga_set_mem_top(struct vc_data *c)
{
	write_vga(12, (c->vc_visible_origin-vga_vram_base)/2);
}

static void vgacon_cursor(struct vc_data *c, int mode)
{
    if (c->vc_origin != c->vc_visible_origin)
	vgacon_scrolldelta(c, 0);
    switch (mode) {
	case CM_ERASE:
	    write_vga(14, (vga_vram_end - vga_vram_base - 1)/2);
	    break;

	case CM_MOVE:
	case CM_DRAW:
	    write_vga(14, (c->vc_pos-vga_vram_base)/2);
	    break;
    }
}

static int vgacon_switch(struct vc_data *c)
{
	/*
	 * We need to save screen size here as it's the only way
	 * we can spot the screen has been resized and we need to
	 * set size of freshly allocated screens ourselves.
	 */
	vga_video_num_columns = c->vc_cols;
	vga_video_num_lines = c->vc_rows;
	scr_memcpyw_to((u16 *) c->vc_origin, (u16 *) c->vc_screenbuf, c->vc_screenbuf_size);
	return 0;	/* Redrawing not needed */
}

static void vga_set_palette(struct vc_data *c, unsigned char *table)
{
	int i, j ;

	for (i=j=0; i<16; i++) {
		outb_p (table[i], dac_reg) ;
		outb_p (c->vc_palette[j++]>>2, dac_val) ;
		outb_p (c->vc_palette[j++]>>2, dac_val) ;
		outb_p (c->vc_palette[j++]>>2, dac_val) ;
	}
}

static int vgacon_set_palette(struct vc_data *c, unsigned char *table)
{
#ifdef CAN_LOAD_PALETTE

	if (vga_video_type != VIDEO_TYPE_VGAC || vga_palette_blanked)
		return -EINVAL;
	vga_set_palette(c, table);
	return 0;
#else
	return -EINVAL;
#endif
}

/* structure holding original VGA register settings */
static struct {
	unsigned char	SeqCtrlIndex;		/* Sequencer Index reg.   */
	unsigned char	CrtCtrlIndex;		/* CRT-Contr. Index reg.  */
	unsigned char	CrtMiscIO;		/* Miscellaneous register */
	unsigned char	HorizontalTotal;	/* CRT-Controller:00h */
	unsigned char	HorizDisplayEnd;	/* CRT-Controller:01h */
	unsigned char	StartHorizRetrace;	/* CRT-Controller:04h */
	unsigned char	EndHorizRetrace;	/* CRT-Controller:05h */
	unsigned char	Overflow;		/* CRT-Controller:07h */
	unsigned char	StartVertRetrace;	/* CRT-Controller:10h */
	unsigned char	EndVertRetrace;		/* CRT-Controller:11h */
	unsigned char	ModeControl;		/* CRT-Controller:17h */
	unsigned char	ClockingMode;		/* Seq-Controller:01h */
} vga_state;

static void vga_vesa_blank(int mode)
{
	/* save original values of VGA controller registers */
	if(!vga_vesa_blanked) {
		cli();
		vga_state.SeqCtrlIndex = inb_p(seq_port_reg);
		vga_state.CrtCtrlIndex = inb_p(vga_video_port_reg);
		vga_state.CrtMiscIO = inb_p(video_misc_rd);
		sti();

		outb_p(0x00,vga_video_port_reg);	/* HorizontalTotal */
		vga_state.HorizontalTotal = inb_p(vga_video_port_val);
		outb_p(0x01,vga_video_port_reg);	/* HorizDisplayEnd */
		vga_state.HorizDisplayEnd = inb_p(vga_video_port_val);
		outb_p(0x04,vga_video_port_reg);	/* StartHorizRetrace */
		vga_state.StartHorizRetrace = inb_p(vga_video_port_val);
		outb_p(0x05,vga_video_port_reg);	/* EndHorizRetrace */
		vga_state.EndHorizRetrace = inb_p(vga_video_port_val);
		outb_p(0x07,vga_video_port_reg);	/* Overflow */
		vga_state.Overflow = inb_p(vga_video_port_val);
		outb_p(0x10,vga_video_port_reg);	/* StartVertRetrace */
		vga_state.StartVertRetrace = inb_p(vga_video_port_val);
		outb_p(0x11,vga_video_port_reg);	/* EndVertRetrace */
		vga_state.EndVertRetrace = inb_p(vga_video_port_val);
		outb_p(0x17,vga_video_port_reg);	/* ModeControl */
		vga_state.ModeControl = inb_p(vga_video_port_val);
		outb_p(0x01,seq_port_reg);		/* ClockingMode */
		vga_state.ClockingMode = inb_p(seq_port_val);
	}

	/* assure that video is enabled */
	/* "0x20" is VIDEO_ENABLE_bit in register 01 of sequencer */
	cli();
	outb_p(0x01,seq_port_reg);
	outb_p(vga_state.ClockingMode | 0x20,seq_port_val);

	/* test for vertical retrace in process.... */
	if ((vga_state.CrtMiscIO & 0x80) == 0x80)
		outb_p(vga_state.CrtMiscIO & 0xef,video_misc_wr);

	/*
	 * Set <End of vertical retrace> to minimum (0) and
	 * <Start of vertical Retrace> to maximum (incl. overflow)
	 * Result: turn off vertical sync (VSync) pulse.
	 */
	if (mode & VESA_VSYNC_SUSPEND) {
		outb_p(0x10,vga_video_port_reg);	/* StartVertRetrace */
		outb_p(0xff,vga_video_port_val); 	/* maximum value */
		outb_p(0x11,vga_video_port_reg);	/* EndVertRetrace */
		outb_p(0x40,vga_video_port_val);	/* minimum (bits 0..3)  */
		outb_p(0x07,vga_video_port_reg);	/* Overflow */
		outb_p(vga_state.Overflow | 0x84,vga_video_port_val); /* bits 9,10 of vert. retrace */
	}

	if (mode & VESA_HSYNC_SUSPEND) {
		/*
		 * Set <End of horizontal retrace> to minimum (0) and
		 *  <Start of horizontal Retrace> to maximum
		 * Result: turn off horizontal sync (HSync) pulse.
		 */
		outb_p(0x04,vga_video_port_reg);	/* StartHorizRetrace */
		outb_p(0xff,vga_video_port_val);	/* maximum */
		outb_p(0x05,vga_video_port_reg);	/* EndHorizRetrace */
		outb_p(0x00,vga_video_port_val);	/* minimum (0) */
	}

	/* restore both index registers */
	outb_p(vga_state.SeqCtrlIndex,seq_port_reg);
	outb_p(vga_state.CrtCtrlIndex,vga_video_port_reg);
	sti();
}

static void vga_vesa_unblank(void)
{
	/* restore original values of VGA controller registers */
	cli();
	outb_p(vga_state.CrtMiscIO,video_misc_wr);

	outb_p(0x00,vga_video_port_reg);		/* HorizontalTotal */
	outb_p(vga_state.HorizontalTotal,vga_video_port_val);
	outb_p(0x01,vga_video_port_reg);		/* HorizDisplayEnd */
	outb_p(vga_state.HorizDisplayEnd,vga_video_port_val);
	outb_p(0x04,vga_video_port_reg);		/* StartHorizRetrace */
	outb_p(vga_state.StartHorizRetrace,vga_video_port_val);
	outb_p(0x05,vga_video_port_reg);		/* EndHorizRetrace */
	outb_p(vga_state.EndHorizRetrace,vga_video_port_val);
	outb_p(0x07,vga_video_port_reg);		/* Overflow */
	outb_p(vga_state.Overflow,vga_video_port_val);
	outb_p(0x10,vga_video_port_reg);		/* StartVertRetrace */
	outb_p(vga_state.StartVertRetrace,vga_video_port_val);
	outb_p(0x11,vga_video_port_reg);		/* EndVertRetrace */
	outb_p(vga_state.EndVertRetrace,vga_video_port_val);
	outb_p(0x17,vga_video_port_reg);		/* ModeControl */
	outb_p(vga_state.ModeControl,vga_video_port_val);
	outb_p(0x01,seq_port_reg);		/* ClockingMode */
	outb_p(vga_state.ClockingMode,seq_port_val);

	/* restore index/control registers */
	outb_p(vga_state.SeqCtrlIndex,seq_port_reg);
	outb_p(vga_state.CrtCtrlIndex,vga_video_port_reg);
	sti();
}

static void vga_pal_blank(void)
{
	int i;

	for (i=0; i<16; i++) {
		outb_p (i, dac_reg) ;
		outb_p (0, dac_val) ;
		outb_p (0, dac_val) ;
		outb_p (0, dac_val) ;
	}
}

static int vgacon_blank(struct vc_data *c, int blank)
{
	switch (blank) {
	case 0:				/* Unblank */
		if (vga_vesa_blanked) {
			vga_vesa_unblank();
			vga_vesa_blanked = 0;
		}
		if (vga_palette_blanked) {
			vga_set_palette(c, color_table);
			vga_palette_blanked = 0;
			return 0;
		}
		vga_is_gfx = 0;
		/* Tell console.c that it has to restore the screen itself */
		return 1;
	case 1:				/* Normal blanking */
		if (vga_video_type == VIDEO_TYPE_VGAC) {
			vga_pal_blank();
			vga_palette_blanked = 1;
			return 0;
		}
		scr_memsetw((void *)vga_vram_base, BLANK, vc_cons[0].d->vc_screenbuf_size);
		return 0;
	case -1:			/* Entering graphic mode */
		scr_memsetw((void *)vga_vram_base, BLANK, vc_cons[0].d->vc_screenbuf_size);
		vga_is_gfx = 1;
		return 0;
	default:			/* VESA blanking */
		if (vga_video_type == VIDEO_TYPE_VGAC) {
			vga_vesa_blank(blank-1);
			vga_vesa_blanked = blank;
		}
		return 0;
	}
}

/*
 * PIO_FONT support.
 *
 * The font loading code goes back to the codepage package by
 * Joel Hoffman (joel@wam.umd.edu). (He reports that the original
 * reference is: "From: p. 307 of _Programmer's Guide to PC & PS/2
 * Video Systems_ by Richard Wilton. 1987.  Microsoft Press".)
 *
 * Change for certain monochrome monitors by Yury Shevchuck
 * (sizif@botik.yaroslavl.su).
 */

#define colourmap 0xa0000
/* Pauline Middelink <middelin@polyware.iaf.nl> reports that we
   should use 0xA0000 for the bwmap as well.. */
#define blackwmap 0xa0000
#define cmapsz 8192

static int
vgacon_font_op(char *arg, int set)
{
#ifdef CAN_LOAD_EGA_FONTS
	int ch512 = video_mode_512ch;
	static int ch512enabled = 0;
	int i;
	char *charmap;
	int beg;
	unsigned short video_port_status = vga_video_port_reg + 6;
	int font_select = 0x00;

	if (vga_video_type == VIDEO_TYPE_EGAC || vga_video_type == VIDEO_TYPE_VGAC) {
		charmap = (char *)VGA_MAP_MEM(colourmap);
		beg = 0x0e;
#ifdef VGA_CAN_DO_64KB
		if (video_type == VIDEO_TYPE_VGAC)
			beg = 0x06;
#endif
	} else if (vga_video_type == VIDEO_TYPE_EGAM) {
		charmap = (char *)VGA_MAP_MEM(blackwmap);
		beg = 0x0a;
	} else
		return -EINVAL;
	
#ifdef BROKEN_GRAPHICS_PROGRAMS
	/*
	 * All fonts are loaded in slot 0 (0:1 for 512 ch)
	 */

	if (!arg)
		return -EINVAL;		/* Return to default font not supported */

	vga_font_is_default = 0;
	font_select = ch512 ? 0x04 : 0x00;
#else	
	/*
	 * The default font is kept in slot 0 and is never touched.
	 * A custom font is loaded in slot 2 (256 ch) or 2:3 (512 ch)
	 */

	if (set) {
		vga_font_is_default = !arg;
		if (!arg)
			ch512 = 0;		/* Default font is always 256 */
		font_select = arg ? (ch512 ? 0x0e : 0x0a) : 0x00;
	}

	if ( !vga_font_is_default )
		charmap += 4*cmapsz;
#endif

	cli();
	outb_p( 0x00, seq_port_reg );   /* First, the sequencer */
	outb_p( 0x01, seq_port_val );   /* Synchronous reset */
	outb_p( 0x02, seq_port_reg );
	outb_p( 0x04, seq_port_val );   /* CPU writes only to map 2 */
	outb_p( 0x04, seq_port_reg );
	outb_p( 0x07, seq_port_val );   /* Sequential addressing */
	outb_p( 0x00, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* Clear synchronous reset */

	outb_p( 0x04, gr_port_reg );    /* Now, the graphics controller */
	outb_p( 0x02, gr_port_val );    /* select map 2 */
	outb_p( 0x05, gr_port_reg );
	outb_p( 0x00, gr_port_val );    /* disable odd-even addressing */
	outb_p( 0x06, gr_port_reg );
	outb_p( 0x00, gr_port_val );    /* map start at A000:0000 */
	sti();
	
	if (arg) {
		if (set)
			for (i=0; i<cmapsz ; i++)
				vga_writeb(arg[i], charmap + i);
		else
			for (i=0; i<cmapsz ; i++)
				arg[i] = vga_readb(charmap + i);

		/*
		 * In 512-character mode, the character map is not contiguous if
		 * we want to remain EGA compatible -- which we do
		 */

		if (ch512) {
			charmap += 2*cmapsz;
			arg += cmapsz;
			if (set)
				for (i=0; i<cmapsz ; i++)
					vga_writeb(arg[i], charmap+i);
			else
				for (i=0; i<cmapsz ; i++)
					arg[i] = vga_readb(charmap+i);
		}
	}
	
	cli();
	outb_p( 0x00, seq_port_reg );   /* First, the sequencer */
	outb_p( 0x01, seq_port_val );   /* Synchronous reset */
	outb_p( 0x02, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* CPU writes to maps 0 and 1 */
	outb_p( 0x04, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* odd-even addressing */
	if (set) {
		outb_p( 0x03, seq_port_reg ); /* Character Map Select */
		outb_p( font_select, seq_port_val );
	}
	outb_p( 0x00, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* clear synchronous reset */

	outb_p( 0x04, gr_port_reg );    /* Now, the graphics controller */
	outb_p( 0x00, gr_port_val );    /* select map 0 for CPU */
	outb_p( 0x05, gr_port_reg );
	outb_p( 0x10, gr_port_val );    /* enable even-odd addressing */
	outb_p( 0x06, gr_port_reg );
	outb_p( beg, gr_port_val );     /* map starts at b800:0 or b000:0 */

	/* if 512 char mode is already enabled don't re-enable it. */
	if ((set)&&(ch512!=ch512enabled)) {	/* attribute controller */
		ch512enabled=ch512;
		/* 256-char: enable intensity bit
		   512-char: disable intensity bit */
		inb_p( video_port_status );	/* clear address flip-flop */
		outb_p ( 0x12, attrib_port ); /* color plane enable register */
		outb_p ( ch512 ? 0x07 : 0x0f, attrib_port );
		/* Wilton (1987) mentions the following; I don't know what
		   it means, but it works, and it appears necessary */
		inb_p( video_port_status );
		outb_p ( 0x20, attrib_port );
	}
	sti();

	return 0;
#else
	return -EINVAL;
#endif
}

/*
 * Adjust the screen to fit a font of a certain height
 */
static int
vgacon_adjust_height(unsigned fontheight)
{
	int rows, maxscan;
	unsigned char ovr, vde, fsr, curs, cure;

	if (fontheight > 32 || (vga_video_type != VIDEO_TYPE_VGAC &&
	    vga_video_type != VIDEO_TYPE_EGAC && vga_video_type != VIDEO_TYPE_EGAM))
		return -EINVAL;

	if (fontheight == video_font_height)
		return 0;

	video_font_height = fontheight;

	rows = video_scan_lines/fontheight;	/* Number of video rows we end up with */
	maxscan = rows*fontheight - 1;		/* Scan lines to actually display-1 */

	/* Reprogram the CRTC for the new font size
	   Note: the attempt to read the overflow register will fail
	   on an EGA, but using 0xff for the previous value appears to
	   be OK for EGA text modes in the range 257-512 scan lines, so I
	   guess we don't need to worry about it.

	   The same applies for the spill bits in the font size and cursor
	   registers; they are write-only on EGA, but it appears that they
	   are all don't care bits on EGA, so I guess it doesn't matter. */

	cli();
	outb_p( 0x07, vga_video_port_reg );		/* CRTC overflow register */
	ovr = inb_p(vga_video_port_val);
	outb_p( 0x09, vga_video_port_reg );		/* Font size register */
	fsr = inb_p(vga_video_port_val);
	outb_p( 0x0a, vga_video_port_reg );		/* Cursor start */
	curs = inb_p(vga_video_port_val);
	outb_p( 0x0b, vga_video_port_reg );		/* Cursor end */
	cure = inb_p(vga_video_port_val);
	sti();

	vde = maxscan & 0xff;			/* Vertical display end reg */
	ovr = (ovr & 0xbd) +			/* Overflow register */
	      ((maxscan & 0x100) >> 7) +
	      ((maxscan & 0x200) >> 3);
	fsr = (fsr & 0xe0) + (fontheight-1);    /*  Font size register */
	curs = (curs & 0xc0) + fontheight - (fontheight < 10 ? 2 : 3);
	cure = (cure & 0xe0) + fontheight - (fontheight < 10 ? 1 : 2);

	cli();
	outb_p( 0x07, vga_video_port_reg );		/* CRTC overflow register */
	outb_p( ovr, vga_video_port_val );
	outb_p( 0x09, vga_video_port_reg );		/* Font size */
	outb_p( fsr, vga_video_port_val );
	outb_p( 0x0a, vga_video_port_reg );		/* Cursor start */
	outb_p( curs, vga_video_port_val );
	outb_p( 0x0b, vga_video_port_reg );		/* Cursor end */
	outb_p( cure, vga_video_port_val );
	outb_p( 0x12, vga_video_port_reg );		/* Vertical display limit */
	outb_p( vde, vga_video_port_val );
	sti();

	vc_resize_all(rows, 0);			/* Adjust console size */
	return 0;
}

static int vgacon_get_font(struct vc_data *c, int *w, int *h, char *data)
{
	*w = 8;
	*h = video_font_height;
	return vgacon_font_op(data, 0);
}

static int vgacon_set_font(struct vc_data *c, int w, int h, char *data)
{
	int rc;
	if (w != 8 || h > 32)
		return -EINVAL;
	rc = vgacon_font_op(data, 1);
	if (!rc)
		rc = vgacon_adjust_height(h);
	return rc;
}

static int vgacon_scrolldelta(struct vc_data *c, int lines)
{
	/* FIXME: Better scrollback strategy, maybe move it to generic code
	 *	  and leave only vga_set_mem_top here.
	 */
	if (!lines)			/* Turn scrollback off */
		c->vc_visible_origin = c->vc_origin;
	else {
		int p = c->vc_visible_origin - vga_vram_base;
		p += lines * c->vc_size_row;
		if (p < 0)
			p = 0;
		c->vc_visible_origin = p + vga_vram_base;
		if (c->vc_visible_origin > c->vc_origin)
			c->vc_visible_origin = c->vc_origin;
	}
	vga_set_mem_top(c);
	return 1;
}

static int vgacon_set_origin(struct vc_data *c)
{
	if (vga_is_gfx)		/* We don't play origin tricks in graphic modes */
		return 0;
	c->vc_origin = c->vc_visible_origin = vga_vram_base;
	vga_set_mem_top(c);
	return 1;
}

static void vgacon_save_screen(struct vc_data *c)
{
	static int vga_bootup_console = 0;

	if (!vga_bootup_console) {
		/* This is a gross hack, but here is the only place we can
		 * set bootup console parameters without messing up generic
		 * console initialization routines.
		 */
		vga_bootup_console = 1;
		c->vc_x = ORIG_X;
		c->vc_y = ORIG_Y;
	}
	scr_memcpyw_from((u16 *) c->vc_screenbuf, (u16 *) c->vc_origin, c->vc_screenbuf_size);
}

static int vgacon_scroll(struct vc_data *c, int t, int b, int dir, int lines)
{
	unsigned long oldo;
	
	if (t || b != c->vc_rows || vga_is_gfx)
		return 0;

	if (c->vc_origin != c->vc_visible_origin)
		vgacon_scrolldelta(c, 0);

	/* FIXME: Handle scrolling down or by more lines? */
	if (!vga_hardscroll_enabled || dir != SM_UP || lines != 1)
		return 0;

	oldo = c->vc_origin;
	if (c->vc_scr_end + c->vc_size_row >= vga_vram_end) {
		scr_memcpyw((u16 *)vga_vram_base,
			    (u16 *)(oldo + c->vc_size_row),
			    c->vc_screenbuf_size - c->vc_size_row);
		c->vc_origin = vga_vram_base;
	} else
		c->vc_origin += c->vc_size_row;
	c->vc_visible_origin = c->vc_origin;
	c->vc_scr_end = c->vc_origin + c->vc_screenbuf_size;
	scr_memsetw((u16 *)(c->vc_scr_end - c->vc_size_row), c->vc_video_erase_char, c->vc_size_row);
	vga_set_mem_top(c);
	c->vc_pos = (c->vc_pos - oldo) + c->vc_origin;
	return 1;
}


/*
 *  The console `switch' structure for the VGA based console
 */

static int vgacon_dummy(struct vc_data *c)
{
	return 0;
}

#define DUMMY (void *) vgacon_dummy

struct consw vga_con = {
	vgacon_startup,
	vgacon_init,
	vgacon_deinit,
	DUMMY,				/* con_clear */
	DUMMY,				/* con_putc */
	DUMMY,				/* con_putcs */
	vgacon_cursor,
	vgacon_scroll,			/* con_scroll */
	DUMMY,				/* con_bmove */
	vgacon_switch,
	vgacon_blank,
	vgacon_get_font,
	vgacon_set_font,
	vgacon_set_palette,
	vgacon_scrolldelta,
	vgacon_set_origin,
	vgacon_save_screen
};
