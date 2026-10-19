/*
 * ojjyOS v3 - Web engine: CSS (parsing, selector matching, cascade)
 *
 * Stylesheets (the built-in UA sheet, <style>, linked sheets and style=""
 * attributes) are parsed into rules held in the page arena, indexed by the
 * rightmost id/class/tag, and cascaded per element (origin, !important,
 * specificity, order, inheritance, custom properties with var()). No
 * floating point: lengths are integer px plus a percentage in hundredths.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_WEB_CSS_H
#define _OJJY_WEB_CSS_H

#include "web/dom.h"

/* Length = px + pct/100 % of the reference (calc(100% - 20px) works). */
typedef struct {
    int32_t px;
    int32_t pct;                /* hundredths of a percent: 10000 = 100% */
    uint8_t kind;               /* LEN_AUTO / LEN_SET / LEN_NONE */
} CssLen;
#define LEN_AUTO 0
#define LEN_SET  1
#define LEN_NONE 2              /* max-width: none, height: auto-like */

enum {
    DISP_INLINE = 0, DISP_BLOCK, DISP_INLINE_BLOCK, DISP_LIST_ITEM, DISP_FLEX, DISP_INLINE_FLEX, DISP_GRID,
    DISP_TABLE, DISP_TABLE_ROW, DISP_TABLE_CELL, DISP_TABLE_ROW_GROUP, DISP_TABLE_CAPTION, DISP_CONTENTS,
    DISP_NONE
};
enum { POS_STATIC = 0, POS_RELATIVE, POS_ABSOLUTE, POS_FIXED, POS_STICKY };
enum { WS_NORMAL = 0, WS_PRE, WS_NOWRAP, WS_PRE_WRAP, WS_PRE_LINE };
enum { ALIGN_LEFT = 0, ALIGN_RIGHT, ALIGN_CENTER, ALIGN_JUSTIFY };
enum { LIST_NONE = 0, LIST_DISC, LIST_CIRCLE, LIST_SQUARE, LIST_DECIMAL, LIST_LOWER_ALPHA, LIST_UPPER_ALPHA, LIST_LOWER_ROMAN, LIST_UPPER_ROMAN };
enum { FLEX_START = 0, FLEX_END, FLEX_CENTER, FLEX_BETWEEN, FLEX_AROUND, FLEX_EVENLY, FLEX_STRETCH, FLEX_BASELINE };
#define DECO_UNDERLINE    1
#define DECO_LINE_THROUGH 2
#define DECO_OVERLINE     4
enum { TT_NONE = 0, TT_UPPER, TT_LOWER, TT_CAPITALIZE };
enum { FLOAT_NONE = 0, FLOAT_LEFT, FLOAT_RIGHT };
enum { VA_BASELINE = 0, VA_MIDDLE, VA_TOP, VA_BOTTOM, VA_SUB, VA_SUPER };

typedef struct CssVar {
    const char *name;           /* "--x" */
    const char *value;
    struct CssVar *next;
} CssVar;

typedef struct CssStyle {
    uint8_t display, position, float_, white_space, text_align, decoration, transform;
    uint8_t italic, mono, list_style, border_box, clip, invisible, flex_dir_col, flex_wrap;
    uint8_t justify, align_items, align_self, valign, nowrap_text, table_fixed, grid_cols;
    uint8_t center_blocks;      /* -webkit-center / <center>: centre child blocks too */
    uint8_t masked;             /* mask-image icon: the background is a mask fill */
    uint8_t clear;              /* 1 left, 2 right, 3 both */
    uint16_t font_weight;
    int16_t font_size;          /* px */
    int16_t line_height;        /* px; 0 = normal */
    int16_t radius;
    int16_t gap_row, gap_col;
    int16_t flex_grow, flex_shrink;     /* hundredths */
    int16_t order;
    int16_t text_indent;
    int16_t border_spacing;
    int16_t grid_min;           /* repeat(auto-fill, minmax(N, 1fr)) */
    int16_t border_w[4];        /* top right bottom left */
    uint32_t border_c[4];
    uint32_t color;             /* 0xAARRGGBB */
    uint32_t bg;                /* alpha 0 = transparent */
    CssLen margin[4], padding[4];
    CssLen width, height, min_w, max_w, min_h, max_h, flex_basis;
    CssLen inset[4];            /* top right bottom left (relative/absolute) */
    const char *bg_image;       /* url(...) */
    const char *content;        /* 'content' (pseudo-elements) */
    CssVar *vars;
} CssStyle;

typedef struct CssSheetSet CssSheetSet;

typedef struct {
    int viewport_w, viewport_h; /* CSS px, for @media and vw/vh */
    bool dark;                  /* prefers-color-scheme: dark */
    int zoom;                   /* percent: absolute lengths are scaled by it */
    bool quirks;                /* legacy (no doctype) UA rules */
} CssMedia;

/* Create an empty rule set in the arena (UA sheet included). */
CssSheetSet *css_new(WebArena *a, const CssMedia *media);
/* Parse a stylesheet and append its rules (author origin). base_url is not
 * used for resolution here; @import is collected for the caller. */
void css_add_sheet(CssSheetSet *set, const char *css, size_t len);
/* Number of @import / <link> URLs requested by sheets added so far. */
int css_import_count(const CssSheetSet *set);
const char *css_import_url(const CssSheetSet *set, int i);
/* Compute styles for every element under tree->root (after all sheets). */
void css_style_tree(CssSheetSet *set, DomTree *tree);
/* Parse one colour ("#fff", "rgb()", named). false if not a colour. */
bool css_parse_color(const char *s, uint32_t *argb);
/* Statistics for tests/logs. */
int css_rule_count(const CssSheetSet *set);

/* Resolve a length against a reference size (percentages). */
static inline int32_t css_len(const CssLen *l, int32_t ref)
{
    return l->px + (int32_t)(((int64_t)l->pct * ref) / 10000);
}

#endif /* _OJJY_WEB_CSS_H */
