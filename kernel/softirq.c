/*
 *	linux/kernel/softirq.c
 *
 *	Copyright (C) 1992 Linus Torvalds
 *
 * Fixed a disable_bh()/enable_bh() race (was causing a console lockup)
 * due bh_mask_count not atomic handling. Copyright (C) 1998  Andrea Arcangeli
 *
 * Rewritten. Old one was good in 2.2, but in 2.3 it was immoral. --ANK (990903)
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/tqueue.h>
#include <linux/percpu.h>
#include <linux/notifier.h>

/*
   - No shared variables, all the data are CPU local.
   - If a softirq needs serialization, let it serialize itself
     by its own spinlocks.
   - Even if softirq is serialized, only local cpu is marked for
     execution. Hence, we get something sort of weak cpu binding.
     Though it is still not clear, will it result in better locality
     or will not.

   Examples:
   - NET RX softirq. It is multithreaded and does not require
     any global serialization.
   - NET TX softirq. It kicks software netdevice queues, hence
     it is logically serialized per device, but this serialization
     is invisible to common code.
   - Tasklets: serialized wrt itself.
   - Bottom halves: globally serialized, grr...
 */

irq_cpustat_t irq_stat[NR_CPUS];

static struct softirq_action softirq_vec[32] __cacheline_aligned_in_smp;

/*
 * we cannot loop indefinitely here to avoid userspace starvation,
 * but we also don't want to introduce a worst case 1/HZ latency
 * to the pending events, so lets the scheduler to balance
 * the softirq load for us.
 */
static inline void wakeup_softirqd(unsigned cpu)
{
	struct task_struct * tsk = ksoftirqd_task(cpu);

	if (tsk && tsk->state != TASK_RUNNING)
		wake_up_process(tsk);
}

asmlinkage void do_softirq()
{
	__u32 pending;
	long flags;
	__u32 mask;
	int cpu;

	if (in_interrupt())
		return;

	local_irq_save(flags);
	cpu = smp_processor_id();

	pending = softirq_pending(cpu);

	if (pending) {
		struct softirq_action *h;

		mask = ~pending;
		local_bh_disable();
restart:
		/* Reset the pending bitmask before enabling irqs */
		softirq_pending(cpu) = 0;

		local_irq_enable();

		h = softirq_vec;

		do {
			if (pending & 1)
				h->action(h);
			h++;
			pending >>= 1;
		} while (pending);

		local_irq_disable();

		pending = softirq_pending(cpu);
		if (pending & mask) {
			mask &= ~pending;
			goto restart;
		}
		__local_bh_enable();

		if (pending)
			wakeup_softirqd(cpu);
	}

	local_irq_restore(flags);
}

/*
 * This function must run with irqs disabled!
 */
inline void cpu_raise_softirq(unsigned int cpu, unsigned int nr)
{
	__cpu_raise_softirq(cpu, nr);

	/*
	 * If we're in an interrupt or bh, we're done
	 * (this also catches bh-disabled code). We will
	 * actually run the softirq once we return from
	 * the irq or bh.
	 *
	 * Otherwise we wake up ksoftirqd to make sure we
	 * schedule the softirq soon.
	 */
	if (!in_interrupt())
		wakeup_softirqd(cpu);
}

void raise_softirq(unsigned int nr)
{
	long flags;

	local_irq_save(flags);
	cpu_raise_softirq(smp_processor_id(), nr);
	local_irq_restore(flags);
}

void open_softirq(int nr, void (*action)(struct softirq_action*), void *data)
{
	softirq_vec[nr].data = data;
	softirq_vec[nr].action = action;
}


/* Tasklets */
struct tasklet_head
{
	struct tasklet_struct *list;
};

/* Some compilers disobey section attribute on statics when not
   initialized -- RR */
static struct tasklet_head tasklet_vec __per_cpu_data = { NULL };
static struct tasklet_head tasklet_hi_vec __per_cpu_data = { NULL };

void __tasklet_schedule(struct tasklet_struct *t)
{
	unsigned long flags;

	local_irq_save(flags);
	t->next = __get_cpu_var(tasklet_vec).list;
	__get_cpu_var(tasklet_vec).list = t;
	cpu_raise_softirq(smp_processor_id(), TASKLET_SOFTIRQ);
	local_irq_restore(flags);
}

void __tasklet_hi_schedule(struct tasklet_struct *t)
{
	unsigned long flags;

	local_irq_save(flags);
	t->next = __get_cpu_var(tasklet_hi_vec).list;
	__get_cpu_var(tasklet_hi_vec).list = t;
	cpu_raise_softirq(smp_processor_id(), HI_SOFTIRQ);
	local_irq_restore(flags);
}

static void tasklet_action(struct softirq_action *a)
{
	struct tasklet_struct *list;

	local_irq_disable();
	list = __get_cpu_var(tasklet_vec).list;
	__get_cpu_var(tasklet_vec).list = NULL;
	local_irq_enable();

	while (list) {
		struct tasklet_struct *t = list;

		list = list->next;

		if (tasklet_trylock(t)) {
			if (!atomic_read(&t->count)) {
				if (!test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
					BUG();
				t->func(t->data);
				tasklet_unlock(t);
				continue;
			}
			tasklet_unlock(t);
		}

		local_irq_disable();
		t->next = __get_cpu_var(tasklet_vec).list;
		__get_cpu_var(tasklet_vec).list = t;
		__cpu_raise_softirq(smp_processor_id(), TASKLET_SOFTIRQ);
		local_irq_enable();
	}
}

static void tasklet_hi_action(struct softirq_action *a)
{
	struct tasklet_struct *list;

	local_irq_disable();
	list = __get_cpu_var(tasklet_hi_vec).list;
	__get_cpu_var(tasklet_hi_vec).list = NULL;
	local_irq_enable();

	while (list) {
		struct tasklet_struct *t = list;

		list = list->next;

		if (tasklet_trylock(t)) {
			if (!atomic_read(&t->count)) {
				if (!test_and_clear_bit(TASKLET_STATE_SCHED, &t->state))
					BUG();
				t->func(t->data);
				tasklet_unlock(t);
				continue;
			}
			tasklet_unlock(t);
		}

		local_irq_disable();
		t->next = __get_cpu_var(tasklet_hi_vec).list;
		__get_cpu_var(tasklet_hi_vec).list = t;
		__cpu_raise_softirq(smp_processor_id(), HI_SOFTIRQ);
		local_irq_enable();
	}
}


void tasklet_init(struct tasklet_struct *t,
		  void (*func)(unsigned long), unsigned long data)
{
	t->next = NULL;
	t->state = 0;
	atomic_set(&t->count, 0);
	t->func = func;
	t->data = data;
}

void tasklet_kill(struct tasklet_struct *t)
{
	if (in_interrupt())
		printk("Attempt to kill tasklet from interrupt\n");

	while (test_and_set_bit(TASKLET_STATE_SCHED, &t->state)) {
		do
			yield();
		while (test_bit(TASKLET_STATE_SCHED, &t->state));
	}
	tasklet_unlock_wait(t);
	clear_bit(TASKLET_STATE_SCHED, &t->state);
}



/* Old style BHs */

static void (*bh_base[32])(void);
struct tasklet_struct bh_task_vec[32];

/* BHs are serialized by spinlock global_bh_lock.

   It is still possible to make synchronize_bh() as
   spin_unlock_wait(&global_bh_lock). This operation is not used
   by kernel now, so that this lock is not made private only
   due to wait_on_irq().

   It can be removed only after auditing all the BHs.
 */
spinlock_t global_bh_lock = SPIN_LOCK_UNLOCKED;

static void bh_action(unsigned long nr)
{
	if (!spin_trylock(&global_bh_lock))
		goto resched;

	if (bh_base[nr])
		bh_base[nr]();

	hardirq_endlock();
	spin_unlock(&global_bh_lock);
	return;

	spin_unlock(&global_bh_lock);
resched:
	mark_bh(nr);
}

void init_bh(int nr, void (*routine)(void))
{
	bh_base[nr] = routine;
	mb();
}

void remove_bh(int nr)
{
	tasklet_kill(bh_task_vec+nr);
	bh_base[nr] = NULL;
}

void __init softirq_init()
{
	int i;

	for (i=0; i<32; i++)
		tasklet_init(bh_task_vec+i, bh_action, i);

	open_softirq(TASKLET_SOFTIRQ, tasklet_action, NULL);
	open_softirq(HI_SOFTIRQ, tasklet_hi_action, NULL);
}

void __run_task_queue(task_queue *list)
{
	struct list_head head, *next;
	unsigned long flags;

	spin_lock_irqsave(&tqueue_lock, flags);
	list_add(&head, list);
	list_del_init(list);
	spin_unlock_irqrestore(&tqueue_lock, flags);

	next = head.next;
	while (next != &head) {
		void (*f) (void *);
		struct tq_struct *p;
		void *data;

		p = list_entry(next, struct tq_struct, list);
		next = next->next;
		f = p->routine;
		data = p->data;
		wmb();
		p->sync = 0;
		if (f)
			f(data);
	}
}

static int ksoftirqd(void * __bind_cpu)
{
	int cpu = (int) (long) __bind_cpu;

	daemonize();
	set_user_nice(current, 19);
	current->flags |= PF_IOTHREAD;
	sigfillset(&current->blocked);

	/* Migrate to the right CPU */
	set_cpus_allowed(current, 1UL << cpu);
	if (smp_processor_id() != cpu)
		BUG();

	sprintf(current->comm, "ksoftirqd_CPU%d", cpu);

	__set_current_state(TASK_INTERRUPTIBLE);
	mb();

	ksoftirqd_task(cpu) = current;

	for (;;) {
		if (!softirq_pending(cpu))
			schedule();

		__set_current_state(TASK_RUNNING);

		while (softirq_pending(cpu)) {
			do_softirq();
			cond_resched();
		}

		__set_current_state(TASK_INTERRUPTIBLE);
	}
}

static int __devinit cpu_callback(struct notifier_block *nfb,
				  unsigned long action,
				  void *hcpu)
{
	int hotcpu = (unsigned long)hcpu;

	if (action == CPU_ONLINE) {
		if (kernel_thread(ksoftirqd, hcpu,
				  CLONE_FS | CLONE_FILES | CLONE_SIGNAL) < 0) {
			printk("ksoftirqd for %i failed\n", hotcpu);
			return NOTIFY_BAD;
		}

		while (!ksoftirqd_task(hotcpu))
			yield();
		return NOTIFY_OK;
 	}
	return NOTIFY_BAD;
}

static struct notifier_block cpu_nfb = { &cpu_callback, NULL, 0 };

__init int spawn_ksoftirqd(void)
{
	cpu_callback(&cpu_nfb, CPU_ONLINE, (void *)(long)smp_processor_id());
	register_cpu_notifier(&cpu_nfb);
	return 0;
}
