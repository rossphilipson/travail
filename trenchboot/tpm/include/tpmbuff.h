/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

#ifndef _TPMBUFF_H
#define _TPMBUFF_H

struct tpmbuff_operations {
	u8 *(*reserve)(void);
	void (*free)(void);
	u8 *(*put)(size_t size);
	size_t (*trim)(size_t size);
	size_t (*size)(void);
};

/* mirroring Linux SKB */
struct tpmbuff {
	size_t truesize;
	size_t len;

	u8 locked;

	u8 *head;
	u8 *data;
	u8 *tail;
	u8 *end;

	struct tpmbuff_operations *ops;
};

struct tpmbuff *alloc_tpmbuff(enum tpm_hw_intf i, uin8_t locality);
void free_tpmbuff(struct tpmbuff *b, enum tpm_hw_intf i);

#endif
