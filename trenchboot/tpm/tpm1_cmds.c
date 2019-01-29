/*
 * Copyright (c) 2018 Daniel P. Smith, Apertus Solutions, LLC
 *
 * The code in this file is based on the article "Writing a TPM Device Driver"
 * published on http://ptgmedia.pearsoncmg.com.
 *
 */

#include <mem.h>
#include <tpm.h>
#include <tpmbuff.h>

#include "tpm_common.h"
#include "tpm1.h"

u8 tpm1_pcr_extend(struct tpm *t, struct tpm_digest *d)
{
	struct tpmbuff *b = t->buff;
	struct tpm_header *hdr;
	struct tpm_extend_cmd *cmd;
	struct tpm_extend_resp *resp;
	size_t bytes;

	if (! b->ops->reserve(b))
		goto out;

	hdr = (struct tpm_header *)b->head;

	hdr->tag = TPM_TAG_RQU_COMMAND;
	hdr->code = TPM_ORD_EXTEND;

	cmd = (struct tpm_extend_cmd *)
		b->ops->put(b, sizeof(struct tpm_extend_cmd));
	cmd->pcr_num = d->pcr;
	memcpy(&(cmd->digest), &(d->digest), sizeof(TPM_DIGEST));

	hdr->size = b->ops->size(b);

	if (hdr->size != t->ops->send(b))
		goto free;
	b->ops->free(b);

	/* Reset buffer for receive */
	if (! b->ops->reserve(b))
		goto out;

	hdr = (struct tpm_header *)b->head;
	resp = (struct tpm_extend_resp *)
		b->ops->put(b, sizeof(struct tpm_extend_resp));
	if (b->ops->size(b) != t->ops->recv(b))
		goto free;
	b->ops->free(b);

	if (resp->ordinal != TPM_SUCCESS)
		goto out;

	return 1;
free:
	b->ops->free(b);
out:
	return 0;
}
