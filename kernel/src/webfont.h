/*
 * ojjyOS v3 - Web page fonts (Inter Regular/Bold in 10 sizes, JetBrains
 * Mono in 2 sizes), each at 1x and 2x density. A face id names one face;
 * webfont_pick() maps a CSS size/weight to the nearest face.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_WEBFONT_H
#define _OJJY_WEBFONT_H

#include "uifont.h"

#define WEBFONT_SIZES      10
#define WEBFONT_MONO_SIZES 2
#define WEBFONT_FACES      (WEBFONT_SIZES * 2 + WEBFONT_MONO_SIZES)

extern const uint8_t webfont_sizes[WEBFONT_SIZES];
extern const uint8_t webfont_mono_sizes[WEBFONT_MONO_SIZES];
extern const UiFontFace webfont_faces[WEBFONT_FACES][2];

/* Face id for a size in logical px (already zoomed), weight and family.
 * Sizes beyond the largest face use it (text stays readable). */
uint8_t webfont_pick(int px, int weight, bool italic, bool mono);
int webfont_width(const char *s, int len, uint8_t face);
int webfont_ascent(uint8_t face);
int webfont_height(uint8_t face);
int webfont_px(uint8_t face);
/* Draw with the top of the line box at y; returns the advance. */
int webfont_draw(GfxSurface *surf, int x, int y, const char *s, int len, uint8_t face, uint32_t color, uint8_t alpha);

#endif /* _OJJY_WEBFONT_H */
