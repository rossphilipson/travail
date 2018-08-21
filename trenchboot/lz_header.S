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

.code32

.section .text

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
    movl %eax, %ebp

    call setup
    ud2
