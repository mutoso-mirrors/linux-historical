/******************************************************************************
 *
 * Module Name: exfield - ACPI AML (p-code) execution - field manipulation
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
#include "acdispat.h"
#include "acinterp.h"


#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exfield")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_read_data_from_field
 *
 * PARAMETERS:  Walk_state          - Current execution state
 *              Obj_desc            - The named field
 *              Ret_buffer_desc     - Where the return data object is stored
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read from a named field.  Returns either an Integer or a
 *              Buffer, depending on the size of the field.
 *
 ******************************************************************************/

acpi_status
acpi_ex_read_data_from_field (
	acpi_walk_state         *walk_state,
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **ret_buffer_desc)
{
	acpi_status             status;
	acpi_operand_object     *buffer_desc;
	acpi_size               length;
	void                    *buffer;
	u8                      locked;


	ACPI_FUNCTION_TRACE_PTR ("Ex_read_data_from_field", obj_desc);


	/* Parameter validation */

	if (!obj_desc) {
		return_ACPI_STATUS (AE_AML_NO_OPERAND);
	}

	if (ACPI_GET_OBJECT_TYPE (obj_desc) == ACPI_TYPE_BUFFER_FIELD) {
		/*
		 * If the Buffer_field arguments have not been previously evaluated,
		 * evaluate them now and save the results.
		 */
		if (!(obj_desc->common.flags & AOPOBJ_DATA_VALID)) {
			status = acpi_ds_get_buffer_field_arguments (obj_desc);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
		}
	}
	else if ((ACPI_GET_OBJECT_TYPE (obj_desc) == ACPI_TYPE_LOCAL_REGION_FIELD) &&
			 (obj_desc->field.region_obj->region.space_id == ACPI_ADR_SPACE_SMBUS)) {
		/*
		 * This is an SMBus read.  We must create a buffer to hold the data
		 * and directly access the region handler.
		 */
		buffer_desc = acpi_ut_create_buffer_object (ACPI_SMBUS_BUFFER_SIZE);
		if (!buffer_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* Lock entire transaction if requested */

		locked = acpi_ex_acquire_global_lock (obj_desc->common_field.field_flags);

		/*
		 * Perform the read.
		 * Note: Smbus protocol value is passed in upper 16-bits of Function
		 */
		status = acpi_ex_access_region (obj_desc, 0,
				  ACPI_CAST_PTR (acpi_integer, buffer_desc->buffer.pointer),
				  ACPI_READ | (obj_desc->field.attribute << 16));
		acpi_ex_release_global_lock (locked);
		goto exit;
	}

	/*
	 * Allocate a buffer for the contents of the field.
	 *
	 * If the field is larger than the size of an acpi_integer, create
	 * a BUFFER to hold it.  Otherwise, use an INTEGER.  This allows
	 * the use of arithmetic operators on the returned value if the
	 * field size is equal or smaller than an Integer.
	 *
	 * Note: Field.length is in bits.
	 */
	length = (acpi_size) ACPI_ROUND_BITS_UP_TO_BYTES (obj_desc->field.bit_length);
	if (length > acpi_gbl_integer_byte_width) {
		/* Field is too large for an Integer, create a Buffer instead */

		buffer_desc = acpi_ut_create_buffer_object (length);
		if (!buffer_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}
		buffer = buffer_desc->buffer.pointer;
	}
	else {
		/* Field will fit within an Integer (normal case) */

		buffer_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!buffer_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		length = acpi_gbl_integer_byte_width;
		buffer_desc->integer.value = 0;
		buffer = &buffer_desc->integer.value;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
		"Obj=%p Type=%X Buf=%p Len=%X\n",
		obj_desc, ACPI_GET_OBJECT_TYPE (obj_desc), buffer, (u32) length));
	ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
		"Field_write: Bit_len=%X Bit_off=%X Byte_off=%X\n",
		obj_desc->common_field.bit_length,
		obj_desc->common_field.start_field_bit_offset,
		obj_desc->common_field.base_byte_offset));

	/* Lock entire transaction if requested */

	locked = acpi_ex_acquire_global_lock (obj_desc->common_field.field_flags);

	/* Read from the field */

	status = acpi_ex_extract_from_field (obj_desc, buffer, (u32) length);
	acpi_ex_release_global_lock (locked);


exit:
	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (buffer_desc);
	}
	else if (ret_buffer_desc) {
		*ret_buffer_desc = buffer_desc;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_write_data_to_field
 *
 * PARAMETERS:  Source_desc         - Contains data to write
 *              Obj_desc            - The named field
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Write to a named field
 *
 ******************************************************************************/

acpi_status
acpi_ex_write_data_to_field (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **result_desc)
{
	acpi_status             status;
	u32                     length;
	u32                     required_length;
	void                    *buffer;
	void                    *new_buffer;
	u8                      locked;
	acpi_operand_object     *buffer_desc;


	ACPI_FUNCTION_TRACE_PTR ("Ex_write_data_to_field", obj_desc);


	/* Parameter validation */

	if (!source_desc || !obj_desc) {
		return_ACPI_STATUS (AE_AML_NO_OPERAND);
	}

	if (ACPI_GET_OBJECT_TYPE (obj_desc) == ACPI_TYPE_BUFFER_FIELD) {
		/*
		 * If the Buffer_field arguments have not been previously evaluated,
		 * evaluate them now and save the results.
		 */
		if (!(obj_desc->common.flags & AOPOBJ_DATA_VALID)) {
			status = acpi_ds_get_buffer_field_arguments (obj_desc);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
		}
	}
	else if ((ACPI_GET_OBJECT_TYPE (obj_desc) == ACPI_TYPE_LOCAL_REGION_FIELD) &&
			 (obj_desc->field.region_obj->region.space_id == ACPI_ADR_SPACE_SMBUS)) {
		/*
		 * This is an SMBus write.  We will bypass the entire field mechanism
		 * and handoff the buffer directly to the handler.
		 *
		 * Source must be a buffer of sufficient size (ACPI_SMBUS_BUFFER_SIZE).
		 */
		if (ACPI_GET_OBJECT_TYPE (source_desc) != ACPI_TYPE_BUFFER) {
			ACPI_REPORT_ERROR (("SMBus write requires Buffer, found type %s\n",
				acpi_ut_get_object_type_name (source_desc)));
			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}

		if (source_desc->buffer.length < ACPI_SMBUS_BUFFER_SIZE) {
			ACPI_REPORT_ERROR (("SMBus write requires Buffer of length %X, found length %X\n",
				ACPI_SMBUS_BUFFER_SIZE, source_desc->buffer.length));
			return_ACPI_STATUS (AE_AML_BUFFER_LIMIT);
		}

		buffer_desc = acpi_ut_create_buffer_object (ACPI_SMBUS_BUFFER_SIZE);
		if (!buffer_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		buffer = buffer_desc->buffer.pointer;
		ACPI_MEMCPY (buffer, source_desc->buffer.pointer, ACPI_SMBUS_BUFFER_SIZE);

		/* Lock entire transaction if requested */

		locked = acpi_ex_acquire_global_lock (obj_desc->common_field.field_flags);

		/*
		 * Perform the write (returns status and perhaps data in the same buffer)
		 * Note: SMBus protocol type is passed in upper 16-bits of Function.
		 */
		status = acpi_ex_access_region (obj_desc, 0,
				  (acpi_integer *) buffer,
				  ACPI_WRITE | (obj_desc->field.attribute << 16));
		acpi_ex_release_global_lock (locked);

		*result_desc = buffer_desc;
		return_ACPI_STATUS (status);
	}

	/*
	 * Get a pointer to the data to be written
	 */
	switch (ACPI_GET_OBJECT_TYPE (source_desc)) {
	case ACPI_TYPE_INTEGER:
		buffer = &source_desc->integer.value;
		length = sizeof (source_desc->integer.value);
		break;

	case ACPI_TYPE_BUFFER:
		buffer = source_desc->buffer.pointer;
		length = source_desc->buffer.length;
		break;

	case ACPI_TYPE_STRING:
		buffer = source_desc->string.pointer;
		length = source_desc->string.length;
		break;

	default:
		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
	}

	/*
	 * We must have a buffer that is at least as long as the field
	 * we are writing to.  This is because individual fields are
	 * indivisible and partial writes are not supported -- as per
	 * the ACPI specification.
	 */
	new_buffer = NULL;
	required_length = ACPI_ROUND_BITS_UP_TO_BYTES (obj_desc->common_field.bit_length);

	if (length < required_length) {
		/* We need to create a new buffer */

		new_buffer = ACPI_MEM_CALLOCATE (required_length);
		if (!new_buffer) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/*
		 * Copy the original data to the new buffer, starting
		 * at Byte zero.  All unused (upper) bytes of the
		 * buffer will be 0.
		 */
		ACPI_MEMCPY ((char *) new_buffer, (char *) buffer, length);
		buffer = new_buffer;
		length = required_length;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
		"Obj=%p Type=%X Buf=%p Len=%X\n",
		obj_desc, ACPI_GET_OBJECT_TYPE (obj_desc), buffer, length));
	ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
		"Field_read: Bit_len=%X Bit_off=%X Byte_off=%X\n",
		obj_desc->common_field.bit_length,
		obj_desc->common_field.start_field_bit_offset,
		obj_desc->common_field.base_byte_offset));

	/* Lock entire transaction if requested */

	locked = acpi_ex_acquire_global_lock (obj_desc->common_field.field_flags);

	/* Write to the field */

	status = acpi_ex_insert_into_field (obj_desc, buffer, length);
	acpi_ex_release_global_lock (locked);

	/* Free temporary buffer if we used one */

	if (new_buffer) {
		ACPI_MEM_FREE (new_buffer);
	}

	return_ACPI_STATUS (status);
}


