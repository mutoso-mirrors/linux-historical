/*****************************************************************************
 *
 * Module Name: ectransx.c
 *   $Revision: 21 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 Andrew Grover
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


#include <acpi.h>
#include "ec.h"

#define _COMPONENT		ACPI_EC
	MODULE_NAME             ("ectransx")


/****************************************************************************
 *
 * FUNCTION:    ec_io_wait
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ec_io_wait (
	EC_CONTEXT              *ec,
	EC_EVENT                wait_event)
{
	EC_STATUS               ec_status = 0;
	u32                     i = 100;

	if (!ec || ((wait_event != EC_EVENT_OUTPUT_BUFFER_FULL)
		&& (wait_event != EC_EVENT_INPUT_BUFFER_EMPTY))) {
		return(AE_BAD_PARAMETER);
	}

	/*
	 * Wait for Event:
	 * ---------------
	 * Poll the EC status register waiting for the event to occur.
	 * Note that we'll wait a maximum of 1ms in 10us chunks.
	 */
	switch (wait_event) {

	case EC_EVENT_OUTPUT_BUFFER_FULL:
		do {
			ec_status = acpi_os_in8(ec->status_port);
			if (ec_status & EC_FLAG_OUTPUT_BUFFER) {
				return(AE_OK);
			}
			acpi_os_sleep_usec(10);
		} while (--i>0);
		break;

	case EC_EVENT_INPUT_BUFFER_EMPTY:
		do {
			ec_status = acpi_os_in8(ec->status_port);
			if (!(ec_status & EC_FLAG_INPUT_BUFFER)) {
				return(AE_OK);
			}
			acpi_os_sleep_usec(10);
		} while (--i>0);
		break;
	}

	return(AE_TIME);
}


/****************************************************************************
 *
 * FUNCTION:    ec_io_read
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ec_io_read (
	EC_CONTEXT              *ec,
	ACPI_IO_ADDRESS         io_port,
	u8                      *data,
	EC_EVENT                wait_event)
{
	ACPI_STATUS             status = AE_OK;

	if (!ec || !data) {
		return(AE_BAD_PARAMETER);
	}

	*data = acpi_os_in8(io_port);

	if (wait_event) {
		status = ec_io_wait(ec, wait_event);
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:    ec_io_write
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ec_io_write (
	EC_CONTEXT              *ec,
	ACPI_IO_ADDRESS         io_port,
	u8                      data,
	EC_EVENT                wait_event)
{
	ACPI_STATUS             status = AE_OK;

	if (!ec) {
		return(AE_BAD_PARAMETER);
	}

	acpi_os_out8(io_port, data);

	if (wait_event) {
		status = ec_io_wait(ec, wait_event);
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:    ec_read
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ec_read (
	EC_CONTEXT              *ec,
	u8                      address,
	u8                      *data)
{
	ACPI_STATUS             status = AE_OK;

	if (!ec || !data) {
		return(AE_BAD_PARAMETER);
	}

	if (ec->use_global_lock) {
		status = acpi_acquire_global_lock();
		if (ACPI_FAILURE(status)) {
			return(status);
		}
	}

	status = ec_io_write(ec, ec->command_port, EC_COMMAND_READ,
		EC_EVENT_INPUT_BUFFER_EMPTY);
	if (ACPI_FAILURE(status)) {
		return(status);
	}

	status = ec_io_write(ec, ec->data_port, address,
		EC_EVENT_OUTPUT_BUFFER_FULL);
	if (ACPI_FAILURE(status)) {
		return(status);
	}

	status = ec_io_read(ec, ec->data_port, data, EC_EVENT_NONE);

	if (ec->use_global_lock) {
		acpi_release_global_lock();
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:    ec_write
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ec_write (
	EC_CONTEXT              *ec,
	u8                      address,
	u8                      data)
{
	ACPI_STATUS             status = AE_OK;

	if (!ec)
		return(AE_BAD_PARAMETER);

	if (ec->use_global_lock) {
		status = acpi_acquire_global_lock();
		if (ACPI_FAILURE(status)) {
			return(status);
		}
	}

	status = ec_io_write(ec, ec->command_port, EC_COMMAND_WRITE,
		EC_EVENT_INPUT_BUFFER_EMPTY);
	if (ACPI_FAILURE(status)) {
		return(status);
	}

	status = ec_io_write(ec, ec->data_port, address,
		EC_EVENT_INPUT_BUFFER_EMPTY);
	if (ACPI_FAILURE(status)) {
		return(status);
	}

	status = ec_io_write(ec, ec->data_port, data,
		EC_EVENT_INPUT_BUFFER_EMPTY);
	if (ACPI_FAILURE(status)) {
		return(status);
	}

	if (ec->use_global_lock) {
		acpi_release_global_lock();
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:    ec_transaction
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ec_transaction (
	EC_CONTEXT              *ec,
	EC_REQUEST              *request)
{
	ACPI_STATUS             status = AE_OK;

	if (!ec || !request) {
		return(AE_BAD_PARAMETER);
	}

	/*
	 * Obtain mutex to serialize all EC transactions.
	 */
	status = acpi_os_wait_semaphore(ec->mutex, 1, EC_DEFAULT_TIMEOUT);
	if (ACPI_FAILURE(status)) {
		return(status);
	}

	/*
	 * Perform the transaction.
	 */
	switch (request->command) {

	case EC_COMMAND_READ:
		status = ec_read(ec, request->address, &(request->data));
		break;

	case EC_COMMAND_WRITE:
		status = ec_write(ec, request->address, request->data);
		break;

	default:
		status = AE_SUPPORT;
		break;
	}

	/*
	 * Signal the mutex to indicate transaction completion.
	 */
	acpi_os_signal_semaphore(ec->mutex, 1);

	return(status);
}
