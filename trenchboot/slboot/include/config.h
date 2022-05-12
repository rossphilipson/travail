/*
 * config.h: project-wide definitions
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
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

/*
 * build/support flags
 */

/* address tboot will load and execute at */
#define TBOOT_START              0x02C04000

/* start address of tboot MLE page table, also the beginning of tboot memory */
#define TBOOT_BASE_ADDR          0x02C00000

/* these addrs must be in low memory so that they are mapped by the */
/* kernel at startup */

/* address/size for memory-resident serial log (when enabled) */
#define TBOOT_SERIAL_LOG_ADDR        0x60000
#define TBOOT_SERIAL_LOG_SIZE        0x08000

/* address/size for AP wakeup code block */
#define TBOOT_AP_WAKE_BLOCK_ADDR     (TBOOT_SERIAL_LOG_ADDR + \
				      TBOOT_SERIAL_LOG_SIZE)
#define TBOOT_AP_WAKE_BLOCK_SIZE     0x04000


/* address/size for modified e820 table */
#define TBOOT_E820_COPY_ADDR         (TBOOT_AP_WAKE_BLOCK_ADDR + \
				      TBOOT_AP_WAKE_BLOCK_SIZE)
#define TBOOT_E820_COPY_SIZE         0x02000

/* Location for MLE page tables < 1M */
/* 1 PDP + 1 PD + 18 PTs = 36M total */
#define SLBOOT_MLEPT_ADDR            (TBOOT_E820_COPY_ADDR + \
                                      TBOOT_E820_COPY_SIZE)
#define SLBOOT_MLEPT_PAGES           20
#define SLBOOT_MLEPT_SIZE            (PAGE_SIZE*SLBOOT_MLEPT_PAGES)
#define SLBOOT_MLEPT_PAGE_TABLES     (SLBOOT_MLEPT_PAGES - 2)
#define SLBOOT_MLEPT_PAGES_COVERED   (SLBOOT_MLEPT_PAGE_TABLES*512)
#define SLBOOT_MLEPT_BYTES_COVERED   (SLBOOT_MLEPT_PAGES_COVERED*PAGE_SIZE)

#define DLMOD_TABLE_ADDR             (SLBOOT_MLEPT_ADDR + SLBOOT_MLEPT_SIZE)
#define DLMOD_TABLE_SIZE             0x01000

/* Used as a basic cmdline buffer size for copying cmdlines */
#define TBOOT_KERNEL_CMDLINE_SIZE    0x0400

#ifndef NR_CPUS
#define NR_CPUS     512
#endif

#ifdef __ASSEMBLY__
#define ENTRY(name)                             \
  .globl name;                                  \
  .align 16,0x90;                               \
  name:
#else
extern char _start[];            /* start of tboot */
extern char _end[];              /* end of tboot */
#endif


#define COMPILE_TIME_ASSERT(e)                 \
{                                              \
    struct tmp {                               \
        int a : ((e) ? 1 : -1);                \
    };                                         \
}


#define __data     __attribute__ ((__section__ (".data")))
#define __text     __attribute__ ((__section__ (".text")))

#define __packed   __attribute__ ((packed))

/* tboot log level */
#ifdef NO_TBOOT_LOGLVL
#define TBOOT_NONE
#define TBOOT_ERR
#define TBOOT_WARN
#define TBOOT_INFO
#define TBOOT_DETA
#define TBOOT_ALL
#else /* NO_TBOOT_LOGLVL */
#define TBOOT_NONE       "<0>"
#define TBOOT_ERR        "<1>"
#define TBOOT_WARN       "<2>"
#define TBOOT_INFO       "<3>"
#define TBOOT_DETA       "<4>"
#define TBOOT_ALL        "<5>"
#endif /* NO_TBOOT_LOGLVL */

#endif /* __CONFIG_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
