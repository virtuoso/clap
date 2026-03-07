// SPDX-License-Identifier: Apache-2.0
#include <stdarg.h>
#include "draw.h"
#include "error.h"
#include "font.h"
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

void *canvas_data(canvas *c)
{
    return c->data;
}

size_t canvas_size(canvas *c)
{
    return c->width * c->height * texture_format_comp_size(c->fmt) * texture_format_nr_comps(c->fmt);
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

struct draw_text {
    struct font         *font;
    const char          *str;
    unsigned long       flags;
    unsigned int        nr_lines;
    struct draw_text_line {
        /* total width of all glyphs in each line, not counting whitespace */
        unsigned int    line_w;
        /* width of one whitespace for each line */
        unsigned int    line_ws;
        /* number of words in each line */
        unsigned int    line_nrw;
    }                   *line;  /* array of nr_lines elements */
    int                 width, height, y_off;
    int                 margin_x, margin_y;
};

/*
 * TODO:
 *  + optional reflow
 */
static void draw_text_measure(struct draw_text *t)
{
    unsigned int i, w = 0, nr_words = 0, nonws_w = 0, ws_w;
    int h_top = 0, h_bottom = 0;
    size_t len = strlen(t->str);
    struct glyph *glyph;

    mem_free(t->line);

    glyph = font_get_glyph(t->font, '-');
    ws_w = glyph->width;
    for (i = 0; i <= len; i++) {
        if (t->str[i] == '\n' || !t->str[i]) { /* end of line */
            nr_words++;

            t->line = mem_realloc_array(t->line, t->nr_lines + 1, sizeof(*t->line), .fatal_fail = 1);
            t->line[t->nr_lines].line_w = nonws_w;
            t->line[t->nr_lines].line_nrw = nr_words - 1;
            w = max(w, nonws_w + ws_w * (nr_words - 1));
            t->nr_lines++;
            nonws_w = nr_words = 0;
            continue;
        }

        if (isspace(t->str[i])) {
            nr_words++;
            continue;
        }

        glyph = font_get_glyph(t->font, t->str[i]);
        nonws_w += glyph->advance_x >> 6;
        if (glyph->bearing_y < 0) {
            h_top = max(h_top, (int)glyph->height + glyph->bearing_y);
            h_bottom = max(h_bottom, -glyph->bearing_y);
        } else {
            h_top = max(h_top, glyph->bearing_y);
            h_bottom = max(h_bottom, max((int)glyph->height - glyph->bearing_y, 0));
        }
    }

    for (i = 0; i < t->nr_lines; i++)
        if ((t->flags & DRAW_AF_VCENTER) == DRAW_AF_VCENTER)
            t->line[i].line_ws = t->line[i].line_nrw ? (w - t->line[i].line_w) / t->line[i].line_nrw : 0;
        else
            t->line[i].line_ws = ws_w;

    t->width  = w;
    t->y_off = h_top;
    t->height = (h_top + h_bottom) * t->nr_lines;
}

static inline int x_off(struct draw_text *t, unsigned int line)
{
    int x = t->margin_x;

    if (t->flags & DRAW_AF_RIGHT) {
        if (t->flags & DRAW_AF_LEFT) {
            if (t->line[line].line_w)
               x += (t->width - t->line[line].line_w) / 2;
        } else {
            x = t->width + t->margin_x - t->line[line].line_w -
                t->line[line].line_ws * t->line[line].line_nrw;
        }
    }

    return x;
}

cresp(canvas) canvas_print(struct font *font, texture_format tex_fmt, float *color, unsigned long flags, const char *str)
{
    if (!flags) flags = DRAW_AF_VCENTER;

    struct draw_text t = {
        .flags     = flags,
        .margin_x  = 10,
        .margin_y  = 10,
        .str       = str,
        .font      = font,
    };

    draw_text_measure(&t);

    auto width  = t.width + t.margin_x * 2;
    auto height = t.height + t.margin_y * 2;

    LOCAL_SET(canvas, c) = CRES_RET_T(canvas_new(tex_fmt, width, height), canvas);

    canvas_fill(c, (vec4) { 0.3f, 0.3f, 0.3f, 0.3f });

    unsigned int y = t.margin_y + t.y_off;
    dbg_on(y < 0, "y: %d, height: %d y_off: %d, margin_y: %d\n",
           y, t.height, t.y_off, t.margin_y);

    for (int line = 0, i = 0, x = x_off(&t, line); i < strlen(str); i++) {
        if (str[i] == '\n') {
            line++;
            y += (t.height / t.nr_lines);
            x = x_off(&t, line);
            continue;
        }
        if (isspace(str[i])) {
            x += t.line[line].line_ws;
            continue;
        }

        auto glyph = font_get_glyph(t.font, str[i]);

        canvas_blit(c, glyph->canvas, x + glyph->bearing_x, y - glyph->bearing_y, color);
        x += glyph->advance_x >> 6;
    }

    mem_free(t.line);

    return cresp_val(canvas, NOCU(c));
}

cresp(canvas) canvas_printf(struct font *font, texture_format tex_fmt, float *color, unsigned long flags, const char *fmt, ...)
{
    va_list ap;
    LOCAL(char, str);

    va_start(ap, fmt);
    CRES_RET_T(mem_vasprintf(&str, fmt, ap), canvas);
    va_end(ap);

    return canvas_print(font, tex_fmt, color, flags, fmt);
}

cerr tex_print(texture_t *tex, struct font *font, texture_format tex_fmt, float *color,
               unsigned long flags, const char *str)
{
    LOCAL_SET(canvas, c) = CRES_RET_CERR(canvas_print(font, tex_fmt, color, flags, str));
    CERR_RET_CERR(texture_load(tex, tex_fmt, c->width, c->height, c->data));

    return CERR_OK;
}

cerr tex_printf(texture_t *tex, struct font *font, texture_format tex_fmt, float *color,
                unsigned long flags, const char *fmt, ...)
{
    va_list ap;
    LOCAL(char, str);

    va_start(ap, fmt);
    CRES_RET_CERR(mem_vasprintf(&str, fmt, ap));
    va_end(ap);

    return tex_print(tex, font, tex_fmt, color, flags, str);
}
