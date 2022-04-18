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

/* General values */
#define DRTM_TABLE_HEADER		0x0000	/* Always first */
#define DRTM_ENTRY_END			0xffff
#define DRTM_NO_SUBTYPE			0x0000

/* DRTM general entries */
#define DRTM_ENTRY_ARCHITECTURE		0x0001
#define DRTM_ENTRY_DCE_INFO		0x0002
#define DRTM_ENTRY_ENTRYPOINT		0x0003
#define DRTM_ENTRY_LOG_BUFFER		0x0004

struct drtm_entry_hdr {
	u16 type;
	u16 sub_type;
	u16 size;
} __packed;

struct drtm_table_header {
	struct drtm_entry_hdr hdr;
	u16 size;
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

/*
 * DRTM Dynamic Configuration Environment
 */
#define DRTM_DCE_TXT_ACM	1
#define DRTM_DCE_AMD_SLB	2

struct drtm_entry_dce_info {
	struct drtm_entry_hdr hdr;
	u64 dce_base;
	u32 dce_size;
} __packed;

/*
 * DRTM Entry Points
 */
#define DRTM_DLME_ENTRYPOINT	1
#define DRTM_DLE_ENTRYPOINT	2

struct drtm_entry_entrypoint {
	struct drtm_entry_hdr hdr;
	u64 phys_base;
} __packed;

/*
 * Logging Buffer Types
 */
#define DRTM_TPM12_LOG		1
#define DRTM_TPM20_LOG		2

struct drtm_entry_log {
	struct drtm_entry_hdr hdr;
	u64 log_base;
	u64 size;
} __packed;

static inline void *drtm_end_of_entrys(struct drtm_entry_hdr *head)
{
	struct drtm_table_header *hdr =
		(struct drtm_table_header *)head;

	return (((void *)head) + hdr->size);
}

static inline struct drtm_entry_hdr *
drtm_next_entry(struct drtm_entry_hdr *head,
		struct drtm_entry_hdr *curr)
{
	void *next = curr + curr->len;

	if (next >= drtm_end_of_entrys(head))
		return NULL;
	if (next->type == DRTM_ENTRY_END)
		return NULL;

	return next;
}

static inline struct drtm_entry_hdr *
drtm_next_of_type_subtype(struct drtm_entry_hdr *head,
			  struct drtm_entry_hdr *entry,
			  u16 type, u16 subtype)
{
	if (!entry) /* Start from the beginning */
		entry = head;

	for ( ; ; ) {
		entry = drtm_next_entry(entry);
		if (!entry)
			return NULL;

		if (entry->type == type &&
		    (subtype != DRTM_NO_SUBTYPE && entry->subtype == subtype))
			return entry;
	}

	return NULL;
}

#define drtm_next_of_type(h, e, t) \
	drtm_next_of_type_subtype(h, e, t, DRTM_NO_SUBTYPE)

#endif /* _LINUX_DRTM_TABLE_H */
