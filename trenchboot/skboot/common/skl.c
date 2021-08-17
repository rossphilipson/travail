#include <config.h>
#include <types.h>
#include <stdbool.h>
#include <skboot.h>
#include <stdarg.h>
#include <string.h>
#include <printk.h>
#include <processor.h>
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

    if (skl_size < (16*PAGE_SIZE)) {
        printk(SKBOOT_INFO"Possible SKL module too small\n");
        return false;
    }

    info = (lz_info_t *)(header + header->skl_info_offset);
    if (info->version != SKL_VERSION) {
        printk(SKBOOT_INFO"Possible SKL module incorrect version\n");
        return false;
    }

    if (sk_memcmp(info->uuid, lz_info.uuid, 16)) {
        printk(SKBOOT_INFO"Possible SKL module incorrect UUID\n");
        return false;
    }

    /* Otherwise it looks like the SKL module */

    return true;
}
