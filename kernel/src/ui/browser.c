/*
 * ojjyOS v3 - Web browser app
 *
 * Fetches pages with the polled HTTP client (web/http.c), lays them out
 * with the HTML engine (web/html.c) and draws the display list in the Web
 * window. Built-in pages live in /System/Web/<name>.html (ojjy: scheme).
 * One instance; buffers are static (never in per-window state or on the
 * stack). Nothing here blocks the desktop.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "ui/browser.h"
#include "ui/draw.h"
#include "uifont.h"
#include "framebuffer.h"
#include "font.h"
#include "web/http.h"
#include "web/html.h"
#include "web/url.h"
#include "net/net.h"
#include "fs/vfs.h"
#include "string.h"
#include "serial.h"
#include "memory.h"

#define PAGE_BUF   (2 * 1024 * 1024)
#define HISTORY    24
#define TOOLBAR_H  36
#define STATUS_H   20
#define PAD        12
/* Wiby: a search engine for the classic web that still answers plain HTTP. */
#define SEARCH_URL "http://wiby.me/?q="

typedef enum { FOCUS_PAGE, FOCUS_ADDRESS, FOCUS_FIELD } Focus;

/* Large buffers come from the page allocator on first use (see memory.h). */
static uint8_t *page_buf;
static WebDoc *docp;
#define doc (*docp)

/* ---- Page text in the Inter UI font (proportional) ---- */
static TextStyle web_style(uint8_t style, uint8_t level)
{
    if (level == 1) return TEXT_LARGE;
    if (level == 2) return TEXT_HEADLINE;
    if (level == 3) return TEXT_TITLE;
    if (level >= 4 || (style & WS_BOLD)) return TEXT_BODY_BOLD;
    return TEXT_BODY;
}

static int web_width(const char *s, int len, uint8_t style, uint8_t level)
{
    return text_width_n(s, len, web_style(style, level));
}

static int web_line_height(uint8_t style, uint8_t level)
{
    return text_line_height(web_style(style, level)) + (level ? 4 : 2);
}

static const WebMetrics web_metrics = { web_width, web_line_height, text_has_glyph };
static HttpFetch fetch;
static char aux_html[8192];             /* generated error/info pages */

static struct {
    bool opened;
    Url url;                            /* page on screen */
    char address[URL_TEXT_MAX];
    Focus focus;
    int field;
    bool select_all;
    bool loading;
    bool replaying;                     /* back/forward: do not push */
    int scroll, view_w, view_h;
    int layout_w;
    const char *src;
    size_t src_len;
    bool src_plain;
    char status[160];
    char hover[160];
    uint64_t load_started, last_status_ms;
    Url history[HISTORY];
    int hist_scroll[HISTORY];
    int hist_count, hist_pos;
    int rect_x, rect_y, rect_w, rect_h;     /* content rect from last draw */
    int restore_scroll;
    char title[128];
} B;

/* ------------------------------------------------------------------ */
/* Small string helpers                                                */
/* ------------------------------------------------------------------ */

static void sappend(char *dst, size_t size, const char *src)
{
    size_t a = strlen(dst), b = strlen(src);
    if (a + b >= size) b = size - a - 1;
    memcpy(dst + a, src, b);
    dst[a + b] = '\0';
}

static void sappend_escaped(char *dst, size_t size, const char *src)
{
    for (; *src; src++) {
        if (*src == '<') sappend(dst, size, "&lt;");
        else if (*src == '>') sappend(dst, size, "&gt;");
        else if (*src == '&') sappend(dst, size, "&amp;");
        else if (*src == '"') sappend(dst, size, "&quot;");
        else { char c[2] = {*src, 0}; sappend(dst, size, c); }
    }
}

static void sappend_num(char *dst, size_t size, uint64_t v)
{
    char n[24];
    utoa(v, n, 10);
    sappend(dst, size, n);
}

static void set_status(const char *s)
{
    strncpy(B.status, s, sizeof(B.status) - 1);
    B.status[sizeof(B.status) - 1] = '\0';
}

/* ------------------------------------------------------------------ */
/* Layout + history                                                    */
/* ------------------------------------------------------------------ */

static void relayout(void)
{
    int w = B.view_w - 2 * PAD;
    if (w < 120) w = 120;
    html_layout(&doc, B.src ? B.src : "", B.src ? B.src_len : 0, w, B.src_plain);
    B.layout_w = B.view_w;
    strncpy(B.title, doc.title, sizeof(B.title) - 1);
}

static void show(const char *src, size_t len, bool plain)
{
    B.src = src;
    B.src_len = len;
    B.src_plain = plain;
    B.scroll = 0;
    B.focus = FOCUS_PAGE;
    B.field = -1;
    relayout();
    if (B.url.fragment[0]) {
        int32_t y = web_anchor_y(&doc, B.url.fragment);
        if (y >= 0) B.scroll = y;
    }
    url_format(&B.url, B.address, sizeof(B.address), false);
    if (B.restore_scroll >= 0) { B.scroll = B.restore_scroll; B.restore_scroll = -1; }
    ui_mark_dirty();
}

/* Record the page now on screen. A Back/Forward/Reload replay updates its
 * own entry (and restores its scroll) instead of pushing a new one; the
 * flag lives until the asynchronous load completes. */
static void push_history(void)
{
    if (B.replaying) {
        B.replaying = false;
        B.history[B.hist_pos] = B.url;
        B.restore_scroll = B.hist_scroll[B.hist_pos];
        return;
    }
    B.restore_scroll = -1;
    if (B.hist_count > 0) {
        B.hist_scroll[B.hist_pos] = B.scroll;
        B.hist_count = B.hist_pos + 1;
        char a[URL_TEXT_MAX], b[URL_TEXT_MAX];
        url_format(&B.history[B.hist_pos], a, sizeof(a), false);
        url_format(&B.url, b, sizeof(b), false);
        if (strcmp(a, b) == 0) { B.history[B.hist_pos] = B.url; return; }   /* reload */
    }
    if (B.hist_count == HISTORY) {
        for (int i = 1; i < HISTORY; i++) { B.history[i - 1] = B.history[i]; B.hist_scroll[i - 1] = B.hist_scroll[i]; }
        B.hist_count--;
    }
    B.history[B.hist_count] = B.url;
    B.hist_scroll[B.hist_count] = 0;
    B.hist_pos = B.hist_count++;
}

/* ------------------------------------------------------------------ */
/* Built-in, error and info pages                                      */
/* ------------------------------------------------------------------ */

static void show_aux(const char *title, const char *body_html)
{
    aux_html[0] = '\0';
    sappend(aux_html, sizeof(aux_html), "<title>");
    sappend_escaped(aux_html, sizeof(aux_html), title);
    sappend(aux_html, sizeof(aux_html), "</title>");
    sappend(aux_html, sizeof(aux_html), body_html);
    show(aux_html, strlen(aux_html), false);
}

static void show_error(const Url *u, const char *reason)
{
    static char body[6144];
    char url[URL_TEXT_MAX], enc[URL_TEXT_MAX * 3];
    url_format(u, url, sizeof(url), false);
    body[0] = '\0';
    sappend(body, sizeof(body), "<h1>Can't open this page</h1><p>ojjyOS Web couldn't load <b>");
    sappend_escaped(body, sizeof(body), url);
    sappend(body, sizeof(body), "</b></p><p>");
    sappend_escaped(body, sizeof(body), reason);
    sappend(body, sizeof(body), "</p>");
    if (strcmp(u->scheme, "https") == 0) {
        Url plain = *u;
        strcpy(plain.scheme, "http");
        if (plain.port == 443) plain.port = 80;
        char http[URL_TEXT_MAX];
        url_format(&plain, http, sizeof(http), false);
        url_encode_component(u->host, enc, sizeof(enc));
        sappend(body, sizeof(body), "<p>ojjyOS speaks TLS 1.3 with x25519; some servers need older or different "
                                    "settings. You can:</p><ul><li><a href=\"");
        sappend_escaped(body, sizeof(body), http);
        sappend(body, sizeof(body), "\">Try the plain http:// version</a></li><li><a href=\"" SEARCH_URL);
        sappend(body, sizeof(body), enc);
        sappend(body, sizeof(body), "\">Search for this site on Wiby</a></li></ul>");
    } else if (!net_info()->present) {
        sappend(body, sizeof(body), "<p>No network hardware was found. Start QEMU with the network device "
                                    "(scripts/run-qemu.sh adds it).</p>");
    } else if (!net_info()->enabled) {
        sappend(body, sizeof(body), "<p>The network is turned off. Open <b>Control Center</b> and turn on "
                                    "<b>Wi-Fi</b>, then try again.</p>");
    }
    sappend(body, sizeof(body), "<p><a href=\"");
    sappend_escaped(body, sizeof(body), url);
    sappend(body, sizeof(body), "\">Try again</a> &nbsp; | &nbsp; <a href=\"ojjy:home\">Start page</a></p>");
    B.url = *u;
    push_history();
    show_aux("Can't open page", body);
}

static void load_builtin(const Url *u)
{
    char path[160];
    path[0] = '\0';
    sappend(path, sizeof(path), "/System/Web/");
    sappend(path, sizeof(path), u->host);
    sappend(path, sizeof(path), ".html");
    for (const char *c = u->host; *c; c++) {
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_')) { show_error(u, "Unknown built-in page."); return; }
    }
    VfsFile *f = vfs_open(path, VFS_O_READ);
    if (!f) { show_error(u, "There is no built-in page with that name."); return; }
    size_t len = 0;
    ssize_t n;
    while (len < PAGE_BUF - 1 && (n = vfs_read(f, page_buf + len, PAGE_BUF - 1 - len)) > 0) len += (size_t)n;
    vfs_close(f);
    B.url = *u;
    push_history();
    show((const char *)page_buf, len, false);
    set_status("Built-in page");
}

/* ------------------------------------------------------------------ */
/* Navigation                                                          */
/* ------------------------------------------------------------------ */

static void navigate_ex(const Url *u, bool replay)
{
    B.replaying = replay;
    if (B.loading) { http_cancel(&fetch); B.loading = false; }
    if (B.hist_count && !replay) B.hist_scroll[B.hist_pos] = B.scroll;
    B.hover[0] = '\0';
    if (strcmp(u->scheme, "ojjy") == 0) { load_builtin(u); return; }
    http_start_url(&fetch, u, page_buf, PAGE_BUF - 1);
    if (fetch.phase == HTTP_ERROR) { show_error(u, fetch.error); return; }
    B.loading = true;
    B.load_started = net_now();
    url_format(u, B.address, sizeof(B.address), false);
    set_status("Loading...");
    serial_printf("[WEB] GET %s\n", B.address);
    ui_mark_dirty();
}

static void navigate(const Url *u) { navigate_ex(u, false); }

static bool looks_like_url(const char *s)
{
    bool dot = false;
    for (const char *c = s; *c; c++) {
        if (*c == ' ') return false;
        if (*c == '.' || *c == ':' || *c == '/') dot = true;
    }
    return dot;
}

void browser_go(const char *address)
{
    if (!docp) return;
    while (*address == ' ') address++;
    if (!*address) return;
    Url u;
    if (!looks_like_url(address) || !url_parse(address, &u)) {
        static char search[URL_TEXT_MAX];
        char enc[URL_TEXT_MAX];
        url_encode_component(address, enc, sizeof(enc) - sizeof(SEARCH_URL));
        strcpy(search, SEARCH_URL);
        sappend(search, sizeof(search), enc);
        if (!url_parse(search, &u)) return;
    }
    B.focus = FOCUS_PAGE;
    navigate(&u);
}

static void follow_href(const char *href)
{
    Url base = B.url, target;
    if (doc.base[0]) { Url b; if (url_resolve(&B.url, doc.base, &b)) base = b; }
    if (href[0] == '#') {
        int32_t y = web_anchor_y(&doc, href + 1);
        if (y >= 0) { B.scroll = y; ui_mark_dirty(); }
        return;
    }
    if (strncmp(href, "mailto:", 7) == 0 || strncmp(href, "tel:", 4) == 0) {
        set_status("ojjyOS Web can't open mail or phone links");
        ui_mark_dirty();
        return;
    }
    if (!url_resolve(&base, href, &target)) {
        set_status("That link's address isn't supported");
        ui_mark_dirty();
        return;
    }
    navigate(&target);
}

static void submit_form(int form, int submitter)
{
    if (form < 0 || form >= doc.form_count) return;
    Url base = B.url, target;
    if (doc.base[0]) { Url b; if (url_resolve(&B.url, doc.base, &b)) base = b; }
    const char *action = web_text(&doc, doc.forms[form].action);
    if (!url_resolve(&base, action[0] ? action : "", &target)) return;
    char query[URL_PATH_MAX];
    web_form_query(&doc, form, submitter, query, sizeof(query));
    char *q = target.path;
    while (*q && *q != '?') q++;
    *q = '\0';
    if (strlen(target.path) + strlen(query) < sizeof(target.path)) sappend(target.path, sizeof(target.path), query);
    target.fragment[0] = '\0';
    navigate(&target);
    if (doc.forms[form].post) set_status("Sent as a GET request (POST forms are not supported yet)");
}

static void go_history(int delta)
{
    int pos = B.hist_pos + delta;
    if (pos < 0 || pos >= B.hist_count) return;
    B.hist_scroll[B.hist_pos] = B.scroll;
    B.hist_pos = pos;
    navigate_ex(&B.history[pos], true);
}

static void reload(void)
{
    if (B.loading) { http_cancel(&fetch); B.loading = false; set_status("Stopped"); ui_mark_dirty(); return; }
    B.hist_scroll[B.hist_pos] = B.scroll;
    navigate_ex(&B.url, B.hist_count > 0);
}

/* ------------------------------------------------------------------ */
/* Fetch completion                                                    */
/* ------------------------------------------------------------------ */

/* Short cipher name for the status bar. */
static const char *strstr_ci_suite(const char *suite)
{
    return suite[4] == 'C' ? "ChaCha20" : suite[4] == 'A' ? "AES-GCM" : suite;
}

static bool starts_with_ci(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        char c = (*s >= 'A' && *s <= 'Z') ? (char)(*s + 32) : *s;
        if (c != *prefix) return false;
    }
    return true;
}

static void fetch_done(void)
{
    B.loading = false;
    B.url = fetch.url;
    const char *type = fetch.head.content_type;
    uint64_t ms = net_now() - B.load_started;
    serial_printf("[WEB] loaded status=%d type=%s bytes=%u ms=%u\n", fetch.head.status, type,
                  (unsigned)fetch.body_len, (unsigned)ms);
    bool html = !type[0] || starts_with_ci(type, "text/html") || starts_with_ci(type, "application/xhtml");
    bool text = starts_with_ci(type, "text/");
    push_history();
    if (html || text) {
        uint8_t *body = (uint8_t *)fetch.body;
        size_t len = fetch.body_len;
        show((const char *)body, len, !html);
    } else {
        static char body[1024];
        char url[URL_TEXT_MAX];
        url_format(&fetch.url, url, sizeof(url), false);
        body[0] = '\0';
        sappend(body, sizeof(body), "<h1>File</h1><p><b>");
        sappend_escaped(body, sizeof(body), url);
        sappend(body, sizeof(body), "</b></p><p>Type: ");
        sappend_escaped(body, sizeof(body), type);
        sappend(body, sizeof(body), "<br>Size: ");
        sappend_num(body, sizeof(body), fetch.body_len);
        sappend(body, sizeof(body), " bytes</p><p>ojjyOS Web shows web pages and text. Images and downloads are "
                                    "planned for a later release.</p>");
        show_aux("File", body);
    }
    char st[160];
    st[0] = '\0';
    if (fetch.head.status != 200) { sappend(st, sizeof(st), "HTTP "); sappend_num(st, sizeof(st), (uint64_t)fetch.head.status); sappend(st, sizeof(st), "  -  "); }
    sappend(st, sizeof(st), "Loaded ");
    sappend_num(st, sizeof(st), (fetch.body_len + 1023) / 1024);
    sappend(st, sizeof(st), " KB in ");
    sappend_num(st, sizeof(st), ms / 1000);
    sappend(st, sizeof(st), ".");
    sappend_num(st, sizeof(st), (ms % 1000) / 100);
    sappend(st, sizeof(st), " s");
    if (fetch.secure) {
        sappend(st, sizeof(st), "  -  Encrypted (TLS 1.3 ");
        sappend(st, sizeof(st), strstr_ci_suite(fetch.tls_suite));
        sappend(st, sizeof(st), "), site identity not verified");
    }
    if (fetch.truncated) sappend(st, sizeof(st), "  (page too large: truncated)");
    if (doc.truncated) sappend(st, sizeof(st), "  (layout truncated)");
    set_status(st);
    /* Test/diagnostic receipt: where the first links landed in page space. */
    int shown = 0;
    for (int i = 0; i < doc.run_count && shown < 4; i++) {
        if (doc.runs[i].link < 0 || (i && doc.runs[i - 1].link == doc.runs[i].link)) continue;
        serial_printf("[WEB] link %d '%s' at %d,%d\n", doc.runs[i].link, web_text(&doc, doc.links[doc.runs[i].link]),
                      (int)doc.runs[i].x, (int)doc.runs[i].y);
        shown++;
    }
}

void browser_poll(void)
{
    if (!B.loading) return;
    HttpPhase phase = http_poll(&fetch);
    if (phase == HTTP_DONE) { fetch_done(); ui_mark_dirty(); return; }
    if (phase == HTTP_ERROR) {
        B.loading = false;
        serial_printf("[WEB] failed: %s\n", fetch.error);
        show_error(&fetch.url, fetch.error);
        set_status("Failed");
        return;
    }
    uint64_t now = net_now();
    if (now - B.last_status_ms >= 200) {
        B.last_status_ms = now;
        char st[160];
        st[0] = '\0';
        sappend(st, sizeof(st), http_phase_text(phase));
        sappend(st, sizeof(st), " ");
        sappend(st, sizeof(st), fetch.url.host);
        if (phase == HTTP_RECEIVING) {
            sappend(st, sizeof(st), "  ");
            sappend_num(st, sizeof(st), fetch.len / 1024);
            sappend(st, sizeof(st), " KB");
        }
        sappend(st, sizeof(st), "...");
        if (strcmp(st, B.status) != 0) { set_status(st); ui_mark_dirty(); }
    }
}

bool browser_loading(void) { return B.loading; }

void browser_open(void)
{
    if (B.opened) return;
    if (!page_buf) page_buf = (uint8_t *)pmm_alloc_bytes(PAGE_BUF);
    if (!docp) docp = (WebDoc *)pmm_alloc_bytes(sizeof(WebDoc));
    html_set_metrics(&web_metrics);
    if (!page_buf || !docp) { serial_printf("[WEB] not enough memory for the browser\n"); return; }
    B.opened = true;
    B.field = -1;
    B.restore_scroll = -1;
    Url home;
    url_parse("ojjy:home", &home);
    navigate(&home);
}

const char *browser_title(void)
{
    if (B.loading) return "Loading...";
    if (B.title[0]) return B.title;
    return B.url.host[0] ? B.url.host : "Web";
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */



/* Dark appearance = light text tokens. Fields and the page follow it. */
static bool dark_ui(const ThemeTokens *t) { return ((t->text >> 16) & 0xFF) > 128; }
static Color field_bg(const ThemeTokens *t) { return dark_ui(t) ? RGB(20, 26, 38) : COLOR_WHITE; }
static Color field_fg(const ThemeTokens *t) { return dark_ui(t) ? t->text : COLOR_TEXT; }

static GfxSurface *surf(void)
{
    static GfxSurface s;
    fb_get_surface(&s);
    return &s;
}

/* Toolbar button with a drawn glyph: '<' '>' back/forward, 'R' reload,
 * 'x' stop, 'H' home. */
static void button(int x, int y, int w, int h, const char *label, bool enabled)
{
    const ThemeTokens *t = ui_theme();
    GfxSurface *s = surf();
    Color c = enabled ? t->text : t->text_muted;
    uint8_t a = enabled ? 255 : 110;
    int cx = x + w / 2, cy = y + h / 2;
    switch (label[0]) {
    case '<': case '>': {
        int dir = label[0] == '<' ? -1 : 1;
        for (int k = 0; k <= 4; k++) {
            ui_rounded_rect(cx + dir * (k - 2) - 1, cy - 5 + k, 2, 2, 1, c, a);
            ui_rounded_rect(cx + dir * (k - 2) - 1, cy + 4 - k, 2, 2, 1, c, a);
        }
        break;
    }
    case 'R':
        gfx_arc_up(s, cx, cy, 5, 7, 150, c, a);
        ui_rounded_rect(cx + 3, cy - 8, 6, 2, 1, c, a);
        ui_rounded_rect(cx + 7, cy - 8, 2, 6, 1, c, a);
        break;
    case 'x':
        for (int k = -4; k <= 4; k++) {
            ui_rounded_rect(cx + k - 1, cy + k - 1, 2, 2, 1, c, a);
            ui_rounded_rect(cx + k - 1, cy - k - 1, 2, 2, 1, c, a);
        }
        break;
    default:            /* house */
        for (int k = 0; k < 7; k++) ui_rounded_rect(cx - k - 1, cy - 7 + k, 2 + 2 * k, 2, 1, c, a);
        ui_rounded_rect(cx - 5, cy - 1, 10, 8, 1, c, a);
        ui_rounded_rect(cx - 1, cy + 3, 3, 4, 1, t->dock_tint, a);
        break;
    }
    (void)h;
}

static int page_x(void) { return B.rect_x + 6; }
static int page_y(void) { return B.rect_y + TOOLBAR_H + 4; }
static int page_w(void) { return B.rect_w - 12; }
static int page_h(void) { return B.rect_h - TOOLBAR_H - STATUS_H - 8; }

static void clamp_scroll(void)
{
    int max = doc.height + 16 - page_h();
    if (max < 0) max = 0;
    if (B.scroll > max) B.scroll = max;
    if (B.scroll < 0) B.scroll = 0;
}

static Color run_color(uint8_t style)
{
    const ThemeTokens *t = ui_theme();
    if (style & (WS_BUTTON | WS_FIELD)) return t->text;
    if (style & WS_LINK) return t->accent;
    if (style & WS_MUTED) return t->text_muted;
    if (style & WS_CODE) return ui_blend(t->text, t->accent, 90);
    if (style & WS_ITALIC) return ui_blend(t->text, t->text_muted, 110);
    return t->text;
}

static void draw_field(const WebBox *b, int ox, int oy)
{
    const ThemeTokens *t = ui_theme();
    if (b->field < 0 || b->field >= doc.field_count) return;
    WebField *f = &doc.fields[b->field];
    int x = ox + b->x, y = oy + b->y;
    bool focused = B.focus == FOCUS_FIELD && B.field == b->field;
    if (b->kind == BOX_BUTTON) {
        ui_rounded_rect(x, y, b->w, b->h, 6, t->accent_soft, f->kind == FIELD_SUBMIT ? 230 : 140);
        const char *label = f->label[0] ? f->label : f->value;
        int lw = text_width(label, TEXT_BODY_MEDIUM);
        if (lw > b->w - 8) lw = b->w - 8;
        text_draw_fit(surf(), x + (b->w - lw) / 2, text_y_center(y, b->h, TEXT_BODY_MEDIUM), label,
                      TEXT_BODY_MEDIUM, t->text, b->w - 8);
        return;
    }
    ui_rounded_rect(x, y, b->w, b->h, 4, field_bg(t), 235);
    ui_rounded_rect(x, y + b->h - 2, b->w, 2, 1, focused ? t->accent : t->text_muted, focused ? 255 : 120);
    if (f->kind == FIELD_CHECKBOX) {
        if (f->checked) ui_rounded_rect(x + 3, y + 3, b->w - 6, b->h - 6, 2, t->accent, 255);
        return;
    }
    /* Value (or placeholder), scrolled so its end stays visible, then caret. */
    char shown[128];
    const char *v = f->value;
    bool placeholder = !v[0] && !focused && f->label[0];
    if (placeholder) v = f->label;
    size_t k = 0;
    for (size_t i = 0; v[i] && k + 1 < sizeof(shown); i++)
        shown[k++] = (f->kind == FIELD_PASSWORD && !placeholder) ? '*' : v[i];
    shown[k] = '\0';
    const char *p = shown;
    int room = b->w - 12;
    while (*p && text_width(p, TEXT_BODY) > room) p++;
    int ty = text_y_center(y, b->h, TEXT_BODY);
    int tw = text_draw(surf(), x + 5, ty, p, TEXT_BODY, placeholder ? t->text_muted : field_fg(t));
    if (focused) ui_rounded_rect(x + 6 + tw, y + 3, 1, b->h - 6, 0, t->accent, 255);
}

void browser_draw(int x, int y, int w, int h)
{
    if (!docp) return;
    const ThemeTokens *t = ui_theme();
    if (B.rect_x != x || B.rect_y != y || B.rect_w != w || B.rect_h != h)
        serial_printf("[WEB] view %d,%d %dx%d page-origin %d,%d\n", x, y, w, h, x + 6 + PAD, y + TOOLBAR_H + 4 + 8);
    B.rect_x = x; B.rect_y = y; B.rect_w = w; B.rect_h = h;
    B.view_w = page_w();
    B.view_h = page_h();
    if (!B.loading && B.src && B.layout_w != B.view_w) relayout();
    clamp_scroll();

    /* Toolbar */
    int by = y + 6;
    button(x + 6, by, 26, 24, "<", B.hist_pos > 0);
    button(x + 34, by, 26, 24, ">", B.hist_pos + 1 < B.hist_count);
    button(x + 62, by, 26, 24, B.loading ? "x" : "R", true);
    button(x + 90, by, 26, 24, "H", true);
    int ax = x + 122, aw = w - 128;
    bool editing = B.focus == FOCUS_ADDRESS;
    GfxSurface *s = surf();
    ui_rounded_rect(ax, by, aw, 24, 8, field_bg(t), editing ? 245 : (dark_ui(t) ? 200 : 170));
    if (editing) {
        gfx_round_rect_stroke(s, ax - 1, by - 1, aw + 2, 26, 9, t->accent, 255);
        gfx_round_rect_stroke(s, ax - 2, by - 2, aw + 4, 28, 10, t->accent, 90);
    }
    if (editing) {
        /* Left-aligned, scrolled so the caret end stays visible */
        const char *a = B.address;
        int room = aw - 20;
        while (*a && text_width(a, TEXT_BODY) > room) a++;
        int tx = ax + 10, ty = text_y_center(by, 24, TEXT_BODY);
        fb_set_clip(ax, by, aw - 4, 24);
        int tw = text_width(a, TEXT_BODY);
        if (B.select_all && *a) ui_rounded_rect(tx - 2, by + 4, tw + 4, 16, 3, t->accent_soft, 170);
        text_draw(s, tx, ty, a, TEXT_BODY, field_fg(t));
        if (!B.select_all) ui_rounded_rect(tx + tw + 1, by + 4, 2, 16, 1, t->accent, 255);
        fb_reset_clip();
    } else {
        /* Safari-style: the host, centred, with a lock for https */
        bool secure = strcmp(B.url.scheme, "https") == 0;
        const char *shown = B.url.host[0] ? B.url.host : (B.address[0] ? B.address : "Search or enter website name");
        int tw = MIN(text_width(shown, TEXT_BODY_MEDIUM), aw - 60);
        int tx = ax + (aw - tw) / 2 + (secure ? 8 : 0);
        if (secure) {
            ui_rounded_rect(tx - 16, by + 11, 9, 7, 2, t->text_muted, 255);
            gfx_arc_up(s, tx - 12, by + 11, 2, 3, 90, t->text_muted, 255);
        }
        text_draw_fit(s, tx, text_y_center(by, 24, TEXT_BODY_MEDIUM), shown, TEXT_BODY_MEDIUM,
                      B.url.host[0] ? t->text : t->text_muted, aw - 60);
    }
    if (B.loading) {
        /* indeterminate progress: a sliding segment under the toolbar */
        int span = w - 12, seg = span / 4;
        int pos = (int)((net_now() / 4) % (uint64_t)(span + seg)) - seg;
        fb_set_clip(x + 6, y + TOOLBAR_H - 3, span, 3);
        ui_rounded_rect(x + 6 + pos, y + TOOLBAR_H - 3, seg, 3, 1, t->accent, 255);
        fb_reset_clip();
    }

    /* Page */
    int px = page_x(), py = page_y(), pw = page_w(), ph = page_h();
    ui_rounded_rect(px, py, pw, ph, 8, dark_ui(t) ? RGB(24, 30, 42) : ui_blend(t->dock_tint, COLOR_WHITE, 120), 238);
    fb_set_clip(px, py, pw, ph);
    int ox = px + PAD, oy = py + 8 - B.scroll;
    for (int i = 0; i < doc.box_count; i++) {
        const WebBox *b = &doc.boxes[i];
        if (b->y + b->h < B.scroll - 8 || b->y > B.scroll + ph) continue;
        switch (b->kind) {
            case BOX_RULE: fb_fill_rect(ox + b->x, oy + b->y, b->w, 1, t->text_muted); break;
            case BOX_BULLET: ui_rounded_rect(ox + b->x, oy + b->y, b->w, b->h, 2, t->text, 255); break;
            case BOX_QUOTE_BAR: fb_fill_rect(ox + b->x, oy + b->y, b->w, b->h, t->accent_soft); break;
            case BOX_CODE: ui_rounded_rect(ox + b->x, oy + b->y, b->w, b->h, 6, t->accent_soft, 50); break;
            default: draw_field(b, ox, oy); break;
        }
    }
    for (int i = web_first_run_at(&doc, B.scroll - 32); i < doc.run_count; i++) {
        const WebRun *r = &doc.runs[i];
        if (r->y > B.scroll + ph) break;
        Color c = run_color(r->style);
        const char *s = web_text(&doc, r->text);
        if (r->style & WS_CODE) {
            /* Code and <pre> keep the monospace font (columns line up). */
            fb_draw_text_scaled(ox + r->x, oy + r->y, s, r->len, c, r->scale, (r->style & WS_BOLD) != 0);
            if (r->style & WS_LINK) fb_fill_rect(ox + r->x, oy + r->y + FONT_HEIGHT * r->scale - 2, r->w, 1, c);
            continue;
        }
        TextStyle ts = web_style(r->style, r->level);
        int ty = oy + r->y + (r->h - text_line_height(ts)) / 2;
        text_draw_n(surf(), ox + r->x, ty, s, r->len, ts, c, 255);
        if (r->style & WS_LINK)
            fb_fill_rect(ox + r->x, ty + text_ascent(ts) + 2, r->w, 1, c);
    }
    /* Scrollbar */
    if (doc.height + 16 > ph) {
        int track = ph - 8;
        int thumb = track * ph / (doc.height + 16);
        if (thumb < 24) thumb = 24;
        int max = doc.height + 16 - ph;
        int ty = py + 4 + (max ? (track - thumb) * B.scroll / max : 0);
        ui_rounded_rect(px + pw - 7, ty, 4, thumb, 2, t->text_muted, 150);
    }
    fb_reset_clip();

    /* Status bar */
    const char *st = B.hover[0] ? B.hover : B.status;
    text_draw_fit(surf(), x + 8, text_y_center(y + h - STATUS_H, STATUS_H, TEXT_SMALL), st, TEXT_SMALL, t->text_muted, w - 16);
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

bool browser_click(int x, int y)
{
    if (!docp) return false;
    if (!in_rect(x, y, B.rect_x, B.rect_y, B.rect_w, B.rect_h)) return false;
    int by = B.rect_y + 6;
    if (in_rect(x, y, B.rect_x + 6, by, 26, 24)) { go_history(-1); return true; }
    if (in_rect(x, y, B.rect_x + 34, by, 26, 24)) { go_history(1); return true; }
    if (in_rect(x, y, B.rect_x + 62, by, 26, 24)) { reload(); return true; }
    if (in_rect(x, y, B.rect_x + 90, by, 26, 24)) { browser_go("ojjy:home"); return true; }
    if (in_rect(x, y, B.rect_x + 122, by, B.rect_w - 128, 24)) {
        if (B.focus != FOCUS_ADDRESS) { B.focus = FOCUS_ADDRESS; B.select_all = true; }
        else B.select_all = false;
        ui_mark_dirty();
        return true;
    }
    if (!in_rect(x, y, page_x(), page_y(), page_w(), page_h())) return true;
    int32_t dx = x - (page_x() + PAD), dy = y - (page_y() + 8) + B.scroll;
    int box = web_box_at(&doc, dx, dy);
    if (box >= 0 && doc.boxes[box].field >= 0) {
        int f = doc.boxes[box].field;
        WebField *fl = &doc.fields[f];
        if (doc.boxes[box].kind == BOX_BUTTON) {
            if (fl->kind == FIELD_SUBMIT) submit_form(fl->form, f);
        } else if (fl->kind == FIELD_CHECKBOX) {
            fl->checked = !fl->checked;
        } else {
            B.focus = FOCUS_FIELD;
            B.field = f;
        }
        ui_mark_dirty();
        return true;
    }
    int link = web_link_at(&doc, dx, dy);
    if (link >= 0) { follow_href(web_text(&doc, doc.links[link])); return true; }
    if (B.focus != FOCUS_PAGE) { B.focus = FOCUS_PAGE; ui_mark_dirty(); }
    return true;
}

bool browser_hover(int x, int y)
{
    if (!docp) return false;
    char next[sizeof(B.hover)];
    next[0] = '\0';
    if (in_rect(x, y, page_x(), page_y(), page_w(), page_h())) {
        int link = web_link_at(&doc, x - (page_x() + PAD), y - (page_y() + 8) + B.scroll);
        if (link >= 0) {
            Url base = B.url, target;
            const char *href = web_text(&doc, doc.links[link]);
            if (url_resolve(&base, href, &target)) url_format(&target, next, sizeof(next), true);
            else strncpy(next, href, sizeof(next) - 1);
        }
    }
    if (strcmp(next, B.hover) == 0) return false;
    strcpy(B.hover, next);
    return true;
}

void browser_scroll(int pixels)
{
    if (!docp) return;
    B.scroll += pixels;
    clamp_scroll();
    ui_mark_dirty();
}

static int next_text_field(int from)
{
    for (int i = from + 1; i < doc.field_count; i++)
        if ((doc.fields[i].kind == FIELD_TEXT || doc.fields[i].kind == FIELD_PASSWORD) && doc.fields[i].box >= 0) return i;
    return -1;
}

bool browser_shortcut(KeyCode key)
{
    if (!docp) return false;
    switch (key) {
        case KEY_L: B.focus = FOCUS_ADDRESS; B.select_all = true; ui_mark_dirty(); return true;
        case KEY_R: reload(); return true;
        case KEY_LBRACKET: go_history(-1); return true;
        case KEY_RBRACKET: go_history(1); return true;
        case KEY_PERIOD: if (B.loading) reload(); return true;
        default: return false;
    }
}

static void edit_text(char *buf, size_t size, KeyCode key, char ascii, bool *select_all)
{
    size_t n = strlen(buf);
    if (key == KEY_BACKSPACE) {
        if (select_all && *select_all) { buf[0] = '\0'; *select_all = false; }
        else if (n) buf[n - 1] = '\0';
        return;
    }
    if (ascii >= 32 && ascii <= 126) {
        if (select_all && *select_all) { buf[0] = '\0'; n = 0; *select_all = false; }
        if (n + 1 < size) { buf[n] = ascii; buf[n + 1] = '\0'; }
    }
}

bool browser_key(KeyCode key, char ascii, uint8_t modifiers)
{
    if (!docp) return false;
    (void)modifiers;
    if (B.focus == FOCUS_ADDRESS) {
        if (key == KEY_ENTER) { char a[URL_TEXT_MAX]; strcpy(a, B.address); B.select_all = false; browser_go(a); }
        else if (key == KEY_ESCAPE) { B.focus = FOCUS_PAGE; url_format(&B.url, B.address, sizeof(B.address), false); }
        else if (key == KEY_LEFT || key == KEY_RIGHT) B.select_all = false;
        else edit_text(B.address, sizeof(B.address), key, ascii, &B.select_all);
        ui_mark_dirty();
        return true;
    }
    if (B.focus == FOCUS_FIELD && B.field >= 0 && B.field < doc.field_count) {
        WebField *f = &doc.fields[B.field];
        if (key == KEY_ENTER) {
            int submitter = -1;
            for (int i = 0; i < doc.field_count; i++)
                if (doc.fields[i].form == f->form && doc.fields[i].kind == FIELD_SUBMIT) { submitter = i; break; }
            submit_form(f->form, submitter);
        } else if (key == KEY_TAB) {
            int nf = next_text_field(B.field);
            if (nf >= 0) B.field = nf; else B.focus = FOCUS_PAGE;
        } else if (key == KEY_ESCAPE) {
            B.focus = FOCUS_PAGE;
        } else {
            edit_text(f->value, sizeof(f->value), key, ascii, NULL);
        }
        ui_mark_dirty();
        return true;
    }
    int step = page_h() - 40;
    switch (key) {
        case KEY_UP: browser_scroll(-48); return true;
        case KEY_DOWN: browser_scroll(48); return true;
        case KEY_PAGEUP: browser_scroll(-step); return true;
        case KEY_PAGEDOWN: browser_scroll(step); return true;
        case KEY_SPACE: browser_scroll((modifiers & INPUT_MOD_SHIFT) ? -step : step); return true;
        case KEY_HOME: B.scroll = 0; ui_mark_dirty(); return true;
        case KEY_END: B.scroll = doc.height; clamp_scroll(); ui_mark_dirty(); return true;
        case KEY_TAB: {
            int nf = next_text_field(-1);
            if (nf >= 0) { B.focus = FOCUS_FIELD; B.field = nf; ui_mark_dirty(); }
            return true;
        }
        default: break;
    }
    return false;
}
