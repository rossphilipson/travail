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
#include <e820.h>
#include <tpm.h>
#include <cmdline.h>
#include <txt/smx.h>
#include <txt/mle.h>
#include <txt/txt.h>
#include <txt/mtrrs.h>
#include <txt/heap.h>

extern long s3_flag;

/*
 * CPUID feature info
 */
static unsigned int g_cpuid_feat_info = 0;

/*
 * IA32_FEATURE_CONTROL_MSR
 */
static unsigned long g_feat_ctrl_msr;


static bool read_processor_info(void)
{
    /* TODO CPUID supported and mfg checks moved to platform function */

    g_cpuid_feat_info = cpuid_ecx(CPUID_X86_FEATURE_INFO_LEAF);

    /* read feature control msr only if processor supports VMX or SMX instructions */
    if ( (g_cpuid_feat_info & CPUID_X86_FEATURE_VMX) ||
         (g_cpuid_feat_info & CPUID_X86_FEATURE_SMX) ) {
        g_feat_ctrl_msr = rdmsr(MSR_IA32_FEATURE_CONTROL);
        printk(SLEXEC_DETA"IA32_FEATURE_CONTROL_MSR: %08lx\n", g_feat_ctrl_msr);
    }

    return true;
}

static bool supports_smx(void)
{
    /* check that processor supports SMX instructions */
    if ( !(g_cpuid_feat_info & CPUID_X86_FEATURE_SMX) ) {
        printk(SLEXEC_ERR"ERR: CPU does not support SMX\n");
        return false;
    }
    printk(SLEXEC_INFO"CPU is SMX-capable\n");

    /*
     * and that SMX is enabled in the feature control MSR
     */

    /* check that the MSR is locked -- BIOS should always lock it */
    if ( !(g_feat_ctrl_msr & FEATURE_CONTROL_LOCK) ) {
        printk(SLEXEC_ERR"ERR: FEATURE_CONTROL_LOCK is not locked\n");
        /* this should not happen, as BIOS is required to lock the MSR */
#ifdef PERMISSIVE_BOOT
        /* we enable VMX outside of SMX as well so that if there was some */
        /* error in the TXT boot, VMX will continue to work */
        g_feat_ctrl_msr |= FEATURE_CONTROL_ENABLE_VMX_IN_SMX |
                           FEATURE_CONTROL_ENABLE_VMX_OUT_SMX |
                           FEATURE_CONTROL_ENABLE_SENTER |
                           FEATURE_CONTROL_SENTER_PARAM_CTL |
                           FEATURE_CONTROL_LOCK;
        wrmsrl(MSR_IA32_FEATURE_CONTROL, g_feat_ctrl_msr);
        return true;
#else
        return false;
#endif
    }

    /* check that SENTER (w/ full params) is enabled */
    if ( !(g_feat_ctrl_msr & (FEATURE_CONTROL_ENABLE_SENTER |
                              FEATURE_CONTROL_SENTER_PARAM_CTL)) ) {
        printk(SLEXEC_ERR"ERR: SENTER disabled by feature control MSR (%lx)\n",
               g_feat_ctrl_msr);
        return false;
    }

    return true;
}

int supports_txt(void)
{
    capabilities_t cap;

    /* processor must support cpuid and must be Intel CPU */
    if ( !read_processor_info() )
        return SL_ERR_SMX_NOT_SUPPORTED;

    /* processor must support SMX */
    if ( !supports_smx() )
        return SL_ERR_SMX_NOT_SUPPORTED;

    /* testing for chipset support requires enabling SMX on the processor */
    write_cr4(read_cr4() | CR4_SMXE);
    printk(SLEXEC_INFO"SMX is enabled\n");

    /*
     * verify that an TXT-capable chipset is present and
     * check that all needed SMX capabilities are supported
     */

    cap = __getsec_capabilities(0);
    if ( cap.chipset_present ) {
        if ( cap.senter && cap.sexit && cap.parameters && cap.smctrl &&
             cap.wakeup ) {
            printk(SLEXEC_INFO"TXT chipset and all needed capabilities present\n");
            return SL_ERR_NONE;
        }
        else
            printk(SLEXEC_ERR"ERR: insufficient SMX capabilities (%x)\n", cap._raw);
    }
    else
        printk(SLEXEC_ERR"ERR: TXT-capable chipset not present\n");

    /* since we are failing, we should clear the SMX flag */
    write_cr4(read_cr4() & ~CR4_SMXE);

    return SL_ERR_TXT_NOT_SUPPORTED;
}

int txt_verify_platform(void)
{
    txt_heap_t *txt_heap;
    int err;

    /* check TXT supported */
    err = supports_txt();
    if ( err != SL_ERR_NONE )
        return err;

    if ( !vtd_bios_enabled() ) {
        return SL_ERR_VTD_NOT_SUPPORTED;
    }

    /* check is TXT_RESET.STS is set, since if it is SENTER will fail */
    txt_ests_t ests = (txt_ests_t)read_pub_config_reg(TXTCR_ESTS);
    if ( ests.txt_reset_sts ) {
        printk(SLEXEC_ERR"TXT_RESET.STS is set and SENTER is disabled (0x%02Lx)\n",
               ests._raw);
        return SL_ERR_SMX_NOT_SUPPORTED;
    }

    /* verify BIOS to OS data */
    txt_heap = get_txt_heap();
    if ( !verify_bios_data(txt_heap) )
        return SL_ERR_TXT_NOT_SUPPORTED;

    return SL_ERR_NONE;
}

/*
 * Local variables:
 * mode: C
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
