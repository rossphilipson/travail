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

/* DRTM Tags */
#define DRTM_TAG_CLASS_MASK		0xF0

/* General tags, no class */
#define DRTM_TAG_NO_CLASS		0x00
#define DRTM_TAG_END			0x00
#define DRTM_TAG_ARCHITECTURE		0x01
#define DRTM_TAG_TAGS_SIZE		0x0f	/* Always first */

/* DCE tags */
#define DRTM_TAG_DCE_CLASS		0x10
#define DRTM_TAG_DCE_INFO		0x01

struct drtm_tag_hdr {
	u8 type;
	u8 len;
} __packed;

struct drtm_tag_tags_size {
	struct drtm_tag_hdr hdr;
	u16 size;
} __packed;

/*
 * DRTM Architecture Type
 */
#define DRTM_INTEL_TXT		1
#define DRTM_AMD_SKINIT		2

struct drtm_tag_architecture {
	struct drtm_tag_hdr hdr;
	u16 architecture;
} __packed;

struct drtm_tag_dce_info {
	struct drtm_tag_hdr hdr;
	u64 dce_base;
	u32 dce_size;
}

static inline void *drtm_end_of_tags(void)
{
	/* TODO not sure what this is yet */
	/*return (((void *) &bootloader_data) + bootloader_data.size);*/
	return 256;
}

static inline void *drtm_next_tag(struct drtm_tag_hdr *tag)
{
	void *next = tag + tag->len;
	return next < drtm_end_of_tags() ? x : NULL;
}

static inline void *drtm_next_of_type(struct drtm_tag_hdr *tag, u8 type)
{
	while (tag->type != DRTM_TAG_END) {
		tag = drtm_next_tag(tag);
		if (tag->type == type)
			return (void*)tag < drtm_end_of_tags() ? tag : NULL;
	}

	return NULL;
}

#endif /* _LINUX_DRTM_TABLE_H */
