/*
 *  linux/drivers/char/fbmem.c
 *
 *  Copyright (C) 1994 Martin Schaller
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/mman.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/init.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#ifdef __mc68000__
#include <asm/setup.h>
#endif
#ifdef __powerpc__
#include <asm/io.h>
#endif
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <linux/fb.h>


    /*
     *  Frame buffer device initialization and setup routines
     */

extern unsigned long acornfb_init(void);
extern void acornfb_setup(char *options, int *ints);
extern void amifb_init(void);
extern void amifb_setup(char *options, int *ints);
extern void atafb_init(void);
extern void atafb_setup(char *options, int *ints);
extern void macfb_init(void);
extern void macfb_setup(char *options, int *ints);
extern void cyberfb_init(void);
extern void cyberfb_setup(char *options, int *ints);
extern void retz3fb_init(void);
extern void retz3fb_setup(char *options, int *ints);
extern void clgenfb_init(void);
extern void clgenfb_setup(char *options, int *ints);
extern void vfb_init(void);
extern void vfb_setup(char *options, int *ints);
extern void offb_init(void);
extern void offb_setup(char *options, int *ints);
extern void atyfb_init(void);
extern void atyfb_setup(char *options, int *ints);
extern void dnfb_init(void);
extern void tgafb_init(void);
extern void virgefb_init(void);
extern void virgefb_setup(char *options, int *ints);
extern void resolver_video_setup(char *options, int *ints);
extern void s3triofb_init(void);
extern void s3triofb_setup(char *options, int *ints);
extern void vgafb_init(void);
extern void vgafb_setup(char *options, int *ints);
extern void vesafb_init(void);
extern void vesafb_setup(char *options, int *ints);
extern void mdafb_init(void);
extern void mdafb_setup(char *options, int *ints);
extern void hpfb_init(void);
extern void hpfb_setup(char *options, int *ints);
extern void sbusfb_init(void);
extern void sbusfb_setup(char *options, int *ints);
extern void promfb_init(void);
extern void promfb_setup(char *options, int *ints);

static struct {
	const char *name;
	void (*init)(void);
	void (*setup)(char *options, int *ints);
} fb_drivers[] __initdata = {
#ifdef CONFIG_FB_RETINAZ3
	{ "retz3", retz3fb_init, retz3fb_setup },
#endif
#ifdef CONFIG_FB_ACORN
	{ "acorn", acornfb_init, acornfb_setup },
#endif
#ifdef CONFIG_FB_AMIGA
	{ "amifb", amifb_init, amifb_setup },
#endif
#ifdef CONFIG_FB_ATARI
	{ "atafb", atafb_init, atafb_setup },
#endif
#ifdef CONFIG_FB_MAC
	{ "macfb", macfb_init, macfb_setup },
#endif
#ifdef CONFIG_FB_CYBER
	{ "cyber", cyberfb_init, cyberfb_setup },
#endif
#ifdef CONFIG_FB_CLGEN
	{ "clgen", clgenfb_init, clgenfb_setup },
#endif
#ifdef CONFIG_FB_OF
	{ "offb", offb_init, offb_setup },
#endif
#ifdef CONFIG_FB_ATY
	{ "atyfb", atyfb_init, atyfb_setup },
#endif
#ifdef CONFIG_APOLLO
	{ "apollo", dnfb_init, NULL },
#endif
#ifdef CONFIG_FB_S3TRIO
	{ "s3trio", s3triofb_init, s3triofb_setup },
#endif 
#ifdef CONFIG_FB_TGA
	{ "tga", tgafb_init, NULL },
#endif
#ifdef CONFIG_FB_VIRGE
	{ "virge", virgefb_init, virgefb_setup },
#endif
#ifdef CONFIG_FB_VGA
	{ "vga", vgafb_init, vgafb_setup },
#endif 
#ifdef CONFIG_FB_VESA
	{ "vesa", vesafb_init, vesafb_setup },
#endif 
#ifdef CONFIG_FB_MDA
	{ "mda", mdafb_init, mdafb_setup },
#endif 
#ifdef CONFIG_FB_HP300
	{ "hpfb", hpfb_init, hpfb_setup },
#endif 
#ifdef CONFIG_FB_SBUS
	{ "sbus", sbusfb_init, sbusfb_setup },
#endif
#ifdef CONFIG_FB_PROM
	{ "prom", promfb_init, promfb_setup },
#endif
#ifdef CONFIG_GSP_RESOLVER
	/* Not a real frame buffer device... */
	{ "resolver", NULL, resolver_video_setup },
#endif
#ifdef CONFIG_FB_VIRTUAL
	/* Must be last to avoid that vfb becomes your primary display */
	{ "vfb", vfb_init, vfb_setup },
#endif
};

#define NUM_FB_DRIVERS	(sizeof(fb_drivers)/sizeof(*fb_drivers))

static void (*pref_init_funcs[FB_MAX])(void);
static int num_pref_init_funcs __initdata = 0;


#define GET_INODE(i) MKDEV(FB_MAJOR, (i) << FB_MODES_SHIFT)
#define GET_FB_VAR_IDX(node) (MINOR(node) & ((1 << FB_MODES_SHIFT)-1)) 

struct fb_info *registered_fb[FB_MAX];
int num_registered_fb = 0;

char con2fb_map[MAX_NR_CONSOLES];

static inline int PROC_CONSOLE(void)
{
	if (!current->tty)
		return fg_console;

	if (current->tty->driver.type != TTY_DRIVER_TYPE_CONSOLE)
		/* XXX Should report error here? */
		return fg_console;

	if (MINOR(current->tty->device) < 1)
		return fg_console;

	return MINOR(current->tty->device) - 1;
}

#ifdef CONFIG_PROC_FS
static int fbmem_read_proc(char *buf, char **start, off_t offset,
			   int len, int *eof, void *private)
{
	struct fb_info **fi;

	len = 0;
	for (fi = registered_fb; fi < &registered_fb[FB_MAX] && len < 4000; fi++)
		if (*fi)
			len += sprintf(buf + len, "%d %s\n",
				       GET_FB_IDX((*fi)->node),
				       (*fi)->modename);
	*start = buf + offset;
	return len > offset ? len - offset : 0;
}
#endif

static ssize_t
fb_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	struct inode *inode = file->f_dentry->d_inode;
	int fbidx = GET_FB_IDX(inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];
	struct fb_ops *fb = info->fbops;
	struct fb_fix_screeninfo fix;
	char *base_addr;
	ssize_t copy_size;

	if (! fb || ! info->disp)
		return -ENODEV;

	fb->fb_get_fix(&fix,PROC_CONSOLE(), info);
	base_addr=info->disp->screen_base;
	copy_size=(count + p <= fix.smem_len ? count : fix.smem_len - p);
	if (copy_to_user(buf, base_addr+p, copy_size))
	    return -EFAULT;
	*ppos += copy_size;
	return copy_size;
}

static ssize_t
fb_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	struct inode *inode = file->f_dentry->d_inode;
	int fbidx = GET_FB_IDX(inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];
	struct fb_ops *fb = info->fbops;
	struct fb_fix_screeninfo fix;
	char *base_addr;
	ssize_t copy_size;

	if (! fb || ! info->disp)
		return -ENODEV;

	fb->fb_get_fix(&fix, PROC_CONSOLE(), info);
	base_addr=info->disp->screen_base;
	copy_size=(count + p <= fix.smem_len ? count : fix.smem_len - p);
	if (copy_from_user(base_addr+p, buf, copy_size))
	    return -EFAULT;
	file->f_pos += copy_size;
	return copy_size;
}


static void set_con2fb_map(int unit, int newidx)
{
    int oldidx = con2fb_map[unit];
    struct fb_info *oldfb, *newfb;
    struct vc_data *conp;

    if (newidx != con2fb_map[unit]) {
       oldfb = registered_fb[oldidx];
       newfb = registered_fb[newidx];
       if (newfb->fbops->fb_open(newfb,0))
	   return;
       oldfb->fbops->fb_release(oldfb,0);
       conp = fb_display[unit].conp;
       con2fb_map[unit] = newidx;
       fb_display[unit] = *(newfb->disp);
       fb_display[unit].conp = conp;
       fb_display[unit].fb_info = newfb;
       if (!newfb->changevar)
	   newfb->changevar = oldfb->changevar;
       /* tell console var has changed */
       if (newfb->changevar)
	   newfb->changevar(unit);
    }
}

#ifdef CONFIG_KMOD
static void try_to_load(int fb)
{
	char modname[16];

	sprintf(modname, "fb%d", fb);
	request_module(modname);
}
#endif /* CONFIG_KMOD */

static int 
fb_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	 unsigned long arg)
{
	int fbidx = GET_FB_IDX(inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];
	struct fb_ops *fb = info->fbops;
	struct fb_cmap cmap;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	struct fb_con2fbmap con2fb;
	int i;
	
	if (! fb)
		return -ENODEV;
	switch (cmd) {
	case FBIOGET_VSCREENINFO:
		if ((i = fb->fb_get_var(&var, PROC_CONSOLE(), info)))
			return i;
		return copy_to_user((void *) arg, &var,
				    sizeof(var)) ? -EFAULT : 0;
	case FBIOPUT_VSCREENINFO:
		if (copy_from_user(&var, (void *) arg, sizeof(var)))
			return -EFAULT;
		if ((i = fb->fb_set_var(&var, PROC_CONSOLE(), info)))
			return i;
		if (copy_to_user((void *) arg, &var, sizeof(var)))
			return -EFAULT;
		return 0;
	case FBIOGET_FSCREENINFO:
		if ((i = fb->fb_get_fix(&fix, PROC_CONSOLE(), info)))
			return i;
		return copy_to_user((void *) arg, &fix, sizeof(fix)) ?
			-EFAULT : 0;
	case FBIOPUTCMAP:
		if (copy_from_user(&cmap, (void *) arg, sizeof(cmap)))
			return -EFAULT;
		return (fb->fb_set_cmap(&cmap, 0, PROC_CONSOLE(), info));
	case FBIOGETCMAP:
		if (copy_from_user(&cmap, (void *) arg, sizeof(cmap)))
			return -EFAULT;
		return (fb->fb_get_cmap(&cmap, 0, PROC_CONSOLE(), info));
	case FBIOPAN_DISPLAY:
		if (copy_from_user(&var, (void *) arg, sizeof(var)))
			return -EFAULT;
		if ((i=fb->fb_pan_display(&var, PROC_CONSOLE(), info)))
			return i;
		if (copy_to_user((void *) arg, &var, sizeof(var)))
			return -EFAULT;
		return i;
	case FBIOGET_CON2FBMAP:
		if (copy_from_user(&con2fb, (void *)arg, sizeof(con2fb)))
			return -EFAULT;
		if (con2fb.console < 1 || con2fb.console > MAX_NR_CONSOLES)
		    return -EINVAL;
		con2fb.framebuffer = con2fb_map[con2fb.console-1];
		return copy_to_user((void *)arg, &con2fb,
				    sizeof(con2fb)) ? -EFAULT : 0;
	case FBIOPUT_CON2FBMAP:
		if (copy_from_user(&con2fb, (void *)arg, sizeof(con2fb)))
			return - EFAULT;
		if (con2fb.console < 0 || con2fb.console > MAX_NR_CONSOLES)
		    return -EINVAL;
		if (con2fb.framebuffer < 0 || con2fb.framebuffer >= FB_MAX)
		    return -EINVAL;
#ifdef CONFIG_KMOD
		if (!registered_fb[con2fb.framebuffer])
		    try_to_load(con2fb.framebuffer);
#endif /* CONFIG_KMOD */
		if (!registered_fb[con2fb.framebuffer])
		    return -EINVAL;
		if (con2fb.console != 0)
		    set_con2fb_map(con2fb.console-1, con2fb.framebuffer);
		else
		    /* set them all */
		    for (i = 0; i < MAX_NR_CONSOLES; i++)
			set_con2fb_map(i, con2fb.framebuffer);
		return 0;
	default:
		return fb->fb_ioctl(inode, file, cmd, arg, PROC_CONSOLE(),
				    info);
	}
}

static int 
fb_mmap(struct file *file, struct vm_area_struct * vma)
{
	int fbidx = GET_FB_IDX(file->f_dentry->d_inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];
	struct fb_ops *fb = info->fbops;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	unsigned long start;
	u32 len;

	if (!fb)
		return -ENODEV;
	if (fb->fb_mmap)
		return fb->fb_mmap(info, file, vma);
	fb->fb_get_fix(&fix, PROC_CONSOLE(), info);

	/* frame buffer memory */
	start = (unsigned long)fix.smem_start;
	len = (start & ~PAGE_MASK)+fix.smem_len;
	start &= PAGE_MASK;
	len = (len+~PAGE_MASK) & PAGE_MASK;
	if (vma->vm_offset >= len) {
		/* memory mapped io */
		vma->vm_offset -= len;
		fb->fb_get_var(&var, PROC_CONSOLE(), info);
		if (var.accel_flags)
			return -EINVAL;
		start = (unsigned long)fix.mmio_start;
		len = (start & ~PAGE_MASK)+fix.mmio_len;
		start &= PAGE_MASK;
		len = (len+~PAGE_MASK) & PAGE_MASK;
	}
	if ((vma->vm_end - vma->vm_start + vma->vm_offset) > len)
		return -EINVAL;
	vma->vm_offset += start;
	if (vma->vm_offset & ~PAGE_MASK)
		return -ENXIO;
#if defined(__mc68000__)
	if (CPU_IS_020_OR_030)
		pgprot_val(vma->vm_page_prot) |= _PAGE_NOCACHE030;
	if (CPU_IS_040_OR_060) {
		pgprot_val(vma->vm_page_prot) &= _CACHEMASK040;
		/* Use no-cache mode, serialized */
		pgprot_val(vma->vm_page_prot) |= _PAGE_NOCACHE_S;
	}
#elif defined(__powerpc__)
	pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE|_PAGE_GUARDED;
#elif defined(__alpha__)
	/* Caching is off in the I/O space quadrant by design.  */
#elif defined(__sparc__)
	/* Should never get here, all fb drivers should have their own
	   mmap routines */
#else
#warning What do we have to do here??
#endif
	if (remap_page_range(vma->vm_start, vma->vm_offset,
			     vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
	vma->vm_file = file;
	file->f_count++;
	return 0;
}

static int
fb_open(struct inode *inode, struct file *file)
{
	int fbidx = GET_FB_IDX(inode->i_rdev);
	struct fb_info *info;

#ifdef CONFIG_KMOD
	if (!(info = registered_fb[fbidx]))
		try_to_load(fbidx);
#endif /* CONFIG_KMOD */
	if (!(info = registered_fb[fbidx]))
		return -ENODEV;
	return info->fbops->fb_open(info,1);
}

static int 
fb_release(struct inode *inode, struct file *file)
{
	int fbidx = GET_FB_IDX(inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];

	info->fbops->fb_release(info,1);
	return 0;
}

static struct file_operations fb_fops = {
	NULL,		/* lseek	*/
	fb_read,	/* read		*/
	fb_write,	/* write	*/
	NULL,		/* readdir 	*/
	NULL,		/* poll 	*/
	fb_ioctl,	/* ioctl 	*/
	fb_mmap,	/* mmap		*/
	fb_open,	/* open 	*/
	fb_release,	/* release 	*/
	NULL		/* fsync 	*/
};

static inline void take_over_console(struct consw *sw)
{
    int i;
    extern void set_palette(void);

    conswitchp = sw;
    conswitchp->con_startup();

    for (i = 0; i < MAX_NR_CONSOLES; i++) {
	if (!vc_cons[i].d || !vc_cons[i].d->vc_sw)
	    continue;
	if (i == fg_console &&
	    vc_cons[i].d->vc_sw->con_save_screen)
		vc_cons[i].d->vc_sw->con_save_screen(vc_cons[i].d);
	vc_cons[i].d->vc_sw->con_deinit(vc_cons[i].d);
	vc_cons[i].d->vc_sw = conswitchp;
	vc_cons[i].d->vc_sw->con_init(vc_cons[i].d, 0);
    }
    set_palette();
}

int
register_framebuffer(struct fb_info *fb_info)
{
	int i, j;
	static int fb_ever_opened[FB_MAX];
	static int first = 1;

	if (num_registered_fb == FB_MAX)
		return -ENXIO;
	num_registered_fb++;
	for (i = 0 ; i < FB_MAX; i++)
		if (!registered_fb[i])
			break;
	fb_info->node=GET_INODE(i);
	registered_fb[i] = fb_info;
	if (!fb_ever_opened[i]) {
		/*
		 *  We assume initial frame buffer devices can be opened this
		 *  many times
		 */
		for (j = 0; j < MAX_NR_CONSOLES; j++)
			if (con2fb_map[j] == i)
				fb_info->fbops->fb_open(fb_info,0);
		fb_ever_opened[i] = 1;
	}

	if (first) {
		first = 0;
		take_over_console(&fb_con);
	}

	return 0;
}

int
unregister_framebuffer(const struct fb_info *fb_info)
{
	int i, j;

	i = GET_FB_IDX(fb_info->node);
	for (j = 0; j < MAX_NR_CONSOLES; j++)
		if (con2fb_map[j] == i)
			return -EBUSY;
	if (!registered_fb[i])
		return -EINVAL; 
	registered_fb[i]=NULL;
	num_registered_fb--;
	return 0;
}

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_fbmem;
#endif

__initfunc(void
fbmem_init(void))
{
	int i;

#ifdef CONFIG_PROC_FS
	proc_fbmem = create_proc_entry("fb", 0, 0);
	if (proc_fbmem)
		proc_fbmem->read_proc = fbmem_read_proc;
#endif

	if (register_chrdev(FB_MAJOR,"fb",&fb_fops))
		printk("unable to get major %d for fb devs\n", FB_MAJOR);

	/*
	 *  Probe for all builtin frame buffer devices
	 */
	for (i = 0; i < num_pref_init_funcs; i++)
		pref_init_funcs[i]();

	for (i = 0; i < NUM_FB_DRIVERS; i++)
		if (fb_drivers[i].init)
			fb_drivers[i].init();
}


int fbmon_valid_timings(u_int pixclock, u_int htotal, u_int vtotal,
			const struct fb_info *fb_info)
{
#if 0
	/*
	 * long long divisions .... $#%%#$ 
	 */
    unsigned long long hpicos, vpicos;
    const unsigned long long _1e12 = 1000000000000ULL;
    const struct fb_monspecs *monspecs = &fb_info->monspecs;

    hpicos = (unsigned long long)htotal*(unsigned long long)pixclock;
    vpicos = (unsigned long long)vtotal*(unsigned long long)hpicos;
    if (!vpicos)
	return 0;

    if (monspecs->hfmin == 0)
	return 1;

    if (hpicos*monspecs->hfmin > _1e12 || hpicos*monspecs->hfmax < _1e12 ||
	vpicos*monspecs->vfmin > _1e12 || vpicos*monspecs->vfmax < _1e12)
	return 0;
#endif
    return 1;
}

int fbmon_dpms(const struct fb_info *fb_info)
{
    return fb_info->monspecs.dpms;
}


    /*
     *  Command line options
     */

__initfunc(void video_setup(char *options, int *ints))
{
    int i, j;

    if (!options || !*options)
	    return;

    if (!strncmp(options, "map:", 4)) {
	    options += 4;
	    if (*options)
		    for (i = 0, j = 0; i < MAX_NR_CONSOLES; i++) {
			    if (!options[j])
				    j = 0;
			    con2fb_map[i] = (options[j++]-'0') % FB_MAX;
		    }
	    return;
    }

    if (num_pref_init_funcs == FB_MAX)
	    return;

    for (i = 0; i < NUM_FB_DRIVERS; i++) {
	    j = strlen(fb_drivers[i].name);
	    if (!strncmp(options, fb_drivers[i].name, j) &&
		options[j] == ':') {
		    if (!strcmp(options+j+1, "off"))
			    fb_drivers[i].init = NULL;
		    else {
			    if (fb_drivers[i].init) {
				    pref_init_funcs[num_pref_init_funcs++] =
					    fb_drivers[i].init;
				    fb_drivers[i].init = NULL;
			    }
			    if (fb_drivers[i].setup)
				    fb_drivers[i].setup(options+j+1, ints);
		    }
		    return;
	    }
    }
    /*
     * If we get here no fb was specified and we default to pass the
     * options to the first frame buffer that has an init and a setup
     * function.
     */
    for (i = 0; i < NUM_FB_DRIVERS; i++) {
	    if (fb_drivers[i].init && fb_drivers[i].setup) {
		    pref_init_funcs[num_pref_init_funcs++] =
			    fb_drivers[i].init;
		    fb_drivers[i].init = NULL;

		    fb_drivers[i].setup(options, ints);
		    return;
	    }
    }
}


    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(register_framebuffer);
EXPORT_SYMBOL(unregister_framebuffer);
