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

#define DRTM_TABLE_HEADER       0x0001	/* Always first */
#define DRTM_ENTRY_END          0xffff
#define DRTM_NO_SUBTYPE         0x0000
#define DRTM_ENTRY_ARCHITECTURE 0x0008
#define DRTM_ENTRY_DCE_INFO     0x0009
#define DRTM_DCE_TXT_ACM 1
#define DRTM_DCE_AMD_SLB 2
#define DRTM_INTEL_TXT   1
#define DRTM_AMD_SKINIT  2

struct drtm_entry_hdr {
    uint16_t type;
    uint16_t subtype;
    uint16_t size;
} __packed;

struct drtm_table_header {
    struct drtm_entry_hdr hdr;
    uint16_t size;
} __packed;

struct drtm_entry_architecture {
    struct drtm_entry_hdr hdr;
    uint16_t architecture;
} __packed;

struct drtm_entry_dce_info {
    struct drtm_entry_hdr hdr;
    uint64_t dce_base;
    uint32_t dce_size;
} __packed;

#define DLMOD_MAGIC 0xffaa7711

typedef struct __packed {
    uint16_t entry;
    uint16_t bootloader_data;
    uint16_t dlmod_info;
    uint32_t magic;
} dlmod_hdr_t;

extern il_kernel_setup_t g_il_kernel_setup;

static uint32_t g_slaunch_header;

static dlmod_hdr_t *g_dlmod = NULL;
static uint32_t g_dlsize = 0;

static struct drtm_table_header *g_table = NULL;

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

static void dl_build_table(void)
{
    struct drtm_table_header *table;
    struct drtm_entry_architecture *arch;
    struct drtm_entry_dce_info *dce_info;
    struct drtm_entry_hdr *end;

    table = (struct drtm_table_header *)DLMOD_TABLE_ADDR;
    tb_memset(table, 0, DLMOD_TABLE_SIZE);
    table->hdr.type = DRTM_TABLE_HEADER;
    table->hdr.subtype = DRTM_NO_SUBTYPE;
    table->hdr.size = sizeof(struct drtm_table_header);
    table->size = (uint16_t)DLMOD_TABLE_SIZE;

    arch = (struct drtm_entry_architecture *)(++table);
    arch->hdr.type = DRTM_ENTRY_ARCHITECTURE;
    arch->hdr.subtype = DRTM_NO_SUBTYPE;
    arch->hdr.size = sizeof(struct drtm_entry_architecture);
    arch->architecture = DRTM_INTEL_TXT;

    dce_info = (struct drtm_entry_dce_info *)(++arch);
    dce_info->hdr.type = DRTM_ENTRY_DCE_INFO;
    dce_info->hdr.subtype = DRTM_DCE_TXT_ACM;
    dce_info->hdr.size = sizeof(struct drtm_entry_dce_info);
    dce_info->dce_base = (uint64_t)(uint32_t)g_sinit;
    dce_info->dce_size = g_sinit->size*4;

    end = (struct drtm_entry_hdr *)(++dce_info);
    end->type = DRTM_ENTRY_END;
    end->subtype = DRTM_NO_SUBTYPE;
    end->size = sizeof(struct drtm_entry_hdr);

    g_table = (struct drtm_table_header *)DLMOD_TABLE_ADDR;

    printk(TBOOT_INFO"Built DRTM table @ %p\n", g_table);
}

void dl_launch(void)
{
    struct kernel_info *ki;
    uint32_t *dl_ptr;
    uint32_t dl_entry = 0, table, base, target;

    /* Build the DRTM table */
    dl_build_table();

    ki = (struct kernel_info*)(g_il_kernel_setup.protected_mode_base +
            g_il_kernel_setup.boot_params->hdr.slaunch_header);
    g_slaunch_header = ki->mle_header_offset;

    dl_ptr = (uint32_t*)(g_il_kernel_setup.protected_mode_base + g_slaunch_header);
    if (*(dl_ptr + 13) != 0) {
        printk("DL Entry Point: 0x%x\n", *(dl_ptr + 13));
        dl_entry = *(dl_ptr + 9);
    }
    else {
        printk("No DL Entry Point, die!!\n");
        shutdown_system(get_error_shutdown());
    }

    table = (uint32_t)g_table;
    base = (uint32_t)g_dlmod;
    target = base + g_dlmod->entry;

    asm volatile("jmpl *%%ecx"
                 :
                 : "a" (base), "D" (table), "S" (dl_entry), "c" (target));
}
