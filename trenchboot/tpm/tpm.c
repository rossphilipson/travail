/*
 * Copyright (c) 2018 Daniel P. Smith, Apertus Solutions, LLC
 *
 * The definitions in this header are extracted from the Trusted Computing
 * Group's "TPM Main Specification", Parts 1-3.
 *
 */

#include "tpm.h"

uint8_t tpm_pcr_extend(struct tpm_digest *d)
{
	size_t bytes;
	struct tpm_cmd_buf send;
	struct tpm_resp_buf resp;

	send.tag = TPM_TAG_RQU_COMMAND;
	send.size = sizeof(tpm_extend_cmd) + 6;
	send.cmd.extend.ordinal = TPM_ORD_Extend;
	send.cmd.extend.pcr_num = d->pcr;
	send.cmd.extend.digest = d->digest.sha1.val;

	if (send.size != tis_send(&send))
		return 0;

	bytes = sizeof(tpm_extend_resp) + 10;
	if (bytes != tis_recv(&resp))
		return 0;

	if (resp.result != TPM_SUCCESS)
		return 0;

	return 1;
}
