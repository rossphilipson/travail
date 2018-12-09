/*
 * Copyright (c) 2018 Daniel P. Smith, Apertus Solutions, LLC
 *
 * The definitions in this header are extracted and/or dervied from the
 * Trusted Computing Group's TPM 2.0 Library Specification Parts 1&2.
 *
 */

#ifndef _TPM2_H
#define _TPM2_H

#include <types.h>
#include "tpm2_constants.h"


/* Table 192  Definition of TPM2B_TEMPLATE Structure:
 *   Using this as the base structure similar to the spec
 */
struct tpm2b {
	uint16_t size;
	uint8_t	buffer[0];
};

// Table 32  Definition of TPMA_SESSION Bits <  IN/OUT>
struct tpma_session{
	uint8_t continue_session  : 1;
	uint8_t audit_exclusive	  : 1;
	uint8_t audit_reset       : 1;
	uint8_t reserved3_4       : 2;
	uint8_t decrypt           : 1;
	uint8_t encrypt           : 1;
	uint8_t audit             : 1;
};


// Table 72  Definition of TPMT_HA Structure <  IN/OUT>
struct tpmt_ha {
	uint16_t alg;		/* TPMI_ALG_HASH	*/
	uint8_t digest[0];	/* TPMU_HA		*/
};

// Table 100  Definition of TPML_DIGEST_VALUES Structure
struct tpml_digest_values {
	uint32_t count;
	struct tpmt_ha digests[0];
};


// Table 124  Definition of TPMS_AUTH_COMMAND Structure <  IN>
struct tpms_auth_cmd {
	uint32_t *handle;
	struct tpm2b *nonce;
	struct tpma_session *attributes;
	struct tpm2b *hmac;
};

// Table 125  Definition of TPMS_AUTH_RESPONSE Structure <  OUT>
struct tpms_auth_resp {
	struct tpm2b *nonce;
	struct tpma_session *attributes;
	struct tpm2b *hmac;
};

struct tpm_header {
	uint16_t tag;		/* TPMI_ST_COMMAND_TAG	*/
	uint32_t size;		/* UINT32		*/
	uint32_t code;		/* TPM_CC		*/
};

struct tpm_cmd {
	struct tpm_header *header;
	uint32_t *handles[];	/* TPM_HANDLE		*/
	struct tpm2b *auth;	/* Authorization Area	*/
	uint8_t *params;	/* Parameters		*/
	uint8_t *raw;		/* internal raw buffer	*/
};

struct tpm_resp {
	struct tpm_header *header;
	uint32_t *handles[];	/* TPM_HANDLE		*/
	struct tpm2b *params;	/* Parameters		*/
	uint8_t *auth;		/* Authorization Area	*/
	uint8_t *raw;		/* internal raw buffer	*/
};

#endif
