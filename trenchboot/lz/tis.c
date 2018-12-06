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

#include <boot.h>
#include <tpm.h>

#define MMIO_BASE			0xFED40000
#define MAX_LOCALITY			4
/* macros to access registers at locality ’’l’’ */
#define ACCESS(l)			(0x0000 | ((l) << 12))
#define STS(l)				(0x0018 | ((l) << 12))
#define DATA_FIFO(l)			(0x0024 | ((l) << 12))
#define DID_VID(l)			(0x0F00 | ((l) << 12))
/* access bits */
#define ACCESS_ACTIVE_LOCALITY		0x20 /* (R)*/
#define ACCESS_RELINQUISH_LOCALITY	0x20 /* (W) */
#define ACCESS_REQUEST_USE		0x02 /* (W) */
/* status bits */
#define STS_VALID			0x80 /* (R) */
#define STS_COMMAND_READY		0x40 /* (R) */
#define STS_DATA_AVAIL			0x10 /* (R) */
#define STS_DATA_EXPECT			0x08 /* (R) */
#define STS_GO				0x20 /* (W) */

#define NO_LOCALITY			0xFF
static uint8_t locality = NO_LOCALITY;

static uint8_t read8(uint32_t field)
{
	void *mmio_addr = (void*)(uint64_t)(MMIO_BASE | field);

	return ioread8(mmio_addr);
}

static void write8(unsigned char val, uint32_t field)
{
	void *mmio_addr = (void*)(uint64_t)(MMIO_BASE | field);

	iowrite8(mmio_addr, val);
}

static uint32_t read32(uint32_t field)
{
	void *mmio_addr = (void*)(uint64_t)(MMIO_BASE | field);

	return ioread32(mmio_addr);
}

static void write32(unsigned int val, uint32_t field)
{
	void *mmio_addr = (void*)(uint64_t)(MMIO_BASE | field);

	iowrite32(mmio_addr, val);
}

static uint32_t burst_wait(void)
{
	uint32_t count = 0;

	while (count == 0) {
		count = read8(STS(locality) + 1);
		count += read8(STS(locality) + 2) << 8;

		if (count == 0)
			io_delay(); /* wait for FIFO to drain */
	}

	return count;
}

uint8_t tis_request_locality(uint8_t l)
{
	write8(ACCESS_RELINQUISH_LOCALITY, ACCESS(locality));
	write8(ACCESS_REQUEST_USE, ACCESS(l));

	/* wait for locality to be granted */
	if (read8(ACCESS(l) & ACCESS_ACTIVE_LOCALITY)) {
		if (l <= MAX_LOCALITY)
			locality = l;
		else
			locality = NO_LOCALITY;
	}

	return locality;
}

uint8_t tis_init(void)
{
	uint32_t vendor;
	uint8_t i;

	for (i=0; i<=MAX_LOCALITY; i++)
		write8(ACCESS_RELINQUISH_LOCALITY, ACCESS(i));

	if (tis_request_locality(0) == NO_LOCALITY)
		return 0;

	vendor = read32(DID_VID(0));
	if ((vendor & 0xFFFF) == 0xFFFF)
		return 0;

	return 1;
}

size_t tis_send(struct tpm_cmd_buf *buf)
{
	uint8_t status, *buf_ptr;
	uint32_t burstcnt = 0;
	uint32_t count = 0;

	if (locality > MAX_LOCALITY)
		return 0;

	write8(STS_COMMAND_READY, STS(locality));

	buf_ptr = (uint8_t *) buf;

	/* send all but the last byte */
	while (count < (buf->size - 1)) {
		burstcnt = burst_wait();
		for (; burstcnt > 0 && count < buf->size - 1; burstcnt--) {
			write8(buf_ptr[count], DATA_FIFO(locality));
			count++;
		}

		/* check for overflow */
		for (status = 0; (status & STS_VALID) == 0; )
			status = read8(STS(locality));

		if ((status & STS_DATA_EXPECT) == 0)
			return 0;
	}

	/* write last byte */
	write8(buf_ptr[count], DATA_FIFO(locality));

	/* make sure it stuck */
	for (status = 0; (status & STS_VALID) == 0; )
		status = read8(STS(locality));

	if ((status & STS_DATA_EXPECT) != 0)
		return 0;

	/* go and do it */
	write8(STS_GO, STS(locality));

	return (size_t)count;
}

static size_t recv_data(unsigned char *buf, size_t len)
{
	size_t size = 0;
	uint8_t status, *bufptr;
	uint32_t burstcnt = 0;

	bufptr = (uint8_t *)buf;

	status = read8(STS(locality));
	while ((status & (STS_DATA_AVAIL | STS_VALID))
			== (STS_DATA_AVAIL | STS_VALID)
			&& size < len) {
		burstcnt = burst_wait();
		for (; burstcnt > 0 && size < len; burstcnt--) {
			*bufptr = read8(DATA_FIFO(locality));
			bufptr++;
			size++;
		}

		status = read8(STS(locality));
	}

	return size;
}

size_t tis_recv(struct tpm_resp_buf *buf)
{
	uint32_t expected;
	uint8_t status, *buf_ptr;
	size_t size = 0;

	buf_ptr = (uint8_t *)buf;

	/* ensure that there is data available */
	status = read8(STS(locality));
	if ((status & (STS_DATA_AVAIL | STS_VALID))
			!= (STS_DATA_AVAIL | STS_VALID))
		goto err;

	/* read first 6 bytes, including tag and paramsize */
	if ((size = recv_data(buf_ptr, 6)) < 6)
		goto err;

	buf_ptr += 6;

	expected = be32_to_cpu(buf->size);
	if (expected > sizeof(struct tpm_resp_buf))
		goto err;

	/* read all data, except last byte */
	if ((size += recv_data(buf_ptr, expected - 7))
			< expected - 1)
		goto err;

	buf_ptr += expected - 7;

	/* check for receive underflow */
	status = read8(STS(locality));
	if ((status & (STS_DATA_AVAIL | STS_VALID))
			!= (STS_DATA_AVAIL | STS_VALID))
		goto err;

	/* read last byte */
	if ((size += recv_data(buf_ptr, 1)) != expected)
		goto err;

	/* make sure we read everything */
	status = read8(STS(locality));
	if ((status & (STS_DATA_AVAIL | STS_VALID))
			== (STS_DATA_AVAIL | STS_VALID)) {
		goto err;
	}

	write8(STS_COMMAND_READY, STS(locality));

	return size;
err:
	return 0;
}
