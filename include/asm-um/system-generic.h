#ifndef __UM_SYSTEM_GENERIC_H
#define __UM_SYSTEM_GENERIC_H

#include "asm/arch/system.h"

#undef switch_to
#undef local_irq_save
#undef local_irq_restore
#undef local_irq_disable
#undef local_irq_enable
#undef local_save_flags
#undef local_irq_restore
#undef local_irq_enable
#undef local_irq_disable
#undef local_irq_save
#undef irqs_disabled

extern void *switch_to(void *prev, void *next, void *last);

extern int set_signals(int enable);
extern void block_signals(void);
extern void unblock_signals(void);
extern int get_signals(void);

#define local_save_flags(flags) do { (flags) = get_signals(); } while(0)
#define local_irq_restore(flags) do { set_signals(flags); } while(0)

#define local_irq_save(flags) do { local_save_flags(flags); \
                                   local_irq_disable(); } while(0)

#define local_irq_enable() unblock_signals()
#define local_irq_disable() block_signals()

#define irqs_disabled()			\
({					\
	unsigned long flags;		\
	local_save_flags(flags);	\
	(flags != 0);			\
})

#endif
