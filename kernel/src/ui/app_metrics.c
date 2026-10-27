/* Bounded frame history and date/display helpers. Created by Jonas Lee. */
#include "ui/app_metrics.h"
void app_frame_record(AppFrameHistory *h, uint64_t ms)
{
    if (!h) return;
    h->ms[h->next] = (uint16_t)(ms > 65535 ? 65535 : ms);
    h->next = (h->next + 1) % APP_FRAME_SAMPLES;
    if (h->count < APP_FRAME_SAMPLES) h->count++;
}
unsigned app_frame_sample(const AppFrameHistory *h, unsigned index)
{
    if (!h || index >= h->count) return 0;
    unsigned first = (h->next + APP_FRAME_SAMPLES - h->count) % APP_FRAME_SAMPLES;
    return h->ms[(first + index) % APP_FRAME_SAMPLES];
}
int app_bar_width(uint32_t used, uint32_t total, int width)
{
    if (width <= 0 || total == 0) return 0;
    if (used >= total) return width;
    return (int)((uint64_t)used * (unsigned)width / total);
}
bool app_date_is_today(int year, int month, int day, const RtcTime *now)
{
    return now && year == now->year && month == now->month && day == now->day;
}
