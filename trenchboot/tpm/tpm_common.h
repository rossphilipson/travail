
/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

#ifndef _TPM_H
#define _TPM_H

struct tpm_header {
	uint16_t tag;
	uint32_t size;
	uint32_t code;
};

#endif
