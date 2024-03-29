/*
 * loader.c: support functions for manipulating ELF/Linux kernel
 *           binaries
 *
 * Copyright (c) 2006-2013, Intel Corporation
 * Copyright (c) 2016 Real-Time Systems GmbH
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

#include <config.h>
#include <stdbool.h>
#include <types.h>
#include <printk.h>
#include <compiler.h>
#include <string.h>
#include <misc.h>
#include <page.h>
#include <multiboot.h>
#include <uuid.h>
#include <loader.h>
#include <e820.h>
#include <slboot.h>
#include <elf_defns.h>
#include <linux_defns.h>
#include <tb_error.h>
#include <txt/txt.h>
#include <mle.h>
#include <txt/acmod.h>
#include <cmdline.h>
#include <tpm.h>

/* multiboot struct saved so that post_launch() can use it (in tboot.c) */
extern loader_ctx *g_ldr_ctx;
extern bool expand_linux_image(const void *linux_image, size_t linux_size,
                               const void *initrd_image, size_t initrd_size);
extern bool is_sinit_acmod(const void *acmod_base, uint32_t acmod_size,
                           bool quiet);
extern void error_action(tb_error_t error);
static uint32_t g_mb_orig_size;

#define LOADER_CTX_BAD(xctx) \
    xctx == NULL ? true : \
        xctx->addr == NULL ? true : \
        xctx->type != 1 && xctx->type != 2 ? true : false

#define MB_NONE 0
#define MB1_ONLY 1
#define MB2_ONLY 2
#define MB_BOTH 3

static void
printk_long(char *what)
{
    /* chunk the command line into 70 byte chunks */
#define CHUNK_SIZE 70
    int      cmdlen = tb_strlen(what);
    char    *cptr = what;
    char     cmdchunk[CHUNK_SIZE+1];
    while (cmdlen > 0) {
        tb_strncpy(cmdchunk, cptr, CHUNK_SIZE);
        cmdchunk[CHUNK_SIZE] = 0;
        printk(TBOOT_INFO"\t%s\n", cmdchunk);
        cmdlen -= CHUNK_SIZE;
        cptr += CHUNK_SIZE;
    }
}

static module_t
*get_module_mb1(const multiboot_info_t *mbi, unsigned int i)
{
    if ( mbi == NULL ) {
        printk(TBOOT_ERR"Error: mbi pointer is zero.\n");
        return NULL;
    }

    if ( i >= mbi->mods_count ) {
        printk(TBOOT_ERR"invalid module #\n");
        return NULL;
    }

    return (module_t *)(mbi->mods_addr + i * sizeof(module_t));
}

static struct mb2_tag
*next_mb2_tag(struct mb2_tag *start)
{
    /* given "start", what's the beginning of the next tag */
    void *addr = (void *) start;
    if (start == NULL)
        return NULL;
    if (start->type == MB2_TAG_TYPE_END)
        return NULL;
    addr += ((start->size + 7) & ~7);
    return (struct mb2_tag *) addr;
}

static struct mb2_tag
*find_mb2_tag_type(struct mb2_tag *start, uint32_t tag_type)
{
    while (start != NULL){
        if (start->type == tag_type)
            return start;
        start = next_mb2_tag(start);
    }
    return start;
}

static module_t
*get_module_mb2(loader_ctx *lctx, unsigned int i)
{
    struct mb2_tag *start = (struct mb2_tag *)(lctx->addr + 8);
    unsigned int ii;
    struct mb2_tag_module *tag_mod = NULL;
    module_t *mt = NULL;
    start = find_mb2_tag_type(start, MB2_TAG_TYPE_MODULE);
    if (start != NULL){
        for (ii = 1; ii <= i; ii++){
            if (start == NULL)
                return NULL;
            else {
                /* nudge off this hit */
                start = next_mb2_tag(start);
                start = find_mb2_tag_type(start, MB2_TAG_TYPE_MODULE);
            }
        }
        /* if we're here, we have the tag struct for the desired module */
        tag_mod = (struct mb2_tag_module *) start;
        mt = (module_t *) &(tag_mod->mod_start);
    }
    return mt;
}

#if 0
void print_mbi(const multiboot_info_t *mbi)
{
    /* print mbi for debug */
    unsigned int i;

    printk(TBOOT_DETA"print mbi@%p ...\n", mbi);
    printk(TBOOT_DETA"\t flags: 0x%x\n", mbi->flags);
    if ( mbi->flags & MBI_MEMLIMITS )
        printk(TBOOT_DETA"\t mem_lower: %uKB, mem_upper: %uKB\n",
               mbi->mem_lower, mbi->mem_upper);
    if ( mbi->flags & MBI_BOOTDEV ) {
        printk(TBOOT_DETA"\t boot_device.bios_driver: 0x%x\n",
               mbi->boot_device.bios_driver);
        printk(TBOOT_DETA"\t boot_device.top_level_partition: 0x%x\n",
               mbi->boot_device.top_level_partition);
        printk(TBOOT_DETA"\t boot_device.sub_partition: 0x%x\n",
               mbi->boot_device.sub_partition);
        printk(TBOOT_DETA"\t boot_device.third_partition: 0x%x\n",
               mbi->boot_device.third_partition);
    }
    if ( mbi->flags & MBI_CMDLINE ) {
# define CHUNK_SIZE 72
        /* Break the command line up into 72 byte chunks */
        int   cmdlen = tb_strlen(mbi->cmdline);
        char *cmdptr = (char *)mbi->cmdline;
        char  chunk[CHUNK_SIZE+1];
        printk(TBOOT_DETA"\t cmdline@0x%x: ", mbi->cmdline);
        chunk[CHUNK_SIZE] = '\0';
        while (cmdlen > 0) {
            tb_strncpy(chunk, cmdptr, CHUNK_SIZE);
            printk(TBOOT_DETA"\n\t\"%s\"", chunk);
            cmdptr += CHUNK_SIZE;
            cmdlen -= CHUNK_SIZE;
        }
        printk(TBOOT_DETA"\n");
    }

    if ( mbi->flags & MBI_MODULES ) {
        printk(TBOOT_DETA"\t mods_count: %u, mods_addr: 0x%x\n",
               mbi->mods_count, mbi->mods_addr);
        for ( i = 0; i < mbi->mods_count; i++ ) {
            module_t *p = (module_t *)(mbi->mods_addr + i*sizeof(module_t));
            printk(TBOOT_DETA"\t     %d : mod_start: 0x%x, mod_end: 0x%x\n", i,
                   p->mod_start, p->mod_end);
            printk(TBOOT_DETA"\t         string (@0x%x): \"%s\"\n", p->string,
                   (char *)p->string);
        }
    }
    if ( mbi->flags & MBI_AOUT ) {
        const aout_t *p = &(mbi->syms.aout_image);
        printk(TBOOT_DETA
               "\t aout :: tabsize: 0x%x, strsize: 0x%x, addr: 0x%x\n",
               p->tabsize, p->strsize, p->addr);
    }
    if ( mbi->flags & MBI_ELF ) {
        const elf_t *p = &(mbi->syms.elf_image);
        printk(TBOOT_DETA
               "\t elf :: num: %u, size: 0x%x, addr: 0x%x, shndx: 0x%x\n",
               p->num, p->size, p->addr, p->shndx);
    }
    if ( mbi->flags & MBI_MEMMAP ) {
        memory_map_t *p;
        printk(TBOOT_DETA
               "\t mmap_length: 0x%x, mmap_addr: 0x%x\n", mbi->mmap_length,
               mbi->mmap_addr);
        for ( p = (memory_map_t *)mbi->mmap_addr;
              (uint32_t)p < mbi->mmap_addr + mbi->mmap_length;
              p=(memory_map_t *)((uint32_t)p + p->size + sizeof(p->size)) ) {
	        printk(TBOOT_DETA"\t     size: 0x%x, base_addr: 0x%04x%04x, "
                   "length: 0x%04x%04x, type: %u\n", p->size,
                   p->base_addr_high, p->base_addr_low,
                   p->length_high, p->length_low, p->type);
        }
    }
    if ( mbi->flags & MBI_DRIVES ) {
        printk(TBOOT_DETA"\t drives_length: %u, drives_addr: 0x%x\n",
               mbi->drives_length, mbi->drives_addr);
    }
    if ( mbi->flags & MBI_CONFIG ) {
        printk(TBOOT_DETA"\t config_table: 0x%x\n", mbi->config_table);
    }
    if ( mbi->flags & MBI_BTLDNAME ) {
        printk(TBOOT_DETA"\t boot_loader_name@0x%x: %s\n",
               mbi->boot_loader_name, (char *)mbi->boot_loader_name);
    }
    if ( mbi->flags & MBI_APM ) {
        printk(TBOOT_DETA"\t apm_table: 0x%x\n", mbi->apm_table);
    }
    if ( mbi->flags & MBI_VBE ) {
        printk(TBOOT_DETA"\t vbe_control_info: 0x%x\n"
               "\t vbe_mode_info: 0x%x\n"
               "\t vbe_mode: 0x%x\n"
               "\t vbe_interface_seg: 0x%x\n"
               "\t vbe_interface_off: 0x%x\n"
               "\t vbe_interface_len: 0x%x\n",
               mbi->vbe_control_info,
               mbi->vbe_mode_info,
               mbi->vbe_mode,
               mbi->vbe_interface_seg,
               mbi->vbe_interface_off,
               mbi->vbe_interface_len
              );
    }
}
#endif

bool verify_loader_context(loader_ctx *lctx)
{
    unsigned int count;
    if (LOADER_CTX_BAD(lctx))
        return false;
    count = get_module_count(lctx);
    if (count < 1){
        printk(TBOOT_ERR"Error: no MB%d modules\n", lctx->type);
        return false;
    } else
        return true;
}

static bool remove_mb2_tag(loader_ctx *lctx, struct mb2_tag *cur)
{
    uint8_t *s, *d, *e;
    struct mb2_tag *next, *end;
    next = next_mb2_tag(cur);
    if (next == NULL){
        printk(TBOOT_ERR"missing next tag in remove_mb2_tag\n");
        return false;
    }
    /* where do we stop? */
    end = (struct mb2_tag *)(lctx->addr + 8);
    end = find_mb2_tag_type(end, MB2_TAG_TYPE_END);
    if (end == NULL){
        printk(TBOOT_ERR"remove_mb2_tag, no end tag!!!!\n");
        return false;
    }
    e = (uint8_t *) end + end->size;
    /* we'll do this byte-wise */
    s = (uint8_t *) next; d = (uint8_t *) cur;

    while (s <= e) {
        *d = *s; d++; s++;
    }
    /* adjust MB2 length */
    *((unsigned long *) lctx->addr) -=
        (uint8_t *)next - (uint8_t *)cur;
    /* sanity check */
    /* print_loader_ctx(lctx); */
    return true;
}

static bool
grow_mb2_tag(loader_ctx *lctx, struct mb2_tag *which, uint32_t how_much)
{
    struct mb2_tag *next, *new_next, *end;
    int growth, slack;
    uint8_t *s, *d;
    // uint32_t old_size = which->size;

    /* we're holding the tag struct to grow, get its successor */
    next = next_mb2_tag(which);

    /* find the end--we will need it */
    end = (struct mb2_tag *)(lctx->addr + 8);
    end = find_mb2_tag_type(end, MB2_TAG_TYPE_END);
    if ( end == NULL )
        return false;

    /* How much bigger does it need to be? */
    /* NOTE: this breaks the MBI 2 structure for walking
     * until we're done copying.
     */
    which->size += how_much;

    /* what's the new growth for its successor? */
    new_next = next_mb2_tag(which);
    growth = ((void *) new_next) - ((void *) next);

    /* check to make sure there's actually room for the growth */
    slack = g_mb_orig_size - *(uint32_t *) (lctx->addr);
    if (growth > slack){
        printk(TBOOT_ERR"YIKES!!! grow_mb2_tag slack %d < growth %d\n",
               slack, growth);
    }

    /* now we copy down from the bottom, going up */
    s = ((uint8_t *) end) + end->size;
    d = s + growth;
    while (s >= (uint8_t *)next){
        *d = *s;
        d--; s--;
    }
    /* adjust MB2 length */
    *((uint32_t *) lctx->addr) += growth;
    return true;
}

static void *remove_module(loader_ctx *lctx, void *mod_start)
{
    module_t *m = NULL;
    unsigned int i;

    if ( !verify_loader_context(lctx))
        return NULL;

    for ( i = 0; i < get_module_count(lctx); i++ ) {
        m = get_module(lctx, i);
        if ( mod_start == NULL || (void *)m->mod_start == mod_start )
            break;
    }

    /* not found */
    if ( m == NULL ) {
        printk(TBOOT_ERR"could not find module to remove\n");
        return NULL;
    }

    if (lctx->type == MB1_ONLY){
        /* multiboot 1 */
        /* if we're removing the first module (i.e. the "kernel") then */
        /* need to adjust some mbi fields as well */
        multiboot_info_t *mbi = (multiboot_info_t *) lctx->addr;
        if ( mod_start == NULL ) {
            mbi->cmdline = m->string;
            mbi->flags |= MBI_CMDLINE;
            mod_start = (void *)m->mod_start;
        }

        /* copy remaing mods down by one */
        tb_memmove(m, m + 1, (mbi->mods_count - i - 1)*sizeof(module_t));

        mbi->mods_count--;

        return mod_start;
    }
    if (lctx->type == MB2_ONLY){
        /* multiboot 2 */
        /* if we're removing the first module (i.e. the "kernel") then */
        /* need to adjust some mbi fields as well */
        char cmdbuf[TBOOT_KERNEL_CMDLINE_SIZE];
        cmdbuf[0] = '\0';
        if ( mod_start == NULL ) {
            char *cmdline = get_cmdline(lctx);
            char *mod_string = get_module_cmd(lctx, m);
            if ( cmdline == NULL ) {
                printk(TBOOT_ERR"could not find cmdline\n");
                return NULL;
            }
            if ( mod_string == NULL ) {
                printk(TBOOT_ERR"could not find module cmdline\n");
                return NULL;
            }
            if ((tb_strlen(mod_string)) > (tb_strlen(cmdline))){
                if (tb_strlen(mod_string) >= TBOOT_KERNEL_CMDLINE_SIZE){
                    printk(TBOOT_ERR"No room to copy MB2 cmdline [%d < %d]\n",
                           (int)(tb_strlen(cmdline)), (int)(tb_strlen(mod_string)));
                } else {
                    char *s = mod_string;
                    char *d = cmdbuf;
                    while (*s){
                        *d = *s;
                        d++; s++;
                    }
                    *d = *s;
                    // strcpy(cmdbuf, mod_string);
                }
            } else {
                // strcpy(cmdline,mod_string);
                char *s = mod_string;
                char *d = cmdline;
                while (*s){
                    *d = *s;
                    d++; s++;
                }
                *d = *s;
                /* note: we didn't adjust the "size" field, since it didn't
                 * grow and this saves us the pain of shuffling everything
                 * after cmdline (which is usually first)
                 */
            }
            mod_start = (void *)m->mod_start;
        }
        /* so MB2 is a different beast.  The modules aren't necessarily
         * adjacent, first, last, anything.  What we can do is bulk copy
         * everything after the thing we're killing over the top of it,
         * and shorten the total length of the MB2 structure.
         */
        {
            struct mb2_tag *cur;
            struct mb2_tag_module *mod = NULL;
            module_t *cur_mod = NULL;
            cur = (struct mb2_tag *)(lctx->addr + 8);
            cur = find_mb2_tag_type(cur, MB2_TAG_TYPE_MODULE);
            mod = (struct mb2_tag_module *) cur;
            if (mod != NULL)
                cur_mod = (module_t *)&(mod->mod_start);

            while (cur_mod != NULL && cur_mod != m){
                /* nudge off current record */
                cur = next_mb2_tag(cur);
                cur = find_mb2_tag_type(cur, MB2_TAG_TYPE_MODULE);
                mod = (struct mb2_tag_module *) cur;
                if (mod != NULL)
                    cur_mod = (module_t *)&(mod->mod_start);
                else
                    cur_mod = NULL;
            }
            if (cur_mod == NULL){
                printk(TBOOT_ERR"remove_module() for MB2 failed\n");
                return NULL;
            }

            /* we're here.  cur is the MB2 tag we need to overwrite. */
            if (false == remove_mb2_tag(lctx, cur))
                return NULL;
        }
        if (cmdbuf[0] != '\0'){
            /* we need to grow the mb2_tag_string that holds the cmdline.
             * we know there's room, since we've shortened the MB2 by the
             * length of the module_tag we've removed, which contained
             * the longer string.
             */
            struct mb2_tag *cur = (struct mb2_tag *)(lctx->addr + 8);
            struct mb2_tag_string *cmd;

            cur = find_mb2_tag_type(cur, MB2_TAG_TYPE_CMDLINE);
            cmd = (struct mb2_tag_string *) cur;
            if (cmd == NULL){
                printk(TBOOT_ERR"remove_modules MB2 shuffle NULL cmd\n");
                return NULL;
            }

            grow_mb2_tag(lctx, cur, tb_strlen(cmdbuf) - tb_strlen(cmd->string));

            /* now we're all good, except for fixing up cmd */
            {
                char * s = cmdbuf;
                char *d = cmd->string;
                while (*s){
                    *d = *s;
                    d++; s++;
                }
                *d = *s;
            }
        }
        return mod_start;
    }
    return NULL;
}

bool
find_module_by_pattern(loader_ctx *lctx, void **base, size_t *size,
                       const void *pattern, size_t len)
{
    if ( lctx == NULL || lctx->addr == NULL) {
        printk(TBOOT_ERR"Error: context pointer is zero.\n");
        return false;
    }

    if ( base == NULL ) {
        printk(TBOOT_ERR"Error: base is NULL.\n");
        return false;
    }

    *base = NULL;
    if ( size != NULL )
        *size = 0;

    if ( 0 == get_module_count(lctx)) {
        printk(TBOOT_ERR"Error: no module.\n");
        return false;
    }

    for ( unsigned int i = get_module_count(lctx) - 1; i > 0; i-- ) {
        module_t *m = get_module(lctx, i);
        /* check size */
        size_t mod_size = m->mod_end - m->mod_start;
        if ( len > mod_size ) {
            printk(TBOOT_ERR"Error: image size is smaller than data size.\n");
            return false;
        }

        for ( unsigned int j = 0; j < (mod_size + len); j++ ) {
            if ( tb_memcmp((void *)(m->mod_start + j), pattern, len) == 0 ) {
                *base = (void *)m->mod_start;
                if ( size != NULL )
                    *size = mod_size;
                return true;
            }
        }
    }

    return false;
}

/*
 * remove (all) SINIT and LCP policy data modules (if present)
 */
bool
remove_txt_modules(loader_ctx *lctx)
{
    if ( 0 == get_module_count(lctx)) {
        printk(TBOOT_ERR"Error: no module.\n");
        return false;
    }

    /* start at end of list so that we can remove w/in the loop */
    for ( unsigned int i = get_module_count(lctx) - 1; i > 0; i-- ) {
        module_t *m = get_module(lctx, i);
        void *base = (void *)m->mod_start;

        if ( is_sinit_acmod(base, m->mod_end - (unsigned long)base, true) ) {
            printk(TBOOT_INFO"got sinit match on module #%d\n", i);
            if ( remove_module(lctx, base) == NULL ) {
                printk(TBOOT_ERR
                       "failed to remove SINIT module from module list\n");
                return false;
            }
        }
    }

    return true;
}

bool
remove_dlmod_modules(loader_ctx *lctx)
{
    if ( 0 == get_module_count(lctx)) {
        printk(TBOOT_ERR"Error: no module.\n");
        return false;
    }

    /* start at end of list so that we can remove w/in the loop */
    for ( unsigned int i = get_module_count(lctx) - 1; i > 0; i-- ) {
        module_t *m = get_module(lctx, i);
        void *base = (void *)m->mod_start;

        if ( is_dlmod(base, m->mod_end - (unsigned long)base) ) {
            printk(TBOOT_INFO"got dlmod match on module #%d\n", i);
            if ( remove_module(lctx, base) == NULL ) {
                printk(TBOOT_ERR
                       "failed to remove DLMOD module from module list\n");
                return false;
            }
            printk(TBOOT_INFO"removed dlmod module\n");
        }
    }

    return true;
}

static unsigned long max(unsigned long a, unsigned long b)
{
    return (a > b) ? a : b;
}

static
unsigned long get_mbi_mem_end_mb1(const multiboot_info_t *mbi)
{
    unsigned long end = (unsigned long)(mbi + 1);

    if ( mbi->flags & MBI_CMDLINE )
        end = max(end, mbi->cmdline + tb_strlen((char *)mbi->cmdline) + 1);
    if ( mbi->flags & MBI_MODULES ) {
        end = max(end, mbi->mods_addr + mbi->mods_count * sizeof(module_t));
        unsigned int i;
        for ( i = 0; i < mbi->mods_count; i++ ) {
            module_t *p = get_module_mb1(mbi, i);
            if ( p == NULL )
                break;
            end = max(end, p->string + tb_strlen((char *)p->string) + 1);
        }
    }
    if ( mbi->flags & MBI_AOUT ) {
        const aout_t *p = &(mbi->syms.aout_image);
        end = max(end, p->addr + p->tabsize
                       + sizeof(unsigned long) + p->strsize);
    }
    if ( mbi->flags & MBI_ELF ) {
        const elf_t *p = &(mbi->syms.elf_image);
        end = max(end, p->addr + p->num * p->size);
    }
    if ( mbi->flags & MBI_MEMMAP )
        end = max(end, mbi->mmap_addr + mbi->mmap_length);
    if ( mbi->flags & MBI_DRIVES )
        end = max(end, mbi->drives_addr + mbi->drives_length);
    /* mbi->config_table field should contain */
    /*  "the address of the rom configuration table returned by the */
    /*  GET CONFIGURATION bios call", so skip it */
    if ( mbi->flags & MBI_BTLDNAME )
        end = max(end, mbi->boot_loader_name
                       + tb_strlen((char *)mbi->boot_loader_name) + 1);
    if ( mbi->flags & MBI_APM )
        /* per Grub-multiboot-Main Part2 Rev94-Structures, apm size is 20 */
        end = max(end, mbi->apm_table + 20);
    if ( mbi->flags & MBI_VBE ) {
        /* VBE2.0, VBE Function 00 return 512 bytes*/
        end = max(end, mbi->vbe_control_info + 512);
        /* VBE2.0, VBE Function 01 return 256 bytes*/
        end = max(end, mbi->vbe_mode_info + 256);
    }

    return PAGE_UP(end);
}

module_t *get_module(loader_ctx *lctx, unsigned int i)
{
    if (LOADER_CTX_BAD(lctx))
        return NULL;
    if (lctx->type == MB1_ONLY){
        return(get_module_mb1((multiboot_info_t *) lctx->addr, i));
    } else {
        /* so currently, must be type 2 */
        return(get_module_mb2(lctx, i));
    }
}

static void *remove_first_module(loader_ctx *lctx)
{
    if (LOADER_CTX_BAD(lctx))
        return NULL;
    return(remove_module(lctx, NULL));
}

bool prepare_intermediate_loader(void)
{
    module_t *m;
    void *kernel_image;
    size_t kernel_size;
    void *initrd_image;
    size_t initrd_size;
    uint64_t base;
    uint64_t size;

    //printk(TBOOT_INFO"reserving SLBOOT AP wake block (%Lx - %Lx) in e820 table\n", base, (base + size - 1));

    /* if using memory logging, reserve log area */
    if ( g_log_targets & TBOOT_LOG_TARGET_MEMORY ) {
        base = TBOOT_SERIAL_LOG_ADDR;
        size = TBOOT_SERIAL_LOG_SIZE;
        printk(TBOOT_INFO"reserving SLBOOT memory log (%Lx - %Lx) in e820 table\n", base, (base + size - 1));
        if ( !e820_protect_region(base, size, E820_RESERVED) )
            error_action(TB_ERR_FATAL);
    }

    /* replace map in loader context with copy */
    replace_e820_map(g_ldr_ctx);
    printk(TBOOT_DETA"adjusted e820 map:\n");
    print_e820_map();

    if ( !verify_loader_context(g_ldr_ctx) )
        return false;

    /* remove all SINIT and LCP modules since kernel may not handle */
    remove_txt_modules(g_ldr_ctx);

    /* remove all DLMOD modules since kernel may not handle */
    remove_dlmod_modules(g_ldr_ctx);

    printk(TBOOT_INFO"Assuming Intermediate Loader kernel is Linux format\n");

    /* print_mbi(g_mbi); */

    /* first module is the IL kernel image */
    m = get_module(g_ldr_ctx, 0);
    kernel_size = m->mod_end - m->mod_start;

    /* removing the module causes its command line to be set in the MBI */
    kernel_image = remove_first_module(g_ldr_ctx);
    if ( kernel_image == NULL )
        return false;

    if ( get_module_count(g_ldr_ctx) == 0 ) {
        initrd_size = 0;
        initrd_image = 0;
    }
    else {
        m = get_module(g_ldr_ctx, 0);
        initrd_image = (void *)m->mod_start;
        initrd_size = m->mod_end - m->mod_start;
    }

    return expand_linux_image(kernel_image, kernel_size,
                              initrd_image, initrd_size);
}

char *get_module_cmd(loader_ctx *lctx, module_t *mod)
{
    if (LOADER_CTX_BAD(lctx) || mod == NULL)
        return NULL;

    if (lctx->type == MB1_ONLY)
        return (char *) mod->string;
    else /* currently must be type 2 */
        return (char *)&(mod->string);
}

char *get_first_module_cmd(loader_ctx *lctx)
{
    module_t *mod = get_module(lctx, 0);
    return get_module_cmd(lctx, mod);
}

char *get_cmdline(loader_ctx *lctx)
{
    if (LOADER_CTX_BAD(lctx))
        return NULL;

    if (lctx->type == MB1_ONLY){
        /* multiboot 1 */
        if (((multiboot_info_t *)lctx->addr)->flags & MBI_CMDLINE){
            return (char *) ((multiboot_info_t *)lctx->addr)->cmdline;
        } else {
            return NULL;
        }
    } else {
        /* currently must be type  2 */
        struct mb2_tag *start = (struct mb2_tag *)(lctx->addr + 8);
        start = find_mb2_tag_type(start, MB2_TAG_TYPE_CMDLINE);
        if (start != NULL){
            struct mb2_tag_string *cmd = (struct mb2_tag_string *) start;
            return (char *) &(cmd->string);
        }
        return NULL;
    }
}

bool have_loader_memlimits(loader_ctx *lctx)
{
    if (LOADER_CTX_BAD(lctx))
        return false;
    if (lctx->type == MB1_ONLY){
        return (((multiboot_info_t *)lctx->addr)->flags & MBI_MEMLIMITS) != 0;
    } else {
        /* currently must be type 2 */
        struct mb2_tag *start = (struct mb2_tag *)(lctx->addr + 8);
        start = find_mb2_tag_type(start, MB2_TAG_TYPE_MEMLIMITS);
        return (start != NULL);
    }
}

uint32_t get_loader_mem_lower(loader_ctx *lctx)
{
    if (LOADER_CTX_BAD(lctx))
        return 0;
    if (lctx->type == MB1_ONLY){
        return ((multiboot_info_t *)lctx->addr)->mem_lower;
    }
    /* currently must be type 2 */
    struct mb2_tag *start = (struct mb2_tag *)(lctx->addr + 8);
    start = find_mb2_tag_type(start, MB2_TAG_TYPE_MEMLIMITS);
    if (start != NULL){
        struct mb2_tag_memlimits *lim = (struct mb2_tag_memlimits *) start;
        return lim->mem_lower;
    }
    return 0;
}

uint32_t get_loader_mem_upper(loader_ctx *lctx)
{
    if (LOADER_CTX_BAD(lctx))
        return 0;
    if (lctx->type == MB1_ONLY){
        return ((multiboot_info_t *)lctx->addr)->mem_upper;
    }
    /* currently must be type 2 */
    struct mb2_tag *start = (struct mb2_tag *)(lctx->addr + 8);
    start = find_mb2_tag_type(start, MB2_TAG_TYPE_MEMLIMITS);
    if (start != NULL){
        struct mb2_tag_memlimits *lim = (struct mb2_tag_memlimits *) start;
        return lim->mem_upper;
    }
    return 0;
}

unsigned int get_module_count(loader_ctx *lctx)
{
    if (LOADER_CTX_BAD(lctx))
        return 0;
    if (lctx->type == MB1_ONLY){
        return(((multiboot_info_t *) lctx->addr)->mods_count);
    } else {
        /* currently must be type 2 */
        struct mb2_tag *start = (struct mb2_tag *)(lctx->addr + 8);
        unsigned int count = 0;
        start = find_mb2_tag_type(start, MB2_TAG_TYPE_MODULE);
        while (start != NULL){
            count++;
            /* nudge off this guy */
            start = next_mb2_tag(start);
            start = find_mb2_tag_type(start, MB2_TAG_TYPE_MODULE);
        }
        return count;
    }
}

bool have_loader_memmap(loader_ctx *lctx)
{
    if (LOADER_CTX_BAD(lctx))
        return false;
    if (lctx->type == MB1_ONLY){
        return (((multiboot_info_t *) lctx->addr)->flags & MBI_MEMMAP) != 0;
    } else {
        /* currently must be type 2 */
        struct mb2_tag *start = (struct mb2_tag *)(lctx->addr + 8);
        start = find_mb2_tag_type(start, MB2_TAG_TYPE_MMAP);
        return (start != NULL);
    }
}

memory_map_t *get_loader_memmap(loader_ctx *lctx)
{
    if (LOADER_CTX_BAD(lctx))
        return NULL;
    if (lctx->type == MB1_ONLY){
        /* multiboot 1 */
        return (memory_map_t *)((multiboot_info_t *) lctx->addr)->mmap_addr;
    } else {
        /* currently must be type 2 */
        struct mb2_tag *start = (struct mb2_tag *)(lctx->addr + 8);
        start = find_mb2_tag_type(start, MB2_TAG_TYPE_MMAP);
        if (start != NULL){
            struct mb2_tag_mmap *mmap = (struct mb2_tag_mmap *) start;
            /* note here: the MB2 mem entries start with the 64-bit address.
             * the memory_map_t starts with four bytes of dummy "size".
             * Pointing to the MB2 mmap "entry_version" instead of the entries
             * lines the two tables up.
             */
            return (memory_map_t *) &(mmap->entry_version);
        }
        return NULL;
    }
}

uint32_t get_loader_memmap_length(loader_ctx *lctx)
{
    if (LOADER_CTX_BAD(lctx))
        return 0;
    if (lctx->type == MB1_ONLY){
        /* multiboot 1 */
        return (uint32_t)((multiboot_info_t *) lctx->addr)->mmap_length;
    } else {
        /* currently must be type 2 */
        struct mb2_tag *start = (struct mb2_tag *)(lctx->addr + 8);
        start = find_mb2_tag_type(start, MB2_TAG_TYPE_MMAP);
        if (start != NULL){
            struct mb2_tag_mmap *mmap = (struct mb2_tag_mmap *) start;
            /* mmap->size is the size of the whole tag.  We have 16 bytes
             * ahead of the entries
             */
            return mmap->size - 16;
        }
        return 0;
    }
}

unsigned long
get_loader_ctx_end(loader_ctx *lctx)
{
    if (LOADER_CTX_BAD(lctx))
        return 0;
    if (lctx->type == 1){
        /* multiboot 1 */
        return (get_mbi_mem_end_mb1((multiboot_info_t *) lctx->addr));
    } else {
        /* currently must be type 2 */
        unsigned long mb2_size = *((unsigned long *) lctx->addr);
        return PAGE_UP(mb2_size + (unsigned long) lctx->addr);
    }
}

/*
 * will go through all modules to find an RACM that matches the platform
 * (size can be NULL)
 */
bool
find_platform_racm(loader_ctx *lctx, void **base, uint32_t *size)
{
    if ( base != NULL )
        *base = NULL;
    if ( size != NULL )
        *size = 0;

    if ( 0 == get_module_count(lctx)) {
        printk(TBOOT_ERR"no module info\n");
        return false;
    }

    for ( int i = get_module_count(lctx) - 1; i >= 0; i-- ) {
        module_t *m = get_module(lctx, i);
        printk(TBOOT_DETA
               "checking if module %s is an RACM for this platform...\n",
               get_module_cmd(lctx, m));
        void *base2 = (void *)m->mod_start;
        uint32_t size2 = m->mod_end - (unsigned long)(base2);
        if ( is_racm_acmod(base2, size2, false) &&
             does_acmod_match_platform((acm_hdr_t *)base2) ) {
            if ( base != NULL )
                *base = base2;
            if ( size != NULL )
                *size = size2;
            printk(TBOOT_DETA"RACM matches platform\n");
            return true;
        }
    }
    /* no RACM found for this platform */
    printk(TBOOT_ERR"no RACM found\n");
    return false;
}

/*
 * will go through all modules to find an SINIT that matches the platform
 * (size can be NULL)
 */
bool
find_platform_sinit_module(loader_ctx *lctx, void **base, uint32_t *size)
{
    if ( base != NULL )
        *base = NULL;
    if ( size != NULL )
        *size = 0;

    if ( 0 == get_module_count(lctx)) {
        printk(TBOOT_ERR"no module info\n");
        return false;
    }

    for ( unsigned int i = get_module_count(lctx) - 1; i > 0; i-- ) {
        module_t *m = get_module(lctx, i);
        if (lctx->type == 1)
            printk(TBOOT_DETA
                   "checking if module %s is an SINIT for this platform...\n",
                   (const char *)m->string);
        if (lctx->type == 2)
            printk(TBOOT_DETA
                   "checking if module %s is an SINIT for this platform...\n",
                   (const char *)&(m->string));

        void *base2 = (void *)m->mod_start;
        uint32_t size2 = m->mod_end - (unsigned long)(base2);
        if ( is_sinit_acmod(base2, size2, false) &&
             does_acmod_match_platform((acm_hdr_t *)base2) ) {
            if ( base != NULL )
                *base = base2;
            if ( size != NULL )
                *size = size2;
            printk(TBOOT_DETA"SINIT matches platform\n");
            return true;
        }
    }
    /* no SINIT found for this platform */
    printk(TBOOT_ERR"no SINIT AC module found\n");
    return false;
}

bool
find_dlmod_module(loader_ctx *lctx, void **base, uint32_t *size)
{
    if ( base != NULL )
        *base = NULL;
    else
        return false;
    if ( size != NULL )
        *size = 0;
    else
        return false;

    if ( 0 == get_module_count(lctx)) {
        printk(TBOOT_ERR"no module info\n");
        return false;
    }

    for ( unsigned int i = get_module_count(lctx) - 1; i > 0; i-- ) {
        module_t *m = get_module(lctx, i);
        if (lctx->type == 1)
            printk(TBOOT_DETA
                   "checking if module %s is a DLMOD module...\n",
                   (const char *)m->string);
        if (lctx->type == 2)
            printk(TBOOT_DETA
                   "checking if module %s is a DLMOD module...\n",
                   (const char *)&(m->string));

        void *base2 = (void *)m->mod_start;
        uint32_t size2 = m->mod_end - (unsigned long)(base2);
        print_hex("MOD ", base2, 16);
        if ( is_dlmod(base2, size2) ) {
            *base = base2;
            *size = size2;
            printk(TBOOT_DETA"DLMOD found base: %p size: 0x%x\n", base2, size2);
            return true;
        }
    }
    printk(TBOOT_ERR"no DLMOD found\n");
    return false;
}

void
replace_e820_map(loader_ctx *lctx)
{
    /* replace original with the copy */
    if (LOADER_CTX_BAD(lctx))
        return;
    if (lctx->type == MB1_ONLY){
        /* multiboot 1 */
        multiboot_info_t *mbi = (multiboot_info_t *) lctx->addr;
        mbi->mmap_addr = (uint32_t)get_e820_copy();
        mbi->mmap_length = (get_nr_map()) * sizeof(memory_map_t);
        mbi->flags |= MBI_MEMMAP;   /* in case only MBI_MEMLIMITS was set */
        return;
    } else {
        /* currently must be type 2 */
        memory_map_t *old, *new;
        uint32_t i;
        uint32_t old_memmap_size = get_loader_memmap_length(lctx);
        uint32_t old_memmap_entry_count =
            old_memmap_size / sizeof(memory_map_t);
        if (old_memmap_entry_count < (get_nr_map())){
            /* we have to grow */
            struct mb2_tag *map = (struct mb2_tag *)(lctx->addr + 8);
            map = find_mb2_tag_type(map, MB2_TAG_TYPE_MMAP);
            if (map == NULL){
                printk(TBOOT_ERR"MB2 map not found\n");
                return;
            }
            if (false ==
                grow_mb2_tag(lctx, map,
                             sizeof(memory_map_t) *
                             ((get_nr_map()) - old_memmap_entry_count))){
                printk(TBOOT_ERR"MB2 failed to grow e820 map tag\n");
                return;
            }
        }
        /* copy in new data */
        {
            /* RLM: for now, we'll leave the entries in MB1 format (with real
             * size).  That may need revisited.
             */
            new = get_e820_copy();
            old = get_loader_memmap(lctx);
            if ( old == NULL ) {
                printk(TBOOT_ERR"old memory map not found\n");
                return;
            }
            for (i = 0; i < (get_nr_map()); i++){
                *old = *new;
                old++, new++;
            }
        }
        /*
           printk(TBOOT_INFO"AFTER replace_e820_map, loader context:\n");
           print_loader_ctx(lctx);
        */
        printk(TBOOT_INFO"replaced memory map:\n");
        print_e820_map();
        return;
    }
    return;
}

void print_loader_ctx(loader_ctx *lctx)
{
    if (lctx->type != MB2_ONLY){
        printk(TBOOT_ERR"this routine only prints out multiboot 2\n");
        return;
    } else {
        struct mb2_tag *start = (struct mb2_tag *)(lctx->addr + 8);
        printk(TBOOT_INFO"MB2 dump, size %d\n", *(uint32_t *)lctx->addr);
        while (start != NULL){
            printk(TBOOT_INFO"MB2 tag found of type %d size %d ",
                   start->type, start->size);
            switch (start->type){
            case MB2_TAG_TYPE_CMDLINE:
            case MB2_TAG_TYPE_LOADER_NAME:
                {
                    struct mb2_tag_string *ts =
                        (struct mb2_tag_string *) start;
                    printk(TBOOT_INFO"%s", ts->string);
                }
                break;
            case MB2_TAG_TYPE_MODULE:
                {
                    struct mb2_tag_module *ts =
                        (struct mb2_tag_module *) start;
                    printk_long(ts->cmdline);
                }
                break;
            default:
                break;
            }
            printk(TBOOT_INFO"\n");
            start = next_mb2_tag(start);
        }
        return;
    }
}

uint8_t
*get_loader_rsdp(loader_ctx *lctx, uint32_t *length)
{
    struct mb2_tag *start;
    struct mb2_tag_new_acpi *new_acpi;

    if (LOADER_CTX_BAD(lctx))
        return NULL;
    if (lctx->type != MB2_ONLY)
        return NULL;
    if (length == NULL)
        return NULL;

    start = (struct mb2_tag *) (lctx->addr + 8);
    new_acpi = (struct mb2_tag_new_acpi *)
        find_mb2_tag_type(start, MB2_TAG_TYPE_ACPI_NEW);
    if (new_acpi == NULL){
        /* we'll try the old type--the tag structs are the same */
        new_acpi = (struct mb2_tag_new_acpi *)
            find_mb2_tag_type(start, MB2_TAG_TYPE_ACPI_OLD);
        if (new_acpi == NULL)
            return NULL;
    }
    *length = new_acpi->size - 8;
    return new_acpi->rsdp;
}

bool
get_loader_efi_ptr(loader_ctx *lctx, uint32_t *address, uint64_t *long_address)
{
    struct mb2_tag *start, *hit;
    struct mb2_tag_efi32 *efi32;
    struct mb2_tag_efi64 *efi64;
    if (LOADER_CTX_BAD(lctx))
        return false;
    if (lctx->type != MB2_ONLY)
        return false;
    start = (struct mb2_tag *)(lctx->addr + 8);
    hit = find_mb2_tag_type(start, MB2_TAG_TYPE_EFI32);
    if (hit != NULL){
        efi32 = (struct mb2_tag_efi32 *) hit;
        *address = (uint32_t) efi32->pointer;
        *long_address = 0;
        return true;
    }
    hit = find_mb2_tag_type(start, MB2_TAG_TYPE_EFI64);
    if (hit != NULL){
        efi64 = (struct mb2_tag_efi64 *) hit;
        *long_address = (uint64_t) efi64->pointer;
        *address = 0;
        return true;
    }
    return false;
}

uint32_t
find_efi_memmap(loader_ctx *lctx, uint32_t *descr_size,
                uint32_t *descr_vers, uint32_t *mmap_size) {
    struct mb2_tag *start = NULL, *hit = NULL;
    struct mb2_tag_efi_mmap *efi_mmap = NULL;

    start = (struct mb2_tag *)(lctx->addr + 8);
    hit = find_mb2_tag_type(start, MB2_TAG_TYPE_EFI_MMAP);
    if (hit == NULL) {
       return 0;
    }

    efi_mmap = (struct mb2_tag_efi_mmap *)hit;
    *descr_size = efi_mmap->descr_size;
    *descr_vers = efi_mmap->descr_vers;
    *mmap_size = efi_mmap->size - sizeof(struct mb2_tag_efi_mmap);
    if (*mmap_size % *descr_size) {
        printk(TBOOT_WARN "EFI memmmap (0x%x) should be a multiple of descriptor size (0x%x)\n",
	       *mmap_size, *descr_size);
    }
    return (uint32_t)(&efi_mmap->efi_mmap);
}

bool
is_loader_launch_efi(loader_ctx *lctx)
{
    uint32_t addr = 0; uint64_t long_addr = 0;
    if (LOADER_CTX_BAD(lctx))
        return false;
    return (get_loader_efi_ptr(lctx, &addr, &long_addr));
}

void load_framebuffer_info(loader_ctx *lctx, void *vscr)
{
    screen_info_t *scr = (screen_info_t *) vscr;
    struct mb2_tag *start;

    if (scr == NULL)
        return;
    if (LOADER_CTX_BAD(lctx))
        return;
    start = (struct mb2_tag *)(lctx->addr + 8);
    start = find_mb2_tag_type(start, MB2_TAG_TYPE_FRAMEBUFFER);
    if (start != NULL){
        struct mb2_fb *mbf = (struct mb2_fb *) start;
        scr->lfb_base = (uint32_t) mbf->common.fb_addr;
        scr->lfb_width = mbf->common.fb_width;
        scr->lfb_height = mbf->common.fb_height;
        scr->lfb_depth =  mbf->common.fb_bpp;
        scr->lfb_line_len = mbf->common.fb_pitch;
        scr->red_mask_size = mbf->fb_red_mask_size;
        scr->red_field_pos = mbf->fb_red_field_position;
        scr->blue_mask_size = mbf->fb_blue_mask_size;
        scr->blue_field_pos = mbf->fb_blue_field_position;
        scr->green_mask_size = mbf->fb_green_mask_size;
        scr->green_field_pos = mbf->fb_green_field_position;

        scr->lfb_size = scr->lfb_line_len * scr->lfb_height;
        /* round up to next 64k */
        scr->lfb_size = (scr->lfb_size + 65535) & 65535;

        scr->orig_video_isVGA = 0x70; /* EFI FB */
        scr->orig_y = 24;
    }

}

void determine_loader_type(void *addr, uint32_t magic)
{
    if (g_ldr_ctx->addr == NULL){
        /* brave new world */
        g_ldr_ctx->addr = addr;
        switch (magic){
        case MB_MAGIC:
            g_ldr_ctx->type = MB1_ONLY;
            {
                /* we may as well do this here--if we received an ELF
                 * sections tag, we won't use it, and it's useless to
                 * Xen downstream, since it's OUR ELF sections, not Xen's
                 */
                multiboot_info_t *mbi =
                    (multiboot_info_t *) addr;
                if (mbi->flags & MBI_AOUT){
                    mbi->flags &= ~MBI_AOUT;
                }
                if (mbi->flags & MBI_ELF){
                    mbi->flags &= ~MBI_ELF;
                }
            }
            break;
        case MB2_LOADER_MAGIC:
            g_ldr_ctx->type = MB2_ONLY;
            /* save the original MB2 info size, since we have
             * to put updates inline
             */
            g_mb_orig_size = *(uint32_t *) addr;

            {
                void *mb2_reloc;

                /* Since GRUB is sticking the MB2 structure very close to the
                 * default location for the kernel, move it just below the SLBOOT
                 * image.
                 */
                mb2_reloc = (void*)PAGE_DOWN(TBOOT_BASE_ADDR - g_mb_orig_size);
                tb_memcpy(mb2_reloc, addr, g_mb_orig_size);
                g_ldr_ctx->addr = mb2_reloc;
                addr = mb2_reloc;
                printk(TBOOT_INFO"MB2 relocated to: %p size: %x\n",
                       addr, g_mb_orig_size);

                /* we may as well do this here--if we received an ELF
                 * sections tag, we won't use it, and it's useless to
                 * Xen downstream, since it's OUR ELF sections, not Xen's
                 */
                struct mb2_tag *start =
                    (struct mb2_tag *)(addr + 8);
                start = find_mb2_tag_type(start, MB2_TAG_TYPE_ELF_SECTIONS);
                if (start != NULL)
                    (void) remove_mb2_tag(g_ldr_ctx, start);
            }
            break;
        default:
            g_ldr_ctx->type = 0;
            break;
        }
    }
    /* so at this point, g_ldr_ctx->type has one of three values:
     * 0: not a multiboot launch--we're doomed
     * 1: MB1 launch
     * 2: MB2 launch
     */
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
