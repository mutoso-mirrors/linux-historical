/******************************************************************************
 *
 * Module Name: nsinit - namespace initialization
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2003, R. Byron Moore
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
#include "acnamesp.h"
#include "acdispat.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_NAMESPACE
	 ACPI_MODULE_NAME    ("nsinit")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_initialize_objects
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk the entire namespace and perform any necessary
 *              initialization on the objects found therein
 *
 ******************************************************************************/

acpi_status
acpi_ns_initialize_objects (
	void)
{
	acpi_status                     status;
	struct acpi_init_walk_info      info;


	ACPI_FUNCTION_TRACE ("ns_initialize_objects");


	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"**** Starting initialization of namespace objects ****\n"));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT, "Completing Region/Field/Buffer/Package initialization:"));

	/* Set all init info to zero */

	ACPI_MEMSET (&info, 0, sizeof (struct acpi_init_walk_info));

	/* Walk entire namespace from the supplied root */

	status = acpi_walk_namespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
			  ACPI_UINT32_MAX, acpi_ns_init_one_object,
			  &info, NULL);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "walk_namespace failed! %s\n",
			acpi_format_exception (status)));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
		"\nInitialized %hd/%hd Regions %hd/%hd Fields %hd/%hd Buffers %hd/%hd Packages (%hd nodes)\n",
		info.op_region_init, info.op_region_count,
		info.field_init,    info.field_count,
		info.buffer_init,   info.buffer_count,
		info.package_init,  info.package_count, info.object_count));

	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"%hd Control Methods found\n", info.method_count));
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"%hd Op Regions found\n", info.op_region_count));

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_initialize_devices
 *
 * PARAMETERS:  None
 *
 * RETURN:      acpi_status
 *
 * DESCRIPTION: Walk the entire namespace and initialize all ACPI devices.
 *              This means running _INI on all present devices.
 *
 *              Note: We install PCI config space handler on region access,
 *              not here.
 *
 ******************************************************************************/

acpi_status
acpi_ns_initialize_devices (
	void)
{
	acpi_status                     status;
	struct acpi_device_walk_info    info;


	ACPI_FUNCTION_TRACE ("ns_initialize_devices");


	/* Init counters */

	info.device_count = 0;
	info.num_STA = 0;
	info.num_INI = 0;

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT, "Executing all Device _STA and_INI methods:"));

	/* Walk namespace for all objects of type Device */

	status = acpi_ns_walk_namespace (ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
			  ACPI_UINT32_MAX, FALSE, acpi_ns_init_one_device, &info, NULL);

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "walk_namespace failed! %s\n",
			acpi_format_exception (status)));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT,
		"\n%hd Devices found containing: %hd _STA, %hd _INI methods\n",
		info.device_count, info.num_STA, info.num_INI));

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_init_one_object
 *
 * PARAMETERS:  obj_handle      - Node
 *              Level           - Current nesting level
 *              Context         - Points to a init info struct
 *              return_value    - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Callback from acpi_walk_namespace. Invoked for every object
 *              within the  namespace.
 *
 *              Currently, the only objects that require initialization are:
 *              1) Methods
 *              2) Op Regions
 *
 ******************************************************************************/

acpi_status
acpi_ns_init_one_object (
	acpi_handle                     obj_handle,
	u32                             level,
	void                            *context,
	void                            **return_value)
{
	acpi_object_type                type;
	acpi_status                     status;
	struct acpi_init_walk_info      *info = (struct acpi_init_walk_info *) context;
	struct acpi_namespace_node      *node = (struct acpi_namespace_node *) obj_handle;
	union acpi_operand_object       *obj_desc;


	ACPI_FUNCTION_NAME ("ns_init_one_object");


	info->object_count++;

	/* And even then, we are only interested in a few object types */

	type = acpi_ns_get_type (obj_handle);
	obj_desc = acpi_ns_get_attached_object (node);
	if (!obj_desc) {
		return (AE_OK);
	}

	/* Increment counters for object types we are looking for */

	switch (type) {
	case ACPI_TYPE_REGION:
		info->op_region_count++;
		break;

	case ACPI_TYPE_BUFFER_FIELD:
		info->field_count++;
		break;

	case ACPI_TYPE_BUFFER:
		info->buffer_count++;
		break;

	case ACPI_TYPE_PACKAGE:
		info->package_count++;
		break;

	default:

		/* No init required, just exit now */
		return (AE_OK);
	}

	/*
	 * If the object is already initialized, nothing else to do
	 */
	if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
		return (AE_OK);
	}

	/*
	 * Must lock the interpreter before executing AML code
	 */
	status = acpi_ex_enter_interpreter ();
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/*
	 * Each of these types can contain executable AML code within
	 * the declaration.
	 */
	switch (type) {
	case ACPI_TYPE_REGION:

		info->op_region_init++;
		status = acpi_ds_get_region_arguments (obj_desc);
		break;


	case ACPI_TYPE_BUFFER_FIELD:

		info->field_init++;
		status = acpi_ds_get_buffer_field_arguments (obj_desc);
		break;


	case ACPI_TYPE_BUFFER:

		info->buffer_init++;
		status = acpi_ds_get_buffer_arguments (obj_desc);
		break;


	case ACPI_TYPE_PACKAGE:

		info->package_init++;
		status = acpi_ds_get_package_arguments (obj_desc);
		break;

	default:
		/* No other types can get here */
		break;
	}

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ERROR, "\n"));
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Could not execute arguments for [%4.4s] (%s), %s\n",
				node->name.ascii, acpi_ut_get_type_name (type), acpi_format_exception (status)));
	}

	/* Print a dot for each object unless we are going to print the entire pathname */

	if (!(acpi_dbg_level & ACPI_LV_INIT_NAMES)) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT, "."));
	}

	/*
	 * We ignore errors from above, and always return OK, since
	 * we don't want to abort the walk on any single error.
	 */
	acpi_ex_exit_interpreter ();
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ns_init_one_device
 *
 * PARAMETERS:  acpi_walk_callback
 *
 * RETURN:      acpi_status
 *
 * DESCRIPTION: This is called once per device soon after ACPI is enabled
 *              to initialize each device. It determines if the device is
 *              present, and if so, calls _INI.
 *
 ******************************************************************************/

acpi_status
acpi_ns_init_one_device (
	acpi_handle                     obj_handle,
	u32                             nesting_level,
	void                            *context,
	void                            **return_value)
{
	acpi_status                     status;
	struct acpi_namespace_node     *node;
	u32                             flags;
	struct acpi_device_walk_info   *info = (struct acpi_device_walk_info *) context;


	ACPI_FUNCTION_TRACE ("ns_init_one_device");


	if ((acpi_dbg_level <= ACPI_LV_ALL_EXCEPTIONS) && (!(acpi_dbg_level & ACPI_LV_INFO))) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INIT, "."));
	}

	info->device_count++;

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	node = acpi_ns_map_handle_to_node (obj_handle);
	if (!node) {
		(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	status = acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * Run _STA to determine if we can run _INI on the device.
	 */
	ACPI_DEBUG_EXEC (acpi_ut_display_init_pathname (ACPI_TYPE_METHOD, node, "_STA"));
	status = acpi_ut_execute_STA (node, &flags);
	if (ACPI_FAILURE (status)) {
		/* Ignore error and move on to next device */

		return_ACPI_STATUS (AE_OK);
	}

	info->num_STA++;

	if (!(flags & 0x01)) {
		/* don't look at children of a not present device */

		return_ACPI_STATUS(AE_CTRL_DEPTH);
	}

	/*
	 * The device is present. Run _INI.
	 */
	ACPI_DEBUG_EXEC (acpi_ut_display_init_pathname (ACPI_TYPE_METHOD, obj_handle, "_INI"));
	status = acpi_ns_evaluate_relative (obj_handle, "_INI", NULL, NULL);
	if (ACPI_FAILURE (status)) {
		/* No _INI (AE_NOT_FOUND) means device requires no initialization */

		if (status != AE_NOT_FOUND) {
			/* Ignore error and move on to next device */

	#ifdef ACPI_DEBUG_OUTPUT
			char                *scope_name = acpi_ns_get_external_pathname (obj_handle);

			ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "%s._INI failed: %s\n",
					scope_name, acpi_format_exception (status)));

			ACPI_MEM_FREE (scope_name);
	#endif
		}

		status = AE_OK;
	}
	else {
		/* Count of successful INIs */

		info->num_INI++;
	}

	if (acpi_gbl_init_handler) {
		/* External initialization handler is present, call it */

		status = acpi_gbl_init_handler (obj_handle, ACPI_INIT_DEVICE_INI);
	}


	return_ACPI_STATUS (status);
}
