/* $Id: sys_cris.c,v 1.10 2001/06/27 21:16:15 hp Exp $
 *
 * linux/arch/cris/kernel/sys_cris.c
 *
 * This file contains various random system calls that
 * have a non-standard calling sequence on some platforms.
 * Since we don't have to do any backwards compatibility, our
 * versions are done in the most "normal" way possible.
 *
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
#include <asm/segment.h>

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way Unix traditionally does this, though.
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

/* common code for old and new mmaps */
static inline long
do_mmap2(unsigned long addr, unsigned long len, unsigned long prot,
        unsigned long flags, unsigned long fd, unsigned long pgoff)
{
        int error = -EBADF;
        struct file * file = NULL;

        flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
        if (!(flags & MAP_ANONYMOUS)) {
                file = fget(fd);
                if (!file)
                        goto out;
        }

        down_write(&current->mm->mmap_sem);
        error = do_mmap_pgoff(file, addr, len, prot, flags, pgoff);
        up_write(&current->mm->mmap_sem);

        if (file)
                fput(file);
out:
        return error;
}

asmlinkage unsigned long old_mmap(unsigned long *args)
{        
	unsigned long buffer[6];
	int err = -EFAULT;

	if (copy_from_user(&buffer, args, sizeof(buffer)))
		goto out;

	err = -EINVAL;
	if (buffer[5] & ~PAGE_MASK) /* verify that offset is on page boundary */
		goto out;

	err = do_mmap2(buffer[0], buffer[1], buffer[2], buffer[3],
                       buffer[4], buffer[5] >> PAGE_SHIFT);
out:
	return err;
}

asmlinkage long
sys_mmap2(unsigned long addr, unsigned long len, unsigned long prot,
          unsigned long flags, unsigned long fd, unsigned long pgoff)
{
        return do_mmap2(addr, len, prot, flags, fd, pgoff);
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly. (same as arch/i386)
 */

asmlinkage int sys_ipc (uint call, int first, int second,
			int third, void *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		return sys_semop (first, (struct sembuf *)ptr, second);
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
			return sys_msgrcv (first,
					   (struct msgbuf *) ptr,
					   second, fifth, third);
		}
	case MSGGET:
		return sys_msgget ((key_t) first, second);
	case MSGCTL:
		return sys_msgctl (first, second, (struct msqid_ds *) ptr);

	case SHMAT: {
                ulong raddr;
                ret = sys_shmat (first, (char *) ptr, second, &raddr);
                if (ret)
                        return ret;
                return put_user (raddr, (ulong *) third);
        }
	case SHMDT: 
		return sys_shmdt ((char *)ptr);
	case SHMGET:
		return sys_shmget (first, second, third);
	case SHMCTL:
		return sys_shmctl (first, second,
				   (struct shmid_ds *) ptr);
	default:
		return -EINVAL;
	}
}

/* apparently this is legacy - if we don't need this in Linux/CRIS we can remove it. */

asmlinkage int sys_pause(void)
{
        current->state = TASK_INTERRUPTIBLE;
        schedule();
        return -ERESTARTNOHAND;
}
