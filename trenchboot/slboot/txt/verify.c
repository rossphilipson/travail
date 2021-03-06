/*
 * verify.c: verify that platform and processor supports Intel(r) TXT
 *
 * Copyright (c) 2003-2010, Intel Corporation
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
#include <msr.h>
#include <compiler.h>
#include <string.h>
#include <misc.h>
#include <processor.h>
#include <page.h>
#include <printk.h>
#include <uuid.h>
#include <loader.h>
#include <tb_error.h>
#include <e820.h>
#include <slboot.h>
#include <acpi.h>
#include <mle.h>
#include <hash.h>
#include <cmdline.h>
#include <txt/txt.h>
#include <txt/smx.h>
#include <txt/mtrrs.h>
#include <txt/config_regs.h>
#include <txt/heap.h>

extern long s3_flag;

/*
 * CPUID extended feature info
 */
static unsigned int g_cpuid_ext_feat_info;

/*
 * IA32_FEATURE_CONTROL_MSR
 */
static unsigned long g_feat_ctrl_msr;


static bool read_processor_info(void)
{
    unsigned long f1, f2;
     /* eax: regs[0], ebx: regs[1], ecx: regs[2], edx: regs[3] */
    uint32_t regs[4];

    /* is CPUID supported? */
    /* (it's supported if ID flag in EFLAGS can be set and cleared) */
    asm("pushf\n\t"
        "pushf\n\t"
        "pop %0\n\t"
        "mov %0,%1\n\t"
        "xor %2,%0\n\t"
        "push %0\n\t"
        "popf\n\t"
        "pushf\n\t"
        "pop %0\n\t"
        "popf\n\t"
        : "=&r" (f1), "=&r" (f2)
        : "ir" (X86_EFLAGS_ID));
    if ( ((f1^f2) & X86_EFLAGS_ID) == 0 ) {
        g_cpuid_ext_feat_info = 0;
        printk(TBOOT_ERR"CPUID instruction is not supported.\n");
        return false;
    }

    do_cpuid(0, regs);
    if ( regs[1] != 0x756e6547        /* "Genu" */
         || regs[2] != 0x6c65746e     /* "ntel" */
         || regs[3] != 0x49656e69 ) { /* "ineI" */
        g_cpuid_ext_feat_info = 0;
        printk(TBOOT_ERR"Non-Intel CPU detected.\n");
        return false;
    }
    g_cpuid_ext_feat_info = cpuid_ecx(1);

    /* read feature control msr only if processor supports VMX or SMX instructions */
    if ( (g_cpuid_ext_feat_info & CPUID_X86_FEATURE_VMX) ||
         (g_cpuid_ext_feat_info & CPUID_X86_FEATURE_SMX) ) {
        g_feat_ctrl_msr = rdmsr(MSR_IA32_FEATURE_CONTROL);
        printk(TBOOT_DETA"IA32_FEATURE_CONTROL_MSR: %08lx\n", g_feat_ctrl_msr);
    }

    return true;
}

static bool supports_smx(void)
{
    /* check that processor supports SMX instructions */
    if ( !(g_cpuid_ext_feat_info & CPUID_X86_FEATURE_SMX) ) {
        printk(TBOOT_ERR"ERR: CPU does not support SMX\n");
        return false;
    }
    printk(TBOOT_INFO"CPU is SMX-capable\n");

    /*
     * and that SMX is enabled in the feature control MSR
     */

    /* check that the MSR is locked -- BIOS should always lock it */
    if ( !(g_feat_ctrl_msr & IA32_FEATURE_CONTROL_MSR_LOCK) ) {
        printk(TBOOT_ERR"ERR: IA32_FEATURE_CONTROL_MSR_LOCK is not locked\n");
        /* this should not happen, as BIOS is required to lock the MSR */
#ifdef PERMISSIVE_BOOT
        /* we enable VMX outside of SMX as well so that if there was some */
        /* error in the TXT boot, VMX will continue to work */
        g_feat_ctrl_msr |= IA32_FEATURE_CONTROL_MSR_ENABLE_VMX_IN_SMX |
                           IA32_FEATURE_CONTROL_MSR_ENABLE_VMX_OUT_SMX |
                           IA32_FEATURE_CONTROL_MSR_ENABLE_SENTER |
                           IA32_FEATURE_CONTROL_MSR_SENTER_PARAM_CTL |
                           IA32_FEATURE_CONTROL_MSR_LOCK;
        wrmsrl(MSR_IA32_FEATURE_CONTROL, g_feat_ctrl_msr);
        return true;
#else
        return false;
#endif
    }

    /* check that SENTER (w/ full params) is enabled */
    if ( !(g_feat_ctrl_msr & (IA32_FEATURE_CONTROL_MSR_ENABLE_SENTER |
                              IA32_FEATURE_CONTROL_MSR_SENTER_PARAM_CTL)) ) {
        printk(TBOOT_ERR"ERR: SENTER disabled by feature control MSR (%lx)\n",
               g_feat_ctrl_msr);
        return false;
    }

    return true;
}

tb_error_t supports_txt(void)
{
    capabilities_t cap;

    /* processor must support cpuid and must be Intel CPU */
    if ( !read_processor_info() )
        return TB_ERR_SMX_NOT_SUPPORTED;

    /* processor must support SMX */
    if ( !supports_smx() )
        return TB_ERR_SMX_NOT_SUPPORTED;

    /* testing for chipset support requires enabling SMX on the processor */
    write_cr4(read_cr4() | CR4_SMXE);
    printk(TBOOT_INFO"SMX is enabled\n");

    /*
     * verify that an TXT-capable chipset is present and
     * check that all needed SMX capabilities are supported
     */

    cap = __getsec_capabilities(0);
    if ( cap.chipset_present ) {
        if ( cap.senter && cap.sexit && cap.parameters && cap.smctrl &&
             cap.wakeup ) {
            printk(TBOOT_INFO"TXT chipset and all needed capabilities present\n");
            return TB_ERR_NONE;
        }
        else
            printk(TBOOT_ERR"ERR: insufficient SMX capabilities (%x)\n", cap._raw);
    }
    else
        printk(TBOOT_ERR"ERR: TXT-capable chipset not present\n");

    /* since we are failing, we should clear the SMX flag */
    write_cr4(read_cr4() & ~CR4_SMXE);

    return TB_ERR_TXT_NOT_SUPPORTED;
}

void set_vtd_pmrs(os_sinit_data_t *os_sinit_data,
                  uint64_t min_lo_ram, uint64_t max_lo_ram,
                  uint64_t min_hi_ram, uint64_t max_hi_ram)
{
    printk(TBOOT_DETA"min_lo_ram: 0x%Lx, max_lo_ram: 0x%Lx\n", min_lo_ram, max_lo_ram);
    printk(TBOOT_DETA"min_hi_ram: 0x%Lx, max_hi_ram: 0x%Lx\n", min_hi_ram, max_hi_ram);

    /*
     * base must be 2M-aligned and size must be multiple of 2M
     * (so round bases and sizes down--rounding size up might conflict
     *  with a BIOS-reserved region and cause problems; in practice, rounding
     *  base down doesn't)
     * we want to protect all of usable mem so that any kernel allocations
     * before VT-d remapping is enabled are protected
     */

    min_lo_ram &= ~0x1fffffULL;
    uint64_t lo_size = (max_lo_ram - min_lo_ram) & ~0x1fffffULL;
    os_sinit_data->vtd_pmr_lo_base = min_lo_ram;
    os_sinit_data->vtd_pmr_lo_size = lo_size;

    min_hi_ram &= ~0x1fffffULL;
    uint64_t hi_size = (max_hi_ram - min_hi_ram) & ~0x1fffffULL;
    os_sinit_data->vtd_pmr_hi_base = min_hi_ram;
    os_sinit_data->vtd_pmr_hi_size = hi_size;
}

tb_error_t txt_verify_platform(void)
{
    txt_heap_t *txt_heap;
    tb_error_t err;

    /* check TXT supported */
    err = supports_txt();
    if ( err != TB_ERR_NONE )
        return err;

    if ( !vtd_bios_enabled() ) {
        return TB_ERR_VTD_NOT_SUPPORTED;
    }

    /* check is TXT_RESET.STS is set, since if it is SENTER will fail */
    txt_ests_t ests = (txt_ests_t)read_pub_config_reg(TXTCR_ESTS);
    if ( ests.txt_reset_sts ) {
        printk(TBOOT_ERR"TXT_RESET.STS is set and SENTER is disabled (0x%02Lx)\n",
               ests._raw);
        return TB_ERR_SMX_NOT_SUPPORTED;
    }

    /* verify BIOS to OS data */
    txt_heap = get_txt_heap();
    if ( !verify_bios_data(txt_heap) )
        return TB_ERR_TXT_NOT_SUPPORTED;

    return TB_ERR_NONE;
}

/*
 * Local variables:
 * mode: C
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
