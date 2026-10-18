/*
 * ojjyOS v3 - Proportional UI text (see uifont.h)
 *
 * Pen positions advance in 1/64 px so spacing stays even; each glyph is
 * placed at the rounded pen position and its coverage is blended over the
 * surface (sub-pixel colour fringes avoided: grayscale anti-aliasing).
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "uifont.h"

static int density = 1;

void text_set_density(int d) { density = d == 2 ? 2 : 1; }

static const UiFontFace *face(TextStyle st)
{
    if ((unsigned)st >= TEXT_STYLE_COUNT) st = TEXT_BODY;
    return &uifont_faces[st][density - 1];
}

/* Decode one UTF-8 sequence; returns bytes consumed (>= 1). */
static int utf8_next(const char *s, int left, uint32_t *cp)
{
    const uint8_t *u = (const uint8_t *)s;
    if (u[0] < 0x80 || left < 2) { *cp = u[0] < 0x80 ? u[0] : '?'; return 1; }
    int len = u[0] >= 0xF0 ? 4 : u[0] >= 0xE0 ? 3 : u[0] >= 0xC0 ? 2 : 1;
    if (len > left) len = left;
    uint32_t v = len == 2 ? u[0] & 0x1F : len == 3 ? u[0] & 0x0F : u[0] & 0x07;
    for (int i = 1; i < len; i++) {
        if ((u[i] & 0xC0) != 0x80) { *cp = '?'; return i; }
        v = (v << 6) | (u[i] & 0x3F);
    }
    *cp = len == 1 ? '?' : v;
    return len;
}

static const UiGlyph *lookup(const UiFontFace *f, uint32_t cp)
{
    if (cp >= 32 && cp < 127 && f->count >= 95 && f->glyphs[cp - 32].cp == cp) return &f->glyphs[cp - 32];
    for (int i = 0; i < f->count; i++) if (f->glyphs[i].cp == cp) return &f->glyphs[i];
    if (cp == 0x2014 || cp == 0x2013) return lookup(f, '-');
    if (cp != '?') return lookup(f, '?');
    return f->count ? &f->glyphs[0] : 0;
}

static void blit_glyph(GfxSurface *s, const UiFontFace *f, const UiGlyph *g, int gx, int gy,
                       uint32_t color, uint8_t alpha)
{
    const uint8_t *bits = f->bits + g->off;
    int x0 = gx + g->bx, y0 = gy - g->top;
    for (int row = 0; row < g->h; row++) {
        int py = y0 + row;
        if (py < s->clip_y1 || py >= s->clip_y2) continue;
        uint32_t *dst = s->pixels + py * s->pitch;
        const uint8_t *src = bits + row * g->w;
        for (int col = 0; col < g->w; col++) {
            uint32_t cov = src[col];
            if (!cov) continue;
            int px = x0 + col;
            if (px < s->clip_x1 || px >= s->clip_x2) continue;
            if (alpha != 255) cov = cov * alpha / 255;
            dst[px] = cov >= 255 ? (color | 0xFF000000u) : gfx_blend(dst[px], color, cov);
        }
    }
}

int text_draw_n(GfxSurface *s, int x, int y, const char *str, int max_bytes, TextStyle st, uint32_t color, uint8_t alpha)
{
    if (!str) return 0;
    const UiFontFace *f = face(st);
    int len = 0;
    while (str[len] && (max_bytes < 0 || len < max_bytes)) len++;
    /* Callers use logical (1x) coordinates; HiDPI scales them here. */
    int base = (y * density) + f->ascent;
    int pen64 = x * density * 64;
    for (int i = 0; i < len;) {
        uint32_t cp;
        i += utf8_next(str + i, len - i, &cp);
        const UiGlyph *g = lookup(f, cp);
        if (!g) continue;
        if (s && g->w) blit_glyph(s, f, g, (pen64 + 32) >> 6, base, color & 0xFFFFFF, alpha);
        pen64 += g->adv64;
    }
    return ((pen64 + 32) >> 6) / density - x;
}

int text_draw(GfxSurface *s, int x, int y, const char *str, TextStyle st, uint32_t color)
{
    return text_draw_n(s, x, y, str, -1, st, color, 255);
}

int text_width_n(const char *str, int max_bytes, TextStyle st)
{
    return text_draw_n(0, 0, 0, str, max_bytes, st, 0, 0);
}

int text_width(const char *str, TextStyle st)
{
    return text_width_n(str, -1, st);
}

int text_draw_fit(GfxSurface *s, int x, int y, const char *str, TextStyle st, uint32_t color, int max_w)
{
    if (!str) return 0;
    if (text_width(str, st) <= max_w) return text_draw(s, x, y, str, st, color);
    static const char ellipsis[] = "\xE2\x80\xA6";
    int ew = text_width(ellipsis, st);
    int n = 0, len = 0;
    while (str[len]) len++;
    /* Longest prefix (on a character boundary) that fits with the ellipsis. */
    for (int i = 0; i < len;) {
        uint32_t cp;
        int step = utf8_next(str + i, len - i, &cp);
        if (text_width_n(str, i + step, st) + ew > max_w) break;
        i += step;
        n = i;
    }
    int w = text_draw_n(s, x, y, str, n, st, color, 255);
    return w + text_draw(s, x + w, y, ellipsis, st, color);
}

int text_line_height(TextStyle st)
{
    return face(st)->line / density;
}

int text_ascent(TextStyle st)
{
    return face(st)->ascent / density;
}

static const uint8_t style_px[TEXT_STYLE_COUNT] = { 11, 11, 13, 13, 13, 15, 20, 28, 72 };

int text_cap_height(TextStyle st)
{
    if ((unsigned)st >= TEXT_STYLE_COUNT) st = TEXT_BODY;
    return (style_px[st] * 727 + 500) / 1000;      /* Inter cap height = 0.727 em */
}

int text_y_center(int top, int h, TextStyle st)
{
    int cap = text_cap_height(st);
    return top + (h - cap + 1) / 2 - (text_ascent(st) - cap);
}
