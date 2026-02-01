/*
 * ojjyOS v3 Kernel - Real-Time Clock Driver Implementation
 *
 * Reads date/time from CMOS RTC via ports 0x70/0x71.
 */

#include "rtc.h"
#include "../serial.h"
#include "../console.h"
#include "../types.h"

/* CMOS ports */
#define CMOS_ADDRESS    0x70
#define CMOS_DATA       0x71

/* CMOS registers */
#define CMOS_REG_SECONDS    0x00
#define CMOS_REG_MINUTES    0x02
#define CMOS_REG_HOURS      0x04
#define CMOS_REG_WEEKDAY    0x06
#define CMOS_REG_DAY        0x07
#define CMOS_REG_MONTH      0x08
#define CMOS_REG_YEAR       0x09
#define CMOS_REG_CENTURY    0x32    /* May not be present */
#define CMOS_REG_STATUS_A   0x0A
#define CMOS_REG_STATUS_B   0x0B

/* Status B flags */
#define RTC_STATUS_B_24HR   0x02    /* 24-hour mode */
#define RTC_STATUS_B_BCD    0x04    /* BCD mode (0) or binary (1) */

/* Forward declarations */
static bool rtc_probe(Driver *drv);
static int rtc_init_driver(Driver *drv);

/* Driver operations */
static DriverOps rtc_ops = {
    .probe = rtc_probe,
    .init = rtc_init_driver,
};

/* Driver instance */
static Driver rtc_driver = {
    .name = "rtc",
    .description = "CMOS Real-Time Clock",
    .version = DRIVER_VERSION(1, 0, 0),
    .type = DRIVER_TYPE_CHAR,
    .ops = &rtc_ops,
};

/*
 * Read CMOS register
 */
static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

/*
 * Check if RTC update in progress
 */
static bool rtc_update_in_progress(void)
{
    return (cmos_read(CMOS_REG_STATUS_A) & 0x80) != 0;
}

/*
 * Convert BCD to binary
 */
static uint8_t bcd_to_binary(uint8_t bcd)
{
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/*
 * Probe for RTC
 */
static bool rtc_probe(Driver *drv)
{
    (void)drv;

    serial_printf("[RTC] Probing for RTC...\n");

    /* RTC is always present on PC-compatible systems */
    /* Just verify we can read status register */
    uint8_t status = cmos_read(CMOS_REG_STATUS_B);
    serial_printf("[RTC] Status B: 0x%x\n", status);

    return true;
}

/*
 * Initialize RTC driver
 */
static int rtc_init_driver(Driver *drv)
{
    (void)drv;

    serial_printf("[RTC] Initializing...\n");

    /* Read and display initial time */
    RtcTime time;
    rtc_read_time(&time);

    serial_printf("[RTC] Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
        time.year, time.month, time.day,
        time.hour, time.minute, time.second);

    return 0;
}

/*
 * Read current time
 */
void rtc_read_time(RtcTime *time)
{
    if (!time) return;

    uint8_t status_b = cmos_read(CMOS_REG_STATUS_B);
    bool is_24hr = (status_b & RTC_STATUS_B_24HR) != 0;
    bool is_binary = (status_b & RTC_STATUS_B_BCD) != 0;

    /* Wait for update to complete */
    while (rtc_update_in_progress()) {
        /* Busy wait */
    }

    /* Read all values */
    uint8_t second = cmos_read(CMOS_REG_SECONDS);
    uint8_t minute = cmos_read(CMOS_REG_MINUTES);
    uint8_t hour = cmos_read(CMOS_REG_HOURS);
    uint8_t day = cmos_read(CMOS_REG_DAY);
    uint8_t month = cmos_read(CMOS_REG_MONTH);
    uint8_t year = cmos_read(CMOS_REG_YEAR);
    uint8_t weekday = cmos_read(CMOS_REG_WEEKDAY);
    uint8_t century = cmos_read(CMOS_REG_CENTURY);

    /* Convert from BCD if necessary */
    if (!is_binary) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour = bcd_to_binary(hour & 0x7F) | (hour & 0x80);  /* Preserve PM flag */
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
        weekday = bcd_to_binary(weekday);
        if (century != 0) {
            century = bcd_to_binary(century);
        }
    }

    /* Convert 12-hour to 24-hour if needed */
    if (!is_24hr) {
        bool pm = (hour & 0x80) != 0;
        hour &= 0x7F;
        if (pm && hour != 12) {
            hour += 12;
        } else if (!pm && hour == 12) {
            hour = 0;
        }
    }

    /* Calculate full year */
    if (century != 0) {
        time->year = century * 100 + year;
    } else {
        /* Assume 2000s */
        time->year = 2000 + year;
    }

    time->month = month;
    time->day = day;
    time->hour = hour;
    time->minute = minute;
    time->second = second;
    time->weekday = weekday;
}

/*
 * Print current time
 */
void rtc_print_time(void)
{
    RtcTime time;
    rtc_read_time(&time);

    static const char *weekdays[] = {
        "???", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };

    const char *wday = (time.weekday >= 1 && time.weekday <= 7)
                       ? weekdays[time.weekday] : "???";

    console_printf("%s %04d-%02d-%02d %02d:%02d:%02d\n",
        wday, time.year, time.month, time.day,
        time.hour, time.minute, time.second);
}

/*
 * Get driver
 */
Driver *rtc_get_driver(void)
{
    return &rtc_driver;
}

/*
 * Initialize RTC (registers driver)
 */
void rtc_init(void)
{
    driver_register(&rtc_driver);
}
