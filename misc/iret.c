static void sl_iret_self(void)
{
	unsigned int tmp, base;

	asm volatile (
		"pushfq\n\t"
		"mov %%cs, %0\n\t"
		"pushq %q0\n\t"
		"pushq $1f\n\t"
		"iretq\n\t"
		"1:"
		: "=&r" (tmp) : : "memory");
}
