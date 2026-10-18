/* URL, HTTP parsing and HTML layout tests. Created by Jonas Lee. */
#include "web/url.h"
#include "web/http.h"
#include "web/html.h"
#include "random.h"
#include "test_support.h"

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

static bool resolves(const char *base, const char *ref, const char *expect)
{
    Url b, r;
    char out[URL_TEXT_MAX];
    if (!url_parse(base, &b) || !url_resolve(&b, ref, &r)) return false;
    url_format(&r, out, sizeof(out), false);
    return strcmp(out, expect) == 0;
}

static WebDoc doc;

static bool has_text(const char *needle, uint8_t style_mask)
{
    for (int i = 0; i < doc.run_count; i++) {
        const WebRun *r = &doc.runs[i];
        const char *t = web_text(&doc, r->text);
        size_t n = strlen(needle);
        for (size_t k = 0; k + n <= r->len; k++)
            if (strncmp(t + k, needle, n) == 0 && (!style_mask || (r->style & style_mask))) return true;
    }
    return false;
}

static bool within_width(void)
{
    for (int i = 0; i < doc.run_count; i++) {
        const WebRun *r = &doc.runs[i];
        if (r->x < 0 || r->x + r->len * WEB_CELL_W * r->scale > doc.width) return false;
    }
    return true;
}

static bool sorted_by_y(void)
{
    for (int i = 1; i < doc.run_count; i++) if (doc.runs[i].y < doc.runs[i - 1].y) return false;
    return true;
}

static void layout(const char *html, int width) { html_layout(&doc, html, strlen(html), width, false); }

int main(void)
{
    Url u;
    char text[URL_TEXT_MAX];
    CHECK(url_parse("http://Example.COM:8080/a/b?x=1#frag", &u) && strcmp(u.host, "example.com") == 0 &&
          u.port == 8080 && strcmp(u.path, "/a/b?x=1") == 0 && strcmp(u.fragment, "frag") == 0, "parse full URL");
    CHECK(url_parse("example.com", &u) && strcmp(u.scheme, "http") == 0 && strcmp(u.path, "/") == 0 && u.port == 80,
          "bare host defaults to http and /");
    CHECK(url_parse("https://a.b", &u) && u.port == 443, "https default port");
    CHECK(url_parse("ojjy:Home", &u) && strcmp(u.scheme, "ojjy") == 0 && strcmp(u.host, "home") == 0, "ojjy: pages");
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

    /* HTML layout */
    layout("<html><head><title> My  &amp; Page </title><style>p{color:red}</style>"
           "<script>document.write('<p>evil</p>')</script></head><body>"
           "<h1>Big Title</h1><p>Hello <b>bold</b> and <a href=\"/x\">a link</a>.</p>"
           "<ul><li>one</li><li>two</li></ul><ol start=3><li>three</li></ol>"
           "<p>caf\xc3\xa9 &mdash; &copy; &#65;&#x42; &lt;tag&gt; \xe2\x80\x9cq\xe2\x80\x9d</p>"
           "<pre>a  b\n  c</pre><noscript>No JS here</noscript>"
           "<img alt=\"logo\" src=x.png><hr><table><tr><td>A</td><td>B</td></tr></table>"
           "<p id=\"end\">The end</p></body></html>", 400);
    CHECK(strcmp(doc.title, "My & Page") == 0, "title captured, entities decoded, trimmed");
    CHECK(!has_text("evil", 0) && !has_text("color", 0), "script and style skipped");
    CHECK(has_text("Big Title", WS_HEADING), "heading styled");
    for (int i = 0; i < doc.run_count; i++)
        if (strncmp(web_text(&doc, doc.runs[i].text), "Big", 3) == 0) CHECK(doc.runs[i].scale == 2, "h1 drawn at 2x");
    CHECK(has_text("bold", WS_BOLD), "bold run");
    CHECK(has_text("a link", WS_LINK) && doc.link_count == 1 && strcmp(web_text(&doc, doc.links[0]), "/x") == 0,
          "link run and href");
    CHECK(has_text("3.", 0) && has_text("three", 0), "ordered list honours start=");
    CHECK(has_text("cafe -- (c) AB <tag> \"q\"", 0), "UTF-8 and entities transliterated");
    CHECK(has_text("a  b", WS_CODE) && has_text("  c", WS_CODE), "pre keeps spacing and lines");
    CHECK(has_text("No JS here", 0), "noscript content shown");
    CHECK(has_text("[logo]", WS_MUTED), "img alt placeholder");
    CHECK(has_text("A", 0) && has_text("B", 0), "table cells");
    CHECK(web_anchor_y(&doc, "end") > 0 && web_anchor_y(&doc, "nope") == -1, "anchors recorded");
    CHECK(within_width() && sorted_by_y(), "all runs inside width and sorted by y");
    int bullets = 0;
    for (int i = 0; i < doc.box_count; i++) bullets += doc.boxes[i].kind == BOX_BULLET;
    CHECK(bullets == 2, "unordered list bullets");

    /* hit testing */
    int link = -1;
    for (int i = 0; i < doc.run_count; i++)
        if (doc.runs[i].link == 0) link = web_link_at(&doc, doc.runs[i].x + 2, doc.runs[i].y + 4);
    CHECK(link == 0, "link hit test");
    CHECK(web_link_at(&doc, 399, 2) == -1, "no link in empty space");

    /* word wrap: narrow width, long words broken, nothing past the edge */
    layout("<p>The quick brown fox jumps over the lazy dog again and again "
           "Supercalifragilisticexpialidociousnesswordthatiswaytoolong end</p>", 120);
    CHECK(within_width() && sorted_by_y() && doc.height > 5 * WEB_CELL_H, "wrapping at 120px");

    /* forms */
    layout("<form action=\"/search\" method=get><input type=hidden name=src value=os>"
           "<input name=q value=\"hi there\"><select name=lang><option value=en>English"
           "<option value=fr selected>French</select><input type=checkbox name=c checked>"
           "<input type=submit name=go value=Search><button name=b>Other</button></form>", 600);
    CHECK(doc.form_count == 1 && strcmp(web_text(&doc, doc.forms[0].action), "/search") == 0 && !doc.forms[0].post,
          "form recorded");
    int submit = -1, select = -1;
    for (int i = 0; i < doc.field_count; i++) {
        if (doc.fields[i].kind == FIELD_SUBMIT && strcmp(doc.fields[i].name, "go") == 0) submit = i;
        if (strcmp(doc.fields[i].name, "lang") == 0) select = i;
    }
    CHECK(select >= 0 && strcmp(doc.fields[select].value, "fr") == 0 && strcmp(doc.fields[select].label, "French") == 0,
          "select uses selected option (</option> omitted)");
    char query[256];
    web_form_query(&doc, 0, submit, query, sizeof(query));
    CHECK(strcmp(query, "?src=os&q=hi+there&lang=fr&c=on&go=Search") == 0, "GET query built from fields");
    int box = doc.fields[submit].box;
    CHECK(box >= 0 && web_box_at(&doc, doc.boxes[box].x + 3, doc.boxes[box].y + 3) == box, "button box hit test");

    /* plain text */
    html_layout(&doc, "line one\nline two <b>not a tag</b>", 34, 400, true);
    CHECK(has_text("<b>not a tag</b>", 0) && doc.run_count == 2, "plain text rendered literally");

    /* Fuzz: random bytes and mutated markup never escape the tables */
    static char fuzz[8192];
    const char *seed = "<div><p class=a>x &amp; y<a href='/q'>l</a><ul><li>z</ul><table><tr><td>t</table>"
                       "<pre>\n p</pre><form><input name=a><select><option>o</select></form><!-- c --></div>";
    random_add_entropy("html", 4);
    bool ok = true;
    for (int i = 0; i < 3000; i++) {
        size_t len = random_u32() % sizeof(fuzz);
        if (i & 1) {
            random_bytes(fuzz, len);
        } else {
            size_t sl = strlen(seed);
            for (size_t k = 0; k < len; k++) fuzz[k] = seed[k % sl];
            for (int m = 0; m < 20 && len; m++) fuzz[random_u32() % len] = (char)random_u32();
        }
        html_layout(&doc, fuzz, len, 80 + (int)(random_u32() % 900), (i % 7) == 0);
        if (!within_width() || !sorted_by_y() || doc.pool_len > WEB_TEXT_POOL) ok = false;
    }
    CHECK(ok, "3000 fuzzed documents stay inside width and tables");
    return finish();
}
