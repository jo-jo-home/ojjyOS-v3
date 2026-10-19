/*
 * ojjyOS v3 - Web engine: page model and layout (see engine.h)
 *
 * Block flow with margin collapsing between siblings, inline formatting
 * (styled words, baseline alignment, text-align, inline backgrounds and
 * padding), inline-block / replaced elements, floats (as rows), flexbox
 * (row/column, wrap, grow/shrink, auto margins, justify/align), simple
 * grids, tables (auto column widths, colspan), list markers, relative
 * positioning and overflow clipping. Absolutely/fixed positioned content is
 * not drawn (it is mostly overlays, menus and screen-reader text).
 *
 * Recursion is bounded (LAYOUT_DEPTH); deeper content is laid out flat.
 * Display-list overflow sets doc->truncated and stops emitting.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "web/engine.h"
#include "web/url.h"
#include "string.h"

#define LAYOUT_DEPTH  96
#define LINE_ATOMS    768
#define MAX_KIDS      4096
#define TABLE_COLS    48
#define FLEX_ITEMS    512

/* ------------------------------------------------------------------ */
/* Context                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    int32_t minw, maxw;
    bool measured;
} NodeInfo;

static struct {
    WebDoc *d;
    const WebFontMetrics *fm;
    WebArena *a;
    int16_t link;               /* current link index (-1) */
    int32_t viewport_h;
    int depth;
    int text_chars;             /* visible text emitted (JS detection) */
    /* pending list marker for the next line box */
    bool marker_pending;
    uint8_t marker_style;
    int marker_number;
    int32_t marker_x;
    uint32_t marker_color;
    uint8_t marker_font;
    int16_t marker_link;
} L;

static CssStyle default_style;

static const CssStyle *st(const DomNode *n)
{
    if (n && n->style) return n->style;
    return &default_style;
}

static NodeInfo *info(DomNode *n)
{
    if (!n->box) n->box = arena_alloc(L.a, sizeof(NodeInfo));
    return (NodeInfo *)n->box;
}

/* ------------------------------------------------------------------ */
/* Display list                                                        */
/* ------------------------------------------------------------------ */

static int emit(uint8_t kind, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
{
    WebDoc *d = L.d;
    if (d->item_count >= d->item_cap) { d->truncated = true; return -1; }
    WebItem *it = &d->items[d->item_count];
    memset(it, 0, sizeof(*it));
    it->kind = kind;
    it->x = x; it->y = y; it->w = w; it->h = h;
    it->color = color;
    it->link = L.link;
    return d->item_count++;
}

static void translate_range(int item_from, int item_to, int anchor_from, int anchor_to, int32_t dx, int32_t dy)
{
    if (!dx && !dy) return;
    WebDoc *d = L.d;
    if (item_to > d->item_count) item_to = d->item_count;
    if (anchor_to > d->anchor_count) anchor_to = d->anchor_count;
    for (int i = item_from; i < item_to; i++) { d->items[i].x += dx; d->items[i].y += dy; }
    for (int i = anchor_from; i < anchor_to; i++) d->anchors[i].y += dy;
}

/* Move everything emitted since (item_from, anchor_from). */
static void translate(int item_from, int anchor_from, int32_t dx, int32_t dy)
{
    translate_range(item_from, L.d->item_count, anchor_from, L.d->anchor_count, dx, dy);
}

static void add_anchor(const char *name, int32_t y)
{
    WebDoc *d = L.d;
    if (!name || !name[0] || d->anchor_count >= WEB_MAX_ANCHORS) return;
    d->anchors[d->anchor_count].name = name;
    d->anchors[d->anchor_count].y = y;
    d->anchor_count++;
}

/* ------------------------------------------------------------------ */
/* Style helpers                                                       */
/* ------------------------------------------------------------------ */

static uint8_t font_of(const CssStyle *s)
{
    int px = s->font_size > 0 ? s->font_size : 1;
    return L.fm->pick(px, s->font_weight, s->italic, s->mono);
}

/* Ascent/descent of a line box for style s (half-leading applied). */
static void line_metrics(const CssStyle *s, int32_t *A, int32_t *D)
{
    uint8_t f = font_of(s);
    int asc = L.fm->ascent(f), h = L.fm->height(f);
    if (s->line_height > 0) {
        int lead = s->line_height - h;
        *A = asc + lead / 2;
        *D = s->line_height - *A;
    } else {
        *A = asc;
        *D = h - asc;
    }
}

static bool out_of_flow(const CssStyle *s) { return s->position == POS_ABSOLUTE || s->position == POS_FIXED; }

static bool is_replaced(const DomNode *n)
{
    switch (n->tag) {
        case TAG_IMG: case TAG_INPUT: case TAG_SELECT: case TAG_TEXTAREA: case TAG_IFRAME: case TAG_VIDEO:
        case TAG_AUDIO: case TAG_CANVAS: case TAG_OBJECT: case TAG_EMBED: case TAG_SVG: case TAG_MATH:
            return true;
        default: return false;
    }
}

static bool closed_details_content(const DomNode *n)
{
    /* like Chrome, only the first <summary> of a closed <details> renders, whatever the CSS says */
    const DomNode *p = n->parent;
    if (!p || p->tag != TAG_DETAILS || dom_attr(p, "open")) return false;
    if (n->type == DOM_ELEMENT && n->tag == TAG_SUMMARY) {
        for (const DomNode *s = p->first; s && s != n; s = s->next) if (s->type == DOM_ELEMENT && s->tag == TAG_SUMMARY) return true;
        return false;
    }
    return true;
}

static bool skip_node(const DomNode *n)
{
    if (closed_details_content(n)) return true;
    if (n->type == DOM_TEXT) return false;
    if (n->type != DOM_ELEMENT) return true;
    const CssStyle *s = st(n);
    if (!n->style || s->display == DISP_NONE || out_of_flow(s)) return true;
    switch (n->tag) {
        case TAG_SCRIPT: case TAG_STYLE: case TAG_TEMPLATE: case TAG_HEAD: case TAG_TITLE: case TAG_META:
        case TAG_LINK: case TAG_BASE: case TAG_OPTION: case TAG_OPTGROUP: case TAG_AREA: case TAG_MAP:
            return true;
        default: return false;
    }
}

static bool block_level_display(uint8_t d)
{
    return d == DISP_BLOCK || d == DISP_LIST_ITEM || d == DISP_FLEX || d == DISP_GRID || d == DISP_TABLE ||
           d == DISP_TABLE_ROW || d == DISP_TABLE_CELL || d == DISP_TABLE_ROW_GROUP || d == DISP_TABLE_CAPTION;
}

static bool has_block_child(const DomNode *n, int depth);

static bool is_block_level(const DomNode *n)
{
    if (n->type != DOM_ELEMENT) return false;
    const CssStyle *s = st(n);
    if (s->float_ != FLOAT_NONE) return true;
    if (block_level_display(s->display)) return true;
    if (s->display == DISP_INLINE && n->first && !is_replaced(n) && has_block_child(n, 0)) return true;  /* block-in-inline */
    return false;
}

static bool has_block_child(const DomNode *n, int depth)
{
    if (depth > 3) return false;
    for (const DomNode *c = n->first; c; c = c->next) {
        if (c->type != DOM_ELEMENT || skip_node(c)) continue;
        const CssStyle *s = st(c);
        if (block_level_display(s->display)) return true;
        if (s->display == DISP_CONTENTS && has_block_child(c, depth + 1)) return true;
        if (s->display == DISP_INLINE && c->first && !is_replaced(c) && has_block_child(c, depth + 1)) return true;
    }
    return false;
}

static bool whitespace_only(const DomNode *n)
{
    if (n->type != DOM_TEXT) return false;
    for (uint32_t i = 0; i < n->text_len; i++) {
        char c = n->text[i];
        if (c != ' ' && c != '\n' && c != '\t' && c != '\r' && c != '\f') return false;
    }
    return true;
}

/* Children with display:contents flattened and skipped nodes removed. */
static int collect_kids(DomNode *n, DomNode **out, int max, int depth)
{
    int k = 0;
    if (n->pseudo_before && k < max && !skip_node(n->pseudo_before)) out[k++] = n->pseudo_before;
    for (DomNode *c = n->first; c && k < max; c = c->next) {
        if (skip_node(c)) continue;
        if (c->type == DOM_ELEMENT && st(c)->display == DISP_CONTENTS && depth < 8) {
            k += collect_kids(c, out + k, max - k, depth + 1);
            continue;
        }
        out[k++] = c;
    }
    if (n->pseudo_after && k < max && !skip_node(n->pseudo_after)) out[k++] = n->pseudo_after;
    return k;
}

static DomNode **kid_array(DomNode *n, int *count)
{
    int cap = 2;
    for (DomNode *c = n->first; c; c = c->next) cap += 1;
    /* display:contents may add more: bound by MAX_KIDS */
    cap = cap * 4 + 8;
    if (cap > MAX_KIDS) cap = MAX_KIDS;
    DomNode **kids = (DomNode **)arena_alloc(L.a, sizeof(DomNode *) * (size_t)cap);
    if (!kids) { *count = 0; return NULL; }
    *count = collect_kids(n, kids, cap, 0);
    return kids;
}

static int32_t lenv(const CssLen *l, int32_t ref) { return l->kind == LEN_SET ? css_len(l, ref) : 0; }

typedef struct {
    int32_t m[4], b[4], p[4];   /* margins, borders, padding: top right bottom left */
    bool mauto_l, mauto_r;
} Edges;

static void edges(const CssStyle *s, int32_t cw, Edges *e)
{
    for (int i = 0; i < 4; i++) {
        e->m[i] = lenv(&s->margin[i], cw);
        e->p[i] = lenv(&s->padding[i], cw);
        if (e->p[i] < 0) e->p[i] = 0;
        e->b[i] = s->border_w[i] > 0 ? s->border_w[i] : 0;
    }
    e->mauto_l = s->margin[3].kind == LEN_AUTO;
    e->mauto_r = s->margin[1].kind == LEN_AUTO;
}

static int32_t pb_h(const Edges *e) { return e->p[1] + e->p[3] + e->b[1] + e->b[3]; }
static int32_t pb_v(const Edges *e) { return e->p[0] + e->p[2] + e->b[0] + e->b[2]; }

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */

static bool is_ws(char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f'; }

/* Prepare drawable text: transliterate code points the font lacks and
 * apply text-transform. Returns s itself when nothing changes. */
static const char *prepare_text(const char *s, uint32_t len, const CssStyle *cs, uint32_t *out_len)
{
    bool needs = cs->transform != TT_NONE;
    for (uint32_t i = 0; i < len && !needs; i++) {
        if ((unsigned char)s[i] < 0x80) continue;
        uint32_t cp;
        size_t n = web_utf8_decode((const unsigned char *)s + i, (const unsigned char *)s + len, &cp);
        if (cp == 0xA0 || !L.fm->has_glyph || !L.fm->has_glyph(cp)) needs = true;
        i += (uint32_t)n - 1;
    }
    if (!needs) { *out_len = len; return s; }
    char *o = (char *)arena_alloc(L.a, (size_t)len * 3 + 4);
    if (!o) { *out_len = len; return s; }
    uint32_t k = 0;
    bool word_start = true;
    for (uint32_t i = 0; i < len;) {
        uint32_t cp;
        size_t n = web_utf8_decode((const unsigned char *)s + i, (const unsigned char *)s + len, &cp);
        i += (uint32_t)n;
        if (cs->transform == TT_UPPER || (cs->transform == TT_CAPITALIZE && word_start)) {
            if (cp >= 'a' && cp <= 'z') cp -= 32;
            else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) cp -= 32;
        } else if (cs->transform == TT_LOWER) {
            if (cp >= 'A' && cp <= 'Z') cp += 32;
            else if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) cp += 32;
        }
        word_start = cp == ' ' || cp == '\n' || cp == '\t' || cp == '-';
        if (cp == 0xA0) { o[k++] = '\x01'; continue; }      /* nbsp: not a break opportunity */
        if (cp < 0x80 || (L.fm->has_glyph && L.fm->has_glyph(cp))) { k += (uint32_t)web_utf8_encode(cp, o + k); continue; }
        const char *t = web_translit(cp);
        while (*t && k < len * 3) o[k++] = *t++;
    }
    o[k] = '\0';
    *out_len = k;
    return o;
}

/* Width of text where '\x01' (nbsp) counts as a space. */
static int text_w(const char *s, int len, uint8_t font)
{
    bool nb = false;
    for (int i = 0; i < len; i++) if (s[i] == '\x01') { nb = true; break; }
    if (!nb) return L.fm->width(s, len, font);
    int w = 0, start = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || s[i] == '\x01') {
            w += L.fm->width(s + start, i - start, font);
            if (i < len) w += L.fm->width(" ", 1, font);
            start = i + 1;
        }
    }
    return w;
}

/* ------------------------------------------------------------------ */
/* Intrinsic widths                                                    */
/* ------------------------------------------------------------------ */

static void measure(DomNode *n, int32_t *mn, int32_t *mx, int depth);

static void measure_text(const DomNode *t, const CssStyle *cs, int32_t *mn, int32_t *mx)
{
    uint32_t len;
    const char *s = prepare_text(t->text, t->text_len, cs, &len);
    uint8_t f = font_of(cs);
    int space = L.fm->width(" ", 1, f);
    int32_t total = 0, widest = 0;
    bool pre = cs->white_space == WS_PRE || cs->white_space == WS_PRE_WRAP;
    bool nowrap = cs->white_space == WS_NOWRAP || cs->white_space == WS_PRE;
    if (cs->font_size <= 0) { *mn = *mx = 0; return; }
    uint32_t i = 0;
    int32_t line = 0;
    bool any = false;
    while (i < len) {
        if (pre && s[i] == '\n') { if (line > total) total = line; line = 0; i++; continue; }
        if (is_ws(s[i])) {
            if (any) line += space;
            any = false;
            while (i < len && is_ws(s[i]) && !(pre && s[i] == '\n')) i++;
            continue;
        }
        uint32_t j = i;
        while (j < len && !is_ws(s[j])) j++;
        int32_t w = text_w(s + i, (int)(j - i), f);
        if (w > widest) widest = w;
        line += w;
        any = true;
        i = j;
    }
    if (line > total) total = line;
    *mx = total;
    *mn = nowrap ? total : widest;
}

static int32_t replaced_width(DomNode *n, int32_t cw, int32_t *h_out);

static void measure_children(DomNode *n, int32_t *mn, int32_t *mx, int depth)
{
    int count;
    DomNode **kids = kid_array(n, &count);
    const CssStyle *s = st(n);
    int32_t run_min = 0, run_max = 0, best_min = 0, best_max = 0;
    bool flex_row = (s->display == DISP_FLEX || s->display == DISP_INLINE_FLEX) && !s->flex_dir_col;
    bool grid = s->display == DISP_GRID && s->grid_cols > 0 && s->grid_cols < 255;
    int items = 0;
    for (int i = 0; i < count; i++) {
        DomNode *c = kids[i];
        int32_t a, b;
        if (c->type == DOM_TEXT) {
            if (whitespace_only(c)) continue;
            measure_text(c, s, &a, &b);
        } else {
            measure(c, &a, &b, depth + 1);
        }
        items++;
        if (flex_row) {
            run_max += b + (items > 1 ? s->gap_col : 0);
            if (s->flex_wrap) { if (a > run_min) run_min = a; }
            else run_min += a + (items > 1 ? s->gap_col : 0);
            continue;
        }
        if (grid) {
            if (b > best_max) best_max = b;
            if (a > best_min) best_min = a;
            continue;
        }
        bool block = c->type == DOM_ELEMENT && is_block_level(c) && st(c)->float_ == FLOAT_NONE;
        if (block) {
            if (run_max > best_max) best_max = run_max;
            if (run_min > best_min) best_min = run_min;
            run_min = run_max = 0;
            if (b > best_max) best_max = b;
            if (a > best_min) best_min = a;
        } else {
            run_max += b;
            if (a > run_min) run_min = a;
        }
    }
    if (grid) {
        int nc = s->grid_cols;
        best_max = best_max * nc + s->gap_col * (nc - 1);
    }
    if (run_max > best_max) best_max = run_max;
    if (run_min > best_min) best_min = run_min;
    *mn = best_min;
    *mx = best_max;
}

static void measure_table(DomNode *n, int32_t *mn, int32_t *mx, int depth);

static void measure(DomNode *n, int32_t *mn, int32_t *mx, int depth)
{
    *mn = *mx = 0;
    if (n->type != DOM_ELEMENT || skip_node(n)) return;
    NodeInfo *ni = info(n);
    if (ni && ni->measured) { *mn = ni->minw; *mx = ni->maxw; return; }
    const CssStyle *s = st(n);
    Edges e;
    edges(s, 0, &e);
    int32_t extra = pb_h(&e) + (e.mauto_l ? 0 : e.m[3]) + (e.mauto_r ? 0 : e.m[1]);
    int32_t a = 0, b = 0;
    if (s->width.kind == LEN_SET && !s->width.pct) {
        a = b = s->width.px + (s->border_box ? 0 : pb_h(&e));
        extra -= pb_h(&e);
    } else if (is_replaced(n)) {
        int32_t h;
        a = b = replaced_width(n, 0, &h) + pb_h(&e);
        extra -= pb_h(&e);
    } else if (depth > LAYOUT_DEPTH) {
        a = b = 0;
    } else if (s->display == DISP_TABLE) {
        measure_table(n, &a, &b, depth);
    } else {
        measure_children(n, &a, &b, depth);
        if (s->white_space == WS_NOWRAP || s->white_space == WS_PRE) a = b;
    }
    if (s->max_w.kind == LEN_SET && !s->max_w.pct) {
        int32_t m = s->max_w.px + (s->border_box ? 0 : pb_h(&e));
        if (b > m) b = m;
        if (a > m) a = m;
    }
    if (s->min_w.kind == LEN_SET && !s->min_w.pct && s->min_w.px > 0) {
        int32_t m = s->min_w.px;
        if (a < m) a = m;
        if (b < m) b = m;
    }
    a += extra;
    b += extra;
    if (a < 0) a = 0;
    if (b < a) b = a;
    if (ni) { ni->minw = a; ni->maxw = b; ni->measured = true; }
    *mn = a;
    *mx = b;
}

/* ------------------------------------------------------------------ */
/* Box layout                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    int32_t w, h;               /* border box */
    int32_t mt, mb;             /* vertical margins (for collapsing) */
    int bg_item;                /* background rect item or -1 */
    int first_item;
} BoxOut;

static void layout_box(DomNode *n, int32_t x, int32_t y, int32_t cw, int32_t forced_w, int32_t forced_h, bool shrink,
                       BoxOut *out);
static int32_t layout_flow(DomNode *n, DomNode **kids, int count, int32_t x, int32_t y, int32_t w);

/* Background + border for a box; returns the background item (or -1). */
static int decorate(const CssStyle *s, const Edges *e, int32_t x, int32_t y, int32_t w, int32_t h, bool skip_bg)
{
    int bg = -1;
    if (s->invisible) return -1;
    int r = s->radius > 255 ? 255 : s->radius;
    if (r > w / 2) r = w / 2;
    if (r > h / 2 && h > 0) r = h / 2;
    if ((s->bg >> 24) && !skip_bg && !s->masked) {
        bg = emit(WI_RECT, x, y, w, h, s->bg);
        if (bg >= 0) L.d->items[bg].radius = (uint8_t)(r < 0 ? 0 : r);
    }
    bool uniform = e->b[0] == e->b[1] && e->b[1] == e->b[2] && e->b[2] == e->b[3] &&
                   s->border_c[0] == s->border_c[1] && s->border_c[1] == s->border_c[2] && s->border_c[2] == s->border_c[3];
    if (uniform && e->b[0] > 0 && (s->border_c[0] >> 24)) {
        int k = emit(WI_STROKE, x, y, w, h, s->border_c[0]);
        if (k >= 0) { L.d->items[k].ref = (int16_t)e->b[0]; L.d->items[k].radius = (uint8_t)(r < 0 ? 0 : r); }
    } else {
        if (e->b[0] > 0 && (s->border_c[0] >> 24)) emit(WI_RECT, x, y, w, e->b[0], s->border_c[0]);
        if (e->b[2] > 0 && (s->border_c[2] >> 24)) emit(WI_RECT, x, y + h - e->b[2], w, e->b[2], s->border_c[2]);
        if (e->b[3] > 0 && (s->border_c[3] >> 24)) emit(WI_RECT, x, y, e->b[3], h, s->border_c[3]);
        if (e->b[1] > 0 && (s->border_c[1] >> 24)) emit(WI_RECT, x + w - e->b[1], y, e->b[1], h, s->border_c[1]);
    }
    return bg;
}

/* ---- Lists ---- */

static int list_number(DomNode *li)
{
    DomNode *list = li->parent;
    int start = 1;
    const char *v;
    if (list && list->tag == TAG_OL && (v = dom_attr(list, "start"))) {
        int k = 0, sign = 1;
        if (*v == '-') { sign = -1; v++; }
        for (; *v >= '0' && *v <= '9' && k < 1000000; v++) k = k * 10 + (*v - '0');
        start = k * sign;
    }
    int n = start;
    for (DomNode *s = list ? list->first : NULL; s && s != li; s = s->next) {
        if (s->type != DOM_ELEMENT || s->tag != TAG_LI) continue;
        if ((v = dom_attr(s, "value"))) { int k = 0; for (; *v >= '0' && *v <= '9' && k < 1000000; v++) k = k * 10 + (*v - '0'); n = k; }
        n++;
    }
    if ((v = dom_attr(li, "value"))) { int k = 0; for (; *v >= '0' && *v <= '9' && k < 1000000; v++) k = k * 10 + (*v - '0'); n = k; }
    return n;
}

static void marker_text(uint8_t style, int n, char *out)
{
    out[0] = '\0';
    if (style == LIST_DECIMAL) { itoa(n, out, 10); strcpy(out + strlen(out), "."); return; }
    if ((style == LIST_LOWER_ALPHA || style == LIST_UPPER_ALPHA) && n >= 1) {
        char buf[8];
        int k = 0;
        int v = n;
        while (v > 0 && k < 6) { v--; buf[k++] = (char)((style == LIST_LOWER_ALPHA ? 'a' : 'A') + v % 26); v /= 26; }
        int o = 0;
        while (k) out[o++] = buf[--k];
        out[o++] = '.';
        out[o] = '\0';
        return;
    }
    if ((style == LIST_LOWER_ROMAN || style == LIST_UPPER_ROMAN) && n >= 1 && n < 4000) {
        static const int vals[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
        static const char *const syms[] = {"m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i"};
        int o = 0, v = n;
        for (int i = 0; i < 13; i++)
            while (v >= vals[i]) {
                for (const char *c = syms[i]; *c; c++) out[o++] = style == LIST_UPPER_ROMAN ? (char)(*c - 32) : *c;
                v -= vals[i];
            }
        out[o++] = '.';
        out[o] = '\0';
    }
}

static void emit_marker(int32_t baseline, int32_t A)
{
    if (!L.marker_pending) return;
    L.marker_pending = false;
    int16_t saved = L.link;
    L.link = L.marker_link;
    if (L.marker_style == LIST_DISC || L.marker_style == LIST_CIRCLE || L.marker_style == LIST_SQUARE) {
        int sz = L.fm->ascent(L.marker_font) / 3;
        if (sz < 4) sz = 4;
        int k = emit(WI_MARKER, L.marker_x - sz - 8, baseline - A / 2 - sz / 2 + 1, sz, sz, L.marker_color);
        if (k >= 0) L.d->items[k].ref = L.marker_style;
    } else {
        char t[24];
        marker_text(L.marker_style, L.marker_number, t);
        int len = (int)strlen(t);
        char *copy = arena_strndup(L.a, t, (size_t)len);
        int w = L.fm->width(t, len, L.marker_font);
        int k = copy ? emit(WI_TEXT, L.marker_x - w - 6, baseline - L.fm->ascent(L.marker_font), w,
                            L.fm->height(L.marker_font), L.marker_color) : -1;
        if (k >= 0) { L.d->items[k].text = copy; L.d->items[k].len = (uint32_t)len; L.d->items[k].font = L.marker_font; }
    }
    L.link = saved;
}

/* ------------------------------------------------------------------ */
/* Floats (exclusions of the current block formatting context)         */
/* ------------------------------------------------------------------ */

#define MAX_FLOATS 256

typedef struct {
    int32_t l, r, top, bottom;
    uint8_t side;
} FloatBox;

static FloatBox floats[MAX_FLOATS];
static int float_count, float_base;

/* Free horizontal band [*left, *right) at page rows [y, y+h) inside [x0, x0+w). */
static bool float_band(int32_t y, int32_t h, int32_t x0, int32_t w, int32_t *left, int32_t *right)
{
    *left = x0;
    *right = x0 + w;
    bool narrowed = false;
    if (h < 1) h = 1;
    for (int i = float_base; i < float_count; i++) {
        const FloatBox *f = &floats[i];
        if (f->bottom <= y || f->top >= y + h) continue;
        if (f->side == FLOAT_LEFT && f->r > *left) { *left = f->r; narrowed = true; }
        if (f->side == FLOAT_RIGHT && f->l < *right) { *right = f->l; narrowed = true; }
    }
    if (*right < *left) *right = *left;
    return narrowed;
}

/* Lowest float bottom strictly below y (-1 if none). */
static int32_t next_float_bottom(int32_t y)
{
    int32_t best = -1;
    for (int i = float_base; i < float_count; i++)
        if (floats[i].bottom > y && (best < 0 || floats[i].bottom < best)) best = floats[i].bottom;
    return best;
}

static int32_t floats_bottom(int sides)
{
    int32_t b = 0;
    for (int i = float_base; i < float_count; i++)
        if ((sides & (floats[i].side == FLOAT_LEFT ? 1 : 2)) && floats[i].bottom > b) b = floats[i].bottom;
    return b;
}

/* ------------------------------------------------------------------ */
/* Inline formatting                                                   */
/* ------------------------------------------------------------------ */

enum { AT_TEXT = 1, AT_BOX, AT_OPEN, AT_CLOSE, AT_SPACE };

typedef struct {
    uint8_t kind;
    uint8_t font;
    uint8_t deco;
    uint8_t valign;
    bool brk;                   /* a line may break before this atom */
    int16_t link;
    uint32_t color;
    const char *text;
    uint32_t len;
    int32_t sp;                 /* collapsible space before (px), dropped at a line start */
    int32_t w;                  /* advance (box: margin box; open/close: edge widths) */
    int32_t A, D;               /* above/below baseline */
    int item_first, item_end;   /* box: provisional items */
    int anchor_first, anchor_end;
    int32_t box_h;
    const DomNode *node;        /* OPEN/CLOSE: the inline element */
    int32_t x;                  /* final position on the line (flush) */
} Atom;

typedef struct {
    int32_t x0, y, w;           /* content box of the container (line area) */
    int32_t cursor_y;
    uint8_t align;
    int32_t strut_A, strut_D;
    int32_t indent;             /* text-indent for the first line */
    bool first_line;
    bool nowrap;
    /* current line */
    Atom atoms[LINE_ATOMS];
    int count;
    int32_t line_w;             /* sum of sp + w */
    bool line_empty;            /* no visible content yet */
    bool band_ok;
    int32_t lx, lw;             /* line area after floats (relative to x0) */
    /* inline elements open across lines */
    const DomNode *open[32];
    int open_count;
    int lines;
    /* whitespace state */
    bool pending_space;
    bool pending_wrap;          /* the pending space allows a break */
} Ifc;

static Ifc *ifc_stack[LAYOUT_DEPTH + 2];
static int ifc_depth;
static Atom moved_atoms[LINE_ATOMS];

static Ifc *ifc_alloc(void)
{
    if (ifc_depth >= LAYOUT_DEPTH + 2) return NULL;
    if (!ifc_stack[ifc_depth]) ifc_stack[ifc_depth] = (Ifc *)arena_alloc(L.a, sizeof(Ifc));
    Ifc *f = ifc_stack[ifc_depth];
    if (f) ifc_depth++;
    return f;
}

static void inline_edges(const DomNode *n, int32_t *left, int32_t *right)
{
    const CssStyle *s = st(n);
    Edges e;
    edges(s, 0, &e);
    *left = e.m[3] + e.b[3] + e.p[3];
    *right = e.m[1] + e.b[1] + e.p[1];
}

static bool inline_decorated(const DomNode *n)
{
    const CssStyle *s = st(n);
    if (s->invisible) return false;
    return ((s->bg >> 24) && !s->masked) || s->border_w[0] || s->border_w[1] || s->border_w[2] || s->border_w[3];
}

static void ensure_band(Ifc *f)
{
    if (f->band_ok) return;
    int32_t l, r;
    float_band(f->cursor_y, f->strut_A + f->strut_D, f->x0, f->w, &l, &r);
    f->lx = l - f->x0;
    f->lw = r - l;
    f->band_ok = true;
}

static int32_t line_room(Ifc *f)
{
    ensure_band(f);
    return f->lw - (f->first_line ? f->indent : 0);
}

/* Emit the inline element backgrounds for spans on this line. */
static void line_decorations(Ifc *f, int32_t baseline, int32_t origin)
{
    const DomNode *stack[32];
    int32_t start[32];
    bool carried[32];
    int depth = 0;
    for (int i = 0; i < f->open_count && depth < 32; i++) { stack[depth] = f->open[i]; start[depth] = 0; carried[depth] = true; depth++; }
    int32_t end_x = 0;
    for (int i = 0; i < f->count; i++) if (f->atoms[i].x + f->atoms[i].w > end_x) end_x = f->atoms[i].x + f->atoms[i].w;
    for (int i = 0; i <= f->count; i++) {
        Atom *a = i < f->count ? &f->atoms[i] : NULL;
        if (a && a->kind == AT_OPEN) {
            if (depth < 32) { stack[depth] = a->node; start[depth] = a->x; carried[depth] = false; depth++; }
            continue;
        }
        bool close_all = !a;
        if (!close_all && a->kind != AT_CLOSE) continue;
        while (depth > 0) {
            depth--;
            const DomNode *n = stack[depth];
            int32_t x1 = close_all ? end_x : a->x + a->w;
            if (inline_decorated(n)) {
                const CssStyle *s = st(n);
                Edges e;
                edges(s, 0, &e);
                uint8_t fnt = font_of(s);
                int asc = L.fm->ascent(fnt), h = L.fm->height(fnt);
                int32_t top = baseline - asc - e.p[0] - e.b[0];
                int32_t bot = baseline + (h - asc) + e.p[2] + e.b[2];
                int32_t x0 = start[depth] + (carried[depth] ? 0 : e.m[3]);
                int32_t w = x1 - x0 - (close_all ? 0 : e.m[1]);
                if (w > 0) {
                    int16_t saved = L.link;
                    decorate(s, &e, origin + x0, top, w, bot - top, false);
                    L.link = saved;
                }
            }
            if (!close_all) break;
        }
    }
}

static void flush_line(Ifc *f, bool forced)
{
    if (!f->count && !forced) return;
    ensure_band(f);
    int32_t A = f->strut_A, D = f->strut_D;
    bool visible = false;
    for (int i = 0; i < f->count; i++) {
        Atom *a = &f->atoms[i];
        if (a->kind == AT_TEXT || a->kind == AT_BOX) visible = true;
        if (a->kind == AT_TEXT || a->kind == AT_OPEN) {
            if (a->A > A) A = a->A;
            if (a->D > D) D = a->D;
        } else if (a->kind == AT_BOX) {
            int32_t ba = a->box_h, bd = 0;
            if (a->valign == VA_MIDDLE) { ba = a->box_h / 2 + f->strut_A / 3; bd = a->box_h - ba; }
            else if (a->valign == VA_TOP) { ba = f->strut_A; bd = a->box_h - ba; }
            else if (a->valign == VA_BOTTOM) { bd = f->strut_D; ba = a->box_h - bd; }
            if (ba > A) A = ba;
            if (bd > D) D = bd;
        }
    }
    if (!visible && !forced) {
        /* only spaces/open/close markers: keep the open state, no line */
        for (int i = 0; i < f->count; i++) {
            if (f->atoms[i].kind == AT_OPEN && f->open_count < 32) f->open[f->open_count++] = f->atoms[i].node;
            else if (f->atoms[i].kind == AT_CLOSE && f->open_count) f->open_count--;
        }
        f->count = 0;
        f->line_w = 0;
        return;
    }
    /* Pass 1: exact positions. Text runs that will be merged are measured as
     * one string, so spacing matches the single draw call. */
    int32_t pen = 0;
    for (int i = 0; i < f->count; i++) {
        Atom *a = &f->atoms[i];
        int32_t sp = (i == 0 || pen == 0) ? 0 : a->sp;
        if (a->kind != AT_TEXT) { a->x = pen + sp; pen = a->x + a->w; continue; }
        /* a run of compatible text atoms (plain spans in between are transparent) */
        int j = i + 1;
        while (j < f->count) {
            Atom *b = &f->atoms[j];
            if (b->kind == AT_OPEN || b->kind == AT_CLOSE) {
                if (inline_decorated(b->node) || b->w || b->sp) break;
                j++;
                continue;
            }
            if (b->kind != AT_TEXT || b->font != a->font || b->color != a->color || b->deco != a->deco ||
                b->link != a->link || b->valign != a->valign) break;
            j++;
        }
        a->x = pen + sp;
        int32_t run_end = a->x + a->w;
        if (j > i + 1) {
            /* place the merged atoms by prefix sums, then take the run's end
             * from measuring the joined text (what the single draw call does) */
            static char joined[8192];
            uint32_t k = 0;
            int32_t cursor = a->x + a->w;
            bool fits = true;
            for (int m = i; m < j; m++) {
                Atom *b = &f->atoms[m];
                if (b->kind != AT_TEXT) { if (m > i) b->x = cursor; continue; }
                if (m > i) {
                    int32_t space = b->sp ? L.fm->width(" ", 1, b->font) : 0;
                    b->x = cursor + space;
                    cursor = b->x + b->w;
                }
                if (k + b->len + 2 >= sizeof(joined)) { fits = false; continue; }
                if (m > i && b->sp) joined[k++] = ' ';
                memcpy(joined + k, b->text, b->len);
                k += b->len;
            }
            run_end = fits ? a->x + text_w(joined, (int)k, a->font) : cursor;
            /* keep the last atom's end consistent with the measured run */
            for (int m = j - 1; m > i; m--) if (f->atoms[m].kind == AT_TEXT) { f->atoms[m].w = run_end - f->atoms[m].x; break; }
        }
        pen = run_end;
        i = j - 1;
    }
    int32_t used = pen;
    int32_t avail = f->lw - (f->first_line ? f->indent : 0);
    int32_t offset = f->lx + (f->first_line ? f->indent : 0);
    if (f->align == ALIGN_CENTER && used < avail) offset += (avail - used) / 2;
    else if (f->align == ALIGN_RIGHT && used < avail) offset += avail - used;
    int32_t origin = f->x0 + offset;
    int32_t baseline = f->cursor_y + A;
    emit_marker(baseline, A);
    line_decorations(f, baseline, origin);
    /* Pass 2: emit merged text runs and place boxes */
    for (int i = 0; i < f->count; i++) {
        Atom *a = &f->atoms[i];
        if (a->kind == AT_BOX) {
            int32_t top = baseline - a->box_h;
            if (a->valign == VA_MIDDLE) top = baseline - a->box_h / 2 - f->strut_A / 3;
            else if (a->valign == VA_TOP) top = baseline - A;
            else if (a->valign == VA_BOTTOM) top = baseline + D - a->box_h;
            translate_range(a->item_first, a->item_end, a->anchor_first, a->anchor_end, origin + a->x, top);
            continue;
        }
        if (a->kind != AT_TEXT) continue;
        int j = i + 1;
        uint32_t total = a->len;
        int texts = 1;
        while (j < f->count) {
            Atom *b = &f->atoms[j];
            if (b->kind == AT_OPEN || b->kind == AT_CLOSE) {
                if (inline_decorated(b->node) || b->w || b->sp) break;
                j++;
                continue;
            }
            if (b->kind != AT_TEXT || b->font != a->font || b->color != a->color || b->deco != a->deco ||
                b->link != a->link || b->valign != a->valign) break;
            total += b->len + (b->sp ? 1 : 0);
            texts++;
            j++;
        }
        const char *text = a->text;
        uint32_t len = a->len;
        int32_t last_end = a->x + a->w;
        if (texts > 1) {
            char *buf = (char *)arena_alloc(L.a, total + 1);
            if (buf) {
                uint32_t k = 0;
                for (int m = i; m < j; m++) {
                    Atom *b = &f->atoms[m];
                    if (b->kind != AT_TEXT) continue;
                    if (m > i && b->sp) buf[k++] = ' ';
                    memcpy(buf + k, b->text, b->len);
                    k += b->len;
                    last_end = b->x + b->w;
                }
                buf[k] = '\0';
                text = buf;
                len = k;
            } else {
                j = i + 1;
            }
        }
        int32_t asc = L.fm->ascent(a->font);
        int32_t ty = baseline - asc;
        if (a->valign == VA_SUPER) ty -= asc / 3;
        else if (a->valign == VA_SUB) ty += asc / 4;
        int16_t saved = L.link;
        L.link = a->link;
        int k = emit(WI_TEXT, origin + a->x, ty, last_end - a->x, L.fm->height(a->font), a->color);
        L.link = saved;
        if (k >= 0) {
            WebItem *it = &L.d->items[k];
            it->text = text;
            it->len = len;
            it->font = a->font;
            it->deco = a->deco;
            L.text_chars += (int)len;
        }
        i = j - 1;
    }
    /* carry open elements to the next line */
    for (int i = 0; i < f->count; i++) {
        if (f->atoms[i].kind == AT_OPEN && f->open_count < 32) f->open[f->open_count++] = f->atoms[i].node;
        else if (f->atoms[i].kind == AT_CLOSE && f->open_count) f->open_count--;
    }
    f->cursor_y = baseline + D;
    f->count = 0;
    f->line_w = 0;
    f->line_empty = true;
    f->first_line = false;
    f->band_ok = false;
    f->lines++;
}

static Atom *push_atom(Ifc *f)
{
    if (f->count >= LINE_ATOMS) flush_line(f, false);
    if (f->count >= LINE_ATOMS) return NULL;
    Atom *a = &f->atoms[f->count++];
    memset(a, 0, sizeof(*a));
    a->link = L.link;
    return a;
}

static void recompute_line_w(Ifc *f)
{
    int32_t w = 0;
    for (int i = 0; i < f->count; i++) w += (i ? f->atoms[i].sp : 0) + f->atoms[i].w;
    f->line_w = w;
    f->line_empty = true;
    for (int i = 0; i < f->count; i++) if (f->atoms[i].kind == AT_TEXT || f->atoms[i].kind == AT_BOX) f->line_empty = false;
}

/* The next atom cannot start a line by itself (no break opportunity before
 * it): move the tail since the last break opportunity to a new line.
 * Returns false when there is no earlier break (the line overflows). */
static bool rewrap(Ifc *f)
{
    int k = -1;
    for (int i = f->count - 1; i > 0; i--) if (f->atoms[i].brk) { k = i; break; }
    if (k <= 0) return false;
    while (k > 1 && f->atoms[k - 1].kind == AT_OPEN && !f->atoms[k - 1].sp) k--;
    int n = f->count - k;
    memcpy(moved_atoms, &f->atoms[k], sizeof(Atom) * (size_t)n);
    f->count = k;
    recompute_line_w(f);
    flush_line(f, false);
    for (int i = 0; i < n && f->count < LINE_ATOMS; i++) f->atoms[f->count++] = moved_atoms[i];
    if (f->count) f->atoms[0].sp = 0;
    recompute_line_w(f);
    return true;
}

/* Make room for an atom of width w (sp before it). */
static void fit(Ifc *f, int32_t sp, int32_t w, bool brk, bool can_wrap)
{
    if (!can_wrap || f->line_empty) return;
    if (f->line_w + sp + w <= line_room(f)) return;
    if (brk) flush_line(f, false);
    else rewrap(f);
}

/* Lines beside floats too narrow for a word move below the float. */
static void clear_for(Ifc *f, int32_t w)
{
    for (int guard = 0; guard < 32 && f->line_empty && !f->count; guard++) {
        ensure_band(f);
        if (f->lw >= f->w || line_room(f) >= w) return;
        int32_t nb = next_float_bottom(f->cursor_y);
        if (nb < 0) return;
        f->cursor_y = nb;
        f->band_ok = false;
    }
}

static void add_word(Ifc *f, const char *s, uint32_t len, const CssStyle *cs, bool space_before, bool space_wrap,
                     bool may_wrap)
{
    uint8_t font = font_of(cs);
    int32_t A, D;
    line_metrics(cs, &A, &D);
    int32_t sp = space_before ? L.fm->width(" ", 1, font) : 0;
    int32_t w = text_w(s, (int)len, font);
    bool prev_box = f->count && f->atoms[f->count - 1].kind == AT_BOX;
    bool brk = (space_before && space_wrap) || prev_box;
    fit(f, sp, w, brk, may_wrap || brk);
    if (f->line_empty && !f->count) clear_for(f, w);
    /* a word wider than the whole line breaks between characters */
    while (may_wrap && f->line_empty && w > line_room(f) && len > 1) {
        uint32_t best = 0;
        int32_t room = line_room(f);
        for (uint32_t i = 0; i < len;) {
            uint32_t cp;
            size_t n = web_utf8_decode((const unsigned char *)s + i, (const unsigned char *)s + len, &cp);
            if (text_w(s, (int)(i + n), font) > room) break;
            i += (uint32_t)n;
            best = i;
        }
        if (!best) {
            uint32_t cp;
            best = (uint32_t)web_utf8_decode((const unsigned char *)s, (const unsigned char *)s + len, &cp);
        }
        if (best >= len) break;
        Atom *a = push_atom(f);
        if (!a) return;
        a->kind = AT_TEXT; a->font = font; a->deco = cs->decoration; a->valign = cs->valign;
        a->color = cs->color; a->text = s; a->len = best; a->A = A; a->D = D;
        a->w = text_w(s, (int)best, font);
        a->brk = true;
        f->line_w += a->w;
        f->line_empty = false;
        flush_line(f, false);
        s += best;
        len -= best;
        w = text_w(s, (int)len, font);
        sp = 0;
    }
    Atom *a = push_atom(f);
    if (!a) return;
    a->kind = AT_TEXT; a->font = font; a->deco = cs->decoration; a->valign = cs->valign;
    a->color = cs->color; a->text = s; a->len = len; a->A = A; a->D = D;
    a->sp = f->line_empty && !f->count ? 0 : sp;
    a->brk = brk;
    a->w = w;
    f->line_w += a->sp + w;
    f->line_empty = false;
}

static void add_text(Ifc *f, const DomNode *t, const CssStyle *cs)
{
    if (cs->font_size <= 0) return;
    if (!(cs->color >> 24) && !cs->invisible) return;
    uint32_t len;
    const char *s = prepare_text(t->text, t->text_len, cs, &len);
    uint8_t ws = cs->white_space;
    bool wrap = !f->nowrap && ws != WS_NOWRAP && ws != WS_PRE;
    if (ws == WS_PRE || ws == WS_PRE_WRAP) {
        uint32_t i = 0;
        while (i < len) {
            uint32_t j = i;
            while (j < len && s[j] != '\n') j++;
            const char *seg = s + i;
            uint32_t sl = j - i;
            bool tab = false;
            for (uint32_t k = 0; k < sl; k++) if (seg[k] == '\t' || seg[k] == '\r') { tab = true; break; }
            if (tab) {
                char *c = (char *)arena_alloc(L.a, sl * 8 + 1);
                if (c) {
                    uint32_t o = 0;
                    for (uint32_t k = 0; k < sl; k++) {
                        if (seg[k] == '\r') continue;
                        if (seg[k] == '\t') { do c[o++] = ' '; while (o % 8); }
                        else c[o++] = seg[k];
                    }
                    seg = c;
                    sl = o;
                }
            }
            if (sl) {
                if (ws == WS_PRE_WRAP) {
                    uint32_t k = 0;
                    bool first = true;
                    while (k < sl) {
                        uint32_t e = k;
                        while (e < sl && seg[e] != ' ') e++;
                        while (e < sl && seg[e] == ' ') e++;
                        add_word(f, seg + k, e - k, cs, false, true, true);
                        if (!first) f->atoms[f->count - 1].brk = true;
                        first = false;
                        k = e;
                    }
                } else {
                    add_word(f, seg, sl, cs, false, false, false);
                }
            }
            if (j < len) flush_line(f, true);
            i = j + 1;
        }
        f->pending_space = false;
        return;
    }
    uint32_t i = 0;
    while (i < len) {
        if (is_ws(s[i])) {
            bool newline = false;
            while (i < len && is_ws(s[i])) { if (s[i] == '\n') newline = true; i++; }
            if (ws == WS_PRE_LINE && newline) { flush_line(f, true); f->pending_space = false; continue; }
            if (!f->line_empty || f->count) { f->pending_space = true; f->pending_wrap = wrap; }
            continue;
        }
        uint32_t j = i;
        while (j < len && !is_ws(s[j])) j++;
        add_word(f, s + i, j - i, cs, f->pending_space, f->pending_wrap, wrap);
        f->pending_space = false;
        i = j;
    }
}

static void ifc_node(Ifc *f, DomNode *n, int depth);

static int16_t link_index(const DomNode *n)
{
    const char *href = dom_attr(n, "href");
    if (!href || !href[0] || strncmp(href, "javascript:", 11) == 0) return -1;
    WebDoc *d = L.d;
    if (d->link_count >= WEB_MAX_LINKS) return -1;
    d->links[d->link_count] = href;
    return (int16_t)d->link_count++;
}

static void ifc_atomic(Ifc *f, DomNode *n)
{
    const CssStyle *s = st(n);
    int item_first = L.d->item_count;
    int anchor_first = L.d->anchor_count;
    BoxOut bo;
    layout_box(n, 0, 0, f->w, -1, -1, true, &bo);
    int item_end = L.d->item_count, anchor_end = L.d->anchor_count;
    Edges e;
    edges(s, f->w, &e);
    int32_t w = bo.w + e.m[1] + e.m[3];
    translate_range(item_first, item_end, anchor_first, anchor_end, e.m[3], e.m[0]);
    int32_t sp = f->pending_space && !f->line_empty ? L.fm->width(" ", 1, font_of(st(n->parent))) : 0;
    bool brk = f->count > 0;            /* break opportunities exist around atomic inlines */
    if (s->white_space == WS_NOWRAP && !(f->pending_space && f->pending_wrap)) brk = f->count && f->atoms[f->count - 1].kind == AT_BOX;
    fit(f, sp, w, brk, !f->nowrap);
    if (f->line_empty && !f->count) { clear_for(f, w); sp = 0; }
    Atom *a = push_atom(f);
    if (!a) return;
    a->kind = AT_BOX;
    a->valign = s->valign;
    a->item_first = item_first;
    a->item_end = item_end;
    a->anchor_first = anchor_first;
    a->anchor_end = anchor_end;
    a->box_h = bo.h + e.m[0] + e.m[2];
    a->sp = f->line_empty && !f->count ? 0 : sp;
    a->brk = brk;
    a->w = w;
    f->line_w += a->sp + w;
    f->line_empty = false;
    f->pending_space = false;
}

static void ifc_node(Ifc *f, DomNode *n, int depth)
{
    if (n->type == DOM_TEXT) {
        add_text(f, n, st(n->parent));
        return;
    }
    if (skip_node(n)) return;
    const CssStyle *s = st(n);
    if (n->id) add_anchor(n->id, f->cursor_y);
    if (n->tag == TAG_A) { const char *nm = dom_attr(n, "name"); if (nm) add_anchor(nm, f->cursor_y); }
    if (n->tag == TAG_BR) {
        if (!f->count) {
            Atom *a = push_atom(f);
            if (a) a->kind = AT_SPACE;
        }
        flush_line(f, true);
        f->pending_space = false;
        return;
    }
    if (n->tag == TAG_WBR) {
        f->pending_space = false;
        return;
    }
    int16_t saved_link = L.link;
    if (n->tag == TAG_A) { int16_t li = link_index(n); if (li >= 0) L.link = li; }
    if (is_replaced(n) || s->display == DISP_INLINE_BLOCK || s->display == DISP_INLINE_FLEX ||
        n->tag == TAG_BUTTON || depth > LAYOUT_DEPTH) {
        if (depth <= LAYOUT_DEPTH) ifc_atomic(f, n);
        L.link = saved_link;
        return;
    }
    /* inline element: open marker, children, close marker */
    int32_t left, right;
    inline_edges(n, &left, &right);
    Atom *o = push_atom(f);
    if (o) {
        o->kind = AT_OPEN;
        o->node = n;
        line_metrics(s, &o->A, &o->D);
        if (left) {
            int32_t sp = f->pending_space && !f->line_empty ? L.fm->width(" ", 1, font_of(s)) : 0;
            o->sp = sp;
            o->brk = sp > 0 && f->pending_wrap;
            o->w = left;
            f->line_w += sp + left;
            f->pending_space = false;
        }
    }
    if (n->pseudo_before) ifc_node(f, n->pseudo_before, depth + 1);
    for (DomNode *c = n->first; c; c = c->next) ifc_node(f, c, depth + 1);
    if (n->pseudo_after) ifc_node(f, n->pseudo_after, depth + 1);
    Atom *cl = push_atom(f);
    if (cl) {
        cl->kind = AT_CLOSE;
        cl->node = n;
        cl->w = right;
        f->line_w += right;
    }
    L.link = saved_link;
}

/* Lay out a run of inline-level siblings as lines. Returns the height. */
static int32_t layout_inline_run(DomNode *container, DomNode **kids, int count, int32_t x, int32_t y, int32_t w)
{
    bool any = false;
    for (int i = 0; i < count; i++) if (!whitespace_only(kids[i])) { any = true; break; }
    if (!any) return 0;
    Ifc *f = ifc_alloc();
    if (!f) return 0;
    const CssStyle *cs = st(container);
    f->x0 = x;
    f->y = y;
    f->w = w > 1 ? w : 1;
    f->cursor_y = y;
    f->align = cs->text_align;
    f->indent = cs->text_indent;
    f->first_line = true;
    f->nowrap = cs->white_space == WS_NOWRAP || cs->white_space == WS_PRE;
    line_metrics(cs, &f->strut_A, &f->strut_D);
    f->count = 0;
    f->line_w = 0;
    f->line_empty = true;
    f->band_ok = false;
    f->open_count = 0;
    f->lines = 0;
    f->pending_space = false;
    f->pending_wrap = true;
    for (int i = 0; i < count; i++) ifc_node(f, kids[i], L.depth);
    flush_line(f, false);
    int32_t h = f->cursor_y - y;
    ifc_depth--;
    return h;
}

/* ------------------------------------------------------------------ */
/* Replaced elements                                                   */
/* ------------------------------------------------------------------ */

static int parse_int_attr(const DomNode *n, const char *name, int def)
{
    const char *v = dom_attr(n, name);
    if (!v) return def;
    int k = 0;
    bool any = false;
    while (*v == ' ') v++;
    for (; *v >= '0' && *v <= '9' && k < 100000; v++) { k = k * 10 + (*v - '0'); any = true; }
    if (*v == '%') return def;
    return any ? k : def;
}

/* Pick an image URL: src, lazy-loading attributes, srcset, <picture>. */
static const char *image_url(const DomNode *n, int32_t want_w)
{
    const char *src = dom_attr(n, "src");
    static const char *const lazy[] = {"data-src", "data-lazy-src", "data-original", "data-url", "data-hi-res-src"};
    if (!src || !src[0] || strncmp(src, "data:", 5) == 0) {
        for (unsigned i = 0; i < ARRAY_SIZE(lazy); i++) {
            const char *v = dom_attr(n, lazy[i]);
            if (v && v[0]) { src = v; break; }
        }
    }
    const char *srcset = dom_attr(n, "srcset");
    if (!srcset || !srcset[0]) srcset = dom_attr(n, "data-srcset");
    /* <picture><source srcset> before the <img> */
    if (n->parent && n->parent->tag == TAG_PICTURE) {
        for (const DomNode *s = n->parent->first; s; s = s->next) {
            if (s->tag != TAG_SOURCE) continue;
            const char *type = dom_attr(s, "type");
            if (type && !(strncmp(type, "image/jpeg", 10) == 0 || strncmp(type, "image/png", 9) == 0 ||
                          strncmp(type, "image/gif", 9) == 0)) continue;
            const char *media = dom_attr(s, "media");
            if (media && media[0]) continue;
            const char *ss = dom_attr(s, "srcset");
            if (ss && ss[0]) { srcset = ss; break; }
        }
    }
    if (srcset && srcset[0]) {
        /* candidates "url 640w" or "url 2x"; pick the smallest >= want (2x density) */
        static char best[WEB_URL_MAX];
        int best_w = -1, best_over = 0x7fffffff, largest = -1;
        static char largest_url[WEB_URL_MAX];
        const char *p = srcset;
        best[0] = largest_url[0] = '\0';
        while (*p) {
            while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t') p++;
            if (!*p) break;
            const char *us = p;
            while (*p && *p != ' ' && *p != '\n' && *p != '\t' && !(*p == ',' && (p[1] == ' ' || p[1] == '\n'))) p++;
            const char *ue = p;
            while (*p == ' ') p++;
            int val = 0;
            char unit = 'x';
            const char *dp = p;
            while (*dp >= '0' && *dp <= '9') { val = val * 10 + (*dp - '0'); if (val > 100000) val = 100000; dp++; }
            if (*dp == '.') { dp++; while (*dp >= '0' && *dp <= '9') dp++; }
            if (*dp == 'w' || *dp == 'x' || *dp == 'h') unit = *dp;
            if (dp == p) val = 1;
            while (*p && *p != ',') p++;
            if (ue - us <= 0 || (size_t)(ue - us) >= WEB_URL_MAX) continue;
            int score = unit == 'w' ? val : val * (want_w > 0 ? want_w : 400);
            int target = (want_w > 0 ? want_w : 400) * 2;
            if (score >= target && score < best_over) { best_over = score; best_w = score; memcpy(best, us, (size_t)(ue - us)); best[ue - us] = '\0'; }
            if (score > largest) { largest = score; memcpy(largest_url, us, (size_t)(ue - us)); largest_url[ue - us] = '\0'; }
        }
        if (best_w > 0) return best;
        if (largest > 0) return largest_url;
    }
    return src && src[0] ? src : NULL;
}

static int register_image(DomNode *n, const char *url)
{
    WebDoc *d = L.d;
    if (n->image) return n->image - 1;
    if (!url || strlen(url) >= WEB_URL_MAX) return -1;
    for (int i = 0; i < d->image_count; i++) if (strcmp(d->images[i].url, url) == 0) { n->image = (int16_t)(i + 1); return i; }
    if (d->image_count >= WEB_MAX_IMAGES) return -1;
    WebImage *im = &d->images[d->image_count];
    memset(im, 0, sizeof(*im));
    strcpy(im->url, url);
    n->image = (int16_t)(d->image_count + 1);
    return d->image_count++;
}

/* Content width of a replaced element (and its height through h_out). */
static int32_t replaced_width(DomNode *n, int32_t cw, int32_t *h_out)
{
    const CssStyle *s = st(n);
    int32_t w = -1, h = -1;
    int fs = s->font_size > 0 ? s->font_size : 16;
    if (s->width.kind == LEN_SET && (!s->width.pct || cw > 0)) w = css_len(&s->width, cw);
    if (s->height.kind == LEN_SET && !s->height.pct) h = s->height.px;
    int32_t iw = -1, ih = -1;               /* intrinsic */
    switch (n->tag) {
    case TAG_IMG: {
        int aw = parse_int_attr(n, "width", -1), ah = parse_int_attr(n, "height", -1);
        int idx = n->image ? n->image - 1 : register_image(n, image_url(n, w > 0 ? w : aw > 0 ? aw : 300));
        const WebImage *im = idx >= 0 ? &L.d->images[idx] : NULL;
        if (im && im->state == IMG_READY && im->w > 0) { iw = im->w; ih = im->h; }
        else if (aw > 0 && ah > 0) { iw = aw; ih = ah; }
        else if (im && im->state == IMG_FAILED) { iw = 0; ih = 0; }
        else { iw = aw > 0 ? aw : 0; ih = ah > 0 ? ah : 0; }
        /* attributes give the ratio before the image arrives */
        if (aw > 0 && ah > 0 && !(im && im->state == IMG_READY && im->w > 0)) { iw = aw; ih = ah; }
        int Z = L.d->zoom > 0 ? L.d->zoom : 100;
        if (!(im && im->state == IMG_READY && im->w > 0) || !(aw > 0)) { }
        iw = iw * Z / 100;
        ih = ih * Z / 100;
        break;
    }
    case TAG_INPUT: {
        const char *type = dom_attr(n, "type");
        if (type && (strcmp(type, "checkbox") == 0 || strcmp(type, "radio") == 0)) { iw = ih = fs * 13 / 16 + 1; break; }
        if (type && (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0 || strcmp(type, "reset") == 0 ||
                     strcmp(type, "image") == 0)) {
            int f = n->field ? n->field - 1 : -1;
            const char *label = f >= 0 ? L.d->fields[f].label : "Submit";
            iw = L.fm->width(label, (int)strlen(label), font_of(s));
            ih = L.fm->height(font_of(s));
            break;
        }
        int size = parse_int_attr(n, "size", 20);
        if (size < 2) size = 2;
        if (size > 120) size = 120;
        iw = size * fs * 55 / 100 + 4;
        if (iw > 600) iw = 600;
        ih = L.fm->height(font_of(s));
        break;
    }
    case TAG_SELECT: {
        int f = n->field ? n->field - 1 : -1;
        int widest = 40;
        if (f >= 0) for (int i = 0; i < L.d->fields[f].option_count; i++) {
            const char *o = L.d->fields[f].options[i];
            int ow = L.fm->width(o, (int)strlen(o), font_of(s));
            if (ow > widest) widest = ow;
        }
        iw = widest + fs + 8;
        ih = L.fm->height(font_of(s));
        break;
    }
    case TAG_TEXTAREA: {
        int cols = parse_int_attr(n, "cols", 20), rows = parse_int_attr(n, "rows", 2);
        if (cols > 200) cols = 200;
        if (rows > 60) rows = 60;
        iw = cols * fs * 55 / 100 + 4;
        ih = rows * L.fm->height(font_of(s)) + 4;
        break;
    }
    case TAG_SVG: case TAG_MATH: {
        int aw = parse_int_attr(n, "width", -1), ah = parse_int_attr(n, "height", -1);
        iw = aw > 0 ? aw : 0;
        ih = ah > 0 ? ah : 0;
        if (iw == 0 && ih == 0 && w < 0) { iw = 0; ih = 0; }
        break;
    }
    case TAG_IFRAME: case TAG_VIDEO: case TAG_CANVAS: case TAG_OBJECT: case TAG_EMBED: {
        int aw = parse_int_attr(n, "width", -1), ah = parse_int_attr(n, "height", -1);
        iw = aw > 0 ? aw : 300;
        ih = ah > 0 ? ah : 150;
        break;
    }
    case TAG_AUDIO:
        iw = dom_attr(n, "controls") ? 300 : 0;
        ih = dom_attr(n, "controls") ? 54 : 0;
        break;
    default:
        iw = ih = 0;
        break;
    }
    if (w < 0 && h < 0) { w = iw; h = ih; }
    else if (w < 0) { w = ih > 0 ? (int32_t)((int64_t)h * iw / ih) : iw; }
    else if (h < 0) { h = iw > 0 ? (int32_t)((int64_t)w * ih / iw) : ih; }
    /* max-width: 100% and friends, keeping the aspect ratio */
    if (s->max_w.kind == LEN_SET && (cw > 0 || !s->max_w.pct)) {
        int32_t m = css_len(&s->max_w, cw);
        if (w > m && m >= 0) { if (w > 0) h = (int32_t)((int64_t)h * m / w); w = m; }
    }
    if (s->max_h.kind == LEN_SET && !s->max_h.pct) {
        int32_t m = s->max_h.px;
        if (h > m && m >= 0) { if (h > 0) w = (int32_t)((int64_t)w * m / h); h = m; }
    }
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (w > 20000) w = 20000;
    if (h > 20000) h = 20000;
    *h_out = h;
    return w;
}

static void layout_replaced(DomNode *n, int32_t x, int32_t y, int32_t cw, BoxOut *out)
{
    const CssStyle *s = st(n);
    Edges e;
    edges(s, cw, &e);
    int32_t h;
    int32_t w = replaced_width(n, cw, &h);
    if (s->border_box && s->width.kind == LEN_SET && (n->tag == TAG_INPUT || n->tag == TAG_SELECT || n->tag == TAG_TEXTAREA)) {
        w -= pb_h(&e);
        if (s->height.kind == LEN_SET) h -= pb_v(&e);
        if (w < 0) w = 0;
        if (h < 0) h = 0;
    }
    int32_t bw = w + pb_h(&e), bh = h + pb_v(&e);
    out->w = bw;
    out->h = bh;
    out->first_item = L.d->item_count;
    out->bg_item = decorate(s, &e, x, y, bw, bh, false);
    int32_t cx = x + e.b[3] + e.p[3], cy = y + e.b[0] + e.p[0];
    if (s->invisible) return;
    switch (n->tag) {
    case TAG_IMG: {
        int idx = n->image ? n->image - 1 : -1;
        if (idx >= 0 && L.d->images[idx].state == IMG_FAILED && w <= 1) {
            /* broken image: show nothing (alt text below when there is room) */
        }
        if (idx >= 0 && w > 0 && h > 0) {
            int k = emit(WI_IMAGE, cx, cy, w, h, 0);
            if (k >= 0) L.d->items[k].ref = (int16_t)idx;
        }
        break;
    }
    case TAG_INPUT: case TAG_SELECT: case TAG_TEXTAREA: {
        int f = n->field ? n->field - 1 : -1;
        if (f >= 0) {
            int k = emit(WI_FIELD, cx, cy, w, h, s->color);
            if (k >= 0) { L.d->items[k].ref = (int16_t)f; L.d->items[k].font = font_of(s); }
            L.d->fields[f].font = font_of(s);
            L.d->fields[f].color = s->color;
        }
        break;
    }
    case TAG_IFRAME: case TAG_VIDEO: case TAG_CANVAS: case TAG_OBJECT: case TAG_EMBED: {
        if (w > 0 && h > 0) {
            uint32_t c = n->tag == TAG_VIDEO ? 0xFF1F1F1Fu : 0xFFEEF1F5u;
            int k = emit(WI_RECT, cx, cy, w, h, c);
            (void)k;
            const char *label = n->tag == TAG_VIDEO ? "Video" : n->tag == TAG_IFRAME ? "Embedded page" : "Embedded content";
            uint8_t font = font_of(s);
            int lw = L.fm->width(label, (int)strlen(label), font);
            if (lw < w) {
                int t = emit(WI_TEXT, cx + (w - lw) / 2, cy + (h - L.fm->height(font)) / 2, lw, L.fm->height(font),
                             n->tag == TAG_VIDEO ? 0xFFDDDDDDu : 0xFF5F6368u);
                if (t >= 0) { L.d->items[t].text = label; L.d->items[t].len = (uint32_t)strlen(label); L.d->items[t].font = font; }
            }
        }
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Float placement                                                     */
/* ------------------------------------------------------------------ */

/* Lay out a floated box and place it at the highest position >= y where it
 * fits beside earlier floats in [x, x+w). Content flows around it. */
static void place_float(DomNode *n, int32_t x, int32_t y, int32_t w)
{
    const CssStyle *s = st(n);
    Edges e;
    edges(s, w, &e);
    int item0 = L.d->item_count, anchor0 = L.d->anchor_count;
    BoxOut bo;
    if (is_replaced(n)) layout_replaced(n, 0, 0, w, &bo);
    else layout_box(n, 0, 0, w, -1, -1, true, &bo);
    int32_t mw = bo.w + e.m[1] + e.m[3], mh = bo.h + e.m[0] + e.m[2];
    int32_t py = y, l = x, r = x + w;
    for (int guard = 0; guard < 64; guard++) {
        bool narrowed = float_band(py, mh, x, w, &l, &r);
        if (!narrowed || r - l >= mw) break;
        int32_t nb = next_float_bottom(py);
        if (nb < 0) break;
        py = nb;
    }
    int32_t px = s->float_ == FLOAT_RIGHT ? r - mw : l;
    translate(item0, anchor0, px + e.m[3], py + e.m[0]);
    if (float_count < MAX_FLOATS) {
        FloatBox *f = &floats[float_count++];
        f->l = px;
        f->r = px + mw;
        f->top = py;
        f->bottom = py + mh;
        f->side = s->float_ == FLOAT_RIGHT ? FLOAT_RIGHT : FLOAT_LEFT;
    }
}

/* Boxes that start a new block formatting context: floats inside them stay
 * inside, and they avoid floats outside. */
static bool establishes_bfc(const DomNode *n, const CssStyle *s)
{
    if (n->tag == TAG_HTML || n->tag == TAG_BUTTON) return true;
    if (s->float_ != FLOAT_NONE || s->clip) return true;
    switch (s->display) {
        case DISP_INLINE_BLOCK: case DISP_INLINE_FLEX: case DISP_FLEX: case DISP_GRID: case DISP_TABLE:
        case DISP_TABLE_CELL: case DISP_TABLE_CAPTION:
            return true;
        default: break;
    }
    const CssStyle *p = n->parent ? st(n->parent) : NULL;
    return p && (p->display == DISP_FLEX || p->display == DISP_INLINE_FLEX || p->display == DISP_GRID);
}

/* ------------------------------------------------------------------ */
/* Flexbox                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    DomNode *n;
    int32_t base, minc, maxc, size;     /* main size of the margin box */
    int32_t ml, mr;                     /* main-axis margins */
    bool auto_l, auto_r;
    int32_t cross;                      /* margin-box cross size */
    int item0, anchor0;
    BoxOut bo;
    int16_t grow, shrink;
    int16_t order;
} FlexItem;

static FlexItem *flex_buf[LAYOUT_DEPTH + 2];
static int flex_depth;

static int32_t layout_flex(DomNode *n, const CssStyle *s, DomNode **kids, int count, int32_t x, int32_t y, int32_t w,
                           int32_t fixed_h)
{
    if (flex_depth >= LAYOUT_DEPTH + 2) return layout_flow(n, kids, count, x, y, w);
    if (!flex_buf[flex_depth]) flex_buf[flex_depth] = (FlexItem *)arena_alloc(L.a, sizeof(FlexItem) * FLEX_ITEMS);
    FlexItem *it = flex_buf[flex_depth];
    if (!it) return layout_flow(n, kids, count, x, y, w);
    flex_depth++;
    int ni = 0;
    for (int i = 0; i < count && ni < FLEX_ITEMS; i++) {
        if (whitespace_only(kids[i])) continue;
        memset(&it[ni], 0, sizeof(FlexItem));
        it[ni].n = kids[i];
        it[ni].order = kids[i]->type == DOM_ELEMENT ? st(kids[i])->order : 0;
        ni++;
    }
    /* stable sort by order */
    for (int i = 1; i < ni; i++) {
        FlexItem t = it[i];
        int j = i;
        while (j > 0 && it[j - 1].order > t.order) { it[j] = it[j - 1]; j--; }
        it[j] = t;
    }
    int32_t total_h = 0;
    if (s->flex_dir_col) {
        /* column: stack, stretch or align horizontally */
        int32_t cy = y;
        for (int i = 0; i < ni; i++) {
            DomNode *c = it[i].n;
            if (c->type == DOM_TEXT) {
                DomNode *one[1] = {c};
                cy += layout_inline_run(n, one, 1, x, cy, w);
                continue;
            }
            const CssStyle *cs = st(c);
            Edges e;
            edges(cs, w, &e);
            uint8_t al = cs->align_self != 255 ? cs->align_self : s->align_items;
            bool stretch = al == FLEX_STRETCH && cs->width.kind != LEN_SET;
            int item0 = L.d->item_count, anchor0 = L.d->anchor_count;
            BoxOut bo;
            layout_box(c, x, cy + e.m[0], w, stretch ? w - e.m[1] - e.m[3] : -1, -1, !stretch, &bo);
            int32_t mw = bo.w + e.m[1] + e.m[3];
            int32_t dx = 0;
            if (e.mauto_l && e.mauto_r) dx = (w - mw) / 2;
            else if (e.mauto_l) dx = w - mw;
            else if (al == FLEX_CENTER) dx = (w - mw) / 2;
            else if (al == FLEX_END) dx = w - mw;
            if (dx > 0) translate(item0, anchor0, dx, 0);
            cy += bo.h + e.m[0] + e.m[2] + (i + 1 < ni ? s->gap_row : 0);
        }
        total_h = cy - y;
        flex_depth--;
        return total_h;
    }
    /* row: hypothetical sizes */
    for (int i = 0; i < ni; i++) {
        FlexItem *f = &it[i];
        DomNode *c = f->n;
        if (c->type == DOM_TEXT) {
            int32_t a, b;
            measure_text(c, s, &a, &b);
            f->base = b; f->minc = a; f->maxc = b;
            f->grow = 0; f->shrink = 100;
            continue;
        }
        const CssStyle *cs = st(c);
        Edges e;
        edges(cs, w, &e);
        int32_t a, b;
        measure(c, &a, &b, L.depth + 1);
        int32_t margins = (e.mauto_l ? 0 : e.m[3]) + (e.mauto_r ? 0 : e.m[1]);
        f->ml = e.mauto_l ? 0 : e.m[3];
        f->mr = e.mauto_r ? 0 : e.m[1];
        f->auto_l = e.mauto_l;
        f->auto_r = e.mauto_r;
        int32_t pbw = cs->border_box ? 0 : pb_h(&e);
        if (cs->flex_basis.kind == LEN_SET) f->base = css_len(&cs->flex_basis, w) + pbw + margins;
        else if (cs->width.kind == LEN_SET) f->base = css_len(&cs->width, w) + pbw + margins;
        else f->base = b;
        if (cs->flex_basis.kind == LEN_SET && cs->flex_grow == 0 && f->base < a && cs->width.kind != LEN_SET) f->base = a;
        f->minc = (cs->clip || cs->min_w.kind == LEN_SET) ? margins + pb_h(&e) : a;
        if (cs->min_w.kind == LEN_SET && (cs->min_w.px || cs->min_w.pct)) {
            int32_t m = css_len(&cs->min_w, w) + pbw + margins;
            if (m > f->minc) f->minc = m;
        }
        if (cs->width.kind == LEN_SET && cs->flex_basis.kind != LEN_SET && f->minc > f->base && !cs->clip) f->minc = f->base;
        f->maxc = 0x3fffffff;
        if (cs->max_w.kind == LEN_SET) f->maxc = css_len(&cs->max_w, w) + pbw + margins;
        if (f->base > f->maxc) f->base = f->maxc;
        if (f->base < f->minc) f->base = f->minc;
        f->grow = cs->flex_grow;
        f->shrink = cs->flex_shrink;
    }
    /* lines */
    int32_t cy = y;
    int start = 0;
    int line_no = 0;
    while (start < ni) {
        int end = start;
        int32_t used = 0;
        while (end < ni) {
            int32_t add = it[end].base + (end > start ? s->gap_col : 0);
            if (s->flex_wrap && end > start && used + add > w) break;
            used += add;
            end++;
        }
        int32_t free = w - used;
        /* grow / shrink */
        for (int i = start; i < end; i++) it[i].size = it[i].base;
        if (free > 0) {
            int32_t grow_total = 0;
            for (int i = start; i < end; i++) grow_total += it[i].grow;
            if (grow_total > 0) {
                int32_t given = 0;
                for (int i = start; i < end; i++) {
                    int32_t add = (int32_t)((int64_t)free * it[i].grow / (grow_total > 100 ? grow_total : 100));
                    if (it[i].size + add > it[i].maxc) add = it[i].maxc - it[i].size;
                    it[i].size += add;
                    given += add;
                }
                free -= given;
            }
        } else if (free < 0) {
            for (int pass = 0; pass < 3 && free < 0; pass++) {
                int64_t scaled = 0;
                for (int i = start; i < end; i++) if (it[i].size > it[i].minc) scaled += (int64_t)it[i].shrink * it[i].base;
                if (!scaled) break;
                int32_t taken = 0;
                for (int i = start; i < end; i++) {
                    if (it[i].size <= it[i].minc) continue;
                    int32_t cut = (int32_t)(-(int64_t)free * it[i].shrink * it[i].base / scaled);
                    if (it[i].size - cut < it[i].minc) cut = it[i].size - it[i].minc;
                    it[i].size -= cut;
                    taken += cut;
                }
                free += taken;
                if (!taken) break;
            }
        }
        /* auto margins absorb free space, else justify-content */
        int autos = 0;
        for (int i = start; i < end; i++) autos += it[i].auto_l + it[i].auto_r;
        int32_t lead = 0, between = 0;
        int n_items = end - start;
        if (free > 0 && !autos) {
            switch (s->justify) {
                case FLEX_END: lead = free; break;
                case FLEX_CENTER: lead = free / 2; break;
                case FLEX_BETWEEN: between = n_items > 1 ? free / (n_items - 1) : 0; break;
                case FLEX_AROUND: between = free / n_items; lead = between / 2; break;
                case FLEX_EVENLY: between = free / (n_items + 1); lead = between; break;
                default: break;
            }
        }
        int32_t auto_share = autos && free > 0 ? free / autos : 0;
        /* lay out items */
        int32_t cx = x + lead;
        int32_t line_h = 0;
        for (int i = start; i < end; i++) {
            FlexItem *f = &it[i];
            if (f->auto_l) cx += auto_share;
            f->item0 = L.d->item_count;
            f->anchor0 = L.d->anchor_count;
            if (f->n->type == DOM_TEXT) {
                DomNode *one[1] = {f->n};
                int32_t h = layout_inline_run(n, one, 1, cx, cy, f->size > 0 ? f->size : 1);
                f->cross = h;
                f->bo.bg_item = -1;
                f->bo.h = h;
            } else {
                const CssStyle *cs = st(f->n);
                Edges e;
                edges(cs, w, &e);
                layout_box(f->n, cx + f->ml, cy + e.m[0], w, f->size - f->ml - f->mr, -1, false, &f->bo);
                f->cross = f->bo.h + e.m[0] + e.m[2];
            }
            if (f->cross > line_h) line_h = f->cross;
            cx += f->size + s->gap_col + between;
            if (f->auto_r) cx += auto_share;
        }
        if (!s->flex_wrap && fixed_h > line_h && line_no == 0) line_h = fixed_h;
        /* cross-axis alignment */
        for (int i = start; i < end; i++) {
            FlexItem *f = &it[i];
            uint8_t al = s->align_items;
            const CssStyle *cs = f->n->type == DOM_ELEMENT ? st(f->n) : NULL;
            if (cs && cs->align_self != 255) al = cs->align_self;
            int32_t dy = 0;
            if (al == FLEX_CENTER) dy = (line_h - f->cross) / 2;
            else if (al == FLEX_END) dy = line_h - f->cross;
            else if (al == FLEX_STRETCH && cs && cs->height.kind != LEN_SET && f->bo.bg_item >= 0) {
                Edges e;
                edges(cs, w, &e);
                int32_t nh = line_h - e.m[0] - e.m[2];
                WebDoc *d = L.d;
                int32_t top = d->items[f->bo.bg_item].y, old = d->items[f->bo.bg_item].h;
                for (int k = f->item0; k < d->item_count; k++) {
                    WebItem *wi = &d->items[k];
                    if ((wi->kind == WI_RECT || wi->kind == WI_STROKE) && wi->y == top && wi->h == old) wi->h = nh;
                    else if (wi->kind == WI_RECT && wi->y == top + old - e.b[2] && e.b[2] && wi->h == e.b[2]) wi->y = top + nh - e.b[2];
                }
            }
            if (dy > 0) {
                int item_to = i + 1 < end ? it[i + 1].item0 : L.d->item_count;
                int anchor_to = i + 1 < end ? it[i + 1].anchor0 : L.d->anchor_count;
                translate_range(f->item0, item_to, f->anchor0, anchor_to, 0, dy);
            }
        }
        cy += line_h + (end < ni ? s->gap_row : 0);
        start = end;
        line_no++;
    }
    total_h = cy - y;
    flex_depth--;
    return total_h;
}

/* ------------------------------------------------------------------ */
/* Grid (equal columns) and tables                                     */
/* ------------------------------------------------------------------ */

#define GRID_COLS 64
#define GRID_ROWS 256

typedef struct {
    DomNode *n;
    int c0, span, r0, rspan;
    int item0, anchor0, item_end, anchor_end;
    BoxOut bo;
    int32_t mh;                 /* margin-box height */
} GridItem;

static int32_t track_fixed(const CssTrack *t, int32_t w) { return t->px + (int32_t)((int64_t)t->pct * w / 10000); }

static int32_t layout_grid(DomNode *n, const CssStyle *s, DomNode **kids, int count, int32_t x, int32_t y, int32_t w)
{
    const CssGrid *g = s->grid;
    int ncols = 0;
    static CssTrack tracks_buf[LAYOUT_DEPTH + 2][GRID_COLS];
    CssTrack *tracks = tracks_buf[L.depth < LAYOUT_DEPTH ? L.depth : LAYOUT_DEPTH];
    int items_n = 0;
    for (int i = 0; i < count; i++) if (!whitespace_only(kids[i])) items_n++;
    if (g && (g->n || g->auto_repeat)) {
        int rep = 0;
        if (g->auto_repeat) {
            int32_t other = 0;
            for (int i = 0; i < g->n; i++) other += g->t[i].kind == TK_FIXED ? track_fixed(&g->t[i], w) : 0;
            int32_t minw = g->rep.min_kind == TK_FIXED ? g->rep.min_px + (int32_t)((int64_t)g->rep.min_pct * w / 10000)
                                                       : g->rep.kind == TK_FIXED ? track_fixed(&g->rep, w) : 200;
            if (minw < 1) minw = 1;
            rep = (int)((w - other + s->gap_col) / (minw + s->gap_col));
            if (rep < 1) rep = 1;
            if (g->auto_repeat == 2 && rep > items_n && items_n > 0) rep = items_n;
        }
        for (int i = 0; i <= g->n && ncols < GRID_COLS; i++) {
            if (g->auto_repeat && i == g->rep_at) for (int r = 0; r < rep && ncols < GRID_COLS; r++) tracks[ncols++] = g->rep;
            if (i < g->n && ncols < GRID_COLS) tracks[ncols++] = g->t[i];
        }
    }
    if (g && g->area_cols > ncols) {
        for (int i = ncols; i < g->area_cols && i < GRID_COLS; i++) { memset(&tracks[i], 0, sizeof(CssTrack)); tracks[i].kind = TK_FR; tracks[i].fr = 100; tracks[i].min_kind = TK_AUTO; }
        ncols = g->area_cols < GRID_COLS ? g->area_cols : GRID_COLS;
    }
    if (ncols <= 0) {
        /* no column template: one column, row gaps between items */
        int32_t cy = y;
        int placed = 0;
        for (int i = 0; i < count; i++) {
            DomNode *c = kids[i];
            if (whitespace_only(c)) continue;
            if (placed++) cy += s->gap_row;
            if (c->type == DOM_TEXT) { DomNode *one[1] = {c}; cy += layout_inline_run(n, one, 1, x, cy, w); continue; }
            Edges e;
            edges(st(c), w, &e);
            BoxOut bo;
            layout_box(c, x, cy + e.m[0], w, -1, -1, false, &bo);
            cy += bo.h + e.m[0] + e.m[2];
        }
        return cy - y;
    }
    GridItem *it = (GridItem *)arena_alloc(L.a, sizeof(GridItem) * (size_t)(items_n ? items_n : 1));
    uint64_t *occ = (uint64_t *)arena_alloc(L.a, sizeof(uint64_t) * GRID_ROWS);
    if (!it || !occ) return 0;
    /* placement */
    int ni = 0, cur_r = 0, cur_c = 0, nrows = 0;
    for (int k = 0; k < count; k++) {
        DomNode *c = kids[k];
        if (whitespace_only(c)) continue;
        GridItem *gi = &it[ni++];
        memset(gi, 0, sizeof(*gi));
        gi->n = c;
        const CssStyle *cs = c->type == DOM_ELEMENT ? st(c) : NULL;
        int c0 = -1, span = 1, r0 = -1, rspan = 1;
        if (cs && cs->grid_area && g) {
            for (int a = 0; a < g->narea; a++) {
                if (strcmp(g->areas[a].name, cs->grid_area) != 0) continue;
                c0 = g->areas[a].c0; span = g->areas[a].c1 - g->areas[a].c0;
                r0 = g->areas[a].r0; rspan = g->areas[a].r1 - g->areas[a].r0;
            }
        }
        if (cs && c0 < 0) {
            if (cs->gc_start > 0) c0 = cs->gc_start - 1;
            else if (cs->gc_start < 0) c0 = ncols + 1 + cs->gc_start;
            if (cs->gc_end) {
                int end_line = cs->gc_end > 0 ? cs->gc_end : ncols + 2 + cs->gc_end;
                if (c0 < 0) c0 = end_line - 1 - (cs->gc_span ? cs->gc_span : 1);
                span = end_line - 1 - c0;
            } else if (cs->gc_span) span = cs->gc_span;
            if (cs->gr_start > 0) r0 = cs->gr_start - 1;
            if (cs->gr_span) rspan = cs->gr_span;
        }
        if (span < 1) span = 1;
        if (span > ncols) span = ncols;
        if (c0 >= ncols) c0 = ncols - 1;
        if (c0 >= 0 && c0 + span > ncols) span = ncols - c0;
        if (rspan < 1) rspan = 1;
        if (rspan > 16) rspan = 16;
        uint64_t mask_cols;
        if (r0 < 0) {
            /* auto placement from the cursor, row-major */
            int r = cur_r, col = c0 >= 0 ? c0 : cur_c;
            for (int guard = 0; guard < GRID_ROWS * 2; guard++) {
                if (c0 < 0 && col + span > ncols) { r++; col = 0; continue; }
                if (r >= GRID_ROWS - rspan) break;
                mask_cols = (span >= 64 ? ~0ull : ((1ull << span) - 1)) << col;
                bool free = true;
                for (int rr = r; rr < r + rspan; rr++) if (occ[rr] & mask_cols) { free = false; break; }
                if (free) break;
                if (c0 >= 0) r++;
                else { col++; if (col + span > ncols) { r++; col = 0; } }
            }
            r0 = r;
            if (c0 < 0) c0 = col;
            if (c0 < 0) c0 = 0;
            cur_r = r0;
            cur_c = c0 + span;
        }
        if (r0 >= GRID_ROWS - rspan) r0 = GRID_ROWS - rspan - 1;
        if (c0 < 0) c0 = 0;
        mask_cols = (span >= 64 ? ~0ull : ((1ull << span) - 1)) << c0;
        for (int rr = r0; rr < r0 + rspan; rr++) occ[rr] |= mask_cols;
        gi->c0 = c0; gi->span = span; gi->r0 = r0; gi->rspan = rspan;
        if (r0 + rspan > nrows) nrows = r0 + rspan;
    }
    /* column widths */
    int32_t cw[GRID_COLS], cmin[GRID_COLS];
    int32_t avail = w - s->gap_col * (ncols - 1);
    int32_t used = 0;
    int64_t fr_sum = 0;
    for (int c = 0; c < ncols; c++) {
        CssTrack *t = &tracks[c];
        cmin[c] = 0;
        int32_t content_min = 0, content_max = 0;
        if (t->kind != TK_FIXED || t->min_kind != TK_FIXED) {
            for (int i = 0; i < ni; i++) {
                if (it[i].c0 != c || it[i].span != 1) continue;
                int32_t a, b;
                if (it[i].n->type == DOM_TEXT) measure_text(it[i].n, s, &a, &b);
                else measure(it[i].n, &a, &b, L.depth + 1);
                if (a > content_min) content_min = a;
                if (b > content_max) content_max = b;
            }
        }
        if (t->min_kind == TK_FIXED) cmin[c] = t->min_px + (int32_t)((int64_t)t->min_pct * w / 10000);
        else cmin[c] = t->kind == TK_FR ? content_min : content_min;
        switch (t->kind) {
            case TK_FIXED: cw[c] = track_fixed(t, w); if (cw[c] < cmin[c]) cw[c] = cmin[c]; break;
            case TK_MINC: cw[c] = content_min; break;
            case TK_MAXC: case TK_AUTO: cw[c] = content_max; break;
            default: cw[c] = cmin[c]; fr_sum += t->fr; break;
        }
        if (cmin[c] < 0) cmin[c] = 0;
        if (cw[c] < 0) cw[c] = 0;
        used += cw[c];
    }
    int32_t free_space = avail - used;
    if (fr_sum > 0 && free_space > 0) {
        /* fr tracks share the space left; a track whose minimum is larger keeps it */
        int32_t share_space = avail;
        int64_t frs = fr_sum;
        for (int c = 0; c < ncols; c++) if (tracks[c].kind != TK_FR) share_space -= cw[c];
        for (int pass = 0; pass < 3; pass++) {
            int64_t per = frs > 0 ? (int64_t)share_space * 100 / (frs < 100 ? 100 : frs) : 0;
            bool changed = false;
            for (int c = 0; c < ncols; c++) {
                if (tracks[c].kind != TK_FR || tracks[c].fr < 0) continue;
                int32_t want = (int32_t)(per * tracks[c].fr / 100);
                if (want < cmin[c] && tracks[c].fr > 0) {
                    cw[c] = cmin[c];
                    share_space -= cmin[c];
                    frs -= tracks[c].fr;
                    tracks[c].fr = -1;          /* frozen */
                    changed = true;
                }
            }
            if (!changed) {
                for (int c = 0; c < ncols; c++) if (tracks[c].kind == TK_FR && tracks[c].fr > 0) cw[c] = (int32_t)(per * tracks[c].fr / 100);
                break;
            }
        }
    } else if (free_space > 0) {
        /* no fr: auto tracks stretch to fill (justify-content: normal) */
        int autos = 0;
        for (int c = 0; c < ncols; c++) if (tracks[c].kind == TK_AUTO) autos++;
        if (autos) for (int c = 0; c < ncols; c++) if (tracks[c].kind == TK_AUTO) cw[c] += free_space / autos;
    } else if (free_space < 0) {
        /* shrink flexible/auto tracks towards their minimum */
        int32_t room = 0;
        for (int c = 0; c < ncols; c++) if (tracks[c].kind != TK_FIXED) room += cw[c] - cmin[c];
        int32_t need = -free_space;
        for (int c = 0; c < ncols && room > 0; c++) {
            if (tracks[c].kind == TK_FIXED) continue;
            int32_t cut = (int32_t)((int64_t)need * (cw[c] - cmin[c]) / room);
            cw[c] -= cut;
        }
    }
    int32_t cx[GRID_COLS + 1];
    cx[0] = x;
    for (int c = 0; c < ncols; c++) cx[c + 1] = cx[c] + cw[c] + s->gap_col;
    /* rows, in order */
    int32_t *rh = (int32_t *)arena_alloc(L.a, sizeof(int32_t) * (size_t)(nrows + 1));
    int32_t *ry = (int32_t *)arena_alloc(L.a, sizeof(int32_t) * (size_t)(nrows + 2));
    if (!rh || !ry) return 0;
    int32_t cy = y;
    for (int r = 0; r < nrows; r++) {
        ry[r] = cy;
        rh[r] = 0;
        for (int i = 0; i < ni; i++) {
            GridItem *gi = &it[i];
            if (gi->r0 != r) continue;
            int32_t width = cx[gi->c0 + gi->span] - cx[gi->c0] - s->gap_col;
            gi->item0 = L.d->item_count;
            gi->anchor0 = L.d->anchor_count;
            if (gi->n->type == DOM_TEXT) {
                DomNode *one[1] = {gi->n};
                gi->mh = layout_inline_run(n, one, 1, cx[gi->c0], cy, width);
                gi->bo.bg_item = -1;
            } else {
                Edges e;
                edges(st(gi->n), width, &e);
                layout_box(gi->n, cx[gi->c0] + e.m[3], cy + e.m[0], width, width - e.m[1] - e.m[3], -1, false, &gi->bo);
                gi->mh = gi->bo.h + e.m[0] + e.m[2];
            }
            gi->item_end = L.d->item_count;
            gi->anchor_end = L.d->anchor_count;
            if (gi->rspan == 1 && gi->mh > rh[r]) rh[r] = gi->mh;
        }
        /* items ending in this row may need more room */
        for (int i = 0; i < ni; i++) {
            GridItem *gi = &it[i];
            if (gi->rspan <= 1 || gi->r0 + gi->rspan - 1 != r) continue;
            int32_t have = cy + rh[r] - ry[gi->r0];
            if (gi->mh > have) rh[r] += gi->mh - have;
        }
        cy += rh[r] + (r + 1 < nrows ? s->gap_row : 0);
    }
    ry[nrows] = cy + s->gap_row;
    /* stretch / align items in their areas */
    for (int i = 0; i < ni; i++) {
        GridItem *gi = &it[i];
        if (gi->n->type != DOM_ELEMENT) continue;
        const CssStyle *cs = st(gi->n);
        int32_t area_h = ry[gi->r0 + gi->rspan] - ry[gi->r0] - s->gap_row;
        if (gi->r0 + gi->rspan >= nrows) area_h = cy - ry[gi->r0];
        uint8_t al = cs->align_self != 255 ? cs->align_self : s->align_items;
        if (al == FLEX_CENTER && gi->mh < area_h) translate_range(gi->item0, gi->item_end, gi->anchor0, gi->anchor_end, 0, (area_h - gi->mh) / 2);
        else if (al == FLEX_END && gi->mh < area_h) translate_range(gi->item0, gi->item_end, gi->anchor0, gi->anchor_end, 0, area_h - gi->mh);
        else if ((al == FLEX_STRETCH || al == FLEX_START) && al == FLEX_STRETCH && cs->height.kind != LEN_SET && gi->bo.bg_item >= 0 && gi->mh < area_h) {
            Edges e;
            edges(cs, w, &e);
            WebDoc *d = L.d;
            int32_t top = d->items[gi->bo.bg_item].y, old = d->items[gi->bo.bg_item].h, nh = area_h - e.m[0] - e.m[2];
            for (int k = gi->item0; k < gi->item_end; k++) {
                WebItem *wi = &d->items[k];
                if ((wi->kind == WI_RECT || wi->kind == WI_STROKE) && wi->y == top && wi->h == old) wi->h = nh;
            }
        }
    }
    return cy - y;
}

typedef struct {
    DomNode *cell;
    int col, span;
} TCell;

typedef struct {
    DomNode *row;
    TCell *cells;
    int ncells;
} TRow;

static int table_rows(DomNode *table, TRow *rows, int max_rows, DomNode **caption)
{
    int nr = 0;
    *caption = NULL;
    int count;
    DomNode **kids = kid_array(table, &count);
    for (int i = 0; i < count && nr < max_rows; i++) {
        DomNode *k = kids[i];
        if (k->type != DOM_ELEMENT) continue;
        uint8_t d = st(k)->display;
        if (d == DISP_TABLE_CAPTION) { if (!*caption) *caption = k; continue; }
        if (d == DISP_TABLE_ROW_GROUP) {
            int rc;
            DomNode **rk = kid_array(k, &rc);
            for (int j = 0; j < rc && nr < max_rows; j++) if (rk[j]->type == DOM_ELEMENT) rows[nr++].row = rk[j];
            continue;
        }
        rows[nr++].row = k;     /* table-row (or anything else: treated as a row) */
    }
    for (int r = 0; r < nr; r++) {
        int cc;
        DomNode **ck = kid_array(rows[r].row, &cc);
        int real = 0;
        for (int j = 0; j < cc; j++) if (ck[j]->type == DOM_ELEMENT) real++;
        rows[r].cells = real ? (TCell *)arena_alloc(L.a, sizeof(TCell) * (size_t)real) : NULL;
        rows[r].ncells = 0;
        int col = 0;
        for (int j = 0; j < cc && rows[r].cells; j++) {
            if (ck[j]->type != DOM_ELEMENT) continue;
            TCell *t = &rows[r].cells[rows[r].ncells++];
            t->cell = ck[j];
            t->col = col;
            t->span = parse_int_attr(ck[j], "colspan", 1);
            if (t->span < 1) t->span = 1;
            if (t->span > TABLE_COLS) t->span = TABLE_COLS;
            col += t->span;
        }
    }
    return nr;
}

#define MAX_TROWS 1500

static void table_columns(DomNode *table, TRow *rows, int nr, int *ncols, int32_t *cmin, int32_t *cmax, int32_t *cfix, int depth)
{
    int nc = 0;
    for (int c = 0; c < TABLE_COLS; c++) { cmin[c] = cmax[c] = 0; cfix[c] = -1; }
    (void)table;
    for (int pass = 0; pass < 2; pass++) {
        for (int r = 0; r < nr; r++) {
            for (int k = 0; k < rows[r].ncells; k++) {
                TCell *t = &rows[r].cells[k];
                if (t->col >= TABLE_COLS) continue;
                if ((pass == 0) != (t->span == 1)) continue;
                int end = t->col + t->span;
                if (end > TABLE_COLS) end = TABLE_COLS;
                if (end > nc) nc = end;
                int32_t a, b;
                measure(t->cell, &a, &b, depth + 1);
                const CssStyle *cs = st(t->cell);
                if (t->span == 1) {
                    if (cs->width.kind == LEN_SET && !cs->width.pct) {
                        Edges e;
                        edges(cs, 0, &e);
                        int32_t fw = cs->width.px + (cs->border_box ? 0 : pb_h(&e));
                        if (fw > cfix[t->col]) cfix[t->col] = fw;
                    }
                    if (a > cmin[t->col]) cmin[t->col] = a;
                    if (b > cmax[t->col]) cmax[t->col] = b;
                } else {
                    int32_t smin = 0, smax = 0;
                    for (int c = t->col; c < end; c++) { smin += cmin[c]; smax += cmax[c]; }
                    int span = end - t->col;
                    if (a > smin) for (int c = t->col; c < end; c++) cmin[c] += (a - smin) / span;
                    if (b > smax) for (int c = t->col; c < end; c++) cmax[c] += (b - smax) / span;
                }
            }
        }
    }
    for (int c = 0; c < nc; c++) {
        if (cfix[c] >= 0) { if (cfix[c] > cmin[c]) cmin[c] = cfix[c]; cmax[c] = cmin[c] > cfix[c] ? cmin[c] : cfix[c]; }
        if (cmax[c] < cmin[c]) cmax[c] = cmin[c];
    }
    *ncols = nc;
}

static void measure_table(DomNode *n, int32_t *mn, int32_t *mx, int depth)
{
    static TRow *rows_m;
    if (!rows_m) rows_m = NULL;
    TRow *rows = (TRow *)arena_alloc(L.a, sizeof(TRow) * 256);
    if (!rows) { *mn = *mx = 0; return; }
    DomNode *cap;
    int nr = table_rows(n, rows, 256, &cap);
    int32_t cmin[TABLE_COLS], cmax[TABLE_COLS], cfix[TABLE_COLS];
    int nc;
    table_columns(n, rows, nr, &nc, cmin, cmax, cfix, depth);
    int32_t sp = st(n)->border_spacing;
    int32_t a = sp * (nc + 1), b = sp * (nc + 1);
    for (int c = 0; c < nc; c++) { a += cmin[c]; b += cmax[c]; }
    *mn = a;
    *mx = b;
}

static int32_t layout_table(DomNode *n, const CssStyle *s, int32_t x, int32_t y, int32_t w)
{
    TRow *rows = (TRow *)arena_alloc(L.a, sizeof(TRow) * MAX_TROWS);
    if (!rows) return 0;
    DomNode *caption;
    int nr = table_rows(n, rows, MAX_TROWS, &caption);
    int32_t cmin[TABLE_COLS], cmax[TABLE_COLS], cfix[TABLE_COLS], cw[TABLE_COLS];
    int nc;
    table_columns(n, rows, nr, &nc, cmin, cmax, cfix, L.depth);
    int32_t sp = s->border_spacing;
    int32_t cy = y;
    if (caption) {
        BoxOut bo;
        layout_box(caption, x, cy, w, w, -1, false, &bo);
        cy += bo.h;
    }
    if (!nc) return cy - y;
    int32_t avail = w - sp * (nc + 1);
    int32_t smin = 0, smax = 0;
    for (int c = 0; c < nc; c++) { smin += cmin[c]; smax += cmax[c]; }
    bool fill = s->width.kind == LEN_SET;
    if (smax <= avail) {
        int32_t extra = fill ? avail - smax : 0;
        /* distribute extra to non-fixed columns proportionally to their max */
        int32_t flex_total = 0;
        for (int c = 0; c < nc; c++) if (cfix[c] < 0) flex_total += cmax[c] > 0 ? cmax[c] : 1;
        for (int c = 0; c < nc; c++) {
            cw[c] = cmax[c];
            if (extra > 0 && cfix[c] < 0 && flex_total) cw[c] += (int32_t)((int64_t)extra * (cmax[c] > 0 ? cmax[c] : 1) / flex_total);
        }
        if (extra > 0 && !flex_total) for (int c = 0; c < nc; c++) cw[c] += extra / nc;
    } else if (smin >= avail) {
        for (int c = 0; c < nc; c++) cw[c] = cmin[c];
    } else {
        int32_t room = avail - smin;
        int64_t span = smax - smin;
        for (int c = 0; c < nc; c++) cw[c] = cmin[c] + (int32_t)(span ? (int64_t)room * (cmax[c] - cmin[c]) / span : 0);
    }
    int32_t total = sp;
    for (int c = 0; c < nc; c++) total += cw[c] + sp;
    cy += sp;
    for (int r = 0; r < nr; r++) {
        TRow *row = &rows[r];
        const CssStyle *rs = st(row->row);
        int row_item = -1;
        if ((rs->bg >> 24) && !rs->invisible) row_item = emit(WI_RECT, x, cy, total, 0, rs->bg);
        int32_t row_h = 0;
        if (rs->height.kind == LEN_SET && !rs->height.pct) row_h = rs->height.px;
        int first_cell_item = L.d->item_count;
        int item_starts[TABLE_COLS + 1], anchor_starts[TABLE_COLS + 1];
        BoxOut outs[TABLE_COLS];
        int ncell = row->ncells < TABLE_COLS ? row->ncells : TABLE_COLS;
        for (int k = 0; k < ncell; k++) {
            TCell *t = &row->cells[k];
            if (t->col >= nc) { item_starts[k] = L.d->item_count; anchor_starts[k] = L.d->anchor_count; outs[k].h = 0; outs[k].bg_item = -1; continue; }
            int32_t cx = x + sp;
            for (int c = 0; c < t->col; c++) cx += cw[c] + sp;
            int32_t width = 0;
            int end = t->col + t->span > nc ? nc : t->col + t->span;
            for (int c = t->col; c < end; c++) width += cw[c] + (c > t->col ? sp : 0);
            item_starts[k] = L.d->item_count;
            anchor_starts[k] = L.d->anchor_count;
            layout_box(t->cell, cx, cy, width, width, -1, false, &outs[k]);
            if (outs[k].h > row_h) row_h = outs[k].h;
        }
        item_starts[ncell] = L.d->item_count;
        (void)first_cell_item;
        /* stretch cell backgrounds, vertical alignment */
        for (int k = 0; k < ncell; k++) {
            const CssStyle *cs = st(row->cells[k].cell);
            WebDoc *d = L.d;
            int32_t ch = outs[k].h;
            if (ch < row_h) {
                for (int m = item_starts[k]; m < item_starts[k + 1] && m < d->item_count; m++) {
                    WebItem *wi = &d->items[m];
                    if ((wi->kind == WI_RECT || wi->kind == WI_STROKE) && wi->y == cy && wi->h == ch) wi->h = row_h;
                }
                int32_t dy = cs->valign == VA_TOP ? 0 : cs->valign == VA_BOTTOM ? row_h - ch :
                             cs->valign == VA_BASELINE && row->cells[k].cell->tag != TAG_TD && row->cells[k].cell->tag != TAG_TH ? 0 :
                             (row_h - ch) / 2;
                if (cs->valign == VA_BASELINE && (row->cells[k].cell->tag == TAG_TD || row->cells[k].cell->tag == TAG_TH)) {
                    /* UA: cells are vertically centred unless the page says otherwise */
                    const CssStyle *ps = st(row->row);
                    dy = ps->valign == VA_TOP ? 0 : ps->valign == VA_BOTTOM ? row_h - ch : (row_h - ch) / 2;
                }
                if (dy > 0) {
                    for (int m = item_starts[k]; m < item_starts[k + 1] && m < d->item_count; m++) {
                        WebItem *wi = &d->items[m];
                        if ((wi->kind == WI_RECT || wi->kind == WI_STROKE) && wi->y == cy && wi->h == row_h) continue;
                        wi->y += dy;
                    }
                    int a_end = k + 1 < ncell ? anchor_starts[k + 1] : d->anchor_count;
                    for (int m = anchor_starts[k]; m < a_end; m++) d->anchors[m].y += dy;
                }
            }
        }
        if (row_item >= 0) L.d->items[row_item].h = row_h;
        cy += row_h + sp;
    }
    return cy - y;
}

/* ------------------------------------------------------------------ */
/* Block flow                                                          */
/* ------------------------------------------------------------------ */

static int32_t layout_flow(DomNode *n, DomNode **kids, int count, int32_t x, int32_t y, int32_t w)
{
    int32_t cy = y;
    int32_t pending_mb = 0;     /* previous sibling's bottom margin (collapsing) */
    bool first = true;
    int i = 0;
    while (i < count) {
        DomNode *c = kids[i];
        if (c->type == DOM_TEXT || !is_block_level(c)) {
            int j = i;
            while (j < count && (kids[j]->type == DOM_TEXT || !is_block_level(kids[j]))) j++;
            int32_t h = layout_inline_run(n, kids + i, j - i, x, cy + pending_mb, w);
            if (h > 0) { cy += pending_mb + h; pending_mb = 0; first = false; }
            i = j;
            continue;
        }
        const CssStyle *cs = st(c);
        if (cs->float_ != FLOAT_NONE) {
            place_float(c, x, cy + (pending_mb > 0 ? pending_mb : 0), w);
            i++;
            continue;
        }
        Edges e;
        edges(cs, w, &e);
        int32_t mt = e.m[0];
        int32_t gap = first && n->tag != TAG_HTML ? mt : (mt > pending_mb ? mt : pending_mb);
        if (mt < 0 || pending_mb < 0) gap = mt + pending_mb;
        int32_t top = cy + gap;
        if (cs->clear) {
            int32_t fb = floats_bottom(cs->clear);
            if (fb > top) top = fb;
        }
        int32_t bx = x, bw = w;
        if (establishes_bfc(c, cs) && float_count > float_base) {
            int32_t l, r;
            if (float_band(top, 1, x, w, &l, &r)) { bx = l; bw = r - l; }
        }
        BoxOut bo;
        layout_box(c, bx, top, bw, -1, -1, false, &bo);
        cy = top + bo.h;
        pending_mb = e.m[2];
        first = false;
        i++;
    }
    return cy + (pending_mb > 0 ? pending_mb : 0) - y;
}

/* Lay out the children of n inside its content box. */
static int32_t layout_children(DomNode *n, const CssStyle *s, int32_t x, int32_t y, int32_t w, int32_t fixed_h)
{
    int count;
    DomNode **kids = kid_array(n, &count);
    if (!kids || !count) return 0;
    if (L.depth > LAYOUT_DEPTH) return layout_inline_run(n, kids, count, x, y, w);
    switch (s->display) {
        case DISP_FLEX: case DISP_INLINE_FLEX: return layout_flex(n, s, kids, count, x, y, w, fixed_h);
        case DISP_GRID: return layout_grid(n, s, kids, count, x, y, w);
        case DISP_TABLE: return layout_table(n, s, x, y, w);
        default: return layout_flow(n, kids, count, x, y, w);
    }
}

static bool is_positioned(const CssStyle *s) { return s->position != POS_STATIC; }

/* Absolutely positioned descendants whose containing block is `cb` (the
 * nearest positioned ancestor): laid out after cb's content, painted on top.
 * Fixed boxes use the page's initial containing block; bottom-anchored fixed
 * bars (cookie banners, chat widgets) are skipped: there is no way to close
 * them without scripts. */
static void layout_abs_descendants(DomNode *cb, int32_t cbx, int32_t cby, int32_t cbw, int32_t cbh, bool root)
{
    if (L.depth > LAYOUT_DEPTH) return;
    for (DomNode *c = cb->first; c; ) {
        bool skip_children = false;
        if (c->type == DOM_ELEMENT) {
            const CssStyle *s = c->style;
            if (!s || s->display == DISP_NONE || c->tag == TAG_SCRIPT || c->tag == TAG_STYLE || c->tag == TAG_TEMPLATE ||
                c->tag == TAG_HEAD || is_replaced(c)) {
                skip_children = true;
                if (s && out_of_flow(s) && is_replaced(c) && s->display != DISP_NONE) goto place;
            } else if (closed_details_content(c)) {
                skip_children = true;
            } else if (out_of_flow(s)) {
            place:
                skip_children = true;
                if (s->invisible) { c = dom_next(c, cb, true); continue; }
                if (s->position == POS_FIXED && !root) { c = dom_next(c, cb, true); continue; }
                if (s->position == POS_FIXED && s->inset[2].kind == LEN_SET && s->inset[0].kind != LEN_SET) {
                    c = dom_next(c, cb, true);
                    continue;
                }
                Edges e;
                edges(s, cbw, &e);
                bool l = s->inset[3].kind == LEN_SET, r = s->inset[1].kind == LEN_SET;
                bool t = s->inset[0].kind == LEN_SET, b = s->inset[2].kind == LEN_SET;
                int32_t left = l ? css_len(&s->inset[3], cbw) : 0, right = r ? css_len(&s->inset[1], cbw) : 0;
                int32_t top = t ? css_len(&s->inset[0], cbh) : 0, bottom = b ? css_len(&s->inset[2], cbh) : 0;
                /* far off-screen tricks (left:-9999px) stay hidden */
                if (left < -2000 || top < -2000 || left > 20000 || top > 50000) { c = dom_next(c, cb, true); continue; }
                int32_t forced_w = -1;
                if (l && r && s->width.kind != LEN_SET) forced_w = cbw - left - right - e.m[1] - e.m[3];
                int item0 = L.d->item_count, anchor0 = L.d->anchor_count;
                int16_t saved_link = L.link;
                BoxOut bo;
                L.depth++;
                layout_box(c, 0, 0, cbw, forced_w >= 0 ? forced_w : -1, -1, forced_w < 0, &bo);
                L.depth--;
                L.link = saved_link;
                int32_t mw = bo.w + e.m[1] + e.m[3], mh = bo.h + e.m[0] + e.m[2];
                int32_t px = l ? cbx + left : r ? cbx + cbw - right - mw : cbx;
                int32_t py = t ? cby + top : b ? cby + cbh - bottom - mh : cby;
                translate(item0, anchor0, px + e.m[3], py + e.m[0]);
            } else if (is_positioned(s) && c != cb) {
                skip_children = true;           /* has its own containing block */
            }
        }
        c = dom_next(c, cb, skip_children);
    }
}

static void layout_box(DomNode *n, int32_t x, int32_t y, int32_t cw, int32_t forced_w, int32_t forced_h, bool shrink,
                       BoxOut *out)
{
    memset(out, 0, sizeof(*out));
    out->bg_item = -1;
    out->first_item = L.d->item_count;
    if (L.d->item_count >= L.d->item_cap) return;
    const CssStyle *s = st(n);
    if (is_replaced(n)) { layout_replaced(n, x, y, cw, out); return; }
    L.depth++;
    Edges e;
    edges(s, cw, &e);
    int32_t pbw = pb_h(&e);
    int32_t content_w;
    if (forced_w >= 0) {
        content_w = forced_w - pbw;
    } else if (s->width.kind == LEN_SET && (s->width.pct == 0 || cw > 0)) {
        content_w = css_len(&s->width, cw) - (s->border_box ? pbw : 0);
    } else {
        int32_t avail = cw - e.m[1] - e.m[3] - pbw;
        if (shrink || s->display == DISP_TABLE || s->display == DISP_INLINE_BLOCK || s->display == DISP_INLINE_FLEX ||
            n->tag == TAG_BUTTON) {
            int32_t a, b;
            measure(n, &a, &b, L.depth);
            int32_t m = (e.mauto_l ? 0 : e.m[3]) + (e.mauto_r ? 0 : e.m[1]);
            a -= pbw + m;
            b -= pbw + m;
            content_w = b < avail ? b : (a > avail ? a : avail);
            if (s->display == DISP_TABLE && s->width.kind != LEN_SET && b > avail) content_w = avail > a ? avail : a;
        } else {
            content_w = avail;
        }
    }
    if (s->max_w.kind == LEN_SET && (!s->max_w.pct || cw > 0)) {
        int32_t m = css_len(&s->max_w, cw) - (s->border_box ? pbw : 0);
        if (content_w > m) content_w = m;
    }
    if (s->min_w.kind == LEN_SET && (s->min_w.px || s->min_w.pct)) {
        int32_t m = css_len(&s->min_w, cw) - (s->border_box ? pbw : 0);
        if (content_w < m) content_w = m;
    }
    if (content_w < 0) content_w = 0;
    int32_t bw = content_w + pbw;
    /* horizontal position: auto margins centre a box with a set width */
    int32_t bx = x + e.m[3];
    const CssStyle *ps = n->parent ? st(n->parent) : NULL;
    if (forced_w < 0 && !shrink && ps && ps->center_blocks && s->margin[1].kind == LEN_SET && s->margin[3].kind == LEN_SET &&
        !e.m[1] && !e.m[3] && bw < cw) {
        bx = x + (cw - bw) / 2;         /* <center> / align=center centre child blocks */
    } else if (forced_w < 0 && !shrink) {
        int32_t free = cw - bw - (e.mauto_l ? 0 : e.m[3]) - (e.mauto_r ? 0 : e.m[1]);
        if (e.mauto_l && e.mauto_r && free > 0) bx = x + free / 2;
        else if (e.mauto_l && free > 0) bx = x + free;
        else if (e.mauto_l || e.mauto_r) bx = x + (e.mauto_l ? 0 : e.m[3]);
    } else if (forced_w >= 0) {
        bx = x;
    } else {
        bx = x;
    }
    if (n->id) add_anchor(n->id, y);
    int16_t saved_link = L.link;
    if (n->tag == TAG_A) { int16_t li = link_index(n); if (li >= 0) L.link = li; }
    bool is_body = n->tag == TAG_BODY, is_html = n->tag == TAG_HTML;
    int deco_from = L.d->item_count;
    int anchor_from = L.d->anchor_count;
    int bg = -1;
    if (!is_html && !(is_body && L.d->bg == s->bg)) {
        bg = decorate(s, &e, bx, y, bw, 0, false);
    } else if (!is_html) {
        bg = decorate(s, &e, bx, y, bw, 0, true);
    }
    /* invisible link area for block links (cards) */
    if (n->tag == TAG_A && L.link >= 0) emit(WI_RECT, bx, y, bw, 0, 0);
    int deco_to = L.d->item_count;
    /* list marker */
    bool marker = s->display == DISP_LIST_ITEM && s->list_style != LIST_NONE;
    if (marker) {
        L.marker_pending = true;
        L.marker_style = s->list_style;
        L.marker_number = s->list_style >= LIST_DECIMAL ? list_number(n) : 0;
        L.marker_x = bx + e.b[3] + e.p[3];
        L.marker_color = s->color;
        L.marker_font = font_of(s);
        L.marker_link = L.link;
    }
    int32_t fixed_h = -1;
    if (forced_h >= 0) fixed_h = forced_h - pb_v(&e);
    else if (s->height.kind == LEN_SET && !s->height.pct) fixed_h = s->height.px - (s->border_box ? pb_v(&e) : 0);
    bool clip = s->clip && (fixed_h >= 0 || (s->max_h.kind == LEN_SET && !s->max_h.pct));
    int clip_item = -1;
    if (clip) clip_item = emit(WI_CLIP, bx, y, bw, 0, 0);
    int32_t cx = bx + e.b[3] + e.p[3], cy = y + e.b[0] + e.p[0];
    bool bfc = establishes_bfc(n, s);
    int saved_count = float_count, saved_base = float_base;
    if (bfc) float_base = float_count;
    int32_t ch = layout_children(n, s, cx, cy, content_w, fixed_h);
    if (bfc) {
        /* a formatting-context root contains its floats */
        int32_t fb = floats_bottom(3);
        if (float_count > float_base && fb - cy > ch) ch = fb - cy;
        float_count = saved_count;
        float_base = saved_base;
    }
    if (marker && L.marker_pending) {
        int32_t A, D;
        line_metrics(s, &A, &D);
        emit_marker(cy + A, A);
    }
    if (fixed_h >= 0) ch = fixed_h;
    if (s->min_h.kind == LEN_SET && !s->min_h.pct) {
        int32_t m = s->min_h.px - (s->border_box ? pb_v(&e) : 0);
        if (ch < m) ch = m;
    }
    if (s->max_h.kind == LEN_SET && !s->max_h.pct) {
        int32_t m = s->max_h.px - (s->border_box ? pb_v(&e) : 0);
        if (ch > m) ch = m;
    }
    if (ch < 0) ch = 0;
    int32_t bh = ch + pb_v(&e);
    if (clip) {
        if (clip_item >= 0) L.d->items[clip_item].h = bh;
        emit(WI_UNCLIP, 0, 0, 0, 0, 0);
    }
    /* absolutely positioned descendants (containing block = padding box) */
    if (is_positioned(s) || is_html) {
        int32_t pbx = bx + e.b[3], pby = y + e.b[0];
        int32_t pbw = bw - e.b[1] - e.b[3], pbh = bh - e.b[0] - e.b[2];
        if (is_html) { pbx = 0; pby = 0; pbw = L.d->width; pbh = L.viewport_h; }
        layout_abs_descendants(n, pbx, pby, pbw, pbh, is_html);
    }
    /* finalize decoration heights */
    WebDoc *d = L.d;
    for (int i = deco_from; i < deco_to && i < d->item_count; i++) {
        WebItem *it = &d->items[i];
        if (it->kind == WI_RECT && it->h == 0) it->h = bh;
        else if (it->kind == WI_STROKE) it->h = bh;
        else if (it->kind == WI_RECT && e.b[2] > 0 && it->y == y - e.b[2] && it->h == e.b[2]) it->y = y + bh - e.b[2];
        int r = s->radius > 255 ? 255 : s->radius;
        if (r > bh / 2) r = bh / 2;
        if (r > bw / 2) r = bw / 2;
        if ((it->kind == WI_RECT || it->kind == WI_STROKE) && it->radius > r) it->radius = (uint8_t)(r < 0 ? 0 : r);
    }
    /* relative positioning */
    if (s->position == POS_RELATIVE) {
        int32_t dx = 0, dy = 0;
        if (s->inset[3].kind == LEN_SET) dx = css_len(&s->inset[3], cw);
        else if (s->inset[1].kind == LEN_SET) dx = -css_len(&s->inset[1], cw);
        if (s->inset[0].kind == LEN_SET && !s->inset[0].pct) dy = s->inset[0].px;
        else if (s->inset[2].kind == LEN_SET && !s->inset[2].pct) dy = -s->inset[2].px;
        if (dx > 2000 || dx < -2000 || dy > 2000 || dy < -2000) { dx = 0; dy = 0; }
        translate(deco_from, anchor_from, dx, dy);
    }
    L.link = saved_link;
    out->w = bw;
    out->h = bh;
    out->mt = e.m[0];
    out->mb = e.m[2];
    out->bg_item = bg;
    L.depth--;
}

/* ------------------------------------------------------------------ */
/* Document setup                                                      */
/* ------------------------------------------------------------------ */

void web_doc_init(WebDoc *d, WebArena *dom, WebArena *css, WebArena *layout, WebItem *items, int item_cap,
                  const WebFontMetrics *fm)
{
    memset(d, 0, sizeof(*d));
    d->dom_arena = dom;
    d->css_arena = css;
    d->layout_arena = layout;
    d->items = items;
    d->item_cap = item_cap;
    d->fm = fm;
    d->refresh_secs = -1;
    d->zoom = 100;
    d->bg = 0xFFFFFFFFu;
}

static void collapse_copy(char *out, size_t size, const char *s, size_t n)
{
    size_t o = 0;
    bool space = false;
    for (size_t i = 0; i < n && o + 1 < size; i++) {
        char c = s[i];
        if (c == ' ' || c == '\n' || c == '\t' || c == '\r') { space = o > 0; continue; }
        if (space && o + 2 < size) out[o++] = ' ';
        space = false;
        out[o++] = c;
    }
    out[o] = '\0';
}

static void text_content(const DomNode *n, char *out, size_t size)
{
    /* concatenated text of a small subtree (labels, options) */
    char buf[512];
    size_t k = 0;
    const DomNode *root = n;
    for (const DomNode *c = n->first; c && k + 1 < sizeof(buf); ) {
        if (c->type == DOM_TEXT) {
            size_t take = c->text_len < sizeof(buf) - 1 - k ? c->text_len : sizeof(buf) - 1 - k;
            memcpy(buf + k, c->text, take);
            k += take;
            if (k + 1 < sizeof(buf)) buf[k++] = ' ';
        }
        /* walk */
        if (c->first && c->type == DOM_ELEMENT && c->tag != TAG_SCRIPT && c->tag != TAG_STYLE) { c = c->first; continue; }
        while (c && !c->next && c != root) c = c->parent;
        if (!c || c == root) break;
        c = c->next;
    }
    collapse_copy(out, size, buf, k);
}

static bool streq(const char *a, const char *b) { return a && strcmp(a, b) == 0; }

static void scan_forms(WebDoc *d)
{
    int form = -1;
    DomNode *root = d->tree.root;
    DomNode *form_node = NULL;
    for (DomNode *n = root; n; ) {
        bool skip = false;
        if (n->type == DOM_ELEMENT) {
            if (form_node && !(n == form_node)) {
                /* left the form subtree? */
                const DomNode *p = n;
                while (p && p != form_node) p = p->parent;
                if (!p) { form = -1; form_node = NULL; }
            }
            if (n->tag == TAG_FORM && d->form_count < WEB_MAX_FORMS) {
                WebForm *f = &d->forms[d->form_count];
                const char *action = dom_attr(n, "action");
                const char *method = dom_attr(n, "method");
                const char *enc = dom_attr(n, "enctype");
                f->action = action ? action : "";
                f->post = method && (method[0] == 'p' || method[0] == 'P');
                f->multipart = enc && enc[0] == 'm';
                form = d->form_count++;
                form_node = n;
            } else if ((n->tag == TAG_INPUT || n->tag == TAG_SELECT || n->tag == TAG_TEXTAREA || n->tag == TAG_BUTTON) &&
                       d->field_count < WEB_MAX_FIELDS) {
                WebField *f = &d->fields[d->field_count];
                memset(f, 0, sizeof(*f));
                f->form = (int8_t)form;
                f->node = n;
                f->selected = -1;
                const char *name = dom_attr(n, "name");
                const char *value = dom_attr(n, "value");
                if (name) strncpy(f->name, name, sizeof(f->name) - 1);
                if (value) strncpy(f->value, value, sizeof(f->value) - 1);
                f->disabled = dom_attr(n, "disabled") != NULL;
                const char *ph = dom_attr(n, "placeholder");
                if (!ph) ph = dom_attr(n, "aria-label");
                if (ph) strncpy(f->placeholder, ph, sizeof(f->placeholder) - 1);
                if (n->tag == TAG_INPUT) {
                    const char *t = dom_attr(n, "type");
                    if (!t || !t[0]) t = "text";
                    char type[16];
                    size_t k = 0;
                    for (; t[k] && k < sizeof(type) - 1; k++) type[k] = (t[k] >= 'A' && t[k] <= 'Z') ? (char)(t[k] + 32) : t[k];
                    type[k] = '\0';
                    if (streq(type, "hidden")) f->kind = FIELD_HIDDEN;
                    else if (streq(type, "password")) f->kind = FIELD_PASSWORD;
                    else if (streq(type, "submit") || streq(type, "image")) f->kind = FIELD_SUBMIT;
                    else if (streq(type, "button")) f->kind = FIELD_BUTTON;
                    else if (streq(type, "reset")) f->kind = FIELD_RESET;
                    else if (streq(type, "checkbox")) f->kind = FIELD_CHECKBOX;
                    else if (streq(type, "radio")) f->kind = FIELD_RADIO;
                    else if (streq(type, "file")) { f->kind = FIELD_BUTTON; f->disabled = true; strcpy(f->label, "Choose File"); }
                    else f->kind = FIELD_TEXT;
                    if (f->kind == FIELD_CHECKBOX || f->kind == FIELD_RADIO) {
                        f->checked = dom_attr(n, "checked") != NULL;
                        if (!value) strcpy(f->value, "on");
                    }
                    if (f->kind == FIELD_SUBMIT || f->kind == FIELD_BUTTON || f->kind == FIELD_RESET) {
                        if (!f->label[0]) strncpy(f->label, value && value[0] ? value : f->kind == FIELD_RESET ? "Reset" : f->kind == FIELD_SUBMIT ? "Submit" : "", sizeof(f->label) - 1);
                        if (streq(type, "image") && dom_attr(n, "alt")) strncpy(f->label, dom_attr(n, "alt"), sizeof(f->label) - 1);
                    }
                } else if (n->tag == TAG_TEXTAREA) {
                    f->kind = FIELD_TEXTAREA;
                    if (n->first && n->first->type == DOM_TEXT) {
                        const char *v = n->first->text;
                        if (*v == '\n') v++;
                        strncpy(f->value, v, sizeof(f->value) - 1);
                    }
                } else if (n->tag == TAG_SELECT) {
                    f->kind = FIELD_SELECT;
                    int count = 0;
                    for (DomNode *o = n->first; o; o = dom_next(o, n, false)) if (o->tag == TAG_OPTION && o->type == DOM_ELEMENT) count++;
                    if (count > 400) count = 400;
                    f->options = count ? (const char **)arena_alloc(d->dom_arena, sizeof(char *) * (size_t)count) : NULL;
                    f->option_values = count ? (const char **)arena_alloc(d->dom_arena, sizeof(char *) * (size_t)count) : NULL;
                    int k = 0;
                    for (DomNode *o = n->first; o && f->options && f->option_values && k < count; o = dom_next(o, n, false)) {
                        if (o->type != DOM_ELEMENT || o->tag != TAG_OPTION) continue;
                        char label[96];
                        text_content(o, label, sizeof(label));
                        f->options[k] = arena_strndup(d->dom_arena, label, strlen(label));
                        const char *ov = dom_attr(o, "value");
                        f->option_values[k] = ov ? ov : f->options[k];
                        if (!f->options[k]) break;
                        if (dom_attr(o, "selected") || f->selected < 0) f->selected = (int16_t)k;
                        k++;
                    }
                    f->option_count = (int16_t)k;
                    if (f->selected >= 0 && f->option_values) strncpy(f->value, f->option_values[f->selected], sizeof(f->value) - 1);
                } else {        /* <button> */
                    const char *t = dom_attr(n, "type");
                    f->kind = (!t || streq(t, "submit")) ? FIELD_SUBMIT : streq(t, "reset") ? FIELD_RESET : FIELD_BUTTON;
                    text_content(n, f->label, sizeof(f->label));
                }
                n->field = (int16_t)(d->field_count + 1);
                d->field_count++;
                skip = n->tag == TAG_SELECT || n->tag == TAG_TEXTAREA;
            }
        }
        n = dom_next(n, root, skip);
    }
}

void web_doc_parse(WebDoc *d, const char *html, size_t len, bool plain, bool latin1)
{
    arena_reset(d->dom_arena);
    arena_reset(d->css_arena);
    arena_reset(d->layout_arena);
    d->title[0] = d->base[0] = d->icon[0] = d->refresh_url[0] = '\0';
    d->refresh_secs = -1;
    d->script_bytes = d->script_count = 0;
    d->needs_js = false;
    d->sheet_count = d->sheet_url_count = 0;
    d->field_count = d->form_count = d->image_count = 0;
    d->item_count = d->link_count = d->anchor_count = 0;
    d->css = NULL;
    d->css_dirty = true;
    d->truncated = false;
    d->bg = 0xFFFFFFFFu;
    dom_parse(&d->tree, d->dom_arena, html, len, plain, latin1);
    DomNode *root = d->tree.root;
    for (DomNode *n = root; n; ) {
        bool skip = false;
        if (n->type == DOM_ELEMENT) {
            switch (n->tag) {
            case TAG_TITLE:
                if (!d->title[0] && n->first && n->first->type == DOM_TEXT)
                    collapse_copy(d->title, sizeof(d->title), n->first->text, n->first->text_len);
                skip = true;
                break;
            case TAG_BASE: {
                const char *h = dom_attr(n, "href");
                if (h && !d->base[0] && strlen(h) < sizeof(d->base)) strcpy(d->base, h);
                break;
            }
            case TAG_STYLE: {
                const char *media = dom_attr(n, "media");
                if (n->first && n->first->type == DOM_TEXT && d->sheet_count < WEB_MAX_SHEETS) {
                    d->sheets[d->sheet_count].text = n->first->text;
                    d->sheets[d->sheet_count].len = n->first->text_len;
                    d->sheets[d->sheet_count].external = false;
                    d->sheets[d->sheet_count].media = media && media[0] ? media : NULL;
                    d->sheet_count++;
                }
                skip = true;
                break;
            }
            case TAG_LINK: {
                const char *rel = dom_attr(n, "rel"), *href = dom_attr(n, "href");
                if (!rel || !href || !href[0]) break;
                bool sheet = false, icon = false;
                for (const char *r = rel; *r; r++) {
                    if (strncmp(r, "stylesheet", 10) == 0) sheet = true;
                    if (strncmp(r, "icon", 4) == 0) icon = true;
                    if (strncmp(r, "alternate", 9) == 0) { sheet = false; break; }
                }
                const char *media = dom_attr(n, "media");
                if (sheet && media && media[0]) {
                    /* print-only sheets are never fetched; others are checked per layout */
                    bool comma = false;
                    for (const char *m = media; *m; m++) if (*m == ',') comma = true;
                    if (strncmp(media, "print", 5) == 0 && !comma) sheet = false;
                }
                if (sheet && d->sheet_count < WEB_MAX_SHEETS && d->sheet_url_count < WEB_MAX_SHEETS && strlen(href) < WEB_URL_MAX) {
                    strcpy(d->sheet_urls[d->sheet_url_count], href);
                    d->sheets[d->sheet_count].text = NULL;
                    d->sheets[d->sheet_count].len = (size_t)d->sheet_url_count;   /* slot -> url index until loaded */
                    d->sheets[d->sheet_count].external = true;
                    d->sheets[d->sheet_count].media = media && media[0] ? media : NULL;
                    d->sheet_count++;
                    d->sheet_url_count++;
                }
                if (icon && !d->icon[0] && strlen(href) < sizeof(d->icon)) strcpy(d->icon, href);
                break;
            }
            case TAG_META: {
                const char *he = dom_attr(n, "http-equiv"), *content = dom_attr(n, "content");
                if (he && content && (he[0] == 'r' || he[0] == 'R')) {
                    int secs = 0;
                    const char *c = content;
                    while (*c == ' ') c++;
                    for (; *c >= '0' && *c <= '9' && secs < 100000; c++) secs = secs * 10 + (*c - '0');
                    while (*c && *c != ';' && *c != ',') c++;
                    if (*c) c++;
                    while (*c == ' ') c++;
                    if ((c[0] == 'u' || c[0] == 'U') && (c[1] == 'r' || c[1] == 'R') && (c[2] == 'l' || c[2] == 'L')) {
                        c += 3;
                        while (*c == ' ' || *c == '=') c++;
                        if (*c == '\'' || *c == '"') c++;
                        size_t k = 0;
                        while (c[k] && c[k] != '\'' && c[k] != '"' && k < sizeof(d->refresh_url) - 1) { d->refresh_url[k] = c[k]; k++; }
                        d->refresh_url[k] = '\0';
                    }
                    d->refresh_secs = secs;
                }
                break;
            }
            case TAG_SCRIPT:
                d->script_count++;
                d->script_bytes += (int)n->text_len;
                skip = true;
                break;
            default:
                break;
            }
        }
        n = dom_next(n, root, skip);
    }
    scan_forms(d);
}

void web_doc_set_sheet(WebDoc *d, int url_index, const char *css, size_t len)
{
    for (int i = 0; i < d->sheet_count; i++) {
        if (d->sheets[i].external && !d->sheets[i].text && (int)d->sheets[i].len == url_index) {
            d->sheets[i].text = css ? css : "";
            d->sheets[i].len = css ? len : 0;
            d->css_dirty = true;
            return;
        }
    }
}

bool web_doc_sheets_pending(const WebDoc *d)
{
    for (int i = 0; i < d->sheet_count; i++) if (d->sheets[i].external && !d->sheets[i].text) return true;
    return false;
}

bool web_doc_image_needs_relayout(const WebDoc *d, int image)
{
    (void)d; (void)image;
    return true;
}

static void reset_boxes(DomNode *root)
{
    for (DomNode *n = root; n; n = dom_next(n, root, false)) n->box = NULL;
}

void web_doc_layout(WebDoc *d, int width, int viewport_h, int zoom, bool dark)
{
    if (!d->tree.root) return;
    if (zoom < 25) zoom = 25;
    if (zoom > 500) zoom = 500;
    int css_w = width * 100 / zoom;
    if (d->css_dirty || !d->css || d->styled_width != css_w || d->styled_zoom != zoom || d->styled_dark != dark) {
        arena_reset(d->css_arena);
        CssMedia m = {css_w, viewport_h * 100 / zoom, dark, zoom, d->tree.quirks};
        d->css = css_new(d->css_arena, &m);
        int imports_seen = 0;
        for (int i = 0; i < d->sheet_count; i++) {
            if (d->sheets[i].text && css_media_matches(d->css, d->sheets[i].media)) css_add_sheet(d->css, d->sheets[i].text, d->sheets[i].len);
            /* new @import URLs become requests */
            int ic = css_import_count(d->css);
            for (; imports_seen < ic; imports_seen++) {
                const char *u = css_import_url(d->css, imports_seen);
                bool known = false;
                for (int k = 0; k < d->sheet_url_count; k++) if (strcmp(d->sheet_urls[k], u) == 0) known = true;
                if (!known && d->sheet_url_count < WEB_MAX_SHEETS && d->sheet_count < WEB_MAX_SHEETS && strlen(u) < WEB_URL_MAX) {
                    strcpy(d->sheet_urls[d->sheet_url_count], u);
                    d->sheets[d->sheet_count].text = NULL;
                    d->sheets[d->sheet_count].len = (size_t)d->sheet_url_count;
                    d->sheets[d->sheet_count].external = true;
                    d->sheets[d->sheet_count].media = NULL;
                    d->sheet_count++;
                    d->sheet_url_count++;
                }
            }
        }
        css_style_tree(d->css, &d->tree);
        d->css_dirty = false;
        d->styled_width = css_w;
        d->styled_zoom = zoom;
        d->styled_dark = dark;
    }
    d->dark = dark;
    d->zoom = zoom;
    arena_reset(d->layout_arena);
    reset_boxes(d->tree.root);
    memset(&L, 0, sizeof(L));
    memset(ifc_stack, 0, sizeof(ifc_stack));
    memset(flex_buf, 0, sizeof(flex_buf));
    ifc_depth = flex_depth = 0;
    float_count = float_base = 0;
    L.d = d;
    L.viewport_h = viewport_h > 0 ? viewport_h : 800;
    L.fm = d->fm;
    L.a = d->layout_arena;
    L.link = -1;
    default_style.color = 0xFF000000u;
    default_style.font_size = 16;
    default_style.font_weight = 400;
    default_style.display = DISP_INLINE;
    d->item_count = d->link_count = d->anchor_count = 0;
    d->truncated = false;
    /* canvas colour: html background, else body background */
    const CssStyle *hs = st(d->tree.root);
    const CssStyle *bs = d->tree.body ? st(d->tree.body) : NULL;
    d->bg = (hs->bg >> 24) ? hs->bg : (bs && (bs->bg >> 24)) ? bs->bg : 0xFFFFFFFFu;
    if ((d->bg >> 24) != 0xFF) {
        /* translucent canvas over white */
        uint32_t a = d->bg >> 24;
        uint32_t r = (((d->bg >> 16) & 0xFF) * a + 255 * (255 - a)) / 255;
        uint32_t g = (((d->bg >> 8) & 0xFF) * a + 255 * (255 - a)) / 255;
        uint32_t b = ((d->bg & 0xFF) * a + 255 * (255 - a)) / 255;
        d->bg = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    d->width = width;
    BoxOut bo;
    layout_box(d->tree.root, 0, 0, width, width, -1, false, &bo);
    int32_t h = bo.h;
    for (int i = 0; i < d->item_count; i++) {
        int32_t b = d->items[i].y + d->items[i].h;
        if (d->items[i].kind != WI_CLIP && b > h && b < 2000000) h = b;
    }
    d->height = h;
    d->needs_js = L.text_chars < 160 && d->script_bytes + d->script_count * 4000 > 12000;
    if (d->dom_arena->full || d->css_arena->full || d->layout_arena->full) d->truncated = true;
}

/* ------------------------------------------------------------------ */
/* Queries                                                             */
/* ------------------------------------------------------------------ */

int web_link_at(const WebDoc *d, int32_t x, int32_t y)
{
    int best = -1;
    for (int i = 0; i < d->item_count; i++) {
        const WebItem *it = &d->items[i];
        if (it->link < 0 || it->kind == WI_CLIP || it->kind == WI_UNCLIP) continue;
        if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h) {
            best = it->link;
            if (it->kind == WI_TEXT || it->kind == WI_IMAGE) return best;    /* most specific */
        }
    }
    return best;
}

int web_field_at(const WebDoc *d, int32_t x, int32_t y)
{
    for (int i = d->item_count - 1; i >= 0; i--) {
        const WebItem *it = &d->items[i];
        if (it->kind != WI_FIELD) continue;
        if (x >= it->x - 2 && x < it->x + it->w + 2 && y >= it->y - 3 && y < it->y + it->h + 3) return it->ref;
    }
    /* <button> elements: their box is the field area */
    for (int f = 0; f < d->field_count; f++) {
        const DomNode *n = d->fields[f].node;
        if (!n || n->tag != TAG_BUTTON) continue;
        (void)n;
    }
    return -1;
}

int32_t web_anchor_y(const WebDoc *d, const char *name)
{
    for (int i = 0; i < d->anchor_count; i++)
        if (strcmp(d->anchors[i].name, name) == 0) return d->anchors[i].y;
    return -1;
}

int web_first_item_at(const WebDoc *d, int32_t y)
{
    (void)y;
    (void)d;
    return 0;
}

static void q_append(char *out, size_t size, const char *s)
{
    size_t a = strlen(out), b = strlen(s);
    if (a + b >= size) b = size - a - 1;
    memcpy(out + a, s, b);
    out[a + b] = '\0';
}

void web_form_encode(const WebDoc *d, int form, int submitter, char *out, size_t size)
{
    static char enc[1600];
    out[0] = '\0';
    bool first = true;
    for (int i = 0; i < d->field_count; i++) {
        const WebField *f = &d->fields[i];
        if (f->form != form || !f->name[0] || f->disabled) continue;
        if ((f->kind == FIELD_SUBMIT) && i != submitter) continue;
        if (f->kind == FIELD_BUTTON || f->kind == FIELD_RESET) continue;
        if ((f->kind == FIELD_CHECKBOX || f->kind == FIELD_RADIO) && !f->checked) continue;
        if (!first) q_append(out, size, "&");
        first = false;
        url_encode_component(f->name, enc, sizeof(enc));
        q_append(out, size, enc);
        q_append(out, size, "=");
        url_encode_component(f->value, enc, sizeof(enc));
        q_append(out, size, enc);
    }
}
