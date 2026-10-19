/*
 * ojjyOS v3 - DEFLATE decompression (RFC 1951) with zlib (RFC 1950) and
 * gzip (RFC 1952) wrappers. Used by PNG and HTTP Content-Encoding.
 * Input is untrusted: all reads and writes are bounds-checked; a corrupt
 * stream returns -1, output beyond `cap` returns -2.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_IMAGE_INFLATE_H
#define _OJJY_IMAGE_INFLATE_H

#include "types.h"

/* Raw DEFLATE. Returns bytes written to out, or -1 (corrupt) / -2 (too big). */
long inflate_raw(const uint8_t *in, size_t len, uint8_t *out, size_t cap, size_t *consumed);
long zlib_inflate(const uint8_t *in, size_t len, uint8_t *out, size_t cap);
long gzip_inflate(const uint8_t *in, size_t len, uint8_t *out, size_t cap);

#endif /* _OJJY_IMAGE_INFLATE_H */
