/*
 * ojjyOS v3 - Wallpaper rendering at the real screen size
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_UI_WALLPAPER_H
#define _OJJY_UI_WALLPAPER_H

#include "../types.h"

/* The built-in Tahoe wallpaper (light or dark), drawn procedurally into a
 * w*h 0x00RRGGBB buffer: smooth gradients, anti-aliased waves, dithered. */
void wallpaper_render_tahoe(uint32_t *dst, int w, int h, bool dark);
/* Bilinear-filtered, dithered scale of an RGBA image to w*h. */
void wallpaper_scale_rgba(uint32_t *dst, int w, int h, const uint8_t *rgba, int sw, int sh);

#endif /* _OJJY_UI_WALLPAPER_H */
