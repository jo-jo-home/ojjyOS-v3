/*
 * ojjyOS v3 Kernel - Bitmap Font
 *
 * 8x16 bitmap font for framebuffer console.
 */

#ifndef _OJJY_FONT_H
#define _OJJY_FONT_H

#include "types.h"

#define FONT_WIDTH      8
#define FONT_HEIGHT     16

/* Get font bitmap for a character (16 bytes, one byte per row) */
const uint8_t *font_get_glyph(char c);

#endif /* _OJJY_FONT_H */
