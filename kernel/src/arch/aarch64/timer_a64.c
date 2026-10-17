/*
 * ojjyOS v3 Kernel - Generic Timer Driver (aarch64)
 *
 * Implements the shared timer API (timer_init / timer_get_ticks)
 * using the ARM architectural counter + EL1 physical timer, wired to
 * PPI 30 on the GIC. Provides a 1 kHz tick, like the x86 PIT driver.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "timer.h"
#include "types.h"
#include "serial.h"
#include "gicv2.h"

#define TIMER_PPI_INTID     30      /* Non-secure EL1 physical timer */

static volatile uint64_t timer_ms_ticks = 0;
static uint64_t timer_freq = 0;
static uint64_t tick_interval = 0;

static inline uint64_t read_cntfrq(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static inline void write_cntp_tval(uint64_t v)
{
    __asm__ volatile("msr cntp_tval_el0, %0" : : "r"(v));
}

static inline void enable_cntp(void)
{
    __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"((uint64_t)1));
}

void timer_init(void)
{
    timer_freq = read_cntfrq();
    if (timer_freq == 0) {
        timer_freq = 62500000;      /* QEMU virt default fallback */
    }
    tick_interval = timer_freq / 1000;   /* 1 kHz */
    if (tick_interval == 0) tick_interval = 1;

    timer_ms_ticks = 0;

    /* Arm + enable the EL1 physical timer; interrupts stay masked
     * in DAIF until the kernel enables them. */
    write_cntp_tval(tick_interval);
    enable_cntp();

    /* Route the timer PPI through the GIC */
    gicv2_enable_intid(TIMER_PPI_INTID);

    serial_printf("[TIMER] Generic timer at %d Hz (tick = %d counts)\n",
        (int)timer_freq, (int)tick_interval);
}

uint64_t timer_get_ticks(void)
{
    return timer_ms_ticks;
}

/*
 * Busy-wait sleep (ticks advance via the timer interrupt, so this
 * requires interrupts to be enabled, like the x86 PIT version).
 */
void timer_sleep(uint64_t ms)
{
    uint64_t start = timer_ms_ticks;
    while (timer_ms_ticks - start < ms) {
        __asm__ volatile("wfi");
    }
}

/*
 * Timer interrupt: called from the IRQ entry path.
 */
void a64_timer_isr(void)
{
    timer_ms_ticks++;
    write_cntp_tval(tick_interval);
}
