/* ojjyOS pointer image (see tools/mkcursor.py). Created by Jonas Lee. */
#ifndef _OJJY_CURSOR_IMAGE_H
#define _OJJY_CURSOR_IMAGE_H
#include "types.h"
#define CURSOR_W 22
#define CURSOR_H 30
#define CURSOR_HOT_X 2
#define CURSOR_HOT_Y 2
extern const uint8_t cursor_rgba[CURSOR_W * CURSOR_H * 4];   /* straight alpha */
#endif
