/* 
 * Copyright (C) 2000 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/sched.h"
#include "linux/shm.h"
#include "asm/ipc.h"
#include "asm/mman.h"
#include "asm/uaccess.h"
#include "asm/unistd.h"

/*
 * Perform the select(nd, in, out, ex, tv) and mmap() system
 * calls. Linux/i386 didn't use to be able to handle more than
 * 4 system call parameters, so these system calls used a memory
 * block for parameter passing..
 */

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

extern int old_mmap(unsigned long addr, unsigned long len,
		    unsigned long prot, unsigned long flags,
		    unsigned long fd, unsigned long offset);

long old_mmap_i386(struct mmap_arg_struct *arg)
{
	struct mmap_arg_struct a;
	int err = -EFAULT;

	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;

	err = old_mmap(a.addr, a.len, a.prot, a.flags, a.fd, a.offset);
 out:
	return err;
}

struct sel_arg_struct {
	unsigned long n;
	fd_set *inp, *outp, *exp;
	struct timeval *tvp;
};

long old_select(struct sel_arg_struct *arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	/* sys_select() does the appropriate kernel locking */
	return sys_select(a.n, a.inp, a.outp, a.exp, a.tvp);
}

/* The i386 version skips reading from %esi, the fourth argument. So we must do
 * this, too.
 */
long sys_clone(unsigned long clone_flags, unsigned long newsp, int *parent_tid,
	       int unused, int *child_tid)
{
	long ret;

	/* XXX: normal arch do here this pass, and also pass the regs to
	 * do_fork, instead of NULL. Currently the arch-independent code
	 * ignores these values, while the UML code (actually it's
	 * copy_thread) does the right thing. But this should change,
	 probably. */
	/*if (!newsp)
		newsp = UPT_SP(current->thread.regs);*/
	current->thread.forking = 1;
	ret = do_fork(clone_flags, newsp, NULL, 0, parent_tid, child_tid);
	current->thread.forking = 0;
	return(ret);
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
long sys_ipc (uint call, int first, int second,
	     int third, void *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		return sys_semtimedop(first, (struct sembuf *) ptr, second,
				      NULL);
	case SEMTIMEDOP:
		return sys_semtimedop(first, (struct sembuf *) ptr, second,
				      (const struct timespec *) fifth);
	case SEMGET:
		return sys_semget (first, second, third);
	case SEMCTL: {
		union semun fourth;
		if (!ptr)
			return -EINVAL;
		if (get_user(fourth.__pad, (void **) ptr))
			return -EFAULT;
		return sys_semctl (first, second, third, fourth);
	}

	case MSGSND:
		return sys_msgsnd (first, (struct msgbuf *) ptr,
				   second, third);
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;
			if (!ptr)
				return -EINVAL;

			if (copy_from_user(&tmp,
					   (struct ipc_kludge *) ptr,
					   sizeof (tmp)))
				return -EFAULT;
			return sys_msgrcv (first, tmp.msgp, second,
					   tmp.msgtyp, third);
		}
		default:
		        panic("msgrcv with version != 0");
			return sys_msgrcv (first,
					   (struct msgbuf *) ptr,
					   second, fifth, third);
		}
	case MSGGET:
		return sys_msgget ((key_t) first, second);
	case MSGCTL:
		return sys_msgctl (first, second, (struct msqid_ds *) ptr);

	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;
			ret = do_shmat (first, (char *) ptr, second, &raddr);
			if (ret)
				return ret;
			return put_user (raddr, (ulong *) third);
		}
		case 1:	/* iBCS2 emulator entry point */
			if (!segment_eq(get_fs(), get_ds()))
				return -EINVAL;
			return do_shmat (first, (char *) ptr, second, (ulong *) third);
		}
	case SHMDT:
		return sys_shmdt ((char *)ptr);
	case SHMGET:
		return sys_shmget (first, second, third);
	case SHMCTL:
		return sys_shmctl (first, second,
				   (struct shmid_ds *) ptr);
	default:
		return -ENOSYS;
	}
}

long sys_sigaction(int sig, const struct old_sigaction __user *act,
			 struct old_sigaction __user *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
