/*
 * linux/drivers/pcmcia/sa1100_badge4.c
 *
 * BadgePAD 4 PCMCIA specific routines
 *
 *   Christopher Hoover <ch@hpl.hp.com>
 *
 * Copyright (C) 2002 Hewlett-Packard Company
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/arch/badge4.h>
#include <asm/hardware/sa1111.h>

#include "sa1100_generic.h"
#include "sa1111_generic.h"

/*
 * BadgePAD 4 Details
 *
 * PCM Vcc:
 *
 *  PCM Vcc on BadgePAD 4 can be jumpered for 3v3 (short pins 1 and 3
 *  on JP6) or 5v0 (short pins 3 and 5 on JP6).
 *
 * PCM Vpp:
 *
 *  PCM Vpp on BadgePAD 4 can be jumpered for 12v0 (short pins 4 and 6
 *  on JP6) or tied to PCM Vcc (short pins 2 and 4 on JP6).  N.B.,
 *  12v0 operation requires that the power supply actually supply 12v0
 *  via pin 7 of JP7.
 *
 * CF Vcc:
 *
 *  CF Vcc on BadgePAD 4 can be jumpered either for 3v3 (short pins 1
 *  and 2 on JP10) or 5v0 (short pins 2 and 3 on JP10).
 *
 * Unfortunately there's no way programmatically to determine how a
 * given board is jumpered.  This code assumes a default jumpering
 * as described below.
 *
 * If the defaults aren't correct, you may override them with a pcmv
 * setup argument: pcmv=<pcm vcc>,<pcm vpp>,<cf vcc>.  The units are
 * tenths of volts; e.g. pcmv=33,120,50 indicates 3v3 PCM Vcc, 12v0
 * PCM Vpp, and 5v0 CF Vcc.
 *
 */

static int badge4_pcmvcc = 50;  /* pins 3 and 5 jumpered on JP6 */
static int badge4_pcmvpp = 50;  /* pins 2 and 4 jumpered on JP6 */
static int badge4_cfvcc = 33;   /* pins 1 and 2 jumpered on JP10 */

static int badge4_pcmcia_init(struct pcmcia_init *init)
{
	printk(KERN_INFO
	       "%s: badge4_pcmvcc=%d, badge4_pcmvpp=%d, badge4_cfvcc=%d\n",
	       __FUNCTION__,
	       badge4_pcmvcc, badge4_pcmvpp, badge4_cfvcc);

	return sa1111_pcmcia_init(init);
}

static int badge4_pcmcia_shutdown(void)
{
	int rc = sa1111_pcmcia_shutdown();

	/* be sure to disable 5v0 use */
	badge4_set_5V(BADGE4_5V_PCMCIA_SOCK0, 0);
	badge4_set_5V(BADGE4_5V_PCMCIA_SOCK1, 0);

	return rc;
}

static void complain_about_jumpering(const char *whom,
				     const char *supply,
				     int given, int wanted)
{
	printk(KERN_ERR
	 "%s: %s %d.%dV wanted but board is jumpered for %s %d.%dV operation"
	 "; re-jumper the board and/or use pcmv=xx,xx,xx\n",
	       whom, supply,
	       wanted / 10, wanted % 10,
	       supply,
	       given / 10, given % 10);
}

static int
badge4_pcmcia_configure_socket(int sock, const struct pcmcia_configure *conf)
{
	int ret;

	switch (sock) {
	case 0:
		if ((conf->vcc != 0) &&
		    (conf->vcc != badge4_pcmvcc)) {
			complain_about_jumpering(__FUNCTION__, "pcmvcc",
						 badge4_pcmvcc, conf->vcc);
			// Apply power regardless of the jumpering.
			// return -1;
		}
		if ((conf->vpp != 0) &&
		    (conf->vpp != badge4_pcmvpp)) {
			complain_about_jumpering(__FUNCTION__, "pcmvpp",
						 badge4_pcmvpp, conf->vpp);
			return -1;
		}
		break;

	case 1:
		if ((conf->vcc != 0) &&
		    (conf->vcc != badge4_cfvcc)) {
			complain_about_jumpering(__FUNCTION__, "cfvcc",
						 badge4_cfvcc, conf->vcc);
			return -1;
		}
		break;

	default:
		return -1;
	}

	ret = sa1111_pcmcia_configure_socket(sock, conf);
	if (ret == 0) {
		unsigned long flags;
		int need5V;

		local_irq_save(flags);

		need5V = ((conf->vcc == 50) || (conf->vpp == 50));

		badge4_set_5V(BADGE4_5V_PCMCIA_SOCK(conf->sock), need5V);

		local_irq_restore(flags);
	}

	return 0;
}

static struct pcmcia_low_level badge4_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.init			= badge4_pcmcia_init,
	.shutdown		= badge4_pcmcia_shutdown,
	.socket_state		= sa1111_pcmcia_socket_state,
	.configure_socket	= badge4_pcmcia_configure_socket,

	.socket_init		= sa1111_pcmcia_socket_init,
	.socket_suspend		= sa1111_pcmcia_socket_suspend,
};

int pcmcia_badge4_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_badge4())
		ret = sa1100_register_pcmcia(&badge4_pcmcia_ops, dev);

	return ret;
}

void __devexit pcmcia_badge4_exit(struct device *dev)
{
	sa1100_unregister_pcmcia(&badge4_pcmcia_ops, dev);
}

static int __init pcmv_setup(char *s)
{
	int v[4];

	s = get_options(s, ARRAY_SIZE(v), v);

	if (v[0] >= 1) badge4_pcmvcc = v[1];
	if (v[0] >= 2) badge4_pcmvpp = v[2];
	if (v[0] >= 3) badge4_cfvcc = v[3];

	return 1;
}

__setup("pcmv=", pcmv_setup);
