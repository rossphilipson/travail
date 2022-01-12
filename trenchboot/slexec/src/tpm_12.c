/*
 * tpm_12.c: TPM1.2-related support functions
 *
 * Copyright (c) 2006-2013, Intel Corporation
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

#include <types.h>
#include <stdbool.h>
#include <slexec.h>
#include <printk.h>
#include <misc.h>
#include <processor.h>
#include <string.h>
#include <tpm.h>

/*
 * return code:
 * The TPM has five types of return code. One indicates successful operation
 * and four indicate failure.
 * TPM_SUCCESS (00000000) indicates successful execution.
 * The failure reports are:
 *      TPM defined fatal errors (00000001 to 000003FF)
 *      vendor defined fatal errors (00000400 to 000007FF)
 *      TPM defined non-fatal errors (00000800 to 00000BFF)
 *      vendor defined non-fatal errors (00000C00 to 00000FFF).
 * Here only give definitions for a few commonly used return code.
 */
#define TPM_BASE                0x00000000
#define TPM_NON_FATAL           0x00000800
#define TPM_SUCCESS             TPM_BASE
#define TPM_BADINDEX            (TPM_BASE + 2)
#define TPM_BAD_PARAMETER       (TPM_BASE + 3)
#define TPM_DEACTIVATED         (TPM_BASE + 6)
#define TPM_DISABLED            (TPM_BASE + 7)
#define TPM_FAIL                (TPM_BASE + 9)
#define TPM_BAD_ORDINAL         (TPM_BASE + 10)
#define TPM_NOSPACE             (TPM_BASE + 17)
#define TPM_NOTRESETABLE        (TPM_BASE + 50)
#define TPM_NOTLOCAL            (TPM_BASE + 51)
#define TPM_BAD_LOCALITY        (TPM_BASE + 61)
#define TPM_READ_ONLY           (TPM_BASE + 62)
#define TPM_NOT_FULLWRITE       (TPM_BASE + 70)
#define TPM_RETRY               (TPM_BASE + TPM_NON_FATAL)

typedef uint8_t tpm_locality_selection_t;

#define TPM_LOC_ZERO    0x01
#define TPM_LOC_ONE     0x02
#define TPM_LOC_TWO     0x04
#define TPM_LOC_THREE   0x08
#define TPM_LOC_FOUR    0x10
#define TPM_LOC_RSVD    0xE0

/* ~5 secs are required for Infineon that requires this, so leave some extra */
#define MAX_SAVESTATE_RETRIES       60

#define TPM_TAG_RQU_COMMAND         0x00C1
#define TPM_TAG_RQU_AUTH1_COMMAND   0x00C2
#define TPM_TAG_RQU_AUTH2_COMMAND   0x00C3
#define TPM_ORD_PCR_EXTEND          0x00000014
#define TPM_ORD_PCR_READ            0x00000015
#define TPM_ORD_PCR_RESET           0x000000C8
#define TPM_ORD_NV_READ_VALUE       0x000000CF
#define TPM_ORD_NV_WRITE_VALUE      0x000000CD
#define TPM_ORD_GET_CAPABILITY      0x00000065
#define TPM_ORD_SEAL                0x00000017
#define TPM_ORD_UNSEAL              0x00000018
#define TPM_ORD_OSAP                0x0000000B
#define TPM_ORD_OIAP                0x0000000A
#define TPM_ORD_SAVE_STATE          0x00000098
#define TPM_ORD_GET_RANDOM          0x00000046

#define TPM_TAG_PCR_INFO_LONG       0x0006
#define TPM_TAG_STORED_DATA12       0x0016

/*
 * specified as minimum cmd buffer size should be supported by all 1.2 TPM
 * device in the TCG_PCClientTPMSpecification_1-20_1-00_FINAL.pdf
 */
#define TPM_CMD_SIZE_MAX        768
#define TPM_RSP_SIZE_MAX        768

/*
 * The _tpm12_submit_cmd function comes with 2 global buffers: cmd_buf & rsp_buf.
 * Before calling, caller should fill cmd arguements into cmd_buf via
 * WRAPPER_IN_BUF macro. After calling, caller should fetch result from
 * rsp_buffer via WRAPPER_OUT_BUF macro.
 * cmd_buf content:
 *  0   1   2   3   4   5   6   7   8   9   10  ...
 * -------------------------------------------------------------
 * |  TAG  |     SIZE      |    ORDINAL    |    arguments ...
 * -------------------------------------------------------------
 * rsp_buf content:
 *  0   1   2   3   4   5   6   7   8   9   10  ...
 * -------------------------------------------------------------
 * |  TAG  |     SIZE      |  RETURN CODE  |    other data ...
 * -------------------------------------------------------------
 *
 *   locality : TPM locality (0 - 4)
 *   tag      : The TPM command tag
 *   cmd      : The TPM command ordinal
 *   arg_size : Size of argument data.
 *   out_size : IN/OUT paramter. The IN is the expected size of out data;
 *              the OUT is the size of output data within out buffer.
 *              The out_size MUST NOT be NULL.
 *   return   : TPM_SUCCESS for success, for other error code, refer to the .h
 */
static uint8_t     cmd_buf[TPM_CMD_SIZE_MAX];
static uint8_t     rsp_buf[TPM_RSP_SIZE_MAX];

__data tpm_pcr_value_t post_launch_pcr17, post_launch_pcr18;

#define WRAPPER_IN_BUF          (cmd_buf + CMD_HEAD_SIZE)
#define WRAPPER_OUT_BUF         (rsp_buf + RSP_HEAD_SIZE)
#define WRAPPER_IN_MAX_SIZE     (TPM_CMD_SIZE_MAX - CMD_HEAD_SIZE)
#define WRAPPER_OUT_MAX_SIZE    (TPM_RSP_SIZE_MAX - RSP_HEAD_SIZE)

static uint32_t _tpm12_submit_cmd(uint32_t locality, uint16_t tag, uint32_t cmd,  uint32_t arg_size, uint32_t *out_size)
{
    uint32_t    ret;
    uint32_t    cmd_size, rsp_size = 0;

    if ( out_size == NULL ) {
        printk(SLEXEC_WARN"TPM: invalid param for _tpm12_submit_cmd()\n");
        return TPM_BAD_PARAMETER;
    }

    /*
     * real cmd size should add 10 more bytes:
     *      2 bytes for tag
     *      4 bytes for size
     *      4 bytes for ordinal
     */
    cmd_size = CMD_HEAD_SIZE + arg_size;

    if ( cmd_size > TPM_CMD_SIZE_MAX ) {
        printk(SLEXEC_WARN"TPM: cmd exceeds the max supported size.\n");
        return TPM_BAD_PARAMETER;
    }

    /* copy tag, size & ordinal into buf in a reversed byte order */
    reverse_copy(cmd_buf, &tag, sizeof(tag));
    reverse_copy(cmd_buf + CMD_SIZE_OFFSET, &cmd_size, sizeof(cmd_size));
    reverse_copy(cmd_buf + CMD_CC_OFFSET, &cmd, sizeof(cmd));

    rsp_size = RSP_HEAD_SIZE + *out_size;
    rsp_size = (rsp_size > TPM_RSP_SIZE_MAX) ? TPM_RSP_SIZE_MAX: rsp_size;
    if ( !tpm_submit_cmd(locality, cmd_buf, cmd_size, rsp_buf, &rsp_size) ) return TPM_FAIL;

    /*
     * should subtract 10 bytes from real response size:
     *      2 bytes for tag
     *      4 bytes for size
     *      4 bytes for return code
     */
    rsp_size -= (rsp_size > RSP_HEAD_SIZE) ? RSP_HEAD_SIZE : rsp_size;

    reverse_copy(&ret, rsp_buf + RSP_RST_OFFSET, sizeof(uint32_t));
    if ( ret != TPM_SUCCESS )     return ret;

    if ( *out_size == 0 || rsp_size == 0 )        *out_size = 0;
    else
        *out_size = (rsp_size < *out_size) ? rsp_size : *out_size;

    return ret;
}

static inline uint32_t tpm12_submit_cmd(uint32_t locality, uint32_t cmd, uint32_t arg_size, uint32_t *out_size)
{
   return  _tpm12_submit_cmd(locality, TPM_TAG_RQU_COMMAND, cmd, arg_size, out_size);
}

static inline uint32_t tpm12_submit_cmd_auth1(uint32_t locality, uint32_t cmd,
                                            uint32_t arg_size, uint32_t *out_size)
{
   return  _tpm12_submit_cmd(locality, TPM_TAG_RQU_AUTH1_COMMAND, cmd,
                           arg_size, out_size);
}

static inline uint32_t tpm12_submit_cmd_auth2(uint32_t locality, uint32_t cmd,
                                            uint32_t arg_size, uint32_t *out_size)
{
   return  _tpm12_submit_cmd(locality, TPM_TAG_RQU_AUTH2_COMMAND, cmd,
                           arg_size, out_size);
}

typedef struct __packed {
    uint8_t     digest[SHA1_LENGTH];
} tpm12_digest_t;

#define TPM_CAP_VERSION_VAL 0x1A

typedef uint16_t tpm_structure_tag_t;

typedef struct __packed {
   uint8_t  major;
   uint8_t  minor;
   uint8_t  rev_major;
   uint8_t  rev_minor;
} tpm_version_t;

typedef struct __packed {
    tpm_structure_tag_t tag;
    tpm_version_t       version;
    uint16_t            specLevel;
    uint8_t             errataRev;
    uint8_t             tpmVendorID[4];
    uint16_t            vendorSpecificSize;
    uint8_t             vendorSpecific[];
} tpm_cap_version_info_t;

#define HMAC_BLOCK_SIZE     64
#define HMAC_OUTPUT_SIZE    20

#define UNLOAD_INTEGER(buf, offset, var) {\
    reverse_copy(buf + offset, &(var), sizeof(var));\
    offset += sizeof(var);\
}

#define UNLOAD_BLOB(buf, offset, blob, size) {\
    sl_memcpy(buf + offset, blob, size);\
    offset += size;\
}

#define UNLOAD_BLOB_TYPE(buf, offset, blob) \
    UNLOAD_BLOB(buf, offset, blob, sizeof(*(blob)))

#define LOAD_INTEGER(buf, offset, var) {\
    reverse_copy(&(var), buf + offset, sizeof(var));\
    offset += sizeof(var);\
}

#define LOAD_BLOB(buf, offset, blob, size) {\
    sl_memcpy(blob, buf + offset, size);\
    offset += size;\
}

typedef uint32_t tpm_capability_area_t;

#define TPM_CAP_NV_INDEX    0x00000011

static uint32_t tpm12_get_capability(uint32_t locality, tpm_capability_area_t cap_area,
                  uint32_t sub_cap_size, const uint8_t *sub_cap,
                  uint32_t *resp_size, uint8_t *resp)
{
    uint32_t ret, offset, out_size, size;

    if ( sub_cap == NULL || resp_size == NULL || resp == NULL ) {
        printk(SLEXEC_WARN"TPM: tpm12_get_capability() bad parameter\n");
        return TPM_BAD_PARAMETER;
    }

    offset = 0;
    UNLOAD_INTEGER(WRAPPER_IN_BUF, offset, cap_area);
    UNLOAD_INTEGER(WRAPPER_IN_BUF, offset, sub_cap_size);
    UNLOAD_BLOB(WRAPPER_IN_BUF, offset, sub_cap, sub_cap_size);

    out_size = sizeof(*resp_size) + *resp_size;

    ret = tpm12_submit_cmd(locality, TPM_ORD_GET_CAPABILITY, offset, &out_size);

#ifdef TPM_TRACE
    printk(SLEXEC_ERR"TPM: get capability, return value = %08X\n", ret);
#endif
    if ( ret != TPM_SUCCESS ) {
        printk(SLEXEC_ERR"TPM: get capability, return value = %08X\n", ret);
        return ret;
    }

    offset = 0;
    LOAD_INTEGER(WRAPPER_OUT_BUF, offset, size);
    if ( *resp_size < size ||
         size != out_size - sizeof(*resp_size) ) {
        printk(SLEXEC_WARN"TPM: capability response too small\n");
        return TPM_FAIL;
    }
    *resp_size = size;
    LOAD_BLOB(WRAPPER_OUT_BUF, offset, resp, *resp_size);

    return ret;
}

typedef struct __packed {
    tpm_structure_tag_t tag;
    uint8_t disable;
    uint8_t ownership;
    uint8_t deactivated;
    uint8_t read_pubek;
    uint8_t disable_owner_clear;
    uint8_t allow_maintenance;
    uint8_t physical_presence_lifetime_lock;
    uint8_t physical_presence_hw_enable;
    uint8_t physical_presence_cmd_enable;
    uint8_t cekp_used;
    uint8_t tpm_post;
    uint8_t tpm_post_lock;
    uint8_t fips;
    uint8_t operator;
    uint8_t enable_revoke_ek;
    uint8_t nv_locked;
    uint8_t read_srk_pub;
    uint8_t tpm_established;
    uint8_t maintenance_done;
    uint8_t disable_full_da_logic_info;
} tpm_permanent_flags_t;

typedef struct __packed {
    tpm_structure_tag_t tag;
    uint8_t deactivated;
    uint8_t disable_force_clear;
    uint8_t physical_presence;
    uint8_t phycical_presence_lock;
    uint8_t b_global_lock;
} tpm_stclear_flags_t;

#define TPM_CAP_FLAG            0x00000004
#define TPM_CAP_FLAG_PERMANENT  0x00000108
#define TPM_CAP_FLAG_VOLATILE   0x00000109

static uint32_t tpm12_get_flags(uint32_t locality, uint32_t flag_id,
                       uint8_t *flags, uint32_t flag_size)
{
    uint32_t ret, offset, resp_size;
    uint8_t sub_cap[sizeof(flag_id)];
    tpm_structure_tag_t tag;

    if ( flags == NULL ) {
        printk(SLEXEC_WARN"TPM: tpm12_get_flags() bad parameter\n");
        return TPM_BAD_PARAMETER;
    }

    offset = 0;
    UNLOAD_INTEGER(sub_cap, offset, flag_id);

    resp_size = flag_size;
    ret = tpm12_get_capability(locality, TPM_CAP_FLAG, sizeof(sub_cap),
                             sub_cap, &resp_size, flags);

#ifdef TPM_TRACE
    printk(SLEXEC_ERR"TPM: get flags %08X, return value = %08X\n", flag_id, ret);
#endif
    if ( ret != TPM_SUCCESS )
        return ret;

    /* 1.2 spec, main part 2, rev 103 add one more byte to permanent flags, to
       be backward compatible, not assume all expected bytes can be gotten */
    if ( resp_size > flag_size ) {
        printk(SLEXEC_WARN"TPM: tpm12_get_flags() response size too small\n");
        return TPM_FAIL;
    }

    offset = 0;
    LOAD_INTEGER(flags, offset, tag);
    offset = 0;
    UNLOAD_BLOB_TYPE(flags, offset, &tag);

    return ret;
}

#define TPM_CAP_PROPERTY          0x00000005
#define TPM_CAP_PROP_TIS_TIMEOUT  0x00000115

static uint32_t tpm12_get_timeout(uint32_t locality,
                       uint8_t *prop, uint32_t prop_size)
{
    uint32_t ret, offset, resp_size, prop_id = TPM_CAP_PROP_TIS_TIMEOUT;
    uint8_t sub_cap[sizeof(prop_id)];
    uint32_t resp[4];

    if ( (prop == NULL) || (prop_size < sizeof(resp)) ) {
        printk(SLEXEC_WARN"TPM: tpm12_get_timeout() bad parameter\n");
        return TPM_BAD_PARAMETER;
    }

    offset = 0;
    UNLOAD_INTEGER(sub_cap, offset, prop_id);

    resp_size = prop_size;
    ret = tpm12_get_capability(locality, TPM_CAP_PROPERTY, sizeof(sub_cap),
                             sub_cap, &resp_size, prop);

#ifdef TPM_TRACE
    printk(SLEXEC_ERR"TPM: get prop %08X, return value = %08X\n", prop_id, ret);
#endif
    if ( ret != TPM_SUCCESS )
        return ret;

    if ( resp_size != prop_size ) {
        printk(SLEXEC_WARN"TPM: tpm_get_property() response size incorrect\n");
        return TPM_FAIL;
    }

    offset = 0;
    LOAD_INTEGER(prop, offset, resp);
    offset = 0;
    UNLOAD_BLOB_TYPE(prop, offset, &resp);

    return ret;
}

/* ensure TPM is ready to accept commands */
static bool tpm12_init(struct tpm_if *ti)
{
    tpm_permanent_flags_t pflags;
    tpm_stclear_flags_t vflags;
    uint32_t timeout[4];
    uint32_t locality;
    uint32_t ret;

    if ( ti == NULL )
        return false;

    ti->cur_loc = 0;

    locality = ti->cur_loc;
    if ( !tpm_validate_locality(locality) ) {
        printk(SLEXEC_WARN"TPM is not available.\n");
        return false;
    }

    /* make sure tpm is not disabled/deactivated */
    sk_memset(&pflags, 0, sizeof(pflags));
    ret = tpm12_get_flags(locality, TPM_CAP_FLAG_PERMANENT,
                        (uint8_t *)&pflags, sizeof(pflags));
    if ( ret != TPM_SUCCESS ) {
        printk(SLEXEC_WARN"TPM is disabled or deactivated.\n");
        ti->error = ret;
        return false;
    }
    if ( pflags.disable ) {
        printk(SLEXEC_WARN"TPM is disabled.\n");
        return false;
    }

    sk_memset(&vflags, 0, sizeof(vflags));
    ret = tpm12_get_flags(locality, TPM_CAP_FLAG_VOLATILE,
                        (uint8_t *)&vflags, sizeof(vflags));
    if ( ret != TPM_SUCCESS ) {
        printk(SLEXEC_WARN"TPM is disabled or deactivated.\n");
        ti->error = ret;
        return false;
    }
    if ( vflags.deactivated ) {
        printk(SLEXEC_WARN"TPM is deactivated.\n");
        return false;
    }

    printk(SLEXEC_INFO"TPM is ready\n");
    printk(SLEXEC_ERR"TPM nv_locked: %s\n", (pflags.nv_locked != 0) ? "TRUE" : "FALSE");

    /* get tpm timeout values */
    ret = tpm12_get_timeout(locality, (uint8_t *)&timeout, sizeof(timeout));
    if ( ret != TPM_SUCCESS ) {
        printk(SLEXEC_WARN"TPM timeout values are not achieved, "
               "default values will be used.\n");
        ti->error = ret;
    } else {
        /*
         * timeout_x represents the number of milliseconds for the timeout
         * and timeout[x] represents the number of microseconds.
         */
        ti->timeout.timeout_a = timeout[0]/1000;
        ti->timeout.timeout_b = timeout[1]/1000;
        ti->timeout.timeout_c = timeout[2]/1000;
        ti->timeout.timeout_d = timeout[3]/1000;
        printk(SLEXEC_ERR"TPM timeout values: A: %u, B: %u, C: %u, D: %u\n",
               ti->timeout.timeout_a, ti->timeout.timeout_b, ti->timeout.timeout_c,
               ti->timeout.timeout_d);
        /*
         * if any timeout values are less than default values, set to default
         * value (due to bug with some TPMs)
         */
        if ( ti->timeout.timeout_a < TIMEOUT_A ) {
            ti->timeout.timeout_a = TIMEOUT_A;
            printk(SLEXEC_WARN"Wrong timeout A, fallback to %u\n", TIMEOUT_A);
        }
        if ( ti->timeout.timeout_b < TIMEOUT_B ) {
            ti->timeout.timeout_b = TIMEOUT_B;
            printk(SLEXEC_WARN"Wrong timeout B, fallback to %u\n", TIMEOUT_B);
        }
        if ( ti->timeout.timeout_c < TIMEOUT_C ) {
            ti->timeout.timeout_c = TIMEOUT_C;
            printk(SLEXEC_WARN"Wrong timeout C, fallback to %u\n", TIMEOUT_C);
        }
        if ( ti->timeout.timeout_d < TIMEOUT_D ) {
            ti->timeout.timeout_d = TIMEOUT_D;
            printk(SLEXEC_WARN"Wrong timeout D, fallback to %u\n", TIMEOUT_D);
        }
    }

    /* init version */
    ti->major = TPM12_VER_MAJOR;
    ti->minor = TPM12_VER_MINOR;

    /* init supported alg list */
    ti->banks = 1;
    ti->alg_count = 1;
    ti->algs[0] = HASH_ALG_SHA1;
    ti->extpol = SK_EXTPOL_FIXED;
    ti->cur_alg = HASH_ALG_SHA1;

    /* init NV index */
    ti->sk_policy_index = 0x20000001;
    ti->lcp_own_index = 0x40000001;
    ti->sk_err_index = 0x20000002;
    ti->sgx_svn_index = 0x50000004;

    return true;
}

static bool tpm12_check(void)
{
    uint32_t ret, out_size = 0;

    ret = tpm12_submit_cmd(0, 0xFFFFFFFF, 0, &out_size);

    return ( ret == TPM_BAD_ORDINAL );
}

const struct tpm_if_fp tpm_12_if_fp = {
    .init = tpm12_init,
    .check = tpm12_check,
};

/*
 * Local variables:
 * mode: C
 * c-set-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
