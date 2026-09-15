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
size_t canvas_size(canvas *c);
void *canvas_data(canvas *c);

typedef struct canvas_address {
    unsigned int    x, y, w, h;
    float           *color;
    blend           src_blend;
    blend           dst_blend;
} canvas_address;

void _canvas_write(canvas *c, const canvas_address *addr);
#define canvas_write(_c, args...) _canvas_write((_c), &(canvas_address){ args })
void canvas_read(canvas *c, unsigned int x, unsigned int y, vec4 color);
void _canvas_fill(canvas *c, const canvas_address *addr);
#define canvas_fill(_c, args...) _canvas_fill((_c), &(canvas_address){ args })
void _canvas_blit(canvas *dst, canvas *src, const canvas_address *addr);
#define canvas_blit(_dst, _src, args...) _canvas_blit((_dst), (_src), &(canvas_address){ args })

/**
 * canvas_normals() - bake a tangent-space normal map from a height field
 * @height: the height field, channel 0, sampled with periodic wrap
 * @amp:    the gradient's scale
 *
 * Return: an RGBA8 canvas of the normals (x along +U, y along +V, z out of
 *         the surface), or an error
 */
cresp(canvas) canvas_normals(canvas *height, float amp);

/**
 * _canvas_to_tex() - upload a canvas into a texture
 * @c:      the canvas
 * @tex:    the texture to initialize and load
 * @opts:   texture options; the size and format come from @c
 *
 * Return: CERR_OK or the texture's initialization or load error
 */
cerr_check _canvas_to_tex(canvas *c, texture_t *tex, const texture_init_options *opts);

/**
 * define canvas_to_tex - upload a canvas into a texture
 * @_c:     the canvas
 * @_t:     the texture
 *
 * Syntax sugar for _canvas_to_tex(), taking texture_init_options
 * initializers.
 */
#define canvas_to_tex(_c, _t, args...) _canvas_to_tex((_c), (_t), &(texture_init_options){ args })

cresp(canvas) canvas_print(struct font *font, texture_format tex_fmt, const canvas_address *addr, unsigned long flags, const char *str);
cresp(canvas) canvas_printf(struct font *font, texture_format tex_fmt, const canvas_address *addr, unsigned long flags, const char *fmt, ...);

cerr tex_print(texture_t *tex, struct font *font, texture_format tex_fmt, float *color, unsigned long flags, const char *str);
cerr tex_printf(texture_t *tex, struct font *font, texture_format tex_fmt, float *color, unsigned long flags, const char *fmt, ...);

#endif /* __CLAP_DRAW_H__ */
