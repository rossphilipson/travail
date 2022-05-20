/*
 * Copyright (c) 2019 Oracle and/or its affiliates. All rights reserved.
 *
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

#include <defs.h>
#include <types.h>
#include <boot.h>

dlmod_info_t __section(".dlmod_info") __used dlmod_info = {
	.uuid = {
		0x78, 0xf1, 0x26, 0x8e, 0x04, 0x92, 0x11, 0xe9,
		0x83, 0x2a, 0xc8, 0x5b, 0x76, 0xc4, 0xcc, 0x02,
	},
	.version = 0,
	.msb_key_algo = 0x14,
	.msb_key_hash = { 0 },
};

static void print_char(char c)
{
	while ( !(inb(0x3f8 + 5) & 0x20) )
		;

	outb(c, 0x3f8);
}

static void print(const char * txt) {
	while (*txt != '\0') {
		if (*txt == '\n')
			print_char('\r');
		print_char(*txt++);
	}
}

static void print_u64(u64 p) {
	char tmp[sizeof(void*)*2 + 5] = "0x";
	int i;

	for (i=0; i<sizeof(void*); i++) {
		if ((p & 0xf) >= 10)
			tmp[sizeof(void*)*2 + 1 - 2*i] = (p & 0xf) + 'a' - 10;
		else
			tmp[sizeof(void*)*2 + 1 - 2*i] = (p & 0xf) + '0';
		p >>= 4;
		if ((p & 0xf) >= 10)
			tmp[sizeof(void*)*2 - 2*i] = (p & 0xf) + 'a' - 10;
		else
			tmp[sizeof(void*)*2 - 2*i] = (p & 0xf) + '0';
		p >>= 4;
	}
	tmp[sizeof(void*)*2 + 2] = ':';
	tmp[sizeof(void*)*2 + 3] = ' ';
	tmp[sizeof(void*)*2 + 4] = '\0';
	print(tmp);
}

void dlmod_main(u64 drtm_table, u64 dl_entry)
{
	print("DL Module running...\n");
	print("DRTM table pointer:\n");
	print_u64(drtm_table);
	print("\n");
	print("Jump to DL stub entry point:\n");
	print_u64(dl_entry);
	print("\n");
}
