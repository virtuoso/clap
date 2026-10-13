// SPDX-License-Identifier: Apache-2.0
/*
 * Texture generator and preset recipe tests, built as the standalone
 * pcg_texgen_tests binary: the painters and the recipes with the noise
 * and canvas helpers and the shared test harness, no renderer.
 */
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "test.h"
#include "util.h"
#include "pcg/material.h"
#include "pcg/texgen.h"

/*
 * pcg_texgen_upload() makes counted textures; there is no renderer here, so
 * hand out bare allocations that keep the options they were made with
 * (test-stubs-render.c stubs the loading)
 */
struct stub_texture {
    texture_t               tex;
    texture_init_options    opts;
};

/* the options @tex was made with */
static const texture_init_options *stub_texture_opts(texture_t *tex)
{
    return &container_of(tex, struct stub_texture, tex)->opts;
}

cresp(texture_t) _texture_new(const texture_init_options *opts)
{
    struct stub_texture *st = mem_alloc(sizeof(*st), .zero = 1);
    if (!st)    return cresp_error(texture_t, CERR_NOMEM);

    st->opts = *opts;

    return cresp_val(texture_t, &st->tex);
}

void texture_done(texture_t *tex)
{
    mem_free(container_of(tex, struct stub_texture, tex));
}

DEFINE_CLEANUP(texture_t, texture_done(*p))

static bool failed(cerr err)
{
    return IS_CERR(err);
}

static bool invalid(cerr err)
{
    return IS_CERR_CODE(err, CERR_INVALID_ARGUMENTS);
}

static bool canvas_is(canvas *c, texture_format fmt, unsigned int w, unsigned int h)
{
    return c && c->fmt == fmt && c->width == w && c->height == h;
}

static bool canvas_same(canvas *a, canvas *b)
{
    return canvas_size(a) == canvas_size(b) && !memcmp(canvas_data(a), canvas_data(b), canvas_size(a));
}

static const pcg_texgen_surface_opts surface_opts = {
    .size = 64, .base = { 0.62f, 0.78f, 0.82f }, .blotch = 0.06f, .speckle = 0.02f, .cells = 3,
    .alpha = 0.35f, .bump = 2.0f, .seed = 0x1234,
};

static const pcg_texgen_wood_opts wood_opts = {
    .size = 48, .planks = 6, .light = { 0.64f, 0.47f, 0.30f }, .dark = { 0.50f, 0.35f, 0.21f },
    .grain = 0.16f, .seam = 0.05f, .seam_floor = 0.55f, .bump = 4.0f, .seed = 0x5678,
};

static const pcg_texgen_carpet_opts carpet_opts = {
    .size = 40, .tiles = 4, .base = { 0.10f, 0.16f, 0.28f }, .accent = { 0.16f, 0.24f, 0.38f },
    .grain = 0.16f, .fuzz = 0.05f, .seam = 0.05f, .seam_floor = 0.78f, .bump = 1.5f, .seed = 0x9abc,
};

static const pcg_texgen_cork_opts cork_opts = {
    .w = 96, .h = 72, .base = { 0.82f, 0.76f, 0.66f }, .border = { 0.30f, 0.22f, 0.15f },
    .border_w = 0.035f, .paper_w = 0.10f, .paper_w_var = 0.08f, .papers = 15, .seed = 0xb0,
};

static const pcg_texgen_glow_grid_opts glow_opts = {
    .size = 32, .cells_u = 4, .cells_v = 3, .glow = { 1.0f, 0.9f, 0.72f }, .rim = 0.06f, .lit = 0.8f,
    .lit_var = 0.2f, .unlit = 0.04f, .seed = 0x51,
};

static const pcg_texgen_monitor_opts monitor_opts = {
    .size = 64, .shell = { 0.88f, 0.87f, 0.83f }, .screen = { 0.07f, 0.10f, 0.12f },
    .glow = { 0.35f, 0.55f, 0.60f }, .grain = 0.08f, .ramp = 2.5f, .seed = 0xa781,
};

static int texgen_formats_test(void)
{
    pcg_texgen_surface_opts so = surface_opts;
    LOCAL_SET_EXACT(pcg_texgen_maps, surface) = CRES_RET(pcg_texgen_surface(&so), return EXIT_FAILURE);
    if (!canvas_is(surface.albedo, TEX_FMT_RGBA8, 64, 64))  return EXIT_FAILURE;
    if (!canvas_is(surface.normal, TEX_FMT_RGBA8, 64, 64))  return EXIT_FAILURE;

    /* the alpha channel is the option, verbatim, on every texel */
    uchar *px = canvas_data(surface.albedo);
    for (size_t i = 0; i < 64 * 64; i++)
        if (px[i * 4 + 3] != (uchar)(0.35f * 255.0f))   return EXIT_FAILURE;

    /* no relief, no normal map */
    so.bump = 0.0f;
    LOCAL_SET_EXACT(pcg_texgen_maps, bare) = CRES_RET(pcg_texgen_surface(&so), return EXIT_FAILURE);
    if (bare.normal)    return EXIT_FAILURE;

    LOCAL_SET_EXACT(pcg_texgen_maps, wood) = CRES_RET(pcg_texgen_wood(&wood_opts), return EXIT_FAILURE);
    if (!canvas_is(wood.albedo, TEX_FMT_RGBA8, 48, 48))     return EXIT_FAILURE;
    if (!canvas_is(wood.normal, TEX_FMT_RGBA8, 48, 48))     return EXIT_FAILURE;

    LOCAL_SET_EXACT(pcg_texgen_maps, carpet) = CRES_RET(pcg_texgen_carpet(&carpet_opts), return EXIT_FAILURE);
    if (!canvas_is(carpet.albedo, TEX_FMT_RGBA8, 40, 40))   return EXIT_FAILURE;
    if (!canvas_is(carpet.normal, TEX_FMT_RGBA8, 40, 40))   return EXIT_FAILURE;

    /* opaque generators */
    canvas *opaque[] = { wood.albedo, carpet.albedo };
    for (size_t i = 0; i < array_size(opaque); i++) {
        px = canvas_data(opaque[i]);
        for (size_t t = 0; t < opaque[i]->width * opaque[i]->height; t++)
            if (px[t * 4 + 3] != 255)   return EXIT_FAILURE;
    }

    LOCAL_SET_EXACT(pcg_texgen_maps, cork) = CRES_RET(pcg_texgen_cork(&cork_opts), return EXIT_FAILURE);
    if (!canvas_is(cork.albedo, TEX_FMT_RGBA8, 96, 72) || cork.normal)  return EXIT_FAILURE;

    LOCAL_SET(canvas, glow) = CRES_RET(pcg_texgen_glow_grid(&glow_opts), return EXIT_FAILURE);
    if (!canvas_is(glow, TEX_FMT_RGB8, 32, 32))             return EXIT_FAILURE;

    const pcg_texgen_flat_opts fo = { .size = 8, .color = { 1.0f, 0.55f, 0.22f } };
    LOCAL_SET(canvas, flat) = CRES_RET(pcg_texgen_flat(&fo), return EXIT_FAILURE);
    if (!canvas_is(flat, TEX_FMT_RGB8, 8, 8))               return EXIT_FAILURE;

    px = canvas_data(flat);
    for (size_t t = 0; t < 8 * 8; t++)
        if (px[t * 3] != 255 || px[t * 3 + 1] != (uchar)(0.55f * 255.0f) || px[t * 3 + 2] != (uchar)(0.22f * 255.0f))
            return EXIT_FAILURE;

    LOCAL_SET_EXACT(pcg_texgen_maps, monitor) = CRES_RET(pcg_texgen_monitor(&monitor_opts), return EXIT_FAILURE);
    if (!canvas_is(monitor.albedo, TEX_FMT_RGBA8, 64, 64) || monitor.normal)    return EXIT_FAILURE;

    LOCAL_SET(canvas, mglow) = CRES_RET(pcg_texgen_monitor_glow(&monitor_opts), return EXIT_FAILURE);
    if (!canvas_is(mglow, TEX_FMT_RGB8, 64, 64))            return EXIT_FAILURE;

    /*
     * The shell half is pale and dark, the screen half is the flat screen
     * colour and glows brightest in its middle, fading toward its edges
     */
    px = canvas_data(monitor.albedo);
    uchar *gx = canvas_data(mglow);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            const uchar *a = &px[(y * 64 + x) * 4], *g = &gx[(y * 64 + x) * 3];

            if (a[3] != 255)                                    return EXIT_FAILURE;
            if (x < 32 && (a[0] < 200 || g[0] || g[1] || g[2])) return EXIT_FAILURE;
            if (x >= 32 && a[0] != (uchar)(0.07f * 255.0f))     return EXIT_FAILURE;
        }
    const uchar *mid = &gx[(32 * 64 + 47) * 3], *edge = &gx[(32 * 64 + 63) * 3], *top = &gx[(0 * 64 + 47) * 3];
    if (mid[1] != (uchar)(0.55f * 255.0f) || edge[1] >= mid[1] || top[1] >= mid[1])     return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* paint twice with @seed, then once with another seed */
#define SEED_CHECK(_paint, _opts) ({                                            \
    auto __o = (_opts);                                                         \
    auto __a = _paint(&__o);                                                    \
    auto __b = _paint(&__o);                                                    \
    __o.seed++;                                                                 \
    auto __c = _paint(&__o);                                                    \
    bool __ok = !IS_CERR(__a) && !IS_CERR(__b) && !IS_CERR(__c);                \
    if (__ok)   __ok = seed_check_maps(&__a.val, &__b.val, &__c.val);           \
    pcg_texgen_maps_done(&__a.val);                                             \
    pcg_texgen_maps_done(&__b.val);                                             \
    pcg_texgen_maps_done(&__c.val);                                             \
    __ok;                                                                       \
})

static bool seed_check_maps(pcg_texgen_maps *a, pcg_texgen_maps *b, pcg_texgen_maps *c)
{
    return canvas_same(a->albedo, b->albedo) && canvas_same(a->normal, b->normal) &&
           !canvas_same(a->albedo, c->albedo) && !canvas_same(a->normal, c->normal);
}

/* paintings without relief: @a and @b the same albedo, @c another one; frees all three */
static bool seed_check_albedo(cres(pcg_texgen_maps) a, cres(pcg_texgen_maps) b, cres(pcg_texgen_maps) c)
{
    bool ok = !IS_CERR(a) && !IS_CERR(b) && !IS_CERR(c) &&
              canvas_same(a.val.albedo, b.val.albedo) && !canvas_same(a.val.albedo, c.val.albedo);

    if (!IS_CERR(a))    pcg_texgen_maps_done(&a.val);
    if (!IS_CERR(b))    pcg_texgen_maps_done(&b.val);
    if (!IS_CERR(c))    pcg_texgen_maps_done(&c.val);

    return ok;
}

static bool seed_check_canvas(cresp(canvas) a, cresp(canvas) b, cresp(canvas) c)
{
    bool ok = !IS_CERR(a) && !IS_CERR(b) && !IS_CERR(c) &&
              canvas_same(a.val, b.val) && !canvas_same(a.val, c.val);

    if (!IS_CERR(a))    canvas_free(a.val);
    if (!IS_CERR(b))    canvas_free(b.val);
    if (!IS_CERR(c))    canvas_free(c.val);

    return ok;
}

static int texgen_seed_test(void)
{
    if (!SEED_CHECK(pcg_texgen_surface, surface_opts))  return EXIT_FAILURE;
    if (!SEED_CHECK(pcg_texgen_wood, wood_opts))        return EXIT_FAILURE;
    if (!SEED_CHECK(pcg_texgen_carpet, carpet_opts))    return EXIT_FAILURE;

    pcg_texgen_cork_opts ko = cork_opts, ko2 = cork_opts;
    ko2.seed++;
    if (!seed_check_albedo(pcg_texgen_cork(&ko), pcg_texgen_cork(&ko), pcg_texgen_cork(&ko2)))  return EXIT_FAILURE;

    pcg_texgen_glow_grid_opts go = glow_opts, go2 = glow_opts;
    go2.seed++;
    if (!seed_check_canvas(pcg_texgen_glow_grid(&go), pcg_texgen_glow_grid(&go), pcg_texgen_glow_grid(&go2)))
        return EXIT_FAILURE;

    /* the shell's grain follows the seed; the glow has none to follow */
    pcg_texgen_monitor_opts mo = monitor_opts, mo2 = monitor_opts;
    mo2.seed++;
    if (!seed_check_albedo(pcg_texgen_monitor(&mo), pcg_texgen_monitor(&mo), pcg_texgen_monitor(&mo2)))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

static bool invalid_maps(cres(pcg_texgen_maps) res)
{
    if (!IS_CERR_CODE(res, CERR_INVALID_ARGUMENTS)) {
        if (!IS_CERR(res))  pcg_texgen_maps_done(&res.val);
        return false;
    }

    return true;
}

static bool invalid_canvas(cresp(canvas) res)
{
    if (!IS_CERR_CODE(res, CERR_INVALID_ARGUMENTS)) {
        if (!IS_CERR(res))  canvas_free(res.val);
        return false;
    }

    return true;
}

static int texgen_invalid_test(void)
{
    pcg_texgen_surface_opts so = surface_opts;
    so.size = 0;
    if (!invalid_maps(pcg_texgen_surface(&so)))     return EXIT_FAILURE;
    so = surface_opts;
    so.cells = 0;
    if (!invalid_maps(pcg_texgen_surface(&so)))     return EXIT_FAILURE;
    so = surface_opts;
    so.bump = -1.0f;
    if (!invalid_maps(pcg_texgen_surface(&so)))     return EXIT_FAILURE;
    if (!invalid_maps(pcg_texgen_surface(nullptr))) return EXIT_FAILURE;

    pcg_texgen_wood_opts wo = wood_opts;
    wo.size = -4;
    if (!invalid_maps(pcg_texgen_wood(&wo)))        return EXIT_FAILURE;
    wo = wood_opts;
    wo.planks = 0;
    if (!invalid_maps(pcg_texgen_wood(&wo)))        return EXIT_FAILURE;

    pcg_texgen_carpet_opts co = carpet_opts;
    co.size = 0;
    if (!invalid_maps(pcg_texgen_carpet(&co)))      return EXIT_FAILURE;
    co = carpet_opts;
    co.tiles = 0;
    if (!invalid_maps(pcg_texgen_carpet(&co)))      return EXIT_FAILURE;

    pcg_texgen_cork_opts ko = cork_opts;
    ko.w = 0;
    if (!invalid_maps(pcg_texgen_cork(&ko)))        return EXIT_FAILURE;
    ko = cork_opts;
    ko.h = 0;
    if (!invalid_maps(pcg_texgen_cork(&ko)))        return EXIT_FAILURE;
    ko = cork_opts;
    ko.papers = 0;
    if (!invalid_maps(pcg_texgen_cork(&ko)))        return EXIT_FAILURE;
    ko = cork_opts;
    ko.border_w = 0.6f;
    if (!invalid_maps(pcg_texgen_cork(&ko)))        return EXIT_FAILURE;
    ko.border_w = -0.1f;
    if (!invalid_maps(pcg_texgen_cork(&ko)))        return EXIT_FAILURE;

    pcg_texgen_glow_grid_opts go = glow_opts;
    go.size = 0;
    if (!invalid_canvas(pcg_texgen_glow_grid(&go))) return EXIT_FAILURE;
    go = glow_opts;
    go.cells_u = 0;
    if (!invalid_canvas(pcg_texgen_glow_grid(&go))) return EXIT_FAILURE;
    go = glow_opts;
    go.cells_v = 0;
    if (!invalid_canvas(pcg_texgen_glow_grid(&go))) return EXIT_FAILURE;

    if (!invalid_canvas(pcg_texgen_flat(&(pcg_texgen_flat_opts){ .size = 0 }))) return EXIT_FAILURE;

    if (!invalid_maps(pcg_texgen_monitor(nullptr)))         return EXIT_FAILURE;
    if (!invalid_canvas(pcg_texgen_monitor_glow(nullptr)))  return EXIT_FAILURE;
    pcg_texgen_monitor_opts bad[] = { monitor_opts, monitor_opts, monitor_opts, monitor_opts, monitor_opts };
    bad[0].size = 4;
    bad[1].grain = -0.1f;
    bad[2].grain = NAN;
    bad[3].ramp = 0.0f;
    bad[4].ramp = INFINITY;
    for (size_t i = 0; i < array_size(bad); i++) {
        if (!invalid_maps(pcg_texgen_monitor(&bad[i])))         return EXIT_FAILURE;
        if (!invalid_canvas(pcg_texgen_monitor_glow(&bad[i])))  return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
 * Boards whose papers don't fit their interior: wide and flat, tiny, or
 * thin and tall. Every texel in the border keeps the border tone (red at
 * most 0.30 * 1.15 of full scale; the papers and pins are far brighter),
 * so nothing is painted outside the interior, let alone the canvas.
 */
static int texgen_cork_small_test(void)
{
    static const int sizes[][2] = {
        { 16, 12 }, { 33, 17 }, { 64, 8 }, { 256, 12 }, { 8, 64 }, { 2, 2 }, { 1, 1 },
    };

    for (size_t i = 0; i < array_size(sizes); i++)
        for (uint32_t seed = 0; seed < 16; seed++) {
            pcg_texgen_cork_opts ko = cork_opts;

            ko.w = sizes[i][0];
            ko.h = sizes[i][1];
            ko.border_w = 0.25f;
            ko.seed = seed;

            LOCAL_SET_EXACT(pcg_texgen_maps, m) = CRES_RET(pcg_texgen_cork(&ko), return EXIT_FAILURE);
            canvas *c = m.albedo;
            if (!canvas_is(c, TEX_FMT_RGBA8, ko.w, ko.h))   return EXIT_FAILURE;

            int bw = (int)(ko.border_w * min(ko.w, ko.h));
            uchar *px = canvas_data(c);

            for (int y = 0; y < ko.h; y++)
                for (int x = 0; x < ko.w; x++) {
                    bool border = x < bw || x >= ko.w - bw || y < bw || y >= ko.h - bw;

                    if (border && px[(y * ko.w + x) * 4] > (uchar)(0.30f * 1.15f * 255.0f) + 1) return EXIT_FAILURE;
                }
        }

    return EXIT_SUCCESS;
}

/* upload @c: the texture is asked for in the canvas's format, with @wrap and @filter both ways */
static bool upload_is(renderer_t *r, canvas *c, texture_wrap wrap, texture_filter filter)
{
    LOCAL_SET(texture_t, tex) = CRES_RET(pcg_texgen_upload(r, c, wrap, filter), return false);
    if (!tex)   return false;

    const texture_init_options *o = stub_texture_opts(tex);

    return o->renderer == r && o->format == c->fmt && o->wrap == wrap && o->min_filter == filter &&
           o->mag_filter == filter;
}

static int texgen_upload_test(void)
{
    const pcg_texgen_flat_opts fo = { .size = 4, .color = { 1.0f, 1.0f, 1.0f } };
    LOCAL_SET(canvas, c) = CRES_RET(pcg_texgen_flat(&fo), return EXIT_FAILURE);

    auto bad = pcg_texgen_upload(nullptr, c, TEX_WRAP_REPEAT, TEX_FLT_LINEAR);
    if (!IS_CERR_CODE(bad, CERR_INVALID_ARGUMENTS))     return EXIT_FAILURE;

    /* any non-null renderer: its texture side is stubbed out */
    renderer_t *r = (renderer_t *)c;
    bad = pcg_texgen_upload(r, nullptr, TEX_WRAP_REPEAT, TEX_FLT_LINEAR);
    if (!IS_CERR_CODE(bad, CERR_INVALID_ARGUMENTS))     return EXIT_FAILURE;

    /* an RGB8 emission map and an RGBA8 albedo, each with its own wrap and filter */
    if (!upload_is(r, c, TEX_WRAP_REPEAT, TEX_FLT_LINEAR))              return EXIT_FAILURE;

    LOCAL_SET_EXACT(pcg_texgen_maps, km) = CRES_RET(pcg_texgen_cork(&cork_opts), return EXIT_FAILURE);
    canvas *kc = km.albedo;
    if (kc->fmt == c->fmt || !upload_is(r, kc, TEX_CLAMP_TO_EDGE, TEX_FLT_NEAREST)) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/* widen [lo, hi] per channel to take in @c */
static void range_add(vec3 lo, vec3 hi, const float *c)
{
    for (int i = 0; i < 3; i++) {
        lo[i] = min(lo[i], c[i]);
        hi[i] = max(hi[i], c[i]);
    }
}

/* the span of the colours a recipe's generator mixes */
static void recipe_colours(const pcg_recipe *r, vec3 lo, vec3 hi)
{
    static const vec3 papers[] = { { 0.92f, 0.90f, 0.85f }, { 0.55f, 0.56f, 0.58f }, { 0.90f, 0.84f, 0.58f } };

    lo[0] = lo[1] = lo[2] = 1.0f;
    hi[0] = hi[1] = hi[2] = 0.0f;

    switch (r->gen) {
        case PCG_TEXGEN_SURFACE:
            range_add(lo, hi, r->surface.base);
            break;
        case PCG_TEXGEN_WOOD:
            range_add(lo, hi, r->wood.light);
            range_add(lo, hi, r->wood.dark);
            break;
        case PCG_TEXGEN_CARPET:
            range_add(lo, hi, r->carpet.base);
            range_add(lo, hi, r->carpet.accent);
            break;
        case PCG_TEXGEN_MONITOR:
            range_add(lo, hi, r->monitor.shell);
            range_add(lo, hi, r->monitor.screen);
            break;
        case PCG_TEXGEN_CORK:
            range_add(lo, hi, r->cork.base);
            range_add(lo, hi, r->cork.border);
            for (size_t i = 0; i < array_size(papers); i++) range_add(lo, hi, papers[i]);
            break;
        default:
            break;
    }
}

/*
 * Every preset's recipe paints: the albedo's channel means fall within a
 * quarter beyond the colours its generator mixes (the lightness noise
 * centers on 1), and the emission map, if any, is an RGB8 canvas.
 */
static int preset_paint_test(void)
{
    for (int p = PCG_PRESET_NONE + 1; p < PCG_PRESET_NR; p++) {
        pcg_recipe r;

        if (failed(pcg_recipe_init(&r, p)))     return EXIT_FAILURE;
        if (!r.name || r.gen >= PCG_TEXGEN_NR)  return EXIT_FAILURE;

        LOCAL_SET_EXACT(pcg_texgen_maps, maps) = CRES_RET(pcg_recipe_paint(&r, 0xa781), return EXIT_FAILURE);
        canvas *alb = maps.albedo;
        if (!alb || alb->fmt != TEX_FMT_RGBA8)  return EXIT_FAILURE;

        vec3 lo, hi, mean = {};
        size_t nr = alb->width * alb->height;
        uchar *px = canvas_data(alb);

        recipe_colours(&r, lo, hi);
        for (size_t t = 0; t < nr; t++)
            for (int i = 0; i < 3; i++)                                 mean[i] += px[t * 4 + i] / 255.0f / nr;
        for (int i = 0; i < 3; i++)
            if (mean[i] < lo[i] * 0.75f || mean[i] > hi[i] * 1.25f)     return EXIT_FAILURE;

        if (r.glow == PCG_GLOW_NONE) {
            auto none = pcg_recipe_paint_glow(&r, 0);
            if (!IS_CERR_CODE(none, CERR_INVALID_ARGUMENTS))    return EXIT_FAILURE;
            continue;
        }

        LOCAL_SET(canvas, glow) = CRES_RET(pcg_recipe_paint_glow(&r, 0xa781), return EXIT_FAILURE);
        if (glow->fmt != TEX_FMT_RGB8)  return EXIT_FAILURE;
    }

    pcg_recipe r;
    if (!invalid(pcg_recipe_init(&r, PCG_PRESET_NONE))) return EXIT_FAILURE;
    if (!invalid(pcg_recipe_init(&r, PCG_PRESET_NR)))   return EXIT_FAILURE;

    auto base = pcg_preset_base(PCG_PRESET_NONE);
    if (!IS_CERR_CODE(base, CERR_INVALID_ARGUMENTS))        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

/*
 * A preset with a base preset wears the base's textures in a material
 * library, so its own recipe must paint the very same albedo, and the
 * same normal map if it bakes one.
 */
static int preset_base_test(void)
{
    for (int p = PCG_PRESET_NONE + 1; p < PCG_PRESET_NR; p++) {
        int base = CRES_RET(pcg_preset_base(p), return EXIT_FAILURE);
        if (base == p)          continue;

        pcg_recipe r, b;
        if (failed(pcg_recipe_init(&r, p)) || failed(pcg_recipe_init(&b, base)))    return EXIT_FAILURE;

        /* bases paint their own */
        if (CRES_RET(pcg_preset_base(base), return EXIT_FAILURE) != base)   return EXIT_FAILURE;

        LOCAL_SET_EXACT(pcg_texgen_maps, rm) = CRES_RET(pcg_recipe_paint(&r, 0x1234), return EXIT_FAILURE);
        LOCAL_SET_EXACT(pcg_texgen_maps, bm) = CRES_RET(pcg_recipe_paint(&b, 0x1234), return EXIT_FAILURE);

        if (!canvas_same(rm.albedo, bm.albedo))                 return EXIT_FAILURE;
        if (r.wrap != b.wrap || r.filter != b.filter)           return EXIT_FAILURE;
        if (rm.normal && !canvas_same(rm.normal, bm.normal))    return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
 * The presets that cast no shadow: glass, and the light fittings and sign
 * plates, small and flat against a ceiling or a wall. Every other preset
 * casts one.
 */
static int preset_shadow_test(void)
{
    for (int p = PCG_PRESET_NONE + 1; p < PCG_PRESET_NR; p++) {
        bool none = p == PCG_PRESET_GLASS || p == PCG_PRESET_FIXTURE_GLOW || p == PCG_PRESET_SIGN_PLATE;
        pcg_recipe r;

        if (failed(pcg_recipe_init(&r, p)))     return EXIT_FAILURE;
        if (r.skip_shadow != none)              return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/* every preset letters its labels on a linearly filtered atlas, legible at an angle */
static int preset_label_filter_test(void)
{
    for (int p = PCG_PRESET_NONE + 1; p < PCG_PRESET_NR; p++) {
        pcg_recipe r;

        if (failed(pcg_recipe_init(&r, p)))         return EXIT_FAILURE;
        if (r.label.filter != TEX_FLT_LINEAR)       return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static const struct test tests[] = {
    { .name = "texgen formats", .test = texgen_formats_test },
    { .name = "texgen seed", .test = texgen_seed_test },
    { .name = "texgen invalid", .test = texgen_invalid_test },
    { .name = "texgen cork small", .test = texgen_cork_small_test },
    { .name = "texgen upload", .test = texgen_upload_test },
    { .name = "preset paint", .test = preset_paint_test },
    { .name = "preset base", .test = preset_base_test },
    { .name = "preset shadow", .test = preset_shadow_test },
    { .name = "preset label filter", .test = preset_label_filter_test },
};

int main(int argc, char **argv, char **envp)
{
    return test_main(argc, argv, envp, tests, array_size(tests));
}
