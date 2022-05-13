/*
 * Copyright (c) 2019 Oracle and/or its affiliates. All rights reserved.
 *
 * Author:
 *     Ross Philipson <ross.philipson@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <defs.h>

/* Selectors, CS and SS compatible with initial state after SKINIT */
#define CS_SEL32         0x0008
#define DS_SEL           0x0010
#define CS_SEL64         0x0018

	.section .headers, "ax", @progbits

GLOBAL(sl_header)
	.word	_entry           /* SL header SKL offset to code start */
	.word	bootloader_data  /* SL header SKL measured length */
	.word	dlmod_info
	.long	0xffaa7711
ENDDATA(sl_header)

GLOBAL(dlmod_stack_canary)
	.long STACK_CANARY
ENDDATA(dlmod_stack_canary)
dlmod_stack:
	.fill 0x280, 1, 0xcc   /* Enough space for now */
	.align 0x10, 0         /* Ensure proper alignment for 64bit */
.L_stack_base:
ENDDATA(dlmod_stack)

	.text
	.code32

GLOBAL(_entry)
	/*
	 * %eax	- Beginning of DLMOD containing the SL header
	 * %edi - DRTM/SL table pointer
	 * %esi - DL stub entry point to jump to
	 *
	 * Restore the world, get back into long mode.
	 *
	 * The GDT needs relocating before we have a usable %ds, which must be
	 * done with an %ss-relative write.  Therefore, we store the base
	 * address in %ebp.
	 */
	mov	%eax, %ebp

	/* Set up the Stage 1 stack. */
	lea	.L_stack_base(%ebp), %esp

	/*
	 * Clobber IDTR.limit to prevent stray interrupts/exceptions/INT from
	 * vectoring via the IVT into unmeasured code.
	 */
	push	$0
	push	$0
	lidt	(%esp)
	add	$8, %esp

	/* Clear DIS_A20M. R_INIT is cleared by the kernel. */
	mov	$IA32_VM_CR, %ecx
	rdmsr
	and	$~(VM_CR_DIS_A20M), %eax
	wrmsr

	/* Relocate GDT.base, 64bit ljmp offset, and pagetables. */
	add	%ebp, 2 + gdtr(%ebp)
	add	%ebp, 1 + .Ljump64(%ebp)
	add	%ebp,        l4_identmap(%ebp) /* L4[0] => L3        */
	add	%ebp,        l3_identmap(%ebp) /* L3[0] => First  L2 */
	add	%ebp, 0x08 + l3_identmap(%ebp) /* L3[1] => Second L2 */
	add	%ebp, 0x10 + l3_identmap(%ebp) /* L3[2] => Third  L2 */
	add	%ebp, 0x18 + l3_identmap(%ebp) /* L3[3] => Fourth L2 */
	add	%ebp,        l2_identmap(%ebp) /* L2[0] => L1        */

	/* Load GDT */
	lgdt	gdtr(%ebp)

	/* Load data segment regs.  %ss is already flat and matches DS_SEL. */
	mov	$DS_SEL, %eax
	mov	%eax, %ds
	mov	%eax, %es

	/* Restore CR4, PAE must be enabled before IA-32e mode */
	mov	%cr4, %ecx
	or	$CR4_PAE, %ecx
	mov	%ecx, %cr4

	/* Load PML4 table location into PT base register */
	lea	l4_identmap(%ebp), %eax
	mov	%eax, %cr3

	/* Enable IA-32e mode and paging */
	mov	$IA32_EFER, %ecx
	rdmsr
	or	$EFER_LME >> 8, %ah
	wrmsr

	mov	%cr0, %eax
	or	$CR0_PG | CR0_NE | CR0_TS | CR0_MP, %eax
	mov	%eax, %cr0

	/* Now in IA-32e compatibility mode, ljmp to 64b mode */
.Ljump64:
        ljmp	$CS_SEL64, $1f /* Offset - dynamically relocated. */

	.code64

1:
	/* DRTM table/DL entry point already in %rdi/%rsi */
	pushq	%rdi
	pushq	%rsi
	call	dlmod_main
	popq	%rsi
	popq	%rdi

	/* Jump to the DL entry point */
	jmp	*%rsi

ENDFUNC(_entry)

	.data

.align 8
gdt:
	/* Null Segment. Reused for 32bit GDTR */
	.word   0
gdtr:
	.word	.Lgdt_end - gdt - 1 /* Limit */
	.long	gdt                 /* Base - dynamically relocated. */
ENDDATA(gdtr)

	/* 32b Code Segment */
	.word	0xffff /* Limit 1 */
	.word	0x0000 /* Base 1 */
	.byte	0x00   /* Base 2 */
	.byte	0x9b   /* P=1 DPL=0 S=1 Type=0010 C=0 W=1 A=1 */
	.byte	0xcf   /* G=1 D=1 L=0 AVL=0 Limit 2 */
	.byte	0x00   /* Base 3 */
	/* Data Segment, can be used both in 32b and 64b */
	.word	0xffff /* Limit 1 */
	.word	0x0000 /* Base 1 */
	.byte	0x00   /* Base 2 */
	.byte	0x93   /* P=1 DPL=0 S=1 Type=0010 C=0 W=1 A=1 */
	.byte	0xcf   /* G=1 D=1 L=0 AVL=0 Limit 2 */
	.byte	0x00   /* Base 3 */
#ifdef __x86_64__
	/* 64b Code Segment */
	.word	0x0000 /* Limit 1 */
	.word	0x0000 /* Base 1 */
	.byte	0x00   /* Base 2 */
	.byte	0x9b   /* P=1 DPL=0 S=1 Type=1010 C=0 R=1 A=1 */
	.byte	0x20   /* G=0 D=0 L=1 AVL=0 Limit 2 */
	.byte	0x00   /* Base 3 */
#endif
.Lgdt_end:
ENDDATA(gdt)

.section .page_data, "a", @progbits
.align PAGE_SIZE
#ifdef __x86_64__
	/* 64bit Pagetables, identity map of the first 4G of RAM. */

l1_identmap: /* 1x L1 page, mapping 2M of RAM.  No relocations. */
	idx = 0
	.rept 512
	.quad (idx << L1_PT_SHIFT) + _PAGE_AD + _PAGE_RW + _PAGE_PRESENT
	idx = idx + 1
	.endr
ENDDATA(l1_identmap)

l2_identmap: /* 4x L2 pages, each mapping 1G of RAM.  1 relocation. */
	.quad l1_identmap + _PAGE_AD + _PAGE_RW + _PAGE_PRESENT
	idx = 1
	.rept (512 * 4) - 1
	.quad (idx << L2_PT_SHIFT) + _PAGE_PSE + _PAGE_AD + _PAGE_RW + _PAGE_PRESENT
	idx = idx + 1
	.endr
ENDDATA(l2_identmap)

l3_identmap: /* 1x L3 page, mapping the 4x L2 pages.  4 relocations. */
	idx = 0
	.rept 4
	.quad l2_identmap + (idx * PAGE_SIZE) + _PAGE_AD + _PAGE_RW + _PAGE_PRESENT
	idx = idx + 1
	.endr
	.align PAGE_SIZE
ENDDATA(l3_identmap)

l4_identmap: /* 1x L4 page, mapping the L3 page. 1 relocation. */
	.quad l3_identmap + _PAGE_AD + _PAGE_RW + _PAGE_PRESENT
	.align PAGE_SIZE
ENDDATA(l4_identmap)
#endif

	.section .bootloader_data, "a", @progbits
GLOBAL(bootloader_data)
	.word	0			/* Dummy data to make sure that file isn't truncated */
