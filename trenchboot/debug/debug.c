void print_debug_chars_inl(void)
{
	asm volatile (  "pushq	%%rcx\n\t"
			"pushq	%%rdx\n\t"
			"xorl	%%ecx, %%ecx\n\t"
			"1:\n\t"
			"cmpb	$5, %%cl\n\t"
			"jz	2f\n\t"
			"movw	$0x3f8, %%dx\n\t"
			"addw	$5, %%dx\n\t"
			"3:\n\t"
			"inb	%%dx, %%al\n\t"
			"testb	$0x20, %%al\n\t"
			"jz	3b\n\t"
			"movw	$0x3f8, %%dx\n\t"
			"movb	$0x46, %%al\n\t"
			"addb	%%cl, %%al\n\t"
			"outb	%%al, %%dx\n\t"
			"incb	%%cl\n\t"
			"jmp	1b\n\t"
			"2:\n\t"
			"popq	%%rdx\n\t"
			"popq	%%rcx\n\t"
			: : : );
}

...
	print_debug_chars_inl();
...
