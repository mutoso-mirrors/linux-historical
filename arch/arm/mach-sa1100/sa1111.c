/*
 * linux/arch/arm/mach-sa1100/sa1111.c
 *
 * SA1111 support
 *
 * Original code by John Dorsey
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This file contains all generic SA1111 support.
 *
 * All initialization functions provided here are intended to be called
 * from machine specific code with proper arguments when required.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>

#include <asm/hardware/sa1111.h>

/*
 * We keep the following data for the overall SA1111.  Note that the
 * struct device and struct resource are "fake"; they should be supplied
 * by the bus above us.  However, in the interests of getting all SA1111
 * drivers converted over to the device model, we provide this as an
 * anchor point for all the other drivers.
 */
struct sa1111 {
	struct device	dev;
	struct resource	res;
	int		irq;
	spinlock_t	lock;
	void		*base;
};

/*
 * We _really_ need to eliminate this.  Its only users
 * are the PWM and DMA checking code.
 */
static struct sa1111 *g_sa1111;

static struct sa1111_dev usb_dev = {
	.dev = {
		.name	= "Intel Corporation SA1111 [USB Controller]",
	},
	.skpcr_mask	= SKPCR_UCLKEN,
	.devid		= SA1111_DEVID_USB,
	.irq = {
		USBPWR
		NIRQHCIM,
		HCIBUFFACC,
		HCIRMTWKP,
		NHCIMFCIR,
		USB_PORT_RESUME
	},
};

static struct sa1111_dev sac_dev = {
	.dev = {
		.name	= "Intel Corporation SA1111 [Audio Controller]",
	},
	.skpcr_mask	= SKPCR_I2SCLKEN | SKPCR_L3CLKEN,
	.devid		= SA1111_DEVID_SAC,
	.irq = {
		AUDXMTDMADONEA,
		AUDXMTDMADONEB,
		AUDRCVDMADONEA,
		AUDRCVDMADONEB
	},
};

static struct sa1111_dev ssp_dev = {
	.dev = {
		.name	= "Intel Corporation SA1111 [SSP Controller]",
	},
	.skpcr_mask	= SKPCR_SCLKEN,
	.devid		= SA1111_DEVID_SSP,
};

static struct sa1111_dev kbd_dev = {
	.dev = {
		.name	= "Intel Corporation SA1111 [PS2]",
	},
	.skpcr_mask	= SKPCR_PTCLKEN,
	.devid		= SA1111_DEVID_PS2,
	.irq = {
		IRQ_TPRXINT,
		IRQ_TPTXINT
	},
};

static struct sa1111_dev mse_dev = {
	.dev = {
		.name	= "Intel Corporation SA1111 [PS2]",
	},
	.skpcr_mask	= SKPCR_PMCLKEN,
	.devid		= SA1111_DEVID_PS2,
	.irq = {
		IRQ_MSRXINT,
		IRQ_MSTXINT
	},
};

static struct sa1111_dev int_dev = {
	.dev = {
		.name	= "Intel Corporation SA1111 [Interrupt Controller]",
	},
	.skpcr_mask	= 0,
	.devid		= SA1111_DEVID_INT,
};

static struct sa1111_dev pcmcia_dev = {
	.dev = {
		.name	= "Intel Corporation SA1111 [PCMCIA Controller]",
	},
	.skpcr_mask	= 0,
	.devid		= SA1111_DEVID_PCMCIA,
	.irq = {
		S0_READY_NINT,
		S0_CD_VALID,
		S0_BVD1_STSCHG,
		S1_READY_NINT,
		S1_CD_VALID,
		S1_BVD1_STSCHG,
	},
};

static struct sa1111_dev *devs[] = {
	&usb_dev,
	&sac_dev,
	&ssp_dev,
	&kbd_dev,
	&mse_dev,
	&int_dev,
	&pcmcia_dev,
};

static unsigned int dev_offset[] = {
	SA1111_USB,
	0x0600,
	0x0800,
	SA1111_KBD,
	SA1111_MSE,
	SA1111_INTC,
	0x1800,
};

/*
 * SA1111 interrupt support.  Since clearing an IRQ while there are
 * active IRQs causes the interrupt output to pulse, the upper levels
 * will call us again if there are more interrupts to process.
 */
static void
sa1111_irq_handler(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs)
{
	unsigned int stat0, stat1, i;

	desc->chip->ack(irq);

	stat0 = INTSTATCLR0;
	stat1 = INTSTATCLR1;

	if (stat0 == 0 && stat1 == 0) {
		do_bad_IRQ(irq, desc, regs);
		return;
	}

	for (i = IRQ_SA1111_START; stat0; i++, stat0 >>= 1)
		if (stat0 & 1)
			do_edge_IRQ(i, irq_desc + i, regs);

	for (i = IRQ_SA1111_START + 32; stat1; i++, stat1 >>= 1)
		if (stat1 & 1)
			do_edge_IRQ(i, irq_desc + i, regs);

	/* For level-based interrupts */
	desc->chip->unmask(irq);
}

#define SA1111_IRQMASK_LO(x)	(1 << (x - IRQ_SA1111_START))
#define SA1111_IRQMASK_HI(x)	(1 << (x - IRQ_SA1111_START - 32))

static void sa1111_ack_lowirq(unsigned int irq)
{
	INTSTATCLR0 = SA1111_IRQMASK_LO(irq);
}

static void sa1111_mask_lowirq(unsigned int irq)
{
	INTEN0 &= ~SA1111_IRQMASK_LO(irq);
}

static void sa1111_unmask_lowirq(unsigned int irq)
{
	INTEN0 |= SA1111_IRQMASK_LO(irq);
}

/*
 * Attempt to re-trigger the interrupt.  The SA1111 contains a register
 * (INTSET) which claims to do this.  However, in practice no amount of
 * manipulation of INTEN and INTSET guarantees that the interrupt will
 * be triggered.  In fact, its very difficult, if not impossible to get
 * INTSET to re-trigger the interrupt.
 */
static void sa1111_rerun_lowirq(unsigned int irq)
{
	unsigned int mask = SA1111_IRQMASK_LO(irq);
	int i;

	for (i = 0; i < 8; i++) {
		INTPOL0 ^= mask;
		INTPOL0 ^= mask;
		if (INTSTATCLR1 & mask)
			break;
	}

	if (i == 8)
		printk(KERN_ERR "Danger Will Robinson: failed to "
			"re-trigger IRQ%d\n", irq);
}

static int sa1111_type_lowirq(unsigned int irq, unsigned int flags)
{
	unsigned int mask = SA1111_IRQMASK_LO(irq);

	if (flags == IRQT_PROBE)
		return 0;

	if ((!(flags & __IRQT_RISEDGE) ^ !(flags & __IRQT_FALEDGE)) == 0)
		return -EINVAL;

	if (flags & __IRQT_RISEDGE)
		INTPOL0 &= ~mask;
	else
		INTPOL0 |= mask;

	return 0;
}

static struct irqchip sa1111_low_chip = {
	.ack		= sa1111_ack_lowirq,
	.mask		= sa1111_mask_lowirq,
	.unmask		= sa1111_unmask_lowirq,
	.rerun		= sa1111_rerun_lowirq,
	.type		= sa1111_type_lowirq,
};

static void sa1111_ack_highirq(unsigned int irq)
{
	INTSTATCLR1 = SA1111_IRQMASK_HI(irq);
}

static void sa1111_mask_highirq(unsigned int irq)
{
	INTEN1 &= ~SA1111_IRQMASK_HI(irq);
}

static void sa1111_unmask_highirq(unsigned int irq)
{
	INTEN1 |= SA1111_IRQMASK_HI(irq);
}

/*
 * Attempt to re-trigger the interrupt.  The SA1111 contains a register
 * (INTSET) which claims to do this.  However, in practice no amount of
 * manipulation of INTEN and INTSET guarantees that the interrupt will
 * be triggered.  In fact, its very difficult, if not impossible to get
 * INTSET to re-trigger the interrupt.
 */
static void sa1111_rerun_highirq(unsigned int irq)
{
	unsigned int mask = SA1111_IRQMASK_HI(irq);
	int i;

	for (i = 0; i < 8; i++) {
		INTPOL1 ^= mask;
		INTPOL1 ^= mask;
		if (INTSTATCLR1 & mask)
			break;
	}

	if (i == 8)
		printk(KERN_ERR "Danger Will Robinson: failed to "
			"re-trigger IRQ%d\n", irq);
}

static int sa1111_type_highirq(unsigned int irq, unsigned int flags)
{
	unsigned int mask = SA1111_IRQMASK_HI(irq);

	if (flags == IRQT_PROBE)
		return 0;

	if ((!(flags & __IRQT_RISEDGE) ^ !(flags & __IRQT_FALEDGE)) == 0)
		return -EINVAL;

	if (flags & __IRQT_RISEDGE)
		INTPOL1 &= ~mask;
	else
		INTPOL1 |= mask;

	return 0;
}

static struct irqchip sa1111_high_chip = {
	.ack		= sa1111_ack_highirq,
	.mask		= sa1111_mask_highirq,
	.unmask		= sa1111_unmask_highirq,
	.rerun		= sa1111_rerun_highirq,
	.type		= sa1111_type_highirq,
};

static void __init sa1111_init_irq(struct sa1111_dev *sadev)
{
	unsigned int irq;

	/*
	 * We're guaranteed that this region hasn't been taken.
	 */
	request_mem_region(sadev->res.start, 512, "irqs");

	/* disable all IRQs */
	sa1111_writel(0, sadev->mapbase + SA1111_INTEN0);
	sa1111_writel(0, sadev->mapbase + SA1111_INTEN1);
	sa1111_writel(0, sadev->mapbase + SA1111_WAKEEN0);
	sa1111_writel(0, sadev->mapbase + SA1111_WAKEEN1);

	/*
	 * detect on rising edge.  Note: Feb 2001 Errata for SA1111
	 * specifies that S0ReadyInt and S1ReadyInt should be '1'.
	 */
	sa1111_writel(0, sadev->mapbase + SA1111_INTPOL0);
	sa1111_writel(SA1111_IRQMASK_HI(S0_READY_NINT) |
		      SA1111_IRQMASK_HI(S1_READY_NINT),
		      sadev->mapbase + SA1111_INTPOL1);

	/* clear all IRQs */
	sa1111_writel(~0, sadev->mapbase + SA1111_INTSTATCLR0);
	sa1111_writel(~0, sadev->mapbase + SA1111_INTSTATCLR1);

	for (irq = IRQ_GPAIN0; irq <= SSPROR; irq++) {
		set_irq_chip(irq, &sa1111_low_chip);
		set_irq_handler(irq, do_edge_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	for (irq = AUDXMTDMADONEA; irq <= S1_BVD1_STSCHG; irq++) {
		set_irq_chip(irq, &sa1111_high_chip);
		set_irq_handler(irq, do_edge_IRQ);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

	/*
	 * Register SA1111 interrupt
	 */
	set_irq_type(sadev->irq[0], IRQT_RISING);
	set_irq_chained_handler(sadev->irq[0], sa1111_irq_handler);
}

static struct device_driver sa1111_device_driver;

/**
 *	sa1111_probe - probe for a single SA1111 chip.
 *	@phys_addr: physical address of device.
 *
 *	Probe for a SA1111 chip.  This must be called
 *	before any other SA1111-specific code.
 *
 *	Returns:
 *	%-ENODEV	device not found.
 *	%-EBUSY		physical address already marked in-use.
 *	%0		successful.
 */
static int __init
sa1111_probe(unsigned long phys_addr, int irq)
{
	struct sa1111 *sachip;
	unsigned long id;
	unsigned int has_devs;
	int i, ret = -ENODEV;

	sachip = kmalloc(sizeof(struct sa1111), GFP_KERNEL);
	if (!sachip)
		return -ENOMEM;

	memset(sachip, 0, sizeof(struct sa1111));

	spin_lock_init(&sachip->lock);

	strncpy(sachip->dev.name, "Intel Corporation SA1111", sizeof(sachip->dev.name));
	snprintf(sachip->dev.bus_id, sizeof(sachip->dev.bus_id), "%8.8lx", phys_addr);
	sachip->dev.driver = &sa1111_device_driver;
	sachip->dev.driver_data = sachip;

	sachip->res.name  = sachip->dev.name;
	sachip->res.start = phys_addr;
	sachip->res.end   = phys_addr + 0x2000;
	sachip->irq = irq;

	if (request_resource(&iomem_resource, &sachip->res)) {
		ret = -EBUSY;
		goto out;
	}

	sachip->base = ioremap(phys_addr, PAGE_SIZE * 2);
	if (!sachip->base) {
		ret = -ENOMEM;
		goto release;
	}

	/*
	 * Probe for the chip.  Only touch the SBI registers.
	 */
	id = sa1111_readl(sachip->base + SA1111_SKID);
	if ((id & SKID_ID_MASK) != SKID_SA1111_ID) {
		printk(KERN_DEBUG "SA1111 not detected: ID = %08lx\n", id);
		ret = -ENODEV;
		goto unmap;
	}

	/*
	 * We found the chip.
	 */
	ret = register_sys_device(&sachip->dev);
	if (ret)
		printk("sa1111 device_register failed: %d\n", ret);

	printk(KERN_INFO "SA1111 Microprocessor Companion Chip: "
		"silicon revision %lx, metal revision %lx\n",
		(id & SKID_SIREV_MASK)>>4, (id & SKID_MTREV_MASK));

	g_sa1111 = sachip;

	has_devs = ~0;
	if (machine_is_assabet() || machine_is_jornada720() ||
	    machine_is_badge4())
		has_devs &= ~(1 << 4);
	else
		has_devs &= ~(1 << 1);

	for (i = 0; i < ARRAY_SIZE(devs); i++) {
		if (!(has_devs & (1 << i)))
			continue;

		snprintf(devs[i]->dev.bus_id, sizeof(devs[i]->dev.bus_id),
			 "%4.4x", dev_offset[i]);

		devs[i]->dev.parent = &sachip->dev;
		devs[i]->dev.bus    = &sa1111_bus_type;
		devs[i]->res.start  = sachip->res.start + dev_offset[i];
		devs[i]->res.end    = devs[i]->res.start + 511;
		devs[i]->res.name   = devs[i]->dev.name;
		devs[i]->res.flags  = IORESOURCE_MEM;
		devs[i]->mapbase    = sachip->base + dev_offset[i];

		if (request_resource(&sachip->res, &devs[i]->res)) {
			printk("SA1111: failed to allocate resource for %s\n",
				devs[i]->res.name);
			continue;
		}

		device_register(&devs[i]->dev);
	}

	return 0;

 unmap:
	iounmap(sachip->base);
 release:
	release_resource(&sachip->res);
 out:
	kfree(sachip);
	return ret;
}

static void __sa1111_remove(struct sa1111 *sachip)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(devs); i++) {
		put_device(&devs[i]->dev);
		release_resource(&devs[i]->res);
	}

	iounmap(sachip->base);
	release_resource(&sachip->res);
	kfree(sachip);
}

/*
 * Bring the SA1111 out of reset.  This requires a set procedure:
 *  1. nRESET asserted (by hardware)
 *  2. CLK turned on from SA1110
 *  3. nRESET deasserted
 *  4. VCO turned on, PLL_BYPASS turned off
 *  5. Wait lock time, then assert RCLKEn
 *  7. PCR set to allow clocking of individual functions
 *
 * Until we've done this, the only registers we can access are:
 *   SBI_SKCR
 *   SBI_SMCR
 *   SBI_SKID
 */
static void sa1111_wake(struct sa1111 *sachip)
{
	unsigned long flags, r;

	spin_lock_irqsave(&sachip->lock, flags);

	/*
	 * First, set up the 3.6864MHz clock on GPIO 27 for the SA-1111:
	 * (SA-1110 Developer's Manual, section 9.1.2.1)
	 */
	GAFR |= GPIO_32_768kHz;
	GPDR |= GPIO_32_768kHz;
	TUCR = TUCR_3_6864MHz;

	/*
	 * Turn VCO on, and disable PLL Bypass.
	 */
	r = sa1111_readl(sachip->base + SA1111_SKCR);
	r &= ~SKCR_VCO_OFF;
	sa1111_writel(r, sachip->base + SA1111_SKCR);
	r |= SKCR_PLL_BYPASS | SKCR_OE_EN;
	sa1111_writel(r, sachip->base + SA1111_SKCR);

	/*
	 * Wait lock time.  SA1111 manual _doesn't_
	 * specify a figure for this!  We choose 100us.
	 */
	udelay(100);

	/*
	 * Enable RCLK.  We also ensure that RDYEN is set.
	 */
	r |= SKCR_RCLKEN | SKCR_RDYEN;
	sa1111_writel(r, sachip->base + SA1111_SKCR);

	/*
	 * Wait 14 RCLK cycles for the chip to finish coming out
	 * of reset. (RCLK=24MHz).  This is 590ns.
	 */
	udelay(1);

	/*
	 * Ensure all clocks are initially off.
	 */
	sa1111_writel(0, sachip->base + SA1111_SKPCR);

	spin_unlock_irqrestore(&sachip->lock, flags);
}

/*
 * Configure the SA1111 shared memory controller.
 */
void
sa1111_configure_smc(struct sa1111 *sachip, int sdram, unsigned int drac,
		     unsigned int cas_latency)
{
	unsigned int smcr = SMCR_DTIM | SMCR_MBGE | FInsrt(drac, SMCR_DRAC);

	if (cas_latency == 3)
		smcr |= SMCR_CLAT;

	sa1111_writel(smcr, sachip->base + SA1111_SMCR);
}

/*
 * According to the "Intel StrongARM SA-1111 Microprocessor Companion
 * Chip Specification Update" (June 2000), erratum #7, there is a
 * significant bug in the SA1111 SDRAM shared memory controller.  If
 * an access to a region of memory above 1MB relative to the bank base,
 * it is important that address bit 10 _NOT_ be asserted. Depending
 * on the configuration of the RAM, bit 10 may correspond to one
 * of several different (processor-relative) address bits.
 *
 * This routine only identifies whether or not a given DMA address
 * is susceptible to the bug.
 */
int sa1111_check_dma_bug(dma_addr_t addr)
{
	struct sa1111 *sachip = g_sa1111;
	unsigned int physaddr = SA1111_DMA_ADDR((unsigned int)addr);
	unsigned int smcr;

	/* Section 4.6 of the "Intel StrongARM SA-1111 Development Module
	 * User's Guide" mentions that jumpers R51 and R52 control the
	 * target of SA-1111 DMA (either SDRAM bank 0 on Assabet, or
	 * SDRAM bank 1 on Neponset). The default configuration selects
	 * Assabet, so any address in bank 1 is necessarily invalid.
	 */
	if ((machine_is_assabet() || machine_is_pfs168()) && addr >= 0xc8000000)
	  	return -1;

	/* The bug only applies to buffers located more than one megabyte
	 * above the start of the target bank:
	 */
	if (physaddr<(1<<20))
		return 0;

	smcr = sa1111_readl(sachip->base + SA1111_SMCR);
	switch (FExtr(smcr, SMCR_DRAC)) {
	case 01: /* 10 row + bank address bits, A<20> must not be set */
	  	if (physaddr & (1<<20))
		  	return -1;
		break;
	case 02: /* 11 row + bank address bits, A<23> must not be set */
	  	if (physaddr & (1<<23))
		  	return -1;
		break;
	case 03: /* 12 row + bank address bits, A<24> must not be set */
	  	if (physaddr & (1<<24))
		  	return -1;
		break;
	case 04: /* 13 row + bank address bits, A<25> must not be set */
	  	if (physaddr & (1<<25))
		  	return -1;
		break;
	case 05: /* 14 row + bank address bits, A<20> must not be set */
	  	if (physaddr & (1<<20))
		  	return -1;
		break;
	case 06: /* 15 row + bank address bits, A<20> must not be set */
	  	if (physaddr & (1<<20))
		  	return -1;
		break;
	default:
	  	printk(KERN_ERR "%s(): invalid SMCR DRAC value 0%lo\n",
		       __FUNCTION__, FExtr(smcr, SMCR_DRAC));
		return -1;
	}

	return 0;
}

int sa1111_init(struct device *parent, unsigned long phys, unsigned int irq)
{
	unsigned int val;
	int ret;

	ret = sa1111_probe(phys, irq);
	if (ret < 0)
		return ret;

	/*
	 * We found it.  Wake the chip up.
	 */
	sa1111_wake(g_sa1111);

	/*
	 * The SDRAM configuration of the SA1110 and the SA1111 must
	 * match.  This is very important to ensure that SA1111 accesses
	 * don't corrupt the SDRAM.  Note that this ungates the SA1111's
	 * MBGNT signal, so we must have called sa1110_mb_disable()
	 * beforehand.
	 */
	sa1111_configure_smc(g_sa1111, 1,
			     FExtr(MDCNFG, MDCNFG_SA1110_DRAC0),
			     FExtr(MDCNFG, MDCNFG_SA1110_TDL0));

	/*
	 * We only need to turn on DCLK whenever we want to use the
	 * DMA.  It can otherwise be held firmly in the off position.
	 */
	val = sa1111_readl(g_sa1111->base + SA1111_SKPCR);
	sa1111_writel(val | SKPCR_DCLKEN, g_sa1111->base + SA1111_SKPCR);

	/*
	 * Enable the SA1110 memory bus request and grant signals.
	 */
	sa1110_mb_enable();

	/*
	 * Initialise SA1111 IRQs
	 */
	int_dev.irq[0] = irq;
	sa1111_init_irq(&int_dev);

	return 0;
}

struct sa1111_save_data {
	unsigned int	skcr;
	unsigned int	skpcr;
	unsigned int	skcdr;
	unsigned char	skaud;
	unsigned char	skpwm0;
	unsigned char	skpwm1;

	/*
	 * Interrupt controller
	 */
	unsigned int	intpol0;
	unsigned int	intpol1;
	unsigned int	inten0;
	unsigned int	inten1;
	unsigned int	wakepol0;
	unsigned int	wakepol1;
	unsigned int	wakeen0;
	unsigned int	wakeen1;
};

static int sa1111_suspend(struct device *dev, u32 state, u32 level)
{
	struct sa1111 *sachip = dev->driver_data;
	unsigned long flags;
	char *base;

	/*
	 * Save state.
	 */
	if (level == SUSPEND_SAVE_STATE ||
	    level == SUSPEND_DISABLE ||
	    level == SUSPEND_POWER_DOWN) {
		struct sa1111_save_data *save;

		if (!dev->saved_state)
			dev->saved_state = kmalloc(sizeof(struct sa1111_save_data), GFP_KERNEL);
		if (!dev->saved_state)
			return -ENOMEM;

		save = (struct sa1111_save_data *)dev->saved_state;

		spin_lock_irqsave(&sachip->lock, flags);
		base = sachip->base;
		save->skcr     = sa1111_readl(base + SA1111_SKCR);
		save->skpcr    = sa1111_readl(base + SA1111_SKPCR);
		save->skcdr    = sa1111_readl(base + SA1111_SKCDR);
		save->skaud    = sa1111_readl(base + SA1111_SKAUD);
		save->skpwm0   = sa1111_readl(base + SA1111_SKPWM0);
		save->skpwm1   = sa1111_readl(base + SA1111_SKPWM1);

		base = sachip->base + SA1111_INTC;
		save->intpol0  = sa1111_readl(base + SA1111_INTPOL0);
		save->intpol1  = sa1111_readl(base + SA1111_INTPOL1);
		save->inten0   = sa1111_readl(base + SA1111_INTEN0);
		save->inten1   = sa1111_readl(base + SA1111_INTEN1);
		save->wakepol0 = sa1111_readl(base + SA1111_WAKEPOL0);
		save->wakepol1 = sa1111_readl(base + SA1111_WAKEPOL1);
		save->wakeen0  = sa1111_readl(base + SA1111_WAKEEN0);
		save->wakeen1  = sa1111_readl(base + SA1111_WAKEEN1);
		spin_unlock_irqrestore(&sachip->lock, flags);
	}

	/*
	 * Disable.
	 */
	if (level == SUSPEND_DISABLE && state == 4) {
		unsigned int val;

		spin_lock_irqsave(&sachip->lock, flags);
		base = sachip->base;

		sa1111_writel(0, base + SA1111_SKPWM0);
		sa1111_writel(0, base + SA1111_SKPWM1);
		val = sa1111_readl(base + SA1111_SKCR);
		sa1111_writel(val | SKCR_SLEEP, base + SA1111_SKCR);

		spin_unlock_irqrestore(&sachip->lock, flags);
	}

	return 0;
}

/*
 *	sa1111_resume - Restore the SA1111 device state.
 *	@dev: device to restore
 *	@level: resume level
 *
 *	Restore the general state of the SA1111; clock control and
 *	interrupt controller.  Other parts of the SA1111 must be
 *	restored by their respective drivers, and must be called
 *	via LDM after this function.
 */
static int sa1111_resume(struct device *dev, u32 level)
{
	struct sa1111 *sachip = dev->driver_data;
	struct sa1111_save_data *save;
	unsigned long flags, id;
	char *base;

	if (level != RESUME_RESTORE_STATE && level != RESUME_ENABLE)
		return 0;

	save = (struct sa1111_save_data *)dev->saved_state;
	if (!save)
		return 0;

	dev->saved_state = NULL;

	/*
	 * Ensure that the SA1111 is still here.
	 */
	id = sa1111_readl(sachip->base + SA1111_SKID);
	if ((id & SKID_ID_MASK) != SKID_SA1111_ID) {
		__sa1111_remove(sachip);
		kfree(save);
		return 0;
	}

	spin_lock_irqsave(&sachip->lock, flags);
	sa1111_wake(sachip);

	base = sachip->base;
	sa1111_writel(save->skcr,     base + SA1111_SKCR);
	sa1111_writel(save->skpcr,    base + SA1111_SKPCR);
	sa1111_writel(save->skcdr,    base + SA1111_SKCDR);
	sa1111_writel(save->skaud,    base + SA1111_SKAUD);
	sa1111_writel(save->skpwm0,   base + SA1111_SKPWM0);
	sa1111_writel(save->skpwm1,   base + SA1111_SKPWM1);

	base = sachip->base + SA1111_INTC;
	sa1111_writel(save->intpol0,  base + SA1111_INTPOL0);
	sa1111_writel(save->intpol1,  base + SA1111_INTPOL1);
	sa1111_writel(save->inten0,   base + SA1111_INTEN0);
	sa1111_writel(save->inten1,   base + SA1111_INTEN1);
	sa1111_writel(save->wakepol0, base + SA1111_WAKEPOL0);
	sa1111_writel(save->wakepol1, base + SA1111_WAKEPOL1);
	sa1111_writel(save->wakeen0,  base + SA1111_WAKEEN0);
	sa1111_writel(save->wakeen1,  base + SA1111_WAKEEN1);
	spin_unlock_irqrestore(&sachip->lock, flags);

	kfree(save);

	return 0;
}

static struct device_driver sa1111_device_driver = {
	.suspend	= sa1111_suspend,
	.resume		= sa1111_resume,
};

/*
 *	Get the parent device driver (us) structure
 *	from a child function device
 */
static inline struct sa1111 *sa1111_chip_driver(struct sa1111_dev *sadev)
{
	return (struct sa1111 *)sadev->dev.parent->driver_data;
}

/*
 * The bits in the opdiv field are non-linear.
 */
static unsigned char opdiv_table[] = { 1, 4, 2, 8 };

static unsigned int __sa1111_pll_clock(struct sa1111 *sachip)
{
	unsigned int skcdr, fbdiv, ipdiv, opdiv;

	skcdr = sa1111_readl(sachip->base + SA1111_SKCDR);

	fbdiv = (skcdr & 0x007f) + 2;
	ipdiv = ((skcdr & 0x0f80) >> 7) + 2;
	opdiv = opdiv_table[(skcdr & 0x3000) >> 12];

	return 3686400 * fbdiv / (ipdiv * opdiv);
}

/**
 *	sa1111_pll_clock - return the current PLL clock frequency.
 *	@sadev: SA1111 function block
 *
 *	BUG: we should look at SKCR.  We also blindly believe that
 *	the chip is being fed with the 3.6864MHz clock.
 *
 *	Returns the PLL clock in Hz.
 */
unsigned int sa1111_pll_clock(struct sa1111_dev *sadev)
{
	struct sa1111 *sachip = sa1111_chip_driver(sadev);

	return __sa1111_pll_clock(sachip);
}

/**
 *	sa1111_select_audio_mode - select I2S or AC link mode
 *	@sadev: SA1111 function block
 *	@mode: One of %SA1111_AUDIO_ACLINK or %SA1111_AUDIO_I2S
 *
 *	Frob the SKCR to select AC Link mode or I2S mode for
 *	the audio block.
 */
void sa1111_select_audio_mode(struct sa1111_dev *sadev, int mode)
{
	struct sa1111 *sachip = sa1111_chip_driver(sadev);
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&sachip->lock, flags);

	val = sa1111_readl(sachip->base + SA1111_SKCR);
	if (mode == SA1111_AUDIO_I2S) {
		val &= ~SKCR_SELAC;
	} else {
		val |= SKCR_SELAC;
	}
	sa1111_writel(val, sachip->base + SA1111_SKCR);

	spin_unlock_irqrestore(&sachip->lock, flags);
}

/**
 *	sa1111_set_audio_rate - set the audio sample rate
 *	@sadev: SA1111 SAC function block
 *	@rate: sample rate to select
 */
int sa1111_set_audio_rate(struct sa1111_dev *sadev, int rate)
{
	struct sa1111 *sachip = sa1111_chip_driver(sadev);
	unsigned int div;

	if (sadev->devid != SA1111_DEVID_SAC)
		return -EINVAL;

	div = (__sa1111_pll_clock(sachip) / 256 + rate / 2) / rate;
	if (div == 0)
		div = 1;
	if (div > 128)
		div = 128;

	sa1111_writel(div - 1, sachip->base + SA1111_SKAUD);

	return 0;
}

/**
 *	sa1111_get_audio_rate - get the audio sample rate
 *	@sadev: SA1111 SAC function block device
 */
int sa1111_get_audio_rate(struct sa1111_dev *sadev)
{
	struct sa1111 *sachip = sa1111_chip_driver(sadev);
	unsigned long div;

	if (sadev->devid != SA1111_DEVID_SAC)
		return -EINVAL;

	div = sa1111_readl(sachip->base + SA1111_SKAUD) + 1;

	return __sa1111_pll_clock(sachip) / (256 * div);
}

/*
 * Individual device operations.
 */

/**
 *	sa1111_enable_device - enable an on-chip SA1111 function block
 *	@sadev: SA1111 function block device to enable
 */
void sa1111_enable_device(struct sa1111_dev *sadev)
{
	struct sa1111 *sachip = sa1111_chip_driver(sadev);
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&sachip->lock, flags);
	val = sa1111_readl(sachip->base + SA1111_SKPCR);
	sa1111_writel(val | sadev->skpcr_mask, sachip->base + SA1111_SKPCR);
	spin_unlock_irqrestore(&sachip->lock, flags);
}

/**
 *	sa1111_disable_device - disable an on-chip SA1111 function block
 *	@sadev: SA1111 function block device to disable
 */
void sa1111_disable_device(struct sa1111_dev *sadev)
{
	struct sa1111 *sachip = sa1111_chip_driver(sadev);
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&sachip->lock, flags);
	val = sa1111_readl(sachip->base + SA1111_SKPCR);
	sa1111_writel(val & ~sadev->skpcr_mask, sachip->base + SA1111_SKPCR);
	spin_unlock_irqrestore(&sachip->lock, flags);
}

/*
 *	SA1111 "Register Access Bus."
 *
 *	We model this as a regular bus type, and hang devices directly
 *	off this.
 */
static int sa1111_match(struct device *_dev, struct device_driver *_drv)
{
	struct sa1111_dev *dev = SA1111_DEV(_dev);
	struct sa1111_driver *drv = SA1111_DRV(_drv);

	return dev->devid == drv->devid;
}

struct bus_type sa1111_bus_type = {
	.name	= "RAB",
	.match	= sa1111_match,
};

static int sa1111_rab_bus_init(void)
{
	return bus_register(&sa1111_bus_type);
}

postcore_initcall(sa1111_rab_bus_init);

EXPORT_SYMBOL(sa1111_check_dma_bug);
EXPORT_SYMBOL(sa1111_select_audio_mode);
EXPORT_SYMBOL(sa1111_set_audio_rate);
EXPORT_SYMBOL(sa1111_get_audio_rate);
EXPORT_SYMBOL(sa1111_enable_device);
EXPORT_SYMBOL(sa1111_disable_device);
EXPORT_SYMBOL(sa1111_pll_clock);
EXPORT_SYMBOL(sa1111_bus_type);
