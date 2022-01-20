/*
 * slexec.h: main header definition file
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
#ifndef __SLEXEC_H__
#define __SLEXEC_H__

/* address slexec will load and execute at */
#define SLEXEC_START              0x02C04000

/*
 * TODO this address is important due to non-PIC code but rather
 * arbitrary and hardcoded. Can slexec be made PIC/PIE? Or some
 * other option?
 */
/* the beginning of slexec memory */
#define SLEXEC_BASE_ADDR          0x02C00000

/* these addrs must be in low memory so that they are mapped by the */
/* kernel at startup */

/* address/size for memory-resident serial log (when enabled) */
/* TODO back this up say to 0x40000 so it doesn't crash into the EBDA region? */
#define SLEXEC_SERIAL_LOG_ADDR         0x60000
#define SLEXEC_SERIAL_LOG_SIZE         0x08000

/* address/size for AP wakeup code block */
#define SLEXEC_AP_WAKE_BLOCK_ADDR      (SLEXEC_SERIAL_LOG_ADDR + \
				        SLEXEC_SERIAL_LOG_SIZE)
#define SLEXEC_AP_WAKE_BLOCK_SIZE      0x04000

/* address/size for TPM event log */
#define SLEXEC_EVENT_LOG_ADDR          (SLEXEC_AP_WAKE_BLOCK_ADDR + \
                                        SLEXEC_AP_WAKE_BLOCK_SIZE)
#define SLEXEC_EVENT_LOG_SIZE          0x08000

/* address/size for modified e820 table */
#define SLEXEC_E820_COPY_ADDR          (SLEXEC_EVENT_LOG_ADDR + \
				        SLEXEC_EVENT_LOG_SIZE)
#define SLEXEC_E820_COPY_SIZE          0x02000

/* Location for MLE page tables < 1M */
/* 1 PDP + 1 PD + 18 PTs = 36M total */
#define SLEXEC_MLEPT_ADDR              (SLEXEC_E820_COPY_ADDR + \
                                        SLEXEC_E820_COPY_SIZE)
#define SLEXEC_MLEPT_PAGES             20
#define SLEXEC_MLEPT_SIZE              (PAGE_SIZE*SLEXEC_MLEPT_PAGES)
#define SLEXEC_MLEPT_PAGE_TABLES       (SLEXEC_MLEPT_PAGES - 2)
#define SLEXEC_MLEPT_PAGES_COVERED     (SLEXEC_MLEPT_PAGE_TABLES*512)
#define SLEXEC_MLEPT_BYTES_COVERED     (SLEXEC_MLEPT_PAGES_COVERED*PAGE_SIZE)

/* Used as a basic cmdline buffer size for copying cmdlines */
#define SLEXEC_KERNEL_CMDLINE_SIZE     0x0400

/* TODO Fixed allocation values */
#define SLEXEC_FIXED_INITRD_BASE       0x20000000
#define SLEXEC_FIXED_SKL_BASE          0x40000000

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

#define SL_MAX_CPUS 512

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
} sl_hash_t;

static inline unsigned int get_hash_size(uint16_t hash_alg)
{
    if ( hash_alg == HASH_ALG_SHA1 || hash_alg == HASH_ALG_SHA1_LG )
        return SHA1_LENGTH;
    else if ( hash_alg == HASH_ALG_SHA256 )
        return SHA256_LENGTH;
    else if ( hash_alg == HASH_ALG_SM3 )
        return SM3_LENGTH;
    else if ( hash_alg == HASH_ALG_SHA384 )
        return SHA384_LENGTH;
    else if ( hash_alg == HASH_ALG_SHA512 )
        return SHA512_LENGTH;
    else
        return 0;
}

extern int sha1_buffer(const unsigned char *buffer, size_t len,
                       unsigned char md[20]);

extern void sha256_buffer(const unsigned char *buffer, size_t len,
                          unsigned char hash[32]);

#define SL_SHUTDOWN_REBOOT      0
#define SL_SHUTDOWN_SHUTDOWN    1
#define SL_SHUTDOWN_HALT        2

/*
 * used to log slexec printk output
 */
typedef struct {
    uuid_t     uuid;
    uint16_t   max_size;
    uint16_t   curr_pos;
    char       buf[];
} slexec_log_t;

/* {C0192526-6B30-4db4-844C-A3E953B88174} */
#define SLEXEC_LOG_UUID {0xc0192526, 0x6b30, 0x4db4, 0x844c, \
                              {0xa3, 0xe9, 0x53, 0xb8, 0x81, 0x74 }}

#define SL_ARCH_NONE   0
#define SL_ARCH_TXT    1
#define SL_ARCH_SKINIT 2

extern char _start[];            /* start of slexec */
extern char _end[];              /* end of slexec */

/* slexec log level */
#ifdef NO_SLEXEC_LOGLVL
#define SLEXEC_NONE
#define SLEXEC_ERR
#define SLEXEC_WARN
#define SLEXEC_INFO
#define SLEXEC_DETA
#define SLEXEC_ALL
#else /* NO_SLEXEC_LOGLVL */
#define SLEXEC_NONE       "<0>"
#define SLEXEC_ERR        "<1>"
#define SLEXEC_WARN       "<2>"
#define SLEXEC_INFO       "<3>"
#define SLEXEC_DETA       "<4>"
#define SLEXEC_ALL        "<5>"
#endif /* NO_SLEXEC_LOGLVL */

#define SL_ERR_NONE                 0
#define SL_ERR_FATAL                1
#define SL_ERR_TPM_NOT_READY        2
#define SL_ERR_SMX_NOT_SUPPORTED    3
#define SL_ERR_TXT_NOT_SUPPORTED    4
#define SL_ERR_VTD_NOT_SUPPORTED    5
#define SL_ERR_SKINIT_NOT_SUPPORTED 6
#define SL_ERR_NO_SKL               7
#define SL_ERR_PREV_TXT_ERROR       9
#define SL_ERR_SINIT_NOT_PRESENT    10 /* SINIT ACM not provided */
#define SL_ERR_ACMOD_VERIFY_FAILED  11

extern void error_action(int error);

extern unsigned long get_slexec_mem_end(void);
extern uint32_t get_apic_base(void);

extern void debug_put_chars(void);

#endif /* !__ASSEMBLY__ */

#endif /* __SLEXEC_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
