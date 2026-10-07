/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __CLAP_PCG_BALUSTRADE_H__
#define __CLAP_PCG_BALUSTRADE_H__

#include "error.h"
#include "linmath.h"
#include "pcg/sink.h"
#include "pcg/span.h"
#include "pcg/surface.h"

/**
 * enum pcg_balustrade_surf - pcg_balustrade_params surfaces
 * @PCG_BALUSTRADE_CAP:     the cap rail on top
 * @PCG_BALUSTRADE_SHOE:    the shoe along the floor
 * @PCG_BALUSTRADE_INFILL:  the slats between them
 * @PCG_BALUSTRADE_NR_SURF: number of surfaces
 */
enum pcg_balustrade_surf {
    PCG_BALUSTRADE_CAP,
    PCG_BALUSTRADE_SHOE,
    PCG_BALUSTRADE_INFILL,
    PCG_BALUSTRADE_NR_SURF,
};

/**
 * struct pcg_balustrade_params - a slatted balustrade's style
 * @h:              top of the cap above the floor
 * @cap_t:          cap rail height
 * @rail_w:         cap rail width, across the run
 * @shoe_h:         shoe height
 * @shoe_w:         shoe width, across the run
 * @slat_w:         slat width, along the run
 * @slat_t:         slat thickness, across the run
 * @gap:            target gap between slats
 * @slat_overlap:   how far the slats reach into the shoe and the cap
 * @slope_shoe_r:   circumradius of the square shoe rod of a sloped run
 * @slope_cap_r:    circumradius of the square cap rod of a sloped run
 * @min_run:        runs of a path no longer than this are left out
 * @merge:          cuts of a path no farther apart than this merge, and
 *                  the piece between them goes
 * @min_frac:       a cut crossing no more than this fraction of a path's
 *                  edge doesn't break it
 * @surf:           per-surface finish, also by name
 *
 * A wall of vertical slats between a shoe along the floor and a cap rail,
 * all centered on the run's line. A level run's shoe and cap are boxes of
 * @shoe_w by @shoe_h and @rail_w by @cap_t. A run that climbs, such as a
 * stair's, slopes its shoe and cap as square rods with a flat on top,
 * centered half the shoe's height above the floor and half the cap's
 * below the top, while its slats stay vertical. The run's length is
 * divided into slats of @slat_w at an even pitch as near @slat_w + @gap
 * as the length allows, at least one.
 */
typedef struct pcg_balustrade_params {
    float   h;
    float   cap_t;
    float   rail_w;
    float   shoe_h;
    float   shoe_w;
    float   slat_w;
    float   slat_t;
    float   gap;
    float   slat_overlap;
    float   slope_shoe_r;
    float   slope_cap_r;
    float   min_run;
    float   merge;
    float   min_frac;
    union {
        pcg_surface surf[PCG_BALUSTRADE_NR_SURF];
        struct {
            pcg_surface cap, shoe, infill;
        };
    };
} pcg_balustrade_params;

/**
 * pcg_balustrade_params_init() - a generic balustrade's style
 * @p:  the params to fill, every field
 *
 * 1.05 m tall: a 0.10 by 0.07 cap, a 0.081 by 0.10 shoe and 0.09 by
 * 0.045 slats 0.05 apart, reaching 0.02 into both; sloped, a 0.07 shoe
 * rod and a 0.06 cap rod. A path drops runs of 0.15 or less, merges cuts
 * within 0.05 and ignores crossings of 1/1000 of an edge or less. Every
 * surface is PCG_ROLE_RAIL and PCG_COLLIDE_HAZARD: a rail is solid, but
 * its flat tops must never bake as a walkable path.
 */
void pcg_balustrade_params_init(pcg_balustrade_params *p);

/**
 * pcg_balustrade_params_check() - check a balustrade's style
 * @p:  the style
 *
 * The checks every emit makes before it emits anything. A parent that
 * embeds the style makes them before any of its children emits, so that
 * a bad style leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p, a dimension
 * that isn't finite, or isn't positive, a negative gap, overlap, minimum
 * run or merge distance, a minimum fraction outside [0, 1) or slats with
 * no height between the shoe and the cap
 */
cerr_check pcg_balustrade_params_check(const pcg_balustrade_params *p);

/**
 * struct pcg_balustrade_site - where a straight run goes
 * @frame:  the run's start on its line, +a along the run; its local
 *          meters chart the boxes
 * @len:    run length along +a
 * @rise:   climb over @len, 0 for a level run
 * @y:      the floor the run starts on, on the frame's local y
 *
 * @y lets runs that stand at different heights in one frame, such as a
 * flight's rails and the rail along its arrival landing, keep one
 * texture grid.
 */
typedef struct pcg_balustrade_site {
    pcg_frame   frame;
    float       len;
    float       rise;
    float       y;
} pcg_balustrade_site;

/**
 * pcg_balustrade_check() - check a straight run's style and fit
 * @p:      the style
 * @site:   the fit
 *
 * The checks every emit makes before it emits anything. A parent that
 * places a run among other children makes them before any of its
 * children emits, so that a bad run leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @site,
 * what pcg_balustrade_params_check() turns down, a frame
 * pcg_frame_check() turns down, a run without length or a rise or floor
 * that isn't finite
 */
cerr_check pcg_balustrade_check(const pcg_balustrade_params *p, const pcg_balustrade_site *site);

/**
 * _pcg_balustrade_emit() - emit a straight balustrade run
 * @ctx:    where it goes
 * @p:      the style
 * @site:   the fit
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx and for what
 * pcg_balustrade_check() turns down; CERR_TOO_LARGE for more slats than
 * an int counts; the pcg_box() and pcg_rod() errors
 */
cerr_check _pcg_balustrade_emit(const pcg_ctx *ctx, const pcg_balustrade_params *p,
                                const pcg_balustrade_site *site);

/**
 * define pcg_balustrade_emit - emit a balustrade run at a site described by pcg_balustrade_site initializers
 *
 * Syntax sugar for _pcg_balustrade_emit().
 */
#define pcg_balustrade_emit(_ctx, _p, args...) \
    _pcg_balustrade_emit((_ctx), (_p), &(const pcg_balustrade_site){ args })

/**
 * struct pcg_balustrade_path - where a balustrade along a polyline goes
 * @frame:      the path's frame; its local meters hold the points
 * @pts:        the path in the frame's (a, c)
 * @nr_pts:     number of points
 * @cuts:       regions of the frame's (a, c) where the path stays open,
 *              such as where a stair lands on the floor; nullptr: none
 * @nr_cuts:    number of @cuts
 * @y:          the floor, on the frame's local y
 * @inset:      the rail's line runs this far to the right of every edge
 *              (its frame's -c): along a floor's edge with the void to
 *              its left, back onto the floor
 * @closed:     the path runs on from its last point back to the first
 *
 * Every edge is a level run along its own line, from the edge's start
 * point. The cuts take their pieces out of the path itself, before the
 * rail's line is moved in, and runs stop on either side of them; runs
 * on consecutive edges butt unmitred.
 */
typedef struct pcg_balustrade_path {
    pcg_frame           frame;
    const vec2          *pts;
    size_t              nr_pts;
    const pcg_region    *cuts;
    size_t              nr_cuts;
    float               y;
    float               inset;
    bool                closed;
} pcg_balustrade_path;

/**
 * pcg_balustrade_path_check() - check a balustrade path's style and fit
 * @p:      the style
 * @path:   the fit
 *
 * The checks every emit makes before it emits anything. A parent that
 * places a path among other children makes them before any of its
 * children emits, so that a bad path leaves nothing behind.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS for a missing @p or @path,
 * what pcg_balustrade_params_check() turns down, a frame
 * pcg_frame_check() turns down, a path of fewer than 2 points (3 if
 * closed), a point that isn't finite, coincident consecutive points,
 * cuts missing or that pcg_regions_check() turns down, or an inset or
 * floor that isn't finite
 */
cerr_check pcg_balustrade_path_check(const pcg_balustrade_params *p, const pcg_balustrade_path *path);

/**
 * _pcg_balustrade_path_emit() - emit a balustrade along a path
 * @ctx:    where it goes
 * @p:      the style
 * @path:   the fit
 *
 * Every edge is cut with pcg_segment_runs() by @path->cuts, with the
 * style's @min_frac, @merge and @min_run, and every run that is left is
 * emitted as a level _pcg_balustrade_emit() run.
 *
 * Return: CERR_OK; CERR_INVALID_ARGUMENTS without @ctx and for what
 * pcg_balustrade_path_check() turns down; CERR_NOMEM; the
 * pcg_segment_runs() and _pcg_balustrade_emit() errors
 */
cerr_check _pcg_balustrade_path_emit(const pcg_ctx *ctx, const pcg_balustrade_params *p,
                                     const pcg_balustrade_path *path);

/**
 * define pcg_balustrade_path_emit - emit a balustrade along a path described by pcg_balustrade_path initializers
 *
 * Syntax sugar for _pcg_balustrade_path_emit().
 */
#define pcg_balustrade_path_emit(_ctx, _p, args...) \
    _pcg_balustrade_path_emit((_ctx), (_p), &(const pcg_balustrade_path){ args })

#endif /* __CLAP_PCG_BALUSTRADE_H__ */
