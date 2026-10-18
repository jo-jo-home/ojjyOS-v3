/*
 * ojjyOS v3 Kernel - Console Font
 *
 * Anti-aliased 8x16 monospace font (JetBrains Mono, SIL Open Font
 * License 1.1 - see fonts/OFL-LICENSE.txt), rendered into the fixed
 * 8x16 console cell by tools/mkfont.py with 4x supersampling.
 *
 * ojjyOS - Created by Jonas Lee
 */

#ifndef _OJJY_FONT_H
#define _OJJY_FONT_H

#include "types.h"

#define FONT_WIDTH      8
#define FONT_HEIGHT     16

/*
 * Get the anti-aliased coverage bitmap for a character:
 * FONT_WIDTH * FONT_HEIGHT bytes, one 0..255 coverage value per pixel
 * (0 = background, 255 = full foreground). Callers blend per pixel
 * with fb_blend(bg, fg, coverage).
 */
const uint8_t *font_get_glyph_aa(char c);
/* Same glyphs at 16x32 (2x displays), see tools/mkfont2x.py. */
const uint8_t *font_get_glyph_aa2x(char c);

#endif /* _OJJY_FONT_H */
