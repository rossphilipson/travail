/*
 * Copyright (c) 2013 Citrix Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdlib.h>
#include <string.h>
#include "device.h"
#include "dm-agent.h"
#include "spawn.h"
#include "util.h"

#define qemu_device_init(devname, parse)                        \
static struct device dev_##devname =                            \
{                                                               \
    .name = #devname,                                           \
    .type = DEVMODEL_SPAWN,                                     \
    .subtype = SPAWN_QEMU,                                      \
    .parse_options = parse,                                     \
};                                                              \
                                                                \
device_init (dev_##devname)

static bool serial_device_parse_options (struct device_model *devmodel,
                                         const char *device)
{
    char *serial = device_option (devmodel, device, "device");
    bool res = true;

    if (!serial)
    {
        device_error (devmodel, device, "missing device option to create serial device");
        return false;
    }

    /* Disable pty serial in stubdomain: fails */
    if (dm_agent_in_stubdom () && !strcmp (serial, "pty"))
        goto end_serial;

    res = spawn_add_argument (devmodel, "-serial");
    if (!res)
        goto end_serial;

    res = spawn_add_argument (devmodel, serial);

end_serial:
    free (serial);

    return res;
}

qemu_device_init (serial, serial_device_parse_options)

static bool audio_device_parse_options (struct device_model *devmodel,
                                        const char *device)
{
    char *audio;
    char *recorder;
    bool res = false;

    audio = retrieve_option (devmodel, device, "device", devaudio);
    recorder = retrieve_option (devmodel, device, "recorder", recaudio);

    SPAWN_ADD_ARGL (devmodel, end_audio, "-soundhw");
    SPAWN_ADD_ARGL (devmodel, end_audio, audio);

    /* If we don't find a "1" we disable audio record */
    if (strcmp ("1", recorder))
        SPAWN_ADD_ARGL (devmodel, end_audio, "-disable-audio-rec");

    res = true;

end_audio:
    free (recorder);
recaudio:
    free (audio);
devaudio:
    return res;
}

qemu_device_init (audio, audio_device_parse_options)

static bool acpi_device_parse_options (struct device_model *devmodel,
                                       const char *device)
{
    return true;

    (void) device;

    return spawn_add_argument (devmodel, "-acpi");
}

qemu_device_init (acpi, acpi_device_parse_options);

static bool svga_device_parse_options (struct device_model *devmodel,
                                       const char *device)
{
    (void) device;

    SPAWN_ADD_ARG (devmodel, "-vga");
    SPAWN_ADD_ARG (devmodel, "std");
    SPAWN_ADD_ARG (devmodel, "-display");
    SPAWN_ADD_ARG (devmodel, "surfman");

    return true;
}

qemu_device_init (svga, svga_device_parse_options);

static bool cdrom_device_parse_options (struct device_model *devmodel,
                                        const char *device)
{
    char *devicepath;
    char *option = NULL;
    bool res = false;

    option = device_option (devmodel, device, "option");
    devicepath = retrieve_option (devmodel, device, "device", cdromdevice);

    if (!option) {
        SPAWN_ADD_ARG (devmodel, "-cdrom");
        SPAWN_ADD_ARG (devmodel, "%s", devicepath);
    } else {
        char *ro = "off";

        /* check to see if disk is read-only */
        if (strcmp(option, "pt-ro-exclusive") == 0) {
            ro = "on";
        }

        SPAWN_ADD_ARG (devmodel, "-drive");
        SPAWN_ADD_ARG (devmodel,
                       "file=%s:%s,media=cdrom,if=atapi-pt,format=raw,readonly=%s",
                       dm_agent_in_stubdom() ?
                       "atapi-pt-v4v" : "atapi-pt-local", devicepath, ro);

        free (option);
    }

    res = true;

    free (devicepath);
cdromdevice:
    return res;
}

qemu_device_init (cdrom, cdrom_device_parse_options);

static bool net_device_parse_options (struct device_model *devmodel,
                                      const char *device)
{
    char *id;
    char *model;
    char *bridge;
    char *mac;
    char *name;
    bool res = false;

    id = retrieve_option (devmodel, device, "id", netid);
    model = retrieve_option (devmodel, device, "model", netmodel);
    bridge = retrieve_option (devmodel, device, "bridge", netbridge);
    mac = retrieve_option (devmodel, device, "mac", netmac);
    name = retrieve_option (devmodel, device, "name", netname);

    SPAWN_ADD_ARGL (devmodel, end_net, "-net");
    SPAWN_ADD_ARGL (devmodel, end_net, "nic,vlan=%s,name=%s,macaddr=%s,model=%s",
                    id, name, mac, model);

    SPAWN_ADD_ARGL (devmodel, end_net, "-net");
    SPAWN_ADD_ARGL (devmodel, end_net, "tap,vlan=%s,ifname=tap%u.%s,script=/etc/qemu/qemu-ifup",
                    id, devmodel->domain->domid, id);

    res = true;

end_net:
    free (name);
netname:
    free (mac);
netmac:
    free (bridge);
netbridge:
    free (model);
netmodel:
    free (id);
netid:
    return res;
}

qemu_device_init (net, net_device_parse_options);

static  bool gfx_device_parse_options (struct device_model *devmodel,
                                       const char *device)
{
    return true;

    (void) device;

    SPAWN_ADD_ARG (devmodel, "-gfx_passthru");
    SPAWN_ADD_ARG (devmodel, "-surfman");

    return true;
}

qemu_device_init (gfx, gfx_device_parse_options);

static bool vgpu_device_parse_options (struct device_model *devmodel,
                                       const char *device)
{
    return true;

    (void) device;

    SPAWN_ADD_ARG (devmodel, "-vgpu");
    SPAWN_ADD_ARG (devmodel, "-surfman");

    return true;
}

qemu_device_init (vgpu, vgpu_device_parse_options);

static bool drive_device_parse_options (struct device_model *devmodel,
                                        const char *device)
{
    char *file;
    char *media;
    char *format;
    char *index;
    char *readonly;
    bool res = false;

    file = retrieve_option (devmodel, device, "file", drivefile);
    media = retrieve_option (devmodel, device, "media", drivemedia);
    format = retrieve_option (devmodel, device, "format", driveformat);
    index = retrieve_option (devmodel, device, "index", driveindex);
    readonly = retrieve_option (devmodel, device, "readonly", drivereadonly);

    SPAWN_ADD_ARGL (devmodel, end_drive, "-drive");

    if ((strcmp(readonly, "on") == 0) || (strcmp(media, "cdrom") == 0)) {
        SPAWN_ADD_ARGL (devmodel, end_drive,
                "file=%s,if=ide,index=%s,media=%s,format=%s,readonly=on",
                file, index, media, format);
    } else {
        SPAWN_ADD_ARGL (devmodel, end_drive,
                "file=%s,if=ide,index=%s,media=%s,format=%s,readonly=off",
                file, index, media, format);
    }

    res = true;

end_drive:
    free (readonly);
drivereadonly:
    free (index);
driveindex:
    free (format);
driveformat:
    free (media);
drivemedia:
    free (file);
drivefile:
    return res;
}

qemu_device_init (drive, drive_device_parse_options);

static  bool xenmou_device_parse_options (struct device_model *devmodel,
                                          const char *device)
{
    (void) device;

    SPAWN_ADD_ARG (devmodel, "-device");
    SPAWN_ADD_ARG (devmodel, "xenmou");

    return true;
}

qemu_device_init (xenmou, xenmou_device_parse_options);

static  bool xen_acpi_pm_device_parse_options (struct device_model *devmodel,
                                               const char *device)
{
    (void) device;

    SPAWN_ADD_ARG (devmodel, "-xen-acpi-pm");

    return true;
}

qemu_device_init (xen_acpi_pm, xen_acpi_pm_device_parse_options);

static  bool xen_pci_pt_device_parse_options (struct device_model *devmodel,
                                              const char *device)
{
    char *hostaddr = NULL;
    bool res = false;

    hostaddr = retrieve_option (devmodel, device, "hostaddr", xen_pci_pt_hostaddr);

    SPAWN_ADD_ARGL (devmodel, end_xen_pci_pt, "-device");
    SPAWN_ADD_ARGL (devmodel, end_xen_pci_pt,
                    "xen-pci-passthrough,hostaddr=%s", hostaddr);

    res = true;
end_xen_pci_pt:
    free(hostaddr);
xen_pci_pt_hostaddr:
    return res;
}

qemu_device_init (xen_pci_pt, xen_pci_pt_device_parse_options);
