/*
 *	We've been given MAC frame buffer info by the booter. Now go set it up
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/nubus.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/bootinfo.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/macintosh.h>
#include <linux/fb.h>


/* conditionalize these ?? */
#include "fbcon-mfb.h"
#include "fbcon-cfb2.h"
#include "fbcon-cfb4.h"
#include "fbcon-cfb8.h"

#define arraysize(x)    (sizeof(x)/sizeof(*(x)))

static struct fb_var_screeninfo macfb_defined={
	0,0,0,0,	/* W,H, W, H (virtual) load xres,xres_virtual*/
	0,0,		/* virtual -> visible no offset */
	8,		/* depth -> load bits_per_pixel */
	0,		/* greyscale ? */
	{0,0,0},	/* R */
	{0,0,0},	/* G */
	{0,0,0},	/* B */
	{0,0,0},	/* transparency */
	0,		/* standard pixel format */
	FB_ACTIVATE_NOW,
	274,195,	/* 14" monitor *Mikael Nykvist's anyway* */
	0,		/* The only way to accelerate a mac is .. */
	0L,0L,0L,0L,0L,
	0L,0L,0,	/* No sync info */
	FB_VMODE_NONINTERLACED,
	{0,0,0,0,0,0}
};

#define NUM_TOTAL_MODES		1
#define NUM_PREDEF_MODES	1

static struct display disp;
static struct fb_info fb_info;

static int inverse = 0;

struct macfb_par
{
	void *unused;
};

static int currcon = 0;
static int current_par_valid = 0;
struct macfb_par current_par;

static int mac_xres,mac_yres,mac_depth, mac_xbytes, mac_vxres;
static unsigned long mac_videobase;
static unsigned long mac_videosize;

	/*
	 * Open/Release the frame buffer device
	 */

static int macfb_open(struct fb_info *info, int user)
{
	/*
	 * Nothing, only a usage count for the moment
	 */
	MOD_INC_USE_COUNT;
	return(0);
}

static int macfb_release(struct fb_info *info, int user)
{
	MOD_DEC_USE_COUNT;
	return(0);
}

static void macfb_encode_var(struct fb_var_screeninfo *var, 
				struct macfb_par *par)
{
	memset(var, 0, sizeof(struct fb_var_screeninfo));
	var->xres=mac_xres;
	var->yres=mac_yres;
	var->xres_virtual=mac_vxres;
	var->yres_virtual=var->yres;
	var->xoffset=0;
	var->yoffset=0;
	var->bits_per_pixel = mac_depth;
	var->grayscale=0;
	var->transp.offset=0;
	var->transp.length=0;
	var->transp.msb_right=0;
	var->nonstd=0;
	var->activate=0;
	var->height= -1;
	var->width= -1;
	var->vmode=FB_VMODE_NONINTERLACED;
	var->pixclock=0;
	var->sync=0;
	var->left_margin=0;
	var->right_margin=0;
	var->upper_margin=0;
	var->lower_margin=0;
	var->hsync_len=0;
	var->vsync_len=0;
	return;
}


static void macfb_get_par(struct macfb_par *par)
{
	*par=current_par;
}

static void macfb_set_par(struct macfb_par *par)
{
	current_par_valid=1;
}

static int fb_update_var(int con, struct fb_info *info)
{
	return 0;
}

static int do_fb_set_var(struct fb_var_screeninfo *var, int isactive)
{
	struct macfb_par par;
	
	macfb_get_par(&par);
	macfb_encode_var(var, &par);
	return 0;
}

extern int console_loglevel;

static void macfb_encode_fix(struct fb_fix_screeninfo *fix, 
				struct macfb_par *par)
{
	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id,"Macintosh");

	/*
	 * X works, but screen wraps ... 
	 */
	fix->smem_start=(char *)(mac_videobase&PAGE_MASK);
	fix->smem_offset=(mac_videobase&~PAGE_MASK);
	fix->smem_len=PAGE_ALIGN(mac_videosize);
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->visual = FB_VISUAL_PSEUDOCOLOR;
	fix->xpanstep=0;
	fix->ypanstep=0;
	fix->ywrapstep=0;
	fix->line_length=mac_xbytes;
	return;
}

static int macfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
	struct macfb_par par;
	macfb_get_par(&par);
	macfb_encode_fix(fix, &par);
	return 0;
}

static int macfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	struct macfb_par par;
	if(con==-1)
	{
		macfb_get_par(&par);
		macfb_encode_var(var, &par);
	}
	else
		*var=fb_display[con].var;
	return 0;
}

static void macfb_set_disp(int con)
{
	struct fb_fix_screeninfo fix;
	struct display *display;
	
	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	/* used during initialization */

	macfb_get_fix(&fix, con, 0);

	display->screen_base = fix.smem_start+fix.smem_offset;
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->line_length = fix.line_length;
	display->next_line = fix.line_length;
	display->can_soft_blank = 0;
	display->inverse = inverse;

	switch (mac_depth) {
#ifdef FBCON_HAS_MFB
	    case 1:
		display->dispsw = &fbcon_mfb;
		break;
#endif
#ifdef FBCON_HAS_CFB2
	    case 2:
		display->dispsw = &fbcon_cfb2;
		break;
#endif
#ifdef FBCON_HAS_CFB4
	    case 4:
		display->dispsw = &fbcon_cfb4;
		break;
#endif
#ifdef FBCON_HAS_CFB8
	    case 8:
		display->dispsw = &fbcon_cfb8;
		break;
#endif
	    default:
		display->dispsw = NULL;
		break;
	}
}

static int macfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	int err;
	
	if ((err=do_fb_set_var(var, 1)))
		return err;
	return 0;
}

static int macfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
#if 0
	printk("macfb_get_cmap: not supported!\n");
	/* interferes with X11 */
	if (console_loglevel < 7)
		return -EINVAL;
	if (con == currcon) /* current console? */
		return fb_get_cmap(cmap, &fb_display[con].var, kspc, 0 /*offb_getcolreg*/, info);
	else if (fb_display[con].cmap.len) /* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(fb_display[con].var.bits_per_pixel),
		     cmap, kspc ? 0 : 2);
#endif
	return 0;

}

static int macfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
#if 0
	printk("macfb_set_cmap: not supported!\n");
	if (console_loglevel < 7)
		return -EINVAL;
	if (!fb_display[con].cmap.len) {	/* no colormap allocated? */
		if ((err = fb_alloc_cmap(&fb_display[con].cmap,
				 1<<fb_display[con].var.bits_per_pixel, 0)))
		return err;
	}
	if (con == currcon)			/* current console? */
		return fb_set_cmap(cmap, &fb_display[con].var, kspc, 1 /*offb_setcolreg*/, info);
	else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
#endif
	return 0;
}

static int macfb_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info)
{
	/* no panning */
	printk("macfb_pan: not supported!\n");
	return -EINVAL;
}

static int macfb_ioctl(struct inode *inode, struct file *file, 
		       unsigned int cmd, unsigned long arg, int con,
		       struct fb_info *info)
{
	printk("macfb_ioctl: not supported!\n");
	return -EINVAL;
}

static struct fb_ops macfb_ops = {
	macfb_open,
	macfb_release,
	macfb_get_fix,
	macfb_get_var,
	macfb_set_var,
	macfb_get_cmap,
	macfb_set_cmap,
	macfb_pan_display,
	macfb_ioctl
};

void macfb_setup(char *options, int *ints)
{
    char *this_opt;

    fb_info.fontname[0] = '\0';

    if (!options || !*options)
		return;
     
    for(this_opt=strtok(options,","); this_opt; this_opt=strtok(NULL,",")) {
	if (!*this_opt) continue;

	if (! strcmp(this_opt, "inverse"))
		inverse=1;
	else if (!strncmp(this_opt, "font:", 5)) {
	   strcpy(fb_info.fontname, this_opt+5);
	   printk("macfb_setup: option %s\n", this_opt);
	}
    }
}

static int macfb_switch(int con, struct fb_info *info)
{
	do_fb_set_var(&fb_display[con].var,1);
	currcon=con;
	return 0;
}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */

static void macfb_blank(int blank, struct fb_info *info)
{
	/* Not supported */
}

/*
 *	Nubus call back. This will give us our board identity and also
 *	other useful info we need later
 */
 
static int nubus_video_card(struct nubus_device_specifier *ns, int slot, struct nubus_type *nt)
{
	if(nt->category==NUBUS_CAT_DISPLAY)
		return 0;
	/* Claim all video cards. We don't yet do driver specifics though. */
	return -ENODEV;
}

static struct nubus_device_specifier nb_video={
	nubus_video_card,
	NULL
};

__initfunc(void macfb_init(void))
{
	/* nubus_remap the video .. */

	if (!MACH_IS_MAC) 
		return;

	mac_xres=mac_bi_data.dimensions&0xFFFF;
	mac_yres=(mac_bi_data.dimensions&0xFFFF0000)>>16;
	mac_depth=mac_bi_data.videodepth;
	mac_xbytes=mac_bi_data.videorow;
	mac_vxres = (mac_xbytes/mac_depth)*8;
	mac_videosize=mac_xbytes*mac_yres;
	mac_videobase=mac_bi_data.videoaddr;

	printk("macfb_init: xres %d yres %d bpp %d addr %x size %d \n",
		mac_xres, mac_yres, mac_depth, mac_videobase, mac_videosize);

	mac_debugging_penguin(4);
	
	/*
	 *	Fill in the available video resolution
	 */
	 
	 macfb_defined.xres=mac_xres;
	 macfb_defined.yres=mac_yres;
	 macfb_defined.xres_virtual=mac_vxres;
	 macfb_defined.yres_virtual=mac_yres;
	 macfb_defined.bits_per_pixel=mac_depth;
	 
	 
	/*
	 *	Let there be consoles..
	 */
	strcpy(fb_info.modename, "Macintosh Builtin ");
	fb_info.changevar = NULL;
	fb_info.node = -1;
	fb_info.fbops = &macfb_ops;
	fb_info.disp=&disp;
	fb_info.switch_con=&macfb_switch;
	fb_info.updatevar=&fb_update_var;
	fb_info.blank=&macfb_blank;
	do_fb_set_var(&macfb_defined,1);

	macfb_get_var(&disp.var, -1, &fb_info);
	macfb_set_disp(-1);

	/*
	 *	Register the nubus hook
	 */
	 
	register_nubus_device(&nb_video);

	if (register_framebuffer(&fb_info) < 0)
	{
		mac_boom(6);
		return;
	}

	printk("fb%d: %s frame buffer device using %ldK of video memory\n",
	       GET_FB_IDX(fb_info.node), fb_info.modename, mac_videosize>>10);
}

#if 0
/*
 * These two auxiliary debug functions should go away ASAP. Only usage: 
 * before the console output is up (after head.S come some other crucial
 * setup routines :-) 
 *
 * Now in debug.c ...
 */
#endif
