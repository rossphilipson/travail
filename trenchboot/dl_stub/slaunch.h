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

struct efi_drtm_info {
	u32 architecture;
	u64 txt_acm_base;
	u32 txt_acm_size;
}

#endif /* !IS_ENABLED(CONFIG_SECURE_LAUNCH) */

#endif /* _LINUX_SLAUNCH_H */
