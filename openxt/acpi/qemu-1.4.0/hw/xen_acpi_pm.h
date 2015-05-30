/*
 * APCI PM feature for battery/AC/lid management for OpenXT guests.
 *
 * Copyright (C) 2014 Citrix Systems Ltd
 * Copyright (c) 2015, Assured Information Security, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef XEN_ACPI_PM_H_
#define XEN_ACPI_PM_H_

/*
 * This function is use for the initialization of the battery/AC/lid devices.
 * These device are linked to the ACPI devices in the ssdt_pm.asl module.
 */
void xen_acpi_pm_create(MemoryRegion *space_io, void *opaque);

/* Set to enable the Xen ACPI PM device support */
void xen_acpi_pm_set_enabled(bool enable);

/* Get enabled/disable state of the ACPI PM device support */
bool xen_acpi_pm_get_enabled(void);

#endif /* !XEN_ACPI_PM_H_ */
