/*
 * drivers/sbus/char/vfc_dev.c
 *
 * Driver for the Videopix Frame Grabber.
 * 
 * In order to use the VFC you need to program the video controller
 * chip. This chip is the Phillips SAA9051.  You need to call their
 * documentation ordering line to get the docs.
 *
 * There is very little documentation on the VFC itself.  There is
 * some useful info that can be found in the manuals that come with
 * the card.  I will hopefully write some better docs at a later date.
 *
 * Copyright (C) 1996 Manish Vachharajani (mvachhar@noc.rutgers.edu)
 * */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/malloc.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/sbus.h>
#include <asm/delay.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#define VFC_MAJOR (60)

#if 0
#define VFC_IOCTL_DEBUG
#endif

#include "vfc.h"
#include <asm/vfc_ioctls.h>

struct vfc_dev **vfc_dev_lst;
static char vfcstr[]="vfc";
static unsigned char saa9051_init_array[VFC_SAA9051_NR]=
{ 0x00, 0x64, 0x72, 0x52,
  0x36, 0x18, 0xff, 0x20,
  0xfc, 0x77, 0xe3, 0x50,
  0x3e};

void vfc_lock_device(struct vfc_dev *dev) {
	down(&dev->device_lock_sem);
}

void vfc_unlock_device(struct vfc_dev *dev) {
	up(&dev->device_lock_sem);
}


void vfc_captstat_reset(struct vfc_dev *dev) 
{
	dev->control_reg |= VFC_CONTROL_CAPTRESET;
	dev->regs->control=dev->control_reg;
	dev->control_reg &= ~VFC_CONTROL_CAPTRESET;
	dev->regs->control=dev->control_reg;
	dev->control_reg |= VFC_CONTROL_CAPTRESET;
	dev->regs->control=dev->control_reg;
	return;
}

void vfc_memptr_reset(struct vfc_dev *dev) 
{
	dev->control_reg |= VFC_CONTROL_MEMPTR;
	dev->regs->control = dev->control_reg; 
	dev->control_reg &= ~VFC_CONTROL_MEMPTR;
	dev->regs->control = dev->control_reg; 
	dev->control_reg |= VFC_CONTROL_MEMPTR; 
	dev->regs->control = dev->control_reg; 
	return;
}

int vfc_csr_init(struct vfc_dev *dev)
{
	dev->control_reg = 0x80000000;
	dev->regs->control = dev->control_reg;
	udelay(200); 
	dev->control_reg &= ~0x80000000;
	dev->regs->control = dev->control_reg;
	udelay(100); 
	dev->regs->i2c_magic2 = 0x0f000000; 

	vfc_memptr_reset(dev);

	dev->control_reg &= ~VFC_CONTROL_DIAGMODE;
	dev->control_reg &= ~VFC_CONTROL_CAPTURE;
	dev->control_reg |= 0x40000000;
	dev->regs->control=dev->control_reg; 

	vfc_captstat_reset(dev);

	return 0;
}

int vfc_saa9051_init(struct vfc_dev *dev)
{
	int i;
	for(i=0;i<VFC_SAA9051_NR;i++) {
		dev->saa9051_state_array[i]=saa9051_init_array[i];
	}
	vfc_i2c_sendbuf(dev,VFC_SAA9051_ADDR,
			dev->saa9051_state_array, VFC_SAA9051_NR);
	return 0;
}

int init_vfc_hw(struct vfc_dev *dev) 
{
	vfc_lock_device(dev);
	vfc_csr_init(dev);

	vfc_pcf8584_init(dev);
	vfc_init_i2c_bus(dev); /* hopefully this doesn't undo the magic
				  sun code above*/
	vfc_saa9051_init(dev);
	vfc_unlock_device(dev);
	return 0; 
}

int init_vfc_devstruct(struct vfc_dev *dev, int instance) 
{
	dev->instance=instance;
	dev->device_lock_sem=MUTEX;
	dev->control_reg=0;
	dev->poll_wait=NULL;
	dev->busy=0;
	return 0;
}

int init_vfc_device(struct linux_sbus_device *sdev,struct vfc_dev *dev,
		    int instance) {
	if(!dev) {
		printk(KERN_ERR "VFC: Bogus pointer passed\n");
		return -ENOMEM;
	}
	printk("Initializing vfc%d\n",instance);
	dev->regs=NULL;
	prom_apply_sbus_ranges(sdev->my_bus, &sdev->reg_addrs[0], sdev->num_registers, sdev);
	dev->regs=sparc_alloc_io(sdev->reg_addrs[0].phys_addr, 0,
				 sizeof(struct vfc_regs), vfcstr, 
				 sdev->reg_addrs[0].which_io, 0x0);
	dev->which_io=sdev->reg_addrs[0].which_io;
	dev->phys_regs=(struct vfc_regs *)sdev->reg_addrs[0].phys_addr;
	if(!dev->regs) return -EIO;

	printk("vfc%d: registers mapped at phys_addr: 0x%lx\n    virt_addr: 0x%lx\n",
	       instance,(unsigned long)sdev->reg_addrs[0].phys_addr,(unsigned long)dev->regs);

	if(init_vfc_devstruct(dev,instance)) return -EINVAL;
	if(init_vfc_hw(dev)) return -EIO;

	return 0;
}


struct vfc_dev *vfc_get_dev_ptr(int instance) 
{
	return vfc_dev_lst[instance];
}

static int vfc_open(struct inode *inode, struct file *file) 
{
	struct vfc_dev *dev;
	dev=vfc_get_dev_ptr(MINOR(inode->i_rdev));
	if(!dev) return -ENODEV;
	if(dev->busy) return -EBUSY;
	dev->busy=1;
	MOD_INC_USE_COUNT;
	vfc_lock_device(dev);
	
	vfc_csr_init(dev);
	vfc_pcf8584_init(dev);
	vfc_init_i2c_bus(dev);
	vfc_saa9051_init(dev);
	vfc_memptr_reset(dev);
	vfc_captstat_reset(dev);
	
	vfc_unlock_device(dev);
	return 0;
}

static void vfc_release(struct inode *inode,struct file *file) 
{
	struct vfc_dev *dev;
	dev=vfc_get_dev_ptr(MINOR(inode->i_rdev));
	if(!dev) return;
	if(!dev->busy) return;
	dev->busy=0;
	MOD_DEC_USE_COUNT;
	return;
}

static int vfc_debug(struct vfc_dev *dev, int cmd, unsigned long arg) 
{
	struct vfc_debug_inout inout;
	unsigned char *buffer;
	int ret;

	if(!suser()) return -EPERM;

	switch(cmd) {
	case VFC_I2C_SEND:
		if(copy_from_user(&inout, (void *)arg, sizeof(inout))) {
			return -EFAULT;
		}

		buffer = kmalloc(inout.len*sizeof(char), GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;

		if(copy_from_user(buffer, inout.buffer, 
				  inout.len*sizeof(char));) {
			kfree_s(buffer,inout.len*sizeof(char));
			return -EFAULT;
		}
		

		vfc_lock_device(dev);
		inout.ret=
			vfc_i2c_sendbuf(dev,inout.addr & 0xff,
					inout.buffer,inout.len);

		if (copy_to_user((void *)arg,&inout,sizeof(inout))) {
			kfree_s(buffer, inout.len);
			return -EFAULT;
		}
		vfc_unlock_device(dev);

		break;
	case VFC_I2C_RECV:
		if (copy_from_user(&inout, (void *)arg, sizeof(inout))) {
			return -EFAULT;
		}

		buffer = kmalloc(inout.len, GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;
		memset(buffer,0,inout.len*sizeof(char));
		vfc_lock_device(dev);
		inout.ret=
			vfc_i2c_recvbuf(dev,inout.addr & 0xff
					,buffer,inout.len);
		vfc_unlock_device(dev);
		
		if (copy_to_user(inout.buffer, buffer, inout.len)) {
			kfree_s(buffer,inout.len);
			return -EFAULT;
		}
		if (copy_to_user((void *)arg,&inout,sizeof(inout))) {
			kfree_s(buffer,inout.len);
			return -EFAULT;
		}
		kfree_s(buffer,inout.len);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int vfc_capture_start(struct vfc_dev *dev) 
{
	vfc_captstat_reset(dev);
	dev->control_reg=dev->regs->control;
	if((dev->control_reg & VFC_STATUS_CAPTURE)) {
		printk(KERN_ERR "vfc%d: vfc capture status not reset\n",
		       dev->instance);
		return -EIO;
	}

	vfc_lock_device(dev);
	dev->control_reg &= ~VFC_CONTROL_CAPTURE;
	dev->regs->control=dev->control_reg;
	dev->control_reg |= VFC_CONTROL_CAPTURE;
	dev->regs->control=dev->control_reg;
	dev->control_reg &= ~VFC_CONTROL_CAPTURE;
	dev->regs->control=dev->control_reg;
	vfc_unlock_device(dev);

	return 0;
}

int vfc_capture_poll(struct vfc_dev *dev) 
{
	int timeout=1000;
	while(!timeout--) {
		if((dev->regs->control & VFC_STATUS_CAPTURE)) break;
		vfc_i2c_delay_no_busy(dev,100);
	}
	if(!timeout) {
		printk(KERN_WARNING "vfc%d: capture timed out\n", dev->instance);
		return -ETIMEDOUT;
	}
	return 0;
}



static int vfc_set_control_ioctl(struct inode *inode, struct file *file, 
			  struct vfc_dev *dev, unsigned long arg) 
{
	int setcmd,ret=0;
	if (copy_from_user(&setcmd,(void *)arg,sizeof(unsigned int)))
		return -EFAULT;
	VFC_IOCTL_DEBUG_PRINTK(("vfc%d: IOCTL(VFCSCTRL) arg=0x%x\n",
				dev->instance,setcmd));
	switch(setcmd) {
	case MEMPRST:
		vfc_lock_device(dev);
		vfc_memptr_reset(dev);
		vfc_unlock_device(dev);
		ret=0;
		break;
	case CAPTRCMD:
		vfc_capture_start(dev);
		vfc_capture_poll(dev);
		break;
	case DIAGMODE:
		if(suser()) {
			vfc_lock_device(dev);
			dev->control_reg |= VFC_CONTROL_DIAGMODE;
			dev->regs->control = dev->control_reg;
			vfc_unlock_device(dev);
			ret=0;
		} else ret=-EPERM; 
		break;
	case NORMMODE:
		vfc_lock_device(dev);
		dev->control_reg &= ~VFC_CONTROL_DIAGMODE;
		dev->regs->control = dev->control_reg;
		vfc_unlock_device(dev);
		ret=0;
		break;
	case CAPTRSTR:
		vfc_capture_start(dev);
		ret=0;
		break;
	case CAPTRWAIT:
		vfc_capture_poll(dev);
		ret=0;
		break;
	default:
		ret=-EINVAL;
	}
	return ret;
}


int vfc_port_change_ioctl(struct inode *inode, struct file *file, 
			  struct vfc_dev *dev, unsigned long arg) 
{
	int ret=0;
	int cmd;

	if(copy_from_user(&cmd, (void *)arg, sizeof(unsigned int))) {
		VFC_IOCTL_DEBUG_PRINTK(("vfc%d: User passed bogus pointer to "
				  "vfc_port_change_ioctl\n",dev->instance));
		return -EFAULT;
	}
	
	VFC_IOCTL_DEBUG_PRINTK(("vfc%d: IOCTL(VFCPORTCHG) arg=0x%x\n",
			  dev->instance,cmd));

	switch(cmd) {
	case 1:
	case 2:
		VFC_SAA9051_SA(dev,VFC_SAA9051_HSY_START) = 0x72; 
		VFC_SAA9051_SA(dev,VFC_SAA9051_HSY_STOP) = 0x52;
		VFC_SAA9051_SA(dev,VFC_SAA9051_HC_START) = 0x36;
		VFC_SAA9051_SA(dev,VFC_SAA9051_HC_STOP) = 0x18;
		VFC_SAA9051_SA(dev,VFC_SAA9051_HORIZ_PEAK) = VFC_SAA9051_BP2;
		VFC_SAA9051_SA(dev,VFC_SAA9051_C3) = VFC_SAA9051_CT | VFC_SAA9051_SS3;
		VFC_SAA9051_SA(dev,VFC_SAA9051_SECAM_DELAY) = 0x3e;
		break;
	case 3:
		VFC_SAA9051_SA(dev,VFC_SAA9051_HSY_START) = 0x3a;
		VFC_SAA9051_SA(dev,VFC_SAA9051_HSY_STOP) = 0x17;
		VFC_SAA9051_SA(dev,VFC_SAA9051_HC_START) = 0xfa;
		VFC_SAA9051_SA(dev,VFC_SAA9051_HC_STOP) = 0xde;
		VFC_SAA9051_SA(dev,VFC_SAA9051_HORIZ_PEAK) = VFC_SAA9051_BY | VFC_SAA9051_PF | VFC_SAA9051_BP2;
		VFC_SAA9051_SA(dev,VFC_SAA9051_C3) = VFC_SAA9051_YC;
		VFC_SAA9051_SA(dev,VFC_SAA9051_SECAM_DELAY) = 0;
		VFC_SAA9051_SA(dev,VFC_SAA9051_C2) &= ~(VFC_SAA9051_SS0 | VFC_SAA9051_SS1);
		break;
	default:
		ret=-EINVAL;
		return ret;
		break;
	}

	switch(cmd) {
	case 1:
		VFC_SAA9051_SA(dev,VFC_SAA9051_C2) |= VFC_SAA9051_SS0 | VFC_SAA9051_SS1;
		break;
	case 2:
		VFC_SAA9051_SA(dev,VFC_SAA9051_C2) &= ~(VFC_SAA9051_SS0 | VFC_SAA9051_SS1);
		VFC_SAA9051_SA(dev,VFC_SAA9051_C2) |= VFC_SAA9051_SS0; 
		break;
	case 3:
		break;
	default:
		ret=-EINVAL;
		return ret;
		break;
	}
	VFC_SAA9051_SA(dev,VFC_SAA9051_C3) &= ~(VFC_SAA9051_SS2);
	ret=vfc_update_saa9051(dev);
	udelay(500);
	VFC_SAA9051_SA(dev,VFC_SAA9051_C3) |= (VFC_SAA9051_SS2);
	ret=vfc_update_saa9051(dev);
	return ret;
}

int vfc_set_video_ioctl(struct inode *inode, struct file *file, 
			struct vfc_dev *dev, unsigned long arg) 
{
	int ret=0;
	int cmd;
	if(copy_from_user(&cmd, (void *)arg, sizeof(unsigned int))) {
		VFC_IOCTL_DEBUG_PRINTK(("vfc%d: User passed bogus pointer to "
				  "vfc_set_video_ioctl\n",dev->instance));
		return ret;
	}
	
	VFC_IOCTL_DEBUG_PRINTK(("vfc%d: IOCTL(VFCSVID) arg=0x%x\n",
			  dev->instance,cmd));
	switch(cmd) {

	case STD_NTSC:
		VFC_SAA9051_SA(dev,VFC_SAA9051_C1) &= ~VFC_SAA9051_ALT;
		VFC_SAA9051_SA(dev,VFC_SAA9051_C1) |= VFC_SAA9051_YPN | 
			VFC_SAA9051_CCFR0 | VFC_SAA9051_CCFR1 | VFC_SAA9051_FS;
		ret=vfc_update_saa9051(dev);
		break;
	case STD_PAL:
		VFC_SAA9051_SA(dev,VFC_SAA9051_C1) &= ~(VFC_SAA9051_YPN | 
							VFC_SAA9051_CCFR1 | 
							VFC_SAA9051_CCFR0 |
							VFC_SAA9051_FS);
		VFC_SAA9051_SA(dev,VFC_SAA9051_C1) |= VFC_SAA9051_ALT;
		ret=vfc_update_saa9051(dev);
		break;

	case COLOR_ON:
		VFC_SAA9051_SA(dev,VFC_SAA9051_C1) |= VFC_SAA9051_CO;
		VFC_SAA9051_SA(dev,VFC_SAA9051_HORIZ_PEAK) &= ~(VFC_SAA9051_BY | VFC_SAA9051_PF);
		ret=vfc_update_saa9051(dev);
		break;
	case MONO:
		VFC_SAA9051_SA(dev,VFC_SAA9051_C1) &= ~(VFC_SAA9051_CO);
		VFC_SAA9051_SA(dev,VFC_SAA9051_HORIZ_PEAK) |= VFC_SAA9051_BY | VFC_SAA9051_PF;
		ret=vfc_update_saa9051(dev);
		break;
	default:
		ret=-EINVAL;
		break;
	}
	return ret;
}

int vfc_get_video_ioctl(struct inode *inode, struct file *file, 
			struct vfc_dev *dev, unsigned long arg) 
{
	int ret=0;
	unsigned int status=NO_LOCK;
	unsigned char buf[1];

	if(vfc_i2c_recvbuf(dev,VFC_SAA9051_ADDR,buf,1)) {
		printk(KERN_ERR "vfc%d: Unable to get status\n",dev->instance);
		return -EIO;
	}

	if(buf[0] & VFC_SAA9051_HLOCK) {
		status = NO_LOCK;
	} else if(buf[0] & VFC_SAA9051_FD) {
		if(buf[0] & VFC_SAA9051_CD)
			status=NTSC_COLOR;
		else
			status=NTSC_NOCOLOR;
	} else {
		if(buf[0] & VFC_SAA9051_CD)
			status=PAL_COLOR;
		else
			status=PAL_NOCOLOR;
	}
	VFC_IOCTL_DEBUG_PRINTK(("vfc%d: IOCTL(VFCGVID) returning status 0x%x; buf[0]=%x\n",dev->instance,
			  status,buf[0]));
	if (copy_to_user((void *)arg,&status,sizeof(unsigned int))) {
		VFC_IOCTL_DEBUG_PRINTK(("vfc%d: User passed bogus pointer to "
				 "vfc_get_video_ioctl\n",dev->instance));
		return ret;
	}
	return ret;
}

static int vfc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	      unsigned long arg) 
{
	int ret=0;
	unsigned int tmp;
	struct vfc_dev *dev;

	dev=vfc_get_dev_ptr(MINOR(inode->i_rdev));
	if(!dev) return -ENODEV;
	
	switch(cmd & 0x0000ffff) {
	case VFCGCTRL:
#if 0
		VFC_IOCTL_DEBUG_PRINTK(("vfc%d: IOCTL(VFCGCTRL)\n",dev->instance));
#endif
		tmp=dev->regs->control;
		if(copy_to_user((void *)arg,&tmp, sizeof(unsigned int))) {
			ret=-EFAULT;
			break;
		}
		ret=0;
		break;
	case VFCSCTRL:
		ret=vfc_set_control_ioctl(inode, file, dev, arg);
		break;
	case VFCGVID:
		ret=vfc_get_video_ioctl(inode,file,dev,arg);
		break;
	case VFCSVID:
		ret=vfc_set_video_ioctl(inode,file,dev,arg);
		break;
	case VFCHUE:
		VFC_IOCTL_DEBUG_PRINTK(("vfc%d: IOCTL(VFCHUE)\n",dev->instance));
		if(copy_from_user(&tmp,(void *)arg,sizeof(unsigned int))) {
			VFC_IOCTL_DEBUG_PRINTK(("vfc%d: User passed bogus pointer "
					  "to IOCTL(VFCHUE)",dev->instance));
			ret=-EFAULT;
		} else {
			VFC_SAA9051_SA(dev,VFC_SAA9051_HUE)=tmp;
			vfc_update_saa9051(dev);
			ret=0;
		}
		break;
	case VFCPORTCHG:
		ret=vfc_port_change_ioctl(inode, file, dev, arg);
		break;
	case VFCRDINFO:
		ret=-EINVAL;
		VFC_IOCTL_DEBUG_PRINTK(("vfc%d: IOCTL(VFCRDINFO)\n",dev->instance));
		break;
	default:
		ret=vfc_debug(vfc_get_dev_ptr(MINOR(inode->i_rdev)),
			      cmd,arg);
		break;
	}
	return ret;
}

static int vfc_mmap(struct inode *inode, struct file *file, 
	     struct vm_area_struct *vma) 
{
	unsigned int map_size,ret,map_offset;
	struct vfc_dev *dev;
	
	dev=vfc_get_dev_ptr(MINOR(inode->i_rdev));
	if(!dev) return -ENODEV;
	
	map_size=vma->vm_end - vma->vm_start;
	if(map_size > sizeof(struct vfc_regs)) 
		map_size=sizeof(struct vfc_regs);


	if(vma->vm_offset & ~PAGE_MASK) return -ENXIO;
	vma->vm_flags |= VM_SHM | VM_LOCKED | VM_IO | VM_MAYREAD | VM_MAYWRITE | VM_MAYSHARE;
	map_offset=(unsigned int)dev->phys_regs;
	ret=io_remap_page_range(vma->vm_start,map_offset,map_size, 
				vma->vm_page_prot, dev->which_io);
	if(ret) return -EAGAIN;
	vma->vm_inode=inode;
	inode->i_count++;
	return 0;
}


static int vfc_lseek(struct inode *inode, struct file *file, 
	      off_t offset, int origin) 
{
	return -ESPIPE;
}

static struct file_operations vfc_fops = {
	vfc_lseek,        /* vfc_lseek */
	NULL,        /* vfc_write */
	NULL,        /* vfc_read */
	NULL,        /* vfc_readdir */
	NULL,        /* vfc_select */
	vfc_ioctl,   
	vfc_mmap, 
	vfc_open,
	vfc_release,
};

static int vfc_probe(void)
{
	struct linux_sbus *bus;
	struct linux_sbus_device *sdev = NULL;
	int ret;
	int instance=0,cards=0;

	for_all_sbusdev(sdev,bus) {
		if (strcmp(sdev->prom_name,"vfc") == 0) {
			cards++;
			continue;
		}
	}

	if (!cards)
		return -ENODEV;

	vfc_dev_lst=(struct vfc_dev **)kmalloc(sizeof(struct vfc_dev *)*
					       (cards+1),
					       GFP_KERNEL);
	if(!vfc_dev_lst)
		return -ENOMEM;
	memset(vfc_dev_lst,0,sizeof(struct vfc_dev *)*(cards+1));
	vfc_dev_lst[cards]=NULL;

	ret=register_chrdev(VFC_MAJOR,vfcstr,&vfc_fops);
	if(ret) {
		printk(KERN_ERR "Unable to get major number %d\n",VFC_MAJOR);
		kfree(vfc_dev_lst);
		return -EIO;
	}

	instance=0;
	for_all_sbusdev(sdev,bus) {
		if (strcmp(sdev->prom_name,"vfc") == 0) {
			vfc_dev_lst[instance]=(struct vfc_dev *)
				kmalloc(sizeof(struct vfc_dev), GFP_KERNEL);
			if(vfc_dev_lst[instance] == NULL) return -ENOMEM;
			ret=init_vfc_device(sdev,
					    vfc_dev_lst[instance],
					    instance);
			if(ret) {
				printk(KERN_ERR "Unable to initialize"
				       " vfc%d device\n",instance);
			} else {
			}
		
			instance++;
			continue;
		}
	}

	return 0;
}

#ifdef MODULE
int init_module(void)
#else 
int vfc_init(void)
#endif
{
#ifdef MODULE
	register_symtab(0);
#endif
	return vfc_probe();
}

#ifdef MODULE
static void deinit_vfc_device(struct vfc_dev *dev)
{
	if(!dev) return;
	sparc_free_io((void *)dev->regs,sizeof(struct vfc_regs));
	kfree(dev);
}

void cleanup_module(void)
{
	struct vfc_dev **devp;
	unregister_chrdev(VFC_MAJOR,vfcstr);
	for(devp=vfc_dev_lst;*devp;devp++) {
		deinit_vfc_device(*devp);
	}
	kfree(vfc_dev_lst);
	return;
}
#endif


