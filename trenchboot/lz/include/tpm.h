/*
 * Copyright (c) 2013 The Chromium OS Authors.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#ifndef __TPM_H
#define __TPM_H
/*
#include <tis.h>
*/
#include <types.h>
/*
 * Here is a partial implementation of TPM commands.  Please consult TCG Main
 * Specification for definitions of TPM commands.
 */
enum tpm_startup_type {
	TPM_ST_CLEAR		= 0x0001,
	TPM_ST_STATE		= 0x0002,
	TPM_ST_DEACTIVATED	= 0x0003,
};
enum tpm_physical_presence {
	TPM_PHYSICAL_PRESENCE_HW_DISABLE	= 0x0200,
	TPM_PHYSICAL_PRESENCE_CMD_DISABLE	= 0x0100,
	TPM_PHYSICAL_PRESENCE_LIFETIME_LOCK	= 0x0080,
	TPM_PHYSICAL_PRESENCE_HW_ENABLE		= 0x0040,
	TPM_PHYSICAL_PRESENCE_CMD_ENABLE	= 0x0020,
	TPM_PHYSICAL_PRESENCE_NOTPRESENT	= 0x0010,
	TPM_PHYSICAL_PRESENCE_PRESENT		= 0x0008,
	TPM_PHYSICAL_PRESENCE_LOCK		= 0x0004,
};
enum tpm_nv_index {
	TPM_NV_INDEX_LOCK	= 0xffffffff,
	TPM_NV_INDEX_0		= 0x00000000,
	TPM_NV_INDEX_DIR	= 0x10000001,
};
/**
 * Initialize TPM device.  It must be called before any TPM commands.
 *
 * @return 0 on success, non-0 on error.
 */
u32 tpm_init(void);
/**
 * Issue a TPM_Startup command.
 *
 * @param mode		TPM startup mode
 * @return return code of the operation
 */
u32 tpm_startup(enum tpm_startup_type mode);
/**
 * Issue a TPM_Extend command.
 *
 * @param index		index of the PCR
 * @param in_digest	160-bit value representing the event to be
 *			recorded
 * @param out_digest	160-bit PCR value after execution of the
 *			command
 * @return return code of the operation
 */
u32 tpm_extend(u32 index, const void *in_digest, void *out_digest);
#endif /* __TPM_H */
