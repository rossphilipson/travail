/*
 * lz_header.lds
 *
 * Linker script for the i386 LZ setup code
 */
ENTRY(_start)

SECTIONS
{
	. = 0;
	.bsdata		: { *(.bsdata) }

	. = 495;
	.header		: { *(.header) }
	__end_init = .;

	.text		: { *(.text) }

	. = ALIGN(16);
	.rodata		: { *(.rodata*) }

	. = ALIGN(16);
	.data		: { *(.data*) }

	.signature	: {
		setup_sig = .;
		LONG(0x5a5aaa55)
	}

	. = ALIGN(16);
	.bss		:
	{
		__bss_start = .;
		*(.bss)
		__bss_end = .;
	}
	. = ALIGN(16);
	_end = .;

	/DISCARD/ : { *(.note*) }

	/*
	 * The ASSERT() sink to . is intentional, for binutils 2.14 compatibility:
	 */
	. = ASSERT(_end <= 0x8000, "Setup too big!");
	. = ASSERT(hdr == 0x1f1, "The setup header has the wrong offset!");
	/* Necessary for the very-old-loader check to work... */
	. = ASSERT(__end_init <= 5*512, "init sections too big!");
}
