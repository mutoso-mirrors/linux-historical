#include <linux/config.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/elfcore.h>
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/vmalloc.h>
#include <linux/pci.h>
#include <linux/irq.h>

#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/checksum.h>
#include <asm/io.h>
#include <asm/hardirq.h>
#include <asm/delay.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

extern void dump_thread(struct pt_regs *, struct user *);
extern int dump_fpu(struct pt_regs *, elf_fpregset_t *);
extern struct hw_interrupt_type no_irq_type;

EXPORT_SYMBOL(sh_mv);

/* platform dependent support */
EXPORT_SYMBOL(dump_thread);
EXPORT_SYMBOL(dump_fpu);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(enable_irq);
EXPORT_SYMBOL(disable_irq);
EXPORT_SYMBOL(probe_irq_mask);
EXPORT_SYMBOL(kernel_thread);
EXPORT_SYMBOL(disable_irq_nosync);
EXPORT_SYMBOL(irq_desc);
EXPORT_SYMBOL(no_irq_type);

EXPORT_SYMBOL(strpbrk);
EXPORT_SYMBOL(strstr);
EXPORT_SYMBOL(strlen);
EXPORT_SYMBOL(strnlen);
EXPORT_SYMBOL(strchr);
EXPORT_SYMBOL(strcat);
EXPORT_SYMBOL(strncat);

/* PCI exports */
#ifdef CONFIG_PCI
EXPORT_SYMBOL(pci_alloc_consistent);
EXPORT_SYMBOL(pci_free_consistent);
EXPORT_SYMBOL(pcibios_penalize_isa_irq);
#endif

/* mem exports */
EXPORT_SYMBOL(memchr);
EXPORT_SYMBOL(memcpy);
EXPORT_SYMBOL(memcpy_fromio);
EXPORT_SYMBOL(memcpy_toio);
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memset_io);
EXPORT_SYMBOL(memmove);
EXPORT_SYMBOL(memcmp);
EXPORT_SYMBOL(memscan);
EXPORT_SYMBOL(__copy_user);

#ifdef CONFIG_VT
/* XXX: MRB-remove - what was screen_info changed to?
EXPORT_SYMBOL(screen_info);
*/
#endif

EXPORT_SYMBOL(boot_cpu_data);

#ifdef CONFIG_MMU
EXPORT_SYMBOL(get_vm_area);
#endif

/* semaphore exports */
EXPORT_SYMBOL(__up);
EXPORT_SYMBOL(__down);
EXPORT_SYMBOL(__down_interruptible);

EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__ndelay);
EXPORT_SYMBOL(__const_udelay);

#define DECLARE_EXPORT(name) extern void name(void);EXPORT_SYMBOL(name)

/* These symbols are generated by the compiler itself */
DECLARE_EXPORT(__udivsi3);
DECLARE_EXPORT(__udivdi3);
DECLARE_EXPORT(__sdivsi3);
DECLARE_EXPORT(__ashrdi3);
DECLARE_EXPORT(__ashldi3);
DECLARE_EXPORT(__lshrdi3);
DECLARE_EXPORT(__movstr);

#ifdef CONFIG_CPU_SH4
 
DECLARE_EXPORT(__movstr_i4_even);
DECLARE_EXPORT(__movstr_i4_odd);
DECLARE_EXPORT(__movstrSI12_i4);

/* needed by some modules */
EXPORT_SYMBOL(flush_cache_all);
EXPORT_SYMBOL(flush_cache_range);
EXPORT_SYMBOL(flush_dcache_page);
EXPORT_SYMBOL(__flush_purge_region);
#endif
EXPORT_SYMBOL(flush_tlb_page);
EXPORT_SYMBOL(__down_trylock);

#ifdef CONFIG_SMP
EXPORT_SYMBOL(synchronize_irq);
#endif

EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(consistent_sync);
