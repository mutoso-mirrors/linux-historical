
/******************************************************************************
 *
 * Module Name: exsystem - Interface to OS services
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2003, R. Byron Moore
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */


#include <acpi/acpi.h>
#include <acpi/acinterp.h>
#include <acpi/acevents.h>

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
	acpi_handle                     semaphore,
	u16                             timeout)
{
	acpi_status                     status;
	acpi_status                     status2;


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
 * PARAMETERS:  how_long            - The amount of time to stall,
 *                                    in microseconds
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Suspend running thread for specified amount of time.
 *              Note: ACPI specification requires that Stall() does not
 *              relinquish the processor, and delays longer than 100 usec
 *              should use Sleep() instead.  We allow stalls up to 255 usec
 *              for compatibility with other interpreters and existing BIOSs.
 *
 ******************************************************************************/

acpi_status
acpi_ex_system_do_stall (
	u32                             how_long)
{
	acpi_status                     status = AE_OK;


	ACPI_FUNCTION_ENTRY ();


	if (how_long > 255) /* 255 microseconds */ {
		/*
		 * Longer than 255 usec, this is an error
		 *
		 * (ACPI specifies 100 usec as max, but this gives some slack in
		 * order to support existing BIOSs)
		 */
		ACPI_REPORT_ERROR (("Stall: Time parameter is too large (%d)\n", how_long));
		status = AE_AML_OPERAND_VALUE;
	}
	else {
		acpi_os_stall (how_long);
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_system_do_suspend
 *
 * PARAMETERS:  how_long            - The amount of time to suspend,
 *                                    in milliseconds
 *
 * RETURN:      None
 *
 * DESCRIPTION: Suspend running thread for specified amount of time.
 *
 ******************************************************************************/

acpi_status
acpi_ex_system_do_suspend (
	u32                             how_long)
{
	acpi_status                     status;


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
	union acpi_operand_object       *time_desc,
	union acpi_operand_object       *obj_desc)
{
	acpi_status                     status = AE_OK;


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
	union acpi_operand_object       *obj_desc)
{
	acpi_status                     status = AE_OK;


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
	union acpi_operand_object       *obj_desc)
{
	acpi_status                     status = AE_OK;


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
	union acpi_operand_object       *time_desc,
	union acpi_operand_object       *obj_desc)
{
	acpi_status                     status = AE_OK;


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
	union acpi_operand_object       *obj_desc)
{
	acpi_status                     status = AE_OK;
	void                            *temp_semaphore;


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

