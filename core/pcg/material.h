/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_MATERIAL_H__
#define __CLAP_PCG_MATERIAL_H__

#include <stdint.h>
#include "draw.h"
#include "error.h"
#include "model.h"
#include "render.h"
#include "util.h"
#include "pcg/surface.h"
#include "pcg/texgen.h"

/**
 * struct pcg_label_style - how labels on a material are lettered
 * @plate:  background of every label
 * @ink:    colour of the lettering
 * @filter: filter of the atlas the labels are painted into
 */
typedef struct pcg_label_style {
    vec4            plate;
    vec4            ink;
    texture_filter  filter;
} pcg_label_style;

/**
 * struct pcg_material - a finish generated geometry can wear
 * @name:           names the meshes that wear it; owned by whoever made
 *                  the material
 * @albedo:         albedo texture; the material library's, and every
 *                  model that wears it holds a reference of its own
 * @normal:         tangent-space normal map, the same way; nullptr: none
 * @emission:       emission map, the same way; nullptr: none
 * @shade:          shading, copied into model3dtx::mat whole; its
 *                  uv_factor is the texture repeats per meter for
 *                  PCG_UV_METERS and 1 for PCG_UV_UNIT
 * @label:          lettering, for materials that labels wear
 * @bloom:          entity3d::bloom_intensity; 0 leaves it to the render
 *                  options
 * @uv_mode:        what the texture coordinates of the surfaces wearing
 *                  it must mean
 * @alpha_blend:    model3d::alpha_blend
 * @skip_shadow:    model3d::skip_shadow: casts no shadow
 * @cull_face:      model3d::cull_face
 *
 * Emitting only compares material pointers, to batch the surfaces that
 * wear the same one, and checks the UV contract. Collision and navmesh
 * flags belong to the surface, not the material. Materials outlive
 * every sink that refers to them.
 */
typedef struct pcg_material {
    const char      *name;
    texture_t       *albedo;
    texture_t       *normal;
    texture_t       *emission;
    material        shade;
    pcg_label_style label;
    float           bloom;
    pcg_uv_mode     uv_mode;
    bool            alpha_blend;
    bool            skip_shadow;
    bool            cull_face;
} pcg_material;
cresp_ret(pcg_material);

/**
 * enum pcg_texgen - the generator that paints a recipe's albedo
 * @PCG_TEXGEN_SURFACE: pcg_texgen_surface()
 * @PCG_TEXGEN_WOOD:    pcg_texgen_wood()
 * @PCG_TEXGEN_CARPET:  pcg_texgen_carpet()
 * @PCG_TEXGEN_CORK:    pcg_texgen_cork(), no relief
 * @PCG_TEXGEN_NR:      number of generators
 */
typedef enum pcg_texgen {
    PCG_TEXGEN_SURFACE = 0,
    PCG_TEXGEN_WOOD,
    PCG_TEXGEN_CARPET,
    PCG_TEXGEN_CORK,
    PCG_TEXGEN_NR
} pcg_texgen;

/**
 * enum pcg_glow - the generator that paints a recipe's emission map
 * @PCG_GLOW_NONE:  no emission map
 * @PCG_GLOW_FLAT:  pcg_texgen_flat()
 * @PCG_GLOW_GRID:  pcg_texgen_glow_grid()
 * @PCG_GLOW_NR:    number of emission generators
 */
typedef enum pcg_glow {
    PCG_GLOW_NONE = 0,
    PCG_GLOW_FLAT,
    PCG_GLOW_GRID,
    PCG_GLOW_NR
} pcg_glow;

/**
 * struct pcg_recipe - how to make a material
 * @name:           material name, copied
 * @gen:            albedo (and normal map) generator
 * @surface:        @gen options for PCG_TEXGEN_SURFACE
 * @wood:           @gen options for PCG_TEXGEN_WOOD
 * @carpet:         @gen options for PCG_TEXGEN_CARPET
 * @cork:           @gen options for PCG_TEXGEN_CORK
 * @glow:           emission map generator
 * @flat:           @glow options for PCG_GLOW_FLAT
 * @grid:           @glow options for PCG_GLOW_GRID
 * @shade:          shading, see &struct pcg_material
 * @label:          lettering, see &struct pcg_material
 * @bloom:          see &struct pcg_material
 * @wrap:           wrap mode of every texture
 * @filter:         filter of the albedo and normal map
 * @glow_filter:    filter of the emission map
 * @uv_mode:        see &struct pcg_material
 * @alpha_blend:    see &struct pcg_material
 * @skip_shadow:    see &struct pcg_material
 * @cull_face:      see &struct pcg_material
 *
 * The generators' seeds are salts: a material library mixes its own seed
 * into them, so one library seed varies every texture at once.
 */
typedef struct pcg_recipe {
    const char          *name;
    pcg_texgen          gen;
    union {
        pcg_texgen_surface_opts surface;
        pcg_texgen_wood_opts    wood;
        pcg_texgen_carpet_opts  carpet;
        pcg_texgen_cork_opts    cork;
    };
    pcg_glow            glow;
    union {
        pcg_texgen_flat_opts        flat;
        pcg_texgen_glow_grid_opts   grid;
    };
    material            shade;
    pcg_label_style     label;
    float               bloom;
    texture_wrap        wrap;
    texture_filter      filter;
    texture_filter      glow_filter;
    pcg_uv_mode         uv_mode;
    bool                alpha_blend;
    bool                skip_shadow;
    bool                cull_face;
} pcg_recipe;

/**
 * enum pcg_preset - the material library's ready-made materials
 * @PCG_PRESET_NONE:            no preset
 * @PCG_PRESET_CONCRETE:        pale concrete
 * @PCG_PRESET_WALNUT:          narrow dark walnut boards
 * @PCG_PRESET_OAK_FLOOR:       oak floor boards, running along U
 * @PCG_PRESET_WOOD_PANEL:      wide wall panelling boards
 * @PCG_PRESET_BRONZE:          dark bronze
 * @PCG_PRESET_GLASS:           tinted glass, blended, casts no shadow
 * @PCG_PRESET_CARPET_TILE:     blue carpet tiles, half a meter each
 * @PCG_PRESET_FIXTURE_GLOW:    bronze glowing orange all over, PCG_UV_UNIT,
 *                              casts no shadow
 * @PCG_PRESET_DIFFUSER:        a glowing coffer grid, a coffer per repeat
 * @PCG_PRESET_SIGN_PLATE:      dark plate with pale lettering, PCG_UV_UNIT,
 *                              casts no shadow
 * @PCG_PRESET_PLASTIC_SHELL:   orange plastic
 * @PCG_PRESET_CORKBOARD:       a pinboard face, PCG_UV_UNIT
 * @PCG_PRESET_NR:              number of presets
 */
typedef enum pcg_preset {
    PCG_PRESET_NONE = 0,
    PCG_PRESET_CONCRETE,
    PCG_PRESET_WALNUT,
    PCG_PRESET_OAK_FLOOR,
    PCG_PRESET_WOOD_PANEL,
    PCG_PRESET_BRONZE,
    PCG_PRESET_GLASS,
    PCG_PRESET_CARPET_TILE,
    PCG_PRESET_FIXTURE_GLOW,
    PCG_PRESET_DIFFUSER,
    PCG_PRESET_SIGN_PLATE,
    PCG_PRESET_PLASTIC_SHELL,
    PCG_PRESET_CORKBOARD,
    PCG_PRESET_NR
} pcg_preset;

/**
 * pcg_recipe_init() - a preset's recipe, to tweak before pcg_matlib_add()
 * @r:      the recipe
 * @preset: the preset
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for PCG_PRESET_NONE or an
 * unknown @preset
 */
cerr_check pcg_recipe_init(pcg_recipe *r, pcg_preset preset);

/**
 * pcg_preset_base() - the preset whose textures a preset wears
 * @preset: the preset
 *
 * Some presets differ from another one only in their shading, emission
 * or relief, and share its albedo: they paint the same albedo as their
 * base from pcg_recipe_init(), and a material library makes them from
 * their base's textures.
 *
 * Return: the base preset; @preset itself if it paints its own;
 * CERR_INVALID_ARGUMENTS for PCG_PRESET_NONE or an unknown @preset
 */
cres_check(int) pcg_preset_base(pcg_preset preset);

/**
 * pcg_recipe_paint() - paint a recipe's albedo and normal map
 * @r:      the recipe
 * @seed:   mixed into the generator's seed
 *
 * Return: the canvases; CERR_INVALID_ARGUMENTS for a bad generator or
 * its options, CERR_NOMEM
 */
cres_check(pcg_texgen_maps) pcg_recipe_paint(const pcg_recipe *r, uint32_t seed);

/**
 * pcg_recipe_paint_glow() - paint a recipe's emission map
 * @r:      the recipe, with an emission generator
 * @seed:   mixed into the generator's seed
 *
 * Return: the canvas, the caller's; CERR_INVALID_ARGUMENTS for
 * PCG_GLOW_NONE, a bad generator or its options, CERR_NOMEM
 */
cresp_check(canvas) pcg_recipe_paint_glow(const pcg_recipe *r, uint32_t seed);

/*
 * A material library makes materials and owns them, and their textures,
 * for as long as it lives. The models made from its materials hold
 * references to the textures they wear, so the library can go as soon
 * as the geometry wearing them is committed.
 */
typedef struct pcg_matlib pcg_matlib;
cresp_ret(pcg_matlib);

/**
 * pcg_matlib_new() - make an empty material library
 * @r:      renderer the textures are made for
 * @seed:   mixed into every recipe's generator seeds
 *
 * Return: the library, the caller's; CERR_INVALID_ARGUMENTS without @r,
 * or CERR_NOMEM
 */
cresp_check(pcg_matlib) pcg_matlib_new(renderer_t *r, uint32_t seed);

/**
 * pcg_matlib_free() - free a material library
 * @lib:    the library, may be nullptr
 *
 * Frees its materials and puts its texture references: the textures go
 * when the last model wearing them does.
 *
 * Context: before the renderer goes
 */
void pcg_matlib_free(pcg_matlib *lib);
DECLARE_CLEANUP(pcg_matlib);

/**
 * pcg_matlib_add() - make a material from a recipe
 * @lib:    the library
 * @r:      the recipe
 *
 * Paints and uploads the textures right away.
 *
 * Return: the material, the library's; CERR_INVALID_ARGUMENTS for a bad
 * recipe: no name, a bad generator, its options or UV mode, or a UV
 * factor that doesn't suit the UV mode; CERR_NOMEM or the renderer's
 * error
 */
cresp_check(pcg_material) pcg_matlib_add(pcg_matlib *lib, const pcg_recipe *r);

/**
 * pcg_matlib_preset() - a preset's material
 * @lib:    the library
 * @preset: the preset
 *
 * Made on the first call, and the same material ever after: everything
 * asking for a preset shares it, and batches with it. Presets with a
 * base preset (see pcg_preset_base()) wear their base's textures.
 * Derive a material to change it, never change a preset's.
 *
 * Return: the material, the library's; CERR_INVALID_ARGUMENTS for
 * PCG_PRESET_NONE or an unknown @preset; pcg_matlib_add()'s errors
 */
cresp_check(pcg_material) pcg_matlib_preset(pcg_matlib *lib, pcg_preset preset);

/**
 * pcg_matlib_derive() - a copy of a material to change
 * @lib:    the library
 * @base:   the material to copy, one of @lib's
 * @name:   the copy's name, copied
 *
 * The copy wears the same textures; change its shading, flags or
 * textures (with pcg_matlib_upload() ones) before geometry wearing it is
 * committed. It is a material of its own, so it batches separately.
 *
 * Return: the copy, the library's; CERR_INVALID_ARGUMENTS without a
 * @name or for a @base that isn't @lib's; CERR_NOMEM
 */
cresp_check(pcg_material) pcg_matlib_derive(pcg_matlib *lib, const pcg_material *base, const char *name);

/**
 * pcg_matlib_upload() - a library texture from a canvas
 * @lib:    the library
 * @c:      the canvas
 * @wrap:   wrap mode
 * @filter: minification and magnification filter
 *
 * For textures painted outside the recipes, like label atlases.
 *
 * Return: the texture, the library's; pcg_texgen_upload()'s errors or
 * CERR_NOMEM
 */
cresp_check(texture_t) pcg_matlib_upload(pcg_matlib *lib, canvas *c, texture_wrap wrap, texture_filter filter);

/**
 * enum pcg_scheme - whole-building finish schemes
 * @PCG_SCHEME_CONCRETE_WOOD:   concrete structure, oak floors, wood panelled
 *                              walls, walnut rails
 * @PCG_SCHEME_CONCRETE:        concrete structure, floors, walls and rails
 * @PCG_SCHEME_WOOD:            wood structure, floors, walls and rails
 * @PCG_SCHEME_NR:              number of schemes
 *
 * The other roles are the same in every scheme: bronze metal, glass,
 * carpet tiles, glowing fixtures and diffusers, sign plates, plastic
 * furniture and corkboards. No scheme has a screen.
 */
typedef enum pcg_scheme {
    PCG_SCHEME_CONCRETE_WOOD = 0,
    PCG_SCHEME_CONCRETE,
    PCG_SCHEME_WOOD,
    PCG_SCHEME_NR
} pcg_scheme;

/**
 * pcg_palette_from_scheme() - a palette of preset materials
 * @pal:    the palette, filled in whole: a role without a preset in
 *          @scheme gets nullptr
 * @lib:    the library the presets come from
 * @scheme: the scheme
 *
 * Makes the presets the scheme names that @lib doesn't have yet.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for an unknown @scheme;
 * pcg_matlib_preset()'s errors
 */
cerr_check pcg_palette_from_scheme(pcg_palette *pal, pcg_matlib *lib, pcg_scheme scheme);

#endif /* __CLAP_PCG_MATERIAL_H__ */
