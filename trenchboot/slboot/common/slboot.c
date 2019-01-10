/*
 * slboot.c: main entry point and pre-launch code for Trenchboot
 *
 * Used to be:
 * tboot.c: main entry point and "generic" routines for measured launch
 *          support
 *
 * Copyright (c) 2006-2010, Intel Corporation
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
#include <stdarg.h>
#include <compiler.h>
#include <string.h>
#include <printk.h>
#include <uuid.h>
#include <loader.h>
#include <processor.h>
#include <misc.h>
#include <page.h>
#include <msr.h>
#include <atomic.h>
#include <io.h>
#include <e820.h>
#include <uuid.h>
#include <loader.h>
#include <hash.h>
#include <mle.h>
#include <tpm.h>
#include <slboot.h>
#include <txt/txt.h>
#include <txt/smx.h>
#include <txt/mtrrs.h>
#include <txt/config_regs.h>
#include <txt/heap.h>
#include <txt/verify.h>
#include <acpi.h>
#include <cmdline.h>
#include <tpm_20.h>

extern void error_action(tb_error_t error);
extern void verify_IA32_se_svn_status(const acm_hdr_t *acm_hdr);

/* loader context struct saved so that post_launch() can use it */
__data loader_ctx g_loader_ctx = { NULL, 0 };
__data loader_ctx *g_ldr_ctx = &g_loader_ctx;

static uint32_t g_default_error_action = TB_SHUTDOWN_HALT;

void error_action(tb_error_t error)
{
    if ( error == TB_ERR_NONE )
        return;

    printk(TBOOT_ERR"error action invoked for: %x\n", error);
    shutdown_system(g_default_error_action);
}

unsigned long get_tboot_mem_end(void)
{
    return PAGE_UP((unsigned long)&_end);
}

static tb_error_t verify_platform(void)
{
    return txt_verify_platform();
}

static bool prepare_cpu(void)
{
    return txt_prepare_cpu();
}

#define ICR_LOW 0x300

static void startup_rlps(void)
{
    uint32_t rlp_count = ((cpuid_ecx(1) >> 16) & 0xff) - 1;
    uint32_t apicbase = (uint32_t)rdmsr(MSR_APICBASE) & 0xfffffffffffff000;

    if ( rlp_count == 0 )
        return;

    /* send init ipi to all rlp -- Dest Shorthand: 11, Delivery Mode: 101 */
    writel(apicbase + ICR_LOW, 0xc0500);
}

static void launch_racm(void)
{
    tb_error_t err;

    /* bsp check & tpm check done by caller */
    /* SMX must be supported */
    if ( !(cpuid_ecx(1) & CPUID_X86_FEATURE_SMX) )
        error_action(TB_ERR_SMX_NOT_SUPPORTED);

    /* Enable SMX */
    write_cr4(read_cr4() | CR4_SMXE);

    /* prepare cpu */
    if ( !prepare_cpu() )
        error_action(TB_ERR_FATAL);

    /* prepare tpm */
    if ( !prepare_tpm() )
        error_action(TB_ERR_TPM_NOT_READY);

    /* Place RLPs in Wait for SIPI state */
    startup_rlps();

    /* Verify loader context */
    if ( !verify_loader_context(g_ldr_ctx) )
        error_action(TB_ERR_FATAL);

    /* load racm */
    err = txt_launch_racm(g_ldr_ctx);
    error_action(err);
}

void check_racm_result(void)
{
    txt_get_racm_error();
    shutdown_system(TB_SHUTDOWN_HALT);
}

void begin_launch(void *addr, uint32_t magic)
{
    tb_error_t err;

    g_default_error_action = get_error_shutdown();

    if (g_ldr_ctx->type == 0)
        determine_loader_type(addr, magic);

    /* on pre-SENTER boot, copy command line to buffer in tboot image
       (so that it will be measured); buffer must be 0 -filled */
    {

        const char *cmdline_orig = get_cmdline(g_ldr_ctx);
        const char *cmdline = NULL;
        if (cmdline_orig){
           // cmdline = skip_filename(cmdline_orig);
            cmdline = cmdline_orig;
        }
        tb_memset(g_cmdline, '\0', sizeof(g_cmdline));
        if (cmdline)
            tb_strncpy(g_cmdline, cmdline, sizeof(g_cmdline)-1);
    }

    /* always parse cmdline */
    tboot_parse_cmdline();

    /* initialize all logging targets */
    printk_init();

    printk(TBOOT_INFO"******************* TBOOT *******************\n");
    printk(TBOOT_INFO"   %s\n", TBOOT_CHANGESET);
    printk(TBOOT_INFO"*********************************************\n");

    printk(TBOOT_INFO"command line: %s\n", g_cmdline);
    /* if telled to check revocation acm result, go with simplified path */
    if ( get_tboot_call_racm_check() )
        check_racm_result(); /* never return */

    /* RLM scaffolding
       if (g_ldr_ctx->type == 2)
       print_loader_ctx(g_ldr_ctx);
    */

    /* we should only be executing on the BSP */
    if ( !(rdmsr(MSR_APICBASE) & APICBASE_BSP) ) {
        printk(TBOOT_INFO"entry processor is not BSP\n");
        error_action(TB_ERR_FATAL);
    }
    printk(TBOOT_INFO"BSP is cpu %u\n", get_apicid());

    /* make copy of e820 map that we will use and adjust */
    if ( !copy_e820_map(g_ldr_ctx) )  error_action(TB_ERR_FATAL);

    /* we need to make sure this is a (TXT-) capable platform before using */
    /* any of the features, incl. those required to check if the environment */
    /* has already been launched */

    if (g_sinit == NULL) {
       find_platform_sinit_module(g_ldr_ctx, (void **)&g_sinit, NULL);
       /* check if it is newer than BIOS provided version, then copy it to BIOS reserved region */
       g_sinit = copy_sinit(g_sinit);
       if (g_sinit == NULL)
           error_action(TB_ERR_SINIT_NOT_PRESENT);
       if (!verify_acmod(g_sinit))
           error_action(TB_ERR_ACMOD_VERIFY_FAILED);
    }

    /* make TPM ready for measured launch */
    if (!tpm_detect())
       error_action(TB_ERR_TPM_NOT_READY);

    /* verify SE enablement status */
    verify_IA32_se_svn_status(g_sinit);

    /* if telled to call revocation acm, go with simplified path */
    if ( get_tboot_call_racm() )
        launch_racm(); /* never return */

    /* need to verify that platform supports TXT before we can check error */
    /* (this includes TPM support) */
    err = supports_txt();
    error_action(err);

    /* print any errors on last boot, which must be from TXT launch */
    txt_display_errors();
    if (txt_has_error() && get_tboot_ignore_prev_err() == false) {
        error_action(TB_ERR_PREV_TXT_ERROR);
    }

    /* need to verify that platform can perform measured launch */
    err = verify_platform();
    error_action(err);

    /* ensure there are modules */
    if ( !verify_loader_context(g_ldr_ctx) )
        error_action(TB_ERR_FATAL);

    /* make the CPU ready for measured launch */
    if ( !prepare_cpu() )
        error_action(TB_ERR_FATAL);

    /* check for error from previous boot */
    printk(TBOOT_INFO"checking previous errors on the last boot.\n\t");
    if ( was_last_boot_error() )
        printk(TBOOT_INFO"last boot has error.\n");
    else
        printk(TBOOT_INFO"last boot has no error.\n");

    if ( !prepare_tpm() )
        error_action(TB_ERR_TPM_NOT_READY);

    /* launch the measured environment */
    err = txt_launch_environment(g_ldr_ctx);
    error_action(err);
}

void shutdown_system(uint32_t shutdown_type)
{
    static const char *types[] = { "TB_SHUTDOWN_REBOOT", "TB_SHUTDOWN_HALT" };
    char type[32];

    if ( shutdown_type >= ARRAY_SIZE(types) )
        tb_snprintf(type, sizeof(type), "unknown: %u", shutdown_type);
    else {
        tb_strncpy(type, types[shutdown_type], sizeof(type));
        type[sizeof(type) - 1] = '\0';
    }
    printk(TBOOT_INFO"shutdown_system() called for shutdown_type: %s\n", type);

    switch( shutdown_type ) {
        case TB_SHUTDOWN_REBOOT:
            if ( txt_is_powercycle_required() ) {
                /* powercycle by writing 0x0a+0x0e to port 0xcf9 */
                /* (supported by all TXT-capable chipsets) */
                outb(0xcf9, 0x0a);
                outb(0xcf9, 0x0e);
            }
            else {
                /* soft reset by writing 0xfe to keyboard reset vector 0x64 */
                /* BIOSes (that are not performing some special operation, */
                /* such as update) will turn this into a platform reset as */
                /* expected. */
                outb(0x64, 0xfe);
                /* fall back to soft reset by writing 0x06 to port 0xcf9 */
                /* (supported by all TXT-capable chipsets) */
                outb(0xcf9, 0x06);
            }

        /* FALLTHROUGH */
        case TB_SHUTDOWN_HALT:
        default:
            while ( true )
                halt();
    }
}

void handle_exception(void)
{
    printk(TBOOT_INFO"received exception; shutting down...\n");
    shutdown_system(TB_SHUTDOWN_HALT);
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
