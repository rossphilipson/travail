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

static void dl_txt_setup_mtrrs(void *drtm_table)
{
	struct drtm_entry_dce_info *dce_info;
	unsigned long cr0, cr4, msr;

	dce_info = (struct drtm_entry_dce_info *)
			drtm_next_of_type_subtype(drtm_table, NULL,
						  DRTM_ENTRY_DCE_INFO,
						  DRTM_DCE_TXT_ACM);
	if (!dce_info)
		sl_txt_reset(DL_ERROR_NO_DRTM_TABLE);

	if (!dce_info->dce_base || dce_info->size)
		sl_txt_reset(DL_ERROR_DCE_VALUES);

	/* Disable interrupts and caching */
	native_irq_disable();

	cr0 = __read_cr0();
	__write_cr0((cr0 & ~X86_CR0_NW) | X86_CR0_CD); /* CRO.NW=0 CRO.CD=1 */

	/* Now flush all caches and disable global pages */
	wbinvd();

	cr4 = __read_cr4();
	__write_cr4(cr4 & ~X86_CR4_PGE);

	/* Disable all MTRRs */
	msr = sl_rdmsr(MSR_MTRRdefType);
	sl_wrmsr(MSR_MTRRdefType, msr & ~MTRR_DEF_ENABLE_ALL);

	/* Setup ACM MTRRs as WB, rest of the world is UC, fixed MTRRs off */
	msr = sl_rdmsr(MSR_MTRRdefType);
	msr &= ~MTRR_DEF_ENABLE_FIXED;
	msr |= (MTRR_TYPE_UNCACHABLE & 0xff);
	sl_wrmsr(MSR_MTRRdefType, msr);

	/* TODO the rest */

	/* Flush all caches again and enable all MTRRs */
	wbinvd();

	mrs = sl_rdmsr(MSR_MTRRdefType);
	sl_wrmsr(MSR_MTRRdefType, msr | MTRR_DEF_ENABLE_ALL);

	/* Flush all caches again and restore control registers */
	__write_cr0(cr0);
	__write_cr4(cr4);

	/* Reenable interrupts */
	native_irq_enable();
}

void dynamic_launch_event(void *drtm_table)
{
	if (drtm_info->architecture == SL_INTEL_TXT) {
		/*
		 * Set ACM memory to WB and all other to UC. Note all
		 * MTRRs have been saved in the TXT heap for restoration
		 * after SENTER
		 */
		dl_txt_setup_mtrrs(drtm_table);

		/* TODO can we do exit_boot() after messing with MTRRs so we
		 * can use efi_err() etc? */
	} else if (drtm_info->architecture == SL_AMD_SKINIT) {
		/* TODO rewrite this if block if there is nothing to do for SKINIT */
	} else {
		/* TODO die horribly */
	}
}
