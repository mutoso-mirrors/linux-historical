
/******************************************************************************
 *
 * Module Name: exsystem - Interface to OS services
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


#include "acpi.h"
#include "acinterp.h"
#include "acevents.h"

#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exsystem")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_system_wait_semaphore
 *
 * PARAMETERS:  Semaphore           - OSD semaphore to wait on
 *              Timeout             - Max time to wait
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Implements a semaphore wait with a check to see if the
 *              semaphore is available immediately.  If it is not, the
 *              interpreter is released.
 *
 ******************************************************************************/

acpi_status
acpi_ex_system_wait_semaphore (
	acpi_handle             semaphore,
	u16                     timeout)
{
	acpi_status             status;
	acpi_status             status2;


	ACPI_FUNCTION_TRACE ("ex_system_wait_semaphore");


	status = acpi_os_wait_semaphore (semaphore, 1, 0);
	if (ACPI_SUCCESS (status)) {
		return_ACPI_STATUS (status);
	}

	if (status == AE_TIME) {
		/* We must wait, so unlock the interpreter */

		acpi_ex_exit_interpreter ();

		status = acpi_os_wait_semaphore (semaphore, 1, timeout);

		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "*** Thread awake after blocking, %s\n",
			acpi_format_exception (status)));

		/* Reacquire the interpreter */

		status2 = acpi_ex_enter_interpreter ();
		if (ACPI_FAILURE (status2)) {
			/* Report fatal error, could not acquire interpreter */

			return_ACPI_STATUS (status2);
		}
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_system_do_stall
 *
 * PARAMETERS:  how_long            - The amount of time to stall
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Suspend running thread for specified amount of time.
 *
 ******************************************************************************/

acpi_status
acpi_ex_system_do_stall (
	u32                     how_long)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_ENTRY ();


	if (how_long > 1000) /* 1 millisecond */ {
		/* Since this thread will sleep, we must release the interpreter */

		acpi_ex_exit_interpreter ();

		acpi_os_stall (how_long);

		/* And now we must get the interpreter again */

		status = acpi_ex_enter_interpreter ();
	}

	else {
		acpi_os_sleep (0, (how_long / 1000) + 1);
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_system_do_suspend
 *
 * PARAMETERS:  how_long            - The amount of time to suspend
 *
 * RETURN:      None
 *
 * DESCRIPTION: Suspend running thread for specified amount of time.
 *
 ******************************************************************************/

acpi_status
acpi_ex_system_do_suspend (
	u32                     how_long)
{
	acpi_status             status;


	ACPI_FUNCTION_ENTRY ();


	/* Since this thread will sleep, we must release the interpreter */

	acpi_ex_exit_interpreter ();

	acpi_os_sleep ((u16) (how_long / (u32) 1000),
			  (u16) (how_long % (u32) 1000));

	/* And now we must get the interpreter again */

	status = acpi_ex_enter_interpreter ();
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_system_acquire_mutex
 *
 * PARAMETERS:  *time_desc          - The 'time to delay' object descriptor
 *              *obj_desc           - The object descriptor for this op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Provides an access point to perform synchronization operations
 *              within the AML.  This function will cause a lock to be generated
 *              for the Mutex pointed to by obj_desc.
 *
 ******************************************************************************/

acpi_status
acpi_ex_system_acquire_mutex (
	acpi_operand_object     *time_desc,
	acpi_operand_object     *obj_desc)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE_PTR ("ex_system_acquire_mutex", obj_desc);


	if (!obj_desc) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * Support for the _GL_ Mutex object -- go get the global lock
	 */
	if (obj_desc->mutex.semaphore == acpi_gbl_global_lock_semaphore) {
		status = acpi_ev_acquire_global_lock ((u16) time_desc->integer.value);
		return_ACPI_STATUS (status);
	}

	status = acpi_ex_system_wait_semaphore (obj_desc->mutex.semaphore,
			  (u16) time_desc->integer.value);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_system_release_mutex
 *
 * PARAMETERS:  *obj_desc           - The object descriptor for this op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Provides an access point to perform synchronization operations
 *              within the AML.  This operation is a request to release a
 *              previously acquired Mutex.  If the Mutex variable is set then
 *              it will be decremented.
 *
 ******************************************************************************/

acpi_status
acpi_ex_system_release_mutex (
	acpi_operand_object     *obj_desc)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("ex_system_release_mutex");


	if (!obj_desc) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * Support for the _GL_ Mutex object -- release the global lock
	 */
	if (obj_desc->mutex.semaphore == acpi_gbl_global_lock_semaphore) {
		status = acpi_ev_release_global_lock ();
		return_ACPI_STATUS (status);
	}

	status = acpi_os_signal_semaphore (obj_desc->mutex.semaphore, 1);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_system_signal_event
 *
 * PARAMETERS:  *obj_desc           - The object descriptor for this op
 *
 * RETURN:      AE_OK
 *
 * DESCRIPTION: Provides an access point to perform synchronization operations
 *              within the AML.
 *
 ******************************************************************************/

acpi_status
acpi_ex_system_signal_event (
	acpi_operand_object     *obj_desc)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("ex_system_signal_event");


	if (obj_desc) {
		status = acpi_os_signal_semaphore (obj_desc->event.semaphore, 1);
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_system_wait_event
 *
 * PARAMETERS:  *time_desc          - The 'time to delay' object descriptor
 *              *obj_desc           - The object descriptor for this op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Provides an access point to perform synchronization operations
 *              within the AML.  This operation is a request to wait for an
 *              event.
 *
 ******************************************************************************/

acpi_status
acpi_ex_system_wait_event (
	acpi_operand_object     *time_desc,
	acpi_operand_object     *obj_desc)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("ex_system_wait_event");


	if (obj_desc) {
		status = acpi_ex_system_wait_semaphore (obj_desc->event.semaphore,
				  (u16) time_desc->integer.value);
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_system_reset_event
 *
 * PARAMETERS:  *obj_desc           - The object descriptor for this op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Reset an event to a known state.
 *
 ******************************************************************************/

acpi_status
acpi_ex_system_reset_event (
	acpi_operand_object     *obj_desc)
{
	acpi_status             status = AE_OK;
	void                    *temp_semaphore;


	ACPI_FUNCTION_ENTRY ();


	/*
	 * We are going to simply delete the existing semaphore and
	 * create a new one!
	 */
	status = acpi_os_create_semaphore (ACPI_NO_UNIT_LIMIT, 0, &temp_semaphore);
	if (ACPI_SUCCESS (status)) {
		(void) acpi_os_delete_semaphore (obj_desc->event.semaphore);
		obj_desc->event.semaphore = temp_semaphore;
	}

	return (status);
}

