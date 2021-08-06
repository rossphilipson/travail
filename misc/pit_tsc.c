/*
 * Calibrate CPU hz with PIT
 * https://wiki.osdev.org/Programmable_Interval_Timer
 *
 * Oscillator used by the PIT chip runs at ~ 1.193182 MHz
 */

static int calibrated = 0;
static u64 ticks_per_ms;

static inline u8 inb(u16 port)
{
	u8 data;

	asm volatile("inb %w1, %0" : "=a" (data) : "Nd" (port));

	return data;
}

static inline void outb(u16 port, u8 data)
{
	asm volatile("outb %0, %w1" : : "a" (data), "Nd" (port));
}

static inline void cpu_relax(void)
{
	asm volatile ("rep; nop" : : : "memory");
}

static inline u64 rdtsc(void)
{
	u32 low, high;

	asm volatile("rdtsc" : "=a" (low), "=d" (high));

	return ((u64)high << 32) | low;
}

/*
 * Channel 2, Mode 3 Square Wave:
 *
 * In one full period the output pin will be high for period/2 and low for
 * period/2. After a wait for high, then a low, a new period has started.
 *
 * PIT channel 2 is current programmed for a period ~= 1ms.
 */
static void pit_sync_period(void)
{
	/*
	 * Port 0x43 - PIT Mode/Command register (WO)
	 *
	 * 0xe8: Read Back Command (x2, see below)
	 * Get status (bit 4 clear) for channel:
	 */

	do {
		outb(0x43, 0xe8);
		cpu_relax();
		/*
		 * Port 0x42 - PIT Channel 2 (R)
		 *
		 * Read Back Status Byte (Port 0x43 written with bit 4 clear)
		 * 0x80: bit 7 - Output pin state
		 * Wait for output to go high:
		 */
	} while ( !(inb(0x42) & 0x80) );

	do {
		outb(0x43, 0xe8);
		cpu_relax();
		/*
		 * Port 0x42 - PIT Channel 2 (R)
		 *
		 * Read Back Status Byte (Port 0x43 written with bit 4 clear)
		 * 0x80: bit 7 - Output pin state
		 * Wait for output to go low:
		 */
	} while ( inb(0x42) & 0x80 );
}

void pit_calibrate(void)
{
	u8 val, latch;
	u64 start, end;

	/*
	 * Port 0x61 - KB controller port B control register (RW)
	 *
	 * Bit 0: PIT timer 2 gate to speaker enable
	 * Bit 1: Speaker enable
	 * Gate to speaker enable and disable speaker:
	 */
	val = inb(0x61);
	val = ((val & ~0x2) | 0x1);
	outb(0x61, val);

	/*
	 * Port 0x43 - PIT Mode/Command register (WO)
	 *
	 * 0xb6: bit 0 - 16b bin
	 *       bits 1/2 - Mode 3
	 *       bits 4/5 - 16b lo/hi byte
	 *       bit 7 - Channel 2 select
	 * Set mode and select channel:
	 */
	outb(0x43, 0xb6);

	/*
	 * Use 1193 divisor:
	 * 1.19318 MHz / 1193 = 1000.15Hz
	 * Period ~= 1/1000Hz ~= 1ms
	 */
	latch = (1193182/1000); /* = 1193 divisor */

	/*
	 * Port 0x42 - PIT Channel 2 (W)
	 *
	 * Set 16b counter, write lo byte then hi byte.
	 * Latch value:
	 */
	outb(0x42, latch & 0xff);
	outb(0x42, latch >> 8);

	/*
	 * Port 0x43 - PIT Mode/Command register (WO)
	 *
	 * 0xe8: bits 7/6 - Read Back Command
	 *       bit 5 - Don't latch count
	 *       bit 3 - Channel 2 select
	 * Get status (bit 4 clear) for channel:
	 */
	do {
		outb(0x43, 0xe8);
		cpu_relax();
		/*
		 * Port 0x42 - PIT Channel 2 (R)
		 *
		 * Read Back Status Byte (Port 0x43 written with bit 4 clear)
		 * 0x40: bit 6 - Null count flags
		 * If set, counter not yet been loaded and cannot be read back
		 */
	} while ( inb(0x42) & 0x40 );

	/*
	 * Counter started with new reload value copied into the current
	 * count. Synchronize on the next period.
	 */
	pit_sync_period();

	/* New period just started, get TSC start count. */
	start = rdtsc();

	/* Synchronize on the next period. */
	pit_sync_period();

	/* New period just started after ~1ms, get TSC end count. */
	end = rdtsc();

	/* Get the ticks per millisecond. */
	ticks_per_ms = end - start;
	calibrated = 1;
}

void mdelay(u32 ms)
{
	u64 ctsc, ftsc;

	if (!ms)
		return 0;

	ctsc = rdtsc();
	ftsc = ms * ticks_per_ms;

	while (ctsc < ftsc) {
		cpu_relax();
		ctsc = rdtsc();
	}
}
