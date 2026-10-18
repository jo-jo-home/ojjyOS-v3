/*
 * ojjyOS v3 - Procedural "Tahoe" wallpaper (see wallpaper.h)
 *
 * Rendered once at the real screen size: a deep-to-bright sky gradient, a
 * soft sun glow, and layered translucent waves whose edges are
 * anti-aliased (sub-pixel coverage) and whose gradients are dithered, so
 * there are no stair-steps or colour bands at any resolution.
 *
 * Fixed point throughout (no FPU in the kernel): colours carry 8 extra
 * fraction bits per channel until the final ordered-dither step.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "wallpaper.h"

/* sin() of a 16-bit angle (65536 = full turn), Q15 result. Odd polynomial
 * on the first quadrant (error < 1e-4). */
static int32_t isin(uint32_t angle)
{
    angle &= 0xFFFF;
    int neg = angle >= 0x8000;
    if (neg) angle -= 0x8000;
    if (angle > 0x4000) angle = 0x8000 - angle;          /* mirror into [0, pi/2] */
    /* x in Q15 radians: angle * (pi/2) / 0x4000 */
    int64_t x = ((int64_t)angle * 51472) >> 14;          /* 51472 = pi/2 in Q15 */
    int64_t x2 = (x * x) >> 15;
    int64_t x3 = (x2 * x) >> 15;
    int64_t x5 = (x3 * x2) >> 15;
    int64_t x7 = (x5 * x2) >> 15;
    int64_t s = x - x3 / 6 + x5 / 120 - x7 / 5040;
    if (s > 32767) s = 32767;
    return neg ? -(int32_t)s : (int32_t)s;
}

typedef struct { int32_t r, g, b; } C16;    /* channels * 256 */

static C16 c16(uint32_t rgb)
{
    C16 c = { (int32_t)((rgb >> 16) & 0xFF) << 8, (int32_t)((rgb >> 8) & 0xFF) << 8, (int32_t)(rgb & 0xFF) << 8 };
    return c;
}

static C16 mix(C16 a, C16 b, int32_t t)      /* t: 0..4096 */
{
    C16 c = { a.r + (((b.r - a.r) * t) >> 12), a.g + (((b.g - a.g) * t) >> 12), a.b + (((b.b - a.b) * t) >> 12) };
    return c;
}

typedef struct {
    int32_t base;            /* baseline, fraction of height * 4096 */
    int32_t amp1, amp2;      /* amplitudes, fraction of height * 4096 */
    uint32_t freq1, freq2;   /* waves across the screen width, * 256 */
    uint32_t phase1, phase2; /* 16-bit angles */
    uint32_t top, bottom;    /* fill gradient (rgb) */
    int32_t alpha;           /* 0..4096 layer opacity */
    uint32_t rim;            /* crest highlight colour */
    int32_t rim_alpha;       /* 0..4096 */
} Wave;

static const Wave light_waves[] = {
    { 1900, 300, 110, 150, 380, 0x2400, 0x9000, 0x8FD0F2, 0x3F95D8, 2500, 0xF2FAFF, 2800 },
    { 2300, 330, 100, 120, 310, 0xB000, 0x3000, 0x5CB0E8, 0x1F72C2, 3200, 0xE2F3FF, 2900 },
    { 2780, 280, 130, 105, 270, 0x5800, 0xE000, 0x3990D8, 0x1455A9, 3700, 0xCDEAFF, 2600 },
    { 3250, 230, 100,  85, 230, 0xF000, 0x6400, 0x1F6FC2, 0x0A3780, 4000, 0xA8D9FB, 2200 },
};

static const Wave dark_waves[] = {
    { 1900, 300, 110, 150, 380, 0x2400, 0x9000, 0x24497A, 0x14325E, 2500, 0x7AA8DA, 1700 },
    { 2300, 330, 100, 120, 310, 0xB000, 0x3000, 0x1C4272, 0x102A54, 3200, 0x6896D0, 1800 },
    { 2780, 280, 130, 105, 270, 0x5800, 0xE000, 0x163868, 0x0B2148, 3700, 0x5687C6, 1700 },
    { 3250, 230, 100,  85, 230, 0xF000, 0x6400, 0x102C58, 0x061634, 4000, 0x4677B6, 1500 },
};

#define NWAVES 4

/* 4x4 Bayer thresholds, scaled to 0..255 (one 8-bit step). */
static const uint8_t dither4[4][4] = {
    {   0, 128,  32, 160 }, { 192,  64, 224,  96 }, {  48, 176,  16, 144 }, { 240, 112, 208,  80 }
};

static uint32_t out_px(C16 c, int x, int y)
{
    int32_t d = dither4[y & 3][x & 3];
    int32_t r = (c.r + d) >> 8, g = (c.g + d) >> 8, b = (c.b + d) >> 8;
    if (r < 0) r = 0;
    if (g < 0) g = 0;
    if (b < 0) b = 0;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void wallpaper_render_tahoe(uint32_t *dst, int w, int h, bool dark)
{
    if (!dst || w <= 0 || h <= 0) return;
    const Wave *waves = dark ? dark_waves : light_waves;
    C16 sky_top = c16(dark ? 0x060B18 : 0x0B5FB4);
    C16 sky_mid = c16(dark ? 0x10264A : 0x3C9BDD);
    C16 sky_low = c16(dark ? 0x23416B : 0xB9E2F7);
    C16 glow = c16(dark ? 0x4B6E9E : 0xFFFFFF);
    int32_t glow_strength = dark ? 1100 : 1900;
    int glow_x = w * 66 / 100, glow_y = h * 22 / 100;
    int32_t glow_r = (int32_t)(w > h ? w : h) * 62 / 100;
    int32_t shade = h * 256 * 5 / 100;           /* crest shadow reach, 1/256 px */

    /* Wave crest heights per column, in 1/256 px (computed per layer per x). */
    for (int y = 0; y < h; y++) {
        uint32_t *row = dst + (size_t)y * (size_t)w;
        /* Sky: two-segment vertical gradient ending at the first wave. */
        int32_t t = (int32_t)(((int64_t)y * 4096) / (h > 1 ? h - 1 : 1));
        C16 sky = t < 2000 ? mix(sky_top, sky_mid, t * 4096 / 2000)
                           : mix(sky_mid, sky_low, (t - 2000) * 4096 / 2096);
        int32_t ypx256 = y * 256 + 128;
        for (int x = 0; x < w; x++) {
            C16 c = sky;
            /* Sun glow: (1 - d/r)^2 falloff, integer distance via squared compare. */
            int32_t dx = x - glow_x, dy = (y - glow_y) * 5 / 4;
            int64_t d2 = (int64_t)dx * dx + (int64_t)dy * dy;
            int64_t r2 = (int64_t)glow_r * glow_r;
            if (d2 < r2) {
                int32_t f = (int32_t)(4096 - (d2 * 4096) / r2);          /* 1 - (d/r)^2 */
                c = mix(c, glow, (int32_t)(((int64_t)f * f >> 12) * glow_strength >> 12));
            }
            for (int i = 0; i < NWAVES; i++) {
                const Wave *wv = &waves[i];
                uint32_t a1 = (uint32_t)(((uint64_t)x * wv->freq1 * 65536 / 256) / (uint32_t)w) + wv->phase1;
                uint32_t a2 = (uint32_t)(((uint64_t)x * wv->freq2 * 65536 / 256) / (uint32_t)w) + wv->phase2;
                int64_t crest = (int64_t)wv->base * 256 * h / 4096
                              + ((int64_t)isin(a1) * wv->amp1 * h / 4096 * 256 >> 15)
                              + ((int64_t)isin(a2) * wv->amp2 * h / 4096 * 256 >> 15);
                int64_t below = ypx256 - crest;                    /* > 0: inside the wave */
                if (below < 0 && below > -shade && i > 0) {
                    /* This sheet casts a soft shadow onto the one behind it. */
                    int32_t k = (int32_t)(4096 + below * 4096 / shade);    /* 1 at crest -> 0 */
                    c = mix(c, c16(0x0A2A5C), (int32_t)(((int64_t)k * k >> 12) * (dark ? 900 : 700) >> 12));
                }
                if (below < -768) continue;                        /* above crest + rim glow */
                if (below > -256) {
                    int32_t cov = below >= 256 ? 4096 : (int32_t)((below + 256) * 4096 / 512);
                    int32_t depth = (int32_t)(below / 256);
                    int32_t span = h - (int32_t)(crest / 256);
                    int32_t g = span > 0 ? depth * 4096 / span : 4096;
                    if (g > 4096) g = 4096;
                    if (g < 0) g = 0;
                    C16 fill = mix(c16(wv->top), c16(wv->bottom), g);
                    c = mix(c, fill, (int32_t)((int64_t)cov * wv->alpha >> 12));
                }
                /* Bright rim just along the crest (soft, ~3 px). */
                int64_t dist = below < 0 ? -below : below;
                if (dist < 768) {
                    int32_t k = (int32_t)(4096 - dist * 4096 / 768);
                    c = mix(c, c16(wv->rim), (int32_t)(((int64_t)k * k >> 12) * wv->rim_alpha >> 12));
                }
            }
            row[x] = out_px(c, x, y);
        }
    }
}

void wallpaper_scale_cover(uint32_t *dst, int w, int h, const uint32_t *src, int sw, int sh)
{
    if (!dst || !src || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
    if (sw == w && sh == h) {
        for (size_t i = 0; i < (size_t)w * h; i++) dst[i] = src[i] & 0xFFFFFF;
        return;
    }
    /* Crop the source to the screen's aspect ratio around its centre. */
    int cx = 0, cy = 0, cw = sw, ch = sh;
    if ((int64_t)sw * h > (int64_t)sh * w) {
        cw = (int)(((int64_t)sh * w) / h);
        cx = (sw - cw) / 2;
    } else {
        ch = (int)(((int64_t)sw * h) / w);
        cy = (sh - ch) / 2;
    }
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    for (int y = 0; y < h; y++) {
        int32_t fy = (int32_t)(((int64_t)(2 * y + 1) * ch * 128) / h) - 128;
        if (fy < 0) fy = 0;
        int iy = fy >> 8, ty = fy & 255, iy2 = iy + 1 < ch ? iy + 1 : ch - 1;
        const uint32_t *r0 = src + (size_t)(cy + iy) * sw + cx, *r1 = src + (size_t)(cy + iy2) * sw + cx;
        for (int x = 0; x < w; x++) {
            int32_t fx = (int32_t)(((int64_t)(2 * x + 1) * cw * 128) / w) - 128;
            if (fx < 0) fx = 0;
            int ix = fx >> 8, tx = fx & 255, ix2 = ix + 1 < cw ? ix + 1 : cw - 1;
            uint32_t p00 = r0[ix], p10 = r0[ix2], p01 = r1[ix], p11 = r1[ix2];
            int32_t ch3[3];
            for (int k = 0; k < 3; k++) {
                int sh8 = 16 - 8 * k;
                int32_t top = (int32_t)((p00 >> sh8) & 255) * (256 - tx) + (int32_t)((p10 >> sh8) & 255) * tx;
                int32_t bot = (int32_t)((p01 >> sh8) & 255) * (256 - tx) + (int32_t)((p11 >> sh8) & 255) * tx;
                ch3[k] = (top * (256 - ty) + bot * ty) >> 8;     /* channel * 256 */
            }
            C16 c = { ch3[0], ch3[1], ch3[2] };
            dst[(size_t)y * w + x] = out_px(c, x, y);
        }
    }
}
