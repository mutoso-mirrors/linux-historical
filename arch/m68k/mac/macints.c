/*
 *	Macintosh interrupts
 *
 * General design:
 * In contrary to the Amiga and Atari platforms, the Mac hardware seems to 
 * exclusively use the autovector interrupts (the 'generic level0-level7' 
 * interrupts with exception vectors 0x19-0x1f). The following interrupt levels
 * are used:
 *	1	- VIA1
 *		  - slot 0: one second interrupt (CA2)
 *		  - slot 1: VBlank (CA1)
 *		  - slot 2: ADB data ready (SR full)
 *		  - slot 3: ADB data  (CB2)
 *		  - slot 4: ADB clock (CB1)
 *		  - slot 5: timer 2
 *		  - slot 6: timer 1
 *		  - slot 7: status of IRQ; signals 'any enabled int.'
 *
 *	2	- VIA2 or RBV
 *		  - slot 0: SCSI DRQ (CA2)
 *		  - slot 1: NUBUS IRQ (CA1) need to read port A to find which
 *		  - slot 2: /EXP IRQ (only on IIci)
 *		  - slot 3: SCSI IRQ (CB2)
 *		  - slot 4: ASC IRQ (CB1)
 *		  - slot 5: timer 2 (not on IIci)
 *		  - slot 6: timer 1 (not on IIci)
 *		  - slot 7: status of IRQ; signals 'any enabled int.'
 *
 *	2	- OSS (IIfx only?)
 *		  - slot 0: SCSI interrupt
 *		  - slot 1: Sound interrupt
 *
 * Levels 3-6 vary by machine type. For VIA or RBV Macintohes:
 *
 *	3	- unused (?)
 *
 *	4	- SCC (slot number determined by reading RR3 on the SSC itself)
 *		  - slot 1: SCC channel A
 *		  - slot 2: SCC channel B
 *
 *	5	- unused (?)
 *		  [serial errors or special conditions seem to raise level 6
 *		  interrupts on some models (LC4xx?)]
 *
 *	6	- off switch (?)
 *
 * For OSS Macintoshes (IIfx only at this point):
 *
 *	3	- Nubus interrupt
 *		  - slot 0: Slot $9
 *		  - slot 1: Slot $A
 *		  - slot 2: Slot $B
 *		  - slot 3: Slot $C
 *		  - slot 4: Slot $D
 *		  - slot 5: Slot $E
 *
 *	4	- SCC IOP
 *		  - slot 1: SCC channel A
 *		  - slot 2: SCC channel B
 *
 *	5	- ISM IOP (ADB?)
 *
 *	6	- unused
 *
 * For PSC Macintoshes (660AV, 840AV):
 *
 *	3	- PSC level 3
 *		  - slot 0: MACE
 *
 *	4	- PSC level 4
 *		  - slot 1: SCC channel A interrupt
 *		  - slot 2: SCC channel B interrupt
 *		  - slot 3: MACE DMA
 *
 *	5	- PSC level 5
 *
 *	6	- PSC level 6
 *
 * Finally we have good 'ole level 7, the non-maskable interrupt:
 *
 *	7	- NMI (programmer's switch on the back of some Macs)
 *		  Also RAM parity error on models which support it (IIc, IIfx?)
 *
 * The current interrupt logic looks something like this:
 *
 * - We install dispatchers for the autovector interrupts (1-7). These
 *   dispatchers are responsible for querying the hardware (the
 *   VIA/RBV/OSS/PSC chips) to determine the actual interrupt source. Using
 *   this information a machspec interrupt number is generated by placing the
 *   index of the interrupt hardware into the low three bits and the original
 *   autovector interrupt number in the upper 5 bits. The handlers for the
 *   resulting machspec interrupt are then called.
 *
 * - Nubus is a special case because its interrupts are hidden behind two
 *   layers of hardware. Nubus interrupts come in as index 1 on VIA #2,
 *   which translates to IRQ number 17. In this spot we install _another_
 *   dispatcher. This dispatcher finds the interrupting slot number (9-F) and
 *   then forms a new machspec interrupt number as above with the slot number
 *   minus 9 in the low three bits and the pseudo-level 7 in the upper five
 *   bits.  The handlers for this new machspec interrupt number are then
 *   called. This puts Nubus interrupts into the range 56-62.
 *
 * - The Baboon interrupts (used on some PowerBooks) are an even more special
 *   case. They're hidden behind the Nubus slot $C interrupt thus adding a
 *   third layer of indirection. Why oh why did the Apple engineers do that?
 *
 * - We support "fast" and "slow" handlers, just like the Amiga port. The
 *   fast handlers are called first and with all interrupts disabled. They
 *   are expected to execute quickly (hence the name). The slow handlers are
 *   called last with interrupts enabled and the interrupt level restored.
 *   They must therefore be reentrant.
 *
 *   TODO:
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h> /* for intr_count */
#include <linux/delay.h>
#include <linux/seq_file.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/bootinfo.h>
#include <asm/machw.h>
#include <asm/macintosh.h>
#include <asm/mac_via.h>
#include <asm/mac_psc.h>
#include <asm/hwtest.h>
#include <asm/errno.h>

#include <asm/macints.h>

#define DEBUG_SPURIOUS
#define SHUTUP_SONIC

/*
 * The mac_irq_list array is an array of linked lists of irq_node_t nodes.
 * Each node contains one handler to be called whenever the interrupt
 * occurs, with fast handlers listed before slow handlers.
 */

irq_node_t *mac_irq_list[NUM_MAC_SOURCES];

/* SCC interrupt mask */

static int scc_mask;

/*
 * VIA/RBV hooks
 */

extern void via_init(void);
extern void via_register_interrupts(void);
extern void via_irq_enable(int);
extern void via_irq_disable(int);
extern void via_irq_clear(int);
extern int  via_irq_pending(int);

/*
 * OSS hooks
 */

extern int oss_present;

extern void oss_init(void);
extern void oss_register_interrupts(void);
extern void oss_irq_enable(int);
extern void oss_irq_disable(int);
extern void oss_irq_clear(int);
extern int  oss_irq_pending(int);

/*
 * PSC hooks
 */

extern int psc_present;

extern void psc_init(void);
extern void psc_register_interrupts(void);
extern void psc_irq_enable(int);
extern void psc_irq_disable(int);
extern void psc_irq_clear(int);
extern int  psc_irq_pending(int);

/*
 * IOP hooks
 */

extern void iop_register_interrupts(void);

/*
 * Baboon hooks
 */

extern int baboon_present;

extern void baboon_init(void);
extern void baboon_register_interrupts(void);
extern void baboon_irq_enable(int);
extern void baboon_irq_disable(int);
extern void baboon_irq_clear(int);
extern int  baboon_irq_pending(int);

/*
 * SCC interrupt routines
 */

static void scc_irq_enable(int);
static void scc_irq_disable(int);

/*
 * console_loglevel determines NMI handler function
 */

extern void mac_bang(int, void *, struct pt_regs *);

void mac_nmi_handler(int, void *, struct pt_regs *);
void mac_debug_handler(int, void *, struct pt_regs *);

/* #define DEBUG_MACINTS */

void mac_init_IRQ(void)
{
        int i;

#ifdef DEBUG_MACINTS
	printk("mac_init_IRQ(): Setting things up...\n");
#endif
	/* Initialize the IRQ handler lists. Initially each list is empty, */

	for (i = 0; i < NUM_MAC_SOURCES; i++) {
		mac_irq_list[i] = NULL;
	}

	scc_mask = 0;

	/* Make sure the SONIC interrupt is cleared or things get ugly */
#ifdef SHUTUP_SONIC
	printk("Killing onboard sonic... ");
	/* This address should hopefully be mapped already */
	if (hwreg_present((void*)(0x50f0a000))) {
		*(long *)(0x50f0a014) = 0x7fffL;
		*(long *)(0x50f0a010) = 0L;
	}
	printk("Done.\n");
#endif /* SHUTUP_SONIC */

	/* 
	 * Now register the handlers for the master IRQ handlers
	 * at levels 1-7. Most of the work is done elsewhere.
	 */

	if (oss_present) {
		oss_register_interrupts();
	} else {
		via_register_interrupts();
	}
	if (psc_present) psc_register_interrupts();
	if (baboon_present) baboon_register_interrupts();
	iop_register_interrupts();
	sys_request_irq(7, mac_nmi_handler, IRQ_FLG_LOCK, "NMI", mac_nmi_handler);
#ifdef DEBUG_MACINTS
	printk("mac_init_IRQ(): Done!\n");
#endif
}

/*
 * Routines to work with irq_node_t's on linked lists lifted from
 * the Amiga code written by Roman Zippel.
 */

static inline void mac_insert_irq(irq_node_t **list, irq_node_t *node)
{
	unsigned long cpu_flags;
	irq_node_t *cur;

	if (!node->dev_id)
		printk("%s: Warning: dev_id of %s is zero\n",
		       __FUNCTION__, node->devname);

	save_flags(cpu_flags);
	cli();

	cur = *list;

	if (node->flags & IRQ_FLG_FAST) {
		node->flags &= ~IRQ_FLG_SLOW;
		while (cur && cur->flags & IRQ_FLG_FAST) {
			list = &cur->next;
			cur = cur->next;
		}
	} else if (node->flags & IRQ_FLG_SLOW) {
		while (cur) {
			list = &cur->next;
			cur = cur->next;
		}
	} else {
		while (cur && !(cur->flags & IRQ_FLG_SLOW)) {
			list = &cur->next;
			cur = cur->next;
		}
	}

	node->next = cur;
	*list = node;

	restore_flags(cpu_flags);
}

static inline void mac_delete_irq(irq_node_t **list, void *dev_id)
{
	unsigned long cpu_flags;
	irq_node_t *node;

	save_flags(cpu_flags);
	cli();

	for (node = *list; node; list = &node->next, node = *list) {
		if (node->dev_id == dev_id) {
			*list = node->next;
			/* Mark it as free. */
			node->handler = NULL;
			restore_flags(cpu_flags);
			return;
		}
	}
	restore_flags(cpu_flags);
	printk ("%s: tried to remove invalid irq\n", __FUNCTION__);
}

/*
 * Call all the handlers for a given interrupt. Fast handlers are called
 * first followed by slow handlers.
 *
 * This code taken from the original Amiga code written by Roman Zippel.
 */

void mac_do_irq_list(int irq, struct pt_regs *fp)
{
	irq_node_t *node, *slow_nodes;
	unsigned long cpu_flags;

	kstat.irqs[0][irq]++;

#ifdef DEBUG_SPURIOUS
	if (!mac_irq_list[irq] && (console_loglevel > 7)) {
		printk("mac_do_irq_list: spurious interrupt %d!\n", irq);
		return;
	}
#endif

	/* serve first fast and normal handlers */
	for (node = mac_irq_list[irq];
	     node && (!(node->flags & IRQ_FLG_SLOW));
	     node = node->next)
		node->handler(irq, node->dev_id, fp);
	if (!node) return;
	save_flags(cpu_flags);
	restore_flags((cpu_flags & ~0x0700) | (fp->sr & 0x0700));
	/* if slow handlers exists, serve them now */
	slow_nodes = node;
	for (; node; node = node->next) {
		node->handler(irq, node->dev_id, fp);
	}
}

/*
 *  mac_enable_irq - enable an interrupt source
 * mac_disable_irq - disable an interrupt source
 *   mac_clear_irq - clears a pending interrupt
 * mac_pending_irq - Returns the pending status of an IRQ (nonzero = pending)
 *
 * These routines are just dispatchers to the VIA/OSS/PSC routines.
 */

void mac_enable_irq (unsigned int irq)
{
	int irq_src	= IRQ_SRC(irq);

	switch(irq_src) {
		case 1: via_irq_enable(irq);
			break;
		case 2:
		case 7: if (oss_present) {
				oss_irq_enable(irq);
			} else {
				via_irq_enable(irq);
			}
			break;
		case 3:
		case 4:
		case 5:
		case 6: if (psc_present) {
				psc_irq_enable(irq);
			} else if (oss_present) {
				oss_irq_enable(irq);
			} else if (irq_src == 4) {
				scc_irq_enable(irq);
			}
			break;
		case 8: if (baboon_present) {
				baboon_irq_enable(irq);
			}
			break;
	}
}

void mac_disable_irq (unsigned int irq)
{
	int irq_src	= IRQ_SRC(irq);

	switch(irq_src) {
		case 1: via_irq_disable(irq);
			break;
		case 2:
		case 7: if (oss_present) {
				oss_irq_disable(irq);
			} else {
				via_irq_disable(irq);
			}
			break;
		case 3:
		case 4:
		case 5:
		case 6: if (psc_present) {
				psc_irq_disable(irq);
			} else if (oss_present) {
				oss_irq_disable(irq);
			} else if (irq_src == 4) {
				scc_irq_disable(irq);
			}
			break;
		case 8: if (baboon_present) {
				baboon_irq_disable(irq);
			}
			break;
	}
}

void mac_clear_irq( unsigned int irq )
{
	switch(IRQ_SRC(irq)) {
		case 1: via_irq_clear(irq);
			break;
		case 2:
		case 7: if (oss_present) {
				oss_irq_clear(irq);
			} else {
				via_irq_clear(irq);
			}
			break;
		case 3:
		case 4:
		case 5:
		case 6: if (psc_present) {
				psc_irq_clear(irq);
			} else if (oss_present) {
				oss_irq_clear(irq);
			}
			break;
		case 8: if (baboon_present) {
				baboon_irq_clear(irq);
			}
			break;
	}
}

int mac_irq_pending( unsigned int irq )
{
	switch(IRQ_SRC(irq)) {
		case 1: return via_irq_pending(irq);
		case 2:
		case 7: if (oss_present) {
				return oss_irq_pending(irq);
			} else {
				return via_irq_pending(irq);
			}
		case 3:
		case 4:
		case 5:
		case 6: if (psc_present) {
				return psc_irq_pending(irq);
			} else if (oss_present) {
				return oss_irq_pending(irq);
			}
	}
	return 0;
}

/*
 * Add an interrupt service routine to an interrupt source.
 * Returns 0 on success.
 *
 * FIXME: You can register interrupts on nonexistant source (ie PSC4 on a
 *        non-PSC machine). We should return -EINVAL in those cases.
 */
 
int mac_request_irq(unsigned int irq,
		    void (*handler)(int, void *, struct pt_regs *),
		    unsigned long flags, const char *devname, void *dev_id)
{
	irq_node_t *node;

#ifdef DEBUG_MACINTS
	printk ("%s: irq %d requested for %s\n", __FUNCTION__, irq, devname);
#endif

	if (irq < VIA1_SOURCE_BASE) {
		return sys_request_irq(irq, handler, flags, devname, dev_id);
	}

	if (irq >= NUM_MAC_SOURCES) {
		printk ("%s: unknown irq %d requested by %s\n",
		        __FUNCTION__, irq, devname);
	}

	/* Get a node and stick it onto the right list */

	if (!(node = new_irq_node())) return -ENOMEM;

	node->handler	= handler;
	node->flags	= flags;
	node->dev_id	= dev_id;
	node->devname	= devname;
	node->next	= NULL;
	mac_insert_irq(&mac_irq_list[irq], node);

	/* Now enable the IRQ source */

	mac_enable_irq(irq);

	return 0;
}
                            
/*
 * Removes an interrupt service routine from an interrupt source.
 */

void mac_free_irq(unsigned int irq, void *dev_id)
{
#ifdef DEBUG_MACINTS
	printk ("%s: irq %d freed by %p\n", __FUNCTION__, irq, dev_id);
#endif

	if (irq < VIA1_SOURCE_BASE) {
		return sys_free_irq(irq, dev_id);
	}

	if (irq >= NUM_MAC_SOURCES) {
		printk ("%s: unknown irq %d freed\n",
		        __FUNCTION__, irq);
		return;
	}

	mac_delete_irq(&mac_irq_list[irq], dev_id);

	/* If the list for this interrupt is */
	/* empty then disable the source.    */

	if (!mac_irq_list[irq]) {
		mac_disable_irq(irq);
	}
}

/*
 * Generate a pretty listing for /proc/interrupts
 *
 * By the time we're called the autovector interrupt list has already been
 * generated, so we just need to do the machspec interrupts.
 *
 * 990506 (jmt) - rewritten to handle chained machspec interrupt handlers.
 *                Also removed display of num_spurious it is already
 *		  displayed for us as autovector irq 0.
 */

int show_mac_interrupts(struct seq_file *p, void *v)
{
	int i;
	irq_node_t *node;
	char *base;

	/* Don't do Nubus interrupts in this loop; we do them separately  */
	/* below so that we can print slot numbers instead of IRQ numbers */

	for (i = VIA1_SOURCE_BASE ; i < NUM_MAC_SOURCES ; ++i) {

		/* Nonexistant interrupt or nothing registered; skip it. */

		if ((node = mac_irq_list[i]) == NULL) continue;
		if (node->flags & IRQ_FLG_STD) continue;

		base = "";
		switch(IRQ_SRC(i)) {
			case 1: base = "via1";
				break;
			case 2: if (oss_present) {
					base = "oss";
				} else {
					base = "via2";
				}
				break;
			case 3:
			case 4:
			case 5:
			case 6: if (psc_present) {
					base = "psc";
				} else if (oss_present) {
					base = "oss";
				} else {
					if (IRQ_SRC(i) == 4) base = "scc";
				}
				break;
			case 7: base = "nbus";
				break;
			case 8: base = "bbn";
				break;
		}
		seq_printf(p, "%4s %2d: %10u ", base, i, kstat.irqs[0][i]);

		do {
			if (node->flags & IRQ_FLG_FAST) {
				seq_puts(p, "F ");
			} else if (node->flags & IRQ_FLG_SLOW) {
				seq_puts(p, "S ");
			} else {
				seq_puts(p, "  ");
			}
			seq_printf(p, "%s\n", node->devname);
			if ((node = node->next)) {
				seq_puts(p, "                    ");
			}
		} while(node);

	}
	return 0;
}

void mac_default_handler(int irq, void *dev_id, struct pt_regs *regs)
{
#ifdef DEBUG_SPURIOUS
	printk("Unexpected IRQ %d on device %p\n", irq, dev_id);
#endif
}

static int num_debug[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

void mac_debug_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	if (num_debug[irq] < 10) {
		printk("DEBUG: Unexpected IRQ %d\n", irq);
		num_debug[irq]++;
	}
}

static int in_nmi = 0;
static volatile int nmi_hold = 0;

void mac_nmi_handler(int irq, void *dev_id, struct pt_regs *fp)
{
	int i;
	/* 
	 * generate debug output on NMI switch if 'debug' kernel option given
	 * (only works with Penguin!)
	 */

	in_nmi++;
	for (i=0; i<100; i++)
		udelay(1000);

	if (in_nmi == 1) {
		nmi_hold = 1;
		printk("... pausing, press NMI to resume ...");
	} else {
		printk(" ok!\n");
		nmi_hold = 0;
	}

	barrier();

	while (nmi_hold == 1)
		udelay(1000);

	if ( console_loglevel >= 8 ) {
#if 0
		show_state();
		printk("PC: %08lx\nSR: %04x  SP: %p\n", fp->pc, fp->sr, fp);
		printk("d0: %08lx    d1: %08lx    d2: %08lx    d3: %08lx\n",
		       fp->d0, fp->d1, fp->d2, fp->d3);
		printk("d4: %08lx    d5: %08lx    a0: %08lx    a1: %08lx\n",
		       fp->d4, fp->d5, fp->a0, fp->a1);
	
		if (STACK_MAGIC != *(unsigned long *)current->kernel_stack_page)
			printk("Corrupted stack page\n");
		printk("Process %s (pid: %d, stackpage=%08lx)\n",
			current->comm, current->pid, current->kernel_stack_page);
		if (intr_count == 1)
			dump_stack((struct frame *)fp);
#else
		/* printk("NMI "); */
#endif
	}
	in_nmi--;
}

/*
 * Simple routines for masking and unmasking
 * SCC interrupts in cases where this can't be
 * done in hardware (only the PSC can do that.)
 */

static void scc_irq_enable(int irq) {
	int irq_idx     = IRQ_IDX(irq);

	scc_mask |= (1 << irq_idx);
}

static void scc_irq_disable(int irq) {
	int irq_idx     = IRQ_IDX(irq);

	scc_mask &= ~(1 << irq_idx);
}

/*
 * SCC master interrupt handler. We have to do a bit of magic here
 * to figure out what channel gave us the interrupt; putting this
 * here is cleaner than hacking it into drivers/char/macserial.c.
 */

void mac_scc_dispatch(int irq, void *dev_id, struct pt_regs *regs)
{
	volatile unsigned char *scc = (unsigned char *) mac_bi_data.sccbase + 2;
	unsigned char reg;
	unsigned long cpu_flags;

	/* Read RR3 from the chip. Always do this on channel A */
	/* This must be an atomic operation so disable irqs.   */

	save_flags(cpu_flags); cli();
	*scc = 3;
	reg = *scc;
	restore_flags(cpu_flags);

	/* Now dispatch. Bits 0-2 are for channel B and */
	/* bits 3-5 are for channel A. We can safely    */
	/* ignore the remaining bits here.              */
	/*                                              */
	/* Note that we're ignoring scc_mask for now.   */
	/* If we actually mask the ints then we tend to */
	/* get hammered by very persistant SCC irqs,    */
	/* and since they're autovector interrupts they */
	/* pretty much kill the system.                 */

	if (reg & 0x38) mac_do_irq_list(IRQ_SCCA, regs);
	if (reg & 0x07) mac_do_irq_list(IRQ_SCCB, regs);
}
