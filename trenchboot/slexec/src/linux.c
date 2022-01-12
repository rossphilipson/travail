/*
 * linux.c: support functions for manipulating Linux kernel binaries
 *
 * Copyright (c) 2006-2010, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of the Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <types.h>
#include <slexec.h>
#include <stdbool.h>
#include <printk.h>
#include <string.h>
#include <loader.h>
#include <e820.h>
#include <linux.h>
#include <cmdline.h>
#include <misc.h>
#include <processor.h>
#include <skl.h>

extern loader_ctx *g_ldr_ctx;

static boot_params_t *boot_params;

il_kernel_setup_t g_sl_kernel_setup = {0};

static void
printk_long(const char *what)
{
    /* chunk the command line into 70 byte chunks */
#define CHUNK_SIZE 70
    int      cmdlen = sl_strlen(what);
    const char    *cptr = what;
    char     cmdchunk[CHUNK_SIZE+1];
    while (cmdlen > 0) {
        sl_strncpy(cmdchunk, cptr, CHUNK_SIZE);
        cmdchunk[CHUNK_SIZE] = 0;
        printk(SLEXEC_INFO"\t%s\n", cmdchunk);
        cmdlen -= CHUNK_SIZE;
        cptr += CHUNK_SIZE;
    }
}

/* expand linux kernel with kernel image and initrd image */
bool expand_linux_image(const void *linux_image, size_t linux_size,
                        const void *initrd_image, size_t initrd_size)
{
    linux_kernel_header_t *hdr;
    linux_kernel_header_t temp_hdr;
    uint32_t real_mode_base, protected_mode_base;
    unsigned long real_mode_size, protected_mode_size;
        /* Note: real_mode_size + protected_mode_size = linux_size */
    uint32_t initrd_base;
    int vid_mode = 0;

    /* Check param */
    if ( linux_image == NULL ) {
        printk(SLEXEC_ERR"Error: Linux kernel image is zero.\n");
        return false;
    }

    if ( linux_size == 0 ) {
        printk(SLEXEC_ERR"Error: Linux kernel size is zero.\n");
        return false;
    }

    if ( linux_size < sizeof(linux_kernel_header_t) ) {
        printk(SLEXEC_ERR"Error: Linux kernel size is too small.\n");
        return false;
    }

    hdr = (linux_kernel_header_t *)(linux_image + KERNEL_HEADER_OFFSET);

    if ( hdr == NULL ) {
        printk(SLEXEC_ERR"Error: Linux kernel header is zero.\n");
        return false;
    }

    /* recommended layout
        0x0000 - 0x7FFF     Real mode kernel
        0x8000 - 0x8CFF     Stack and heap
        0x8D00 - 0x90FF     Kernel command line
        for details, see linux.h
    */

    /* if setup_sects is zero, set to default value 4 */
    if ( hdr->setup_sects == 0 )
        hdr->setup_sects = DEFAULT_SECTOR_NUM;
    if ( hdr->setup_sects > MAX_SECTOR_NUM ) {
        printk(SLEXEC_ERR
               "Error: Linux setup sectors %d exceed maximum limitation 64.\n",
                hdr->setup_sects);
        return false;
    }

    /* set vid_mode */
    linux_parse_cmdline(get_cmdline(g_ldr_ctx));
    if ( get_linux_vga(&vid_mode) )
        hdr->vid_mode = vid_mode;

    /* compare to the magic number */
    if ( hdr->header != HDRS_MAGIC ) {
        /* old kernel */
        printk(SLEXEC_ERR
               "Error: Old kernel (< 2.6.20) is not supported by slexec.\n");
        return false;
    }

    if ( hdr->version < 0x0205 ) {
        printk(SLEXEC_ERR
               "Error: Old kernel (<2.6.20) is not supported by slboot.\n");
        return false;
    }

    if ( !(hdr->loadflags & FLAG_LOAD_HIGH) ) {
        printk(SLEXEC_ERR
               "Error: Secure Launch kernel must have the FLAG_LOAD_HIGH loadflag set.\n");
        return false;
    }

    /* boot loader is grub, set type_of_loader to 0x71 */
    hdr->type_of_loader = LOADER_TYPE_GRUB;

    /* set loadflags and heap_end_ptr */
    hdr->loadflags |= FLAG_CAN_USE_HEAP;         /* can use heap */
    hdr->heap_end_ptr = KERNEL_CMDLINE_OFFSET - BOOT_SECTOR_OFFSET;

    if ( initrd_size > 0 ) {
        /* load initrd and set ramdisk_image and ramdisk_size */
        /* The initrd should typically be located as high in memory as
           possible, as it may otherwise get overwritten by the early
           kernel initialization sequence. */

        /* check if Linux command line explicitly specified a memory limit */
        uint64_t mem_limit;
        get_linux_mem(&mem_limit);
        if ( mem_limit > 0x100000000ULL || mem_limit == 0 )
            mem_limit = 0x100000000ULL;

        uint64_t max_ram_base, max_ram_size;
        get_highest_sized_ram(initrd_size, mem_limit,
                              &max_ram_base, &max_ram_size);
        if ( max_ram_size == 0 ) {
            printk(SLEXEC_ERR"not enough RAM for initrd\n");
            return false;
        }
        if ( initrd_size > max_ram_size ) {
            printk(SLEXEC_ERR"initrd_size is too large\n");
            return false;
        }
        if ( max_ram_base > ((uint64_t)(uint32_t)(~0)) ) {
            printk(SLEXEC_ERR"max_ram_base is too high\n");
            return false;
        }
        if ( plus_overflow_u32((uint32_t)max_ram_base,
                 (uint32_t)(max_ram_size - initrd_size)) ) {
            printk(SLEXEC_ERR"max_ram overflows\n");
            return false;
        }
        /*initrd_base = (max_ram_base + max_ram_size - initrd_size) & PAGE_MASK;*/

        /*
         * TODO used fixed allocation:
         * The location, initrd_base, determined above in the commented out
         * code causes memory corruption. This value leads to the corruption
         * of EFI System Resource Table (ESRT), which is also located in RAM.
         */
        initrd_base = SLEXEC_FIXED_INITRD_BASE;

        /* should not exceed initrd_addr_max */
        if ( initrd_base + initrd_size > hdr->initrd_addr_max ) {
            if ( hdr->initrd_addr_max < initrd_size ) {
                printk(SLEXEC_ERR"initrd_addr_max is too small\n");
                return false;
            }
            initrd_base = hdr->initrd_addr_max - initrd_size;
            initrd_base = initrd_base & PAGE_MASK;
        }

        /* check for overlap with a kernel image placed high in memory */
        if( (initrd_base < ((uint32_t)linux_image + linux_size))
            && ((uint32_t)linux_image < (initrd_base+initrd_size)) ){
            /* set the starting address just below the image */
            initrd_base = (uint32_t)linux_image - initrd_size;
            initrd_base = initrd_base & PAGE_MASK;
            /* make sure we're still in usable RAM and above slexec end address*/
            if( initrd_base < max_ram_base ){
                printk(SLEXEC_ERR"no available memory for initrd\n");
                return false;
            }
        }

        sk_memmove((void *)initrd_base, initrd_image, initrd_size);
        printk(SLEXEC_ERR"Initrd from 0x%lx to 0x%lx\n",
               (unsigned long)initrd_base,
               (unsigned long)(initrd_base + initrd_size));

        hdr->ramdisk_image = initrd_base;
        hdr->ramdisk_size = initrd_size;
    }
    else {
        hdr->ramdisk_image = 0;
        hdr->ramdisk_size = 0;
    }

    /* calc location of real mode part */
    real_mode_base = LEGACY_REAL_START;
    if ( have_loader_memlimits(g_ldr_ctx))
        real_mode_base =
            ((get_loader_mem_lower(g_ldr_ctx)) << 10) - REAL_MODE_SIZE;
    if ( real_mode_base < SLEXEC_E820_COPY_ADDR + SLEXEC_E820_COPY_SIZE )
        real_mode_base = SLEXEC_E820_COPY_ADDR + SLEXEC_E820_COPY_SIZE;
    if ( real_mode_base > LEGACY_REAL_START )
        real_mode_base = LEGACY_REAL_START;

    real_mode_size = (hdr->setup_sects + 1) * SECTOR_SIZE;
    if ( real_mode_size > KERNEL_CMDLINE_OFFSET ) {
        printk(SLEXEC_ERR"realmode data is too large\n");
        return false;
    }

    /* calc location of protected mode part */
    protected_mode_size = linux_size - real_mode_size;

    /* if kernel is relocatable then move it above slexec */
    /* else it may expand over top of slexec */
    /* NOTE the SL kernel is not relocatable and should be loaded at the
     * default location */
    if ( hdr->relocatable_kernel ) {
        protected_mode_base = (uint32_t)get_slexec_mem_end();
        /* fix possible mbi overwrite in grub2 case */
        /* assuming grub2 only used for relocatable kernel */
        /* assuming mbi & components are contiguous */
        unsigned long ldr_ctx_end = get_loader_ctx_end(g_ldr_ctx);
        if ( ldr_ctx_end > protected_mode_base )
            protected_mode_base = ldr_ctx_end;
        /* overflow? */
        if ( plus_overflow_u32(protected_mode_base,
                 hdr->kernel_alignment - 1) ) {
            printk(SLEXEC_ERR"protected_mode_base overflows\n");
            return false;
        }
        /* round it up to kernel alignment */
        protected_mode_base = (protected_mode_base + hdr->kernel_alignment - 1)
                              & ~(hdr->kernel_alignment-1);
        hdr->code32_start = protected_mode_base;
    }
    else if ( hdr->loadflags & FLAG_LOAD_HIGH ) {
        protected_mode_base = BZIMAGE_PROTECTED_START;
                /* bzImage:0x100000 */
        /* overflow? */
        if ( plus_overflow_u32(protected_mode_base, protected_mode_size) ) {
            printk(SLEXEC_ERR
                   "protected_mode_base plus protected_mode_size overflows\n");
            return false;
        }
        /* Check: protected mode part cannot exceed mem_upper */
        if ( have_loader_memlimits(g_ldr_ctx)){
            uint32_t mem_upper = get_loader_mem_upper(g_ldr_ctx);
            if ( (protected_mode_base + protected_mode_size)
                    > ((mem_upper << 10) + 0x100000) ) {
                printk(SLEXEC_ERR
                       "Error: Linux protected mode part (0x%lx ~ 0x%lx) "
                       "exceeds mem_upper (0x%lx ~ 0x%lx).\n",
                       (unsigned long)protected_mode_base,
                       (unsigned long)
                       (protected_mode_base + protected_mode_size),
                       (unsigned long)0x100000,
                       (unsigned long)((mem_upper << 10) + 0x100000));
                return false;
            }
        }
    }
    else {
        printk(SLEXEC_ERR"Error: Linux protected mode not loaded high\n");
        return false;
    }

    /* save linux header struct to temp memory to copy changes to zero page */
    sk_memmove(&temp_hdr, hdr, sizeof(temp_hdr));

    /* load real-mode part */
    sk_memmove((void *)real_mode_base, linux_image, real_mode_size);
    printk(SLEXEC_ERR"Kernel (real mode) from 0x%lx to 0x%lx size: 0x%lx\n",
           (unsigned long)linux_image,
           (unsigned long)real_mode_base,
           (unsigned long)real_mode_size);

    /* load protected-mode part */
    sk_memmove((void *)protected_mode_base, linux_image + real_mode_size,
            protected_mode_size);
    printk(SLEXEC_ERR"Kernel (protected mode) from 0x%lx to 0x%lx size: 0x%lx\n",
           (unsigned long)(linux_image + real_mode_size),
           (unsigned long)protected_mode_base,
           (unsigned long)protected_mode_size);

    /* reset pointers to point into zero page at real mode base */
    hdr = (linux_kernel_header_t *)(real_mode_base + KERNEL_HEADER_OFFSET);

    /* copy back the updated kernel header */
    sk_memmove(hdr, &temp_hdr, sizeof(temp_hdr));

    /* set cmd_line_ptr */
    hdr->cmd_line_ptr = real_mode_base + KERNEL_CMDLINE_OFFSET;

    /* copy cmdline */
    const char *kernel_cmdline = get_cmdline(g_ldr_ctx);
    if ( kernel_cmdline == NULL ) {
        printk(SLEXEC_ERR"Error: kernel cmdline not available\n");
        return false;
    }
    const size_t kernel_cmdline_size = REAL_END_OFFSET - KERNEL_CMDLINE_OFFSET;
    size_t kernel_cmdline_strlen = sl_strlen(kernel_cmdline);
    if (kernel_cmdline_strlen > kernel_cmdline_size - 1)
        kernel_cmdline_strlen = kernel_cmdline_size - 1;
    sk_memset((void *)hdr->cmd_line_ptr, 0, kernel_cmdline_size);
    sl_memcpy((void *)hdr->cmd_line_ptr, kernel_cmdline, kernel_cmdline_strlen);

    printk(SLEXEC_INFO"Linux cmdline from 0x%lx to 0x%lx:\n",
           (unsigned long)hdr->cmd_line_ptr,
           (unsigned long)(hdr->cmd_line_ptr + kernel_cmdline_size));
    printk_long((void *)hdr->cmd_line_ptr);

    /* need to put boot_params in real mode area so it gets mapped */
    boot_params = (boot_params_t *)real_mode_base;

    /* need to handle a few EFI things here if such is our parentage */
    if (is_loader_launch_efi(g_ldr_ctx)){
        struct efi_info *efi = (struct efi_info *)(boot_params->efi_info);
        struct screen_info_t *scr =
            (struct screen_info_t *)(boot_params->screen_info);
        uint32_t address = 0;
        uint64_t long_address = 0UL;
        uint32_t descr_size = 0, descr_vers = 0, mmap_size = 0, efi_mmap_addr = 0;

        /* loader signature */
        sl_memcpy(&efi->efi_ldr_sig, "EL64", sizeof(uint32_t));

        /* EFI system table addr */
        {
            if (get_loader_efi_ptr(g_ldr_ctx, &address, &long_address)){
                if (long_address){
                    efi->efi_systable = (uint32_t) (long_address & 0xffffffff);
                    efi->efi_systable_hi = long_address >> 32;
                } else {
                    efi->efi_systable = address;
                    efi->efi_systable_hi = 0;
                }
            } else {
                printk(SLEXEC_INFO"failed to get efi system table ptr\n");
            }
        }

        efi_mmap_addr = find_efi_memmap(g_ldr_ctx, &descr_size,
                                        &descr_vers, &mmap_size);
        if (!efi_mmap_addr) {
            printk(SLEXEC_INFO"failed to get EFI memory map\n");
            efi->efi_memdescr_size = 0x1; // Avoid div by 0 in kernel.
            efi->efi_memmap_size = 0;
            efi->efi_memmap = 0;
        } else {
            efi->efi_memdescr_size = descr_size;
            efi->efi_memdescr_ver = descr_vers;
            efi->efi_memmap_size = mmap_size;
            efi->efi_memmap = efi_mmap_addr;
            /* From Multiboot2 spec:
             * The bootloader must not load any part of the kernel, the modules,
             * the Multiboot2 information structure, etc. higher than 4 GiB - 1.
             */
            efi->efi_memmap_hi = 0;

            printk(SLEXEC_INFO "EFI memmap: memmap base: 0x%x, memmap size: 0x%x\n",
                  efi->efi_memmap, efi->efi_memmap_size);
            printk(SLEXEC_INFO "EFI memmap: descr size: 0x%x, descr version: 0x%x\n",
                  efi->efi_memdescr_size, efi->efi_memdescr_ver);
         }

        /* if we're here, GRUB2 probably threw a framebuffer tag at us */
        load_framebuffer_info(g_ldr_ctx, (void *)scr);
    }

    /* detect e820 table */
    if (have_loader_memmap(g_ldr_ctx)) {
        int i;

        memory_map_t *p = get_loader_memmap(g_ldr_ctx);
        if ( p == NULL ) {
            printk(SLEXEC_ERR"Error: no memory map available\n");
            return false;
        }
        uint32_t memmap_start = (uint32_t) p;
        uint32_t memmap_length = get_loader_memmap_length(g_ldr_ctx);
        for ( i = 0; (uint32_t)p < memmap_start + memmap_length; i++ )
        {
            boot_params->e820_map[i].addr = ((uint64_t)p->base_addr_high << 32)
                                            | (uint64_t)p->base_addr_low;
            boot_params->e820_map[i].size = ((uint64_t)p->length_high << 32)
                                            | (uint64_t)p->length_low;
            boot_params->e820_map[i].type = p->type;
            p = (void *)p + sizeof(memory_map_t);
        }
        boot_params->e820_entries = i;
    }

    if (0 == is_loader_launch_efi(g_ldr_ctx)){
        screen_info_t *screen = (screen_info_t *)&boot_params->screen_info;
        screen->orig_video_mode = 3;       /* BIOS 80*25 text mode */
        screen->orig_video_lines = 25;
        screen->orig_video_cols = 80;
        screen->orig_video_points = 16;    /* set font height to 16 pixels */
        screen->orig_video_isVGA = 1;      /* use VGA text screen setups */
        screen->orig_y = 24;               /* start display text @ screen end*/
    }

    /* Clear out some boot_params we don't want dangling around */
    sk_memset((void *)boot_params->slexec_shared_addr, 0, 8);
    sk_memset((void *)boot_params->acpi_rsdp_addr, 0, 8);

    /* Copy all the handoff information about the loaded IL kernel */
    g_sl_kernel_setup.real_mode_base = real_mode_base;
    g_sl_kernel_setup.real_mode_size = real_mode_size;
    g_sl_kernel_setup.protected_mode_base = protected_mode_base;
    g_sl_kernel_setup.protected_mode_size = protected_mode_size;
    g_sl_kernel_setup.boot_params = boot_params;

    printk(SLEXEC_ERR"Intermediate Loader kernel details:\n");
    printk(SLEXEC_ERR"\treal_mode_base: 0x%x\n", real_mode_base);
    printk(SLEXEC_ERR"\treal_mode_size: 0x%lx\n", real_mode_size);
    printk(SLEXEC_ERR"\tprotected_mode_base: 0x%x\n", protected_mode_base);
    printk(SLEXEC_ERR"\tprotected_mode_size: 0x%lx\n", protected_mode_size);
    printk(SLEXEC_ERR"\tboot_params: 0x%p\n", boot_params);

    return true;
}

void linux_skl_setup_indirect(setup_data_t *data, uint32_t adj_size)
{
    setup_indirect_t *ind =
        (setup_indirect_t *)((u8 *)data + sizeof(setup_data_t));

    /* Setup the node */
    data->type = SETUP_INDIRECT;
    data->len = sizeof(setup_data_t) + sizeof(setup_indirect_t);
    ind->type = (SETUP_INDIRECT | SETUP_SECURE_LAUNCH);
    ind->reserved = 0;
    ind->len = adj_size;
    ind->addr = (uint64_t)(uint32_t)g_skl_module;

    printk(SLEXEC_INFO"SKL indirect tag setup - addr: %p size: 0x%x\n",
           g_skl_module, adj_size);

    /* Chain it into the setup_data list, stick it up front */
    data->next = g_sl_kernel_setup.boot_params->hdr.setup_data;
    g_sl_kernel_setup.boot_params->hdr.setup_data = (uint64_t)(uint32_t)data;
}

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
