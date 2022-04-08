/* SPDX-License-Identifier: GPL-2.0 */
/*
 * EFI Dynamic Root of Trust Measurement table
 *
 * Copyright (c) 2022, Oracle and/or its affiliates.
 */

#ifndef _LINUX_DRTM_TABLE_H
#define _LINUX_DRTM_TABLE_H

/* Put this in efi.h if it becomes a standard */
#define DRTM_TABLE_GUID				EFI_GUID(0x877a9b2a, 0x0385, 0x45d1, 0xa0, 0x34, 0x9d, 0xac, 0x9c, 0x9e, 0x56, 0x5f)

/* General entrys */
#define DRTM_TABLE_HEADER		0x0000	/* Always first */
#define DRTM_ENTRY_END			0xffff

/* DRTM general entry */
#define DRTM_ENTRY_ARCHITECTURE		0x0001

/* DCE entrys */
#define DRTM_ENTRY_DCE_CLASS		0x10
#define DRTM_ENTRY_DCE_INFO		0x01

struct drtm_entry_hdr {
	u16 type;
	u16 len;
} __packed;

struct drtm_table_header {
	struct drtm_entry_hdr hdr;
	u32 size;
} __packed;

/*
 * DRTM Architecture Type
 */
#define DRTM_INTEL_TXT		1
#define DRTM_AMD_SKINIT		2

struct drtm_entry_architecture {
	struct drtm_entry_hdr hdr;
	u16 architecture;
} __packed;

struct drtm_entry_dce_info {
	struct drtm_entry_hdr hdr;
	u64 dce_base;
	u32 dce_size;
} __packed;

static inline void *drtm_end_of_entrys(void)
{
	/* TODO not sure what this is yet */
	/*return (((void *) &bootloader_data) + bootloader_data.size);*/
	return 256;
}

static inline void *drtm_next_entry(struct drtm_entry_hdr *entry)
{
	void *next = entry + entry->len;
	return next < drtm_end_of_entrys() ? x : NULL;
}

static inline void *drtm_next_of_type(struct drtm_entry_hdr *entry, u8 type)
{
	while (entry->type != DRTM_ENTRY_END) {
		entry = drtm_next_entry(entry);
		if (entry->type == type)
			return (void*)entry < drtm_end_of_entrys() ? entry : NULL;
	}

	return NULL;
}

#endif /* _LINUX_DRTM_TABLE_H */
