/*
 * arch/ppc/mm/extable.c
 *
 * from arch/i386/mm/extable.c
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/uaccess.h>

extern struct exception_table_entry __start___ex_table[];
extern struct exception_table_entry __stop___ex_table[];

/*
 * The exception table needs to be sorted because we use the macros
 * which put things into the exception table in a variety of segments
 * such as the prep, pmac, chrp, etc. segments as well as the init
 * segment and the main kernel text segment.
 */
static inline void
sort_ex_table(struct exception_table_entry *start,
	      struct exception_table_entry *finish)
{
	struct exception_table_entry el, *p, *q;

	/* insertion sort */
	for (p = start + 1; p < finish; ++p) {
		/* start .. p-1 is sorted */
		if (p[0].insn < p[-1].insn) {
			/* move element p down to its right place */
			el = *p;
			q = p;
			do {
				/* el comes before q[-1], move q[-1] up one */
				q[0] = q[-1];
				--q;
			} while (q > start && el.insn < q[-1].insn);
			*q = el;
		}
	}
}

void
sort_exception_table(void)
{
	sort_ex_table(__start___ex_table, __stop___ex_table);
}

static inline unsigned long
search_one_table(const struct exception_table_entry *first,
		 const struct exception_table_entry *last,
		 unsigned long value)
{
        while (first <= last) {
		const struct exception_table_entry *mid;
		long diff;

		mid = (last - first) / 2 + first;
		diff = mid->insn - value;
                if (diff == 0)
                        return mid->fixup;
                else if (diff < 0)
                        first = mid+1;
                else
                        last = mid-1;
        }
        return 0;
}

unsigned long
search_exception_table(unsigned long addr)
{
	unsigned long ret = 0;

#ifndef CONFIG_MODULES
	/* There is only the kernel to search.  */
	ret = search_one_table(__start___ex_table, __stop___ex_table-1, addr);
#else
	unsigned long flags;
	struct list_head *i;

	/* The kernel is the last "module" -- no need to treat it special. */
	spin_lock_irqsave(&modlist_lock, flags);
	list_for_each(i, &extables) {
		struct exception_table *ex
			= list_entry(i, struct exception_table, list);
		if (ex->num_entries == 0)
			continue;
		ret = search_one_table(ex->entry,
				       ex->entry + ex->num_entries - 1, addr);
		if (ret)
			break;
	}
	spin_unlock_irqrestore(&modlist_lock, flags);
#endif

	return ret;
}
