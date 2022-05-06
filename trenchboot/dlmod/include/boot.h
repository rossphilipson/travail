/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Header file for the real-mode kernel code
 */

/*
 * Just a snippet from kernel of what we need
 */

#include <defs.h>
#include <types.h>

#ifndef __BOOT_H__
#define __BOOT_H__

typedef struct __packed dlmod_info {
	u8  uuid[16]; /* 78 f1 26 8e 04 92 11 e9  83 2a c8 5b 76 c4 cc 02 */
	u32 version;
	u16 msb_key_algo;
	u8  msb_key_hash[64]; /* Support up to SHA512 */
} dlmod_info_t;
extern dlmod_info_t dlmod_info;

static inline u8 inb(u16 port)
{
	u8 val;

	asm volatile("inb %1,%0" : "=a" (val) : "dN" (port));
	return val;
}

static inline void outb(u8 val, u16 port)
{
	asm volatile("outb %0,%1" : : "a" (val), "dN" (port));
}

#endif /* __BOOT_H__ */
