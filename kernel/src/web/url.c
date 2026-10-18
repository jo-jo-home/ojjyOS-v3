/*
 * ojjyOS v3 - URL parsing and resolution (see url.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "web/url.h"
#include "string.h"

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool copy_n(char *dst, size_t size, const char *src, size_t n)
{
    if (n >= size) return false;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return true;
}

static bool append(char *dst, size_t size, const char *src)
{
    size_t a = strlen(dst), b = strlen(src);
    if (a + b >= size) return false;
    memcpy(dst + a, src, b + 1);
    return true;
}

/* RFC 3986 5.2.4 remove_dot_segments, in place. Query is preserved. */
static void normalize_path(char *path)
{
    char query[URL_PATH_MAX];
    query[0] = '\0';
    char *q = path;
    while (*q && *q != '?') q++;
    if (*q == '?') { copy_n(query, sizeof(query), q, strlen(q)); *q = '\0'; }

    char out[URL_PATH_MAX];
    size_t o = 0;
    const char *in = path;
    while (*in) {
        if (in[0] == '/' && in[1] == '.' && (in[2] == '/' || in[2] == '\0')) {
            in += 2;                                    /* "/." */
            if (*in == '\0') { out[o++] = '/'; break; }
        } else if (in[0] == '/' && in[1] == '.' && in[2] == '.' && (in[3] == '/' || in[3] == '\0')) {
            in += 3;                                    /* "/.." pops a segment */
            while (o > 0 && out[o - 1] != '/') o--;
            if (o > 0) o--;
            if (*in == '\0') { out[o++] = '/'; break; }
        } else {
            do { if (o + 1 < sizeof(out)) out[o++] = *in; in++; } while (*in && *in != '/');
        }
    }
    if (o == 0) out[o++] = '/';
    out[o] = '\0';
    copy_n(path, URL_PATH_MAX, out, o);
    append(path, URL_PATH_MAX, query);
}

static bool valid_url_char(char c) { return (unsigned char)c > 32 && (unsigned char)c < 127; }

bool url_parse(const char *text, Url *out)
{
    memset(out, 0, sizeof(*out));
    while (*text == ' ') text++;
    size_t len = strlen(text);
    while (len && text[len - 1] == ' ') len--;
    if (!len) return false;

    /* Scheme */
    const char *p = text;
    size_t s = 0;
    while (s < len && ((p[s] >= 'a' && p[s] <= 'z') || (p[s] >= 'A' && p[s] <= 'Z'))) s++;
    if (s < len && p[s] == ':' && s > 0 && s < sizeof(out->scheme)) {
        for (size_t i = 0; i < s; i++) out->scheme[i] = lower(p[i]);
        p += s + 1; len -= s + 1;
        if (len >= 2 && p[0] == '/' && p[1] == '/') { p += 2; len -= 2; }
    } else {
        strcpy(out->scheme, "http");
    }

    if (strcmp(out->scheme, "ojjy") == 0) {
        size_t n = 0;
        while (n < len && p[n] != '/' && p[n] != '#' && p[n] != '?') n++;
        if (!n) { strcpy(out->host, "home"); }
        else if (!copy_n(out->host, sizeof(out->host), p, n)) return false;
        for (char *c = out->host; *c; c++) *c = lower(*c);
        strcpy(out->path, "/");
        return true;
    }
    if (strcmp(out->scheme, "http") != 0 && strcmp(out->scheme, "https") != 0) return false;
    out->port = strcmp(out->scheme, "https") == 0 ? 443 : 80;

    /* Authority */
    size_t a = 0;
    while (a < len && p[a] != '/' && p[a] != '?' && p[a] != '#') a++;
    const char *auth = p;
    size_t alen = a;
    for (size_t i = 0; i < alen; i++) if (auth[i] == '@') { auth += i + 1; alen -= i + 1; i = 0; }   /* drop userinfo */
    size_t hlen = alen;
    for (size_t i = 0; i < alen; i++) {
        if (auth[i] == ':') {
            hlen = i;
            uint32_t port = 0;
            if (i + 1 == alen) break;
            for (size_t k = i + 1; k < alen; k++) {
                if (auth[k] < '0' || auth[k] > '9') return false;
                port = port * 10 + (uint32_t)(auth[k] - '0');
                if (port > 65535) return false;
            }
            if (!port) return false;
            out->port = (uint16_t)port;
            break;
        }
    }
    if (!hlen || !copy_n(out->host, sizeof(out->host), auth, hlen)) return false;
    for (char *c = out->host; *c; c++) {
        *c = lower(*c);
        if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '.')) return false;
    }
    p += a; len -= a;

    /* Path + query, then fragment */
    size_t f = 0;
    while (f < len && p[f] != '#') f++;
    if (f < len) copy_n(out->fragment, sizeof(out->fragment), p + f + 1, MIN(len - f - 1, sizeof(out->fragment) - 1));
    if (f == 0 || p[0] == '?') {
        strcpy(out->path, "/");
        if (f && !copy_n(out->path + 1, sizeof(out->path) - 1, p, f)) return false;
    } else if (!copy_n(out->path, sizeof(out->path), p, f)) {
        return false;
    }
    for (char *c = out->path; *c; c++) if (!valid_url_char(*c)) return false;
    normalize_path(out->path);
    return true;
}

bool url_resolve(const Url *base, const char *ref, Url *out)
{
    while (*ref == ' ' || *ref == '\n' || *ref == '\t' || *ref == '\r') ref++;
    if (!*ref) { *out = *base; out->fragment[0] = '\0'; return true; }

    /* Absolute reference: has a scheme */
    const char *c = ref;
    while ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '+' || *c == '.' || *c == '-') c++;
    if (*c == ':' && c != ref) return url_parse(ref, out);

    char text[URL_TEXT_MAX + URL_PATH_MAX];
    if (ref[0] == '/' && ref[1] == '/') {
        if (strlen(base->scheme) + strlen(ref) + 2 >= sizeof(text)) return false;
        strcpy(text, base->scheme); append(text, sizeof(text), ":"); append(text, sizeof(text), ref);
        return url_parse(text, out);
    }
    *out = *base;
    out->fragment[0] = '\0';
    if (strcmp(base->scheme, "ojjy") == 0) {        /* offline pages: ref is a page id */
        if (ref[0] == '#') return true;
        Url tmp;
        strcpy(text, "ojjy:");
        if (!append(text, sizeof(text), ref[0] == '/' ? ref + 1 : ref)) return false;
        if (!url_parse(text, &tmp)) return false;
        *out = tmp;
        return true;
    }
    if (ref[0] == '#') {
        copy_n(out->fragment, sizeof(out->fragment), ref + 1, MIN(strlen(ref + 1), sizeof(out->fragment) - 1));
        return true;
    }

    char path[URL_PATH_MAX];
    const char *frag = ref;
    while (*frag && *frag != '#') frag++;
    size_t rlen = (size_t)(frag - ref);
    if (ref[0] == '?') {                            /* same path, new query */
        size_t keep = 0;
        while (base->path[keep] && base->path[keep] != '?') keep++;
        if (!copy_n(path, sizeof(path), base->path, keep)) return false;
        char q[URL_PATH_MAX];
        if (!copy_n(q, sizeof(q), ref, rlen) || !append(path, sizeof(path), q)) return false;
    } else if (ref[0] == '/') {
        if (!copy_n(path, sizeof(path), ref, rlen)) return false;
    } else {                                        /* merge with base directory */
        size_t dir = 0;
        for (size_t i = 0; base->path[i] && base->path[i] != '?'; i++) if (base->path[i] == '/') dir = i + 1;
        if (!copy_n(path, sizeof(path), base->path, dir)) return false;
        char r[URL_PATH_MAX];
        if (!copy_n(r, sizeof(r), ref, rlen) || !append(path, sizeof(path), r)) return false;
    }
    for (char *p = path; *p; p++) {
        if (*p == ' ') *p = '+';                    /* tolerate sloppy hrefs */
        if (!valid_url_char(*p)) return false;
    }
    normalize_path(path);
    strcpy(out->path, path);
    if (*frag == '#') copy_n(out->fragment, sizeof(out->fragment), frag + 1, MIN(strlen(frag + 1), sizeof(out->fragment) - 1));
    return true;
}

void url_format(const Url *u, char *out, size_t size, bool with_fragment)
{
    if (!size) return;
    out[0] = '\0';
    append(out, size, u->scheme);
    if (strcmp(u->scheme, "ojjy") == 0) {
        append(out, size, ":");
        append(out, size, u->host);
        return;
    }
    append(out, size, "://");
    append(out, size, u->host);
    bool default_port = (strcmp(u->scheme, "http") == 0 && u->port == 80) ||
                        (strcmp(u->scheme, "https") == 0 && u->port == 443);
    if (!default_port) {
        char num[8];
        utoa(u->port, num, 10);
        append(out, size, ":");
        append(out, size, num);
    }
    append(out, size, u->path);
    if (with_fragment && u->fragment[0]) { append(out, size, "#"); append(out, size, u->fragment); }
}

void url_encode_component(const char *in, char *out, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o + 4 < size; in++) {
        unsigned char c = (unsigned char)*in;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') out[o++] = (char)c;
        else if (c == ' ') out[o++] = '+';
        else { out[o++] = '%'; out[o++] = hex[c >> 4]; out[o++] = hex[c & 15]; }
    }
    if (size) out[o] = '\0';
}
