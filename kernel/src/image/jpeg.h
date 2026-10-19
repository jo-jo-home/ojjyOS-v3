/*
 * ojjyOS v3 - Baseline JPEG decoder (wallpapers, later web images)
 *
 * Sequential DCT (SOF0/SOF1, 8-bit), Huffman coding, 1 or 3 components,
 * sampling factors up to 2x2 (4:4:4, 4:2:2, 4:2:0), restart intervals.
 * jpeg_decode_full() adds progressive (SOF2) and non-interleaved scans.
 * Arithmetic-coded and lossless files are rejected. Input is untrusted:
 * every read is bounds-checked and a bad file just returns false.
 * Output pixels are 0x00RRGGBB, row-major, width*height.
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_IMAGE_JPEG_H
#define _OJJY_IMAGE_JPEG_H

#include "types.h"

bool jpeg_info(const uint8_t *data, size_t len, int *width, int *height);
bool jpeg_decode(const uint8_t *data, size_t len, uint32_t *out, size_t out_pixels);
/* Any sequential or progressive Huffman JPEG, including non-interleaved
 * scans. Needs scratch for the whole image's coefficients (~2.5 bytes per
 * pixel plus component planes). Baseline files decode identically with
 * jpeg_decode(), which the wallpapers use. */
bool jpeg_decode_full(const uint8_t *data, size_t len, uint32_t *out, size_t out_pixels, uint8_t *scratch, size_t scratch_size);

#endif /* _OJJY_IMAGE_JPEG_H */
