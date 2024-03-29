/*
 * e820.c: support functions for manipulating the e820 table
 *
 * Copyright (c) 2006-2012, Intel Corporation
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
#include <stdbool.h>
#include <skboot.h>
#include <printk.h>
#include <cmdline.h>
#include <string.h>
#include <loader.h>
#include <stdarg.h>
#include <e820.h>

/*
 * copy of bootloader/BIOS e820 table with adjusted entries
 * this version will replace original in mbi
 */
#define MAX_E820_ENTRIES      (SKBOOT_E820_COPY_SIZE / sizeof(memory_map_t))
static unsigned int g_nr_map;
static memory_map_t *g_copy_e820_map = (memory_map_t *)SKBOOT_E820_COPY_ADDR;

static inline void split64b(uint64_t val, uint32_t *val_lo, uint32_t *val_hi)  {
     *val_lo = (uint32_t)(val & 0xffffffff);
     *val_hi = (uint32_t)(val >> 32);
 }

static inline uint64_t combine64b(uint32_t val_lo, uint32_t val_hi)
{
    return ((uint64_t)val_hi << 32) | (uint64_t)val_lo;
}

static inline uint64_t e820_base_64(memory_map_t *entry)
{
    return combine64b(entry->base_addr_low, entry->base_addr_high);
}

static inline uint64_t e820_length_64(memory_map_t *entry)
{
    return combine64b(entry->length_low, entry->length_high);
}


/*
 * print_e820_map
 *
 * Prints copied e820 map w/o any header (i.e. just entries, indented by a tab)
 *
 */
static void print_map(memory_map_t *e820, int nr_map)
{
    for ( int i = 0; i < nr_map; i++ ) {
        memory_map_t *entry = &e820[i];
        uint64_t base_addr, length;

        base_addr = e820_base_64(entry);
        length = e820_length_64(entry);

        printk(SKBOOT_DETA"\t%016Lx - %016Lx  (%d)\n",
               (unsigned long long)base_addr,
               (unsigned long long)(base_addr + length),
               entry->type);
    }
}

static bool insert_after_region(memory_map_t *e820map, unsigned int *nr_map,
                                unsigned int pos, uint64_t addr, uint64_t size,
                                uint32_t type)
{
    /* no more room */
    if ( *nr_map + 1 > MAX_E820_ENTRIES )
        return false;

    /* shift (copy) everything up one entry */
    for ( unsigned int i = *nr_map - 1; i > pos; i--)
        e820map[i+1] = e820map[i];

    /* now add our entry */
    split64b(addr, &(e820map[pos+1].base_addr_low),
             &(e820map[pos+1].base_addr_high));
    split64b(size, &(e820map[pos+1].length_low),
             &(e820map[pos+1].length_high));
    e820map[pos+1].type = type;
    e820map[pos+1].size = sizeof(memory_map_t) - sizeof(uint32_t);

    (*nr_map)++;

    return true;
}

static void remove_region(memory_map_t *e820map, unsigned int *nr_map,
                          unsigned int pos)
{
    /* shift (copy) everything down one entry */
    for ( unsigned int i = pos; i < *nr_map - 1; i++)
        e820map[i] = e820map[i+1];

    (*nr_map)--;
}

static bool protect_region(memory_map_t *e820map, unsigned int *nr_map,
                           uint64_t new_addr, uint64_t new_size,
                           uint32_t new_type)
{
    uint64_t addr, tmp_addr, size, tmp_size;
    uint32_t type;
    unsigned int i;

    if ( new_size == 0 )
        return true;
    /* check for wrap */
    if ( new_addr + new_size < new_addr )
        return false;

    /* find where our region belongs in the table and insert it */
    for ( i = 0; i < *nr_map; i++ ) {
        addr = e820_base_64(&e820map[i]);
        size = e820_length_64(&e820map[i]);
        type = e820map[i].type;
        /* is our region at the beginning of the current map region? */
        if ( new_addr == addr ) {
            if ( !insert_after_region(e820map, nr_map, i-1, new_addr, new_size,
                                      new_type) )
                return false;
            break;
        }
        /* are we w/in the current map region? */
        else if ( new_addr > addr && new_addr < (addr + size) ) {
            if ( !insert_after_region(e820map, nr_map, i, new_addr, new_size,
                                      new_type) )
                return false;
            /* fixup current region */
            tmp_addr = e820_base_64(&e820map[i]);
            split64b(new_addr - tmp_addr, &(e820map[i].length_low),
                     &(e820map[i].length_high));
            i++;   /* adjust to always be that of our region */
            /* insert a copy of current region (before adj) after us so */
            /* that rest of code can be common with previous case */
            if ( !insert_after_region(e820map, nr_map, i, addr, size, type) )
                return false;
            break;
        }
        /* is our region in a gap in the map? */
        else if ( addr > new_addr ) {
            if ( !insert_after_region(e820map, nr_map, i-1, new_addr, new_size,
                                      new_type) )
                return false;
            break;
        }
    }
    /* if we reached the end of the map without finding an overlapping */
    /* region, insert us at the end (note that this test won't trigger */
    /* for the second case above because the insert() will have incremented */
    /* nr_map and so i++ will still be less) */
    if ( i == *nr_map ) {
        if ( !insert_after_region(e820map, nr_map, i-1, new_addr, new_size,
                                  new_type) )
            return false;
        return true;
    }

    i++;     /* move to entry after our inserted one (we're not at end yet) */

    tmp_addr = e820_base_64(&e820map[i]);
    tmp_size = e820_length_64(&e820map[i]);

    /* did we split the (formerly) previous region? */
    if ( (new_addr >= tmp_addr) &&
         ((new_addr + new_size) < (tmp_addr + tmp_size)) ) {
        /* then adjust the current region (adj size first) */
        split64b((tmp_addr + tmp_size) - (new_addr + new_size),
                 &(e820map[i].length_low), &(e820map[i].length_high));
        split64b(new_addr + new_size,
                 &(e820map[i].base_addr_low), &(e820map[i].base_addr_high));
        return true;
    }

    /* if our region completely covers any existing regions, delete them */
    while ( (i < *nr_map) && ((new_addr + new_size) >=
                              (tmp_addr + tmp_size)) ) {
        remove_region(e820map, nr_map, i);
        tmp_addr = e820_base_64(&e820map[i]);
        tmp_size = e820_length_64(&e820map[i]);
    }

    /* finally, if our region partially overlaps an existing region, */
    /* then truncate the existing region */
    if ( i < *nr_map ) {
        tmp_addr = e820_base_64(&e820map[i]);
        tmp_size = e820_length_64(&e820map[i]);
        if ( (new_addr + new_size) > tmp_addr ) {
            split64b((tmp_addr + tmp_size) - (new_addr + new_size),
                        &(e820map[i].length_low), &(e820map[i].length_high));
            split64b(new_addr + new_size, &(e820map[i].base_addr_low),
                        &(e820map[i].base_addr_high));
        }
    }

    return true;
}

/* helper funcs for loader.c */
memory_map_t *get_e820_copy()
{
    return g_copy_e820_map;
}

unsigned int get_nr_map()
{
    return g_nr_map;
}

/*
 * copy_e820_map
 *
 * Copies the raw e820 map from bootloader to new table with room for expansion
 *
 * return:  false = error (no table or table too big for new space)
 */
bool copy_e820_map(loader_ctx *lctx)
{
    g_nr_map = 0;

    if (have_loader_memmap(lctx)){
        uint32_t memmap_length = get_loader_memmap_length(lctx);
        memory_map_t *memmap = get_loader_memmap(lctx);
        printk(SKBOOT_DETA"original e820 map:\n");
        print_map(memmap, memmap_length/sizeof(memory_map_t));

        uint32_t entry_offset = 0;

        while ( entry_offset < memmap_length &&
                g_nr_map < MAX_E820_ENTRIES ) {
            memory_map_t *entry = (memory_map_t *)
                (((uint32_t) memmap) + entry_offset);

            /* we want to support unordered and/or overlapping entries */
            /* so use protect_region() to insert into existing map, since */
            /* it handles these cases */
            if ( !protect_region(g_copy_e820_map, &g_nr_map,
                                 e820_base_64(entry), e820_length_64(entry),
                                 entry->type) )
                return false;
            if (lctx->type == 1)
                entry_offset += entry->size + sizeof(entry->size);
            if (lctx->type == 2)
                /* the MB2 memory map entries don't have a size--
                 * they have a "zero" with a value of zero. Additionally,
                 * because they *end* with a size and the MB1 guys *start*
                 * with a size, we get into trouble if we try to use them,
                 */
                entry_offset += sizeof(memory_map_t);

        }
        if ( g_nr_map == MAX_E820_ENTRIES ) {
            printk(SKBOOT_ERR"Too many e820 entries\n");
            return false;
        }
    }
    else if ( have_loader_memlimits(lctx) ) {
        printk(SKBOOT_DETA"no e820 map, mem_lower=%x, mem_upper=%x\n",
               get_loader_mem_lower(lctx), get_loader_mem_upper(lctx));

        /* lower limit is 0x00000000 - <mem_lower>*0x400 (i.e. in kb) */
        g_copy_e820_map[0].base_addr_low = 0;
        g_copy_e820_map[0].base_addr_high = 0;
        g_copy_e820_map[0].length_low = (get_loader_mem_lower(lctx)) << 10;
        g_copy_e820_map[0].length_high = 0;
        g_copy_e820_map[0].type = E820_RAM;
        g_copy_e820_map[0].size = sizeof(memory_map_t) - sizeof(uint32_t);

        /* upper limit is 0x00100000 - <mem_upper>*0x400 */
        g_copy_e820_map[1].base_addr_low = 0x100000;
        g_copy_e820_map[1].base_addr_high = 0;
        split64b((uint64_t)(get_loader_mem_upper(lctx)) << 10,
                 &(g_copy_e820_map[1].length_low),
                 &(g_copy_e820_map[1].length_high));
        g_copy_e820_map[1].type = E820_RAM;
        g_copy_e820_map[1].size = sizeof(memory_map_t) - sizeof(uint32_t);

        g_nr_map = 2;
    }
    else {
        printk(SKBOOT_ERR"no e820 map nor memory limits provided\n");
        return false;
    }

    return true;
}

bool e820_protect_region(uint64_t addr, uint64_t size, uint32_t type)
{
    return protect_region(g_copy_e820_map, &g_nr_map, addr, size, type);
}

void print_e820_map(void)
{
    print_map(g_copy_e820_map, g_nr_map);
}

/* find highest (< <limit>) RAM region of at least <size> bytes */
void get_highest_sized_ram(uint64_t size, uint64_t limit,
                           uint64_t *ram_base, uint64_t *ram_size)
{
    uint64_t last_fit_base = 0, last_fit_size = 0;

    if ( ram_base == NULL || ram_size == NULL )
        return;

    for ( unsigned int i = 0; i < g_nr_map; i++ ) {
        memory_map_t *entry = &g_copy_e820_map[i];

        if ( entry->type == E820_RAM ) {
            uint64_t base = e820_base_64(entry);
            uint64_t length = e820_length_64(entry);

            /* over 4GB so use the last region that fit */
            if ( base + length > limit )
                break;
            if ( size <= length ) {
                last_fit_base = base;
                last_fit_size = length;
            }
        }
    }

    *ram_base = last_fit_base;
    *ram_size = last_fit_size;
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
