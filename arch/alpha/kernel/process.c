/*
 *  linux/arch/alpha/kernel/process.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of process handling..
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/ldt.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/utsname.h>
#include <linux/time.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/elfcore.h>

#include <asm/reg.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>

asmlinkage int sys_sethae(unsigned long hae, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs regs)
{
	(&regs)->hae = hae;
	return 0;
}

asmlinkage int sys_idle(void)
{
	if (current->pid != 0)
		return -EPERM;

	/* endless idle loop with no priority at all */
	current->counter = -100;
	for (;;) {
		schedule();
	}
}

void hard_reset_now(void)
{
#if defined(CONFIG_ALPHA_SRM) && defined(CONFIG_ALPHA_ALCOR)
	/* who said DEC engineer's have no sense of humor? ;-)) */
	*(int *) GRU_RESET = 0x0000dead;
	mb();
#endif
	halt();
}

void show_regs(struct pt_regs * regs)
{
	printk("\nps: %04lx pc: [<%016lx>]\n", regs->ps, regs->pc);
	printk("rp: [<%016lx>] sp: %p\n", regs->r26, regs+1);
	printk(" r0: %016lx  r1: %016lx  r2: %016lx  r3: %016lx\n",
	       regs->r0, regs->r1, regs->r2, regs->r3);
	printk(" r4: %016lx  r5: %016lx  r6: %016lx  r7: %016lx\n",
	       regs->r4, regs->r5, regs->r6, regs->r7);
	printk(" r8: %016lx r16: %016lx r17: %016lx r18: %016lx\n",
	       regs->r8, regs->r16, regs->r17, regs->r18);
	printk("r19: %016lx r20: %016lx r21: %016lx r22: %016lx\n",
	       regs->r19, regs->r20, regs->r21, regs->r22);
	printk("r23: %016lx r24: %016lx r25: %016lx r26: %016lx\n",
	       regs->r23, regs->r24, regs->r25, regs->r26);
	printk("r27: %016lx r28: %016lx r29: %016lx hae: %016lx\n",
	       regs->r27, regs->r28, regs->gp, regs->hae);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
}

void flush_thread(void)
{
}

void release_thread(struct task_struct *dead_task)
{
}

/*
 * "alpha_clone()".. By the time we get here, the
 * non-volatile registers have also been saved on the
 * stack. We do some ugly pointer stuff here.. (see
 * also copy_thread)
 *
 * Notice that "fork()" is implemented in terms of clone,
 * with parameters (SIGCHLD, 0).
 */
int alpha_clone(unsigned long clone_flags, unsigned long usp,
	struct switch_stack * swstack)
{
	if (!usp)
		usp = rdusp();
	return do_fork(clone_flags, usp, (struct pt_regs *) (swstack+1));
}

extern void ret_from_sys_call(void);
/*
 * Copy an alpha thread..
 *
 * Note the "stack_offset" stuff: when returning to kernel mode, we need
 * to have some extra stack-space for the kernel stack that still exists
 * after the "ret_from_sys_call". When returning to user mode, we only
 * want the space needed by the syscall stack frame (ie "struct pt_regs").
 * Use the passed "regs" pointer to determine how much space we need
 * for a kernel fork().
 */
void copy_thread(int nr, unsigned long clone_flags, unsigned long usp,
	struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs * childregs;
	struct switch_stack * childstack, *stack;
	unsigned long stack_offset;

	stack_offset = PAGE_SIZE - sizeof(struct pt_regs);
	if (!(regs->ps & 8))
		stack_offset = (PAGE_SIZE-1) & (unsigned long) regs;
	childregs = (struct pt_regs *) (p->kernel_stack_page + stack_offset);
		
	*childregs = *regs;
	childregs->r0 = 0;
	childregs->r19 = 0;
	childregs->r20 = 1;	/* OSF/1 has some strange fork() semantics.. */
	regs->r20 = 0;
	stack = ((struct switch_stack *) regs) - 1;
	childstack = ((struct switch_stack *) childregs) - 1;
	*childstack = *stack;
	childstack->r26 = (unsigned long) ret_from_sys_call;
	p->tss.usp = usp;
	p->tss.ksp = (unsigned long) childstack;
	p->tss.flags = 1;
	p->mm->context = 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread(struct pt_regs * pt, struct user * dump)
{
	/* switch stack follows right below pt_regs: */
	struct switch_stack * sw = ((struct switch_stack *) pt) - 1;

	dump->magic = CMAGIC;
	dump->start_code  = current->mm->start_code;
	dump->start_data  = current->mm->start_data;
	dump->start_stack = rdusp() & ~(PAGE_SIZE - 1);
	dump->u_tsize = (current->mm->end_code - dump->start_code) >> PAGE_SHIFT;
	dump->u_dsize = (current->mm->brk + (PAGE_SIZE - 1) - dump->start_data) >> PAGE_SHIFT;
	dump->u_ssize =
	  (current->mm->start_stack - dump->start_stack + PAGE_SIZE - 1) >> PAGE_SHIFT;

	/*
	 * We store the registers in an order/format that is
	 * compatible with DEC Unix/OSF/1 as this makes life easier
	 * for gdb.
	 */
	dump->regs[EF_V0]  = pt->r0;
	dump->regs[EF_T0]  = pt->r1;
	dump->regs[EF_T1]  = pt->r2;
	dump->regs[EF_T2]  = pt->r3;
	dump->regs[EF_T3]  = pt->r4;
	dump->regs[EF_T4]  = pt->r5;
	dump->regs[EF_T5]  = pt->r6;
	dump->regs[EF_T6]  = pt->r7;
	dump->regs[EF_T7]  = pt->r8;
	dump->regs[EF_S0]  = sw->r9;
	dump->regs[EF_S1]  = sw->r10;
	dump->regs[EF_S2]  = sw->r11;
	dump->regs[EF_S3]  = sw->r12;
	dump->regs[EF_S4]  = sw->r13;
	dump->regs[EF_S5]  = sw->r14;
	dump->regs[EF_S6]  = sw->r15;
	dump->regs[EF_A3]  = pt->r19;
	dump->regs[EF_A4]  = pt->r20;
	dump->regs[EF_A5]  = pt->r21;
	dump->regs[EF_T8]  = pt->r22;
	dump->regs[EF_T9]  = pt->r23;
	dump->regs[EF_T10] = pt->r24;
	dump->regs[EF_T11] = pt->r25;
	dump->regs[EF_RA]  = pt->r26;
	dump->regs[EF_T12] = pt->r27;
	dump->regs[EF_AT]  = pt->r28;
	dump->regs[EF_SP]  = rdusp();
	dump->regs[EF_PS]  = pt->ps;
	dump->regs[EF_PC]  = pt->pc;
	dump->regs[EF_GP]  = pt->gp;
	dump->regs[EF_A0]  = pt->r16;
	dump->regs[EF_A1]  = pt->r17;
	dump->regs[EF_A2]  = pt->r18;
	memcpy((char *)dump->regs + EF_SIZE, sw->fp, 32 * 8);
}

int dump_fpu (struct pt_regs * regs, elf_fpregset_t *r)
{
	/* switch stack follows right below pt_regs: */
	struct switch_stack * sw = ((struct switch_stack *) regs) - 1;
	memcpy(r, sw->fp, 32 * 8);
	return 1;
}

/*
 * sys_execve() executes a new program.
 *
 * This works due to the alpha calling sequence: the first 6 args
 * are gotten from registers, while the rest is on the stack, so
 * we get a0-a5 for free, and then magically find "struct pt_regs"
 * on the stack for us..
 *
 * Don't do this at home.
 */
asmlinkage int sys_execve(unsigned long a0, unsigned long a1, unsigned long a2,
	unsigned long a3, unsigned long a4, unsigned long a5,
	struct pt_regs regs)
{
	int error;
	char * filename;

	error = getname((char *) a0, &filename);
	if (error)
		return error;
	error = do_execve(filename, (char **) a1, (char **) a2, &regs);
	putname(filename);
	return error;
}
