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
static struct tpm tpm;
#endif

void tpm_io_delay(void)
{
	/* This is the default delay type in native_io_delay */
	asm volatile ("outb %al, $0x80");
}

u8 tpm_read8(u32 field)
{
	void *mmio_addr = (void*)(u64)(TPM_MMIO_BASE | field);

	return ioread8(mmio_addr);
}

void tpm_write8(unsigned char val, u32 field)
{
	void *mmio_addr = (void*)(u64)(TPM_MMIO_BASE | field);

	iowrite8(val, mmio_addr);
}

u32 tpm_read32(u32 field)
{
	void *mmio_addr = (void*)(u64)(TPM_MMIO_BASE | field);

	return ioread32(mmio_addr);
}

void tpm_write32(unsigned int val, u32 field)
{
	void *mmio_addr = (void*)(u64)(TPM_MMIO_BASE | field);

	iowrite32(val, mmio_addr);
}

static void find_interface_and_family(tpm *t)
{
	struct tpm_interface_id intf_id;
	struct tpm_intf_capability intf_cap;

	/* First see if the interface is CRB, then we know it is TPM20 */
	intf_id.val = tpm_read32(TPM_INTERFACE_ID_0);
	if (intf.interface_type == TPM_CRB_INTF_ACTIVE) {
		t->intf = TPM_CRB;
		t->family = TPM20;
		return;
	}

	/* If not a CRB then a TIS/FIFO interface */
	t->intf = TPM_TIS;

	/* Now to sort out whether it is 1.2 or 2.0 using TIS */
	intf_cap.val = tpm_read32(TPM_INTF_CAPABILITY_0);
	if ( (intf_cap.interface_version == TPM12_TIS_INTF_12) ||
	     (intf_cap.interface_version == TPM12_TIS_INTF_13) )
		t->family = TPM12;
	else
		t->family = TPM20;
}

struct tpm *enable_tpm(void)
{
#ifdef CONF_STATIC_ENV
	struct tpm *t = &tpm;
#else
	struct tpm *t = (struct tpm *)malloc(sizeof(struct tpm));

	if (!t)
		goto err;
#endif
	find_interface_and_family(t);

	switch (t->intf) {
	case TPM_DEVNODE:
		/* Not implemented yet */
		break;
	case TPM_TIS:
		if (tis_init(t))
			t->ops = &tis_ops;
		else
			goto free;
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
	return t->ops->request_locality(l);
}

#define MAX_TPM_EXTEND_SIZE 70 /* TPM2 SHA512 is the largest */
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
		struct tpml_digest_values *d;
#ifdef CONF_STATIC_ENV
		u8 buf[MAX_TPM_EXTEND_SIZE];
#else
		u8 *buf = (u8 *)malloc(MAX_TPM_EXTEND_SIZE);

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

		ret = tpm2_extend_pcr(t, pcr, d);
		goto free;
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
