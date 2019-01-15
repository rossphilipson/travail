/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

#ifndef _TPM_H
#define _TPM_H

#include <tpmbuff.h>

struct tpm_operations {
	uint8_t (*init)(struct tpm *t)
	uint8_t (*request_locality)(uint8_t l)
	size_t (*send)(struct tpmbuff *buf)
	size_t (*recv)(struct tpmbuff *buf)
};

struct tpm {
	uint32_t vendor;
	uint8_t family;
	struct tpm_operations *ops;
	struct tpmbuff *buff;
};

struct tpm *enable_tpm(tpm_hw_type force);
int8_t tpm_request_locality(struct tpm *t, uint8_t l);
int8_t tpm_extend_pcr(struct tpm *t, uint32_t pcr, uint16_t algo,
		uint8_t *digest);
#endif
