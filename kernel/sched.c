/*
 *  linux/kernel/sched.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1996-04-21	Modified by Ulrich Windl to make NTP work
 *  1996-12-23  Modified by Dave Grothe to fix bugs in semaphores and
 *              make semaphores SMP safe
 *  1997-01-28  Modified by Finn Arne Gangstad to make timers scale better.
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid()), which just extract a field from
 * current-task
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/fdreg.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/ptrace.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/tqueue.h>
#include <linux/resource.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>

#include <linux/timex.h>

/*
 * kernel variables
 */

int securelevel = 0;			/* system security level */

long tick = (1000000 + HZ/2) / HZ;	/* timer interrupt period */
volatile struct timeval xtime;		/* The current time */
int tickadj = 500/HZ;			/* microsecs */

DECLARE_TASK_QUEUE(tq_timer);
DECLARE_TASK_QUEUE(tq_immediate);
DECLARE_TASK_QUEUE(tq_scheduler);

/*
 * phase-lock loop variables
 */
/* TIME_ERROR prevents overwriting the CMOS clock */
int time_state = TIME_ERROR;	/* clock synchronization status */
int time_status = STA_UNSYNC;	/* clock status bits */
long time_offset = 0;		/* time adjustment (us) */
long time_constant = 2;		/* pll time constant */
long time_tolerance = MAXFREQ;	/* frequency tolerance (ppm) */
long time_precision = 1;	/* clock precision (us) */
long time_maxerror = MAXPHASE;	/* maximum error (us) */
long time_esterror = MAXPHASE;	/* estimated error (us) */
long time_phase = 0;		/* phase offset (scaled us) */
long time_freq = ((1000000 + HZ/2) % HZ - HZ/2) << SHIFT_USEC;	/* frequency offset (scaled ppm) */
long time_adj = 0;		/* tick adjust (scaled 1 / HZ) */
long time_reftime = 0;		/* time at last adjustment (s) */

long time_adjust = 0;
long time_adjust_step = 0;

int need_resched = 0;
unsigned long event = 0;

extern int _setitimer(int, struct itimerval *, struct itimerval *);
unsigned int * prof_buffer = NULL;
unsigned long prof_len = 0;
unsigned long prof_shift = 0;

#define _S(nr) (1<<((nr)-1))

extern void mem_use(void);

static unsigned long init_kernel_stack[1024] = { STACK_MAGIC, };
unsigned long init_user_stack[1024] = { STACK_MAGIC, };
static struct vm_area_struct init_mmap = INIT_MMAP;
static struct fs_struct init_fs = INIT_FS;
static struct files_struct init_files = INIT_FILES;
static struct signal_struct init_signals = INIT_SIGNALS;

struct mm_struct init_mm = INIT_MM;
struct task_struct init_task = INIT_TASK;

unsigned long volatile jiffies=0;

/*
 *	Init task must be ok at boot for the ix86 as we will check its signals
 *	via the SMP irq return path.
 */
 
struct task_struct *current_set[NR_CPUS] = {&init_task, };
struct task_struct *last_task_used_math = NULL;

struct task_struct * task[NR_TASKS] = {&init_task, };

struct kernel_stat kstat = { 0 };

static inline void add_to_runqueue(struct task_struct * p)
{
#if 1	/* sanity tests */
	if (p->next_run || p->prev_run) {
		printk("task already on run-queue\n");
		return;
	}
#endif
	if (p->counter > current->counter + 3)
		need_resched = 1;
	nr_running++;
	(p->prev_run = init_task.prev_run)->next_run = p;
	p->next_run = &init_task;
	init_task.prev_run = p;
#ifdef __SMP__
	/* this is safe only if called with cli()*/
	inc_smp_counter(&smp_process_available);
	if ((0!=p->pid) && smp_threads_ready)
	{
		int i;
		for (i=0;i<smp_num_cpus;i++)
		{
			if (0==current_set[cpu_logical_map[i]]->pid) 
			{
				smp_message_pass(cpu_logical_map[i], MSG_RESCHEDULE, 0L, 0);
				break;
			}
		}
	}
#endif
}

static inline void del_from_runqueue(struct task_struct * p)
{
	struct task_struct *next = p->next_run;
	struct task_struct *prev = p->prev_run;

#if 1	/* sanity tests */
	if (!next || !prev) {
		printk("task not on run-queue\n");
		return;
	}
#endif
	if (p == &init_task) {
		static int nr = 0;
		if (nr < 5) {
			nr++;
			printk("idle task may not sleep\n");
		}
		return;
	}
	nr_running--;
	next->prev_run = prev;
	prev->next_run = next;
	p->next_run = NULL;
	p->prev_run = NULL;
}

static inline void move_last_runqueue(struct task_struct * p)
{
	struct task_struct *next = p->next_run;
	struct task_struct *prev = p->prev_run;

	/* remove from list */
	next->prev_run = prev;
	prev->next_run = next;
	/* add back to list */
	p->next_run = &init_task;
	prev = init_task.prev_run;
	init_task.prev_run = p;
	p->prev_run = prev;
	prev->next_run = p;
}

/*
 * Wake up a process. Put it on the run-queue if it's not
 * already there.  The "current" process is always on the
 * run-queue (except when the actual re-schedule is in
 * progress), and as such you're allowed to do the simpler
 * "current->state = TASK_RUNNING" to mark yourself runnable
 * without the overhead of this.
 */
inline void wake_up_process(struct task_struct * p)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	p->state = TASK_RUNNING;
	if (!p->next_run)
		add_to_runqueue(p);
	restore_flags(flags);
}

static void process_timeout(unsigned long __data)
{
	struct task_struct * p = (struct task_struct *) __data;

	p->timeout = 0;
	wake_up_process(p);
}

/*
 * This is the function that decides how desirable a process is..
 * You can weigh different processes against each other depending
 * on what CPU they've run on lately etc to try to handle cache
 * and TLB miss penalties.
 *
 * Return values:
 *	 -1000: never select this
 *	     0: out of time, recalculate counters (but it might still be
 *		selected)
 *	   +ve: "goodness" value (the larger, the better)
 *	 +1000: realtime process, select this.
 */
static inline int goodness(struct task_struct * p, struct task_struct * prev, int this_cpu)
{
	int weight;

#ifdef __SMP__	
	/* We are not permitted to run a task someone else is running */
	if (p->processor != NO_PROC_ID)
		return -1000;
#ifdef PAST_2_0		
	/* This process is locked to a processor group */
	if (p->processor_mask && !(p->processor_mask & (1<<this_cpu))
		return -1000;
#endif		
#endif

	/*
	 * Realtime process, select the first one on the
	 * runqueue (taking priorities within processes
	 * into account).
	 */
	if (p->policy != SCHED_OTHER)
		return 1000 + p->rt_priority;

	/*
	 * Give the process a first-approximation goodness value
	 * according to the number of clock-ticks it has left.
	 *
	 * Don't do any other calculations if the time slice is
	 * over..
	 */
	weight = p->counter;
	if (weight) {
			
#ifdef __SMP__
		/* Give a largish advantage to the same processor...   */
		/* (this is equivalent to penalizing other processors) */
		if (p->last_processor == this_cpu)
			weight += PROC_CHANGE_PENALTY;
#endif

		/* .. and a slight advantage to the current process */
		if (p == prev)
			weight += 1;
	}

	return weight;
}

void allow_interrupts(void)
{
#if defined(__SMP__) && defined(__i386__)
/* UGH UGH UGH. Damn broken IRQ handling test-fix for x86.. */
	int this_cpu=smp_processor_id();
	int lock_depth = current_set[this_cpu]->lock_depth;
	if (lock_depth) {
		cli();
		current_set[this_cpu]->lock_depth = 0;
		active_kernel_processor = NO_PROC_ID;
		__asm__ __volatile__("lock ; btrl    $0, kernel_flag");
		sti();
		/* interrupts should work here */
		lock_kernel();
		current_set[this_cpu]->lock_depth += lock_depth-1;
	}
#endif
}		

/*
 *  'schedule()' is the scheduler function. It's a very simple and nice
 * scheduler: it's not perfect, but certainly works for most things.
 *
 * The goto is "interesting".
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */
asmlinkage void schedule(void)
{
	int c;
	struct task_struct * p;
	struct task_struct * prev, * next;
	unsigned long timeout = 0;
	int this_cpu=smp_processor_id();

/* check alarm, wake up any interruptible tasks that have got a signal */
	allow_interrupts();
	lock_kernel();
	if (intr_count)
		goto scheduling_in_interrupt;

	if (bh_active & bh_mask)
		do_bottom_half();

	run_task_queue(&tq_scheduler);

	need_resched = 0;
	prev = current;
	cli();
	/* move an exhausted RR process to be last.. */
	if (!prev->counter && prev->policy == SCHED_RR) {
		prev->counter = prev->priority;
		move_last_runqueue(prev);
	}
	switch (prev->state) {
		case TASK_INTERRUPTIBLE:
			if (prev->signal & ~prev->blocked)
				goto makerunnable;
			timeout = prev->timeout;
			if (timeout && (timeout <= jiffies)) {
				prev->timeout = 0;
				timeout = 0;
		makerunnable:
				prev->state = TASK_RUNNING;
				break;
			}
		default:
			del_from_runqueue(prev);
		case TASK_RUNNING:
	}
	p = init_task.next_run;
	sti();
	
#ifdef __SMP__
	/*
	 *	This is safe as we do not permit re-entry of schedule()
	 */
	prev->processor = NO_PROC_ID;
#define idle_task (task[cpu_number_map[this_cpu]])
#else
#define idle_task (&init_task)
#endif	

/*
 * Note! there may appear new tasks on the run-queue during this, as
 * interrupts are enabled. However, they will be put on front of the
 * list, so our list starting at "p" is essentially fixed.
 */
/* this is the scheduler proper: */
	c = -1000;
	next = idle_task;
	while (p != &init_task) {
		int weight = goodness(p, prev, this_cpu);
		if (weight > c)
			c = weight, next = p;
		p = p->next_run;
	}

	/* if all runnable processes have "counter == 0", re-calculate counters */
	if (!c) {
		for_each_task(p)
			p->counter = (p->counter >> 1) + p->priority;
	}
#ifdef __SMP__
	/*
	 *	Allocate process to CPU
	 */
	 
	 next->processor = this_cpu;
	 next->last_processor = this_cpu;
#endif	 
#ifdef __SMP_PROF__ 
	/* mark processor running an idle thread */
	if (0==next->pid)
		set_bit(this_cpu,&smp_idle_map);
	else
		clear_bit(this_cpu,&smp_idle_map);
#endif
	if (prev != next) {
		struct timer_list timer;

		kstat.context_swtch++;
		if (timeout) {
			init_timer(&timer);
			timer.expires = timeout;
			timer.data = (unsigned long) prev;
			timer.function = process_timeout;
			add_timer(&timer);
		}
		get_mmu_context(next);
		switch_to(prev,next);
		if (timeout)
			del_timer(&timer);
	}
	goto out;

scheduling_in_interrupt:
	printk("Aiee: scheduling in interrupt %p\n",
		__builtin_return_address(0));
out:
	unlock_kernel();
}

#ifndef __alpha__

/*
 * For backwards compatibility?  This can be done in libc so Alpha
 * and all newer ports shouldn't need it.
 */
asmlinkage int sys_pause(void)
{
	lock_kernel();
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	unlock_kernel();
	return -ERESTARTNOHAND;
}

#endif

/*
 * wake_up doesn't wake up stopped processes - they have to be awakened
 * with signals or similar.
 *
 * Note that this doesn't need cli-sti pairs: interrupts may not change
 * the wait-queue structures directly, but only call wake_up() to wake
 * a process. The process itself must remove the queue once it has woken.
 */
void wake_up(struct wait_queue **q)
{
	struct wait_queue *next;
	struct wait_queue *head;

	if (!q || !(next = *q))
		return;
	head = WAIT_QUEUE_HEAD(q);
	while (next != head) {
		struct task_struct *p = next->task;
		next = next->next;
		if (p != NULL) {
			if ((p->state == TASK_UNINTERRUPTIBLE) ||
			    (p->state == TASK_INTERRUPTIBLE))
				wake_up_process(p);
		}
		if (!next)
			goto bad;
	}
	return;
bad:
	printk("wait_queue is bad (eip = %p)\n",
		__builtin_return_address(0));
	printk("        q = %p\n",q);
	printk("       *q = %p\n",*q);
}

void wake_up_interruptible(struct wait_queue **q)
{
	struct wait_queue *next;
	struct wait_queue *head;

	if (!q || !(next = *q))
		return;
	head = WAIT_QUEUE_HEAD(q);
	while (next != head) {
		struct task_struct *p = next->task;
		next = next->next;
		if (p != NULL) {
			if (p->state == TASK_INTERRUPTIBLE)
				wake_up_process(p);
		}
		if (!next)
			goto bad;
	}
	return;
bad:
	printk("wait_queue is bad (eip = %p)\n",
		__builtin_return_address(0));
	printk("        q = %p\n",q);
	printk("       *q = %p\n",*q);
}


/*
 * Semaphores are implemented using a two-way counter:
 * The "count" variable is decremented for each process
 * that tries to sleep, while the "waking" variable is
 * incremented when the "up()" code goes to wake up waiting
 * processes.
 *
 * Notably, the inline "up()" and "down()" functions can
 * efficiently test if they need to do any extra work (up
 * needs to do something only if count was negative before
 * the increment operation.
 *
 * This routine must execute atomically.
 */
static inline int waking_non_zero(struct semaphore *sem)
{
	int ret;
	long flags;

	save_flags(flags);
	cli();

	ret = 0;
	if (sem->waking > 0) {
		sem->waking--;
		ret = 1;
	}

	restore_flags(flags);
	return ret;
}

/*
 * When __up() is called, the count was negative before
 * incrementing it, and we need to wake up somebody.
 *
 * This routine adds one to the count of processes that need to
 * wake up and exit.  ALL waiting processes actually wake up but
 * only the one that gets to the "waking" field first will gate
 * through and acquire the semaphore.  The others will go back
 * to sleep.
 *
 * Note that these functions are only called when there is
 * contention on the lock, and as such all this is the
 * "non-critical" part of the whole semaphore business. The
 * critical part is the inline stuff in <asm/semaphore.h>
 * where we want to avoid any extra jumps and calls.
 */
void __up(struct semaphore *sem)
{
	atomic_inc(&sem->waking);
	wake_up(&sem->wait);
}

/*
 * Perform the "down" function.  Return zero for semaphore acquired,
 * return negative for signalled out of the function.
 *
 * If called from __down, the return is ignored and the wait loop is
 * not interruptible.  This means that a task waiting on a semaphore
 * using "down()" cannot be killed until someone does an "up()" on
 * the semaphore.
 *
 * If called from __down_interruptible, the return value gets checked
 * upon return.  If the return value is negative then the task continues
 * with the negative value in the return register (it can be tested by
 * the caller).
 *
 * Either form may be used in conjunction with "up()".
 *
 */
static inline int __do_down(struct semaphore * sem, int task_state)
{
	struct task_struct *tsk = current;
	struct wait_queue wait = { tsk, NULL };
	int		  ret = 0;

	tsk->state = task_state;
	add_wait_queue(&sem->wait, &wait);

	/*
	 * Ok, we're set up.  sem->count is known to be less than zero
	 * so we must wait.
	 *
	 * We can let go the lock for purposes of waiting.
	 * We re-acquire it after awaking so as to protect
	 * all semaphore operations.
	 *
	 * If "up()" is called before we call waking_non_zero() then
	 * we will catch it right away.  If it is called later then
	 * we will have to go through a wakeup cycle to catch it.
	 *
	 * Multiple waiters contend for the semaphore lock to see
	 * who gets to gate through and who has to wait some more.
	 */
	for (;;) {
		if (waking_non_zero(sem))	/* are we waking up?  */
			break;			/* yes, exit loop */

		if (   task_state == TASK_INTERRUPTIBLE
		    && (tsk->signal & ~tsk->blocked)	/* signalled */
		   ) {
			ret = -EINTR;			/* interrupted */
			atomic_inc(&sem->count);	/* give up on down operation */
			break;
		}

		schedule();
		tsk->state = task_state;
	}

	tsk->state = TASK_RUNNING;
	remove_wait_queue(&sem->wait, &wait);
	return ret;
}

void __down(struct semaphore * sem)
{
	__do_down(sem,TASK_UNINTERRUPTIBLE);
}

int __down_interruptible(struct semaphore * sem)
{
	return __do_down(sem,TASK_INTERRUPTIBLE);
}


static inline void __sleep_on(struct wait_queue **p, int state)
{
	unsigned long flags;
	struct wait_queue wait = { current, NULL };

	if (!p)
		return;
	if (current == task[0])
		panic("task[0] trying to sleep");
	current->state = state;
	save_flags(flags);
	cli();
	__add_wait_queue(p, &wait);
	sti();
	schedule();
	cli();
	__remove_wait_queue(p, &wait);
	restore_flags(flags);
}

void interruptible_sleep_on(struct wait_queue **p)
{
	__sleep_on(p,TASK_INTERRUPTIBLE);
}

void sleep_on(struct wait_queue **p)
{
	__sleep_on(p,TASK_UNINTERRUPTIBLE);
}


#define TVN_BITS 6
#define TVR_BITS 8
#define TVN_SIZE (1 << TVN_BITS)
#define TVR_SIZE (1 << TVR_BITS)
#define TVN_MASK (TVN_SIZE - 1)
#define TVR_MASK (TVR_SIZE - 1)

#define SLOW_BUT_DEBUGGING_TIMERS 0

struct timer_vec {
        int index;
        struct timer_list *vec[TVN_SIZE];
};

struct timer_vec_root {
        int index;
        struct timer_list *vec[TVR_SIZE];
};

static struct timer_vec tv5 = { 0 };
static struct timer_vec tv4 = { 0 };
static struct timer_vec tv3 = { 0 };
static struct timer_vec tv2 = { 0 };
static struct timer_vec_root tv1 = { 0 };

static struct timer_vec * const tvecs[] = {
	(struct timer_vec *)&tv1, &tv2, &tv3, &tv4, &tv5
};

#define NOOF_TVECS (sizeof(tvecs) / sizeof(tvecs[0]))

static unsigned long timer_jiffies = 0;

static inline void insert_timer(struct timer_list *timer,
				struct timer_list **vec, int idx)
{
	if ((timer->next = vec[idx]))
		vec[idx]->prev = timer;
	vec[idx] = timer;
	timer->prev = (struct timer_list *)&vec[idx];
}

static inline void internal_add_timer(struct timer_list *timer)
{
	/*
	 * must be cli-ed when calling this
	 */
	unsigned long expires = timer->expires;
	unsigned long idx = expires - timer_jiffies;

	if (idx < TVR_SIZE) {
		int i = expires & TVR_MASK;
		insert_timer(timer, tv1.vec, i);
	} else if (idx < 1 << (TVR_BITS + TVN_BITS)) {
		int i = (expires >> TVR_BITS) & TVN_MASK;
		insert_timer(timer, tv2.vec, i);
	} else if (idx < 1 << (TVR_BITS + 2 * TVN_BITS)) {
		int i = (expires >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
		insert_timer(timer, tv3.vec, i);
	} else if (idx < 1 << (TVR_BITS + 3 * TVN_BITS)) {
		int i = (expires >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
		insert_timer(timer, tv4.vec, i);
	} else if ((idx + 1UL) == 0UL) {
		/* can happen if you add a timer with expires == jiffies */
		insert_timer(timer, tv1.vec, tv1.index);
	} else if (idx < 0xffffffffUL) {
		int i = (expires >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;
		insert_timer(timer, tv5.vec, i);
	} else {
		/* Can only get here on architectures with 64-bit jiffies */
		timer->next = timer->prev = timer;
	}
}

void add_timer(struct timer_list *timer)
{
	unsigned long flags;
	save_flags(flags);
	cli();
#if SLOW_BUT_DEBUGGING_TIMERS
        if (timer->next || timer->prev) {
                printk("add_timer() called with non-zero list from %p\n",
		       __builtin_return_address(0));
		goto out;
        }
#endif
	internal_add_timer(timer);
#if SLOW_BUT_DEBUGGING_TIMERS
out:
#endif
	restore_flags(flags);
}

static inline int detach_timer(struct timer_list *timer)
{
	int ret = 0;
	struct timer_list *next, *prev;
	next = timer->next;
	prev = timer->prev;
	if (next) {
		next->prev = prev;
	}
	if (prev) {
		ret = 1;
		prev->next = next;
	}
	return ret;
}


int del_timer(struct timer_list * timer)
{
	int ret;
	unsigned long flags;
	save_flags(flags);
	cli();
	ret = detach_timer(timer);
	timer->next = timer->prev = 0;
	restore_flags(flags);
	return ret;
}

static inline void cascade_timers(struct timer_vec *tv)
{
        /* cascade all the timers from tv up one level */
        struct timer_list *timer;
        timer = tv->vec[tv->index];
        /*
         * We are removing _all_ timers from the list, so we don't  have to
         * detach them individually, just clear the list afterwards.
         */
        while (timer) {
                struct timer_list *tmp = timer;
                timer = timer->next;
                internal_add_timer(tmp);
        }
        tv->vec[tv->index] = NULL;
        tv->index = (tv->index + 1) & TVN_MASK;
}

static inline void run_timer_list(void)
{
	cli();
	while ((long)(jiffies - timer_jiffies) >= 0) {
		struct timer_list *timer;
		if (!tv1.index) {
			int n = 1;
			do {
				cascade_timers(tvecs[n]);
			} while (tvecs[n]->index == 1 && ++n < NOOF_TVECS);
		}
		while ((timer = tv1.vec[tv1.index])) {
			void (*fn)(unsigned long) = timer->function;
			unsigned long data = timer->data;
			detach_timer(timer);
			timer->next = timer->prev = NULL;
			sti();
			fn(data);
			cli();
		}
		++timer_jiffies; 
		tv1.index = (tv1.index + 1) & TVR_MASK;
	}
	sti();
}


static inline void run_old_timers(void)
{
	struct timer_struct *tp;
	unsigned long mask;

	for (mask = 1, tp = timer_table+0 ; mask ; tp++,mask += mask) {
		if (mask > timer_active)
			break;
		if (!(mask & timer_active))
			continue;
		if (tp->expires > jiffies)
			continue;
		timer_active &= ~mask;
		tp->fn();
		sti();
	}
}

void tqueue_bh(void)
{
	run_task_queue(&tq_timer);
}

void immediate_bh(void)
{
	run_task_queue(&tq_immediate);
}

unsigned long timer_active = 0;
struct timer_struct timer_table[32];

/*
 * Hmm.. Changed this, as the GNU make sources (load.c) seems to
 * imply that avenrun[] is the standard name for this kind of thing.
 * Nothing else seems to be standardized: the fractional size etc
 * all seem to differ on different machines.
 */
unsigned long avenrun[3] = { 0,0,0 };

/*
 * Nr of active tasks - counted in fixed-point numbers
 */
static unsigned long count_active_tasks(void)
{
	struct task_struct **p;
	unsigned long nr = 0;

	for(p = &LAST_TASK; p > &FIRST_TASK; --p)
		if (*p && ((*p)->state == TASK_RUNNING ||
			   (*p)->state == TASK_UNINTERRUPTIBLE ||
			   (*p)->state == TASK_SWAPPING))
			nr += FIXED_1;
#ifdef __SMP__
	nr-=(smp_num_cpus-1)*FIXED_1;
#endif			
	return nr;
}

static inline void calc_load(unsigned long ticks)
{
	unsigned long active_tasks; /* fixed-point */
	static int count = LOAD_FREQ;

	count -= ticks;
	if (count < 0) {
		count += LOAD_FREQ;
		active_tasks = count_active_tasks();
		CALC_LOAD(avenrun[0], EXP_1, active_tasks);
		CALC_LOAD(avenrun[1], EXP_5, active_tasks);
		CALC_LOAD(avenrun[2], EXP_15, active_tasks);
	}
}

/*
 * this routine handles the overflow of the microsecond field
 *
 * The tricky bits of code to handle the accurate clock support
 * were provided by Dave Mills (Mills@UDEL.EDU) of NTP fame.
 * They were originally developed for SUN and DEC kernels.
 * All the kudos should go to Dave for this stuff.
 *
 */
static void second_overflow(void)
{
    long ltemp;

    /* Bump the maxerror field */
    time_maxerror += time_tolerance >> SHIFT_USEC;
    if ( time_maxerror > MAXPHASE )
        time_maxerror = MAXPHASE;

    /*
     * Leap second processing. If in leap-insert state at
     * the end of the day, the system clock is set back one
     * second; if in leap-delete state, the system clock is
     * set ahead one second. The microtime() routine or
     * external clock driver will insure that reported time
     * is always monotonic. The ugly divides should be
     * replaced.
     */
    switch (time_state) {

    case TIME_OK:
	if (time_status & STA_INS)
	    time_state = TIME_INS;
	else if (time_status & STA_DEL)
	    time_state = TIME_DEL;
	break;

    case TIME_INS:
	if (xtime.tv_sec % 86400 == 0) {
	    xtime.tv_sec--;
	    time_state = TIME_OOP;
	    printk("Clock: inserting leap second 23:59:60 UTC\n");
	}
	break;

    case TIME_DEL:
	if ((xtime.tv_sec + 1) % 86400 == 0) {
	    xtime.tv_sec++;
	    time_state = TIME_WAIT;
	    printk("Clock: deleting leap second 23:59:59 UTC\n");
	}
	break;

    case TIME_OOP:
	time_state = TIME_WAIT;
	break;

    case TIME_WAIT:
	if (!(time_status & (STA_INS | STA_DEL)))
	    time_state = TIME_OK;
    }

    /*
     * Compute the phase adjustment for the next second. In
     * PLL mode, the offset is reduced by a fixed factor
     * times the time constant. In FLL mode the offset is
     * used directly. In either mode, the maximum phase
     * adjustment for each second is clamped so as to spread
     * the adjustment over not more than the number of
     * seconds between updates.
     */
    if (time_offset < 0) {
	ltemp = -time_offset;
	if (!(time_status & STA_FLL))
	    ltemp >>= SHIFT_KG + time_constant;
	if (ltemp > (MAXPHASE / MINSEC) << SHIFT_UPDATE)
	    ltemp = (MAXPHASE / MINSEC) << SHIFT_UPDATE;
	time_offset += ltemp;
	time_adj = -ltemp << (SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE);
    } else {
	ltemp = time_offset;
	if (!(time_status & STA_FLL))
	    ltemp >>= SHIFT_KG + time_constant;
	if (ltemp > (MAXPHASE / MINSEC) << SHIFT_UPDATE)
	    ltemp = (MAXPHASE / MINSEC) << SHIFT_UPDATE;
	time_offset -= ltemp;
	time_adj = ltemp << (SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE);
    }

    /*
     * Compute the frequency estimate and additional phase
     * adjustment due to frequency error for the next
     * second. When the PPS signal is engaged, gnaw on the
     * watchdog counter and update the frequency computed by
     * the pll and the PPS signal.
     */
    pps_valid++;
    if (pps_valid == PPS_VALID) {
	pps_jitter = MAXTIME;
	pps_stabil = MAXFREQ;
	time_status &= ~(STA_PPSSIGNAL | STA_PPSJITTER |
			 STA_PPSWANDER | STA_PPSERROR);
    }
    ltemp = time_freq + pps_freq;
    if (ltemp < 0)
	time_adj -= -ltemp >>
	    (SHIFT_USEC + SHIFT_HZ - SHIFT_SCALE);
    else
	time_adj += ltemp >>
	    (SHIFT_USEC + SHIFT_HZ - SHIFT_SCALE);

#if HZ == 100
    /* compensate for (HZ==100) != 128. Add 25% to get 125; => only 3% error */
    if (time_adj < 0)
	time_adj -= -time_adj >> 2;
    else
	time_adj += time_adj >> 2;
#endif
}

/* in the NTP reference this is called "hardclock()" */
static void update_wall_time_one_tick(void)
{
	/*
	 * Advance the phase, once it gets to one microsecond, then
	 * advance the tick more.
	 */
	time_phase += time_adj;
	if (time_phase <= -FINEUSEC) {
		long ltemp = -time_phase >> SHIFT_SCALE;
		time_phase += ltemp << SHIFT_SCALE;
		xtime.tv_usec += tick + time_adjust_step - ltemp;
	}
	else if (time_phase >= FINEUSEC) {
		long ltemp = time_phase >> SHIFT_SCALE;
		time_phase -= ltemp << SHIFT_SCALE;
		xtime.tv_usec += tick + time_adjust_step + ltemp;
	} else
		xtime.tv_usec += tick + time_adjust_step;

	if (time_adjust) {
	    /* We are doing an adjtime thing. 
	     *
	     * Modify the value of the tick for next time.
	     * Note that a positive delta means we want the clock
	     * to run fast. This means that the tick should be bigger
	     *
	     * Limit the amount of the step for *next* tick to be
	     * in the range -tickadj .. +tickadj
	     */
	     if (time_adjust > tickadj)
		time_adjust_step = tickadj;
	     else if (time_adjust < -tickadj)
		time_adjust_step = -tickadj;
	     else
		time_adjust_step = time_adjust;
	     
	    /* Reduce by this step the amount of time left  */
	    time_adjust -= time_adjust_step;
	}
	else
	    time_adjust_step = 0;
}

/*
 * Using a loop looks inefficient, but "ticks" is
 * usually just one (we shouldn't be losing ticks,
 * we're doing this this way mainly for interrupt
 * latency reasons, not because we think we'll
 * have lots of lost timer ticks
 */
static void update_wall_time(unsigned long ticks)
{
	do {
		ticks--;
		update_wall_time_one_tick();
	} while (ticks);

	if (xtime.tv_usec >= 1000000) {
	    xtime.tv_usec -= 1000000;
	    xtime.tv_sec++;
	    second_overflow();
	}
}

static inline void do_process_times(struct task_struct *p,
	unsigned long user, unsigned long system)
{
	long psecs;

	p->utime += user;
	p->stime += system;

	psecs = (p->stime + p->utime) / HZ;
	if (psecs > p->rlim[RLIMIT_CPU].rlim_cur) {
		/* Send SIGXCPU every second.. */
		if (psecs * HZ == p->stime + p->utime)
			send_sig(SIGXCPU, p, 1);
		/* and SIGKILL when we go over max.. */
		if (psecs > p->rlim[RLIMIT_CPU].rlim_max)
			send_sig(SIGKILL, p, 1);
	}
}

static inline void do_it_virt(struct task_struct * p, unsigned long ticks)
{
	unsigned long it_virt = p->it_virt_value;

	if (it_virt) {
		if (it_virt <= ticks) {
			it_virt = ticks + p->it_virt_incr;
			send_sig(SIGVTALRM, p, 1);
		}
		p->it_virt_value = it_virt - ticks;
	}
}

static inline void do_it_prof(struct task_struct * p, unsigned long ticks)
{
	unsigned long it_prof = p->it_prof_value;

	if (it_prof) {
		if (it_prof <= ticks) {
			it_prof = ticks + p->it_prof_incr;
			send_sig(SIGPROF, p, 1);
		}
		p->it_prof_value = it_prof - ticks;
	}
}

static __inline__ void update_one_process(struct task_struct *p,
	unsigned long ticks, unsigned long user, unsigned long system)
{
	do_process_times(p, user, system);
	do_it_virt(p, user);
	do_it_prof(p, ticks);
}	

static void update_process_times(unsigned long ticks, unsigned long system)
{
#ifndef  __SMP__
	struct task_struct * p = current;
	unsigned long user = ticks - system;
	if (p->pid) {
		p->counter -= ticks;
		if (p->counter < 0) {
			p->counter = 0;
			need_resched = 1;
		}
		if (p->priority < DEF_PRIORITY)
			kstat.cpu_nice += user;
		else
			kstat.cpu_user += user;
		kstat.cpu_system += system;
	}
	update_one_process(p, ticks, user, system);
#else
	int cpu,j;
	cpu = smp_processor_id();
	for (j=0;j<smp_num_cpus;j++)
	{
		int i = cpu_logical_map[j];
		struct task_struct *p;
		
#ifdef __SMP_PROF__
		if (test_bit(i,&smp_idle_map)) 
			smp_idle_count[i]++;
#endif
		p = current_set[i];
		/*
		 * Do we have a real process?
		 */
		if (p->pid) {
			/* assume user-mode process */
			unsigned long utime = ticks;
			unsigned long stime = 0;
			if (cpu == i) {
				utime = ticks-system;
				stime = system;
			} else if (smp_proc_in_lock[j]) {
				utime = 0;
				stime = ticks;
			}

			update_one_process(p, ticks, utime, stime);

			if (p->priority < DEF_PRIORITY)
				kstat.cpu_nice += utime;
			else
				kstat.cpu_user += utime;
			kstat.cpu_system += stime;

			p->counter -= ticks;
			if (p->counter >= 0)
				continue;
			p->counter = 0;
		} else {
			/*
			 * Idle processor found, do we have anything
			 * we could run?
			 */
			if (!(read_smp_counter(&smp_process_available)))
				continue;
		}
		/* Ok, we should reschedule, do the magic */
		cli();
		if (i==cpu) {
			need_resched = 1;
		} else {
			smp_message_pass(i, MSG_RESCHEDULE, 0L, 0);
		}
		sti();
	}
#endif
}

volatile unsigned long lost_ticks = 0;
static unsigned long lost_ticks_system = 0;

static inline void update_times(void)
{
	unsigned long ticks;
	unsigned long flags;

	save_flags(flags);
	cli();

	ticks = lost_ticks;
	lost_ticks = 0;

	if (ticks) {
		unsigned long system;
		system = xchg(&lost_ticks_system, 0);

		calc_load(ticks);
		update_wall_time(ticks);
		restore_flags(flags);
		
		update_process_times(ticks, system);

	} else
		restore_flags(flags);
}

static void timer_bh(void)
{
	update_times();
	run_old_timers();
	run_timer_list();
}

void do_timer(struct pt_regs * regs)
{
	(*(unsigned long *)&jiffies)++;
	lost_ticks++;
	mark_bh(TIMER_BH);
	if (!user_mode(regs)) {
		lost_ticks_system++;
		if (prof_buffer && current->pid) {
			extern int _stext;
			unsigned long ip = instruction_pointer(regs);
			ip -= (unsigned long) &_stext;
			ip >>= prof_shift;
			if (ip < prof_len)
				prof_buffer[ip]++;
		}
	}
	if (tq_timer)
		mark_bh(TQUEUE_BH);
}

#ifndef __alpha__

/*
 * For backwards compatibility?  This can be done in libc so Alpha
 * and all newer ports shouldn't need it.
 */
asmlinkage unsigned int sys_alarm(unsigned int seconds)
{
	struct itimerval it_new, it_old;
	unsigned int oldalarm;

	lock_kernel();
	it_new.it_interval.tv_sec = it_new.it_interval.tv_usec = 0;
	it_new.it_value.tv_sec = seconds;
	it_new.it_value.tv_usec = 0;
	_setitimer(ITIMER_REAL, &it_new, &it_old);
	oldalarm = it_old.it_value.tv_sec;
	/* ehhh.. We can't return 0 if we have an alarm pending.. */
	/* And we'd better return too much than too little anyway */
	if (it_old.it_value.tv_usec)
		oldalarm++;
	unlock_kernel();
	return oldalarm;
}

/*
 * The Alpha uses getxpid, getxuid, and getxgid instead.  Maybe this
 * should be moved into arch/i386 instead?
 */
 
asmlinkage int sys_getpid(void)
{
	/* This is SMP safe - current->pid doesnt change */
	return current->pid;
}

/*
 * This is not strictly SMP safe: p_opptr could change
 * from under us. However, rather than getting any lock
 * we can use an optimistic algorithm: get the parent
 * pid, and go back and check that the parent is still
 * the same. If it has changed (which is extremely unlikely
 * indeed), we just try again..
 *
 * NOTE! This depends on the fact that even if we _do_
 * get an old value of "parent", we can happily dereference
 * the pointer: we just can't necessarily trust the result
 * until we know that the parent pointer is valid.
 *
 * The "mb()" macro is a memory barrier - a synchronizing
 * event. It also makes sure that gcc doesn't optimize
 * away the necessary memory references.. The barrier doesn't
 * have to have all that strong semantics: on x86 we don't
 * really require a synchronizing instruction, for example.
 * The barrier is more important for code generation than
 * for any real memory ordering semantics (even if there is
 * a small window for a race, using the old pointer is
 * harmless for a while).
 */
asmlinkage int sys_getppid(void)
{
	int pid;
	struct task_struct * me = current;
	struct task_struct * parent;

	parent = me->p_opptr;
	for (;;) {
		pid = parent->pid;
#if __SMP__
{
		struct task_struct *old = parent;
		mb();
		parent = me->p_opptr;
		if (old != parent)
			continue;
}
#endif
		break;
	}
	return pid;
}

asmlinkage int sys_getuid(void)
{
	/* Only we change this so SMP safe */
	return current->uid;
}

asmlinkage int sys_geteuid(void)
{
	/* Only we change this so SMP safe */
	return current->euid;
}

asmlinkage int sys_getgid(void)
{
	/* Only we change this so SMP safe */
	return current->gid;
}

asmlinkage int sys_getegid(void)
{
	/* Only we change this so SMP safe */
	return  current->egid;
}

/*
 * This has been replaced by sys_setpriority.  Maybe it should be
 * moved into the arch dependent tree for those ports that require
 * it for backward compatibility?
 */

asmlinkage int sys_nice(int increment)
{
	unsigned long newprio;
	int increase = 0;

	/*
	 *	Setpriority might change our priority at the same moment.
	 *	We don't have to worry. Conceptually one call occurs first
	 *	and we have a single winner.
	 */
	 
	newprio = increment;
	if (increment < 0) {
		if (!suser())
			return -EPERM;
		newprio = -increment;
		increase = 1;
	}

	if (newprio > 40)
		newprio = 40;
	/*
	 * do a "normalization" of the priority (traditionally
	 * unix nice values are -20..20, linux doesn't really
	 * use that kind of thing, but uses the length of the
	 * timeslice instead (default 150 msec). The rounding is
	 * why we want to avoid negative values.
	 */
	newprio = (newprio * DEF_PRIORITY + 10) / 20;
	increment = newprio;
	if (increase)
		increment = -increment;
	/*
	 *	Current->priority can change between this point
	 *	and the assignment. We are assigning not doing add/subs
	 *	so thats ok. Conceptually a process might just instantaneously
	 *	read the value we stomp over. I don't think that is an issue
	 *	unless posix makes it one. If so we can loop on changes
	 *	to current->priority.
	 */
	newprio = current->priority - increment;
	if ((signed) newprio < 1)
		newprio = 1;
	if (newprio > DEF_PRIORITY*2)
		newprio = DEF_PRIORITY*2;
	current->priority = newprio;
	return 0;
}

#endif

static struct task_struct *find_process_by_pid(pid_t pid)
{
	struct task_struct *p;

	p = current;
	if (pid) {
		for_each_task(p) {
			if (p->pid == pid)
				goto found;
		}
		p = NULL;
	}
found:
	return p;
}

static int setscheduler(pid_t pid, int policy, 
			struct sched_param *param)
{
	struct sched_param lp;
	struct task_struct *p;

	if (!param || pid < 0)
		return -EINVAL;

	if (copy_from_user(&lp, param, sizeof(struct sched_param)))
		return -EFAULT;

	p = find_process_by_pid(pid);
	if (!p)
		return -ESRCH;
			
	if (policy < 0)
		policy = p->policy;
	else if (policy != SCHED_FIFO && policy != SCHED_RR &&
		 policy != SCHED_OTHER)
		return -EINVAL;
	
	/*
	 * Valid priorities for SCHED_FIFO and SCHED_RR are 1..99, valid
	 * priority for SCHED_OTHER is 0.
	 */
	if (lp.sched_priority < 0 || lp.sched_priority > 99)
		return -EINVAL;
	if ((policy == SCHED_OTHER) != (lp.sched_priority == 0))
		return -EINVAL;

	if ((policy == SCHED_FIFO || policy == SCHED_RR) && !suser())
		return -EPERM;
	if ((current->euid != p->euid) && (current->euid != p->uid) &&
	    !suser())
		return -EPERM;

	p->policy = policy;
	p->rt_priority = lp.sched_priority;
	cli();
	if (p->next_run)
		move_last_runqueue(p);
	sti();
	schedule();

	return 0;
}

asmlinkage int sys_sched_setscheduler(pid_t pid, int policy, 
				      struct sched_param *param)
{
	int ret;

	lock_kernel();
	ret = setscheduler(pid, policy, param);
	unlock_kernel();
	return ret;
}

asmlinkage int sys_sched_setparam(pid_t pid, struct sched_param *param)
{
	int ret;

	lock_kernel();
	ret = setscheduler(pid, -1, param);
	unlock_kernel();
	return ret;
}

asmlinkage int sys_sched_getscheduler(pid_t pid)
{
	struct task_struct *p;
	int ret = -EINVAL;

	lock_kernel();
	if (pid < 0)
		goto out;

	p = find_process_by_pid(pid);
	ret = -ESRCH;
	if (!p)
		goto out;
			
	ret = p->policy;
out:
	unlock_kernel();
	return ret;
}

asmlinkage int sys_sched_getparam(pid_t pid, struct sched_param *param)
{
	struct task_struct *p;
	struct sched_param lp;
	int ret = -EINVAL;

	lock_kernel();
	if (!param || pid < 0)
		goto out;

	p = find_process_by_pid(pid);
	ret = -ESRCH;
	if (!p)
		goto out;

	lp.sched_priority = p->rt_priority;
	ret = copy_to_user(param, &lp, sizeof(struct sched_param)) ? -EFAULT : 0;
out:
	unlock_kernel();
	return ret;
}

asmlinkage int sys_sched_yield(void)
{
	lock_kernel();
	cli();
	move_last_runqueue(current);
	sti();
	unlock_kernel();
	return 0;
}

asmlinkage int sys_sched_get_priority_max(int policy)
{
	int ret = -EINVAL;

	lock_kernel();
	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = 99;
		break;
	case SCHED_OTHER:
		ret = 0;
		break;
	}
	unlock_kernel();
	return ret;
}

asmlinkage int sys_sched_get_priority_min(int policy)
{
	int ret = -EINVAL;

	lock_kernel();
	switch (policy) {
	case SCHED_FIFO:
	case SCHED_RR:
		ret = 1;
		break;
	case SCHED_OTHER:
		ret = 0;
	}
	unlock_kernel();
	return ret;
}

asmlinkage int sys_sched_rr_get_interval(pid_t pid, struct timespec *interval)
{
	struct timespec t;
	int ret;

	lock_kernel();
	t.tv_sec = 0;
	t.tv_nsec = 0;            /* <-- Linus, please fill correct value in here */
	ret = -ENOSYS; goto out;  /* and then delete this line. Thanks!           */
	ret = copy_to_user(interval, &t, sizeof(struct timespec)) ? -EFAULT : 0;
out:
	unlock_kernel();
	return ret;
}

/*
 * change timeval to jiffies, trying to avoid the 
 * most obvious overflows..
 */
static unsigned long timespectojiffies(struct timespec *value)
{
	unsigned long sec = (unsigned) value->tv_sec;
	long nsec = value->tv_nsec;

	if (sec > (LONG_MAX / HZ))
		return LONG_MAX;
	nsec += 1000000000L / HZ - 1;
	nsec /= 1000000000L / HZ;
	return HZ * sec + nsec;
}

static void jiffiestotimespec(unsigned long jiffies, struct timespec *value)
{
	value->tv_nsec = (jiffies % HZ) * (1000000000L / HZ);
	value->tv_sec = jiffies / HZ;
	return;
}

asmlinkage int sys_nanosleep(struct timespec *rqtp, struct timespec *rmtp)
{
	int error = -EFAULT;
	struct timespec t;
	unsigned long expire;

	lock_kernel();
	if(copy_from_user(&t, rqtp, sizeof(struct timespec)))
		goto out;

	error = -EINVAL;
	if (t.tv_nsec >= 1000000000L || t.tv_nsec < 0 || t.tv_sec < 0)
		goto out;

	if (t.tv_sec == 0 && t.tv_nsec <= 2000000L &&
	    current->policy != SCHED_OTHER) {
		/*
		 * Short delay requests up to 2 ms will be handled with
		 * high precision by a busy wait for all real-time processes.
		 */
		udelay((t.tv_nsec + 999) / 1000);
		error = 0;
		goto out;
	}

	expire = timespectojiffies(&t) + (t.tv_sec || t.tv_nsec) + jiffies;
	current->timeout = expire;
	current->state = TASK_INTERRUPTIBLE;
	schedule();

	error = 0;
	if (expire > jiffies) {
		if (rmtp) {
			jiffiestotimespec(expire - jiffies -
					  (expire > jiffies + 1), &t);
			error = -EFAULT;
			if (copy_to_user(rmtp, &t, sizeof(struct timespec)))
				goto out;
		}
		error = -EINTR;
	}
out:
	unlock_kernel();
	return error;
}

static void show_task(int nr,struct task_struct * p)
{
	unsigned long free;
	static const char * stat_nam[] = { "R", "S", "D", "Z", "T", "W" };

	printk("%-8s %3d ", p->comm, (p == current) ? -nr : nr);
	if (((unsigned) p->state) < sizeof(stat_nam)/sizeof(char *))
		printk(stat_nam[p->state]);
	else
		printk(" ");
#if ((~0UL) == 0xffffffff)
	if (p == current)
		printk(" current  ");
	else
		printk(" %08lX ", thread_saved_pc(&p->tss));
#else
	if (p == current)
		printk("   current task   ");
	else
		printk(" %016lx ", thread_saved_pc(&p->tss));
#endif
	for (free = 1; free < PAGE_SIZE/sizeof(long) ; free++) {
		if (((unsigned long *)p->kernel_stack_page)[free])
			break;
	}
	printk("%5lu %5d %6d ", free*sizeof(long), p->pid, p->p_pptr->pid);
	if (p->p_cptr)
		printk("%5d ", p->p_cptr->pid);
	else
		printk("      ");
	if (p->p_ysptr)
		printk("%7d", p->p_ysptr->pid);
	else
		printk("       ");
	if (p->p_osptr)
		printk(" %5d\n", p->p_osptr->pid);
	else
		printk("\n");
}

void show_state(void)
{
	int i;

#if ((~0UL) == 0xffffffff)
	printk("\n"
	       "                         free                        sibling\n");
	printk("  task             PC    stack   pid father child younger older\n");
#else
	printk("\n"
	       "                                 free                        sibling\n");
	printk("  task                 PC        stack   pid father child younger older\n");
#endif
	for (i=0 ; i<NR_TASKS ; i++)
		if (task[i])
			show_task(i,task[i]);
}

void sched_init(void)
{
	/*
	 *	We have to do a little magic to get the first
	 *	process right in SMP mode.
	 */
	int cpu=smp_processor_id();
#ifndef __SMP__
	current_set[cpu]=&init_task;
#else
	init_task.processor=cpu;
	for(cpu = 0; cpu < NR_CPUS; cpu++)
		current_set[cpu] = &init_task;
#endif
	init_bh(TIMER_BH, timer_bh);
	init_bh(TQUEUE_BH, tqueue_bh);
	init_bh(IMMEDIATE_BH, immediate_bh);
}
