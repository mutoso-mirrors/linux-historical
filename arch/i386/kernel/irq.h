#ifndef __irq_h
#define __irq_h

/*
 * Various low-level irq details needed by irq.c and smp.c
 *
 * Interrupt entry/exit code at both C and assembly level
 */

#ifdef __SMP__

#undef INIT_STUCK
#define INIT_STUCK 200000000

#undef STUCK
#define STUCK \
if (!--stuck) {printk("irq_enter stuck (irq=%d, cpu=%d, global=%d)\n",irq,cpu,global_irq_holder); stuck = INIT_STUCK;}

static inline void irq_enter(int cpu, int irq)
{
	int stuck = INIT_STUCK;

	hardirq_enter(cpu);
	while (test_bit(0,&global_irq_lock)) {
		if ((unsigned char) cpu == global_irq_holder) {
			static int printed = 0;
			if (!printed)
				printk("BAD! Local interrupts enabled, global disabled\n");
			printed++;
			break;
		}
		STUCK;
		/* nothing */;
	}
}

static inline void irq_exit(int cpu, int irq)
{
	__cli();
	hardirq_exit(cpu);
	release_irqlock(cpu);
}

#else

#define irq_enter(cpu, irq)	(++local_irq_count[cpu])
#define irq_exit(cpu, irq)	(--local_irq_count[cpu])

#endif

#define __STR(x) #x
#define STR(x) __STR(x)

#define SAVE_ALL \
	"cld\n\t" \
	"push %es\n\t" \
	"push %ds\n\t" \
	"pushl %eax\n\t" \
	"pushl %ebp\n\t" \
	"pushl %edi\n\t" \
	"pushl %esi\n\t" \
	"pushl %edx\n\t" \
	"pushl %ecx\n\t" \
	"pushl %ebx\n\t" \
	"movl $" STR(KERNEL_DS) ",%edx\n\t" \
	"mov %dx,%ds\n\t" \
	"mov %dx,%es\n\t"

/*
 * These are used just for the "bad" interrupt handlers, 
 * which just clear the mask and return..
 */
#define SAVE_MOST \
	"cld\n\t" \
	"push %es\n\t" \
	"push %ds\n\t" \
	"pushl %eax\n\t" \
	"pushl %edx\n\t" \
	"pushl %ecx\n\t" \
	"movl $" STR(KERNEL_DS) ",%edx\n\t" \
	"mov %dx,%ds\n\t" \
	"mov %dx,%es\n\t"

#define RESTORE_MOST \
	"popl %ecx\n\t" \
	"popl %edx\n\t" \
	"popl %eax\n\t" \
	"pop %ds\n\t" \
	"pop %es\n\t" \
	"iret"

/*
 * The "inb" instructions are not needed, but seem to change the timings
 * a bit - without them it seems that the harddisk driver won't work on
 * all hardware. Arghh.
 */
#define ACK_FIRST(mask,nr) \
	"inb $0x21,%al\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\torb $" #mask ","SYMBOL_NAME_STR(cache_21)"\n\t" \
	"movb "SYMBOL_NAME_STR(cache_21)",%al\n\t" \
	"outb %al,$0x21\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\tmovb $0x20,%al\n\t" \
	"outb %al,$0x20\n\t"

#define ACK_SECOND(mask,nr) \
	"inb $0xA1,%al\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\torb $" #mask ","SYMBOL_NAME_STR(cache_A1)"\n\t" \
	"movb "SYMBOL_NAME_STR(cache_A1)",%al\n\t" \
	"outb %al,$0xA1\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\tmovb $0x20,%al\n\t" \
	"outb %al,$0xA0\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\toutb %al,$0x20\n\t"

#define UNBLK_FIRST(mask) \
	"inb $0x21,%al\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\tandb $~(" #mask "),"SYMBOL_NAME_STR(cache_21)"\n\t" \
	"movb "SYMBOL_NAME_STR(cache_21)",%al\n\t" \
	"outb %al,$0x21\n\t"

#define UNBLK_SECOND(mask) \
	"inb $0xA1,%al\n\t" \
	"jmp 1f\n" \
	"1:\tjmp 1f\n" \
	"1:\tandb $~(" #mask "),"SYMBOL_NAME_STR(cache_A1)"\n\t" \
	"movb "SYMBOL_NAME_STR(cache_A1)",%al\n\t" \
	"outb %al,$0xA1\n\t"

#define IRQ_NAME2(nr) nr##_interrupt(void)
#define IRQ_NAME(nr) IRQ_NAME2(IRQ##nr)
#define BAD_IRQ_NAME(nr) IRQ_NAME2(bad_IRQ##nr)

#define GET_CURRENT \
	"movl %esp, %ebx\n\t" \
	"andl $-8192, %ebx\n\t"

#ifdef __SMP__

/*
 *	SMP has a few special interrupts for IPI messages
 */

#define BUILD_SMP_INTERRUPT(x) \
asmlinkage void x(void); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(x) ":\n\t" \
	"pushl $-1\n\t" \
	SAVE_ALL \
	"call "SYMBOL_NAME_STR(smp_##x)"\n\t" \
	"jmp ret_from_intr\n");

#define BUILD_SMP_TIMER_INTERRUPT(x) \
asmlinkage void x(struct pt_regs * regs); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(x) ":\n\t" \
	"pushl $-1\n\t" \
	SAVE_ALL \
	"movl %esp,%eax\n\t" \
	"pushl %eax\n\t" \
	"call "SYMBOL_NAME_STR(smp_##x)"\n\t" \
	"addl $4,%esp\n\t" \
	"jmp ret_from_intr\n");

#endif /* __SMP__ */

#define BUILD_IRQ(chip,nr,mask) \
asmlinkage void IRQ_NAME(nr); \
asmlinkage void BAD_IRQ_NAME(nr); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(IRQ) #nr "_interrupt:\n\t" \
	"pushl $-"#nr"-2\n\t" \
	SAVE_ALL \
	ACK_##chip(mask,(nr&7)) \
	"movl %esp,%eax\n\t" \
	"pushl %eax\n\t" \
	"pushl $" #nr "\n\t" \
	"call "SYMBOL_NAME_STR(do_IRQ)"\n\t" \
	"addl $8,%esp\n\t" \
	UNBLK_##chip(mask) \
	"jmp ret_from_intr\n" \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(bad_IRQ) #nr "_interrupt:\n\t" \
	SAVE_MOST \
	ACK_##chip(mask,(nr&7)) \
	RESTORE_MOST);

#define BUILD_TIMER_IRQ(chip,nr,mask) \
asmlinkage void IRQ_NAME(nr); \
asmlinkage void BAD_IRQ_NAME(nr); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(bad_IRQ) #nr "_interrupt:\n\t" \
SYMBOL_NAME_STR(IRQ) #nr "_interrupt:\n\t" \
	"pushl $-"#nr"-2\n\t" \
	SAVE_ALL \
	ACK_##chip(mask,(nr&7)) \
	"movl %esp,%eax\n\t" \
	"pushl %eax\n\t" \
	"pushl $" #nr "\n\t" \
	"call "SYMBOL_NAME_STR(do_IRQ)"\n\t" \
	"addl $8,%esp\n\t" \
	UNBLK_##chip(mask) \
	"jmp ret_from_intr\n");

/*
 * x86 profiling function, SMP safe. We might want to do this in
 * assembly totally?
 */
static inline void x86_do_profile (unsigned long eip)
{
	if (prof_buffer && current->pid) {
		extern int _stext;
		eip -= (unsigned long) &_stext;
		eip >>= prof_shift;
		if (eip < prof_len)
			atomic_inc((atomic_t *)&prof_buffer[eip]);
		else
		/*
		 * Dont ignore out-of-bounds EIP values silently,
		 * put them into the last histogram slot, so if
		 * present, they will show up as a sharp peak.
		 */
			atomic_inc((atomic_t *)&prof_buffer[prof_len-1]);
	}
}

#endif
