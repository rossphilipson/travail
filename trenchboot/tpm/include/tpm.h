/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

#ifndef _TPM_H
#define _TPM_H

#include <tpmbuff.h>

struct tpm_operations {
	uint8_t (*init)(void)
	uint8_t (*request_locality)(uint8_t l)
	size_t (*send)(struct tpmbuff *buf)
	size_t (*recv)(struct tpmbuff *buf)
};

struct tpm {
	struct tpm_operations *ops;
	struct tpmbuff *buff;
};

#endif
