/*
 * xcpmd.c
 *
 * XenClient platform management daemon
 *
 * Copyright (c) 2008 Kamala Narasimhan <kamala.narasimhan@citrix.com>
 * Copyright (c) 2011 Ross Philipson <ross.philipson@citrix.com>
 * Copyright (c) 2011 Citrix Systems, Inc.
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

static char rcsid[] = "$Id:$";

/*
 * $Log:$
 */

/* Xen extended power management support provides HVM guest power management
 * features beyond S3, S4, S5.  For example, it helps expose system level
 * battery status and battery meter information and in future will be extended
 * to include more power management support.  This extended power management
 * support is enabled by setting xen_extended_power_mgmt to 1 or 2 in the HVM
 * config file.  When set to 2, non-pass through mode is enabled which heavily
 * relies on this power management daemon to glean battery information from
 * dom0 and store it xenstore which would then be queries and used by qemu and
 * passed to the guest when appropriate battery ports are read/written to.
 */

#include "project.h"
#include "xcpmd.h"

xc_interface *xch = NULL;
static struct event misc_event;
static struct event refresh_battery_event;

static unsigned long last_full_capacity = 0;
static enum BATTERY_LEVEL current_battery_level = NORMAL;
static int monitoring_battery_level = 0;
static int battery_level_under_threshold = 0;

FILE *get_ac_adpater_state_file(void)
{
    FILE *file;

    file = fopen(AC_ADAPTER_STATE_FILE_PATH, "r");

    return file;
}

DIR *get_battery_dir(DIR *battery_dir, char* folder, int bat_n)
{
    DIR *dir = NULL;
    struct dirent *dir_entries;

    sprintf(folder, "%s/BAT%i", BATTERY_DIR_PATH, bat_n);
    dir = opendir(folder);

    return dir;
}

static void set_attribute_battery_info(char *attrib_name,
                                       char *attrib_value,
                                       struct battery_info *info)
{
    if ( !strcmp(attrib_name, "present") )
    {
        if ( strstr(attrib_value, "1") )
	  info->present = YES;
    }

    if ( !strcmp(attrib_name, "charge_full_design") )
      info->charge_full_design = strtoull(attrib_value, NULL, 10) / 1000;

    if ( !strcmp(attrib_name, "charge_full") )
      info->charge_full = strtoull(attrib_value, NULL, 10) / 1000;

    if ( !strcmp(attrib_name, "energy_full_design") )
      info->energy_full_design = strtoull(attrib_value, NULL, 10) / 1000;

    if ( !strcmp(attrib_name, "energy_full") )
      info->energy_full = strtoull(attrib_value, NULL, 10) / 1000;

    if ( !strcmp(attrib_name, "voltage_min_design") )
      info->design_voltage = strtoull(attrib_value, NULL, 10) / 1000;

    if ( !strcmp(attrib_name, "model_name") )
      strncpy(info->model_number, attrib_value, 32);

    if ( !strcmp(attrib_name, "serial_number") )
      strncpy(info->serial_number, attrib_value, 32);

    if ( !strcmp(attrib_name, "technology") )
    {
        if (strstr(attrib_value, "Li-ion"))
	  strncpy(info->battery_type, "LION\n\0", 6);
	else
	  if (strstr(attrib_value, "Li-poly"))
	    strncpy(info->battery_type, "LiP\n\0", 6);
	  else
	    strncpy(info->battery_type, attrib_value, 32);
        /* Hack: Now "technology" stands for the type of battery, but
           come on, a non-rechargeable battery? */
        info->battery_technology = RECHARGEABLE;
    }

    if ( !strcmp(attrib_name, "manufacturer") )
      strncpy(info->oem_info, attrib_value, 32);
}

static void fix_battery_info(struct battery_info *info)
{
    /* In sysfs, the charge nodes are for batteries reporting in mA and
     * the energy nodes are for mW (even though Watts are not a measurement
     * of energy but power rather...sigh).
     */
    if (info->charge_full_design != 0)
    {
        info->power_unit = mA;
        info->design_capacity = info->charge_full_design;
        info->last_full_capacity = info->charge_full;
    }
    else
    {
        info->power_unit = mW;
        info->design_capacity = info->energy_full_design;
        info->last_full_capacity = info->energy_full;
    }

    /* Unlike the old procfs files, sysfs does not report some values like the
     * warn and low levels. These values are generally ignored anyway. The
     * various OS's decide what to do at different depletion levels through
     * their own policies. These are just some approximate values to pass.
     */
    info->design_capacity_warning = info->last_full_capacity *
        (BATTERY_WARNING_PERCENT / 100);
    info->design_capacity_low = info->last_full_capacity *
        (BATTERY_LOW_PERCENT / 100);
    info->capacity_granularity_1 = 1;
    info->capacity_granularity_2 = 1;

    last_full_capacity += info->last_full_capacity;
}

static void set_attribute_battery_status(char *attrib_name,
                                         char *attrib_value,
                                         struct battery_status *status)
{
    if ( !strcmp(attrib_name, "status") )
    {
        /* The spec says bit 0 and bit 1 are mutually exclusive */
        if ( strstr(attrib_value, "Discharging") )
            status->state |= 0x1;
        else if ( strstr(attrib_value, "Charging") )
            status->state |= 0x2;
        return;
    }

    if ( !strcmp(attrib_name, "capacity_level") )
    {
        if ( strstr(attrib_value, "critical") )
            status->state |= 4;
        return;
    }

    if ( !strcmp(attrib_name, "current_now") )
      status->current_now = strtoull(attrib_value, NULL, 10) / 1000;

    if ( !strcmp(attrib_name, "charge_now") )
      status->charge_now = strtoull(attrib_value, NULL, 10) / 1000;

    if ( !strcmp(attrib_name, "power_now") )
      status->power_now = strtoull(attrib_value, NULL, 10) / 1000;

    if ( !strcmp(attrib_name, "energy_now") )
      status->energy_now = strtoull(attrib_value, NULL, 10) / 1000;

    if ( !strcmp(attrib_name, "voltage_now") )
      status->present_voltage = strtoull(attrib_value, NULL, 10) / 1000;

    if ( !strcmp(attrib_name, "present") )
    {
        if ( strstr(attrib_value, "1") )
            status->present = YES;
        return;
    }
}

static void fix_battery_status(struct battery_status *status)
{
    if (status->current_now != 0)
    {
        /* Rate in mAh, remaining in mA */
        status->present_rate = status->current_now;
        status->remaining_capacity = status->charge_now;
    }
    else
    {
        /* Rate in mWh, remaining in mW */
        status->present_rate = status->power_now;
        status->remaining_capacity = status->energy_now;
    }
}

static int get_next_battery_info_or_status(DIR *battery_dir,
                                           enum BATTERY_INFO_TYPE type,
                                           void *info_or_status,
					   int bat_n)
{
    FILE *file;
    char line_info[128];
    DIR *d;
    struct dirent *dir;
    char folder[256];
    char filename[256];
    char *start;

    if (!info_or_status || !battery_dir)
    {
        return 0;
    }

    memset(line_info, 0, sizeof(line_info));
    if (type == BIF)
        memset(info_or_status, 0, sizeof(struct battery_info));
    else
        memset(info_or_status, 0, sizeof(struct battery_status));

    d = get_battery_dir(battery_dir, folder, bat_n);
    if (d == NULL)
    {
        return 0;
    }
    if (type == BIF)
        memset(info_or_status, 0, sizeof(struct battery_info));
    else
        memset(info_or_status, 0, sizeof(struct battery_status));

    while ((dir = readdir(d)) != NULL)
    {
	if (dir->d_type == DT_REG)
	{
	    memset(filename, 0, sizeof(filename));
	    sprintf(filename, "%s/%s", folder, dir->d_name);
	    file = fopen(filename, "r");
	    if (!file)
	        continue;
	    memset(line_info, 0, sizeof (line_info));
	    fgets(line_info, sizeof(line_info), file);
	    fclose(file);
	    start = line_info;
	    while (*start == ' ')
	        start++;

	    if (type == BIF)
	        set_attribute_battery_info(dir->d_name, start, info_or_status);
	    else
	        set_attribute_battery_status(dir->d_name, start, info_or_status);
	}
    }

    if (type == BIF)
        fix_battery_info(info_or_status);
    else
        fix_battery_status(info_or_status);

    if (d != NULL) {
        closedir(d);
    }

    return 1;
}

static void write_battery_info_to_xenstore(struct battery_info *info, unsigned short battery_num)
{
    char val[1024], string_info[256];

    memset(val, 0, 1024);
    memset(string_info, 0, 256);
    /* write 9 dwords (so 9*4) + length of 4 strings + 4 null terminators */
    snprintf(val, 3, "%02x",
             (unsigned int)(9*4 +
                            strlen(info->model_number) +
                            strlen(info->serial_number) +
                            strlen(info->battery_type) +
                            strlen(info->oem_info) + 4));
    write_ulong_lsb_first(val+2, info->power_unit);
    write_ulong_lsb_first(val+10, info->design_capacity);
    write_ulong_lsb_first(val+18, info->last_full_capacity);
    write_ulong_lsb_first(val+26, info->battery_technology);
    write_ulong_lsb_first(val+34, info->design_voltage);
    write_ulong_lsb_first(val+42, info->design_capacity_warning);
    write_ulong_lsb_first(val+50, info->design_capacity_low);
    write_ulong_lsb_first(val+58, info->capacity_granularity_1);
    write_ulong_lsb_first(val+66, info->capacity_granularity_2);

    snprintf(string_info, 256, "%02x%s%02x%s%02x%s%02x%s",
             (unsigned int)strlen(info->model_number), info->model_number,
             (unsigned int)strlen(info->serial_number), info->serial_number,
             (unsigned int)strlen(info->battery_type), info->battery_type,
             (unsigned int)strlen(info->oem_info), info->oem_info);
    strncat(val+73, string_info, 1024-73-1);

    if (battery_num == 0)
        xenstore_write(val, XS_BIF);
    else
        xenstore_write(val, XS_BIF1);
}

int write_battery_info(int *total_count)
{
    DIR *dir;
    int present = 0, total = 0, batn = 0;
    struct battery_info info[MAX_BATTERY_SUPPORTED];
    int i, rc;

    last_full_capacity = 0;
    xenstore_rm(XS_BIF);
    xenstore_rm(XS_BIF1);

    dir = opendir(BATTERY_DIR_PATH);
    if ( !dir )
    {
        /* This is an error, the procfs always has the battery directory though it may be empty */
        xcpmd_log(LOG_ERR, "Failed to open dir %s with error - %d\n", BATTERY_DIR_PATH, errno);
        return 0;
    }

    for (i = 0; i < MAX_BATTERY_SCANNED; ++i)
    {
        rc = get_next_battery_info_or_status(dir, BIF, (void *)&info[batn], i);
        if (!rc)
            continue;

        print_battery_info(&info[batn]);
        total++;

        /* If there is a battery slob but no battery present, go on and reuse
         * the current info struct slot.
         */
        if ( info[batn].present == NO )
            continue;

        write_battery_info_to_xenstore(&info[batn], batn);
        batn++;
        xcpmd_log(LOG_INFO, "One time battery information written to xenstore\n");
        if ( batn >= MAX_BATTERY_SUPPORTED )
            break;
    }

    closedir(dir);

    /* optionally returns total battery slot count, not just ones with batteries present */
    if ( total_count )
        *total_count = total;

    /* returns count of slots with batteries present */
    return batn;
}

void
adjust_brightness(int increase, int force)
{
    if ( force || (pm_quirks & PM_QUIRK_SW_ASSIST_BCL))
    {
        if (increase)
            com_citrix_xenclient_surfman_increase_brightness_(xcdbus_conn, SURFMAN_SERVICE, SURFMAN_PATH);
        else
            com_citrix_xenclient_surfman_decrease_brightness_(xcdbus_conn, SURFMAN_SERVICE, SURFMAN_PATH);
    }
}

int is_ac_adapter_in_use(void)
{
    FILE *file;
    char file_data[32];
    int ret = 0;

    file = get_ac_adpater_state_file();
    if ( file == NULL )
        return ret;

    memset(file_data, 0, 32);
    fgets(file_data, 32, file);
    if ( strstr(file_data, "1") )
        ret = 1;

    fclose(file);

    return ret;
}

static void adjust_guest_battery_level(struct battery_status *status)
{
    unsigned short count;
    unsigned long remaining_capacity = 0;
    unsigned int remaining_percent;

    current_battery_level = NORMAL;
    if ( is_ac_adapter_in_use() ) /* charging */
        return;

   if ( last_full_capacity == 0 )
       return; //we are not fully initialized yet

    for ( count = 0; count < MAX_BATTERY_SUPPORTED; count++,status++ )
        if ( status->present == YES )
            remaining_capacity += status->remaining_capacity;

    remaining_percent = (remaining_capacity * 100) / last_full_capacity;
    if ( remaining_percent > BATTERY_WARNING_PERCENT )
        return;

    if ( remaining_percent <= BATTERY_CRITICAL_PERCENT )
        current_battery_level = CRITICAL;
    else if ( remaining_percent <= BATTERY_LOW_PERCENT )
        current_battery_level = LOW;
    else
        current_battery_level = WARNING;
}

static void write_current_battery_level_to_xenstore(void)
{
    char val[8];

    if ( current_battery_level == NORMAL )
    {
        xenstore_rm(XS_CURRENT_BATTERY_LEVEL);
        return;
    }

    sprintf(val, "%d", current_battery_level);
    xenstore_write(val, XS_CURRENT_BATTERY_LEVEL);
    notify_com_citrix_xenclient_xcpmd_battery_level_notification(xcdbus_conn, XCPMD_SERVICE, XCPMD_PATH);
    xcpmd_log(LOG_ALERT, "Battery level below normal  - %d!\n", current_battery_level);
}

static void write_battery_status_to_xenstore(struct battery_status *status)
{
    char val[35];
    unsigned short count;

    for ( count = 0; count < MAX_BATTERY_SUPPORTED; ++count, ++status )
    {
        if ( status->present == YES )
        {
	    memset(val, 0, 35);
	    snprintf(val, 3, "%02x", 16);
	    write_ulong_lsb_first(val+2, status->state);
	    write_ulong_lsb_first(val+10, status->present_rate);
	    write_ulong_lsb_first(val+18, status->remaining_capacity);
	    write_ulong_lsb_first(val+26, status->present_voltage);

            xenstore_write(val, count ? XS_BST1 : XS_BST);
        }
	else
            xenstore_rm(count ? XS_BST1 : XS_BST);
    }

    write_current_battery_level_to_xenstore();
#ifdef XCPMD_DEBUG
    xcpmd_log(LOG_DEBUG, "~Updated battery information in xenstore\n");
#endif
}

static int get_battery_status(struct battery_status *status)
{
    DIR *dir;
    int batn = 0;
    struct battery_status *current = status;
    int i, rc;

    dir = opendir(BATTERY_DIR_PATH);
    if ( !dir )
    {
        /* This is an error, the procfs always has the battery directory though it may be empty */
        xcpmd_log(LOG_ERR, "opendir failed for directory %s with error - %d\n", BATTERY_DIR_PATH, errno);
        return 0;
    }

    for (i = 0; i < MAX_BATTERY_SCANNED; ++i)
    {
        rc = get_next_battery_info_or_status(dir, BST, (void *)current, i);
        if (!rc)
            continue;

        print_battery_status(current);

        if ( current->present == NO )
            continue;

        batn++;
        if ( batn >= MAX_BATTERY_SUPPORTED )
            break;
        current++;
    }

    closedir(dir);

    /* returns count of slots with batteries present */
    return batn;
}

static void update_battery_status(void)
{
    struct battery_status status[MAX_BATTERY_SUPPORTED];

    if ( pm_specs & PM_SPEC_NO_BATTERIES )
        return;

    if ( get_battery_status(status) == 0 )
        return;

    adjust_guest_battery_level(status);
    write_battery_status_to_xenstore(status);
    write_battery_info(NULL);
}

static int open_thermal_files(char *subdir, FILE **trip_points_file, FILE **temp_file)
{
    char trip_points_file_name[64];
    char temperature_file_name[64];

    snprintf(trip_points_file_name, 64, THERMAL_TRIP_POINTS_FILE, subdir);
    snprintf(temperature_file_name, 64, THERMAL_TEMPERATURE_FILE, subdir);

    *trip_points_file = fopen(trip_points_file_name, "r");
    *temp_file = fopen(temperature_file_name, "r");

    if ( *trip_points_file == NULL || *temp_file == NULL )
    {
        if ( *trip_points_file )
        {
            fclose(*trip_points_file);
            *trip_points_file = NULL;
        }

        if ( *temp_file )
        {
            fclose(*temp_file);
            *temp_file = NULL;
        }
        return 0;
    }

    return 1;
}

/* Note:  Below is the simplest approach based on studying the
 * different thermal zones exposed by the OEMs at this point.
 * In specific Dell E6*00, Lenovo T400, HP 6930p was taken into
 * consideration before arriving at which thermal zone to expose
 * to the guest.  But, if we choose to expand our thermal zone
 * implementation to be closer to the underlying firmware, we
 * should revisit the below.
 */
static void get_thermal_files(FILE **trip_points_file, FILE **temp_file)
{
    if ( open_thermal_files("/THM", trip_points_file, temp_file) )
        return;

    if ( open_thermal_files("/CPUZ", trip_points_file, temp_file) )
        return;

    if ( open_thermal_files("/THM1", trip_points_file, temp_file) )
        return;

    open_thermal_files("/THM0", trip_points_file, temp_file);
}

static int get_attribute_value(char *line_info, char *attribute_name)
{
    char attrib_name[64];
    char attrib_value[64];
    char *delimiter;
    unsigned long length;

    length = strlen(line_info);
    delimiter = (char *) strchr( line_info, ':');
    if ( (!delimiter) || (delimiter == line_info) ||
         (delimiter == line_info + length) )
        return 0;

    strncpy(attrib_name, line_info, delimiter-line_info);
    if ( !strstr(attrib_name, attribute_name) )
        return 0;

    while ( *(delimiter+1) == ' ' )
    {
        delimiter++;
        if ( delimiter+1 == line_info + length)
            return 0;
    }

    strncpy(attrib_value, delimiter+1,
            (unsigned long)line_info + length -(unsigned long)delimiter);
    return strtoull(attrib_value, NULL, 10);
}

static int get_thermalzone_value(FILE *file, char *attribute_name)
{
    char line_info[256];
    int attribute_value;

    memset(line_info, 0, 256);
    while ( fgets(line_info, 1024, file) != NULL )
    {
        attribute_value = get_attribute_value(line_info, attribute_name);
        if ( attribute_value > 0 )
            return attribute_value;
        memset(line_info, 0, 256);
    }

    return 0;
}

static void update_thermal_info(void)
{
    char buffer[32];
    int current_temp, critical_trip_point;
    FILE *trip_points_file = NULL, *temp_file = NULL;

    get_thermal_files(&trip_points_file, &temp_file);
    if ( trip_points_file == NULL || temp_file == NULL )
        return;

    current_temp = get_thermalzone_value(temp_file, "temperature");
    critical_trip_point = get_thermalzone_value(trip_points_file, "critical");

    fclose(trip_points_file);
    fclose(temp_file);

    if ( current_temp <= 0 || critical_trip_point <= 0 )
        return;

    snprintf(buffer, 32, "%d", current_temp);
    xenstore_write(buffer, XS_CURRENT_TEMPERATURE);

    snprintf(buffer, 32, "%d", critical_trip_point);
    xenstore_write(buffer, XS_CRITICAL_TEMPERATURE);
#ifdef XCPMD_DEBUG
    xcpmd_log(LOG_DEBUG, "~Updated thermal information in xenstore\n");
#endif
}

int
xcpmd_process_input(int input_value)
{
    switch (input_value)
    {
        case XCPMD_INPUT_SLEEP:
            xcpmd_log(LOG_INFO, "Sleep button pressed input\n");
            xenstore_write("1", XS_SBTN_EVENT_PATH);
            notify_com_citrix_xenclient_xcpmd_sleep_button_pressed(xcdbus_conn, XCPMD_SERVICE, XCPMD_PATH);
            break;
        case XCPMD_INPUT_BRIGHTNESSUP:
        case XCPMD_INPUT_BRIGHTNESSDOWN:
            /* Only HP laptops use input events for brightness */
            if (pm_quirks & PM_QUIRK_HP_HOTKEY_INPUT)
                adjust_brightness(input_value == XCPMD_INPUT_BRIGHTNESSUP ? 1 : 0, 1);
            break;
        default:
            xcpmd_log(LOG_WARNING, "Input invalid value %d\n", input_value);
            break;
    };

#ifdef XCPMD_DEBUG
    xcpmd_log(LOG_DEBUG, "~Input value %d processed\n", input_value);
#endif

    return 0;
}

static void
refresh_battery_status_cb(const char *path, void *opaque)
{
    update_battery_status();
}

static void
refresh_thermal_info_cb(const char *path, void *opaque)
{
    update_thermal_info();
}


static bool pm_monitor_initialize(void)
{
    bool ret = false;

    ret = xenstore_watch(refresh_battery_status_cb, NULL, "/pm/events/refreshbatterystatus") &&
        xenstore_watch(refresh_thermal_info_cb, NULL, "/pm/events/refreshthermalinfo");

    xcpmd_log(LOG_INFO, "PM monitor initialized.\n");

    return ret;
}

static void pm_monitor_cleanup(void)
{
    xcpmd_log(LOG_INFO, "PM monitor cleanup\n");
    xenstore_watch(NULL, NULL, "/pm/events/refreshbatterystatus");
    xenstore_watch(NULL, NULL, "/pm/events/refreshthermalinfo");
}

void monitor_battery_level(int enable)
{
    /* Never turn on the battery monitor on systems with no batteries */
    if ( pm_specs & PM_SPEC_NO_BATTERIES )
        return;

    if ( enable != 0 )
    {
        monitoring_battery_level = 1;
        battery_level_under_threshold = 0;
        xcpmd_log(LOG_INFO, "Battery monitor running.\n");
    }
    else
    {
        monitoring_battery_level = 0;
        battery_level_under_threshold = 0;
        xcpmd_log(LOG_INFO, "Battery monitor stopped.\n");
    }
}

static void
wrapper_misc_event(int fd, short event, void *opaque)
{
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));

    check_hp_hotkey_switch();

    tv.tv_sec = 1;
    evtimer_add(&misc_event, &tv);
}

static void
wrapper_refresh_battery_event(int fd, short evemt, void *opaque)
{
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));

    update_battery_status();

    tv.tv_sec = 60;
    evtimer_add(&refresh_battery_event, &tv);
}

/* Removed extra worker thread since XC is no longer using uClib */
int main(int argc, char *argv[])
{
    int ret = 0;

#ifndef RUN_STANDALONE
    openlog("xcpmd", 0, LOG_DAEMON);
    daemonize();
#endif

    xcpmd_log(LOG_INFO, "Starting XenClient power management daemon.\n");

    event_init();
    if (xenstore_init() == -1)
    {
        xcpmd_log(LOG_ERR, "Unable to init xenstore\n");
        return -1;
    }

    /* Allow everyone to read from /pm/ in xenstore */
    xenstore_rm("/pm");
    xenstore_mkdir("/pm");
    xenstore_chmod("r0", 1, "/pm");

    xch = xc_interface_open(NULL, NULL, 0);
    if (xch == NULL)
    {
        xcpmd_log(LOG_ERR, "Unable to get a handle to XCLIB\n");
        goto xcpmd_err;
    }

    if (test_has_directio() == -1)
    {
        xcpmd_log(LOG_ERR, "Failure testing for Direct IO capabilities - error: %d\n", errno);
        goto xcpmd_err;
    }

    initialize_platform_info();
    initialize_system_state_info();

    /* Initialize xcpmd services */
    if (xcpmd_dbus_initialize() == -1)
    {
        xcpmd_log(LOG_ERR, "Failed to initialize DBUS server\n");
        goto xcpmd_err;
    }

    if (acpi_events_initialize() == -1)
    {
        xcpmd_log(LOG_ERR, "Failed to initialize ACPI events monitor\n");
        goto xcpmd_err;
    }

    if (!pm_monitor_initialize())
    {
        xcpmd_log(LOG_ERR, "Failed to initialize PM monitor\n");
        goto xcpmd_err;
    }

    if (netlink_init() != 0)
    {
        xcpmd_log(LOG_ERR, "Failed to initialize netlink\n");
        goto xcpmd_err;
    }

    /* XXX: Watch input queue and HP hotkey switch status */
    event_set(&misc_event, -1, EV_TIMEOUT | EV_PERSIST, wrapper_misc_event, NULL);
    wrapper_misc_event(0, 0, NULL);

    /* XXX: Setup a watch rather than something based on a timer */
    event_set(&refresh_battery_event, -1, EV_TIMEOUT | EV_PERSIST, wrapper_refresh_battery_event, NULL);
    wrapper_refresh_battery_event(0, 0, NULL);

    /* Run main server loop */
    event_dispatch();

    goto xcpmd_out;

xcpmd_err:
    ret = -1;
xcpmd_out:
    pm_monitor_cleanup();
    acpi_events_cleanup();
    xcpmd_dbus_cleanup();
    netlink_cleanup();

    if ( xch != NULL )
        xc_interface_close(xch);

#ifndef RUN_STANDALONE
    closelog();
#endif

    return ret;
}
