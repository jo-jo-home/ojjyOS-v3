/*
 * ojjyOS v3 - Web engine: arena allocator and HTML document tree
 *
 * dom_parse() turns untrusted HTML into a tree of element and text nodes
 * (a tolerant subset of the HTML5 tree-construction rules: implied end
 * tags, void and raw-text elements, entities, UTF-8). Everything lives in a
 * caller-provided arena that is reset per page, so nothing is ever freed
 * individually and nothing large touches the kernel stack.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_WEB_DOM_H
#define _OJJY_WEB_DOM_H

#include "types.h"

/* ---- Arena ---- */
typedef struct {
    uint8_t *mem;
    size_t cap, used;
    bool full;                  /* an allocation failed (output truncated) */
} WebArena;

void arena_init(WebArena *a, void *mem, size_t cap);
void arena_reset(WebArena *a);
void *arena_alloc(WebArena *a, size_t n);          /* zeroed, 8-aligned; NULL when full */
char *arena_strndup(WebArena *a, const char *s, size_t n);

/* ---- Tags (known ones interned for speed; others are TAG_OTHER + name) ---- */
typedef enum {
    TAG_OTHER = 0,
    TAG_HTML, TAG_HEAD, TAG_BODY, TAG_TITLE, TAG_META, TAG_LINK, TAG_STYLE, TAG_SCRIPT, TAG_NOSCRIPT, TAG_BASE,
    TAG_DIV, TAG_SPAN, TAG_P, TAG_A, TAG_IMG, TAG_BR, TAG_HR, TAG_H1, TAG_H2, TAG_H3, TAG_H4, TAG_H5, TAG_H6,
    TAG_UL, TAG_OL, TAG_LI, TAG_DL, TAG_DT, TAG_DD, TAG_TABLE, TAG_THEAD, TAG_TBODY, TAG_TFOOT, TAG_TR, TAG_TD,
    TAG_TH, TAG_CAPTION, TAG_COLGROUP, TAG_COL, TAG_FORM, TAG_INPUT, TAG_BUTTON, TAG_SELECT, TAG_OPTION,
    TAG_OPTGROUP, TAG_TEXTAREA, TAG_LABEL, TAG_PRE, TAG_CODE, TAG_B, TAG_STRONG, TAG_I, TAG_EM, TAG_U, TAG_S,
    TAG_SMALL, TAG_BIG, TAG_SUB, TAG_SUP, TAG_BLOCKQUOTE, TAG_CENTER, TAG_FONT, TAG_SECTION, TAG_ARTICLE,
    TAG_HEADER, TAG_FOOTER, TAG_NAV, TAG_MAIN, TAG_ASIDE, TAG_FIGURE, TAG_FIGCAPTION, TAG_SVG, TAG_MATH,
    TAG_TEMPLATE, TAG_IFRAME, TAG_VIDEO, TAG_AUDIO, TAG_CANVAS, TAG_OBJECT, TAG_EMBED, TAG_PICTURE, TAG_SOURCE,
    TAG_DETAILS, TAG_SUMMARY, TAG_FIELDSET, TAG_LEGEND, TAG_ADDRESS, TAG_MENU, TAG_KBD, TAG_SAMP, TAG_TT,
    TAG_VAR, TAG_CITE, TAG_Q, TAG_ABBR, TAG_MARK, TAG_DEL, TAG_INS, TAG_STRIKE, TAG_WBR, TAG_AREA, TAG_MAP,
    TAG_TRACK, TAG_PARAM, TAG_XMP, TAG_LISTING, TAG_PLAINTEXT, TAG_DIALOG, TAG_HGROUP, TAG_SEARCH, TAG_TIME,
    TAG_DFN, TAG_NOBR,
    TAG_COUNT
} WebTag;

typedef struct {
    const char *name;           /* lower case */
    const char *value;          /* decoded UTF-8 */
} DomAttr;

typedef struct DomNode DomNode;
struct DomNode {
    uint8_t type;               /* DOM_ELEMENT / DOM_TEXT */
    uint8_t tag;                /* WebTag */
    uint16_t nattrs;
    uint16_t nclasses;
    uint16_t child_index;       /* 1-based among element siblings */
    int16_t field;              /* engine: form field index + 1 (0 = none) */
    int16_t image;              /* engine: image index + 1 (0 = none) */
    const char *name;           /* tag name (lower case) */
    DomAttr *attrs;
    const char *id;
    const char **classes;
    const char *text;           /* DOM_TEXT: UTF-8 */
    uint32_t text_len;
    DomNode *parent, *first, *last, *next, *prev;
    struct CssStyle *style;     /* computed style (css.c) */
    DomNode *pseudo_before, *pseudo_after;  /* ::before / ::after boxes (css.c, rebuilt per cascade) */
    void *box;                  /* layout scratch */
};

#define DOM_ELEMENT 1
#define DOM_TEXT    2
#define DOM_MAX_DEPTH 160

typedef struct {
    DomNode *root;              /* <html> */
    DomNode *head, *body;
    int node_count;
    bool truncated;
    bool quirks;                /* no <!DOCTYPE html>: legacy rendering rules */
} DomTree;

/* Parse len bytes of HTML into `tree` (nodes in arena). plain=true makes a
 * single <pre> with the text. latin1=true decodes bytes >= 0x80 as
 * Windows-1252/Latin-1 instead of UTF-8. */
void dom_parse(DomTree *tree, WebArena *arena, const char *html, size_t len, bool plain, bool latin1);

const char *dom_attr(const DomNode *n, const char *name);     /* NULL if absent */
bool dom_has_class(const DomNode *n, const char *cls);
WebTag dom_tag_id(const char *name, size_t len);
const char *dom_tag_name(WebTag tag);

/* Iteration helper: next node in document order (pre-order), staying under root. */
DomNode *dom_next(DomNode *n, DomNode *root, bool skip_children);

/* Decode one UTF-8 sequence (bounded). Returns bytes consumed (>= 1). */
size_t web_utf8_decode(const unsigned char *p, const unsigned char *end, uint32_t *cp);
/* Append the UTF-8 encoding of cp to out (needs 4 bytes). Returns bytes. */
int web_utf8_encode(uint32_t cp, char *out);
/* ASCII rendering for a code point the font cannot draw ("" to drop). */
const char *web_translit(uint32_t cp);

#endif /* _OJJY_WEB_DOM_H */
