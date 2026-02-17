/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_DRAW_H__
#define __CLAP_DRAW_H__

#include "render.h"
#include "util.h"

#define DRAW_AF_TOP    0x1
#define DRAW_AF_BOTTOM 0x2
#define DRAW_AF_LEFT   0x4
#define DRAW_AF_RIGHT  0x8
#define DRAW_AF_HCENTER (DRAW_AF_LEFT | DRAW_AF_RIGHT)
#define DRAW_AF_VCENTER (DRAW_AF_TOP | DRAW_AF_BOTTOM)
#define DRAW_AF_CENTER (DRAW_AF_VCENTER | DRAW_AF_HCENTER)

typedef void (*conv_write_fn)(void *dst, void *v);
typedef void (*conv_read_fn)(void *dst, void *v);

typedef struct canvas {
    conv_write_fn   conv_write;
    conv_read_fn    conv_read;
    unsigned int    width;
    unsigned int    height;
    texture_format  fmt;
    uchar           data[0];
} canvas;

cresp_ret(canvas);

DECLARE_CLEANUP(canvas);

cresp(canvas) canvas_new(texture_format fmt, unsigned int width, unsigned int height);
void canvas_free(canvas *c);
void canvas_write(canvas *c, unsigned int x, unsigned int y, vec4 color);
void canvas_read(canvas *c, unsigned int x, unsigned int y, vec4 color);
void canvas_blit(canvas *dst, canvas *src, unsigned int x, unsigned int y, float *color);

#endif /* __CLAP_DRAW_H__ */
