/*
 * slboot.h: main header definition file
 *
 * Used to be:
 * tboot.h: shared data structure with MLE and kernel and functions
 *          used by kernel for runtime support
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
#ifndef __TBOOT_H__
#define __TBOOT_H__

#include <tb_error.h>

#ifndef __packed
#define __packed   __attribute__ ((packed))
#endif

/* define uuid_t here in case uuid.h wasn't pre-included */
/* (i.e. so tboot.h can be self-sufficient) */
#ifndef __UUID_H__
typedef struct __packed {
  uint32_t    data1;
  uint16_t    data2;
  uint16_t    data3;
  uint16_t    data4;
  uint8_t     data5[6];
} uuid_t;
#endif

/*
 * used to communicate between tboot and the launched kernel (i.e. Xen)
 */

#define TB_KEY_SIZE             64   /* 512 bits */

#define MAX_TB_MAC_REGIONS      32
typedef struct __packed {
    uint64_t  start;         /* must be 4k byte -aligned */
    uint32_t  size;          /* must be 4k byte -granular */
} tboot_mac_region_t;

/* GAS - Generic Address Structure (ACPI 2.0+) */
typedef struct __packed {
    uint8_t  space_id;         /* only 0,1 (memory, I/O) are supported */
    uint8_t  bit_width;
    uint8_t  bit_offset;
    uint8_t  access_width;     /* only 1-3 (byte, word, dword) are supported */
    uint64_t address;
} tboot_acpi_generic_address_t;

typedef struct __packed {
    tboot_acpi_generic_address_t pm1a_cnt_blk;
    tboot_acpi_generic_address_t pm1b_cnt_blk;
    tboot_acpi_generic_address_t pm1a_evt_blk;
    tboot_acpi_generic_address_t pm1b_evt_blk;
    uint16_t pm1a_cnt_val;
    uint16_t pm1b_cnt_val;
    uint64_t wakeup_vector;
    uint32_t vector_width;
    uint64_t kernel_s3_resume_vector;
} tboot_acpi_sleep_info_t;

#define TB_SHUTDOWN_REBOOT      0
#define TB_SHUTDOWN_HALT        4

/* {663C8DFF-E8B3-4b82-AABF-19EA4D057A08} */
#define TBOOT_SHARED_UUID    {0x663c8dff, 0xe8b3, 0x4b82, 0xaabf, \
                                 {0x19, 0xea, 0x4d, 0x5, 0x7a, 0x8 }}

/*
 * used to log tboot printk output
 */
#define ZIP_COUNT_MAX 10
typedef struct {
    uuid_t     uuid;
    uint16_t   max_size;
    uint16_t   curr_pos;
    uint16_t   zip_pos[ZIP_COUNT_MAX];
    uint16_t   zip_size[ZIP_COUNT_MAX];
    uint8_t    zip_count;
    char       buf[];
} tboot_log_t;

/* {C0192526-6B30-4db4-844C-A3E953B88174} */
#define TBOOT_LOG_UUID   {0xc0192526, 0x6b30, 0x4db4, 0x844c, \
                             {0xa3, 0xe9, 0x53, 0xb8, 0x81, 0x74 }}

extern void error_action(tb_error_t error);
extern void shutdown_system(uint32_t shutdown_type);

#endif    /* __TBOOT_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
