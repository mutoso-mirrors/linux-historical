/* $Id: hardirq.h,v 1.4 2000/02/23 00:41:38 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997, 1998, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_HARDIRQ_H
#define _ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>

extern unsigned int local_irq_count[NR_CPUS];

/*
 * Are we in an interrupt context? Either doing bottom half
 * or hardware interrupt processing?
 */
#define in_interrupt() ({ int __cpu = smp_processor_id(); \
	(local_irq_count[__cpu] + local_bh_count[__cpu] != 0); })
#define in_irq() (local_irq_count[smp_processor_id()] != 0)

#ifndef CONFIG_SMP

#define hardirq_trylock(cpu)	(local_irq_count[cpu] == 0)
#define hardirq_endlock(cpu)	do { } while (0)

#define irq_enter(cpu)		(local_irq_count[cpu]++)
#define irq_exit(cpu)		(local_irq_count[cpu]--)

#define synchronize_irq()	barrier();

#else

#error No habla MIPS SMP

#endif /* CONFIG_SMP */
#endif /* _ASM_HARDIRQ_H */
