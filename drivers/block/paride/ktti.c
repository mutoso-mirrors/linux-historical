/* 
        ktti.c        (c) 1998  Grant R. Guenther <grant@torque.net>
                          Under the terms of the GNU public license.

	ktti.c is a low-level protocol driver for the KT Technology
	parallel port adapter.  This adapter is used in the "PHd" 
        portable hard-drives.  As far as I can tell, this device
	supports 4-bit mode _only_.  

*/

#define KTTI_VERSION      "1.0"

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/io.h>

#include "paride.h"

#define j44(a,b)                (((a>>4)&0x0f)|(b&0xf0))

/* cont = 0 - access the IDE register file 
   cont = 1 - access the IDE command set 
*/

static int  cont_map[2] = { 0x10, 0x08 };

static void  ktti_write_regr( PIA *pi, int cont, int regr, int val)

{	int r;

	r = regr + cont_map[cont];

	w0(r); w2(0xb); w2(0xa); w2(3); w2(6); 
	w0(val); w2(3); w0(0); w2(6); w2(0xb);
}

static int ktti_read_regr( PIA *pi, int cont, int regr )

{	int  a, b, r;

        r = regr + cont_map[cont];

        w0(r); w2(0xb); w2(0xa); w2(9); w2(0xc); w2(9); 
	a = r1(); w2(0xc);  b = r1(); w2(9); w2(0xc); w2(9);
	return j44(a,b);

}

static void ktti_read_block( PIA *pi, char * buf, int count )

{	int  k, a, b;

	for (k=0;k<count/2;k++) {
		w0(0x10); w2(0xb); w2(0xa); w2(9); w2(0xc); w2(9);
		a = r1(); w2(0xc); b = r1(); w2(9);
		buf[2*k] = j44(a,b);
		a = r1(); w2(0xc); b = r1(); w2(9);
		buf[2*k+1] = j44(a,b);
	}
}

static void ktti_write_block( PIA *pi, char * buf, int count )

{	int k;

	for (k=0;k<count/2;k++) {
		w0(0x10); w2(0xb); w2(0xa); w2(3); w2(6);
		w0(buf[2*k]); w2(3);
		w0(buf[2*k+1]); w2(6);
		w2(0xb);
	}
}

static void ktti_connect ( PIA *pi  )

{       pi->saved_r0 = r0();
        pi->saved_r2 = r2();
	w2(0xb); w2(0xa); w0(0); w2(3); w2(6);	
}

static void ktti_disconnect ( PIA *pi )

{       w2(0xb); w2(0xa); w0(0xa0); w2(3); w2(4);
	w0(pi->saved_r0);
        w2(pi->saved_r2);
} 

static void ktti_log_adapter( PIA *pi, char * scratch, int verbose )

{       printk("%s: ktti %s, KT adapter at 0x%x, delay %d\n",
                pi->device,KTTI_VERSION,pi->port,pi->delay);

}

static void ktti_init_proto( PIA *pi)

{       MOD_INC_USE_COUNT;
}

static void ktti_release_proto( PIA *pi)

{       MOD_DEC_USE_COUNT;
}

struct pi_protocol ktti = {"ktti",0,1,2,1,1,
                           ktti_write_regr,
                           ktti_read_regr,
                           ktti_write_block,
                           ktti_read_block,
                           ktti_connect,
                           ktti_disconnect,
                           0,
                           0,
                           0,
                           ktti_log_adapter,
                           ktti_init_proto,
                           ktti_release_proto
                          };


#ifdef MODULE

int     init_module(void)

{       return pi_register( &ktti ) - 1;
}

void    cleanup_module(void)

{       pi_unregister( &ktti );
}

#endif

/* end of ktti.c */
