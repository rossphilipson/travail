
/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

#ifndef _TPM_H
#define _TPM_H

#define SHA1_SIZE	20
#define SHA256_SIZE	32
#define SHA384_SIZE	48
#define SHA512_SIZE	64
#define SM3256_SIZE	32

struct tpm_header {
	u16 tag;
	u32 size;
	u32 code;
};

#endif
