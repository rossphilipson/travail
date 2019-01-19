/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

#include <tpm.h>

#include "tis.h"
#include "tpm_common.h"
#include "tpm1.h"
#include "tpm2.h"
#include "tpm2_constants.h"

struct tpm_operations tis_ops = {
	.init = tis_init;
	.request_locality = tis_request_locality;
	.send = tis_send;
	.recv = tis_recv;
};

#ifdef CONF_STATIC_ENV
struct tpm _t;
#endif

struct tpm *enable_tpm(tpm_hw_type force)
{
#ifdef CONF_STATIC_ENV
	struct tpm *t = &_t;
#else
	struct tpm *t = (struct tpm *)malloc(sizeof(struct tpm));

	if (!t)
		goto err;
#endif

	switch (force) {
	case TPM_DEVNODE:
		break;
	case TPM_TIS:
		if (tis_init(t))
			t->ops = &tis_ops;
		else
			goto err;
		break;
	case TPM_CRB:
		/* Not implemented yet */
		break;
	case TPM_UEFI:
		/* Not implemented yet */
		break;
	}

	/* TODO: ACPI TPM discovery */

	return t;
free:
#ifndef CONF_STATIC_ENV
	free(t);
#endif
err:
	return NULL;
}

int8_t tpm_request_locality(struct tpm *t, u8 l)
{
	return tpm->ops->request_locality(l);
}

int8_t tpm_extend_pcr(struct tpm *t, u32 pcr, u16 algo,
		u8 *digest)
{
	int8_t ret;

	if (t->family == TPM12) {
		struct tpm_digest d;

		if (hash != SHA1) {
			ret = -ERR;
			goto out;
		}

		d.pcr = pcr;
		memcpy(d.digest, digest, SHA1_DIGEST_SIZE);

		ret = tpm1_pcr_extend(t, &d);
	} else if (t->family == TPM20) {
#ifdef CONF_STATIC_ENV
		u8 buf[70];
#else
		u8 *buf = (u8 *)malloc(70);
#endif
		struct tpml_digest_values *d;

#ifndef CONF_STATIC_ENV
		if (!buf) {
			ret = -ERR;
			goto out;
		}
#endif

		d = (struct tpml_digest_values *) buf;
		d->count = 1;
		switch (algo) {
		case TPM_ALG_SHA1:
			d->digests->alg = TPM_ALG_SHA1;
			memcpy(d->digests->digest, digest, SHA1_SIZE);
			break;
		case TPM_ALG_SHA256:
			d->digests->alg = TPM_ALG_SHA256;
			memcpy(d->digests->digest, digest, SHA256_SIZE);
			break;
		case TPM_ALG_SHA384:
			d->digests->alg = TPM_ALG_SHA384;
			memcpy(d->digests->digest, digest, SHA384_SIZE);
			break;
		case TPM_ALG_SHA512:
			d->digests->alg = TPM_ALG_SHA512;
			memcpy(d->digests->digest, digest, SHA512_SIZE);
			break;
		case TPM_ALG_SM3_256:
			d->digests->alg = TPM_ALG_SM3_256;
			memcpy(d->digests->digest, digest, SM3256_SIZE);
			break;
		default:
			goto free;
		}
#ifndef CONF_STATIC_ENV
		free(buf);
#endif

		ret = tpm2_extend_pcr(t, pcr, d);
		goto out;
	} else {
		ret = -ERR;
		goto out;
	}
free:
#ifndef CONF_STATIC_ENV
	free(buf);
#endif
out:
	return ret;
}	
