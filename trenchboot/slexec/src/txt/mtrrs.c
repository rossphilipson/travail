/*
 * mtrrs.c: support functions for manipulating MTRRs
 *
 * Copyright (c) 2003-2010, Intel Corporation
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
#include <slexec.h>
#include <stdarg.h>
#include <string.h>
#include <printk.h>
#include <loader.h>
#include <processor.h>
#include <misc.h>
#include <acpi.h>
#include <tpm.h>
#include <txt/mle.h>
#include <txt/txt.h>
#include <txt/acmod.h>
#include <txt/mtrrs.h>

#define SINIT_MTRR_MASK 0xFFFFFF  /* SINIT requires 36b mask */

static void print_mtrrs(const mtrr_state_t *saved_state)
{
    printk(SLEXEC_DETA"mtrr_def_type: e = %d, fe = %d, type = %x\n",
           saved_state->mtrr_def_type.e, saved_state->mtrr_def_type.fe,
           saved_state->mtrr_def_type.type );
    printk(SLEXEC_DETA"mtrrs:\n");
    printk(SLEXEC_DETA"\t\t    base          mask      type  v\n");
    for ( unsigned int i = 0; i < saved_state->num_var_mtrrs; i++ ) {
        printk(SLEXEC_DETA"\t\t%13.13Lx %13.13Lx  %2.2x  %d\n",
               (uint64_t)saved_state->mtrr_var_pair[i].mtrr_physbase.base,
               (uint64_t)saved_state->mtrr_var_pair[i].mtrr_physmask.mask,
               saved_state->mtrr_var_pair[i].mtrr_physbase.type,
               saved_state->mtrr_var_pair[i].mtrr_physmask.v );
    }
}

/*
 * set the memory type for specified range (base to base+size)
 * to mem_type and everything else to UC
 */
static bool set_mem_type(const void *base, uint32_t size,
                          uint32_t mem_type)
{
    int num_pages;
    int ndx;
    mtrr_def_type_t mtrr_def_type;
    mtrr_cap_t mtrr_cap;
    mtrr_physmask_t mtrr_physmask;
    mtrr_physbase_t mtrr_physbase;
    unsigned long base_v;
    int i =0;
    int mtrr_s = 1; /* mtrr size in pages */

    /*
     * disable all fixed MTRRs
     * set default type to UC
     */
    mtrr_def_type.raw = rdmsr(MSR_MTRRdefType);
    mtrr_def_type.fe = 0;
    mtrr_def_type.type = MTRR_TYPE_UNCACHABLE;
    wrmsr(MSR_MTRRdefType, mtrr_def_type.raw);

    /*
     * initially disable all variable MTRRs (we'll enable the ones we use)
     */
    mtrr_cap.raw = rdmsr(MSR_MTRRcap);
    for ( ndx = 0; ndx < mtrr_cap.vcnt; ndx++ ) {
        mtrr_physmask.raw = rdmsr(MTRR_PHYS_MASK0_MSR + ndx*2);
        mtrr_physmask.v = 0;
        wrmsr(MTRR_PHYS_MASK0_MSR + ndx*2, mtrr_physmask.raw);
    }

    /*
     * map all AC module pages as mem_type
     */

    num_pages = PAGE_UP(size) >> PAGE_SHIFT;
    ndx = 0;

    printk(SLEXEC_DETA"setting MTRRs for acmod: base=%p, size=%x, num_pages=%d\n",
           base, size, num_pages);
    /*
     * Each VAR MTRR base must be a multiple if that MTRR's Size
    */
    base_v = (unsigned long) base;
    while ((base_v & 0x01) == 0) {
          i++;
          base_v = base_v >>1 ;

    }

    for (int j = i - 12; j > 0; j--)
        mtrr_s = mtrr_s*2; //mtrr_s = mtrr_s << 1

    printk(SLEXEC_DETA"The maximum allowed MTRR range size=%d Pages \n", mtrr_s);

    while (num_pages >= mtrr_s) {
	/* set the base of the current MTRR */
        mtrr_physbase.raw = rdmsr(MTRR_PHYS_BASE0_MSR + ndx*2);
        mtrr_physbase.base = ((unsigned long)base >> PAGE_SHIFT) &
	                     SINIT_MTRR_MASK;
        mtrr_physbase.type = mem_type;
        wrmsr(MTRR_PHYS_BASE0_MSR + ndx*2, mtrr_physbase.raw);

        mtrr_physmask.raw = rdmsr(MTRR_PHYS_MASK0_MSR + ndx*2);
        mtrr_physmask.mask = ~(mtrr_s - 1) & SINIT_MTRR_MASK;
        mtrr_physmask.v = 1;
        wrmsr(MTRR_PHYS_MASK0_MSR + ndx*2, mtrr_physmask.raw);

        base += (mtrr_s * PAGE_SIZE);
        num_pages -= mtrr_s;
        ndx++;
        if ( ndx == mtrr_cap.vcnt ) {
            printk(SLEXEC_ERR"exceeded number of var MTRRs when mapping range\n");
            return false;
        }
    }

    while ( num_pages > 0 ) {
        uint32_t pages_in_range;

        /* set the base of the current MTRR */
        mtrr_physbase.raw = rdmsr(MTRR_PHYS_BASE0_MSR + ndx*2);
        mtrr_physbase.base = ((unsigned long)base >> PAGE_SHIFT) &
	                     SINIT_MTRR_MASK;
        mtrr_physbase.type = mem_type;
        wrmsr(MTRR_PHYS_BASE0_MSR + ndx*2, mtrr_physbase.raw);

        /*
         * calculate MTRR mask
         * MTRRs can map pages in power of 2
         * may need to use multiple MTRRS to map all of region
         */
        pages_in_range = 1 << (fls(num_pages) - 1);

        mtrr_physmask.raw = rdmsr(MTRR_PHYS_MASK0_MSR + ndx*2);
        mtrr_physmask.mask = ~(pages_in_range - 1) & SINIT_MTRR_MASK;
        mtrr_physmask.v = 1;
        wrmsr(MTRR_PHYS_MASK0_MSR + ndx*2, mtrr_physmask.raw);

        /*prepare for the next loop depending on number of pages
         * We figure out from the above how many pages could be used in this
         * mtrr. Then we decrement the count, increment the base,
         * increment the mtrr we are dealing with, and if num_pages is
         * still not zero, we do it again.
         */
        base += (pages_in_range * PAGE_SIZE);
        num_pages -= pages_in_range;
        ndx++;
        if ( ndx == mtrr_cap.vcnt ) {
            printk(SLEXEC_ERR"exceeded number of var MTRRs when mapping range\n");
            return false;
        }
    }
    return true;
}

/*
 * this must be done for each processor so that all have the same
 * memory types
 */
bool set_mtrrs_for_acmod(const acm_hdr_t *hdr)
{
    unsigned long eflags;
    unsigned long cr0, cr4;

    /*
     * need to do some things before we start changing MTRRs
     *
     * since this will modify some of the MTRRs, they should be saved first
     * so that they can be restored once the AC mod is done
     */

    /* disable interrupts */
    eflags = read_eflags();
    disable_intr();

    /* save CR0 then disable cache (CRO.CD=1, CR0.NW=0) */
    cr0 = read_cr0();
    write_cr0((cr0 & ~CR0_NW) | CR0_CD);

    /* flush caches */
    wbinvd();

    /* save CR4 and disable global pages (CR4.PGE=0) */
    cr4 = read_cr4();
    write_cr4(cr4 & ~CR4_PGE);

    /* disable MTRRs */
    set_all_mtrrs(false);

    /*
     * now set MTRRs for AC mod and rest of memory
     */
    if ( !set_mem_type(hdr, hdr->size*4, MTRR_TYPE_WRBACK) )
        return false;

    /*
     * now undo some of earlier changes and enable our new settings
     */

    /* flush caches */
    wbinvd();

    /* enable MTRRs */
    set_all_mtrrs(true);

    /* restore CR0 (cacheing) */
    write_cr0(cr0);

    /* restore CR4 (global pages) */
    write_cr4(cr4);

    /* enable interrupts */
    write_eflags(eflags);

    return true;
}

void save_mtrrs(mtrr_state_t *saved_state)
{
    mtrr_cap_t mtrr_cap;

    /* IA32_MTRR_DEF_TYPE MSR */
    saved_state->mtrr_def_type.raw = rdmsr(MSR_MTRRdefType);

    /* number variable MTTRRs */
    mtrr_cap.raw = rdmsr(MSR_MTRRcap);
    if ( mtrr_cap.vcnt > MAX_VARIABLE_MTRRS ) {
        /* print warning but continue saving what we can */
        /* (set_mem_type() won't exceed the array, so we're safe doing this) */
        printk(SLEXEC_WARN"actual # var MTRRs (%d) > MAX_VARIABLE_MTRRS (%d)\n",
               mtrr_cap.vcnt, MAX_VARIABLE_MTRRS);
        saved_state->num_var_mtrrs = MAX_VARIABLE_MTRRS;
    }
    else
        saved_state->num_var_mtrrs = mtrr_cap.vcnt;

    /* physmask's and physbase's */
    for ( unsigned int ndx = 0; ndx < saved_state->num_var_mtrrs; ndx++ ) {
        saved_state->mtrr_var_pair[ndx].mtrr_physmask.raw =
            rdmsr(MTRR_PHYS_MASK0_MSR + ndx*2);
        saved_state->mtrr_var_pair[ndx].mtrr_physbase.raw =
            rdmsr(MTRR_PHYS_BASE0_MSR + ndx*2);
    }

    print_mtrrs(saved_state);
}

/* enable/disable all MTRRs */
void set_all_mtrrs(bool enable)
{
    mtrr_def_type_t mtrr_def_type;

    mtrr_def_type.raw = rdmsr(MSR_MTRRdefType);
    mtrr_def_type.e = enable ? 1 : 0;
    wrmsr(MSR_MTRRdefType, mtrr_def_type.raw);
}

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil * End:
 */
