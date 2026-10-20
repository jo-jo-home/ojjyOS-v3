/*
 * ojjyOS v3 - Image decoding front end (the interface agreed in
 * PARALLEL_PLAN.md): dispatches by file signature to PNG, GIF (first frame)
 * and JPEG. Output pixels are 0xAARRGGBB with straight alpha.
 *
 * Decoders need scratch memory (compressed + filtered PNG rows, GIF
 * index buffers); the caller provides it once with image_set_scratch().
 *
 * ojjyOS - Created by Jonas Lee
 */
#ifndef _OJJY_IMAGE_H
#define _OJJY_IMAGE_H

#include "types.h"

void image_set_scratch(void *buf, size_t size);
bool image_info(const uint8_t *data, size_t len, int *w, int *h);
bool image_decode(const uint8_t *data, size_t len, uint32_t *argb, size_t pixels);

bool png_info(const uint8_t *data, size_t len, int *w, int *h);
bool png_decode(const uint8_t *data, size_t len, uint32_t *argb, size_t pixels, uint8_t *scratch, size_t scratch_size);
bool gif_info(const uint8_t *data, size_t len, int *w, int *h);
bool gif_decode(const uint8_t *data, size_t len, uint32_t *argb, size_t pixels, uint8_t *scratch, size_t scratch_size);

bool webp_info(const uint8_t *data, size_t len, int *w, int *h);
bool webp_decode(const uint8_t *data, size_t len, uint32_t *argb, size_t pixels, uint8_t *scratch, size_t scratch_size);

#endif /* _OJJY_IMAGE_H */
