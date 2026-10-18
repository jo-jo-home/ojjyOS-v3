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
static bool fb_back_valid, cursor_visible;
/* Clip rectangle for drawing primitives (defaults to the whole screen). */
static int clip_x1, clip_y1, clip_x2 = 1 << 30, clip_y2 = 1 << 30;

void fb_set_clip(int x, int y, int w, int h)
{
    clip_x1 = MAX(0, x); clip_y1 = MAX(0, y);
    clip_x2 = MIN((int)fb_width, x + w); clip_y2 = MIN((int)fb_height, y + h);
}

void fb_reset_clip(void)
{
    clip_x1 = clip_y1 = 0;
    clip_x2 = (int)fb_width; clip_y2 = (int)fb_height;
}

static void (*flush_hook)(int x, int y, int w, int h);

void fb_set_flush_hook(void (*hook)(int x, int y, int w, int h)) { flush_hook = hook; }
void fb_flush_all(void) { if (flush_hook) flush_hook(0, 0, (int)fb_width, (int)fb_height); }
bool fb_backbuffer_active(void) { return fb_back_active; }

void fb_get_surface(GfxSurface *s)
{
    s->pixels = fb_base;
    s->width = (int)fb_width; s->height = (int)fb_height; s->pitch = (int)fb_pitch;
    s->clip_x1 = MAX(0, clip_x1); s->clip_y1 = MAX(0, clip_y1);
    s->clip_x2 = MIN((int)fb_width, clip_x2); s->clip_y2 = MIN((int)fb_height, clip_y2);
}

static int cursor_x, cursor_y;
static uint64_t full_presents, cursor_updates;

static void cursor_draw(void)
{
    static const uint8_t mask[12] = {
        0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC,
        0xFE, 0xF0, 0xD8, 0x8C, 0x0C, 0x06
    };
    for (int row = 0; row < 12; row++) for (int col = 0; col < 8; col++) {
        if (!(mask[row] & (0x80 >> col))) continue;
        Color color = (row >= 1 && row < 11 && col >= 1 && col < 7 &&
                       (mask[row] & (0x80 >> (col - 1)))) ? COLOR_WHITE : COLOR_BLACK;
        for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
            int x = cursor_x + col * 2 + dx, y = cursor_y + row * 2 + dy;
            if (x >= 0 && y >= 0 && x < (int)fb_width && y < (int)fb_height)
                fb_screen[y * fb_screen_pitch + x] = color;
        }
    }
}

static void cursor_restore(void)
{
    for (int y = MAX(0, cursor_y); y < MIN((int)fb_height, cursor_y + 24); y++)
        for (int x = MAX(0, cursor_x); x < MIN((int)fb_width, cursor_x + 16); x++)
            fb_screen[y * fb_screen_pitch + x] = fb_back[y * fb_width + x];
}

void fb_cursor_move(int x, int y)
{
    if (!fb_back_active || !fb_screen || !fb_width || !fb_height) return;
    x = MAX(0, MIN(x, (int)fb_width - 1)); y = MAX(0, MIN(y, (int)fb_height - 1));
    if (cursor_visible && x == cursor_x && y == cursor_y) return;
    int old_x = cursor_x, old_y = cursor_y;
    bool was_visible = cursor_visible && fb_back_valid;
    if (was_visible) cursor_restore();
    cursor_x = x; cursor_y = y; cursor_visible = true;
    if (fb_back_valid) {
        cursor_draw(); cursor_updates++;
        if (flush_hook) {
            int x1 = was_visible ? MIN(old_x, x) : x, y1 = was_visible ? MIN(old_y, y) : y;
            int x2 = was_visible ? MAX(old_x, x) : x, y2 = was_visible ? MAX(old_y, y) : y;
            flush_hook(x1, y1, x2 - x1 + 16, y2 - y1 + 24);
        }
    }
}

void fb_get_present_stats(uint64_t *frames, uint64_t *updates)
{
    if (frames) *frames = full_presents;
    if (updates) *updates = cursor_updates;
}

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
    fb_back_valid = cursor_visible = false;
    full_presents = cursor_updates = 0;
    fb_reset_clip();
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
        if (!fb_back_active) { fb_back_valid = false; cursor_visible = false; }
        fb_base = fb_back;
        fb_pitch = fb_width;
        fb_back_active = true;
    } else {
        if (fb_back_active && fb_back_valid && cursor_visible) cursor_restore();
        cursor_visible = fb_back_valid = false;
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
        if (((uintptr_t)dst | (uintptr_t)src) & 7) {
            for (uint32_t x = 0; x < fb_width; x++)
                fb_screen[y * fb_screen_pitch + x] = fb_back[y * fb_width + x];
            continue;
        }
        uint32_t n = fb_width / 2;
        for (uint32_t i = 0; i < n; i++) {
            dst[i] = src[i];
        }
        if (fb_width & 1) {
            fb_screen[y * fb_screen_pitch + fb_width - 1] = fb_back[y * fb_width + fb_width - 1];
        }
    }
    fb_back_valid = true;
    full_presents++;
    if (cursor_visible) cursor_draw();
    fb_flush_all();
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
    if (x < clip_x1 || x >= clip_x2 || y < clip_y1 || y >= clip_y2 ||
        x >= (int)fb_width || y >= (int)fb_height) {
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
    int x1 = MAX(clip_x1, x);
    int y1 = MAX(clip_y1, y);
    int x2 = MIN(MIN((int)fb_width, clip_x2), x + w);
    int y2 = MIN(MIN((int)fb_height, clip_y2), y + h);

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
            if (fb_back_active) {
                /* Scene frames are fully repainted before text: blend into
                 * the real surface (glass, gradients) instead of boxing
                 * every glyph in a guessed background colour. */
                int px = x + col, py = y + row;
                if (cov == 0 || px < clip_x1 || py < clip_y1 || px >= clip_x2 || py >= clip_y2 ||
                    px >= (int)fb_width || py >= (int)fb_height) continue;
                Color *dst = &fb_back[py * fb_width + px];
                *dst = cov >= 255 ? fg : fb_blend(*dst, fg, cov);
                continue;
            }
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
        } else if ((uint8_t)*s >= 0x80) {
            /* The font is ASCII. Map UTF-8 dashes to '-' and draw any other
             * multibyte sequence as one '?' instead of one per byte. */
            const uint8_t *u = (const uint8_t *)s;
            int len = u[0] >= 0xF0 ? 4 : u[0] >= 0xE0 ? 3 : u[0] >= 0xC0 ? 2 : 1;
            for (int i = 1; i < len; i++) if ((u[i] & 0xC0) != 0x80) { len = i; break; }
            bool dash = len == 3 && u[0] == 0xE2 && u[1] == 0x80 && (u[2] == 0x93 || u[2] == 0x94);
            bool dot = (len == 3 && u[0] == 0xE2 && u[1] == 0x80 && u[2] == 0xA2) ||
                       (len == 2 && u[0] == 0xC2 && u[1] == 0xB7);
            fb_draw_char(cur_x, y, dash ? '-' : dot ? ' ' : '?', fg, bg);
            if (dot) { /* bullet / middle dot: small centred disc */
                fb_fill_rect(cur_x + 3, y + 7, 2, 3, fg);
                fb_fill_rect(cur_x + 2, y + 8, 4, 1, fg);
            }
            cur_x += FONT_WIDTH;
            s += len - 1;
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
 * Scaled (1x/2x) text with optional faux-bold, blended into the backbuffer
 * (or drawn over `bg` on the direct console path). Draws exactly n chars.
 */
void fb_draw_text_scaled(int x, int y, const char *s, int n, Color fg, int scale, bool bold)
{
    if (scale < 1) scale = 1;
    for (int i = 0; i < n; i++, x += FONT_WIDTH * scale) {
        if (x >= clip_x2 || x + FONT_WIDTH * scale + 1 < clip_x1) continue;
        const uint8_t *glyph = font_get_glyph_aa(s[i]);
        for (int row = 0; row < FONT_HEIGHT; row++) {
            for (int col = 0; col < FONT_WIDTH; col++) {
                uint8_t cov = glyph[row * FONT_WIDTH + col];
                if (!cov) continue;
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale + (bold ? 1 : 0); sx++) {
                        int px = x + col * scale + sx, py = y + row * scale + sy;
                        if (px < clip_x1 || py < clip_y1 || px >= clip_x2 || py >= clip_y2 ||
                            px >= (int)fb_width || py >= (int)fb_height) continue;
                        Color *dst = &fb_base[py * fb_pitch + px];
                        *dst = cov >= 255 ? fg : fb_blend(*dst, fg, cov);
                    }
                }
            }
        }
    }
}

/*
 * Blend two colors with alpha
 */
Color fb_blend(Color bg, Color fg, uint8_t alpha)
{
    return gfx_blend(bg, fg, alpha);    /* exact at 0 and 255, no divides */
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
