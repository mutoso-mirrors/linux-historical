/*
 * linux/ipc/msg.c
 * Copyright (C) 1992 Krishna Balasubramanian 
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/msg.h>
#include <linux/stat.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>

#include <asm/uaccess.h>

extern int ipcperms (struct ipc_perm *ipcp, short msgflg);

static void freeque (int id);
static int newque (key_t key, int msgflg);
static int findkey (key_t key);

static struct msqid_ds *msgque[MSGMNI];
static int msgbytes = 0;
static int msghdrs = 0;
static unsigned short msg_seq = 0;
static int used_queues = 0;
static int max_msqid = 0;
static struct wait_queue *msg_lock = NULL;

__initfunc(void msg_init (void))
{
	int id;
	
	for (id = 0; id < MSGMNI; id++) 
		msgque[id] = (struct msqid_ds *) IPC_UNUSED;
	msgbytes = msghdrs = msg_seq = max_msqid = used_queues = 0;
	msg_lock = NULL;
	return;
}

static int real_msgsnd (int msqid, struct msgbuf *msgp, size_t msgsz, int msgflg)
{
	int id, err;
	struct msqid_ds *msq;
	struct ipc_perm *ipcp;
	struct msg *msgh;
	long mtype;
	unsigned long flags;
	
	if (msgsz > MSGMAX || (long) msgsz < 0 || msqid < 0)
		return -EINVAL;
	if (!msgp) 
		return -EFAULT;
	err = verify_area (VERIFY_READ, msgp->mtext, msgsz);
	if (err) 
		return err;
	get_user(mtype, &msgp->mtype);
	if (mtype < 1)
		return -EINVAL;
	id = (unsigned int) msqid % MSGMNI;
	msq = msgque [id];
	if (msq == IPC_UNUSED || msq == IPC_NOID)
		return -EINVAL;
	ipcp = &msq->msg_perm; 

 slept:
	if (msq->msg_perm.seq != (unsigned int) msqid / MSGMNI) 
		return -EIDRM;

	if (ipcperms(ipcp, S_IWUGO)) 
		return -EACCES;
	
	if (msgsz + msq->msg_cbytes > msq->msg_qbytes) { 
		if (msgsz + msq->msg_cbytes > msq->msg_qbytes) { 
			/* still no space in queue */
			if (msgflg & IPC_NOWAIT)
				return -EAGAIN;
			if (signal_pending(current))
				return -EINTR;
			interruptible_sleep_on (&msq->wwait);
			goto slept;
		}
	}
	
	/* allocate message header and text space*/ 
	msgh = (struct msg *) kmalloc (sizeof(*msgh) + msgsz, GFP_ATOMIC);
	if (!msgh)
		return -ENOMEM;
	msgh->msg_spot = (char *) (msgh + 1);

	copy_from_user (msgh->msg_spot, msgp->mtext, msgsz); 
	
	if (msgque[id] == IPC_UNUSED || msgque[id] == IPC_NOID
		|| msq->msg_perm.seq != (unsigned int) msqid / MSGMNI) {
		kfree(msgh);
		return -EIDRM;
	}

	msgh->msg_next = NULL;
	msgh->msg_ts = msgsz;
	msgh->msg_type = mtype;
	msgh->msg_stime = CURRENT_TIME;

	save_flags(flags);
	cli();
	if (!msq->msg_first)
		msq->msg_first = msq->msg_last = msgh;
	else {
		msq->msg_last->msg_next = msgh;
		msq->msg_last = msgh;
	}
	msq->msg_cbytes += msgsz;
	msgbytes  += msgsz;
	msghdrs++;
	msq->msg_qnum++;
	msq->msg_lspid = current->pid;
	msq->msg_stime = CURRENT_TIME;
	restore_flags(flags);
	wake_up (&msq->rwait);
	return 0;
}

static int real_msgrcv (int msqid, struct msgbuf *msgp, size_t msgsz, long msgtyp, int msgflg)
{
	struct msqid_ds *msq;
	struct ipc_perm *ipcp;
	struct msg *tmsg, *leastp = NULL;
	struct msg *nmsg = NULL;
	int id, err;
	unsigned long flags;

	if (msqid < 0 || (long) msgsz < 0)
		return -EINVAL;
	if (!msgp || !msgp->mtext)
	    return -EFAULT;

	err = verify_area (VERIFY_WRITE, msgp->mtext, msgsz);
	if (err)
		return err;

	id = (unsigned int) msqid % MSGMNI;
	msq = msgque [id];
	if (msq == IPC_NOID || msq == IPC_UNUSED)
		return -EINVAL;
	ipcp = &msq->msg_perm; 

	/* 
	 *  find message of correct type.
	 *  msgtyp = 0 => get first.
	 *  msgtyp > 0 => get first message of matching type.
	 *  msgtyp < 0 => get message with least type must be < abs(msgtype).  
	 */
	while (!nmsg) {
		if (msq->msg_perm.seq != (unsigned int) msqid / MSGMNI) {
			return -EIDRM;
		}
		if (ipcperms (ipcp, S_IRUGO)) {
			return -EACCES;
		}

		save_flags(flags);
		cli();
		if (msgtyp == 0) 
			nmsg = msq->msg_first;
		else if (msgtyp > 0) {
			if (msgflg & MSG_EXCEPT) { 
				for (tmsg = msq->msg_first; tmsg; 
				     tmsg = tmsg->msg_next)
					if (tmsg->msg_type != msgtyp)
						break;
				nmsg = tmsg;
			} else {
				for (tmsg = msq->msg_first; tmsg; 
				     tmsg = tmsg->msg_next)
					if (tmsg->msg_type == msgtyp)
						break;
				nmsg = tmsg;
			}
		} else {
			for (leastp = tmsg = msq->msg_first; tmsg; 
			     tmsg = tmsg->msg_next) 
				if (tmsg->msg_type < leastp->msg_type) 
					leastp = tmsg;
			if (leastp && leastp->msg_type <= - msgtyp)
				nmsg = leastp;
		}
		restore_flags(flags);
		
		if (nmsg) { /* done finding a message */
			if ((msgsz < nmsg->msg_ts) && !(msgflg & MSG_NOERROR)) {
				return -E2BIG;
			}
			msgsz = (msgsz > nmsg->msg_ts)? nmsg->msg_ts : msgsz;
			save_flags(flags);
			cli();
			if (nmsg ==  msq->msg_first)
				msq->msg_first = nmsg->msg_next;
			else {
				for (tmsg = msq->msg_first; tmsg; 
				     tmsg = tmsg->msg_next)
					if (tmsg->msg_next == nmsg) 
						break;
				tmsg->msg_next = nmsg->msg_next;
				if (nmsg == msq->msg_last)
					msq->msg_last = tmsg;
			}
			if (!(--msq->msg_qnum))
				msq->msg_last = msq->msg_first = NULL;
			
			msq->msg_rtime = CURRENT_TIME;
			msq->msg_lrpid = current->pid;
			msgbytes -= nmsg->msg_ts; 
			msghdrs--; 
			msq->msg_cbytes -= nmsg->msg_ts;
			restore_flags(flags);
			wake_up (&msq->wwait);
			put_user (nmsg->msg_type, &msgp->mtype);
			copy_to_user (msgp->mtext, nmsg->msg_spot, msgsz);
			kfree(nmsg);
			return msgsz;
		} else {  /* did not find a message */
			if (msgflg & IPC_NOWAIT) {
				return -ENOMSG;
			}
			if (signal_pending(current)) {
				return -EINTR; 
			}
			interruptible_sleep_on (&msq->rwait);
		}
	} /* end while */
	return -1;
}

asmlinkage int sys_msgsnd (int msqid, struct msgbuf *msgp, size_t msgsz, int msgflg)
{
	int ret;

	lock_kernel();
	ret = real_msgsnd(msqid, msgp, msgsz, msgflg);
	unlock_kernel();
	return ret;
}

asmlinkage int sys_msgrcv (int msqid, struct msgbuf *msgp, size_t msgsz,
	long msgtyp, int msgflg)
{
	int ret;

	lock_kernel();
	ret = real_msgrcv (msqid, msgp, msgsz, msgtyp, msgflg);
	unlock_kernel();
	return ret;
}

static int findkey (key_t key)
{
	int id;
	struct msqid_ds *msq;
	
	for (id = 0; id <= max_msqid; id++) {
		while ((msq = msgque[id]) == IPC_NOID) 
			interruptible_sleep_on (&msg_lock);
		if (msq == IPC_UNUSED)
			continue;
		if (key == msq->msg_perm.key)
			return id;
	}
	return -1;
}

static int newque (key_t key, int msgflg)
{
	int id;
	struct msqid_ds *msq;
	struct ipc_perm *ipcp;

	for (id = 0; id < MSGMNI; id++) 
		if (msgque[id] == IPC_UNUSED) {
			msgque[id] = (struct msqid_ds *) IPC_NOID;
			goto found;
		}
	return -ENOSPC;

found:
	msq = (struct msqid_ds *) kmalloc (sizeof (*msq), GFP_KERNEL);
	if (!msq) {
		msgque[id] = (struct msqid_ds *) IPC_UNUSED;
		wake_up (&msg_lock);
		return -ENOMEM;
	}
	ipcp = &msq->msg_perm;
	ipcp->mode = (msgflg & S_IRWXUGO);
	ipcp->key = key;
	ipcp->cuid = ipcp->uid = current->euid;
	ipcp->gid = ipcp->cgid = current->egid;
	msq->msg_perm.seq = msg_seq;
	msq->msg_first = msq->msg_last = NULL;
	msq->rwait = msq->wwait = NULL;
	msq->msg_cbytes = msq->msg_qnum = 0;
	msq->msg_lspid = msq->msg_lrpid = 0;
	msq->msg_stime = msq->msg_rtime = 0;
	msq->msg_qbytes = MSGMNB;
	msq->msg_ctime = CURRENT_TIME;
	if (id > max_msqid)
		max_msqid = id;
	msgque[id] = msq;
	used_queues++;
	wake_up (&msg_lock);
	return (unsigned int) msq->msg_perm.seq * MSGMNI + id;
}

asmlinkage int sys_msgget (key_t key, int msgflg)
{
	int id, ret = -EPERM;
	struct msqid_ds *msq;
	
	lock_kernel();
	if (key == IPC_PRIVATE) 
		ret = newque(key, msgflg);
	else if ((id = findkey (key)) == -1) { /* key not used */
		if (!(msgflg & IPC_CREAT))
			ret = -ENOENT;
		else
			ret = newque(key, msgflg);
	} else if (msgflg & IPC_CREAT && msgflg & IPC_EXCL) {
		ret = -EEXIST;
	} else {
		msq = msgque[id];
		if (msq == IPC_UNUSED || msq == IPC_NOID)
			ret = -EIDRM;
		else if (ipcperms(&msq->msg_perm, msgflg))
			ret = -EACCES;
		else
			ret = (unsigned int) msq->msg_perm.seq * MSGMNI + id;
	}
	unlock_kernel();
	return ret;
} 

static void freeque (int id)
{
	struct msqid_ds *msq = msgque[id];
	struct msg *msgp, *msgh;

	msq->msg_perm.seq++;
	msg_seq = (msg_seq+1) % ((unsigned)(1<<31)/MSGMNI); /* increment, but avoid overflow */
	msgbytes -= msq->msg_cbytes;
	if (id == max_msqid)
		while (max_msqid && (msgque[--max_msqid] == IPC_UNUSED));
	msgque[id] = (struct msqid_ds *) IPC_UNUSED;
	used_queues--;
	while (waitqueue_active(&msq->rwait) || waitqueue_active(&msq->wwait)) {
		wake_up (&msq->rwait); 
		wake_up (&msq->wwait);
		schedule(); 
	}
	for (msgp = msq->msg_first; msgp; msgp = msgh ) {
		msgh = msgp->msg_next;
		msghdrs--;
		kfree(msgp);
	}
	kfree(msq);
}

asmlinkage int sys_msgctl (int msqid, int cmd, struct msqid_ds *buf)
{
	int id, err = -EINVAL;
	struct msqid_ds *msq;
	struct msqid_ds tbuf;
	struct ipc_perm *ipcp;
	
	lock_kernel();
	if (msqid < 0 || cmd < 0)
		goto out;
	err = -EFAULT;
	switch (cmd) {
	case IPC_INFO: 
	case MSG_INFO: 
		if (!buf)
			goto out;
	{ 
		struct msginfo msginfo;
		msginfo.msgmni = MSGMNI;
		msginfo.msgmax = MSGMAX;
		msginfo.msgmnb = MSGMNB;
		msginfo.msgmap = MSGMAP;
		msginfo.msgpool = MSGPOOL;
		msginfo.msgtql = MSGTQL;
		msginfo.msgssz = MSGSSZ;
		msginfo.msgseg = MSGSEG;
		if (cmd == MSG_INFO) {
			msginfo.msgpool = used_queues;
			msginfo.msgmap = msghdrs;
			msginfo.msgtql = msgbytes;
		}
		err = verify_area (VERIFY_WRITE, buf, sizeof (struct msginfo));
		if (err)
			goto out;
		copy_to_user (buf, &msginfo, sizeof(struct msginfo));
		err = max_msqid;
		goto out;
	}
	case MSG_STAT:
		if (!buf)
			goto out;
		err = verify_area (VERIFY_WRITE, buf, sizeof (*buf));
		if (err)
			goto out;
		err = -EINVAL;
		if (msqid > max_msqid)
			goto out;
		msq = msgque[msqid];
		if (msq == IPC_UNUSED || msq == IPC_NOID)
			goto out;
		err = -EACCES;
		if (ipcperms (&msq->msg_perm, S_IRUGO))
			goto out;
		id = (unsigned int) msq->msg_perm.seq * MSGMNI + msqid;
		tbuf.msg_perm   = msq->msg_perm;
		tbuf.msg_stime  = msq->msg_stime;
		tbuf.msg_rtime  = msq->msg_rtime;
		tbuf.msg_ctime  = msq->msg_ctime;
		tbuf.msg_cbytes = msq->msg_cbytes;
		tbuf.msg_qnum   = msq->msg_qnum;
		tbuf.msg_qbytes = msq->msg_qbytes;
		tbuf.msg_lspid  = msq->msg_lspid;
		tbuf.msg_lrpid  = msq->msg_lrpid;
		copy_to_user (buf, &tbuf, sizeof(*buf));
		err = id;
		goto out;
	case IPC_SET:
		if (!buf)
			goto out;
		err = verify_area (VERIFY_READ, buf, sizeof (*buf));
		if (err)
			goto out;
		copy_from_user (&tbuf, buf, sizeof (*buf));
		break;
	case IPC_STAT:
		if (!buf)
			goto out;
		err = verify_area (VERIFY_WRITE, buf, sizeof(*buf));
		if (err)
			goto out;
		break;
	}

	id = (unsigned int) msqid % MSGMNI;
	msq = msgque [id];
	err = -EINVAL;
	if (msq == IPC_UNUSED || msq == IPC_NOID)
		goto out;
	err = -EIDRM;
	if (msq->msg_perm.seq != (unsigned int) msqid / MSGMNI)
		goto out;
	ipcp = &msq->msg_perm;

	switch (cmd) {
	case IPC_STAT:
		err = -EACCES;
		if (ipcperms (ipcp, S_IRUGO))
			goto out;
		tbuf.msg_perm   = msq->msg_perm;
		tbuf.msg_stime  = msq->msg_stime;
		tbuf.msg_rtime  = msq->msg_rtime;
		tbuf.msg_ctime  = msq->msg_ctime;
		tbuf.msg_cbytes = msq->msg_cbytes;
		tbuf.msg_qnum   = msq->msg_qnum;
		tbuf.msg_qbytes = msq->msg_qbytes;
		tbuf.msg_lspid  = msq->msg_lspid;
		tbuf.msg_lrpid  = msq->msg_lrpid;
		copy_to_user (buf, &tbuf, sizeof (*buf));
		err = 0;
		goto out;
	case IPC_SET:
		err = -EPERM;
		if (current->euid != ipcp->cuid && 
		    current->euid != ipcp->uid && !suser())
			goto out;
		if (tbuf.msg_qbytes > MSGMNB && !suser())
			goto out;
		msq->msg_qbytes = tbuf.msg_qbytes;
		ipcp->uid = tbuf.msg_perm.uid;
		ipcp->gid =  tbuf.msg_perm.gid;
		ipcp->mode = (ipcp->mode & ~S_IRWXUGO) | 
			(S_IRWXUGO & tbuf.msg_perm.mode);
		msq->msg_ctime = CURRENT_TIME;
		err = 0;
		goto out;
	case IPC_RMID:
		err = -EPERM;
		if (current->euid != ipcp->cuid && 
		    current->euid != ipcp->uid && !suser())
			goto out;

		freeque (id); 
		err = 0;
		goto out;
	default:
		err = -EINVAL;
		goto out;
	}
out:
	unlock_kernel();
	return err;
}

