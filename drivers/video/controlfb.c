/*
 *  controlfb.c -- frame buffer device for the PowerMac 'control' display
 *
 *  Created 12 July 1998 by Dan Jacobowitz <dan@debian.org>
 *  Copyright (C) 1998 Dan Jacobowitz
 *  Copyright (C) 2001 Takashi Oe
 *
 *  Mmap code by Michel Lanners <mlan@cpu.lu>
 *
 *  Frame buffer structure from:
 *    drivers/video/chipsfb.c -- frame buffer device for
 *    Chips & Technologies 65550 chip.
 *
 *    Copyright (C) 1998 Paul Mackerras
 *
 *    This file is derived from the Powermac "chips" driver:
 *    Copyright (C) 1997 Fabio Riccardi.
 *    And from the frame buffer device for Open Firmware-initialized devices:
 *    Copyright (C) 1997 Geert Uytterhoeven.
 *
 *  Hardware information from:
 *    control.c: Console support for PowerMac "control" display adaptor.
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/selection.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/nvram.h>
#ifdef CONFIG_FB_COMPAT_XPMAC
#include <asm/vc_ioctl.h>
#endif
#include <linux/adb.h>
#include <linux/cuda.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pgtable.h>
#include <asm/btext.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb32.h>
#include <video/macmodes.h>

#include "controlfb.h"

struct fb_par_control {
	int	vmode, cmode;
	int	xres, yres;
	int	vxres, vyres;
	int	xoffset, yoffset;
	int	pitch;
	struct control_regvals	regvals;
	unsigned long sync;
	unsigned char ctrl;
};

#define DIRTY(z) ((x)->z != (y)->z)
#define DIRTY_CMAP(z) (memcmp(&((x)->z), &((y)->z), sizeof((y)->z)))
static inline int PAR_EQUAL(struct fb_par_control *x, struct fb_par_control *y)
{
	int i, results;

	results = 1;
	for (i = 0; i < 3; i++)
		results &= !DIRTY(regvals.clock_params[i]);
	if (!results)
		return 0;
	for (i = 0; i < 16; i++)
		results &= !DIRTY(regvals.regs[i]);
	if (!results)
		return 0;
	return (!DIRTY(cmode) && !DIRTY(xres) && !DIRTY(yres)
		&& !DIRTY(vxres) && !DIRTY(vyres));
}
static inline int VAR_MATCH(struct fb_var_screeninfo *x, struct fb_var_screeninfo *y)
{
	return (!DIRTY(bits_per_pixel) && !DIRTY(xres)
		&& !DIRTY(yres) && !DIRTY(xres_virtual)
		&& !DIRTY(yres_virtual)
		&& !DIRTY_CMAP(red) && !DIRTY_CMAP(green) && !DIRTY_CMAP(blue));
}

struct fb_info_control {
	struct fb_info			info;
	struct display			display;
	struct fb_par_control		par;
	struct {
		__u8 red, green, blue;
	}			palette[256];
	
	struct cmap_regs	*cmap_regs;
	unsigned long		cmap_regs_phys;
	
	struct control_regs	*control_regs;
	unsigned long		control_regs_phys;
	unsigned long		control_regs_size;
	
	__u8			*frame_buffer;
	unsigned long		frame_buffer_phys;
	unsigned long		fb_orig_base;
	unsigned long		fb_orig_size;

	int			control_use_bank2;
	unsigned long		total_vram;
	unsigned char		vram_attr;
	union {
#ifdef FBCON_HAS_CFB16
		u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB32
		u32 cfb32[16];
#endif
	} fbcon_cmap;
};

/* control register access macro */
#define CNTRL_REG(INFO,REG) (&(((INFO)->control_regs-> ## REG).r))


/******************** Prototypes for exported functions ********************/
/*
 * struct fb_ops
 */
static int control_get_fix(struct fb_fix_screeninfo *fix, int con,
	struct fb_info *info);
static int control_get_var(struct fb_var_screeninfo *var, int con,
	struct fb_info *info);
static int control_set_var(struct fb_var_screeninfo *var, int con,
	struct fb_info *info);
static int control_pan_display(struct fb_var_screeninfo *var, int con,
	struct fb_info *info);
static int control_get_cmap(struct fb_cmap *cmap, int kspc, int con,
	struct fb_info *info);
static int control_set_cmap(struct fb_cmap *cmap, int kspc, int con,
	struct fb_info *info);
static int controlfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
	u_int transp, struct fb_info *info);
static int controlfb_blank(int blank_mode, struct fb_info *info);
static int control_mmap(struct fb_info *info, struct file *file,
	struct vm_area_struct *vma);

/*
 * low level fbcon ops
 */
static int controlfb_switch(int con, struct fb_info *info);
static int controlfb_updatevar(int con, struct fb_info *info);

/*
 * low level cmap set/get ops
 */
static int controlfb_getcolreg(u_int regno, u_int *red, u_int *green,
	u_int *blue, u_int *transp, struct fb_info *info);

/*
 * inititialization
 */
int control_init(void);
void control_setup(char *);

/*
 * low level fbcon revc ops
 */
static void control_cfb16_revc(struct display *p, int xx, int yy);
static void control_cfb32_revc(struct display *p, int xx, int yy);


/******************** Prototypes for internal functions **********************/

static void do_install_cmap(struct display *disp, struct fb_info *info);
static void set_control_clock(unsigned char *params);
static int init_control(struct fb_info_control *p);
static void control_set_hardware(struct fb_info_control *p,
	struct fb_par_control *par);
static int control_of_init(struct device_node *dp);
static void control_par_to_fix(struct fb_par_control *par,
	struct fb_fix_screeninfo *fix, struct fb_info_control *p);
static void control_set_dispsw(struct display *disp, int cmode,
	struct fb_info_control *p);
static void find_vram_size(struct fb_info_control *p);
static int read_control_sense(struct fb_info_control *p);
static int calc_clock_params(unsigned long clk, unsigned char *param);
static int control_var_to_par(struct fb_var_screeninfo *var,
	struct fb_par_control *par, const struct fb_info *fb_info);
static inline void control_par_to_var(struct fb_par_control *par,
	struct fb_var_screeninfo *var);
static void control_par_to_fix(struct fb_par_control *par,
	struct fb_fix_screeninfo *fix, struct fb_info_control *p);
static void control_par_to_display(struct fb_par_control *par,
	struct display *disp, struct fb_fix_screeninfo *fix,
	struct fb_info_control *p);
static void control_set_dispsw(struct display *disp, int cmode,
	struct fb_info_control *p);
static void control_init_info(struct fb_info *info, struct fb_info_control *p);
static void control_cleanup(void);


/************************** Internal variables *******************************/

static struct fb_info_control *control_fb;

static char fontname[40] __initdata = { 0 };
static int default_vmode __initdata = VMODE_NVRAM;
static int default_cmode __initdata = CMODE_NVRAM;


static struct fb_ops controlfb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	control_get_fix,
	fb_get_var:	control_get_var,
	fb_set_var:	control_set_var,
	fb_get_cmap:	control_get_cmap,
	fb_set_cmap:	control_set_cmap,
	fb_setcolreg:	control_setcolreg,
	fb_pan_display:	control_pan_display,
	fb_blank:	controlfb_blank,
	fb_mmap:	control_mmap,
};


/********************  The functions for controlfb_ops ********************/

#ifdef MODULE
MODULE_LICENSE("GPL");

int init_module(void)
{
	struct device_node *dp;

	dp = find_devices("control");
	if (dp != 0 && !control_of_init(dp))
		return 0;

	return -ENXIO;
}

void cleanup_module(void)
{
	control_cleanup();
}
#endif

/*********** Providing our information to the user ************/

static int control_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;

	if(con == -1) {
		control_par_to_fix(&p->par, fix, p);
	} else {
		struct fb_par_control par;
		
		control_var_to_par(&fb_display[con].var, &par, info);
		control_par_to_fix(&par, fix, p);
	}
	return 0;
}

static int control_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;

	if(con == -1) {
		control_par_to_var(&p->par, var);
	} else {
		*var = fb_display[con].var;
	}
	return 0;
}


/*
 * Sets everything according to var
 */
static int control_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;
	struct display *disp;
	struct fb_par_control par;
	int depthchange, err;
	int activate = var->activate;

	if((err = control_var_to_par(var, &par, info))) {
		if (con < 0)
			printk (KERN_ERR "control_set_var: error calling"
					 " control_var_to_par: %d.\n", err);
		return err;
	}
	
	control_par_to_var(&par, var);
	
	if ((activate & FB_ACTIVATE_MASK) != FB_ACTIVATE_NOW)
		return 0;

	disp = (con >= 0) ? &fb_display[con] : info->disp;

	depthchange = (disp->var.bits_per_pixel != var->bits_per_pixel);
	if(!VAR_MATCH(&disp->var, var)) {
		struct fb_fix_screeninfo	fix;
		control_par_to_fix(&par, &fix, p);
		control_par_to_display(&par, disp, &fix, p);
		if(info->changevar)
			(*info->changevar)(con);
	} else
		disp->var = *var;

	if (con == info->currcon) {
		control_set_hardware(p, &par);
		if(depthchange) {
			if((err = fb_alloc_cmap(&disp->cmap, 0, 0)))
				return err;
			do_install_cmap(disp, info);
		}
	}

	return 0;
}


/*
 * Set screen start address according to var offset values
 */
static inline void set_screen_start(int xoffset, int yoffset,
	struct fb_info_control *p)
{
	struct fb_par_control *par = &p->par;

	par->xoffset = xoffset;
	par->yoffset = yoffset;
	out_le32(CNTRL_REG(p,start_addr),
		 par->yoffset * par->pitch + (par->xoffset << par->cmode));
}


static int control_pan_display(struct fb_var_screeninfo *var, int con,
			     struct fb_info *info)
{
	unsigned int xoffset, hstep;
	struct fb_info_control *p = (struct fb_info_control *)info;
	struct fb_par_control *par = &p->par;

	/*
	 * make sure start addr will be 32-byte aligned
	 */
	hstep = 0x1f >> par->cmode;
	xoffset = (var->xoffset + hstep) & ~hstep;

	if (xoffset+par->xres > par->vxres ||
	    var->yoffset+par->yres > par->vyres)
		return -EINVAL;

	set_screen_start(xoffset, var->yoffset, p);

	return 0;
}

static int control_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info)
{
	if (con == info->currcon)		/* current console? */
		return fb_get_cmap(cmap, kspc, controlfb_getcolreg, info);
	if (fb_display[con].cmap.len)	/* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0: 2);
	else {
		int size = fb_display[con].var.bits_per_pixel == 16 ? 32 : 256;
		fb_copy_cmap(fb_default_cmap(size), cmap, kspc ? 0 : 2);
	}
	return 0;
}

static int control_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
	struct display *disp = (con < 0)? info->disp: &fb_display[con];
	int err, size = disp->var.bits_per_pixel == 16 ? 32 : 256;

	if (disp->cmap.len != size) {
		err = fb_alloc_cmap(&disp->cmap, size, 0);
		if (err)
			return err;
	}
	if (con == info->currcon)
		return fb_set_cmap(cmap, kspc, info);
	fb_copy_cmap(cmap, &disp->cmap, kspc ? 0 : 1);
	return 0;
}


/*
 * Private mmap since we want to have a different caching on the framebuffer
 * for controlfb.
 * Note there's no locking in here; it's done in fb_mmap() in fbmem.c.
 */
static int control_mmap(struct fb_info *info, struct file *file,
                       struct vm_area_struct *vma)
{
       struct fb_ops *fb = info->fbops;
       struct fb_fix_screeninfo fix;
       struct fb_var_screeninfo var;
       unsigned long off, start;
       u32 len;

       fb->fb_get_fix(&fix, PROC_CONSOLE(info), info);
       off = vma->vm_pgoff << PAGE_SHIFT;

       /* frame buffer memory */
       start = fix.smem_start;
       len = PAGE_ALIGN((start & ~PAGE_MASK)+fix.smem_len);
       if (off >= len) {
               /* memory mapped io */
               off -= len;
               fb->fb_get_var(&var, PROC_CONSOLE(info), info);
               if (var.accel_flags)
                       return -EINVAL;
               start = fix.mmio_start;
               len = PAGE_ALIGN((start & ~PAGE_MASK)+fix.mmio_len);
               pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE|_PAGE_GUARDED;
       } else {
               /* framebuffer */
               pgprot_val(vma->vm_page_prot) |= _PAGE_WRITETHRU;
       }
       start &= PAGE_MASK;
       if ((vma->vm_end - vma->vm_start + off) > len)
       		return -EINVAL;
       off += start;
       vma->vm_pgoff = off >> PAGE_SHIFT;
       if (io_remap_page_range(vma, vma->vm_start, off,
           vma->vm_end - vma->vm_start, vma->vm_page_prot))
               return -EAGAIN;

       return 0;
}


/********************  End of controlfb_ops implementation  ******************/

/*
 * low level fbcon ops
 */

static int controlfb_switch(int con, struct fb_info *info)
{
	struct fb_info_control	*p = (struct fb_info_control *)info;
	struct fb_par_control	par;

	if (info->currcon >= 0 && fb_display[info->currcon].cmap.len)
		fb_get_cmap(&fb_display[info->currcon].cmap, 1, controlfb_getcolreg,
			    info);
	info->currcon = con;

	fb_display[con].var.activate = FB_ACTIVATE_NOW;
	control_var_to_par(&fb_display[con].var, &par, info);
	control_set_hardware(p, &par);
	control_set_dispsw(&fb_display[con], par.cmode, p);
	do_install_cmap(&fb_display[con], info);

	return 1;
}


static int controlfb_updatevar(int con, struct fb_info *info)
{
	struct fb_var_screeninfo *var = &fb_display[con].var;
	struct fb_info_control *p = (struct fb_info_control *) info;

	set_screen_start(var->xoffset, var->yoffset, p);

	return 0;
}


static int controlfb_blank(int blank_mode, struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;
	unsigned ctrl;

	ctrl = ld_le32(CNTRL_REG(p,ctrl));
	if (blank_mode > 0)
		switch (blank_mode - 1) {
		case VESA_VSYNC_SUSPEND:
			ctrl &= ~3;
			break;
		case VESA_HSYNC_SUSPEND:
			ctrl &= ~0x30;
			break;
		case VESA_POWERDOWN:
			ctrl &= ~0x33;
			/* fall through */
		case VESA_NO_BLANKING:
			ctrl |= 0x400;
			break;
		default:
			break;
		}
	else {
		ctrl &= ~0x400;
		ctrl |= 0x33;
	}
	out_le32(CNTRL_REG(p,ctrl), ctrl);

	return 0;
}


/*
 * low level cmap set/get ops
 */

static int controlfb_getcolreg(u_int regno, u_int *red, u_int *green,
			     u_int *blue, u_int *transp, struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;

	if (regno > 255)
		return 1;
	*red = (p->palette[regno].red<<8) | p->palette[regno].red;
	*green = (p->palette[regno].green<<8) | p->palette[regno].green;
	*blue = (p->palette[regno].blue<<8) | p->palette[regno].blue;
	*transp = 0;
	return 0;
}

static int controlfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			     u_int transp, struct fb_info *info)
{
	struct fb_info_control *p = (struct fb_info_control *) info;
	u_int i;
	__u8 r, g, b;

	if (regno > 255)
		return 1;

	r = red >> 8;
	g = green >> 8;
	b = blue >> 8;

	p->palette[regno].red = r;
	p->palette[regno].green = g;
	p->palette[regno].blue = b;

	out_8(&p->cmap_regs->addr, regno);	/* tell clut what addr to fill	*/
	out_8(&p->cmap_regs->lut, r);		/* send one color channel at	*/
	out_8(&p->cmap_regs->lut, g);		/* a time...			*/
	out_8(&p->cmap_regs->lut, b);

	if (regno < 16)
		switch (p->par.cmode) {
#ifdef FBCON_HAS_CFB16
			case CMODE_16:
				p->fbcon_cmap.cfb16[regno] = (regno << 10) | (regno << 5) | regno;
				break;
#endif
#ifdef FBCON_HAS_CFB32
			case CMODE_32:
				i = (regno << 8) | regno;
				p->fbcon_cmap.cfb32[regno] = (i << 16) | i;
				break;
#endif
		}
	return 0;
}

static void do_install_cmap(struct display *disp, struct fb_info *info)
{
	if (disp->cmap.len)
		fb_set_cmap(&disp->cmap, 1, controlfb_setcolreg,
			    info);
	else {
		int size = disp->var.bits_per_pixel == 16 ? 32 : 256;
		fb_set_cmap(fb_default_cmap(size), 1, controlfb_setcolreg,
			    info);
	}
}


static void set_control_clock(unsigned char *params)
{
#ifdef CONFIG_ADB_CUDA
	struct adb_request req;
	int i;

	for (i = 0; i < 3; ++i) {
		cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC,
			     0x50, i + 1, params[i]);
		while (!req.complete)
			cuda_poll();
	}
#endif	
}


/*
 * finish off the driver initialization and register
 */
static int __init init_control(struct fb_info_control *p)
{
	int full, sense, vmode, cmode, vyres;
	struct fb_var_screeninfo var;

	printk(KERN_INFO "controlfb: ");

	full = p->total_vram == 0x400000;

	/* Try to pick a video mode out of NVRAM if we have one. */
	if (default_cmode == CMODE_NVRAM){
		cmode = nvram_read_byte(NV_CMODE);
		if(cmode < CMODE_8 || cmode > CMODE_32)
			cmode = CMODE_8;
	} else
		cmode=default_cmode;

	if (default_vmode == VMODE_NVRAM) {
		vmode = nvram_read_byte(NV_VMODE);
		if (vmode < 1 || vmode > VMODE_MAX ||
		    control_mac_modes[vmode - 1].m[full] < cmode) {
			sense = read_control_sense(p);
			printk("Monitor sense value = 0x%x, ", sense);
			vmode = mac_map_monitor_sense(sense);
			if (control_mac_modes[vmode - 1].m[full] < cmode)
				vmode = VMODE_640_480_60;
		}
	} else {
		vmode=default_vmode;
		if (control_mac_modes[vmode - 1].m[full] < cmode) {
			if (cmode > CMODE_8)
				cmode--;
			else
				vmode = VMODE_640_480_60;
		}
	}

	if (mac_vmode_to_var(vmode, cmode, &var) < 0) {
		/* This shouldn't happen! */
		printk("mac_vmode_to_var(%d, %d,) failed\n", vmode, cmode);
try_again:
		vmode = VMODE_640_480_60;
		cmode = CMODE_8;
		if (mac_vmode_to_var(vmode, cmode, &var) < 0) {
			printk(KERN_ERR "controlfb: mac_vmode_to_var() failed\n");
			return -ENXIO;
		}
		printk(KERN_INFO "controlfb: ");
	}
	printk("using video mode %d and color mode %d.\n", vmode, cmode);

	vyres = (p->total_vram - CTRLFB_OFF) / (var.xres << cmode);
	if (vyres > var.yres)
		var.yres_virtual = vyres;

	control_init_info(&p->info, p);
	p->info.currcon = -1;
	var.activate = FB_ACTIVATE_NOW;

	if (control_set_var(&var, -1, &p->info) < 0) {
		if (vmode != VMODE_640_480_60 || cmode != CMODE_8)
			goto try_again;
		printk(KERN_ERR "controlfb: initilization failed\n");
		return -ENXIO;
	}

	p->info.flags = FBINFO_FLAG_DEFAULT;
	if (register_framebuffer(&p->info) < 0)
		return -ENXIO;
	
	printk(KERN_INFO "fb%d: control display adapter\n", GET_FB_IDX(p->info.node));	

	return 0;
}

#define RADACAL_WRITE(a,d) \
	out_8(&p->cmap_regs->addr, (a)); \
	out_8(&p->cmap_regs->dat,   (d))

/* Now how about actually saying, Make it so! */
/* Some things in here probably don't need to be done each time. */
static void control_set_hardware(struct fb_info_control *p, struct fb_par_control *par)
{
	struct control_regvals	*r;
	volatile struct preg	*rp;
	int			i, cmode;

	if (PAR_EQUAL(&p->par, par)) {
		/*
		 * check if only xoffset or yoffset differs.
		 * this prevents flickers in typical VT switch case.
		 */
		if (p->par.xoffset != par->xoffset ||
		    p->par.yoffset != par->yoffset)
			set_screen_start(par->xoffset, par->yoffset, p);
			
		return;
	}
	
	p->par = *par;
	cmode = p->par.cmode;
	r = &par->regvals;
	
	/* Turn off display */
	out_le32(CNTRL_REG(p,ctrl), 0x400 | par->ctrl);
	
	set_control_clock(r->clock_params);
	
	RADACAL_WRITE(0x20, r->radacal_ctrl);
	RADACAL_WRITE(0x21, p->control_use_bank2 ? 0 : 1);
	RADACAL_WRITE(0x10, 0);
	RADACAL_WRITE(0x11, 0);

	rp = &p->control_regs->vswin;
	for (i = 0; i < 16; ++i, ++rp)
		out_le32(&rp->r, r->regs[i]);
	
	out_le32(CNTRL_REG(p,pitch), par->pitch);
	out_le32(CNTRL_REG(p,mode), r->mode);
	out_le32(CNTRL_REG(p,vram_attr), p->vram_attr);
	out_le32(CNTRL_REG(p,start_addr), par->yoffset * par->pitch
		 + (par->xoffset << cmode));
	out_le32(CNTRL_REG(p,rfrcnt), 0x1e5);
	out_le32(CNTRL_REG(p,intr_ena), 0);

	/* Turn on display */
	out_le32(CNTRL_REG(p,ctrl), par->ctrl);

#ifdef CONFIG_FB_COMPAT_XPMAC
	/* And let the world know the truth. */
	if (!console_fb_info || console_fb_info == &p->info) {
		display_info.height = p->par.yres;
		display_info.width = p->par.xres;
		display_info.depth = (cmode == CMODE_32) ? 32 :
			((cmode == CMODE_16) ? 16 : 8);
		display_info.pitch = p->par.pitch;
		display_info.mode = p->par.vmode;
		strncpy(display_info.name, "control",
			sizeof(display_info.name));
		display_info.fb_address = p->frame_buffer_phys + CTRLFB_OFF;
		display_info.cmap_adr_address = p->cmap_regs_phys;
		display_info.cmap_data_address = p->cmap_regs_phys + 0x30;
		display_info.disp_reg_address = p->control_regs_phys;
		console_fb_info = &p->info;
	}
#endif /* CONFIG_FB_COMPAT_XPMAC */
#ifdef CONFIG_BOOTX_TEXT
	btext_update_display(p->frame_buffer_phys + CTRLFB_OFF,
			     p->par.xres, p->par.yres,
			     (cmode == CMODE_32? 32: cmode == CMODE_16? 16: 8),
			     p->par.pitch);
#endif /* CONFIG_BOOTX_TEXT */
}


/*
 * Called from fbmem.c for probing & intializing
 */
int __init control_init(void)
{
	struct device_node *dp;

	dp = find_devices("control");
	if (dp != 0 && !control_of_init(dp))
		return 0;

	return -ENXIO;
}


/* Work out which banks of VRAM we have installed. */
/* danj: I guess the card just ignores writes to nonexistant VRAM... */

static void __init find_vram_size(struct fb_info_control *p)
{
	int bank1, bank2;

	/*
	 * Set VRAM in 2MB (bank 1) mode
	 * VRAM Bank 2 will be accessible through offset 0x600000 if present
	 * and VRAM Bank 1 will not respond at that offset even if present
	 */
	out_le32(CNTRL_REG(p,vram_attr), 0x31);

	out_8(&p->frame_buffer[0x600000], 0xb3);
	out_8(&p->frame_buffer[0x600001], 0x71);
	asm volatile("eieio; dcbf 0,%0" : : "r" (&p->frame_buffer[0x600000])
					: "memory" );
	mb();
	asm volatile("eieio; dcbi 0,%0" : : "r" (&p->frame_buffer[0x600000])
					: "memory" );
	mb();

	bank2 = (in_8(&p->frame_buffer[0x600000]) == 0xb3)
		&& (in_8(&p->frame_buffer[0x600001]) == 0x71);

	/*
	 * Set VRAM in 2MB (bank 2) mode
	 * VRAM Bank 1 will be accessible through offset 0x000000 if present
	 * and VRAM Bank 2 will not respond at that offset even if present
	 */
	out_le32(CNTRL_REG(p,vram_attr), 0x39);

	out_8(&p->frame_buffer[0], 0x5a);
	out_8(&p->frame_buffer[1], 0xc7);
	asm volatile("eieio; dcbf 0,%0" : : "r" (&p->frame_buffer[0])
					: "memory" );
	mb();
	asm volatile("eieio; dcbi 0,%0" : : "r" (&p->frame_buffer[0])
					: "memory" );
	mb();

	bank1 = (in_8(&p->frame_buffer[0]) == 0x5a)
		&& (in_8(&p->frame_buffer[1]) == 0xc7);

	if (bank2) {
		if (!bank1) {
			/*
			 * vram bank 2 only
			 */
			p->control_use_bank2 = 1;
			p->vram_attr = 0x39;
			p->frame_buffer += 0x600000;
			p->frame_buffer_phys += 0x600000;
		} else {
			/*
			 * 4 MB vram
			 */
			p->vram_attr = 0x51;
		}
	} else {
		/*
		 * vram bank 1 only
		 */
		p->vram_attr = 0x31;
	}

        p->total_vram = (bank1 + bank2) * 0x200000;

	printk(KERN_INFO "controlfb: VRAM Total = %dMB "
			"(%dMB @ bank 1, %dMB @ bank 2)\n",
			(bank1 + bank2) << 1, bank1 << 1, bank2 << 1);
}


/*
 * find "control" and initialize
 */
static int __init control_of_init(struct device_node *dp)
{
	struct fb_info_control	*p;
	unsigned long		addr;
	int			i;

	if (control_fb) {
		printk(KERN_ERR "controlfb: only one control is supported\n");
		return -ENXIO;
	}
	if(dp->n_addrs != 2) {
		printk(KERN_ERR "expecting 2 address for control (got %d)", dp->n_addrs);
		return -ENXIO;
	}
	p = kmalloc(sizeof(*p), GFP_ATOMIC);
	if (p == 0)
		return -ENXIO;
	control_fb = p;	/* save it for cleanups */
	memset(p, 0, sizeof(*p));

	/* Map in frame buffer and registers */
	for (i = 0; i < dp->n_addrs; ++i) {
		addr = dp->addrs[i].address;
		if (dp->addrs[i].size >= 0x800000) {
			p->fb_orig_base = addr;
			p->fb_orig_size = dp->addrs[i].size;
			/* use the big-endian aperture (??) */
			p->frame_buffer_phys = addr + 0x800000;
		} else {
			p->control_regs_phys = addr;
			p->control_regs_size = dp->addrs[i].size;
		}
	}

	if (!p->fb_orig_base ||
	    !request_mem_region(p->fb_orig_base,p->fb_orig_size,"controlfb")) {
		p->fb_orig_base = 0;
		goto error_out;
	}
	/* map at most 8MB for the frame buffer */
	p->frame_buffer = __ioremap(p->frame_buffer_phys, 0x800000,
				    _PAGE_WRITETHRU);

	if (!p->control_regs_phys ||
	    !request_mem_region(p->control_regs_phys, p->control_regs_size,
	    "controlfb regs")) {
		p->control_regs_phys = 0;
		goto error_out;
	}
	p->control_regs = ioremap(p->control_regs_phys, p->control_regs_size);

	p->cmap_regs_phys = 0xf301b000;	 /* XXX not in prom? */
	if (!request_mem_region(p->cmap_regs_phys, 0x1000, "controlfb cmap")) {
		p->cmap_regs_phys = 0;
		goto error_out;
	}
	p->cmap_regs = ioremap(p->cmap_regs_phys, 0x1000);

	if (!p->cmap_regs || !p->control_regs || !p->frame_buffer)
		goto error_out;

	find_vram_size(p);
	if (!p->total_vram)
		goto error_out;

	if (init_control(p) < 0)
		goto error_out;

	return 0;

error_out:
	control_cleanup();
	return -ENXIO;
}

/*
 * Get the monitor sense value.
 * Note that this can be called before calibrate_delay,
 * so we can't use udelay.
 */
static int read_control_sense(struct fb_info_control *p)
{
	int sense;

	out_le32(CNTRL_REG(p,mon_sense), 7);	/* drive all lines high */
	__delay(200);
	out_le32(CNTRL_REG(p,mon_sense), 077);	/* turn off drivers */
	__delay(2000);
	sense = (in_le32(CNTRL_REG(p,mon_sense)) & 0x1c0) << 2;

	/* drive each sense line low in turn and collect the other 2 */
	out_le32(CNTRL_REG(p,mon_sense), 033);	/* drive A low */
	__delay(2000);
	sense |= (in_le32(CNTRL_REG(p,mon_sense)) & 0xc0) >> 2;
	out_le32(CNTRL_REG(p,mon_sense), 055);	/* drive B low */
	__delay(2000);
	sense |= ((in_le32(CNTRL_REG(p,mon_sense)) & 0x100) >> 5)
		| ((in_le32(CNTRL_REG(p,mon_sense)) & 0x40) >> 4);
	out_le32(CNTRL_REG(p,mon_sense), 066);	/* drive C low */
	__delay(2000);
	sense |= (in_le32(CNTRL_REG(p,mon_sense)) & 0x180) >> 7;

	out_le32(CNTRL_REG(p,mon_sense), 077);	/* turn off drivers */
	
	return sense;
}

/**********************  Various translation functions  **********************/

#define CONTROL_PIXCLOCK_BASE	256016
#define CONTROL_PIXCLOCK_MIN	5000	/* ~ 200 MHz dot clock */

/*
 * calculate the clock paramaters to be sent to CUDA according to given
 * pixclock in pico second.
 */
static int calc_clock_params(unsigned long clk, unsigned char *param)
{
	unsigned long p0, p1, p2, k, l, m, n, min;

	if (clk > (CONTROL_PIXCLOCK_BASE << 3))
		return 1;

	p2 = ((clk << 4) < CONTROL_PIXCLOCK_BASE)? 3: 2;
	l = clk << p2;
	p0 = 0;
	p1 = 0;
	for (k = 1, min = l; k < 32; k++) {
		unsigned long rem;

		m = CONTROL_PIXCLOCK_BASE * k;
		n = m / l;
		rem = m % l;
		if (n && (n < 128) && rem < min) {
			p0 = k;
			p1 = n;
			min = rem;
		}
	}
	if (!p0 || !p1)
		return 1;

	param[0] = p0;
	param[1] = p1;
	param[2] = p2;

	return 0;
}


/*
 * This routine takes a user-supplied var, and picks the best vmode/cmode
 * from it.
 */

static int control_var_to_par(struct fb_var_screeninfo *var,
	struct fb_par_control *par, const struct fb_info *fb_info)
{
	int cmode, piped_diff, hstep;
	unsigned hperiod, hssync, hsblank, hesync, heblank, piped, heq, hlfln,
		 hserr, vperiod, vssync, vesync, veblank, vsblank, vswin, vewin;
	unsigned long pixclock;
	struct fb_info_control *p = (struct fb_info_control *) fb_info;
	struct control_regvals *r = &par->regvals;

	switch (var->bits_per_pixel) {
	case 8:
		par->cmode = CMODE_8;
		if (p->total_vram > 0x200000) {
			r->mode = 3;
			r->radacal_ctrl = 0x20;
			piped_diff = 13;
		} else {
			r->mode = 2;
			r->radacal_ctrl = 0x10;
			piped_diff = 9;
		}
		break;
	case 15:
	case 16:
		par->cmode = CMODE_16;
		if (p->total_vram > 0x200000) {
			r->mode = 2;
			r->radacal_ctrl = 0x24;
			piped_diff = 5;
		} else {
			r->mode = 1;
			r->radacal_ctrl = 0x14;
			piped_diff = 3;
		}
		break;
	case 32:
		par->cmode = CMODE_32;
		if (p->total_vram > 0x200000) {
			r->mode = 1;
			r->radacal_ctrl = 0x28;
		} else {
			r->mode = 0;
			r->radacal_ctrl = 0x18;
		}
		piped_diff = 1;
		break;
	default:
		return -EINVAL;
	}

	/*
	 * adjust xres and vxres so that the corresponding memory widths are
	 * 32-byte aligned
	 */
	hstep = 31 >> par->cmode;
	par->xres = (var->xres + hstep) & ~hstep;
	par->vxres = (var->xres_virtual + hstep) & ~hstep;
	par->xoffset = (var->xoffset + hstep) & ~hstep;
	if (par->vxres < par->xres)
		par->vxres = par->xres;
	par->pitch = par->vxres << par->cmode;

	par->yres = var->yres;
	par->vyres = var->yres_virtual;
	par->yoffset = var->yoffset;
	if (par->vyres < par->yres)
		par->vyres = par->yres;

	par->sync = var->sync;

	if (par->pitch * par->vyres + CTRLFB_OFF > p->total_vram)
		return -EINVAL;

	if (par->xoffset + par->xres > par->vxres)
		par->xoffset = par->vxres - par->xres;
	if (par->yoffset + par->yres > par->vyres)
		par->yoffset = par->vyres - par->yres;

	pixclock = (var->pixclock < CONTROL_PIXCLOCK_MIN)? CONTROL_PIXCLOCK_MIN:
		   var->pixclock;
	if (calc_clock_params(pixclock, r->clock_params))
		return -EINVAL;

	hperiod = ((var->left_margin + par->xres + var->right_margin
		    + var->hsync_len) >> 1) - 2;
	hssync = hperiod + 1;
	hsblank = hssync - (var->right_margin >> 1);
	hesync = (var->hsync_len >> 1) - 1;
	heblank = (var->left_margin >> 1) + hesync;
	piped = heblank - piped_diff;
	heq = var->hsync_len >> 2;
	hlfln = (hperiod+2) >> 1;
	hserr = hssync-hesync;
	vperiod = (var->vsync_len + var->lower_margin + par->yres
		   + var->upper_margin) << 1;
	vssync = vperiod - 2;
	vesync = (var->vsync_len << 1) - vperiod + vssync;
	veblank = (var->upper_margin << 1) + vesync;
	vsblank = vssync - (var->lower_margin << 1);
	vswin = (vsblank+vssync) >> 1;
	vewin = (vesync+veblank) >> 1;

	r->regs[0] = vswin;
	r->regs[1] = vsblank;
	r->regs[2] = veblank;
	r->regs[3] = vewin;
	r->regs[4] = vesync;
	r->regs[5] = vssync;
	r->regs[6] = vperiod;
	r->regs[7] = piped;
	r->regs[8] = hperiod;
	r->regs[9] = hsblank;
	r->regs[10] = heblank;
	r->regs[11] = hesync;
	r->regs[12] = hssync;
	r->regs[13] = heq;
	r->regs[14] = hlfln;
	r->regs[15] = hserr;

	if (par->xres >= 1280 && par->cmode >= CMODE_16)
		par->ctrl = 0x7f;
	else
		par->ctrl = 0x3b;

	if (mac_var_to_vmode(var, &par->vmode, &cmode))
		par->vmode = 0;

	return 0;
}


/*
 * Convert hardware data in par to an fb_var_screeninfo
 */

static void control_par_to_var(struct fb_par_control *par, struct fb_var_screeninfo *var)
{
	struct control_regints *rv;
	
	rv = (struct control_regints *) par->regvals.regs;
	
	memset(var, 0, sizeof(*var));
	var->xres = par->xres;
	var->yres = par->yres;
	var->xres_virtual = par->vxres;
	var->yres_virtual = par->vyres;
	var->xoffset = par->xoffset;
	var->yoffset = par->yoffset;
	
	switch(par->cmode) {
	default:
	case CMODE_8:
		var->bits_per_pixel = 8;
		var->red.length = 8;
		var->green.length = 8;
		var->blue.length = 8;
		break;
	case CMODE_16:	/* RGB 555 */
		var->bits_per_pixel = 16;
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.length = 5;
		break;
	case CMODE_32:	/* RGB 888 */
		var->bits_per_pixel = 32;
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		break;
	}
	var->height = -1;
	var->width = -1;
	var->vmode = FB_VMODE_NONINTERLACED;

	var->left_margin = (rv->heblank - rv->hesync) << 1;
	var->right_margin = (rv->hssync - rv->hsblank) << 1;
	var->hsync_len = (rv->hperiod + 2 - rv->hssync + rv->hesync) << 1;

	var->upper_margin = (rv->veblank - rv->vesync) >> 1;
	var->lower_margin = (rv->vssync - rv->vsblank) >> 1;
	var->vsync_len = (rv->vperiod - rv->vssync + rv->vesync) >> 1;

	var->sync = par->sync;

	/*
	 * 10^12 * clock_params[0] / (3906400 * clock_params[1]
	 *			      * 2^clock_params[2])
	 * (10^12 * clock_params[0] / (3906400 * clock_params[1]))
	 * >> clock_params[2]
	 */
	/* (255990.17 * clock_params[0] / clock_params[1]) >> clock_params[2] */
	var->pixclock = CONTROL_PIXCLOCK_BASE * par->regvals.clock_params[0];
	var->pixclock /= par->regvals.clock_params[1];
	var->pixclock >>= par->regvals.clock_params[2];
}


/*
 * init fix according to given par
 */
static void control_par_to_fix(struct fb_par_control *par, struct fb_fix_screeninfo *fix,
	struct fb_info_control *p)
{
	memset(fix, 0, sizeof(*fix));
	strcpy(fix->id, "control");
	fix->mmio_start = p->control_regs_phys;
	fix->mmio_len = sizeof(struct control_regs);
	fix->type = FB_TYPE_PACKED_PIXELS;
	
	fix->xpanstep = 32 >> par->cmode;
	fix->ypanstep = 1;

	fix->smem_start = p->frame_buffer_phys + CTRLFB_OFF;
	fix->smem_len = p->total_vram - CTRLFB_OFF;
	fix->visual = (par->cmode == CMODE_8) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_DIRECTCOLOR;
	fix->line_length = par->pitch;
}

/*
 * initialize a portion of struct display which low level driver is responsible
 * for.
 */
static void control_par_to_display(struct fb_par_control *par,
  struct display *disp, struct fb_fix_screeninfo *fix, struct fb_info_control *p)
{
	disp->type = fix->type;
	disp->can_soft_blank = 1;
	disp->scrollmode = SCROLL_YNOMOVE | SCROLL_YNOPARTIAL;
	disp->ypanstep = fix->ypanstep;
	disp->ywrapstep = fix->ywrapstep;

	control_par_to_var(par, &disp->var);
	p->info.screen_base = (char *) p->frame_buffer + CTRLFB_OFF;
	disp->visual = fix->visual;
	disp->line_length = fix->line_length;
	control_set_dispsw(disp, par->cmode, p);
}


/*
 * our own _revc() routines since generic routines don't work for DIRECT Color
 * devices like control
 */
static void control_cfb16_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes = p->next_line, rows;

    dest = p->info.screen_base + yy * fontheight(p) * bytes + xx * fontwidth(p)*2;
    for (rows = fontheight(p); rows--; dest += bytes) {
       switch (fontwidth(p)) {
       case 16:
           ((u32 *)dest)[6] ^= 0x3def3def; ((u32 *)dest)[7] ^= 0x3def3def;
           /* FALL THROUGH */
       case 12:
           ((u32 *)dest)[4] ^= 0x3def3def; ((u32 *)dest)[5] ^= 0x3def3def;
           /* FALL THROUGH */
       case 8:
           ((u32 *)dest)[2] ^= 0x3def3def; ((u32 *)dest)[3] ^= 0x3def3def;
           /* FALL THROUGH */
       case 4:
           ((u32 *)dest)[0] ^= 0x3def3def; ((u32 *)dest)[1] ^= 0x3def3def;
       }
    }
}

static void control_cfb32_revc(struct display *p, int xx, int yy)
{
    u8 *dest;
    int bytes = p->next_line, rows;

    dest = p->info.screen_base + yy * fontheight(p) * bytes + xx * fontwidth(p) * 4;
    for (rows = fontheight(p); rows--; dest += bytes) {
       switch (fontwidth(p)) {
       case 16:
           ((u32 *)dest)[12] ^= 0x0f0f0f0f; ((u32 *)dest)[13] ^= 0x0f0f0f0f;
           ((u32 *)dest)[14] ^= 0x0f0f0f0f; ((u32 *)dest)[15] ^= 0x0f0f0f0f;
           /* FALL THROUGH */
       case 12:
           ((u32 *)dest)[8] ^= 0x0f0f0f0f; ((u32 *)dest)[9] ^= 0x0f0f0f0f;
           ((u32 *)dest)[10] ^= 0x0f0f0f0f; ((u32 *)dest)[11] ^= 0x0f0f0f0f;
           /* FALL THROUGH */
       case 8:
           ((u32 *)dest)[4] ^= 0x0f0f0f0f; ((u32 *)dest)[5] ^= 0x0f0f0f0f;
           ((u32 *)dest)[6] ^= 0x0f0f0f0f; ((u32 *)dest)[7] ^= 0x0f0f0f0f;
           /* FALL THROUGH */
       case 4:
           ((u32 *)dest)[0] ^= 0x0f0f0f0f; ((u32 *)dest)[1] ^= 0x0f0f0f0f;
           ((u32 *)dest)[2] ^= 0x0f0f0f0f; ((u32 *)dest)[3] ^= 0x0f0f0f0f;
           /* FALL THROUGH */
       }
    }
}

static struct display_switch control_cfb16 = {
    setup:		fbcon_cfb16_setup,
    bmove:		fbcon_cfb16_bmove,
    clear:		fbcon_cfb16_clear,
    putc:		fbcon_cfb16_putc,
    putcs:		fbcon_cfb16_putcs,
    revc:		control_cfb16_revc,
    clear_margins:	fbcon_cfb16_clear_margins,
    fontwidthmask:	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};

static struct display_switch control_cfb32 = {
    setup:		fbcon_cfb32_setup,
    bmove:		fbcon_cfb32_bmove,
    clear:		fbcon_cfb32_clear,
    putc:		fbcon_cfb32_putc,
    putcs:		fbcon_cfb32_putcs,
    revc:		control_cfb32_revc,
    clear_margins:	fbcon_cfb32_clear_margins,
    fontwidthmask:	FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};


/*
 * Set struct dispsw according to given cmode
 */
static void control_set_dispsw(struct display *disp, int cmode, struct fb_info_control *p)
{
	switch (cmode) {
#ifdef FBCON_HAS_CFB8
		case CMODE_8:
			disp->dispsw = &fbcon_cfb8;
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case CMODE_16:
			disp->dispsw = &control_cfb16;
			disp->dispsw_data = p->fbcon_cmap.cfb16;
			break;
#endif
#ifdef FBCON_HAS_CFB32
		case CMODE_32:
			disp->dispsw = &control_cfb32;
			disp->dispsw_data = p->fbcon_cmap.cfb32;
			break;
#endif
		default:
			disp->dispsw = &fbcon_dummy;
			break;
	}
}


/*
 * Set misc info vars for this driver
 */
static void __init control_init_info(struct fb_info *info, struct fb_info_control *p)
{
	strcpy(info->modename, "control");
	info->node = NODEV;
	info->fbops = &controlfb_ops;
	info->disp = &p->display;
	strcpy(info->fontname, fontname);
	info->changevar = NULL;
	info->switch_con = &controlfb_switch;
	info->updatevar = &controlfb_updatevar;
}


static void control_cleanup(void)
{
	struct fb_info_control	*p = control_fb;

	if (!p)
		return;

	if (p->cmap_regs)
		iounmap(p->cmap_regs);
	if (p->control_regs)
		iounmap(p->control_regs);
	if (p->frame_buffer) {
		if (p->control_use_bank2)
			p->frame_buffer -= 0x600000;
		iounmap(p->frame_buffer);
	}
	if (p->cmap_regs_phys)
		release_mem_region(p->cmap_regs_phys, 0x1000);
	if (p->control_regs_phys)
		release_mem_region(p->control_regs_phys, p->control_regs_size);
	if (p->fb_orig_base)
		release_mem_region(p->fb_orig_base, p->fb_orig_size);
	kfree(p);
}


/*
 * Parse user speficied options (`video=controlfb:')
 */
void __init control_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "font:", 5)) {
			char *p;
			int i;

			p = this_opt +5;
			for (i = 0; i < sizeof(fontname) - 1; i++)
				if (!*p || *p == ' ' || *p == ',')
					break;
			memcpy(fontname, this_opt + 5, i);
			fontname[i] = 0;
		} else if (!strncmp(this_opt, "vmode:", 6)) {
			int vmode = simple_strtoul(this_opt+6, NULL, 0);
			if (vmode > 0 && vmode <= VMODE_MAX &&
			    control_mac_modes[vmode - 1].m[1] >= 0)
				default_vmode = vmode;
		} else if (!strncmp(this_opt, "cmode:", 6)) {
			int depth = simple_strtoul(this_opt+6, NULL, 0);
			switch (depth) {
			 case CMODE_8:
			 case CMODE_16:
			 case CMODE_32:
			 	default_cmode = depth;
			 	break;
			 case 8:
				default_cmode = CMODE_8;
				break;
			 case 15:
			 case 16:
				default_cmode = CMODE_16;
				break;
			 case 24:
			 case 32:
				default_cmode = CMODE_32;
				break;
			}
		}
	}
}

