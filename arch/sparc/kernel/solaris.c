/* solaris.c: Solaris binary emulation, whee...
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>

#include <asm/errno.h>
#include <asm/solerrno.h>

#if 0
/* Not used - actually translated in iBCS */
unsigned long solaris_xlatb_rorl[] = {
	0, SOL_EPERM, SOL_ENOENT, SOL_ESRCH, SOL_EINTR, SOL_EIO,
	SOL_ENXIO, SOL_E2BIG, SOL_ENOEXEC, SOL_EBADF, SOL_ECHILD,
	SOL_EAGAIN, SOL_ENOMEM, SOL_EACCES, SOL_EFAULT,
	SOL_ENOTBLK, SOL_EBUSY, SOL_EEXIST, SOL_EXDEV, SOL_ENODEV,
	SOL_ENOTDIR, SOL_EISDIR, SOL_EINVAL, SOL_ENFILE, SOL_EMFILE,
	SOL_ENOTTY, SOL_ETXTBSY, SOL_EFBIG, SOL_ENOSPC, SOL_ESPIPE,
	SOL_EROFS, SOL_EMLINK, SOL_EPIPE, SOL_EDOM, SOL_ERANGE,
	SOL_EWOULDBLOCK, SOL_EINPROGRESS, SOL_EALREADY, SOL_ENOTSOCK,
	SOL_EDESTADDRREQ, SOL_EMSGSIZE, SOL_EPROTOTYPE,	SOL_ENOPROTOOPT,
	SOL_EPROTONOSUPPORT, SOL_ESOCKTNOSUPPORT, SOL_EOPNOTSUPP,
	SOL_EPFNOSUPPORT, SOL_EAFNOSUPPORT, SOL_EADDRINUSE,
	SOL_EADDRNOTAVAIL, SOL_ENETDOWN, SOL_ENETUNREACH, SOL_ENETRESET,
	SOL_ECONNABORTED, SOL_ECONNRESET, SOL_ENOBUFS, SOL_EISCONN,
	SOL_ENOTCONN, SOL_ESHUTDOWN, SOL_ETOOMANYREFS, SOL_ETIMEDOUT,
	SOL_ECONNREFUSED, SOL_ELOOP, SOL_ENAMETOOLONG, SOL_EHOSTDOWN,
	SOL_EHOSTUNREACH, SOL_ENOTEMPTY, SOL_EUSERS, SOL_EUSERS,
	SOL_EDQUOT, SOL_ESTALE, SOL_EREMOTE, SOL_ENOSTR, SOL_ETIME,
	SOL_ENOSR, SOL_ENOMSG, SOL_EBADMSG, SOL_EIDRM, SOL_EDEADLK,
	SOL_ENOLCK, SOL_ENONET, SOL_EINVAL, SOL_ENOLINK, SOL_EADV,
	SOL_ESRMNT, SOL_ECOMM, SOL_EPROTO, SOL_EMULTIHOP, SOL_EINVAL,
	SOL_EREMCHG, SOL_ENOSYS
};
#endif

extern asmlinkage int sys_open(const char *,int,int);

asmlinkage int solaris_open(const char *filename, int flags, int mode)
{
	int newflags = flags & 0xf;

	flags &= ~0xf;
	if(flags & 0x8050)
		newflags |= FASYNC;
	if(flags & 0x80)
		newflags |= O_NONBLOCK;
	if(flags & 0x100)
		newflags |= O_CREAT;
	if(flags & 0x200)
		newflags |= O_TRUNC;
	if(flags & 0x400)
		newflags |= O_EXCL;
	if(flags & 0x800)
		newflags |= O_NOCTTY;
	return sys_open(filename, newflags, mode);
}


