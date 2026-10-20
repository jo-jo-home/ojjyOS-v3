/*
 * ojjyOS v3 - Web engine: page model and layout (the browser's renderer)
 *
 *   web_doc_parse()  HTML -> DOM, collects <style>, <link rel=stylesheet>,
 *                    title, base, favicon, refresh, form fields, images
 *   web_doc_add_sheet()  an external stylesheet arrived
 *   web_doc_layout() cascade (when needed) + layout for a viewport width,
 *                    producing a display list of WebItems in page pixels
 *
 * Text is measured through WebFontMetrics (the browser plugs in the Inter
 * web faces; host tests plug in a fake). All memory comes from three
 * caller-provided arenas; nothing is freed individually.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_WEB_ENGINE_H
#define _OJJY_WEB_ENGINE_H

#include "web/dom.h"
#include "web/css.h"

#define WEB_MAX_LINKS    4096
#define WEB_MAX_FIELDS   256
#define WEB_MAX_FORMS    32
#define WEB_MAX_ANCHORS  1024
#define WEB_MAX_IMAGES   384
#define WEB_MAX_SHEETS   40
#define WEB_URL_MAX      512

typedef struct {
    /* Font id for a CSS size (device px), weight and style. */
    uint8_t (*pick)(int px, int weight, bool italic, bool mono);
    int (*width)(const char *s, int len, uint8_t font);
    int (*ascent)(uint8_t font);
    int (*height)(uint8_t font);        /* default line box height ("normal") */
    bool (*has_glyph)(uint32_t cp);
} WebFontMetrics;

typedef enum {
    WI_RECT = 1,        /* filled (rounded) rectangle: color, radius */
    WI_STROKE,          /* rounded outline: color, ref = width */
    WI_TEXT,            /* text run: font, color, deco */
    WI_IMAGE,           /* ref = image index */
    WI_FIELD,           /* ref = field index (browser draws the control) */
    WI_CLIP,            /* push clip rect */
    WI_UNCLIP,          /* pop clip */
    WI_MARKER,          /* list bullet: ref = list style */
    WI_BGIMAGE,         /* CSS background image: ref = image, see bg_* */
} WebItemKind;

typedef struct {
    uint8_t kind;
    uint8_t font;
    uint8_t deco;
    uint8_t radius;
    int16_t link;               /* -1 = none */
    int16_t ref;
    int32_t x, y, w, h;
    uint32_t color;             /* 0xAARRGGBB */
    const char *text;
    uint32_t len;
    /* WI_BGIMAGE: size mode/repeat (css.h BG_*), explicit size, position */
    uint8_t bg_size, bg_repeat;
    int16_t bg_w, bg_h, bg_px[2], bg_pct[2];
} WebItem;

typedef enum { FIELD_TEXT, FIELD_PASSWORD, FIELD_HIDDEN, FIELD_SUBMIT, FIELD_BUTTON, FIELD_CHECKBOX, FIELD_RADIO,
               FIELD_SELECT, FIELD_TEXTAREA, FIELD_RESET } WebFieldKind;

typedef struct {
    uint8_t kind;
    int8_t form;
    bool checked;
    bool disabled;
    int16_t selected;           /* select: option index */
    int16_t option_count;
    char name[64];
    char value[512];
    char placeholder[96];
    char label[96];             /* button text */
    const char **options;       /* select: option labels (arena) */
    const char **option_values;
    DomNode *node;
    uint8_t font;               /* from the last layout */
    uint32_t color;
} WebField;

typedef struct {
    const char *action;
    bool post;
    bool multipart;
} WebForm;

typedef struct {
    const char *name;
    int32_t y;
} WebAnchor;

enum { IMG_PENDING = 0, IMG_LOADING, IMG_READY, IMG_FAILED };

typedef struct {
    char url[WEB_URL_MAX];      /* as written in the page (resolve against the base) */
    int16_t w, h;               /* natural size (0 = unknown) */
    int16_t pw, ph;             /* size of `pixels` (may be downscaled) */
    uint8_t state;
    const uint32_t *pixels;     /* owned by the browser's image cache */
} WebImage;

typedef struct {
    const char *text;           /* inline <style> or external sheet text */
    size_t len;
    bool external;
    const char *media;          /* media attribute (NULL = all) */
    const char *base;           /* directory URL for relative url() (NULL = the page) */
} WebSheetSrc;

typedef struct WebDoc {
    WebArena *dom_arena, *css_arena, *layout_arena;
    const WebFontMetrics *fm;
    DomTree tree;
    CssSheetSet *css;
    /* head information */
    char title[160];
    char base[WEB_URL_MAX];
    char icon[WEB_URL_MAX];
    char refresh_url[WEB_URL_MAX];
    int refresh_secs;           /* -1 = none */
    int script_bytes, script_count;
    bool needs_js;              /* the page is (nearly) empty without JavaScript */
    /* stylesheets */
    WebSheetSrc sheets[WEB_MAX_SHEETS];
    int sheet_count;
    char sheet_urls[WEB_MAX_SHEETS][WEB_URL_MAX];       /* requested, in order */
    int sheet_url_count;
    bool css_dirty;
    int styled_width, styled_zoom;
    bool styled_dark, dark;
    /* forms */
    WebField fields[WEB_MAX_FIELDS];
    int field_count;
    WebForm forms[WEB_MAX_FORMS];
    int form_count;
    /* images */
    WebImage images[WEB_MAX_IMAGES];
    int image_count;
    /* layout output */
    WebItem *items;
    int item_count, item_cap;
    const char *links[WEB_MAX_LINKS];
    int link_count;
    WebAnchor anchors[WEB_MAX_ANCHORS];
    int anchor_count;
    int32_t width, height;
    uint32_t bg;                /* canvas colour */
    int zoom;                   /* percent */
    bool truncated;
    uint64_t layout_ms;
} WebDoc;

/* items: caller-owned array of item_cap WebItems. */
void web_doc_init(WebDoc *d, WebArena *dom, WebArena *css, WebArena *layout, WebItem *items, int item_cap,
                  const WebFontMetrics *fm);
void web_doc_parse(WebDoc *d, const char *html, size_t len, bool plain, bool latin1);
/* The stylesheet sheet_urls[url_index] arrived (css NULL = failed); the
 * text must stay valid while the document is in use. */
void web_doc_set_sheet(WebDoc *d, int url_index, const char *css, size_t len, const char *base_dir);
/* True while a requested stylesheet has not arrived (or failed). */
bool web_doc_sheets_pending(const WebDoc *d);
/* Cascade if needed and lay out for `width` CSS px at `zoom` percent. */
void web_doc_layout(WebDoc *d, int width, int viewport_h, int zoom, bool dark);
/* An image finished (pixels set or failed): relayout needed when its size was unknown. */
bool web_doc_image_needs_relayout(const WebDoc *d, int image);

int web_link_at(const WebDoc *d, int32_t x, int32_t y);
int web_field_at(const WebDoc *d, int32_t x, int32_t y);
int32_t web_anchor_y(const WebDoc *d, const char *name);
/* "a=1&b=2" (application/x-www-form-urlencoded) for a form submit. */
void web_form_encode(const WebDoc *d, int form, int submitter, char *out, size_t size);
/* Visible text of the page for find-in-page (items in order). */
int web_first_item_at(const WebDoc *d, int32_t y);

#endif /* _OJJY_WEB_ENGINE_H */
