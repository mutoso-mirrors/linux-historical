/******************************************************************************
 *
 * Name:	skvpd.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.24 $
 * Date:	$Date: 1999/03/11 14:25:49 $
 * Purpose:	Shared software to read and write VPD data
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	See the file "skge.c" for further information.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/******************************************************************************
 *
 * History:
 *
 *	$Log: skvpd.c,v $
 *	Revision 1.24  1999/03/11 14:25:49  malthoff
 *	Replace __STDC__ with SK_KR_PROTO.
 *	
 *	Revision 1.23  1999/01/11 15:13:11  gklug
 *	fix: syntax error
 *	
 *	Revision 1.22  1998/10/30 06:41:15  gklug
 *	rmv: WARNING
 *	
 *	Revision 1.21  1998/10/29 07:15:14  gklug
 *	fix: Write Stream function needs verify.
 *	
 *	Revision 1.20  1998/10/28 18:05:08  gklug
 *	chg: no DEBUG in VpdMayWrite
 *	
 *	Revision 1.19  1998/10/28 15:56:11  gklug
 *	fix: Return len at end of ReadStream
 *	fix: Write even less than 4 bytes correctly
 *	
 *	Revision 1.18  1998/10/28 09:00:47  gklug
 *	fix: unreferenced local vars
 *	
 *	Revision 1.17  1998/10/28 08:25:45  gklug
 *	fix: WARNING
 *	
 *	Revision 1.16  1998/10/28 08:17:30  gklug
 *	fix: typo
 *	
 *	Revision 1.15  1998/10/28 07:50:32  gklug
 *	fix: typo
 *	
 *	Revision 1.14  1998/10/28 07:20:38  gklug
 *	chg: Interface functions to use IoC as parameter as well
 *	fix: VpdRead/WriteDWord now return SK_U32
 *	chg: VPD_IN/OUT names conform to SK_IN/OUT
 *	add: usage of VPD_IN/OUT8 macros
 *	add: VpdRead/Write Stream functions to r/w a stream of data
 *	fix: VpdTransferBlock swapped illeagal
 *	add: VpdMayWrite
 *	
 *	Revision 1.13  1998/10/22 10:02:37  gklug
 *	fix: SysKonnectFileId typo
 *	
 *	Revision 1.12  1998/10/20 10:01:01  gklug
 *	fix: parameter to SkOsGetTime
 *	
 *	Revision 1.11  1998/10/15 12:51:48  malthoff
 *	Remove unrequired parameter p in vpd_setup_para().
 *	
 *	Revision 1.10  1998/10/08 14:52:43  malthoff
 *	Remove CvsId by SysKonnectFileId.
 *	
 *	Revision 1.9  1998/09/16 07:33:52  malthoff
 *	remove memcmp() by SK_MEMCMP and
 *	memcpy() by SK_MEMCPY() to be
 *	independant from the 'C' Standard Library.
 *	
 *	Revision 1.8  1998/08/19 12:52:35  malthoff
 *	compiler fix: use SK_VPD_KEY instead of S_VPD.
 *	
 *	Revision 1.7  1998/08/19 08:14:01  gklug
 *	fix: remove struct keyword as much as possible from the c-code (see CCC)
 *	
 *	Revision 1.6  1998/08/18 13:03:58  gklug
 *	SkOsGetTime now returns SK_U64
 *	
 *	Revision 1.5  1998/08/18 08:17:29  malthoff
 *	Ensure we issue a VPD read in vpd_read_dword().
 *	Discard all VPD keywords other than Vx or Yx, where
 *	x is '0..9' or 'A..Z'.
 *	
 *	Revision 1.4  1998/07/03 14:52:19  malthoff
 *	Add category SK_DBGCAT_FATAL to some debug macros.
 *	bug fix: correct the keyword name check in vpd_write().
 *	
 *	Revision 1.3  1998/06/26 11:16:53  malthoff
 *	Correct the modified File Identifier.
 *	
 *	Revision 1.2  1998/06/26 11:13:43  malthoff
 *	Modify the File Identifier.
 *	
 *	Revision 1.1  1998/06/19 14:11:08  malthoff
 *	Created, Tests with AIX were performed successfully
 *	
 *
 ******************************************************************************/

/*
	Please refer skvpd.txt for infomation how to include this module
 */
static const char SysKonnectFileId[] =
	"@(#)$Id: skvpd.c,v 1.24 1999/03/11 14:25:49 malthoff Exp $ (C) SK" ;

#include "h/skdrv1st.h"
#include "h/sktypes.h"
#include "h/skdebug.h"
#include "h/skdrv2nd.h"

/*
 * Static functions
 */
#ifndef SK_KR_PROTO
static SK_VPD_PARA	*vpd_find_para(
	SK_AC	*pAC,
	char		*key,
	SK_VPD_PARA *p) ;
#else	/* SK_KR_PROTO */
static SK_VPD_PARA	*vpd_find_para() ;
#endif	/* SK_KR_PROTO */

/*
 * waits for a completetion of a VPD transfer
 * The VPD transfer must complete within SK_TICKS_PER_SEC/16
 *
 * returns	0:	success, transfer completes
 *		error	exit(9) with a error message
 */
static int	VpdWait(
SK_AC		*pAC,	/* Adapters context */
SK_IOC		IoC,	/* IO Context */
int		event)	/* event to wait for (VPD_READ / VPD_write) completion*/
{
	SK_U64	start_time ;
	SK_U16	state ;

	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
		("vpd wait for %s\n",event?"Write":"Read")) ;
	start_time = SkOsGetTime(pAC) ;
	do {
		if (SkOsGetTime(pAC) - start_time > SK_TICKS_PER_SEC/16) {
			VPD_STOP(pAC,IoC) ;
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,
				SK_DBGCAT_FATAL|SK_DBGCAT_ERR,
				("ERROR:vpd wait timeout\n")) ;
			return(1) ;
		}
		VPD_IN16(pAC,IoC,PCI_VPD_ADR_REG,&state) ;
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
			("state = %x, event %x\n",state,event)) ;
	} while((int)(state & PCI_VPD_FLAG) == event) ;

	return(0) ;
}


/*
 * Read the dword at address 'addr' from the VPD EEPROM.
 *
 * Needed Time:	MIN 1,3 ms	MAX 2,6 ms
 *
 * Note: The DWord is returned in the endianess of the machine the routine
 *       is running on.
 *
 * Returns the data read.
 */
SK_U32		VpdReadDWord(
SK_AC		*pAC,	/* Adapters context */
SK_IOC		IoC,	/* IO Context */
int		addr)	/* VPD address */
{
	SK_U32	Rtv ;

	/* start VPD read */
	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
		("vpd read dword at 0x%x\n",addr)) ;
	addr &= ~VPD_WRITE ;		/* ensure the R/W bit is set to read */

	VPD_OUT16(pAC,IoC,PCI_VPD_ADR_REG, (SK_U16) addr) ;

	/* ignore return code here */
	(void)VpdWait(pAC,IoC,VPD_READ) ;

	/* Don't swap here, it's a data stream of bytes */
	Rtv = 0 ;

	VPD_IN32(pAC,IoC,PCI_VPD_DAT_REG,&Rtv) ;
	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
		("vpd read dword data = 0x%x\n",Rtv)) ;
	return (Rtv) ;
}

/*
	Write the dword 'data' at address 'addr' into the VPD EEPROM, and
	verify that the data is written.

 Needed Time:

.				MIN		MAX
. -------------------------------------------------------------------
. write				1.8 ms		3.6 ms
. internal write cyles		0.7 ms		7.0 ms
. -------------------------------------------------------------------
. over all program time	 	2.5 ms		10.6 ms
. read				1.3 ms		2.6 ms
. -------------------------------------------------------------------
. over all 			3.8 ms		13.2 ms
.


 Returns	0:	success
		1:	error,	I2C transfer does not terminate
		2:	error,	data verify error

 */
static int	VpdWriteDWord(
SK_AC		*pAC,	/* pAC pointer */
SK_IOC		IoC,	/* IO Context */
int		addr,	/* VPD address */
SK_U32		data)	/* VPD data to write */
{
	/* start VPD write */
	/* Don't swap here, it's a data stream of bytes */
	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
		("vpd write dword at addr 0x%x, data = 0x%x\n",addr,data)) ;
	VPD_OUT32(pAC,IoC,PCI_VPD_DAT_REG, (SK_U32)data) ;
	/* But do it here */
	addr |= VPD_WRITE ;

	VPD_OUT16(pAC,IoC,PCI_VPD_ADR_REG, (SK_U16)(addr | VPD_WRITE)) ;

	/* this may take up to 10,6 ms */
	if (VpdWait(pAC,IoC,VPD_WRITE)) {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
			("Write Timed Out\n")) ;
		return(1) ;
	} ;

	/* verify data */
	if (VpdReadDWord(pAC,IoC,addr) != data) {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR|SK_DBGCAT_FATAL,
			("Data Verify Error\n")) ;
		return(2) ;
	}
	return(0) ;
}

/*
 *	Read one Stream of 'len' bytes of VPD data, starting at 'addr' from
 *	or to the I2C EEPROM.
 *
 * Returns number of bytes read / written.
 */
static int	VpdWriteStream(
SK_AC		*pAC,	/* Adapters context */
SK_IOC		IoC,	/* IO Context */
char		*buf,	/* data buffer */
int		Addr,	/* VPD start address */
int		Len)	/* number of bytes to read / to write */
{
	int		i ;
	int		j ;
	SK_U16		AdrReg ;
	int		Rtv ;
	SK_U8		* pComp;	/* Compare pointer */
	SK_U8		Data ;		/* Input Data for Compare */

	/* Init Compare Pointer */
	pComp = (SK_U8 *) buf;

	for (i=0; i < Len; i ++, buf++) {
		if ((i%sizeof(SK_U32)) == 0) {
			/*
			 * At the begin of each cycle read the Data Reg
			 * So it is initialized even if only a few bytes
			 * are written.
			 */
			AdrReg = (SK_U16) Addr ;
			AdrReg &= ~VPD_WRITE ;	/* READ operation */

			VPD_OUT16(pAC,IoC,PCI_VPD_ADR_REG, AdrReg) ;

			/* ignore return code here */
			Rtv = VpdWait(pAC,IoC,VPD_READ) ;
			if (Rtv != 0) {
				return(i) ;
			}
		}

		/* Write current Byte */
		VPD_OUT8(pAC,IoC,PCI_VPD_DAT_REG+(i%sizeof(SK_U32)),
				*(SK_U8*)buf) ;

		if (((i%sizeof(SK_U32)) == 3) || (i == (Len - 1))) {
			/* New Address needs to be written to VPD_ADDR reg */
			AdrReg = (SK_U16) Addr ;
			Addr += sizeof(SK_U32);
			AdrReg |= VPD_WRITE ;	/* WRITE operation */

			VPD_OUT16(pAC,IoC,PCI_VPD_ADR_REG, AdrReg) ;

			/* Wait for termination */
			Rtv = VpdWait(pAC,IoC,VPD_WRITE) ;
			if (Rtv != 0) {
				SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
					("Write Timed Out\n")) ;
				return(i - (i%sizeof(SK_U32))) ;
			}

			/*
			 * Now re-read to verify
			 */
			AdrReg &= ~VPD_WRITE ;	/* READ operation */

			VPD_OUT16(pAC,IoC,PCI_VPD_ADR_REG, AdrReg) ;

			/* Wait for termination */
			Rtv = VpdWait(pAC,IoC,VPD_READ) ;
			if (Rtv != 0) {
				SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
					("Verify Timed Out\n")) ;
				return(i - (i%sizeof(SK_U32))) ;
			}

			for (j = 0; j <= (int) (i%sizeof(SK_U32));
				j ++, pComp ++ ) {
				VPD_IN8(pAC,IoC,PCI_VPD_DAT_REG+j, &Data) ;
				if (Data != *pComp) {
					/* Verify Error */
					SK_DBG_MSG(pAC,SK_DBGMOD_VPD,
						SK_DBGCAT_ERR,
						("WriteStream Verify Error\n"));
					return(i - (i%sizeof(SK_U32)) + j);
				}
			}

		}
	}

	return(Len);
}
	

/*
 *	Read one Stream of 'len' bytes of VPD data, starting at 'addr' from
 *	or to the I2C EEPROM.
 *
 * Returns number of bytes read / written.
 */
static int	VpdReadStream(
SK_AC		*pAC,	/* Adapters context */
SK_IOC		IoC,	/* IO Context */
char		*buf,	/* data buffer */
int		Addr,	/* VPD start address */
int		Len)	/* number of bytes to read / to write */
{
	int		i ;
	SK_U16		AdrReg ;
	int		Rtv ;

	for (i=0; i < Len; i ++, buf++) {
		if ((i%sizeof(SK_U32)) == 0) {
			/* New Address needs to be written to VPD_ADDR reg */
			AdrReg = (SK_U16) Addr ;
			Addr += sizeof(SK_U32);
			AdrReg &= ~VPD_WRITE ;	/* READ operation */

			VPD_OUT16(pAC,IoC,PCI_VPD_ADR_REG, AdrReg) ;

			/* ignore return code here */
			Rtv = VpdWait(pAC,IoC,VPD_READ) ;
			if (Rtv != 0) {
				return(i) ;
			}

		}
		VPD_IN8(pAC,IoC,PCI_VPD_DAT_REG+(i%sizeof(SK_U32)),
			(SK_U8 *)buf) ;
	}

	return(Len) ;
}

/*
 *	Read ore wirtes 'len' bytes of VPD data, starting at 'addr' from
 *	or to the I2C EEPROM.
 *
 * Returns number of bytes read / written.
 */
static int	VpdTransferBlock(
SK_AC		*pAC,	/* Adapters context */
SK_IOC		IoC,	/* IO Context */
char		*buf,	/* data buffer */
int		addr,	/* VPD start address */
int		len,	/* number of bytes to read / to write */
int		dir)	/* transfer direction may be VPD_READ or VPD_WRITE */
{
	int		Rtv ;	/* Return value */
	int		vpd_rom_size ;
	SK_U32		our_reg2 ;

	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
		("vpd %s block, addr = 0x%x, len = %d\n",
		dir?"write":"read",addr,len)) ;

	if (len == 0)
		return (0) ;

	VPD_IN32(pAC,IoC,PCI_OUR_REG_2,&our_reg2) ;
	vpd_rom_size = 256 << ((our_reg2 & PCI_VPD_ROM_SZ) >> 14);
	if (addr > vpd_rom_size - 4) {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR|SK_DBGCAT_FATAL,
			("Address error: 0x%x, exp. < 0x%x\n",
			addr, vpd_rom_size - 4)) ;
		return (0) ;
	}
	if (addr + len > vpd_rom_size) {
		len = vpd_rom_size - addr ;
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
			("Warning: len was cut to %d\n",len)) ;
	}

	if (dir == VPD_READ) {
		Rtv = VpdReadStream(pAC, IoC, buf, addr, len);
	} else {
		Rtv = VpdWriteStream(pAC, IoC, buf, addr, len);
	}

	return (Rtv) ;
}

#ifdef SKDIAG

/*
 *	Read 'len' bytes of VPD data, starting at 'addr'.
 *
 * Returns number of bytes read.
 */
int		VpdReadBlock(
SK_AC		*pAC,	/* pAC pointer */
SK_IOC		IoC,	/* IO Context */
char		*buf,	/* buffer were the data should be stored */
int		addr,	/* start reading at the VPD address */
int		len)	/* number of bytes to read */
{
	return (VpdTransferBlock(pAC, IoC, buf, addr, len, VPD_READ)) ;
}

/*
 *	Write 'len' bytes of *but to the VPD EEPROM, starting at 'addr'.
 *
 * Returns number of bytes writes.
 */
int		VpdWriteBlock(
SK_AC		*pAC,	/* pAC pointer */
SK_IOC		IoC,	/* IO Context */
char		*buf,	/* buffer, holds the data to write */
int		addr,	/* start writing at the VPD address */
int		len)	/* number of bytes to write */
{
	return (VpdTransferBlock(pAC, IoC, buf, addr, len, VPD_WRITE)) ;
}
#endif	/* SKDIAG */

/*
 * (re)initialize the VPD buffer
 *
 * Reads the VPD data from the EEPROM into the VPD buffer.
 * Get the remaining read only and read / write space.
 *
 * return	0:	success
 *		1:	fatal VPD error
 */
static int	VpdInit(
SK_AC		*pAC,	/* Adapters context */
SK_IOC		IoC)	/* IO Context */
{
	SK_VPD_PARA *r, rp ;	/* RW or RV */
	int		i ;
	unsigned char	x ;

	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_INIT,("VpdInit .. ")) ;
	/* read the VPD data into the VPD buffer */
	if (VpdTransferBlock(pAC,IoC,pAC->vpd.vpd_buf,0,VPD_SIZE,VPD_READ)
		!= VPD_SIZE) {

		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
			("Block Read Error\n")) ;
		return(1) ;
	}

	/* find the end tag of the RO area */
	if (!(r = vpd_find_para(pAC,VPD_RV,&rp))) {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR | SK_DBGCAT_FATAL,
			("Encoding Error: RV Tag not found\n")) ;
		return (1) ;
	}
	if (r->p_val + r->p_len > pAC->vpd.vpd_buf + VPD_SIZE/2) {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR | SK_DBGCAT_FATAL,
			("Encoding Error: Invalid VPD struct size\n")) ;
		return (1) ;
	}
	pAC->vpd.v.vpd_free_ro = r->p_len - 1 ;

	/* test the checksum */
	for (i = 0, x = 0; (unsigned)i<=(unsigned)VPD_SIZE/2 - r->p_len; i++) {
		x += pAC->vpd.vpd_buf[i] ;
	}
	if (x != 0) {
		/* checksum error */
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR | SK_DBGCAT_FATAL,
			("VPD Checksum Error\n")) ;
		return (1) ;
	}

	/* find and check the end tag of the RW area */
	if (!(r = vpd_find_para(pAC,VPD_RW,&rp))) {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR | SK_DBGCAT_FATAL,
			("Encoding Error: RV Tag not found\n")) ;
		return (1) ;
	}
	if (r->p_val < pAC->vpd.vpd_buf + VPD_SIZE/2) {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR | SK_DBGCAT_FATAL,
			("Encoding Error: Invalid VPD struct size\n")) ;
		return (1) ;
	}
	pAC->vpd.v.vpd_free_rw = r->p_len ;

	/* everything seems to be ok */
	pAC->vpd.v.vpd_status |= VPD_VALID ;
	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_INIT,
		("done. Free RO = %d, Free RW = %d\n",
		pAC->vpd.v.vpd_free_ro, pAC->vpd.v.vpd_free_rw)) ;

	return(0) ;
}

/*
 *	find the Keyword 'key' in the VPD buffer and fills the
 *	parameter sturct 'p' with it's values
 *
 * returns	*p	success
 *		0:	parameter was not found or VPD encoding error
 */
static SK_VPD_PARA *vpd_find_para(
SK_AC *pAC,	/* common data base */
char *key,		/* keyword to find (e.g. "MN") */
SK_VPD_PARA *p)	/* parameter description struct */
{
	char *v	;	/* points to vpd buffer */
	int max ;	/* Maximum Number of Iterations */

	v = pAC->vpd.vpd_buf ;
	max = 128 ;

	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
		("vpd find para %s .. ",key)) ;

	/* check mandatory resource type ID string (Product Name) */
	if (*v != (char) RES_ID) {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR | SK_DBGCAT_FATAL,
			("Error: 0x%x missing\n",RES_ID)) ;
		return (0) ;
	}

	if (strcmp(key,VPD_NAME) == 0) {
		p->p_len = VPD_GET_RES_LEN(v) ;
		p->p_val = VPD_GET_VAL(v) ;
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
			("found, len = %d\n",p->p_len)) ;
		return(p) ;
	}

	v += 3 + VPD_GET_RES_LEN(v) + 3 ;
	for ( ; ; ) {
		if (SK_MEMCMP(key,v,2) == 0) {
			p->p_len = VPD_GET_VPD_LEN(v) ;
			p->p_val = VPD_GET_VAL(v) ;
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
				("found, len = %d\n",p->p_len)) ;
			return (p) ;
		}

		/* exit when reaching the "RW" Tag or the maximum of itera. */
		max-- ;
		if (SK_MEMCMP(VPD_RW,v,2) == 0 || max == 0) {
			break ;
		}

		if (SK_MEMCMP(VPD_RV,v,2) == 0) {
			v += 3 + VPD_GET_VPD_LEN(v) + 3 ;	/* skip VPD-W */
		} else {
			v += 3 + VPD_GET_VPD_LEN(v) ;
		}
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
			("scanning '%c%c' len = %d\n",v[0],v[1],v[2])) ;
	}

#ifdef DEBUG
	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,("not found\n")) ;
	if (max == 0) {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR | SK_DBGCAT_FATAL,
			("Key/Len Encoding error\n")) ;
	}
#endif
	return (0) ;
}

/*
 *	Move 'n' bytes. Begin with the last byte if 'n' is > 0,
 *	Start with the last byte if n is < 0.
 *
 * returns nothing
 */
static void vpd_move_para(
char *start,		/* start of memory block */
char *end,		/* end of memory block to move */
int n)			/* number of bytes the memory block has to be moved */
{
	char *p ;
	int i ;		/* number of byte copied */

	if (n == 0)
		return ;

	i = end - start + 1 ;
	if (n < 0) {
		p = start + n ;
		while (i != 0) {
			*p++ = *start++ ;
			i-- ;
		}
	} else {
		p = end + n ;
		while (i != 0) {
			*p-- = *end-- ;
			i-- ;
		}
	}
}

/*
 *	setup the VPD keyword 'key' at 'ip'.
 *
 * returns nothing
 */
static void vpd_insert_key(
char *key,		/* keyword to insert */
char *buf,		/* buffer with the keyword value */
int len,		/* length of the value string */
char *ip)		/* inseration point */
{
	SK_VPD_KEY *p ;

	p = (SK_VPD_KEY *) ip ;
	p->p_key[0] = key[0] ;
	p->p_key[1] = key[1] ;
	p->p_len = (unsigned char) len ;
	SK_MEMCPY(&p->p_val,buf,len) ;
}

/*
 *	Setup the VPD end tag "RV" / "RW".
 *	Also correct the remaining space variables vpd_free_ro / vpd_free_rw.
 *
 * returns	0:	success
 *		1:	encoding error
 */
static int vpd_mod_endtag(
SK_AC *pAC,	/* common data base */
char *etp)		/* end pointer input position */
{
	SK_VPD_KEY *p ;
	unsigned char	x ;
	int	i ;

	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
		("vpd modify endtag at 0x%x = '%c%c'\n",etp,etp[0],etp[1])) ;

	p = (SK_VPD_KEY *) etp ;

	if (p->p_key[0] != 'R' || (p->p_key[1] != 'V' && p->p_key[1] != 'W')) {
		/* something wrong here, encoding error */
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR | SK_DBGCAT_FATAL,
			("Encoding Error: invalid end tag\n")) ;
		return(1) ;
	}
	if (etp > pAC->vpd.vpd_buf + VPD_SIZE/2) {
		/* create "RW" tag */
		p->p_len = (unsigned char)(pAC->vpd.vpd_buf+VPD_SIZE-etp-3-1) ;
		pAC->vpd.v.vpd_free_rw = (int) p->p_len ;
		i = pAC->vpd.v.vpd_free_rw ;
		etp += 3 ;
	} else {
		/* create "RV" tag */
		p->p_len = (unsigned char)(pAC->vpd.vpd_buf+VPD_SIZE/2-etp-3) ;
		pAC->vpd.v.vpd_free_ro = (int) p->p_len - 1 ;

		/* setup checksum */
		for (i = 0, x = 0; i < VPD_SIZE/2 - p->p_len; i++) {
			x += pAC->vpd.vpd_buf[i] ;
		}
		p->p_val = (char) 0 - x ;
		i = pAC->vpd.v.vpd_free_ro ;
		etp += 4 ;
	}
	while (i) {
		*etp++ = 0x00 ;
		i-- ;
	}

	return (0) ;
}

/*
 *	Insert a VPD keyword into the VPD buffer.
 *
 *	The keyword 'key' is inserted at the position 'ip' in the
 *	VPD buffer.
 *	The keywords behind the input position will
 *	be moved. The VPD end tag "RV" or "RW" is generated again.
 *
 * returns	0:	success
 *		2:	value string was cut
 *		4:	VPD full, keyword was not written
 *		6:	fatal VPD error
 *
 */
int	VpdSetupPara(
SK_AC	*pAC,		/* common data base */
char	*key,		/* keyword to insert */
char	*buf,		/* buffer with the keyword value */
int	len,		/* length of the keyword value */
int	type,		/* VPD_RO_KEY or VPD_RW_KEY */
int	op)			/* operation to do: ADD_KEY or OWR_KEY */
{
	SK_VPD_PARA vp ;
	char	*etp ;		/* end tag position */
	int	free ;		/* remaining space in selected area */
	char	*ip ;		/* input position inside the VPD buffer */
	int	rtv ;		/* return code */
	int	head ;		/* additional haeder bytes to move */
	int	found ;		/* additinoal bytes if the keyword was found */

	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
		("vpd setup para key = %s, val = %s\n",key,buf)) ;

	rtv = 0 ;
	ip = 0 ;
	if (type == VPD_RW_KEY) {
		/* end tag is "RW" */
		free = pAC->vpd.v.vpd_free_rw ;
		etp = pAC->vpd.vpd_buf + (VPD_SIZE - free - 1 - 3) ;
	} else {
		/* end tag is "RV" */
		free = pAC->vpd.v.vpd_free_ro ;
		etp = pAC->vpd.vpd_buf + (VPD_SIZE/2 - free - 4) ;
	}
	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
		("Free RO = %d, Free RW = %d\n",
		pAC->vpd.v.vpd_free_ro, pAC->vpd.v.vpd_free_rw)) ;

	head = 0 ;
	found = 0 ;
	if (op == OWR_KEY) {
		if (vpd_find_para(pAC,key,&vp)) {
			found = 3 ;
			ip = vp.p_val - 3 ;
			free += vp.p_len + 3 ;
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
				("Overwrite Key\n")) ;
		} else {
			op = ADD_KEY ;
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_CTRL,
				("Add Key\n")) ;
		}
	}
	if (op == ADD_KEY) {
		ip = etp ;
		vp.p_len = 0 ;
		head = 3 ;
	}

	if (len + 3 > free) {
		if (free < 7) {
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
				("VPD Buffer Overflow, keyword not written\n"));
			return (4) ;
		}
		/* cut it again */
		len = free - 3 ;
		rtv = 2 ;
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
			("VPD Buffer Full, Keyword was cut\n")) ;
	}

	vpd_move_para(ip + vp.p_len + found, etp+2, len-vp.p_len+head) ;
	vpd_insert_key(key, buf, len, ip) ;
	if (vpd_mod_endtag(pAC, etp + len - vp.p_len + head)) {
		pAC->vpd.v.vpd_status &= ~VPD_VALID ;
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
			("VPD Encoding Error\n")) ;
		return(6) ;
	}

	return (rtv) ;
}


/*
 *	Read the contents of the VPD EEPROM and copy it to the
 *	VPD buffer if not already done.
 *
 * return:	A pointer to the vpd_status structure. The structure contain
 *		this fields.
 */
SK_VPD_STATUS	*VpdStat(
SK_AC		*pAC,	/* Adapters context */
SK_IOC		IoC)	/* IO Context */
{
	if (!(pAC->vpd.v.vpd_status & VPD_VALID)) {
		(void)VpdInit(pAC,IoC) ;
	}
	return(&pAC->vpd.v) ;
}


/*
 *	Read the contents of the VPD EEPROM and copy it to the VPD
 *	buffer if not already done.
 *	Scan the VPD buffer for VPD keywords and create the VPD
 *	keyword list by copying the keywords to 'buf', all after
 *	each other and terminated with a '\0'.
 *
 * Exceptions:	o The Resource Type ID String (product name) is called "Name"
 *		o The VPD end tags 'RV' and 'RW' are not listed
 *
 *	The number of copied keywords is counted in 'elements'.
 *
 * returns	0:	success
 *		2:	buffer overfull, one or more keywords are missing
 *		6:	fatal VPD error
 *
 *	example values after returning:
 *
 *		buf =	"Name\0PN\0EC\0MN\0SN\0CP\0VF\0VL\0YA\0"
 *		*len =		30
 *		*elements =	 9
 */
int		VpdKeys(
SK_AC		*pAC,		/* common data base */
SK_IOC		IoC,		/* IO Context */
char		*buf,		/* buffer where to copy the keywords */
int		*len,		/* buffer length */
int		*elements)	/* number of keywords returned */
{
	char *v ;
	int n ;

	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_RX,("list vpd keys .. ")) ;
	*elements = 0 ;
	if (!(pAC->vpd.v.vpd_status & VPD_VALID)) {
		if (VpdInit(pAC,IoC) != 0 ) {
			*len = 0 ;
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
				("VPD Init Error, terminated\n")) ;
			return(6) ;
		}
	}

	if ((signed)strlen(VPD_NAME) + 1 <= *len) {
		v = pAC->vpd.vpd_buf ;
		strcpy(buf,VPD_NAME) ;
		n = strlen(VPD_NAME) + 1 ;
		buf += n ;
		*elements = 1 ;
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_RX,
			("'%c%c' ",v[0],v[1])) ;
	} else {
		*len = 0 ;
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
			("buffer overflow\n")) ;
		return(2) ;
	}

	v += 3 + VPD_GET_RES_LEN(v) + 3 ;
	for ( ; ; ) {
		/* exit when reaching the "RW" Tag */
		if (SK_MEMCMP(VPD_RW,v,2) == 0) {
			break ;
		}

		if (SK_MEMCMP(VPD_RV,v,2) == 0) {
			v += 3 + VPD_GET_VPD_LEN(v) + 3 ;	/* skip VPD-W */
			continue ;
		}

		if (n+3 <= *len) {
			SK_MEMCPY(buf,v,2) ;
			buf += 2 ;
			*buf++ = '\0' ;
			n += 3 ;
			v += 3 + VPD_GET_VPD_LEN(v) ;
			*elements += 1 ;
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_RX,
				("'%c%c' ",v[0],v[1])) ;
		} else {
			*len = n ;
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
				("buffer overflow\n")) ;
			return (2) ;
		}
	}

	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_RX,("\n")) ;
	*len = n ;
	return(0) ;
}


/*
 *	Read the contents of the VPD EEPROM and copy it to the
 *	VPD buffer if not already done. Search for the VPD keyword
 *	'key' and copy its value to 'buf'. Add a terminating '\0'.
 *	If the value does not fit into the buffer cut it after
 *	'len' - 1 bytes.
 *
 * returns	0:	success
 *		1:	keyword not found
 *		2:	value string was cut
 *		3:	VPD transfer timeout
 *		6:	fatal VPD error
 */
int		VpdRead(
SK_AC		*pAC,	/* common data base */
SK_IOC		IoC,	/* IO Context */
char		*key,	/* keyword to read (e.g. "MN") */
char		*buf,	/* buffer where to copy the keyword value */
int		*len)	/* buffer length */
{
	SK_VPD_PARA *p, vp ;

	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_RX,("vpd read %s .. ",key)) ;
	if (!(pAC->vpd.v.vpd_status & VPD_VALID)) {
		if (VpdInit(pAC,IoC) != 0 ) {
			*len = 0 ;
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
				("vpd init error\n")) ;
			return(6) ;
		}
	}

	if ((p = vpd_find_para(pAC,key,&vp))) {
		if (p->p_len > (*(unsigned *)len)-1) {
			p->p_len = *len - 1 ;
		}
		SK_MEMCPY(buf,p->p_val,p->p_len) ;
		buf[p->p_len] = '\0' ;
		*len = p->p_len ;
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_RX,
			("%c%c%c%c.., len = %d\n",
			buf[0],buf[1],buf[2],buf[3],*len)) ;
	} else {
		*len = 0 ;
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,("not found\n")) ;
		return (1) ;
	}
	return (0) ;
}


/*
 *	Check whether a given key may be written
 *
 * returns
 *	SK_TRUE		Yes it may be written
 *	SK_FALSE	No it may be written
 */
SK_BOOL		VpdMayWrite(
char		*key)	/* keyword to write (allowed values "Yx", "Vx") */
{
	if ((*key != 'Y' && *key != 'V') ||
		key[1] < '0' || key[1] > 'Z' ||
		(key[1] > '9' && key[1] < 'A') || strlen(key) != 2) {

		return (SK_FALSE) ;
	}
	return (SK_TRUE) ;
}

/*
 *	Read the contents of the VPD EEPROM and copy it to the VPD
 *	buffer if not already done. Insert/overwrite the keyword 'key'
 *	in the VPD buffer. Cut the keyword value if it does not fit
 *	into the VPD read / write area.
 *
 * returns	0:	success
 *		2:	value string was cut
 *		3:	VPD transfer timeout
 *		4:	VPD full, keyword was not written
 *		5:	keyword cannot be written
 *		6:	fatal VPD error
 */
int		VpdWrite(
SK_AC		*pAC,	/* common data base */
SK_IOC		IoC,	/* IO Context */
char		*key,	/* keyword to write (allowed values "Yx", "Vx") */
char		*buf)	/* buffer where the keyword value can be read from */
{
	int len ;			/* lenght of the keyword to write */
	int rtv ;			/* return code */
	int rtv2 ;

	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_TX,
		("vpd write %s = %s\n",key,buf)) ;

	if ((*key != 'Y' && *key != 'V') ||
		key[1] < '0' || key[1] > 'Z' ||
		(key[1] > '9' && key[1] < 'A') || strlen(key) != 2) {

		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
			("illegal key tag, keyword not written\n")) ;
		return (5) ;
	}

	if (!(pAC->vpd.v.vpd_status & VPD_VALID)) {
		if (VpdInit(pAC,IoC) != 0 ) {
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
				("vpd init error\n")) ;
			return(6) ;
		}
	}

	rtv = 0 ;
	len = strlen(buf) ;
	if (len > VPD_MAX_LEN) {
		/* cut it */
		len = VPD_MAX_LEN ;
		rtv = 2 ;
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
			("keyword to long, cut after %d bytes\n",VPD_MAX_LEN)) ;
	}
	if ((rtv2 = VpdSetupPara(pAC,key,buf,len,VPD_RW_KEY,OWR_KEY)) != 0) {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
			("vpd write error\n")) ;
		return(rtv2) ;
	}

	return (rtv) ;
}

/*
 *	Read the contents of the VPD EEPROM and copy it to the
 *	VPD buffer if not already done. Remove the VPD keyword
 *	'key' from the VPD buffer.
 *	Only the keywords in the read/write area can be deleted.
 *	Keywords in the read only area cannot be deleted.
 *
 * returns	0:	success, keyword was removed
 *		1:	keyword not found
 *		5:	keyword cannot be deleted
 *		6:	fatal VPD error
 */
int		VpdDelete(
SK_AC		*pAC,	/* common data base */
SK_IOC		IoC,	/* IO Context */
char		*key)	/* keyword to read (e.g. "MN") */
{
	SK_VPD_PARA *p, vp ;
	char *etp ;

	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_TX,("vpd delete key %s\n",key)) ;
	if (!(pAC->vpd.v.vpd_status & VPD_VALID)) {
		if (VpdInit(pAC,IoC) != 0 ) {
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
				("vpd init error\n")) ;
			return(6) ;
		}
	}

	if ((p = vpd_find_para(pAC,key,&vp))) {
		if (p->p_val < pAC->vpd.vpd_buf + VPD_SIZE/2) {
			/* try to delete read only keyword */
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
				("cannot delete RO keyword\n")) ;
			return (5) ;
		}

		etp = pAC->vpd.vpd_buf + (VPD_SIZE-pAC->vpd.v.vpd_free_rw-1-3) ;

		vpd_move_para(vp.p_val+vp.p_len, etp+2,
			- ((int)(vp.p_len + 3))) ;
		if (vpd_mod_endtag(pAC, etp - vp.p_len - 3)) {
			pAC->vpd.v.vpd_status &= ~VPD_VALID ;
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
				("vpd encoding error\n")) ;
			return(6) ;
		}
	} else {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
			("keyword not found\n")) ;
		return (1) ;
	}

	return (0) ;
}

/*
 *	If the VPD buffer contains valid data write the VPD
 *	read/write area back to the VPD EEPROM.
 *
 * returns	0:	success
 *		3:	VPD transfer timeout
 */
int		VpdUpdate(
SK_AC		*pAC,	/* Adapters context */
SK_IOC		IoC)	/* IO Context */
{
	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_TX,("vpd update .. ")) ;
	if (pAC->vpd.v.vpd_status & VPD_VALID) {
		if (VpdTransferBlock(pAC,IoC,pAC->vpd.vpd_buf + VPD_SIZE/2,
			VPD_SIZE/2, VPD_SIZE/2, VPD_WRITE) != VPD_SIZE/2) {

			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
				("transfer timed out\n")) ;
			return(3) ;
		}
	}
	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_TX,("done\n")) ;
	return (0) ;
}



/*
 *	Read the contents of the VPD EEPROM and copy it to the VPD buffer
 *	if not already done. If the keyword "VF" is not present it will be
 *	created and the error log message will be stored to this keyword.
 *	If "VF" is not present the error log message will be stored to the
 *	keyword "VL". "VL" will created or overwritten if "VF" is present.
 *	The VPD read/write area is saved to the VPD EEPROM.
 *
 * returns nothing, errors will be ignored.
 */
void		VpdErrLog(
SK_AC		*pAC,	/* common data base */
SK_IOC		IoC,	/* IO Context */
char		*msg)	/* error log message */
{
	SK_VPD_PARA *v, vf ;	/* VF */
	int len ;

	SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_TX,
		("vpd error log msg %s\n",msg)) ;
	if (!(pAC->vpd.v.vpd_status & VPD_VALID)) {
		if (VpdInit(pAC,IoC) != 0 ) {
			SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_ERR,
				("vpd init error\n")) ;
			return ;
		}
	}

	len = strlen(msg) ;
	if (len > VPD_MAX_LEN) {
		/* cut it */
		len = VPD_MAX_LEN ;
	}
	if ((v = vpd_find_para(pAC,VPD_VF,&vf))) {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_TX,("overwrite VL\n")) ;
		(void)VpdSetupPara(pAC,VPD_VL,msg,len,VPD_RW_KEY,OWR_KEY) ;
	} else {
		SK_DBG_MSG(pAC,SK_DBGMOD_VPD,SK_DBGCAT_TX,("write VF\n")) ;
		(void)VpdSetupPara(pAC,VPD_VF,msg,len,VPD_RW_KEY,ADD_KEY) ;
	}

	(void)VpdUpdate(pAC,IoC) ;
}

