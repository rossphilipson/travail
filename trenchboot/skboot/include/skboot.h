/*
 * slboot.h: main header definition file
 *
 * Used to be:
 * skboot.h: shared data structure with MLE and kernel and functions
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
#ifndef __SKBOOT_H__
#define __SKBOOT_H__

/* address skboot will load and execute at */
#define SKBOOT_START              0x02C04000

/* the beginning of skboot memory */
#define SKBOOT_BASE_ADDR          0x02C00000

/* these addrs must be in low memory so that they are mapped by the */
/* kernel at startup */

/* address/size for memory-resident serial log (when enabled) */
#define SKBOOT_SERIAL_LOG_ADDR        0x60000
#define SKBOOT_SERIAL_LOG_SIZE        0x08000

/* address/size for TPM event log */
#define SKBOOT_EVENT_LOG_ADDR        (SKBOOT_SERIAL_LOG_ADDR + \
                                      SKBOOT_SERIAL_LOG_SIZE)
#define SKBOOT_EVENT_LOG_SIZE        0x08000

/* address/size for modified e820 table */
#define SKBOOT_E820_COPY_ADDR         (SKBOOT_EVENT_LOG_ADDR + \
				       SKBOOT_EVENT_LOG_SIZE)
#define SKBOOT_E820_COPY_SIZE         0x02000

/* Used as a basic cmdline buffer size for copying cmdlines */
#define SKBOOT_KERNEL_CMDLINE_SIZE    0x0400

/* Fixed allocation values */
#define SKBOOT_FIXED_INITRD_BASE       0x20000000
#define SKBOOT_FIXED_SKL_BASE          0x40000000
#define SKBOOT_FIXED_DMA_AREA_BASE     0x40080000
#define SKBOOT_FIXED_DMA_AREA_SIZE     0x4000

#define ENTRY(name)                             \
  .globl name;                                  \
  .align 16,0x90;                               \
  name:

#ifndef __ASSEMBLY__

#ifndef __packed
#define __packed   __attribute__ ((packed))
#endif

#define inline        __inline__
#define always_inline __inline__ __attribute__ ((always_inline))

#define __data     __attribute__ ((__section__ (".data")))
#define __text     __attribute__ ((__section__ (".text")))

#define __packed   __attribute__ ((packed))

#define COMPILE_TIME_ASSERT(e)                 \
{                                              \
    struct tmp {                               \
        int a : ((e) ? 1 : -1);                \
    };                                         \
}

typedef struct __packed {
  uint32_t    data1;
  uint16_t    data2;
  uint16_t    data3;
  uint16_t    data4;
  uint8_t     data5[6];
} uuid_t;

#define HASH_ALG_SHA1_LG 0x0000  /* legacy define for SHA1 */
#define HASH_ALG_SHA1    0x0004
#define HASH_ALG_SHA256  0x000B
#define HASH_ALG_SM3     0x0012
#define HASH_ALG_SHA384  0x000C
#define HASH_ALG_SHA512  0x000D
#define HASH_ALG_NULL    0x0010

#define SHA1_LENGTH        20
#define SHA256_LENGTH      32
#define SM3_LENGTH         32
#define SHA384_LENGTH      48
#define SHA512_LENGTH      64

typedef union {
    uint8_t    sha1[SHA1_LENGTH];
    uint8_t    sha256[SHA256_LENGTH];
    uint8_t    sm3[SM3_LENGTH];
    uint8_t    sha384[SHA384_LENGTH];
    uint8_t    sha512[SHA512_LENGTH];
} sk_hash_t;

extern int sha1_buffer(const unsigned char *buffer, size_t len,
                       unsigned char md[20]);

extern void sha256_buffer(const unsigned char *buffer, size_t len,
                          unsigned char hash[32]);

#define SK_SHUTDOWN_REBOOT      0
#define SK_SHUTDOWN_SHUTDOWN    1
#define SK_SHUTDOWN_HALT        2

/*
 * used to log skboot printk output
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
} skboot_log_t;

/* {C0192526-6B30-4db4-844C-A3E953B88174} */
#define SKBOOT_LOG_UUID   {0xc0192526, 0x6b30, 0x4db4, 0x844c, \
                             {0xa3, 0xe9, 0x53, 0xb8, 0x81, 0x74 }}

#define SLAUNCH_LZ_UUID {0x78f1268e, 0x0492, 0x11e9, 0x832a, \
                             {0xc8, 0x5b, 0x76, 0xc4, 0xcc, 0x03 }}

extern char _start[];            /* start of skboot */
extern char _end[];              /* end of skboot */

/* skboot log level */
#ifdef NO_SKBOOT_LOGLVL
#define SKBOOT_NONE
#define SKBOOT_ERR
#define SKBOOT_WARN
#define SKBOOT_INFO
#define SKBOOT_DETA
#define SKBOOT_ALL
#else /* NO_SKBOOT_LOGLVL */
#define SKBOOT_NONE       "<0>"
#define SKBOOT_ERR        "<1>"
#define SKBOOT_WARN       "<2>"
#define SKBOOT_INFO       "<3>"
#define SKBOOT_DETA       "<4>"
#define SKBOOT_ALL        "<5>"
#endif /* NO_SKBOOT_LOGLVL */

#define SK_ERR_NONE          0
#define SK_ERR_FATAL         1
#define SK_ERR_NO_SKINIT     2
#define SK_ERR_TPM_NOT_READY 3
#define SK_ERR_NO_SKL        4

extern void error_action(int error);

extern unsigned long get_skboot_mem_end(void);

#endif /* !__ASSEMBLY__ */

#endif    /* __SKBOOT_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
