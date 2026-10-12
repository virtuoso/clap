/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_FACADE_H__
#define __CLAP_PCG_FACADE_H__

#include "error.h"
#include "linmath.h"
#include "pcg/sink.h"
#include "pcg/span.h"
#include "pcg/window-wall.h"

/**
 * struct pcg_facade_params - a perimeter facade's style
 * @wall:   every side's window wall
 *
 * A building's outer wall: a window wall along every side of a closed
 * polygon, which is the walls' centerline. Each side is a window wall
 * from its point to the next one, straddling the line, with its +c to
 * the left of the run: wound counterclockwise, every side's +c faces
 * into the polygon. Each side ends square at its points, so neighbouring
 * sides overlap at the corners.
 */
typedef struct pcg_facade_params {
    pcg_window_wall_params  wall;
} pcg_facade_params;

/**
 * pcg_facade_params_init() - a generic facade's style
 * @p:  the params to fill, every field
 *
 * pcg_window_wall_params_init()'s window wall on every side.
 */
void pcg_facade_params_init(pcg_facade_params *p);

/**
 * struct pcg_facade_edge - the openings in one side of a facade
 * @edge:           the side, from point @edge of the polygon to the next
 * @openings:       portals in the side, such as an entrance, centered at
 *                  their @at from the side's start, as the window wall
 *                  takes them (pcg_window_wall_site)
 * @nr_openings:    number of @openings
 */
typedef struct pcg_facade_edge {
    size_t              edge;
    const pcg_opening   *openings;
    size_t              nr_openings;
} pcg_facade_edge;

/**
 * struct pcg_facade_site - where a facade goes
 * @frame:      the facade's frame; its y is the floor the walls stand on
 * @pts:        the walls' centerline, a closed polygon in the frame's
 *              (a, c)
 * @nr_pts:     number of points, and of sides
 * @edges:      the sides that have openings, in ascending order of
 *              their @edge; nullptr: none
 * @nr_edges:   number of @edges
 * @h:          wall height
 */
typedef struct pcg_facade_site {
    pcg_frame               frame;
    const vec2              *pts;
    size_t                  nr_pts;
    const pcg_facade_edge   *edges;
    size_t                  nr_edges;
    float                   h;
} pcg_facade_site;

/**
 * pcg_facade_check() - check a facade's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * Everything _pcg_facade_emit() checks before it emits anything: the
 * polygon, and every side the way pcg_window_wall_check() checks it.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site, a
 * frame pcg_frame_check() turns down, a polygon of fewer than 3 points,
 * missing or with a point that isn't finite, consecutive points that
 * coincide, or edges missing, past the last side or out of order; the
 * pcg_window_wall_check() errors of any side
 */
cerr_check pcg_facade_check(const pcg_facade_params *p, const pcg_facade_site *site);

/**
 * _pcg_facade_emit() - emit a facade
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Checks every side before any of them is emitted.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx; the
 * pcg_facade_check() and pcg_window_wall_emit() errors
 */
cerr_check _pcg_facade_emit(const pcg_ctx *ctx, const pcg_facade_params *p, const pcg_facade_site *site);

/**
 * define pcg_facade_emit - emit a facade at a site described by pcg_facade_site initializers
 *
 * Syntax sugar for _pcg_facade_emit().
 */
#define pcg_facade_emit(_ctx, _p, args...) _pcg_facade_emit((_ctx), (_p), &(const pcg_facade_site){ args })

#endif /* __CLAP_PCG_FACADE_H__ */
