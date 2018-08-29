#ifndef __CONFIG_H__
#define __CONFIG_H__

/**********************************************************
 * LZ fixed memory layout
 *
 * +---------------+ EAX - 0x2000 - Size IL Image
 * |               |
 * | Intermediate  |
 * |   Loader      |
 * |   bzImage     |
 * |               |
 * +---------------+ EAX - 0x2000
 * |   Second      |
 * |   Stage       |
 * |   Stack       |
 * +---------------+ EAX - 0x1000
 * |   Second      |
 * |   Stage       |
 * |   Heap        |
 * +---------------+ EAX (begin LZ)
 * |   SL Header   | [0x4b]
 * | ------------- |
 * |   LZ Header   | [0x1Cb]
 * | ------------- |
 * |   First       | [0x1E0b]
 * |   Stage       |
 * |   Stack       |
 * | ------------- | EAX + 0x200
 * |               |
 * |   LZ Code     |
 * |               |
 * | ------------- | EAX + 0xA000
 * |     DEV       | [0x3000b]
 * |   Tables      |
 * | ------------- | EAX + 0xD000
 * |    Page       | [0x3000b]
 * |   Tables      |
 * +---------------+ EAX + 0x10000 (end of LZ)
 *
 **********************************************************/

#define LZ_SECOND_STAGE_STACK_OFFSET (0x2000) /* Negative */
#define LZ_SECOND_STAGE_STACK_SIZE   (0x1000)

#define LZ_SECOND_STAGE_HEAP_OFFSET  (0x1000) /* Negative */
#define LZ_SECOND_STAGE_HEAP_SIZE    (0x1000)

#define LZ_SL_HEADER_OFFSET          (0x0)
#define LZ_SL_HEADER_SIZE            (0x4)

#define LZ_HEADER_OFFSET             (0x4)
#define LZ_HEADER_SIZE               (0x1c)

#define LZ_FIRST_STAGE_STACK_START   (0x200)
#define LZ_FIRST_STAGE_STACK_SIZE    (0x1e0)

#define LZ_DATA_SECTION_SIZE         (0x200)

#define LZ_DEV_TABLE_OFFSET          (0xa000)
#define LZ_DEV_TABLE_SIZE            (0x3000)

#define LZ_PAGE_TABLES_OFFSET        (0xd000)
#define LZ_PAGE_TABLES_SIZE          (0x3000)

#endif /* __CONFIG_H__ */
