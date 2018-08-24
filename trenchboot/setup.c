#include <defs.h>

volatile __text int tester = 0;

void setup(void *lz_base)
{
    /*
     * TODO we are in 64b mode, paging is setup. This is the launching
     * point. We can now do what we want. First order of business is to setup
     * DEV to cover memory from LZ_SECOND_STAGE_STACK_OFFSET and load a
     * bigger stack...
     * ... When all is done, drop back to 32b protected mode, paging off and
     * trampoline to the kernel.
     */

     /*
      * TODO this is just an example of forcing a global into our .text section
      * and the resultant code using RIP relative addressing to access it. This
      * can be removed later.
      */
     tester += 10;
}
