#include <defs.h>
#include <types.h>
#include <config.h>
#include <boot.h>
#include <pci.h>
#include <dev.h>
#include <sha1sum.h>

static __text void *lz_base;
static __text lz_header_t *lz_header;
static __text void *zero_page;
static __text SHA1_CONTEXT sha1ctx;

void setup2(void);

void setup(void *_lz_base)
{
    void *dev_table;
    void **second_stack;
    u64 pfn, end_pfn;
    u32 dev;

    /*
     * Now in 64b mode, paging is setup. This is the launching point. We can
     * now do what we want. First order of business is to setup
     * DEV to cover memory from the start of bzImage to the end of the LZ "kernel".
     * At the end, trampoline to the PM entry point which will include the
     * TrenchBoot stub.
     */

    /* Store the lz_base for all to use */
    lz_base = _lz_base;

    /* The LZ header setup by the bootloader */
    lz_header = (lz_header_t*)((u8*)lz_base + sizeof(sl_header_t));

    /* The Zero Page with the boot_params and legacy header */
    zero_page = (u8*)(u64)lz_header->zero_page_addr;

    /* DEV CODE */

    /* Pointer to dev_table bitmap for DEV protection */
    dev_table = (u8*)lz_base + LZ_DEV_TABLE_OFFSET;

    pfn = PAGE_PFN(zero_page);
    end_pfn = PAGE_PFN(PAGE_DOWN((u8*)lz_base + 0x10000));

    /* TODO: check end_pfn is not ouside of range of DEV map */

    /* build protection bitmap */
    for (;pfn++; pfn <= end_pfn)
        dev_protect_page(pfn, (u8*)dev_table);

    dev = dev_locate();
    dev_load_map(dev, (u32)((u64)dev_table));
    dev_flush_cache(dev);

    /*
     * Switch to our nice big stack which starts at the page behind the
     * landing zone and of course grows down.
     */
    second_stack = lz_base;
    load_stack(*second_stack);

    /* Call secondary setup on new stack */
    setup2();

    /* Should never get here */
    die();
}

void setup2(void)
{
    void *tl_image_base;
    u32 *code32_start;
    void *pm_kernel_entry;

    /* Do the SHA1 of the Trenchboot Loader image */
    tl_image_base = (u8*)lz_base - LZ_SECOND_STAGE_STACK_SIZE -
                    PAGE_UP(lz_header->trenchboot_loader_size);
    sha1sum(&sha1ctx, tl_image_base, lz_header->trenchboot_loader_size);

    /* TODO extend TPM PCRs */

    code32_start = (u32*)((u8*)zero_page + BP_CODE32_START);
    pm_kernel_entry = (void*)((u64)(*code32_start));

    /* End of the line, off to the protected mode entry into the kernel */
    lz_exit(pm_kernel_entry, zero_page, lz_base);

    /* Should never get here */
    die();
}