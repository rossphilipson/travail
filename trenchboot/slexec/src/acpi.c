/*
 * acpi.c: ACPI utility fns
 *
 * Copyright (c) 2010, Intel Corporation
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
 */

#include <types.h>
#include <stdbool.h>
#include <slexec.h>
#include <stdarg.h>
#include <string.h>
#include <printk.h>
#include <loader.h>
#include <processor.h>
#include <misc.h>
#include <acpi.h>
#include <pci.h>
#include <cmdline.h>

#ifdef ACPI_DEBUG
#define acpi_printk         printk
#else
#define acpi_printk(...)    /* */
#endif

extern loader_ctx *g_ldr_ctx;

static struct acpi_rsdp *rsdp = NULL;

static inline struct acpi_xsdt *get_xsdt(void)
{
    if ( rsdp->rsdp_xsdt >= 0x100000000ULL ) {
        printk(SLEXEC_ERR"XSDT above 4GB\n");
        return NULL;
    }
    return (struct acpi_xsdt *)(uintptr_t)rsdp->rsdp_xsdt;
}

static bool verify_acpi_checksum(uint8_t *start, uint8_t len)
{
    uint8_t sum = 0;
    while ( len ) {
        sum += *start++;
        len--;
    }
    return (sum == 0);
}

static bool find_rsdp_in_range(void *start, void *end)
{
#define RSDP_BOUNDARY    16  /* rsdp ranges on 16-byte boundaries */
#define RSDP_CHKSUM_LEN  20  /* rsdp check sum length, defined in ACPI 1.0 */

    for ( ; start < end; start += RSDP_BOUNDARY ) {
        rsdp = (struct acpi_rsdp *)start;

        if ( sl_memcmp(rsdp->rsdp1.signature, RSDP_SIG,
                    sizeof(rsdp->rsdp1.signature)) == 0 ) {
            if ( verify_acpi_checksum((uint8_t *)rsdp, RSDP_CHKSUM_LEN) ) {
                printk(SLEXEC_DETA"RSDP (v%u, %.6s) @ %p\n", rsdp->rsdp1.revision,
                       rsdp->rsdp1.oemid, rsdp);
                return true;
            }
            else {
                printk(SLEXEC_ERR"checksum failed.\n");
                return false;
            }
        }
    }
    return false;
}

static bool find_rsdp(void)
{
    uint32_t length;
    uint8_t *ldr_rsdp = NULL;

    if ( rsdp != NULL )
        return true;

    /* for grins, let's try asking the loader_ctx if it has a copy */
    ldr_rsdp = get_loader_rsdp(g_ldr_ctx, &length);
    if (ldr_rsdp != NULL){
        rsdp = (struct acpi_rsdp *) ldr_rsdp;
        return true;
    }

    /*  0x00 - 0x400 */
    if ( find_rsdp_in_range(RSDP_SCOPE1_LOW, RSDP_SCOPE1_HIGH) )
        return true;

    /* 0xE0000 - 0x100000 */
    if ( find_rsdp_in_range(RSDP_SCOPE2_LOW, RSDP_SCOPE2_HIGH) )
        return true;

    printk(SLEXEC_ERR"can't find RSDP\n");
    rsdp = NULL;
    return false;
}

static struct acpi_rsdp
*get_rsdp_internal(loader_ctx *lctx)
{
    if (find_rsdp())
        return rsdp;

    /* so far we're striking out. Must have been an EFI lauch */
    if (!is_loader_launch_efi(lctx))
        return NULL;

    /* EFI launch, and the loader didn't grace us with an ACPI tag.
     * We can try to find this the hard way, right?
     */
    return NULL;
}

/* TODO fix this mess. The problem is in the MBI code */
/* Need to define efi_systable to get to config tables for this */
/* See uint8_t *efi_get_rsdp(void) in efi_tboot */
/* TODO rework all this, find RSDP using EFI system table too */
struct acpi_rsdp g_rsdp;
struct acpi_rsdp
*get_rsdp(loader_ctx *lctx)
{
    /* Only do this once and save a safe copy */
    /* !HACK! if grow_mb2_tag is called before saving a copy of the RSDP, */
    /* that call will corrupt the RSDP in the loader context (MBI) */
    if (rsdp == NULL) {
        if (get_rsdp_internal(lctx) == NULL)
            return NULL;
        sl_memcpy((void *)&g_rsdp, rsdp, sizeof(struct acpi_rsdp));
        rsdp = &g_rsdp;
    }

    return rsdp;
}

/* this function can find dmar table whether or not it was hidden */
static struct acpi_table_header *find_table(const char *table_name)
{
    struct acpi_table_header *table = NULL;
    struct acpi_xsdt *xsdt = NULL;
    struct acpi_rsdt *rsdt;

    if ( get_rsdp(g_ldr_ctx) == NULL ) {
        printk(SLEXEC_ERR"no rsdp to use\n");
        return NULL;
    }

    if ( rsdp->rsdp_xsdt < 0x100000000ULL )
        xsdt = (struct acpi_xsdt *)(uintptr_t)rsdp->rsdp_xsdt;
    else
        printk(SLEXEC_ERR"XSDT above 4GB\n");

    if ( rsdp->rsdp1.revision >= 2 && xsdt != NULL ) { /*  ACPI 2.0+ */
        uint64_t first_table = xsdt->table_offsets[0];
        for ( uint64_t *curr_table = (uint64_t *)(uintptr_t)first_table;
              curr_table < (uint64_t *)((void *)xsdt + xsdt->hdr.length);
              curr_table++ ) {
            table = (struct acpi_table_header *)(uintptr_t)*curr_table;
            if ( sl_memcmp(table->signature, table_name,
                           sizeof(table->signature)) == 0 )
                return table;
        }
    }
    else { /* ACPI 1.0 */
        rsdt = (struct acpi_rsdt *)rsdp->rsdp1.rsdt;
        uint32_t first_table = rsdt->table_offsets[0];

        if ( rsdt == NULL ) {
            printk(SLEXEC_ERR"rsdt is invalid.\n");
            return NULL;
        }

        for ( uint32_t *curr_table = (uint32_t *)(uintptr_t)first_table;
              curr_table < (uint32_t *)((void *)rsdt + rsdt->hdr.length);
              curr_table++ ) {
            table = (struct acpi_table_header *)(uintptr_t)*curr_table;
            if ( sl_memcmp(table->signature, table_name,
                           sizeof(table->signature)) == 0 )
                return table;
        }
    }

    printk(SLEXEC_ERR"can't find %s table.\n", table_name);
    return NULL;
}

bool vtd_bios_enabled(void)
{
    return !!(find_table(DMAR_SIG) != NULL);
}

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
