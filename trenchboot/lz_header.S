#include <defs.h>
#include <config.h>

/* 64b selectors */
#define CS_SEL64         0x0008
#define DS_SEL64         0x0010

.section .bstext, "ax"

.global bootsect_start
bootsect_start:

.global sl_header
sl_header:
    .word 0      /* SL header LZ offset to code start */
    .word 0xffff /* SL header LZ total length */

.section .bsdata, "aw"

.global lz_header
lz_header:
    .long 0      /* Total size of TrenchBoot Intermediate Loader */
    .fill 0x14   /* MSB Key Hash */

.global lz_first_stack
lz_first_stack:
    .fill LZ_FIRST_STAGE_STACK_SIZE, 1, 0

.section .header, "a"
.globl	sentinel
sentinel:
    .byte 0xff, 0xff /* Used to detect broken loaders */

.global hdr
hdr:
setup_sects: .byte 0          /* setup_sects: The size of the setup in sectors */
root_flags:  .word 0          /* root_flags: If set, the root is mounted readonly */
syssize:     .long 0          /* syssize: The size of the 32-bit code in 16-byte paras */
ram_size:    .word 0          /* ram_size: DO NOT USE - for bootsect.S use only */
vid_mode:    .word 0          /* vid_mode: Video mode control */
root_dev:    .word 0          /* root_dev: Default root device number */
boot_flag:   .word 0xAA55     /* boot_flag: 0xAA55 magic number */

/* Offset 512, entry point */

.globl _start
_start:

    /*
     * Explicitly enter this as bytes, or the assembler
     * tries to generate a 3-byte jump here, which causes
     * everything else to push off to the wrong offset.
     * short (2-byte) jump
     */
    .byte 0xeb
    .byte _entry-1f
1:

    /* Part 2 of the header, from the old setup.S */

    .ascii "HdrS"  /* header: Magic signature "HdrS" */
    .word  0x020d  /* version: Boot protocol version supported (>= 0x0105) */
					# or else old loadlin-1.5 will fail)
.globl realmode_swtch
realmode_swtch:
    .word 0, 0     /* default_switch, SETUPSEG */

/*
 * TODO need the rest of the extended header field beyond 512 bytes. They
 * have been cropped out for now.
 */

/* End of setup header ##################################################### */

.section .text

.code32

.globl _entry
_entry:
   /*
    * Per the spec:
    * EAX - Beginning of LZ containing the SL header.
    *
    * Restore the world, get back into longer mode. EBX contains the entry
    * point which is our only known location in protected mode. We will
    * use it to set things right then validate it later.
    */
    movl %eax, %ebx

    /* TODO check we are below 4G? */

    /* Clear R_INIT and DIS_A20M.  */
    movl $(VM_CR_MSR), %ecx
    rdmsr
    andl $(~(1 << VM_CR_R_INIT)), %eax
    andl $(~(1 << VM_CR_DIS_A20M)), %eax
    wrmsr

    /* Fixup some addresses for the GDT and long jump */
    movl %ebx, %ecx
    addl $(gdt_desc64 - _entry + 2), %ecx
    movl %ebx, %eax
    addl $(gdt_table64 - _entry), %eax
    movl %eax, (%ecx)

    movl %ebx, %ecx
    addl $(jump64 - _entry + 1), %ecx
    movl %ebx, %eax
    addl $(entry64 - _entry), %eax
    movl %eax, (%ecx)

    /* First page is the PML4 table with one PDP entry */
    movl %ebx, %eax
    addl $(LZ_PAGE_TABLES_OFFSET), %eax
    movl %eax, %ecx
    addl $PAGE_SIZE, %ecx
    orl $0x3, %ecx
    movl %ecx, (%eax)

    /*
     * Second page is the PDP table with 1 PD entry. Need to figure out
     * which 1G range the LZ is in to determine what range to identity map.
     */
    addl $PAGE_SIZE, %eax
    movl %eax, %ecx
    addl $PAGE_SIZE, %ecx
    orl $0x3, %ecx
    xorl %edx, %edx
    movl $GIGABYTE, %edi
1:
    cmpl %ebx, %edi
    jl 2f
    addl $GIGABYTE, %edi
    addl $8, %eax
    jmp 1b
2:
    movl %ecx, (%eax)

    /* Next page is the PDs that map 1G as 2M pages */
    movl %ebx, %eax
    addl $(LZ_PAGE_TABLES_OFFSET + 2*PAGE_SIZE), %eax
    xorl %edx, %edx
    xorl %ecx, %ecx
    addl $0x83, %ecx
1:
    cmpw $512, %dx
    jz 2f
    movl %ecx, (%eax)
    addl $0x8, %eax
    addl $0x200000, %ecx
    incw %dx
    jmp 1b
2:
    /* Done setting up page tables */

jump64:
    .byte 0xea       /* far jmp op */
    .long 0x00000000 /* offset (fixed up) */
    .word CS_SEL64   /* 64b code segment selector */

.code64

entry64:

    /* End of the line, we should never return */
    callq setup
    ud2

/* GDT */
	.align 16
gdt_desc64:
	.word	gdt_table64_end - gdt_table64 - 1 /* Limit */
	.quad	0x0000000000000000 /* Base */
gdt_desc64_end:

	.align 16
gdt_table64:
	/* Null Segment */
	.quad	0x0000000000000000
	/* Code Segment */
	.word	0x0000 /* Limit 1 */
	.word	0x0000 /* Base 1 */
	.byte	0x00   /* Base 2 */
	.byte	0x9a   /* P=1 DPL=0 11=code C=0 R=1 A=0 */
	.byte	0x20   /* G=0 D=0 L=1 D=0 AVL=0 Limit 2 */
	.byte	0x00   /* Base 3 */
	/* Data Segment */
	.word	0x0000 /* Limit 1 */
	.word	0x0000 /* Base 1 */
	.byte	0x00   /* Base 2 */
	.byte	0x92   /* P=1 DPL=0 10=code C=0 W=1 A=0 */
	.byte	0x00   /* G=0 D=0 L=0 D=0 AVL=0 Limit 2 */
	.byte	0x00   /* Base 3 */
gdt_table64_end:

