/*
 * ojjyOS v3 - 2D graphics primitives (see gfx.h)
 *
 * Coverage model: a pixel's centre is (px + 0.5, py + 0.5). Corner arcs
 * are circles of radius r tangent to the rectangle's edges; a corner pixel
 * is covered by clamp(r + 0.5 - distance_to_arc_centre, 0, 1), computed in
 * 1/256 px fixed point. Straight edges are pixel-aligned (always full).
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "gfx.h"

uint32_t gfx_isqrt(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* 4x4 ordered dither thresholds (0..15) for gradients. */
static const uint8_t bayer4[4][4] = {
    { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
};

/* Coverage (0..256) of pixel (px,py) for the rounded rect, assuming the
 * pixel lies in one of the r x r corner squares. */
static inline uint32_t corner_cov(int px, int py, int x, int y, int w, int h, int r)
{
    int cx = px < x + r ? x + r : x + w - r;     /* arc centre (pixel-edge units) */
    int cy = py < y + r ? y + r : y + h - r;
    int dx = cx * 256 - (px * 256 + 128);
    int dy = cy * 256 - (py * 256 + 128);
    uint32_t d = gfx_isqrt((uint32_t)(dx * dx + dy * dy));
    int c = r * 256 + 128 - (int)d;
    return c <= 0 ? 0 : c >= 256 ? 256 : (uint32_t)c;
}

enum { MODE_SOLID, MODE_IMAGE, MODE_GRADIENT };

typedef struct {
    int mode;
    uint32_t color;          /* solid / current gradient row colour */
    uint32_t alpha;          /* 0..255 */
    const uint32_t *src;     /* image: same geometry as the surface */
    uint32_t top, bottom;    /* gradient ends */
} Paint;

static inline uint32_t paint_pixel(const GfxSurface *s, const Paint *p, int px, int py)
{
    if (p->mode == MODE_IMAGE) return p->src[py * s->width + px];
    return p->color;
}

/* Gradient row colour in 8.8 per channel, dithered per pixel. */
static inline uint32_t grad_pixel(uint32_t r16, uint32_t g16, uint32_t b16, int px, int py)
{
    uint32_t t = bayer4[py & 3][px & 3] * 16;       /* 0..240 of 256 */
    uint32_t r = (r16 + t) >> 8, g = (g16 + t) >> 8, b = (b16 + t) >> 8;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void fill_shape(GfxSurface *s, int x, int y, int w, int h, int r, Paint *p)
{
    if (w <= 0 || h <= 0 || !p->alpha) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r < 0) r = 0;
    int y1 = y < s->clip_y1 ? s->clip_y1 : y;
    int y2 = y + h > s->clip_y2 ? s->clip_y2 : y + h;
    int cx1 = x < s->clip_x1 ? s->clip_x1 : x;
    int cx2 = x + w > s->clip_x2 ? s->clip_x2 : x + w;
    if (cx1 >= cx2) return;
    int den = h > 1 ? h - 1 : 1;

    for (int py = y1; py < y2; py++) {
        uint32_t *row = s->pixels + py * s->pitch;
        uint32_t r16 = 0, g16 = 0, b16 = 0;
        if (p->mode == MODE_GRADIENT) {
            int t = ((py - y) * 256) / den;          /* 0..256 */
            #define CH(c, sh) ((uint32_t)((((c) >> (sh)) & 0xFF) * 256))
            r16 = (CH(p->top, 16) * (256 - t) + CH(p->bottom, 16) * t) >> 8;
            g16 = (CH(p->top, 8) * (256 - t) + CH(p->bottom, 8) * t) >> 8;
            b16 = (CH(p->top, 0) * (256 - t) + CH(p->bottom, 0) * t) >> 8;
            #undef CH
        }
        bool corner_row = r > 0 && (py < y + r || py >= y + h - r);
        int in1 = corner_row ? x + r : x, in2 = corner_row ? x + w - r : x + w;
        if (in1 < cx1) in1 = cx1;
        if (in2 > cx2) in2 = cx2;

        /* Edge pixels (corner squares) */
        if (corner_row) {
            for (int side = 0; side < 2; side++) {
                int a = side ? (x + w - r > cx1 ? x + w - r : cx1) : cx1;
                int b = side ? cx2 : (x + r < cx2 ? x + r : cx2);
                for (int px = a; px < b; px++) {
                    uint32_t cov = corner_cov(px, py, x, y, w, h, r);
                    if (!cov) continue;
                    uint32_t c = p->mode == MODE_GRADIENT ? grad_pixel(r16, g16, b16, px, py)
                                                          : paint_pixel(s, p, px, py);
                    row[px] = gfx_blend(row[px], c, (p->alpha * cov) >> 8);
                }
            }
        }
        /* Interior span */
        if (in1 >= in2) continue;
        if (p->mode == MODE_IMAGE) {
            const uint32_t *src = p->src + py * s->width;
            if (p->alpha >= 255) for (int px = in1; px < in2; px++) row[px] = src[px];
            else for (int px = in1; px < in2; px++) row[px] = gfx_blend(row[px], src[px], p->alpha);
        } else if (p->mode == MODE_GRADIENT) {
            for (int px = in1; px < in2; px++) {
                uint32_t c = grad_pixel(r16, g16, b16, px, py);
                row[px] = p->alpha >= 255 ? c : gfx_blend(row[px], c, p->alpha);
            }
        } else if (p->alpha >= 255) {
            for (int px = in1; px < in2; px++) row[px] = p->color;
        } else {
            /* Pre-multiplied solid blend: one multiply per channel pair. */
            uint32_t a = p->alpha + (p->alpha >> 7), ia = 256 - a;
            uint32_t frb = (p->color & 0xFF00FF) * a, fg = (p->color & 0x00FF00) * a;
            for (int px = in1; px < in2; px++) {
                uint32_t bg = row[px];
                uint32_t rb = ((bg & 0xFF00FF) * ia + frb) >> 8;
                uint32_t g = ((bg & 0x00FF00) * ia + fg) >> 8;
                row[px] = (rb & 0xFF00FF) | (g & 0x00FF00) | 0xFF000000u;
            }
        }
    }
}

#define SCALE_IN(s) int k_ = (s)->scale > 1 ? (s)->scale : 1

void gfx_round_rect(GfxSurface *s, int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha)
{
    SCALE_IN(s); x *= k_; y *= k_; w *= k_; h *= k_; r *= k_;
    Paint p = { MODE_SOLID, color & 0xFFFFFF, alpha, 0, 0, 0 };
    fill_shape(s, x, y, w, h, r, &p);
}

void gfx_round_rect_image(GfxSurface *s, int x, int y, int w, int h, int r, const uint32_t *src, uint8_t alpha)
{
    SCALE_IN(s); x *= k_; y *= k_; w *= k_; h *= k_; r *= k_;
    Paint p = { MODE_IMAGE, 0, alpha, src, 0, 0 };
    fill_shape(s, x, y, w, h, r, &p);
}

void gfx_round_rect_vgradient(GfxSurface *s, int x, int y, int w, int h, int r,
                              uint32_t top, uint32_t bottom, uint8_t alpha)
{
    SCALE_IN(s); x *= k_; y *= k_; w *= k_; h *= k_; r *= k_;
    Paint p = { MODE_GRADIENT, 0, alpha, 0, top, bottom };
    fill_shape(s, x, y, w, h, r, &p);
}

/* Signed distance (1/16 px; > 0 outside) from pixel centre to a rounded rect. */
static int rr_sdf16(int px, int py, int x, int y, int w, int h, int r)
{
    int pcx = px * 16 + 8, pcy = py * 16 + 8;
    int cx = x * 16 + w * 8, cy = y * 16 + h * 8;     /* centre, 1/16 px */
    int qx = (pcx > cx ? pcx - cx : cx - pcx) - (w * 8 - r * 16);
    int qy = (pcy > cy ? pcy - cy : cy - pcy) - (h * 8 - r * 16);
    int ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    int outside = (int)gfx_isqrt((uint32_t)(ox * ox + oy * oy));
    int inside = qx > qy ? qx : qy;
    if (inside > 0) inside = 0;
    return outside + inside - r * 16;
}

void gfx_shadow(GfxSurface *s, int x, int y, int w, int h, int r, int spread, int dy,
                uint32_t color, uint8_t alpha)
{
    SCALE_IN(s); x *= k_; y *= k_; w *= k_; h *= k_; r *= k_; spread *= k_; dy *= k_;
    if (spread <= 0 || w <= 0 || h <= 0) return;
    int sy = y + dy;
    int y1 = sy - spread, y2 = sy + h + spread;
    int x1 = x - spread, x2 = x + w + spread;
    if (y1 < s->clip_y1) y1 = s->clip_y1;
    if (y2 > s->clip_y2) y2 = s->clip_y2;
    if (x1 < s->clip_x1) x1 = s->clip_x1;
    if (x2 > s->clip_x2) x2 = s->clip_x2;
    int sp16 = spread * 16;
    for (int py = y1; py < y2; py++) {
        uint32_t *row = s->pixels + py * s->pitch;
        bool body_row = py >= y + r && py < y + h - r;   /* rows where the shape is a plain band */
        for (int px = x1; px < x2; px++) {
            if (body_row && px >= x && px < x + w) { px = x + w - 1; continue; }   /* covered by the shape */
            if (py >= y && py < y + h && px >= x && px < x + w &&
                rr_sdf16(px, py, x, y, w, h, r) < -8) continue;
            int d = rr_sdf16(px, py, x, sy, w, h, r);
            if (d >= sp16) continue;
            uint32_t a;
            if (d <= 0) a = alpha;
            else {
                uint32_t t = (uint32_t)(sp16 - d) * 256 / (uint32_t)sp16;    /* 1 at edge -> 0 */
                a = (alpha * ((t * t) >> 8)) >> 8;
            }
            if (a) row[px] = gfx_blend(row[px], color, a);
        }
    }
}

void gfx_round_rect_stroke(GfxSurface *s, int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha)
{
    SCALE_IN(s); x *= k_; y *= k_; w *= k_; h *= k_; r *= k_;
    if (w <= 2 || h <= 2) return;
    /* Straight runs */
    Paint p = { MODE_SOLID, color & 0xFFFFFF, alpha, 0, 0, 0 };
    fill_shape(s, x + r, y, w - 2 * r, 1, 0, &p);
    fill_shape(s, x + r, y + h - 1, w - 2 * r, 1, 0, &p);
    fill_shape(s, x, y + r, 1, h - 2 * r, 0, &p);
    fill_shape(s, x + w - 1, y + r, 1, h - 2 * r, 0, &p);
    if (r <= 0) return;
    /* Corner arcs: coverage of a 1 px ring = cov(outer) - cov(inner) */
    for (int corner = 0; corner < 4; corner++) {
        int ax = corner & 1 ? x + w - r : x, ay = corner & 2 ? y + h - r : y;
        for (int py = ay; py < ay + r; py++) {
            if (py < s->clip_y1 || py >= s->clip_y2) continue;
            for (int px = ax; px < ax + r; px++) {
                if (px < s->clip_x1 || px >= s->clip_x2) continue;
                uint32_t outer = corner_cov(px, py, x, y, w, h, r);
                uint32_t inner = r > 1 ? corner_cov(px, py, x + 1, y + 1, w - 2, h - 2, r - 1) : 0;
                if (outer <= inner) continue;
                uint32_t *d = s->pixels + py * s->pitch + px;
                *d = gfx_blend(*d, color, (alpha * (outer - inner)) >> 8);
            }
        }
    }
}

void gfx_blit_scaled(GfxSurface *s, const uint32_t *src, int src_pitch, int src_x, int src_y, int src_w, int src_h,
                     int x, int y, int w, int h, int r, uint8_t alpha)
{
    SCALE_IN(s); x *= k_; y *= k_; w *= k_; h *= k_; r *= k_;
    if (w <= 0 || h <= 0 || src_w <= 0 || src_h <= 0 || !alpha) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    int y1 = y < s->clip_y1 ? s->clip_y1 : y, y2 = y + h > s->clip_y2 ? s->clip_y2 : y + h;
    int x1 = x < s->clip_x1 ? s->clip_x1 : x, x2 = x + w > s->clip_x2 ? s->clip_x2 : x + w;
    bool same = w == src_w && h == src_h;
    for (int py = y1; py < y2; py++) {
        uint32_t *row = s->pixels + py * s->pitch;
        bool corner_row = py < y + r || py >= y + h - r;
        int fy = same ? 0 : (int)(((int64_t)((py - y) * 2 + 1) * src_h * 128) / h) - 128;
        if (fy < 0) fy = 0;
        int iy = same ? py - y : fy >> 8, ty = fy & 255;
        if (iy >= src_h) iy = src_h - 1;
        int iy2 = iy + 1 < src_h ? iy + 1 : src_h - 1;
        const uint32_t *r0 = src + (size_t)(src_y + iy) * (size_t)src_pitch + src_x;
        const uint32_t *r1 = src + (size_t)(src_y + iy2) * (size_t)src_pitch + src_x;
        for (int px = x1; px < x2; px++) {
            uint32_t cov = 256;
            if (corner_row && (px < x + r || px >= x + w - r)) {
                cov = corner_cov(px, py, x, y, w, h, r);
                if (!cov) continue;
            }
            uint32_t c;
            if (same) {
                c = r0[px - x];
            } else {
                int fx = (int)(((int64_t)((px - x) * 2 + 1) * src_w * 128) / w) - 128;
                if (fx < 0) fx = 0;
                int ix = fx >> 8, tx = fx & 255;
                if (ix >= src_w) ix = src_w - 1;
                int ix2 = ix + 1 < src_w ? ix + 1 : src_w - 1;
                uint32_t top = gfx_blend(r0[ix], r0[ix2], (uint32_t)tx * 255 / 256);
                uint32_t bot = gfx_blend(r1[ix], r1[ix2], (uint32_t)tx * 255 / 256);
                c = gfx_blend(top, bot, (uint32_t)ty * 255 / 256);
            }
            uint32_t a = (alpha * cov) >> 8;
            row[px] = a >= 255 ? (c | 0xFF000000u) : gfx_blend(row[px], c, a);
        }
    }
}

void gfx_arc_up(GfxSurface *s, int cx, int cy, int r_in, int r_out, int half_angle_deg,
                uint32_t color, uint8_t alpha)
{
    SCALE_IN(s); cx *= k_; cy *= k_; r_in *= k_; r_out *= k_;
    /* cos(half angle) in 1/1024 for the direction test (dy/len >= cos). */
    static const int16_t cos_tab[19] = { 1024, 1020, 1008, 989, 962, 928, 887, 839, 785, 724,
                                         658, 587, 512, 433, 350, 265, 178, 89, 0 };
    int a = half_angle_deg < 0 ? 0 : half_angle_deg > 180 ? 180 : half_angle_deg;
    int cosv = a >= 90 ? -cos_tab[(180 - a) / 5] : cos_tab[a / 5];
    for (int py = cy - r_out - 1; py <= cy + r_out + 1; py++) {
        if (py < s->clip_y1 || py >= s->clip_y2) continue;
        uint32_t *row = s->pixels + py * s->pitch;
        for (int px = cx - r_out - 1; px <= cx + r_out + 1; px++) {
            if (px < s->clip_x1 || px >= s->clip_x2) continue;
            int dx = px * 16 + 8 - cx * 16, dy = cy * 16 - (py * 16 + 8);      /* 1/16 px, y up */
            int d = (int)gfx_isqrt((uint32_t)(dx * dx + dy * dy));
            if (d == 0 || dy * 1024 < cosv * d) continue;
            int c_out = r_out * 16 + 8 - d, c_in = d - (r_in * 16 - 8);
            int c = c_out < c_in ? c_out : c_in;
            if (c <= 0) continue;
            uint32_t cov = c >= 16 ? 256 : (uint32_t)c * 16;
            row[px] = gfx_blend(row[px], color, (alpha * cov) >> 8);
        }
    }
}

void gfx_draw_rgba_scaled(GfxSurface *s, const uint8_t *rgba, int src_w, int src_h,
                          int x, int y, int w, int h, uint8_t alpha)
{
    SCALE_IN(s); x *= k_; y *= k_; w *= k_; h *= k_;
    if (w <= 0 || h <= 0 || src_w <= 0 || src_h <= 0 || !alpha) return;
    int y1 = y < s->clip_y1 ? s->clip_y1 : y, y2 = y + h > s->clip_y2 ? s->clip_y2 : y + h;
    int x1 = x < s->clip_x1 ? s->clip_x1 : x, x2 = x + w > s->clip_x2 ? s->clip_x2 : x + w;
    bool shrink = src_w > w || src_h > h;
    for (int py = y1; py < y2; py++) {
        uint32_t *row = s->pixels + py * s->pitch;
        for (int px = x1; px < x2; px++) {
            uint32_t ar = 0, ag = 0, ab = 0, aa = 0, n = 0;
            if (shrink) {
                /* Area average of the source box under this pixel. */
                int sx0 = (px - x) * src_w / w, sx1 = (px - x + 1) * src_w / w;
                int sy0 = (py - y) * src_h / h, sy1 = (py - y + 1) * src_h / h;
                if (sx1 <= sx0) sx1 = sx0 + 1;
                if (sy1 <= sy0) sy1 = sy0 + 1;
                for (int yy = sy0; yy < sy1 && yy < src_h; yy++) {
                    const uint8_t *sp = rgba + ((size_t)yy * src_w + sx0) * 4;
                    for (int xx = sx0; xx < sx1 && xx < src_w; xx++, sp += 4) {
                        uint32_t a = sp[3];
                        ar += sp[0] * a; ag += sp[1] * a; ab += sp[2] * a; aa += a; n++;
                    }
                }
            } else {
                /* Bilinear, premultiplied (no dark fringes). 8.8 source coords. */
                int fx = ((px - x) * 2 + 1) * src_w * 128 / w - 128;
                int fy = ((py - y) * 2 + 1) * src_h * 128 / h - 128;
                if (fx < 0) fx = 0;
                if (fy < 0) fy = 0;
                int ix = fx >> 8, iy = fy >> 8, tx = fx & 255, ty = fy & 255;
                for (int k = 0; k < 4; k++) {
                    int sx = ix + (k & 1), syy = iy + (k >> 1);
                    if (sx >= src_w) sx = src_w - 1;
                    if (syy >= src_h) syy = src_h - 1;
                    uint32_t wgt = (uint32_t)((k & 1 ? tx : 256 - tx) * (k >> 1 ? ty : 256 - ty));   /* /65536 */
                    const uint8_t *sp = rgba + ((size_t)syy * src_w + sx) * 4;
                    uint32_t a = sp[3] * wgt >> 8;            /* alpha * weight, /256 */
                    ar += sp[0] * a; ag += sp[1] * a; ab += sp[2] * a; aa += a;
                }
                n = 256;
            }
            if (!aa || !n) continue;
            uint32_t r = ar / aa, g = ag / aa, b = ab / aa;
            uint32_t cover = (aa + n / 2) / n;                /* 0..255, rounded */
            if (cover > 255) cover = 255;
            cover = cover * alpha / 255;
            if (!cover) continue;
            row[px] = gfx_blend(row[px], (r << 16) | (g << 8) | b, cover);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Liquid Glass                                                        */
/* ------------------------------------------------------------------ */
static uint32_t *glass_scratch;
static size_t glass_scratch_px;
static uint32_t glass_line[4096];

void gfx_set_scratch(uint32_t *pixels, size_t count) { glass_scratch = pixels; glass_scratch_px = count; }

/* One box-blur pass over n pixels spaced `stride` apart (in place). */
static void glass_blur_line(uint32_t *p, int n, int stride, int radius)
{
    if (n <= 1 || radius <= 0 || n > 4096) return;
    for (int i = 0; i < n; i++) glass_line[i] = p[i * stride];
    int win = radius * 2 + 1;
    uint32_t sr = 0, sg = 0, sb = 0;
    for (int k = -radius; k <= radius; k++) {
        uint32_t c = glass_line[k < 0 ? 0 : k >= n ? n - 1 : k];
        sr += (c >> 16) & 255; sg += (c >> 8) & 255; sb += c & 255;
    }
    for (int i = 0; i < n; i++) {
        p[i * stride] = 0xFF000000u | ((sr / win) << 16) | ((sg / win) << 8) | (sb / win);
        int o = i - radius, a = i + radius + 1;
        uint32_t co = glass_line[o < 0 ? 0 : o], ca = glass_line[a >= n ? n - 1 : a];
        sr += ((ca >> 16) & 255) - ((co >> 16) & 255);
        sg += ((ca >> 8) & 255) - ((co >> 8) & 255);
        sb += (ca & 255) - (co & 255);
    }
}

static uint32_t saturate_px(uint32_t c, int boost)
{
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    int l = (r * 77 + g * 150 + b * 29) >> 8;
    r = l + ((r - l) * (256 + boost) >> 8);
    g = l + ((g - l) * (256 + boost) >> 8);
    b = l + ((b - l) * (256 + boost) >> 8);
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return 0xFF000000u | (uint32_t)(r << 16 | g << 8 | b);
}

void gfx_liquid_glass(GfxSurface *s, int x, int y, int w, int h, int r, const GfxGlass *g)
{
    if (!g || !g->alpha || w <= 0 || h <= 0) return;
    SCALE_IN(s);
    int blur = g->blur * k_, refract = g->refract * k_, band = g->band * k_ ? g->band * k_ : 1;
    x *= k_; y *= k_; w *= k_; h *= k_; r *= k_;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    /* Backdrop box (surface bounds; refraction reads up to `refract` in). */
    int m = blur + 2;
    int bx0 = x - m < 0 ? 0 : x - m, by0 = y - m < 0 ? 0 : y - m;
    int bx1 = x + w + m > s->width ? s->width : x + w + m, by1 = y + h + m > s->height ? s->height : y + h + m;
    int bw = bx1 - bx0, bh = by1 - by0;
    if (bw <= 0 || bh <= 0) return;
    if (!glass_scratch || (size_t)bw * (size_t)bh > glass_scratch_px || bw > 4096 || bh > 4096) {
        /* No scratch: plain tinted fill keeps the UI usable. */
        Paint p = { MODE_SOLID, g->tint & 0xFFFFFF, (uint32_t)(g->tint_alpha + 40 > 255 ? 255 : g->tint_alpha + 40) * g->alpha / 255, 0, 0, 0 };
        fill_shape(s, x, y, w, h, r, &p);
        return;
    }
    /* The blurred backdrop is smooth, so it is built at half resolution
     * (2x2 average) whenever there is real blur: ~4x less work per frame. */
    int sh = blur >= 4 ? 1 : 0;
    int aw = (bw + sh) >> sh, ah = (bh + sh) >> sh;
    uint32_t *A = glass_scratch;
    for (int yy = 0; yy < ah; yy++) {
        const uint32_t *s0 = s->pixels + (size_t)(by0 + (yy << sh)) * s->pitch + bx0;
        const uint32_t *s1 = sh && (yy << sh) + 1 < bh ? s0 + s->pitch : s0;
        for (int xx = 0; xx < aw; xx++) {
            int x0 = xx << sh, x1 = sh && x0 + 1 < bw ? x0 + 1 : x0;
            if (!sh) { A[(size_t)yy * aw + xx] = s0[x0]; continue; }
            uint32_t a = s0[x0], b = s0[x1], c = s1[x0], d = s1[x1];
            uint32_t rb = ((a & 0xFF00FF) + (b & 0xFF00FF) + (c & 0xFF00FF) + (d & 0xFF00FF)) >> 2;
            uint32_t g2 = ((a & 0x00FF00) + (b & 0x00FF00) + (c & 0x00FF00) + (d & 0x00FF00)) >> 2;
            A[(size_t)yy * aw + xx] = 0xFF000000u | (rb & 0xFF00FF) | (g2 & 0x00FF00);
        }
    }
    if (blur > 0) {
        int br = ((blur >> sh) / 2) + 1;
        for (int pass = 0; pass < 2; pass++) {
            for (int yy = 0; yy < ah; yy++) glass_blur_line(A + (size_t)yy * aw, aw, 1, br);
            for (int xx = 0; xx < aw; xx++) glass_blur_line(A + xx, ah, aw, br);
        }
    }

    int x1 = x < s->clip_x1 ? s->clip_x1 : x, x2 = x + w > s->clip_x2 ? s->clip_x2 : x + w;
    int y1 = y < s->clip_y1 ? s->clip_y1 : y, y2 = y + h > s->clip_y2 ? s->clip_y2 : y + h;
    int cx16 = x * 16 + w * 8, cy16 = y * 16 + h * 8;           /* centre, 1/16 px */
    int hw16 = w * 8 - r * 16, hh16 = h * 8 - r * 16;           /* inner box half sizes */
    int band16 = band * 16, rim16 = (k_ * 3) * 16;              /* specular rim ~3 logical px */
    uint32_t ta = g->tint_alpha, tint = g->tint & 0xFFFFFF;

    /* Deep interior (beyond corners, refraction band and rim): no edge
     * maths at all, just backdrop + saturation + tint. */
    int edge = band > r ? band : r;
    if (k_ * 3 > edge) edge = k_ * 3;
    edge += 1;
    for (int py = y1; py < y2; py++) {
        uint32_t *row = s->pixels + (size_t)py * s->pitch;
        bool mid_row = py >= y + edge && py < y + h - edge;
        int ix1 = x + edge, ix2 = x + w - edge;
        for (int px = x1; px < x2; px++) {
            if (mid_row && px >= ix1 && px < ix2) {
                uint32_t c = A[(size_t)((py - by0) >> sh) * aw + ((px - bx0) >> sh)];
                if (g->saturate) c = saturate_px(c, g->saturate);
                c = gfx_blend(c, tint, ta);
                row[px] = g->alpha >= 255 ? c : gfx_blend(row[px], c, g->alpha);
                continue;
            }
            int d = rr_sdf16(px, py, x, y, w, h, r);          /* > 0 outside */
            if (d >= 8) continue;
            uint32_t cov = d <= -8 ? 256 : (uint32_t)(8 - d) * 16;
            int t16 = -d < 0 ? 0 : -d;                         /* depth inside, 1/16 px */

            /* Outward normal (n16x, n16y), unit length 16. */
            int pcx = px * 16 + 8 - cx16, pcy = py * 16 + 8 - cy16;
            int qx = (pcx < 0 ? -pcx : pcx) - hw16, qy = (pcy < 0 ? -pcy : pcy) - hh16;
            int nx = 0, ny = 0;
            if (qx > 0 && qy > 0) {
                int len = (int)gfx_isqrt((uint32_t)(qx * qx + qy * qy));
                if (len < 1) len = 1;
                nx = qx * 16 / len; ny = qy * 16 / len;
            } else if (qx > qy) nx = 16; else ny = 16;
            if (pcx < 0) nx = -nx;
            if (pcy < 0) ny = -ny;

            /* Refraction: near the rim, sample further inside. */
            int sxp = px, syp = py;
            if (t16 < band16 && refract) {
                int f = band16 - t16;                          /* 0..band16 */
                int disp = refract * f / band16 * f / band16;  /* (1 - t/band)^2 */
                sxp -= nx * disp / 16;
                syp -= ny * disp / 16;
            }
            sxp = sxp < bx0 ? bx0 : sxp >= bx1 ? bx1 - 1 : sxp;
            syp = syp < by0 ? by0 : syp >= by1 ? by1 - 1 : syp;
            uint32_t c = A[(size_t)((syp - by0) >> sh) * aw + ((sxp - bx0) >> sh)];
            if (g->saturate) c = saturate_px(c, g->saturate);
            c = gfx_blend(c, tint, ta);

            /* Specular rim: light from above (strong top, faint bottom). */
            if (t16 < rim16 && g->specular) {
                int up = -ny;                                  /* +16 on the top edge */
                int lit = up > 0 ? 6 + up * 10 / 16 : 6 + (-up) * 3 / 16;   /* 6..16 */
                uint32_t a = (uint32_t)g->specular * (uint32_t)(rim16 - t16) / (uint32_t)rim16 * (uint32_t)lit / 16;
                c = gfx_blend(c, 0xFFFFFF, a > 255 ? 255 : a);
            }
            uint32_t a = (uint32_t)g->alpha * cov >> 8;
            row[px] = a >= 255 ? c : gfx_blend(row[px], c, a);
        }
    }
}

void gfx_round_rect_rim(GfxSurface *s, int x, int y, int w, int h, int r, uint8_t strength)
{
    if (!strength || w <= 0 || h <= 0) return;
    SCALE_IN(s); x *= k_; y *= k_; w *= k_; h *= k_; r *= k_;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    int x1 = x < s->clip_x1 ? s->clip_x1 : x, x2 = x + w > s->clip_x2 ? s->clip_x2 : x + w;
    int y1 = y < s->clip_y1 ? s->clip_y1 : y, y2 = y + h > s->clip_y2 ? s->clip_y2 : y + h;
    int cx16 = x * 16 + w * 8, cy16 = y * 16 + h * 8;
    int hw16 = w * 8 - r * 16, hh16 = h * 8 - r * 16;
    int rim16 = (k_ * 3) * 16;              /* band width ~3 logical px */
    int band = (rim16 + 15) / 16;           /* physical px */
    for (int py = y1; py < y2; py++) {
        uint32_t *row = s->pixels + (size_t)py * s->pitch;
        int dy = py - y < (y + h - 1) - py ? py - y : (y + h - 1) - py;
        bool horiz = dy < band;             /* within the straight top/bottom rim */
        bool corners = py < y + r || py >= y + h - r;
        /* Rows outside the horizontal rim only need the side segments;
         * corner rows need wider segments for the arcs. */
        int segw = corners ? r + band : band;
        for (int pass = 0; pass < (horiz ? 1 : 2); pass++) {
            int px1 = horiz ? x1 : (pass == 0 ? x1 : x + w - segw);
            int px2 = horiz ? x2 : (pass == 0 ? x + segw : x2);
            if (pass == 1 && x + segw >= x2) continue;   /* pass 0 covered the row */
            if (px2 <= px1) continue;
            if (px1 < x1) px1 = x1;
            if (px2 > x2) px2 = x2;
            for (int px = px1; px < px2; px++) {
            int d = rr_sdf16(px, py, x, y, w, h, r);    /* > 0 outside */
            if (d >= 8) continue;
            uint32_t cov = d <= -8 ? 256 : (uint32_t)(8 - d) * 16;
            int t16 = -d < 0 ? 0 : -d;                  /* depth inside, 1/16 px */
            if (t16 >= rim16) continue;
            int pcx = px * 16 + 8 - cx16, pcy = py * 16 + 8 - cy16;
            int qx = (pcx < 0 ? -pcx : pcx) - hw16, qy = (pcy < 0 ? -pcy : pcy) - hh16;
            int ny = 0;
            if (qx > 0 && qy > 0) {
                int len = (int)gfx_isqrt((uint32_t)(qx * qx + qy * qy));
                if (len < 1) len = 1;
                ny = qy * 16 / len;
            } else if (qy > 0) {
                ny = 16;
            }
            if (pcy < 0) ny = -ny;
            int up = -ny;                                /* +16 on the top edge */
            int lit = up > 0 ? 6 + up * 10 / 16 : 6 + (-up) * 3 / 16;
            uint32_t a = (uint32_t)strength * (uint32_t)(rim16 - t16) / (uint32_t)rim16
                       * (uint32_t)lit / 16 * cov >> 8;
            if (a) row[px] = gfx_blend(row[px], 0xFFFFFF, a > 255 ? 255 : a);
            }
        }
    }
}

void gfx_sprite_extract(const uint32_t *on_black, const uint32_t *on_white, uint32_t *argb, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint32_t b = on_black[i], w = on_white[i];
        /* Each channel: black = a*c, white = a*c + (1-a)*255. Use the
         * channel with the clearest signal (largest white-black gap is the
         * same for all channels in exact arithmetic; average for rounding). */
        int gap = (int)((w >> 16) & 255) - (int)((b >> 16) & 255)
                + (int)((w >> 8) & 255) - (int)((b >> 8) & 255)
                + (int)(w & 255) - (int)(b & 255);
        int a = 255 - (gap + 1) / 3;
        if (a <= 0) { argb[i] = 0; continue; }
        if (a > 255) a = 255;
        uint32_t out = (uint32_t)a << 24;
        for (int sh = 16; sh >= 0; sh -= 8) {
            int c = (int)((b >> sh) & 255) * 255 / a;   /* un-premultiply */
            if (c > 255) c = 255;
            out |= (uint32_t)c << sh;
        }
        argb[i] = out;
    }
}

void gfx_blit_argb(GfxSurface *s, const uint32_t *argb, int sw, int sh, int px, int py)
{
    int x1 = px < s->clip_x1 ? s->clip_x1 : px, x2 = px + sw > s->clip_x2 ? s->clip_x2 : px + sw;
    int y1 = py < s->clip_y1 ? s->clip_y1 : py, y2 = py + sh > s->clip_y2 ? s->clip_y2 : py + sh;
    for (int y = y1; y < y2; y++) {
        uint32_t *row = s->pixels + (size_t)y * s->pitch;
        const uint32_t *src = argb + (size_t)(y - py) * sw - px;
        for (int x = x1; x < x2; x++) {
            uint32_t c = src[x], a = c >> 24;
            if (a == 255) row[x] = c & 0xFFFFFF;
            else if (a) row[x] = gfx_blend(row[x], c & 0xFFFFFF, a);
        }
    }
}
