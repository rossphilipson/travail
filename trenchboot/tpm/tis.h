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

static inline bool tis_data_available(int locality) 
{ 
        int status; 

        status = tpm_read8(STS(locality)); 
        return ((status & (STS_DATA_AVAIL | STS_VALID)) == 
		(STS_DATA_AVAIL | STS_VALID)) 
} 

/* TPM Interface Specification functions */
i8 tis_request_locality(u8 l);
void tis_relinquish_locality(void);
u8 tis_init(struct tpm *t);
size_t tis_send(struct tpmbuff *buf);
size_t tis_recv(struct tpmbuff *buf);

#endif
