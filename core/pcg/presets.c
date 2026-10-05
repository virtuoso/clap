// SPDX-License-Identifier: Apache-2.0
#include "pcg/material.h"

/**
 * struct pcg_preset_row - a preset
 * @base:       the preset whose textures this one wears; itself when it
 *              paints its own
 * @no_relief:  wears the base's albedo without its normal map
 * @recipe:     the recipe; with another @base, its generator, wrap and
 *              filter are the base's and only the rest is read here
 */
struct pcg_preset_row {
    pcg_recipe  recipe;
    pcg_preset  base;
    bool        no_relief;
};

/* glowing materials keep their emission texels sharp */
#define PCG_GLOW_FILTER TEX_FLT_NEAREST

static const struct pcg_preset_row pcg_presets[PCG_PRESET_NR] = {
    [PCG_PRESET_CONCRETE] = {
        .base   = PCG_PRESET_CONCRETE,
        .recipe = {
            .name       = "concrete",
            .gen        = PCG_TEXGEN_SURFACE,
            .surface    = {
                .size = 256, .base = { 0.84f, 0.81f, 0.76f }, .blotch = 0.08f, .speckle = 0.05f,
                .cells = 6, .alpha = 1.0f, .bump = 2.0f, .seed = 0xc0,
            },
            .shade      = { .uv_factor = 1.0f / 2.4f, .roughness = 0.85f },
            .wrap       = TEX_WRAP_REPEAT,
            .filter     = TEX_FLT_LINEAR,
            .uv_mode    = PCG_UV_METERS,
            .cull_face  = true,
        },
    },
    [PCG_PRESET_WALNUT] = {
        .base   = PCG_PRESET_WALNUT,
        .recipe = {
            .name       = "walnut",
            .gen        = PCG_TEXGEN_WOOD,
            .wood       = {
                .size = 256, .planks = 14,
                .light = { 0.46f, 0.28f, 0.16f }, .dark = { 0.24f, 0.13f, 0.08f },
                .grain = 0.22f, .seam = 0.10f, .seam_floor = 0.55f, .bump = 2.5f, .seed = 0x51,
            },
            .shade      = { .uv_factor = 1.0f / 1.4f, .roughness = 0.6f },
            .wrap       = TEX_WRAP_REPEAT,
            .filter     = TEX_FLT_LINEAR,
            .uv_mode    = PCG_UV_METERS,
            .cull_face  = true,
        },
    },
    [PCG_PRESET_OAK_FLOOR] = {
        .base   = PCG_PRESET_OAK_FLOOR,
        .recipe = {
            .name       = "oak-floor",
            .gen        = PCG_TEXGEN_WOOD,
            .wood       = {
                .size = 256, .planks = 10, .along_u = true,
                .light = { 0.64f, 0.47f, 0.30f }, .dark = { 0.50f, 0.35f, 0.21f },
                .grain = 0.16f, .seam = 0.05f, .seam_floor = 0.55f, .bump = 4.0f, .seed = 0xf1,
            },
            .shade      = { .uv_factor = 1.0f / 2.0f, .roughness = 0.55f, .metallic = 0.05f },
            .wrap       = TEX_WRAP_REPEAT,
            .filter     = TEX_FLT_LINEAR,
            .uv_mode    = PCG_UV_METERS,
            .cull_face  = true,
        },
    },
    [PCG_PRESET_WOOD_PANEL] = {
        .base   = PCG_PRESET_WOOD_PANEL,
        .recipe = {
            .name       = "wood-panel",
            .gen        = PCG_TEXGEN_WOOD,
            .wood       = {
                .size = 256, .planks = 8,
                .light = { 0.52f, 0.34f, 0.20f }, .dark = { 0.38f, 0.23f, 0.13f },
                .grain = 0.18f, .seam = 0.07f, .seam_floor = 0.55f, .bump = 2.0f, .seed = 0x9a,
            },
            .shade      = { .uv_factor = 1.0f / 2.6f, .roughness = 0.65f },
            .wrap       = TEX_WRAP_REPEAT,
            .filter     = TEX_FLT_LINEAR,
            .uv_mode    = PCG_UV_METERS,
            .cull_face  = true,
        },
    },
    [PCG_PRESET_BRONZE] = {
        .base   = PCG_PRESET_BRONZE,
        .recipe = {
            .name       = "bronze",
            .gen        = PCG_TEXGEN_SURFACE,
            .surface    = {
                .size = 128, .base = { 0.18f, 0.14f, 0.10f }, .blotch = 0.10f, .speckle = 0.04f,
                .cells = 5, .alpha = 1.0f, .bump = 1.2f, .seed = 0x3e,
            },
            .shade      = { .uv_factor = 1.0f / 1.2f, .roughness = 0.35f, .metallic = 0.85f },
            .wrap       = TEX_WRAP_REPEAT,
            .filter     = TEX_FLT_LINEAR,
            .uv_mode    = PCG_UV_METERS,
            .cull_face  = true,
        },
    },
    [PCG_PRESET_GLASS] = {
        .base   = PCG_PRESET_GLASS,
        .recipe = {
            .name       = "glass",
            .gen        = PCG_TEXGEN_SURFACE,
            .surface    = {
                .size = 64, .base = { 0.62f, 0.78f, 0.82f }, .blotch = 0.06f, .speckle = 0.02f,
                .cells = 3, .alpha = 0.35f, .bump = 0.0f, .seed = 0x6a,
            },
            /* the bronze density: glass shares its members' tiling */
            .shade      = { .uv_factor = 1.0f / 1.2f, .roughness = 0.15f, .metallic = 0.1f },
            .wrap       = TEX_WRAP_REPEAT,
            .filter     = TEX_FLT_LINEAR,
            .uv_mode    = PCG_UV_METERS,
            .alpha_blend = true,
            .skip_shadow = true,
            .cull_face  = true,
        },
    },
    [PCG_PRESET_CARPET_TILE] = {
        .base   = PCG_PRESET_CARPET_TILE,
        .recipe = {
            .name       = "carpet-tile",
            .gen        = PCG_TEXGEN_CARPET,
            .carpet     = {
                .size = 256, .tiles = 8,
                .base = { 0.10f, 0.16f, 0.28f }, .accent = { 0.16f, 0.24f, 0.38f },
                .grain = 0.16f, .fuzz = 0.05f, .seam = 0.05f, .seam_floor = 0.78f, .bump = 1.5f, .seed = 0x2b,
            },
            /* 8 half-meter tiles per repeat */
            .shade      = { .uv_factor = 1.0f / 4.0f, .roughness = 0.9f },
            .wrap       = TEX_WRAP_REPEAT,
            .filter     = TEX_FLT_LINEAR,
            .uv_mode    = PCG_UV_METERS,
            .cull_face  = true,
        },
    },
    [PCG_PRESET_FIXTURE_GLOW] = {
        .base       = PCG_PRESET_BRONZE,
        .no_relief  = true,
        .recipe     = {
            .name       = "fixture-glow",
            .glow       = PCG_GLOW_FLAT,
            .flat       = { .size = 8, .color = { 1.0f, 0.55f, 0.22f } },
            /* small fittings wear the whole tile on every face */
            .shade      = { .uv_factor = 1.0f, .roughness = 0.5f },
            .glow_filter = PCG_GLOW_FILTER,
            .uv_mode    = PCG_UV_UNIT,
            .skip_shadow = true,
            .cull_face  = true,
        },
    },
    [PCG_PRESET_DIFFUSER] = {
        .base       = PCG_PRESET_BRONZE,
        .no_relief  = true,
        .recipe     = {
            .name       = "diffuser",
            .glow       = PCG_GLOW_GRID,
            .grid       = {
                .size = 128, .cells_u = 1, .cells_v = 1, .glow = { 1.0f, 0.9f, 0.72f },
                .rim = 0.06f, .lit = 0.8f, .lit_var = 0.2f, .unlit = 0.04f, .seed = 0x51,
            },
            /* one coffer per 1.2 m beam pitch */
            .shade      = { .uv_factor = 1.0f / 1.2f, .roughness = 0.9f },
            .glow_filter = PCG_GLOW_FILTER,
            .uv_mode    = PCG_UV_METERS,
            .cull_face  = true,
        },
    },
    [PCG_PRESET_SIGN_PLATE] = {
        .base   = PCG_PRESET_SIGN_PLATE,
        .recipe = {
            .name       = "sign-plate",
            .gen        = PCG_TEXGEN_SURFACE,
            /* a blank plate, for a plate without a label */
            .surface    = { .size = 4, .base = { 0.15f, 0.12f, 0.09f }, .cells = 1, .alpha = 1.0f },
            .shade      = { .uv_factor = 1.0f, .roughness = 0.55f },
            .label      = {
                .plate  = { 0.15f, 0.12f, 0.09f, 1.0f },
                .ink    = { 0.92f, 0.88f, 0.78f, 1.0f },
                .filter = TEX_FLT_LINEAR,
            },
            .wrap       = TEX_CLAMP_TO_EDGE,
            .filter     = TEX_FLT_LINEAR,
            .uv_mode    = PCG_UV_UNIT,
            .skip_shadow = true,
            .cull_face  = true,
        },
    },
    [PCG_PRESET_PLASTIC_SHELL] = {
        .base   = PCG_PRESET_PLASTIC_SHELL,
        .recipe = {
            .name       = "plastic-shell",
            .gen        = PCG_TEXGEN_SURFACE,
            .surface    = {
                .size = 128, .base = { 0.78f, 0.36f, 0.10f }, .blotch = 0.05f, .speckle = 0.03f,
                .cells = 4, .alpha = 1.0f, .bump = 0.0f, .seed = 0xde,
            },
            .shade      = { .uv_factor = 1.0f, .roughness = 0.6f },
            .wrap       = TEX_WRAP_REPEAT,
            .filter     = TEX_FLT_LINEAR,
            .uv_mode    = PCG_UV_METERS,
            .cull_face  = true,
        },
    },
    [PCG_PRESET_CORKBOARD] = {
        .base   = PCG_PRESET_CORKBOARD,
        .recipe = {
            .name       = "corkboard",
            .gen        = PCG_TEXGEN_CORK,
            .cork       = {
                .w = 256, .h = 192, .base = { 0.82f, 0.76f, 0.66f }, .border = { 0.30f, 0.22f, 0.15f },
                .border_w = 0.035f, .paper_w = 0.10f, .paper_w_var = 0.08f, .papers = 15, .seed = 0xb0,
            },
            .shade      = { .uv_factor = 1.0f, .roughness = 0.9f },
            .wrap       = TEX_CLAMP_TO_EDGE,
            .filter     = TEX_FLT_LINEAR,
            .uv_mode    = PCG_UV_UNIT,
            .cull_face  = true,
        },
    },
};

cres(int) pcg_preset_base(pcg_preset preset)
{
    if (preset <= PCG_PRESET_NONE || preset >= PCG_PRESET_NR)
        return cres_error(int, CERR_INVALID_ARGUMENTS, .fmt = "unknown preset");

    return cres_val(int, pcg_presets[preset].base);
}

cerr pcg_recipe_init(pcg_recipe *r, pcg_preset preset)
{
    if (!r)     return CERR_INVALID_ARGUMENTS;

    pcg_preset base = CRES_RET_CERR(pcg_preset_base(preset));
    const struct pcg_preset_row *row = &pcg_presets[preset];
    const pcg_recipe *own = &row->recipe;

    *r = *own;
    if (base == preset) return CERR_OK;

    /* the base's albedo: its generator, wrap and filter */
    const pcg_recipe *b = &pcg_presets[base].recipe;

    r->gen = b->gen;
    switch (b->gen) {
        case PCG_TEXGEN_SURFACE:    r->surface = b->surface; break;
        case PCG_TEXGEN_WOOD:       r->wood = b->wood; break;
        case PCG_TEXGEN_CARPET:     r->carpet = b->carpet; break;
        case PCG_TEXGEN_CORK:       r->cork = b->cork; break;
        default:                    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "bad base preset");
    }
    r->wrap = b->wrap;
    r->filter = b->filter;

    if (!row->no_relief)    return CERR_OK;

    switch (r->gen) {
        case PCG_TEXGEN_SURFACE:    r->surface.bump = 0.0f; break;
        case PCG_TEXGEN_WOOD:       r->wood.bump = 0.0f; break;
        case PCG_TEXGEN_CARPET:     r->carpet.bump = 0.0f; break;
        default:                    break;
    }

    return CERR_OK;
}

cres(pcg_texgen_maps) pcg_recipe_paint(const pcg_recipe *r, uint32_t seed)
{
    if (!r)     return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS);

    switch (r->gen) {
        case PCG_TEXGEN_SURFACE: {
            pcg_texgen_surface_opts o = r->surface;
            o.seed ^= seed;
            return pcg_texgen_surface(&o);
        }
        case PCG_TEXGEN_WOOD: {
            pcg_texgen_wood_opts o = r->wood;
            o.seed ^= seed;
            return pcg_texgen_wood(&o);
        }
        case PCG_TEXGEN_CARPET: {
            pcg_texgen_carpet_opts o = r->carpet;
            o.seed ^= seed;
            return pcg_texgen_carpet(&o);
        }
        case PCG_TEXGEN_CORK: {
            pcg_texgen_cork_opts o = r->cork;
            o.seed ^= seed;
            return pcg_texgen_cork(&o);
        }
        default:
            break;
    }

    return cres_error(pcg_texgen_maps, CERR_INVALID_ARGUMENTS, .fmt = "unknown texture generator");
}

cresp(canvas) pcg_recipe_paint_glow(const pcg_recipe *r, uint32_t seed)
{
    if (!r)     return cresp_error(canvas, CERR_INVALID_ARGUMENTS);

    switch (r->glow) {
        case PCG_GLOW_FLAT:
            return pcg_texgen_flat(&r->flat);
        case PCG_GLOW_GRID: {
            pcg_texgen_glow_grid_opts o = r->grid;
            o.seed ^= seed;
            return pcg_texgen_glow_grid(&o);
        }
        default:
            break;
    }

    return cresp_error(canvas, CERR_INVALID_ARGUMENTS, .fmt = "no emission generator");
}
