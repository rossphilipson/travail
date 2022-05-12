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

#ifndef IS_INCLUDED     /*  defined in utils/acminfo.c  */
#include <config.h>
#include <types.h>
#include <stdbool.h>
#include <printk.h>
#include <string.h>
#include <cmdline.h>
#include <multiboot.h>
#include <loader.h>
#include <e820.h>
#include <linux_defns.h>
#include <slboot.h>
#endif    /* IS_INCLUDED */

#define DLMOD_MAGIC 0xffaa7711

typedef struct {
    uint16_t entry;
    uint16_t bootloader_data;
    uint16_t dlmod_info;
    uint32_t magic;
} dlmod_hdr_t;

extern il_kernel_setup_t g_il_kernel_setup;

static uint32_t g_slaunch_header;

static dlmod_hdr_t *g_dlmod = NULL;

bool is_dlmod(const void *dlmod_base, uint32_t dlmod_size)
{
    dlmod_hdr_t *dlmod_hdr = (dlmod_hdr_t *)dlmod_base;

    if (dlmod_size <= sizeof(dlmod_hdr_t))
        return false;

    if (dlmod_hdr->magic != DLMOD_MAGIC)
        return false;

    g_dlmod = dlmod_hdr;

    return true;
}

void dl_build_table(void)
{
}

void dl_launch(void)
{
    struct kernel_info *ki;
    uint32_t *dl_ptr;
    uint32_t dl_entry;

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

    dl_entry = dl_entry;
}
