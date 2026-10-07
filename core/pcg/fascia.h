/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_FASCIA_H__
#define __CLAP_PCG_FASCIA_H__

#include "error.h"
#include "linmath.h"
#include "pcg/sink.h"
#include "pcg/span.h"
#include "pcg/surface.h"

/**
 * enum pcg_fascia_surf - pcg_fascia_params surfaces
 * @PCG_FASCIA_FACE:    the faces that read: the void's side, the
 *                      underside, the top ledge and the ends of every run
 * @PCG_FASCIA_BACK:    the face behind the path, toward the floor; its top
 *                      is buried in the slab's edge, the rest is seen from
 *                      under the slab
 * @PCG_FASCIA_NR_SURF: number of surfaces
 */
enum pcg_fascia_surf {
    PCG_FASCIA_FACE,
    PCG_FASCIA_BACK,
    PCG_FASCIA_NR_SURF,
};

/**
 * struct pcg_fascia_params - a drop band's style
 * @surf:       per-surface finish, also by name
 * @depth:      drop below the site's y = 0
 * @lip:        reach past the path into the void
 * @tuck:       reach back from the path, into the slab's edge
 * @min_seg:    runs no longer than this are left out
 * @min_frac:   a cut crossing no more than this fraction of an edge
 *              doesn't break it
 *
 * The deep band that hangs under the edge of a floor around a void: a
 * box along every edge of a path, over [-@depth, 0], from @tuck behind
 * the path to @lip beyond it. Boxes on consecutive edges butt unmitred:
 * the overlaps land inside the solid or tucked into the slab.
 */
typedef struct pcg_fascia_params {
    union {
        pcg_surface surf[PCG_FASCIA_NR_SURF];
        struct {
            pcg_surface face, back;
        };
    };
    float   depth;
    float   lip;
    float   tuck;
    float   min_seg;
    float   min_frac;
} pcg_fascia_params;

/**
 * pcg_fascia_params_init() - a generic fascia's style
 * @p:  the params to fill, every field
 *
 * A 0.9 m drop, 0.06 m into the void and 0.02 m into the slab; runs of
 * 0.05 m or less are left out, cuts over 1/1000 of an edge break it.
 * Both surfaces are PCG_ROLE_STRUCTURE and PCG_COLLIDE_SOLID.
 */
void pcg_fascia_params_init(pcg_fascia_params *p);

/**
 * pcg_fascia_params_check() - check a fascia's style
 * @p:  the style
 *
 * The checks every emit makes before it emits anything. A parent that
 * embeds the style makes them before any of its children emits, so that
 * a bad style leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p, a dimension
 * that isn't finite, a depth that isn't positive, a negative lip, tuck or
 * minimum run, no width or a minimum fraction outside [0, 1)
 */
cerr_check pcg_fascia_params_check(const pcg_fascia_params *p);

/**
 * struct pcg_fascia_site - where a fascia goes
 * @frame:      the fascia's frame; its y is the top of the band
 * @pts:        the path in the frame's (a, c): a floor's edge, with the
 *              void to the left of every edge, toward its frame's +c
 * @nr_pts:     number of points
 * @cuts:       regions of the frame's (a, c) the band breaks over, such
 *              as where a flight passes under it; nullptr: none
 * @nr_cuts:    number of @cuts
 * @closed:     the path runs on from its last point back to the first
 *
 * Every edge runs U along the edge from its start in meters, so the
 * ends of the runs either side of a cut keep their place on the band.
 */
typedef struct pcg_fascia_site {
    pcg_frame           frame;
    const vec2          *pts;
    size_t              nr_pts;
    const pcg_region    *cuts;
    size_t              nr_cuts;
    bool                closed;
} pcg_fascia_site;

/**
 * pcg_fascia_check() - check a fascia's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * The checks every emit makes before it emits anything. A parent that
 * places a fascia among other children makes them before any of its
 * children emits, so that a bad fascia leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site,
 * what pcg_fascia_params_check() turns down, a frame pcg_frame_check()
 * turns down, a path of fewer than 2 points (3 if closed), with a point
 * that isn't finite or with coincident consecutive points, or cuts
 * missing or that pcg_regions_check() turns down
 */
cerr_check pcg_fascia_check(const pcg_fascia_params *p, const pcg_fascia_site *site);

/**
 * _pcg_fascia_emit() - emit a fascia
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx and for what
 * pcg_fascia_check() turns down; CERR_NOMEM; the pcg_band() errors
 */
cerr_check _pcg_fascia_emit(const pcg_ctx *ctx, const pcg_fascia_params *p, const pcg_fascia_site *site);

/**
 * define pcg_fascia_emit - emit a fascia at a site described by pcg_fascia_site initializers
 *
 * Syntax sugar for _pcg_fascia_emit().
 */
#define pcg_fascia_emit(_ctx, _p, args...) _pcg_fascia_emit((_ctx), (_p), &(const pcg_fascia_site){ args })

#endif /* __CLAP_PCG_FASCIA_H__ */
