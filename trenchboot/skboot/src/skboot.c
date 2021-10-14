/*
 * skboot.c: main entry point and pre-launch code for Trenchboot
 *
 * Used to be:
 * skboot.c: main entry point and "generic" routines for measured launch
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

#include <types.h>
#include <stdbool.h>
#include <skboot.h>
#include <stdarg.h>
#include <string.h>
#include <printk.h>
#include <loader.h>
#include <processor.h>
#include <misc.h>
#include <e820.h>
#include <linux.h>
#include <tpm.h>
#include <cmdline.h>
#include <skl.h>

/* loader context struct saved so that post_launch() can use it */
__data loader_ctx g_loader_ctx = { NULL, 0 };
__data loader_ctx *g_ldr_ctx = &g_loader_ctx;

sl_header_t *g_skl_module = NULL;
uint32_t g_skl_size = 0;

static uint32_t g_default_error_action = SK_SHUTDOWN_HALT;
static unsigned int g_cpuid_ext_feat_info;
static bool is_powercycle_required = true;
static uint32_t apic_base;

static void shutdown_system(uint32_t shutdown_type)
{
    static const char *types[] = { "SK_SHUTDOWN_REBOOT", "SK_SHUTDOWN_SHUTDOWN",
                                   "SK_SHUTDOWN_HALT" };
    char type[32];

    /* NOTE: the TPM close and open current locality is not needed here since */
    /* since that only makes sense if this is called post laucnh which is */
    /* the case in SKBOOT */

    if ( shutdown_type >= ARRAY_SIZE(types) )
        sk_snprintf(type, sizeof(type), "unknown: %u", shutdown_type);
    else {
        sk_strncpy(type, types[shutdown_type], sizeof(type));
        type[sizeof(type) - 1] = '\0';
    }
    printk(SKBOOT_INFO"shutdown_system() called for shutdown_type: %s\n", type);

    switch( shutdown_type ) {
        case SK_SHUTDOWN_REBOOT:
            if ( is_powercycle_required ) {
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
            break;
        case SK_SHUTDOWN_SHUTDOWN:
            /* TODO implement S5 */
            break;
        /* FALLTHROUGH */
        case SK_SHUTDOWN_HALT:
        default:
            while ( true )
                halt();
    }
}

unsigned long get_skboot_mem_end(void)
{
    return PAGE_UP((unsigned long)&_end);
}

static bool prepare_cpu(void)
{
    unsigned long eflags, cr0;
    uint64_t mcg_cap, mcg_stat;

    /* must be running at CPL 0 => this is implicit in even getting this far */
    /* since our bootstrap code loads a GDT, etc. */

    cr0 = read_cr0();

    /* must be in protected mode */
    if ( !(cr0 & CR0_PE) ) {
        printk(SKBOOT_ERR"ERR: not in protected mode\n");
        return false;
    }

    /* cache must be enabled (CR0.CD = CR0.NW = 0) */
    if ( cr0 & CR0_CD ) {
        printk(SKBOOT_INFO"CR0.CD set\n");
        cr0 &= ~CR0_CD;
    }
    if ( cr0 & CR0_NW ) {
        printk(SKBOOT_INFO"CR0.NW set\n");
        cr0 &= ~CR0_NW;
    }

    /* native FPU error reporting must be enabled for proper */
    /* interaction behavior */
    if ( !(cr0 & CR0_NE) ) {
        printk(SKBOOT_INFO"CR0.NE not set\n");
        cr0 |= CR0_NE;
    }

    write_cr0(cr0);

    /* cannot be in virtual-8086 mode (EFLAGS.VM=1) */
    eflags = read_eflags();
    if ( eflags & X86_EFLAGS_VM ) {
        printk(SKBOOT_INFO"EFLAGS.VM set\n");
        write_eflags(eflags | ~X86_EFLAGS_VM);
    }

    printk(SKBOOT_INFO"CR0 and EFLAGS OK\n");

    /*
     * verify all machine check status registers are clear (unless
     * support preserving them)
     */

    /* no machine check in progress (IA32_MCG_STATUS.MCIP=1) */
    mcg_stat = rdmsr(MSR_MCG_STATUS);
    if ( mcg_stat & 0x04 ) {
        printk(SKBOOT_ERR"machine check in progress\n");
        return false;
    }

    /* check if all machine check regs are clear */
    mcg_cap = rdmsr(MSR_MCG_CAP);
    for ( unsigned int i = 0; i < (mcg_cap & 0xff); i++ ) {
        mcg_stat = rdmsr(MSR_MC0_STATUS + 4*i);
        if ( mcg_stat & (1ULL << 63) ) {
            printk(SKBOOT_ERR"MCG[%u] = %Lx ERROR\n", i, mcg_stat);
            return false;
        }
    }

    printk(SKBOOT_INFO"Machine Check OK\n");

    return true;
}

static bool platform_architecture(void)
{
    uint32_t regs[4];

    do_cpuid(0, regs);

    if ( regs[1] == 0x68747541 /* "Auth" */
         && regs[2] == 0x444d4163   /* "cAMD" */
         && regs[3] == 0x69746e65 ) /* "enti" */
        return true;

    printk(SKBOOT_ERR"Error: platform is neither Intel or AMD\n");
    return false;
}

static int supports_skinit(void)
{
    g_cpuid_ext_feat_info = cpuid_ecx(0x80000001);

    if (g_cpuid_ext_feat_info & CPUID_X86_FEATURE_SKINIT) {
        printk(SKBOOT_INFO"SKINIT CPU and all needed capabilities present\n");
        return SK_ERR_NONE;
    }
    return SK_ERR_NO_SKINIT;
}

void error_action(int error)
{
    if ( error == SK_ERR_NONE )
        return;

    printk(SKBOOT_ERR"error action invoked for: %x\n", error);
    shutdown_system(g_default_error_action);
}

extern void debug_put_chars(void);

/* Broadcast INIT to all APs except self */
static void send_init_ipi_shorthand(void)
{
    uint32_t *icr_reg;

    /* accessing the ICR depends on the APIC mode */
    if (apic_base & X2APIC_ENABLE) {
        mb();

        /* access ICR through MSR */
        wrmsr(MSR_X2APIC_ICR, (ICR_DELIVER_EXCL_SELF|ICR_MODE_INIT));
        printk(SKBOOT_INFO"SKINIT assert #INIT on APs - x2APIC MSR reg: 0x%x\n", MSR_X2APIC_ICR);
    } else {
        /* mask off low order bits to get base address */
        apic_base &= APICBASE_BASE_MASK;
        /* access ICR through MMIO */
        icr_reg = (uint32_t *)(apic_base + LAPIC_ICR_LO);

        writel(icr_reg, (ICR_DELIVER_EXCL_SELF|ICR_MODE_INIT));
        printk(SKBOOT_INFO"SKINIT assert #INIT on APs - xAPIC ICR reg: %p\n", icr_reg);
    }

    printk(SKBOOT_INFO"Wait for IPI delivery\n");
    delay(1000);
}

static void skinit_launch_environment(void)
{
    uint32_t slb = (uint32_t)g_skl_module;

    send_init_ipi_shorthand();

    disable_intr();

    printk(SKBOOT_INFO"SKINIT launch SKL - slb: 0x%x\n", slb);
    asm volatile ("movl %0, %%eax\n"
	          "skinit\n"
                  : : "r" (slb));

    printk(SKBOOT_INFO"SKINIT failed\n");
}

void begin_launch(void *addr, uint32_t magic)
{
    const char *cmdline;
    int err;

    /* this is the SKBOOT module loader type, either MB1 or MB2 */
    determine_loader_type(addr, magic);

    cmdline = get_cmdline(g_ldr_ctx);
    sk_memset(g_cmdline, '\0', sizeof(g_cmdline));
    if ( cmdline )
        sk_strncpy(g_cmdline, cmdline, sizeof(g_cmdline)-1);

    /* always parse cmdline */
    skboot_parse_cmdline();

    g_default_error_action = get_error_shutdown();

    /* initialize all logging targets */
    printk_init();

    printk(SKBOOT_INFO"******************* SKBOOT *******************\n");
    printk(SKBOOT_INFO"   %s -- @ %p\n", SKBOOT_CHANGESET, _start);
    printk(SKBOOT_INFO"*********************************************\n");

    printk(SKBOOT_INFO"command line: %s\n", g_cmdline);

    if ( !platform_architecture() )
        error_action(SK_ERR_FATAL);

    /* we should only be executing on the BSP */
    apic_base = (uint32_t)rdmsr(MSR_APICBASE);
    if ( !(apic_base & APICBASE_BSP) ) {
        printk(SKBOOT_INFO"entry processor is not BSP\n");
        error_action(SK_ERR_FATAL);
    }
    printk(SKBOOT_INFO"BSP is cpu %u APIC base MSR: 0x%x\n", get_apicid(), apic_base);

    /* make copy of e820 map that we will use and adjust */
    if ( !copy_e820_map(g_ldr_ctx) )
        error_action(SK_ERR_FATAL);

    /* we need to make sure this is a (SKINIT) capable platform before using */
    /* any of the features, incl. those required to check if the environment */
    /* has already been launched */
    err = supports_skinit();
    error_action(err);

    /* make TPM ready for measured launch */
    if ( !tpm_detect() )
       error_action(SK_ERR_TPM_NOT_READY);

    /* ensure there are modules */
    if ( !verify_loader_context(g_ldr_ctx) )
        error_action(SK_ERR_FATAL);

    /* make the CPU ready for secure launch */
    if ( !prepare_cpu() )
        error_action(SK_ERR_FATAL);

    if ( !prepare_tpm() )
        error_action(SK_ERR_TPM_NOT_READY);

    /* locate and load SKL module */
    if ( !find_skl_module(g_ldr_ctx) )
        error_action(SK_ERR_NO_SKL);

    relocate_skl_module();
    print_skl_module();

    /* locate and prepare the secure launch kernel */
    if ( !prepare_intermediate_loader() )
        error_action(SK_ERR_FATAL);

    /* prepare the bootloader data area in the SKL */
    if ( !prepare_skl_bootloader_data() )
        error_action(SK_ERR_FATAL);

    /* launch the secure environment */
    skinit_launch_environment();
    /* No return */
}

void handle_exception(void)
{
    printk(SKBOOT_INFO"Received exception; shutting down...\n");
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
