/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Secure Launch Resource Table
 *
 * Copyright (c) 2022, Oracle and/or its affiliates.
 */

#ifndef _LINUX_SLR_TABLE_H
#define _LINUX_SLR_TABLE_H

/* Put this in efi.h if it becomes a standard */
#define SLR_TABLE_GUID				EFI_GUID(0x877a9b2a, 0x0385, 0x45d1, 0xa0, 0x34, 0x9d, 0xac, 0x9c, 0x9e, 0x56, 0x5f)

/* SLR table header values */
#define SLR_TABLE_MAGIC		0x4452544d
#define SLR_TABLE_REVISION	1

/* SLR defined architectures */
#define SLR_INTEL_TXT		1
#define SLR_AMD_SKINIT		2

/* Log formats */
#define SLR_DRTM_TPM12_LOG	1
#define SLR_DRTM_TPM20_LOG	2

/* Tags */
#define SLR_ENTRY_END		0xffff
/* Types 0x0000 - 0x0007 are reserved */

#define SLR_ENTRY_DL_INFO	0x0008
#define SLR_ENTRY_LOG_INFO	0x0009

#ifndef __ASSEMBLY__

struct slr_table {
	u32 magic;
	u16 revision;
	u16 architecture;
	u32 size;
	u32 max_size;
	/* entries[] */
} __packed;

struct slr_entry_hdr {
	u16 tag;
	u16 size;
} __packed;

/*
 * DRTM Dynamic Launch Configuration
 */
struct slr_entry_dl_info {
	struct slr_entry_hdr hdr;
	u64 dl_handler;
	u64 dce_base;
	u32 dce_size;
	u64 dlme_entry;
} __packed;

/*
 * Log information
 */
struct slr_entry_log_info {
	struct slr_entry_hdr hdr;
	u16 format;
	u16 reserved;
	u64 addr;
	u32 size;
} __packed;

static inline void *slr_end_of_entrys(struct slr_table *table)
{
	return (((void *)table) + table->size);
}

static inline struct slr_entry_hdr *
slr_next_entry(struct slr_table *table,
		struct slr_entry_hdr *curr)
{
	struct slr_entry_hdr *next = (struct slr_entry_hdr *)
				((u8 *)curr + curr->size);

	if ((void *)next >= slr_end_of_entrys(table))
		return NULL;
	if (next->tag == SLR_ENTRY_END)
		return NULL;

	return next;
}

static inline struct slr_entry_hdr *
slr_next_entry_by_tag(struct slr_table *table,
		      struct slr_entry_hdr *entry,
		      u16 tag)
{
	if (!entry) /* Start from the beginning */
		entry = (struct slr_entry_hdr *)(((u8 *)table) + sizeof(*table));

	for ( ; ; ) {
		entry = slr_next_entry(table, entry);
		if (!entry)
			return NULL;

		if (entry->tag == tag)
			return entry;
	}

	return NULL;
}

static inline int
slr_add_entry(struct slr_table *table,
	      struct slr_entry_hdr *entry)
{
	struct slr_entry_hdr *end;

	if ((table->size + entry->size) > table->max_size)
		return -1;

	end = (struct slr_entry_hdr *)((u8 *)table + table->size
		- sizeof(*end));
	if (end->tag != SLR_ENTRY_END)
		return -1; /* malformed table */

	memcpy((u8 *)end + entry->size, end, sizeof(*end));
	memcpy((u8 *)end, entry, entry->size);
	table->size += entry->size;

	return 0;

}

#endif /* !__ASSEMBLY */

#endif /* _LINUX_SLR_TABLE_H */
