// SPDX-License-Identifier: Apache-2.0
#include "draw.h"
#include "error.h"
#include "linmath.h"
#include "memory.h"
#include "render.h"

static void u8_to_f32(void *dst, void *v)
{
    *(float *)dst = (uchar)clampf((float)*(uchar *)v / 255.0f, 0.0f, 255.0f);
}

static void f32_to_u8(void *dst, void *v)
{
    *(uchar *)dst = (uchar)clampf(*(float *)v * 255.0f, 0.0f, 255.0f);
}

static void f32_to_f32(void *dst, void *v)
{
    *(float *)dst = *(float *)v;
}

DEFINE_CLEANUP(canvas, if (*p) canvas_free(*p))

cresp(canvas) canvas_new(texture_format fmt, unsigned int width, unsigned int height)
{
    size_t comp_sz = texture_format_comp_size(fmt);
    size_t txsz = comp_sz * texture_format_nr_comps(fmt);

    LOCAL_SET(canvas, c) = mem_alloc(sizeof(*c) + txsz * width * height, .zero = 1);
    if (!c) return cresp_error(canvas, CERR_NOMEM);

    c->fmt = fmt;
    c->width = width;
    c->height = height;

    switch (comp_sz) {
        case 1:     c->conv_write = f32_to_u8; c->conv_read = u8_to_f32; break;
        case 4:     c->conv_write = c->conv_read = f32_to_f32; break;
        default:    return cresp_error(canvas, CERR_INVALID_FORMAT);
    }
    return cresp_val(canvas, NOCU(c));
}

void canvas_free(canvas *c)
{
    mem_free(c);
}

void canvas_write(canvas *c, unsigned int x, unsigned int y, vec4 color)
{
    size_t nr_comps = texture_format_nr_comps(c->fmt);
    size_t comp_sz = texture_format_comp_size(c->fmt);
    size_t txsz = comp_sz * nr_comps;

    for (size_t i = 0; i < nr_comps; i++) {
        void *p = c->data + txsz * (y * c->width + x) + comp_sz * i;
        c->conv_write(p, &color[i]);
    }
}

void canvas_read(canvas *c, unsigned int x, unsigned int y, vec4 color)
{
    size_t nr_comps = texture_format_nr_comps(c->fmt);
    size_t comp_sz = texture_format_comp_size(c->fmt);
    size_t txsz = comp_sz * nr_comps;

    for (size_t i = 0; i < nr_comps; i++) {
        void *p = c->data + txsz * (y * c->width + x) + comp_sz * i;
        c->conv_read(&color[i], p);
    }
}

void canvas_fill(canvas *c, vec4 color)
{
    for (unsigned int y = 0; y < c->height; y++)
        for (unsigned int x = 0; x < c->width; x++)
            canvas_write(c, x, y, color);
}

void canvas_blit(canvas *dst, canvas *src, unsigned int x, unsigned int y, float *color)
{
    size_t nr_comps = min(texture_format_nr_comps(src->fmt), texture_format_nr_comps(dst->fmt));
    unsigned int dx, dy;

    for (dy = y; dy < min(y + src->height, dst->height); dy++)
        for (dx = x; dx < min(x + src->width, dst->width); dx++) {
            vec4 texel;
            canvas_read(src, dx - x, dy - y, texel);

            if (vec4_mul_inner(texel, texel) < 1e-5)    continue;

            if (color)
                for (size_t i = 0; i < nr_comps; i++)
                    texel[i] *= color[i];

            canvas_write(dst, dx, dy, texel);
        }
}
