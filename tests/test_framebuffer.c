/* Cursor overlay/pitch/restore tests. Created by Jonas Lee. */
#include "framebuffer.h"
#include "cursor_image.h"
#include "test_support.h"
/* A solid white pixel of the pointer, relative to its hotspot. */
static int probe_dx, probe_dy;
static void find_probe(void)
{
    for (int y = 0; y < CURSOR_H; y++) for (int x = 0; x < CURSOR_W; x++) {
        const uint8_t *p = cursor_rgba + (y * CURSOR_W + x) * 4;
        if (p[0] == 255 && p[1] == 255 && p[2] == 255 && p[3] == 255) {
            probe_dx = x - CURSOR_HOT_X; probe_dy = y - CURSOR_HOT_Y; return;
        }
    }
}
#define AT(x, y) screen[((y) + probe_dy) * 55 + (x) + probe_dx]
const uint8_t *font_get_glyph_aa(char c) { (void)c; static uint8_t glyph[128]; return glyph; }
static Color screen[64 * 60];
int main(void)
{
    BootInfo boot = {0}; boot.fb_addr = (uintptr_t)screen;
    boot.fb_width = 51; boot.fb_height = 45; boot.fb_pitch = 55 * 4;
    for (unsigned i = 0; i < ARRAY_SIZE(screen); i++) screen[i] = 0x12345678;
    find_probe();
    CHECK(probe_dx >= 0 && probe_dy > 0 && probe_dx < 6 && probe_dy < 8, "pointer has a white body near its tip");
    fb_init(&boot);
    CHECK(fb_use_backbuffer(true), "backbuffer available");
    fb_clear(COLOR_OCEAN); fb_cursor_move(2, 3); fb_present();
    CHECK(AT(2, 3) == COLOR_WHITE, "cursor is on scanout");
    CHECK(fb_get_pixel(2, 3) == COLOR_OCEAN, "backbuffer remains cursor-free");
    fb_cursor_move(20, 5);
    CHECK(AT(2, 3) == COLOR_OCEAN && AT(20, 5) == COLOR_WHITE,
          "tiny move restores old cursor and draws new one");
    fb_fill_rect(0, 0, 51, 45, COLOR_ROYAL_BLUE); fb_present();
    CHECK(AT(20, 5) == COLOR_WHITE && fb_get_pixel(20 + probe_dx, 5 + probe_dy) == COLOR_ROYAL_BLUE,
          "full frame reapplies overlay without baking it into backbuffer");
    fb_cursor_move(21, 6); fb_cursor_move(50 - probe_dx, 44 - probe_dy);
    CHECK(screen[44 * 55 + 50] == COLOR_WHITE, "bottom/right clipping keeps last screen pixel usable");
    bool padding_ok = true;
    for (int y = 0; y < 45; y++) for (int x = 51; x < 55; x++)
        if (screen[y * 55 + x] != 0x12345678) padding_ok = false;
    CHECK(padding_ok && screen[45 * 55] == 0x12345678, "odd widths and padded scanout do not overwrite padding");
    uint64_t frames, updates, previous;
    fb_get_present_stats(&frames, &updates); previous = updates;
    fb_cursor_move(50 - probe_dx, 44 - probe_dy); fb_get_present_stats(&frames, &updates);
    CHECK(frames == 2 && updates == previous, "stationary pointer does no work and motion needs no full present");
    fb_use_backbuffer(false);
    CHECK(screen[44 * 55 + 50] == COLOR_ROYAL_BLUE, "console transition removes overlay");
    fb_cursor_move(1, 1);
    CHECK(screen[55 + 1] == COLOR_ROYAL_BLUE, "overlay disabled for direct drawing");
    fb_use_backbuffer(true); fb_clear(COLOR_OCEAN); fb_cursor_move(4, 4); fb_present();
    CHECK(AT(4, 4) == COLOR_WHITE, "UI re-entry restores cursor on new scene");
    return finish();
}
