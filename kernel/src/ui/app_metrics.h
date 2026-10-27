/* Small app display models; no fabricated CPU utilization. Created by Jonas Lee. */
#ifndef OJJY_APP_METRICS_H
#define OJJY_APP_METRICS_H
#include "types.h"
#include "drivers/rtc.h"
#define APP_FRAME_SAMPLES 48
typedef struct { uint16_t ms[APP_FRAME_SAMPLES]; unsigned next, count; } AppFrameHistory;
void app_frame_record(AppFrameHistory *history, uint64_t ms);
unsigned app_frame_sample(const AppFrameHistory *history, unsigned oldest_index);
int app_bar_width(uint32_t used, uint32_t total, int width);
bool app_date_is_today(int year, int month, int day, const RtcTime *now);
#endif
