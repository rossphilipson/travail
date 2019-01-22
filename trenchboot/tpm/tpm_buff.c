/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

#include <types.h>
#include <tpmbuff.h>

#include "tpm_common.h"

#define TPM_CRB_DATA_BUFFER_OFFSET	0x80
#define TPM_CRB_DATA_BUFFER_SIZE	3966

static u8 *tpmb_reserve(struct tpmbuff *b)
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

static u8 *tpmb_put(struct tpmbuff *b, size_t size)
{
	u8 *tail = b->tail;

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
statuc u8 tis_buff[STATIC_TIS_BUFFER_SIZE];

static struct tpmbuff tpm_buff = {
	.ops = &ops;
};
#endif

struct tpmbuff *alloc_tpmbuff(enum tpm_hw_intf intf, uin8_t locality)
{
#ifdef CONF_STATIC_ENV
	struct tpmbuff *b = &tpm_buff;
#else
	struct tpmbuff *b = (struct tpmbuff *)malloc(sizeof(struct tpmbuff));

	if (!b)
		goto err;

	b->ops = &ops;
#endif

	switch (intf) {
	case TPM_DEVNODE:
		break;
	case TPM_TIS:
		if (b->head)
			goto reset;

#ifdef CONF_STATIC_ENV
		b->head = &tis_buff;
		b->truesize = STATIC_TIS_BUFFER_SIZE;
#else
		b->head = (u8 *)malloc(PAGE_SIZE);
		if (!b->head)
			goto free_buff;
		b->truesize = PAGE_SIZE;
#endif
		break;
	case TPM_CRB:
		b->buf = TPM_LPC_BASE + (locality << 12) \
			       + TPM_CRB_DATA_BUFFER_OFFSET;
		b->truesize = TPM_CRB_DATA_BUFFER_SIZE;
		break;
	case TPM_UEFI:
		/* Not implemented yet */
		break;
	default:
		goto err;
	}

reset:
	b->len = 0;
	b->locked = 0;
	b->data = NULL;
	b->tail = NULL;
	b->end = b->head + (b->truesize - 1);

	return b;

#ifndef CONF_STATIC_ENV
free_buff:
	free(b);
#endif
err:
	return NULL;
}

void free_tpmbuff(struct tpmbuff *b, enum tpm_hw_intf i)
{

	switch (i) {
	case TPM_TIS:
#ifdef CONF_STATIC_ENV
		b->head = NULL;
#else
		free(b->head);
#endif
		break;
	case TPM_CRB:
		/* No Op */
		break;
	case TPM_UEFI:
		/* Not implemented yet */
		break;
	default:
		break;
	}

#ifndef CONF_STATIC_ENV
	free(b);
#endif
}
