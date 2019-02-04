/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

#ifndef _TPM_H
#define _TPM_H

#define TPM_NO_LOCALITY		0xFF

enum tpm_hw_intf {
	TPM_DEVNODE,
	TPM_TIS,
	TPM_CRB,
	TPM_UEFI
};

enum tpm_family {
	TPM12,
	TPM20
};

struct tpmbuff;

struct tpm {
	u32 vendor;
	enum tpm_family family;
	enum tpm_hw_intf intf;
	struct tpm_operations *ops;
	struct tpmbuff *buff;
};

struct tpm_operations {
	u8 (*init)(struct tpm *t);
	u8 (*request_locality)(u8 l);
	void (*relinquish_locality)(void);
	size_t (*send)(struct tpmbuff *buf);
	size_t (*recv)(struct tpmbuff *buf);
};

struct tpm *enable_tpm(void);
int8_t tpm_request_locality(struct tpm *t, u8 l);
void tpm_relinquish_locality(struct tpm *t);
int8_t tpm_extend_pcr(struct tpm *t, u32 pcr, u16 algo,
		u8 *digest);
void free_tpm(struct tpm *t);
#endif
