/*
 * ojjyOS v3 - URL parsing and resolution (RFC 3986 subset)
 *
 * Schemes: http, https, ojjy (built-in offline pages, e.g. "ojjy:home").
 * The fragment is kept separately; path always starts with '/' and
 * includes the query string.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_URL_H
#define _OJJY_URL_H

#include "types.h"

#define URL_HOST_MAX 128
#define URL_PATH_MAX 1024
#define URL_TEXT_MAX (URL_HOST_MAX + URL_PATH_MAX + 32)

typedef struct {
    char scheme[8];             /* "http", "https", "ojjy" */
    char host[URL_HOST_MAX];    /* lower-case; page id for ojjy: */
    uint16_t port;
    char path[URL_PATH_MAX];    /* "/a/b?q=1" */
    char fragment[64];
} Url;

/* Accepts full URLs and bare "example.com/x" (assumed http). */
bool url_parse(const char *text, Url *out);
/* Resolve an href against the page URL (absolute, //host, /path, rel, ?q, #f). */
bool url_resolve(const Url *base, const char *ref, Url *out);
/* Canonical text form, without the fragment unless with_fragment. */
void url_format(const Url *u, char *out, size_t size, bool with_fragment);
/* Percent-encode a form value (application/x-www-form-urlencoded). */
void url_encode_component(const char *in, char *out, size_t size);

#endif /* _OJJY_URL_H */
