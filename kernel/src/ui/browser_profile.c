/*
 * ojjyOS v3 - Web browser profile: bookmarks, history, downloads list,
 * settings and the cookie jar, stored per user in ~/Library/Web/.
 * Files are small line-based text (fields separated by tabs); a corrupt
 * line is skipped, never trusted.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "ui/browser_int.h"
#include "fs/vfs.h"
#include "users.h"
#include "string.h"
#include "memory.h"
#include "drivers/rtc.h"
#include "net/net.h"

#define MAX_BOOKMARKS 120
#define MAX_HISTORY   400
#define MAX_DOWNLOADS 40
#define MAX_COOKIES   300

typedef struct {
    char domain[64];
    char path[64];
    char name[64];
    char value[400];
    uint32_t expires;           /* 0 = session */
    bool secure, host_only, used;
} Cookie;

typedef struct {
    Bookmark bookmarks[MAX_BOOKMARKS];
    int nbook;
    HistEntry history[MAX_HISTORY];             /* oldest first */
    int nhist;
    Download downloads[MAX_DOWNLOADS];
    int ndown;
    Cookie cookies[MAX_COOKIES];
    int search;
    bool bmbar;
    bool loaded;
    char user[64];
    char dir[160];
    bool dirty;
    uint64_t dirty_ms;
} Profile;

static Profile *P;

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */

static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

uint32_t profile_now(void)
{
    RtcTime t;
    rtc_read_time(&t);
    int64_t days = days_from_civil(t.year, t.month, t.day);
    int64_t s = days * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
    return s > 0 ? (uint32_t)s : 0;
}

/* ------------------------------------------------------------------ */
/* Files                                                               */
/* ------------------------------------------------------------------ */

static void scopy(char *dst, size_t size, const char *src)
{
    size_t n = 0;
    while (src && src[n] && n + 1 < size) { dst[n] = src[n] == '\t' || src[n] == '\n' || src[n] == '\r' ? ' ' : src[n]; n++; }
    dst[n] = '\0';
}

static void path_of(char *out, size_t size, const char *file)
{
    out[0] = '\0';
    size_t n = strlen(P->dir);
    if (n + strlen(file) + 2 >= size) return;
    strcpy(out, P->dir);
    out[n] = '/';
    strcpy(out + n + 1, file);
}

const char *profile_dir(void) { return P ? P->dir : ""; }

static char *read_file(const char *file, size_t *len)
{
    static char *buf;
    if (!buf) buf = (char *)pmm_alloc_bytes(512 * 1024);
    if (!buf) return NULL;
    char path[200];
    path_of(path, sizeof(path), file);
    VfsFile *f = vfs_open(path, VFS_O_READ);
    if (!f) return NULL;
    size_t n = 0;
    ssize_t r;
    while (n < 512 * 1024 - 1 && (r = vfs_read(f, buf + n, 512 * 1024 - 1 - n)) > 0) n += (size_t)r;
    vfs_close(f);
    buf[n] = '\0';
    *len = n;
    return buf;
}

static char *wbuf;
static size_t wlen, wcap;

static void w_str(const char *s)
{
    size_t n = strlen(s);
    if (wlen + n + 1 >= wcap) return;
    memcpy(wbuf + wlen, s, n);
    wlen += n;
    wbuf[wlen] = '\0';
}

static void w_num(uint64_t v)
{
    char n[24];
    utoa(v, n, 10);
    w_str(n);
}

static void write_file(const char *file)
{
    char path[200];
    path_of(path, sizeof(path), file);
    VfsFile *f = vfs_open(path, VFS_O_WRITE | VFS_O_CREATE | VFS_O_TRUNC);
    if (!f) return;
    vfs_write(f, wbuf, wlen);
    vfs_close(f);
}

/* Split a line into tab-separated fields (in place). */
static int fields(char *line, char **out, int max)
{
    int n = 0;
    out[n++] = line;
    for (char *c = line; *c && n < max; c++) if (*c == '\t') { *c = '\0'; out[n++] = c + 1; }
    return n;
}

static uint32_t parse_u32(const char *s)
{
    uint64_t v = 0;
    for (; *s >= '0' && *s <= '9' && v < 0xFFFFFFFFull; s++) v = v * 10 + (uint64_t)(*s - '0');
    return (uint32_t)v;
}

static void load_lines(const char *file, void (*fn)(char **f, int n))
{
    size_t len;
    char *b = read_file(file, &len);
    if (!b) return;
    char *line = b;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || b[i] == '\n') {
            b[i] = '\0';
            char *f[8];
            if (*line) fn(f, fields(line, f, 8));
            line = b + i + 1;
        }
    }
}

static void on_bookmark(char **f, int n)
{
    if (n < 2 || P->nbook >= MAX_BOOKMARKS) return;
    scopy(P->bookmarks[P->nbook].title, sizeof(P->bookmarks[0].title), f[0]);
    scopy(P->bookmarks[P->nbook].url, sizeof(P->bookmarks[0].url), f[1]);
    P->nbook++;
}

static void on_history(char **f, int n)
{
    if (n < 4 || P->nhist >= MAX_HISTORY) return;
    HistEntry *h = &P->history[P->nhist++];
    h->when = parse_u32(f[0]);
    h->visits = (uint16_t)parse_u32(f[1]);
    scopy(h->title, sizeof(h->title), f[2]);
    scopy(h->url, sizeof(h->url), f[3]);
}

static void on_download(char **f, int n)
{
    if (n < 4 || P->ndown >= MAX_DOWNLOADS) return;
    Download *d = &P->downloads[P->ndown++];
    d->when = parse_u32(f[0]);
    d->size = parse_u32(f[1]);
    scopy(d->name, sizeof(d->name), f[2]);
    scopy(d->path, sizeof(d->path), f[3]);
}

static void on_setting(char **f, int n)
{
    if (n < 2) return;
    if (strcmp(f[0], "search") == 0) { int v = (int)parse_u32(f[1]); if (v >= 0 && v < SEARCH_ENGINES) P->search = v; }
    else if (strcmp(f[0], "bookmarks_bar") == 0) P->bmbar = f[1][0] == '1';
}

static void on_cookie(char **f, int n)
{
    if (n < 7) return;
    uint32_t exp = parse_u32(f[0]);
    if (!exp || exp < profile_now()) return;
    for (int i = 0; i < MAX_COOKIES; i++) {
        Cookie *c = &P->cookies[i];
        if (c->used) continue;
        c->used = true;
        c->expires = exp;
        c->secure = f[1][0] == '1';
        c->host_only = f[2][0] == '1';
        scopy(c->domain, sizeof(c->domain), f[3]);
        scopy(c->path, sizeof(c->path), f[4]);
        scopy(c->name, sizeof(c->name), f[5]);
        scopy(c->value, sizeof(c->value), f[6]);
        return;
    }
}

void profile_load(void)
{
    if (!P) P = (Profile *)pmm_alloc_bytes(sizeof(Profile));
    if (!wbuf) { wcap = 256 * 1024; wbuf = (char *)pmm_alloc_bytes(wcap); }
    if (!P || !wbuf) return;
    const char *user = users_current_username();
    if (!user) user = "";
    if (P->loaded && strcmp(P->user, user) == 0) return;
    memset(P, 0, sizeof(*P));
    P->bmbar = true;
    scopy(P->user, sizeof(P->user), user);
    const char *home = users_current_home();
    if (home && home[0]) {
        char lib[160];
        vfs_join_path(lib, sizeof(lib), home, "Library");
        if (!vfs_isdir(lib)) vfs_mkdir(lib);
        vfs_join_path(P->dir, sizeof(P->dir), lib, "Web");
        if (!vfs_isdir(P->dir)) vfs_mkdir(P->dir);
    } else {
        strcpy(P->dir, "/Library/Web");
    }
    load_lines("bookmarks.txt", on_bookmark);
    load_lines("history.txt", on_history);
    load_lines("downloads.txt", on_download);
    load_lines("settings.txt", on_setting);
    load_lines("cookies.txt", on_cookie);
    if (!P->nbook) {
        /* first run: a few friendly starting points */
        bookmark_add("Wikipedia", "https://en.wikipedia.org/");
        bookmark_add("DuckDuckGo", "https://html.duckduckgo.com/html/");
        bookmark_add("Hacker News", "https://news.ycombinator.com/");
        bookmark_add("NPR Text", "https://text.npr.org/");
        bookmark_add("CNN Lite", "https://lite.cnn.com/");
        bookmark_add("ojjyOS", "ojjy:about");
    }
    P->loaded = true;
    P->dirty = false;
}

static void save_all(void)
{
    if (!P || !P->loaded || !wbuf) return;
    wlen = 0; wbuf[0] = '\0';
    for (int i = 0; i < P->nbook; i++) { w_str(P->bookmarks[i].title); w_str("\t"); w_str(P->bookmarks[i].url); w_str("\n"); }
    write_file("bookmarks.txt");
    wlen = 0; wbuf[0] = '\0';
    for (int i = 0; i < P->nhist; i++) {
        HistEntry *h = &P->history[i];
        w_num(h->when); w_str("\t"); w_num(h->visits); w_str("\t"); w_str(h->title); w_str("\t"); w_str(h->url); w_str("\n");
    }
    write_file("history.txt");
    wlen = 0; wbuf[0] = '\0';
    for (int i = 0; i < P->ndown; i++) {
        Download *d = &P->downloads[i];
        w_num(d->when); w_str("\t"); w_num(d->size); w_str("\t"); w_str(d->name); w_str("\t"); w_str(d->path); w_str("\n");
    }
    write_file("downloads.txt");
    wlen = 0; wbuf[0] = '\0';
    w_str("search\t"); w_num((uint64_t)P->search); w_str("\n");
    w_str("bookmarks_bar\t"); w_str(P->bmbar ? "1" : "0"); w_str("\n");
    write_file("settings.txt");
    wlen = 0; wbuf[0] = '\0';
    uint32_t now = profile_now();
    for (int i = 0; i < MAX_COOKIES; i++) {
        Cookie *c = &P->cookies[i];
        if (!c->used || !c->expires || c->expires < now) continue;
        w_num(c->expires); w_str(c->secure ? "\t1\t" : "\t0\t"); w_str(c->host_only ? "1\t" : "0\t");
        w_str(c->domain); w_str("\t"); w_str(c->path); w_str("\t"); w_str(c->name); w_str("\t"); w_str(c->value); w_str("\n");
    }
    write_file("cookies.txt");
    P->dirty = false;
}

void profile_save_soon(void)
{
    if (!P) return;
    if (!P->dirty) P->dirty_ms = net_now();
    P->dirty = true;
}

void profile_poll(void)
{
    if (P && P->dirty && net_now() - P->dirty_ms > 3000) save_all();
}

/* ------------------------------------------------------------------ */
/* Bookmarks, history, downloads, settings                             */
/* ------------------------------------------------------------------ */

int bookmark_count(void) { return P ? P->nbook : 0; }
const Bookmark *bookmark_get(int i) { return P && i >= 0 && i < P->nbook ? &P->bookmarks[i] : NULL; }

int bookmark_find(const char *url)
{
    if (!P) return -1;
    for (int i = 0; i < P->nbook; i++) if (strcmp(P->bookmarks[i].url, url) == 0) return i;
    return -1;
}

void bookmark_add(const char *title, const char *url)
{
    if (!P || P->nbook >= MAX_BOOKMARKS || bookmark_find(url) >= 0) return;
    scopy(P->bookmarks[P->nbook].title, sizeof(P->bookmarks[0].title), title && title[0] ? title : url);
    scopy(P->bookmarks[P->nbook].url, sizeof(P->bookmarks[0].url), url);
    P->nbook++;
    profile_save_soon();
}

void bookmark_remove(int i)
{
    if (!P || i < 0 || i >= P->nbook) return;
    memmove(&P->bookmarks[i], &P->bookmarks[i + 1], sizeof(Bookmark) * (size_t)(P->nbook - i - 1));
    P->nbook--;
    profile_save_soon();
}

void history_add(const char *title, const char *url)
{
    if (!P || !url[0] || strncmp(url, "ojjy:", 5) == 0) return;
    uint32_t now = profile_now();
    for (int i = P->nhist - 1; i >= 0; i--) {
        if (strcmp(P->history[i].url, url) != 0) continue;
        HistEntry e = P->history[i];
        memmove(&P->history[i], &P->history[i + 1], sizeof(HistEntry) * (size_t)(P->nhist - i - 1));
        e.when = now;
        if (e.visits < 65000) e.visits++;
        if (title && title[0]) scopy(e.title, sizeof(e.title), title);
        P->history[P->nhist - 1] = e;
        profile_save_soon();
        return;
    }
    if (P->nhist == MAX_HISTORY) {
        memmove(&P->history[0], &P->history[1], sizeof(HistEntry) * (MAX_HISTORY - 1));
        P->nhist--;
    }
    HistEntry *e = &P->history[P->nhist++];
    scopy(e->title, sizeof(e->title), title && title[0] ? title : url);
    scopy(e->url, sizeof(e->url), url);
    e->when = now;
    e->visits = 1;
    profile_save_soon();
}

int history_count(void) { return P ? P->nhist : 0; }
const HistEntry *history_get(int i) { return P && i >= 0 && i < P->nhist ? &P->history[P->nhist - 1 - i] : NULL; }
void history_clear(void) { if (P) { P->nhist = 0; profile_save_soon(); } }

int download_count(void) { return P ? P->ndown : 0; }
const Download *download_get(int i) { return P && i >= 0 && i < P->ndown ? &P->downloads[P->ndown - 1 - i] : NULL; }

void download_add(const char *name, const char *path, uint32_t size)
{
    if (!P) return;
    if (P->ndown == MAX_DOWNLOADS) { memmove(&P->downloads[0], &P->downloads[1], sizeof(Download) * (MAX_DOWNLOADS - 1)); P->ndown--; }
    Download *d = &P->downloads[P->ndown++];
    scopy(d->name, sizeof(d->name), name);
    scopy(d->path, sizeof(d->path), path);
    d->size = size;
    d->when = profile_now();
    profile_save_soon();
}

int setting_search(void) { return P ? P->search : 0; }
void setting_set_search(int i) { if (P && i >= 0 && i < SEARCH_ENGINES) { P->search = i; profile_save_soon(); } }
bool setting_bmbar(void) { return P ? P->bmbar : true; }
void setting_set_bmbar(bool on) { if (P) { P->bmbar = on; profile_save_soon(); } }

/* ------------------------------------------------------------------ */
/* Cookies (RFC 6265 subset)                                           */
/* ------------------------------------------------------------------ */

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool ieq_prefix(const char *s, const char *p)
{
    for (; *p; s++, p++) if (lc(*s) != *p) return false;
    return true;
}

static bool domain_match(const char *host, const Cookie *c)
{
    if (c->host_only) return strcmp(host, c->domain) == 0;
    size_t hl = strlen(host), dl = strlen(c->domain);
    if (hl < dl) return false;
    if (strcmp(host + hl - dl, c->domain) != 0) return false;
    return hl == dl || host[hl - dl - 1] == '.';
}

static bool path_match(const char *req, const char *cp)
{
    size_t n = strlen(cp);
    if (strncmp(req, cp, n) != 0) return false;
    return req[n] == '\0' || req[n] == '?' || cp[n - 1] == '/' || req[n] == '/';
}

static void cookie_get(const Url *u, char *out, size_t size)
{
    out[0] = '\0';
    if (!P) return;
    if (strcmp(u->scheme, "http") != 0 && strcmp(u->scheme, "https") != 0) return;
    uint32_t now = profile_now();
    size_t o = 0;
    for (int i = 0; i < MAX_COOKIES; i++) {
        Cookie *c = &P->cookies[i];
        if (!c->used) continue;
        if (c->expires && c->expires < now) { c->used = false; continue; }
        if (c->secure && strcmp(u->scheme, "https") != 0) continue;
        if (!domain_match(u->host, c) || !path_match(u->path, c->path)) continue;
        size_t need = strlen(c->name) + strlen(c->value) + 3;
        if (o + need >= size) break;
        if (o) { out[o++] = ';'; out[o++] = ' '; }
        strcpy(out + o, c->name); o += strlen(c->name);
        out[o++] = '=';
        strcpy(out + o, c->value); o += strlen(c->value);
        out[o] = '\0';
    }
}

/* "Wed, 21 Oct 2026 07:28:00 GMT" -> unix seconds (0 on failure). */
static uint32_t parse_http_date(const char *s)
{
    static const char *const mon[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
    int day = -1, month = -1, year = -1, hh = 0, mm = 0, ss = 0;
    const char *p = s;
    while (*p) {
        while (*p && !((*p >= '0' && *p <= '9') || (lc(*p) >= 'a' && lc(*p) <= 'z'))) p++;
        if (!*p) break;
        if (*p >= '0' && *p <= '9') {
            int v = 0, digits = 0;
            const char *q = p;
            while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); q++; digits++; }
            if (*q == ':') {
                hh = v; q++;
                mm = 0; while (*q >= '0' && *q <= '9') { mm = mm * 10 + (*q - '0'); q++; }
                if (*q == ':') { q++; ss = 0; while (*q >= '0' && *q <= '9') { ss = ss * 10 + (*q - '0'); q++; } }
            } else if (digits >= 3) year = v;
            else if (day < 0) day = v;
            else year = v < 70 ? 2000 + v : 1900 + v;
            p = q;
        } else {
            for (int m = 0; m < 12; m++) if (ieq_prefix(p, mon[m])) month = m + 1;
            while (*p && lc(*p) >= 'a' && lc(*p) <= 'z') p++;
        }
    }
    if (day < 1 || month < 1 || year < 1970) return 0;
    int64_t t = days_from_civil(year, month, day) * 86400 + hh * 3600 + mm * 60 + ss;
    return t > 0 && t < 0xFFFFFFFFll ? (uint32_t)t : 0xFFFFFFF0u;
}

static void cookie_set(const Url *u, const char *v)
{
    if (!P) return;
    Cookie c;
    memset(&c, 0, sizeof(c));
    const char *p = v;
    /* name=value */
    const char *eq = p;
    while (*eq && *eq != '=' && *eq != ';') eq++;
    if (*eq != '=') return;
    size_t nl = (size_t)(eq - p);
    while (nl && p[nl - 1] == ' ') nl--;
    if (!nl || nl >= sizeof(c.name)) return;
    memcpy(c.name, p, nl);
    const char *vs = eq + 1;
    const char *ve = vs;
    while (*ve && *ve != ';') ve++;
    size_t vl = (size_t)(ve - vs);
    if (vl >= sizeof(c.value)) return;
    memcpy(c.value, vs, vl);
    c.host_only = true;
    scopy(c.domain, sizeof(c.domain), u->host);
    /* default path: the request path's directory */
    const char *last = u->path;
    for (const char *q = u->path; *q && *q != '?'; q++) if (*q == '/') last = q;
    size_t pl = (size_t)(last - u->path);
    if (pl == 0 || pl >= sizeof(c.path)) strcpy(c.path, "/");
    else { memcpy(c.path, u->path, pl); c.path[pl] = '\0'; }
    bool max_age = false;
    uint32_t now = profile_now();
    for (p = ve; *p;) {
        while (*p == ';' || *p == ' ') p++;
        if (!*p) break;
        const char *ae = p;
        while (*ae && *ae != ';') ae++;
        const char *av = p;
        while (av < ae && *av != '=') av++;
        const char *val = av < ae ? av + 1 : ae;
        char vbuf[96];
        size_t k = (size_t)(ae - val) < sizeof(vbuf) - 1 ? (size_t)(ae - val) : sizeof(vbuf) - 1;
        memcpy(vbuf, val, k);
        vbuf[k] = '\0';
        if (ieq_prefix(p, "domain")) {
            const char *d = vbuf;
            if (*d == '.') d++;
            char dom[64];
            size_t dl = 0;
            for (; d[dl] && dl < sizeof(dom) - 1; dl++) dom[dl] = lc(d[dl]);
            dom[dl] = '\0';
            /* the domain must cover the request host and have a dot */
            size_t hl = strlen(u->host);
            bool dot = false;
            for (size_t i = 0; i < dl; i++) if (dom[i] == '.') dot = true;
            if (!dot || hl < dl || strcmp(u->host + hl - dl, dom) != 0 || (hl > dl && u->host[hl - dl - 1] != '.')) return;
            strcpy(c.domain, dom);
            c.host_only = false;
        } else if (ieq_prefix(p, "path")) {
            if (vbuf[0] == '/') scopy(c.path, sizeof(c.path), vbuf);
        } else if (ieq_prefix(p, "max-age")) {
            bool neg = vbuf[0] == '-';
            uint32_t secs = parse_u32(neg ? vbuf + 1 : vbuf);
            c.expires = neg || secs == 0 ? 1 : (now + secs < now ? 0xFFFFFFF0u : now + secs);
            max_age = true;
        } else if (ieq_prefix(p, "expires") && !max_age) {
            c.expires = parse_http_date(vbuf);
            if (!c.expires) c.expires = 1;
        } else if (ieq_prefix(p, "secure")) {
            c.secure = true;
        }
        p = ae;
    }
    /* replace an existing cookie with the same name/domain/path */
    int free_slot = -1, oldest = -1;
    for (int i = 0; i < MAX_COOKIES; i++) {
        Cookie *o = &P->cookies[i];
        if (!o->used) { if (free_slot < 0) free_slot = i; continue; }
        if (strcmp(o->name, c.name) == 0 && strcmp(o->domain, c.domain) == 0 && strcmp(o->path, c.path) == 0) {
            if (c.expires && c.expires <= now) { o->used = false; profile_save_soon(); return; }
            c.used = true;
            *o = c;
            if (c.expires) profile_save_soon();
            return;
        }
        if (oldest < 0) oldest = i;
    }
    if (c.expires && c.expires <= now) return;
    int slot = free_slot >= 0 ? free_slot : oldest;
    if (slot < 0) return;
    c.used = true;
    P->cookies[slot] = c;
    if (c.expires) profile_save_soon();
}

void cookies_clear(void)
{
    if (!P) return;
    for (int i = 0; i < MAX_COOKIES; i++) P->cookies[i].used = false;
    profile_save_soon();
}

int cookies_count(void)
{
    int n = 0;
    if (P) for (int i = 0; i < MAX_COOKIES; i++) n += P->cookies[i].used;
    return n;
}

void profile_hooks_install(void) { http_set_cookie_hooks(cookie_get, cookie_set); }
