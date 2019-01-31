/*
 * Copyright (c) 2019 Daniel P. Smith, Apertus Solutions, LLC
 *
 */

void io_delay(void)
{
	/* This is the default delay type in native_io_delay */
	asm volatile ("outb %al, $0x80");
}

void udelay(int loops)
{
	while (loops--)
		io_delay();	/* Approximately 1 us */
}

u8 read8(u32 field)
{
	void *addr = (void*)(u64)(TPM_MMIO_BASE | field);
	u8 val;

	barrier();
	val = (*(volatile u8*)(addr));
	rmb();

	return val;
}

void write8(unsigned char val, u32 field)
{
	void *addr = (void*)(u64)(TPM_MMIO_BASE | field);

	barrier();
	(*(volatile u8 *)(addr)) = val;
	wmb();
}

u32 read32(u32 field)
{
	void *addr = (void*)(u64)(TPM_MMIO_BASE | field);
	u32 val;

	barrier();
	val = (*(volatile u32*)(addr));
	rmb();

	return val;
}

void write32(u32 val, u32 field)
{
	void *addr = (void*)(u64)(TPM_MMIO_BASE | field);

	barrier();
	(*(volatile u32 *)(addr)) = val;
	wmb();
}
