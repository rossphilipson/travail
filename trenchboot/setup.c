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
}
