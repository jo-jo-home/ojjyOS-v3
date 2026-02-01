/*
 * ojjyOS v3 Kernel - Real-Time Clock Driver
 *
 * Reads date/time from CMOS RTC.
 */

#ifndef _OJJY_RTC_H
#define _OJJY_RTC_H

#include "../types.h"
#include "driver.h"

/* Time structure */
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;   /* 1=Sunday, 7=Saturday */
} RtcTime;

/* Get the RTC driver */
Driver *rtc_get_driver(void);

/* Initialize RTC (registers driver) */
void rtc_init(void);

/* Read current time */
void rtc_read_time(RtcTime *time);

/* Print current time */
void rtc_print_time(void);

#endif /* _OJJY_RTC_H */
