#ifndef _ASM_S390X_COMPAT_H
#define _ASM_S390X_COMPAT_H
/*
 * Architecture specific compatibility types
 */
#include <linux/types.h>
#include <linux/sched.h>

#define COMPAT_USER_HZ	100

typedef u32		compat_size_t;
typedef s32		compat_ssize_t;
typedef s32		compat_time_t;
typedef s32		compat_clock_t;
typedef s32		compat_pid_t;
typedef u16		compat_uid_t;
typedef u16		compat_gid_t;
typedef u32		compat_uid32_t;
typedef u32		compat_gid32_t;
typedef u16		compat_mode_t;
typedef u32		compat_ino_t;
typedef u16		compat_dev_t;
typedef s32		compat_off_t;
typedef s64		compat_loff_t;
typedef u16		compat_nlink_t;
typedef u16		compat_ipc_pid_t;
typedef s32		compat_daddr_t;
typedef u32		compat_caddr_t;
typedef __kernel_fsid_t	compat_fsid_t;
typedef s32		compat_key_t;
typedef s32		compat_timer_t;

typedef s32		compat_int_t;
typedef s32		compat_long_t;
typedef u32		compat_uint_t;
typedef u32		compat_ulong_t;

struct compat_timespec {
	compat_time_t	tv_sec;
	s32		tv_nsec;
};

struct compat_timeval {
	compat_time_t	tv_sec;
	s32		tv_usec;
};

struct compat_stat {
	compat_dev_t	st_dev;
	u16		__pad1;
	compat_ino_t	st_ino;
	compat_mode_t	st_mode;
	compat_nlink_t	st_nlink;
	compat_uid_t	st_uid;
	compat_gid_t	st_gid;
	compat_dev_t	st_rdev;
	u16		__pad2;
	u32		st_size;
	u32		st_blksize;
	u32		st_blocks;
	u32		st_atime;
	u32		st_atime_nsec;
	u32		st_mtime;
	u32		st_mtime_nsec;
	u32		st_ctime;
	u32		st_ctime_nsec;
	u32		__unused4;
	u32		__unused5;
};

struct compat_flock {
	short		l_type;
	short		l_whence;
	compat_off_t	l_start;
	compat_off_t	l_len;
	compat_pid_t	l_pid;
};

#define F_GETLK64       12
#define F_SETLK64       13
#define F_SETLKW64      14    

struct compat_flock64 {
	short		l_type;
	short		l_whence;
	compat_loff_t	l_start;
	compat_loff_t	l_len;
	compat_pid_t	l_pid;
};

struct compat_statfs {
	s32		f_type;
	s32		f_bsize;
	s32		f_blocks;
	s32		f_bfree;
	s32		f_bavail;
	s32		f_files;
	s32		f_ffree;
	compat_fsid_t	f_fsid;
	s32		f_namelen;
	s32		f_frsize;
	s32		f_spare[6];
};

#define COMPAT_RLIM_OLD_INFINITY	0x7fffffff
#define COMPAT_RLIM_INFINITY		0xffffffff

typedef u32		compat_old_sigset_t;	/* at least 32 bits */

#define _COMPAT_NSIG		64
#define _COMPAT_NSIG_BPW	32

typedef u32		compat_sigset_word;

#define COMPAT_OFF_T_MAX	0x7fffffff
#define COMPAT_LOFF_T_MAX	0x7fffffffffffffffL

/*
 * A pointer passed in from user mode. This should not
 * be used for syscall parameters, just declare them
 * as pointers because the syscall entry code will have
 * appropriately comverted them already.
 */
typedef	u32		compat_uptr_t;

static inline void __user *compat_ptr(compat_uptr_t uptr)
{
	return (void __user *)(unsigned long)(uptr & 0x7fffffffUL);
}

static inline void __user *compat_alloc_user_space(long len)
{
	unsigned long stack;

	stack = KSTK_ESP(current);
	if (test_thread_flag(TIF_31BIT))
		stack &= 0x7fffffffUL;
	return (void __user *) (stack - len);
}

struct compat_ipc64_perm {
	compat_key_t key;
	compat_uid32_t uid;
	compat_gid32_t gid;
	compat_uid32_t cuid;
	compat_gid32_t cgid;
	compat_mode_t mode;
	unsigned short __pad1;
	unsigned short seq;
	unsigned short __pad2;
	unsigned int __unused1;
	unsigned int __unused2;
};

struct compat_semid64_ds {
	struct compat_ipc64_perm sem_perm;
	compat_time_t  sem_otime;
	compat_ulong_t __pad1;
	compat_time_t  sem_ctime;
	compat_ulong_t __pad2;
	compat_ulong_t sem_nsems;
	compat_ulong_t __unused1;
	compat_ulong_t __unused2;
};

struct compat_msqid64_ds {
	struct compat_ipc64_perm msg_perm;
	compat_time_t   msg_stime;
	compat_ulong_t __pad1;
	compat_time_t   msg_rtime;
	compat_ulong_t __pad2;
	compat_time_t   msg_ctime;
	compat_ulong_t __pad3;
	compat_ulong_t msg_cbytes;
	compat_ulong_t msg_qnum;
	compat_ulong_t msg_qbytes;
	compat_pid_t   msg_lspid;
	compat_pid_t   msg_lrpid;
	compat_ulong_t __unused1;
	compat_ulong_t __unused2;
};

struct compat_shmid64_ds {
	struct compat_ipc64_perm shm_perm;
	compat_size_t  shm_segsz;
	compat_time_t  shm_atime;
	compat_ulong_t __pad1;
	compat_time_t  shm_dtime;
	compat_ulong_t __pad2;
	compat_time_t  shm_ctime;
	compat_ulong_t __pad3;
	compat_pid_t   shm_cpid;
	compat_pid_t   shm_lpid;
	compat_ulong_t shm_nattch;
	compat_ulong_t __unused1;
	compat_ulong_t __unused2;
};
#endif /* _ASM_S390X_COMPAT_H */
