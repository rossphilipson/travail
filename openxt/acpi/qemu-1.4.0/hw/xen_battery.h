/*
 * Battery management for OpenXT guests.
 *
 * Copyright (C) 2014 Citrix Systems Ltd
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

#ifndef XEN_BATTERY_H_
# define XEN_BATTERY_H_

# include "hw/pc.h"
# include <stdint.h>

/* XenClient: battery
 * This function is use for the initialization of the device.
 * This device is dedicated to be linked to the ACPI device.
 * Then, the device should be the same used to initialize the ACPI */
int32_t xen_battery_init(PCIDevice *device);

/* Let the user choose if he wants to use Xen Battery or not */
enum xen_battery_options_type {
    XEN_BATTERY_NONE = 0,
    XEN_BATTERY_XENSTORE = 1,
};

/* XenClient: battery
 * Set the kind of way to emulate or not the Xen Battery device */
void xen_battery_set_option(unsigned int const opt);

/* XenClient: battery
 * Get if the user ask for the battery emulation */
bool xen_battery_get_option(void);

#endif /* !XEN_BATTERY_H_ */
