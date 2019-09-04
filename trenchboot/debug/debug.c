void print_debug_chars_inl(void)
{
	asm volatile (  "pushq	%%rcx\n\t"
			"pushq	%%rdx\n\t"
			"pushq	%%rax\n\t"
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
			"popq	%%rax\n\t"
			"popq	%%rdx\n\t"
			"popq	%%rcx\n\t"
			: : : );
}

...
	print_debug_chars_inl();
...

void __init setup_dbregs(int cpuid, u64 l0, u64 l1)
{
#define G0 (1<<1)
#define G1 (1<<3)
#define W0 (1<<16) /* W only */
#define L0 (1<<18) /* 2b */
#define W1 (1<<20) /* W only */
#define L1 (1<<22) /* 2b */
	u64 value  = G0|G1|W0|L0|W1|L1;

	asm("movq %0, %%db0"     ::"r" (l0));
	asm("movq %0, %%db1"     ::"r" (l1));
	asm("movq %0, %%db7"     ::"r" (value));

	printk("***RJP*** setup 2 DB regs on CPU# %d\n", cpuid);
}

