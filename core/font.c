// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include "common.h"
#include "draw.h"
#include "logger.h"
#include "librarian.h"
#include "util.h"
#include "font.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_OUTLINE_H

struct font {
    char *       name;
    void         *buf;
    FT_Face      face;
    struct glyph g[256];
    struct ref   ref;
};

typedef struct font_context {
    FT_Library  ft;
    struct font *default_font;
} font_context;

const char *font_name(struct font *font)
{
    return font->name;
}

static void font_load_glyph(struct font *font, unsigned char c)
{
    FT_GlyphSlot glyph;
    unsigned int x, y;

    if (FT_Load_Char(font->face, c, FT_LOAD_RENDER)) {
        err("failed to load glyph %c\n", c);
        return;
    }

    glyph = font->face->glyph;
    auto g = &font->g[c];
    g->width = glyph->bitmap.width;
    g->height = glyph->bitmap.rows;
    g->canvas = CRES_RET(canvas_new(TEX_FMT_RGBA8, g->width, g->height), return);
#define _GAT(_x, _y) ((_y) * glyph->bitmap.width + (_x))
    for (y = 0; y < glyph->bitmap.rows; y++)
        for (x = 0; x < glyph->bitmap.width; x++) {
            unsigned int factor = glyph->bitmap.buffer[_GAT(x, y)];
            if (factor)
                canvas_write(g->canvas, .x = x, .y = y, .color = (vec4){ 1.0f, 1.0f, 1.0f, (float)factor / 255.0f });
        }
#undef _GAT
    font->g[c].advance_x = glyph->advance.x;
    font->g[c].advance_y = glyph->advance.y;
    font->g[c].bearing_x = glyph->bitmap_left;
    font->g[c].bearing_y = glyph->bitmap_top;
    font->g[c].loaded    = true;
}

/**
 * font_set_weight() - dial in a variable font's 'wght' axis
 * @ft:     FreeType library
 * @face:   the font's face
 * @weight: the weight, clamped to the axis range
 *
 * Fonts without a 'wght' axis (or without variable axes altogether) are
 * left as they are.
 */
static void font_set_weight(FT_Library ft, FT_Face face, unsigned int weight)
{
    FT_MM_Var *mm;

    if (FT_Get_MM_Var(face, &mm))   return;

    FT_UInt nr_axes = mm->num_axis;
    FT_Fixed *coords = mem_alloc(sizeof(*coords), .nr = nr_axes);

    if (!coords || FT_Get_Var_Design_Coordinates(face, nr_axes, coords))    goto out;

    for (FT_UInt i = 0; i < nr_axes; i++) {
        if (mm->axis[i].tag != FT_MAKE_TAG('w', 'g', 'h', 't')) continue;

        FT_Fixed w = (FT_Fixed)weight << 16;
        coords[i] = w < mm->axis[i].minimum ? mm->axis[i].minimum :
                    w > mm->axis[i].maximum ? mm->axis[i].maximum : w;

        if (FT_Set_Var_Design_Coordinates(face, nr_axes, coords))   err("failed to set font weight %u\n", weight);
        break;
    }

out:
    mem_free(coords);
    FT_Done_MM_Var(ft, mm);
}

static cerr font_make(struct ref *ref, void *_opts)
{
    rc_init_opts(font) *opts = _opts;
    struct font *font = container_of(ref, struct font, ref);

    void *buf;
    size_t buf_size;
    LOCAL_SET(lib_handle, lh) = lib_read_file(RES_ASSET, opts->name, &buf, &buf_size);
    if (!lh) {
        err("failed to fetch font '%s'\n", opts->name);
        return CERR_FONT_NOT_LOADED;
    }

    LOCAL_SET(void, _buf) = memdup(buf, buf_size);

    FT_Face face;
    if (FT_New_Memory_Face(opts->ctx->ft, _buf, buf_size, 0, &face)) {
        err("failed to load font '%s'\n", opts->name);
        return CERR_FONT_NOT_LOADED;
    }

    if (opts->weight)
        CRES_RET_CERR(mem_asprintf(&font->name, "%s:%u:w%u", opts->name, opts->size,
                                   opts->weight));
    else
        CRES_RET_CERR(mem_asprintf(&font->name, "%s:%u", opts->name, opts->size));

    if (opts->weight)   font_set_weight(opts->ctx->ft, face, opts->weight);

    font->buf = NOCU(_buf);
    font->face = face;
    FT_Set_Pixel_Sizes(font->face, opts->size, opts->size);

    return CERR_OK;
}

static void font_drop(struct ref *ref)
{
    struct font *font = container_of(ref, struct font, ref);
    unsigned int i;

    for (i = 0; i < array_size(font->g); i++) {
        canvas_free(font->g[i].canvas);
    }
    mem_free(font->name);
    FT_Done_Face(font->face);
    mem_free(font->buf);
}

DEFINE_REFCLASS2(font);

struct font *font_get(struct font *font)
{
    return ref_get(font);
}

struct font *font_get_default(font_context *ctx)
{
    if (!ctx->default_font)
        return NULL;

    return font_get(ctx->default_font);
}

struct glyph *font_get_glyph(struct font *font, unsigned char c)
{
    if (!font->g[c].loaded)
        font_load_glyph(font, c);
    if (!font->g[c].loaded)
        return NULL;

    return &font->g[c];
}

void glyph_outline_free(glyph_outline *outline)
{
    if (!outline)   return;

    mem_free(outline->points);
    mem_free(outline->contours);
    mem_free(outline);
}

DEFINE_CLEANUP(glyph_outline, glyph_outline_free(*p))

/* FT_Outline_Decompose() state: points and contours collected so far */
typedef struct outline_ctx {
    darray(vec2, points);
    darray(poly_ring, contours);
    float           scale;          /* font units -> em units */
    unsigned int    nr_segments;    /* points per curve, 0: adaptive */
} outline_ctx;

static poly_ring *outline_contour(outline_ctx *ctx)
{
    size_t nr = darray_count(ctx->contours);

    return nr ? darray_get(ctx->contours, nr - 1) : nullptr;
}

/* close the current contour: decompose repeats the first point at the end */
static void outline_close(outline_ctx *ctx)
{
    poly_ring *c = outline_contour(ctx);
    if (!c)     return;

    if (c->nr >= 2) {
        vec2 *first = darray_get(ctx->points, c->start);
        vec2 *last = darray_get(ctx->points, c->start + c->nr - 1);

        if ((*first)[0] == (*last)[0] && (*first)[1] == (*last)[1]) {
            darray_resize(ctx->points, c->start + c->nr - 1);
            c->nr--;
        }
    }

    /* fewer than 3 points don't make a contour */
    if (c->nr < 3) {
        darray_resize(ctx->points, c->start);
        darray_resize(ctx->contours, darray_count(ctx->contours) - 1);
    }
}

static int outline_point(outline_ctx *ctx, float x, float y)
{
    poly_ring *c = outline_contour(ctx);
    if (!c)     return -1;      /* a segment before any move_to: malformed */

    if (c->nr) {
        vec2 *last = darray_get(ctx->points, c->start + c->nr - 1);
        if ((*last)[0] == x && (*last)[1] == y)     return 0;
    }

    vec2 *p = darray_add(ctx->points);
    if (!p)     return -1;

    (*p)[0] = x;
    (*p)[1] = y;
    c->nr++;

    return 0;
}

static int outline_move_to(const FT_Vector *to, void *user)
{
    outline_ctx *ctx = user;

    outline_close(ctx);

    poly_ring *c = darray_add(ctx->contours);
    if (!c)     return -1;

    c->start = darray_count(ctx->points);

    return outline_point(ctx, to->x * ctx->scale, to->y * ctx->scale);
}

static int outline_line_to(const FT_Vector *to, void *user)
{
    outline_ctx *ctx = user;

    return outline_point(ctx, to->x * ctx->scale, to->y * ctx->scale);
}

/* the default number of points per curve: proportional to its size */
#define FLATTEN_PER_EM      32.0f
#define FLATTEN_STEPS_MIN   2
#define FLATTEN_STEPS_MAX   16

static unsigned int outline_steps(outline_ctx *ctx, float len)
{
    if (ctx->nr_segments)   return ctx->nr_segments;

    return clamp((int)(len * FLATTEN_PER_EM), FLATTEN_STEPS_MIN, FLATTEN_STEPS_MAX);
}

/* control polygon length: cheap upper bound estimate of a curve's length */
static float control_len(const vec2 p[], int nr)
{
    float len = 0.0f;

    for (int i = 0; i < nr - 1; i++) {
        vec2 d;
        vec2_sub(d, p[i + 1], p[i]);
        len += vec2_len(d);
    }

    return len;
}

static int outline_conic_to(const FT_Vector *ctrl, const FT_Vector *to, void *user)
{
    outline_ctx *ctx = user;
    poly_ring *c = outline_contour(ctx);

    if (!c || !c->nr)   return -1;

    vec2 *p0 = darray_get(ctx->points, c->start + c->nr - 1);
    vec2 p[3] = {
        { (*p0)[0], (*p0)[1] },
        { ctrl->x * ctx->scale, ctrl->y * ctx->scale },
        { to->x * ctx->scale, to->y * ctx->scale },
    };

    unsigned int steps = outline_steps(ctx, control_len(p, 3));
    for (unsigned int k = 1; k < steps; k++) {
        float t = (float)k / steps, u = 1.0f - t;
        float x = u * u * p[0][0] + 2.0f * u * t * p[1][0] + t * t * p[2][0];
        float y = u * u * p[0][1] + 2.0f * u * t * p[1][1] + t * t * p[2][1];

        if (outline_point(ctx, x, y))   return -1;
    }

    return outline_point(ctx, p[2][0], p[2][1]);
}

static int outline_cubic_to(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to,
                            void *user)
{
    outline_ctx *ctx = user;
    poly_ring *c = outline_contour(ctx);

    if (!c || !c->nr)   return -1;

    vec2 *p0 = darray_get(ctx->points, c->start + c->nr - 1);
    vec2 p[4] = {
        { (*p0)[0], (*p0)[1] },
        { c1->x * ctx->scale, c1->y * ctx->scale },
        { c2->x * ctx->scale, c2->y * ctx->scale },
        { to->x * ctx->scale, to->y * ctx->scale },
    };

    unsigned int steps = outline_steps(ctx, control_len(p, 4));
    for (unsigned int k = 1; k < steps; k++) {
        float t = (float)k / steps, u = 1.0f - t;
        float x = u * u * u * p[0][0] + 3.0f * u * u * t * p[1][0] +
                  3.0f * u * t * t * p[2][0] + t * t * t * p[3][0];
        float y = u * u * u * p[0][1] + 3.0f * u * u * t * p[1][1] +
                  3.0f * u * t * t * p[2][1] + t * t * t * p[3][1];

        if (outline_point(ctx, x, y))   return -1;
    }

    return outline_point(ctx, p[3][0], p[3][1]);
}

cresp(glyph_outline) font_glyph_outline(struct font *font, unsigned char c,
                                        unsigned int nr_segments)
{
    if (!font)  return cresp_error(glyph_outline, CERR_INVALID_ARGUMENTS);

    FT_Face face = font->face;
    if (!face->units_per_EM)    return cresp_error(glyph_outline, CERR_NOT_SUPPORTED);

    if (FT_Load_Char(face, c, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP))
        return cresp_error(glyph_outline, CERR_NOT_FOUND);

    FT_GlyphSlot slot = face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_OUTLINE)    return cresp_error(glyph_outline, CERR_NOT_SUPPORTED);

    outline_ctx ctx = { .scale = 1.0f / face->units_per_EM, .nr_segments = nr_segments };
    darray_init(ctx.points);
    darray_init(ctx.contours);

    const FT_Outline_Funcs funcs = {
        .move_to    = outline_move_to,
        .line_to    = outline_line_to,
        .conic_to   = outline_conic_to,
        .cubic_to   = outline_cubic_to,
    };

    if (FT_Outline_Decompose(&slot->outline, &funcs, &ctx)) goto err_arrays;

    outline_close(&ctx);

    glyph_outline *outline = mem_alloc(sizeof(*outline), .zero = 1);
    if (!outline)   goto err_arrays;

    outline->nr_points   = darray_count(ctx.points);
    outline->nr_contours = darray_count(ctx.contours);
    outline->points      = ctx.points.x;
    outline->contours    = ctx.contours.x;
    outline->advance     = slot->metrics.horiAdvance * ctx.scale;

    return cresp_val(glyph_outline, outline);

err_arrays:
    darray_clearout(ctx.points);
    darray_clearout(ctx.contours);
    return cresp_error(glyph_outline, CERR_NOMEM);
}

float font_glyph_kerning(struct font *font, unsigned char left, unsigned char right)
{
    if (!font || !font->face->units_per_EM || !FT_HAS_KERNING(font->face))  return 0.0f;

    FT_UInt l = FT_Get_Char_Index(font->face, left);
    FT_UInt r = FT_Get_Char_Index(font->face, right);
    if (!l || !r)   return 0.0f;

    FT_Vector delta;
    if (FT_Get_Kerning(font->face, l, r, FT_KERNING_UNSCALED, &delta))  return 0.0f;

    return (float)delta.x / font->face->units_per_EM;
}

float font_line_height(struct font *font)
{
    if (!font || !font->face->units_per_EM)     return 0.0f;

    return (float)font->face->height / font->face->units_per_EM;
}

void font_put(struct font *font)
{
    ref_put(font);
}

DEFINE_CLEANUP(font_context, if (*p) mem_free(*p))

#define DEFAULT_FONT_NAME "ofl/Unbounded-Regular.ttf"
cresp(font_context) font_init(const char *default_font_name)
{
    LOCAL_SET(font_context, ctx) = mem_alloc(sizeof(*ctx));
    if (!ctx)
        return cresp_error(font_context, CERR_NOMEM);

    FT_Error err = FT_Init_FreeType(&ctx->ft);
    if (err != FT_Err_Ok)
        return cresp_error(font_context, CERR_INITIALIZATION_FAILED);

    if (!default_font_name)
        default_font_name = DEFAULT_FONT_NAME;

    ctx->default_font = CRES_RET(
        ref_new_checked(font, .ctx = ctx, .name = default_font_name, .size = 32),
        { err("couldn't load default font\n"); return cresp_error_cerr(font_context, __resp); }
    );

    dbg("freetype initialized\n");
    return cresp_val(font_context, NOCU(ctx));
}

void font_done(font_context *ctx)
{
    if (ctx->default_font)
        font_put(ctx->default_font);
    FT_Done_FreeType(ctx->ft);
    mem_free(ctx);
}
