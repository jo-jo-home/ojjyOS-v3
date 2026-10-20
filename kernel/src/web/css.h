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
enum { BG_AUTO = 0, BG_COVER, BG_CONTAIN, BG_LEN };
enum { BG_REPEAT = 0, BG_NO_REPEAT, BG_REPEAT_X, BG_REPEAT_Y };
enum { VA_BASELINE = 0, VA_MIDDLE, VA_TOP, VA_BOTTOM, VA_SUB, VA_SUPER };

/* Grid tracks (grid-template-columns) and areas (grid-template-areas). */
enum { TK_FIXED = 0, TK_FR, TK_AUTO, TK_MINC, TK_MAXC };
typedef struct {
    uint8_t kind;               /* max sizing function */
    uint8_t min_kind;           /* min sizing (TK_FIXED or TK_AUTO) */
    int32_t px, pct;            /* TK_FIXED */
    int32_t fr;                 /* hundredths */
    int32_t min_px, min_pct;    /* minmax() minimum when min_kind == TK_FIXED */
} CssTrack;
#define GRID_MAX_TRACKS 32
#define GRID_MAX_AREAS  24
typedef struct {
    const char *name;
    uint8_t r0, c0, r1, c1;     /* half-open */
} CssArea;
typedef struct CssGrid {
    CssTrack t[GRID_MAX_TRACKS];
    uint8_t n;
    uint8_t auto_repeat;        /* 0 none, 1 auto-fill, 2 auto-fit */
    uint8_t rep_at;             /* index where the auto repeat goes */
    CssTrack rep;
    CssArea areas[GRID_MAX_AREAS];
    uint8_t narea, area_cols;
} CssGrid;

typedef struct CssVar {
    const char *name;           /* "--x" */
    uint32_t hash;              /* of name: lookups compare this first */
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
    uint8_t svg_fill_kind;      /* CSS fill (inherited): 0 unset, 1 none, 2 colour, 3 currentColor */
    uint32_t svg_fill;
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
    const char *bg_image;       /* url(...), made absolute against its stylesheet */
    uint8_t bg_size;            /* BG_AUTO / BG_COVER / BG_CONTAIN / BG_LEN */
    uint8_t bg_repeat;          /* BG_REPEAT / BG_NO_REPEAT / BG_REPEAT_X / BG_REPEAT_Y */
    int16_t bg_w, bg_h;         /* BG_LEN size (px, -1 = auto) */
    int16_t bg_px[2], bg_pct[2];/* position: px + hundredths of % of the free space */
    const char *content;        /* 'content' (pseudo-elements) */
    CssGrid *grid;              /* grid container template */
    int16_t gc_start, gc_end, gc_span;  /* grid item column placement (0 = auto) */
    int16_t gr_start, gr_span;
    const char *grid_area;      /* named area */
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
/* Same, with the sheet's directory URL ("https://a.com/css/") for relative url(). */
void css_add_sheet_base(CssSheetSet *set, const char *css, size_t len, const char *base_dir);
/* Number of @import / <link> URLs requested by sheets added so far. */
int css_import_count(const CssSheetSet *set);
const char *css_import_url(const CssSheetSet *set, int i);
/* Compute styles for every element under tree->root (after all sheets). */
void css_style_tree(CssSheetSet *set, DomTree *tree);
/* Parse one colour ("#fff", "rgb()", named). false if not a colour. */
bool css_parse_color(const char *s, uint32_t *argb);
/* Does a media query list ("screen and (min-width: 600px)") match? */
bool css_media_matches(const CssSheetSet *set, const char *media);
/* Statistics for tests/logs. */
int css_rule_count(const CssSheetSet *set);

/* Resolve a length against a reference size (percentages). */
static inline int32_t css_len(const CssLen *l, int32_t ref)
{
    return l->px + (int32_t)(((int64_t)l->pct * ref) / 10000);
}

#endif /* _OJJY_WEB_CSS_H */
