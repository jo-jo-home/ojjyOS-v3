/* Bounded frame history and date/display helpers. Created by Jonas Lee. */
#include "ui/app_metrics.h"
#include "string.h"
AppMemoryUsage app_memory_usage(PmmStats stats, bool high_ready)
{
    AppMemoryUsage usage = {0};
    usage.total = stats.total_bytes;
    usage.managed = MIN(stats.usable_bytes, usage.total);
    usage.reserved = usage.total - usage.managed;
    usage.free = MIN(stats.low_free_bytes, usage.managed);
    if (high_ready) usage.free += MIN(stats.high_free_bytes, usage.managed - usage.free);
    usage.used = usage.managed - usage.free;
    return usage;
}
void app_memory_label(uint64_t bytes, char label[32])
{
    uint64_t whole = bytes >> 30;
    uint64_t tenth = (((bytes & ((1ULL << 30) - 1)) * 10) + (1ULL << 29)) >> 30;
    if (tenth == 10) { whole++; tenth = 0; }
    if (bytes < (1ULL << 30)) {
        utoa(bytes >> 20, label, 10);
        strcpy(label + strlen(label), " MB");
        return;
    }
    utoa(whole, label, 10);
    if (tenth) {
        size_t length = strlen(label);
        label[length] = '.';
        label[length + 1] = (char)('0' + tenth);
        label[length + 2] = 0;
    }
    strcpy(label + strlen(label), " GB");
}
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
