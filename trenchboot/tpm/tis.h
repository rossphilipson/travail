/*
 * Copyright (c) 2019 Apertus Solutions, LLC
 *
 * Author(s):
 *      Daniel P. Smith <dpsmith@apertussolutions.com>
 *
 * The definitions in this header are extracted from the Trusted Computing
 * Group's "TPM Main Specification", Parts 1-3.
 *
 */

#ifndef _TIS_H
#define _TIS_H

#include <types.h>
#include <tpm.h>

#define STATIC_TIS_BUFFER_SIZE 1024
/* TPM Interface Specification functions */
u8 tis_request_locality(u8 l);
void tis_relinquish_locality(void);
u8 tis_init(struct tpm *t);
size_t tis_send(struct tpmbuff *buf);
size_t tis_recv(struct tpmbuff *buf);

#endif
