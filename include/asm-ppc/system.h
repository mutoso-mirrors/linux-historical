#ifndef __PPC_SYSTEM_H
#define __PPC_SYSTEM_H

#include <linux/kdev_t.h>
#include <asm/processor.h>

#define mb()  __asm__ __volatile__ ("sync" : : : "memory")

#define __save_flags(flags)	({\
	__asm__ __volatile__ ("mfmsr %0" : "=r" ((flags)) : : "memory"); })
#define __save_and_cli(flags)	({__save_flags(flags);__cli();})

extern __inline__ void __restore_flags(unsigned long flags)
{
        extern unsigned lost_interrupts;
	extern void do_lost_interrupts(unsigned long);

        if ((flags & MSR_EE) && lost_interrupts != 0) {
                do_lost_interrupts(flags);
        } else {
                __asm__ __volatile__ ("sync; mtmsr %0; isync"
                              : : "r" (flags) : "memory");
        }
}


#if 0
/*
 * Gcc bug prevents us from using this inline func so for now
 * it lives in misc.S
 */
void __inline__ __restore_flags(unsigned long flags)
{
	extern unsigned lost_interrupts;
	__asm__ __volatile__ (
		"andi.	0,%0,%2 \n\t"
		"beq	2f \n\t"
		"cmpi	0,%1,0 \n\t"
		"bne	do_lost_interrupts \n\t"
		"2:	sync \n\t"
		"mtmsr	%0 \n\t"
		"isync \n\t"
		: 
		: "r" (flags), "r"(lost_interrupts), "i" (1<<15)/*MSR_EE*/
		: "0", "cc");
}
#endif

extern void __sti(void);
extern void __cli(void);
extern int _disable_interrupts(void);
extern void _enable_interrupts(int);

extern void print_backtrace(unsigned long *);
extern void show_regs(struct pt_regs * regs);
extern void flush_instruction_cache(void);
extern void hard_reset_now(void);
extern void poweroff_now(void);
/*extern void note_bootable_part(kdev_t, int);*/
extern int sd_find_target(void *, int);
extern int _get_PVR(void);
extern void via_cuda_init(void);
extern void pmac_nvram_init(void);
extern void read_rtc_time(void);
extern void pmac_find_display(void);
extern void giveup_fpu(void);
extern void cvt_fd(float *from, double *to);
extern void cvt_df(double *from, float *to);

struct device_node;
extern void note_scsi_host(struct device_node *, void *);

struct task_struct;
extern void switch_to(struct task_struct *prev, struct task_struct *next);

struct thread_struct;
extern void _switch(struct thread_struct *prev, struct thread_struct *next,
		    unsigned long context);

struct pt_regs;
extern int do_signal(unsigned long oldmask, struct pt_regs *regs);
extern void dump_regs(struct pt_regs *);

#ifndef __SMP__

#define cli()	__cli()
#define sti()	__sti()
#define save_flags(flags)	__save_flags(flags)
#define restore_flags(flags)	__restore_flags(flags)

#else /* __SMP__ */

extern void __global_cli(void);
extern void __global_sti(void);
extern unsigned long __global_save_flags(void);
extern void __global_restore_flags(unsigned long);
#define cli() __global_cli()
#define sti() __global_sti()
#define save_flags(x) ((x)=__global_save_flags())
#define restore_flags(x) __global_restore_flags(x)

#endif /* !__SMP__ */

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

extern void *xchg_u64(void *ptr, unsigned long val);
extern void *xchg_u32(void *m, unsigned long val);

/*
 * This function doesn't exist, so you'll get a linker error
 * if something tries to do an invalid xchg().
 *
 * This only works if the compiler isn't horribly bad at optimizing.
 * gcc-2.5.8 reportedly can't handle this, but as that doesn't work
 * too well on the alpha anyway..
 */
extern void __xchg_called_with_bad_pointer(void);

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

static inline unsigned long __xchg(unsigned long x, void * ptr, int size)
{
	switch (size) {
		case 4:
			return (unsigned long )xchg_u32(ptr, x);
		case 8:
			return (unsigned long )xchg_u64(ptr, x);
	}
	__xchg_called_with_bad_pointer();
	return x;


}

extern inline void * xchg_ptr(void * m, void * val)
{
	return (void *) xchg_u32(m, (unsigned long) val);
}

#endif
