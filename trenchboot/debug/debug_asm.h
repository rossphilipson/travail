#ifndef _DEBUG_ASM
#define _DEBUG_ASM

#define DEBUG_MEMLOC 0x48000

.macro DEBUG_PUTC char
#ifdef DEBUG_USE_MEMLOC
	movl	%ecx, (DEBUG_MEMLOC)
	movl	%edx, (DEBUG_MEMLOC + 4)
	movl	%eax, (DEBUG_MEMLOC + 8)
#else
	pushl	%ecx
	pushl	%edx
	pushl	%eax
#endif
	xorl	%ecx, %ecx
.Ldbg_loop_\@:
	cmpb	$5, %cl
	jz	.Ldbg_done_\@
	movw	$0x3f8, %dx
	addw	$5, %dx
.Ldbg_wait_\@:
	inb	%dx, %al
	testb	$0x20, %al
	jz	.Ldbg_wait_\@
	movw	$0x3f8, %dx
	movb	\char, %al
	outb	%al, %dx
	incb	%cl
	jmp	.Ldbg_loop_\@
.Ldbg_done_\@:
#ifdef DEBUG_USE_MEMLOC
	movl	(DEBUG_MEMLOC + 8), %eax
	movl	(DEBUG_MEMLOC + 4), %edx
	movl	(DEBUG_MEMLOC), %ecx
#else
	popl	%eax
	popl	%edx
	popl	%ecx
#endif
.endm

.macro DEBUG64_PUTC char
#ifdef DEBUG_USE_MEMLOC
	movq	%rcx, (DEBUG_MEMLOC)
	movq	%rdx, (DEBUG_MEMLOC + 8)
	movq	%rax, (DEBUG_MEMLOC + 16)
#else
	pushq	%rcx
	pushq	%rdx
	pushq	%rax
#endif
	xorq	%rcx, %rcx
.Ldbg_loop_\@:
	cmpb	$5, %cl
	jz	.Ldbg_done_\@
	movw	$0x3f8, %dx
	addw	$5, %dx
.Ldbg_wait_\@:
	inb	%dx, %al
	testb	$0x20, %al
	jz	.Ldbg_wait_\@
	movw	$0x3f8, %dx
	movb	\char, %al
	outb	%al, %dx
	incb	%cl
	jmp	.Ldbg_loop_\@
.Ldbg_done_\@:
#ifdef DEBUG_USE_MEMLOC
	movq	(DEBUG_MEMLOC + 16), %rax
	movq	(DEBUG_MEMLOC + 8), %rdx
	movq	(DEBUG_MEMLOC), %rcx
#else
	popq	%rax
	popq	%rdx
	popq	%rcx
#endif
.endm

#endif
