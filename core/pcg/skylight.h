/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_SKYLIGHT_H__
#define __CLAP_PCG_SKYLIGHT_H__

#include <limits.h>
#include "error.h"
#include "pcg/sink.h"
#include "pcg/surface.h"

/** PCG_SKYLIGHT_SIDES_MAX - most aperture sides: the curb takes 12 vertices a side in one mesh */
#define PCG_SKYLIGHT_SIDES_MAX  (USHRT_MAX / 12)

/**
 * enum pcg_skylight_surf - pcg_skylight_params surfaces
 * @PCG_SKYLIGHT_FRAME:     the curb and the beams
 * @PCG_SKYLIGHT_DIFFUSER:  the glowing sheet over them, facing down
 * @PCG_SKYLIGHT_NR_SURF:   number of surfaces
 */
enum pcg_skylight_surf {
    PCG_SKYLIGHT_FRAME,
    PCG_SKYLIGHT_DIFFUSER,
    PCG_SKYLIGHT_NR_SURF,
};

/**
 * struct pcg_skylight_params - a coffered skylight's style
 * @curb_w:     the curb's width around the aperture, square to its edges
 * @curb_h:     the curb's height over the roof
 * @beam_w:     beam width
 * @beam_h:     beam depth, down from the top of the curb
 * @pitch:      the beam spacing to aim for: across each axis, the
 *              aperture takes the whole number of bays nearest to its
 *              extent over @pitch, all of one width
 * @embed:      how far the beam ends reach past the aperture into the
 *              curb; at most @curb_w
 * @cross_drop: how far the beams along +c sit below the beams along +a,
 *              so the tops don't fight where they cross; less than
 *              @beam_h
 * @lift:       the diffuser's height over the top of the curb
 * @surf:       per-surface finish, also by name
 *
 * A grid of beams over a regular polygonal aperture in a roof, framed by
 * a curb around it, under a glowing sheet as wide as the curb. The beams
 * run along the site frame's +a and +c, each clipped to the aperture's
 * chord where it crosses, and a beam on a chord narrower than a beam is
 * left out. The curb charts like a plate, the beams like boxes in the
 * site frame. The diffuser charts the site frame's plane stretched to
 * the grid: one @pitch of texture coordinates per bay on each axis,
 * counted from the aperture's bounding box, so a diffuser material that
 * repeats once per @pitch (a coffer per repeat) lays one cell in every
 * bay, under any frame.
 */
typedef struct pcg_skylight_params {
    float   curb_w;
    float   curb_h;
    float   beam_w;
    float   beam_h;
    float   pitch;
    float   embed;
    float   cross_drop;
    float   lift;
    union {
        pcg_surface surf[PCG_SKYLIGHT_NR_SURF];
        struct {
            pcg_surface frame, diffuser;
        };
    };
} pcg_skylight_params;

/**
 * pcg_skylight_params_init() - a coffered skylight's style
 * @p:  the params to fill, every field
 *
 * A 0.25 m curb 0.5 m high, 0.07 m beams 0.4 m deep about 1.2 m apart,
 * bearing 0.08 m into the curb, the beams along +c 0.03 m lower, and the
 * diffuser 0.12 m over the curb. The frame is PCG_ROLE_METAL and
 * PCG_COLLIDE_SOLID, the diffuser PCG_ROLE_DIFFUSER and
 * PCG_COLLIDE_NONE.
 */
void pcg_skylight_params_init(pcg_skylight_params *p);

/**
 * struct pcg_skylight_site - where a skylight goes
 * @frame:  the aperture's center, on the roof's top: the beams run along
 *          +a and +c
 * @sides:  the aperture, a regular polygon: its number of sides, edge 0
 *          facing -c (see poly2d_phase_edge0_neg_z())
 * @r:      its circumradius
 */
typedef struct pcg_skylight_site {
    pcg_frame   frame;
    int         sides;
    float       r;
} pcg_skylight_site;

/**
 * pcg_skylight_check() - check a skylight's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * The checks every emit makes before it emits anything, the bays the
 * aperture divides into included. A parent that places a skylight among
 * other children makes them before any of its children emits, so that a
 * bad skylight leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a dimension or radius that isn't
 * finite, a curb, beam or pitch measure that isn't positive, an embed
 * outside [0, curb_w], a cross drop outside [0, beam_h), a negative lift,
 * an aperture of fewer than 3 sides or without a radius, or bays no wider
 * than a beam; CERR_TOO_LARGE for more bays than an int counts or more
 * than PCG_SKYLIGHT_SIDES_MAX sides; CERR_NOMEM
 */
cerr_check pcg_skylight_check(const pcg_skylight_params *p, const pcg_skylight_site *site);

/**
 * _pcg_skylight_emit() - emit a skylight
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for what pcg_skylight_check()
 * turns down; CERR_TOO_LARGE for more bays than an int counts or more
 * than PCG_SKYLIGHT_SIDES_MAX sides; CERR_NOMEM; the pcg_plate() and
 * pcg_box() errors
 */
cerr_check _pcg_skylight_emit(const pcg_ctx *ctx, const pcg_skylight_params *p, const pcg_skylight_site *site);

/**
 * define pcg_skylight_emit - emit a skylight at a site described by pcg_skylight_site initializers
 *
 * Syntax sugar for _pcg_skylight_emit().
 */
#define pcg_skylight_emit(_ctx, _p, args...) _pcg_skylight_emit((_ctx), (_p), &(const pcg_skylight_site){ args })

#endif /* __CLAP_PCG_SKYLIGHT_H__ */
