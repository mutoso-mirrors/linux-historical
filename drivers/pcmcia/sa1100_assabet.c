/*
 * drivers/pcmcia/sa1100_assabet.c
 *
 * PCMCIA implementation routines for Assabet
 *
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/signal.h>
#include <asm/arch/assabet.h>

#include "sa1100_generic.h"

static struct irqs {
	int irq;
	const char *str;
} irqs[] = {
	{ ASSABET_IRQ_GPIO_CF_CD,   "CF_CD"   },
	{ ASSABET_IRQ_GPIO_CF_BVD2, "CF_BVD2" },
	{ ASSABET_IRQ_GPIO_CF_BVD1, "CF_BVD1" },
};

static int assabet_pcmcia_init(struct pcmcia_init *init)
{
	int i, res;

	/* Register interrupts */
	for (i = 0; i < ARRAY_SIZE(irqs); i++) {
		res = request_irq(irqs[i].irq, sa1100_pcmcia_interrupt,
				  SA_INTERRUPT, irqs[i].str, NULL);
		if (res)
			goto irq_err;
		set_irq_type(irqs[i].irq, IRQT_NOEDGE);
	}

	init->socket_irq[0] = NO_IRQ;
	init->socket_irq[1] = ASSABET_IRQ_GPIO_CF_IRQ;

	/* There's only one slot, but it's "Slot 1": */
	return 2;

 irq_err:
	printk(KERN_ERR "%s: request for IRQ%d failed (%d)\n",
		__FUNCTION__, irqs[i].irq, res);

	while (i--)
		free_irq(irqs[i].irq, NULL);

	return res;
}

/*
 * Release all resources.
 */
static int assabet_pcmcia_shutdown(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(irqs); i++)
		free_irq(irqs[i].irq, NULL);
  
	return 0;
}

static void
assabet_pcmcia_socket_state(int sock, struct pcmcia_state *state)
{
	unsigned long levels = GPLR;

	if (sock == 1) {
		state->detect = (levels & ASSABET_GPIO_CF_CD) ? 0 : 1;
		state->ready  = (levels & ASSABET_GPIO_CF_IRQ) ? 1 : 0;
		state->bvd1   = (levels & ASSABET_GPIO_CF_BVD1) ? 1 : 0;
		state->bvd2   = (levels & ASSABET_GPIO_CF_BVD2) ? 1 : 0;
		state->wrprot = 0; /* Not available on Assabet. */
		state->vs_3v  = 1; /* Can only apply 3.3V on Assabet. */
		state->vs_Xv  = 0;
	}
}

static int
assabet_pcmcia_configure_socket(int sock, const struct pcmcia_configure *configure)
{
	unsigned int mask;

	if (sock > 1)
		return -1;

	if (sock == 0)
		return 0;

	switch (configure->vcc) {
	case 0:
		mask = 0;
		break;

	case 50:
		printk(KERN_WARNING "%s(): CS asked for 5V, applying 3.3V...\n",
			__FUNCTION__);

	case 33:  /* Can only apply 3.3V to the CF slot. */
		mask = ASSABET_BCR_CF_PWR;
		break;

	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __FUNCTION__,
			configure->vcc);
		return -1;
	}

	/* Silently ignore Vpp, output enable, speaker enable. */

	if (configure->reset)
		mask |= ASSABET_BCR_CF_RST;

	ASSABET_BCR_frob(ASSABET_BCR_CF_RST | ASSABET_BCR_CF_PWR, mask);

	return 0;
}

/*
 * Enable card status IRQs on (re-)initialisation.  This can
 * be called at initialisation, power management event, or
 * pcmcia event.
 */
static int assabet_pcmcia_socket_init(int sock)
{
	int i;

	if (sock == 1) {
		/*
		 * Enable CF bus
		 */
		ASSABET_BCR_clear(ASSABET_BCR_CF_BUS_OFF);

		for (i = 0; i < ARRAY_SIZE(irqs); i++)
			set_irq_type(irqs[i].irq, IRQT_BOTHEDGE);
	}

	return 0;
}

/*
 * Disable card status IRQs on suspend.
 */
static int assabet_pcmcia_socket_suspend(int sock)
{
	int i;

	if (sock == 1) {
		for (i = 0; i < ARRAY_SIZE(irqs); i++)
			set_irq_type(irqs[i].irq, IRQT_NOEDGE);

		/*
		 * Tristate the CF bus signals.  Also assert CF
		 * reset as per user guide page 4-11.
		 */
		ASSABET_BCR_set(ASSABET_BCR_CF_BUS_OFF | ASSABET_BCR_CF_RST);
	}

	return 0;
}

static struct pcmcia_low_level assabet_pcmcia_ops = { 
	.owner			= THIS_MODULE,
	.init			= assabet_pcmcia_init,
	.shutdown		= assabet_pcmcia_shutdown,
	.socket_state		= assabet_pcmcia_socket_state,
	.configure_socket	= assabet_pcmcia_configure_socket,

	.socket_init		= assabet_pcmcia_socket_init,
	.socket_suspend		= assabet_pcmcia_socket_suspend,
};

int __init pcmcia_assabet_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_assabet()) {
		if (!machine_has_neponset())
			ret = sa1100_register_pcmcia(&assabet_pcmcia_ops, dev);
#ifndef CONFIG_ASSABET_NEPONSET
		else
			printk(KERN_ERR "Card Services disabled: missing "
				"Neponset support\n");
#endif
	}
	return ret;
}

void __exit pcmcia_assabet_exit(struct device *dev)
{
	sa1100_unregister_pcmcia(&assabet_pcmcia_ops, dev);
}

