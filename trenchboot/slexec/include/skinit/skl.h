/*
 * Copyright (c) 2022, Oracle and/or its affiliates.
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

#ifndef __SKINIT_SKL_H__
#define __SKINIT__SKL_H__

#define SKL_VERSION 0

#define SKINIT_SKL_UUID  {0x78f1268e, 0x0492, 0x11e9, 0x832a, \
                          {0xc8, 0x5b, 0x76, 0xc4, 0xcc, 0x03 }}

/* The SL bloc header, first two fields defined by spec */
typedef struct __packed {
    uint16_t skl_entry_point;
    uint16_t bootloader_data_offset;
    uint16_t skl_info_offset;
} sl_header_t;

typedef struct __packed {
    uint8_t  uuid[16]; /* 78 f1 26 8e 04 92 11 e9  83 2a c8 5b 76 c4 cc 02 */
    uint32_t version;
    uint16_t msb_key_algo;
    uint8_t  msb_key_hash[64]; /* Support up to SHA512 */
} skl_info_t;

#define SKL_TAG_CLASS_MASK	0xF0

/* Tags with no particular class */
#define SKL_TAG_NO_CLASs	0x00
#define SKL_TAG_END		0x00
#define SKL_TAG_SETUP_INDIRECT	0x01
#define SKL_TAG_TAGS_SIZE	0x0F	/* Always first */

/* Tags specifying kernel type */
#define SKL_TAG_BOOT_CLASS	0x10
#define SKL_TAG_BOOT_LINUX	0x10
#define SKL_TAG_BOOT_MB2	0x11

/* Tags specific to TPM event log */
#define SKL_TAG_EVENT_LOG_CLASS	0x20
#define SKL_TAG_EVENT_LOG	0x20
#define SKL_TAG_SKL_HASH	0x21

typedef struct __packed {
    uint8_t type;
    uint8_t len;
} skl_tag_hdr_t;

typedef struct __packed {
    skl_tag_hdr_t hdr;
    uint16_t size;
} skl_tag_tags_size_t;

typedef struct __packed {
    skl_tag_hdr_t hdr;
    uint32_t zero_page;
} skl_tag_boot_linux_t;

typedef struct __packed {
    skl_tag_hdr_t hdr;
    uint32_t address;
    uint32_t size;
} skl_tag_evtlog_t;

typedef struct __packed {
    skl_tag_hdr_t hdr;
    uint16_t algo_id;
    /* digest[] */
} skl_tag_hash_t;

typedef struct __packed {
    skl_tag_hdr_t hdr;
    /* type = SETUP_INDIRECT */
    setup_data_t data;
    /* type = SETUP_INDIRECT | SETUP_SECURE_LAUNCH */
    setup_indirect_t indirect;
} skl_tag_setup_indirect_t;

extern sl_header_t *g_skl_module;
extern uint32_t g_skl_size;

extern bool is_skl_module(const void *skl_base, uint32_t skl_size);
extern void print_skl_module(void);
extern void relocate_skl_module(void);
extern bool prepare_skl_bootloader_data(void);

#endif /* __SKINIT_SKL_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
