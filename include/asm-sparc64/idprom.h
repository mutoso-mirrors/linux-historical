/* $Id: idprom.h,v 1.1 1996/11/20 15:33:43 davem Exp $
 * idprom.h: Macros and defines for idprom routines
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_IDPROM_H
#define _SPARC64_IDPROM_H

/* Offset into the EEPROM where the id PROM is located on the 4c */
#define IDPROM_OFFSET  0x7d8

/* On sun4m; physical. */
/* MicroSPARC(-II) does not decode 31rd bit, but it works. */
#define IDPROM_OFFSET_M  0xfd8

struct idprom
{
	unsigned char	id_format;	/* Format identifier (always 0x01) */
	unsigned char	id_machtype;	/* Machine type */
	unsigned char	id_ethaddr[6];	/* Hardware ethernet address */
	int		id_date;	/* Date of manufacture */
	unsigned int	id_sernum:24;	/* Unique serial number */
	unsigned char	id_cksum;	/* Checksum - xor of the data bytes */
	unsigned char	reserved[16];
};

extern struct idprom *idprom;
extern void idprom_init(void);

#define IDPROM_SIZE  (sizeof(struct idprom))

#endif /* !(_SPARC_IDPROM_H) */
