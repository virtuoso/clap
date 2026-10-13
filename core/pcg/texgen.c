// SPDX-License-Identifier: Apache-2.0
#include <math.h>
#include "interp.h"
#include "noise.h"
#include "pcg/texgen.h"

/*
 * hash31() streams: the painters draw their per-texel, per-board and
 * per-tile values from hash31(x, y, z, seed), every kind of draw on a z
 * of its own, so that one draw doesn't echo another
 */
static const int texgen_z_speckle = 1, texgen_z_board_tone = 2, texgen_z_glow_level = 3, texgen_z_board_lift = 4,
                 texgen_z_tile_tone = 7, texgen_z_fuzz = 9, texgen_z_fabric = 11, texgen_z_paper = 13,
                 texgen_z_monitor_grain = 21;
/* a per-board draw's y: the board is its x */
static const int texgen_y_board_tone = 37, texgen_y_board_lift = 53;
/* a board's grain noise is seeded this far on from the previous board's */
static const uint32_t texgen_board_seed_step = 131u;
/* a paper keeps this many texels clear of the border all round */
static const int texgen_paper_margin = 2;
/* the content lines on a light paper are this many texels apart */
static const int texgen_paper_line_pitch = 5;

void pcg_texgen_maps_done(pcg_texgen_maps *m)
{
    canvas_free(m->albedo);
    canvas_free(m->normal);
    m->albedo = m->normal = nullptr;
}

DEFINE_CLEANUP_EXACT(pcg_texgen_maps, pcg_texgen_maps_done(p))

/* the albedo canvas, and the height canvas when there is relief to bake */
static cerr texgen_canvases(int w, int h, float bump, canvas **alb, canvas **hgt)
{
    if (w < 1 || h < 1)     return CERR_INVALID_ARGUMENTS_REASON(.fmt = "texture of no size");
    if (!(bump >= 0.0f))    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "negative relief");

    *alb = CRES_RET_CERR(canvas_new(TEX_FMT_RGBA8, w, h));
    if (bump > 0.0f)    *hgt = CRES_RET_CERR(canvas_new(TEX_FMT_R32F, w, h));

    return CERR_OK;
}

/* hand out the albedo and the normal map baked from @hgt, if any */
static cres(pcg_texgen_maps) texgen_maps_bake(canvas **alb, canvas *hgt, float bump)
{
    LOCAL_SET(canvas, nrm) = nullptr;

    if (hgt)    nrm = CRES_RET_SCALAR(canvas_normals(hgt, bump), pcg_texgen_maps);

    return cres_val(pcg_texgen_maps, ((pcg_texgen_maps){ .albedo = NOCU(*alb), .normal = NOCU(nrm) }));
}

cres(pcg_texgen_maps) pcg_texgen_surface(const pcg_texgen_surface_opts *o)
{
    if (!o)             return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS);
    if (o->cells < 1)   return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS, .fmt = "no blotch cells");

    LOCAL_SET(canvas, alb) = nullptr;
    LOCAL_SET(canvas, hgt) = nullptr;
    CERR_RET_SCALAR(texgen_canvases(o->size, o->size, o->bump, &alb, &hgt), pcg_texgen_maps);

    for (int y = 0; y < o->size; y++)
        for (int x = 0; x < o->size; x++) {
            float u = (float)x / o->size * o->cells, v = (float)y / o->size * o->cells;
            float blotch = fbm2_periodic(u, v, 4, 2.0f, 0.5f, o->cells, o->cells, o->seed) - 0.5f;
            float speckle = hash31(x, y, texgen_z_speckle, o->seed) - 0.5f;
            float l = 1.0f + blotch * 2.0f * o->blotch + speckle * 2.0f * o->speckle;

            canvas_write(alb, .x = x, .y = y, .color = (vec4){
                o->base[0] * l, o->base[1] * l, o->base[2] * l, o->alpha,
            });
            /* the lightness sources double as relief: blotchy swell, speckle pits */
            if (hgt)
                canvas_write(hgt, .x = x, .y = y,
                             .color = (vec4){ 0.5f + blotch + speckle * 0.5f });
        }

    return texgen_maps_bake(&alb, hgt, o->bump);
}

cres(pcg_texgen_maps) pcg_texgen_wood(const pcg_texgen_wood_opts *o)
{
    if (!o)             return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS);
    if (o->planks < 1)  return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS, .fmt = "no planks");

    LOCAL_SET(canvas, alb) = nullptr;
    LOCAL_SET(canvas, hgt) = nullptr;
    CERR_RET_SCALAR(texgen_canvases(o->size, o->size, o->bump, &alb, &hgt), pcg_texgen_maps);

    for (int y = 0; y < o->size; y++)
        for (int x = 0; x < o->size; x++) {
            float uu = (float)x / o->size, vv = (float)y / o->size;
            float across = (o->along_u ? vv : uu) * o->planks;
            float along = o->along_u ? uu : vv;
            int plank = (int)across % o->planks;
            float within = across - floorf(across);

            float tone = hash31(plank, texgen_y_board_tone, texgen_z_board_tone, o->seed);
            /* streaks: high frequency across the board, low along it */
            float g = fbm2_periodic(across * 5.0f, along * 24.0f, 3, 2.0f, 0.5f,
                                    o->planks * 5, 24, o->seed + texgen_board_seed_step * (uint32_t)plank);
            float l = 1.0f + (g - 0.5f) * 2.0f * o->grain;
            float h = hash31(plank, texgen_y_board_lift, texgen_z_board_lift, o->seed) * 0.25f + (g - 0.5f) * 0.15f;

            /* darken the board seams, sink them in relief */
            float edge = fminf(within, 1.0f - within);
            if (edge < o->seam) {
                l *= o->seam_floor + (1.0f - o->seam_floor) * edge / o->seam;
                h -= (1.0f - edge / o->seam) * 0.35f;
            }

            canvas_write(alb, .x = x, .y = y, .color = (vec4){
                linf_interp(o->light[0], o->dark[0], tone) * l,
                linf_interp(o->light[1], o->dark[1], tone) * l,
                linf_interp(o->light[2], o->dark[2], tone) * l,
                1.0f,
            });
            if (hgt)    canvas_write(hgt, .x = x, .y = y, .color = (vec4){ 0.5f + h });
        }

    return texgen_maps_bake(&alb, hgt, o->bump);
}

cres(pcg_texgen_maps) pcg_texgen_carpet(const pcg_texgen_carpet_opts *o)
{
    if (!o)             return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS);
    if (o->tiles < 1)   return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS, .fmt = "no carpet tiles");

    LOCAL_SET(canvas, alb) = nullptr;
    LOCAL_SET(canvas, hgt) = nullptr;
    CERR_RET_SCALAR(texgen_canvases(o->size, o->size, o->bump, &alb, &hgt), pcg_texgen_maps);

    for (int y = 0; y < o->size; y++)
        for (int x = 0; x < o->size; x++) {
            float tu = (float)x / o->size * o->tiles, tv = (float)y / o->size * o->tiles;
            int ix = (int)tu, iy = (int)tv;
            float fu = tu - ix, fv = tv - iy;

            float tone = hash31(ix, iy, texgen_z_tile_tone, o->seed);
            /* pile rows: dense across the pile direction, loose along it */
            float g = (ix + iy) & 1 ?
                fbm2_periodic(tu * 3.0f, tv * 12.0f, 3, 2.0f, 0.5f,
                              o->tiles * 3, o->tiles * 12, o->seed) :
                fbm2_periodic(tu * 12.0f, tv * 3.0f, 3, 2.0f, 0.5f,
                              o->tiles * 12, o->tiles * 3, o->seed);
            float fuzz = (hash31(x, y, texgen_z_fuzz, o->seed) - 0.5f) * 2.0f * o->fuzz;
            float l = 1.0f + (g - 0.5f) * 2.0f * o->grain + fuzz;
            float h = tone * 0.15f + (g - 0.5f) * 0.3f + fuzz * 0.5f;

            /* sink the tile seams */
            float edge = fminf(fminf(fu, 1.0f - fu), fminf(fv, 1.0f - fv));
            if (edge < o->seam) {
                l *= o->seam_floor + (1.0f - o->seam_floor) * edge / o->seam;
                h -= (1.0f - edge / o->seam) * 0.2f;
            }

            canvas_write(alb, .x = x, .y = y, .color = (vec4){
                linf_interp(o->base[0], o->accent[0], tone) * l,
                linf_interp(o->base[1], o->accent[1], tone) * l,
                linf_interp(o->base[2], o->accent[2], tone) * l,
                1.0f,
            });
            if (hgt)    canvas_write(hgt, .x = x, .y = y, .color = (vec4){ 0.5f + h });
        }

    return texgen_maps_bake(&alb, hgt, o->bump);
}

cres(pcg_texgen_maps) pcg_texgen_cork(const pcg_texgen_cork_opts *o)
{
    if (!o)                     return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS);
    if (o->w < 1 || o->h < 1)   return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS, .fmt = "board of no size");
    if (o->papers < 1)          return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS, .fmt = "no papers");
    if (!(o->border_w >= 0.0f && o->border_w <= 0.5f))
        return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS, .fmt = "border width outside 0..0.5");
    if (!(o->paper_w > 0.0f && o->paper_w_var >= 0.0f && o->paper_w + o->paper_w_var <= 1.0f))
        return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS, .fmt = "paper widths outside 0..1");

    LOCAL_SET(canvas, alb) = nullptr;
    LOCAL_SET(canvas, hgt) = nullptr;
    CERR_RET_SCALAR(texgen_canvases(o->w, o->h, 0.0f, &alb, &hgt), pcg_texgen_maps);
    int bw = (int)(o->border_w * fminf(o->w, o->h));

    for (int y = 0; y < o->h; y++)
        for (int x = 0; x < o->w; x++) {
            float uu = (float)x / o->w, vv = (float)y / o->h;
            float l = 1.0f + (hash31(x, y, texgen_z_fabric, o->seed) - 0.5f) * 0.14f
                    + (fbm2_periodic(uu * 6.0f, vv * 6.0f, 3, 2.0f, 0.5f, 6, 6, o->seed)
                       - 0.5f) * 0.16f;
            bool border = x < bw || x >= o->w - bw || y < bw || y >= o->h - bw;
            const float *c = border ? o->border : o->base;

            canvas_write(alb, .x = x, .y = y,
                         .color = (vec4){ c[0] * l, c[1] * l, c[2] * l, 1.0f });
        }

    /* pinned papers: axis-aligned rectangles in a few paper tones */
    for (int i = 0; i < o->papers; i++) {
        int pw = (int)((o->paper_w + o->paper_w_var * hash31(i, 1, texgen_z_paper, o->seed)) * o->w);
        int ph = (int)(pw * (1.1f + 0.5f * hash31(i, 2, texgen_z_paper, o->seed)));
        /*
         * The papers scatter over the room the interior leaves beside
         * them; a paper bigger than the interior has none and starts at
         * its corner, cut at the far border.
         */
        int m = texgen_paper_margin;
        int room_x = max(o->w - 2 * bw - pw - 2 * m, 0), room_y = max(o->h - 2 * bw - ph - 2 * m, 0);
        int px = bw + m + (int)(hash31(i, 3, texgen_z_paper, o->seed) * room_x);
        int py = bw + m + (int)(hash31(i, 4, texgen_z_paper, o->seed) * room_y);
        float t = hash31(i, 5, texgen_z_paper, o->seed);
        float *pc = t < 0.55f ? (float[]){ 0.92f, 0.90f, 0.85f, 1.0f } :
                    t < 0.80f ? (float[]){ 0.55f, 0.56f, 0.58f, 1.0f } :
                                (float[]){ 0.90f, 0.84f, 0.58f, 1.0f };

        for (int y = py; y < py + ph && y < o->h - bw; y++)
            for (int x = px; x < px + pw && x < o->w - bw; x++) {
                /* light papers get faint content lines */
                bool line = pc[0] > 0.8f && y > py + ph / 4 && !((y - py) % texgen_paper_line_pitch) &&
                            x > px + pw / 6 && x < px + pw - pw / 6;

                if (line)
                    canvas_write(alb, .x = x, .y = y,
                                 .color = (vec4){ pc[0] * 0.55f, pc[1] * 0.55f,
                                                  pc[2] * 0.55f, 1.0f });
                else
                    canvas_write(alb, .x = x, .y = y, .color = pc);
            }

        /* the pin, two texels down from the paper's top edge, unless the border cuts it */
        for (int y = py + 1; y <= py + 2; y++) {
            int x = px + pw / 2;

            if (x >= o->w - bw || y >= o->h - bw)   continue;
            canvas_write(alb, .x = x, .y = y, .color = (vec4){ 0.75f, 0.16f, 0.12f, 1.0f });
        }
    }

    return texgen_maps_bake(&alb, hgt, 0.0f);
}

cresp(canvas) pcg_texgen_glow_grid(const pcg_texgen_glow_grid_opts *o)
{
    if (!o)                 return cresp_error(canvas, CERR_INVALID_ARGUMENTS);
    if (o->size < 1)        return cresp_error(canvas, CERR_INVALID_ARGUMENTS, .fmt = "texture of no size");
    if (o->cells_u < 1 || o->cells_v < 1)   return cresp_error(canvas, CERR_INVALID_ARGUMENTS, .fmt = "no coffers");

    LOCAL_SET(canvas, c) = CRES_RET_T(canvas_new(TEX_FMT_RGB8, o->size, o->size), canvas);

    for (int y = 0; y < o->size; y++)
        for (int x = 0; x < o->size; x++) {
            float cu = (float)x / o->size * o->cells_u, cv = (float)y / o->size * o->cells_v;
            float eu = fminf(cu - floorf(cu), 1.0f - (cu - floorf(cu)));
            float ev = fminf(cv - floorf(cv), 1.0f - (cv - floorf(cv)));
            bool lit = fminf(eu, ev) > o->rim;

            /* slight per-coffer level variation */
            float l = lit ? o->lit + o->lit_var * hash31((int)cu, (int)cv, texgen_z_glow_level, o->seed) : o->unlit;

            canvas_write(c, .x = x, .y = y,
                         .color = (vec4){ o->glow[0] * l, o->glow[1] * l, o->glow[2] * l, 1.0f });
        }

    return cresp_val(canvas, NOCU(c));
}

cresp(canvas) pcg_texgen_flat(const pcg_texgen_flat_opts *o)
{
    if (!o)             return cresp_error(canvas, CERR_INVALID_ARGUMENTS);
    if (o->size < 1)    return cresp_error(canvas, CERR_INVALID_ARGUMENTS, .fmt = "texture of no size");

    LOCAL_SET(canvas, c) = CRES_RET_T(canvas_new(TEX_FMT_RGB8, o->size, o->size), canvas);
    canvas_fill(c, .color = (vec4){ o->color[0], o->color[1], o->color[2], 1.0f });

    return cresp_val(canvas, NOCU(c));
}

/* the monitor atlas's halves: the screen is the right half, centered in it */
typedef struct texgen_monitor_split {
    int     half;
    float   cu, ru;
    float   cv;
} texgen_monitor_split;

static cerr texgen_monitor_check(const pcg_texgen_monitor_opts *o, texgen_monitor_split *split)
{
    if (!o)             return CERR_INVALID_ARGUMENTS;
    if (o->size < 5)    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "monitor atlas without a screen");
    if (!(o->grain >= 0.0f) || !isfinite(o->grain) || !(o->ramp > 0.0f) || !isfinite(o->ramp))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "monitor grain or glow ramp out of range");

    split->half = o->size / 2;
    split->ru = (o->size - split->half - 1) * 0.5f;
    split->cu = split->half + split->ru;
    split->cv = (o->size - 1) * 0.5f;

    return CERR_OK;
}

cres(pcg_texgen_maps) pcg_texgen_monitor(const pcg_texgen_monitor_opts *o)
{
    texgen_monitor_split split;
    CERR_RET_SCALAR(texgen_monitor_check(o, &split), pcg_texgen_maps);

    LOCAL_SET(canvas, alb) = nullptr;
    LOCAL_SET(canvas, hgt) = nullptr;
    CERR_RET_SCALAR(texgen_canvases(o->size, o->size, 0.0f, &alb, &hgt), pcg_texgen_maps);

    for (int y = 0; y < o->size; y++)
        for (int x = 0; x < o->size; x++) {
            bool screen = x >= split.half;
            float l = 1.0f + (hash31(x, y, texgen_z_monitor_grain, o->seed) - 0.5f) * o->grain;

            canvas_write(alb, .x = x, .y = y, .color = screen ?
                         (vec4){ o->screen[0], o->screen[1], o->screen[2], 1.0f } :
                         (vec4){ o->shell[0] * l, o->shell[1] * l, o->shell[2] * l, 1.0f });
        }

    return texgen_maps_bake(&alb, hgt, 0.0f);
}

cresp(canvas) pcg_texgen_monitor_glow(const pcg_texgen_monitor_opts *o)
{
    texgen_monitor_split split;
    CERR_RET_T(texgen_monitor_check(o, &split), canvas);

    LOCAL_SET(canvas, c) = CRES_RET_T(canvas_new(TEX_FMT_RGB8, o->size, o->size), canvas);

    for (int y = 0; y < o->size; y++)
        for (int x = 0; x < o->size; x++) {
            bool screen = x >= split.half;
            /* the glow vignettes toward the screen's edges */
            float gu = screen ? 1.0f - fabsf((x - split.cu) / split.ru) : 0.0f;
            float gv = 1.0f - fabsf((y - split.cv) / split.cv);
            float g = screen ? fminf(1.0f, o->ramp * fminf(gu, gv)) : 0.0f;

            canvas_write(c, .x = x, .y = y,
                         .color = (vec4){ o->glow[0] * g, o->glow[1] * g, o->glow[2] * g, 1.0f });
        }

    return cresp_val(canvas, NOCU(c));
}

cresp(texture_t) pcg_texgen_upload(renderer_t *r, canvas *c, texture_wrap wrap, texture_filter filter)
{
    if (!r || !c)   return cresp_error(texture_t, CERR_INVALID_ARGUMENTS);

    LOCAL_SET(texture_t, tex) = CRES_RET_T(
        texture_new(.renderer = r, .format = c->fmt, .wrap = wrap, .min_filter = filter, .mag_filter = filter),
        texture_t
    );
    CERR_RET_T(texture_load(tex, c->fmt, c->width, c->height, canvas_data(c)), texture_t);

    return cresp_val(texture_t, NOCU(tex));
}
