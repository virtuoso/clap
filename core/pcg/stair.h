/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_STAIR_H__
#define __CLAP_PCG_STAIR_H__

#include "error.h"
#include "linmath.h"
#include "pcg/sink.h"
#include "pcg/surface.h"

/**
 * enum pcg_stair_surf - pcg_stair_params surfaces
 * @PCG_STAIR_TREAD:    tread tops, the walk surface
 * @PCG_STAIR_RISER:    riser faces, toward the foot of the flight
 * @PCG_STAIR_SIDE:     the flight's two side faces
 * @PCG_STAIR_SOFFIT:   the stepped underside: the bottoms of the steps and
 *                      their ends below the treads, facing up the flight
 * @PCG_STAIR_LANDING:  landing slabs, all faces
 * @PCG_STAIR_NR_SURF:  number of surfaces
 */
enum pcg_stair_surf {
    PCG_STAIR_TREAD,
    PCG_STAIR_RISER,
    PCG_STAIR_SIDE,
    PCG_STAIR_SOFFIT,
    PCG_STAIR_LANDING,
    PCG_STAIR_NR_SURF,
};

/**
 * struct pcg_stair_params - a straight flight's style
 * @surf:       per-surface finish, also by name
 * @width:      flight width
 * @riser_max:  tallest riser allowed; the flight takes the fewest risers
 *              of at most this height that climb the site's rise
 * @going:      tread depth
 * @waist:      solid depth under the treads
 * @landing_t:  landing slab thickness
 *
 * A solid flight of equal steps: one box per step, from a waist below
 * the tread before it to its own tread, so the underside steps with the
 * treads. Every box charts the flight's local meters, the same way for
 * every step, so the faces of one surface tile up the flight. Style
 * only: the flight's position, climb and landings are its site.
 */
typedef struct pcg_stair_params {
    union {
        pcg_surface surf[PCG_STAIR_NR_SURF];
        struct {
            pcg_surface tread, riser, side, soffit, landing;
        };
    };
    float   width;
    float   riser_max;
    float   going;
    float   waist;
    float   landing_t;
} pcg_stair_params;

/**
 * pcg_stair_params_init() - a generic flight's style
 * @p:  the params to fill, every field
 *
 * A 1.2 m flight of risers up to 0.17 and 0.28 goings, on a 0.15 waist,
 * with 0.2 landings. Every surface is PCG_ROLE_STRUCTURE and
 * PCG_COLLIDE_SOLID: a monolithic concrete flight.
 */
void pcg_stair_params_init(pcg_stair_params *p);

/**
 * pcg_stair_params_check() - check a flight's style
 * @p:  the style
 *
 * The checks every emit makes before it emits anything. A parent that
 * embeds the style makes them before any of its children emits, so that
 * a bad style leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p, or a width,
 * going, waist, landing_t or riser_max that isn't positive and finite
 */
cerr_check pcg_stair_params_check(const pcg_stair_params *p);

/**
 * struct pcg_stair_site - one flight's fit
 * @frame:      the first riser's foot on the centerline, +a ascending
 * @rise:       total climb
 * @land_lo:    landing length behind the first riser, 0 for none
 * @land_hi:    landing length past the last tread, 0 for none
 *
 * Tread k tops out at (k + 1) * @rise / pcg_stair_steps(), so the last
 * one is flush with the arrival floor at @rise. The landings are level
 * slabs of the style's landing_t, flush with the floors at either end.
 */
typedef struct pcg_stair_site {
    pcg_frame   frame;
    float       rise;
    float       land_lo;
    float       land_hi;
} pcg_stair_site;

/**
 * pcg_stair_steps() - the riser count a flight takes for a climb
 * @p:      flight style
 * @rise:   the climb
 *
 * The smallest n for which @rise / n, as a float, is at most
 * @p->riser_max. The float quotient @rise / @p->riser_max rounds, so its
 * ceiling is a candidate that can be one off either way; the candidate
 * and its neighbour are settled by that same test, without a tolerance.
 *
 * Return: the step count; CERR_INVALID_ARGUMENTS for a @rise or
 * @p->riser_max that isn't positive and finite; CERR_TOO_LARGE for more
 * risers than a float counts exactly
 */
cres_check(int) pcg_stair_steps(const pcg_stair_params *p, float rise);

/**
 * pcg_stair_run() - a flight's run length
 * @p:      flight style
 * @rise:   the climb
 *
 * Return: pcg_stair_steps() * @p->going, the length along +a from the
 * first riser to the far end of the last tread; the pcg_stair_steps()
 * errors
 */
cres_check(float) pcg_stair_run(const pcg_stair_params *p, float rise);

/**
 * pcg_stair_check() - check a flight's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * The checks every emit makes before it emits anything. A parent that
 * places a flight among other children makes them before any of its
 * children emits, so that a bad flight leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * dimension that isn't positive and finite, a frame pcg_frame_check()
 * turns down or a landing that is negative or isn't finite; the
 * pcg_stair_steps() errors
 */
cerr_check pcg_stair_check(const pcg_stair_params *p, const pcg_stair_site *site);

/**
 * _pcg_stair_emit() - emit a flight
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx and for what
 * pcg_stair_check() turns down; the pcg_stair_steps() and pcg_box()
 * errors
 */
cerr_check _pcg_stair_emit(const pcg_ctx *ctx, const pcg_stair_params *p, const pcg_stair_site *site);

/**
 * define pcg_stair_emit - emit a flight at a site described by pcg_stair_site initializers
 *
 * Syntax sugar for _pcg_stair_emit().
 */
#define pcg_stair_emit(_ctx, _p, args...) _pcg_stair_emit((_ctx), (_p), &(const pcg_stair_site){ args })

#endif /* __CLAP_PCG_STAIR_H__ */
