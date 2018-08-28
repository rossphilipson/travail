/*
 * lz_header.lds
 *
 * Linker script for the i386 LZ setup code
 */
ENTRY(_start)

SECTIONS
{
	. = 0;
	.lzdata		: { *(.lzdata) }
	.text		: { *(.text) }
	.dynamic	: { *(.dynamic) }
	.got.plt	: { *(.got.plt) }
}
