#ifndef _INCLUDE_GUARD_i82092aa_H_
#define _INCLUDE_GUARD_i82092aa_H_

#include <linux/interrupt.h>

/* $Id: i82092aa.h,v 1.1.1.1 2001/09/19 14:53:15 dwmw2 Exp $ */

/* Debuging defines */
#ifdef NOTRACE
#define enter(x)   printk("Enter: %s, %s line %i\n",x,__FILE__,__LINE__)
#define leave(x)   printk("Leave: %s, %s line %i\n",x,__FILE__,__LINE__)
#define dprintk(fmt, args...) printk(fmt , ## args)
#else
#define enter(x)   do {} while (0)
#define leave(x)   do {} while (0)
#define dprintk(fmt, args...) do {} while (0)
#endif



/* prototypes */

static int  i82092aa_pci_probe(struct pci_dev *dev, const struct pci_device_id *id);
static void i82092aa_pci_remove(struct pci_dev *dev);
static int card_present(int socketno);
static irqreturn_t i82092aa_interrupt(int irq, void *dev, struct pt_regs *regs);




static int i82092aa_get_status(struct pcmcia_socket *socket, u_int *value);
static int i82092aa_get_socket(struct pcmcia_socket *socket, socket_state_t *state);
static int i82092aa_set_socket(struct pcmcia_socket *socket, socket_state_t *state);
static int i82092aa_set_io_map(struct pcmcia_socket *socket, struct pccard_io_map *io);
static int i82092aa_set_mem_map(struct pcmcia_socket *socket, struct pccard_mem_map *mem);
static int i82092aa_init(struct pcmcia_socket *socket);
static int i82092aa_suspend(struct pcmcia_socket *socket);
static int i82092aa_register_callback(struct pcmcia_socket *socket, void (*handler)(void *, unsigned int), void * info);
static int i82092aa_inquire_socket(struct pcmcia_socket *socket, socket_cap_t *cap);
static void i82092aa_proc_setup(struct pcmcia_socket *socket, struct proc_dir_entry *base);

#endif

