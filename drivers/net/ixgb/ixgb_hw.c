/*******************************************************************************

  
  Copyright(c) 1999 - 2003 Intel Corporation. All rights reserved.
  
  This program is free software; you can redistribute it and/or modify it 
  under the terms of the GNU General Public License as published by the Free 
  Software Foundation; either version 2 of the License, or (at your option) 
  any later version.
  
  This program is distributed in the hope that it will be useful, but WITHOUT 
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
  more details.
  
  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59 
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
  
  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
  
  Contact Information:
  Linux NICS <linux.nics@intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
*******************************************************************************/

/* ixgb_hw.c
 * Shared functions for accessing and configuring the adapter
 */

#include "ixgb_hw.h"
#include "ixgb_ids.h"

/*  Local function prototypes */

static u32 ixgb_hash_mc_addr(struct ixgb_hw *hw, u8 * mc_addr);

static void ixgb_mta_set(struct ixgb_hw *hw, u32 hash_value);

static void ixgb_get_bus_info(struct ixgb_hw *hw);

boolean_t mac_addr_valid(u8 * mac_addr);

static boolean_t ixgb_link_reset(struct ixgb_hw *hw);

static void ixgb_optics_reset(struct ixgb_hw *hw);

u32 ixgb_mac_reset(struct ixgb_hw *hw);

u32
ixgb_mac_reset(struct ixgb_hw *hw)
{
	u32 ctrl_reg;

	/* Setup up hardware to known state with RESET.  */
	ctrl_reg = IXGB_CTRL0_RST | IXGB_CTRL0_SDP3_DIR |	/* All pins are Output=1 */
	    IXGB_CTRL0_SDP2_DIR | IXGB_CTRL0_SDP1_DIR | IXGB_CTRL0_SDP0_DIR | IXGB_CTRL0_SDP3 |	/* Initial value 1101   */
	    IXGB_CTRL0_SDP2 | IXGB_CTRL0_SDP0;
#ifdef HP_ZX1
	outl(IXGB_CTRL0, hw->io_base);
	outl(ctrl_reg, hw->io_base + 4);
#else
	IXGB_WRITE_REG(hw, CTRL0, ctrl_reg);
#endif

	/* Delay a few ms just to allow the reset to complete */
	msec_delay(IXGB_DELAY_AFTER_RESET);
	ctrl_reg = IXGB_READ_REG(hw, CTRL0);
#if DBG
	/* Make sure the self-clearing global reset bit did self clear */
	ASSERT(!(ctrl_reg & IXGB_CTRL0_RST));
#endif

	ixgb_optics_reset(hw);
	return ctrl_reg;
}

/******************************************************************************
 * Reset the transmit and receive units; mask and clear all interrupts.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
boolean_t
ixgb_adapter_stop(struct ixgb_hw * hw)
{
	u32 ctrl_reg;
	u32 icr_reg;

	DEBUGFUNC("ixgb_adapter_stop");

	/* If we are stopped or resetting exit gracefully and wait to be
	 * started again before accessing the hardware.
	 */
	if (hw->adapter_stopped) {
		DEBUGOUT("Exiting because the adapter is already stopped!!!\n");
		return FALSE;
	}

	/* Set the Adapter Stopped flag so other driver functions stop
	 * touching the Hardware.
	 */
	hw->adapter_stopped = TRUE;

	/* Clear interrupt mask to stop board from generating interrupts */
	DEBUGOUT("Masking off all interrupts\n");
	IXGB_WRITE_REG(hw, IMC, 0xFFFFFFFF);

	/* Disable the Transmit and Receive units.  Then delay to allow
	 * any pending transactions to complete before we hit the MAC with
	 * the global reset.
	 */
	IXGB_WRITE_REG(hw, RCTL, IXGB_READ_REG(hw, RCTL) & ~IXGB_RCTL_RXEN);
	IXGB_WRITE_REG(hw, TCTL, IXGB_READ_REG(hw, TCTL) & ~IXGB_TCTL_TXEN);
	msec_delay(IXGB_DELAY_BEFORE_RESET);

	/* Issue a global reset to the MAC.  This will reset the chip's
	 * transmit, receive, DMA, and link units.  It will not effect
	 * the current PCI configuration.  The global reset bit is self-
	 * clearing, and should clear within a microsecond.
	 */
	DEBUGOUT("Issuing a global reset to MAC\n");

	ctrl_reg = ixgb_mac_reset(hw);

	/* Clear interrupt mask to stop board from generating interrupts */
	DEBUGOUT("Masking off all interrupts\n");
	IXGB_WRITE_REG(hw, IMC, 0xffffffff);

	/* Clear any pending interrupt events. */
	icr_reg = IXGB_READ_REG(hw, ICR);

	return (ctrl_reg & IXGB_CTRL0_RST);
}

/******************************************************************************
 * Performs basic configuration of the adapter.
 *
 * hw - Struct containing variables accessed by shared code
 * 
 * Resets the controller.  
 * Reads and validates the EEPROM.
 * Initializes the receive address registers.
 * Initializes the multicast table.
 * Clears all on-chip counters. 
 * Calls routine to setup flow control settings. 
 * Leaves the transmit and receive units disabled and uninitialized.
 * 
 * Returns:
 *      TRUE if successful, 
 *      FALSE if unrecoverable problems were encountered.
 *****************************************************************************/
boolean_t
ixgb_init_hw(struct ixgb_hw * hw)
{
	u32 i;
	u32 ctrl_reg;
	boolean_t status;

	DEBUGFUNC("ixgb_init_hw");

	/* Issue a global reset to the MAC.  This will reset the chip's
	 * transmit, receive, DMA, and link units.  It will not effect
	 * the current PCI configuration.  The global reset bit is self-
	 * clearing, and should clear within a microsecond.
	 */
	DEBUGOUT("Issuing a global reset to MAC\n");

	ctrl_reg = ixgb_mac_reset(hw);

	DEBUGOUT("Issuing an EE reset to MAC\n");
#ifdef HP_ZX1
	outl(IXGB_CTRL1, hw->io_base);
	outl(IXGB_CTRL1_EE_RST, hw->io_base + 4);
#else
	IXGB_WRITE_REG(hw, CTRL1, IXGB_CTRL1_EE_RST);
#endif

	/* Delay a few ms just to allow the reset to complete */
	msec_delay(IXGB_DELAY_AFTER_EE_RESET);

	if (ixgb_get_eeprom_data(hw) == FALSE) {
		return (FALSE);
	}

	/* Setup the receive addresses. 
	 * Receive Address Registers (RARs 0 - 15).
	 */
	ixgb_init_rx_addrs(hw);

	/* 
	 * Check that a valid MAC address has been set.
	 * If it is not valid, we fail hardware init.
	 */
	if (!mac_addr_valid(hw->curr_mac_addr)) {
		DEBUGOUT("MAC address invalid after ixgb_init_rx_addrs\n");
		return (FALSE);
	}

	/* tell the routines in this file they can access hardware again */
	hw->adapter_stopped = FALSE;

	/* Fill in the bus_info structure */
	ixgb_get_bus_info(hw);

	/* Zero out the Multicast HASH table */
	DEBUGOUT("Zeroing the MTA\n");
	for (i = 0; i < IXGB_MC_TBL_SIZE; i++)
		IXGB_WRITE_REG_ARRAY(hw, MTA, i, 0);

	/* Zero out the VLAN Filter Table Array */
	ixgb_clear_vfta(hw);

	/* Zero all of the hardware counters */
	ixgb_clear_hw_cntrs(hw);

	/* Call a subroutine to setup flow control. */
	status = ixgb_setup_fc(hw);

	/* check-for-link in case lane deskew is locked */
	ixgb_check_for_link(hw);

	return (status);
}

/******************************************************************************
 * Initializes receive address filters.
 *
 * hw - Struct containing variables accessed by shared code 
 *
 * Places the MAC address in receive address register 0 and clears the rest
 * of the receive addresss registers. Clears the multicast table. Assumes
 * the receiver is in reset when the routine is called.
 *****************************************************************************/
void
ixgb_init_rx_addrs(struct ixgb_hw *hw)
{
	u32 i;

	DEBUGFUNC("ixgb_init_rx_addrs");

	/* 
	 * If the current mac address is valid, assume it is a software override
	 * to the permanent address.
	 * Otherwise, use the permanent address from the eeprom.
	 */
	if (!mac_addr_valid(hw->curr_mac_addr)) {

		/* Get the MAC address from the eeprom for later reference */
		ixgb_get_ee_mac_addr(hw, hw->curr_mac_addr);

		DEBUGOUT3(" Keeping Permanent MAC Addr =%.2X %.2X %.2X ",
			  hw->curr_mac_addr[0],
			  hw->curr_mac_addr[1], hw->curr_mac_addr[2]);
		DEBUGOUT3("%.2X %.2X %.2X\n",
			  hw->curr_mac_addr[3],
			  hw->curr_mac_addr[4], hw->curr_mac_addr[5]);
	} else {

		/* Setup the receive address. */
		DEBUGOUT("Overriding MAC Address in RAR[0]\n");
		DEBUGOUT3(" New MAC Addr =%.2X %.2X %.2X ",
			  hw->curr_mac_addr[0],
			  hw->curr_mac_addr[1], hw->curr_mac_addr[2]);
		DEBUGOUT3("%.2X %.2X %.2X\n",
			  hw->curr_mac_addr[3],
			  hw->curr_mac_addr[4], hw->curr_mac_addr[5]);

		ixgb_rar_set(hw, hw->curr_mac_addr, 0);
	}

	/* Zero out the other 15 receive addresses. */
	DEBUGOUT("Clearing RAR[1-15]\n");
	for (i = 1; i < IXGB_RAR_ENTRIES; i++) {
		IXGB_WRITE_REG_ARRAY(hw, RA, (i << 1), 0);
		IXGB_WRITE_REG_ARRAY(hw, RA, ((i << 1) + 1), 0);
	}

	return;
}

/******************************************************************************
 * Updates the MAC's list of multicast addresses.
 *
 * hw - Struct containing variables accessed by shared code
 * mc_addr_list - the list of new multicast addresses
 * mc_addr_count - number of addresses
 * pad - number of bytes between addresses in the list
 *
 * The given list replaces any existing list. Clears the last 15 receive
 * address registers and the multicast table. Uses receive address registers
 * for the first 15 multicast addresses, and hashes the rest into the 
 * multicast table.
 *****************************************************************************/
void
ixgb_mc_addr_list_update(struct ixgb_hw *hw,
			 u8 * mc_addr_list,
			 u32 mc_addr_count, u32 pad)
{
	u32 hash_value;
	u32 i;
	u32 rar_used_count = 1;	/* RAR[0] is used for our MAC address */

	DEBUGFUNC("ixgb_mc_addr_list_update");

	/* Set the new number of MC addresses that we are being requested to use. */
	hw->num_mc_addrs = mc_addr_count;

	/* Clear RAR[1-15] */
	DEBUGOUT(" Clearing RAR[1-15]\n");
	for (i = rar_used_count; i < IXGB_RAR_ENTRIES; i++) {
		IXGB_WRITE_REG_ARRAY(hw, RA, (i << 1), 0);
		IXGB_WRITE_REG_ARRAY(hw, RA, ((i << 1) + 1), 0);
	}

	/* Clear the MTA */
	DEBUGOUT(" Clearing MTA\n");
	for (i = 0; i < IXGB_MC_TBL_SIZE; i++) {
		IXGB_WRITE_REG_ARRAY(hw, MTA, i, 0);
	}

	/* Add the new addresses */
	for (i = 0; i < mc_addr_count; i++) {
		DEBUGOUT(" Adding the multicast addresses:\n");
		DEBUGOUT7(" MC Addr #%d =%.2X %.2X %.2X %.2X %.2X %.2X\n", i,
			  mc_addr_list[i * (IXGB_ETH_LENGTH_OF_ADDRESS + pad)],
			  mc_addr_list[i * (IXGB_ETH_LENGTH_OF_ADDRESS + pad) +
				       1],
			  mc_addr_list[i * (IXGB_ETH_LENGTH_OF_ADDRESS + pad) +
				       2],
			  mc_addr_list[i * (IXGB_ETH_LENGTH_OF_ADDRESS + pad) +
				       3],
			  mc_addr_list[i * (IXGB_ETH_LENGTH_OF_ADDRESS + pad) +
				       4],
			  mc_addr_list[i * (IXGB_ETH_LENGTH_OF_ADDRESS + pad) +
				       5]);

		/* Place this multicast address in the RAR if there is room, *
		 * else put it in the MTA            
		 */
		if (rar_used_count < IXGB_RAR_ENTRIES) {
			ixgb_rar_set(hw,
				     mc_addr_list +
				     (i * (IXGB_ETH_LENGTH_OF_ADDRESS + pad)),
				     rar_used_count);
			DEBUGOUT1("Added a multicast address to RAR[%d]\n", i);
			rar_used_count++;
		} else {
			hash_value = ixgb_hash_mc_addr(hw,
						       mc_addr_list +
						       (i *
							(IXGB_ETH_LENGTH_OF_ADDRESS
							 + pad)));

			DEBUGOUT1(" Hash value = 0x%03X\n", hash_value);

			ixgb_mta_set(hw, hash_value);
		}
	}

	DEBUGOUT("MC Update Complete\n");
	return;
}

/******************************************************************************
 * Hashes an address to determine its location in the multicast table
 *
 * hw - Struct containing variables accessed by shared code
 * mc_addr - the multicast address to hash 
 *
 * Returns:
 *      The hash value
 *****************************************************************************/
static u32
ixgb_hash_mc_addr(struct ixgb_hw *hw, u8 * mc_addr)
{
	u32 hash_value = 0;

	DEBUGFUNC("ixgb_hash_mc_addr");

	/* The portion of the address that is used for the hash table is
	 * determined by the mc_filter_type setting.  
	 */
	switch (hw->mc_filter_type) {
		/* [0] [1] [2] [3] [4] [5]
		 * 01  AA  00  12  34  56
		 * LSB                 MSB - According to H/W docs */
	case 0:
		/* [47:36] i.e. 0x563 for above example address */
		hash_value =
		    ((mc_addr[4] >> 4) | (((u16) mc_addr[5]) << 4));
		break;
	case 1:		/* [46:35] i.e. 0xAC6 for above example address */
		hash_value =
		    ((mc_addr[4] >> 3) | (((u16) mc_addr[5]) << 5));
		break;
	case 2:		/* [45:34] i.e. 0x5D8 for above example address */
		hash_value =
		    ((mc_addr[4] >> 2) | (((u16) mc_addr[5]) << 6));
		break;
	case 3:		/* [43:32] i.e. 0x634 for above example address */
		hash_value = ((mc_addr[4]) | (((u16) mc_addr[5]) << 8));
		break;
	default:
		/* Invalid mc_filter_type, what should we do? */
		DEBUGOUT("MC filter type param set incorrectly\n");
		ASSERT(0);
		break;
	}

	hash_value &= 0xFFF;
	return (hash_value);
}

/******************************************************************************
 * Sets the bit in the multicast table corresponding to the hash value.
 *
 * hw - Struct containing variables accessed by shared code
 * hash_value - Multicast address hash value
 *****************************************************************************/
static void
ixgb_mta_set(struct ixgb_hw *hw, u32 hash_value)
{
	u32 hash_bit, hash_reg;
	u32 mta_reg;

	/* The MTA is a register array of 128 32-bit registers.  
	 * It is treated like an array of 4096 bits.  We want to set 
	 * bit BitArray[hash_value]. So we figure out what register
	 * the bit is in, read it, OR in the new bit, then write
	 * back the new value.  The register is determined by the 
	 * upper 7 bits of the hash value and the bit within that 
	 * register are determined by the lower 5 bits of the value.
	 */
	hash_reg = (hash_value >> 5) & 0x7F;
	hash_bit = hash_value & 0x1F;

	mta_reg = IXGB_READ_REG_ARRAY(hw, MTA, hash_reg);

	mta_reg |= (1 << hash_bit);

	IXGB_WRITE_REG_ARRAY(hw, MTA, hash_reg, mta_reg);

	return;
}

/******************************************************************************
 * Puts an ethernet address into a receive address register.
 *
 * hw - Struct containing variables accessed by shared code
 * addr - Address to put into receive address register
 * index - Receive address register to write
 *****************************************************************************/
void
ixgb_rar_set(struct ixgb_hw *hw, u8 * addr, u32 index)
{
	u32 rar_low, rar_high;

	DEBUGFUNC("ixgb_rar_set");

	/* HW expects these in little endian so we reverse the byte order
	 * from network order (big endian) to little endian              
	 */
	rar_low = ((u32) addr[0] |
		   ((u32) addr[1] << 8) |
		   ((u32) addr[2] << 16) | ((u32) addr[3] << 24));

	rar_high = ((u32) addr[4] |
		    ((u32) addr[5] << 8) | IXGB_RAH_AV);

	IXGB_WRITE_REG_ARRAY(hw, RA, (index << 1), rar_low);
	IXGB_WRITE_REG_ARRAY(hw, RA, ((index << 1) + 1), rar_high);
	return;
}

/******************************************************************************
 * Writes a value to the specified offset in the VLAN filter table.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - Offset in VLAN filer table to write
 * value - Value to write into VLAN filter table
 *****************************************************************************/
void
ixgb_write_vfta(struct ixgb_hw *hw, u32 offset, u32 value)
{
	IXGB_WRITE_REG_ARRAY(hw, VFTA, offset, value);
	return;
}

/******************************************************************************
 * Clears the VLAN filer table
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
void
ixgb_clear_vfta(struct ixgb_hw *hw)
{
	u32 offset;

	for (offset = 0; offset < IXGB_VLAN_FILTER_TBL_SIZE; offset++)
		IXGB_WRITE_REG_ARRAY(hw, VFTA, offset, 0);
	return;
}

/******************************************************************************
 * Configures the flow control settings based on SW configuration.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/

boolean_t
ixgb_setup_fc(struct ixgb_hw * hw)
{
	u32 ctrl_reg;
	u32 pap_reg = 0;	/* by default, assume no pause time */
	boolean_t status = TRUE;

	DEBUGFUNC("ixgb_setup_fc");

	/* Get the current control reg 0 settings */
	ctrl_reg = IXGB_READ_REG(hw, CTRL0);

	/* Clear the Receive Pause Enable and Transmit Pause Enable bits */
	ctrl_reg &= ~(IXGB_CTRL0_RPE | IXGB_CTRL0_TPE);

	/* The possible values of the "flow_control" parameter are:
	 *      0:  Flow control is completely disabled
	 *      1:  Rx flow control is enabled (we can receive pause frames
	 *          but not send pause frames).
	 *      2:  Tx flow control is enabled (we can send pause frames
	 *          but we do not support receiving pause frames).
	 *      3:  Both Rx and TX flow control (symmetric) are enabled.
	 *  other:  Invalid.
	 */
	switch (hw->fc.type) {
	case ixgb_fc_none:	/* 0 */
		break;
	case ixgb_fc_rx_pause:	/* 1 */
		/* RX Flow control is enabled, and TX Flow control is
		 * disabled.
		 */
		ctrl_reg |= (IXGB_CTRL0_RPE);
		break;
	case ixgb_fc_tx_pause:	/* 2 */
		/* TX Flow control is enabled, and RX Flow control is
		 * disabled, by a software over-ride.
		 */
		ctrl_reg |= (IXGB_CTRL0_TPE);
		pap_reg = hw->fc.pause_time;
		break;
	case ixgb_fc_full:	/* 3 */
		/* Flow control (both RX and TX) is enabled by a software
		 * over-ride.
		 */
		ctrl_reg |= (IXGB_CTRL0_RPE | IXGB_CTRL0_TPE);
		pap_reg = hw->fc.pause_time;
		break;
	default:
		/* We should never get here.  The value should be 0-3. */
		DEBUGOUT("Flow control param set incorrectly\n");
		ASSERT(0);
		break;
	}

	/* Write the new settings */
	IXGB_WRITE_REG(hw, CTRL0, ctrl_reg);

	if (pap_reg != 0) {
		IXGB_WRITE_REG(hw, PAP, pap_reg);
	}

	/* Set the flow control receive threshold registers.  Normally,
	 * these registers will be set to a default threshold that may be
	 * adjusted later by the driver's runtime code.  However, if the
	 * ability to transmit pause frames in not enabled, then these
	 * registers will be set to 0. 
	 */
	if (!(hw->fc.type & ixgb_fc_tx_pause)) {
		IXGB_WRITE_REG(hw, FCRTL, 0);
		IXGB_WRITE_REG(hw, FCRTH, 0);
	} else {
		/* We need to set up the Receive Threshold high and low water
		 * marks as well as (optionally) enabling the transmission of XON frames.
		 */
		if (hw->fc.send_xon) {
			IXGB_WRITE_REG(hw, FCRTL,
				       (hw->fc.low_water | IXGB_FCRTL_XONE));
		} else {
			IXGB_WRITE_REG(hw, FCRTL, hw->fc.low_water);
		}
		IXGB_WRITE_REG(hw, FCRTH, hw->fc.high_water);
	}
	return (status);
}

/******************************************************************************
 * Reads a word from a device over the Management Data Interface (MDI) bus.
 * This interface is used to manage Physical layer devices.
 *
 * hw          - Struct containing variables accessed by hw code
 * reg_address - Offset of device register being read.
 * phy_address - Address of device on MDI.
 *
 * Returns:  Data word (16 bits) from MDI device.
 *
 * This routine uses the new protocol MDI Single Command and Address Operation.
 * This requires that first an address cycle command is sent, followed by a
 * read command.
 *****************************************************************************/
u16
ixgb_read_phy_reg(struct ixgb_hw * hw,
		  u32 reg_address,
		  u32 phy_address, u32 device_type)
{
	u32 i;
	u32 data;
	u32 command = 0;

	ASSERT(reg_address <= IXGB_MAX_PHY_REG_ADDRESS);
	ASSERT(phy_address <= IXGB_MAX_PHY_ADDRESS);
	ASSERT(device_type <= IXGB_MAX_PHY_DEV_TYPE);

	/* Setup and write the address cycle command */
	command = ((reg_address << IXGB_MSCA_NP_ADDR_SHIFT) |
		   (device_type << IXGB_MSCA_DEV_TYPE_SHIFT) |
		   (phy_address << IXGB_MSCA_PHY_ADDR_SHIFT) |
		   (IXGB_MSCA_ADDR_CYCLE | IXGB_MSCA_MDI_COMMAND));

	IXGB_WRITE_REG(hw, MSCA, command);

    /**************************************************************
    ** Check every 10 usec to see if the address cycle completed
    ** The COMMAND bit will clear when the operation is complete.
    ** This may take as long as 64 usecs (we'll wait 100 usecs max)
    ** from the CPU Write to the Ready bit assertion.
    **************************************************************/

	for (i = 0; i < 10; i++) {
		usec_delay(10);

		command = IXGB_READ_REG(hw, MSCA);

		if ((command & IXGB_MSCA_MDI_COMMAND) == 0)
			break;
	}

	ASSERT((command & IXGB_MSCA_MDI_COMMAND) == 0);

	/* Address cycle complete, setup and write the read command */
	command = ((reg_address << IXGB_MSCA_NP_ADDR_SHIFT) |
		   (device_type << IXGB_MSCA_DEV_TYPE_SHIFT) |
		   (phy_address << IXGB_MSCA_PHY_ADDR_SHIFT) |
		   (IXGB_MSCA_READ | IXGB_MSCA_MDI_COMMAND));

	IXGB_WRITE_REG(hw, MSCA, command);

    /**************************************************************
    ** Check every 10 usec to see if the read command completed
    ** The COMMAND bit will clear when the operation is complete.
    ** The read may take as long as 64 usecs (we'll wait 100 usecs max)
    ** from the CPU Write to the Ready bit assertion.
    **************************************************************/

	for (i = 0; i < 10; i++) {
		usec_delay(10);

		command = IXGB_READ_REG(hw, MSCA);

		if ((command & IXGB_MSCA_MDI_COMMAND) == 0)
			break;
	}

	ASSERT((command & IXGB_MSCA_MDI_COMMAND) == 0);

	/* Operation is complete, get the data from the MDIO Read/Write Data
	 * register and return. 
	 */
	data = IXGB_READ_REG(hw, MSRWD);
	data >>= IXGB_MSRWD_READ_DATA_SHIFT;
	return ((u16) data);
}

/******************************************************************************
 * Writes a word to a device over the Management Data Interface (MDI) bus.
 * This interface is used to manage Physical layer devices.
 *
 * hw          - Struct containing variables accessed by hw code
 * reg_address - Offset of device register being read.
 * phy_address - Address of device on MDI.
 * device_type - Also known as the Device ID or DID.
 * data        - 16-bit value to be written
 *
 * Returns:  void.
 *
 * This routine uses the new protocol MDI Single Command and Address Operation.
 * This requires that first an address cycle command is sent, followed by a
 * write command.
 *****************************************************************************/
void
ixgb_write_phy_reg(struct ixgb_hw *hw,
		   u32 reg_address,
		   u32 phy_address, u32 device_type, u16 data)
{
	u32 i;
	u32 command = 0;

	ASSERT(reg_address <= IXGB_MAX_PHY_REG_ADDRESS);
	ASSERT(phy_address <= IXGB_MAX_PHY_ADDRESS);
	ASSERT(device_type <= IXGB_MAX_PHY_DEV_TYPE);

	/* Put the data in the MDIO Read/Write Data register */
	IXGB_WRITE_REG(hw, MSRWD, (u32) data);

	/* Setup and write the address cycle command */
	command = ((reg_address << IXGB_MSCA_NP_ADDR_SHIFT) |
		   (device_type << IXGB_MSCA_DEV_TYPE_SHIFT) |
		   (phy_address << IXGB_MSCA_PHY_ADDR_SHIFT) |
		   (IXGB_MSCA_ADDR_CYCLE | IXGB_MSCA_MDI_COMMAND));

	IXGB_WRITE_REG(hw, MSCA, command);

    /**************************************************************
    ** Check every 10 usec to see if the address cycle completed
    ** The COMMAND bit will clear when the operation is complete.
    ** This may take as long as 64 usecs (we'll wait 100 usecs max)
    ** from the CPU Write to the Ready bit assertion.
    **************************************************************/

	for (i = 0; i < 10; i++) {
		usec_delay(10);

		command = IXGB_READ_REG(hw, MSCA);

		if ((command & IXGB_MSCA_MDI_COMMAND) == 0)
			break;
	}

	ASSERT((command & IXGB_MSCA_MDI_COMMAND) == 0);

	/* Address cycle complete, setup and write the write command */
	command = ((reg_address << IXGB_MSCA_NP_ADDR_SHIFT) |
		   (device_type << IXGB_MSCA_DEV_TYPE_SHIFT) |
		   (phy_address << IXGB_MSCA_PHY_ADDR_SHIFT) |
		   (IXGB_MSCA_WRITE | IXGB_MSCA_MDI_COMMAND));

	IXGB_WRITE_REG(hw, MSCA, command);

    /**************************************************************
    ** Check every 10 usec to see if the read command completed
    ** The COMMAND bit will clear when the operation is complete.
    ** The write may take as long as 64 usecs (we'll wait 100 usecs max)
    ** from the CPU Write to the Ready bit assertion.
    **************************************************************/

	for (i = 0; i < 10; i++) {
		usec_delay(10);

		command = IXGB_READ_REG(hw, MSCA);

		if ((command & IXGB_MSCA_MDI_COMMAND) == 0)
			break;
	}

	ASSERT((command & IXGB_MSCA_MDI_COMMAND) == 0);

	/* Operation is complete, return. */
}

/******************************************************************************
 * Checks to see if the link status of the hardware has changed.
 *
 * hw - Struct containing variables accessed by hw code
 *
 * Called by any function that needs to check the link status of the adapter.
 *****************************************************************************/
void
ixgb_check_for_link(struct ixgb_hw *hw)
{
	u32 status_reg;
	u32 xpcss_reg;

	DEBUGFUNC("ixgb_check_for_link");

	xpcss_reg = IXGB_READ_REG(hw, XPCSS);
	status_reg = IXGB_READ_REG(hw, STATUS);

	if ((xpcss_reg & IXGB_XPCSS_ALIGN_STATUS) &&
	    (status_reg & IXGB_STATUS_LU)) {
		hw->link_up = TRUE;
	} else if (!(xpcss_reg & IXGB_XPCSS_ALIGN_STATUS) &&
		   (status_reg & IXGB_STATUS_LU)) {
		DEBUGOUT("XPCSS Not Aligned while Status:LU is set.\n");
		hw->link_up = ixgb_link_reset(hw);
	} else {
		hw->link_up = ixgb_link_reset(hw);
	}

	/*  Anything else for 10 Gig?? */
}

boolean_t
ixgb_check_for_bad_link(struct ixgb_hw *hw)
{
	u32 newLFC, newRFC;
	boolean_t bad_link_returncode = FALSE;

	/* check for a bad reset that may have occured
	 * the indication is that the RFC / LFC registers may be incrementing
	 * continually. Do a full adapter reset to recover
	 */
	newLFC = IXGB_READ_REG(hw, LFC);
	newRFC = IXGB_READ_REG(hw, RFC);
	if ((hw->lastLFC + 250 < newLFC) || (hw->lastRFC + 250 < newRFC)) {
		DEBUGOUT("BAD LINK! too many LFC/RFC since last check\n");
		bad_link_returncode = TRUE;
	}
	hw->lastLFC = newLFC;
	hw->lastRFC = newRFC;
	return bad_link_returncode;
}

/******************************************************************************
 * Clears all hardware statistics counters. 
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
void
ixgb_clear_hw_cntrs(struct ixgb_hw *hw)
{
	volatile u32 temp_reg;

	DEBUGFUNC("ixgb_clear_hw_cntrs");

	/* if we are stopped or resetting exit gracefully */
	if (hw->adapter_stopped) {
		DEBUGOUT("Exiting because the adapter is stopped!!!\n");
		return;
	}

	temp_reg = IXGB_READ_REG(hw, TPRL);
	temp_reg = IXGB_READ_REG(hw, TPRH);
	temp_reg = IXGB_READ_REG(hw, GPRCL);
	temp_reg = IXGB_READ_REG(hw, GPRCH);
	temp_reg = IXGB_READ_REG(hw, BPRCL);
	temp_reg = IXGB_READ_REG(hw, BPRCH);
	temp_reg = IXGB_READ_REG(hw, MPRCL);
	temp_reg = IXGB_READ_REG(hw, MPRCH);
	temp_reg = IXGB_READ_REG(hw, UPRCL);
	temp_reg = IXGB_READ_REG(hw, UPRCH);
	temp_reg = IXGB_READ_REG(hw, VPRCL);
	temp_reg = IXGB_READ_REG(hw, VPRCH);
	temp_reg = IXGB_READ_REG(hw, JPRCL);
	temp_reg = IXGB_READ_REG(hw, JPRCH);
	temp_reg = IXGB_READ_REG(hw, GORCL);
	temp_reg = IXGB_READ_REG(hw, GORCH);
	temp_reg = IXGB_READ_REG(hw, TORL);
	temp_reg = IXGB_READ_REG(hw, TORH);
	temp_reg = IXGB_READ_REG(hw, RNBC);
	temp_reg = IXGB_READ_REG(hw, RUC);
	temp_reg = IXGB_READ_REG(hw, ROC);
	temp_reg = IXGB_READ_REG(hw, RLEC);
	temp_reg = IXGB_READ_REG(hw, CRCERRS);
	temp_reg = IXGB_READ_REG(hw, ICBC);
	temp_reg = IXGB_READ_REG(hw, ECBC);
	temp_reg = IXGB_READ_REG(hw, MPC);
	temp_reg = IXGB_READ_REG(hw, TPTL);
	temp_reg = IXGB_READ_REG(hw, TPTH);
	temp_reg = IXGB_READ_REG(hw, GPTCL);
	temp_reg = IXGB_READ_REG(hw, GPTCH);
	temp_reg = IXGB_READ_REG(hw, BPTCL);
	temp_reg = IXGB_READ_REG(hw, BPTCH);
	temp_reg = IXGB_READ_REG(hw, MPTCL);
	temp_reg = IXGB_READ_REG(hw, MPTCH);
	temp_reg = IXGB_READ_REG(hw, UPTCL);
	temp_reg = IXGB_READ_REG(hw, UPTCH);
	temp_reg = IXGB_READ_REG(hw, VPTCL);
	temp_reg = IXGB_READ_REG(hw, VPTCH);
	temp_reg = IXGB_READ_REG(hw, JPTCL);
	temp_reg = IXGB_READ_REG(hw, JPTCH);
	temp_reg = IXGB_READ_REG(hw, GOTCL);
	temp_reg = IXGB_READ_REG(hw, GOTCH);
	temp_reg = IXGB_READ_REG(hw, TOTL);
	temp_reg = IXGB_READ_REG(hw, TOTH);
	temp_reg = IXGB_READ_REG(hw, DC);
	temp_reg = IXGB_READ_REG(hw, PLT64C);
	temp_reg = IXGB_READ_REG(hw, TSCTC);
	temp_reg = IXGB_READ_REG(hw, TSCTFC);
	temp_reg = IXGB_READ_REG(hw, IBIC);
	temp_reg = IXGB_READ_REG(hw, RFC);
	temp_reg = IXGB_READ_REG(hw, LFC);
	temp_reg = IXGB_READ_REG(hw, PFRC);
	temp_reg = IXGB_READ_REG(hw, PFTC);
	temp_reg = IXGB_READ_REG(hw, MCFRC);
	temp_reg = IXGB_READ_REG(hw, MCFTC);
	temp_reg = IXGB_READ_REG(hw, XONRXC);
	temp_reg = IXGB_READ_REG(hw, XONTXC);
	temp_reg = IXGB_READ_REG(hw, XOFFRXC);
	temp_reg = IXGB_READ_REG(hw, XOFFTXC);
	temp_reg = IXGB_READ_REG(hw, RJC);
	return;
}

/******************************************************************************
 * Turns on the software controllable LED
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
void
ixgb_led_on(struct ixgb_hw *hw)
{
	u32 ctrl0_reg = IXGB_READ_REG(hw, CTRL0);

	/* To turn on the LED, clear software-definable pin 0 (SDP0). */
	ctrl0_reg &= ~IXGB_CTRL0_SDP0;
	IXGB_WRITE_REG(hw, CTRL0, ctrl0_reg);
	return;
}

/******************************************************************************
 * Turns off the software controllable LED
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
void
ixgb_led_off(struct ixgb_hw *hw)
{
	u32 ctrl0_reg = IXGB_READ_REG(hw, CTRL0);

	/* To turn off the LED, set software-definable pin 0 (SDP0). */
	ctrl0_reg |= IXGB_CTRL0_SDP0;
	IXGB_WRITE_REG(hw, CTRL0, ctrl0_reg);
	return;
}

/******************************************************************************
 * Gets the current PCI bus type, speed, and width of the hardware
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
ixgb_get_bus_info(struct ixgb_hw *hw)
{
	u32 status_reg;

	status_reg = IXGB_READ_REG(hw, STATUS);

	hw->bus.type = (status_reg & IXGB_STATUS_PCIX_MODE) ?
	    ixgb_bus_type_pcix : ixgb_bus_type_pci;

	if (hw->bus.type == ixgb_bus_type_pci) {
		hw->bus.speed = (status_reg & IXGB_STATUS_PCI_SPD) ?
		    ixgb_bus_speed_66 : ixgb_bus_speed_33;
	} else {
		switch (status_reg & IXGB_STATUS_PCIX_SPD_MASK) {
		case IXGB_STATUS_PCIX_SPD_66:
			hw->bus.speed = ixgb_bus_speed_66;
			break;
		case IXGB_STATUS_PCIX_SPD_100:
			hw->bus.speed = ixgb_bus_speed_100;
			break;
		case IXGB_STATUS_PCIX_SPD_133:
			hw->bus.speed = ixgb_bus_speed_133;
			break;
		default:
			hw->bus.speed = ixgb_bus_speed_reserved;
			break;
		}
	}

	hw->bus.width = (status_reg & IXGB_STATUS_BUS64) ?
	    ixgb_bus_width_64 : ixgb_bus_width_32;

	return;
}

/******************************************************************************
 * Tests a MAC address to ensure it is a valid Individual Address
 *
 * mac_addr - pointer to MAC address. 
 *
 *****************************************************************************/
boolean_t
mac_addr_valid(u8 * mac_addr)
{
	boolean_t is_valid = TRUE;
	DEBUGFUNC("mac_addr_valid");

	/* Make sure it is not a multicast address */
	if (IS_MULTICAST(mac_addr)) {
		DEBUGOUT("MAC address is multicast\n");
		is_valid = FALSE;
	}
	/* Not a broadcast address */
	else if (IS_BROADCAST(mac_addr)) {
		DEBUGOUT("MAC address is broadcast\n");
		is_valid = FALSE;
	}
	/* Reject the zero address */
	else if (mac_addr[0] == 0 &&
		 mac_addr[1] == 0 &&
		 mac_addr[2] == 0 &&
		 mac_addr[3] == 0 && mac_addr[4] == 0 && mac_addr[5] == 0) {
		DEBUGOUT("MAC address is all zeros\n");
		is_valid = FALSE;
	}
	return (is_valid);
}

/******************************************************************************
 * Resets the 10GbE link.  Waits the settle time and returns the state of 
 * the link.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
boolean_t
ixgb_link_reset(struct ixgb_hw * hw)
{
	boolean_t link_status = FALSE;
	u8 wait_retries = MAX_RESET_ITERATIONS;
	u8 lrst_retries = MAX_RESET_ITERATIONS;

	do {
		IXGB_WRITE_REG(hw, CTRL0,
			       IXGB_READ_REG(hw, CTRL0) | IXGB_CTRL0_LRST);

		do {
			usec_delay(IXGB_DELAY_USECS_AFTER_LINK_RESET);
			link_status =
			    ((IXGB_READ_REG(hw, STATUS) & IXGB_STATUS_LU)
			     && (IXGB_READ_REG(hw, XPCSS) &
				 IXGB_XPCSS_ALIGN_STATUS)) ? TRUE : FALSE;
		} while (!link_status && --wait_retries);
	} while (!link_status && --lrst_retries);

	return link_status;
}

/******************************************************************************
 * Resets the 10GbE optics module.
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
void
ixgb_optics_reset(struct ixgb_hw *hw)
{
	u16 mdio_reg;

	ixgb_write_phy_reg(hw,
			   TXN17401_PMA_PMD_CR1,
			   IXGB_PHY_ADDRESS,
			   TXN17401_PMA_PMD_DID, TXN17401_PMA_PMD_CR1_RESET);
	mdio_reg = ixgb_read_phy_reg(hw,
				     TXN17401_PMA_PMD_CR1,
				     IXGB_PHY_ADDRESS, TXN17401_PMA_PMD_DID);
}
