/* 
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2001 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/efi.h>
#include <asm/page.h>
#include <linux/threads.h>
#include <asm/sn/simulator.h>
#include <asm/sn/leds.h>

#include "llsc4.h"


#ifdef STANDALONE
#include "lock.h"
#endif

#ifdef INTTEST
static int	inttest=0;
#endif

#ifdef IA64_SEMFIX_INSN
#undef IA64_SEMFIX_INSN
#endif
#ifdef IA64_SEMFIX
#undef IA64_SEMFIX
#endif
# define IA64_SEMFIX_INSN
# define IA64_SEMFIX    ""

#define NOLOCK		0xdead
#define BGUARD(linei)	(0xbbbb0000 | (linei));
#define EGUARD(linei)	(0xeeee0000 | (linei));
#define GUARDLINE(v)	((v)&0xffff)

/*
 * Test parameter table for AUTOTEST
 */
typedef struct {
	int	passes;
	int	linecount;
	int	linepad;
} autotest_table_t;

autotest_table_t autotest_table[] = {
	{50000000,	2,	0x2b4		},
	{50000000,	16,	0,		},
	{50000000,	16,	4,		},
	{50000000,	128,	0x44		},
	{50000000,	128,	0x84		},
	{50000000,	128,	0x200		},
	{50000000,	128,	0x204		},
	{50000000,	128,	0x2b4		},
	{50000000,	2,	8*MB+0x2b4	},
	{50000000,	16,	8*MB+0		},
	{50000000,	16,	8*MB+4		},
	{50000000,	128,	8*MB+0x44	},
	{50000000,	128,	8*MB+0x84	},
	{50000000,	128,	8*MB+0x200	},
	{50000000,	128,	8*MB+0x204	},
	{50000000,	128,	8*MB+0x2b4	},
	{0}};

/*
 * Array of virtual addresses available for test purposes.
 */

typedef struct {
	long	vstart;
	long	vend;
	long	nextaddr;
	long	nextinit;
	int	wrapcount;
} memmap_t;

#define MAPCHUNKS		128
memmap_t 	memmap[MAPCHUNKS];
int		memmapx=0;

typedef struct {
	void	*addr;
	long	data[16];
	long	data_fc[16];
} capture_line_t;

typedef struct {
	int	size;
	void	*blockaddr;
	void	*shadaddr;
	long	blockdata[48];
	long	shaddata[48];
	long	blockdata_fc[48];
	long	shaddata_fc[48];
	long	synerr;
} capture_t;

/*
 * PORTING NOTE: revisit this statement. On hardware we put mbase at 0 and
 * the rest of the tables have to start at 1MB to skip PROM tables.
 */
#define THREADPRIVATE(t)	((threadprivate_t*)(((long)mbase)+1024*1024+t*((sizeof(threadprivate_t)+511)/512*512)))

#define k_capture		mbase->sk_capture
#define k_go			mbase->sk_go
#define k_linecount		mbase->sk_linecount
#define k_passes		mbase->sk_passes
#define k_napticks		mbase->sk_napticks
#define k_stop_on_error		mbase->sk_stop_on_error
#define k_verbose		mbase->sk_verbose
#define k_threadprivate		mbase->sk_threadprivate
#define k_blocks		mbase->sk_blocks
#define k_iter_msg		mbase->sk_iter_msg
#define k_vv			mbase->sk_vv
#define k_linepad		mbase->sk_linepad
#define k_options		mbase->sk_options
#define k_testnumber		mbase->sk_testnumber
#define k_currentpass		mbase->sk_currentpass

static long		blocks[MAX_LINECOUNT];		/* addresses of data blocks */
static control_t	*mbase;
static vint		initialized=0;

static unsigned int ran_conf_llsc(int);
static int  rerr(capture_t *, char *, void *, void *, int, int, int, int, int, int);
static void dumpline(void *, char *, char *, void *, void *, int);
static int  checkstop(int, int, uint);
static void spin(int);
static void capturedata(capture_t *, uint, void *, void *, int);
static int  randn(uint max, uint *seed);
static uint zrandom (uint *zranseed);
static int  set_lock(uint *, uint);
static int  clr_lock(uint *, uint);
static void Speedo(void);

int autotest_enabled=0;
static int llsctest_number=-1;
static int errstop_enabled=0;
static int fail_enabled=0;
static int l4_opt=0;
static int selective_trigger=0;
static int dump_block_addrs_opt=0;
static lock_t errlock=NOLOCK;
static private_t init_private[LLSC_MAXCPUS];

static int __init autotest_enable(char *str)
{
        autotest_enabled = 1;
	return 1;
}
static int __init set_llscblkadr(char *str)
{
	dump_block_addrs_opt = 1;
	return 1;
}
static int __init set_llscselt(char *str)
{
	selective_trigger = 1;
	return 1;
}
static int __init set_llsctest(char *str)
{
        llsctest_number = simple_strtol(str, &str, 10);
	if (llsctest_number < 0 || llsctest_number > 15)
		llsctest_number = -1;
	return 1;
}
static int __init set_llscerrstop(char *str)
{
        errstop_enabled = 1;
	return 1;
}
static int __init set_llscfail(char *str)
{
        fail_enabled = 8;
	return 1;
}
static int __init set_llscl4(char *str)
{
        l4_opt = 1;
	return 1;
}

static void print_params(void)
{
	printk ("********* Enter AUTOTEST facility on master cpu *************\n");
	printk ("  Test options:\n");
	printk ("     llsctest=<n>\t%d\tTest number to run (all = -1)\n", llsctest_number);
	printk ("     llscerrstop \t%s\tStop on error\n", errstop_enabled ? "on" : "off");
	printk ("     llscfail    \t%s\tForce a failure to test the trigger & error messages\n", fail_enabled ? "on" : "off");
	printk ("     llscselt    \t%s\tSelective triger on failures\n", selective_trigger ? "on" : "off");
	printk ("     llscblkadr  \t%s\tDump data block addresses\n", dump_block_addrs_opt ? "on" : "off");
	printk ("     llscl4      \t%s\tRun only tests that evict from L4\n", l4_opt ? "on" : "off");
	printk ("  SEMFIX: %s\n", IA64_SEMFIX);
	printk ("\n");
}
__setup("autotest", autotest_enable);
__setup("llsctest=", set_llsctest);
__setup("llscerrstop", set_llscerrstop);
__setup("llscfail", set_llscfail);
__setup("llscselt", set_llscselt);
__setup("llscblkadr", set_llscblkadr);
__setup("llscl4", set_llscl4);



static inline int
set_lock(uint *lock, uint id)
{
	uint	old;
	old = cmpxchg_acq(lock, NOLOCK, id);
	return (old == NOLOCK);
}

static inline int
clr_lock(uint *lock, uint id)
{
	uint	old;
	old = cmpxchg_rel(lock, id, NOLOCK);
	return (old == id);
}

static inline void
init_lock(uint *lock)
{
	*lock = NOLOCK;
}

/*------------------------------------------------------------------------+
| Routine  :  ran_conf_llsc - ll/sc shared data test                      |
| Description: This test checks the coherency of shared data              |
+------------------------------------------------------------------------*/
static unsigned int
ran_conf_llsc(int thread)
{
	private_t	pval;
	share_t		sval, sval2;
	uint		vv, linei, slinei, sharei, pass;
	long		t;
	lock_t		lockpat;
	share_t		*sharecopy;
	long		verbose, napticks, passes, linecount, lcount;
	dataline_t	*linep, *slinep;
	int		s, seed;
	threadprivate_t	*tp;
	uint		iter_msg, iter_msg_i=0;
	int		vv_mask;
	int		correct_errors;
	int		errs=0;
	int		stillbad;
	capture_t	capdata;
	private_t	*privp;
	share_t		*sharep;


	linecount = k_linecount;
	napticks = k_napticks;
	verbose = k_verbose;
	passes = k_passes;
	iter_msg = k_iter_msg;
	seed = (thread + 1) * 647;
	tp = THREADPRIVATE(thread);
	vv_mask = (k_vv>>((thread%16)*4)) & 0xf;
	correct_errors = k_options&0xff;

	memset (&capdata, 0, sizeof(capdata));
	for (linei=0; linei<linecount; linei++)
		tp->private[linei] = thread;	

	for (pass = 1; passes == 0 || pass < passes; pass++) {
		lockpat = (pass & 0x0fffffff) + (thread <<28);
		if (lockpat == NOLOCK)
			continue;
		tp->threadpasses = pass;
		if (checkstop(thread, pass, lockpat))
			return 0;
		iter_msg_i++;
		if (iter_msg && iter_msg_i > iter_msg) {
			printk("Thread %d, Pass %d\n", thread, pass);
			iter_msg_i = 0;
		}
		lcount = 0;

		/*
		 * Select line to perform operations on.
		 */
		linei = randn(linecount, &seed);
		sharei = randn(2, &seed);
		slinei = (linei + (linecount/2))%linecount;		/* I dont like this - fix later */

		linep = (dataline_t *)blocks[linei];
		slinep = (dataline_t *)blocks[slinei];
		if (sharei == 0)
			sharecopy = &slinep->share0;
		else
			sharecopy = &slinep->share1;


		vv = randn(4, &seed);
		if ((vv_mask & (1<<vv)) == 0)
			continue;

		if (napticks) {
			t = randn(napticks, &seed);
			udelay(t);
		}
		privp = &linep->private[thread];
		sharep = &linep->share[sharei];
		
		switch(vv) {
		case 0:
			/* Read and verify private count on line. */
			pval = *privp;
			if (verbose)
				printk("Line:%3d, Thread:%d:%d. Val: %x\n", linei, thread, vv, tp->private[linei]);
			if (pval != tp->private[linei]) {
				capturedata(&capdata, pass, privp, NULL, sizeof(*privp));
				stillbad = (*privp != tp->private[linei]);
				if (rerr(&capdata, "Private count", linep, slinep, thread, pass, linei, tp->private[linei], pval, stillbad)) {
					return 1;
				}
				if (correct_errors) {
					tp->private[linei] = *privp;
				}
				errs++;
			}
			break;

		case 1:
			/* Read, verify, and increment private count on line. */
			pval = *privp;
			if (verbose)
				printk("Line:%3d, Thread:%d:%d. Val: %x\n", linei, thread, vv, tp->private[linei]);
			if (pval != tp->private[linei]) {
				capturedata(&capdata, pass, privp, NULL, sizeof(*privp));
				stillbad = (*privp != tp->private[linei]);
				if (rerr(&capdata, "Private count & inc", linep, slinep, thread, pass, linei, tp->private[linei], pval, stillbad)) {
					return 1;
				}
				errs++;
			}
			pval = (pval==255) ? 0 : pval+1;
			*privp = pval;
			tp->private[linei] = pval;
			break;

		case 2:
			/* Lock line, read and verify shared data. */
			if (verbose)
				printk("Line:%3d, Thread:%d:%d. Val: %x\n", linei, thread, vv, *sharecopy);
			lcount = 0;
			while (LOCK(sharei) != 1) {
				if (checkstop(thread, pass, lockpat))
					return 0;
				if (lcount++>1000000) {
					capturedata(&capdata, pass, LOCKADDR(sharei), NULL, sizeof(lock_t));
					stillbad = (GETLOCK(sharei) != 0);
					rerr(&capdata, "Shared data lock", linep, slinep, thread, pass, linei, 0, GETLOCK(sharei), stillbad);
					return 1;
				}
				if ((lcount&0x3fff) == 0)
					udelay(1000);
			}

			sval = *sharep;
			sval2 = *sharecopy;
			if (pass > 12 && thread == 0 && fail_enabled == 1)
				sval++;
			if (sval != sval2) {
				capturedata(&capdata, pass, sharep, sharecopy, sizeof(*sharecopy));
				stillbad = (*sharep != *sharecopy);
				if (!stillbad && *sharep != sval && *sharecopy == sval2)
					stillbad = 2;
				if (rerr(&capdata, "Shared data", linep, slinep, thread, pass, linei, sval2, sval, stillbad)) {
					return 1;
				}
				if (correct_errors)
					*sharep = *sharecopy;
				errs++;
			}


			if ( (s=UNLOCK(sharei)) != 1) {
				capturedata(&capdata, pass, LOCKADDR(sharei), NULL, 4);
				stillbad = (GETLOCK(sharei) != lockpat);
				if (rerr(&capdata, "Shared data unlock", linep, slinep, thread, pass, linei, lockpat, GETLOCK(sharei), stillbad))
					return 1;
				if (correct_errors)
					ZEROLOCK(sharei);	
				errs++;
			}
			break;

		case 3:
			/* Lock line, read and verify shared data, modify shared data. */
			if (verbose)
				printk("Line:%3d, Thread:%d:%d. Val: %x\n", linei, thread, vv, *sharecopy);
			lcount = 0;
			while (LOCK(sharei) != 1) {
				if (checkstop(thread, pass, lockpat))
					return 0;
				if (lcount++>1000000) {
					capturedata(&capdata, pass, LOCKADDR(sharei), NULL, sizeof(lock_t));
					stillbad = (GETLOCK(sharei) != 0);
					rerr(&capdata, "Shared data lock & inc", linep, slinep, thread, pass, linei, 0, GETLOCK(sharei), stillbad);
					return 1;
				}
				if ((lcount&0x3fff) == 0)
					udelay(1000);
			}
			sval = *sharep;
			sval2 = *sharecopy;
			if (sval != sval2) {
				capturedata(&capdata, pass, sharep, sharecopy, sizeof(*sharecopy));
				stillbad = (*sharep != *sharecopy);
				if (!stillbad && *sharep != sval && *sharecopy == sval2)
					stillbad = 2;
				if (rerr(&capdata, "Shared data & inc", linep, slinep, thread, pass, linei, sval2, sval, stillbad)) {
					return 1;
				}
				errs++;
			}

			*sharep = lockpat;
			*sharecopy = lockpat;


			if ( (s=UNLOCK(sharei)) != 1) {
				capturedata(&capdata, pass, LOCKADDR(sharei), NULL, 4);
				stillbad = (GETLOCK(sharei) != lockpat);
				if (rerr(&capdata, "Shared data & inc unlock", linep, slinep, thread, pass, linei, thread, GETLOCK(sharei), stillbad))
					return 1;
				if (correct_errors)
					ZEROLOCK(sharei);	
				errs++;
			}
			break;
		}
	}

	return (errs > 0);
}

static void
trigger_la(long val)
{
	long	*p;

	p = (long*)0xc0000a0001000020L; /* PI_CPU_NUM */
	*p = val;
}

static long
getsynerr(void)
{
	long	err, *errp;

	errp = (long*)0xc0000e0000000340L;	/* SYN_ERR */
	err = *errp;
	if (err)
		*errp = -1L;
	return (err & ~0x60);
}

static int
rerr(capture_t *cap, char *msg, void *lp, void *slp, int thread, int pass, int badlinei, int exp, int found, int stillbad)
{
	int		cpu, i, linei;
	long 		synerr;
	int		selt;


	selt = selective_trigger && stillbad > 1 && 
			memcmp(cap->blockdata, cap->blockdata_fc, 128) != 0 &&
			memcmp(cap->shaddata, cap->shaddata_fc, 128) == 0;
	if (selt) {
		trigger_la(pass);
	} else if (selective_trigger) {
		k_go = ST_STOP;
		return k_stop_on_error;;
	}

	spin(1);
	i = 100;
	while (i && set_lock(&errlock, 1) != 1) {
		spin(1);
		i--;
	}
	printk ("\nDataError!: %-20s, test %ld, thread %d, line:%d, pass %d (0x%x), time %ld expected:%x, found:%x\n",
	    msg, k_testnumber, thread, badlinei, pass, pass, jiffies, exp, found);

	dumpline (lp, "Corrupted data", "D ", cap->blockaddr, cap->blockdata, cap->size);
#ifdef ZZZ
	if (memcmp(cap->blockdata, cap->blockdata_fc, 128))
		dumpline (lp, "Corrupted data", "DF", cap->blockaddr, cap->blockdata_fc, cap->size);
#endif

	if (cap->shadaddr) {
		dumpline (slp, "Shadow    data", "S ", cap->shadaddr, cap->shaddata, cap->size);
#ifdef ZZZ
		if (memcmp(cap->shaddata, cap->shaddata_fc, 128))
			dumpline (slp, "Shadow    data", "SF", cap->shadaddr, cap->shaddata_fc, cap->size);
#endif
	}
	
	printk("Threadpasses: ");
	for (cpu=0,i=0; cpu<LLSC_MAXCPUS; cpu++)
		if (k_threadprivate[cpu]->threadpasses) {
			if (i && (i%8) == 0)
				printk("\n            : ");
			printk("  %d:0x%x", cpu, k_threadprivate[cpu]->threadpasses);
			i++;
		}
	printk("\n");
	
	for (linei=0; linei<k_linecount; linei++) {
		int		slinei, g1linei, g2linei, g1err, g2err, sh0err, sh1err;
		dataline_t	*linep, *slinep;

		slinei = (linei + (k_linecount/2))%k_linecount;
		linep = (dataline_t *)blocks[linei];
		slinep = (dataline_t *)blocks[slinei];

		g1linei = GUARDLINE(linep->guard1);
		g2linei = GUARDLINE(linep->guard2);
		g1err = (g1linei != linei);
		g2err = (g2linei != linei);
		sh0err = (linep->share[0] != slinep->share0);
		sh1err = (linep->share[1] != slinep->share1);

		if (g1err || g2err || sh0err || sh1err) {
			printk("Line 0x%lx (%03d), %sG1 0x%lx (%03d), %sG2 0x%lx (%03d), %sSH0 %08x (%08x), %sSH1 %08x (%08x)\n",
				blocks[linei], linei, 
				g1err ? "*" : " ", blocks[g1linei], g1linei,
				g2err ? "*" : " ", blocks[g2linei], g2linei,
				sh0err ? "*" : " ", linep->share[0], slinep->share0,
				sh1err ? "*" : " ", linep->share[1], slinep->share1);


		}
	}

	printk("\nData was %sfixed by flushcache\n", (stillbad == 1 ? "**** NOT **** " : " "));
	synerr = getsynerr();
	if (synerr)
		printk("SYNERR: Thread %d, Synerr: 0x%lx\n", thread, synerr);
	spin(2);
	printk("\n\n");
	clr_lock(&errlock, 1);

	if (errstop_enabled) {
		local_irq_disable();
		while(1);
	}
	return k_stop_on_error;
}


static void
dumpline(void *lp, char *str1, char *str2, void *addr, void *data, int size)
{
	long *p;
	int i, off;

	printk("%s at 0x%lx, size %d, block starts at 0x%lx\n", str1, (long)addr, size, (long)lp);
	p = (long*) data;
	for (i=0; i<48; i++, p++) {
		if (i%8 == 0) printk("%2s", i==16 ? str2 : "  ");
		printk(" %016lx", *p);
		if ((i&7)==7) printk("\n");
	}
	printk("   ");
	off = (((long)addr) ^ size) & 63L;
	for (i=0; i<off+size; i++) {
		printk("%s", (i>=off) ? "--" : "  ");
		if ((i%8) == 7)
			printk(" ");
	}

	off = ((long)addr) & 127;
	printk(" (line %d)\n", 2+off/64+1);
}


static int
randn(uint max, uint *seedp)
{
	if (max == 1)
		return(0);
	else
		return((int)(zrandom(seedp)>>10) % max);
}


static int
checkstop(int thread, int pass, uint lockpat)
{
	long	synerr;

	if (k_go == ST_RUN)
		return 0;
	if (k_go == ST_STOP)
		return 1;

	if (errstop_enabled) {
		local_irq_disable();
		while(1);
	}
	synerr = getsynerr();
	spin(2);
	if (k_go == ST_STOP)
		return 1;
	if (synerr)
		printk("SYNERR: Thread %d, Synerr: 0x%lx\n", thread, synerr);
	return 1;
}


static void
spin(int j)
{
	udelay(j * 500000);
}

static void
capturedata(capture_t *cap, uint pass, void *blockaddr, void *shadaddr, int size)
{

	if (!selective_trigger)
		trigger_la (pass);

	memcpy (cap->blockdata, CACHEALIGN(blockaddr)-128, 3*128);
	if (shadaddr) 
		memcpy (cap->shaddata, CACHEALIGN(shadaddr)-128, 3*128);

	if (k_stop_on_error) {
		k_go = ST_ERRSTOP;
	}

 	cap->size = size;
	cap->blockaddr = blockaddr;
	cap->shadaddr = shadaddr;

	asm volatile ("fc %0" :: "r"(blockaddr) : "memory");
	ia64_sync_i();
	ia64_srlz_d();
	memcpy (cap->blockdata_fc, CACHEALIGN(blockaddr)-128, 3*128);

	if (shadaddr) {
		asm volatile ("fc %0" :: "r"(shadaddr) : "memory");
		ia64_sync_i();
		ia64_srlz_d();
		memcpy (cap->shaddata_fc, CACHEALIGN(shadaddr)-128, 3*128);
	}
}

int             zranmult = 0x48c27395;

static uint  
zrandom (uint *seedp)
{
        *seedp = (*seedp * zranmult) & 0x7fffffff;
        return (*seedp);
}


void
set_autotest_params(void)
{
	static int	testnumber=-1;

	if (llsctest_number >= 0) {
		testnumber = llsctest_number;
	} else {
		testnumber++;
		if (autotest_table[testnumber].passes == 0) {
			testnumber = 0;
			dump_block_addrs_opt = 0;
		}
	}
	if (testnumber == 0 && l4_opt) testnumber = 9;

	k_passes = autotest_table[testnumber].passes;
	k_linepad = autotest_table[testnumber].linepad;
	k_linecount = autotest_table[testnumber].linecount;
	k_testnumber = testnumber;

	if (IS_RUNNING_ON_SIMULATOR()) {
		printk ("llsc start test %ld\n", k_testnumber);
		k_passes = 1000;
	}
}


static void
set_leds(int errs)
{
	unsigned char	leds=0;

	/*
	 * Leds are:
	 * 	ppppeee-  
	 *   where
	 *      pppp = test number
	 *       eee = error count but top bit is stick
	 */

	leds =  ((errs&7)<<1) | ((k_testnumber&15)<<4) | (errs ? 0x08 : 0);
	set_led_bits(leds, LED_MASK_AUTOTEST);
}

static void
setup_block_addresses(void)
{
	int			i, stride, memmapi;
	dataline_t		*dp;
	long			*ip, *ipe;


	stride = k_linepad + sizeof(dataline_t);
	memmapi = 0;
	for (i=0; i<memmapx; i++) {
		memmap[i].nextaddr = memmap[i].vstart;
		memmap[i].nextinit = memmap[i].vstart;
		memmap[i].wrapcount = 0;
	}

	for (i=0; i<k_linecount; i++) {
		blocks[i] = memmap[memmapi].nextaddr;
		dp = (dataline_t*)blocks[i];
		memmap[memmapi].nextaddr += (stride & 0xffff);
		if (memmap[memmapi].nextaddr + sizeof(dataline_t) >= memmap[memmapi].vend) {
			memmap[memmapi].wrapcount++;
			memmap[memmapi].nextaddr = memmap[memmapi].vstart + 
					memmap[memmapi].wrapcount * sizeof(dataline_t);
		}

		ip = (long*)((memmap[memmapi].nextinit+7)&~7);
		ipe = (long*)(memmap[memmapi].nextaddr+2*sizeof(dataline_t)+8);
		while(ip <= ipe && ip < ((long*)memmap[memmapi].vend-8))
			*ip++ = (long)ip;
		memmap[memmapi].nextinit = (long) ipe;
		dp->guard1 = BGUARD(i);
		dp->guard2 = EGUARD(i);
		dp->lock[0] = dp->lock[1] = NOLOCK;
		dp->share[0] = dp->share0 = 0x1111;
		dp->share[1] = dp->share1 = 0x2222;
		memcpy(dp->private, init_private, LLSC_MAXCPUS*sizeof(private_t));


		if (stride > 16384) {
			memmapi++;
			if (memmapi == memmapx)
				memmapi = 0;
		}
	}

}

static void
dump_block_addrs(void)
{
	int	i;

	printk("LLSC TestNumber %ld\n", k_testnumber);

	for (i=0; i<k_linecount; i++) {
		printk("  %lx", blocks[i]);
		if (i%4 == 3)
			printk("\n");
	}
	printk("\n");
}


static void
set_thread_state(int cpuid, int state)
{
	if (k_threadprivate[cpuid]->threadstate == TS_KILLED) {
		set_led_bits(LED_MASK_AUTOTEST, LED_MASK_AUTOTEST);
		while(1);
	}
	k_threadprivate[cpuid]->threadstate = state;
}

static int
build_mem_map(unsigned long start, unsigned long end, void *arg)
{
	long	lstart;
	long	align = 8*MB;

	/*
	 * HACK - skip the kernel on the first node 
	 */

	printk ("LLSC memmap: start 0x%lx, end 0x%lx, (0x%lx - 0x%lx)\n", 
		start, end, (long) virt_to_page(start), (long) virt_to_page(end-PAGE_SIZE));

	if (memmapx >= MAPCHUNKS)
		return 0;
	while (end > start && (PageReserved(virt_to_page(end-PAGE_SIZE)) || virt_to_page(end-PAGE_SIZE)->count.counter > 0))
		end -= PAGE_SIZE;

	lstart = end;
	while (lstart > start && (!PageReserved(virt_to_page(lstart-PAGE_SIZE)) && virt_to_page(lstart-PAGE_SIZE)->count.counter == 0))
		lstart -= PAGE_SIZE;

	lstart = (lstart + align -1) /align * align;
	end = end / align * align;
	if (lstart >= end)
		return 0;
	printk ("     memmap: start 0x%lx, end 0x%lx\n", lstart, end);

	memmap[memmapx].vstart = lstart;
	memmap[memmapx].vend = end;
	memmapx++;
	return 0;
}

void int_test(void);

int
llsc_main (int cpuid, long mbasex)
{
	int		i, cpu, is_master, repeatcnt=0;
	unsigned int	preverr=0, errs=0, pass=0;
	int		automode=0;

#ifdef INTTEST
	if (inttest)
		int_test();
#endif

	if (!autotest_enabled)
		return 0;

#ifdef CONFIG_SMP
	is_master = !smp_processor_id();
#else
	is_master = 1;
#endif


	if (is_master) {
		print_params();
		if(!IS_RUNNING_ON_SIMULATOR())
			spin(10);
		mbase = (control_t*)mbasex;
		k_currentpass = 0;
		k_go = ST_IDLE;
		k_passes = DEF_PASSES;
		k_napticks = DEF_NAPTICKS;
		k_stop_on_error = DEF_STOP_ON_ERROR;
		k_verbose = DEF_VERBOSE;
		k_linecount = DEF_LINECOUNT;
		k_iter_msg = DEF_ITER_MSG;
		k_vv = DEF_VV;
		k_linepad = DEF_LINEPAD;
		k_blocks = (void*)blocks;
		efi_memmap_walk(build_mem_map, 0);
	
#ifdef CONFIG_IA64_SGI_AUTOTEST
		automode = 1;
#endif

		for (i=0; i<LLSC_MAXCPUS; i++) {
			k_threadprivate[i] = THREADPRIVATE(i);
			memset(k_threadprivate[i], 0, sizeof(*k_threadprivate[i]));
			init_private[i] = i;
		}
		initialized = 1;
	} else {
		while (initialized == 0)
			udelay(100);
	}

loop:
	if (is_master) {
		if (automode) {
			if (!preverr || repeatcnt++ > 5) {
				set_autotest_params();
				repeatcnt = 0;
			}
		} else {
			while (k_go == ST_IDLE);
		}

		k_go = ST_INIT;
		if (k_linecount > MAX_LINECOUNT) k_linecount = MAX_LINECOUNT;
		k_linecount = k_linecount & ~1;
		setup_block_addresses();
		if (!preverr && dump_block_addrs_opt)
			dump_block_addrs();

		k_currentpass = pass++;
		k_go = ST_RUN;
		if (fail_enabled)
			fail_enabled--;

	} else {
		while (k_go != ST_RUN || k_currentpass != pass);
		pass++;
	}


	set_leds(errs);
	set_thread_state(cpuid, TS_RUNNING);

	errs += ran_conf_llsc(cpuid);
	preverr = (k_go == ST_ERRSTOP);

	set_leds(errs);
	set_thread_state(cpuid, TS_STOPPED);

	if (is_master) {
		Speedo();
		for (i=0, cpu=0; cpu<LLSC_MAXCPUS; cpu++) {
			while (k_threadprivate[cpu]->threadstate == TS_RUNNING) {
				i++;
				if (i == 10000) { 
					k_go = ST_STOP;
					printk ("  llsc master stopping test number %ld\n", k_testnumber);
				}
				if (i > 100000) {
					k_threadprivate[cpu]->threadstate = TS_KILLED;
					printk ("  llsc: master killing cpuid %d, running test number %ld\n", 
							cpu, k_testnumber);
				}
				udelay(1000);
			}
		}
	}

	goto loop;
}


static void
Speedo(void)
{
	static int i = 0;

	switch (++i%4) {
	case 0:
		printk("|\b");
		break;
	case 1:
		printk("\\\b");
		break;
	case 2:
		printk("-\b");
		break;
	case 3:
		printk("/\b");
		break;
	}
}

#ifdef INTTEST

/* ======================================================================================================== 
 *
 * Some test code to verify that interrupts work
 *
 * Add the following to the arch/ia64/kernel/smp.c after the comment "Reschedule callback"
 * 		if (zzzprint_resched) printk("  cpu %d got interrupt\n", smp_processor_id());
 *
 * Enable the code in arch/ia64/sn/sn1/smp.c to print sending IPIs.
 *
 */

static int __init set_inttest(char *str)
{
        inttest = 1;
	autotest_enabled = 1;

	return 1;
}	

__setup("inttest=", set_inttest);

int	zzzprint_resched=0;

void
int_test() {
	int			mycpu, cpu;
	static volatile int	control_cpu=0;

	mycpu = smp_processor_id();
	zzzprint_resched = 2;

	printk("Testing cross interrupts\n");
	
	while (control_cpu != NR_CPUS) {
		if (mycpu == control_cpu) {
			for (cpu=0; cpu<NR_CPUS; cpu++) {
				if (!cpu_online(cpu)) continue;
				printk("Sending interrupt from %d to %d\n", mycpu, cpu);
				udelay(IS_RUNNING_ON_SIMULATOR ? 10000 : 400000);
				smp_send_reschedule(cpu);
				udelay(IS_RUNNING_ON_SIMULATOR ? 10000 : 400000);
				smp_send_reschedule(cpu);
				udelay(IS_RUNNING_ON_SIMULATOR ? 10000 : 400000);
			}
			control_cpu++;
		}
	}

	zzzprint_resched = 1;

	if (mycpu == NR_CPUS-1) {
		printk("\nTight loop of cpu %d sending ints to cpu 0 (every 100 us)\n", mycpu);
		udelay(IS_RUNNING_ON_SIMULATOR ? 1000 : 1000000);
		local_irq_disable();
		while (1) {
			smp_send_reschedule(0);
			udelay(100);
		}

	}

	while(1);
}
#endif
