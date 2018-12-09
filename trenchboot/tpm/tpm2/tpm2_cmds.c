

#include "tpm2.h"

#define SHA1_SIZE	20
#define SHA256_SIZE	32
#define SHA384_SIZE	48
#define SHA512_SIZE	64
#define SM3256_SIZE	32

int8_t tpm2_alloc_cmd(struct tpm_cmd *c, uint16_t tag, uint32_t code)
{
	c->raw = malloc(PAGE_SIZE);
	if (!c->raw)
		return -ENOMEM;

	c->header = (struct tpm_header *)c->raw;
	c->header.tag = cpu_to_be16(tag);
	c->header.code = cpu_to_be32(code);
	// wait unitl the end to convert
	c->header.size = sizeof(struct tpm_header); 

	return 0
}

static uint16_t convert_digest_list(struct tpml_digest_values *digests)
{
	int i;
	uint16_t size = 0;
	struct tpmt_ha *h = digests->digests;

	for (i=0; i<digests->count; i++) {
		switch(h->alg) {
		case TPM_ALG_SHA1:
			h->alg = cpu_to_be16(h->alg);
			h = (struct tpmt_ha *)((uint8_t *)h + SHA1_SIZE);
			size += sizeof(uint_16_t) + SHA1_SIZE;
			break;
		case TPM_ALG_SHA256:
			h->alg = cpu_to_be16(h->alg);
			h = (struct tpmt_ha *)((uint8_t *)h + SHA256_SIZE);
			size += sizeof(uint_16_t) + SHA256_SIZE;
			break;
		case TPM_ALG_SHA384:
			h->alg = cpu_to_be16(h->alg);
			h = (struct tpmt_ha *)((uint8_t *)h + SHA384_SIZE);
			size += sizeof(uint_16_t) + SHA384_SIZE;
			break;
		case TPM_ALG_SHA512:
			h->alg = cpu_to_be16(h->alg);
			h = (struct tpmt_ha *)((uint8_t *)h + SHA512_SIZE);
			size += sizeof(uint_16_t) + SHA512_SIZE;
			break;
		case TPM_ALG_SM3256:
			h->alg = cpu_to_be16(h->alg);
			h = (struct tpmt_ha *)((uint8_t *)h + SM3256_SIZE);
			size += sizeof(uint_16_t) + SHA1_SIZE;
			break;
		default:
			return 0;
		}
	}

	return size;
}

int8_t tpm2_extend_pcr(uint32_t pcr, struct tpml_digest_values *digests)
{
	struct tpm_cmd cmd;
	uint8_t *ptr;
	uint16_t size;
	int8_t ret = 0;

	ret = tpm2_alloc_cmd(&cmd, TPM_ST_SESSIONS, TPM_CC_PCR_EXTEND);
	if (ret < 0)
		return ret;

	cmd.handles = (uint32_t *)(cmd.raw + cmd.size);
	*cmd.handles = cpu_to_be32(pcr);
	cmd.header->size += sizeof(uint32_t);

	cmd.auth = (struct tpmb *)(cmd.raw + cmd.size);
	cmd.auth->size = tpm2_null_auth(cmd.auth->buffer);
	cmd.header->size += cmd.auth->size;
	cmd.auth->size = cpu_to_be16(cmd.auth->size);

	cmd.params = (uint8_t *)(cmd.raw + cmd.size);
	size = convert_digest_list(digests);
	if (size == 0) {
		if (cmd.raw)
			free(cmd.raw);
		return -EINVAL;
	}
	memcpy(cmd.params, digests, size);
	cmd.header->size = cpu_to_be16(cmd.header->size + size);

	/* TODO: write the freaking send command */
	ret = tpm2_send(&cmd);
	free(cmd.raw);

	return ret;
}
