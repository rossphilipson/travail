/*
 * Copyright (c) 2018 Daniel P. Smith, Apertus Solutions, LLC
 *
 * The code in this file is based on the article "Writing a TPM Device Driver"
 * published on http://ptgmedia.pearsoncmg.com.
 *
 */

#include <mem.h>
#include <tpm.h>

uint8_t tpm_pcr_extend(struct tpm_digest *d)
{
	size_t bytes;
	struct tpm_cmd_buf send;
	struct tpm_resp_buf resp;

	send.tag = TPM_TAG_RQU_COMMAND;
	send.size = sizeof(struct tpm_extend_cmd) + 6;
	send.cmd.extend.ordinal = TPM_ORD_EXTEND;
	send.cmd.extend.pcr_num = d->pcr;
	memcpy(&(send.cmd.extend.digest), &(d->digest), sizeof(TPM_DIGEST));

	if (send.size != tis_send(&send))
		return 0;

	bytes = sizeof(struct tpm_extend_resp) + 10;
	if (bytes != tis_recv(&resp))
		return 0;

	if (resp.result != TPM_SUCCESS)
		return 0;

	return 1;
}
