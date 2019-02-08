/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

#include <types.h>
#include <tpm.h>
#include <tpmbuff.h>

#include "tpm_common.h"
#include "tpm1.h"
#include "tis.h"

#define TPM_CRB_DATA_BUFFER_OFFSET	0x80
#define TPM_CRB_DATA_BUFFER_SIZE	3966

u8 *tpmb_reserve(struct tpmbuff *b)
{
	if (b->locked)
		return NULL;

	b->len = sizeof(struct tpm_header);
	b->locked = 1;
	b->data = b->head + b->len;
	b->tail = b->data;

	return b->head;
}

void tpmb_free(struct tpmbuff *b)
{
	b->len = 0;
	b->locked = 0;
	b->data = NULL;
	b->tail = NULL;
}

u8 *tpmb_put(struct tpmbuff *b, size_t size)
{
	u8 *tail = b->tail;

	if ((b->len + size) > b->truesize)
		return NULL; /* TODO: add overflow buffer support */

	b->tail += size;
	b->len += size;

	return tail;
}

size_t tpmb_trim(struct tpmbuff *b, size_t size)
{
	if (b->len < size)
		size = b->len;

	/* TODO: add overflow buffer support */

	b->tail -= size;
	b->len -= size;

	return size;
}

size_t tpmb_size(struct tpmbuff *b)
{
	return b->len;
}

#ifdef CONF_STATIC_ENV
static u8 tis_buff[STATIC_TIS_BUFFER_SIZE];
static struct tpmbuff tpm_buff;
#endif

struct tpmbuff *alloc_tpmbuff(enum tpm_hw_intf intf, u8 locality)
{
#ifdef CONF_STATIC_ENV
	struct tpmbuff *b = &tpm_buff;
#else
	struct tpmbuff *b = (struct tpmbuff *)malloc(sizeof(struct tpmbuff));

	if (!b)
		goto err;
#endif

	switch (intf) {
	case TPM_DEVNODE:
		/* TODO: need implementation */
		goto err;
		break;
	case TPM_TIS:
		if (b->head)
			goto reset;

#ifdef CONF_STATIC_ENV
		b->head = (u8 *)&tis_buff;
		b->truesize = STATIC_TIS_BUFFER_SIZE;
#else
		b->head = (u8 *)malloc(PAGE_SIZE);
		if (!b->head)
			goto free_buff;
		b->truesize = PAGE_SIZE;
#endif
		break;
	case TPM_CRB:
		b->head = (u8 *)(u64)(TPM_MMIO_BASE + (locality << 12) \
			       + TPM_CRB_DATA_BUFFER_OFFSET);
		b->truesize = TPM_CRB_DATA_BUFFER_SIZE;
		break;
	case TPM_UEFI:
		/* Not implemented yet */
		goto err;
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

void free_tpmbuff(struct tpmbuff *b, enum tpm_hw_intf intf)
{

	switch (intf) {
	case TPM_DEVNODE:
		/* Not implemented yet */
		break;
	case TPM_TIS:
#ifdef CONF_STATIC_ENV
		b->head = NULL;
#else
		free(b->head);
#endif
		break;
	case TPM_CRB:
		b->head = NULL;
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
