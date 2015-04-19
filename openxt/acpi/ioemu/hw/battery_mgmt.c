/*
 * battery_mgmt.c
 *
 * Copyright (c) 2008  Kamala Narasimhan
 * Copyright (c) 2008  Citrix Systems, Inc.
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

/* Battery Management Implementation. Implements the following -
 *  1) Check xenstore for pm implementation type - Pass-through or non-passthrough
 *  2) Map appropriate ports for pass-through battery information
 *  3) Register for appropriate ports and respond to those port read/writes for
 *     non-passthrough implementation
 */

#include "hw.h"
#include "pc.h"
#include "qemu-xen.h"
#include "isa.h" //register_ioport_read declaration
#include "battery_mgmt.h"

#ifndef CONFIG_NO_BATTERY_MGMT

#include <sys/io.h>

//#define BATTERY_MGMT_DEBUG
//#define BATTERY_MGMT_DEBUG_EXT

#define BATTERY_NUM_MAX 0x2

#define BATTERY_PORT_1 0xb2
#define BATTERY_PORT_2 0x86
#define BATTERY_PORT_3 0x88
#define BATTERY_PORT_4 0x90
#define BATTERY_PORT_5 0xb4

#define BATTERY_OP_INIT            0x7b
#define BATTERY_OP_SET_INFO_TYPE   0x7c /*BIF or BST */
#define BATTERY_OP_GET_DATA_LENGTH 0x79
#define BATTERY_OP_GET_DATA        0x7d

/*#ifdef BATTERY_MGMT_DEBUG_EXT
    #define BATTERY_DBG_PORT_1 0xB040
    #define BATTERY_DBG_PORT_2 0xB044
    #define BATTERY_DBG_PORT_3 0xB046
    #define BATTERY_DBG_PORT_4 0xB048
    static int monitor_battery_port = 0;
#endif*/

enum POWER_MGMT_MODE { PM_MODE_NONE = 0, PM_MODE_PT, PM_MODE_NON_PT };
enum BATTERY_INFO_TYPE { BATT_NONE, BIF, BST };
typedef struct battery_state_info {
    enum BATTERY_INFO_TYPE type;
    uint8_t port_b2_val;
    uint8_t port_86_val;
    uint8_t port_66_val;
    char *battery_data;
    uint8_t current_index;
} battery_state_info;

static enum POWER_MGMT_MODE power_mgmt_mode = PM_MODE_NONE;
/* Virtual firmware synchronizes the battery operations.  So, no need
   for extra locks to handle the below. */
static battery_state_info battery_info[BATTERY_NUM_MAX];
static unsigned short battery_num = 0;
extern FILE *logfile;

int is_battery_pt_feasible(void)
{
    int val;

    if ( (ioperm(BATTERY_PORT_1, 1, 1) != 0) ||
         (ioperm(BATTERY_PORT_2, 1, 1) != 0) )
        return 0;

    outb(BATTERY_OP_INIT, BATTERY_PORT_1);
    outb(1 /*BIF*/, BATTERY_PORT_2);
    outb(BATTERY_OP_SET_INFO_TYPE, BATTERY_PORT_1);
    outb(0, BATTERY_PORT_2);
    outb(BATTERY_OP_GET_DATA_LENGTH, BATTERY_PORT_1);
    val = inb(BATTERY_PORT_2);

    ioperm(BATTERY_PORT_1, 1, 0);
    ioperm(BATTERY_PORT_2, 1, 0);
    if ( !val )
        fprintf(logfile, "xen_extended_power_mgmt set to 1 but this mode is incomapatible with the current firmware!\n");
    return val;
}

void battery_mgmt_pt_mode_init(void)
{
    xc_interface *xc;

    xc = xc_interface_open(0,0,0);
    if ( !xc )
    {
        fprintf(logfile, "%s: xc_interface_open failed\n", __FUNCTION__);
        return;
    }

    if ( xc_domain_ioport_mapping(xc, domid, BATTERY_PORT_1, BATTERY_PORT_1, 0x2, 1) != 0 ) 
       fprintf(logfile, "Failed to map port %x to guest\n", BATTERY_PORT_1);

    if ( xc_domain_ioport_mapping(xc, domid, BATTERY_PORT_2, BATTERY_PORT_2, 0x1, 1) != 0 ) 
        fprintf(logfile, "Failed to map port %x to guest\n", BATTERY_PORT_2);

    xc_interface_close(xc);
}

/*#ifdef BATTERY_MGMT_DEBUG_EXT

static void battery_dbg_monitor(void *opaque, uint32_t addr, uint32_t val)
{   
    monitor_battery_port = !monitor_battery_port;
}

static void battery_dbg_port_b2_input(void *opaque, uint32_t addr, uint32_t val)
{
    if ( monitor_battery_port ) 
        fprintf(logfile, "Input value to battery port 0xb2 - %d; port 0x86 -  ", val);
}

static void battery_dbg_port_86_output(void *opaque, uint32_t addr, uint32_t val)
{
    if ( monitor_battery_port ) 
        fprintf(logfile, "Output value from battery port 0x86 - %d\n", val);
}
    
static void battery_dbg_port_86_input(void *opaque, uint32_t addr, uint32_t val)
{
    if ( monitor_battery_port ) 
        fprintf(logfile, "%d.  ", val); 
}
#endif //BATTERY_MGMT_DEBUG_EXT
*/

void get_battery_data_from_xenstore(void)
{
    battery_info[battery_num].battery_data = 0;
    battery_info[battery_num].current_index = 0;

    if ( battery_info[battery_num].type == BIF )
        battery_info[battery_num].battery_data = xenstore_read_battery_data(0/*battery info*/, battery_num);
    else if ( battery_info[battery_num].type == BST )
        battery_info[battery_num].battery_data = xenstore_read_battery_data(1/*battery status*/, battery_num);
}

void write_battery_data_to_port(void)
{
    char temp[3];
    char *data;

    if ( battery_info[battery_num].battery_data == NULL )
        return;

    data = battery_info[battery_num].battery_data + battery_info[battery_num].current_index;
    //KN: @Todo - Revisit the hard coding below and add bounds checking
    // for current index though the querying software is not likely to 
    // ask for more than what we provide as initial data length.
    if ( ( battery_info[battery_num].current_index <= 74 ) ||
         (( battery_info[battery_num].current_index > 74 ) && (*(data - 1) == '\n' )) )
    {
        snprintf(temp, 3, "%s", data);
        battery_info[battery_num].port_86_val = (uint8_t)strtoull(temp, NULL, 16);
        battery_info[battery_num].current_index+=2;
    } else 
    {
        if ( *data == '\n' )
            battery_info[battery_num].port_86_val = 0;
        else
            battery_info[battery_num].port_86_val = *data;
        battery_info[battery_num].current_index+=1;
    }

#ifdef BATTERY_MGMT_DEBUG_EXT
        fprintf(logfile, "Wrote %d to port 0x86\n", battery_info[battery_num].port_86_val);
#endif
    return;
}  

static void battery_port_1_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    switch (val) 
    {
        case BATTERY_OP_GET_DATA_LENGTH:
            get_battery_data_from_xenstore();
            write_battery_data_to_port();
            battery_info[battery_num].port_b2_val = 0;
            break;
        case BATTERY_OP_INIT:
            battery_info[battery_num].type = BATT_NONE;
            if ( battery_info[battery_num].battery_data != 0 )
                free(battery_info[battery_num].battery_data);
            battery_info[battery_num].battery_data = 0;
            battery_info[battery_num].current_index = 0;
            battery_info[battery_num].port_b2_val = 0;
            break;
        case BATTERY_OP_SET_INFO_TYPE:
            if ( battery_info[battery_num].type == BATT_NONE )
            {
                if ( battery_info[battery_num].port_86_val == 1 )
                    battery_info[battery_num].type = BIF;
                else if ( battery_info[battery_num].port_86_val == 2 )
                {
                    xenstore_refresh_battery_status();  
                    battery_info[battery_num].type = BST;
                }
            }   
            battery_info[battery_num].port_b2_val = 0;
            break;
        case BATTERY_OP_GET_DATA:
            write_battery_data_to_port();
            battery_info[battery_num].port_b2_val = 0;
            break;
        default:
            break;
    }
    return;
}

static uint32_t battery_port_1_readb(void *opaque, uint32_t addr)
{
    return battery_info[battery_num].port_b2_val;
}

static void battery_port_2_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    battery_info[battery_num].port_86_val = val;
}

static uint32_t battery_port_2_readb(void *opaque, uint32_t addr)
{
    return battery_info[battery_num].port_86_val;
}

static uint32_t battery_port_3_readb(void *opaque, uint32_t addr)
{
    uint32_t system_state;

    if ( power_mgmt_mode != PM_MODE_PT && power_mgmt_mode != PM_MODE_NON_PT )
        return 0x0;

    if ( (power_mgmt_mode == PM_MODE_PT) && (is_battery_pt_feasible() == 0) )
        return 0x0;

    system_state = xenstore_read_ac_adapter_state();
    if ( xenstore_read_lid_state() == 1 )
        system_state |= 0x4;

    if ( xenstore_read_battery_data(0/*battery info*/, 0) != NULL )
        system_state |= 0x2;

    return system_state;
}

static uint32_t battery_port_4_readb(void *opaque, uint32_t addr)
{
    return xenstore_read_is_secondary_battery_present();
}

static void battery_port_5_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    if ( val > 0 && val <= BATTERY_NUM_MAX )
    {
        battery_num = val - 1;
#ifdef BATTERY_MGMT_DEBUG
        fprintf(logfile, "Current battery is - %d\n", val);
#endif
    }
}

void battery_mgmt_non_pt_mode_init(PCIDevice *device)
{
    memset(battery_info, 0, sizeof(battery_state_info) * BATTERY_NUM_MAX);
    register_ioport_read(BATTERY_PORT_1, 2, 1, battery_port_1_readb, device);
    register_ioport_write(BATTERY_PORT_1, 2, 1, battery_port_1_writeb, device);
    register_ioport_read(BATTERY_PORT_2, 1, 1, battery_port_2_readb, device);
    register_ioport_write(BATTERY_PORT_2, 1, 1, battery_port_2_writeb, device);
    register_ioport_write(BATTERY_PORT_5, 2, 1, battery_port_5_writeb, device); /* RJP this is not 2 bytes!!!! */

#ifdef BATTERY_MGMT_DEBUG_EXT
    register_ioport_write(BATTERY_DBG_PORT_1, 1, 1, battery_dbg_monitor , device);
    register_ioport_write(BATTERY_DBG_PORT_2, 1, 1, battery_dbg_port_b2_input, device);
    register_ioport_write(BATTERY_DBG_PORT_3, 1, 1, battery_dbg_port_86_output, device);
    register_ioport_write(BATTERY_DBG_PORT_4, 1, 1, battery_dbg_port_86_input, device);
#endif
}
 
void battery_mgmt_init(PCIDevice *device)
{
    char *mode_buffer = NULL;

    /* First check if battery is present; then enable extended power management support if
     * requested for the guest
     */
    if ( xenstore_read_is_battery_present() == 0 )
        return;

    //xen_extended_power_mgmt xenstore entry indicates whether or not extended power
    //management support is requested for the hvm guest.  Extended power management
    //support includes power management support beyond S3, S4, S5.  A value of 1
    //indicates pass-through pm support where upon pm resources are mapped to the guest
    //as appropriate where as a value of 2 as set in non pass-through mode, requires qemu
    //to take the onus of responding to relevant pm port reads/writes.
    mode_buffer = xenstore_device_model_read(domid, "xen_extended_power_mgmt", NULL);
    if ( mode_buffer == NULL ) 
    {
#ifdef BATTERY_MGMT_DEBUG
        fprintf(logfile,"Xenpm mode not set\n");
#endif
        return;
    }

    power_mgmt_mode = (enum POWER_MGMT_MODE) strtoull(mode_buffer, NULL, 10);
    free(mode_buffer);
    switch ( power_mgmt_mode ) 
    {
        case PM_MODE_PT:
            battery_mgmt_pt_mode_init();
            break;
        case PM_MODE_NON_PT:
            battery_mgmt_non_pt_mode_init(device);
            break;
        case PM_MODE_NONE:
        default:
            return;
    }

    register_ioport_read(BATTERY_PORT_3, 1, 1, battery_port_3_readb, device);
    register_ioport_read(BATTERY_PORT_4, 1, 1, battery_port_4_readb, device);
    xenstore_register_for_pm_events();

#ifdef BATTERY_MGMT_DEBUG
    fprintf(logfile, "Power management mode set to - %d\n", power_mgmt_mode);
#endif    
}

#else

void battery_mgmt_init(PCIDevice *device) { }

#endif
