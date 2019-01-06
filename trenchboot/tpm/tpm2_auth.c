

#include "tpm2_constants.h"

#define NULL_AUTH_SIZE 9
uint16_t tpm2_null_auth(uint8_t *b)
{
	uint32_t *handle = (uint32_t *)b;

	memset(b, 0, NULL_AUTH_SIZE);

	*handle = cpu_to_be32(TPM_RS_PW);

	return NULL_AUTH_SIZE;
}

