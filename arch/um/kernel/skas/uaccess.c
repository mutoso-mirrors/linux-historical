/*
 * Copyright (C) 2002 - 2003 Jeff Dike (jdike@addtoit.com)
 * Licensed under the GPL
 */

#include "linux/stddef.h"
#include "linux/kernel.h"
#include "linux/string.h"
#include "linux/fs.h"
#include "linux/highmem.h"
#include "asm/page.h"
#include "asm/pgtable.h"
#include "asm/uaccess.h"
#include "kern_util.h"

extern void *um_virt_to_phys(struct task_struct *task, unsigned long addr,
			     pte_t *pte_out);

static unsigned long maybe_map(unsigned long virt, int is_write)
{
	pte_t pte;
	int err;

	void *phys = um_virt_to_phys(current, virt, &pte);
	int dummy_code;

	if(IS_ERR(phys) || (is_write && !pte_write(pte))){
		err = handle_page_fault(virt, 0, is_write, 0, &dummy_code);
		if(err)
			return(0);
		phys = um_virt_to_phys(current, virt, NULL);
	}
	return((unsigned long) phys);
}

static int do_op(unsigned long addr, int len, int is_write,
		 int (*op)(unsigned long addr, int len, void *arg), void *arg)
{
	struct page *page;
	int n;

	addr = maybe_map(addr, is_write);
	if(addr == -1)
		return(-1);

	page = phys_to_page(addr);
	addr = (unsigned long) kmap(page) + (addr & ~PAGE_MASK);
	n = (*op)(addr, len, arg);
	kunmap(page);

	return(n);
}

static int buffer_op(unsigned long addr, int len, int is_write,
		     int (*op)(unsigned long addr, int len, void *arg),
		     void *arg)
{
	int size = min(PAGE_ALIGN(addr) - addr, (unsigned long) len);
	int remain = len, n;

	n = do_op(addr, size, is_write, op, arg);
	if(n != 0)
		return(n < 0 ? remain : 0);

	addr += size;
	remain -= size;
	if(remain == 0)
		return(0);

	while(addr < ((addr + remain) & PAGE_MASK)){
		n = do_op(addr, PAGE_SIZE, is_write, op, arg);
		if(n != 0)
			return(n < 0 ? remain : 0);

		addr += PAGE_SIZE;
		remain -= PAGE_SIZE;
	}
	if(remain == 0)
		return(0);

	n = do_op(addr, remain, is_write, op, arg);
	if(n != 0)
		return(n < 0 ? remain : 0);
	return(0);
}

static int copy_chunk_from_user(unsigned long from, int len, void *arg)
{
	unsigned long *to_ptr = arg, to = *to_ptr;

	memcpy((void *) to, (void *) from, len);
	*to_ptr += len;
	return(0);
}

int copy_from_user_skas(void *to, const void *from, int n)
{
	if(segment_eq(get_fs(), KERNEL_DS)){
		memcpy(to, from, n);
		return(0);
	}

	return(access_ok_skas(VERIFY_READ, from, n) ?
	       buffer_op((unsigned long) from, n, 0, copy_chunk_from_user, &to):
	       n);
}

static int copy_chunk_to_user(unsigned long to, int len, void *arg)
{
	unsigned long *from_ptr = arg, from = *from_ptr;

	memcpy((void *) to, (void *) from, len);
	*from_ptr += len;
	return(0);
}

int copy_to_user_skas(void *to, const void *from, int n)
{
	if(segment_eq(get_fs(), KERNEL_DS)){
		memcpy(to, from, n);
		return(0);
	}

	return(access_ok_skas(VERIFY_WRITE, to, n) ?
	       buffer_op((unsigned long) to, n, 1, copy_chunk_to_user, &from) :
	       n);
}

static int strncpy_chunk_from_user(unsigned long from, int len, void *arg)
{
	char **to_ptr = arg, *to = *to_ptr;
	int n;

	strncpy(to, (void *) from, len);
	n = strnlen(to, len);
	*to_ptr += n;

	if(n < len)
	        return(1);
	return(0);
}

int strncpy_from_user_skas(char *dst, const char *src, int count)
{
	int n;
	char *ptr = dst;

	if(segment_eq(get_fs(), KERNEL_DS)){
		strncpy(dst, src, count);
		return(strnlen(dst, count));
	}

	if(!access_ok_skas(VERIFY_READ, src, 1))
		return(-EFAULT);

	n = buffer_op((unsigned long) src, count, 0, strncpy_chunk_from_user,
		      &ptr);
	if(n != 0)
		return(-EFAULT);
	return(strnlen(dst, count));
}

static int clear_chunk(unsigned long addr, int len, void *unused)
{
	memset((void *) addr, 0, len);
	return(0);
}

int __clear_user_skas(void *mem, int len)
{
	return(buffer_op((unsigned long) mem, len, 1, clear_chunk, NULL));
}

int clear_user_skas(void *mem, int len)
{
	if(segment_eq(get_fs(), KERNEL_DS)){
		memset(mem, 0, len);
		return(0);
	}

	return(access_ok_skas(VERIFY_WRITE, mem, len) ?
	       buffer_op((unsigned long) mem, len, 1, clear_chunk, NULL) : len);
}

static int strnlen_chunk(unsigned long str, int len, void *arg)
{
	int *len_ptr = arg, n;

	n = strnlen((void *) str, len);
	*len_ptr += n;

	if(n < len)
		return(1);
	return(0);
}

int strnlen_user_skas(const void *str, int len)
{
	int count = 0, n;

	if(segment_eq(get_fs(), KERNEL_DS))
		return(strnlen(str, len) + 1);

	n = buffer_op((unsigned long) str, len, 0, strnlen_chunk, &count);
	if(n == 0)
		return(count + 1);
	return(-EFAULT);
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
