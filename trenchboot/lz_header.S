#define ENTRY(name) .globl name; .align 16,0x90; name:

#define PAGE_SHIFT       12
#define PAGE_SIZE        (1 << PAGE_SHIFT)
#define PAGE_MASK        (~(PAGE_SIZE-1))

/* 64b selectors */
#define CS_SEL64         0x0008
#define DS_SEL64         0x0010

.code32

.section .text

.text 0

.global sl_lz_header
sl_lz_header:
    .word 0       /* SL header LZ offset to code start */
    .word 0xffff  /* SL header LZ total length */
    .long 0       /* Total size of TrenchBoot Intermediate Loader */
    .long 0x1000  /* Size of LZ second stage stack */
    .long 0x1000  /* Size of LZ second stage heap */
    .fill 0x14    /* MSB Key Hash */

.text 1

.global lz_first_stack
lz_first_stack:
    .fill 0x1d6, 1, 0


.text 2

.global rm_header
rm_header:
    .byte 0        /* setup_sects: The size of the setup in sectors */
    .word 0        /* root_flags: If set, the root is mounted readonly */
    .long 0        /* syssize: The size of the 32-bit code in 16-byte paras */
    .word 0        /* ram_size: DO NOT USE - for bootsect.S use only */
    .word 0        /* vid_mode: Video mode control */
    .word 0        /* root_dev: Default root device number */
    .word 0xAA55   /* boot_flag: 0xAA55 magic number */
    .word 0        /* jump: Jump instruction */
    .string "HdrS" /* header: Magic signature "HdrS" */

.text 3

ENTRY(lz_entry)
   /*
    * Per the spec:
    * EAX - Beginning of LZ containing the SL header.
    *
    * Restore the world, get back into longer mode. EBX contains the entry
    * point which is our only known location in protected mode. We will
    * use it to set things right then validate it later.
    */
    movl %eax, %ebp

    call setup
    ud2
