/*
 *  
 *  Copyright (C) 2002 Intersil Americas Inc.
 *  Copyright (C) 2003-2004 Luis R. Rodriguez <mcgrof@ruslug.rutgers.edu>_
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define __KERNEL_SYSCALLS__

#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>

#include "isl_38xx.h"
#include <linux/firmware.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/config.h>
#if !defined(CONFIG_FW_LOADER) && !defined(CONFIG_FW_LOADER_MODULE)
#error No Firmware Loading configured in the kernel !
#endif

#include "islpci_dev.h"
#include "islpci_mgt.h"

/******************************************************************************
    Device Interface & Control functions
******************************************************************************/

/**
 * isl38xx_disable_interrupts - disable all interrupts
 * @device: pci memory base address
 *
 *  Instructs the device to disable all interrupt reporting by asserting 
 *  the IRQ line. New events may still show up in the interrupt identification
 *  register located at offset %ISL38XX_INT_IDENT_REG.
 */
void
isl38xx_disable_interrupts(void *device)
{
	isl38xx_w32_flush(device, 0x00000000, ISL38XX_INT_EN_REG);
	udelay(ISL38XX_WRITEIO_DELAY);
}

void
isl38xx_handle_sleep_request(isl38xx_control_block *control_block,
			     int *powerstate, void *device_base)
{
	/* device requests to go into sleep mode
	 * check whether the transmit queues for data and management are empty */
	if (isl38xx_in_queue(control_block, ISL38XX_CB_TX_DATA_LQ))
		/* data tx queue not empty */
		return;

	if (isl38xx_in_queue(control_block, ISL38XX_CB_TX_MGMTQ))
		/* management tx queue not empty */
		return;

	/* check also whether received frames are pending */
	if (isl38xx_in_queue(control_block, ISL38XX_CB_RX_DATA_LQ))
		/* data rx queue not empty */
		return;

	if (isl38xx_in_queue(control_block, ISL38XX_CB_RX_MGMTQ))
		/* management rx queue not empty */
		return;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_TRACING, "Device going to sleep mode\n");
#endif

	/* all queues are empty, allow the device to go into sleep mode */
	*powerstate = ISL38XX_PSM_POWERSAVE_STATE;

	/* assert the Sleep interrupt in the Device Interrupt Register */
	isl38xx_w32_flush(device_base, ISL38XX_DEV_INT_SLEEP,
			  ISL38XX_DEV_INT_REG);
	udelay(ISL38XX_WRITEIO_DELAY);
}

void
isl38xx_handle_wakeup(isl38xx_control_block *control_block,
		      int *powerstate, void *device_base)
{
	/* device is in active state, update the powerstate flag */
	*powerstate = ISL38XX_PSM_ACTIVE_STATE;

	/* now check whether there are frames pending for the card */
	if (!isl38xx_in_queue(control_block, ISL38XX_CB_TX_DATA_LQ)
	    && !isl38xx_in_queue(control_block, ISL38XX_CB_TX_MGMTQ))
		return;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_ANYTHING, "Wake up handler trigger the device\n");
#endif

	/* either data or management transmit queue has a frame pending
	 * trigger the device by setting the Update bit in the Device Int reg */
	isl38xx_w32_flush(device_base, ISL38XX_DEV_INT_UPDATE,
			  ISL38XX_DEV_INT_REG);
	udelay(ISL38XX_WRITEIO_DELAY);
}

void
isl38xx_trigger_device(int asleep, void *device_base)
{
	struct timeval current_time;
	u32 reg, counter = 0;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_FUNCTION_CALLS, "isl38xx trigger device\n");
#endif

	/* check whether the device is in power save mode */
	if (asleep) {
		/* device is in powersave, trigger the device for wakeup */
#if VERBOSE > SHOW_ERROR_MESSAGES
		do_gettimeofday(&current_time);
		DEBUG(SHOW_TRACING, "%08li.%08li Device wakeup triggered\n",
		      current_time.tv_sec, current_time.tv_usec);
#endif

		DEBUG(SHOW_TRACING, "%08li.%08li Device register read %08x\n",
		      current_time.tv_sec, current_time.tv_usec,
		      readl(device_base + ISL38XX_CTRL_STAT_REG));
		udelay(ISL38XX_WRITEIO_DELAY);

		if (reg = readl(device_base + ISL38XX_INT_IDENT_REG),
		    reg == 0xabadface) {
#if VERBOSE > SHOW_ERROR_MESSAGES
			do_gettimeofday(&current_time);
			DEBUG(SHOW_TRACING,
			      "%08li.%08li Device register abadface\n",
			      current_time.tv_sec, current_time.tv_usec);
#endif
			/* read the Device Status Register until Sleepmode bit is set */
			while (reg = readl(device_base + ISL38XX_CTRL_STAT_REG),
			       (reg & ISL38XX_CTRL_STAT_SLEEPMODE) == 0) {
				udelay(ISL38XX_WRITEIO_DELAY);
				counter++;
			}

			DEBUG(SHOW_TRACING,
			      "%08li.%08li Device register read %08x\n",
			      current_time.tv_sec, current_time.tv_usec,
			      readl(device_base + ISL38XX_CTRL_STAT_REG));
			udelay(ISL38XX_WRITEIO_DELAY);

#if VERBOSE > SHOW_ERROR_MESSAGES
			do_gettimeofday(&current_time);
			DEBUG(SHOW_TRACING,
			      "%08li.%08li Device asleep counter %i\n",
			      current_time.tv_sec, current_time.tv_usec,
			      counter);
#endif
		}
		/* assert the Wakeup interrupt in the Device Interrupt Register */
		isl38xx_w32_flush(device_base, ISL38XX_DEV_INT_WAKEUP,
				  ISL38XX_DEV_INT_REG);
		udelay(ISL38XX_WRITEIO_DELAY);

		/* perform another read on the Device Status Register */
		reg = readl(device_base + ISL38XX_CTRL_STAT_REG);
		udelay(ISL38XX_WRITEIO_DELAY);

#if VERBOSE > SHOW_ERROR_MESSAGES
		do_gettimeofday(&current_time);
		DEBUG(SHOW_TRACING, "%08li.%08li Device register read %08x\n",
		      current_time.tv_sec, current_time.tv_usec, reg);
#endif
	} else {
		/* device is (still) awake  */
#if VERBOSE > SHOW_ERROR_MESSAGES
		DEBUG(SHOW_TRACING, "Device is in active state\n");
#endif
		/* trigger the device by setting the Update bit in the Device Int reg */

		isl38xx_w32_flush(device_base, ISL38XX_DEV_INT_UPDATE,
				  ISL38XX_DEV_INT_REG);
		udelay(ISL38XX_WRITEIO_DELAY);
	}
}

void
isl38xx_interface_reset(void *device_base, dma_addr_t host_address)
{
	u32 reg;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_FUNCTION_CALLS, "isl38xx_interface_reset \n");
#endif

	/* load the address of the control block in the device */
	isl38xx_w32_flush(device_base, host_address, ISL38XX_CTRL_BLK_BASE_REG);
	udelay(ISL38XX_WRITEIO_DELAY);

	/* set the reset bit in the Device Interrupt Register */
	isl38xx_w32_flush(device_base, ISL38XX_DEV_INT_RESET,
			  ISL38XX_DEV_INT_REG);
	udelay(ISL38XX_WRITEIO_DELAY);

	/* enable the interrupt for detecting initialization */

	/* Note: Do not enable other interrupts here. We want the
	 * device to have come up first 100% before allowing any other 
	 * interrupts. */
	reg = ISL38XX_INT_IDENT_INIT;

	isl38xx_w32_flush(device_base, reg, ISL38XX_INT_EN_REG);
	udelay(ISL38XX_WRITEIO_DELAY);  /* allow complete full reset */
}

void
isl38xx_enable_common_interrupts(void *device_base) {
	u32 reg;
	reg = ( ISL38XX_INT_IDENT_UPDATE | 
			ISL38XX_INT_IDENT_SLEEP | ISL38XX_INT_IDENT_WAKEUP);
	isl38xx_w32_flush(device_base, reg, ISL38XX_INT_EN_REG);
	udelay(ISL38XX_WRITEIO_DELAY);
}

int
isl38xx_upload_firmware(char *fw_id, _REQ_FW_DEV_T dev, void *device_base,
			dma_addr_t host_address)
{
	u32 reg, rc;

#if VERBOSE > SHOW_ERROR_MESSAGES
	DEBUG(SHOW_ERROR_MESSAGES, "isl38xx_upload_firmware(0x%lx, 0x%lx)\n",
	      (long) device_base, (long) host_address);
#endif

	/* clear the RAMBoot and the Reset bit */
	reg = readl(device_base + ISL38XX_CTRL_STAT_REG);
	reg &= ~ISL38XX_CTRL_STAT_RESET;
	reg &= ~ISL38XX_CTRL_STAT_RAMBOOT;
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	wmb();
	udelay(ISL38XX_WRITEIO_DELAY);

	/* set the Reset bit without reading the register ! */
	reg |= ISL38XX_CTRL_STAT_RESET;
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	wmb();
	udelay(ISL38XX_WRITEIO_DELAY);

	/* clear the Reset bit */
	reg &= ~ISL38XX_CTRL_STAT_RESET;
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	wmb();

	/* wait a while for the device to reboot */
	mdelay(50);

	{
		const struct firmware *fw_entry = 0;
		long fw_len;
		const u32 *fw_ptr;

		rc = request_firmware(&fw_entry, fw_id, dev);
		if (rc) {
			printk(KERN_ERR
			       "%s: request_firmware() failed for '%s'\n",
			       "prism54", fw_id);
			return rc;
		}
		/* prepare the Direct Memory Base register */
		reg = ISL38XX_DEV_FIRMWARE_ADDRES;

		fw_ptr = (u32 *) fw_entry->data;
		fw_len = fw_entry->size;

		if (fw_len % 4) {
			printk(KERN_ERR
			       "%s: firmware '%s' size is not multiple of 32bit, aborting!\n",
			       "prism54", fw_id);
			release_firmware(fw_entry);
			return EILSEQ; /* Illegal byte sequence  */;
		}

		while (fw_len > 0) {
			long _fw_len =
			    (fw_len >
			     ISL38XX_MEMORY_WINDOW_SIZE) ?
			    ISL38XX_MEMORY_WINDOW_SIZE : fw_len;
			u32 *dev_fw_ptr = device_base + ISL38XX_DIRECT_MEM_WIN;

			/* set the cards base address for writting the data */
			isl38xx_w32_flush(device_base, reg,
					  ISL38XX_DIR_MEM_BASE_REG);
			wmb();	/* be paranoid */

			/* increment the write address for next iteration */
			reg += _fw_len;
			fw_len -= _fw_len;

			/* write the data to the Direct Memory Window 32bit-wise */
			/* memcpy_toio() doesn't guarantee 32bit writes :-| */
			while (_fw_len > 0) {
				/* use non-swapping writel() */
				__raw_writel(*fw_ptr, dev_fw_ptr);
				fw_ptr++, dev_fw_ptr++;
				_fw_len -= 4;
			}

			/* flush PCI posting */
			(void) readl(device_base + ISL38XX_PCI_POSTING_FLUSH);
			wmb();	/* be paranoid again */

			BUG_ON(_fw_len != 0);
		}

		BUG_ON(fw_len != 0);

		release_firmware(fw_entry);
	}

	/* now reset the device
	 * clear the Reset & ClkRun bit, set the RAMBoot bit */
	reg = readl(device_base + ISL38XX_CTRL_STAT_REG);
	reg &= ~ISL38XX_CTRL_STAT_CLKRUN;
	reg &= ~ISL38XX_CTRL_STAT_RESET;
	reg |= ISL38XX_CTRL_STAT_RAMBOOT;
	isl38xx_w32_flush(device_base, reg, ISL38XX_CTRL_STAT_REG);
	wmb();
	udelay(ISL38XX_WRITEIO_DELAY);

	/* set the reset bit latches the host override and RAMBoot bits
	 * into the device for operation when the reset bit is reset */
	reg |= ISL38XX_CTRL_STAT_RESET;
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	/* don't do flush PCI posting here! */
	wmb();
	udelay(ISL38XX_WRITEIO_DELAY);

	/* clear the reset bit should start the whole circus */
	reg &= ~ISL38XX_CTRL_STAT_RESET;
	writel(reg, device_base + ISL38XX_CTRL_STAT_REG);
	/* don't do flush PCI posting here! */
	wmb();
	udelay(ISL38XX_WRITEIO_DELAY);

	return 0;
}

int
isl38xx_in_queue(isl38xx_control_block *cb, int queue)
{
	const s32 delta = (le32_to_cpu(cb->driver_curr_frag[queue]) -
			   le32_to_cpu(cb->device_curr_frag[queue]));

	/* determine the amount of fragments in the queue depending on the type
	 * of the queue, either transmit or receive */

	BUG_ON(delta < 0);	/* driver ptr must be ahead of device ptr */

	switch (queue) {
		/* send queues */
	case ISL38XX_CB_TX_MGMTQ:
		BUG_ON(delta > ISL38XX_CB_MGMT_QSIZE);
	case ISL38XX_CB_TX_DATA_LQ:
	case ISL38XX_CB_TX_DATA_HQ:
		BUG_ON(delta > ISL38XX_CB_TX_QSIZE);
		return delta;
		break;

		/* receive queues */
	case ISL38XX_CB_RX_MGMTQ:
		BUG_ON(delta > ISL38XX_CB_MGMT_QSIZE);
		return ISL38XX_CB_MGMT_QSIZE - delta;
		break;

	case ISL38XX_CB_RX_DATA_LQ:
	case ISL38XX_CB_RX_DATA_HQ:
		BUG_ON(delta > ISL38XX_CB_RX_QSIZE);
		return ISL38XX_CB_RX_QSIZE - delta;
		break;
	}
	BUG();
	return 0;
}
