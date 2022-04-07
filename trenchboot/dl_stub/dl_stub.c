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

#define MTRR_DEF_DISABLE_ALL (1<<11)

static common_prepare_cpu(void)
{
	/* TODO prepare_cpu steps here */
}

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
	wrmsr(MSR_MTRRdefType, (msrlo & ~MTRR_DEF_DISABLE_ALL), msrhi);

	/* TODO setup ACM MTRRs here */

	/* Flush all caches again and restore control registers */
	wbinvd();
	__write_cr0(cr0);
	__write_cr4(cr4);

	/* Reenable interrupts */
	native_irq_enable();
}

void dynamic_launch_event(struct efi_drtm_info *drtm_info)
{
	/* Common steps to put the BSP in the right state for DL event */
	common_prepare_cpu();

	if (drtm_info->architecture == SL_INTEL_TXT) {
		/*
		 * Set ACM memory to WB and all other to UC. Note all
		 * MTRRs have been saved in the TXT heap for restoration
		 * after SENTER
		 */
		txt_setup_mtrrs(drtm_info);
	}
}
