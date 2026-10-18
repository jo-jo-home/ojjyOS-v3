/*
 * ojjyOS v3 - 2D graphics primitives (anti-aliased, span based)
 *
 * Everything draws into a GfxSurface (32-bit 0x00RRGGBB pixels, a pitch
 * in pixels and a clip rectangle). Shapes are filled row by row with
 * direct pointer writes; only edge pixels pay for coverage maths, so a
 * large rounded window costs about the same as a plain rectangle.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_GFX_H
#define _OJJY_GFX_H

#include "types.h"

typedef struct {
    uint32_t *pixels;
    int width, height, pitch;          /* pitch in pixels */
    int clip_x1, clip_y1, clip_x2, clip_y2;   /* half-open clip box */
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
void gfx_round_rect_image(GfxSurface *s, int x, int y, int w, int h, int r, const uint32_t *src);
/* Anti-aliased ring of thickness 1 (hairline border) inside the shape. */
void gfx_round_rect_stroke(GfxSurface *s, int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
/* Vertical gradient fill (top -> bottom colours) with AA rounded corners. */
void gfx_round_rect_vgradient(GfxSurface *s, int x, int y, int w, int h, int r,
                              uint32_t top, uint32_t bottom, uint8_t alpha);
/* Soft drop shadow around a rounded rect: `spread` px blur, shifted down by
 * dy, darkest alpha at the edge. The shape's interior is left untouched. */
void gfx_shadow(GfxSurface *s, int x, int y, int w, int h, int r, int spread, int dy,
                uint32_t color, uint8_t alpha);
/* Bilinear-filtered RGBA image (straight alpha, bytes R,G,B,A), scaled into
 * the destination rectangle and blended with an overall `alpha`. */
void gfx_draw_rgba_scaled(GfxSurface *s, const uint8_t *rgba, int src_w, int src_h,
                          int x, int y, int w, int h, uint8_t alpha);

#endif /* _OJJY_GFX_H */
