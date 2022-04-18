/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Main Secure Launch header file.
 *
 * Copyright (c) 2022, Oracle and/or its affiliates.
 */

#ifndef _LINUX_SLAUNCH_H
#define _LINUX_SLAUNCH_H

/* NOTE truncated version of header for POC */

#if IS_ENABLED(CONFIG_SECURE_LAUNCH)

#define __SL32_CS       0x0008
#define __SL32_DS       0x0010

/*
 * Secure Launch DRTM Architecture Type
 */
#define SL_INTEL_TXT	1
#define SL_AMD_SKINIT	2

/* TODO add this: */

/*
 * SMX GETSEC Leaf Functions
 */
#define SMX_X86_GETSEC_SENTER	4
#define SMX_X86_GETSEC_SEXIT	5
#define SMX_X86_GETSEC_SMCTRL	7
#define SMX_X86_GETSEC_WAKEUP	8

#define DL_ERROR_NO_DRTM_TABLE		0xc0008101
#define DL_ERROR_INVALID_DCE_VALUES	0xc0008102

static inline u64 sl_txt_read(u32 reg)
{
	return readq((void *)(u64)(TXT_PRIV_CONFIG_REGS_BASE + reg));
}

static inline void sl_txt_write(u32 reg, u64 val)
{
	writeq(val, (void *)(u64)(TXT_PRIV_CONFIG_REGS_BASE + reg));
}

static void __noreturn sl_txt_reset(u64 error)
{
	/* Reading the E2STS register acts as a barrier for TXT registers */
	sl_txt_write(TXT_CR_ERRORCODE, error);
	sl_txt_read(TXT_CR_E2STS);
	sl_txt_write(TXT_CR_CMD_UNLOCK_MEM_CONFIG, 1);
	sl_txt_read(TXT_CR_E2STS);
	sl_txt_write(TXT_CR_CMD_RESET, 1);

	for ( ; ; )
		asm volatile ("hlt");

	unreachable();
}

#endif /* !IS_ENABLED(CONFIG_SECURE_LAUNCH) */

#endif /* _LINUX_SLAUNCH_H */
