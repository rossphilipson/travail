/*
 * Intel TXT definitions header file.
 *
 * Copyright (c) 2019 Oracle and/or its affiliates. All rights reserved.
 *
 * Author:
 *     Ross Philipson <ross.philipson@oracle.com>
 */

#ifndef X86_TXT_H
#define X86_TXT_H

/* Intel TXT Software Developers Guide */

/* Chapter 2, Table 2 MLE/SINIT Capabilities Field Bit Definitions */

#define TXT_PLATFORM_TYPE_LEGACY	0
#define TXT_PLATFORM_TYPE_CLIENT	1
#define TXT_PLATFORM_TYPE_SERVER        2
#define TXT_PLATFORM_TYPE_RESERVED	3

#define TXT_CAPS_GETSEC_WAKE_SUPPORT	(1<<0)
#define TXT_CAPS_MONITOR_SUPPORT	(1<<1)
#define TXT_CAPS_ECX_PT_SUPPORT		(1<<2)
#define TXT_CAPS_STM_SUPPORT		(1<<3)
#define TXT_CAPS_TPM12_LEGACY_PCR_USAGE	(1<<4)
#define TXT_CAPS_TPM12_AUTH_PCR_USAGE	(1<<5)
#define TXT_CAPS_PLATFORM_TYPE		(3<<6)
#define TXT_CAPS_MAXPHYSADDR_SUPPORT	(1<<8)
#define TXT_CAPS_TPM20_EVLOG_SUPPORT	(1<<9)
#define TXT_CAPS_CBNT_SUPPORT		(1<<10)
/* Rest is reserved */

/* Appendix A TXT Execution Technology Authenticated Code Modules */
/* A.1 Authenticated Code Module Format */

#define TXT_ACM_MODULE_TYPE		2

#define TXT_ACM_MODULE_SUB_TYPE_TXT_ACM	0
#define TXT_ACM_MODULE_SUB_TYPE_S_ACM	1

#define TXT_ACM_HEADER_LEN_0_0		161
#define TXT_ACM_HEADER_LEN_3_0		224

#define TXT_ACM_HEADER_VERSION_0_0	0x0000
#define TXT_ACM_HEADER_VERSION_3_0	0x0300

#define TXT_ACM_FLAG_PREPRODUCTION	(1<<14)
#define TXT_ACM_FLAG_DEBUG_SIGNED	(1<<15)

#define TXT_ACM_MODULE_VENDOR_INTEL	0x00008086

struct txt_acm_header {
	u16 module_type;
	u16 module_sub_type;
	u32 header_len;
	u32 header_version;
	u16 chipset_id;
	u16 flags;
	u32 module_vendor;
	u32 date; /* e.g 20131231H == December 31, 2013 */
	u32 size; /* multiples of 4 bytes */
	u16 txt_svn;
	u16 se_svn;
	u32 code_control;
	u32 error_entry_point;
	u32 gdt_limit;
	u32 gdt_base;
	u32 seg_sel;
	u32 entry_point;
	u8 reserved2[64];
	u32 key_size;
	u32 scratch_size;
	/* RSA Pub Key and Signature */
} __attribute__((packed));

#define TXT_ACM_UUID "\xaa\x3a\xc0\x7f\xa7\x46\xdb\x18\x2e\xac\x69\x8f\x8d\x41\x7f\x5a"

#define TXT_ACM_CHIPSET_TYPE_BIOS	0
#define TXT_ACM_CHIPSET_TYPE_SINIT	1
#define TXT_ACM_CHIPSET_TYPE_BIOS_RACM	8
#define TXT_ACM_CHIPSET_TYPE_SINIT_RACM	9

struct txt_acm_info_table {
	u8 uuid[16];
	u8 chipset_acm_type;
	u8 version;
	u16 length;
	u32 chipset_id_list;
	u32 os_sinit_data_ver;
	u32 min_mle_header_ver;
	u32 capabilities;
	u32 acm_version_revision;
	u32 processor_id_list;
	/* Version >= 5 */
	u32 tpm_info_list;
} __attribute__((packed));

struct txt_acm_chipset_id_list {
	u32 count;
	/* Array of chipset ID structs */
} __attribute__((packed));

#define TXT_ACM_REVISION_ID_MASK	(1<<0)

struct txt_acm_chipset_id {
	u32 flags;
	u16 vendor_id;
	u16 device_id;
	u16 revision_id;
	u8 reserved[6];
} __attribute__((packed));

struct txt_acm_processor_id_list {
	u32 count;
	/* Array of processor ID structs */
} __attribute__((packed));

struct txt_acm_processor_id {
	u32 fms;
	u32 fms_mask;
	u64 platform_id;
	u64 platform_mask;
} __attribute__((packed));

/* Appendix B SMX Interaction with Platform */
/* B.1 Intel Trusted Execution Technology Configuration Registers */

#ifdef CONFIG_X86_64
#define TXT_PUB_CONFIG_REGS		0xfed30000ULL
#define TXT_PRIV_CONFIG_REGS		0xfed20000ULL
#else
#define TXT_PUB_CONFIG_REGS		0xfed30000
#define TXT_PRIV_CONFIG_REGS		0xfed20000
#endif

#define TXT_STS				0x0000
#define TXT_ESTS			0x0008
#define TXT_ERRORCODE			0x0030
#define TXT_CMD_RESET			0x0038
#define TXT_CMD_CLOSE_PRIVATE		0x0048
#define TXT_VER_FSBIF			0x0100
#define TXT_DIDVID			0x0110
#define TXT_VER_QPIIF			0x0200
#define TXT_CMD_UNLOCK_MEM_CONFIG	0x0218
#define TXT_SINIT_BASE			0x0270
#define TXT_SINIT_SIZE			0x0278
#define TXT_MLE_JOIN			0x0290
#define TXT_HEAP_BASE			0x0300
#define TXT_HEAP_SIZE			0x0308
#define TXT_MSEG_BASE			0x0310
#define TXT_MSEG_SIZE			0x0318
#define TXT_DPR				0x0330
#define TXT_CMD_OPEN_LOCALITY1		0x0380
#define TXT_CMD_CLOSE_LOCALITY1		0x0388
#define TXT_CMD_OPEN_LOCALITY2		0x0390
#define TXT_CMD_CLOSE_LOCALITY2		0x0398
#define TXT_PUBLIC_KEY			0x0400
#define TXT_CMD_SECRETS			0x08e0
#define TXT_CMD_NO_SECRETS		0x08e8
#define TXT_E2STS			0x08f0

union txt_didvid {
	u64 value;
	struct {
		u16 vid;
		u16 did;
		u16 rid;
		u16 id_ext;
	};
} __attribute__((packed));

#define TXT_VERSION_DEBUG_FUSED		(1<<31)

/* Appendix C Intel TXT Heap Memory */

/* Ext Data Structs */

struct txt_heap_uuid {
	u32 data1;
	u16 data2;
	u16 data3;
	u16 data4;
	u8 data5[6];
} __attribute__((packed));

struct txt_heap_ext_data_element
{
	u32 type;
	u32 size;
	/* Data */
} __attribute__((packed));

#define TXT_HEAP_EXTDATA_TYPE_END			0

struct txt_heap_end_element {
	u32 type;
	u32 size;
} __attribute__((packed));

#define TXT_HEAP_EXTDATA_TYPE_BIOS_SPEC_VER		1

struct txt_heap_bios_spec_ver_element {
	u16 spec_ver_major;
	u16 spec_ver_minor;
	u16 spec_ver_revision;
} HEAP_BIOS_SPEC_VER_ELEMENT;

#define TXT_HEAP_EXTDATA_TYPE_ACM			2

struct txt_heap_acm_element {
	u32 num_acms;
	/* Array of num_acms u64 addresses */
} __attribute__((packed));

#define TXT_HEAP_EXTDATA_TYPE_STM			3

struct txt_heap_stm_element {
	/* STM specific BIOS properties */
} __attribute__((packed));

#define TXT_HEAP_EXTDATA_TYPE_CUSTOM			4

struct txt_heap_custom_element {
	struct txt_heap_uuid uuid;
	  /* Vendor Data */
} __attribute__((packed));

#define TXT_HEAP_EXTDATA_TYPE_TPM_EVENT_LOG_PTR		5

struct txt_heap_event_log_element {
	u64 event_log_phys_addr;
} __attribute__((packed));

#define TXT_HEAP_EXTDATA_TYPE_MADT			6

struct txt_heap_madt_element {
	/* Copy of ACPI MADT table */
} __attribute__((packed));

#define TXT_HEAP_EXTDATA_TYPE_EVENT_LOG_POINTER2_1	8

struct txt_heap_event_log_pointer2_1_element {
	u64 phys_addr;
	u32 allocated_event_container_size;
	u32 first_record_offset;
	u32 next_record_offset;
} __attribute__((packed));

#define TXT_HEAP_EXTDATA_TYPE_MCFG			8

struct txt_heap_mcfg_element {
	/* Copy of ACPI MCFG table */
} __attribute__((packed));

/* TXT Heap Tables */

struct txt_bios_data {
	u32 version; /* Currently 5 for TPM 1.2 and 6 for TPM 2.0 */
	u32 bios_sinit_size;
	u64 reserved1;
	u64 reserved22;
	u32 num_logical_procs;
	/* Versions >= 5 with updates in version 6 */
	u32 sinit_flags;
	u32 mle_flags;
	/* Versions >= 4 */
	/* Ext Data Elements */
} __attribute__((packed));

struct txt_os_mle_data {
	/* This structure is implementation specific */
} __attribute__((packed));

struct txt_os_sinit_data {
	u32 version; /* Currently 6 for TPM 1.2 and 7 for TPM 2.0 */
	u32 flags;
	u64 mle_ptab;
	u64 mle_size;
	u64 mle_hdr_base;
	u64 vtd_pmr_lo_base;
	u64 vtd_pmr_lo_size;
	u64 vtd_pmr_hi_base;
	u64 vtd_pmr_hi_size;
	u64 lcp_po_base;
	u64 lcp_po_size;
	u32 capabilities;
	/* Version = 5 */
	u64    efi_rsdt_ptr;
	/* Versions >= 6 */
	/* Ext Data Elements */
} __attribute__((packed));

struct txt_sinit_mle_data {
	u32 version;             /* Current values are 6 through 9 */
	/* Versions <= 8 */
	u8 bios_acm_id[20];
	u32 edx_senter_flags;
	u64 mseg_valid;
	u8 sinit_hash[20];
	u8 mle_hash[20];
	u8 stm_hash[20];
	u8 lcp_policy_hash[20];
	u32 lcp_policy_control;
	/* Versions >= 7 */
	u32 rlp_wakeup_addr;
	u32 reserved;
	u32 num_of_sinit_mdrs;
	u32 sinit_mdrs_table_offset;
	u32 sinit_vtd_dmar_table_size;
	u32 sinit_vtd_dmar_table_offset;
	/* Versions >= 8 */
	u32 processor_scrtm_status;
	/* Versions >= 9 */
	/* Ext Data Elements */
} __attribute__((packed));

struct txt_sinit_memory_descriptor_records {
	u64 address;
	u64 length;
	u8 type;
	u8 reserved[7];
} __attribute__((packed));

/* Section 2 Measured Launch Environment */
/* 2.1 MLE Architecture Overview */
/* Table 1. MLE Header structure */

struct txt_mle_header {
	u8 uuid[16];
	u32 header_len;
	u32 version;
	u32 entry_point;
	u32 first_valid_page;
	u32 mle_start;
	u32 mle_end;
	u32 capabilities;
	u32 cmdline_start;
	u32 cmdline_end;
} __attribute__((packed));

/* TXT register and heap access */

static inline u64 txt_read64(u32 reg)
{
	/* TODO implementation specific */
	return 0;
}

static inline void txt_write64(u32 reg)
{
	/* TODO implementation specific */
}

static inline u64 txt_read_reg(u32 reg, u8 read_public)
{
	u8 *addr = (u8*)(read_public ? TXT_PUB_CONFIG_REGS :
			 TXT_PRIV_CONFIG_REGS);

	return txt_read64(addr + reg);
}

static inline void txt_write_reg(u32 reg, u64 val, u8 read_public)
{
	u8 *addr = (u8*)(read_public ? TXT_PUB_CONFIG_REGS :
			 TXT_PRIV_CONFIG_REGS);

	txt_write64(val, addr + reg);
}

static inline u8 *txt_get_heap(void)
{
#ifdef CONFIG_X86_64
	return (u8*)txt_read_reg(TXT_HEAP_BASE, 1);
#else
	return (u8*)(u32)txt_read_reg(TXT_HEAP_BASE, 1);
#endif
}

static inline u64 txt_bios_data_size(u8 *heap)
{
	return *(u64 *)heap;
}

static inline struct txt_bios_data *txt_bios_data_start(u8 *heap)
{
	return (struct txt_bios_data*)(heap + sizeof(u64));
}

static inline u64 txt_os_mle_data_size(u8 *heap)
{
	return *(u64 *)(heap + txt_bios_data_size(heap));
}

static inline struct txt_os_mle_data *txt_os_mle_data_start(u8 *heap)
{
	return (struct txt_os_mle_data*)(heap + txt_bios_data_size(heap) +
					 sizeof(u64));
}

static inline u64 txt_os_sinit_data_size(u8 *heap)
{
	return *(u64 *)(heap + txt_bios_data_size(heap) +
			txt_os_mle_data_size(heap));
}

static inline struct txt_os_sinit_data *txt_os_sinit_data_start(u8 *heap)
{
	return (struct txt_os_sinit_data*)(heap +
			txt_bios_data_size(heap) +
			txt_os_mle_data_size(heap) + sizeof(u64));
}

static inline u64 txt_sinit_mle_data_size(u8 *heap)
{
	return *(u64 *)(heap + txt_bios_data_size(heap) +
			txt_os_mle_data_size(heap) +
			txt_os_sinit_data_size(heap));
}

static inline struct txt_sinit_mle_data *txt_sinit_mle_data_start(u8 *heap)
{
	return (struct txt_sinit_mle_data*)(heap +
			txt_bios_data_size(heap) +
			txt_os_mle_data_size(heap) +
			txt_os_sinit_data_size(heap) +
			sizeof(u64));
}

/* Intel 64 and IA-32 Architectures Software Developer’s Manual */
/* Volume 2 (2A, 2B, 2C & 2D): Instruction Set Reference, A-Z */

/* CHAPTER 6 SAFER MODE EXTENSIONS REFERENCE */

#define SMX_LEAF_CAPABILITIES	0
#define SMX_LEAF_UNDEFINED	1
#define SMX_LEAF_ENTERACCS	2
#define SMX_LEAF_EXITAC 	3
#define SMX_LEAF_SENTER		4
#define SMX_LEAF_SEXIT		5
#define SMX_LEAF_PARAMETERS	6
#define SMX_LEAF_SMCTRL		7
#define SMX_LEAF_WAKEUP		8

#define SMX_CAPABILITY_CHIPSET_PRESENT	(1<<0)
#define SMX_CAPABILITY_UNDEFINED	(1<<1)
#define SMX_CAPABILITY_ENTERACCS	(1<<2)
#define SMX_CAPABILITY_EXITAC		(1<<3)
#define SMX_CAPABILITY_SENTER		(1<<4)
#define SMX_CAPABILITY_SEXIT		(1<<5)
#define SMX_CAPABILITY_PARAMETERS	(1<<6)
#define SMX_CAPABILITY_SMCTRL		(1<<7)
#define SMX_CAPABILITY_WAKEUP		(1<<8)
#define SMX_CAPABILITY_EXTENDED_LEAFS	(1<<31)

static inline u32 txt_getsec_capabilities(u32 index)
{
	u32 caps;

	__asm__ __volatile__ (".byte 0x0f,0x37\n"
			: "=a" (caps)
			: "a" (SMX_LEAF_CAPABILITIES), "b" (index));
	return caps;
}

static inline void txt_getsec_enteraccs(u32 acm_phys_addr, u32 acm_size)
{
	__asm__ __volatile__ (".byte 0x0f,0x37\n" :
			: "a" (SMX_LEAF_ENTERACCS),
			  "b" (acm_phys_addr), "c" (acm_size));
}

static inline void txt_getsec_exitac(u32 near_jump)
{
	__asm__ __volatile__ (".byte 0x0f,0x37\n" :
			: "a" (SMX_LEAF_EXITAC), "b" (near_jump));
}

static inline void txt_getsec_senter(u32 acm_phys_addr, u32 acm_size)
{
	__asm__ __volatile__ (".byte 0x0f,0x37\n" :
			: "a" (SMX_LEAF_SENTER),
			  "b" (acm_phys_addr), "c" (acm_size));
}

static inline void txt_getsec_sexit(void)
{
	__asm__ __volatile__ (".byte 0x0f,0x37\n" : : "a" (SMX_LEAF_SEXIT));
}

#define SMX_PARAMETER_TYPE_MASK		0x1f

#define SMX_PARAMETER_MAX_VERSIONS	0x20

#define SMX_GET_MAX_ACM_SIZE(v)		((v & ~SMX_PARAMETER_TYPE_MASK)*0x20)

#define SMX_ACM_MEMORY_TYPE_UC		0x000000100
#define SMX_ACM_MEMORY_TYPE_WC		0x000000200
#define SMX_ACM_MEMORY_TYPE_WT		0x000001000
#define SMX_ACM_MEMORY_TYPE_WP		0x000002000
#define SMX_ACM_MEMORY_TYPE_WB		0x000004000

#define SMX_GET_ACM_MEMORY_TYPES(v)	(v & ~SMX_PARAMETER_TYPE_MASK)

#define SMX_GET_SENTER_CONTROLS(v)	((v & 0x7f00) >> 8)

#define SMX_PROCESSOR_BASE_SCRTM	0x000000020
#define SMX_MACHINE_CHECK_HANLDING	0x000000040
#define SMX_GET_TXT_EXT_FEATURES(v)	(v & (SMX_PROCESSOR_BASE_SCRTM|SMX_MACHINE_CHECK_HANLDING))

#define SMX_DEFAULT_VERSION		0x0
#define SMX_DEFAULT_VERSION_MAX		0xfffffffff
#define SMX_DEFAULT_MAX_ACM_SIZE	0x8000 /* 32K */
#define SMX_DEFAULT_ACM_MEMORY_TYPE	SMX_ACM_MEMORY_TYPE_UC
#define SMX_DEFAULT_SENTER_CONTROLS	0x0

struct smx_supported_versions {
	u32 mask;
	u32 version;
} __attribute__((packed));

struct smx_parameters {
	struct smx_supported_versions versions[SMX_PARAMETER_MAX_VERSIONS];
	u32 version_count;
	u32 max_acm_size;
	u32 acm_memory_types;
	u32 senter_controls;
	u32 txt_feature_ext_flags;
} __attribute__((packed));

static inline void txt_getsec_parameters(u32 index, u32 *eax_out,
					 u32 *ebx_out, u32 *ecx_out)
{
	if (!eax_out || !ebx_out || !ecx_out)
		return;

	__asm__ __volatile__ (".byte 0x0f,0x37\n"
			: "=a" (*eax_out), "=b" (*ebx_out), "=c" (*ecx_out)
			: "a" (SMX_LEAF_PARAMETERS), "b" (index));
}

static inline void txt_getsec_smctrl(void)
{
	__asm__ __volatile__ (".byte 0x0f,0x37\n" :
			: "a" (SMX_LEAF_SMCTRL), "b" (0));
}

static inline void txt_getsec_wakeup(void)
{
	__asm__ __volatile__ (".byte 0x0f,0x37\n" : : "a" (SMX_LEAF_WAKEUP));
}

#endif
