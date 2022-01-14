/*
 * tpm_20.c: TPM2.0-related support functions
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
#include <tpm_20.h>

u32 handle2048 = 0;

static u8 cmd_buf[MAX_COMMAND_SIZE];
static u8 rsp_buf[MAX_RESPONSE_SIZE];

#define reverse_copy_in(out, var) {\
    _reverse_copy((uint8_t *)(out), (uint8_t *)&(var), sizeof(var));\
    out += sizeof(var);\
}

#define reverse_copy_out(var, out) {\
    _reverse_copy((uint8_t *)&(var), (uint8_t *)(out), sizeof(var));\
    out += sizeof(var);\
}

static void reverse_copy_header(u32 cmd_code, TPM_CMD_SESSIONS_IN *sessions_in)
{
    u16 tag;

    if (sessions_in == NULL || sessions_in->num_sessions == 0)
        tag = TPM_ST_NO_SESSIONS;
    else
        tag = TPM_ST_SESSIONS;

    reverse_copy(cmd_buf, &tag, sizeof(tag));
    reverse_copy(cmd_buf + CMD_CC_OFFSET, &cmd_code, sizeof(cmd_code));
}

/*
 * Copy sized byte buffer from source to destination and
 * twiddle the bytes in the size field.
 *
 * This can be used for the any of the following
 * TPM 2.0 data structures, but is not limited to these:
 *
 *      ENCRYPTED_SECRET_2B
 *      TPM2B_DIGEST
 *      TPM2B_NONCE
 *      TPM2B_DATA
 *      etc. (any structures consisting of UINT16 followed by a
 *          byte buffer whose size is specified by the UINT16.
 *
 * Inputs:
 *
 *      dest -- pointer to SIZED_BYTE_BUFFER
 *      src -- pointer to SIZED_BYTE_BUFFER
 *
 * Outputs:
 *
 *      number of bytes copied
 */
static u16 reverse_copy_sized_buf_in(TPM2B *dest, TPM2B *src)
{
    int i;

    if (dest == NULL || src == NULL)
        return 0;

    reverse_copy(&dest->size, &src->size, sizeof(u16));
    for (i=0; i<src->size; i++)
        dest->buffer[i] = src->buffer[i];

    return sizeof(u16) + src->size;
}

/*
 * Inputs: dest->size should contain the buffer size of dest->buffer[]
 * Ouputs: dest->size should contain the final copied data size
 *
 * Return: 0, failed; 2+, succeed.
 */
static u16 reverse_copy_sized_buf_out(TPM2B *dest, TPM2B *src)
{
    u16 i, size;

    if (dest == NULL || src == NULL)
        return 0;

    reverse_copy(&size, &src->size, sizeof(u16));
    if ( size > dest->size )
        return 0;

    dest->size = size;
    for (i=0; i<dest->size; i++)
        dest->buffer[i] = src->buffer[i];

    return sizeof(u16) + dest->size;
}

static void reverse_copy_session_data_in(void **other,
                                         TPM_CMD_SESSION_DATA_IN *session_data,
                                         u32 *session_size)
{
    *session_size += sizeof(u32) + sizeof( u16 ) +
        session_data->nonce.t.size + sizeof( u8 ) +
        sizeof( u16 ) + session_data->hmac.t.size;

    /* copy session handle */
    reverse_copy_in(*other, session_data->session_handle);

    /* Copy nonce */
    *other += reverse_copy_sized_buf_in((TPM2B *)*other, (TPM2B *)&session_data->nonce);

    /* Copy attributes */
    *((u8 *)*other) = *(u8 *)(void *)&(session_data->session_attr);
    *other += sizeof(u8);

    /* Copy hmac data */
    *other += reverse_copy_sized_buf_in((TPM2B *)*other, (TPM2B *)&session_data->hmac);
}

static void reverse_copy_sessions_in(void **other, TPM_CMD_SESSIONS_IN *sessions_in)
{
    int i;
    u32 session_size = 0;
    void *session_size_ptr = *other;

    if (sessions_in == NULL)
        return;

    if (sessions_in->num_sessions != 0) {
        *other += sizeof(u32);
        for (i=0; i<sessions_in->num_sessions; i++)
            reverse_copy_session_data_in(other,
                    &sessions_in->sessions[i], &session_size);
    }

    reverse_copy(session_size_ptr, &session_size, sizeof(u32));
}

static bool reverse_copy_session_data_out(TPM_CMD_SESSION_DATA_OUT *session_data,
                                          void **other)
{
    u16 size;

    if (session_data == NULL)
        return false;

    /* Copy nonce */
    session_data->nonce.t.size = sizeof(session_data->nonce.t.buffer);
    size = reverse_copy_sized_buf_out((TPM2B *)&(session_data->nonce),
            (TPM2B *)*other);
    if ( size == 0 )
        return false;
    *other += size;

    /* Copy sessionAttributes */
    *(u8 *)(void *)&(session_data->session_attr) = *((u8 *)*other);
    *other += sizeof(u8);

    /* Copy hmac */
    session_data->hmac.t.size = sizeof(session_data->hmac.t.buffer);
    size = reverse_copy_sized_buf_out((TPM2B *)&(session_data->hmac),
            (TPM2B *)*other);
    if ( size == 0 )
        return false;
    *other += size;

    return true;
}

static bool reverse_copy_sessions_out(TPM_CMD_SESSIONS_OUT *sessions_out,
                                      void *other, u16 rsp_tag,
                                      TPM_CMD_SESSIONS_IN *sessions_in)
{
    int i;

    if (sessions_in == NULL || sessions_out == NULL || rsp_tag != TPM_ST_SESSIONS)
        return false;

    sessions_out->num_sessions = sessions_in->num_sessions;
    for (i=0; i<sessions_in->num_sessions; i++)
        if ( !reverse_copy_session_data_out(&sessions_out->sessions[i], &other) )
            return false;

    return true;
}

typedef struct {
    u16         alg_id;
    u16         size;  /* Size of digest */
} HASH_SIZE_INFO;

HASH_SIZE_INFO hash_sizes[] = {
    {TPM_ALG_SHA1,          SHA1_DIGEST_SIZE},
    {TPM_ALG_SHA256,        SHA256_DIGEST_SIZE},
    {TPM_ALG_SHA384,        SHA384_DIGEST_SIZE},
    {TPM_ALG_SHA512,        SHA512_DIGEST_SIZE},
    {TPM_ALG_SM3_256,       SM3_256_DIGEST_SIZE},
    {TPM_ALG_NULL,0}
};

u16 get_digest_size(u16 id)
{
    unsigned int i;
    for(i=0; i<(sizeof(hash_sizes)/sizeof(HASH_SIZE_INFO)); i++) {
        if(hash_sizes[i].alg_id == id)
            return hash_sizes[i].size;
    }

    /* If not found, return 0 size, and let TPM handle the error. */
    return 0 ;
}

static bool reverse_copy_digest_values_out(TPML_DIGEST_VALUES *tpml_digest,
                                           void **other)
{
    unsigned int i, k, num_bytes;

    if (tpml_digest == NULL)
        return false;

    reverse_copy_out(tpml_digest->count, *other);
    if ( tpml_digest->count > HASH_COUNT )
        return false;

    for (i=0; i<tpml_digest->count; i++) {
        reverse_copy_out(tpml_digest->digests[i].hash_alg, *other);

        num_bytes = get_digest_size(tpml_digest->digests[i].hash_alg);

        for (k=0; k<num_bytes; k++) {
            tpml_digest->digests[i].digest.sha1[k] = *((u8 *)*other);
            *other += sizeof(u8);
        }
    }

    return true;
}

static uint32_t _tpm20_pcr_event(uint32_t locality,
                                 tpm_pcr_event_in *in,
                                 tpm_pcr_event_out *out)
{
    u32 ret;
    u32 cmd_size, rsp_size;
    u16 rsp_tag;
    void *other;

    reverse_copy_header(TPM_CC_PCR_Event, &in->sessions);

    other = (void *)cmd_buf + CMD_HEAD_SIZE;
    reverse_copy_in(other, in->pcr_handle);

    reverse_copy_sessions_in(&other, &in->sessions);

    other += reverse_copy_sized_buf_in((TPM2B *)other, (TPM2B *)&(in->data));

    /* Now set the command size field, now that we know the size of the whole command */
    cmd_size = (u8 *)other - cmd_buf;
    reverse_copy(cmd_buf + CMD_SIZE_OFFSET, &cmd_size, sizeof(cmd_size));

    rsp_size = sizeof(*out);

    if (g_tpm_family == TPM_IF_20_FIFO) {
        if (!tpm_submit_cmd(locality, cmd_buf, cmd_size, rsp_buf, &rsp_size))
            return TPM_RC_FAILURE;
        }
    if (g_tpm_family == TPM_IF_20_CRB) {
        if (!tpm_submit_cmd_crb(locality, cmd_buf, cmd_size, rsp_buf, &rsp_size))
            return TPM_RC_FAILURE;
        }

    reverse_copy(&ret, rsp_buf + RSP_RST_OFFSET, sizeof(ret));
    if ( ret != TPM_RC_SUCCESS )
        return ret;

    other = (void *)rsp_buf + RSP_HEAD_SIZE;
    reverse_copy(&rsp_tag, rsp_buf, sizeof(rsp_tag));
    if (rsp_tag == TPM_ST_SESSIONS)
        other += sizeof(u32);

    if ( !reverse_copy_digest_values_out(&out->digests, &other) )
        return TPM_RC_FAILURE;

    if ( !reverse_copy_sessions_out(&out->sessions, other, rsp_tag, &in->sessions) )
        return TPM_RC_FAILURE;

    return ret;
}

static uint32_t _tpm20_pcr_reset(uint32_t locality,
                                 tpm_pcr_reset_in *in,
                                 tpm_pcr_reset_out *out)
{
    u32 ret;
    u32 cmd_size, rsp_size;
    u16 rsp_tag;
    void *other;

    reverse_copy_header(TPM_CC_PCR_Reset, &in->sessions);

    other = (void *)cmd_buf + CMD_HEAD_SIZE;
    reverse_copy(other, &in->pcr_handle, sizeof(u32));

    other += sizeof(u32);
    reverse_copy_sessions_in(&other, &in->sessions);

    /* Now set the command size field, now that we know the size of the whole command */
    cmd_size = (u8 *)other - cmd_buf;
    reverse_copy(cmd_buf + CMD_SIZE_OFFSET, &cmd_size, sizeof(cmd_size));

    rsp_size = sizeof(*out);
    if (g_tpm_family == TPM_IF_20_FIFO) {
        if (!tpm_submit_cmd(locality, cmd_buf, cmd_size, rsp_buf, &rsp_size))
            return TPM_RC_FAILURE;
        }
    if (g_tpm_family == TPM_IF_20_CRB) {
        if (!tpm_submit_cmd_crb(locality, cmd_buf, cmd_size, rsp_buf, &rsp_size))
            return TPM_RC_FAILURE;
        }

    reverse_copy(&ret, rsp_buf + RSP_RST_OFFSET, sizeof(ret));
    if ( ret != TPM_RC_SUCCESS )
        return ret;

    other = (void *)rsp_buf + RSP_HEAD_SIZE;
    reverse_copy(&rsp_tag, rsp_buf, sizeof(rsp_tag));
    if (rsp_tag == TPM_ST_SESSIONS)
        other += sizeof(u32);

    if ( !reverse_copy_sessions_out(&out->sessions, other, rsp_tag, &in->sessions) )
        return TPM_RC_FAILURE;

    return ret;
}

TPM_CMD_SESSION_DATA_IN pw_session;
static void create_pw_session(TPM_CMD_SESSION_DATA_IN *ses)
{
    ses->session_handle = TPM_RS_PW;
    ses->nonce.t.size = 0;
    *((u8 *)((void *)&ses->session_attr)) = 0;
    ses->hmac.t.size = 0;
}

static bool tpm20_pcr_reset(struct tpm_if *ti, uint32_t locality, uint32_t pcr)
{
    tpm_pcr_reset_in reset_in;
    tpm_pcr_reset_out reset_out;
    u32 ret;

    reset_in.pcr_handle = pcr;
    reset_in.sessions.num_sessions = 1;
    reset_in.sessions.sessions[0] = pw_session;

    ret = _tpm20_pcr_reset(locality, &reset_in, &reset_out);
    if (ret != TPM_RC_SUCCESS) {
        printk(SLEXEC_WARN"TPM: Pcr %d Reset return value = %08X\n", pcr, ret);
        ti->error = ret;
        return false;
    }

    return true;
}

static bool alg_is_supported(u16 alg)
{
    for (int i=0; i<2; i++) {
        if (alg == slexec_alg_list[i])
            return true;
    }

    return false;
}

static bool tpm20_init(struct tpm_if *ti)
{
    u32 ret;
    unsigned int i;
    //tpm_info_list_t *info_list = get_tpm_info_list(g_sinit);

    if ( ti == NULL )
        return false;

    ti->cur_loc = 0;

    /* init version */
    ti->major = TPM20_VER_MAJOR;
    ti->minor = TPM20_VER_MINOR;

    /* init timeouts value */
    ti->timeout.timeout_a = TIMEOUT_A;
    ti->timeout.timeout_b = TIMEOUT_B;
    ti->timeout.timeout_c = TIMEOUT_C;
    ti->timeout.timeout_d = TIMEOUT_D;

    /* create one common password sesson*/
    create_pw_session(&pw_session);

    /* init supported alg list for banks */
    tpm_pcr_event_in event_in;
    tpm_pcr_event_out event_out;
    event_in.pcr_handle = 16;
    event_in.sessions.num_sessions = 1;
    event_in.sessions.sessions[0] = pw_session;
    event_in.data.t.size = 4;
    event_in.data.t.buffer[0] = 0;
    event_in.data.t.buffer[1] = 0xff;
    event_in.data.t.buffer[2] = 0x55;
    event_in.data.t.buffer[3] = 0xaa;
    ret = _tpm20_pcr_event(ti->cur_loc, &event_in, &event_out);
    if (ret != TPM_RC_SUCCESS) {
        printk(SLEXEC_WARN"TPM: PcrEvent not successful, return value = %08X\n", ret);
        ti->error = ret;
        return false;
    }
    ti->banks = event_out.digests.count;
    printk(SLEXEC_INFO"TPM: supported bank count = %d\n", ti->banks);
    for (i=0; i<ti->banks; i++) {
        ti->algs_banks[i] = event_out.digests.digests[i].hash_alg;;
        printk(SLEXEC_INFO"TPM: bank alg = %08x\n", ti->algs_banks[i]);
    }

    /* init supported alg list */
    ti->alg_count = 0;
    for (i=0; i<ti->banks; i++) {
        if (alg_is_supported(ti->algs_banks[i])) {
            ti->algs[ti->alg_count] = ti->algs_banks[i];
            ti->alg_count++;
        }
    }
    printk(SLEXEC_INFO"slexec: supported alg count = %d\n", ti->alg_count);
    for (unsigned int i=0; i<ti->alg_count; i++)
        printk(SLEXEC_INFO"slexec: hash alg = %08X\n", ti->algs[i]);

    /* reset debug PCR 16 */
    if (!tpm20_pcr_reset(ti, ti->cur_loc, 16)){
        printk(SLEXEC_WARN"TPM: tpm20_pcr_reset failed...\n");
	return false;
    }

    return true;
}

const struct tpm_if_fp tpm_20_if_fp = {
    .init = tpm20_init
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
