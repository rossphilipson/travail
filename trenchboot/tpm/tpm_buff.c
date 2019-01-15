/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

#include <types.h>
#include <tpmbuff.h>

#include "tpm_common.h"

#define TPM_CRB_DATA_BUFFER_OFFSET	0x80
#define TPM_CRB_DATA_BUFFER_SIZE	3966

static uint8_t *tpmb_reserve(struct tpmbuff *b)
{
	if (b->locked)
		return NULL;

	b->len = sizeof(struct tpm_header);
	b->locked = 1;
	b->data = b->head + b->len;
	b->tail = b->data;

	return b->head;
}

static void tpmb_free(struct tpmbuff *b)
{
	b->len = 0;
	b->locked = 0;
	b->data = NULL;
	b->tail = NULL;
}

static uint8_t *tpmb_put(struct tpmbuff *b, size_t size)
{
	uint8_t *tail = b->tail;

	if ((b->len + size) > b->truesize)
		return NULL; /* TODO: add overflow buffer support */

	b->tail += size;
	b->len += size;

	return tail;
}

static size_t tpmb_trim(struct tpmbuff *b, size_t size)
{
	if ((b->len - size) < 0)
		size = b->len;
	
	/* TODO: add overflow buffer support */

	b->tail -= size;
	b->len -= size;

	return size;
}

static size_t tpmb_size(struct tpmbuff *b)
{
	return b->len;
}

static struct tpmbuff_operations ops = {
	.reserve = tpmb_reserve;
	.free = tpmb_free;
	.put = tpmb_put;
	.trim = tpmb_trim;
	.size = tpmb_size;
};

#ifdef CONF_STATIC_ENV
static struct tpmbuff _b = {
	.ops = &ops;
};
#endif

struct tpmbuff *alloc_tpmbuff(tpm_hw_type intf, uin8_t locality)
{
#ifdef CONF_STATIC_ENV
	struct tpmbuff *b = &_b;
#else
	struct tpmbuff *b = (struct tpmbuff *)malloc(sizeof(struct tpmbuff));

	if (!b)
		return NULL;

	b->ops = &ops;
#endif

	switch (intf) {
	case TPM_DEVNODE:
		break;;
	case TPM_TIS:
		if (b->head)
			goto reset;

		b->head = (uint8_t *)malloc(PAGE_SIZE);
		b->truesize = PAGE_SIZE;
		break;
	case TPM_CRB:
		b->buf = TPM_LPC_BASE + (locality << 12) \
			       + TPM_CRB_DATA_BUFFER_OFFSET;
		b->truesize = TPM_CRB_DATA_BUFFER_SIZE;
		break;
	case TPM_UEFI:
		/* Not implemented yet */
		break;;
	default:
		return NULL;
	}

reset:
	b->len = 0;
	b->locked = 0;
	b->data = NULL;
	b->tail = NULL;
	b->end = b->head + (b->truesize - 1);

	return b;
}

void free_tpmbuff(struct tpmbuff *b, tpm_hw_intf i)
{

	switch (i) {
	case TPM_TIS:
		free(b->head);
		break;
	case TPM_CRB:
		/* No Op */
		break;
	case TPM_UEFI:
		/* Not implemented yet */
		break;;
	default:
		break;
	}

	free(b);
}
