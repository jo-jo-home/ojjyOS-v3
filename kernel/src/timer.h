/*
 * ojjyOS v3 Kernel - Timer Driver
 *
 * Uses PIT (Programmable Interval Timer) for system tick.
 */

#ifndef _OJJY_TIMER_H
#define _OJJY_TIMER_H

#include "types.h"

/* Initialize timer (sets up PIT at 1000 Hz) */
void timer_init(void);

/* Get tick count (milliseconds since boot) */
uint64_t timer_get_ticks(void);

/* Sleep for specified milliseconds */
void timer_sleep(uint64_t ms);

#endif /* _OJJY_TIMER_H */
