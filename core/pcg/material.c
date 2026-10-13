// SPDX-License-Identifier: Apache-2.0
#include "memory.h"
#include "pcg/material.h"

/**
 * struct pcg_matlib - a material library
 * @renderer:   what the textures are made for
 * @seed:       mixed into every recipe's generator seeds
 * @textures:   every texture handed out, one reference each
 * @mats:       every material handed out, each allocated on its own, so
 *              it never moves
 * @names:      the materials' names
 * @preset:     the presets made so far, by &enum pcg_preset
 */
struct pcg_matlib {
    renderer_t                  *renderer;
    darray(texture_t *,         textures);
    darray(pcg_material *,      mats);
    darray(char *,              names);
    pcg_material                *preset[PCG_PRESET_NR];
    uint32_t                    seed;
};

DEFINE_CLEANUP(pcg_matlib, if (*p) pcg_matlib_free(*p))
static DEFINE_CLEANUP(pcg_material, mem_free(*p))

cresp(pcg_matlib) pcg_matlib_new(renderer_t *r, uint32_t seed)
{
    if (!r)     return cresp_error(pcg_matlib, CERR_INVALID_ARGUMENTS);

    pcg_matlib *lib = mem_alloc(sizeof(*lib), .zero = 1);
    if (!lib)   return cresp_error(pcg_matlib, CERR_NOMEM);

    lib->renderer = r;
    lib->seed = seed;
    darray_init(lib->textures);
    darray_init(lib->mats);
    darray_init(lib->names);

    return cresp_val(pcg_matlib, lib);
}

void pcg_matlib_free(pcg_matlib *lib)
{
    if (!lib)   return;

    texture_t **tex;
    darray_for_each(tex, lib->textures)
        texture_done(*tex);
    darray_clearout(lib->textures);

    pcg_material **mat;
    darray_for_each(mat, lib->mats)
        mem_free(*mat);
    darray_clearout(lib->mats);

    char **name;
    darray_for_each(name, lib->names)
        mem_free(*name);
    darray_clearout(lib->names);

    mem_free(lib);
}

cresp(texture_t) pcg_matlib_upload(pcg_matlib *lib, canvas *c, texture_wrap wrap, texture_filter filter)
{
    if (!lib || !c)     return cresp_error(texture_t, CERR_INVALID_ARGUMENTS);

    /* a failed upload leaves the slot empty, which texture_done() skips */
    texture_t **slot = darray_add(lib->textures);
    if (!slot)          return cresp_error(texture_t, CERR_NOMEM);

    *slot = nullptr;
    *slot = CRES_RET_T(pcg_texgen_upload(lib->renderer, c, wrap, filter), texture_t);

    return cresp_val(texture_t, *slot);
}

/* hand a new material over to the library, named @name */
static cresp(pcg_material) pcg_matlib_adopt(pcg_matlib *lib, pcg_material **pmat, const char *name)
{
    LOCAL(char, copy);
    CRES_RET_T(mem_asprintf(&copy, "%s", name), pcg_material);

    char **name_slot = darray_add(lib->names);
    if (!name_slot)     return cresp_error(pcg_material, CERR_NOMEM);
    *name_slot = NOCU(copy);

    pcg_material **mat_slot = darray_add(lib->mats);
    if (!mat_slot)      return cresp_error(pcg_material, CERR_NOMEM);
    *mat_slot = NOCU(*pmat);

    (*mat_slot)->name = *name_slot;

    return cresp_val(pcg_material, *mat_slot);
}

/* the parts of a material that a recipe sets, short of its textures */
static void pcg_material_shade_from(pcg_material *mat, const pcg_recipe *r)
{
    mat->shade          = r->shade;
    mat->label          = r->label;
    mat->bloom          = r->bloom;
    mat->uv_mode        = r->uv_mode;
    mat->alpha_blend    = r->alpha_blend;
    mat->skip_shadow    = r->skip_shadow;
    mat->cull_face      = r->cull_face;
}

/* the recipe bakes a normal map; cork and monitor atlases never do */
static bool pcg_recipe_has_relief(const pcg_recipe *r)
{
    switch (r->gen) {
        case PCG_TEXGEN_SURFACE:    return r->surface.bump > 0.0f;
        case PCG_TEXGEN_WOOD:       return r->wood.bump > 0.0f;
        case PCG_TEXGEN_CARPET:     return r->carpet.bump > 0.0f;
        default:                    return false;
    }
}

static cerr pcg_recipe_check(const pcg_recipe *r)
{
    if (!r)                         return CERR_INVALID_ARGUMENTS;
    if (!r->name || !*r->name)      return CERR_INVALID_ARGUMENTS_REASON(.fmt = "recipe without a name");
    if (r->uv_mode >= PCG_UV_NR || r->glow >= PCG_GLOW_NR)
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "recipe '%s': bad UV mode or emission generator", .arg0 = r->name);
    if (r->glow == PCG_GLOW_MONITOR && r->gen != PCG_TEXGEN_MONITOR)
        return CERR_INVALID_ARGUMENTS_REASON(
            .fmt    = "recipe '%s': monitor glow without a monitor atlas",
            .arg0   = r->name
        );
    if (r->uv_mode == PCG_UV_UNIT && r->shade.uv_factor != 1.0f)
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "recipe '%s': unit UVs need a UV factor of 1", .arg0 = r->name);
    if (!(r->shade.uv_factor > 0.0f))
        return CERR_INVALID_ARGUMENTS_REASON(.fmt = "recipe '%s': UV factor isn't positive", .arg0 = r->name);

    return CERR_OK;
}

/* the recipe's emission map on @mat, if it has one */
static cerr pcg_matlib_glow(pcg_matlib *lib, pcg_material *mat, const pcg_recipe *r)
{
    if (r->glow == PCG_GLOW_NONE)   return CERR_OK;

    LOCAL_SET(canvas, glow) = CRES_RET_CERR(pcg_recipe_paint_glow(r, lib->seed));
    mat->emission = CRES_RET_CERR(pcg_matlib_upload(lib, glow, r->wrap, r->glow_filter));

    return CERR_OK;
}

cresp(pcg_material) pcg_matlib_add(pcg_matlib *lib, const pcg_recipe *r)
{
    if (!lib)   return cresp_error(pcg_material, CERR_INVALID_ARGUMENTS);
    CERR_RET_T(pcg_recipe_check(r), pcg_material);

    LOCAL_SET_EXACT(pcg_texgen_maps, maps) = CRES_RET_T(pcg_recipe_paint(r, lib->seed), pcg_material);
    LOCAL_SET(pcg_material, mat) = mem_alloc(sizeof(*mat), .zero = 1);
    if (!mat)   return cresp_error(pcg_material, CERR_NOMEM);

    pcg_material_shade_from(mat, r);

    /* uploaded textures are the library's from here on, whatever fails next */
    mat->albedo = CRES_RET_T(pcg_matlib_upload(lib, maps.albedo, r->wrap, r->filter), pcg_material);
    if (maps.normal)    mat->normal = CRES_RET_T(pcg_matlib_upload(lib, maps.normal, r->wrap, r->filter), pcg_material);
    CERR_RET_T(pcg_matlib_glow(lib, mat, r), pcg_material);

    return pcg_matlib_adopt(lib, &mat, r->name);
}

static bool pcg_matlib_owns(pcg_matlib *lib, const pcg_material *mat)
{
    pcg_material **it;

    darray_for_each(it, lib->mats)
        if (*it == mat) return true;

    return false;
}

cresp(pcg_material) pcg_matlib_derive(pcg_matlib *lib, const pcg_material *base, const char *name)
{
    if (!lib || !base || !name || !*name)   return cresp_error(pcg_material, CERR_INVALID_ARGUMENTS);
    if (!pcg_matlib_owns(lib, base))
        return cresp_error(pcg_material, CERR_INVALID_ARGUMENTS, .fmt = "material '%s' is from another library",
                           .arg0 = base->name);

    LOCAL_SET(pcg_material, mat) = mem_alloc(sizeof(*mat));
    if (!mat)   return cresp_error(pcg_material, CERR_NOMEM);

    *mat = *base;

    return pcg_matlib_adopt(lib, &mat, name);
}

cresp(pcg_material) pcg_matlib_preset(pcg_matlib *lib, pcg_preset preset)
{
    if (!lib)   return cresp_error(pcg_material, CERR_INVALID_ARGUMENTS);

    pcg_preset base = CRES_RET_T(pcg_preset_base(preset), pcg_material);
    if (lib->preset[preset])    return cresp_val(pcg_material, lib->preset[preset]);

    pcg_recipe r;
    CERR_RET_T(pcg_recipe_init(&r, preset), pcg_material);

    pcg_material *mat;
    if (base == preset) {
        mat = CRES_RET_T(pcg_matlib_add(lib, &r), pcg_material);
    } else {
        CERR_RET_T(pcg_recipe_check(&r), pcg_material);

        /* the base's textures, this preset's shading and emission */
        pcg_material *b = CRES_RET_T(pcg_matlib_preset(lib, base), pcg_material);
        mat = CRES_RET_T(pcg_matlib_derive(lib, b, r.name), pcg_material);

        pcg_material_shade_from(mat, &r);
        mat->emission = nullptr;
        if (!pcg_recipe_has_relief(&r)) mat->normal = nullptr;
        CERR_RET_T(pcg_matlib_glow(lib, mat, &r), pcg_material);
    }

    lib->preset[preset] = mat;

    return cresp_val(pcg_material, mat);
}

/* every scheme's role -> preset map; PCG_PRESET_NONE leaves the role empty */
static const pcg_preset pcg_schemes[PCG_SCHEME_NR][PCG_ROLE_NR] = {
    [PCG_SCHEME_CONCRETE_WOOD] = {
        [PCG_ROLE_STRUCTURE]    = PCG_PRESET_CONCRETE,
        [PCG_ROLE_FLOOR]        = PCG_PRESET_OAK_FLOOR,
        [PCG_ROLE_CLADDING]     = PCG_PRESET_WOOD_PANEL,
        [PCG_ROLE_RAIL]         = PCG_PRESET_WALNUT,
        [PCG_ROLE_METAL]        = PCG_PRESET_BRONZE,
        [PCG_ROLE_GLASS]        = PCG_PRESET_GLASS,
        [PCG_ROLE_CARPET]       = PCG_PRESET_CARPET_TILE,
        [PCG_ROLE_FIXTURE]      = PCG_PRESET_FIXTURE_GLOW,
        [PCG_ROLE_DIFFUSER]     = PCG_PRESET_DIFFUSER,
        [PCG_ROLE_SIGN]         = PCG_PRESET_SIGN_PLATE,
        [PCG_ROLE_FURNITURE]    = PCG_PRESET_PLASTIC_SHELL,
        [PCG_ROLE_SCREEN]       = PCG_PRESET_MONITOR,
        [PCG_ROLE_BOARD]        = PCG_PRESET_CORKBOARD,
    },
    [PCG_SCHEME_CONCRETE] = {
        [PCG_ROLE_STRUCTURE]    = PCG_PRESET_CONCRETE,
        [PCG_ROLE_FLOOR]        = PCG_PRESET_CONCRETE,
        [PCG_ROLE_CLADDING]     = PCG_PRESET_CONCRETE,
        [PCG_ROLE_RAIL]         = PCG_PRESET_CONCRETE,
        [PCG_ROLE_METAL]        = PCG_PRESET_BRONZE,
        [PCG_ROLE_GLASS]        = PCG_PRESET_GLASS,
        [PCG_ROLE_CARPET]       = PCG_PRESET_CARPET_TILE,
        [PCG_ROLE_FIXTURE]      = PCG_PRESET_FIXTURE_GLOW,
        [PCG_ROLE_DIFFUSER]     = PCG_PRESET_DIFFUSER,
        [PCG_ROLE_SIGN]         = PCG_PRESET_SIGN_PLATE,
        [PCG_ROLE_FURNITURE]    = PCG_PRESET_PLASTIC_SHELL,
        [PCG_ROLE_SCREEN]       = PCG_PRESET_MONITOR,
        [PCG_ROLE_BOARD]        = PCG_PRESET_CORKBOARD,
    },
    [PCG_SCHEME_WOOD] = {
        [PCG_ROLE_STRUCTURE]    = PCG_PRESET_WOOD_PANEL,
        [PCG_ROLE_FLOOR]        = PCG_PRESET_OAK_FLOOR,
        [PCG_ROLE_CLADDING]     = PCG_PRESET_WOOD_PANEL,
        [PCG_ROLE_RAIL]         = PCG_PRESET_WALNUT,
        [PCG_ROLE_METAL]        = PCG_PRESET_BRONZE,
        [PCG_ROLE_GLASS]        = PCG_PRESET_GLASS,
        [PCG_ROLE_CARPET]       = PCG_PRESET_CARPET_TILE,
        [PCG_ROLE_FIXTURE]      = PCG_PRESET_FIXTURE_GLOW,
        [PCG_ROLE_DIFFUSER]     = PCG_PRESET_DIFFUSER,
        [PCG_ROLE_SIGN]         = PCG_PRESET_SIGN_PLATE,
        [PCG_ROLE_FURNITURE]    = PCG_PRESET_PLASTIC_SHELL,
        [PCG_ROLE_SCREEN]       = PCG_PRESET_MONITOR,
        [PCG_ROLE_BOARD]        = PCG_PRESET_CORKBOARD,
    },
};

cerr pcg_palette_from_scheme(pcg_palette *pal, pcg_matlib *lib, pcg_scheme scheme)
{
    if (!pal || !lib)               return CERR_INVALID_ARGUMENTS;
    if (scheme >= PCG_SCHEME_NR)    return CERR_INVALID_ARGUMENTS_REASON(.fmt = "unknown scheme");

    pcg_palette out = {};

    for (int role = 0; role < PCG_ROLE_NR; role++) {
        pcg_preset preset = pcg_schemes[scheme][role];

        if (preset != PCG_PRESET_NONE)  out.mat[role] = CRES_RET_CERR(pcg_matlib_preset(lib, preset));
    }

    *pal = out;

    return CERR_OK;
}
