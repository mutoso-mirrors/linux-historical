/*
 * linux/arch/i386/kernel/sys_i386.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/i386
 * platform.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/file.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sys_pipe(unsigned long * fildes)
{
	int fd[2];
	int error;

	lock_kernel();
	error = do_pipe(fd);
	unlock_kernel();
	if (!error) {
		if (copy_to_user(fildes, fd, 2*sizeof(int)))
			error = -EFAULT;
	}
	return error;
}

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

asmlinkage int old_mmap(struct mmap_arg_struct *arg)
{
	struct file * file = NULL;
	struct mmap_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	if (!(a.flags & MAP_ANONYMOUS)) {
		if (a.fd >= NR_OPEN || !(file = current->files->fd[a.fd]))
			return -EBADF;
	}
	a.flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	{
		unsigned long retval;
		struct semaphore *sem = &current->mm->mmap_sem;
		down(sem);
		retval = do_mmap(file, a.addr, a.len, a.prot, a.flags, a.offset);
		up(sem);
		return retval;
	}
}

extern asmlinkage int sys_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

struct sel_arg_struct {
	unsigned long n;
	fd_set *inp, *outp, *exp;
	struct timeval *tvp;
};

asmlinkage int old_select(struct sel_arg_struct *arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	/* sys_select() does the appropriate kernel locking */
	return sys_select(a.n, a.inp, a.outp, a.exp, a.tvp);
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int sys_ipc (uint call, int first, int second, int third, void *ptr, long fifth)
{
	int version, ret;

	lock_kernel();
	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	if (call <= SEMCTL)
		switch (call) {
		case SEMOP:
			ret = sys_semop (first, (struct sembuf *)ptr, second);
			goto out;
		case SEMGET:
			ret = sys_semget (first, second, third);
			goto out;
		case SEMCTL: {
			union semun fourth;
			ret = -EINVAL;
			if (!ptr)
				goto out;
			ret = -EFAULT;
			if (get_user(fourth.__pad, (void **) ptr))
				goto out;	
			ret = sys_semctl (first, second, third, fourth);
			goto out;
			}
		default:
			ret = -EINVAL;
			goto out;
		}
	if (call <= MSGCTL) 
		switch (call) {
		case MSGSND:
			ret = sys_msgsnd (first, (struct msgbuf *) ptr, 
					  second, third);
			goto out;
		case MSGRCV:
			switch (version) {
			case 0: {
				struct ipc_kludge tmp;
				ret = -EINVAL;
				if (!ptr)
					goto out;
				ret = -EFAULT;
				if (copy_from_user(&tmp,(struct ipc_kludge *) ptr, 
								   sizeof (tmp)))
					goto out; 	
				ret = sys_msgrcv (first, tmp.msgp, second, tmp.msgtyp, third);
				goto out;
				}
			case 1: default:
				ret = sys_msgrcv (first, (struct msgbuf *) ptr, second, fifth, third);
				goto out;
			}
		case MSGGET:
			ret = sys_msgget ((key_t) first, second);
			goto out;
		case MSGCTL:
			ret = sys_msgctl (first, second, (struct msqid_ds *) ptr);
			goto out;
		default:
			ret = -EINVAL;
			goto out;
		}
	if (call <= SHMCTL) 
		switch (call) {
		case SHMAT:
			switch (version) {
			case 0: default: {
				ulong raddr;
				ret = sys_shmat (first, (char *) ptr, second, &raddr);
				if (ret)
					goto out;
				ret = put_user (raddr, (ulong *) third);
				goto out;
			}
			case 1:	/* iBCS2 emulator entry point */
				ret = -EINVAL;
				if (get_fs() != get_ds())
					goto out;
				ret = sys_shmat (first, (char *) ptr, second, (ulong *) third);
				goto out;
			}
		case SHMDT: 
			ret = sys_shmdt ((char *)ptr);
			goto out;
		case SHMGET:
			ret = sys_shmget (first, second, third);
			goto out;
		case SHMCTL:
			ret = sys_shmctl (first, second, (struct shmid_ds *) ptr);
			goto out;
		default:
			ret = -EINVAL;
			goto out;
		}
	else
		ret = -EINVAL;
out:
	unlock_kernel();
	return ret;
}
