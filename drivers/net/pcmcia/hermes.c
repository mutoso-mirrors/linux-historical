/* hermes.c
 *
 * Driver core for the "Hermes" wireless MAC controller, as used in
 * the Lucent Orinoco and Cabletron RoamAbout cards. It should also
 * work on the hfa3841 and hfa3842 MAC controller chips used in the
 * Prism I & II chipsets.
 *
 * This is not a complete driver, just low-level access routines for
 * the MAC controller itself.
 *
 * Based on the prism2 driver from Absolute Value Systems' linux-wlan
 * project, the Linux wvlan_cs driver, Lucent's HCF-Light
 * (wvlan_hcf.c) library, and the NetBSD wireless driver.
 *
 * Copyright (C) 2000, David Gibson, Linuxcare Australia <hermes@gibson.dropbear.id.au>
 * 
 * This file distributed under the GPL, version 2.
 */

static const char *version = "hermes.c: 12 Dec 2000 David Gibson <hermes@gibson.dropbear.id.au>";

#include <linux/module.h>
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <asm/io.h>
#include <linux/ptrace.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/errno.h>

#include "hermes.h"

#define CMD_BUSY_TIMEOUT (100) /* In iterations of ~1us */
#define CMD_INIT_TIMEOUT (50000) /* in iterations of ~10us */
#define CMD_COMPL_TIMEOUT (10000) /* in iterations of ~10us */
#define ALLOC_COMPL_TIMEOUT (1000) /* in iterations of ~10us */
#define BAP_BUSY_TIMEOUT (500) /* In iterations of ~1us */
#define BAP_ERROR_RETRY (10) /* How many times to retry a BAP seek when there is an error */

#define MAX(a, b) ( (a) > (b) ? (a) : (b) )
#define MIN(a, b) ( (a) < (b) ? (a) : (b) )

/*
 * Debugging helpers
 */

#undef HERMES_DEBUG
#ifdef HERMES_DEBUG

#include <stdarg.h>

#define DMSG(stuff...) do {printk(KERN_DEBUG "hermes @ 0x%x: " , hw->iobase); \
			printk(#stuff);} while (0)

#define DEBUG(lvl, stuff...) if ( (lvl) <= HERMES_DEBUG) DMSG(#stuff)

#else /* ! HERMES_DEBUG */

#define DEBUG(lvl, stuff...) do { } while (0)

#endif /* ! HERMES_DEBUG */

/*
 * Prototypes
 */

static int hermes_issue_cmd(hermes_t *hw, uint16_t cmd, uint16_t param0);

/*
 * Internal inline functions
 */

/*
 * Internal functions
 */

/* Issue a command to the chip. Waiting for it to complete is the caller's
   problem.

   Returns -EBUSY if the command register is busy, 0 on success.

   Callable from any context.
*/
static int hermes_issue_cmd(hermes_t *hw, uint16_t cmd, uint16_t param0)
{
	uint16_t reg;
/*  	unsigned long k = CMD_BUSY_TIMEOUT; */

	/* First check that the command register is not busy */
	reg = hermes_read_regn(hw, CMD);
	if (reg & HERMES_CMD_BUSY) {
		return -EBUSY;
	}

	hermes_write_regn(hw, PARAM2, 0);
	hermes_write_regn(hw, PARAM1, 0);
	hermes_write_regn(hw, PARAM0, param0);
	hermes_write_regn(hw, CMD, cmd);
	
	return 0;
}

/*
 * Function definitions
 */

void hermes_struct_init(hermes_t *hw, ushort io)
{
	hw->iobase = io;
	hw->inten = 0x0;
}

int hermes_reset(hermes_t *hw)
{
	uint16_t status, reg;
	int err = 0;
	int k;

	/* We don't want to be interrupted while resetting the chipset */
	hw->inten = 0x0;
	hermes_write_regn(hw, INTEN, 0);
	hermes_write_regn(hw, EVACK, 0xffff);

	/* Because we hope we can reset the card even if it gets into
	   a stupid state, we actually wait to see if the command
	   register will unbusy itself */
	k = CMD_BUSY_TIMEOUT;
	reg = hermes_read_regn(hw, CMD);
	while (k && (reg & HERMES_CMD_BUSY)) {
		if (reg == 0xffff) /* Special case - the card has probably been removed,
				      so don't wait for the timeout */
			return -ENODEV;

		k--;
		udelay(1);
		reg = hermes_read_regn(hw, CMD);
	}
	
	/* No need to explicitly handle the timeout - hermes_issue_cmd() will
	   probably return -EBUSY */

	/* We don't use hermes_docmd_wait here, because the reset wipes
	   the magic constant in SWSUPPORT0 away, and it gets confused */
	err = hermes_issue_cmd(hw, HERMES_CMD_INIT, 0);
	if (err)
		return err;

	reg = hermes_read_regn(hw, EVSTAT);
	k = CMD_INIT_TIMEOUT;
	while ( (! (reg & HERMES_EV_CMD)) && k) {
		k--;
		udelay(10);
		reg = hermes_read_regn(hw, EVSTAT);
	}

	DEBUG(0, "Reset completed in %d iterations\n", CMD_INIT_TIMEOUT - k);

	hermes_write_regn(hw, SWSUPPORT0, HERMES_MAGIC);

	if (! hermes_present(hw)) {
		DEBUG(0, "hermes @ 0x%x: Card removed during reset.\n",
		       hw->iobase);
		err = -ENODEV;
		goto out;
	}
		
	if (! (reg & HERMES_EV_CMD)) {
		printk(KERN_ERR "hermes @ 0x%x: Timeout waiting for card to reset (reg=0x%04x)!\n",
		       hw->iobase, reg);
		err = -ETIMEDOUT;
		goto out;
	}

	status = hermes_read_regn(hw, STATUS);

	hermes_write_regn(hw, EVACK, HERMES_EV_CMD);

	err = status & HERMES_STATUS_RESULT;

 out:
	return err;
}

/* Issue a command to the chip, and (busy!) wait for it to
 * complete.
 *
 * Returns: < 0 on internal error, 0 on success, > 0 on error returned by the firmware
 *
 * Callable from any context, but locking is your problem. */
int hermes_docmd_wait(hermes_t *hw, uint16_t cmd, uint16_t parm0, hermes_response_t *resp)
{
	int err;
	int k;
	uint16_t reg;

	err = hermes_issue_cmd(hw, cmd, parm0);
	if (err) {
		if (! hermes_present(hw)) {
			printk(KERN_WARNING "hermes @ 0x%x: Card removed while issuing command.\n",
			       hw->iobase);
			err = -ENODEV;
		} else 
			printk(KERN_ERR "hermes @ 0x%x: CMD register busy in hermes_issue_command().\n",
			       hw->iobase);
		goto out;
	}

	reg = hermes_read_regn(hw, EVSTAT);
	k = CMD_COMPL_TIMEOUT;
	while ( (! (reg & HERMES_EV_CMD)) && k) {
		k--;
		udelay(10);
		reg = hermes_read_regn(hw, EVSTAT);
	}

	if (! hermes_present(hw)) {
		printk(KERN_WARNING "hermes @ 0x%x: Card removed while waiting for command completion.\n",
		       hw->iobase);
		err = -ENODEV;
		goto out;
	}
		
	if (! (reg & HERMES_EV_CMD)) {
		printk(KERN_ERR "hermes @ 0x%x: Timeout waiting for command completion.\n",
		       hw->iobase);
		err = -ETIMEDOUT;
		goto out;
	}

	resp->status = hermes_read_regn(hw, STATUS);
	resp->resp0 = hermes_read_regn(hw, RESP0);
	resp->resp1 = hermes_read_regn(hw, RESP1);
	resp->resp2 = hermes_read_regn(hw, RESP2);

	hermes_write_regn(hw, EVACK, HERMES_EV_CMD);

	err = resp->status & HERMES_STATUS_RESULT;

 out:
	return err;
}

int hermes_allocate(hermes_t *hw, uint16_t size, uint16_t *fid)
{
	int err = 0;
	hermes_response_t resp;
	int k;
	uint16_t reg;
	
	if ( (size < HERMES_ALLOC_LEN_MIN) || (size > HERMES_ALLOC_LEN_MAX) )
		return -EINVAL;

	err = hermes_docmd_wait(hw, HERMES_CMD_ALLOC, size, &resp);
	if (err) {
		printk(KERN_WARNING "hermes @ 0x%x: Frame allocation command failed (0x%X).\n",
		       hw->iobase, err);
		return err;
	}

	reg = hermes_read_regn(hw, EVSTAT);
	k = ALLOC_COMPL_TIMEOUT;
	while ( (! (reg & HERMES_EV_ALLOC)) && k) {
		k--;
		udelay(10);
		reg = hermes_read_regn(hw, EVSTAT);
	}
	
	if (! hermes_present(hw)) {
		printk(KERN_WARNING "hermes @ 0x%x: Card removed waiting for frame allocation.\n",
		       hw->iobase);
		return -ENODEV;
	}
		
	if (! (reg & HERMES_EV_ALLOC)) {
		printk(KERN_ERR "hermes @ 0x%x: Timeout waiting for frame allocation\n",
		       hw->iobase);
		return -ETIMEDOUT;
	}

	*fid = hermes_read_regn(hw, ALLOCFID);
	hermes_write_regn(hw, EVACK, HERMES_EV_ALLOC);
	
	return 0;
}

/* Set up a BAP to read a particular chunk of data from card's internal buffer.
 *
 * Returns: < 0 on internal failure (errno), 0 on success, >0 on error
 * from firmware
 *
 * Callable from any context */
static int hermes_bap_seek(hermes_t *hw, int bap, uint16_t id, uint16_t offset)
{
	int sreg = bap ? HERMES_SELECT1 : HERMES_SELECT0;
	int oreg = bap ? HERMES_OFFSET1 : HERMES_OFFSET0;
	int k;
	int l = BAP_ERROR_RETRY;
	uint16_t reg;

	/* Paranoia.. */
	if ( (offset > HERMES_BAP_OFFSET_MAX) || (offset % 2) )
		return -EINVAL;

	k = BAP_BUSY_TIMEOUT;
	reg = hermes_read_reg(hw, oreg);

	if (reg & HERMES_OFFSET_BUSY)
		return -EBUSY;

	/* Now we actually set up the transfer */
 retry:
	hermes_write_reg(hw, sreg, id);
	hermes_write_reg(hw, oreg, offset);

	/* Wait for the BAP to be ready */
	k = BAP_BUSY_TIMEOUT;
	reg = hermes_read_reg(hw, oreg);
	while ( (reg & (HERMES_OFFSET_BUSY | HERMES_OFFSET_ERR)) && k) {
		k--;
		udelay(1);
		reg = hermes_read_reg(hw, oreg);
	}

	if (reg & HERMES_OFFSET_BUSY)
		return -ETIMEDOUT;

	/* For some reason, seeking the BAP seems to randomly fail somewhere
	   (firmware bug?). We retry a few times before giving up. */
	if (reg & HERMES_OFFSET_ERR) {
		if (l--) {
			udelay(1);
			goto retry;
		} else
			return -EIO;
	}

	return 0;
}

/* Read a block of data from the chip's buffer, via the
 * BAP. Synchronization/serialization is the caller's problem.  len
 * must be even.
 *
 * Returns: < 0 on internal failure (errno), 0 on success, > 0 on error from firmware
 */
int hermes_bap_pread(hermes_t *hw, int bap, void *buf, uint16_t len,
		     uint16_t id, uint16_t offset)
{
	int dreg = bap ? HERMES_DATA1 : HERMES_DATA0;
	int err = 0;

	if (len % 2)
		return -EINVAL;

	err = hermes_bap_seek(hw, bap, id, offset);
	if (err)
		goto out;

	/* Actually do the transfer */
	hermes_read_data(hw, dreg, buf, len/2);

 out:
	return err;
}

/* Write a block of data to the chip's buffer, via the
 * BAP. Synchronization/serialization is the caller's problem. len
 * must be even.
 *
 * Returns: < 0 on internal failure (errno), 0 on success, > 0 on error from firmware
 */
int hermes_bap_pwrite(hermes_t *hw, int bap, const void *buf, uint16_t len,
		      uint16_t id, uint16_t offset)
{
	int dreg = bap ? HERMES_DATA1 : HERMES_DATA0;
	int err = 0;

	if (len % 2)
		return -EINVAL;

	err = hermes_bap_seek(hw, bap, id, offset);
	if (err)
		goto out;
	
	/* Actually do the transfer */
	hermes_write_data(hw, dreg, buf, len/2);

 out:	
	return err;
}

/* Read a Length-Type-Value record from the card.
 *
 * If length is NULL, we ignore the length read from the card, and
 * read the entire buffer regardless. This is useful because some of
 * the configuration records appear to have incorrect lengths in
 * practice.
 *
 * Callable from user or bh context.  */
int hermes_read_ltv(hermes_t *hw, int bap, uint16_t rid, int buflen,
		    uint16_t *length, void *buf)
{
	int err = 0;
	int dreg = bap ? HERMES_DATA1 : HERMES_DATA0;
	uint16_t rlength, rtype;
	hermes_response_t resp;
	int count;

	if (buflen % 2)
		return -EINVAL;

	err = hermes_docmd_wait(hw, HERMES_CMD_ACCESS, rid, &resp);
	if (err)
		goto out;

	err = hermes_bap_seek(hw, bap, rid, 0);
	if (err)
		goto out;

	rlength = hermes_read_reg(hw, dreg);
	rtype = hermes_read_reg(hw, dreg);

	if (length)
		*length = rlength;

	if (rtype != rid)
		printk(KERN_WARNING "hermes_read_ltv(): rid  (0x%04x) does "
		       "not match type (0x%04x)\n", rid, rtype);
	if (HERMES_RECLEN_TO_BYTES(rlength) > buflen)
		printk(KERN_WARNING "hermes @ 0x%x: Truncating LTV record from %d to %d bytes. "
		       "(rid=0x%04x, len=0x%04x)\n", hw->iobase,
		       HERMES_RECLEN_TO_BYTES(rlength), buflen, rid, rlength);
	
	/* For now we always read the whole buffer, the
	   lengths in the records seem to be wrong, frequently */
	count = buflen / 2;

#if 0
	if (length)
		count = (MIN(buflen, rlength) + 1) / 2;
	else {
		count = buflen / 2;
		if (rlength != buflen)
			printk(KERN_WARNING "hermes_read_ltv(): Incorrect \
record length %d instead of %d on RID 0x%04x\n", rlength, buflen, rid);
	}
#endif
	hermes_read_data(hw, dreg, buf, count);

 out:
	return err;
}

int hermes_write_ltv(hermes_t *hw, int bap, uint16_t rid, 
		     uint16_t length, const void *value)
{
	int dreg = bap ? HERMES_DATA1 : HERMES_DATA0;
	int err = 0;
	hermes_response_t resp;
	int count;
	
	DEBUG(3, "write_ltv(): bap=%d rid=0x%04x length=%d (value=0x%04x)\n",
	      bap, rid, length, * ((uint16_t *)value));

	err = hermes_bap_seek(hw, bap, rid, 0);
	if (err)
		goto out;

	hermes_write_reg(hw, dreg, length);
	hermes_write_reg(hw, dreg, rid);

	count = length - 1;

	hermes_write_data(hw, dreg, value, count);

	err = hermes_docmd_wait(hw, HERMES_CMD_ACCESS | HERMES_CMD_WRITE, 
				rid, &resp);

 out:
	return err;
}

EXPORT_SYMBOL(hermes_struct_init);
EXPORT_SYMBOL(hermes_reset);
EXPORT_SYMBOL(hermes_docmd_wait);
EXPORT_SYMBOL(hermes_allocate);

EXPORT_SYMBOL(hermes_bap_pread);
EXPORT_SYMBOL(hermes_bap_pwrite);
EXPORT_SYMBOL(hermes_read_ltv);
EXPORT_SYMBOL(hermes_write_ltv);

static int __init init_hermes(void)
{
	printk(KERN_INFO "%s\n", version);

	return 0;
}

module_init(init_hermes);
