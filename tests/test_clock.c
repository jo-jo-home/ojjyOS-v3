/*
 * Host test for the wall clock and SNTP client (kernel/src/clock.c,
 * card TIM-04): civil<->unix conversion (leap years, weekday), NTP
 * fixed-point conversion, client packet build, reply validation
 * (mode/stratum/echo/plausible-window/absurd-jump/2036 era), RFC 4330
 * offset math, and the polled client state machine driven through
 * stubbed DNS + UDP against a settable fake RTC.
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "clock.h"
#include "net/net.h"
#include "string.h"
#include "test_support.h"
#include "timer.h"

void clock_test_reset(void);

/* ---- fakes for what the kernel build provides for real ---- */

static int64_t fake_rtc_unix;
static uint64_t fake_ticks;
uint64_t timer_get_ticks(void) { return fake_ticks; }
void rtc_read_time(RtcTime *time)
{
    if (!time) return;
    clock_unix_to_civil(fake_rtc_unix, time);
    /* Exactly like the platform drivers: the correction is applied by
     * the RTC read itself. */
    clock_adjust_rtc(time);
}

static ip4_t dns_result_ip;
static int dns_result;                  /* 0 pending, 1 done, -1 fail */
DnsStatus dns_resolve(const char *name, ip4_t *out)
{
    (void)name;
    if (dns_result == 1) { *out = dns_result_ip; return DNS_DONE; }
    if (dns_result == -1) return DNS_FAIL;
    return DNS_PENDING;
}

static udp_handler_fn bound_handler;
int net_udp_bind(uint16_t port, udp_handler_fn handler)
{
    (void)port;
    bound_handler = handler;
    return 0;
}
void net_udp_unbind(uint16_t port) { (void)port; }

static uint8_t sent[48];
static int sent_count;
int net_udp_send(ip4_t dst, uint16_t src_port, uint16_t dst_port, const uint8_t *data, size_t len)
{
    (void)src_port; (void)dst_port;
    if (len != 48) return -1;
    (void)dst;
    memcpy(sent, data, 48);
    sent_count++;
    return 0;
}

/* ---- packet helpers ---- */

static uint32_t rd_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint64_t rd_be64(const uint8_t *p)
{
    return ((uint64_t)rd_be(p) << 32) | rd_be(p + 4);
}

static void wr_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* Build a server reply answering our transaction (`our_tx` in NTP
 * format, i.e. what we sent as transmit) with server time `server_ntp`
 * (full 64-bit NTP). Knobs for the rejection cases. */
static void build_reply(uint8_t *out, uint64_t server_ntp, uint64_t our_tx,
                        uint8_t first_byte, uint8_t stratum, bool echo)
{
    memset(out, 0, 48);
    out[0] = first_byte;
    out[1] = stratum;
    if (echo) {
        wr_be(out + 24, (uint32_t)(our_tx >> 32));
        wr_be(out + 28, (uint32_t)our_tx);
    }
    wr_be(out + 32, (uint32_t)(server_ntp >> 32));   /* receive */
    wr_be(out + 36, (uint32_t)server_ntp);
    wr_be(out + 40, (uint32_t)(server_ntp >> 32));   /* transmit */
    wr_be(out + 44, (uint32_t)server_ntp);
}

#define T2026  1793059200LL           /* 2026-10-27 00:00:00 UTC */
#define T2040  2208988800LL           /* 2040-01-01 00:00:00 UTC */

static void test_conversions(void)
{
    CHECK(clock_civil_to_unix(1970, 1, 1, 0, 0, 0) == 0, "1970-01-01 is Unix 0");
    CHECK(clock_civil_to_unix(2000, 2, 29, 12, 0, 0) == 951782400 + 43200, "leap day 2000-02-29");
    CHECK(clock_civil_to_unix(2026, 10, 27, 0, 0, 0) == T2026, "2026-10-27 midnight");

    RtcTime t;
    memset(&t, 0, sizeof(t));
    clock_unix_to_civil(T2026 + 13 * 3600 + 7 * 60 + 42, &t);
    CHECK(t.year == 2026 && t.month == 10 && t.day == 27, "civil fields of 2026-10-27");
    CHECK(t.hour == 13 && t.minute == 7 && t.second == 42, "time fields of 13:07:42");
    CHECK(t.weekday == 3, "2026-10-27 is a Tuesday (weekday 3)");

    int64_t moments[] = { 0, 951782400, T2026, T2026 + 86399, 2147483647LL };
    int ok = 1;
    for (unsigned i = 0; i < sizeof(moments) / sizeof(moments[0]); i++) {
        RtcTime r;
        memset(&r, 0, sizeof(r));
        clock_unix_to_civil(moments[i], &r);
        if (clock_civil_to_unix(r.year, r.month, r.day, r.hour, r.minute, r.second) != moments[i]) ok = 0;
    }
    CHECK(ok, "civil<->unix round trips");

    CHECK((int64_t)(clock_unix_ms_to_ntp(0) >> 32) == 2208988800LL, "Unix 0 is NTP 2208988800");
    CHECK(clock_ntp_to_unix(clock_unix_ms_to_ntp(T2026 * 1000)) == T2026, "ntp<->unix round trip");
}

static void test_offset_math(void)
{
    CHECK(clock_sntp_offset_ms(0, 5000, 5000, 10000) == 0, "symmetric exchange has offset 0");
    CHECK(clock_sntp_offset_ms(1000, 4000, 5000, 8000) == 0, "mixed delays cancel");
    CHECK(clock_sntp_offset_ms(0, 6000, 7000, 10000) == 1500, "offset is the average delay");
    CHECK(clock_sntp_offset_ms(0, 4000, 5000, 10000) == -500, "server behind us");
}

static void test_parse(void)
{
    fake_rtc_unix = T2026;
    int64_t offset = 0, server = 0;
    uint64_t our_tx = clock_unix_ms_to_ntp(T2026 * 1000);
    uint8_t reply[48];

    /* Good reply: server 2.5 s ahead of us. */
    build_reply(reply, clock_unix_ms_to_ntp((T2026 + 2) * 1000 + 500), our_tx, 0x24, 2, true);
    CHECK(clock_sntp_parse(reply, 48, T2026 * 1000, T2026 * 1000 + 100,
                           T2026 * 1000, &offset, &server), "good reply parses");
    CHECK(offset >= 2400 && offset <= 2600, "offset ~+2.5 s");
    CHECK(server == T2026 + 2, "server seconds reported");

    /* Rejections. */
    build_reply(reply, clock_unix_ms_to_ntp((T2026 + 2) * 1000), our_tx, 0x24, 2, true);
    CHECK(!clock_sntp_parse(reply, 47, T2026 * 1000, T2026 * 1000, T2026 * 1000, &offset, &server),
          "short packet rejected");
    build_reply(reply, clock_unix_ms_to_ntp((T2026 + 2) * 1000), our_tx, 0xE4, 2, true);
    CHECK(!clock_sntp_parse(reply, 48, T2026 * 1000, T2026 * 1000, T2026 * 1000, &offset, &server),
          "unsynchronized server (LI=3) rejected");
    build_reply(reply, clock_unix_ms_to_ntp((T2026 + 2) * 1000), our_tx, 0x21, 2, true);
    CHECK(!clock_sntp_parse(reply, 48, T2026 * 1000, T2026 * 1000, T2026 * 1000, &offset, &server),
          "non-server mode rejected");
    build_reply(reply, clock_unix_ms_to_ntp((T2026 + 2) * 1000), our_tx, 0x14, 2, true);
    CHECK(!clock_sntp_parse(reply, 48, T2026 * 1000, T2026 * 1000, T2026 * 1000, &offset, &server),
          "ancient version rejected");
    build_reply(reply, clock_unix_ms_to_ntp((T2026 + 2) * 1000), our_tx, 0x24, 0, true);
    CHECK(!clock_sntp_parse(reply, 48, T2026 * 1000, T2026 * 1000, T2026 * 1000, &offset, &server),
          "stratum 0 (kiss of death) rejected");
    build_reply(reply, clock_unix_ms_to_ntp((T2026 + 2) * 1000), our_tx, 0x24, 16, true);
    CHECK(!clock_sntp_parse(reply, 48, T2026 * 1000, T2026 * 1000, T2026 * 1000, &offset, &server),
          "stratum 16 rejected");
    build_reply(reply, 0, our_tx, 0x24, 2, true);
    CHECK(!clock_sntp_parse(reply, 48, T2026 * 1000, T2026 * 1000, T2026 * 1000, &offset, &server),
          "zero transmit timestamp rejected");
    build_reply(reply, clock_unix_ms_to_ntp((T2026 + 2) * 1000), our_tx + 1, 0x24, 2, true);
    CHECK(!clock_sntp_parse(reply, 48, T2026 * 1000, T2026 * 1000, T2026 * 1000, &offset, &server),
          "reply to another transaction rejected");
    build_reply(reply, clock_unix_ms_to_ntp((T2026 - 11 * 366 * 86400) * 1000), our_tx, 0x24, 2, true);
    CHECK(!clock_sntp_parse(reply, 48, T2026 * 1000, T2026 * 1000, T2026 * 1000, &offset, &server),
          "absurd server jump rejected");

    /* First correction from an implausible clock (a 1970 boot with no
     * RTC) is allowed — that is the whole point of the client. */
    build_reply(reply, clock_unix_ms_to_ntp(T2026 * 1000), clock_unix_ms_to_ntp(0), 0x24, 2, true);
    CHECK(clock_sntp_parse(reply, 48, 0, 0, 0, &offset, &server),
          "big correction accepted when the local clock is implausible");
    CHECK(server == T2026, "1970 boot syncs to the server time");

    memset(reply + 32, 0, 8);
    CHECK(!clock_sntp_parse(reply, 48, 0, 100, 0, &offset, &server),
          "missing server receive timestamp rejected even on 1970 boot");
    build_reply(reply, clock_unix_ms_to_ntp(T2026 * 1000), clock_unix_ms_to_ntp(0), 0x24, 2, true);
    uint64_t later = clock_unix_ms_to_ntp((T2026 + 1) * 1000);
    wr_be(reply + 32, (uint32_t)(later >> 32)); wr_be(reply + 36, (uint32_t)later);
    CHECK(!clock_sntp_parse(reply, 48, 0, 100, 0, &offset, &server),
          "server transmit before receive rejected");
}

static void test_era_rollover(void)
{
    /* A transmit timestamp whose 32-bit seconds field belongs to the
     * previous NTP era: raw conversion says 1903; the parser must lift
     * it by 2^32 seconds against our expectation (2040). */
    int64_t offset = 0, server = 0;
    uint64_t era1_secs = (uint64_t)(T2040 + 2208988800 - ((int64_t)1 << 32));
    uint64_t reply_ntp = era1_secs << 32;
    uint64_t our_tx = clock_unix_ms_to_ntp(T2040 * 1000);
    uint8_t reply[48];
    build_reply(reply, reply_ntp, our_tx, 0x24, 2, true);
    CHECK(clock_ntp_to_unix(reply_ntp) < T2040 - 315532800, "raw value really is era-0 (1903)");
    CHECK(clock_sntp_parse(reply, 48, T2040 * 1000, T2040 * 1000, T2040 * 1000, &offset, &server),
          "era-wrapped transmit timestamp accepted");
    CHECK(server == T2040, "era rollover resolves into this era");
    build_reply(reply, reply_ntp, clock_unix_ms_to_ntp(0), 0x24, 2, true);
    CHECK(clock_sntp_parse(reply, 48, 0, 0, 0, &offset, &server) && server == T2040,
          "1970 no-RTC boot can sync after the 2036 rollover");

    int64_t rollover_ms = (4294967296LL - 2208988800LL) * 1000;
    build_reply(reply, clock_unix_ms_to_ntp(rollover_ms + 1),
                clock_unix_ms_to_ntp(rollover_ms - 10), 0x24, 2, true);
    uint64_t before_rollover = clock_unix_ms_to_ntp(rollover_ms - 1);
    wr_be(reply + 32, (uint32_t)(before_rollover >> 32));
    wr_be(reply + 36, (uint32_t)before_rollover);
    CHECK(clock_sntp_parse(reply, 48, rollover_ms - 10, rollover_ms + 10,
                           rollover_ms, &offset, &server) && offset >= -1 && offset <= 1,
          "exchange straddling the NTP era boundary parses both timestamps independently");

    /* The same wrapped value is NOT lifted when it makes no sense:
     * expected now 2026, wrapped value lands after 2036 -> offset way
     * over 10 years -> rejected. */
    build_reply(reply, reply_ntp, clock_unix_ms_to_ntp(T2026 * 1000), 0x24, 2, true);
    CHECK(!clock_sntp_parse(reply, 48, T2026 * 1000, T2026 * 1000, T2026 * 1000, &offset, &server),
          "era lift that lands far outside the skew window is rejected");
}

static void test_state_machine(void)
{
    clock_test_reset();
    fake_rtc_unix = T2026;

    /* Offline: DNS fails for both servers -> backoff, no crash. */
    dns_result = -1;
    clock_ntp_start(1000);
    clock_ntp_poll(1000);
    clock_ntp_poll(1001);
    CHECK(sent_count == 0, "nothing sent while DNS fails");

    /* DNS pending then done. */
    dns_result = 0;
    clock_ntp_force(5000);
    clock_ntp_poll(5000);
    CHECK(sent_count == 0, "pending DNS sends nothing");
    dns_result = 1;
    dns_result_ip = 0x08080808;
    clock_ntp_poll(6000);
    CHECK(sent_count == 1, "query sent once DNS resolves");
    CHECK(sent[0] == 0x23, "sent packet is a client request");
    uint64_t our_tx = rd_be64(sent + 40);

    /* Reply through the bound handler: the server runs 30 s ahead of
     * our raw clock; the reply arrives ~100 ms after our request. */
    uint8_t reply[48];
    build_reply(reply, clock_unix_ms_to_ntp((T2026 + 30) * 1000 + 100), our_tx, 0x24, 2, true);
    fake_ticks = 6000;
    bound_handler(0x08080808, 123, 53536, reply, 48);
    CHECK(clock_ntp_synced(), "synced after a good reply");
    CHECK(clock_ntp_offset_ms() > 29900 && clock_ntp_offset_ms() < 30200, "offset ~+30 s applied");
    CHECK(strcmp(clock_ntp_server(), "time.apple.com") == 0, "server name recorded");

    /* The corrected clock: raw RTC is 30 s behind, readers see the
     * server's time. */
    CHECK(clock_now_unix() >= T2026 + 29 && clock_now_unix() <= T2026 + 31,
          "clock_now_unix shows the corrected time");
    RtcTime t;
    memset(&t, 0, sizeof(t));
    rtc_read_time(&t);
    CHECK(t.year == 2026 && t.month == 10 && t.day == 27, "rtc_read_time is corrected");

    /* Deadlines use uptime, not Unix time; start itself never sends. */
    clock_ntp_start(7000);
    clock_ntp_poll(7000);
    CHECK(sent_count == 1, "no resync before the 6 h mark");
}

static void test_lifecycle_regressions(void)
{
    clock_test_reset(); sent_count = 0; fake_ticks = 1000;
    fake_rtc_unix = T2026; dns_result = -1;
    clock_ntp_start(1000);
    clock_ntp_poll(1000); clock_ntp_poll(1001);
    dns_result = 1; dns_result_ip = 0x08080808;
    clock_ntp_poll(61001);
    CHECK(sent_count == 1, "offline retry works without another DHCP event");
    clock_test_reset(); sent_count = 0; dns_result = -1;
    clock_ntp_start(1000); clock_ntp_poll(1000); clock_ntp_poll(1001);
    dns_result = 1;
    clock_ntp_start(2000); /* DHCP renewal must not defeat backoff. */
    clock_ntp_poll(2000);
    CHECK(sent_count == 0, "DHCP restart respects offline backoff");
    fake_ticks = 61001;
    clock_ntp_poll(fake_ticks);
    CHECK(sent_count == 1, "offline retry starts automatically at its uptime deadline");

    clock_test_reset(); sent_count = 0;
    clock_ntp_start(fake_ticks); clock_ntp_poll(fake_ticks);

    uint8_t reply[48];
    build_reply(reply, clock_unix_ms_to_ntp(T2026 * 1000 + 500), rd_be64(sent + 40), 0x24, 2, true);
    bound_handler(0x01010101, 123, 53536, reply, 48);
    CHECK(!clock_ntp_synced(), "reply from another IP rejected");
    bound_handler(dns_result_ip, 124, 53536, reply, 48);
    CHECK(!clock_ntp_synced(), "reply from another source port rejected");
    bound_handler(dns_result_ip, 123, 53537, reply, 48);
    CHECK(!clock_ntp_synced(), "reply to another local port rejected");
    bound_handler(dns_result_ip, 123, 53536, reply, 48);
    CHECK(clock_ntp_synced(), "selected server accepted");
    clock_ntp_poll(fake_ticks + CLOCK_RESYNC_MS - 1);
    CHECK(sent_count == 1, "periodic sync not early");
    fake_ticks += CLOCK_RESYNC_MS;
    clock_ntp_poll(fake_ticks);
    CHECK(sent_count == 2, "six-hour resync starts automatically using uptime");

    /* Several +0.5-second corrections against a second-resolution RTC
     * must replace the fractional remainder, not accumulate into seconds. */
    for (int i = 0; i < 4; i++) {
        clock_ntp_force(fake_ticks); clock_ntp_poll(fake_ticks);
        build_reply(reply, clock_unix_ms_to_ntp(T2026 * 1000 + 500), rd_be64(sent + 40), 0x24, 2, true);
        bound_handler(dns_result_ip, 123, 53536, reply, 48);
        CHECK(clock_ntp_offset_ms() >= 499 && clock_ntp_offset_ms() <= 500,
              "each fractional sync keeps the same offset");
    }
    CHECK(clock_ntp_offset_ms() >= 499 && clock_ntp_offset_ms() <= 500,
          "repeated fractional syncs do not accumulate unapplied corrections");
    CHECK(clock_now_unix() == T2026, "fractional sync does not move RTC by whole seconds");

    clock_ntp_force(fake_ticks); clock_ntp_poll(fake_ticks);
    build_reply(reply, clock_unix_ms_to_ntp(T2026 * 1000), rd_be64(sent + 40), 0x24, 2, true);
    int64_t saved = clock_ntp_offset_ms();
    fake_ticks += 3000;
    bound_handler(dns_result_ip, 123, 53536, reply, 48);
    CHECK(clock_ntp_offset_ms() == saved, "reply arriving at timeout cannot change clock");
}

int main(void)
{
    test_conversions();
    test_offset_math();
    test_parse();
    test_era_rollover();
    test_state_machine();
    test_lifecycle_regressions();
    return finish();
}
