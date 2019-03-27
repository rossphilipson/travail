/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (c) 2019 Oracle and/or its affiliates. All rights reserved.
 *
 * Author(s):
 *     Ross Philipson <ross.philipson@oracle.com>
 */
	.code32
	.text
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/msr.h>
#include <asm/processor-flags.h>
#include <asm/asm-offsets.h>
#include <asm/bootparam.h>
#include <asm/irq_vectors.h>
#include <asm/slaunch.h>

/* Can't include apiddef.h in asm */
#define APIC_BASE_MSR	0x800
#define XAPIC_ENABLE	(1 << 11)
#define X2APIC_ENABLE	(1 << 10)
#define	APIC_EOI	0xB0
#define	APIC_EOI_ACK	0x0

	/* The MLE Header per the TXT Specification, section 4.1 */
	.global	sl_mle_header
sl_mle_header:
	.long	0x9082ac5a    /* UUID0 */
	.long	0x74a7476f    /* UUID1 */
	.long	0xa2555c0f    /* UUID2 */
	.long	0x42b651cb    /* UUID3 */
	.long	0x00000034    /* MLE header size */
	.long	0x00020002    /* MLE version 2.2 */
	.long	sl_stub_entry /* Linear entry point of MLE (virt. address) */
	.long	0x00000000    /* First valid page of MLE */
	.long	0x00000000    /* Offset within binary of first byte of MLE */
	.long	0x00000000    /* Offset within binary of last byte + 1 of MLE */
	.long	0x00000223    /* Bit vector of MLE-supported capabilities */
	.long	0x00000000    /* Starting linear address of command line */
	.long	0x00000000    /* Ending linear address of command line */

	.code32
ENTRY(sl_stub)
	/*
	 * On entry, %ebp has the base address from head_64.S
	 * and only %cs is known good
	 */
	cli
	cld

	/*
	 * Take the first stack for the BSP. The AP stacks are only used
	 * on Intel.
	 */
	leal	sl_stacks_end(%ebp), %esp

	/* Load GDT, set segment regs and lret to __SL32_CS */
	addl	%ebp, (sl_gdt_desc + 2)(%ebp)
	lgdt	sl_gdt_desc(%ebp)

	movl	$(__SL32_DS), %eax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss

	leal	.Lsl_cs(%ebp), %eax
	pushl	$(__SL32_CS)
	pushl	%eax
	lret

.Lsl_cs:
	addl	$8, %esp

	/* Before going any further, make sure this is the BSP */
	movl	$(MSR_IA32_APICBASE), %ecx
	rdmsr
	testl	$(MSR_IA32_APICBASE_BSP), %eax
	jnz	.Lbsp_ok
	ud2

.Lbsp_ok:
	/* Assume CPU is AMD to start */
	movl	$(SL_CPU_AMD), %edi

	/* Now see if it is Intel */
	movl	$0x0, %eax
	cpuid
	cmpl	$0x756e6547, %ebx # GenuineIntel?
	jnz	.Lcpu_check_done
	cmpl	$0x49656e69, %edx
	jnz	.Lcpu_check_done
	cmpl	$0x6c65746e, %ecx
	jnz	.Lcpu_check_done
	movl	$(SL_CPU_INTEL), %edi

.Lcpu_check_done:
	/* Now that we know what CPU it is, do vendor specific operations */
	cmpl	$(SL_CPU_AMD), %edi
	jz	.Ldo_amd

	/* Know it is Intel */
	movl	$(SL_CPU_INTEL), sl_cpu_type(%ebp)

	/* Increment CPU count for BSP */
	incl	sl_txt_cpu_count(%ebp)

	/* Enable SMI with GETSET[SMCTRL] */
	xorl	%ebx, %ebx
	movl	$(TXT_X86_GETSEC_SMCTRL), %eax
	.byte 	0x0f, 0x37 /* GETSEC opcode */

	/* IRET-to-self can be used to enable NMIs which SENTER disabled */
	leal	.Lnmi_enabled(%ebp), %eax
	pushfl
	pushl	$(__SL32_CS)
	pushl	%eax
	iret

.Lnmi_enabled:
	addl	$12, %esp

	/* Clear the TXT error registers for a clean start of day */
	movl	$0, (TXT_PRIV_CONFIG_REGS_BASE + TXTCR_ERRORCODE)
	movl	$0xffffffff, (TXT_PRIV_CONFIG_REGS_BASE + TXTCR_ESTS)

	/* On Intel, the zero page address is passed in the TXT heap */
	/* Read physical base of heap into EAX */
	movl	(TXT_PRIV_CONFIG_REGS_BASE + TXTCR_HEAP_BASE), %eax
	/* Read the size of the BIOS data into ECX (in first 8 bytes */
	movl	(%eax), %ecx
	/* Skip over BIOS data and size of OS to MLE */
	addl	%ecx, %eax
	addl	$8, %eax
	/* First 4 bytes of OS to MLE are the version */
	/* Second 4 bytes of OS to MLE are the zero page */
	movl	TXT_OS_MLE_ZERO_PAGE_ADDR(%eax), %esi

	/* Save ebp so the APs can find their way home */
	movl	%ebp, TXT_OS_MLE_AP_WAKE_EBP(%eax)

	/* Store the AP PM entry address location that is update later */
	leal	sl_ap_pm_entry_addr(%ebp), %ecx
	movl	%ecx, TXT_OS_MLE_AP_PM_ENTRY(%eax)

	/* Note only %esi and %ebp MUST be preserved across calls */
	movl	%eax, %edi
	call	sl_txt_load_regs

	/* Wake up all APs and wait for them to halt */
	call	sl_txt_wake_aps

	jmp	.Lcpu_setup_done

.Ldo_amd:
	/* Know it is AMD */
	movl	$(SL_CPU_AMD), sl_cpu_type(%ebp)

	/*
	 * Disable maskable interrups in EFLAGS then enable global interrupts
	 * including SMI and NMI (GIF).
	 */
	cli
	stgi

	/* On AMD %esi is set up by the Landing Zone, just go on */

.Lcpu_setup_done:
	/*
	 * Don't enable MCE at this point. The kernel will enable
	 * it on the BSP later when it is ready.
	 */

	/* Keep SL segments for the early portion of the kernel boot */
	orb	$(KEEP_SEGMENTS), BP_loadflags(%esi)

	/* Done, jump to normal 32b pm entry */
	jmp	startup_32
ENDPROC(sl_stub)

ENTRY(sl_txt_ap_entry)
	cli
	cld

	/*
	 * The code segment is known good. The data segments are
	 * fine too so we can get to our stack before loading the
	 * GDT.
	 *
	 * First order of business is to find where we are and
	 * save it in ebp.
	 */

	/* Read physical base of heap into EAX */
	movl	(TXT_PRIV_CONFIG_REGS_BASE + TXTCR_HEAP_BASE), %eax
	/* Read the size of the BIOS data into ECX (in first 8 bytes */
	movl	(%eax), %ecx
	/* Skip over BIOS data and size of OS to MLE */
	addl	%ecx, %eax
	addl	$8, %eax

	/* Saved ebp from the BSP and stash OS-MLE pointer */
	movl	TXT_OS_MLE_AP_WAKE_EBP(%eax), %ebp
	movl	%eax, %edi

	/* Lock and get our stack index */
	movl	$1, %ecx
.Lspin:
	xorl	%eax, %eax
	lock cmpxchgl	%ecx, sl_txt_spin_lock(%ebp)
	jnz	.Lspin

	leal	sl_txt_stack_index(%ebp), %ebx
	movl	(%ebx), %eax
	incl	%eax
	movl	%eax, (%ebx)

	/* Unlock */
	movl	$0, sl_txt_spin_lock(%ebp)

	/* Load our AP stack */
	movl	$(TXT_BOOT_STACK_SIZE), %edx
	mull	%edx
	leal	sl_stacks_end(%ebp), %edx
	subl	%eax, %edx
	movl	%edx, %esp

	/* Load GDT, set segment regs and lret to __SL32_CS */
	lgdt	sl_gdt_desc(%ebp)

	movl	$(__SL32_DS), %eax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %fs
	movw	%ax, %gs
	movw	%ax, %ss

	leal	.Lsl_ap_cs(%ebp), %eax
	pushl	$(__SL32_CS)
	pushl	%eax
	lret

.Lsl_ap_cs:
	addl	$8, %esp

	/*xorl	%eax, %eax
	lldt	%ax
	movl    $__SL32_TSS, %eax
	ltr	%ax*/

	/* Load the IDT */
	lidt	sl_idt_desc(%ebp)

	/* Enable SMI with GETSET[SMCTRL] */
	xorl	%ebx, %ebx
	movl	$(TXT_X86_GETSEC_SMCTRL), %eax
	.byte 	0x0f, 0x37 /* GETSEC opcode */

	/* IRET-to-self can be used to enable NMIs which SENTER disabled */
	leal	.Lnmi_enabled_ap(%ebp), %eax
	pushfl
	pushl	$(__SL32_CS)
	pushl	%eax
	iret

.Lnmi_enabled_ap:
	addl	$12, %esp

	/* Fixup MTRRs and misc enable MSR on APs too */
	call	sl_txt_load_regs

	/* Put APs in X2APIC mode like the BSP */
	movl	$(MSR_IA32_APICBASE), %ecx
	rdmsr
	movl	%eax, %edi
	orl	$(XAPIC_ENABLE|X2APIC_ENABLE), %eax
	wrmsr

	/* Basically done, increment the CPU count and wait for IPI */
	xorl	%ebx, %ebx
	sti
	lock incl	sl_txt_cpu_count(%ebp)

1:
	cmpl	$0, %ebx
	jnz	2f
	pause
	jmp	1b
2:

	/* Restore whatever the APIC mode was before, %edx unchanged */
	movl	$(MSR_IA32_APICBASE), %ecx
	movl	%edi, %eax
	wrmsr

	.byte	0xea
sl_ap_pm_entry_addr:
	.long	0x00000000
	.word	__SL32_CS
ENDPROC(sl_txt_ap_entry)

ENTRY(sl_txt_load_regs)
	/*
	 * On Intel, the original variable MTRRs and Misc Enable MSR are
	 * restored on the BSP at early boot. Each AP will also restore
	 * its MTRRs and Misc Enable MSR.
	 */
	pushl	%edi
	addl	$(TXT_OS_MLE_MTRR_STATE), %edi
	movl	(%edi), %ebx
	pushl	%ebx /* default_type_reg lo */
	addl	$4, %edi
	movl	(%edi), %ebx
	pushl	%ebx /* default_type_reg hi */
	addl	$4, %edi
	movl	(%edi), %ebx /* mtrr_vcnt lo, don't care about hi part */
	addl	$8, %edi /* now at MTRR pair array */
	/* Write the variable MTRRs */
	movl	$(MTRRphysBase0), %ecx
1:
	cmpl	$0, %ebx
	jz	2f

	movl	(%edi), %eax /* MTRRphysBaseX lo */
	addl	$4, %edi
	movl	(%edi), %edx /* MTRRphysBaseX hi */
	wrmsr
	addl	$4, %edi
	incl	%ecx
	movl	(%edi), %eax /* MTRRphysMaskX lo */
	addl	$4, %edi
	movl	(%edi), %edx /* MTRRphysMaskX hi */
	wrmsr
	addl	$4, %edi
	incl	%ecx

	decl	%ebx
	jmp	1b
2:
	/* Write the default MTRR register */
	popl	%edx
	popl	%eax
	movl	$(MSR_MTRRdefType), %ecx
	wrmsr

	/* Return to beginning and write the misc enable msr */
	popl	%edi
	addl	$(TXT_OS_MLE_MISC_EN_MSR), %edi
	movl	(%edi), %eax /* saved_misc_enable_msr lo */
	addl	$4, %edi
	movl	(%edi), %edx /* saved_misc_enable_msr hi */
	movl	$(MSR_IA32_MISC_ENABLE), %ecx
	wrmsr

	ret
ENDPROC(sl_txt_load_regs)

ENTRY(sl_txt_wake_aps)
	/* First setup the IDT for the APs to use */
	leal	sl_txt_int_ipi_wake(%ebp), %ebx
	leal	sl_idt(%ebp), %ecx
	xorl	%edx, %edx

1:
	cmpw	$(NR_VECTORS), %dx
	jz	2f

	/* Load wake IPI vector */
	movl	%ebx, %eax
	movw	%ax, (%ecx)
	shrl	$16, %eax
	movw	%ax, 6(%ecx)

	incw	%dx
	addl	$8, %ecx
	jmp	1b

2:
	/* Fixup descriptor */
	addl	%ebp, (sl_idt_desc + 2)(%ebp)

	/* Next setup the MLE join structure and load it into TXT reg */
	leal	sl_gdt(%ebp), %eax
	leal	sl_txt_ap_entry(%ebp), %ecx
	leal	sl_txt_mle_join(%ebp), %edx
	movl	%eax, 4(%edx)
	movl	%ecx, 12(%edx)
	movl	%edx, (TXT_PRIV_CONFIG_REGS_BASE + TXTCR_MLE_JOIN)

	/* Another TXT heap walk to find various values needed to wake APs */
	movl	(TXT_PRIV_CONFIG_REGS_BASE + TXTCR_HEAP_BASE), %eax
	/* At BIOS data size, find the number of logical processors */
	movl	(TXT_BIOS_NUM_LOG_PROCS + 8)(%eax), %edx
	/* Skip over BIOS data */
	movl	(%eax), %ecx
	addl	%ecx, %eax
	/* Skip over OS to MLE */
	movl	(%eax), %ecx
	addl	%ecx, %eax
	/* At OS-SNIT size, get capabilities to know how to wake up the APs */
	movl	(TXT_OS_SINIT_CAPABILITIES + 8)(%eax), %ebx
	/* Skip over OS to SNIT */
	movl	(%eax), %ecx
	addl	%ecx, %eax
	/* At SNIT-MLE size, get the AP wake MONITOR address */
	movl	(TXT_SINIT_MLE_RLP_WAKEUP_ADDR + 8)(%eax), %edi

	/* Determine how to wake up the APs */
	testl	$(1 << TXT_SINIT_CAPS_WAKE_MONITOR), %ebx
	jz	.Lwake_getsec

	/* Wake using MWAIT MONITOR */
	movl	$1, (%edi)
	jmp	.Laps_awake

.Lwake_getsec:
	/* Wake using GETSEC(WAKEUP) */
	xorl	%ebx, %ebx
	movl	$(TXT_X86_GETSEC_WAKEUP), %eax
	.byte 	0x0f, 0x37 /* GETSEC opcode */

.Laps_awake:
	/* Wait for all of them to halt */
1:
	cmpl	sl_txt_cpu_count(%ebp), %edx
	jz	2f
	pause
	jmp	1b

2:
	ret
ENDPROC(sl_txt_wake_aps)

ENTRY(sl_txt_int_ipi_wake)
	movl	$1, %ebx

	movl	$(APIC_EOI), %ecx
	shrl	$4, %ecx
	addl	$(APIC_BASE_MSR), %ecx
	movl	$(APIC_EOI_ACK), %eax
	wrmsr

	iret
ENDPROC(sl_txt_int_ipi_wake)

ENTRY(sl_txt_int_reset)
	movl	$(TXT_SLERROR_INV_AP_INTERRUPT), (TXT_PRIV_CONFIG_REGS_BASE + TXTCR_ERRORCODE)
	movl	(TXT_PRIV_CONFIG_REGS_BASE + TXTCR_E2STS), %eax
	movl	$1, (TXT_PRIV_CONFIG_REGS_BASE + TXTCR_CMD_UNLOCK_MEM_CONFIG)
	movl	(TXT_PRIV_CONFIG_REGS_BASE + TXTCR_E2STS), %eax
	movl	$1, (TXT_PRIV_CONFIG_REGS_BASE + TXTCR_CMD_RESET)
1:
	pause
	jmp 	1b
ENDPROC(sl_txt_int_reset)

	.data
	.balign 4096
sl_gdt_desc:
	.word	sl_gdt_end - sl_gdt - 1
	.long	sl_gdt
sl_gdt_desc_end:

	.balign	16
sl_gdt:
	.quad	0x0000000000000000	/* NULL */
	.quad	0x00cf9a000000ffff	/* __SL32_CS */
	.quad	0x00cf92000000ffff	/* __SL32_DS */
	.quad	0x008f89000000ffff	/* __SL32_TSS */
sl_gdt_end:

	.balign 16
sl_idt_desc:
	.word	sl_idt_end - sl_idt - 1	/* Limit */
	.long	sl_idt			/* Base */
sl_idt_desc_end:

	.balign 16
sl_idt:
	.rept	NR_VECTORS
	.word	0x0000		/* Offset 15 to 0 */
	.word	__SL32_CS	/* Segment selector */
	.word	0x8e00		/* Present, DPL=0, 32b Vector, Interrupt */
	.word	0x0000		/* Offset 31 to 16 */
	.endr
sl_idt_end:

	.balign 16
sl_txt_mle_join:
	.long	sl_gdt_end - sl_gdt - 1	/* GDT limit */
	.long	0x00000000		/* GDT base */
	.long	__SL32_CS	/* Seg Sel - CS (DS, ES, SS = seg_sel+8) */
	.long	0x00000000	/* Entry point physical address */

	.global	sl_cpu_type
sl_cpu_type:
	.long	0x00000000

sl_txt_spin_lock:
	.long	0x00000000

sl_txt_stack_index:
	.long	0x00000000

sl_txt_cpu_count:
	.long	0x00000000

	/* Small stacks for BSP and APs to work with */
	.balign 4
sl_stacks:
	.fill (TXT_MAX_CPUS*TXT_BOOT_STACK_SIZE), 1, 0
sl_stacks_end:
