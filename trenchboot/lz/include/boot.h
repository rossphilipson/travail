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

typedef struct __packed sl_header {
    u16 lz_offet;
    u16 lz_length;
} sl_header_t;

typedef struct __packed lz_header {
    u32 trenchboot_loader_size;
    u32 zero_page_addr;
    u8  msb_key_hash[20];
} lz_header_t;

/* Basic port I/O */
static inline void outb(u8 v, u16 port)
{
	asm volatile("outb %0,%1" : : "a" (v), "dN" (port));
}

static inline u8 inb(u16 port)
{
	u8 v;
	asm volatile("inb %1,%0" : "=a" (v) : "dN" (port));
	return v;
}

static inline void outw(u16 v, u16 port)
{
	asm volatile("outw %0,%1" : : "a" (v), "dN" (port));
}

static inline u16 inw(u16 port)
{
	u16 v;
	asm volatile("inw %1,%0" : "=a" (v) : "dN" (port));
	return v;
}

static inline void outl(u32 v, u16 port)
{
	asm volatile("outl %0,%1" : : "a" (v), "dN" (port));
}

static inline u32 inl(u16 port)
{
	u32 v;
	asm volatile("inl %1,%0" : "=a" (v) : "dN" (port));
	return v;
}

static inline void io_delay(void)
{
	const u16 DELAY_PORT = 0x80;
	asm volatile("outb %%al,%0" : : "dN" (DELAY_PORT));
}

static inline void die(void)
{
	asm volatile("ud2");
}

/* Lib */
static inline void *memset(void *s, int c, u32 n)
{
    char *buf = (char*)s;

    for ( ; n--; )
        *buf++ = c;

    return buf;
}

/* Accessors */
lz_header_t *get_lz_header(void);
void *get_zero_page(void);
void *get_dev_table(void);

/* Assembly routines */
void load_stack(const void *new_stack);
void print_char(char c);
void stgi(void);
void lz_exit(const void *pm_enrty, const void *zp_base, const void *lz_base);

#endif /* __BOOT_H__ */
