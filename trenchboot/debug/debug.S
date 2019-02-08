	.code32
ENTRY(debug_put_chars)
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
	jmp	debug_back
ENDPROC(debug_put_chars)

...
	jmp	debug_put_chars
debug_back:
...

