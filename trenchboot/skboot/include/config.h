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

/* address skboot will load and execute at */
#define SKBOOT_START              0x02C04000

/* the beginning of skboot memory */
#define SKBOOT_BASE_ADDR          0x02C00000

/* these addrs must be in low memory so that they are mapped by the */
/* kernel at startup */

/* address/size for memory-resident serial log (when enabled) */
#define SKBOOT_SERIAL_LOG_ADDR        0x60000
#define SKBOOT_SERIAL_LOG_SIZE        0x08000

/* address/size for modified e820 table */
#define SKBOOT_E820_COPY_ADDR         (SKBOOT_SERIAL_LOG_ADDR + \
				      SKBOOT_SERIAL_LOG_SIZE)
#define SKBOOT_E820_COPY_SIZE         0x02000

/* Used as a basic cmdline buffer size for copying cmdlines */
#define SKBOOT_KERNEL_CMDLINE_SIZE    0x0400

#ifndef NR_CPUS
#define NR_CPUS     512
#endif

#ifdef __ASSEMBLY__
#define ENTRY(name)                             \
  .globl name;                                  \
  .align 16,0x90;                               \
  name:
#else
extern char _start[];            /* start of skboot */
extern char _end[];              /* end of skboot */
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
