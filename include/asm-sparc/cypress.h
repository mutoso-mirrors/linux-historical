/* cypress.h: Cypress module specific definitions and defines.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_CYPRESS_H
#define _SPARC_CYPRESS_H

/* Cypress chips have %psr 'impl' of '0001' and 'vers' of '0001'. */

/* The MMU control register fields on the Sparc Cypress 604/605 MMU's.
 *
 * ---------------------------------------------------------------
 * |implvers| MCA | MCM |MV| MID |BM| C|RSV|MR|CM|CL|CE|RSV|NF|ME|
 * ---------------------------------------------------------------
 *  31    24 23-22 21-20 19 18-15 14 13  12 11 10  9  8 7-2  1  0
 *
 * MCA: MultiChip Access -- Used for configuration of multiple
 *      CY7C604/605 cache units.
 * MCM: MultiChip Mask -- Again, for multiple cache unit config.
 * MV: MultiChip Valid -- Indicates MCM and MCA have valid settings.
 * MID: ModuleID -- Unique processor ID for MBus transactions. (605 only)
 * BM: Boot Mode -- 0 = not in boot mode, 1 = in boot mode
 * C: Cacheable -- Indicates whether accesses are cacheable while
 *    the MMU is off.  0=no 1=yes
 * MR: MemoryReflection -- Indicates whether the bus attacted to the
 *     MBus supports memory reflection. 0=no 1=yes (605 only)
 * CM: CacheMode -- Indicates whether the cache is operating in write
 *     through or copy-back mode. 0=write-through 1=copy-back
 * CL: CacheLock -- Indicates if the entire cache is locked or not.
 *     0=not-locked 1=locked  (604 only)
 * CE: CacheEnable -- Is the virtual cache on? 0=no 1=yes
 * NF: NoFault -- Do faults generate traps? 0=yes 1=no
 * ME: MmuEnable -- Is the MMU doing translations? 0=no 1=yes
 */

/* NEEDS TO BE FIXED */
#define CYPRESS_MCABITS   0x01800000
#define CYPRESS_MCMBITS   0x00600000
#define CYPRESS_MVALID    0x00040000
#define CYPRESS_MIDMASK   0x0003c000   /* Only on 605 */
#define CYPRESS_BMODE     0x00002000
#define CYPRESS_ACENABLE  0x00001000
#define CYPRESS_MRFLCT    0x00000800   /* Only on 605 */
#define CYPRESS_CMODE     0x00000400
#define CYPRESS_CLOCK     0x00000200   /* Only on 604 */
#define CYPRESS_CENABLE   0x00000100
#define CYPRESS_NFAULT    0x00000002
#define CYPRESS_MENABLE   0x00000001

#endif /* !(_SPARC_CYPRESS_H) */
