OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(start)
PHDRS
{
  text PT_LOAD ;
}
SECTIONS
{
  . = 0x0800000;

  .text : {
	*(.slboot_multiboot_header)
  . = ALIGN(4096);
	*(.mlept)

  _mle_start = .;               /* beginning of MLE pages */
	*(.text)
	*(.fixup)
	*(.gnu.warning)
	} :text = 0x9090

  .rodata : { *(.rodata) *(.rodata.*) }
  . = ALIGN(4096);

  _mle_end = .;                 /* end of MLE pages */

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
