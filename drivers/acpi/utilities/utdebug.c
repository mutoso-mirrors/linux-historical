/******************************************************************************
 *
 * Module Name: utdebug - Debug print routines
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

#define _COMPONENT          ACPI_UTILITIES
	 ACPI_MODULE_NAME    ("utdebug")


#ifdef ACPI_DEBUG_OUTPUT

static u32   acpi_gbl_prev_thread_id = 0xFFFFFFFF;
static char     *acpi_gbl_fn_entry_str = "----Entry";
static char     *acpi_gbl_fn_exit_str = "----Exit-";


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_init_stack_ptr_trace
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Save the current stack pointer
 *
 ****************************************************************************/

void
acpi_ut_init_stack_ptr_trace (
	void)
{
	u32                 current_sp;


	acpi_gbl_entry_stack_pointer = ACPI_PTR_DIFF (&current_sp, NULL);
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_track_stack_ptr
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Save the current stack pointer
 *
 ****************************************************************************/

void
acpi_ut_track_stack_ptr (
	void)
{
	acpi_size           current_sp;


	current_sp = ACPI_PTR_DIFF (&current_sp, NULL);

	if (current_sp < acpi_gbl_lowest_stack_pointer) {
		acpi_gbl_lowest_stack_pointer = current_sp;
	}

	if (acpi_gbl_nesting_level > acpi_gbl_deepest_nesting) {
		acpi_gbl_deepest_nesting = acpi_gbl_nesting_level;
	}
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_debug_print
 *
 * PARAMETERS:  debug_level         - Requested debug print level
 *              proc_name           - Caller's procedure name
 *              module_name         - Caller's module name (for error output)
 *              line_number         - Caller's line number (for error output)
 *              component_id        - Caller's component ID (for error output)
 *
 *              Format              - Printf format field
 *              ...                 - Optional printf arguments
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message with prefix consisting of the module name,
 *              line number, and component ID.
 *
 ****************************************************************************/

void  ACPI_INTERNAL_VAR_XFACE
acpi_ut_debug_print (
	u32                     requested_debug_level,
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	char                    *format,
	...)
{
	u32                     thread_id;
	va_list                 args;


	/*
	 * Stay silent if the debug level or component ID is disabled
	 */
	if (!(requested_debug_level & acpi_dbg_level) ||
		!(dbg_info->component_id & acpi_dbg_layer)) {
		return;
	}

	/*
	 * Thread tracking and context switch notification
	 */
	thread_id = acpi_os_get_thread_id ();

	if (thread_id != acpi_gbl_prev_thread_id) {
		if (ACPI_LV_THREADS & acpi_dbg_level) {
			acpi_os_printf ("\n**** Context Switch from TID %X to TID %X ****\n\n",
				acpi_gbl_prev_thread_id, thread_id);
		}

		acpi_gbl_prev_thread_id = thread_id;
	}

	/*
	 * Display the module name, current line number, thread ID (if requested),
	 * current procedure nesting level, and the current procedure name
	 */
	acpi_os_printf ("%8s-%04ld ", dbg_info->module_name, line_number);

	if (ACPI_LV_THREADS & acpi_dbg_level) {
		acpi_os_printf ("[%04lX] ", thread_id);
	}

	acpi_os_printf ("[%02ld] %-22.22s: ", acpi_gbl_nesting_level, dbg_info->proc_name);

	va_start (args, format);
	acpi_os_vprintf (format, args);
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_debug_print_raw
 *
 * PARAMETERS:  requested_debug_level - Requested debug print level
 *              line_number         - Caller's line number
 *              dbg_info            - Contains:
 *                  proc_name           - Caller's procedure name
 *                  module_name         - Caller's module name
 *                  component_id        - Caller's component ID
 *              Format              - Printf format field
 *              ...                 - Optional printf arguments
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print message with no headers.  Has same interface as
 *              debug_print so that the same macros can be used.
 *
 ****************************************************************************/

void  ACPI_INTERNAL_VAR_XFACE
acpi_ut_debug_print_raw (
	u32                     requested_debug_level,
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	char                    *format,
	...)
{
	va_list                 args;


	if (!(requested_debug_level & acpi_dbg_level) ||
		!(dbg_info->component_id & acpi_dbg_layer)) {
		return;
	}

	va_start (args, format);
	acpi_os_vprintf (format, args);
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_trace
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              dbg_info            - Contains:
 *                  proc_name           - Caller's procedure name
 *                  module_name         - Caller's module name
 *                  component_id        - Caller's component ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function entry trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level
 *
 ****************************************************************************/

void
acpi_ut_trace (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info)
{

	acpi_gbl_nesting_level++;
	acpi_ut_track_stack_ptr ();

	acpi_ut_debug_print (ACPI_LV_FUNCTIONS, line_number, dbg_info,
			"%s\n", acpi_gbl_fn_entry_str);
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_trace_ptr
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              dbg_info            - Contains:
 *                  proc_name           - Caller's procedure name
 *                  module_name         - Caller's module name
 *                  component_id        - Caller's component ID
 *              Pointer             - Pointer to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function entry trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level
 *
 ****************************************************************************/

void
acpi_ut_trace_ptr (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	void                    *pointer)
{
	acpi_gbl_nesting_level++;
	acpi_ut_track_stack_ptr ();

	acpi_ut_debug_print (ACPI_LV_FUNCTIONS, line_number, dbg_info,
			"%s %p\n", acpi_gbl_fn_entry_str, pointer);
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_trace_str
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              dbg_info            - Contains:
 *                  proc_name           - Caller's procedure name
 *                  module_name         - Caller's module name
 *                  component_id        - Caller's component ID
 *              String              - Additional string to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function entry trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level
 *
 ****************************************************************************/

void
acpi_ut_trace_str (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	char                    *string)
{

	acpi_gbl_nesting_level++;
	acpi_ut_track_stack_ptr ();

	acpi_ut_debug_print (ACPI_LV_FUNCTIONS, line_number, dbg_info,
			"%s %s\n", acpi_gbl_fn_entry_str, string);
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_trace_u32
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              dbg_info            - Contains:
 *                  proc_name           - Caller's procedure name
 *                  module_name         - Caller's module name
 *                  component_id        - Caller's component ID
 *              Integer             - Integer to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function entry trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level
 *
 ****************************************************************************/

void
acpi_ut_trace_u32 (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	u32                     integer)
{

	acpi_gbl_nesting_level++;
	acpi_ut_track_stack_ptr ();

	acpi_ut_debug_print (ACPI_LV_FUNCTIONS, line_number, dbg_info,
			"%s %08X\n", acpi_gbl_fn_entry_str, integer);
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_exit
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              dbg_info            - Contains:
 *                  proc_name           - Caller's procedure name
 *                  module_name         - Caller's module name
 *                  component_id        - Caller's component ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level
 *
 ****************************************************************************/

void
acpi_ut_exit (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info)
{

	acpi_ut_debug_print (ACPI_LV_FUNCTIONS, line_number, dbg_info,
			"%s\n", acpi_gbl_fn_exit_str);

	acpi_gbl_nesting_level--;
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_status_exit
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              dbg_info            - Contains:
 *                  proc_name           - Caller's procedure name
 *                  module_name         - Caller's module name
 *                  component_id        - Caller's component ID
 *              Status              - Exit status code
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level. Prints exit status also.
 *
 ****************************************************************************/

void
acpi_ut_status_exit (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	acpi_status             status)
{

	if (ACPI_SUCCESS (status)) {
		acpi_ut_debug_print (ACPI_LV_FUNCTIONS, line_number, dbg_info,
				"%s %s\n", acpi_gbl_fn_exit_str,
				acpi_format_exception (status));
	}
	else {
		acpi_ut_debug_print (ACPI_LV_FUNCTIONS, line_number, dbg_info,
				"%s ****Exception****: %s\n", acpi_gbl_fn_exit_str,
				acpi_format_exception (status));
	}

	acpi_gbl_nesting_level--;
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_value_exit
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              dbg_info            - Contains:
 *                  proc_name           - Caller's procedure name
 *                  module_name         - Caller's module name
 *                  component_id        - Caller's component ID
 *              Value               - Value to be printed with exit msg
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level. Prints exit value also.
 *
 ****************************************************************************/

void
acpi_ut_value_exit (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	acpi_integer            value)
{

	acpi_ut_debug_print (ACPI_LV_FUNCTIONS, line_number, dbg_info,
			"%s %8.8X%8.8X\n", acpi_gbl_fn_exit_str,
			ACPI_HIDWORD (value), ACPI_LODWORD (value));

	acpi_gbl_nesting_level--;
}


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_ptr_exit
 *
 * PARAMETERS:  line_number         - Caller's line number
 *              dbg_info            - Contains:
 *                  proc_name           - Caller's procedure name
 *                  module_name         - Caller's module name
 *                  component_id        - Caller's component ID
 *              Value               - Value to be printed with exit msg
 *
 * RETURN:      None
 *
 * DESCRIPTION: Function exit trace.  Prints only if TRACE_FUNCTIONS bit is
 *              set in debug_level. Prints exit value also.
 *
 ****************************************************************************/

void
acpi_ut_ptr_exit (
	u32                     line_number,
	acpi_debug_print_info   *dbg_info,
	u8                      *ptr)
{

	acpi_ut_debug_print (ACPI_LV_FUNCTIONS, line_number, dbg_info,
			"%s %p\n", acpi_gbl_fn_exit_str, ptr);

	acpi_gbl_nesting_level--;
}

#endif


/*****************************************************************************
 *
 * FUNCTION:    acpi_ut_dump_buffer
 *
 * PARAMETERS:  Buffer              - Buffer to dump
 *              Count               - Amount to dump, in bytes
 *              Display             - BYTE, WORD, DWORD, or QWORD display
 *              component_iD        - Caller's component ID
 *
 * RETURN:      None
 *
 * DESCRIPTION: Generic dump buffer in both hex and ascii.
 *
 ****************************************************************************/

void
acpi_ut_dump_buffer (
	u8                      *buffer,
	u32                     count,
	u32                     display,
	u32                     component_id)
{
	acpi_native_uint        i = 0;
	acpi_native_uint        j;
	u32                     temp32;
	u8                      buf_char;


	/* Only dump the buffer if tracing is enabled */

	if (!((ACPI_LV_TABLES & acpi_dbg_level) &&
		(component_id & acpi_dbg_layer))) {
		return;
	}

	if ((count < 4) || (count & 0x01)) {
		display = DB_BYTE_DISPLAY;
	}

	acpi_os_printf ("\nOffset Value\n");

	/*
	 * Nasty little dump buffer routine!
	 */
	while (i < count) {
		/* Print current offset */

		acpi_os_printf ("%05X  ", (u32) i);

		/* Print 16 hex chars */

		for (j = 0; j < 16;) {
			if (i + j >= count) {
				acpi_os_printf ("\n");
				return;
			}

			/* Make sure that the s8 doesn't get sign-extended! */

			switch (display) {
			/* Default is BYTE display */

			default:

				acpi_os_printf ("%02X ",
						*((u8 *) &buffer[i + j]));
				j += 1;
				break;


			case DB_WORD_DISPLAY:

				ACPI_MOVE_UNALIGNED16_TO_32 (&temp32,
						   &buffer[i + j]);
				acpi_os_printf ("%04X ", temp32);
				j += 2;
				break;


			case DB_DWORD_DISPLAY:

				ACPI_MOVE_UNALIGNED32_TO_32 (&temp32,
						   &buffer[i + j]);
				acpi_os_printf ("%08X ", temp32);
				j += 4;
				break;


			case DB_QWORD_DISPLAY:

				ACPI_MOVE_UNALIGNED32_TO_32 (&temp32,
						   &buffer[i + j]);
				acpi_os_printf ("%08X", temp32);

				ACPI_MOVE_UNALIGNED32_TO_32 (&temp32,
						   &buffer[i + j + 4]);
				acpi_os_printf ("%08X ", temp32);
				j += 8;
				break;
			}
		}

		/*
		 * Print the ASCII equivalent characters
		 * But watch out for the bad unprintable ones...
		 */
		for (j = 0; j < 16; j++) {
			if (i + j >= count) {
				acpi_os_printf ("\n");
				return;
			}

			buf_char = buffer[i + j];
			if ((buf_char > 0x1F && buf_char < 0x2E) ||
				(buf_char > 0x2F && buf_char < 0x61) ||
				(buf_char > 0x60 && buf_char < 0x7F)) {
				acpi_os_printf ("%c", buf_char);
			}
			else {
				acpi_os_printf (".");
			}
		}

		/* Done with that line. */

		acpi_os_printf ("\n");
		i += 16;
	}

	return;
}

