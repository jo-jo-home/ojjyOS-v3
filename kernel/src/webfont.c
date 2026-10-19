/*
 * ojjyOS v3 - Web page fonts (see webfont.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "webfont.h"

static const UiFontFace *wf(uint8_t face)
{
    if (face >= WEBFONT_FACES) face = 4 * 2;         /* 16 px regular */
    return &webfont_faces[face][text_density() - 1];
}

uint8_t webfont_pick(int px, int weight, bool italic, bool mono)
{
    (void)italic;
    if (mono) {
        int best = 0;
        for (int i = 1; i < WEBFONT_MONO_SIZES; i++)
            if ((px - webfont_mono_sizes[i] < 0 ? webfont_mono_sizes[i] - px : px - webfont_mono_sizes[i]) <
                (px - webfont_mono_sizes[best] < 0 ? webfont_mono_sizes[best] - px : px - webfont_mono_sizes[best])) best = i;
        return (uint8_t)(WEBFONT_SIZES * 2 + best);
    }
    int best = 0;
    for (int i = 1; i < WEBFONT_SIZES; i++) {
        int d = px - webfont_sizes[i], b = px - webfont_sizes[best];
        if (d < 0) d = -d;
        if (b < 0) b = -b;
        if (d < b || (d == b && webfont_sizes[i] < webfont_sizes[best])) best = i;
    }
    return (uint8_t)(best * 2 + (weight >= 600 ? 1 : 0));
}

int webfont_px(uint8_t face)
{
    if (face >= WEBFONT_SIZES * 2) return webfont_mono_sizes[(face - WEBFONT_SIZES * 2) % WEBFONT_MONO_SIZES];
    return webfont_sizes[face / 2];
}

int webfont_draw(GfxSurface *surf, int x, int y, const char *s, int len, uint8_t face, uint32_t color, uint8_t alpha)
{
    return text_draw_face(surf, x, y, s, len, wf(face), color, alpha);
}

int webfont_width(const char *s, int len, uint8_t face)
{
    return text_draw_face(0, 0, 0, s, len, wf(face), 0, 0);
}

int webfont_ascent(uint8_t face) { return wf(face)->ascent / text_density(); }
int webfont_height(uint8_t face) { return wf(face)->line / text_density(); }
