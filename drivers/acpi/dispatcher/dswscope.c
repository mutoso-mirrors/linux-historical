/******************************************************************************
 *
 * Module Name: dswscope - Scope stack manipulation
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
#include "acdispat.h"


#define _COMPONENT          ACPI_DISPATCHER
	 ACPI_MODULE_NAME    ("dswscope")


#define STACK_POP(head) head


/****************************************************************************
 *
 * FUNCTION:    acpi_ds_scope_stack_clear
 *
 * PARAMETERS:  None
 *
 * DESCRIPTION: Pop (and free) everything on the scope stack except the
 *              root scope object (which remains at the stack top.)
 *
 ***************************************************************************/

void
acpi_ds_scope_stack_clear (
	struct acpi_walk_state          *walk_state)
{
	union acpi_generic_state        *scope_info;

	ACPI_FUNCTION_NAME ("ds_scope_stack_clear");


	while (walk_state->scope_info) {
		/* Pop a scope off the stack */

		scope_info = walk_state->scope_info;
		walk_state->scope_info = scope_info->scope.next;

		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
			"Popped object type (%s)\n", acpi_ut_get_type_name (scope_info->common.value)));
		acpi_ut_delete_generic_state (scope_info);
	}
}


/****************************************************************************
 *
 * FUNCTION:    acpi_ds_scope_stack_push
 *
 * PARAMETERS:  *Node,              - Name to be made current
 *              Type,               - Type of frame being pushed
 *
 * DESCRIPTION: Push the current scope on the scope stack, and make the
 *              passed Node current.
 *
 ***************************************************************************/

acpi_status
acpi_ds_scope_stack_push (
	struct acpi_namespace_node      *node,
	acpi_object_type                type,
	struct acpi_walk_state          *walk_state)
{
	union acpi_generic_state        *scope_info;
	union acpi_generic_state        *old_scope_info;


	ACPI_FUNCTION_TRACE ("ds_scope_stack_push");


	if (!node) {
		/* Invalid scope   */

		ACPI_REPORT_ERROR (("ds_scope_stack_push: null scope passed\n"));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Make sure object type is valid */

	if (!acpi_ut_valid_object_type (type)) {
		ACPI_REPORT_WARNING (("ds_scope_stack_push: type code out of range\n"));
	}


	/* Allocate a new scope object */

	scope_info = acpi_ut_create_generic_state ();
	if (!scope_info) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Init new scope object */

	scope_info->common.data_type = ACPI_DESC_TYPE_STATE_WSCOPE;
	scope_info->scope.node      = node;
	scope_info->common.value    = (u16) type;

	walk_state->scope_depth++;

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
		"[%.2d] Pushed scope ", (u32) walk_state->scope_depth));

	old_scope_info = walk_state->scope_info;
	if (old_scope_info) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EXEC,
			"[%4.4s] (%10s)",
			old_scope_info->scope.node->name.ascii,
			acpi_ut_get_type_name (old_scope_info->common.value)));
	}
	else {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EXEC,
			"[\\___] (%10s)", "ROOT"));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EXEC,
		", New scope -> [%4.4s] (%s)\n",
		scope_info->scope.node->name.ascii,
		acpi_ut_get_type_name (scope_info->common.value)));

	/* Push new scope object onto stack */

	acpi_ut_push_generic_state (&walk_state->scope_info, scope_info);

	return_ACPI_STATUS (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    acpi_ds_scope_stack_pop
 *
 * PARAMETERS:  Type                - The type of frame to be found
 *
 * DESCRIPTION: Pop the scope stack until a frame of the requested type
 *              is found.
 *
 * RETURN:      Count of frames popped.  If no frame of the requested type
 *              was found, the count is returned as a negative number and
 *              the scope stack is emptied (which sets the current scope
 *              to the root).  If the scope stack was empty at entry, the
 *              function is a no-op and returns 0.
 *
 ***************************************************************************/

acpi_status
acpi_ds_scope_stack_pop (
	struct acpi_walk_state          *walk_state)
{
	union acpi_generic_state        *scope_info;
	union acpi_generic_state        *new_scope_info;


	ACPI_FUNCTION_TRACE ("ds_scope_stack_pop");


	/*
	 * Pop scope info object off the stack.
	 */
	scope_info = acpi_ut_pop_generic_state (&walk_state->scope_info);
	if (!scope_info) {
		return_ACPI_STATUS (AE_STACK_UNDERFLOW);
	}

	walk_state->scope_depth--;

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
		"[%.2d] Popped scope [%4.4s] (%10s), New scope -> ",
		(u32) walk_state->scope_depth,
		scope_info->scope.node->name.ascii,
		acpi_ut_get_type_name (scope_info->common.value)));

	new_scope_info = walk_state->scope_info;
	if (new_scope_info) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EXEC,
			"[%4.4s] (%s)\n",
			new_scope_info->scope.node->name.ascii,
			acpi_ut_get_type_name (new_scope_info->common.value)));
	}
	else {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EXEC,
			"[\\___] (ROOT)\n"));
	}

	acpi_ut_delete_generic_state (scope_info);

	return_ACPI_STATUS (AE_OK);
}


