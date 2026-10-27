/* APP-CAL-01 state/geometry regressions. Created by Jonas Lee. */
#include "ui/calculator.h"
#include "ui/app_metrics.h"
#include "string.h"
#include "test_support.h"

static void type(CalculatorState *s, const char *text)
{
    for (; *text; text++) calculator_press(s, calculator_key_for_char(*text), 100);
}

int main(void)
{
    CalculatorState s;
    calculator_reset(&s);
    CHECK(strcmp(s.display, "0") == 0 && s.pressed_key == -1 && !s.history[0], "reset all calculator fields");
    type(&s, "0.1+0.2=");
    CHECK(strcmp(s.display, "0.3") == 0, "decimal addition unchanged");
    CHECK(strcmp(s.history, "0.1 + 0.2 =") == 0, "equation history preserves actual operands");
    CHECK(s.pressed_key == 19 && !calculator_expire(&s, 279), "key feedback does not expire early");
    CHECK(calculator_expire(&s, 280) && s.pressed_key == -1, "key feedback expires once");
    CHECK(!calculator_expire(&s, 281), "idle feedback causes no further repaint");
    type(&s, "c2+3*4=");
    CHECK(strcmp(s.display, "20") == 0 && strcmp(s.history, "5 * 4 =") == 0, "left-to-right chain records the intermediate result");
    type(&s, "c1/0=");
    CHECK(s.has_error && strcmp(s.history, "1 / 0 =") == 0, "divide by zero retains explanatory history");
    type(&s, "7");
    CHECK(!s.has_error && strcmp(s.display, "7") == 0, "typing recovers from errors");
    type(&s, "c3n+2=");
    CHECK(strcmp(s.display, "-1") == 0, "negation works with history");
    type(&s, "c50%");
    CHECK(strcmp(s.display, "0.5") == 0, "percent keeps fixed-point precision");
    type(&s, "c12\b");
    CHECK(strcmp(s.display, "1") == 0, "Backspace works");
    type(&s, "c1..2");
    CHECK(strcmp(s.display, "1.2") == 0, "duplicate decimal ignored");
    type(&s, "c99999999999*99999999999=");
    CHECK(s.has_error && !s.op, "overflow fails safely without retaining pending operator");
    CHECK(!calculator_press(&s, -1, 0) && !calculator_press(&s, 20, 0), "invalid key indices rejected");
    CHECK(calculator_key_for_char('?') == -1 && calculator_key_for_char('\n') == 19, "keyboard map and Return");

    CalculatorLayout l;
    bool geometry_ok = true;
    for (int w = 140; w <= 1000; w += 43) for (int h = 240; h <= 800; h += 40) {
        if (!calculator_layout(w, h, &l)) geometry_ok = false;
        for (int r = 0; r < 5; r++) for (int c = 0; c < 4; c++) {
            int x = l.lcd_x + c * (l.key_w + 6), y = l.grid_y + r * (l.key_h + 6);
            if (calculator_hit(&l, x + l.key_w / 2, y + l.key_h / 2) != r * 4 + c ||
                x < 0 || y < 0 || x + l.key_w > w || y + l.key_h > h ||
                calculator_hit(&l, x + l.key_w, y) != -1) geometry_ok = false;
        }
    }
    CHECK(geometry_ok, "all twenty hit boxes match drawing and fit at every tested size");
    CHECK(!calculator_layout(100, 300, &l) && !calculator_layout(300, 100, &l), "too-small layouts rejected");
    AppFrameHistory history = {0};
    CHECK(app_frame_sample(&history, 0) == 0, "empty frame history");
    for (unsigned i = 0; i < 120; i++) app_frame_record(&history, i);
    bool samples_ok = history.count == APP_FRAME_SAMPLES;
    for (unsigned i = 0; i < APP_FRAME_SAMPLES; i++) if (app_frame_sample(&history, i) != 72 + i) samples_ok = false;
    CHECK(samples_ok, "frame history wraps in chronological order without growth");
    app_frame_record(&history, (uint64_t)-1);
    CHECK(app_frame_sample(&history, APP_FRAME_SAMPLES - 1) == 65535, "extreme frame time saturates");
    CHECK(app_frame_sample(&history, APP_FRAME_SAMPLES) == 0, "invalid frame index rejected");
    CHECK(app_bar_width(0, 100, 200) == 0 && app_bar_width(50, 100, 200) == 100, "memory bars show exact zero and half");
    CHECK(app_bar_width(101, 100, 200) == 200 && app_bar_width(1, 0, 200) == 0, "memory counters clamp safely");
    CHECK(app_bar_width(0xfffffffeU, 0xffffffffU, 2000000000) == 1999999999, "large memory ratio does not overflow");
    RtcTime now = { .year=2026, .month=10, .day=27 };
    CHECK(app_date_is_today(2026,10,27,&now), "today matches the full date");
    CHECK(!app_date_is_today(2026,9,27,&now) && !app_date_is_today(2025,10,27,&now), "same day in another month/year is not today");
    CHECK(!app_date_is_today(2026,10,26,&now) && !app_date_is_today(2026,10,27,0), "other day and missing clock not today");
    bool fuzz_ok = true;
    unsigned seed = 0x12345;
    calculator_reset(&s);
    for (int i = 0; i < 5000; i++) {
        seed = seed * 1664525U + 1013904223U;
        calculator_press(&s, (int)(seed % 24) - 2, (uint64_t)i * 50);
        if (strlen(s.display) >= sizeof(s.display) || strlen(s.history) >= sizeof(s.history) ||
            s.pressed_key < -1 || s.pressed_key >= 20) fuzz_ok = false;
    }
    CHECK(fuzz_ok, "5000 mixed valid/invalid key presses preserve bounded strings and state");
    return finish();
}
