/*
 * Copyright (c) 2018 Daniel P. Smith, Apertus Solutions, LLC
 *
 * The code in this file is based on the article "Writing a TPM Device Driver"
 * published on http://ptgmedia.pearsoncmg.com.
 *
 */

#include <boot.h>
#include <tpm.h>

#include "tpm_common.h"

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

	iowrite8(val, mmio_addr);
}

static uint32_t read32(uint32_t field)
{
	void *mmio_addr = (void*)(uint64_t)(MMIO_BASE | field);

	return ioread32(mmio_addr);
}

static void write32(unsigned int val, uint32_t field)
{
	void *mmio_addr = (void*)(uint64_t)(MMIO_BASE | field);

	iowrite32(val, mmio_addr);
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

uint8_t tis_init(struct tpm *t)
{
	uint8_t i;

	for (i=0; i<=MAX_LOCALITY; i++)
		write8(ACCESS_RELINQUISH_LOCALITY, ACCESS(i));

	if (tis_request_locality(0) == NO_LOCALITY)
		return 0;

	t->vendor = read32(DID_VID(0));
	if ((t->vendor & 0xFFFF) == 0xFFFF)
		return 0;

	return 1;
}

size_t tis_send(struct tpmbuff *buf)
{
	uint8_t status, *buf_ptr;
	uint32_t burstcnt = 0;
	uint32_t count = 0;

	if (locality > MAX_LOCALITY)
		return 0;

	write8(STS_COMMAND_READY, STS(locality));

	buf_ptr = buf->head;

	/* send all but the last byte */
	while (count < (buf->len - 1)) {
		burstcnt = burst_wait();
		for (; burstcnt > 0 && count < (buf->len - 1); burstcnt--) {
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
	write8(buf_ptr[buf->len - 1], DATA_FIFO(locality));

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

size_t tis_recv(struct tpmbuff *buf)
{
	uint32_t expected;
	uint8_t status, *buf_ptr;
	struct tpm_header *hdr;

	/* ensure that there is data available */
	status = read8(STS(locality));
	if ((status & (STS_DATA_AVAIL | STS_VALID))
			!= (STS_DATA_AVAIL | STS_VALID))
		goto err;

	/* read header */
	hdr = (struct tpm_header *)buf->head;
	expected = sizeof(struct tpm_header);
	if (recv_data(buf->head, expected) < expected)
		goto err;
	
	/* convert header */
	hdr->tag = be16_to_cpu(hdr->tag);
	hdr->size = be32_to_cpu(hdr->size);
	hdr->code = be32_to_cpu(hdr->code);

	/* hdr->size = header + data */
	expected = hdr->size - expected;
	buf_ptr = buf->put(buf, expected);
	if (! buf_ptr)
		goto err;

	/* read all data, except last byte */
	if (recv_data(buf_ptr, expected - 1) < (expected - 1))
		goto err;

	/* check for receive underflow */
	status = read8(STS(locality));
	if ((status & (STS_DATA_AVAIL | STS_VALID))
			!= (STS_DATA_AVAIL | STS_VALID))
		goto err;

	/* read last byte */
	buf_ptr = buf->put(buf, 1);
	if (recv_data(buf_ptr, 1) != 1)
		goto err;

	/* make sure we read everything */
	status = read8(STS(locality));
	if ((status & (STS_DATA_AVAIL | STS_VALID))
			== (STS_DATA_AVAIL | STS_VALID)) {
		goto err;
	}

	write8(STS_COMMAND_READY, STS(locality));

	return hdr->size;
err:
	return 0;
}
