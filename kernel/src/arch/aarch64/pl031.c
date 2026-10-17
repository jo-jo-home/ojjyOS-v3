/*
 * ojjyOS v3 Kernel - PL031 RTC (aarch64)
 *
 * Implements the shared RTC API (rtc_read_time / rtc_print_time)
 * on the PL031 real-time clock of the QEMU virt machine, which
 * counts seconds since the Unix epoch.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "drivers/rtc.h"
#include "console.h"
#include "serial.h"
#include "string.h"
#include "types.h"

#define RTC_BASE        0x09010000UL

#define RTC_DR          0x00    /* Data register (seconds) */
#define RTC_MR          0x04    /* Match register */
#define RTC_LR          0x08    /* Load register */
#define RTC_CR          0x0C    /* Control */
#define RTC_IMSC        0x10    /* Interrupt mask */
#define RTC_ICR         0x1C    /* Interrupt clear */

static volatile uint32_t *rtc = (volatile uint32_t *)RTC_BASE;

void rtc_init(void)
{
    /* Clear any pending interrupt; the RTC is polled */
    rtc[RTC_IMSC / 4] = 0;
    rtc[RTC_CR / 4] = 1;             /* Start counting */

    serial_printf("[RTC] PL031 ready (%d seconds since epoch)\n",
        (int)rtc[RTC_DR / 4]);
}

/*
 * The PL031 is not part of the driver model (it is initialized
 * directly); diagnostics shows it via the serial log.
 */
Driver *rtc_get_driver(void)
{
    return NULL;
}

/*
 * Convert days since 1970-01-01 to year/month/day
 * (Howard Hinnant's civil_from_days algorithm)
 */
static void days_to_civil(int64_t days, int *year, int *month, int *day)
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

static int day_of_week(int64_t days_since_epoch)
{
    /* 1970-01-01 was a Thursday (4). RtcTime.weekday: 1 = Sunday */
    int dow = (int)(((days_since_epoch + 4) % 7) + 7) % 7;
    return dow + 1;
}

void rtc_read_time(RtcTime *time)
{
    if (!time) return;

    uint64_t secs = rtc[RTC_DR / 4];

    int64_t days = (int64_t)(secs / 86400);
    uint64_t rem = secs % 86400;

    int year, month, day;
    days_to_civil(days, &year, &month, &day);

    time->year = (uint16_t)year;
    time->month = (uint8_t)month;
    time->day = (uint8_t)day;
    time->hour = (uint8_t)(rem / 3600);
    time->minute = (uint8_t)((rem % 3600) / 60);
    time->second = (uint8_t)(rem % 60);
    time->weekday = (uint8_t)day_of_week(days);
}

void rtc_print_time(void)
{
    RtcTime time;
    rtc_read_time(&time);

    static const char *months[] = {
        "??", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    const char *month = (time.month >= 1 && time.month <= 12)
        ? months[time.month] : "??";

    console_printf("%s %d %d, %02d:%02d:%02d",
        month, (int)time.day, (int)time.year,
        (int)time.hour, (int)time.minute, (int)time.second);
}
