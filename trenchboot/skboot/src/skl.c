#include <types.h>
#include <stdbool.h>
#include <skboot.h>
#include <stdarg.h>
#include <string.h>
#include <printk.h>
#include <processor.h>
#include <loader.h>
#include <e820.h>
#include <linux.h>
#include <skl.h>

lz_info_t lz_info = {
	.uuid = {
		0x78, 0xf1, 0x26, 0x8e, 0x04, 0x92, 0x11, 0xe9,
		0x83, 0x2a, 0xc8, 0x5b, 0x76, 0xc4, 0xcc, 0x02,
	},
	.version = SKL_VERSION,
	.msb_key_algo = 0x14,
	.msb_key_hash = { 0 },
};

bool is_skl_module(const void *skl_base, uint32_t skl_size)
{
    sl_header_t *header = (sl_header_t *)skl_base;
    lz_info_t *info;

    if (skl_size < (8*PAGE_SIZE)) {
        printk(SKBOOT_INFO"Possible SKL module too small\n");
        return false;
    }

    info = (lz_info_t *)((u8 *)header + header->skl_info_offset);
    if (info->version != SKL_VERSION) {
        printk(SKBOOT_INFO"Possible SKL module incorrect version\n");
        return false;
    }

    if (sk_memcmp(info->uuid, lz_info.uuid, 16)) {
        printk(SKBOOT_INFO"Possible SKL module incorrect UUID\n");
        return false;
    }

    return true;
}

void relocate_skl_module(void)
{
    void *dest = (void *)SKBOOT_FIXED_SKL_BASE;

    /* TODO hardcoded relocation for now */
    sk_memcpy(dest, g_skl_module, g_skl_size);
    printk(SKBOOT_INFO"SKL relocated module from %p to %p\n", g_skl_module, dest);
    g_skl_module = dest;
}

void print_skl_module(void)
{
    /* Otherwise it looks like the SKL module */
    printk(SKBOOT_INFO"SKL module @ %p - offsets:\n", g_skl_module);
    printk(SKBOOT_INFO"    entry:            0x%x\n", g_skl_module->skl_entry_point);
    printk(SKBOOT_INFO"    bootloader_data:  0x%x\n", g_skl_module->bootloader_data_offset);
    printk(SKBOOT_INFO"    skl_info:         0x%x\n", g_skl_module->skl_info_offset);
}

bool prepare_skl_bootloader_data(void)
{
    skl_tag_tags_size_t *stag;
    skl_tag_hash_t *htag;
    skl_tag_boot_linux_t *btag;
    skl_tag_evtlog_t *ltag;
    skl_tag_setup_indirect_t *itag;
    skl_tag_hdr_t *etag;
    sk_hash_t hash;

    /* Size tag is always first */
    stag = (skl_tag_tags_size_t *)((u8 *)g_skl_module + g_skl_module->bootloader_data_offset);
    stag->hdr.type = SKL_TAG_TAGS_SIZE;
    stag->hdr.len = sizeof(skl_tag_tags_size_t);
    stag->size = sizeof(skl_tag_tags_size_t);
    printk(SKBOOT_INFO"SKL added size tag\n");

    /* Hash tags for measured part of SKL */
    htag = (skl_tag_hash_t *)((u8 *)stag + sizeof(skl_tag_tags_size_t));
    htag->hdr.type = SKL_TAG_SKL_HASH;
    htag->hdr.len = sizeof(skl_tag_hash_t) + SHA256_LENGTH;
    htag->algo_id = HASH_ALG_SHA256;
    sha256_buffer((u8 *)g_skl_module,
                  g_skl_module->skl_info_offset,
                  hash.sha256);
    sk_memcpy((u8 *)htag + sizeof(skl_tag_hash_t),
              &hash.sha256[0], SHA256_LENGTH);
    stag->size += htag->hdr.len;
    printk(SKBOOT_INFO"SKL added hash tag for SHA256\n");

    htag = (skl_tag_hash_t *)((u8 *)htag + htag->hdr.len);
    htag->hdr.type = SKL_TAG_SKL_HASH;
    htag->hdr.len = sizeof(skl_tag_hash_t) + SHA1_LENGTH;
    htag->algo_id = HASH_ALG_SHA1;
    sha1_buffer((u8 *)g_skl_module,
                g_skl_module->skl_info_offset,
                hash.sha1);
    sk_memcpy((u8 *)htag + sizeof(skl_tag_hash_t),
              &hash.sha1[0], SHA1_LENGTH);
    stag->size += htag->hdr.len;
    printk(SKBOOT_INFO"SKL added hash tag for SHA1\n");

    /* Setup boot tag for Linux kernel. Note all the information needed
     * is in the zero page. */
    btag = (skl_tag_boot_linux_t *)((u8 *)htag + htag->hdr.len);
    btag->hdr.type = SKL_TAG_BOOT_LINUX;
    btag->hdr.len = sizeof(skl_tag_boot_linux_t);
    btag->zero_page = g_sl_kernel_setup.real_mode_base;
    stag->size += btag->hdr.len;
    printk(SKBOOT_INFO"SKL added boot linux tag\n");

    /* Event log tag. There is not DRTM ACPI table but SKBOOT will provide a buffer */
    ltag = (skl_tag_evtlog_t *)((u8 *)btag + btag->hdr.len);
    ltag->hdr.type = SKL_TAG_EVENT_LOG;
    ltag->hdr.len = sizeof(skl_tag_evtlog_t);
    ltag->address = SKBOOT_EVENT_LOG_ADDR;
    ltag->size = SKBOOT_EVENT_LOG_SIZE;
    stag->size += ltag->hdr.len;
    printk(SKBOOT_INFO"SKL added event log tag\n");

    /* The indirect tag contains the extent of the SKL in memory */
    itag = (skl_tag_setup_indirect_t *)((u8 *)ltag + ltag->hdr.len);
    itag->hdr.type = SKL_TAG_SETUP_INDIRECT;
    itag->hdr.len = sizeof(skl_tag_setup_indirect_t);
    linux_skl_setup_indirect(&itag->data);
    stag->size += itag->hdr.len;
    printk(SKBOOT_INFO"SKL added setup indirect tag\n");

    /* End tag comes last */
    etag = (skl_tag_hdr_t *)((u8 *)itag + itag->hdr.len);
    etag->type = SKL_TAG_END;
    etag->len = sizeof(skl_tag_hdr_t);
    stag->size += etag->len;
    printk(SKBOOT_INFO"SKL added end tag - final size: %d (0x%x)\n",
           (int)stag->size, stag->size);

    return true;
}
