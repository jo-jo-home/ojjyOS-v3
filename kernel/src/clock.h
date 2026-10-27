/*
 * ojjyOS v3 - Wall clock and SNTP correction (Track N, card TIM-04)
 *
 * The firmware/RTC gives a boot-time guess; this module adds an NTP
 * correction offset on top of it. Both platform rtc_read_time()
 * implementations call clock_adjust_rtc() just before returning, so the
 * menu bar clock, Auto appearance and everything else that reads the
 * RTC picks the corrected time up without a single caller change.
 *
 * The SNTP client (RFC 4330) is a polled state machine like DNS: after
 * the DHCP lease it resolves time.apple.com (fallback pool.ntp.org),
 * asks once, validates the reply hard, and resyncs every 6 hours.
 * Everything time-dependent is passed in as now_ms, so the host tests
 * (tests/test_clock.c) drive the whole client without a network.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_CLOCK_H
#define _OJJY_CLOCK_H

#include "types.h"
#include "drivers/rtc.h"

/* Resync interval and sanity bounds. */
#define CLOCK_RESYNC_MS     (6ULL * 60 * 60 * 1000)   /* 6 hours */
#define CLOCK_MAX_SKEW_MS   (10LL * 366 * 24 * 60 * 60 * 1000)  /* 10 years */
#define CLOCK_PLAUSIBLE_MIN 631152000LL   /* 1990-01-01 UTC */
#define CLOCK_PLAUSIBLE_MAX 4102444800LL  /* 2100-01-01 UTC */

/* ---- corrected time (kernel + host) ---- */

/* Apply the NTP correction to a freshly read RTC time. Called by the
 * platform rtc_read_time() implementations; safe to call before any
 * sync (offset 0). */
void clock_adjust_rtc(RtcTime *time);

/* Corrected wall clock as seconds since the Unix epoch. */
int64_t clock_now_unix(void);

/* Current correction and where it came from (for `date`/`ntp`). */
int64_t clock_ntp_offset_ms(void);
bool clock_ntp_synced(void);
const char *clock_ntp_server(void);

/* ---- pure helpers (host-tested in tests/test_clock.c) ---- */

/* Civil date/time <-> Unix seconds (UTC). */
int64_t clock_civil_to_unix(int year, int month, int day, int hour, int minute, int second);
void clock_unix_to_civil(int64_t unix_secs, RtcTime *out);

/* Unix ms <-> the NTP 64-bit fixed point (seconds since 1900 + 32-bit
 * fraction). 2036 rollover handled at parse time. */
uint64_t clock_unix_ms_to_ntp(int64_t unix_ms);
int64_t clock_ntp_to_unix(uint64_t ntp);

/* Build the 48-byte client packet (LI=0, VN=4, mode=3, our clock as
 * the transmit timestamp). */
void clock_sntp_build(uint8_t *packet48, int64_t unix_tx_ms);

/* Validate an SNTP reply against our transaction (unix_tx_ms is what
 * we sent, unix_rx_ms when it arrived, expected_now_ms our current
 * best guess of the time). Returns true and fills offset_ms_out /
 * server_unix_out for a good reply; false for anything wrong.
 * Rules: 48 bytes, LI != 3 (unsynchronized), mode 4, VN 3 or 4,
 * stratum 1..15, non-zero receive/transmit timestamps in order, originate echo must
 * match our request exactly, server time inside a plausible window,
 * |offset| within CLOCK_MAX_SKEW_MS unless our own clock is
 * implausible (then one big correction is allowed), 2036 era
 * rollover resolved independently within the supported plausible window.
 * This is unauthenticated SNTP, not protection against an on-path attacker. */
bool clock_sntp_parse(const uint8_t *reply, size_t len,
                      int64_t unix_tx_ms, int64_t unix_rx_ms,
                      int64_t expected_now_ms,
                      int64_t *offset_ms_out, int64_t *server_unix_out);

/* SNTP offset per RFC 4330: ((t1 - t0) + (t2 - t3)) / 2, all in ms. */
int64_t clock_sntp_offset_ms(int64_t t0_ms, int64_t t1_ms, int64_t t2_ms, int64_t t3_ms);

/* ---- client state machine (driven by net.c; time passed in) ---- */

/* Start/reschedule a sync (after the DHCP lease, or the `ntp` command).
 * Idempotent while a sync is in flight. */
void clock_ntp_start(uint64_t now_ms);
/* Force a new attempt even if one is in flight (the `ntp` command). */
void clock_ntp_force(uint64_t now_ms);
/* Advance the state machine; call from net_poll every tick. */
void clock_ntp_poll(uint64_t now_ms);

#endif /* _OJJY_CLOCK_H */
