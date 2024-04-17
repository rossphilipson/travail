#ifndef _DEBUG_ASM
#define _DEBUG_ASM

.macro DEBUG_PUTC char
	pushl	%ecx
	pushl	%edx
	pushl	%eax
	xorl	%ecx, %ecx
1:
	cmpb	$5, %cl
	jz	2f
	movw	$0x3f8, %dx
	addw	$5, %dx
3:
	inb	%dx, %al
	testb	$0x20, %al
	jz	3b
	movw	$0x3f8, %dx
	movb	\char, %al
	outb	%al, %dx
	incb	%cl
	jmp	1b
2:
	popl	%eax
	popl	%edx
	popl	%ecx
.endm

.macro DEBUG64_PUTC char
	pushq	%rcx
	pushq	%rdx
	pushq	%rax
	xorq	%rcx, %rcx
1:
	cmpb	$5, %cl
	jz	2f
	movw	$0x3f8, %dx
	addw	$5, %dx
3:
	inb	%dx, %al
	testb	$0x20, %al
	jz	3b
	movw	$0x3f8, %dx
	movb	\char, %al
	outb	%al, %dx
	incb	%cl
	jmp	1b
2:
	popq	%rax
	popq	%rdx
	popq	%rcx
.endm

#endif
