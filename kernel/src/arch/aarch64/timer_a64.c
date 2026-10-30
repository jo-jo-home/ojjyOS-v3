/*
 * ojjyOS v3 Kernel - Generic Timer Driver (aarch64)
 *
 * Implements the shared timer API (timer_init / timer_get_ticks)
 * using the ARM architectural counter + EL1 virtual timer, wired to
 * the GTDT virtual timer PPI. Requests a 1 kHz tick, like the x86 PIT.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "timer.h"
#include "types.h"
#include "serial.h"
#include "gicv2.h"
#include "platform.h"
#include "drivers/rtc.h"
#include "clock.h"

/* The EL1 *virtual* timer (PPI from the platform, 27 on both machines):
 * Apple's hypervisor exposes it natively; the physical timer may trap. */

static volatile uint64_t timer_ms_ticks = 0;
static uint64_t timer_freq = 0;
static uint64_t tick_interval = 0;

static inline uint64_t read_cntfrq(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static uint64_t counter_start;

static inline uint64_t read_cntvct(void)
{
    uint64_t v;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

static inline void write_cntv_tval(uint64_t v)
{
    __asm__ volatile("msr cntv_tval_el0, %0" : : "r"(v));
}

static inline void enable_cntv(void)
{
    __asm__ volatile("msr cntv_ctl_el0, %0" : : "r"((uint64_t)1));
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
    counter_start = read_cntvct();

    /* Arm + enable the EL1 virtual timer; interrupts stay masked
     * in DAIF until the kernel enables them. */
    write_cntv_tval(tick_interval);
    enable_cntv();

    /* Route the timer PPI through the GIC */
    gicv2_enable_intid(platform.vtimer_ppi);

    serial_printf("[TIMER] Generic timer at %d Hz (tick = %d counts)\n",
        (int)timer_freq, (int)tick_interval);
}

/* Milliseconds since timer_init, straight from the counter: never drifts
 * when interrupts are late (or masked during long operations). */
uint64_t timer_get_ticks(void)
{
    if (!tick_interval) return timer_ms_ticks;
    return (read_cntvct() - counter_start) / tick_interval;
}

/*
 * Busy-wait sleep (ticks advance via the timer interrupt, so this
 * requires interrupts to be enabled, like the x86 PIT version).
 */
void timer_sleep(uint64_t ms)
{
    uint64_t start = timer_get_ticks();
    while (timer_get_ticks() - start < ms) {
        __asm__ volatile("wfi");
    }
}

/*
 * Timer interrupt: called from the IRQ entry path.
 */
void a64_timer_isr(void)
{
    timer_ms_ticks++;
    write_cntv_tval(tick_interval);
}

/* Once-only native boot health sample, outside the ISR. A working counter
 * alone does not prove the GIC actually delivers timer interrupts. */
void a64_timer_report_health(void)
{
    static bool reported;
    if (platform.qemu || reported) return;
    uint64_t ms = timer_get_ticks();
    if (ms < 5000) return;
    reported = true;
    RtcTime clock;
    rtc_read_time(&clock);
    uint64_t end_ms=timer_get_ticks();
    serial_printf("[TIMER] Health ms=%llu irqs=%llu clock=%d-%d-%dT%d:%d:%d end_ms=%llu ntp_ms=%lld\n",
        (unsigned long long)ms, (unsigned long long)timer_ms_ticks,
        (int)clock.year, (int)clock.month, (int)clock.day,
        (int)clock.hour, (int)clock.minute, (int)clock.second,
        end_ms,clock_ntp_offset_ms());
}
