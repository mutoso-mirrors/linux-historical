/*
 * linux/drivers/s390/scsi/zfcp_sysfs_adapter.c
 *
 * FCP adapter driver for IBM eServer zSeries
 *
 * sysfs adapter related routines
 *
 * (C) Copyright IBM Corp. 2003, 2004
 *
 * Authors:
 *      Martin Peschke <mpeschke@de.ibm.com>
 *	Heiko Carstens <heiko.carstens@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define ZFCP_SYSFS_ADAPTER_C_REVISION "$Revision: 1.30 $"

#include <asm/ccwdev.h>
#include "zfcp_ext.h"
#include "zfcp_def.h"

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_CONFIG

static const char fc_topologies[5][25] = {
	{"<error>"},
	{"point-to-point"},
	{"fabric"},
	{"arbitrated loop"},
	{"fabric (virt. adapter)"}
};

/**
 * ZFCP_DEFINE_ADAPTER_ATTR
 * @_name:   name of show attribute
 * @_format: format string
 * @_value:  value to print
 *
 * Generates attributes for an adapter.
 */
#define ZFCP_DEFINE_ADAPTER_ATTR(_name, _format, _value)                      \
static ssize_t zfcp_sysfs_adapter_##_name##_show(struct device *dev,          \
						 char *buf)                   \
{                                                                             \
	struct zfcp_adapter *adapter;                                         \
                                                                              \
	adapter = dev_get_drvdata(dev);                                       \
	return sprintf(buf, _format, _value);                                 \
}                                                                             \
                                                                              \
static DEVICE_ATTR(_name, S_IRUGO, zfcp_sysfs_adapter_##_name##_show, NULL);

ZFCP_DEFINE_ADAPTER_ATTR(status, "0x%08x\n", atomic_read(&adapter->status));
ZFCP_DEFINE_ADAPTER_ATTR(wwnn, "0x%016llx\n", adapter->wwnn);
ZFCP_DEFINE_ADAPTER_ATTR(wwpn, "0x%016llx\n", adapter->wwpn);
ZFCP_DEFINE_ADAPTER_ATTR(s_id, "0x%06x\n", adapter->s_id);
ZFCP_DEFINE_ADAPTER_ATTR(card_version, "0x%04x\n", adapter->hydra_version);
ZFCP_DEFINE_ADAPTER_ATTR(lic_version, "0x%08x\n", adapter->fsf_lic_version);
ZFCP_DEFINE_ADAPTER_ATTR(fc_link_speed, "%d Gb/s\n", adapter->fc_link_speed);
ZFCP_DEFINE_ADAPTER_ATTR(fc_service_class, "%d\n", adapter->fc_service_class);
ZFCP_DEFINE_ADAPTER_ATTR(fc_topology, "%s\n",
			 fc_topologies[adapter->fc_topology]);
ZFCP_DEFINE_ADAPTER_ATTR(hardware_version, "0x%08x\n",
			 adapter->hardware_version);
ZFCP_DEFINE_ADAPTER_ATTR(serial_number, "%17s\n", adapter->serial_number);

/**
 * zfcp_sysfs_adapter_in_recovery_show - recovery state of adapter
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 *
 * Show function of "in_recovery" attribute of adapter. Will be
 * "0" if no error recovery is pending for adapter, otherwise "1".
 */
static ssize_t
zfcp_sysfs_adapter_in_recovery_show(struct device *dev, char *buf)
{
	struct zfcp_adapter *adapter;

	adapter = dev_get_drvdata(dev);
	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_INUSE, &adapter->status))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static DEVICE_ATTR(in_recovery, S_IRUGO,
		   zfcp_sysfs_adapter_in_recovery_show, NULL);

/**
 * zfcp_sysfs_adapter_scsi_host_no_show - display scsi_host_no of adapter
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 *
 * "scsi_host_no" attribute of adapter. Displays the SCSI host number.
 */
static ssize_t
zfcp_sysfs_adapter_scsi_host_no_show(struct device *dev, char *buf)
{
	struct zfcp_adapter *adapter;
	unsigned short host_no = 0;

	down(&zfcp_data.config_sema);
	adapter = dev_get_drvdata(dev);
	if (adapter->scsi_host)
		host_no = adapter->scsi_host->host_no;
	up(&zfcp_data.config_sema);
	return sprintf(buf, "0x%x\n", host_no);
}

static DEVICE_ATTR(scsi_host_no, S_IRUGO, zfcp_sysfs_adapter_scsi_host_no_show,
		   NULL);

/**
 * zfcp_sysfs_port_add_store - add a port to sysfs tree
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 * @count: number of bytes in buffer
 *
 * Store function of the "port_add" attribute of an adapter.
 */
static ssize_t
zfcp_sysfs_port_add_store(struct device *dev, const char *buf, size_t count)
{
	wwn_t wwpn;
	char *endp;
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	int retval = -EINVAL;

	down(&zfcp_data.config_sema);

	adapter = dev_get_drvdata(dev);
	if (atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status)) {
		retval = -EBUSY;
		goto out;
	}

	wwpn = simple_strtoull(buf, &endp, 0);
	if ((endp + 1) < (buf + count))
		goto out;

	port = zfcp_port_enqueue(adapter, wwpn, 0);
	if (!port)
		goto out;

	retval = 0;

	zfcp_erp_port_reopen(port, 0);
	zfcp_erp_wait(port->adapter);
	zfcp_port_put(port);
 out:
	up(&zfcp_data.config_sema);
	return retval ? retval : count;
}

static DEVICE_ATTR(port_add, S_IWUSR, NULL, zfcp_sysfs_port_add_store);

/**
 * zfcp_sysfs_port_remove_store - remove a port from sysfs tree
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 * @count: number of bytes in buffer
 *
 * Store function of the "port_remove" attribute of an adapter.
 */
static ssize_t
zfcp_sysfs_port_remove_store(struct device *dev, const char *buf, size_t count)
{
	struct zfcp_adapter *adapter;
	struct zfcp_port *port;
	wwn_t wwpn;
	char *endp;
	int retval = 0;

	down(&zfcp_data.config_sema);

	adapter = dev_get_drvdata(dev);
	if (atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status)) {
		retval = -EBUSY;
		goto out;
	}

	wwpn = simple_strtoull(buf, &endp, 0);
	if ((endp + 1) < (buf + count)) {
		retval = -EINVAL;
		goto out;
	}

	write_lock_irq(&zfcp_data.config_lock);
	port = zfcp_get_port_by_wwpn(adapter, wwpn);
	if (port && (atomic_read(&port->refcount) == 0)) {
		zfcp_port_get(port);
		atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status);
		list_move(&port->list, &adapter->port_remove_lh);
	}
	else {
		port = NULL;
	}
	write_unlock_irq(&zfcp_data.config_lock);

	if (!port) {
		retval = -ENXIO;
		goto out;
	}

	zfcp_erp_port_shutdown(port, 0);
	zfcp_erp_wait(adapter);
	zfcp_port_put(port);
	zfcp_sysfs_port_remove_files(&port->sysfs_device,
				     atomic_read(&port->status));
	device_unregister(&port->sysfs_device);
 out:
	up(&zfcp_data.config_sema);
	return retval ? retval : count;
}

static DEVICE_ATTR(port_remove, S_IWUSR, NULL, zfcp_sysfs_port_remove_store);

/**
 * zfcp_sysfs_adapter_failed_store - failed state of adapter
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 * @count: number of bytes in buffer
 *
 * Store function of the "failed" attribute of an adapter.
 * If a "0" gets written to "failed", error recovery will be
 * started for the belonging adapter.
 */
static ssize_t
zfcp_sysfs_adapter_failed_store(struct device *dev,
				const char *buf, size_t count)
{
	struct zfcp_adapter *adapter;
	unsigned int val;
	char *endp;
	int retval = 0;

	down(&zfcp_data.config_sema);

	adapter = dev_get_drvdata(dev);
	if (atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &adapter->status)) {
		retval = -EBUSY;
		goto out;
	}

	val = simple_strtoul(buf, &endp, 0);
	if (((endp + 1) < (buf + count)) || (val != 0)) {
		retval = -EINVAL;
		goto out;
	}

	zfcp_erp_modify_adapter_status(adapter, ZFCP_STATUS_COMMON_RUNNING,
				       ZFCP_SET);
	zfcp_erp_adapter_reopen(adapter, ZFCP_STATUS_COMMON_ERP_FAILED);
	zfcp_erp_wait(adapter);
 out:
	up(&zfcp_data.config_sema);
	return retval ? retval : count;
}

/**
 * zfcp_sysfs_adapter_failed_show - failed state of adapter
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 *
 * Show function of "failed" attribute of adapter. Will be
 * "0" if adapter is working, otherwise "1".
 */
static ssize_t
zfcp_sysfs_adapter_failed_show(struct device *dev, char *buf)
{
	struct zfcp_adapter *adapter;

	adapter = dev_get_drvdata(dev);
	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &adapter->status))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static DEVICE_ATTR(failed, S_IWUSR | S_IRUGO, zfcp_sysfs_adapter_failed_show,
		   zfcp_sysfs_adapter_failed_store);

static struct attribute *zfcp_adapter_attrs[] = {
	&dev_attr_failed.attr,
	&dev_attr_in_recovery.attr,
	&dev_attr_port_remove.attr,
	&dev_attr_port_add.attr,
	&dev_attr_wwnn.attr,
	&dev_attr_wwpn.attr,
	&dev_attr_s_id.attr,
	&dev_attr_card_version.attr,
	&dev_attr_lic_version.attr,
	&dev_attr_fc_link_speed.attr,
	&dev_attr_fc_service_class.attr,
	&dev_attr_fc_topology.attr,
	&dev_attr_scsi_host_no.attr,
	&dev_attr_status.attr,
	&dev_attr_hardware_version.attr,
	&dev_attr_serial_number.attr,
	NULL
};

static struct attribute_group zfcp_adapter_attr_group = {
	.attrs = zfcp_adapter_attrs,
};

/**
 * zfcp_sysfs_create_adapter_files - create sysfs adapter files
 * @dev: pointer to belonging device
 *
 * Create all attributes of the sysfs representation of an adapter.
 */
int
zfcp_sysfs_adapter_create_files(struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &zfcp_adapter_attr_group);
}

/**
 * zfcp_sysfs_remove_adapter_files - remove sysfs adapter files
 * @dev: pointer to belonging device
 *
 * Remove all attributes of the sysfs representation of an adapter.
 */
void
zfcp_sysfs_adapter_remove_files(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &zfcp_adapter_attr_group);
}

#undef ZFCP_LOG_AREA
