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

#include <types.h>
#include <be_byteshift.h>
#include <mem.h>
#include <tpm.h>
#include <tis.h>

/* Internal error of TPM command library */
#define TPM_LIB_ERROR	((u32)~0u)
/* Useful constants */
enum {
	COMMAND_BUFFER_SIZE		= 256,
	TPM_PUBEK_SIZE			= 256,
	TPM_REQUEST_HEADER_LENGTH	= 10,
	TPM_RESPONSE_HEADER_LENGTH	= 10,
	PCR_DIGEST_LENGTH		= 20,
};
/**
 * Get TPM command size.
 *
 * @param command	byte string of TPM command
 * @return command size of the TPM command
 */
static u32 tpm_command_size(const void *command)
{
	const size_t command_size_offset = 2;
	return get_unaligned_be32(command + command_size_offset);
}
/**
 * Get TPM response return code, which is one of TPM_RESULT values.
 *
 * @param response	byte string of TPM response
 * @return return code of the TPM response
 */
static u32 tpm_return_code(const void *response)
{
	const size_t return_code_offset = 6;
	return get_unaligned_be32(response + return_code_offset);
}
/**
 * Send a TPM command and return response's return code, and optionally
 * return response to caller.
 *
 * @param command	byte string of TPM command
 * @param response	output buffer for TPM response, or NULL if the
 *			caller does not care about it
 * @param size_ptr	output buffer size (input parameter) and TPM
 *			response length (output parameter); this parameter
 *			is a bidirectional
 * @return return code of the TPM response
 */
static u32 tpm_sendrecv_command(const void *command,
		void *response, size_t *size_ptr)
{
	u8 response_buffer[COMMAND_BUFFER_SIZE];
	size_t response_length;
	u32 err;
	if (response) {
		response_length = *size_ptr;
	} else {
		response = response_buffer;
		response_length = sizeof(response_buffer);
	}
	err = tis_sendrecv(command, tpm_command_size(command),
			response, &response_length);
	if (err)
		return TPM_LIB_ERROR;
	if (response)
		*size_ptr = response_length;
	return tpm_return_code(response);
}
u32 tpm_extend(u32 index, const void *in_digest, void *out_digest)
{
	static const u8 command[34] = {
		0x0, 0xc1, 0x0, 0x0, 0x0, 0x22, 0x0, 0x0, 0x0, 0x14,
	};
	const size_t index_offset = 10;
	const size_t in_digest_offset = 14;
	const size_t out_digest_offset = 10;
	u8 buf[COMMAND_BUFFER_SIZE];
	u8 response[TPM_RESPONSE_HEADER_LENGTH + PCR_DIGEST_LENGTH];
	size_t response_length = sizeof(response);
	u32 err;
	/* replacement follows
	if (pack_byte_string(buf, sizeof(buf), "sds",
				0, command, sizeof(command),
				index_offset, index,
				in_digest_offset, in_digest,
				PCR_DIGEST_LENGTH))
		return TPM_LIB_ERROR;
	*/
	memcpy(buf, command, sizeof(command));
	put_unaligned_be32(index, buf + index_offset);
	memcpy(buf + in_digest_offset, in_digest, PCR_DIGEST_LENGTH);

	err = tpm_sendrecv_command(buf, response, &response_length);
	if (err)
		return err;
	/* replacement follows
	if (unpack_byte_string(response, response_length, "s",
				out_digest_offset, out_digest,
				PCR_DIGEST_LENGTH))
		return TPM_LIB_ERROR;
	*/
	memcpy(out_digest, response + out_digest_offset, PCR_DIGEST_LENGTH);
	return 0;
}
