
/******************************************************************************
 *
 * Name: acpixf.h - External interfaces to the ACPI subsystem
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


#ifndef __ACXFACE_H__
#define __ACXFACE_H__

#include <acpi/actypes.h>
#include <acpi/actbl.h>


 /*
 * Global interfaces
 */

acpi_status
acpi_initialize_subsystem (
	void);

acpi_status
acpi_enable_subsystem (
	u32                             flags);

acpi_status
acpi_initialize_objects (
	u32                             flags);

acpi_status
acpi_terminate (
	void);

acpi_status
acpi_subsystem_status (
	void);

acpi_status
acpi_enable (
	void);

acpi_status
acpi_disable (
	void);

acpi_status
acpi_get_system_info (
	struct acpi_buffer              *ret_buffer);

const char *
acpi_format_exception (
	acpi_status                     exception);

acpi_status
acpi_purge_cached_objects (
	void);

acpi_status
acpi_install_initialization_handler (
	acpi_init_handler               handler,
	u32                             function);

/*
 * ACPI Memory manager
 */

void *
acpi_allocate (
	u32                             size);

void *
acpi_callocate (
	u32                             size);

void
acpi_free (
	void                            *address);


/*
 * ACPI table manipulation interfaces
 */

acpi_status
acpi_find_root_pointer (
	u32                             flags,
	struct acpi_pointer             *rsdp_address);

acpi_status
acpi_load_tables (
	void);

acpi_status
acpi_load_table (
	struct acpi_table_header        *table_ptr);

acpi_status
acpi_unload_table (
	acpi_table_type                 table_type);

acpi_status
acpi_get_table_header (
	acpi_table_type                 table_type,
	u32                             instance,
	struct acpi_table_header        *out_table_header);

acpi_status
acpi_get_table (
	acpi_table_type                 table_type,
	u32                             instance,
	struct acpi_buffer              *ret_buffer);

acpi_status
acpi_get_firmware_table (
	acpi_string                     signature,
	u32                             instance,
	u32                             flags,
	struct acpi_table_header        **table_pointer);


/*
 * Namespace and name interfaces
 */

acpi_status
acpi_walk_namespace (
	acpi_object_type                type,
	acpi_handle                     start_object,
	u32                             max_depth,
	acpi_walk_callback              user_function,
	void                            *context,
	void                            **return_value);

acpi_status
acpi_get_devices (
	char                            *HID,
	acpi_walk_callback              user_function,
	void                            *context,
	void                            **return_value);

acpi_status
acpi_get_name (
	acpi_handle                     handle,
	u32                             name_type,
	struct acpi_buffer              *ret_path_ptr);

acpi_status
acpi_get_handle (
	acpi_handle                     parent,
	acpi_string                     pathname,
	acpi_handle                     *ret_handle);

acpi_status
acpi_attach_data (
	acpi_handle                     obj_handle,
	acpi_object_handler             handler,
	void                            *data);

acpi_status
acpi_detach_data (
	acpi_handle                     obj_handle,
	acpi_object_handler             handler);

acpi_status
acpi_get_data (
	acpi_handle                     obj_handle,
	acpi_object_handler             handler,
	void                            **data);


/*
 * Object manipulation and enumeration
 */

acpi_status
acpi_evaluate_object (
	acpi_handle                     object,
	acpi_string                     pathname,
	struct acpi_object_list         *parameter_objects,
	struct acpi_buffer              *return_object_buffer);

acpi_status
acpi_evaluate_object_typed (
	acpi_handle                     object,
	acpi_string                     pathname,
	struct acpi_object_list         *external_params,
	struct acpi_buffer              *return_buffer,
	acpi_object_type                return_type);

acpi_status
acpi_get_object_info (
	acpi_handle                     device,
	struct acpi_device_info         *info);

acpi_status
acpi_get_next_object (
	acpi_object_type                type,
	acpi_handle                     parent,
	acpi_handle                     child,
	acpi_handle                     *out_handle);

acpi_status
acpi_get_type (
	acpi_handle                     object,
	acpi_object_type                *out_type);

acpi_status
acpi_get_parent (
	acpi_handle                     object,
	acpi_handle                     *out_handle);


/*
 * Event handler interfaces
 */

acpi_status
acpi_install_fixed_event_handler (
	u32                             acpi_event,
	acpi_event_handler              handler,
	void                            *context);

acpi_status
acpi_remove_fixed_event_handler (
	u32                             acpi_event,
	acpi_event_handler              handler);

acpi_status
acpi_install_notify_handler (
	acpi_handle                     device,
	u32                             handler_type,
	acpi_notify_handler             handler,
	void                            *context);

acpi_status
acpi_remove_notify_handler (
	acpi_handle                     device,
	u32                             handler_type,
	acpi_notify_handler             handler);

acpi_status
acpi_install_address_space_handler (
	acpi_handle                     device,
	acpi_adr_space_type             space_id,
	acpi_adr_space_handler          handler,
	acpi_adr_space_setup            setup,
	void                            *context);

acpi_status
acpi_remove_address_space_handler (
	acpi_handle                     device,
	acpi_adr_space_type             space_id,
	acpi_adr_space_handler          handler);

acpi_status
acpi_install_gpe_handler (
	u32                             gpe_number,
	u32                             type,
	acpi_gpe_handler                handler,
	void                            *context);

acpi_status
acpi_acquire_global_lock (
	u16                             timeout,
	u32                             *handle);

acpi_status
acpi_release_global_lock (
	u32                             handle);

acpi_status
acpi_remove_gpe_handler (
	u32                             gpe_number,
	acpi_gpe_handler                handler);

acpi_status
acpi_enable_event (
	u32                             acpi_event,
	u32                             type,
	u32                             flags);

acpi_status
acpi_disable_event (
	u32                             acpi_event,
	u32                             type,
	u32                             flags);

acpi_status
acpi_clear_event (
	u32                             acpi_event,
	u32                             type);

acpi_status
acpi_get_event_status (
	u32                             acpi_event,
	u32                             type,
	acpi_event_status               *event_status);

/*
 * Resource interfaces
 */

acpi_status
acpi_get_current_resources(
	acpi_handle                     device_handle,
	struct acpi_buffer              *ret_buffer);

acpi_status
acpi_get_possible_resources(
	acpi_handle                     device_handle,
	struct acpi_buffer              *ret_buffer);

acpi_status
acpi_set_current_resources (
	acpi_handle                     device_handle,
	struct acpi_buffer              *in_buffer);

acpi_status
acpi_get_irq_routing_table (
	acpi_handle                     bus_device_handle,
	struct acpi_buffer              *ret_buffer);


/*
 * Hardware (ACPI device) interfaces
 */

acpi_status
acpi_get_register (
	u32                             register_id,
	u32                             *return_value,
	u32                             flags);

acpi_status
acpi_set_register (
	u32                             register_id,
	u32                             value,
	u32                             flags);

acpi_status
acpi_set_firmware_waking_vector (
	acpi_physical_address           physical_address);

acpi_status
acpi_get_firmware_waking_vector (
	acpi_physical_address           *physical_address);

acpi_status
acpi_get_sleep_type_data (
	u8                              sleep_state,
	u8                              *slp_typ_a,
	u8                              *slp_typ_b);

acpi_status
acpi_enter_sleep_state_prep (
	u8                              sleep_state);

acpi_status
acpi_enter_sleep_state (
	u8                              sleep_state);

acpi_status
acpi_leave_sleep_state (
	u8                              sleep_state);


#endif /* __ACXFACE_H__ */
