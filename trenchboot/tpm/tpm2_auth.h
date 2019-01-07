/*
 * Copyright (c) 2018 Daniel P. Smith, Apertus Solutions, LLC
 *
 * The definitions in this header are extracted and/or dervied from the
 * Trusted Computing Group's TPM 2.0 Library Specification Parts 1&2.
 *
 */

#ifndef _TPM2_H
#define _TPM2_H

uint16_t tpm2_null_auth_size(void);
uint16_t tpm2_null_auth(uint8_t *b);

#endif
