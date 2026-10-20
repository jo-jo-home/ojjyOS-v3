/*
 * Host render tool for the web engine: lays out an HTML file (plus its
 * stylesheets) with the real Inter web faces and paints it with the same
 * code the browser uses, writing a PPM image. Used to iterate on the
 * engine against saved real-world pages without booting the OS.
 *
 * usage: web_render page.html out.ppm [width] [scale] [sheet.css ...]
 * Created by Jonas Lee.
 */
#include "web/engine.h"
#include "web/paint.h"
#include "webfont.h"
#include "string.h"
#include "image/image.h"
#include "web/url.h"

int printf(const char *fmt, ...);
void *malloc(unsigned long n);
void *calloc(unsigned long n, unsigned long size);
typedef struct FILE FILE;
FILE *fopen(const char *p, const char *m);
unsigned long fread(void *b, unsigned long s, unsigned long n, FILE *f);
unsigned long fwrite(const void *b, unsigned long s, unsigned long n, FILE *f);
int fclose(FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int atoi(const char *s);
int system(const char *cmd);
char *getenv(const char *name);
int snprintf(char *b, unsigned long n, const char *f, ...);
long clock(void);
void serial_printf(const char *fmt, ...) { (void)fmt; }
void serial_puts(const char *s) { (void)s; }

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, 2);
    long n = ftell(f);
    fseek(f, 0, 0);
    char *b = (char *)malloc((unsigned long)n + 1);
    *len = fread(b, 1, (unsigned long)n, f);
    b[*len] = 0;
    fclose(f);
    return b;
}

static int pick_glyph(uint32_t cp) { return text_has_glyph(cp); }
static uint8_t m_pick(int px, int w, bool it, bool mono) { return webfont_pick(px, w, it, mono); }
static int m_width(const char *s, int len, uint8_t f) { return webfont_width(s, len, f); }
static int m_ascent(uint8_t f) { return webfont_ascent(f); }
static int m_height(uint8_t f) { return webfont_height(f); }
static bool m_glyph(uint32_t cp) { return pick_glyph(cp); }
static const WebFontMetrics fm = {m_pick, m_width, m_ascent, m_height, m_glyph};

static void draw_field(void *ctx, GfxSurface *s, const WebItem *it, int x, int y)
{
    WebDoc *d = (WebDoc *)ctx;
    const WebField *f = &d->fields[it->ref];
    if (f->kind == FIELD_CHECKBOX || f->kind == FIELD_RADIO) {
        gfx_round_rect(s, x, y, it->w, it->h, f->kind == FIELD_RADIO ? it->w / 2 : 3, 0xFFFFFF, 255);
        gfx_round_rect_stroke(s, x, y, it->w, it->h, f->kind == FIELD_RADIO ? it->w / 2 : 3, 0x767676, 255);
        return;
    }
    const char *t = f->kind == FIELD_SUBMIT || f->kind == FIELD_BUTTON || f->kind == FIELD_RESET ? f->label :
                    f->kind == FIELD_SELECT && f->selected >= 0 ? f->options[f->selected] :
                    f->value[0] ? f->value : f->placeholder;
    uint32_t c = f->value[0] || f->kind == FIELD_SUBMIT || f->kind == FIELD_SELECT ? (it->color & 0xFFFFFF) : 0x757575;
    webfont_draw(s, x + 1, y + (it->h - webfont_height(it->font)) / 2, t, (int)strlen(t), it->font, c, 255);
}

int main(int argc, char **argv)
{
    if (argc < 3) { printf("usage: web_render page.html out.ppm [width] [scale] [sheet.css ...]\n"); return 2; }
    size_t len;
    char *html = slurp(argv[1], &len);
    if (!html) { printf("cannot read %s\n", argv[1]); return 1; }
    int width = argc > 3 ? atoi(argv[3]) : 1000;
    int scale = argc > 4 ? atoi(argv[4]) : 1;
    text_set_density(scale);
    static WebArena dom, css, lay;
    arena_init(&dom, malloc(48u << 20), 48u << 20);
    arena_init(&css, malloc(32u << 20), 32u << 20);
    arena_init(&lay, malloc(24u << 20), 24u << 20);
    int cap = 200000;
    WebItem *items = (WebItem *)malloc(sizeof(WebItem) * (unsigned long)cap);
    static WebDoc doc;
    web_doc_init(&doc, &dom, &css, &lay, items, cap, &fm);
    long t0 = clock();
    web_doc_parse(&doc, html, len, false, false);
    long t1 = clock();
    for (int i = 5; i < argc; i++) {
        size_t cl;
        char *c = slurp(argv[i], &cl);
        web_doc_set_sheet(&doc, i - 5, c, c ? cl : 0, NULL);
    }
    web_doc_layout(&doc, width, 900, 100, false);
    /* inline <svg>s rasterize like the browser does (twice the laid-out size) */
    static bool scratch_set;
    if (!scratch_set) { image_set_scratch(malloc(64u << 20), 64u << 20); scratch_set = true; }
    int svgs = 0;
    for (int i = 0; i < doc.image_count; i++) {
        WebImage *im = &doc.images[i];
        if (!im->svg || im->state != IMG_PENDING) continue;
        int rw = im->rw > 0 ? im->rw : 16, rh = im->rh > 0 ? im->rh : 16;
        int pw = rw * scale * 2, ph = rh * scale * 2;
        while (pw > 2048 || ph > 2048) { pw /= 2; ph /= 2; }
        uint32_t *px = (uint32_t *)malloc((unsigned long)pw * (unsigned long)ph * 4);
        if (px && image_render_svg((const uint8_t *)im->svg, im->svg_len, px, pw, ph, im->color)) {
            im->pixels = px; im->w = (int16_t)rw; im->h = (int16_t)rh; im->pw = (int16_t)pw; im->ph = (int16_t)ph;
            im->state = IMG_READY;
            svgs++;
        } else {
            im->state = IMG_FAILED;
        }
    }
    if (svgs) web_doc_layout(&doc, width, 900, 100, false);
    /* WEB_REPEAT=n: lay out n more times (profiling) */
    for (int rep = getenv("WEB_REPEAT") ? atoi(getenv("WEB_REPEAT")) : 0; rep > 0; rep--)
        web_doc_layout(&doc, width, 900, 100, (rep & 1) != 0);  /* alternating media forces a restyle */
    if (getenv("WEB_REPEAT")) web_doc_layout(&doc, width, 900, 100, false);
    /* WEB_BENCH=n: time n restyles split into sheet parsing and the style tree */
    if (getenv("WEB_BENCH")) {
        long tp = 0, ts = 0;
        for (int rep = atoi(getenv("WEB_BENCH")); rep > 0; rep--) {
            long a0 = clock();
            arena_reset(doc.css_arena);
            CssMedia m = {width, 900, false, 100, doc.tree.quirks};
            doc.css = css_new(doc.css_arena, &m);
            for (int i = 0; i < doc.sheet_count; i++)
                if (doc.sheets[i].text && css_media_matches(doc.css, doc.sheets[i].media))
                    css_add_sheet_base(doc.css, doc.sheets[i].text, doc.sheets[i].len, doc.sheets[i].base);
            long a1 = clock();
            css_style_tree(doc.css, &doc.tree);
            long a2 = clock();
            tp += a1 - a0;
            ts += a2 - a1;
        }
        printf("bench parse=%ldms style=%ldms (total over runs)\n", tp / 1000, ts / 1000);
        doc.css_dirty = true;
        web_doc_layout(&doc, width, 900, 100, false);
    }
    /* WEB_BASE=<page url>: download and decode the page's images (curl), relayout */
    const char *base = getenv("WEB_BASE");
    if (base) {
        image_set_scratch(malloc(64u << 20), 64u << 20);
        Url bu;
        url_parse(base, &bu);
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < doc.image_count; i++) {
                WebImage *im = &doc.images[i];
                if (im->state != IMG_PENDING) continue;
                Url u;
                char abs[2048], cmd[4200], path[64];
                if (!url_resolve(&bu, im->url, &u)) { im->state = IMG_FAILED; continue; }
                url_format(&u, abs, sizeof(abs), false);
                snprintf(path, sizeof(path), "/tmp/wr_img_%d", i);
                snprintf(cmd, sizeof(cmd), "curl -sL -m 15 -A 'ojjyOSWeb/3.1' -o %s '%s'", path, abs);
                system(cmd);
                size_t il;
                char *data = slurp(path, &il);
                int w, h;
                if (data && image_info((uint8_t *)data, il, &w, &h) && (long)w * h < 20000000) {
                    uint32_t *px = (uint32_t *)malloc((unsigned long)w * h * 4);
                    if (image_decode((uint8_t *)data, il, px, (size_t)w * h)) {
                        im->pixels = px; im->w = (int16_t)w; im->h = (int16_t)h; im->pw = (int16_t)w; im->ph = (int16_t)h;
                        im->state = IMG_READY;
                        continue;
                    }
                }
                im->state = IMG_FAILED;
            }
            web_doc_layout(&doc, width, 900, 100, false);
        }
    }
    long t2 = clock();
    int height = doc.height + 16;
    if (height > 12000) height = 12000;
    if (height < 200) height = 200;
    GfxSurface s;
    s.width = width * scale;
    s.height = height * scale;
    s.pitch = s.width;
    s.pixels = (uint32_t *)calloc((unsigned long)s.width * (unsigned long)s.height, 4);
    s.clip_x1 = s.clip_y1 = 0;
    s.clip_x2 = s.width;
    s.clip_y2 = s.height;
    s.scale = scale;
    WebPaintOpts opts = {draw_field, &doc, -1, NULL, 0, -1};
    web_paint(&s, &doc, 0, 0, 0, 0, 0, width, height, &opts);
    long t3 = clock();
    printf("title='%s' nodes=%d rules=%d items=%d links=%d fields=%d images=%d sheets=%d/%d height=%d "
           "parse=%ldms layout=%ldms paint=%ldms js=%d trunc=%d dom=%luKB css=%luKB lay=%luKB\n",
           doc.title, doc.tree.node_count, css_rule_count(doc.css), doc.item_count, doc.link_count, doc.field_count,
           doc.image_count, doc.sheet_count, doc.sheet_url_count, doc.height, (t1 - t0) / 1000, (t2 - t1) / 1000,
           (t3 - t2) / 1000, doc.needs_js, doc.truncated, (unsigned long)(dom.used >> 10),
           (unsigned long)(css.used >> 10), (unsigned long)(lay.used >> 10));
    for (int i = 0; i < doc.sheet_url_count; i++) printf("sheet %s\n", doc.sheet_urls[i]);
    /* WEB_DUMP=text: list the display items whose text contains it */
    const char *dump = getenv("WEB_DUMP");
    for (int i = 0; dump && i < doc.item_count; i++) {
        const WebItem *it = &doc.items[i];
        if (it->kind != WI_TEXT || !it->text) continue;
        size_t dl = strlen(dump);
        bool hit = false;
        for (size_t k = 0; !hit && k + dl <= (size_t)it->len; k++) hit = memcmp(it->text + k, dump, dl) == 0;
        if (!hit) continue;
        printf("item %d x=%d y=%d w=%d h=%d font=%d '%.*s'\n", i, (int)it->x, (int)it->y, (int)it->w, (int)it->h,
               (int)it->font, (int)it->len, it->text);
    }
    FILE *o = fopen(argv[2], "wb");
    char hdr[64];
    char num[16];
    strcpy(hdr, "P6\n");
    utoa((uint64_t)s.width, num, 10); strcpy(hdr + strlen(hdr), num); strcpy(hdr + strlen(hdr), " ");
    utoa((uint64_t)s.height, num, 10); strcpy(hdr + strlen(hdr), num); strcpy(hdr + strlen(hdr), "\n255\n");
    fwrite(hdr, 1, strlen(hdr), o);
    unsigned char *row = (unsigned char *)malloc((unsigned long)s.width * 3);
    for (int y = 0; y < s.height; y++) {
        for (int x = 0; x < s.width; x++) {
            uint32_t p = s.pixels[y * s.pitch + x];
            row[x * 3] = (unsigned char)(p >> 16); row[x * 3 + 1] = (unsigned char)(p >> 8); row[x * 3 + 2] = (unsigned char)p;
        }
        fwrite(row, 1, (unsigned long)s.width * 3, o);
    }
    fclose(o);
    return 0;
}
