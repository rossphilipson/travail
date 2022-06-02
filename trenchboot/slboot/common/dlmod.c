/*
 * dlmod.c: test stuff for DL entry
 *
 * Copyright (c) 2003-2011, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <config.h>
#include <types.h>
#include <stdbool.h>
#include <page.h>
#include <printk.h>
#include <string.h>
#include <tb_error.h>
#include <cmdline.h>
#include <uuid.h>
#include <hash.h>
#include <mle.h>
#include <multiboot.h>
#include <loader.h>
#include <e820.h>
#include <linux_defns.h>
#include <txt/txt.h>
#include <txt/config_regs.h>
#include <txt/mtrrs.h>
#include <txt/heap.h>
#include <txt/acmod.h>
#include <txt/smx.h>
#include <slboot.h>

#define memcpy tb_memcpy
#include <slr_table.h>

#define DLMOD_MAGIC 0xffaa7711

typedef struct __packed {
    uint16_t entry;
    uint16_t bootloader_data;
    uint16_t dlmod_info;
    uint32_t magic;
} dlmod_hdr_t;

extern il_kernel_setup_t g_il_kernel_setup;

static dlmod_hdr_t *g_dlmod = NULL;
static uint32_t g_dlsize = 0;

static struct slr_table *g_table = NULL;

bool is_dlmod(const void *dlmod_base, uint32_t dlmod_size)
{
    dlmod_hdr_t *dlmod_hdr = (dlmod_hdr_t *)dlmod_base;

    if (dlmod_size <= sizeof(dlmod_hdr_t))
        return false;

    if (dlmod_hdr->magic != DLMOD_MAGIC) {
        printk(TBOOT_INFO"DLMOD magic: 0x%x\n", dlmod_hdr->magic);
        return false;
    }

    return true;
}

void set_dlmod(void *dlmod_base, uint32_t dlmod_size)
{
    g_dlmod = dlmod_base;
    g_dlsize = dlmod_size;
}

static void dl_get_entry_points(u32 *dl_entry, u32 *dlme_entry)
{
    uint32_t slaunch_header;
    struct kernel_info *ki;
    uint32_t *dl_ptr, *dlme_ptr;

    ki = (struct kernel_info*)(g_il_kernel_setup.protected_mode_base +
            g_il_kernel_setup.boot_params->hdr.slaunch_header);
    slaunch_header = ki->mle_header_offset;

    dl_ptr = (uint32_t*)(g_il_kernel_setup.protected_mode_base + slaunch_header);
    if (*(dl_ptr + 13) != 0) {
        printk("DL Entry Point: 0x%x\n", *(dl_ptr + 13));
        *dl_entry = *(dl_ptr + 13);
	*dl_entry += g_il_kernel_setup.protected_mode_base;
    }
    else {
        printk("No DL Entry Point, die!!\n");
        shutdown_system(get_error_shutdown());
    }

    dlme_ptr = (uint32_t*)(g_il_kernel_setup.protected_mode_base + slaunch_header);
    if (*(dlme_ptr + 6) != 0) {
        printk("DLME Entry Point: 0x%x\n", *(dlme_ptr + 6));
        *dlme_entry = *(dlme_ptr + 6);
	*dlme_entry += g_il_kernel_setup.protected_mode_base;
    }
    else {
        printk("No DLME Entry Point, die!!\n");
        shutdown_system(get_error_shutdown());
    }
}

static void dl_build_table(u32 dl_entry, u32 dlme_entry)
{
    struct slr_table *table;
    struct slr_entry_hdr *end;
    struct slr_entry_dl_info dl_info;

    table = (struct slr_table *)DLMOD_TABLE_ADDR;
    tb_memset(table, 0, DLMOD_TABLE_SIZE);
    table->magic = SLR_TABLE_MAGIC;
    table->revision = SLR_TABLE_REVISION;
    table->architecture = SLR_INTEL_TXT;
    table->size = sizeof(*table);
    table->max_size = DLMOD_TABLE_SIZE;

    end = (struct slr_entry_hdr *)((u8 *)table + table->size);
    end->tag = SLR_ENTRY_END;
    end->size = sizeof(*end);
    table->size += sizeof(*end);

    dl_info.hdr.tag = SLR_ENTRY_DL_INFO;
    dl_info.hdr.size = sizeof(dl_info);
    dl_info.dl_handler = dl_entry;
    dl_info.dce_base = (uint64_t)(uint32_t)g_sinit;
    dl_info.dce_size = g_sinit->size*4;
    dl_info.dlme_entry = dlme_entry;

    if (slr_add_entry(table, (struct slr_entry_hdr *)&dl_info)) {
        printk(TBOOT_ERR"Failed to add DL info to SLR\n");
        shutdown_system(get_error_shutdown());
    }

    g_table = (struct slr_table *)DLMOD_TABLE_ADDR;

    printk(TBOOT_INFO"Built SLR table @ %p\n", g_table);
}

void dl_launch(void)
{
    uint32_t dl_entry = 0, dlme_entry = 0, table, base, target;

    /* Get the entry points */
    dl_get_entry_points(&dl_entry, &dlme_entry);

    /* Build the DRTM table */
    dl_build_table(dl_entry, dlme_entry);

    table = (uint32_t)g_table;
    base = (uint32_t)g_dlmod;
    target = base + g_dlmod->entry;

    asm volatile("jmpl *%%ecx"
                 :
                 : "a" (base), "D" (table), "S" (dl_entry), "c" (target));
}
