/*
 * Device driver for the via-cuda on Apple Powermacs.
 *
 * The VIA (versatile interface adapter) interfaces to the CUDA,
 * a 6805 microprocessor core which controls the ADB (Apple Desktop
 * Bus) which connects to the keyboard and mouse.  The CUDA also
 * controls system power and the RTC (real time clock) chip.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <stdarg.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <asm/prom.h>
#include <asm/adb.h>
#include <asm/cuda.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/init.h>

static volatile unsigned char *via;

/* VIA registers - spaced 0x200 bytes apart */
#define RS		0x200		/* skip between registers */
#define B		0		/* B-side data */
#define A		RS		/* A-side data */
#define DIRB		(2*RS)		/* B-side direction (1=output) */
#define DIRA		(3*RS)		/* A-side direction (1=output) */
#define T1CL		(4*RS)		/* Timer 1 ctr/latch (low 8 bits) */
#define T1CH		(5*RS)		/* Timer 1 counter (high 8 bits) */
#define T1LL		(6*RS)		/* Timer 1 latch (low 8 bits) */
#define T1LH		(7*RS)		/* Timer 1 latch (high 8 bits) */
#define T2CL		(8*RS)		/* Timer 2 ctr/latch (low 8 bits) */
#define T2CH		(9*RS)		/* Timer 2 counter (high 8 bits) */
#define SR		(10*RS)		/* Shift register */
#define ACR		(11*RS)		/* Auxiliary control register */
#define PCR		(12*RS)		/* Peripheral control register */
#define IFR		(13*RS)		/* Interrupt flag register */
#define IER		(14*RS)		/* Interrupt enable register */
#define ANH		(15*RS)		/* A-side data, no handshake */

/* Bits in B data register: all active low */
#define TREQ		0x08		/* Transfer request (input) */
#define TACK		0x10		/* Transfer acknowledge (output) */
#define TIP		0x20		/* Transfer in progress (output) */

/* Bits in ACR */
#define SR_CTRL		0x1c		/* Shift register control bits */
#define SR_EXT		0x0c		/* Shift on external clock */
#define SR_OUT		0x10		/* Shift out if 1 */

/* Bits in IFR and IER */
#define IER_SET		0x80		/* set bits in IER */
#define IER_CLR		0		/* clear bits in IER */
#define SR_INT		0x04		/* Shift register full/empty */

static enum cuda_state {
    idle,
    sent_first_byte,
    sending,
    reading,
    read_done,
    awaiting_reply
} cuda_state;

static struct adb_request *current_req;
static struct adb_request *last_req;
static unsigned char cuda_rbuf[16];
static unsigned char *reply_ptr;
static int reading_reply;
static int data_index;
static struct device_node *vias;

static int init_via(void);
static void cuda_start(void);
static void via_interrupt(int irq, void *arg, struct pt_regs *regs);
static void cuda_input(unsigned char *buf, int nb, struct pt_regs *regs);
static int cuda_adb_send_request(struct adb_request *req, int sync);
static int cuda_adb_autopoll(int on);

__openfirmware

void
find_via_cuda()
{
    vias = find_devices("via-cuda");
    if (vias == 0)
	return;
    if (vias->next != 0)
	printk(KERN_WARNING "Warning: only using 1st via-cuda\n");

#if 0
    { int i;

    printk("via_cuda_init: node = %p, addrs =", vias->node);
    for (i = 0; i < vias->n_addrs; ++i)
	printk(" %x(%x)", vias->addrs[i].address, vias->addrs[i].size);
    printk(", intrs =");
    for (i = 0; i < vias->n_intrs; ++i)
	printk(" %x", vias->intrs[i].line);
    printk("\n"); }
#endif

    if (vias->n_addrs != 1 || vias->n_intrs != 1) {
	printk(KERN_ERR "via-cuda: expecting 1 address (%d) and 1 interrupt (%d)\n",
	       vias->n_addrs, vias->n_intrs);
	if (vias->n_addrs < 1 || vias->n_intrs < 1)
	    return;
    }
    via = (volatile unsigned char *) ioremap(vias->addrs->address, 0x2000);

    cuda_state = idle;

    if (!init_via()) {
	printk(KERN_ERR "init_via failed\n");
	via = NULL;
    }

    adb_hardware = ADB_VIACUDA;
}

void
via_cuda_init(void)
{
    if (via == NULL)
	return;

    if (request_irq(vias->intrs[0].line, via_interrupt, 0, "VIA", (void *)0)) {
	printk(KERN_ERR "VIA: can't get irq %d\n", vias->intrs[0].line);
	return;
    }

    /* Clear and enable interrupts */
    via[IFR] = 0x7f; eieio();	/* clear interrupts by writing 1s */
    via[IER] = IER_SET|SR_INT; eieio();	/* enable interrupt from SR */

    /* Set function pointers */
    adb_send_request = cuda_adb_send_request;
    adb_autopoll = cuda_adb_autopoll;
}

#define WAIT_FOR(cond, what)				\
    do {						\
	for (x = 1000; !(cond); --x) {			\
	    if (x == 0) {				\
		printk("Timeout waiting for " what);	\
		return 0;				\
	    }						\
	    udelay(100);					\
	}						\
    } while (0)

static int
init_via()
{
    int x;

    via[DIRB] = (via[DIRB] | TACK | TIP) & ~TREQ;	/* TACK & TIP out */
    via[B] |= TACK | TIP;				/* negate them */
    via[ACR] = (via[ACR] & ~SR_CTRL) | SR_EXT;		/* SR data in */
    eieio();
    x = via[SR]; eieio();	/* clear any left-over data */
    via[IER] = 0x7f; eieio();	/* disable interrupts from VIA */
    eieio();

    /* delay 4ms and then clear any pending interrupt */
    udelay(4000);
    x = via[SR]; eieio();

    /* sync with the CUDA - assert TACK without TIP */
    via[B] &= ~TACK; eieio();

    /* wait for the CUDA to assert TREQ in response */
    WAIT_FOR((via[B] & TREQ) == 0, "CUDA response to sync");

    /* wait for the interrupt and then clear it */
    WAIT_FOR(via[IFR] & SR_INT, "CUDA response to sync (2)");
    x = via[SR]; eieio();

    /* finish the sync by negating TACK */
    via[B] |= TACK; eieio();

    /* wait for the CUDA to negate TREQ and the corresponding interrupt */
    WAIT_FOR(via[B] & TREQ, "CUDA response to sync (3)");
    WAIT_FOR(via[IFR] & SR_INT, "CUDA response to sync (4)");
    x = via[SR]; eieio();
    via[B] |= TIP; eieio();	/* should be unnecessary */

    return 1;
}

/* Send an ADB command */
static int
cuda_adb_send_request(struct adb_request *req, int sync)
{
    int i;

    for (i = req->nbytes; i > 0; --i)
	req->data[i] = req->data[i-1];
    req->data[0] = ADB_PACKET;
    ++req->nbytes;
    req->reply_expected = 1;
    i = cuda_send_request(req);
    if (i)
	return i;
    if (sync) {
	while (!req->complete)
	    cuda_poll();
    }
    return 0;
}

/* Enable/disable autopolling */
static int
cuda_adb_autopoll(int on)
{
    struct adb_request req;

    cuda_request(&req, NULL, 3, CUDA_PACKET, CUDA_AUTOPOLL, on);
    while (!req.complete)
	cuda_poll();
    return 0;
}

/* Construct and send a cuda request */
int
cuda_request(struct adb_request *req, void (*done)(struct adb_request *),
	     int nbytes, ...)
{
    va_list list;
    int i;

    req->nbytes = nbytes;
    req->done = done;
    va_start(list, nbytes);
    for (i = 0; i < nbytes; ++i)
	req->data[i] = va_arg(list, int);
    va_end(list);
    req->reply_expected = 1;
    return cuda_send_request(req);
}

int
cuda_send_request(struct adb_request *req)
{
    unsigned long flags;

    if (via == NULL) {
	req->complete = 1;
	return -ENXIO;
    }
    if (req->nbytes < 2 || req->data[0] > CUDA_PACKET) {
	req->complete = 1;
	return -EINVAL;
    }
    req->next = 0;
    req->sent = 0;
    req->complete = 0;
    req->reply_len = 0;
    save_flags(flags); cli();

    if (current_req != 0) {
	last_req->next = req;
	last_req = req;
    } else {
	current_req = req;
	last_req = req;
	if (cuda_state == idle)
	    cuda_start();
    }

    restore_flags(flags);
    return 0;
}

static void
cuda_start()
{
    unsigned long flags;
    struct adb_request *req;

    /* assert cuda_state == idle */
    /* get the packet to send */
    req = current_req;
    if (req == 0)
	return;
    save_flags(flags); cli();
    if ((via[B] & TREQ) == 0) {
	restore_flags(flags);
	return;			/* a byte is coming in from the CUDA */
    }

    /* set the shift register to shift out and send a byte */
    via[ACR] |= SR_OUT; eieio();
    via[SR] = req->data[0]; eieio();
    via[B] &= ~TIP;
    cuda_state = sent_first_byte;
    restore_flags(flags);
}

void
cuda_poll()
{
    int ie;

    ie = _disable_interrupts();
    if (via[IFR] & SR_INT)
	via_interrupt(0, 0, 0);
    _enable_interrupts(ie);
}

static void
via_interrupt(int irq, void *arg, struct pt_regs *regs)
{
    int x, status;
    struct adb_request *req;

    if ((via[IFR] & SR_INT) == 0)
	return;

    status = (~via[B] & (TIP|TREQ)) | (via[ACR] & SR_OUT); eieio();
    /* printk("via_interrupt: state=%d status=%x\n", cuda_state, status); */
    switch (cuda_state) {
    case idle:
	/* CUDA has sent us the first byte of data - unsolicited */
	if (status != TREQ)
	    printk("cuda: state=idle, status=%x\n", status);
	x = via[SR]; eieio();
	via[B] &= ~TIP; eieio();
	cuda_state = reading;
	reply_ptr = cuda_rbuf;
	reading_reply = 0;
	break;

    case awaiting_reply:
	/* CUDA has sent us the first byte of data of a reply */
	if (status != TREQ)
	    printk("cuda: state=awaiting_reply, status=%x\n", status);
	x = via[SR]; eieio();
	via[B] &= ~TIP; eieio();
	cuda_state = reading;
	reply_ptr = current_req->reply;
	reading_reply = 1;
	break;

    case sent_first_byte:
	if (status == TREQ + TIP + SR_OUT) {
	    /* collision */
	    via[ACR] &= ~SR_OUT; eieio();
	    x = via[SR]; eieio();
	    via[B] |= TIP | TACK; eieio();
	    cuda_state = idle;
	} else {
	    /* assert status == TIP + SR_OUT */
	    if (status != TIP + SR_OUT)
		printk("cuda: state=sent_first_byte status=%x\n", status);
	    via[SR] = current_req->data[1]; eieio();
	    via[B] ^= TACK; eieio();
	    data_index = 2;
	    cuda_state = sending;
	}
	break;

    case sending:
	req = current_req;
	if (data_index >= req->nbytes) {
	    via[ACR] &= ~SR_OUT; eieio();
	    x = via[SR]; eieio();
	    via[B] |= TACK | TIP; eieio();
	    req->sent = 1;
	    if (req->reply_expected) {
		cuda_state = awaiting_reply;
	    } else {
		current_req = req->next;
		if (req->done)
		    (*req->done)(req);
		/* not sure about this */
		cuda_state = idle;
		cuda_start();
	    }
	} else {
	    via[SR] = req->data[data_index++]; eieio();
	    via[B] ^= TACK; eieio();
	}
	break;

    case reading:
	*reply_ptr++ = via[SR]; eieio();
	if (status == TIP) {
	    /* that's all folks */
	    via[B] |= TACK | TIP; eieio();
	    cuda_state = read_done;
	} else {
	    /* assert status == TIP | TREQ */
	    if (status != TIP + TREQ)
		printk("cuda: state=reading status=%x\n", status);
	    via[B] ^= TACK; eieio();
	}
	break;

    case read_done:
	x = via[SR]; eieio();
	if (reading_reply) {
	    req = current_req;
	    req->reply_len = reply_ptr - req->reply;
	    if (req->data[0] == ADB_PACKET) {
		/* Have to adjust the reply from ADB commands */
		if (req->reply_len <= 2 || (req->reply[1] & 2) != 0) {
		    /* the 0x2 bit indicates no response */
		    req->reply_len = 0;
		} else {
		    /* leave just the command and result bytes in the reply */
		    req->reply_len -= 2;
		    memmove(req->reply, req->reply + 2, req->reply_len);
		}
	    }
	    req->complete = 1;
	    current_req = req->next;
	    if (req->done)
		(*req->done)(req);
	} else {
	    cuda_input(cuda_rbuf, reply_ptr - cuda_rbuf, regs);
	}
	if (status == TREQ) {
	    via[B] &= ~TIP; eieio();
	    cuda_state = reading;
	    reply_ptr = cuda_rbuf;
	    reading_reply = 0;
	} else {
	    cuda_state = idle;
	    cuda_start();
	}
	break;

    default:
	printk("via_interrupt: unknown cuda_state %d?\n", cuda_state);
    }
}

static void
cuda_input(unsigned char *buf, int nb, struct pt_regs *regs)
{
    int i;

    switch (buf[0]) {
    case ADB_PACKET:
	adb_input(buf+2, nb-2, regs, buf[1] & 0x40);
	break;

    default:
	printk("data from cuda (%d bytes):", nb);
	for (i = 0; i < nb; ++i)
	    printk(" %.2x", buf[i]);
	printk("\n");
    }
}
