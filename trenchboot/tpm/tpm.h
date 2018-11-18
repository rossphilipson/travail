/*
 * Copyright (c) 2018 Daniel P. Smith, Apertus Solutions, LLC
 *
 * The definitions in this header are extracted from the Trusted Computing
 * Group's "TPM Main Specification", Parts 1-3.
 *
 */

#ifndef _TPM_H
#define _TPM_H

/* Section 2.2.3 */
#define TPM_AUTH_DATA_USAGE uint8_t
#define TPM_PAYLOAD_TYPE uint8_t
#define TPM_VERSION_BYTE uint8_t
#define TPM_TAG uint16_t
#define TPM_PROTOCOL_ID uint16_t
#define TPM_STARTUP_TYPE uint16_t
#define TPM_ENC_SCHEME uint16_t
#define TPM_SIG_SCHEME uint16_t
#define TPM_MIGRATE_SCHEME uint16_t
#define TPM_PHYSICAL_PRESENCE uint16_t
#define TPM_ENTITY_TYPE uint16_t
#define TPM_KEY_USAGE uint16_t
#define TPM_EK_TYPE uint16_t
#define TPM_STRUCTURE_TAG uint16_t
#define TPM_PLATFORM_SPECIFIC uint16_t
#define TPM_COMMAND_CODE uint32_t
#define TPM_CAPABILITY_AREA uint32_t
#define TPM_KEY_FLAGS uint32_t
#define TPM_ALGORITHM_ID uint32_t
#define TPM_MODIFIER_INDICATOR uint32_t
#define TPM_ACTUAL_COUNT uint32_t
#define TPM_TRANSPORT_ATTRIBUTES uint32_t
#define TPM_AUTHHANDLE uint32_t
#define TPM_DIRINDEX uint32_t
#define TPM_KEY_HANDLE uint32_t
#define TPM_PCRINDEX uint32_t
#define TPM_RESULT uint32_t
#define TPM_RESOURCE_TYPE uint32_t
#define TPM_KEY_CONTROL uint32_t
#define TPM_NV_INDEX uint32_t The
#define TPM_FAMILY_ID uint32_t
#define TPM_FAMILY_VERIFICATION uint32_t
#define TPM_STARTUP_EFFECTS uint32_t
#define TPM_SYM_MODE uint32_t
#define TPM_FAMILY_FLAGS uint32_t
#define TPM_DELEGATE_INDEX uint32_t
#define TPM_CMK_DELEGATE uint32_t
#define TPM_COUNT_ID uint32_t
#define TPM_REDIT_COMMAND uint32_t
#define TPM_TRANSHANDLE uint32_t
#define TPM_HANDLE uint32_t
#define TPM_FAMILY_OPERATION uint32_t 

/* Section 6 */
#define TPM_TAG_RQU_COMMAND		0x00C1
#define TPM_TAG_RQU_AUTH1_COMMAND	0x00C2
#define TPM_TAG_RQU_AUTH2_COMMAND	0x00C3
#define TPM_TAG_RSP_COMMAND		0x00C4
#define TPM_TAG_RSP_AUTH1_COMMAND	0x00C5
#define TPM_TAG_RSP_AUTH2_COMMAND	0x00C6

/* Section 16 */
#define TPM_SUCCESS 0x0

struct tpm_extend_cmd {
	TPM_COMMAND_CODE ordinal;
	TPM_PCRINDEX pcr_num;
	TPM_DIGEST digest;
}

struct tpm_extend_resp {
	TPM_COMMAND_CODE ordinal;
	TPM_PCRVALUE digest;
}

struct tpm_cmd_buf {
	TPM_TAG tag;
	uint32_t size;
	TPM_RESULT result;
	union {
		struct tpm_extend_cmd extend;
	} cmd;
}

struct tpm_resp_buf {
	PM_TAG tag;
	uint32_t size;
	union {
		struct tpm_extend_resp extend;
		uint8_t paylaod[0];
	} resp;
}

struct tpm_digest {
	TPM_PCRINDEX pcr;
	union {
		struct tpm_sha1_digest sha1;
	} digest;
}

/* TPM Interface Specification functions */
uint8_t tis_request_locality(uint8_t l);
uint8_t tis_init(void);
size_t tis_send(struct tpm_cmd_buf *buf);
size_t tis_recv(struct tpm_resp_buf *buf);

/* TPM Commands */
uint8_t tpm_pcr_extend(struct tpm_digest *d);

#endif
