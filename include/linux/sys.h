#ifndef _LINUX_SYS_H
#define _LINUX_SYS_H

/*
 * system call entry points ... but not all are defined
 */
#define NR_syscalls 256

/*
 * These are system calls with the same entry-point
 */
#define _sys_clone _sys_fork

/*
 * These are system calls that will be removed at some time
 * due to newer versions existing..
 */
#ifdef notdef
#define _sys_waitpid	_sys_old_syscall	/* _sys_wait4 */
#define _sys_olduname	_sys_old_syscall	/* _sys_newuname */
#define _sys_uname	_sys_old_syscall	/* _sys_newuname */
#define _sys_stat	_sys_old_syscall	/* _sys_newstat */
#define _sys_fstat	_sys_old_syscall	/* _sys_newfstat */
#define _sys_lstat	_sys_old_syscall	/* _sys_newlstat */
#define _sys_signal	_sys_old_syscall	/* _sys_sigaction */
#define _sys_sgetmask	_sys_old_syscall	/* _sys_sigprocmask */
#define _sys_ssetmask	_sys_old_syscall	/* _sys_sigprocmask */
#endif

/*
 * These are system calls that haven't been implemented yet
 * but have an entry in the table for future expansion..
 */
#define _sys_quotactl	_sys_ni_syscall
#define _sys_bdflush	_sys_ni_syscall

#endif
