/* $Id: newport_con.c,v 1.14 1999/06/24 01:10:24 ulfc Exp $
 *
 * newport_con.c: Abscon for newport hardware
 * 
 * (C) 1998 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 * (C) 1999 Ulf Carlsson (ulfc@thepuffingruop.com)
 * 
 * This driver is based on sgicons.c and cons_newport.
 * 
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1997 Miguel de Icaza (miguel@nuclecu.unam.mx)
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/kd.h>
#include <linux/selection.h>
#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/vt_kern.h>
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <video/newport.h>
#define INCLUDE_LINUX_LOGO_DATA
#include <asm/linux_logo.h>

#include <video/font.h>

#define LOGO_W		80
#define LOGO_H		80

extern struct fbcon_font_desc font_vga_8x16;

#define FONT_DATA ((unsigned char *)font_vga_8x16.data)

extern struct newport_regs *npregs;

static int logo_active;
static int topscan;
static int xcurs_correction = 29;
static int newport_xsize;
static int newport_ysize;

#define BMASK(c) (c << 24)

#define RENDER(regs, cp) do { \
(regs)->go.zpattern = BMASK((cp)[0x0]); (regs)->go.zpattern = BMASK((cp)[0x1]); \
(regs)->go.zpattern = BMASK((cp)[0x2]); (regs)->go.zpattern = BMASK((cp)[0x3]); \
(regs)->go.zpattern = BMASK((cp)[0x4]); (regs)->go.zpattern = BMASK((cp)[0x5]); \
(regs)->go.zpattern = BMASK((cp)[0x6]); (regs)->go.zpattern = BMASK((cp)[0x7]); \
(regs)->go.zpattern = BMASK((cp)[0x8]); (regs)->go.zpattern = BMASK((cp)[0x9]); \
(regs)->go.zpattern = BMASK((cp)[0xa]); (regs)->go.zpattern = BMASK((cp)[0xb]); \
(regs)->go.zpattern = BMASK((cp)[0xc]); (regs)->go.zpattern = BMASK((cp)[0xd]); \
(regs)->go.zpattern = BMASK((cp)[0xe]); (regs)->go.zpattern = BMASK((cp)[0xf]); \
} while(0)

#define TESTVAL 0xdeadbeef
#define XSTI_TO_FXSTART(val) (((val) & 0xffff) << 11)

static inline void newport_render_background(int xstart, int ystart,
					     int xend, int yend, int ci)
{
    newport_wait();
    npregs->set.wrmask = 0xffffffff;
    npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			     NPORT_DMODE0_DOSETUP | NPORT_DMODE0_STOPX |
			     NPORT_DMODE0_STOPY);
    npregs->set.colori = ci;
    npregs->set.xystarti = (xstart << 16) | ((ystart + topscan) & 0x3ff);
    npregs->go.xyendi = ((xend + 7) << 16) | ((yend + topscan + 15) & 0x3ff);
}

static inline void newport_init_cmap(void)
{
    unsigned short i;
    
    for(i = 0; i < 16; i++) {
	newport_bfwait();
	newport_cmap_setaddr(npregs, color_table[i]);
	newport_cmap_setrgb(npregs,
			    default_red[i],
			    default_grn[i],
			    default_blu[i]);
    }
}

static inline void newport_show_logo(void)
{
    unsigned long i;

    for(i = 0; i < LINUX_LOGO_COLORS; i++) {
	newport_bfwait();
	newport_cmap_setaddr(npregs, i + 0x20);
	newport_cmap_setrgb(npregs,
			    linux_logo_red[i],
			    linux_logo_green[i],
			    linux_logo_blue[i]);
    }

    newport_wait();
    npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			     NPORT_DMODE0_CHOST);
    
    npregs->set.xystarti = ((newport_xsize - LOGO_W) << 16) | (0);
    npregs->set.xyendi = ((newport_xsize - 1) << 16);
    newport_wait();

    for (i = 0; i < LOGO_W * LOGO_H; i++)
	npregs->go.hostrw0 = linux_logo[i] << 24;
}

static inline void newport_clear_screen(int xstart, int ystart, int xend,
					int yend, int ci) {
    if (logo_active)
	return;

    newport_wait();
    npregs->set.wrmask = 0xffffffff;
    npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			     NPORT_DMODE0_DOSETUP | NPORT_DMODE0_STOPX |
			     NPORT_DMODE0_STOPY);
    npregs->set.colori = ci;
    npregs->set.xystarti = (xstart << 16) | ystart;
    npregs->go.xyendi = (xend << 16) | yend;
}

static inline void newport_clear_lines(int ystart, int yend, int ci)
{
    ystart = ((ystart << 4) + topscan) & 0x3ff;
    yend = ((yend << 4) + topscan + 15) & 0x3ff;    
    newport_clear_screen (0, ystart, 1280+63, yend, ci);
}

void newport_reset (void)
{
    unsigned short treg;    
    int i;
    
    newport_wait();
    treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
    newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg | VC2_CTRL_EVIDEO));

    treg = newport_vc2_get(npregs, VC2_IREG_CENTRY);
    newport_vc2_set(npregs, VC2_IREG_RADDR, treg);
    npregs->set.dcbmode = (NPORT_DMODE_AVC2 | VC2_REGADDR_RAM |
			   NPORT_DMODE_W2 | VC2_PROTOCOL);
    for(i = 0; i < 128; i++) {
	newport_bfwait();
	if (i == 92 || i == 94)
	    npregs->set.dcbdata0.byshort.s1 = 0xff00;
	else
	    npregs->set.dcbdata0.byshort.s1 = 0x0000;
    }

    newport_init_cmap();

    /* turn off popup plane */
    npregs->set.dcbmode = (DCB_XMAP0 | R_DCB_XMAP9_PROTOCOL |
                           XM9_CRS_CONFIG | NPORT_DMODE_W1);
    npregs->set.dcbdata0.bybytes.b3 &= ~XM9_PUPMODE;
    npregs->set.dcbmode = (DCB_XMAP1 | R_DCB_XMAP9_PROTOCOL |
                           XM9_CRS_CONFIG | NPORT_DMODE_W1);
    npregs->set.dcbdata0.bybytes.b3 &= ~XM9_PUPMODE;
    
    topscan = 0;
    npregs->cset.topscan = 0x3ff;
    npregs->cset.xywin = (4096 << 16) | 4096;

    /* Clear the screen. */
    newport_clear_screen(0,0,1280+63,1024,0);
}

/*
 * calculate the actual screen size by reading
 * the video timing out of the VC2
 */
void newport_get_screensize(void)
{
    int i,cols;
    unsigned short ventry,treg;
    unsigned short linetable[128]; /* should be enough */

    ventry = newport_vc2_get (npregs, VC2_IREG_VENTRY);
    newport_vc2_set(npregs, VC2_IREG_RADDR, ventry);
    npregs->set.dcbmode = (NPORT_DMODE_AVC2 | VC2_REGADDR_RAM |
			   NPORT_DMODE_W2 | VC2_PROTOCOL);
    for(i = 0; i < 128; i++) {
	newport_bfwait();
	linetable[i] = npregs->set.dcbdata0.byshort.s1;
    }

    newport_xsize = newport_ysize = 0;
    for (i = 0; linetable[i+1] && (i < sizeof(linetable)); i+=2) {
	cols = 0;
        newport_vc2_set(npregs, VC2_IREG_RADDR, linetable[i]);
        npregs->set.dcbmode = (NPORT_DMODE_AVC2 | VC2_REGADDR_RAM |
			       NPORT_DMODE_W2 | VC2_PROTOCOL);
	do {
	    newport_bfwait();
	    treg = npregs->set.dcbdata0.byshort.s1;
	    if ((treg & 1) == 0)
		cols += (treg >> 7) & 0xfe;
	    if ((treg & 0x80) == 0) {
		newport_bfwait();
		treg = npregs->set.dcbdata0.byshort.s1;
	    } 
	} while ((treg & 0x8000) == 0);
	if (cols) {
	    if (cols > newport_xsize)
		newport_xsize = cols;
	    newport_ysize += linetable[i+1];
	}
    }
    printk ("NG1: Screensize %dx%d\n",newport_xsize,newport_ysize);
}

static void newport_get_revisions(void)
{
    unsigned int tmp;
    unsigned int board_rev;
    unsigned int rex3_rev;
    unsigned int vc2_rev;
    unsigned int cmap_rev;
    unsigned int xmap9_rev;
    unsigned int bt445_rev;
    unsigned int bitplanes;

    rex3_rev = npregs->cset.status & NPORT_STAT_VERS;

    npregs->set.dcbmode = (DCB_CMAP0 | NCMAP_PROTOCOL |
                           NCMAP_REGADDR_RREG | NPORT_DMODE_W1);
    tmp = npregs->set.dcbdata0.bybytes.b3;
    cmap_rev = tmp & 7;
    board_rev = (tmp >> 4) & 7;
    bitplanes = ((board_rev > 1) && (tmp & 0x80)) ? 8 : 24; 

    npregs->set.dcbmode = (DCB_CMAP1 | NCMAP_PROTOCOL |
                           NCMAP_REGADDR_RREG | NPORT_DMODE_W1);
    tmp = npregs->set.dcbdata0.bybytes.b3;
    if ((tmp & 7) < cmap_rev)
	cmap_rev = (tmp & 7);

    vc2_rev = (newport_vc2_get(npregs, VC2_IREG_CONFIG) >> 5) & 7;

    npregs->set.dcbmode = (DCB_XMAP0 | R_DCB_XMAP9_PROTOCOL |
                           XM9_CRS_REVISION | NPORT_DMODE_W1);
    xmap9_rev = npregs->set.dcbdata0.bybytes.b3 & 7;

    npregs->set.dcbmode = (DCB_BT445 | BT445_PROTOCOL |
                           BT445_CSR_ADDR_REG | NPORT_DMODE_W1);
    npregs->set.dcbdata0.bybytes.b3 = BT445_REVISION_REG;
    npregs->set.dcbmode = (DCB_BT445 | BT445_PROTOCOL |
                           BT445_CSR_REVISION | NPORT_DMODE_W1);
    bt445_rev = (npregs->set.dcbdata0.bybytes.b3 >> 4) - 0x0a;

#define L(a)     (char)('A'+(a))
    printk ("NG1: Revision %d, %d bitplanes, REX3 revision %c, VC2 revision %c, xmap9 revision %c, cmap revision %c, bt445 revision %c\n",
	    board_rev,bitplanes,L(rex3_rev),L(vc2_rev), L(xmap9_rev),
	    L(cmap_rev ? (cmap_rev+1):0),L(bt445_rev));
#undef L

    if (board_rev == 3) /* I don't know all affected revisions */
	xcurs_correction = 21;
}

#ifdef MODULE
static const char *newport_startup(void)
#else
static const char * __init newport_startup(void)
#endif
{
    struct newport_regs *p;

    npregs = (struct newport_regs *) (KSEG1 + 0x1f0f0000);
	
    p = npregs;
    p->cset.config = NPORT_CFG_GD0;

    if(newport_wait()) {
	return NULL;
    }

    p->set.xstarti = TESTVAL; if(p->set._xstart.word != XSTI_TO_FXSTART(TESTVAL)) {
	return NULL;
    }

    newport_reset ();
    newport_get_revisions();
    newport_get_screensize();

    // gfx_init (display_desc);
    
    return "SGI Newport";
}

static void newport_init(struct vc_data *vc, int init)
{
    vc->vc_cols = newport_xsize / 8;
    vc->vc_rows = newport_ysize / 16;
    vc->vc_can_do_color = 1;
}

static void newport_clear(struct vc_data *vc, int sy, int sx, int height, int width)
{
    int xend = ((sx + width) << 3) - 1;
    int ystart = ((sy << 4) + topscan) & 0x3ff;
    int yend = (((sy + height) << 4) + topscan - 1) & 0x3ff;

    if (logo_active)
	return;
    
    if (ystart < yend) {
	newport_clear_screen(sx << 3, ystart, xend, yend,
			     (vc->vc_color & 0xf0) >> 4);
    } else {
	newport_clear_screen(sx << 3, ystart, xend, 1023,
			     (vc->vc_color & 0xf0) >> 4);
	newport_clear_screen(sx << 3, 0, xend, yend,
			     (vc->vc_color & 0xf0) >> 4);
    }
}

static void newport_putc(struct vc_data *vc, int charattr, int ypos, int xpos)
{
    unsigned char *p;
    
    p = &FONT_DATA[(charattr & 0xff) << 4];
    charattr = (charattr >> 8) & 0xff;
    xpos <<= 3;
    ypos <<= 4;

    newport_render_background(xpos, ypos, xpos, ypos, (charattr & 0xf0) >> 4);
    
    /* Set the color and drawing mode. */
    newport_wait();
    npregs->set.colori = charattr & 0xf;
    npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			     NPORT_DMODE0_STOPX | NPORT_DMODE0_ZPENAB |
			     NPORT_DMODE0_L32);
    
    /* Set coordinates for bitmap operation. */
    npregs->set.xystarti = (xpos << 16) | ((ypos + topscan) & 0x3ff);
    npregs->set.xyendi = ((xpos + 7) << 16);
    newport_wait();
    
    /* Go, baby, go... */
    RENDER(npregs, p);
}

static void newport_putcs(struct vc_data *vc, const unsigned short *s,
			  int count, int ypos, int xpos)
{
    int i;
    int charattr;
    unsigned char *p; 

    charattr = (*s >> 8) & 0xff;

    xpos <<= 3;
    ypos <<= 4;

    if (!logo_active)
	/* Clear the area behing the string */
	newport_render_background(xpos, ypos, xpos + ((count-1) << 3), ypos,
				  (charattr & 0xf0) >> 4);

    newport_wait();

    /* Set the color and drawing mode. */
    npregs->set.colori = charattr & 0xf;
    npregs->set.drawmode0 = (NPORT_DMODE0_DRAW | NPORT_DMODE0_BLOCK |
			     NPORT_DMODE0_STOPX | NPORT_DMODE0_ZPENAB |
			     NPORT_DMODE0_L32);
    
    for (i = 0; i < count; i++, xpos += 8) {
	p = &FONT_DATA[(s[i] & 0xff) << 4];

	newport_wait();

	/* Set coordinates for bitmap operation. */
	npregs->set.xystarti = (xpos << 16) | ((ypos + topscan) & 0x3ff);
	npregs->set.xyendi = ((xpos + 7) << 16);

	/* Go, baby, go... */
	RENDER(npregs, p);
    }
}

static void newport_cursor(struct vc_data *vc, int mode)
{
    unsigned short treg;
    int xcurs, ycurs;
    
    switch (mode) {
     case CM_ERASE:
	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg & ~(VC2_CTRL_ECDISP)));
	break;

     case CM_MOVE:
     case CM_DRAW:
	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg | VC2_CTRL_ECDISP));
	xcurs = (vc->vc_pos - vc->vc_visible_origin) / 2;
	ycurs = ((xcurs / vc->vc_cols) << 4) + 31;
	xcurs = ((xcurs % vc->vc_cols) << 3) + xcurs_correction;
	newport_vc2_set(npregs, VC2_IREG_CURSX, xcurs);
	newport_vc2_set(npregs, VC2_IREG_CURSY, ycurs);
    }
}

static int newport_switch(struct vc_data *vc)
{
    static int logo_drawn = 0;

    topscan = 0;
    npregs->cset.topscan = 0x3ff;

    if (!logo_drawn) {
	newport_show_logo();
	logo_drawn = 1;
	logo_active = 1;
    }

    return 1;
}

static int newport_blank(struct vc_data *c, int blank)
{
    unsigned short treg;
    
    if (blank == 0) {
	/* unblank console */
	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg | VC2_CTRL_EDISP));
    } else {
	/* blank console */
	treg = newport_vc2_get(npregs, VC2_IREG_CONTROL);
	newport_vc2_set(npregs, VC2_IREG_CONTROL, (treg & ~(VC2_CTRL_EDISP)));
    }
    return 1;
}

static int newport_font_op(struct vc_data *vc, struct console_font_op *f)
{
    return -ENOSYS;
}

static int newport_set_palette(struct vc_data *vc, unsigned char *table)
{
    return -EINVAL;
}

static int newport_scrolldelta(struct vc_data *vc, int lines)
{
    /* there is (nearly) no off-screen memory, so we can't scroll back */
    return 0;
}

static int newport_scroll(struct vc_data *vc, int t, int b, int dir, int lines)
{
    int count,x,y;
    unsigned short *s, *d;
    unsigned short chattr;

    logo_active = 0;	/* it's time to disable the logo now.. */

    if (t == 0 && b == vc->vc_rows) {
	if (dir == SM_UP) {
	    topscan = (topscan + (lines << 4)) & 0x3ff;
	    newport_clear_lines (vc->vc_rows-lines,vc->vc_rows-1,
				 (vc->vc_color & 0xf0) >> 4);
	} else {
	    topscan = (topscan + (-lines << 4)) & 0x3ff;
	    newport_clear_lines (0,lines-1, (vc->vc_color & 0xf0) >> 4);
	}
	npregs->cset.topscan = (topscan - 1) & 0x3ff;
	return 0;
    }
    
    count = (b-t-lines) * vc->vc_cols;
    if (dir == SM_UP) {
	x = 0; y = t;
	s = (unsigned short *)(vc->vc_origin + vc->vc_size_row*(t+lines));
	d = (unsigned short *)(vc->vc_origin + vc->vc_size_row*t);
	while (count--) {
	    chattr = scr_readw (s++);
	    if (chattr != scr_readw(d)) {
		newport_putc (vc, chattr, y, x);
		scr_writew (chattr, d);
	    }
	    d++;
	    if (++x == vc->vc_cols) {
		x = 0; y++;
	    }
	}
	d = (unsigned short *)(vc->vc_origin + vc->vc_size_row*(b-lines));
	x = 0; y = b-lines;
	for (count = 0; count < (lines * vc->vc_cols); count++) {
	    if (scr_readw(d) != vc->vc_video_erase_char) {
		newport_putc (vc, chattr, y, x);
		scr_writew (vc->vc_video_erase_char, d);
	    }
	    d++;
	    if (++x == vc->vc_cols) {
		x = 0; y++;
	    }
	}
    } else {
	x = vc->vc_cols-1; y = b-1;
	s = (unsigned short *)(vc->vc_origin + vc->vc_size_row*(b-lines)-2);
	d = (unsigned short *)(vc->vc_origin + vc->vc_size_row*b-2);
	while (count--) {
	    chattr = scr_readw (s--);
	    if (chattr != scr_readw(d)) {
		newport_putc (vc, chattr, y, x);
		scr_writew (chattr, d);
	    }
	    d--;
	    if (x-- == 0) {
		x = vc->vc_cols-1; y--;
	    }
	}
	d = (unsigned short *)(vc->vc_origin + vc->vc_size_row*t);
	x = 0; y = t;
	for (count = 0; count < (lines * vc->vc_cols); count++) {
	    if (scr_readw(d) != vc->vc_video_erase_char) {
		newport_putc (vc, vc->vc_video_erase_char, y, x);
		scr_writew (vc->vc_video_erase_char, d);
	    }
	    d++;
	    if (++x == vc->vc_cols) {
		x = 0; y++;
	    }
	}
    }
    return 1;
}

static void newport_bmove(struct vc_data *vc, int sy, int sx, int dy, int dx, int h, int w)
{
    short xs, ys, xe, ye, xoffs, yoffs, tmp;

    xs = sx << 3; xe = ((sx+w) << 3)-1;
    /*
     * as bmove is only used to move stuff around in the same line
     * (h == 1), we don't care about wrap arounds caused by topscan != 0
     */
    ys = ((sy << 4) + topscan) & 0x3ff; ye = (((sy+h) << 4)-1+topscan) & 0x3ff;
    xoffs = (dx - sx) << 3;
    yoffs = (dy - sy) << 4;
    if (xoffs > 0) {
	/* move to the right, exchange starting points */
	tmp = xe; xe = xs; xs = tmp;
    }
    newport_wait();
    npregs->set.drawmode0 = (NPORT_DMODE0_S2S | NPORT_DMODE0_BLOCK |
			     NPORT_DMODE0_DOSETUP | NPORT_DMODE0_STOPX |
			     NPORT_DMODE0_STOPY);
    npregs->set.xystarti = (xs << 16) | ys;
    npregs->set.xyendi = (xe << 16) | ye;
    npregs->go.xymove = (xoffs << 16) | yoffs;
}

static int newport_dummy(struct vc_data *c)
{
    return 0;
}

#define DUMMY (void *) newport_dummy

struct consw newport_con = {
    newport_startup,
    newport_init,
    DUMMY,                          /* con_deinit */
    newport_clear,
    newport_putc,
    newport_putcs,
    newport_cursor,
    newport_scroll,
    newport_bmove,
    newport_switch,
    newport_blank,
    newport_font_op,
    newport_set_palette,
    newport_scrolldelta,
    DUMMY, /* newport_set_origin, */
    DUMMY, /* newport_save_screen */
    NULL, /* newport_build_attr */
    NULL  /* newport_invert_region */
};

#ifdef MODULE

int init_module(void) {
    if (!newport_startup()) 
       printk("Error loading SGI Newport Console driver\n");
    else 
       printk("Loading SGI Newport Console Driver\n");

    take_over_console(&newport_con,0,MAX_NR_CONSOLES-1,1);

    return 0;
}

int cleanup_module(void) {
    printk("Unloading SGI Newport Console Driver\n");
    return 0;
}

#endif
