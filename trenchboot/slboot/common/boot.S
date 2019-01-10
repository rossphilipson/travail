/*
 * boot.S: assembly bootstrapping code for tboot module
 *
 * Copyright (c) 2006-2010, Intel Corporation
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

#include <multiboot.h>
#include <config.h>
#include <msr.h>
#include <page.h>
#include <processor.h>

#define BSP_STACK_SIZE		0x2000

#define cs_sel      1<<3
#define ds_sel      2<<3
#define cs16_sel    4<<3
#define ds16_sel    5<<3

.section ".slboot_multiboot_header","w"
        .align 4
/* multiboot header */
multiboot_header:
#define MULTIBOOT_HEADER_FLAGS (MULTIBOOT_HEADER_MODS_ALIGNED | \
                                MULTIBOOT_HEADER_WANT_MEMORY)
        /* magic number for multiboot header */
        .long MULTIBOOT_HEADER_MAGIC
        /* flags for bootloader */
        .long MULTIBOOT_HEADER_FLAGS
        /* checksum: negated sum of above */
        .long -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)

/* multiboot2 header */
        .align 8
multiboot2_header:
        .long MB2_HEADER_MAGIC
        .long MB2_ARCH_X86
        .long multiboot2_header_end - multiboot2_header
        /* checksum */
        .long -(MB2_HEADER_MAGIC + MB2_ARCH_X86 + (multiboot2_header_end - multiboot2_header))
multiboot2_header_end:
        /* tag requests here--RLM, fix me!! Well, maybe! */
        .short MB2_HDR_TAG_END
        .short 0
        .long 8

	.text

ENTRY(start)
ENTRY(_start)
        jmp __start

ENTRY(__start)
        /* Set up a few descriptors: on entry only CS is guaranteed good. */
        lgdt    %cs:gdt_descr
        mov     $(ds_sel),%ecx
        mov     %ecx,%ds
        mov     %ecx,%es
        mov     %ecx,%fs
        mov     %ecx,%gs
        mov     %ecx,%ss
        ljmp    $(cs_sel),$(1f)
1:	leal	bsp_stack,%esp

        /* Reset EFLAGS (subsumes CLI and CLD). */
        pushl   $0
        popf

        /* preserve EAX to be a param to begin_launch--it should
         *  contain either MULTIBOOT_MAGIC or MULTIBOOT2_MAGIC--we'll need
         * to figure out which */
        mov     %eax,%edx

        /* Initialize BSS (no nasty surprises!) */
        mov     $__bss_start,%edi
        mov     $_end,%ecx
        sub     %edi,%ecx
        xor     %eax,%eax
        rep     stosb

        /* Load IDT */
        lidt    idt_descr

        /* enable MCE */
        mov     %cr4,%eax
        or      $CR4_MCE,%eax
        mov     %eax,%cr4

        /* pass multiboot info struct, magic and call measured launch code */
        push    %edx
        push    %ebx
        call    begin_launch
        ud2

/*
 * interrupt handler
 */

int_handler:
	call handle_exception
	ud2

/*
 * descriptors and descriptor tables
 */

	.align 8

/* GDT */
gdt_descr:
	.word	gdt_table_end - gdt_table - 1
	.long	gdt_table

        .align PAGE_SIZE, 0

ENTRY(gdt_table)
		/* unused */
        .quad	0x0000000000000000
cs_descr:	/* cs */
	.word	0xffff		/* limit = 4GB */
	.word	0x00		/* base = 0 */
	.word	0x9b00		/* read + exec + accessed */
	.word	0x00cf		/* granularity = 4096 */
ds_descr:	/* ds */
	.word	0xffff		/* limit = 4GB */
	.word	0x00		/* base = 0 */
	.word	0x9300		/* read + write + accessed */
	.word	0x00cf		/* granularity = 4096 */
tss_descr:	/* tss */
	.word	0xffff		/* limit = 4GB */
	.word	0x00		/* base = 0 */
	.word	0x8900		/* system segment, 32b available TSS */
	.word	0x008f		/* granularity = 4096 */
cs16_desc:	/* cs16 */
	.word	0xffff		/* limit = 4GB */
	.word   0x0000      /* base = 0 */
	.word   0x9b00      /* read + exec + accessed */
	.word	0x008f      /* granularity = 4096, D = 0 */
ds16_desc:  /* ds16 */
	.word   0xffff      /* limit = 4GB */
	.word   0x0000      /* base = 0 */
	.word   0x9300      /* read + exec + accessed */
	.word   0x008f      /* granularity = 4096, D = 0 */
		/* end (unused) */
	.quad   0x0000000000000000
ENTRY(gdt_table_end)

/* IDT */
idt_descr:
	.word	idt_table_end - idt_table - 1
	.long	idt_table

	.align	8

idt_table:
	.rept 18
		.word	int_handler - _start
		.word	cs_sel
		.word	0x8e00   /* present, DPL=0, 32b, interrupt */
		.word	(int_handler - _start + TBOOT_START) >> 16
	.endr
	/* for machine-check exception */
		.word	int_handler - _start
		.word	cs_sel
		.word	0x8f00   /* present, DPL=0, 32b, trap */
		.word	(int_handler - _start + TBOOT_START) >> 16
	.rept 237
		.word	int_handler - _start
		.word	cs_sel
		.word	0x8e00   /* present, DPL=0, 32b, interrupt */
		.word	(int_handler - _start + TBOOT_START) >> 16
	.endr
idt_table_end:

/*
 * stacks
 */

.section ".bss.stack_aligned","w"

bsp_stack_end:
        .fill BSP_STACK_SIZE, 1, 0
bsp_stack:

/*
 * misc. bss data
 */
.section ".bss"

.section ".data"

ENTRY(_end)
