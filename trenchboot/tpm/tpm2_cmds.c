

#include <tpm.h>

#include "tpm_common.h"
#include "tpm2.h"
#include "tpm2_auth.h"

static int8_t tpm2_alloc_cmd(struct tpmbuff *b, struct tpm2_cmd *c, uint16_t tag,
		uint32_t code)
{
	c->raw = b->ops->reserve();
	if (!c->raw)
		return -ENOMEM;

	c->header = (struct tpm_header *)c->raw;
	c->header.tag = cpu_to_be16(tag);
	c->header.code = cpu_to_be32(code);

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

int8_t tpm2_extend_pcr(struct tpm *t, uint32_t pcr,
		struct tpml_digest_values *digests)
{
	struct tpmbuff *b = t->buff;
	struct tpm2_cmd cmd;
	uint8_t *ptr;
	uint16_t size;
	int8_t ret = 0;

	ret = tpm2_alloc_cmd(b, &cmd, TPM_ST_SESSIONS, TPM_CC_PCR_EXTEND);
	if (ret < 0)
		return ret;

	cmd.handles = (uint32_t *)b->ops->put(sizeof(uint32_t));
	*cmd.handles = cpu_to_be32(pcr);

	cmd.auth = (struct tpm2b *)b->ops->put(tpm2_null_auth_size());
	cmd.auth->size = tpm2_null_auth(cmd.auth->buffer);
	cmd.auth->size = cpu_to_be16(cmd.auth->size);

	size = convert_digest_list(digests);
	if (size == 0) {
		t->free();
		return -EINVAL;
	}
	cmd.params = (uint8_t *)b->ops->put(size);
	memcpy(cmd.params, digests, size);

	cmd.header->size = cpu_to_be16(b->ops->size);

	ret = t->ops->send(b);
	b->ops->free();

	return ret;
}
