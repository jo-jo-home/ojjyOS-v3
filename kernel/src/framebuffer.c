/*
 * ojjyOS v3 Kernel - Framebuffer Driver Implementation
 *
 * Provides basic 2D drawing operations on the UEFI GOP framebuffer.
 */

#include "framebuffer.h"
#include "font.h"
#include "string.h"

/* Framebuffer state: fb_base/fb_pitch describe the CURRENT draw target,
 * which is either the real screen or the off-screen back buffer. */
static uint32_t *fb_base = NULL;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;   /* In pixels, not bytes */

/* Real (visible) screen */
static uint32_t *fb_screen = NULL;
static uint32_t fb_screen_pitch = 0;

/* Off-screen back buffer (static: the kernel heap is far too small) */
#define FB_BACK_MAX_PIXELS  (1920 * 1200)
static uint32_t fb_back[FB_BACK_MAX_PIXELS];
static bool fb_back_active = false;

/*
 * Initialize framebuffer from boot info
 */
void fb_init(BootInfo *info)
{
    fb_screen = (uint32_t *)info->fb_addr;
    fb_width = info->fb_width;
    fb_height = info->fb_height;
    fb_screen_pitch = info->fb_pitch / 4;  /* Convert bytes to pixels */
    fb_base = fb_screen;
    fb_pitch = fb_screen_pitch;
    fb_back_active = false;
}

/*
 * Route drawing to the off-screen back buffer (true) or straight to the
 * screen (false). Returns false if the screen is too big for the buffer.
 */
bool fb_use_backbuffer(bool enable)
{
    if (enable) {
        if ((uint64_t)fb_width * fb_height > FB_BACK_MAX_PIXELS) {
            return false;
        }
        fb_base = fb_back;
        fb_pitch = fb_width;
        fb_back_active = true;
    } else {
        fb_base = fb_screen;
        fb_pitch = fb_screen_pitch;
        fb_back_active = false;
    }
    return true;
}

/*
 * Copy the finished back-buffer frame to the visible screen.
 */
void fb_present(void)
{
    if (!fb_back_active || !fb_screen) return;
    for (uint32_t y = 0; y < fb_height; y++) {
        uint64_t *dst = (uint64_t *)(fb_screen + y * fb_screen_pitch);
        const uint64_t *src = (const uint64_t *)(fb_back + y * fb_width);
        uint32_t n = fb_width / 2;
        for (uint32_t i = 0; i < n; i++) {
            dst[i] = src[i];
        }
        if (fb_width & 1) {
            fb_screen[y * fb_screen_pitch + fb_width - 1] = fb_back[y * fb_width + fb_width - 1];
        }
    }
}

/*
 * Copy a full-screen image (width*height pixels, tightly packed) into
 * the current draw target.
 */
void fb_blit_full(const uint32_t *src)
{
    for (uint32_t y = 0; y < fb_height; y++) {
        uint32_t *row = fb_base + y * fb_pitch;
        const uint32_t *srow = src + y * fb_width;
        for (uint32_t x = 0; x < fb_width; x++) {
            row[x] = srow[x];
        }
    }
}

/*
 * Get framebuffer dimensions
 */
uint32_t fb_get_width(void)
{
    return fb_width;
}

uint32_t fb_get_height(void)
{
    return fb_height;
}

/*
 * Clear entire framebuffer
 */
void fb_clear(Color color)
{
    for (uint32_t y = 0; y < fb_height; y++) {
        uint32_t *row = fb_base + y * fb_pitch;
        for (uint32_t x = 0; x < fb_width; x++) {
            row[x] = color;
        }
    }
}

/*
 * Put a single pixel
 */
void fb_put_pixel(int x, int y, Color color)
{
    if (x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height) {
        return;
    }
    fb_base[y * fb_pitch + x] = color;
}

/*
 * Get a single pixel
 */
Color fb_get_pixel(int x, int y)
{
    if (x < 0 || x >= (int)fb_width || y < 0 || y >= (int)fb_height) {
        return 0;
    }
    return fb_base[y * fb_pitch + x];
}

/*
 * Fill a rectangle
 */
void fb_fill_rect(int x, int y, int w, int h, Color color)
{
    /* Clip to screen bounds */
    int x1 = MAX(0, x);
    int y1 = MAX(0, y);
    int x2 = MIN((int)fb_width, x + w);
    int y2 = MIN((int)fb_height, y + h);

    for (int py = y1; py < y2; py++) {
        uint32_t *row = fb_base + py * fb_pitch;
        for (int px = x1; px < x2; px++) {
            row[px] = color;
        }
    }
}

/*
 * Draw rectangle outline
 */
void fb_draw_rect(int x, int y, int w, int h, Color color)
{
    /* Top */
    fb_fill_rect(x, y, w, 1, color);
    /* Bottom */
    fb_fill_rect(x, y + h - 1, w, 1, color);
    /* Left */
    fb_fill_rect(x, y, 1, h, color);
    /* Right */
    fb_fill_rect(x + w - 1, y, 1, h, color);
}

/*
 * Draw a character using the anti-aliased font:
 * each pixel of the 8x16 cell is blended toward the foreground by
 * its coverage value, which gives smooth, modern-looking text.
 */
void fb_draw_char(int x, int y, char c, Color fg, Color bg)
{
    const uint8_t *glyph = font_get_glyph_aa(c);

    for (int row = 0; row < FONT_HEIGHT; row++) {
        for (int col = 0; col < FONT_WIDTH; col++) {
            uint8_t cov = glyph[row * FONT_WIDTH + col];
            Color color;
            if (cov == 0) {
                color = bg;
            } else if (cov >= 255) {
                color = fg;
            } else {
                color = fb_blend(bg, fg, cov);
            }
            fb_put_pixel(x + col, y + row, color);
        }
    }
}

/*
 * Draw a string
 */
void fb_draw_string(int x, int y, const char *s, Color fg, Color bg)
{
    int cur_x = x;

    while (*s) {
        if (*s == '\n') {
            cur_x = x;
            y += FONT_HEIGHT;
        } else if (*s == '\t') {
            cur_x += FONT_WIDTH * 4;  /* 4-space tabs */
        } else {
            fb_draw_char(cur_x, y, *s, fg, bg);
            cur_x += FONT_WIDTH;
        }
        s++;

        /* Wrap at screen edge */
        if (cur_x + FONT_WIDTH > (int)fb_width) {
            cur_x = x;
            y += FONT_HEIGHT;
        }
    }
}

/*
 * Blend two colors with alpha
 */
Color fb_blend(Color bg, Color fg, uint8_t alpha)
{
    uint8_t bg_b = (bg >> 0) & 0xFF;
    uint8_t bg_g = (bg >> 8) & 0xFF;
    uint8_t bg_r = (bg >> 16) & 0xFF;

    uint8_t fg_b = (fg >> 0) & 0xFF;
    uint8_t fg_g = (fg >> 8) & 0xFF;
    uint8_t fg_r = (fg >> 16) & 0xFF;

    uint8_t out_r = ((fg_r * alpha) + (bg_r * (255 - alpha))) / 255;
    uint8_t out_g = ((fg_g * alpha) + (bg_g * (255 - alpha))) / 255;
    uint8_t out_b = ((fg_b * alpha) + (bg_b * (255 - alpha))) / 255;

    return RGB(out_r, out_g, out_b);
}

/*
 * Copy a rectangular region (for scrolling), clipped to the framebuffer
 */
void fb_copy_rect(int dst_x, int dst_y, int src_x, int src_y, int w, int h)
{
    /* Clip to screen bounds first */
    if (w <= 0 || h <= 0) return;
    if (dst_x < 0) { w += dst_x; src_x -= dst_x; dst_x = 0; }
    if (dst_y < 0) { h += dst_y; src_y -= dst_y; dst_y = 0; }
    if (dst_x + w > (int)fb_width) w = (int)fb_width - dst_x;
    if (dst_y + h > (int)fb_height) h = (int)fb_height - dst_y;
    if (w <= 0 || h <= 0) return;

    /* Handle overlapping regions by choosing copy direction */
    if (dst_y < src_y || (dst_y == src_y && dst_x < src_x)) {
        /* Copy top-to-bottom, left-to-right */
        for (int row = 0; row < h; row++) {
            uint32_t *dst_row = fb_base + (dst_y + row) * fb_pitch;
            uint32_t *src_row = fb_base + (src_y + row) * fb_pitch;
            for (int col = 0; col < w; col++) {
                dst_row[dst_x + col] = src_row[src_x + col];
            }
        }
    } else {
        /* Copy bottom-to-top, right-to-left */
        for (int row = h - 1; row >= 0; row--) {
            uint32_t *dst_row = fb_base + (dst_y + row) * fb_pitch;
            uint32_t *src_row = fb_base + (src_y + row) * fb_pitch;
            for (int col = w - 1; col >= 0; col--) {
                dst_row[dst_x + col] = src_row[src_x + col];
            }
        }
    }
}
