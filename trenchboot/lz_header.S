#include <defs.h>
#include <config.h>

/* 64b selectors */
#define CS_SEL64         0x0008
#define DS_SEL64         0x0010

.section .sl_lz_header, "w"

.global sl_lz_header
sl_lz_header:
    .word 0      /* SL header LZ offset to code start */
    .word 0xffff /* SL header LZ total length */
    .long 0      /* Total size of TrenchBoot Intermediate Loader */
    .fill 0x14   /* MSB Key Hash */

.section .lz_first_stack, "w"

.global lz_first_stack
lz_first_stack:
    .fill 0x1cc, 1, 0

.section .rm_header, "w"

.global rm_header
rm_header:
    .byte 0xff       /* sentinal: byte 2 of sentinal preceding RMH at 0x1f1 */
    .byte 0          /* setup_sects: The size of the setup in sectors */
    .word 0          /* root_flags: If set, the root is mounted readonly */
    .long 0          /* syssize: The size of the 32-bit code in 16-byte paras */
    .word 0          /* ram_size: DO NOT USE - for bootsect.S use only */
    .word 0          /* vid_mode: Video mode control */
    .word 0          /* root_dev: Default root device number */
    .word 0xAA55     /* boot_flag: 0xAA55 magic number */
/* TODO do we care about the extended header field beyond 512 bytes? */
    /*.word 0           jump: Jump instruction */
    /*.long 0x53726448  header: Magic signature "HdrS" */
    /*.word 0x020d      version: Boot protocol version supported */
    /* ... */

.code32

.section .text

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
