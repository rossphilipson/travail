/*
 * Calibrate CPU hz with PIT
 * https://wiki.osdev.org/Programmable_Interval_Timer
 */

#define TIMER_FREQ      1193182
#define TIMER_DIV(hz)   ((TIMER_FREQ+(hz)/2)/(hz))

static inline void cpu_relax(void)
{
	asm volatile ("rep; nop" : : : "memory");
}

void pit_calibrate(void)
{
	u8 val, latch;

	/*
	 * Port 0x61 - KB controller port B control register
	 * Bit 0: PIT timer 2 gate to speaker enable
	 * Bit 1: PC Speaker enable
	 * Disable speaker:
	 */
	val = inb(0x61);
	val = ((val & ~0x2) | 0x1);
	outb(0x61, val);

	/*
	 * Port 0x43 - PIT Mode/Command register (WO)
	 * 0xb6: bit 0 - 16b bin
	 *       bits 1/2 - Mode 3
	 *       bits 4/5 - 16b lo/hi byte
	 *       bit 7 - Channel 2 select
	 * Set mode and select channel:
	 */
	outb(0x43, 0xb6);

	/*
	 * 1193 divisor to get 1ms period time
	 * 1.19318 MHz / 1193 = 1000.15Hz
	 */
	latch = TIMER_DIV(1000);

	/*
	 * Port 0x42 - PIT Channel 2
	 * Set 16b counter with lo then hi write.
	 * Latch value:
	 */
	outb(0x42, latch & 0xff);
	outb(0x42, latch >> 8);
}
