/*
 * arch/ppc/syslib/gt64260_pic.c
 *
 * Interrupt controller support for Galileo's GT64260.
 *
 * Author: Chris Zankel <chris@mvista.com>
 * Modified by: Mark A. Greer <mgreer@mvista.com>
 *
 * Based on sources from Rabeeh Khoury / Galileo Technology
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * This file contains the specific functions to support the GT64260
 * interrupt controller.
 *
 * The GT64260 has two main interrupt registers (high and low) that
 * summarizes the interrupts generated by the units of the GT64260.
 * Each bit is assigned to an interrupt number, where the low register
 * are assigned from IRQ0 to IRQ31 and the high cause register
 * from IRQ32 to IRQ63
 * The GPP (General Purpose Port) interrupts are assigned from IRQ64 (GPP0)
 * to IRQ95 (GPP31).
 * get_irq() returns the lowest interrupt number that is currently asserted.
 *
 * Note:
 *  - This driver does not initialize the GPP when used as an interrupt
 *    input.
 */

#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/stddef.h>
#include <linux/delay.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/gt64260.h>


/* ========================== forward declaration ========================== */

static void gt64260_unmask_irq(unsigned int);
static void gt64260_mask_irq(unsigned int);

/* ========================== local declarations =========================== */

struct hw_interrupt_type gt64260_pic = {
	" GT64260_PIC ",		/* typename */
	NULL,				/* startup */
	NULL,				/* shutdown */
	gt64260_unmask_irq,		/* enable */
	gt64260_mask_irq,		/* disable */
	gt64260_mask_irq,		/* ack */
	NULL,				/* end */
	NULL				/* set_affinity */
};

u32 gt64260_irq_base = 0;      /* GT64260 handles the next 96 IRQs from here */

/* gt64260_init_irq()
 *
 *  This function initializes the interrupt controller. It assigns
 *  all interrupts from IRQ0 to IRQ95 to the gt64260 interrupt controller.
 *
 * Input Variable(s):
 *  None.
 *
 * Outpu. Variable(s):
 *  None.
 *
 * Returns:
 *  void
 *
 * Note:
 *  We register all GPP inputs as interrupt source, but disable them.
 */

__init void
gt64260_init_irq(void)
{
	int i;

	if ( ppc_md.progress ) ppc_md.progress("gt64260_init_irq: enter", 0x0);

	ppc_cached_irq_mask[0] = 0;
	ppc_cached_irq_mask[1] = 0x0f000000; /* Enable GPP intrs */
	ppc_cached_irq_mask[2] = 0;

	/* disable all interrupts and clear current interrupts */
	gt_write(GT64260_GPP_INTR_MASK, ppc_cached_irq_mask[2]);
	gt_write(GT64260_GPP_INTR_CAUSE,0);
	gt_write(GT64260_IC_CPU_INTR_MASK_LO, ppc_cached_irq_mask[0]);
	gt_write(GT64260_IC_CPU_INTR_MASK_HI, ppc_cached_irq_mask[1]);

	/* use the gt64260 for all (possible) interrupt sources */
	for( i = gt64260_irq_base;  i < (gt64260_irq_base + 96);  i++ )  {
		irq_desc[i].handler = &gt64260_pic;
	}

	if ( ppc_md.progress ) ppc_md.progress("gt64260_init_irq: exit", 0x0);
}


/* gt64260_get_irq()
 *
 *  This function returns the lowest interrupt number of all interrupts that
 *  are currently asserted.
 *
 * Input Variable(s):
 *  struct pt_regs*	not used
 *
 * Output Variable(s):
 *  None.
 *
 * Returns:
 *  int	<interrupt number> or -2 (bogus interrupt)
 *
 */
int
gt64260_get_irq(struct pt_regs *regs)
{
	int irq;
	int irq_gpp;

	irq = gt_read(GT64260_IC_MAIN_CAUSE_LO);
	irq = __ilog2((irq & 0x3dfffffe) & ppc_cached_irq_mask[0]);

	if (irq == -1) {
		irq = gt_read(GT64260_IC_MAIN_CAUSE_HI);
		irq = __ilog2((irq & 0x0f000db7) & ppc_cached_irq_mask[1]);

		if (irq == -1) {
			irq = -2;   /* bogus interrupt, should never happen */
		} else {
			if (irq >= 24) {
				irq_gpp = gt_read(GT64260_GPP_INTR_CAUSE);
				irq_gpp = __ilog2(irq_gpp &
						  ppc_cached_irq_mask[2]);

				if (irq_gpp == -1) {
					irq = -2;
				} else {
					irq = irq_gpp + 64;
					gt_write(GT64260_GPP_INTR_CAUSE, ~(1<<(irq-64)));
				}
			} else {
				irq += 32;
			}
		}
	}

	if( irq < 0 )  {
		return( irq );
	} else  {
		return( gt64260_irq_base + irq );
	}
}

/* gt64260_unmask_irq()
 *
 *  This function enables an interrupt.
 *
 * Input Variable(s):
 *  unsigned int	interrupt number (IRQ0...IRQ95).
 *
 * Output Variable(s):
 *  None.
 *
 * Returns:
 *  void
 */

static void
gt64260_unmask_irq(unsigned int irq)
{
	irq -= gt64260_irq_base;
	if (irq > 31) {
		if (irq > 63) {
			/* unmask GPP irq */
			gt_write(GT64260_GPP_INTR_MASK,
				     ppc_cached_irq_mask[2] |= (1<<(irq-64)));
		} else {
			/* mask high interrupt register */
			gt_write(GT64260_IC_CPU_INTR_MASK_HI,
				     ppc_cached_irq_mask[1] |= (1<<(irq-32)));
		}
	} else {
		/* mask low interrupt register */
		gt_write(GT64260_IC_CPU_INTR_MASK_LO,
			     ppc_cached_irq_mask[0] |= (1<<irq));
	}
}


/* gt64260_mask_irq()
 *
 *  This funktion disables the requested interrupt.
 *
 * Input Variable(s):
 *  unsigned int	interrupt number (IRQ0...IRQ95).
 *
 * Output Variable(s):
 *  None.
 *
 * Returns:
 *  void
 */

static void
gt64260_mask_irq(unsigned int irq)
{
	irq -= gt64260_irq_base;
	if (irq > 31) {
		if (irq > 63) {
			/* mask GPP irq */
			gt_write(GT64260_GPP_INTR_MASK,
				     ppc_cached_irq_mask[2] &= ~(1<<(irq-64)));
		} else {
			/* mask high interrupt register */
			gt_write(GT64260_IC_CPU_INTR_MASK_HI,
				     ppc_cached_irq_mask[1] &= ~(1<<(irq-32)));
		}
	} else {
		/* mask low interrupt register */
		gt_write(GT64260_IC_CPU_INTR_MASK_LO,
			     ppc_cached_irq_mask[0] &= ~(1<<irq));
	}

	if (irq == 36) { /* Seems necessary for SDMA interrupts */
		udelay(1);
	}
}
