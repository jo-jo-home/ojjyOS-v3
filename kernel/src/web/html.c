/*
 * ojjyOS v3 - HTML layout engine (see html.h)
 *
 * Input is untrusted: every scan is bounded by the input length and every
 * output table by its capacity (overflow sets doc->truncated).
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "web/html.h"
#include "web/url.h"
#include "string.h"

#define MAX_ATTRS   12
#define WORD_MAX    256
#define LIST_DEPTH  8
#define INDENT      24

typedef struct { char name[16]; char value[256]; } Attr;

typedef struct {
    WebDoc *d;
    int width;
    int32_t x, y;
    int line_h;
    int indent;
    bool line_empty, pending_space;
    int gap_done;               /* vertical space already added since content */
    /* style state */
    int bold, italic, code, muted, heading, pre, skip;
    int link;
    /* lists */
    int list_depth;
    bool list_ordered[LIST_DEPTH];
    int list_counter[LIST_DEPTH];
    /* blocks with decorations */
    int32_t quote_start[LIST_DEPTH];
    int quote_depth;
    int32_t pre_start;
    /* forms */
    int form;
    int capture;                /* 0 none, 1 title, 2 button, 3 option, 4 textarea */
    char cap[128];
    int cap_len;
    int select_field;
    bool option_selected, select_has_value;
    char option_value[128];
    int table_cell;
    char word[WORD_MAX];
    int wlen;
} Layout;

/* ------------------------------------------------------------------ */
/* Output tables                                                       */
/* ------------------------------------------------------------------ */

static uint32_t pool_add(WebDoc *d, const char *s, size_t n)
{
    if (d->pool_len + n + 1 > WEB_TEXT_POOL) { d->truncated = true; return 0; }
    uint32_t off = d->pool_len;
    memcpy(d->pool + off, s, n);
    d->pool[off + n] = '\0';
    d->pool_len += (uint32_t)n + 1;
    return off;
}

const char *web_text(const WebDoc *doc, uint32_t off)
{
    return off < doc->pool_len ? doc->pool + off : "";
}

static int add_box(Layout *L, int kind, int32_t x, int32_t y, int32_t w, int32_t h, int field)
{
    WebDoc *d = L->d;
    if (d->box_count >= WEB_MAX_BOXES) { d->truncated = true; return -1; }
    WebBox *b = &d->boxes[d->box_count];
    b->x = x; b->y = y; b->w = w; b->h = h; b->kind = (uint8_t)kind; b->field = (int16_t)field;
    return d->box_count++;
}

static int scale_of(const Layout *L) { return L->heading == 1 || L->heading == 2 ? 2 : 1; }
static int cell(const Layout *L) { return WEB_CELL_W * scale_of(L); }

static uint8_t style_of(const Layout *L)
{
    uint8_t s = 0;
    if (L->bold || L->heading) s |= WS_BOLD;
    if (L->heading) s |= WS_HEADING;
    if (L->link >= 0) s |= WS_LINK;
    if (L->muted) s |= WS_MUTED;
    if (L->code || L->pre) s |= WS_CODE;
    if (L->italic) s |= WS_ITALIC;
    return s;
}

static void place(Layout *L, const char *s, int n)
{
    WebDoc *d = L->d;
    if (n <= 0) return;
    int scale = scale_of(L), cw = WEB_CELL_W * scale;
    uint8_t style = style_of(L);
    int16_t link = (int16_t)L->link;
    WebRun *r = d->run_count ? &d->runs[d->run_count - 1] : NULL;
    if (r && r->y == L->y && r->style == style && r->scale == scale && r->link == link &&
        r->x + r->len * cw == L->x && r->text + r->len + 1 == d->pool_len &&
        d->pool_len + (uint32_t)n < WEB_TEXT_POOL && r->len + n < 65000) {
        memcpy(d->pool + d->pool_len - 1, s, (size_t)n);     /* overwrite the NUL, extend */
        d->pool_len += (uint32_t)n;
        d->pool[d->pool_len - 1] = '\0';
        r->len = (uint16_t)(r->len + n);
    } else {
        if (d->run_count >= WEB_MAX_RUNS || d->pool_len + (uint32_t)n + 1 > WEB_TEXT_POOL) {
            d->truncated = true;
            return;
        }
        uint32_t off = pool_add(d, s, (size_t)n);
        r = &d->runs[d->run_count++];
        r->x = L->x; r->y = L->y; r->text = off; r->len = (uint16_t)n;
        r->style = style; r->scale = (uint8_t)scale; r->link = link;
    }
    L->x += n * cw;
    L->line_empty = false;
    L->gap_done = 0;
    if (L->line_h < WEB_CELL_H * scale) L->line_h = WEB_CELL_H * scale;
}

static void newline(Layout *L)
{
    L->y += L->line_h;
    L->x = L->indent;
    L->line_h = WEB_CELL_H;
    L->line_empty = true;
    L->pending_space = false;
}

/* Start a new block: finish the line, then ensure `gap` px of space. */
static void flush_word(Layout *L);

static void block(Layout *L, int gap)
{
    flush_word(L);                      /* the paragraph's last word stays on its line */
    if (!L->line_empty) newline(L);
    if (gap > L->gap_done) { L->y += gap - L->gap_done; L->gap_done = gap; }
    L->x = L->indent;
}

static void flush_word(Layout *L)
{
    if (!L->wlen) return;
    int cw = cell(L);
    bool space = L->pending_space && !L->line_empty;
    int need = (space ? cw : 0) + L->wlen * cw;
    if (!L->line_empty && L->x + need > L->width) { newline(L); space = false; }
    if (space) {
        /* The gap belongs to the preceding text: a link's underline must
         * not start one cell early, so drop the new word's link/style. */
        int saved_link = L->link;
        WebRun *prev = L->d->run_count ? &L->d->runs[L->d->run_count - 1] : NULL;
        if (!prev || prev->link != L->link) L->link = -1;
        place(L, " ", 1);
        L->link = saved_link;
    }
    int i = 0;
    while (i < L->wlen) {
        int avail = (L->width - L->x) / cw;
        if (avail <= 0) {
            newline(L);
            avail = (L->width - L->x) / cw;
            if (avail <= 0) avail = 1;
        }
        int n = L->wlen - i < avail ? L->wlen - i : avail;
        place(L, L->word + i, n);
        i += n;
        if (i < L->wlen) newline(L);
    }
    L->wlen = 0;
    L->pending_space = false;
}

/* One output character (already ASCII). nbsp arrives as '\x01'. */
static void text_char(Layout *L, char c)
{
    if (L->skip) return;
    if (L->capture) {
        if (c == '\x01') c = ' ';
        bool ws = c == ' ' || c == '\n' || c == '\t' || c == '\r';
        if (L->capture != 4 && ws) {
            if (L->cap_len && L->cap[L->cap_len - 1] != ' ' && L->cap_len < (int)sizeof(L->cap) - 1) L->cap[L->cap_len++] = ' ';
        } else if (L->cap_len < (int)sizeof(L->cap) - 1 && c != '\r') {
            L->cap[L->cap_len++] = (c == '\n' || c == '\t') ? ' ' : c;
        }
        return;
    }
    if (L->pre) {
        if (c == '\r') return;
        if (c == '\n') { if (L->line_empty) { L->line_empty = false; } newline(L); return; }
        if (c == '\x01') c = ' ';
        if (c == '\t') { int n = 4 - (((L->x - L->indent) / WEB_CELL_W) % 4); while (n--) text_char(L, ' '); return; }
        if (L->x + WEB_CELL_W > L->width) newline(L);
        place(L, &c, 1);
        return;
    }
    if (c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f') {
        flush_word(L);
        if (!L->line_empty) L->pending_space = true;
        return;
    }
    if (c == '\x01') c = ' ';
    if (L->wlen >= WORD_MAX - 1) flush_word(L);
    L->word[L->wlen++] = c;
}

static void text_str(Layout *L, const char *s) { while (*s) text_char(L, *s++); }

/* ------------------------------------------------------------------ */
/* Characters: entities and UTF-8 -> ASCII                             */
/* ------------------------------------------------------------------ */

static const char *latin1_base = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";

/* Emit an ASCII rendering of Unicode code point cp. */
static void emit_cp(Layout *L, uint32_t cp)
{
    if (cp < 0x80) { if (cp >= 32 || cp == '\n' || cp == '\t' || cp == '\r') text_char(L, (char)cp); return; }
    if (cp >= 0xC0 && cp <= 0xFF) { text_char(L, latin1_base[cp - 0xC0]); if (cp == 0xC6 || cp == 0xE6) text_char(L, 'e'); if (cp == 0xDF) text_char(L, 's'); return; }
    const char *s = NULL;
    switch (cp) {
        case 0xA0: case 0x2007: case 0x202F: text_char(L, '\x01'); return;
        case 0x2002: case 0x2003: case 0x2009: case 0x200A: s = " "; break;
        case 0x200B: case 0x200C: case 0x200D: case 0xFEFF: case 0xAD: return;
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2212: s = "-"; break;
        case 0x2014: case 0x2015: s = "--"; break;
        case 0x2018: case 0x2019: case 0x201A: case 0x2032: s = "'"; break;
        case 0x201C: case 0x201D: case 0x201E: case 0x2033: s = "\""; break;
        case 0x2022: case 0x2023: case 0x25CF: case 0x25E6: s = "*"; break;
        case 0xB7: case 0x22C5: s = "."; break;
        case 0x2026: s = "..."; break;
        case 0xA9: s = "(c)"; break;
        case 0xAE: s = "(R)"; break;
        case 0x2122: s = "TM"; break;
        case 0x20AC: s = "EUR"; break;
        case 0xA3: s = "GBP"; break;
        case 0xA5: s = "JPY"; break;
        case 0xA2: s = "c"; break;
        case 0xB0: s = "deg"; break;
        case 0xB1: s = "+/-"; break;
        case 0xD7: s = "x"; break;
        case 0xF7: s = "/"; break;
        case 0xAB: s = "<<"; break;
        case 0xBB: s = ">>"; break;
        case 0x2039: s = "<"; break;
        case 0x203A: s = ">"; break;
        case 0x2190: s = "<-"; break;
        case 0x2192: s = "->"; break;
        case 0x2191: s = "^"; break;
        case 0x2193: s = "v"; break;
        case 0x21D2: s = "=>"; break;
        case 0x2264: s = "<="; break;
        case 0x2265: s = ">="; break;
        case 0x2260: s = "!="; break;
        case 0x2248: s = "~"; break;
        case 0xBC: s = "1/4"; break;
        case 0xBD: s = "1/2"; break;
        case 0xBE: s = "3/4"; break;
        case 0xA7: s = "S"; break;
        case 0xB6: s = "P"; break;
        case 0x2713: case 0x2714: s = "v"; break;
        case 0x2717: case 0x2718: s = "x"; break;
        case 0x2605: case 0x2606: s = "*"; break;
        default: s = cp >= 0x1F000 ? "" : "?"; break;    /* emoji etc: drop */
    }
    text_str(L, s);
}

typedef struct { const char *name; uint32_t cp; } Entity;
static const Entity entities[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
    {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122}, {"mdash", 0x2014}, {"ndash", 0x2013},
    {"hellip", 0x2026}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
    {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bull", 0x2022}, {"middot", 0xB7}, {"euro", 0x20AC},
    {"pound", 0xA3}, {"yen", 0xA5}, {"cent", 0xA2}, {"deg", 0xB0}, {"times", 0xD7}, {"divide", 0xF7},
    {"larr", 0x2190}, {"rarr", 0x2192}, {"uarr", 0x2191}, {"darr", 0x2193}, {"sect", 0xA7},
    {"para", 0xB6}, {"plusmn", 0xB1}, {"frac12", 0xBD}, {"frac14", 0xBC}, {"frac34", 0xBE},
    {"shy", 0xAD}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009}, {"zwj", 0x200D},
    {"zwnj", 0x200C}, {"le", 0x2264}, {"ge", 0x2265}, {"ne", 0x2260}, {"minus", 0x2212},
    {"eacute", 0xE9}, {"egrave", 0xE8}, {"aacute", 0xE1}, {"agrave", 0xE0}, {"uuml", 0xFC},
    {"ouml", 0xF6}, {"auml", 0xE4}, {"ccedil", 0xE7}, {"ntilde", 0xF1}, {"szlig", 0xDF},
};

/* Decode an entity at p ('&' already seen). Returns bytes consumed after '&'. */
static size_t decode_entity(const char *p, const char *end, uint32_t *cp)
{
    if (p < end && *p == '#') {
        uint32_t v = 0;
        size_t i = 1;
        bool hex = i < (size_t)(end - p) && (p[i] == 'x' || p[i] == 'X');
        if (hex) i++;
        size_t digits = 0;
        while (p + i < end && digits < 8) {
            char c = p[i];
            int dv = (c >= '0' && c <= '9') ? c - '0' :
                     (hex && c >= 'a' && c <= 'f') ? c - 'a' + 10 :
                     (hex && c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (dv < 0) break;
            v = v * (hex ? 16 : 10) + (uint32_t)dv;
            i++; digits++;
        }
        if (!digits) return 0;
        if (p + i < end && p[i] == ';') i++;
        *cp = v > 0x10FFFF ? '?' : v;
        return i;
    }
    size_t n = 0;
    while (p + n < end && n < 10 && ((p[n] >= 'a' && p[n] <= 'z') || (p[n] >= 'A' && p[n] <= 'Z') || (p[n] >= '0' && p[n] <= '9'))) n++;
    for (unsigned k = 0; k < ARRAY_SIZE(entities); k++) {
        size_t l = strlen(entities[k].name);
        if (l == n && strncmp(p, entities[k].name, n) == 0) {
            *cp = entities[k].cp;
            return (p + n < end && p[n] == ';') ? n + 1 : n;
        }
    }
    return 0;
}

/* Decode one UTF-8 sequence. Returns bytes consumed (>=1). */
static size_t utf8_decode(const unsigned char *p, const unsigned char *end, uint32_t *cp)
{
    unsigned char c = p[0];
    int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
    if (!len || p + len > end) { *cp = c >= 0xA0 ? c : '?'; return 1; }    /* Latin-1 fallback */
    uint32_t v = c & (len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07);
    for (int i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) { *cp = '?'; return 1; }
        v = (v << 6) | (p[i] & 0x3F);
    }
    *cp = v;
    return (size_t)len;
}

/* Emit decoded text (entities + UTF-8) for [p, end). */
static void emit_text(Layout *L, const char *p, const char *end)
{
    while (p < end) {
        unsigned char c = (unsigned char)*p;
        if (c == '&') {
            uint32_t cp;
            size_t n = decode_entity(p + 1, end, &cp);
            if (n) { emit_cp(L, cp); p += 1 + n; continue; }
            text_char(L, '&'); p++;
        } else if (c >= 0x80) {
            uint32_t cp;
            p += utf8_decode((const unsigned char *)p, (const unsigned char *)end, &cp);
            emit_cp(L, cp);
        } else {
            text_char(L, (char)c);
            p++;
        }
    }
}

/* Decode entities/UTF-8 of an attribute value into ASCII (bounded). */
static void decode_attr(char *s)
{
    char out[256];
    size_t o = 0;
    const char *p = s, *end = s + strlen(s);
    while (p < end && o + 4 < sizeof(out)) {
        uint32_t cp = (unsigned char)*p;
        if (*p == '&') {
            size_t n = decode_entity(p + 1, end, &cp);
            if (n) p += 1 + n; else { cp = '&'; p++; }
        } else if ((unsigned char)*p >= 0x80) {
            p += utf8_decode((const unsigned char *)p, (const unsigned char *)end, &cp);
        } else {
            p++;
        }
        if (cp == 0xA0) cp = ' ';
        out[o++] = cp < 0x80 ? (char)cp : (cp >= 0xC0 && cp <= 0xFF) ? latin1_base[cp - 0xC0] : '?';
    }
    out[o] = '\0';
    strcpy(s, out);
}

/* ------------------------------------------------------------------ */
/* Tags                                                                */
/* ------------------------------------------------------------------ */

static const char *attr(const Attr *a, int n, const char *name)
{
    for (int i = 0; i < n; i++) if (strcmp(a[i].name, name) == 0) return a[i].value;
    return NULL;
}

static bool is(const char *tag, const char *name) { return strcmp(tag, name) == 0; }

static bool is_block(const char *t)
{
    static const char *blocks[] = {
        "div", "section", "article", "header", "footer", "nav", "main", "aside", "center",
        "figure", "figcaption", "address", "details", "summary", "fieldset", "legend", "dl",
        "dt", "caption", "tbody", "thead", "tfoot", "html", "body", "noscript", "menu", "hgroup",
        "search", "label_block"
    };
    for (unsigned i = 0; i < ARRAY_SIZE(blocks); i++) if (is(t, blocks[i])) return true;
    return false;
}

static void add_anchor(Layout *L, const char *name)
{
    WebDoc *d = L->d;
    if (!name || !name[0] || d->anchor_count >= WEB_MAX_ANCHORS) return;
    uint32_t off = pool_add(d, name, strlen(name));
    if (d->truncated) return;
    d->anchors[d->anchor_count].name = off;
    d->anchors[d->anchor_count].y = L->y;
    d->anchor_count++;
}

static int add_field(Layout *L, int kind, const char *name, const char *value)
{
    WebDoc *d = L->d;
    if (d->field_count >= WEB_MAX_FIELDS) return -1;
    WebField *f = &d->fields[d->field_count];
    memset(f, 0, sizeof(*f));
    f->kind = (uint8_t)kind;
    f->form = (int8_t)L->form;
    f->box = -1;
    if (name) { strncpy(f->name, name, sizeof(f->name) - 1); }
    if (value) { strncpy(f->value, value, sizeof(f->value) - 1); }
    return d->field_count++;
}

/* Reserve an inline box of w x h px on the current line. */
static int inline_box(Layout *L, int kind, int w, int h, int field)
{
    flush_word(L);
    if (L->pending_space && !L->line_empty) L->x += WEB_CELL_W;
    L->pending_space = false;
    if (w > L->width - L->indent) w = L->width - L->indent;
    if (!L->line_empty && L->x + w > L->width) newline(L);
    int b = add_box(L, kind, L->x, L->y, w, h, field);
    L->x += w + 4;
    L->line_empty = false;
    L->gap_done = 0;
    if (L->line_h < h + 2) L->line_h = h + 2;
    if (field >= 0 && b >= 0) L->d->fields[field].box = (int16_t)b;
    return b;
}

static void close_tag(Layout *L, const char *t);

static void open_tag(Layout *L, const char *t, Attr *a, int na)
{
    WebDoc *d = L->d;
    const char *id = attr(a, na, "id");
    if (id) add_anchor(L, id);

    if (is(t, "svg") || is(t, "math") || is(t, "template") || is(t, "object") || is(t, "canvas") ||
        is(t, "select_skip")) { flush_word(L); L->skip++; return; }
    if (L->skip) return;

    if (is(t, "br")) {
        flush_word(L);
        if (L->line_empty) L->y += L->line_h; else newline(L);
        L->gap_done = 0;
        return;
    }
    if (is(t, "p")) { block(L, 10); return; }
    if (t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && !t[2]) {
        flush_word(L);
        int level = t[1] - '0';
        block(L, level <= 2 ? 16 : 12);
        L->heading = level;
        return;
    }
    if (is_block(t)) {
        block(L, is(t, "dt") || is(t, "summary") || is(t, "legend") ? 4 : 0);
        if (is(t, "dt") || is(t, "summary") || is(t, "legend")) L->bold++;
        return;
    }
    if (is(t, "dd")) { block(L, 0); L->indent += INDENT; L->x = L->indent; return; }
    if (is(t, "ul") || is(t, "ol") || is(t, "menu")) {
        block(L, L->list_depth ? 0 : 8);
        if (L->list_depth < LIST_DEPTH) {
            L->list_ordered[L->list_depth] = is(t, "ol");
            const char *start = attr(a, na, "start");
            int s = 0;
            if (start) for (const char *c = start; *c >= '0' && *c <= '9' && s < 100000; c++) s = s * 10 + (*c - '0');
            L->list_counter[L->list_depth] = s ? s - 1 : 0;
        }
        L->list_depth++;
        L->indent += INDENT;
        L->x = L->indent;
        return;
    }
    if (is(t, "li")) {
        block(L, 2);
        int depth = L->list_depth ? L->list_depth - 1 : 0;
        if (depth < LIST_DEPTH && L->list_depth && L->list_ordered[depth]) {
            char num[16];
            utoa((uint64_t)++L->list_counter[depth], num, 10);
            size_t n = strlen(num);
            num[n] = '.'; num[n + 1] = '\0';
            int32_t save = L->x;
            L->x = L->indent - (int)(n + 2) * WEB_CELL_W;
            if (L->x < 0) L->x = 0;
            place(L, num, (int)n + 1);
            L->x = save;
        } else {
            add_box(L, BOX_BULLET, L->indent - 14, L->y + 6, 5, 5, -1);
        }
        L->line_empty = true;   /* bullet does not count as content for wrapping */
        return;
    }
    if (is(t, "blockquote")) {
        block(L, 8);
        if (L->quote_depth < LIST_DEPTH) L->quote_start[L->quote_depth] = L->y;
        L->quote_depth++;
        L->indent += INDENT;
        L->x = L->indent;
        L->muted++;
        return;
    }
    if (is(t, "pre") || is(t, "listing") || is(t, "xmp")) {
        block(L, 8);
        L->pre++;
        L->pre_start = L->y;
        L->indent += 8;
        L->x = L->indent;
        return;
    }
    if (is(t, "hr")) {
        block(L, 4);
        add_box(L, BOX_RULE, L->indent, L->y + 6, L->width - L->indent, 1, -1);
        L->y += 14;
        L->gap_done = 4;
        return;
    }
    if (is(t, "table")) { block(L, 8); L->table_cell = 0; return; }
    if (is(t, "tr")) { block(L, 2); L->table_cell = 0; return; }
    if (is(t, "td") || is(t, "th")) {
        flush_word(L);
        if (L->table_cell++ > 0 && !L->line_empty) { L->pending_space = false; L->x += 2 * WEB_CELL_W; }
        if (is(t, "th")) L->bold++;
        return;
    }
    if (is(t, "b") || is(t, "strong")) { flush_word(L); L->bold++; return; }
    if (is(t, "i") || is(t, "em") || is(t, "cite") || is(t, "var") || is(t, "dfn")) { flush_word(L); L->italic++; return; }
    if (is(t, "code") || is(t, "kbd") || is(t, "samp") || is(t, "tt")) { flush_word(L); L->code++; return; }
    if (is(t, "del") || is(t, "s") || is(t, "strike") || is(t, "small")) { flush_word(L); L->muted++; return; }
    if (is(t, "q")) { text_char(L, '"'); return; }
    if (is(t, "a")) {
        flush_word(L);
        const char *href = attr(a, na, "href");
        const char *name = attr(a, na, "name");
        if (name) add_anchor(L, name);
        L->link = -1;
        if (href && href[0] && strncmp(href, "javascript:", 11) != 0 && d->link_count < WEB_MAX_LINKS) {
            uint32_t off = pool_add(d, href, strlen(href));
            if (!d->truncated) { d->links[d->link_count] = off; L->link = d->link_count++; }
        }
        return;
    }
    if (is(t, "img")) {
        const char *alt = attr(a, na, "alt");
        if (alt && alt[0]) {
            flush_word(L);
            L->muted++;
            text_char(L, '[');
            text_str(L, alt);
            text_char(L, ']');
            flush_word(L);
            L->muted--;
        }
        return;
    }
    if (is(t, "iframe") || is(t, "video") || is(t, "audio") || is(t, "embed")) {
        flush_word(L);
        const char *src = attr(a, na, "src");
        int saved = L->link;
        if (src && src[0] && d->link_count < WEB_MAX_LINKS) {
            uint32_t off = pool_add(d, src, strlen(src));
            if (!d->truncated) { d->links[d->link_count] = off; L->link = d->link_count++; }
        }
        L->muted++;
        text_str(L, is(t, "iframe") ? "[embedded page]" : is(t, "video") ? "[video]" : "[media]");
        flush_word(L);
        L->muted--;
        L->link = saved;
        if (!is(t, "embed")) L->skip++;      /* fallback content is not shown */
        return;
    }
    if (is(t, "form")) {
        block(L, 6);
        if (d->form_count < WEB_MAX_FORMS) {
            const char *action = attr(a, na, "action");
            const char *method = attr(a, na, "method");
            d->forms[d->form_count].action = pool_add(d, action ? action : "", action ? strlen(action) : 0);
            d->forms[d->form_count].post = method && (method[0] == 'p' || method[0] == 'P');
            L->form = d->form_count++;
        }
        return;
    }
    if (is(t, "input")) {
        const char *type = attr(a, na, "type");
        const char *name = attr(a, na, "name");
        const char *value = attr(a, na, "value");
        if (!type) type = "text";
        if (is(type, "hidden")) { add_field(L, FIELD_HIDDEN, name, value); return; }
        if (is(type, "submit") || is(type, "button") || is(type, "image") || is(type, "reset")) {
            const char *label = value && value[0] ? value : (is(type, "reset") ? "Reset" : "Submit");
            int f = add_field(L, is(type, "submit") || is(type, "image") ? FIELD_SUBMIT : FIELD_HIDDEN, name, value);
            if (f < 0) return;
            if (is(type, "button") || is(type, "reset")) d->fields[f].name[0] = '\0';   /* never submitted */
            strncpy(d->fields[f].label, label, sizeof(d->fields[f].label) - 1);
            inline_box(L, BOX_BUTTON, (int)(strlen(d->fields[f].label) + 2) * WEB_CELL_W, 18, f);
            return;
        }
        if (is(type, "checkbox") || is(type, "radio")) {
            int f = add_field(L, FIELD_CHECKBOX, name, value ? value : "on");
            if (f < 0) return;
            d->fields[f].checked = attr(a, na, "checked") != NULL;
            inline_box(L, BOX_INPUT, 14, 14, f);
            return;
        }
        int f = add_field(L, is(type, "password") ? FIELD_PASSWORD : FIELD_TEXT, name, value);
        if (f < 0) return;
        const char *ph = attr(a, na, "placeholder");
        if (ph) strncpy(d->fields[f].label, ph, sizeof(d->fields[f].label) - 1);
        const char *size = attr(a, na, "size");
        int chars = 24;
        if (size) { chars = 0; for (const char *c = size; *c >= '0' && *c <= '9' && chars < 200; c++) chars = chars * 10 + (*c - '0'); }
        if (chars < 6) chars = 6;
        if (chars > 48) chars = 48;
        inline_box(L, BOX_INPUT, chars * WEB_CELL_W + 8, 20, f);
        return;
    }
    if (is(t, "button")) {
        flush_word(L);
        const char *type = attr(a, na, "type");
        int f = add_field(L, (!type || is(type, "submit")) ? FIELD_SUBMIT : FIELD_HIDDEN, attr(a, na, "name"),
                          attr(a, na, "value"));
        if (f >= 0 && type && !is(type, "submit")) d->fields[f].name[0] = '\0';
        L->select_field = f;
        L->capture = 2;
        L->cap_len = 0;
        return;
    }
    if (is(t, "select")) {
        flush_word(L);
        L->select_field = add_field(L, FIELD_TEXT, attr(a, na, "name"), "");
        L->select_has_value = false;
        return;
    }
    if (is(t, "option")) {
        if (L->capture == 3) close_tag(L, "option");      /* </option> is optional */
        const char *v = attr(a, na, "value");
        L->option_selected = attr(a, na, "selected") != NULL;
        L->option_value[0] = '\0';
        if (v) { strncpy(L->option_value, v, sizeof(L->option_value) - 1); L->option_value[sizeof(L->option_value) - 1] = '\0'; }
        else L->option_value[0] = '\x02';     /* use the option text */
        L->capture = 3;
        L->cap_len = 0;
        return;
    }
    if (is(t, "textarea")) {
        flush_word(L);
        L->select_field = add_field(L, FIELD_TEXT, attr(a, na, "name"), "");
        return;     /* content handled as raw text by the tokenizer */
    }
    if (is(t, "base")) {
        const char *href = attr(a, na, "href");
        if (href) { strncpy(d->base, href, sizeof(d->base) - 1); d->base[sizeof(d->base) - 1] = '\0'; }
        return;
    }
}

static void close_tag(Layout *L, const char *t)
{
    WebDoc *d = L->d;
    if (is(t, "svg") || is(t, "math") || is(t, "template") || is(t, "object") || is(t, "canvas") ||
        is(t, "iframe") || is(t, "video") || is(t, "audio")) {
        if (L->skip) L->skip--;
        return;
    }
    if (L->skip) return;

    if (is(t, "p")) { block(L, 10); return; }
    if (t[0] == 'h' && t[1] >= '1' && t[1] <= '6' && !t[2]) {
        flush_word(L);
        L->heading = 0;
        block(L, 8);
        return;
    }
    if (is_block(t)) {
        flush_word(L);
        if (is(t, "dt") || is(t, "summary") || is(t, "legend")) { if (L->bold) L->bold--; }
        block(L, 0);
        return;
    }
    if (is(t, "dd")) { block(L, 0); L->indent = L->indent >= INDENT ? L->indent - INDENT : 0; L->x = L->indent; return; }
    if (is(t, "ul") || is(t, "ol") || is(t, "menu")) {
        block(L, 0);
        if (L->list_depth) L->list_depth--;
        L->indent = L->indent >= INDENT ? L->indent - INDENT : 0;
        L->x = L->indent;
        if (!L->list_depth) block(L, 8);
        return;
    }
    if (is(t, "li")) { flush_word(L); return; }
    if (is(t, "blockquote")) {
        block(L, 0);
        if (L->quote_depth) {
            L->quote_depth--;
            if (L->quote_depth < LIST_DEPTH) {
                int32_t top = L->quote_start[L->quote_depth];
                add_box(L, BOX_QUOTE_BAR, L->indent - INDENT + 6, top, 3, L->y - top, -1);
            }
        }
        L->indent = L->indent >= INDENT ? L->indent - INDENT : 0;
        L->x = L->indent;
        if (L->muted) L->muted--;
        block(L, 8);
        return;
    }
    if (is(t, "pre") || is(t, "listing") || is(t, "xmp")) {
        if (!L->line_empty) newline(L);
        if (L->pre) {
            L->pre--;
            add_box(L, BOX_CODE, L->indent - 8, L->pre_start - 4, L->width - L->indent + 8, L->y - L->pre_start + 8, -1);
        }
        L->indent = L->indent >= 8 ? L->indent - 8 : 0;
        L->x = L->indent;
        block(L, 10);
        return;
    }
    if (is(t, "table")) { block(L, 8); return; }
    if (is(t, "tr")) { block(L, 0); return; }
    if (is(t, "th")) { flush_word(L); if (L->bold) L->bold--; return; }
    if (is(t, "td")) { flush_word(L); return; }
    if (is(t, "b") || is(t, "strong")) { flush_word(L); if (L->bold) L->bold--; return; }
    if (is(t, "i") || is(t, "em") || is(t, "cite") || is(t, "var") || is(t, "dfn")) { flush_word(L); if (L->italic) L->italic--; return; }
    if (is(t, "code") || is(t, "kbd") || is(t, "samp") || is(t, "tt")) { flush_word(L); if (L->code) L->code--; return; }
    if (is(t, "del") || is(t, "s") || is(t, "strike") || is(t, "small")) { flush_word(L); if (L->muted) L->muted--; return; }
    if (is(t, "q")) { text_char(L, '"'); return; }
    if (is(t, "a")) { flush_word(L); L->link = -1; return; }
    if (is(t, "form")) { block(L, 6); L->form = -1; return; }
    if (is(t, "button") && L->capture == 2) {
        L->capture = 0;
        int f = L->select_field;
        if (f >= 0) {
            WebField *fl = &d->fields[f];
            int n = L->cap_len;
            while (n && L->cap[n - 1] == ' ') n--;
            const char *start = L->cap;
            while (*start == ' ' && n) { start++; n--; }
            if (n <= 0) { start = "Submit"; n = 6; }
            if (n > (int)sizeof(fl->label) - 1) n = sizeof(fl->label) - 1;
            memcpy(fl->label, start, (size_t)n);
            fl->label[n] = '\0';
            inline_box(L, BOX_BUTTON, (n + 2) * WEB_CELL_W, 18, f);
        }
        L->select_field = -1;
        return;
    }
    if (is(t, "option") && L->capture == 3) {
        L->capture = 0;
        L->cap[L->cap_len] = '\0';
        int f = L->select_field;
        if (f >= 0 && (!L->select_has_value || L->option_selected)) {
            WebField *fl = &d->fields[f];
            strncpy(fl->label, L->cap, sizeof(fl->label) - 1);
            fl->label[sizeof(fl->label) - 1] = '\0';
            const char *v = L->option_value[0] == '\x02' ? L->cap : L->option_value;
            strncpy(fl->value, v, sizeof(fl->value) - 1);
            fl->value[sizeof(fl->value) - 1] = '\0';
            L->select_has_value = true;
        }
        return;
    }
    if (is(t, "select")) {
        if (L->capture == 3) close_tag(L, "option");
        int f = L->select_field;
        if (f >= 0) {
            WebField *fl = &d->fields[f];
            fl->kind = FIELD_HIDDEN;     /* fixed choice; shown read-only */
            int n = (int)strlen(fl->label);
            inline_box(L, BOX_BUTTON, (n + 4) * WEB_CELL_W, 18, f);
        }
        L->select_field = -1;
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Tokenizer                                                           */
/* ------------------------------------------------------------------ */

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* Find "</name" (case-insensitive) from p; returns position or end. */
static const char *find_end_tag(const char *p, const char *end, const char *name)
{
    size_t n = strlen(name);
    for (; p + 2 + n <= end; p++) {
        if (p[0] != '<' || p[1] != '/') continue;
        size_t i = 0;
        while (i < n && lower(p[2 + i]) == name[i]) i++;
        if (i == n && (p + 2 + n == end || !((lower(p[2 + n]) >= 'a' && lower(p[2 + n]) <= 'z') || (p[2 + n] >= '0' && p[2 + n] <= '9'))))
            return p;
    }
    return end;
}

static const char *skip_past(const char *p, const char *end, char c)
{
    while (p < end && *p != c) p++;
    return p < end ? p + 1 : end;
}

/* Parse a tag at p (just after '<'). Returns pointer after '>'. */
static const char *parse_tag(const char *p, const char *end, char *name, size_t name_size, bool *closing,
                             Attr *attrs, int *nattrs)
{
    *closing = false;
    *nattrs = 0;
    if (p < end && *p == '/') { *closing = true; p++; }
    size_t n = 0;
    while (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == ':')) {
        if (n + 1 < name_size) name[n++] = lower(*p);
        p++;
    }
    name[n] = '\0';
    for (int guard = 0; p < end && guard < 4096; guard++) {
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r' || *p == '/')) p++;
        if (p >= end) break;
        if (*p == '>') return p + 1;
        char an[16];
        size_t al = 0;
        while (p < end && *p != '=' && *p != '>' && *p != ' ' && *p != '\n' && *p != '\t' && *p != '\r' && *p != '/') {
            if (al + 1 < sizeof(an)) an[al++] = lower(*p);
            p++;
        }
        an[al] = '\0';
        char val[256];
        size_t vl = 0;
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')) p++;
        if (p < end && *p == '=') {
            p++;
            while (p < end && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r')) p++;
            if (p < end && (*p == '"' || *p == '\'')) {
                char q = *p++;
                while (p < end && *p != q) { if (vl + 1 < sizeof(val)) val[vl++] = *p == '\n' ? ' ' : *p; p++; }
                if (p < end) p++;
            } else {
                while (p < end && *p != ' ' && *p != '>' && *p != '\n' && *p != '\t') { if (vl + 1 < sizeof(val)) val[vl++] = *p; p++; }
            }
        }
        val[vl] = '\0';
        if (al && *nattrs < MAX_ATTRS && !*closing) {
            strcpy(attrs[*nattrs].name, an);
            strcpy(attrs[*nattrs].value, val);
            decode_attr(attrs[*nattrs].value);
            (*nattrs)++;
        }
        if (!al && p < end && *p != '>') p++;          /* stray character: make progress */
    }
    return end;
}

static void finish_capture_title(Layout *L, const char *p, const char *e)
{
    L->capture = 1;
    L->cap_len = 0;
    emit_text(L, p, e);
    L->cap[L->cap_len] = '\0';
    int s = 0, n = L->cap_len;
    while (s < n && L->cap[s] == ' ') s++;
    while (n > s && L->cap[n - 1] == ' ') n--;
    if (n - s > (int)sizeof(L->d->title) - 1) n = s + (int)sizeof(L->d->title) - 1;
    memcpy(L->d->title, L->cap + s, (size_t)(n - s));
    L->d->title[n - s] = '\0';
    L->capture = 0;
}

void html_layout(WebDoc *doc, const char *html, size_t len, int width, bool plain)
{
    /* Reset only what is reused; the big tables are filled as needed. */
    doc->title[0] = doc->base[0] = '\0';
    doc->run_count = doc->link_count = doc->box_count = doc->field_count = 0;
    doc->form_count = doc->anchor_count = 0;
    doc->pool_len = 1;
    doc->pool[0] = '\0';                /* offset 0 = empty string */
    doc->truncated = false;
    doc->width = width < 80 ? 80 : width;

    static Layout L;
    memset(&L, 0, sizeof(L));
    L.d = doc;
    L.width = doc->width;
    L.line_h = WEB_CELL_H;
    L.line_empty = true;
    L.link = -1;
    L.form = -1;
    L.select_field = -1;
    L.gap_done = 1000;                  /* no leading gap at the top */

    const char *p = html, *end = html + len;
    if (plain) {
        L.pre = 1;
        emit_text(&L, p, end);
    } else {
        Attr attrs[MAX_ATTRS];
        while (p < end && !doc->truncated) {
            if (*p != '<') {
                const char *t = p;
                while (t < end && *t != '<') t++;
                emit_text(&L, p, t);
                p = t;
                continue;
            }
            if (p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-') {      /* comment */
                const char *c = p + 4;
                while (c + 2 < end && !(c[0] == '-' && c[1] == '-' && c[2] == '>')) c++;
                p = c + 2 < end ? c + 3 : end;
                continue;
            }
            if (p + 1 < end && (p[1] == '!' || p[1] == '?')) { p = skip_past(p, end, '>'); continue; }
            bool letter = p + 1 < end && ((p[1] >= 'a' && p[1] <= 'z') || (p[1] >= 'A' && p[1] <= 'Z') || p[1] == '/');
            if (!letter) { text_char(&L, '<'); p++; continue; }

            char name[16];
            bool closing;
            int na;
            p = parse_tag(p + 1, end, name, sizeof(name), &closing, attrs, &na);
            if (!name[0]) continue;
            if (closing) { close_tag(&L, name); continue; }

            if (is(name, "script") || is(name, "style") || is(name, "head_skip")) {
                const char *e = find_end_tag(p, end, name);
                p = e < end ? skip_past(e, end, '>') : end;
                continue;
            }
            if (is(name, "title")) {
                const char *e = find_end_tag(p, end, "title");
                if (!doc->title[0]) finish_capture_title(&L, p, e);
                p = e < end ? skip_past(e, end, '>') : end;
                continue;
            }
            if (is(name, "textarea")) {
                open_tag(&L, name, attrs, na);
                const char *e = find_end_tag(p, end, "textarea");
                int f = L.select_field;
                L.capture = 4;
                L.cap_len = 0;
                emit_text(&L, p, e);
                L.capture = 0;
                L.cap[L.cap_len] = '\0';
                if (f >= 0) {
                    strncpy(doc->fields[f].value, L.cap, sizeof(doc->fields[f].value) - 1);
                    inline_box(&L, BOX_INPUT, 32 * WEB_CELL_W + 8, 20, f);
                }
                L.select_field = -1;
                p = e < end ? skip_past(e, end, '>') : end;
                continue;
            }
            open_tag(&L, name, attrs, na);
        }
    }
    flush_word(&L);
    if (!L.line_empty) newline(&L);
    doc->height = L.y + WEB_CELL_H;
}

int web_first_run_at(const WebDoc *doc, int32_t y)
{
    int lo = 0, hi = doc->run_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        const WebRun *r = &doc->runs[mid];
        if (r->y + WEB_CELL_H * r->scale <= y) lo = mid + 1; else hi = mid;
    }
    return lo;
}

int web_link_at(const WebDoc *doc, int32_t x, int32_t y)
{
    for (int i = web_first_run_at(doc, y - 2 * WEB_CELL_H); i < doc->run_count; i++) {
        const WebRun *r = &doc->runs[i];
        if (r->y > y) break;
        int h = WEB_CELL_H * r->scale, w = r->len * WEB_CELL_W * r->scale;
        if (r->link >= 0 && x >= r->x && x < r->x + w && y >= r->y && y < r->y + h) return r->link;
    }
    return -1;
}

int web_box_at(const WebDoc *doc, int32_t x, int32_t y)
{
    for (int i = 0; i < doc->box_count; i++) {
        const WebBox *b = &doc->boxes[i];
        if ((b->kind == BOX_INPUT || b->kind == BOX_BUTTON) &&
            x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) return i;
    }
    return -1;
}

int32_t web_anchor_y(const WebDoc *doc, const char *name)
{
    for (int i = 0; i < doc->anchor_count; i++)
        if (strcmp(web_text(doc, doc->anchors[i].name), name) == 0) return doc->anchors[i].y;
    return -1;
}

static void q_append(char *out, size_t size, const char *s)
{
    size_t a = strlen(out), b = strlen(s);
    if (a + b >= size) b = size - a - 1;
    memcpy(out + a, s, b);
    out[a + b] = '\0';
}

void web_form_query(const WebDoc *doc, int form, int submitter, char *out, size_t size)
{
    char enc[400];
    out[0] = '\0';
    q_append(out, size, "?");
    bool first = true;
    for (int i = 0; i < doc->field_count; i++) {
        const WebField *f = &doc->fields[i];
        if (f->form != form || !f->name[0]) continue;
        if (f->kind == FIELD_SUBMIT && i != submitter) continue;
        if (f->kind == FIELD_CHECKBOX && !f->checked) continue;
        if (!first) q_append(out, size, "&");
        first = false;
        url_encode_component(f->name, enc, sizeof(enc));
        q_append(out, size, enc);
        q_append(out, size, "=");
        url_encode_component(f->value, enc, sizeof(enc));
        q_append(out, size, enc);
    }
}
