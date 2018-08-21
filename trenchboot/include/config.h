#ifndef __CONFIG_H__
#define __CONFIG_H__

/**********************************************************
 * LZ fixed memory layout
 *
 * +---------------+ EAX - 0x2000
 * |   Second      |
 * |   Stage       |
 * |   Stack       |
 * +---------------+ EAX - 0x1000
 * |   Second      |
 * |   Stage       |
 * |   Heap        |
 * +---------------+ EAX (begin LZ)
 * |   LZ Header   | [0x24b]
 * | ------------- |
 * |   First       | [0x1ccb]
 * |   Stage       |
 * |   Stack       |
 * | ------------- |
 * |  Realmode     | [0x10b]
 * |  Header       |
 * | ------------- | EAX + 0x200
 * |               |
 * |    Code       |
 * |               |
 * | ------------- | EAX + 0xa000
 * |     DEV       | [0x3000b]
 * |    Table      |
 * | ------------- | EAX + 0xd000
 * |    Page       | [0x3000b]
 * |   Tables      |
 * +---------------+ EAX + 0x10000 (end of LZ)
 * |   Rest of     |
 * |    Image      |
 *       ...
 *
 **********************************************************/

#define LZ_SECOND_STAGE_STACK_OFFSET (0x2000) /* Negative */
#define LZ_SECOND_STAGE_STACK_SIZE   (0x1000)

#define LZ_SECOND_STAGE_HEAP_OFFSET  (0x1000) /* Negative */
#define LZ_SECOND_STAGE_HEAP_SIZE    (0x1000)

#define LZ_SL_HEADER_OFFSET          (0x0)
#define LZ_SL_HEADER_SIZE            (0x24)

#define LZ_FIRST_STAGE_STACK_START   (0x1f0)
#define LZ_FIRST_STAGE_STACK_SIZE    (0x1cc)

#define LZ_RM_HEADER_OFFSET          (0x1f0)
#define LZ_RM_HEADER_SIZE            (0x10)

#define LZ_DEV_TABLE_OFFSET          (0xa000)
#define LZ_DEV_TABLE_SIZE            (0x3000)

#define LZ_PAGE_TABLES_OFFSET        (0xd000)
#define LZ_PAGE_TABLES_SIZE          (0x3000)

#endif /* __CONFIG_H__ */
