/*
 * ojjyOS v3 - SVG rasterizer (icons, logos, simple illustrations)
 *
 * Supports the static subset most web icons use: path (all commands incl.
 * arcs), rect (rounded), circle, ellipse, line, polyline, polygon, g, use,
 * symbol, nested svg; fill/stroke (colours, currentColor, first stop of a
 * gradient), stroke-width/linecap/linejoin, fill-rule, opacity, transform,
 * style="" and simple .class rules from <style>. Text, filters, masks,
 * clipping and real gradients are not drawn.
 *
 * Integer only (the kernel has no floating point): 16.16 fixed point
 * geometry, CORDIC trigonometry, 4x vertical / exact horizontal coverage
 * anti-aliasing. Input is untrusted: every buffer access is bounded.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_IMAGE_SVG_H
#define _OJJY_IMAGE_SVG_H

#include "types.h"

/* Is this an SVG document (looks for "<svg" near the start)? */
bool svg_detect(const uint8_t *data, size_t len);
/* Intrinsic size: width/height attributes, else the viewBox, else 300x150. */
bool svg_info(const uint8_t *data, size_t len, int *w, int *h);
/* Render into argb (w*h straight-alpha 0xAARRGGBB, cleared first). */
bool svg_render(const uint8_t *data, size_t len, uint32_t *argb, int w, int h, uint32_t current_color,
                uint8_t *scratch, size_t scratch_size);

#endif /* _OJJY_IMAGE_SVG_H */
