/*
 *  linux/drivers/video/sbusfb.c -- SBUS or UPA based frame buffer device
 *
 *	Copyright (C) 1998 Jakub Jelinek
 *
 *  This driver is partly based on the Open Firmware console driver
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  and SPARC console subsystem
 *
 *      Copyright (C) 1995 Peter Zaitcev (zaitcev@lab.ipmce.su)
 *      Copyright (C) 1995-1997 David S. Miller (davem@caip.rutgers.edu)
 *      Copyright (C) 1995-1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *      Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
 *      Copyright (C) 1996-1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *      Copyright (C) 1996 Eddie C. Dost (ecd@skynet.be)
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
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/selection.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/kd.h>
#include <linux/vt_kern.h>

#include <asm/uaccess.h>

#include "sbusfb.h"

    /*
     *  Interface used by the world
     */

void sbusfb_init(void);
void sbusfb_setup(char *options, int *ints);

static int currcon;
static int defx_margin = -1, defy_margin = -1;
static char fontname[40] __initdata = { 0 };
static struct {
	int depth;
	int xres, yres;
	int x_margin, y_margin;
} def_margins [] = {
	{ 8, 1280, 1024, 64, 80 },
	{ 8, 1152, 1024, 64, 80 },
	{ 8, 1152, 900,  64, 18 },
	{ 8, 1024, 768,  0,  0 },
	{ 8, 800, 600, 16, 12 },
	{ 8, 640, 480, 0, 0 },
	{ 1, 1152, 900,  8,  18 },
	{ 0 },
};

static int sbusfb_open(struct fb_info *info, int user);
static int sbusfb_release(struct fb_info *info, int user);
static int sbusfb_mmap(struct fb_info *info, struct file *file, 
			struct vm_area_struct *vma);
static int sbusfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			struct fb_info *info);
static int sbusfb_get_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int sbusfb_set_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int sbusfb_pan_display(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int sbusfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info);
static int sbusfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info);
static int sbusfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
			    u_long arg, int con, struct fb_info *info);
static void sbusfb_cursor(struct display *p, int mode, int x, int y);
static void sbusfb_clear_margin(struct display *p, int s);
extern int io_remap_page_range(unsigned long from, unsigned long offset, 
			unsigned long size, pgprot_t prot, int space);
			    

    /*
     *  Interface to the low level console driver
     */

static int sbusfbcon_switch(int con, struct fb_info *info);
static int sbusfbcon_updatevar(int con, struct fb_info *info);
static void sbusfbcon_blank(int blank, struct fb_info *info);


    /*
     *  Internal routines
     */

static int sbusfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			    u_int *transp, struct fb_info *info);
static int sbusfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			    u_int transp, struct fb_info *info);
static void do_install_cmap(int con, struct fb_info *info);

static struct fb_ops sbusfb_ops = {
	sbusfb_open, sbusfb_release, sbusfb_get_fix, sbusfb_get_var, sbusfb_set_var,
	sbusfb_get_cmap, sbusfb_set_cmap, sbusfb_pan_display, sbusfb_ioctl, sbusfb_mmap
};


    /*
     *  Open/Release the frame buffer device
     */

static int sbusfb_open(struct fb_info *info, int user)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);
	
	if (user) {
		if (fb->open) return -EBUSY;
		fb->mmaped = 0;
		fb->open = 1;
		fb->vtconsole = -1;
	} else
		fb->consolecnt++;
	MOD_INC_USE_COUNT;
	return 0;
}

static int sbusfb_release(struct fb_info *info, int user)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);

	if (user) {	
		if (fb->vtconsole != -1) {
			vt_cons[fb->vtconsole]->vc_mode = KD_TEXT;
			if (fb->mmaped)
				sbusfb_clear_margin(&fb_display[fb->vtconsole], 0);
		}
		if (fb->reset)
			fb->reset(fb);
		fb->open = 0;
	} else
		fb->consolecnt--;
	MOD_DEC_USE_COUNT;
	return 0;
}

static unsigned long sbusfb_mmapsize(struct fb_info_sbusfb *fb, long size)
{
	if (size == SBUS_MMAP_EMPTY) return 0;
	if (size >= 0) return size;
	return fb->type.fb_size * (-size);
}

static int sbusfb_mmap(struct fb_info *info, struct file *file, 
			struct vm_area_struct *vma)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);
	unsigned int size, page, r, map_size;
	unsigned long map_offset = 0;
	int i;
                                        
	size = vma->vm_end - vma->vm_start;
	if (vma->vm_offset & ~PAGE_MASK)
		return -ENXIO;
		
	/* To stop the swapper from even considering these pages */
	vma->vm_flags |= (VM_SHM| VM_LOCKED);
	
#ifdef __sparc_v9__
	/* Align it as much as desirable */
	{
		int j, max = -1, alignment, s = 0;
		
		map_offset = vma->vm_offset+size;
		for (i = 0; fb->mmap_map[i].size; i++) {
			if (fb->mmap_map[i].voff < vma->vm_offset)
				continue;
			if (fb->mmap_map[i].voff >= map_offset)
				break;
			if (max < 0 || sbusfb_mmapsize(fb,fb->mmap_map[i].size) > s) {
				max = i;
				s = sbusfb_mmapsize(fb,fb->mmap_map[max].size);
			}
		}
		if (max >= 0) {
			j = s;
			if (fb->mmap_map[max].voff + j > map_offset)
				j = map_offset - fb->mmap_map[max].voff;
			for (alignment = 0x400000; alignment > PAGE_SIZE; alignment >>= 3)
				if (j >= alignment && !(fb->mmap_map[max].poff & (alignment - 1)))
					break;
			if (alignment > PAGE_SIZE) {
				j = alignment;
				alignment = j - ((vma->vm_start + fb->mmap_map[max].voff - vma->vm_offset) & (j - 1));
				if (alignment != j) {
					struct vm_area_struct *vmm = find_vma(current->mm, vma->vm_start);
					if (!vmm || vmm->vm_start >= vma->vm_end + alignment) {
						vma->vm_start += alignment;
						vma->vm_end += alignment;
					}
				}
			}
		}
	}
#endif	

	/* Each page, see which map applies */
	for (page = 0; page < size; ){
		map_size = 0;
		for (i = 0; fb->mmap_map[i].size; i++)
			if (fb->mmap_map[i].voff == vma->vm_offset+page) {
				map_size = sbusfb_mmapsize(fb,fb->mmap_map[i].size);
				map_offset = (fb->physbase + fb->mmap_map[i].poff) & PAGE_MASK;
				break;
			}
		if (!map_size){
			page += PAGE_SIZE;
			continue;
		}
		if (page + map_size > size)
			map_size = size - page;
		r = io_remap_page_range (vma->vm_start+page, map_offset, map_size, vma->vm_page_prot, fb->iospace);
		if (r)
			return -EAGAIN;
		page += map_size;
	}
	
	vma->vm_file = file;
	file->f_count++;
	vma->vm_flags |= VM_IO;
	if (!fb->mmaped) {
		int lastconsole = 0;
		
		if (info->display_fg)
			lastconsole = info->display_fg->vc_num;
		fb->mmaped = 1;
		if (fb->consolecnt && fb_display[lastconsole].fb_info == info) {
			fb->vtconsole = lastconsole;
			vt_cons [lastconsole]->vc_mode = KD_GRAPHICS;
		} else if (fb->unblank && !fb->blanked)
			(*fb->unblank)(fb);
	}
	return 0;
}

static void sbusfb_clear_margin(struct display *p, int s)
{
	struct fb_info_sbusfb *fb = sbusfbinfod(p);

	if (fb->fill) {
		unsigned short rects [16];

		rects [0] = 0;
		rects [1] = 0;
		rects [2] = fb->var.xres_virtual;
		rects [3] = fb->y_margin;
		rects [4] = 0;
		rects [5] = fb->y_margin;
		rects [6] = fb->x_margin;
		rects [7] = fb->var.yres_virtual;
		rects [8] = fb->var.xres_virtual - fb->x_margin;
		rects [9] = fb->y_margin;
		rects [10] = fb->var.xres_virtual;
		rects [11] = fb->var.yres_virtual;
		rects [12] = fb->x_margin;
		rects [13] = fb->var.yres_virtual - fb->y_margin;
		rects [14] = fb->var.xres_virtual - fb->x_margin;
		rects [15] = fb->var.yres_virtual;
		(*fb->fill)(fb, s, 4, rects);
	} else {
		unsigned char *fb_base = p->screen_base, *q;
		int skip_bytes = fb->y_margin * fb->var.xres_virtual;
		int scr_size = fb->var.xres_virtual * fb->var.yres_virtual;
		int h, he, incr, size;

		he = fb->var.yres;
		if (fb->var.bits_per_pixel == 1) {
			fb_base -= (skip_bytes + fb->x_margin) / 8;
			skip_bytes /= 8;
			scr_size /= 8;
			memset (fb_base, ~0, skip_bytes - fb->x_margin / 8);
			memset (fb_base + scr_size - skip_bytes + fb->x_margin / 8, ~0, skip_bytes - fb->x_margin / 8);
			incr = fb->var.xres_virtual / 8;
			size = fb->x_margin / 8 * 2;
			for (q = fb_base + skip_bytes - fb->x_margin / 8, h = 0;
			     h <= he; q += incr, h++)
				memset (q, ~0, size);
		} else {
			fb_base -= (skip_bytes + fb->x_margin);
			memset (fb_base, attr_bg_col(s), skip_bytes - fb->x_margin);
			memset (fb_base + scr_size - skip_bytes + fb->x_margin, attr_bg_col(s), skip_bytes - fb->x_margin);
			incr = fb->var.xres_virtual;
			size = fb->x_margin * 2;
			for (q = fb_base + skip_bytes - fb->x_margin, h = 0;
			     h <= he; q += incr, h++)
				memset (q, attr_bg_col(s), size);
		}
	}
	if (fb->switch_from_graph)
		(*fb->switch_from_graph)(fb);
}

static void sbusfb_disp_setup(struct display *p)
{
	struct fb_info_sbusfb *fb = sbusfbinfod(p);

	if (fb->setup)
		fb->setup(p);	
	sbusfb_clear_margin(p, 0);
}

    /*
     *  Get the Fixed Part of the Display
     */

static int sbusfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			  struct fb_info *info)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);

	memcpy(fix, &fb->fix, sizeof(struct fb_fix_screeninfo));
	return 0;
}

    /*
     *  Get the User Defined Part of the Display
     */

static int sbusfb_get_var(struct fb_var_screeninfo *var, int con,
			  struct fb_info *info)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);

	memcpy(var, &fb->var, sizeof(struct fb_var_screeninfo));
	return 0;
}

    /*
     *  Set the User Defined Part of the Display
     */

static int sbusfb_set_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info)
{
	return -EINVAL;
}

    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int sbusfb_pan_display(struct fb_var_screeninfo *var, int con,
			      struct fb_info *info)
{
	if (var->xoffset || var->yoffset)
		return -EINVAL;
	else
		return 0;
}

    /*
     *  Hardware cursor
     */
     
static int sbus_hw_scursor (struct fbcursor *cursor, struct fb_info_sbusfb *fb)
{
	int op;
	int i, bytes = 0;
	struct fbcursor f;
	char red[2], green[2], blue[2];
	
	if (copy_from_user (&f, cursor, sizeof(struct fbcursor)))
		return -EFAULT;
	op = f.set;
	if (op & FB_CUR_SETSHAPE){
		if ((u32) f.size.fbx > fb->cursor.hwsize.fbx)
			return -EINVAL;
		if ((u32) f.size.fby > fb->cursor.hwsize.fby)
			return -EINVAL;
		if (f.size.fbx > 32)
			bytes = f.size.fby << 3;
		else
			bytes = f.size.fby << 2;
	}
	if (op & FB_CUR_SETCMAP){
		if (f.cmap.index || f.cmap.count != 2)
			return -EINVAL;
		if (copy_from_user (red, f.cmap.red, 2) ||
		    copy_from_user (green, f.cmap.green, 2) ||
		    copy_from_user (blue, f.cmap.blue, 2))
			return -EFAULT;
	}
	if (op & FB_CUR_SETCMAP)
		(*fb->setcursormap) (fb, red, green, blue);
	if (op & FB_CUR_SETSHAPE){
		u32 u;
		
		fb->cursor.size = f.size;
		memset ((void *)&fb->cursor.bits, 0, sizeof (fb->cursor.bits));
		if (copy_from_user (fb->cursor.bits [0], f.mask, bytes) ||
		    copy_from_user (fb->cursor.bits [1], f.image, bytes))
			return -EFAULT;
		if (f.size.fbx <= 32) {
			u = ~(0xffffffff >> f.size.fbx);
			for (i = fb->cursor.size.fby - 1; i >= 0; i--) {
				fb->cursor.bits [0][i] &= u;
				fb->cursor.bits [1][i] &= fb->cursor.bits [0][i];
			}
		} else {
			u = ~(0xffffffff >> (f.size.fbx - 32));
			for (i = fb->cursor.size.fby - 1; i >= 0; i--) {
				fb->cursor.bits [0][2*i+1] &= u;
				fb->cursor.bits [1][2*i] &= fb->cursor.bits [0][2*i];
				fb->cursor.bits [1][2*i+1] &= fb->cursor.bits [0][2*i+1];
			}
		}
		(*fb->setcurshape) (fb);
	}
	if (op & (FB_CUR_SETCUR | FB_CUR_SETPOS | FB_CUR_SETHOT)){
		if (op & FB_CUR_SETCUR)
			fb->cursor.enable = f.enable;
		if (op & FB_CUR_SETPOS)
			fb->cursor.cpos = f.pos;
		if (op & FB_CUR_SETHOT)
			fb->cursor.chot = f.hot;
		(*fb->setcursor) (fb);
	}
	return 0;
}

static unsigned char hw_cursor_cmap[2] = { 0, 0xff };

static void sbusfb_cursor(struct display *p, int mode, int x, int y)
{
	struct fb_info_sbusfb *fb = sbusfbinfod(p);
	
	switch (mode) {
	case CM_ERASE:
		fb->cursor.enable = 0;
		(*fb->setcursor)(fb);
		fb->hw_cursor_shown = 0;
		break;
				  
	case CM_MOVE:
	case CM_DRAW:
		if (!fb->hw_cursor_shown) {
			fb->cursor.size.fbx = p->fontwidth;
			fb->cursor.size.fby = p->fontheight;
			fb->cursor.chot.fbx = 0;
			fb->cursor.chot.fby = 0;
			fb->cursor.enable = 1;
			memset (fb->cursor.bits, 0, sizeof (fb->cursor.bits));
			fb->cursor.bits[0][p->fontheight - 2] = (0xffffffff << (32 - p->fontwidth));
			fb->cursor.bits[1][p->fontheight - 2] = (0xffffffff << (32 - p->fontwidth));
			fb->cursor.bits[0][p->fontheight - 1] = (0xffffffff << (32 - p->fontwidth));
			fb->cursor.bits[1][p->fontheight - 1] = (0xffffffff << (32 - p->fontwidth));
			(*fb->setcursormap) (fb, hw_cursor_cmap, hw_cursor_cmap, hw_cursor_cmap);
			(*fb->setcurshape) (fb);
			fb->hw_cursor_shown = 1;
		}
		if (p->fontwidthlog)
			fb->cursor.cpos.fbx = (x << p->fontwidthlog) + fb->x_margin;
		else
			fb->cursor.cpos.fbx = (x * p->fontwidth) + fb->x_margin;
		if (p->fontheightlog)
			fb->cursor.cpos.fby = (y << p->fontheightlog) + fb->y_margin;
		else
			fb->cursor.cpos.fby = (y * p->fontheight) + fb->y_margin;
		(*fb->setcursor)(fb);
		break;
	}
}

    /*
     *  Get the Colormap
     */

static int sbusfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
	if (con == currcon) /* current console? */
		return fb_get_cmap(cmap, &fb_display[con].var, kspc, sbusfb_getcolreg, info);
	else if (fb_display[con].cmap.len) /* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel), cmap, kspc ? 0 : 2);
	return 0;
}

    /*
     *  Set the Colormap
     */

static int sbusfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			 struct fb_info *info)
{
	int err;

	if (!fb_display[con].cmap.len) {	/* no colormap allocated? */
		if ((err = fb_alloc_cmap(&fb_display[con].cmap, 1<<fb_display[con].var.bits_per_pixel, 0)))
			return err;
	}
	if (con == currcon) {			/* current console? */
		err = fb_set_cmap(cmap, &fb_display[con].var, kspc, sbusfb_setcolreg, info);
		if (!err) {
			struct fb_info_sbusfb *fb = sbusfbinfo(info);
			
			if (fb->loadcmap)
				(*fb->loadcmap)(fb, cmap->start, cmap->len);
		}
		return err;
	} else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
	return 0;
}

static int sbusfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
			u_long arg, int con, struct fb_info *info)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);
	int i;
	int lastconsole;
	
	switch (cmd){
	case FBIOGTYPE:		/* return frame buffer type */
		copy_to_user_ret((struct fbtype *)arg, &fb->type, sizeof(struct fbtype), -EFAULT);
		break;
	case FBIOGATTR: {
		struct fbgattr *fba = (struct fbgattr *) arg;
		
		i = verify_area (VERIFY_WRITE, (void *) arg, sizeof (struct fbgattr));
		if (i) return i;
		__put_user_ret(fb->emulations[0], &fba->real_type, -EFAULT);
		__put_user_ret(0, &fba->owner, -EFAULT);
		__copy_to_user_ret(&fba->fbtype, &fb->type,
				   sizeof(struct fbtype), -EFAULT);
		__put_user_ret(0, &fba->sattr.flags, -EFAULT);
		__put_user_ret(fb->type.fb_type, &fba->sattr.emu_type, -EFAULT);
		__put_user_ret(-1, &fba->sattr.dev_specific[0], -EFAULT);
		for (i = 0; i < 4; i++)
			put_user_ret(fb->emulations[i], &fba->emu_types[i], -EFAULT);
		break;
	}
	case FBIOSATTR:
		i = verify_area (VERIFY_READ, (void *) arg, sizeof (struct fbsattr));
		if (i) return i;
		return -EINVAL;
	case FBIOSVIDEO:
		if (fb->consolecnt) {
			lastconsole = info->display_fg->vc_num;
			if (vt_cons[lastconsole]->vc_mode == KD_TEXT)
 				break;
 		}
		get_user_ret(i, (int *)arg, -EFAULT);
		if (i){
			if (!fb->blanked || !fb->unblank)
				break;
			if (fb->consolecnt || (fb->open && fb->mmaped))
				(*fb->unblank)(fb);
			fb->blanked = 0;
		} else {
			if (fb->blanked || !fb->blank)
				break;
			(*fb->blank)(fb);
			fb->blanked = 1;
		}
		break;
	case FBIOGVIDEO:
		put_user_ret(fb->blanked, (int *) arg, -EFAULT);
		break;
	case FBIOGETCMAP_SPARC: {
		char *rp, *gp, *bp;
		int end, count, index;
		struct fbcmap *cmap;

		if (!fb->loadcmap)
			return -EINVAL;
		i = verify_area (VERIFY_READ, (void *) arg, sizeof (struct fbcmap));
		if (i) return i;
		cmap = (struct fbcmap *) arg;
		__get_user_ret(count, &cmap->count, -EFAULT);
		__get_user_ret(index, &cmap->index, -EFAULT);
		if ((index < 0) || (index > 255))
			return -EINVAL;
		if (index + count > 256)
			count = 256 - index;
		__get_user_ret(rp, &cmap->red, -EFAULT);
		__get_user_ret(gp, &cmap->green, -EFAULT);
		__get_user_ret(bp, &cmap->blue, -EFAULT);
		if(verify_area (VERIFY_WRITE, rp, count))  return -EFAULT;
		if(verify_area (VERIFY_WRITE, gp, count))  return -EFAULT;
		if(verify_area (VERIFY_WRITE, bp, count))  return -EFAULT;
		end = index + count;
		for (i = index; i < end; i++){
			__put_user_ret(fb->color_map CM(i,0), rp, -EFAULT);
			__put_user_ret(fb->color_map CM(i,1), gp, -EFAULT);
			__put_user_ret(fb->color_map CM(i,2), bp, -EFAULT);
			rp++; gp++; bp++;
		}
		(*fb->loadcmap)(fb, index, count);
		break;			
	}
	case FBIOPUTCMAP_SPARC: {	/* load color map entries */
		char *rp, *gp, *bp;
		int end, count, index;
		struct fbcmap *cmap;
		
		if (!fb->loadcmap)
			return -EINVAL;
		i = verify_area (VERIFY_READ, (void *) arg, sizeof (struct fbcmap));
		if (i) return i;
		cmap = (struct fbcmap *) arg;
		__get_user_ret(count, &cmap->count, -EFAULT);
		__get_user_ret(index, &cmap->index, -EFAULT);
		if ((index < 0) || (index > 255))
			return -EINVAL;
		if (index + count > 256)
			count = 256 - index;
		__get_user_ret(rp, &cmap->red, -EFAULT);
		__get_user_ret(gp, &cmap->green, -EFAULT);
		__get_user_ret(bp, &cmap->blue, -EFAULT);
		if(verify_area (VERIFY_READ, rp, count)) return -EFAULT;
		if(verify_area (VERIFY_READ, gp, count)) return -EFAULT;
		if(verify_area (VERIFY_READ, bp, count)) return -EFAULT;

		end = index + count;
		for (i = index; i < end; i++){
			__get_user_ret(fb->color_map CM(i,0), rp, -EFAULT);
			__get_user_ret(fb->color_map CM(i,1), gp, -EFAULT);
			__get_user_ret(fb->color_map CM(i,2), bp, -EFAULT);
			rp++; gp++; bp++;
		}
		(*fb->loadcmap)(fb, index, count);
		break;			
	}
	case FBIOGCURMAX: {
		struct fbcurpos *p = (struct fbcurpos *) arg;
		if (!fb->setcursor) return -EINVAL;
		if(verify_area (VERIFY_WRITE, p, sizeof (struct fbcurpos)))
			return -EFAULT;
		__put_user_ret(fb->cursor.hwsize.fbx, &p->fbx, -EFAULT);
		__put_user_ret(fb->cursor.hwsize.fby, &p->fby, -EFAULT);
		break;
	}
	case FBIOSCURSOR:
		if (!fb->setcursor) return -EINVAL;
 		if (fb->consolecnt) {
 			lastconsole = info->display_fg->vc_num; 
 			if (vt_cons[lastconsole]->vc_mode == KD_TEXT)
 				return -EINVAL; /* Don't let graphics programs hide our nice text cursor */
			fb->hw_cursor_shown = 0; /* Forget state of our text cursor */
		}
		return sbus_hw_scursor ((struct fbcursor *) arg, fb);

	case FBIOSCURPOS:
		if (!fb->setcursor) return -EINVAL;
		/* Don't let graphics programs move our nice text cursor */
 		if (fb->consolecnt) {
 			lastconsole = info->display_fg->vc_num; 
 			if (vt_cons[lastconsole]->vc_mode == KD_TEXT)
 				return -EINVAL; /* Don't let graphics programs move our nice text cursor */
 		}
		if (copy_from_user(&fb->cursor.cpos, (void *)arg, sizeof(struct fbcurpos)))
			return -EFAULT;
		(*fb->setcursor) (fb);
		break;
	default:
		/* FIXME: Call here possible fb specific ioctl */
		return -EINVAL;
	}		
	return 0;
}

    /*
     *  Setup: parse used options
     */

__initfunc(void sbusfb_setup(char *options, int *ints))
{
	char *p;
	
	for (p = options;;) {
		if (!strncmp(p, "nomargins", 9)) {
			defx_margin = 0; defy_margin = 0;
		} else if (!strncmp(p, "margins=", 8)) {
			int i, j;
			char *q;
			
			i = simple_strtoul(p+8,&q,10);
			if (i >= 0 && *q == 'x') {
			    j = simple_strtoul(q+1,&q,10);
			    if (j >= 0 && (*q == ' ' || !*q)) {
			    	defx_margin = i; defy_margin = j;
			    }
			}
		} else if (!strncmp(p, "font=", 5)) {
			int i;
			
			for (i = 0; i < sizeof(fontname) - 1; i++)
				if (p[i+5] == ' ' || !p[i+5])
					break;
			memcpy(fontname, p+5, i);
			fontname[i] = 0;
		}
		while (*p && *p != ' ' && *p != ',') p++;
		if (*p != ',') break;
		p++;
	}
}

static int sbusfbcon_switch(int con, struct fb_info *info)
{
	int x_margin, y_margin;
	struct fb_info_sbusfb *fb = sbusfbinfo(info);
	int lastconsole;
    
	/* Do we have to save the colormap? */
	if (fb_display[currcon].cmap.len)
		fb_get_cmap(&fb_display[currcon].cmap, &fb_display[currcon].var, 1, sbusfb_getcolreg, info);

	lastconsole = info->display_fg->vc_num;
	if (lastconsole != con && 
	    (fb_display[lastconsole].fontwidth != fb_display[con].fontwidth ||
	     fb_display[lastconsole].fontheight != fb_display[con].fontheight))
		fb->hw_cursor_shown = 0;
	x_margin = (fb_display[con].var.xres_virtual - fb_display[con].var.xres) / 2;
	y_margin = (fb_display[con].var.yres_virtual - fb_display[con].var.yres) / 2;
	if (fb->margins)
		fb->margins(fb, &fb_display[con], x_margin, y_margin);
	if (fb->x_margin != x_margin || fb->y_margin != y_margin) {
		fb->x_margin = x_margin; fb->y_margin = y_margin;
		sbusfb_clear_margin(&fb_display[con], 0);
	}
	currcon = con;
	/* Install new colormap */
	do_install_cmap(con, info);
	return 0;
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int sbusfbcon_updatevar(int con, struct fb_info *info)
{
	/* Nothing */
	return 0;
}

    /*
     *  Blank the display.
     */

static void sbusfbcon_blank(int blank, struct fb_info *info)
{
    struct fb_info_sbusfb *fb = sbusfbinfo(info);
    
    if (blank && fb->blank)
    	return fb->blank(fb);
    else if (!blank && fb->unblank)
    	return fb->unblank(fb);
}

    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */

static int sbusfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			  u_int *transp, struct fb_info *info)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);

	if (!fb->color_map || regno > 255)
		return 1;
	*red = fb->color_map CM(regno, 0);
	*green = fb->color_map CM(regno, 1);
	*blue = fb->color_map CM(regno, 2);
	return 0;
}


    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int sbusfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			    u_int transp, struct fb_info *info)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);

	if (!fb->color_map || regno > 255)
		return 1;
	fb->color_map CM(regno, 0) = red;
	fb->color_map CM(regno, 1) = green;
	fb->color_map CM(regno, 2) = blue;
	return 0;
}


static void do_install_cmap(int con, struct fb_info *info)
{
	struct fb_info_sbusfb *fb = sbusfbinfo(info);
	
	if (con != currcon)
		return;
	if (fb_display[con].cmap.len)
		fb_set_cmap(&fb_display[con].cmap, &fb_display[con].var, 1,
			    sbusfb_setcolreg, info);
	else
		fb_set_cmap(fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
			    &fb_display[con].var, 1, sbusfb_setcolreg, info);
	if (fb->loadcmap)
		(*fb->loadcmap)(fb, 0, 256);
}

static int sbusfb_set_font(struct display *p, int width, int height)
{
	int margin;
	int w = p->var.xres_virtual, h = p->var.yres_virtual;
	int depth = p->var.bits_per_pixel;
	struct fb_info_sbusfb *fb = sbusfbinfod(p);
	int x_margin, y_margin;
	
	if (depth > 8) depth = 8;
	x_margin = 0;
	y_margin = 0;
	if (defx_margin < 0 || defy_margin < 0) {
		for (margin = 0; def_margins[margin].depth; margin++)
			if (w == def_margins[margin].xres &&
			    h == def_margins[margin].yres &&
			    depth == def_margins[margin].depth) {
				x_margin = def_margins[margin].x_margin;
				y_margin = def_margins[margin].y_margin;
				break;
			}
	} else {
		x_margin = defx_margin;
		y_margin = defy_margin;
	}
	x_margin += ((w - 2*x_margin) % width) / 2;
	y_margin += ((h - 2*y_margin) % height) / 2;

	p->var.xres = w - 2*x_margin;
	p->var.yres = h - 2*y_margin;
	
	fb->hw_cursor_shown = 0;
	
	if (fb->margins)
		fb->margins(fb, p, x_margin, y_margin);
	if (fb->x_margin != x_margin || fb->y_margin != y_margin) {
		fb->x_margin = x_margin; fb->y_margin = y_margin;
		sbusfb_clear_margin(p, 0);
	}

	return 1;
}

void sbusfb_palette(int enter)
{
	int i;
	struct display *p;
	
	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		p = &fb_display[i];
		if (p->dispsw && p->dispsw->setup == sbusfb_disp_setup &&
		    p->fb_info->display_fg &&
		    p->fb_info->display_fg->vc_num == i) {
			struct fb_info_sbusfb *fb = sbusfbinfod(p);

			if (fb->restore_palette) {
				if (enter)
					fb->restore_palette(fb);
				else if (vt_cons[i]->vc_mode != KD_GRAPHICS)
				         vc_cons[i].d->vc_sw->con_set_palette(vc_cons[i].d, color_table);
			}
		}
	}
}

    /*
     *  Initialisation
     */
     
extern void (*prom_palette)(int);

__initfunc(static void sbusfb_init_fb(int node, int parent, int fbtype,
				      struct linux_sbus_device *sbdp))
{
	struct fb_fix_screeninfo *fix;
	struct fb_var_screeninfo *var;
	struct display *disp;
	struct fb_info_sbusfb *fb;
	struct fbtype *type;
	int linebytes, w, h, depth;
	char *p = NULL;
	int margin;

	fb = kmalloc(sizeof(struct fb_info_sbusfb), GFP_ATOMIC);
	if (!fb) {
		prom_printf("Could not allocate sbusfb structure\n");
		return;
	}
	
	if (!prom_palette)
		prom_palette = sbusfb_palette;
	
	memset(fb, 0, sizeof(struct fb_info_sbusfb));
	fix = &fb->fix;
	var = &fb->var;
	disp = &fb->disp;
	type = &fb->type;
	
	fb->prom_node = node;
	fb->prom_parent = parent;
	fb->sbdp = sbdp;
	if (sbdp)
		fb->iospace = sbdp->reg_addrs[0].which_io;

	type->fb_type = fbtype;
	memset(&fb->emulations, 0xff, sizeof(fb->emulations));
	fb->emulations[0] = fbtype;
	
#ifndef __sparc_v9__
	disp->screen_base = (unsigned char *)prom_getintdefault(node, "address", 0);
#endif
	
	type->fb_height = h = prom_getintdefault(node, "height", 900);
	type->fb_width  = w = prom_getintdefault(node, "width", 1152);
	type->fb_depth  = depth = (fbtype == FBTYPE_SUN2BW) ? 1 : 8;
	linebytes = prom_getintdefault(node, "linebytes", w * depth / 8);
	type->fb_size   = PAGE_ALIGN((linebytes) * h);
	
	if (defx_margin < 0 || defy_margin < 0) {
		for (margin = 0; def_margins[margin].depth; margin++)
			if (w == def_margins[margin].xres &&
			    h == def_margins[margin].yres &&
			    depth == def_margins[margin].depth) {
				fb->x_margin = def_margins[margin].x_margin;
				fb->y_margin = def_margins[margin].y_margin;
				break;
			}
	} else {
		fb->x_margin = defx_margin;
		fb->y_margin = defy_margin;
	}
	fb->x_margin += ((w - 2*fb->x_margin) & 7) / 2;
	fb->y_margin += ((h - 2*fb->y_margin) & 15) / 2;

	var->xres_virtual = w;
	var->yres_virtual = h;
	var->xres = w - 2*fb->x_margin;
	var->yres = h - 2*fb->y_margin;
	
	var->bits_per_pixel = depth;
	var->height = var->width = -1;
	var->pixclock = 10000;
	var->vmode = FB_VMODE_NONINTERLACED;
	var->red.length = var->green.length = var->blue.length = 8;

	fix->line_length = linebytes;
	fix->smem_len = type->fb_size;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->visual = FB_VISUAL_PSEUDOCOLOR;
	
	fb->info.node = -1;
	fb->info.fbops = &sbusfb_ops;
	fb->info.disp = disp;
	strcpy(fb->info.fontname, fontname);
	fb->info.changevar = NULL;
	fb->info.switch_con = &sbusfbcon_switch;
	fb->info.updatevar = &sbusfbcon_updatevar;
	fb->info.blank = &sbusfbcon_blank;
	
	fb->cursor.hwsize.fbx = 32;
	fb->cursor.hwsize.fby = 32;
	
	if (depth > 1)
		fb->color_map = kmalloc(256 * 3, GFP_ATOMIC);
		
	switch(fbtype) {
#ifdef CONFIG_FB_CREATOR
	case FBTYPE_CREATOR:
		p = creatorfb_init(fb); break;
#endif
#ifdef CONFIG_FB_CGSIX
	case FBTYPE_SUNFAST_COLOR:
		p = cgsixfb_init(fb); break;
#endif
#ifdef CONFIG_FB_CGTHREE
	case FBTYPE_SUN3COLOR:
		p = cgthreefb_init(fb); break;
#endif
#ifdef CONFIG_FB_TCX
	case FBTYPE_TCXCOLOR:
		p = tcxfb_init(fb); break;
#endif
#ifdef CONFIG_FB_LEO
	case FBTYPE_SUNLEO:
		p = leofb_init(fb); break;
#endif
#ifdef CONFIG_FB_BWTWO
	case FBTYPE_SUN2BW:
		p = bwtwofb_init(fb); break;
#endif
#ifdef CONFIG_FB_CGFOURTEEN
	case FBTYPE_MDICOLOR:
		p = cgfourteenfb_init(fb); break;
#endif
	}
	
	if (!p) {
		kfree(fb);
		return;
	}

	disp->dispsw = &fb->dispsw;
	if (fb->setcursor)
		fb->dispsw.cursor = sbusfb_cursor;
	fb->dispsw.set_font = sbusfb_set_font;
	fb->setup = fb->dispsw.setup;
	fb->dispsw.setup = sbusfb_disp_setup;
	fb->dispsw.clear_margins = NULL;

	disp->var = *var;
	disp->visual = fix->visual;
	disp->type = fix->type;
	disp->type_aux = fix->type_aux;
	disp->line_length = fix->line_length;
	
	if (fb->blank)
		disp->can_soft_blank = 1;

	sbusfb_set_var(var, -1, &fb->info);

	if (register_framebuffer(&fb->info) < 0) {
		kfree(fb);
		return;
	}
	printk("fb%d: %s\n", GET_FB_IDX(fb->info.node), p);
}

static inline int known_card(char *name)
{
	char *p;
	for (p = name; *p && *p != ','; p++);
	if (*p == ',') name = p + 1;
	if (!strcmp(name, "cgsix") || !strcmp(name, "cgthree+"))
		return FBTYPE_SUNFAST_COLOR;
	if (!strcmp(name, "cgthree") || !strcmp(name, "cgRDI"))
		return FBTYPE_SUN3COLOR;
	if (!strcmp(name, "cgfourteen"))
		return FBTYPE_MDICOLOR;
	if (!strcmp(name, "leo"))
		return FBTYPE_SUNLEO;
	if (!strcmp(name, "bwtwo"))
		return FBTYPE_SUN2BW;
	if (!strcmp(name, "tcx"))
		return FBTYPE_TCXCOLOR;
	return FBTYPE_NOTYPE;
}

__initfunc(void sbusfb_init(void))
{
	int type;
	struct linux_sbus_device *sbdp;
	struct linux_sbus *sbus;
	char prom_name[40];
	extern int con_is_present(void);
	
	if (!con_is_present()) return;
	
#ifdef CONFIG_FB_CREATOR
	{
		int root, node;
		root = prom_getchild(prom_root_node);
		for (node = prom_searchsiblings(root, "SUNW,ffb"); node;
		     node = prom_searchsiblings(prom_getsibling(node), "SUNW,ffb")) {
			sbusfb_init_fb(node, prom_root_node, FBTYPE_CREATOR, NULL);
		}
	}
#endif
#ifdef CONFIG_SUN4
	sbusfb_init_fb(0, 0, FBTYPE_SUN2BW, NULL);
#endif
#if defined(CONFIG_FB_CGFOURTEEN) && !defined(__sparc_v9__)
	{
		int root, node;
		root = prom_getchild(prom_root_node);
		root = prom_searchsiblings(root, "obio");
		if (root && 
		    (node = prom_searchsiblings(prom_getchild(root), "cgfourteen"))) {
			sbusfb_init_fb(node, root, FBTYPE_MDICOLOR, NULL);
		}
	}
#endif
	if (!SBus_chain) return;
	for_all_sbusdev(sbdp, sbus) {
		type = known_card(sbdp->prom_name);
		if (type == FBTYPE_NOTYPE) continue;
		if (prom_getproperty(sbdp->prom_node, "emulation", prom_name, sizeof(prom_name)) > 0) {
			type = known_card(prom_name);
			if (type == FBTYPE_NOTYPE) type = known_card(sbdp->prom_name);
		}
		prom_apply_sbus_ranges(sbdp->my_bus, &sbdp->reg_addrs[0],
				       sbdp->num_registers, sbdp);
		sbusfb_init_fb(sbdp->prom_node, sbdp->my_bus->prom_node, type, sbdp);
	}
}	
