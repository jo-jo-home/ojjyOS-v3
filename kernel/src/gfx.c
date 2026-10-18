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
