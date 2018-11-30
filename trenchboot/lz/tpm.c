/*
 * Copyright (c) 2018 Daniel P. Smith, Apertus Solutions, LLC
 *
 * The code in this file is based on the article "Writing a TPM Device Driver"
 * published on http://ptgmedia.pearsoncmg.com.
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

#include <mem.h>
#include <tpm.h>

uint8_t tpm_pcr_extend(struct tpm_digest *d)
{
	size_t bytes;
	struct tpm_cmd_buf send;
	struct tpm_resp_buf resp;

	send.tag = TPM_TAG_RQU_COMMAND;
	send.size = sizeof(struct tpm_extend_cmd) + 6;
	send.cmd.extend.ordinal = TPM_ORD_EXTEND;
	send.cmd.extend.pcr_num = d->pcr;
	memcpy(&(send.cmd.extend.digest), &(d->digest), sizeof(TPM_DIGEST));

	if (send.size != tis_send(&send))
		return 0;

	bytes = sizeof(struct tpm_extend_resp) + 10;
	if (bytes != tis_recv(&resp))
		return 0;

	if (resp.result != TPM_SUCCESS)
		return 0;

	return 1;
}
