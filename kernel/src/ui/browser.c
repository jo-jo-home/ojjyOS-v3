/*
 * ojjyOS v3 - ojjyOS Web: a tabbed, Chrome-style browser
 *
 * Tabs keep their own history, page source, stylesheets and scroll; the
 * active tab's page is parsed and laid out by the web engine (web/engine.h)
 * into one shared document. Pages, stylesheets and images load with the
 * polled HTTP client; nothing here blocks the desktop. Drawing lives in
 * browser_ui.c, the profile (bookmarks, history, cookies) in
 * browser_profile.c and built-in pages in browser_pages.c.
 *
 * Big buffers come from the page allocator once and are reused (pmm memory
 * cannot be freed); nothing large is on the stack or in BSS.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "ui/browser.h"
#include "ui/browser_int.h"
#include "ui/draw.h"
#include "ui/services.h"
#include "web/paint.h"
#include "webfont.h"
#include "fs/vfs.h"
#include "net/net.h"
#include "users.h"
#include "system.h"
#include "string.h"
#include "serial.h"
#include "memory.h"
#include "image/image.h"
#include "timer.h"

Browser *BR;

static const int zoom_levels[] = {50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200, 250, 300};

/* Engine memory (shared by all tabs: only the active tab is laid out). */
static WebArena dom_arena, css_arena, lay_arena;
static WebItem *items;
#define ITEM_CAP 160000

/* Subresource fetches (stylesheets, images) for the active document. */
typedef struct {
    bool busy;
    int kind, index, tab;
    uint32_t generation;
    HttpFetch fetch;
    uint8_t *buf;
} ResSlot;
static ResSlot *res;
static uint32_t doc_generation;

/* Decoded images of the active page (bump pool, reset per document). */
static uint8_t *img_pool;
static size_t img_used;

/* ---- Font metrics for the engine ---- */
static uint8_t m_pick(int px, int w, bool it, bool mono) { return webfont_pick(px, w, it, mono); }
static int m_width(const char *s, int len, uint8_t f) { return webfont_width(s, len, f); }
static int m_ascent(uint8_t f) { return webfont_ascent(f); }
static int m_height(uint8_t f) { return webfont_height(f); }
static bool m_glyph(uint32_t cp) { return text_has_glyph(cp); }
static const WebFontMetrics metrics = {m_pick, m_width, m_ascent, m_height, m_glyph};

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void sappend(char *dst, size_t size, const char *src)
{
    size_t a = strlen(dst), b = strlen(src);
    if (a + b >= size) b = size - a - 1;
    memcpy(dst + a, src, b);
    dst[a + b] = '\0';
}

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool starts_ci(const char *s, const char *p)
{
    for (; *p; s++, p++) if (lc(*s) != lc(*p)) return false;
    return true;
}

static bool contains_ci(const char *h, const char *n)
{
    size_t nl = strlen(n);
    if (!nl) return true;
    for (; *h; h++) {
        size_t i = 0;
        while (i < nl && h[i] && lc(h[i]) == lc(n[i])) i++;
        if (i == nl) return true;
    }
    return false;
}

static void set_status(Tab *t, const char *s)
{
    strncpy(t->status, s, sizeof(t->status) - 1);
    t->status[sizeof(t->status) - 1] = '\0';
}

Tab *active_tab(void)
{
    if (!BR || BR->active < 0 || BR->active >= MAX_TABS || !BR->tabs[BR->active].used) return NULL;
    return &BR->tabs[BR->active];
}

static int tab_index(const Tab *t) { return (int)(t - BR->tabs); }

static void tab_url_text(const Tab *t, char *out, size_t size)
{
    if (t->url.scheme[0]) url_format(&t->url, out, size, false);
    else out[0] = '\0';
}

const char *br_search_url(void) { return search_urls[setting_search()]; }

int br_content_top(void)
{
    return TOOLBAR_Y + TOOLBAR_H + (BR->show_bmbar ? BMBAR_H : 0);
}

/* ------------------------------------------------------------------ */
/* Document (active tab)                                               */
/* ------------------------------------------------------------------ */

static void cancel_resources(void)
{
    for (int i = 0; i < RES_SLOTS; i++) if (res[i].busy) { http_cancel(&res[i].fetch); res[i].busy = false; }
}

static bool is_latin1(const Tab *t)
{
    /* Content-Type charset, else a <meta charset> near the top */
    for (const char *c = t->ctype; *c; c++) {
        if (starts_ci(c, "charset=")) return starts_ci(c + 8, "iso-8859") || starts_ci(c + 8, "windows-125") || starts_ci(c + 8, "latin");
    }
    size_t n = t->src_len < 2048 ? t->src_len : 2048;
    const char *s = (const char *)t->buf;
    for (size_t i = 0; i + 16 < n; i++) {
        if (!starts_ci(s + i, "charset=")) continue;
        const char *v = s + i + 8;
        if (*v == '"' || *v == '\'') v++;
        return starts_ci(v, "iso-8859") || starts_ci(v, "windows-125") || starts_ci(v, "latin");
    }
    return false;
}

/* Parse the active tab's page into the shared document. */
static void doc_load(Tab *t)
{
    cancel_resources();
    doc_generation++;
    img_used = 0;
    BR->doc_tab = tab_index(t);
    BR->doc_parsed = BR->doc_laid_out = false;
    BR->field = -1;
    BR->mark_count = 0;
    if (!t->have_page) return;
    web_doc_parse(BR->doc, (const char *)t->buf, t->src_len, t->plain, t->latin1);
    if (BR->doc->title[0]) {
        strncpy(t->title, BR->doc->title, sizeof(t->title) - 1);
        t->title[sizeof(t->title) - 1] = '\0';
    }
    /* stylesheets this tab already has (tab switch, back/forward) */
    for (int i = 0; i < BR->doc->sheet_url_count; i++)
        if (t->sheet_state[i] == SHEET_DONE) web_doc_set_sheet(BR->doc, i, (const char *)t->sbuf + t->sheet_off[i], t->sheet_len[i], t->sheet_base[i]);
        else if (t->sheet_state[i] == SHEET_FAILED) web_doc_set_sheet(BR->doc, i, NULL, 0, NULL);
    BR->doc_parsed = true;
    BR->layout_dirty = true;
    serial_printf("[WEB] parsed nodes=%d sheets=%d fields=%d js=%dB\n", BR->doc->tree.node_count,
                  BR->doc->sheet_url_count, BR->doc->field_count, BR->doc->script_bytes);
}

static void base_url(Url *out)
{
    Tab *t = &BR->tabs[BR->doc_tab >= 0 ? BR->doc_tab : BR->active];
    *out = t->url;
    if (BR->doc->base[0]) { Url b; if (url_resolve(&t->url, BR->doc->base, &b)) *out = b; }
}

static void relayout(void)
{
    Tab *t = active_tab();
    if (!t || !BR->doc_parsed || BR->doc_tab != BR->active) return;
    uint64_t t0 = timer_get_ticks();
    int w = BR->cw - 2;
    if (w < 200) w = 200;
    web_doc_layout(BR->doc, w, BR->ch, t->zoom, false);
    BR->layout_w = BR->cw;
    BR->doc_laid_out = true;
    BR->layout_dirty = false;
    BR->layout_ms = net_now();
    if (t->restore_scroll >= 0) { t->scroll = t->restore_scroll; t->restore_scroll = -1; }
    if (t->url.fragment[0]) {
        int32_t y = web_anchor_y(BR->doc, t->url.fragment);
        if (y >= 0 && t->scroll == 0) t->scroll = y;
    }
    if (BR->find_open) br_find_update();
    serial_printf("[WEB] layout items=%d height=%d ms=%u%s\n", BR->doc->item_count, (int)BR->doc->height,
                  (unsigned)(timer_get_ticks() - t0), BR->doc->truncated ? " (truncated)" : "");
    /* diagnostic receipt for tests: where the first links landed */
    int shown = 0;
    for (int i = 0; i < BR->doc->item_count && shown < 4; i++) {
        const WebItem *it = &BR->doc->items[i];
        if (it->kind != WI_TEXT || it->link < 0) continue;
        bool seen = false;
        for (int k = 0; k < i; k++) if (BR->doc->items[k].kind == WI_TEXT && BR->doc->items[k].link == it->link) { seen = true; break; }
        if (seen) continue;
        serial_printf("[WEB] link %d '%s' at %d,%d\n", it->link, BR->doc->links[it->link], (int)it->x, (int)it->y);
        shown++;
    }
    ui_mark_dirty();
}

/* ------------------------------------------------------------------ */
/* Subresources                                                        */
/* ------------------------------------------------------------------ */

static bool resolve_res(const char *ref, Url *out)
{
    Url base;
    base_url(&base);
    return url_resolve(&base, ref, out) && (strcmp(out->scheme, "http") == 0 || strcmp(out->scheme, "https") == 0);
}

static bool free_slot(void)
{
    for (int s = 0; s < RES_SLOTS; s++) if (!res[s].busy) return true;
    return false;
}

static void start_res(int kind, int index)
{
    Tab *t = &BR->tabs[BR->doc_tab];
    const char *ref = kind == RES_SHEET ? BR->doc->sheet_urls[index] : BR->doc->images[index].url;
    Url u;
    if (!resolve_res(ref, &u)) {
        if (kind == RES_SHEET) { t->sheet_state[index] = SHEET_FAILED; web_doc_set_sheet(BR->doc, index, NULL, 0, NULL); BR->layout_dirty = true; }
        else BR->doc->images[index].state = IMG_FAILED;
        return;
    }
    for (int s = 0; s < RES_SLOTS; s++) {
        ResSlot *r = &res[s];
        if (r->busy || !r->buf) continue;
        char referer[256];
        url_format(&t->url, referer, sizeof(referer), false);
        http_start_request(&r->fetch, &u, false, NULL, 0, NULL, referer, r->buf, RES_CAP - 1);
        if (r->fetch.phase == HTTP_ERROR) break;
        r->busy = true;
        r->kind = kind;
        r->index = index;
        r->tab = BR->doc_tab;
        r->generation = doc_generation;
        if (kind == RES_SHEET) t->sheet_state[index] = SHEET_LOADING;
        else BR->doc->images[index].state = IMG_LOADING;
        return;
    }
}

/* SVG at a chosen size: twice the requested size for HiDPI, at most 2048 */
static bool svg_raster(WebImage *im, const uint8_t *data, size_t len, int rw, int rh, uint32_t color)
{
    int w = rw * 2, h = rh * 2;
    while (w > 2048 || h > 2048) { w /= 2; h /= 2; }
    if (w < 1 || h < 1) return false;
    size_t need = (size_t)w * (size_t)h * 4;
    if (img_used + need > IMG_POOL) return false;
    uint32_t *px = (uint32_t *)(img_pool + img_used);
    if (!image_render_svg(data, len, px, w, h, color)) return false;
    img_used += (need + 15) & ~(size_t)15;
    im->pixels = px;
    im->w = (int16_t)rw;
    im->h = (int16_t)rh;
    im->pw = (int16_t)w;
    im->ph = (int16_t)h;
    im->state = IMG_READY;
    return true;
}

static void image_done(int index, const uint8_t *data, size_t len)
{
    WebImage *im = &BR->doc->images[index];
    int w = 0, h = 0, natural_w, natural_h;
    if (image_is_svg(data, len)) {
        if (!image_info(data, len, &w, &h) || !svg_raster(im, data, len, w, h, 0xFF000000u)) im->state = IMG_FAILED;
        return;
    }
    if (!image_info(data, len, &w, &h) || w <= 0 || h <= 0 || (uint64_t)w * (uint64_t)h > 16u * 1024 * 1024) {
        im->state = IMG_FAILED;
        serial_printf("[WEB] image %d undecodable (%02x %02x %02x %02x)\n", index, len > 0 ? data[0] : 0, len > 1 ? data[1] : 0,
                      len > 2 ? data[2] : 0, len > 3 ? data[3] : 0);
        return;
    }
    size_t need = (size_t)w * (size_t)h * 4;
    if (img_used + need > IMG_POOL) { im->state = IMG_FAILED; return; }
    uint32_t *px = (uint32_t *)(img_pool + img_used);
    natural_w = w;
    natural_h = h;
    if (!image_decode(data, len, px, (size_t)w * (size_t)h)) { im->state = IMG_FAILED; serial_printf("[WEB] image %d decode failed %dx%d\n", index, w, h); return; }
    /* very large pictures are halved (2x2 average) until they fit a page
     * view: the pool then holds many more of them */
    while (w > 1400 && h > 1) {
        int nw = w / 2, nh = h / 2;
        for (int y = 0; y < nh; y++)
            for (int x = 0; x < nw; x++) {
                const uint32_t *a = px + (size_t)(2 * y) * w + 2 * x, *b = a + w;
                uint32_t out = 0;
                for (int sh = 0; sh < 32; sh += 8) {
                    uint32_t v = ((a[0] >> sh) & 255) + ((a[1] >> sh) & 255) + ((b[0] >> sh) & 255) + ((b[1] >> sh) & 255);
                    out |= ((v + 2) >> 2) << sh;
                }
                px[(size_t)y * nw + x] = out;
            }
        w = nw;
        h = nh;
        need = (size_t)w * (size_t)h * 4;
    }
    img_used += (need + 15) & ~(size_t)15;
    im->pixels = px;
    im->w = (int16_t)(natural_w > 32767 ? 32767 : natural_w);
    im->h = (int16_t)(natural_h > 32767 ? 32767 : natural_h);
    im->pw = (int16_t)w;
    im->ph = (int16_t)h;
    im->state = IMG_READY;
}

static void poll_resources(void)
{
    Tab *t = active_tab();
    for (int s = 0; s < RES_SLOTS; s++) {
        ResSlot *r = &res[s];
        if (!r->busy) continue;
        HttpPhase ph = http_poll(&r->fetch);
        if (ph != HTTP_DONE && ph != HTTP_ERROR) continue;
        r->busy = false;
        if (r->generation != doc_generation || !t || r->tab != BR->doc_tab) continue;
        bool ok = ph == HTTP_DONE && r->fetch.head.status >= 200 && r->fetch.head.status < 300;
        serial_printf("[WEB] %s %d status=%d bytes=%u type=%s%s\n", r->kind == RES_SHEET ? "sheet" : "image", r->index,
                      ph == HTTP_DONE ? r->fetch.head.status : -1, (unsigned)r->fetch.body_len, r->fetch.head.content_type,
                      ph == HTTP_ERROR ? r->fetch.error : "");
        if (r->kind == RES_SHEET) {
            Tab *dt = &BR->tabs[r->tab];
            if (ok && dt->sused + r->fetch.body_len + 1 < SHEET_CAP) {
                memcpy(dt->sbuf + dt->sused, r->fetch.body, r->fetch.body_len);
                dt->sheet_off[r->index] = (uint32_t)dt->sused;
                dt->sheet_len[r->index] = (uint32_t)r->fetch.body_len;
                dt->sused += r->fetch.body_len + 1;
                dt->sbuf[dt->sused - 1] = '\0';
                dt->sheet_state[r->index] = SHEET_DONE;
                /* the sheet's directory: relative url() inside it resolve there */
                char *b = dt->sheet_base[r->index];
                url_format(&r->fetch.url, b, sizeof(dt->sheet_base[0]), false);
                char *q = b;
                while (*q && *q != '?') q++;
                *q = '\0';
                char *last = b;
                for (char *c = b; *c; c++) if (*c == '/') last = c;
                if (last > b) last[1] = '\0';
                web_doc_set_sheet(BR->doc, r->index, (const char *)dt->sbuf + dt->sheet_off[r->index], r->fetch.body_len, b);
            } else {
                dt->sheet_state[r->index] = SHEET_FAILED;
                web_doc_set_sheet(BR->doc, r->index, NULL, 0, NULL);
            }
            BR->layout_dirty = true;
        } else {
            if (ok) image_done(r->index, r->fetch.body, r->fetch.body_len);
            else BR->doc->images[r->index].state = IMG_FAILED;
            BR->layout_dirty = true;
            ui_mark_dirty();
        }
    }
    if (!t || !BR->doc_parsed || BR->doc_tab != BR->active) return;
    /* stylesheets first, then images (those near the screen first) */
    for (int i = 0; i < BR->doc->sheet_url_count && i < WEB_MAX_SHEETS; i++) {
        if (!free_slot()) return;
        if (t->sheet_state[i] == SHEET_NONE) start_res(RES_SHEET, i);
    }
    if (!BR->doc_laid_out) return;
    /* inline <svg>s: rasterized here, nothing to fetch (bounded per turn) */
    for (int i = 0, done = 0; i < BR->doc->image_count && done < 48; i++) {
        WebImage *im = &BR->doc->images[i];
        if (!im->svg || im->state != IMG_PENDING) continue;
        int nw = 0, nh = 0;
        image_info((const uint8_t *)im->svg, im->svg_len, &nw, &nh);
        int rw = im->rw > 0 ? im->rw : nw, rh = im->rh > 0 ? im->rh : nh;
        if (!svg_raster(im, (const uint8_t *)im->svg, im->svg_len, rw, rh, im->color)) im->state = IMG_FAILED;
        done++;
        ui_mark_dirty();
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < BR->doc->image_count; i++) {
            if (!free_slot()) return;
            if (BR->doc->images[i].state != IMG_PENDING || BR->doc->images[i].svg) continue;
            if (pass == 0) {
                bool near = false;
                for (int k = 0; k < BR->doc->item_count; k++) {
                    const WebItem *it = &BR->doc->items[k];
                    if ((it->kind == WI_IMAGE || it->kind == WI_BGIMAGE) && it->ref == i && it->y < t->scroll + BR->ch * 2 && it->y + it->h > t->scroll - 200) { near = true; break; }
                }
                if (!near) continue;
            }
            start_res(RES_IMAGE, i);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Tabs and history                                                    */
/* ------------------------------------------------------------------ */

static void tab_alloc(Tab *t)
{
    if (!t->buf) t->buf = (uint8_t *)pmm_alloc_bytes(PAGE_CAP);
    if (!t->sbuf) t->sbuf = (uint8_t *)pmm_alloc_bytes(SHEET_CAP);
    if (!t->fetch) t->fetch = (HttpFetch *)pmm_alloc_bytes(sizeof(HttpFetch));
}

static void push_history(Tab *t)
{
    if (t->replay) {
        t->replay = false;
        t->hist[t->hist_pos] = t->url;
        t->restore_scroll = t->hist_scroll[t->hist_pos];
        return;
    }
    t->restore_scroll = -1;
    if (t->hist_count > 0) {
        t->hist_scroll[t->hist_pos] = t->scroll;
        t->hist_count = t->hist_pos + 1;
        char a[URL_TEXT_MAX], b[URL_TEXT_MAX];
        url_format(&t->hist[t->hist_pos], a, sizeof(a), false);
        url_format(&t->url, b, sizeof(b), false);
        if (strcmp(a, b) == 0) { t->hist[t->hist_pos] = t->url; return; }
    }
    if (t->hist_count == TAB_HIST) {
        for (int i = 1; i < TAB_HIST; i++) { t->hist[i - 1] = t->hist[i]; t->hist_scroll[i - 1] = t->hist_scroll[i]; }
        t->hist_count--;
    }
    t->hist[t->hist_count] = t->url;
    t->hist_scroll[t->hist_count] = 0;
    t->hist_pos = t->hist_count++;
}

/* Show generated HTML (built-in pages, errors) in a tab. */
static void show_html(Tab *t, const Url *u, const char *html, size_t len)
{
    if (len >= PAGE_CAP) len = PAGE_CAP - 1;
    memmove(t->buf, html, len);
    t->buf[len] = '\0';
    t->src_len = len;
    t->plain = false;
    t->latin1 = false;
    t->have_page = true;
    t->secure = false;
    t->http_status = 200;
    t->ctype[0] = '\0';
    t->url = *u;
    t->sused = 0;
    memset(t->sheet_state, 0, sizeof(t->sheet_state));
    t->title[0] = '\0';
    t->scroll = 0;
    t->arrived_ms = net_now();
    push_history(t);
    if (tab_index(t) == BR->active) doc_load(t);
    if (!t->title[0]) {
        const char *s = (const char *)t->buf;
        for (size_t i = 0; i + 7 < t->src_len && i < 4096; i++) {
            if (!starts_ci(s + i, "<title>")) continue;
            size_t k = 0;
            while (s[i + 7 + k] && s[i + 7 + k] != '<' && k < sizeof(t->title) - 1) { t->title[k] = s[i + 7 + k]; k++; }
            t->title[k] = '\0';
            break;
        }
    }
    ui_mark_dirty();
}

static char *gen_buf(void)
{
    static char *g;
    if (!g) g = (char *)pmm_alloc_bytes(512 * 1024);
    return g;
}

static void show_error(Tab *t, const Url *u, const char *reason)
{
    char url[URL_TEXT_MAX];
    url_format(u, url, sizeof(url), false);
    char *g = gen_buf();
    size_t n = page_error(g, 512 * 1024, url, reason, strcmp(u->scheme, "https") == 0);
    show_html(t, u, g, n);
    strncpy(t->title, url, sizeof(t->title) - 1);
    set_status(t, reason);
}

static bool query_param(const char *path, const char *name, char *out, size_t size)
{
    const char *q = path;
    while (*q && *q != '?') q++;
    if (!*q) return false;
    q++;
    size_t nl = strlen(name);
    while (*q) {
        if (strncmp(q, name, nl) == 0 && q[nl] == '=') {
            q += nl + 1;
            size_t k = 0;
            while (q[k] && q[k] != '&' && k + 1 < size) { out[k] = q[k]; k++; }
            out[k] = '\0';
            return true;
        }
        while (*q && *q != '&') q++;
        if (*q) q++;
    }
    return false;
}

static void load_builtin(Tab *t, const Url *u)
{
    char *g = gen_buf();
    size_t n = 0;
    char v[32];
    const char *page = u->host;
    if (!g) return;
    if (strcmp(page, "newtab") == 0 || strcmp(page, "home") == 0) n = page_newtab(g, 512 * 1024);
    else if (strcmp(page, "history") == 0) {
        if (query_param(u->path, "clear", v, sizeof(v))) history_clear();
        n = page_history(g, 512 * 1024);
    } else if (strcmp(page, "bookmarks") == 0) {
        if (query_param(u->path, "remove", v, sizeof(v))) {
            int k = 0;
            for (const char *c = v; *c >= '0' && *c <= '9'; c++) k = k * 10 + (*c - '0');
            bookmark_remove(k);
        }
        n = page_bookmarks(g, 512 * 1024);
    } else if (strcmp(page, "downloads") == 0) n = page_downloads(g, 512 * 1024);
    else if (strcmp(page, "settings") == 0) {
        if (query_param(u->path, "search", v, sizeof(v))) setting_set_search(v[0] - '0');
        if (query_param(u->path, "bar", v, sizeof(v))) { setting_set_bmbar(v[0] == '1'); BR->show_bmbar = setting_bmbar(); }
        if (query_param(u->path, "cookies", v, sizeof(v))) cookies_clear();
        n = page_settings(g, 512 * 1024);
    }
    if (n) {
        Url clean = *u;
        if (strcmp(page, "home") == 0) strcpy(clean.host, "newtab");
        strcpy(clean.path, "/");
        show_html(t, &clean, g, n);
        return;
    }
    /* /System/Web/<name>.html */
    for (const char *c = page; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_')) { show_error(t, u, "Unknown built-in page."); return; }
    char path[160] = "/System/Web/";
    sappend(path, sizeof(path), page);
    sappend(path, sizeof(path), ".html");
    VfsFile *f = vfs_open(path, VFS_O_READ);
    if (!f) { show_error(t, u, "There is no built-in page with that name."); return; }
    size_t len = 0;
    ssize_t r;
    while (len < 512 * 1024 - 1 && (r = vfs_read(f, g + len, 512 * 1024 - 1 - len)) > 0) len += (size_t)r;
    vfs_close(f);
    show_html(t, u, g, len);
}

static void start_load(Tab *t, const Url *u, bool post)
{
    tab_alloc(t);
    if (!t->buf || !t->sbuf || !t->fetch) { serial_printf("[WEB] no memory for tab\n"); return; }
    if (t->loading) { http_cancel(t->fetch); t->loading = false; }
    char referer[256] = "";
    if (t->url.scheme[0] && strcmp(t->url.scheme, "ojjy") != 0) url_format(&t->url, referer, sizeof(referer), false);
    http_start_request(t->fetch, u, post, t->post_body, post ? strlen(t->post_body) : 0,
                       "application/x-www-form-urlencoded", referer, t->buf, PAGE_CAP - 1);
    if (t->fetch->phase == HTTP_ERROR) { show_error(t, u, t->fetch->error); return; }
    t->loading = true;
    t->started = net_now();
    char text[URL_TEXT_MAX];
    url_format(u, text, sizeof(text), false);
    if (!t->have_page || !t->title[0] || strcmp(t->title, "New Tab") == 0) {
        strncpy(t->title, u->host, sizeof(t->title) - 1);
        t->title[sizeof(t->title) - 1] = '\0';
    }
    set_status(t, "Loading...");
    serial_printf("[WEB] GET %s\n", text);
    if (tab_index(t) == BR->active && !BR->omni_focus) {
        strncpy(BR->omni, text, sizeof(BR->omni) - 1);
        BR->omni[sizeof(BR->omni) - 1] = '\0';
    }
    ui_mark_dirty();
}

void br_navigate(Tab *t, const Url *u, bool replay)
{
    if (!t) return;
    t->replay = replay;
    if (t->hist_count && !replay) t->hist_scroll[t->hist_pos] = t->scroll;
    if (tab_index(t) == BR->active) { BR->hover_link = -1; BR->hover_url[0] = '\0'; BR->menu = MENU_NONE; }
    if (strcmp(u->scheme, "ojjy") == 0) {
        if (t->loading) { http_cancel(t->fetch); t->loading = false; }
        tab_alloc(t);
        if (!t->buf) return;
        load_builtin(t, u);
        return;
    }
    start_load(t, u, false);
}

static bool looks_like_url(const char *s)
{
    if (starts_ci(s, "ojjy:") || starts_ci(s, "http://") || starts_ci(s, "https://") || starts_ci(s, "view-source:")) return true;
    if (starts_ci(s, "localhost")) return true;
    bool dot = false;
    for (const char *c = s; *c; c++) {
        if (*c == ' ') return false;
        if (*c == '.' && c[1] && c != s) dot = true;
    }
    return dot;
}

void br_go_text(Tab *t, const char *text)
{
    if (!t) return;
    while (*text == ' ') text++;
    if (!*text) return;
    bool source = false;
    if (starts_ci(text, "view-source:")) { source = true; text += 12; }
    Url u;
    if (!looks_like_url(text) || !url_parse(text, &u)) {
        static char search[URL_TEXT_MAX];
        char enc[URL_TEXT_MAX];
        url_encode_component(text, enc, sizeof(enc) - 128);
        strcpy(search, br_search_url());
        sappend(search, sizeof(search), enc);
        if (!url_parse(search, &u)) return;
    } else if (!starts_ci(text, "http://") && !starts_ci(text, "https://") && strcmp(u.scheme, "http") == 0 &&
               !starts_ci(text, "localhost") && !(u.host[0] >= '0' && u.host[0] <= '9')) {
        /* bare names: https first, like modern browsers */
        strcpy(u.scheme, "https");
        if (u.port == 80) u.port = 443;
    }
    t->want_source = source;
    BR->omni_focus = false;
    BR->sugg_count = 0;
    br_navigate(t, &u, false);
}

static void tab_init(Tab *t)
{
    uint8_t *buf = t->buf, *sbuf = t->sbuf;
    HttpFetch *fetch = t->fetch;
    memset(t, 0, sizeof(*t));
    t->buf = buf;
    t->sbuf = sbuf;
    t->fetch = fetch;
    t->used = true;
    t->zoom = 100;
    t->restore_scroll = -1;
    strcpy(t->title, "New Tab");
}

void br_activate(int i)
{
    if (i < 0 || i >= MAX_TABS || !BR->tabs[i].used) return;
    BR->active = i;
    BR->omni_focus = false;
    BR->sugg_count = 0;
    BR->menu = MENU_NONE;
    BR->find_open = false;
    Tab *t = &BR->tabs[i];
    if (BR->doc_tab != i || !BR->doc_parsed) {
        int keep = t->scroll;
        doc_load(t);
        t->restore_scroll = keep;
    }
    tab_url_text(t, BR->omni, sizeof(BR->omni));
    ui_mark_dirty();
}

void br_new_tab(const char *url, bool activate)
{
    int slot = -1;
    for (int i = 0; i < MAX_TABS; i++) if (!BR->tabs[i].used) { slot = i; break; }
    if (slot < 0) { ui_mark_dirty(); return; }
    Tab *t = &BR->tabs[slot];
    tab_init(t);
    tab_alloc(t);
    BR->count++;
    if (activate) { BR->active = slot; BR->doc_tab = -1; }
    Url u;
    if (url && url_parse(url, &u)) br_navigate(t, &u, false);
    if (activate) {
        br_activate(slot);
        if (!url || strcmp(url, "ojjy:newtab") == 0) { BR->omni_focus = true; BR->omni[0] = '\0'; BR->omni_caret = 0; BR->sugg_count = 0; }
    }
    ui_mark_dirty();
}

void br_close_tab(int i)
{
    if (i < 0 || i >= MAX_TABS || !BR->tabs[i].used) return;
    Tab *t = &BR->tabs[i];
    if (t->loading) { http_cancel(t->fetch); t->loading = false; }
    if (t->url.scheme[0]) {
        if (BR->closed_count == 10) { memmove(&BR->closed[0], &BR->closed[1], sizeof(Url) * 9); BR->closed_count--; }
        BR->closed[BR->closed_count++] = t->url;
    }
    t->used = false;
    BR->count--;
    if (BR->doc_tab == i) { cancel_resources(); BR->doc_tab = -1; BR->doc_parsed = false; }
    if (BR->count <= 0) { BR->count = 0; br_new_tab("ojjy:newtab", true); return; }
    if (BR->active == i) {
        int next = -1;
        for (int k = i + 1; k < MAX_TABS; k++) if (BR->tabs[k].used) { next = k; break; }
        if (next < 0) for (int k = i - 1; k >= 0; k--) if (BR->tabs[k].used) { next = k; break; }
        br_activate(next);
    }
    ui_mark_dirty();
}

void br_reload(Tab *t)
{
    if (!t) return;
    if (t->loading) { br_stop(t); return; }
    if (!t->url.scheme[0]) return;
    t->hist_scroll[t->hist_pos] = t->scroll;
    br_navigate(t, &t->url, t->hist_count > 0);
}

void br_stop(Tab *t)
{
    if (!t || !t->loading) return;
    http_cancel(t->fetch);
    t->loading = false;
    set_status(t, "Stopped");
    ui_mark_dirty();
}

void br_history_go(Tab *t, int delta)
{
    if (!t) return;
    int pos = t->hist_pos + delta;
    if (pos < 0 || pos >= t->hist_count) return;
    t->hist_scroll[t->hist_pos] = t->scroll;
    t->hist_pos = pos;
    br_navigate(t, &t->hist[pos], true);
}

void br_set_zoom(int zoom)
{
    Tab *t = active_tab();
    if (!t) return;
    if (zoom < 50) zoom = 50;
    if (zoom > 300) zoom = 300;
    BR->zoom_bubble_until = net_now() + 1500;
    if (t->zoom == zoom) { ui_mark_dirty(); return; }
    int64_t frac = BR->doc->height > 0 ? (int64_t)t->scroll * 10000 / BR->doc->height : 0;
    t->zoom = zoom;
    relayout();
    t->scroll = (int)(frac * BR->doc->height / 10000);
    ui_mark_dirty();
}

static void zoom_step(int dir)
{
    Tab *t = active_tab();
    if (!t) return;
    int n = (int)ARRAY_SIZE(zoom_levels);
    int i = 0;
    while (i < n && zoom_levels[i] < t->zoom) i++;
    if (dir > 0) { if (i < n && zoom_levels[i] == t->zoom) i++; }
    else i--;
    if (i < 0) i = 0;
    if (i >= n) i = n - 1;
    br_set_zoom(zoom_levels[i]);
}

/* ------------------------------------------------------------------ */
/* Page loaded                                                         */
/* ------------------------------------------------------------------ */

static void download_file(Tab *t)
{
    HttpFetch *f = t->fetch;
    char name[96];
    name[0] = '\0';
    if (f->head.filename[0]) strncpy(name, f->head.filename, sizeof(name) - 1);
    else {
        const char *p = f->url.path, *last = p;
        for (; *p && *p != '?'; p++) if (*p == '/') last = p + 1;
        size_t k = 0;
        while (last[k] && last[k] != '?' && k < sizeof(name) - 1) { name[k] = last[k]; k++; }
        name[k] = '\0';
    }
    name[sizeof(name) - 1] = '\0';
    for (char *c = name; *c; c++) if (*c == '/' || *c == '\\' || *c == ':' || (unsigned char)*c < 32) *c = '_';
    if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) strcpy(name, "download");
    char dir[160], path[256];
    const char *home = users_current_home();
    vfs_join_path(dir, sizeof(dir), home && home[0] ? home : "/Users", "Downloads");
    if (!vfs_isdir(dir)) vfs_mkdir(dir);
    vfs_join_path(path, sizeof(path), dir, name);
    /* "file.zip" -> "file (1).zip" when it exists */
    for (int n = 1; n < 100 && vfs_exists(path); n++) {
        char alt[96];
        const char *dot = NULL;
        for (const char *c = name; *c; c++) if (*c == '.') dot = c;
        size_t stem = dot && dot != name ? (size_t)(dot - name) : strlen(name);
        if (stem > 60) stem = 60;
        memcpy(alt, name, stem);
        alt[stem] = '\0';
        char num[8];
        utoa((uint64_t)n, num, 10);
        sappend(alt, sizeof(alt), " (");
        sappend(alt, sizeof(alt), num);
        sappend(alt, sizeof(alt), ")");
        if (dot && dot != name) sappend(alt, sizeof(alt), dot);
        vfs_join_path(path, sizeof(path), dir, alt);
    }
    VfsFile *out = vfs_open(path, VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    char msg[160] = "Downloaded ";
    if (out) {
        vfs_write(out, f->body, f->body_len);
        vfs_close(out);
        download_add(vfs_basename(path), path, (uint32_t)f->body_len);
        sappend(msg, sizeof(msg), vfs_basename(path));
        system_request_sync();
    } else {
        strcpy(msg, "Download failed: could not save ");
        sappend(msg, sizeof(msg), name);
    }
    notifications_push(msg);
    serial_printf("[WEB] download %s bytes=%u\n", path, (unsigned)f->body_len);
    set_status(t, msg);
}

static void fetch_done(Tab *t)
{
    HttpFetch *f = t->fetch;
    t->loading = false;
    const char *type = f->head.content_type;
    uint64_t ms = net_now() - t->started;
    serial_printf("[WEB] loaded status=%d type=%s bytes=%u ms=%u%s%u\n", f->head.status, type, (unsigned)f->body_len,
                  (unsigned)ms, f->head.gzip ? " gzip wire=" : f->head.deflate ? " deflate wire=" : " wire=",
                  (unsigned)f->wire_len);
    bool html = !type[0] || starts_ci(type, "text/html") || starts_ci(type, "application/xhtml");
    bool text = starts_ci(type, "text/") || starts_ci(type, "application/json") || starts_ci(type, "application/javascript") ||
                starts_ci(type, "application/xml");
    bool image = starts_ci(type, "image/png") || starts_ci(type, "image/jpeg") || starts_ci(type, "image/gif");
    if ((!html && !text && !image) || f->head.attachment) {
        download_file(t);
        if (!t->have_page) {
            Url u;
            url_parse("ojjy:downloads", &u);
            br_navigate(t, &u, false);
        }
        ui_mark_dirty();
        return;
    }
    if (image && !t->want_source) {
        char url[URL_TEXT_MAX];
        url_format(&f->url, url, sizeof(url), false);
        char *g = gen_buf();
        size_t n = page_image(g, 512 * 1024, url);
        Url u = f->url;
        show_html(t, &u, g, n);
        t->secure = f->secure;
        return;
    }
    /* the body sits inside t->buf (chunked data decoded in place) */
    size_t off = (size_t)(f->body - t->buf);
    memmove(t->buf, t->buf + off, f->body_len);
    t->src_len = f->body_len;
    t->buf[t->src_len] = '\0';
    t->plain = !html || t->want_source;
    t->want_source = false;
    strncpy(t->ctype, type, sizeof(t->ctype) - 1);
    t->latin1 = is_latin1(t);
    t->have_page = true;
    t->http_status = f->head.status;
    t->secure = f->secure;
    strncpy(t->suite, f->tls_suite, sizeof(t->suite) - 1);
    t->url = f->url;
    t->sused = 0;
    memset(t->sheet_state, 0, sizeof(t->sheet_state));
    t->title[0] = '\0';
    t->scroll = 0;
    t->arrived_ms = net_now();
    push_history(t);
    if (tab_index(t) == BR->active) doc_load(t);
    char url[URL_TEXT_MAX];
    url_format(&t->url, url, sizeof(url), false);
    if (tab_index(t) == BR->active && !BR->omni_focus) strcpy(BR->omni, url);
    if (!t->title[0]) {
        /* background tabs: a cheap <title> scan for the tab strip */
        const char *s = (const char *)t->buf;
        for (size_t i = 0; i + 7 < t->src_len && i < 65536; i++) {
            if (!starts_ci(s + i, "<title")) continue;
            const char *b = s + i + 6;
            while (*b && *b != '>') b++;
            if (*b) b++;
            size_t k = 0;
            while (b[k] && b[k] != '<' && k < sizeof(t->title) - 1) { t->title[k] = (b[k] == '\n' || b[k] == '\r') ? ' ' : b[k]; k++; }
            t->title[k] = '\0';
            break;
        }
        if (!t->title[0]) strncpy(t->title, t->url.host, sizeof(t->title) - 1);
    }
    history_add(t->title, url);
    char st[160] = "";
    if (f->head.status != 200) {
        char n[8];
        utoa((uint64_t)f->head.status, n, 10);
        sappend(st, sizeof(st), "HTTP ");
        sappend(st, sizeof(st), n);
        sappend(st, sizeof(st), " - ");
    }
    sappend(st, sizeof(st), "Loaded");
    if (f->truncated) sappend(st, sizeof(st), " (page too large: truncated)");
    set_status(t, st);
    /* <meta http-equiv=refresh> to another page soon */
    if (tab_index(t) == BR->active && BR->doc->refresh_url[0] && BR->doc->refresh_secs >= 0 && BR->doc->refresh_secs <= 5) {
        Url base, target;
        base_url(&base);
        if (url_resolve(&base, BR->doc->refresh_url, &target)) br_navigate(t, &target, false);
    }
    ui_mark_dirty();
}

/* ------------------------------------------------------------------ */
/* Links and forms                                                     */
/* ------------------------------------------------------------------ */

void br_follow_link(int link, bool new_tab)
{
    Tab *t = active_tab();
    if (!t || link < 0 || link >= BR->doc->link_count) return;
    const char *href = BR->doc->links[link];
    if (href[0] == '#') {
        int32_t y = web_anchor_y(BR->doc, href + 1);
        if (y >= 0) { t->scroll = y; ui_mark_dirty(); }
        return;
    }
    if (starts_ci(href, "mailto:") || starts_ci(href, "tel:")) { set_status(t, "Mail and phone links aren't supported"); ui_mark_dirty(); return; }
    Url base, target;
    base_url(&base);
    if (!url_resolve(&base, href, &target)) { set_status(t, "That link's address isn't supported"); ui_mark_dirty(); return; }
    if (strcmp(target.scheme, t->url.scheme) == 0 && strcmp(target.host, t->url.host) == 0 &&
        strcmp(target.path, t->url.path) == 0 && target.fragment[0]) {
        int32_t y = web_anchor_y(BR->doc, target.fragment);
        if (y >= 0) { t->scroll = y; ui_mark_dirty(); return; }
    }
    if (new_tab) {
        char text[URL_TEXT_MAX];
        url_format(&target, text, sizeof(text), true);
        int keep = BR->active;
        br_new_tab(text, false);
        BR->active = keep;
        return;
    }
    br_navigate(t, &target, false);
}

static void submit_form(int form, int submitter)
{
    Tab *t = active_tab();
    if (!t) return;
    Url base, target;
    base_url(&base);
    const char *action = form >= 0 && form < BR->doc->form_count ? BR->doc->forms[form].action : "";
    if (!url_resolve(&base, action[0] ? action : "", &target)) return;
    static char query[2048];
    web_form_encode(BR->doc, form, submitter, query, sizeof(query));
    bool post = form >= 0 && form < BR->doc->form_count && BR->doc->forms[form].post;
    target.fragment[0] = '\0';
    serial_printf("[WEB] submit form=%d %s\n", form, post ? "POST" : "GET");
    if (post && strcmp(target.scheme, "ojjy") != 0) {
        strncpy(t->post_body, query, sizeof(t->post_body) - 1);
        t->post_body[sizeof(t->post_body) - 1] = '\0';
        t->hist_scroll[t->hist_pos] = t->scroll;
        start_load(t, &target, true);
        return;
    }
    char *q = target.path;
    while (*q && *q != '?') q++;
    *q = '\0';
    if (strlen(target.path) + strlen(query) + 2 < sizeof(target.path)) {
        sappend(target.path, sizeof(target.path), "?");
        sappend(target.path, sizeof(target.path), query);
    }
    br_navigate(t, &target, false);
}

static void field_activate(int f)
{
    WebField *fl = &BR->doc->fields[f];
    if (fl->disabled) return;
    switch (fl->kind) {
    case FIELD_SUBMIT:
        submit_form(fl->form, f);
        break;
    case FIELD_CHECKBOX:
        fl->checked = !fl->checked;
        break;
    case FIELD_RADIO:
        for (int i = 0; i < BR->doc->field_count; i++) {
            WebField *o = &BR->doc->fields[i];
            if (o->kind == FIELD_RADIO && o->form == fl->form && strcmp(o->name, fl->name) == 0) o->checked = false;
        }
        fl->checked = true;
        break;
    case FIELD_SELECT:
        if (fl->option_count) {
            BR->menu = MENU_SELECT;
            BR->menu_field = f;
            BR->menu_hover = fl->selected;
        }
        break;
    case FIELD_TEXT: case FIELD_PASSWORD: case FIELD_TEXTAREA:
        BR->field = f;
        BR->field_caret = (int)strlen(fl->value);
        BR->omni_focus = false;
        break;
    case FIELD_RESET:
        for (int i = 0; i < BR->doc->field_count; i++) {
            WebField *o = &BR->doc->fields[i];
            if (o->form != fl->form) continue;
            const char *v = o->node ? dom_attr(o->node, "value") : NULL;
            if (o->kind == FIELD_TEXT || o->kind == FIELD_PASSWORD) { o->value[0] = '\0'; if (v) strncpy(o->value, v, sizeof(o->value) - 1); }
            if (o->kind == FIELD_CHECKBOX || o->kind == FIELD_RADIO) o->checked = o->node && dom_attr(o->node, "checked");
        }
        break;
    default:
        break;
    }
    ui_mark_dirty();
}

/* ------------------------------------------------------------------ */
/* Find in page                                                        */
/* ------------------------------------------------------------------ */

void br_find_update(void)
{
    BR->mark_count = 0;
    if (!BR->find[0] || !BR->doc_laid_out) return;
    size_t nl = strlen(BR->find);
    Tab *t = active_tab();
    int first_visible = -1;
    for (int i = 0; i < BR->doc->item_count && BR->mark_count < MAX_MARKS; i++) {
        const WebItem *it = &BR->doc->items[i];
        if (it->kind != WI_TEXT) continue;
        for (uint32_t k = 0; k + nl <= it->len && BR->mark_count < MAX_MARKS; k++) {
            size_t m = 0;
            while (m < nl && lc(it->text[k + m]) == lc(BR->find[m])) m++;
            if (m != nl) continue;
            BR->marks[BR->mark_count * 3] = i;
            BR->marks[BR->mark_count * 3 + 1] = (int32_t)k;
            BR->marks[BR->mark_count * 3 + 2] = (int32_t)nl;
            if (first_visible < 0 && t && it->y >= t->scroll) first_visible = BR->mark_count;
            BR->mark_count++;
            k += (uint32_t)nl - 1;
        }
    }
    if (BR->mark_cur >= BR->mark_count) BR->mark_cur = 0;
    if (first_visible >= 0 && BR->mark_cur == 0) BR->mark_cur = first_visible;
}

static void find_step(int dir)
{
    Tab *t = active_tab();
    if (!t || !BR->mark_count) return;
    BR->mark_cur = (BR->mark_cur + dir + BR->mark_count) % BR->mark_count;
    const WebItem *it = &BR->doc->items[BR->marks[BR->mark_cur * 3]];
    if (it->y < t->scroll + 40 || it->y > t->scroll + BR->ch - 60) t->scroll = it->y - BR->ch / 3;
    if (t->scroll < 0) t->scroll = 0;
    ui_mark_dirty();
}

void br_view_source(void)
{
    Tab *t = active_tab();
    if (!t || !t->url.scheme[0] || strcmp(t->url.scheme, "ojjy") == 0) return;
    char text[URL_TEXT_MAX + 16] = "view-source:";
    char u[URL_TEXT_MAX];
    url_format(&t->url, u, sizeof(u), false);
    sappend(text, sizeof(text), u);
    br_new_tab(NULL, true);
    br_go_text(active_tab(), text);
}

/* ------------------------------------------------------------------ */
/* Omnibox suggestions                                                 */
/* ------------------------------------------------------------------ */

static void add_sugg(int kind, const char *title, const char *url)
{
    if (BR->sugg_count >= MAX_SUGG) return;
    for (int i = 0; i < BR->sugg_count; i++) if (BR->sugg[i].kind == kind && strcmp(BR->sugg[i].url, url) == 0) return;
    Suggestion *s = &BR->sugg[BR->sugg_count++];
    s->kind = kind;
    strncpy(s->title, title, sizeof(s->title) - 1);
    s->title[sizeof(s->title) - 1] = '\0';
    strncpy(s->url, url, sizeof(s->url) - 1);
    s->url[sizeof(s->url) - 1] = '\0';
}

void br_omnibox_suggest(void)
{
    BR->sugg_count = 0;
    BR->sugg_sel = 0;
    const char *q = BR->omni;
    while (*q == ' ') q++;
    if (!*q) return;
    if (looks_like_url(q)) add_sugg(3, q, q);
    char search_title[160] = "";
    sappend(search_title, sizeof(search_title), q);
    sappend(search_title, sizeof(search_title), " \xE2\x80\x94 Search ");
    sappend(search_title, sizeof(search_title), search_names[setting_search()]);
    add_sugg(0, search_title, q);
    for (int i = 0; i < bookmark_count() && BR->sugg_count < 4; i++) {
        const Bookmark *b = bookmark_get(i);
        if (contains_ci(b->url, q) || contains_ci(b->title, q)) add_sugg(2, b->title, b->url);
    }
    for (int i = 0; i < history_count() && BR->sugg_count < MAX_SUGG; i++) {
        const HistEntry *h = history_get(i);
        if (contains_ci(h->url, q) || contains_ci(h->title, q)) add_sugg(1, h->title, h->url);
    }
}

static void omni_commit(void)
{
    Tab *t = active_tab();
    if (!t) return;
    static char text[URL_TEXT_MAX];
    if (BR->sugg_count && BR->sugg_sel >= 0 && BR->sugg_sel < BR->sugg_count && BR->sugg[BR->sugg_sel].kind == 0) {
        /* explicit search, even for URL-like text */
        char enc[URL_TEXT_MAX];
        url_encode_component(BR->sugg[BR->sugg_sel].url, enc, sizeof(enc) - 128);
        strcpy(text, br_search_url());
        sappend(text, sizeof(text), enc);
    } else if (BR->sugg_count && BR->sugg_sel >= 0 && BR->sugg_sel < BR->sugg_count) {
        strcpy(text, BR->sugg[BR->sugg_sel].url);
    } else {
        strcpy(text, BR->omni);
    }
    BR->omni_focus = false;
    BR->sugg_count = 0;
    br_go_text(t, text);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void browser_open(void)
{
    if (!BR) {
        Browser *b = (Browser *)pmm_alloc_bytes(sizeof(Browser));
        void *dm = pmm_alloc_bytes(40u << 20), *cm = pmm_alloc_bytes(28u << 20), *lm = pmm_alloc_bytes(20u << 20);
        items = (WebItem *)pmm_alloc_bytes(sizeof(WebItem) * ITEM_CAP);
        WebDoc *doc = (WebDoc *)pmm_alloc_bytes(sizeof(WebDoc));
        res = (ResSlot *)pmm_alloc_bytes(sizeof(ResSlot) * RES_SLOTS);
        img_pool = (uint8_t *)pmm_alloc_bytes(IMG_POOL);
        if (!b || !dm || !cm || !lm || !items || !doc || !res || !img_pool) { serial_printf("[WEB] not enough memory for the browser\n"); return; }
        memset(b, 0, sizeof(*b));
        memset(res, 0, sizeof(ResSlot) * RES_SLOTS);
        for (int i = 0; i < RES_SLOTS; i++) res[i].buf = (uint8_t *)pmm_alloc_bytes(RES_CAP);
        arena_init(&dom_arena, dm, 40u << 20);
        arena_init(&css_arena, cm, 28u << 20);
        arena_init(&lay_arena, lm, 20u << 20);
        b->doc = doc;
        web_doc_init(b->doc, &dom_arena, &css_arena, &lay_arena, items, ITEM_CAP, &metrics);
        b->doc_tab = -1;
        b->field = -1;
        b->hover_link = -1;
        b->hover_tab = -1;
        b->hover_close = -1;
        b->menu_link = -1;
        void *scratch = pmm_alloc_bytes(48u << 20);
        if (scratch) image_set_scratch(scratch, 48u << 20);
        /* gzip/deflate pages and stylesheets decode through this buffer */
        uint8_t *zbuf = (uint8_t *)pmm_alloc_bytes(PAGE_CAP);
        if (zbuf) http_set_inflate_buffer(zbuf, PAGE_CAP - 1);
        BR = b;
    }
    profile_load();
    profile_hooks_install();
    BR->show_bmbar = setting_bmbar();
    BR->opened = true;
    if (BR->count == 0) br_new_tab("ojjy:newtab", true);
}

void browser_poll(void)
{
    if (!BR || !BR->opened) return;
    static uint64_t last_paint;
    for (int i = 0; i < MAX_TABS; i++) {
        Tab *t = &BR->tabs[i];
        if (!t->used || !t->loading) continue;
        HttpPhase ph = http_poll(t->fetch);
        if (ph == HTTP_DONE) { fetch_done(t); continue; }
        if (ph == HTTP_ERROR) {
            t->loading = false;
            serial_printf("[WEB] failed: %s\n", t->fetch->error);
            if (strcmp(t->fetch->error, "stopped") != 0) show_error(t, &t->fetch->url, t->fetch->error);
            continue;
        }
        if (net_now() - last_paint > 120) { last_paint = net_now(); ui_mark_dirty(); }   /* spinner, progress */
    }
    poll_resources();
    Tab *t = active_tab();
    if (t && BR->doc_parsed && BR->doc_tab == BR->active && BR->layout_dirty && BR->cw > 0) {
        /* the first paint waits (briefly) for stylesheets, like Chrome */
        bool pending = web_doc_sheets_pending(BR->doc);
        uint64_t now = net_now();
        bool first = !BR->doc_laid_out;
        if ((first && (!pending || now - t->arrived_ms > 3000)) || (!first && (now - BR->layout_ms > 300 || !pending)))
            relayout();
    }
    /* caret blink */
    static uint64_t blink;
    if ((BR->omni_focus || BR->field >= 0 || BR->find_open) && net_now() - blink > 530) { blink = net_now(); ui_mark_dirty(); }
    profile_poll();
}

void browser_draw(int x, int y, int w, int h)
{
    if (!BR || !BR->opened) return;
    if (BR->x != x || BR->y != y || BR->w != w || BR->h != h)
        serial_printf("[WEB] view %d,%d %dx%d page-origin %d,%d\n", x, y, w, h, x + 2, y + br_content_top());
    BR->x = x; BR->y = y; BR->w = w; BR->h = h;
    BR->show_bmbar = setting_bmbar();
    BR->cx = x + 1;
    BR->cy = y + br_content_top();
    BR->cw = w - 2;
    BR->ch = h - br_content_top() - 1;
    Tab *t = active_tab();
    if (t && BR->doc_parsed && BR->doc_tab == BR->active && BR->doc_laid_out && BR->layout_w != BR->cw) relayout();
    if (t && BR->doc_laid_out) {
        int max = BR->doc->height - BR->ch + 24;
        if (max < 0) max = 0;
        if (t->scroll > max) t->scroll = max;
        if (t->scroll < 0) t->scroll = 0;
    }
    ui_draw_browser(x, y, w, h);
}

const char *browser_title(void)
{
    Tab *t = active_tab();
    if (!t) return "Web";
    return t->title[0] ? t->title : "New Tab";
}

bool browser_loading(void)
{
    if (!BR) return false;
    for (int i = 0; i < MAX_TABS; i++) if (BR->tabs[i].used && BR->tabs[i].loading) return true;
    for (int i = 0; i < RES_SLOTS; i++) if (res && res[i].busy) return true;
    return false;
}

void browser_go(const char *address)
{
    if (!BR) return;
    Tab *t = active_tab();
    if (!t) { br_new_tab(address, true); return; }
    br_go_text(t, address);
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

static bool in_rect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

void br_menu_action(int menu, int item)
{
    Tab *t = active_tab();
    BR->menu = MENU_NONE;
    if (menu == MENU_SELECT) {
        WebField *f = &BR->doc->fields[BR->menu_field];
        if (item >= 0 && item < f->option_count) {
            f->selected = (int16_t)item;
            strncpy(f->value, f->option_values[item], sizeof(f->value) - 1);
            f->value[sizeof(f->value) - 1] = '\0';
        }
        ui_mark_dirty();
        return;
    }
    const char *labels[20];
    int n = ui_menu_items(menu, labels, 20);
    if (item < 0 || item >= n) return;
    const char *l = labels[item];
    if (strcmp(l, "New Tab") == 0) br_new_tab("ojjy:newtab", true);
    else if (strcmp(l, "Reopen Closed Tab") == 0) {
        if (BR->closed_count) {
            char text[URL_TEXT_MAX];
            url_format(&BR->closed[--BR->closed_count], text, sizeof(text), true);
            br_new_tab(text, true);
        }
    } else if (strcmp(l, "History") == 0) br_go_text(t, "ojjy:history");
    else if (strcmp(l, "Bookmarks") == 0) br_go_text(t, "ojjy:bookmarks");
    else if (strcmp(l, "Downloads") == 0) br_go_text(t, "ojjy:downloads");
    else if (strcmp(l, "Settings") == 0) br_go_text(t, "ojjy:settings");
    else if (strcmp(l, "Find...") == 0) { BR->find_open = true; BR->find[0] = '\0'; BR->mark_count = 0; }
    else if (strcmp(l, "Zoom In") == 0) zoom_step(1);
    else if (strcmp(l, "Zoom Out") == 0) zoom_step(-1);
    else if (strcmp(l, "Actual Size") == 0) br_set_zoom(100);
    else if (strcmp(l, "View Page Source") == 0) br_view_source();
    else if (strcmp(l, "Show Bookmarks Bar") == 0 || strcmp(l, "Hide Bookmarks Bar") == 0) {
        setting_set_bmbar(!setting_bmbar());
        BR->show_bmbar = setting_bmbar();
    } else if (strcmp(l, "About ojjyOS Web") == 0) br_go_text(t, "ojjy:about");
    else if (strcmp(l, "Back") == 0) br_history_go(t, -1);
    else if (strcmp(l, "Forward") == 0) br_history_go(t, 1);
    else if (strcmp(l, "Reload") == 0) br_reload(t);
    else if (strcmp(l, "Open Link in New Tab") == 0) br_follow_link(BR->menu_link, true);
    else if (strcmp(l, "Copy Link Address") == 0 && BR->menu_link >= 0) {
        Url base, target;
        base_url(&base);
        char text[URL_TEXT_MAX];
        if (url_resolve(&base, BR->doc->links[BR->menu_link], &target)) {
            url_format(&target, text, sizeof(text), true);
            ui_clipboard_set(text);
        }
    } else if (strcmp(l, "Bookmark This Page") == 0 && t) {
        char u[URL_TEXT_MAX];
        tab_url_text(t, u, sizeof(u));
        bookmark_add(t->title, u);
    }
    ui_mark_dirty();
}

static void toggle_bookmark(void)
{
    Tab *t = active_tab();
    if (!t || !t->url.scheme[0]) return;
    char u[URL_TEXT_MAX];
    tab_url_text(t, u, sizeof(u));
    int i = bookmark_find(u);
    if (i >= 0) bookmark_remove(i);
    else bookmark_add(t->title, u);
    ui_mark_dirty();
}

bool browser_titlebar_click(int x, int y)
{
    if (!BR || !BR->opened) return false;
    bool close;
    int tab = ui_tab_hit(x, y, &close);
    if (tab >= 0) {
        if (close) br_close_tab(tab);
        else br_activate(tab);
        return true;
    }
    if (ui_toolbar_hit(x, y) == TB_NEWTAB) { br_new_tab("ojjy:newtab", true); return true; }
    return false;
}

bool browser_click(int x, int y)
{
    if (!BR || !BR->opened) return false;
    if (!in_rect(x, y, BR->x, BR->y, BR->w, BR->h)) return false;
    Tab *t = active_tab();
    if (BR->menu != MENU_NONE) {
        int item = ui_menu_hit(x, y);
        int menu = BR->menu;
        if (item >= 0) br_menu_action(menu, item);
        else BR->menu = MENU_NONE;
        ui_mark_dirty();
        return true;
    }
    if (BR->omni_focus && BR->sugg_count) {
        int s = ui_sugg_hit(x, y);
        if (s >= 0) { BR->sugg_sel = s; omni_commit(); return true; }
    }
    if (browser_titlebar_click(x, y)) return true;
    int button;
    if (BR->find_open && ui_find_hit(x, y, &button)) {
        if (button == 1) find_step(-1);
        else if (button == 2) find_step(1);
        else if (button == 3) { BR->find_open = false; BR->mark_count = 0; }
        ui_mark_dirty();
        return true;
    }
    int tb = ui_toolbar_hit(x, y);
    if (tb > 0) {
        switch (tb) {
            case TB_BACK: br_history_go(t, -1); break;
            case TB_FWD: br_history_go(t, 1); break;
            case TB_RELOAD: if (t && t->loading) br_stop(t); else br_reload(t); break;
            case TB_HOME: br_go_text(t, "ojjy:newtab"); break;
            case TB_STAR: toggle_bookmark(); break;
            case TB_MENU: BR->menu = MENU_MAIN; BR->menu_hover = -1; break;
            case TB_LOCK: BR->menu = MENU_PAGE_INFO; BR->menu_hover = -1; break;
            case TB_OMNI:
                if (!BR->omni_focus) {
                    BR->omni_focus = true;
                    BR->omni_all = true;
                    if (t) tab_url_text(t, BR->omni, sizeof(BR->omni));
                    BR->omni_caret = (int)strlen(BR->omni);
                    BR->sugg_count = 0;
                } else {
                    BR->omni_all = false;
                }
                BR->field = -1;
                break;
            default: break;
        }
        ui_mark_dirty();
        return true;
    }
    int bm = ui_bookmark_hit(x, y);
    if (bm >= 0) {
        const Bookmark *b = bookmark_get(bm);
        if (b) br_go_text(t, b->url);
        return true;
    }
    if (BR->omni_focus) { BR->omni_focus = false; BR->sugg_count = 0; if (t) tab_url_text(t, BR->omni, sizeof(BR->omni)); }
    if (!t || !in_rect(x, y, BR->cx, BR->cy, BR->cw, BR->ch) || !BR->doc_laid_out) { ui_mark_dirty(); return true; }
    int32_t px = x - BR->cx, py = y - BR->cy + t->scroll;
    int f = web_field_at(BR->doc, px, py);
    if (f >= 0) { field_activate(f); return true; }
    int link = web_link_at(BR->doc, px, py);
    BR->field = -1;
    if (link >= 0) {
        br_follow_link(link, ui_super_held());
        return true;
    }
    ui_mark_dirty();
    return true;
}

bool browser_right_click(int x, int y)
{
    if (!BR || !BR->opened || !in_rect(x, y, BR->x, BR->y, BR->w, BR->h)) return false;
    Tab *t = active_tab();
    BR->menu_link = -1;
    if (t && BR->doc_laid_out && in_rect(x, y, BR->cx, BR->cy, BR->cw, BR->ch))
        BR->menu_link = web_link_at(BR->doc, x - BR->cx, y - BR->cy + t->scroll);
    BR->menu = MENU_CONTEXT;
    BR->menu_x = x;
    BR->menu_y = y;
    BR->menu_hover = -1;
    ui_mark_dirty();
    return true;
}

bool browser_hover(int x, int y)
{
    if (!BR || !BR->opened) return false;
    bool changed = false;
    bool close;
    int tab = ui_tab_hit(x, y, &close);
    int hc = close ? tab : -1;
    if (tab != BR->hover_tab || hc != BR->hover_close) { BR->hover_tab = tab; BR->hover_close = hc; changed = true; }
    int tb = ui_toolbar_hit(x, y);
    if (tb != BR->hover_button) { BR->hover_button = tb; changed = true; }
    if (BR->menu != MENU_NONE) {
        int item = ui_menu_hit(x, y);
        if (item != BR->menu_hover && item >= 0) { BR->menu_hover = item; changed = true; }
    }
    if (BR->omni_focus && BR->sugg_count) {
        int s = ui_sugg_hit(x, y);
        if (s >= 0 && s != BR->sugg_sel) { BR->sugg_sel = s; changed = true; }
    }
    Tab *t = active_tab();
    int link = -1;
    if (t && BR->doc_laid_out && in_rect(x, y, BR->cx, BR->cy, BR->cw, BR->ch) && BR->menu == MENU_NONE)
        link = web_link_at(BR->doc, x - BR->cx, y - BR->cy + t->scroll);
    if (link != BR->hover_link) {
        BR->hover_link = link;
        BR->hover_url[0] = '\0';
        if (link >= 0) {
            Url base, target;
            base_url(&base);
            if (url_resolve(&base, BR->doc->links[link], &target)) url_format(&target, BR->hover_url, sizeof(BR->hover_url), true);
            else strncpy(BR->hover_url, BR->doc->links[link], sizeof(BR->hover_url) - 1);
        }
        changed = true;
    }
    return changed;
}

void browser_scroll(int pixels)
{
    Tab *t = active_tab();
    if (!t || BR->menu == MENU_SELECT) return;
    t->scroll += pixels;
    if (t->scroll < 0) t->scroll = 0;
    ui_mark_dirty();
}

static void edit_insert(char *buf, size_t size, int *caret, const char *s)
{
    size_t n = strlen(buf), k = strlen(s);
    if (*caret < 0) *caret = 0;
    if ((size_t)*caret > n) *caret = (int)n;
    if (n + k >= size) k = size - n - 1;
    memmove(buf + *caret + k, buf + *caret, n - (size_t)*caret + 1);
    memcpy(buf + *caret, s, k);
    *caret += (int)k;
}

static bool edit_key(char *buf, size_t size, int *caret, KeyCode key, char ascii, uint8_t mods)
{
    int n = (int)strlen(buf);
    if (*caret > n) *caret = n;
    if (*caret < 0) *caret = 0;
    switch (key) {
    case KEY_BACKSPACE:
        if (*caret > 0) {
            int start = *caret - 1;
            while (start > 0 && ((unsigned char)buf[start] & 0xC0) == 0x80) start--;
            if (mods & INPUT_MOD_ALT) { while (start > 0 && buf[start - 1] != ' ') start--; }
            memmove(buf + start, buf + *caret, (size_t)(n - *caret + 1));
            *caret = start;
        }
        return true;
    case KEY_DELETE:
        if (*caret < n) memmove(buf + *caret, buf + *caret + 1, (size_t)(n - *caret));
        return true;
    case KEY_LEFT:
        if (mods & INPUT_MOD_SUPER) *caret = 0;
        else if (*caret > 0) (*caret)--;
        return true;
    case KEY_RIGHT:
        if (mods & INPUT_MOD_SUPER) *caret = n;
        else if (*caret < n) (*caret)++;
        return true;
    case KEY_HOME: *caret = 0; return true;
    case KEY_END: *caret = n; return true;
    default: break;
    }
    if (ascii >= 32 && ascii <= 126) {
        char c[2] = {ascii, 0};
        edit_insert(buf, size, caret, c);
        return true;
    }
    return false;
}

static bool omni_key(KeyCode key, char ascii, uint8_t mods)
{
    Tab *t = active_tab();
    if (key == KEY_ENTER) { omni_commit(); return true; }
    if (key == KEY_ESCAPE) {
        if (BR->sugg_count) BR->sugg_count = 0;
        else { BR->omni_focus = false; if (t) tab_url_text(t, BR->omni, sizeof(BR->omni)); }
        return true;
    }
    if (key == KEY_DOWN) { if (BR->sugg_count) BR->sugg_sel = (BR->sugg_sel + 1) % BR->sugg_count; return true; }
    if (key == KEY_UP) { if (BR->sugg_count) BR->sugg_sel = (BR->sugg_sel + BR->sugg_count - 1) % BR->sugg_count; return true; }
    if (key == KEY_TAB) return true;
    if (BR->omni_all) {
        BR->omni_all = false;
        if (key == KEY_LEFT) { BR->omni_caret = 0; return true; }
        if (key == KEY_RIGHT) { BR->omni_caret = (int)strlen(BR->omni); return true; }
        if (key == KEY_BACKSPACE || key == KEY_DELETE || (ascii >= 32 && ascii <= 126)) { BR->omni[0] = '\0'; BR->omni_caret = 0; }
        if (key == KEY_BACKSPACE || key == KEY_DELETE) { br_omnibox_suggest(); return true; }
    }
    if (edit_key(BR->omni, sizeof(BR->omni), &BR->omni_caret, key, ascii, mods)) {
        if (key != KEY_LEFT && key != KEY_RIGHT && key != KEY_HOME && key != KEY_END) br_omnibox_suggest();
    }
    return true;
}

static int next_text_field(int from)
{
    for (int i = from + 1; i < BR->doc->field_count; i++) {
        WebField *f = &BR->doc->fields[i];
        if ((f->kind == FIELD_TEXT || f->kind == FIELD_PASSWORD || f->kind == FIELD_TEXTAREA) && !f->disabled) {
            for (int k = 0; k < BR->doc->item_count; k++)
                if (BR->doc->items[k].kind == WI_FIELD && BR->doc->items[k].ref == i) return i;
        }
    }
    return -1;
}

static void scroll_to_field(int f)
{
    Tab *t = active_tab();
    for (int k = 0; t && k < BR->doc->item_count; k++) {
        const WebItem *it = &BR->doc->items[k];
        if (it->kind != WI_FIELD || it->ref != f) continue;
        if (it->y < t->scroll || it->y + it->h > t->scroll + BR->ch) t->scroll = it->y - BR->ch / 3;
        if (t->scroll < 0) t->scroll = 0;
        return;
    }
}

bool browser_key(KeyCode key, char ascii, uint8_t modifiers)
{
    if (!BR || !BR->opened) return false;
    Tab *t = active_tab();
    ui_mark_dirty();
    if ((modifiers & INPUT_MOD_CTRL) && key == KEY_TAB) {
        int dir = (modifiers & INPUT_MOD_SHIFT) ? -1 : 1;
        for (int k = 1; k <= MAX_TABS; k++) {
            int i = (BR->active + dir * k + MAX_TABS * 2) % MAX_TABS;
            if (BR->tabs[i].used) { br_activate(i); break; }
        }
        return true;
    }
    if (BR->menu != MENU_NONE) {
        const char *labels[20];
        int n = BR->menu == MENU_SELECT ? BR->doc->fields[BR->menu_field].option_count : ui_menu_items(BR->menu, labels, 20);
        if (key == KEY_ESCAPE) { BR->menu = MENU_NONE; return true; }
        if (key == KEY_DOWN && n) { BR->menu_hover = (BR->menu_hover + 1) % n; return true; }
        if (key == KEY_UP && n) { BR->menu_hover = (BR->menu_hover + n - 1) % n; return true; }
        if (key == KEY_ENTER) { br_menu_action(BR->menu, BR->menu_hover); return true; }
        return true;
    }
    if (BR->omni_focus) return omni_key(key, ascii, modifiers);
    if (BR->find_open) {
        if (key == KEY_ESCAPE) { BR->find_open = false; BR->mark_count = 0; return true; }
        if (key == KEY_ENTER) { find_step((modifiers & INPUT_MOD_SHIFT) ? -1 : 1); return true; }
        int caret = (int)strlen(BR->find);
        if (edit_key(BR->find, sizeof(BR->find), &caret, key, ascii, modifiers)) {
            BR->mark_cur = 0;
            br_find_update();
            if (BR->mark_count) find_step(0);
            return true;
        }
    }
    if (BR->field >= 0 && BR->field < BR->doc->field_count) {
        WebField *f = &BR->doc->fields[BR->field];
        if (key == KEY_ESCAPE) { BR->field = -1; return true; }
        if (key == KEY_TAB) {
            int nf = next_text_field(BR->field);
            BR->field = nf;
            if (nf >= 0) { BR->field_caret = (int)strlen(BR->doc->fields[nf].value); scroll_to_field(nf); }
            return true;
        }
        if (key == KEY_ENTER) {
            if (f->kind == FIELD_TEXTAREA) { edit_insert(f->value, sizeof(f->value), &BR->field_caret, "\n"); return true; }
            int submitter = -1;
            for (int i = 0; i < BR->doc->field_count; i++)
                if (BR->doc->fields[i].form == f->form && BR->doc->fields[i].kind == FIELD_SUBMIT) { submitter = i; break; }
            int form = f->form;
            BR->field = -1;
            submit_form(form, submitter);
            return true;
        }
        if (edit_key(f->value, sizeof(f->value), &BR->field_caret, key, ascii, modifiers)) return true;
    }
    if (!t) return false;
    int step = BR->ch - 60;
    switch (key) {
        case KEY_UP: t->scroll -= 48; break;
        case KEY_DOWN: t->scroll += 48; break;
        case KEY_PAGEUP: t->scroll -= step; break;
        case KEY_PAGEDOWN: t->scroll += step; break;
        case KEY_SPACE: t->scroll += (modifiers & INPUT_MOD_SHIFT) ? -step : step; break;
        case KEY_HOME: t->scroll = 0; break;
        case KEY_END: t->scroll = BR->doc->height; break;
        case KEY_ESCAPE: if (t->loading) br_stop(t); break;
        case KEY_TAB: {
            int nf = next_text_field(-1);
            if (nf >= 0) { BR->field = nf; BR->field_caret = (int)strlen(BR->doc->fields[nf].value); scroll_to_field(nf); }
            break;
        }
        default: return false;
    }
    if (t->scroll < 0) t->scroll = 0;
    return true;
}

bool browser_shortcut(KeyCode key, uint8_t modifiers)
{
    if (!BR || !BR->opened) return false;
    Tab *t = active_tab();
    bool shift = (modifiers & INPUT_MOD_SHIFT) != 0;
    ui_mark_dirty();
    switch (key) {
    case KEY_T:
        if (shift) {
            if (BR->closed_count) {
                char text[URL_TEXT_MAX];
                url_format(&BR->closed[--BR->closed_count], text, sizeof(text), true);
                br_new_tab(text, true);
            }
        } else br_new_tab("ojjy:newtab", true);
        return true;
    case KEY_W: br_close_tab(BR->active); return true;
    case KEY_L:
        BR->omni_focus = true;
        BR->omni_all = true;
        if (t) tab_url_text(t, BR->omni, sizeof(BR->omni));
        BR->omni_caret = (int)strlen(BR->omni);
        BR->sugg_count = 0;
        BR->field = -1;
        return true;
    case KEY_R: br_reload(t); return true;
    case KEY_PERIOD: br_stop(t); return true;
    case KEY_LBRACKET:
        if (shift) { for (int k = 1; k < MAX_TABS; k++) { int i = (BR->active - k + MAX_TABS) % MAX_TABS; if (BR->tabs[i].used) { br_activate(i); break; } } }
        else br_history_go(t, -1);
        return true;
    case KEY_RBRACKET:
        if (shift) { for (int k = 1; k < MAX_TABS; k++) { int i = (BR->active + k) % MAX_TABS; if (BR->tabs[i].used) { br_activate(i); break; } } }
        else br_history_go(t, 1);
        return true;
    case KEY_D: toggle_bookmark(); return true;
    case KEY_F: BR->find_open = true; BR->field = -1; BR->omni_focus = false; br_find_update(); return true;
    case KEY_G: if (BR->find_open) find_step(shift ? -1 : 1); return true;
    case KEY_Y: br_go_text(t, "ojjy:history"); return true;
    case KEY_J: if (shift) { br_go_text(t, "ojjy:downloads"); return true; } return false;
    case KEY_B: if (shift) { setting_set_bmbar(!setting_bmbar()); BR->show_bmbar = setting_bmbar(); return true; } return false;
    case KEY_U: br_view_source(); return true;
    case KEY_EQUALS: zoom_step(1); return true;
    case KEY_MINUS: zoom_step(-1); return true;
    case KEY_0: br_set_zoom(100); return true;
    case KEY_COMMA: br_go_text(t, "ojjy:settings"); return true;
    case KEY_UP: if (t) t->scroll = 0; return true;
    case KEY_DOWN: if (t) t->scroll = BR->doc->height; return true;
    case KEY_A: if (BR->omni_focus) { BR->omni_all = true; return true; } return false;
    case KEY_C:
        if (BR->omni_focus) { ui_clipboard_set(BR->omni); return true; }
        return false;
    case KEY_V: {
        const char *clip = ui_clipboard_get();
        if (!clip || !clip[0]) return false;
        if (BR->omni_focus) {
            if (BR->omni_all) { BR->omni[0] = '\0'; BR->omni_caret = 0; BR->omni_all = false; }
            edit_insert(BR->omni, sizeof(BR->omni), &BR->omni_caret, clip);
            br_omnibox_suggest();
            return true;
        }
        if (BR->field >= 0) {
            WebField *f = &BR->doc->fields[BR->field];
            edit_insert(f->value, sizeof(f->value), &BR->field_caret, clip);
            return true;
        }
        return false;
    }
    default:
        break;
    }
    if (key >= KEY_1 && key <= KEY_9) {
        int want = key - KEY_1;
        int idx = -1, k = 0;
        for (int i = 0; i < MAX_TABS; i++) {
            if (!BR->tabs[i].used) continue;
            if (key == KEY_9 || k == want) idx = i;
            if (k == want && key != KEY_9) break;
            k++;
        }
        if (idx >= 0) br_activate(idx);
        return true;
    }
    return false;
}
