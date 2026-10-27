/*
 * ojjyOS v3 - Wall clock and SNTP correction (implementation)
 *
 * Pure conversion/validation logic plus a polled SNTP client. See
 * clock.h for the design; NET_WEB_PLAN.md N4 for the spec.
 *
 * ojjyOS - Created by Jonas Lee
 */
#include "clock.h"
#include "string.h"
#include "serial.h"
#include "net/net.h"

#define NTP_PORT        123
#define NTP_PORT_LOCAL  53536
#define NTP_REPLY_MS    3000        /* give up on a reply after 3 s */
#define NTP_SERVERS     2

static const char *const ntp_hosts[NTP_SERVERS] = {
    "time.apple.com", "pool.ntp.org",
};

/* ------------------------------------------------------------------ */
/* Pure helpers                                                        */
/* ------------------------------------------------------------------ */

/* Days since 1970-01-01 from a civil date (Howard Hinnant's
 * days_from_civil; the mathematical identity behind every calendar). */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t days, int *year, int *month, int *day)
{
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    uint64_t doe = (uint64_t)(days - era * 146097);
    uint64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = (int64_t)yoe + era * 400;
    uint64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint64_t mp = (5 * doy + 2) / 153;
    uint64_t d = doy - (153 * mp + 2) / 5 + 1;
    uint64_t m = (mp < 10 ? mp + 3 : mp - 9);
    if (m <= 2) y++;
    *year = (int)y;
    *month = (int)m;
    *day = (int)d;
}

int64_t clock_civil_to_unix(int year, int month, int day, int hour, int minute, int second)
{
    return days_from_civil(year, month, day) * 86400LL + hour * 3600LL + minute * 60LL + second;
}

void clock_unix_to_civil(int64_t unix_secs, RtcTime *out)
{
    if (!out) return;
    int64_t days = unix_secs / 86400;
    int64_t rem = unix_secs % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }

    int year, month, day;
    civil_from_days(days, &year, &month, &day);
    out->year = (uint16_t)year;
    out->month = (uint8_t)month;
    out->day = (uint8_t)day;
    out->hour = (uint8_t)(rem / 3600);
    out->minute = (uint8_t)((rem % 3600) / 60);
    out->second = (uint8_t)(rem % 60);
    /* 1970-01-01 was a Thursday; RtcTime.weekday: 1 = Sunday. */
    int dow = (int)(((days + 4) % 7 + 7) % 7);
    out->weekday = (uint8_t)(dow + 1);
}

/* NTP timestamps: seconds since 1900-01-01 in the high 32 bits,
 * fraction of a second in the low 32. Unix is seconds since 1970. */
#define NTP_UNIX_DELTA  2208988800LL

uint64_t clock_unix_ms_to_ntp(int64_t unix_ms)
{
    int64_t secs = unix_ms / 1000;
    int64_t frac = unix_ms % 1000;
    if (frac < 0) { frac += 1000; secs -= 1; }
    uint64_t ntp = (uint64_t)(secs + NTP_UNIX_DELTA) << 32;
    return ntp | (uint32_t)((uint64_t)frac * (1ULL << 32) / 1000);
}

int64_t clock_ntp_to_unix(uint64_t ntp)
{
    return (int64_t)(ntp >> 32) - NTP_UNIX_DELTA;
}

static uint64_t rd32_be(const uint8_t *p)
{
    return ((uint64_t)p[0] << 24) | ((uint64_t)p[1] << 16) | ((uint64_t)p[2] << 8) | p[3];
}

static void wr32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

void clock_sntp_build(uint8_t *packet48, int64_t unix_tx_ms)
{
    if (!packet48) return;
    memset(packet48, 0, 48);
    packet48[0] = 0x23;                      /* LI=0, VN=4, mode=3 (client) */
    uint64_t tx = clock_unix_ms_to_ntp(unix_tx_ms);
    wr32_be(packet48 + 40, (uint32_t)(tx >> 32));   /* transmit seconds */
    wr32_be(packet48 + 44, (uint32_t)tx);            /* transmit fraction */
}

int64_t clock_sntp_offset_ms(int64_t t0_ms, int64_t t1_ms, int64_t t2_ms, int64_t t3_ms)
{
    return ((t1_ms - t0_ms) + (t2_ms - t3_ms)) / 2;
}

bool clock_sntp_parse(const uint8_t *reply, size_t len,
                       int64_t unix_tx_ms, int64_t unix_rx_ms,
                       int64_t expected_now_ms,
                       int64_t *offset_ms_out, int64_t *server_unix_out)
{
    if (!reply || len != 48) return false;

    unsigned li = (unsigned)reply[0] >> 6;
    unsigned vn = ((unsigned)reply[0] >> 3) & 7;
    unsigned mode = (unsigned)reply[0] & 7;
    unsigned stratum = reply[1];
    if (li == 3) return false;               /* server unsynchronized */
    if (mode != 4) return false;             /* must be a server reply */
    if (vn < 3 || vn > 4) return false;
    if (stratum < 1 || stratum > 15) return false;

    uint64_t originate = (rd32_be(reply + 24) << 32) | rd32_be(reply + 28);
    uint64_t receive = (rd32_be(reply + 32) << 32) | rd32_be(reply + 36);
    uint64_t transmit = (rd32_be(reply + 40) << 32) | rd32_be(reply + 44);
    if (transmit == 0) return false;

    /* The reply must be to OUR request: the server echoes our transmit
     * timestamp in the originate field. */
    uint64_t our_tx = clock_unix_ms_to_ntp(unix_tx_ms);
    if (originate != our_tx) return false;

    /* Resolve the 2036-era rollover against what we believe "now" is. */
    int64_t t1_unix = clock_ntp_to_unix(receive);
    int64_t t2_unix = clock_ntp_to_unix(transmit);
    int64_t floor = expected_now_ms / 1000 - CLOCK_MAX_SKEW_MS / 1000;
    if (t2_unix < floor) {
        t1_unix += (int64_t)1 << 32;
        t2_unix += (int64_t)1 << 32;
    }

    /* Server time must land in a plausible window at all. */
    if (t2_unix < CLOCK_PLAUSIBLE_MIN || t2_unix > CLOCK_PLAUSIBLE_MAX) return false;

    int64_t t0_ms = unix_tx_ms;
    int64_t t1_ms = t1_unix * 1000 + (int64_t)((receive & 0xffffffff) * 1000 >> 32);
    int64_t t2_ms = t2_unix * 1000 + (int64_t)((transmit & 0xffffffff) * 1000 >> 32);
    int64_t t3_ms = unix_rx_ms;
    int64_t offset = clock_sntp_offset_ms(t0_ms, t1_ms, t2_ms, t3_ms);

    /* Reject absurd jumps — unless our own clock is implausible, in
     * which case one large first correction is exactly what we want
     * (and the plausible-window check above still bounds it). */
    bool local_implausible = expected_now_ms / 1000 < CLOCK_PLAUSIBLE_MIN ||
                             expected_now_ms / 1000 > CLOCK_PLAUSIBLE_MAX;
    if (!local_implausible && (offset > CLOCK_MAX_SKEW_MS || offset < -CLOCK_MAX_SKEW_MS)) {
        return false;
    }

    if (offset_ms_out) *offset_ms_out = offset;
    if (server_unix_out) *server_unix_out = t2_unix;
    return true;
}

/* ------------------------------------------------------------------ */
/* Corrected time                                                       */
/* ------------------------------------------------------------------ */

static int64_t ntp_offset_ms;
static bool ntp_synced;
static char ntp_server_name[20];

int64_t clock_ntp_offset_ms(void) { return ntp_offset_ms; }
bool clock_ntp_synced(void) { return ntp_synced; }
const char *clock_ntp_server(void) { return ntp_server_name; }

void clock_adjust_rtc(RtcTime *time)
{
    if (!time) return;
    int64_t unix = clock_civil_to_unix(time->year, time->month, time->day,
                                       time->hour, time->minute, time->second);
    unix += ntp_offset_ms / 1000;
    clock_unix_to_civil(unix, time);
}

int64_t clock_now_unix(void)
{
    RtcTime now;
    memset(&now, 0, sizeof(now));
    rtc_read_time(&now);
    return clock_civil_to_unix(now.year, now.month, now.day, now.hour, now.minute, now.second);
}

/* ------------------------------------------------------------------ */
/* SNTP client (poll from net_poll; every wait is in now_ms units)      */
/* ------------------------------------------------------------------ */

typedef enum { NTP_IDLE, NTP_RESOLVING, NTP_WAIT_REPLY } NtpState;

static NtpState ntp_state;
static int ntp_host_index;
static ip4_t ntp_ip;
static int64_t ntp_tx_ms;
static uint64_t ntp_next_ms;          /* when to do the next action */
static uint64_t ntp_backoff_ms;       /* retry spacing while offline */
static bool ntp_bound;

static void ntp_log_offset(int64_t delta_ms)
{
    char delta[24];
    utoa((uint64_t)(delta_ms < 0 ? -delta_ms : delta_ms), delta, 10);
    serial_printf("[CLOCK] NTP offset %s%s ms from %s\n",
                  delta_ms < 0 ? "-" : "+", delta, ntp_hosts[ntp_host_index]);
}

static void ntp_schedule_retry(uint64_t now_ms)
{
    if (ntp_backoff_ms < 30000) ntp_backoff_ms = 30000;
    ntp_backoff_ms *= 2;
    if (ntp_backoff_ms > 600000) ntp_backoff_ms = 600000;
    ntp_state = NTP_IDLE;
    ntp_next_ms = now_ms + ntp_backoff_ms;
    if (ntp_backoff_ms == 60000 || ntp_backoff_ms == 600000) {
        serial_printf("[CLOCK] NTP unreachable, retrying every %u s\n",
                      (unsigned)(ntp_backoff_ms / 1000));
    }
}

static void ntp_send_query(uint64_t now_ms)
{
    uint8_t packet[48];
    ntp_tx_ms = clock_now_unix() * 1000;
    clock_sntp_build(packet, ntp_tx_ms);
    if (net_udp_send(ntp_ip, NTP_PORT_LOCAL, NTP_PORT, packet, sizeof(packet)) == 0) {
        ntp_state = NTP_WAIT_REPLY;
        ntp_next_ms = now_ms + NTP_REPLY_MS;
    } else {
        ntp_schedule_retry(now_ms);
    }
}

static void ntp_udp_handler(ip4_t src, uint16_t src_port, uint16_t dst_port,
                            const uint8_t *data, size_t len)
{
    (void)src; (void)src_port; (void)dst_port;
    if (ntp_state != NTP_WAIT_REPLY) return;

    uint64_t now_ms = (uint64_t)clock_now_unix() * 1000;
    int64_t delta = 0;
    if (!clock_sntp_parse(data, len, ntp_tx_ms, (int64_t)now_ms,
                           (int64_t)now_ms, &delta, NULL)) {
        return;                          /* bad reply: let the timeout retry */
    }

    ntp_offset_ms += delta;
    ntp_synced = true;
    strncpy(ntp_server_name, ntp_hosts[ntp_host_index], sizeof(ntp_server_name) - 1);
    ntp_server_name[sizeof(ntp_server_name) - 1] = '\0';
    ntp_log_offset(delta);
    ntp_backoff_ms = 0;
    ntp_state = NTP_IDLE;
    ntp_next_ms = now_ms + CLOCK_RESYNC_MS;
}

void clock_ntp_start(uint64_t now_ms)
{
    if (!ntp_bound) {
        net_udp_bind(NTP_PORT_LOCAL, ntp_udp_handler);
        ntp_bound = true;
    }
    if (ntp_state != NTP_IDLE && ntp_state != NTP_WAIT_REPLY) return;
    if (ntp_state == NTP_IDLE && ntp_synced && now_ms < ntp_next_ms) return;
    ntp_host_index = 0;
    ntp_state = NTP_RESOLVING;
    ntp_next_ms = now_ms;
}

void clock_ntp_force(uint64_t now_ms)
{
    ntp_backoff_ms = 0;
    ntp_host_index = 0;
    if (!ntp_bound) {
        net_udp_bind(NTP_PORT_LOCAL, ntp_udp_handler);
        ntp_bound = true;
    }
    ntp_state = NTP_RESOLVING;
    ntp_next_ms = now_ms;
}

void clock_ntp_poll(uint64_t now_ms)
{
    if (ntp_state == NTP_IDLE || now_ms < ntp_next_ms) return;

    if (ntp_state == NTP_RESOLVING) {
        ip4_t ip = 0;
        DnsStatus st = dns_resolve(ntp_hosts[ntp_host_index], &ip);
        if (st == DNS_PENDING) {
            ntp_next_ms = now_ms + 100;
            return;
        }
        if (st == DNS_DONE) {
            ntp_ip = ip;
            ntp_send_query(now_ms);
            return;
        }
        /* This host failed to resolve; try the fallback, then back off. */
        ntp_host_index++;
        if (ntp_host_index < NTP_SERVERS) {
            ntp_next_ms = now_ms;
            return;
        }
        ntp_schedule_retry(now_ms);
        return;
    }

    if (ntp_state == NTP_WAIT_REPLY) {
        /* No (good) reply in time; try the fallback host, then back off. */
        ntp_host_index++;
        if (ntp_host_index < NTP_SERVERS) {
            ntp_state = NTP_RESOLVING;
            ntp_next_ms = now_ms;
            return;
        }
        ntp_schedule_retry(now_ms);
    }
}

#ifdef OJJY_HOST_TEST
/* Host tests reset the whole client between cases (like net_test_reset). */
void clock_test_reset(void)
{
    ntp_state = NTP_IDLE;
    ntp_host_index = 0;
    ntp_ip = 0;
    ntp_tx_ms = 0;
    ntp_next_ms = 0;
    ntp_backoff_ms = 0;
    ntp_bound = false;
    ntp_offset_ms = 0;
    ntp_synced = false;
    ntp_server_name[0] = '\0';
}
#endif
