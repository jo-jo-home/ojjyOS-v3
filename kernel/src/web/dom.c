/*
 * ojjyOS v3 - Web engine: arena + tolerant HTML tree builder (see dom.h)
 *
 * Input is untrusted: every scan is bounded by the input length, every
 * table by its capacity, recursion is avoided (an explicit open-element
 * stack, DOM_MAX_DEPTH deep), and arena exhaustion only truncates.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "web/dom.h"
#include "string.h"

/* ------------------------------------------------------------------ */
/* Arena                                                               */
/* ------------------------------------------------------------------ */

void arena_init(WebArena *a, void *mem, size_t cap)
{
    a->mem = (uint8_t *)mem;
    a->cap = cap;
    a->used = 0;
    a->full = false;
}

void arena_reset(WebArena *a)
{
    a->used = 0;
    a->full = false;
}

void *arena_alloc(WebArena *a, size_t n)
{
    size_t start = (a->used + 7) & ~(size_t)7;
    if (!a->mem || n > a->cap || start > a->cap - n) { a->full = true; return NULL; }
    a->used = start + n;
    memset(a->mem + start, 0, n);
    return a->mem + start;
}

char *arena_strndup(WebArena *a, const char *s, size_t n)
{
    char *d = (char *)arena_alloc(a, n + 1);
    if (!d) return NULL;
    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

/* ------------------------------------------------------------------ */
/* Tags                                                                */
/* ------------------------------------------------------------------ */

static const char *const tag_names[TAG_COUNT] = {
    "", "html", "head", "body", "title", "meta", "link", "style", "script", "noscript", "base",
    "div", "span", "p", "a", "img", "br", "hr", "h1", "h2", "h3", "h4", "h5", "h6",
    "ul", "ol", "li", "dl", "dt", "dd", "table", "thead", "tbody", "tfoot", "tr", "td",
    "th", "caption", "colgroup", "col", "form", "input", "button", "select", "option",
    "optgroup", "textarea", "label", "pre", "code", "b", "strong", "i", "em", "u", "s",
    "small", "big", "sub", "sup", "blockquote", "center", "font", "section", "article",
    "header", "footer", "nav", "main", "aside", "figure", "figcaption", "svg", "math",
    "template", "iframe", "video", "audio", "canvas", "object", "embed", "picture", "source",
    "details", "summary", "fieldset", "legend", "address", "menu", "kbd", "samp", "tt",
    "var", "cite", "q", "abbr", "mark", "del", "ins", "strike", "wbr", "area", "map",
    "track", "param", "xmp", "listing", "plaintext", "dialog", "hgroup", "search", "time",
    "dfn", "nobr",
};

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

WebTag dom_tag_id(const char *name, size_t len)
{
    for (int i = 1; i < TAG_COUNT; i++) {
        const char *t = tag_names[i];
        size_t k = 0;
        while (k < len && t[k] && t[k] == lower(name[k])) k++;
        if (k == len && !t[k]) return (WebTag)i;
    }
    return TAG_OTHER;
}

const char *dom_tag_name(WebTag tag) { return tag < TAG_COUNT ? tag_names[tag] : ""; }

static bool is_void(int t)
{
    switch (t) {
        case TAG_BR: case TAG_IMG: case TAG_INPUT: case TAG_HR: case TAG_META: case TAG_LINK: case TAG_AREA:
        case TAG_BASE: case TAG_COL: case TAG_EMBED: case TAG_SOURCE: case TAG_TRACK: case TAG_WBR: case TAG_PARAM:
            return true;
        default: return false;
    }
}

/* Elements whose start tag closes an open <p> (HTML "closes a p element"). */
static bool closes_p(int t)
{
    switch (t) {
        case TAG_P: case TAG_DIV: case TAG_UL: case TAG_OL: case TAG_DL: case TAG_TABLE: case TAG_H1: case TAG_H2:
        case TAG_H3: case TAG_H4: case TAG_H5: case TAG_H6: case TAG_PRE: case TAG_BLOCKQUOTE: case TAG_SECTION:
        case TAG_ARTICLE: case TAG_HEADER: case TAG_FOOTER: case TAG_NAV: case TAG_MAIN: case TAG_ASIDE:
        case TAG_FIGURE: case TAG_FORM: case TAG_HR: case TAG_ADDRESS: case TAG_FIELDSET: case TAG_DETAILS:
        case TAG_MENU: case TAG_CENTER: case TAG_DIALOG: case TAG_HGROUP: case TAG_SEARCH: case TAG_LISTING:
        case TAG_XMP: case TAG_FIGCAPTION: case TAG_SUMMARY: case TAG_LI: case TAG_DT: case TAG_DD:
        case TAG_PLAINTEXT:
            return true;
        default: return false;
    }
}

/* "Special" elements stop the search for a matching end tag of an inline
 * (formatting) element, so a stray </span> cannot close a whole <div>. */
static bool is_special(int t)
{
    switch (t) {
        case TAG_HTML: case TAG_BODY: case TAG_DIV: case TAG_P: case TAG_UL: case TAG_OL: case TAG_LI: case TAG_DL:
        case TAG_DT: case TAG_DD: case TAG_TABLE: case TAG_TBODY: case TAG_THEAD: case TAG_TFOOT: case TAG_TR:
        case TAG_TD: case TAG_TH: case TAG_SECTION: case TAG_ARTICLE: case TAG_HEADER: case TAG_FOOTER: case TAG_NAV:
        case TAG_MAIN: case TAG_ASIDE: case TAG_FORM: case TAG_BLOCKQUOTE: case TAG_PRE: case TAG_H1: case TAG_H2:
        case TAG_H3: case TAG_H4: case TAG_H5: case TAG_H6: case TAG_BUTTON: case TAG_FIELDSET: case TAG_FIGURE:
        case TAG_SELECT: case TAG_CAPTION: case TAG_DETAILS: case TAG_CENTER: case TAG_MENU: case TAG_ADDRESS:
        case TAG_TEMPLATE: case TAG_SVG: case TAG_MATH: case TAG_IFRAME: case TAG_OBJECT: case TAG_DIALOG:
            return true;
        default: return false;
    }
}

static bool is_head_element(int t)
{
    return t == TAG_TITLE || t == TAG_META || t == TAG_LINK || t == TAG_STYLE || t == TAG_SCRIPT || t == TAG_BASE;
}

/* ------------------------------------------------------------------ */
/* Characters                                                          */
/* ------------------------------------------------------------------ */

size_t web_utf8_decode(const unsigned char *p, const unsigned char *end, uint32_t *cp)
{
    unsigned char c = p[0];
    if (c < 0x80) { *cp = c; return 1; }
    int len = c >= 0xF0 && c < 0xF8 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 ? 2 : 0;
    if (!len || p + len > end) { *cp = 0xFFFD; return 1; }
    uint32_t v = c & (len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07);
    for (int i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        v = (v << 6) | (p[i] & 0x3F);
    }
    *cp = v > 0x10FFFF ? 0xFFFD : v;
    return (size_t)len;
}

int web_utf8_encode(uint32_t cp, char *o)
{
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static const char latin1_base[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";

const char *web_translit(uint32_t cp)
{
    static char one[2];
    if (cp >= 0xC0 && cp <= 0xFF) {
        if (cp == 0xC6) return "AE";
        if (cp == 0xE6) return "ae";
        if (cp == 0xDF) return "ss";
        one[0] = latin1_base[cp - 0xC0]; one[1] = 0;
        return one;
    }
    switch (cp) {
        case 0xA0: case 0x2007: case 0x202F: case 0x2002: case 0x2003: case 0x2009: case 0x200A: return " ";
        case 0x200B: case 0x200C: case 0x200D: case 0xFEFF: case 0xAD: case 0x200E: case 0x200F: return "";
        case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2212: return "-";
        case 0x2014: case 0x2015: return "--";
        case 0x2018: case 0x2019: case 0x201A: case 0x2032: return "'";
        case 0x201C: case 0x201D: case 0x201E: case 0x2033: return "\"";
        case 0x2022: case 0x2023: case 0x25CF: case 0x25E6: case 0x2219: return "*";
        case 0xB7: case 0x22C5: return ".";
        case 0x2026: return "...";
        case 0xA9: return "(c)";
        case 0xAE: return "(R)";
        case 0x2122: return "TM";
        case 0x20AC: return "EUR";
        case 0xA3: return "GBP";
        case 0xA5: return "JPY";
        case 0xB0: return "deg";
        case 0xD7: return "x";
        case 0xF7: return "/";
        case 0xAB: return "<<";
        case 0xBB: return ">>";
        case 0x2039: return "<";
        case 0x203A: return ">";
        case 0x2190: return "<-";
        case 0x2192: return "->";
        case 0x2191: return "^";
        case 0x2193: return "v";
        case 0x21D2: return "=>";
        case 0x2264: return "<=";
        case 0x2265: return ">=";
        case 0x2260: return "!=";
        case 0x2248: return "~";
        case 0x2713: case 0x2714: return "v";
        case 0x2717: case 0x2718: case 0x2715: case 0x2716: return "x";
        case 0x2605: case 0x2606: return "*";
        case 0x25B6: case 0x25BA: return ">";
        case 0x25C0: case 0x25C4: return "<";
        case 0x25BC: case 0x25BE: return "v";
        case 0x25B2: case 0x25B4: return "^";
        case 0x2630: return "=";
        case 0xFFFD: return "?";
        default: break;
    }
    if (cp >= 0x1F000 || (cp >= 0x2600 && cp < 0x2800) || (cp >= 0xE000 && cp < 0xF900)) return "";  /* emoji, icons */
    if (cp >= 0x0300 && cp < 0x0370) return "";        /* combining marks */
    return "?";
}

/* Windows-1252 0x80..0x9F */
static const uint16_t cp1252[32] = {
    0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0x8D, 0x17D, 0x8F,
    0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178,
};

typedef struct { const char *name; uint16_t cp; } Entity;
static const Entity entities[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
    {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122}, {"mdash", 0x2014}, {"ndash", 0x2013},
    {"hellip", 0x2026}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
    {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"sbquo", 0x201A}, {"bdquo", 0x201E}, {"bull", 0x2022},
    {"middot", 0xB7}, {"euro", 0x20AC}, {"pound", 0xA3}, {"yen", 0xA5}, {"cent", 0xA2}, {"deg", 0xB0},
    {"times", 0xD7}, {"divide", 0xF7}, {"larr", 0x2190}, {"rarr", 0x2192}, {"uarr", 0x2191},
    {"darr", 0x2193}, {"harr", 0x2194}, {"rArr", 0x21D2}, {"lArr", 0x21D0}, {"sect", 0xA7},
    {"para", 0xB6}, {"plusmn", 0xB1}, {"frac12", 0xBD}, {"frac14", 0xBC}, {"frac34", 0xBE},
    {"shy", 0xAD}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"thinsp", 0x2009}, {"zwj", 0x200D},
    {"zwnj", 0x200C}, {"le", 0x2264}, {"ge", 0x2265}, {"ne", 0x2260}, {"minus", 0x2212},
    {"iexcl", 0xA1}, {"iquest", 0xBF}, {"ordf", 0xAA}, {"ordm", 0xBA}, {"micro", 0xB5},
    {"sup1", 0xB9}, {"sup2", 0xB2}, {"sup3", 0xB3}, {"acute", 0xB4}, {"cedil", 0xB8},
    {"uml", 0xA8}, {"macr", 0xAF}, {"not", 0xAC}, {"curren", 0xA4}, {"brvbar", 0xA6},
    {"dagger", 0x2020}, {"Dagger", 0x2021}, {"permil", 0x2030}, {"prime", 0x2032}, {"Prime", 0x2033},
    {"lsaquo", 0x2039}, {"rsaquo", 0x203A}, {"infin", 0x221E}, {"asymp", 0x2248}, {"hearts", 0x2665},
    {"check", 0x2713}, {"star", 0x2606}, {"starf", 0x2605}, {"lrm", 0x200E}, {"rlm", 0x200F},
    {"Agrave", 0xC0}, {"Aacute", 0xC1}, {"Acirc", 0xC2}, {"Atilde", 0xC3}, {"Auml", 0xC4}, {"Aring", 0xC5},
    {"AElig", 0xC6}, {"Ccedil", 0xC7}, {"Egrave", 0xC8}, {"Eacute", 0xC9}, {"Ecirc", 0xCA}, {"Euml", 0xCB},
    {"Igrave", 0xCC}, {"Iacute", 0xCD}, {"Icirc", 0xCE}, {"Iuml", 0xCF}, {"ETH", 0xD0}, {"Ntilde", 0xD1},
    {"Ograve", 0xD2}, {"Oacute", 0xD3}, {"Ocirc", 0xD4}, {"Otilde", 0xD5}, {"Ouml", 0xD6}, {"Oslash", 0xD8},
    {"Ugrave", 0xD9}, {"Uacute", 0xDA}, {"Ucirc", 0xDB}, {"Uuml", 0xDC}, {"Yacute", 0xDD}, {"THORN", 0xDE},
    {"szlig", 0xDF}, {"agrave", 0xE0}, {"aacute", 0xE1}, {"acirc", 0xE2}, {"atilde", 0xE3}, {"auml", 0xE4},
    {"aring", 0xE5}, {"aelig", 0xE6}, {"ccedil", 0xE7}, {"egrave", 0xE8}, {"eacute", 0xE9}, {"ecirc", 0xEA},
    {"euml", 0xEB}, {"igrave", 0xEC}, {"iacute", 0xED}, {"icirc", 0xEE}, {"iuml", 0xEF}, {"eth", 0xF0},
    {"ntilde", 0xF1}, {"ograve", 0xF2}, {"oacute", 0xF3}, {"ocirc", 0xF4}, {"otilde", 0xF5}, {"ouml", 0xF6},
    {"oslash", 0xF8}, {"ugrave", 0xF9}, {"uacute", 0xFA}, {"ucirc", 0xFB}, {"uuml", 0xFC}, {"yacute", 0xFD},
    {"thorn", 0xFE}, {"yuml", 0xFF}, {"OElig", 0x152}, {"oelig", 0x153}, {"Scaron", 0x160}, {"scaron", 0x161},
    {"Yuml", 0x178}, {"fnof", 0x192}, {"circ", 0x2C6}, {"tilde", 0x2DC}, {"alpha", 0x3B1}, {"beta", 0x3B2},
    {"gamma", 0x3B3}, {"delta", 0x3B4}, {"pi", 0x3C0}, {"mu", 0x3BC}, {"sigma", 0x3C3}, {"lambda", 0x3BB},
    {"Omega", 0x3A9}, {"omega", 0x3C9}, {"theta", 0x3B8}, {"sum", 0x2211}, {"radic", 0x221A},
};

/* Decode an entity at p ('&' already consumed). Returns bytes consumed after '&' (0 = not an entity). */
static size_t decode_entity(const char *p, const char *end, uint32_t *cp)
{
    if (p < end && *p == '#') {
        uint32_t v = 0;
        size_t i = 1;
        bool hex = p + 1 < end && (p[1] == 'x' || p[1] == 'X');
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
        if (v >= 0x80 && v < 0xA0) v = cp1252[v - 0x80];
        *cp = (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v < 0xE000)) ? 0xFFFD : v;
        return i;
    }
    size_t n = 0;
    while (p + n < end && n < 10 && ((p[n] >= 'a' && p[n] <= 'z') || (p[n] >= 'A' && p[n] <= 'Z') || (p[n] >= '0' && p[n] <= '9'))) n++;
    for (unsigned k = 0; k < ARRAY_SIZE(entities); k++) {
        size_t l = strlen(entities[k].name);
        if (l == n && strncmp(p, entities[k].name, l) == 0) {
            *cp = entities[k].cp;
            return (p + l < end && p[l] == ';') ? l + 1 : l;
        }
    }
    /* Legacy forms without ';' followed by more letters ("&copy2026", "&ampx"). */
    static const char *const legacy[] = {"amp", "lt", "gt", "quot", "nbsp", "copy", "reg"};
    static const uint16_t legacy_cp[] = {'&', '<', '>', '"', 0xA0, 0xA9, 0xAE};
    for (unsigned k = 0; k < ARRAY_SIZE(legacy); k++) {
        size_t l = strlen(legacy[k]);
        if (n > l && strncmp(p, legacy[k], l) == 0) { *cp = legacy_cp[k]; return l; }
    }
    return 0;
}

/* Text builder: decoded UTF-8 into a growing arena buffer. */
typedef struct { char *buf; size_t len, cap; } TextBuf;

static void tb_put(WebArena *a, TextBuf *t, const char *s, size_t n)
{
    if (t->len + n + 1 > t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 256;
        while (cap < t->len + n + 1) cap *= 2;
        char *nb = (char *)arena_alloc(a, cap);
        if (!nb) return;
        if (t->len) memcpy(nb, t->buf, t->len);
        t->buf = nb;
        t->cap = cap;
    }
    memcpy(t->buf + t->len, s, n);
    t->len += n;
    t->buf[t->len] = '\0';
}

/* Decode [p,end) (entities optional) into t as UTF-8. */
static void decode_into(WebArena *a, TextBuf *t, const char *p, const char *end, bool entities_on, bool latin1)
{
    char tmp[256];
    size_t k = 0;
    while (p < end) {
        unsigned char c = (unsigned char)*p;
        uint32_t cp;
        if (c == '&' && entities_on) {
            size_t n = decode_entity(p + 1, end, &cp);
            if (n) p += 1 + n; else { cp = '&'; p++; }
        } else if (c >= 0x80) {
            if (latin1) { cp = c < 0xA0 ? cp1252[c - 0x80] : c; p++; }
            else p += web_utf8_decode((const unsigned char *)p, (const unsigned char *)end, &cp);
        } else {
            cp = c ? c : 0xFFFD;
            p++;
        }
        if (k + 4 >= sizeof(tmp)) { tb_put(a, t, tmp, k); k = 0; }
        k += (size_t)web_utf8_encode(cp, tmp + k);
    }
    if (k) tb_put(a, t, tmp, k);
}

/* ------------------------------------------------------------------ */
/* Tree building                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    DomTree *tree;
    WebArena *a;
    bool latin1;
    DomNode *stack[DOM_MAX_DEPTH];
    int depth;                  /* stack[0] = html */
    int foreign;                /* inside <svg>/<math>: "/>" self-closes */
} Builder;

static DomNode *new_node(Builder *B, int type)
{
    DomNode *n = (DomNode *)arena_alloc(B->a, sizeof(DomNode));
    if (!n) { B->tree->truncated = true; return NULL; }
    n->type = (uint8_t)type;
    B->tree->node_count++;
    return n;
}

static void append(DomNode *parent, DomNode *n)
{
    n->parent = parent;
    n->prev = parent->last;
    if (parent->last) parent->last->next = n; else parent->first = n;
    parent->last = n;
    if (n->type == DOM_ELEMENT) {
        uint16_t idx = 1;
        for (DomNode *s = n->prev; s; s = s->prev) if (s->type == DOM_ELEMENT) { idx = (uint16_t)(s->child_index + 1); break; }
        n->child_index = idx;
    }
}

static DomNode *current(Builder *B) { return B->stack[B->depth - 1]; }

static void ensure_body(Builder *B)
{
    DomTree *T = B->tree;
    if (T->body) return;
    DomNode *b = new_node(B, DOM_ELEMENT);
    if (!b) return;
    b->tag = TAG_BODY;
    b->name = "body";
    append(T->root, b);
    T->body = b;
    B->depth = 1;
    B->stack[B->depth++] = b;
}

static void add_text(Builder *B, const char *p, const char *end)
{
    if (p >= end) return;
    if (!B->tree->body) {
        const char *q = p;
        while (q < end && (*q == ' ' || *q == '\n' || *q == '\t' || *q == '\r' || *q == '\f')) q++;
        if (q == end) return;                   /* whitespace in the head */
        ensure_body(B);
        if (!B->tree->body) return;
    }
    DomNode *parent = current(B);
    TextBuf t = {0};
    DomNode *last = parent->last;
    if (last && last->type == DOM_TEXT) {
        /* Merge adjacent text (an entity split, a skipped comment). */
        t.buf = (char *)last->text; t.len = last->text_len; t.cap = last->text_len + 1;
        decode_into(B->a, &t, p, end, true, B->latin1);
        if (t.buf) { last->text = t.buf; last->text_len = (uint32_t)t.len; }
        return;
    }
    decode_into(B->a, &t, p, end, true, B->latin1);
    if (!t.buf) return;
    DomNode *n = new_node(B, DOM_TEXT);
    if (!n) return;
    n->text = t.buf;
    n->text_len = (uint32_t)t.len;
    append(parent, n);
}

static void pop_to(Builder *B, int index)
{
    if (index >= 1) B->depth = index;           /* never pop <html> */
}

/* Index in the stack of the nearest element with tag t, stopping at any of
 * the `stop` tags (0-terminated). -1 if not found. */
static int find_open(Builder *B, int t, const int *stop)
{
    for (int i = B->depth - 1; i >= 1; i--) {
        int tag = B->stack[i]->tag;
        if (tag == t) return i;
        for (const int *s = stop; s && *s; s++) if (tag == *s) return -1;
    }
    return -1;
}

static void close_p(Builder *B)
{
    static const int stop[] = {TAG_BUTTON, TAG_TABLE, TAG_TD, TAG_TH, TAG_HTML, TAG_TEMPLATE, TAG_CAPTION, 0};
    int i = find_open(B, TAG_P, stop);
    if (i >= 0) pop_to(B, i);
}

static void before_start_tag(Builder *B, int t)
{
    if (closes_p(t)) close_p(B);
    switch (t) {
        case TAG_LI: {
            static const int stop[] = {TAG_UL, TAG_OL, TAG_MENU, TAG_TABLE, TAG_TD, TAG_TH, 0};
            int i = find_open(B, TAG_LI, stop);
            if (i >= 0) pop_to(B, i);
            break;
        }
        case TAG_DT: case TAG_DD: {
            static const int stop[] = {TAG_DL, TAG_TABLE, TAG_TD, 0};
            int i = find_open(B, TAG_DT, stop);
            int j = find_open(B, TAG_DD, stop);
            if (j > i) i = j;
            if (i >= 0) pop_to(B, i);
            break;
        }
        case TAG_OPTION:
            if (current(B)->tag == TAG_OPTION) B->depth--;
            break;
        case TAG_OPTGROUP:
            if (current(B)->tag == TAG_OPTION) B->depth--;
            if (current(B)->tag == TAG_OPTGROUP) B->depth--;
            break;
        case TAG_TR: {
            static const int stop[] = {TAG_TABLE, 0};
            int i = find_open(B, TAG_TR, stop);
            if (i >= 0) pop_to(B, i);
            break;
        }
        case TAG_TD: case TAG_TH: {
            static const int stop[] = {TAG_TR, TAG_TABLE, 0};
            int i = find_open(B, TAG_TD, stop);
            int j = find_open(B, TAG_TH, stop);
            if (j > i) i = j;
            if (i >= 0) pop_to(B, i);
            break;
        }
        case TAG_THEAD: case TAG_TBODY: case TAG_TFOOT: {
            static const int stop[] = {TAG_TABLE, 0};
            int best = -1;
            int tags[3] = {TAG_THEAD, TAG_TBODY, TAG_TFOOT};
            for (int k = 0; k < 3; k++) { int i = find_open(B, tags[k], stop); if (i > best) best = i; }
            if (best >= 0) pop_to(B, best);
            break;
        }
        case TAG_H1: case TAG_H2: case TAG_H3: case TAG_H4: case TAG_H5: case TAG_H6: {
            int c = current(B)->tag;
            if (c >= TAG_H1 && c <= TAG_H6) B->depth--;
            break;
        }
        case TAG_A: {
            static const int stop[] = {TAG_TABLE, TAG_TD, TAG_TH, 0};
            int i = find_open(B, TAG_A, stop);
            if (i >= 0) pop_to(B, i);
            break;
        }
        case TAG_BUTTON: {
            int i = find_open(B, TAG_BUTTON, NULL);
            if (i >= 0) pop_to(B, i);
            break;
        }
        case TAG_NOBR: {
            int i = find_open(B, TAG_NOBR, NULL);
            if (i >= 0) pop_to(B, i);
            break;
        }
        default: break;
    }
}

static void end_tag(Builder *B, int t, const char *name, size_t nlen)
{
    if (t == TAG_HTML || t == TAG_BODY || t == TAG_HEAD) return;
    if (t == TAG_SVG || t == TAG_MATH) { if (B->foreign) B->foreign--; }
    bool inline_tag = t == TAG_OTHER ? false : !is_special(t);
    for (int i = B->depth - 1; i >= 1; i--) {
        DomNode *n = B->stack[i];
        bool match = t != TAG_OTHER ? n->tag == t
                                    : (n->tag == TAG_OTHER && strlen(n->name) == nlen && strncmp(n->name, name, nlen) == 0);
        if (match) { pop_to(B, i); return; }
        if (inline_tag && is_special(n->tag) && !B->foreign) return;   /* stray inline end tag */
        if (t == TAG_P || t == TAG_LI || t == TAG_DD || t == TAG_DT) {
            if (n->tag == TAG_TABLE || n->tag == TAG_TD || n->tag == TAG_TH || n->tag == TAG_BUTTON) return;
        }
        if ((t == TAG_TD || t == TAG_TH || t == TAG_TR) && n->tag == TAG_TABLE) return;
    }
}

static bool name_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == ':' || c == '_' || c == '.';
}

static bool space(char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f'; }

/* Find "</name" (case-insensitive) from p. */
static const char *find_end_tag(const char *p, const char *end, const char *name)
{
    size_t n = strlen(name);
    for (; p + 2 + n <= end; p++) {
        if (p[0] != '<' || p[1] != '/') continue;
        size_t i = 0;
        while (i < n && lower(p[2 + i]) == name[i]) i++;
        if (i == n && (p + 2 + n == end || !name_char(p[2 + n]))) return p;
    }
    return end;
}

static const char *skip_past(const char *p, const char *end, char c)
{
    while (p < end && *p != c) p++;
    return p < end ? p + 1 : end;
}

#define MAX_ATTRS 32

/* Parse a start tag's attributes at p (after the name). Fills attrs (arena).
 * Returns pointer after '>' and sets *self_closing. */
static const char *parse_attrs(Builder *B, const char *p, const char *end, DomNode *n, bool *self_closing)
{
    DomAttr tmp[MAX_ATTRS];
    int na = 0;
    *self_closing = false;
    for (int guard = 0; p < end && guard < 8192; guard++) {
        while (p < end && space(*p)) p++;
        if (p >= end) break;
        if (*p == '>') { p++; break; }
        if (*p == '/') {
            p++;
            if (p < end && *p == '>') { *self_closing = true; p++; break; }
            continue;
        }
        const char *an = p;
        while (p < end && *p != '=' && *p != '>' && !space(*p) && *p != '/') p++;
        size_t al = (size_t)(p - an);
        if (!al) { p++; continue; }                  /* stray character: progress */
        while (p < end && space(*p)) p++;
        const char *vs = NULL, *ve = NULL;
        if (p < end && *p == '=') {
            p++;
            while (p < end && space(*p)) p++;
            if (p < end && (*p == '"' || *p == '\'')) {
                char q = *p++;
                vs = p;
                while (p < end && *p != q) p++;
                ve = p;
                if (p < end) p++;
            } else {
                vs = p;
                while (p < end && !space(*p) && *p != '>') p++;
                ve = p;
            }
        }
        if (na >= MAX_ATTRS || al > 64) continue;
        bool dup = false;
        for (int i = 0; i < na; i++) {
            size_t l = strlen(tmp[i].name);
            if (l == al) { size_t k = 0; while (k < al && tmp[i].name[k] == lower(an[k])) k++; if (k == al) dup = true; }
        }
        if (dup) continue;
        char *nm = (char *)arena_alloc(B->a, al + 1);
        if (!nm) break;
        for (size_t k = 0; k < al; k++) nm[k] = lower(an[k]);
        TextBuf t = {0};
        if (vs) {
            if (ve - vs > 16384) ve = vs + 16384;
            decode_into(B->a, &t, vs, ve, true, B->latin1);
        }
        tmp[na].name = nm;
        tmp[na].value = t.buf ? t.buf : "";
        na++;
    }
    if (na) {
        n->attrs = (DomAttr *)arena_alloc(B->a, sizeof(DomAttr) * (size_t)na);
        if (n->attrs) { memcpy(n->attrs, tmp, sizeof(DomAttr) * (size_t)na); n->nattrs = (uint16_t)na; }
    }
    /* id and classes for selector matching */
    const char *id = dom_attr(n, "id");
    if (id && id[0]) n->id = id;
    const char *cls = dom_attr(n, "class");
    if (cls) {
        int count = 0;
        for (const char *c = cls; *c;) {
            while (*c && space(*c)) c++;
            if (!*c) break;
            count++;
            while (*c && !space(*c)) c++;
        }
        if (count > 64) count = 64;
        if (count) {
            n->classes = (const char **)arena_alloc(B->a, sizeof(char *) * (size_t)count);
            if (n->classes) {
                int k = 0;
                for (const char *c = cls; *c && k < count;) {
                    while (*c && space(*c)) c++;
                    if (!*c) break;
                    const char *s = c;
                    while (*c && !space(*c)) c++;
                    n->classes[k] = arena_strndup(B->a, s, (size_t)(c - s));
                    if (n->classes[k]) k++;
                }
                n->nclasses = (uint16_t)k;
            }
        }
    }
    return p;
}

static void push(Builder *B, DomNode *n)
{
    if (B->depth < DOM_MAX_DEPTH) B->stack[B->depth++] = n;
}

void dom_parse(DomTree *T, WebArena *a, const char *html, size_t len, bool plain, bool latin1)
{
    static Builder B;           /* ~1.3 KB of stack pointers: keep it off the kernel stack */
    memset(T, 0, sizeof(*T));
    memset(&B, 0, sizeof(B));
    B.tree = T;
    B.a = a;
    B.latin1 = latin1;
    DomNode *root = new_node(&B, DOM_ELEMENT);
    DomNode *head = new_node(&B, DOM_ELEMENT);
    if (!root || !head) return;
    root->tag = TAG_HTML; root->name = "html"; root->child_index = 1;
    head->tag = TAG_HEAD; head->name = "head";
    append(root, head);
    T->root = root;
    T->head = head;
    B.stack[0] = root;
    B.depth = 1;

    const char *p = html, *end = html + len;
    bool saw_doctype = false;
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    if (plain) {
        ensure_body(&B);
        DomNode *pre = new_node(&B, DOM_ELEMENT);
        if (!pre || !T->body) return;
        pre->tag = TAG_PRE; pre->name = "pre";
        append(T->body, pre);
        B.stack[B.depth++] = pre;
        TextBuf t = {0};
        decode_into(a, &t, p, end, false, latin1);
        DomNode *tx = new_node(&B, DOM_TEXT);
        if (tx && t.buf) { tx->text = t.buf; tx->text_len = (uint32_t)t.len; append(pre, tx); }
        return;
    }

    while (p < end && !a->full) {
        if (*p != '<') {
            const char *t = p;
            while (t < end && *t != '<') t++;
            add_text(&B, p, t);
            p = t;
            continue;
        }
        if (p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-') {      /* comment */
            const char *c = p + 4;
            while (c + 2 < end && !(c[0] == '-' && c[1] == '-' && c[2] == '>')) c++;
            p = c + 2 < end ? c + 3 : end;
            continue;
        }
        if (p + 8 < end && strncmp(p, "<![CDATA[", 9) == 0) {
            const char *c = p + 9;
            while (c + 2 < end && !(c[0] == ']' && c[1] == ']' && c[2] == '>')) c++;
            if (B.foreign) add_text(&B, p + 9, c);
            p = c + 2 < end ? c + 3 : end;
            continue;
        }
        if (p + 1 < end && (p[1] == '!' || p[1] == '?')) {
            if (p + 9 < end && lower(p[2]) == 'd' && lower(p[3]) == 'o' && lower(p[4]) == 'c' && lower(p[5]) == 't') {
                /* <!DOCTYPE html> (any public id with "html" and no legacy DTD) selects standards mode */
                const char *e = skip_past(p, end, '>');
                bool html5 = false, legacy = false;
                for (const char *q = p + 9; q + 4 < e; q++) {
                    if (lower(q[0]) == 'h' && lower(q[1]) == 't' && lower(q[2]) == 'm' && lower(q[3]) == 'l') html5 = true;
                    if (q + 12 < e && lower(q[0]) == 't' && lower(q[1]) == 'r' && lower(q[2]) == 'a' && lower(q[3]) == 'n' &&
                        lower(q[4]) == 's') legacy = true;
                }
                T->quirks = !html5 || legacy;
                saw_doctype = true;
                p = e;
                continue;
            }
            p = skip_past(p, end, '>');
            continue;
        }
        bool closing = p + 1 < end && p[1] == '/';
        const char *ns = p + (closing ? 2 : 1);
        if (ns >= end || !((*ns >= 'a' && *ns <= 'z') || (*ns >= 'A' && *ns <= 'Z'))) {
            if (closing) { p = skip_past(p, end, '>'); continue; }  /* "</ >" bogus comment */
            add_text(&B, p, p + 1);
            p++;
            continue;
        }
        const char *ne = ns;
        while (ne < end && name_char(*ne)) ne++;
        size_t nlen = (size_t)(ne - ns);
        int t = dom_tag_id(ns, nlen);
        if (closing) {
            p = skip_past(ne, end, '>');
            if (t == TAG_BR) { t = TAG_BR; closing = false; }
            else if (t == TAG_P && find_open(&B, TAG_P, NULL) < 0) { continue; }
            else { if (T->body || !is_head_element(t)) end_tag(&B, t, ns, nlen); continue; }
            /* </br> acts as <br> */
            ensure_body(&B);
            DomNode *br = new_node(&B, DOM_ELEMENT);
            if (br) { br->tag = TAG_BR; br->name = "br"; append(current(&B), br); }
            continue;
        }

        DomNode *n = new_node(&B, DOM_ELEMENT);
        if (!n) break;
        n->tag = (uint8_t)t;
        if (t != TAG_OTHER) n->name = tag_names[t];
        else {
            char *nm = (char *)arena_alloc(a, nlen + 1);
            if (!nm) break;
            for (size_t k = 0; k < nlen && k < 64; k++) nm[k] = lower(ns[k]);
            nm[nlen < 64 ? nlen : 64] = '\0';
            n->name = nm;
        }
        bool self_closing;
        p = parse_attrs(&B, ne, end, n, &self_closing);

        if (t == TAG_HTML || t == TAG_HEAD) {
            if (t == TAG_HTML && n->nattrs && !root->nattrs) { root->attrs = n->attrs; root->nattrs = n->nattrs; }
            continue;
        }
        if (t == TAG_BODY) {
            if (!T->body) {
                n->name = "body";
                append(root, n);
                T->body = n;
                B.depth = 1;
                B.stack[B.depth++] = n;
            } else if (!T->body->nattrs && n->nattrs) {
                T->body->attrs = n->attrs; T->body->nattrs = n->nattrs;
                T->body->id = n->id; T->body->classes = n->classes; T->body->nclasses = n->nclasses;
            }
            continue;
        }
        DomNode *parent;
        if (!T->body && (is_head_element(t) || t == TAG_NOSCRIPT)) {
            parent = head;
        } else {
            ensure_body(&B);
            if (!T->body) break;
            if (!B.foreign) before_start_tag(&B, t);
            parent = current(&B);
        }
        append(parent, n);

        /* Raw text elements: content up to the matching end tag. */
        if (t == TAG_SCRIPT || t == TAG_STYLE || t == TAG_TITLE || t == TAG_TEXTAREA || t == TAG_XMP ||
            t == TAG_PLAINTEXT || t == TAG_IFRAME) {
            if (self_closing && B.foreign) continue;
            const char *e = t == TAG_PLAINTEXT ? end : find_end_tag(p, end, n->name);
            bool decode = t == TAG_TITLE || t == TAG_TEXTAREA;
            if (t != TAG_IFRAME && t != TAG_SCRIPT) {
                TextBuf tb = {0};
                decode_into(a, &tb, p, e, decode, latin1);
                DomNode *tx = tb.buf ? new_node(&B, DOM_TEXT) : NULL;
                if (tx) { tx->text = tb.buf; tx->text_len = (uint32_t)tb.len; append(n, tx); }
            } else if (t == TAG_SCRIPT) {
                n->text_len = (uint32_t)(e - p);     /* script size (JS detection), content not kept */
            }
            p = e < end ? skip_past(e, end, '>') : end;
            continue;
        }
        if (is_void(t)) continue;
        if (self_closing && (B.foreign || t == TAG_OTHER)) continue;
        if (t == TAG_SVG || t == TAG_MATH) B.foreign++;
        if (parent == head) {
            /* <noscript> in the head: keep it, parse its content in place */
            if (t == TAG_NOSCRIPT) { const char *e = find_end_tag(p, end, "noscript"); p = e < end ? skip_past(e, end, '>') : end; }
            continue;
        }
        if (B.depth >= DOM_MAX_DEPTH) continue;      /* too deep: content goes to the parent */
        push(&B, n);
    }
    ensure_body(&B);
    if (!saw_doctype) T->quirks = true;
}

const char *dom_attr(const DomNode *n, const char *name)
{
    if (!n || n->type != DOM_ELEMENT) return NULL;
    for (int i = 0; i < n->nattrs; i++) if (strcmp(n->attrs[i].name, name) == 0) return n->attrs[i].value;
    return NULL;
}

bool dom_has_class(const DomNode *n, const char *cls)
{
    for (int i = 0; i < n->nclasses; i++) if (strcmp(n->classes[i], cls) == 0) return true;
    return false;
}

DomNode *dom_next(DomNode *n, DomNode *root, bool skip_children)
{
    if (!skip_children && n->first) return n->first;
    while (n && n != root) {
        if (n->next) return n->next;
        n = n->parent;
    }
    return NULL;
}
