/* 
        aten.c        (c) 1997  Grant R. Guenther <grant@torque.net>
                          Under the terms of the GNU public license.

	aten.c is a low-level protocol driver for the ATEN EH-100
	parallel port adapter.  The EH-100 supports 4-bit and 8-bit
        modes only.  There is also an EH-132 which supports EPP mode
        transfers.  The EH-132 is not yet supported.

*/

#define ATEN_VERSION      "1.0"

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/io.h>

#include "paride.h"

#define j44(a,b)                ((((a>>4)&0x0f)|(b&0xf0))^0x88)

/* cont = 0 - access the IDE register file 
   cont = 1 - access the IDE command set 
*/

static int  cont_map[2] = { 0x08, 0x20 };

static void  aten_write_regr( PIA *pi, int cont, int regr, int val)

{	int r;

	r = regr + cont_map[cont] + 0x80;

	w0(r); w2(0xe); w2(6); w0(val); w2(7); w2(6); w2(0xc);
}

static int aten_read_regr( PIA *pi, int cont, int regr )

{	int  a, b, r;

        r = regr + cont_map[cont] + 0x40;

	switch (pi->mode) {

        case 0: w0(r); w2(0xe); w2(6); 
		w2(7); w2(6); w2(0);
		a = r1(); w0(0x10); b = r1(); w2(0xc);
		return j44(a,b);

        case 1: r |= 0x10;
		w0(r); w2(0xe); w2(6); w0(0xff); 
		w2(0x27); w2(0x26); w2(0x20);
		a = r0();
		w2(0x26); w2(0xc);
		return a;
	}
	return -1;
}

static void aten_read_block( PIA *pi, char * buf, int count )

{	int  k, a, b, c, d;

	switch (pi->mode) {

	case 0:	w0(0x48); w2(0xe); w2(6);
		for (k=0;k<count/2;k++) {
			w2(7); w2(6); w2(2);
			a = r1(); w0(0x58); b = r1();
			w2(0); d = r1(); w0(0x48); c = r1();
			buf[2*k] = j44(c,d);
			buf[2*k+1] = j44(a,b);
		}
		w2(0xc);
		break;

	case 1: w0(0x58); w2(0xe); w2(6);
		for (k=0;k<count/2;k++) {
			w2(0x27); w2(0x26); w2(0x22);
			a = r0(); w2(0x20); b = r0();
			buf[2*k] = b; buf[2*k+1] = a;
		}
		w2(0x26); w2(0xc);
		break;
	}
}

static void aten_write_block( PIA *pi, char * buf, int count )

{	int k;

	w0(0x88); w2(0xe); w2(6);
	for (k=0;k<count/2;k++) {
		w0(buf[2*k+1]); w2(0xe); w2(6);
		w0(buf[2*k]); w2(7); w2(6);
	}
	w2(0xc);
}

static void aten_connect ( PIA *pi  )

{       pi->saved_r0 = r0();
        pi->saved_r2 = r2();
	w2(0xc);	
}

static void aten_disconnect ( PIA *pi )

{       w0(pi->saved_r0);
        w2(pi->saved_r2);
} 

static void aten_log_adapter( PIA *pi, char * scratch, int verbose )

{       char    *mode_string[2] = {"4-bit","8-bit"};

        printk("%s: aten %s, ATEN EH-100 at 0x%x, ",
                pi->device,ATEN_VERSION,pi->port);
        printk("mode %d (%s), delay %d\n",pi->mode,
		mode_string[pi->mode],pi->delay);

}

static void aten_inc_use ( void )

{       MOD_INC_USE_COUNT;
}

static void aten_dec_use ( void )

{       MOD_DEC_USE_COUNT;
}

struct pi_protocol aten = {"aten",0,2,2,1,1,
                           aten_write_regr,
                           aten_read_regr,
                           aten_write_block,
                           aten_read_block,
                           aten_connect,
                           aten_disconnect,
                           0,
                           0,
                           0,
                           aten_log_adapter,
                           aten_inc_use, 
                           aten_dec_use 
                          };


#ifdef MODULE

int     init_module(void)

{       return pi_register( &aten ) - 1;
}

void    cleanup_module(void)

{       pi_unregister( &aten );
}

#endif

/* end of aten.c */
