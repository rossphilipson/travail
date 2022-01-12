OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(start)
PHDRS
{
  text PT_LOAD ;
}
SECTIONS
{
  . = 0x02C00000;

  .text : {
	*(.skexec_multiboot_header)
	*(.text)
	*(.fixup)
	*(.gnu.warning)
	} :text = 0x9090

  .rodata : { *(.rodata) *(.rodata.*) }
  . = ALIGN(4096);

  .data : {			/* Data */
	*(.data)
	CONSTRUCTORS
	}

  . = ALIGN(4096);

  __bss_start = .;		/* BSS */
  .bss : {
	*(.bss.stack_aligned)
	*(.bss.page_aligned)
	*(.bss)
	}

  _end = . ;
}
