#ifndef _ASM_PPC_UNISTD_H_
#define _ASM_PPC_UNISTD_H_

/*
 * This file contains the system call numbers.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define __NR_exit		  1
#define __NR_fork		  2
#define __NR_read		  3
#define __NR_write		  4
#define __NR_open		  5
#define __NR_close		  6
#define __NR_waitpid		  7
#define __NR_creat		  8
#define __NR_link		  9
#define __NR_unlink		 10
#define __NR_execve		 11
#define __NR_chdir		 12
#define __NR_time		 13
#define __NR_mknod		 14
#define __NR_chmod		 15
#define __NR_lchown		 16
#define __NR_break		 17
#define __NR_oldstat		 18
#define __NR_lseek		 19
#define __NR_getpid		 20
#define __NR_mount		 21
#define __NR_umount		 22
#define __NR_setuid		 23
#define __NR_getuid		 24
#define __NR_stime		 25
#define __NR_ptrace		 26
#define __NR_alarm		 27
#define __NR_oldfstat		 28
#define __NR_pause		 29
#define __NR_utime		 30
#define __NR_stty		 31
#define __NR_gtty		 32
#define __NR_access		 33
#define __NR_nice		 34
#define __NR_ftime		 35
#define __NR_sync		 36
#define __NR_kill		 37
#define __NR_rename		 38
#define __NR_mkdir		 39
#define __NR_rmdir		 40
#define __NR_dup		 41
#define __NR_pipe		 42
#define __NR_times		 43
#define __NR_prof		 44
#define __NR_brk		 45
#define __NR_setgid		 46
#define __NR_getgid		 47
#define __NR_signal		 48
#define __NR_geteuid		 49
#define __NR_getegid		 50
#define __NR_acct		 51
#define __NR_umount2		 52
#define __NR_lock		 53
#define __NR_ioctl		 54
#define __NR_fcntl		 55
#define __NR_mpx		 56
#define __NR_setpgid		 57
#define __NR_ulimit		 58
#define __NR_oldolduname	 59
#define __NR_umask		 60
#define __NR_chroot		 61
#define __NR_ustat		 62
#define __NR_dup2		 63
#define __NR_getppid		 64
#define __NR_getpgrp		 65
#define __NR_setsid		 66
#define __NR_sigaction		 67
#define __NR_sgetmask		 68
#define __NR_ssetmask		 69
#define __NR_setreuid		 70
#define __NR_setregid		 71
#define __NR_sigsuspend		 72
#define __NR_sigpending		 73
#define __NR_sethostname	 74
#define __NR_setrlimit		 75
#define __NR_getrlimit		 76
#define __NR_getrusage		 77
#define __NR_gettimeofday	 78
#define __NR_settimeofday	 79
#define __NR_getgroups		 80
#define __NR_setgroups		 81
#define __NR_select		 82
#define __NR_symlink		 83
#define __NR_oldlstat		 84
#define __NR_readlink		 85
#define __NR_uselib		 86
#define __NR_swapon		 87
#define __NR_reboot		 88
#define __NR_readdir		 89
#define __NR_mmap		 90
#define __NR_munmap		 91
#define __NR_truncate		 92
#define __NR_ftruncate		 93
#define __NR_fchmod		 94
#define __NR_fchown		 95
#define __NR_getpriority	 96
#define __NR_setpriority	 97
#define __NR_profil		 98
#define __NR_statfs		 99
#define __NR_fstatfs		100
#define __NR_ioperm		101
#define __NR_socketcall		102
#define __NR_syslog		103
#define __NR_setitimer		104
#define __NR_getitimer		105
#define __NR_stat		106
#define __NR_lstat		107
#define __NR_fstat		108
#define __NR_olduname		109
#define __NR_iopl		110
#define __NR_vhangup		111
#define __NR_idle		112
#define __NR_vm86		113
#define __NR_wait4		114
#define __NR_swapoff		115
#define __NR_sysinfo		116
#define __NR_ipc		117
#define __NR_fsync		118
#define __NR_sigreturn		119
#define __NR_clone		120
#define __NR_setdomainname	121
#define __NR_uname		122
#define __NR_modify_ldt		123
#define __NR_adjtimex		124
#define __NR_mprotect		125
#define __NR_sigprocmask	126
#define __NR_create_module	127
#define __NR_init_module	128
#define __NR_delete_module	129
#define __NR_get_kernel_syms	130
#define __NR_quotactl		131
#define __NR_getpgid		132
#define __NR_fchdir		133
#define __NR_bdflush		134
#define __NR_sysfs		135
#define __NR_personality	136
#define __NR_afs_syscall	137 /* Syscall for Andrew File System */
#define __NR_setfsuid		138
#define __NR_setfsgid		139
#define __NR__llseek		140
#define __NR_getdents		141
#define __NR__newselect		142
#define __NR_flock		143
#define __NR_msync		144
#define __NR_readv		145
#define __NR_writev		146
#define __NR_getsid		147
#define __NR_fdatasync		148
#define __NR__sysctl		149
#define __NR_mlock		150
#define __NR_munlock		151
#define __NR_mlockall		152
#define __NR_munlockall		153
#define __NR_sched_setparam		154
#define __NR_sched_getparam		155
#define __NR_sched_setscheduler		156
#define __NR_sched_getscheduler		157
#define __NR_sched_yield		158
#define __NR_sched_get_priority_max	159
#define __NR_sched_get_priority_min	160
#define __NR_sched_rr_get_interval	161
#define __NR_nanosleep		162
#define __NR_mremap		163
#define __NR_setresuid		164
#define __NR_getresuid		165
#define __NR_query_module	166
#define __NR_poll		167
#define __NR_nfsservctl		168
#define __NR_setresgid		169
#define __NR_getresgid		170
#define __NR_prctl		171
#define __NR_rt_sigreturn	172
#define __NR_rt_sigaction	173
#define __NR_rt_sigprocmask	174
#define __NR_rt_sigpending	175
#define __NR_rt_sigtimedwait	176
#define __NR_rt_sigqueueinfo	177
#define __NR_rt_sigsuspend	178
#define __NR_pread		179
#define __NR_pwrite		180
#define __NR_chown		181
#define __NR_getcwd		182
#define __NR_capget		183
#define __NR_capset		184
#define __NR_sigaltstack	185
#define __NR_sendfile		186
#define __NR_getpmsg		187	/* some people actually want streams */
#define __NR_putpmsg		188	/* some people actually want streams */
#define __NR_vfork		189
#define __NR_ugetrlimit		190	/* SuS compliant getrlimit */
#define __NR_mmap2		192
#define __NR_truncate64		193
#define __NR_ftruncate64	194
#define __NR_stat64		195
#define __NR_lstat64		196
#define __NR_fstat64		197
#define __NR_pciconfig_read	198
#define __NR_pciconfig_write	199
#define __NR_pciconfig_iobase	200
#define __NR_multiplexer	201
#define __NR_getdents64		202
#define __NR_pivot_root		203
#define __NR_fcntl64		204
#define __NR_madvise		205
#define __NR_mincore		206
#define __NR_gettid		207
#define __NR_tkill		208
#define __NR_setxattr		209
#define __NR_lsetxattr		210
#define __NR_fsetxattr		211
#define __NR_getxattr		212
#define __NR_lgetxattr		213
#define __NR_fgetxattr		214
#define __NR_listxattr		215
#define __NR_llistxattr		216
#define __NR_flistxattr		217
#define __NR_removexattr	218
#define __NR_lremovexattr	219
#define __NR_fremovexattr	220
#define __NR_futex		221

#if 0
/* Remind paulus to add these into ppc32 */
__NR_security
__NR_readahead
#endif

#define __NR(n)	#n


#define __syscall_return(type) \
	return (__sc_err & 0x10000000 ? errno = __sc_ret, __sc_ret = -1 : 0), \
	       (type) __sc_ret

#define __syscall_clobbers \
	"r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12"

#define _syscall0(type,name)						\
type name(void)								\
{									\
	unsigned long __sc_ret, __sc_err;				\
	{								\
		register unsigned long __sc_0 __asm__ ("r0");		\
		register unsigned long __sc_3 __asm__ ("r3");		\
									\
		__sc_0 = __NR_##name;					\
		__asm__ __volatile__					\
			("sc           \n\t"				\
			 "mfcr %1      "				\
			: "=&r" (__sc_3), "=&r" (__sc_0)		\
			: "0"   (__sc_3), "1"   (__sc_0)		\
			: __syscall_clobbers);				\
		__sc_ret = __sc_3;					\
		__sc_err = __sc_0;					\
	}								\
	__syscall_return (type);					\
}

#define _syscall1(type,name,type1,arg1)					\
type name(type1 arg1)							\
{									\
	unsigned long __sc_ret, __sc_err;				\
	{								\
		register unsigned long __sc_0 __asm__ ("r0");		\
		register unsigned long __sc_3 __asm__ ("r3");		\
									\
		__sc_3 = (unsigned long) (arg1);			\
		__sc_0 = __NR_##name;					\
		__asm__ __volatile__					\
			("sc           \n\t"				\
			 "mfcr %1      "				\
			: "=&r" (__sc_3), "=&r" (__sc_0)		\
			: "0"   (__sc_3), "1"   (__sc_0)		\
			: __syscall_clobbers);				\
		__sc_ret = __sc_3;					\
		__sc_err = __sc_0;					\
	}								\
	__syscall_return (type);					\
}

#define _syscall2(type,name,type1,arg1,type2,arg2)			\
type name(type1 arg1, type2 arg2)					\
{									\
	unsigned long __sc_ret, __sc_err;				\
	{								\
		register unsigned long __sc_0 __asm__ ("r0");		\
		register unsigned long __sc_3 __asm__ ("r3");		\
		register unsigned long __sc_4 __asm__ ("r4");		\
									\
		__sc_3 = (unsigned long) (arg1);			\
		__sc_4 = (unsigned long) (arg2);			\
		__sc_0 = __NR_##name;					\
		__asm__ __volatile__					\
			("sc           \n\t"				\
			 "mfcr %1      "				\
			: "=&r" (__sc_3), "=&r" (__sc_0)		\
			: "0"   (__sc_3), "1"   (__sc_0),		\
			  "r"   (__sc_4)				\
			: __syscall_clobbers);				\
		__sc_ret = __sc_3;					\
		__sc_err = __sc_0;					\
	}								\
	__syscall_return (type);					\
}

#define _syscall3(type,name,type1,arg1,type2,arg2,type3,arg3)		\
type name(type1 arg1, type2 arg2, type3 arg3)				\
{									\
	unsigned long __sc_ret, __sc_err;				\
	{								\
		register unsigned long __sc_0 __asm__ ("r0");		\
		register unsigned long __sc_3 __asm__ ("r3");		\
		register unsigned long __sc_4 __asm__ ("r4");		\
		register unsigned long __sc_5 __asm__ ("r5");		\
									\
		__sc_3 = (unsigned long) (arg1);			\
		__sc_4 = (unsigned long) (arg2);			\
		__sc_5 = (unsigned long) (arg3);			\
		__sc_0 = __NR_##name;					\
		__asm__ __volatile__					\
			("sc           \n\t"				\
			 "mfcr %1      "				\
			: "=&r" (__sc_3), "=&r" (__sc_0)		\
			: "0"   (__sc_3), "1"   (__sc_0),		\
			  "r"   (__sc_4),				\
			  "r"   (__sc_5)				\
			: __syscall_clobbers);				\
		__sc_ret = __sc_3;					\
		__sc_err = __sc_0;					\
	}								\
	__syscall_return (type);					\
}

#define _syscall4(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
type name(type1 arg1, type2 arg2, type3 arg3, type4 arg4)		\
{									\
	unsigned long __sc_ret, __sc_err;				\
	{								\
		register unsigned long __sc_0 __asm__ ("r0");		\
		register unsigned long __sc_3 __asm__ ("r3");		\
		register unsigned long __sc_4 __asm__ ("r4");		\
		register unsigned long __sc_5 __asm__ ("r5");		\
		register unsigned long __sc_6 __asm__ ("r6");		\
									\
		__sc_3 = (unsigned long) (arg1);			\
		__sc_4 = (unsigned long) (arg2);			\
		__sc_5 = (unsigned long) (arg3);			\
		__sc_6 = (unsigned long) (arg4);			\
		__sc_0 = __NR_##name;					\
		__asm__ __volatile__					\
			("sc           \n\t"				\
			 "mfcr %1      "				\
			: "=&r" (__sc_3), "=&r" (__sc_0)		\
			: "0"   (__sc_3), "1"   (__sc_0),		\
			  "r"   (__sc_4),				\
			  "r"   (__sc_5),				\
			  "r"   (__sc_6)				\
			: __syscall_clobbers);				\
		__sc_ret = __sc_3;					\
		__sc_err = __sc_0;					\
	}								\
	__syscall_return (type);					\
}

#define _syscall5(type,name,type1,arg1,type2,arg2,type3,arg3,type4,arg4,type5,arg5) \
type name(type1 arg1, type2 arg2, type3 arg3, type4 arg4, type5 arg5)	\
{									\
	unsigned long __sc_ret, __sc_err;				\
	{								\
		register unsigned long __sc_0 __asm__ ("r0");		\
		register unsigned long __sc_3 __asm__ ("r3");		\
		register unsigned long __sc_4 __asm__ ("r4");		\
		register unsigned long __sc_5 __asm__ ("r5");		\
		register unsigned long __sc_6 __asm__ ("r6");		\
		register unsigned long __sc_7 __asm__ ("r7");		\
									\
		__sc_3 = (unsigned long) (arg1);			\
		__sc_4 = (unsigned long) (arg2);			\
		__sc_5 = (unsigned long) (arg3);			\
		__sc_6 = (unsigned long) (arg4);			\
		__sc_7 = (unsigned long) (arg5);			\
		__sc_0 = __NR_##name;					\
		__asm__ __volatile__					\
			("sc           \n\t"				\
			 "mfcr %1      "				\
			: "=&r" (__sc_3), "=&r" (__sc_0)		\
			: "0"   (__sc_3), "1"   (__sc_0),		\
			  "r"   (__sc_4),				\
			  "r"   (__sc_5),				\
			  "r"   (__sc_6),				\
			  "r"   (__sc_7)				\
			: __syscall_clobbers);				\
		__sc_ret = __sc_3;					\
		__sc_err = __sc_0;					\
	}								\
	__syscall_return (type);					\
}


#ifdef __KERNEL_SYSCALLS__

/*
 * Forking from kernel space will result in the child getting a new,
 * empty kernel stack area.  Thus the child cannot access automatic
 * variables set in the parent unless they are in registers, and the
 * procedure where the fork was done cannot return to its caller in
 * the child.
 */

/*
 * System call prototypes.
 */
#define __NR__exit __NR_exit
static inline _syscall0(int,pause)
static inline _syscall0(int,sync)
static inline _syscall0(pid_t,setsid)
static inline _syscall3(int,write,int,fd,const char *,buf,off_t,count)
static inline _syscall3(int,read,int,fd,char *,buf,off_t,count)
static inline _syscall3(off_t,lseek,int,fd,off_t,offset,int,count)
static inline _syscall1(int,dup,int,fd)
static inline _syscall3(int,execve,const char *,file,char **,argv,char **,envp)
static inline _syscall3(int,open,const char *,file,int,flag,int,mode)
static inline _syscall1(int,close,int,fd)
static inline _syscall1(int,_exit,int,exitcode)
static inline _syscall3(pid_t,waitpid,pid_t,pid,int *,wait_stat,int,options)
static inline _syscall1(int,delete_module,const char *,name)

static inline pid_t wait(int * wait_stat) 
{
	return waitpid(-1,wait_stat,0);
}

#endif /* __KERNEL_SYSCALLS__ */

/*
 * "Conditional" syscalls
 *
 * What we want is __attribute__((weak,alias("sys_ni_syscall"))),
 * but it doesn't work on all toolchains, so we just do it by hand
 */
#define cond_syscall(x) asm(".weak\t." #x "\n\t.set\t." #x ",sys_ni_syscall");

#endif /* _ASM_PPC_UNISTD_H_ */
