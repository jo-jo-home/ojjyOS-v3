/*
 * ojjyOS v3 - HTML layout engine
 *
 * One pass: a tolerant tokenizer feeds a flow-layout state machine that
 * produces a display list (positioned text runs, boxes, links, form fields)
 * for a given width. Text is measured by pluggable metrics (WebMetrics):
 * the browser plugs in the proportional Inter UI font; without metrics the
 * engine uses fixed 8x16 cells (host tests, plain consoles). Code and <pre>
 * always use fixed cells (the monospace font). There is no DOM and no
 * JavaScript: <script>/<style> contents are skipped, <noscript> content is
 * shown. Characters the font lacks are transliterated to ASCII.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_HTML_H
#define _OJJY_HTML_H

#include "types.h"

#define WEB_CELL_W        8
#define WEB_CELL_H        16
#define WEB_MAX_RUNS      12000
#define WEB_TEXT_POOL     (640 * 1024)
#define WEB_MAX_LINKS     2048
#define WEB_MAX_BOXES     1024
#define WEB_MAX_FIELDS    64
#define WEB_MAX_FORMS     16
#define WEB_MAX_ANCHORS   128

/* Run style flags */
#define WS_BOLD       0x01
#define WS_LINK       0x02
#define WS_HEADING    0x04
#define WS_MUTED      0x08      /* image alt text, captions, quotes */
#define WS_CODE       0x10
#define WS_ITALIC     0x20
#define WS_BUTTON     0x40      /* text drawn on a button box */
#define WS_FIELD      0x80      /* text drawn inside an input box */

typedef struct {
    int32_t x, y;               /* page pixels (top-left of the run) */
    uint32_t text;              /* offset into pool */
    uint16_t len;               /* bytes (UTF-8) */
    uint8_t style;
    uint8_t scale;              /* 1 or 2: fixed-cell scale (h1/h2, code) */
    int16_t link;               /* -1 or link index */
    int16_t w, h;               /* measured size in pixels (hit tests, underline) */
    uint8_t level;              /* heading level 1..6, 0 = body text */
} WebRun;

/* Text measurement. width(): pixel width of len bytes of UTF-8 in the given
 * run style/heading level. line_height(): line box height for that style.
 * has_glyph(): whether a code point can be drawn as itself (else it is
 * transliterated to ASCII). Pass NULL to html_set_metrics for fixed cells. */
typedef struct {
    int (*width)(const char *s, int len, uint8_t style, uint8_t level);
    int (*line_height)(uint8_t style, uint8_t level);
    bool (*has_glyph)(uint32_t cp);
} WebMetrics;

void html_set_metrics(const WebMetrics *metrics);

typedef enum { BOX_RULE, BOX_BULLET, BOX_INPUT, BOX_BUTTON, BOX_IMAGE, BOX_QUOTE_BAR, BOX_CODE } WebBoxKind;

typedef struct {
    int32_t x, y, w, h;
    uint8_t kind;
    int16_t field;              /* form field for inputs/buttons, else -1 */
} WebBox;

typedef enum { FIELD_TEXT, FIELD_PASSWORD, FIELD_HIDDEN, FIELD_SUBMIT, FIELD_CHECKBOX } WebFieldKind;

typedef struct {
    uint8_t kind;
    int8_t form;
    int16_t box;                /* box index or -1 */
    char name[48];
    char value[128];
    char label[64];             /* button text / select display text */
    bool checked;
} WebField;

typedef struct {
    uint32_t action;            /* pool offset ("" = same page) */
    bool post;
} WebForm;

typedef struct {
    uint32_t name;              /* pool offset */
    int32_t y;
} WebAnchor;

typedef struct {
    char title[128];
    char base[256];             /* <base href>, "" if none */
    int32_t width, height;      /* layout width / total height (px) */
    WebRun runs[WEB_MAX_RUNS];
    int run_count;
    char pool[WEB_TEXT_POOL];
    uint32_t pool_len;
    uint32_t links[WEB_MAX_LINKS];  /* pool offsets of href strings */
    int link_count;
    WebBox boxes[WEB_MAX_BOXES];
    int box_count;
    WebField fields[WEB_MAX_FIELDS];
    int field_count;
    WebForm forms[WEB_MAX_FORMS];
    int form_count;
    WebAnchor anchors[WEB_MAX_ANCHORS];
    int anchor_count;
    bool truncated;             /* ran out of display-list space */
} WebDoc;

/* Lay out `html` (len bytes) for `width` pixels. plain=true renders text. */
void html_layout(WebDoc *doc, const char *html, size_t len, int width, bool plain);
const char *web_text(const WebDoc *doc, uint32_t off);
/* First run index whose bottom edge is below y (binary search). */
int web_first_run_at(const WebDoc *doc, int32_t y);
/* Link under page point, or -1. */
int web_link_at(const WebDoc *doc, int32_t x, int32_t y);
/* Box under page point, or -1. */
int web_box_at(const WebDoc *doc, int32_t x, int32_t y);
int32_t web_anchor_y(const WebDoc *doc, const char *name);   /* -1 if absent */
/* Build "?a=1&b=2" for a GET form submit (fields of form `form`). */
void web_form_query(const WebDoc *doc, int form, int submitter, char *out, size_t size);

#endif /* _OJJY_HTML_H */
