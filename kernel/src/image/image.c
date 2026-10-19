/*
 * ojjyOS v3 - Image decoding front end (see image.h)
 *
 * ojjyOS - Created by Jonas Lee
 */

#include "image/image.h"
#include "image/jpeg.h"
#include "string.h"

static uint8_t *scratch;
static size_t scratch_size;

void image_set_scratch(void *buf, size_t size)
{
    scratch = (uint8_t *)buf;
    scratch_size = size;
}

static bool is_png(const uint8_t *d, size_t len) { return len >= 8 && d[0] == 137 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G'; }
static bool is_gif(const uint8_t *d, size_t len) { return len >= 6 && memcmp(d, "GIF8", 4) == 0; }
static bool is_jpeg(const uint8_t *d, size_t len) { return len >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF; }

bool image_info(const uint8_t *data, size_t len, int *w, int *h)
{
    if (!data) return false;
    if (is_png(data, len)) return png_info(data, len, w, h);
    if (is_gif(data, len)) return gif_info(data, len, w, h);
    if (is_jpeg(data, len)) return jpeg_info(data, len, w, h);
    return false;
}

bool image_decode(const uint8_t *data, size_t len, uint32_t *argb, size_t pixels)
{
    if (!data || !argb) return false;
    if (is_png(data, len)) return scratch && png_decode(data, len, argb, pixels, scratch, scratch_size);
    if (is_gif(data, len)) return scratch && gif_decode(data, len, argb, pixels, scratch, scratch_size);
    if (is_jpeg(data, len)) {
        if (jpeg_decode(data, len, argb, pixels)) {
            for (size_t i = 0; i < pixels; i++) argb[i] |= 0xFF000000u;     /* JPEG is opaque */
            return true;
        }
        /* progressive / non-interleaved files */
        return scratch && jpeg_decode_full(data, len, argb, pixels, scratch, scratch_size);
    }
    return false;
}
