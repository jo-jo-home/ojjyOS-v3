/*
 * ojjyOS v3 - Web engine: painting a laid-out page into a surface
 *
 * Shared by the browser and the host render tool (tests/web_render.c), so
 * what the tests see is exactly what the OS draws.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_WEB_PAINT_H
#define _OJJY_WEB_PAINT_H

#include "web/engine.h"
#include "gfx.h"

typedef struct {
    /* Form controls are drawn by the browser (focus, caret, typed text). */
    void (*draw_field)(void *ctx, GfxSurface *s, const WebItem *it, int x, int y);
    void *ctx;
    int hover_link;             /* -1, or a link index to draw hovered */
    /* find-in-page highlights: items[] index ranges with byte offsets */
    const int32_t *marks;       /* triples: item, start byte, length */
    int mark_count;
    int mark_current;           /* index into marks (drawn orange) */
} WebPaintOpts;

/* Paint doc at (ox, oy) (logical px), scrolled by `scroll`, clipped to the
 * logical rect (cx, cy, cw, ch). The canvas colour is filled first. */
void web_paint(GfxSurface *s, const WebDoc *d, int ox, int oy, int scroll, int cx, int cy, int cw, int ch,
               const WebPaintOpts *opts);

/* Bilinear ARGB (straight alpha) image into a logical rect, clipped. */
void web_paint_image(GfxSurface *s, const uint32_t *px, int iw, int ih, int x, int y, int w, int h);

#endif /* _OJJY_WEB_PAINT_H */
