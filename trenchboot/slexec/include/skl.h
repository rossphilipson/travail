#ifndef _SKL_H
#define _SKL_H

#define SKL_VERSION 0

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

#endif /* _SKL_H */
