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

/* TODO
 * Fix the status register mess
 * Remove references to XenClient
 * Fix error logging and tracing
 * Move AC and lid state out of the battery overlord
 * Use ACQR/REL to make a common status port - is it feasible?
 * _BIF is deprecated in ACPI 4.0: See ACPI spec (chap 10.2.2.1), add the _BIX.
 */

#include <stdint.h>
#include "include/qemu-common.h"
#include "xen_backend.h"
#include "xen.h"
#include "pci/pci.h"
#include "hw/pc.h"
#include "hw/xen_acpi_pm.h"

/* Uncomment the following line to have debug messages about
 * Battery Management */
/* #define XEN_BATTERY_DEBUG */

#ifdef XEN_BATTERY_DEBUG
# define XBM_DPRINTF(fmt, ...)                            \
    do {                                                  \
        fprintf(stderr, "[BATTERY][%s(%d)]: " fmt,        \
                __func__, __LINE__, ## __VA_ARGS__);      \
    } while (0)
#else
# define XBM_DPRINTF(fmt, ...)                            \
    { }
#endif

# define XBM_ERROR_MSG(fmt, ...)                          \
    do {                                                  \
        fprintf(stderr, "[BATTERY][ERROR][%s(%d)]: " fmt, \
                __func__, __LINE__, ## __VA_ARGS__);      \
    } while (0)

#define MAX_BATTERIES              2

#define BATTERY_PORT_1             0xb2 /* Battery command port */
#define BATTERY_PORT_2             0x86 /* Battery data port */
#define BATTERY_PORT_3             0x88 /* Battery 1 (BAT0) status port */ 
#define BATTERY_PORT_4             0x90 /* Battery 2 (BAT1) status port */ 
#define BATTERY_PORT_5             0xb4 /* Battery selector port */

#define BATTERY_OP_INIT            0x7b /* Battery operation init */
#define BATTERY_OP_SET_INFO_TYPE   0x7c /* Battery operation type */
#define BATTERY_OP_GET_DATA_LENGTH 0x79 /* Battery operation data length */
#define BATTERY_OP_GET_DATA        0x7d /* Battery operation data read */

#define ACPI_PM_STATUS_PORT        0x9c /* General ACPI PM status port */

/* Describes the different type of MODE managed by this module */
enum xen_battery_mode {
    XEN_BATTERY_MODE_NONE = 0,
    XEN_BATTERY_MODE_PT,
    XEN_BATTERY_MODE_HVM
};

enum xen_battery_selector {
    XEN_BATTERY_TYPE_NONE = 0,
    XEN_BATTERY_TYPE_BIF,
    XEN_BATTERY_TYPE_BST
};

/* From each battery, xenstore provides the Battery Status (_bst) and the
 * battery informatiom (_bif).
 *
 */
struct battery_buffer {
    char *_bst;           /* _BST */
    char *_bif;           /* _BIF */
    uint8_t port_b2_val;  /* Variable to manage BATTERY_PORT_1 */
    uint8_t port_86_val;  /* Variable to manage BATTERY_PORT_2 */
    uint8_t index;        /* Index inside the _BST or _BIF string */
    uint8_t bif_changed;
    /* Selector to mark which buffer we should use */
    enum xen_battery_selector _selector;
};

struct xen_battery_manager {
    enum xen_battery_mode mode; /* /[...]/xen_extended_power_mgmt */
    uint8_t battery_present;    /* /pm/battery_present */
    struct battery_buffer batteries[MAX_BATTERIES]; /* Battery array */
    uint8_t index;              /* Battery selector */
    MemoryRegion mr[5];         /* MemoryRegion to register IO ops */
};

#define TYPE_XEN_ACPI_PM_DEVICE "xen-acpi-pm-device"
#define XEN_ACPI_PM_DEVICE(obj) \
    OBJECT_CHECK(XenACPIPMState, (obj), TYPE_XEN_ACPI_PM_DEVICE)

typedef struct XenACPIPMState {
    DeviceState qdev;

    PCIDevice *pci_dev;

    struct xen_battery_manager xbm;

    uint8_t ac_adapter_present;      /* /pm/ac_adapter */
    uint8_t lid_state;               /* /pm/lid_state */
    MemoryRegion mr;                 /* General ACPI MemoryRegion to register IO ops */
} XenACPIPMState;

#define XEN_ACPI_PM_DEVICE_CLASS(cls) \
    OBJECT_CLASS_CHECK(XenACPIPMClass, (cls), TYPE_XEN_ACPI_PM_DEVICE)
#define XEN_ACPI_PM_DEVICE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(XenACPIPMClass, (obj), TYPE_XEN_ACPI_PM_DEVICE)

typedef struct XenACPIPMClass {
    DeviceClass parent_class;
} XenACPIPMClass;

/* -------/ Enable /-------------------------------------------------------- */

static bool xen_acpi_pm_enabled = false;

void xen_acpi_pm_set_enabled(bool enable)
{
    xen_acpi_pm_enabled = enable;
}

bool xen_acpi_pm_get_enabled(void)
{
    return xen_acpi_pm_enabled;
}

/* -------/ Xenstore /------------------------------------------------------ */

/* Read a string from the /pm/'key'
 * set the result in 'return_value'
 * retun 0 in success */
static int32_t xen_pm_read_str(char const *key, char **return_value)
{
    char path[XEN_BUFSIZE];
    char *value = NULL;

    if (NULL == key || NULL == return_value) {
        XBM_DPRINTF("ERROR, argument couldn't be null\n");
        return -1;
    }

    if (0 > snprintf(path, sizeof(path), "/pm/%s", key)) {
        XBM_DPRINTF("ERROR, snprintf failed\n");
        return -1;
    }

    value = xs_read(xenstore, XBT_NULL, path, NULL);

    if (NULL == value) {
        XBM_DPRINTF("ERROR, unable to read the content of \"%s\"\n", path);
        return -1;
    }

    *return_value = value;

    return 0;
}

/* Read a signed integer from the /pm/'key'
 * set the result in 'return_value'
 * retun 0 in success */
static int32_t xen_pm_read_int(char const *key, int32_t default_value,
                               int32_t *return_value)
{
    char path[XEN_BUFSIZE];
    char *value = NULL;

    if (0 > snprintf(path, sizeof(path), "/pm/%s", key)) {
        XBM_DPRINTF("ERROR, snprintf failed\n");
        return -1;
    }

    value = xs_read(xenstore, XBT_NULL, path, NULL);
    if (NULL == value) {
        XBM_DPRINTF("Unable to read the content of \"%s\"\n", path);
        *return_value = default_value;
        return 0;
    }

    *return_value = strtoull(value, NULL, 10);

    free(value);

    return 0;
}

/* -------/ Battery /------------------------------------------------------- */

static int32_t xen_battery_update_battery_present(struct xen_battery_manager *xbm)
{
    int32_t value;

    if (0 != xen_pm_read_int("battery_present", 0, &value)) {
        XBM_DPRINTF("ERROR, unable to update the battery present status\"\n");
        /* in error case, it's preferable to show the worst situation */
        xbm->battery_present = 0;
        return -1;
    }

    xbm->battery_present = value;

    return 0;
}

static int32_t xen_battery_update_bst(struct battery_buffer *battery,
                                      int32_t battery_num)
{
    char *value = NULL;
    char *old_value = NULL;
    char key[6];
    int32_t rc;

    old_value = battery->_bst;

    if (battery_num == 0) {
        rc = xen_pm_read_str("bst", &value);
    }
    else {
        memset(key, 0, sizeof(key));

        if (0 > snprintf(key, sizeof(key) - 1, "bst%d", battery_num)) {
            XBM_DPRINTF("ERROR, snprintf failed\n");
            return -1;
        }

        rc = xen_pm_read_str(key, &value);
    }

    if (0 != rc) {
        XBM_DPRINTF("ERROR, unable to read the content of \"/pm/bst%d\"\n",
                    battery_num);
        /* TODO: determine what could be the best way to do that */
        battery->_bst = old_value;
        if (NULL != value) {
            free(value);
        }
        return -1;
    }

    battery->_bst = value;

    if (NULL != old_value) {
        free(old_value);
    }
    return 0;
}

static int32_t xen_battery_update_bif(struct battery_buffer *battery,
                                      int32_t battery_num)
{
    char *value = NULL;
    char *old_value = NULL;
    char key[6];
    int32_t rc;

    old_value = battery->_bif;

    if (battery_num == 0) {
        rc = xen_pm_read_str("bif", &value);
    }
    else {
        memset(key, 0, sizeof(key));

        if (0 > snprintf(key, sizeof(key) - 1, "bif%d", battery_num)) {
            XBM_DPRINTF("ERROR, snprintf failed\n");
            return -1;
        }

        rc = xen_pm_read_str(key, &value);
    }

    if (0 != rc) {
        XBM_DPRINTF("ERROR, unable to read the content of \"/pm/bif%d\"\n",
                    battery_num);
        /* TODO: determine what could be the best way to do that */
        battery->_bif = old_value;
        if (NULL != value) {
            free(value);
        }
        return -1;
    }

    if ((NULL != old_value) && (NULL != value) &&
        (strncmp(old_value, value, 70) != 0)) {
        battery->bif_changed = 1;
    }

    battery->_bif = value;
    if (NULL != old_value) {
        free(old_value);
    }
    return 0;
}

static int32_t xen_battery_update_status_info(struct xen_battery_manager *xbm)
{
    int32_t index;

    if (NULL == xbm) {
        XBM_DPRINTF("ERROR, argument couldn't be null\n");
        return -1;
    }

    for (index = 0; index < MAX_BATTERIES; index++) {
        xen_battery_update_bif(&(xbm->batteries[index]), index);
        xen_battery_update_bst(&(xbm->batteries[index]), index);
    }

    return 0;
}

/* This function initializes the mode of the power management. */
static int32_t xen_battery_init_mode(struct xen_battery_manager *xbm)
{
    char dompath[XEN_BUFSIZE];
    char *value = NULL;

    if (NULL == xbm) {
        XBM_DPRINTF("ERROR, argument couldn't be null\n");
        return -1;
    }

    /* xen_extended_power_mgmt xenstore entry indicates whether or not extended
     * power management support is requested for the hvm guest.  Extended power
     * management support includes power management support beyond S3, S4, S5.
     * A value of 1 indicates pass-through pm support where upon pm resources
     * are mapped to the guest as appropriate where as a value of 2 as set in
     * non pass-through mode, requires qemu to take the onus of responding to
     * relevant pm port reads/writes. */
    if (0 > snprintf(dompath, sizeof(dompath),
                     "/local/domain/0/device-model/%d/xen_extended_power_mgmt",
                     xen_domid)) {
        XBM_DPRINTF("ERROR, snprintf failed\n");
        return -1;
    }

    value = xs_read(xenstore, XBT_NULL, dompath, NULL);

    if (NULL == value) {
        XBM_DPRINTF("ERROR, unable to read the content of \"%s\"\n", dompath);
        return -1;
    }

    xbm->mode = strtoull(value, NULL, 10);

    free(value);

    return 0;
}

/* -------/ Battery IO /---------------------------------------------------- */

static void battery_port_1_write_op_init(struct battery_buffer *bb)
{
    if (NULL != bb->_bif) {
        free(bb->_bif);
        bb->_bif = NULL;
    }
    if (NULL != bb->_bst) {
        free(bb->_bst);
        bb->_bst = NULL;
    }

    bb->_selector = XEN_BATTERY_TYPE_NONE;
    bb->index = 0;
}

static void battery_port_1_write_op_set_type(struct battery_buffer *bb,
                                             struct xen_battery_manager *xbm)
{
    if (XEN_BATTERY_TYPE_NONE == bb->_selector) {
        switch (bb->port_86_val) {
        case XEN_BATTERY_TYPE_BIF:
            bb->_selector = XEN_BATTERY_TYPE_BIF;
            xen_battery_update_bif(bb, xbm->index);
            XBM_DPRINTF("BATTERY_OP_SET_INFO_TYPE (BIF)\n");
            break;
        case XEN_BATTERY_TYPE_BST:
            bb->_selector = XEN_BATTERY_TYPE_BST;
            xen_battery_update_bst(bb, xbm->index);
            XBM_DPRINTF("BATTERY_OP_SET_INFO_TYPE (BST)\n");
            break;
        case XEN_BATTERY_TYPE_NONE:
            /* NO BREAK HERE: fallthrough */
        default:
            XBM_DPRINTF("ERROR, unknown type: %d\n", bb->port_86_val);
        }
    }
}

/*
 * Command option helper to get the next data byte.
 *
 * To understand this function, the format of the data passed in the xenstore
 * nodes for BIF and BST must be understood. Both the BIF and BST are defined
 * in the ACPI specification (see 10.2.2.1 and 10.2.2.6 in the version 5.0
 * spec).
 *
 * The BIF starts with 9 fixed DWORD fields followed by 4 variable length
 * ASCII fields. All of this is flattened out in into a string in xenstore by
 * xcpmd. Length specifiers and DWORD bytes are represented by 2 hex chars.
 * The first byte pair is the length of the entire block (see below). The next
 * 36 bytes pairs are the DWORD fields. At the end are the four ASCII strings
 * each starting with a length byte pair then the string then a trailing \n.
 *
 * This is an example of a BIF:
 *
 * 4a010000009222000092220000010000005c2b0000000000000000000001000000010000000dDELL GCJ4839\n04500\n05LION\n0cSamsung SDI\n
 * ^ |           9 DWORDS * 4 bytes each * 2 chars each = 72 bytes          |^ |  string 1  | ....
 * |                                                                         |
 * +--- D-LENGTH of DWORD block                   S-LENGTH of first sting ---+
 *
 * This layout is where the magic number BIF_DATA_BOUNDARY (74) comes from. It
 * it the switch over point from reading the D-LENGTH + DWORD block to
 * reading the ASCII strings. Each read of the byte pair data is a read of 2
 * where the reading of the ASCII chars is a 1 byte read. S-LENGTH is pretty
 * simple, it is the string length and \n counts as 1 char. D-LENGTH is more
 * complicated. For byte pairs, it counts each as one byte but chars are each
 * 1 byte (with \n as one char). So 0x4a == 74 is the entire length.
 *
 * Now compared to that, the BST is simple. It is a structure of 4 DWORDs.
 * This is flattened out the same way as the BIF and it is clear why the
 * reading algorithm below works on it the same way.
 *
 * This is an example BST:
 *
 * 1000000000010000009222000045320000
 * ^ | 4 DWORDS ... = 32 bytes      |
 * |
 * +--- D-LENGTH of DWORD block
 * 
 */
static void battery_port_1_op_get_data(struct battery_buffer *bb,
                                       struct xen_battery_manager *xbm)
{
#define BIF_DATA_BOUNDARY 74
    char *data = NULL;
    char buf[3];

    if (XEN_BATTERY_TYPE_BST == bb->_selector) {
       data = bb->_bst;
    }
    else if (XEN_BATTERY_TYPE_BIF == bb->_selector) {
       data = bb->_bif;
    }
    else {
       XBM_DPRINTF("ERROR, unknown _selector: %d\n", bb->_selector);
       return;
    }

    data += bb->index;
    if ((bb->index <= BIF_DATA_BOUNDARY) ||
        ((bb->index > BIF_DATA_BOUNDARY) && ((*(data - 1)) == '\n'))) {
        snprintf(buf, sizeof(buf), "%s", data);
        bb->port_86_val = (uint8_t)strtoull(buf, NULL, 0x10);
        bb->index += 2;
    }
    else {
       if (*data == '\n') {
           bb->port_86_val = 0;
       }
       else {
           bb->port_86_val = *data;
       }
       bb->index++;
    }
}

/*
 * Battery command IO port write.
 *
 * Writes to the command ports select the operations including
 * reads on the data port.
 */
static void battery_port_1_write(void *opaque, hwaddr addr,
                                 uint64_t val, uint32_t size)
{
    struct xen_battery_manager *xbm = opaque;
    struct battery_buffer *bb;

    bb = &(xbm->batteries[xbm->index]);

    switch (val) {
    case BATTERY_OP_INIT:
    {
        battery_port_1_write_op_init(bb);
        XBM_DPRINTF("BATTERY_OP_INIT\n");
        break;
    }
    case BATTERY_OP_SET_INFO_TYPE:
    {
        battery_port_1_write_op_set_type(bb, xbm);
        break;
    }
    case BATTERY_OP_GET_DATA_LENGTH:
    {
        /*
         * Length read comes first and the length is the first byte of the
	 * data so fallthrough.
         */
    }
    case BATTERY_OP_GET_DATA:
    {
        XBM_DPRINTF("BATTERY_OP_GET_DATA\n");
        battery_port_1_op_get_data(bb, xbm);
        break;
    }
    default:
        XBM_DPRINTF("Unknown cmd: %llu", val);
        break;
    }

    bb->port_b2_val = 0;
}

/*
 * Battery command IO port read.
 */
static uint64_t battery_port_1_read(void *opaque, hwaddr addr, uint32_t size)
{
    struct xen_battery_manager *xbm = opaque;
    XBM_DPRINTF("port_b2 == 0x%02x\n", xbm->batteries[xbm->index].port_b2_val);
    return xbm->batteries[xbm->index].port_b2_val;
}

struct MemoryRegionOps port_1_ops = {
    .read = battery_port_1_read,
    .write = battery_port_1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
    .valid = {
        .min_access_size = 1,
        .max_access_size = 2,
    },
};

/*
 * Battery data IO port write.
 */
static void battery_port_2_write(void *opaque, hwaddr addr,
                                 uint64_t val, uint32_t size)
{
    struct xen_battery_manager *xbm = opaque;
    xbm->batteries[xbm->index].port_86_val = val;
    XBM_DPRINTF("port_86 := 0x%x\n", xbm->batteries[xbm->index].port_86_val);
}

/*
 * Battery data IO port read.
 *
 * For get data command ops, each byte is read sequentially from this port.
 */
static uint64_t battery_port_2_read(void *opaque, hwaddr addr, uint32_t size)
{
    struct xen_battery_manager *xbm = opaque;
    XBM_DPRINTF("port_86 == 0x%x\n", xbm->batteries[xbm->index].port_86_val);
    return xbm->batteries[xbm->index].port_86_val;
}

struct MemoryRegionOps port_2_ops = {
    .read = battery_port_2_read,
    .write = battery_port_2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

/*
 * Battery 1 (BAT0) status IO port write.
 *
 * Returns 0x01 if battery present.
 *         0x80 if battery information is updated.
 */
static uint64_t battery_port_3_read(void *opaque, hwaddr addr, uint32_t size)
{
    struct xen_battery_manager *xbm = opaque;
    uint64_t system_state = 0x0000000000000000ULL;

    xen_battery_update_battery_present(xbm);

    xen_battery_update_bif(&(xbm->batteries[0]), 0);

    if (NULL != xbm->batteries[0]._bif) {
        system_state |= 0x1;
    }

    if (1 == xbm->batteries[0].bif_changed) {
        xbm->batteries[0].bif_changed = 0;
        system_state |= 0x80;
    }

    XBM_DPRINTF("BAT0 system_state == 0x%02llx\n", system_state);
    return system_state;
}

struct MemoryRegionOps port_3_ops = {
    .read = battery_port_3_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

/*
 * Battery 2 (BAT1) status IO port write.
 *
 * Returns 0x01 if battery present.
 *         0x80 if battery information is updated.
 */
static uint64_t battery_port_4_read(void *opaque, hwaddr addr, uint32_t size)
{
    struct xen_battery_manager *xbm = opaque;
    uint64_t system_state = 0x0000000000000000ULL;

    xen_battery_update_battery_present(xbm);

    xen_battery_update_bif(&(xbm->batteries[1]), 1);

    if (NULL != xbm->batteries[1]._bif) {
        system_state |= 0x1;
    }

    if (1 == xbm->batteries[1].bif_changed) {
        xbm->batteries[1].bif_changed = 0;
        system_state |= 0x80;
    }

    XBM_DPRINTF("BAT1 system_state == 0x%02llx\n", system_state);
    return system_state;
}

struct MemoryRegionOps port_4_ops = {
    .read = battery_port_4_read,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void battery_port_5_write(void *opaque, hwaddr addr,
                                 uint64_t val, uint32_t size)
{
    struct xen_battery_manager *xbm = opaque;

    XBM_DPRINTF("opaque(%p) addr(0x%x) val(%llu) size(%u)\n",
                opaque, (uint32_t)addr, val, size);

    if ((val > 0) && (val <= MAX_BATTERIES)) {
        xbm->index = ((uint8_t)val) - 1;
        XBM_DPRINTF("Current battery is %u\n", xbm->index);
    }
}

struct MemoryRegionOps port_5_ops = {
    .write = battery_port_5_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

/* TODO: The MemoryRegion field isn't dynamically allocated find a way to do
 * the same by keeping it sexy */
struct {
    struct MemoryRegionOps const *ops;
    hwaddr base;
    char const *name;
    uint64_t size;
} opsTab[] = {
    { .ops = &port_1_ops,
      .base = BATTERY_PORT_1,
      .name = "acpi-xbm1",
      .size = 2, },
    { .ops = &port_2_ops,
      .base = BATTERY_PORT_2,
      .name = "acpi-xbm2",
      .size = 1, },
    { .ops = &port_3_ops,
      .base = BATTERY_PORT_3,
      .name = "acpi-xbm3",
      .size = 1, },
    { .ops = &port_4_ops,
      .base = BATTERY_PORT_4,
      .name = "acpi-xbm4",
      .size = 1, },
    { .ops = &port_5_ops,
      .base = BATTERY_PORT_5,
      .name = "acpi-xbm5",
      .size = 1, },
    /* /!\ END OF ARRAY */
    { .ops = NULL, .base =  0, .name = NULL, },
};


/* TODO: check error code
 * TODO: release memory region when qemu is leaving */
static int xen_battery_register_ports(struct xen_battery_manager *xbm,
                                      MemoryRegion *parent)
{
    int index;

    for (index = 0; (NULL != opsTab[index].name); index++) {
        memory_region_init_io(&xbm->mr[index], opsTab[index].ops,
                              xbm, opsTab[index].name, opsTab[index].size);
        memory_region_add_subregion(parent, opsTab[index].base,
                                    &xbm->mr[index]);
    }

    return 0;
}

/* -------/ Support/AC/lid /------------------------------------------------ */

/*
 * Update the AC adapter state.
 *
 * Note the default is 1 since the ac_adapter values is not written on
 * non-portable systems like desktops. But they have an AC adapter or they
 * could not start.
 */
static void xen_pm_update_ac_adapter(XenACPIPMState *s)
{
    int32_t value;

    if (0 != xen_pm_read_int("ac_adapter", 1, &value)) {
        XBM_DPRINTF("ERROR, unable to update the ac_adapter present status\n");
        s->ac_adapter_present = 1;
        return;
    }

    s->ac_adapter_present = value;
}

/*
 * Update the AC lid state.
 *
 * Note the default is 1 since the lid_state is not written on systems that
 * do not have an attached panel. We don't want the lid device to say it is
 * closed in guests on these systems.
 */
static void xen_pm_update_lid_state(XenACPIPMState *s)
{
    int32_t value;

    if (0 != xen_pm_read_int("lid_state", 1, &value)) {
        XBM_DPRINTF("ERROR, unable to update the lid_state status\"\n");
        s->lid_state = 1;
        return;
    }

    s->lid_state = value;
}

/* -------/ Initialization /------------------------------------------------ */

static int xen_acpi_pm_initfn(DeviceState *qdev)
{
    XenACPIPMState *s = XEN_ACPI_PM_DEVICE(qdev);
    int i;

    memset(&s->xbm, 0, sizeof(struct xen_battery_manager));
    for (i = 0; i < MAX_BATTERIES; i++) {
        s->xbm.batteries[i].bif_changed = 1;
    }

    if (0 != xen_battery_init_mode(&s->xbm)) {
        goto error_init;
    }

    if (0 != xen_battery_update_battery_present(&s->xbm)) {
        goto error_init;
    }

    if (0 != xen_battery_update_status_info(&s->xbm)) {
        goto error_init;
    }

    switch (s->xbm.mode) {
    case XEN_BATTERY_MODE_HVM:
        XBM_DPRINTF("Emulated mode\n");
        xen_battery_register_ports(&s->xbm, pci_address_space_io(s->pci_dev));
        break;
    case XEN_BATTERY_MODE_PT:
        XBM_ERROR_MSG("Mode Pass Through no longer supported for security reasons\n");
        goto error_init;
        break;
    case XEN_BATTERY_MODE_NONE:
    default:
        XBM_ERROR_MSG("Mode (0x%02x) unsupported\n", s->xbm.mode);
        goto error_init;
    }

    xen_pm_update_ac_adapter(s);
    xen_pm_update_lid_state(s);

    fprintf(stdout, "Battery initialized\n");

    return 0;

error_init:
    XBM_ERROR_MSG("unable to initialize the battery emulation\n");
    return -1;
}

void xen_acpi_pm_create(PCIDevice *device)
{
    DeviceState *dev;
    XenACPIPMState *s;

    dev = qdev_create(NULL, "xen-acpi-pm");
    s = XEN_ACPI_PM_DEVICE(dev);
    s->pci_dev = device;
    qdev_init_nofail(&s->qdev);
}

static void xen_acpi_pm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);

    k->init = xen_acpi_pm_initfn;
    k->desc = "Xen ACPI PM device";
}

static const TypeInfo xen_acpi_pm_info = {
    .name = "xen-acpi-pm",
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(XenACPIPMState),
    .class_init = xen_acpi_pm_class_init,
};

static void xen_acpi_pm_register_types(void)
{
    type_register_static(&xen_acpi_pm_info);
}

type_init(xen_acpi_pm_register_types)
