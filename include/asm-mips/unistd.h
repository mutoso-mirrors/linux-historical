/*
 * This file contains the system call numbers.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997 by Ralf Baechle
 *
 * Changed system calls macros _syscall5 - _syscall7 to push args 5 to 7 onto
 * the stack. Robin Farine for ACN S.A, Copyright (C) 1996 by ACN S.A
 */
#ifndef __ASM_MIPS_UNISTD_H
#define __ASM_MIPS_UNISTD_H

/*
 * The syscalls 0 - 3999 are reserved for a down to the root syscall
 * compatibility with RISC/os and IRIX.  We'll see how to deal with the
 * various "real" BSD variants like Ultrix, NetBSD ...
 */

/*
 * SVR4 syscalls are in the range from 1 to 999
 */
#define __NR_SVR4			0
#define __NR_SVR4_syscall		(__NR_SVR4 +   0)
#define __NR_SVR4_exit			(__NR_SVR4 +   1)
#define __NR_SVR4_fork			(__NR_SVR4 +   2)
#define __NR_SVR4_read			(__NR_SVR4 +   3)
#define __NR_SVR4_write			(__NR_SVR4 +   4)
#define __NR_SVR4_open			(__NR_SVR4 +   5)
#define __NR_SVR4_close			(__NR_SVR4 +   6)
#define __NR_SVR4_wait			(__NR_SVR4 +   7)
#define __NR_SVR4_creat			(__NR_SVR4 +   8)
#define __NR_SVR4_link			(__NR_SVR4 +   9)
#define __NR_SVR4_unlink		(__NR_SVR4 +  10)
#define __NR_SVR4_exec			(__NR_SVR4 +  11)
#define __NR_SVR4_chdir			(__NR_SVR4 +  12)
#define __NR_SVR4_gtime			(__NR_SVR4 +  13)
#define __NR_SVR4_mknod			(__NR_SVR4 +  14)
#define __NR_SVR4_chmod			(__NR_SVR4 +  15)
#define __NR_SVR4_chown			(__NR_SVR4 +  16)
#define __NR_SVR4_sbreak		(__NR_SVR4 +  17)
#define __NR_SVR4_stat			(__NR_SVR4 +  18)
#define __NR_SVR4_lseek			(__NR_SVR4 +  19)
#define __NR_SVR4_getpid		(__NR_SVR4 +  20)
#define __NR_SVR4_mount			(__NR_SVR4 +  21)
#define __NR_SVR4_umount		(__NR_SVR4 +  22)
#define __NR_SVR4_setuid		(__NR_SVR4 +  23)
#define __NR_SVR4_getuid		(__NR_SVR4 +  24)
#define __NR_SVR4_stime			(__NR_SVR4 +  25)
#define __NR_SVR4_ptrace		(__NR_SVR4 +  26)
#define __NR_SVR4_alarm			(__NR_SVR4 +  27)
#define __NR_SVR4_fstat			(__NR_SVR4 +  28)
#define __NR_SVR4_pause			(__NR_SVR4 +  29)
#define __NR_SVR4_utime			(__NR_SVR4 +  30)
#define __NR_SVR4_stty			(__NR_SVR4 +  31)
#define __NR_SVR4_gtty			(__NR_SVR4 +  32)
#define __NR_SVR4_access		(__NR_SVR4 +  33)
#define __NR_SVR4_nice			(__NR_SVR4 +  34)
#define __NR_SVR4_statfs		(__NR_SVR4 +  35)
#define __NR_SVR4_sync			(__NR_SVR4 +  36)
#define __NR_SVR4_kill			(__NR_SVR4 +  37)
#define __NR_SVR4_fstatfs		(__NR_SVR4 +  38)
#define __NR_SVR4_setpgrp		(__NR_SVR4 +  39)
#define __NR_SVR4_cxenix		(__NR_SVR4 +  40)
#define __NR_SVR4_dup			(__NR_SVR4 +  41)
#define __NR_SVR4_pipe			(__NR_SVR4 +  42)
#define __NR_SVR4_times			(__NR_SVR4 +  43)
#define __NR_SVR4_profil		(__NR_SVR4 +  44)
#define __NR_SVR4_plock			(__NR_SVR4 +  45)
#define __NR_SVR4_setgid		(__NR_SVR4 +  46)
#define __NR_SVR4_getgid		(__NR_SVR4 +  47)
#define __NR_SVR4_sig			(__NR_SVR4 +  48)
#define __NR_SVR4_msgsys		(__NR_SVR4 +  49)
#define __NR_SVR4_sysmips		(__NR_SVR4 +  50)
#define __NR_SVR4_sysacct		(__NR_SVR4 +  51)
#define __NR_SVR4_shmsys		(__NR_SVR4 +  52)
#define __NR_SVR4_semsys		(__NR_SVR4 +  53)
#define __NR_SVR4_ioctl			(__NR_SVR4 +  54)
#define __NR_SVR4_uadmin		(__NR_SVR4 +  55)
#define __NR_SVR4_exch 			(__NR_SVR4 +  56)
#define __NR_SVR4_utssys		(__NR_SVR4 +  57)
#define __NR_SVR4_fsync			(__NR_SVR4 +  58)
#define __NR_SVR4_exece			(__NR_SVR4 +  59)
#define __NR_SVR4_umask			(__NR_SVR4 +  60)
#define __NR_SVR4_chroot		(__NR_SVR4 +  61)
#define __NR_SVR4_fcntl			(__NR_SVR4 +  62)
#define __NR_SVR4_ulimit		(__NR_SVR4 +  63)
#define __NR_SVR4_reserved1		(__NR_SVR4 +  64)
#define __NR_SVR4_reserved2		(__NR_SVR4 +  65)
#define __NR_SVR4_reserved3		(__NR_SVR4 +  66)
#define __NR_SVR4_reserved4		(__NR_SVR4 +  67)
#define __NR_SVR4_reserved5		(__NR_SVR4 +  68)
#define __NR_SVR4_reserved6		(__NR_SVR4 +  69)
#define __NR_SVR4_advfs			(__NR_SVR4 +  70)
#define __NR_SVR4_unadvfs		(__NR_SVR4 +  71)
#define __NR_SVR4_unused1		(__NR_SVR4 +  72)
#define __NR_SVR4_unused2		(__NR_SVR4 +  73)
#define __NR_SVR4_rfstart		(__NR_SVR4 +  74)
#define __NR_SVR4_unused3		(__NR_SVR4 +  75)
#define __NR_SVR4_rdebug		(__NR_SVR4 +  76)
#define __NR_SVR4_rfstop		(__NR_SVR4 +  77)
#define __NR_SVR4_rfsys			(__NR_SVR4 +  78)
#define __NR_SVR4_rmdir			(__NR_SVR4 +  79)
#define __NR_SVR4_mkdir			(__NR_SVR4 +  80)
#define __NR_SVR4_getdents		(__NR_SVR4 +  81)
#define __NR_SVR4_libattach		(__NR_SVR4 +  82)
#define __NR_SVR4_libdetach		(__NR_SVR4 +  83)
#define __NR_SVR4_sysfs			(__NR_SVR4 +  84)
#define __NR_SVR4_getmsg		(__NR_SVR4 +  85)
#define __NR_SVR4_putmsg		(__NR_SVR4 +  86)
#define __NR_SVR4_poll			(__NR_SVR4 +  87)
#define __NR_SVR4_lstat			(__NR_SVR4 +  88)
#define __NR_SVR4_symlink		(__NR_SVR4 +  89)
#define __NR_SVR4_readlink		(__NR_SVR4 +  90)
#define __NR_SVR4_setgroups		(__NR_SVR4 +  91)
#define __NR_SVR4_getgroups		(__NR_SVR4 +  92)
#define __NR_SVR4_fchmod		(__NR_SVR4 +  93)
#define __NR_SVR4_fchown		(__NR_SVR4 +  94)
#define __NR_SVR4_sigprocmask		(__NR_SVR4 +  95)
#define __NR_SVR4_sigsuspend		(__NR_SVR4 +  96)
#define __NR_SVR4_sigaltstack		(__NR_SVR4 +  97)
#define __NR_SVR4_sigaction		(__NR_SVR4 +  98)
#define __NR_SVR4_sigpending		(__NR_SVR4 +  99)
#define __NR_SVR4_setcontext		(__NR_SVR4 + 100)
#define __NR_SVR4_evsys			(__NR_SVR4 + 101)
#define __NR_SVR4_evtrapret		(__NR_SVR4 + 102)
#define __NR_SVR4_statvfs		(__NR_SVR4 + 103)
#define __NR_SVR4_fstatvfs		(__NR_SVR4 + 104)
#define __NR_SVR4_reserved7		(__NR_SVR4 + 105)
#define __NR_SVR4_nfssys		(__NR_SVR4 + 106)
#define __NR_SVR4_waitid		(__NR_SVR4 + 107)
#define __NR_SVR4_sigsendset		(__NR_SVR4 + 108)
#define __NR_SVR4_hrtsys		(__NR_SVR4 + 109)
#define __NR_SVR4_acancel		(__NR_SVR4 + 110)
#define __NR_SVR4_async			(__NR_SVR4 + 111)
#define __NR_SVR4_priocntlset		(__NR_SVR4 + 112)
#define __NR_SVR4_pathconf		(__NR_SVR4 + 113)
#define __NR_SVR4_mincore		(__NR_SVR4 + 114)
#define __NR_SVR4_mmap			(__NR_SVR4 + 115)
#define __NR_SVR4_mprotect		(__NR_SVR4 + 116)
#define __NR_SVR4_munmap		(__NR_SVR4 + 117)
#define __NR_SVR4_fpathconf		(__NR_SVR4 + 118)
#define __NR_SVR4_vfork			(__NR_SVR4 + 119)
#define __NR_SVR4_fchdir		(__NR_SVR4 + 120)
#define __NR_SVR4_readv			(__NR_SVR4 + 121)
#define __NR_SVR4_writev		(__NR_SVR4 + 122)
#define __NR_SVR4_xstat			(__NR_SVR4 + 123)
#define __NR_SVR4_lxstat		(__NR_SVR4 + 124)
#define __NR_SVR4_fxstat		(__NR_SVR4 + 125)
#define __NR_SVR4_xmknod		(__NR_SVR4 + 126)
#define __NR_SVR4_clocal		(__NR_SVR4 + 127)
#define __NR_SVR4_setrlimit		(__NR_SVR4 + 128)
#define __NR_SVR4_getrlimit		(__NR_SVR4 + 129)
#define __NR_SVR4_lchown		(__NR_SVR4 + 130)
#define __NR_SVR4_memcntl		(__NR_SVR4 + 131)
#define __NR_SVR4_getpmsg		(__NR_SVR4 + 132)
#define __NR_SVR4_putpmsg		(__NR_SVR4 + 133)
#define __NR_SVR4_rename		(__NR_SVR4 + 134)
#define __NR_SVR4_nuname		(__NR_SVR4 + 135)
#define __NR_SVR4_setegid		(__NR_SVR4 + 136)
#define __NR_SVR4_sysconf		(__NR_SVR4 + 137)
#define __NR_SVR4_adjtime		(__NR_SVR4 + 138)
#define __NR_SVR4_sysinfo		(__NR_SVR4 + 139)
#define __NR_SVR4_reserved8		(__NR_SVR4 + 140)
#define __NR_SVR4_seteuid		(__NR_SVR4 + 141)
#define __NR_SVR4_PYRAMID_statis	(__NR_SVR4 + 142)
#define __NR_SVR4_PYRAMID_tuning	(__NR_SVR4 + 143)
#define __NR_SVR4_PYRAMID_forcerr	(__NR_SVR4 + 144)
#define __NR_SVR4_PYRAMID_mpcntl	(__NR_SVR4 + 145)
#define __NR_SVR4_reserved9		(__NR_SVR4 + 146)
#define __NR_SVR4_reserved10		(__NR_SVR4 + 147)
#define __NR_SVR4_reserved11		(__NR_SVR4 + 148)
#define __NR_SVR4_reserved12		(__NR_SVR4 + 149)
#define __NR_SVR4_reserved13		(__NR_SVR4 + 150)
#define __NR_SVR4_reserved14		(__NR_SVR4 + 151)
#define __NR_SVR4_reserved15		(__NR_SVR4 + 152)
#define __NR_SVR4_reserved16		(__NR_SVR4 + 153)
#define __NR_SVR4_reserved17		(__NR_SVR4 + 154)
#define __NR_SVR4_reserved18		(__NR_SVR4 + 155)
#define __NR_SVR4_reserved19		(__NR_SVR4 + 156)
#define __NR_SVR4_reserved20		(__NR_SVR4 + 157)
#define __NR_SVR4_reserved21		(__NR_SVR4 + 158)
#define __NR_SVR4_reserved22		(__NR_SVR4 + 159)
#define __NR_SVR4_reserved23		(__NR_SVR4 + 160)
#define __NR_SVR4_reserved24		(__NR_SVR4 + 161)
#define __NR_SVR4_reserved25		(__NR_SVR4 + 162)
#define __NR_SVR4_reserved26		(__NR_SVR4 + 163)
#define __NR_SVR4_reserved27		(__NR_SVR4 + 164)
#define __NR_SVR4_reserved28		(__NR_SVR4 + 165)
#define __NR_SVR4_reserved29		(__NR_SVR4 + 166)
#define __NR_SVR4_reserved30		(__NR_SVR4 + 167)
#define __NR_SVR4_reserved31		(__NR_SVR4 + 168)
#define __NR_SVR4_reserved32		(__NR_SVR4 + 169)
#define __NR_SVR4_reserved33		(__NR_SVR4 + 170)
#define __NR_SVR4_reserved34		(__NR_SVR4 + 171)
#define __NR_SVR4_reserved35		(__NR_SVR4 + 172)
#define __NR_SVR4_reserved36		(__NR_SVR4 + 173)
#define __NR_SVR4_reserved37		(__NR_SVR4 + 174)
#define __NR_SVR4_reserved38		(__NR_SVR4 + 175)
#define __NR_SVR4_reserved39		(__NR_SVR4 + 176)
#define __NR_SVR4_reserved40		(__NR_SVR4 + 177)
#define __NR_SVR4_reserved41		(__NR_SVR4 + 178)
#define __NR_SVR4_reserved42		(__NR_SVR4 + 179)
#define __NR_SVR4_reserved43		(__NR_SVR4 + 180)
#define __NR_SVR4_reserved44		(__NR_SVR4 + 181)
#define __NR_SVR4_reserved45		(__NR_SVR4 + 182)
#define __NR_SVR4_reserved46		(__NR_SVR4 + 183)
#define __NR_SVR4_reserved47		(__NR_SVR4 + 184)
#define __NR_SVR4_reserved48		(__NR_SVR4 + 185)
#define __NR_SVR4_reserved49		(__NR_SVR4 + 186)
#define __NR_SVR4_reserved50		(__NR_SVR4 + 187)
#define __NR_SVR4_reserved51		(__NR_SVR4 + 188)
#define __NR_SVR4_reserved52		(__NR_SVR4 + 189)
#define __NR_SVR4_reserved53		(__NR_SVR4 + 190)
#define __NR_SVR4_reserved54		(__NR_SVR4 + 191)
#define __NR_SVR4_reserved55		(__NR_SVR4 + 192)
#define __NR_SVR4_reserved56		(__NR_SVR4 + 193)
#define __NR_SVR4_reserved57		(__NR_SVR4 + 194)
#define __NR_SVR4_reserved58		(__NR_SVR4 + 195)
#define __NR_SVR4_reserved59		(__NR_SVR4 + 196)
#define __NR_SVR4_reserved60		(__NR_SVR4 + 197)
#define __NR_SVR4_reserved61		(__NR_SVR4 + 198)
#define __NR_SVR4_reserved62		(__NR_SVR4 + 199)
#define __NR_SVR4_reserved63		(__NR_SVR4 + 200)
#define __NR_SVR4_aread			(__NR_SVR4 + 201)
#define __NR_SVR4_awrite		(__NR_SVR4 + 202)	
#define __NR_SVR4_listio		(__NR_SVR4 + 203)
#define __NR_SVR4_mips_acancel		(__NR_SVR4 + 204)
#define __NR_SVR4_astatus		(__NR_SVR4 + 205)
#define __NR_SVR4_await			(__NR_SVR4 + 206)
#define __NR_SVR4_areadv		(__NR_SVR4 + 207)
#define __NR_SVR4_awritev		(__NR_SVR4 + 208)
#define __NR_SVR4_MIPS_reserved1	(__NR_SVR4 + 209)
#define __NR_SVR4_MIPS_reserved2	(__NR_SVR4 + 210)
#define __NR_SVR4_MIPS_reserved3	(__NR_SVR4 + 211)
#define __NR_SVR4_MIPS_reserved4	(__NR_SVR4 + 212)
#define __NR_SVR4_MIPS_reserved5	(__NR_SVR4 + 213)
#define __NR_SVR4_MIPS_reserved6	(__NR_SVR4 + 214)
#define __NR_SVR4_MIPS_reserved7	(__NR_SVR4 + 215)
#define __NR_SVR4_MIPS_reserved8	(__NR_SVR4 + 216)
#define __NR_SVR4_MIPS_reserved9	(__NR_SVR4 + 217)
#define __NR_SVR4_MIPS_reserved10	(__NR_SVR4 + 218)
#define __NR_SVR4_MIPS_reserved11	(__NR_SVR4 + 219)
#define __NR_SVR4_MIPS_reserved12	(__NR_SVR4 + 220)
#define __NR_SVR4_CDC_reserved1		(__NR_SVR4 + 221)
#define __NR_SVR4_CDC_reserved2		(__NR_SVR4 + 222)
#define __NR_SVR4_CDC_reserved3		(__NR_SVR4 + 223)
#define __NR_SVR4_CDC_reserved4		(__NR_SVR4 + 224)
#define __NR_SVR4_CDC_reserved5		(__NR_SVR4 + 225)
#define __NR_SVR4_CDC_reserved6		(__NR_SVR4 + 226)
#define __NR_SVR4_CDC_reserved7		(__NR_SVR4 + 227)
#define __NR_SVR4_CDC_reserved8		(__NR_SVR4 + 228)
#define __NR_SVR4_CDC_reserved9		(__NR_SVR4 + 229)
#define __NR_SVR4_CDC_reserved10	(__NR_SVR4 + 230)
#define __NR_SVR4_CDC_reserved11	(__NR_SVR4 + 231)
#define __NR_SVR4_CDC_reserved12	(__NR_SVR4 + 232)
#define __NR_SVR4_CDC_reserved13	(__NR_SVR4 + 233)
#define __NR_SVR4_CDC_reserved14	(__NR_SVR4 + 234)
#define __NR_SVR4_CDC_reserved15	(__NR_SVR4 + 235)
#define __NR_SVR4_CDC_reserved16	(__NR_SVR4 + 236)
#define __NR_SVR4_CDC_reserved17	(__NR_SVR4 + 237)
#define __NR_SVR4_CDC_reserved18	(__NR_SVR4 + 238)
#define __NR_SVR4_CDC_reserved19	(__NR_SVR4 + 239)
#define __NR_SVR4_CDC_reserved20	(__NR_SVR4 + 240)

/*
 * SYS V syscalls are in the range from 1000 to 1999
 */
#define __NR_SYSV			1000
#define __NR_SYSV_syscall		(__NR_SYSV +   0)
#define __NR_SYSV_exit			(__NR_SYSV +   1)
#define __NR_SYSV_fork			(__NR_SYSV +   2)
#define __NR_SYSV_read			(__NR_SYSV +   3)
#define __NR_SYSV_write			(__NR_SYSV +   4)
#define __NR_SYSV_open			(__NR_SYSV +   5)
#define __NR_SYSV_close			(__NR_SYSV +   6)
#define __NR_SYSV_wait			(__NR_SYSV +   7)
#define __NR_SYSV_creat			(__NR_SYSV +   8)
#define __NR_SYSV_link			(__NR_SYSV +   9)
#define __NR_SYSV_unlink		(__NR_SYSV +  10)
#define __NR_SYSV_execv			(__NR_SYSV +  11)
#define __NR_SYSV_chdir			(__NR_SYSV +  12)
#define __NR_SYSV_time			(__NR_SYSV +  13)
#define __NR_SYSV_mknod			(__NR_SYSV +  14)
#define __NR_SYSV_chmod			(__NR_SYSV +  15)
#define __NR_SYSV_chown			(__NR_SYSV +  16)
#define __NR_SYSV_brk			(__NR_SYSV +  17)
#define __NR_SYSV_stat			(__NR_SYSV +  18)
#define __NR_SYSV_lseek			(__NR_SYSV +  19)
#define __NR_SYSV_getpid		(__NR_SYSV +  20)
#define __NR_SYSV_mount			(__NR_SYSV +  21)
#define __NR_SYSV_umount		(__NR_SYSV +  22)
#define __NR_SYSV_setuid		(__NR_SYSV +  23)
#define __NR_SYSV_getuid		(__NR_SYSV +  24)
#define __NR_SYSV_stime			(__NR_SYSV +  25)
#define __NR_SYSV_ptrace		(__NR_SYSV +  26)
#define __NR_SYSV_alarm			(__NR_SYSV +  27)
#define __NR_SYSV_fstat			(__NR_SYSV +  28)
#define __NR_SYSV_pause			(__NR_SYSV +  29)
#define __NR_SYSV_utime			(__NR_SYSV +  30)
#define __NR_SYSV_stty			(__NR_SYSV +  31)
#define __NR_SYSV_gtty			(__NR_SYSV +  32)
#define __NR_SYSV_access		(__NR_SYSV +  33)
#define __NR_SYSV_nice			(__NR_SYSV +  34)
#define __NR_SYSV_statfs		(__NR_SYSV +  35)
#define __NR_SYSV_sync			(__NR_SYSV +  36)
#define __NR_SYSV_kill			(__NR_SYSV +  37)
#define __NR_SYSV_fstatfs		(__NR_SYSV +  38)
#define __NR_SYSV_setpgrp		(__NR_SYSV +  39)
#define __NR_SYSV_syssgi		(__NR_SYSV +  40)
#define __NR_SYSV_dup			(__NR_SYSV +  41)
#define __NR_SYSV_pipe			(__NR_SYSV +  42)
#define __NR_SYSV_times			(__NR_SYSV +  43)
#define __NR_SYSV_profil		(__NR_SYSV +  44)
#define __NR_SYSV_plock			(__NR_SYSV +  45)
#define __NR_SYSV_setgid		(__NR_SYSV +  46)
#define __NR_SYSV_getgid		(__NR_SYSV +  47)
#define __NR_SYSV_sig			(__NR_SYSV +  48)
#define __NR_SYSV_msgsys		(__NR_SYSV +  49)
#define __NR_SYSV_sysmips		(__NR_SYSV +  50)
#define __NR_SYSV_acct			(__NR_SYSV +  51)
#define __NR_SYSV_shmsys		(__NR_SYSV +  52)
#define __NR_SYSV_semsys		(__NR_SYSV +  53)
#define __NR_SYSV_ioctl			(__NR_SYSV +  54)
#define __NR_SYSV_uadmin		(__NR_SYSV +  55)
#define __NR_SYSV_sysmp			(__NR_SYSV +  56)
#define __NR_SYSV_utssys		(__NR_SYSV +  57)
#define __NR_SYSV_USG_reserved1		(__NR_SYSV +  58)
#define __NR_SYSV_execve		(__NR_SYSV +  59)
#define __NR_SYSV_umask			(__NR_SYSV +  60)
#define __NR_SYSV_chroot		(__NR_SYSV +  61)
#define __NR_SYSV_fcntl			(__NR_SYSV +  62)
#define __NR_SYSV_ulimit		(__NR_SYSV +  63)
#define __NR_SYSV_SAFARI4_reserved1	(__NR_SYSV +  64)
#define __NR_SYSV_SAFARI4_reserved2	(__NR_SYSV +  65)
#define __NR_SYSV_SAFARI4_reserved3	(__NR_SYSV +  66)
#define __NR_SYSV_SAFARI4_reserved4	(__NR_SYSV +  67)
#define __NR_SYSV_SAFARI4_reserved5	(__NR_SYSV +  68)
#define __NR_SYSV_SAFARI4_reserved6	(__NR_SYSV +  69)
#define __NR_SYSV_advfs			(__NR_SYSV +  70)
#define __NR_SYSV_unadvfs		(__NR_SYSV +  71)
#define __NR_SYSV_rmount		(__NR_SYSV +  72)
#define __NR_SYSV_rumount		(__NR_SYSV +  73)
#define __NR_SYSV_rfstart		(__NR_SYSV +  74)
#define __NR_SYSV_getrlimit64		(__NR_SYSV +  75)
#define __NR_SYSV_setrlimit64		(__NR_SYSV +  76)
#define __NR_SYSV_nanosleep		(__NR_SYSV +  77)
#define __NR_SYSV_lseek64		(__NR_SYSV +  78)
#define __NR_SYSV_rmdir			(__NR_SYSV +  79)
#define __NR_SYSV_mkdir			(__NR_SYSV +  80)
#define __NR_SYSV_getdents		(__NR_SYSV +  81)
#define __NR_SYSV_sginap		(__NR_SYSV +  82)
#define __NR_SYSV_sgikopt		(__NR_SYSV +  83)
#define __NR_SYSV_sysfs			(__NR_SYSV +  84)
#define __NR_SYSV_getmsg		(__NR_SYSV +  85)
#define __NR_SYSV_putmsg		(__NR_SYSV +  86)
#define __NR_SYSV_poll			(__NR_SYSV +  87)
#define __NR_SYSV_sigreturn		(__NR_SYSV +  88)
#define __NR_SYSV_accept		(__NR_SYSV +  89)
#define __NR_SYSV_bind			(__NR_SYSV +  90)
#define __NR_SYSV_connect		(__NR_SYSV +  91)
#define __NR_SYSV_gethostid		(__NR_SYSV +  92)
#define __NR_SYSV_getpeername		(__NR_SYSV +  93)
#define __NR_SYSV_getsockname		(__NR_SYSV +  94)
#define __NR_SYSV_getsockopt		(__NR_SYSV +  95)
#define __NR_SYSV_listen		(__NR_SYSV +  96)
#define __NR_SYSV_recv			(__NR_SYSV +  97)
#define __NR_SYSV_recvfrom		(__NR_SYSV +  98)
#define __NR_SYSV_recvmsg		(__NR_SYSV +  99)
#define __NR_SYSV_select		(__NR_SYSV + 100)
#define __NR_SYSV_send			(__NR_SYSV + 101)
#define __NR_SYSV_sendmsg		(__NR_SYSV + 102)
#define __NR_SYSV_sendto		(__NR_SYSV + 103)
#define __NR_SYSV_sethostid		(__NR_SYSV + 104)
#define __NR_SYSV_setsockopt		(__NR_SYSV + 105)
#define __NR_SYSV_shutdown		(__NR_SYSV + 106)
#define __NR_SYSV_socket		(__NR_SYSV + 107)
#define __NR_SYSV_gethostname		(__NR_SYSV + 108)
#define __NR_SYSV_sethostname		(__NR_SYSV + 109)
#define __NR_SYSV_getdomainname		(__NR_SYSV + 110)
#define __NR_SYSV_setdomainname		(__NR_SYSV + 111)
#define __NR_SYSV_truncate		(__NR_SYSV + 112)
#define __NR_SYSV_ftruncate		(__NR_SYSV + 113)
#define __NR_SYSV_rename		(__NR_SYSV + 114)
#define __NR_SYSV_symlink		(__NR_SYSV + 115)
#define __NR_SYSV_readlink		(__NR_SYSV + 116)
#define __NR_SYSV_lstat			(__NR_SYSV + 117)
#define __NR_SYSV_nfsmount		(__NR_SYSV + 118)
#define __NR_SYSV_nfssvc		(__NR_SYSV + 119)
#define __NR_SYSV_getfh			(__NR_SYSV + 120)
#define __NR_SYSV_async_daemon		(__NR_SYSV + 121)
#define __NR_SYSV_exportfs		(__NR_SYSV + 122)
#define __NR_SYSV_setregid		(__NR_SYSV + 123)
#define __NR_SYSV_setreuid		(__NR_SYSV + 124)
#define __NR_SYSV_getitimer		(__NR_SYSV + 125)
#define __NR_SYSV_setitimer		(__NR_SYSV + 126)
#define __NR_SYSV_adjtime		(__NR_SYSV + 127)
#define __NR_SYSV_BSD_getime		(__NR_SYSV + 128)
#define __NR_SYSV_sproc			(__NR_SYSV + 129)
#define __NR_SYSV_prctl			(__NR_SYSV + 130)
#define __NR_SYSV_procblk		(__NR_SYSV + 131)
#define __NR_SYSV_sprocsp		(__NR_SYSV + 132)
#define __NR_SYSV_sgigsc		(__NR_SYSV + 133)
#define __NR_SYSV_mmap			(__NR_SYSV + 134)
#define __NR_SYSV_munmap		(__NR_SYSV + 135)
#define __NR_SYSV_mprotect		(__NR_SYSV + 136)
#define __NR_SYSV_msync			(__NR_SYSV + 137)
#define __NR_SYSV_madvise		(__NR_SYSV + 138)
#define __NR_SYSV_pagelock		(__NR_SYSV + 139)
#define __NR_SYSV_getpagesize		(__NR_SYSV + 140)
#define __NR_SYSV_quotactl		(__NR_SYSV + 141)
#define __NR_SYSV_libdetach		(__NR_SYSV + 142)
#define __NR_SYSV_BSDgetpgrp		(__NR_SYSV + 143)
#define __NR_SYSV_BSDsetpgrp		(__NR_SYSV + 144)
#define __NR_SYSV_vhangup		(__NR_SYSV + 145)
#define __NR_SYSV_fsync			(__NR_SYSV + 146)
#define __NR_SYSV_fchdir		(__NR_SYSV + 147)
#define __NR_SYSV_getrlimit		(__NR_SYSV + 148)
#define __NR_SYSV_setrlimit		(__NR_SYSV + 149)
#define __NR_SYSV_cacheflush		(__NR_SYSV + 150)
#define __NR_SYSV_cachectl		(__NR_SYSV + 151)
#define __NR_SYSV_fchown		(__NR_SYSV + 152)
#define __NR_SYSV_fchmod		(__NR_SYSV + 153)
#define __NR_SYSV_wait3			(__NR_SYSV + 154)
#define __NR_SYSV_socketpair		(__NR_SYSV + 155)
#define __NR_SYSV_sysinfo		(__NR_SYSV + 156)
#define __NR_SYSV_nuname		(__NR_SYSV + 157)
#define __NR_SYSV_xstat			(__NR_SYSV + 158)
#define __NR_SYSV_lxstat		(__NR_SYSV + 159)
#define __NR_SYSV_fxstat		(__NR_SYSV + 160)
#define __NR_SYSV_xmknod		(__NR_SYSV + 161)
#define __NR_SYSV_ksigaction		(__NR_SYSV + 162)
#define __NR_SYSV_sigpending		(__NR_SYSV + 163)
#define __NR_SYSV_sigprocmask		(__NR_SYSV + 164)
#define __NR_SYSV_sigsuspend		(__NR_SYSV + 165)
#define __NR_SYSV_sigpoll		(__NR_SYSV + 166)
#define __NR_SYSV_swapctl		(__NR_SYSV + 167)
#define __NR_SYSV_getcontext		(__NR_SYSV + 168)
#define __NR_SYSV_setcontext		(__NR_SYSV + 169)
#define __NR_SYSV_waitsys		(__NR_SYSV + 170)
#define __NR_SYSV_sigstack		(__NR_SYSV + 171)
#define __NR_SYSV_sigaltstack		(__NR_SYSV + 172)
#define __NR_SYSV_sigsendset		(__NR_SYSV + 173)
#define __NR_SYSV_statvfs		(__NR_SYSV + 174)
#define __NR_SYSV_fstatvfs		(__NR_SYSV + 175)
#define __NR_SYSV_getpmsg		(__NR_SYSV + 176)
#define __NR_SYSV_putpmsg		(__NR_SYSV + 177)
#define __NR_SYSV_lchown		(__NR_SYSV + 178)
#define __NR_SYSV_priocntl		(__NR_SYSV + 179)
#define __NR_SYSV_ksigqueue		(__NR_SYSV + 180)
#define __NR_SYSV_readv			(__NR_SYSV + 181)
#define __NR_SYSV_writev		(__NR_SYSV + 182)
#define __NR_SYSV_truncate64		(__NR_SYSV + 183)
#define __NR_SYSV_ftruncate64		(__NR_SYSV + 184)
#define __NR_SYSV_mmap64		(__NR_SYSV + 185)
#define __NR_SYSV_dmi			(__NR_SYSV + 186)
#define __NR_SYSV_pread			(__NR_SYSV + 187)
#define __NR_SYSV_pwrite		(__NR_SYSV + 188)

/*
 * BSD 4.3 syscalls are in the range from 2000 to 2999
 */
#define __NR_BSD43			2000
#define __NR_BSD43_syscall		(__NR_BSD43 +   0)
#define __NR_BSD43_exit			(__NR_BSD43 +   1)
#define __NR_BSD43_fork			(__NR_BSD43 +   2)
#define __NR_BSD43_read			(__NR_BSD43 +   3)
#define __NR_BSD43_write		(__NR_BSD43 +   4)
#define __NR_BSD43_open			(__NR_BSD43 +   5)
#define __NR_BSD43_close		(__NR_BSD43 +   6)
#define __NR_BSD43_wait			(__NR_BSD43 +   7)
#define __NR_BSD43_creat		(__NR_BSD43 +   8)
#define __NR_BSD43_link			(__NR_BSD43 +   9)
#define __NR_BSD43_unlink		(__NR_BSD43 +  10)
#define __NR_BSD43_exec			(__NR_BSD43 +  11)
#define __NR_BSD43_chdir		(__NR_BSD43 +  12)
#define __NR_BSD43_time			(__NR_BSD43 +  13)
#define __NR_BSD43_mknod		(__NR_BSD43 +  14)
#define __NR_BSD43_chmod		(__NR_BSD43 +  15)
#define __NR_BSD43_chown		(__NR_BSD43 +  16)
#define __NR_BSD43_sbreak		(__NR_BSD43 +  17)
#define __NR_BSD43_oldstat		(__NR_BSD43 +  18)
#define __NR_BSD43_lseek		(__NR_BSD43 +  19)
#define __NR_BSD43_getpid		(__NR_BSD43 +  20)
#define __NR_BSD43_oldmount		(__NR_BSD43 +  21)
#define __NR_BSD43_umount		(__NR_BSD43 +  22)
#define __NR_BSD43_setuid		(__NR_BSD43 +  23)
#define __NR_BSD43_getuid		(__NR_BSD43 +  24)
#define __NR_BSD43_stime		(__NR_BSD43 +  25)
#define __NR_BSD43_ptrace		(__NR_BSD43 +  26)
#define __NR_BSD43_alarm		(__NR_BSD43 +  27)
#define __NR_BSD43_oldfstat		(__NR_BSD43 +  28)
#define __NR_BSD43_pause		(__NR_BSD43 +  29)
#define __NR_BSD43_utime		(__NR_BSD43 +  30)
#define __NR_BSD43_stty			(__NR_BSD43 +  31)
#define __NR_BSD43_gtty			(__NR_BSD43 +  32)
#define __NR_BSD43_access		(__NR_BSD43 +  33)
#define __NR_BSD43_nice			(__NR_BSD43 +  34)
#define __NR_BSD43_ftime		(__NR_BSD43 +  35)
#define __NR_BSD43_sync			(__NR_BSD43 +  36)
#define __NR_BSD43_kill			(__NR_BSD43 +  37)
#define __NR_BSD43_stat			(__NR_BSD43 +  38)
#define __NR_BSD43_oldsetpgrp		(__NR_BSD43 +  39)
#define __NR_BSD43_lstat		(__NR_BSD43 +  40)
#define __NR_BSD43_dup			(__NR_BSD43 +  41)
#define __NR_BSD43_pipe			(__NR_BSD43 +  42)
#define __NR_BSD43_times		(__NR_BSD43 +  43)
#define __NR_BSD43_profil		(__NR_BSD43 +  44)
#define __NR_BSD43_msgsys		(__NR_BSD43 +  45)
#define __NR_BSD43_setgid		(__NR_BSD43 +  46)
#define __NR_BSD43_getgid		(__NR_BSD43 +  47)
#define __NR_BSD43_ssig			(__NR_BSD43 +  48)
#define __NR_BSD43_reserved1		(__NR_BSD43 +  49)
#define __NR_BSD43_reserved2		(__NR_BSD43 +  50)
#define __NR_BSD43_sysacct		(__NR_BSD43 +  51)
#define __NR_BSD43_phys			(__NR_BSD43 +  52)
#define __NR_BSD43_lock			(__NR_BSD43 +  53)
#define __NR_BSD43_ioctl		(__NR_BSD43 +  54)
#define __NR_BSD43_reboot		(__NR_BSD43 +  55)
#define __NR_BSD43_mpxchan		(__NR_BSD43 +  56)
#define __NR_BSD43_symlink		(__NR_BSD43 +  57)
#define __NR_BSD43_readlink		(__NR_BSD43 +  58)
#define __NR_BSD43_execve		(__NR_BSD43 +  59)
#define __NR_BSD43_umask		(__NR_BSD43 +  60)
#define __NR_BSD43_chroot		(__NR_BSD43 +  61)
#define __NR_BSD43_fstat		(__NR_BSD43 +  62)
#define __NR_BSD43_reserved3		(__NR_BSD43 +  63)
#define __NR_BSD43_getpagesize		(__NR_BSD43 +  64)
#define __NR_BSD43_mremap		(__NR_BSD43 +  65)
#define __NR_BSD43_vfork		(__NR_BSD43 +  66)
#define __NR_BSD43_vread		(__NR_BSD43 +  67)
#define __NR_BSD43_vwrite		(__NR_BSD43 +  68)
#define __NR_BSD43_sbrk			(__NR_BSD43 +  69)
#define __NR_BSD43_sstk			(__NR_BSD43 +  70)
#define __NR_BSD43_mmap			(__NR_BSD43 +  71)
#define __NR_BSD43_vadvise		(__NR_BSD43 +  72)
#define __NR_BSD43_munmap		(__NR_BSD43 +  73)
#define __NR_BSD43_mprotect		(__NR_BSD43 +  74)
#define __NR_BSD43_madvise		(__NR_BSD43 +  75)
#define __NR_BSD43_vhangup		(__NR_BSD43 +  76)
#define __NR_BSD43_vlimit		(__NR_BSD43 +  77)
#define __NR_BSD43_mincore		(__NR_BSD43 +  78)
#define __NR_BSD43_getgroups		(__NR_BSD43 +  79)
#define __NR_BSD43_setgroups		(__NR_BSD43 +  80)
#define __NR_BSD43_getpgrp		(__NR_BSD43 +  81)
#define __NR_BSD43_setpgrp		(__NR_BSD43 +  82)
#define __NR_BSD43_setitimer		(__NR_BSD43 +  83)
#define __NR_BSD43_wait3		(__NR_BSD43 +  84)
#define __NR_BSD43_swapon		(__NR_BSD43 +  85)
#define __NR_BSD43_getitimer		(__NR_BSD43 +  86)
#define __NR_BSD43_gethostname		(__NR_BSD43 +  87)
#define __NR_BSD43_sethostname		(__NR_BSD43 +  88)
#define __NR_BSD43_getdtablesize	(__NR_BSD43 +  89)
#define __NR_BSD43_dup2			(__NR_BSD43 +  90)
#define __NR_BSD43_getdopt		(__NR_BSD43 +  91)
#define __NR_BSD43_fcntl		(__NR_BSD43 +  92)
#define __NR_BSD43_select		(__NR_BSD43 +  93)
#define __NR_BSD43_setdopt		(__NR_BSD43 +  94)
#define __NR_BSD43_fsync		(__NR_BSD43 +  95)
#define __NR_BSD43_setpriority		(__NR_BSD43 +  96)
#define __NR_BSD43_socket		(__NR_BSD43 +  97)
#define __NR_BSD43_connect		(__NR_BSD43 +  98)
#define __NR_BSD43_oldaccept		(__NR_BSD43 +  99)
#define __NR_BSD43_getpriority		(__NR_BSD43 + 100)
#define __NR_BSD43_send			(__NR_BSD43 + 101)
#define __NR_BSD43_recv			(__NR_BSD43 + 102)
#define __NR_BSD43_sigreturn		(__NR_BSD43 + 103)
#define __NR_BSD43_bind			(__NR_BSD43 + 104)
#define __NR_BSD43_setsockopt		(__NR_BSD43 + 105)
#define __NR_BSD43_listen		(__NR_BSD43 + 106)
#define __NR_BSD43_vtimes		(__NR_BSD43 + 107)
#define __NR_BSD43_sigvec		(__NR_BSD43 + 108)
#define __NR_BSD43_sigblock		(__NR_BSD43 + 109)
#define __NR_BSD43_sigsetmask		(__NR_BSD43 + 110)
#define __NR_BSD43_sigpause		(__NR_BSD43 + 111)
#define __NR_BSD43_sigstack		(__NR_BSD43 + 112)
#define __NR_BSD43_oldrecvmsg		(__NR_BSD43 + 113)
#define __NR_BSD43_oldsendmsg		(__NR_BSD43 + 114)
#define __NR_BSD43_vtrace		(__NR_BSD43 + 115)
#define __NR_BSD43_gettimeofday		(__NR_BSD43 + 116)
#define __NR_BSD43_getrusage		(__NR_BSD43 + 117)
#define __NR_BSD43_getsockopt		(__NR_BSD43 + 118)
#define __NR_BSD43_reserved4		(__NR_BSD43 + 119)
#define __NR_BSD43_readv		(__NR_BSD43 + 120)
#define __NR_BSD43_writev		(__NR_BSD43 + 121)
#define __NR_BSD43_settimeofday		(__NR_BSD43 + 122)
#define __NR_BSD43_fchown		(__NR_BSD43 + 123)
#define __NR_BSD43_fchmod		(__NR_BSD43 + 124)
#define __NR_BSD43_oldrecvfrom		(__NR_BSD43 + 125)
#define __NR_BSD43_setreuid		(__NR_BSD43 + 126)
#define __NR_BSD43_setregid		(__NR_BSD43 + 127)
#define __NR_BSD43_rename		(__NR_BSD43 + 128)
#define __NR_BSD43_truncate		(__NR_BSD43 + 129)
#define __NR_BSD43_ftruncate		(__NR_BSD43 + 130)
#define __NR_BSD43_flock		(__NR_BSD43 + 131)
#define __NR_BSD43_semsys		(__NR_BSD43 + 132)
#define __NR_BSD43_sendto		(__NR_BSD43 + 133)
#define __NR_BSD43_shutdown		(__NR_BSD43 + 134)
#define __NR_BSD43_socketpair		(__NR_BSD43 + 135)
#define __NR_BSD43_mkdir		(__NR_BSD43 + 136)
#define __NR_BSD43_rmdir		(__NR_BSD43 + 137)
#define __NR_BSD43_utimes		(__NR_BSD43 + 138)
#define __NR_BSD43_sigcleanup		(__NR_BSD43 + 139)
#define __NR_BSD43_adjtime		(__NR_BSD43 + 140)
#define __NR_BSD43_oldgetpeername	(__NR_BSD43 + 141)
#define __NR_BSD43_gethostid		(__NR_BSD43 + 142)
#define __NR_BSD43_sethostid		(__NR_BSD43 + 143)
#define __NR_BSD43_getrlimit		(__NR_BSD43 + 144)
#define __NR_BSD43_setrlimit		(__NR_BSD43 + 145)
#define __NR_BSD43_killpg		(__NR_BSD43 + 146)
#define __NR_BSD43_shmsys		(__NR_BSD43 + 147)
#define __NR_BSD43_quota		(__NR_BSD43 + 148)
#define __NR_BSD43_qquota		(__NR_BSD43 + 149)
#define __NR_BSD43_oldgetsockname	(__NR_BSD43 + 150)
#define __NR_BSD43_sysmips		(__NR_BSD43 + 151)
#define __NR_BSD43_cacheflush		(__NR_BSD43 + 152)
#define __NR_BSD43_cachectl		(__NR_BSD43 + 153)
#define __NR_BSD43_debug		(__NR_BSD43 + 154)
#define __NR_BSD43_reserved5		(__NR_BSD43 + 155)
#define __NR_BSD43_reserved6		(__NR_BSD43 + 156)
#define __NR_BSD43_nfs_mount		(__NR_BSD43 + 157)
#define __NR_BSD43_nfs_svc		(__NR_BSD43 + 158)
#define __NR_BSD43_getdirentries	(__NR_BSD43 + 159)
#define __NR_BSD43_statfs		(__NR_BSD43 + 160)
#define __NR_BSD43_fstatfs		(__NR_BSD43 + 161)
#define __NR_BSD43_unmount		(__NR_BSD43 + 162)
#define __NR_BSD43_async_daemon		(__NR_BSD43 + 163)
#define __NR_BSD43_nfs_getfh		(__NR_BSD43 + 164)
#define __NR_BSD43_getdomainname	(__NR_BSD43 + 165)
#define __NR_BSD43_setdomainname	(__NR_BSD43 + 166)
#define __NR_BSD43_pcfs_mount		(__NR_BSD43 + 167)
#define __NR_BSD43_quotactl		(__NR_BSD43 + 168)
#define __NR_BSD43_oldexportfs		(__NR_BSD43 + 169)
#define __NR_BSD43_smount		(__NR_BSD43 + 170)
#define __NR_BSD43_mipshwconf		(__NR_BSD43 + 171)
#define __NR_BSD43_exportfs		(__NR_BSD43 + 172)
#define __NR_BSD43_nfsfh_open		(__NR_BSD43 + 173)
#define __NR_BSD43_libattach		(__NR_BSD43 + 174)
#define __NR_BSD43_libdetach		(__NR_BSD43 + 175)
#define __NR_BSD43_accept		(__NR_BSD43 + 176)
#define __NR_BSD43_reserved7		(__NR_BSD43 + 177)
#define __NR_BSD43_reserved8		(__NR_BSD43 + 178)
#define __NR_BSD43_recvmsg		(__NR_BSD43 + 179)
#define __NR_BSD43_recvfrom		(__NR_BSD43 + 180)
#define __NR_BSD43_sendmsg		(__NR_BSD43 + 181)
#define __NR_BSD43_getpeername		(__NR_BSD43 + 182)
#define __NR_BSD43_getsockname		(__NR_BSD43 + 183)
#define __NR_BSD43_aread		(__NR_BSD43 + 184)
#define __NR_BSD43_awrite		(__NR_BSD43 + 185)
#define __NR_BSD43_listio		(__NR_BSD43 + 186)
#define __NR_BSD43_acancel		(__NR_BSD43 + 187)
#define __NR_BSD43_astatus		(__NR_BSD43 + 188)
#define __NR_BSD43_await		(__NR_BSD43 + 189)
#define __NR_BSD43_areadv		(__NR_BSD43 + 190)
#define __NR_BSD43_awritev		(__NR_BSD43 + 191)

/*
 * POSIX syscalls are in the range from 3000 to 3999
 */
#define __NR_POSIX			3000
#define __NR_POSIX_syscall		(__NR_POSIX +   0)
#define __NR_POSIX_exit			(__NR_POSIX +   1)
#define __NR_POSIX_fork			(__NR_POSIX +   2)
#define __NR_POSIX_read			(__NR_POSIX +   3)
#define __NR_POSIX_write		(__NR_POSIX +   4)
#define __NR_POSIX_open			(__NR_POSIX +   5)
#define __NR_POSIX_close		(__NR_POSIX +   6)
#define __NR_POSIX_wait			(__NR_POSIX +   7)
#define __NR_POSIX_creat		(__NR_POSIX +   8)
#define __NR_POSIX_link			(__NR_POSIX +   9)
#define __NR_POSIX_unlink		(__NR_POSIX +  10)
#define __NR_POSIX_exec			(__NR_POSIX +  11)
#define __NR_POSIX_chdir		(__NR_POSIX +  12)
#define __NR_POSIX_gtime		(__NR_POSIX +  13)
#define __NR_POSIX_mknod		(__NR_POSIX +  14)
#define __NR_POSIX_chmod		(__NR_POSIX +  15)
#define __NR_POSIX_chown		(__NR_POSIX +  16)
#define __NR_POSIX_sbreak		(__NR_POSIX +  17)
#define __NR_POSIX_stat			(__NR_POSIX +  18)
#define __NR_POSIX_lseek		(__NR_POSIX +  19)
#define __NR_POSIX_getpid		(__NR_POSIX +  20)
#define __NR_POSIX_mount		(__NR_POSIX +  21)
#define __NR_POSIX_umount		(__NR_POSIX +  22)
#define __NR_POSIX_setuid		(__NR_POSIX +  23)
#define __NR_POSIX_getuid		(__NR_POSIX +  24)
#define __NR_POSIX_stime		(__NR_POSIX +  25)
#define __NR_POSIX_ptrace		(__NR_POSIX +  26)
#define __NR_POSIX_alarm		(__NR_POSIX +  27)
#define __NR_POSIX_fstat		(__NR_POSIX +  28)
#define __NR_POSIX_pause		(__NR_POSIX +  29)
#define __NR_POSIX_utime		(__NR_POSIX +  30)
#define __NR_POSIX_stty			(__NR_POSIX +  31)
#define __NR_POSIX_gtty			(__NR_POSIX +  32)
#define __NR_POSIX_access		(__NR_POSIX +  33)
#define __NR_POSIX_nice			(__NR_POSIX +  34)
#define __NR_POSIX_statfs		(__NR_POSIX +  35)
#define __NR_POSIX_sync			(__NR_POSIX +  36)
#define __NR_POSIX_kill			(__NR_POSIX +  37)
#define __NR_POSIX_fstatfs		(__NR_POSIX +  38)
#define __NR_POSIX_getpgrp		(__NR_POSIX +  39)
#define __NR_POSIX_syssgi		(__NR_POSIX +  40)
#define __NR_POSIX_dup			(__NR_POSIX +  41)
#define __NR_POSIX_pipe			(__NR_POSIX +  42)
#define __NR_POSIX_times		(__NR_POSIX +  43)
#define __NR_POSIX_profil		(__NR_POSIX +  44)
#define __NR_POSIX_lock			(__NR_POSIX +  45)
#define __NR_POSIX_setgid		(__NR_POSIX +  46)
#define __NR_POSIX_getgid		(__NR_POSIX +  47)
#define __NR_POSIX_sig			(__NR_POSIX +  48)
#define __NR_POSIX_msgsys		(__NR_POSIX +  49)
#define __NR_POSIX_sysmips		(__NR_POSIX +  50)
#define __NR_POSIX_sysacct		(__NR_POSIX +  51)
#define __NR_POSIX_shmsys		(__NR_POSIX +  52)
#define __NR_POSIX_semsys		(__NR_POSIX +  53)
#define __NR_POSIX_ioctl		(__NR_POSIX +  54)
#define __NR_POSIX_uadmin		(__NR_POSIX +  55)
#define __NR_POSIX_exch			(__NR_POSIX +  56)
#define __NR_POSIX_utssys		(__NR_POSIX +  57)
#define __NR_POSIX_USG_reserved1	(__NR_POSIX +  58)
#define __NR_POSIX_exece		(__NR_POSIX +  59)
#define __NR_POSIX_umask		(__NR_POSIX +  60)
#define __NR_POSIX_chroot		(__NR_POSIX +  61)
#define __NR_POSIX_fcntl		(__NR_POSIX +  62)
#define __NR_POSIX_ulimit		(__NR_POSIX +  63)
#define __NR_POSIX_SAFARI4_reserved1	(__NR_POSIX +  64)
#define __NR_POSIX_SAFARI4_reserved2	(__NR_POSIX +  65)
#define __NR_POSIX_SAFARI4_reserved3	(__NR_POSIX +  66)
#define __NR_POSIX_SAFARI4_reserved4	(__NR_POSIX +  67)
#define __NR_POSIX_SAFARI4_reserved5	(__NR_POSIX +  68)
#define __NR_POSIX_SAFARI4_reserved6	(__NR_POSIX +  69)
#define __NR_POSIX_advfs		(__NR_POSIX +  70)
#define __NR_POSIX_unadvfs		(__NR_POSIX +  71)
#define __NR_POSIX_rmount		(__NR_POSIX +  72)
#define __NR_POSIX_rumount		(__NR_POSIX +  73)
#define __NR_POSIX_rfstart		(__NR_POSIX +  74)
#define __NR_POSIX_reserved1		(__NR_POSIX +  75)
#define __NR_POSIX_rdebug		(__NR_POSIX +  76)
#define __NR_POSIX_rfstop		(__NR_POSIX +  77)
#define __NR_POSIX_rfsys		(__NR_POSIX +  78)
#define __NR_POSIX_rmdir		(__NR_POSIX +  79)
#define __NR_POSIX_mkdir		(__NR_POSIX +  80)
#define __NR_POSIX_getdents		(__NR_POSIX +  81)
#define __NR_POSIX_sginap		(__NR_POSIX +  82)
#define __NR_POSIX_sgikopt		(__NR_POSIX +  83)
#define __NR_POSIX_sysfs		(__NR_POSIX +  84)
#define __NR_POSIX_getmsg		(__NR_POSIX +  85)
#define __NR_POSIX_putmsg		(__NR_POSIX +  86)
#define __NR_POSIX_poll			(__NR_POSIX +  87)
#define __NR_POSIX_sigreturn		(__NR_POSIX +  88)
#define __NR_POSIX_accept		(__NR_POSIX +  89)
#define __NR_POSIX_bind			(__NR_POSIX +  90)
#define __NR_POSIX_connect		(__NR_POSIX +  91)
#define __NR_POSIX_gethostid		(__NR_POSIX +  92)
#define __NR_POSIX_getpeername		(__NR_POSIX +  93)
#define __NR_POSIX_getsockname		(__NR_POSIX +  94)
#define __NR_POSIX_getsockopt		(__NR_POSIX +  95)
#define __NR_POSIX_listen		(__NR_POSIX +  96)
#define __NR_POSIX_recv			(__NR_POSIX +  97)
#define __NR_POSIX_recvfrom		(__NR_POSIX +  98)
#define __NR_POSIX_recvmsg		(__NR_POSIX +  99)
#define __NR_POSIX_select		(__NR_POSIX + 100)
#define __NR_POSIX_send			(__NR_POSIX + 101)
#define __NR_POSIX_sendmsg		(__NR_POSIX + 102)
#define __NR_POSIX_sendto		(__NR_POSIX + 103)
#define __NR_POSIX_sethostid		(__NR_POSIX + 104)
#define __NR_POSIX_setsockopt		(__NR_POSIX + 105)
#define __NR_POSIX_shutdown		(__NR_POSIX + 106)
#define __NR_POSIX_socket		(__NR_POSIX + 107)
#define __NR_POSIX_gethostname		(__NR_POSIX + 108)
#define __NR_POSIX_sethostname		(__NR_POSIX + 109)
#define __NR_POSIX_getdomainname	(__NR_POSIX + 110)
#define __NR_POSIX_setdomainname	(__NR_POSIX + 111)
#define __NR_POSIX_truncate		(__NR_POSIX + 112)
#define __NR_POSIX_ftruncate		(__NR_POSIX + 113)
#define __NR_POSIX_rename		(__NR_POSIX + 114)
#define __NR_POSIX_symlink		(__NR_POSIX + 115)
#define __NR_POSIX_readlink		(__NR_POSIX + 116)
#define __NR_POSIX_lstat		(__NR_POSIX + 117)
#define __NR_POSIX_nfs_mount		(__NR_POSIX + 118)
#define __NR_POSIX_nfs_svc		(__NR_POSIX + 119)
#define __NR_POSIX_nfs_getfh		(__NR_POSIX + 120)
#define __NR_POSIX_async_daemon		(__NR_POSIX + 121)
#define __NR_POSIX_exportfs		(__NR_POSIX + 122)
#define __NR_POSIX_SGI_setregid		(__NR_POSIX + 123)
#define __NR_POSIX_SGI_setreuid		(__NR_POSIX + 124)
#define __NR_POSIX_getitimer		(__NR_POSIX + 125)
#define __NR_POSIX_setitimer		(__NR_POSIX + 126)
#define __NR_POSIX_adjtime		(__NR_POSIX + 127)
#define __NR_POSIX_SGI_bsdgettime	(__NR_POSIX + 128)
#define __NR_POSIX_SGI_sproc		(__NR_POSIX + 129)
#define __NR_POSIX_SGI_prctl		(__NR_POSIX + 130)
#define __NR_POSIX_SGI_blkproc		(__NR_POSIX + 131)
#define __NR_POSIX_SGI_reserved1	(__NR_POSIX + 132)
#define __NR_POSIX_SGI_sgigsc		(__NR_POSIX + 133)
#define __NR_POSIX_SGI_mmap		(__NR_POSIX + 134)
#define __NR_POSIX_SGI_munmap		(__NR_POSIX + 135)
#define __NR_POSIX_SGI_mprotect		(__NR_POSIX + 136)
#define __NR_POSIX_SGI_msync		(__NR_POSIX + 137)
#define __NR_POSIX_SGI_madvise		(__NR_POSIX + 138)
#define __NR_POSIX_SGI_mpin		(__NR_POSIX + 139)
#define __NR_POSIX_SGI_getpagesize	(__NR_POSIX + 140)
#define __NR_POSIX_SGI_libattach	(__NR_POSIX + 141)
#define __NR_POSIX_SGI_libdetach	(__NR_POSIX + 142)
#define __NR_POSIX_SGI_getpgrp		(__NR_POSIX + 143)
#define __NR_POSIX_SGI_setpgrp		(__NR_POSIX + 144)
#define __NR_POSIX_SGI_reserved2	(__NR_POSIX + 145)
#define __NR_POSIX_SGI_reserved3	(__NR_POSIX + 146)
#define __NR_POSIX_SGI_reserved4	(__NR_POSIX + 147)
#define __NR_POSIX_SGI_reserved5	(__NR_POSIX + 148)
#define __NR_POSIX_SGI_reserved6	(__NR_POSIX + 149)
#define __NR_POSIX_cacheflush		(__NR_POSIX + 150)
#define __NR_POSIX_cachectl		(__NR_POSIX + 151)
#define __NR_POSIX_fchown		(__NR_POSIX + 152)
#define __NR_POSIX_fchmod		(__NR_POSIX + 153)
#define __NR_POSIX_wait3		(__NR_POSIX + 154)
#define __NR_POSIX_mmap			(__NR_POSIX + 155)
#define __NR_POSIX_munmap		(__NR_POSIX + 156)
#define __NR_POSIX_madvise		(__NR_POSIX + 157)
#define __NR_POSIX_BSD_getpagesize	(__NR_POSIX + 158)
#define __NR_POSIX_setreuid		(__NR_POSIX + 159)
#define __NR_POSIX_setregid		(__NR_POSIX + 160)
#define __NR_POSIX_setpgid		(__NR_POSIX + 161)
#define __NR_POSIX_getgroups		(__NR_POSIX + 162)
#define __NR_POSIX_setgroups		(__NR_POSIX + 163)
#define __NR_POSIX_gettimeofday		(__NR_POSIX + 164)
#define __NR_POSIX_getrusage		(__NR_POSIX + 165)
#define __NR_POSIX_getrlimit		(__NR_POSIX + 166)
#define __NR_POSIX_setrlimit		(__NR_POSIX + 167)
#define __NR_POSIX_waitpid		(__NR_POSIX + 168)
#define __NR_POSIX_dup2			(__NR_POSIX + 169)
#define __NR_POSIX_reserved2		(__NR_POSIX + 170)
#define __NR_POSIX_reserved3		(__NR_POSIX + 171)
#define __NR_POSIX_reserved4		(__NR_POSIX + 172)
#define __NR_POSIX_reserved5		(__NR_POSIX + 173)
#define __NR_POSIX_reserved6		(__NR_POSIX + 174)
#define __NR_POSIX_reserved7		(__NR_POSIX + 175)
#define __NR_POSIX_reserved8		(__NR_POSIX + 176)
#define __NR_POSIX_reserved9		(__NR_POSIX + 177)
#define __NR_POSIX_reserved10		(__NR_POSIX + 178)
#define __NR_POSIX_reserved11		(__NR_POSIX + 179)
#define __NR_POSIX_reserved12		(__NR_POSIX + 180)
#define __NR_POSIX_reserved13		(__NR_POSIX + 181)
#define __NR_POSIX_reserved14		(__NR_POSIX + 182)
#define __NR_POSIX_reserved15		(__NR_POSIX + 183)
#define __NR_POSIX_reserved16		(__NR_POSIX + 184)
#define __NR_POSIX_reserved17		(__NR_POSIX + 185)
#define __NR_POSIX_reserved18		(__NR_POSIX + 186)
#define __NR_POSIX_reserved19		(__NR_POSIX + 187)
#define __NR_POSIX_reserved20		(__NR_POSIX + 188)
#define __NR_POSIX_reserved21		(__NR_POSIX + 189)
#define __NR_POSIX_reserved22		(__NR_POSIX + 190)
#define __NR_POSIX_reserved23		(__NR_POSIX + 191)
#define __NR_POSIX_reserved24		(__NR_POSIX + 192)
#define __NR_POSIX_reserved25		(__NR_POSIX + 193)
#define __NR_POSIX_reserved26		(__NR_POSIX + 194)
#define __NR_POSIX_reserved27		(__NR_POSIX + 195)
#define __NR_POSIX_reserved28		(__NR_POSIX + 196)
#define __NR_POSIX_reserved29		(__NR_POSIX + 197)
#define __NR_POSIX_reserved30		(__NR_POSIX + 198)
#define __NR_POSIX_reserved31		(__NR_POSIX + 199)
#define __NR_POSIX_reserved32		(__NR_POSIX + 200)
#define __NR_POSIX_reserved33		(__NR_POSIX + 201)
#define __NR_POSIX_reserved34		(__NR_POSIX + 202)
#define __NR_POSIX_reserved35		(__NR_POSIX + 203)
#define __NR_POSIX_reserved36		(__NR_POSIX + 204)
#define __NR_POSIX_reserved37		(__NR_POSIX + 205)
#define __NR_POSIX_reserved38		(__NR_POSIX + 206)
#define __NR_POSIX_reserved39		(__NR_POSIX + 207)
#define __NR_POSIX_reserved40		(__NR_POSIX + 208)
#define __NR_POSIX_reserved41		(__NR_POSIX + 209)
#define __NR_POSIX_reserved42		(__NR_POSIX + 210)
#define __NR_POSIX_reserved43		(__NR_POSIX + 211)
#define __NR_POSIX_reserved44		(__NR_POSIX + 212)
#define __NR_POSIX_reserved45		(__NR_POSIX + 213)
#define __NR_POSIX_reserved46		(__NR_POSIX + 214)
#define __NR_POSIX_reserved47		(__NR_POSIX + 215)
#define __NR_POSIX_reserved48		(__NR_POSIX + 216)
#define __NR_POSIX_reserved49		(__NR_POSIX + 217)
#define __NR_POSIX_reserved50		(__NR_POSIX + 218)
#define __NR_POSIX_reserved51		(__NR_POSIX + 219)
#define __NR_POSIX_reserved52		(__NR_POSIX + 220)
#define __NR_POSIX_reserved53		(__NR_POSIX + 221)
#define __NR_POSIX_reserved54		(__NR_POSIX + 222)
#define __NR_POSIX_reserved55		(__NR_POSIX + 223)
#define __NR_POSIX_reserved56		(__NR_POSIX + 224)
#define __NR_POSIX_reserved57		(__NR_POSIX + 225)
#define __NR_POSIX_reserved58		(__NR_POSIX + 226)
#define __NR_POSIX_reserved59		(__NR_POSIX + 227)
#define __NR_POSIX_reserved60		(__NR_POSIX + 228)
#define __NR_POSIX_reserved61		(__NR_POSIX + 229)
#define __NR_POSIX_reserved62		(__NR_POSIX + 230)
#define __NR_POSIX_reserved63		(__NR_POSIX + 231)
#define __NR_POSIX_reserved64		(__NR_POSIX + 232)
#define __NR_POSIX_reserved65		(__NR_POSIX + 233)
#define __NR_POSIX_reserved66		(__NR_POSIX + 234)
#define __NR_POSIX_reserved67		(__NR_POSIX + 235)
#define __NR_POSIX_reserved68		(__NR_POSIX + 236)
#define __NR_POSIX_reserved69		(__NR_POSIX + 237)
#define __NR_POSIX_reserved70		(__NR_POSIX + 238)
#define __NR_POSIX_reserved71		(__NR_POSIX + 239)
#define __NR_POSIX_reserved72		(__NR_POSIX + 240)
#define __NR_POSIX_reserved73		(__NR_POSIX + 241)
#define __NR_POSIX_reserved74		(__NR_POSIX + 242)
#define __NR_POSIX_reserved75		(__NR_POSIX + 243)
#define __NR_POSIX_reserved76		(__NR_POSIX + 244)
#define __NR_POSIX_reserved77		(__NR_POSIX + 245)
#define __NR_POSIX_reserved78		(__NR_POSIX + 246)
#define __NR_POSIX_reserved79		(__NR_POSIX + 247)
#define __NR_POSIX_reserved80		(__NR_POSIX + 248)
#define __NR_POSIX_reserved81		(__NR_POSIX + 249)
#define __NR_POSIX_reserved82		(__NR_POSIX + 250)
#define __NR_POSIX_reserved83		(__NR_POSIX + 251)
#define __NR_POSIX_reserved84		(__NR_POSIX + 252)
#define __NR_POSIX_reserved85		(__NR_POSIX + 253)
#define __NR_POSIX_reserved86		(__NR_POSIX + 254)
#define __NR_POSIX_reserved87		(__NR_POSIX + 255)
#define __NR_POSIX_reserved88		(__NR_POSIX + 256)
#define __NR_POSIX_reserved89		(__NR_POSIX + 257)
#define __NR_POSIX_reserved90		(__NR_POSIX + 258)
#define __NR_POSIX_reserved91		(__NR_POSIX + 259)
#define __NR_POSIX_netboot		(__NR_POSIX + 260)
#define __NR_POSIX_netunboot		(__NR_POSIX + 261)
#define __NR_POSIX_rdump		(__NR_POSIX + 262)
#define __NR_POSIX_setsid		(__NR_POSIX + 263)
#define __NR_POSIX_getmaxsig		(__NR_POSIX + 264)
#define __NR_POSIX_sigpending		(__NR_POSIX + 265)
#define __NR_POSIX_sigprocmask		(__NR_POSIX + 266)
#define __NR_POSIX_sigsuspend		(__NR_POSIX + 267)
#define __NR_POSIX_sigaction		(__NR_POSIX + 268)
#define __NR_POSIX_MIPS_reserved1	(__NR_POSIX + 269)
#define __NR_POSIX_MIPS_reserved2	(__NR_POSIX + 270)
#define __NR_POSIX_MIPS_reserved3	(__NR_POSIX + 271)
#define __NR_POSIX_MIPS_reserved4	(__NR_POSIX + 272)
#define __NR_POSIX_MIPS_reserved5	(__NR_POSIX + 273)
#define __NR_POSIX_MIPS_reserved6	(__NR_POSIX + 274)
#define __NR_POSIX_MIPS_reserved7	(__NR_POSIX + 275)
#define __NR_POSIX_MIPS_reserved8	(__NR_POSIX + 276)
#define __NR_POSIX_MIPS_reserved9	(__NR_POSIX + 277)
#define __NR_POSIX_MIPS_reserved10	(__NR_POSIX + 278)
#define __NR_POSIX_MIPS_reserved11	(__NR_POSIX + 279)
#define __NR_POSIX_TANDEM_reserved1	(__NR_POSIX + 280)
#define __NR_POSIX_TANDEM_reserved2	(__NR_POSIX + 281)
#define __NR_POSIX_TANDEM_reserved3	(__NR_POSIX + 282)
#define __NR_POSIX_TANDEM_reserved4	(__NR_POSIX + 283)
#define __NR_POSIX_TANDEM_reserved5	(__NR_POSIX + 284)
#define __NR_POSIX_TANDEM_reserved6	(__NR_POSIX + 285)
#define __NR_POSIX_TANDEM_reserved7	(__NR_POSIX + 286)
#define __NR_POSIX_TANDEM_reserved8	(__NR_POSIX + 287)
#define __NR_POSIX_TANDEM_reserved9	(__NR_POSIX + 288)
#define __NR_POSIX_TANDEM_reserved10	(__NR_POSIX + 289)
#define __NR_POSIX_TANDEM_reserved11	(__NR_POSIX + 290)
#define __NR_POSIX_TANDEM_reserved12	(__NR_POSIX + 291)
#define __NR_POSIX_TANDEM_reserved13	(__NR_POSIX + 292)
#define __NR_POSIX_TANDEM_reserved14	(__NR_POSIX + 293)
#define __NR_POSIX_TANDEM_reserved15	(__NR_POSIX + 294)
#define __NR_POSIX_TANDEM_reserved16	(__NR_POSIX + 295)
#define __NR_POSIX_TANDEM_reserved17	(__NR_POSIX + 296)
#define __NR_POSIX_TANDEM_reserved18	(__NR_POSIX + 297)
#define __NR_POSIX_TANDEM_reserved19	(__NR_POSIX + 298)
#define __NR_POSIX_TANDEM_reserved20	(__NR_POSIX + 299)
#define __NR_POSIX_SGI_reserved7	(__NR_POSIX + 300)
#define __NR_POSIX_SGI_reserved8	(__NR_POSIX + 301)
#define __NR_POSIX_SGI_reserved9	(__NR_POSIX + 302)
#define __NR_POSIX_SGI_reserved10	(__NR_POSIX + 303)
#define __NR_POSIX_SGI_reserved11	(__NR_POSIX + 304)
#define __NR_POSIX_SGI_reserved12	(__NR_POSIX + 305)
#define __NR_POSIX_SGI_reserved13	(__NR_POSIX + 306)
#define __NR_POSIX_SGI_reserved14	(__NR_POSIX + 307)
#define __NR_POSIX_SGI_reserved15	(__NR_POSIX + 308)
#define __NR_POSIX_SGI_reserved16	(__NR_POSIX + 309)
#define __NR_POSIX_SGI_reserved17	(__NR_POSIX + 310)
#define __NR_POSIX_SGI_reserved18	(__NR_POSIX + 311)
#define __NR_POSIX_SGI_reserved19	(__NR_POSIX + 312)
#define __NR_POSIX_SGI_reserved20	(__NR_POSIX + 313)
#define __NR_POSIX_SGI_reserved21	(__NR_POSIX + 314)
#define __NR_POSIX_SGI_reserved22	(__NR_POSIX + 315)
#define __NR_POSIX_SGI_reserved23	(__NR_POSIX + 316)
#define __NR_POSIX_SGI_reserved24	(__NR_POSIX + 317)
#define __NR_POSIX_SGI_reserved25	(__NR_POSIX + 318)
#define __NR_POSIX_SGI_reserved26	(__NR_POSIX + 319)

/*
 * Linux syscalls are in the range from 4000 to 4999
 * Hopefully these syscall numbers are unused ...  If not everyone using
 * statically linked binaries is pretty upsh*t.  You've been warned.
 */
#define __NR_Linux			4000
#define __NR_syscall			(__NR_Linux +   0)
#define __NR_exit			(__NR_Linux +   1)
#define __NR_fork			(__NR_Linux +   2)
#define __NR_read			(__NR_Linux +   3)
#define __NR_write			(__NR_Linux +   4)
#define __NR_open			(__NR_Linux +   5)
#define __NR_close			(__NR_Linux +   6)
#define __NR_waitpid			(__NR_Linux +   7)
#define __NR_creat			(__NR_Linux +   8)
#define __NR_link			(__NR_Linux +   9)
#define __NR_unlink			(__NR_Linux +  10)
#define __NR_execve			(__NR_Linux +  11)
#define __NR_chdir			(__NR_Linux +  12)
#define __NR_time			(__NR_Linux +  13)
#define __NR_mknod			(__NR_Linux +  14)
#define __NR_chmod			(__NR_Linux +  15)
#define __NR_chown			(__NR_Linux +  16)
#define __NR_break			(__NR_Linux +  17)
#define __NR_oldstat			(__NR_Linux +  18)
#define __NR_lseek			(__NR_Linux +  19)
#define __NR_getpid			(__NR_Linux +  20)
#define __NR_mount			(__NR_Linux +  21)
#define __NR_umount			(__NR_Linux +  22)
#define __NR_setuid			(__NR_Linux +  23)
#define __NR_getuid			(__NR_Linux +  24)
#define __NR_stime			(__NR_Linux +  25)
#define __NR_ptrace			(__NR_Linux +  26)
#define __NR_alarm			(__NR_Linux +  27)
#define __NR_oldfstat			(__NR_Linux +  28)
#define __NR_pause			(__NR_Linux +  29)
#define __NR_utime			(__NR_Linux +  30)
#define __NR_stty			(__NR_Linux +  31)
#define __NR_gtty			(__NR_Linux +  32)
#define __NR_access			(__NR_Linux +  33)
#define __NR_nice			(__NR_Linux +  34)
#define __NR_ftime			(__NR_Linux +  35)
#define __NR_sync			(__NR_Linux +  36)
#define __NR_kill			(__NR_Linux +  37)
#define __NR_rename			(__NR_Linux +  38)
#define __NR_mkdir			(__NR_Linux +  39)
#define __NR_rmdir			(__NR_Linux +  40)
#define __NR_dup			(__NR_Linux +  41)
#define __NR_pipe			(__NR_Linux +  42)
#define __NR_times			(__NR_Linux +  43)
#define __NR_prof			(__NR_Linux +  44)
#define __NR_brk			(__NR_Linux +  45)
#define __NR_setgid			(__NR_Linux +  46)
#define __NR_getgid			(__NR_Linux +  47)
#define __NR_signal			(__NR_Linux +  48)
#define __NR_geteuid			(__NR_Linux +  49)
#define __NR_getegid			(__NR_Linux +  50)
#define __NR_acct			(__NR_Linux +  51)
#define __NR_phys			(__NR_Linux +  52)
#define __NR_lock			(__NR_Linux +  53)
#define __NR_ioctl			(__NR_Linux +  54)
#define __NR_fcntl			(__NR_Linux +  55)
#define __NR_mpx			(__NR_Linux +  56)
#define __NR_setpgid			(__NR_Linux +  57)
#define __NR_ulimit			(__NR_Linux +  58)
#define __NR_oldolduname		(__NR_Linux +  59)
#define __NR_umask			(__NR_Linux +  60)
#define __NR_chroot			(__NR_Linux +  61)
#define __NR_ustat			(__NR_Linux +  62)
#define __NR_dup2			(__NR_Linux +  63)
#define __NR_getppid			(__NR_Linux +  64)
#define __NR_getpgrp			(__NR_Linux +  65)
#define __NR_setsid			(__NR_Linux +  66)
#define __NR_sigaction			(__NR_Linux +  67)
#define __NR_sgetmask			(__NR_Linux +  68)
#define __NR_ssetmask			(__NR_Linux +  69)
#define __NR_setreuid			(__NR_Linux +  70)
#define __NR_setregid			(__NR_Linux +  71)
#define __NR_sigsuspend			(__NR_Linux +  72)
#define __NR_sigpending			(__NR_Linux +  73)
#define __NR_sethostname		(__NR_Linux +  74)
#define __NR_setrlimit			(__NR_Linux +  75)
#define __NR_getrlimit			(__NR_Linux +  76)
#define __NR_getrusage			(__NR_Linux +  77)
#define __NR_gettimeofday		(__NR_Linux +  78)
#define __NR_settimeofday		(__NR_Linux +  79)
#define __NR_getgroups			(__NR_Linux +  80)
#define __NR_setgroups			(__NR_Linux +  81)
#define __NR_reserved82			(__NR_Linux +  82)
#define __NR_symlink			(__NR_Linux +  83)
#define __NR_oldlstat			(__NR_Linux +  84)
#define __NR_readlink			(__NR_Linux +  85)
#define __NR_uselib			(__NR_Linux +  86)
#define __NR_swapon			(__NR_Linux +  87)
#define __NR_reboot			(__NR_Linux +  88)
#define __NR_readdir			(__NR_Linux +  89)
#define __NR_mmap			(__NR_Linux +  90)
#define __NR_munmap			(__NR_Linux +  91)
#define __NR_truncate			(__NR_Linux +  92)
#define __NR_ftruncate			(__NR_Linux +  93)
#define __NR_fchmod			(__NR_Linux +  94)
#define __NR_fchown			(__NR_Linux +  95)
#define __NR_getpriority		(__NR_Linux +  96)
#define __NR_setpriority		(__NR_Linux +  97)
#define __NR_profil			(__NR_Linux +  98)
#define __NR_statfs			(__NR_Linux +  99)
#define __NR_fstatfs			(__NR_Linux + 100)
#define __NR_ioperm			(__NR_Linux + 101)
#define __NR_socketcall			(__NR_Linux + 102)
#define __NR_syslog			(__NR_Linux + 103)
#define __NR_setitimer			(__NR_Linux + 104)
#define __NR_getitimer			(__NR_Linux + 105)
#define __NR_stat			(__NR_Linux + 106)
#define __NR_lstat			(__NR_Linux + 107)
#define __NR_fstat			(__NR_Linux + 108)
#define __NR_olduname			(__NR_Linux + 109)
#define __NR_iopl			(__NR_Linux + 110)
#define __NR_vhangup			(__NR_Linux + 111)
#define __NR_idle			(__NR_Linux + 112)
#define __NR_vm86			(__NR_Linux + 113)
#define __NR_wait4			(__NR_Linux + 114)
#define __NR_swapoff			(__NR_Linux + 115)
#define __NR_sysinfo			(__NR_Linux + 116)
#define __NR_ipc			(__NR_Linux + 117)
#define __NR_fsync			(__NR_Linux + 118)
#define __NR_sigreturn			(__NR_Linux + 119)
#define __NR_clone			(__NR_Linux + 120)
#define __NR_setdomainname		(__NR_Linux + 121)
#define __NR_uname			(__NR_Linux + 122)
#define __NR_modify_ldt			(__NR_Linux + 123)
#define __NR_adjtimex			(__NR_Linux + 124)
#define __NR_mprotect			(__NR_Linux + 125)
#define __NR_sigprocmask		(__NR_Linux + 126)
#define __NR_create_module		(__NR_Linux + 127)
#define __NR_init_module		(__NR_Linux + 128)
#define __NR_delete_module		(__NR_Linux + 129)
#define __NR_get_kernel_syms		(__NR_Linux + 130)
#define __NR_quotactl			(__NR_Linux + 131)
#define __NR_getpgid			(__NR_Linux + 132)
#define __NR_fchdir			(__NR_Linux + 133)
#define __NR_bdflush			(__NR_Linux + 134)
#define __NR_sysfs			(__NR_Linux + 135)
#define __NR_personality		(__NR_Linux + 136)
#define __NR_afs_syscall		(__NR_Linux + 137) /* Syscall for Andrew File System */
#define __NR_setfsuid			(__NR_Linux + 138)
#define __NR_setfsgid			(__NR_Linux + 139)
#define __NR__llseek			(__NR_Linux + 140)
#define __NR_getdents			(__NR_Linux + 141)
#define __NR__newselect			(__NR_Linux + 142)
#define __NR_flock			(__NR_Linux + 143)
#define __NR_msync			(__NR_Linux + 144)
#define __NR_readv			(__NR_Linux + 145)
#define __NR_writev			(__NR_Linux + 146)
#define __NR_cacheflush			(__NR_Linux + 147)
#define __NR_cachectl			(__NR_Linux + 148)
#define __NR_sysmips			(__NR_Linux + 149)
#define __NR_setup			(__NR_Linux + 150)	/* used only by init, to get system going */
#define __NR_getsid			(__NR_Linux + 151)
#define __NR_fdatasync			(__NR_Linux + 152)
#define __NR__sysctl			(__NR_Linux + 153)
#define __NR_mlock			(__NR_Linux + 154)
#define __NR_munlock			(__NR_Linux + 155)
#define __NR_mlockall			(__NR_Linux + 156)
#define __NR_munlockall			(__NR_Linux + 157)
#define __NR_sched_setparam		(__NR_Linux + 158)
#define __NR_sched_getparam		(__NR_Linux + 159)
#define __NR_sched_setscheduler		(__NR_Linux + 160)
#define __NR_sched_getscheduler		(__NR_Linux + 161)
#define __NR_sched_yield		(__NR_Linux + 162)
#define __NR_sched_get_priority_max	(__NR_Linux + 163)
#define __NR_sched_get_priority_min	(__NR_Linux + 164)
#define __NR_sched_rr_get_interval	(__NR_Linux + 165)
#define __NR_nanosleep			(__NR_Linux + 166)
#define __NR_mremap			(__NR_Linux + 167)
#define __NR_accept			(__NR_Linux + 168)
#define __NR_bind			(__NR_Linux + 169)
#define __NR_connect			(__NR_Linux + 170)
#define __NR_getpeername		(__NR_Linux + 171)
#define __NR_getsockname		(__NR_Linux + 172)
#define __NR_getsockopt			(__NR_Linux + 173)
#define __NR_listen			(__NR_Linux + 174)
#define __NR_recv			(__NR_Linux + 175)
#define __NR_recvfrom			(__NR_Linux + 176)
#define __NR_recvmsg			(__NR_Linux + 177)
#define __NR_send			(__NR_Linux + 178)
#define __NR_sendmsg			(__NR_Linux + 179)
#define __NR_sendto			(__NR_Linux + 180)
#define __NR_setsockopt			(__NR_Linux + 181)
#define __NR_shutdown			(__NR_Linux + 182)
#define __NR_socket			(__NR_Linux + 183)
#define __NR_socketpair			(__NR_Linux + 184)
#define __NR_setresuid			(__NR_Linux + 185)
#define __NR_getresuid			(__NR_Linux + 186)
#define __NR_query_module		(__NR_Linux + 187)
#define __NR_poll			(__NR_Linux + 188)
#define __NR_nfsservctl			(__NR_Linux + 189)
#define __NR_setresgid			(__NR_Linux + 190)
#define __NR_getresgid			(__NR_Linux + 191)

/*
 * Offset of the last Linux flavoured syscall
 */
#define __NR_Linux_syscalls		191

#ifndef __LANGUAGE_ASSEMBLY__

/* XXX - _foo needs to be __foo, while __NR_bar could be _NR_bar. */
#define _syscall0(type,name) \
type name(void) \
{ \
register long __res __asm__ ("$2"); \
register long __err __asm__ ("$7"); \
__asm__ volatile ("li\t$2,%2\n\t" \
		  "syscall" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name)); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

/*
 * DANGER: This macro isn't usable for the pipe(2) call
 * which has a unusual return convention.
 */
#define _syscall1(type,name,atype,a) \
type name(atype a) \
{ \
register long __res __asm__ ("$2"); \
register long __err __asm__ ("$7"); \
__asm__ volatile ("move\t$4,%3\n\t" \
		  "li\t$2,%2\n\t" \
                  "syscall" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)) \
                  : "$4"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall2(type,name,atype,a,btype,b) \
type name(atype a,btype b) \
{ \
register long __res __asm__ ("$2"); \
register long __err __asm__ ("$7"); \
__asm__ volatile ("move\t$4,%3\n\t" \
                  "move\t$5,%4\n\t" \
		  "li\t$2,%2\n\t" \
                  "syscall" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)) \
                  : "$4","$5"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall3(type,name,atype,a,btype,b,ctype,c) \
type name (atype a, btype b, ctype c) \
{ \
register long __res __asm__ ("$2"); \
register long __err __asm__ ("$7"); \
__asm__ volatile ("move\t$4,%3\n\t" \
                  "move\t$5,%4\n\t" \
                  "move\t$6,%5\n\t" \
		  "li\t$2,%2\n\t" \
                  "syscall" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)) \
                  : "$4","$5","$6"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall4(type,name,atype,a,btype,b,ctype,c,dtype,d) \
type name (atype a, btype b, ctype c, dtype d) \
{ \
register long __res __asm__ ("$2"); \
register long __err __asm__ ("$7"); \
__asm__ volatile ("move\t$4,%3\n\t" \
                  "move\t$5,%4\n\t" \
                  "move\t$6,%5\n\t" \
                  "move\t$7,%6\n\t" \
		  "li\t$2,%2\n\t" \
                  "syscall" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)) \
                  : "$4","$5","$6"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall5(type,name,atype,a,btype,b,ctype,c,dtype,d,etype,e) \
type name (atype a,btype b,ctype c,dtype d,etype e) \
{ \
register long __res __asm__ ("$2"); \
register long __err __asm__ ("$7"); \
__asm__ volatile ("move\t$4,%3\n\t" \
                  "move\t$5,%4\n\t" \
                  "move\t$6,%5\n\t" \
		  "lw\t$2,%7\n\t" \
                  "move\t$7,%6\n\t" \
		  "subu\t$29,24\n\t" \
		  "sw\t$2,16($29)\n\t" \
		  "li\t$2,%2\n\t" \
                  "syscall\n\t" \
		  "addiu\t$29,24" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)), \
                                      "m" ((long)(e)) \
                  : "$2","$4","$5","$6","$7"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall6(type,name,atype,a,btype,b,ctype,c,dtype,d,etype,e,ftype,f) \
type name (atype a,btype b,ctype c,dtype d,etype e,ftype f) \
{ \
register long __res __asm__ ("$2"); \
register long __err __asm__ ("$7"); \
__asm__ volatile ("move\t$4,%3\n\t" \
                  "move\t$5,%4\n\t" \
                  "move\t$6,%5\n\t" \
		  "lw\t$2,%7\n\t" \
		  "lw\t$3,%8\n\t" \
                  "move\t$7,%6\n\t" \
		  "subu\t$29,24\n\t" \
		  "sw\t$2,16($29)\n\t" \
		  "sw\t$3,20($29)\n\t" \
		  "li\t$2,%2\n\t" \
                  "syscall\n\t" \
		  "addiu\t$29,24" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)), \
                                      "m" ((long)(e)), \
                                      "m" ((long)(f)) \
                  : "$2","$3","$4","$5","$6","$7"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
return -1; \
}

#define _syscall7(type,name,atype,a,btype,b,ctype,c,dtype,d,etype,e,ftype,f,gtype,g) \
type name (atype a,btype b,ctype c,dtype d,etype e,ftype f,gtype g) \
{ \
register long __res __asm__ ("$2"); \
register long __err __asm__ ("$7"); \
__asm__ volatile ("move\t$4,%3\n\t" \
                  "move\t$5,%4\n\t" \
                  "move\t$6,%5\n\t" \
		  "lw\t$2,%7\n\t" \
		  "lw\t$3,%8\n\t" \
                  "move\t$7,%6\n\t" \
		  "subu\t$29,32\n\t" \
		  "sw\t$2,16($29)\n\t" \
		  "lw\t$2,%9\n\t" \
		  "sw\t$3,20($29)\n\t" \
		  "sw\t$2,24($29)\n\t" \
		  "li\t$2,%2\n\t" \
                  "syscall\n\t" \
		  "addiu\t$29,32" \
                  : "=r" (__res), "=r" (__err) \
                  : "i" (__NR_##name),"r" ((long)(a)), \
                                      "r" ((long)(b)), \
                                      "r" ((long)(c)), \
                                      "r" ((long)(d)), \
                                      "m" ((long)(e)), \
                                      "m" ((long)(f)), \
                                      "m" ((long)(g)) \
                  : "$2","$3","$4","$5","$6","$7"); \
if (__err == 0) \
	return (type) __res; \
errno = __res; \
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
static inline _syscall0(int,idle)
static inline _syscall0(int,fork)
static inline _syscall2(int,clone,unsigned long,flags,char *,esp)
static inline _syscall0(int,pause)
static inline _syscall1(int,setup,int,magic)
static inline _syscall0(int,sync)
static inline _syscall0(pid_t,setsid)
static inline _syscall3(int,write,int,fd,const char *,buf,off_t,count)
static inline _syscall1(int,dup,int,fd)
static inline _syscall3(int,execve,const char *,file,char **,argv,char **,envp)
static inline _syscall3(int,open,const char *,file,int,flag,int,mode)
static inline _syscall1(int,close,int,fd)
static inline _syscall1(int,_exit,int,exitcode)
static inline _syscall3(pid_t,waitpid,pid_t,pid,int *,wait_stat,int,options)

static inline pid_t wait(int * wait_stat)
{
	return waitpid(-1,wait_stat,0);
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

	__asm__ __volatile__(
		".set\tnoreorder\n\t"
		"move\t$8,$sp\n\t"
		"move\t$4,%5\n\t"
		"li\t$2,%1\n\t"
		"syscall\n\t"
		"beq\t$8,$sp,1f\n\t"
		"subu\t$sp,32\n\t"	/* delay slot */
		"jalr\t%4\n\t"
		"move\t$4,%3\n\t"	/* delay slot */
		"move\t$4,$2\n\t"
		"li\t$2,%2\n\t"
		"syscall\n"
		"1:\taddiu\t$sp,32\n\t"
		"move\t%0,$2\n\t"
		".set\treorder"
		:"=r" (retval)
		:"i" (__NR_clone), "i" (__NR_exit),
		 "r" (arg), "r" (fn),
		 "r" (flags | CLONE_VM)
		 /*
		  * The called subroutine might have destroyed any of the
		  * at, result, argument or temporary registers ...
		  */
		:"$1", "$2", "$3", "$4", "$5", "$6", "$7", "$8",
		 "$9","$10","$11","$12","$13","$14","$15","$24","$25");

	return retval;
}

#endif /* !defined (__KERNEL_SYSCALLS__) */
#endif /* !defined (__LANGUAGE_ASSEMBLY__) */

#endif /* __ASM_MIPS_UNISTD_H */
