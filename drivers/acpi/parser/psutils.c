/******************************************************************************
 *
 * Module Name: psutils - Parser miscellaneous utilities (Parser only)
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


#include <acpi/acpi.h>
#include <acpi/acparser.h>
#include <acpi/amlcode.h>
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_PARSER
	 ACPI_MODULE_NAME    ("psutils")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_create_scope_op
 *
 * PARAMETERS:  None
 *
 * RETURN:      scope_op
 *
 * DESCRIPTION: Create a Scope and associated namepath op with the root name
 *
 ******************************************************************************/

union acpi_parse_object *
acpi_ps_create_scope_op (
	void)
{
	union acpi_parse_object         *scope_op;


	scope_op = acpi_ps_alloc_op (AML_SCOPE_OP);
	if (!scope_op) {
		return (NULL);
	}


	scope_op->named.name = ACPI_ROOT_NAME;
	return (scope_op);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_init_op
 *
 * PARAMETERS:  Op              - A newly allocated Op object
 *              Opcode          - Opcode to store in the Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Allocate an acpi_op, choose op type (and thus size) based on
 *              opcode
 *
 ******************************************************************************/

void
acpi_ps_init_op (
	union acpi_parse_object         *op,
	u16                             opcode)
{
	ACPI_FUNCTION_ENTRY ();


	op->common.data_type = ACPI_DESC_TYPE_PARSER;
	op->common.aml_opcode = opcode;

	ACPI_DISASM_ONLY_MEMBERS (ACPI_STRNCPY (op->common.aml_op_name,
			(acpi_ps_get_opcode_info (opcode))->name, sizeof (op->common.aml_op_name)));
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_alloc_op
 *
 * PARAMETERS:  Opcode          - Opcode that will be stored in the new Op
 *
 * RETURN:      Pointer to the new Op.
 *
 * DESCRIPTION: Allocate an acpi_op, choose op type (and thus size) based on
 *              opcode.  A cache of opcodes is available for the pure
 *              GENERIC_OP, since this is by far the most commonly used.
 *
 ******************************************************************************/

union acpi_parse_object*
acpi_ps_alloc_op (
	u16                             opcode)
{
	union acpi_parse_object         *op = NULL;
	u32                             size;
	u8                              flags;
	const struct acpi_opcode_info   *op_info;


	ACPI_FUNCTION_ENTRY ();


	op_info = acpi_ps_get_opcode_info (opcode);

	/* Allocate the minimum required size object */

	if (op_info->flags & AML_DEFER) {
		size = sizeof (struct acpi_parse_obj_named);
		flags = ACPI_PARSEOP_DEFERRED;
	}
	else if (op_info->flags & AML_NAMED) {
		size = sizeof (struct acpi_parse_obj_named);
		flags = ACPI_PARSEOP_NAMED;
	}
	else if (opcode == AML_INT_BYTELIST_OP) {
		size = sizeof (struct acpi_parse_obj_named);
		flags = ACPI_PARSEOP_BYTELIST;
	}
	else {
		size = sizeof (struct acpi_parse_obj_common);
		flags = ACPI_PARSEOP_GENERIC;
	}

	if (size == sizeof (struct acpi_parse_obj_common)) {
		/*
		 * The generic op is by far the most common (16 to 1)
		 */
		op = acpi_ut_acquire_from_cache (ACPI_MEM_LIST_PSNODE);
	}
	else {
		op = acpi_ut_acquire_from_cache (ACPI_MEM_LIST_PSNODE_EXT);
	}

	/* Initialize the Op */

	if (op) {
		acpi_ps_init_op (op, opcode);
		op->common.flags = flags;
	}

	return (op);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_free_op
 *
 * PARAMETERS:  Op              - Op to be freed
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Free an Op object.  Either put it on the GENERIC_OP cache list
 *              or actually free it.
 *
 ******************************************************************************/

void
acpi_ps_free_op (
	union acpi_parse_object         *op)
{
	ACPI_FUNCTION_NAME ("ps_free_op");


	if (op->common.aml_opcode == AML_INT_RETURN_VALUE_OP) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, "Free retval op: %p\n", op));
	}

	if (op->common.flags & ACPI_PARSEOP_GENERIC) {
		acpi_ut_release_to_cache (ACPI_MEM_LIST_PSNODE, op);
	}
	else {
		acpi_ut_release_to_cache (ACPI_MEM_LIST_PSNODE_EXT, op);
	}
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_delete_parse_cache
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Free all objects that are on the parse cache list.
 *
 ******************************************************************************/

void
acpi_ps_delete_parse_cache (
	void)
{
	ACPI_FUNCTION_TRACE ("ps_delete_parse_cache");


	acpi_ut_delete_generic_cache (ACPI_MEM_LIST_PSNODE);
	acpi_ut_delete_generic_cache (ACPI_MEM_LIST_PSNODE_EXT);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Utility functions
 *
 * DESCRIPTION: Low level character and object functions
 *
 ******************************************************************************/


/*
 * Is "c" a namestring lead character?
 */
u8
acpi_ps_is_leading_char (
	u32                             c)
{
	return ((u8) (c == '_' || (c >= 'A' && c <= 'Z')));
}


/*
 * Is "c" a namestring prefix character?
 */
u8
acpi_ps_is_prefix_char (
	u32                             c)
{
	return ((u8) (c == '\\' || c == '^'));
}


/*
 * Get op's name (4-byte name segment) or 0 if unnamed
 */
u32
acpi_ps_get_name (
	union acpi_parse_object         *op)
{


	/* The "generic" object has no name associated with it */

	if (op->common.flags & ACPI_PARSEOP_GENERIC) {
		return (0);
	}

	/* Only the "Extended" parse objects have a name */

	return (op->named.name);
}


/*
 * Set op's name
 */
void
acpi_ps_set_name (
	union acpi_parse_object         *op,
	u32                             name)
{

	/* The "generic" object has no name associated with it */

	if (op->common.flags & ACPI_PARSEOP_GENERIC) {
		return;
	}

	op->named.name = name;
}

