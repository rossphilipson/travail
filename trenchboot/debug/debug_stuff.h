#ifndef _DEBUG_PRINT_H_
#define _DEBUG_PRINT_H_

static void printd_chars(int c, int d)
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

static void printd_str(const char *s)
{
	/* Use some for of sprintf to format string */
	while (*s) {
		if (*s == '\n')
			printd_chars('\r', 1);
		printd_chars(*s, 1);
		s++;
	}
}

#ifndef NODB_HEX
static void printd_hex(unsigned long value)
{
	char alpha[2] = "0";
	int bits;

	for (bits = sizeof(value) * 8 - 4; bits >= 0; bits -= 4) {
		unsigned long digit = (value >> bits) & 0xf;

		if (digit < 0xA)
			alpha[0] = '0' + digit;
		else
			alpha[0] = 'a' + (digit - 0xA);

		printd_str(alpha);
	}
}
#endif

#ifndef NODB_BYTES
static void printd_byte(unsigned char p)
{
	char tmp[4];

	if ( (p & 0xf) >= 10 )
		tmp[1] = (p & 0xf) + 'a' - 10;
	else
		tmp[1] = (p & 0xf) + '0';
	p >>= 4;

	if ( (p & 0xf) >= 10 )
		tmp[0] = (p & 0xf) + 'a' - 10;
	else
		tmp[0] = (p & 0xf) + '0';

	tmp[2] = ' ';
	tmp[3] = '\0';
	printd_str(tmp);
}
#endif

#ifndef NODB_DUMP
static void dump_hex(const void *memory, unsigned int length)
{
	unsigned int i;
	unsigned char *line;
	int all_zero = 0;
	int all_one = 0;
	unsigned int num_bytes;

	for ( i = 0; i < length; i += 16 ) {
		unsigned int j;
		num_bytes = 16;
		line = ((unsigned char *)memory) + i;

		all_zero++;
		all_one++;

		for ( j = 0; j < num_bytes; j++ ) {
			if ( line[j] != 0 ) {
				all_zero = 0;
				break;
			}
		}

		for ( j = 0; j < num_bytes; j++ )
		{
			if ( line[j] != 0xff ) {
				all_one = 0;
				break;
			}
		}

		if ( (all_zero < 2) && (all_one < 2) ) {
			printd_hex((unsigned long)memory + i);
			printd_str(": ");
			for ( j = 0; j < num_bytes; j++ )
				printd_byte(line[j]);
			for ( ; j < 16; j++ )
				printd_str("   ");
			printd_str("  ");
			for ( j = 0; j < num_bytes; j++ )
				(line[j] >= ' ' && line[j] <= '~') ? printd_chars(line[j], 1) : printd_chars('.', 1);
			printd_str("\n");
		}
		else if ( (all_zero == 2) || (all_one == 2) )
			printd_str("...\n");
	}
}
#endif

#endif
