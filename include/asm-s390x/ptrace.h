/*
 *  include/asm-s390/ptrace.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 */

#ifndef _S390_PTRACE_H
#define _S390_PTRACE_H

/*
 * Offsets in the user_regs_struct. They are used for the ptrace
 * system call and in entry.S
 */
#define PT_PSWMASK  0x00
#define PT_PSWADDR  0x08
#define PT_GPR0     0x10
#define PT_GPR1     0x18
#define PT_GPR2     0x20
#define PT_GPR3     0x28
#define PT_GPR4     0x30
#define PT_GPR5     0x38
#define PT_GPR6     0x40
#define PT_GPR7     0x48
#define PT_GPR8     0x50
#define PT_GPR9     0x58
#define PT_GPR10    0x60
#define PT_GPR11    0x68
#define PT_GPR12    0x70
#define PT_GPR13    0x78
#define PT_GPR14    0x80
#define PT_GPR15    0x88
#define PT_ACR0     0x90
#define PT_ACR1     0x94
#define PT_ACR2     0x98
#define PT_ACR3     0x9C
#define PT_ACR4	    0xA0
#define PT_ACR5	    0xA4
#define PT_ACR6	    0xA8
#define PT_ACR7	    0xAC
#define PT_ACR8	    0xB0
#define PT_ACR9	    0xB4
#define PT_ACR10    0xB8
#define PT_ACR11    0xBC
#define PT_ACR12    0xC0
#define PT_ACR13    0xC4
#define PT_ACR14    0xC8
#define PT_ACR15    0xCC
#define PT_ORIGGPR2 0xD0
#define PT_FPC	    0xD8
#define PT_FPR0     0xE0
#define PT_FPR1     0xE8
#define PT_FPR2     0xF0
#define PT_FPR3     0xF8
#define PT_FPR4     0x100
#define PT_FPR5     0x108
#define PT_FPR6     0x110
#define PT_FPR7     0x118
#define PT_FPR8     0x120
#define PT_FPR9     0x128
#define PT_FPR10    0x130
#define PT_FPR11    0x138
#define PT_FPR12    0x140
#define PT_FPR13    0x148
#define PT_FPR14    0x150
#define PT_FPR15    0x158
#define PT_CR_9     0x160
#define PT_CR_10    0x168
#define PT_CR_11    0x170
#define PT_IEEE_IP  0x1A8
#define PT_LASTOFF  PT_IEEE_IP
#define PT_ENDREGS  0x1B0-1

#define NUM_GPRS      16
#define NUM_FPRS      16
#define NUM_CRS       16
#define NUM_ACRS      16
#define GPR_SIZE      8
#define FPR_SIZE      8
#define FPC_SIZE      4
#define FPC_PAD_SIZE  4 /* gcc insists on aligning the fpregs */
#define CR_SIZE       8
#define ACR_SIZE      4

#define STACK_FRAME_OVERHEAD    160      /* size of minimum stack frame */

#define PTRACE_OLDSETOPTIONS         21

#ifndef __ASSEMBLY__
#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/types.h>

#include <asm/setup.h>

/* this typedef defines how a Program Status Word looks like */
typedef struct 
{
        __u64   mask;
        __u64   addr;
} __attribute__ ((aligned(8))) psw_t;

#ifdef __KERNEL__
#define FIX_PSW(addr) ((unsigned long)(addr))
#define ADDR_BITS_REMOVE(addr) ((addr))
#endif

typedef union
{
	float   f;
	double  d;
	__u64   ui;
	struct
	{
		__u32 hi;
		__u32 lo;
	} fp;
} freg_t;

typedef struct
{
	__u32   fpc;
	freg_t  fprs[NUM_FPRS];              
} s390_fp_regs;

#define FPC_EXCEPTION_MASK      0xF8000000
#define FPC_FLAGS_MASK          0x00F80000
#define FPC_DXC_MASK            0x0000FF00
#define FPC_RM_MASK             0x00000003
#define FPC_VALID_MASK          0xF8F8FF03

/*
 * The first entries in pt_regs and user_regs_struct
 * are common for the two structures. The s390_regs structure
 * covers the common parts. It simplifies copying the common part
 * between the three structures.
 */
typedef struct
{
	psw_t psw;
	__u64 gprs[NUM_GPRS];
	__u32 acrs[NUM_ACRS];
	__u64 orig_gpr2;
} s390_regs;

/*
 * The pt_regs struct defines the way the registers are stored on
 * the stack during a system call.
 */
struct pt_regs 
{
	psw_t psw;
	__u64 gprs[NUM_GPRS];
	__u32 acrs[NUM_ACRS];
	__u64 orig_gpr2;
	__u32 trap;
} __attribute__ ((packed));

/*
 * Now for the program event recording (trace) definitions.
 */
typedef struct
{
	__u64 cr[3];
} per_cr_words;

#define PER_EM_MASK 0x00000000E8000000UL

typedef	struct
{
	unsigned                       : 32;
	unsigned em_branching          : 1;
	unsigned em_instruction_fetch  : 1;
	/*
	 * Switching on storage alteration automatically fixes
	 * the storage alteration event bit in the users std.
	 */
	unsigned em_storage_alteration : 1;
	unsigned em_gpr_alt_unused     : 1;
	unsigned em_store_real_address : 1;
	unsigned                       : 3;
	unsigned branch_addr_ctl       : 1;
	unsigned                       : 1;
	unsigned storage_alt_space_ctl : 1;
	unsigned                       : 21;
	addr_t   starting_addr;
	addr_t   ending_addr;
} per_cr_bits;

typedef struct
{
	__u16          perc_atmid;
	addr_t         address;
	__u8           access_id;
} per_lowcore_words;

typedef struct
{
	unsigned perc_branching          : 1; /* 0x096 */
	unsigned perc_instruction_fetch  : 1;
	unsigned perc_storage_alteration : 1;
	unsigned perc_gpr_alt_unused     : 1;
	unsigned perc_store_real_address : 1;
	unsigned                         : 3;
	unsigned atmid_psw_bit_31        : 1;
	unsigned atmid_validity_bit      : 1;
	unsigned atmid_psw_bit_32        : 1;
	unsigned atmid_psw_bit_5         : 1;
	unsigned atmid_psw_bit_16        : 1;
	unsigned atmid_psw_bit_17        : 1;
	unsigned si                      : 2;
	addr_t   address;                     /* 0x098 */
	unsigned                         : 4; /* 0x0a1 */
	unsigned access_id               : 4;
} per_lowcore_bits;

typedef struct
{
	union {
		per_cr_words   words;
		per_cr_bits    bits;
	} control_regs;
	/*
	 * Use these flags instead of setting em_instruction_fetch
	 * directly they are used so that single stepping can be
	 * switched on & off while not affecting other tracing
	 */
	unsigned  single_step       : 1;
	unsigned  instruction_fetch : 1;
	unsigned                    : 30;
	/*
	 * These addresses are copied into cr10 & cr11 if single
	 * stepping is switched off
	 */
	addr_t    starting_addr;
	addr_t    ending_addr;
	union {
		per_lowcore_words words;
		per_lowcore_bits  bits;
	} lowcore; 
} per_struct;

typedef struct
{
	__u32  len;
	addr_t kernel_addr;
	addr_t process_addr;
} ptrace_area;

/*
 * S/390 specific non posix ptrace requests. I chose unusual values so
 * they are unlikely to clash with future ptrace definitions.
 */
#define PTRACE_PEEKUSR_AREA           0x5000
#define PTRACE_POKEUSR_AREA           0x5001
#define PTRACE_PEEKTEXT_AREA	      0x5002
#define PTRACE_PEEKDATA_AREA	      0x5003
#define PTRACE_POKETEXT_AREA	      0x5004
#define PTRACE_POKEDATA_AREA 	      0x5005
/*
 * PT_PROT definition is loosely based on hppa bsd definition in
 * gdb/hppab-nat.c
 */
#define PTRACE_PROT                       21

typedef enum
{
	ptprot_set_access_watchpoint,
	ptprot_set_write_watchpoint,
	ptprot_disable_watchpoint
} ptprot_flags;

typedef struct
{
	addr_t           lowaddr;
	addr_t           hiaddr;
	ptprot_flags     prot;
} ptprot_area;                     

/* Sequence of bytes for breakpoint illegal instruction.  */
#define S390_BREAKPOINT     {0x0,0x1}
#define S390_BREAKPOINT_U16 ((__u16)0x0001)
#define S390_SYSCALL_OPCODE ((__u16)0x0a00)
#define S390_SYSCALL_SIZE   2

/*
 * The user_regs_struct defines the way the user registers are
 * store on the stack for signal handling.
 */
struct user_regs_struct
{
	psw_t psw;
	__u64 gprs[NUM_GPRS];
	__u32 acrs[NUM_ACRS];
	__u64 orig_gpr2;
	s390_fp_regs fp_regs;
	/*
	 * These per registers are in here so that gdb can modify them
	 * itself as there is no "official" ptrace interface for hardware
	 * watchpoints. This is the way intel does it.
	 */
	per_struct per_info;
	addr_t  ieee_instruction_pointer; 
	/* Used to give failing instruction back to user for ieee exceptions */
};

#ifdef __KERNEL__
#define user_mode(regs) (((regs)->psw.mask & PSW_PROBLEM_STATE) != 0)
#define instruction_pointer(regs) ((regs)->psw.addr)
extern void show_regs(struct pt_regs * regs);
#endif

#endif /* __ASSEMBLY__ */

#endif /* _S390X_PTRACE_H */
