/*
 *	iovec manipulation routines.
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */


#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/net.h>
#include <asm/segment.h>


extern inline int min(int x, int y)
{
	return x>y?y:x;
}

int verify_iovec(struct msghdr *m, struct iovec *iov, char *address, int mode)
{
	int err=0;
	int len=0;
	int ct;
	
	if(m->msg_name!=NULL)
	{
		if(mode==VERIFY_READ)
			err=move_addr_to_kernel(m->msg_name, m->msg_namelen, address);
		else
			err=verify_area(mode, m->msg_name, m->msg_namelen);
		if(err<0)
			return err;
	}
	if(m->msg_accrights!=NULL)
	{
		err=verify_area(mode, m->msg_accrights, m->msg_accrightslen);
		if(err)
			return err;
	}
	
	for(ct=0;ct<m->msg_iovlen;ct++)
	{
		err=verify_area(mode, m->msg_iov[ct].iov_base, m->msg_iov[ct].iov_len);
		if(err)
			return err;
		len+=m->msg_iov[ct].iov_len;
	}
	
	return len;
}

/*
 *	Copy kernel to iovec.
 */
 
void memcpy_toiovec(struct iovec *iov, unsigned char *kdata, int len)
{
	while(len>0)
	{
		memcpy_tofs(iov->iov_base, kdata,iov->iov_len);
		kdata+=iov->iov_len;
		len-=iov->iov_len;
		iov++;
	}
}

/*
 *	Copy iovec to kernel.
 */
 
void memcpy_fromiovec(unsigned char *kdata, struct iovec *iov, int len)
{
	int copy;
	while(len>0)
	{
		copy=min(len,iov->iov_len);
		memcpy_fromfs(kdata, iov->iov_base, copy);
		len-=copy;
		kdata+=copy;
		iov++;
	}
}

