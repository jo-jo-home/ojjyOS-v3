/*
 * ojjyOS v3 - Baseline JPEG decoder (wallpapers, later web images)
 *
 * Sequential DCT (SOF0/SOF1, 8-bit), Huffman coding, 1 or 3 components,
 * sampling factors up to 2x2 (4:4:4, 4:2:2, 4:2:0), restart intervals.
 * Progressive and arithmetic-coded files are rejected. Input is untrusted:
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

#endif /* _OJJY_IMAGE_JPEG_H */
