/*
 * Copyright (c) 2022, Oracle and/or its affiliates.
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
#include <misc.h>
#include <processor.h>
#include <loader.h>
#include <e820.h>
#include <linux.h>
#include <skinit/skl.h>

/*
 * CPUID extended feature info
 */
static unsigned int g_cpuid_ext_feat_info = 0;

int supports_skinit(void)
{
    g_cpuid_ext_feat_info = cpuid_ecx(CPUID_X86_EXT_FEATURE_INFO_LEAF);

    if (g_cpuid_ext_feat_info & CPUID_X86_EXT_FEATURE_SKINIT) {
        printk(SLEXEC_INFO"SKINIT CPU and all needed capabilities present\n");
        return SL_ERR_NONE;
    }
    return SL_ERR_SKINIT_NOT_SUPPORTED;
}

/* Broadcast INIT to all APs except self */
static void send_init_ipi_shorthand(void)
{
    uint32_t *icr_reg;
    uint32_t apic_base = get_apic_base();

    /* accessing the ICR depends on the APIC mode */
    if (apic_base & X2APIC_ENABLE) {
        mb();

        /* access ICR through MSR */
        wrmsr(MSR_X2APIC_ICR, (ICR_DELIVER_EXCL_SELF|ICR_MODE_INIT));
        printk(SLEXEC_INFO"SKINIT assert #INIT on APs - x2APIC MSR reg: 0x%x\n", MSR_X2APIC_ICR);
    } else {
        /* mask off low order bits to get base address */
        apic_base &= APICBASE_BASE_MASK;
        /* access ICR through MMIO */
        icr_reg = (uint32_t *)(apic_base + LAPIC_ICR_LO);

        writel(icr_reg, (ICR_DELIVER_EXCL_SELF|ICR_MODE_INIT));
        printk(SLEXEC_INFO"SKINIT assert #INIT on APs - xAPIC ICR reg: %p\n", icr_reg);
    }

    printk(SLEXEC_INFO"Wait for IPI delivery\n");
    delay(1000);
}

void skinit_launch_environment(void)
{
    uint32_t slb = (uint32_t)g_skl_module;

    send_init_ipi_shorthand();

    disable_intr();

    printk(SLEXEC_INFO"SKINIT launch SKL - slb: 0x%x\n", slb);
    asm volatile ("movl %0, %%eax\n"
	          "skinit\n"
                  : : "r" (slb));

    printk(SLEXEC_INFO"SKINIT failed\n");
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
