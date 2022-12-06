/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2022  Oracle and/or its affiliates.
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Secure Launch Resource Table definitions
 */

#ifndef GRUB_SLR_TABLE_H
#define GRUB_SLR_TABLE_H 1

#define GRUB_EFI_SLR_TABLE_GUID \
  { 0x877a9b2a, 0x0385, 0x45d1, { 0xa0, 0x34, 0x9d, 0xac, 0x9c, 0x9e, 0x56, 0x5f }}

/* SLR table header values */
#define GRUB_SLR_TABLE_MAGIC		0x4452544d
#define GRUB_SLR_TABLE_REVISION		1

/* SLR defined architectures */
#define GRUB_SLR_INTEL_TXT		1
#define GRUB_SLR_AMD_SKINIT		2

/* Log formats */
#define GRUB_SLR_DRTM_TPM12_LOG		1
#define GRUB_SLR_DRTM_TPM20_LOG		2

/* DRTM Policy Entry Flags */
#define GRUB_SLR_POLICY_FLAG_MEASURED	0x1
#define GRUB_SLR_POLICY_IMPLICIT_SIZE	0x2

/* Array Lengths */
#define GRUB_TPM_EVENT_INFO_LENGTH	32
#define GRUB_TXT_VARIABLE_MTRRS_LENGTH	32

/* Tags */
#define GRUB_SLR_ENTRY_INVALID		0x0000
#define GRUB_SLR_ENTRY_DL_INFO		0x0001
#define GRUB_SLR_ENTRY_LOG_INFO		0x0002
#define GRUB_SLR_ENTRY_ENTRY_POLICY	0x0003
#define GRUB_SLR_ENTRY_INTEL_INFO	0x0004
#define GRUB_SLR_ENTRY_AMD_INFO		0x0005
#define GRUB_SLR_ENTRY_ARM_INFO		0x0006
#define GRUB_SLR_ENTRY_EFI_INFO		0x0007
#define GRUB_SLR_ENTRY_EFI_CONFIG	0x0008
#define GRUB_SLR_ENTRY_END		0xffff

/* Entity Types */
#define GRUB_SLR_ET_UNSPECIFIED		0x0000
#define GRUB_SLR_ET_SLTR		0x0001
#define GRUB_SLR_ET_BOOT_PARAMS		0x0002
#define GRUB_SLR_ET_SETUP_DATA		0x0003
#define GRUB_SLR_ET_CMDLINE		0x0004
#define GRUB_SLR_ET_EFI_MEMMAP		0x0005
#define GRUB_SLR_ET_INITRD		0x0006
#define GRUB_SLR_ET_TXT_OS2MLE		0x0010
#define GRUB_SLR_ET_UNUSED		0xffff

/*
 * Primary SLR Table Header
 */
struct grub_slr_table
{
  grub_uint32_t magic;
  grub_uint16_t revision;
  grub_uint16_t architecture;
  grub_uint32_t size;
  grub_uint32_t max_size;
  /* entries[] */
} GRUB_PACKED;

/*
 * Common SLRT Table Header
 */
struct grub_slr_entry_hdr
{
  grub_uint16_t tag;
  grub_uint16_t size;
} GRUB_PACKED;

/*
 * DRTM Dynamic Launch Configuration
 */
struct grub_slr_entry_dl_info
{
  struct grub_slr_entry_hdr hdr;
  grub_uint64_t dl_handler;
  grub_uint64_t dce_base;
  grub_uint32_t dce_size;
  grub_uint64_t dlme_entry;
} GRUB_PACKED;

/*
 * TPM Log Information
 */
struct grub_slr_entry_log_info
{
  struct grub_slr_entry_hdr hdr;
  grub_uint16_t format;
  grub_uint16_t reserved;
  grub_uint64_t addr;
  grub_uint32_t size;
} GRUB_PACKED;

/*
 * DRTM Measurement Policy
 */
struct grub_slr_entry_policy
{
  struct grub_slr_entry_hdr hdr;
  grub_uint16_t revision;
  grub_uint16_t nr_entries;
  /* policy_entries[] */
} GRUB_PACKED;

/*
 * DRTM Measurement Entry
 */
struct grub_slr_policy_entry
{
  grub_uint16_t pcr;
  grub_uint16_t entity_type;
  grub_uint16_t flags;
  grub_uint16_t reserved;
  grub_uint64_t entity;
  grub_uint64_t size;
  char evt_info[GRUB_TPM_EVENT_INFO_LENGTH];
} GRUB_PACKED;

/*
 * Secure Launch defined MTRR saving structures
 */
struct grub_txt_mtrr_pair
{
  grub_uint64_t mtrr_physbase;
  grub_uint64_t mtrr_physmask;
} GRUB_PACKED;

struct grub_txt_mtrr_state
{
  grub_uint64_t default_mem_type;
  grub_uint64_t mtrr_vcnt;
  struct grub_txt_mtrr_pair mtrr_pair[GRUB_TXT_VARIABLE_MTRRS_LENGTH];
} GRUB_PACKED;

/*
 * Intel TXT Info table
 */
struct grub_slr_entry_intel_info
{
  struct grub_slr_entry_hdr hdr;
  grub_uint64_t saved_misc_enable_msr;
  struct grub_txt_mtrr_state saved_bsp_mtrrs;
} GRUB_PACKED;

/*
 * AMD SKINIT Info table
 */
struct grub_slr_entry_amd_info
{
  struct grub_slr_entry_hdr hdr;
} GRUB_PACKED;

/*
 * ARM DRTM Info table
 */
struct grub_slr_entry_arm_info
{
  struct grub_slr_entry_hdr hdr;
} GRUB_PACKED;

struct grub_slr_entry_efi_config
{
  struct grub_slr_entry_hdr hdr;
  grub_uint32_t identifier;
  grub_uint16_t reserved;
  grub_uint16_t nr_entries;
  /* efi_cfg_entries[] */
} GRUB_PACKED;

struct grub_efi_cfg_entry
{
  grub_uint64_t cfg; /* address or value */
  grub_uint32_t size;
  char evt_info[GRUB_TPM_EVENT_INFO_LENGTH];
} GRUB_PACKED;

static inline void *
grub_slr_end_of_entrys (struct grub_slr_table *table)
{
  return (((void *)table) + table->size);
}

static inline struct grub_slr_entry_hdr *
grub_slr_next_entry (struct grub_slr_table *table,
                     struct grub_slr_entry_hdr *curr)
{
  struct grub_slr_entry_hdr *next = (struct grub_slr_entry_hdr *)
                                    ((u8 *)curr + curr->size);

  if ((void *)next >= grub_slr_end_of_entrys(table))
    return NULL;
  if (next->tag == GRUB_SLR_ENTRY_END)
    return NULL;

  return next;
}

static inline struct grub_slr_entry_hdr *
grub_slr_next_entry_by_tag (struct grub_slr_table *table,
                            struct grub_slr_entry_hdr *entry,
                            grub_uint16_t tag)
{
  if (!entry) /* Start from the beginning */
    entry = (struct grub_slr_entry_hdr *)(((u8 *)table) + sizeof(*table));

  for ( ; ; )
    {
      if (entry->tag == tag)
        return entry;

      entry = grub_slr_next_entry (table, entry);
      if (!entry)
        return NULL;
    }

  return NULL;
}

static inline int
grub_slr_add_entry (struct grub_slr_table *table,
                    struct grub_slr_entry_hdr *entry)
{
  struct grub_slr_entry_hdr *end;

  if ((table->size + entry->size) > table->max_size)
    return -1;

  grub_memcpy((u8 *)table + table->size, entry, entry->size);
  table->size += entry->size;

  return 0;
}

static inline void
grub_slr_init_table(struct slr_table *slrt, grub_uint16_t architecture,
                    grub_uint32_t max_size)
{
  slrt->magic = GRUB_SLR_TABLE_MAGIC;
  slrt->revision = GRUB_SLR_TABLE_REVISION;
  slrt->architecture = architecture;
  slrt->size = sizeof(*slrt);
  slrt->max_size = max_size;
}

#endif /* GRUB_SLR_TABLE_H */