/*
 * txt.h: Intel(r) TXT support functions
 *
 * Copyright (c) 2003-2008, Intel Corporation
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

#ifndef __TXT_TXT_H__
#define __TXT_TXT_H__

/*
 * error values for processor error codes (ERRORCODE.external = 0)
 */
#define TXT_ERR_PROC_LEGACY_SHUTDOWN          0
#define TXT_ERR_PROC_INVALID_ACM_MEM_TYPE     5
#define TXT_ERR_PROC_UNSUPPORTED_ACM          6
#define TXT_ERR_PROC_AUTH_FAIL                7
#define TXT_ERR_PROC_INVALID_ACM_FORMAT       8
#define TXT_ERR_PROC_UNEXPECTED_HITM          9
#define TXT_ERR_PROC_INVALID_EVENT           10
#define TXT_ERR_PROC_INVALID_JOIN_FORMAT     11
#define TXT_ERR_PROC_UNRECOVERABLE_MCE       12
#define TXT_ERR_PROC_VMX_ABORT               13
#define TXT_ERR_PROC_ACM_CORRUPT             14
#define TXT_ERR_PROC_INVALID_VIDB_RATIO      15

/*
 * for SW errors (ERRORCODE.external = 1)
 */
typedef union {
    uint32_t _raw;
    struct {
        uint32_t  err1     : 15;     /* specific to src */
        uint32_t  src      : 1;      /* 0=ACM, 1=other */
        uint32_t  err2     : 14;     /* specific to src */
        uint32_t  external : 1;      /* always 1 for this type */
        uint32_t  valid    : 1;      /* always 1 */
    };
} txt_errorcode_sw_t;

/*
 * ACM errors (txt_errorcode_sw_t.src=0), format of err1+src+err2 fields
 */
typedef union __attribute__((packed)){
    uint32_t _raw;
    struct __attribute__((packed)){
        uint32_t acm_type  : 4;  /* 0000=BIOS ACM, 0001=SINIT, */
                                 /* 0010-1111=reserved */
        uint32_t progress  : 6;
        uint32_t error     : 5;
        uint32_t src       : 1;  /* above value */
        union __attribute__((packed)){
            struct __attribute__((packed)) {  /* progress=0x0d, error=1010 */
                uint32_t tpm_err    : 9;
                uint32_t reserved1  : 5;
            };
            struct __attribute__((packed)) {  /* progress=0x10 */
                uint32_t lcp_minor  : 6;
                uint32_t lcp_index  : 3;
                uint32_t reserved2  : 5;
            };
        }; /* sub-error */
    };
} acmod_error_t;

/* TPM event log types */
#define EVTLOG_UNKNOWN       0
#define EVTLOG_TPM12         1
#define EVTLOG_TPM2_LEGACY   2
#define EVTLOG_TPM2_TCG      3

/*
 * TXT configuration registers (offsets from TXT_{PUB, PRIV}_CONFIG_REGS_BASE)
 */

#define TXT_PUB_CONFIG_REGS_BASE       0xfed30000
#define TXT_PRIV_CONFIG_REGS_BASE      0xfed20000

#define TXT_CONFIG_REGS_SIZE           (TXT_PUB_CONFIG_REGS_BASE - \
                                        TXT_PRIV_CONFIG_REGS_BASE)

/* offsets to config regs (from either public or private _BASE) */
#define TXTCR_STS                   0x0000
#define TXTCR_ESTS                  0x0008
#define TXTCR_ERRORCODE             0x0030
#define TXTCR_CMD_RESET             0x0038
#define TXTCR_CMD_CLOSE_PRIVATE     0x0048
#define TXTCR_VER_FSBIF             0x0100
#define TXTCR_DIDVID                0x0110
#define TXTCR_VER_QPIIF             0x0200
#define TXTCR_CMD_UNLOCK_MEM_CONFIG 0x0218
#define TXTCR_SINIT_BASE            0x0270
#define TXTCR_SINIT_SIZE            0x0278
#define TXTCR_MLE_JOIN              0x0290
#define TXTCR_HEAP_BASE             0x0300
#define TXTCR_HEAP_SIZE             0x0308
#define TXTCR_MSEG_BASE             0x0310
#define TXTCR_MSEG_SIZE             0x0318
#define TXTCR_DPR                   0x0330
#define TXTCR_CMD_OPEN_LOCALITY1    0x0380
#define TXTCR_CMD_CLOSE_LOCALITY1   0x0388
#define TXTCR_CMD_OPEN_LOCALITY2    0x0390
#define TXTCR_CMD_CLOSE_LOCALITY2   0x0398
#define TXTCR_PUBLIC_KEY            0x0400
#define TXTCR_CMD_SECRETS           0x08e0
#define TXTCR_CMD_NO_SECRETS        0x08e8
#define TXTCR_E2STS                 0x08f0

/*
 * format of ERRORCODE register
 */
typedef union {
    uint64_t _raw;
    struct {
        uint64_t   type       : 30;    /* external-specific error code */
        uint64_t   external   : 1;     /* 0=from proc, 1=from external SW */
        uint64_t   valid      : 1;     /* 1=valid */
    };
} txt_errorcode_t;

/*
 * format of ESTS register
 */
typedef union {
    uint64_t _raw;
    struct {
        uint64_t   txt_reset_sts      : 1;
    };
} txt_ests_t;

/*
 * format of E2STS register
 */
typedef union {
    uint64_t _raw;
    struct {
        uint64_t   reserved             : 1;
        uint64_t   secrets_sts          : 1;
    };
} txt_e2sts_t;

/*
 * format of STS register
 */
typedef union {
    uint64_t _raw;
    struct {
        uint64_t   senter_done_sts         : 1;
        uint64_t   sexit_done_sts          : 1;
        uint64_t   reserved1               : 4;
        uint64_t   mem_config_lock_sts     : 1;
        uint64_t   private_open_sts        : 1;
        uint64_t   reserved2               : 7;
        uint64_t   locality_1_open_sts     : 1;
        uint64_t   locality_2_open_sts     : 1;
    };
} txt_sts_t;

/*
 * format of DIDVID register
 */
typedef union {
    uint64_t _raw;
    struct {
        uint16_t  vendor_id;
        uint16_t  device_id;
        uint16_t  revision_id;
        uint16_t  reserved;
    };
} txt_didvid_t;

/*
 * format of VER.FSBIF and VER.QPIIF registers
 */
typedef union {
    uint64_t _raw;
    struct {
        uint64_t  reserved       : 31;
        uint64_t  prod_fused     : 1;
    };
} txt_ver_fsbif_qpiif_t;

/*
 * format of DPR register
 */
typedef union {
    uint64_t _raw;
    struct {
        uint64_t  lock           : 1;
        uint64_t  reserved1      : 3;
        uint64_t  size           : 8;
        uint64_t  reserved2      : 8;
        uint64_t  top            : 12;
        uint64_t  reserved3      : 32;
    };
} txt_dpr_t;

/*
 * RLP JOIN structure for GETSEC[WAKEUP] and MLE_JOIN register
 */
typedef struct {
    uint32_t	gdt_limit;
    uint32_t	gdt_base;
    uint32_t	seg_sel;               /* cs (ds, es, ss are seg_sel+8) */
    uint32_t	entry_point;           /* phys addr */
} mle_join_t;

/*
 * format of MSEG header
 */
typedef struct {
    uint32_t    revision_id;
    uint32_t    smm_mon_feat;
    uint32_t    gdtr_limit;
    uint32_t    gdtr_base_offset;
    uint32_t    cs_sel;
    uint32_t    eip_offset;
    uint32_t    esp_offset;
    uint32_t    cr3_offset;
} mseg_hdr_t;

/*
 * fns to read/write TXT config regs
 */

static inline uint64_t read_config_reg(uint32_t config_regs_base, uint32_t reg)
{
    /* these are MMIO so make sure compiler doesn't optimize */
    return *(volatile uint64_t *)(unsigned long)(config_regs_base + reg);
}

static inline void write_config_reg(uint32_t config_regs_base, uint32_t reg,
                                    uint64_t val)
{
    /* these are MMIO so make sure compiler doesn't optimize */
    *(volatile uint64_t *)(unsigned long)(config_regs_base + reg) = val;
}

static inline uint64_t read_pub_config_reg(uint32_t reg)
{
    return read_config_reg(TXT_PUB_CONFIG_REGS_BASE, reg);
}

static inline void write_pub_config_reg(uint32_t reg, uint64_t val)
{
    write_config_reg(TXT_PUB_CONFIG_REGS_BASE, reg, val);
}

static inline uint64_t read_priv_config_reg(uint32_t reg)
{
    return read_config_reg(TXT_PRIV_CONFIG_REGS_BASE, reg);
}

static inline void write_priv_config_reg(uint32_t reg, uint64_t val)
{
    write_config_reg(TXT_PRIV_CONFIG_REGS_BASE, reg, val);
}

extern void txt_display_errors(void);
extern bool txt_has_error(void);
extern int supports_txt(void);
extern int txt_verify_platform(void);
extern int txt_launch_environment(loader_ctx *lctx);
extern int txt_launch_racm(loader_ctx *lctx);
extern void txt_post_launch(void);
extern int txt_post_launch_verify_platform(void);
extern void txt_shutdown(void);
extern bool txt_is_powercycle_required(void);
extern int get_evtlog_type(void);

#endif /* __TXT_TXT_H__ */

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

