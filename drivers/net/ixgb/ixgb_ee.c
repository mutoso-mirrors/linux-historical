/*******************************************************************************

  
  Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
  
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

#include "ixgb_hw.h"
#include "ixgb_ee.h"
/* Local prototypes */
static uint16_t ixgb_shift_in_bits(struct ixgb_hw *hw);

static void ixgb_shift_out_bits(struct ixgb_hw *hw,
				uint16_t data,
				uint16_t count);
static void ixgb_standby_eeprom(struct ixgb_hw *hw);

static boolean_t ixgb_wait_eeprom_command(struct ixgb_hw *hw);

static void ixgb_cleanup_eeprom(struct ixgb_hw *hw);

/******************************************************************************
 * Raises the EEPROM's clock input.
 *
 * hw - Struct containing variables accessed by shared code
 * eecd_reg - EECD's current value
 *****************************************************************************/
static void
ixgb_raise_clock(struct ixgb_hw *hw,
		  uint32_t *eecd_reg)
{
	/* Raise the clock input to the EEPROM (by setting the SK bit), and then
	 *  wait 50 microseconds.
	 */
	*eecd_reg = *eecd_reg | IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, *eecd_reg);
	udelay(50);
	return;
}

/******************************************************************************
 * Lowers the EEPROM's clock input.
 *
 * hw - Struct containing variables accessed by shared code
 * eecd_reg - EECD's current value
 *****************************************************************************/
static void
ixgb_lower_clock(struct ixgb_hw *hw,
		  uint32_t *eecd_reg)
{
	/* Lower the clock input to the EEPROM (by clearing the SK bit), and then
	 * wait 50 microseconds.
	 */
	*eecd_reg = *eecd_reg & ~IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, *eecd_reg);
	udelay(50);
	return;
}

/******************************************************************************
 * Shift data bits out to the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * data - data to send to the EEPROM
 * count - number of bits to shift out
 *****************************************************************************/
static void
ixgb_shift_out_bits(struct ixgb_hw *hw,
					 uint16_t data,
					 uint16_t count)
{
	uint32_t eecd_reg;
	uint32_t mask;

	/* We need to shift "count" bits out to the EEPROM. So, value in the
	 * "data" parameter will be shifted out to the EEPROM one bit at a time.
	 * In order to do this, "data" must be broken down into bits.
	 */
	mask = 0x01 << (count - 1);
	eecd_reg = IXGB_READ_REG(hw, EECD);
	eecd_reg &= ~(IXGB_EECD_DO | IXGB_EECD_DI);
	do {
		/* A "1" is shifted out to the EEPROM by setting bit "DI" to a "1",
		 * and then raising and then lowering the clock (the SK bit controls
		 * the clock input to the EEPROM).  A "0" is shifted out to the EEPROM
		 * by setting "DI" to "0" and then raising and then lowering the clock.
		 */
		eecd_reg &= ~IXGB_EECD_DI;

		if(data & mask)
			eecd_reg |= IXGB_EECD_DI;

		IXGB_WRITE_REG(hw, EECD, eecd_reg);

		udelay(50);

		ixgb_raise_clock(hw, &eecd_reg);
		ixgb_lower_clock(hw, &eecd_reg);

		mask = mask >> 1;

	} while(mask);

	/* We leave the "DI" bit set to "0" when we leave this routine. */
	eecd_reg &= ~IXGB_EECD_DI;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	return;
}

/******************************************************************************
 * Shift data bits in from the EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static uint16_t
ixgb_shift_in_bits(struct ixgb_hw *hw)
{
	uint32_t eecd_reg;
	uint32_t i;
	uint16_t data;

	/* In order to read a register from the EEPROM, we need to shift 16 bits
	 * in from the EEPROM. Bits are "shifted in" by raising the clock input to
	 * the EEPROM (setting the SK bit), and then reading the value of the "DO"
	 * bit.  During this "shifting in" process the "DI" bit should always be
	 * clear..
	 */

	eecd_reg = IXGB_READ_REG(hw, EECD);

	eecd_reg &= ~(IXGB_EECD_DO | IXGB_EECD_DI);
	data = 0;

	for(i = 0; i < 16; i++) {
		data = data << 1;
		ixgb_raise_clock(hw, &eecd_reg);

		eecd_reg = IXGB_READ_REG(hw, EECD);

		eecd_reg &= ~(IXGB_EECD_DI);
		if(eecd_reg & IXGB_EECD_DO)
			data |= 1;

		ixgb_lower_clock(hw, &eecd_reg);
	}

	return data;
}

/******************************************************************************
 * Prepares EEPROM for access
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Lowers EEPROM clock. Clears input pin. Sets the chip select pin. This
 * function should be called before issuing a command to the EEPROM.
 *****************************************************************************/
static void
ixgb_setup_eeprom(struct ixgb_hw *hw)
{
	uint32_t eecd_reg;

	eecd_reg = IXGB_READ_REG(hw, EECD);

	/*  Clear SK and DI  */
	eecd_reg &= ~(IXGB_EECD_SK | IXGB_EECD_DI);
	IXGB_WRITE_REG(hw, EECD, eecd_reg);

	/*  Set CS  */
	eecd_reg |= IXGB_EECD_CS;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	return;
}

/******************************************************************************
 * Returns EEPROM to a "standby" state
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
ixgb_standby_eeprom(struct ixgb_hw *hw)
{
	uint32_t eecd_reg;

	eecd_reg = IXGB_READ_REG(hw, EECD);

	/*  Deselct EEPROM  */
	eecd_reg &= ~(IXGB_EECD_CS | IXGB_EECD_SK);
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	udelay(50);

	/*  Clock high  */
	eecd_reg |= IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	udelay(50);

	/*  Select EEPROM  */
	eecd_reg |= IXGB_EECD_CS;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	udelay(50);

	/*  Clock low  */
	eecd_reg &= ~IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	udelay(50);
	return;
}

/******************************************************************************
 * Raises then lowers the EEPROM's clock pin
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
ixgb_clock_eeprom(struct ixgb_hw *hw)
{
	uint32_t eecd_reg;

	eecd_reg = IXGB_READ_REG(hw, EECD);

	/*  Rising edge of clock  */
	eecd_reg |= IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	udelay(50);

	/*  Falling edge of clock  */
	eecd_reg &= ~IXGB_EECD_SK;
	IXGB_WRITE_REG(hw, EECD, eecd_reg);
	udelay(50);
	return;
}

/******************************************************************************
 * Terminates a command by lowering the EEPROM's chip select pin
 *
 * hw - Struct containing variables accessed by shared code
 *****************************************************************************/
static void
ixgb_cleanup_eeprom(struct ixgb_hw *hw)
{
	uint32_t eecd_reg;

	eecd_reg = IXGB_READ_REG(hw, EECD);

	eecd_reg &= ~(IXGB_EECD_CS | IXGB_EECD_DI);

	IXGB_WRITE_REG(hw, EECD, eecd_reg);

	ixgb_clock_eeprom(hw);
	return;
}

/******************************************************************************
 * Waits for the EEPROM to finish the current command.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * The command is done when the EEPROM's data out pin goes high.
 *
 * Returns:
 *      TRUE: EEPROM data pin is high before timeout.
 *      FALSE:  Time expired.
 *****************************************************************************/
static boolean_t
ixgb_wait_eeprom_command(struct ixgb_hw *hw)
{
	uint32_t eecd_reg;
	uint32_t i;

	/* Toggle the CS line.  This in effect tells to EEPROM to actually execute
	 * the command in question.
	 */
	ixgb_standby_eeprom(hw);

	/* Now read DO repeatedly until is high (equal to '1').  The EEEPROM will
	 * signal that the command has been completed by raising the DO signal.
	 * If DO does not go high in 10 milliseconds, then error out.
	 */
	for(i = 0; i < 200; i++) {
		eecd_reg = IXGB_READ_REG(hw, EECD);

		if(eecd_reg & IXGB_EECD_DO)
			return (TRUE);

		udelay(50);
	}
	ASSERT(0);
	return (FALSE);
}

/******************************************************************************
 * Verifies that the EEPROM has a valid checksum
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Reads the first 64 16 bit words of the EEPROM and sums the values read.
 * If the the sum of the 64 16 bit words is 0xBABA, the EEPROM's checksum is
 * valid.
 *
 * Returns:
 *  TRUE: Checksum is valid
 *  FALSE: Checksum is not valid.
 *****************************************************************************/
boolean_t
ixgb_validate_eeprom_checksum(struct ixgb_hw *hw)
{
	uint16_t checksum = 0;
	uint16_t i;

	for(i = 0; i < (EEPROM_CHECKSUM_REG + 1); i++)
		checksum += ixgb_read_eeprom(hw, i);

	if(checksum == (uint16_t) EEPROM_SUM)
		return (TRUE);
	else
		return (FALSE);
}

/******************************************************************************
 * Calculates the EEPROM checksum and writes it to the EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Sums the first 63 16 bit words of the EEPROM. Subtracts the sum from 0xBABA.
 * Writes the difference to word offset 63 of the EEPROM.
 *****************************************************************************/
void
ixgb_update_eeprom_checksum(struct ixgb_hw *hw)
{
	uint16_t checksum = 0;
	uint16_t i;

	for(i = 0; i < EEPROM_CHECKSUM_REG; i++)
		checksum += ixgb_read_eeprom(hw, i);

	checksum = (uint16_t) EEPROM_SUM - checksum;

	ixgb_write_eeprom(hw, EEPROM_CHECKSUM_REG, checksum);
	return;
}

/******************************************************************************
 * Writes a 16 bit word to a given offset in the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * reg - offset within the EEPROM to be written to
 * data - 16 bit word to be writen to the EEPROM
 *
 * If ixgb_update_eeprom_checksum is not called after this function, the
 * EEPROM will most likely contain an invalid checksum.
 *
 *****************************************************************************/
void
ixgb_write_eeprom(struct ixgb_hw *hw, uint16_t offset, uint16_t data)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	/* Prepare the EEPROM for writing */
	ixgb_setup_eeprom(hw);

	/*  Send the 9-bit EWEN (write enable) command to the EEPROM (5-bit opcode
	 *  plus 4-bit dummy).  This puts the EEPROM into write/erase mode.
	 */
	ixgb_shift_out_bits(hw, EEPROM_EWEN_OPCODE, 5);
	ixgb_shift_out_bits(hw, 0, 4);

	/*  Prepare the EEPROM  */
	ixgb_standby_eeprom(hw);

	/*  Send the Write command (3-bit opcode + 6-bit addr)  */
	ixgb_shift_out_bits(hw, EEPROM_WRITE_OPCODE, 3);
	ixgb_shift_out_bits(hw, offset, 6);

	/*  Send the data  */
	ixgb_shift_out_bits(hw, data, 16);

	ixgb_wait_eeprom_command(hw);

	/*  Recover from write  */
	ixgb_standby_eeprom(hw);

	/* Send the 9-bit EWDS (write disable) command to the EEPROM (5-bit
	 * opcode plus 4-bit dummy).  This takes the EEPROM out of write/erase
	 * mode.
	 */
	ixgb_shift_out_bits(hw, EEPROM_EWDS_OPCODE, 5);
	ixgb_shift_out_bits(hw, 0, 4);

	/*  Done with writing  */
	ixgb_cleanup_eeprom(hw);

	/* clear the init_ctrl_reg_1 to signify that the cache is invalidated */
	ee_map->init_ctrl_reg_1 = EEPROM_ICW1_SIGNATURE_CLEAR;

	return;
}

/******************************************************************************
 * Reads a 16 bit word from the EEPROM.
 *
 * hw - Struct containing variables accessed by shared code
 * offset - offset of 16 bit word in the EEPROM to read
 *
 * Returns:
 *  The 16-bit value read from the eeprom
 *****************************************************************************/
uint16_t
ixgb_read_eeprom(struct ixgb_hw *hw,
		  uint16_t offset)
{
	uint16_t data;

	/*  Prepare the EEPROM for reading  */
	ixgb_setup_eeprom(hw);

	/*  Send the READ command (opcode + addr)  */
	ixgb_shift_out_bits(hw, EEPROM_READ_OPCODE, 3);
	/*
	 * We have a 64 word EEPROM, there are 6 address bits
	 */
	ixgb_shift_out_bits(hw, offset, 6);

	/*  Read the data  */
	data = ixgb_shift_in_bits(hw);

	/*  End this read operation  */
	ixgb_standby_eeprom(hw);

	return (data);
}

/******************************************************************************
 * Reads eeprom and stores data in shared structure.
 * Validates eeprom checksum and eeprom signature.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *      TRUE: if eeprom read is successful
 *      FALSE: otherwise.
 *****************************************************************************/
boolean_t
ixgb_get_eeprom_data(struct ixgb_hw *hw)
{
	uint16_t i;
	uint16_t checksum = 0;
	struct ixgb_ee_map_type *ee_map;

	DEBUGFUNC("ixgb_get_eeprom_data");

	ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	DEBUGOUT("ixgb_ee: Reading eeprom data\n");
	for(i = 0; i < IXGB_EEPROM_SIZE ; i++) {
		uint16_t ee_data;
		ee_data = ixgb_read_eeprom(hw, i);
		checksum += ee_data;
		hw->eeprom[i] = le16_to_cpu(ee_data);
	}

	if (checksum != (uint16_t) EEPROM_SUM) {
		DEBUGOUT("ixgb_ee: Checksum invalid.\n");
		/* clear the init_ctrl_reg_1 to signify that the cache is
		 * invalidated */
		ee_map->init_ctrl_reg_1 = EEPROM_ICW1_SIGNATURE_CLEAR;
		return (FALSE);
	}

	if ((ee_map->init_ctrl_reg_1 & le16_to_cpu(EEPROM_ICW1_SIGNATURE_MASK))
		 != le16_to_cpu(EEPROM_ICW1_SIGNATURE_VALID)) {
		DEBUGOUT("ixgb_ee: Signature invalid.\n");
		return(FALSE);
	}

	return(TRUE);
}

/******************************************************************************
 * Local function to check if the eeprom signature is good
 * If the eeprom signature is good, calls ixgb)get_eeprom_data.
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *      TRUE: eeprom signature was good and the eeprom read was successful
 *      FALSE: otherwise.
 ******************************************************************************/
static boolean_t
ixgb_check_and_get_eeprom_data (struct ixgb_hw* hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if ((ee_map->init_ctrl_reg_1 & le16_to_cpu(EEPROM_ICW1_SIGNATURE_MASK))
	    == le16_to_cpu(EEPROM_ICW1_SIGNATURE_VALID)) {
		return (TRUE);
	} else {
		return ixgb_get_eeprom_data(hw);
	}
}

/******************************************************************************
 * return a word from the eeprom
 *
 * hw - Struct containing variables accessed by shared code
 * index - Offset of eeprom word
 *
 * Returns:
 *          Word at indexed offset in eeprom, if valid, 0 otherwise.
 ******************************************************************************/
uint16_t
ixgb_get_eeprom_word(struct ixgb_hw *hw, uint16_t index)
{

	if ((index < IXGB_EEPROM_SIZE) &&
		(ixgb_check_and_get_eeprom_data(hw) == TRUE)) {
	   return(hw->eeprom[index]);
	}

	return(0);
}

/******************************************************************************
 * return the mac address from EEPROM
 *
 * hw       - Struct containing variables accessed by shared code
 * mac_addr - Ethernet Address if EEPROM contents are valid, 0 otherwise
 *
 * Returns: None.
 ******************************************************************************/
void
ixgb_get_ee_mac_addr(struct ixgb_hw *hw,
			uint8_t *mac_addr)
{
	int i;
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	DEBUGFUNC("ixgb_get_ee_mac_addr");

	if (ixgb_check_and_get_eeprom_data(hw) == TRUE) {
		for (i = 0; i < IXGB_ETH_LENGTH_OF_ADDRESS; i++) {
			mac_addr[i] = ee_map->mac_addr[i];
			DEBUGOUT2("mac(%d) = %.2X\n", i, mac_addr[i]);
		}
	}
}

/******************************************************************************
 * return the compatibility flags from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          compatibility flags if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
uint16_t
ixgb_get_ee_compatibility(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if(ixgb_check_and_get_eeprom_data(hw) == TRUE)
		return(ee_map->compatibility);

	return(0);
}

/******************************************************************************
 * return the Printed Board Assembly number from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          PBA number if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
uint32_t
ixgb_get_ee_pba_number(struct ixgb_hw *hw)
{
	if(ixgb_check_and_get_eeprom_data(hw) == TRUE)
		return (le16_to_cpu(hw->eeprom[EEPROM_PBA_1_2_REG])
			| (le16_to_cpu(hw->eeprom[EEPROM_PBA_3_4_REG])<<16));

	return(0);
}

/******************************************************************************
 * return the Initialization Control Word 1 from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          Initialization Control Word 1 if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
uint16_t
ixgb_get_ee_init_ctrl_reg_1(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if(ixgb_check_and_get_eeprom_data(hw) == TRUE)
		return(ee_map->init_ctrl_reg_1);

	return(0);
}

/******************************************************************************
 * return the Initialization Control Word 2 from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          Initialization Control Word 2 if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
uint16_t
ixgb_get_ee_init_ctrl_reg_2(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if(ixgb_check_and_get_eeprom_data(hw) == TRUE)
		return(ee_map->init_ctrl_reg_2);

	return(0);
}

/******************************************************************************
 * return the Subsystem Id from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          Subsystem Id if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
uint16_t
ixgb_get_ee_subsystem_id(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if(ixgb_check_and_get_eeprom_data(hw) == TRUE)
	   return(ee_map->subsystem_id);

	return(0);
}

/******************************************************************************
 * return the Sub Vendor Id from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          Sub Vendor Id if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
uint16_t
ixgb_get_ee_subvendor_id(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if(ixgb_check_and_get_eeprom_data(hw) == TRUE)
		return(ee_map->subvendor_id);

	return(0);
}

/******************************************************************************
 * return the Device Id from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          Device Id if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
uint16_t
ixgb_get_ee_device_id(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if(ixgb_check_and_get_eeprom_data(hw) == TRUE)
		return(ee_map->device_id);

	return(0);
}

/******************************************************************************
 * return the Vendor Id from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          Device Id if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
uint16_t
ixgb_get_ee_vendor_id(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if(ixgb_check_and_get_eeprom_data(hw) == TRUE)
		return(ee_map->vendor_id);

	return(0);
}

/******************************************************************************
 * return the Software Defined Pins Register from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          SDP Register if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
uint16_t
ixgb_get_ee_swdpins_reg(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if(ixgb_check_and_get_eeprom_data(hw) == TRUE)
		return(ee_map->swdpins_reg);

	return(0);
}

/******************************************************************************
 * return the D3 Power Management Bits from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          D3 Power Management Bits if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
uint8_t
ixgb_get_ee_d3_power(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if(ixgb_check_and_get_eeprom_data(hw) == TRUE)
		return(ee_map->d3_power);

	return(0);
}

/******************************************************************************
 * return the D0 Power Management Bits from EEPROM
 *
 * hw - Struct containing variables accessed by shared code
 *
 * Returns:
 *          D0 Power Management Bits if EEPROM contents are valid, 0 otherwise
 ******************************************************************************/
uint8_t
ixgb_get_ee_d0_power(struct ixgb_hw *hw)
{
	struct ixgb_ee_map_type *ee_map = (struct ixgb_ee_map_type *)hw->eeprom;

	if(ixgb_check_and_get_eeprom_data(hw) == TRUE)
		return(ee_map->d0_power);

	return(0);
}
