/*
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/string.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/adb.h>
#include <asm/prom.h>

static volatile unsigned char *sccc, *sccd;
unsigned long TXRDY, RXRDY;
extern void xmon_printf(const char *fmt, ...);

static int console = 0;

void buf_access(void)
{
	if ( _machine == _MACH_chrp )
		sccd[3] &= ~0x80;	/* reset DLAB */
}

void
xmon_map_scc(void)
{
	volatile unsigned char *base;

	if ( _machine == _MACH_Pmac )
	{
		struct device_node *np;
#ifdef CHRP_ESCC
		unsigned long addr = 0xc1013020;
#else
		unsigned long addr = 0xf3013020;
#endif
		TXRDY = 4;
		RXRDY = 1;
		
		np = find_devices("mac-io");
		if (np && np->n_addrs) {
			addr = np->addrs[0].address + 0x13000;
			/* use the B channel on the iMac, A channel on others */
			if (addr >= 0xf0000000)
				addr += 0x20; /* use A channel */
		}
		base = (volatile unsigned char *) ioremap(addr & PAGE_MASK, PAGE_SIZE);
		sccc = base + (addr & ~PAGE_MASK);
#ifdef CHRP_ESCC
		sccd = sccc + (0xc1013030 - 0xc1013020);
#else
		sccd = sccc + (0xf3013030 - 0xf3013020);
#endif
	}
	else
	{
		/* should already be mapped by the kernel boot */
		sccc = (volatile unsigned char *) (isa_io_base + 0x3fd);
		sccd = (volatile unsigned char *) (isa_io_base + 0x3f8);
		TXRDY = 0x20;
		RXRDY = 1;
	}
}

static int scc_initialized = 0;

void xmon_init_scc(void);
extern void pmu_poll(void);

int
xmon_write(void *handle, void *ptr, int nb)
{
    char *p = ptr;
    int i, ct;

    if (!scc_initialized)
	xmon_init_scc();
    for (i = 0; i < nb; ++i) {
	while ((*sccc & TXRDY) == 0)
	    if (adb_hardware == ADB_VIAPMU)
		pmu_poll();
	buf_access();
	if ( console && (*p != '\r'))
		printk("%c", *p);
	ct = 0;
	if ( *p == '\n')
		ct = 1;
	*sccd = *p++;
	if ( ct )
		xmon_write(handle, "\r", 1);
    }
    return i;
}

int
xmon_read(void *handle, void *ptr, int nb)
{
    char *p = ptr;
    int i;

    if (!scc_initialized)
	xmon_init_scc();
    for (i = 0; i < nb; ++i) {
	while ((*sccc & RXRDY) == 0)
	    if (adb_hardware == ADB_VIAPMU)
		pmu_poll();
	buf_access();
#if 0	
	if ( 0/*console*/ )
		*p++ = ppc_md.kbd_getkeycode();
	else
#endif		
		*p++ = *sccd;
    }
    return i;
}

static unsigned char scc_inittab[] = {
    13, 0,		/* set baud rate divisor */
    12, 1,
    14, 1,		/* baud rate gen enable, src=rtxc */
    11, 0x50,		/* clocks = br gen */
    5,  0x6a,		/* tx 8 bits, assert RTS */
    4,  0x44,		/* x16 clock, 1 stop */
    3,  0xc1,		/* rx enable, 8 bits */
};

void
xmon_init_scc()
{
	if ( _machine == _MACH_chrp )
	{
		sccd[3] = 0x83; eieio();	/* LCR = 8N1 + DLAB */
		sccd[0] = 3; eieio();		/* DLL = 38400 baud */
		sccd[1] = 0; eieio();
		sccd[2] = 0; eieio();		/* FCR = 0 */
		sccd[3] = 3; eieio();		/* LCR = 8N1 */
		sccd[1] = 0; eieio();		/* IER = 0 */
	}
	else
	{
		int i, x;

		for (i = 20000; i != 0; --i) {
			x = *sccc; eieio();
		}
		*sccc = 9; eieio();		/* reset A or B side */
		*sccc = ((unsigned long)sccc & 0x20)? 0x80: 0x40; eieio();
		for (i = 0; i < sizeof(scc_inittab); ++i) {
			*sccc = scc_inittab[i];
			eieio();
		}
	}
	scc_initialized = 1;
}

#if 0
extern int (*prom_entry)(void *);

int
xmon_exit(void)
{
    struct prom_args {
	char *service;
    } args;

    for (;;) {
	args.service = "exit";
	(*prom_entry)(&args);
    }
}
#endif

void *xmon_stdin;
void *xmon_stdout;
void *xmon_stderr;

void
xmon_init(void)
{
}

int
xmon_putc(int c, void *f)
{
    char ch = c;

    if (c == '\n')
	xmon_putc('\r', f);
    return xmon_write(f, &ch, 1) == 1? c: -1;
}

int
xmon_putchar(int c)
{
    return xmon_putc(c, xmon_stdout);
}

int
xmon_fputs(char *str, void *f)
{
    int n = strlen(str);

    return xmon_write(f, str, n) == n? 0: -1;
}

int
xmon_readchar(void)
{
    char ch;

    for (;;) {
	switch (xmon_read(xmon_stdin, &ch, 1)) {
	case 1:
	    return ch;
	case -1:
	    xmon_printf("read(stdin) returned -1\r\n", 0, 0);
	    return -1;
	}
    }
}

static char line[256];
static char *lineptr;
static int lineleft;

int
xmon_getchar(void)
{
    int c;

    if (lineleft == 0) {
	lineptr = line;
	for (;;) {
	    c = xmon_readchar();
	    if (c == -1 || c == 4)
		break;
	    if (c == '\r' || c == '\n') {
		*lineptr++ = '\n';
		xmon_putchar('\n');
		break;
	    }
	    switch (c) {
	    case 0177:
	    case '\b':
		if (lineptr > line) {
		    xmon_putchar('\b');
		    xmon_putchar(' ');
		    xmon_putchar('\b');
		    --lineptr;
		}
		break;
	    case 'U' & 0x1F:
		while (lineptr > line) {
		    xmon_putchar('\b');
		    xmon_putchar(' ');
		    xmon_putchar('\b');
		    --lineptr;
		}
		break;
	    default:
		if (lineptr >= &line[sizeof(line) - 1])
		    xmon_putchar('\a');
		else {
		    xmon_putchar(c);
		    *lineptr++ = c;
		}
	    }
	}
	lineleft = lineptr - line;
	lineptr = line;
    }
    if (lineleft == 0)
	return -1;
    --lineleft;
    return *lineptr++;
}

char *
xmon_fgets(char *str, int nb, void *f)
{
    char *p;
    int c;

    for (p = str; p < str + nb - 1; ) {
	c = xmon_getchar();
	if (c == -1) {
	    if (p == str)
		return 0;
	    break;
	}
	*p++ = c;
	if (c == '\n')
	    break;
    }
    *p = 0;
    return str;
}
