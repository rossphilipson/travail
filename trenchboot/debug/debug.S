	.code32
.macro DEBUG_PUT_CHAR char
	pushl	%edx
	pushl	%eax

	movw	$0x3f8, %dx
	addw	$5, %dx
1:
	inb	%dx, %al
	testb	$0x20, %al
	jz	1b
	movw	$0x3f8, %dx
	movb	\char, %al
	outb	%al, %dx

	popl	%eax
	popl	%edx
.endm

SYM_FUNC_START(debug_put_chars)
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
	movb	$0x41, %al
	addb	%cl, %al
	outb	%al, %dx
	incb	%cl
	jmp	1b
2:
	popl	%eax
	popl	%edx
	popl	%ecx
	/*jmp	debug_back*/
	ret
SYM_FUNC_END(debug_put_chars)

...
	jmp	debug_put_chars
debug_back:
	/* or call */
...

	.code64
SYM_FUNC_START(debug_put_chars64)
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
	movb	$0x42, %al
	addb	%cl, %al
	outb	%al, %dx
	incb	%cl
	jmp	1b
2:
	popq	%rax
	popq	%rdx
	popq	%rcx
	/*jmp	debug_back64*/
	ret
SYM_FUNC_END(debug_put_chars64)
