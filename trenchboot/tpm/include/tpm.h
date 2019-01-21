/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

#ifndef _TPM_H
#define _TPM_H

#include <tpmbuff.h>

enum tpm_hw_intf {
	TPM_DEVNODE,
	TPM_TIS,
	TPM_CRB,
	TPM_UEFI
}

struct tpm_operations {
	u8 (*init)(struct tpm *t)
	u8 (*request_locality)(u8 l)
	size_t (*send)(struct tpmbuff *buf)
	size_t (*recv)(struct tpmbuff *buf)
};

struct tpm {
	u32 vendor;
	u8 family;
	struct tpm_operations *ops;
	struct tpmbuff *buff;
};

struct tpm *enable_tpm(enum tpm_hw_intf force);
int8_t tpm_request_locality(struct tpm *t, u8 l);
int8_t tpm_extend_pcr(struct tpm *t, u32 pcr, u16 algo,
		u8 *digest);
#endif
