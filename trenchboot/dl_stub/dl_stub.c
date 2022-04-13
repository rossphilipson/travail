// SPDX-License-Identifier: GPL-2.0
/*
 * Secure Launch dynamic launch event support.
 *
 * Copyright (c) 2022, Oracle and/or its affiliates.
 */

#include <linux/init.h>
#include <linux/string.h>
#include <linux/linkage.h>
#include <linux/efi.h>
#include <asm/segment.h>
#include <asm/boot.h>
#include <asm/msr.h>
#include <asm/io.h>
#include <asm/mtrr.h>
#include <asm/msr-index.h>
#include <asm/processor-flags.h>
#include <asm/special_insns.h>
#include <asm/asm-offsets.h>
#include <asm/bootparam.h>
#include <asm/efi.h>
#include <asm/bootparam_utils.h>
#include <linux/slaunch.h>

#define MTRR_DEF_ENABLE_FIXED	(1<<10)
#define MTRR_DEF_ENABLE_ALL	(1<<11)

extern void __noreturn dl_stub_entry(u64 architecture,
				     u64 dce_phys_addr,
				     u64 dce_size);

static void txt_setup_mtrrs(struct efi_drtm_info *drtm_info)
{
	unsigned long cr0, cr4, mrslo, msrhi;

	/* Disable interrupts and caching */
	native_irq_disable();

	cr0 = __read_cr0();
	__write_cr0((cr0 & ~X86_CR0_NW) | X86_CR0_CD); /* CRO.NW=0 CRO.CD=1 */

	/* Now flush all caches and disable global pages */
	wbinvd();

	cr4 = __read_cr4();
	__write_cr4(cr4 & ~X86_CR4_PGE);

	/* Disable all MTRRs */
	rdmsr(MSR_MTRRdefType, msrlo, msrhi);
	wrmsr(MSR_MTRRdefType, (msrlo & ~MTRR_DEF_ENABLE_ALL), msrhi);

	/* Setup ACM MTRRs as WB, rest of the world is UC, fixed MTRRs off */
	rdmsr(MSR_MTRRdefType, msrlo, msrhi);
	msrlo &= ~MTRR_DEF_ENABLE_FIXED;
	msrlo |= (MTRR_TYPE_UNCACHABLE & 0xff);
	wrmsr(MSR_MTRRdefType, msrlo, msrhi);

	/* TODO the rest */

	/* Flush all caches again and enable all MTRRs */
	wbinvd();

	rdmsr(MSR_MTRRdefType, msrlo, msrhi);
	wrmsr(MSR_MTRRdefType, (msrlo | MTRR_DEF_ENABLE_ALL), msrhi);

	/* Flush all caches again and restore control registers */
	__write_cr0(cr0);
	__write_cr4(cr4);

	/* Reenable interrupts */
	native_irq_enable();
}

void dynamic_launch_event(struct efi_drtm_info *drtm_info)
{
	if (drtm_info->architecture == SL_INTEL_TXT) {
		/*
		 * Set ACM memory to WB and all other to UC. Note all
		 * MTRRs have been saved in the TXT heap for restoration
		 * after SENTER
		 */
		txt_setup_mtrrs(drtm_info);
	} else if (drtm_info->architecture == SL_INTEL_SKINIT) {
	} else {
		/* TODO die horribly */
	}
}
