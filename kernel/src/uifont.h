/*
 * ojjyOS v3 - Proportional UI fonts (Inter) and text drawing
 *
 * Coordinates are in screen pixels; `y` is the TOP of the line box
 * (baseline = y + ascent), matching how the old 8x16 font was positioned.
 * Each style exists at 1x and 2x density; 2x is picked when the display
 * runs in HiDPI mode (see fb_ui_scale()).
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_UIFONT_H
#define _OJJY_UIFONT_H

#include "types.h"
#include "gfx.h"

typedef enum {
    TEXT_SMALL,          /* 11 px Regular: captions, secondary labels */
    TEXT_SMALL_BOLD,     /* 11 px SemiBold: section headers */
    TEXT_BODY,           /* 13 px Regular: default UI text */
    TEXT_BODY_MEDIUM,    /* 13 px Medium: menu titles, buttons */
    TEXT_BODY_BOLD,      /* 13 px SemiBold: app name, emphasis */
    TEXT_TITLE,          /* 15 px SemiBold: window and panel titles */
    TEXT_HEADLINE,       /* 20 px Bold */
    TEXT_LARGE,          /* 28 px SemiBold */
    TEXT_DISPLAY,        /* 72 px Medium, digits/colon/AM/PM only (clock) */
    TEXT_STYLE_COUNT
} TextStyle;

typedef struct {
    uint16_t cp;
    int16_t bx, top;         /* left bearing; pixels above the baseline */
    uint8_t w, h;
    uint16_t adv64;          /* advance, 1/64 px */
    uint32_t off;            /* into the face's bitmap */
} UiGlyph;

typedef struct {
    const UiGlyph *glyphs;
    uint16_t count;
    const uint8_t *bits;
    int16_t ascent, descent, line;     /* 2x display faces exceed 127 px */
    uint8_t packed4;                   /* 1: 4-bit coverage, two pixels per byte (web faces) */
} UiFontFace;

extern const UiFontFace uifont_faces[TEXT_STYLE_COUNT][2];

/* Draw UTF-8 text; returns the advance in pixels. Draws at most `max_bytes`
 * bytes of `s` (-1 = whole string). */
int text_draw_n(GfxSurface *s, int x, int y, const char *str, int max_bytes, TextStyle st, uint32_t color, uint8_t alpha);
int text_draw(GfxSurface *s, int x, int y, const char *str, TextStyle st, uint32_t color);
/* Like text_draw, but truncates with an ellipsis to fit `max_w` pixels. */
int text_draw_fit(GfxSurface *s, int x, int y, const char *str, TextStyle st, uint32_t color, int max_w);
int text_width(const char *str, TextStyle st);
int text_width_n(const char *str, int max_bytes, TextStyle st);
int text_line_height(TextStyle st);
int text_ascent(TextStyle st);
/* Height of capital letters; text_draw at y = top + (h - cap)/2 - (ascent - cap)
 * centres a line optically in a box (see text_y_center). */
int text_cap_height(TextStyle st);
int text_y_center(int top, int h, TextStyle st);
/* True if the UI font has a real glyph for code point cp (no fallback). */
bool text_has_glyph(uint32_t cp);
/* Density used for drawing (1 or 2); set by the framebuffer's HiDPI mode. */
void text_set_density(int density);
int text_density(void);
/* Draw with an explicit face at the current density (the face must be the
 * density's variant). Same conventions as text_draw_n. */
int text_draw_face(GfxSurface *s, int x, int y, const char *str, int max_bytes, const UiFontFace *f, uint32_t color,
                   uint8_t alpha);

#endif /* _OJJY_UIFONT_H */
