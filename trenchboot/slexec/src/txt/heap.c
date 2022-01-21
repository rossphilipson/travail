/*
 * heap.c: fns for verifying and printing the Intel(r) TXT heap data structs
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
#include <processor.h>
#include <multiboot.h>
#include <misc.h>
#include <tpm.h>
#include <loader.h>
#include <txt/mle.h>
#include <txt/txt.h>
#include <txt/acmod.h>
#include <txt/mtrrs.h>
#include <txt/heap.h>

/*
 * extended data elements
 */

/* HEAP_BIOS_SPEC_VER_ELEMENT */
static void print_bios_spec_ver_elt(const heap_ext_data_element_t *elt)
{
    const heap_bios_spec_ver_elt_t *bios_spec_ver_elt =
        (const heap_bios_spec_ver_elt_t *)elt->data;

    printk(SLEXEC_INFO"\t\t BIOS_SPEC_VER:\n");
    printk(SLEXEC_INFO"\t\t     major: 0x%x\n", bios_spec_ver_elt->spec_ver_major);
    printk(SLEXEC_INFO"\t\t     minor: 0x%x\n", bios_spec_ver_elt->spec_ver_minor);
    printk(SLEXEC_INFO"\t\t     rev: 0x%x\n", bios_spec_ver_elt->spec_ver_rev);
}

/* HEAP_ACM_ELEMENT */
static void print_acm_elt(const heap_ext_data_element_t *elt)
{
    const heap_acm_elt_t *acm_elt = (const heap_acm_elt_t *)elt->data;

    printk(SLEXEC_DETA"\t\t ACM:\n");
    printk(SLEXEC_DETA"\t\t     num_acms: %u\n", acm_elt->num_acms);
    for ( unsigned int i = 0; i < acm_elt->num_acms; i++ )
        printk(SLEXEC_DETA"\t\t     acm_addrs[%u]: 0x%jx\n", i, acm_elt->acm_addrs[i]);
}

/* HEAP_CUSTOM_ELEMENT */
static void print_custom_elt(const heap_ext_data_element_t *elt)
{
    const heap_custom_elt_t *custom_elt = (const heap_custom_elt_t *)elt->data;

    printk(SLEXEC_DETA"\t\t CUSTOM:\n");
    printk(SLEXEC_DETA"\t\t     size: %u\n", elt->size);
    printk(SLEXEC_DETA"\t\t     uuid: "); print_uuid(&custom_elt->uuid);
    printk(SLEXEC_DETA"\n");
}

/* HEAP_EVENT_LOG_POINTER_ELEMENT */
static void print_event(const tpm12_pcr_event_t *evt)
{
    printk(SLEXEC_DETA"\t\t\t Event:\n");
    printk(SLEXEC_DETA"\t\t\t     PCRIndex: %u\n", evt->pcr_index);
    printk(SLEXEC_DETA"\t\t\t         Type: 0x%x\n", evt->type);
    printk(SLEXEC_DETA"\t\t\t       Digest: ");
    print_hash((const sl_hash_t *)evt->digest, HASH_ALG_SHA1);
    printk(SLEXEC_DETA"\t\t\t         Data: %u bytes", evt->data_size);
    print_hex("\t\t\t         ", evt->data, evt->data_size);
}

static void print_evt_log(const event_log_container_t *elog)
{
    printk(SLEXEC_DETA"\t\t\t Event Log Container:\n");
    printk(SLEXEC_DETA"\t\t\t     Signature: %s\n", elog->signature);
    printk(SLEXEC_DETA"\t\t\t  ContainerVer: %u.%u\n",
           elog->container_ver_major, elog->container_ver_minor);
    printk(SLEXEC_DETA"\t\t\t   PCREventVer: %u.%u\n",
           elog->pcr_event_ver_major, elog->pcr_event_ver_minor);
    printk(SLEXEC_DETA"\t\t\t          Size: %u\n", elog->size);
    printk(SLEXEC_DETA"\t\t\t  EventsOffset: [%u,%u]\n",
           elog->pcr_events_offset, elog->next_event_offset);

    const tpm12_pcr_event_t *curr, *next;
    curr = (tpm12_pcr_event_t *)((void*)elog + elog->pcr_events_offset);
    next = (tpm12_pcr_event_t *)((void*)elog + elog->next_event_offset);

    while ( curr < next ) {
        print_event(curr);
        curr = (void *)curr + sizeof(*curr) + curr->data_size;
    }
}

static void print_evt_log_ptr_elt(const heap_ext_data_element_t *elt)
{
    const heap_event_log_ptr_elt_t *elog_elt =
              (const heap_event_log_ptr_elt_t *)elt->data;

    printk(SLEXEC_DETA"\t\t EVENT_LOG_POINTER:\n");
    printk(SLEXEC_DETA"\t\t       size: %u\n", elt->size);
    printk(SLEXEC_DETA"\t\t  elog_addr: 0x%jx\n", elog_elt->event_log_phys_addr);

    if ( elog_elt->event_log_phys_addr )
        print_evt_log((event_log_container_t *)(unsigned long)
                      elog_elt->event_log_phys_addr);
}

static void print_event_2(void *evt, uint16_t alg)
{
    uint32_t hash_size, data_size;
    void *next = evt;

    hash_size = get_hash_size(alg);
    if ( hash_size == 0 )
        return;

    printk(SLEXEC_DETA"\t\t\t Event:\n");
    printk(SLEXEC_DETA"\t\t\t     PCRIndex: %u\n", *((uint32_t *)next));

    if ( *((uint32_t *)next) > 24 && *((uint32_t *)next) != 0xFF ) {
         printk(SLEXEC_DETA"\t\t\t           Wrong Event Log.\n");
         return;
    }

    next += sizeof(uint32_t);
    printk(SLEXEC_DETA"\t\t\t         Type: 0x%x\n", *((uint32_t *)next));

    if ( *((uint32_t *)next) > 0xFFF ) {
        printk(SLEXEC_DETA"\t\t\t           Wrong Event Log.\n");
        return;
    }

    next += sizeof(uint32_t);
    printk(SLEXEC_DETA"\t\t\t       Digest: ");
    print_hex(NULL, (uint8_t *)next, hash_size);
    next += hash_size;
    data_size = *(uint32_t *)next;
    printk(SLEXEC_DETA"\t\t\t         Data: %u bytes", data_size);
    if ( data_size > 4096 ) {
        printk(SLEXEC_DETA"\t\t\t           Wrong Event Log.\n");
        return;
    }

    next += sizeof(uint32_t);
    if ( data_size )
         print_hex("\t\t\t         ", (uint8_t *)next, data_size);
    else
         printk(SLEXEC_DETA"\n");
}

static uint32_t print_event_2_1_log_header(void *evt)
{
    tcg_pcr_event *evt_ptr = (tcg_pcr_event *)evt;
    tcg_efi_specid_event_strcut *evt_data_ptr = (tcg_efi_specid_event_strcut *) evt_ptr->event_data;

    printk(SLEXEC_DETA"\t TCG Event Log Header:\n");
    printk(SLEXEC_DETA"\t\t       pcr_index: %u\n", evt_ptr->pcr_index);
    printk(SLEXEC_DETA"\t\t      event_type: %u\n", evt_ptr->event_type);
    printk(SLEXEC_DETA"\t\t          digest: %s\n", evt_ptr->digest);
    printk(SLEXEC_DETA"\t\t event_data_size: %u\n", evt_ptr->event_data_size);

    // print out event log header data

    printk(SLEXEC_DETA"\t\t 	   header event data:  \n");
    printk(SLEXEC_DETA"\t\t\t              signature: %s\n", evt_data_ptr->signature);
    printk(SLEXEC_DETA"\t\t\t         platform_class: %u\n", evt_data_ptr->platform_class);
    printk(SLEXEC_DETA"\t\t\t     spec_version_major: %u\n", evt_data_ptr->spec_version_major);
    printk(SLEXEC_DETA"\t\t\t     spec_version_minor: %u\n", evt_data_ptr->spec_version_minor);
    printk(SLEXEC_DETA"\t\t\t            spec_errata: %u\n", evt_data_ptr->spec_errata);
    printk(SLEXEC_DETA"\t\t\t             uintn_size: %u\n", evt_data_ptr->uintn_size);
    printk(SLEXEC_DETA"\t\t\t   number_of_algorithms: %u\n", evt_data_ptr->number_of_algorithms);

    for ( uint32_t i = 0; i < evt_data_ptr->number_of_algorithms; i++ ) {
        printk(SLEXEC_DETA"\t\t\t\t   algorithm_id: 0x%x \n", evt_data_ptr->digestSizes[i].algorithm_id);
        printk(SLEXEC_DETA"\t\t\t\t    digest_size: %u\n", evt_data_ptr->digestSizes[i].digest_size);
    }

    printk(SLEXEC_DETA"\t\t\t       vendor_info: %u bytes\n", evt_data_ptr->vendor_info_size);
    print_hex(NULL, evt_data_ptr->vendor_info, evt_data_ptr->vendor_info_size);

    return evt_ptr->event_data_size;
}

uint32_t print_event_2_1(void *evt)
{
    tcg_pcr_event2 *evt_ptr = (tcg_pcr_event2 *)evt;
    uint8_t *evt_data_ptr;
    uint16_t hash_alg;
    uint32_t event_size = 0;
    printk(SLEXEC_DETA"\t\t\t TCG Event:\n");
    printk(SLEXEC_DETA"\t\t\t      pcr_index: %u\n", evt_ptr->pcr_index);
    printk(SLEXEC_DETA"\t\t\t     event_type: 0x%x\n", evt_ptr->event_type);
    printk(SLEXEC_DETA"\t\t\t          count: %u\n", evt_ptr->digest.count);
    if (evt_ptr->digest.count != 0) {
        evt_data_ptr = (uint8_t *)evt_ptr->digest.digests[0].digest;
        hash_alg = evt_ptr->digest.digests[0].hash_alg;
        for ( uint32_t i = 0; i < evt_ptr->digest.count; i++ ) {
            switch (hash_alg) {
                case HASH_ALG_SHA1:
                    printk(SLEXEC_INFO"SHA1: \n");
                    print_hex(NULL, evt_data_ptr, SHA1_LENGTH);
                    evt_data_ptr += SHA1_LENGTH;
                    break;

                case HASH_ALG_SHA256:
                    printk(SLEXEC_INFO"SHA256: \n");
                    print_hex(NULL, evt_data_ptr, SHA256_LENGTH);
                    evt_data_ptr += SHA256_LENGTH;
                    break;

                case HASH_ALG_SM3:
                    printk(SLEXEC_INFO"SM3_256: \n");
                    print_hex(NULL, evt_data_ptr, SM3_LENGTH);
                    evt_data_ptr += SM3_LENGTH;
                    break;

                case HASH_ALG_SHA384:
                    printk(SLEXEC_INFO"SHA384: \n");
                    print_hex(NULL, evt_data_ptr, SHA384_LENGTH);
                    evt_data_ptr += SHA384_LENGTH;
                    break;

                case HASH_ALG_SHA512:
                    printk(SLEXEC_INFO"SHA512:  \n");
                    print_hex(NULL, evt_data_ptr, SHA512_LENGTH);
                    evt_data_ptr += SHA512_LENGTH;
                    break;
                default:
                    printk(SLEXEC_ERR"Unsupported algorithm: %u\n", evt_ptr->digest.digests[i].hash_alg);
            }
            hash_alg = (uint16_t)*evt_data_ptr;
            evt_data_ptr += sizeof(uint16_t);
        }
        evt_data_ptr -= sizeof(uint16_t);
        event_size = (uint32_t)*evt_data_ptr;
        printk(SLEXEC_DETA"\t\t\t     event_data: %u bytes", event_size);
        evt_data_ptr += sizeof(uint32_t);
        print_hex("\t\t\t     ", evt_data_ptr, event_size);
    }
    else {
        printk(SLEXEC_DETA"sth wrong in TCG event log: algoritm count = %u\n", evt_ptr->digest.count);
        evt_data_ptr= (uint8_t *)evt +12;
    }
    return (evt_data_ptr + event_size - (uint8_t *)evt);
}

static void print_evt_log_ptr_elt_2(const heap_ext_data_element_t *elt)
{
    const heap_event_log_ptr_elt2_t *elog_elt =
              (const heap_event_log_ptr_elt2_t *)elt->data;
    const heap_event_log_descr_t *log_descr;

    printk(SLEXEC_DETA"\t\t EVENT_LOG_PTR:\n");
    printk(SLEXEC_DETA"\t\t       size: %u\n", elt->size);
    printk(SLEXEC_DETA"\t\t      count: %d\n", elog_elt->count);

    for ( unsigned int i=0; i<elog_elt->count; i++ ) {
        log_descr = &elog_elt->event_log_descr[i];
        printk(SLEXEC_DETA"\t\t\t Log Descrption:\n");
        printk(SLEXEC_DETA"\t\t\t             Alg: %u\n", log_descr->alg);
        printk(SLEXEC_DETA"\t\t\t            Size: %u\n", log_descr->size);
        printk(SLEXEC_DETA"\t\t\t    EventsOffset: [%u,%u]\n",
                log_descr->pcr_events_offset,
                log_descr->next_event_offset);

        if (log_descr->pcr_events_offset == log_descr->next_event_offset) {
            printk(SLEXEC_DETA"\t\t\t              No Event Log.\n");
            continue;
        }

        uint32_t hash_size, data_size;
        hash_size = get_hash_size(log_descr->alg);
        if ( hash_size == 0 )
            return;

        void *curr, *next;

        curr = (void *)(unsigned long)log_descr->phys_addr +
                log_descr->pcr_events_offset;
        next = (void *)(unsigned long)log_descr->phys_addr +
                log_descr->next_event_offset;

        if (log_descr->alg != HASH_ALG_SHA1){
            print_event_2(curr, HASH_ALG_SHA1);
            curr += sizeof(tpm12_pcr_event_t) + sizeof(tpm20_log_descr_t);
        }

        while ( curr < next ) {
            print_event_2(curr, log_descr->alg);
            data_size = *(uint32_t *)(curr + 2*sizeof(uint32_t) + hash_size);
            curr += 3*sizeof(uint32_t) + hash_size + data_size;
        }
    }
}

static void print_evt_log_ptr_elt_2_1(const heap_ext_data_element_t *elt)
{
    const heap_event_log_ptr_elt2_1_t *elog_elt = (const heap_event_log_ptr_elt2_1_t *)elt->data;

    printk(SLEXEC_DETA"\t TCG EVENT_LOG_PTR:\n");
    printk(SLEXEC_DETA"\t\t       type: %d\n", elt->type);
    printk(SLEXEC_DETA"\t\t       size: %u\n", elt->size);
    printk(SLEXEC_DETA"\t TCG Event Log Descrption:\n");
    printk(SLEXEC_DETA"\t     allcoated_event_container_size: %u\n", elog_elt->allcoated_event_container_size);
    printk(SLEXEC_DETA"\t                       EventsOffset: [%u,%u]\n",
           elog_elt->first_record_offset, elog_elt->next_record_offset);

    if (elog_elt->first_record_offset == elog_elt->next_record_offset) {
        printk(SLEXEC_DETA"\t\t\t No Event Log found.\n");
        return;
    }
    void *curr, *next;

    curr = (void *)(unsigned long)elog_elt->phys_addr + elog_elt->first_record_offset;
    next = (void *)(unsigned long)elog_elt->phys_addr + elog_elt->next_record_offset;
    uint32_t event_header_data_size = print_event_2_1_log_header(curr);

    curr += sizeof(tcg_pcr_event) + event_header_data_size;
    while ( curr < next ) {
        curr += print_event_2_1(curr);
    }
}

static void print_ext_data_elts(const heap_ext_data_element_t elts[])
{
    const heap_ext_data_element_t *elt = elts;

    printk(SLEXEC_DETA"\t ext_data_elts[]:\n");
    while ( elt->type != HEAP_EXTDATA_TYPE_END ) {
        switch ( elt->type ) {
            case HEAP_EXTDATA_TYPE_BIOS_SPEC_VER:
                print_bios_spec_ver_elt(elt);
                break;
            case HEAP_EXTDATA_TYPE_ACM:
                print_acm_elt(elt);
                break;
            case HEAP_EXTDATA_TYPE_CUSTOM:
                print_custom_elt(elt);
                break;
            case HEAP_EXTDATA_TYPE_TPM_EVENT_LOG_PTR:
                print_evt_log_ptr_elt(elt);
                break;
            case HEAP_EXTDATA_TYPE_TPM_EVENT_LOG_PTR_2:
                print_evt_log_ptr_elt_2(elt);
                break;
            case HEAP_EXTDATA_TYPE_TPM_EVENT_LOG_PTR_2_1:
                print_evt_log_ptr_elt_2_1(elt);
                break;
            default:
                printk(SLEXEC_WARN"\t\t unknown element:  type: %u, size: %u\n",
                       elt->type, elt->size);
                break;
        }
        elt = (void *)elt + elt->size;
    }
}

static void print_bios_data(const bios_data_t *bios_data, uint64_t size)
{
    printk(SLEXEC_DETA"bios_data (@%p, %jx):\n", bios_data,
           *((uint64_t *)bios_data - 1));
    printk(SLEXEC_DETA"\t version: %u\n", bios_data->version);
    printk(SLEXEC_DETA"\t bios_sinit_size: 0x%x (%u)\n", bios_data->bios_sinit_size,
           bios_data->bios_sinit_size);
    printk(SLEXEC_DETA"\t lcp_pd_base: 0x%jx\n", bios_data->lcp_pd_base);
    printk(SLEXEC_DETA"\t lcp_pd_size: 0x%jx (%ju)\n", bios_data->lcp_pd_size,
           bios_data->lcp_pd_size);
    printk(SLEXEC_DETA"\t num_logical_procs: %u\n", bios_data->num_logical_procs);
    if ( bios_data->version >= 3 )
        printk(SLEXEC_DETA"\t flags: 0x%08jx\n", bios_data->flags);
    if ( bios_data->version >= 4 && size > sizeof(*bios_data) + sizeof(size) )
        print_ext_data_elts(bios_data->ext_data_elts);
}

static bool verify_bios_spec_ver_elt(const heap_ext_data_element_t *elt)
{
    const heap_bios_spec_ver_elt_t *bios_spec_ver_elt =
        (const heap_bios_spec_ver_elt_t *)elt->data;

    if ( elt->size != sizeof(*elt) + sizeof(*bios_spec_ver_elt) ) {
        printk(SLEXEC_ERR"HEAP_BIOS_SPEC_VER element has wrong size (%u)\n", elt->size);
        return false;
    }

    /* any values are allowed */
    return true;
}

static bool verify_acm_elt(const heap_ext_data_element_t *elt)
{
    const heap_acm_elt_t *acm_elt = (const heap_acm_elt_t *)elt->data;

    if ( elt->size != sizeof(*elt) + sizeof(*acm_elt) +
         acm_elt->num_acms*sizeof(uint64_t) ) {
        printk(SLEXEC_ERR"HEAP_ACM element has wrong size (%u)\n", elt->size);
        return false;
    }

    /* no addrs is not error, but print warning */
    if ( acm_elt->num_acms == 0 )
        printk(SLEXEC_WARN"HEAP_ACM element has no ACM addrs\n");

    for ( unsigned int i = 0; i < acm_elt->num_acms; i++ ) {
        if ( acm_elt->acm_addrs[i] == 0 ) {
            printk(SLEXEC_ERR"HEAP_ACM element ACM addr (%u) is NULL\n", i);
            return false;
        }

        if ( acm_elt->acm_addrs[i] >= 0x100000000UL ) {
            printk(SLEXEC_ERR"HEAP_ACM element ACM addr (%u) is >4GB (0x%jx)\n", i,
                   acm_elt->acm_addrs[i]);
            return false;
        }

        /* not going to check if ACM addrs are valid ACMs */
    }

    return true;
}

static bool verify_custom_elt(const heap_ext_data_element_t *elt)
{
    const heap_custom_elt_t *custom_elt = (const heap_custom_elt_t *)elt->data;

    if ( elt->size < sizeof(*elt) + sizeof(*custom_elt) ) {
        printk(SLEXEC_ERR"HEAP_CUSTOM element has wrong size (%u)\n", elt->size);
        return false;
    }

    /* any values are allowed */
    return true;
}

static bool verify_evt_log(const event_log_container_t *elog)
{
    if ( elog == NULL ) {
        printk(SLEXEC_ERR"Event log container pointer is NULL\n");
        return false;
    }

    if ( sl_memcmp(elog->signature, EVTLOG_SIGNATURE, sizeof(elog->signature)) ) {
        printk(SLEXEC_ERR"Bad event log container signature: %s\n", elog->signature);
        return false;
    }

    if ( elog->size != MAX_EVENT_LOG_SIZE ) {
        printk(SLEXEC_ERR"Bad event log container size: 0x%x\n", elog->size);
        return false;
    }

    /* no need to check versions */

    if ( elog->pcr_events_offset < sizeof(*elog) ||
         elog->next_event_offset < elog->pcr_events_offset ||
         elog->next_event_offset > elog->size ) {
        printk(SLEXEC_ERR"Bad events offset range: [%u, %u)\n",
               elog->pcr_events_offset, elog->next_event_offset);
        return false;
    }

    return true;
}

static bool verify_evt_log_ptr_elt(const heap_ext_data_element_t *elt)
{
    const heap_event_log_ptr_elt_t *elog_elt =
              (const heap_event_log_ptr_elt_t *)elt->data;

    if ( elt->size != sizeof(*elt) + sizeof(*elog_elt) ) {
        printk(SLEXEC_ERR"HEAP_EVENT_LOG_POINTER element has wrong size (%u)\n",
               elt->size);
        return false;
    }

    return verify_evt_log((event_log_container_t *)(unsigned long)
                          elog_elt->event_log_phys_addr);
}

static bool verify_evt_log_ptr_elt_2(const heap_ext_data_element_t *elt)
{
    if ( !elt )
        return false;

    return true;
}

static bool verify_ext_data_elts(const heap_ext_data_element_t elts[],
                                 size_t elts_size)
{
    const heap_ext_data_element_t *elt = elts;

    while ( true ) {
        if ( elts_size < sizeof(*elt) ) {
            printk(SLEXEC_ERR"heap ext data elements too small\n");
            return false;
        }
        if ( elts_size < elt->size || elt->size == 0 ) {
            printk(SLEXEC_ERR"invalid element size:  type: %u, size: %u\n",
                   elt->type, elt->size);
            return false;
        }
        switch ( elt->type ) {
            case HEAP_EXTDATA_TYPE_END:
                return true;
            case HEAP_EXTDATA_TYPE_BIOS_SPEC_VER:
                if ( !verify_bios_spec_ver_elt(elt) )
                    return false;
                break;
            case HEAP_EXTDATA_TYPE_ACM:
                if ( !verify_acm_elt(elt) )
                    return false;
                break;
            case HEAP_EXTDATA_TYPE_CUSTOM:
                if ( !verify_custom_elt(elt) )
                    return false;
                break;
            case HEAP_EXTDATA_TYPE_TPM_EVENT_LOG_PTR:
                if ( !verify_evt_log_ptr_elt(elt) )
                    return false;
                break;
            case HEAP_EXTDATA_TYPE_TPM_EVENT_LOG_PTR_2:
                if ( !verify_evt_log_ptr_elt_2(elt) )
                    return false;
                break;
            default:
                printk(SLEXEC_WARN"unknown element:  type: %u, size: %u\n", elt->type,
                       elt->size);
                break;
        }
        elts_size -= elt->size;
        elt = (void *)elt + elt->size;
    }
    return true;
}

bool verify_bios_data(const txt_heap_t *txt_heap)
{
    uint64_t heap_base = read_pub_config_reg(TXTCR_HEAP_BASE);
    uint64_t heap_size = read_pub_config_reg(TXTCR_HEAP_SIZE);
    printk(SLEXEC_DETA"TXT.HEAP.BASE: 0x%jx\n", heap_base);
    printk(SLEXEC_DETA"TXT.HEAP.SIZE: 0x%jx (%ju)\n", heap_size, heap_size);

    /* verify that heap base/size are valid */
    if ( txt_heap == NULL || heap_base == 0 || heap_size == 0 )
        return false;

    /* check size */
    uint64_t size = get_bios_data_size(txt_heap);
    if ( size == 0 ) {
        printk(SLEXEC_ERR"BIOS data size is 0\n");
        return false;
    }
    if ( size > heap_size ) {
        printk(SLEXEC_ERR"BIOS data size is larger than heap size "
               "(%jx, heap size=%jx)\n", size, heap_size);
        return false;
    }

    bios_data_t *bios_data = get_bios_data_start(txt_heap);

    /* check version */
    if ( bios_data->version < 2 ) {
        printk(SLEXEC_ERR"unsupported BIOS data version (%u)\n", bios_data->version);
        return false;
    }
    /* we assume backwards compatibility but print a warning */
    if ( bios_data->version > 4 )
        printk(SLEXEC_WARN"unsupported BIOS data version (%u)\n", bios_data->version);

    /* all TXT-capable CPUs support at least 1 core */
    if ( bios_data->num_logical_procs < 1 ) {
        printk(SLEXEC_ERR"BIOS data has incorrect num_logical_procs (%u)\n",
               bios_data->num_logical_procs);
        return false;
    }
    else if ( bios_data->num_logical_procs > SL_MAX_CPUS ) {
        printk(SLEXEC_ERR"BIOS data specifies too many CPUs (%u)\n",
               bios_data->num_logical_procs);
        return false;
    }

    if ( bios_data->version >= 4 && size > sizeof(*bios_data) + sizeof(size) ) {
        if ( !verify_ext_data_elts(bios_data->ext_data_elts,
                                   size - sizeof(*bios_data) - sizeof(size)) )
            return false;
    }

    print_bios_data(bios_data, size);

    return true;
}

/*
 * Make sure version is in [MIN_OS_SINIT_DATA_VER, MAX_OS_SINIT_DATA_VER]
 * before calling calc_os_sinit_data_size
 */
uint64_t calc_os_sinit_data_size(uint32_t version)
{
    uint64_t size[] = {
        offsetof(os_sinit_data_t, efi_rsdt_ptr) + sizeof(uint64_t),
        sizeof(os_sinit_data_t) + sizeof(uint64_t),
        sizeof(os_sinit_data_t) + sizeof(uint64_t) +
            2 * sizeof(heap_ext_data_element_t) +
            sizeof(heap_event_log_ptr_elt_t)
    };
    int log_type = EVTLOG_TPM2_TCG; /* TODO fix this function get_evtlog_type(); */

    if ( log_type == EVTLOG_TPM2_TCG ) {
        size[2] = sizeof(os_sinit_data_t) + sizeof(uint64_t) +
        2 * sizeof(heap_ext_data_element_t) +
        sizeof(heap_event_log_ptr_elt2_1_t);
    }
#if 0 /* TODO we only support TCG event log right? I don't think we care about the extpol stuff */
    } else if (log_type == EVTLOG_TPM2_LEGACY) {
        u32 count;
        if ( tpm->extpol == TB_EXTPOL_AGILE )
            count = tpm->banks;
        else
            if ( tpm->extpol == TB_EXTPOL_EMBEDDED )
                count = tpm->alg_count;
            else
                count = 1;
        size[2] = sizeof(os_sinit_data_t) + sizeof(uint64_t) +
            2 * sizeof(heap_ext_data_element_t) + 4 +
            count*sizeof(heap_event_log_descr_t);
    }
#endif

    if ( version >= 6 )
        return size[2];
    else
        return size[version - MIN_OS_SINIT_DATA_VER];
}

static void print_os_sinit_data_vtdpmr(const os_sinit_data_t *os_sinit_data)
{
    printk(SLEXEC_DETA"\t vtd_pmr_lo_base: 0x%Lx\n", os_sinit_data->vtd_pmr_lo_base);
    printk(SLEXEC_DETA"\t vtd_pmr_lo_size: 0x%Lx\n", os_sinit_data->vtd_pmr_lo_size);
    printk(SLEXEC_DETA"\t vtd_pmr_hi_base: 0x%Lx\n", os_sinit_data->vtd_pmr_hi_base);
    printk(SLEXEC_DETA"\t vtd_pmr_hi_size: 0x%Lx\n", os_sinit_data->vtd_pmr_hi_size);
}

void print_os_sinit_data(const os_sinit_data_t *os_sinit_data)
{
    printk(SLEXEC_DETA"os_sinit_data (@%p, %Lx):\n", os_sinit_data,
           *((uint64_t *)os_sinit_data - 1));
    printk(SLEXEC_DETA"\t version: %u\n", os_sinit_data->version);
    printk(SLEXEC_DETA"\t flags: %u\n", os_sinit_data->flags);
    printk(SLEXEC_DETA"\t mle_ptab: 0x%Lx\n", os_sinit_data->mle_ptab);
    printk(SLEXEC_DETA"\t mle_size: 0x%Lx (%Lu)\n", os_sinit_data->mle_size,
           os_sinit_data->mle_size);
    printk(SLEXEC_DETA"\t mle_hdr_base: 0x%Lx\n", os_sinit_data->mle_hdr_base);
    print_os_sinit_data_vtdpmr(os_sinit_data);
    printk(SLEXEC_DETA"\t lcp_po_base: 0x%Lx\n", os_sinit_data->lcp_po_base);
    printk(SLEXEC_DETA"\t lcp_po_size: 0x%Lx (%Lu)\n", os_sinit_data->lcp_po_size, os_sinit_data->lcp_po_size);
    /* TODO add back in later print_txt_caps("\t ", os_sinit_data->capabilities); */
    if ( os_sinit_data->version >= 5 )
        printk(SLEXEC_DETA"\t efi_rsdt_ptr: 0x%Lx\n", os_sinit_data->efi_rsdt_ptr);
    if ( os_sinit_data->version >= 6 )
        print_ext_data_elts(os_sinit_data->ext_data_elts);
}

/*
 * Local variables:
 * mode: C
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
