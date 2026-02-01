/*
 * ojjyOS v3 Kernel - Diagnostics Screen Implementation
 *
 * Provides in-OS status display showing drivers, memory, cache, and input state.
 */

#include "diagnostics.h"
#include "driver.h"
#include "input.h"
#include "ata.h"
#include "rtc.h"
#include "block_cache.h"
#include "../console.h"
#include "../framebuffer.h"
#include "../timer.h"
#include "../memory.h"
#include "../serial.h"

/*
 * Show full diagnostics screen
 */
void diagnostics_show(void)
{
    console_printf("\n");
    console_printf("========================================\n");
    console_printf("   ojjyOS v3 System Diagnostics\n");
    console_printf("========================================\n\n");

    /* System info */
    console_printf("System:\n");
    console_printf("  Display: %dx%d\n", fb_get_width(), fb_get_height());
    console_printf("  Memory:  %d MB total, %d MB free\n",
        (int)(pmm_get_total_memory() / (1024 * 1024)),
        (int)(pmm_get_free_memory() / (1024 * 1024)));
    console_printf("  Uptime:  %d ms\n", (int)timer_get_ticks());
    console_printf("\n");

    /* RTC time */
    console_printf("Time: ");
    rtc_print_time();
    console_printf("\n");

    /* Driver status */
    driver_print_all();

    /* ATA devices */
    ata_print_devices();

    /* Block cache stats */
    block_cache_print_stats();

    /* Input status */
    int32_t mx, my;
    input_get_mouse_position(&mx, &my);
    console_printf("Input:\n");
    console_printf("  Mouse position: (%d, %d)\n", mx, my);
    console_printf("  Mouse buttons:  0x%x\n", input_get_mouse_buttons());
    console_printf("  Modifiers:      0x%x\n", input_get_modifiers());
    console_printf("  Events total:   %d\n", (int)input_get_event_count());
    console_printf("  Events dropped: %d\n", (int)input_get_dropped_count());
    console_printf("\n");
}

/*
 * Arrow cursor bitmap (16x16)
 */
static const uint8_t cursor_bitmap[16][16] = {
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,1,1,1,1,0,0,0,0,0},
    {1,2,2,1,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0},
};

/* Cursor colors */
static const Color cursor_colors[3] = {
    COLOR_BLACK,  /* 1 = Black outline */
    COLOR_WHITE,  /* 2 = White fill */
    0,            /* 0 = Transparent (not used) */
};

/* Cursor size */
#define CURSOR_WIDTH  16
#define CURSOR_HEIGHT 16

/* Background buffer for cursor restore */
static Color cursor_background[CURSOR_WIDTH * CURSOR_HEIGHT];
static int32_t prev_mx = -1;
static int32_t prev_my = -1;
static bool cursor_saved = false;

/*
 * Save background under cursor position
 */
static void save_cursor_background(int32_t x, int32_t y)
{
    int fb_width = fb_get_width();
    int fb_height = fb_get_height();

    for (int dy = 0; dy < CURSOR_HEIGHT; dy++) {
        for (int dx = 0; dx < CURSOR_WIDTH; dx++) {
            int px = x + dx;
            int py = y + dy;

            if (px >= 0 && px < fb_width && py >= 0 && py < fb_height) {
                cursor_background[dy * CURSOR_WIDTH + dx] = fb_get_pixel(px, py);
            } else {
                cursor_background[dy * CURSOR_WIDTH + dx] = 0;
            }
        }
    }
    cursor_saved = true;
}

/*
 * Restore background under previous cursor position
 */
static void restore_cursor_background(int32_t x, int32_t y)
{
    if (!cursor_saved) return;

    int fb_width = fb_get_width();
    int fb_height = fb_get_height();

    for (int dy = 0; dy < CURSOR_HEIGHT; dy++) {
        for (int dx = 0; dx < CURSOR_WIDTH; dx++) {
            int px = x + dx;
            int py = y + dy;

            if (px >= 0 && px < fb_width && py >= 0 && py < fb_height) {
                fb_put_pixel(px, py, cursor_background[dy * CURSOR_WIDTH + dx]);
            }
        }
    }
}

/*
 * Draw mouse cursor at position
 */
static void draw_mouse_cursor(int32_t x, int32_t y)
{
    int fb_width = fb_get_width();
    int fb_height = fb_get_height();

    for (int dy = 0; dy < CURSOR_HEIGHT; dy++) {
        for (int dx = 0; dx < CURSOR_WIDTH; dx++) {
            uint8_t pixel = cursor_bitmap[dy][dx];
            if (pixel == 0) continue;  /* Transparent */

            int px = x + dx;
            int py = y + dy;

            if (px >= 0 && px < fb_width && py >= 0 && py < fb_height) {
                fb_put_pixel(px, py, cursor_colors[pixel]);
            }
        }
    }
}

/*
 * Update live diagnostics (call from main loop)
 * Handles cursor rendering with proper background save/restore
 */
void diagnostics_update(void)
{
    int32_t mx, my;
    input_get_mouse_position(&mx, &my);

    /* Only redraw if position changed */
    if (mx != prev_mx || my != prev_my) {
        /* Restore old background */
        if (prev_mx >= 0 && prev_my >= 0) {
            restore_cursor_background(prev_mx, prev_my);
        }

        /* Save new background */
        save_cursor_background(mx, my);

        /* Draw new cursor */
        draw_mouse_cursor(mx, my);

        prev_mx = mx;
        prev_my = my;
    }
}

/*
 * Reset cursor state (e.g., after screen clear)
 */
void diagnostics_reset_cursor(void)
{
    prev_mx = -1;
    prev_my = -1;
    cursor_saved = false;
}
