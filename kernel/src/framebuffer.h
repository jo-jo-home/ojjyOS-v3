/*
 * ojjyOS v3 Kernel - Framebuffer Driver
 *
 * Basic framebuffer operations: pixels, rectangles, text.
 */

#ifndef _OJJY_FRAMEBUFFER_H
#define _OJJY_FRAMEBUFFER_H

#include "types.h"
#include "boot_info.h"

/* Color type (BGRA format as provided by UEFI GOP) */
typedef uint32_t Color;

/* Create color from RGB */
#define RGB(r, g, b)    ((Color)(((b) & 0xFF) | (((g) & 0xFF) << 8) | (((r) & 0xFF) << 16) | 0xFF000000))
#define RGBA(r, g, b, a) ((Color)(((b) & 0xFF) | (((g) & 0xFF) << 8) | (((r) & 0xFF) << 16) | (((a) & 0xFF) << 24)))

/* Predefined colors - Tahoe palette */
#define COLOR_BLACK         RGB(0, 0, 0)
#define COLOR_WHITE         RGB(255, 255, 255)
#define COLOR_CREAM         RGB(245, 232, 208)      /* #F5E8D0 */
#define COLOR_SKY_BLUE      RGB(123, 188, 224)      /* #7BBCE0 */
#define COLOR_OCEAN         RGB(58, 157, 212)       /* #3A9DD4 */
#define COLOR_AZURE         RGB(46, 139, 200)       /* #2E8BC8 */
#define COLOR_ROYAL_BLUE    RGB(29, 90, 156)        /* #1D5A9C */
#define COLOR_DEEP_BLUE     RGB(24, 66, 120)        /* #184278 */
#define COLOR_SLATE         RGB(92, 139, 170)       /* #5C8BAA */
#define COLOR_DARK_SLATE    RGB(61, 106, 138)       /* #3D6A8A */

/* Dark mode colors */
#define COLOR_VOID          RGB(6, 6, 14)           /* #06060E */
#define COLOR_NIGHT_PURPLE  RGB(18, 16, 42)         /* #12102A */
#define COLOR_TWILIGHT      RGB(42, 30, 80)         /* #2A1E50 */
#define COLOR_DUSK          RGB(92, 56, 120)        /* #5C3878 */

/* UI colors */
#define COLOR_TEXT          RGB(26, 40, 56)         /* #1A2838 */
#define COLOR_TEXT_LIGHT    RGB(240, 244, 248)      /* #F0F4F8 */
#define COLOR_PANIC_BG      RGB(180, 40, 40)        /* Red for panic */
#define COLOR_PANIC_TEXT    RGB(255, 255, 255)

/* Initialize framebuffer from boot info */
void fb_init(BootInfo *info);

/* Get framebuffer dimensions */
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);

/* Basic drawing */
void fb_clear(Color color);
void fb_put_pixel(int x, int y, Color color);
Color fb_get_pixel(int x, int y);
void fb_fill_rect(int x, int y, int w, int h, Color color);
void fb_draw_rect(int x, int y, int w, int h, Color color);

/* Text drawing */
void fb_draw_char(int x, int y, char c, Color fg, Color bg);
void fb_draw_string(int x, int y, const char *s, Color fg, Color bg);

/* Alpha blending */
Color fb_blend(Color bg, Color fg, uint8_t alpha);

/* Copy region (for scrolling) */
void fb_copy_rect(int dst_x, int dst_y, int src_x, int src_y, int w, int h);

#endif /* _OJJY_FRAMEBUFFER_H */
