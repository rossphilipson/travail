/*
 * Copyright (c) 2018 Daniel P. Smith, Apertus Solutions, LLC
 *
 * The definitions in this header are extracted from the Trusted Computing
 * Group's "TPM Main Specification", Parts 1-3.
 *
 */

#ifndef _TIS_H
#define _TIS_H

#include <types.h>
#include <tpm.h>

/* TPM Interface Specification functions */
uint8_t tis_request_locality(uint8_t l);
uint8_t tis_init(struct tpm *t);
size_t tis_send(struct tpm_cmd_buf *buf);
size_t tis_recv(struct tpm_resp_buf *buf);

#endif
