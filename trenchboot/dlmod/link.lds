/*
 * link.lds
 *
 * Linker script for the i386 SKL setup code
 *
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
ENTRY(_entry)

SECTIONS
{
	. = 0;
	_start = .;
	.text : {
		*(.headers)
		*(.text*)
	}
	. = ALIGN(64);
	.rodata : {
		*(SORT_BY_ALIGNMENT(.rodata*))
	}
	.data : {
		*(SORT_BY_ALIGNMENT(.data*))
	}
	.bss : {
		*(SORT_BY_ALIGNMENT(.bss*))
	}

	/*
	 * Due to the 64k total size constraint, we link all page size/aligned
	 * data together in a single section, to avoid wasting space in the
	 * individual data/bss sections.
	 */
	.page_data : {
		*(.page_data)
	}

	.dlmod_info : {
		*(.dlmod_info)
	}

	. = ALIGN(8);

	/*
	 * Bootloader must pass non-constant data (e.g. address of zeropage). Keep
	 * it in separate section, outside of measured part of SL. This must be
	 * done in order to keep hashes constant, it also allows us to measure SL
	 * offline.
	 */
	.bootloader_data : {
		*(.bootloader_data)
	}

	/* This section is expected to be empty. */
	.got : {
		*(.got)
	}

	_end = .;

	/DISCARD/ : {
		*(.eh_frame*)
	}
}

ASSERT(SIZEOF(.got) == 0, ".got section not empty - non-hidden symbols used?");
