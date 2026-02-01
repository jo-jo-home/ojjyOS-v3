/*
 * ojjyOS v3 Kernel - Timer Driver Implementation
 *
 * Uses the legacy PIT (8254) for system timing.
 * Configured for approximately 1000 Hz (1ms per tick).
 */

#include "timer.h"
#include "idt.h"
#include "serial.h"

/* PIT ports */
#define PIT_CHANNEL0    0x40
#define PIT_CHANNEL1    0x41
#define PIT_CHANNEL2    0x42
#define PIT_CMD         0x43

/* PIT frequency */
#define PIT_FREQUENCY   1193182

/* Desired tick rate (Hz) */
#define TICK_RATE       1000

/* Tick counter */
static volatile uint64_t tick_count = 0;

/* PIC helper (from idt.c) */
extern void pic_enable_irq(uint8_t irq);

/*
 * Timer interrupt handler
 */
static void timer_handler(InterruptFrame *frame)
{
    (void)frame;
    tick_count++;
}

/*
 * Initialize timer
 */
void timer_init(void)
{
    serial_printf("[TIMER] Initializing PIT at %d Hz...\n", TICK_RATE);

    /* Calculate divisor */
    uint16_t divisor = PIT_FREQUENCY / TICK_RATE;

    /* Set PIT mode: channel 0, access mode lobyte/hibyte, mode 3 (square wave) */
    outb(PIT_CMD, 0x36);

    /* Set divisor */
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);

    /* Register handler */
    idt_register_handler(IRQ_BASE + IRQ_TIMER, timer_handler);

    /* Enable IRQ 0 */
    pic_enable_irq(IRQ_TIMER);

    serial_printf("[TIMER] PIT initialized (divisor = %d)\n", divisor);
}

/*
 * Get tick count
 */
uint64_t timer_get_ticks(void)
{
    return tick_count;
}

/*
 * Sleep for specified milliseconds
 */
void timer_sleep(uint64_t ms)
{
    uint64_t end = tick_count + ms;
    while (tick_count < end) {
        __asm__ volatile("hlt");  /* Wait for interrupt */
    }
}
