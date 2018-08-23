#include <defs.h>
#include <config.h>

/* 64b selectors */
#define CS_SEL64         0x0008
#define DS_SEL64         0x0010

.section .bsdata, "aw"

.global bootsect_start
bootsect_start:

.global sl_header
sl_header:
    .word 0      /* SL header LZ offset to code start */
    .word 0xffff /* SL header LZ total length */

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
setup_sects: .byte 0          /* The size of the setup in sectors, Filled in by build.c */
root_flags:  .word 0          /* If set, the root is mounted readonly */
syssize:     .long 0          /* The size of the 32-bit code in 16-byte paras. Filled in by build.c */
ram_size:    .word 0          /* DO NOT USE - for bootsect.S use only */
vid_mode:    .word 0          /* Video mode control */
root_dev:    .word 0          /* Default root device number. Filled in by build.c */
boot_flag:   .word 0xAA55     /* 0xAA55 magic number */

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
    .word  0x020d  /* version: Boot protocol version supported (>= 0x0105)
                      or else old loadlin-1.5 will fail) */
.globl realmode_swtch
    .word 0, 0     /* realmode_switch: hdefault_switch, SETUPSEG */
    .word 0x1000   /* start_sys_seg: gobsolete and meaningless, but just
                      in case something decided to "use" it */
    .word 0        /* kernel_version: set to NULL which is valid */
    .byte 0x0f     /* type_of_loader: 0xf Special LZ loader */
    .byte 0        /* loadflags: TODO none of the flags really mean much */
    .word 0        /* setup_move_size: TODO not sure what to do with this */
    .long 0        /* code32_start: TODO not sure what to do with this */
    .long 0        /* ramdisk_image: built in ramdisk, this is not needed */
    .long 0        /* ramdisk_size: built in ramdisk, this is not needed */
    .long 0        /* bootsect_kludge: obsolete */
    .word 0        /* heap_end_ptr: flag not set, not using this */
    .byte 0        /* ext_loader_ver: Extended boot loader version */
    .byte 0        /* ext_loader_type: Extended boot loader type */
    .long 0        /* cmd_line_ptr: Pointer to kernel cmdline setup by loader */
    .long 0x7fffffff /* initrd_addr_max: TODO use Linux default? */
    .long 0x100000 /* kernel_alignment: TODO set to default 16M? */
    .byte 0        /* relocatable_kernel: TODO don't want it relocated? */

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

    /* Third page is the PD that map 1G as 2M pages */
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

    /* Restore CR4, PAE must be enabled before IA-32e mode */
    movl %cr4, %ecx
    orl $(CR4_PAE | CR4_PGE), %ecx
    movl %ecx, %cr4

    /* Load PML4 table location into PT base register */
    movl %ebx, %eax
    addl $LZ_PAGE_TABLES_OFFSET, %eax
    movl %eax, %cr3

    /* Enable IA-32e mode and paging */
    movl $MSR_EFER, %ecx
    rdmsr
    orl $(1 << EFER_LME), %eax
    wrmsr
    movl %cr0, %eax
    orl $(CR0_PG | CR0_NE | CR0_ET | CR0_MP | CR0_PE), %eax
    movl %eax, %cr0
    jmp 1f
1:

    /* Now in IA-32e compatibility mode */

    /* Setup GDT and ljmp to 64b mode */
    movl %esi, %ebx
    addl $(gdt_desc64 - _entry), %ebx
    lgdt (%ebx)

jump64:
    .byte 0xea       /* far jmp op */
    .long 0x00000000 /* offset (fixed up) */
    .word CS_SEL64   /* 64b code segment selector */

.code64

entry64:
    /* Load data segment regs */
    movw $DS_SEL64, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* ESI still has original EBX, put it back */
    xorq %rbx, %rbx
    movl %esi, %ebx

    /* Load the stage 1 stack */
    movq %rbx, %rax
    addq $LZ_FIRST_STAGE_STACK_START, %rax
    movq %rax, %rsp

    /* Pass the base of the LZ to the setup code */
    movq %rbx, %rcx

    /* End of the line, we should never return */
    callq setup
    ud2

.globl print_char
print_char:
    /* Args ECX, Clobbers EDX, EAX */
    xorq %rdx, %rdx
    movw $0x3f8, %dx
    addw $5, %dx
1:
    inb %dx, %al
    testb $0x20, %al
    jz 1b
    movw $0x3f8, %dx
    movb %cl, %al
    outb %al, %dx
    retq

.globl load_stack
load_stack:
    movq %rcx, %rsp
    retq

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

