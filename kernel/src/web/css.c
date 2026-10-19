/*
 * ojjyOS v3 - Web engine: CSS parser, selector matching and cascade (css.h)
 *
 * Untrusted input: every scan is bounded, nesting depth is limited, and the
 * rule/declaration tables have fixed capacities (overflow drops rules).
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "web/css.h"
#include "string.h"

#define MAX_RULES      24000
#define MAX_IMPORTS    16
#define MAX_COMPOUNDS  12
#define HASH_BUCKETS   4096
#define MAX_MATCHED    2048
#define VAR_DEPTH      6
#define VALUE_MAX      2048

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static bool ws(char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f'; }
static bool ident_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
           (unsigned char)c >= 0x80 || c == '\\';
}

static bool ieq(const char *a, size_t n, const char *b)
{
    size_t i = 0;
    for (; i < n && b[i]; i++) if (lc(a[i]) != b[i]) return false;
    return i == n && !b[i];
}

static bool istarts(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) if (lc(*s) != *prefix) return false;
    return true;
}

static const char *skip_ws(const char *p, const char *end) { while (p < end && ws(*p)) p++; return p; }

static uint32_t hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
    return h;
}

/* Skip a balanced (), [] or {} group starting at p (p points at the opener). */
static const char *skip_group(const char *p, const char *end)
{
    int depth = 0;
    while (p < end) {
        char c = *p;
        if (c == '"' || c == '\'') {
            p++;
            while (p < end && *p != c) { if (*p == '\\' && p + 1 < end) p++; p++; }
            if (p < end) p++;
            continue;
        }
        if (c == '(' || c == '[' || c == '{') depth++;
        else if (c == ')' || c == ']' || c == '}') { if (--depth <= 0) return p + 1; }
        p++;
    }
    return end;
}

/* Fixed-point number (hundredths). Advances *pp. */
static bool parse_num(const char **pp, const char *end, int32_t *out)
{
    const char *p = *pp;
    bool neg = false;
    if (p < end && (*p == '+' || *p == '-')) { neg = *p == '-'; p++; }
    int64_t v = 0;
    int digits = 0;
    while (p < end && *p >= '0' && *p <= '9') { if (v < 100000000) v = v * 10 + (*p - '0'); p++; digits++; }
    int64_t frac = 0;
    if (p + 1 < end && *p == '.' && p[1] >= '0' && p[1] <= '9') {
        p++;
        int f = 0;
        while (p < end && *p >= '0' && *p <= '9') { if (f < 2) { frac = frac * 10 + (*p - '0'); f++; } p++; digits++; }
        if (f == 1) frac *= 10;
    }
    if (!digits) return false;
    if (p < end && (*p == 'e' || *p == 'E') && p + 1 < end && ((p[1] >= '0' && p[1] <= '9') || p[1] == '-' || p[1] == '+')) {
        /* exponents: rare (1e3px); handle small integer exponents */
        p++;
        bool en = false;
        if (*p == '-' || *p == '+') { en = *p == '-'; p++; }
        int e = 0;
        while (p < end && *p >= '0' && *p <= '9') { if (e < 10) e = e * 10 + (*p - '0'); p++; }
        int64_t whole = v * 100 + frac;
        for (int i = 0; i < e; i++) whole = en ? whole / 10 : (whole < 1000000000 ? whole * 10 : whole);
        *out = (int32_t)(neg ? -whole : whole);
        *pp = p;
        return true;
    }
    int64_t r = v * 100 + frac;
    if (r > 2000000000) r = 2000000000;
    *out = (int32_t)(neg ? -r : r);
    *pp = p;
    return true;
}

/* ------------------------------------------------------------------ */
/* Data                                                                */
/* ------------------------------------------------------------------ */

typedef enum {
    P_UNKNOWN = 0, P_CUSTOM,
    P_DISPLAY, P_POSITION, P_FLOAT, P_VISIBILITY, P_COLOR, P_BACKGROUND, P_BACKGROUND_COLOR, P_BACKGROUND_IMAGE,
    P_FONT, P_FONT_SIZE, P_FONT_WEIGHT, P_FONT_STYLE, P_FONT_FAMILY, P_LINE_HEIGHT, P_TEXT_ALIGN,
    P_TEXT_DECORATION, P_TEXT_DECORATION_LINE, P_TEXT_TRANSFORM, P_WHITE_SPACE, P_TEXT_WRAP, P_TEXT_INDENT,
    P_MARGIN, P_MARGIN_T, P_MARGIN_R, P_MARGIN_B, P_MARGIN_L, P_MARGIN_INLINE, P_MARGIN_BLOCK,
    P_MARGIN_INLINE_START, P_MARGIN_INLINE_END, P_MARGIN_BLOCK_START, P_MARGIN_BLOCK_END,
    P_PADDING, P_PADDING_T, P_PADDING_R, P_PADDING_B, P_PADDING_L, P_PADDING_INLINE, P_PADDING_BLOCK,
    P_PADDING_INLINE_START, P_PADDING_INLINE_END, P_PADDING_BLOCK_START, P_PADDING_BLOCK_END,
    P_BORDER, P_BORDER_T, P_BORDER_R, P_BORDER_B, P_BORDER_L, P_BORDER_WIDTH, P_BORDER_COLOR, P_BORDER_STYLE,
    P_BORDER_T_WIDTH, P_BORDER_R_WIDTH, P_BORDER_B_WIDTH, P_BORDER_L_WIDTH,
    P_BORDER_T_COLOR, P_BORDER_R_COLOR, P_BORDER_B_COLOR, P_BORDER_L_COLOR,
    P_BORDER_T_STYLE, P_BORDER_R_STYLE, P_BORDER_B_STYLE, P_BORDER_L_STYLE, P_BORDER_RADIUS,
    P_WIDTH, P_HEIGHT, P_MIN_WIDTH, P_MAX_WIDTH, P_MIN_HEIGHT, P_MAX_HEIGHT, P_BOX_SIZING,
    P_LIST_STYLE, P_LIST_STYLE_TYPE, P_FLEX, P_FLEX_DIRECTION, P_FLEX_WRAP, P_FLEX_FLOW, P_FLEX_GROW,
    P_FLEX_SHRINK, P_FLEX_BASIS, P_JUSTIFY_CONTENT, P_ALIGN_ITEMS, P_ALIGN_SELF, P_GAP, P_ROW_GAP, P_COLUMN_GAP,
    P_ORDER, P_OVERFLOW, P_OVERFLOW_X, P_OVERFLOW_Y, P_OPACITY, P_CLIP, P_CLIP_PATH, P_VERTICAL_ALIGN,
    P_TOP, P_RIGHT, P_BOTTOM, P_LEFT, P_INSET, P_TABLE_LAYOUT, P_BORDER_SPACING, P_GRID_TEMPLATE_COLUMNS,
    P_CONTENT_VISIBILITY, P_FONT_VARIANT, P_TRANSFORM, P_PLACE_ITEMS, P_PLACE_CONTENT, P_MASK, P_CLEAR, P_CONTENT, P_GRID_TEMPLATE_AREAS, P_GRID_COLUMN, P_GRID_COLUMN_START, P_GRID_COLUMN_END,
    P_GRID_AREA, P_GRID_ROW, P_GRID_ROW_START, P_GRID_TEMPLATE, P_BACKGROUND_SIZE, P_BACKGROUND_REPEAT,
    P_BACKGROUND_POSITION,
    P_COUNT
} PropId;

static const struct { const char *name; uint8_t id; } prop_names[] = {
    {"display", P_DISPLAY}, {"position", P_POSITION}, {"float", P_FLOAT}, {"visibility", P_VISIBILITY},
    {"color", P_COLOR}, {"background", P_BACKGROUND}, {"background-color", P_BACKGROUND_COLOR},
    {"background-image", P_BACKGROUND_IMAGE}, {"font", P_FONT}, {"font-size", P_FONT_SIZE},
    {"font-weight", P_FONT_WEIGHT}, {"font-style", P_FONT_STYLE}, {"font-family", P_FONT_FAMILY},
    {"line-height", P_LINE_HEIGHT}, {"text-align", P_TEXT_ALIGN}, {"text-decoration", P_TEXT_DECORATION},
    {"text-decoration-line", P_TEXT_DECORATION_LINE}, {"text-transform", P_TEXT_TRANSFORM},
    {"white-space", P_WHITE_SPACE}, {"text-wrap", P_TEXT_WRAP}, {"text-wrap-mode", P_TEXT_WRAP},
    {"text-indent", P_TEXT_INDENT},
    {"margin", P_MARGIN}, {"margin-top", P_MARGIN_T}, {"margin-right", P_MARGIN_R}, {"margin-bottom", P_MARGIN_B},
    {"margin-left", P_MARGIN_L}, {"margin-inline", P_MARGIN_INLINE}, {"margin-block", P_MARGIN_BLOCK},
    {"margin-inline-start", P_MARGIN_INLINE_START}, {"margin-inline-end", P_MARGIN_INLINE_END},
    {"margin-block-start", P_MARGIN_BLOCK_START}, {"margin-block-end", P_MARGIN_BLOCK_END},
    {"padding", P_PADDING}, {"padding-top", P_PADDING_T}, {"padding-right", P_PADDING_R},
    {"padding-bottom", P_PADDING_B}, {"padding-left", P_PADDING_L}, {"padding-inline", P_PADDING_INLINE},
    {"padding-block", P_PADDING_BLOCK}, {"padding-inline-start", P_PADDING_INLINE_START},
    {"padding-inline-end", P_PADDING_INLINE_END}, {"padding-block-start", P_PADDING_BLOCK_START},
    {"padding-block-end", P_PADDING_BLOCK_END},
    {"border", P_BORDER}, {"border-top", P_BORDER_T}, {"border-right", P_BORDER_R}, {"border-bottom", P_BORDER_B},
    {"border-left", P_BORDER_L}, {"border-width", P_BORDER_WIDTH}, {"border-color", P_BORDER_COLOR},
    {"border-style", P_BORDER_STYLE}, {"border-top-width", P_BORDER_T_WIDTH},
    {"border-right-width", P_BORDER_R_WIDTH}, {"border-bottom-width", P_BORDER_B_WIDTH},
    {"border-left-width", P_BORDER_L_WIDTH}, {"border-top-color", P_BORDER_T_COLOR},
    {"border-right-color", P_BORDER_R_COLOR}, {"border-bottom-color", P_BORDER_B_COLOR},
    {"border-left-color", P_BORDER_L_COLOR}, {"border-top-style", P_BORDER_T_STYLE},
    {"border-right-style", P_BORDER_R_STYLE}, {"border-bottom-style", P_BORDER_B_STYLE},
    {"border-left-style", P_BORDER_L_STYLE}, {"border-radius", P_BORDER_RADIUS},
    {"border-block-end", P_BORDER_B}, {"border-block-start", P_BORDER_T},
    {"border-inline-start", P_BORDER_L}, {"border-inline-end", P_BORDER_R},
    {"width", P_WIDTH}, {"height", P_HEIGHT}, {"min-width", P_MIN_WIDTH}, {"max-width", P_MAX_WIDTH},
    {"min-height", P_MIN_HEIGHT}, {"max-height", P_MAX_HEIGHT}, {"box-sizing", P_BOX_SIZING},
    {"inline-size", P_WIDTH}, {"max-inline-size", P_MAX_WIDTH}, {"min-inline-size", P_MIN_WIDTH},
    {"block-size", P_HEIGHT},
    {"list-style", P_LIST_STYLE}, {"list-style-type", P_LIST_STYLE_TYPE}, {"flex", P_FLEX},
    {"flex-direction", P_FLEX_DIRECTION}, {"flex-wrap", P_FLEX_WRAP}, {"flex-flow", P_FLEX_FLOW},
    {"flex-grow", P_FLEX_GROW}, {"flex-shrink", P_FLEX_SHRINK}, {"flex-basis", P_FLEX_BASIS},
    {"justify-content", P_JUSTIFY_CONTENT}, {"align-items", P_ALIGN_ITEMS}, {"align-self", P_ALIGN_SELF},
    {"gap", P_GAP}, {"grid-gap", P_GAP}, {"row-gap", P_ROW_GAP}, {"column-gap", P_COLUMN_GAP},
    {"grid-row-gap", P_ROW_GAP}, {"grid-column-gap", P_COLUMN_GAP}, {"order", P_ORDER},
    {"overflow", P_OVERFLOW}, {"overflow-x", P_OVERFLOW_X}, {"overflow-y", P_OVERFLOW_Y}, {"opacity", P_OPACITY},
    {"clip", P_CLIP}, {"clip-path", P_CLIP_PATH}, {"vertical-align", P_VERTICAL_ALIGN}, {"top", P_TOP},
    {"right", P_RIGHT}, {"bottom", P_BOTTOM}, {"left", P_LEFT}, {"inset", P_INSET},
    {"table-layout", P_TABLE_LAYOUT}, {"border-spacing", P_BORDER_SPACING},
    {"grid-template-columns", P_GRID_TEMPLATE_COLUMNS}, {"content-visibility", P_CONTENT_VISIBILITY},
    {"font-variant", P_FONT_VARIANT}, {"transform", P_TRANSFORM}, {"place-items", P_PLACE_ITEMS},
    {"place-content", P_PLACE_CONTENT}, {"mask", P_MASK}, {"mask-image", P_MASK}, {"-webkit-mask", P_MASK},
    {"-webkit-mask-image", P_MASK}, {"clear", P_CLEAR}, {"content", P_CONTENT},
    {"grid-template-areas", P_GRID_TEMPLATE_AREAS}, {"grid-column", P_GRID_COLUMN}, {"grid-column-start", P_GRID_COLUMN_START},
    {"grid-column-end", P_GRID_COLUMN_END}, {"grid-area", P_GRID_AREA}, {"grid-row", P_GRID_ROW},
    {"grid-row-start", P_GRID_ROW_START}, {"grid-template", P_GRID_TEMPLATE},
    {"background-size", P_BACKGROUND_SIZE}, {"background-repeat", P_BACKGROUND_REPEAT},
    {"background-position", P_BACKGROUND_POSITION},
};

static uint8_t prop_id(const char *s, size_t n)
{
    if (n >= 2 && s[0] == '-' && s[1] == '-') return P_CUSTOM;
    for (unsigned i = 0; i < ARRAY_SIZE(prop_names); i++)
        if (ieq(s, n, prop_names[i].name)) return prop_names[i].id;
    return P_UNKNOWN;
}

typedef struct {
    uint8_t prop;
    uint8_t important;
    const char *name;           /* custom properties */
    const char *value;          /* trimmed, without !important */
    const char *base;           /* directory URL of the stylesheet (relative url()) */
} CssDecl;

enum { ATTR_EXISTS, ATTR_EQ, ATTR_PREFIX, ATTR_SUFFIX, ATTR_SUBSTR, ATTR_WORD, ATTR_DASH };

typedef struct {
    const char *name, *value;
    uint8_t op;
    bool icase;
} CssAttrSel;

struct CssSelector;

#define PS_FIRST_CHILD  0x0001
#define PS_LAST_CHILD   0x0002
#define PS_ONLY_CHILD   0x0004
#define PS_ROOT         0x0008
#define PS_LINK         0x0010
#define PS_NTH_CHILD    0x0020
#define PS_NTH_LAST     0x0040
#define PS_EMPTY        0x0080
#define PS_NEVER        0x0100
#define PS_FIRST_TYPE   0x0200
#define PS_LAST_TYPE    0x0400
#define PS_CHECKED      0x0800
#define PS_DISABLED     0x1000

enum { COMB_NONE = 0, COMB_DESC, COMB_CHILD, COMB_ADJ, COMB_SIB };

typedef struct {
    const char *tag;            /* NULL = any */
    uint8_t tag_id;
    const char *id;
    const char *classes[6];
    uint8_t nclasses;
    CssAttrSel attrs[4];
    uint8_t nattrs;
    uint16_t pseudo;
    int16_t nth_a, nth_b;
    struct CssSelector *not_list[4];    /* :not(...) */
    uint8_t nnot;
    struct CssSelector *is_list[4];     /* :is()/:where()/:has() alternatives (any) */
    uint8_t nis;
    uint8_t comb;               /* combinator to the compound on the LEFT */
} CssCompound;

typedef struct CssSelector {
    CssCompound *c;             /* n compounds, left to right */
    uint8_t n;
    uint32_t spec;
    struct CssSelector *alt;    /* next alternative inside :is()/:not() */
    uint8_t pseudo_elem;        /* 0, 1 = ::before, 2 = ::after */
} CssSelector;

typedef struct {
    CssSelector *sel;
    CssDecl *decls;
    uint16_t ndecls;
    uint8_t origin;             /* 0 = UA, 1 = author */
    uint32_t order;
} CssRule;

typedef struct HashEntry {
    const char *key;
    int *rules;
    int count, cap;
    struct HashEntry *next;
} HashEntry;

struct CssSheetSet {
    WebArena *a;
    CssMedia media;
    CssRule *rules;
    int rule_count;
    uint32_t order;
    uint8_t origin;
    const char *cur_base;       /* while parsing a sheet */
    char imports[MAX_IMPORTS][256];
    int import_count;
    /* index by rightmost compound */
    HashEntry *by_id[HASH_BUCKETS], *by_class[HASH_BUCKETS], *by_tag[HASH_BUCKETS];
    int *universal;
    int universal_count, universal_cap;
    bool indexed;
};

/* ------------------------------------------------------------------ */
/* Colours                                                             */
/* ------------------------------------------------------------------ */

static const struct { const char *n; uint32_t c; } named_colors[] = {
    {"aliceblue", 0xF0F8FF}, {"antiquewhite", 0xFAEBD7}, {"aqua", 0x00FFFF}, {"aquamarine", 0x7FFFD4},
    {"azure", 0xF0FFFF}, {"beige", 0xF5F5DC}, {"bisque", 0xFFE4C4}, {"black", 0x000000},
    {"blanchedalmond", 0xFFEBCD}, {"blue", 0x0000FF}, {"blueviolet", 0x8A2BE2}, {"brown", 0xA52A2A},
    {"burlywood", 0xDEB887}, {"cadetblue", 0x5F9EA0}, {"chartreuse", 0x7FFF00}, {"chocolate", 0xD2691E},
    {"coral", 0xFF7F50}, {"cornflowerblue", 0x6495ED}, {"cornsilk", 0xFFF8DC}, {"crimson", 0xDC143C},
    {"cyan", 0x00FFFF}, {"darkblue", 0x00008B}, {"darkcyan", 0x008B8B}, {"darkgoldenrod", 0xB8860B},
    {"darkgray", 0xA9A9A9}, {"darkgreen", 0x006400}, {"darkgrey", 0xA9A9A9}, {"darkkhaki", 0xBDB76B},
    {"darkmagenta", 0x8B008B}, {"darkolivegreen", 0x556B2F}, {"darkorange", 0xFF8C00}, {"darkorchid", 0x9932CC},
    {"darkred", 0x8B0000}, {"darksalmon", 0xE9967A}, {"darkseagreen", 0x8FBC8F}, {"darkslateblue", 0x483D8B},
    {"darkslategray", 0x2F4F4F}, {"darkslategrey", 0x2F4F4F}, {"darkturquoise", 0x00CED1},
    {"darkviolet", 0x9400D3}, {"deeppink", 0xFF1493}, {"deepskyblue", 0x00BFFF}, {"dimgray", 0x696969},
    {"dimgrey", 0x696969}, {"dodgerblue", 0x1E90FF}, {"firebrick", 0xB22222}, {"floralwhite", 0xFFFAF0},
    {"forestgreen", 0x228B22}, {"fuchsia", 0xFF00FF}, {"gainsboro", 0xDCDCDC}, {"ghostwhite", 0xF8F8FF},
    {"gold", 0xFFD700}, {"goldenrod", 0xDAA520}, {"gray", 0x808080}, {"green", 0x008000},
    {"greenyellow", 0xADFF2F}, {"grey", 0x808080}, {"honeydew", 0xF0FFF0}, {"hotpink", 0xFF69B4},
    {"indianred", 0xCD5C5C}, {"indigo", 0x4B0082}, {"ivory", 0xFFFFF0}, {"khaki", 0xF0E68C},
    {"lavender", 0xE6E6FA}, {"lavenderblush", 0xFFF0F5}, {"lawngreen", 0x7CFC00}, {"lemonchiffon", 0xFFFACD},
    {"lightblue", 0xADD8E6}, {"lightcoral", 0xF08080}, {"lightcyan", 0xE0FFFF},
    {"lightgoldenrodyellow", 0xFAFAD2}, {"lightgray", 0xD3D3D3}, {"lightgreen", 0x90EE90},
    {"lightgrey", 0xD3D3D3}, {"lightpink", 0xFFB6C1}, {"lightsalmon", 0xFFA07A}, {"lightseagreen", 0x20B2AA},
    {"lightskyblue", 0x87CEFA}, {"lightslategray", 0x778899}, {"lightslategrey", 0x778899},
    {"lightsteelblue", 0xB0C4DE}, {"lightyellow", 0xFFFFE0}, {"lime", 0x00FF00}, {"limegreen", 0x32CD32},
    {"linen", 0xFAF0E6}, {"magenta", 0xFF00FF}, {"maroon", 0x800000}, {"mediumaquamarine", 0x66CDAA},
    {"mediumblue", 0x0000CD}, {"mediumorchid", 0xBA55D3}, {"mediumpurple", 0x9370DB},
    {"mediumseagreen", 0x3CB371}, {"mediumslateblue", 0x7B68EE}, {"mediumspringgreen", 0x00FA9A},
    {"mediumturquoise", 0x48D1CC}, {"mediumvioletred", 0xC71585}, {"midnightblue", 0x191970},
    {"mintcream", 0xF5FFFA}, {"mistyrose", 0xFFE4E1}, {"moccasin", 0xFFE4B5}, {"navajowhite", 0xFFDEAD},
    {"navy", 0x000080}, {"oldlace", 0xFDF5E6}, {"olive", 0x808000}, {"olivedrab", 0x6B8E23},
    {"orange", 0xFFA500}, {"orangered", 0xFF4500}, {"orchid", 0xDA70D6}, {"palegoldenrod", 0xEEE8AA},
    {"palegreen", 0x98FB98}, {"paleturquoise", 0xAFEEEE}, {"palevioletred", 0xDB7093}, {"papayawhip", 0xFFEFD5},
    {"peachpuff", 0xFFDAB9}, {"peru", 0xCD853F}, {"pink", 0xFFC0CB}, {"plum", 0xDDA0DD},
    {"powderblue", 0xB0E0E6}, {"purple", 0x800080}, {"rebeccapurple", 0x663399}, {"red", 0xFF0000},
    {"rosybrown", 0xBC8F8F}, {"royalblue", 0x4169E1}, {"saddlebrown", 0x8B4513}, {"salmon", 0xFA8072},
    {"sandybrown", 0xF4A460}, {"seagreen", 0x2E8B57}, {"seashell", 0xFFF5EE}, {"sienna", 0xA0522D},
    {"silver", 0xC0C0C0}, {"skyblue", 0x87CEEB}, {"slateblue", 0x6A5ACD}, {"slategray", 0x708090},
    {"slategrey", 0x708090}, {"snow", 0xFFFAFA}, {"springgreen", 0x00FF7F}, {"steelblue", 0x4682B4},
    {"tan", 0xD2B48C}, {"teal", 0x008080}, {"thistle", 0xD8BFD8}, {"tomato", 0xFF6347},
    {"turquoise", 0x40E0D0}, {"violet", 0xEE82EE}, {"wheat", 0xF5DEB3}, {"white", 0xFFFFFF},
    {"whitesmoke", 0xF5F5F5}, {"yellow", 0xFFFF00}, {"yellowgreen", 0x9ACD32},
    /* system colours used by real sites */
    {"canvas", 0xFFFFFF}, {"canvastext", 0x000000}, {"linktext", 0x0000EE}, {"buttonface", 0xEFEFEF},
    {"buttontext", 0x000000}, {"field", 0xFFFFFF}, {"fieldtext", 0x000000}, {"graytext", 0x6D6D6D},
    {"highlight", 0xB4D5FE}, {"highlighttext", 0x000000}, {"visitedtext", 0x551A8B}, {"mark", 0xFFFF00},
};

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = lc(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int clamp255(int64_t v) { return v < 0 ? 0 : v > 255 ? 255 : (int)v; }

/* Parse up to 4 numeric arguments of rgb()/hsl(): commas, spaces or '/'.
 * vals in hundredths; pct[i] set when the value had '%'. */
static int color_args(const char *p, const char *end, int32_t *vals, bool *pct, int max)
{
    int n = 0;
    while (p < end && n < max) {
        while (p < end && (ws(*p) || *p == ',' || *p == '/')) p++;
        if (p >= end || *p == ')') break;
        int32_t v;
        if (istarts(p, "none")) { vals[n] = 0; pct[n] = false; n++; p += 4; continue; }
        if (!parse_num(&p, end, &v)) return -1;
        pct[n] = p < end && *p == '%';
        if (pct[n]) p++;
        while (p < end && ((*p >= 'a' && *p <= 'z'))) p++;      /* deg, turn, ... */
        vals[n++] = v;
    }
    return n;
}

static uint32_t hsl_to_rgb(int32_t h100, int32_t s100, int32_t l100)
{
    /* h in hundredths of degrees, s/l in hundredths of percent */
    int64_t h = ((h100 % 36000) + 36000) % 36000;
    int64_t s = s100 < 0 ? 0 : s100 > 10000 ? 10000 : s100;
    int64_t l = l100 < 0 ? 0 : l100 > 10000 ? 10000 : l100;
    int64_t c = (10000 - (2 * l - 10000 < 0 ? -(2 * l - 10000) : 2 * l - 10000)) * s / 10000;   /* 0..10000 */
    int64_t hp = h / 6000;                                  /* sector 0..5 */
    int64_t rem = (h % 12000);                              /* (h/60) mod 2, scaled */
    int64_t xx = c * (6000 - (rem - 6000 < 0 ? -(rem - 6000) : rem - 6000)) / 6000;
    int64_t r = 0, g = 0, b = 0;
    switch (hp) {
        case 0: r = c; g = xx; break;
        case 1: r = xx; g = c; break;
        case 2: g = c; b = xx; break;
        case 3: g = xx; b = c; break;
        case 4: r = xx; b = c; break;
        default: r = c; b = xx; break;
    }
    int64_t m = l - c / 2;
    return ((uint32_t)clamp255((r + m) * 255 / 10000) << 16) | ((uint32_t)clamp255((g + m) * 255 / 10000) << 8) |
           (uint32_t)clamp255((b + m) * 255 / 10000);
}

static bool parse_color_n(const char *s, size_t n, uint32_t *out)
{
    const char *p = s, *end = s + n;
    p = skip_ws(p, end);
    while (end > p && ws(end[-1])) end--;
    size_t len = (size_t)(end - p);
    if (!len) return false;
    if (*p == '#') {
        int d[8];
        size_t k = len - 1;
        if (k != 3 && k != 4 && k != 6 && k != 8) return false;
        for (size_t i = 0; i < k; i++) { d[i] = hexv(p[1 + i]); if (d[i] < 0) return false; }
        uint32_t r, g, b, a = 255;
        if (k <= 4) { r = (uint32_t)d[0] * 17; g = (uint32_t)d[1] * 17; b = (uint32_t)d[2] * 17; if (k == 4) a = (uint32_t)d[3] * 17; }
        else { r = (uint32_t)(d[0] * 16 + d[1]); g = (uint32_t)(d[2] * 16 + d[3]); b = (uint32_t)(d[4] * 16 + d[5]); if (k == 8) a = (uint32_t)(d[6] * 16 + d[7]); }
        *out = (a << 24) | (r << 16) | (g << 8) | b;
        return true;
    }
    if (istarts(p, "rgb")) {
        const char *q = p + 3;
        if (q < end && lc(*q) == 'a') q++;
        if (q >= end || *q != '(') return false;
        int32_t v[4]; bool pc[4];
        int k = color_args(q + 1, end, v, pc, 4);
        if (k < 3) return false;
        int rgb[3];
        for (int i = 0; i < 3; i++) rgb[i] = pc[i] ? clamp255(((int64_t)v[i] * 255 + 5000) / 10000) : clamp255((v[i] + 50) / 100);
        int a = 255;
        if (k == 4) a = pc[3] ? clamp255(((int64_t)v[3] * 255 + 5000) / 10000) : clamp255(((int64_t)v[3] * 255 + 50) / 100);
        *out = ((uint32_t)a << 24) | ((uint32_t)rgb[0] << 16) | ((uint32_t)rgb[1] << 8) | (uint32_t)rgb[2];
        return true;
    }
    if (istarts(p, "hsl")) {
        const char *q = p + 3;
        if (q < end && lc(*q) == 'a') q++;
        if (q >= end || *q != '(') return false;
        int32_t v[4]; bool pc[4];
        int k = color_args(q + 1, end, v, pc, 4);
        if (k < 3) return false;
        uint32_t rgb = hsl_to_rgb(v[0], v[1], v[2]);
        int a = 255;
        if (k == 4) a = pc[3] ? clamp255(((int64_t)v[3] * 255 + 5000) / 10000) : clamp255(((int64_t)v[3] * 255 + 50) / 100);
        *out = ((uint32_t)a << 24) | rgb;
        return true;
    }
    if (ieq(p, len, "transparent")) { *out = 0; return true; }
    for (unsigned i = 0; i < ARRAY_SIZE(named_colors); i++)
        if (ieq(p, len, named_colors[i].n)) { *out = 0xFF000000u | named_colors[i].c; return true; }
    return false;
}

bool css_parse_color(const char *s, uint32_t *argb) { return parse_color_n(s, strlen(s), argb); }

/* ------------------------------------------------------------------ */
/* Selectors                                                           */
/* ------------------------------------------------------------------ */

static const char *dup_ident(WebArena *a, const char *s, size_t n, bool lower_it)
{
    char *d = (char *)arena_alloc(a, n + 1);
    if (!d) return NULL;
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < n) { c = s[++i]; if (hexv(c) >= 0) { /* \31 style escapes */ int v = 0, m = 0; while (i < n && hexv(s[i]) >= 0 && m < 6) { v = v * 16 + hexv(s[i]); i++; m++; } if (i < n && s[i] == ' ') i++; i--; c = v < 128 ? (char)v : '?'; } }
        d[k++] = lower_it ? lc(c) : c;
    }
    d[k] = '\0';
    return d;
}

static const char *read_ident(const char *p, const char *end)
{
    while (p < end && ident_char(*p)) { if (*p == '\\' && p + 1 < end) p++; p++; }
    return p;
}

static CssSelector *parse_selector(CssSheetSet *S, const char *p, const char *end, int depth);

/* Parse a comma-separated selector list inside :not()/:is() into a chain. */
static CssSelector *parse_selector_list(CssSheetSet *S, const char *p, const char *end, int depth, uint32_t *max_spec)
{
    CssSelector *head = NULL, **tail = &head;
    *max_spec = 0;
    while (p < end) {
        const char *s = p;
        int d = 0;
        while (p < end && !(d == 0 && *p == ',')) { if (*p == '(' || *p == '[') d++; else if (*p == ')' || *p == ']') d--; p++; }
        CssSelector *sel = parse_selector(S, s, p, depth + 1);
        if (sel) { *tail = sel; tail = &sel->alt; if (sel->spec > *max_spec) *max_spec = sel->spec; }
        if (p < end) p++;
    }
    return head;
}

static void parse_nth(const char *p, const char *end, int16_t *a, int16_t *b)
{
    p = skip_ws(p, end);
    while (end > p && ws(end[-1])) end--;
    size_t n = (size_t)(end - p);
    if (ieq(p, n, "odd")) { *a = 2; *b = 1; return; }
    if (ieq(p, n, "even")) { *a = 2; *b = 0; return; }
    int sign = 1, av = 0, bv = 0;
    bool has_n = false;
    const char *q = p;
    if (q < end && (*q == '-' || *q == '+')) { if (*q == '-') sign = -1; q++; }
    int digits = 0;
    while (q < end && *q >= '0' && *q <= '9') { av = av * 10 + (*q - '0'); q++; digits++; if (av > 10000) av = 10000; }
    if (q < end && lc(*q) == 'n') {
        has_n = true;
        if (!digits) av = 1;
        av *= sign;
        q++;
        q = skip_ws(q, end);
        int bs = 1;
        if (q < end && (*q == '+' || *q == '-')) { bs = *q == '-' ? -1 : 1; q++; }
        q = skip_ws(q, end);
        while (q < end && *q >= '0' && *q <= '9') { bv = bv * 10 + (*q - '0'); q++; if (bv > 10000) bv = 10000; }
        bv *= bs;
    } else {
        bv = av * sign;
        av = 0;
    }
    (void)has_n;
    *a = (int16_t)av;
    *b = (int16_t)bv;
}

static CssSelector *parse_selector_into(CssSheetSet *S, const char *p, const char *end, int depth, CssSelector *sel);

/* Parse into a per-depth scratch selector, then copy only the compounds used. */
static CssSelector *parse_selector(CssSheetSet *S, const char *p, const char *end, int depth)
{
    static CssCompound scratch_c[6][MAX_COMPOUNDS];
    static CssSelector scratch[6];
    if (depth > 4) return NULL;
    CssSelector *t = &scratch[depth];
    memset(t, 0, sizeof(*t));
    memset(scratch_c[depth], 0, sizeof(scratch_c[depth]));
    t->c = scratch_c[depth];
    if (!parse_selector_into(S, p, end, depth, t)) return NULL;
    CssSelector *sel = (CssSelector *)arena_alloc(S->a, sizeof(CssSelector));
    CssCompound *c = (CssCompound *)arena_alloc(S->a, sizeof(CssCompound) * t->n);
    if (!sel || !c) return NULL;
    memcpy(c, t->c, sizeof(CssCompound) * t->n);
    *sel = *t;
    sel->c = c;
    return sel;
}

static CssSelector *parse_selector_into(CssSheetSet *S, const char *p, const char *end, int depth, CssSelector *sel)
{
    uint32_t ids = 0, cls = 0, tags = 0;
    p = skip_ws(p, end);
    while (end > p && ws(end[-1])) end--;
    if (p >= end) return NULL;
    uint8_t pending_comb = COMB_NONE;
    while (p < end) {
        if (sel->n >= MAX_COMPOUNDS) return NULL;
        CssCompound *c = &sel->c[sel->n];
        c->comb = sel->n ? (pending_comb ? pending_comb : COMB_DESC) : COMB_NONE;
        bool any = false;
        while (p < end && !ws(*p) && *p != '>' && *p != '+' && *p != '~') {
            if (*p == '*') { p++; any = true; continue; }
            if (*p == '&') return NULL;                     /* nesting selector: unsupported */
            if (ident_char(*p) && *p != '-' ? true : (*p == '-' && p + 1 < end && ident_char(p[1]))) {
                const char *e = read_ident(p, end);
                if (*p >= '0' && *p <= '9') return NULL;
                c->tag = dup_ident(S->a, p, (size_t)(e - p), true);
                c->tag_id = (uint8_t)dom_tag_id(p, (size_t)(e - p));
                tags++;
                p = e;
                any = true;
                continue;
            }
            if (*p == '#') {
                const char *e = read_ident(p + 1, end);
                if (e == p + 1) return NULL;
                c->id = dup_ident(S->a, p + 1, (size_t)(e - p - 1), false);
                ids++;
                p = e;
                any = true;
                continue;
            }
            if (*p == '.') {
                const char *e = read_ident(p + 1, end);
                if (e == p + 1) return NULL;
                if (c->nclasses < 6) c->classes[c->nclasses++] = dup_ident(S->a, p + 1, (size_t)(e - p - 1), false);
                else c->pseudo |= PS_NEVER;
                cls++;
                p = e;
                any = true;
                continue;
            }
            if (*p == '[') {
                const char *close = skip_group(p, end);
                const char *q = skip_ws(p + 1, close);
                const char *ne = read_ident(q, close);
                if (ne == q) return NULL;
                if (c->nattrs < 4) {
                    CssAttrSel *as = &c->attrs[c->nattrs++];
                    as->name = dup_ident(S->a, q, (size_t)(ne - q), true);
                    q = skip_ws(ne, close);
                    as->op = ATTR_EXISTS;
                    if (q < close && *q != ']') {
                        char op = *q;
                        if (op == '=') { as->op = ATTR_EQ; q++; }
                        else if (q + 1 < close && q[1] == '=') {
                            as->op = op == '^' ? ATTR_PREFIX : op == '$' ? ATTR_SUFFIX : op == '*' ? ATTR_SUBSTR :
                                     op == '~' ? ATTR_WORD : op == '|' ? ATTR_DASH : ATTR_EQ;
                            q += 2;
                        } else return NULL;
                        q = skip_ws(q, close);
                        const char *vs, *ve;
                        if (q < close && (*q == '"' || *q == '\'')) {
                            char qc = *q++;
                            vs = q;
                            while (q < close && *q != qc) q++;
                            ve = q;
                            if (q < close) q++;
                        } else {
                            vs = q;
                            while (q < close && !ws(*q) && *q != ']') q++;
                            ve = q;
                        }
                        as->value = dup_ident(S->a, vs, (size_t)(ve - vs), false);
                        q = skip_ws(q, close);
                        if (q < close && (lc(*q) == 'i')) as->icase = true;
                    }
                } else {
                    c->pseudo |= PS_NEVER;
                }
                cls++;
                p = close;
                any = true;
                continue;
            }
            if (*p == ':') {
                bool element = p + 1 < end && p[1] == ':';
                p += element ? 2 : 1;
                const char *ne = read_ident(p, end);
                size_t nl = (size_t)(ne - p);
                const char *name = p;
                p = ne;
                const char *args = NULL, *args_end = NULL;
                if (p < end && *p == '(') {
                    const char *close = skip_group(p, end);
                    args = p + 1;
                    args_end = close - 1;
                    p = close;
                }
                any = true;
                if ((ieq(name, nl, "before") || ieq(name, nl, "after")) && depth == 0) {
                    /* must be the last thing in the selector */
                    const char *rest = skip_ws(p, end);
                    if (rest < end) return NULL;
                    sel->pseudo_elem = ieq(name, nl, "before") ? 1 : 2;
                    tags++;
                    continue;
                }
                if (element || ieq(name, nl, "before") || ieq(name, nl, "after") || ieq(name, nl, "first-line") ||
                    ieq(name, nl, "first-letter") || ieq(name, nl, "selection") || ieq(name, nl, "placeholder") ||
                    ieq(name, nl, "marker") || ieq(name, nl, "backdrop")) return NULL;     /* pseudo-elements */
                cls++;
                if (ieq(name, nl, "first-child")) c->pseudo |= PS_FIRST_CHILD;
                else if (ieq(name, nl, "last-child")) c->pseudo |= PS_LAST_CHILD;
                else if (ieq(name, nl, "only-child")) c->pseudo |= PS_ONLY_CHILD;
                else if (ieq(name, nl, "first-of-type")) c->pseudo |= PS_FIRST_TYPE;
                else if (ieq(name, nl, "last-of-type")) c->pseudo |= PS_LAST_TYPE;
                else if (ieq(name, nl, "root")) c->pseudo |= PS_ROOT;
                else if (ieq(name, nl, "link") || ieq(name, nl, "any-link")) c->pseudo |= PS_LINK;
                else if (ieq(name, nl, "empty")) c->pseudo |= PS_EMPTY;
                else if (ieq(name, nl, "checked")) c->pseudo |= PS_CHECKED;
                else if (ieq(name, nl, "disabled")) c->pseudo |= PS_DISABLED;
                else if (ieq(name, nl, "enabled") || ieq(name, nl, "defined") || ieq(name, nl, "scope") ||
                         ieq(name, nl, "lang") || ieq(name, nl, "dir")) { /* always true */ }
                else if (ieq(name, nl, "nth-child") && args) { c->pseudo |= PS_NTH_CHILD; parse_nth(args, args_end, &c->nth_a, &c->nth_b); }
                else if (ieq(name, nl, "nth-last-child") && args) { c->pseudo |= PS_NTH_LAST; parse_nth(args, args_end, &c->nth_a, &c->nth_b); }
                else if (ieq(name, nl, "nth-of-type") && args) { c->pseudo |= PS_NTH_CHILD; parse_nth(args, args_end, &c->nth_a, &c->nth_b); }
                else if (ieq(name, nl, "not") && args) {
                    uint32_t ms;
                    CssSelector *l = parse_selector_list(S, args, args_end, depth, &ms);
                    if (!l) return NULL;
                    if (c->nnot < 4) c->not_list[c->nnot++] = l;
                    cls--; ids += ms >> 20; cls += (ms >> 10) & 0x3FF; tags += ms & 0x3FF;
                } else if ((ieq(name, nl, "is") || ieq(name, nl, "matches") || ieq(name, nl, "where") ||
                            ieq(name, nl, "-webkit-any")) && args) {
                    uint32_t ms;
                    CssSelector *l = parse_selector_list(S, args, args_end, depth, &ms);
                    if (!l) c->pseudo |= PS_NEVER;
                    else if (c->nis < 4) c->is_list[c->nis++] = l;
                    cls--;
                    if (!ieq(name, nl, "where")) { ids += ms >> 20; cls += (ms >> 10) & 0x3FF; tags += ms & 0x3FF; }
                } else {
                    c->pseudo |= PS_NEVER;          /* :hover, :focus, :has, unknown */
                }
                continue;
            }
            return NULL;            /* unexpected character */
        }
        if (!any) return NULL;
        sel->n++;
        p = skip_ws(p, end);
        pending_comb = COMB_NONE;
        if (p < end && (*p == '>' || *p == '+' || *p == '~')) {
            pending_comb = *p == '>' ? COMB_CHILD : *p == '+' ? COMB_ADJ : COMB_SIB;
            p = skip_ws(p + 1, end);
            if (p >= end) return NULL;
        }
    }
    if (!sel->n) return NULL;
    if (ids > 1023) ids = 1023;
    if (cls > 1023) cls = 1023;
    if (tags > 1023) tags = 1023;
    sel->spec = (ids << 20) | (cls << 10) | tags;
    return sel;
}

static bool attr_match(const CssAttrSel *as, const DomNode *n)
{
    const char *v = dom_attr(n, as->name);
    if (!v) return false;
    if (as->op == ATTR_EXISTS) return true;
    const char *w = as->value ? as->value : "";
    size_t vl = strlen(v), wl = strlen(w);
    switch (as->op) {
        case ATTR_EQ:
            if (vl != wl) return false;
            for (size_t i = 0; i < vl; i++) if ((as->icase ? lc(v[i]) != lc(w[i]) : v[i] != w[i])) return false;
            return true;
        case ATTR_PREFIX: return wl && vl >= wl && strncmp(v, w, wl) == 0;
        case ATTR_SUFFIX: return wl && vl >= wl && strcmp(v + vl - wl, w) == 0;
        case ATTR_SUBSTR:
            if (!wl) return false;
            for (size_t i = 0; i + wl <= vl; i++) if (strncmp(v + i, w, wl) == 0) return true;
            return false;
        case ATTR_WORD:
            for (const char *s = v; *s;) {
                while (*s && ws(*s)) s++;
                const char *e = s;
                while (*e && !ws(*e)) e++;
                if ((size_t)(e - s) == wl && wl && strncmp(s, w, wl) == 0) return true;
                s = e;
            }
            return false;
        case ATTR_DASH: return strcmp(v, w) == 0 || (vl > wl && strncmp(v, w, wl) == 0 && v[wl] == '-');
        default: return false;
    }
}

static bool match_selector(const CssSelector *sel, int idx, const DomNode *n);

static bool any_matches(const CssSelector *list, const DomNode *n)
{
    for (const CssSelector *s = list; s; s = s->alt) if (match_selector(s, s->n - 1, n)) return true;
    return false;
}

static int element_count_after(const DomNode *n)
{
    int k = 0;
    for (const DomNode *s = n->next; s; s = s->next) if (s->type == DOM_ELEMENT) k++;
    return k;
}

static bool nth_ok(int a, int b, int pos)
{
    if (a == 0) return pos == b;
    int d = pos - b;
    if (d % a != 0) return false;
    return d / a >= 0;
}

static bool compound_match(const CssCompound *c, const DomNode *n)
{
    if (n->type != DOM_ELEMENT) return false;
    if (c->pseudo & PS_NEVER) return false;
    if (c->tag) {
        if (c->tag_id) { if (n->tag != c->tag_id) return false; }
        else if (n->tag != TAG_OTHER || strcmp(n->name, c->tag) != 0) return false;
    }
    if (c->id && (!n->id || strcmp(n->id, c->id) != 0)) return false;
    for (int i = 0; i < c->nclasses; i++) if (!dom_has_class(n, c->classes[i])) return false;
    for (int i = 0; i < c->nattrs; i++) if (!attr_match(&c->attrs[i], n)) return false;
    if (c->pseudo) {
        uint16_t ps = c->pseudo;
        if ((ps & PS_ROOT) && n->tag != TAG_HTML) return false;
        if ((ps & PS_LINK) && !((n->tag == TAG_A || n->tag == TAG_AREA) && dom_attr(n, "href"))) return false;
        if ((ps & (PS_FIRST_CHILD | PS_ONLY_CHILD | PS_FIRST_TYPE)) && n->child_index != 1) {
            if (!(ps & PS_FIRST_TYPE)) return false;
            for (const DomNode *s = n->prev; s; s = s->prev) if (s->type == DOM_ELEMENT && s->tag == n->tag && (n->tag != TAG_OTHER || strcmp(s->name, n->name) == 0)) return false;
        }
        if ((ps & (PS_LAST_CHILD | PS_ONLY_CHILD)) && element_count_after(n)) return false;
        if (ps & PS_LAST_TYPE)
            for (const DomNode *s = n->next; s; s = s->next) if (s->type == DOM_ELEMENT && s->tag == n->tag && (n->tag != TAG_OTHER || strcmp(s->name, n->name) == 0)) return false;
        if ((ps & PS_NTH_CHILD) && !nth_ok(c->nth_a, c->nth_b, n->child_index)) return false;
        if ((ps & PS_NTH_LAST) && !nth_ok(c->nth_a, c->nth_b, element_count_after(n) + 1)) return false;
        if (ps & PS_EMPTY) {
            for (const DomNode *s = n->first; s; s = s->next) if (s->type == DOM_ELEMENT || s->text_len) return false;
        }
        if ((ps & PS_CHECKED) && !dom_attr(n, "checked") && !dom_attr(n, "selected")) return false;
        if ((ps & PS_DISABLED) && !dom_attr(n, "disabled")) return false;
    }
    for (int i = 0; i < c->nnot; i++) if (any_matches(c->not_list[i], n)) return false;
    for (int i = 0; i < c->nis; i++) if (!any_matches(c->is_list[i], n)) return false;
    return true;
}

static bool match_selector(const CssSelector *sel, int idx, const DomNode *n)
{
    if (!compound_match(&sel->c[idx], n)) return false;
    if (idx == 0) return true;
    switch (sel->c[idx].comb) {
        case COMB_CHILD:
            return n->parent && n->parent->type == DOM_ELEMENT && match_selector(sel, idx - 1, n->parent);
        case COMB_DESC:
            for (const DomNode *a = n->parent; a; a = a->parent)
                if (match_selector(sel, idx - 1, a)) return true;
            return false;
        case COMB_ADJ:
            for (const DomNode *s = n->prev; s; s = s->prev)
                if (s->type == DOM_ELEMENT) return match_selector(sel, idx - 1, s);
            return false;
        case COMB_SIB:
            for (const DomNode *s = n->prev; s; s = s->prev)
                if (s->type == DOM_ELEMENT && match_selector(sel, idx - 1, s)) return true;
            return false;
        default:
            return false;
    }
}

/* ------------------------------------------------------------------ */
/* Media queries                                                       */
/* ------------------------------------------------------------------ */

static int32_t media_len_px(const char *p, const char *end)
{
    int32_t v;
    if (!parse_num(&p, end, &v)) return -1;
    if (p + 1 < end && istarts(p, "em")) return v * 16 / 100;
    if (p + 2 < end && istarts(p, "rem")) return v * 16 / 100;
    return v / 100;
}

/* One media query (no commas). */
static bool media_query(const CssSheetSet *S, const char *p, const char *end)
{
    p = skip_ws(p, end);
    bool negate = false;
    if (istarts(p, "not ")) { negate = true; p += 4; }
    if (istarts(p, "only ")) p += 5;
    bool ok = true;
    while (p < end) {
        p = skip_ws(p, end);
        if (p >= end) break;
        if (istarts(p, "and") && (p + 3 >= end || ws(p[3]) || p[3] == '(')) { p += 3; continue; }
        if (*p == '(') {
            const char *close = skip_group(p, end);
            const char *f = skip_ws(p + 1, close - 1);
            const char *fe = close - 1;
            const char *colon = f;
            while (colon < fe && *colon != ':' && *colon != '<' && *colon != '>' && *colon != '=') colon++;
            size_t fl = (size_t)(colon - f);
            while (fl && ws(f[fl - 1])) fl--;
            const char *val = colon < fe ? skip_ws(colon + 1, fe) : fe;
            int vw = S->media.viewport_w;
            bool r = false;
            if (colon < fe && (*colon == '<' || *colon == '>')) {
                /* range syntax: (width >= 600px) / (width < 40em) */
                char op = *colon;
                bool eq = colon + 1 < fe && colon[1] == '=';
                const char *v2 = skip_ws(colon + (eq ? 2 : 1), fe);
                int32_t px = media_len_px(v2, fe);
                if (ieq(f, fl, "width")) r = op == '>' ? (eq ? vw >= px : vw > px) : (eq ? vw <= px : vw < px);
                else r = false;
            } else if (ieq(f, fl, "min-width")) r = vw >= media_len_px(val, fe);
            else if (ieq(f, fl, "max-width")) r = vw <= media_len_px(val, fe);
            else if (ieq(f, fl, "min-height")) r = S->media.viewport_h >= media_len_px(val, fe);
            else if (ieq(f, fl, "max-height")) r = S->media.viewport_h <= media_len_px(val, fe);
            else if (ieq(f, fl, "prefers-color-scheme")) r = istarts(val, "dark") ? S->media.dark : !S->media.dark;
            else if (ieq(f, fl, "prefers-reduced-motion")) r = istarts(val, "reduce");
            else if (ieq(f, fl, "orientation")) r = istarts(val, "landscape");
            else if (ieq(f, fl, "hover") || ieq(f, fl, "any-hover")) r = istarts(val, "hover") || val == fe;
            else if (ieq(f, fl, "pointer") || ieq(f, fl, "any-pointer")) r = istarts(val, "fine");
            else if (ieq(f, fl, "min-resolution") || ieq(f, fl, "-webkit-min-device-pixel-ratio")) r = false;
            else if (ieq(f, fl, "color")) r = true;
            else r = false;
            ok = ok && r;
            p = close;
            continue;
        }
        const char *e = read_ident(p, end);
        size_t n = (size_t)(e - p);
        if (!n) { p++; continue; }
        if (ieq(p, n, "screen") || ieq(p, n, "all")) { /* ok */ }
        else ok = false;                    /* print, speech, tv ... */
        p = e;
    }
    return negate ? !ok : ok;
}

static bool media_list(const CssSheetSet *S, const char *p, const char *end)
{
    p = skip_ws(p, end);
    if (p >= end) return true;
    while (p < end) {
        const char *s = p;
        int d = 0;
        while (p < end && !(d == 0 && *p == ',')) { if (*p == '(') d++; else if (*p == ')') d--; p++; }
        if (media_query(S, s, p)) return true;
        if (p < end) p++;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Sheet parsing                                                       */
/* ------------------------------------------------------------------ */

/* Parse declarations [p,end) into an arena array. */
static CssDecl *parse_decls(CssSheetSet *S, const char *p, const char *end, uint16_t *count)
{
    /* Design systems put hundreds of custom properties in one rule. Not
     * re-entered while running, so a static buffer keeps the stack small. */
    static CssDecl tmp[2048];
    int n = 0;
    while (p < end) {
        p = skip_ws(p, end);
        if (p >= end) break;
        if (*p == ';') { p++; continue; }
        const char *ns = p;
        while (p < end && *p != ':' && *p != ';' && *p != '{' && *p != '}') p++;
        if (p >= end) break;
        if (*p == '{') { p = skip_group(p, end); continue; }     /* nested rule: skipped */
        if (*p != ':') { p++; continue; }
        const char *ne = p;
        while (ne > ns && ws(ne[-1])) ne--;
        p++;
        const char *vs = skip_ws(p, end);
        const char *q = vs;
        while (q < end && *q != ';' && *q != '}') {
            if (*q == '(' || *q == '[') { q = skip_group(q, end); continue; }
            if (*q == '{') { q = skip_group(q, end); continue; }
            if (*q == '"' || *q == '\'') {
                char c = *q++;
                while (q < end && *q != c) { if (*q == '\\' && q + 1 < end) q++; q++; }
                if (q < end) q++;
                continue;
            }
            q++;
        }
        const char *ve = q;
        p = q < end ? q + 1 : end;
        while (ve > vs && ws(ve[-1])) ve--;
        bool important = false;
        if (ve - vs >= 10) {
            const char *b = ve - 1;
            while (b > vs && *b != '!') b--;
            if (*b == '!' && istarts(b + 1, "important")) { important = true; ve = b; while (ve > vs && ws(ve[-1])) ve--; }
        }
        uint8_t prop = prop_id(ns, (size_t)(ne - ns));
        if (prop == P_UNKNOWN || n >= (int)ARRAY_SIZE(tmp)) continue;
        tmp[n].prop = prop;
        tmp[n].important = important;
        tmp[n].name = prop == P_CUSTOM ? dup_ident(S->a, ns, (size_t)(ne - ns), false) : NULL;
        tmp[n].value = arena_strndup(S->a, vs, (size_t)(ve - vs));      /* raw: escapes are decoded per property */
        tmp[n].base = S->cur_base;
        if (!tmp[n].value) break;
        n++;
    }
    *count = (uint16_t)n;
    if (!n) return NULL;
    CssDecl *d = (CssDecl *)arena_alloc(S->a, sizeof(CssDecl) * (size_t)n);
    if (d) memcpy(d, tmp, sizeof(CssDecl) * (size_t)n);
    else *count = 0;
    return d;
}

static void add_rules(CssSheetSet *S, const char *sel_start, const char *sel_end, CssDecl *decls, uint16_t nd)
{
    const char *p = sel_start;
    while (p < sel_end) {
        const char *s = p;
        int d = 0;
        while (p < sel_end && !(d == 0 && *p == ',')) { if (*p == '(' || *p == '[') d++; else if (*p == ')' || *p == ']') d--; p++; }
        CssSelector *sel = parse_selector(S, s, p, 0);
        if (sel && S->rule_count < MAX_RULES) {
            CssRule *r = &S->rules[S->rule_count++];
            r->sel = sel;
            r->decls = decls;
            r->ndecls = nd;
            r->origin = S->origin;
            r->order = S->order++;
        }
        if (p < sel_end) p++;
    }
}

static void parse_sheet(CssSheetSet *S, const char *p, const char *end, int depth)
{
    if (depth > 8) return;
    while (p < end && !S->a->full) {
        p = skip_ws(p, end);
        if (p >= end) break;
        if (p + 1 < end && p[0] == '/' && p[1] == '*') {
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) p++;
            p = p + 1 < end ? p + 2 : end;
            continue;
        }
        if (*p == '<' || *p == '}' || *p == ';') { p++; continue; }   /* <!-- -->, stray */
        if (*p == '-' && p + 2 < end && p[1] == '-' && p[2] == '>') { p += 3; continue; }
        if (*p == '@') {
            const char *ks = p + 1;
            const char *ke = read_ident(ks, end);
            size_t kn = (size_t)(ke - ks);
            const char *q = ke;
            while (q < end && *q != '{' && *q != ';') {
                if (*q == '(' ) { q = skip_group(q, end); continue; }
                if (*q == '"' || *q == '\'') { char c = *q++; while (q < end && *q != c) q++; if (q < end) q++; continue; }
                q++;
            }
            if (ieq(ks, kn, "import")) {
                const char *u = skip_ws(ke, q);
                if (istarts(u, "url(")) u += 4;
                u = skip_ws(u, q);
                char quote = (*u == '"' || *u == '\'') ? *u++ : 0;
                const char *ue = u;
                while (ue < q && (quote ? *ue != quote : (*ue != ')' && !ws(*ue)))) ue++;
                /* media after the URL */
                const char *after = ue < q ? ue + 1 : q;
                if (after < q && *after == ')') after++;
                if (S->import_count < MAX_IMPORTS && ue > u && (size_t)(ue - u) < 255 && media_list(S, after, q)) {
                    memcpy(S->imports[S->import_count], u, (size_t)(ue - u));
                    S->imports[S->import_count][ue - u] = '\0';
                    S->import_count++;
                }
                p = q < end ? q + 1 : end;
                continue;
            }
            if (q >= end) break;
            if (*q == ';') { p = q + 1; continue; }
            const char *close = skip_group(q, end);
            const char *inner = q + 1, *inner_end = close > inner ? close - 1 : inner;
            if (ieq(ks, kn, "media")) {
                if (media_list(S, ke, q)) parse_sheet(S, inner, inner_end, depth + 1);
            } else if (ieq(ks, kn, "supports")) {
                /* assume supported unless it tests something we know we lack */
                const char *s = ke;
                bool ok = true;
                for (; s + 3 < q; s++) if (istarts(s, "not ")) { ok = false; break; }
                if (ok) parse_sheet(S, inner, inner_end, depth + 1);
            } else if (ieq(ks, kn, "layer") || ieq(ks, kn, "container") || ieq(ks, kn, "scope") ||
                       ieq(ks, kn, "document") || ieq(ks, kn, "-moz-document") || ieq(ks, kn, "starting-style")) {
                if (!ieq(ks, kn, "starting-style")) parse_sheet(S, inner, inner_end, depth + 1);
            }
            /* @font-face, @keyframes, @page, @property, @counter-style ... skipped */
            p = close;
            continue;
        }
        /* rule: selector list { declarations } */
        const char *q = p;
        while (q < end && *q != '{' && *q != '}') {
            if (*q == '(' || *q == '[') { q = skip_group(q, end); continue; }
            if (*q == '"' || *q == '\'') { char c = *q++; while (q < end && *q != c) q++; if (q < end) q++; continue; }
            q++;
        }
        if (q >= end) break;
        if (*q == '}') { p = q + 1; continue; }
        const char *close = skip_group(q, end);
        const char *de = close > q + 1 ? close - 1 : q + 1;
        uint16_t nd;
        CssDecl *decls = parse_decls(S, q + 1, de, &nd);
        if (nd) add_rules(S, p, q, decls, nd);
        p = close;
    }
}

static const char UA_SHEET[] =
    "html,address,blockquote,body,dd,div,dl,dt,fieldset,form,frame,frameset,h1,h2,h3,h4,h5,h6,noframes,ol,p,ul,"
    "center,dir,hr,menu,pre,header,footer,nav,main,section,article,aside,figure,figcaption,hgroup,details,summary,"
    "search,legend,listing,xmp,plaintext,optgroup,noscript{display:block}"
    "head,script,style,title,meta,link,base,template,noembed,noframes,param,datalist,area,source,track,"
    "[hidden],input[type=hidden],dialog:not([open]),option,colgroup,col{display:none}"
    "li{display:list-item}"
    "table{display:table;border-spacing:2px}caption{display:table-caption;text-align:center}"
    "thead,tbody,tfoot{display:table-row-group}tr{display:table-row}td,th{display:table-cell;padding:1px;vertical-align:middle}"
    "th{font-weight:bold;text-align:center}"
    "body{margin:8px;line-height:normal}"
    "p,dl{margin:1em 0}blockquote,figure{margin:1em 40px}"
    "ul,ol,menu{margin:1em 0;padding-left:40px}ul,menu{list-style-type:disc}ol{list-style-type:decimal}"
    "ul ul,ol ul,ul menu{list-style-type:circle}ul ul ul,ol ol ul,ol ul ul,ul ol ul{list-style-type:square}"
    "li ul,li ol,ul ul,ol ol,ul ol,ol ul{margin-top:0;margin-bottom:0}"
    "dd{margin-left:40px}"
    "h1{font-size:2em;margin:.67em 0;font-weight:bold}h2{font-size:1.5em;margin:.83em 0;font-weight:bold}"
    "h3{font-size:1.17em;margin:1em 0;font-weight:bold}h4{margin:1.33em 0;font-weight:bold}"
    "h5{font-size:.83em;margin:1.67em 0;font-weight:bold}h6{font-size:.67em;margin:2.33em 0;font-weight:bold}"
    "pre,code,kbd,samp,tt,xmp,listing,plaintext{font-family:monospace}pre,xmp,listing,plaintext{white-space:pre;margin:1em 0}"
    "b,strong{font-weight:bold}i,em,cite,var,dfn,address{font-style:italic}u,ins{text-decoration:underline}"
    "s,strike,del{text-decoration:line-through}small,sub,sup{font-size:smaller}big{font-size:larger}"
    "a[href]{color:#0000EE;text-decoration:underline}"
    "center{text-align:-webkit-center}"
    "hr{border:1px inset #c0c0c0;margin:.5em 0;height:0}"
    "img,input,button,select,textarea,iframe,video,audio,canvas,object,embed,svg{display:inline-block}"
    "mark{background-color:yellow;color:black}"
    "fieldset{margin:0 2px;padding:.35em .75em .625em;border:2px groove #c0c0c0}legend{padding:0 2px}"
    "details>:not(summary){display:none}details[open]>:not(summary){display:block}"
    "summary{display:block}"
    "nobr{white-space:nowrap}"
    "abbr[title]{text-decoration:underline}"
    "q:before{content:'\"'}"
    "button{text-align:center}";

static const char QUIRKS_SHEET[] =
    "table{text-align:left;white-space:normal;line-height:normal;font-weight:normal;font-size:medium;font-style:normal}"
    "img{vertical-align:bottom}";

CssSheetSet *css_new(WebArena *a, const CssMedia *media)
{
    CssSheetSet *S = (CssSheetSet *)arena_alloc(a, sizeof(CssSheetSet));
    if (!S) return NULL;
    S->a = a;
    S->media = *media;
    S->rules = (CssRule *)arena_alloc(a, sizeof(CssRule) * MAX_RULES);
    if (!S->rules) return NULL;
    S->origin = 0;
    parse_sheet(S, UA_SHEET, UA_SHEET + sizeof(UA_SHEET) - 1, 0);
    if (media->quirks) parse_sheet(S, QUIRKS_SHEET, QUIRKS_SHEET + sizeof(QUIRKS_SHEET) - 1, 0);
    S->origin = 1;
    return S;
}

void css_add_sheet(CssSheetSet *S, const char *css, size_t len)
{
    css_add_sheet_base(S, css, len, NULL);
}

void css_add_sheet_base(CssSheetSet *S, const char *css, size_t len, const char *base_dir)
{
    if (!S) return;
    S->cur_base = base_dir && base_dir[0] ? base_dir : NULL;
    parse_sheet(S, css, css + len, 0);
    S->cur_base = NULL;
    S->indexed = false;
}

int css_import_count(const CssSheetSet *S) { return S ? S->import_count : 0; }
const char *css_import_url(const CssSheetSet *S, int i) { return S && i >= 0 && i < S->import_count ? S->imports[i] : ""; }
int css_rule_count(const CssSheetSet *S) { return S ? S->rule_count : 0; }

bool css_media_matches(const CssSheetSet *S, const char *media)
{
    if (!S || !media) return true;
    return media_list(S, media, media + strlen(media));
}

/* ------------------------------------------------------------------ */
/* Rule index                                                          */
/* ------------------------------------------------------------------ */

static void bucket_add(CssSheetSet *S, HashEntry **table, const char *key, int rule)
{
    uint32_t h = hash_str(key) & (HASH_BUCKETS - 1);
    HashEntry *e = table[h];
    while (e && strcmp(e->key, key) != 0) e = e->next;
    if (!e) {
        e = (HashEntry *)arena_alloc(S->a, sizeof(HashEntry));
        if (!e) return;
        e->key = key;
        e->next = table[h];
        table[h] = e;
    }
    if (e->count == e->cap) {
        int cap = e->cap ? e->cap * 2 : 4;
        int *nr = (int *)arena_alloc(S->a, sizeof(int) * (size_t)cap);
        if (!nr) return;
        if (e->count) memcpy(nr, e->rules, sizeof(int) * (size_t)e->count);
        e->rules = nr;
        e->cap = cap;
    }
    e->rules[e->count++] = rule;
}

static void build_index(CssSheetSet *S)
{
    memset(S->by_id, 0, sizeof(S->by_id));
    memset(S->by_class, 0, sizeof(S->by_class));
    memset(S->by_tag, 0, sizeof(S->by_tag));
    S->universal_count = 0;
    S->universal_cap = 256;
    S->universal = (int *)arena_alloc(S->a, sizeof(int) * (size_t)S->universal_cap);
    for (int i = 0; i < S->rule_count; i++) {
        const CssCompound *c = &S->rules[i].sel->c[S->rules[i].sel->n - 1];
        if (c->pseudo & PS_NEVER) continue;
        if (c->id) bucket_add(S, S->by_id, c->id, i);
        else if (c->nclasses) bucket_add(S, S->by_class, c->classes[0], i);
        else if (c->tag) bucket_add(S, S->by_tag, c->tag, i);
        else {
            if (S->universal_count == S->universal_cap) {
                int cap = S->universal_cap * 2;
                int *nu = (int *)arena_alloc(S->a, sizeof(int) * (size_t)cap);
                if (!nu) continue;
                memcpy(nu, S->universal, sizeof(int) * (size_t)S->universal_count);
                S->universal = nu;
                S->universal_cap = cap;
            }
            if (S->universal) S->universal[S->universal_count++] = i;
        }
    }
    S->indexed = true;
}

static const HashEntry *bucket_find(HashEntry *const *table, const char *key)
{
    uint32_t h = hash_str(key) & (HASH_BUCKETS - 1);
    for (const HashEntry *e = table[h]; e; e = e->next) if (strcmp(e->key, key) == 0) return e;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Values                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    const CssStyle *parent;
    CssStyle *s;
    const CssSheetSet *S;
    int root_fs;
} Ctx;

/* Substitute var(--x, fallback) into out (bounded, depth-limited). */
static const char *var_lookup(const CssStyle *s, const char *name, size_t n)
{
    for (const CssVar *v = s->vars; v; v = v->next)
        if (strlen(v->name) == n && strncmp(v->name, name, n) == 0) return v->value;
    return NULL;
}

static bool substitute(const CssStyle *s, const char *in, char *out, size_t cap, int depth)
{
    size_t o = 0;
    const char *p = in;
    while (*p) {
        if (istarts(p, "var(")) {
            const char *a = p + 4;
            while (ws(*a)) a++;
            const char *ne = a;
            while (*ne && *ne != ',' && *ne != ')' && !ws(*ne)) ne++;
            /* find the matching ')' */
            const char *q = a;
            int d = 1;
            const char *comma = NULL;
            while (*q && d) {
                if (*q == '(') d++;
                else if (*q == ')') { if (--d == 0) break; }
                else if (*q == ',' && d == 1 && !comma) comma = q;
                q++;
            }
            if (!*q) return false;
            const char *val = var_lookup(s, a, (size_t)(ne - a));
            char tmp[VALUE_MAX];
            if (val) {
                if (depth >= VAR_DEPTH || !substitute(s, val, tmp, sizeof(tmp), depth + 1)) return false;
            } else if (comma) {
                size_t fl = (size_t)(q - comma - 1);
                if (fl >= sizeof(tmp)) return false;
                char fb[VALUE_MAX];
                memcpy(fb, comma + 1, fl);
                fb[fl] = '\0';
                if (depth >= VAR_DEPTH || !substitute(s, fb, tmp, sizeof(tmp), depth + 1)) return false;
            } else {
                return false;           /* invalid at computed-value time */
            }
            size_t tl = strlen(tmp);
            if (o + tl + 1 >= cap) return false;
            memcpy(out + o, tmp, tl);
            o += tl;
            p = q + 1;
            continue;
        }
        if (o + 2 >= cap) return false;
        out[o++] = *p++;
    }
    out[o] = '\0';
    return true;
}

/* A length token or calc() expression -> CssLen. em = own font size. */
typedef struct { int64_t px100, pct100; bool num; bool ok; } CalcVal;

static CalcVal calc_expr(const Ctx *C, const char **pp, const char *end, int fs, int depth);

static CalcVal calc_term(const Ctx *C, const char **pp, const char *end, int fs, int depth)
{
    CalcVal r = {0, 0, false, false};
    const char *p = skip_ws(*pp, end);
    if (depth > 8) return r;
    if (p < end && *p == '(') {
        p++;
        r = calc_expr(C, &p, end, fs, depth + 1);
        p = skip_ws(p, end);
        if (p < end && *p == ')') p++;
        *pp = p;
        return r;
    }
    if (istarts(p, "calc(") || istarts(p, "min(") || istarts(p, "max(") || istarts(p, "clamp(")) {
        int kind = lc(*p) == 'c' && lc(p[1]) == 'a' ? 0 : lc(p[1]) == 'i' ? 1 : lc(p[1]) == 'a' ? 2 : 3;
        while (p < end && *p != '(') p++;
        p++;
        CalcVal args[3];
        int n = 0;
        while (p < end && n < 3) {
            args[n++] = calc_expr(C, &p, end, fs, depth + 1);
            p = skip_ws(p, end);
            if (p < end && *p == ',') { p++; continue; }
            break;
        }
        p = skip_ws(p, end);
        if (p < end && *p == ')') p++;
        *pp = p;
        if (!n) return r;
        for (int i = 0; i < n; i++) if (!args[i].ok) return r;
        if (kind == 0) return args[0];
        /* Compare against the viewport for percentages (best guess). */
        int64_t vw = C->S->media.viewport_w;
        int64_t v[3];
        for (int i = 0; i < n; i++) v[i] = args[i].px100 + args[i].pct100 * vw / 10000;
        int pick = 0;
        if (kind == 1) { for (int i = 1; i < n; i++) if (v[i] < v[pick]) pick = i; }
        else if (kind == 2) { for (int i = 1; i < n; i++) if (v[i] > v[pick]) pick = i; }
        else if (n == 3) { pick = v[1] < v[0] ? 0 : v[1] > v[2] ? 2 : 1; }
        return args[pick];
    }
    int32_t num;
    if (!parse_num(&p, end, &num)) { *pp = p; return r; }
    r.ok = true;
    const char *u = p;
    while (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '%')) p++;
    size_t ul = (size_t)(p - u);
    *pp = p;
    if (!ul) { r.num = true; r.px100 = num; return r; }
    int64_t Z = C->S->media.zoom > 0 ? C->S->media.zoom : 100;
    if (ieq(u, ul, "px")) r.px100 = (int64_t)num * Z / 100;
    else if (ieq(u, ul, "%")) r.pct100 = num;
    else if (ieq(u, ul, "em")) r.px100 = (int64_t)num * fs;
    else if (ieq(u, ul, "rem")) r.px100 = (int64_t)num * C->root_fs;
    else if (ieq(u, ul, "ex") || ieq(u, ul, "ch")) r.px100 = (int64_t)num * fs / 2;
    else if (ieq(u, ul, "vw") || ieq(u, ul, "svw") || ieq(u, ul, "dvw") || ieq(u, ul, "lvw") || ieq(u, ul, "vi"))
        r.px100 = (int64_t)num * C->S->media.viewport_w * Z / 10000;
    else if (ieq(u, ul, "vh") || ieq(u, ul, "svh") || ieq(u, ul, "dvh") || ieq(u, ul, "lvh") || ieq(u, ul, "vb"))
        r.px100 = (int64_t)num * C->S->media.viewport_h * Z / 10000;
    else if (ieq(u, ul, "vmin")) r.px100 = (int64_t)num * (C->S->media.viewport_w < C->S->media.viewport_h ? C->S->media.viewport_w : C->S->media.viewport_h) / 100;
    else if (ieq(u, ul, "vmax")) r.px100 = (int64_t)num * (C->S->media.viewport_w > C->S->media.viewport_h ? C->S->media.viewport_w : C->S->media.viewport_h) / 100;
    else if (ieq(u, ul, "pt")) r.px100 = (int64_t)num * 4 * Z / 300;
    else if (ieq(u, ul, "pc")) r.px100 = (int64_t)num * 16 * Z / 100;
    else if (ieq(u, ul, "in")) r.px100 = (int64_t)num * 96 * Z / 100;
    else if (ieq(u, ul, "cm")) r.px100 = (int64_t)num * 3780 * Z / 10000;
    else if (ieq(u, ul, "mm")) r.px100 = (int64_t)num * 378 * Z / 10000;
    else if (ieq(u, ul, "q")) r.px100 = (int64_t)num * 945 / 1000;
    else if (ieq(u, ul, "lh")) r.px100 = (int64_t)num * fs * 12 / 10;
    else if (ieq(u, ul, "fr")) { r.px100 = 0; r.pct100 = 0; r.num = true; }
    else r.ok = false;
    return r;
}

static CalcVal calc_product(const Ctx *C, const char **pp, const char *end, int fs, int depth)
{
    CalcVal a = calc_term(C, pp, end, fs, depth);
    for (;;) {
        const char *p = skip_ws(*pp, end);
        if (p >= end || (*p != '*' && *p != '/')) break;
        char op = *p++;
        *pp = p;
        CalcVal b = calc_term(C, pp, end, fs, depth);
        if (!a.ok || !b.ok) { a.ok = false; break; }
        if (op == '*') {
            if (b.num) { a.px100 = a.px100 * b.px100 / 100; a.pct100 = a.pct100 * b.px100 / 100; }
            else if (a.num) { int64_t k = a.px100; a = b; a.px100 = a.px100 * k / 100; a.pct100 = a.pct100 * k / 100; }
            else a.ok = false;
        } else {
            if (!b.num || b.px100 == 0) { a.ok = false; break; }
            a.px100 = a.px100 * 100 / b.px100;
            a.pct100 = a.pct100 * 100 / b.px100;
        }
    }
    return a;
}

static CalcVal calc_expr(const Ctx *C, const char **pp, const char *end, int fs, int depth)
{
    CalcVal a = calc_product(C, pp, end, fs, depth);
    for (;;) {
        const char *p = skip_ws(*pp, end);
        if (p >= end || (*p != '+' && *p != '-')) break;
        if (p + 1 < end && !ws(p[1])) break;            /* "-5px" without spaces is a token */
        char op = *p++;
        *pp = p;
        CalcVal b = calc_product(C, pp, end, fs, depth);
        if (!a.ok || !b.ok) { a.ok = false; break; }
        if (op == '+') { a.px100 += b.px100; a.pct100 += b.pct100; }
        else { a.px100 -= b.px100; a.pct100 -= b.pct100; }
        if (b.num != a.num) { a.num = false; }
    }
    return a;
}

/* Parse a single length value (keywords handled by callers). */
static bool parse_len(const Ctx *C, const char *p, const char *end, int fs, CssLen *out)
{
    p = skip_ws(p, end);
    if (p >= end) return false;
    if (istarts(p, "auto")) { out->kind = LEN_AUTO; out->px = out->pct = 0; return true; }
    if (istarts(p, "none")) { out->kind = LEN_NONE; out->px = out->pct = 0; return true; }
    if (istarts(p, "fit-content") || istarts(p, "max-content") || istarts(p, "min-content") ||
        istarts(p, "-webkit-fill-available") || istarts(p, "stretch")) {
        out->kind = LEN_AUTO; out->px = out->pct = 0;
        return true;
    }
    const char *q = p;
    CalcVal v = calc_expr(C, &q, end, fs, 0);
    if (!v.ok) return false;
    if (v.num && v.px100 != 0) return false;        /* unitless non-zero is invalid for lengths */
    out->kind = LEN_SET;
    int64_t px = v.px100 / 100, pct = v.pct100;
    if (px > 100000) px = 100000;
    if (px < -100000) px = -100000;
    if (pct > 1000000) pct = 1000000;
    if (pct < -1000000) pct = -1000000;
    out->px = (int32_t)px;
    out->pct = (int32_t)pct;
    return true;
}

/* Split a value into up to 4 space-separated tokens (respecting parens). */
static int split_tokens(const char *p, const char *end, const char **ts, const char **te, int max)
{
    int n = 0;
    while (p < end && n < max) {
        p = skip_ws(p, end);
        if (p >= end) break;
        const char *s = p;
        while (p < end && !ws(*p)) {
            if (*p == '(') { p = skip_group(p, end); continue; }
            if (*p == ',' ) { if (p == s) p++; break; }
            p++;
        }
        ts[n] = s;
        te[n] = p;
        n++;
    }
    return n;
}

static void box4(const Ctx *C, const char *v, CssLen *dst, int fs, bool allow_auto)
{
    const char *ts[4], *te[4];
    const char *end = v + strlen(v);
    int n = split_tokens(v, end, ts, te, 4);
    CssLen l[4];
    for (int i = 0; i < n; i++) {
        if (!parse_len(C, ts[i], te[i], fs, &l[i])) return;
        if (!allow_auto && l[i].kind != LEN_SET) return;
    }
    if (n == 1) { dst[0] = dst[1] = dst[2] = dst[3] = l[0]; }
    else if (n == 2) { dst[0] = dst[2] = l[0]; dst[1] = dst[3] = l[1]; }
    else if (n == 3) { dst[0] = l[0]; dst[1] = dst[3] = l[1]; dst[2] = l[2]; }
    else if (n == 4) { dst[0] = l[0]; dst[1] = l[1]; dst[2] = l[2]; dst[3] = l[3]; }
}

static void box2(const Ctx *C, const char *v, CssLen *a, CssLen *b, int fs)
{
    const char *ts[2], *te[2];
    const char *end = v + strlen(v);
    int n = split_tokens(v, end, ts, te, 2);
    CssLen l[2];
    for (int i = 0; i < n; i++) if (!parse_len(C, ts[i], te[i], fs, &l[i])) return;
    if (n == 1) *a = *b = l[0];
    else if (n == 2) { *a = l[0]; *b = l[1]; }
}

static int border_width_kw(const Ctx *C, const char *s, const char *e, int fs, bool *ok)
{
    *ok = true;
    if (istarts(s, "thin")) return 1;
    if (istarts(s, "medium")) return 3;
    if (istarts(s, "thick")) return 5;
    CssLen l;
    if (parse_len(C, s, e, fs, &l) && l.kind == LEN_SET && !l.pct) {
        if (l.px == 0 && s[0] == '0' && s + 1 < e && s[1] == '.') return 1;     /* hairlines: 0.5px */
        return l.px;
    }
    *ok = false;
    return 0;
}

static bool is_border_style(const char *s)
{
    static const char *const st[] = {"none", "hidden", "solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset"};
    for (unsigned i = 0; i < ARRAY_SIZE(st); i++) if (istarts(s, st[i])) return true;
    return false;
}

/* border side shorthand: width style color in any order. side -1 = all. */
static void border_short(const Ctx *C, CssStyle *s, int side, const char *v, int fs)
{
    const char *ts[4], *te[4];
    int n = split_tokens(v, v + strlen(v), ts, te, 4);
    int w = 3;                  /* medium */
    bool none = true;
    uint32_t col = s->color;
    for (int i = 0; i < n; i++) {
        bool ok;
        uint32_t c;
        if (is_border_style(ts[i])) { none = istarts(ts[i], "none") || istarts(ts[i], "hidden"); continue; }
        int bw = border_width_kw(C, ts[i], te[i], fs, &ok);
        if (ok) { w = bw; continue; }
        if (istarts(ts[i], "currentcolor")) { col = s->color; continue; }
        if (parse_color_n(ts[i], (size_t)(te[i] - ts[i]), &c)) col = c;
    }
    if (n == 1 && (istarts(v, "0") || istarts(v, "none"))) none = true;
    for (int k = 0; k < 4; k++) {
        if (side >= 0 && k != side) continue;
        s->border_w[k] = (int16_t)(none ? 0 : w);
        s->border_c[k] = col;
    }
}

static uint8_t list_type(const char *v)
{
    if (istarts(v, "none")) return LIST_NONE;
    if (istarts(v, "disc")) return LIST_DISC;
    if (istarts(v, "circle")) return LIST_CIRCLE;
    if (istarts(v, "square")) return LIST_SQUARE;
    if (istarts(v, "decimal")) return LIST_DECIMAL;
    if (istarts(v, "lower-alpha") || istarts(v, "lower-latin")) return LIST_LOWER_ALPHA;
    if (istarts(v, "upper-alpha") || istarts(v, "upper-latin")) return LIST_UPPER_ALPHA;
    if (istarts(v, "lower-roman")) return LIST_LOWER_ROMAN;
    if (istarts(v, "upper-roman")) return LIST_UPPER_ROMAN;
    return 255;
}

static uint8_t flex_kw(const char *v)
{
    if (istarts(v, "safe ") || istarts(v, "unsafe ")) { while (*v && !ws(*v)) v++; while (ws(*v)) v++; }
    if (istarts(v, "flex-start") || istarts(v, "start") || istarts(v, "left") || istarts(v, "self-start") || istarts(v, "normal")) return FLEX_START;
    if (istarts(v, "flex-end") || istarts(v, "end") || istarts(v, "right") || istarts(v, "self-end")) return FLEX_END;
    if (istarts(v, "center")) return FLEX_CENTER;
    if (istarts(v, "space-between")) return FLEX_BETWEEN;
    if (istarts(v, "space-around")) return FLEX_AROUND;
    if (istarts(v, "space-evenly")) return FLEX_EVENLY;
    if (istarts(v, "stretch")) return FLEX_STRETCH;
    if (istarts(v, "baseline") || istarts(v, "first baseline") || istarts(v, "last baseline")) return FLEX_BASELINE;
    return 255;
}

static int font_size_value(const Ctx *C, const char *v, int parent_fs)
{
    static const struct { const char *k; int px; } kws[] = {
        {"xx-small", 9}, {"x-small", 10}, {"small", 13}, {"medium", 16}, {"large", 18}, {"x-large", 24},
        {"xx-large", 32}, {"xxx-large", 48},
    };
    if (istarts(v, "smaller")) return parent_fs * 5 / 6;
    if (istarts(v, "larger")) return parent_fs * 6 / 5;
    for (unsigned i = 0; i < ARRAY_SIZE(kws); i++) {
        size_t l = strlen(kws[i].k);
        if (istarts(v, kws[i].k) && !ident_char(v[l])) return kws[i].px * (C->S->media.zoom > 0 ? C->S->media.zoom : 100) / 100;
    }
    CssLen l;
    if (!parse_len(C, v, v + strlen(v), parent_fs, &l) || l.kind != LEN_SET) return -1;
    int px = l.px + (int)((int64_t)l.pct * parent_fs / 10000);
    return px;
}

static int weight_value(const char *v, int parent)
{
    if (istarts(v, "bold")) return 700;
    if (istarts(v, "bolder")) return parent >= 600 ? 900 : 700;
    if (istarts(v, "lighter")) return parent >= 600 ? 400 : 100;
    if (istarts(v, "normal")) return 400;
    if (*v >= '1' && *v <= '9') {
        int w = 0;
        for (const char *c = v; *c >= '0' && *c <= '9' && w < 2000; c++) w = w * 10 + (*c - '0');
        return w;
    }
    return -1;
}

static bool family_is_mono(const char *v)
{
    for (const char *p = v; *p; p++) {
        if (istarts(p, "mono") || istarts(p, "courier") || istarts(p, "menlo") || istarts(p, "consolas") ||
            istarts(p, "monaco") || istarts(p, "code")) return true;
    }
    return false;
}

static void set_line_height(const Ctx *C, CssStyle *s, const char *v)
{
    if (istarts(v, "normal")) { s->line_height = 0; return; }
    const char *p = v, *end = v + strlen(v);
    int32_t num;
    const char *q = p;
    if (parse_num(&q, end, &num) && (q >= end || ws(*q))) {       /* unitless multiplier */
        int lh = (int)((int64_t)num * s->font_size / 100);
        s->line_height = (int16_t)(lh < 1 ? 1 : lh > 1000 ? 1000 : lh);
        return;
    }
    CssLen l;
    if (parse_len(C, v, end, s->font_size, &l) && l.kind == LEN_SET) {
        int lh = l.px + (int)((int64_t)l.pct * s->font_size / 10000);
        s->line_height = (int16_t)(lh < 1 ? 1 : lh > 1000 ? 1000 : lh);
    }
}

static void apply_font_shorthand(const Ctx *C, CssStyle *s, const char *v, int parent_fs)
{
    /* [style] [variant] [weight] size[/line-height] family */
    const char *p = v;
    for (int guard = 0; *p && guard < 8; guard++) {
        while (ws(*p)) p++;
        if (istarts(p, "italic") || istarts(p, "oblique")) { s->italic = 1; }
        else if (istarts(p, "normal") || istarts(p, "small-caps")) { }
        else if (weight_value(p, 400) > 0 && !(p[0] >= '0' && p[0] <= '9' && (p[1] == 'p' || p[2] == 'p' || p[3] == 'p'))) {
            const char *q = p;
            while (*q && !ws(*q)) q++;
            /* a bare number of 3 digits is a weight; "16px" is a size */
            bool unitless = true;
            for (const char *c = p; c < q; c++) if (!(*c >= '0' && *c <= '9')) unitless = false;
            if (unitless || istarts(p, "bold") || istarts(p, "lighter") || istarts(p, "bolder")) s->font_weight = (uint16_t)weight_value(p, 400);
            else break;
        } else break;
        while (*p && !ws(*p)) p++;
    }
    while (ws(*p)) p++;
    const char *size_end = p;
    while (*size_end && !ws(*size_end) && *size_end != '/') size_end++;
    if (size_end > p) {
        char tmp[64];
        size_t n = (size_t)(size_end - p) < sizeof(tmp) - 1 ? (size_t)(size_end - p) : sizeof(tmp) - 1;
        memcpy(tmp, p, n);
        tmp[n] = '\0';
        int fs = font_size_value(C, tmp, parent_fs);
        if (fs > 0) s->font_size = (int16_t)(fs > 200 ? 200 : fs);
    }
    p = size_end;
    if (*p == '/') {
        p++;
        const char *le = p;
        while (*le && !ws(*le)) le++;
        char tmp[64];
        size_t n = (size_t)(le - p) < sizeof(tmp) - 1 ? (size_t)(le - p) : sizeof(tmp) - 1;
        memcpy(tmp, p, n);
        tmp[n] = '\0';
        set_line_height(C, s, tmp);
        p = le;
    }
    s->mono = family_is_mono(p);
}

/* Inherited properties copied from the parent before the cascade. */
static void inherit_from(CssStyle *s, const CssStyle *p)
{
    s->color = p->color;
    s->font_size = p->font_size;
    s->font_weight = p->font_weight;
    s->italic = p->italic;
    s->mono = p->mono;
    s->line_height = p->line_height;
    s->text_align = p->text_align;
    s->center_blocks = p->center_blocks;
    s->white_space = p->white_space;
    s->invisible = p->invisible;
    s->list_style = p->list_style;
    s->transform = p->transform;
    s->text_indent = p->text_indent;
    s->border_spacing = p->border_spacing;
    s->decoration = p->decoration;          /* propagates to descendants' text */
    s->vars = p->vars;
}

static void initial_values(CssStyle *s)
{
    s->display = DISP_INLINE;
    s->flex_shrink = 100;
    s->align_items = FLEX_STRETCH;
    s->align_self = 255;
    s->justify = FLEX_START;
    for (int i = 0; i < 4; i++) {
        s->margin[i].kind = LEN_SET;
        s->padding[i].kind = LEN_SET;
        s->border_c[i] = s->color;
    }
    s->max_w.kind = LEN_NONE;
    s->max_h.kind = LEN_NONE;
    s->min_w.kind = LEN_SET;
    s->min_h.kind = LEN_SET;
}

/* url(...) at t -> arena string, made absolute against the sheet's directory. */
static const char *bg_url(const Ctx *C, const CssDecl *d, const char *t, const char *end)
{
    const char *u = t + 4;
    while (u < end && ws(*u)) u++;
    char q = 0;
    if (u < end && (*u == '"' || *u == '\'')) q = *u++;
    const char *ue = u;
    while (ue < end && (q ? *ue != q : (*ue != ')' && !ws(*ue)))) ue++;
    size_t n = (size_t)(ue - u);
    if (!n || istarts(u, "data:")) return NULL;
    bool absolute = istarts(u, "http:") || istarts(u, "https:") || *u == '/';
    if (absolute || !d->base) return dup_ident(C->S->a, u, n, false);
    size_t bl = strlen(d->base);
    char *o = (char *)arena_alloc(C->S->a, bl + n + 1);
    if (!o) return NULL;
    memcpy(o, d->base, bl);
    memcpy(o + bl, u, n);
    o[bl + n] = '\0';
    return o;
}

static bool bg_position_token(const Ctx *C, CssStyle *s, const char *t, const char *te, int *axis, int fs)
{
    /* keywords pick an axis; lengths fill x then y */
    if (istarts(t, "left")) { s->bg_px[0] = 0; s->bg_pct[0] = 0; *axis = 1; return true; }
    if (istarts(t, "right")) { s->bg_px[0] = 0; s->bg_pct[0] = 10000; *axis = 1; return true; }
    if (istarts(t, "top")) { s->bg_px[1] = 0; s->bg_pct[1] = 0; return true; }
    if (istarts(t, "bottom")) { s->bg_px[1] = 0; s->bg_pct[1] = 10000; return true; }
    if (istarts(t, "center")) { s->bg_px[*axis] = 0; s->bg_pct[*axis] = 5000; if (*axis == 0) *axis = 1; return true; }
    CssLen l;
    if (parse_len(C, t, te, fs, &l) && l.kind == LEN_SET) {
        int a = *axis > 1 ? 1 : *axis;
        s->bg_px[a] = (int16_t)(l.px > 30000 ? 30000 : l.px < -30000 ? -30000 : l.px);
        s->bg_pct[a] = (int16_t)(l.pct > 30000 ? 30000 : l.pct < -30000 ? -30000 : l.pct);
        *axis = a + 1;
        return true;
    }
    return false;
}

static bool bg_size_value(const Ctx *C, CssStyle *s, const char *v, const char *end, int fs)
{
    v = skip_ws(v, end);
    if (istarts(v, "cover")) { s->bg_size = BG_COVER; return true; }
    if (istarts(v, "contain")) { s->bg_size = BG_CONTAIN; return true; }
    const char *ts[2], *te[2];
    int n = split_tokens(v, end, ts, te, 2);
    if (!n) return false;
    CssLen a, b;
    if (!parse_len(C, ts[0], te[0], fs, &a)) return false;
    bool hb = n > 1 && parse_len(C, ts[1], te[1], fs, &b);
    if (a.kind == LEN_AUTO && (!hb || b.kind == LEN_AUTO)) { s->bg_size = BG_AUTO; return true; }
    s->bg_size = BG_LEN;
    /* percentages of the box are resolved at layout: store negative hundredths */
    s->bg_w = a.kind == LEN_SET ? (int16_t)(a.pct ? -(a.pct / 100) - 2 : a.px) : -1;
    s->bg_h = hb && b.kind == LEN_SET ? (int16_t)(b.pct ? -(b.pct / 100) - 2 : b.px) : -1;
    return true;
}

static const char *strchr_local(const char *s, char c)
{
    for (; *s; s++) if (*s == c) return s;
    return NULL;
}

static CssGrid *grid_of(const Ctx *C, CssStyle *s)
{
    if (!s->grid) s->grid = (CssGrid *)arena_alloc(C->S->a, sizeof(CssGrid));
    return s->grid;
}

/* One track size: length, %, fr, auto, min-content, max-content. */
static bool parse_track_size(const Ctx *C, const char *p, const char *e, int fs, uint8_t *kind, int32_t *px, int32_t *pct, int32_t *fr)
{
    p = skip_ws(p, e);
    while (e > p && ws(e[-1])) e--;
    size_t n = (size_t)(e - p);
    if (!n) return false;
    if (ieq(p, n, "auto") || istarts(p, "fit-content")) { *kind = TK_AUTO; return true; }
    if (ieq(p, n, "min-content")) { *kind = TK_MINC; return true; }
    if (ieq(p, n, "max-content")) { *kind = TK_MAXC; return true; }
    if (n > 2 && lc(e[-2]) == 'f' && lc(e[-1]) == 'r') {
        const char *q = p;
        int32_t v;
        if (!parse_num(&q, e - 2, &v)) return false;
        *kind = TK_FR;
        *fr = v;
        return true;
    }
    CssLen l;
    if (!parse_len(C, p, e, fs, &l) || l.kind != LEN_SET) return false;
    *kind = TK_FIXED;
    *px = l.px;
    *pct = l.pct;
    return true;
}

static bool parse_track(const Ctx *C, const char *p, const char *e, int fs, CssTrack *t)
{
    memset(t, 0, sizeof(*t));
    p = skip_ws(p, e);
    if (istarts(p, "minmax(")) {
        const char *a = p + 7, *close = skip_group(p + 6, e) - 1, *comma = a;
        int depth = 0;
        while (comma < close && !(depth == 0 && *comma == ',')) { if (*comma == '(') depth++; else if (*comma == ')') depth--; comma++; }
        if (comma >= close) return false;
        uint8_t k;
        int32_t px = 0, pct = 0, fr = 0;
        if (!parse_track_size(C, a, comma, fs, &k, &px, &pct, &fr)) return false;
        t->min_kind = k == TK_FIXED ? TK_FIXED : TK_AUTO;
        t->min_px = px;
        t->min_pct = pct;
        if (!parse_track_size(C, comma + 1, close, fs, &t->kind, &t->px, &t->pct, &t->fr)) return false;
        return true;
    }
    if (!parse_track_size(C, p, e, fs, &t->kind, &t->px, &t->pct, &t->fr)) return false;
    t->min_kind = t->kind == TK_FIXED ? TK_FIXED : TK_AUTO;
    t->min_px = t->px;
    t->min_pct = t->pct;
    if (t->kind == TK_FR) { t->min_kind = TK_AUTO; }
    return true;
}

static void parse_tracks(const Ctx *C, CssStyle *s, const char *v, int fs)
{
    CssGrid *g = grid_of(C, s);
    if (!g) return;
    g->n = 0;
    g->auto_repeat = 0;
    const char *p = v, *end = v + strlen(v);
    while (p < end && g->n < GRID_MAX_TRACKS) {
        p = skip_ws(p, end);
        if (p >= end) break;
        if (*p == '[') { p = skip_group(p, end); continue; }          /* line names */
        const char *t = p;
        while (p < end && !ws(*p)) { if (*p == '(') { p = skip_group(p, end); continue; } if (*p == '[') break; p++; }
        if (istarts(t, "repeat(")) {
            const char *a = t + 7, *close = p - 1, *comma = a;
            while (comma < close && *comma != ',') comma++;
            if (comma >= close) continue;
            const char *body = comma + 1;
            if (istarts(skip_ws(a, comma), "auto-fill") || istarts(skip_ws(a, comma), "auto-fit")) {
                g->auto_repeat = istarts(skip_ws(a, comma), "auto-fill") ? 1 : 2;
                g->rep_at = g->n;
                /* the first track of the repeat body */
                const char *b = skip_ws(body, close);
                const char *be = b;
                while (be < close && !ws(*be)) { if (*be == '(') { be = skip_group(be, close); continue; } be++; }
                if (!parse_track(C, b, be, fs, &g->rep)) g->auto_repeat = 0;
                continue;
            }
            const char *q = a;
            int32_t count;
            if (!parse_num(&q, comma, &count)) continue;
            count /= 100;
            if (count < 1) count = 1;
            if (count > GRID_MAX_TRACKS) count = GRID_MAX_TRACKS;
            /* the repeat body may hold several tracks */
            CssTrack body_tracks[8];
            int nb = 0;
            const char *b = body;
            while (b < close && nb < 8) {
                b = skip_ws(b, close);
                if (b >= close) break;
                if (*b == '[') { b = skip_group(b, close); continue; }
                const char *be = b;
                while (be < close && !ws(*be)) { if (*be == '(') { be = skip_group(be, close); continue; } if (*be == '[') break; be++; }
                if (parse_track(C, b, be, fs, &body_tracks[nb])) nb++;
                b = be;
            }
            for (int r = 0; r < count && g->n < GRID_MAX_TRACKS; r++)
                for (int k = 0; k < nb && g->n < GRID_MAX_TRACKS; k++) g->t[g->n++] = body_tracks[k];
            continue;
        }
        if (parse_track(C, t, p, fs, &g->t[g->n])) g->n++;
    }
    s->grid_cols = g->n ? (uint8_t)(g->n > 255 ? 255 : g->n) : (g->auto_repeat ? 255 : 0);
}

/* grid-template-areas: "a a b" "c c b" */
static void parse_areas(const Ctx *C, CssStyle *s, const char *v)
{
    CssGrid *g = grid_of(C, s);
    if (!g) return;
    g->narea = 0;
    g->area_cols = 0;
    int row = 0;
    for (const char *p = v; *p && row < 64; p++) {
        if (*p != '"' && *p != '\'') continue;
        char q = *p++;
        int col = 0;
        while (*p && *p != q) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p || *p == q) break;
            const char *ns = p;
            while (*p && *p != q && *p != ' ' && *p != '\t') p++;
            size_t nl = (size_t)(p - ns);
            bool dot = true;
            for (size_t i = 0; i < nl; i++) if (ns[i] != '.') dot = false;
            if (!dot) {
                int found = -1;
                for (int i = 0; i < g->narea; i++) if (strlen(g->areas[i].name) == nl && strncmp(g->areas[i].name, ns, nl) == 0) found = i;
                if (found < 0 && g->narea < GRID_MAX_AREAS) {
                    found = g->narea++;
                    g->areas[found].name = dup_ident(C->S->a, ns, nl, false);
                    g->areas[found].r0 = (uint8_t)row;
                    g->areas[found].c0 = (uint8_t)col;
                    g->areas[found].r1 = (uint8_t)(row + 1);
                    g->areas[found].c1 = (uint8_t)(col + 1);
                } else if (found >= 0) {
                    CssArea *a = &g->areas[found];
                    if (col < a->c0) a->c0 = (uint8_t)col;
                    if (col + 1 > a->c1) a->c1 = (uint8_t)(col + 1);
                    if (row + 1 > a->r1) a->r1 = (uint8_t)(row + 1);
                }
            }
            col++;
        }
        if (col > g->area_cols) g->area_cols = (uint8_t)(col > 255 ? 255 : col);
        row++;
        if (!*p) break;
    }
}

/* "3", "-1", "span 2", "auto", "name" -> line (1-based, negative from end, 0 auto) and span. */
static void grid_line(const char *p, const char *e, int16_t *line, int16_t *span)
{
    p = skip_ws(p, e);
    *line = 0;
    if (istarts(p, "span")) {
        const char *q = skip_ws(p + 4, e);
        int32_t n;
        if (parse_num(&q, e, &n) && n >= 100) *span = (int16_t)(n / 100 > 64 ? 64 : n / 100);
        return;
    }
    const char *q = p;
    int32_t n;
    if (parse_num(&q, e, &n)) {
        int v = n / 100;
        if (v > 64) v = 64;
        if (v < -64) v = -64;
        *line = (int16_t)v;
        q = skip_ws(q, e);
        if (istarts(q, "span")) { const char *r = skip_ws(q + 4, e); if (parse_num(&r, e, &n)) *span = (int16_t)(n / 100); }
    }
}

static void apply(const Ctx *C, CssStyle *s, const CssDecl *d, const char *v)
{
    const CssStyle *P = C->parent;
    int fs = s->font_size;
    const char *end = v + strlen(v);
    uint32_t col;
    bool inherit = istarts(v, "inherit");
    bool initial = istarts(v, "initial") || istarts(v, "unset") || istarts(v, "revert");
    switch (d->prop) {
    case P_DISPLAY: {
        if (inherit) { s->display = P->display; break; }
        static const struct { const char *k; uint8_t d; } m[] = {
            {"none", DISP_NONE}, {"block", DISP_BLOCK}, {"inline-block", DISP_INLINE_BLOCK},
            {"inline-flex", DISP_INLINE_FLEX}, {"inline-grid", DISP_INLINE_BLOCK}, {"inline-table", DISP_INLINE_BLOCK},
            {"inline", DISP_INLINE}, {"list-item", DISP_LIST_ITEM}, {"flex", DISP_FLEX}, {"grid", DISP_GRID},
            {"table-row-group", DISP_TABLE_ROW_GROUP}, {"table-header-group", DISP_TABLE_ROW_GROUP},
            {"table-footer-group", DISP_TABLE_ROW_GROUP}, {"table-row", DISP_TABLE_ROW},
            {"table-cell", DISP_TABLE_CELL}, {"table-caption", DISP_TABLE_CAPTION},
            {"table-column-group", DISP_NONE}, {"table-column", DISP_NONE}, {"table", DISP_TABLE},
            {"contents", DISP_CONTENTS}, {"flow-root", DISP_BLOCK}, {"-webkit-box", DISP_BLOCK},
            {"-ms-flexbox", DISP_FLEX}, {"-webkit-flex", DISP_FLEX}, {"run-in", DISP_BLOCK},
        };
        for (unsigned i = 0; i < ARRAY_SIZE(m); i++) {
            size_t l = strlen(m[i].k);
            if (istarts(v, m[i].k) && !ident_char(v[l])) { s->display = m[i].d; break; }
        }
        if (istarts(v, "block flex")) s->display = DISP_FLEX;
        if (istarts(v, "inline flex")) s->display = DISP_INLINE_FLEX;
        break;
    }
    case P_POSITION:
        if (istarts(v, "absolute")) s->position = POS_ABSOLUTE;
        else if (istarts(v, "fixed")) s->position = POS_FIXED;
        else if (istarts(v, "relative")) s->position = POS_RELATIVE;
        else if (istarts(v, "sticky") || istarts(v, "-webkit-sticky")) s->position = POS_STICKY;
        else s->position = POS_STATIC;
        break;
    case P_FLOAT:
        s->float_ = istarts(v, "left") || istarts(v, "inline-start") ? FLOAT_LEFT :
                    istarts(v, "right") || istarts(v, "inline-end") ? FLOAT_RIGHT : FLOAT_NONE;
        break;
    case P_VISIBILITY:
        if (inherit) s->invisible = P->invisible;
        else s->invisible = istarts(v, "hidden") || istarts(v, "collapse");
        break;
    case P_CONTENT_VISIBILITY:
        if (istarts(v, "hidden")) s->display = DISP_NONE;
        break;
    case P_COLOR:
        if (inherit || initial) { s->color = initial ? 0xFF000000u : P->color; break; }
        if (istarts(v, "currentcolor")) break;
        if (parse_color_n(v, (size_t)(end - v), &col)) s->color = col;
        break;
    case P_BACKGROUND_COLOR:
        if (istarts(v, "currentcolor")) { s->bg = s->color; break; }
        if (inherit) { s->bg = P->bg; break; }
        if (parse_color_n(v, (size_t)(end - v), &col)) s->bg = col;
        break;
    case P_BACKGROUND: {
        if (istarts(v, "none") || istarts(v, "transparent") || initial) { s->bg = 0; s->bg_image = NULL; break; }
        if (inherit) { s->bg = P->bg; break; }
        /* shorthand resets the longhands, then each token sets one of them */
        s->bg = 0;
        s->bg_image = NULL;
        s->bg_size = BG_AUTO;
        s->bg_repeat = BG_REPEAT;
        s->bg_px[0] = s->bg_px[1] = s->bg_pct[0] = s->bg_pct[1] = 0;
        int bg_axis = 0;
        const char *p = v;
        while (p < end) {
            p = skip_ws(p, end);
            const char *t = p;
            while (p < end && !ws(*p) && *p != ',' && (*p != '/' || p == t)) { if (*p == '(') { p = skip_group(p, end); continue; } p++; }
            if (*t == ',') { /* later layers: only their colour matters (the last layer's) */ p = t + 1; continue; }
            if (istarts(t, "url(")) {
                if (!s->bg_image) s->bg_image = bg_url(C, d, t, p);
            } else if (istarts(t, "no-repeat")) { s->bg_repeat = BG_NO_REPEAT;
            } else if (istarts(t, "repeat-x")) { s->bg_repeat = BG_REPEAT_X;
            } else if (istarts(t, "repeat-y")) { s->bg_repeat = BG_REPEAT_Y;
            } else if (istarts(t, "repeat")) { s->bg_repeat = BG_REPEAT;
            } else if (*t == '/') {
                const char *z = skip_ws(t + 1, end);
                const char *ze = z;
                while (ze < end && *ze != ',' && !(istarts(ze, "no-repeat") || istarts(ze, "repeat"))) ze++;
                if (z == t + 1 && p == t + 1) { /* "/" alone: size follows as the next token(s) */ }
                bg_size_value(C, s, z, ze, fs);
                p = ze;
            } else if (bg_axis < 3 && bg_position_token(C, s, t, p, &bg_axis, fs)) {
            } else if (istarts(t, "linear-gradient") || istarts(t, "radial-gradient") ||
                       istarts(t, "-webkit-linear-gradient")) {
                /* use the first colour stop as a flat colour */
                const char *g = t;
                while (g < p && *g != '(') g++;
                g++;
                while (g < p) {
                    g = skip_ws(g, p);
                    const char *ge = g;
                    while (ge < p && *ge != ',' && *ge != ')') { if (*ge == '(') { ge = skip_group(ge, p); continue; } ge++; }
                    const char *ce = ge;
                    /* strip a trailing stop position */
                    const char *sp = g;
                    while (sp < ce && !ws(*sp)) { if (*sp == '(') { sp = skip_group(sp, ce); continue; } sp++; }
                    if (parse_color_n(g, (size_t)(sp - g), &col)) { s->bg = col; break; }
                    g = ge + 1;
                }
            } else if (parse_color_n(t, (size_t)(p - t), &col)) {
                s->bg = col;
            }
            if (p < end && *p == ',') p++;
        }
        break;
    }
    case P_BACKGROUND_IMAGE:
        if (istarts(v, "url(")) {
            s->bg_image = bg_url(C, d, v, end);
        } else if (istarts(v, "none")) {
            s->bg_image = NULL;
        } else if (istarts(v, "linear-gradient") && !(s->bg >> 24)) {
            const char *g = v;
            while (g < end && *g != '(') g++;
            g++;
            while (g < end) {
                g = skip_ws(g, end);
                const char *ge = g;
                while (ge < end && *ge != ',' && *ge != ')') { if (*ge == '(') { ge = skip_group(ge, end); continue; } ge++; }
                const char *sp = g;
                while (sp < ge && !ws(*sp)) { if (*sp == '(') { sp = skip_group(sp, ge); continue; } sp++; }
                if (parse_color_n(g, (size_t)(sp - g), &col)) { s->bg = col; break; }
                g = ge + 1;
            }
        }
        break;
    case P_FONT:
        if (inherit) break;
        apply_font_shorthand(C, s, v, P->font_size);
        break;
    case P_FONT_SIZE:
        break;                  /* applied in the font pass */
    case P_FONT_WEIGHT: {
        if (inherit) { s->font_weight = P->font_weight; break; }
        int w = weight_value(v, P->font_weight);
        if (w > 0) s->font_weight = (uint16_t)w;
        break;
    }
    case P_FONT_STYLE:
        s->italic = inherit ? P->italic : (istarts(v, "italic") || istarts(v, "oblique"));
        break;
    case P_FONT_FAMILY:
        s->mono = inherit ? P->mono : family_is_mono(v);
        break;
    case P_LINE_HEIGHT:
        if (inherit) { s->line_height = P->line_height; break; }
        set_line_height(C, s, v);
        break;
    case P_TEXT_ALIGN:
        if (inherit) { s->text_align = P->text_align; s->center_blocks = P->center_blocks; break; }
        s->center_blocks = istarts(v, "-webkit-center") || istarts(v, "-moz-center");
        if (istarts(v, "center") || istarts(v, "-webkit-center") || istarts(v, "-moz-center")) s->text_align = ALIGN_CENTER;
        else if (istarts(v, "right") || istarts(v, "end") || istarts(v, "-webkit-right")) s->text_align = ALIGN_RIGHT;
        else if (istarts(v, "justify")) s->text_align = ALIGN_JUSTIFY;
        else s->text_align = ALIGN_LEFT;
        break;
    case P_TEXT_DECORATION:
    case P_TEXT_DECORATION_LINE:
        s->decoration = 0;
        for (const char *p = v; *p; p++) {
            if (istarts(p, "underline")) s->decoration |= DECO_UNDERLINE;
            if (istarts(p, "line-through")) s->decoration |= DECO_LINE_THROUGH;
            if (istarts(p, "overline")) s->decoration |= DECO_OVERLINE;
        }
        break;
    case P_TEXT_TRANSFORM:
        s->transform = istarts(v, "uppercase") ? TT_UPPER : istarts(v, "lowercase") ? TT_LOWER :
                       istarts(v, "capitalize") ? TT_CAPITALIZE : inherit ? P->transform : TT_NONE;
        break;
    case P_WHITE_SPACE:
        if (inherit) { s->white_space = P->white_space; break; }
        s->white_space = istarts(v, "pre-wrap") || istarts(v, "break-spaces") ? WS_PRE_WRAP :
                         istarts(v, "pre-line") ? WS_PRE_LINE : istarts(v, "pre") ? WS_PRE :
                         istarts(v, "nowrap") ? WS_NOWRAP : WS_NORMAL;
        break;
    case P_TEXT_WRAP:
        if (istarts(v, "nowrap")) s->white_space = WS_NOWRAP;
        break;
    case P_TEXT_INDENT: {
        CssLen l;
        if (parse_len(C, v, end, fs, &l) && l.kind == LEN_SET && !l.pct) s->text_indent = (int16_t)l.px;
        break;
    }
    case P_MARGIN: box4(C, v, s->margin, fs, true); break;
    case P_MARGIN_T: case P_MARGIN_BLOCK_START: parse_len(C, v, end, fs, &s->margin[0]); break;
    case P_MARGIN_R: case P_MARGIN_INLINE_END: parse_len(C, v, end, fs, &s->margin[1]); break;
    case P_MARGIN_B: case P_MARGIN_BLOCK_END: parse_len(C, v, end, fs, &s->margin[2]); break;
    case P_MARGIN_L: case P_MARGIN_INLINE_START: parse_len(C, v, end, fs, &s->margin[3]); break;
    case P_MARGIN_INLINE: box2(C, v, &s->margin[3], &s->margin[1], fs); break;
    case P_MARGIN_BLOCK: box2(C, v, &s->margin[0], &s->margin[2], fs); break;
    case P_PADDING: box4(C, v, s->padding, fs, false); break;
    case P_PADDING_T: case P_PADDING_BLOCK_START: parse_len(C, v, end, fs, &s->padding[0]); break;
    case P_PADDING_R: case P_PADDING_INLINE_END: parse_len(C, v, end, fs, &s->padding[1]); break;
    case P_PADDING_B: case P_PADDING_BLOCK_END: parse_len(C, v, end, fs, &s->padding[2]); break;
    case P_PADDING_L: case P_PADDING_INLINE_START: parse_len(C, v, end, fs, &s->padding[3]); break;
    case P_PADDING_INLINE: box2(C, v, &s->padding[3], &s->padding[1], fs); break;
    case P_PADDING_BLOCK: box2(C, v, &s->padding[0], &s->padding[2], fs); break;
    case P_BORDER: border_short(C, s, -1, v, fs); break;
    case P_BORDER_T: border_short(C, s, 0, v, fs); break;
    case P_BORDER_R: border_short(C, s, 1, v, fs); break;
    case P_BORDER_B: border_short(C, s, 2, v, fs); break;
    case P_BORDER_L: border_short(C, s, 3, v, fs); break;
    case P_BORDER_WIDTH: {
        const char *ts[4], *te[4];
        int n = split_tokens(v, end, ts, te, 4);
        int w[4];
        bool ok;
        for (int i = 0; i < n; i++) { w[i] = border_width_kw(C, ts[i], te[i], fs, &ok); if (!ok) return; }
        int idx[4][4] = {{0, 0, 0, 0}, {0, 1, 0, 1}, {0, 1, 2, 1}, {0, 1, 2, 3}};
        if (n >= 1 && n <= 4) for (int k = 0; k < 4; k++) if (s->border_w[k] || 1) s->border_w[k] = (int16_t)w[idx[n - 1][k]];
        break;
    }
    case P_BORDER_COLOR: {
        const char *ts[4], *te[4];
        int n = split_tokens(v, end, ts, te, 4);
        uint32_t c[4];
        for (int i = 0; i < n; i++) if (!parse_color_n(ts[i], (size_t)(te[i] - ts[i]), &c[i])) c[i] = s->color;
        int idx[4][4] = {{0, 0, 0, 0}, {0, 1, 0, 1}, {0, 1, 2, 1}, {0, 1, 2, 3}};
        if (n >= 1 && n <= 4) for (int k = 0; k < 4; k++) s->border_c[k] = c[idx[n - 1][k]];
        break;
    }
    case P_BORDER_STYLE: {
        const char *ts[4], *te[4];
        int n = split_tokens(v, end, ts, te, 4);
        int idx[4][4] = {{0, 0, 0, 0}, {0, 1, 0, 1}, {0, 1, 2, 1}, {0, 1, 2, 3}};
        if (n >= 1 && n <= 4)
            for (int k = 0; k < 4; k++) {
                const char *t = ts[idx[n - 1][k]];
                if (istarts(t, "none") || istarts(t, "hidden")) s->border_w[k] = 0;
                else if (!s->border_w[k]) s->border_w[k] = 3;
            }
        break;
    }
    case P_BORDER_T_WIDTH: case P_BORDER_R_WIDTH: case P_BORDER_B_WIDTH: case P_BORDER_L_WIDTH: {
        bool ok;
        int w = border_width_kw(C, v, end, fs, &ok);
        if (ok) s->border_w[d->prop - P_BORDER_T_WIDTH] = (int16_t)w;
        break;
    }
    case P_BORDER_T_COLOR: case P_BORDER_R_COLOR: case P_BORDER_B_COLOR: case P_BORDER_L_COLOR:
        if (parse_color_n(v, (size_t)(end - v), &col)) s->border_c[d->prop - P_BORDER_T_COLOR] = col;
        break;
    case P_BORDER_T_STYLE: case P_BORDER_R_STYLE: case P_BORDER_B_STYLE: case P_BORDER_L_STYLE: {
        int k = d->prop - P_BORDER_T_STYLE;
        if (istarts(v, "none") || istarts(v, "hidden")) s->border_w[k] = 0;
        else if (!s->border_w[k]) s->border_w[k] = 3;
        break;
    }
    case P_BORDER_RADIUS: {
        CssLen l;
        const char *ts[1], *te[1];
        if (split_tokens(v, end, ts, te, 1) == 1 && parse_len(C, ts[0], te[0], fs, &l) && l.kind == LEN_SET) {
            int r = l.px;
            if (l.pct >= 5000) r = 9999;                    /* 50%: a circle/capsule */
            s->radius = (int16_t)(r < 0 ? 0 : r > 9999 ? 9999 : r);
        }
        break;
    }
    case P_WIDTH: parse_len(C, v, end, fs, &s->width); break;
    case P_HEIGHT: parse_len(C, v, end, fs, &s->height); break;
    case P_MIN_WIDTH: if (!parse_len(C, v, end, fs, &s->min_w) || s->min_w.kind != LEN_SET) { s->min_w.kind = LEN_SET; s->min_w.px = s->min_w.pct = 0; } break;
    case P_MAX_WIDTH: parse_len(C, v, end, fs, &s->max_w); if (s->max_w.kind == LEN_AUTO) s->max_w.kind = LEN_NONE; break;
    case P_MIN_HEIGHT: if (!parse_len(C, v, end, fs, &s->min_h) || s->min_h.kind != LEN_SET) { s->min_h.kind = LEN_SET; s->min_h.px = s->min_h.pct = 0; } break;
    case P_MAX_HEIGHT: parse_len(C, v, end, fs, &s->max_h); if (s->max_h.kind == LEN_AUTO) s->max_h.kind = LEN_NONE; break;
    case P_BOX_SIZING: s->border_box = istarts(v, "border-box"); break;
    case P_LIST_STYLE: {
        const char *ts[3], *te[3];
        int n = split_tokens(v, end, ts, te, 3);
        for (int i = 0; i < n; i++) { uint8_t t = list_type(ts[i]); if (t != 255) s->list_style = t; }
        break;
    }
    case P_LIST_STYLE_TYPE: {
        uint8_t t = list_type(v);
        if (inherit) s->list_style = P->list_style;
        else if (t != 255) s->list_style = t;
        else if (*v == '"' || *v == '\'') s->list_style = LIST_NONE;     /* custom string markers */
        break;
    }
    case P_FLEX: {
        if (istarts(v, "none")) { s->flex_grow = 0; s->flex_shrink = 0; break; }
        if (istarts(v, "auto")) { s->flex_grow = 100; s->flex_shrink = 100; s->flex_basis.kind = LEN_AUTO; break; }
        const char *ts[3], *te[3];
        int n = split_tokens(v, end, ts, te, 3);
        int nums = 0;
        for (int i = 0; i < n; i++) {
            const char *q = ts[i];
            int32_t num;
            if (parse_num(&q, te[i], &num) && q == te[i]) {
                if (nums == 0) { s->flex_grow = (int16_t)(num > 30000 ? 30000 : num); s->flex_basis.kind = LEN_SET; s->flex_basis.px = 0; s->flex_basis.pct = 0; }
                else s->flex_shrink = (int16_t)(num > 30000 ? 30000 : num);
                nums++;
            } else {
                parse_len(C, ts[i], te[i], fs, &s->flex_basis);
            }
        }
        break;
    }
    case P_FLEX_DIRECTION: s->flex_dir_col = istarts(v, "column"); break;
    case P_FLEX_WRAP: s->flex_wrap = istarts(v, "wrap"); break;
    case P_FLEX_FLOW:
        s->flex_dir_col = istarts(v, "column");
        for (const char *p = v; *p; p++) if (istarts(p, "wrap") && (p == v || ws(p[-1]))) s->flex_wrap = 1;
        break;
    case P_FLEX_GROW: { const char *q = v; int32_t num; if (parse_num(&q, end, &num)) s->flex_grow = (int16_t)(num > 30000 ? 30000 : num); break; }
    case P_FLEX_SHRINK: { const char *q = v; int32_t num; if (parse_num(&q, end, &num)) s->flex_shrink = (int16_t)(num > 30000 ? 30000 : num); break; }
    case P_FLEX_BASIS: parse_len(C, v, end, fs, &s->flex_basis); break;
    case P_JUSTIFY_CONTENT: { uint8_t k = flex_kw(v); if (k != 255) s->justify = k; break; }
    case P_ALIGN_ITEMS: { uint8_t k = flex_kw(v); if (k != 255) s->align_items = k; break; }
    case P_ALIGN_SELF: { uint8_t k = flex_kw(v); if (k != 255 && !istarts(v, "auto")) s->align_self = k; break; }
    case P_PLACE_ITEMS: { uint8_t k = flex_kw(v); if (k != 255) s->align_items = k; break; }
    case P_PLACE_CONTENT: { uint8_t k = flex_kw(v); if (k != 255) s->justify = k; break; }
    case P_GAP: {
        CssLen a = {0, 0, LEN_SET}, b = {0, 0, LEN_SET};
        box2(C, v, &a, &b, fs);
        s->gap_row = (int16_t)(a.kind == LEN_SET ? a.px : 0);
        s->gap_col = (int16_t)(b.kind == LEN_SET ? b.px : 0);
        break;
    }
    case P_ROW_GAP: { CssLen l; if (parse_len(C, v, end, fs, &l) && l.kind == LEN_SET) s->gap_row = (int16_t)l.px; break; }
    case P_COLUMN_GAP: { CssLen l; if (parse_len(C, v, end, fs, &l) && l.kind == LEN_SET) s->gap_col = (int16_t)l.px; break; }
    case P_ORDER: { const char *q = v; int32_t num; if (parse_num(&q, end, &num)) s->order = (int16_t)(num / 100); break; }
    case P_OVERFLOW: case P_OVERFLOW_X: case P_OVERFLOW_Y:
        if (istarts(v, "hidden") || istarts(v, "clip")) s->clip = 1;
        else if (istarts(v, "auto") || istarts(v, "scroll")) s->clip = 2;
        break;
    case P_OPACITY: {
        const char *q = v;
        int32_t num;
        if (parse_num(&q, end, &num)) { if (q < end && *q == '%') num /= 100; if (num <= 1) s->invisible = 1; }
        break;
    }
    case P_CLIP:
        if (istarts(v, "rect(0") || istarts(v, "rect(1px")) s->invisible = 1;
        break;
    case P_CLIP_PATH:
        if (istarts(v, "inset(50%") || istarts(v, "inset(100%") || istarts(v, "circle(0")) s->invisible = 1;
        break;
    case P_TRANSFORM: {
        /* Off-canvas drawers and collapsed popups: translate by a whole box
         * (+-100%) or scale to zero. The box is not drawn (its space stays). */
        bool hide = false;
        for (const char *q = v; *q && !hide; q++) {
            if (istarts(q, "scale(0") || istarts(q, "scalex(0") || istarts(q, "scaley(0")) {
                const char *r = q;
                while (*r && *r != '(') r++;
                r++;
                while (ws(*r)) r++;
                if (*r == '0' && (r[1] == ')' || r[1] == ',' || ws(r[1]))) hide = true;
            }
            if (istarts(q, "translate")) {
                const char *r = q;
                while (*r && *r != '(') r++;
                for (; *r && *r != ')'; r++) if (istarts(r, "100%") && (r == q || r[-1] == '-' || r[-1] == '(' || ws(r[-1]) || r[-1] == ',')) hide = true;
            }
        }
        if (hide) s->invisible = 1;
        break;
    }
    case P_VERTICAL_ALIGN:
        s->valign = istarts(v, "middle") ? VA_MIDDLE : istarts(v, "top") || istarts(v, "text-top") ? VA_TOP :
                    istarts(v, "bottom") || istarts(v, "text-bottom") ? VA_BOTTOM : istarts(v, "sub") ? VA_SUB :
                    istarts(v, "super") ? VA_SUPER : VA_BASELINE;
        break;
    case P_TOP: parse_len(C, v, end, fs, &s->inset[0]); break;
    case P_RIGHT: parse_len(C, v, end, fs, &s->inset[1]); break;
    case P_BOTTOM: parse_len(C, v, end, fs, &s->inset[2]); break;
    case P_LEFT: parse_len(C, v, end, fs, &s->inset[3]); break;
    case P_INSET: box4(C, v, s->inset, fs, true); break;
    case P_TABLE_LAYOUT: s->table_fixed = istarts(v, "fixed"); break;
    case P_MASK: s->masked = !istarts(v, "none"); break;
    case P_BACKGROUND_SIZE: bg_size_value(C, s, v, end, fs); break;
    case P_BACKGROUND_REPEAT:
        s->bg_repeat = istarts(v, "no-repeat") ? BG_NO_REPEAT : istarts(v, "repeat-x") ? BG_REPEAT_X :
                       istarts(v, "repeat-y") ? BG_REPEAT_Y : BG_REPEAT;
        break;
    case P_BACKGROUND_POSITION: {
        const char *ts[4], *te[4];
        int n = split_tokens(v, end, ts, te, 4);
        int axis = 0;
        s->bg_px[0] = s->bg_px[1] = s->bg_pct[0] = s->bg_pct[1] = 0;
        for (int i = 0; i < n; i++) bg_position_token(C, s, ts[i], te[i], &axis, fs);
        break;
    }
    case P_CONTENT: s->content = dup_ident(C->S->a, v, strlen(v), false); break;
    case P_CLEAR:
        s->clear = istarts(v, "both") ? 3 : istarts(v, "left") || istarts(v, "inline-start") ? 1 :
                   istarts(v, "right") || istarts(v, "inline-end") ? 2 : 0;
        break;
    case P_BORDER_SPACING: { CssLen l; if (parse_len(C, v, end, fs, &l) && l.kind == LEN_SET) s->border_spacing = (int16_t)l.px; break; }
    case P_GRID_TEMPLATE_COLUMNS:
        if (istarts(v, "none") || istarts(v, "subgrid") || istarts(v, "masonry")) { if (s->grid) s->grid->n = 0; s->grid_cols = 0; break; }
        parse_tracks(C, s, v, fs);
        break;
    case P_GRID_TEMPLATE: {
        /* "areas.../ columns" or "rows / columns": take the part after '/' */
        const char *slash = NULL;
        int depth = 0;
        for (const char *q = v; *q; q++) { if (*q == '(') depth++; else if (*q == ')') depth--; else if (*q == '/' && !depth) slash = q; }
        if (slash) parse_tracks(C, s, slash + 1, fs);
        if (strchr_local(v, '"')) parse_areas(C, s, v);
        break;
    }
    case P_GRID_TEMPLATE_AREAS:
        if (istarts(v, "none")) { if (s->grid) s->grid->narea = 0; break; }
        parse_areas(C, s, v);
        break;
    case P_GRID_COLUMN: {
        const char *slash = strchr_local(v, '/');
        grid_line(v, slash ? slash : end, &s->gc_start, &s->gc_span);
        if (slash) { int16_t sp = 0; grid_line(slash + 1, end, &s->gc_end, &sp); if (sp) { s->gc_span = sp; s->gc_end = 0; } }
        else s->gc_end = 0;
        break;
    }
    case P_GRID_COLUMN_START: grid_line(v, end, &s->gc_start, &s->gc_span); break;
    case P_GRID_COLUMN_END: { int16_t sp = 0; grid_line(v, end, &s->gc_end, &sp); if (sp) { s->gc_span = sp; s->gc_end = 0; } break; }
    case P_GRID_ROW: case P_GRID_ROW_START: {
        const char *slash = strchr_local(v, '/');
        grid_line(v, slash ? slash : end, &s->gr_start, &s->gr_span);
        break;
    }
    case P_GRID_AREA: {
        const char *q = v;
        while (*q && *q != '/' && *q != ' ') q++;
        bool numeric = (*v >= '0' && *v <= '9') || *v == '-' || istarts(v, "span");
        if (!numeric && !istarts(v, "auto")) { s->grid_area = dup_ident(C->S->a, v, (size_t)(q - v), false); break; }
        /* row-start / column-start / row-end / column-end */
        const char *parts[4] = {v, NULL, NULL, NULL};
        int np = 1;
        for (const char *c = v; *c && np < 4; c++) if (*c == '/') parts[np++] = c + 1;
        grid_line(parts[0], np > 1 ? parts[1] - 1 : end, &s->gr_start, &s->gr_span);
        if (np > 1) grid_line(parts[1], np > 2 ? parts[2] - 1 : end, &s->gc_start, &s->gc_span);
        if (np > 3) { int16_t sp = 0; grid_line(parts[3], end, &s->gc_end, &sp); if (sp) { s->gc_span = sp; s->gc_end = 0; } }
        break;
    }
    default:
        break;
    }
}

/* Presentational attributes (bgcolor, align, width, color ...): author-level
 * hints applied before any author rule. */
static void presentational(const Ctx *C, CssStyle *s, const DomNode *n)
{
    const char *v;
    uint32_t col;
    if ((v = dom_attr(n, "bgcolor")) && parse_color_n(v, strlen(v), &col)) s->bg = col;
    if (n->tag == TAG_FONT || n->tag == TAG_BODY) {
        if ((v = dom_attr(n, n->tag == TAG_BODY ? "text" : "color")) && parse_color_n(v, strlen(v), &col)) s->color = col;
    }
    if (n->tag == TAG_FONT && (v = dom_attr(n, "size"))) {
        static const int sizes[] = {10, 13, 16, 18, 24, 32, 48};
        int k = 0;
        bool rel = *v == '+' || *v == '-';
        int sign = *v == '-' ? -1 : 1;
        for (const char *c = rel ? v + 1 : v; *c >= '0' && *c <= '9' && k < 10; c++) k = k * 10 + (*c - '0');
        int idx = rel ? 3 + sign * k : k;
        if (idx < 1) idx = 1;
        if (idx > 7) idx = 7;
        s->font_size = (int16_t)(sizes[idx - 1] * C->S->media.zoom / 100);
    }
    if (n->tag == TAG_FONT && (v = dom_attr(n, "face"))) s->mono = family_is_mono(v);
    if ((v = dom_attr(n, "align"))) {
        if (n->tag == TAG_TABLE || n->tag == TAG_IMG) {
            if (istarts(v, "center")) { s->margin[1].kind = LEN_AUTO; s->margin[3].kind = LEN_AUTO; }
            else if (istarts(v, "right")) s->float_ = FLOAT_RIGHT;
            else if (istarts(v, "left")) s->float_ = FLOAT_LEFT;
        } else {
            if (istarts(v, "center") || istarts(v, "middle")) { s->text_align = ALIGN_CENTER; s->center_blocks = 1; }
            else if (istarts(v, "right")) s->text_align = ALIGN_RIGHT;
            else if (istarts(v, "left")) s->text_align = ALIGN_LEFT;
            else if (istarts(v, "justify")) s->text_align = ALIGN_JUSTIFY;
        }
    }
    if ((v = dom_attr(n, "valign"))) s->valign = istarts(v, "top") ? VA_TOP : istarts(v, "bottom") ? VA_BOTTOM : VA_MIDDLE;
    if (n->tag == TAG_TABLE || n->tag == TAG_TD || n->tag == TAG_TH || n->tag == TAG_IMG || n->tag == TAG_COL ||
        n->tag == TAG_HR || n->tag == TAG_IFRAME || n->tag == TAG_VIDEO || n->tag == TAG_CANVAS || n->tag == TAG_SVG ||
        n->tag == TAG_EMBED || n->tag == TAG_OBJECT || n->tag == TAG_INPUT) {
        if ((v = dom_attr(n, "width")) && !(n->tag == TAG_INPUT)) {
            CssLen l;
            char tmp[32];
            size_t k = 0;
            while (v[k] && k < sizeof(tmp) - 3) { tmp[k] = v[k]; k++; }
            tmp[k] = '\0';
            bool digits_only = k > 0;
            for (size_t i = 0; i < k; i++) if (!(tmp[i] >= '0' && tmp[i] <= '9') && tmp[i] != '.') digits_only = false;
            if (digits_only) { tmp[k] = 'p'; tmp[k + 1] = 'x'; tmp[k + 2] = '\0'; }
            if (parse_len(C, tmp, tmp + strlen(tmp), s->font_size, &l) && l.kind == LEN_SET) s->width = l;
        }
        if ((v = dom_attr(n, "height")) && n->tag != TAG_TD && n->tag != TAG_TH && n->tag != TAG_TABLE && n->tag != TAG_INPUT) {
            CssLen l;
            char tmp[32];
            size_t k = 0;
            while (v[k] && k < sizeof(tmp) - 3) { tmp[k] = v[k]; k++; }
            tmp[k] = '\0';
            bool digits_only = k > 0;
            for (size_t i = 0; i < k; i++) if (!(tmp[i] >= '0' && tmp[i] <= '9') && tmp[i] != '.') digits_only = false;
            if (digits_only) { tmp[k] = 'p'; tmp[k + 1] = 'x'; tmp[k + 2] = '\0'; }
            if (parse_len(C, tmp, tmp + strlen(tmp), s->font_size, &l) && l.kind == LEN_SET && !l.pct) s->height = l;
        }
    }
    if (n->tag == TAG_TABLE) {
        if ((v = dom_attr(n, "border")) && *v && *v != '0') {
            for (int k = 0; k < 4; k++) { s->border_w[k] = 1; s->border_c[k] = 0xFF808080u; }
        }
        if ((v = dom_attr(n, "cellspacing"))) {
            int k = 0;
            for (const char *c = v; *c >= '0' && *c <= '9' && k < 100; c++) k = k * 10 + (*c - '0');
            s->border_spacing = (int16_t)k;
        }
    }
    if ((n->tag == TAG_TD || n->tag == TAG_TH) && n->parent) {
        /* cellpadding and table border come from the enclosing table */
        const DomNode *t = n->parent;
        for (int i = 0; i < 3 && t && t->tag != TAG_TABLE; i++) t = t->parent;
        if (t && t->tag == TAG_TABLE) {
            if ((v = dom_attr(t, "cellpadding"))) {
                int k = 0;
                for (const char *c = v; *c >= '0' && *c <= '9' && k < 100; c++) k = k * 10 + (*c - '0');
                for (int i = 0; i < 4; i++) { s->padding[i].kind = LEN_SET; s->padding[i].px = k; s->padding[i].pct = 0; }
            }
            if ((v = dom_attr(t, "border")) && *v && *v != '0')
                for (int i = 0; i < 4; i++) { s->border_w[i] = 1; s->border_c[i] = 0xFF808080u; }
        }
        if (dom_attr(n, "nowrap")) s->white_space = WS_NOWRAP;
    }
    if (n->tag == TAG_HR) {
        if ((v = dom_attr(n, "color")) && parse_color_n(v, strlen(v), &col)) { for (int k = 0; k < 4; k++) s->border_c[k] = col; s->bg = col; }
        if (dom_attr(n, "noshade")) for (int k = 0; k < 4; k++) s->border_c[k] = 0xFF808080u;
    }
    if (n->tag == TAG_UL || n->tag == TAG_OL || n->tag == TAG_LI) {
        if ((v = dom_attr(n, "type"))) {
            if (*v == '1') s->list_style = LIST_DECIMAL;
            else if (*v == 'a') s->list_style = LIST_LOWER_ALPHA;
            else if (*v == 'A') s->list_style = LIST_UPPER_ALPHA;
            else if (*v == 'i') s->list_style = LIST_LOWER_ROMAN;
            else if (*v == 'I') s->list_style = LIST_UPPER_ROMAN;
            else { uint8_t t = list_type(v); if (t != 255) s->list_style = t; }
        }
    }
    if (n->tag == TAG_BODY) {
        for (int k = 0; k < 4; k++) {
            const char *names[] = {"topmargin", "rightmargin", "bottommargin", "leftmargin"};
            if ((v = dom_attr(n, names[k]))) {
                int px = 0;
                for (const char *c = v; *c >= '0' && *c <= '9' && px < 1000; c++) px = px * 10 + (*c - '0');
                s->margin[k].kind = LEN_SET; s->margin[k].px = px; s->margin[k].pct = 0;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Cascade                                                             */
/* ------------------------------------------------------------------ */

typedef struct { const CssRule *rule; uint64_t key; } Matched;

static Matched matched[MAX_MATCHED];
static int nmatched;
static Matched pmatched[256];          /* ::before / ::after rules of the element */
static int npmatched;

static void consider(const CssSheetSet *S, int ri, const DomNode *n)
{
    const CssRule *r = &S->rules[ri];
    if (nmatched >= MAX_MATCHED) return;
    if (!match_selector(r->sel, r->sel->n - 1, n)) return;
    if (r->sel->pseudo_elem) {
        if (npmatched < (int)ARRAY_SIZE(pmatched)) {
            pmatched[npmatched].rule = r;
            pmatched[npmatched].key = ((uint64_t)r->origin << 62) | ((uint64_t)r->sel->spec << 32) | r->order;
            npmatched++;
        }
        return;
    }
    matched[nmatched].rule = r;
    matched[nmatched].key = ((uint64_t)r->origin << 62) | ((uint64_t)r->sel->spec << 32) | r->order;
    nmatched++;
}

static void sort_matched(void)
{
    /* Shell sort: few hundred entries at most. */
    for (int gap = nmatched / 2; gap > 0; gap /= 2)
        for (int i = gap; i < nmatched; i++) {
            Matched t = matched[i];
            int j = i;
            while (j >= gap && matched[j - gap].key > t.key) { matched[j] = matched[j - gap]; j -= gap; }
            matched[j] = t;
        }
}

static char value_buf[VALUE_MAX];

/* Apply one declaration (var() substituted). pass: 0 custom, 1 font-size, 2 rest. */
static void apply_decl(const Ctx *C, CssStyle *s, const CssDecl *d, int pass)
{
    if (pass == 0) {
        if (d->prop != P_CUSTOM) return;
        CssVar *v = (CssVar *)arena_alloc(C->S->a, sizeof(CssVar));
        if (!v) return;
        v->name = d->name;
        v->value = d->value;
        v->next = s->vars;
        s->vars = v;
        return;
    }
    if (d->prop == P_CUSTOM) return;
    if ((pass == 1) != (d->prop == P_FONT_SIZE || d->prop == P_FONT)) return;
    const char *v = d->value;
    bool has_var = false;
    for (const char *p = v; *p; p++) if (*p == 'v' && istarts(p, "var(")) { has_var = true; break; }
    if (has_var) {
        if (!substitute(s, v, value_buf, sizeof(value_buf), 0)) return;
        v = value_buf;
    }
    while (ws(*v)) v++;
    if (pass == 1) {
        if (d->prop == P_FONT) { if (!istarts(v, "inherit")) apply_font_shorthand(C, s, v, C->parent->font_size); return; }
        if (istarts(v, "inherit") || istarts(v, "initial") || istarts(v, "unset")) {
            s->font_size = istarts(v, "initial") ? (int16_t)(16 * C->S->media.zoom / 100) : C->parent->font_size;
            return;
        }
        int fs = font_size_value(C, v, C->parent->font_size);
        if (fs > 0) s->font_size = (int16_t)(fs > 200 ? 200 : fs < 4 ? 4 : fs);
        else if (fs == 0) s->font_size = 0;         /* font-size:0 hides text (icon tricks) */
        return;
    }
    if (d->prop == P_FONT) return;
    apply(C, s, d, v);
}

static void run_passes(const Ctx *C, CssStyle *s, const CssDecl *inline_decls, int ninline)
{
    for (int pass = 0; pass < 3; pass++) {
        for (int imp = 0; imp < 2; imp++) {
            for (int i = 0; i < nmatched; i++) {
                const CssRule *r = matched[i].rule;
                for (int k = 0; k < r->ndecls; k++) if (r->decls[k].important == imp) apply_decl(C, s, &r->decls[k], pass);
            }
            for (int k = 0; k < ninline; k++) if (inline_decls[k].important == imp) apply_decl(C, s, &inline_decls[k], pass);
        }
        if (pass == 1 && s->line_height && C->parent && s->font_size != C->parent->font_size && C->parent->font_size) {
            /* inherited unitless-ish line height scales with the font */
            s->line_height = (int16_t)((int64_t)s->line_height * s->font_size / C->parent->font_size);
        }
    }
}

/* Decode a 'content' value (strings with CSS escapes, attr(), quotes) to UTF-8. */
static size_t content_text(const char *v, const DomNode *n, char *out, size_t cap)
{
    size_t o = 0;
    const char *p = v;
    bool any = false;
    while (*p && o + 8 < cap) {
        while (ws(*p)) p++;
        if (!*p) break;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            any = true;
            while (*p && *p != q && o + 8 < cap) {
                if (*p == '\\') {
                    p++;
                    if (hexv(*p) >= 0) {
                        uint32_t cp = 0;
                        int k = 0;
                        while (k < 6 && hexv(*p) >= 0) { cp = cp * 16 + (uint32_t)hexv(*p); p++; k++; }
                        if (*p == ' ') p++;
                        if (cp == 0 || cp > 0x10FFFF) cp = 0xFFFD;
                        o += (size_t)web_utf8_encode(cp, out + o);
                    } else if (*p == '\n') {
                        p++;
                    } else if (*p) {
                        out[o++] = *p++;
                    }
                    continue;
                }
                out[o++] = *p++;
            }
            if (*p) p++;
            continue;
        }
        if (istarts(p, "attr(")) {
            p += 5;
            const char *e = p;
            while (*e && *e != ')') e++;
            char name[48];
            size_t k = (size_t)(e - p) < sizeof(name) - 1 ? (size_t)(e - p) : sizeof(name) - 1;
            memcpy(name, p, k);
            name[k] = '\0';
            while (k && ws(name[k - 1])) name[--k] = '\0';
            const char *a = dom_attr(n, name);
            for (; a && *a && o + 8 < cap; a++) out[o++] = *a;
            p = *e ? e + 1 : e;
            any = true;
            continue;
        }
        if (istarts(p, "open-quote")) { o += (size_t)web_utf8_encode(0x201C, out + o); p += 10; any = true; continue; }
        if (istarts(p, "close-quote")) { o += (size_t)web_utf8_encode(0x201D, out + o); p += 11; any = true; continue; }
        if (istarts(p, "none") || istarts(p, "normal")) return (size_t)-1;
        /* url(), counter(), image-set(): not rendered */
        const char *e = p;
        if (*e == '(' ) e++;
        while (*e && !ws(*e) && *e != '(') e++;
        if (*e == '(') { int d = 0; while (*e) { if (*e == '(') d++; else if (*e == ')' && --d == 0) { e++; break; } e++; } }
        if (e == p) e++;
        p = e;
        any = true;
    }
    out[o] = '\0';
    return any ? o : (size_t)-1;
}

static void sort_list(Matched *m, int n)
{
    for (int gap = n / 2; gap > 0; gap /= 2)
        for (int i = gap; i < n; i++) {
            Matched t = m[i];
            int j = i;
            while (j >= gap && m[j - gap].key > t.key) { m[j] = m[j - gap]; j -= gap; }
            m[j] = t;
        }
}

static DomNode *make_pseudo(CssSheetSet *S, DomNode *n, const CssStyle *es, int which, int root_fs)
{
    /* rules for this pseudo-element, in cascade order */
    int k = 0;
    for (int i = 0; i < npmatched; i++)
        if (pmatched[i].rule->sel->pseudo_elem == which && k < MAX_MATCHED) matched[k++] = pmatched[i];
    if (!k) return NULL;
    nmatched = k;
    sort_matched();
    CssStyle *ps = (CssStyle *)arena_alloc(S->a, sizeof(CssStyle));
    if (!ps) return NULL;
    inherit_from(ps, es);
    initial_values(ps);
    Ctx C = {es, ps, S, root_fs};
    run_passes(&C, ps, NULL, 0);
    if (!ps->content || ps->display == DISP_NONE) return NULL;
    static char text[512];
    size_t len = content_text(ps->content, n, text, sizeof(text));
    if (len == (size_t)-1) return NULL;
    DomNode *e = (DomNode *)arena_alloc(S->a, sizeof(DomNode));
    if (!e) return NULL;
    e->type = DOM_ELEMENT;
    e->tag = TAG_OTHER;
    e->name = which == 1 ? "::before" : "::after";
    e->parent = n;
    e->style = ps;
    if (len) {
        DomNode *t = (DomNode *)arena_alloc(S->a, sizeof(DomNode));
        char *copy = arena_strndup(S->a, text, len);
        if (t && copy) {
            t->type = DOM_TEXT;
            t->text = copy;
            t->text_len = (uint32_t)len;
            t->parent = e;
            t->style = ps;
            e->first = e->last = t;
        }
    }
    return e;
}

static void style_element(CssSheetSet *S, DomNode *n, const CssStyle *parent, int root_fs)
{
    CssStyle *s = (CssStyle *)arena_alloc(S->a, sizeof(CssStyle));
    if (!s) return;
    if (parent) inherit_from(s, parent);
    else { s->color = 0xFF000000u; s->font_size = (int16_t)(16 * (S->media.zoom > 0 ? S->media.zoom : 100) / 100); s->font_weight = 400; }
    initial_values(s);
    Ctx C = {parent ? parent : s, s, S, root_fs};
    nmatched = 0;
    npmatched = 0;
    n->pseudo_before = n->pseudo_after = NULL;
    for (int i = 0; i < S->universal_count; i++) consider(S, S->universal[i], n);
    if (n->id) { const HashEntry *e = bucket_find(S->by_id, n->id); if (e) for (int i = 0; i < e->count; i++) consider(S, e->rules[i], n); }
    for (int c = 0; c < n->nclasses; c++) {
        /* skip duplicate class names on the element */
        bool dup = false;
        for (int k = 0; k < c; k++) if (strcmp(n->classes[k], n->classes[c]) == 0) dup = true;
        if (dup) continue;
        const HashEntry *e = bucket_find(S->by_class, n->classes[c]);
        if (e) for (int i = 0; i < e->count; i++) consider(S, e->rules[i], n);
    }
    { const HashEntry *e = bucket_find(S->by_tag, n->name); if (e) for (int i = 0; i < e->count; i++) consider(S, e->rules[i], n); }
    sort_matched();
    /* UA rules first, then presentational hints, then author rules. */
    int ua = 0;
    while (ua < nmatched && matched[ua].rule->origin == 0) ua++;
    int saved = nmatched;
    nmatched = ua;
    run_passes(&C, s, NULL, 0);
    presentational(&C, s, n);
    nmatched = saved;
    memmove(matched, matched + ua, sizeof(Matched) * (size_t)(nmatched - ua));
    nmatched -= ua;
    const char *inl = dom_attr(n, "style");
    uint16_t ninline = 0;
    CssDecl *idecl = inl && *inl ? parse_decls(S, inl, inl + strlen(inl), &ninline) : NULL;
    run_passes(&C, s, idecl, ninline);
    if (s->display == DISP_NONE) s->invisible = 1;
    n->style = s;
    if (npmatched && s->display != DISP_NONE && n->tag != TAG_IMG && n->tag != TAG_INPUT && n->tag != TAG_SELECT &&
        n->tag != TAG_TEXTAREA && n->tag != TAG_BR && n->tag != TAG_HR) {
        sort_list(pmatched, npmatched);
        n->pseudo_before = make_pseudo(S, n, s, 1, root_fs);
        n->pseudo_after = make_pseudo(S, n, s, 2, root_fs);
    }
}

void css_style_tree(CssSheetSet *S, DomTree *T)
{
    if (!S || !T->root) return;
    if (!S->indexed) build_index(S);
    int root_fs = 16;
    /* Iterative pre-order walk; the parent's style is always computed first. */
    for (DomNode *n = T->root; n; ) {
        bool skip = false;
        if (n->type == DOM_ELEMENT) {
            const CssStyle *ps = n->parent ? n->parent->style : NULL;
            style_element(S, n, ps, root_fs);
            if (n == T->root && n->style) root_fs = n->style->font_size > 0 ? n->style->font_size : 16;
            /* Hidden subtrees and raw content are not styled further. */
            if (!n->style || n->style->display == DISP_NONE || n->tag == TAG_SVG || n->tag == TAG_MATH ||
                n->tag == TAG_SCRIPT || n->tag == TAG_STYLE || n->tag == TAG_TEMPLATE) {
                skip = true;
                /* descendants keep no stale pointers into an old cascade */
                for (DomNode *c = n->first; c; c = dom_next(c, n, false)) {
                    c->style = NULL;
                    c->pseudo_before = c->pseudo_after = NULL;
                }
            }
        } else {
            n->style = n->parent ? n->parent->style : NULL;
        }
        if (S->a->full) break;
        n = dom_next(n, T->root, skip);
    }
}
