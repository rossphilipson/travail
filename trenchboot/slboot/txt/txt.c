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

#include <config.h>
#include <stdbool.h>
#include <types.h>
#include <tb_error.h>
#include <msr.h>
#include <compiler.h>
#include <string.h>
#include <misc.h>
#include <page.h>
#include <processor.h>
#include <printk.h>
#include <atomic.h>
#include <tpm.h>
#include <uuid.h>
#include <loader.h>
#include <e820.h>
#include <slboot.h>
#include <mle.h>
#include <hash.h>
#include <cmdline.h>
#include <acpi.h>
#include <txt/txt.h>
#include <txt/config_regs.h>
#include <txt/mtrrs.h>
#include <txt/heap.h>
#include <txt/acmod.h>
#include <txt/smx.h>
#include <txt/verify.h>
#include <io.h>

/* counter timeout for waiting for all APs to enter wait-for-sipi */
#define AP_WFS_TIMEOUT     0x10000000

__data struct acpi_rsdp g_rsdp;
extern char _start[];             /* start of module */
extern char _end[];               /* end of module */
extern char _mle_start[];         /* start of text section */
extern char _mle_end[];           /* end of text section */
/*extern char _post_launch_entry[];*/ /* entry point post SENTER, in boot.S */

extern long s3_flag;

/* MLE/kernel shared data page (in boot.S) */
extern void print_event(const tpm12_pcr_event_t *evt);
extern void print_event_2(void *evt, uint16_t alg);
extern uint32_t print_event_2_1(void *evt);

/*
 * this is the structure whose addr we'll put in TXT heap
 * it needs to be within the MLE pages, so force it to the .text section
 */
/* TODO the MLE header will be elsewhere */
static __text const mle_hdr_t g_mle_hdr = {
    uuid              :  MLE_HDR_UUID,
    length            :  sizeof(mle_hdr_t),
    version           :  MLE_HDR_VER,
    entry_point       :  0, /*(uint32_t)&_post_launch_entry - TBOOT_START,*/
    first_valid_page  :  0,
    mle_start_off     :  (uint32_t)&_mle_start - TBOOT_BASE_ADDR,
    mle_end_off       :  (uint32_t)&_mle_end - TBOOT_BASE_ADDR,
    capabilities      :  { MLE_HDR_CAPS },
    cmdline_start_off :  (uint32_t)g_cmdline - TBOOT_BASE_ADDR,
    cmdline_end_off   :  (uint32_t)g_cmdline + CMDLINE_SIZE - 1 -
                                                       TBOOT_BASE_ADDR,
};

/*
 * counts of APs going into wait-for-sipi
 */
/* count of APs in WAIT-FOR-SIPI */
atomic_t ap_wfs_count;

static void print_file_info(void)
{
    printk(TBOOT_DETA"file addresses:\n");
    printk(TBOOT_DETA"\t &_start=%p\n", &_start);
    printk(TBOOT_DETA"\t &_end=%p\n", &_end);
    printk(TBOOT_DETA"\t &_mle_start=%p\n", &_mle_start);
    printk(TBOOT_DETA"\t &_mle_end=%p\n", &_mle_end);
}

/*
 * build_mle_pagetable()
 */

/* page dir/table entry is phys addr + P + R/W + PWT */
#define MAKE_PDTE(addr)  (((uint64_t)(unsigned long)(addr) & PAGE_MASK) | 0x01)

/* we assume/know that our image is <2MB and thus fits w/in a single */
/* PT (512*4KB = 2MB) and thus fixed to 1 pg dir ptr and 1 pgdir and */
/* 1 ptable = 3 pages and just 1 loop loop for ptable MLE page table */
/* can only contain 4k pages */

static __mlept uint8_t g_mle_pt[3 * PAGE_SIZE];
/* pgdir ptr + pgdir + ptab = 3 */

static void *build_mle_pagetable(uint32_t mle_start, uint32_t mle_size)
{
    void *ptab_base;
    uint32_t ptab_size, mle_off;
    void *pg_dir_ptr_tab, *pg_dir, *pg_tab;
    uint64_t *pte;

    printk(TBOOT_DETA"MLE start=0x%x, end=0x%x, size=0x%x\n",
           mle_start, mle_start+mle_size, mle_size);
    if ( mle_size > 512*PAGE_SIZE ) {
        printk(TBOOT_ERR"MLE size too big for single page table\n");
        return NULL;
    }


    /* should start on page boundary */
    if ( mle_start & ~PAGE_MASK ) {
        printk(TBOOT_ERR"MLE start is not page-aligned\n");
        return NULL;
    }

    /* place ptab_base below MLE */
    ptab_size = sizeof(g_mle_pt);
    ptab_base = &g_mle_pt;
    tb_memset(ptab_base, 0, ptab_size);
    printk(TBOOT_DETA"ptab_size=%x, ptab_base=%p\n", ptab_size, ptab_base);

    pg_dir_ptr_tab = ptab_base;
    pg_dir         = pg_dir_ptr_tab + PAGE_SIZE;
    pg_tab         = pg_dir + PAGE_SIZE;

    /* only use first entry in page dir ptr table */
    *(uint64_t *)pg_dir_ptr_tab = MAKE_PDTE(pg_dir);

    /* only use first entry in page dir */
    *(uint64_t *)pg_dir = MAKE_PDTE(pg_tab);

    pte = pg_tab;
    mle_off = 0;
    do {
        *pte = MAKE_PDTE(mle_start + mle_off);

        pte++;
        mle_off += PAGE_SIZE;
    } while ( mle_off < mle_size );

    return ptab_base;
}


static __data event_log_container_t *g_elog = NULL;
static __data heap_event_log_ptr_elt2_t *g_elog_2 = NULL;
static __data heap_event_log_ptr_elt2_1_t *g_elog_2_1 = NULL;

/* should be called after os_mle_data initialized */
static void *init_event_log(void)
{
    os_mle_data_t *os_mle_data = get_os_mle_data_start(get_txt_heap());
    g_elog = (event_log_container_t *)&os_mle_data->event_log_buffer;

    tb_memcpy((void *)g_elog->signature, EVTLOG_SIGNATURE,
           sizeof(g_elog->signature));
    g_elog->container_ver_major = EVTLOG_CNTNR_MAJOR_VER;
    g_elog->container_ver_minor = EVTLOG_CNTNR_MINOR_VER;
    g_elog->pcr_event_ver_major = EVTLOG_EVT_MAJOR_VER;
    g_elog->pcr_event_ver_minor = EVTLOG_EVT_MINOR_VER;
    g_elog->size = sizeof(os_mle_data->event_log_buffer);
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
    printk(TBOOT_DETA"TCG compliant TPM 2.0 event log descriptor:\n");
    printk(TBOOT_DETA"\t phys_addr = 0x%LX\n",  evt_log->phys_addr);
    printk(TBOOT_DETA"\t allcoated_event_container_size = 0x%x \n", evt_log->allcoated_event_container_size);
    printk(TBOOT_DETA"\t first_record_offset = 0x%x \n", evt_log->first_record_offset);
    printk(TBOOT_DETA"\t next_record_offset = 0x%x \n", evt_log->next_record_offset);
}

static void init_evtlog_desc(heap_event_log_ptr_elt2_t *evt_log)
{
    unsigned int i;
    os_mle_data_t *os_mle_data = get_os_mle_data_start(get_txt_heap());
    struct tpm_if *tpm = get_tpm();
    switch (tpm->extpol) {
    case TB_EXTPOL_AGILE:
        for (i=0; i<evt_log->count; i++) {
            evt_log->event_log_descr[i].alg = tpm->algs_banks[i];
            evt_log->event_log_descr[i].phys_addr =
                    (uint64_t)(unsigned long)(os_mle_data->event_log_buffer + i*4096);
            evt_log->event_log_descr[i].size = 4096;
            evt_log->event_log_descr[i].pcr_events_offset = 0;
            evt_log->event_log_descr[i].next_event_offset = 0;
        }
        break;
    case TB_EXTPOL_EMBEDDED:
        for (i=0; i<evt_log->count; i++) {
            evt_log->event_log_descr[i].alg = tpm->algs[i];
            evt_log->event_log_descr[i].phys_addr =
                    (uint64_t)(unsigned long)(os_mle_data->event_log_buffer + i*4096);
            evt_log->event_log_descr[i].size = 4096;
            evt_log->event_log_descr[i].pcr_events_offset = 0;
            evt_log->event_log_descr[i].next_event_offset = 0;
        }
        break;
    case TB_EXTPOL_FIXED:
        evt_log->event_log_descr[0].alg = tpm->cur_alg;
        evt_log->event_log_descr[0].phys_addr =
                    (uint64_t)(unsigned long)os_mle_data->event_log_buffer;
        evt_log->event_log_descr[0].size = 4096;
        evt_log->event_log_descr[0].pcr_events_offset = 0;
        evt_log->event_log_descr[0].next_event_offset = 0;
        break;
    default:
        return;
    }
}

int get_evtlog_type(void)
{
    struct tpm_if *tpm = get_tpm();

    if (tpm->major == TPM12_VER_MAJOR) {
        return EVTLOG_TPM12;
    } else if (tpm->major == TPM20_VER_MAJOR) {
        /*
         * Force use of legacy TPM2 log format to deal with a bug in some SINIT
         * ACMs that where they don't log the MLE hash to the event log.
         */
        if (get_tboot_force_tpm2_legacy_log()) {
            return EVTLOG_TPM2_LEGACY;
        }
        if (g_sinit) {
            txt_caps_t sinit_caps = get_sinit_capabilities(g_sinit);
            return sinit_caps.tcg_event_log_format ? EVTLOG_TPM2_TCG : EVTLOG_TPM2_LEGACY;
        } else {
            printk(TBOOT_ERR"SINIT not found\n");
        }
    } else {
        printk(TBOOT_ERR"Unknown TPM major version: %d\n", tpm->major);
    }
    printk(TBOOT_ERR"Unable to determine log type\n");
    return EVTLOG_UNKNOWN;
}

static void init_os_sinit_ext_data(heap_ext_data_element_t* elts)
{
    heap_ext_data_element_t* elt = elts;
    heap_event_log_ptr_elt_t* evt_log;
    struct tpm_if *tpm = get_tpm();

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
        printk(TBOOT_DETA"heap_ext_data_element TYPE = %d \n", elt->type);
        printk(TBOOT_DETA"heap_ext_data_element SIZE = %d \n", elt->size);
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
        printk(TBOOT_DETA"INTEL TXT LOG elt SIZE = %d \n", elt->size);
    }

    elt = (void *)elt + elt->size;
    elt->type = HEAP_EXTDATA_TYPE_END;
    elt->size = sizeof(*elt);
}

bool evtlog_append_tpm12(uint8_t pcr, tb_hash_t *hash, uint32_t type)
{
    if ( g_elog == NULL )
        return true;

    tpm12_pcr_event_t *next = (tpm12_pcr_event_t *)
                              ((void*)g_elog + g_elog->next_event_offset);

    if ( g_elog->next_event_offset + sizeof(*next) > g_elog->size )
        return false;

    next->pcr_index = pcr;
    next->type = type;
    tb_memcpy(next->digest, hash, sizeof(next->digest));
    next->data_size = 0;

    g_elog->next_event_offset += sizeof(*next) + next->data_size;

    print_event(next);
    return true;
}

void dump_event_2(void)
{
    heap_event_log_descr_t *log_descr;

    for ( unsigned int i=0; i<g_elog_2->count; i++ ) {
        log_descr = &g_elog_2->event_log_descr[i];
        printk(TBOOT_DETA"\t\t\t Log Descrption:\n");
        printk(TBOOT_DETA"\t\t\t             Alg: %u\n", log_descr->alg);
        printk(TBOOT_DETA"\t\t\t            Size: %u\n", log_descr->size);
        printk(TBOOT_DETA"\t\t\t    EventsOffset: [%u,%u)\n",
                log_descr->pcr_events_offset,
                log_descr->next_event_offset);

        uint32_t hash_size, data_size;
        hash_size = get_hash_size(log_descr->alg);
        if ( hash_size == 0 )
            return;

        void *curr, *next;
        *((u64 *)(&curr)) = log_descr->phys_addr +
                log_descr->pcr_events_offset;
        *((u64 *)(&next)) = log_descr->phys_addr +
                log_descr->next_event_offset;

        if ( log_descr->alg != TB_HALG_SHA1 ) {
            print_event_2(curr, TB_HALG_SHA1);
            curr += sizeof(tpm12_pcr_event_t) + sizeof(tpm20_log_descr_t);
        }

        while ( curr < next ) {
            print_event_2(curr, log_descr->alg);
            data_size = *(uint32_t *)(curr + 2*sizeof(uint32_t) + hash_size);
            curr += 3*sizeof(uint32_t) + hash_size + data_size;
        }
    }
}

bool evtlog_append_tpm2_legacy(uint8_t pcr, uint16_t alg, tb_hash_t *hash, uint32_t type)
{
    heap_event_log_descr_t *cur_desc = NULL;
    uint32_t hash_size;
    void *cur, *next;

    for ( unsigned int i=0; i<g_elog_2->count; i++ ) {
        if ( g_elog_2->event_log_descr[i].alg == alg ) {
            cur_desc = &g_elog_2->event_log_descr[i];
            break;
        }
    }
    if ( !cur_desc )
        return false;

    hash_size = get_hash_size(alg);
    if ( hash_size == 0 )
        return false;

    if ( cur_desc->next_event_offset + 32 > cur_desc->size )
        return false;

    cur = next = (void *)(unsigned long)cur_desc->phys_addr +
                     cur_desc->next_event_offset;
    *((u32 *)next) = pcr;
    next += sizeof(u32);
    *((u32 *)next) = type;
    next += sizeof(u32);
    tb_memcpy((uint8_t *)next, hash, hash_size);
    next += hash_size;
    *((u32 *)next) = 0;
    cur_desc->next_event_offset += 3*sizeof(uint32_t) + hash_size;

    print_event_2(cur, alg);
    return true;
}

bool evtlog_append_tpm2_tcg(uint8_t pcr, uint32_t type, hash_list_t *hl)
{
    uint32_t i, event_size;
    unsigned int hash_size;
    tcg_pcr_event2 *event;
    uint8_t *hash_entry;
    tcg_pcr_event2 dummy;

    /*
     * Dont't use sizeof(tcg_pcr_event2) since that has TPML_DIGESTV_VALUES_1.digests
     * set to 5. Compute the static size as pcr_index + event_type +
     * digest.count + event_size. Then add the space taken up by the hashes.
     */
    event_size = sizeof(dummy.pcr_index) + sizeof(dummy.event_type) +
        sizeof(dummy.digest.count) + sizeof(dummy.event_size);

    for (i = 0; i < hl->count; i++) {
        hash_size = get_hash_size(hl->entries[i].alg);
        if (hash_size == 0) {
            return false;
        }
        event_size += sizeof(uint16_t); // hash_alg field
        event_size += hash_size;
    }

    // Check if event will fit in buffer.
    if (event_size + g_elog_2_1->next_record_offset >
        g_elog_2_1->allcoated_event_container_size) {
        return false;
    }

    event = (tcg_pcr_event2*)(void *)(unsigned long)g_elog_2_1->phys_addr +
        g_elog_2_1->next_record_offset;
    event->pcr_index = pcr;
    event->event_type = type;
    event->event_size = 0;  // No event data passed by tboot.
    event->digest.count = hl->count;

    hash_entry = (uint8_t *)&event->digest.digests[0];
    for (i = 0; i < hl->count; i++) {
        // Populate individual TPMT_HA_1 structs.
        *((uint16_t *)hash_entry) = hl->entries[i].alg; // TPMT_HA_1.hash_alg
        hash_entry += sizeof(uint16_t);
        hash_size = get_hash_size(hl->entries[i].alg);  // already checked above
        tb_memcpy(hash_entry, &(hl->entries[i].hash), hash_size);
        hash_entry += hash_size;
    }

    g_elog_2_1->next_record_offset += event_size;
    print_event_2_1(event);
    return true;
}

bool evtlog_append(uint8_t pcr, hash_list_t *hl, uint32_t type)
{
    int log_type = get_evtlog_type();
    switch (log_type) {
    case EVTLOG_TPM12:
        if ( !evtlog_append_tpm12(pcr, &hl->entries[0].hash, type) )
            return false;
        break;
    case EVTLOG_TPM2_LEGACY:
        for (unsigned int i=0; i<hl->count; i++) {
            if ( !evtlog_append_tpm2_legacy(pcr, hl->entries[i].alg,
                &hl->entries[i].hash, type))
                return false;
	    }
        break;
    case EVTLOG_TPM2_TCG:
        if ( !evtlog_append_tpm2_tcg(pcr, type, hl) )
            return false;
        break;
    default:
        return false;
    }

    return true;
}

__data uint32_t g_using_da = 0;
__data acm_hdr_t *g_sinit = 0;

/*
 * sets up TXT heap
 */
static txt_heap_t *init_txt_heap(void *ptab_base, acm_hdr_t *sinit, loader_ctx *lctx)
{
    txt_heap_t *txt_heap;
    uint64_t *size;
    struct tpm_if *tpm = get_tpm();

    txt_heap = get_txt_heap();

    /*
     * BIOS data already setup by BIOS
     */
    if ( !verify_bios_data(txt_heap) )
        return NULL;

    /*
     * OS/loader to MLE data
     */
    os_mle_data_t *os_mle_data = get_os_mle_data_start(txt_heap);
    size = (uint64_t *)((uint32_t)os_mle_data - sizeof(uint64_t));
    *size = sizeof(*os_mle_data) + sizeof(uint64_t);
    tb_memset(os_mle_data, 0, sizeof(*os_mle_data));

    /*
     * OS/loader to SINIT data
     */
    /* check sinit supported os_sinit_data version */
    uint32_t version = get_supported_os_sinit_data_ver(sinit);
    if ( version < MIN_OS_SINIT_DATA_VER ) {
        printk(TBOOT_ERR"unsupported OS to SINIT data version(%u) in sinit\n",
               version);
        return NULL;
    }
    if ( version > MAX_OS_SINIT_DATA_VER )
        version = MAX_OS_SINIT_DATA_VER;

    os_sinit_data_t *os_sinit_data = get_os_sinit_data_start(txt_heap);
    size = (uint64_t *)((uint32_t)os_sinit_data - sizeof(uint64_t));
    *size = calc_os_sinit_data_size(version);
    tb_memset(os_sinit_data, 0, *size);
    os_sinit_data->version = version;

    /* this is phys addr */
    os_sinit_data->mle_ptab = (uint64_t)(unsigned long)ptab_base;
    os_sinit_data->mle_size = g_mle_hdr.mle_end_off - g_mle_hdr.mle_start_off;
    /* this is linear addr (offset from MLE base) of mle header */
    os_sinit_data->mle_hdr_base = (uint64_t)(unsigned long)&g_mle_hdr -
        (uint64_t)(unsigned long)&_mle_start;
    /* VT-d PMRs */
    uint64_t min_lo_ram, max_lo_ram, min_hi_ram, max_hi_ram;

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
        printk(TBOOT_ERR"SINIT capabilities are incompatible (0x%x)\n", sinit_caps._raw);
        return NULL;
    }
    if ( get_evtlog_type() == EVTLOG_TPM2_TCG ) {
        printk(TBOOT_INFO"SINIT ACM supports TCG compliant TPM 2.0 event log format, tcg_event_log_format = %d \n",
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
                tb_memcpy((void *)&g_rsdp, rsdp, sizeof(struct acpi_rsdp));
                os_sinit_data->efi_rsdt_ptr = (uint64_t)((uint32_t)&g_rsdp);
            }
        } else {
            /* per discussions--if we don't have an ACPI pointer, die */
            printk(TBOOT_ERR"Failed to find RSDP for EFI launch\n");
            return NULL;
        }
    }

    /* capabilities : choose DA/LG */
    os_sinit_data->capabilities.pcr_map_no_legacy = 1;
    if ( sinit_caps.pcr_map_da && get_tboot_prefer_da() )
        os_sinit_data->capabilities.pcr_map_da = 1;
    else if ( !sinit_caps.pcr_map_no_legacy )
        os_sinit_data->capabilities.pcr_map_no_legacy = 0;
    else if ( sinit_caps.pcr_map_da ) {
        printk(TBOOT_INFO
               "DA is the only supported PCR mapping by SINIT, use it\n");
        os_sinit_data->capabilities.pcr_map_da = 1;
    }
    else {
        printk(TBOOT_ERR"SINIT capabilities are incompatible (0x%x)\n",
               sinit_caps._raw);
        return NULL;
    }
    g_using_da = os_sinit_data->capabilities.pcr_map_da;

    /* PCR mapping selection MUST be zero in TPM2.0 mode
     * since D/A mapping is the only supported by TPM2.0 */
    if ( tpm->major >= TPM20_VER_MAJOR ) {
        os_sinit_data->flags = (tpm->extpol == TB_EXTPOL_AGILE) ? 0 : 1;
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

bool txt_is_launched(void)
{
    txt_sts_t sts;

    sts._raw = read_pub_config_reg(TXTCR_STS);

    return sts.senter_done_sts;
}

tb_error_t txt_launch_environment(loader_ctx *lctx)
{
    void *mle_ptab_base;
    os_mle_data_t *os_mle_data;
    txt_heap_t *txt_heap;

    /*
     * find correct SINIT AC module in modules list
     */
    // find_platform_sinit_module(lctx, (void **)&g_sinit, NULL);
    /* if it is newer than BIOS-provided version, then copy it to */
    /* BIOS reserved region */
    // g_sinit = copy_sinit(g_sinit);
    // if ( g_sinit == NULL )
    //    return TB_ERR_SINIT_NOT_PRESENT;
    /* do some checks on it */
    // if ( !verify_acmod(g_sinit) )
     //   return TB_ERR_ACMOD_VERIFY_FAILED;

    /* print some debug info */
    print_file_info();

    /* create MLE page table */
    mle_ptab_base = build_mle_pagetable(
                             g_mle_hdr.mle_start_off + TBOOT_BASE_ADDR,
                             g_mle_hdr.mle_end_off - g_mle_hdr.mle_start_off);
    if ( mle_ptab_base == NULL )
        return TB_ERR_FATAL;

    /* initialize TXT heap */
    txt_heap = init_txt_heap(mle_ptab_base, g_sinit, lctx);
    if ( txt_heap == NULL )
        return TB_ERR_TXT_NOT_SUPPORTED;

    /* TODO set the zero page addr here or possibly later in the launch */
    os_mle_data = get_os_mle_data_start(txt_heap);
    os_mle_data->zero_page_addr = 0;

    /* set MTRRs properly for AC module (SINIT) */
    if ( !set_mtrrs_for_acmod(g_sinit) )
        return TB_ERR_FATAL;

   /* deactivate current locality */
   if (g_tpm_family == TPM_IF_20_CRB ) {
       printk(TBOOT_INFO"Relinquish CRB localility 0 before executing GETSEC[SENTER]...\n");
	if (!tpm_relinquish_locality_crb(0)){
		printk(TBOOT_INFO"Relinquish CRB locality 0 failed...\n");
		error_action(TB_ERR_TPM_NOT_READY) ;
	}
   }

   /*{
   tpm_reg_loc_ctrl_t    reg_loc_ctrl;
   tpm_reg_loc_state_t  reg_loc_state;

   reg_loc_ctrl._raw[0] = 0;
   reg_loc_ctrl.relinquish = 1;
   write_tpm_reg(0, TPM_REG_LOC_CTRL, &reg_loc_ctrl);
   printk(TBOOT_INFO"Relinquish CRB localility 0 before executing GETSEC[SENTER]...\n");
   read_tpm_reg(0, TPM_REG_LOC_STATE, &reg_loc_state);
   printk(TBOOT_INFO"CRB reg_loc_state.active_locality is 0x%x \n", reg_loc_state.active_locality);
   printk(TBOOT_INFO"CRB reg_loc_state.loc_assigned is 0x%x \n", reg_loc_state.loc_assigned);
   }*/

   printk(TBOOT_INFO"executing GETSEC[SENTER]...\n");
    /* (optionally) pause before executing GETSEC[SENTER] */
    if ( g_vga_delay > 0 )
        delay(g_vga_delay * 1000);
    __getsec_senter((uint32_t)g_sinit, (g_sinit->size)*4);
    printk(TBOOT_INFO"ERROR--we should not get here!\n");
    return TB_ERR_FATAL;
}

tb_error_t txt_launch_racm(loader_ctx *lctx)
{
    acm_hdr_t *racm = NULL;

    /*
     * find correct revocation AC module in modules list
     */
    find_platform_racm(lctx, (void **)&racm, NULL);
    if ( racm == NULL )
        return TB_ERR_SINIT_NOT_PRESENT;
    /* copy it to a 32KB aligned memory address */
    racm = copy_racm(racm);
    if ( racm == NULL )
        return TB_ERR_SINIT_NOT_PRESENT;
    /* do some checks on it */
    if ( !verify_racm(racm) )
        return TB_ERR_ACMOD_VERIFY_FAILED;

    /* set MTRRs properly for AC module (RACM) */
    if ( !set_mtrrs_for_acmod(racm) )
        return TB_ERR_FATAL;

    /* clear MSEG_BASE/SIZE registers */
    write_pub_config_reg(TXTCR_MSEG_BASE, 0);
    write_pub_config_reg(TXTCR_MSEG_SIZE, 0);

    printk(TBOOT_INFO"executing GETSEC[ENTERACCS]...\n");
    /* (optionally) pause before executing GETSEC[ENTERACCS] */
    if ( g_vga_delay > 0 )
        delay(g_vga_delay * 1000);
    __getsec_enteraccs((uint32_t)racm, (racm->size)*4, 0xF0);
    /* powercycle by writing 0x0a+0x0e to port 0xcf9, */
    /* warm reset by write 0x06 to port 0xcf9 */
    //outb(0xcf9, 0x0a);
    //outb(0xcf9, 0x0e);
    outb(0xcf9, 0x06);

    printk(TBOOT_ERR"ERROR--we should not get here!\n");
    return TB_ERR_FATAL;
}

bool txt_prepare_cpu(void)
{
    unsigned long eflags, cr0;
    uint64_t mcg_cap, mcg_stat;

    /* must be running at CPL 0 => this is implicit in even getting this far */
    /* since our bootstrap code loads a GDT, etc. */

    cr0 = read_cr0();

    /* must be in protected mode */
    if ( !(cr0 & CR0_PE) ) {
        printk(TBOOT_ERR"ERR: not in protected mode\n");
        return false;
    }

    /* cache must be enabled (CR0.CD = CR0.NW = 0) */
    if ( cr0 & CR0_CD ) {
        printk(TBOOT_INFO"CR0.CD set\n");
        cr0 &= ~CR0_CD;
    }
    if ( cr0 & CR0_NW ) {
        printk(TBOOT_INFO"CR0.NW set\n");
        cr0 &= ~CR0_NW;
    }

    /* native FPU error reporting must be enabled for proper */
    /* interaction behavior */
    if ( !(cr0 & CR0_NE) ) {
        printk(TBOOT_INFO"CR0.NE not set\n");
        cr0 |= CR0_NE;
    }

    write_cr0(cr0);

    /* cannot be in virtual-8086 mode (EFLAGS.VM=1) */
    eflags = read_eflags();
    if ( eflags & X86_EFLAGS_VM ) {
        printk(TBOOT_INFO"EFLAGS.VM set\n");
        write_eflags(eflags | ~X86_EFLAGS_VM);
    }

    printk(TBOOT_INFO"CR0 and EFLAGS OK\n");

    /*
     * verify that we're not already in a protected environment
     */
    if ( txt_is_launched() ) {
        printk(TBOOT_ERR"already in protected environment\n");
        return false;
    }

    /*
     * verify all machine check status registers are clear (unless
     * support preserving them)
     */

    /* no machine check in progress (IA32_MCG_STATUS.MCIP=1) */
    mcg_stat = rdmsr(MSR_MCG_STATUS);
    if ( mcg_stat & 0x04 ) {
        printk(TBOOT_ERR"machine check in progress\n");
        return false;
    }

    getsec_parameters_t params;
    if ( !get_parameters(&params) ) {
        printk(TBOOT_ERR"get_parameters() failed\n");
        return false;
    }

    /* check if all machine check regs are clear */
    mcg_cap = rdmsr(MSR_MCG_CAP);
    for ( unsigned int i = 0; i < (mcg_cap & 0xff); i++ ) {
        mcg_stat = rdmsr(MSR_MC0_STATUS + 4*i);
        if ( mcg_stat & (1ULL << 63) ) {
            printk(TBOOT_ERR"MCG[%u] = %Lx ERROR\n", i, mcg_stat);
            if ( !params.preserve_mce )
                return false;
        }
    }

    if ( params.preserve_mce )
        printk(TBOOT_INFO"supports preserving machine check errors\n");
    else
        printk(TBOOT_INFO"no machine check errors\n");

    if ( params.proc_based_scrtm )
        printk(TBOOT_INFO"CPU support processor-based S-CRTM\n");

    /* all is well with the processor state */
    printk(TBOOT_INFO"CPU is ready for SENTER\n");

    return true;
}

bool txt_is_powercycle_required(void)
{
    /* a powercycle is required to clear the TXT_RESET.STS flag */
    txt_ests_t ests = (txt_ests_t)read_pub_config_reg(TXTCR_ESTS);
    return ests.txt_reset_sts;
}

#define ACM_MEM_TYPE_UC                 0x0100
#define ACM_MEM_TYPE_WC                 0x0200
#define ACM_MEM_TYPE_WT                 0x1000
#define ACM_MEM_TYPE_WP                 0x2000
#define ACM_MEM_TYPE_WB                 0x4000

#define DEF_ACM_MAX_SIZE                0x8000
#define DEF_ACM_VER_MASK                0xffffffff
#define DEF_ACM_VER_SUPPORTED           0x00
#define DEF_ACM_MEM_TYPES               ACM_MEM_TYPE_UC
#define DEF_SENTER_CTRLS                0x00

bool get_parameters(getsec_parameters_t *params)
{
    unsigned long cr4;
    uint32_t index, eax, ebx, ecx;
    int param_type;

    /* sanity check because GETSEC[PARAMETERS] will fail if not set */
    cr4 = read_cr4();
    if ( !(cr4 & CR4_SMXE) ) {
        printk(TBOOT_ERR"SMXE not enabled, can't read parameters\n");
        return false;
    }

    tb_memset(params, 0, sizeof(*params));
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
                printk(TBOOT_WARN"number of supported ACM version exceeds "
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
            printk(TBOOT_WARN"unknown GETSEC[PARAMETERS] type: %d\n",
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
