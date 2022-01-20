/*
 * txt.c: Intel(r) TXT support functions, including initiating measured
 *        launch, post-launch, AP wakeup, etc.
 *
 * Copyright (c) 2003-2011, Intel Corporation
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
#include <e820.h>
#include <acpi.h>
#include <linux.h>
#include <tpm.h>
#include <cmdline.h>
#include <txt/smx.h>
#include <txt/mle.h>
#include <txt/txt.h>
#include <txt/smx.h>
#include <txt/acmod.h>
#include <txt/mtrrs.h>
#include <txt/heap.h>

extern uint32_t g_min_ram;
extern char _start[];             /* start of module */
extern char _end[];               /* end of module */

static uint32_t g_slaunch_header;
static event_log_container_t *g_elog = NULL;
static heap_event_log_ptr_elt2_1_t *g_elog_2_1 = NULL;
static uint32_t g_using_da = 0;

static void print_file_info(void)
{
    printk(SLEXEC_DETA"file addresses:\n");
    printk(SLEXEC_DETA"\t &_start=%p\n", &_start);
    printk(SLEXEC_DETA"\t &_end=%p\n", &_end);
}

#if 0
static void dump_page_tables(void *ptab_base)
{
    uint64_t *pg_dir_ptr_tab;
    uint64_t *pte, *pde;
    int i, j;

    pg_dir_ptr_tab = (uint64_t*)ptab_base;
    printk(SLEXEC_DETA"PDPE(0)=0x%llx\n", pg_dir_ptr_tab[0] & PAGE_MASK);

    pde = (uint64_t*)(uint32_t)(pg_dir_ptr_tab[0] & PAGE_MASK);

    for (i = 0; i < 512; i++) {
        if (pde[i] == 0)
            break;

        printk(SLEXEC_DETA"  PDE(%d)=0x%llx\n", i, pde[i]);
        pte = (uint64_t*)(uint32_t)(pde[i] & PAGE_MASK);

        for (j = 0; j < 512; j++) {
            if (pte[j] == 0)
                break;

            printk(SLEXEC_DETA"    PTE(%d)=0x%llx\n", j, pte[j]);
        }
    }
}
#endif

/*
 * build_mle_pagetable()
 */
static void *calculate_ptab_base_size(uint32_t *ptab_size)
{
    uint32_t pages;
    void *ptab_base;

    /*
     * TODO should check there is enough space between kernel base and the
     * beginning of the RAM region where it is located.
     */

    /* Round up pages from int divide and add PD and PDPT */
    pages = g_sl_kernel_setup.protected_mode_size/(512*PAGE_SIZE) + 3;
    *ptab_size = pages*PAGE_SIZE;
    ptab_base = (void*)(PAGE_DOWN(g_sl_kernel_setup.protected_mode_base) - *ptab_size);

    printk(SLEXEC_DETA"Page table start=0x%x, size=0x%x, count=0x%x\n",
           (uint32_t)ptab_base, *ptab_size, pages);

    return ptab_base;
}

/*
 * If enough room is available in front of the MLE, the maximum size of an
 * MLE that can be covered is 1G. This is due to having 512 PDEs pointing
 * to 512 page tables with 512 PTEs each.
 */
#define SLEXEC_MAX_MLE_SIZE (512*512*4096)

/* Page dir/table entry is phys addr + P + R/W + PWT */
#define MAKE_PDTE(addr)  (((uint64_t)(unsigned long)(addr) & PAGE_MASK) | 0x01)

/* The MLE page tables have to be below the MLE which by default loads at 1M */
/* so 20 pages are carved out of low memory from 0x6B000 to 0x80000 */
/* That leave 18 page table pages that can cover up to 36M */
/* can only contain 4k pages */

static void *build_mle_pagetable(void)
{
    void *ptab_base;
    uint32_t ptab_size, mle_off, pd_off;
    void *pg_dir_ptr_tab, *pg_dir, *pg_tab;
    uint64_t *pte, *pde;
    uint32_t mle_start = g_sl_kernel_setup.protected_mode_base;
    uint32_t mle_size = g_sl_kernel_setup.protected_mode_size;

    printk(SLEXEC_DETA"MLE start=0x%x, end=0x%x, size=0x%x\n",
           mle_start, mle_start+mle_size, mle_size);

    if ( mle_size > SLEXEC_MAX_MLE_SIZE ) {
        printk(SLEXEC_ERR"MLE size exceeds maximum size allowable (1Gb)\n");
        return NULL;
    }

    /* should start on page boundary */
    if ( mle_start & ~PAGE_MASK ) {
        printk(SLEXEC_ERR"MLE start is not page-aligned\n");
        return NULL;
    }

    /*
     * Place ptab_base below MLE. If the kernel is not relocatable then
     * we have to use the low memory block since the kernel gets loaded
     * at 1M. This does not work on server systems though.
     */
    if ( g_sl_kernel_setup.boot_params->hdr.relocatable_kernel ) {
        ptab_base = calculate_ptab_base_size(&ptab_size);
        if ( !ptab_base ) {
            printk(SLEXEC_ERR"MLE size exceeds space available for page tables\n");
            return NULL;
        }
    }
    else {
        if ( mle_size > SLEXEC_MLEPT_BYTES_COVERED ) {
            printk(SLEXEC_ERR"MLE size exceeds size allowable in low mem\n");
            return NULL;
        }
        ptab_size = SLEXEC_MLEPT_SIZE;
        ptab_base = (void*)SLEXEC_MLEPT_ADDR;
    }

    sl_memset(ptab_base, 0, ptab_size);
    printk(SLEXEC_DETA"ptab_size=%x, ptab_base=%p\n", ptab_size, ptab_base);

    pg_dir_ptr_tab = ptab_base;
    pg_dir         = pg_dir_ptr_tab + PAGE_SIZE;
    pg_tab         = pg_dir + PAGE_SIZE;

    /* only use first entry in page dir ptr table */
    *(uint64_t *)pg_dir_ptr_tab = MAKE_PDTE(pg_dir);

    /* start with first entry in page dir */
    *(uint64_t *)pg_dir = MAKE_PDTE(pg_tab);

    pte = pg_tab;
    pde = pg_dir;
    mle_off = 0;
    pd_off = 0;

    do {
        *pte = MAKE_PDTE(mle_start + mle_off);

        pte++;
        mle_off += PAGE_SIZE;

        pd_off++;
        if ( !(pd_off % 512) ) {
            /* break if we don't need any additional page entries */
            if (mle_off >= mle_size)
                break;
            pde++;
            *pde = MAKE_PDTE(pte);
        }
    } while ( mle_off < mle_size );

#if 0
    dump_page_tables(ptab_base);
#endif

    return ptab_base;
}


/* should be called after os_mle_data initialized */
static void *init_event_log(void)
{
    os_mle_data_t *os_mle_data = get_os_mle_data_start(get_txt_heap());
    g_elog = (event_log_container_t *)&os_mle_data->event_log_buffer;

    sl_memcpy((void *)g_elog->signature, EVTLOG_SIGNATURE,
           sizeof(g_elog->signature));
    g_elog->container_ver_major = EVTLOG_CNTNR_MAJOR_VER;
    g_elog->container_ver_minor = EVTLOG_CNTNR_MINOR_VER;
    g_elog->pcr_event_ver_major = EVTLOG_EVT_MAJOR_VER;
    g_elog->pcr_event_ver_minor = EVTLOG_EVT_MINOR_VER;
    //g_elog->size = sizeof(os_mle_data->event_log_buffer);
    g_elog->size = MAX_EVENT_LOG_SIZE;
    g_elog->pcr_events_offset = sizeof(*g_elog);
    g_elog->next_event_offset = sizeof(*g_elog);

    return (void *)g_elog;
}

/* initialize TCG compliant TPM 2.0 event log descriptor */
static void init_evtlog_desc_1(heap_event_log_ptr_elt2_1_t *evt_log)
{
    os_mle_data_t *os_mle_data = get_os_mle_data_start(get_txt_heap());

    evt_log->phys_addr = (uint64_t)(unsigned long)(os_mle_data->event_log_buffer);
    evt_log->allcoated_event_container_size = 2*PAGE_SIZE;
    evt_log->first_record_offset = 0;
    evt_log->next_record_offset = 0;
    printk(SLEXEC_DETA"TCG compliant TPM 2.0 event log descriptor:\n");
    printk(SLEXEC_DETA"\t phys_addr = 0x%LX\n",  evt_log->phys_addr);
    printk(SLEXEC_DETA"\t allcoated_event_container_size = 0x%x \n", evt_log->allcoated_event_container_size);
    printk(SLEXEC_DETA"\t first_record_offset = 0x%x \n", evt_log->first_record_offset);
    printk(SLEXEC_DETA"\t next_record_offset = 0x%x \n", evt_log->next_record_offset);
}

int get_evtlog_type(void)
{
    struct tpm_if *tpm = get_tpm();

    if (tpm->major == TPM12_VER_MAJOR) {
        return EVTLOG_TPM12;
    } else if (tpm->major == TPM20_VER_MAJOR) {
        /* TODO fix to only support TCG formait */
        if (g_sinit_module) {
            txt_caps_t sinit_caps = get_sinit_capabilities(g_sinit_module);
            return sinit_caps.tcg_event_log_format ? EVTLOG_TPM2_TCG : EVTLOG_TPM2_LEGACY;
        } else {
            printk(SLEXEC_ERR"SINIT not found\n");
        }
    } else {
        printk(SLEXEC_ERR"Unknown TPM major version: %d\n", tpm->major);
    }
    printk(SLEXEC_ERR"Unable to determine log type\n");
    return EVTLOG_UNKNOWN;
}

static void init_os_sinit_ext_data(heap_ext_data_element_t* elts)
{
    heap_ext_data_element_t* elt = elts;
    heap_event_log_ptr_elt_t* evt_log;
    int log_type = get_evtlog_type();

    if ( log_type == EVTLOG_TPM12 ) {
        evt_log = (heap_event_log_ptr_elt_t *)elt->data;
        evt_log->event_log_phys_addr = (uint64_t)(unsigned long)init_event_log();
        elt->type = HEAP_EXTDATA_TYPE_TPM_EVENT_LOG_PTR;
        elt->size = sizeof(*elt) + sizeof(*evt_log);
    } else if ( log_type == EVTLOG_TPM2_TCG ) {
        g_elog_2_1 = (heap_event_log_ptr_elt2_1_t *)elt->data;
        init_evtlog_desc_1(g_elog_2_1);
        elt->type = HEAP_EXTDATA_TYPE_TPM_EVENT_LOG_PTR_2_1;
        elt->size = sizeof(*elt) + sizeof(heap_event_log_ptr_elt2_1_t);
        printk(SLEXEC_DETA"heap_ext_data_element TYPE = %d \n", elt->type);
        printk(SLEXEC_DETA"heap_ext_data_element SIZE = %d \n", elt->size);
    }
#if 0 /* TODO we only support TCG event log right? I don't think we care about the extpol stuff */
    }  else if ( log_type == EVTLOG_TPM2_LEGACY ) {
        g_elog_2 = (heap_event_log_ptr_elt2_t *)elt->data;
        if ( tpm->extpol == TB_EXTPOL_AGILE )
            g_elog_2->count = tpm->banks;
        else
            if ( tpm->extpol == TB_EXTPOL_EMBEDDED )
                g_elog_2->count = tpm->alg_count;
            else
                g_elog_2->count = 1;
        init_evtlog_desc(g_elog_2);
        elt->type = HEAP_EXTDATA_TYPE_TPM_EVENT_LOG_PTR_2;
        elt->size = sizeof(*elt) + sizeof(u32) +
            g_elog_2->count * sizeof(heap_event_log_descr_t);
        printk(SLEXEC_DETA"INTEL TXT LOG elt SIZE = %d \n", elt->size);
    }
#endif

    elt = (void *)elt + elt->size;
    elt->type = HEAP_EXTDATA_TYPE_END;
    elt->size = sizeof(*elt);
}

static void set_vtd_pmrs(os_sinit_data_t *os_sinit_data,
                         uint64_t min_lo_ram, uint64_t max_lo_ram,
                         uint64_t min_hi_ram, uint64_t max_hi_ram)
{
    printk(SLEXEC_DETA"min_lo_ram: 0x%Lx, max_lo_ram: 0x%Lx\n", min_lo_ram, max_lo_ram);
    printk(SLEXEC_DETA"min_hi_ram: 0x%Lx, max_hi_ram: 0x%Lx\n", min_hi_ram, max_hi_ram);

    /*
     * base must be 2M-aligned and size must be multiple of 2M
     * (so round bases and sizes down--rounding size up might conflict
     *  with a BIOS-reserved region and cause problems; in practice, rounding
     *  base down doesn't)
     * we want to protect all of usable mem so that any kernel allocations
     * before VT-d remapping is enabled are protected
     */

    min_lo_ram &= ~0x1fffffULL;
    uint64_t lo_size = (max_lo_ram - min_lo_ram) & ~0x1fffffULL;
    os_sinit_data->vtd_pmr_lo_base = min_lo_ram;
    os_sinit_data->vtd_pmr_lo_size = lo_size;

    min_hi_ram &= ~0x1fffffULL;
    uint64_t hi_size = (max_hi_ram - min_hi_ram) & ~0x1fffffULL;
    os_sinit_data->vtd_pmr_hi_base = min_hi_ram;
    os_sinit_data->vtd_pmr_hi_size = hi_size;
}

/*
 * sets up TXT heap
 */
static txt_heap_t *init_txt_heap(void *ptab_base, acm_hdr_t *sinit, loader_ctx *lctx)
{
    txt_heap_t *txt_heap;
    uint64_t *size;
    uint32_t *mle_size;
    struct tpm_if *tpm = get_tpm();
    os_mle_data_t *os_mle_data;
    struct kernel_info *ki;
    uint32_t version;
    uint64_t min_lo_ram, max_lo_ram, min_hi_ram, max_hi_ram;

    txt_heap = get_txt_heap();

    /*
     * BIOS data already setup by BIOS
     */
    if ( !verify_bios_data(txt_heap) )
        return NULL;

    /*
     * OS/loader to MLE data
     */
    os_mle_data = get_os_mle_data_start(txt_heap);
    size = (uint64_t *)((uint32_t)os_mle_data - sizeof(uint64_t));
    *size = sizeof(*os_mle_data) + sizeof(uint64_t);
    sl_memset(os_mle_data, 0, sizeof(*os_mle_data));
    /* set the zero page addr here */
    /* NOTE msb_key_hash is not currently used and the log is setup later */
    os_mle_data = get_os_mle_data_start(txt_heap);
    os_mle_data->zero_page_addr = (uint32_t)g_sl_kernel_setup.boot_params;
    printk(SLEXEC_DETA"Zero page addr: 0x%x\n", os_mle_data->zero_page_addr);
    os_mle_data->version = OS_MLE_STRUCT_VERSION;
    os_mle_data->saved_misc_enable_msr = rdmsr(MSR_IA32_MISC_ENABLE);
    /* might as well save the MTRR state here where OS-MLE is setup */
    save_mtrrs(&(os_mle_data->saved_mtrr_state));
    /* provide AP wake code block area */
    sl_memset((void*)SLEXEC_AP_WAKE_BLOCK_ADDR, 0, SLEXEC_AP_WAKE_BLOCK_SIZE);
    os_mle_data->ap_wake_block = SLEXEC_AP_WAKE_BLOCK_ADDR;
    os_mle_data->ap_wake_block_size = SLEXEC_AP_WAKE_BLOCK_SIZE;
    printk(SLEXEC_DETA"AP wake  addr: 0x%x size: 0x%x\n", (uint32_t)os_mle_data->ap_wake_block,
           (uint32_t)os_mle_data->ap_wake_block_size);
    /* event log and size */
    os_mle_data->evtlog_addr = (uint32_t)&os_mle_data->event_log_buffer;
    os_mle_data->evtlog_size = MAX_EVENT_LOG_SIZE;
    printk(SLEXEC_DETA"Event log addr: 0x%x\n", (uint32_t)os_mle_data->evtlog_addr);

    /*
     * OS/loader to SINIT data
     */
    /* check sinit supported os_sinit_data version */
    version = get_supported_os_sinit_data_ver(sinit);
    if ( version < MIN_OS_SINIT_DATA_VER ) {
        printk(SLEXEC_ERR"unsupported OS to SINIT data version(%u) in sinit\n",
               version);
        return NULL;
    }
    if ( version > MAX_OS_SINIT_DATA_VER )
        version = MAX_OS_SINIT_DATA_VER;

    ki = (struct kernel_info*)(g_sl_kernel_setup.protected_mode_base +
            g_sl_kernel_setup.boot_params->hdr.slaunch_header);
    g_slaunch_header = ki->mle_header_offset;

    os_sinit_data_t *os_sinit_data = get_os_sinit_data_start(txt_heap);
    size = (uint64_t *)((uint32_t)os_sinit_data - sizeof(uint64_t));
    *size = calc_os_sinit_data_size(version);
    sl_memset(os_sinit_data, 0, *size);
    os_sinit_data->version = version;

    mle_size = (uint32_t*)(g_sl_kernel_setup.protected_mode_base + g_slaunch_header);
    /* this is phys addr */
    os_sinit_data->mle_ptab = (uint64_t)(unsigned long)ptab_base;
    if (*(mle_size + 9) != 0) {
        printk("Protected Mode Size: 0x%x MLE Reported Size: 0x%x\n",
              (uint32_t)g_sl_kernel_setup.protected_mode_size, *(mle_size + 9));
        os_sinit_data->mle_size = *(mle_size + 9);
        printk("MLE size set to MLE header reported size: 0x%x\n",
               (uint32_t)os_sinit_data->mle_size);
    } else
        os_sinit_data->mle_size = g_sl_kernel_setup.protected_mode_size;

    /* this is linear addr (offset from MLE base) of mle header */
    os_sinit_data->mle_hdr_base = g_slaunch_header;

    /* VT-d PMRs */
    if ( !get_ram_ranges(&min_lo_ram, &max_lo_ram, &min_hi_ram, &max_hi_ram) )
        return NULL;

    set_vtd_pmrs(os_sinit_data, min_lo_ram, max_lo_ram, min_hi_ram,
                 max_hi_ram);

    /* capabilities : choose monitor wake mechanism first */
    txt_caps_t sinit_caps = get_sinit_capabilities(sinit);
    txt_caps_t caps_mask = { 0 };
    caps_mask.rlp_wake_getsec = 1;
    caps_mask.rlp_wake_monitor = 1;
    caps_mask.pcr_map_da = 1;
    caps_mask.tcg_event_log_format = 1;
    caps_mask.tcg_event_log_format = 1;
    os_sinit_data->capabilities._raw = MLE_HDR_CAPS & ~caps_mask._raw;
    if ( sinit_caps.rlp_wake_monitor )
        os_sinit_data->capabilities.rlp_wake_monitor = 1;
    else if ( sinit_caps.rlp_wake_getsec )
        os_sinit_data->capabilities.rlp_wake_getsec = 1;
    else {     /* should have been detected in verify_acmod() */
        printk(SLEXEC_ERR"SINIT capabilities are incompatible (0x%x)\n", sinit_caps._raw);
        return NULL;
    }
    if ( get_evtlog_type() == EVTLOG_TPM2_TCG ) {
        printk(SLEXEC_INFO"SINIT ACM supports TCG compliant TPM 2.0 event log format, tcg_event_log_format = %d \n",
              sinit_caps.tcg_event_log_format);
        os_sinit_data->capabilities.tcg_event_log_format = 1;
    }

    /* capabilities : require MLE pagetable in ECX on launch */
    /* TODO: when SINIT ready
     * os_sinit_data->capabilities.ecx_pgtbl = 1;
     */
    os_sinit_data->capabilities.ecx_pgtbl = 0;
    if (is_loader_launch_efi(lctx)){
        /* we were launched EFI, set efi_rsdt_ptr */
        struct acpi_rsdp *rsdp = get_rsdp(lctx);
        if (rsdp != NULL){
            if (version < 6){
                /* rsdt */
                /* NOTE: Winston Wang says this doesn't work for v5 */
                os_sinit_data->efi_rsdt_ptr = (uint64_t) rsdp->rsdp1.rsdt;
            } else {
                /* rsdp */
                os_sinit_data->efi_rsdt_ptr = (uint64_t)((uint32_t)rsdp);
            }
        } else {
            /* per discussions--if we don't have an ACPI pointer, die */
            printk(SLEXEC_ERR"Failed to find RSDP for EFI launch\n");
            return NULL;
        }
    }

    /* capabilities : choose DA/LG */
    os_sinit_data->capabilities.pcr_map_no_legacy = 1;
    if ( sinit_caps.pcr_map_da && get_slexec_prefer_da() )
        os_sinit_data->capabilities.pcr_map_da = 1;
    else if ( !sinit_caps.pcr_map_no_legacy )
        os_sinit_data->capabilities.pcr_map_no_legacy = 0;
    else if ( sinit_caps.pcr_map_da ) {
        printk(SLEXEC_INFO
               "DA is the only supported PCR mapping by SINIT, use it\n");
        os_sinit_data->capabilities.pcr_map_da = 1;
    }
    else {
        printk(SLEXEC_ERR"SINIT capabilities are incompatible (0x%x)\n",
               sinit_caps._raw);
        return NULL;
    }
    g_using_da = os_sinit_data->capabilities.pcr_map_da;

    /* PCR mapping selection MUST be zero in TPM2.0 mode
     * since D/A mapping is the only supported by TPM2.0 */
    if ( tpm->major >= TPM20_VER_MAJOR ) {
        /* don't support ExtPol options and only TCG TPM 2.0 log format */
        /* os_sinit_data->flags = (tpm->extpol == SL_EXTPOL_AGILE) ? 0 : 1; */
        os_sinit_data->flags = 1; /* Maximum Performance Policy */
        os_sinit_data->capabilities.pcr_map_no_legacy = 0;
        os_sinit_data->capabilities.pcr_map_da = 0;
        g_using_da = 1;
    }

    /* Event log initialization */
    if ( os_sinit_data->version >= 6 )
        init_os_sinit_ext_data(os_sinit_data->ext_data_elts);

    print_os_sinit_data(os_sinit_data);

    /*
     * SINIT to MLE data will be setup by SINIT
     */

    return txt_heap;
}

int txt_launch_environment(loader_ctx *lctx)
{
    void *mle_ptab_base;
    txt_heap_t *txt_heap;
    uint32_t *mle_size;

    /* print some debug info */
    print_file_info();

    /* create MLE page table */
    mle_ptab_base = build_mle_pagetable();
    if ( mle_ptab_base == NULL )
        return SL_ERR_FATAL;

    /* initialize TXT heap */
    txt_heap = init_txt_heap(mle_ptab_base, g_sinit_module, lctx);
    if ( txt_heap == NULL )
        return SL_ERR_TXT_NOT_SUPPORTED;

    /* set MTRRs properly for AC module (SINIT) */
    if ( !set_mtrrs_for_acmod(g_sinit_module) )
        return SL_ERR_FATAL;

    /* deactivate current locality */
    /* TODO why is it not done for 1.2 w/ release_locality() ? */
    if (g_tpm_family == TPM_IF_20_CRB ) {
        printk(SLEXEC_INFO"Relinquish CRB localility 0 before executing GETSEC[SENTER]...\n");
        if (!tpm_relinquish_locality_crb(0)){
            printk(SLEXEC_INFO"Relinquish CRB locality 0 failed...\n");
            error_action(SL_ERR_TPM_NOT_READY);
        }
    }

    /*
     * Need to update the MLE header with the size of the MLE. The field is
     * the 9th dword in.
     */
    mle_size = (uint32_t*)(g_sl_kernel_setup.protected_mode_base + g_slaunch_header);
    if (*(mle_size + 9) == 0) {
        printk("Protected Mode Size: 0x%x MLE Reported Size: 0x%x\n",
              (uint32_t)g_sl_kernel_setup.protected_mode_size, *(mle_size + 9));
        printk("Setting MLE size\n");
        *(mle_size + 9) = g_sl_kernel_setup.protected_mode_size;
    }

    printk(SLEXEC_INFO"executing GETSEC[SENTER]...\n");
    /* (optionally) pause before executing GETSEC[SENTER] */
    if ( g_vga_delay > 0 )
        delay(g_vga_delay * 1000);
    __getsec_senter((uint32_t)g_sinit_module, (g_sinit_module->size)*4);
    printk(SLEXEC_INFO"ERROR--we should not get here!\n");
    return SL_ERR_FATAL;
}

bool txt_is_powercycle_required(void)
{
    /* a powercycle is required to clear the TXT_RESET.STS flag */
    txt_ests_t ests = (txt_ests_t)read_pub_config_reg(TXTCR_ESTS);
    return ests.txt_reset_sts;
}

bool smx_get_parameters(getsec_parameters_t *params)
{
    unsigned long cr4;
    uint32_t index, eax, ebx, ecx;
    int param_type;

    /* sanity check because GETSEC[PARAMETERS] will fail if not set */
    cr4 = read_cr4();
    if ( !(cr4 & CR4_SMXE) ) {
        printk(SLEXEC_ERR"SMXE not enabled, can't read parameters\n");
        return false;
    }

    sl_memset(params, 0, sizeof(*params));
    params->acm_max_size = DEF_ACM_MAX_SIZE;
    params->acm_mem_types = DEF_ACM_MEM_TYPES;
    params->senter_controls = DEF_SENTER_CTRLS;
    params->proc_based_scrtm = false;
    params->preserve_mce = false;

    index = 0;
    do {
        __getsec_parameters(index++, &param_type, &eax, &ebx, &ecx);
        /* the code generated for a 'switch' statement doesn't work in this */
        /* environment, so use if/else blocks instead */

        /* NULL - all reserved */
        if ( param_type == 0 )
            ;
        /* supported ACM versions */
        else if ( param_type == 1 ) {
            if ( params->n_versions == MAX_SUPPORTED_ACM_VERSIONS )
                printk(SLEXEC_WARN"number of supported ACM version exceeds "
                       "MAX_SUPPORTED_ACM_VERSIONS\n");
            else {
                params->acm_versions[params->n_versions].mask = ebx;
                params->acm_versions[params->n_versions].version = ecx;
                params->n_versions++;
            }
        }
        /* max size AC execution area */
        else if ( param_type == 2 )
            params->acm_max_size = eax & 0xffffffe0;
        /* supported non-AC mem types */
        else if ( param_type == 3 )
            params->acm_mem_types = eax & 0xffffffe0;
        /* SENTER controls */
        else if ( param_type == 4 )
            params->senter_controls = (eax & 0x00007fff) >> 8;
        /* TXT extensions support */
        else if ( param_type == 5 ) {
            params->proc_based_scrtm = (eax & 0x00000020) ? true : false;
            params->preserve_mce = (eax & 0x00000040) ? true : false;
        }
        else {
            printk(SLEXEC_WARN"unknown GETSEC[PARAMETERS] type: %d\n",
                   param_type);
            param_type = 0;    /* set so that we break out of the loop */
        }
    } while ( param_type != 0 );

    if ( params->n_versions == 0 ) {
        params->acm_versions[0].mask = DEF_ACM_VER_MASK;
        params->acm_versions[0].version = DEF_ACM_VER_SUPPORTED;
        params->n_versions = 1;
    }

    return true;
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
