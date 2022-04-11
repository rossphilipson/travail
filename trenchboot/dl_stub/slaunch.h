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

#endif /* !IS_ENABLED(CONFIG_SECURE_LAUNCH) */

#endif /* _LINUX_SLAUNCH_H */
