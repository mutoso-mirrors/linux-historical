/*
 * Copyright (C) 2004 PathScale, Inc
 * Licensed under the GPL
 */

#include <errno.h>
#include <string.h>
#include "sysdep/ptrace_user.h"
#include "sysdep/ptrace.h"
#include "uml-config.h"
#include "skas_ptregs.h"
#include "registers.h"
#include "user.h"

/* These are set once at boot time and not changed thereafter */

static unsigned long exec_regs[HOST_FRAME_SIZE];
static unsigned long exec_fp_regs[HOST_FP_SIZE];
static unsigned long exec_fpx_regs[HOST_XFP_SIZE];
static int have_fpx_regs = 1;

void init_thread_registers(union uml_pt_regs *to)
{
	memcpy(to->skas.regs, exec_regs, sizeof(to->skas.regs));
	memcpy(to->skas.fp, exec_fp_regs, sizeof(to->skas.fp));
	if(have_fpx_regs)
		memcpy(to->skas.xfp, exec_fpx_regs, sizeof(to->skas.xfp));
}

/* XXX These need to use [GS]ETFPXREGS and copy_sc_{to,from}_user_skas needs
 * to pass in a sufficiently large buffer
 */
int save_fp_registers(int pid, unsigned long *fp_regs)
{
	if(ptrace(PTRACE_GETFPREGS, pid, 0, fp_regs) < 0)
		return(-errno);
	return(0);
}

int restore_fp_registers(int pid, unsigned long *fp_regs)
{
	if(ptrace(PTRACE_SETFPREGS, pid, 0, fp_regs) < 0)
		return(-errno);
	return(0);
}

static int move_registers(int pid, int int_op, union uml_pt_regs *regs,
			  int fp_op, unsigned long *fp_regs)
{
	if(ptrace(int_op, pid, 0, regs->skas.regs) < 0)
		return(-errno);

	if(ptrace(fp_op, pid, 0, fp_regs) < 0)
		return(-errno);

	return(0);
}

void save_registers(int pid, union uml_pt_regs *regs)
{
	unsigned long *fp_regs;
	int err, fp_op;

	if(have_fpx_regs){
		fp_op = PTRACE_GETFPXREGS;
		fp_regs = regs->skas.xfp;
	}
	else {
		fp_op = PTRACE_GETFPREGS;
		fp_regs = regs->skas.fp;
	}

	err = move_registers(pid, PTRACE_GETREGS, regs, fp_op, fp_regs);
	if(err)
		panic("save_registers - saving registers failed, errno = %d\n",
		      -err);
}

void restore_registers(int pid, union uml_pt_regs *regs)
{
	unsigned long *fp_regs;
	int err, fp_op;

	if(have_fpx_regs){
		fp_op = PTRACE_SETFPXREGS;
		fp_regs = regs->skas.xfp;
	}
	else {
		fp_op = PTRACE_SETFPREGS;
		fp_regs = regs->skas.fp;
	}

	err = move_registers(pid, PTRACE_SETREGS, regs, fp_op, fp_regs);
	if(err)
		panic("restore_registers - saving registers failed, "
		      "errno = %d\n", -err);
}

void init_registers(int pid)
{
	int err;

	err = ptrace(PTRACE_GETREGS, pid, 0, exec_regs);
	if(err)
		panic("check_ptrace : PTRACE_GETREGS failed, errno = %d",
		      err);

	err = ptrace(PTRACE_GETFPXREGS, pid, 0, exec_fpx_regs);
	if(!err)
		return;

	have_fpx_regs = 0;
	if(err != EIO)
		panic("check_ptrace : PTRACE_GETFPXREGS failed, errno = %d",
		      err);

	err = ptrace(PTRACE_GETFPREGS, pid, 0, exec_fp_regs);
	if(err)
		panic("check_ptrace : PTRACE_GETFPREGS failed, errno = %d",
		      err);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
