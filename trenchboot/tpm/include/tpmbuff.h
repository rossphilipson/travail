/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

#ifndef _TPMBUFF_H
#define _TPMBUFF_H

struct tpmbuff_operations {
	uint8_t *(*reserve)(void);
	void (*free)(void);
	uint8_t *(*put)(size_t size);
	size_t (*trim)(size_t size);
	size_t (*size)(void);
};

/* mirroring Linux SKB */
struct tpmbuff {
	size_t truesize;
	size_t len;

	uint8_t locked;

	uint8_t *head;
	uint8_t *data;
	uint8_t *tail;
	uint8_t *end;

	struct tpmbuff_operations *ops;
};

struct tpmbuff *alloc_tpmbuff(tpm_hw_intf i, uin8_t locality);
void free_tpmbuff(struct tpmbuff *b, tpm_hw_intf i);

#endif
