#include <defs.h>
#include <config.h>
#include <types.h>
#include <boot.h>

__text u8 *pm_kernel_entry = NULL;

extern __text u8 *dev_table;

void setup(void *lz_base)
{
    /*
     * Now in 64b mode, paging is setup. This is the launching point. We can
     * now do what we want. First order of business is to setup
     * DEV to cover memory from the start of bzImage to the end of the LZ "kernel".
     * At the end, trampoline to the PM entry point which will include the
     * TrenchBoot stub.
     */

     /* Setup pointer to global dev_table bitmap for DEV protection */
     dev_table = (u8*)lz_base + LZ_DEV_TABLE_OFFSET;

     /* TODO rest */

     /* End of the line, off to the protected mode entry into the kernel */
     lz_exit(pm_kernel_entry, lz_base);

     /* Should never get here */
     die();
}
