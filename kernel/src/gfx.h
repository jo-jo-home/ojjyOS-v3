/*
 * ojjyOS v3 - 2D graphics primitives (anti-aliased, span based)
 *
 * Everything draws into a GfxSurface (32-bit 0x00RRGGBB pixels, a pitch
 * in pixels and a clip rectangle). Shapes are filled row by row with
 * direct pointer writes; only edge pixels pay for coverage maths, so a
 * large rounded window costs about the same as a plain rectangle.
 *
 * Coordinates, sizes and radii passed to gfx_* are LOGICAL; they are
 * multiplied by the surface's scale (HiDPI) so shapes render at full
 * density. Image sources (gfx_round_rect_image) are physical-size.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_GFX_H
#define _OJJY_GFX_H

#include "types.h"

typedef struct {
    uint32_t *pixels;
    int width, height, pitch;          /* pitch in pixels */
    int clip_x1, clip_y1, clip_x2, clip_y2;   /* half-open clip box (physical) */
    int scale;               /* logical->physical factor for all gfx_* inputs (0/1 = 1) */
} GfxSurface;

/* Fast blend: alpha 0..255 (255 = fg). Two multiplies per pixel. */
static inline uint32_t gfx_blend(uint32_t bg, uint32_t fg, uint32_t alpha)
{
    uint32_t a = alpha + (alpha >> 7);          /* 0..256 */
    uint32_t rb = ((fg & 0xFF00FF) * a + (bg & 0xFF00FF) * (256 - a)) >> 8;
    uint32_t g = ((fg & 0x00FF00) * a + (bg & 0x00FF00) * (256 - a)) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00) | 0xFF000000u;   /* X byte set like RGB() */
}

uint32_t gfx_isqrt(uint32_t v);

/* Solid colour, blended with `alpha`, anti-aliased corners of radius r. */
void gfx_round_rect(GfxSurface *s, int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
/* Same shape, but pixels come from a full-surface-sized source image
 * (`src` has the surface's width as pitch), e.g. the blurred glass cache. */
void gfx_round_rect_image(GfxSurface *s, int x, int y, int w, int h, int r, const uint32_t *src, uint8_t alpha);
/* Anti-aliased ring of thickness 1 (hairline border) inside the shape. */
void gfx_round_rect_stroke(GfxSurface *s, int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
/* Vertical gradient fill (top -> bottom colours) with AA rounded corners. */
void gfx_round_rect_vgradient(GfxSurface *s, int x, int y, int w, int h, int r,
                              uint32_t top, uint32_t bottom, uint8_t alpha);
/* Soft drop shadow around a rounded rect: `spread` px blur, shifted down by
 * dy, darkest alpha at the edge. The shape's interior is left untouched. */
void gfx_shadow(GfxSurface *s, int x, int y, int w, int h, int r, int spread, int dy,
                uint32_t color, uint8_t alpha);
/* Liquid Glass (macOS Tahoe-style material): the pixels already drawn
 * behind the shape are blurred and saturated, bent toward the inside near
 * the rim (refraction), lightly tinted, and lit by a specular rim from
 * above. All sizes LOGICAL. Needs a scratch buffer (gfx_set_scratch) at
 * least as large as the shape's box plus margins; without one it falls
 * back to a plain tinted fill. */
typedef struct {
    uint32_t tint;          /* glass tint colour */
    uint8_t tint_alpha;     /* 0..255 */
    uint8_t blur;           /* backdrop blur radius (logical px) */
    uint8_t refract;        /* max edge displacement (logical px) */
    uint8_t band;           /* width of the refracting rim (logical px) */
    uint8_t specular;       /* rim highlight strength 0..255 */
    uint8_t saturate;       /* backdrop saturation boost 0..255 */
    uint8_t alpha;          /* overall opacity (fades) */
} GfxGlass;
void gfx_set_scratch(uint32_t *pixels, size_t count);
void gfx_liquid_glass(GfxSurface *s, int x, int y, int w, int h, int r, const GfxGlass *g);

/* Anti-aliased ring segment centred at (cx, cy): radii [r_in, r_out],
 * limited to directions within `half_angle_deg` of straight up (90 = a
 * semicircle, 180 = full ring). Used for Wi-Fi fans, spinners, gauges. */
void gfx_arc_up(GfxSurface *s, int cx, int cy, int r_in, int r_out, int half_angle_deg,
                uint32_t color, uint8_t alpha);
/* Layer blit for animations: the PHYSICAL source rectangle (src_x, src_y,
 * src_w, src_h) of `src` (row pitch src_pitch pixels) is scaled with
 * bilinear filtering into the LOGICAL destination rect, clipped to a
 * rounded-rect mask of radius r (anti-aliased) and blended with `alpha`. */
void gfx_blit_scaled(GfxSurface *s, const uint32_t *src, int src_pitch, int src_x, int src_y, int src_w, int src_h,
                     int x, int y, int w, int h, int r, uint8_t alpha);
/* Bilinear-filtered RGBA image (straight alpha, bytes R,G,B,A), scaled into
 * the destination rectangle and blended with an overall `alpha`. */
void gfx_draw_rgba_scaled(GfxSurface *s, const uint8_t *rgba, int src_w, int src_h,
                          int x, int y, int w, int h, uint8_t alpha);

#endif /* _OJJY_GFX_H */
