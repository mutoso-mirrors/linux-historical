/* $Id: unistd.h,v 1.16 1995/12/29 23:14:26 miguel Exp $ */
#ifndef _SPARC_UNISTD_H
#define _SPARC_UNISTD_H

/*
 * System calls under the Sparc.
 *
 * Don't be scared by the ugly clobbers, it is the only way I can
 * think of right now to force the arguments into fixed registers
 * before the trap into the system call with gcc 'asm' statements.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 * SunOS compatability based upon preliminary work which is:
 *
 * Copyright (C) 1995 Adrian M. Rodriguez (adrian@remus.rutgers.edu)
 */

#define __NR_setup                0 /* Used only by init, to get system going.     */
#define __NR_exit                 1 /* Common                                      */
#define __NR_fork                 2 /* Common                                      */
#define __NR_read                 3 /* Common                                      */
#define __NR_write                4 /* Common                                      */
#define __NR_open                 5 /* Common                                      */
#define __NR_close                6 /* Common                                      */
#define __NR_wait4                7 /* Common                                      */
#define __NR_creat                8 /* Common                                      */
#define __NR_link                 9 /* Common                                      */
#define __NR_unlink              10 /* Common                                      */
#define __NR_execv               11 /* SunOS Specific                              */
#define __NR_chdir               12 /* Common                                      */
/* #define __NR_ni_syscall       13    ENOSYS under SunOS                          */
#define __NR_mknod               14 /* Common                                      */
#define __NR_chmod               15 /* Common                                      */
#define __NR_chown               16 /* Common                                      */
#define __NR_brk                 17 /* Common                                      */
/* #define __NR_ni_syscall       18    ENOSYS under SunOS                          */
#define __NR_lseek               19 /* Common                                      */
#define __NR_getpid              20 /* Common                                      */
/* #define __NR_ni_syscall       21    ENOSYS under SunOS                          */
/* #define __NR_ni_syscall       22    ENOSYS under SunOS                          */
#define __NR_setuid              23 /* Implemented via setreuid in SunOS           */
#define __NR_getuid              24 /* Common                                      */
/* #define __NR_ni_syscall       25    ENOSYS under SunOS                          */
#define __NR_ptrace              26 /* Common                                      */
#define __NR_alarm               27 /* Implemented via setitimer in SunOS          */
/* #define __NR_ni_syscall       28    ENOSYS under SunOS                          */
#define __NR_pause               29 /* Is sigblock(0)->sigpause() in SunOS         */
#define __NR_utime               30 /* Implemented via utimes() under SunOS        */
#define __NR_stty                31 /* Implemented via ioctl() under SunOS         */
#define __NR_gtty                32 /* Implemented via ioctl() under SunOS         */
#define __NR_access              33 /* Common                                      */
#define __NR_nice                34 /* Implemented via get/setpriority() in SunOS  */
#define __NR_ftime               35 /* Implemented via gettimeofday() in SunOS     */
#define __NR_sync                36 /* Common                                      */
#define __NR_kill                37 /* Common                                      */
#define __NR_stat                38 /* Common                                      */
/* #define __NR_ni_syscall       39    ENOSYS under SunOS                          */
#define __NR_lstat               40 /* Common                                      */
#define __NR_dup                 41 /* Common                                      */
#define __NR_pipe                42 /* Common                                      */
#define __NR_times               43 /* Implemented via getrusage() in SunOS        */
#define __NR_profil              44 /* Common                                      */
/* #define __NR_ni_syscall       45    ENOSYS under SunOS                          */
#define __NR_setgid              46 /* Implemented via setregid() in SunOS         */
#define __NR_getgid              47 /* Common                                      */
#define __NR_signal              48 /* Implemented via sigvec() in SunOS           */
#define __NR_geteuid             49 /* SunOS calls getuid()                        */
#define __NR_getegid             50 /* SunOS calls getgid()                        */
#define __NR_acct                51 /* Common                                      */
/* #define __NR_ni_syscall       52    ENOSYS under SunOS                          */
#define __NR_mctl                53 /* SunOS specific                              */
#define __NR_ioctl               54 /* Common                                      */
#define __NR_reboot              55 /* Common                                      */
/* #define __NR_ni_syscall       56    ENOSYS under SunOS                          */
#define __NR_symlink             57 /* Common                                      */
#define __NR_readlink            58 /* Common                                      */
#define __NR_execve              59 /* Common                                      */
#define __NR_umask               60 /* Common                                      */
#define __NR_chroot              61 /* Common                                      */
#define __NR_fstat               62 /* Common                                      */
/* #define __NR_ni_syscall       63    ENOSYS under SunOS                          */
#define __NR_getpagesize         64 /* Common                                      */
#define __NR_msync               65 /* Common in newer 1.3.x revs...               */
/* #define __NR_ni_syscall       66    ENOSYS under SunOS                          */
/* #define __NR_ni_syscall       67    ENOSYS under SunOS                          */
/* #define __NR_ni_syscall       68    ENOSYS under SunOS                          */
#define __NR_sbrk                69 /* SunOS Specific                              */
#define __NR_sstk                70 /* SunOS Specific                              */
#define __NR_mmap                71 /* Common                                      */
#define __NR_vadvise             72 /* SunOS Specific                              */
#define __NR_munmap              73 /* Common                                      */
#define __NR_mprotect            74 /* Common                                      */
#define __NR_madvise             75 /* SunOS Specific                              */
#define __NR_vhangup             76 /* Common                                      */
/* #define __NR_ni_syscall       77    ENOSYS under SunOS                          */
#define __NR_mincore             78 /* SunOS Specific                              */
#define __NR_getgroups           79 /* Common                                      */
#define __NR_setgroups           80 /* Common                                      */
#define __NR_getpgrp             81 /* Common                                      */
#define __NR_setpgrp             82 /* setpgid, same difference...                 */
#define __NR_setitimer           83 /* Common                                      */
/* #define __NR_ni_syscall       84    ENOSYS under SunOS                          */
#define __NR_swapon              85 /* Common                                      */
#define __NR_getitimer           86 /* Common                                      */
#define __NR_gethostname         87 /* SunOS Specific                              */
#define __NR_sethostname         88 /* Common                                      */
#define __NR_getdtablesize       89 /* SunOS Specific                              */
#define __NR_dup2                90 /* Common                                      */
#define __NR_getdopt             91 /* SunOS Specific                              */
#define __NR_fcntl               92 /* Common                                      */
#define __NR_select              93 /* Common                                      */
#define __NR_setdopt             94 /* SunOS Specific                              */
#define __NR_fsync               95 /* Common                                      */
#define __NR_setpriority         96 /* Common                                      */
#define __NR_socket              97 /* SunOS Specific                              */
#define __NR_connect             98 /* SunOS Specific                              */
#define __NR_accept              99 /* SunOS Specific                              */
#define __NR_getpriority        100 /* Common                                      */
#define __NR_send               101 /* SunOS Specific                              */
#define __NR_recv               102 /* SunOS Specific                              */
/* #define __NR_ni_syscall      103    ENOSYS under SunOS                          */
#define __NR_bind               104 /* SunOS Specific                              */
#define __NR_setsockopt         105 /* SunOS Specific                              */
#define __NR_listen             106 /* SunOS Specific                              */
/* #define __NR_ni_syscall      107    ENOSYS under SunOS                          */
#define __NR_sigvec             108 /* SunOS Specific                              */
#define __NR_sigblock           109 /* SunOS Specific                              */
#define __NR_sigsetmask         110 /* SunOS Specific                              */
#define __NR_sigpause           111 /* SunOS Specific                              */
#define __NR_sigstack           112 /* SunOS Specific                              */
#define __NR_recvmsg            113 /* SunOS Specific                              */
#define __NR_sendmsg            114 /* SunOS Specific                              */
#define __NR_vtrace             115 /* SunOS Specific                              */
#define __NR_gettimeofday       116 /* Common                                      */
#define __NR_getrusage          117 /* Common                                      */
#define __NR_getsockopt         118 /* SunOS Specific                              */
/* #define __NR_ni_syscall      119    ENOSYS under SunOS                          */
#define __NR_readv              120 /* Common                                      */
#define __NR_writev             121 /* Common                                      */
#define __NR_settimeofday       122 /* Common                                      */
#define __NR_fchown             123 /* Common                                      */
#define __NR_fchmod             124 /* Common                                      */
#define __NR_recvfrom           125 /* SunOS Specific                              */
#define __NR_setreuid           126 /* Common                                      */
#define __NR_setregid           127 /* Common                                      */
#define __NR_rename             128 /* Common                                      */
#define __NR_truncate           129 /* Common                                      */
#define __NR_ftruncate          130 /* Common                                      */
#define __NR_flock              131 /* Common                                      */
/* #define __NR_ni_syscall      132    ENOSYS under SunOS                          */
#define __NR_sendto             133 /* SunOS Specific                              */
#define __NR_shutdown           134 /* SunOS Specific                              */
#define __NR_socketpair         135 /* SunOS Specific                              */
#define __NR_mkdir              136 /* Common                                      */
#define __NR_rmdir              137 /* Common                                      */
#define __NR_utimes             138 /* SunOS Specific                              */
/* #define __NR_ni_syscall      139    ENOSYS under SunOS                          */
#define __NR_adjtime            140 /* SunOS Specific                              */
#define __NR_getpeername        141 /* SunOS Specific                              */
#define __NR_gethostid          142 /* SunOS Specific                              */
/* #define __NR_ni_syscall      143    ENOSYS under SunOS                          */
#define __NR_getrlimit          144 /* Common                                      */
#define __NR_setrlimit          145 /* Common                                      */
#define __NR_killpg             146 /* SunOS Specific                              */
/* #define __NR_ni_syscall      147    ENOSYS under SunOS                          */
/* #define __NR_ni_syscall      148    ENOSYS under SunOS                          */
/* #define __NR_ni_syscall      149    ENOSYS under SunOS                          */
#define __NR_getsockname        150 /* SunOS Specific                              */
#define __NR_getmsg             151 /* SunOS Specific                              */
#define __NR_putmsg             152 /* SunOS Specific                              */
#define __NR_poll               153 /* SunOS Specific                              */
/* #define __NR_ni_syscall      154    ENOSYS under SunOS                          */
#define __NR_nfssvc             155 /* SunOS Specific                              */
#define __NR_getdirentries      156 /* SunOS Specific                              */
#define __NR_statfs             157 /* Common                                      */
#define __NR_fstatfs            158 /* Common                                      */
#define __NR_umount             159 /* Common                                      */
#define __NR_async_daemon       160 /* SunOS Specific                              */
#define __NR_getfh              161 /* SunOS Specific                              */
#define __NR_getdomainname      162 /* SunOS Specific                              */
#define __NR_setdomainname      163 /* Common                                      */
/* #define __NR_ni_syscall      164    ENOSYS under SunOS                          */
#define __NR_quotactl           165 /* Common                                      */
#define __NR_exportfs           166 /* SunOS Specific                              */
#define __NR_mount              167 /* Common                                      */
#define __NR_ustat              168 /* Common                                      */
#define __NR_semsys             169 /* SunOS Specific                              */
#define __NR_msgsys             170 /* SunOS Specific                              */
#define __NR_shmsys             171 /* SunOS Specific                              */
#define __NR_auditsys           172 /* SunOS Specific                              */
#define __NR_rfssys             173 /* SunOS Specific                              */
#define __NR_getdents           174 /* Common                                      */
#define __NR_setsid             175 /* Common                                      */
#define __NR_fchdir             176 /* Common                                      */
#define __NR_fchroot            177 /* SunOS Specific                              */
#define __NR_vpixsys            178 /* SunOS Specific                              */
#define __NR_aioread            179 /* SunOS Specific                              */
#define __NR_aiowrite           180 /* SunOS Specific                              */
#define __NR_aiowait            181 /* SunOS Specific                              */
#define __NR_aiocancel          182 /* SunOS Specific                              */
#define __NR_sigpending         183 /* Common                                      */
/* #define __NR_ni_syscall      184    ENOSYS under SunOS                          */
#define __NR_setpgid            185 /* Common                                      */
#define __NR_pathconf           186 /* SunOS Specific                              */
#define __NR_fpathconf          187 /* SunOS Specific                              */
#define __NR_sysconf            188 /* SunOS Specific                              */
#define __NR_uname              189 /* Linux Specific                              */
#define __NR_init_module        190 /* Linux Specific                              */
#define __NR_personality        191 /* Linux Specific                              */
#define __NR_prof               192 /* Linux Specific                              */
#define __NR_break              193 /* Linux Specific                              */
#define __NR_lock               194 /* Linux Specific                              */
#define __NR_mpx                195 /* Linux Specific                              */
#define __NR_ulimit             196 /* Linux Specific                              */
#define __NR_getppid            197 /* Linux Specific                              */
#define __NR_sigaction          198 /* Linux Specific                              */
#define __NR_sgetmask           199 /* Linux Specific                              */
#define __NR_ssetmask           200 /* Linux Specific                              */
#define __NR_sigsuspend         201 /* Linux Specific                              */
#define __NR_oldlstat           202 /* Linux Specific                              */
#define __NR_uselib             203 /* Linux Specific                              */
#define __NR_readdir            204 /* Linux Specific                              */
#define __NR_ioperm             205 /* Linux Specific - i386 specific, unused      */
#define __NR_socketcall         206 /* Linux Specific                              */
#define __NR_syslog             207 /* Linux Specific                              */
#define __NR_olduname           208 /* Linux Specific                              */
#define __NR_iopl               209 /* Linux Specific - i386 specific, unused      */
#define __NR_idle               210 /* Linux Specific                              */
#define __NR_vm86               211 /* Linux Specific - i386 specific, unused      */
#define __NR_waitpid            212 /* Linux Specific                              */
#define __NR_swapoff            213 /* Linux Specific                              */
#define __NR_sysinfo            214 /* Linux Specific                              */
#define __NR_ipc                215 /* Linux Specific                              */
#define __NR_sigreturn          216 /* Linux Specific                              */
#define __NR_clone              217 /* Linux Specific                              */
#define __NR_modify_ldt         218 /* Linux Specific - i386 specific, unused      */
#define __NR_adjtimex           219 /* Linux Specific                              */
#define __NR_sigprocmask        220 /* Linux Specific                              */
#define __NR_create_module      221 /* Linux Specific                              */
#define __NR_delete_module      222 /* Linux Specific                              */
#define __NR_get_kernel_syms    223 /* Linux Specific                              */
#define __NR_getpgid            224 /* Linux Specific                              */
#define __NR_bdflush            225 /* Linux Specific                              */
#define __NR_sysfs              226 /* Linux Specific                              */
#define __NR_afs_syscall        227 /* Linux Specific                              */
#define __NR_setfsuid           228 /* Linux Specific                              */
#define __NR_setfsgid           229 /* Linux Specific                              */
#define __NR__newselect         230 /* Linux Specific                              */
#define __NR_time               231 /* Linux Specific                              */
#define __NR_oldstat            232 /* Linux Specific                              */
#define __NR_stime              233 /* Linux Specific                              */
#define __NR_oldfstat           234 /* Linux Specific                              */
#define __NR_phys               235 /* Linux Specific                              */
#define __NR__llseek            236 /* Linux Specific                              */
#define __NR_mlock              237
#define __NR_munlock            238
#define __NR_mlockall           239
#define __NR_munlockall         240

#define _syscall0(type,name) \
type name(void) \
{ \
long __res; \
__asm__ volatile ("or %%g0, %0, %%g1\n\t" \
		  "t 0x10\n\t" \
		  "bcc 1f\n\t" \
		  "or %%g0, %%o0, %0\n\t" \
		  "sub %%g0, %%o0, %0\n\t" \
		  "1:\n\t" \
		  : "=r" (__res)\
		  : "0" (__NR_##name) \
		  : "g1"); \
if (__res >= 0) \
    return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall1(type,name,type1,arg1) \
type name(type1 arg1) \
{ \
long __res; \
__asm__ volatile ("or %%g0, %0, %%g1\n\t" \
		  "or %%g0, %1, %%o0\n\t" \
		  "t 0x10\n\t" \
		  "bcc 1f\n\t" \
		  "or %%g0, %%o0, %0\n\t" \
		  "sub %%g0, %%o0, %0\n\t" \
		  "1:\n\t" \
		  : "=r" (__res), "=r" ((long)(arg1)) \
		  : "0" (__NR_##name),"1" ((long)(arg1)) \
		  : "g1", "o0"); \
if (__res >= 0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall2(type,name,type1,arg1,type2,arg2) \
type name(type1 arg1,type2 arg2) \
{ \
long __res; \
__asm__ volatile ("or %%g0, %0, %%g1\n\t" \
		  "or %%g0, %1, %%o0\n\t" \
		  "or %%g0, %2, %%o1\n\t" \
		  "t 0x10\n\t" \
		  "bcc 1f\n\t" \
		  "or %%g0, %%o0, %0\n\t" \
		  "sub %%g0,%%o0,%0\n\t" \
		  "1:\n\t" \
		  : "=r" (__res), "=r" ((long)(arg1)), "=r" ((long)(arg2)) \
		  : "0" (__NR_##name),"1" ((long)(arg1)),"2" ((long)(arg2)) \
		  : "g1", "o0", "o1"); \
if (__res >= 0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3) \
type name(type1 arg1,type2 arg2,type3 arg3) \
{ \
long __res; \
__asm__ volatile ("or %%g0, %0, %%g1\n\t" \
		  "or %%g0, %1, %%o0\n\t" \
		  "or %%g0, %2, %%o1\n\t" \
		  "or %%g0, %3, %%o2\n\t" \
		  "t 0x10\n\t" \
		  "bcc 1f\n\t" \
		  "or %%g0, %%o0, %0\n\t" \
		  "sub %%g0, %%o0, %0\n\t" \
		  "1:\n\t" \
		  : "=r" (__res), "=r" ((long)(arg1)), "=r" ((long)(arg2)), \
		    "=r" ((long)(arg3)) \
		  : "0" (__NR_##name), "1" ((long)(arg1)), "2" ((long)(arg2)), \
		    "3" ((long)(arg3)) \
		  : "g1", "o0", "o1", "o2"); \
if (__res>=0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}

#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
type name (type1 arg1, type2 arg2, type3 arg3, type4 arg4) \
{ \
long __res; \
__asm__ volatile ("or %%g0, %0, %%g1\n\t" \
		  "or %%g0, %1, %%o0\n\t" \
		  "or %%g0, %2, %%o1\n\t" \
		  "or %%g0, %3, %%o2\n\t" \
		  "or %%g0, %4, %%o3\n\t" \
		  "t 0x10\n\t" \
		  "bcc 1f\n\t" \
		  "or %%g0, %%o0, %0\n\t" \
		  "sub %%g0,%%o0, %0\n\t" \
		  "1:\n\t" \
		  : "=r" (__res), "=r" ((long)(arg1)), "=r" ((long)(arg2)), \
		    "=r" ((long)(arg3)), "=r" ((long)(arg4)) \
		  : "0" (__NR_##name),"1" ((long)(arg1)),"2" ((long)(arg2)), \
		    "3" ((long)(arg3)),"4" ((long)(arg4)) \
		  : "g1", "o0", "o1", "o2", "o3"); \
if (__res>=0) \
	return (type) __res; \
errno = -__res; \
return -1; \
} 

#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4, \
	  type5,arg5) \
type name (type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) \
{ \
long __res; \
__asm__ volatile ("or %%g0, %0, %%g1\n\t" \
		  "or %%g0, %1, %%o0\n\t" \
		  "or %%g0, %2, %%o1\n\t" \
		  "or %%g0, %3, %%o2\n\t" \
		  "or %%g0, %4, %%o3\n\t" \
		  "or %%g0, %5, %%o4\n\t" \
		  "t 0x10\n\t" \
		  "or %%g0, %%o0, %0\n\t" \
		  : "=r" (__res), "=r" ((long)(arg1)), "=r" ((long)(arg2)), \
		    "=r" ((long)(arg3)), "=r" ((long)(arg4)), "=r" ((long)(arg5)) \
		  : "0" (__NR_##name),"1" ((long)(arg1)),"2" ((long)(arg2)), \
		    "3" ((long)(arg3)),"4" ((long)(arg4)),"5" ((long)(arg5)) \
		  : "g1", "o0", "o1", "o2", "o3", "o4"); \
if (__res>=0) \
	return (type) __res; \
errno = -__res; \
return -1; \
}
#ifdef __KERNEL_SYSCALLS__

/*
 * we need this inline - forking from kernel space will result
 * in NO COPY ON WRITE (!!!), until an execve is executed. This
 * is no problem, but for the stack. This is handled by not letting
 * main() use the stack at all after fork(). Thus, no function
 * calls - which means inline code for fork too, as otherwise we
 * would use the stack upon exit from 'fork()'.
 *
 * Actually only pause and fork are needed inline, so that there
 * won't be any messing with the stack from main(), but we define
 * some others too.
 */
#define __NR__exit __NR_exit
/* static inline _syscall0(int,idle) */
static inline _syscall0(int,fork)
static inline _syscall2(int,clone,unsigned long,flags,char *,ksp)
static inline _syscall0(int,pause)
/* static inline _syscall0(int,setup) */
static inline _syscall0(int,sync)
static inline _syscall0(pid_t,setsid)
static inline _syscall3(int,write,int,fd,const char *,buf,off_t,count)
static inline _syscall1(int,dup,int,fd)
static inline _syscall3(int,execve,const char *,file,char **,argv,char **,envp)
static inline _syscall3(int,open,const char *,file,int,flag,int,mode)
static inline _syscall1(int,close,int,fd)
static inline _syscall1(int,_exit,int,exitcode)
static inline _syscall3(pid_t,waitpid,pid_t,pid,int *,wait_stat,int,options)

extern void sys_idle(void);
static inline void idle(void)
{
	sys_idle();
}

extern int sys_setup(void);
static inline int setup(void)
{
	return sys_setup();
}

extern int sys_waitpid(int, int *, int);
static inline pid_t wait(int * wait_stat)
{
	long retval;
	retval = waitpid(-1,wait_stat,0);
	return retval;
}

/*
 * This is the mechanism for creating a new kernel thread.
 *
 * NOTE! Only a kernel-only process(ie the swapper or direct descendants
 * who haven't done an "execve()") should use this: it will work within
 * a system call from a "real" process, but the process memory space will
 * not be free'd until both the parent and the child have exited.
 */
static inline pid_t kernel_thread(int (*fn)(void *), void * arg, unsigned long flags)
{
	long retval;

	__asm__ __volatile("mov %4, %%g2\n\t"    /* Set aside fn ptr... */
			   "mov %5, %%g3\n\t"    /* and arg. */
			   "mov %1, %%g1\n\t"
			   "mov %2, %%o0\n\t"    /* Clone flags. */
			   "mov 0, %%o1\n\t"     /* usp arg == 0 */
			   "t 0x10\n\t"          /* Linux/Sparc clone(). */
			   "cmp %%o1, 0\n\t"
			   "be 1f\n\t"           /* The parent, just return. */
			   " nop\n\t"            /* Delay slot. */
			   "jmpl %%g2, %%o7\n\t" /* Call the function. */
			   " mov %%g3, %%o0\n\t" /* Get back the arg in delay. */
			   "mov %3, %%g1\n\t"
			   "t 0x10\n\t"          /* Linux/Sparc exit(). */
			   /* Notreached by child. */
			   "1: mov %%o0, %0\n\t" :
			   "=r" (retval) :
			   "i" (__NR_clone), "r" (flags | CLONE_VM),
			   "i" (__NR_exit),  "r" (fn), "r" (arg) :
			   "g1", "g2", "g3", "o0", "o1", "memory");
	return retval;
}

#endif /* __KERNEL_SYSCALLS__ */

/* sysconf options, for SunOS compatibility */
#define   _SC_ARG_MAX             1
#define   _SC_CHILD_MAX           2
#define   _SC_CLK_TCK             3
#define   _SC_NGROUPS_MAX         4
#define   _SC_OPEN_MAX            5
#define   _SC_JOB_CONTROL         6
#define   _SC_SAVED_IDS           7
#define   _SC_VERSION             8

#endif /* _SPARC_UNISTD_H */
