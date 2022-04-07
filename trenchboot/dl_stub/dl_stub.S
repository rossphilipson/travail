/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Secure Launch dynamic launch event support.
 *
 * Copyright (c) 2022, Oracle and/or its affiliates.
 */
	.code32
	.text
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/msr.h>
#include <asm/processor-flags.h>
#include <asm/asm-offsets.h>
#include <asm/page_types.h>
#include <linux/slaunch.h>

	.code32
SYM_FUNC_START(dl_stub_entry)
	/* TODO */
SYM_FUNC_END(sl_stub_entry)

	.data
	.balign 8
SYM_DATA_START_LOCAL(dl_gdt_desc)
	.word	dl_gdt_end - dl_gdt - 1
	.long	dl_gdt - dl_gdt_desc
SYM_DATA_END_LABEL(dl_gdt_desc, SYM_L_LOCAL, dl_gdt_desc_end)

	.balign	8
SYM_DATA_START_LOCAL(dl_gdt)
	.quad	0x0000000000000000	/* NULL */
	.quad	0x00cf9a000000ffff	/* __SL32_CS */
	.quad	0x00cf92000000ffff	/* __SL32_DS */
SYM_DATA_END_LABEL(dl_gdt, SYM_L_LOCAL, dl_gdt_end)
