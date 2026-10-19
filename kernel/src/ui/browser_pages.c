/*
 * ojjyOS v3 - Web browser built-in pages (New Tab, History, Bookmarks,
 * Downloads, Settings, errors). They are ordinary HTML + CSS, rendered by
 * the same engine as every web page.
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "ui/browser_int.h"
#include "string.h"
#include "drivers/rtc.h"

const char *const search_names[SEARCH_ENGINES] = {"DuckDuckGo", "Bing", "Brave Search", "Wikipedia"};
const char *const search_urls[SEARCH_ENGINES] = {
    "https://html.duckduckgo.com/html/?q=",
    "https://www.bing.com/search?q=",
    "https://search.brave.com/search?q=",
    "https://en.wikipedia.org/w/index.php?search=",
};

typedef struct { char *b; size_t len, cap; } Out;

static bool strstr_simple(const char *h, const char *n)
{
    size_t nl = strlen(n);
    for (; *h; h++) if (strncmp(h, n, nl) == 0) return true;
    return false;
}

static void put(Out *o, const char *s)
{
    size_t n = strlen(s);
    if (o->len + n + 1 >= o->cap) n = o->cap - o->len - 1;
    memcpy(o->b + o->len, s, n);
    o->len += n;
    o->b[o->len] = '\0';
}

static void put_esc(Out *o, const char *s)
{
    for (; *s; s++) {
        char c[2] = {*s, 0};
        put(o, *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : *s == '&' ? "&amp;" : *s == '"' ? "&quot;" : c);
    }
}

static void put_num(Out *o, uint64_t v)
{
    char n[24];
    utoa(v, n, 10);
    put(o, n);
}

static const char STYLE[] =
    "<style>"
    ":root{--bg:#f6f8fb;--card:#ffffff;--text:#1f2328;--muted:#5f6b7a;--line:#e3e8ef;--accent:#1a73e8;--chip:#e8f0fe}"
    "@media (prefers-color-scheme: dark){:root{--bg:#1d2129;--card:#262b35;--text:#e8eaed;--muted:#9aa3b0;--line:#363c48;"
    "--accent:#8ab4f8;--chip:#2b3445}}"
    "body{margin:0;background:var(--bg);color:var(--text);font-family:Inter,system-ui,sans-serif;font-size:14px}"
    "a{color:var(--accent);text-decoration:none}"
    ".wrap{max-width:760px;margin:0 auto;padding:28px 24px}"
    "h1{font-size:24px;font-weight:600;margin:8px 0 20px}"
    "h2{font-size:14px;font-weight:600;color:var(--muted);margin:22px 0 8px}"
    ".card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:6px 0}"
    ".row{padding:10px 16px;border-bottom:1px solid var(--line)}"
    ".row:last-child{border-bottom:0}"
    ".muted{color:var(--muted);font-size:12px}"
    ".btn{display:inline-block;padding:6px 14px;border-radius:16px;background:var(--chip);color:var(--accent);margin:4px 6px 4px 0}"
    ".sel{background:var(--accent);color:#fff}"
    "</style>";

static void head(Out *o, const char *title)
{
    put(o, "<!DOCTYPE html><html><head><title>");
    put_esc(o, title);
    put(o, "</title>");
    put(o, STYLE);
    put(o, "</head><body>");
}

static void letter_tile(Out *o, const char *title, const char *url)
{
    /* a coloured circle with the site's first letter, like Chrome's tiles */
    static const char *const colors[] = {"#1a73e8", "#e8710a", "#188038", "#d93025", "#9334e6", "#007b83", "#c5221f", "#1967d2"};
    unsigned h = 0;
    for (const char *c = url; *c; c++) h = h * 31 + (unsigned char)*c;
    char letter[2] = {title[0] ? title[0] : url[0], 0};
    if (letter[0] >= 'a' && letter[0] <= 'z') letter[0] = (char)(letter[0] - 32);
    put(o, "<a class=tile href=\"");
    put_esc(o, url);
    put(o, "\"><span class=ic style=\"background:");
    put(o, colors[h % 8]);
    put(o, "\">");
    put_esc(o, letter);
    put(o, "</span><span class=tl>");
    char t[24];
    size_t k = 0;
    for (; title[k] && k < 16; k++) t[k] = title[k];
    t[k] = '\0';
    if (title[k]) { t[k > 14 ? 14 : k] = '\0'; put_esc(o, t); put(o, "\xE2\x80\xA6"); }
    else put_esc(o, t);
    put(o, "</span></a>");
}

size_t page_newtab(char *buf, size_t cap)
{
    Out o = {buf, 0, cap};
    buf[0] = '\0';
    head(&o, "New Tab");
    put(&o, "<style>"
            ".hero{text-align:center;padding-top:90px}"
            ".logo{font-size:44px;font-weight:700;color:var(--accent);letter-spacing:-1px;margin-bottom:26px}"
            ".logo span{color:var(--text)}"
            ".box{display:inline-block;width:520px;max-width:90%;text-align:left}"
            "input[type=text]{width:100%;box-sizing:border-box;font-size:16px;padding:12px 20px;border:1px solid var(--line);"
            "border-radius:24px;background:var(--card);color:var(--text)}"
            ".tiles{display:flex;flex-wrap:wrap;justify-content:center;margin:36px auto 0;max-width:640px}"
            ".tile{display:block;width:104px;padding:14px 4px;text-align:center;color:var(--text);border-radius:12px}"
            ".ic{display:inline-block;width:44px;height:44px;line-height:44px;border-radius:22px;color:#fff;font-size:18px;"
            "font-weight:700;text-align:center}"
            ".tl{display:block;font-size:12px;margin-top:8px;color:var(--text)}"
            ".foot{text-align:center;margin-top:40px}"
            "</style><div class=hero><div class=logo>ojjyOS <span>Web</span></div><div class=box><form action=\"");
    int engine = setting_search();
    const char *su = search_urls[engine];
    /* form action = search URL without the query parameter name */
    char action[160];
    size_t k = 0;
    while (su[k] && su[k] != '?' && k < sizeof(action) - 1) { action[k] = su[k]; k++; }
    action[k] = '\0';
    char param[24] = "q";
    const char *qm = su;
    while (*qm && *qm != '?') qm++;
    if (*qm == '?') {
        qm++;
        size_t j = 0;
        while (qm[j] && qm[j] != '=' && j < sizeof(param) - 1) { param[j] = qm[j]; j++; }
        param[j] = '\0';
    }
    put_esc(&o, action);
    put(&o, "\" method=get><input type=text name=");
    put(&o, param);
    put(&o, " placeholder=\"Search ");
    put(&o, search_names[engine]);
    put(&o, " or type a URL\"></form></div></div><div class=tiles>");
    /* shortcuts: bookmarks first, then most visited history */
    int shown = 0;
    for (int i = 0; i < bookmark_count() && shown < 8; i++, shown++) letter_tile(&o, bookmark_get(i)->title, bookmark_get(i)->url);
    for (int pass = 0; pass < 1 && shown < 10; pass++)
        for (int i = 0; i < history_count() && shown < 10; i++) {
            const HistEntry *h = history_get(i);
            if (h->visits < 2 || bookmark_find(h->url) >= 0) continue;
            bool dup = false;
            for (int j = 0; j < i; j++) if (strcmp(history_get(j)->url, h->url) == 0) dup = true;
            if (dup) continue;
            letter_tile(&o, h->title, h->url);
            shown++;
        }
    put(&o, "</div><div class=foot><a class=btn href=\"ojjy:history\">History</a><a class=btn href=\"ojjy:bookmarks\">"
            "Bookmarks</a><a class=btn href=\"ojjy:downloads\">Downloads</a><a class=btn href=\"ojjy:settings\">Settings</a>"
            "</div></body></html>");
    return o.len;
}

static void date_label(Out *o, uint32_t when)
{
    /* seconds since 1970 -> "Monday, October 19, 2026" */
    static const char *const months[] = {"January", "February", "March", "April", "May", "June", "July", "August",
                                         "September", "October", "November", "December"};
    static const char *const days[] = {"Thursday", "Friday", "Saturday", "Sunday", "Monday", "Tuesday", "Wednesday"};
    int64_t z = when / 86400;
    int dow = (int)(z % 7);
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    put(o, days[dow]);
    put(o, ", ");
    put(o, months[m - 1]);
    put(o, " ");
    put_num(o, (uint64_t)d);
    put(o, ", ");
    put_num(o, (uint64_t)y);
}

static void put_time(Out *o, uint32_t when)
{
    int h = (int)((when / 3600) % 24), m = (int)((when / 60) % 60);
    char t[8];
    t[0] = (char)('0' + h / 10); t[1] = (char)('0' + h % 10); t[2] = ':'; t[3] = (char)('0' + m / 10); t[4] = (char)('0' + m % 10); t[5] = 0;
    put(o, t);
}

static void put_host(Out *o, const char *url)
{
    const char *h = url;
    for (const char *c = url; *c; c++) if (c[0] == '/' && c[1] == '/') { h = c + 2; break; }
    char host[96];
    size_t k = 0;
    while (h[k] && h[k] != '/' && k < sizeof(host) - 1) { host[k] = h[k]; k++; }
    host[k] = '\0';
    put_esc(o, host);
}

size_t page_history(char *buf, size_t cap)
{
    Out o = {buf, 0, cap};
    buf[0] = '\0';
    head(&o, "History");
    put(&o, "<div class=wrap><h1>History</h1><p><a class=btn href=\"ojjy:history?clear=1\">Clear browsing history</a></p>");
    int n = history_count();
    if (!n) put(&o, "<p class=muted>Pages you visit will appear here.</p>");
    uint32_t last_day = 0xFFFFFFFFu;
    bool open = false;
    for (int i = 0; i < n && o.len + 2048 < cap; i++) {
        const HistEntry *h = history_get(i);
        uint32_t day = h->when / 86400;
        if (day != last_day) {
            if (open) put(&o, "</div>");
            put(&o, "<h2>");
            date_label(&o, h->when);
            put(&o, "</h2><div class=card>");
            open = true;
            last_day = day;
        }
        put(&o, "<div class=row><span class=muted>");
        put_time(&o, h->when);
        put(&o, "</span> &nbsp; <a href=\"");
        put_esc(&o, h->url);
        put(&o, "\">");
        put_esc(&o, h->title);
        put(&o, "</a> <span class=muted>");
        put_host(&o, h->url);
        put(&o, "</span></div>");
    }
    if (open) put(&o, "</div>");
    put(&o, "</div></body></html>");
    return o.len;
}

size_t page_bookmarks(char *buf, size_t cap)
{
    Out o = {buf, 0, cap};
    buf[0] = '\0';
    head(&o, "Bookmarks");
    put(&o, "<div class=wrap><h1>Bookmarks</h1><p class=muted>Press \xE2\x8C\x98" "D or click the star in the address bar "
            "to bookmark a page.</p><div class=card>");
    if (!bookmark_count()) put(&o, "<div class=row class=muted>No bookmarks yet.</div>");
    for (int i = 0; i < bookmark_count() && o.len + 1024 < cap; i++) {
        const Bookmark *b = bookmark_get(i);
        put(&o, "<div class=row><a href=\"");
        put_esc(&o, b->url);
        put(&o, "\">");
        put_esc(&o, b->title);
        put(&o, "</a> <span class=muted>");
        put_host(&o, b->url);
        put(&o, "</span> &nbsp; <a class=muted href=\"ojjy:bookmarks?remove=");
        put_num(&o, (uint64_t)i);
        put(&o, "\">Remove</a></div>");
    }
    put(&o, "</div></div></body></html>");
    return o.len;
}

size_t page_downloads(char *buf, size_t cap)
{
    Out o = {buf, 0, cap};
    buf[0] = '\0';
    head(&o, "Downloads");
    put(&o, "<div class=wrap><h1>Downloads</h1><p class=muted>Files are saved to the Downloads folder in your home "
            "folder (open it in Finder).</p><div class=card>");
    if (!download_count()) put(&o, "<div class=row>Files you download will appear here.</div>");
    for (int i = 0; i < download_count() && o.len + 1024 < cap; i++) {
        const Download *d = download_get(i);
        put(&o, "<div class=row><b>");
        put_esc(&o, d->name);
        put(&o, "</b><br><span class=muted>");
        put_num(&o, (d->size + 1023) / 1024);
        put(&o, " KB &middot; ");
        put_esc(&o, d->path);
        put(&o, "</span></div>");
    }
    put(&o, "</div></div></body></html>");
    return o.len;
}

size_t page_settings(char *buf, size_t cap)
{
    Out o = {buf, 0, cap};
    buf[0] = '\0';
    head(&o, "Settings");
    put(&o, "<div class=wrap><h1>Settings</h1><h2>Search engine</h2><div class=card><div class=row>"
            "Used when you type words in the address bar.<br>");
    for (int i = 0; i < SEARCH_ENGINES; i++) {
        put(&o, "<a class=\"btn");
        if (i == setting_search()) put(&o, " sel");
        put(&o, "\" href=\"ojjy:settings?search=");
        put_num(&o, (uint64_t)i);
        put(&o, "\">");
        put(&o, search_names[i]);
        put(&o, "</a>");
    }
    put(&o, "<br><span class=muted>Google Search needs JavaScript, which ojjyOS Web does not run yet.</span></div></div>"
            "<h2>Appearance</h2><div class=card><div class=row>Bookmarks bar: <a class=\"btn");
    if (setting_bmbar()) put(&o, " sel");
    put(&o, "\" href=\"ojjy:settings?bar=1\">Show</a><a class=\"btn");
    if (!setting_bmbar()) put(&o, " sel");
    put(&o, "\" href=\"ojjy:settings?bar=0\">Hide</a></div></div>"
            "<h2>Privacy</h2><div class=card><div class=row>Cookies stored: ");
    put_num(&o, (uint64_t)cookies_count());
    put(&o, " &nbsp; <a class=btn href=\"ojjy:settings?cookies=clear\">Clear cookies</a></div><div class=row>History: ");
    put_num(&o, (uint64_t)history_count());
    put(&o, " pages &nbsp; <a class=btn href=\"ojjy:history?clear=1\">Clear history</a></div></div>"
            "<h2>Security</h2><div class=card><div class=row>Connections to https:// sites are encrypted with TLS 1.3. "
            "<b>Certificate checking is still being built</b>, so the browser cannot yet prove a site's identity: "
            "avoid typing passwords into sites for now.</div></div>"
            "<h2>About</h2><div class=card><div class=row><b>ojjyOS Web</b> &middot; part of ojjyOS 3, created by Jonas "
            "Lee. Its own HTML and CSS engine, network stack and TLS, written from scratch for the ojjyOS kernel.<br>"
            "<span class=muted>Your browsing data is stored in ~/Library/Web.</span></div></div></div></body></html>");
    return o.len;
}

size_t page_error(char *buf, size_t cap, const char *url, const char *reason, bool https)
{
    Out o = {buf, 0, cap};
    buf[0] = '\0';
    head(&o, "Can't reach this page");
    const char *code = "ERR_FAILED";
    if (strncmp(reason, "could not find", 14) == 0) code = "ERR_NAME_NOT_RESOLVED";
    else if (strstr_simple(reason, "refused")) code = "ERR_CONNECTION_REFUSED";
    else if (strstr_simple(reason, "timed out") || strstr_simple(reason, "stopped responding")) code = "ERR_TIMED_OUT";
    else if (strstr_simple(reason, "secure")) code = "ERR_SSL_PROTOCOL_ERROR";
    else if (strstr_simple(reason, "turned off") || strstr_simple(reason, "no network")) code = "ERR_INTERNET_DISCONNECTED";
    else if (strstr_simple(reason, "redirect")) code = "ERR_TOO_MANY_REDIRECTS";
    put(&o, "<div class=wrap style=\"padding-top:70px\"><pre style=\"color:var(--muted);font-size:13px;line-height:1.1\">"
            "               __\n"
            "              / _)\n"
            "     _.----._/ /\n"
            "    /         /\n"
            " __/ (  | (  |\n"
            "/__.-'|_|--|_|</pre><h1>This site can\xE2\x80\x99t be reached</h1><p>");
    put(&o, "<b>");
    put_esc(&o, url);
    put(&o, "</b></p><p>");
    put_esc(&o, reason);
    put(&o, "</p><p class=muted>");
    put(&o, code);
    put(&o, "</p><p>");
    if (https) {
        put(&o, "Some servers only speak older security protocols that ojjyOS does not support yet. ");
    }
    put(&o, "Try:</p><ul><li>Checking the address for typos</li><li>Checking the connection in Control Center</li>");
    if (https) {
        put(&o, "<li><a href=\"");
        put_esc(&o, "http://");
        const char *h = url;
        for (const char *c = url; *c; c++) if (c[0] == '/' && c[1] == '/') { h = c + 2; break; }
        put_esc(&o, h);
        put(&o, "\">Opening the plain http:// version</a></li>");
    }
    put(&o, "</ul><p><a class=btn href=\"");
    put_esc(&o, url);
    put(&o, "\">Reload</a><a class=btn href=\"ojjy:newtab\">New Tab</a></p></div></body></html>");
    return o.len;
}

size_t page_needs_js(char *buf, size_t cap, const char *url)
{
    Out o = {buf, 0, cap};
    buf[0] = '\0';
    head(&o, "JavaScript needed");
    put(&o, "<div class=wrap><h1>This site needs JavaScript</h1><p>");
    put_esc(&o, url);
    put(&o, " builds its page with JavaScript, which ojjyOS Web does not run yet.</p></div></body></html>");
    return o.len;
}

size_t page_image(char *buf, size_t cap, const char *url)
{
    Out o = {buf, 0, cap};
    buf[0] = '\0';
    put(&o, "<!DOCTYPE html><html><head><title>");
    put_esc(&o, url);
    put(&o, "</title><style>body{margin:0;background:#0e0e0e;text-align:center}img{max-width:100%;margin-top:24px}"
            "</style></head><body><img src=\"");
    put_esc(&o, url);
    put(&o, "\"></body></html>");
    return o.len;
}
