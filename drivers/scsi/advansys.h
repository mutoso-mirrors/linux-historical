/* $Id: advansys.h,v 1.12 1996/09/23 18:12:02 bobf Exp bobf $ */
/*
 * advansys.h - Linux Host Driver for AdvanSys SCSI Adapters
 *
 * Copyright (c) 1995-1996 Advanced System Products, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that redistributions of source
 * code retain the above copyright notice and this comment without
 * modification.
 *
 * The latest version of this driver is available at the AdvanSys
 * FTP and BBS sites listed below.
 *
 * Please send questions, comments, and bug reports to:
 * bobf@advansys.com (Bob Frey)
 */

#ifndef _ADVANSYS_H
#define _ADVANSYS_H

/* Convert Linux Version, Patch-level, Sub-level to LINUX_VERSION_CODE. */
#define ASC_LINUX_VERSION(V, P, S)	(((V) * 65536) + ((P) * 256) + (S))

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif /* LINUX_VERSION_CODE */

/*
 * Scsi_Host_Template function prototypes.
 */
int advansys_detect(Scsi_Host_Template *);
int advansys_release(struct Scsi_Host *);
const char *advansys_info(struct Scsi_Host *);
int advansys_command(Scsi_Cmnd *);
int advansys_queuecommand(Scsi_Cmnd *, void (* done)(Scsi_Cmnd *));
int advansys_abort(Scsi_Cmnd *);
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,89)
int advansys_reset(Scsi_Cmnd *);
#else /* version >= v1.3.89 */
int advansys_reset(Scsi_Cmnd *, unsigned int);
#endif /* version >= v1.3.89 */
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,0)
int advansys_biosparam(Disk *, int, int[]);
#else /* version >= v1.3.0 */
int advansys_biosparam(Disk *, kdev_t, int[]);
extern struct proc_dir_entry proc_scsi_advansys;
int advansys_proc_info(char *, char **, off_t, int, int, int);
#endif /* version >= v1.3.0 */

/* init/main.c setup function */
void advansys_setup(char *, int *);

/*
 * AdvanSys Host Driver Scsi_Host_Template (struct SHT) from hosts.h.
 */
#if LINUX_VERSION_CODE < ASC_LINUX_VERSION(1,3,0)
#define ADVANSYS { \
	NULL,					/* struct SHT *next */ \
	NULL,					/* int *usage_count */ \
	"advansys",				/* char *name */ \
	advansys_detect,		/* int (*detect)(struct SHT *) */ \
	advansys_release,		/* int (*release)(struct Scsi_Host *) */ \
	advansys_info,			/* const char *(*info)(struct Scsi_Host *) */ \
	advansys_command, 		/* int (*command)(Scsi_Cmnd *) */ \
	advansys_queuecommand, \
			/* int (*queuecommand)(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *)) */ \
	advansys_abort,			/* int (*abort)(Scsi_Cmnd *) */ \
	advansys_reset,			/* int (*reset)(Scsi_Cmnd *) */ \
	NULL,					/* int (*slave_attach)(int, int) */ \
	advansys_biosparam,		/* int (* bios_param)(Disk *, int, int []) */ \
	/* \
	 * The following fields are set per adapter in advansys_detect(). \
	 */ \
	0,						/* int can_queue */ \
	0,						/* int this_id */ \
	0,						/* short unsigned int sg_tablesize */ \
	0,						/* short cmd_per_lun */ \
	0,						/* unsigned char present */	\
	/* \
	 * Because the driver may control an ISA adapter 'unchecked_isa_dma' \
	 * must be set. The flag will be cleared in advansys_detect for non-ISA \
	 * adapters. Refer to the comment in scsi_module.c for more information. \
	 */ \
	1,						/* unsigned unchecked_isa_dma:1 */ \
	/* \
	 * All adapters controlled by this driver are capable of large \
	 * scatter-gather lists. According to the mid-level SCSI documentation \
	 * this obviates any performance gain provided by setting \
	 * 'use_clustering'. But empirically while CPU utilization is increased \
	 * by enabling clustering, I/O throughput increases as well. \
	 */ \
	ENABLE_CLUSTERING,		/* unsigned use_clustering:1 */ \
}
#else /* version >= v1.3.0 */
#define ADVANSYS { \
	NULL,					/* struct SHT *next */ \
	NULL,					/* long *usage_count */ \
	&proc_scsi_advansys,	/* struct proc_dir_entry *proc_dir */ \
	advansys_proc_info,	\
			/* int (*proc_info)(char *, char **, off_t, int, int, int) */ \
	"advansys",				/* const char *name */ \
	advansys_detect,		/* int (*detect)(struct SHT *) */ \
	advansys_release,		/* int (*release)(struct Scsi_Host *) */ \
	advansys_info,			/* const char *(*info)(struct Scsi_Host *) */ \
	advansys_command, 		/* int (*command)(Scsi_Cmnd *) */ \
	advansys_queuecommand, \
			/* int (*queuecommand)(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *)) */ \
	advansys_abort,			/* int (*abort)(Scsi_Cmnd *) */ \
	advansys_reset, \
		/* version < v1.3.89 int (*reset)(Scsi_Cmnd *) */ \
		/* version >= v1.3.89 int (*reset)(Scsi_Cmnd *, unsigned int) */ \
	NULL,					/* int (*slave_attach)(int, int) */ \
	advansys_biosparam,		/* int (* bios_param)(Disk *, kdev_t, int []) */ \
	/* \
	 * The following fields are set per adapter in advansys_detect(). \
	 */ \
	0,						/* int can_queue */ \
	0,						/* int this_id */ \
	0,						/* short unsigned int sg_tablesize */ \
	0,						/* short cmd_per_lun */ \
	0,						/* unsigned char present */	\
	/* \
	 * Because the driver may control an ISA adapter 'unchecked_isa_dma' \
	 * must be set. The flag will be cleared in advansys_detect for non-ISA \
	 * adapters. Refer to the comment in scsi_module.c for more information. \
	 */ \
	1,						/* unsigned unchecked_isa_dma:1 */ \
	/* \
	 * All adapters controlled by this driver are capable of large \
	 * scatter-gather lists. According to the mid-level SCSI documentation \
	 * this obviates any performance gain provided by setting \
	 * 'use_clustering'. But empirically while CPU utilization is increased \
	 * by enabling clustering, I/O throughput increases as well. \
	 */ \
	ENABLE_CLUSTERING,		/* unsigned use_clustering:1 */ \
}
#endif /* version >= v1.3.0 */
#endif /* _ADVANSYS_H */
