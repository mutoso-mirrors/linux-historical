/*
 *	linux/arch/i386/kernel/irq.c
 *
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

/*
 * IRQ's are in fact implemented a bit like signal handlers for the kernel.
 * Naturally it's not a 1:1 relation, but there are similarities.
 */

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/smp.h>
#include <linux/tasks.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/smp.h>
#include <asm/pgtable.h>
#include <asm/delay.h>

#include "irq.h"

unsigned int local_bh_count[NR_CPUS];
unsigned int local_irq_count[NR_CPUS];

atomic_t nmi_counter;

/*
 * About the IO-APIC, the architecture is 'merged' into our
 * current irq architecture, seemlessly. (i hope). It is only
 * visible through 8 more hardware interrupt lines, but otherwise
 * drivers are unaffected. The main code is believed to be
 * NR_IRQS-safe (nothing anymore thinks we have 16
 * irq lines only), but there might be some places left ...
 */

/*
 * This contains the irq mask for both 8259A irq controllers,
 * and on SMP the extended IO-APIC IRQs 16-23. The IO-APIC
 * uses this mask too, in probe_irq*().
 *
 * (0x0000ffff for NR_IRQS==16, 0x00ffffff for NR_IRQS=24)
 */
static unsigned int cached_irq_mask = (1<<NR_IRQS)-1;

#define cached_21	((cached_irq_mask | io_apic_irqs) & 0xff)
#define cached_A1	(((cached_irq_mask | io_apic_irqs) >> 8) & 0xff)

spinlock_t irq_controller_lock;

/*
 * Not all IRQs can be routed through the IO-APIC, eg. on certain (older)
 * boards the timer interrupt is not connected to any IO-APIC pin, it's
 * fed to the CPU IRQ line directly.
 *
 * Any '1' bit in this mask means the IRQ is routed through the IO-APIC.
 * this 'mixed mode' IRQ handling costs us one more branch in do_IRQ,
 * but we have _much_ higher compatibility and robustness this way.
 */

/*
 * Default to all normal IRQ's _not_ using the IO APIC.
 *
 * To get IO-APIC interrupts we turn some of them into IO-APIC
 * interrupts during boot.
 */
unsigned int io_apic_irqs = 0;

struct hw_interrupt_type {
	const char * typename;
	void (*handle)(unsigned int irq, int cpu, struct pt_regs * regs);
	void (*enable)(unsigned int irq);
	void (*disable)(unsigned int irq);
};


static void do_8259A_IRQ (unsigned int irq, int cpu, struct pt_regs * regs);
static void enable_8259A_irq (unsigned int irq);
static void disable_8259A_irq (unsigned int irq);

static struct hw_interrupt_type i8259A_irq_type = {
	"XT-PIC",
	do_8259A_IRQ,
	enable_8259A_irq,
	disable_8259A_irq
};


#ifdef __SMP__

/*
 * Level and edge triggered IO-APIC interrupts need different handling,
 * so we use two separate irq descriptors. edge triggered IRQs can be
 * handled with the level-triggered descriptor, but that one has slightly
 * more overhead. Level-triggered interrupts cannot be handled with the
 * edge-triggered handler, without risking IRQ storms and other ugly
 * races.
 */

static void do_edge_ioapic_IRQ (unsigned int irq, int cpu,
						 struct pt_regs * regs);
static void enable_edge_ioapic_irq (unsigned int irq);
static void disable_edge_ioapic_irq (unsigned int irq);

static struct hw_interrupt_type ioapic_edge_irq_type = {
	"IO-APIC-edge",
	do_edge_ioapic_IRQ,
	enable_edge_ioapic_irq,
	disable_edge_ioapic_irq
};

static void do_level_ioapic_IRQ (unsigned int irq, int cpu,
						 struct pt_regs * regs);
static void enable_level_ioapic_irq (unsigned int irq);
static void disable_level_ioapic_irq (unsigned int irq);

static struct hw_interrupt_type ioapic_level_irq_type = {
	"IO-APIC-level",
	do_level_ioapic_IRQ,
	enable_level_ioapic_irq,
	disable_level_ioapic_irq
};

#endif

/*
 * Status: reason for being disabled: somebody has
 * done a "disable_irq()" or we must not re-enter the
 * already executing irq..
 */
#define IRQ_INPROGRESS	1
#define IRQ_DISABLED	2

/*
 * This is the "IRQ descriptor", which contains various information
 * about the irq, including what kind of hardware handling it has,
 * whether it is disabled etc etc.
 *
 * Pad this out to 32 bytes for cache and indexing reasons.
 */
typedef struct {
	unsigned int status;			/* IRQ status - IRQ_INPROGRESS, IRQ_DISABLED */
	unsigned int events;			/* Do we have any pending events? */
	unsigned int ipi;			/* Have we sent off the pending IPI? */
	struct hw_interrupt_type *handler;	/* handle/enable/disable functions */
	struct irqaction *action;		/* IRQ action list */
	unsigned int unused[3];
} irq_desc_t;

irq_desc_t irq_desc[NR_IRQS] = {
	[0 ... 15] = { 0, 0, 0, &i8259A_irq_type, },	/* standard ISA IRQs */
#ifdef __SMP__
	[16 ... 23] = { 0, 0, 0, &ioapic_edge_irq_type, }, /* 'high' PCI IRQs */
#endif
};


/*
 * These have to be protected by the irq controller spinlock
 * before being called.
 */

static inline void mask_8259A(unsigned int irq)
{
	cached_irq_mask |= 1 << irq;
	if (irq & 8) {
		outb(cached_A1,0xA1);
	} else {
		outb(cached_21,0x21);
	}
}

static inline void unmask_8259A(unsigned int irq)
{
	cached_irq_mask &= ~(1 << irq);
	if (irq & 8) {
		outb(cached_A1,0xA1);
	} else {
		outb(cached_21,0x21);
	}
}

void set_8259A_irq_mask(unsigned int irq)
{
	/*
	 * (it might happen that we see IRQ>15 on a UP box, with SMP
	 * emulation)
	 */
	if (irq < 16) {
		if (irq & 8) {
			outb(cached_A1,0xA1);
		} else {
			outb(cached_21,0x21);
		}
	}
}

/*
 * This builds up the IRQ handler stubs using some ugly macros in irq.h
 *
 * These macros create the low-level assembly IRQ routines that save
 * register context and call do_IRQ(). do_IRQ() then does all the
 * operations that are needed to keep the AT (or SMP IOAPIC)
 * interrupt-controller happy.
 */


BUILD_COMMON_IRQ()
/*
 * ISA PIC or IO-APIC triggered (INTA-cycle or APIC) interrupts:
 */
BUILD_IRQ(0) BUILD_IRQ(1) BUILD_IRQ(2) BUILD_IRQ(3)
BUILD_IRQ(4) BUILD_IRQ(5) BUILD_IRQ(6) BUILD_IRQ(7)
BUILD_IRQ(8) BUILD_IRQ(9) BUILD_IRQ(10) BUILD_IRQ(11)
BUILD_IRQ(12) BUILD_IRQ(13) BUILD_IRQ(14) BUILD_IRQ(15)

#ifdef __SMP__

/*
 * The IO-APIC (present only in SMP boards) has 8 more hardware
 * interrupt pins, for all of them we define an IRQ vector:
 *
 * raw PCI interrupts 0-3, basically these are the ones used
 * heavily:
 */
BUILD_IRQ(16) BUILD_IRQ(17) BUILD_IRQ(18) BUILD_IRQ(19)

/*
 * [FIXME: anyone with 2 separate PCI buses and 2 IO-APICs, please
 *	   speak up if problems and request experimental patches.
 *         --mingo ]
 */

/*
 * MIRQ (motherboard IRQ) interrupts 0-1:
 */
BUILD_IRQ(20) BUILD_IRQ(21)

/*
 * 'nondefined general purpose interrupt'.
 */
BUILD_IRQ(22)
/*
 * optionally rerouted SMI interrupt:
 */
BUILD_IRQ(23)

/*
 * The following vectors are part of the Linux architecture, there
 * is no hardware IRQ pin equivalent for them, they are triggered
 * through the ICC by us (IPIs), via smp_message_pass():
 */
BUILD_SMP_INTERRUPT(reschedule_interrupt)
BUILD_SMP_INTERRUPT(invalidate_interrupt)
BUILD_SMP_INTERRUPT(stop_cpu_interrupt)
BUILD_SMP_INTERRUPT(mtrr_interrupt)
BUILD_SMP_INTERRUPT(spurious_interrupt)

/*
 * every pentium local APIC has two 'local interrupts', with a
 * soft-definable vector attached to both interrupts, one of
 * which is a timer interrupt, the other one is error counter
 * overflow. Linux uses the local APIC timer interrupt to get
 * a much simpler SMP time architecture:
 */
BUILD_SMP_TIMER_INTERRUPT(apic_timer_interrupt)

#endif

static void (*interrupt[NR_IRQS])(void) = {
	IRQ0_interrupt, IRQ1_interrupt, IRQ2_interrupt, IRQ3_interrupt,
	IRQ4_interrupt, IRQ5_interrupt, IRQ6_interrupt, IRQ7_interrupt,
	IRQ8_interrupt, IRQ9_interrupt, IRQ10_interrupt, IRQ11_interrupt,
	IRQ12_interrupt, IRQ13_interrupt, IRQ14_interrupt, IRQ15_interrupt
#ifdef __SMP__
	,IRQ16_interrupt, IRQ17_interrupt, IRQ18_interrupt, IRQ19_interrupt,
	IRQ20_interrupt, IRQ21_interrupt, IRQ22_interrupt, IRQ23_interrupt
#endif
};

/*
 * Initial irq handlers.
 */

static void no_action(int cpl, void *dev_id, struct pt_regs *regs) { }

/*
 * Note that on a 486, we don't want to do a SIGFPE on an irq13
 * as the irq is unreliable, and exception 16 works correctly
 * (ie as explained in the intel literature). On a 386, you
 * can't use exception 16 due to bad IBM design, so we have to
 * rely on the less exact irq13.
 *
 * Careful.. Not only is IRQ13 unreliable, but it is also
 * leads to races. IBM designers who came up with it should
 * be shot.
 */
 
static void math_error_irq(int cpl, void *dev_id, struct pt_regs *regs)
{
	outb(0,0xF0);
	if (ignore_irq13 || !boot_cpu_data.hard_math)
		return;
	math_error();
}

static struct irqaction irq13 = { math_error_irq, 0, 0, "fpu", NULL, NULL };

/*
 * IRQ2 is cascade interrupt to second interrupt controller
 */
static struct irqaction irq2  = { no_action, 0, 0, "cascade", NULL, NULL};

int get_irq_list(char *buf)
{
	int i, j;
	struct irqaction * action;
	char *p = buf;

	p += sprintf(p, "           ");
	for (j=0; j<smp_num_cpus; j++)
		p += sprintf(p, "CPU%d       ",j);
	*p++ = '\n';

	for (i = 0 ; i < NR_IRQS ; i++) {
		action = irq_desc[i].action;
		if (!action) 
			continue;
		p += sprintf(p, "%3d: ",i);
#ifndef __SMP__
		p += sprintf(p, "%10u ", kstat_irqs(i));
#else
		for (j=0; j<smp_num_cpus; j++)
			p += sprintf(p, "%10u ",
				kstat.irqs[cpu_logical_map(j)][i]);
#endif
		p += sprintf(p, " %14s", irq_desc[i].handler->typename);
		p += sprintf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next) {
			p += sprintf(p, ", %s", action->name);
		}
		*p++ = '\n';
	}
	p += sprintf(p, "NMI: %10u\n", atomic_read(&nmi_counter));
#ifdef __SMP__
	p += sprintf(p, "IPI: %10lu\n", ipi_count);
#endif		
	return p - buf;
}

/*
 * Global interrupt locks for SMP. Allow interrupts to come in on any
 * CPU, yet make cli/sti act globally to protect critical regions..
 */
#ifdef __SMP__
unsigned char global_irq_holder = NO_PROC_ID;
unsigned volatile int global_irq_lock;
atomic_t global_irq_count;

atomic_t global_bh_count;
atomic_t global_bh_lock;

/*
 * "global_cli()" is a special case, in that it can hold the
 * interrupts disabled for a longish time, and also because
 * we may be doing TLB invalidates when holding the global
 * IRQ lock for historical reasons. Thus we may need to check
 * SMP invalidate events specially by hand here (but not in
 * any normal spinlocks)
 */
static inline void check_smp_invalidate(int cpu)
{
	if (test_bit(cpu, &smp_invalidate_needed)) {
		clear_bit(cpu, &smp_invalidate_needed);
		local_flush_tlb();
	}
}

static void show(char * str)
{
	int i;
	unsigned long *stack;
	int cpu = smp_processor_id();

	printk("\n%s, CPU %d:\n", str, cpu);
	printk("irq:  %d [%d %d]\n",
		atomic_read(&global_irq_count), local_irq_count[0], local_irq_count[1]);
	printk("bh:   %d [%d %d]\n",
		atomic_read(&global_bh_count), local_bh_count[0], local_bh_count[1]);
	stack = (unsigned long *) &str;
	for (i = 40; i ; i--) {
		unsigned long x = *++stack;
		if (x > (unsigned long) &init_task_union && x < (unsigned long) &vsprintf) {
			printk("<[%08lx]> ", x);
		}
	}
}
	

#define MAXCOUNT 100000000

static inline void wait_on_bh(void)
{
	int count = MAXCOUNT;
	do {
		if (!--count) {
			show("wait_on_bh");
			count = ~0;
		}
		/* nothing .. wait for the other bh's to go away */
	} while (atomic_read(&global_bh_count) != 0);
}

/*
 * I had a lockup scenario where a tight loop doing
 * spin_unlock()/spin_lock() on CPU#1 was racing with
 * spin_lock() on CPU#0. CPU#0 should have noticed spin_unlock(), but
 * apparently the spin_unlock() information did not make it
 * through to CPU#0 ... nasty, is this by design, do we have to limit
 * 'memory update oscillation frequency' artificially like here?
 *
 * Such 'high frequency update' races can be avoided by careful design, but
 * some of our major constructs like spinlocks use similar techniques,
 * it would be nice to clarify this issue. Set this define to 0 if you
 * want to check wether your system freezes. I suspect the delay done
 * by SYNC_OTHER_CORES() is in correlation with 'snooping latency', but
 * i thought that such things are guaranteed by design, since we use
 * the 'LOCK' prefix.
 */
#define SUSPECTED_CPU_OR_CHIPSET_BUG_WORKAROUND 1

#if SUSPECTED_CPU_OR_CHIPSET_BUG_WORKAROUND
# define SYNC_OTHER_CORES(x) udelay(x+1)
#else
/*
 * We have to allow irqs to arrive between __sti and __cli
 */
# define SYNC_OTHER_CORES(x) __asm__ __volatile__ ("nop")
#endif

static inline void wait_on_irq(int cpu)
{
	int count = MAXCOUNT;

	for (;;) {

		/*
		 * Wait until all interrupts are gone. Wait
		 * for bottom half handlers unless we're
		 * already executing in one..
		 */
		if (!atomic_read(&global_irq_count)) {
			if (local_bh_count[cpu] || !atomic_read(&global_bh_count))
				break;
		}

		/* Duh, we have to loop. Release the lock to avoid deadlocks */
		clear_bit(0,&global_irq_lock);

		for (;;) {
			if (!--count) {
				show("wait_on_irq");
				count = ~0;
			}
			__sti();
			SYNC_OTHER_CORES(cpu);
			__cli();
			check_smp_invalidate(cpu);
			if (atomic_read(&global_irq_count))
				continue;
			if (global_irq_lock)
				continue;
			if (!local_bh_count[cpu] && atomic_read(&global_bh_count))
				continue;
			if (!test_and_set_bit(0,&global_irq_lock))
				break;
		}
	}
}

/*
 * This is called when we want to synchronize with
 * bottom half handlers. We need to wait until
 * no other CPU is executing any bottom half handler.
 *
 * Don't wait if we're already running in an interrupt
 * context or are inside a bh handler.
 */
void synchronize_bh(void)
{
	if (atomic_read(&global_bh_count) && !in_interrupt())
			wait_on_bh();
}

/*
 * This is called when we want to synchronize with
 * interrupts. We may for example tell a device to
 * stop sending interrupts: but to make sure there
 * are no interrupts that are executing on another
 * CPU we need to call this function.
 */
void synchronize_irq(void)
{
	if (atomic_read(&global_irq_count)) {
		/* Stupid approach */
		cli();
		sti();
	}
}

static inline void get_irqlock(int cpu)
{
	if (test_and_set_bit(0,&global_irq_lock)) {
		/* do we already hold the lock? */
		if ((unsigned char) cpu == global_irq_holder)
			return;
		/* Uhhuh.. Somebody else got it. Wait.. */
		do {
			do {
				check_smp_invalidate(cpu);
			} while (test_bit(0,&global_irq_lock));
		} while (test_and_set_bit(0,&global_irq_lock));		
	}
	/* 
	 * We also to make sure that nobody else is running
	 * in an interrupt context. 
	 */
	wait_on_irq(cpu);

	/*
	 * Ok, finally..
	 */
	global_irq_holder = cpu;
}

#define EFLAGS_IF_SHIFT 9

/*
 * A global "cli()" while in an interrupt context
 * turns into just a local cli(). Interrupts
 * should use spinlocks for the (very unlikely)
 * case that they ever want to protect against
 * each other.
 *
 * If we already have local interrupts disabled,
 * this will not turn a local disable into a
 * global one (problems with spinlocks: this makes
 * save_flags+cli+sti usable inside a spinlock).
 */
void __global_cli(void)
{
	unsigned int flags;

	__save_flags(flags);
	if (flags & (1 << EFLAGS_IF_SHIFT)) {
		int cpu = smp_processor_id();
		__cli();
		if (!local_irq_count[cpu])
			get_irqlock(cpu);
	}
}

void __global_sti(void)
{
	int cpu = smp_processor_id();

	if (!local_irq_count[cpu])
		release_irqlock(cpu);
	__sti();
}

/*
 * SMP flags value to restore to:
 * 0 - global cli
 * 1 - global sti
 * 2 - local cli
 * 3 - local sti
 */
unsigned long __global_save_flags(void)
{
	int retval;
	int local_enabled;
	unsigned long flags;

	__save_flags(flags);
	local_enabled = (flags >> EFLAGS_IF_SHIFT) & 1;
	/* default to local */
	retval = 2 + local_enabled;

	/* check for global flags if we're not in an interrupt */
	if (!local_irq_count[smp_processor_id()]) {
		if (local_enabled)
			retval = 1;
		if (global_irq_holder == (unsigned char) smp_processor_id())
			retval = 0;
	}
	return retval;
}

void __global_restore_flags(unsigned long flags)
{
	switch (flags) {
	case 0:
		__global_cli();
		break;
	case 1:
		__global_sti();
		break;
	case 2:
		__cli();
		break;
	case 3:
		__sti();
		break;
	default:
		printk("global_restore_flags: %08lx (%08lx)\n",
			flags, (&flags)[-1]);
	}
}

#endif

static int handle_IRQ_event(unsigned int irq, struct pt_regs * regs)
{
	struct irqaction * action;
	int status;

	status = 0;
	action = irq_desc[irq].action;

	if (action) {
		status |= 1;

		if (!(action->flags & SA_INTERRUPT))
			__sti();

		do {
			status |= action->flags;
			action->handler(irq, action->dev_id, regs);
			action = action->next;
		} while (action);
		if (status & SA_SAMPLE_RANDOM)
			add_interrupt_randomness(irq);
		__cli();
	}

	return status;
}

/*
 * disable/enable_irq() wait for all irq contexts to finish
 * executing. Also it's recursive.
 */
static void disable_8259A_irq(unsigned int irq)
{
	cached_irq_mask |= 1 << irq;
	set_8259A_irq_mask(irq);
}

void enable_8259A_irq (unsigned int irq)
{
	cached_irq_mask &= ~(1 << irq);
	set_8259A_irq_mask(irq);
}

int i8259A_irq_pending (unsigned int irq)
{
	unsigned int mask = 1<<irq;

	if (irq < 8)
                return (inb(0x20) & mask);
        return (inb(0xA0) & (mask >> 8));
}


void make_8259A_irq (unsigned int irq)
{
	io_apic_irqs &= ~(1<<irq);
	irq_desc[irq].handler = &i8259A_irq_type;
	disable_irq(irq);
	enable_irq(irq);
}

/*
 * Careful! The 8259A is a fragile beast, it pretty
 * much _has_ to be done exactly like this (mask it
 * first, _then_ send the EOI, and the order of EOI
 * to the two 8259s is important!
 */
static inline void mask_and_ack_8259A(unsigned int irq)
{
	spin_lock(&irq_controller_lock);
	irq_desc[irq].status |= IRQ_INPROGRESS;
	cached_irq_mask |= 1 << irq;
	if (irq & 8) {
		inb(0xA1);	/* DUMMY */
		outb(cached_A1,0xA1);
		outb(0x62,0x20);	/* Specific EOI to cascade */
		outb(0x20,0xA0);
	} else {
		inb(0x21);	/* DUMMY */
		outb(cached_21,0x21);
		outb(0x20,0x20);
	}
	spin_unlock(&irq_controller_lock);
}

static void do_8259A_IRQ(unsigned int irq, int cpu, struct pt_regs * regs)
{
	mask_and_ack_8259A(irq);

	irq_enter(cpu, irq);

	if (handle_IRQ_event(irq, regs)) {
		spin_lock(&irq_controller_lock);
		if (!(irq_desc[irq].status &= IRQ_DISABLED))
			unmask_8259A(irq);
		spin_unlock(&irq_controller_lock);
	}

	irq_exit(cpu, irq);
}

#ifdef __SMP__

/*
 * In the SMP+IOAPIC case it might happen that there are an unspecified
 * number of pending IRQ events unhandled. These cases are very rare,
 * so we 'resend' these IRQs via IPIs, to the same CPU. It's much
 * better to do it this way as thus we dont have to be aware of
 * 'pending' interrupts in the IRQ path, except at this point.
 */
static inline void self_IPI (unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;

	if (desc->events && !desc->ipi) {
		desc->ipi = 1;
		send_IPI(APIC_DEST_SELF, IO_APIC_VECTOR(irq));
	}
}

static void enable_edge_ioapic_irq(unsigned int irq)
{
	self_IPI(irq);
}

static void disable_edge_ioapic_irq(unsigned int irq)
{
}

static void enable_level_ioapic_irq(unsigned int irq)
{
	enable_IO_APIC_irq(irq);
	self_IPI(irq);
}

static void disable_level_ioapic_irq(unsigned int irq)
{
	disable_IO_APIC_irq(irq);
}

/*
 * Has to be called with the irq controller locked, and has to exit
 * with with the lock held as well.
 */
static void handle_ioapic_event (unsigned int irq, int cpu,
						struct pt_regs * regs)
{
	irq_desc_t *desc = irq_desc + irq;

	desc->status = IRQ_INPROGRESS;
	desc->events = 0;
	hardirq_enter(cpu);
	spin_unlock(&irq_controller_lock);

	while (test_bit(0,&global_irq_lock)) barrier();

	for (;;) {
		int pending, handler;

		/*
		 * If there is no IRQ handler, exit early, leaving the irq
		 * "in progress"
		 */
		handler = handle_IRQ_event(irq, regs);

		spin_lock(&irq_controller_lock);
		if (!handler)
			goto no_handler;

		pending = desc->events;
		desc->events = 0;
		if (!pending)
			break;
		spin_unlock(&irq_controller_lock);
	}
	desc->status &= IRQ_DISABLED;

no_handler:
}

static void do_edge_ioapic_IRQ(unsigned int irq, int cpu, struct pt_regs * regs)
{
	irq_desc_t *desc = irq_desc + irq;

	/*
	 * Edge triggered IRQs can be acked immediately
	 */
	ack_APIC_irq();

	spin_lock(&irq_controller_lock);
	desc->ipi = 0;

	/*
	 * If the irq is disabled for whatever reason, just
	 * set a flag and return
	 */
	if (desc->status & (IRQ_DISABLED | IRQ_INPROGRESS)) {
		desc->events = 1;
		spin_unlock(&irq_controller_lock);
		return;
	}

	handle_ioapic_event(irq,cpu,regs);
	spin_unlock(&irq_controller_lock);

	hardirq_exit(cpu);
	release_irqlock(cpu);
}

static void do_level_ioapic_IRQ (unsigned int irq, int cpu,
						 struct pt_regs * regs)
{
	irq_desc_t *desc = irq_desc + irq;

	spin_lock(&irq_controller_lock);
	/*
	 * In the level triggered case we first disable the IRQ
	 * in the IO-APIC, then we 'early ACK' the IRQ, then we
	 * handle it and enable the IRQ when finished.
	 *
	 * disable has to happen before the ACK, to avoid IRQ storms.
	 * So this all has to be within the spinlock.
	 */
	disable_IO_APIC_irq(irq);
	ack_APIC_irq();
	desc->ipi = 0;

	/*
	 * If the irq is disabled for whatever reason, just
	 * set a flag and return
	 */
	if (desc->status & (IRQ_DISABLED | IRQ_INPROGRESS)) {
		desc->events = 1;
		spin_unlock(&irq_controller_lock);
		return;
	}

	handle_ioapic_event(irq,cpu,regs);
	/* we still have the spinlock held here */

	enable_IO_APIC_irq(irq);
	spin_unlock(&irq_controller_lock);

	hardirq_exit(cpu);
	release_irqlock(cpu);
}

#endif


/*
 * Generic enable/disable code: this just calls
 * down into the PIC-specific version for the actual
 * hardware disable after having gotten the irq
 * controller lock. 
 */
void disable_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	/*
	 * At this point we may actually have a pending interrupt being active
	 * on another CPU. So don't touch the IRQ_INPROGRESS bit..
	 */
	irq_desc[irq].status |= IRQ_DISABLED;
	irq_desc[irq].handler->disable(irq);
	spin_unlock_irqrestore(&irq_controller_lock, flags);

	synchronize_irq();
}

void enable_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	/*
	 * In contrast to the above, we should _not_ have any concurrent
	 * interrupt activity here, so we just clear both disabled bits.
	 *
	 * This allows us to have IRQ_INPROGRESS set until we actually
	 * install a handler for this interrupt (make irq autodetection
	 * work by just looking at the status field for the irq)
	 */
	irq_desc[irq].status = 0;
	irq_desc[irq].handler->enable(irq);
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 *
 * the biggest change on SMP is the fact that we no more mask
 * interrupts in hardware, please believe me, this is unavoidable,
 * the hardware is largely message-oriented, i tried to force our
 * state-driven irq handling scheme onto the IO-APIC, but no avail.
 *
 * so we soft-disable interrupts via 'event counters', the first 'incl'
 * will do the IRQ handling. This also has the nice side effect of increased
 * overlapping ... i saw no driver problem so far.
 */
asmlinkage void do_IRQ(struct pt_regs regs)
{	
	/* 
	 * We ack quickly, we don't want the irq controller
	 * thinking we're snobs just because some other CPU has
	 * disabled global interrupts (we have already done the
	 * INT_ACK cycles, it's too late to try to pretend to the
	 * controller that we aren't taking the interrupt).
	 *
	 * 0 return value means that this irq is already being
	 * handled by some other CPU. (or is disabled)
	 */
	unsigned int irq = regs.orig_eax & 0xff;
	int cpu = smp_processor_id();

	kstat.irqs[cpu][irq]++;
	irq_desc[irq].handler->handle(irq, cpu, &regs);

	/*
	 * This should be conditional: we should really get
	 * a return code from the irq handler to tell us
	 * whether the handler wants us to do software bottom
	 * half handling or not..
	 */
	if (1) {
		if (bh_active & bh_mask)
			do_bottom_half();
	}
}

int setup_x86_irq(unsigned int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	/*
	 * Some drivers like serial.c use request_irq() heavily,
	 * so we have to be careful not to interfere with a
	 * running system.
	 */
	if (new->flags & SA_SAMPLE_RANDOM) {
		/*
		 * This function might sleep, we want to call it first,
		 * outside of the atomic block.
		 * Yes, this might clear the entropy pool if the wrong
		 * driver is attempted to be loaded, without actually
		 * installing a new handler, but is this really a problem,
		 * only the sysadmin is able to do this.
		 */
		rand_initialize_irq(irq);
	}

	/*
	 * The following block of code has to be executed atomically
	 */
	spin_lock_irqsave(&irq_controller_lock,flags);
	p = &irq_desc[irq].action;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			spin_unlock_irqrestore(&irq_controller_lock,flags);
			return -EBUSY;
		}

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	*p = new;

	if (!shared) {
#ifdef __SMP__
		if (IO_APIC_IRQ(irq)) {
			if (IO_APIC_VECTOR(irq) > 0xfe)
				/*
				 * break visibly for now, FIXME
				 */
				panic("ayiee, tell mingo");

			/*
			 * First disable it in the 8259A:
			 */
			cached_irq_mask |= 1 << irq;
			if (irq < 16) {
				set_8259A_irq_mask(irq);
				/*
				 * transport pending ISA IRQs to
				 * the new descriptor
				 */
				if (i8259A_irq_pending(irq))
					irq_desc[irq].events = 1;
			}
		}
#endif
		irq_desc[irq].status = 0;
		irq_desc[irq].handler->enable(irq);
	}
	spin_unlock_irqrestore(&irq_controller_lock,flags);
	return 0;
}

int request_irq(unsigned int irq, 
		void (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, 
		const char * devname,
		void *dev_id)
{
	int retval;
	struct irqaction * action;

	if (irq >= NR_IRQS)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

	action = (struct irqaction *)
			kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_x86_irq(irq, action);

	if (retval)
		kfree(action);
	return retval;
}
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq >= NR_IRQS)
		return;

	spin_lock_irqsave(&irq_controller_lock,flags);
	for (p = &irq_desc[irq].action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		*p = action->next;
		kfree(action);
		irq_desc[irq].handler->disable(irq);
		goto out;
	}
	printk("Trying to free free IRQ%d\n",irq);
out:
	spin_unlock_irqrestore(&irq_controller_lock,flags);
}

/*
 * IRQ autodetection code..
 *
 * This depends on the fact that any interrupt that
 * comes in on to an unassigned handler will get stuck
 * with "IRQ_INPROGRESS" asserted and the interrupt
 * disabled.
 */
unsigned long probe_irq_on (void)
{
	unsigned int i, irqs = 0;
	unsigned long delay;

	/*
	 * first, enable any unassigned irqs
	 */
	spin_lock_irq(&irq_controller_lock);
	for (i = NR_IRQS-1; i > 0; i--) {
		if (!irq_desc[i].action) {
			irq_desc[i].handler->enable(i);
			irqs |= (1 << i);
		}
	}
	spin_unlock_irq(&irq_controller_lock);

	/*
	 * wait for spurious interrupts to increase counters
	 */
	for (delay = jiffies + HZ/10; delay > jiffies; )
		/* about 100ms delay */ synchronize_irq();

	/*
	 * now filter out any obviously spurious interrupts
	 */
	spin_lock_irq(&irq_controller_lock);
	for (i=0; i<NR_IRQS; i++) {
		if (irq_desc[i].status & IRQ_INPROGRESS)
			irqs &= ~(1UL << i);
	}
	spin_unlock_irq(&irq_controller_lock);

	return irqs;
}

int probe_irq_off (unsigned long irqs)
{
	int i, irq_found = -1;

	spin_lock_irq(&irq_controller_lock);
	for (i=0; i<NR_IRQS; i++) {
		if ((irqs & 1) && (irq_desc[i].status & IRQ_INPROGRESS)) {
			if (irq_found != -1) {
				irq_found = -irq_found;
				goto out;
			}
			irq_found = i;
		}
		irqs >>= 1;
	}
	if (irq_found == -1)
		irq_found = 0;
out:
	spin_unlock_irq(&irq_controller_lock);
	return irq_found;
}

#ifdef __SMP__
void init_IO_APIC_traps(void)
{
	int i;
	/*
	 * NOTE! The local APIC isn't very good at handling
	 * multiple interrupts at the same interrupt level.
	 * As the interrupt level is determined by taking the
	 * vector number and shifting that right by 4, we
	 * want to spread these out a bit so that they don't
	 * all fall in the same interrupt level
	 *
	 * also, we've got to be careful not to trash gate
	 * 0x80, because int 0x80 is hm, kindof importantish ;)
	 */
	for (i = 0; i < NR_IRQS ; i++) {
		if ((IO_APIC_VECTOR(i) <= 0xfe)  /* HACK */ &&
		    (IO_APIC_IRQ(i))) {
			if (IO_APIC_irq_trigger(i))
				irq_desc[i].handler = &ioapic_level_irq_type;
			else /* edge */
				irq_desc[i].handler = &ioapic_level_irq_type;
			/*
			 * disable it in the 8259A:
			 */
			cached_irq_mask |= 1 << i;
			if (i < 16)
				set_8259A_irq_mask(i);
		}
	}
}
#endif

__initfunc(void init_IRQ(void))
{
	int i;

	/* set the clock to 100 Hz */
	outb_p(0x34,0x43);		/* binary, mode 2, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */

	for (i=0; i<NR_IRQS; i++) {
		irq_desc[i].events = 0;
		irq_desc[i].status = 0;
	}
	/*
	 * 16 old-style INTA-cycle interrupt gates:
	 */
	for (i = 0; i < 16; i++)
		set_intr_gate(0x20+i,interrupt[i]);

#ifdef __SMP__	

	for (i = 0; i < NR_IRQS ; i++)
		if (IO_APIC_VECTOR(i) <= 0xfe)  /* hack -- mingo */
			set_intr_gate(IO_APIC_VECTOR(i),interrupt[i]);

	/*
	 * The reschedule interrupt slowly changes it's functionality,
	 * while so far it was a kind of broadcasted timer interrupt,
	 * in the future it should become a CPU-to-CPU rescheduling IPI,
	 * driven by schedule() ?
	 *
	 * [ It has to be here .. it doesn't work if you put
	 *   it down the bottom - assembler explodes 8) ]
	 */

	/* IPI for rescheduling */
	set_intr_gate(0x30, reschedule_interrupt);

	/* IPI for invalidation */
	set_intr_gate(0x31, invalidate_interrupt);

	/* IPI for CPU halt */
	set_intr_gate(0x40, stop_cpu_interrupt);

	/* self generated IPI for local APIC timer */
	set_intr_gate(0x41, apic_timer_interrupt);

	/* IPI for MTRR control */
	set_intr_gate(0x50, mtrr_interrupt);

	/* IPI vector for APIC spurious interrupts */
	set_intr_gate(0xff, spurious_interrupt);
#endif	
	request_region(0x20,0x20,"pic1");
	request_region(0xa0,0x20,"pic2");
	setup_x86_irq(2, &irq2);
	setup_x86_irq(13, &irq13);
} 

