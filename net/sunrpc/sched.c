/*
 * linux/net/sunrpc/sched.c
 *
 * Scheduling for synchronous and asynchronous RPC requests.
 *
 * Copyright (C) 1996 Olaf Kirch, <okir@monad.swb.de>
 */

#define __NO_VERSION__
#include <linux/module.h>

#define __KERNEL_SYSCALLS__
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/unistd.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sunrpc/clnt.h>

#ifdef RPC_DEBUG
#define RPCDBG_FACILITY		RPCDBG_SCHED
static int			rpc_task_id = 0;
#endif

#define _S(signo)		(1 << ((signo)-1))

/*
 * We give RPC the same get_free_pages priority as NFS
 */
#define GFP_RPC			GFP_NFS

static void			__rpc_default_timer(struct rpc_task *task);
static void			rpciod_killall(void);

/*
 * When an asynchronous RPC task is activated within a bottom half
 * handler, or while executing another RPC task, it is put on
 * schedq, and rpciod is woken up.
 */
static struct rpc_wait_queue	schedq = RPC_INIT_WAITQ("schedq");

/*
 * RPC tasks that create another task (e.g. for contacting the portmapper)
 * will wait on this queue for their child's completion
 */
static struct rpc_wait_queue	childq = RPC_INIT_WAITQ("childq");

/*
 * All RPC tasks are linked into this list
 */
static struct rpc_task *	all_tasks = NULL;

/*
 * rpciod-related stuff
 */
static struct wait_queue *	rpciod_idle = NULL;
static struct wait_queue *	rpciod_killer = NULL;
static int			rpciod_sema = 0;
static pid_t			rpciod_pid = 0;
static int			rpc_inhibit = 0;

/*
 * This is the last-ditch buffer for NFS swap requests
 */
static u32			swap_buffer[PAGE_SIZE >> 2];
static int			swap_buffer_used = 0;

/*
 * Add new request to wait queue.
 *
 * Swapper tasks always get inserted at the head of the queue.
 * This should avoid many nasty memory deadlocks and hopefully
 * improve overall performance.
 * Everyone else gets appended to the queue to ensure proper FIFO behavior.
 */
void
rpc_add_wait_queue(struct rpc_wait_queue *queue, struct rpc_task *task)
{
	if (task->tk_rpcwait) {
		if (task->tk_rpcwait != queue)
			printk(KERN_WARNING "RPC: doubly enqueued task!\n");
		return;
	}
	if (RPC_IS_SWAPPER(task))
		rpc_insert_list(&queue->task, task);
	else
		rpc_append_list(&queue->task, task);
	task->tk_rpcwait = queue;

	dprintk("RPC: %4d added to queue %p \"%s\"\n",
				task->tk_pid, queue, rpc_qname(queue));
}

/*
 * Remove request from queue
 */
void
rpc_remove_wait_queue(struct rpc_task *task)
{
	struct rpc_wait_queue *queue;

	if (!(queue = task->tk_rpcwait))
		return;
	rpc_remove_list(&queue->task, task);
	task->tk_rpcwait = NULL;

	dprintk("RPC: %4d removed from queue %p \"%s\"\n",
				task->tk_pid, queue, rpc_qname(queue));
}

/*
 * Set up a timer for the current task.
 */
inline void
rpc_add_timer(struct rpc_task *task, rpc_action timer)
{
	unsigned long	expires = jiffies + task->tk_timeout;

	dprintk("RPC: %4d setting alarm for %lu ms\n",
			task->tk_pid, task->tk_timeout * 1000 / HZ);
	if (!timer)
		timer = __rpc_default_timer;
	if (expires < jiffies) {
		printk("RPC: bad timeout value %ld - setting to 10 sec!\n",
					task->tk_timeout);
		expires = jiffies + 10 * HZ;
	}
	task->tk_timer.expires  = expires;
	task->tk_timer.data     = (unsigned long) task;
	task->tk_timer.function = (void (*)(unsigned long)) timer;
	task->tk_timer.prev     = NULL;
	task->tk_timer.next     = NULL;
	add_timer(&task->tk_timer);
}

/*
 * Delete any timer for the current task.
 * Must be called with interrupts off.
 */
inline void
rpc_del_timer(struct rpc_task *task)
{
	if (task->tk_timeout) {
		dprintk("RPC: %4d deleting timer\n", task->tk_pid);
		del_timer(&task->tk_timer);
		task->tk_timeout = 0;
	}
}

/*
 * Make an RPC task runnable.
 */
static inline void
rpc_make_runnable(struct rpc_task *task)
{
	if (task->tk_timeout) {
		printk("RPC: task w/ running timer in rpc_make_runnable!!\n");
		return;
	}
	if (RPC_IS_ASYNC(task)) {
		rpc_add_wait_queue(&schedq, task);
		wake_up(&rpciod_idle);
	} else {
		wake_up(&task->tk_wait);
	}
	task->tk_flags |= RPC_TASK_RUNNING;
}

/*
 * Prepare for sleeping on a wait queue.
 * By always appending tasks to the list we ensure FIFO behavior.
 * NB: An RPC task will only receive interrupt-driven events as long
 * as it's on a wait queue.
 */
static void
__rpc_sleep_on(struct rpc_wait_queue *q, struct rpc_task *task,
			rpc_action action, rpc_action timer)
{
	unsigned long	oldflags;

	dprintk("RPC: %4d sleep_on(queue \"%s\" time %ld)\n", task->tk_pid,
				rpc_qname(q), jiffies);

	/*
	 * Protect the execution below.
	 */
	save_flags(oldflags); cli();

	rpc_add_wait_queue(q, task);
	task->tk_callback = action;
	if (task->tk_timeout)
		rpc_add_timer(task, timer);
	task->tk_flags &= ~RPC_TASK_RUNNING;

	restore_flags(oldflags);
	return;
}

void
rpc_sleep_on(struct rpc_wait_queue *q, struct rpc_task *task,
				rpc_action action, rpc_action timer)
{
	__rpc_sleep_on(q, task, action, timer);
}

/*
 * Wake up a single task -- must be invoked with bottom halves off.
 *
 * It would probably suffice to cli/sti the del_timer and remove_wait_queue
 * operations individually.
 */
static void
__rpc_wake_up(struct rpc_task *task)
{
	dprintk("RPC: %4d __rpc_wake_up (now %ld inh %d)\n",
					task->tk_pid, jiffies, rpc_inhibit);

#ifdef RPC_DEBUG
	if (task->tk_magic != 0xf00baa) {
		printk("RPC: attempt to wake up non-existing task!\n");
		rpc_debug = ~0;
		return;
	}
#endif
	rpc_del_timer(task);
	if (task->tk_rpcwait != &schedq)
		rpc_remove_wait_queue(task);
	if (!RPC_IS_RUNNING(task)) {
		rpc_make_runnable(task);
		task->tk_flags |= RPC_TASK_CALLBACK;
	}
	dprintk("RPC:      __rpc_wake_up done\n");
}

/*
 * Default timeout handler if none specified by user
 */
static void
__rpc_default_timer(struct rpc_task *task)
{
	dprintk("RPC: %d timeout (default timer)\n", task->tk_pid);
	task->tk_status = -ETIMEDOUT;
	task->tk_timeout = 0;
	__rpc_wake_up(task);
}

/*
 * Wake up the specified task
 */
void
rpc_wake_up_task(struct rpc_task *task)
{
	unsigned long	oldflags;

	save_flags(oldflags); cli();
	__rpc_wake_up(task);
	restore_flags(oldflags);
}

/*
 * Wake up the next task on the wait queue.
 */
struct rpc_task *
rpc_wake_up_next(struct rpc_wait_queue *queue)
{
	unsigned long	oldflags;
	struct rpc_task	*task;

	dprintk("RPC:      wake_up_next(%p \"%s\")\n", queue, rpc_qname(queue));
	save_flags(oldflags); cli();
	if ((task = queue->task) != 0)
		__rpc_wake_up(task);
	restore_flags(oldflags);

	return task;
}

/*
 * Wake up all tasks on a queue
 */
void
rpc_wake_up(struct rpc_wait_queue *queue)
{
	unsigned long	oldflags;

	save_flags(oldflags); cli();
	while (queue->task)
		__rpc_wake_up(queue->task);
	restore_flags(oldflags);
}

/*
 * Wake up all tasks on a queue, and set their status value.
 */
void
rpc_wake_up_status(struct rpc_wait_queue *queue, int status)
{
	struct rpc_task	*task;
	unsigned long	oldflags;

	save_flags(oldflags); cli();
	while ((task = queue->task) != NULL) {
		task->tk_status = status;
		__rpc_wake_up(task);
	}
	restore_flags(oldflags);
}

/*
 * Run a task at a later time
 */
static void	__rpc_atrun(struct rpc_task *);
void
rpc_delay(struct rpc_task *task, unsigned long delay)
{
	static struct rpc_wait_queue	delay_queue;

	task->tk_timeout = delay;
	rpc_sleep_on(&delay_queue, task, NULL, __rpc_atrun);
}

static void
__rpc_atrun(struct rpc_task *task)
{
	task->tk_status = 0;
	__rpc_wake_up(task);
}

/*
 * This is the RPC `scheduler' (or rather, the finite state machine).
 */
static int
__rpc_execute(struct rpc_task *task)
{
	unsigned long	oldflags;
	int		status = 0;

	dprintk("RPC: %4d rpc_execute flgs %x\n",
				task->tk_pid, task->tk_flags);

	if (!RPC_IS_RUNNING(task)) {
		printk("RPC: rpc_execute called for sleeping task!!\n");
		return 0;
	}

	while (1) {
		/*
		 * Execute any pending callback.
		 */
		if (task->tk_flags & RPC_TASK_CALLBACK) {
			task->tk_flags &= ~RPC_TASK_CALLBACK;
			if (task->tk_callback) {
				task->tk_callback(task);
				task->tk_callback = NULL;
			}
		}

		/*
		 * No handler for next step means exit.
		 */
		if (!task->tk_action)
			break;

		/*
		 * Perform the next FSM step.
		 * tk_action may be NULL when the task has been killed
		 * by someone else.
		 */
		if (RPC_IS_RUNNING(task) && task->tk_action)
			task->tk_action(task);

		/*
		 * Check whether task is sleeping.
		 * Note that if the task may go to sleep in tk_action,
		 * and the RPC reply arrives before we get here, it will
		 * have state RUNNING, but will still be on schedq.
		 */
		save_flags(oldflags); cli();
		if (RPC_IS_RUNNING(task)) {
			if (task->tk_rpcwait == &schedq)
				rpc_remove_wait_queue(task);
		} else while (!RPC_IS_RUNNING(task)) {
			if (RPC_IS_ASYNC(task)) {
				restore_flags(oldflags);
				return 0;
			}

			/* sync task: sleep here */
			dprintk("RPC: %4d sync task going to sleep\n",
							task->tk_pid);
			current->timeout = 0;
			sleep_on(&task->tk_wait);

			/* When the task received a signal, remove from
			 * any queues etc, and make runnable again. */
			if (signalled())
				__rpc_wake_up(task);

			dprintk("RPC: %4d sync task resuming\n",
							task->tk_pid);
		}
		restore_flags(oldflags);

		/*
		 * When a sync task receives a signal, it exits with
		 * -ERESTARTSYS. In order to catch any callbacks that
		 * clean up after sleeping on some queue, we don't
		 * break the loop here, but go around once more.
		 */
		if (0 && !RPC_IS_ASYNC(task) && signalled()) {
			dprintk("RPC: %4d got signal (map %08lx)\n",
				task->tk_pid,
				current->signal & ~current->blocked);
			rpc_exit(task, -ERESTARTSYS);
		}
	}

	dprintk("RPC: %4d exit() = %d\n", task->tk_pid, task->tk_status);
	if (task->tk_exit) {
		status = task->tk_status;
		task->tk_exit(task);
	}

	return status;
}

/*
 * User-visible entry point to the scheduler.
 * The recursion protection is for debugging. It should go away once
 * the code has stabilized.
 */
void
rpc_execute(struct rpc_task *task)
{
	static int	executing = 0;
	int		incr = RPC_IS_ASYNC(task)? 1 : 0;

	if (incr && (executing || rpc_inhibit)) {
		printk("RPC: rpc_execute called recursively!\n");
		return;
	}
	executing += incr;
	__rpc_execute(task);
	executing -= incr;
}

/*
 * This is our own little scheduler for async RPC tasks.
 */
static void
__rpc_schedule(void)
{
	struct rpc_task	*task;
	int		count = 0;
	unsigned long	oldflags;

	dprintk("RPC:      rpc_schedule enter\n");
	while (1) {
		save_flags(oldflags); cli();
		if (!(task = schedq.task))
			break;
		rpc_del_timer(task);
		rpc_remove_wait_queue(task);
		task->tk_flags |= RPC_TASK_RUNNING;
		restore_flags(oldflags);

		__rpc_execute(task);

		if (++count >= 200) {
			count = 0;
			need_resched = 1;
		}
		if (need_resched)
			schedule();
	}
	restore_flags(oldflags);
	dprintk("RPC:      rpc_schedule leave\n");
}

/*
 * Allocate memory for RPC purpose.
 *
 * This is yet another tricky issue: For sync requests issued by
 * a user process, we want to make kmalloc sleep if there isn't
 * enough memory. Async requests should not sleep too excessively
 * because that will block rpciod (but that's not dramatic when
 * it's starved of memory anyway). Finally, swapout requests should
 * never sleep at all, and should not trigger another swap_out
 * request through kmalloc which would just increase memory contention.
 *
 * I hope the following gets it right, which gives async requests
 * a slight advantage over sync requests (good for writeback, debatable
 * for readahead):
 *
 *   sync user requests:	GFP_KERNEL
 *   async requests:		GFP_RPC		(== GFP_NFS)
 *   swap requests:		GFP_ATOMIC	(or new GFP_SWAPPER)
 */
void *
rpc_allocate(unsigned int flags, unsigned int size)
{
	u32	*buffer;
	int	gfp;

	if (flags & RPC_TASK_SWAPPER)
		gfp = GFP_ATOMIC;
	else if (flags & RPC_TASK_ASYNC)
		gfp = GFP_RPC;
	else
		gfp = GFP_KERNEL;

	do {
		if ((buffer = (u32 *) kmalloc(size, gfp)) != NULL) {
			dprintk("RPC:      allocated buffer %p\n", buffer);
			return buffer;
		}
		if ((flags & RPC_TASK_SWAPPER) && !swap_buffer_used++) {
			dprintk("RPC:      used last-ditch swap buffer\n");
			return swap_buffer;
		}
		if (flags & RPC_TASK_ASYNC)
			return NULL;
		current->timeout = jiffies + (HZ >> 4);
		schedule();
	} while (!signalled());

	return NULL;
}

void
rpc_free(void *buffer)
{
	if (buffer != swap_buffer) {
		kfree(buffer);
		return;
	}
	swap_buffer_used = 0;
}

/*
 * Creation and deletion of RPC task structures
 */
inline void
rpc_init_task(struct rpc_task *task, struct rpc_clnt *clnt,
				rpc_action callback, int flags)
{
	memset(task, 0, sizeof(*task));
	task->tk_client = clnt;
	task->tk_flags  = RPC_TASK_RUNNING | flags;
	task->tk_exit   = callback;
	if (current->uid != current->fsuid || current->gid != current->fsgid)
		task->tk_flags |= RPC_TASK_SETUID;

	/* Initialize retry counters */
	task->tk_garb_retry = 2;
	task->tk_cred_retry = 2;
	task->tk_suid_retry = 1;

	/* Add to global list of all tasks */
	task->tk_next_task = all_tasks;
	task->tk_prev_task = NULL;
	if (all_tasks)
		all_tasks->tk_prev_task = task;
	all_tasks = task;

	if (clnt)
		clnt->cl_users++;

#ifdef RPC_DEBUG
	task->tk_magic = 0xf00baa;
	task->tk_pid = rpc_task_id++;
#endif
	dprintk("RPC: %4d new task procpid %d\n", task->tk_pid,
				current->pid);
}

struct rpc_task *
rpc_new_task(struct rpc_clnt *clnt, rpc_action callback, int flags)
{
	struct rpc_task	*task;

	if (!(task = (struct rpc_task *) rpc_allocate(flags, sizeof(*task))))
		return NULL;

	rpc_init_task(task, clnt, callback, flags);

	dprintk("RPC: %4d allocated task\n", task->tk_pid);
	task->tk_flags |= RPC_TASK_DYNAMIC;
	return task;
}

void
rpc_release_task(struct rpc_task *task)
{
	struct rpc_task	*next, *prev;

	dprintk("RPC: %4d release task\n", task->tk_pid);

	/* Remove from global task list */
	prev = task->tk_prev_task;
	next = task->tk_next_task;
	if (next)
		next->tk_prev_task = prev;
	if (prev)
		prev->tk_next_task = next;
	else
		all_tasks = next;

	/* Release resources */
	if (task->tk_rqstp)
		xprt_release(task);
	if (task->tk_cred)
		rpcauth_releasecred(task);
	if (task->tk_buffer) {
		rpc_free(task->tk_buffer);
		task->tk_buffer = NULL;
	}
	if (task->tk_client) {
		rpc_release_client(task->tk_client);
		task->tk_client = NULL;
	}

#ifdef RPC_DEBUG
	task->tk_magic = 0;
#endif

	if (task->tk_flags & RPC_TASK_DYNAMIC) {
		dprintk("RPC: %4d freeing task\n", task->tk_pid);
		task->tk_flags &= ~RPC_TASK_DYNAMIC;
		rpc_free(task);
	}
}

/*
 * Handling of RPC child tasks
 * We can't simply call wake_up(parent) here, because the
 * parent task may already have gone away
 */
static inline struct rpc_task *
rpc_find_parent(struct rpc_task *child)
{
	struct rpc_task	*temp, *parent;

	parent = (struct rpc_task *) child->tk_calldata;
	for (temp = childq.task; temp; temp = temp->tk_next) {
		if (temp == parent)
			return parent;
	}
	return NULL;
}

static void
rpc_child_exit(struct rpc_task *child)
{
	struct rpc_task	*parent;

	if ((parent = rpc_find_parent(child)) != NULL) {
		parent->tk_status = child->tk_status;
		rpc_wake_up_task(parent);
	}
	rpc_release_task(child);
}

struct rpc_task *
rpc_new_child(struct rpc_clnt *clnt, struct rpc_task *parent)
{
	struct rpc_task	*task;

	if (!(task = rpc_new_task(clnt, NULL, RPC_TASK_ASYNC|RPC_TASK_CHILD))) {
		parent->tk_status = -ENOMEM;
		return NULL;
	}
	task->tk_exit = rpc_child_exit;
	task->tk_calldata = parent;

	return task;
}

void
rpc_run_child(struct rpc_task *task, struct rpc_task *child, rpc_action func)
{
	rpc_make_runnable(child);
	rpc_sleep_on(&childq, task, func, NULL);
}

/*
 * Kill all tasks for the given client.
 * XXX: kill their descendants as well?
 */
void
rpc_killall_tasks(struct rpc_clnt *clnt)
{
	struct rpc_task	**q, *rovr;

	dprintk("RPC:      killing all tasks for client %p\n", clnt);
	rpc_inhibit++;
	for (q = &all_tasks; (rovr = *q); q = &rovr->tk_next_task) {
		if (!clnt || rovr->tk_client == clnt) {
			rovr->tk_flags |= RPC_TASK_KILLED;
			rpc_exit(rovr, -EIO);
			rpc_wake_up_task(rovr);
		}
	}
	rpc_inhibit--;
}

/*
 * This is the rpciod kernel thread
 */
static int
rpciod(void *ptr)
{
	struct wait_queue **assassin = (struct wait_queue **) ptr;
	unsigned long	oldflags;
	int		rounds = 0;

	lock_kernel();
	rpciod_pid = current->pid;

	MOD_INC_USE_COUNT;
	/* exit_files(current); */
	exit_mm(current);
	current->blocked |= ~_S(SIGKILL);
	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "rpciod");

	dprintk("RPC: rpciod starting (pid %d)\n", rpciod_pid);
	while (rpciod_sema) {
		if (signalled()) {
			if (current->signal & _S(SIGKILL)) {
				rpciod_killall();
			} else {
				printk("rpciod: ignoring signal (%d users)\n",
					rpciod_sema);
			}
			current->signal &= current->blocked;
		}
		__rpc_schedule();

		if (++rounds >= 64)	/* safeguard */
			schedule();
		save_flags(oldflags); cli();
		if (!schedq.task) {
			dprintk("RPC: rpciod back to sleep\n");
			interruptible_sleep_on(&rpciod_idle);
			dprintk("RPC: switch to rpciod\n");
		}
		restore_flags(oldflags);
	}

	dprintk("RPC: rpciod shutdown commences\n");
	if (all_tasks) {
		printk("rpciod: active tasks at shutdown?!\n");
		rpciod_killall();
	}

	rpciod_pid = 0;
	wake_up(assassin);

	dprintk("RPC: rpciod exiting\n");
	MOD_DEC_USE_COUNT;
	return 0;
}

static void
rpciod_killall(void)
{
	while (all_tasks) {
		unsigned long	oldsig = current->signal;

		current->signal = 0;
		rpc_killall_tasks(NULL);
		__rpc_schedule();
		current->timeout = jiffies + HZ / 100;
		need_resched = 1;
		schedule();
		current->signal = oldsig;
	}
}

void
rpciod_up(void)
{
	dprintk("rpciod_up pid %d sema %d\n", rpciod_pid, rpciod_sema);
	if (!(rpciod_sema++) || !rpciod_pid)
		kernel_thread(rpciod, &rpciod_killer, 0);
}

void
rpciod_down(void)
{
	dprintk("rpciod_down pid %d sema %d\n", rpciod_pid, rpciod_sema);
	if (--rpciod_sema > 0)
		return;

	rpciod_sema = 0;
	kill_proc(rpciod_pid, SIGKILL, 1);
	while (rpciod_pid) {
		if (signalled())
			return;
		interruptible_sleep_on(&rpciod_killer);
	}
}
