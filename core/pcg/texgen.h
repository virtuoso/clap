/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_TEXGEN_H__
#define __CLAP_PCG_TEXGEN_H__

#include <stdint.h>
#include "draw.h"
#include "error.h"
#include "linmath.h"
#include "render.h"
#include "util.h"

/*
 * Procedural material textures: generators that paint albedo canvases
 * with tileable noise and bake their relief into tangent-space normal
 * map canvases. Painting is pure CPU work with no renderer; uploading a
 * canvas is a separate step, see pcg_texgen_upload(). The same options and
 * seed always paint the same texels.
 */

/**
 * struct pcg_texgen_maps - the canvases a material generator paints
 * @albedo: RGBA8 albedo, the caller's
 * @normal: RGBA8 tangent-space normal map, the caller's; nullptr when
 *          the options ask for no relief
 */
typedef struct pcg_texgen_maps {
    canvas  *albedo;
    canvas  *normal;
} pcg_texgen_maps;
cres_ret(pcg_texgen_maps);

/**
 * pcg_texgen_maps_done() - free a generator's canvases
 * @m:  the canvases; either may be nullptr, both are nullptr afterwards
 */
void pcg_texgen_maps_done(pcg_texgen_maps *m);
DECLARE_CLEANUP_EXACT(pcg_texgen_maps);

/**
 * struct pcg_texgen_surface_opts - blotchy mineral surface (concrete, plaster, metal)
 * @size:       texture side, texels
 * @base:       base albedo
 * @blotch:     low-frequency lightness variation amplitude
 * @speckle:    per-texel grain amplitude
 * @cells:      blotch noise cells per tile
 * @alpha:      alpha channel, 1 for opaque
 * @bump:       normal map amplitude, 0 for no normal map
 * @seed:       RNG seed
 */
typedef struct pcg_texgen_surface_opts {
    int         size;
    vec3        base;
    float       blotch;
    float       speckle;
    int         cells;
    float       alpha;
    float       bump;
    uint32_t    seed;
} pcg_texgen_surface_opts;

/**
 * pcg_texgen_surface() - paint a blotchy mineral surface
 * @o:  options
 *
 * The lightness sources double as relief: blotches swell, speckles pit.
 *
 * Return: the canvases; CERR_INVALID_ARGUMENTS for a @size or @cells
 * below 1 or a negative @bump; CERR_NOMEM
 */
cres_check(pcg_texgen_maps) pcg_texgen_surface(const pcg_texgen_surface_opts *o);

/**
 * struct pcg_texgen_wood_opts - board-and-grain wood surface
 * @size:       texture side, texels
 * @planks:     boards across the tile
 * @light:      light board albedo
 * @dark:       dark board albedo
 * @grain:      grain streak contrast
 * @seam:       board seam width, fraction of a board
 * @seam_floor: lightness right at a board's edge, rising to 1 at @seam
 *              into the board
 * @bump:       normal map amplitude, 0 for no normal map
 * @seed:       RNG seed
 * @along_u:    boards run along U (default: along V)
 *
 * Each board picks its tone between @light and @dark, gets anisotropic
 * fBm grain streaks running along it and a darkened seam at its edges.
 * The height field sinks into the seams, ripples with the grain and
 * offsets whole boards, so the normal map reads as separate planks.
 */
typedef struct pcg_texgen_wood_opts {
    int         size;
    int         planks;
    vec3        light, dark;
    float       grain;
    float       seam;
    float       seam_floor;
    float       bump;
    uint32_t    seed;
    bool        along_u;
} pcg_texgen_wood_opts;

/**
 * pcg_texgen_wood() - paint boards of wood
 * @o:  options
 *
 * Return: the canvases; CERR_INVALID_ARGUMENTS for a @size or @planks
 * below 1 or a negative @bump; CERR_NOMEM
 */
cres_check(pcg_texgen_maps) pcg_texgen_wood(const pcg_texgen_wood_opts *o);

/**
 * struct pcg_texgen_carpet_opts - quarter-turned carpet tiles
 * @size:       texture side, texels
 * @tiles:      carpet tiles across the texture
 * @base:       pile albedo
 * @accent:     alternate pile albedo, mixed in per tile
 * @grain:      pile row contrast
 * @fuzz:       per-texel fiber noise amplitude
 * @seam:       tile seam width, fraction of a tile
 * @seam_floor: lightness right at a tile's edge, rising to 1 at @seam
 *              into the tile
 * @bump:       normal map amplitude, 0 for no normal map
 * @seed:       RNG seed
 *
 * An office carpet-tile grid: every tile picks its tone between @base
 * and @accent, the pile rows run along u or v by tile parity (the
 * quarter-turn look), fiber fuzz on top, seams slightly sunk.
 */
typedef struct pcg_texgen_carpet_opts {
    int         size;
    int         tiles;
    vec3        base, accent;
    float       grain;
    float       fuzz;
    float       seam;
    float       seam_floor;
    float       bump;
    uint32_t    seed;
} pcg_texgen_carpet_opts;

/**
 * pcg_texgen_carpet() - paint carpet tiles
 * @o:  options
 *
 * Return: the canvases; CERR_INVALID_ARGUMENTS for a @size or @tiles
 * below 1 or a negative @bump; CERR_NOMEM
 */
cres_check(pcg_texgen_maps) pcg_texgen_carpet(const pcg_texgen_carpet_opts *o);

/**
 * struct pcg_texgen_cork_opts - a pinboard face with pinned papers
 * @w:          texture width, texels; match the board's aspect
 * @h:          texture height, texels
 * @base:       board fabric albedo
 * @border:     border albedo
 * @border_w:   border width, fraction of the shorter side, 0..0.5
 * @paper_w:    width of the narrowest paper, fraction of @w
 * @paper_w_var: how much wider a paper can be, at random, fraction of
 *              @w; a paper is 1.1 to 1.6 times as tall as it is wide
 * @papers:     pinned paper count
 * @seed:       RNG seed
 *
 * A speckled fabric face inside a darker border, with paper rectangles
 * in a few off-white tones pinned across it. Not tileable: meant to
 * stretch once across a board face, so the texture clamps to edge.
 */
typedef struct pcg_texgen_cork_opts {
    int         w, h;
    vec3        base, border;
    float       border_w;
    float       paper_w;
    float       paper_w_var;
    int         papers;
    uint32_t    seed;
} pcg_texgen_cork_opts;

/**
 * pcg_texgen_cork() - paint a pinboard
 * @o:  options
 *
 * Papers keep inside the border: on a board too small for one, a paper
 * and its pin are cut at the border or left out.
 *
 * Return: the albedo canvas, without a normal map; CERR_INVALID_ARGUMENTS
 * for a size or @papers below 1 or a @border_w outside 0..0.5; CERR_NOMEM
 */
cres_check(pcg_texgen_maps) pcg_texgen_cork(const pcg_texgen_cork_opts *o);

/**
 * struct pcg_texgen_glow_grid_opts - a glowing coffer grid
 * @size:       texture side, texels
 * @cells_u:    coffers along U
 * @cells_v:    coffers along V
 * @glow:       emission colour of a fully lit coffer
 * @rim:        the dark grid line inside every coffer's edge, fraction
 *              of the coffer
 * @lit:        the level of the dimmest lit coffer, of @glow
 * @lit_var:    how much brighter a coffer can be, at random
 * @unlit:      the level of the grid lines
 * @seed:       RNG seed of the per-coffer level variation
 */
typedef struct pcg_texgen_glow_grid_opts {
    int         size;
    int         cells_u;
    int         cells_v;
    vec3        glow;
    float       rim;
    float       lit;
    float       lit_var;
    float       unlit;
    uint32_t    seed;
} pcg_texgen_glow_grid_opts;

/**
 * pcg_texgen_glow_grid() - paint warm coffers between dark grid lines
 * @o:  options
 *
 * Return: the RGB8 canvas for an emission map, the caller's;
 * CERR_INVALID_ARGUMENTS for a @size or cell count below 1; CERR_NOMEM
 */
cresp_check(canvas) pcg_texgen_glow_grid(const pcg_texgen_glow_grid_opts *o);

/**
 * struct pcg_texgen_flat_opts - one colour all over
 * @size:   texture side, texels
 * @color:  the colour
 */
typedef struct pcg_texgen_flat_opts {
    int         size;
    vec3        color;
} pcg_texgen_flat_opts;

/**
 * pcg_texgen_flat() - paint one colour all over
 * @o:  options
 *
 * Return: the RGB8 canvas for an emission map, the caller's;
 * CERR_INVALID_ARGUMENTS for a @size below 1; CERR_NOMEM
 */
cresp_check(canvas) pcg_texgen_flat(const pcg_texgen_flat_opts *o);

/**
 * pcg_texgen_upload() - turn a canvas into a counted texture
 * @r:      renderer
 * @c:      the canvas; its format and size are the texture's
 * @wrap:   wrap mode
 * @filter: minification and magnification filter
 *
 * Context: needs the renderer, see texture_new()
 * Return: the texture with one reference, the caller's, which puts it
 * with texture_done(); CERR_INVALID_ARGUMENTS without @r or @c, or the
 * renderer's error
 */
cresp_check(texture_t) pcg_texgen_upload(renderer_t *r, canvas *c, texture_wrap wrap, texture_filter filter);

#endif /* __CLAP_PCG_TEXGEN_H__ */
