/*
 * ojjyOS v3 Kernel - Framebuffer Driver Implementation
 *
 * Provides basic 2D drawing operations on the UEFI GOP / virtio-gpu
 * framebuffer.
 *
 * HiDPI: the screen stores PHYSICAL pixels, but every public function takes
 * LOGICAL coordinates. On large displays (>= 2560x1440) the scale is 2:
 * one logical pixel is a 2x2 block, so the UI keeps its layout while shapes
 * (gfx.c), text (uifont.c, font2x) and the pointer render at full density.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "framebuffer.h"
#include "font.h"
#include "string.h"
#include "memory.h"
#include "cursor_image.h"

/* Framebuffer state (PHYSICAL pixels): fb_base/fb_pitch describe the
 * CURRENT draw target, either the real screen or the off-screen back buffer. */
static uint32_t *fb_base = NULL;
static uint32_t fb_width = 0;
static uint32_t fb_height = 0;
static uint32_t fb_pitch = 0;   /* In pixels, not bytes */
static int fb_scale = 1;        /* physical pixels per logical pixel */

/* Real (visible) screen */
static uint32_t *fb_screen = NULL;
static uint32_t fb_screen_pitch = 0;

/* Off-screen back buffer, allocated at the real screen size on first use
 * (never in BSS: the kernel image must fit below firmware memory). */
static uint32_t *fb_back;
static size_t fb_back_pixels;
static bool fb_back_active = false;
static bool fb_back_valid, cursor_visible;
/* Clip rectangle (physical) for drawing primitives. */
static int clip_x1, clip_y1, clip_x2 = 1 << 30, clip_y2 = 1 << 30;

#define S fb_scale

int fb_ui_scale(void) { return fb_scale; }
uint32_t fb_physical_width(void) { return fb_width; }
uint32_t fb_physical_height(void) { return fb_height; }

void fb_set_clip(int x, int y, int w, int h)
{
    clip_x1 = MAX(0, x * S); clip_y1 = MAX(0, y * S);
    clip_x2 = MIN((int)fb_width, (x + w) * S); clip_y2 = MIN((int)fb_height, (y + h) * S);
}

void fb_reset_clip(void)
{
    clip_x1 = clip_y1 = 0;
    clip_x2 = (int)fb_width; clip_y2 = (int)fb_height;
}

static void (*flush_hook)(int x, int y, int w, int h);

void fb_set_flush_hook(void (*hook)(int x, int y, int w, int h)) { flush_hook = hook; }
static void (*sync_hook)(void);
void fb_set_sync_hook(void (*hook)(void)) { sync_hook = hook; }
void fb_flush_all(void) { if (flush_hook) flush_hook(0, 0, (int)fb_width, (int)fb_height); }
bool fb_backbuffer_active(void) { return fb_back_active; }

void fb_redirect(uint32_t *target)
{
    if (!fb_back_active) return;
    fb_base = target ? target : fb_back;
    fb_pitch = fb_width;
}

uint32_t *fb_backbuffer(void) { return fb_back_active ? fb_back : NULL; }

void fb_get_surface(GfxSurface *s)
{
    s->pixels = fb_base;
    s->width = (int)fb_width; s->height = (int)fb_height; s->pitch = (int)fb_pitch;
    s->clip_x1 = MAX(0, clip_x1); s->clip_y1 = MAX(0, clip_y1);
    s->clip_x2 = MIN((int)fb_width, clip_x2); s->clip_y2 = MIN((int)fb_height, clip_y2);
    s->scale = fb_scale;
}

/* Pointer overlay, physical coordinates of the hotspot. */
static int cursor_x, cursor_y;
static uint64_t full_presents, cursor_updates;

/* Anti-aliased pointer image (tools/mkcursor.py), blended over the frame. */
#define CUR_W   (CURSOR_W * S)
#define CUR_H   (CURSOR_H * S)
#define CUR_X0  (cursor_x - CURSOR_HOT_X * S)
#define CUR_Y0  (cursor_y - CURSOR_HOT_Y * S)

static void cursor_draw(void)
{
    const uint8_t *image = S == 2 ? cursor_rgba2x : cursor_rgba;
    for (int row = 0; row < CUR_H; row++) {
        int y = CUR_Y0 + row;
        if (y < 0 || y >= (int)fb_height) continue;
        const uint8_t *src = image + row * CUR_W * 4;
        for (int col = 0; col < CUR_W; col++, src += 4) {
            int x = CUR_X0 + col;
            if (!src[3] || x < 0 || x >= (int)fb_width) continue;
            Color c = RGB(src[0], src[1], src[2]);
            fb_screen[y * fb_screen_pitch + x] = src[3] == 255 ? c : gfx_blend(fb_back[y * fb_width + x], c, src[3]);
        }
    }
}

static void cursor_restore(void)
{
    for (int y = MAX(0, CUR_Y0); y < MIN((int)fb_height, CUR_Y0 + CUR_H); y++)
        for (int x = MAX(0, CUR_X0); x < MIN((int)fb_width, CUR_X0 + CUR_W); x++)
            fb_screen[y * fb_screen_pitch + x] = fb_back[y * fb_width + x];
}

void fb_cursor_move(int x, int y)
{
    if (!fb_back_active || !fb_screen || !fb_width || !fb_height) return;
    x = MAX(0, MIN(x * S, (int)fb_width - 1)); y = MAX(0, MIN(y * S, (int)fb_height - 1));
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
            flush_hook(x1 - CURSOR_HOT_X * S, y1 - CURSOR_HOT_Y * S, x2 - x1 + CUR_W, y2 - y1 + CUR_H);
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
    fb_scale = (fb_width >= 2560 && fb_height >= 1440) ? 2 : 1;
    fb_base = fb_screen;
    fb_pitch = fb_screen_pitch;
    fb_back_active = false;
    fb_back_valid = cursor_visible = false;
    full_presents = cursor_updates = 0;
    fb_reset_clip();
}

/*
 * Route drawing to the off-screen back buffer (true) or straight to the
 * screen (false). Returns false if the back buffer cannot be allocated.
 */
bool fb_use_backbuffer(bool enable)
{
    if (enable) {
        size_t need = (size_t)fb_width * fb_height;
        if (!fb_back || fb_back_pixels < need) {
            uint32_t *buf = need ? (uint32_t *)pmm_alloc_bytes(need * sizeof(uint32_t)) : NULL;
            if (!buf) return false;
            fb_back = buf;
            fb_back_pixels = need;
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
    if (sync_hook) sync_hook();
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
 * Copy a full-screen image (PHYSICAL width*height pixels, tightly packed)
 * into the current draw target.
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
 * Logical framebuffer dimensions
 */
uint32_t fb_get_width(void)
{
    return fb_width / (uint32_t)S;
}

uint32_t fb_get_height(void)
{
    return fb_height / (uint32_t)S;
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

/* Physical-coordinate rectangle fill, clipped. */
static void fill_phys(int x1, int y1, int x2, int y2, Color color)
{
    x1 = MAX(x1, clip_x1); y1 = MAX(y1, clip_y1);
    x2 = MIN(x2, MIN((int)fb_width, clip_x2)); y2 = MIN(y2, MIN((int)fb_height, clip_y2));
    for (int py = y1; py < y2; py++) {
        uint32_t *row = fb_base + py * fb_pitch;
        for (int px = x1; px < x2; px++) row[px] = color;
    }
}

/*
 * Put a single (logical) pixel
 */
void fb_put_pixel(int x, int y, Color color)
{
    if (S == 1) {
        if (x < clip_x1 || x >= clip_x2 || y < clip_y1 || y >= clip_y2 ||
            x < 0 || y < 0 || x >= (int)fb_width || y >= (int)fb_height) {
            return;
        }
        fb_base[y * fb_pitch + x] = color;
        return;
    }
    fill_phys(x * S, y * S, x * S + S, y * S + S, color);
}

/*
 * Get a single (logical) pixel
 */
Color fb_get_pixel(int x, int y)
{
    x *= S; y *= S;
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
    fill_phys(x * S, y * S, (x + w) * S, (y + h) * S, color);
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
 * Draw one monospace glyph cell (8x16 logical) at physical (px0, py0):
 * the 2x font on HiDPI screens. In backbuffer mode coverage blends into
 * the scene; on the direct console path it is drawn over `bg`.
 */
static void draw_glyph_phys(int px0, int py0, char c, Color fg, Color bg, bool blend_into)
{
    const uint8_t *glyph = S == 2 ? font_get_glyph_aa2x(c) : font_get_glyph_aa(c);
    int gw = FONT_WIDTH * S, gh = FONT_HEIGHT * S;
    for (int row = 0; row < gh; row++) {
        int py = py0 + row;
        if (py < clip_y1 || py >= clip_y2 || py < 0 || py >= (int)fb_height) continue;
        uint32_t *dst_row = fb_base + py * fb_pitch;
        for (int col = 0; col < gw; col++) {
            int px = px0 + col;
            if (px < clip_x1 || px >= clip_x2 || px < 0 || px >= (int)fb_width) continue;
            uint8_t cov = glyph[row * gw + col];
            if (blend_into) {
                if (!cov) continue;
                dst_row[px] = cov >= 255 ? fg : fb_blend(dst_row[px], fg, cov);
            } else {
                dst_row[px] = cov == 0 ? bg : cov >= 255 ? fg : fb_blend(bg, fg, cov);
            }
        }
    }
}

/*
 * Draw a character using the anti-aliased font:
 * each pixel of the 8x16 cell is blended toward the foreground by
 * its coverage value, which gives smooth, modern-looking text.
 */
void fb_draw_char(int x, int y, char c, Color fg, Color bg)
{
    /* Scene frames are fully repainted before text: blend into the real
     * surface (glass, gradients) instead of boxing every glyph in a guessed
     * background colour. */
    draw_glyph_phys(x * S, y * S, c, fg, bg, fb_back_active);
}

/*
 * Draw a string
 */
void fb_draw_string(int x, int y, const char *s, Color fg, Color bg)
{
    int cur_x = x;
    int width = (int)fb_get_width();

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
        if (cur_x + FONT_WIDTH > width) {
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
    if (scale == 1 && !bold) {
        for (int i = 0; i < n; i++) draw_glyph_phys((x + i * FONT_WIDTH) * S, y * S, s[i], fg, fg, true);
        return;
    }
    /* Enlarged text: use the densest glyphs and replicate them. */
    int rep = scale * S / (S == 2 ? 2 : 1);
    int gw = FONT_WIDTH * (S == 2 ? 2 : 1), gh = FONT_HEIGHT * (S == 2 ? 2 : 1);
    for (int i = 0; i < n; i++) {
        int x0 = (x + i * FONT_WIDTH * scale) * S, y0 = y * S;
        if (x0 >= clip_x2 || x0 + gw * rep + S < clip_x1) continue;
        const uint8_t *glyph = S == 2 ? font_get_glyph_aa2x(s[i]) : font_get_glyph_aa(s[i]);
        for (int row = 0; row < gh; row++) {
            for (int col = 0; col < gw; col++) {
                uint8_t cov = glyph[row * gw + col];
                if (!cov) continue;
                for (int sy = 0; sy < rep; sy++) {
                    for (int sx = 0; sx < rep + (bold ? S : 0); sx++) {
                        int px = x0 + col * rep + sx, py = y0 + row * rep + sy;
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
    dst_x *= S; dst_y *= S; src_x *= S; src_y *= S; w *= S; h *= S;
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
