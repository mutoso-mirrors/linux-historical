/******************************************************************************
 *
 * Name: actbl1.h - ACPI 1.0 tables
 *       $Revision: 22 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ACTBL1_H__
#define __ACTBL1_H__

#pragma pack(1)

/*
 * ACPI 1.0 Root System Description Table (RSDT)
 */
typedef struct
{
	acpi_table_header       header;                 /* ACPI Table header */
	u32                     table_offset_entry [1]; /* Array of pointers to other */
			 /* ACPI tables */
} RSDT_DESCRIPTOR_REV1;


/*
 * ACPI 1.0 Firmware ACPI Control Structure (FACS)
 */
typedef struct
{
	char                    signature[4];           /* ACPI Signature */
	u32                     length;                 /* Length of structure, in bytes */
	u32                     hardware_signature;     /* Hardware configuration signature */
	u32                     firmware_waking_vector; /* ACPI OS waking vector */
	u32                     global_lock;            /* Global Lock */
	u32                     S4bios_f        : 1;    /* Indicates if S4BIOS support is present */
	u32                     reserved1       : 31;   /* Must be 0 */
	u8                      resverved3 [40];        /* Reserved - must be zero */

} facs_descriptor_rev1;


/*
 * ACPI 1.0 Fixed ACPI Description Table (FADT)
 */
typedef struct
{
	acpi_table_header       header;                 /* ACPI Table header */
	u32                     firmware_ctrl;          /* Physical address of FACS */
	u32                     dsdt;                   /* Physical address of DSDT */
	u8                      model;                  /* System Interrupt Model */
	u8                      reserved1;              /* Reserved */
	u16                     sci_int;                /* System vector of SCI interrupt */
	u32                     smi_cmd;                /* Port address of SMI command port */
	u8                      acpi_enable;            /* Value to write to smi_cmd to enable ACPI */
	u8                      acpi_disable;           /* Value to write to smi_cmd to disable ACPI */
	u8                      S4bios_req;             /* Value to write to SMI CMD to enter S4BIOS state */
	u8                      reserved2;              /* Reserved - must be zero */
	u32                     pm1a_evt_blk;           /* Port address of Power Mgt 1a Acpi_event Reg Blk */
	u32                     pm1b_evt_blk;           /* Port address of Power Mgt 1b Acpi_event Reg Blk */
	u32                     pm1a_cnt_blk;           /* Port address of Power Mgt 1a Control Reg Blk */
	u32                     pm1b_cnt_blk;           /* Port address of Power Mgt 1b Control Reg Blk */
	u32                     pm2_cnt_blk;            /* Port address of Power Mgt 2 Control Reg Blk */
	u32                     pm_tmr_blk;             /* Port address of Power Mgt Timer Ctrl Reg Blk */
	u32                     gpe0_blk;               /* Port addr of General Purpose Acpi_event 0 Reg Blk */
	u32                     gpe1_blk;               /* Port addr of General Purpose Acpi_event 1 Reg Blk */
	u8                      pm1_evt_len;            /* Byte Length of ports at pm1_x_evt_blk */
	u8                      pm1_cnt_len;            /* Byte Length of ports at pm1_x_cnt_blk */
	u8                      pm2_cnt_len;            /* Byte Length of ports at pm2_cnt_blk */
	u8                      pm_tm_len;              /* Byte Length of ports at pm_tm_blk */
	u8                      gpe0_blk_len;           /* Byte Length of ports at gpe0_blk */
	u8                      gpe1_blk_len;           /* Byte Length of ports at gpe1_blk */
	u8                      gpe1_base;              /* Offset in gpe model where gpe1 events start */
	u8                      reserved3;              /* Reserved */
	u16                     plvl2_lat;              /* Worst case HW latency to enter/exit C2 state */
	u16                     plvl3_lat;              /* Worst case HW latency to enter/exit C3 state */
	u16                     flush_size;             /* Size of area read to flush caches */
	u16                     flush_stride;           /* Stride used in flushing caches */
	u8                      duty_offset;            /* Bit location of duty cycle field in p_cnt reg */
	u8                      duty_width;             /* Bit width of duty cycle field in p_cnt reg */
	u8                      day_alrm;               /* Index to day-of-month alarm in RTC CMOS RAM */
	u8                      mon_alrm;               /* Index to month-of-year alarm in RTC CMOS RAM */
	u8                      century;                /* Index to century in RTC CMOS RAM */
	u8                      reserved4;              /* Reserved */
	u8                      reserved4a;             /* Reserved */
	u8                      reserved4b;             /* Reserved */
	u32                     wb_invd         : 1;    /* The wbinvd instruction works properly */
	u32                     wb_invd_flush   : 1;    /* The wbinvd flushes but does not invalidate */
	u32                     proc_c1         : 1;    /* All processors support C1 state */
	u32                     plvl2_up        : 1;    /* C2 state works on MP system */
	u32                     pwr_button      : 1;    /* Power button is handled as a generic feature */
	u32                     sleep_button    : 1;    /* Sleep button is handled as a generic feature, or not present */
	u32                     fixed_rTC       : 1;    /* RTC wakeup stat not in fixed register space */
	u32                     rtcs4           : 1;    /* RTC wakeup stat not possible from S4 */
	u32                     tmr_val_ext     : 1;    /* The tmr_val width is 32 bits (0 = 24 bits) */
	u32                     reserved5       : 23;   /* Reserved - must be zero */

}  fadt_descriptor_rev1;

#pragma pack()

#endif /* __ACTBL1_H__ */


