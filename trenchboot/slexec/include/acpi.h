/*	$OpenBSD: acpireg.h,v 1.17 2009/04/11 08:22:48 kettenis Exp $	*/
/*
 * Copyright (c) 2005 Thorsten Lockert <tholo@sigmasoft.com>
 * Copyright (c) 2005 Marco Peereboom <marco@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Portions copyright (c) 2010, Intel Corporation
 */

#ifndef __ACPI_H__
#define __ACPI_H__

#define RSDP_SCOPE1_LOW    (void *)0x000000
#define RSDP_SCOPE1_HIGH   (void *)0x000400
#define RSDP_SCOPE2_LOW    (void *)0x0E0000
#define RSDP_SCOPE2_HIGH   (void *)0x100000

/* Root System Descriptor Pointer (RSDP) for ACPI 1.0 */
struct acpi_rsdp1 {
    uint8_t  signature[8];
#define	RSDP_SIG	"RSD PTR "

    uint8_t  checksum; /* make sum == 0 */
    uint8_t  oemid[6];
    uint8_t  revision;	/* 0 for v1.0, 2 for v2.0 */
    uint32_t rsdt;     /* physical */
} __packed;

/* Root System Descriptor Pointer (RSDP) for ACPI 2.0 */
struct acpi_rsdp {
    struct acpi_rsdp1 rsdp1;
    /*
     * The following values are only valid
     * when rsdp_revision == 2
     */
    uint32_t rsdp_length;      /* length of rsdp */
    uint64_t rsdp_xsdt;        /* physical */
    uint8_t  rsdp_extchecksum; /* entire table */
    uint8_t  rsdp_reserved[3]; /* must be zero */
} __packed;

/* Common System Description Table Header */
struct acpi_table_header {
    uint8_t  signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    uint8_t  oemid[6];
    uint8_t  oemtableid[8];
    uint32_t oemrevision;
    uint8_t  aslcompilerid[4];
    uint32_t aslcompilerrevision;
} __packed;

/* Root System Description Table (RSDT) */
struct acpi_rsdt {
    struct acpi_table_header hdr;
#define RSDT_SIG "RSDT"

    uint32_t table_offsets[1];
} __packed;

/* Extended System Descriptiion Table */
struct acpi_xsdt {
    struct acpi_table_header hdr;
#define XSDT_SIG "XSDT"

    uint64_t table_offsets[1];
} __packed;


/* Generic Address Structure */
struct acpi_gas {
    uint8_t address_space_id;
#define GAS_SYSTEM_MEMORY    0
#define GAS_SYSTEM_IOSPACE   1
#define GAS_PCI_CFG_SPACE    2
#define GAS_EMBEDDED         3
#define GAS_SMBUS            4
#define GAS_FUNCTIONAL_FIXED 127
    uint8_t register_bit_width;
    uint8_t register_bit_offset;
    uint8_t access_size;
#define GAS_ACCESS_UNDEFINED 0
#define GAS_ACCESS_BYTE	     1
#define GAS_ACCESS_WORD      2
#define GAS_ACCESS_DWORD     3
#define GAS_ACCESS_QWORD     4
    uint64_t address;
} __packed;

/* Fixed ACPI Descriptiion Table */
struct acpi_fadt {
    struct acpi_table_header hdr;
#define	FADT_SIG "FACP"

    uint32_t firmware_ctl;   /* phys addr FACS */
    uint32_t dsdt;           /* phys addr DSDT */

/* int_model is defined in ACPI 1.0, in ACPI 2.0, it should be zero */
    uint8_t  int_model;      /* interrupt model (hdr_revision < 3) */

#define	FADT_INT_DUAL_PIC   0
#define	FADT_INT_MULTI_APIC 1
    uint8_t  pm_profile;     /* power mgmt profile */
#define	FADT_PM_UNSPEC      0
#define	FADT_PM_DESKTOP     1
#define	FADT_PM_MOBILE      2
#define	FADT_PM_WORKSTATION 3
#define	FADT_PM_ENT_SERVER  4
#define	FADT_PM_SOHO_SERVER 5
#define	FADT_PM_APPLIANCE   6
#define	FADT_PM_PERF_SERVER 7
    uint16_t sci_int;        /* SCI interrupt */
    uint32_t smi_cmd;        /* SMI command port */
    uint8_t  acpi_enable;    /* value to enable */
    uint8_t  acpi_disable;   /* value to disable */
    uint8_t  s4bios_req;     /* value for S4 */
    uint8_t  pstate_cnt;     /* value for performance (hdr_revision > 2) */
    uint32_t pm1a_evt_blk;   /* power management 1a */
    uint32_t pm1b_evt_blk;   /* power mangement 1b */
    uint32_t pm1a_cnt_blk;   /* pm control 1a */
    uint32_t pm1b_cnt_blk;   /* pm control 1b */
    uint32_t pm2_cnt_blk;    /* pm control 2 */
    uint32_t pm_tmr_blk;
    uint32_t gpe0_blk;
    uint32_t gpe1_blk;
    uint8_t  pm1_evt_len;
    uint8_t  pm1_cnt_len;
    uint8_t  pm2_cnt_len;
    uint8_t  pm_tmr_len;
    uint8_t  gpe0_blk_len;
    uint8_t  gpe1_blk_len;
    uint8_t  gpe1_base;
    uint8_t  cst_cnt;	     /* (hdr_revision > 2) */
    uint16_t p_lvl2_lat;
    uint16_t p_lvl3_lat;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t  duty_offset;
    uint8_t  duty_width;
    uint8_t  day_alrm;
    uint8_t  mon_alrm;
    uint8_t  century;
    uint16_t iapc_boot_arch; /* (hdr_revision > 2) */
#define	FADT_LEGACY_DEVICES 0x0001 /* Legacy devices supported */
#define	FADT_i8042          0x0002 /* Keyboard controller present */
#define	FADT_NO_VGA         0x0004 /* Do not probe VGA */
    uint8_t  reserved1;
    uint32_t flags;
#define	FADT_WBINVD                    0x00000001
#define	FADT_WBINVD_FLUSH              0x00000002
#define	FADT_PROC_C1                   0x00000004
#define	FADT_P_LVL2_UP                 0x00000008
#define	FADT_PWR_BUTTON                0x00000010
#define	FADT_SLP_BUTTON                0x00000020
#define	FADT_FIX_RTC                   0x00000040
#define	FADT_RTC_S4                    0x00000080
#define	FADT_TMR_VAL_EXT               0x00000100
#define	FADT_DCK_CAP                   0x00000200
#define	FADT_RESET_REG_SUP             0x00000400
#define	FADT_SEALED_CASE               0x00000800
#define	FADT_HEADLESS                  0x00001000
#define	FADT_CPU_SW_SLP                0x00002000
#define	FADT_PCI_EXP_WAK               0x00004000
#define	FADT_USE_PLATFORM_CLOCK	       0x00008000
#define	FADT_S4_RTC_STS_VALID          0x00010000
#define	FADT_REMOTE_POWER_ON_CAPABLE   0x00020000
#define	FADT_FORCE_APIC_CLUSTER_MODEL  0x00040000
#define	FADT_FORCE_APIC_PHYS_DEST_MODE 0x00080000
    /*
     * Following values only exist when rev > 1
     * If the extended addresses exists, they
     * must be used in preferense to the non-
     * extended values above
     */
    struct acpi_gas reset_reg;
    uint8_t  reset_value;

    uint8_t  reserved2a;
    uint8_t  reserved2b;
    uint8_t  reserved2c;

    uint64_t x_firmware_ctl;
    uint64_t x_dsdt;
    struct acpi_gas x_pm1a_evt_blk;
    struct acpi_gas x_pm1b_evt_blk;
    struct acpi_gas x_pm1a_cnt_blk;
    struct acpi_gas x_pm1b_cnt_blk;
    struct acpi_gas x_pm2_cnt_blk;
    struct acpi_gas x_pm_tmr_blk;
    struct acpi_gas x_gpe0_blk;
    struct acpi_gas x_gpe1_blk;
} __packed;

struct acpi_madt {
    struct acpi_table_header hdr;
#define MADT_SIG "APIC"

    uint32_t local_apic_address;
    uint32_t flags;
#define ACPI_APIC_PCAT_COMPAT 0x00000001
} __packed;

struct acpi_madt_lapic {
    uint8_t  apic_type;
#define	ACPI_MADT_LAPIC	 0
    uint8_t  length;
    uint8_t  acpi_proc_id;
    uint8_t  apic_id;
    uint32_t flags;
#define	ACPI_PROC_ENABLE 0x00000001
} __packed;

struct acpi_madt_ioapic {
    uint8_t  apic_type;
#define	ACPI_MADT_IOAPIC 1
    uint8_t  length;
    uint8_t  acpi_ioapic_id;
    uint8_t  reserved;
    uint32_t address;
    uint32_t global_int_base;
} __packed;
typedef struct acpi_madt_ioapic acpi_table_ioapic_t;

struct acpi_madt_override {
    uint8_t  apic_type;
#define	ACPI_MADT_OVERRIDE    2
    uint8_t  length;
    uint8_t  bus;
#define	ACPI_OVERRIDE_BUS_ISA 0
    uint8_t  source;
    uint32_t global_int;
    uint16_t flags;
#define	ACPI_OVERRIDE_POLARITY_BITS 0x3
#define	ACPI_OVERRIDE_POLARITY_BUS  0x0
#define	ACPI_OVERRIDE_POLARITY_HIGH 0x1
#define	ACPI_OVERRIDE_POLARITY_LOW  0x3
#define	ACPI_OVERRIDE_TRIGGER_BITS  0xc
#define	ACPI_OVERRIDE_TRIGGER_BUS   0x0
#define	ACPI_OVERRIDE_TRIGGER_EDGE  0x4
#define	ACPI_OVERRIDE_TRIGGER_LEVEL 0xc
} __packed;

struct acpi_madt_nmi {
    uint8_t  apic_type;
#define	ACPI_MADT_NMI 3
    uint8_t  length;
    uint16_t flags;          /* Same flags as acpi_madt_override */
    uint32_t global_int;
} __packed;

struct acpi_madt_lapic_nmi {
    uint8_t  apic_type;
#define	ACPI_MADT_LAPIC_NMI 4
    uint8_t  length;
    uint8_t  acpi_proc_id;
    uint16_t flags;          /* Same flags as acpi_madt_override */
    uint8_t  local_apic_lint;
} __packed;

struct acpi_madt_lapic_override {
    uint8_t  apic_type;
#define	ACPI_MADT_LAPIC_OVERRIDE 5
    uint8_t  length;
    uint16_t reserved;
    uint64_t lapic_address;
} __packed;

struct acpi_madt_io_sapic {
    uint8_t  apic_type;
#define	ACPI_MADT_IO_SAPIC 6
    uint8_t  length;
    uint8_t  iosapic_id;
    uint8_t  reserved;
    uint32_t global_int_base;
    uint64_t iosapic_address;
} __packed;

struct acpi_madt_local_sapic {
    uint8_t  apic_type;
#define	ACPI_MADT_LOCAL_SAPIC 7
    uint8_t  length;
    uint8_t  acpi_proc_id;
    uint8_t  local_sapic_id;
    uint8_t  local_sapic_eid;
    uint8_t  reserved[3];
    uint32_t flags;      /* Same flags as acpi_madt_lapic */
    uint32_t acpi_proc_uid;
    uint8_t  acpi_proc_uid_string[1];
} __packed;

struct acpi_madt_platform_int {
    uint8_t  apic_type;
#define	ACPI_MADT_PLATFORM_INT        8
    uint8_t  length;
    uint16_t flags;          /* Same flags as acpi_madt_override */
    uint8_t  int_type;
#define	ACPI_MADT_PLATFORM_PMI        1
#define	ACPI_MADT_PLATFORM_INIT       2
#define	ACPI_MADT_PLATFORM_CORR_ERROR 3
    uint8_t  proc_id;
    uint8_t  proc_eid;
    uint8_t  io_sapic_vec;
    uint32_t global_int;
    uint32_t platform_int_flags;
#define	ACPI_MADT_PLATFORM_CPEI       0x00000001
} __packed;

union acpi_madt_entry {
    struct acpi_madt_lapic madt_lapic;
    struct acpi_madt_ioapic madt_ioapic;
    struct acpi_madt_override madt_override;
    struct acpi_madt_nmi madt_nmi;
    struct acpi_madt_lapic_nmi madt_lapic_nmi;
    struct acpi_madt_lapic_override madt_lapic_override;
    struct acpi_madt_io_sapic madt_io_sapic;
    struct acpi_madt_local_sapic madt_local_sapic;
    struct acpi_madt_platform_int madt_platform_int;
} __packed;

struct device_scope {
    uint8_t  type;
    uint8_t  length;
    uint16_t reserved;
    uint8_t  enumeration_id;
    uint8_t  start_bus_number;
    uint16_t path[1]; /* Path starts here */
} __packed;

struct dmar_remapping {
    uint16_t type;
#define DMAR_REMAPPING_DRHD 0
#define DMAR_REMAPPING_RMRR 1
#define DMAR_REMAPPING_ATSR 2
#define DMAR_REMAPPING_RHSA 3
#define DMAR_REMAPPING_RESERVED 4
    uint16_t length;
    uint8_t flags;
#define REMAPPING_INCLUDE_PCI_ALL Ox01

    uint8_t reserved;
    uint16_t segment_number;
    uint8_t register_base_address[8];
    struct device_scope device_scope_entry[1]; /* Device Scope starts here */
} __packed;

struct acpi_dmar {
    struct acpi_table_header hdr;
#define DMAR_SIG "DMAR"
    uint8_t host_address_width;
    uint8_t flags;
#define DMAR_INTR_REMAP 0x01

    uint8_t reserved[10];
    struct dmar_remapping table_offsets[1]; /* dmar_remapping structure starts here */
} __packed;

struct acpi_mcfg_mmcfg {
    uint64_t base_address;
    uint16_t group_number;
    uint8_t start_bus_number;
    uint8_t end_bus_number;
    uint32_t reserved;
} __packed;

struct acpi_mcfg {
    struct acpi_table_header hdr;
#define MCFG_SIG "MCFG"

    uint64_t reserved;
    /* struct acpi_mcfg_mmcfg table_offsets[1]; */
    uint32_t base_address;
} __packed;

typedef struct acpi_mcfg acpi_table_mcfg_t;

extern struct acpi_rsdp *get_rsdp(loader_ctx *lctx);
extern bool vtd_bios_enabled(void);

#endif	/* __ACPI_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
