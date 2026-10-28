/* URL, HTTP, web engine (DOM, CSS, layout, forms), inflate, PNG and GIF
 * tests. Created by Jonas Lee. */
#include "web/url.h"
#include "web/http.h"
#include "web/engine.h"
#include "image/image.h"
#include "image/inflate.h"
#include "random.h"
#include "test_support.h"
#include "image_vectors.h"

/* http.c links against the TCP/DNS API; these tests only use its pure
 * parsing helpers, so the network calls are stubbed out. */
#include "net/tcp.h"
int tcp_connect(ip4_t ip, uint16_t port) { (void)ip; (void)port; return -1; }
int tcp_send(int h, const void *d, size_t l) { (void)h; (void)d; (void)l; return -1; }
int tcp_recv(int h, void *b, size_t l) { (void)h; (void)b; (void)l; return 0; }
bool tcp_peer_closed(int h) { (void)h; return true; }
TcpState tcp_state(int h) { (void)h; return TCP_S_FREE; }
TcpError tcp_error(int h) { (void)h; return TCP_ERR_NONE; }
void tcp_release(int h) { (void)h; }
const char *tcp_error_string(TcpError e) { (void)e; return ""; }
DnsStatus dns_resolve(const char *n, ip4_t *o) { (void)n; (void)o; return DNS_FAIL; }
uint64_t net_now(void) { return 0; }
static NetInfo ni;
const NetInfo *net_info(void) { return &ni; }
bool net_ready(void) { return false; }
#include "net/tls.h"
void tls_start(TlsConn *c, const char *h, TlsIo io) { (void)c; (void)h; (void)io; }
void tls_start_ex(TlsConn *c, const char *h, TlsIo io, unsigned f) { (void)c; (void)h; (void)io; (void)f; }
TlsState tls_poll(TlsConn *c) { (void)c; return TLS_FAILED; }
int tls_send(TlsConn *c, const void *d, size_t l) { (void)c; (void)d; (void)l; return -1; }
int tls_recv(TlsConn *c, void *b, size_t l) { (void)c; (void)b; (void)l; return 0; }
bool tls_eof(TlsConn *c) { (void)c; return true; }
void tls_close(TlsConn *c) { (void)c; }
const char *tls_suite_name(uint16_t s) { (void)s; return ""; }
void *pmm_alloc_bytes(size_t n) { (void)n; return 0; }

static bool resolves(const char *base, const char *ref, const char *expect)
{
    Url b, r;
    char out[URL_TEXT_MAX];
    if (!url_parse(base, &b) || !url_resolve(&b, ref, &r)) return false;
    url_format(&r, out, sizeof(out), false);
    return strcmp(out, expect) == 0;
}

static void url_http_tests(void)
{
    Url u;
    char text[URL_TEXT_MAX];
    CHECK(url_parse("http://Example.COM:8080/a/b?x=1#frag", &u) && strcmp(u.host, "example.com") == 0 &&
          u.port == 8080 && strcmp(u.path, "/a/b?x=1") == 0 && strcmp(u.fragment, "frag") == 0, "parse full URL");
    CHECK(url_parse("example.com", &u) && strcmp(u.scheme, "http") == 0 && strcmp(u.path, "/") == 0 && u.port == 80,
          "bare host defaults to http and /");
    CHECK(url_parse("https://a.b", &u) && u.port == 443, "https default port");
    CHECK(url_parse("ojjy:Home", &u) && strcmp(u.scheme, "ojjy") == 0 && strcmp(u.host, "home") == 0, "ojjy: pages");
    CHECK(url_parse("ojjy:settings?search=1", &u) && strcmp(u.host, "settings") == 0 && strcmp(u.path, "/?search=1") == 0,
          "ojjy: page keeps its query (Settings buttons, NET-02 Visit anyway)");
    CHECK(url_parse("ojjy:proceed?url=https://10.0.2.2:8443/a?b=1#frag", &u) && strcmp(u.host, "proceed") == 0 &&
          strcmp(u.path, "/?url=https://10.0.2.2:8443/a?b=1") == 0, "ojjy: query with a nested URL");
    {
        char text[URL_TEXT_MAX];
        url_format(&u, text, sizeof(text), false);
        CHECK(strcmp(text, "ojjy:proceed?url=https://10.0.2.2:8443/a?b=1") == 0, "ojjy: query round-trips");
        Url base, link;
        url_parse("ojjy:settings", &base);
        CHECK(url_resolve(&base, "ojjy:settings?cookies=clear", &link) && strcmp(link.path, "/?cookies=clear") == 0,
              "ojjy: link keeps its query");
        CHECK(url_parse("ojjy:newtab", &u) && strcmp(u.path, "/") == 0, "ojjy: page without a query");
    }
    CHECK(!url_parse("ftp://x/", &u) && !url_parse("http://bad host/", &u) && !url_parse("http://h:99999/", &u) &&
          !url_parse("", &u) && !url_parse("http://:80/", &u), "reject unsupported/bad URLs");
    CHECK(url_parse("http://h/a/./b/../c", &u) && strcmp(u.path, "/a/c") == 0, "dot segments removed");
    url_parse("http://h:81/p?q", &u);
    url_format(&u, text, sizeof(text), false);
    CHECK(strcmp(text, "http://h:81/p?q") == 0, "format keeps non-default port");

    /* RFC 3986 5.4.1 normal examples (base http://a/b/c/d;p?q) */
    const char *base = "http://a/b/c/d;p?q";
    CHECK(resolves(base, "g", "http://a/b/c/g"), "rfc g");
    CHECK(resolves(base, "./g", "http://a/b/c/g"), "rfc ./g");
    CHECK(resolves(base, "g/", "http://a/b/c/g/"), "rfc g/");
    CHECK(resolves(base, "/g", "http://a/g"), "rfc /g");
    CHECK(resolves(base, "//g", "http://g/"), "rfc //g");
    CHECK(resolves(base, "?y", "http://a/b/c/d;p?y"), "rfc ?y");
    CHECK(resolves(base, "g?y", "http://a/b/c/g?y"), "rfc g?y");
    CHECK(resolves(base, "#s", "http://a/b/c/d;p?q"), "rfc #s keeps document");
    CHECK(resolves(base, "..", "http://a/b/"), "rfc ..");
    CHECK(resolves(base, "../g", "http://a/b/g"), "rfc ../g");
    CHECK(resolves(base, "../..", "http://a/"), "rfc ../..");
    CHECK(resolves(base, "../../../g", "http://a/g"), "rfc above root");
    CHECK(resolves(base, "https://x.org/y", "https://x.org/y"), "absolute href");
    char enc[64];
    url_encode_component("a b&c=d/é", enc, sizeof(enc));
    CHECK(strcmp(enc, "a+b%26c%3Dd%2F%C3%A9") == 0, "form encoding");

    /* HTTP head + chunked */
    HttpResponseHead h;
    const char *resp = "HTTP/1.1 302 Found\r\nLocation: /next\r\ncontent-length: 5\r\nContent-Type: text/html\r\n\r\nhello";
    CHECK(http_parse_head((const uint8_t *)resp, strlen(resp), &h) == 1 && h.status == 302 &&
          strcmp(h.location, "/next") == 0 && h.content_length == 5 && strcmp(h.content_type, "text/html") == 0,
          "parse status and headers case-insensitively");
    CHECK(http_parse_head((const uint8_t *)resp, 30, &h) == 0, "incomplete head needs more data");
    CHECK(http_parse_head((const uint8_t *)"garbage\r\n\r\n", 11, &h) == -1, "malformed status rejected");
    const char *ch = "HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\nContent-Length: 99\r\n\r\n";
    CHECK(http_parse_head((const uint8_t *)ch, strlen(ch), &h) == 1 && h.chunked && h.content_length == -1,
          "chunked overrides content-length");
    char chunked[] = "4;ext=1\r\nWiki\r\n5\r\npedia\r\nE\r\n in\r\n\r\nchunks.\r\n0\r\n\r\n";
    bool complete;
    long n = http_dechunk((uint8_t *)chunked, strlen(chunked), &complete);
    CHECK(n == 23 && complete && memcmp(chunked, "Wikipedia in\r\n\r\nchunks.", 23) == 0, "dechunk RFC example");
    char partial[] = "5\r\nhel";
    n = http_dechunk((uint8_t *)partial, strlen(partial), &complete);
    CHECK(n == 3 && !complete, "partial chunk kept, not complete");
    char bad[] = "zz\r\nxx";
    CHECK(http_dechunk((uint8_t *)bad, strlen(bad), &complete) == -2, "malformed chunk size");

}

/* ---- Engine ---- */

/* Fake metrics: every byte is font/2 px wide; ascent 4/5, line 6/5 of the size. */
static uint8_t f_pick(int px, int w, bool it, bool mono) { (void)it; (void)mono; if (px > 120) px = 120; return (uint8_t)(px | (w >= 600 ? 128 : 0)); }
static int f_size(uint8_t f) { return f & 127; }
static int f_width(const char *s, int len, uint8_t f) { (void)s; return len * f_size(f) / 2; }
static int f_ascent(uint8_t f) { return f_size(f) * 4 / 5; }
static int f_height(uint8_t f) { return f_size(f) * 6 / 5; }
static bool f_glyph(uint32_t cp) { return cp == 0xE9; }
static const WebFontMetrics fm = {f_pick, f_width, f_ascent, f_height, f_glyph};

static uint8_t dom_mem[8 << 20], css_mem[8 << 20], lay_mem[4 << 20];
static WebArena dom_a, css_a, lay_a;
static WebItem items[40000];
static WebDoc doc;

static void page(const char *html, int width)
{
    web_doc_init(&doc, &dom_a, &css_a, &lay_a, items, 40000, &fm);
    web_doc_parse(&doc, html, strlen(html), false, false);
    web_doc_layout(&doc, width, 600, 100, false);
}

static bool strstr_local(const char *h, size_t hl, const char *n)
{
    size_t nl = strlen(n);
    for (size_t i = 0; i + nl <= hl; i++) if (memcmp(h + i, n, nl) == 0) return true;
    return false;
}

static const WebItem *text_item(const char *needle)
{
    for (int i = 0; i < doc.item_count; i++)
        if (doc.items[i].kind == WI_TEXT && strstr_local(doc.items[i].text, doc.items[i].len, needle)) return &doc.items[i];
    return NULL;
}

static int count_kind(int kind)
{
    int n = 0;
    for (int i = 0; i < doc.item_count; i++) n += doc.items[i].kind == kind;
    return n;
}

static bool all_inside(int width)
{
    for (int i = 0; i < doc.item_count; i++) {
        const WebItem *it = &doc.items[i];
        if (it->kind == WI_TEXT && (it->x < 0 || it->x + it->w > width + 1)) return false;
    }
    return true;
}

static void dom_tests(void)
{
    DomTree t;
    const char *html = "<!DOCTYPE html><title>T</title><p>one<p>two<ul><li>a<li>b</ul><div id=x class='a  b'>&amp;&lt;&#65;&#x42;</div>"
                       "<script>if (a < b) { x(); }</script><table><tr><td>c<td>d</table>";
    arena_reset(&dom_a);
    dom_parse(&t, &dom_a, html, strlen(html), false, false);
    CHECK(t.root && t.body && !t.quirks, "dom: html/body built, standards mode");
    int ps = 0, lis = 0, tds = 0;
    DomNode *x = NULL;
    for (DomNode *n = t.root; n; n = dom_next(n, t.root, false)) {
        if (n->type != DOM_ELEMENT) continue;
        ps += n->tag == TAG_P;
        lis += n->tag == TAG_LI;
        tds += n->tag == TAG_TD;
        if (n->id && strcmp(n->id, "x") == 0) x = n;
    }
    CHECK(ps == 2 && lis == 2 && tds == 2, "dom: implied end tags for p, li, td");
    CHECK(x && x->nclasses == 2 && dom_has_class(x, "b") && x->first && x->first->type == DOM_TEXT &&
          strcmp(x->first->text, "&<AB") == 0, "dom: classes, entities decoded");
    DomNode *p1 = NULL;
    for (DomNode *n = t.root; n; n = dom_next(n, t.root, false)) if (n->tag == TAG_P) { p1 = n; break; }
    CHECK(p1 && p1->next && p1->next->tag == TAG_P && p1->first && strcmp(p1->first->text, "one") == 0, "dom: second <p> is a sibling");
    dom_parse(&t, &dom_a, "<table><tr><td>x</table>", 24, false, false);
    CHECK(t.quirks, "dom: no doctype = quirks mode");
}

static void css_tests(void)
{
    page("<!DOCTYPE html><style>"
         ":root{--brand:#123456}"
         "p{color:red}.blue{color:blue}#special{color:green}"
         "p.imp{color:orange!important}#imp2{color:purple}"
         "div>span{font-weight:bold}div span.deep{font-style:italic}"
         "li:nth-child(2){color:#00ff00}li:not(.skip){text-decoration:underline}"
         ".v{color:var(--brand)} .fb{color:var(--nope, #abcdef)}"
         "@media (max-width: 500px){.m{color:#111111}} @media (min-width: 501px){.m{color:#222222}}"
         "h1{font-size:2em}.rem{font-size:1.5rem}"
         ".hid{display:none}"
         ".pre::before{content:'[' attr(data-x) ']'}"
         ".sep::after{content:\"x\\a0 \\b7 \\\"q\"}"
         "</style>"
         "<p>red</p><p class=blue>blue</p><p class=blue id=special>green</p><p class=imp id=imp2>orange</p>"
         "<div><span>bold<span class=deep>deep</span></span></div>"
         "<ul><li class=skip>one</li><li>two</li></ul>"
         "<p class=v>var</p><p class=fb>fallback</p><p class=m>media</p>"
         "<h1>big</h1><p class=rem>rem</p><p class=hid>hidden</p><p class=pre data-x=7>after</p><p class=sep>sep</p>", 800);
    const WebItem *it;
    CHECK((it = text_item("red")) && it->color == 0xFFFF0000u, "css: type selector");
    CHECK((it = text_item("blue")) && it->color == 0xFF0000FFu, "css: class beats type");
    CHECK((it = text_item("green")) && it->color == 0xFF008000u, "css: id beats class");
    CHECK((it = text_item("orange")) && it->color == 0xFFFFA500u, "css: !important beats id");
    CHECK((it = text_item("bold")) && (it->font & 128), "css: child combinator");
    CHECK((it = text_item("two")) && it->color == 0xFF00FF00u && (it->deco & DECO_UNDERLINE), "css: nth-child and :not");
    CHECK((it = text_item("one")) && !(it->deco & DECO_UNDERLINE), "css: :not excludes");
    CHECK((it = text_item("var")) && it->color == 0xFF123456u, "css: custom property via var()");
    CHECK((it = text_item("fallback")) && it->color == 0xFFABCDEFu, "css: var() fallback");
    CHECK((it = text_item("media")) && it->color == 0xFF222222u, "css: @media min-width at 800px");
    CHECK((it = text_item("big")) && (it->font & 127) == 32, "css: h1 2em = 32px");
    CHECK((it = text_item("rem")) && (it->font & 127) == 24, "css: 1.5rem = 24px");
    CHECK(!text_item("hidden"), "css: display:none removes content");
    CHECK(text_item("[7]"), "css: ::before content with attr()");
    CHECK(text_item("x\x01") && text_item("\"q"), "css: content escapes decode once (nbsp, escaped quote)");
    page("<!DOCTYPE html><style>"
         "@layer base, theme;"
         "@layer theme{.l1{color:#000002}}"
         "@layer base{#x.l1{color:#000001} .l2{color:#000001!important}}"
         ".l1{color:#000003}"
         "@layer theme{.l2{color:#000002!important}}"
         ".l2{color:#000003}"
         "@layer base{.l3{color:#000001}} @layer theme{.l3{color:#000002}}"
         "</style><p class=l1 id=x>layone</p><p class=l2>laytwo</p><p class=l3>laythree</p>", 800);
    CHECK((it = text_item("layone")) && it->color == 0xFF000003u, "css: unlayered beats layered despite specificity");
    CHECK((it = text_item("laytwo")) && it->color == 0xFF000001u, "css: !important in the earliest layer wins");
    CHECK((it = text_item("laythree")) && it->color == 0xFF000002u, "css: later layer beats earlier layer");
    uint32_t c;
    CHECK(css_parse_color("rgb(10 20 30 / 50%)", &c) && c == 0x800A141Eu, "css: rgb() space syntax with alpha");
    CHECK(css_parse_color("#abc", &c) && c == 0xFFAABBCCu && css_parse_color("hsl(120, 100%, 50%)", &c) && c == 0xFF00FF00u,
          "css: hex shorthand and hsl()");
}

static void layout_tests(void)
{
    /* wrapping within the width, block stacking */
    page("<!DOCTYPE html><body style='margin:0'><p style='margin:0'>aaaa bbbb cccc dddd eeee ffff gggg</p><p style='margin:10px 0'>next</p>", 200);
    const WebItem *a = text_item("aaaa"), *n = text_item("next");
    CHECK(a && n && all_inside(200) && n->y > a->y + 2 * 19, "layout: text wraps inside the width, blocks stack");
    /* flex row with space-between */
    page("<!DOCTYPE html><body style='margin:0'><div style='display:flex;justify-content:space-between;width:400px'>"
         "<span>left</span><span>right</span></div>", 800);
    const WebItem *l = text_item("left"), *r = text_item("right");
    CHECK(l && r && l->x == 0 && r->x + r->w == 400 && l->y == r->y, "layout: flex space-between");
    /* flex grow */
    page("<!DOCTYPE html><body style='margin:0'><div style='display:flex'><div style='width:100px'>a</div>"
         "<div style='flex:1;background:red'>b</div></div>", 500);
    int red = -1;
    for (int i = 0; i < doc.item_count; i++) if (doc.items[i].kind == WI_RECT && doc.items[i].color == 0xFFFF0000u) red = i;
    CHECK(red >= 0 && doc.items[red].x == 100 && doc.items[red].w == 400, "layout: flex-grow fills the rest");
    /* min-width:auto: a nowrap flex item never shrinks below its text */
    page("<!DOCTYPE html><body style='margin:0'><div style='display:flex;width:300px'>"
         "<div style='white-space:nowrap'>keepwhole keepwhole</div><div style='width:100%'>other</div></div>", 600);
    const WebItem *kw = text_item("keepwhole"), *ot = text_item("other");
    CHECK(kw && ot && ot->x >= kw->x + kw->w, "layout: flex items keep their min-content width");
    /* absolute box with only left set shrink-fits its text */
    page("<!DOCTYPE html><body style='margin:0'><div style='position:relative;width:300px;height:40px'>"
         "<span style='position:absolute;left:10px;top:0'>twowords here</span></div>", 600);
    const WebItem *t1 = text_item("twowords"), *t2 = text_item("here");
    CHECK(t1 && t2 && t1->y == t2->y && t1->x == 10, "layout: absolute box shrink-fits on one line");
    /* no insets: the static position */
    page("<!DOCTYPE html><body style='margin:0'><div style='position:relative'><div style='height:50px'></div>"
         "<div style='position:absolute'>statpos</div></div>", 600);
    CHECK(text_item("statpos") && text_item("statpos")->y >= 50 && text_item("statpos")->y < 70, "layout: absolute static position");
    /* fixed inside a positioned ancestor still uses the viewport */
    page("<!DOCTYPE html><body style='margin:0'><div style='position:relative;margin-top:100px'>"
         "<div style='position:fixed;top:5px;left:0'>fixd</div></div>", 600);
    CHECK(text_item("fixd") && text_item("fixd")->y < 30, "layout: nested fixed box placed against the viewport");
    /* the same stylesheet linked twice is fetched once */
    page("<!DOCTYPE html><link rel=stylesheet href=a.css><link rel=stylesheet href=b.css><link rel=stylesheet href=a.css>", 600);
    CHECK(doc.sheet_url_count == 2, "layout: duplicate stylesheet links requested once");
    /* centred block with auto margins */
    page("<!DOCTYPE html><body style='margin:0'><div style='width:200px;margin:0 auto;background:#00f'>c</div>", 600);
    int blue = -1;
    for (int i = 0; i < doc.item_count; i++) if (doc.items[i].kind == WI_RECT && doc.items[i].color == 0xFF0000FFu) blue = i;
    CHECK(blue >= 0 && doc.items[blue].x == 200 && doc.items[blue].w == 200, "layout: margin auto centres");
    /* float with text wrapping beside it */
    page("<!DOCTYPE html><body style='margin:0'><div style='float:right;width:100px;height:100px;background:#0f0'></div>"
         "<p style='margin:0'>word word word word word word word word word word</p>", 300);
    const WebItem *w = text_item("word");
    int green = -1;
    for (int i = 0; i < doc.item_count; i++) if (doc.items[i].kind == WI_RECT && doc.items[i].color == 0xFF00FF00u) green = i;
    CHECK(green >= 0 && doc.items[green].x == 200 && w && w->x + w->w <= 200, "layout: float right, text flows beside");
    /* table columns */
    page("<!DOCTYPE html><body style='margin:0'><table style='border-spacing:0'><tr><td>a</td><td>bbbbbbbb</td></tr>"
         "<tr><td>cccc</td><td>d</td></tr></table>", 600);
    const WebItem *ta = text_item("a"), *tb = text_item("bbbbbbbb"), *tc = text_item("cccc"), *td = text_item("d");
    CHECK(ta && tb && tc && td && tb->x == td->x && ta->x == tc->x && tb->x > tc->x && ta->y == tb->y && tc->y > ta->y,
          "layout: table columns line up");
    /* lists and markers */
    page("<!DOCTYPE html><ol start=3><li>x</li><li>y</li></ol><ul><li>z</li></ul>", 600);
    CHECK(text_item("3.") && text_item("4.") && count_kind(WI_MARKER) == 1, "layout: ordered numbers and bullets");
    /* links and anchors */
    page("<!DOCTYPE html><p><a href='/next'>go</a></p><h2 id=sec>Section</h2>", 600);
    const WebItem *g = text_item("go");
    CHECK(g && doc.link_count == 1 && strcmp(doc.links[0], "/next") == 0 && web_link_at(&doc, g->x + 2, g->y + 2) == 0,
          "layout: links registered and hit-tested");
    CHECK(web_anchor_y(&doc, "sec") > 0 && web_anchor_y(&doc, "nope") == -1, "layout: anchors recorded");
    /* forms */
    page("<!DOCTYPE html><form action='/s' method=post><input type=hidden name=src value=os>"
         "<input name=q value='hi there'><select name=lang><option value=en>English<option value=fr selected>French</select>"
         "<input type=checkbox name=c checked><input type=radio name=r value=1><input type=radio name=r value=2 checked>"
         "<input type=submit name=go value=Search><button type=button>No</button></form>", 600);
    CHECK(doc.form_count == 1 && doc.forms[0].post && strcmp(doc.forms[0].action, "/s") == 0, "forms: action and POST");
    int submit = -1;
    for (int i = 0; i < doc.field_count; i++) if (doc.fields[i].kind == FIELD_SUBMIT) submit = i;
    char q[256];
    web_form_encode(&doc, 0, submit, q, sizeof(q));
    CHECK(strcmp(q, "src=os&q=hi+there&lang=fr&c=on&r=2&go=Search") == 0, "forms: urlencoded body");
    CHECK(count_kind(WI_FIELD) >= 5, "forms: controls laid out");
    /* plain text */
    web_doc_init(&doc, &dom_a, &css_a, &lay_a, items, 40000, &fm);
    web_doc_parse(&doc, "line one\nline <b>two</b>", 24, true, false);
    web_doc_layout(&doc, 600, 600, 100, false);
    CHECK(text_item("<b>two</b>") && text_item("line one"), "layout: plain text kept literally");
    /* zoom doubles sizes */
    page("<!DOCTYPE html><p style='font-size:10px'>z</p>", 600);
    int small = text_item("z") ? text_item("z")->font & 127 : 0;
    web_doc_layout(&doc, 600, 600, 200, false);
    CHECK(small == 10 && text_item("z") && (text_item("z")->font & 127) == 20, "layout: zoom scales fonts");
}

static void image_tests(void)
{
    static uint8_t out[16384];
    long n = zlib_inflate(zlib_text, zlib_text_len, out, sizeof(out));
    CHECK(n == (long)plain_text_len && memcmp(out, plain_text, plain_text_len) == 0, "inflate: zlib (dynamic Huffman)");
    n = zlib_inflate(zlib_fixed, zlib_fixed_len, out, sizeof(out));
    CHECK(n == 23 && memcmp(out, "hello hello hello hello", 23) == 0, "inflate: zlib (fixed Huffman)");
    n = gzip_inflate(gzip_text, gzip_text_len, out, sizeof(out));
    CHECK(n == (long)plain_text_len && memcmp(out, plain_text, plain_text_len) == 0, "inflate: gzip wrapper");
    CHECK(zlib_inflate(zlib_text, zlib_text_len, out, 100) == -2, "inflate: output limit reported");

    /* HTTP Content-Encoding decoding in place */
    static uint8_t body[16384], zscratch[16384];
    HttpResponseHead hh;
    bool trunc = false;
    memset(&hh, 0, sizeof(hh));
    hh.gzip = true;
    memcpy(body, gzip_text, gzip_text_len);
    n = http_decode_body(&hh, body, gzip_text_len, sizeof(body), zscratch, sizeof(zscratch), &trunc);
    CHECK(n == (long)plain_text_len && !trunc && memcmp(body, plain_text, plain_text_len) == 0, "http: gzip body decoded in place");
    memset(&hh, 0, sizeof(hh));
    hh.deflate = true;
    memcpy(body, zlib_text, zlib_text_len);
    n = http_decode_body(&hh, body, zlib_text_len, sizeof(body), zscratch, sizeof(zscratch), &trunc);
    memcpy(body + 4096, raw_deflate_text, raw_deflate_text_len);
    long n2 = http_decode_body(&hh, body + 4096, raw_deflate_text_len, sizeof(body) - 4096, zscratch, sizeof(zscratch), &trunc);
    CHECK(n == (long)plain_text_len && n2 == (long)plain_text_len && !trunc && memcmp(body + 4096, plain_text, plain_text_len) == 0,
          "http: deflate as zlib and as raw DEFLATE");
    memset(&hh, 0, sizeof(hh));
    hh.gzip = true;
    memcpy(body, gzip_text, gzip_text_len);
    n = http_decode_body(&hh, body, gzip_text_len / 2, sizeof(body), zscratch, sizeof(zscratch), &trunc);
    CHECK(n > 0 && trunc && memcmp(body, plain_text, (size_t)n) == 0, "http: cut-off gzip keeps the decoded prefix");
    trunc = false;
    memcpy(body, gzip_text, gzip_text_len);
    n = http_decode_body(&hh, body, gzip_text_len, 100, zscratch, sizeof(zscratch), &trunc);
    CHECK(n > 0 && n <= 100 && trunc && memcmp(body, plain_text, (size_t)n) == 0, "http: decoded body limited to room");
    trunc = false;
    memset(body, 0x5A, 64);
    CHECK(http_decode_body(&hh, body, 64, sizeof(body), zscratch, sizeof(zscratch), &trunc) == -1, "http: garbage gzip rejected");
    memset(&hh, 0, sizeof(hh));
    CHECK(http_decode_body(&hh, body, 64, sizeof(body), zscratch, sizeof(zscratch), &trunc) == 64, "http: identity body untouched");
    static uint8_t scratch[1 << 20];
    image_set_scratch(scratch, sizeof(scratch));
    static uint32_t px[256];
    int w, h;
    CHECK(image_info(png_rgba, png_rgba_len, &w, &h) && w == 7 && h == 5 && image_decode(png_rgba, png_rgba_len, px, 256) &&
          memcmp(px, png_rgba_px, sizeof(png_rgba_px)) == 0, "png: RGBA with alpha matches Pillow");
    CHECK(image_info(png_interlaced, png_interlaced_len, &w, &h) && w == 9 && h == 11 &&
          image_decode(png_interlaced, png_interlaced_len, px, 256) && memcmp(px, png_interlaced_px, sizeof(png_interlaced_px)) == 0,
          "png: Adam7 interlaced RGB");
    CHECK(image_decode(png_palette, png_palette_len, px, 256) && memcmp(px, png_palette_px, sizeof(png_palette_px)) == 0,
          "png: palette + tRNS");
    CHECK(image_decode(png_grey1, png_grey1_len, px, 256) && memcmp(px, png_grey1_px, sizeof(png_grey1_px)) == 0,
          "png: 1-bit greyscale");
    CHECK(image_info(gif_small, gif_small_len, &w, &h) && w == 8 && h == 6 && image_decode(gif_small, gif_small_len, px, 256) &&
          memcmp(px, gif_small_px, sizeof(gif_small_px)) == 0, "gif: first frame with transparency");
    /* progressive JPEG == baseline JPEG of the same picture */
    static uint32_t a[45 * 29], b[45 * 29];
    struct { const unsigned char *base; unsigned blen; const unsigned char *prog; unsigned plen; const char *what; } jp[] = {
        {jpg_base, jpg_base_len, jpg_prog, jpg_prog_len, "4:2:0"},
        {jpg_base444, jpg_base444_len, jpg_prog444, jpg_prog444_len, "4:4:4"},
        {jpg_base_grey, jpg_base_grey_len, jpg_prog_grey, jpg_prog_grey_len, "grey"},
    };
    bool jok = true;
    for (unsigned i = 0; i < 3; i++) {
        int jw = 0, jh = 0;
        memset(a, 0, sizeof(a)); memset(b, 0, sizeof(b));
        if (!image_info(jp[i].prog, jp[i].plen, &jw, &jh) || jw != 45 || jh != 29) jok = false;
        if (!image_decode(jp[i].base, jp[i].blen, a, 45 * 29) || !image_decode(jp[i].prog, jp[i].plen, b, 45 * 29)) jok = false;
        if (memcmp(a, b, sizeof(a)) != 0) jok = false;
    }
    CHECK(jok, "jpeg: progressive decodes exactly like baseline (4:2:0, 4:4:4, grey)");
    /* WebP against libwebp (Pillow): lossless exact, lossy within rounding */
    static uint32_t wp[64 * 48];
    struct { const unsigned char *d; unsigned len; const unsigned *px; int w, h, tol; const char *what; } wv[] = {
        {webp_lossless, webp_lossless_len, webp_lossless_px, 37, 23, 0, "lossless"},
        {webp_lossy, webp_lossy_len, webp_lossy_px, 37, 23, 3, "lossy"},
        {webp_alpha, webp_alpha_len, webp_alpha_px, 37, 23, 3, "alpha"},
        {webp_mb, webp_mb_len, webp_mb_px, 64, 48, 3, "multi-MB"},
    };
    for (unsigned i = 0; i < 4; i++) {
        int ww = 0, wh = 0;
        memset(wp, 0, sizeof(wp));
        bool ok = image_info(wv[i].d, wv[i].len, &ww, &wh) && ww == wv[i].w && wh == wv[i].h &&
                  image_decode(wv[i].d, wv[i].len, wp, 64 * 48);
        int worst = 0;
        for (int k = 0; ok && k < ww * wh; k++)
            for (int sh = 0; sh < 32; sh += 8) {
                int dlt = (int)((wp[k] >> sh) & 255) - (int)((wv[i].px[k] >> sh) & 255);
                if (dlt < 0) dlt = -dlt;
                if (dlt > worst) worst = dlt;
            }
        if (!ok || worst > wv[i].tol) {
            char m[96] = "FAIL detail: webp ";
            strcpy(m + strlen(m), wv[i].what);
            strcpy(m + strlen(m), ok ? " max diff " : " decode failed");
            if (ok) utoa((uint64_t)worst, m + strlen(m), 10);
            emit(m);
        }
        CHECK(ok && worst <= wv[i].tol, "webp: decodes like libwebp");
    }
    /* fuzz the decoders */
    static uint8_t buf[8192];
    random_add_entropy("img", 3);
    bool ok = true;
    for (int i = 0; i < 4000; i++) {
        const uint8_t *srcs[6] = {png_rgba, png_interlaced, gif_small, jpg_prog, webp_lossy, webp_lossless};
        const unsigned lens[6] = {png_rgba_len, png_interlaced_len, gif_small_len, jpg_prog_len, webp_lossy_len, webp_lossless_len};
        const uint8_t *src = srcs[i % 6];
        unsigned len = lens[i % 6];
        size_t take = random_u32() % (len + 1);
        memcpy(buf, src, take);
        for (int m = 0; m < 6 && take; m++) buf[random_u32() % take] = (uint8_t)random_u32();
        if (image_info(buf, take, &w, &h) && (size_t)w * (size_t)h <= 256) image_decode(buf, take, px, 256);
        n = zlib_inflate(buf, take, out, sizeof(out));
        if (n > (long)sizeof(out)) ok = false;
    }
    CHECK(ok, "fuzz: 4000 corrupted images and streams handled");
}

static int alpha_sum(const uint32_t *px, int n)
{
    int t = 0;
    for (int i = 0; i < n; i++) t += (int)(px[i] >> 24);
    return t;
}

static void svg_tests(void)
{
    static uint32_t px[64 * 64];
    int w, h;
    const char *sq = "<svg viewBox='0 0 10 10' width='10' height='10'><rect x='2' y='2' width='6' height='6' fill='#f00'/></svg>";
    CHECK(image_info((const uint8_t *)sq, strlen(sq), &w, &h) && w == 10 && h == 10 && image_is_svg((const uint8_t *)sq, strlen(sq)),
          "svg: detected, size from attributes");
    CHECK(image_render_svg((const uint8_t *)sq, strlen(sq), px, 10, 10, 0xFF000000u) && px[5 * 10 + 5] == 0xFFFF0000u &&
          px[2 * 10 + 2] == 0xFFFF0000u && px[1 * 10 + 1] == 0 && px[8 * 10 + 8] == 0, "svg: rect covers exactly its pixels");
    const char *vb = "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 48 24'></svg>";
    CHECK(image_info((const uint8_t *)vb, strlen(vb), &w, &h) && w == 48 && h == 24, "svg: size from the viewBox");
    /* circle r=8 at 2x scale: area pi*r^2 */
    const char *ci = "<svg viewBox='0 0 32 32'><circle cx='16' cy='16' r='8' fill='currentColor'/></svg>";
    image_render_svg((const uint8_t *)ci, strlen(ci), px, 32, 32, 0xFF0000FFu);
    int area = alpha_sum(px, 32 * 32) / 255;
    CHECK(area >= 195 && area <= 207 && px[16 * 32 + 16] == 0xFF0000FFu, "svg: circle area and currentColor");
    /* even-odd hole, near-closed arcs */
    const char *eo = "<svg viewBox='0 0 32 32'><path fill-rule='evenodd' d='M16 2 A14 14 0 1 1 15.9 2 Z M16 10 a6 6 0 1 0 .1 0 z'/></svg>";
    image_render_svg((const uint8_t *)eo, strlen(eo), px, 32, 32, 0xFF000000u);
    CHECK(px[16 * 32 + 16] == 0 && (px[16 * 32 + 5] >> 24) == 255 && (px[1 * 32 + 16] >> 24) < 40, "svg: arcs and even-odd hole");
    /* stroke, transform, use + symbol, class rule */
    const char *mix = "<svg viewBox='0 0 40 40'><style>.k{fill:#00ff00}</style>"
                      "<defs><symbol id='s' viewBox='0 0 2 2'><rect width='2' height='2'/></symbol></defs>"
                      "<line x1='0' y1='5' x2='40' y2='5' stroke='#0000ff' stroke-width='2'/>"
                      "<g transform='translate(20,20)'><rect class='k' width='4' height='4'/></g>"
                      "<use href='#s' x='30' y='30' width='6' height='6' fill='#ff0000'/></svg>";
    image_render_svg((const uint8_t *)mix, strlen(mix), px, 40, 40, 0xFF000000u);
    CHECK(px[5 * 40 + 20] == 0xFF0000FFu && px[4 * 40 + 20] == 0xFF0000FFu && px[8 * 40 + 20] == 0, "svg: stroke width");
    CHECK(px[22 * 40 + 22] == 0xFF00FF00u && px[18 * 40 + 18] == 0, "svg: transform and <style> class rule");
    CHECK(px[33 * 40 + 33] == 0xFFFF0000u && px[29 * 40 + 29] == 0, "svg: use of a symbol with its viewBox");
    /* inline svg in a page: registered, serialized with CSS fill, sprite pulled in */
    page("<!DOCTYPE html><style>.ic{fill:#00ff00}</style><body style='margin:0;color:#123456'>"
         "<svg width='16' height='16' viewBox='0 0 16 16' fill='currentColor'><path d='M0 0h16v16H0z'></path></svg>"
         "<svg class='ic' width='8' height='8'><use href='#spr'></use></svg>"
         "<svg style='display:none'><symbol id='spr' viewBox='0 0 8 8'><circle cx='4' cy='4' r='4'></circle></symbol></svg>", 600);
    int svgs = 0, item16 = 0;
    for (int i = 0; i < doc.image_count; i++) if (doc.images[i].svg) svgs++;
    for (int i = 0; i < doc.item_count; i++) if (doc.items[i].kind == WI_IMAGE && doc.items[i].w == 16 && doc.items[i].h == 16) item16++;
    CHECK(svgs == 2 && item16 == 1 && doc.images[0].color == 0xFF123456u &&
          strstr_local(doc.images[0].svg, doc.images[0].svg_len, "<path d=\"M0 0h16v16H0z\">"), "svg: inline svg laid out as an image");
    CHECK(strstr_local(doc.images[1].svg, doc.images[1].svg_len, "fill=\"#00ff00\"") &&
          strstr_local(doc.images[1].svg, doc.images[1].svg_len, "<defs><symbol id=\"spr\""), "svg: CSS fill and page sprite carried over");
    image_render_svg((const uint8_t *)doc.images[1].svg, doc.images[1].svg_len, px, 8, 8, 0xFF000000u);
    CHECK(px[4 * 8 + 4] == 0xFF00FF00u, "svg: sprite rendered with the CSS fill");
    /* fuzz */
    static uint8_t fz[2048];
    random_add_entropy("svg", 3);
    for (int i = 0; i < 1500; i++) {
        const char *src = i % 3 == 0 ? mix : i % 3 == 1 ? eo : ci;
        size_t len = strlen(src), take = random_u32() % (len + 1);
        memcpy(fz, src, take);
        for (int m = 0; m < 8 && take; m++) fz[random_u32() % take] = (uint8_t)(i & 1 ? random_u32() : "0123456789.-,MLCAZz <>/\"'"[random_u32() % 25]);
        int fw = 1 + (int)(random_u32() % 64), fh = 1 + (int)(random_u32() % 64);
        image_render_svg(fz, take, px, fw, fh, 0xFF000000u);
    }
    CHECK(true, "fuzz: 1500 mutated SVG documents rasterized safely");
}

static void fuzz_engine(void)
{
    static char fuzz[8192];
    const char *seed = "<!DOCTYPE html><style>.a{color:red;display:flex}p>b{margin:1em auto}td:nth-child(2n+1){float:left}"
                       "@media (min-width:9px){div{width:calc(100% - 20px)}}</style><div class=a><p class=a>x &amp; y"
                       "<a href='/q'>l</a><ul><li>z</ul><table><tr><td>t<td>u</table><pre>\n p</pre><form><input name=a>"
                       "<select><option>o</select></form><!-- c --><img src=x width=10 height=10></div>";
    random_add_entropy("html", 4);
    bool ok = true;
    for (int i = 0; i < 1500; i++) {
        size_t len = random_u32() % sizeof(fuzz);
        if (i & 1) {
            random_bytes(fuzz, len);
        } else {
            size_t sl = strlen(seed);
            for (size_t k = 0; k < len; k++) fuzz[k] = seed[k % sl];
            for (int m = 0; m < 20 && len; m++) fuzz[random_u32() % len] = (char)random_u32();
        }
        web_doc_init(&doc, &dom_a, &css_a, &lay_a, items, 40000, &fm);
        web_doc_parse(&doc, fuzz, len, (i % 7) == 0, (i % 5) == 0);
        web_doc_layout(&doc, 80 + (int)(random_u32() % 900), 600, 50 + (int)(random_u32() % 200), false);
        if (doc.item_count > 40000 || doc.link_count > WEB_MAX_LINKS) ok = false;
    }
    CHECK(ok, "fuzz: 1500 mutated documents parse, style and lay out safely");
}

int main(void)
{
    arena_init(&dom_a, dom_mem, sizeof(dom_mem));
    arena_init(&css_a, css_mem, sizeof(css_mem));
    arena_init(&lay_a, lay_mem, sizeof(lay_mem));
    url_http_tests();
    dom_tests();
    css_tests();
    layout_tests();
    image_tests();
    svg_tests();
    fuzz_engine();
    return finish();
}
