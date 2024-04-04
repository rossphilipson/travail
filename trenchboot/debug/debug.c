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

static void print_debug_chars_inl(int c, int d)
{
	asm volatile (  "pushq	%%rcx\n\t"
			"pushq	%%rdx\n\t"
			"pushq	%%rax\n\t"
			"xorl	%%ecx, %%ecx\n\t"
			"1:\n\t"
			"cmpw	%%si, %%cx\n\t"
			"jz	2f\n\t"
			"movw	$0x3f8, %%dx\n\t"
			"addw	$5, %%dx\n\t"
			"3:\n\t"
			"inb	%%dx, %%al\n\t"
			"testb	$0x20, %%al\n\t"
			"jz	3b\n\t"
			"movw	$0x3f8, %%dx\n\t"
			"movw	%%di, %%ax\n\t"
			"addb	%%cl, %%al\n\t"
			"outb	%%al, %%dx\n\t"
			"incb	%%cl\n\t"
			"jmp	1b\n\t"
			"2:\n\t"
			"popq	%%rax\n\t"
			"popq	%%rdx\n\t"
			"popq	%%rcx\n\t"
			: : "D" (c), "S" (d) : );
}

static void print_debug_str(const char *s)
{
	/* Use some for of sprintf to format string */
	while (*s) {
		if (*s == '\n')
			print_debug_chars_inl ('\r', 1);
		print_debug_chars_inl (*s, 1);
		s++;
	}
}

static void print_debug_hex(unsigned long value)
{
	char alpha[2] = "0";
	int bits;

	for (bits = sizeof(value) * 8 - 4; bits >= 0; bits -= 4) {
		unsigned long digit = (value >> bits) & 0xf;

		if (digit < 0xA)
			alpha[0] = '0' + digit;
		else
			alpha[0] = 'a' + (digit - 0xA);

		print_debug_str(alpha);
	}
}

/* rax = 64b
 * eax = 32b
 * ax = 16b
 * al = low 8b
 * ah = hi 8b
 */
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

void dump_localities(struct tpm_chip *chip)
{
	struct tpm_tis_data *priv = dev_get_drvdata(&chip->dev);
	int i, rc;
	u8 access;
	u16 localities = 0;

	for ( i=0; i<=4; i++ ) {
		rc = tpm_tis_read8(priv, TPM_ACCESS(i), &access);
		pr_notice("***RJP*** tpm register value: %2.2x\n", access);

		if (rc < 0) {
			pr_err("***RJP*** error reading locality %i tpm register\n", i);
			continue;
		}

		if ( ! (access & TPM_ACCESS_VALID) ) {
			pr_err("***RJP*** locality %i tpm register not in valid state\n", i);
			continue;
		}

		if ( access & TPM_ACCESS_REQUEST_PENDING ) {
			if ( access & TPM_ACCESS_REQUEST_USE ) {
				pr_notice("***RJP*** tpm register pending request: %i\n", i);
				localities |= 1 << (i + 8);
			}
		}

		if ( access & TPM_ACCESS_ACTIVE_LOCALITY ) {
			pr_notice("***RJP*** tpm register active locality: %i\n", i);
			localities |= 1 << i;
		}
	}

	pr_notice("***RJP*** locality state: %x\n", localities);
}

print_hex_dump(KERN_WARNING, "EVTLOG: ", DUMP_PREFIX_ADDRESS, 16, 1, (void *)event_header, 0x1000, true);

